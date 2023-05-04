# Cache_eng

Cache_eng is custom software specialized in replicating & profiling low-level
shared-memory communication patterns between multiple cores in a system. It's
implemented in the C language, and realized as an MPI application, to aid in
initialization and setting up. The output of cache_eng of consists of time
measurements for the different operations that take place, and/or performance
monitoring counter events that occur during them.

#### WARNING

Cache_eng accesses & manipulates Model Specific Registers (MSRs) and
registers in the PCI configuration space. It has more privileges than ordinary
programs, and conceivably has the potential to affect the operation of the
system in perhaps destructive ways. You must thoroughly understand the
implications of these factors, and verify the functionality of the software
prior to running it. Use at your own risk.

## Configuration

Some configuration options are hard-coded inside the source code. It's highly
possible that you will need to alter them to match the characteristics of your
system, especially if you plan on capturing performance counters.

- The `NUM_SOCKETS` and `NUM_CORES_PER_SOCKET` macros in `perfctr.h`. Set to
your system's number of sockets and cores per socket.

- The `MMCONFIG_BASE` and `MMCONFIG_BOUND` macros in `perfctr.h`. Set according
to the PCI MMCONFIG area on your machine.
	- Hint: `$ sudo grep MMCONFIG /proc/iomem`

- The `PCI_PMON_bus` array in `perfctr.c`. Set the PCI bus number for each
socket on the system.
	- Hint: `$ sudo lspci -vnn | grep 8086:3450`

Furthermore, for `cache_eng` to access performance counters, the following are
necessary:

- The `msr` kernel module must be loaded.

- The user running cache_eng must have r/w access to `/dev/cpu/*/msr`.

- The user running cache_eng must have r/w access to `/dev/mem`.

- If cache_eng is run as a regular user (recommended), the executable must
have the `CAP_SYS_RAWIO` capability.

See also the [top-level README](../README.md) for more information and
instructions.

## Building

Cache_eng is built as an MPI application. A `Makefile` is included for convenience.

- The Makefile by default uses `sudo setcap` to set the `CAP_SYS_RAWIO`
capability to the new executable.
	- Set `NO_MSR=1` in the make parameter line to disable this.
	- Without it, you won't be able to work with performance counters.

- If you wish to use a prepared/external communication scenario, use the
`SC=` and `SC_FN=` parameters. Read further down regarding this functionality.

## Command line parameters

- `-x`: Number of warm-up iterations.

- `-i`: Number of actual profiled iterations.

- `-n`: Number of cache lines for which to perform the experiment.

- `-f`: Path to file containing the list of performance counter events to track.

- `-s`/`--no-times`: Do not measure/report the latency of operations.

- `--sum-socket`/`--sum-system` (default: socket): Report the sum of
performance events observed in each socket, or the across all sockets.

- `--per-box`/`--no-per-box` (default: per-box): Whether to report the
performance events observed per PMU or not. (otherwise, only the sum across
all PMUs per socket/system will be reported).

- `--no-perfctr`: Do not track and report performance counters.

- `--prefetch`: Keep prefetching enabled for the experiment. By default, it's
turned off in the beginning and re-enabled after it.

- `--ns`: Report operation latencies in nano-seconds (ns). By default, they are
reported in micro-seconds (us).

## Communication pattern

The communication pattern is programmed inside `cahce_eng.c:perform_test()`. It
looks something like the following:

```c
#ifdef EXTERNAL_SCENARIO
	#include EXTERNAL_SCENARIO
	EXTERNAL_SCENARIO_FN();
#else
	if(rank == 0) {
		// Initialize the cache lines by writing to them
		WRITE_LINES();
		
		// Set personal sequence number flag to 1
		SET(1);
	}

	if(rank == 1) {
		// Wait for rank 0 to set his flag to 1
		WAIT(0, 1);
		
		// Read the cache lines
		READ_LINES();
		
		// Set flag to 1
		SET(1);
	}

	// First core on second socket (assuming 2 sockets)
	if(rank == comm_size/2) {
		// Wait for rank 1 to set his flag to 1
		WAIT(1, 1);
		
		/* Unfreeze the counters. We are interested in the
		 * events that occur for the read that follows */
		START_COUNTERS();
		
		READ_LINES();
		
		SET(1);
	}
#endif
```

Adjust the code inside the pre-processor else-endif to implement your desired
communication scenario.

### Provided primitives

The following macros are provided for common communication actions, for ease of
implementation & integration.

- `WAIT(rank, val)`: Busy-wait until `rank` has set its flag to `val`.

