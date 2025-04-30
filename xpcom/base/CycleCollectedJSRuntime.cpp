/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We're dividing JS objects into 3 categories:
//
// 1. "real" roots, held by the JS engine itself or rooted through the root
//    and lock JS APIs. Roots from this category are considered black in the
//    cycle collector, any cycle they participate in is uncollectable.
//
// 2. certain roots held by C++ objects that are guaranteed to be alive.
//    Roots from this category are considered black in the cycle collector,
//    and any cycle they participate in is uncollectable. These roots are
//    traced from TraceNativeBlackRoots.
//
// 3. all other roots held by C++ objects that participate in cycle collection,
//    held by us (see TraceNativeGrayRoots). Roots from this category are
//    considered grey in the cycle collector; whether or not they are collected
//    depends on the objects that hold them.
//
// Note that if a root is in multiple categories the fact that it is in
// category 1 or 2 that takes precedence, so it will be considered black.
//
// During garbage collection we switch to an additional mark color (gray) when
// tracing inside TraceNativeGrayRoots. This allows us to walk those roots later
// on and add all objects reachable only from them to the cycle collector.
//
// Phases:
//
// 1. marking of the roots in category 1 by having the JS GC do its marking
// 2. marking of the roots in category 2 by having the JS GC call us back
//    (via JS_SetExtraGCRootsTracer) and running TraceNativeBlackRoots
// 3. marking of the roots in category 3 by
//    TraceNativeGrayRootsInCollectingZones using an additional color (gray).
// 4. end of GC, GC can sweep its heap
//
// At some later point, when the cycle collector runs:
//
// 5. walk gray objects and add them to the cycle collector, cycle collect
//
// JS objects that are part of cycles the cycle collector breaks will be
// collected by the next JS GC.
//
// If WantAllTraces() is false the cycle collector will not traverse roots
// from category 1 or any JS objects held by them. Any JS objects they hold
// will already be marked by the JS GC and will thus be colored black
// themselves. Any C++ objects they hold will have a missing (untraversed)
// edge from the JS object to the C++ object and so it will be marked black
// too. This decreases the number of objects that the cycle collector has to
// deal with.
// To improve debugging, if WantAllTraces() is true all JS objects are
// traversed.

#include "mozilla/CycleCollectedJSRuntime.h"

#include <algorithm>
#include <utility>

#include "js/Debug.h"
#include "js/RealmOptions.h"
#include "js/friend/DumpFunctions.h"  // js::DumpHeap
#include "js/GCAPI.h"
#include "js/HeapAPI.h"
#include "js/Object.h"  // JS::GetClass, JS::GetCompartment, JS::GetPrivate
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/Warnings.h"            // JS::SetWarningReporter
#include "js/ShadowRealmCallbacks.h"
#include "js/SliceBudget.h"
#include "jsfriendapi.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DebuggerOnGCRunnable.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PerfStats.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/glean/XpcomMetrics.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseBinding.h"
#include "mozilla/dom/PromiseDebugging.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ShadowRealmGlobalScope.h"
#include "mozilla/dom/RegisterShadowRealmBindings.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollector.h"
#include "nsDOMJSUtils.h"
#include "nsExceptionHandler.h"
#include "nsJSUtils.h"
#include "nsWrapperCache.h"
#include "prenv.h"

#if defined(XP_MACOSX)
#  include "nsMacUtilsImpl.h"
#endif

#include "nsThread.h"
#include "nsThreadUtils.h"
#include "xpcpublic.h"

#ifdef NIGHTLY_BUILD
// For performance reasons, we make the JS Dev Error Interceptor a Nightly-only
// feature.
#  define MOZ_JS_DEV_ERROR_INTERCEPTOR = 1
#endif  // NIGHTLY_BUILD

using namespace mozilla;
using namespace mozilla::dom;

namespace mozilla {

struct DeferredFinalizeFunctionHolder {
  DeferredFinalizeFunction run;
  void* data;
};

class IncrementalFinalizeRunnable : public DiscardableRunnable {
  typedef AutoTArray<DeferredFinalizeFunctionHolder, 16> DeferredFinalizeArray;
  typedef CycleCollectedJSRuntime::DeferredFinalizerTable
      DeferredFinalizerTable;

  CycleCollectedJSRuntime* mRuntime;
  DeferredFinalizeArray mDeferredFinalizeFunctions;
  uint32_t mFinalizeFunctionToRun;
  bool mReleasing;

  static const PRTime SliceMillis = 5; /* ms */

 public:
  IncrementalFinalizeRunnable(CycleCollectedJSRuntime* aRt,
                              DeferredFinalizerTable& aFinalizerTable);
  virtual ~IncrementalFinalizeRunnable();

  void ReleaseNow(bool aLimited);

  NS_DECL_NSIRUNNABLE
};

}  // namespace mozilla

struct NoteWeakMapChildrenTracer : public JS::CallbackTracer {
  NoteWeakMapChildrenTracer(JSRuntime* aRt,
                            nsCycleCollectionNoteRootCallback& aCb)
      : JS::CallbackTracer(aRt, JS::TracerKind::Callback),
        mCb(aCb),
        mTracedAny(false),
        mMap(nullptr),
        mKey(nullptr),
        mKeyDelegate(nullptr) {}
  void onChild(JS::GCCellPtr aThing, const char* name) override;
  nsCycleCollectionNoteRootCallback& mCb;
  bool mTracedAny;
  JSObject* mMap;
  JS::GCCellPtr mKey;
  JSObject* mKeyDelegate;
};

void NoteWeakMapChildrenTracer::onChild(JS::GCCellPtr aThing,
                                        const char* name) {
  if (aThing.is<JSString>()) {
    return;
  }

  if (!JS::GCThingIsMarkedGrayInCC(aThing) && !mCb.WantAllTraces()) {
    return;
  }

  if (JS::IsCCTraceKind(aThing.kind())) {
    mCb.NoteWeakMapping(mMap, mKey, mKeyDelegate, aThing);
    mTracedAny = true;
  } else {
    JS::TraceChildren(this, aThing);
  }
}

struct NoteWeakMapsTracer : public js::WeakMapTracer {
  NoteWeakMapsTracer(JSRuntime* aRt, nsCycleCollectionNoteRootCallback& aCccb)
      : js::WeakMapTracer(aRt), mCb(aCccb), mChildTracer(aRt, aCccb) {}
  void trace(JSObject* aMap, JS::GCCellPtr aKey, JS::GCCellPtr aValue) override;
  nsCycleCollectionNoteRootCallback& mCb;
  NoteWeakMapChildrenTracer mChildTracer;
};

void NoteWeakMapsTracer::trace(JSObject* aMap, JS::GCCellPtr aKey,
                               JS::GCCellPtr aValue) {
  // If nothing that could be held alive by this entry is marked gray, return.
  if ((!aKey || !JS::GCThingIsMarkedGrayInCC(aKey)) &&
      MOZ_LIKELY(!mCb.WantAllTraces())) {
    if (!aValue || !JS::GCThingIsMarkedGrayInCC(aValue) ||
        aValue.is<JSString>()) {
      return;
    }
  }

  // The cycle collector can only properly reason about weak maps if it can
  // reason about the liveness of their keys, which in turn requires that
  // the key can be represented in the cycle collector graph.  All existing
  // uses of weak maps use either objects or scripts as keys, which are okay.
  MOZ_ASSERT(JS::IsCCTraceKind(aKey.kind()));

  // As an emergency fallback for non-debug builds, if the key is not
  // representable in the cycle collector graph, we treat it as marked.  This
  // can cause leaks, but is preferable to ignoring the binding, which could
  // cause the cycle collector to free live objects.
  if (!JS::IsCCTraceKind(aKey.kind())) {
    aKey = nullptr;
  }

  JSObject* kdelegate = nullptr;
  if (aKey.is<JSObject>()) {
    kdelegate = js::UncheckedUnwrapWithoutExpose(&aKey.as<JSObject>());
  }

  if (JS::IsCCTraceKind(aValue.kind())) {
    mCb.NoteWeakMapping(aMap, aKey, kdelegate, aValue);
  } else {
    mChildTracer.mTracedAny = false;
    mChildTracer.mMap = aMap;
    mChildTracer.mKey = aKey;
    mChildTracer.mKeyDelegate = kdelegate;

    if (!aValue.is<JSString>()) {
      JS::TraceChildren(&mChildTracer, aValue);
    }

    // The delegate could hold alive the key, so report something to the CC
    // if we haven't already.
    if (!mChildTracer.mTracedAny && aKey && JS::GCThingIsMarkedGrayInCC(aKey) &&
        kdelegate) {
      mCb.NoteWeakMapping(aMap, aKey, kdelegate, nullptr);
    }
  }
}

// Report whether the key or value of a weak mapping entry are gray but need to
// be marked black.
static void ShouldWeakMappingEntryBeBlack(JSObject* aMap, JS::GCCellPtr aKey,
                                          JS::GCCellPtr aValue,
                                          bool* aKeyShouldBeBlack,
                                          bool* aValueShouldBeBlack) {
  *aKeyShouldBeBlack = false;
  *aValueShouldBeBlack = false;

  // If nothing that could be held alive by this entry is marked gray, return.
  bool keyMightNeedMarking = aKey && JS::GCThingIsMarkedGrayInCC(aKey);
  bool valueMightNeedMarking = aValue && JS::GCThingIsMarkedGrayInCC(aValue) &&
                               aValue.kind() != JS::TraceKind::String;
  if (!keyMightNeedMarking && !valueMightNeedMarking) {
    return;
  }

  if (!JS::IsCCTraceKind(aKey.kind())) {
    aKey = nullptr;
  }

  if (keyMightNeedMarking && aKey.is<JSObject>()) {
    JSObject* kdelegate =
        js::UncheckedUnwrapWithoutExpose(&aKey.as<JSObject>());
    if (kdelegate && !JS::ObjectIsMarkedGray(kdelegate) &&
        (!aMap || !JS::ObjectIsMarkedGray(aMap))) {
      *aKeyShouldBeBlack = true;
    }
  }

  if (aValue && JS::GCThingIsMarkedGrayInCC(aValue) &&
      (!aKey || !JS::GCThingIsMarkedGrayInCC(aKey)) &&
      (!aMap || !JS::ObjectIsMarkedGray(aMap)) &&
      aValue.kind() != JS::TraceKind::Shape) {
    *aValueShouldBeBlack = true;
  }
}

struct FixWeakMappingGrayBitsTracer : public js::WeakMapTracer {
  explicit FixWeakMappingGrayBitsTracer(JSRuntime* aRt)
      : js::WeakMapTracer(aRt) {}

