/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CycleCollectedJSRuntime_h
#define mozilla_CycleCollectedJSRuntime_h

#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DeferredFinalize.h"
#include "mozilla/HashTable.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SegmentedVector.h"
#include "mozilla/Variant.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/TypeDecls.h"

#include "nsCycleCollectionParticipant.h"
#include "nsTHashMap.h"
#include "nsHashKeys.h"
#include "nsStringFwd.h"
#include "nsTHashSet.h"

class nsCycleCollectionNoteRootCallback;
class nsIException;
class nsWrapperCache;

namespace mozilla {

class JSGCThingParticipant : public nsCycleCollectionParticipant {
 public:
  constexpr JSGCThingParticipant() : nsCycleCollectionParticipant(false) {}

  NS_IMETHOD_(void) Root(void*) override {
    MOZ_ASSERT(false, "Don't call Root on GC things");
  }

  NS_IMETHOD_(void) Unlink(void*) override {
    MOZ_ASSERT(false, "Don't call Unlink on GC things, as they may be dead");
  }

  NS_IMETHOD_(void) Unroot(void*) override {
    MOZ_ASSERT(false, "Don't call Unroot on GC things, as they may be dead");
  }

  NS_IMETHOD_(void) DeleteCycleCollectable(void* aPtr) override {
    MOZ_ASSERT(false, "Can't directly delete a cycle collectable GC thing");
  }

  NS_IMETHOD TraverseNative(void* aPtr,
                            nsCycleCollectionTraversalCallback& aCb) override;

  NS_DECL_CYCLE_COLLECTION_CLASS_NAME_METHOD(JSGCThingParticipant)
};

class JSZoneParticipant : public nsCycleCollectionParticipant {
 public:
  constexpr JSZoneParticipant() : nsCycleCollectionParticipant(false) {}

  NS_IMETHOD_(void) Root(void*) override {
    MOZ_ASSERT(false, "Don't call Root on GC things");
  }

  NS_IMETHOD_(void) Unlink(void*) override {
    MOZ_ASSERT(false, "Don't call Unlink on GC things, as they may be dead");
  }

  NS_IMETHOD_(void) Unroot(void*) override {
    MOZ_ASSERT(false, "Don't call Unroot on GC things, as they may be dead");
  }

  NS_IMETHOD_(void) DeleteCycleCollectable(void*) override {
    MOZ_ASSERT(false, "Can't directly delete a cycle collectable GC thing");
  }

  NS_IMETHOD TraverseNative(void* aPtr,
                            nsCycleCollectionTraversalCallback& aCb) override;

  NS_DECL_CYCLE_COLLECTION_CLASS_NAME_METHOD(JSZoneParticipant)
};

class IncrementalFinalizeRunnable;

enum WhichJSHolders { AllJSHolders, JSHoldersRequiredForGrayMarking };

// A map from JS holders to tracer objects, where the values are stored in
// SegmentedVector to speed up iteration.
class JSHolderMap {
 public:
  class Iter;

  JSHolderMap();
  ~JSHolderMap() { MOZ_RELEASE_ASSERT(!mHasIterator); }

  bool Has(void* aHolder) const;
  nsScriptObjectTracer* Get(void* aHolder) const;
  nsScriptObjectTracer* Extract(void* aHolder);
  void Put(void* aHolder, nsScriptObjectTracer* aTracer, JS::Zone* aZone);

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;

 private:
  struct Entry {
    void* mHolder;
    nsScriptObjectTracer* mTracer;
#ifdef DEBUG
    JS::Zone* mZone;
#endif

    Entry();
    Entry(void* aHolder, nsScriptObjectTracer* aTracer, JS::Zone* aZone);
  };

  using EntryMap = mozilla::HashMap<void*, Entry*, DefaultHasher<void*>,
                                    InfallibleAllocPolicy>;

  using EntryVector = SegmentedVector<Entry, 256, InfallibleAllocPolicy>;

