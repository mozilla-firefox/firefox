/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitRealm_h
#define jit_JitRealm_h

#include "mozilla/Assertions.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/TimeStamp.h"

#include <stddef.h>
#include <utility>

#include "jit/BaselineCompileQueue.h"
#include "jit/CacheIRStubKey.h"
#include "jit/ICStubSpace.h"
#include "js/AllocPolicy.h"
#include "js/HashTable.h"

class JSScript;
class JSTracer;

namespace js {
namespace jit {

// JIT state for a single realm.
class JitRealm {
  // Scripts queued for Baseline compilation.
  BaselineCompileQueue baselineCompileQueue_;

  // Allocated space for CacheIR stubs.
  ICStubSpace stubSpace_;

  // Set of CacheIRStubInfo instances used by Ion stubs in this realm.
  using IonCacheIRStubInfoSet =
      HashSet<CacheIRStubKey, CacheIRStubKey, SystemAllocPolicy>;
  IonCacheIRStubInfoSet ionCacheIRStubInfoSet_;

  // Last time at which JIT code was discarded for this realm. This is only
  // set when JitScripts and Baseline code are discarded as well.
  mozilla::TimeStamp lastDiscardedCodeTime_;

  // Whether the GC should preserve this realm's JIT code. This is
  // recomputed at the start of each GC.
  bool gcPreserveCode_ = false;

 public:
  BaselineCompileQueue& baselineCompileQueue() { return baselineCompileQueue_; }

  ICStubSpace* stubSpace() { return &stubSpace_; }

  CacheIRStubInfo* getIonCacheIRStubInfo(const CacheIRStubKey::Lookup& key) {
    IonCacheIRStubInfoSet::Ptr p = ionCacheIRStubInfoSet_.lookup(key);
    return p ? p->stubInfo.get() : nullptr;
  }
  [[nodiscard]] bool putIonCacheIRStubInfo(const CacheIRStubKey::Lookup& lookup,
                                           CacheIRStubKey& key) {
    IonCacheIRStubInfoSet::AddPtr p =
        ionCacheIRStubInfoSet_.lookupForAdd(lookup);
    MOZ_ASSERT(!p);
    return ionCacheIRStubInfoSet_.add(p, std::move(key));
  }
  void purgeIonCacheIRStubInfo() { ionCacheIRStubInfoSet_.clearAndCompact(); }

  void setPreservingCode(bool preserving) { gcPreserveCode_ = preserving; }
  bool isPreservingCode() const { return gcPreserveCode_; }

  mozilla::TimeStamp lastDiscardedCodeTime() const {
    return lastDiscardedCodeTime_;
  }
  void setLastDiscardedCodeTime(mozilla::TimeStamp time) {
    lastDiscardedCodeTime_ = time;
  }

  void removeFromCompileQueue(JSScript* script) {
    baselineCompileQueue_.remove(script);
  }

  void trace(JSTracer* trc) { baselineCompileQueue_.trace(trc); }

  void finishRoots() { baselineCompileQueue_.clear(); }

  void addSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf,
                              size_t* cacheIRStubs) const {
    *cacheIRStubs += stubSpace_.sizeOfExcludingThis(mallocSizeOf);
    *cacheIRStubs +=
        ionCacheIRStubInfoSet_.shallowSizeOfExcludingThis(mallocSizeOf);
  }

  static constexpr size_t offsetOfBaselineCompileQueue() {
    return offsetof(JitRealm, baselineCompileQueue_);
  }
};

}  // namespace jit
}  // namespace js

#endif /* jit_JitRealm_h */