  void FixAll() {
    do {
      mAnyMarked = false;
      js::TraceWeakMaps(this);
    } while (mAnyMarked);
  }

  void trace(JSObject* aMap, JS::GCCellPtr aKey,
             JS::GCCellPtr aValue) override {
    bool keyShouldBeBlack;
    bool valueShouldBeBlack;
    ShouldWeakMappingEntryBeBlack(aMap, aKey, aValue, &keyShouldBeBlack,
                                  &valueShouldBeBlack);
    if (keyShouldBeBlack && JS::UnmarkGrayGCThingRecursively(aKey)) {
      mAnyMarked = true;
    }

    if (valueShouldBeBlack && JS::UnmarkGrayGCThingRecursively(aValue)) {
      mAnyMarked = true;
    }
  }

  MOZ_INIT_OUTSIDE_CTOR bool mAnyMarked;
};

#ifdef DEBUG
// Check whether weak maps are marked correctly according to the logic above.
struct CheckWeakMappingGrayBitsTracer : public js::WeakMapTracer {
  explicit CheckWeakMappingGrayBitsTracer(JSRuntime* aRt)
      : js::WeakMapTracer(aRt), mFailed(false) {}

  static bool Check(JSRuntime* aRt) {
    CheckWeakMappingGrayBitsTracer tracer(aRt);
    js::TraceWeakMaps(&tracer);
    return !tracer.mFailed;
  }

  void trace(JSObject* aMap, JS::GCCellPtr aKey,
             JS::GCCellPtr aValue) override {
    bool keyShouldBeBlack;
    bool valueShouldBeBlack;
    ShouldWeakMappingEntryBeBlack(aMap, aKey, aValue, &keyShouldBeBlack,
                                  &valueShouldBeBlack);

    if (keyShouldBeBlack) {
      fprintf(stderr, "Weak mapping key %p of map %p should be black\n",
              aKey.asCell(), aMap);
      mFailed = true;
    }

    if (valueShouldBeBlack) {
      fprintf(stderr, "Weak mapping value %p of map %p should be black\n",
              aValue.asCell(), aMap);
      mFailed = true;
    }
  }

  bool mFailed;
};
#endif  // DEBUG

static void CheckParticipatesInCycleCollection(JS::GCCellPtr aThing,
                                               const char* aName,
                                               void* aClosure) {
  bool* cycleCollectionEnabled = static_cast<bool*>(aClosure);

  if (*cycleCollectionEnabled) {
    return;
  }

  if (JS::IsCCTraceKind(aThing.kind()) && JS::GCThingIsMarkedGrayInCC(aThing)) {
    *cycleCollectionEnabled = true;
  }
}

NS_IMETHODIMP
JSGCThingParticipant::TraverseNative(void* aPtr,
                                     nsCycleCollectionTraversalCallback& aCb) {
  auto runtime = reinterpret_cast<CycleCollectedJSRuntime*>(
      reinterpret_cast<char*>(this) -
      offsetof(CycleCollectedJSRuntime, mGCThingCycleCollectorGlobal));

  JS::GCCellPtr cellPtr(aPtr, JS::GCThingTraceKind(aPtr));
  runtime->TraverseGCThing(CycleCollectedJSRuntime::TRAVERSE_FULL, cellPtr,
                           aCb);
  return NS_OK;
}

// NB: This is only used to initialize the participant in
// CycleCollectedJSRuntime. It should never be used directly.
static JSGCThingParticipant sGCThingCycleCollectorGlobal;

NS_IMETHODIMP
JSZoneParticipant::TraverseNative(void* aPtr,
                                  nsCycleCollectionTraversalCallback& aCb) {
  auto runtime = reinterpret_cast<CycleCollectedJSRuntime*>(
      reinterpret_cast<char*>(this) -
      offsetof(CycleCollectedJSRuntime, mJSZoneCycleCollectorGlobal));

  MOZ_ASSERT(!aCb.WantAllTraces());
  JS::Zone* zone = static_cast<JS::Zone*>(aPtr);

  runtime->TraverseZone(zone, aCb);
  return NS_OK;
}

struct TraversalTracer : public JS::CallbackTracer {
  TraversalTracer(JSRuntime* aRt, nsCycleCollectionTraversalCallback& aCb)
      : JS::CallbackTracer(aRt, JS::TracerKind::Callback,
                           JS::TraceOptions(JS::WeakMapTraceAction::Skip,
                                            JS::WeakEdgeTraceAction::Trace)),
        mCb(aCb) {}
  void onChild(JS::GCCellPtr aThing, const char* name) override;
  nsCycleCollectionTraversalCallback& mCb;
};

void TraversalTracer::onChild(JS::GCCellPtr aThing, const char* name) {
  // Checking strings and symbols for being gray is rather slow, and we don't
  // need either of them for the cycle collector.
  if (aThing.is<JSString>() || aThing.is<JS::Symbol>()) {
    return;
  }

  // Don't traverse non-gray objects, unless we want all traces.
  if (!JS::GCThingIsMarkedGrayInCC(aThing) && !mCb.WantAllTraces()) {
    return;
  }

  /*
   * This function needs to be careful to avoid stack overflow. Normally, when
   * IsCCTraceKind is true, the recursion terminates immediately as we just add
   * |thing| to the CC graph. So overflow is only possible when there are long
   * or cyclic chains of non-IsCCTraceKind GC things. Places where this can
   * occur use special APIs to handle such chains iteratively.
   */
  if (JS::IsCCTraceKind(aThing.kind())) {
    if (MOZ_UNLIKELY(mCb.WantDebugInfo())) {
      char buffer[200];
      context().getEdgeName(name, buffer, sizeof(buffer));
      mCb.NoteNextEdgeName(buffer);
    }
    mCb.NoteJSChild(aThing);
    return;
  }

  // Allow re-use of this tracer inside trace callback.
  JS::AutoClearTracingContext actc(this);

  if (aThing.is<js::Shape>()) {
    // The maximum depth of traversal when tracing a Shape is unbounded, due to
    // the parent pointers on the shape.
    JS_TraceShapeCycleCollectorChildren(this, aThing);
  } else {
    JS::TraceChildren(this, aThing);
  }
}

/*
 * The cycle collection participant for a Zone is intended to produce the same
 * results as if all of the gray GCthings in a zone were merged into a single
 * node, except for self-edges. This avoids the overhead of representing all of
 * the GCthings in the zone in the cycle collector graph, which should be much
 * faster if many of the GCthings in the zone are gray.
 *
 * Zone merging should not always be used, because it is a conservative
 * approximation of the true cycle collector graph that can incorrectly identify
 * some garbage objects as being live. For instance, consider two cycles that
 * pass through a zone, where one is garbage and the other is live. If we merge
 * the entire zone, the cycle collector will think that both are alive.
 *
 * We don't have to worry about losing track of a garbage cycle, because any
 * such garbage cycle incorrectly identified as live must contain at least one
 * C++ to JS edge, and XPConnect will always add the C++ object to the CC graph.
 * (This is in contrast to pure C++ garbage cycles, which must always be
 * properly identified, because we clear the purple buffer during every CC,
 * which may contain the last reference to a garbage cycle.)
 */

// NB: This is only used to initialize the participant in
// CycleCollectedJSRuntime. It should never be used directly.
static const JSZoneParticipant sJSZoneCycleCollectorGlobal;

static void JSObjectsTenuredCb(JS::GCContext* aGCContext, void* aData) {
  static_cast<CycleCollectedJSRuntime*>(aData)->JSObjectsTenured(aGCContext);
}

static void MozCrashWarningReporter(JSContext*, JSErrorReport*) {
  MOZ_CRASH("Why is someone touching JSAPI without an AutoJSAPI?");
}

JSHolderMap::Entry::Entry() : Entry(nullptr, nullptr, nullptr) {}

JSHolderMap::Entry::Entry(void* aHolder, nsScriptObjectTracer* aTracer,
                          JS::Zone* aZone)
    : mHolder(aHolder),
      mTracer(aTracer)
#ifdef DEBUG
      ,
      mZone(aZone)
#endif
{
}

void JSHolderMap::EntryVectorIter::Settle() {
  if (Done()) {
    return;
  }

  Entry* entry = &mIter.Get();

  // If the entry has been cleared, remove it and shrink the vector.
  if (!entry->mHolder && !mHolderMap.RemoveEntry(mVector, entry)) {
    // We removed the last entry, so reset the iterator to an empty one.
    mIter = EntryVector().Iter();
    MOZ_ASSERT(Done());
  }
}

JSHolderMap::Iter::Iter(JSHolderMap& aMap, WhichJSHolders aWhich)
    : mHolderMap(aMap), mIter(aMap, aMap.mAnyZoneJSHolders) {
  MOZ_RELEASE_ASSERT(!mHolderMap.mHasIterator);
  mHolderMap.mHasIterator = true;

  // Populate vector of zones to iterate after the any-zone holders.
  for (auto i = aMap.mPerZoneJSHolders.iter(); !i.done(); i.next()) {
    JS::Zone* zone = i.get().key();
    if (aWhich == AllJSHolders || JS::NeedGrayRootsForZone(i.get().key())) {
      MOZ_ALWAYS_TRUE(mZones.append(zone));
    }
  }

  Settle();
}

void JSHolderMap::Iter::Settle() {
  while (mIter.Done()) {
    if (mZone && mIter.Vector().IsEmpty()) {
      mHolderMap.mPerZoneJSHolders.remove(mZone);
    }

    mZone = nullptr;
    if (mZones.empty()) {
      break;
    }

    mZone = mZones.popCopy();
    EntryVector& vector = *mHolderMap.mPerZoneJSHolders.lookup(mZone)->value();
    new (&mIter) EntryVectorIter(mHolderMap, vector);
  }
}

void JSHolderMap::Iter::UpdateForRemovals() {
  mIter.Settle();
  Settle();
}

JSHolderMap::JSHolderMap() : mJSHolderMap(256) {}

bool JSHolderMap::RemoveEntry(EntryVector& aJSHolders, Entry* aEntry) {
  MOZ_ASSERT(aEntry);
  MOZ_ASSERT(!aEntry->mHolder);

  // Remove all dead entries from the end of the vector.
  while (!aJSHolders.GetLast().mHolder && &aJSHolders.GetLast() != aEntry) {
    aJSHolders.PopLast();
  }

  // Swap the element we want to remove with the last one and update the hash
  // table.
  Entry* lastEntry = &aJSHolders.GetLast();
  if (aEntry != lastEntry) {
    MOZ_ASSERT(lastEntry->mHolder);
    *aEntry = *lastEntry;
    MOZ_ASSERT(mJSHolderMap.has(aEntry->mHolder));
    MOZ_ALWAYS_TRUE(mJSHolderMap.put(aEntry->mHolder, aEntry));
  }

  aJSHolders.PopLast();

  // Return whether aEntry is still in the vector.
  return aEntry != lastEntry;
}

