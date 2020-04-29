#include <list>
#include <map>

#include "omptarget.h"
#include "private.h"
#include "rttype.h"

// ID to RttJobs
// cache result
std::map<uint64_t, RttJobsTy*> RttJobsCache;

int RttTy::init(void **ptr_begin, void **ptr_base,
  int64_t *data_size, int64_t *data_type) {
  isFirst = true;

  if (!validMaptype(*data_type)) {
    return RTT_FAILED;
  }
  // set stack address
  this->ptr_begin = ptr_begin;
  this->ptr_base = ptr_base;
  this->data_size = data_size;
  this->data_type = data_type;

  this->origin_type = *data_type & ~OMP_TGT_MAPTYPE_TARGET_PARAM;

  // Incre rtts here
  RttTypes *rtt = *rtts++;
  this->rtt = rtt;
  int32_t ID = (int32_t) (*rtt & ~RTT_TID);
  printf("ID: %d ", ID);
  // Dump Type
  dumpType();

  // check first size TODO
  if (*data_size != *this->rtt_sizes) {
    printf("Data size mismatch\n");
    return RTT_FAILED;
  }

  auto JobsItr = RttJobsCache.find(ID);
  if (JobsItr != RttJobsCache.end()) {
    this->Jobs = JobsItr->second;
  } else {
    printf("Gen Jobs\n");
    auto newJobs = genJobs(this->rtt + 1);
    if (!newJobs) {
      printf("GenJobs failed\n");
      return RTT_FAILED;
    }
    this->Jobs = newJobs;
    RttJobsCache[ID] = newJobs;
  }
  // assign size
  fillData(this->rtt_sizes, *ptr_begin);
  // set CurJob
  this->CurJob = ++this->Jobs->begin();
  this->BackReturning = false;
  dumpJobs();
  return RTT_SUCCESS;
}

void RttTy::dumpType() {
  // Do it before parsing
  int64_t *tmp_sizes = this->rtt_sizes;
  RttTypes *tmp_rtt = this->rtt;
  printf("Dump rtt: ");
  do {
    printf(" %lx", *tmp_rtt);
    if (RTT_IS_PTR(*tmp_rtt)) {
      printf(":%ld ", *tmp_sizes++);
    }
    tmp_rtt++;
  } while (!(*tmp_rtt & RTT_BUILTIN));
  printf("\n");
}

void RttTy::dumpJobs() {
  for (auto &it : *this->Jobs) {
    switch(it.Kind) {
      case RttJob::UpdatePtrJob:
        printf("\tUpdatePtrJob base:%p, idx: %ld, size: %ld",
            it.base, it.idx, it.size);
        break;
      case RttJob::DataTransferJob:
        printf("\tDataTransferJob base:%p, size: %ld",
            it.base, it.size);
        break;
      case RttJob::RootRegionJob:
        printf("\tRootRegionJob base:%p, size: %ld",
            it.base, it.size);
        break;
      case RttJob::EndJob:
        printf("\tEndJob");
        break;
      case RttJob::SkipJob:
        printf("\tSkipJob");
        break;
      default:
        break;
    }
    if (&it == &*this->CurJob) {
      printf("   <- CurJob");
    }
    printf("\n");
  }
}
// with Type
struct RttRegionInfoTy {
  void *base;
  int64_t idx;
  int64_t size;
  bool pointeeTodo;
  RttTypes type; // what type this region stores

  // for PTR
  RttRegionInfoTy(void *b, int i, int64_t s, RttTypes t):
    base(b), idx(i), size(s), pointeeTodo(false), type(t) {};
  // for Builtin
  RttRegionInfoTy(int64_t s, RttTypes t):
    size(s), type(t) {};
};

RttJobsTy *RttTy::genJobs(RttTypes *T/* without ID */) {
  // consume first pointer
  // First type must be a pointer
  if (!RTT_IS_PTR(*T++)) {
    printf("Failed\n");
    return nullptr;
  }
  RttJobsTy *Jobs = new RttJobsTy;
  // EndJob is on the top
  Jobs->emplace_back(RttJob::EndJob);
  int size_offset = 0;
  if (RTT_IS_PTR(*T++)) {
    Jobs->emplace_back(RttJob::RootRegionJob);
    Jobs->emplace_back(RttJob::UpdatePtrJob);
  }
  while (RTT_IS_PTR(*T)) {
    Jobs->emplace_back(RttJob::DataTransferJob);
    Jobs->emplace_back(RttJob::UpdatePtrJob);
    T++;
  }
  if (RTT_IS_BUILTIN(*T)) {
    Jobs->emplace_back(RttJob::DataTransferJob);
  }
  return Jobs;
}

