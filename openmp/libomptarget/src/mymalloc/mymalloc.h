#ifndef __MYMALLOC_H__
#define __MYMALLOC_H__

#include <inttypes.h>

extern void *(*mallocp)(size_t size);
extern void *(*reallocp)(void *ptr, size_t size);
extern void (*freep)(void *);
extern void *(*callocp)(size_t count, size_t size);
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

typedef struct heap {
  void *begin;
  void *end;
  void *next_free; // for next fit
  void *tbegin;
  size_t page_count;
  // maybe free count??
  // Navigator
  struct heap *next;
  struct heap *pre;
} heap_t;

// mmcontext for submit and retrieve
typedef struct mm_context {
  unsigned int id;
  int64_t device_id;
  struct heap *heap_list;
  struct RTLInfoTy *RTL;
  int32_t data_submit();
  int32_t data_retrieve();
  // TODO buddy system is needed
} mm_context_t;

extern mm_context_t *get_mm_context(void *p);

//extern intptr_t *get_offset_table(int *size);
extern void get_offset_table(int *size, intptr_t *ret);
extern intptr_t get_offset(mm_context_t* c);

#endif // __MYMALLOC_H__
