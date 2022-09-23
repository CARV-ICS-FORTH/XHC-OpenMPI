/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "mpi.h"

#include "ompi/communicator/communicator.h"
#include "ompi/constants.h"
#include "ompi/op/op.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"

#include "opal/util/bit_ops.h"
#include "opal/sys/atomic.h"

#include "coll_smhc.h"

// ----------------------------------------

static void smhc_store_32(uint32_t *dst, uint32_t v) {
	opal_atomic_wmb();
	*dst = v;
}

static uint32_t smhc_load_32(uint32_t *src) {
	uint32_t v = *src;
	opal_atomic_rmb();
	return v;
}

static inline bool rank_is_same_hier(int rank, ompi_communicator_t *comm) {
	ompi_proc_t *proc = ompi_comm_peer_lookup(comm, rank);
	opal_hwloc_locality_t loc = mca_coll_smhc_component.tree_topo;
	
	return ((proc->super.proc_flags & loc) == loc);
}

static int smhc_tree_lazy_init(ompi_communicator_t *comm,
		mca_coll_base_module_t *module) {
	
	mca_coll_smhc_tree_data_t *data = NULL;
	
	opal_info_t comm_info;
	bool comm_info_obj = false;
	
	int return_code = 0;
	int ret;
	
	// ---
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	
	data = malloc(sizeof(mca_coll_smhc_tree_data_t));
	if(data == NULL) RETURN_WITH_ERROR(return_code, -101, end);
	
	*data = (mca_coll_smhc_tree_data_t) {
		.hier_comm = MPI_COMM_NULL,
		.flat_comm = MPI_COMM_NULL,
		.leaders = NULL,
		.leader_count = 0
	};
	
	data->leaders = malloc(comm_size * sizeof(int));
	if(data == NULL) RETURN_WITH_ERROR(return_code, -102, end);
	
	// ---
	
	// Comm split uses allgather, which might/will use bcast
	mca_coll_base_module_bcast_fn_t old_bcast_fn = comm->c_coll->coll_bcast;
	comm->c_coll->coll_bcast = ompi_coll_base_bcast_intra_basic_linear;
	
	// Create sub-communicators
	// ------------------------
	
	OBJ_CONSTRUCT(&comm_info, opal_info_t);
	comm_info_obj = true;
	
	ret = opal_info_set(&comm_info, "coll_smhc_impl", "flat");
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -103, end);
	
	/* Split the communicator according to the selected topology sensitivity */
	
	int split_type = -1;
	
	switch(mca_coll_smhc_component.tree_topo) {
		case OPAL_PROC_ON_NUMA:   split_type = OMPI_COMM_TYPE_NUMA; break;
		case OPAL_PROC_ON_SOCKET: split_type = OMPI_COMM_TYPE_SOCKET; break;
	}
	
	ret = ompi_comm_split_type(comm, split_type, 0,
		&comm_info, &data->hier_comm);
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -104, end);
	
	int hrank = ompi_comm_rank(data->hier_comm);
	int hcomm_size = ompi_comm_size(data->hier_comm);
	
	if(comm_size == hcomm_size) {
		opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:smhc:smhc_tree_lazy_init: Warning: The selected "
			"'%s' topology sensitivity results in a non-hierarchical "
			"communication pattern", mca_coll_smhc_component.tree_topo_param);
	}
	
	/* Mirror the original communicator, to leverage easy
	 * initialization of smhc (non-tree) data structures. */
	
	ret = ompi_comm_split_with_info(comm, 0, rank,
		&comm_info, &data->flat_comm, false);
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -105, end);
	
	OBJ_DESTRUCT(&comm_info);
	comm_info_obj = false;
	
	/* Trigger lazy initilization of smhc data */
	
	ret = data->hier_comm->c_coll->coll_bcast(NULL, 0, MPI_INT, 0,
		data->hier_comm, data->hier_comm->c_coll->coll_bcast_module);
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -106, end);
	
	ret = data->flat_comm->c_coll->coll_bcast(NULL, 0, MPI_INT, 0,
		data->flat_comm, data->flat_comm->c_coll->coll_bcast_module);
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -107, end);
	
	// The group leaders broadcast their rank (from the original comm)
	// --------------------------------------------------------------
	
	if(hrank == 0)
		data->my_leader = rank;
	
	ret = data->hier_comm->c_coll->coll_bcast(&data->my_leader, 1, MPI_INT,
		0, data->hier_comm, data->hier_comm->c_coll->coll_bcast_module);
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -108, end);
	
	// All ranks create a list of all leader ranks
	// -------------------------------------------
	
	int am_i_a_leader = (hrank == 0 ? rank : -1);
	
	ret = comm->c_coll->coll_allgather(&am_i_a_leader, 1, MPI_INT,
		data->leaders, 1, MPI_INT, comm, comm->c_coll->coll_allgather_module);
	
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -109, end);
	
	for(int i = 0; i < comm_size; i++) {
		if(data->leaders[i] >= 0)
			data->leaders[data->leader_count++] = data->leaders[i];
	}
	
	void *rp = realloc(data->leaders, data->leader_count * sizeof(int));
	if(rp) data->leaders = rp;
	
	// ---
	
	comm->c_coll->coll_bcast = old_bcast_fn;
	
	((mca_coll_smhc_module_t *) module)->tree_data = data;
	((mca_coll_smhc_module_t *) module)->tree_initialized = true;
	
	// ---
	
	end:
	
	if(return_code != 0) {
		opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:smhc: ERROR %d at smhc_tree_lazy_init [%s]",
			return_code, strerror(errno));
		
		if(comm_info_obj)
			OBJ_DESTRUCT(&comm_info);
		
		if(data) {
			free(data->leaders);
			
			if(data->hier_comm != MPI_COMM_NULL)
				ompi_comm_free(&data->hier_comm);
			
			if(data->flat_comm != MPI_COMM_NULL)
				ompi_comm_free(&data->flat_comm);
		}
		
		free(data);
	}
	
	return return_code;
}

