#!/usr/bin/env bash

rm -f AT-omp.cubin

clang AT.c -O2 -o AT.cubin -fopenmp -fopenmp-targets=nvptx64 -c --save-temps


# Remove mid files
#rm -f AT.bc AT.s AT.ll
rm -f *.bc *.i *.o *.s AT.cubin

mv AT-openmp-nvptx64.cubin AT.cubin
chmod a-w AT.cubin