  using EntryVectorMap =
      mozilla::HashMap<JS::Zone*, UniquePtr<EntryVector>,
                       DefaultHasher<JS::Zone*>, InfallibleAllocPolicy>;

  class EntryVectorIter;

  bool RemoveEntry(EntryVector& aJSHolders, Entry* aEntry);

  // A map from a holder pointer to a pointer to an entry in a vector.
  EntryMap mJSHolderMap;

  // A vector of holders not associated with a particular zone or that can
  // contain pointers to GC things in more than one zone.
  EntryVector mAnyZoneJSHolders;

  // A map from a zone to a vector of holders that only contain pointers to GC
  // things in that zone.
  //
  // Currently this will only contain wrapper cache wrappers since these are the
  // only holders to pass a zone parameter through to AddJSHolder.
  EntryVectorMap mPerZoneJSHolders;

  // Iterators can mutate the element vectors by removing stale elements. Allow
  // at most one to exist at a time.
  bool mHasIterator = false;
};

// An iterator over an EntryVector that skips over removed entries and removes
// them from the map.
class JSHolderMap::EntryVectorIter {
 public:
  EntryVectorIter(JSHolderMap& aMap, EntryVector& aVector)
      : mHolderMap(aMap), mVector(aVector), mIter(aVector.Iter()) {
    Settle();
  }

  const EntryVector& Vector() const { return mVector; }

  bool Done() const { return mIter.Done(); }
  const Entry& Get() const { return mIter.Get(); }
  void Next() {
    mIter.Next();
    Settle();
  }

  operator const Entry*() const { return &Get(); }
  const Entry* operator->() const { return &Get(); }

 private:
  void Settle();
  friend class JSHolderMap::Iter;

  JSHolderMap& mHolderMap;
  EntryVector& mVector;
  EntryVector::IterImpl mIter;
};

class JSHolderMap::Iter {
 public:
  explicit Iter(JSHolderMap& aMap, WhichJSHolders aWhich = AllJSHolders);

  ~Iter() {
    MOZ_RELEASE_ASSERT(mHolderMap.mHasIterator);
    mHolderMap.mHasIterator = false;
  }

  bool Done() const { return mIter.Done(); }
  const Entry& Get() const { return mIter.Get(); }
  void Next() {
    mIter.Next();
    Settle();
  }

  // If the holders have been removed from the map while the iterator is live,
  // then the iterator may point to a removed entry. Update the iterator to make
  // sure it points to a valid entry or is done.
  void UpdateForRemovals();

  operator const Entry*() const { return &Get(); }
  const Entry* operator->() const { return &Get(); }

  JS::Zone* Zone() const { return mZone; }

 private:
  void Settle();

  JSHolderMap& mHolderMap;
  Vector<JS::Zone*, 1, InfallibleAllocPolicy> mZones;
  JS::Zone* mZone = nullptr;
  EntryVectorIter mIter;
};

struct JSHolderListEntry {
  void* mHolder;
  JSHolderKey* mKey;
  nsScriptObjectTracer* mTracer;

  JSHolderListEntry();
  JSHolderListEntry(void* aHolder, JSHolderKey* aKey,
                    nsScriptObjectTracer* aTracer);
};

// A list of JS holders and their tracer objects.
//
// This supports the same operations as JSHolderMap but does not use a hash
// table to look up holders. Instead it requires that entries embed a
// JSHolderKey, which it uses to store the location of the entry in the list.
class JSHolderList {
 public:
  class Iter;

  JSHolderList();
  ~JSHolderList() { MOZ_RELEASE_ASSERT(!mHasIterator); }

  bool Has(JSHolderKey* aKey) const;
  nsScriptObjectTracer* Get(void* aHolder, JSHolderKey* aKey) const;
  nsScriptObjectTracer* Extract(void* aHolder, JSHolderKey* aKey);
  void Put(void* aHolder, nsScriptObjectTracer* aTracer, JSHolderKey* aKey);

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;

