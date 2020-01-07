//===--------- device.cpp - Target independent OpenMP target RTL ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Functionality for managing devices that are handled by RTL plugins.
//
//===----------------------------------------------------------------------===//

#include "device.h"
#include "private.h"
#include "rtl.h"

#include <cassert>
#include <climits>
#include <string>

#include <unistd.h>

/// Map between Device ID (i.e. openmp device id) and its DeviceTy.
DevicesTy Devices;

int DeviceTy::associatePtr(void *HstPtrBegin, void *TgtPtrBegin, int64_t Size) {
  DataMapMtx.lock();

  if (IsBulkEnabled) {
     assert(0 && __func__ " shall not used");
  }
  // Check if entry exists
  for (auto &HT : HostDataToTargetMap) {
    if ((uintptr_t)HstPtrBegin == HT.HstPtrBegin) {
      // Mapping already exists
      bool isValid = HT.HstPtrBegin == (uintptr_t) HstPtrBegin &&
                     HT.HstPtrEnd == (uintptr_t) HstPtrBegin + Size &&
                     HT.TgtPtrBegin == (uintptr_t) TgtPtrBegin;
      DataMapMtx.unlock();
      if (isValid) {
        DP("Attempt to re-associate the same device ptr+offset with the same "
            "host ptr, nothing to do\n");
        return OFFLOAD_SUCCESS;
      } else {
        DP("Not allowed to re-associate a different device ptr+offset with the "
            "same host ptr\n");
        return OFFLOAD_FAIL;
      }
    }
  }

  // Mapping does not exist, allocate it
  HostDataToTargetTy newEntry;

  // Set up missing fields
  newEntry.HstPtrBase = (uintptr_t) HstPtrBegin;
  newEntry.HstPtrBegin = (uintptr_t) HstPtrBegin;
  newEntry.HstPtrEnd = (uintptr_t) HstPtrBegin + Size;
  newEntry.TgtPtrBegin = (uintptr_t) TgtPtrBegin;
  // refCount must be infinite
  newEntry.RefCount = INF_REF_CNT;

  DP("Creating new map entry: HstBase=" DPxMOD ", HstBegin=" DPxMOD ", HstEnd="
      DPxMOD ", TgtBegin=" DPxMOD "\n", DPxPTR(newEntry.HstPtrBase),
      DPxPTR(newEntry.HstPtrBegin), DPxPTR(newEntry.HstPtrEnd),
      DPxPTR(newEntry.TgtPtrBegin));
  HostDataToTargetMap.push_front(newEntry);

  DataMapMtx.unlock();

  return OFFLOAD_SUCCESS;
}

int DeviceTy::disassociatePtr(void *HstPtrBegin) {
  DataMapMtx.lock();

  if (IsBulkEnabled) {
     assert(0 && __func__ " shall not used");
  }
  // Check if entry exists
  for (HostDataToTargetListTy::iterator ii = HostDataToTargetMap.begin();
      ii != HostDataToTargetMap.end(); ++ii) {
    if ((uintptr_t)HstPtrBegin == ii->HstPtrBegin) {
      // Mapping exists
      if (CONSIDERED_INF(ii->RefCount)) {
        DP("Association found, removing it\n");
        HostDataToTargetMap.erase(ii);
        DataMapMtx.unlock();
        return OFFLOAD_SUCCESS;
      } else {
        DP("Trying to disassociate a pointer which was not mapped via "
            "omp_target_associate_ptr\n");
        break;
      }
    }
  }

  // Mapping not found
  DataMapMtx.unlock();
  DP("Association not found\n");
  return OFFLOAD_FAIL;
}

// Get ref count of map entry containing HstPtrBegin
long DeviceTy::getMapEntryRefCnt(void *HstPtrBegin) {
  uintptr_t hp = (uintptr_t)HstPtrBegin;
  long RefCnt = -1;

  DataMapMtx.lock();
  for (auto &HT : HostDataToTargetMap) {
    if (hp >= HT.HstPtrBegin && hp < HT.HstPtrEnd) {
      DP("DeviceTy::getMapEntry: requested entry found\n");
      RefCnt = HT.RefCount;
      break;
    }
  }
  DataMapMtx.unlock();

  if (RefCnt < 0) {
    DP("DeviceTy::getMapEntry: requested entry not found\n");
  }

  return RefCnt;
}

