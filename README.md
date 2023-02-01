# XHC: A framework for hierarchical single-copy MPI collectives on multicore nodes

This branch holds material related to the IEEE Cluster 2022 paper and the *XPMEM-based Hierarchical Collectives* (XHC) component:

**A framework for hierarchical single-copy MPI collectives on multicore nodes**,  
*George Katevenis, Manolis Ploumidis, and Manolis Marazakis*,  
IEEE Cluster 2022, Heidelberg, Germany.

Available on IEEE Xplore: https://ieeexplore.ieee.org/document/9912729

### Contents

Within this branch is contained:

- The source code for the XHC component, upon which work the paper is based

- The source code for the implementations of the two relevant recent research studies for
intra-node MPI collectives, as those where described in the paper
	- Shared Memory Hierarchical Collectives (SMHC)
	- XPMEM-Based Reduction Collectives (XBRC)

- XB, a complementary component to XHC, to aid in accurate micro-benchmarking

- Our modified OSU micro-benchmarks suite used for the experiments in the paper

All collectives' implementations are bundled as common components for OpenMPI's `coll` MCA 
framework. More information about the components, as well as instruction on how to build and use 
them, are available inside their respective directories.

### Acknowledgment

We thankfully acknowledge the support of the European Commission and the 
Greek General Secretariat for Research and Innovation under the EuroHPC 
Programme through the DEEP-SEA project (GA 955606). National 
contributions from the involved state members (including the Greek 
General Secretariat for Research and Innovation) match the EuroHPC 
funding.

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
