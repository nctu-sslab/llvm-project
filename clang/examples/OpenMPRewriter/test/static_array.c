#include <stdio.h>

int ompf1(int *a) {
    // OMP offloading
    return 1;
}

int main() {
    int a[100];
    long long b[1000];
    ompf1(a);
    ompf1(&a[0]);
    return 0;
}



