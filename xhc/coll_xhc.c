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

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"

#include "opal/mca/rcache/rcache.h"
#include "opal/mca/shmem/base/base.h"
#include "opal/mca/smsc/smsc.h"

#include "opal/include/opal/align.h"
#include "opal/util/show_help.h"
#include "opal/util/minmax.h"

#include "coll_xhc.h"

static void *xhc_shmem_create(opal_shmem_ds_t *seg_ds, size_t size,
	ompi_communicator_t *ompi_comm, const char *name_chr_s, int name_chr_i);
static void *xhc_shmem_attach(opal_shmem_ds_t *seg_ds);
static mca_smsc_endpoint_t *xhc_smsc_ep(xhc_rank_info_t *rank_info);

static int xhc_make_comms(ompi_communicator_t *ompi_comm,
	xhc_rank_info_t *rank_info, xhc_comm_t *comms,
	xhc_loc_t *hierarchy, int hierarchy_len);

static void xhc_destroy_comms(xhc_comm_t *comms, int comm_count);

int xhc_lazy_init(mca_coll_base_module_t *module,
		ompi_communicator_t *comm) {
	
	xhc_module_t *xhc_module = (xhc_module_t *) module;
	xhc_data_t *data = NULL;
	
	int rank = ompi_comm_rank(comm);
	int comm_size = ompi_comm_size(comm);
	
	int return_code = OMPI_SUCCESS;
	int ret;
	
	errno = 0;
	
	// ----
	
	/* XHC requires rank communication during its initialization.
	 * Temporarily apply the saved fallback collective modules,
	 * and restore XHC's after initialization is done. */
	
	xhc_coll_fns_t xhc_fns = xhc_module_set_fns(comm, xhc_module->prev_colls);
	
	// ----
	
	data = malloc(sizeof(xhc_data_t));
	if(data == NULL)
		RETURN_WITH_ERROR(return_code, -1, end);
	
	*data = (xhc_data_t) {
		.comms = NULL,
		.comm_count = -1,
		
		.rank_info = NULL,
		.rank_info_count = -1,
		
		.ompi_rank = rank,
		.pvt_coll_seq = 0
	};
	
	data->rank_info = calloc(comm_size, sizeof(xhc_rank_info_t));
	
	if(data->rank_info == NULL)
		RETURN_WITH_ERROR(return_code, -2, end);
	
	if(OMPI_XHC_CICO_MAX > 0) {
		xhc_rank_info_t my_rank_info = (xhc_rank_info_t) {0};
		
		void *my_cico = xhc_shmem_create(&my_rank_info.cico_ds,
			OMPI_XHC_CICO_MAX, comm, "cico", 0);
		
		if(my_cico == NULL) RETURN_WITH_ERROR(return_code, -3, end);
		
		/* Manually "touch" to assert allocation in local NUMA node
		 * (assuming linux's default firt-touch-alloc NUMA policy) */
		memset(my_cico, 0, OMPI_XHC_CICO_MAX);
		
		ret = comm->c_coll->coll_allgather(&my_rank_info,
			sizeof(xhc_rank_info_t), MPI_BYTE, data->rank_info,
			sizeof(xhc_rank_info_t), MPI_BYTE, comm,
			comm->c_coll->coll_allgather_module);
		
		if(ret != OMPI_SUCCESS) {
			opal_shmem_unlink(&my_rank_info.cico_ds);
			opal_shmem_segment_detach(&my_rank_info.cico_ds);
			
			RETURN_WITH_ERROR(return_code, -4, end);
		}
		
		data->rank_info[rank].cico_buffer = my_cico;
	}
	
	data->rank_info_count = comm_size;
	
	// ----
	
	for(int r = 0; r < data->rank_info_count; r++)
		data->rank_info[r].proc = ompi_comm_peer_lookup(comm, r);
	
	// ----
	
	/* An XHC communicator is created for each level of the hierarchy.
	 * The hierachy must be in an order of most-specific to most-general. */
	
	data->comms = malloc(xhc_module->hierarchy_len * sizeof(xhc_comm_t));
	if(data->comms == NULL)
		RETURN_WITH_ERROR(return_code, -5, end);
	
	data->comm_count = xhc_make_comms(comm, data->rank_info, data->comms,
		xhc_module->hierarchy, xhc_module->hierarchy_len);
	
	if(data->comm_count <= 0)
		RETURN_WITH_ERROR(return_code, data->comm_count, end);
	
	if(data->comm_count < xhc_module->hierarchy_len)
		REALLOC(data->comms, data->comm_count, xhc_comm_t);
	
	for(int i = 0, c = 0; i < data->comm_count; i++) {
		data->comms[i].chunk_size = xhc_module->chunks[c];
		c = opal_min(c + 1, xhc_module->chunks_len - 1);
	}
	
	if(xhc_module->chunks_len < data->comm_count) {
		opal_output_verbose(MCA_BASE_VERBOSE_WARN,
			ompi_coll_base_framework.framework_output,
			"coll:xhc: Warning: The chunk sizes count is shorter than the "
			"hierarchy size; filling in with the last entry provided");
	} else if(xhc_module->chunks_len > data->comm_count) {
		opal_output_verbose(MCA_BASE_VERBOSE_WARN,
			ompi_coll_base_framework.framework_output,
			"coll:xhc: Warning: The chunk size count is larger than the "
			"hierarchy size; omitting last entries");
	}
	
	// ----
	
	if(mca_coll_xhc_component.set_hierarchy_envs) {
		for(int i = 0; i < data->comm_count; i++) {
			if(rank == data->comms[i].manager_rank) {
				const char *env = NULL;
				
				switch(data->comms[i].locality) {
					case OPAL_PROC_ON_SOCKET:
						env = "AM_SOCKET_LEADER";
						break;
					case OPAL_PROC_ON_NUMA:
						env = "AM_NUMA_LEADER";
						break;
					case OPAL_PROC_ON_L3CACHE:
						env = "AM_L3_LEADER";
						break;
				}
				
				if(env != NULL)
					setenv(env, "1", 0);
				
				break;
			}
		}
	}
	
	// Verbose comm info
	if(mca_coll_xhc_component.print_info) {
		if(rank == 0) {
			const char *drval_str;
			
			switch(mca_coll_xhc_component.dynamic_reduce) {
				case OMPI_XHC_DYNAMIC_REDUCE_DISABLED:
					drval_str = "OFF"; break;
				case OMPI_XHC_DYNAMIC_REDUCE_NON_FLOAT:
					drval_str = "ON (non-float)"; break;
				case OMPI_XHC_DYNAMIC_REDUCE_ALL:
					drval_str = "ON (all)"; break;
				default:
					drval_str = "???";
			}
			
			printf("------------------------------------------------\n"
				"OMPI coll/xhc @ %s, priority %d\n"
				"  Dynamic Leader '%s', Dynamic Reduce '%s'\n"
				"  CICO up until %zu bytes\n"
				"------------------------------------------------\n",
				comm->c_name, mca_coll_xhc_component.priority,
				(mca_coll_xhc_component.dynamic_leader ? "ON" : "OFF"),
				drval_str, mca_coll_xhc_component.cico_max);
		}
		
		for(int i = 0; i < data->comm_count; i++) {
			char buf[BUFSIZ] = {0};
			size_t buf_idx = 0;
			
			buf_idx += sprintf(buf+buf_idx, "%d",
				data->comms[i].manager_rank);
			
			for(int j = 1; j < data->comms[i].size; j++) {
				if(j == data->comms[i].member_id) {
					if(i == 0 || data->comms[i-1].manager_rank == rank)
						buf_idx += sprintf(buf+buf_idx, " %d", rank);
					else
						buf_idx += sprintf(buf+buf_idx, " _");
				} else
					buf_idx += sprintf(buf+buf_idx, " x");
			}
			
			printf("XHC comm loc=0x%04x chunk_size=%zu with %d members [%s]\n",
				data->comms[i].locality, data->comms[i].chunk_size,
				data->comms[i].size, buf);
		}
	}
	
	// ----
	
	xhc_module->data = data;
	xhc_module->initialized = true;
	
	end:
	
	xhc_module_set_fns(comm, xhc_fns);
	
	if(return_code != 0) {
		opal_show_help("help-coll-xhc.txt", "xhc-init-failed", true,
			return_code, errno, strerror(errno));
		
		xhc_destroy_data((xhc_module_t *) module);
		
		free(xhc_module->hierarchy);
		free(xhc_module->chunks);
	}
	
	return return_code;
}