 private:
  using Entry = JSHolderListEntry;

  using EntryVector = SegmentedVector<Entry, 256, InfallibleAllocPolicy>;

  class EntryVectorIter;

  bool RemoveEntry(EntryVector& aJSHolders, Entry* aEntry);

  // A vector of holders. These are not associated with a particular zone and
  // can contain pointers to GC things in more than one zone.
  EntryVector mJSHolders;

  // Iterators can mutate the element vectors by removing stale elements. Allow
  // at most one to exist at a time.
  bool mHasIterator = false;
};

// An iterator over an EntryVector that skips over removed entries and removes
// them from the list.
class JSHolderList::EntryVectorIter {
 public:
  EntryVectorIter(JSHolderList& aList, EntryVector& aVector)
      : mHolderList(aList), mVector(aVector), mIter(aVector.Iter()) {
    Settle();
  }

  const EntryVector& Vector() const { return mVector; }

  bool Done() const { return mIter.Done(); }
  const Entry& Get() const { return mIter.Get(); }
  void Next() {
    mIter.Next();
    Settle();
  }

  operator const Entry*() const { return &Get(); }
  const Entry* operator->() const { return &Get(); }

 private:
  void Settle();
  friend class JSHolderList::Iter;

  JSHolderList& mHolderList;
  EntryVector& mVector;
  EntryVector::IterImpl mIter;
};

class JSHolderList::Iter {
 public:
  explicit Iter(JSHolderList& aList, WhichJSHolders aWhich = AllJSHolders);

  ~Iter() {
    MOZ_RELEASE_ASSERT(mHolderList.mHasIterator);
    mHolderList.mHasIterator = false;
  }

  bool Done() const { return mIter.Done(); }
  const Entry& Get() const { return mIter.Get(); }
  void Next() { mIter.Next(); }

  // If the holders have been removed from the list while the iterator is live,
  // then the iterator may point to a removed entry. Update the iterator to make
  // sure it points to a valid entry or is done.
  void UpdateForRemovals();

  operator const Entry*() const { return &Get(); }
  const Entry* operator->() const { return &Get(); }

  JS::Zone* Zone() const { return nullptr; }

 private:
  JSHolderList& mHolderList;
  EntryVectorIter mIter;
};

class CycleCollectedJSRuntime {
  friend class JSGCThingParticipant;
  friend class JSZoneParticipant;
  friend class IncrementalFinalizeRunnable;
  friend class CycleCollectedJSContext;

 protected:
  CycleCollectedJSRuntime(JSContext* aMainContext);
  virtual ~CycleCollectedJSRuntime();

  virtual void Shutdown(JSContext* cx);

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  void UnmarkSkippableJSHolders();

  virtual void TraverseAdditionalNativeRoots(
      nsCycleCollectionNoteRootCallback& aCb) {}
  virtual void TraceAdditionalNativeGrayRoots(JSTracer* aTracer) {}

  virtual void CustomGCCallback(JSGCStatus aStatus) {}
  virtual void CustomOutOfMemoryCallback() {}

  CycleCollectedJSContext* GetContext() { return mContext; }

  void TraceNativeBlackRoots(JSTracer* aTracer);
  virtual void TraceAdditionalNativeBlackRoots(JSTracer* aTracer) {}

 private:
  void DescribeGCThing(bool aIsMarked, JS::GCCellPtr aThing,
                       nsCycleCollectionTraversalCallback& aCb) const;

  virtual bool DescribeCustomObjects(JSObject* aObject, const JSClass* aClasp,
                                     char (&aName)[72]) const {
    return false;  // We did nothing.
  }

  void NoteGCThingJSChildren(JS::GCCellPtr aThing,
                             nsCycleCollectionTraversalCallback& aCb) const;

