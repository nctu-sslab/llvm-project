#include <stdio.h>
#include "device.h"
#include "private.h"

void SegmentTy::dump() {
  DP2("%s\n", getString().c_str());
}

std::string SegmentTy::getString() {
  char buf[120];
  uintptr_t size = HstPtrEnd - HstPtrBegin;
  if (TgtPtrBegin) {
    sprintf(buf, "[" DPxMOD ":" DPxMOD "->" DPxMOD "<%" PRIuPTR ">]",
        DPxPTR(HstPtrBegin), DPxPTR(HstPtrEnd), DPxPTR(TgtPtrBegin), size);
  } else {
    sprintf(buf, "[" DPxMOD ":" DPxMOD "<%" PRIuPTR ">]",
        DPxPTR(HstPtrBegin), DPxPTR(HstPtrEnd), size);
  }
  return std::string(buf);
}
