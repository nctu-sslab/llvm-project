#include <cassert>

#include "perf.h"

#define DP(...) printf(__VA_ARGS__);


PerfRecordTy Perf;

void PerfEventTy::start() {
  StartTime = high_resolution_clock::now();
  StartCnt++;
  Count++;
  // start timer
}

void PerfEventTy::end() {
  assert(StartCnt > 0 && "PerfEvent end withnot started");
  auto EndTime = high_resolution_clock::now();
  time_span += duration_cast<duration<double>>(EndTime - StartTime);
  // end timer
  StartCnt--;
}

void PerfEventTy::dump() {
  assert(StartCnt == 0 && "PerfEvent didn't end");
  DP("%-10s |Count: %7d |TimeSpan: %10lfs\n", Name.c_str(), Count, time_span.count());
}

void BulkMemCount::get(int64_t device_id) {
  DeviceTy &Device = Devices[device_id];
  uintptr_t sum = 0;
  for (auto i : Device.SegmentList) {
    sum += i.second.HstPtrEnd - i.second.HstPtrBegin;
  }
  Count = sum;
}

void BulkMemCount::dump() {
  char units[5] = {'B', 'K', 'M', 'G', 'X'};
  int units_idx = 0;
  double n = Count;

  while (n > 1000) {
    n /= 1000;
    units_idx++;
  }
  if (units_idx > 4) {
    units_idx = 5;
  }
  printf("BulkMemCount: %lf%c\n", n, units[units_idx]);
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
}
