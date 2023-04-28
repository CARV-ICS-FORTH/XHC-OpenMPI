#!/bin/bash

set -e

# How many times to perform the micro-benchmark; the average of these
# runs will eventually be calculated and used by plotting scripts.
REPS=30

# How many ranks can this node fit? Most likely, this is the
# number of available cores on the system (cores, NOT threads)
MAX_RANKS=$(mpirun echo | wc -l)

# Where to place the results of the benchmark(s) (txt files)
RESULT_DIR="$HOME/bcast_remedies"

# Where the benchmark is located. Remember to grab the co-located
# modified version of the OSU micro-benchmark suite
OSU_DIR="$HOME/osu-xhc/mpi/collective"

# Where XHC's folder inside the OpenMPI installation is located
XHC_DIR="$HOME/ompi-xhc-src/ompi/mca/coll/xhc"

# --------------------

# You probably don't have to adjust this if you've set MAX_RANKS and your node
# has the expected format. After the calculations, the value should be in the
# format of [0..x],node. 'x' is the highest core ID in the same socket as core
# 0. In a node with 2 sockets, and 24 cores on each one, x is 23.
DUAL_TOPO="[0..$((MAX_RANKS/2-1))],node"

# --------------------

# Only utilized in the filenames of the benchmark results
if [[ $SLURM_JOB_PARTITION ]]; then
	HOSTNAME=$SLURM_JOB_PARTITION
else
	HOSTNAME=$(hostname -s)
fi

# Mapping and binding directives to OpenMPI
export PRTE_MCA_hwloc_default_binding_policy="core"
export PRTE_MCA_rmaps_default_mapping_policy="slot"

# Communication backend settings
export OMPI_MCA_pml="^cm"
export OMPI_MCA_btl="sm,self"
export OMPI_MCA_smsc="xpmem"

export OMPI_MCA_coll="basic,libnbc,xhc,xb"

# A special topology is utilized for XB, to make sure that ranks
# enter the collective as soon and as synchronized as possbile.
export OMPI_MCA_coll_xb_hierarchy="$DUAL_TOPO"

# Flat topology, not looking into tree ones at all in this work
export OMPI_MCA_coll_xhc_hierarchy="flat"

# This is actually the default value of XHC for the chunk size. It has no
# effect in this experiment, and is only defined here for quality control.
export OMPI_MCA_coll_xhc_chunk_size="16K"

# Use the shared-memory-segment transportation mechanism (instead of the
# single-copy one) for messages up to 512 bytes (inclusive). Determined
# as the optimal one through experimental tuning. On another system,
# another value might be a better overall choice.
export OMPI_MCA_coll_xhc_cico_max=512

# Priorities of components; higher than all
# other components, and XB higher than XHC
export OMPI_MCA_coll_xhc_priority=90
export OMPI_MCA_coll_xb_priority=95

# --------------------

# Sanity check for XHC directory and git branches

(
	cd "$XHC_DIR"
	
	if [[ $(git rev-parse --is-inside-work-tree 2>/dev/null) != "true" ]]; then
		echo "Error: '$XHC_DIR' is not a git repository"
		exit 1
	fi
	
	for branch in vanilla wait dual clwb opt; do
		if ! git rev-parse --verify "$branch" >/dev/null 2>&1; then
			echo "Error: branch '$branch' not found in '$XHC_DIR'"
			exit 2
		fi
	done
)

if [[ ! $? -eq 0 ]]; then
	exit 1
fi

# --------------------

mkdir -p "$RESULT_DIR"

# --------------------

# This function checks-out a branch and recompiles XHC
function apply_branch() {
	echo "Applying branch $1"
	
	(cd "$XHC_DIR" && git checkout "$1")
	make -s install -C "$XHC_DIR"
}

# The commands benchmark MPI_Bcast performance with different remedies
# applied. The results are saved in the output directory with appropriate
# file names, indicating the applied remedy for each result.

# Vanilla; no remedies applied, the baseline implementation
apply_branch "vanilla"
(
	OUT="${RESULT_DIR}/${HOSTNAME}_xhc_vanilla_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}.txt"
	
	for ((i = 0; i < $REPS; i++)); do
		mpirun "$OSU_DIR/osu_bcast_dv"
	done | tee "$OUT"
)

# The 'delay-flag' remedy
apply_branch "wait"

(
	# Try out multiple chunk sizes
	for chunk in 2K 4K 8K 16K; do
		export OMPI_MCA_coll_xhc_chunk_size=$chunk
		
		# Try out multiple modes; mode 0 is the 'strict' one
		for mode in 0 2; do
			export OMPI_MCA_coll_xhc_sc_wait_mode=$mode
			
			OUT="${RESULT_DIR}/${HOSTNAME}_xhc_wait_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}_${OMPI_MCA_coll_xhc_sc_wait_mode}.txt"
			
			for ((i = 0; i < $REPS; i++)); do
				mpirun "$OSU_DIR/osu_bcast_dv"
			done | tee "$OUT"
		done
	done
)

# The 'dual buffer' remedy
apply_branch "dual"

(
	# This remedy relies on a custom hierarchy (same one as the one used for XB)
	export OMPI_MCA_coll_xhc_hierarchy="$DUAL_TOPO"
	
	OUT="${RESULT_DIR}/${HOSTNAME}_xhc_dual_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}.txt"
	
	for ((i = 0; i < $REPS; i++)); do
		mpirun "$OSU_DIR/osu_bcast_dv"
	done | tee "$OUT"
)

# The 'clwb' remedy
apply_branch "clwb"

(
	OUT="${RESULT_DIR}/${HOSTNAME}_xhc_clwb_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}.txt"
	
	for ((i = 0; i < $REPS; i++)); do
		mpirun "$OSU_DIR/osu_bcast_dv"
	done | tee "$OUT"
)

# The optimized version combining multiple remedies
apply_branch "opt"

(
	# Relies on the the custom hierarchy, like the 'dual buffer' remedy
	export OMPI_MCA_coll_xhc_hierarchy="$DUAL_TOPO"
	
	# Try out multiple chunk sizes
	for chunk in 2K 4K 8K 16K; do
		export OMPI_MCA_coll_xhc_chunk_size=$chunk
		
		# If mode is -1, the wait-based remedy intended
		# for large message is always disabled
		for mode in -1 2; do
			export OMPI_MCA_coll_xhc_sc_wait_mode=$mode
			
			OUT="${RESULT_DIR}/${HOSTNAME}_xhc_opt4_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}_${OMPI_MCA_coll_xhc_sc_wait_mode}.txt"
			
			for ((i = 0; i < $REPS; i++)); do
				mpirun "$OSU_DIR/osu_bcast_dv"
			done | tee "$OUT"
		done
	done
)

# Revert back to the vanilla branch
apply_branch "vanilla"