bool JSHolderMap::Has(void* aHolder) const { return mJSHolderMap.has(aHolder); }

nsScriptObjectTracer* JSHolderMap::Get(void* aHolder) const {
  auto ptr = mJSHolderMap.lookup(aHolder);
  if (!ptr) {
    return nullptr;
  }

  Entry* entry = ptr->value();
  MOZ_ASSERT(entry->mHolder == aHolder);
  return entry->mTracer;
}

nsScriptObjectTracer* JSHolderMap::Extract(void* aHolder) {
  MOZ_ASSERT(aHolder);

  auto ptr = mJSHolderMap.lookup(aHolder);
  if (!ptr) {
    return nullptr;
  }

  Entry* entry = ptr->value();
  MOZ_ASSERT(entry->mHolder == aHolder);
  nsScriptObjectTracer* tracer = entry->mTracer;

  // Clear the entry's contents. It will be removed the next time iteration
  // visits this entry.
  *entry = Entry();

  mJSHolderMap.remove(ptr);

  return tracer;
}

void JSHolderMap::Put(void* aHolder, nsScriptObjectTracer* aTracer,
                      JS::Zone* aZone) {
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(aTracer);

  // Don't associate multi-zone holders with a zone, even if one is supplied.
  if (!aTracer->IsSingleZoneJSHolder()) {
    aZone = nullptr;
  }

  auto ptr = mJSHolderMap.lookupForAdd(aHolder);
  if (ptr) {
    Entry* entry = ptr->value();
#ifdef DEBUG
    MOZ_ASSERT(entry->mHolder == aHolder);
    MOZ_ASSERT(entry->mTracer == aTracer,
               "Don't call HoldJSObjects in superclass ctors");
    if (aZone) {
      if (entry->mZone) {
        MOZ_ASSERT(entry->mZone == aZone);
      } else {
        entry->mZone = aZone;
      }
    }
#endif
    entry->mTracer = aTracer;
    return;
  }

  EntryVector* vector = &mAnyZoneJSHolders;
  if (aZone) {
    auto ptr = mPerZoneJSHolders.lookupForAdd(aZone);
    if (!ptr) {
      MOZ_ALWAYS_TRUE(
          mPerZoneJSHolders.add(ptr, aZone, MakeUnique<EntryVector>()));
    }
    vector = ptr->value().get();
  }

  vector->InfallibleAppend(Entry{aHolder, aTracer, aZone});
  MOZ_ALWAYS_TRUE(mJSHolderMap.add(ptr, aHolder, &vector->GetLast()));
}

size_t JSHolderMap::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;

  // We're deliberately not measuring anything hanging off the entries in
  // mJSHolderMap.
  n += mJSHolderMap.shallowSizeOfExcludingThis(aMallocSizeOf);
  n += mAnyZoneJSHolders.SizeOfExcludingThis(aMallocSizeOf);
  n += mPerZoneJSHolders.shallowSizeOfExcludingThis(aMallocSizeOf);
  for (auto i = mPerZoneJSHolders.iter(); !i.done(); i.next()) {
    n += i.get().value()->SizeOfExcludingThis(aMallocSizeOf);
  }

  return n;
}

JSHolderListEntry::JSHolderListEntry()
    : JSHolderListEntry(nullptr, nullptr, nullptr) {}

JSHolderListEntry::JSHolderListEntry(void* aHolder, JSHolderKey* aKey,
                                     nsScriptObjectTracer* aTracer)
    : mHolder(aHolder), mKey(aKey), mTracer(aTracer) {}

void JSHolderList::EntryVectorIter::Settle() {
  if (Done()) {
    return;
  }

  Entry* entry = &mIter.Get();

  // If the entry has been cleared, remove it and shrink the vector.
  if (!entry->mHolder && !mHolderList.RemoveEntry(mVector, entry)) {
    // We removed the last entry, so reset the iterator to an empty one.
    mIter = EntryVector().Iter();
    MOZ_ASSERT(Done());
  }
}

JSHolderList::Iter::Iter(JSHolderList& aList, WhichJSHolders aWhich)
    : mHolderList(aList), mIter(aList, aList.mJSHolders) {
  // aWhich is ignored since there are no per-zone holders in the list. Iterate
  // all holders every time.
  MOZ_RELEASE_ASSERT(!mHolderList.mHasIterator);
  mHolderList.mHasIterator = true;
}

void JSHolderList::Iter::UpdateForRemovals() { mIter.Settle(); }

JSHolderList::JSHolderList() {}

bool JSHolderList::RemoveEntry(EntryVector& aJSHolders, Entry* aEntry) {
  MOZ_ASSERT(aEntry);
  MOZ_ASSERT(!aEntry->mHolder);

  // Remove all dead entries from the end of the vector.
  while (!aJSHolders.GetLast().mHolder && &aJSHolders.GetLast() != aEntry) {
    aJSHolders.PopLast();
  }

  // Swap the element we want to remove with the last one and update the back
  // pointer.
  Entry* lastEntry = &aJSHolders.GetLast();
  if (aEntry != lastEntry) {
    MOZ_ASSERT(lastEntry->mHolder);
    *aEntry = *lastEntry;
    MOZ_ASSERT(aEntry->mKey->mEntry == lastEntry);
    aEntry->mKey->mEntry = aEntry;
  }

  aJSHolders.PopLast();

  // Return whether aEntry is still in the vector.
  return aEntry != lastEntry;
}

bool JSHolderList::Has(JSHolderKey* aKey) const {
  return aKey->mEntry != nullptr;
}

nsScriptObjectTracer* JSHolderList::Get(void* aHolder,
                                        JSHolderKey* aKey) const {
  Entry* entry = aKey->mEntry;
  if (!entry) {
    return nullptr;
  }

  MOZ_ASSERT(entry->mHolder == aHolder);
  return entry->mTracer;
}

nsScriptObjectTracer* JSHolderList::Extract(void* aHolder, JSHolderKey* aKey) {
  MOZ_ASSERT(aHolder);

  Entry* entry = aKey->mEntry;
  if (!entry) {
    return nullptr;
  }

  MOZ_ASSERT(entry->mHolder == aHolder);
  nsScriptObjectTracer* tracer = entry->mTracer;

  // Clear the back pointer to the entry.
  aKey->mEntry = nullptr;

  // Clear the entry's contents. It will be removed the next time iteration
  // visits this entry.
  *entry = Entry();

  return tracer;
}

void JSHolderList::Put(void* aHolder, nsScriptObjectTracer* aTracer,
                       JSHolderKey* aKey) {
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(aTracer);
  MOZ_ASSERT(aKey);

  Entry* entry = aKey->mEntry;
  if (entry) {
#ifdef DEBUG
    MOZ_ASSERT(entry->mHolder == aHolder);
    MOZ_ASSERT(entry->mTracer == aTracer,
               "Don't call HoldJSObjects in superclass ctors");
#endif
    entry->mTracer = aTracer;
    return;
  }

  mJSHolders.InfallibleAppend(Entry{aHolder, aKey, aTracer});
  aKey->mEntry = &mJSHolders.GetLast();
}

size_t JSHolderList::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  // We're deliberately not measuring anything hanging off the entries in
  // mJSHolders.
  return mJSHolders.SizeOfExcludingThis(aMallocSizeOf);
}

static bool InitializeShadowRealm(JSContext* aCx,
                                  JS::Handle<JSObject*> aGlobal) {
  MOZ_ASSERT(StaticPrefs::javascript_options_experimental_shadow_realms());

  JSAutoRealm ar(aCx, aGlobal);
  return dom::RegisterShadowRealmBindings(aCx, aGlobal);
}

static bool InstanceClassIsError(const JSClass* clasp) {
  if (clasp->isDOMClass()) {
    const DOMJSClass* domClass = DOMJSClass::FromJSClass(clasp);
    if (domClass->mInterfaceChain[0] == prototypes::id::DOMException ||
        domClass->mInterfaceChain[0] == prototypes::id::Exception) {
      return true;
    }
  }

  return false;
}

CycleCollectedJSRuntime::CycleCollectedJSRuntime(JSContext* aCx)
    : mContext(nullptr),
      mGCThingCycleCollectorGlobal(sGCThingCycleCollectorGlobal),
      mJSZoneCycleCollectorGlobal(sJSZoneCycleCollectorGlobal),
      mJSRuntime(JS_GetRuntime(aCx)),
      mHasPendingIdleGCTask(false),
      mPrevGCSliceCallback(nullptr),
      mTraceState(Nothing()),
      mOutOfMemoryState(OOMState::OK),
      mLargeAllocationFailureState(OOMState::OK)
#ifdef DEBUG
      ,
      mShutdownCalled(false)
#endif
{
  MOZ_COUNT_CTOR(CycleCollectedJSRuntime);
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(mJSRuntime);

#if defined(XP_MACOSX)
  if (!XRE_IsParentProcess()) {
    nsMacUtilsImpl::EnableTCSMIfAvailable();
  }
#endif

  if (!JS_AddExtraGCRootsTracer(aCx, TraceBlackJS, this)) {
    MOZ_CRASH("JS_AddExtraGCRootsTracer failed");
  }
  JS_SetGrayGCRootsTracer(aCx, TraceGrayJS, this);
  JS_SetGCCallback(aCx, GCCallback, this);
  mPrevGCSliceCallback = JS::SetGCSliceCallback(aCx, GCSliceCallback);

  JS::AddGCNurseryCollectionCallback(aCx, GCNurseryCollectionCallback, this);

  JS_SetObjectsTenuredCallback(aCx, JSObjectsTenuredCb, this);
  JS::SetOutOfMemoryCallback(aCx, OutOfMemoryCallback, this);
  JS::SetWaitCallback(mJSRuntime, BeforeWaitCallback, AfterWaitCallback,
                      sizeof(dom::AutoYieldJSThreadExecution));
  JS::SetWarningReporter(aCx, MozCrashWarningReporter);
  JS::SetShadowRealmInitializeGlobalCallback(aCx, InitializeShadowRealm);
  JS::SetShadowRealmGlobalCreationCallback(aCx, dom::NewShadowRealmGlobal);

  js::AutoEnterOOMUnsafeRegion::setAnnotateOOMAllocationSizeCallback(
      CrashReporter::AnnotateOOMAllocationSize);

  static js::DOMCallbacks DOMcallbacks = {InstanceClassHasProtoAtDepth,
                                          InstanceClassIsError};
  SetDOMCallbacks(aCx, &DOMcallbacks);
  js::SetScriptEnvironmentPreparer(aCx, &mEnvironmentPreparer);

  JS::dbg::SetDebuggerMallocSizeOf(aCx, moz_malloc_size_of);

#ifdef MOZ_JS_DEV_ERROR_INTERCEPTOR
  JS_SetErrorInterceptorCallback(mJSRuntime, &mErrorInterceptor);
#endif  // MOZ_JS_DEV_ERROR_INTERCEPTOR

  JS_SetDestroyZoneCallback(aCx, OnZoneDestroyed);
}

