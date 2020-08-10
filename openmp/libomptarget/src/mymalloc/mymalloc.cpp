#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "device.h"
#include "mmprivate.h"
#include "mymalloc.h"
#include "private.h"
#include "rtl.h"
#include "perf.h"

// reference https://moss.cs.iit.edu/cs351/slides/slides-malloc.pdf
// TODO implement free

#define ALIGNMENT 16
#define ALIGN_SHIFT 4

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) >> ALIGN_SHIFT << ALIGN_SHIFT)
#define PAGE_ALIGN(size) (((size) + (PAGE_SIZE - 1)) >> pg_shift << pg_shift)

#define HEADER_SIZE (ALIGN(sizeof(blk_hdr_t)))

#define IS_USED(b) (b->size & 1)
#define IS_FREE(b) (!IS_USED(b))
#define PAGE_SIZE pg_size

typedef struct blk_hdr {
  size_t size;
} __attribute__((aligned(ALIGNMENT))) blk_hdr_t;

typedef struct heap {
  void *begin;
  void *end;
  void *next_free; // for next fit
  size_t page_count;
  // maybe free count??
  // Navigator
  struct heap *next;
  struct heap *pre;
} heap_t;

static size_t pg_size;
static char pg_shift = 0;
static uintptr_t pg_mask = 0;
static heap_t default_heap;
heap_t *heap_list = &default_heap;
heap_t *curr_heap = &default_heap;
mm_context_t *curr_context;
uint16_t context_count = 0;
static char inited = 0;

static mm_context_t contexts[10];

static void *myrealloc(void *ptr, size_t size);
static void *mymalloc(size_t size);
static void myfree(void *ptr);
static void *mycalloc(size_t count, size_t size);

static void *dlrealloc(void *ptr, size_t size);
static void *dlmalloc(size_t size);
static void *dlcalloc(size_t count, size_t size);
static void dlfree(void *ptr);

static void context_create(int64_t did);

static unsigned char use_default = 1;

static int is_myspace(void *ptr) {
  uintptr_t index = (uintptr_t)ptr & _omp_header_mask;
  return !(index ^ _omp_check_mask);
}
void mymalloc_begin(int64_t device_id) {
  context_create(device_id);
  use_default = 0;
}
void mymalloc_end() {
  use_default = 1;
}

// wrappers
#define WRAP
#ifdef WRAP
void *malloc(size_t size) {
  if (use_default) {
    return dlmalloc(size);
  }
  return mymalloc(size);
}
void free(void *ptr) {
  if (!inited) {
    exit(77);
  }
  if (is_myspace(ptr)) {
    myfree(ptr);
  } else {
    dlfree(ptr);
  }
}
void *realloc(void *ptr, size_t size) {
  if (!inited) {
    exit(77);
  }
  if (is_myspace(ptr)) {
    if (use_default) {
      blk_hdr_t *blk = (blk_hdr_t *)((char *)ptr - HEADER_SIZE);
      void *ret = dlmalloc(size);
      int copy_size = size > blk->size ? blk->size : size;
      memcpy(ret, ptr, copy_size);
      myfree(ptr);
      return ret;
    } else {
      return myrealloc(ptr, size);
    }
  } else {
    if (use_default) {
      return dlrealloc(ptr, size);
    } else {
      return myrealloc(ptr, size);
    }
  }
}
#endif

/*
void *calloc(size_t count, size_t size) {
    if (use_default) {
        return dlcalloc(count, size);
    }
    size_t total = count * size;
    void *ret;
    ret = malloc(total);
    //exit(__LINE__);
    memset(ret ,0, total);
    return ret;
}*/

static unsigned char buffer[8192];
void *(*mallocp)(size_t size) = NULL;
void *(*reallocp)(void *ptr, size_t size) = NULL;
void (*freep)(void *) = NULL;

// warning dlsym calls memory allocation
__attribute__((constructor)) static void init() {
  // static void init() {
  // puts("mmminit");
  pg_size = getpagesize();
  pg_mask = ~((uintptr_t)PAGE_SIZE - 1);
  int shift = 3;
  int mask = 1;
  while (++shift < 32) {
    if (pg_size & (mask << shift)) {
      pg_shift = shift;
      break;
    }
  }
  if (pg_shift == 0) {
    fprintf(stderr, "Failed to get page shift\n");
    exit(87);
  }
  // exit(12);
  // load myself symbol to prevent they are overrided by libc
  if (!mallocp) {
    char *error;
    *(void **)(&mallocp) = dlsym(RTLD_NEXT, "malloc");
    error = dlerror();
    if (!mallocp) {
      exit(87);
      // NOTE dlerror may also call malloc
      // error = dlerror();
    }
  }
  if (!reallocp) {
    char *error;
    *(void **)(&reallocp) = dlsym(RTLD_NEXT, "realloc");
    // error = dlerror();
    if (!reallocp) {
      exit(87);
    }
  }
  if (!freep) {
    *(void **)(&freep) = dlsym(RTLD_NEXT, "free");
    if (!freep) {
      exit(87);
    }
  }
  // init _omp_d2hmask _omp_h2dmask ...etc
  find_mem_hole(getpid()); // init transform mask
  inited = 1;
}

