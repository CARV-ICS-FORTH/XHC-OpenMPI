#!/bin/bash

function pwait() {
    while [ $(jobs -p | wc -l) -ge $1 ]; do
        sleep 1
    done
}

mkdir -p plots

for host_dir in data/*; do
	host=$(basename $host_dir)
	
	pwait $(getconf _NPROCESSORS_ONLN)
	
	./plot-effect.py -s -d "plots" -h "$host" &
	./plot-effect-locality.py -s -d "plots" -h "$host" &
done

pwait $(getconf _NPROCESSORS_ONLN)
./plot-effect-slowdown-all.py -s -d "plots" &

wait
