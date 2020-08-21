#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "mmprivate.h"

ssize_t readline(char *buf, size_t sz, int fd, off_t *offset);

// credit:
// https://stackoverflow.com/questions/36523584/how-to-see-memory-layout-of-my-program-in-c-during-run-time/36524010#36524010

void free_mem_stats(address_range *list) {
  while (list) {
    address_range *curr = list;
    list = list->next;
    free(curr);
  }
}

void print_mem_stats(address_range *list) {
  address_range *curr;
  for (curr = list; curr != NULL; curr = curr->next) {
    printf("%p-%p: %s\n", curr->start,
           (void *)((char *)curr->start + curr->length), curr->name);
  }
}

void dump_partial_vmmap() {
  address_range *list = mem_stats(getpid());
  print_partial_mem_stats(list);
  free_mem_stats(list);
}
void print_partial_mem_stats(address_range *list) {
  address_range *curr;
  puts("####### VM Map ######");
  for (curr = list; curr != NULL; curr = curr->next) {
    if ((uintptr_t)curr->start >= 0x00007f0000000000) {
      break;
    }
    printf("\t%p-%p: %s\n", curr->start,
           (void *)((char *)curr->start + curr->length), curr->name);
  }
  puts("#####################");
}

address_range *mem_stats(pid_t pid) {
  address_range *end = NULL;
  address_range *list = NULL;

  char *line = NULL;
  size_t size = 0;
  FILE *maps;

  if (pid > 0) {
    char namebuf[128];
    int namelen;

    namelen = snprintf(namebuf, sizeof namebuf, "/proc/%ld/maps", (long)pid);
    if (namelen < 12) {
      errno = EINVAL;
      return NULL;
    }

    maps = fopen(namebuf, "r");
  } else
    maps = fopen("/proc/self/maps", "r");

  if (!maps) {
    return NULL;
  }

  while (getline(&line, &size, maps) > 0) {
    address_range *curr;
    char perms[8];
    unsigned int devmajor, devminor;
    unsigned long addr_start, addr_end, offset, inode;

    curr = (address_range *)malloc(sizeof(address_range));
    if (!curr) {
      fclose(maps);
      free(line);
      free_mem_stats(list);
      errno = ENOMEM;
      return NULL;
    }

    if (list) {
      end->next = curr;
      curr->next = NULL;
      end = end->next;
    } else {
      curr->next = NULL;
      list = curr;
      end = list;
    }

    int matched_count =
        sscanf(line, "%lx-%lx %7s %lx %x:%x %lu %s", &addr_start, &addr_end,
               perms, &offset, &devmajor, &devminor, &inode, curr->name);
    if (matched_count < 7) {
      printf("Only matched %d item\n", matched_count);
      fclose(maps);
      free(line);
      free_mem_stats(list);
      errno = EIO;
      return NULL;
    }

    curr->start = (void *)addr_start;
    curr->end = (void *)addr_end;
    curr->length = addr_end - addr_start;
    curr->offset = offset;
    curr->device = makedev(devmajor, devminor);
    curr->inode = (ino_t)inode;

    curr->perms = 0U;
    if (strchr(perms, 'r'))
      curr->perms |= PERMS_READ;
    if (strchr(perms, 'w'))
      curr->perms |= PERMS_WRITE;
    if (strchr(perms, 'x'))
      curr->perms |= PERMS_EXEC;
    if (strchr(perms, 's'))
      curr->perms |= PERMS_SHARED;
    if (strchr(perms, 'p'))
      curr->perms |= PERMS_PRIVATE;
  }

  free(line);

  if (!feof(maps) || ferror(maps)) {
    fclose(maps);
    free_mem_stats(list);
    errno = EIO;
    return NULL;
  }
  if (fclose(maps)) {
    free_mem_stats(list);
    errno = EIO;
    return NULL;
  }

  errno = 0;
  return list;
}

static int trailing_zeros(uintptr_t addr);
static uintptr_t index_mask = 0x00007f0000000000;
// linux user space upper bound
static uintptr_t user_ub = 0x00007fffffffffff;

