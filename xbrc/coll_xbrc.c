#include "ompi_config.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"
#include "ompi/mca/coll/base/coll_base_functions.h"

#include "opal/mca/rcache/base/base.h"
#include "opal/include/opal/align.h"
#include "opal/util/show_help.h"

#include "coll_xbrc.h"

static void *xbrc_attach(xpmem_apid_t apid, void *peer_vaddr, size_t size);
static void xbrc_detach(void *local_vaddr);

static int xbrc_rcache_dereg_mem_cb(void *reg_data,
	mca_rcache_base_registration_t *reg);
static int xbrc_rcache_reg_mem_cb(void *reg_data, void *base,
	size_t size, mca_rcache_base_registration_t *reg);

int mca_coll_xbrc_lazy_init(mca_coll_base_module_t *module, ompi_communicator_t *comm) {
	xbrc_data_t *data = NULL;
	
	char *rcache_name = NULL;
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	int comm_id = ompi_comm_get_local_cid(comm);
	
	int return_code = 0;
	int ret;
	
	errno = 0;
	
	// ----
	
	mca_coll_base_module_allgather_fn_t old_allgather = comm->c_coll->coll_allgather;
	mca_coll_base_module_t *old_allgather_module = comm->c_coll->coll_allgather_module;
	mca_coll_base_module_gather_fn_t old_gather = comm->c_coll->coll_gather;
	mca_coll_base_module_t *old_gather_module = comm->c_coll->coll_gather_module;
	mca_coll_base_module_bcast_fn_t old_bcast = comm->c_coll->coll_bcast;
	mca_coll_base_module_t *old_bcast_module = comm->c_coll->coll_bcast_module;
	
	comm->c_coll->coll_allgather = ompi_coll_base_allgather_intra_basic_linear;
	comm->c_coll->coll_allgather_module = module;
	comm->c_coll->coll_gather = ompi_coll_base_gather_intra_basic_linear;
	comm->c_coll->coll_gather_module = module;
	comm->c_coll->coll_bcast = ompi_coll_base_bcast_intra_basic_linear;
	comm->c_coll->coll_bcast_module = module;
	
	if((module->base_data = OBJ_NEW(mca_coll_base_comm_t)) == NULL)
		RETURN_WITH_ERROR(return_code, -1, end);
	
	// ----
	
	data = malloc(sizeof(xbrc_data_t));
	if(data == NULL)
		RETURN_WITH_ERROR(return_code, -2, end);
	
	*data = (xbrc_data_t) {
		.pvt_coll_seq = 0,
		
		.ompi_rank = rank,
		.ompi_size = comm_size,
		
		.rank_info = NULL,
		.rank_ctrl = NULL,
		.ack_num = NULL
	};
	
	// ----
	
	data->rank_info = malloc(comm_size * sizeof(xbrc_rank_info_t));
	
	if(data->rank_info == NULL)
		RETURN_WITH_ERROR(return_code, -3, end);
	
	xbrc_rank_info_t my_rank_info = (xbrc_rank_info_t) {
		.seg_id = xpmem_make(0, XPMEM_MAXADDR_SIZE,
			XPMEM_PERMIT_MODE, (void *) 0666),
		
		.max_address = XPMEM_MAXADDR_SIZE,
		
		.apid = -1,
		
		.rcache = NULL
	};
	
	if(my_rank_info.seg_id == -1)
		RETURN_WITH_ERROR(return_code, -4, end);
	
	ret = comm->c_coll->coll_allgather(&my_rank_info,
		sizeof(xbrc_rank_info_t), MPI_BYTE, data->rank_info,
		sizeof(xbrc_rank_info_t), MPI_BYTE, comm,
		comm->c_coll->coll_allgather_module);
	
	if(ret != OMPI_SUCCESS)
		RETURN_WITH_ERROR(return_code, -5, end);
	
	// ----
	
	size_t max_rcache_name = snprintf(NULL, 0,
		"xbrc %d %d", comm_id, comm_size);
	
	rcache_name = malloc(max_rcache_name + 1);
	
	for(int r = 0; r < comm_size; r++) {
		if(r == rank) continue;
		
		data->rank_info[r].apid = xpmem_get(data->rank_info[r].seg_id,
			XPMEM_RDWR, XPMEM_PERMIT_MODE, (void *) 0666);
		
		if(data->rank_info[r].apid == -1)
			RETURN_WITH_ERROR(return_code, -6, end);
		
		mca_rcache_base_resources_t rcache_resources = {
			.cache_name = rcache_name,
			.reg_data = &data->rank_info[r],
			.sizeof_reg = sizeof(xbrc_reg_t),
			.register_mem = xbrc_rcache_reg_mem_cb,
			.deregister_mem = xbrc_rcache_dereg_mem_cb
		};
		
		snprintf(rcache_resources.cache_name, max_rcache_name + 1,
			"xbrc %d %d", comm_id, r);
		
		data->rank_info[r].rcache = mca_rcache_base_module_create ("grdma",
			NULL, &rcache_resources);
		
		if(data->rank_info[r].rcache == NULL)
			RETURN_WITH_ERROR(return_code, -7, end);
	}
	
	// ----
	
	void *sync_flags_vaddr[2] = {0};
	
	if(rank == 0) {
		ret = posix_memalign((void **) &data->rank_ctrl,
			OMPI_XBRC_CTRL_ALIGN, comm_size * sizeof(xbrc_rank_ctrl_t));
		
		if(ret != 0) RETURN_WITH_ERROR(return_code, -8, end);
		
		ret = posix_memalign((void **) &data->ack_num,
			OMPI_XBRC_CTRL_ALIGN, sizeof(pf_sig_t));
		
		if(ret != 0) RETURN_WITH_ERROR(return_code, -9, end);
		
		sync_flags_vaddr[0] = (void *) data->rank_ctrl;
		sync_flags_vaddr[1] = (void *) data->ack_num;
		
		for(int r = 0; r < comm_size; r++)
			data->rank_ctrl[r].coll_seq = 0;
		
		*data->ack_num = data->pvt_coll_seq;
	}
	
	ret = comm->c_coll->coll_bcast(sync_flags_vaddr, sizeof sync_flags_vaddr,
		MPI_BYTE, 0, comm, comm->c_coll->coll_bcast_module);
	
	if(ret != 0) RETURN_WITH_ERROR(return_code, -10, end);
	
	if(rank != 0) {
		data->rank_ctrl = xbrc_attach(data->rank_info[0].apid,
			sync_flags_vaddr[0], comm_size * sizeof(xbrc_rank_ctrl_t));
		
		if(data->rank_ctrl == NULL)
			RETURN_WITH_ERROR(return_code, -11, end);
		
		data->ack_num = xbrc_attach(data->rank_info[0].apid,
			sync_flags_vaddr[1], comm_size * sizeof(xbrc_rank_ctrl_t));
		
		if(data->ack_num == NULL)
			RETURN_WITH_ERROR(return_code, -12, end);
	}
	
	// ----
	
	((xbrc_module_t *) module)->data = data;
	((xbrc_module_t *) module)->initialized = true;
	
	end:
	
	free(rcache_name);
	
	comm->c_coll->coll_allgather = old_allgather;
	comm->c_coll->coll_allgather_module = old_allgather_module;
	comm->c_coll->coll_gather = old_gather;
	comm->c_coll->coll_gather_module = old_gather_module;
	comm->c_coll->coll_bcast = old_bcast;
	comm->c_coll->coll_bcast_module = old_bcast_module;
	
	if(module->base_data)
		OBJ_RELEASE(module->base_data);
	
	if(return_code != 0)
		mca_coll_xbrc_destroy_data((xbrc_module_t *) module);
	
	return return_code;
}