void xhc_destroy_data(mca_coll_xhc_module_t *module) {
	xhc_data_t *data = module->data;
	
	if(data) {
		if(data->comm_count >= 0)
			xhc_destroy_comms(data->comms, data->comm_count);
		
		free(data->comms);
		
		if(data->rank_info_count >= 0) {
			for(int r = 0; r < data->rank_info_count; r++) {
				if(data->rank_info[r].cico_buffer) {
					if(r == data->ompi_rank)
						opal_shmem_unlink(&data->rank_info[r].cico_ds);
					
					opal_shmem_segment_detach(&data->rank_info[r].cico_ds);
				}
				
				if(data->rank_info[r].smsc_ep != NULL)
					MCA_SMSC_CALL(return_endpoint, data->rank_info[r].smsc_ep);
			}
		}
		
		free(data->rank_info);
	}
	
	free(data);
}

static void *xhc_shmem_create(opal_shmem_ds_t *seg_ds, size_t size,
		ompi_communicator_t *ompi_comm, const char *name_chr_s, int name_chr_i) {
	
	char *shmem_file;
	int ret;
	
	// xhc_shmem_seg.<UID>@<HOST>.<JOBID>.<RANK@COMM_WORLD>.<CID>_<CHRS>:<CHRI>
	
	ret = opal_asprintf(&shmem_file, "%s" OPAL_PATH_SEP "xhc_shmem_seg.%u@%s.%x.%d:%d_%s:%d",
		mca_coll_xhc_component.shmem_backing, geteuid(), opal_process_info.nodename,
		OPAL_PROC_MY_NAME.jobid, ompi_comm_rank(MPI_COMM_WORLD), ompi_comm_get_local_cid(ompi_comm),
		name_chr_s, name_chr_i);
	
	if(ret < 0)
		return NULL;
	
	// Not 100% sure what this does!, copied from btl/sm
	opal_pmix_register_cleanup(shmem_file, false, false, false);
	
	ret = opal_shmem_segment_create(seg_ds, shmem_file, size);
	
	free(shmem_file);
	
	if(ret != OPAL_SUCCESS) {
		opal_output_verbose(MCA_BASE_VERBOSE_ERROR,
			ompi_coll_base_framework.framework_output,
			"coll:xhc: Error: Could not create shared memory segment");
		
		return NULL;
	}
	
	void *addr = xhc_shmem_attach(seg_ds);
	
	if(addr == NULL)
		opal_shmem_unlink(seg_ds);
	
	return addr;
}

