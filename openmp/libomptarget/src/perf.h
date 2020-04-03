#ifndef _OMPTARGET_PERF_H_
#include <string>
#include <omptarget.h>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "device.h"

# define _PERF

#ifdef _PERF
#define PERF_WRAP(...) if (Perf.Enabled) do { __VA_ARGS__ } while(0);
#else
#define PERF_WRAP(...)
#endif

using namespace std;
using namespace std::chrono;

struct PerfBaseTy {
  int func();
  virtual void dump() {};
};

struct PerfEventTy : public PerfBaseTy {
  string Name;
  float Time; // elapsed time in sec
  int Count;
  int StartCnt;
  high_resolution_clock::time_point StartTime;
  duration<double> time_span;

  PerfEventTy(): Time(0), Count(0), StartCnt(0) {};
  struct PerfBaseTy *setName(string str) {
    Name = str;
    return this;
  };
  void start();
  void end();
  void dump();
};

// Try to get bulk alloc size
template <typename T>struct PerfCountTy : public PerfBaseTy {
  T Sum;
  int Count;
  string Name;
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
  PerfCountTy(): Count(0), Sum(0) {}
  struct PerfBaseTy *setName(string str) {
    Name = str;
    return this;
  };
};

struct BulkMemCount : public PerfCountTy<long> {
  using PerfCountTy::PerfCountTy;
  void get(int64_t device_id);
};

struct PerfRecordTy {
  bool Enabled;

  PerfEventTy Runtime;
  PerfEventTy Kernel;
  PerfEventTy H2DTransfer;
  PerfEventTy UpdatePtr;
  PerfEventTy D2HTransfer;

  PerfEventTy RTDataBegin;
  PerfEventTy RTDataUpdate;
  PerfEventTy RTDataEnd;
  PerfEventTy RTTarget;

  PerfCountTy<long> Parallelism;

  BulkMemCount TargetMem;

  std::vector<PerfBaseTy*> Perfs;
  PerfRecordTy() : Enabled(false) {
#define SET_PERF_NAME(Name) Perfs.push_back(Name.setName(#Name));
    SET_PERF_NAME(Runtime); // NOTE this contains following 4
    SET_PERF_NAME(Kernel);
    SET_PERF_NAME(H2DTransfer); //  NOTE this contains UpdatePtr
    SET_PERF_NAME(UpdatePtr);
    SET_PERF_NAME(D2HTransfer);

    SET_PERF_NAME(RTTarget);
    SET_PERF_NAME(RTDataBegin);
    SET_PERF_NAME(RTDataUpdate);
    SET_PERF_NAME(RTDataEnd);

    SET_PERF_NAME(Parallelism);
    SET_PERF_NAME(TargetMem);
#undef SET_PERF_NAME
  };
  void dump();
  void init() {Enabled = true;}
};

extern PerfRecordTy Perf;

#endif
