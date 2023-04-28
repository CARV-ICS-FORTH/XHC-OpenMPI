#!/bin/bash

set -e

# How many times to perform the micro-benchmark; the average of these
# runs will eventually be calculated and used by plotting scripts.
REPS=30

# How many ranks can this node fit? Most likely, this is the
# number of available cores on the system (cores, NOT threads)
MAX_RANKS=$(mpirun echo | wc -l)

# Where to place the results of the benchmark(s) (txt files)
RESULT_DIR="$HOME/bcast_effect"

# Where the benchmark is located. Remember to grab the co-located
# modified version of the OSU micro-benchmark suite.
OSU_DIR="$HOME/osu-xhc/mpi/collective"

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

# Mapping and binding directives to OpenMPI. This will place ranks on cores
# sequentially, resulting in a single socket being fully populated before a
# following one starts being occupied.
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

mkdir -p "$RESULT_DIR"

# --------------------

# For each possible rank count, run the benchmark $REPS times.
# All reps' results are stored in the same file.
for((r = 2; r <= "$MAX_RANKS"; r++)); do
	for((i = 0; i < "$REPS"; i++)); do
		LAT_ALL= mpirun -n "$r" "$OSU_DIR/osu_bcast_dv"
	done | tee "${RESULT_DIR}/${HOSTNAME}_xhc_${OMPI_MCA_coll_xhc_hierarchy}_${OMPI_MCA_coll_xhc_chunk_size}_$(printf "%02d" $r)x.txt"
	
	echo "----"
done
