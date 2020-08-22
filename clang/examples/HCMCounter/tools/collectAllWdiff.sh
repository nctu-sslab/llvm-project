#!/usr/bin/env bash

wdiffs=`ls *.wdiff`
output=AllWdiff.c
rm -f $output
for file in $wdiffs
do
    wdiff.py $file >> $output
done
clang++ -cc1 -fsyntax-only -I. -I $LLVM_BUILD_PATH/include -load $LLVM_BUILD_PATH/lib/HCMCounter.so ~/a.cpp  -plugin hcm -plugin-arg-hcm  4  -plugin-arg-hcm $output > out.json
