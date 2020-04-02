#include <stdint.h>

struct ATTableTy {
  uintptr_t HstPtrBegin;
  uintptr_t HstPtrEnd;
  uintptr_t TgtPtrBegin;
};

#pragma omp declare target
// Binary search version
void *AddrTrans(void* addr, struct ATTableTy* table) {
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
    return (void*)ret;
}
#pragma omp end declare target
