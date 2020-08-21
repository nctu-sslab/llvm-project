#ifndef _MMPRIVATE_H_
#define _MMPRIVATE_H_

// mmap_mgr.h
extern int mmap_region_register(void *begin, size_t size);
extern void mmap_region_dump();

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

#define PERMS_READ 1U
#define PERMS_WRITE 2U
#define PERMS_EXEC 4U
#define PERMS_SHARED 8U
#define PERMS_PRIVATE 16U

typedef struct address_range address_range;
struct address_range {
  struct address_range *next;
  void *start;
  void *end;
  size_t length;
  unsigned long offset;
  dev_t device;
  ino_t inode;
  unsigned char perms;
  char name[128];
};

address_range *mem_stats(pid_t);
intptr_t find_mem_hole(pid_t); // malloc-free
void free_mem_stats(address_range *);
void print_mem_stats(address_range *);
void print_partial_mem_stats(address_range *);
void dump_partial_vmmap();

extern void *myrealloc_uvm(void *ptr, size_t size);
extern void *mymalloc_uvm(size_t size);
extern void myfree_uvm(void *ptr);

#endif //_MMPRIVATE_H_
