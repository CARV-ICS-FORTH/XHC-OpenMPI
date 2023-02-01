# OSU Micro-benchmarks

Micro-benchmarks based on the OSU micro-benchmarks suite v7.0
(https://mvapich.cse.ohio-state.edu/benchmarks/)

## Modifications

Notable delta from OSU the distribution:

-  The `_dv` collective benchmark variants, which modify the message data
before each operation, to avoid implicit data caching  across operations.
- The `_integrity` variants, which verify the correctness of the output data
after each operation.
- Allow message sizes with modifiers (e.g. `-m 1M`)
- *Possibly more!*
	- Referring to the actual code is always the safest bet for seeing what a
	micro-benchmark actually does.

## Building

With mpicc/mpicxx on the `PATH`:

```
$ ./autogen.sh
$ ./configure CC=mpicc CXX=mpicxx
$ make all
```

## Running

Run as you would with the standard OSU suite, and as you would a common MPI
application.

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

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
