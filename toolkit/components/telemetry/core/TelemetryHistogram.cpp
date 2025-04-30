/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TelemetryHistogram.h"

#include <limits>
#include "base/histogram.h"
#include "ipc/TelemetryIPCAccumulator.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Array.h"  // JS::GetArrayLength, JS::IsArrayObject, JS::NewArrayObject
#include "js/GCAPI.h"
#include "js/Object.h"  // JS::GetClass, JS::GetMaybePtrFromReservedSlot, JS::SetReservedSlot
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_DefineFunction, JS_DefineProperty, JS_DefineUCProperty, JS_Enumerate, JS_GetElement, JS_GetProperty, JS_GetPropertyById
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/Atomics.h"
#include "mozilla/JSONWriter.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/Unused.h"
#include "nsClassHashtable.h"
#include "nsString.h"
#include "nsHashKeys.h"
#include "nsITelemetry.h"
#include "nsPrintfCString.h"
#include "TelemetryHistogramNameMap.h"
#include "TelemetryScalar.h"

using base::BooleanHistogram;
using base::CountHistogram;
using base::FlagHistogram;
using base::LinearHistogram;
using mozilla::MakeUnique;
using mozilla::StaticMutex;
using mozilla::StaticMutexAutoLock;
using mozilla::UniquePtr;
using mozilla::Telemetry::HistogramAccumulation;
using mozilla::Telemetry::HistogramCount;
using mozilla::Telemetry::HistogramID;
using mozilla::Telemetry::HistogramIDByNameLookup;
using mozilla::Telemetry::KeyedHistogramAccumulation;
using mozilla::Telemetry::ProcessID;
using mozilla::Telemetry::Common::CanRecordDataset;
using mozilla::Telemetry::Common::CanRecordProduct;
using mozilla::Telemetry::Common::GetCurrentProduct;
using mozilla::Telemetry::Common::GetIDForProcessName;
using mozilla::Telemetry::Common::GetNameForProcessID;
using mozilla::Telemetry::Common::IsExpiredVersion;
using mozilla::Telemetry::Common::IsInDataset;
using mozilla::Telemetry::Common::LogToBrowserConsole;
using mozilla::Telemetry::Common::RecordedProcessType;
using mozilla::Telemetry::Common::StringHashSet;
using mozilla::Telemetry::Common::SupportedProduct;
using mozilla::Telemetry::Common::ToJSString;

namespace TelemetryIPCAccumulator = mozilla::TelemetryIPCAccumulator;

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// Naming: there are two kinds of functions in this file:
//
// * Functions named internal_*: these can only be reached via an
//   interface function (TelemetryHistogram::*).  They mostly expect
//   the interface function to have acquired
//   |gTelemetryHistogramMutex|, so they do not have to be
//   thread-safe.  However, those internal_* functions that are
//   reachable from internal_WrapAndReturnHistogram and
//   internal_WrapAndReturnKeyedHistogram can sometimes be called
//   without |gTelemetryHistogramMutex|, and so might be racey.
//
// * Functions named TelemetryHistogram::*.  This is the external interface.
//   Entries and exits to these functions are serialised using
//   |gTelemetryHistogramMutex|, except for GetKeyedHistogramSnapshots and
//   CreateHistogramSnapshots.
//
// Avoiding races and deadlocks:
//
// All functions in the external interface (TelemetryHistogram::*) are
// serialised using the mutex |gTelemetryHistogramMutex|.  This means
// that the external interface is thread-safe, and many of the
// internal_* functions can ignore thread safety.  But it also brings
// a danger of deadlock if any function in the external interface can
// get back to that interface.  That is, we will deadlock on any call
// chain like this
//
// TelemetryHistogram::* -> .. any functions .. -> TelemetryHistogram::*
//
// To reduce the danger of that happening, observe the following rules:
//
// * No function in TelemetryHistogram::* may directly call, nor take the
//   address of, any other function in TelemetryHistogram::*.
//
// * No internal function internal_* may call, nor take the address
//   of, any function in TelemetryHistogram::*.
//
// internal_WrapAndReturnHistogram and
// internal_WrapAndReturnKeyedHistogram are not protected by
// |gTelemetryHistogramMutex| because they make calls to the JS
// engine, but that can in turn call back to Telemetry and hence back
// to a TelemetryHistogram:: function, in order to report GC and other
// statistics.  This would lead to deadlock due to attempted double
// acquisition of |gTelemetryHistogramMutex|, if the internal_* functions
// were required to be protected by |gTelemetryHistogramMutex|.  To
// break that cycle, we relax that requirement.  Unfortunately this
// means that this file is not guaranteed race-free.

// This is a StaticMutex rather than a plain Mutex (1) so that
// it gets initialised in a thread-safe manner the first time
// it is used, and (2) because it is never de-initialised, and
// a normal Mutex would show up as a leak in BloatView.  StaticMutex
// also has the "OffTheBooks" property, so it won't show as a leak
// in BloatView.
static StaticMutex gTelemetryHistogramMutex MOZ_UNANNOTATED;

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE TYPES

namespace {

// Hardcoded probes
//
// The order of elements here is important to minimize the memory footprint of a
// HistogramInfo instance.
//
// Any adjustements need to be reflected in gen_histogram_data.py
struct HistogramInfo {
  uint32_t min;
  uint32_t max;
  uint32_t bucketCount;
  uint32_t name_offset;
  uint32_t expiration_offset;
  uint32_t label_count;
  uint32_t key_count;
  uint32_t store_count;
  uint16_t label_index;
  uint16_t key_index;
  uint16_t store_index;
  RecordedProcessType record_in_processes;
  bool keyed;
  uint8_t histogramType;
  uint8_t dataset;
  SupportedProduct products;

  const char* name() const;
  const char* expiration() const;
  nsresult label_id(const char* label, uint32_t* labelId) const;
  bool allows_key(const nsACString& key) const;
  bool is_single_store() const;
};

// Structs used to keep information about the histograms for which a
// snapshot should be created.
struct HistogramSnapshotData {
  CopyableTArray<base::Histogram::Sample> mBucketRanges;
  CopyableTArray<base::Histogram::Count> mBucketCounts;
  int64_t mSampleSum;  // Same type as base::Histogram::SampleSet::sum_
};

struct HistogramSnapshotInfo {
  HistogramSnapshotData data;
  HistogramID histogramID;
};

typedef mozilla::Vector<HistogramSnapshotInfo> HistogramSnapshotsArray;
typedef mozilla::Vector<HistogramSnapshotsArray> HistogramProcessSnapshotsArray;

// The following is used to handle snapshot information for keyed histograms.
typedef nsTHashMap<nsCStringHashKey, HistogramSnapshotData>
    KeyedHistogramSnapshotData;

struct KeyedHistogramSnapshotInfo {
  KeyedHistogramSnapshotData data;
  HistogramID histogramId;
};

typedef mozilla::Vector<KeyedHistogramSnapshotInfo>
    KeyedHistogramSnapshotsArray;
typedef mozilla::Vector<KeyedHistogramSnapshotsArray>
    KeyedHistogramProcessSnapshotsArray;

/**
 * A Histogram storage engine.
 *
 * Takes care of recording data into multiple stores if necessary.
 */
class Histogram {
 public:
  /*
   * Create a new histogram instance from the given info.
   *
   * If the histogram is already expired, this does not allocate.
   */
  Histogram(HistogramID histogramId, const HistogramInfo& info, bool expired);
  ~Histogram();

  /**
   * Add a sample to this histogram in all registered stores.
   */
  void Add(uint32_t sample);

  /**
   * Clear the named store for this histogram.
   */
  void Clear(const nsACString& store);

  /**
   * Get the histogram instance from the named store.
   */
  bool GetHistogram(const nsACString& store, base::Histogram** h);

  bool IsExpired() const {
    if (mIsExpired) {
      PROFILER_MARKER_TEXT("HistogramError", TELEMETRY,
                           mozilla::MarkerStack::Capture(),
                           "accessing expired histogram");
    }
    return mIsExpired;
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

 private:
  // String -> Histogram*
  typedef nsClassHashtable<nsCStringHashKey, base::Histogram> HistogramStore;
  HistogramStore mStorage;

  // A valid pointer if this histogram belongs to only the main store
  base::Histogram* mSingleStore;

  // We don't track stores for expired histograms.
  // We just store a single flag and all other operations become a no-op.
  bool mIsExpired;
};

class KeyedHistogram {
 public:
  KeyedHistogram(HistogramID id, const HistogramInfo& info, bool expired);
  ~KeyedHistogram();
  nsresult GetHistogram(const nsCString& aStore, const nsCString& key,
                        base::Histogram** histogram);
  base::Histogram* GetHistogram(const nsCString& aStore, const nsCString& key);
  uint32_t GetHistogramType() const { return mHistogramInfo.histogramType; }
  nsresult GetKeys(const StaticMutexAutoLock& aLock, const nsCString& store,
                   nsTArray<nsCString>& aKeys);
  // Note: unlike other methods, GetJSSnapshot is thread safe.
  nsresult GetJSSnapshot(JSContext* cx, JS::Handle<JSObject*> obj,
                         const nsACString& aStore, bool clearSubsession);
  nsresult GetSnapshot(const StaticMutexAutoLock& aLock,
                       const nsACString& aStore,
                       KeyedHistogramSnapshotData& aSnapshot,
                       bool aClearSubsession);

  nsresult Add(const nsCString& key, uint32_t aSample, ProcessID aProcessType);
  void Clear(const nsACString& aStore);

  HistogramID GetHistogramID() const { return mId; }

  bool IsEmpty(const nsACString& aStore) const;

  bool IsExpired() const {
    if (mIsExpired) {
      PROFILER_MARKER_TEXT("HistogramError", TELEMETRY,
                           mozilla::MarkerStack::Capture(),
                           "accessing expired histogram");
    }
    return mIsExpired;
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

 private:
  typedef nsClassHashtable<nsCStringHashKey, base::Histogram>
      KeyedHistogramMapType;
  typedef nsClassHashtable<nsCStringHashKey, KeyedHistogramMapType>
      StoreMapType;

  StoreMapType mStorage;
  // A valid map if this histogram belongs to only the main store
  KeyedHistogramMapType* mSingleStore;

  const HistogramID mId;
  const HistogramInfo& mHistogramInfo;
  bool mIsExpired;
};

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE STATE, SHARED BY ALL THREADS

namespace {

// Set to true once this global state has been initialized
bool gTelemetryHistogramInitDone = false;

// Whether we are collecting the base, opt-out, Histogram data.
bool gTelemetryHistogramCanRecordBase = false;
// Whether we are collecting the extended, opt-in, Histogram data.
bool gTelemetryHistogramCanRecordExtended = false;

// The storage for actual Histogram instances.
// We use separate ones for plain and keyed histograms.
Histogram** gHistogramStorage;
// Keyed histograms internally map string keys to individual Histogram
// instances.
KeyedHistogram** gKeyedHistogramStorage;

// To simplify logic below we use a single histogram instance for all expired
// histograms.
Histogram* gExpiredHistogram = nullptr;

// The single placeholder for expired keyed histograms.
KeyedHistogram* gExpiredKeyedHistogram = nullptr;

// This tracks whether recording is enabled for specific histograms.
// To utilize C++ initialization rules, we invert the meaning to "disabled".
bool gHistogramRecordingDisabled[HistogramCount] = {};

// This is for gHistogramInfos, gHistogramStringTable
#include "TelemetryHistogramData.inc"

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE CONSTANTS

namespace {

const char* TEST_HISTOGRAM_PREFIX = "TELEMETRY_TEST_";

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// The core storage access functions.
// They wrap access to the histogram storage and lookup caches.

namespace {

size_t internal_KeyedHistogramStorageIndex(HistogramID aHistogramId,
                                           ProcessID aProcessId) {
  return aHistogramId * size_t(ProcessID::Count) + size_t(aProcessId);
}

size_t internal_HistogramStorageIndex(const StaticMutexAutoLock& aLock,
                                      HistogramID aHistogramId,
                                      ProcessID aProcessId) {
  static_assert(HistogramCount < std::numeric_limits<size_t>::max() /
                                     size_t(ProcessID::Count),
                "Too many histograms and processes to store in a 1D array.");

  return aHistogramId * size_t(ProcessID::Count) + size_t(aProcessId);
}

Histogram* internal_GetHistogramFromStorage(const StaticMutexAutoLock& aLock,
                                            HistogramID aHistogramId,
                                            ProcessID aProcessId) {
  size_t index =
      internal_HistogramStorageIndex(aLock, aHistogramId, aProcessId);
  return gHistogramStorage[index];
}

void internal_SetHistogramInStorage(const StaticMutexAutoLock& aLock,
                                    HistogramID aHistogramId,
                                    ProcessID aProcessId,
                                    Histogram* aHistogram) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Histograms are stored only in the parent process.");

