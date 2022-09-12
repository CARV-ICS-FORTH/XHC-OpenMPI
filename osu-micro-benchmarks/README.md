# OSU Micro-benchmarks

Micro-benchmarks based on the OSU micro-benchmarks suite v5.8
(https://mvapich.cse.ohio-state.edu/benchmarks/)

## Modifications

Notable delta from OSU the distribution:

-  The `_dv` collective benchmark variants, which modify the message data before each operation, 
to avoid implicit data caching  across operations.
- The `_integrity` variants, which verify the correctness of the output data after each operation.
- Allow message sizes with modifiers (e.g. `-m 1M`)
- *Possibly more!*
	- Referring to the actual code is always the safest bet for seeing what a micro-benchmark
	actually does.

## Building

```
$ ./autogen.sh
$ ./configure CC=mpicc CXX=mpicxx
$ make all
```

## Running

Run as you would with the standard OSU suite, and as you would a common MPI application.

**Examples**

MPI_Bcast 1 to 1M bytes:

```
$ mpirun mpi/collective/osu_bcast_dv -m 1:1M
```

MPI_Allreduce 4 to 8M bytes:

```
$ mpirun mpi/collective/osu_allreduce_dv -m 4:8M
```

MPI_Allreduce with integrity check:

```
$ mpirun mpi/collective/osu_allreduce_integrity
```

## Paper experiments

Some/most of the MPI-related settings for the experiments in the paper:

```
OpenMPI v5.0.0rc6, UCX v.12.1, UCC v1.0.0, 
Built with XPMEM support

Map to slot, bind to core, only using 1 hyperthread/core

pml=ob1, or for coll/tuned pml=ucx
btl=sm,self
smsc=xpmem

pml_ucx_tls=any
UCX_TLS=xpmem,self
```

Finally, make sure that the implementation of MPI_Barrier performs as good as possible, since it
affects the degree to which the different processes enter the collective at the same time. Ideally,
the component providing this performant barrier is a distinct one, different from the one being
measured, and is the same for all benchmarks. This condition is most important for small messages,
but can/will also affect large-message performance as well.

---
Contact: George Katevenis, gkatev@ics.forth.gr  
Foundation for Research and Technology - Hellas (FORTH), Institute of Computer Science