static void *xhc_shmem_attach(opal_shmem_ds_t *seg_ds) {
	void *addr = opal_shmem_segment_attach(seg_ds);
	
	if(addr == NULL) {
		opal_output_verbose(MCA_BASE_VERBOSE_ERROR,
			ompi_coll_base_framework.framework_output,
			"coll:xhc: Error: Could not attach to shared memory segment");
	}
	
	return addr;
}

void *xhc_get_cico(xhc_rank_info_t *rank_info, int rank) {
	if(OMPI_XHC_CICO_MAX == 0)
		return NULL;
	
	if(rank_info[rank].cico_buffer == NULL)
		rank_info[rank].cico_buffer = xhc_shmem_attach(&rank_info[rank].cico_ds);
	
	return rank_info[rank].cico_buffer;
}

static mca_smsc_endpoint_t *xhc_smsc_ep(xhc_rank_info_t *rank_info) {
	if(!rank_info->smsc_ep) {
		rank_info->smsc_ep = MCA_SMSC_CALL(get_endpoint, &rank_info->proc->super);
		
		if(!rank_info->smsc_ep) {
			opal_output_verbose(MCA_BASE_VERBOSE_ERROR,
				ompi_coll_base_framework.framework_output,
				"coll:xhc: Error: Failed to initialize smsc endpoint");
			
			return NULL;
		}
	}
	
	return rank_info->smsc_ep;
}