LookupResult DeviceTy::lookupMapping(void *HstPtrBegin, int64_t Size) {
  uintptr_t hp = (uintptr_t)HstPtrBegin;
  LookupResult lr;

  DP("Looking up mapping(HstPtrBegin=" DPxMOD ", Size=%ld)...\n", DPxPTR(hp),
      Size);
  for (lr.Entry = HostDataToTargetMap.begin();
      lr.Entry != HostDataToTargetMap.end(); ++lr.Entry) {
    auto &HT = *lr.Entry;
    // Is it contained?
    lr.Flags.IsContained = hp >= HT.HstPtrBegin && hp < HT.HstPtrEnd &&
        (hp+Size) <= HT.HstPtrEnd;
    // Does it extend into an already mapped region?
    lr.Flags.ExtendsBefore = hp < HT.HstPtrBegin && (hp+Size) > HT.HstPtrBegin;
    // Does it extend beyond the mapped region?
    lr.Flags.ExtendsAfter = hp < HT.HstPtrEnd && (hp+Size) > HT.HstPtrEnd;

    if (lr.Flags.IsContained || lr.Flags.ExtendsBefore ||
        lr.Flags.ExtendsAfter) {
      break;
    }
  }

  if (lr.Flags.ExtendsBefore) {
    DP("WARNING: Pointer is not mapped but section extends into already "
        "mapped data\n");
  }
  if (lr.Flags.ExtendsAfter) {
    DP("WARNING: Pointer is already mapped but section extends beyond mapped "
        "region\n");
  }

  return lr;
}

void *DeviceTy::bulkLookupMapping(void *HstPtrBegin, int64_t Size) {
  void *TgtPtrBegin = NULL;
  if (IsBulkEnabled) {
    // FIXME  lower latency
    // FIXME Add extend check
    auto entry = RegionList.lower_bound((intptr_t)HstPtrBegin);
    bool IsContained = false;
    bool ExtendsBefore = false;
    bool ExtendsAfter = false;
    if (entry != RegionList.end()) {
    //    DP2("[%p, %p]\n", (void*)entry->second.HstPtrBegin, (void*) entry->second.HstPtrEnd);
      if ((intptr_t) HstPtrBegin < entry->second.HstPtrEnd) {
        if ((intptr_t) HstPtrBegin + Size <= entry->second.HstPtrEnd) {
          IsContained = true;
          TgtPtrBegin = (void*)((intptr_t)HstPtrBegin + entry->second.bias);
        } else {
          ExtendsAfter  = true;
        }
      } else if (entry != RegionList.begin()) {
        // check higher
        if ((intptr_t) HstPtrBegin + Size < (--entry)->second.HstPtrEnd) {
          ExtendsBefore = true;
        }
      }
    }
  } else {
    DP2("ERROR, bulkLookupMapping should not be called\n");
  }
  if (!TgtPtrBegin) {
    DP2("ERROR, bulkLookupMapping no region found:%p %ld\n", HstPtrBegin, Size);
  }
  return TgtPtrBegin;
}


// Used by target_data_begin
// Return the target pointer begin (where the data will be moved).
// Allocate memory if this is the first occurrence of this mapping.
// Increment the reference counter.
// If NULL is returned, then either data allocation failed or the user tried
// to do an illegal mapping.
void *DeviceTy::getOrAllocTgtPtr(void *HstPtrBegin, void *HstPtrBase,
    int64_t Size, bool &IsNew, bool IsImplicit, bool UpdateRefCount) {
  void *rc = NULL;
  if (IsBulkEnabled) {
    DP2("Dont do this\n");
    return rc;
    rc = bulkLookupMapping(HstPtrBegin, Size);
  }
  DataMapMtx.lock();
  LookupResult lr = lookupMapping(HstPtrBegin, Size);

  // Check if the pointer is contained.
  if (lr.Flags.IsContained ||
      ((lr.Flags.ExtendsBefore || lr.Flags.ExtendsAfter) && IsImplicit)) {
    auto &HT = *lr.Entry;
    IsNew = false;

    if (UpdateRefCount)
      ++HT.RefCount;

    uintptr_t tp = HT.TgtPtrBegin + ((uintptr_t)HstPtrBegin - HT.HstPtrBegin);
    DP("Mapping exists%s with HstPtrBegin=" DPxMOD ", TgtPtrBegin=" DPxMOD ", "
        "Size=%ld,%s RefCount=%s\n", (IsImplicit ? " (implicit)" : ""),
        DPxPTR(HstPtrBegin), DPxPTR(tp), Size,
        (UpdateRefCount ? " updated" : ""),
        (CONSIDERED_INF(HT.RefCount)) ? "INF" :
            std::to_string(HT.RefCount).c_str());
    rc = (void *)tp;
  } else if ((lr.Flags.ExtendsBefore || lr.Flags.ExtendsAfter) && !IsImplicit) {
    // Explicit extension of mapped data - not allowed.
    DP("Explicit extension of mapping is not allowed.\n");
  } else if (Size) {
    // If it is not contained and Size > 0 we should create a new entry for it.
    IsNew = true;
    uintptr_t tp = (uintptr_t)RTL->data_alloc(RTLDeviceID, Size, HstPtrBegin);
    DP("Creating new map entry: HstBase=" DPxMOD ", HstBegin=" DPxMOD ", "
        "HstEnd=" DPxMOD ", TgtBegin=" DPxMOD "\n", DPxPTR(HstPtrBase),
        DPxPTR(HstPtrBegin), DPxPTR((uintptr_t)HstPtrBegin + Size), DPxPTR(tp));

    HostDataToTargetMap.push_front(HostDataToTargetTy((uintptr_t)HstPtrBase,
        (uintptr_t)HstPtrBegin, (uintptr_t)HstPtrBegin + Size, tp));
    rc = (void *)tp;
  }

  DataMapMtx.unlock();
  return rc;
}

