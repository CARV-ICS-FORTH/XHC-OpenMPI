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

#include "opal/util/arch.h"
#include "opal/util/minmax.h"

#include "coll_xbrc.h"

static void mca_coll_xbrc_module_construct(mca_coll_xbrc_module_t *module) {
	module->data = NULL;
	module->initialized = false;
}

static void mca_coll_xbrc_module_destruct(mca_coll_xbrc_module_t *module) {
	if(module->initialized) {
		mca_coll_xbrc_destroy_data(module);
		module->initialized = false;
	}
}

OBJ_CLASS_INSTANCE(mca_coll_xbrc_module_t,
	mca_coll_base_module_t,
	mca_coll_xbrc_module_construct,
	mca_coll_xbrc_module_destruct);

mca_coll_base_module_t *mca_coll_xbrc_module_comm_query(
		struct ompi_communicator_t *comm, int *priority) {
	
	if((*priority = mca_coll_xbrc_component.priority) < 0)
		return NULL;
	
	if(OMPI_COMM_IS_INTER(comm) || ompi_comm_size(comm) == 1
			|| ompi_group_have_remote_peers (comm->c_local_group)) {
        
        opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:xbrc:comm_query (%s/%s): intercomm, self-comm, "
			"or not all ranks local; disqualifying myself",
			ompi_comm_print_cid(comm), comm->c_name);
        
        return NULL;
    }
    
    int comm_size = ompi_comm_size(comm);
	for(int r = 0; r < comm_size; r++) {
		ompi_proc_t *proc = ompi_comm_peer_lookup(comm, r);
		
		if(proc->super.proc_arch != opal_local_arch) {
			opal_output_verbose(10, ompi_coll_base_framework.framework_output,
				"coll:xbrc:comm_query (%s/%s): All ranks not of the same arch; "
				"disabling myself", ompi_comm_print_cid(comm), comm->c_name);
			
			return NULL;
		}
	}
	
	mca_coll_base_module_t *module =
		(mca_coll_base_module_t *) OBJ_NEW(mca_coll_xbrc_module_t);
	
	if(module == NULL)
		return NULL;
	
	module->coll_module_enable = mca_coll_xbrc_module_enable;
	module->coll_allreduce = mca_coll_xbrc_allreduce;
	
	return module;
}

int mca_coll_xbrc_module_enable(mca_coll_base_module_t *module,
		struct ompi_communicator_t *comm) {
	
	return OMPI_SUCCESS;
}
