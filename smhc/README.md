# SMHC: Shared Memory Hierarchical Collectives

Implementation of MPI Broadcast as described in **Framework for scalable intra-node collective 
operations using shared memory**, at SC-18 -- https://dl.acm.org/doi/10.5555/3291656.3291695


## Building

### Prerequisites

- OpenMPI version 5 (tested with v5.0.0rc6)
- A fix to this bug: https://github.com/open-mpi/ompi/issues/10335
	- See diff for bandaid fix at end of original issue

### Process

1. Place the component's folder and its contents inside OpenMPI's `coll` framework (`ompi/mca/coll`)

2. Run `autogen.pl`, so that it will generate the required build files for the new component

3. Configure OpenMPI
	- If all has gone well, at this point you will find in the configure log, a line confirming
	SMHC's inclusion: `checking if MCA component coll:smhc can compile... yes`.
	
4. Compile (`make`) and install (`make install`) OpenMPI

General information on building OpenMPI can be found in its documentation:  
https://www.open-mpi.org/faq/?category=building  
https://github.com/open-mpi/ompi/blob/master/README.md

## Running

General information on running Open MPI jobs can be found here:  
https://www.open-mpi.org/faq/?category=running

mpirun's man page will also be useful:  
https://docs.open-mpi.org/en/v5.0.x/man-openmpi/man1/mpirun.1.html

SMHC follows OpenMPI's conventions for collective component selection. Make sure to include it in
the coll list (`--mca coll` parameter), and give it a priority higher than that of any other
component that also provides the Broadcast operation (`--mca coll_smhc_priority` parameter).

### Tuning options

*(prepend MCA parameters "coll_smhc_")*

- MCA **impl** (default `flat`): Configure the Broadcast implementation to use; flat (`flat`) or
hierarchical (`tree`).

- MCA **tree_topo** (default `socket`): When the hierarchical implementation is chosen, configure
the topological feature according to which the hierarchy will be formed. Current options:`numa`, 
`socket`.
	
	- If the requested feature results in a flat tree, the flat implementation will be silently
	invoked instead of the tree-based one.

- MCA **pipeline_stages** (default `2`): The number of pipeline stages.

- MCA **chunk_size** (default `4K`): The size (in bytes) of each pipeline segment.
	
	- The total size of the shared broadcast buffer will be *pipeline_stages x chunk_size*.

The `--bind-to core` option to `mpirun` is suggested, especially when using the tree-based
implementation, to ensure that the clustering process can accurately pinpoint each process's 
location.

### Example command lines

Basic  
`$ mpirun --mca coll basic,libnbc,tuned,smhc --mca coll_smhc_priority 100 <application>`

Tree-based  
`$ mpirun --mca coll basic,libnbc,tuned,smhc --mca coll_smhc_priority 100 --mca coll_smhc_impl tree <application>`

Tune topology sensitivity (numa-wise)  
`$ mpirun --mca coll basic,libnbc,tuned,smhc --mca coll_smhc_priority 100 --mca coll_smhc_impl tree --mca coll_smhc_tree_topo numa <application>`

Tune pipeline (4x2KB)  
`$ mpirun --mca coll basic,libnbc,tuned,smhc --mca coll_smhc_priority 100 --mca coll_smhc_pipeline_stages 4 --mca coll_smhc_chunk_size 2K <application>`

---
Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