// Used by target_data_begin, target_data_end, target_data_update and target.
// Return the target pointer begin (where the data will be moved).
// Decrement the reference counter if called from target_data_end.
void *DeviceTy::getTgtPtrBegin(void *HstPtrBegin, int64_t Size, bool &IsLast,
    bool UpdateRefCount) {
  void *rc = NULL;
  if (IsBulkEnabled) {
    void *rc = bulkLookupMapping(HstPtrBegin, Size);
    return rc;
  }
  DataMapMtx.lock();
  LookupResult lr = lookupMapping(HstPtrBegin, Size);

  if (lr.Flags.IsContained || lr.Flags.ExtendsBefore || lr.Flags.ExtendsAfter) {
    auto &HT = *lr.Entry;
    IsLast = !(HT.RefCount > 1);

    if (HT.RefCount > 1 && UpdateRefCount)
      --HT.RefCount;

    uintptr_t tp = HT.TgtPtrBegin + ((uintptr_t)HstPtrBegin - HT.HstPtrBegin);
    DP("Mapping exists with HstPtrBegin=" DPxMOD ", TgtPtrBegin=" DPxMOD ", "
        "Size=%ld,%s RefCount=%s\n", DPxPTR(HstPtrBegin), DPxPTR(tp), Size,
        (UpdateRefCount ? " updated" : ""),
        (CONSIDERED_INF(HT.RefCount)) ? "INF" :
            std::to_string(HT.RefCount).c_str());
    rc = (void *)tp;
  } else {
    IsLast = false;
  }

  DataMapMtx.unlock();
  return rc;
}

// Return the target pointer begin (where the data will be moved).
// Lock-free version called when loading global symbols from the fat binary.
void *DeviceTy::getTgtPtrBegin(void *HstPtrBegin, int64_t Size) {
  uintptr_t hp = (uintptr_t)HstPtrBegin;
  if (IsBulkEnabled) {
    void *rc = bulkLookupMapping(HstPtrBegin, Size);
    return rc;
  }
  LookupResult lr = lookupMapping(HstPtrBegin, Size);
  if (lr.Flags.IsContained || lr.Flags.ExtendsBefore || lr.Flags.ExtendsAfter) {
    auto &HT = *lr.Entry;
    uintptr_t tp = HT.TgtPtrBegin + (hp - HT.HstPtrBegin);
    return (void *)tp;
  }

  return NULL;
}

int DeviceTy::deallocTgtPtr(void *HstPtrBegin, int64_t Size, bool ForceDelete) {
  // Check if the pointer is contained in any sub-nodes.
  int rc;
  DataMapMtx.lock();
  LookupResult lr = lookupMapping(HstPtrBegin, Size);
  if (lr.Flags.IsContained || lr.Flags.ExtendsBefore || lr.Flags.ExtendsAfter) {
    auto &HT = *lr.Entry;
    if (ForceDelete)
      HT.RefCount = 1;
    if (--HT.RefCount <= 0) {
      assert(HT.RefCount == 0 && "did not expect a negative ref count");
      DP("Deleting tgt data " DPxMOD " of size %ld\n",
          DPxPTR(HT.TgtPtrBegin), Size);
      RTL->data_delete(RTLDeviceID, (void *)HT.TgtPtrBegin);
      DP("Removing%s mapping with HstPtrBegin=" DPxMOD ", TgtPtrBegin=" DPxMOD
          ", Size=%ld\n", (ForceDelete ? " (forced)" : ""),
          DPxPTR(HT.HstPtrBegin), DPxPTR(HT.TgtPtrBegin), Size);
      HostDataToTargetMap.erase(lr.Entry);
    }
    rc = OFFLOAD_SUCCESS;
  } else {
    DP("Section to delete (hst addr " DPxMOD ") does not exist in the allocated"
       " memory\n", DPxPTR(HstPtrBegin));
    rc = OFFLOAD_FAIL;
  }

  DataMapMtx.unlock();
  return rc;
}

