#!/bin/bash

stdbuf -i0 -o0 -e0 sed -r 's/(^\[[0-9]+,[0-9]+\])<std(out|err)>:([^ ])/\1<std\2>: \3/'\ | \
stdbuf -i0 -o0 -e0 sed -r 's/.*?,([0-9]+)]<stdout>:(.*)/\1:\2/' | \
stdbuf -i0 -o0 -e0 sed -r 's/.*?,([0-9]+)]<stderr>:(.*)/(e) \1:\2/'

# 1st sed is compatibility with OMPI v4, which does not have a space after the colon
