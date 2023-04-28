#!/bin/bash

function pwait() {
    while [ $(jobs -p | wc -l) -ge $1 ]; do
        sleep 1
    done
}

mkdir -p plots

for host_dir in data/*; do
	host=$(basename $host_dir)
	
	for remedy in "wait" "dual" "clwb" "opt"; do
		pwait $(getconf _NPROCESSORS_ONLN)
		./plot-remedies.py -s -d "plots" -h "$host" -r "$remedy" &
	done
done

pwait $(getconf _NPROCESSORS_ONLN)
./plot-remedies-speedup-all.py -s -d "plots" -r opt &

wait