/// Init device, should not be called directly.
void DeviceTy::init() {
  // Make call to init_requires if it exists for this plugin.
  if (RTL->init_requires)
    RTL->init_requires(RTLRequiresFlags);
  int32_t rc = RTL->init_device(RTLDeviceID);
  if (rc == OFFLOAD_SUCCESS) {
    if (getenv("OMP_AT")) {
      DP2("Address Translate Enabled\n");
      IsATEnabled = true;
    } else {
      IsATEnabled = false;
    }
    if (getenv("OMP_BULK") || IsATEnabled) {
      DP2("Bulk Transfer Enabled\n");
      IsBulkEnabled = true;
    } else {
      IsBulkEnabled = false;
    }
    IsInit = true;
  }
}

/// Thread-safe method to initialize the device only once.
int32_t DeviceTy::initOnce() {
  std::call_once(InitFlag, &DeviceTy::init, this);

  // At this point, if IsInit is true, then either this thread or some other
  // thread in the past successfully initialized the device, so we can return
  // OFFLOAD_SUCCESS. If this thread executed init() via call_once() and it
  // failed, return OFFLOAD_FAIL. If call_once did not invoke init(), it means
  // that some other thread already attempted to execute init() and if IsInit
  // is still false, return OFFLOAD_FAIL.
  if (IsInit)
    return OFFLOAD_SUCCESS;
  else
    return OFFLOAD_FAIL;
}

// Load binary to device.
__tgt_target_table *DeviceTy::load_binary(void *Img) {
  RTL->Mtx.lock();
  __tgt_target_table *rc = RTL->load_binary(RTLDeviceID, Img);
  RTL->Mtx.unlock();
  return rc;
}

// Submit data to device.
int32_t DeviceTy::data_submit(void *TgtPtrBegin, void *HstPtrBegin,
    int64_t Size) {
  return RTL->data_submit(RTLDeviceID, TgtPtrBegin, HstPtrBegin, Size);
}

// Retrieve data from device.
int32_t DeviceTy::data_retrieve(void *HstPtrBegin, void *TgtPtrBegin,
    int64_t Size) {
  return RTL->data_retrieve(RTLDeviceID, HstPtrBegin, TgtPtrBegin, Size);
}

// Add pointer update to suspend list.
int32_t DeviceTy::suspend_update(void *HstPtrBaseAddr, void *HstPtrValue, void *OldHstPtrBase, void* NewHstPtrBase) {
  UpdatePtrTy upl = {HstPtrBaseAddr, HstPtrValue, OldHstPtrBase, NewHstPtrBase};
  UpdatePtrList.push(upl);
  return 0;
}

// Update the suspend list
int32_t DeviceTy::update_suspend_list() {
  PFs();
  int32_t ret;

  //struct UpdatePtrTy upt;
  int i = 0;
  //DP2("Start update_suspend_list \n");
  while (!UpdatePtrList.empty()) {
    struct UpdatePtrTy &upt = UpdatePtrList.front();
    void *PtrBaseAddr = bulkLookupMapping(upt.PtrBaseAddr,sizeof(void*));
    //uintptr_t Delta = (intptr_t)upt.PtrValue - (intptr_t)upt.PtrBaseAddr;
    void *PtrValue = (void*)((uintptr_t)bulkLookupMapping(upt.PtrValue,sizeof(void*)));
    //DP2("Delta: %ld\n", Delta);
    DP2("Update pointer (" DPxMOD ") <- [" DPxMOD "]\n",
                  DPxPTR(PtrBaseAddr), DPxPTR(PtrValue));
    ret = data_submit(PtrBaseAddr,  &PtrValue, sizeof(void*));
    if (ret) {
      DP2("update_suspend_list failed");
      return ret;
    }
    ShadowMtx.lock();
    //ShadowPtrMap[upt.PtrAddr] = {upt.PtrValue, PtrValue, PtrBaseAddr};
    //ShadowPtrMap[upt.PtrValue] = {upt.PtrAddr, PtrValue, PtrBaseAddr};
    //           old ptr base  new ptr base
    ShadowPtrMap[upt.OldHstPtrBase] = {upt.NewHstPtrBase, PtrBaseAddr,PtrValue};
    ShadowMtx.unlock();

    UpdatePtrList.pop();
    ++i;
  }
  return 0;
}

