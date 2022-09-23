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

#include "coll_xbrc.h"

const char *mca_coll_xbrc_component_version_string =
	"Open MPI xbrc collective MCA component version " OMPI_VERSION;

static int xbrc_register(void);

mca_coll_xbrc_component_t mca_coll_xbrc_component = {
	.super = {
		.collm_version = {
			MCA_COLL_BASE_VERSION_2_4_0,
			
			.mca_component_name = "xbrc",
			MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION,
				OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION),
			
			.mca_register_component_params = xbrc_register,
		},
		
		.collm_data = {
			MCA_BASE_METADATA_PARAM_CHECKPOINT
		},
		
		.collm_init_query = mca_coll_xbrc_component_init_query,
		.collm_comm_query = mca_coll_xbrc_module_comm_query,
	},
	
	.priority = 0,
	
	// Initialized at runtime
	.xpmem_align = 0,
};

/* Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support. */
int mca_coll_xbrc_component_init_query(bool enable_progress_threads,
		bool enable_mpi_threads) {
	
	mca_coll_xbrc_component.xpmem_align = OMPI_XBRC_XPMEM_ALIGN;
	
	return OMPI_SUCCESS;
}

static int xbrc_register(void) {
	(void) mca_base_component_var_register(&mca_coll_xbrc_component.super.collm_version,
		"priority", "Priority of the xbrc component",
		MCA_BASE_VAR_TYPE_INT, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_xbrc_component.priority);
	
	return OMPI_SUCCESS;
}