// ----------------------------------------

int mca_coll_smhc_bcast_tree(void *buf, int count,
		struct ompi_datatype_t *datatype, int root,
		struct ompi_communicator_t *comm,
		mca_coll_base_module_t *module) {
	
	mca_coll_smhc_module_t *smhc_module = (mca_coll_smhc_module_t *) module;
	
	mca_coll_smhc_tree_data_t *data;
	mca_coll_smhc_flat_data_t *hier_data, *flat_data;
	
	if(smhc_module->tree_initialized == false) {
		int ret = smhc_tree_lazy_init(comm, module);
		if(ret != 0) return OMPI_ERROR;
	}
	
	data = smhc_module->tree_data;
	
	hier_data = ((mca_coll_smhc_module_t *) data->hier_comm
		->c_coll->coll_bcast_module)->flat_data;
	
	flat_data = ((mca_coll_smhc_module_t *) data->flat_comm
		->c_coll->coll_bcast_module)->flat_data;
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	
	int hrank = ompi_comm_rank(data->hier_comm);
	int hcomm_size = ompi_comm_size(data->hier_comm);
	
	/* If no actual hierarchy is present, delegate the
	 * broadcast to the non-hierarchical implementation */
	if(comm_size == hcomm_size) {
		mca_coll_base_comm_coll_t *c_coll = data->flat_comm->c_coll;
		
		return c_coll->coll_bcast(buf, count, datatype, root,
			comm, c_coll->coll_bcast_module);
	}
	
	size_t dtype_size;
	ompi_datatype_type_size(datatype, &dtype_size);
	
	size_t msg_bytes = count * dtype_size;
	size_t chunk_size = SMHC_CHUNK;
	
	uint32_t flat_initial_pvt_state = flat_data->pvt_release_state;
	uint32_t hier_initial_pvt_state = hier_data->pvt_release_state;
	
	/* Wait until the previous operation is over, before touching
	 * the bcast buffer or the communication flags (keep in mind
	 * this op's root might be different than last one's). */
	if(rank == root)
		while(smhc_load_32(&flat_data->sh_root_gather->flag) != flat_initial_pvt_state) {}
	
	uint32_t sh_r, sh_g;
	uint32_t pvt_r, pvt_g;
	
	for(size_t b = 0; b < msg_bytes; b += chunk_size) {
		flat_data->pvt_release_state++;
		hier_data->pvt_release_state++;
		
		void *user_buf_p = (void *) ((uintptr_t) buf + b);
		
		size_t to_copy = (b + chunk_size < msg_bytes ?
			chunk_size : (msg_bytes - b));
		
		int flat_chunk_id = (flat_data->pvt_release_state
			- flat_initial_pvt_state - 1) % SMHC_PIPELINE;
		int hier_chunk_id = (hier_data->pvt_release_state
			- hier_initial_pvt_state - 1) % SMHC_PIPELINE;
		
		size_t flat_sh_buf_offset = flat_chunk_id * chunk_size;
		size_t hier_sh_buf_offset = hier_chunk_id * chunk_size;
		
		void *flat_sh_buf_p = (void *) ((uintptr_t) flat_data
			->sh_bcast_buffer + flat_sh_buf_offset);
		void *hier_sh_buf_p = (void *) ((uintptr_t) hier_data
			->sh_bcast_buffer + hier_sh_buf_offset);
		
		if(rank == root) {
			/* The root sends the chunk to the rest of the leaders. */
			
			memcpy(flat_sh_buf_p, user_buf_p, to_copy);
			
			// Release to leaders, through the flat comm
			for(size_t l = 0; l < data->leader_count; l++) {
				int r = data->leaders[l];
				
				if(rank_is_same_hier(r, comm))
					continue;
				
				smhc_store_32(&flat_data->sh_release[r].flag,
					flat_data->pvt_release_state);
			}
			
			// We don't perform a gather step until the pipeline is full
			if(b >= chunk_size * (SMHC_PIPELINE - 1)) {
				/* Gather step for some previous chunk (probably not the
				 * one we just released, because there is pipelining).*/
				
				flat_data->pvt_gather_state++;
				
				// Gather from leaders
				for(size_t l = 0; l < data->leader_count; l++) {
					int r = data->leaders[l];
				
					if(rank_is_same_hier(r, comm))
						continue;
					
					do {
						sh_g = smhc_load_32(&flat_data->sh_gather[r].flag);
						pvt_g = flat_data->pvt_gather_state;
					} while((uint32_t) (sh_g - pvt_g) >= SMHC_PIPELINE);
				}
			}
		}
		
		if(rank == root || (hrank == 0 && !rank_is_same_hier(root, comm))) {
			/* The leaders send the chunk to their group peers.
			 * The root acts as the leader of its group. */
			
			if(rank != root) {
				/* Copy from flat comm, written by root */
				
				do {
					sh_r = smhc_load_32(&flat_data->sh_release[rank].flag);
					pvt_r = flat_data->pvt_release_state;
				} while((uint32_t) (sh_r - pvt_r) >= SMHC_PIPELINE);
				
				memcpy(user_buf_p, flat_sh_buf_p, to_copy);
				
				flat_data->pvt_gather_state++;
				smhc_store_32(&flat_data->sh_gather[rank].flag,
					flat_data->pvt_gather_state);
			}
			
			memcpy(hier_sh_buf_p, user_buf_p, to_copy);
			
			// Release to group peers
			for(int r = 0; r < hcomm_size; r++) {
				if((rank == root ? (r == hrank) : (r == 0)))
					continue;
				
				smhc_store_32(&hier_data->sh_release[r].flag,
					hier_data->pvt_release_state);
			}
			
			// We don't perform a gather step until the pipeline is full
			if(b >= chunk_size * (SMHC_PIPELINE - 1)) {
				hier_data->pvt_gather_state++;
				
				// Gather from group peers
				for(int r = 0; r < hcomm_size; r++) {
					if((rank == root ? (r == hrank) : (r == 0)))
						continue;
					
					do {
						sh_g = smhc_load_32(&hier_data->sh_gather[r].flag);
						pvt_g = hier_data->pvt_gather_state;
					} while((uint32_t) (sh_g - pvt_g) >= SMHC_PIPELINE);
				}
			}
		} else {
			do {
				sh_r = smhc_load_32(&hier_data->sh_release[hrank].flag);
				pvt_r = hier_data->pvt_release_state;
			} while((uint32_t) (sh_r - pvt_r) >= SMHC_PIPELINE);
			
			memcpy(user_buf_p, hier_sh_buf_p, to_copy);
			
			hier_data->pvt_gather_state++;
			flat_data->pvt_gather_state++;
			
			smhc_store_32(&hier_data->sh_gather[hrank].flag,
				hier_data->pvt_gather_state);
		}
	}
	
	assert(flat_data->pvt_release_state >= flat_data->pvt_gather_state);
	assert(hier_data->pvt_release_state >= hier_data->pvt_gather_state);
	
	/* Gather chunks still in-the-air before exiting */
	
	if(hier_data->pvt_release_state != hier_data->pvt_gather_state) {
		hier_data->pvt_gather_state = hier_data->pvt_release_state;
		
		// Gather from group peers
		for(int r = 0; r < hcomm_size; r++) {
			if((rank == root ? (r == hrank) : (r == 0)))
				continue;
			
			do {
				sh_g = smhc_load_32(&hier_data->sh_gather[r].flag);
			} while(sh_g != hier_data->pvt_gather_state);
		}
	}
	
	if(flat_data->pvt_release_state != flat_data->pvt_gather_state) {
		flat_data->pvt_gather_state = flat_data->pvt_release_state;
		
		// Gather from leaders
		for(size_t l = 0; l < data->leader_count; l++) {
			int r = data->leaders[l];
			
			if(rank_is_same_hier(r, comm))
				continue;
			
			do {
				sh_g = smhc_load_32(&flat_data->sh_gather[r].flag);
			} while(sh_g != flat_data->pvt_gather_state);
		}
	}
	
	// Operations up until <gather_state> are officially over!
	if(rank == root)
		smhc_store_32(&flat_data->sh_root_gather->flag, flat_data->pvt_gather_state);
	
	assert(flat_data->pvt_release_state == flat_data->pvt_gather_state);
	assert(hier_data->pvt_release_state == hier_data->pvt_gather_state);
	
	return OMPI_SUCCESS;
}
