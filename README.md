# Impact of Cache Coherence on the Performance of Shared-Memory based MPI Primitives: A Case Study for Broadcast on Intel Xeon Scalable Processors

This branch holds material for reproducing the experiments in the paper.

## Contents

The material is organized in directories, as such:

- **xhc**: The source code of the baseline XHC component implementation.
	- Variants implementing improvements included in the form of patches.

- **xb**: The source code of the XB component, utilized for the barrier
synchronizing rank entry in the micro-benchmarks.

- **ompi_patches**: A small collection of fixes/improvements for OpenMPI,
to be applied before building it.

- **osu-micro-benchmarks**: A modified version of the OSU micro-benchmark
suite, which is used for the experiments.

- **effect**: Scripts to perform benchmarks and plot their results, to
highlight the observed performance anomaly phenomenon.

- **remedies**: Scripts to benchmark and plot the performance of software
remedies in comparison to the baseline implementation.

- **cache_eng**: The software utilized for the analysis of the cache
coherency protocol.

- **coherency-protocol**: Scripts to aid in performing the coherency
protocol experiments, and example written analysis of expected results,
for reference.

- **utils**: Miscellaneous supporting scripts for other tools. You're probably
not interested in this.

See also the README in each respective directory for extra information

## Reproducing experiments

Below is a high-level view of the process for reproducing the experiments from the
paper. The rest of the sections in this document will go into more detail.

1. Build OpenMPI & deploy, including XHC and XB.

2. Build & deploy the (custom) OSU micro-benchmark suite.

3. Use the provided run-scripts (in dirs `effects`, `remedies`) to perform the
benchmarks, and the respective plotting scripts to plot the results.

4. Use the provided run-scripts to perform the cache coherency protocol
experiments (in dir `coherency-protocol`), use the included scripts and
instruction to analyze the results.

## Building

### OpenMPI+XHC/XB

The XHC & XB components require OpenMPI version 5; the experiments in the paper
are performed using `v5.0.0rc10`. Both are drop-in components for the `coll`
framework of OpenMPI; one needs only place the components in the appropriate
directory (`ompi/mca/coll/`), re-run the autogen script, and build OpenMPI.

In order to reap the full benefits of XHC, and correctly reproduce the results
of the paper, XPMEM support in OpenMPI is required.

