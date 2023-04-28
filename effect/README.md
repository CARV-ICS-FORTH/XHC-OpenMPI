# Performance Anomaly

This directory contains material & tools to reproduce and demonstrate the
observed performance anomaly on multi-socket nodes based on processors of the
Intel Xeon Scalable architecture.

## Tools & scripts

### run-effect.sh

This script automates the process of running benchmarks that highlight the
effect. It benchmarks the performance of MPI Broadcast (using `osu_bcast_dv`,
see about [our modified OSU micro-benchmark suite](../osu-micro-benchmarks))
for continuously increasing rank counts.

A number of configurations can be made inside the script.
Adjustment/verification of the highlighted/underlined items is strongly
recommended.

- **REPS** (default `30`): Number of times to repeat each micro-benchmark, to
rule out noise and one-off results.

- **MAX_RANKS** (default `mpirun echo | wc -l`): Max number of ranks that the
system can support with over-subscribing.

- **RESULT_DIR** (default `$HOME/bcast_effect`): Location in which the script
will place the results of the benchmarks.

- <ins>**OSU_DIR**</ins> (default `$HOME/osu-xhc/mpi/collective`): Location of
the MPI collective benchmarks inside the modified OSU micro-benchmark suite.

Furthermore, it's recommended to go through the script to see how it works, the
actions that it performs, and the OpenMPI configurations it applies.

### plot-effect.sh

This is a simple helper utilizing the co-located plotting scripts to
auto-magically generate graphics that illustrate the effect, similarly to the
those in the paper. It uses the data generated from the `run-effect.sh` script.

The plotting scripts expect the data to reside in specific directories. For
each node on which the experiment is run, a `data/<hostname>` directory needs
to be created, for each host. The `.txt` files created by `run-effect.sh`
(found by default under `$HOME/bcast_effect`) need to be placed inside the
appropriate host directory -- consult the names of the files for the hostname.
The actual raw results featured in the paper are already included inside the
data directory, for reference. Remove them from the `data` folder if you don't
want them included in the plots.

Example structure:

```
effect
├── data
│   ├── CSX-48
│   │   ├── CSX-48_xhc_flat_16K_02x.txt
│   │   ├── CSX-48_xhc_flat_16K_03x.txt
│   │   ...
│   │   ...
│   │   └── CSX-48_xhc_flat_16K_48x.txt
│   ├── ICX-48
│   │   ├── ICX-48_xhc_flat_16K_02x.txt
│   │   ├── ICX-48_xhc_flat_16K_03x.txt
│   │   ...
│   │   ...
│   │   └── ICX-48_xhc_flat_16K_48x.txt
│   └── SKX-24
│       ├── SKX-24_xhc_flat_16K_02x.txt
│       ├── SKX-24_xhc_flat_16K_03x.txt
│       ...
│       ...
│       └── SKX-24_xhc_flat_16K_24x.txt
```

Once the required structure is prepared, simple execute `plot-effect.sh`. The
script appropriately calls the python scripts. The plots are placed under a new
directory named `plots`.

The following plot files will be generated:

- `effect_<host>.svg` (for each host): Shows the effect, when the 2nd
socket is populated, in high-level view.

- `effect_locality_<host>.svg` (for each host): Same illustration as
before, except instead of the average latency of all ranks, this plot shows
the average latency of local ranks (ranks residing in the same as the root),
and the average latency of remote ranks.

- `effect_locality_slowdown_all.svg`: Plots, for all hosts in the same graph,
the slowdown in the average broadcast latency, when the second socket is fully
populated.

Note that the following external python packages are required:

- `matplotlib`, recommended version `3.7.1`
- `seaborn`, recommended version `0.12.2`
- `pandas`, recommended version `1.5.3`
- `numpy`, recommended version `1.24.2`

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
