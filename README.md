# XPMEM-based Hierarchical Collectives

This branch houses the most up-to-date version of the XHC collectives implementation
and of its accompanying software.

### Contents

Within this branch you will find:

- The source code of the XHC component ([xhc](xhc))
- XB, a complementary component to XHC, to aid in accurate micro-benchmarking ([xb](xb))
- Our modified OSU micro-benchmarks suite we use for experiments pertaining
to intra-node MPI collectives ([osu-micro-benchmarks](osu-micro-benchmarks))

See the README file in each respective directory for more information.

### Publications

Publications related to XHC

1. **A framework for hierarchical single-copy MPI collectives on multicore nodes**,  
*George Katevenis, Manolis Ploumidis, and Manolis Marazakis*,  
IEEE Cluster 2022, Heidelberg, Germany.  
https://ieeexplore.ieee.org/document/9912729

	- See also the `ieee-cluster-22` branch in this repository.

##  Acknowledgments

We thankfully acknowledge the support of the European Commission and the Greek General Secretariat for Research and Innovation under the EuroHPC Programme through the **DEEP-SEA** project (GA 955606). National contributions from the involved state members (including the Greek General Secretariat for Research and Innovation) match the EuroHPC funding.

This work is partly supported by project **EUPEX**, which has received funding from the European High-Performance Computing Joint Undertaking (JU) under grant agreement No 101033975. The JU receives support from the European Union's Horizon 2020 re-search and innovation programme and France, Germany, Italy, Greece, United Kingdom, Czech Republic, Croatia.

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth