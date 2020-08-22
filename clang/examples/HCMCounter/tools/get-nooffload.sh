#!/usr/bin/env bash

mkdir -p tmp
mkdir -p tmp/offload
mkdir -p tmp/dc
mkdir -p tmp/flat


modify=`git diff --compact-summary --name-status nooffload . | grep '^M'`

for line in $modify
do
    size=${#line}
    if [[ $size -lt 3 ]]; then
        continue
    fi
    # save old files
    filename=`basename $line`
    echo saving file $line
    git show nooffload:$line > tmp/$filename
done


