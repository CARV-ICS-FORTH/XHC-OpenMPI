#include "ompi_config.h"
#include "coll_smhc.h"

#include "mpi.h"
#include "ompi/mca/coll/coll.h"
#include "coll_smhc.h"

const char *mca_coll_smhc_component_version_string =
	"Open MPI smhc collective MCA component version " OMPI_VERSION;

static int smhc_register(void);

mca_coll_smhc_component_t mca_coll_smhc_component = {
	.super = {
		/* First, the mca_component_t struct containing meta information
		 * about the component itself */
		
		.collm_version = {
			MCA_COLL_BASE_VERSION_2_4_0,
			
			/* Component name and version */
			.mca_component_name = "smhc",
			MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION,
				OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION),
			
			/* Component open and close functions */
			.mca_register_component_params = smhc_register,
		},
		
		.collm_data = {
			/* The component is checkpoint ready */
			MCA_BASE_METADATA_PARAM_CHECKPOINT
		},
		
		/* Initialization / querying functions */
		.collm_init_query = mca_coll_smhc_init_query,
		.collm_comm_query = mca_coll_smhc_comm_query,
	},
	
	.priority = 0,
	
	.impl_param = "flat",
	.tree_topo_param = "socket",
	
	.pipeline_factor = 2,
	.chunk_size = 4096,
	
	.tree_topo = (opal_hwloc_locality_t) -1
};

static int smhc_register(void) {
	(void) mca_base_component_var_register(&mca_coll_smhc_component.super.collm_version,
		"priority", "Priority of the smhc coll component",
		MCA_BASE_VAR_TYPE_INT, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_smhc_component.priority);
	
	(void) mca_base_component_var_register(&mca_coll_smhc_component.super.collm_version,
		"impl", "Set the broadcast implementation to use (flat/tree)",
		MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_smhc_component.impl_param);
	
	(void) mca_base_component_var_register(&mca_coll_smhc_component.super.collm_version,
		"tree_topo", "Topology according to which to split the communicator, "
		"for hierarchical implementations",
		MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_smhc_component.tree_topo_param);
	
	(void) mca_base_component_var_register(&mca_coll_smhc_component.super.collm_version,
		"pipeline_stages", "Number of pipeline stages",
		MCA_BASE_VAR_TYPE_INT, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_smhc_component.pipeline_factor);
	
	(void) mca_base_component_var_register(&mca_coll_smhc_component.super.collm_version,
		"chunk_size", "Size of each pipeline stage",
		MCA_BASE_VAR_TYPE_INT, NULL, 0, 0, OPAL_INFO_LVL_9,
		MCA_BASE_VAR_SCOPE_READONLY, &mca_coll_smhc_component.chunk_size);
	
	return OMPI_SUCCESS;
}
