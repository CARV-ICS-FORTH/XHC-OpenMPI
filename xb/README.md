# XB: XHC's Barrier

XB is an assisting OpenMPI component, that provides the MPI_Barrier that XHC
implements, for benchmarking purposes.

XHC's barrier is the best performing one across the board, but it's preferred
that the component that provides the implementation of the barrier that appears
before each micro-benchmark operation is not one of those being measured. XB
creates a new nested (duplicate) communicator with hints to prefer XHC, and
delegates barrier operations to it. See more details about it and the source of
its necessity, in [XHC's README file](../xhc/README.md#microbenchmark-barrier).

<sub>
While XB is designed and named after XHC, it can actually be easily converted
to perform the same task with some other component. See the
ompi_comm_coll_preference info key in coll_xb_barrier.c:xb_lazy_init(). </sub>

## Building

XB is built as an ordinary OpenMPI component, under the coll MCA framework.
Place the component's folder under `ompi/mca/coll`, and build OpenMPI as
normally. Be sure to run the autogen script before configuring, so that the
required files for the new component will be generated.

General information on building OpenMPI can be found in its documentation:
<https://www.open-mpi.org/doc/v5.0>  
<https://docs.open-mpi.org/en/v5.0.x/developers/autogen.html>  
<https://www.open-mpi.org/faq/?category=building>  
<https://github.com/open-mpi/ompi/blob/master/README.md>

## Running

General information on running Open MPI jobs can be found here:  
<https://www.open-mpi.org/faq/?category=running>  
<https://docs.open-mpi.org/en/v5.0.x/launching-apps/index.html>

mpirun's man page will also be useful:  
<https://docs.open-mpi.org/en/v5.0.x/man-openmpi/man1/mpirun.1.html>

XB follows OpenMPI's conventions for collective component selection. Make sure
to include it in the coll list (`--mca coll` parameter), and give it a priority
higher than that of any other component that also provides the `MPI_Barrier`
operation (`--mca coll_xb_priority` parameter).

Furthermore, you need to make sure that XHC is available, as XB will attempt to
select it for its sub-communicator. Therefore, include it in the coll list, and
make sure that its priority is not less than zero.

### Examples

Benchmarking `coll/tuned` with XB:

`$ mpirun --mca coll basic,libnbc,tuned,xb,xhc --mca coll_xhc_priority 0 --mca coll_xb_priority 95 --mca coll_tuned_priority 90 <application>`

Benchmarking XHC itself, with XB:

`$ mpirun --mca coll basic,libnbc,xb,xhc --mca coll_xhc_priority 90 --mca coll_xb_priority 95 <application>`

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
