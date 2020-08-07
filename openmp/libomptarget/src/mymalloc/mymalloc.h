#ifndef __MYMALLOC_H__
#define __MYMALLOC_H__

#include <inttypes.h>

extern void *(*mallocp)(size_t size);
extern void *(*reallocp)(void *ptr, size_t size);
extern void (*freep)(void *);
extern void pragma_omp_enter();
extern void pragma_omp_exit();

#define _MYMALLOC_ISMYSPACE(ptr)                                               \
  (((uintptr_t)ptr & _omp_header_mask) == _omp_check_mask)
#define _MYMALLOC_D2H(ptr) (((uintptr_t)ptr & _omp_d2hmask))
#define _MYMALLOC_H2D(ptr) (((uintptr_t)ptr | _omp_h2dmask))

// mem_layout.h
extern uintptr_t _omp_h2dmask; // | index
extern uintptr_t _omp_d2hmask; // | index
extern uintptr_t _omp_check_mask;
extern uintptr_t _omp_header_mask;

/*
typedef struct mallocer {
  int8_t use_default_alloc;
} mallocer_t;
extern struct mallocer mymallocer;
*/

extern void mymalloc_begin(int64_t deviceID);
extern void mymalloc_end();

// mmcontext for submit and retrieve

#endif // __MYMALLOC_H__
