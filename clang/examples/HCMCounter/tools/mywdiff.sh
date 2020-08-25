#!/usr/bin/env bash

# remove commnet first
if [ "$#" -ne 2 ]; then
        echo "Illegal number of parameters"
        exit 1
fi

tmp1=".tmp1"
tmp2=".tmp2"
echo $1
echo $2
# Remove comments and no insert line mark
gcc -fpreprocessed -dD -P -E $1 > $tmp1
gcc -fpreprocessed -dD -P -E $2 > $tmp2

basename=`basename $1`

wdiff --no-common $tmp1 $tmp2 > $basename.wdiff
#rm -f $tmp1 $tmp2
