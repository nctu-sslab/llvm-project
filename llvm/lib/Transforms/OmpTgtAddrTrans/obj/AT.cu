#include <stdint.h>

struct ATTableTy {
  uintptr_t HstPtrBegin;
  uintptr_t HstPtrEnd;
  uintptr_t TgtPtrBegin;
};

extern "C" { // disable name mangling

// Translate function
// Binary search version
__device__ void *AddrTrans(void* addr, struct ATTableTy* table) {
    int size = table[0].HstPtrBegin;
    uintptr_t ret = 0;
    uintptr_t addr_int = (intptr_t) addr;
    int head = 1, end = size + 1;
    while (head < end) {
        int mid = (head + end) >> 1;
        if (addr_int >= table[mid].HstPtrBegin) {
            if (addr_int < table[mid].HstPtrEnd) {
                ret = addr_int - table[mid].HstPtrBegin + table[mid].TgtPtrBegin;
                break;
            }
            head = mid+1;
        } else {
            end = mid;
        }
    }
    // Don't fault when notfound
    if (ret == 0) {
        for (int i = 1; i <= size; i++) {
            if (addr_int >= table[i].HstPtrBegin && addr_int < table[i].HstPtrEnd) {
                ret = addr_int - table[i].HstPtrBegin + table[i].TgtPtrBegin;
                break;
            }
        }
    }
    if (ret == 0) {
        return addr;
    }
    return (void*)ret;
}

// Only id 0 do this
__device__ struct ATTableTy *StoreTableShared(struct ATTableTy* table, struct ATTableTy *sm,
        int8_t size /* max size of table in sm */, int32_t tid) {
    int table_size = table[0].HstPtrBegin + 1;
    // if oversize
    if (table_size > size) {
        return table;
    }
    if (tid != 0) {
        return sm;
    }
    // memcpy
    for (int i = 0; i < table_size; i++) {
        sm[i] = table[i];
    }
    return sm;
}
// sync to wait at return
}