  void NoteGCThingXPCOMChildren(const JSClass* aClasp, JSObject* aObj,
                                nsCycleCollectionTraversalCallback& aCb) const;

  virtual bool NoteCustomGCThingXPCOMChildren(
      const JSClass* aClasp, JSObject* aObj,
      nsCycleCollectionTraversalCallback& aCb) const {
    return false;  // We did nothing.
  }

  enum TraverseSelect { TRAVERSE_CPP, TRAVERSE_FULL };

  void TraverseGCThing(TraverseSelect aTs, JS::GCCellPtr aThing,
                       nsCycleCollectionTraversalCallback& aCb);

  void TraverseZone(JS::Zone* aZone, nsCycleCollectionTraversalCallback& aCb);

  static void TraverseObjectShim(void* aData, JS::GCCellPtr aThing,
                                 const JS::AutoRequireNoGC& nogc);

  void TraverseNativeRoots(nsCycleCollectionNoteRootCallback& aCb);

  template <typename ContainerT>
  void TraverseJSHolders(ContainerT& aHolders,
                         nsCycleCollectionNoteRootCallback& aCb);

  static void TraceBlackJS(JSTracer* aTracer, void* aData);

  // Trace gray JS roots until budget is exceeded and return whether we
  // finished.
  static bool TraceGrayJS(JSTracer* aTracer, JS::SliceBudget& budget,
                          void* aData);

  static void GCCallback(JSContext* aContext, JSGCStatus aStatus,
                         JS::GCReason aReason, void* aData);
  static void GCSliceCallback(JSContext* aContext, JS::GCProgress aProgress,
                              const JS::GCDescription& aDesc);
  static void GCNurseryCollectionCallback(JSContext* aContext,
                                          JS::GCNurseryProgress aProgress,
                                          JS::GCReason aReason, void* data);
  static void OutOfMemoryCallback(JSContext* aContext, void* aData);

  static bool ContextCallback(JSContext* aCx, unsigned aOperation, void* aData);

  static void* BeforeWaitCallback(uint8_t* aMemory);
  static void AfterWaitCallback(void* aCookie);

#ifdef NS_BUILD_REFCNT_LOGGING
  void TraceAllNativeGrayRoots(JSTracer* aTracer);
#endif

  bool TraceNativeGrayRoots(JSTracer* aTracer, WhichJSHolders aWhich,
                            JS::SliceBudget& aBudget);
  template <typename IterT>
  bool TraceJSHolders(JSTracer* aTracer, IterT& aIter,
                      JS::SliceBudget& aBudget);

 public:
  enum DeferredFinalizeType {
    // Never finalize immediately, because it would be unsafe.
    FinalizeLater,
    // Finalize later if we can, but it is okay to do it immediately.
    FinalizeIncrementally,
    // Finalize immediately, for shutdown or testing purposes.
    FinalizeNow,
  };

  void FinalizeDeferredThings(DeferredFinalizeType aType);

  virtual void PrepareForForgetSkippable() = 0;
  virtual void BeginCycleCollectionCallback(mozilla::CCReason aReason) = 0;
  virtual void EndCycleCollectionCallback(CycleCollectorResults& aResults) = 0;
  virtual void DispatchDeferredDeletion(bool aContinuation,
                                        bool aPurge = false) = 0;

  // Two conditions, JSOutOfMemory and JSLargeAllocationFailure, are noted in
  // crash reports. Here are the values that can appear in the reports:
  enum class OOMState : uint32_t {
    // The condition has never happened. No entry appears in the crash report.
    OK,

    // We are currently reporting the given condition.
    //
    // Suppose a crash report contains "JSLargeAllocationFailure:
    // Reporting". This means we crashed while executing memory-pressure
    // observers, trying to shake loose some memory. The large allocation in
    // question did not return null: it is still on the stack. Had we not
    // crashed, it would have been retried.
    Reporting,