- `SET(val)`: Set own flag to `val`.

- `FLAG_RESET()`: Reset flag sequence number base, for ease of use, e.g.
quickly introducing extra initialization code without manually adjusting
all subsequent WAIT/SET ops.
	- `SET(1); FLAG_RESET(); SET(1)` is equivalent to `SET(1); SET(2)`
	- This is a local operation; use it for all ranks if you want it to have
	affect all of them.

- `READ_LINES()`: Perform a load operation for all cache lines. The number of
cache lines considered for the experiment is set using the `-n` command line
parameter.

- `WRITE_LINES()`: Perform a store operation for all cache lines.

- `CLWB_LINES()`: Execute CLWB for all cache lines.

- `CLFLUSH_LINES()`: Execute CLFLUSH for all cache lines.

- `START_COUNTERS()`: Unfreeze the performance counters.

- `STOP_COUNTERS()`: Freeze the performance counters.

See the source code for additional & more specialized macros.

All macros contain appropriate `mfence` operations, to ensure that the
profiled accesses occur within the intended observation window.

### External

Instead of adjusting the code inside `perform_test()`, one may define an
external C source file that contains the communication scenario to execute.
This is achieved through the `EXTERNAL_SCENARIO` and `EXTERNAL_SCENARIO_FN`
macros. The Makefile supports automatically setting these macros through the `SC`
and `SC_FN` parameters. See the source code of `perform_test()` and `Makefile`
for reference. Also, see the included prepared scenarios under
[cache_eng/scenarios](cache_eng/scenarios) for examples on the structure of external communication
scenarios.

## Time measurements

Unless disabled via the respective command line parameters, the accesses to the
shared cache lines are timed. The average latency across all non-warmup
iterations is reported in the end. Two timing infrastructures are implemented,
using (1) `clock_gettime()`, or (2) x86's `rdtscp` (default).

When the TSC is used, in order to convert from TSC ticks to time units, the
base frequency of the processor is discovered using `cpuid`. However, this may
not work on some architectures or processor generations. Alternatively, you
may implement a different method to attain it (in `cpu_base_freq_mhz()`), or
alter the code to report elapsed ticks instead of time.

Switch between the two methods, or implement a new one, in `cache_eng.h`.

## Performance Counters

The performance events to track on each counter, in the manual mode, are
set in `cache_eng.c:perfctr_program_XXX()`. Each PMU is different; see the
existing examples in the code for reference.

The counters are frozen, i.e. they don't count events, until `START_COUNTERS()`
is called (in a non-warmup iteration). Then, they are frozen again at the end of
the iteration (or when `STOP_COUNTERS()` is called). At the end of the
experiment, the average number of event occurrences per iteration is reported.

Note that events occur in a cache line manner, so expect multiples of *n* event
counts when the experiment is performed for *n* cache lines. Keep in mind
that if too few cache lines are used, it may be hard to tell apart events
triggered because of the communication scenario, from ones triggered by system
noise.

### From file

Alternatively, the events to track may be sourced from an input file (with the
`-f` parameter). See the example files under the `events` directory for the
format of the file.

If the number of events for a specific PMON box exceed the number of its
available counters, the experiment is repeated multiple times (so, the number
of total iterations is multiplied), until it has executed with all requested
events.

## Analysis

### Tools

A few scripts to post-process the output of cache_eng are included in the
[tools](tools) directory.

- `mpi_tag.sh`: Makes the output of OpenMPI's `--output tag` more beautiful.

- `msm_avg.awk`: Takes the time measurement outputs of multiple cache_eng runs
and computes for each type of measurement the average value.

- `pmon_ev.sh`: Takes the output of captured performance events from cache_eng,
keeps only the socket/system sums, and improves the structure of the output, to
aid in analysis. Optionally receives a command line parameter, instructing to
only show the events whose counts exceed its value -- e.g. `$ pmon_ev.sh 100`.

## Miscellaneous

#### Prefetching

Cache_eng automatically disables prefetching across all cores, and re-enables
it when the experiment is over. If it's interrupted, or if something else goes
wrong, the system might be left in a state where prefetching is disabled, which
can heavily degrade performance in subsequent use.

- Use `sudo rdmsr -ac 0x1A4` when done, to ensure prefetching is enabled. All
lines must show `0x0`.

- If any core does not report `0x0`, you may use `sudo wrmsr -a 0x1A4 0x0` to
manually enable set the correct value.
	- Or, alternatively, if prefetching was not re-enabled because cache_eng was
	interrupted, re-run it and let it properly finish.

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
