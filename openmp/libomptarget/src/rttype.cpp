#include <list>
#include <map>
#include <string.h>

#include "omptarget.h"
#include "private.h"
#include "rttype.h"

// ID to RttJobs
// cache result
static std::map<uint64_t, RttJobsTy*> RttJobsCache;

// For dump
static bool first = true;

int RttTy::newRttObject(void **ptr_begin, void **ptr_base,
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

  RttInfoTy *info = NextRttInfo();
  RttTypes *type_array = info->RttTypeArray;
  int64_t *size_array = info->RttSizeArray;

#ifdef OMPTARGET_DEBUG
  dumpRttInfo(info);
#endif

  // check first size TODO
  if (*data_size != *size_array) {
    DP2("rtt Data size mismatch\n");
    return RTT_FAILED;
  }
  if (!(this->Jobs = getOrGenJobs(type_array))) {
      DP2("rtt getOrGenJobs failed\n");
      return RTT_FAILED;
  }

  // assign size
  fillData(size_array, *ptr_begin);
  // set CurJob to 2nd Job
  this->CurJob = ++this->Jobs->begin();
  this->BackReturning = false;
#ifdef OMPTARGET_DEBUG
  dumpJobs();
#endif
  if (first) {
    printf("[omp-dc] Deep Copy Expression\n");
    first = false;
  }
  return RTT_SUCCESS;
}

void RttTy::dumpRttInfo(RttInfoTy *info) {
  //info++;
  RttTypes *type_array = info->RttTypeArray;
  int64_t *size_array = info->RttSizeArray;
  char str[160] = "";
  char buf[80];

  if (RTT_IS_TID(*type_array)) {
    int32_t ID = (int32_t) (*type_array & ~RTT_TID);
    sprintf(buf, "TID #%d -", ID);
    strcat(str, buf);
  } else {
    fprintf(stderr, "Error invalid ID\n");
  }
  type_array++;

  do {
    sprintf(buf," %lx", *type_array);
    strcat(str, buf);
    if (RTT_IS_PTR(*type_array)) {
      sprintf(buf, ":%ld ", *size_array);
      strcat(str, buf);
      size_array++;
    } else if (!RTT_IS_BUILTIN(*type_array)) {
      printf("Not a valid type");
      break;
    }
    type_array++;
  } while (!(*type_array & RTT_BUILTIN));
  //  printf("HERE\n");
  DP2("%s\n", str);
}

void RttTy::dumpJobs() {
  if (this->isFrom) {
    DP2("IsFrom\n");
  }
  for (auto &it : *this->Jobs) {
    char str[160];
    switch(it.Kind) {
      case RttJob::UpdatePtrJob:
        sprintf(str, "\tUpdatePtrJob    base:%p, idx: %ld, size: %ld",
            it.base, it.idx, it.size);
        break;
      case RttJob::DataTransferJob:
        sprintf(str, "\tDataTransferJob base:%p, size: %ld",
            it.base, it.size);
        break;
      case RttJob::RootRegionJob:
        sprintf(str, "\tRootRegionJob   base:%p, size: %ld",
            it.base, it.size);
        break;
      case RttJob::EndJob:
        sprintf(str, "\tEndJob");
        break;
      case RttJob::SkipJob:
        sprintf(str, "\tSkipJob");
        break;
      default:
        break;
    }
    if (&it == &*this->CurJob) {
      strcat(str, "   <- CurJob");
    }
    DP2("%s\n", str);
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

RttJobsTy *RttTy::getOrGenJobs(RttTypes *T) {
  // check cache
  // 1st type is ID
  int32_t ID = (int32_t) (*T++ & ~RTT_TID);
  auto JobsItr = RttJobsCache.find(ID);
  // FIXME thread unsafe
  if (JobsItr != RttJobsCache.end()) {
    return JobsItr->second;
  }
  // 2nd type must be a pointer
  if (!RTT_IS_PTR(*T++)) {
    DP2("Failed\n");
    return nullptr;
  }
  RttJobsTy *Jobs = new RttJobsTy;
  // EndJob is on the top
  Jobs->emplace_back(RttJob::EndJob);
  int size_offset = 0;
  if (RTT_IS_PTR(*T)) {
    Jobs->emplace_back(RttJob::RootRegionJob, *T);
    Jobs->emplace_back(RttJob::UpdatePtrJob);
  }
  T++;
  while (RTT_IS_PTR(*T)) {
    Jobs->emplace_back(RttJob::DataTransferJob, *T);
    Jobs->emplace_back(RttJob::UpdatePtrJob);
    T++;
  }
  if (RTT_IS_BUILTIN(*T)) {
    Jobs->emplace_back(RttJob::DataTransferJob, *T);
  }
  RttJobsCache[ID] = Jobs;
  return Jobs;
}

enum RttReturn RttTy::fillData(int64_t *size_array, void *first_base) {
  for (auto &it : *this->Jobs) {
    switch(it.Kind) {
      case RttJob::UpdatePtrJob:
        it.size = DIVID_PTR_SIZE(*size_array) ;
        size_array++; // incre pointer in update ptr
        break;
      case RttJob::DataTransferJob:
        it.size = *size_array;
        break;
      case RttJob::RootRegionJob:
        it.size = *size_array;
        it.base = first_base;
        break;
      default:
        break;
    }
  }
  return RTT_SUCCESS;
}

enum RttReturn RttTy::computeRegion() {
  //dumpJobs();
  // Depends on CurJob
  // Deal with back
COMPUTE:
  while (BackReturning) {
    if (CurJob->Kind == RttJob::UpdatePtrJob) {
      if (CurJob->idx >= CurJob->size) {
        CurJob--;
      } else {
        break;
      }
    } else if (CurJob->Kind == RttJob::EndJob) {
      return RTT_END;
    } else {
      CurJob--;
    }
  }
  switch (CurJob->Kind) {
    case RttJob::UpdatePtrJob: {
      void **ptr = (void**) CurJob->base + CurJob->idx;
      CurJob->idx++;

      // Update base of next
      CurJob++;
      CurJob->base = ptr;
      BackReturning = false;;
      goto COMPUTE;
    }
    case RttJob::DataTransferJob: {
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
    case RttJob::RootRegionJob: {
      CurJob++;
      if (CurJob->Kind == RttJob::UpdatePtrJob) {
        CurJob->base = *ptr_begin;
        CurJob->idx = 0;
      }
      // Skip if isFrom
      if (this->isFrom && (CurJob->DataType & RTT_PTR)) {
        goto COMPUTE;
      }
      break;
    }
    case RttJob::SkipJob:
      CurJob++;
      return computeRegion();
      break;
    case RttJob::EndJob:
      DP2("This should not happend");
      return RTT_FAILED;
  }
  if (CurJob == this->Jobs->end()) {
    CurJob--;
    BackReturning = true;
    //DP2("Going back\n");
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
