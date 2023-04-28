#!/bin/bash

set -e

# Where to place the results of the experiment
RESULT_DIR="$HOME/protocol"

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
# sequentially. Altering this is very likely to interfere with the prepared
# scenarios. Only do so with immense caution.
export PRTE_MCA_hwloc_default_binding_policy="core"
export PRTE_MCA_rmaps_default_mapping_policy="slot"

# --------------------

(
	mkdir -p "$RESULT_DIR/local"
	
	declare -A desc=(
		[1]="SC_L_1: Init = root write. Action = local read"
		[2]="SC_L_2: Init = root write, local read. Action = 2nd local read"
		[3]="SC_L_3: Init = root write, 2x local read. Action = 3rd local read"
	)
	
	for sc in 1 2 3; do
		make -C "$CACHE_ENG_DIR" clean
		make -C "$CACHE_ENG_DIR" SC="local" SC_FN="sc_l_$sc"
		
		OUT="$RESULT_DIR/local/${HOSTNAME}_sc_l_${sc}.txt"
		
		printf "%s\n\n" "${desc[$sc]}" > "$OUT"
		
		# Fake MCA: OMPI bug #11402
		mpirun --mca fake fake "$CACHE_ENG_DIR/cache_eng" -sn 1000 \
			-f "$CACHE_ENG_DIR/events/ev_local.input" \
			| "$CACHE_ENG_DIR/tools/pmon_ev.sh" 100 \
			|  tee -a "$OUT"
	done
)

# --------------------

(
	mkdir -p "$RESULT_DIR/remote"
	
	declare -A desc=(
		[1]="SC_R_1: Init = all shared, root write. Action = remote read"
		[2]="SC_R_2: Init = all shared, root write, local read. Action = remote read"
		[3]="SC_R_3: Init = all shared, root write, remote read. Action = local read"
	)
	
	for sc in 1 2 3; do
		make -C "$CACHE_ENG_DIR" clean
		make -C "$CACHE_ENG_DIR" SC="remote" SC_FN="sc_r_$sc"
		
		OUT="$RESULT_DIR/remote/${HOSTNAME}_sc_r_${sc}.txt"
		
		printf "%s\n\n" "${desc[$sc]}" > "$OUT"
		
		# Fake MCA: OMPI bug #11402
		mpirun --mca fake fake "$CACHE_ENG_DIR/cache_eng" -sn 1000 \
			-f "$CACHE_ENG_DIR/events/ev_remote.input" \
			| "$CACHE_ENG_DIR/tools/pmon_ev.sh" 100 \
			|  tee -a "$OUT"
	done
)