int xhc_copy_expose_region(void *base, size_t len, xhc_copy_data_t **region_data) {
	if(mca_smsc_base_has_feature(MCA_SMSC_FEATURE_REQUIRE_REGISTATION)) {
		void *data = MCA_SMSC_CALL(register_region, base, len);
		
		if(data == NULL) {
			opal_output_verbose(MCA_BASE_VERBOSE_ERROR,
				ompi_coll_base_framework.framework_output,
				"coll:xhc: Error: Failed to register memory region with smsc");
			
			return -1;
		}
		
		*region_data = data;
	}
	
	return 0;
}

void xhc_copy_region_post(void *dst, xhc_copy_data_t *region_data) {
	memcpy(dst, region_data, mca_smsc_base_registration_data_size());
}

int xhc_copy_from(xhc_rank_info_t *rank_info,
		void *dst, void *src, size_t size, void *access_token) {
	
	mca_smsc_endpoint_t *smsc_ep = xhc_smsc_ep(rank_info);
	
	if(smsc_ep == NULL)
		return -1;
	
	int status = MCA_SMSC_CALL(copy_from, smsc_ep,
		dst, src, size, access_token);
	
	return (status == OPAL_SUCCESS ? 0 : -1);
}

void xhc_copy_close_region(xhc_copy_data_t *region_data) {
	if(mca_smsc_base_has_feature(MCA_SMSC_FEATURE_REQUIRE_REGISTATION))
		MCA_SMSC_CALL(deregister_region, region_data);
}

void *xhc_get_registration(xhc_rank_info_t *rank_info,
		void *peer_vaddr, size_t size, xhc_reg_t **reg) {
	
	mca_smsc_endpoint_t *smsc_ep = xhc_smsc_ep(rank_info);
	
	if(smsc_ep == NULL)
		return NULL;
	
	/* MCA_RCACHE_FLAGS_PERSIST will cause the registration to stick around.
	 * Though actually, because smsc/xpmem initializes the ref count to 2,
	 * as a means of keeping the registration around (instead of using the
	 * flag), our flag here doesn't have much effect. If at some point we
	 * would wish to actually detach memory in some or all cases, we should
	 * either call the unmap method twice, or reach out to Open MPI devs and
	 * inquire about the ref count. */
	
	void *local_ptr;
	
	*reg = MCA_SMSC_CALL(map_peer_region, smsc_ep,
		MCA_RCACHE_FLAGS_PERSIST, peer_vaddr, size, &local_ptr);
	
	if(*reg == NULL)
		return NULL;
	
	return local_ptr;
}

/* Won't actually unmap/detach, since we've set
 * the "persist" flag while creating the mapping */
void xhc_return_registration(xhc_reg_t *reg) {
	MCA_SMSC_CALL(unmap_peer_region, reg);
}