  size_t index =
      internal_HistogramStorageIndex(aLock, aHistogramId, aProcessId);
  MOZ_ASSERT(!gHistogramStorage[index],
             "Mustn't overwrite storage without clearing it first.");
  gHistogramStorage[index] = aHistogram;
}

KeyedHistogram* internal_GetKeyedHistogramFromStorage(HistogramID aHistogramId,
                                                      ProcessID aProcessId) {
  size_t index = internal_KeyedHistogramStorageIndex(aHistogramId, aProcessId);
  return gKeyedHistogramStorage[index];
}

void internal_SetKeyedHistogramInStorage(HistogramID aHistogramId,
                                         ProcessID aProcessId,
                                         KeyedHistogram* aKeyedHistogram) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Keyed Histograms are stored only in the parent process.");

  size_t index = internal_KeyedHistogramStorageIndex(aHistogramId, aProcessId);
  MOZ_ASSERT(!gKeyedHistogramStorage[index],
             "Mustn't overwrite storage without clearing it first");
  gKeyedHistogramStorage[index] = aKeyedHistogram;
}

// Factory function for base::Histogram instances.
base::Histogram* internal_CreateBaseHistogramInstance(const HistogramInfo& info,
                                                      int bucketsOffset);

// Factory function for histogram instances.
Histogram* internal_CreateHistogramInstance(HistogramID histogramId);

bool internal_IsHistogramEnumId(HistogramID aID) {
  static_assert(((HistogramID)-1 > 0), "ID should be unsigned.");
  return aID < HistogramCount;
}

// Look up a plain histogram by id.
Histogram* internal_GetHistogramById(const StaticMutexAutoLock& aLock,
                                     HistogramID histogramId,
                                     ProcessID processId,
                                     bool instantiate = true) {
  MOZ_ASSERT(internal_IsHistogramEnumId(histogramId));
  MOZ_ASSERT(!gHistogramInfos[histogramId].keyed);
  MOZ_ASSERT(processId < ProcessID::Count);

  Histogram* h =
      internal_GetHistogramFromStorage(aLock, histogramId, processId);
  if (h || !instantiate) {
    return h;
  }

  h = internal_CreateHistogramInstance(histogramId);
  MOZ_ASSERT(h);
  internal_SetHistogramInStorage(aLock, histogramId, processId, h);

  return h;
}

// Look up a keyed histogram by id.
KeyedHistogram* internal_GetKeyedHistogramById(HistogramID histogramId,
                                               ProcessID processId,
                                               bool instantiate = true) {
  MOZ_ASSERT(internal_IsHistogramEnumId(histogramId));
  MOZ_ASSERT(gHistogramInfos[histogramId].keyed);
  MOZ_ASSERT(processId < ProcessID::Count);

  KeyedHistogram* kh =
      internal_GetKeyedHistogramFromStorage(histogramId, processId);
  if (kh || !instantiate) {
    return kh;
  }

  const HistogramInfo& info = gHistogramInfos[histogramId];
  const bool isExpired = IsExpiredVersion(info.expiration());

  // If the keyed histogram is expired, set its storage to the expired
  // keyed histogram.
  if (isExpired) {
    if (!gExpiredKeyedHistogram) {
      // If we don't have an expired keyed histogram, create one.
      gExpiredKeyedHistogram =
          new KeyedHistogram(histogramId, info, true /* expired */);
      MOZ_ASSERT(gExpiredKeyedHistogram);
    }
    kh = gExpiredKeyedHistogram;
  } else {
    kh = new KeyedHistogram(histogramId, info, false /* expired */);
  }

  internal_SetKeyedHistogramInStorage(histogramId, processId, kh);

  return kh;
}

// Look up a histogram id from a histogram name.
nsresult internal_GetHistogramIdByName(const StaticMutexAutoLock& aLock,
                                       const nsACString& name,
                                       HistogramID* id) {
  const uint32_t idx = HistogramIDByNameLookup(name);
  MOZ_ASSERT(idx < HistogramCount,
             "Intermediate lookup should always give a valid index.");

  // The lookup hashes the input and uses it as an index into the value array.
  // Hash collisions can still happen for unknown values,
  // therefore we check that the name matches.
  if (name.Equals(gHistogramInfos[idx].name())) {
    *id = HistogramID(idx);
    return NS_OK;
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: Misc small helpers

namespace {

bool internal_CanRecordBase() { return gTelemetryHistogramCanRecordBase; }

bool internal_CanRecordExtended() {
  return gTelemetryHistogramCanRecordExtended;
}

bool internal_AttemptedGPUProcess() {
  // Check if it was tried to launch a process.
  bool attemptedGPUProcess = false;
  if (auto gpm = mozilla::gfx::GPUProcessManager::Get()) {
    attemptedGPUProcess = gpm->AttemptedGPUProcess();
  }
  return attemptedGPUProcess;
}

// Note: this is completely unrelated to mozilla::IsEmpty.
bool internal_IsEmpty(const StaticMutexAutoLock& aLock,
                      const base::Histogram* h) {
  return h->is_empty();
}

void internal_SetHistogramRecordingEnabled(const StaticMutexAutoLock& aLock,
                                           HistogramID id, bool aEnabled) {
  MOZ_ASSERT(internal_IsHistogramEnumId(id));
  gHistogramRecordingDisabled[id] = !aEnabled;
}

bool internal_IsRecordingEnabled(HistogramID id) {
  MOZ_ASSERT(internal_IsHistogramEnumId(id));
  return !gHistogramRecordingDisabled[id];
}

const char* HistogramInfo::name() const {
  return &gHistogramStringTable[this->name_offset];
}

const char* HistogramInfo::expiration() const {
  return &gHistogramStringTable[this->expiration_offset];
}

nsresult HistogramInfo::label_id(const char* label, uint32_t* labelId) const {
  MOZ_ASSERT(label);
  MOZ_ASSERT(this->histogramType == nsITelemetry::HISTOGRAM_CATEGORICAL);
  if (this->histogramType != nsITelemetry::HISTOGRAM_CATEGORICAL) {
    return NS_ERROR_FAILURE;
  }

  for (uint32_t i = 0; i < this->label_count; ++i) {
    // gHistogramLabelTable contains the indices of the label strings in the
    // gHistogramStringTable.
    // They are stored in-order and consecutively, from the offset label_index
    // to (label_index + label_count).
    uint32_t string_offset = gHistogramLabelTable[this->label_index + i];
    const char* const str = &gHistogramStringTable[string_offset];
    if (::strcmp(label, str) == 0) {
      *labelId = i;
      return NS_OK;
    }
  }

  return NS_ERROR_FAILURE;
}

bool HistogramInfo::allows_key(const nsACString& key) const {
  MOZ_ASSERT(this->keyed);

  // If we didn't specify a list of allowed keys, just return true.
  if (this->key_count == 0) {
    return true;
  }

  // Otherwise, check if |key| is in the list of allowed keys.
  for (uint32_t i = 0; i < this->key_count; ++i) {
    // gHistogramKeyTable contains the indices of the key strings in the
    // gHistogramStringTable. They are stored in-order and consecutively,
    // from the offset key_index to (key_index + key_count).
    uint32_t string_offset = gHistogramKeyTable[this->key_index + i];
    const char* const str = &gHistogramStringTable[string_offset];
    if (key.EqualsASCII(str)) {
      return true;
    }
  }

  // |key| was not found.
  return false;
}

bool HistogramInfo::is_single_store() const {
  return store_count == 1 && store_index == UINT16_MAX;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: Histogram Get, Add, Clone, Clear functions

namespace {

nsresult internal_CheckHistogramArguments(const HistogramInfo& info) {
  if (info.histogramType != nsITelemetry::HISTOGRAM_BOOLEAN &&
      info.histogramType != nsITelemetry::HISTOGRAM_FLAG &&
      info.histogramType != nsITelemetry::HISTOGRAM_COUNT) {
    // Sanity checks for histogram parameters.
    if (info.min >= info.max) {
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if (info.bucketCount <= 2) {
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if (info.min < 1) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
  }

  return NS_OK;
}

Histogram* internal_CreateHistogramInstance(HistogramID histogramId) {
  const HistogramInfo& info = gHistogramInfos[histogramId];

  if (NS_FAILED(internal_CheckHistogramArguments(info))) {
    MOZ_ASSERT(false, "Failed histogram argument checks.");
    return nullptr;
  }

  const bool isExpired = IsExpiredVersion(info.expiration());

  if (isExpired) {
    if (!gExpiredHistogram) {
      gExpiredHistogram = new Histogram(histogramId, info, /* expired */ true);
    }

    return gExpiredHistogram;
  }

  Histogram* wrapper = new Histogram(histogramId, info, /* expired */ false);

  return wrapper;
}

base::Histogram* internal_CreateBaseHistogramInstance(
    const HistogramInfo& passedInfo, int bucketsOffset) {
  if (NS_FAILED(internal_CheckHistogramArguments(passedInfo))) {
    MOZ_ASSERT(false, "Failed histogram argument checks.");
    return nullptr;
  }

  // We don't actually store data for expired histograms at all.
  MOZ_ASSERT(!IsExpiredVersion(passedInfo.expiration()));

  HistogramInfo info = passedInfo;
  const int* buckets = &gHistogramBucketLowerBounds[bucketsOffset];

  base::Histogram::Flags flags = base::Histogram::kNoFlags;
  base::Histogram* h = nullptr;
  switch (info.histogramType) {
    case nsITelemetry::HISTOGRAM_EXPONENTIAL:
      h = base::Histogram::FactoryGet(info.min, info.max, info.bucketCount,
                                      flags, buckets);
      break;
    case nsITelemetry::HISTOGRAM_LINEAR:
    case nsITelemetry::HISTOGRAM_CATEGORICAL:
      h = LinearHistogram::FactoryGet(info.min, info.max, info.bucketCount,
                                      flags, buckets);
      break;
    case nsITelemetry::HISTOGRAM_BOOLEAN:
      h = BooleanHistogram::FactoryGet(flags, buckets);
      break;
    case nsITelemetry::HISTOGRAM_FLAG:
      h = FlagHistogram::FactoryGet(flags, buckets);
      break;
    case nsITelemetry::HISTOGRAM_COUNT:
      h = CountHistogram::FactoryGet(flags, buckets);
      break;
    default:
      MOZ_ASSERT(false, "Invalid histogram type");
      return nullptr;
  }

  return h;
}

nsresult internal_HistogramAdd(const StaticMutexAutoLock& aLock,
                               Histogram& histogram, const HistogramID id,
                               uint32_t value, ProcessID aProcessType) {
  // Check if we are allowed to record the data.
  const HistogramInfo& h = gHistogramInfos[id];
  bool canRecordDataset = CanRecordDataset(h.dataset, internal_CanRecordBase(),
                                           internal_CanRecordExtended());
  if (!canRecordDataset) {
    return NS_OK;
  }

  // If `histogram` is a non-parent-process histogram, then recording-enabled
  // has been checked in its owner process.
  if (aProcessType == ProcessID::Parent && !internal_IsRecordingEnabled(id)) {
    PROFILER_MARKER_TEXT(
        "HistogramError", TELEMETRY, mozilla::MarkerStack::Capture(),
        nsPrintfCString("CannotRecordInProcess: %s", h.name()));
    return NS_OK;
  }

  // Don't record if the current platform is not enabled
  if (!CanRecordProduct(h.products)) {
    return NS_OK;
  }

  // The internal representation of a base::Histogram's buckets uses `int`.
  // Clamp large values of `value` to be INT_MAX so they continue to be treated
  // as large values (instead of negative ones).
  if (value > INT_MAX) {
    TelemetryScalar::Add(
        mozilla::Telemetry::ScalarID::TELEMETRY_ACCUMULATE_CLAMPED_VALUES,
        NS_ConvertASCIItoUTF16(h.name()), 1);
    value = INT_MAX;
  }

  histogram.Add(value);

  return NS_OK;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: Histogram reflection helpers

namespace {

/**
 * Copy histograms and samples to Mozilla-friendly structures.
 * Please note that this version does not make use of JS contexts.
 *
 * @param {StaticMutexAutoLock} the proof we hold the mutex.
 * @param {Histogram} the histogram to reflect.
 * @return {nsresult} NS_ERROR_FAILURE if we fail to allocate memory for the
 *                    snapshot.
 */
nsresult internal_GetHistogramAndSamples(const StaticMutexAutoLock& aLock,
                                         const base::Histogram* h,
                                         HistogramSnapshotData& aSnapshot) {
  MOZ_ASSERT(h);

  // Convert the ranges of the buckets to a nsTArray.
  const size_t bucketCount = h->bucket_count();
  for (size_t i = 0; i < bucketCount; i++) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier, or change the return type to void.
    aSnapshot.mBucketRanges.AppendElement(h->ranges(i));
  }

  // Get a snapshot of the samples.
  base::Histogram::SampleSet ss = h->SnapshotSample();

  // Get the number of samples in each bucket.
  for (size_t i = 0; i < bucketCount; i++) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier, or change the return type to void.
    aSnapshot.mBucketCounts.AppendElement(ss.counts(i));
  }

  // Finally, save the |sum|. We don't need to reflect declared_min,
  // declared_max and histogram_type as they are in gHistogramInfo.
  aSnapshot.mSampleSum = ss.sum();
  return NS_OK;
}

/**
 * Reflect a histogram snapshot into a JavaScript object.
 * The returned histogram object will have the following properties:
 *
 *   bucket_count - Number of buckets of this histogram
 *   histogram_type - HISTOGRAM_EXPONENTIAL, HISTOGRAM_LINEAR,
 * HISTOGRAM_BOOLEAN, HISTOGRAM_FLAG, HISTOGRAM_COUNT, or HISTOGRAM_CATEGORICAL
 *   sum - sum of the bucket contents
 *   range - A 2-item array of minimum and maximum bucket size
 *   values - Map from bucket start to the bucket's count
 */
nsresult internal_ReflectHistogramAndSamples(
    JSContext* cx, JS::Handle<JSObject*> obj,
    const HistogramInfo& aHistogramInfo,
    const HistogramSnapshotData& aSnapshot) {
  if (!(JS_DefineProperty(cx, obj, "bucket_count", aHistogramInfo.bucketCount,
                          JSPROP_ENUMERATE) &&
        JS_DefineProperty(cx, obj, "histogram_type",
                          aHistogramInfo.histogramType, JSPROP_ENUMERATE) &&
        JS_DefineProperty(cx, obj, "sum", double(aSnapshot.mSampleSum),
                          JSPROP_ENUMERATE))) {
    return NS_ERROR_FAILURE;
  }

  // Don't rely on the bucket counts from "aHistogramInfo": it may
  // differ from the length of aSnapshot.mBucketCounts due to expired
  // histograms.
  const size_t count = aSnapshot.mBucketCounts.Length();
  MOZ_ASSERT(count == aSnapshot.mBucketRanges.Length(),
             "The number of buckets and the number of counts must match.");

  // Create the "range" property and add it to the final object.
  JS::Rooted<JSObject*> rarray(cx, JS::NewArrayObject(cx, 2));
  if (rarray == nullptr ||
      !JS_DefineProperty(cx, obj, "range", rarray, JSPROP_ENUMERATE)) {
    return NS_ERROR_FAILURE;
  }
  // Add [min, max] into the range array
  if (!JS_DefineElement(cx, rarray, 0, aHistogramInfo.min, JSPROP_ENUMERATE)) {
    return NS_ERROR_FAILURE;
  }
  if (!JS_DefineElement(cx, rarray, 1, aHistogramInfo.max, JSPROP_ENUMERATE)) {
    return NS_ERROR_FAILURE;
  }

  JS::Rooted<JSObject*> values(cx, JS_NewPlainObject(cx));
  if (values == nullptr ||
      !JS_DefineProperty(cx, obj, "values", values, JSPROP_ENUMERATE)) {
    return NS_ERROR_FAILURE;
  }

  bool first = true;
  size_t last = 0;

  for (size_t i = 0; i < count; i++) {
    auto value = aSnapshot.mBucketCounts[i];
    if (value == 0) {
      continue;
    }

    if (i > 0 && first) {
      auto range = aSnapshot.mBucketRanges[i - 1];
      if (!JS_DefineProperty(cx, values, nsPrintfCString("%d", range).get(), 0,
                             JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }

    first = false;
    last = i + 1;

    auto range = aSnapshot.mBucketRanges[i];
    if (!JS_DefineProperty(cx, values, nsPrintfCString("%d", range).get(),
                           value, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }

  if (last > 0 && last < count) {
    auto range = aSnapshot.mBucketRanges[last];
    if (!JS_DefineProperty(cx, values, nsPrintfCString("%d", range).get(), 0,
                           JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}

bool internal_ShouldReflectHistogram(const StaticMutexAutoLock& aLock,
                                     base::Histogram* h, HistogramID id) {
  // Only flag histograms are serialized when they are empty.
  // This has historical reasons, changing this will require downstream changes.
  // The cheaper path here is to just deprecate flag histograms in favor
  // of scalars.
  uint32_t type = gHistogramInfos[id].histogramType;
  if (internal_IsEmpty(aLock, h) && type != nsITelemetry::HISTOGRAM_FLAG) {
    return false;
  }

  // Don't reflect the histogram if it's not allowed in this product.
  if (!CanRecordProduct(gHistogramInfos[id].products)) {
    return false;
  }

  return true;
}

/**
 * Helper function to get a snapshot of the histograms.
 *
 * @param {aLock} the lock proof.
 * @param {aStore} the name of the store to snapshot.
 * @param {aDataset} the dataset for which the snapshot is being requested.
 * @param {aClearSubsession} whether or not to clear the data after
 *        taking the snapshot.
 * @param {aIncludeGPU} whether or not to include data for the GPU.
 * @param {aOutSnapshot} the container in which the snapshot data will be
 *                       stored.
 * @return {nsresult} NS_OK if the snapshot was successfully taken or
 *         NS_ERROR_OUT_OF_MEMORY if it failed to allocate memory.
 */
nsresult internal_GetHistogramsSnapshot(
    const StaticMutexAutoLock& aLock, const nsACString& aStore,
    unsigned int aDataset, bool aClearSubsession, bool aIncludeGPU,
    bool aFilterTest, HistogramProcessSnapshotsArray& aOutSnapshot) {
  if (!aOutSnapshot.resize(static_cast<uint32_t>(ProcessID::Count))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t process = 0; process < static_cast<uint32_t>(ProcessID::Count);
       ++process) {
    HistogramSnapshotsArray& hArray = aOutSnapshot[process];

    for (size_t i = 0; i < HistogramCount; ++i) {
      const HistogramInfo& info = gHistogramInfos[i];
      if (info.keyed) {
        continue;
      }

      HistogramID id = HistogramID(i);

      if (!CanRecordInProcess(info.record_in_processes, ProcessID(process)) ||
          ((ProcessID(process) == ProcessID::Gpu) && !aIncludeGPU)) {
        continue;
      }

      if (!IsInDataset(info.dataset, aDataset)) {
        continue;
      }

      bool shouldInstantiate =
          info.histogramType == nsITelemetry::HISTOGRAM_FLAG;
      Histogram* w = internal_GetHistogramById(aLock, id, ProcessID(process),
                                               shouldInstantiate);
      if (!w || w->IsExpired()) {
        continue;
      }

      base::Histogram* h = nullptr;
      if (!w->GetHistogram(aStore, &h)) {
        continue;
      }

      if (!internal_ShouldReflectHistogram(aLock, h, id)) {
        continue;
      }

      const char* name = info.name();
      if (aFilterTest && strncmp(TEST_HISTOGRAM_PREFIX, name,
                                 strlen(TEST_HISTOGRAM_PREFIX)) == 0) {
        if (aClearSubsession) {
          h->Clear();
        }
        continue;
      }

      HistogramSnapshotData snapshotData;
      if (NS_FAILED(internal_GetHistogramAndSamples(aLock, h, snapshotData))) {
        continue;
      }

      if (!hArray.emplaceBack(HistogramSnapshotInfo{snapshotData, id})) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      if (aClearSubsession) {
        h->Clear();
      }
    }
  }
  return NS_OK;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: class Histogram

namespace {

Histogram::Histogram(HistogramID histogramId, const HistogramInfo& info,
                     bool expired)
    : mSingleStore(nullptr), mIsExpired(expired) {
  if (IsExpired()) {
    return;
  }

  const int bucketsOffset = gHistogramBucketLowerBoundIndex[histogramId];

  if (info.is_single_store()) {
    mSingleStore = internal_CreateBaseHistogramInstance(info, bucketsOffset);
  } else {
    for (uint32_t i = 0; i < info.store_count; i++) {
      auto store = nsDependentCString(
          &gHistogramStringTable[gHistogramStoresTable[info.store_index + i]]);
      mStorage.InsertOrUpdate(store, UniquePtr<base::Histogram>(
                                         internal_CreateBaseHistogramInstance(
                                             info, bucketsOffset)));
    }
  }
}

Histogram::~Histogram() { delete mSingleStore; }

void Histogram::Add(uint32_t sample) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Only add to histograms in the parent process");
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (IsExpired()) {
    return;
  }

  if (mSingleStore != nullptr) {
    mSingleStore->Add(sample);
  } else {
    for (auto iter = mStorage.Iter(); !iter.Done(); iter.Next()) {
      auto& h = iter.Data();
      h->Add(sample);
    }
  }
}

void Histogram::Clear(const nsACString& store) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Only clear histograms in the parent process");
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (mSingleStore != nullptr) {
    if (store.EqualsASCII("main")) {
      mSingleStore->Clear();
    }
  } else {
    base::Histogram* h = nullptr;
    bool found = GetHistogram(store, &h);
    if (!found) {
      return;
    }
    MOZ_ASSERT(h, "Should have found a valid histogram in the named store");

    h->Clear();
  }
}

bool Histogram::GetHistogram(const nsACString& store, base::Histogram** h) {
  MOZ_ASSERT(!IsExpired());
  if (IsExpired()) {
    return false;
  }

  if (mSingleStore != nullptr) {
    if (store.EqualsASCII("main")) {
      *h = mSingleStore;
      return true;
    }

    return false;
  }

  return mStorage.Get(store, h);
}

size_t Histogram::SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
  size_t n = 0;
  n += aMallocSizeOf(this);
  /*
   * In theory mStorage.SizeOfExcludingThis should included the data part of the
   * map, but the numbers seemed low, so we are only taking the shallow size and
   * do the iteration here.
   */
  n += mStorage.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& h : mStorage.Values()) {
    n += h->SizeOfIncludingThis(aMallocSizeOf);
  }
  if (mSingleStore != nullptr) {
    // base::Histogram doesn't have SizeOfExcludingThis, so we are overcounting
    // the pointer here.
    n += mSingleStore->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: class KeyedHistogram and internal_ReflectKeyedHistogram

namespace {

nsresult internal_ReflectKeyedHistogram(
    const KeyedHistogramSnapshotData& aSnapshot, const HistogramInfo& info,
    JSContext* aCx, JS::Handle<JSObject*> aObj) {
  for (const auto& entry : aSnapshot) {
    const HistogramSnapshotData& keyData = entry.GetData();

    JS::Rooted<JSObject*> histogramSnapshot(aCx, JS_NewPlainObject(aCx));
    if (!histogramSnapshot) {
      return NS_ERROR_FAILURE;
    }

    if (NS_FAILED(internal_ReflectHistogramAndSamples(aCx, histogramSnapshot,
                                                      info, keyData))) {
      return NS_ERROR_FAILURE;
    }

    const NS_ConvertUTF8toUTF16 key(entry.GetKey());
    if (!JS_DefineUCProperty(aCx, aObj, key.Data(), key.Length(),
                             histogramSnapshot, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}

KeyedHistogram::KeyedHistogram(HistogramID id, const HistogramInfo& info,
                               bool expired)
    : mSingleStore(nullptr),
      mId(id),
      mHistogramInfo(info),
      mIsExpired(expired) {
  if (IsExpired()) {
    return;
  }

  if (info.is_single_store()) {
    mSingleStore = new KeyedHistogramMapType;
  } else {
    for (uint32_t i = 0; i < info.store_count; i++) {
      auto store = nsDependentCString(
          &gHistogramStringTable[gHistogramStoresTable[info.store_index + i]]);
      mStorage.InsertOrUpdate(store, MakeUnique<KeyedHistogramMapType>());
    }
  }
}

KeyedHistogram::~KeyedHistogram() { delete mSingleStore; }

nsresult KeyedHistogram::GetHistogram(const nsCString& aStore,
                                      const nsCString& key,
                                      base::Histogram** histogram) {
  if (IsExpired()) {
    MOZ_ASSERT(false,
               "KeyedHistogram::GetHistogram called on an expired histogram.");
    return NS_ERROR_FAILURE;
  }

  KeyedHistogramMapType* histogramMap;
  bool found;

  if (mSingleStore != nullptr) {
    histogramMap = mSingleStore;
  } else {
    found = mStorage.Get(aStore, &histogramMap);
    if (!found) {
      return NS_ERROR_FAILURE;
    }
  }

  found = histogramMap->Get(key, histogram);
  if (found) {
    return NS_OK;
  }

  int bucketsOffset = gHistogramBucketLowerBoundIndex[mId];
  auto h = UniquePtr<base::Histogram>{
      internal_CreateBaseHistogramInstance(mHistogramInfo, bucketsOffset)};
  if (!h) {
    return NS_ERROR_FAILURE;
  }

  h->ClearFlags(base::Histogram::kUmaTargetedHistogramFlag);
  *histogram = h.get();

  bool inserted =
      histogramMap->InsertOrUpdate(key, std::move(h), mozilla::fallible);
  if (MOZ_UNLIKELY(!inserted)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

base::Histogram* KeyedHistogram::GetHistogram(const nsCString& aStore,
                                              const nsCString& key) {
  base::Histogram* h = nullptr;
  if (NS_FAILED(GetHistogram(aStore, key, &h))) {
    return nullptr;
  }
  return h;
}

nsresult KeyedHistogram::Add(const nsCString& key, uint32_t sample,
                             ProcessID aProcessType) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Only add to keyed histograms in the parent process");
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  bool canRecordDataset =
      CanRecordDataset(mHistogramInfo.dataset, internal_CanRecordBase(),
                       internal_CanRecordExtended());
  // If `histogram` is a non-parent-process histogram, then recording-enabled
  // has been checked in its owner process.
  if (!canRecordDataset || (aProcessType == ProcessID::Parent &&
                            !internal_IsRecordingEnabled(mId))) {
    return NS_OK;
  }

  // Don't record if expired.
  if (IsExpired()) {
    return NS_OK;
  }

  // Don't record if the current platform is not enabled
  if (!CanRecordProduct(gHistogramInfos[mId].products)) {
    return NS_OK;
  }

  // The internal representation of a base::Histogram's buckets uses `int`.
  // Clamp large values of `sample` to be INT_MAX so they continue to be treated
  // as large values (instead of negative ones).
  if (sample > INT_MAX) {
    TelemetryScalar::Add(
        mozilla::Telemetry::ScalarID::TELEMETRY_ACCUMULATE_CLAMPED_VALUES,
        NS_ConvertASCIItoUTF16(mHistogramInfo.name()), 1);
    sample = INT_MAX;
  }

  base::Histogram* histogram;
  if (mSingleStore != nullptr) {
    histogram = GetHistogram("main"_ns, key);
    if (!histogram) {
      MOZ_ASSERT(false, "Missing histogram in single store.");
      return NS_ERROR_FAILURE;
    }

    histogram->Add(sample);
  } else {
    for (uint32_t i = 0; i < mHistogramInfo.store_count; i++) {
      auto store = nsDependentCString(
          &gHistogramStringTable
              [gHistogramStoresTable[mHistogramInfo.store_index + i]]);
      base::Histogram* histogram = GetHistogram(store, key);
      MOZ_ASSERT(histogram);
      if (histogram) {
        histogram->Add(sample);
      } else {
        return NS_ERROR_FAILURE;
      }
    }
  }

  return NS_OK;
}

void KeyedHistogram::Clear(const nsACString& aStore) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Only clear keyed histograms in the parent process");
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (IsExpired()) {
    return;
  }

  if (mSingleStore) {
    if (aStore.EqualsASCII("main")) {
      mSingleStore->Clear();
    }
    return;
  }

  KeyedHistogramMapType* histogramMap;
  bool found = mStorage.Get(aStore, &histogramMap);
  if (!found) {
    return;
  }

  histogramMap->Clear();
}

bool KeyedHistogram::IsEmpty(const nsACString& aStore) const {
  if (mSingleStore != nullptr) {
    if (aStore.EqualsASCII("main")) {
      return mSingleStore->IsEmpty();
    }

    return true;
  }

  KeyedHistogramMapType* histogramMap;
  bool found = mStorage.Get(aStore, &histogramMap);
  if (!found) {
    return true;
  }
  return histogramMap->IsEmpty();
}

size_t KeyedHistogram::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  size_t n = 0;
  n += aMallocSizeOf(this);
  /*
   * In theory mStorage.SizeOfExcludingThis should included the data part of the
   * map, but the numbers seemed low, so we are only taking the shallow size and
   * do the iteration here.
   */
  n += mStorage.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& h : mStorage.Values()) {
    n += h->SizeOfIncludingThis(aMallocSizeOf);
  }
  if (mSingleStore != nullptr) {
    // base::Histogram doesn't have SizeOfExcludingThis, so we are overcounting
    // the pointer here.
    n += mSingleStore->SizeOfExcludingThis(aMallocSizeOf);
  }
  return n;
}

nsresult KeyedHistogram::GetKeys(const StaticMutexAutoLock& aLock,
                                 const nsCString& store,
                                 nsTArray<nsCString>& aKeys) {
  KeyedHistogramMapType* histogramMap;
  if (mSingleStore != nullptr) {
    histogramMap = mSingleStore;
  } else {
    bool found = mStorage.Get(store, &histogramMap);
    if (!found) {
      return NS_ERROR_FAILURE;
    }
  }

  if (!aKeys.SetCapacity(histogramMap->Count(), mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (const auto& key : histogramMap->Keys()) {
    if (!aKeys.AppendElement(key, mozilla::fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return NS_OK;
}

nsresult KeyedHistogram::GetJSSnapshot(JSContext* cx, JS::Handle<JSObject*> obj,
                                       const nsACString& aStore,
                                       bool clearSubsession) {
  // Get a snapshot of the data.
  KeyedHistogramSnapshotData dataSnapshot;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    MOZ_ASSERT(internal_IsHistogramEnumId(mId));

    // Take a snapshot of the data here, protected by the lock, and then,
    // outside of the lock protection, mirror it to a JS structure.
    nsresult rv = GetSnapshot(locker, aStore, dataSnapshot, clearSubsession);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Now that we have a copy of the data, mirror it to JS.
  return internal_ReflectKeyedHistogram(dataSnapshot, gHistogramInfos[mId], cx,
                                        obj);
}

/**
 * Return a histogram snapshot for the named store.
 *
 * If getting the snapshot succeeds, NS_OK is returned and `aSnapshot` contains
 * the snapshot data. If the histogram is not available in the named store,
 * NS_ERROR_NO_CONTENT is returned. For other errors, NS_ERROR_FAILURE is
 * returned.
 */
nsresult KeyedHistogram::GetSnapshot(const StaticMutexAutoLock& aLock,
                                     const nsACString& aStore,
                                     KeyedHistogramSnapshotData& aSnapshot,
                                     bool aClearSubsession) {
  KeyedHistogramMapType* histogramMap;
  if (mSingleStore != nullptr) {
    if (!aStore.EqualsASCII("main")) {
      return NS_ERROR_NO_CONTENT;
    }

    histogramMap = mSingleStore;
  } else {
    bool found = mStorage.Get(aStore, &histogramMap);
    if (!found) {
      // Nothing in the main store is fine, it's just handled as empty
      return NS_ERROR_NO_CONTENT;
    }
  }

  // Snapshot every key.
  for (const auto& entry : *histogramMap) {
    base::Histogram* keyData = entry.GetWeak();
    if (!keyData) {
      return NS_ERROR_FAILURE;
    }

    HistogramSnapshotData keySnapshot;
    if (NS_FAILED(
            internal_GetHistogramAndSamples(aLock, keyData, keySnapshot))) {
      return NS_ERROR_FAILURE;
    }

    // Append to the final snapshot.
    aSnapshot.InsertOrUpdate(entry.GetKey(), std::move(keySnapshot));
  }

  if (aClearSubsession) {
    Clear(aStore);
  }

  return NS_OK;
}

/**
 * Helper function to get a snapshot of the keyed histograms.
 *
 * @param {aLock} the lock proof.
 * @param {aDataset} the dataset for which the snapshot is being requested.
 * @param {aClearSubsession} whether or not to clear the data after
 *        taking the snapshot.
 * @param {aIncludeGPU} whether or not to include data for the GPU.
 * @param {aOutSnapshot} the container in which the snapshot data will be
 *                       stored.
 * @return {nsresult} NS_OK if the snapshot was successfully taken or
 *         NS_ERROR_OUT_OF_MEMORY if it failed to allocate memory.
 */
nsresult internal_GetKeyedHistogramsSnapshot(
    const StaticMutexAutoLock& aLock, const nsACString& aStore,
    unsigned int aDataset, bool aClearSubsession, bool aIncludeGPU,
    bool aFilterTest, KeyedHistogramProcessSnapshotsArray& aOutSnapshot) {
  if (!aOutSnapshot.resize(static_cast<uint32_t>(ProcessID::Count))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t process = 0; process < static_cast<uint32_t>(ProcessID::Count);
       ++process) {
    KeyedHistogramSnapshotsArray& hArray = aOutSnapshot[process];

    for (size_t i = 0; i < HistogramCount; ++i) {
      HistogramID id = HistogramID(i);
      const HistogramInfo& info = gHistogramInfos[id];
      if (!info.keyed) {
        continue;
      }

      if (!CanRecordInProcess(info.record_in_processes, ProcessID(process)) ||
          ((ProcessID(process) == ProcessID::Gpu) && !aIncludeGPU)) {
        continue;
      }

      if (!IsInDataset(info.dataset, aDataset)) {
        continue;
      }

      KeyedHistogram* keyed =
          internal_GetKeyedHistogramById(id, ProcessID(process),
                                         /* instantiate = */ false);
      if (!keyed || keyed->IsEmpty(aStore) || keyed->IsExpired()) {
        continue;
      }

      const char* name = info.name();
      if (aFilterTest && strncmp(TEST_HISTOGRAM_PREFIX, name,
                                 strlen(TEST_HISTOGRAM_PREFIX)) == 0) {
        if (aClearSubsession) {
          keyed->Clear(aStore);
        }
        continue;
      }

      // Take a snapshot of the keyed histogram data!
      KeyedHistogramSnapshotData snapshot;
      if (!NS_SUCCEEDED(
              keyed->GetSnapshot(aLock, aStore, snapshot, aClearSubsession))) {
        return NS_ERROR_FAILURE;
      }

      if (!hArray.emplaceBack(
              KeyedHistogramSnapshotInfo{std::move(snapshot), id})) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
  }
  return NS_OK;
}

}  // namespace

namespace geckoprofiler::markers {

struct HistogramMarker {
  static constexpr mozilla::Span<const char> MarkerTypeName() {
    return mozilla::MakeStringSpan("Hist");
  }
  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      mozilla::Telemetry::HistogramID aId, const nsCString& key,
      uint32_t aSample) {
    aWriter.UniqueStringProperty(
        "id", mozilla::MakeStringSpan(GetHistogramName(aId)));
    if (!key.IsEmpty()) {
      aWriter.StringProperty("key", key);
    }
    aWriter.IntProperty("val", aSample);
  }
  using MS = mozilla::MarkerSchema;
  static MS MarkerTypeDisplay() {
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyLabelFormatSearchable("id", "Histogram Name",
                                       MS::Format::UniqueString,
                                       MS::Searchable::Searchable);
    schema.AddKeyLabelFormat("key", "Key", MS::Format::String);
    schema.AddKeyLabelFormat("val", "Sample", MS::Format::Integer);
    schema.SetTooltipLabel(
        "{marker.data.id}[{marker.data.key}] {marker.data.val}");
    schema.SetTableLabel(
        "{marker.name} - {marker.data.id}[{marker.data.key}]: "
        "{marker.data.val}");
    return schema;
  }
};

}  // namespace geckoprofiler::markers

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: thread-unsafe helpers for the external interface

namespace {

bool internal_RemoteAccumulate(const StaticMutexAutoLock& aLock,
                               HistogramID aId, uint32_t aSample) {
  if (XRE_IsParentProcess()) {
    return false;
  }

  if (!internal_IsRecordingEnabled(aId)) {
    return true;
  }

  PROFILER_MARKER("Histogram::Add", TELEMETRY, {}, HistogramMarker, aId,
                  EmptyCString(), aSample);
  TelemetryIPCAccumulator::AccumulateChildHistogram(aId, aSample);
  return true;
}

bool internal_RemoteAccumulate(const StaticMutexAutoLock& aLock,
                               HistogramID aId, const nsCString& aKey,
                               uint32_t aSample) {
  if (XRE_IsParentProcess()) {
    return false;
  }

  if (!internal_IsRecordingEnabled(aId)) {
    return true;
  }

  PROFILER_MARKER("Histogram::Add", TELEMETRY, {}, HistogramMarker, aId, aKey,
                  aSample);
  TelemetryIPCAccumulator::AccumulateChildKeyedHistogram(aId, aKey, aSample);
  return true;
}

void internal_Accumulate(const StaticMutexAutoLock& aLock, HistogramID aId,
                         uint32_t aSample) {
  if (!internal_CanRecordBase() ||
      internal_RemoteAccumulate(aLock, aId, aSample)) {
    return;
  }

  PROFILER_MARKER("Histogram::Add", TELEMETRY, {}, HistogramMarker, aId,
                  EmptyCString(), aSample);
  Histogram* w = internal_GetHistogramById(aLock, aId, ProcessID::Parent);
  MOZ_ASSERT(w);
  internal_HistogramAdd(aLock, *w, aId, aSample, ProcessID::Parent);
}

void internal_Accumulate(const StaticMutexAutoLock& aLock, HistogramID aId,
                         const nsCString& aKey, uint32_t aSample) {
  if (!gTelemetryHistogramInitDone || !internal_CanRecordBase() ||
      internal_RemoteAccumulate(aLock, aId, aKey, aSample)) {
    return;
  }

  PROFILER_MARKER("Histogram::Add", TELEMETRY, {}, HistogramMarker, aId, aKey,
                  aSample);
  KeyedHistogram* keyed =
      internal_GetKeyedHistogramById(aId, ProcessID::Parent);
  MOZ_ASSERT(keyed);
  keyed->Add(aKey, aSample, ProcessID::Parent);
}

void internal_AccumulateChild(const StaticMutexAutoLock& aLock,
                              ProcessID aProcessType, HistogramID aId,
                              uint32_t aSample) {
  if (!internal_CanRecordBase()) {
    return;
  }

  PROFILER_MARKER("ChildHistogram::Add", TELEMETRY, {}, HistogramMarker, aId,
                  EmptyCString(), aSample);
  Histogram* w = internal_GetHistogramById(aLock, aId, aProcessType);
  if (w == nullptr) {
    NS_WARNING("Failed GetHistogramById for CHILD");
  } else {
    internal_HistogramAdd(aLock, *w, aId, aSample, aProcessType);
  }
}

void internal_AccumulateChildKeyed(const StaticMutexAutoLock& aLock,
                                   ProcessID aProcessType, HistogramID aId,
                                   const nsCString& aKey, uint32_t aSample) {
  if (!gTelemetryHistogramInitDone || !internal_CanRecordBase()) {
    return;
  }

  PROFILER_MARKER("ChildHistogram::Add", TELEMETRY, {}, HistogramMarker, aId,
                  aKey, aSample);
  KeyedHistogram* keyed = internal_GetKeyedHistogramById(aId, aProcessType);
  MOZ_ASSERT(keyed);
  keyed->Add(aKey, aSample, aProcessType);
}

void internal_ClearHistogram(const StaticMutexAutoLock& aLock, HistogramID id,
                             const nsACString& aStore) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!XRE_IsParentProcess()) {
    return;
  }

  // Handle keyed histograms.
  if (gHistogramInfos[id].keyed) {
    for (uint32_t process = 0;
         process < static_cast<uint32_t>(ProcessID::Count); ++process) {
      KeyedHistogram* kh = internal_GetKeyedHistogramById(
          id, static_cast<ProcessID>(process), /* instantiate = */ false);
      if (kh) {
        kh->Clear(aStore);
      }
    }
  } else {
    // Reset the histograms instances for all processes.
    for (uint32_t process = 0;
         process < static_cast<uint32_t>(ProcessID::Count); ++process) {
      Histogram* h =
          internal_GetHistogramById(aLock, id, static_cast<ProcessID>(process),
                                    /* instantiate = */ false);
      if (h) {
        h->Clear(aStore);
      }
    }
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: JSHistogram_* functions

// NOTE: the functions in this section:
//
//   internal_JSHistogram_Name
//   internal_JSHistogram_Snapshot
//   internal_JSHistogram_Clear
//   internal_WrapAndReturnHistogram
//
// all run without protection from |gTelemetryHistogramMutex|.  If they
// held |gTelemetryHistogramMutex|, there would be the possibility of
// deadlock because the JS_ calls that they make may call back into the
// TelemetryHistogram interface, hence trying to re-acquire the mutex.
//
// This means that these functions potentially race against threads, but
// that seems preferable to risking deadlock.

namespace {

static constexpr uint32_t HistogramObjectDataSlot = 0;
static constexpr uint32_t HistogramObjectSlotCount =
    HistogramObjectDataSlot + 1;

void internal_JSHistogram_finalize(JS::GCContext*, JSObject*);

static const JSClassOps sJSHistogramClassOps = {nullptr, /* addProperty */
                                                nullptr, /* delProperty */
                                                nullptr, /* enumerate */
                                                nullptr, /* newEnumerate */
                                                nullptr, /* resolve */
                                                nullptr, /* mayResolve */
                                                internal_JSHistogram_finalize};

static const JSClass sJSHistogramClass = {
    "JSHistogram", /* name */
    JSCLASS_HAS_RESERVED_SLOTS(HistogramObjectSlotCount) |
        JSCLASS_FOREGROUND_FINALIZE, /* flags */
    &sJSHistogramClassOps};

struct JSHistogramData {
  HistogramID histogramId;
};

bool internal_JSHistogram_CoerceValue(JSContext* aCx,
                                      JS::Handle<JS::Value> aElement,
                                      HistogramID aId, uint32_t aHistogramType,
                                      uint32_t& aValue) {
  if (aElement.isString()) {
    // Strings only allowed for categorical histograms
    if (aHistogramType != nsITelemetry::HISTOGRAM_CATEGORICAL) {
      LogToBrowserConsole(
          nsIScriptError::errorFlag,
          nsLiteralString(
              u"String argument only allowed for categorical histogram"));
      return false;
    }

    // Label is given by the string argument
    nsAutoJSString label;
    if (!label.init(aCx, aElement)) {
      LogToBrowserConsole(nsIScriptError::errorFlag,
                          u"Invalid string parameter"_ns);
      return false;
    }

    // Get the label id for accumulation
    nsresult rv = gHistogramInfos[aId].label_id(
        NS_ConvertUTF16toUTF8(label).get(), &aValue);
    if (NS_FAILED(rv)) {
      nsPrintfCString msg("'%s' is an invalid string label",
                          NS_ConvertUTF16toUTF8(label).get());
      LogToBrowserConsole(nsIScriptError::errorFlag,
                          NS_ConvertUTF8toUTF16(msg));
      return false;
    }
  } else if (!(aElement.isNumber() || aElement.isBoolean())) {
    LogToBrowserConsole(nsIScriptError::errorFlag, u"Argument not a number"_ns);
    return false;
  } else if (aElement.isNumber() && aElement.toNumber() > UINT32_MAX) {
    // Clamp large numerical arguments to aValue's acceptable values.
    // JS::ToUint32 will take aElement modulo 2^32 before returning it, which
    // may result in a smaller final value.
    aValue = UINT32_MAX;
#ifdef DEBUG
    LogToBrowserConsole(nsIScriptError::errorFlag,
                        u"Clamped large numeric value"_ns);
#endif
  } else if (!JS::ToUint32(aCx, aElement, &aValue)) {
    LogToBrowserConsole(nsIScriptError::errorFlag,
                        u"Failed to convert element to UInt32"_ns);
    return false;
  }

  // If we're here then all type checks have passed and aValue contains the
  // coerced integer
  return true;
}

bool internal_JSHistogram_GetValueArray(JSContext* aCx, JS::CallArgs& args,
                                        uint32_t aHistogramType,
                                        HistogramID aId, bool isKeyed,
                                        nsTArray<uint32_t>& aArray) {
  // This function populates aArray with the values extracted from args. Handles
  // keyed and non-keyed histograms, and single and array of values. Also
  // performs sanity checks on the arguments. Returns true upon successful
  // population, false otherwise.

  uint32_t firstArgIndex = 0;
  if (isKeyed) {
    firstArgIndex = 1;
  }

  // Special case of no argument (or only key) and count histogram
  if (args.length() == firstArgIndex) {
    if (!(aHistogramType == nsITelemetry::HISTOGRAM_COUNT)) {
      LogToBrowserConsole(
          nsIScriptError::errorFlag,
          nsLiteralString(
              u"Need at least one argument for non count type histogram"));
      return false;
    }

    aArray.AppendElement(1);
    return true;
  }

  if (args[firstArgIndex].isObject() && !args[firstArgIndex].isString()) {
    JS::Rooted<JSObject*> arrayObj(aCx, &args[firstArgIndex].toObject());

    bool isArray = false;
    JS::IsArrayObject(aCx, arrayObj, &isArray);

    if (!isArray) {
      LogToBrowserConsole(
          nsIScriptError::errorFlag,
          nsLiteralString(
              u"The argument to accumulate can't be a non-array object"));
      return false;
    }

    uint32_t arrayLength = 0;
    if (!JS::GetArrayLength(aCx, arrayObj, &arrayLength)) {
      LogToBrowserConsole(nsIScriptError::errorFlag,
                          u"Failed while trying to get array length"_ns);
      return false;
    }

    for (uint32_t arrayIdx = 0; arrayIdx < arrayLength; arrayIdx++) {
      JS::Rooted<JS::Value> element(aCx);

      if (!JS_GetElement(aCx, arrayObj, arrayIdx, &element)) {
        nsPrintfCString msg("Failed while trying to get element at index %d",
                            arrayIdx);
        LogToBrowserConsole(nsIScriptError::errorFlag,
                            NS_ConvertUTF8toUTF16(msg));
        return false;
      }

      uint32_t value = 0;
      if (!internal_JSHistogram_CoerceValue(aCx, element, aId, aHistogramType,
                                            value)) {
        nsPrintfCString msg("Element at index %d failed type checks", arrayIdx);
        LogToBrowserConsole(nsIScriptError::errorFlag,
                            NS_ConvertUTF8toUTF16(msg));
        return false;
      }
      aArray.AppendElement(value);
    }

    return true;
  }

  uint32_t value = 0;
  if (!internal_JSHistogram_CoerceValue(aCx, args[firstArgIndex], aId,
                                        aHistogramType, value)) {
    return false;
  }
  aArray.AppendElement(value);
  return true;
}

static JSHistogramData* GetJSHistogramData(JSObject* obj) {
  MOZ_ASSERT(JS::GetClass(obj) == &sJSHistogramClass);
  return JS::GetMaybePtrFromReservedSlot<JSHistogramData>(
      obj, HistogramObjectDataSlot);
}

bool internal_JSHistogram_Name(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;
  MOZ_ASSERT(internal_IsHistogramEnumId(id));
  const char* name = gHistogramInfos[id].name();

  auto cname = NS_ConvertASCIItoUTF16(name);
  args.rval().setString(ToJSString(cx, cname));

  return true;
}

/**
 * Extract the store name from JavaScript function arguments.
 * The first and only argument needs to be an object with a "store" property.
 * If no arguments are given it defaults to "main".
 */
nsresult internal_JS_StoreFromObjectArgument(JSContext* cx,
                                             const JS::CallArgs& args,
                                             nsAutoString& aStoreName) {
  if (args.length() == 0) {
    aStoreName.AssignLiteral("main");
  } else if (args.length() == 1) {
    if (!args[0].isObject()) {
      JS_ReportErrorASCII(cx, "Expected object argument.");
      return NS_ERROR_FAILURE;
    }

    JS::Rooted<JS::Value> storeValue(cx);
    JS::Rooted<JSObject*> argsObject(cx, &args[0].toObject());
    if (!JS_GetProperty(cx, argsObject, "store", &storeValue)) {
      JS_ReportErrorASCII(cx,
                          "Expected object argument to have property 'store'.");
      return NS_ERROR_FAILURE;
    }

    nsAutoJSString store;
    if (!storeValue.isString() || !store.init(cx, storeValue)) {
      JS_ReportErrorASCII(
          cx, "Expected object argument's 'store' property to be a string.");
      return NS_ERROR_FAILURE;
    }

    aStoreName.Assign(store);
  } else {
    JS_ReportErrorASCII(cx, "Expected at most one argument.");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool internal_JSHistogram_Snapshot(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!XRE_IsParentProcess()) {
    JS_ReportErrorASCII(
        cx, "Histograms can only be snapshotted in the parent process");
    return false;
  }

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;

  nsAutoString storeName;
  nsresult rv = internal_JS_StoreFromObjectArgument(cx, args, storeName);
  if (NS_FAILED(rv)) {
    return false;
  }

  HistogramSnapshotData dataSnapshot;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    MOZ_ASSERT(internal_IsHistogramEnumId(id));

    // This is not good standard behavior given that we have histogram instances
    // covering multiple processes.
    // However, changing this requires some broader changes to callers.
    Histogram* w = internal_GetHistogramById(locker, id, ProcessID::Parent);
    base::Histogram* h = nullptr;
    if (!w->GetHistogram(NS_ConvertUTF16toUTF8(storeName), &h)) {
      // When it's not in the named store, let's skip the snapshot completely,
      // but don't fail
      args.rval().setUndefined();
      return true;
    }
    // Take a snapshot of the data here, protected by the lock, and then,
    // outside of the lock protection, mirror it to a JS structure
    if (NS_FAILED(internal_GetHistogramAndSamples(locker, h, dataSnapshot))) {
      return false;
    }
  }

  JS::Rooted<JSObject*> snapshot(cx, JS_NewPlainObject(cx));
  if (!snapshot) {
    return false;
  }

  if (NS_FAILED(internal_ReflectHistogramAndSamples(
          cx, snapshot, gHistogramInfos[id], dataSnapshot))) {
    return false;
  }

  args.rval().setObject(*snapshot);
  return true;
}

bool internal_JSHistogram_Clear(JSContext* cx, unsigned argc, JS::Value* vp) {
  if (!XRE_IsParentProcess()) {
    JS_ReportErrorASCII(cx,
                        "Histograms can only be cleared in the parent process");
    return false;
  }

  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSHistogramData(obj);
  MOZ_ASSERT(data);

  nsAutoString storeName;
  nsresult rv = internal_JS_StoreFromObjectArgument(cx, args, storeName);
  if (NS_FAILED(rv)) {
    return false;
  }

  // This function should always return |undefined| and never fail but
  // rather report failures using the console.
  args.rval().setUndefined();

  HistogramID id = data->histogramId;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);

    MOZ_ASSERT(internal_IsHistogramEnumId(id));
    internal_ClearHistogram(locker, id, NS_ConvertUTF16toUTF8(storeName));
  }

  return true;
}

// NOTE: Runs without protection from |gTelemetryHistogramMutex|.
// See comment at the top of this section.
nsresult internal_WrapAndReturnHistogram(HistogramID id, JSContext* cx,
                                         JS::MutableHandle<JS::Value> ret) {
  JS::Rooted<JSObject*> obj(cx, JS_NewObject(cx, &sJSHistogramClass));
  if (!obj) {
    return NS_ERROR_FAILURE;
  }

  // The 3 functions that are wrapped up here are eventually called
  // by the same thread that runs this function.
  if (!(JS_DefineFunction(cx, obj, "name", internal_JSHistogram_Name, 1, 0) &&
        JS_DefineFunction(cx, obj, "snapshot", internal_JSHistogram_Snapshot, 1,
                          0) &&
        JS_DefineFunction(cx, obj, "clear", internal_JSHistogram_Clear, 1,
                          0))) {
    return NS_ERROR_FAILURE;
  }

  JSHistogramData* data = new JSHistogramData{id};
  JS::SetReservedSlot(obj, HistogramObjectDataSlot, JS::PrivateValue(data));
  ret.setObject(*obj);

  return NS_OK;
}

void internal_JSHistogram_finalize(JS::GCContext* gcx, JSObject* obj) {
  if (!obj || JS::GetClass(obj) != &sJSHistogramClass) {
    MOZ_ASSERT_UNREACHABLE("Should have the right JS class.");
    return;
  }

  JSHistogramData* data = GetJSHistogramData(obj);
  MOZ_ASSERT(data);
  delete data;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: JSKeyedHistogram_* functions

// NOTE: the functions in this section:
//
//   internal_JSKeyedHistogram_Add
//   internal_JSKeyedHistogram_Name
//   internal_JSKeyedHistogram_Keys
//   internal_JSKeyedHistogram_Snapshot
//   internal_JSKeyedHistogram_Clear
//   internal_WrapAndReturnKeyedHistogram
//
// Same comments as above, at the JSHistogram_* section, regarding
// deadlock avoidance, apply.

namespace {

void internal_JSKeyedHistogram_finalize(JS::GCContext*, JSObject*);

static const JSClassOps sJSKeyedHistogramClassOps = {
    nullptr, /* addProperty */
    nullptr, /* delProperty */
    nullptr, /* enumerate */
    nullptr, /* newEnumerate */
    nullptr, /* resolve */
    nullptr, /* mayResolve */
    internal_JSKeyedHistogram_finalize};

static const JSClass sJSKeyedHistogramClass = {
    "JSKeyedHistogram", /* name */
    JSCLASS_HAS_RESERVED_SLOTS(HistogramObjectSlotCount) |
        JSCLASS_FOREGROUND_FINALIZE, /* flags */
    &sJSKeyedHistogramClassOps};

static JSHistogramData* GetJSKeyedHistogramData(JSObject* obj) {
  MOZ_ASSERT(JS::GetClass(obj) == &sJSKeyedHistogramClass);
  return JS::GetMaybePtrFromReservedSlot<JSHistogramData>(
      obj, HistogramObjectDataSlot);
}

bool internal_JSKeyedHistogram_Snapshot(JSContext* cx, unsigned argc,
                                        JS::Value* vp) {
  if (!XRE_IsParentProcess()) {
    JS_ReportErrorASCII(
        cx, "Keyed histograms can only be snapshotted in the parent process");
    return false;
  }

  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSKeyedHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSKeyedHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;
  MOZ_ASSERT(internal_IsHistogramEnumId(id));

  // This function should always return |undefined| and never fail but
  // rather report failures using the console.
  args.rval().setUndefined();

  // This is not good standard behavior given that we have histogram instances
  // covering multiple processes.
  // However, changing this requires some broader changes to callers.
  KeyedHistogram* keyed = internal_GetKeyedHistogramById(
      id, ProcessID::Parent, /* instantiate = */ true);
  if (!keyed) {
    JS_ReportErrorASCII(cx, "Failed to look up keyed histogram");
    return false;
  }

  nsAutoString storeName;
  nsresult rv;
  rv = internal_JS_StoreFromObjectArgument(cx, args, storeName);
  if (NS_FAILED(rv)) {
    return false;
  }

  JS::Rooted<JSObject*> snapshot(cx, JS_NewPlainObject(cx));
  if (!snapshot) {
    JS_ReportErrorASCII(cx, "Failed to create object");
    return false;
  }

  rv = keyed->GetJSSnapshot(cx, snapshot, NS_ConvertUTF16toUTF8(storeName),
                            false);

  // If the store is not available, we return nothing and don't fail
  if (rv == NS_ERROR_NO_CONTENT) {
    args.rval().setUndefined();
    return true;
  }

  if (!NS_SUCCEEDED(rv)) {
    JS_ReportErrorASCII(cx, "Failed to reflect keyed histograms");
    return false;
  }

  args.rval().setObject(*snapshot);
  return true;
}

bool internal_JSKeyedHistogram_Add(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSKeyedHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSKeyedHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;
  MOZ_ASSERT(internal_IsHistogramEnumId(id));

  // This function should always return |undefined| and never fail but
  // rather report failures using the console.
  args.rval().setUndefined();
  if (args.length() < 1) {
    LogToBrowserConsole(nsIScriptError::errorFlag, u"Expected one argument"_ns);
    return true;
  }

  nsAutoJSString key;
  if (!args[0].isString() || !key.init(cx, args[0])) {
    LogToBrowserConsole(nsIScriptError::errorFlag, u"Not a string"_ns);
    return true;
  }

  // Check if we're allowed to record in the provided key, for this histogram.
  if (!gHistogramInfos[id].allows_key(NS_ConvertUTF16toUTF8(key))) {
    nsPrintfCString msg("%s - key '%s' not allowed for this keyed histogram",
                        gHistogramInfos[id].name(),
                        NS_ConvertUTF16toUTF8(key).get());
    LogToBrowserConsole(nsIScriptError::errorFlag, NS_ConvertUTF8toUTF16(msg));
    TelemetryScalar::Add(mozilla::Telemetry::ScalarID::
                             TELEMETRY_ACCUMULATE_UNKNOWN_HISTOGRAM_KEYS,
                         NS_ConvertASCIItoUTF16(gHistogramInfos[id].name()), 1);
    return true;
  }

  const uint32_t type = gHistogramInfos[id].histogramType;

  nsTArray<uint32_t> values;
  if (!internal_JSHistogram_GetValueArray(cx, args, type, id, true, values)) {
    // Either GetValueArray or CoerceValue utility function will have printed a
    // meaningful error message so we simple return true
    return true;
  }

  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    for (uint32_t aValue : values) {
      internal_Accumulate(locker, id, NS_ConvertUTF16toUTF8(key), aValue);
    }
  }
  return true;
}

bool internal_JSKeyedHistogram_Name(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
  JS::CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSKeyedHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSKeyedHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;
  MOZ_ASSERT(internal_IsHistogramEnumId(id));
  const char* name = gHistogramInfos[id].name();

  auto cname = NS_ConvertASCIItoUTF16(name);
  args.rval().setString(ToJSString(cx, cname));

  return true;
}

bool internal_JSKeyedHistogram_Keys(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSKeyedHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSKeyedHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;

  nsAutoString storeName;
  nsresult rv = internal_JS_StoreFromObjectArgument(cx, args, storeName);
  if (NS_FAILED(rv)) {
    return false;
  }

  nsTArray<nsCString> keys;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    MOZ_ASSERT(internal_IsHistogramEnumId(id));

    // This is not good standard behavior given that we have histogram instances
    // covering multiple processes.
    // However, changing this requires some broader changes to callers.
    KeyedHistogram* keyed =
        internal_GetKeyedHistogramById(id, ProcessID::Parent);

    MOZ_ASSERT(keyed);
    if (!keyed) {
      return false;
    }

    if (NS_FAILED(
            keyed->GetKeys(locker, NS_ConvertUTF16toUTF8(storeName), keys))) {
      return false;
    }
  }

  // Convert keys from nsTArray<nsCString> to JS array.
  JS::RootedVector<JS::Value> autoKeys(cx);
  if (!autoKeys.reserve(keys.Length())) {
    return false;
  }

  for (const auto& key : keys) {
    JS::Rooted<JS::Value> jsKey(cx);
    jsKey.setString(ToJSString(cx, key));
    if (!autoKeys.append(jsKey)) {
      return false;
    }
  }

  JS::Rooted<JSObject*> jsKeys(cx, JS::NewArrayObject(cx, autoKeys));
  if (!jsKeys) {
    return false;
  }

  args.rval().setObject(*jsKeys);
  return true;
}

bool internal_JSKeyedHistogram_Clear(JSContext* cx, unsigned argc,
                                     JS::Value* vp) {
  if (!XRE_IsParentProcess()) {
    JS_ReportErrorASCII(
        cx, "Keyed histograms can only be cleared in the parent process");
    return false;
  }

  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject() ||
      JS::GetClass(&args.thisv().toObject()) != &sJSKeyedHistogramClass) {
    JS_ReportErrorASCII(cx, "Wrong JS class, expected JSKeyedHistogram class");
    return false;
  }

  JSObject* obj = &args.thisv().toObject();
  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  HistogramID id = data->histogramId;

  // This function should always return |undefined| and never fail but
  // rather report failures using the console.
  args.rval().setUndefined();

  nsAutoString storeName;
  nsresult rv = internal_JS_StoreFromObjectArgument(cx, args, storeName);
  if (NS_FAILED(rv)) {
    return false;
  }

  KeyedHistogram* keyed = nullptr;
  {
    MOZ_ASSERT(internal_IsHistogramEnumId(id));
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);

    // This is not good standard behavior given that we have histogram instances
    // covering multiple processes.
    // However, changing this requires some broader changes to callers.
    keyed = internal_GetKeyedHistogramById(id, ProcessID::Parent,
                                           /* instantiate = */ false);

    if (!keyed) {
      return true;
    }

    keyed->Clear(NS_ConvertUTF16toUTF8(storeName));
  }

  return true;
}

// NOTE: Runs without protection from |gTelemetryHistogramMutex|.
// See comment at the top of this section.
nsresult internal_WrapAndReturnKeyedHistogram(
    HistogramID id, JSContext* cx, JS::MutableHandle<JS::Value> ret) {
  JS::Rooted<JSObject*> obj(cx, JS_NewObject(cx, &sJSKeyedHistogramClass));
  if (!obj) return NS_ERROR_FAILURE;
  // The 6 functions that are wrapped up here are eventually called
  // by the same thread that runs this function.
  if (!(JS_DefineFunction(cx, obj, "add", internal_JSKeyedHistogram_Add, 2,
                          0) &&
        JS_DefineFunction(cx, obj, "name", internal_JSKeyedHistogram_Name, 1,
                          0) &&
        JS_DefineFunction(cx, obj, "snapshot",
                          internal_JSKeyedHistogram_Snapshot, 1, 0) &&
        JS_DefineFunction(cx, obj, "keys", internal_JSKeyedHistogram_Keys, 1,
                          0) &&
        JS_DefineFunction(cx, obj, "clear", internal_JSKeyedHistogram_Clear, 1,
                          0))) {
    return NS_ERROR_FAILURE;
  }

  JSHistogramData* data = new JSHistogramData{id};
  JS::SetReservedSlot(obj, HistogramObjectDataSlot, JS::PrivateValue(data));
  ret.setObject(*obj);

  return NS_OK;
}

void internal_JSKeyedHistogram_finalize(JS::GCContext* gcx, JSObject* obj) {
  if (!obj || JS::GetClass(obj) != &sJSKeyedHistogramClass) {
    MOZ_ASSERT_UNREACHABLE("Should have the right JS class.");
    return;
  }

  JSHistogramData* data = GetJSKeyedHistogramData(obj);
  MOZ_ASSERT(data);
  delete data;
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in namespace TelemetryHistogram::

// All of these functions are actually in namespace TelemetryHistogram::,
// but the ::TelemetryHistogram prefix is given explicitly.  This is
// because it is critical to see which calls from these functions are
// to another function in this interface.  Mis-identifying "inwards
// calls" from "calls to another function in this interface" will lead
// to deadlocking and/or races.  See comments at the top of the file
// for further (important!) details.

void TelemetryHistogram::InitializeGlobalState(bool canRecordBase,
                                               bool canRecordExtended) {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  MOZ_ASSERT(!gTelemetryHistogramInitDone,
             "TelemetryHistogram::InitializeGlobalState "
             "may only be called once");

  gTelemetryHistogramCanRecordBase = canRecordBase;
  gTelemetryHistogramCanRecordExtended = canRecordExtended;

  if (XRE_IsParentProcess()) {
    gHistogramStorage =
        new Histogram* [HistogramCount * size_t(ProcessID::Count)] {};
    gKeyedHistogramStorage =
        new KeyedHistogram* [HistogramCount * size_t(ProcessID::Count)] {};
  }

  // Some Telemetry histograms depend on the value of C++ constants and hardcode
  // their values in Histograms.json.
  // We add static asserts here for those values to match so that future changes
  // don't go unnoticed.
  // clang-format off
  static_assert((uint32_t(JS::GCReason::NUM_TELEMETRY_REASONS) + 1) ==
      gHistogramInfos[mozilla::Telemetry::GC_MINOR_REASON].bucketCount &&
      (uint32_t(JS::GCReason::NUM_TELEMETRY_REASONS) + 1) ==
      gHistogramInfos[mozilla::Telemetry::GC_MINOR_REASON_LONG].bucketCount &&
      (uint32_t(JS::GCReason::NUM_TELEMETRY_REASONS) + 1) ==
      gHistogramInfos[mozilla::Telemetry::GC_REASON_2].bucketCount,
      "NUM_TELEMETRY_REASONS is assumed to be a fixed value in Histograms.json."
      " If this was an intentional change, update the n_values for the "
      "following in Histograms.json: GC_MINOR_REASON, GC_MINOR_REASON_LONG, "
      "GC_REASON_2");

  // clang-format on

  gTelemetryHistogramInitDone = true;
}

void TelemetryHistogram::DeInitializeGlobalState() {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  gTelemetryHistogramCanRecordBase = false;
  gTelemetryHistogramCanRecordExtended = false;
  gTelemetryHistogramInitDone = false;

  // FactoryGet `new`s Histograms for us, but requires us to manually delete.
  if (XRE_IsParentProcess()) {
    for (size_t i = 0; i < HistogramCount * size_t(ProcessID::Count); ++i) {
      if (gKeyedHistogramStorage[i] != gExpiredKeyedHistogram) {
        delete gKeyedHistogramStorage[i];
      }
      if (gHistogramStorage[i] != gExpiredHistogram) {
        delete gHistogramStorage[i];
      }
    }
    delete[] gHistogramStorage;
    delete[] gKeyedHistogramStorage;
  }
  delete gExpiredHistogram;
  gExpiredHistogram = nullptr;
  delete gExpiredKeyedHistogram;
  gExpiredKeyedHistogram = nullptr;
}

#ifdef DEBUG
bool TelemetryHistogram::GlobalStateHasBeenInitialized() {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  return gTelemetryHistogramInitDone;
}
#endif

bool TelemetryHistogram::CanRecordBase() {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  return internal_CanRecordBase();
}

void TelemetryHistogram::SetCanRecordBase(bool b) {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  gTelemetryHistogramCanRecordBase = b;
}

bool TelemetryHistogram::CanRecordExtended() {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  return internal_CanRecordExtended();
}

void TelemetryHistogram::SetCanRecordExtended(bool b) {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  gTelemetryHistogramCanRecordExtended = b;
}

void TelemetryHistogram::InitHistogramRecordingEnabled() {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  auto processType = XRE_GetProcessType();
  for (size_t i = 0; i < HistogramCount; ++i) {
    const HistogramInfo& h = gHistogramInfos[i];
    mozilla::Telemetry::HistogramID id = mozilla::Telemetry::HistogramID(i);
    bool canRecordInProcess =
        CanRecordInProcess(h.record_in_processes, processType);
    internal_SetHistogramRecordingEnabled(locker, id, canRecordInProcess);
  }
}

void TelemetryHistogram::Accumulate(HistogramID aID, uint32_t aSample) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(aID))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return;
  }

  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  internal_Accumulate(locker, aID, aSample);
}

void TelemetryHistogram::Accumulate(HistogramID aID,
                                    const nsTArray<uint32_t>& aSamples) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(aID))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return;
  }

  MOZ_ASSERT(!gHistogramInfos[aID].keyed,
             "Cannot accumulate into a keyed histogram. No key given.");

  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  for (uint32_t sample : aSamples) {
    internal_Accumulate(locker, aID, sample);
  }
}

void TelemetryHistogram::Accumulate(HistogramID aID, const nsCString& aKey,
                                    uint32_t aSample) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(aID))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return;
  }

  // Check if we're allowed to record in the provided key, for this histogram.
  if (!gHistogramInfos[aID].allows_key(aKey)) {
    nsPrintfCString msg("%s - key '%s' not allowed for this keyed histogram",
                        gHistogramInfos[aID].name(), aKey.get());
    LogToBrowserConsole(nsIScriptError::errorFlag, NS_ConvertUTF8toUTF16(msg));
    TelemetryScalar::Add(mozilla::Telemetry::ScalarID::
                             TELEMETRY_ACCUMULATE_UNKNOWN_HISTOGRAM_KEYS,
                         NS_ConvertASCIItoUTF16(gHistogramInfos[aID].name()),
                         1);
    return;
  }

  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  internal_Accumulate(locker, aID, aKey, aSample);
}

void TelemetryHistogram::AccumulateCategorical(HistogramID aId,
                                               const nsCString& label) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return;
  }

  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  if (!internal_CanRecordBase()) {
    return;
  }
  uint32_t labelId = 0;
  if (NS_FAILED(gHistogramInfos[aId].label_id(label.get(), &labelId))) {
    return;
  }
  internal_Accumulate(locker, aId, labelId);
}

void TelemetryHistogram::AccumulateChild(
    ProcessID aProcessType,
    const nsTArray<HistogramAccumulation>& aAccumulations) {
  MOZ_ASSERT(XRE_IsParentProcess());

  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  if (!internal_CanRecordBase()) {
    return;
  }
  for (uint32_t i = 0; i < aAccumulations.Length(); ++i) {
    if (NS_WARN_IF(!internal_IsHistogramEnumId(aAccumulations[i].mId))) {
      MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
      continue;
    }
    internal_AccumulateChild(locker, aProcessType, aAccumulations[i].mId,
                             aAccumulations[i].mSample);
  }
}

void TelemetryHistogram::AccumulateChildKeyed(
    ProcessID aProcessType,
    const nsTArray<KeyedHistogramAccumulation>& aAccumulations) {
  MOZ_ASSERT(XRE_IsParentProcess());
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);
  if (!internal_CanRecordBase()) {
    return;
  }
  for (uint32_t i = 0; i < aAccumulations.Length(); ++i) {
    if (NS_WARN_IF(!internal_IsHistogramEnumId(aAccumulations[i].mId))) {
      MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
      continue;
    }
    internal_AccumulateChildKeyed(locker, aProcessType, aAccumulations[i].mId,
                                  aAccumulations[i].mKey,
                                  aAccumulations[i].mSample);
  }
}

nsresult TelemetryHistogram::GetAllStores(StringHashSet& set) {
  for (uint32_t storeIdx : gHistogramStoresTable) {
    const char* name = &gHistogramStringTable[storeIdx];
    nsAutoCString store;
    store.AssignASCII(name);
    if (!set.Insert(store, mozilla::fallible)) {
      return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}

nsresult TelemetryHistogram::GetCategoricalHistogramLabels(
    JSContext* aCx, JS::MutableHandle<JS::Value> aResult) {
  JS::Rooted<JSObject*> root_obj(aCx, JS_NewPlainObject(aCx));
  if (!root_obj) {
    return NS_ERROR_FAILURE;
  }
  aResult.setObject(*root_obj);

  for (const HistogramInfo& info : gHistogramInfos) {
    if (info.histogramType != nsITelemetry::HISTOGRAM_CATEGORICAL) {
      continue;
    }

    const char* name = info.name();
    JS::Rooted<JSObject*> labels(aCx,
                                 JS::NewArrayObject(aCx, info.label_count));
    if (!labels) {
      return NS_ERROR_FAILURE;
    }

    if (!JS_DefineProperty(aCx, root_obj, name, labels, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    for (uint32_t i = 0; i < info.label_count; ++i) {
      uint32_t string_offset = gHistogramLabelTable[info.label_index + i];
      const char* const label = &gHistogramStringTable[string_offset];
      auto clabel = NS_ConvertASCIItoUTF16(label);
      JS::Rooted<JS::Value> value(aCx);
      value.setString(ToJSString(aCx, clabel));
      if (!JS_DefineElement(aCx, labels, i, value, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  return NS_OK;
}

nsresult TelemetryHistogram::GetHistogramById(
    const nsACString& name, JSContext* cx, JS::MutableHandle<JS::Value> ret) {
  HistogramID id;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    nsresult rv = internal_GetHistogramIdByName(locker, name, &id);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    if (gHistogramInfos[id].keyed) {
      return NS_ERROR_FAILURE;
    }
  }
  // Runs without protection from |gTelemetryHistogramMutex|
  return internal_WrapAndReturnHistogram(id, cx, ret);
}

nsresult TelemetryHistogram::GetKeyedHistogramById(
    const nsACString& name, JSContext* cx, JS::MutableHandle<JS::Value> ret) {
  HistogramID id;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    nsresult rv = internal_GetHistogramIdByName(locker, name, &id);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    if (!gHistogramInfos[id].keyed) {
      return NS_ERROR_FAILURE;
    }
  }
  // Runs without protection from |gTelemetryHistogramMutex|
  return internal_WrapAndReturnKeyedHistogram(id, cx, ret);
}

const char* TelemetryHistogram::GetHistogramName(HistogramID id) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(id))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return nullptr;
  }

  const HistogramInfo& h = gHistogramInfos[id];
  return h.name();
}

uint8_t TelemetryHistogram::GetHistogramType(HistogramID id) {
  if (NS_WARN_IF(!internal_IsHistogramEnumId(id))) {
    MOZ_ASSERT_UNREACHABLE("Histogram usage requires valid ids.");
    return std::numeric_limits<uint8_t>::max();
  }

  const HistogramInfo& h = gHistogramInfos[id];
  return h.histogramType;
}

nsresult TelemetryHistogram::CreateHistogramSnapshots(
    JSContext* aCx, JS::MutableHandle<JS::Value> aResult,
    const nsACString& aStore, unsigned int aDataset, bool aClearSubsession,
    bool aFilterTest) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  // Runs without protection from |gTelemetryHistogramMutex|
  JS::Rooted<JSObject*> root_obj(aCx, JS_NewPlainObject(aCx));
  if (!root_obj) {
    return NS_ERROR_FAILURE;
  }
  aResult.setObject(*root_obj);

  // Include the GPU process in histogram snapshots only if we actually tried
  // to launch a process for it.
  bool includeGPUProcess = internal_AttemptedGPUProcess();

  HistogramProcessSnapshotsArray processHistArray;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    nsresult rv = internal_GetHistogramsSnapshot(
        locker, aStore, aDataset, aClearSubsession, includeGPUProcess,
        aFilterTest, processHistArray);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Make the JS calls on the stashed histograms for every process
  for (uint32_t process = 0; process < processHistArray.length(); ++process) {
    JS::Rooted<JSObject*> processObject(aCx, JS_NewPlainObject(aCx));
    if (!processObject) {
      return NS_ERROR_FAILURE;
    }
    if (!JS_DefineProperty(aCx, root_obj,
                           GetNameForProcessID(ProcessID(process)),
                           processObject, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    for (const HistogramSnapshotInfo& hData : processHistArray[process]) {
      HistogramID id = hData.histogramID;

      JS::Rooted<JSObject*> hobj(aCx, JS_NewPlainObject(aCx));
      if (!hobj) {
        return NS_ERROR_FAILURE;
      }

      if (NS_FAILED(internal_ReflectHistogramAndSamples(
              aCx, hobj, gHistogramInfos[id], hData.data))) {
        return NS_ERROR_FAILURE;
      }

      if (!JS_DefineProperty(aCx, processObject, gHistogramInfos[id].name(),
                             hobj, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }
  return NS_OK;
}

nsresult TelemetryHistogram::GetKeyedHistogramSnapshots(
    JSContext* aCx, JS::MutableHandle<JS::Value> aResult,
    const nsACString& aStore, unsigned int aDataset, bool aClearSubsession,
    bool aFilterTest) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  // Runs without protection from |gTelemetryHistogramMutex|
  JS::Rooted<JSObject*> obj(aCx, JS_NewPlainObject(aCx));
  if (!obj) {
    return NS_ERROR_FAILURE;
  }
  aResult.setObject(*obj);

  // Include the GPU process in histogram snapshots only if we actually tried
  // to launch a process for it.
  bool includeGPUProcess = internal_AttemptedGPUProcess();

  // Get a snapshot of all the data while holding the mutex.
  KeyedHistogramProcessSnapshotsArray processHistArray;
  {
    StaticMutexAutoLock locker(gTelemetryHistogramMutex);
    nsresult rv = internal_GetKeyedHistogramsSnapshot(
        locker, aStore, aDataset, aClearSubsession, includeGPUProcess,
        aFilterTest, processHistArray);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Mirror the snapshot data to JS, now that we released the mutex.
  for (uint32_t process = 0; process < processHistArray.length(); ++process) {
    JS::Rooted<JSObject*> processObject(aCx, JS_NewPlainObject(aCx));
    if (!processObject) {
      return NS_ERROR_FAILURE;
    }
    if (!JS_DefineProperty(aCx, obj, GetNameForProcessID(ProcessID(process)),
                           processObject, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    for (const KeyedHistogramSnapshotInfo& hData : processHistArray[process]) {
      const HistogramInfo& info = gHistogramInfos[hData.histogramId];

      JS::Rooted<JSObject*> snapshot(aCx, JS_NewPlainObject(aCx));
      if (!snapshot) {
        return NS_ERROR_FAILURE;
      }

      if (!NS_SUCCEEDED(internal_ReflectKeyedHistogram(hData.data, info, aCx,
                                                       snapshot))) {
        return NS_ERROR_FAILURE;
      }

      if (!JS_DefineProperty(aCx, processObject, info.name(), snapshot,
                             JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }
  return NS_OK;
}

size_t TelemetryHistogram::GetHistogramSizesOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  StaticMutexAutoLock locker(gTelemetryHistogramMutex);

  size_t n = 0;

  // If we allocated the array, let's count the number of pointers in there and
  // each entry's size.
  if (gKeyedHistogramStorage) {
    n += HistogramCount * size_t(ProcessID::Count) * sizeof(KeyedHistogram*);
    for (size_t i = 0; i < HistogramCount * size_t(ProcessID::Count); ++i) {
      if (gKeyedHistogramStorage[i] &&
          gKeyedHistogramStorage[i] != gExpiredKeyedHistogram) {
        n += gKeyedHistogramStorage[i]->SizeOfIncludingThis(aMallocSizeOf);
      }
    }
  }

  // If we allocated the array, let's count the number of pointers in there.
  if (gHistogramStorage) {
    n += HistogramCount * size_t(ProcessID::Count) * sizeof(Histogram*);
    for (size_t i = 0; i < HistogramCount * size_t(ProcessID::Count); ++i) {
      if (gHistogramStorage[i] && gHistogramStorage[i] != gExpiredHistogram) {
        n += gHistogramStorage[i]->SizeOfIncludingThis(aMallocSizeOf);
      }
    }
  }

  // We only allocate the expired (keyed) histogram once.
  if (gExpiredKeyedHistogram) {
    n += gExpiredKeyedHistogram->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (gExpiredHistogram) {
    n += gExpiredHistogram->SizeOfIncludingThis(aMallocSizeOf);
  }

  return n;
}
