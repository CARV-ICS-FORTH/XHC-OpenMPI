# XPMEM-based Hierarchical Collectives for OpenMPI

This repository holds material related to the XHC collectives implementation and to the 
corresponding research paper: **A framework for hierarchical single-copy MPI collectives on 
multicore nodes**.

### Contents

Within the repository you will find:

- The source code for the XHC component, upon which work the paper is based

- The source code for the implementations of the two relevant recent research studies for
intra-node MPI collectives, as those where described in the paper
	- Shared Memory Hierarchical Collectives (SMHC)
	- XPMEM-Based Reduction Collective (XBRC)

- XB, a complementary component to XHC, to aid accurate micro-benchmarking

- Our modified OSU micro-benchmarks suite used for the experiments in the paper

All collectives' implementations are bundled as common components for OpenMPI's `coll` MCA 
framework. More information about the components, as well as instruction on how to build and use 
them, are available inside their respective directories.

---
Contact: George Katevenis, gkatev@ics.forth.gr  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
