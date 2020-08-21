#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mmprivate.h"
#include "mymalloc.h"

typedef struct mmap_region {
  // All page aligned
  void *begin;
  void *end;
  int ref_count;
  struct mmap_region *next;
} mmap_region;

static unsigned int page_size;
static uintptr_t page_mask;

static struct mmap_region *list;

static int insert_new_region(void *begin, size_t size, mmap_region *pos) {
  // mmap
  uintptr_t align_begin = (uintptr_t)begin & page_mask;
  uintptr_t align_end = ((uintptr_t)begin + size + page_size - 1) & page_mask;
  size_t region_size = align_end - align_begin;

  void *ret = mmap((void *)align_begin, region_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (ret == (void *)-1) {
    // failed
    // perror("");
    return -1;
  }

  mmap_region *new_region = (mmap_region *)mallocp(sizeof(mmap_region));
  new_region->begin = (void *)align_begin;
  new_region->end = (void *)align_end;
  new_region->ref_count = 1;

  // insert
  if (pos) {
    new_region->next = pos->next;
    pos->next = new_region;
  } else {
    new_region->next = list;
    list = new_region;
  }
  return 0;
}

static int extend_region(void *begin, size_t size, mmap_region *target) {
  uintptr_t new_align_end =
      ((uintptr_t)begin + size + 3 * page_size - 1) & page_mask;
  uintptr_t align_begin = (uintptr_t)target->begin;
  size_t new_region_size = new_align_end - align_begin;
  size_t region_size = (uintptr_t)target->end - align_begin;

  void *ret = mremap((void *)align_begin, region_size, new_region_size, 0);
  if (ret == (void *)-1) {
    perror("");
    return -1;
  }
  target->end = (void *)new_align_end;
  return 0;
}

__attribute__((constructor)) static int init() {
  page_size = getpagesize();
  page_mask = ~((uintptr_t)page_size - 1);
  return 0;
}

int mmap_region_register(void *begin, size_t size) {
  // static size_t dummy = init();
  // TODO merge neighbor region
  int found = 0;
  mmap_region *pos = NULL;
  int ret;
  void *end = (void *)((uintptr_t)begin + size);
  for (struct mmap_region *curr = list; curr != NULL; curr = curr->next) {
    if (curr->end < begin) {
      pos = curr;
      continue;
    }
    if (curr->begin >= end) {
      break;
    }
    if (curr->begin <= begin && curr->end >= end) {
      found = 1;
      curr->ref_count++;
      break;
    }
    if (curr->begin <= begin && curr->end < end) {
      // mremap forward
      curr->ref_count++;
      found = 1;
      return extend_region(begin, size, curr);
    }
    exit(13);
  }
  if (found) {
    return 0;
  }
  return ret = insert_new_region(begin, size, pos);
}

/*
void mmap_region_dump() {
    for (mmap_region *curr = list; curr != NULL; curr = curr->next) {
    }
}*/
