# XPMEM-based Hierarchical Collectives for OpenMPI

This repository holds material related to the XHC component and to the corresponding research 
paper: **A framework for hierarchical single-copy MPI collectives on multicore nodes**.

Within is contained the source code for the implementations of the two relevant recent research
studies for intra-node MPI collectives, as those where described in the paper:

- Shared Memory Hierarchical Collectives (SMHC)
- XPMEM-Based Reduction Collective (XBRC)

The implementations are bundled as components for OpenMPI's `coll` MCA framework. More information
about them, as well as instruction on how to build and use them, are available inside the 
respective directories (smhc, xbrc).
