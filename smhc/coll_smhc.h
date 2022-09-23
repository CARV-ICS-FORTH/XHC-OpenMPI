/*
 * Copyright (c) 2021-2022 Computer Architecture and VLSI Systems (CARV)
 *                         Laboratory, ICS Forth. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_SMHC_EXPORT_H
#define MCA_COLL_SMHC_EXPORT_H

#include "ompi_config.h"

#include "mpi.h"
#include "ompi/mca/mca.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/request/request.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/base/coll_base_functions.h"

BEGIN_C_DECLS

// ----------------------------------------

#define SMHC_SHM_FILE_MAXLEN 32

#define SMHC_PIPELINE (mca_coll_smhc_component.pipeline_factor)
#define SMHC_CHUNK (mca_coll_smhc_component.chunk_size)

#define RETURN_WITH_ERROR(var, err, label) do {var = err; goto label;} while(0)

enum {
	OMPI_SMHC_HIER_LEVEL_NUMA,
	OMPI_SMHC_HIER_LEVEL_SOCKET
};

// ----------------------------------------

typedef struct mca_coll_smhc_flag_t {
	uint32_t flag;
} __attribute__((aligned(64))) mca_coll_smhc_flag_t;

typedef struct mca_coll_smhc_flat_data_t {
	void *shm_base;
	size_t shm_size;
	
	mca_coll_smhc_flag_t *sh_release;
	mca_coll_smhc_flag_t *sh_gather;
	mca_coll_smhc_flag_t *sh_root_gather;
	
	void *sh_bcast_buffer;
	
	uint32_t pvt_release_state;
	uint32_t pvt_gather_state;
} mca_coll_smhc_flat_data_t;

typedef struct mca_coll_smhc_tree_data_t {
	ompi_communicator_t *hier_comm;
	ompi_communicator_t *flat_comm;
	
	int *leaders;
	size_t leader_count;
	int my_leader;
} mca_coll_smhc_tree_data_t;

typedef struct mca_coll_smhc_component_t {
	mca_coll_base_component_t super;
	
	int priority;
	
	char *impl_param;
	char *tree_topo_param;
	
	uint chunk_size;
	uint pipeline_factor;
	
	opal_hwloc_locality_t tree_topo;
} mca_coll_smhc_component_t;
OMPI_MODULE_DECLSPEC extern mca_coll_smhc_component_t mca_coll_smhc_component;

typedef struct mca_coll_smhc_module_t {
	mca_coll_base_module_t super;
	
	mca_coll_smhc_flat_data_t *flat_data;
	bool flat_initialized;
	
	mca_coll_smhc_tree_data_t *tree_data;
	bool tree_initialized;
} mca_coll_smhc_module_t;
OMPI_DECLSPEC OBJ_CLASS_DECLARATION(mca_coll_smhc_module_t);

// ----------------------------------------

int mca_coll_smhc_init_query(bool enable_progress_threads,
	bool enable_mpi_threads);

mca_coll_base_module_t *mca_coll_smhc_comm_query(
	struct ompi_communicator_t *comm, int *priority);

int mca_coll_smhc_module_enable(mca_coll_base_module_t *module,
	struct ompi_communicator_t *comm);

// int mca_coll_smhc_bcast(void *buf, int count,
	// struct ompi_datatype_t *datatype,int root,
	// struct ompi_communicator_t *comm,
	// mca_coll_base_module_t *module);

int mca_coll_smhc_bcast_flat(void *buf, int count,
	struct ompi_datatype_t *datatype,int root,
	struct ompi_communicator_t *comm,
	mca_coll_base_module_t *module);

int mca_coll_smhc_bcast_tree(void *buf, int count,
	struct ompi_datatype_t *datatype,int root,
	struct ompi_communicator_t *comm,
	mca_coll_base_module_t *module);

// ----------------------------------------

END_C_DECLS

#endif