enum RttReturn RttTy::fillData(int64_t *&rtt_sizes, void *first_base) {
  for (auto &it : *this->Jobs) {
    switch(it.Kind) {
      case RttJob::UpdatePtrJob:
        it.size = DIVID_PTR_SIZE(*rtt_sizes) ;
        rtt_sizes++; // incre pointer in update ptr
        break;
      case RttJob::DataTransferJob:
        it.size = *rtt_sizes;
        break;
      case RttJob::RootRegionJob:
        printf("Filled first job base\n");
        it.size = *rtt_sizes;
        it.base = first_base;
        break;
      default:
        break;
    }
  }
  rtt_sizes++;
  return RTT_SUCCESS;
}

enum RttReturn RttTy::computeRegion() {
  //dumpJobs();
  // Depends on CurJob
  // Deal with back
COMPUTE:
  while (BackReturning) {
    if (CurJob->Kind == RttJob::EndJob) {
      return RTT_END;
    } else if (CurJob->Kind == RttJob::UpdatePtrJob) {
      if (CurJob->idx >= CurJob->size) {
        CurJob--;
      } else {
        break;
      }
    } else {
      CurJob--;
    }
  }
  switch (CurJob->Kind) {
    case RttJob::UpdatePtrJob: {
      //printf("UpdatePtrJob\n");
      void **ptr = (void**) CurJob->base + CurJob->idx;
      //printf("idx: %ld, size: %ld\n", CurJob->idx, CurJob->size);
      //*ptr_base = CurJob->base;
      //*ptr_begin = ptr;
      //*data_size = sizeof(void*);
      //*data_type = origin_type;
      // Incre idx here
      CurJob->idx++;

      // Update base of next
      CurJob++;
      CurJob->base = ptr;
      BackReturning = false;;
      goto COMPUTE;
    }
    case RttJob::DataTransferJob: {
      //printf("DataTransferJob\n");
      void **base = (void**)CurJob->base;
      void *ptr = *base;
      *ptr_base = base;
      *ptr_begin = ptr;
      *data_size = CurJob->size;
      *data_type = origin_type | OMP_TGT_MAPTYPE_PTR_AND_OBJ;
      CurJob++;
      if (CurJob->Kind == RttJob::UpdatePtrJob) {
        CurJob->base = ptr;
        CurJob->idx = 0;
      }
      break;
    }
    case RttJob::RootRegionJob:
      //printf("RootRegionJob\n");
      CurJob++;
      if (CurJob->Kind == RttJob::UpdatePtrJob) {
        CurJob->base = *ptr_begin;
        CurJob->idx = 0;
      }
      break;
    case RttJob::SkipJob:
      //printf("SkipJob\n");
      CurJob++;
      return computeRegion();
      break;
    case RttJob::EndJob:
      printf("This should not happend");
      return RTT_FAILED;
  }
  if (CurJob == this->Jobs->end()) {
    CurJob--;
    BackReturning = true;
    //printf("Going back\n");
  }
  return RTT_SUCCESS;
}

int RttTy::validMaptype(int Type) {
  if (!(Type & OMP_TGT_MAPTYPE_NESTED)) {
    return false;
  }
  // Not supported type:
  int InvalidTypes[] = {
    OMP_TGT_MAPTYPE_RETURN_PARAM, OMP_TGT_MAPTYPE_PRIVATE,
    OMP_TGT_MAPTYPE_LITERAL, OMP_TGT_MAPTYPE_IMPLICIT};

  for (int i = 0; i < sizeof(InvalidTypes)/sizeof(int); i++) {
    if (InvalidTypes[i] & Type) {
      DP2("Invalid type for deep copy expression");
      return RTT_FAILED;
    }
  }
  return RTT_SUCCESS;
}
  /*
  OMP_TGT_MAPTYPE_NONE            = 0x000,
  OMP_TGT_MAPTYPE_TO              = 0x001,
  OMP_TGT_MAPTYPE_FROM            = 0x002,
  OMP_TGT_MAPTYPE_ALWAYS          = 0x004,
  OMP_TGT_MAPTYPE_DELETE          = 0x008,
  OMP_TGT_MAPTYPE_PTR_AND_OBJ     = 0x010,
  OMP_TGT_MAPTYPE_TARGET_PARAM    = 0x020,
  OMP_TGT_MAPTYPE_RETURN_PARAM    = 0x040,
  OMP_TGT_MAPTYPE_PRIVATE         = 0x080,
  OMP_TGT_MAPTYPE_LITERAL         = 0x100,
  OMP_TGT_MAPTYPE_IMPLICIT        = 0x200,
  OMP_TGT_MAPTYPE_MEMBER_OF       = 0xffff000000000000
    */
