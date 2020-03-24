#include <cassert>

#include "perf.h"

#define DP(...) fprintf(stderr, __VA_ARGS__);

PerfRecordTy Perf;

// start timer
void PerfEventTy::start() {
  StartTime = high_resolution_clock::now();
  StartCnt++;
  Count++;
}

// end timer
void PerfEventTy::end() {
  assert(StartCnt > 0 && "PerfEvent end withnot started");
  auto EndTime = high_resolution_clock::now();
  time_span += duration_cast<duration<double>>(EndTime - StartTime);
  StartCnt--;
}

void PerfEventTy::dump() {
  assert(StartCnt == 0 && "PerfEvent didn't end");
  DP("%-11s , %7d , %10lf\n", Name.c_str(), Count, time_span.count());
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
  Runtime.dump();
  Kernel.dump();
  H2DTransfer.dump();
  D2HTransfer.dump();
  UpdatePtr.dump();
  TargetMem.dump();
  Parallelism.dump();
}
