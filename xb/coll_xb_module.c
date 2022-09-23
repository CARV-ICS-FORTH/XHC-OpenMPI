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

#include <stdio.h>

#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"

#include "coll_xb.h"

static void mca_coll_xb_module_construct(mca_coll_xb_module_t *module) {
	module->comm = MPI_COMM_NULL;
}

static void mca_coll_xb_module_destruct(mca_coll_xb_module_t *module) {
	if(module->comm != MPI_COMM_NULL)
		ompi_comm_free(&module->comm);
}

OBJ_CLASS_INSTANCE(mca_coll_xb_module_t,
	mca_coll_base_module_t,
	mca_coll_xb_module_construct,
	mca_coll_xb_module_destruct);

mca_coll_base_module_t *mca_coll_xb_module_comm_query(
		struct ompi_communicator_t *comm, int *priority) {
	
	if((*priority = mca_coll_xb_component.priority) < 0)
		return NULL;
	
	if(OMPI_COMM_IS_INTER(comm) || ompi_comm_size(comm) == 1
			|| ompi_group_have_remote_peers (comm->c_local_group)) {
        
        opal_output_verbose(10, ompi_coll_base_framework.framework_output,
			"coll:xb:comm_query (%s/%s): intercomm, self-comm, "
			"or not all ranks local; disqualifying myself",
			ompi_comm_print_cid(comm), comm->c_name);
        
        return NULL;
    }
    
	mca_coll_base_module_t *module =
		(mca_coll_base_module_t *) OBJ_NEW(mca_coll_xb_module_t);
	
	if(module == NULL)
		return NULL;
	
	module->coll_module_enable = mca_coll_xb_module_enable;
	module->coll_barrier = mca_coll_xb_barrier;
	
	return module;
}

int mca_coll_xb_module_enable(mca_coll_base_module_t *module,
		struct ompi_communicator_t *comm) {
	
	return OMPI_SUCCESS;
}
