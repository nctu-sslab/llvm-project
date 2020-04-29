#include <cassert>

#include "perf.h"

#define DP(...) fprintf(stderr, __VA_ARGS__);

PerfRecordTy Perf;

// start timer
void PerfEventTy::start() {
  if (Lock) {
    return;
  }
  if (LockTarget) {
    LockTarget->Lock = true;
  }
  StartTime = high_resolution_clock::now();
  StartCnt++;
  Count++;

}

// end timer
void PerfEventTy::end() {
  if (Lock) {
    return;
  }
  assert(StartCnt > 0 && "PerfEvent end withnot started");
  auto EndTime = high_resolution_clock::now();
  time_span += duration_cast<duration<double>>(EndTime - StartTime);
  StartCnt--;
  if (LockTarget) {
    LockTarget->Lock = false;
  }
}

void PerfEventTy::dump() {
  assert(StartCnt == 0 && "PerfEvent didn't end");
  fprintf(stderr, "%-11s , %7d , %10lf", Name.c_str(), Count, time_span.count());
  if (StartCnt != 0) {
    fprintf(stderr, ", StartCnt: %d", StartCnt);
  }
  fprintf(stderr, "\n");
}

void BulkMemCount::get(int64_t device_id) {
  DeviceTy &Device = Devices[device_id];
  uintptr_t sum = 0;
  for (auto i : Device.SegmentList) {
    sum += i.second.HstPtrEnd - i.second.HstPtrBegin;
  }
  Sum = sum;
}

// PerfRecordTy
void PerfRecordTy::dump() {
  DP("PerfRecord dump\n");
  for (auto p : Perfs) {
    p->dump();
  }
}
