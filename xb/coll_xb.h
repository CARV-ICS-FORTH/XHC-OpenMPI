/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_XB_EXPORT_H
#define MCA_COLL_XB_EXPORT_H

#include "ompi_config.h"
#include "mpi.h"

#include <stdint.h>

#include "ompi/mca/mca.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"

BEGIN_C_DECLS

typedef struct mca_coll_xb_component_t {
	mca_coll_base_component_t super;
	
	int priority;
	char *hierarchy_mca;
} mca_coll_xb_component_t;

typedef struct mca_coll_xb_module_t {
	mca_coll_base_module_t super;
	ompi_communicator_t *comm;
} mca_coll_xb_module_t;

OMPI_MODULE_DECLSPEC extern mca_coll_xb_component_t mca_coll_xb_component;
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(mca_coll_xb_module_t);

// ----------------------------------------

int mca_coll_xb_component_init_query(bool enable_progress_threads,
	bool enable_mpi_threads);

mca_coll_base_module_t *mca_coll_xb_module_comm_query(
	ompi_communicator_t *comm, int *priority);

int mca_coll_xb_module_enable(mca_coll_base_module_t *module,
	ompi_communicator_t *comm);

int mca_coll_xb_barrier(ompi_communicator_t *ompi_comm,
	mca_coll_base_module_t *module);

END_C_DECLS

#endif