- XPMEM can be obtained from <https://github.com/hpc/xpmem>. For the
experiments in the paper we used git hash `61c39ef`. See the included
[instructions in XPMEM's repository](https://github.com/hpc/xpmem/blob/master/INSTALL) 
in order to build it. You may need to manually point OpenMPI's
configure script to XPMEM's installation, via the `--with-xpmem=` parameter.

- At run-time, you will need to insert the kernel module and obtain proper
access rights to `/dev/xpmem`.

Apart from instructing Open MPI to include XPMEM support, the rest of the build
process is standard. General information on building Open MPI can be found in
its documentation:

<https://www.open-mpi.org/doc/v5.0>  
<https://www.open-mpi.org/faq/?category=building>  
<https://github.com/open-mpi/ompi/blob/master/README.md>

Furthermore, pay attention to the following items, which are required in order
to correctly support the run-scripts:

- Make sure to run `make-git.sh` in XHC's directory, which prepares git
branches from the included patches that implement the various variants. Refer
to the detailed instructions below. Do this before building OpenMPI.

- Make sure to include `--enable-mca-dso=coll-xhc` in the OpenMPI configure
line. This allows the run-scripts to quickly recompile XHC between variants.

- Apply the OMPI patches in the `ompi-patches` dir. These patches fix a bug
that would prevent correct operation of the XB component, and add an
optimization for the XPMEM registration cache.

Below is a detailed example of building OpenMPI+XHC/XB. Pay attention to the
commands, and appropriately adjust any installation paths or build/config
directives according to your specific preferences.

```shell
# Get artifacts
$ cd "$HOME"
$ git clone -b icpp-23 https://github.com/CARV-ICS-FORTH/XHC-OpenMPI

# Build XPMEM
$ git clone https://github.com/hpc/xpmem xpmem-src
$ cd xpmem-src
$ ./autogen.sh
$ ./configure --prefix "$HOME/xpmem" # --with-kerneldir=<kernel source>
$ make -sj16
$ make install
$ cd ..

# Insert XPMEM's kernel module
$ sudo insmod $(find "$HOME/xpmem" -name xpmem.ko | head -n 1)
$ sudo chmod 666 /dev/xpmem

# Get OpenMPI 5.0.0rc10
$ git clone -b v5.0.0rc10 https://github.com/open-mpi/ompi ompi-xhc-src
$ cd ompi-xhc-src

# Add XHC/XB
$ cp -r ../XHC-OpenMPI/{xhc,xb} ompi/mca/coll/
$ (cd ompi/mca/coll/xhc && ./make-git.sh)

# Apply OMPI patches
$ git am ../XHC-OpenMPI/ompi-patches/*

# Build OMPI
$ git submodule update --init --recursive
$ AUTOMAKE_JOBS=16 ./autogen.pl
$ ./configure --prefix="$HOME/ompi-xhc" --enable-mca-dso=coll-xhc --with-pmix=internal --with-xpmem="$HOME/xpmem"
$ make -sj16
$ make -s install
$ cd ..
```

At this point, if all went well, the installation of OpenMPI is present in
`$HOME/ompi-xhc` (or in the respective alternate directory chosen). For the
next steps, you need to add the appropriate directory to `PATH` and `LD_LIBRARY_PATH`.
For example:

```shell
$ export PATH="$HOME/ompi-xhc/bin:$PATH"
$ export LD_LIBRARY_PATH="$HOME/ompi-xhc/lib:$LD_LIBRARY_PATH"
```

Finally, verify that the installation is OK:

```shell
$ which mpicc # should show sth like $HOME/ompi-xhc/bin/mpicc
$ which mpicxx # should show sth like $HOME/ompi-xhc/bin/mpicxx
$ which mpirun # should show sth like $HOME/ompi-xhc/bin/mpirun

$ mpirun hostname
$ mpicc "$HOME/ompi-xhc-src/examples/hello_c.c" -o hello_c
$ mpirun hello_c
```

### OSU micro-benchmarks

The build process for the OSU micro-benchmarks is fairly straightforward. With
the OpenMPI installation included in `PATH`/`LD_LIBRARY_PATH`:

```shell
$ cp -r XHC-OpenMPI/osu-micro-benchmarks osu-xhc
$ cd osu-xhc

$ ./autogen.sh
$ ./configure CC=mpicc CXX=mpicxx
$ make -sj 16

$ cd ..
```

## Performance experiments

### Running

The experiments are split up in two groups: (1) [effect](effect), (2)
[remedies](remedies). They can be performed easily via the included scripts;
detailed information is found in the README file inside each respective
directory. It's recommended to go through the scripts themselves, and verify
that their default configuration options are suitable. If the paths recommended
in the building instructions above are followed, likely no adjustments will be
necessary. Both groups' experiments are expected to complete in a matter of few
hours.

Example sequence:

```shell
$ cd "$HOME"

$ nano XHC-OpenMPI/effect/run-effect.sh
$ XHC-OpenMPI/effect/run-effect.sh

# WARNING Don't execute run-remedies.sh on multiple systems at the
# same time, if they share an OpenMPI installation (e.g. over NFS)

$ nano XHC-OpenMPI/remedies/run-remedies.sh
$ XHC-OpenMPI/remedies/run-remedies.sh
```

### Plotting results

Similarly to running the experiments, included scripts may be used to plot the
results. Refer to the README file in each respective directory for instruction
on these scripts.

Keep in mind that you will need to manually place the raw data that the run
scripts will output, into appropriate directories. The required pattern is the
respective README files ([effect/README.md](effect),
[remedies/README.md](remedies)).

Example commands to plot the data:

```shell

# Organize raw data into {effect,remedies}/data; check respective READMEs
# This command might help automate the process (assumes default paths):

$ for type in effect remedies; do for host in $(find "$HOME/bcast_$type" -type f -name '*.txt' -printf "%f\n" | sed -r 's/(.*)_xhc_.*/\1/' | uniq); do mkdir -p $HOME/XHC-OpenMPI/$type/data/$host; cp "$HOME/bcast_$type"/${h}* $HOME/XHC-OpenMPI/$type/data/$host; done; done

$ cd "$HOME/XHC-OpenMPI/effect"
$ ./plot-effect.sh

# See remedies/README.md for remedy tuning options

$ cd ../remedies
$ nano plot-remedies.py plot-remedies-speedup-all.py
$ ./plot-remedies.sh
```

## Cache coherency protocol experiments

Finally, the experiments that aid in the analysis of the cache coherency
protocol follow. The `cache_eng` software is used for these experiments. The
experiments are of two types: (1) experiments demonstrating the coherence state
transitions, and (2) experiments that monitor hardware performance monitoring
events for various communication scenarios. More details about cache_eng are
available inside its directory. Additional information about these exeperiments
is found inside the [coherency-protocol](coherency-protocol) directory.

### State transitions

This experiment does not require much set-up, and can be run on any generation
of the Intel Xeon Scalable processor architecture. Use
`./run-state-transitions.sh` to run the experiment, and
`./analyze-state-transitions.awk` to process the results. See the
[coherency-protocol](coherency-protocol) directory for more information about the
run-script & its configuration options, as well as how to interpret the
output of the analysis script.

Example sequence:

```shell
cd "$HOME/XHC-OpenMPI/coherency-protocol"

$ ./run-state-transitions.sh
$ ./analyze-state-transitions.awk "$HOME/protocol_state_transitions"/*.txt

```

### Communication scenarios

For the communication scenarios, some system configuration is necessary.
Root-level access on the system will be required. Note that this experiment
can currently only be run on the **Ice Lake** architecture.

#### WARNING

The `cache_eng` software accesses & manipulates Model Specific Registers (MSRs),
and registers in the PCI configuration space. It has more privileges than common
programs, and conceivably has the potential to affect the operation of the system
in perhaps destructive ways. You must thoroughly understand the implications of
these factors, and verify the functionality of the software prior to running it.
Use at your own risk.

#### Preparation

For cache_eng to correctly initialize and capture performance counters, the following
configurations are necessary:

- The `msr` kernel module must be loaded.

- The user running cache_eng must have r/w access to `/dev/cpu/*/msr`.

- The user running cache_eng must have r/w access to `/dev/mem`.

- If cache_eng is run as a regular user (recommended), the executable must
have the `CAP_SYS_RAWIO` capability.
	
	- The Makefile for `cache_eng` by default does this after each compilation,
	using `sudo setcap cap_sys_rawio=ep`. This work-flow might not be desirable
	for you. Note that with this method, you may be prompted to enter your
	password (for the sudo command) during the execution of the experiment.

Below follow example commands for these configurations:

```shell
# Access to MSRs
$ sudo modprobe msr
$ sudo groupadd msr
$ sudo usermod -aG msr <user>
$ sudo chown root:msr /dev/cpu/*/msr
$ sudo chmod g+rw /dev/cpu/*/msr

# Access to /dev/mem
$ sudo usermod -aG kmem <user>
$ sudo chmod g+rw /dev/mem

# Remember to re-login for groups to take effect and re-export PATH/LD_LIBRARY_PATH
```

Furthermore, some configuration is necessary inside `cache_eng`'s source code.

- In [cache_eng/perfctr.h](), lines 15-16, set your system's *number of CPU
sockets* and *cores per socket*.

- In [cache_eng/perfctr.h](), lines 45-46, set the mmconfig area values for
your system.

- In [cache_eng/perfctr.c](), line 278, set the PCI bus number for each CPU socket.

Example how to obtain these values:

```shell
$ lscpu | grep -P '(Socket\(s\)|Core\(s\) per socket)'
Core(s) per socket:              24
Socket(s):                       2

$ sudo grep MMCONFIG /proc/iomem
  80000000-8fffffff : PCI MMCONFIG 0000 [bus 00-ff]

$ sudo lspci -vnn | grep 8086:3450
7e:00.0 System peripheral [0880]: Intel Corporation Device [8086:3450]
fe:00.0 System peripheral [0880]: Intel Corporation Device [8086:3450]
```

In this example, the following values are used:

```c
// perfctr.h:15-16
#define NUM_SOCKETS 2
#define NUM_CORES_PER_SOCKET 24

// perfctr.h:45-46
#define MMCONFIG_BASE 0x80000000
#define MMCONFIG_BOUND 0x8FFFFFFF

// perfctr.c:278
uint PCI_PMON_bus[NUM_SOCKETS] = {0x7e, 0xfe};
```

Finally, run cache_eng to make sure that the system is configured properly.

```shell
$ (cd XHC-OpenMPI/cache_eng; make clean; make; mpirun --bind-to core ./cache_eng -sn 100)
```

If you get a bunch of information and captured counters, and not any errors,
you are set to go. Example expected output:

```
$ (cd XHC-OpenMPI/cache_eng; make clean; make; mpirun --bind-to core ./cache_eng -sn 100)
rm -f cache_eng
mpicc -Wall -Wno-unused-variable -g -march=icelake-server   -o cache_eng \
	cache_eng.c utils.c perfctr.c -lrt -lmpi
sudo setcap cap_sys_rawio=ep "cache_eng"
Active CPUs: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 
Setting up MSR
Programming CORE counters
Programming CHA counters
Programming M2M counters
Programming IMC counters
CORE 0 MEM_LOAD_RETIRED.L2_MISS 19
CORE 24 MEM_LOAD_RETIRED.L2_MISS 99
CORE 0 MEM_LOAD_RETIRED.L2_HIT 2
CORE 24 MEM_LOAD_RETIRED.L2_HIT 1
====================
SOCKET 0 ALL CORE MEM_LOAD_RETIRED.L2_MISS 19
SOCKET 0 ALL CORE MEM_LOAD_RETIRED.L2_HIT 2
SOCKET 1 ALL CORE MEM_LOAD_RETIRED.L2_MISS 99
SOCKET 1 ALL CORE MEM_LOAD_RETIRED.L2_HIT 1

CHA 0:00 LLC_LOOKUP.ALL 9
CHA 0:01 LLC_LOOKUP.ALL 8
...
...
CHA 1:22 LLC_LOOKUP.ALL 6
CHA 1:23 LLC_LOOKUP.ALL 5
====================
SOCKET 0 ALL CHA LLC_LOOKUP.ALL 193
SOCKET 1 ALL CHA LLC_LOOKUP.ALL 182

====================

IMC 0:0:0 CAS_COUNT.RD 31
...
...
IMC 1:3:0 CAS_COUNT.WR 2
====================
SOCKET 0 ALL IMC CAS_COUNT.RD 117
SOCKET 0 ALL IMC CAS_COUNT.WR 137
SOCKET 1 ALL IMC CAS_COUNT.RD 3
SOCKET 1 ALL IMC CAS_COUNT.WR 6
Cleaning up MSR
```

#### Execution

Once the system is properly prepared, the experiment can be run using the
included `run-protocol.sh` script. See the README in the [coherency-protocol](coherency-protocol)
directory for configuration options and further instructions, including how
to interpret the results.

```shell
$ cd "$HOME/XHC-OpenMPI/coherency-protocol"

$ ./run-protocol.sh
$ tree "$HOME/protocol" "$HOME/XHC-OpenMPI/coherency-protocol/analysis"
```

Finally, note that cache_eng globally disables pre-fetching while running.
It is re-enabled automatically, but this might not happen, e.g. if the
execution was interrupted. Use the following command to verify that prefetching
is enabled:

```shell
$ sudo rdmsr -ac 0x1A4
```

A value of `0x0` means that the prefetchers are enabled; all lines should have
this value. If any lines do not, use the following command to manually set the
correct one.

```shell
$ sudo wrmsr -a 0x1A4 0x0
```

##  Acknowledgments

We thankfully acknowledge the support of the European Commission and the Greek
General Secretariat for Research and Innovation under the EuroHPC Programme
through the **DEEP-SEA** project (GA 955606). National contributions from the
involved state members (including the Greek General Secretariat for Research
and Innovation) match the EuroHPC funding.

This work is partly supported by project **EUPEX**, which has received funding
from the European High-Performance Computing Joint Undertaking (JU) under grant
agreement No 101033975. The JU receives support from the European Union's
Horizon 2020 re-search and innovation programme and France, Germany, Italy,
Greece, United Kingdom, Czech Republic, Croatia.

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