uintptr_t _omp_header_mask = 0xffffff0000000000;
uintptr_t _omp_check_mask = 0x00007f0000000000;
// Assume device addr start       0x00007f0000000000
uintptr_t _omp_h2dmask = 0x00007f0000000000; // | host_ptr
uintptr_t _omp_d2hmask = 0x000000ffffffffff; // & device ptr

static uintptr_t GB8 = 0x0000000200000000;

static int trailing_zeros(uintptr_t indexs) {
  int count = 0;
  for (int i = 0; i < 64; i++) {
    if ((indexs >> i) & 1) {
      break;
    }
    count++;
  }
  return count;
}

int if_overlaped(int mapfd, intptr_t begin, intptr_t end) {
#define LINE_BUFSZ 128
  size_t size = 0;
  char line[LINE_BUFSZ];
  off_t offset = 0;
  ssize_t len = 0;
  size_t i = 0;
  int overlaped = 0;
  int count = 0;
  while (readline(line, LINE_BUFSZ, mapfd, &offset) > 0) {
    address_range curr;
    char perms[8];
    unsigned int devmajor, devminor;
    uintptr_t addr_start, addr_end, offset, inode;

    int matched_count =
        sscanf(line, "%lx-%lx %7s %lx %x:%x %lu %s", &addr_start, &addr_end,
               perms, &offset, &devmajor, &devminor, &inode, curr.name);
    if (matched_count < 7) {
      return -1;
    }
    // check overlap here

    count++;
    if ((uintptr_t)addr_end <= begin) {
      continue;
    }
    if ((uintptr_t)addr_end >= end) {
      break;
    }
    overlaped = 1;
    break;
  }
  if (overlaped == 1) {
    return 1;
  }
  return 0;
}

// implement without malloc
intptr_t find_mem_hole(pid_t pid) {
  char namebuf[128];
  int mapfd;

  if (pid > 0) {
    int namelen = 0;
    namelen = snprintf(namebuf, sizeof namebuf, "/proc/%ld/maps", (long)pid);
    if (namelen < 12) {
      puts("snprintf");
      return -1;
    }
  } else {
    return -1;
  }
  mapfd = open(namebuf, O_RDONLY, 0);
  if (mapfd == -1) {
    puts("open");
    return -1;
  }
  intptr_t indexs = user_ub & _omp_header_mask;
  intptr_t size = ~_omp_header_mask + 1;
  int trailing0 = trailing_zeros(indexs);
  int index_ub = indexs >> trailing0;

  // Search from low address, high address like to be heap
  for (intptr_t i = index_ub - 0x20; i >= 0; i--) {
    // for (intptr_t i = 1; i < index_ub; i++) {
    lseek(mapfd, 0, SEEK_SET);
    intptr_t begin = i << trailing0;
    intptr_t end = begin + size;
    int ret = if_overlaped(mapfd, begin, end);
    if (ret == 0) {
      //_omp_h2dmask |= begin; //
      _omp_d2hmask |= begin;
      _omp_check_mask = begin;
      close(mapfd);
      return begin;
    } else if (ret < 0) {
      puts("if_overlaped");
      return -1; // failed
    }
  }
  puts("No result");
  return -1;
}

// credit
// https://stackoverflow.com/questions/33106505/read-file-line-by-line-in-c-mostly-with-syscalls
ssize_t readline(char *buf, size_t sz, int fd, off_t *offset) {
  ssize_t nchr = 0;
  ssize_t idx = 0;
  char *p = NULL;
  /* position fd & read line */
  if ((nchr = lseek(fd, *offset, SEEK_SET)) != -1)
    nchr = read(fd, buf, sz);

  if (nchr == -1) { /* read error   */
    return nchr;
  }
  /* end of file - no chars read
  (not an error, but return -1 )*/
  if (nchr == 0)
    return -1;
  p = buf; /* check each chacr */
  while (idx < nchr && *p != '\n')
    p++, idx++;
  *p = 0;
  if (idx == nchr) { /* newline not found  */
    *offset += nchr;
    return nchr < (ssize_t)sz ? nchr : 0;
  }
  *offset += idx + 1;
  return idx;
}