    // The condition has been reported since the last GC.
    //
    // If a crash report contains "JSOutOfMemory: Reported", that means a small
    // allocation failed, and then we crashed, probably due to buggy
    // error-handling code that ran after allocation returned null.
    //
    // This contrasts with "Reporting" which means that no error-handling code
    // had executed yet.
    Reported,

    // The condition has happened, but a GC cycle ended since then.
    //
    // GC is taken as a proxy for "we've been banging on the heap a good bit
    // now and haven't crashed; the OOM was probably handled correctly".
    Recovered
  };

  const char* OOMStateToString(const OOMState aOomState) const;

  // Returns true if OOM was reported and a new successful GC cycle hasn't
  // occurred since.
  bool OOMReported();

  void SetLargeAllocationFailure(OOMState aNewState);

  void AnnotateAndSetOutOfMemory(OOMState* aStatePtr, OOMState aNewState);
  void OnGC(JSContext* aContext, JSGCStatus aStatus, JS::GCReason aReason);
  void OnOutOfMemory();
  void OnLargeAllocationFailure();

  JSRuntime* Runtime() { return mJSRuntime; }
  const JSRuntime* Runtime() const { return mJSRuntime; }

  bool HasPendingIdleGCTask() const {
    // Idle GC task associates with JSRuntime.
    MOZ_ASSERT_IF(mHasPendingIdleGCTask, Runtime());
    return mHasPendingIdleGCTask;
  }
  void SetPendingIdleGCTask() {
    // Idle GC task associates with JSRuntime.
    MOZ_ASSERT(Runtime());
    mHasPendingIdleGCTask = true;
  }
  void ClearPendingIdleGCTask() { mHasPendingIdleGCTask = false; }

  void RunIdleTimeGCTask() {
    if (HasPendingIdleGCTask()) {
      JS::MaybeRunNurseryCollection(Runtime(),
                                    JS::GCReason::EAGER_NURSERY_COLLECTION);
      ClearPendingIdleGCTask();
    }
  }

  bool IsIdleGCTaskNeeded() {
    return !HasPendingIdleGCTask() && Runtime() &&
           JS::WantEagerMinorGC(Runtime()) != JS::GCReason::NO_REASON;
  }

 public:
  void AddJSHolder(void* aHolder, nsScriptObjectTracer* aTracer,
                   JS::Zone* aZone);
  void AddJSHolderWithKey(void* aHolder, nsScriptObjectTracer* aTracer,
                          JSHolderKey* aKey);
  void RemoveJSHolder(void* aHolder);
  void RemoveJSHolderWithKey(void* aHolder, JSHolderKey* aKey);
#ifdef DEBUG
  void AssertNoObjectsToTrace(void* aPossibleJSHolder);
#endif

  nsCycleCollectionParticipant* GCThingParticipant();
  nsCycleCollectionParticipant* ZoneParticipant();

  nsresult TraverseRoots(nsCycleCollectionNoteRootCallback& aCb);
  virtual bool UsefulToMergeZones() const;
  void FixWeakMappingGrayBits() const;
  void CheckGrayBits() const;
  bool AreGCGrayBitsValid() const;
  void GarbageCollect(JS::GCOptions options, JS::GCReason aReason) const;

  // This needs to be an nsWrapperCache, not a JSObject, because we need to know
  // when our object gets moved.  But we can't trace it (and hence update our
  // storage), because we do not want to keep it alive.  nsWrapperCache handles
  // this for us via its "object moved" handling.
  void NurseryWrapperAdded(nsWrapperCache* aCache);
  void JSObjectsTenured(JS::GCContext* aGCContext);

  void DeferredFinalize(DeferredFinalizeAppendFunction aAppendFunc,
                        DeferredFinalizeFunction aFunc, void* aThing);
  void DeferredFinalize(nsISupports* aSupports);

  void DumpJSHeap(FILE* aFile);

