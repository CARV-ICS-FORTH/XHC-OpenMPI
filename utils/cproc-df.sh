#!/bin/bash

if [[ ! "$1" ]]; then
	echo "Usage: cproc-df.sh <dir OR file> [dir OR file...]"
	exit 1
fi

DIR="$(cd "$(dirname "$0")" && pwd)"

[[ ! "$PROC" ]] &&
	PROC="proc-df.awk"

function proc_file() {
	name=$(perl -n -e'/(?:coll_)?(.+)\.txt/ && print $1' <<< "$(basename $1)")
	"$DIR/$PROC" -v PREFIX="$name" "$1"
}

for arg in "$@"; do
	if [[ -d "$arg" ]]; then
		readarray -d '' files < <(find "$arg/" -type f -name '*.txt' -print0)
		
		IFS=$'\n'
		uniq_dirs=$(rev <<< "${files[*]}" | cut -d '/' -f 2- | rev | uniq | wc -l)
		
		IFS=''
		for f in "${files[@]}"; do
			prefix="$f"
			
			if [[ $uniq_dirs == 1 ]]; then
				prefix=$(perl -n -e'/(?:coll_)?(.+)\.txt/ && print $1' <<< "$(basename $f)")
			fi
			
			"$DIR/$PROC" -v PREFIX="$prefix" "$f"
		done
		
		unset IFS
	else
		prefix=$(perl -n -e'/(?:coll_)?(.+)\.txt/ && print $1' <<< "$(basename $f)")
		"$DIR/$PROC" -v PREFIX="$prefix" "$arg"
	fi
done
