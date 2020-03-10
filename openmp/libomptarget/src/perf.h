#ifndef _OMPTARGET_PERF_H_
#define _OMPTARGET_PERF_H_

#include <string>
#include <omptarget.h>
#include <chrono>

#include "device.h"

using namespace std;
using namespace std::chrono;

//#include "device.h"

// TODO how to set unit
struct PerfEventTy {
  string Name;
  float Time; // elapsed time
  int Count;
  int StartCnt;
  high_resolution_clock::time_point StartTime;
  duration<double> time_span;

  PerfEventTy(string name): Name(name), Time(0), Count(0), StartCnt(0) {};
  void start();
  void end();
  void dump();
};

// Try to get bulk alloc size
/*struct PerfCountTy {

  PerfCountTy() {};
  virtual void get(int64_t device_id);
  virtual void dump();
};*/

struct BulkMemCount {// : public PerfCountTy {

  uintptr_t Count;
  void get(int64_t device_id);
  void dump();
};


struct PerfRecordTy {

  PerfEventTy Kernel;
  PerfEventTy H2DTransfer;
  PerfEventTy D2HTransfer;
  PerfEventTy Runtime;
  PerfEventTy UpdatePtr;

  struct BulkMemCount TargetMem;

  PerfRecordTy(): Kernel("Kernel"), H2DTransfer("H2D"), D2HTransfer("D2H"),
    Runtime("Runtime"), UpdatePtr("UpdatePtr") {};
  void dump();
};

extern PerfRecordTy Perf;

#endif
