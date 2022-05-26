#include "ompi_config.h"
#include "mpi.h"

#include "ompi/constants.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/communicator/communicator.h"
#include "ompi/op/op.h"

#include "opal/mca/rcache/base/base.h"
#include "opal/util/minmax.h"
#include <opal/sys/atomic.h>

#include "coll_xbrc.h"

static void xbrc_allreduce_reduce_index(xbrc_rank_info_t *rank_info,
		int rank, int comm_size, void *rbuf, int index,
		int length, ompi_datatype_t *dtype, size_t dtype_size, ompi_op_t *op) {
	
	char *dst = (char *) rbuf + index * dtype_size;
	
	char *self_src = (char *) rank_info[rank].reduce_info.sbuf + index * dtype_size;
	
	/* 1. If rank == 0 and not MPI_IN_PLACE: copy from own sbuf to own rbuf
	 * 2. If rank == 0 and MPI_IN_PLACE: nop
	 * 3. If rank != 0 and not MPI_IN_PLACE: 0's sbuf + own sbuf -> own rbuf
	 * 4. If rank != 0 and MPI_IN_PLACE: own rbuf += 0's sbuf */
	if(rank == 0) {
		if(self_src != dst)
			memcpy(dst, self_src, length * dtype_size);
	} else{
		char *r0_src = (char *) rank_info[0].reduce_info.sbuf + index * dtype_size;
		
		if(self_src != dst)
			ompi_3buff_op_reduce(op, r0_src, self_src, dst, length, dtype);
		else
			ompi_op_reduce(op, r0_src, dst, length, dtype);
	}
	
	for(int r = 1; r < comm_size; r++) {
		if(r == rank)
			continue;
		
		char *src = (char *) rank_info[r].reduce_info.sbuf + index * dtype_size;
		ompi_op_reduce(op, src, dst, length, dtype);
	}
}

static void xbrc_allreduce_copy_index(xbrc_rank_info_t *rank_info, int src_rank,
		void *rbuf, int index, int length, size_t dtype_size) {
	
	char *src = (char *) rank_info[src_rank].reduce_info.rbuf + (index * dtype_size);
	char *dst = (char *) rbuf + (index * dtype_size);
	
	memcpy(dst, src, length * dtype_size);
}

static int xbrc_allreduce_attach(xbrc_data_t *data, const void *sbuf,
		void *rbuf, size_t bytes_total, pf_sig_t seq) {
	
	// Attach to peers
	for(int r = 0; r < data->ompi_size; r++) {
		xbrc_reg_t *sbuf_reg = NULL, *rbuf_reg = NULL;
		void *sbuf_r = NULL, *rbuf_r = NULL;
		
		if(r != data->ompi_rank) {
			mca_rcache_base_module_t *rcache = data->rank_info[r].rcache;
			
			WAIT_FLAG(data->rank_ctrl[r].coll_seq, seq, 0);
			opal_atomic_rmb();
			
			rbuf_r = mca_coll_xbrc_get_registration(rcache,
				data->rank_ctrl[r].rbuf_vaddr, bytes_total, &rbuf_reg);
			if(rbuf_reg == NULL) return -2;
			
			if(data->rank_ctrl[r].sbuf_vaddr != data->rank_ctrl[r].rbuf_vaddr) {
				sbuf_r = mca_coll_xbrc_get_registration(rcache,
					data->rank_ctrl[r].sbuf_vaddr, bytes_total, &sbuf_reg);
				if(sbuf_r == NULL) return -1;
			} else
				sbuf_r = rbuf_r;
		} else {
			sbuf_r = (void *) sbuf;
			rbuf_r = rbuf;
		}
		
		data->rank_info[r].reduce_info = (xbrc_reduce_info_t) {
			.sbuf_reg = sbuf_reg, .sbuf = sbuf_r,
			.rbuf_reg = sbuf_reg, .rbuf = rbuf_r
		};
	}
	
	return 0;
}

int mca_coll_xbrc_allreduce(const void *sbuf, void *rbuf,
		int count, ompi_datatype_t *datatype, ompi_op_t *op,
		ompi_communicator_t *ompi_comm, mca_coll_base_module_t *module) {
	
	if(((mca_coll_xbrc_module_t *) module)->initialized == false) {
		int ret = mca_coll_xbrc_lazy_init(module, ompi_comm);
		if(ret != 0) return OMPI_ERROR;
	}
	
	xbrc_data_t *data = ((xbrc_module_t *) module)->data;
	
	size_t dtype_size, bytes_total;
	ompi_datatype_type_size(datatype, &dtype_size);
	bytes_total = count * dtype_size;
	
	int rank = data->ompi_rank;
	int comm_size = data->ompi_size;
	
	// ---
	
	pf_sig_t pvt_seq = ++data->pvt_coll_seq;
	
	if(sbuf == MPI_IN_PLACE)
		sbuf = rbuf;
	
	data->rank_ctrl[rank].sbuf_vaddr = (void *) sbuf;
	data->rank_ctrl[rank].rbuf_vaddr = rbuf;
	data->rank_ctrl[rank].reduction_complete = 0;
	data->rank_ctrl[rank].copy_complete = 0;
	
	opal_atomic_wmb();
	data->rank_ctrl[rank].coll_seq = pvt_seq;
	
	if(xbrc_allreduce_attach(data, sbuf, rbuf, bytes_total, pvt_seq) != 0)
		return OMPI_ERROR;
	
	// ---
	
	int chunk_size = (count / OMPI_XBRC_CHUNK_ALIGN / comm_size) * OMPI_XBRC_CHUNK_ALIGN;
	int surplus = count - chunk_size * comm_size;
	
	int surplus_chunk_size = surplus - rank * OMPI_XBRC_CHUNK_ALIGN;
	surplus_chunk_size = opal_min(surplus_chunk_size, OMPI_XBRC_CHUNK_ALIGN);
	surplus_chunk_size = opal_max(surplus_chunk_size, 0);
	
	// ---
	
	int surplus_cumulative_excess = opal_min(rank * OMPI_XBRC_CHUNK_ALIGN, surplus);
	int reduce_index = rank * chunk_size + surplus_cumulative_excess;
	int reduce_size = chunk_size + surplus_chunk_size;
	
	xbrc_allreduce_reduce_index(data->rank_info, rank, comm_size,
		rbuf, reduce_index, reduce_size, datatype, dtype_size, op);
	
	opal_atomic_wmb();
	data->rank_ctrl[rank].reduction_complete = 1;
	
	// ---
	
	int surplus_left = surplus;
	for(int index = 0, r = 0; index < count; r++) {
		int size = chunk_size + opal_min(surplus_left, OMPI_XBRC_CHUNK_ALIGN);
		
		if(r != rank) {
			while(data->rank_ctrl[r].reduction_complete != 1) {}
			opal_atomic_rmb();
			
			xbrc_allreduce_copy_index(data->rank_info, r,
				rbuf, index, size, dtype_size);
		}
		
		surplus_left -= (size - chunk_size);
		index += size;
	}
	
	opal_atomic_wmb();
	data->rank_ctrl[rank].copy_complete = 1;
	
	// ---
	
	if(rank == 0) {
		for(int r = 0; r < comm_size; r++) {
			if(r == rank)
				continue;
			
			while(data->rank_ctrl[r].copy_complete != 1) {}
		}
		
		// load-store control dependency with copy_complete loads
		*data->ack_num = pvt_seq;
	} else
		WAIT_FLAG(*data->ack_num, pvt_seq, 0);
	
	return OMPI_SUCCESS;
}