int32_t DeviceTy::dump_regions() {
  DP2("--- Dump RegionList -----------\n");
  DP2("|\tSize :%lu\n", RegionList.size());
  auto it = RegionList.begin();
  while (it != RegionList.end()) {
    DP2("|%s\n", it->second.getString().c_str());
    it++;
    // Add size?
    //DP2("|[%p:%p] Size:%lu Tgt:%p\n", HstBegin, HstEnd, Size, (void*)it->second.TgtPtrBegin);
  }
  DP2("-------------------------------\n");
  return 0;
}


// Run region on device
int32_t DeviceTy::run_region(void *TgtEntryPtr, void **TgtVarsPtr,
    ptrdiff_t *TgtOffsets, int32_t TgtVarsSize) {
  return RTL->run_region(RTLDeviceID, TgtEntryPtr, TgtVarsPtr, TgtOffsets,
      TgtVarsSize);
}

// Run team region on device.
int32_t DeviceTy::run_team_region(void *TgtEntryPtr, void **TgtVarsPtr,
    ptrdiff_t *TgtOffsets, int32_t TgtVarsSize, int32_t NumTeams,
    int32_t ThreadLimit, uint64_t LoopTripCount) {
  return RTL->run_team_region(RTLDeviceID, TgtEntryPtr, TgtVarsPtr, TgtOffsets,
      TgtVarsSize, NumTeams, ThreadLimit, LoopTripCount);
}

// bulk transfer depends on Transfer type
int32_t DeviceTy::bulk_transfer() {
  auto it = RegionList.begin();
  void *TgtPtrBegin;
  while (it != RegionList.end()) {
    size_t size = it->second.HstPtrEnd - it->second.HstPtrBegin;

    if (Transfer == TransferFrom) {
      // Must be searched
      // Trans
      void *TgtPtrBegin = bulkLookupMapping((void*)it->second.HstPtrBegin, size);
      if (TgtPtrBegin) {
        data_retrieve((void*)it->second.HstPtrBegin, TgtPtrBegin, size);
      }
    } else if (Transfer == TransferTo) {
      bool IsNew = false;
      if (!it->second.TgtPtrBegin) {
        IsNew = true;
			  TgtPtrBegin = RTL->data_alloc(RTLDeviceID, size, NULL);
    	  it->second.TgtPtrBegin = (intptr_t)TgtPtrBegin;
        it->second.bias = (intptr_t)TgtPtrBegin - (intptr_t) it->second.HstPtrBegin;
      }
      data_submit(TgtPtrBegin,(void*)it->second.HstPtrBegin,size);
    } else {
      DP2("Error unrecognized transfer type \n");
    }
    it++;
  }
  return OFFLOAD_SUCCESS;
}

int32_t DeviceTy::bulk_map_from( void *HstPtrBegin, size_t Size) {
  /*
  if (Transfer != TransferFrom) {
    RegionList.clear();
    DP2("Device clean regions\n");
    Transfer = TransferFrom;
  }
  return bulk_add(HstPtrBegin, Size);
  */
  void *TgtPtrBegin = bulkLookupMapping(HstPtrBegin, Size);
  if (TgtPtrBegin) {
    data_retrieve(HstPtrBegin, TgtPtrBegin, Size);
    /*
    if (Size == 4) {
      DP2("Retrieve %p val: %d\n", HstPtrBegin, *(int*)HstPtrBegin);
    }
    */
    return OFFLOAD_SUCCESS;
  }
  return OFFLOAD_FAIL;
}

int32_t DeviceTy::bulk_map_to( void *HstPtrBegin, size_t Size) {
  if (Transfer != TransferTo) {
    RegionList.clear();
    Transfer = TransferTo;
  }
  return bulk_add(HstPtrBegin, Size);
}

