#include <sstream>

#include "device.h"
#include "private.h"

void RegionTy::dump() {
  DP2("%s\n", getString().c_str());
}

std::string RegionTy::getString() {
  std::ostringstream str;
  str << "[" << (void*)HstPtrBegin << ":" << (void*) HstPtrEnd;
  if (TgtPtrBegin) {
    str << "->" << (void*)TgtPtrBegin;
  }
  str << "]";
  return str.str();
}
