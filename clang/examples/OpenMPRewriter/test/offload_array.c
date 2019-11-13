#include <stdio.h>
#include <stdlib.h>

#define N 10
int offload(int *a,int n) {
    long long sum = 0;
    for (int i = 0; i < N; i++) {
        a[i] = i;
    }

    for (int i =0; i < n; i++) {
        printf("%d ", a[i]);
    }
#pragma omp target teams distribute parallel for
    for (int i = 0; i < n; i++) {
        a[i] = i + a[i];
    }
    for (int i =0; i < n; i++) {
        printf("%d ", a[i]);
        sum += a[i];
    }

    printf("SUM: %lld\n", sum);
    return  0;
}

int main() {
    int a[N];
	offload(a,N);
	return 0;
}
