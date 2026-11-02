/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * APIs for integration with the cycle collector.
 */

#ifndef js_friend_CycleCollector_h
#define js_friend_CycleCollector_h

#include "jstypes.h"

#include "js/HeapAPI.h"  // JS::GCCellPtr
#include "js/TraceKind.h"

namespace JS {
class AutoRequireNoGC;
class SliceBudget;
}  // namespace JS

/*
 * Trace hook used to trace gray roots incrementally.
 *
 * This should return whether tracing is finished. It will be called repeatedly
 * in subsequent GC slices until it returns true.
 *
 * While tracing this should check the budget and return false if it has been
 * exceeded. When passed an unlimited budget it should always return true.
 */
using JSGrayRootsTracer = bool (*)(JSTracer* trc, JS::SliceBudget& budget,
                                   void* data);

/*
 * Set a callback used to trace gray roots.
 *
 * The callback is called after the first slice of GC so the embedding must
 * implement appropriate barriers on its gray roots to ensure correctness.
 *
 * This callback may be called multiple times for different sets of zones. Use
 * JS::ZoneIsGrayMarking() to determine whether roots from a particular zone are
 * required.
 */
extern JS_PUBLIC_API void JS_SetGrayGCRootsTracer(JSContext* cx,
                                                  JSGrayRootsTracer traceOp,
                                                  void* data);

using JSObjectsTenuredCallback = void (*)(JS::GCContext* gcx, void* data);

extern JS_PUBLIC_API void JS_SetObjectsTenuredCallback(
    JSContext* cx, JSObjectsTenuredCallback cb, void* data);

/*
 * Used by the cycle collector to trace through a shape and all
 * cycle-participating data it reaches, using bounded stack space.
 */
extern JS_PUBLIC_API void JS_TraceShapeCycleCollectorChildren(JSTracer* trc,
                                                              js::Shape* shape);

namespace JS {

using DoCycleCollectionCallback = void (*)(JSContext* cx);

/**
 * The cycle collection callback is called after any COMPARTMENT_REVIVED GC in
 * which the majority of compartments have been marked gray.
 */
extern JS_PUBLIC_API DoCycleCollectionCallback
SetDoCycleCollectionCallback(JSContext* cx, DoCycleCollectionCallback callback);

inline JS_PUBLIC_API bool NeedGrayRootsForZone(Zone* zoneArg) {
  shadow::Zone* zone = shadow::Zone::from(zoneArg);
  return zone->isGCMarkingBlackAndGray() || zone->isGCCompacting();
}

using NoteWeakRefTargetCallback = void (*)(GCCellPtr ptr, void* data);

extern JS_PUBLIC_API void NoteWeakRefTargets(JSRuntime* runtime,
                                             NoteWeakRefTargetCallback callback,
                                             void* data);

using ShouldClearWeakRefTargetCallback = bool (*)(GCCellPtr ptr, void* data);

extern JS_PUBLIC_API void MaybeClearWeakRefTargets(
    JSRuntime* runtime, ShouldClearWeakRefTargetCallback callback, void* data);

}  // namespace JS

namespace js {

struct WeakMapTracer {
  JSRuntime* runtime;

  explicit WeakMapTracer(JSRuntime* rt) : runtime(rt) {}

  // Weak map tracer callback, called once for every binding of every
  // weak map that was live at the time of the last garbage collection.
  //
  // |m| will be nullptr if the weak map is not contained in a JS Object.
  // |value| will be nullptr if the binding's value is not a GC thing.
  //
  // The callback should not GC (and will assert in a debug build if it does
  // so.)
  virtual void trace(JSObject* m, JS::GCCellPtr key, JS::GCCellPtr value) = 0;
};

extern JS_PUBLIC_API void TraceWeakMaps(WeakMapTracer* trc);

extern JS_PUBLIC_API bool AreGCGrayBitsValid(JSRuntime* rt);

extern JS_PUBLIC_API bool ZoneGlobalsAreAllGray(JS::Zone* zone);

extern JS_PUBLIC_API void TraceGrayWrapperTargets(JSTracer* trc,
                                                  JS::Zone* zone);

using IterateGCThingCallback = void (*)(void*, JS::GCCellPtr,
                                        const JS::AutoRequireNoGC&);

/**
 * Invoke cellCallback on every gray JSObject in the given zone.
 */
extern JS_PUBLIC_API void IterateGrayObjects(
    JS::Zone* zone, IterateGCThingCallback cellCallback, void* data);

#if defined(JS_GC_ZEAL) || defined(DEBUG)
// Trace the heap and check there are no black to gray edges. These are
// not allowed since the cycle collector could throw away the gray thing and
// leave a dangling pointer.
//
// This doesn't trace weak maps as these are handled separately.
extern JS_PUBLIC_API bool CheckGrayMarkingState(JSRuntime* rt);
#endif

}  // namespace js

#endif  // js_friend_CycleCollector_h