#ifdef NS_BUILD_REFCNT_LOGGING
class JSLeakTracer : public JS::CallbackTracer {
 public:
  explicit JSLeakTracer(JSRuntime* aRuntime)
      : JS::CallbackTracer(aRuntime, JS::TracerKind::Callback,
                           JS::WeakMapTraceAction::TraceKeysAndValues) {}

 private:
  void onChild(JS::GCCellPtr thing, const char* name) override {
    const char* kindName = JS::GCTraceKindToAscii(thing.kind());
    size_t size = JS::GCTraceKindSize(thing.kind());
    MOZ_LOG_CTOR(thing.asCell(), kindName, size);
  }
};
#endif

void CycleCollectedJSRuntime::Shutdown(JSContext* aCx) {
#ifdef MOZ_JS_DEV_ERROR_INTERCEPTOR
  mErrorInterceptor.Shutdown(mJSRuntime);
#endif  // MOZ_JS_DEV_ERROR_INTERCEPTOR

  // There should not be any roots left to trace at this point. Ensure any that
  // remain are flagged as leaks.
#ifdef NS_BUILD_REFCNT_LOGGING
  JSLeakTracer tracer(Runtime());
  TraceNativeBlackRoots(&tracer);
  TraceAllNativeGrayRoots(&tracer);
#endif

#ifdef DEBUG
  mShutdownCalled = true;
#endif

  JS_SetDestroyZoneCallback(aCx, nullptr);

  JS::RemoveGCNurseryCollectionCallback(aCx, GCNurseryCollectionCallback, this);
}

CycleCollectedJSRuntime::~CycleCollectedJSRuntime() {
  MOZ_COUNT_DTOR(CycleCollectedJSRuntime);
  MOZ_ASSERT(!mDeferredFinalizerTable.Count());
  MOZ_ASSERT(!mFinalizeRunnable);
  MOZ_ASSERT(mShutdownCalled);
  MOZ_ASSERT(mTraceState.is<Nothing>());
}

void CycleCollectedJSRuntime::SetContext(CycleCollectedJSContext* aContext) {
  MOZ_ASSERT(!mContext || !aContext, "Don't replace the context!");
  mContext = aContext;
}

size_t CycleCollectedJSRuntime::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return mJSHolderMap.SizeOfExcludingThis(aMallocSizeOf) +
         mJSHolderList.SizeOfExcludingThis(aMallocSizeOf);
}

void CycleCollectedJSRuntime::UnmarkSkippableJSHolders() {
  for (JSHolderMap::Iter entry(mJSHolderMap); !entry.Done(); entry.Next()) {
    entry->mTracer->CanSkip(entry->mHolder, true);
  }
  for (JSHolderList::Iter entry(mJSHolderList); !entry.Done(); entry.Next()) {
    entry->mTracer->CanSkip(entry->mHolder, true);
  }
}

void CycleCollectedJSRuntime::DescribeGCThing(
    bool aIsMarked, JS::GCCellPtr aThing,
    nsCycleCollectionTraversalCallback& aCb) const {
  if (!aCb.WantDebugInfo()) {
    aCb.DescribeGCedNode(aIsMarked, "JS Object");
    return;
  }

  char name[72];
  uint64_t compartmentAddress = 0;
  if (aThing.is<JSObject>()) {
    JSObject* obj = &aThing.as<JSObject>();
    compartmentAddress = (uint64_t)JS::GetCompartment(obj);
    const JSClass* clasp = JS::GetClass(obj);

    // Give the subclass a chance to do something
    if (DescribeCustomObjects(obj, clasp, name)) {
      // Nothing else to do!
    } else if (js::IsFunctionObject(obj)) {
      JSFunction* fun = JS_GetObjectFunction(obj);
      JSString* str = JS_GetMaybePartialFunctionDisplayId(fun);
      if (str) {
        JSLinearString* linear = JS_ASSERT_STRING_IS_LINEAR(str);
        nsAutoString chars;
        AssignJSLinearString(chars, linear);
        NS_ConvertUTF16toUTF8 fname(chars);
        SprintfLiteral(name, "JS Object (Function - %s)", fname.get());
      } else {
        SprintfLiteral(name, "JS Object (Function)");
      }
    } else {
      SprintfLiteral(name, "JS Object (%s)", clasp->name);
    }
  } else {
    SprintfLiteral(name, "%s", JS::GCTraceKindToAscii(aThing.kind()));
  }

  // Disable printing global for objects while we figure out ObjShrink fallout.
  aCb.DescribeGCedNode(aIsMarked, name, compartmentAddress);
}

void CycleCollectedJSRuntime::NoteGCThingJSChildren(
    JS::GCCellPtr aThing, nsCycleCollectionTraversalCallback& aCb) const {
  TraversalTracer trc(mJSRuntime, aCb);
  JS::TraceChildren(&trc, aThing);
}

void CycleCollectedJSRuntime::NoteGCThingXPCOMChildren(
    const JSClass* aClasp, JSObject* aObj,
    nsCycleCollectionTraversalCallback& aCb) const {
  MOZ_ASSERT(aClasp);
  MOZ_ASSERT(aClasp == JS::GetClass(aObj));

  JS::Rooted<JSObject*> obj(RootingCx(), aObj);

  if (NoteCustomGCThingXPCOMChildren(aClasp, obj, aCb)) {
    // Nothing else to do!
    return;
  }

  // XXX This test does seem fragile, we should probably allowlist classes
  //     that do hold a strong reference, but that might not be possible.
  if (aClasp->slot0IsISupports()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "JS::GetObjectISupports(obj)");
    aCb.NoteXPCOMChild(JS::GetObjectISupports<nsISupports>(obj));
    return;
  }

  const DOMJSClass* domClass = GetDOMClass(aClasp);
  if (domClass) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "UnwrapDOMObject(obj)");
    // It's possible that our object is an unforgeable holder object, in
    // which case it doesn't actually have a C++ DOM object associated with
    // it.  Use UnwrapPossiblyNotInitializedDOMObject, which produces null in
    // that case, since NoteXPCOMChild/NoteNativeChild are null-safe.
    if (domClass->mDOMObjectIsISupports) {
      aCb.NoteXPCOMChild(
          UnwrapPossiblyNotInitializedDOMObject<nsISupports>(obj));
    } else if (domClass->mParticipant) {
      aCb.NoteNativeChild(UnwrapPossiblyNotInitializedDOMObject<void>(obj),
                          domClass->mParticipant);
    }
    return;
  }

  if (IsRemoteObjectProxy(obj)) {
    auto handler =
        static_cast<const RemoteObjectProxyBase*>(js::GetProxyHandler(obj));
    return handler->NoteChildren(obj, aCb);
  }

  JS::Value value = js::MaybeGetScriptPrivate(obj);
  if (!value.isUndefined()) {
    aCb.NoteXPCOMChild(static_cast<nsISupports*>(value.toPrivate()));
  }
}

void CycleCollectedJSRuntime::TraverseGCThing(
    TraverseSelect aTs, JS::GCCellPtr aThing,
    nsCycleCollectionTraversalCallback& aCb) {
  bool isMarkedGray = JS::GCThingIsMarkedGrayInCC(aThing);

  if (aTs == TRAVERSE_FULL) {
    DescribeGCThing(!isMarkedGray, aThing, aCb);
  }

  // If this object is alive, then all of its children are alive. For JS
  // objects, the black-gray invariant ensures the children are also marked
  // black. For C++ objects, the ref count from this object will keep them
  // alive. Thus we don't need to trace our children, unless we are debugging
  // using WantAllTraces.
  if (!isMarkedGray && !aCb.WantAllTraces()) {
    return;
  }

  if (aTs == TRAVERSE_FULL) {
    NoteGCThingJSChildren(aThing, aCb);
  }

  if (aThing.is<JSObject>()) {
    JSObject* obj = &aThing.as<JSObject>();
    NoteGCThingXPCOMChildren(JS::GetClass(obj), obj, aCb);
  }
}

struct TraverseObjectShimClosure {
  nsCycleCollectionTraversalCallback& cb;
  CycleCollectedJSRuntime* self;
};

void CycleCollectedJSRuntime::TraverseZone(
    JS::Zone* aZone, nsCycleCollectionTraversalCallback& aCb) {
  /*
   * We treat the zone as being gray. We handle non-gray GCthings in the
   * zone by not reporting their children to the CC. The black-gray invariant
   * ensures that any JS children will also be non-gray, and thus don't need to
   * be added to the graph. For C++ children, not representing the edge from the
   * non-gray JS GCthings to the C++ object will keep the child alive.
   *
   * We don't allow zone merging in a WantAllTraces CC, because then these
   * assumptions don't hold.
   */
  aCb.DescribeGCedNode(false, "JS Zone");

  /*
   * Every JS child of everything in the zone is either in the zone
   * or is a cross-compartment wrapper. In the former case, we don't need to
   * represent these edges in the CC graph because JS objects are not ref
   * counted. In the latter case, the JS engine keeps a map of these wrappers,
   * which we iterate over. Edges between compartments in the same zone will add
   * unnecessary loop edges to the graph (bug 842137).
   */
  TraversalTracer trc(mJSRuntime, aCb);
  js::TraceGrayWrapperTargets(&trc, aZone);

  /*
   * To find C++ children of things in the zone, we scan every JS Object in
   * the zone. Only JS Objects can have C++ children.
   */
  TraverseObjectShimClosure closure = {aCb, this};
  js::IterateGrayObjects(aZone, TraverseObjectShim, &closure);
}

/* static */
void CycleCollectedJSRuntime::TraverseObjectShim(
    void* aData, JS::GCCellPtr aThing, const JS::AutoRequireNoGC& nogc) {
  TraverseObjectShimClosure* closure =
      static_cast<TraverseObjectShimClosure*>(aData);

  MOZ_ASSERT(aThing.is<JSObject>());
  closure->self->TraverseGCThing(CycleCollectedJSRuntime::TRAVERSE_CPP, aThing,
                                 closure->cb);
}

