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

#include <stdio.h>
#include <string.h>

#include "mpi.h"

#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"
#include "opal/mca/smsc/base/base.h"

#include "opal/util/arch.h"
#include "opal/util/show_help.h"
#include "opal/util/minmax.h"

#include "coll_xhc.h"

static void mca_coll_xhc_module_construct(mca_coll_xhc_module_t *module) {
	module->hierarchy = NULL;
	module->hierarchy_len = 0;
	
	module->chunks = NULL;
	module->chunks_len = 0;
	
	module->data = NULL;
	module->initialized = false;
}

static void mca_coll_xhc_module_destruct(mca_coll_xhc_module_t *module) {
	if(module->initialized) {
		xhc_destroy_data(module);
		
		free(module->hierarchy);
		free(module->chunks);
		
		module->hierarchy_len = 0;
		module->chunks_len = 0;
		
		module->initialized = false;
	}
	
	OBJ_RELEASE_IF_NOT_NULL(module->prev_colls.coll_allreduce_module);
	OBJ_RELEASE_IF_NOT_NULL(module->prev_colls.coll_barrier_module);
	OBJ_RELEASE_IF_NOT_NULL(module->prev_colls.coll_bcast_module);
	OBJ_RELEASE_IF_NOT_NULL(module->prev_colls.coll_reduce_module);
}

OBJ_CLASS_INSTANCE(mca_coll_xhc_module_t,
	mca_coll_base_module_t,
	mca_coll_xhc_module_construct,
	mca_coll_xhc_module_destruct);

mca_coll_base_module_t *mca_coll_xhc_module_comm_query(
		struct ompi_communicator_t *comm, int *priority) {
	
	if((*priority = mca_coll_xhc_component.priority) < 0)
		return NULL;
	
	if(OMPI_COMM_IS_INTER(comm) || ompi_comm_size(comm) == 1
			|| ompi_group_have_remote_peers (comm->c_local_group)) {
        
        opal_output_verbose(MCA_BASE_VERBOSE_COMPONENT,
			ompi_coll_base_framework.framework_output,
			"coll:xhc:comm_query (%s/%s): intercomm, self-comm, "
			"or not all ranks local; disqualifying myself",
			ompi_comm_print_cid(comm), comm->c_name);
        
        return NULL;
    }
    
    int comm_size = ompi_comm_size(comm);
	for(int r = 0; r < comm_size; r++) {
		ompi_proc_t *proc = ompi_comm_peer_lookup(comm, r);
		
		if(proc->super.proc_arch != opal_local_arch) {
			opal_output_verbose(MCA_BASE_VERBOSE_COMPONENT,
				ompi_coll_base_framework.framework_output,
				"coll:xhc:comm_query (%s/%s): All ranks not of the same arch; "
				"disabling myself", ompi_comm_print_cid(comm), comm->c_name);
			
			opal_show_help("help-coll-xhc.txt", "xhc-diff-arch",
				true, OPAL_PROC_MY_HOSTNAME, opal_local_arch,
				opal_get_proc_hostname(&proc->super), proc->super.proc_arch);
			
			return NULL;
		}
	}
	
	mca_coll_base_module_t *module =
		(mca_coll_base_module_t *) OBJ_NEW(mca_coll_xhc_module_t);
	
	if(module == NULL)
		return NULL;
	
	module->coll_module_enable = mca_coll_xhc_module_enable;
	module->coll_barrier = mca_coll_xhc_barrier;
	
	if(mca_smsc == NULL)
		mca_smsc_base_select();
	
	if(mca_smsc == NULL) {
		opal_show_help("help-coll-xhc.txt", "xhc-no-smsc", true);
		return module;
	}
	
	module->coll_bcast = mca_coll_xhc_bcast;
	
	if(!mca_smsc_base_has_feature(MCA_SMSC_FEATURE_CAN_MAP)) {
		opal_show_help("help-coll-xhc.txt", "xhc-smsc-no-map", true);
		return module;
	}
	
	module->coll_allreduce = mca_coll_xhc_allreduce;
	module->coll_reduce = mca_coll_xhc_reduce;
	
	return module;
}

#define SAVE_COLL(_dst, _f) do { \
	_dst.coll_ ## _f = comm->c_coll->coll_ ## _f; \
	_dst.coll_ ## _f ## _module = comm->c_coll->coll_ ## _f ## _module; \
	\
	if(!_dst.coll_ ## _f || !_dst.coll_ ## _f ## _module) status = OMPI_ERROR; \
	if(_dst.coll_ ## _f ## _module) OBJ_RETAIN(_dst.coll_ ## _f ## _module); \
} while(0)

#define SET_COLL(_src, _f) do { \
	comm->c_coll->coll_ ## _f = _src.coll_ ## _f; \
	comm->c_coll->coll_ ## _f ## _module = _src.coll_ ## _f ## _module; \
} while(0)