void mca_coll_xbrc_destroy_data(mca_coll_xbrc_module_t *module) {
	xbrc_data_t *data = module->data;
	
	if(!data)
		return;
	
	if(data->rank_info) {
		for(int r = 0; r < data->ompi_size; r++) {
			xpmem_apid_t apid = data->rank_info[r].apid;
			if(apid != -1) xpmem_release(apid);
			
			if(data->rank_info[r].rcache != NULL)
				mca_rcache_base_module_destroy(data->rank_info[r].rcache);
		}
		
		xpmem_apid_t segid = data->rank_info[data->ompi_rank].seg_id;
		if(segid != -1) xpmem_remove(segid);
		
		free(data->rank_info);
	}
	
	if(data->ompi_rank == 0) {
		free(data->rank_ctrl);
		free((void *) data->ack_num);
	} else {
		if(data->rank_ctrl) xbrc_detach(data->rank_ctrl);
		if(data->ack_num) xbrc_detach((void *) data->ack_num);
	}
	
	free(data);
}

static void *xbrc_attach(xpmem_apid_t apid, void *peer_vaddr, size_t size) {
	uintptr_t base = OPAL_DOWN_ALIGN((uintptr_t) peer_vaddr,
		mca_coll_xbrc_component.xpmem_align, uintptr_t);
	
	uintptr_t bound = OPAL_ALIGN((uintptr_t) peer_vaddr + size,
		mca_coll_xbrc_component.xpmem_align, uintptr_t);
	
	xpmem_addr_t xaddr = {.apid = apid, .offset = base};
	void *local_vaddr = xpmem_attach(xaddr, bound - base, NULL);
	
	if(local_vaddr == (void *) -1)
		return NULL;
	
	return (void *) ((uintptr_t) local_vaddr
		+ ((uintptr_t) peer_vaddr - base));
}