void CycleCollectedJSRuntime::TraverseNativeRoots(
    nsCycleCollectionNoteRootCallback& aCb) {
  // NB: This is here just to preserve the existing XPConnect order. I doubt it
  // would hurt to do this after the JS holders.
  TraverseAdditionalNativeRoots(aCb);

  TraverseJSHolders<JSHolderMap>(mJSHolderMap, aCb);
  TraverseJSHolders<JSHolderList>(mJSHolderList, aCb);
}

template <typename ContainerT>
void CycleCollectedJSRuntime::TraverseJSHolders(
    ContainerT& aHolders, nsCycleCollectionNoteRootCallback& aCb) {
  for (typename ContainerT::Iter entry(aHolders); !entry.Done(); entry.Next()) {
    void* holder = entry->mHolder;
    nsScriptObjectTracer* tracer = entry->mTracer;

    bool noteRoot = false;
    if (MOZ_UNLIKELY(aCb.WantAllTraces())) {
      noteRoot = true;
    } else {
      tracer->Trace(holder,
                    TraceCallbackFunc(CheckParticipatesInCycleCollection),
                    &noteRoot);
    }

    if (noteRoot) {
      aCb.NoteNativeRoot(holder, tracer);
    }
  }
}

/* static */
void CycleCollectedJSRuntime::TraceBlackJS(JSTracer* aTracer, void* aData) {
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  self->TraceNativeBlackRoots(aTracer);
}

/* static */
bool CycleCollectedJSRuntime::TraceGrayJS(JSTracer* aTracer,
                                          JS::SliceBudget& budget,
                                          void* aData) {
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  // Mark these roots as gray so the CC can walk them later.

  WhichJSHolders which = AllJSHolders;

  // Only trace holders in collecting zones when marking, except if we are
  // collecting the atoms zone since any holder may point into that zone.
  if (aTracer->isMarkingTracer() &&
      !JS::AtomsZoneIsCollecting(self->Runtime())) {
    which = JSHoldersRequiredForGrayMarking;
  }

  return self->TraceNativeGrayRoots(aTracer, which, budget);
}

/* static */
void CycleCollectedJSRuntime::GCCallback(JSContext* aContext,
                                         JSGCStatus aStatus,
                                         JS::GCReason aReason, void* aData) {
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  MOZ_ASSERT(CycleCollectedJSContext::Get()->Context() == aContext);
  MOZ_ASSERT(CycleCollectedJSContext::Get()->Runtime() == self);

  self->OnGC(aContext, aStatus, aReason);
}

struct GCMajorMarker : public BaseMarkerType<GCMajorMarker> {
  static constexpr const char* Name = "GCMajor";
  static constexpr const char* Description =
      "Summary data for an entire major GC, encompassing a set of "
      "incremental slices. The main thread is not blocked for the "
      "entire major GC interval, only for the individual slices.";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"timings", MS::InputType::CString, "GC timings"}};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineMemory};
  static constexpr MS::ETWMarkerGroup Group = MS::ETWMarkerGroup::Memory;

  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const mozilla::ProfilerString8View& aTimingJSON) {
    if (aTimingJSON.Length() != 0) {
      aWriter.SplicedJSONProperty("timings", aTimingJSON);
    } else {
      aWriter.NullProperty("timings");
    }
  }
};

/* static */
void CycleCollectedJSRuntime::GCSliceCallback(JSContext* aContext,
                                              JS::GCProgress aProgress,
                                              const JS::GCDescription& aDesc) {
  CycleCollectedJSRuntime* self = CycleCollectedJSRuntime::Get();
  MOZ_ASSERT(CycleCollectedJSContext::Get()->Context() == aContext);

  if (profiler_thread_is_being_profiled_for_markers()) {
    if (aProgress == JS::GC_CYCLE_END) {
      profiler_add_marker("GCMajor", baseprofiler::category::GCCC,
                          MarkerTiming::Interval(aDesc.startTime(aContext),
                                                 aDesc.endTime(aContext)),
                          GCMajorMarker{},
                          ProfilerString8View::WrapNullTerminatedString(
                              aDesc.formatJSONProfiler(aContext).get()));
    } else if (aProgress == JS::GC_SLICE_END) {
      struct GCSliceMarker {
        static constexpr mozilla::Span<const char> MarkerTypeName() {
          return mozilla::MakeStringSpan("GCSlice");
        }
        static void StreamJSONMarkerData(
            mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
            const mozilla::ProfilerString8View& aTimingJSON) {
          if (aTimingJSON.Length() != 0) {
            aWriter.SplicedJSONProperty("timings", aTimingJSON);
          } else {
            aWriter.NullProperty("timings");
          }
        }
        static mozilla::MarkerSchema MarkerTypeDisplay() {
          using MS = mozilla::MarkerSchema;
          MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable,
                    MS::Location::TimelineMemory};
          schema.AddStaticLabelValue(
              "Description",
              "One slice of an incremental garbage collection (GC). The main "
              "thread is blocked during this time.");
          // No display instructions here, there is special handling in the
          // front-end.
          return schema;
        }
      };

      profiler_add_marker("GCSlice", baseprofiler::category::GCCC,
                          MarkerTiming::Interval(aDesc.lastSliceStart(aContext),
                                                 aDesc.lastSliceEnd(aContext)),
                          GCSliceMarker{},
                          ProfilerString8View::WrapNullTerminatedString(
                              aDesc.sliceToJSONProfiler(aContext).get()));
    }
  }

  if (aProgress == JS::GC_CYCLE_END &&
      JS::dbg::FireOnGarbageCollectionHookRequired(aContext)) {
    JS::GCReason reason = aDesc.reason_;
    Unused << NS_WARN_IF(
        NS_FAILED(DebuggerOnGCRunnable::Enqueue(aContext, aDesc)) &&
        reason != JS::GCReason::SHUTDOWN_CC &&
        reason != JS::GCReason::DESTROY_RUNTIME &&
        reason != JS::GCReason::XPCONNECT_SHUTDOWN);
  }

  if (self->mPrevGCSliceCallback) {
    self->mPrevGCSliceCallback(aContext, aProgress, aDesc);
  }
}

/* static */
void CycleCollectedJSRuntime::GCNurseryCollectionCallback(
    JSContext* aContext, JS::GCNurseryProgress aProgress, JS::GCReason aReason,
    void* data) {
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(data);
  MOZ_ASSERT(self->GetContext()->Context() == aContext);

  TimeStamp now = TimeStamp::Now();
  if (aProgress == JS::GCNurseryProgress::GC_NURSERY_COLLECTION_START) {
    self->mLatestNurseryCollectionStart = now;
  } else if (aProgress == JS::GCNurseryProgress::GC_NURSERY_COLLECTION_END) {
    PerfStats::RecordMeasurement(PerfStats::Metric::MinorGC,
                                 now - self->mLatestNurseryCollectionStart);
  }

  if (aProgress == JS::GCNurseryProgress::GC_NURSERY_COLLECTION_END &&
      profiler_thread_is_being_profiled_for_markers()) {
    struct GCMinorMarker {
      static constexpr mozilla::Span<const char> MarkerTypeName() {
        return mozilla::MakeStringSpan("GCMinor");
      }
      static void StreamJSONMarkerData(
          mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
          const mozilla::ProfilerString8View& aTimingJSON) {
        if (aTimingJSON.Length() != 0) {
          aWriter.SplicedJSONProperty("nursery", aTimingJSON);
        } else {
          aWriter.NullProperty("nursery");
        }
      }
      static mozilla::MarkerSchema MarkerTypeDisplay() {
        using MS = mozilla::MarkerSchema;
        MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable,
                  MS::Location::TimelineMemory};
        schema.AddStaticLabelValue(
            "Description",
            "A minor GC (aka nursery collection) to clear out the buffer used "
            "for recent allocations and move surviving data to the tenured "
            "(long-lived) heap.");
        // No display instructions here, there is special handling in the
        // front-end.
        return schema;
      }
    };

    profiler_add_marker(
        "GCMinor", baseprofiler::category::GCCC,
        MarkerTiming::Interval(self->mLatestNurseryCollectionStart, now),
        GCMinorMarker{},
        ProfilerString8View::WrapNullTerminatedString(
            JS::MinorGcToJSON(aContext).get()));
  }
}

/* static */
void CycleCollectedJSRuntime::OutOfMemoryCallback(JSContext* aContext,
                                                  void* aData) {
  CycleCollectedJSRuntime* self = static_cast<CycleCollectedJSRuntime*>(aData);

  MOZ_ASSERT(CycleCollectedJSContext::Get()->Context() == aContext);
  MOZ_ASSERT(CycleCollectedJSContext::Get()->Runtime() == self);

  self->OnOutOfMemory();
}

/* static */
void* CycleCollectedJSRuntime::BeforeWaitCallback(uint8_t* aMemory) {
  MOZ_ASSERT(aMemory);

  // aMemory is stack allocated memory to contain our RAII object. This allows
  // for us to avoid allocations on the heap during this callback.
  return new (aMemory) dom::AutoYieldJSThreadExecution;
}

/* static */
void CycleCollectedJSRuntime::AfterWaitCallback(void* aCookie) {
  MOZ_ASSERT(aCookie);
  static_cast<dom::AutoYieldJSThreadExecution*>(aCookie)
      ->~AutoYieldJSThreadExecution();
}

void CycleCollectedJSRuntime::TraceNativeBlackRoots(JSTracer* aTracer) {
  if (CycleCollectedJSContext* context = GetContext()) {
    context->TraceMicroTasks(aTracer);
  }
  TraceAdditionalNativeBlackRoots(aTracer);
}