void *DeviceTy::table_transfer(std::vector <RegionTy>table) {
  int table_size = table.size() * sizeof(RegionTy);
  void *tgt_table = RTL->data_alloc(RTLDeviceID, table_size, NULL);
  data_submit(tgt_table, &table[0],table_size);
  return tgt_table;
}

int32_t DeviceTy::bulk_add(void *HstPtrBegin, size_t Size) {
  //intptr_t threshold = 1000;
  intptr_t threshold = getpagesize();
  // NOTE we use signed int to compute ptr
  intptr_t HstPtrBeginNew = (intptr_t) HstPtrBegin;
  intptr_t HstPtrEndNew = (intptr_t) HstPtrBegin + Size;
  intptr_t HstPtrBeginCur, HstPtrEndCur;
  
  auto it = RegionList.lower_bound((intptr_t)HstPtrBegin);
  // HstPtrBegin <= it

  //Merge
  bool extend_high = false;
  bool extend_low = false;

  // FIXME contain is not correct
  bool contain = false;

  // Region list  <-- back--   ++forward--->
  //              High addr ---------- Low addr
  // Check forward
  if (it != RegionList.end()) {
    HstPtrBeginCur = (intptr_t) it->second.HstPtrBegin;
    HstPtrEndCur = (intptr_t) it->second.HstPtrEnd;
    if (HstPtrBeginNew - HstPtrEndCur < threshold) {
      contain = true;
      HstPtrBeginNew = HstPtrBeginCur;
      if (HstPtrEndNew > HstPtrEndCur) {
        extend_high = true;
      } else {
        HstPtrEndNew = HstPtrEndCur;
      }
    } 
  }
  // Check backward, higher addr
  if (!contain && it != RegionList.begin()) {
    it--;
    HstPtrBeginCur = (intptr_t)  it->second.HstPtrBegin;
    HstPtrEndCur = (intptr_t) it->second.HstPtrEnd;
    if (HstPtrBeginCur - HstPtrEndNew < threshold) {
      contain  = true;
      // FIXME next line is useless?
      //HstPtrBeginNew = HstPtrBeginCur < HstPtrEndNew ? HstPtrEndCur : HstPtrEndNew;
      if (HstPtrEndNew > HstPtrEndCur) {
        extend_high = true;
      } else {
        HstPtrEndNew = HstPtrEndCur;
      }
    }
  }

  auto cur = it;
  if (extend_high) {
    while (cur != RegionList.begin()) {
      cur --;
      if ((intptr_t) cur->second.HstPtrEnd - HstPtrBeginNew < threshold) {
        HstPtrBeginCur = (intptr_t) cur->second.HstPtrBegin;
        HstPtrBeginNew = HstPtrBeginCur > HstPtrBeginNew ? HstPtrBeginNew : HstPtrBeginCur;
        DP2("Merged %s\n", (void*)cur->second.getString().c_str());
        // Remove region
        RegionList.erase(cur--);
      }
    }
  }
  if (contain) {
    RegionList.erase(it);
  }
  RegionTy r;
  r.HstPtrBegin = (intptr_t) HstPtrBeginNew;
  r.HstPtrEnd = (intptr_t) HstPtrEndNew;
  RegionList[(intptr_t) HstPtrBeginNew] = r;
  if (contain) {
    DP2("Added [%p,%p] to region: %s, extend_high: %d\n", (void*)HstPtrBegin, (char*)HstPtrBegin + Size, r.getString().c_str(), extend_high);
  } else {
    DP2("New region [%p,%p]\n", (void*)HstPtrBegin, (char*)HstPtrBegin + Size);
  }
  return contain;
}

/// Check whether a device has an associated RTL and initialize it if it's not
/// already initialized.
bool device_is_ready(int device_num) {
  DP("Checking whether device %d is ready.\n", device_num);
  // Devices.size() can only change while registering a new
  // library, so try to acquire the lock of RTLs' mutex.
  RTLsMtx.lock();
  size_t Devices_size = Devices.size();
  RTLsMtx.unlock();
  if (Devices_size <= (size_t)device_num) {
    DP("Device ID  %d does not have a matching RTL\n", device_num);
    return false;
  }

  // Get device info
  DeviceTy &Device = Devices[device_num];

  DP("Is the device %d (local ID %d) initialized? %d\n", device_num,
       Device.RTLDeviceID, Device.IsInit);

  // Init the device if not done before
  if (!Device.IsInit && Device.initOnce() != OFFLOAD_SUCCESS) {
    DP("Failed to init device %d\n", device_num);
    return false;
  }

  DP("Device %d is ready to use.\n", device_num);

  return true;
}
