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

#include "coll_xb.h"

static int xb_lazy_init(mca_coll_xb_module_t *xb_module,
		ompi_communicator_t *comm) {
	
	opal_info_t info;
	int ret;
	
	OBJ_CONSTRUCT(&info, opal_info_t);
	
	// 1. The new comm should use XHC, and not use XB
	
	ret = opal_info_set(&info, "ompi_comm_coll_preference", "xhc,^xb");
	if(ret != OPAL_SUCCESS) { OBJ_DESTRUCT(&info); return -1; }
	
	// 2. Pass requested barrier hierarchy to XHC via the info key
	
	const char *hmca = mca_coll_xb_component.hierarchy_mca;
	if(hmca[0] != '\0') {
		ret = opal_info_set(&info, "ompi_comm_coll_xhc_hierarchy", hmca);
		if(ret != OPAL_SUCCESS) { OBJ_DESTRUCT(&info); return -2; }
	}
	
	// 3. Create dup comm, future barrier operations will be delegated to it
	
	ret = ompi_comm_dup_with_info(comm, &info, &xb_module->comm);
	
	OBJ_DESTRUCT(&info);
	
	return (ret == MPI_SUCCESS ? 0 : -3);
}

int mca_coll_xb_barrier(ompi_communicator_t *ompi_comm,
		mca_coll_base_module_t *module) {
	
	mca_coll_xb_module_t *xb_module = (mca_coll_xb_module_t *) module;
	
	if(xb_module->comm == MPI_COMM_NULL) {
		int ret = xb_lazy_init(xb_module, ompi_comm);
		if(ret != 0) return OMPI_ERROR;
	}
	
	return MPI_Barrier(xb_module->comm);
}