  // Add aZone to the set of zones waiting for a GC.
  void AddZoneWaitingForGC(JS::Zone* aZone) {
    mZonesWaitingForGC.Insert(aZone);
  }

  static void OnZoneDestroyed(JS::GCContext* aGcx, JS::Zone* aZone);

  // Prepare any zones for GC that have been passed to AddZoneWaitingForGC()
  // since the last GC or since the last call to PrepareWaitingZonesForGC(),
  // whichever was most recent. If there were no such zones, prepare for a
  // full GC.
  void PrepareWaitingZonesForGC();

  // Get the current thread's CycleCollectedJSRuntime.  Returns null if there
  // isn't one.
  static CycleCollectedJSRuntime* Get();

  void SetContext(CycleCollectedJSContext* aContext);

#ifdef NIGHTLY_BUILD
  bool GetRecentDevError(JSContext* aContext,
                         JS::MutableHandle<JS::Value> aError);
  void ClearRecentDevError();
#endif  // defined(NIGHTLY_BUILD)

 private:
  CycleCollectedJSContext* mContext;

  JSGCThingParticipant mGCThingCycleCollectorGlobal;

  JSZoneParticipant mJSZoneCycleCollectorGlobal;

  JSRuntime* mJSRuntime;
  bool mHasPendingIdleGCTask;

  JS::GCSliceCallback mPrevGCSliceCallback;

  mozilla::TimeStamp mLatestNurseryCollectionStart;

  JSHolderMap mJSHolderMap;
  JSHolderList mJSHolderList;

  // Holds state for incremental tracing of gray roots between calls to
  // TraceNativeGrayRoots.
  Variant<Nothing, JSHolderMap::Iter, JSHolderList::Iter> mTraceState;

  using DeferredFinalizerTable = nsTHashMap<DeferredFinalizeFunction, void*>;
  DeferredFinalizerTable mDeferredFinalizerTable;

  RefPtr<IncrementalFinalizeRunnable> mFinalizeRunnable;

  OOMState mOutOfMemoryState;
  OOMState mLargeAllocationFailureState;

  static const size_t kSegmentSize = 512;
  using NurseryObjectsVector =
      SegmentedVector<nsWrapperCache*, kSegmentSize, InfallibleAllocPolicy>;
  NurseryObjectsVector mNurseryObjects;

  nsTHashSet<JS::Zone*> mZonesWaitingForGC;

  struct EnvironmentPreparer : public js::ScriptEnvironmentPreparer {
    void invoke(JS::Handle<JSObject*> global, Closure& closure) override;
  };
  EnvironmentPreparer mEnvironmentPreparer;

#ifdef DEBUG
  bool mShutdownCalled;
#endif

#ifdef NIGHTLY_BUILD
  // Implementation of the error interceptor.
  // Built on nightly only to avoid any possible performance impact on release

  struct ErrorInterceptor final : public JSErrorInterceptor {
    virtual void interceptError(JSContext* cx,
                                JS::Handle<JS::Value> exn) override;
    void Shutdown(JSRuntime* rt);

    // Copy of the details of the exception.
    // We store this rather than the exception itself to avoid dealing with
    // complicated garbage-collection scenarios, e.g. a JSContext being killed
    // while we still hold onto an exception thrown from it.
    struct ErrorDetails {
      nsCString mFilename;
      nsString mMessage;
      nsString mStack;
      JSExnType mType;
      uint32_t mLine;
      uint32_t mColumn;
    };

    // If we have encountered at least one developer error,
    // the first error we have encountered. Otherwise, or
    // if we have reset since the latest error, `None`.
    Maybe<ErrorDetails> mThrownError;
  };
  ErrorInterceptor mErrorInterceptor;

#endif  // defined(NIGHTLY_BUILD)
};

void TraceScriptHolder(nsISupports* aHolder, JSTracer* aTracer);

}  // namespace mozilla

#endif  // mozilla_CycleCollectedJSRuntime_h
