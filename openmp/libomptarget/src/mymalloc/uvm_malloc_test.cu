#include <stdio.h>

void *mymalloc_uvm(size_t size) {
    void *ret;
    cuMemAllocManaged ((CUdeviceptr*)&ret, size, CU_MEM_ATTACH_GLOBAL);

    return ret;
}

void myfree_uvm(void *ptr) {
    cuMemFree((CUdeviceptr)ptr);
}

void *myrealloc_uvm(void *ptr, size_t size) {

    //cuMemAllocManaged ((CUdeviceptr*)&ret, size, );
    return NULL;
}
int main() {
    void *p = mymalloc_uvm(10);
    printf("%p\n", p);
    return 0;
}