int mca_coll_xhc_module_enable(mca_coll_base_module_t *module,
		ompi_communicator_t *comm) {
	
	mca_coll_xhc_module_t *xhc_module = (mca_coll_xhc_module_t *) module;
	
	int status = OMPI_SUCCESS;
	
	SAVE_COLL(xhc_module->prev_colls, barrier);
	SAVE_COLL(xhc_module->prev_colls, bcast);
	SAVE_COLL(xhc_module->prev_colls, allreduce);
	SAVE_COLL(xhc_module->prev_colls, reduce);
	
	if(status != OMPI_SUCCESS) {
		opal_show_help("help-coll-xhc.txt", "xhc-module-save-error", true);
		return status;
	}
	
	/* This was intially performed inside lazy_init(). However, by then,
	 * a possibly present info key will have been deleted, as it won't have
	 * been referenced during the communicator's initialization (see PR #9567).
	 * Nevertheless, currently, there's no drawback to having it here instead. */
	if(xhc_module_prepare_hierarchy(xhc_module, comm) != 0) {
		status = OMPI_ERROR;
		return status;
	}
	
	if(xhc_component_parse_chunk_sizes(mca_coll_xhc_component.chunk_size_mca,
			&xhc_module->chunks, &xhc_module->chunks_len) != 0) {
		status = OMPI_ERROR;
		return status;
	}
	
	return status;
}

xhc_coll_fns_t xhc_module_set_fns(ompi_communicator_t *comm,
		xhc_coll_fns_t new) {
	
	xhc_coll_fns_t current;
	int status;
	
	SAVE_COLL(current, barrier);
	SAVE_COLL(current, bcast);
	SAVE_COLL(current, allreduce);
	SAVE_COLL(current, reduce);
	
	(void) status; // ignore
	
	SET_COLL(new, barrier);
	SET_COLL(new, bcast);
	SET_COLL(new, allreduce);
	SET_COLL(new, reduce);
	
	return current;
}

int xhc_module_prepare_hierarchy(mca_coll_xhc_module_t *module,
		ompi_communicator_t *comm) {
	
	xhc_loc_t *hier = NULL;
	int hier_len;
	
	xhc_loc_t *new_hier = NULL;
	bool *hier_done = NULL;
	
	int return_code = 0;
	int ret;
	
	// ----
	
	const char *hmca = mca_coll_xhc_component.hierarchy_mca;
	
	opal_cstring_t *hier_info;
	int hier_info_flag = 0;
	
	if(comm->super.s_info != NULL) {
		opal_info_get(comm->super.s_info, "ompi_comm_coll_xhc_hierarchy",
			&hier_info, &hier_info_flag);
		
		if(hier_info_flag)
			hmca = hier_info->string;
	}
	
	ret = xhc_component_parse_hierarchy(hmca, &hier, &hier_len);
	if(ret != 0) RETURN_WITH_ERROR(return_code, -1, end);
	
	// ----
	
	new_hier = malloc((hier_len + 1) * sizeof(xhc_loc_t));
	hier_done = calloc(hier_len, sizeof(bool));
	
	if(new_hier == NULL || hier_done == NULL)
		RETURN_WITH_ERROR(return_code, -3, end);
	
	int comm_size = ompi_comm_size(comm);
	
	for(int new_idx = hier_len - 1; new_idx >= 0; new_idx--) {
		int max_matches_count = -1;
		int max_matches_hier_idx;
		
		for(int i = 0; i < hier_len; i++) {
			if(hier_done[i])
				continue;
			
			int matches = 0;
			
			for(int r = 0; r < comm_size; r++) {
				if(RANK_IS_LOCAL(comm, r, hier[i]))
					matches++;
			}
			
			if(matches > max_matches_count) {
				max_matches_count = matches;
				max_matches_hier_idx = i;
			}
		}
		
		if(max_matches_count == -1) {
			opal_show_help("help-coll-xhc.txt", "xhc-hier-order-error", true);
			RETURN_WITH_ERROR(return_code, -4, end);
		}
		
		new_hier[new_idx] = hier[max_matches_hier_idx];
		hier_done[max_matches_hier_idx] = true;
	}
	
	xhc_loc_t common_locality = 0xFFFF;
	
	for(int r = 0; r < comm_size; r++) {
		ompi_proc_t *proc = ompi_comm_peer_lookup(comm, r);
		common_locality &= proc->super.proc_flags;
	}
	
	if(common_locality == 0) {
		opal_show_help("help-coll-xhc.txt", "xhc-no-common-locality", true);
		RETURN_WITH_ERROR(return_code, -5, end);
	}
	
	if(hier_len == 0 || (common_locality & new_hier[hier_len - 1]) != new_hier[hier_len - 1]) {
		new_hier[hier_len] = common_locality;
		hier_len++;
	}
	
	module->hierarchy = new_hier;
	module->hierarchy_len = hier_len;
	
end:
	
	free(hier);
	
	if(hier_info_flag)
		OBJ_RELEASE(hier_info);
	
	free(hier_done);
	
	if(return_code != 0)
		free(new_hier);
	
	return return_code;
}
