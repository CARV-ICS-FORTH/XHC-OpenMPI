#!/bin/bash

set -e

if [[ ! -f coll_xhc_bcast.c ]]; then
	echo "Error: XHC sources not found"
	exit 1
fi

if ! git --version; then
	echo "Error: git not installed"
	exit 2
fi

# git init -b vanilla

git init
git add --all
git reset -- make-branches.sh patches
git commit -am "vanilla"

git branch -m vanilla

printf "\n=============\n"

for p in patches/*; do
	name=$(sed -r 's|.*/remedy_(\w+).diff|\1|' <<< "$p")
	
	echo
	
	git checkout -b "$name" vanilla
	git apply --whitespace=nowarn "$p"
	git commit -am "$name"
done

git checkout vanilla

printf "\n=============\n\n"
git --no-pager log --graph --pretty=oneline --abbrev-commit --branches
printf "\n=============\n"
