#!/bin/bash

set -e

# How many times to perform the experiment. The
# average of these runs will be output as the result
REPS=30

# Where to place the results of the experiment
RESULT_DIR="$HOME/protocol_state_transitions"

# The directory of the cache_eng software
CACHE_ENG_DIR="$HOME/XHC-OpenMPI/cache_eng"

# --------------------

# Only utilized in the filenames of the benchmark results
if [[ $SLURM_JOB_PARTITION ]]; then
	HOSTNAME=$SLURM_JOB_PARTITION
else
	HOSTNAME=$(hostname -s)
fi

# Mapping and binding directives to OpenMPI. This will place ranks on cores
# sequentially, resulting in a single socket being fully populated before a
# following one starts being occupied.
export PRTE_MCA_hwloc_default_binding_policy="core"
export PRTE_MCA_rmaps_default_mapping_policy="slot"

# --------------------

mkdir -p "$RESULT_DIR"

# --------------------

for sc in l r lr rl; do
	make -C "$CACHE_ENG_DIR" NO_MSR=1 clean
	make -C "$CACHE_ENG_DIR" NO_MSR=1 SC="state_transitions" SC_FN="sc_s_$sc"
	
	for((i = 0; i < $REPS; i++)); do
		mpirun --output tag "$CACHE_ENG_DIR/cache_eng" --no-perfctr -n 1 --ns \
			| "$CACHE_ENG_DIR/tools/mpi_tag.sh"
	done | "$CACHE_ENG_DIR/tools/msm_avg.awk" \
		| tee "$RESULT_DIR/${HOSTNAME}_sc_s_${sc}.txt"
done
