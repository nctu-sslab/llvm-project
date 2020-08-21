#include <stdint.h>

struct ATTableTy {
  uintptr_t HstPtrBegin;
  uintptr_t HstPtrEnd;
  uintptr_t TgtPtrBegin;
};

extern "C" { // disable name mangling

// Translate function
// Binary search version
#define SM_TABLE_SIZE 20
__shared__ struct ATTableTy sm_table[SM_TABLE_SIZE];
__device__ static struct ATTableTy *tableptr;

__device__ static int flag = 0;

__device__ void *AddrTransTable(void* addr) {
    int size = tableptr[0].HstPtrBegin;
    uintptr_t ret = 0;
    uintptr_t addr_int = (intptr_t) addr;
    int head = 1, end = size;
    int mid;
    while (head <= end) {
        mid = (head + end) >> 1;
            // TODO don't check end to increase perf
        if (addr_int >= tableptr[mid].HstPtrBegin) {
            if (addr_int < tableptr[mid].HstPtrEnd) {
                ret = addr_int - tableptr[mid].HstPtrBegin + tableptr[mid].TgtPtrBegin;
                break;
            } else {
                end = mid -1;
            }
        } else {
            head = mid+1;
        }
    }
    if (ret == 0) {
        printf("fall back addrtrans: %p mid=%d end=%d, size:%d\n",addr, mid, end,size);
        return addr;
    }
    return (void*)ret;
}

// Only id 0 of the block does this
__device__ struct ATTableTy *StoreTableShared(struct ATTableTy* table) {
    int32_t id = threadIdx.x; // warning missing blockdim
    size_t table_size;
    if (id != 0) {
        goto end;
    }
    // FIXME
        tableptr = table;
        goto end;
    table_size = table[0].HstPtrBegin + 1;
    // if oversize
    if (table_size > SM_TABLE_SIZE) {
        tableptr = table;
        goto end;
    }
    // memcpy TODO cuda has memcpy
    tableptr = sm_table;
    for (int i = 0; i < table_size; i++) {
        tableptr[i] = table[i];
    }
end:
    // sync to wait at return
    __syncthreads();
    return tableptr;
}

__device__ void *AddrTransOffset(void *addr, intptr_t *offsets) {
    intptr_t mask = offsets[0];
    intptr_t shift = offsets[1];
    int index = ((intptr_t)addr & mask) >> shift;
    uintptr_t ret = ((uintptr_t)addr + offsets[index+2]);
    return (void*)ret;
}

#define DEFAULT_CM_ENTRY 16
__constant__ intptr_t ConstMem[DEFAULT_CM_ENTRY];
__device__ void *AddrTransOffset2(void *addr, intptr_t *offsets) {
    int id = threadIdx.x;
    intptr_t mask = ConstMem[0];
    intptr_t shift = ConstMem[1];
    int index = ((intptr_t)addr & mask) >> shift;
    uintptr_t ret = ((uintptr_t)addr + ConstMem[index+2]);
    return (void*)ret;
}
/*
__device__ void *AddrTransMask(void *addr) {

    int32_t id = threadIdx.x; // warning missing blockdim
    uintptr_t ret = ((uintptr_t)addr + 0x0000200000000000L);
    //printf("%d: %p->%p\n",id, addr,(void*)ret);
    return (void*)ret;
}
*/

/*
// TODO QQ God bless
__device__ void ConcurrentTrans(int count) {
    int32_t id = threadIdx.x; // warning missing blockdim
    //sm[idx] = AddrTrans(sm[idx]);
    for (start: end) {
    }
}*/
}