#define DEFAULT_BLOCK_SZ 1

// pre and next are uninited
// Allocate new heap and init first block
static heap_t *new_heap(int page_count) {
  if (page_count == 0) {
    page_count = DEFAULT_BLOCK_SZ;
  }
  heap_t *ret = (heap_t *)mallocp(sizeof(heap_t));
  ret->page_count = page_count;
  size_t size = page_count * PAGE_SIZE;

  // ret->begin = aligned_alloc(PAGE_SIZE, DEFAULT_BLOCK_SZ);
  // ret->begin = aligned_alloc(PAGE_SIZE, DEFAULT_BLOCK_SZ);

  // size_t actual_size = PAGE_ALIGN(size + sizeof(struct header));
  // FIXME
  void *dptr =
      curr_context->RTL->data_alloc(curr_context->device_id, size, NULL);

  void *d2h_ptr = (void *)(_omp_d2hmask & (uintptr_t)dptr);

  // align to page size
  int mmap_ret = mmap_region_register(d2h_ptr, size);
  if (mmap_ret) {
    puts("mmap_region_register failed");
    exit(90);
  }

  ret->begin = d2h_ptr;
  ret->next_free = ret->begin;
  ret->end = (void *)((char *)ret->begin + size);
  // init first blk
  blk_hdr_t *first_blk = (blk_hdr_t *)ret->begin;
  first_blk->size = (uintptr_t)ret->end - (uintptr_t)ret->begin;
  // size_t size = (uintptr_t)ret->end - (uintptr_t)ret->begin;
  if (page_count != DEFAULT_BLOCK_SZ) {
    DP2("Alloced larger heap: %p-%p 0x%zx\n",
        ret->begin, ret->end, first_blk->size);
  }
  return ret;
}

static blk_hdr_t *find_next_fit(size_t size) {
  blk_hdr_t *curr_blk = (blk_hdr_t *)curr_heap->next_free;
  void *end = curr_heap->end;
  while ((void *)curr_blk < end) {
    if (IS_FREE(curr_blk) && curr_blk->size >= size) {
      // try split here
      // NOTE check rest space for header
      if (size + HEADER_SIZE < curr_blk->size) {
        // set rest block
        blk_hdr_t *rest = (blk_hdr_t *)((char *)curr_blk + size);
        rest->size = curr_blk->size - size;
        // printf("split: rest: %zu %p\n", rest->size, rest);
        curr_heap->next_free = rest;
      } else {
        curr_heap->next_free = curr_heap->begin;
        exit(99);
        size = curr_blk->size;
      }
      curr_blk->size = size;
      return curr_blk;
    }
    curr_blk = (blk_hdr_t *)((char *)curr_blk + (curr_blk->size & ~1L));
  }
  // recursive
  // NOTICE if the size is too big allocate bigger heap
  heap_t *heap_new;
  if (size >= curr_heap->page_count * PAGE_SIZE) {
    //DP2("allocate too big 0x%zx: new_heap size from 0x%zx -> 0x%zx\n", size, curr_heap->page_count  PAGE_ALIGN(size * 8), size * 8);
    // FIXME how to balance this value
    // multi-level threshold to determine the multiplier
    heap_new = new_heap(PAGE_ALIGN(size*2) >> pg_shift);
  } else {
    // NOTE heap size is incremental
    heap_new = new_heap(curr_heap->page_count);
  }
  if (!heap_new) {
    // Error oom
    return NULL;
  }
  heap_new->next = curr_heap->next;
  heap_new->pre = curr_heap;
  curr_heap->next = heap_new;
  heap_new->next->pre = heap_new;
  curr_heap = heap_new;
  return find_next_fit(size);
  // TODO when find block in another heap
}

void context_create(int64_t device_id) {
  if (!inited) {
    init();
  }
  uint16_t cid = context_count++;
  DeviceTy *Device = &Devices[device_id];

  curr_context = &contexts[cid];

  // bind device with context
  // curr_context->device = Device;
  curr_context->device_id = device_id;
  curr_context->RTL = Devices[device_id].RTL;
  curr_context->id = cid;

  heap_t *heap_new = new_heap(0);
  // circlar heap list
  heap_new->pre = heap_new;
  heap_new->next = heap_new;
  curr_context->heap_list = heap_new;
  curr_heap = curr_context->heap_list;
  DP2("Create new mm context #%d\n", curr_context->id);
}

