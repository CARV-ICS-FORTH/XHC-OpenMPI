#!/bin/bash

THRESH=${1:-0}

grep -E 'ALL CHA|ALL M2M|ALL IMC|CORE [0-9]+' \
	| sed -r 's/CORE ([0-9]+) (.*) ([0-9]+)/CORE \1 _ _ \2 \3/' \
	| awk -v THRESH="$THRESH" '{if($6>=THRESH) print $1, $2, $4, $5, $6}' \
	| sort -s -k 1,1 -k 2,2n -k 3,3 \
	| column -t \
	| awk '{x=$1 $2 $3; if(lx && x != lx) print ""; print $0; lx=x}'
