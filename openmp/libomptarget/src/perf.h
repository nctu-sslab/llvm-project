#ifndef _OMPTARGET_PERF_H_
#define _OMPTARGET_PERF_H_

#include <string>
#include <omptarget.h>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "device.h"

# define _PERF

#ifdef _PERF
#define PERF_WRAP(...) __VA_ARGS__
#else
#define PERF_WRAP(...) __VA_ARGS__
#endif

using namespace std;
using namespace std::chrono;

struct PerfEventTy {
  string Name;
  float Time; // elapsed time in sec
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
template <typename T>struct PerfCountTy {
  T Sum;
  int Count;
  string Name;
  PerfCountTy() {};
  void add(T count) {
    Sum += count;
    Count++;
  }
  void dump() {
    char units[5] = {'B', 'K', 'M', 'G', 'X'};
    int units_idx = 0;
    double n = Sum;

    while (n > 1000) {
      n /= 1000;
      units_idx++;
    }
    if (units_idx > 4) {
      units_idx = 5;
    }
    cerr << std::left << setw(11) << Name << " , " << std::right <<
      setw(7) << Count << " , " << setw(10) <<  n << units[units_idx] << "\n";
  }
  PerfCountTy(string name): Name(name), Count(0), Sum(0) {}
};

struct BulkMemCount : public PerfCountTy<long> {
  using PerfCountTy::PerfCountTy;
  void get(int64_t device_id);
};


struct PerfRecordTy {

  PerfEventTy Kernel;
  PerfEventTy H2DTransfer;
  PerfEventTy D2HTransfer;
  PerfEventTy Runtime;
  PerfEventTy UpdatePtr;

  PerfCountTy<long> Parallelism;

  BulkMemCount TargetMem;

  PerfRecordTy(): Kernel("Kernel"), H2DTransfer("H2D"), D2HTransfer("D2H"),
    Runtime("Runtime"), UpdatePtr("UpdatePtr"), Parallelism("Parallelism"), TargetMem("TargetMem") {};
  void dump();
};

extern PerfRecordTy Perf;

#endif