static void *mymalloc(size_t size) {
  use_default |= 2;
  size_t blk_size = ALIGN(size + HEADER_SIZE);
  if (blk_size == 0) {
    return NULL;
  }
  blk_hdr_t *found = find_next_fit(blk_size);
  if (found) {
    // printf("find_next_fit: %p 0x%zx\n", found, found->size);
  } else {
    puts("malloc failed");
    exit(87);
  }
  found->size |= 1;
  use_default &= ~(unsigned char)2;
  return (char *)found + HEADER_SIZE;
}
// FIXME
static void myfree(void *ptr) {
  if (!is_myspace(ptr)) {
    freep(ptr);
  }
  // TODO implement free

  // munmap(aligned, head->size);
  // cuMemFree((CUdeviceptr)((uintptr_t)ptr | _omp_h2dmask));
}

static void *myrealloc(void *ptr, size_t size) {
  void *ret;
  if (!is_myspace(ptr)) {
    void *tmp_ptr = dlrealloc(ptr, size);
    ret = mymalloc(size);
    memcpy(ret, tmp_ptr, size);
    freep(tmp_ptr);
    return ret;
  }
  exit(88);
  /*
  ret = mymalloc(size);
  size_t old_size = head->size;
  memcpy(ret, ptr, old_size);
  myfree(ptr);
  */
  return ret;
}

void setDefault() {
  use_default = 1;
}

static void *dlmalloc(size_t size) {
  return mallocp(size);
}

static void *dlrealloc(void *ptr, size_t size) {
  return reallocp(ptr, size);
}

static void dlfree(void *ptr) {
  freep(ptr);
  return;
}
/*
static unsigned char buffer[8192];
static void *dlcalloc(size_t count, size_t size) {
    // get address of libc malloc
    if (!mallocp) {
        struct header *head = (struct header *)(void*)buffer;
        head->is_default = 2;
    }
    void *ret;
    size_t total = count * size;
    size_t new_size = total + sizeof(struct header);
    ret = mallocp(new_size);
    struct header *head = (struct header*)ret;
    head->is_default = 1;
    // init 0
    memset(ret ,0, total);
    return ret;
}*/

int32_t mm_context_t::data_submit() {
  static unsigned int count = 0;
  heap_t *first = heap_list;
  heap_t *curr = first;
  do  {
    count++;
    PERF_WRAP(Perf.H2DTransfer.start();)
    void *hbegin = curr->begin;
    void *tbegin = (void*)_MYMALLOC_H2D(curr->begin);
    // FIXME next_free is not always the end
    int64_t size = (uintptr_t)curr->next_free - (uintptr_t)hbegin;
    if (size==0) {
      curr = curr->next;
      PERF_WRAP(Perf.H2DTransfer.end();)
      continue;
    }
    //size = curr->page_count*PAGE_SIZE;
    int32_t ret = RTL->data_submit(device_id,
        tbegin, hbegin, size);
        //tbegin, hbegin, curr->page_count*PAGE_SIZE);
    if (ret) {
      DP2("mm_context_t data_submit failed\n");
      return OFFLOAD_FAIL;
    }
    PERF_WRAP(Perf.H2DTransfer.end();)
    curr = curr->next;
  } while(curr != first);
  DP2("data_submit accum count: %d\n", count);
  return OFFLOAD_SUCCESS;
  //return 0;
}
int32_t mm_context_t::data_retrieve() {
  static unsigned int count = 0;
  heap_t *first = heap_list;
  heap_t *curr = first;
  do  {
    count++;
    PERF_WRAP(Perf.D2HTransfer.start();)
    void *hbegin = curr->begin;
    void *tbegin = (void*)_MYMALLOC_H2D(curr->begin);
    size_t size = (uintptr_t)curr->next_free - (uintptr_t)hbegin;
    if (size==0) {
      curr = curr->next;
      continue;
    }
    int32_t ret = RTL->data_retrieve(device_id,
        hbegin, tbegin, size);
        //hbegin, tbegin, curr->page_count*PAGE_SIZE);
    if (ret) {
      return OFFLOAD_FAIL;
    }
    PERF_WRAP(Perf.D2HTransfer.end();)
    curr = curr->next;
  } while(curr != first);
  DP2("data_retrieve accum count: %d\n", count);
  return OFFLOAD_SUCCESS;
}

mm_context_t *get_mm_context(void *p) {
  static unsigned int count = 0;
  for (int i = 0; i < context_count; i++) {
    heap_t *first = contexts[i].heap_list;
    heap_t *curr = first;
    do  {
      count++;
      if (p >= curr->begin && p < curr->end) {
        DP2("get_mm_context accum search count: %d\n", count);
        return &contexts[i];
      }
      curr = curr->next;
    } while(curr != first);
  }
  return NULL;
}
