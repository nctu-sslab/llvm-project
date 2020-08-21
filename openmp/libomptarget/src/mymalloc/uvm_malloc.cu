#include <stdio.h>
#include "mmprivate.h"

void *mymalloc_uvm(size_t size) {
    void *ret = NULL;
    CUresult err = cuMemAllocManaged (
            (CUdeviceptr*)&ret, size, CU_MEM_ATTACH_GLOBAL);
    if (err != CUDA_SUCCESS) {
      puts("Error cuMemAllocManaged\n");
      //CUDA_ERR_STRING(err);
      const char *errStr;
      cuGetErrorString(err, &errStr);
      printf("%s",errStr);
      return NULL;
    }
    return ret;
}

void myfree_uvm(void *ptr) {
    cuMemFree((CUdeviceptr)ptr);
}

void *myrealloc_uvm(void *ptr, size_t size) {

    //cuMemAllocManaged ((CUdeviceptr*)&ret, size, );
    return NULL;
}