static int xhc_make_comms(ompi_communicator_t *ompi_comm,
		xhc_rank_info_t *rank_info, xhc_comm_t *comms,
		xhc_loc_t *hierarchy, int hierarchy_len) {
	
	int comm_count = 0;
	int hier_idx = 0;
	
	int ompi_rank = ompi_comm_rank(ompi_comm);
	int ompi_size = ompi_comm_size(ompi_comm);
	
	int return_code, ret;
	
	bool *comm_candidate_info = malloc(ompi_size * sizeof(bool));
	xhc_comm_info_t *comm_members_info = malloc(ompi_size * sizeof(xhc_comm_info_t));
	
	if(comm_candidate_info == NULL || comm_members_info == NULL)
		RETURN_WITH_ERROR(return_code, -50, end);
	
	for(int i = 0; i < hierarchy_len; i++) {
		xhc_comm_t *xc = &comms[comm_count];
		
		*xc = (xhc_comm_t) {
			.locality = hierarchy[hier_idx++],
			
			.size = 0,
			.manager_rank = -1,
			
			.member_info = NULL,
			.reduce_queue = NULL,
			
			.comm_ctrl = NULL,
			.member_ctrl = NULL,
			
			.ctrl_ds = (opal_shmem_ds_t) {0}
		};
		
		// ----
		
		bool candidate = (comm_count == 0 || comms[comm_count - 1].manager_rank == ompi_rank);
		
		ret = ompi_comm->c_coll->coll_allgather(&candidate, 1,
			MPI_C_BOOL, comm_candidate_info, 1, MPI_C_BOOL,
			ompi_comm, ompi_comm->c_coll->coll_allgather_module);
		
		if(ret != OMPI_SUCCESS)
			RETURN_WITH_ERROR(return_code, -51, comm_error);
		
		for(int r = 0; r < ompi_size; r++) {
			
			/* If on a non-bottom comm, only managers of the previous
			 * comm are "full" members. However, this procedure also has
			 * to take place for the bottom-most comm; even if this is the
			 * current rank's bottom-most comm, it may not actually be so,
			 * for another rank (eg. with some non-symmetric hierarchies). */
			if(comm_candidate_info[r] == false)
				continue;
			
			// Non-local => not part of the comm :/
			if(PROC_IS_LOCAL(rank_info[r].proc, xc->locality) == false)
				continue;
			
			/* The member ID means slightly different things whether on the
			 * bottom-most comm or not. On the bottom-most comm, a rank can
			 * either be a "full" member or not. However, on higher-up comms,
			 * if a rank was not a manager on the previous comm, it will not
			 * a "full" member. Instead, it will be a "potential" member, in
			 * that it keeps information about this comm, and is ready to
			 * take over duties and act as a normal member for a specific
			 * collective (eg. dynamic leader feature, or root != manager). */
			if(r == ompi_rank || (comm_count > 0 && r == comms[comm_count - 1].manager_rank))
				xc->member_id = xc->size;
			
			// First to join the comm becomes the manager
			if(xc->manager_rank == -1)
				xc->manager_rank = r;
			
			xc->size++;
		}
		
		if(xc->size == 0)
			RETURN_WITH_ERROR(return_code, -53, comm_error);
		
		if(xc->size == 1) {
			opal_output_verbose(MCA_BASE_VERBOSE_WARN,
				ompi_coll_base_framework.framework_output,
				"coll:xhc: Warning: Locality 0x%04x results in zero-size"
				" XHC comm; merging", xc->locality);
			
			/* Must participate in this allgather, even if useless
			 * to thee, since it's necessary for other ranks. */
			
			xhc_comm_info_t empty_info = (xhc_comm_info_t) {0};
			
			ret = ompi_comm->c_coll->coll_allgather(&empty_info,
				sizeof(xhc_comm_info_t), MPI_BYTE, comm_members_info,
				sizeof(xhc_comm_info_t), MPI_BYTE, ompi_comm,
				ompi_comm->c_coll->coll_allgather_module);
			
			if(ret != OMPI_SUCCESS)
				RETURN_WITH_ERROR(return_code, -54, comm_error);
			
			xhc_destroy_comms(xc, 1);
			continue;
		}
		
		xc->member_info = calloc(xc->size, sizeof(xhc_member_info_t));
		if(xc->member_info == NULL)
			RETURN_WITH_ERROR(return_code, -55, comm_error);
		
		xc->reduce_queue = OBJ_NEW(opal_list_t);
		if(!xc->reduce_queue)
			RETURN_WITH_ERROR(return_code, -56, comm_error);
		
		for(int m = 0; m < xc->size - 1; m++) {
			xhc_rq_item_t *item = OBJ_NEW(xhc_rq_item_t);
			if(!item) RETURN_WITH_ERROR(return_code, -57, comm_error);
			
			opal_list_append(xc->reduce_queue, (opal_list_item_t *) item);
		}
		
		// ----
		
		size_t smsc_reg_size = 0;
			
		if(mca_smsc_base_has_feature(MCA_SMSC_FEATURE_REQUIRE_REGISTATION))
			smsc_reg_size = mca_smsc_base_registration_data_size();
		
		if(ompi_rank == xc->manager_rank) {
			size_t ctrl_len = sizeof(xhc_comm_ctrl_t) + smsc_reg_size
				+ xc->size * sizeof(xhc_member_ctrl_t);
			
			char *ctrl_base = xhc_shmem_create(&xc->ctrl_ds,
				ctrl_len, ompi_comm, "ctrl", comm_count);
			
			if(ctrl_base == NULL)
				RETURN_WITH_ERROR(return_code, -58, comm_error);
			
			/* Manually "touch" to assert allocation in local NUMA node
			* (assuming linux's default firt-touch-alloc NUMA policy) */
			memset(ctrl_base, 0, ctrl_len);
			
			xc->comm_ctrl = (void *) ctrl_base;
			xc->member_ctrl = (void *) (ctrl_base
				+ sizeof(xhc_comm_ctrl_t) + smsc_reg_size);
		}
		
		xhc_comm_info_t comm_info = (xhc_comm_info_t) {
			.ctrl_ds = xc->ctrl_ds
		};
		
		ret = ompi_comm->c_coll->coll_allgather(&comm_info,
			sizeof(xhc_comm_info_t), MPI_BYTE, comm_members_info,
			sizeof(xhc_comm_info_t), MPI_BYTE, ompi_comm,
			ompi_comm->c_coll->coll_allgather_module);
		
		if(ret != OMPI_SUCCESS)
			RETURN_WITH_ERROR(return_code, -59, comm_error);
		
		if(ompi_rank != xc->manager_rank) {
			xc->ctrl_ds = comm_members_info[xc->manager_rank].ctrl_ds;
			
			char *ctrl_base = xhc_shmem_attach(&xc->ctrl_ds);
			if(ctrl_base == NULL) RETURN_WITH_ERROR(return_code, -60, comm_error);
			
			xc->comm_ctrl = (void *) ctrl_base;
			xc->member_ctrl = (void *) (ctrl_base
				+ sizeof(xhc_comm_ctrl_t) + smsc_reg_size);
		}
		
		xc->my_member_ctrl = &xc->member_ctrl[xc->member_id];
		xc->my_member_info = &xc->member_info[xc->member_id];
		
		// ----
		
		comm_count++;
		continue;
		
		// ----
		
		comm_error: {
			xhc_destroy_comms(comms, comm_count+1);
			comm_count = -1;
			
			goto end;
		}
	}
	
	end:
	
	free(comm_candidate_info);
	free(comm_members_info);
	
	if(comm_count == 0)
		return_code = -70;
	else if(comm_count > 0)
		return_code = comm_count;
	
	return return_code;
}

static void xhc_destroy_comms(xhc_comm_t *comms, int comm_count) {
	bool is_manager = true;
	
	for(int i = 0; i < comm_count; i++) {
		xhc_comm_t *xc = &comms[i];
		
		if(xc->member_id != 0)
			is_manager = false;
		
		free(xc->member_info);
		
		if(xc->reduce_queue)
			OBJ_RELEASE(xc->reduce_queue);
		
		if(xc->comm_ctrl) {
			if(is_manager)
				opal_shmem_unlink(&xc->ctrl_ds);
			
			opal_shmem_segment_detach(&xc->ctrl_ds);
		}
		
		*xc = (xhc_comm_t) {0};
	}
}
