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

#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"

#include "opal/mca/shmem/base/base.h"
#include "opal/util/show_help.h"

#include "coll_xhc.h"

const char *mca_coll_xhc_component_version_string =
	"Open MPI xhc collective MCA component version " OMPI_VERSION;

static int xhc_register(void);

static const char *topo_locs_str[] = {
	"node", "flat",
	"socket",
	"numa",
	"l3", "l3cache",
	"l2", "l2cache",
	"l1", "l1cache",
	"core",
	"hwthread", "thread"
};

static const char *topo_locs_mca_desc_list =
	"node, socket, numa, l3, l2, l1, core, hwthread";

static const xhc_loc_t topo_locs_val[] = {
	OPAL_PROC_ON_NODE, OPAL_PROC_ON_NODE,
	OPAL_PROC_ON_SOCKET,
	OPAL_PROC_ON_NUMA,
	OPAL_PROC_ON_L3CACHE, OPAL_PROC_ON_L3CACHE,
	OPAL_PROC_ON_L2CACHE, OPAL_PROC_ON_L2CACHE,
	OPAL_PROC_ON_L1CACHE, OPAL_PROC_ON_L1CACHE,
	OPAL_PROC_ON_CORE,
	OPAL_PROC_ON_HWTHREAD, OPAL_PROC_ON_HWTHREAD
};

mca_coll_xhc_component_t mca_coll_xhc_component = {
	.super = {
		.collm_version = {
			MCA_COLL_BASE_VERSION_2_4_0,
			
			.mca_component_name = "xhc",
			MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION,
				OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION),
			
			.mca_register_component_params = xhc_register,
		},
		
		.collm_data = {
			MCA_BASE_METADATA_PARAM_CHECKPOINT
		},
		
		.collm_init_query = mca_coll_xhc_component_init_query,
		.collm_comm_query = mca_coll_xhc_module_comm_query,
	},
	
	.priority = 0,
	.print_info = false,
	.set_hierarchy_envs = false,
	
	.shmem_backing = NULL,
	
	.dynamic_leader = false,
	.dynamic_reduce = OMPI_XHC_DYNAMIC_REDUCE_NON_FLOAT,
	.force_reduce = false,
	
	.cico_max = 1024,
	
	.uniform_chunks = true,
	.uniform_chunks_min = 1024,
	
	/* These are the parameters that will need
	 * processing, and their default values. */
	.hierarchy_mca = "numa,socket",
	.chunk_size_mca = "16K"
};

/* Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support. */
int mca_coll_xhc_component_init_query(bool enable_progress_threads,
		bool enable_mpi_threads) {
	
	return OMPI_SUCCESS;
}

static mca_base_var_enum_value_t dynamic_reduce_options[] = {
	{OMPI_XHC_DYNAMIC_REDUCE_DISABLED, "disabled"},
	{OMPI_XHC_DYNAMIC_REDUCE_NON_FLOAT, "enabled for non-floats"},
	{OMPI_XHC_DYNAMIC_REDUCE_ALL, "enabled for all types"},
	{0, NULL}
};

static int xhc_register(void) {
	mca_base_var_enum_t *var_enum;
	char *desc;
	int ret;
	
	/* Priority */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"priority", "Priority of the xhc component",
		MCA_BASE_VAR_TYPE_INT, NULL, 0, 0, OPAL_INFO_LVL_1,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.priority);
	
	/* Info */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"print_info", "Print information during initialization",
		MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_3,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.print_info);
	
	/* Hier env vars */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"hpenv", "Request definition of env vars that denote each process's role in "
		"the hierarchy (for testing/benchmarking)",
		MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.set_hierarchy_envs);
	
	/* SHM Backing dir */
	
	mca_coll_xhc_component.shmem_backing = (access("/dev/shm", W_OK) == 0 ?
		"/dev/shm" : opal_process_info.job_session_dir);
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"shmem_backing", "Directory to place backing files for shared-memory"
		" control-data communication", MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
		OPAL_INFO_LVL_3, MCA_BASE_VAR_SCOPE_READONLY,
		&mca_coll_xhc_component.shmem_backing);
	
	/* Dynamic leader */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"dynamic_leader", "Enable dynamic operation-wise group-leader selection.",
		MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_5,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.dynamic_leader);
	
	/* Dynamic reduce */
	
	ret = mca_base_var_enum_create("coll_xhc_dynamic_reduce_options",
		dynamic_reduce_options, &var_enum);
	if(ret != OPAL_SUCCESS) return ret;
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"dynamic_reduce", "Dynamic/out-of-order intra-group reduction",
		MCA_BASE_VAR_TYPE_INT, var_enum, 0, 0, OPAL_INFO_LVL_6,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.dynamic_reduce);
	
	OBJ_RELEASE(var_enum);
	
	/* Force enable "hacky" reduce */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"force_reduce", "Force enable the \"special\" Reduce for all calls",
		MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.force_reduce);
	
	/* Hierarchy features */
	
	ret = opal_asprintf(&desc, "%s (%s)", "Comma-separated list of "
		"topology features to consider for the hierarchy", topo_locs_mca_desc_list);
	if(ret < 0) return OMPI_ERROR;
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"hierarchy", desc, MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OPAL_INFO_LVL_4,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.hierarchy_mca);
	
	free(desc); desc = NULL;
	
	/* Chunk size(s) */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"chunk_size", "The chunk size(s) to be used for the pipeline "
		"(single value, or comma separated list for different hierarchy levels "
		"(bottom to top))",
		MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OPAL_INFO_LVL_5,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.chunk_size_mca);
	
	/* Allreduce uniform chunks */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"uniform_chunks", "Automatically optimize chunk size in reduction "
		"collectives according to message size, for load balancing",
		MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_5,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.uniform_chunks);
	
	/* Allreduce uniform min size */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"uniform_chunks_min", "Minimum chunk size for reduction collectives, "
		"when \"uniform chunks\" are enabled", MCA_BASE_VAR_TYPE_SIZE_T,
		NULL, 0, 0, OPAL_INFO_LVL_5, MCA_BASE_VAR_SCOPE_READONLY,
		&mca_coll_xhc_component.uniform_chunks_min);
	
	/* CICO threshold (inclusive) */
	
	(void) mca_base_component_var_register(&mca_coll_xhc_component.super.collm_version,
		"cico_max", "Maximum message size up to which to use CICO",
		MCA_BASE_VAR_TYPE_SIZE_T, NULL, 0, 0, OPAL_INFO_LVL_5,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xhc_component.cico_max);
	
	return OMPI_SUCCESS;
}

int xhc_component_parse_hierarchy(const char *val_str,
		xhc_loc_t **vals_dst, int *len_dst) {
	
	if(val_str == NULL) {
		*vals_dst = NULL;
		*len_dst = 0;
		return 0;
	}
	
	char *hparam = strdup(val_str);
	if(hparam == NULL)
		return -1;
	
	xhc_loc_t *hier = NULL;
	int hier_len = 1;
	
	for(const char *p = hparam; *p; p++) {
		if(*p == ',')
			hier_len++;
	}
	
	hier = malloc(hier_len * sizeof(xhc_loc_t));
	if(hier == NULL) {
		free(hparam);
		return -2;
	}
	
	int hier_idx = 0;
	char *token = strtok(hparam, ",");
	
	for(; token != NULL; token = strtok(NULL, ",")) {
		bool found = false;
		
		for(size_t k = 0; k < sizeof(topo_locs_str)/sizeof(char *); k++) {
			if(strcasecmp(token, topo_locs_str[k]) == 0) {
				hier[hier_idx++] = topo_locs_val[k];
				
				found = true;
				break;
			}
		}
		
		if(!found) {
			opal_show_help("help-coll-xhc.txt", "bad-locality-param",
				true, token, val_str);
			
			free(hparam);
			free(hier);
			
			return -3;
		}
	}
	
	free(hparam);
	
	*vals_dst = hier;
	*len_dst = hier_len;
	
	return 0;
}

int xhc_component_parse_chunk_sizes(const char *val_str,
		size_t **vals_dst, int *len_dst) {
	
	if(val_str == NULL) {
		*vals_dst = malloc(sizeof(size_t));
		if(*vals_dst == NULL) return -1;
		
		(*vals_dst)[0] = (size_t) -1;
		*len_dst = 1;
		
		return 0;
	}
	
	char *cparam = strdup(val_str);
	if(cparam == NULL)
		return -2;
	
	int chunks_len = 1;
	
	for(const char *p = cparam; *p; p++) {
		if(*p == ',' && *(p+1))
			chunks_len++;
	}
	
	size_t *chunk_sizes = malloc(chunks_len * sizeof(size_t));
	
	if(chunk_sizes == NULL) {
		free(cparam);
		return -3;
	}
	
	char *scanner = NULL;
	char *token = strtok_r(cparam, ",", &scanner);
	
	if(!token) {
		opal_show_help("help-coll-xhc.txt", "bad-chunk-size-param",
			true, val_str);
		
		free(cparam);
		free(chunk_sizes);
		
		return -4;
	}
	
	for(int i = 0; i < chunks_len; i++) {
		size_t last_idx = strlen(token) - 1;
		size_t mult = 1;
		
		switch(token[last_idx]) {
			case 'g': case 'G':
				mult *= 1024;
			case 'm': case 'M':
				mult *= 1024;
			case 'k': case 'K':
				mult *= 1024;
			
			token[last_idx] = '\0';
		}
		
		bool legal = (*token);
		
		for(char *c = token; *c; c++) {
			if(*c < '0' || *c > '9') {
				legal = false;
				break;
			}
		}
		
		if(!legal) {
			opal_show_help("help-coll-xhc.txt", "bad-chunk-size-param",
				true, val_str);
			
			free(cparam);
			free(chunk_sizes);
			
			return -5;
		}
		
		chunk_sizes[i] = atoll(token) * mult;
		
		token = strtok_r(NULL, ",", &scanner);
	}
	
	free(cparam);
	
	*vals_dst = chunk_sizes;
	*len_dst = chunks_len;
	
	return 0;
}