static void xbrc_detach(void *local_vaddr) {
	uintptr_t base = OPAL_DOWN_ALIGN((uintptr_t) local_vaddr,
		mca_coll_xbrc_component.xpmem_align, uintptr_t);
	
	xpmem_detach((void *) base);
}

static int xbrc_rcache_reg_mem_cb(void *reg_data, void *base,
		size_t size, mca_rcache_base_registration_t *reg) {
	
	xbrc_reg_t *xbrc_reg = (xbrc_reg_t *) reg;
	xbrc_rank_info_t *rank_info = (xbrc_rank_info_t *) reg_data;
	
	xpmem_addr_t xaddr = {.apid = rank_info->apid, .offset = (uintptr_t) base};
	xbrc_reg->attach_base = xpmem_attach(xaddr, size, NULL);
	
	if(xbrc_reg->attach_base == (void *) -1) {
		opal_show_help("help-coll-xbrc.txt", "xbrc-attach-error", true);
		return OPAL_ERROR;
	}
	
	return OPAL_SUCCESS;
}

static int xbrc_rcache_dereg_mem_cb(void *reg_data,
		mca_rcache_base_registration_t *reg) {
	
	xpmem_detach(((xbrc_reg_t *) reg)->attach_base);
	return OPAL_SUCCESS;
}

// Will perform lookup and only attach if necessary
void *mca_coll_xbrc_get_registration(mca_rcache_base_module_t *rcache,
		void *peer_vaddr, size_t size, xbrc_reg_t **reg) {
	
	uint32_t flags = 0;
	
	uintptr_t base = OPAL_DOWN_ALIGN((uintptr_t) peer_vaddr,
		mca_coll_xbrc_component.xpmem_align, uintptr_t);
	
	uintptr_t bound = OPAL_ALIGN((uintptr_t) peer_vaddr + size,
		mca_coll_xbrc_component.xpmem_align, uintptr_t);
	
	int ret = rcache->rcache_register(rcache, (void *) base, bound - base, flags,
		MCA_RCACHE_ACCESS_ANY, (mca_rcache_base_registration_t **) reg);
	
	if(ret != OPAL_SUCCESS) {
		*reg = NULL;
		return NULL;
	}
	
	return (void *) ((uintptr_t) (*reg)->attach_base
		+ ((uintptr_t) peer_vaddr - (uintptr_t) (*reg)->super.base));
}

/* As long as MCA leave_pinned=1 (but also watch out for registration flags),
 * this will not actually detach or erase from the cache, but rather simply
 * reduce the ref count and add to the eviction LRU if it is 0. */
void mca_coll_xbrc_return_registration(xbrc_reg_t *reg) {
	mca_rcache_base_module_t *rcache = reg->super.rcache;
	rcache->rcache_deregister(rcache, (mca_rcache_base_registration_t *) reg);
}
