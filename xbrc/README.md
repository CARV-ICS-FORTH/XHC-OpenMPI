# XBRC: XPMEM-Based Reduction Collectives

Implementation of MPI Allreduce as described in **Designing Efﬁcient Shared
Address Space Reduction Collectives for Multi-/Many-cores**, at IPDPS-18 -- https://ieeexplore.ieee.org/document/8425255.

## Building

### Prerequisites

- OpenMPI version 5 (tested with `v5.0.0rc6`)

- [XPMEM](https://github.com/hjelmn/xpmem)'s user-level library and kernel module, with appropriate 
access to `/dev/xpmem`

### Process

1. Place the component's folder and its contents inside OpenMPI's `coll` framework(`ompi/mca/coll`)

2. Run `autogen.pl`, so that it will generate the required build files for the new component

3. Configure OpenMPI, ensuring that XPMEM support is included
	- Use `--with-xpmem=<PATH>` parameter to explicitely request it

	- If all has gone well, at this point you will find in the configure log, a line confirming
	XBRC's inclusion: `checking if MCA component coll:xbrc can compile... yes`.
	
4. Compile (`make`) and install (`make install`) OpenMPI

General information on building OpenMPI can be found in its documentation:  
https://www.open-mpi.org/faq/?category=building  
https://github.com/open-mpi/ompi/blob/master/README.md

## Running

General information on running Open MPI jobs can be found here:  
https://www.open-mpi.org/faq/?category=running

`mpirun`'s man page will also be useful:  
https://docs.open-mpi.org/en/v5.0.x/man-openmpi/man1/mpirun.1.html

XBRC follows OpenMPI's conventions for collective component selection. Make sure to include it in
the `coll` list (`--mca coll` parameter), and give it a priority higher than that of any other
component that also provides the `Allreduce` operation (`--mca coll_xbrc_priority` parameter).

Example command line:  
`$ mpirun --mca coll basic,libnbc,tuned,xbrc --mca coll_xbrc_priority 100 <application>`

---
Contact: George Katevenis, gkatev@ics.forth.gr  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
