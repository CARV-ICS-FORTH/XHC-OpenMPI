#include "ompi_config.h"
#include "coll_smhc.h"

#include <stdio.h>
#include <sys/mman.h>

#include "mpi.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"
#include "opal/util/show_help.h"

#include "coll_smhc.h"

static void mca_coll_smhc_module_construct(mca_coll_smhc_module_t *module) {
	module->flat_data = NULL;
	module->flat_initialized = false;
	
	module->tree_data = NULL;
	module->tree_initialized = false;
}

static void mca_coll_smhc_module_destruct(mca_coll_smhc_module_t *module) {
	if(module->flat_initialized) {
		munmap(module->flat_data->shm_base,
			module->flat_data->shm_size);
		
		free(module->flat_data);
		
		module->flat_initialized = false;
	}
	
	if(module->tree_initialized) {
		ompi_comm_free(&module->tree_data->hier_comm);
		ompi_comm_free(&module->tree_data->flat_comm);
		
		free(module->tree_data->leaders);
		free(module->tree_data);
		
		module->tree_initialized = false;
	}
}

/* Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support. */
int mca_coll_smhc_init_query(bool enable_progress_threads,
		bool enable_mpi_threads) {
	
	const char *tpar = mca_coll_smhc_component.tree_topo_param;
	opal_hwloc_locality_t topo;
	
	if(tpar == NULL) {
		opal_show_help("help-coll-smhc.txt", "unknown-topo-param", true, tpar);
		return OMPI_ERROR;
	}
	
	if(strcmp(tpar, "numa") == 0)
		topo = OPAL_PROC_ON_NUMA;
	else if(strcmp(tpar, "socket") == 0)
		topo = OPAL_PROC_ON_SOCKET;
	else {
		opal_show_help("help-coll-smhc.txt", "unknown-topo-param", true, tpar);
		return OMPI_ERROR;
	}
	
	mca_coll_smhc_component.tree_topo = topo;
	
	return OMPI_SUCCESS;
}

static const char *impl_str[] = {
	"flat", "tree"
};

mca_coll_base_module_bcast_fn_t bcast_impl_fn[] = {
	mca_coll_smhc_bcast_flat, mca_coll_smhc_bcast_tree
};

static void choose_impls(ompi_communicator_t *comm,
		mca_coll_base_module_t *module) {
	
	const char *impl_name;
	
	opal_cstring_t *impl_info;
	int flag = 0;
	
	// Watch out for OMPI issue #10335
	if(comm->super.s_info != NULL)
		opal_info_get(comm->super.s_info, "coll_smhc_impl", &impl_info, &flag);
	
	if(flag) impl_name = impl_info->string;
	else impl_name = mca_coll_smhc_component.impl_param;
	
	if(impl_name) {
		for(size_t i = 0; i < sizeof(impl_str)/sizeof(impl_str[0]); i++) {
			if(strcmp(impl_name, impl_str[i]) == 0) {
				module->coll_bcast = bcast_impl_fn[i];
				break;
			}
		}
	} else
		module->coll_bcast = bcast_impl_fn[0];
	
	if(flag)
		OBJ_RELEASE(impl_info);
}

mca_coll_base_module_t *mca_coll_smhc_comm_query(ompi_communicator_t *comm,
		int *priority) {
	
	opal_output_verbose(10, ompi_coll_base_framework.framework_output,
		"coll:smhc:comm_query (%s/%s): priority = %d",
		ompi_comm_print_cid(comm), comm->c_name, mca_coll_smhc_component.priority);
	
	*priority = mca_coll_smhc_component.priority;
	
	if(mca_coll_smhc_component.priority < 0)
		return NULL;
	
	if(ompi_comm_size(comm) == 1) {
		opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:smhc:comm_query (%s/%s): self-comm; disqualifying myself",
			ompi_comm_print_cid(comm), comm->c_name);
		
		return NULL;
	}
	
	mca_coll_base_module_t *module =
		(mca_coll_base_module_t *) OBJ_NEW(mca_coll_smhc_module_t);
	
	if(module == NULL)
		return NULL;
	
	module->coll_module_enable = mca_coll_smhc_module_enable;
	choose_impls(comm, module);
	
	return module;
}

int mca_coll_smhc_module_enable(mca_coll_base_module_t *module,
		struct ompi_communicator_t *comm) {

	int ret = OMPI_SUCCESS;
	
	// Because we use base's functions at initialization
	module->base_data = OBJ_NEW(mca_coll_base_comm_t);
	if(module->base_data == NULL) ret = OMPI_ERROR;
	
	opal_output_verbose(10, ompi_coll_base_framework.framework_output,
		"coll:smhc:module_enable (%s/%s): %s",
		ompi_comm_print_cid(comm), comm->c_name,
		(ret == OMPI_SUCCESS ? "OMPI_SUCCESS" : "OMPI_ERROR"));
	
	return ret;
}

OBJ_CLASS_INSTANCE(mca_coll_smhc_module_t,
	mca_coll_base_module_t,
	mca_coll_smhc_module_construct,
	mca_coll_smhc_module_destruct);