struct JsGcTracer : public TraceCallbacks {
  virtual void Trace(JS::Heap<JS::Value>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(JS::Heap<jsid>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(JS::Heap<JSObject*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(nsWrapperCache* aPtr, const char* aName,
                     void* aClosure) const override {
    aPtr->TraceWrapper(static_cast<JSTracer*>(aClosure), aName);
  }
  virtual void Trace(JS::TenuredHeap<JSObject*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(JS::Heap<JSString*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(JS::Heap<JSScript*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
  virtual void Trace(JS::Heap<JSFunction*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::TraceEdge(static_cast<JSTracer*>(aClosure), aPtr, aName);
  }
};

void mozilla::TraceScriptHolder(nsISupports* aHolder, JSTracer* aTracer) {
  nsXPCOMCycleCollectionParticipant* participant = nullptr;
  CallQueryInterface(aHolder, &participant);
  participant->Trace(aHolder, JsGcTracer(), aTracer);
}

#if defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION) || defined(DEBUG)
#  define CHECK_SINGLE_ZONE_JS_HOLDERS
#endif

#ifdef CHECK_SINGLE_ZONE_JS_HOLDERS

// A tracer that checks that a JS holder only holds JS GC things in a single
// JS::Zone.
struct CheckZoneTracer : public TraceCallbacks {
  const char* mClassName;
  mutable JS::Zone* mZone;

  explicit CheckZoneTracer(const char* aClassName, JS::Zone* aZone = nullptr)
      : mClassName(aClassName), mZone(aZone) {}

  void checkZone(JS::Zone* aZone, const char* aName) const {
    if (JS::IsAtomsZone(aZone)) {
      // Any holder may contain pointers into the atoms zone.
      return;
    }

    if (!mZone) {
      mZone = aZone;
      return;
    }

    if (aZone == mZone) {
      return;
    }

    // Most JS holders only contain pointers to GC things in a single zone. We
    // group holders by referent zone where possible, allowing us to improve GC
    // performance by only tracing holders for zones that are being collected.
    //
    // Additionally, pointers from any holder into the atoms zone are allowed
    // since all holders are traced when we collect the atoms zone.
    //
    // If you added a holder that has pointers into multiple zones do not
    // use NS_IMPL_CYCLE_COLLECTION_SINGLE_ZONE_SCRIPT_HOLDER_CLASS.
    MOZ_CRASH_UNSAFE_PRINTF(
        "JS holder %s contains pointers to GC things in more than one zone ("
        "found in %s)\n",
        mClassName, aName);
  }

  virtual void Trace(JS::Heap<JS::Value>* aPtr, const char* aName,
                     void* aClosure) const override {
    JS::Value value = aPtr->unbarrieredGet();
    if (value.isGCThing()) {
      checkZone(JS::GetGCThingZone(value.toGCCellPtr()), aName);
    }
  }
  virtual void Trace(JS::Heap<jsid>* aPtr, const char* aName,
                     void* aClosure) const override {
    jsid id = aPtr->unbarrieredGet();
    if (id.isGCThing()) {
      MOZ_ASSERT(JS::IsAtomsZone(JS::GetTenuredGCThingZone(id.toGCCellPtr())));
    }
  }
  virtual void Trace(JS::Heap<JSObject*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JSObject* obj = aPtr->unbarrieredGet();
    if (obj) {
      checkZone(js::GetObjectZoneFromAnyThread(obj), aName);
    }
  }
  virtual void Trace(nsWrapperCache* aPtr, const char* aName,
                     void* aClosure) const override {
    JSObject* obj = aPtr->GetWrapperPreserveColor();
    if (obj) {
      checkZone(js::GetObjectZoneFromAnyThread(obj), aName);
    }
  }
  virtual void Trace(JS::TenuredHeap<JSObject*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JSObject* obj = aPtr->unbarrieredGetPtr();
    if (obj) {
      checkZone(js::GetObjectZoneFromAnyThread(obj), aName);
    }
  }
  virtual void Trace(JS::Heap<JSString*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JSString* str = aPtr->unbarrieredGet();
    if (str) {
      checkZone(JS::GetStringZone(str), aName);
    }
  }
  virtual void Trace(JS::Heap<JSScript*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JSScript* script = aPtr->unbarrieredGet();
    if (script) {
      checkZone(JS::GetTenuredGCThingZone(JS::GCCellPtr(script)), aName);
    }
  }
  virtual void Trace(JS::Heap<JSFunction*>* aPtr, const char* aName,
                     void* aClosure) const override {
    JSFunction* fun = aPtr->unbarrieredGet();
    if (fun) {
      checkZone(js::GetObjectZoneFromAnyThread(JS_GetFunctionObject(fun)),
                aName);
    }
  }
};

static inline void CheckHolderIsSingleZone(
    void* aHolder, nsCycleCollectionParticipant* aParticipant,
    JS::Zone* aZone) {
  CheckZoneTracer tracer(aParticipant->ClassName(), aZone);
  aParticipant->Trace(aHolder, tracer, nullptr);
}

#endif

static inline bool ShouldCheckSingleZoneHolders() {
#if defined(DEBUG)
  return true;
#elif defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION)
  // Don't check every time to avoid performance impact.
  return rand() % 256 == 0;
#else
  return false;
#endif
}

#ifdef NS_BUILD_REFCNT_LOGGING
void CycleCollectedJSRuntime::TraceAllNativeGrayRoots(JSTracer* aTracer) {
  MOZ_RELEASE_ASSERT(mTraceState.is<Nothing>());
  JS::SliceBudget budget = JS::SliceBudget::unlimited();
  MOZ_ALWAYS_TRUE(TraceNativeGrayRoots(aTracer, AllJSHolders, budget));
}
#endif

bool CycleCollectedJSRuntime::TraceNativeGrayRoots(JSTracer* aTracer,
                                                   WhichJSHolders aWhich,
                                                   JS::SliceBudget& aBudget) {
  // Holders may have been removed between slices, so we may need to update
  // the iterator.
  if (mTraceState.is<JSHolderMap::Iter>()) {
    mTraceState.as<JSHolderMap::Iter>().UpdateForRemovals();
  } else if (mTraceState.is<JSHolderList::Iter>()) {
    mTraceState.as<JSHolderList::Iter>().UpdateForRemovals();
  }

  if (mTraceState.is<Nothing>()) {
    // NB: This is here just to preserve the existing XPConnect order. I doubt
    // it would hurt to do this after the JS holders.
    TraceAdditionalNativeGrayRoots(aTracer);

    mTraceState.emplace<JSHolderMap::Iter>(mJSHolderMap, aWhich);
    aBudget.forceCheck();
  }

  if (mTraceState.is<JSHolderMap::Iter>()) {
    auto& iter = mTraceState.as<JSHolderMap::Iter>();
    if (!TraceJSHolders(aTracer, iter, aBudget)) {
      return false;  // Yield.
    }

    mTraceState.emplace<JSHolderList::Iter>(mJSHolderList, aWhich);
  }

  if (mTraceState.is<JSHolderList::Iter>()) {
    auto& iter = mTraceState.as<JSHolderList::Iter>();
    if (!TraceJSHolders(aTracer, iter, aBudget)) {
      return false;  // Yield.
    }

    mTraceState.emplace<Nothing>();
  }

  return true;  // Finished.
}

class GetHolderAddressFunctor : public JS::TracingContext::Functor {
 public:
  GetHolderAddressFunctor() = default;

  virtual void operator()(JS::TracingContext* aTrc, const char* aName,
                          char* aBuf, size_t aBufSize) override {
    SprintfBuf(aBuf, aBufSize, "%s, holder 0x%p", aName, mHolder);
  }

  void SetHolder(void* aHolder) { mHolder = aHolder; }

 private:
  void* mHolder = nullptr;
};

template <typename IterT>
bool CycleCollectedJSRuntime::TraceJSHolders(JSTracer* aTracer, IterT& aIter,
                                             JS::SliceBudget& aBudget) {
  bool checkSingleZoneHolders = ShouldCheckSingleZoneHolders();
  GetHolderAddressFunctor functor;
  JS::AutoTracingDetails tracingDetails(aTracer, functor);

  while (!aIter.Done() && !aBudget.isOverBudget()) {
    void* holder = aIter->mHolder;
    nsScriptObjectTracer* tracer = aIter->mTracer;

#ifdef CHECK_SINGLE_ZONE_JS_HOLDERS
    if (checkSingleZoneHolders && tracer->IsSingleZoneJSHolder()) {
      CheckHolderIsSingleZone(holder, tracer, aIter.Zone());
    }
#else
    Unused << checkSingleZoneHolders;
#endif

    functor.SetHolder(holder);
    tracer->Trace(holder, JsGcTracer(), aTracer);
    functor.SetHolder(nullptr);

    aIter.Next();
    aBudget.step();
  }

  return aIter.Done();
}

void CycleCollectedJSRuntime::AddJSHolder(void* aHolder,
                                          nsScriptObjectTracer* aTracer,
                                          JS::Zone* aZone) {
  mJSHolderMap.Put(aHolder, aTracer, aZone);
}

void CycleCollectedJSRuntime::AddJSHolderWithKey(void* aHolder,
                                                 nsScriptObjectTracer* aTracer,
                                                 JSHolderKey* aKey) {
  MOZ_ASSERT(!mJSHolderMap.Has(aHolder));
  mJSHolderList.Put(aHolder, aTracer, aKey);
}

struct ClearJSHolder : public TraceCallbacks {
  virtual void Trace(JS::Heap<JS::Value>* aPtr, const char*,
                     void*) const override {
    aPtr->setUndefined();
  }

  virtual void Trace(JS::Heap<jsid>* aPtr, const char*, void*) const override {
    *aPtr = JS::PropertyKey::Void();
  }

  virtual void Trace(JS::Heap<JSObject*>* aPtr, const char*,
                     void*) const override {
    *aPtr = nullptr;
  }

  virtual void Trace(nsWrapperCache* aPtr, const char* aName,
                     void* aClosure) const override {
    aPtr->ClearWrapper();
  }

  virtual void Trace(JS::TenuredHeap<JSObject*>* aPtr, const char*,
                     void*) const override {
    *aPtr = nullptr;
  }

  virtual void Trace(JS::Heap<JSString*>* aPtr, const char*,
                     void*) const override {
    *aPtr = nullptr;
  }

  virtual void Trace(JS::Heap<JSScript*>* aPtr, const char*,
                     void*) const override {
    *aPtr = nullptr;
  }

  virtual void Trace(JS::Heap<JSFunction*>* aPtr, const char*,
                     void*) const override {
    *aPtr = nullptr;
  }
};

void CycleCollectedJSRuntime::RemoveJSHolder(void* aHolder) {
  nsScriptObjectTracer* tracer = mJSHolderMap.Extract(aHolder);
  if (tracer) {
    // Bug 1531951: The analysis can't see through the virtual call but we know
    // that the ClearJSHolder tracer will never GC.
    JS::AutoSuppressGCAnalysis nogc;
    tracer->Trace(aHolder, ClearJSHolder(), nullptr);
  }
}

void CycleCollectedJSRuntime::RemoveJSHolderWithKey(void* aHolder,
                                                    JSHolderKey* aKey) {
  MOZ_ASSERT(!mJSHolderMap.Has(aHolder));

  nsScriptObjectTracer* tracer = mJSHolderList.Extract(aHolder, aKey);
  if (tracer) {
    // Bug 1531951: The analysis can't see through the virtual call but we know
    // that the ClearJSHolder tracer will never GC.
    JS::AutoSuppressGCAnalysis nogc;
    tracer->Trace(aHolder, ClearJSHolder(), nullptr);
  }
}

#ifdef DEBUG
static void AssertNoGcThing(JS::GCCellPtr aGCThing, const char* aName,
                            void* aClosure) {
  MOZ_ASSERT(!aGCThing);
}

void CycleCollectedJSRuntime::AssertNoObjectsToTrace(void* aPossibleJSHolder) {
  nsScriptObjectTracer* tracer = mJSHolderMap.Get(aPossibleJSHolder);
  if (tracer) {
    tracer->Trace(aPossibleJSHolder, TraceCallbackFunc(AssertNoGcThing),
                  nullptr);
  }
}
#endif

nsCycleCollectionParticipant* CycleCollectedJSRuntime::GCThingParticipant() {
  return &mGCThingCycleCollectorGlobal;
}

nsCycleCollectionParticipant* CycleCollectedJSRuntime::ZoneParticipant() {
  return &mJSZoneCycleCollectorGlobal;
}

nsresult CycleCollectedJSRuntime::TraverseRoots(
    nsCycleCollectionNoteRootCallback& aCb) {
  TraverseNativeRoots(aCb);

  NoteWeakMapsTracer trc(mJSRuntime, aCb);
  js::TraceWeakMaps(&trc);

  return NS_OK;
}

bool CycleCollectedJSRuntime::UsefulToMergeZones() const { return false; }

void CycleCollectedJSRuntime::FixWeakMappingGrayBits() const {
  MOZ_ASSERT(!JS::IsIncrementalGCInProgress(mJSRuntime),
             "Don't call FixWeakMappingGrayBits during a GC.");
  FixWeakMappingGrayBitsTracer fixer(mJSRuntime);
  fixer.FixAll();
}

void CycleCollectedJSRuntime::CheckGrayBits() const {
  MOZ_ASSERT(!JS::IsIncrementalGCInProgress(mJSRuntime),
             "Don't call CheckGrayBits during a GC.");

#ifndef ANDROID
  // Bug 1346874 - The gray state check is expensive. Android tests are already
  // slow enough that this check can easily push them over the threshold to a
  // timeout.

  MOZ_ASSERT(js::CheckGrayMarkingState(mJSRuntime));
  MOZ_ASSERT(CheckWeakMappingGrayBitsTracer::Check(mJSRuntime));
#endif
}

bool CycleCollectedJSRuntime::AreGCGrayBitsValid() const {
  return js::AreGCGrayBitsValid(mJSRuntime);
}

void CycleCollectedJSRuntime::GarbageCollect(JS::GCOptions aOptions,
                                             JS::GCReason aReason) const {
  JSContext* cx = CycleCollectedJSContext::Get()->Context();
  JS::PrepareForFullGC(cx);
  JS::NonIncrementalGC(cx, aOptions, aReason);
}

void CycleCollectedJSRuntime::JSObjectsTenured(JS::GCContext* aGCContext) {
  NurseryObjectsVector objects;
  std::swap(objects, mNurseryObjects);

  for (auto iter = objects.Iter(); !iter.Done(); iter.Next()) {
    nsWrapperCache* cache = iter.Get();
    JSObject* wrapper = cache->GetWrapperMaybeDead();
    MOZ_DIAGNOSTIC_ASSERT(wrapper);

    if (js::gc::InCollectedNurseryRegion(wrapper)) {
      MOZ_ASSERT(!cache->PreservingWrapper());
      const JSClass* jsClass = JS::GetClass(wrapper);
      jsClass->doFinalize(aGCContext, wrapper);
      continue;
    }

    if (js::gc::IsInsideNursery(wrapper)) {
      mNurseryObjects.InfallibleAppend(cache);
    }
  }

  if (!mFinalizeRunnable) {
    FinalizeDeferredThings(FinalizeIncrementally);
  }
}

void CycleCollectedJSRuntime::NurseryWrapperAdded(nsWrapperCache* aCache) {
  MOZ_ASSERT(aCache);
  MOZ_ASSERT(aCache->GetWrapperMaybeDead());
  MOZ_ASSERT(!JS::ObjectIsTenured(aCache->GetWrapperMaybeDead()));
  mNurseryObjects.InfallibleAppend(aCache);
}

void CycleCollectedJSRuntime::DeferredFinalize(
    DeferredFinalizeAppendFunction aAppendFunc, DeferredFinalizeFunction aFunc,
    void* aThing) {
  // Tell the analysis that the function pointers will not GC.
  JS::AutoSuppressGCAnalysis suppress;
  mDeferredFinalizerTable.WithEntryHandle(aFunc, [&](auto&& entry) {
    if (entry) {
      aAppendFunc(entry.Data(), aThing);
    } else {
      entry.Insert(aAppendFunc(nullptr, aThing));
    }
  });
}

void CycleCollectedJSRuntime::DeferredFinalize(nsISupports* aSupports) {
  typedef DeferredFinalizerImpl<nsISupports> Impl;
  DeferredFinalize(Impl::AppendDeferredFinalizePointer, Impl::DeferredFinalize,
                   aSupports);
}

void CycleCollectedJSRuntime::DumpJSHeap(FILE* aFile) {
  JSContext* cx = CycleCollectedJSContext::Get()->Context();

  mozilla::MallocSizeOf mallocSizeOf =
      PR_GetEnv("MOZ_GC_LOG_SIZE") ? moz_malloc_size_of : nullptr;
  js::DumpHeap(cx, aFile, js::CollectNurseryBeforeDump, mallocSizeOf);
}

IncrementalFinalizeRunnable::IncrementalFinalizeRunnable(
    CycleCollectedJSRuntime* aRt, DeferredFinalizerTable& aFinalizers)
    : DiscardableRunnable("IncrementalFinalizeRunnable"),
      mRuntime(aRt),
      mFinalizeFunctionToRun(0),
      mReleasing(false) {
  for (auto iter = aFinalizers.Iter(); !iter.Done(); iter.Next()) {
    DeferredFinalizeFunction& function = iter.Key();
    void*& data = iter.Data();

    DeferredFinalizeFunctionHolder* holder =
        mDeferredFinalizeFunctions.AppendElement();
    holder->run = function;
    holder->data = data;

    iter.Remove();
  }
  MOZ_ASSERT(mDeferredFinalizeFunctions.Length());
}

IncrementalFinalizeRunnable::~IncrementalFinalizeRunnable() {
  MOZ_ASSERT(!mDeferredFinalizeFunctions.Length());
  MOZ_ASSERT(!mRuntime);
}

void IncrementalFinalizeRunnable::ReleaseNow(bool aLimited) {
  if (mReleasing) {
    NS_WARNING("Re-entering ReleaseNow");
    return;
  }
  {
    AUTO_PROFILER_LABEL("IncrementalFinalizeRunnable::ReleaseNow",
                        GCCC_Finalize);

    mozilla::AutoRestore<bool> ar(mReleasing);
    mReleasing = true;
    MOZ_ASSERT(mDeferredFinalizeFunctions.Length() != 0,
               "We should have at least ReleaseSliceNow to run");
    MOZ_ASSERT(mFinalizeFunctionToRun < mDeferredFinalizeFunctions.Length(),
               "No more finalizers to run?");

    TimeDuration sliceTime = TimeDuration::FromMilliseconds(SliceMillis);
    TimeStamp started = aLimited ? TimeStamp::Now() : TimeStamp();
    bool timeout = false;
    do {
      const DeferredFinalizeFunctionHolder& function =
          mDeferredFinalizeFunctions[mFinalizeFunctionToRun];
      if (aLimited) {
        bool done = false;
        while (!timeout && !done) {
          /*
           * We don't want to read the clock too often, so we try to
           * release slices of 100 items.
           */
          done = function.run(100, function.data);
          timeout = TimeStamp::Now() - started >= sliceTime;
        }
        if (done) {
          ++mFinalizeFunctionToRun;
        }
        if (timeout) {
          break;
        }
      } else {
        while (!function.run(UINT32_MAX, function.data));
        ++mFinalizeFunctionToRun;
      }
    } while (mFinalizeFunctionToRun < mDeferredFinalizeFunctions.Length());
  }

  if (mFinalizeFunctionToRun == mDeferredFinalizeFunctions.Length()) {
    MOZ_ASSERT(mRuntime->mFinalizeRunnable == this);
    mDeferredFinalizeFunctions.Clear();
    CycleCollectedJSRuntime* runtime = mRuntime;
    mRuntime = nullptr;
    // NB: This may delete this!
    runtime->mFinalizeRunnable = nullptr;
  }
}

NS_IMETHODIMP
IncrementalFinalizeRunnable::Run() {
  if (!mDeferredFinalizeFunctions.Length()) {
    /* These items were already processed synchronously in JSGC_END. */
    MOZ_ASSERT(!mRuntime);
    return NS_OK;
  }

  MOZ_ASSERT(mRuntime->mFinalizeRunnable == this);
  auto timerId = glean::cycle_collector::deferred_finalize_async.Start();
  ReleaseNow(true);

  if (mDeferredFinalizeFunctions.Length()) {
    nsresult rv = NS_DispatchToCurrentThread(this);
    if (NS_FAILED(rv)) {
      ReleaseNow(false);
    }
  } else {
    MOZ_ASSERT(!mRuntime);
  }

  glean::cycle_collector::deferred_finalize_async.StopAndAccumulate(
      std::move(timerId));

  return NS_OK;
}

void CycleCollectedJSRuntime::FinalizeDeferredThings(
    DeferredFinalizeType aType) {
  // If mFinalizeRunnable isn't null, we didn't finalize everything from the
  // previous GC.
  if (mFinalizeRunnable) {
    if (aType == FinalizeLater) {
      // We need to defer all finalization until we return to the event loop,
      // so leave things alone. Any new objects to be finalized from the current
      // GC will be handled by the existing mFinalizeRunnable.
      return;
    }
    MOZ_ASSERT(aType == FinalizeIncrementally || aType == FinalizeNow);
    // If we're finalizing incrementally, we don't want finalizers to build up,
    // so try to finish them off now.
    // If we're finalizing synchronously, also go ahead and clear them out,
    // so we make sure as much as possible is freed.
    mFinalizeRunnable->ReleaseNow(false);
    if (mFinalizeRunnable) {
      // If we re-entered ReleaseNow, we couldn't delete mFinalizeRunnable and
      // we need to just continue processing it.
      return;
    }
  }

  // If there's nothing to finalize, don't create a new runnable.
  if (mDeferredFinalizerTable.Count() == 0) {
    return;
  }

  mFinalizeRunnable =
      new IncrementalFinalizeRunnable(this, mDeferredFinalizerTable);

  // Everything should be gone now.
  MOZ_ASSERT(mDeferredFinalizerTable.Count() == 0);

  if (aType == FinalizeNow) {
    mFinalizeRunnable->ReleaseNow(false);
    MOZ_ASSERT(!mFinalizeRunnable);
  } else {
    MOZ_ASSERT(aType == FinalizeIncrementally || aType == FinalizeLater);
    NS_DispatchToCurrentThreadQueue(do_AddRef(mFinalizeRunnable), 2500,
                                    EventQueuePriority::Idle);
  }
}

const char* CycleCollectedJSRuntime::OOMStateToString(
    const OOMState aOomState) const {
  switch (aOomState) {
    case OOMState::OK:
      return "OK";
    case OOMState::Reporting:
      return "Reporting";
    case OOMState::Reported:
      return "Reported";
    case OOMState::Recovered:
      return "Recovered";
    default:
      MOZ_ASSERT_UNREACHABLE("OOMState holds an invalid value");
      return "Unknown";
  }
}

bool CycleCollectedJSRuntime::OOMReported() {
  return mOutOfMemoryState == OOMState::Reported;
}

void CycleCollectedJSRuntime::AnnotateAndSetOutOfMemory(OOMState* aStatePtr,
                                                        OOMState aNewState) {
  *aStatePtr = aNewState;
  CrashReporter::Annotation annotation =
      (aStatePtr == &mOutOfMemoryState)
          ? CrashReporter::Annotation::JSOutOfMemory
          : CrashReporter::Annotation::JSLargeAllocationFailure;

  CrashReporter::RecordAnnotationCString(annotation,
                                         OOMStateToString(aNewState));
}

void CycleCollectedJSRuntime::OnGC(JSContext* aContext, JSGCStatus aStatus,
                                   JS::GCReason aReason) {
  switch (aStatus) {
    case JSGC_BEGIN:
      MOZ_RELEASE_ASSERT(mTraceState.is<Nothing>());
      nsCycleCollector_prepareForGarbageCollection();
      PrepareWaitingZonesForGC();
      break;
    case JSGC_END: {
      MOZ_RELEASE_ASSERT(mTraceState.is<Nothing>());
      if (mOutOfMemoryState == OOMState::Reported) {
        AnnotateAndSetOutOfMemory(&mOutOfMemoryState, OOMState::Recovered);
      }
      if (mLargeAllocationFailureState == OOMState::Reported) {
        AnnotateAndSetOutOfMemory(&mLargeAllocationFailureState,
                                  OOMState::Recovered);
      }

      DeferredFinalizeType finalizeType;
      if (JS_IsExceptionPending(aContext)) {
        // There is a pending exception. The finalizers are not set up to run
        // in that state, so don't run the finalizer until we've returned to the
        // event loop.
        finalizeType = FinalizeLater;
      } else if (JS::InternalGCReason(aReason)) {
        if (aReason == JS::GCReason::DESTROY_RUNTIME) {
          // We're shutting down, so we need to destroy things immediately.
          finalizeType = FinalizeNow;
        } else {
          // We may be in the middle of running some code that the JIT has
          // assumed can't have certain kinds of side effects. Finalizers can do
          // all sorts of things, such as run JS, so we want to run them later,
          // after we've returned to the event loop.
          finalizeType = FinalizeLater;
        }
      } else if (JS::WasIncrementalGC(mJSRuntime)) {
        // The GC was incremental, so we probably care about pauses. Try to
        // break up finalization, but it is okay if we do some now.
        finalizeType = FinalizeIncrementally;
      } else {
        // If we're running a synchronous GC, we probably want to free things as
        // quickly as possible. This can happen during testing or if memory is
        // low.
        finalizeType = FinalizeNow;
      }
      FinalizeDeferredThings(finalizeType);

      break;
    }
    default:
      MOZ_CRASH();
  }

  CustomGCCallback(aStatus);
}

void CycleCollectedJSRuntime::OnOutOfMemory() {
  AnnotateAndSetOutOfMemory(&mOutOfMemoryState, OOMState::Reporting);
  CustomOutOfMemoryCallback();
  AnnotateAndSetOutOfMemory(&mOutOfMemoryState, OOMState::Reported);
}

void CycleCollectedJSRuntime::SetLargeAllocationFailure(OOMState aNewState) {
  AnnotateAndSetOutOfMemory(&mLargeAllocationFailureState, aNewState);
}

void CycleCollectedJSRuntime::PrepareWaitingZonesForGC() {
  JSContext* cx = CycleCollectedJSContext::Get()->Context();
  if (mZonesWaitingForGC.Count() == 0) {
    JS::PrepareForFullGC(cx);
  } else {
    for (const auto& key : mZonesWaitingForGC) {
      JS::PrepareZoneForGC(cx, key);
    }
    mZonesWaitingForGC.Clear();
  }
}

/* static */
void CycleCollectedJSRuntime::OnZoneDestroyed(JS::GCContext* aGcx,
                                              JS::Zone* aZone) {
  // Remove the zone from the set of zones waiting for GC, if present. This can
  // happen if a zone is added to the set during an incremental GC in which it
  // is later destroyed.
  CycleCollectedJSRuntime* runtime = Get();
  runtime->mZonesWaitingForGC.Remove(aZone);
}

void CycleCollectedJSRuntime::EnvironmentPreparer::invoke(
    JS::HandleObject global, js::ScriptEnvironmentPreparer::Closure& closure) {
  MOZ_ASSERT(JS_IsGlobalObject(global));
  nsIGlobalObject* nativeGlobal = xpc::NativeGlobal(global);

  // Not much we can do if we simply don't have a usable global here...
  NS_ENSURE_TRUE_VOID(nativeGlobal && nativeGlobal->HasJSGlobal());

  AutoEntryScript aes(nativeGlobal, "JS-engine-initiated execution");

  MOZ_ASSERT(!JS_IsExceptionPending(aes.cx()));

  DebugOnly<bool> ok = closure(aes.cx());

  MOZ_ASSERT_IF(ok, !JS_IsExceptionPending(aes.cx()));

  // The AutoEntryScript will check for JS_IsExceptionPending on the
  // JSContext and report it as needed as it comes off the stack.
}

/* static */
CycleCollectedJSRuntime* CycleCollectedJSRuntime::Get() {
  auto context = CycleCollectedJSContext::Get();
  if (context) {
    return context->Runtime();
  }
  return nullptr;
}

#ifdef MOZ_JS_DEV_ERROR_INTERCEPTOR

namespace js {
extern void DumpValue(const JS::Value& val);
}

void CycleCollectedJSRuntime::ErrorInterceptor::Shutdown(JSRuntime* rt) {
  JS_SetErrorInterceptorCallback(rt, nullptr);
  mThrownError.reset();
}

/* virtual */
void CycleCollectedJSRuntime::ErrorInterceptor::interceptError(
    JSContext* cx, JS::HandleValue exn) {
  if (mThrownError) {
    // We already have an error, we don't need anything more.
    return;
  }

  if (!nsContentUtils::ThreadsafeIsSystemCaller(cx)) {
    // We are only interested in chrome code.
    return;
  }

  const auto type = JS_GetErrorType(exn);
  if (!type) {
    // This is not one of the primitive error types.
    return;
  }

  switch (*type) {
    case JSExnType::JSEXN_REFERENCEERR:
    case JSExnType::JSEXN_SYNTAXERR:
      break;
    default:
      // Not one of the errors we are interested in.
      // Note that we are not interested in instances of `TypeError`
      // for the time being, as DOM (ab)uses this constructor to represent
      // all sorts of errors that are not even remotely related to type
      // errors (e.g. some network errors).
      // If we ever have a mechanism to differentiate between DOM-thrown
      // and SpiderMonkey-thrown instances of `TypeError`, we should
      // consider watching for `TypeError` here.
      return;
  }

  // Now copy the details of the exception locally.
  // While copying the details of an exception could be expensive, in most runs,
  // this will be done at most once during the execution of the process, so the
  // total cost should be reasonable.

  ErrorDetails details;
  details.mType = *type;
  // If `exn` isn't an exception object, `ExtractErrorValues` could end up
  // calling `toString()`, which could in turn end up throwing an error. While
  // this should work, we want to avoid that complex use case. Fortunately, we
  // have already checked above that `exn` is an exception object, so nothing
  // such should happen.
  nsContentUtils::ExtractErrorValues(cx, exn, details.mFilename, &details.mLine,
                                     &details.mColumn, details.mMessage);

  JS::UniqueChars buf =
      JS::FormatStackDump(cx, /* showArgs = */ false, /* showLocals = */ false,
                          /* showThisProps = */ false);
  CopyUTF8toUTF16(mozilla::MakeStringSpan(buf.get()), details.mStack);

  mThrownError.emplace(std::move(details));
}

void CycleCollectedJSRuntime::ClearRecentDevError() {
  mErrorInterceptor.mThrownError.reset();
}

bool CycleCollectedJSRuntime::GetRecentDevError(
    JSContext* cx, JS::MutableHandle<JS::Value> error) {
  if (!mErrorInterceptor.mThrownError) {
    return true;
  }

  // Create a copy of the exception.
  JS::RootedObject obj(cx, JS_NewPlainObject(cx));
  if (!obj) {
    return false;
  }

  JS::RootedValue message(cx);
  JS::RootedValue filename(cx);
  JS::RootedValue stack(cx);
  if (!ToJSValue(cx, mErrorInterceptor.mThrownError->mMessage, &message) ||
      !ToJSValue(cx, mErrorInterceptor.mThrownError->mFilename, &filename) ||
      !ToJSValue(cx, mErrorInterceptor.mThrownError->mStack, &stack)) {
    return false;
  }

  // Build the object.
  const auto FLAGS = JSPROP_READONLY | JSPROP_ENUMERATE | JSPROP_PERMANENT;
  if (!JS_DefineProperty(cx, obj, "message", message, FLAGS) ||
      !JS_DefineProperty(cx, obj, "fileName", filename, FLAGS) ||
      !JS_DefineProperty(cx, obj, "lineNumber",
                         mErrorInterceptor.mThrownError->mLine, FLAGS) ||
      !JS_DefineProperty(cx, obj, "stack", stack, FLAGS)) {
    return false;
  }

  // Pass the result.
  error.setObject(*obj);
  return true;
}
#endif  // MOZ_JS_DEV_ERROR_INTERCEPTOR

#undef MOZ_JS_DEV_ERROR_INTERCEPTOR
