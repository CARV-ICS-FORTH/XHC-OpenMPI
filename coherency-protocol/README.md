# Coherency Protocol Analysis

This directory contains scripts that aid in executing the experiments for
dissecting the operation of the cache coherency protocol. Furthermore, it
contains accompanying material an instructions on interpreting the results.

## Tools & scripts

### run-state-transitions.sh

This scripts automates the process of running the experiments that reveal the
coherence state transitions of the processor. No special system configuration
is necessary for these experiments.

A number of configurations can be made inside the script.
Adjustment/verification of the highlighted/underlined items is strongly
recommended.

- **REPS** (default `30`): Number of times to repeat each run, to rule out
noise and one-off results.

- **RESULT_DIR** (default `$HOME/protocol_state_transitions`): Location in
which the script will place the results of the benchmarks.

- <ins>**CACHE_ENG_DIR**</ins> (default `$HOME/XHC-OpenMPI/cache_eng`):
Location of the `cache_eng` software.

### analyze-state-transitions.awk

The results from `run-state-transition.sh` are time measurements of different
ranks, but are in a raw format. This tool extracts the meaningful values for
this analysis, and displays them in a pretty format. Read further down regarding
how to interpret the output.

Usage:

```shell
$ ./analyze-state-transitions.awk "$HOME/protocol_state_transitions"/*.txt
remote read, after local read : 219 ns
local read                    : 20 ns
local read, after remote read : 163 ns
remote read                   : 21 ns
```

### run-protocol.sh

Finally, this script automatically performs the execution of the communication
scenarios (of both single- and multi- socket types). The results consist of the
performance counter events captured during the scenario, and are placed in text
files. See the [root README](../README.md) for the system configuration necessary
before running this experiment.

A number of configurations can be made inside the script.
Adjustment/verification of the highlighted/underlined items is strongly
recommended.

- **RESULT_DIR** (default `$HOME/protocol`): Location in which the script will
place the results of the benchmarks.

- <ins>**CACHE_ENG_DIR**</ins> (default `$HOME/XHC-OpenMPI/cache_eng`):
Location of the `cache_eng` software.

Example:

```
$ ./run-protocol.sh
$ cat "$HOME/protocol/local"/*_sc_l_1.txt
SC_L_1: Init = root write. Action = local read

CORE    0  _    CORE_SNOOP_RESPONSE.I_FWD_M        1005

CORE    1  _    L2_RQSTS.DEMAND_DATA_RD_MISS       986
CORE    1  _    MEM_LOAD_RETIRED.L2_MISS           985
CORE    1  _    OFFCORE_REQUESTS.DEMAND_DATA_RD    986
CORE    1  _    LONGEST_LAT_CACHE.REFERENCE        1003
CORE    1  _    MEM_LOAD_L3_HIT_RETIRED.XSNP_FWD   984
CORE    1  _    L2_LINES_OUT                       1003
CORE    1  _    L2_LINES_IN                        1003

SOCKET  0  CHA  LLC_LOOKUP.READ_HIT                1144
SOCKET  0  CHA  LLC_LOOKUP.READ_SF_HIT             1074
SOCKET  0  CHA  LLC_LOOKUP.SF_E                    1055
SOCKET  0  CHA  CORE_SNP.CORE_ONE                  1052
SOCKET  0  CHA  _XSNP_RESP._CORE_RSPI_FWDM         1024
SOCKET  0  CHA  SNOOP_RSP_MISC.M_TO_I_RSP_I_FWD_M  1027

```

Read further down regarding how to interpret these results.

## Analyzing results

### State transitions

The state-transition experiment executes certain communication sequences. Of
interest is the last *read* operation in the sequence: we want to know the
state in which the reader acquired the cache line. It's possibly to
heuristically infer this state, by then having this reader *write* to the
cache line. If this write has especially low latency, it means that the read
operation acquired the cache line in an exclusive manner, i.e. in the exclusive
(E) or in the modified (M) state. If instead it was acquired in a shared manner
(states shared (S) or forward (F)), the necessary RFO will be sent to the CHA,
cross-core or cross-socket snoops might need to be performed, and only after all
these operations have completed will the write to the line take place. All
these extra steps elevate the latency of the write operation.

Use the output of `analyze-state-transitions.awk` to see the latency of this
write operation by each respective actor, and thus infer the state in which he
acquired the line in the read operation. For example, if the latency is a couple
10s of nano-seconds or less, it was taken in the exclusive state. If the latency
is higher, e.g. couple 100s of nano-seconds or more, it was taken in the shared
state.

An example output and written analysis is present in
[analysis/state_transitions.txt](analysis/state_transitions.txt).

### Coherency protocol

The output from the communication scenarios experiment is comprised of
performance counter events that occured during the action of interest. For each
scenario, one needs to carefully observe the events and infer the steps that
took place in order to service the action. Each scenario is performed for 1000
cache lines, in order to account for noise, so the reported event counts are
(almost) multiples of 1000. We use these results to generalize for a single
cache line, so when the count is almost 1000, we say that for the single cache
line the event would have occured once. The margin is very small for some
events (e.g. 995-1005), and a bit larger for other ones (e.g. 950-1200); some
events are inherently more noisy than other ones. See the [cache_eng's
documentation](../cache_eng/README.md) for more information.

Example outputs for each scenario, with an included written analysis is
included, in the [analysis/local_scenario.txt](analysis/local_scenario.txt) and
[analysis/remote_scenario.txt](analysis/remote_scenario.txt) files.

#### Performance events

The meaning of most performance events can be inferred through their names. Our
included written-analysis of the results also further assists in this. The full
lists of considered events, for the single-sockket and the multi-socket scenarios,
are located in [events/ev_local.input](../cache_eng/events/ev_local.input) and
[events/ev_remote.input](../cache_eng/events/ev_remote.input), respectively,
in cache_eng's source tree. See the inline comments inside these files for a
description of each event.

For further, potentially-but-not-necessarily more detailed information, refer
to the following resources from Intel:

- Ice Lake Uncore Performance Monitoring Reference Manual:
https://www.intel.com/content/www/us/en/content-details/639778

- Skylake Uncore Performance Monitoring Reference Manual:
https://kib.kiev.ua/x86docs/Intel/PerfMon/336274-001.pdf

- Intel Performance Monitoring Events Reference:
https://perfmon-events.intel.com

- Intel Performance Monitoring Events GitHub Repository:
https://github.com/intel/perfmon
