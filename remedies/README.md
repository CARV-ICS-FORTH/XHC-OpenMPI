# Software Remedies

This directory contains material & tools to benchmark the performance of the
broadcast collective with the proposed software remedies. In this repository,
5 different variants of XHC are included:

1. The baseline implementation.
	- Codename *baseline* or *vanilla*.

2. The remedy(-ies) that orders access of local/remote ranks.
	- Codename *delay-flag*, *delay-flag-strict*, or *wait*.

3. The remedy that utilizes multiple internal buffers and synchronization flags
	- Codename *dual buffer* or *dual*

4. The remedy that uses the CLWB instruction.
	- Codename *clwb*

5. The final optimized cache-coherency--aware version that combines multiple remedies
	- Codename *optimized* or *opt*

The included [XHC code-base](../xhc) is the baseline implementation, while the
rest of the variants are attached in the form of [patches](../xhc/patches).

## Tools & scripts

### run-remedies.sh

This script automates the process of benchmarking MPI_Bcast with the different
variants. It automatically applies each of the variants above, recompiles XHC,
and performs the experiment.

The different variants have already been placed inside distinct git branches
during the [OpenMPI/XHC build process](../README.md#building). Note that if the
script is interrupted, the XHC source-tree and installation might be left in a
state where a variant other that the baseline implementation is active. If this
happens, manually switch to the *vanilla* git branch, and re-install XHC.
Furthermore, note that because the script automatically rebuilds XHC (for ease
of use), it mustn't be executed on multiple systems that share the same OpenMPI
installation (e.g. over NFS), at the same time.

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

- <ins>**XHC_DIR**</ins> (default `$HOME/ompi-xhc/ompi/mca/coll/xhc`): Location
of the XHC component within the OpenMPI installation. This is necessary in
order for the script to switch between the variants and re-compile XHC.

Finally, it's recommended to go through the script to see how it works, the
actions that it performs, and the OpenMPI configurations it applies.

### plot-remedies.sh

This helper utilizes the co-located plotting scripts to automatically generate
graphics that illustrate the performance of the different remedies compared to
the baseline algorithm. It uses the data generated from `run-remedies.sh`.

The plotting scripts expect the data to reside in specific directories. For
each node on which the experiment is run, a `data/<hostname>` directory needs
to be created, for each host. The `.txt` files created by `run-remedies.sh`
(found by default under `$HOME/bcast_remedies`) need to be placed inside the
appropriate host directory -- consult the names of the files for the hostname.
The actual raw results featured in the paper are already included inside the
data directory, for reference. Remove them from the `data` folder if you don't
want them included in the plots.

Example structure:

```
remedies
├── data
│   ├── ICX-48
│   │   ├── ICX-48_xhc_clwb_flat_16K.txt
│   │   ├── ICX-48_xhc_dual_[0..23],node_16K.txt
│   │   ├── ICX-48_xhc_opt4_[0..23],node_16K_-1.txt
│   │   ...
│   │   ...
│   │   ├── ICX-48_xhc_opt4_[0..23],node_8K_2.txt
│   │   ├── ICX-48_xhc_vanilla_flat_16K.txt
│   │   ├── ICX-48_xhc_wait_flat_16K_0.txt
│   │   ...
│   │   ...
│   │   └── ICX-48_xhc_wait_flat_8K_2.txt
│   └── SKX-24
│       ├── SKX-24_xhc_clwb_flat_16K.txt
│       ├── SKX-24_xhc_dual_[0..11],node_16K.txt
│       ├── SKX-24_xhc_opt4_[0..11],node_16K_-1.txt
│       ...
│       ...
│       ├── SKX-24_xhc_opt4_[0..11],node_8K_2.txt
│       ├── SKX-24_xhc_vanilla_flat_16K.txt
│       ├── SKX-24_xhc_wait_flat_16K_0.txt
│       ...
│       ...
│       └── SKX-24_xhc_wait_flat_8K_2.txt
```

Once the required structure is prepared, simple execute `plot-remedies.sh`. The
script appropriately calls the python scripts. The plots are placed under a new
directory named `plots`.

The following plot files will be generated:

- `remedy_wait_<host>.svg` (for each host): Shows the performance of the
*delay-flag* and *delay-flag-string* remedies, compared to the baseline.

- `remedy_dual_<host>.svg` (for each host): Shows the performance of the
*dual buffer* remedy, in comparison to the baseline.

- `remedy_clwb_<host>.svg` (for each host): Shows the performance of the *clwb*
remedy versus the baseline algorithm.

- `remedy_opt_<host>.svg` (for each host): Shows the performance of the
*optimized* cache-coherency--aware variant, compared to the baseline.

- `remedy_opt speedup_all.svg`: Plots, for all hosts in the same graph,
the speedup that the *optimized* variant achieves over the baseline.

You might also want to choose a different *chunk size* or *wait mode*
configuration. The run script by default performs the benchmark of multiple
such combinations. See the respective sections for the *wait* and the *opt*
remedies in `run-remedies.sh`. To specify which configuration(s) should be used
for the plotting, you need to modify `plot-remedies.py`, lines 86-125 (shown
below); refer to the inline comments for instructions. An equivalent approach
may used in `plot-remedies-speedup-all`, for the speedup plot.

```python
# Configure which chunk size to plot
chunk = {
	# If plotting the 'wait' remedy
	'wait': {
		# Different chunk sizes depending on hostname
		'ICX-48': '2K',
		'SKX-24': '8K',
		'CSX-48': '16K',
	}.get(host, '8K'),
	# .get() does dictionary access by key (host), but if the
	# key is not present, the default value (8K) is used
	
	# For the 'opt' remedy (optimized version)
	'opt': {
		'ICX-48': '16K',
		'SKX-24': '4K',
		'CSX-48': '8K',
	}.get(host, '8K'),
	
	# For all other remedies, use the 16K chunk size. The chunk does not
	# actually matter for the them; 16K is the default XHC chunk size.
}.get(remedy, '16K')

# Configure the desired 'wait mode'
mod = {
	# In the 'wait' remedy, 0 is the scrict mode, 2 is
	# the more efficient one. This setting plots both.
	'wait': [0, 2],
	
	# In 'opt', mode -1 is with the remedy for the large messages
	# completely disabled, mode 2 is the efficient one (like above)
	'opt': [{
		'ICX-48': -1,
		'SKX-24': 2,
		'CSX-48': 2,
	}.get(host, 2)]
	# If not specified here, mode 2 is used
	
	# For other remedies, the wait mode is not applicable
}.get(remedy, [0])
```

Note that the following external python packages are required:

- `matplotlib`, recommended version `3.7.1`
- `seaborn`, recommended version `0.12.2`
- `pandas`, recommended version `1.5.3`
- `numpy`, recommended version `1.24.2`

---

Contact: George Katevenis (gkatev@ics.forth.gr), Manolis Ploumidis (ploumid@ics.forth.gr)  
Computer Architecture and VLSI Systems (CARV) Laboratory, ICS Forth
