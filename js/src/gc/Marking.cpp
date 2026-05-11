/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/Marking-inl.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/PodOperations.h"
#include "mozilla/ScopeExit.h"

#include <algorithm>
#include <type_traits>

#include "debugger/Debugger.h"
#include "gc/BufferAllocator.h"
#include "gc/GCInternals.h"
#include "gc/ParallelMarking.h"
#include "gc/TraceKind.h"
#include "jit/JitCode.h"
#include "js/GCTypeMacros.h"  // JS_FOR_EACH_PUBLIC_{,TAGGED_}GC_POINTER_TYPE
#include "js/SliceBudget.h"
#include "util/Poison.h"
#include "util/RandomSeed.h"
#include "vm/GeneratorObject.h"

#include "gc/BufferAllocator-inl.h"
#include "gc/GC-inl.h"
#include "gc/PrivateIterators-inl.h"
#include "gc/TraceMethods-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/GeckoProfiler-inl.h"

using namespace js;
using namespace js::gc;

using JS::MapTypeToTraceKind;
using JS::SliceBudget;

using mozilla::DebugOnly;
using mozilla::IntegerRange;

// [SMDOC] GC Tracing
//
// Tracing Overview
// ================
//
// Tracing, in this context, refers to an abstract visitation of some or all of
// the GC-controlled heap. The effect of tracing an edge of the graph depends
// on the subclass of the JSTracer on whose behalf we are tracing.
//
// Marking
// -------
//
// The primary JSTracer is the GCMarker. The marking tracer causes the target
// of each traversed edge to be marked black and the target edge's children to
// be marked either gray (in the gc algorithm sense) or immediately black.
//
// Callback
// --------
//
// The secondary JSTracer is the CallbackTracer. This simply invokes a callback
// on each edge in a child.
//
// The following is a rough outline of the general struture of the tracing
// internals.
//
/* clang-format off */
//
//  +-------------------+                             ......................
//  |                   |                             :                    :
//  |                   v                             v                +---+---+
//  |   TraceRoot   TraceEdge   TraceRange        GCMarker::           |       |
//  |       |           |           |         processMarkStackTop      | Mark  |
//  |       +-----------------------+                 |                | Stack |
//  |                   |                             |                |       |
//  |                   v                             |                +---+---+
//  |           TraceEdgeInternal                     |                    ^
//  |                   |                             +<-------------+     :
//  |                   |                             |              |     :
//  |                   v                             v              |     :
//  |            CallbackTracer::             markAndTraverseEdge    |     :
//  |              onSomeEdge                         |              |     :
//  |                   |                             |              |     :
//  |                   |                             |              |     :
//  |                   +-------------+---------------+              |     :
//  |                                 |                              |     :
//  |                                 v                              |     :
//  |                          markAndTraverse                       |     :
//  |                                 |                              |     :
//  |                                 |                              |     :
//  |                              traverse                          |     :
//  |                                 |                              |     :
//  |             +--------------------------------------+           |     :
//  |             |                   |                  |           |     :
//  |             v                   v                  v           |     :
//  |    markAndTraceChildren    markAndPush    eagerlyMarkChildren  |     :
//  |             |                   :                  |           |     :
//  |             v                   :                  +-----------+     :
//  |      T::traceChildren           :                                    :
//  |             |                   :                                    :
//  +-------------+                   ......................................
//
//   Legend:
//     ------- Direct calls
//     ....... Data flow
//
/* clang-format on */

static const size_t ValueRangeWords =
    sizeof(MarkStack::SlotsOrElementsRange) / sizeof(uintptr_t);

/*** Tracing Invariants *****************************************************/

template <typename T>
static inline bool IsOwnedByOtherRuntime(JSRuntime* rt, T thing) {
  bool other = thing->runtimeFromAnyThread() != rt;
  MOZ_ASSERT_IF(other, thing->isPermanentAndMayBeShared());
  return other;
}

#ifdef DEBUG

static inline bool IsInFreeList(TenuredCell* cell) {
  Arena* arena = cell->arena();
  uintptr_t addr = reinterpret_cast<uintptr_t>(cell);
  MOZ_ASSERT(Arena::isAligned(addr, arena->getThingSize()));
  return arena->inFreeList(addr);
}

template <typename T>
void js::CheckTracedThing(JSTracer* trc, T* thing) {
  MOZ_ASSERT(trc);

  if (!thing) {
    return;
  }

  if (IsForwarded(thing)) {
    JS::TracerKind kind = trc->kind();
    MOZ_ASSERT(kind == JS::TracerKind::Tenuring ||
               kind == JS::TracerKind::MinorSweeping ||
               kind == JS::TracerKind::Moving ||
               kind == JS::TracerKind::HeapCheck);
    thing = Forwarded(thing);
  }

  /* This function uses data that's not available in the nursery. */
  if (IsInsideNursery(thing)) {
    return;
  }

  /*
   * Permanent shared things that are not associated with this runtime will be
   * ignored during marking.
   */
  Zone* zone = thing->zoneFromAnyThread();
  if (IsOwnedByOtherRuntime(trc->runtime(), thing)) {
    MOZ_ASSERT(!zone->wasGCStarted());
    MOZ_ASSERT(thing->isMarkedBlack());
    return;
  }

  JSRuntime* rt = trc->runtime();
  MOZ_ASSERT(zone->runtimeFromAnyThread() == rt);

  bool isGcMarkingTracer = trc->isMarkingTracer();
  bool isUnmarkGrayTracer = IsTracerKind(trc, JS::TracerKind::UnmarkGray);
  bool isClearEdgesTracer = IsTracerKind(trc, JS::TracerKind::ClearEdges);

  if (TlsContext.get()) {
    // If we're on the main thread we must have access to the runtime and zone.
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(rt));
    MOZ_ASSERT(CurrentThreadCanAccessZone(zone));
  } else {
    MOZ_ASSERT(isGcMarkingTracer || isUnmarkGrayTracer || isClearEdgesTracer ||
               IsTracerKind(trc, JS::TracerKind::Moving) ||
               IsTracerKind(trc, JS::TracerKind::Sweeping));
    MOZ_ASSERT_IF(!isClearEdgesTracer, CurrentThreadIsPerformingGC());
  }

  MOZ_ASSERT(thing->isAligned());
  MOZ_ASSERT(MapTypeToTraceKind<std::remove_pointer_t<T>>::kind ==
             thing->getTraceKind());

  /*
   * Check that we only mark allocated cells.
   *
   * This check is restricted to marking for two reasons: Firstly, if background
   * sweeping is running and concurrently modifying the free list then it is not
   * safe. Secondly, it was thought to be slow so this is a compromise so as to
   * not affect test times too much.
   */
  MOZ_ASSERT_IF(zone->isGCMarking(), !IsInFreeList(&thing->asTenured()));
}

template <typename T>
void js::CheckTracedThing(JSTracer* trc, const T& thing) {
  ApplyGCThingTyped(thing, [trc](auto t) { CheckTracedThing(trc, t); });
}

template <typename T>
static void CheckMarkedThing(GCMarker* gcMarker, T* thing) {
  Zone* zone = thing->zoneFromAnyThread();

  MOZ_ASSERT(zone->shouldMarkInZone(gcMarker->markColor()) ||
             zone->isAtomsZone());

  MOZ_ASSERT_IF(gcMarker->shouldCheckCompartments(),
                zone->isCollectingFromAnyThread() || zone->isAtomsZone());

  MOZ_ASSERT_IF(gcMarker->markColor() == MarkColor::Gray,
                !zone->isGCMarkingBlackOnly() || zone->isAtomsZone());

  MOZ_ASSERT(!(zone->isGCSweeping() || zone->isGCFinished() ||
               zone->isGCCompacting()));

  // Check that we don't stray from the current compartment and zone without
  // using TraceCrossCompartmentEdge.
  Compartment* comp = thing->maybeCompartment();
  MOZ_ASSERT_IF(gcMarker->tracingCompartment && comp,
                gcMarker->tracingCompartment == comp);
  MOZ_ASSERT_IF(gcMarker->tracingZone,
                gcMarker->tracingZone == zone || zone->isAtomsZone());
}

namespace js {

#  define IMPL_CHECK_TRACED_THING(_, type, _1, _2) \
    template void CheckTracedThing<type>(JSTracer*, type*);
JS_FOR_EACH_TRACEKIND(IMPL_CHECK_TRACED_THING);
#  undef IMPL_CHECK_TRACED_THING

template void CheckTracedThing<Value>(JSTracer*, const Value&);
template void CheckTracedThing<wasm::AnyRef>(JSTracer*, const wasm::AnyRef&);

}  // namespace js

#endif

static inline bool ShouldMarkCrossCompartment(GCMarker* marker, JSObject* src,
                                              Cell* dstCell, const char* name) {
#ifdef DEBUG
  if (src->isMarkedGray() && !dstCell->isTenured()) {
    // Bug 1743098: This shouldn't be possible but it does seem to happen. Log
    // some useful information in debug builds.
    SEprinter printer;
    printer.printf(
        "ShouldMarkCrossCompartment: cross compartment edge '%s' from gray "
        "object to nursery thing\n",
        name);
    printer.put("src: ");
    src->dump(printer);
    printer.put("dst: ");
    dstCell->dump(printer);
    MOZ_CRASH("Found cross compartment edge from gray object to nursery thing");
  }
#endif

  CellColor targetColor = AsCellColor(marker->markColor());
  CellColor currentColor = dstCell->color();
  if (currentColor >= targetColor) {
    // Cell is already sufficiently marked. Nothing to do.
    return false;
  }

  TenuredCell& dst = dstCell->asTenured();
  JS::Zone* dstZone = dst.zone();
  if (!src->zone()->isGCMarking() && !dstZone->isGCMarking()) {
    return false;
  }

  if (targetColor == CellColor::Black) {
    // Check our sweep groups are correct: we should never have to
    // mark something in a zone that we have started sweeping.
    MOZ_ASSERT(currentColor < CellColor::Black);
    MOZ_ASSERT(!dstZone->isGCSweeping());

    /*
     * Having black->gray edges violates our promise to the cycle collector so
     * we ensure that gray things we encounter when marking black end up getting
     * marked black.
     *
     * This can happen for two reasons:
     *
     * 1) If we're collecting a compartment and it has an edge to an uncollected
     * compartment it's possible that the source and destination of the
     * cross-compartment edge should be gray, but the source was marked black by
     * the write barrier.
     *
     * 2) If we yield during gray marking and the write barrier marks a gray
     * thing black.
     *
     * We handle the first case before returning whereas the second case happens
     * as part of normal marking.
     */
    if (currentColor == CellColor::Gray && !dstZone->isGCMarking()) {
      UnmarkGrayGCThingUnchecked(marker,
                                 JS::GCCellPtr(&dst, dst.getTraceKind()));
      return false;
    }

    return dstZone->isGCMarking();
  }

  // Check our sweep groups are correct as above.
  MOZ_ASSERT(currentColor == CellColor::White);
  MOZ_ASSERT(!dstZone->isGCSweeping());

  if (dstZone->isGCMarkingBlackOnly()) {
    /*
     * The destination compartment is being not being marked gray now,
     * but it will be later, so record the cell so it can be marked gray
     * at the appropriate time.
     */
    DelayCrossCompartmentGrayMarking(marker, src);
    return false;
  }

  return dstZone->isGCMarkingBlackAndGray();
}

static bool ShouldTraceCrossCompartment(JSTracer* trc, JSObject* src,
                                        Cell* dstCell, const char* name) {
  if (!trc->isMarkingTracer()) {
    return true;
  }

  return ShouldMarkCrossCompartment(GCMarker::fromTracer(trc), src, dstCell,
                                    name);
}

static bool ShouldTraceCrossCompartment(JSTracer* trc, JSObject* src,
                                        const Value& val, const char* name) {
  return val.isGCThing() &&
         ShouldTraceCrossCompartment(trc, src, val.toGCThing(), name);
}

template <typename T>
static inline bool ShouldMark(MarkColor color, T* thing) {
  // We may encounter nursery things during normal marking since we don't
  // collect the nursery at the start of every GC slice.
  if (!thing->isTenured()) {
    return false;
  }

  // Allow marking symbols even if we're not collecting the atoms zone. This is
  // necessary to unmark gray symbols during an incremental GC. Failing to do
  // this will break our promise to the cycle collector that there are no black
  // to gray edges.
  if (std::is_same_v<T, JS::Symbol> && color == MarkColor::Black) {
    return true;
  }

  // Otherwise don't mark things outside a collected zone if we are in a
  // per-zone GC. Don't mark permanent shared things owned by other runtimes (we
  // will never observe their zone being collected).
  Zone* zone = thing->asTenured().zoneFromAnyThread();
  return zone->shouldMarkInZone(color);
}

#ifdef DEBUG

template <typename T>
void js::gc::AssertShouldMarkInZone(GCMarker* marker, T* thing) {
  if (thing->isMarkedBlack()) {
    return;
  }

  Zone* zone = thing->zone();
  MOZ_ASSERT(zone->shouldMarkInZone(marker->markColor()) ||
             zone->isAtomsZone());
}

void js::gc::AssertRootMarkingPhase(JSTracer* trc) {
  MOZ_ASSERT_IF(trc->isMarkingTracer(),
                trc->runtime()->gc.state() == State::NotActive ||
                    trc->runtime()->gc.state() == State::MarkRoots);
}

#endif  // DEBUG

/*** Tracing Interface ******************************************************/

template <typename T>
static void TraceExternalEdgeHelper(JSTracer* trc, T* thingp,
                                    const char* name) {
  TraceEdgeInternal(trc, ConvertToBase(thingp), name);
}

JS_PUBLIC_API void js::UnsafeTraceManuallyBarrieredEdge(JSTracer* trc,
                                                        JSObject** thingp,
                                                        const char* name) {
  TraceEdgeInternal(trc, ConvertToBase(thingp), name);
}

template <typename T>
static void TraceRootHelper(JSTracer* trc, T* thingp, const char* name) {
  MOZ_ASSERT(thingp);
  js::TraceRoot(trc, thingp, name);
}

namespace js {
class AbstractGeneratorObject;
class SavedFrame;
}  // namespace js

#define DEFINE_TRACE_EXTERNAL_EDGE_FUNCTION(type)                           \
  JS_PUBLIC_API void js::gc::TraceExternalEdge(JSTracer* trc, type* thingp, \
                                               const char* name) {          \
    TraceExternalEdgeHelper(trc, thingp, name);                             \
  }

// Define TraceExternalEdge for each public GC pointer type.
JS_FOR_EACH_PUBLIC_GC_POINTER_TYPE(DEFINE_TRACE_EXTERNAL_EDGE_FUNCTION)
JS_FOR_EACH_PUBLIC_TAGGED_GC_POINTER_TYPE(DEFINE_TRACE_EXTERNAL_EDGE_FUNCTION)

#undef DEFINE_TRACE_EXTERNAL_EDGE_FUNCTION

#define DEFINE_UNSAFE_TRACE_ROOT_FUNCTION(type)                 \
  JS_PUBLIC_API void JS::TraceRoot(JSTracer* trc, type* thingp, \
                                   const char* name) {          \
    TraceRootHelper(trc, thingp, name);                         \
  }

// Define TraceRoot for each public GC pointer type.
JS_FOR_EACH_PUBLIC_GC_POINTER_TYPE(DEFINE_UNSAFE_TRACE_ROOT_FUNCTION)
JS_FOR_EACH_PUBLIC_TAGGED_GC_POINTER_TYPE(DEFINE_UNSAFE_TRACE_ROOT_FUNCTION)

// Also, for the moment, define TraceRoot for internal GC pointer types.
DEFINE_UNSAFE_TRACE_ROOT_FUNCTION(AbstractGeneratorObject*)
DEFINE_UNSAFE_TRACE_ROOT_FUNCTION(SavedFrame*)
DEFINE_UNSAFE_TRACE_ROOT_FUNCTION(wasm::AnyRef)

#undef DEFINE_UNSAFE_TRACE_ROOT_FUNCTION

namespace js::gc {

#define INSTANTIATE_INTERNAL_TRACE_FUNCTIONS(type)                     \
  template void TraceRangeInternal<type>(JSTracer*, size_t len, type*, \
                                         const char*);

#define INSTANTIATE_INTERNAL_TRACE_FUNCTIONS_FROM_TRACEKIND(_1, type, _2, _3) \
  INSTANTIATE_INTERNAL_TRACE_FUNCTIONS(type*)

JS_FOR_EACH_TRACEKIND(INSTANTIATE_INTERNAL_TRACE_FUNCTIONS_FROM_TRACEKIND)
JS_FOR_EACH_PUBLIC_TAGGED_GC_POINTER_TYPE(INSTANTIATE_INTERNAL_TRACE_FUNCTIONS)
INSTANTIATE_INTERNAL_TRACE_FUNCTIONS(TaggedProto)

#undef INSTANTIATE_INTERNAL_TRACE_FUNCTIONS_FROM_TRACEKIND
#undef INSTANTIATE_INTERNAL_TRACE_FUNCTIONS

}  // namespace js::gc

// Records the source zone (and, in debug builds, compartment) before calling
// a trace hook or traceChildren() method on a GC thing. The source zone is
// required in all builds so that MarkingTracerT::onEdge can keep the per-zone
// atom-marking bitmap in sync for Symbol edges traced via the generic tracer.
class MOZ_RAII AutoSetTracingSource {
  GCMarker* marker = nullptr;

 public:
  template <typename T>
  AutoSetTracingSource(JSTracer* trc, T* thing) {
    if (trc->isMarkingTracer() && thing) {
      marker = GCMarker::fromTracer(trc);
      MOZ_ASSERT(!marker->tracingZone);
      marker->tracingZone = thing->asTenured().zone();
#ifdef DEBUG
      MOZ_ASSERT(!marker->tracingCompartment);
      marker->tracingCompartment = thing->maybeCompartment();
#endif
    }
  }

  ~AutoSetTracingSource() {
    if (marker) {
      marker->tracingZone = nullptr;
#ifdef DEBUG
      marker->tracingCompartment = nullptr;
#endif
    }
  }
};

// Clear the tracing source. This happens after the trace hook has called back
// into one of our trace APIs and we've checked the traced thing, before any
// nested traversal that may itself use AutoSetTracingSource.
class MOZ_RAII AutoClearTracingSource {
  GCMarker* marker = nullptr;
  JS::Zone* prevZone = nullptr;
#ifdef DEBUG
  Compartment* prevCompartment = nullptr;
#endif

  void init(GCMarker* marker) {
    this->marker = marker;
    prevZone = marker->tracingZone;
    marker->tracingZone = nullptr;
#ifdef DEBUG
    prevCompartment = marker->tracingCompartment;
    marker->tracingCompartment = nullptr;
#endif
  }

 public:
  explicit AutoClearTracingSource(JSTracer* trc) {
    if (trc->isMarkingTracer()) {
      init(GCMarker::fromTracer(trc));
    }
  }
  explicit AutoClearTracingSource(GCMarker* marker) { init(marker); }
  ~AutoClearTracingSource() {
    if (marker) {
      marker->tracingZone = prevZone;
#ifdef DEBUG
      marker->tracingCompartment = prevCompartment;
#endif
    }
  }
};

template <typename T>
void js::TraceManuallyBarrieredCrossCompartmentEdge(JSTracer* trc,
                                                    JSObject* src, T* dst,
                                                    const char* name) {
  // Clear expected compartment for cross-compartment edge.
  AutoClearTracingSource acts(trc);

  if (ShouldTraceCrossCompartment(trc, src, *dst, name)) {
    TraceEdgeInternal(trc, dst, name);
  }
}
template void js::TraceManuallyBarrieredCrossCompartmentEdge<Value>(
    JSTracer*, JSObject*, Value*, const char*);
template void js::TraceManuallyBarrieredCrossCompartmentEdge<JSObject*>(
    JSTracer*, JSObject*, JSObject**, const char*);
template void js::TraceManuallyBarrieredCrossCompartmentEdge<BaseScript*>(
    JSTracer*, JSObject*, BaseScript**, const char*);

template <typename T>
void js::TraceSameZoneCrossCompartmentEdge(JSTracer* trc,
                                           const BarrieredBase<T>* dst,
                                           const char* name) {
#ifdef DEBUG
  if (trc->isMarkingTracer()) {
    T thing = *dst->unbarrieredAddress();
    MOZ_ASSERT(thing->maybeCompartment(),
               "Use TraceEdge for GC things without a compartment");

    GCMarker* gcMarker = GCMarker::fromTracer(trc);
    MOZ_ASSERT_IF(gcMarker->tracingZone,
                  thing->zone() == gcMarker->tracingZone);
  }

  // Skip compartment checks for this edge.
  if (trc->kind() == JS::TracerKind::CompartmentCheck) {
    return;
  }
#endif

  // Clear expected compartment for cross-compartment edge.
  AutoClearTracingSource acts(trc);
  TraceEdgeInternal(trc, ConvertToBase(dst->unbarrieredAddress()), name);
}
template void js::TraceSameZoneCrossCompartmentEdge(
    JSTracer*, const BarrieredBase<Shape*>*, const char*);

template <typename T>
void js::TraceWeakMapKeyEdgeInternal(JSTracer* trc, Zone* weakMapZone,
                                     T** thingp, const char* name) {
  // We'd like to assert that the the thing's zone is currently being marked but
  // that's not always true when tracing debugger weak maps which have keys in
  // other compartments.

  // Clear expected compartment for cross-compartment edge.
  AutoClearTracingSource acts(trc);

  TraceEdgeInternal(trc, thingp, name);
}

template <typename T>
void js::TraceWeakMapKeyEdgeInternal(JSTracer* trc, Zone* weakMapZone,
                                     T* thingp, const char* name) {
  // We can't use ShouldTraceCrossCompartment here because that assumes the
  // source of the edge is a CCW object which could be used to delay gray
  // marking. Instead, assert that the weak map zone is in the same marking
  // state as the target thing's zone and therefore we can go ahead and mark it.
#ifdef DEBUG
  if (trc->isMarkingTracer()) {
    MOZ_ASSERT(weakMapZone->isGCMarking());
    MOZ_ASSERT(weakMapZone->gcState() ==
               gc::ToMarkable(*thingp)->zone()->gcState());
  }
#endif

  // Clear expected compartment for cross-compartment edge.
  AutoClearTracingSource acts(trc);

  TraceEdgeInternal(trc, thingp, name);
}

template void js::TraceWeakMapKeyEdgeInternal<JSObject>(JSTracer*, Zone*,
                                                        JSObject**,
                                                        const char*);
template void js::TraceWeakMapKeyEdgeInternal<BaseScript>(JSTracer*, Zone*,
                                                          BaseScript**,
                                                          const char*);
template void js::TraceWeakMapKeyEdgeInternal<JS::Value>(JSTracer*, Zone*,
                                                         JS::Value*,
                                                         const char*);

static Cell* TraceGenericPointerRootAndType(JSTracer* trc, Cell* thing,
                                            JS::TraceKind kind,
                                            const char* name) {
  return MapGCThingTyped(thing, kind, [trc, name](auto t) -> Cell* {
    TraceRoot(trc, &t, name);
    return t;
  });
}

void js::TraceGenericPointerRoot(JSTracer* trc, Cell** thingp,
                                 const char* name) {
  MOZ_ASSERT(thingp);
  Cell* thing = *thingp;
  if (!thing) {
    return;
  }

  Cell* traced =
      TraceGenericPointerRootAndType(trc, thing, thing->getTraceKind(), name);
  if (traced != thing) {
    *thingp = traced;
  }
}

void js::TraceManuallyBarrieredGenericPointerEdge(JSTracer* trc, Cell** thingp,
                                                  const char* name) {
  MOZ_ASSERT(thingp);
  Cell* thing = *thingp;
  if (!*thingp) {
    return;
  }

  auto* traced = MapGCThingTyped(thing, thing->getTraceKind(),
                                 [trc, name](auto t) -> Cell* {
                                   TraceManuallyBarrieredEdge(trc, &t, name);
                                   return t;
                                 });
  if (traced != thing) {
    *thingp = traced;
  }
}

void js::TraceGCCellPtrRoot(JSTracer* trc, JS::GCCellPtr* thingp,
                            const char* name) {
  Cell* thing = thingp->asCell();
  if (!thing) {
    return;
  }

  Cell* traced =
      TraceGenericPointerRootAndType(trc, thing, thingp->kind(), name);

  if (!traced) {
    *thingp = JS::GCCellPtr();
  } else if (traced != thingp->asCell()) {
    *thingp = JS::GCCellPtr(traced, thingp->kind());
  }
}

void js::TraceManuallyBarrieredGCCellPtr(JSTracer* trc, JS::GCCellPtr* thingp,
                                         const char* name) {
  Cell* thing = thingp->asCell();
  if (!thing) {
    return;
  }

  Cell* traced = MapGCThingTyped(thing, thing->getTraceKind(),
                                 [trc, name](auto t) -> Cell* {
                                   TraceManuallyBarrieredEdge(trc, &t, name);
                                   return t;
                                 });

  if (!traced) {
    // If we are clearing edges, also erase the type. This happens when using
    // ClearEdgesTracer.
    *thingp = JS::GCCellPtr();
  } else if (traced != thingp->asCell()) {
    *thingp = JS::GCCellPtr(traced, thingp->kind());
  }
}

template <typename T>
inline bool TraceTaggedPtrEdge(JSTracer* trc, T* thingp, const char* name) {
  // Return true by default. For some types the lambda below won't be called.
  bool ret = true;
  auto thing = MapGCThingTyped(*thingp, [&](auto thing) {
    if (!TraceEdgeInternal(trc, &thing, name)) {
      ret = false;
      return TaggedPtr<T>::empty();
    }

    return TaggedPtr<T>::wrap(thing);
  });

  // Only update *thingp if the value changed, to avoid TSan false positives for
  // template objects when using DumpHeapTracer or UbiNode tracers while Ion
  // compiling off-thread.
  if (thing.isSome() && thing.value() != *thingp) {
    *thingp = thing.value();
  }

  return ret;
}

bool js::gc::TraceEdgeInternal(JSTracer* trc, Value* thingp, const char* name) {
  return TraceTaggedPtrEdge(trc, thingp, name);
}
bool js::gc::TraceEdgeInternal(JSTracer* trc, jsid* thingp, const char* name) {
  return TraceTaggedPtrEdge(trc, thingp, name);
}
bool js::gc::TraceEdgeInternal(JSTracer* trc, TaggedProto* thingp,
                               const char* name) {
  return TraceTaggedPtrEdge(trc, thingp, name);
}
bool js::gc::TraceEdgeInternal(JSTracer* trc, wasm::AnyRef* thingp,
                               const char* name) {
  return TraceTaggedPtrEdge(trc, thingp, name);
}

template <typename T>
void js::gc::TraceRangeInternal(JSTracer* trc, size_t len, T* vec,
                                const char* name) {
  JS::AutoTracingIndex index(trc);
  for (auto i : IntegerRange(len)) {
    if (InternalBarrierMethods<T>::isMarkable(vec[i])) {
      TraceEdgeInternal(trc, &vec[i], name);
    }
    ++index;
  }
}

/*** GC Marking Interface ***************************************************/

template <uint32_t opts>
void MarkingTracerT<opts>::markEphemeronEdges(EphemeronEdgeVector& edges,
                                              gc::MarkColor srcColor) {
  // This is only called as part of GC weak marking.
  static_assert(hasOption(MarkingOptions::MarkImplicitEdges));

  DebugOnly<size_t> initialLength = edges.length();

  for (auto& edge : edges) {
    MarkColor targetColor = std::min(srcColor, MarkColor(edge.color()));
    MOZ_ASSERT(markColor() >= targetColor);
    if (targetColor == markColor()) {
      ApplyGCThingTyped(edge.target(), edge.target()->getTraceKind(),
                        [this](auto t) { this->markAndTraverse(t); });
    }
  }

  // The above marking always goes through pushThing, which will not cause
  // 'edges' to be appended to while iterating.
  MOZ_ASSERT(edges.length() == initialLength);

  // This is not just an optimization. When nuking a CCW, we conservatively
  // mark through the related edges and then lose the CCW->target connection
  // that induces a sweep group edge. As a result, it is possible for the
  // delegate zone to get marked later, look up an edge in this table, and
  // then try to mark something in a Zone that is no longer marking.
  if (srcColor == MarkColor::Black && markColor() == MarkColor::Black) {
    edges.eraseIf([](auto& edge) { return edge.color() == MarkColor::Black; });
  }
}

template <typename T>
struct TypeCanHaveImplicitEdges : std::false_type {};
template <>
struct TypeCanHaveImplicitEdges<JSObject> : std::true_type {};
template <>
struct TypeCanHaveImplicitEdges<BaseScript> : std::true_type {};
template <>
struct TypeCanHaveImplicitEdges<JS::Symbol> : std::true_type {};

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::maybeMarkImplicitEdges(T* markedThing) {
  if constexpr (hasOption(MarkingOptions::MarkImplicitEdges) &&
                TypeCanHaveImplicitEdges<T>::value) {
    markImplicitEdges(markedThing);
  }
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::markImplicitEdges(T* markedThing) {
  static_assert(hasOption(MarkingOptions::MarkImplicitEdges));
  static_assert(TypeCanHaveImplicitEdges<T>::value);

  Zone* zone = markedThing->asTenured().zone();
  MOZ_ASSERT(zone->isGCMarking() || zone->isAtomsZone());
  MOZ_ASSERT(!zone->isGCSweeping());

  auto& ephemeronTable = zone->gcEphemeronEdges();
  auto p = ephemeronTable.lookup(&markedThing->asTenured());
  if (!p) {
    return;
  }

  EphemeronEdgeVector& edges = p->value();

  // markedThing might be a key in a debugger weakmap, which can end up
  // marking values that are in a different compartment.
  AutoClearTracingSource acts(this);

  MarkColor thingColor = markColor();
  MOZ_ASSERT(CellColor(thingColor) ==
             gc::detail::GetEffectiveColor(gcMarker(), markedThing));

  markEphemeronEdges(edges, thingColor);

  if (edges.empty()) {
    ephemeronTable.remove(p);
  }
}

template <uint32_t opts>
MarkingTracerT<opts>::MarkingTracerT(JSRuntime* runtime, GCMarker* marker)
    : GenericTracerImpl<MarkingTracerT<opts>>(
          runtime, JS::TracerKind::Marking,
          JS::TraceOptions(JS::WeakMapTraceAction::Expand,
                           JS::WeakEdgeTraceAction::Skip)) {
  // Marking tracers are owned by (and part of) a GCMarker.
  MOZ_ASSERT(this == marker->tracer());
  MOZ_ASSERT(gcMarker() == marker);
}

template <uint32_t opts>
MOZ_ALWAYS_INLINE GCMarker* MarkingTracerT<opts>::gcMarker() {
  return GCMarker::fromTracer(this);
}
template <uint32_t opts>
MOZ_ALWAYS_INLINE const GCMarker* MarkingTracerT<opts>::gcMarker() const {
  return GCMarker::fromTracer(const_cast<MarkingTracerT<opts>*>(this));
}

// Unmark gray symbols in incremental GC: gray unmarking doesn't proceed through
// zones which are currently being marked incrementally because the marking
// state isn't consistent, and we handle this later as part of marking.
static inline void MaybeUnmarkGraySymbol(JSRuntime* runtime,
                                         JS::Zone* sourceZone,
                                         JS::Symbol* target) {
  // Ignore edges from self-hosted JitCode that lives in the atoms zone.
  if (sourceZone->isAtomsZone()) {
    return;
  }

  AtomMarkingRuntime& atomMarking = runtime->gc.atomMarking;
  MOZ_ASSERT(atomMarking.atomIsMarked(sourceZone, target));
  atomMarking.maybeUnmarkGrayAtomically(sourceZone, target);
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::onEdge(T** thingp, const char* name) {
  T* thing = *thingp;
  if (!thing) {
    return;
  }

  // Do per-type marking precondition checks.
  if (!ShouldMark(markColor(), thing)) {
    MOZ_ASSERT(gc::detail::GetEffectiveColor(gcMarker(), thing) ==
               js::gc::CellColor::Black);
    return;
  }

  MOZ_ASSERT_IF(IsOwnedByOtherRuntime(this->runtime(), thing),
                thing->isMarkedBlack());

  if constexpr (std::is_same_v<T, JS::Symbol>) {
    Zone* zone = tracingZone();
    if (markColor() == MarkColor::Black && zone) {
      MaybeUnmarkGraySymbol(this->runtime(), zone, thing);
    }
  }

#ifdef DEBUG
  CheckMarkedThing(gcMarker(), thing);
#endif

  AutoClearTracingSource acts(this);
  this->markAndTraverse(thing);

  if constexpr (hasOption(MarkingOptions::MarkRootCompartments)) {
    // Mark the compartment as live.
    SetCompartmentHasMarkedCells(thing);
  }
}

#define INSTANTIATE_ONEDGE_METHOD(name, type, _1, _2)                 \
  template void MarkingTracerT<MarkingOptions::None>::onEdge<type>(   \
      type * *thingp, const char* name);                              \
  template void                                                       \
  MarkingTracerT<MarkingOptions::MarkImplicitEdges>::onEdge<type>(    \
      type * *thingp, const char* name);                              \
  template void                                                       \
  MarkingTracerT<MarkingOptions::MarkRootCompartments>::onEdge<type>( \
      type * *thingp, const char* name);
JS_FOR_EACH_TRACEKIND(INSTANTIATE_ONEDGE_METHOD)
#undef INSTANTIATE_ONEDGE_METHOD

static void TraceEdgeForBarrier(GCMarker* gcmarker, TenuredCell* thing,
                                JS::TraceKind kind) {
  // Dispatch to markAndTraverse without checking ShouldMark.

#ifdef DEBUG
  MOZ_ASSERT(gcmarker->markColor() == MarkColor::Black);
  AutoSetThreadIsMarking threadIsMarking;
#endif  // DEBUG

  AutoClearTracingSource acts(gcmarker);

  ApplyGCThingTyped(thing, kind, [gcmarker](auto thing) {
    MOZ_ASSERT(ShouldMark(MarkColor::Black, thing));
    gcmarker->matchRegularOrParallelTracer([thing](auto& trc) {
      CheckTracedThing(&trc, thing);
      trc.markAndTraverse(thing);
    });
  });
}

JS_PUBLIC_API void js::gc::PerformIncrementalReadBarrier(JS::GCCellPtr thing) {
  // Optimized marking for read barriers. This is called from
  // ExposeGCThingToActiveJS which has already checked the prerequisites for
  // performing a read barrier. This means we can skip a bunch of checks and
  // call into the tracer directly.

  MOZ_ASSERT(thing);
  MOZ_ASSERT(!JS::RuntimeHeapIsCollecting());

  TenuredCell* cell = &thing.asCell()->asTenured();

#ifndef JS_GC_CONCURRENT_MARKING
  MOZ_ASSERT(!cell->isMarkedBlack());
#endif

  Zone* zone = cell->zone();
  MOZ_ASSERT(zone->needsMarkingBarrier());

  // Skip dispatching on known tracer type.
  GCMarker* gcmarker = GCMarker::fromTracer(zone->barrierTracer());
  TraceEdgeForBarrier(gcmarker, cell, thing.kind());
}

void js::gc::PerformIncrementalReadBarrier(TenuredCell* cell) {
  // Internal version of previous function.

  MOZ_ASSERT(cell);
  MOZ_ASSERT(!JS::RuntimeHeapIsCollecting());

  if (cell->isMarkedBlack()) {
    return;
  }

  Zone* zone = cell->zone();
  MOZ_ASSERT(zone->needsMarkingBarrier());

  // Skip dispatching on known tracer type.
  GCMarker* gcmarker = GCMarker::fromTracer(zone->barrierTracer());
  TraceEdgeForBarrier(gcmarker, cell, cell->getTraceKind());
}

void js::gc::PerformIncrementalPreWriteBarrier(TenuredCell* cell) {
  // The same as PerformIncrementalReadBarrier except for an extra check on the
  // runtime for cells in atoms zone.

  MOZ_ASSERT(cell);
  if (cell->isMarkedBlack()) {
    return;
  }

  // Barriers can be triggered off the main thread by background finalization of
  // HeapPtrs to the atoms zone. We don't want to trigger the barrier in this
  // case.
  Zone* zone = cell->zoneFromAnyThread();
  bool checkThread = zone->isAtomsZone();
  JSRuntime* runtime = cell->runtimeFromAnyThread();
  if (checkThread && !CurrentThreadCanAccessRuntime(runtime)) {
    MOZ_ASSERT(CurrentThreadIsGCFinalizing());
    return;
  }

  MOZ_ASSERT(zone->needsMarkingBarrier());
  MOZ_ASSERT(CurrentThreadIsMainThread());
  MOZ_ASSERT(!JS::RuntimeHeapIsMajorCollecting());

  // Skip dispatching on known tracer type.
  GCMarker* gcmarker = GCMarker::fromTracer(zone->barrierTracer());
  TraceEdgeForBarrier(gcmarker, cell, cell->getTraceKind());
}

#ifdef ENABLE_WASM_JSPI
void js::gc::PerformIncrementalPreWriteBarrierAllChildren(JSObject* cell) {
  if (!cell) {
    return;
  }

  // If the object is already marked black, its children may already be in the
  // GC's marking work queue. However, with incremental and concurrent marking,
  // objects can be marked black before their trace hooks have run. So we
  // conservatively mark it even if it's black.
  Zone* zone = cell->zoneFromAnyThread();
  MOZ_ASSERT(!zone->isAtomsZone());
  MOZ_ASSERT(zone->needsMarkingBarrier());
  MOZ_ASSERT(CurrentThreadIsMainThread());
  MOZ_ASSERT(!JS::RuntimeHeapIsMajorCollecting());

  // Skip dispatching on known tracer type.
  GCMarker* gcmarker = GCMarker::fromTracer(zone->barrierTracer());

  MOZ_ASSERT(ShouldMark(gcmarker->markColor(), cell));
  CheckTracedThing(gcmarker->tracer(), cell);
  AutoClearTracingSource acts(gcmarker->tracer());
#  ifdef DEBUG
  AutoSetThreadIsMarking threadIsMarking;
#  endif  // DEBUG
  cell->traceChildren(zone->barrierTracer());
}
#endif  // ENABLE_WASM_JSPI

void js::gc::PerformIncrementalBarrierDuringFlattening(JSString* str) {
  TenuredCell* cell = &str->asTenured();

  // Skip eager marking of ropes during flattening. Their children will also be
  // barriered by flattening process so we don't need to traverse them.
  if (str->isRope()) {
    cell->markBlack();
    return;
  }

  PerformIncrementalPreWriteBarrier(cell);
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::markAndTraverse(T* thing) {
  if (!mark(thing)) {
    return;
  }

  // We only mark permanent things during initialization.
  MOZ_ASSERT_IF(thing->isPermanentAndMayBeShared(),
                !this->runtime()->permanentAtomsPopulated());

  MemoryAcquireFence<opts>(this->runtime());

  traverse(thing);
}

// The |traverse| method overloads select the traversal strategy for each kind.
//
// There are three possible strategies:
//
// 1. traceChildren
//
//    The simplest traversal calls out to the fully generic traceChildren
//    function to visit the child edges. In the absence of other traversal
//    mechanisms, this function will rapidly grow the stack past its bounds and
//    crash the process. Thus, this generic tracing should only be used in cases
//    where subsequent tracing will not recurse.
//
// 2. scanChildren
//
//    Strings, Shapes, and Scopes are extremely common, but have simple patterns
//    of recursion. We traverse trees of these edges immediately, with
//    aggressive, manual inlining, implemented by eagerlyTraceChildren.
//
// 3. pushThing
//
//    Objects are extremely common and can contain arbitrarily nested graphs, so
//    are not trivially inlined. In this case we use the mark stack to control
//    recursion. JitCode shares none of these properties, but is included for
//    historical reasons. JSScript normally cannot recurse, but may be used as a
//    weakmap key and thereby recurse into weakmapped values.

template <uint32_t opts>
void MarkingTracerT<opts>::traverse(GetterSetter* thing) {
  traceChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(JS::Symbol* thing) {
  if constexpr (hasOption(MarkingOptions::MarkImplicitEdges)) {
    pushThing(thing);
    return;
  }
  traceChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(JS::BigInt* thing) {
  traceChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(RegExpShared* thing) {
  traceChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(JSString* thing) {
  scanChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(Shape* thing) {
  scanChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(BaseShape* thing) {
  scanChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(PropMap* thing) {
  scanChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(js::Scope* thing) {
  scanChildren(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(JSObject* thing) {
  pushThing(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(jit::JitCode* thing) {
  pushThing(thing);
}
template <uint32_t opts>
void MarkingTracerT<opts>::traverse(BaseScript* thing) {
  pushThing(thing);
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::traceChildren(T* thing) {
  MOZ_ASSERT(!thing->isPermanentAndMayBeShared());
  MOZ_ASSERT(thing->isMarkedAny());
  AutoSetTracingSource asts(this, thing);
  thing->traceChildren(this);
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::scanChildren(T* thing) {
  MOZ_ASSERT(!thing->isPermanentAndMayBeShared());
  MOZ_ASSERT(thing->isMarkedAny());
  eagerlyMarkChildren(thing);
}

template <uint32_t opts>
template <typename T>
void MarkingTracerT<opts>::pushThing(T* thing) {
  MOZ_ASSERT(!thing->isPermanentAndMayBeShared());
  MOZ_ASSERT(thing->isMarkedAny());
  gcMarker()->pushTaggedPtr(thing);
}

template void MarkingTracerT<MarkingOptions::None>::markAndTraverse(
    JSObject* thing);
template void MarkingTracerT<
    MarkingOptions::MarkImplicitEdges>::markAndTraverse(JSObject* thing);
template void MarkingTracerT<
    MarkingOptions::MarkRootCompartments>::markAndTraverse(JSObject* thing);

#ifdef DEBUG
void GCMarker::setCheckAtomMarking(bool check) {
  MOZ_ASSERT(check != checkAtomMarking);
  checkAtomMarking = check;
}
#endif

template <typename S, typename T>
inline void GCMarker::checkTraversedEdge(S source, T* target) {
#ifdef DEBUG
  // Atoms and Symbols do not have or mark their internal pointers,
  // respectively.
  MOZ_ASSERT(!source->isPermanentAndMayBeShared());

  // Shared things are already black so we will not mark them.
  if (target->isPermanentAndMayBeShared()) {
    Zone* zone = target->zoneFromAnyThread();
    MOZ_ASSERT(!zone->wasGCStarted());
    MOZ_ASSERT(!zone->needsMarkingBarrier());
    MOZ_ASSERT(target->isMarkedBlack());
    MOZ_ASSERT(!target->maybeCompartment());
    return;
  }

  Zone* sourceZone = source->zone();
  Zone* targetZone = target->zone();

  // Atoms and Symbols do not have access to a compartment pointer, or we'd need
  // to adjust the subsequent check to catch that case.
  MOZ_ASSERT_IF(targetZone->isAtomsZone(), !target->maybeCompartment());

  // The Zones must match, unless the target is an atom.
  MOZ_ASSERT(targetZone == sourceZone || targetZone->isAtomsZone());

  // If we are marking an atom, that atom must be marked in the source zone's
  // atom bitmap.
  if (checkAtomMarking && !sourceZone->isAtomsZone() &&
      targetZone->isAtomsZone()) {
    GCRuntime* gc = &target->runtimeFromAnyThread()->gc;
    TenuredCell* atom = &target->asTenured();
    MOZ_ASSERT(gc->atomMarking.getAtomMarkColor(sourceZone, atom) >=
               AsCellColor(markColor()));
  }

  // If we have access to a compartment pointer for both things, they must
  // match.
  MOZ_ASSERT_IF(source->maybeCompartment() && target->maybeCompartment(),
                source->maybeCompartment() == target->maybeCompartment());
#endif
}

template <uint32_t opts>
template <typename S, typename T>
void MarkingTracerT<opts>::markAndTraverseEdge(S* source, T* target) {
  if constexpr (std::is_same_v<T, JS::Symbol>) {
    if (markColor() == MarkColor::Black) {
      MaybeUnmarkGraySymbol(this->runtime(), source->zone(), target);
    }
  }

  gcMarker()->checkTraversedEdge(source, target);
  markAndTraverse(target);
}

template <uint32_t opts>
template <typename S, typename T>
void MarkingTracerT<opts>::markAndTraverseEdge(S* source, const T& target) {
  ApplyGCThingTyped(
      target, [this, source](auto t) { this->markAndTraverseEdge(source, t); });
}

template <uint32_t opts>
MOZ_NEVER_INLINE bool MarkingTracerT<opts>::markAndTraversePrivateGCThing(
    JSObject* source, Cell* target) {
  JS::TraceKind kind = target->getTraceKind();
  ApplyGCThingTyped(target, kind, [this, source](auto t) {
    this->markAndTraverseEdge(source, t);
  });

  GCMarker* marker = gcMarker();
  // Ensure stack headroom in case we pushed.
  if (MOZ_UNLIKELY(!marker->stack.ensureSpace(ValueRangeWords))) {
    marker->delayMarkingChildrenOnOOM(source);
    return false;
  }

  return true;
}

template <uint32_t opts>
bool MarkingTracerT<opts>::markAndTraverseSymbol(JSObject* source,
                                                 JS::Symbol* target) {
  this->markAndTraverseEdge(source, target);

  GCMarker* marker = gcMarker();
  // Ensure stack headroom in case we pushed.
  if (MOZ_UNLIKELY(!marker->stack.ensureSpace(ValueRangeWords))) {
    marker->delayMarkingChildrenOnOOM(source);
    return false;
  }

  return true;
}

template <uint32_t opts>
template <typename T>
bool MarkingTracerT<opts>::mark(T* thing) {
  if (!thing->isTenured()) {
    return false;
  }

  if constexpr (std::is_same_v<T, JS::Symbol>) {
    // Don't mark symbols owned by other runtimes. Mark symbols black in
    // uncollected zones for gray unmarking, but don't mark symbols gray in
    // uncollected zones.
    if (IsOwnedByOtherRuntime(this->runtime(), thing) ||
        (markColor() == MarkColor::Gray &&
         !thing->zone()->isGCMarkingOrVerifyingPreBarriers())) {
      return false;
    }
  }

  AssertShouldMarkInZone(gcMarker(), thing);

  MarkColor color =
      TraceKindCanBeGray<T>::value ? markColor() : MarkColor::Black;

#ifdef JS_GC_CONCURRENT_MARKING
  // TODO: We don't need thread safe marking if concurrent marking is
  // disabled. We do need this for barrier tracing on the main thread during
  // concurrent marking however.
  return thing->asTenured().markIfUnmarkedThreadSafe(color);
#else
  if constexpr (hasOption(MarkingOptions::AtomicMarking)) {
    return thing->asTenured().markIfUnmarkedThreadSafe(color);
  }

  return thing->asTenured().markIfUnmarked(color);
#endif
}

/*** Mark-stack Marking *****************************************************/

static gcstats::PhaseKind GrayMarkingPhaseForCurrentPhase(
    const gcstats::Statistics& stats) {
  using namespace gcstats;

  MOZ_ASSERT(CurrentThreadIsMainThread());

  switch (stats.currentPhaseKind()) {
    case PhaseKind::MARK:
      return PhaseKind::MARK_GRAY;
    case PhaseKind::MARK_WEAK:
      return PhaseKind::MARK_GRAY_WEAK;
    default:
      MOZ_CRASH("Unexpected current phase");
  }
}

/* static */
void GCMarker::moveAllWork(GCMarker* dst, GCMarker* src) {
  MOZ_ASSERT(dst->markColor() == src->markColor());
  MarkStack::moveAllWork(dst->stack, src->stack);
  MarkStack::moveAllWork(dst->otherStack, src->otherStack);
}

/* static */
size_t GCMarker::moveSomeWork(GCMarker* dst, GCMarker* src,
                              bool allowDistribute) {
  MOZ_ASSERT(dst->markColor() == src->markColor());
  MOZ_ASSERT(dst->stack.isEmpty());
  MOZ_ASSERT(src->canDonateWork());

  return MarkStack::moveSomeWork(src, dst->stack, src->stack, allowDistribute);
}

bool GCMarker::initStack() {
  MOZ_ASSERT(!isActive());
  MOZ_ASSERT(markColor_ == gc::MarkColor::Black);
  return stack.init();
}

void GCMarker::resetStackCapacity() {
  MOZ_ASSERT(!isActive());
  MOZ_ASSERT(markColor_ == gc::MarkColor::Black);
  (void)stack.resetStackCapacity();
}

void GCMarker::freeStack() {
  MOZ_ASSERT(!isActive());
  MOZ_ASSERT(markColor_ == gc::MarkColor::Black);
  stack.clearAndFreeStack();
}

bool GCMarker::markUntilBudgetExhausted(SliceBudget& budget,
                                        ShouldReportMarkTime reportTime) {
  MOZ_ASSERT(isRegularMarking() || isWeakMarking() || isConcurrentMarking());

#ifdef DEBUG
  MOZ_ASSERT(!strictCompartmentChecking);
  strictCompartmentChecking = true;
  auto acc = mozilla::MakeScopeExit([&] { strictCompartmentChecking = false; });
#endif

  if (budget.isOverBudget()) {
    return false;
  }

  return matchTracer(
      [&](auto& trc) { return trc.doMarking(budget, reportTime); });
}

template <uint32_t opts>
bool MarkingTracerT<opts>::doMarking(SliceBudget& budget,
                                     ShouldReportMarkTime reportTime) {
  GCMarker* marker = gcMarker();
  GCRuntime& gc = this->runtime()->gc;

  // This method leaves the mark color as it found it.

  if (marker->hasBlackEntries() && !markOneColor<MarkColor::Black>(budget)) {
    return false;
  }

  if (marker->hasGrayEntries()) {
    mozilla::Maybe<gcstats::AutoPhase> ap;
    if (reportTime) {
      auto& stats = this->runtime()->gc.stats();
      ap.emplace(stats, GrayMarkingPhaseForCurrentPhase(stats));
    }

    if (!markOneColor<MarkColor::Gray>(budget)) {
      return false;
    }
  }

  // Mark children of things that caused too deep recursion during the above
  // tracing. All normal marking happens before any delayed marking.
  if (marker == &gc.marker() && gc.hasDelayedMarking()) {
    gc.markAllDelayedChildren(reportTime);
    MOZ_ASSERT(!gc.hasDelayedMarking());
  }

  MOZ_ASSERT(marker->isMarkStackEmpty());

  return true;
}

template <uint32_t opts>
template <MarkColor color>
bool MarkingTracerT<opts>::markOneColor(SliceBudget& budget) {
  GCMarker* marker = gcMarker();
  AutoSetMarkColor setColor(*marker, color);
  AutoUpdateMarkStackRanges updateRanges(*marker);
  return markCurrentColor(budget);
}

template <uint32_t opts>
bool MarkingTracerT<opts>::markCurrentColor(SliceBudget& budget) {
  GCMarker* marker = gcMarker();
  while (processMarkStackTop(budget)) {
    if (marker->stack.isEmpty()) {
      return true;
    }
  }

  return false;
}

bool GCMarker::markCurrentColorInParallel(ParallelMarkTask* task,
                                          SliceBudget& budget) {
  MOZ_ASSERT(isParallelMarking());
  MOZ_ASSERT(stack.elementsRangesAreValid);

  ParallelMarkTask::AtomicCount& waitingTaskCount = task->waitingTaskCountRef();

  auto* trc = &tracer_.as<ParallelMarkingTracer>();
  while (trc->processMarkStackTop(budget)) {
    if (stack.isEmpty()) {
      return true;
    }

    // TODO: It might be better to only check this occasionally, possibly
    // combined with the slice budget check. Experiments with giving this its
    // own counter resulted in worse performance.
    if (waitingTaskCount && shouldDonateWork()) {
      task->donateWork();
    }
  }

  return false;
}

#ifdef DEBUG
bool GCMarker::markOneObjectForTest(JSObject* obj) {
  MOZ_ASSERT(obj->zone()->isGCMarking());
  MOZ_ASSERT(!obj->isMarked(markColor()));

  return matchTracer([this, obj](auto& trc) {
    size_t oldPosition = stack.position();
    trc.markAndTraverse(obj);
    if (stack.position() == oldPosition) {
      return false;
    }

    AutoUpdateMarkStackRanges updateRanges(*this);
    SliceBudget unlimited = SliceBudget::unlimited();
    trc.processMarkStackTop(unlimited);
    return true;
  });
}
#endif

#ifdef JS_GC_CONCURRENT_MARKING

// The maximum number of entries in a main thread buffer before we stop
// concurrent marking and interrupt the main thread to do this work.
static constexpr size_t MainThreadBufferThreshold = 16384;

inline bool GCMarker::addToMainThreadBuffer(JSObject* object,
                                            SliceBudget& budget) {
  auto& buffer = markColor() == MarkColor::Black ? blackMainThreadBuffer_.ref()
                                                 : grayMainThreadBuffer_.ref();
  if (!buffer.append(object)) {
    return false;
  }

  if (MOZ_UNLIKELY(buffer.length() == MainThreadBufferThreshold)) {
    // Ensure |budget.isOverBudget()| will return true if the buffer is full.
    budget.setInterrupted();
    budget.forceCheck();
  }

  return true;
}

bool GCMarker::processMainThreadBuffers(SliceBudget& budget) {
  // We can be on the main thread or on a helper thread during sweeping here.
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime()) ||
             JS::RuntimeHeapIsMajorCollecting());

  MOZ_ASSERT(markColor() == MarkColor::Black);
  if (!processMainThreadBuffer(blackMainThreadBuffer_.ref(), budget)) {
    return false;
  }

  if (!grayMainThreadBuffer_.ref().empty()) {
    // Allow pushing gray marking even if there is still black marking
    // work. This reduces the amount of handshaking between the main thread and
    // the marking thread.
    AutoSetMarkColor autoSetGray(*this, MarkColor::Gray,
                                 AllowGrayMarkingBeforeEndOfBlackMarking::Yes);
    if (!processMainThreadBuffer(grayMainThreadBuffer_.ref(), budget)) {
      return false;
    }
  }

  MOZ_ASSERT(mainThreadBuffersAreEmpty());

  return true;
}

bool GCMarker::processMainThreadBuffer(MainThreadBuffer& buffer,
                                       SliceBudget& budget) {
  while (!buffer.empty()) {
    JSObject* obj = buffer.popCopy();

    MOZ_ASSERT(obj->isMarkedAtLeast(markColor()));
    if (markColor() == MarkColor::Gray && obj->isMarkedBlack()) {
      // We subsequently marked this black so we can skip marking it gray.
      continue;
    }

    const JSClass* clasp = obj->getClass();
    MOZ_ASSERT(clasp->hasTrace());
    AutoSetTracingSource asts(tracer(), obj);
    clasp->doTrace(tracer(), obj);

    budget.step();
    if (budget.isOverBudget()) {
      return false;
    }
  }

  return true;
}

#endif  // JS_GC_CONCURRENT_MARKING

static inline void CheckForCompartmentMismatch(JSObject* obj, JSObject* obj2) {
#ifdef DEBUG
  if (MOZ_UNLIKELY(obj->compartment() != obj2->compartment())) {
    fprintf(
        stderr,
        "Compartment mismatch in pointer from %s object slot to %s object\n",
        obj->getClass()->name, obj2->getClass()->name);
    MOZ_CRASH("Compartment mismatch");
  }
#endif
}

static inline size_t NumUsedFixedSlots(NativeObject* obj) {
  // Concurrent marking: this can happen concurrently with a shape change by the
  // mutator. This is safe because 1) the total number of fixed slots cannot
  // change and 2) if the slot span changes new/deleted slots still get marked
  // because of the snapshot at the beginning invariant. We do need to ensure we
  // only read object fields once though.
  Shape* shape = obj->shape();
  ObjectSlots* slotsHeader = obj->getSlotsHeader();
  return std::min(NumNativeObjectFixedSlots(shape),
                  NativeObjectSlotSpan(shape, slotsHeader));
}

#ifndef JS_GC_CONCURRENT_MARKING
static inline size_t NumUsedDynamicSlots(NativeObject* obj) {
  size_t nfixed = obj->numFixedSlots();
  size_t nslots = obj->slotSpan();
  if (nslots < nfixed) {
    return 0;
  }

  return nslots - nfixed;
}
#endif

void GCMarker::updateRangesAtStartOfSlice() {
  MOZ_ASSERT(!stack.elementsRangesAreValid);

  for (MarkStackIter iter(stack); !iter.done(); iter.next()) {
    if (iter.isSlotsOrElementsRange()) {
      MarkStack::SlotsOrElementsRange range = iter.slotsOrElementsRange();
      JSObject* obj = range.ptr().asRangeObject();
      MOZ_ASSERT(obj->is<NativeObject>());
      if (range.kind() == SlotsOrElementsKind::Elements) {
        NativeObject* nobj = &obj->as<NativeObject>();
        size_t index = range.start();
        size_t numShifted = nobj->getElementsHeader()->numShiftedElements();
        index -= std::min(numShifted, index);
        range.setStart(index);
        iter.setSlotsOrElementsRange(range);
      }
    }
  }

#ifdef DEBUG
  stack.elementsRangesAreValid = true;
#endif
}

void GCMarker::updateRangesAtEndOfSlice() {
  MOZ_ASSERT(stack.elementsRangesAreValid);

  for (MarkStackIter iter(stack); !iter.done(); iter.next()) {
    if (iter.isSlotsOrElementsRange()) {
      MarkStack::SlotsOrElementsRange range = iter.slotsOrElementsRange();
      if (range.kind() == SlotsOrElementsKind::Elements) {
        NativeObject* obj = &range.ptr().asRangeObject()->as<NativeObject>();
        size_t numShifted = obj->getElementsHeader()->numShiftedElements();
        range.setStart(range.start() + numShifted);
        iter.setSlotsOrElementsRange(range);
      }
    }
  }

#ifdef DEBUG
  stack.elementsRangesAreValid = false;
#endif
}

template <uint32_t opts>
inline bool MarkingTracerT<opts>::processMarkStackTop(SliceBudget& budget) {
  /*
   * This function uses explicit goto and scans objects directly. This allows us
   * to eliminate tail recursion and significantly improve the marking
   * performance, see bug 641025.
   *
   * Note that the mutator can change the size and layout of objects between
   * marking slices, so we must check slots and element ranges read from the
   * stack.
   */

  GCMarker* marker = gcMarker();
  MarkStack& stack = marker->stack;

  MOZ_ASSERT(!stack.isEmpty());
  MOZ_ASSERT(stack.elementsRangesAreValid);
  MOZ_ASSERT_IF(markColor() == MarkColor::Gray, !marker->hasBlackEntries());

  JSObject* obj;             // The object being scanned.
  SlotsOrElementsKind kind;  // The kind of slot range being scanned, if any.
  HeapSlot* base;            // Slot range base pointer.
  size_t index;              // Index of the next slot to mark.
  size_t end;                // End of slot range to mark.

  if (stack.peekTag() == MarkStack::SlotsOrElementsRangeTag) {
    auto range = stack.popSlotsOrElementsRange();
    obj = range.ptr().asRangeObject();
    NativeObject* nobj = &obj->as<NativeObject>();
    kind = range.kind();
    index = range.start();

    switch (kind) {
      case SlotsOrElementsKind::FixedSlots: {
        base = nobj->fixedSlots();
        end = NumUsedFixedSlots(nobj);
        break;
      }

      case SlotsOrElementsKind::DynamicSlots: {
        base = nobj->slots_;
#ifdef JS_GC_CONCURRENT_MARKING
        // TODO: Investigate whether we can safely restrict this to the number
        // of used slots.
        end = ObjectSlots::fromSlots(base)->capacity();
#else
        end = NumUsedDynamicSlots(nobj);
#endif
        break;
      }

      case SlotsOrElementsKind::Elements: {
        base = nobj->getDenseElements();
        end = nobj->getDenseInitializedLength();
        break;
      }

      case SlotsOrElementsKind::Unused: {
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unused SlotsOrElementsKind");
      }
    }

    goto scan_value_range;
  }

  budget.step();
  if (budget.isOverBudget()) {
    return false;
  }

  {
    MarkStack::TaggedPtr ptr = stack.popPtr();
    switch (ptr.tag()) {
      case MarkStack::ObjectTag: {
        obj = ptr.as<JSObject>();
        AssertShouldMarkInZone(marker, obj);
        goto scan_obj;
      }

      case MarkStack::SymbolTag: {
        auto* symbol = ptr.as<JS::Symbol>();
        maybeMarkImplicitEdges(symbol);
        AutoSetTracingSource asts(this, symbol);
        symbol->traceChildren(this);
        return true;
      }

      case MarkStack::JitCodeTag: {
        auto* code = ptr.as<jit::JitCode>();
        AutoSetTracingSource asts(this, code);
        code->traceChildren(this);
        return true;
      }

      case MarkStack::ScriptTag: {
        auto* script = ptr.as<BaseScript>();
        maybeMarkImplicitEdges(script);
        AutoSetTracingSource asts(this, script);
        script->traceChildren(this);
        return true;
      }

      default:
        MOZ_CRASH("Invalid tag in mark stack");
    }
  }

  return true;

scan_value_range:
  MemoryAcquireFence<opts>(this->runtime());

  while (index < end) {
    MOZ_ASSERT(stack.capacity() >= stack.position() + ValueRangeWords);

    budget.step();
    if (budget.isOverBudget()) {
      marker->pushValueRange(obj, kind, index, end);
      return false;
    }

    Value v = base[index];
    index++;

    if (!v.isGCThing()) {
      continue;
    }

    if (v.isString()) {
      markAndTraverseEdge(obj, v.toString());
    } else if (v.isObject()) {
      JSObject* obj2 = &v.toObject();
#ifdef DEBUG
      if (!obj2) {
        fprintf(stderr,
                "processMarkStackTop found ObjectValue(nullptr) "
                "at %zu Values from end of range in object:\n",
                size_t(end - (index - 1)));
        obj->dump();
      }
#endif
      CheckForCompartmentMismatch(obj, obj2);
      if (mark(obj2)) {
        // Save the rest of this value range for later and start scanning obj2's
        // children.
        marker->pushValueRange(obj, kind, index, end);
        obj = obj2;
        goto scan_obj;
      }
    } else if (v.isSymbol()) {
      if (!markAndTraverseSymbol(obj, v.toSymbol())) {
        return true;
      }
    } else if (v.isBigInt()) {
      markAndTraverseEdge(obj, v.toBigInt());
    } else {
      MOZ_ASSERT(v.isPrivateGCThing());
      if (!markAndTraversePrivateGCThing(obj, v.toGCThing())) {
        return true;
      }
    }
  }

  return true;

scan_obj: {
  AssertShouldMarkInZone(marker, obj);

  maybeMarkImplicitEdges(obj);
  markAndTraverseEdge(obj, obj->shape());

  const JSClass* clasp = obj->getClass();
  if (clasp->hasTrace() && !callOrDelayTraceHook(obj, clasp, budget)) {
    return false;
  }

  if (!obj->is<NativeObject>()) {
    return true;
  }

  // Ensure stack headroom for three ranges (fixed slots, dynamic slots and
  // elements).
  if (MOZ_UNLIKELY(!stack.ensureSpace(ValueRangeWords * 3))) {
    marker->delayMarkingChildrenOnOOM(obj);
    return true;
  }

  // For concurrent marking, we need to read all object fields at most once to
  // prevent the possibility of seeing different values each time.
  NativeObject* nobj = &obj->as<NativeObject>();
  Shape* shape = nobj->shape();
  HeapSlot* slotsPtr = nobj->slots_;
  HeapSlot* elementsPtr = nobj->elements_;

  // Get number of slots using previously read shape and slots pointers.
  ObjectSlots* slotsHeader = ObjectSlots::fromSlots(slotsPtr);
  unsigned nslots = NativeObjectSlotSpan(shape, slotsHeader);
  unsigned nfixed = NumNativeObjectFixedSlots(shape);

  if (IsNativeObjectDynamicSlots(slotsPtr)) {
    MarkTenuredBuffer(nobj->zone(), slotsHeader);
  }

  ObjectElements* elementsHeader = ObjectElements::fromElements(elementsPtr);
  if (IsNativeObjectDynamicElements(elementsPtr)) {
    void* unshiftedHeader = elementsHeader->getUnshiftedHeader();
    MarkTenuredBuffer(nobj->zone(), unshiftedHeader);
  }

  if (!IsNativeObjectEmptyElements(elementsPtr)) {
    base = elementsPtr;
    kind = SlotsOrElementsKind::Elements;
    index = 0;
    end = elementsHeader->getInitializedLength();

    if (!nslots) {
      // No slots at all. Scan elements immediately.
      goto scan_value_range;
    }

    marker->pushValueRange(nobj, kind, index, end);
  }

  base = nobj->fixedSlots();
  kind = SlotsOrElementsKind::FixedSlots;
  index = 0;

  if (nslots > nfixed) {
    // Push dynamic slots for later scan.
    marker->pushValueRange(nobj, SlotsOrElementsKind::DynamicSlots, 0,
                           nslots - nfixed);
    end = nfixed;
  } else {
    end = nslots;
  }

  // Scan any fixed slots.
  goto scan_value_range;
}
}

template <uint32_t opts>
bool MarkingTracerT<opts>::callOrDelayTraceHook(JSObject* obj,
                                                const JSClass* clasp,
                                                JS::SliceBudget& budget) {
  MOZ_ASSERT(clasp->hasTrace());

#ifdef JS_GC_CONCURRENT_MARKING
  if constexpr (hasOption(MarkingOptions::ConcurrentMarking)) {
    // TODO: Add a class flag to allow us to call the trace hook concurrently
    // for classes that support it.
    GCMarker* marker = gcMarker();
    if (MOZ_UNLIKELY(!marker->addToMainThreadBuffer(obj, budget))) {
      marker->delayMarkingChildrenOnOOM(obj);
      return false;
    }
    return true;
  }
#endif

  AutoSetTracingSource asts(this, obj);
  clasp->doTrace(this, obj);
  return true;
}

/*** Mark Stack *************************************************************/

static_assert(sizeof(MarkStack::TaggedPtr) == sizeof(uintptr_t),
              "A TaggedPtr should be the same size as a pointer");
static_assert((sizeof(MarkStack::SlotsOrElementsRange) % sizeof(uintptr_t)) ==
                  0,
              "SlotsOrElementsRange size should be a multiple of "
              "the pointer size");

template <typename T>
struct MapTypeToMarkStackTag {};
template <>
struct MapTypeToMarkStackTag<JSObject*> {
  static const auto value = MarkStack::ObjectTag;
};
template <>
struct MapTypeToMarkStackTag<JS::Symbol*> {
  static const auto value = MarkStack::SymbolTag;
};
template <>
struct MapTypeToMarkStackTag<jit::JitCode*> {
  static const auto value = MarkStack::JitCodeTag;
};
template <>
struct MapTypeToMarkStackTag<BaseScript*> {
  static const auto value = MarkStack::ScriptTag;
};

static inline bool TagIsRangeTag(MarkStack::Tag tag) {
  return tag == MarkStack::SlotsOrElementsRangeTag;
}

inline MarkStack::TaggedPtr::TaggedPtr(Tag tag, Cell* ptr)
    : bits(tag | uintptr_t(ptr)) {
  assertValid();
}

/* static */
inline MarkStack::TaggedPtr MarkStack::TaggedPtr::fromBits(uintptr_t bits) {
  return TaggedPtr(bits);
}

inline MarkStack::TaggedPtr::TaggedPtr(uintptr_t bits) : bits(bits) {
  assertValid();
}

inline uintptr_t MarkStack::TaggedPtr::asBits() const { return bits; }

inline MarkStack::Tag MarkStack::TaggedPtr::tag() const {
  auto tag = Tag(bits & TagMask);
  MOZ_ASSERT(tag <= LastTag);
  return tag;
}

inline Cell* MarkStack::TaggedPtr::ptr() const {
  return reinterpret_cast<Cell*>(bits & ~TagMask);
}

inline void MarkStack::TaggedPtr::assertValid() const {
  (void)tag();
  MOZ_ASSERT(IsCellPointerValid(ptr()));
}

template <typename T>
inline T* MarkStack::TaggedPtr::as() const {
  MOZ_ASSERT(tag() == MapTypeToMarkStackTag<T*>::value);
  MOZ_ASSERT(ptr()->isTenured());
  MOZ_ASSERT(ptr()->is<T>());
  return static_cast<T*>(ptr());
}

inline JSObject* MarkStack::TaggedPtr::asRangeObject() const {
  MOZ_ASSERT(TagIsRangeTag(tag()));
  MOZ_ASSERT(ptr()->isTenured());
  return ptr()->as<JSObject>();
}

inline JSRope* MarkStack::TaggedPtr::asTempRope() const {
  MOZ_ASSERT(tag() == TempRopeTag);
  return &ptr()->as<JSString>()->asRope();
}

inline MarkStack::SlotsOrElementsRange::SlotsOrElementsRange(
    SlotsOrElementsKind kindArg, JSObject* obj, size_t startArg)
    : startAndKind_((startArg << StartShift) | size_t(kindArg)),
      ptr_(SlotsOrElementsRangeTag, obj) {
  assertValid();
  MOZ_ASSERT(kind() == kindArg);
  MOZ_ASSERT(start() == startArg);
}

/* static */
inline MarkStack::SlotsOrElementsRange
MarkStack::SlotsOrElementsRange::fromBits(uintptr_t startAndKind,
                                          uintptr_t ptr) {
  return SlotsOrElementsRange(startAndKind, ptr);
}

inline MarkStack::SlotsOrElementsRange::SlotsOrElementsRange(
    uintptr_t startAndKind, uintptr_t ptr)
    : startAndKind_(startAndKind), ptr_(TaggedPtr::fromBits(ptr)) {
  assertValid();
}

inline void MarkStack::SlotsOrElementsRange::assertValid() const {
  ptr_.assertValid();
  MOZ_ASSERT(TagIsRangeTag(ptr_.tag()));
}

inline SlotsOrElementsKind MarkStack::SlotsOrElementsRange::kind() const {
  return SlotsOrElementsKind(startAndKind_ & KindMask);
}

inline size_t MarkStack::SlotsOrElementsRange::start() const {
  return startAndKind_ >> StartShift;
}

inline void MarkStack::SlotsOrElementsRange::setStart(size_t newStart) {
  startAndKind_ = (newStart << StartShift) | uintptr_t(kind());
  MOZ_ASSERT(start() == newStart);
}

inline void MarkStack::SlotsOrElementsRange::setEmpty() {
  // Replace this SlotsOrElementsRange with something that's valid for marking
  // but doesn't involve accessing this range, which is now invalid. This
  // replaces the two-word range with two single-word entries for the owning
  // object.
  TaggedPtr entry(ObjectTag, ptr().asRangeObject());
  ptr_ = entry;
  startAndKind_ = entry.asBits();
}

inline MarkStack::TaggedPtr MarkStack::SlotsOrElementsRange::ptr() const {
  return ptr_;
}

inline uintptr_t MarkStack::SlotsOrElementsRange::asBits0() const {
  return startAndKind_;
}

inline uintptr_t MarkStack::SlotsOrElementsRange::asBits1() const {
  return ptr_.asBits();
}

MarkStack::MarkStack() { MOZ_ASSERT(isEmpty()); }

MarkStack::~MarkStack() {
  MOZ_ASSERT(isEmpty());
  clearAndFreeStack();
}

void MarkStack::swap(MarkStack& other) {
  std::swap(stack_, other.stack_);
  std::swap(capacity_, other.capacity_);
  std::swap(topIndex_, other.topIndex_);
#ifdef JS_GC_ZEAL
  std::swap(maxCapacity_, other.maxCapacity_);
#endif
#ifdef DEBUG
  std::swap(elementsRangesAreValid, other.elementsRangesAreValid);
#endif
}

bool MarkStack::init() { return resetStackCapacity(); }

bool MarkStack::resetStackCapacity() {
  MOZ_ASSERT(isEmpty());

  size_t capacity = MARK_STACK_BASE_CAPACITY;

#ifdef JS_GC_ZEAL
  capacity = std::min(capacity, maxCapacity_.ref());
#endif

  return resize(capacity);
}

#ifdef JS_GC_ZEAL
void MarkStack::setMaxCapacity(size_t maxCapacity) {
  MOZ_ASSERT(maxCapacity != 0);
  MOZ_ASSERT(isEmpty());

  maxCapacity_ = maxCapacity;
  if (capacity() > maxCapacity_) {
    // If the realloc fails, just keep using the existing stack; it's
    // not ideal but better than failing.
    (void)resize(maxCapacity_);
  }
}
#endif

MOZ_ALWAYS_INLINE bool MarkStack::indexIsEntryBase(size_t index) const {
  // The mark stack holds both TaggedPtr and SlotsOrElementsRange entries, which
  // are one or two words long respectively. Determine whether |index| points to
  // the base of an entry (i.e. the lowest word in memory).
  //
  // The possible cases are that |index| points to:
  //  1. a single word TaggedPtr entry => true
  //  2. the startAndKind_ word of SlotsOrElementsRange => true
  //     (startAndKind_ is a uintptr_t tagged with SlotsOrElementsKind)
  //  3. the ptr_ word of SlotsOrElementsRange (itself a TaggedPtr) => false
  //
  // To check for case 3, interpret the word as a TaggedPtr: if it is tagged as
  // a SlotsOrElementsRange tagged pointer then we are inside such a range and
  // |index| does not point to the base of an entry. This requires that no
  // startAndKind_ word can be interpreted as such, which is arranged by making
  // SlotsOrElementsRangeTag zero and all SlotsOrElementsKind tags non-zero.

  MOZ_ASSERT(index < capacity_);
  return (stack_[index] & TagMask) != SlotsOrElementsRangeTag;
}

/* static */
void MarkStack::moveAllWork(MarkStack& dst, MarkStack& src) {
  MOZ_ASSERT(src.elementsRangesAreValid == dst.elementsRangesAreValid);

  if (dst.isEmpty()) {
    dst.swap(src);
    return;
  }

  size_t wordsToMove = src.position();

  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!dst.ensureSpace<false>(wordsToMove)) {
    oomUnsafe.crash("MarkStack::moveAllWork");
  }

  mozilla::PodCopy(dst.end(), src.ptr(0), wordsToMove);
  dst.topIndex_ += wordsToMove;
  src.topIndex_ = 0;  // Doesn't reset capacity.

  MOZ_ASSERT(src.isEmpty());
}

/* static */
size_t MarkStack::moveSomeWork(GCMarker* marker, MarkStack& dst, MarkStack& src,
                               bool allowDistribute) {
  // Move some work from |src| to |dst|. Assumes |dst| is empty.
  //
  // When this method runs during parallel marking, we are on the thread that
  // owns |src|, and the thread that owns |dst| is blocked waiting on the
  // ParallelMarkTask::resumed condition variable.

  MOZ_ASSERT(dst.isEmpty());
  MOZ_ASSERT(src.elementsRangesAreValid == dst.elementsRangesAreValid);

  // Limit the size of moves to stop threads with work spending too much time
  // donating.
  static const size_t MaxWordsToMove = 4096;

  size_t totalWords = src.position();
  size_t wordsToMove = std::min(totalWords / 2, MaxWordsToMove);

  // Mark stack entries do not represent uniform amounts of marking work (they
  // are either single GC things or arbitrarily large arrays) and when the mark
  // stack is small the situation often arises where one thread repeatedly takes
  // what is in effect a small amount of marking work while leaving the other
  // thread with a whole lot more. To split the work up more effectively we
  // randomly distribute stack entries for small stack.
  //
  // This works by randomly choosing one of every pair of entries in |src| and
  // moving it to |dst| (rather than moving half of the stack as a contiguous
  // region).
  //
  // This has the effect of reducing the number of donations between threads. It
  // does not decrease average marking time but it does decrease variance of
  // marking time.
  static constexpr size_t MaxWordsToDistribute = 30;
  if (allowDistribute && totalWords <= MaxWordsToDistribute) {
    if (!dst.ensureSpace(totalWords)) {
      return 0;
    }

    src.topIndex_ = 0;

    // We will use bits from a single 64-bit random number.
    static_assert(HowMany(MaxWordsToDistribute, 2) <= 64);
    uint64_t randomBits = marker->random.ref().next();
    DebugOnly<size_t> randomBitCount = 64;

    size_t i = 0;    // Entry index.
    size_t pos = 0;  // Source stack position.
    uintptr_t* data = src.stack_;
    while (pos < totalWords) {
      MOZ_ASSERT(src.indexIsEntryBase(pos));

      // Randomly chose which stack to copy the entry to, with each half of each
      // pair of entries moving to different stacks.
      MOZ_ASSERT(randomBitCount != 0);
      bool whichStack = (randomBits & 1) ^ (i & 1);
      randomBits >>= i & 1;
      randomBitCount -= i & 1;

      MarkStack& stack = whichStack ? dst : src;

      bool isRange =
          pos < totalWords - 1 && TagIsRangeTag(Tag(data[pos + 1] & TagMask));
      if (isRange) {
        stack.infalliblePush(
            SlotsOrElementsRange::fromBits(data[pos], data[pos + 1]));
        pos += ValueRangeWords;
      } else {
        stack.infalliblePush(TaggedPtr::fromBits(data[pos]));
        pos++;
      }

      i++;
    }

    return totalWords;
  }

  size_t targetPos = src.position() - wordsToMove;

  // Adjust the target position in case it points to the middle of a two word
  // entry.
  if (!src.indexIsEntryBase(targetPos)) {
    targetPos--;
    wordsToMove++;
  }
  MOZ_ASSERT(src.indexIsEntryBase(targetPos));
  MOZ_ASSERT(targetPos < src.position());
  MOZ_ASSERT(targetPos > 0);
  MOZ_ASSERT(wordsToMove == src.position() - targetPos);

  if (!dst.ensureSpace(wordsToMove)) {
    return 0;
  }

  // TODO: This doesn't have good cache behaviour when moving work between
  // threads. It might be better if the original thread ended up with the top
  // part of the stack, in src words if this method stole from the bottom of
  // the stack rather than the top.

  mozilla::PodCopy(dst.end(), src.stack_ + targetPos, wordsToMove);
  dst.topIndex_ += wordsToMove;
  dst.peekPtr().assertValid();

  src.topIndex_ = targetPos;
#ifdef DEBUG
  src.poisonUnused();
#endif
  src.peekPtr().assertValid();
  return wordsToMove;
}

void MarkStack::clearAndResetCapacity() {
  // Fall back to the smaller initial capacity so we don't hold on to excess
  // memory between GCs.
  topIndex_ = 0;
  (void)resetStackCapacity();
}

void MarkStack::clearAndFreeStack() {
  // Free all stack memory so we don't hold on to excess memory between GCs.
  js_free(stack_);
  stack_ = nullptr;
  capacity_ = 0;
  topIndex_ = 0;
}

template <typename T>
inline bool MarkStack::push(T* ptr) {
  return push(TaggedPtr(MapTypeToMarkStackTag<T*>::value, ptr));
}

inline bool MarkStack::pushTempRope(JSRope* rope) {
  return push(TaggedPtr(TempRopeTag, rope));
}

inline bool MarkStack::push(const TaggedPtr& ptr) {
  if (!ensureSpace(1)) {
    return false;
  }

  infalliblePush(ptr);
  return true;
}

inline void MarkStack::infalliblePush(const TaggedPtr& ptr) {
  MOZ_ASSERT(position() + 1 <= capacity());
  *end() = ptr.asBits();
  topIndex_++;
}

inline void MarkStack::infalliblePush(JSObject* obj, SlotsOrElementsKind kind,
                                      size_t start) {
  SlotsOrElementsRange range(kind, obj, start);
  infalliblePush(range);
}

inline void MarkStack::infalliblePush(const SlotsOrElementsRange& range) {
  MOZ_ASSERT(position() + ValueRangeWords <= capacity());

  range.assertValid();
  end()[0] = range.asBits0();
  end()[1] = range.asBits1();
  topIndex_ += ValueRangeWords;
  MOZ_ASSERT(TagIsRangeTag(peekTag()));
}

inline MarkStack::TaggedPtr MarkStack::peekPtr() const {
  MOZ_ASSERT(!isEmpty());
  return TaggedPtr::fromBits(at(topIndex_ - 1));
}

inline MarkStack::Tag MarkStack::peekTag() const {
  MOZ_ASSERT(!isEmpty());
  return peekPtr().tag();
}

inline MarkStack::TaggedPtr MarkStack::popPtr() {
  MOZ_ASSERT(!isEmpty());
  MOZ_ASSERT(!TagIsRangeTag(peekTag()));
  peekPtr().assertValid();
  topIndex_--;
  return TaggedPtr::fromBits(*end());
}

inline MarkStack::SlotsOrElementsRange MarkStack::popSlotsOrElementsRange() {
  MOZ_ASSERT(!isEmpty());
  MOZ_ASSERT(TagIsRangeTag(peekTag()));
  MOZ_ASSERT(position() >= ValueRangeWords);

  topIndex_ -= ValueRangeWords;
  return SlotsOrElementsRange::fromBits(end()[0], end()[1]);
}

template <bool checkMaxCapacity>
inline bool MarkStack::ensureSpace(size_t count) {
  size_t required = topIndex_ + count;
  if (MOZ_LIKELY(required <= capacity())) {
    return true;
  }

  size_t newCapacity = mozilla::RoundUpPow2(required);

#ifdef JS_GC_ZEAL
  if constexpr (checkMaxCapacity) {
    newCapacity = std::min(newCapacity, maxCapacity_.ref());
    if (newCapacity < required) {
      return false;
    }
  }
#endif

  return resize(newCapacity);
}

bool MarkStack::resize(size_t newCapacity) {
  MOZ_ASSERT(newCapacity != 0);
  MOZ_ASSERT(newCapacity >= position());

  auto poisonOnExit = mozilla::MakeScopeExit([this]() { poisonUnused(); });

  if (newCapacity == capacity_) {
    return true;
  }

  uintptr_t* newStack =
      js_pod_realloc<uintptr_t>(stack_, capacity_, newCapacity);
  if (!newStack) {
    return false;
  }

  stack_ = newStack;
  capacity_ = newCapacity;
  return true;
}

inline void MarkStack::poisonUnused() {
  static_assert((JS_FRESH_MARK_STACK_PATTERN & TagMask) > LastTag,
                "The mark stack poison pattern must not look like a valid "
                "tagged pointer");

  MOZ_ASSERT(topIndex_ <= capacity_);
  AlwaysPoison(stack_ + topIndex_, JS_FRESH_MARK_STACK_PATTERN,
               capacity_ - topIndex_, MemCheckKind::MakeUndefined);
}

size_t MarkStack::sizeOfExcludingThis() const {
  return capacity_ * sizeof(uintptr_t);
}

MarkStackIter::MarkStackIter(MarkStack& stack)
    : stack_(stack), pos_(stack.position()) {}

inline size_t MarkStackIter::position() const { return pos_; }

inline bool MarkStackIter::done() const { return position() == 0; }

inline void MarkStackIter::next() {
  if (isSlotsOrElementsRange()) {
    MOZ_ASSERT(position() >= ValueRangeWords);
    pos_ -= ValueRangeWords;
    return;
  }

  MOZ_ASSERT(!done());
  pos_--;
}

inline bool MarkStackIter::isSlotsOrElementsRange() const {
  return TagIsRangeTag(peekTag());
}

inline MarkStack::Tag MarkStackIter::peekTag() const { return peekPtr().tag(); }

inline MarkStack::TaggedPtr MarkStackIter::peekPtr() const {
  MOZ_ASSERT(!done());
  return MarkStack::TaggedPtr::fromBits(stack_.at(pos_ - 1));
}

inline MarkStack::SlotsOrElementsRange MarkStackIter::slotsOrElementsRange()
    const {
  MOZ_ASSERT(TagIsRangeTag(peekTag()));
  MOZ_ASSERT(position() >= ValueRangeWords);

  uintptr_t* ptr = stack_.ptr(pos_ - ValueRangeWords);
  return MarkStack::SlotsOrElementsRange::fromBits(ptr[0], ptr[1]);
}

inline void MarkStackIter::setSlotsOrElementsRange(
    const MarkStack::SlotsOrElementsRange& range) {
  MOZ_ASSERT(isSlotsOrElementsRange());

  uintptr_t* ptr = stack_.ptr(pos_ - ValueRangeWords);
  ptr[0] = range.asBits0();
  ptr[1] = range.asBits1();
}

/*** GCMarker ***************************************************************/

/*
 * WeakMapTraceAction::Expand: the GC is recomputing the liveness of WeakMap
 * entries by expanding each live WeakMap into its constituent key->value edges,
 * a table of which will be consulted in a later phase whenever marking a
 * potential key.
 */
GCMarker::GCMarker(JSRuntime* rt)
    : tracer_(mozilla::VariantType<MarkingTracer>(), rt, this),
      runtime_(rt),
      haveSwappedStacks(false),
      markColor_(MarkColor::Black),
      state(NotActive),
      incrementalWeakMapMarkingEnabled(
          TuningDefaults::IncrementalWeakMapMarkingEnabled),
      random(js::GenerateRandomSeed(), js::GenerateRandomSeed())
#ifdef DEBUG
      ,
      checkAtomMarking(true),
      strictCompartmentChecking(false)
#endif
{
}

bool GCMarker::init() { return stack.init(); }

bool GCMarker::isDrained() const {
#ifdef JS_GC_CONCURRENT_MARKING
  if (!mainThreadBuffersAreEmpty()) {
    return false;
  }
#endif

  return isMarkStackEmpty();
}

void GCMarker::start() {
  MOZ_ASSERT(state == NotActive);
  MOZ_ASSERT(stack.isEmpty());
  state = RegularMarking;
  haveAllImplicitEdges = true;
  setMarkColor(MarkColor::Black);
}

static void ClearEphemeronEdges(JSRuntime* rt) {
  for (GCZonesIter zone(rt); !zone.done(); zone.next()) {
    zone->gcEphemeronEdges().clearAndCompact();
  }
}

void GCMarker::stop() {
  MOZ_ASSERT(isDrained());
  MOZ_ASSERT(markColor() == MarkColor::Black);

  if (state == NotActive) {
    MOZ_ASSERT(!haveSwappedStacks);
    return;
  }
  state = NotActive;

  if (haveSwappedStacks) {
    swapMarkStacks();
  }
  otherStack.clearAndFreeStack();
  ClearEphemeronEdges(runtime());
  unmarkGrayStack.clearAndFree();
}

void GCMarker::reset() {
  state = NotActive;

  setMarkColor(MarkColor::Black);
  if (haveSwappedStacks) {
    swapMarkStacks();
  }

  stack.clearAndResetCapacity();
  otherStack.clearAndFreeStack();
  ClearEphemeronEdges(runtime());

#ifdef JS_GC_CONCURRENT_MARKING
  blackMainThreadBuffer_.ref().clearAndFree();
  grayMainThreadBuffer_.ref().clearAndFree();
#endif

  MOZ_ASSERT(isDrained());
  MOZ_ASSERT(!haveSwappedStacks);

  unmarkGrayStack.clearAndFree();
}

void GCMarker::setMarkColor(gc::MarkColor newColor) {
  if (markColor_ == newColor) {
    return;
  }

  markColor_ = newColor;

  // Switch stacks. We only need to do this if there are any stack entries (as
  // empty stacks are interchangeable) or to switch back to the original stack.
  if (!isMarkStackEmpty() ||
      (haveSwappedStacks && newColor == MarkColor::Black)) {
    swapMarkStacks();
  }
}

void GCMarker::swapMarkStacks() {
  stack.swap(otherStack);
  haveSwappedStacks = !haveSwappedStacks;
}

bool GCMarker::hasEntries(MarkColor color) const {
  const MarkStack& stackForColor = color == markColor() ? stack : otherStack;
  return stackForColor.hasEntries();
}

template <typename T>
inline void GCMarker::pushTaggedPtr(T* ptr) {
  MOZ_ASSERT(ptr->isTenured());
  checkZone(ptr);
  if (!stack.push(ptr)) {
    delayMarkingChildrenOnOOM(ptr);
  }
}

inline void GCMarker::pushValueRange(JSObject* obj, SlotsOrElementsKind kind,
                                     size_t start, size_t end) {
  MOZ_ASSERT(obj->isTenured());
  checkZone(obj);
  MOZ_ASSERT(obj->is<NativeObject>());
  MOZ_ASSERT(start <= end);

  if (start != end) {
    stack.infalliblePush(obj, kind, start);
  }
}

void GCMarker::setRootMarkingMode(bool newState) {
  if (newState) {
    setMarkingStateAndTracer<RootMarkingTracer>(RegularMarking, RootMarking);
  } else {
    setMarkingStateAndTracer<MarkingTracer>(RootMarking, RegularMarking);
  }
}

void GCMarker::enterParallelMarkingMode() {
  setMarkingStateAndTracer<ParallelMarkingTracer>(RegularMarking,
                                                  ParallelMarking);
}

void GCMarker::leaveParallelMarkingMode() {
  setMarkingStateAndTracer<MarkingTracer>(ParallelMarking, RegularMarking);
}

void GCMarker::enterConcurrentMarkingMode() {
  setMarkingStateAndTracer<ConcurrentMarkingTracer>(RegularMarking,
                                                    ConcurrentMarking);
}

void GCMarker::leaveConcurrentMarkingMode() {
  setMarkingStateAndTracer<MarkingTracer>(ConcurrentMarking, RegularMarking);
}

// It may not be worth the overhead of donating very few mark stack entries. For
// some (non-parallelizable) workloads this could lead to constantly
// interrupting marking work and makes parallel marking slower than single
// threaded.
//
// Conversely, we do want to try splitting up work occasionally or we may fail
// to parallelize workloads that result in few mark stack entries.
//
// Therefore we try hard to split work up at the start of a slice (calling
// canDonateWork) but when a slice is running we only donate if there is enough
// work to make it worthwhile (calling shouldDonateWork).
bool GCMarker::canDonateWork() const {
  return stack.position() > ValueRangeWords;
}
bool GCMarker::shouldDonateWork() const {
  constexpr size_t MinWordCount = 12;
  static_assert(MinWordCount >= ValueRangeWords,
                "We must always leave at least one stack entry.");

  return stack.position() > MinWordCount;
}

template <typename Tracer>
void GCMarker::setMarkingStateAndTracer(MarkingState prev, MarkingState next) {
  MOZ_ASSERT(state == prev);
  state = next;
  tracer_.emplace<Tracer>(runtime(), this);
}

bool GCMarker::enterWeakMarkingMode() {
  MOZ_ASSERT(tracer()->weakMapAction() == JS::WeakMapTraceAction::Expand);
  if (!haveAllImplicitEdges) {
    return false;
  }

  // During weak marking mode, we maintain a table mapping weak keys to
  // entries in known-live weakmaps. Initialize it with the keys of marked
  // weakmaps -- or more precisely, the keys of marked weakmaps that are
  // mapped to not yet live values. (Once bug 1167452 implements incremental
  // weakmap marking, this initialization step will become unnecessary, as
  // the table will already hold all such keys.)

  // Set state before doing anything else, so any new key that is marked
  // during the following gcEphemeronEdges scan will itself be looked up in
  // gcEphemeronEdges and marked according to ephemeron rules.
  setMarkingStateAndTracer<WeakMarkingTracer>(RegularMarking, WeakMarking);

  return true;
}

IncrementalProgress JS::Zone::enterWeakMarkingMode(GCMarker* marker,
                                                   SliceBudget& budget) {
  MOZ_ASSERT(isGCMarking());
  MOZ_ASSERT(marker->isWeakMarking());

  if (!marker->incrementalWeakMapMarkingEnabled) {
    ForAllWeakMapsInZone(this, [marker](WeakMapBase* map) {
      if (map->isMarked()) {
        (void)map->markEntries(marker);
      }
    });
    return IncrementalProgress::Finished;
  }

  // gcEphemeronEdges contains the keys from all weakmaps marked so far, or at
  // least the keys that might still need to be marked through. Scan through
  // gcEphemeronEdges and mark all values whose keys are marked. This marking
  // may recursively mark through other weakmap entries (immediately since we
  // are now in WeakMarking mode). The end result is a consistent state where
  // all values are marked if both their map and key are marked -- though note
  // that we may later leave weak marking mode, do some more marking, and then
  // enter back in.

  if (!isGCMarking()) {
    return IncrementalProgress::Finished;
  }

  WeakMarkingTracer* trc = marker->getWeakMarkingTracer();
  for (auto iter = gcEphemeronEdges().iter(); !iter.done(); iter.next()) {
    Cell* src = iter.get().key();
    CellColor srcColor = gc::detail::GetEffectiveColor(marker, src);

    auto& edges = iter.get().value();
    size_t numEdges = edges.length();
    if (IsMarked(srcColor) && edges.length() > 0) {
      trc->markEphemeronEdges(edges, AsMarkColor(srcColor));
    }
    budget.step(1 + numEdges);
    if (budget.isOverBudget()) {
      return NotFinished;
    }
  }

  return IncrementalProgress::Finished;
}

void GCMarker::leaveWeakMarkingMode() {
  if (state == RegularMarking) {
    return;
  }

  setMarkingStateAndTracer<MarkingTracer>(WeakMarking, RegularMarking);

  // The gcEphemeronEdges table is still populated and may be used during a
  // future weak marking mode within this GC.
}

void GCMarker::abortLinearWeakMarking() {
  haveAllImplicitEdges = false;
  if (state == WeakMarking) {
    leaveWeakMarkingMode();
  }
}

MOZ_NEVER_INLINE void GCMarker::delayMarkingChildrenOnOOM(Cell* cell) {
  runtime()->gc.delayMarkingChildren(cell, markColor());
}

bool GCRuntime::hasDelayedMarking() const {
  bool result = delayedMarkingList;
  MOZ_ASSERT(result == (markLaterArenas != 0));
  return result;
}

void GCRuntime::delayMarkingChildren(Cell* cell, MarkColor color) {
  // Synchronize access to delayed marking state during parallel marking.
  LockGuard<Mutex> lock(delayedMarkingLock);

  Arena* arena = cell->asTenured().arena();
  if (!arena->onDelayedMarkingList()) {
    arena->setNextDelayedMarkingArena(delayedMarkingList);
    delayedMarkingList = arena;
#ifdef DEBUG
    markLaterArenas++;
#endif
  }

  if (!arena->hasDelayedMarking(color)) {
    arena->setHasDelayedMarking(color, true);
    delayedMarkingWorkAdded = true;
  }
}

void GCRuntime::markDelayedChildren(Arena* arena, MarkColor color) {
  JSTracer* trc = marker().tracer();
  JS::TraceKind kind = MapAllocToTraceKind(arena->getAllocKind());
  MarkColor colorToCheck =
      TraceKindCanBeMarkedGray(kind) ? color : MarkColor::Black;

  for (ArenaCellIterUnderGC cell(arena); !cell.done(); cell.next()) {
    if (cell->isMarked(colorToCheck)) {
      ApplyGCThingTyped(cell, kind, [trc, this](auto t) {
        // Record the source zone so onEdge can update the atom-marking
        // bitmap for any Symbol edges traced via the generic tracer.
        AutoSetTracingSource asts(trc, t);
        t->traceChildren(trc);
        if (marker().isWeakMarking()) {
          marker().getWeakMarkingTracer()->maybeMarkImplicitEdges(t);
        }
      });
    }
  }
}

/*
 * Process arenas from |delayedMarkingList| by marking the unmarked children of
 * marked cells of color |color|.
 *
 * This is called twice, first to mark gray children and then to mark black
 * children.
 */
void GCRuntime::processDelayedMarkingList(MarkColor color) {
  // Marking delayed children may add more arenas to the list, including arenas
  // we are currently processing or have previously processed. Handle this by
  // clearing a flag on each arena before marking its children. This flag will
  // be set again if the arena is re-added. Iterate the list until no new arenas
  // were added.

  AutoSetMarkColor setColor(marker(), color);
  AutoUpdateMarkStackRanges updateRanges(marker());

  do {
    delayedMarkingWorkAdded = false;
    for (Arena* arena = delayedMarkingList; arena;
         arena = arena->getNextDelayedMarking()) {
      if (arena->hasDelayedMarking(color)) {
        arena->setHasDelayedMarking(color, false);
        markDelayedChildren(arena, color);
      }
    }
    if (marker().hasEntriesForCurrentColor()) {
      MOZ_ALWAYS_TRUE(marker().matchTracer([](auto& trc) {
        SliceBudget budget = SliceBudget::unlimited();
        return trc.markCurrentColor(budget);
      }));
    }
  } while (delayedMarkingWorkAdded);

  MOZ_ASSERT(marker().isDrained());
}

void GCRuntime::markAllDelayedChildren(ShouldReportMarkTime reportTime) {
  MOZ_ASSERT(CurrentThreadIsMainThread() || CurrentThreadIsPerformingGC());
  MOZ_ASSERT(marker().isMarkStackEmpty());
  MOZ_ASSERT(hasDelayedMarking());

  mozilla::Maybe<gcstats::AutoPhase> ap;
  if (reportTime) {
    ap.emplace(stats(), gcstats::PhaseKind::MARK_DELAYED);
  }

  // We have a list of arenas containing marked cells with unmarked children
  // where we ran out of stack space during marking. Both black and gray cells
  // in these arenas may have unmarked children. Mark black children first.

  const MarkColor colors[] = {MarkColor::Black, MarkColor::Gray};
  for (MarkColor color : colors) {
    processDelayedMarkingList(color);
    rebuildDelayedMarkingList();
  }

  MOZ_ASSERT(!hasDelayedMarking());
}

void GCRuntime::rebuildDelayedMarkingList() {
  // Rebuild the delayed marking list, removing arenas which do not need further
  // marking.

  Arena* listTail = nullptr;
  forEachDelayedMarkingArena([&](Arena* arena) {
    if (!arena->hasAnyDelayedMarking()) {
      arena->clearDelayedMarkingState();
#ifdef DEBUG
      MOZ_ASSERT(markLaterArenas);
      markLaterArenas--;
#endif
      return;
    }

    appendToDelayedMarkingList(&listTail, arena);
  });
  appendToDelayedMarkingList(&listTail, nullptr);
}

void GCRuntime::resetDelayedMarking() {
  MOZ_ASSERT(CurrentThreadIsMainThread());

  forEachDelayedMarkingArena([&](Arena* arena) {
    MOZ_ASSERT(arena->onDelayedMarkingList());
    arena->clearDelayedMarkingState();
#ifdef DEBUG
    MOZ_ASSERT(markLaterArenas);
    markLaterArenas--;
#endif
  });
  delayedMarkingList = nullptr;
  MOZ_ASSERT(!markLaterArenas);
}

inline void GCRuntime::appendToDelayedMarkingList(Arena** listTail,
                                                  Arena* arena) {
  if (*listTail) {
    (*listTail)->updateNextDelayedMarkingArena(arena);
  } else {
    delayedMarkingList = arena;
  }
  *listTail = arena;
}

template <typename F>
inline void GCRuntime::forEachDelayedMarkingArena(F&& f) {
  Arena* arena = delayedMarkingList;
  Arena* next;
  while (arena) {
    next = arena->getNextDelayedMarking();
    f(arena);
    arena = next;
  }
}

#ifdef DEBUG
void GCMarker::checkZone(Cell* cell) {
  MOZ_ASSERT(state != NotActive);
  if (cell->isTenured()) {
    Zone* zone = cell->asTenured().zone();
    MOZ_ASSERT(zone->isGCMarkingOrVerifyingPreBarriers() ||
               zone->isAtomsZone());
  }
}
#endif

size_t GCMarker::sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
  return mallocSizeOf(this) + stack.sizeOfExcludingThis() +
         otherStack.sizeOfExcludingThis();
}

/*** IsMarked / IsAboutToBeFinalized ****************************************/

template <typename T>
static inline void CheckIsMarkedThing(T* thing) {
#define IS_SAME_TYPE_OR(name, type, _, _1) std::is_same_v<type, T> ||
  static_assert(JS_FOR_EACH_TRACEKIND(IS_SAME_TYPE_OR) false,
                "Only the base cell layout types are allowed into "
                "marking/tracing internals");
#undef IS_SAME_TYPE_OR

#ifdef DEBUG
  MOZ_ASSERT(thing);

  // Allow any thread access to uncollected things.
  Zone* zone = thing->zoneFromAnyThread();
  if (thing->isPermanentAndMayBeShared()) {
    // Shared things are not collected and should always be marked, except
    // during shutdown when we've merged shared atoms back into the main atoms
    // zone.
    if (zone->wasGCStarted()) {
      MOZ_ASSERT(!zone->runtimeFromAnyThread()->gc.maybeSharedAtomsZone());
      return;
    }
    MOZ_ASSERT(!zone->needsMarkingBarrier());
    MOZ_ASSERT(thing->isMarkedBlack());
    return;
  }

  // Allow the current thread access if it is sweeping or in sweep-marking, but
  // try to check the zone. Some threads have access to all zones when sweeping.
  JS::GCContext* gcx = TlsGCContext.get();
  MOZ_ASSERT(gcx->gcUse() != GCUse::Finalizing);
  if (gcx->gcUse() == GCUse::Sweeping || gcx->gcUse() == GCUse::Marking) {
    MOZ_ASSERT_IF(gcx->gcSweepZone(),
                  gcx->gcSweepZone() == zone || zone->isAtomsZone());
    return;
  }

  // Otherwise only allow access from the main thread or this zone's associated
  // thread.
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(thing->runtimeFromAnyThread()) ||
             CurrentThreadCanAccessZone(thing->zoneFromAnyThread()));
#endif
}

template <typename T>
bool js::gc::IsMarkedInternal(JSRuntime* rt, T* thing) {
  // Don't depend on the mark state of other cells during finalization.
  MOZ_ASSERT(!CurrentThreadIsGCFinalizing());
  MOZ_ASSERT(rt->heapState() != JS::HeapState::MinorCollecting);
  MOZ_ASSERT(thing);
  CheckIsMarkedThing(thing);

  // This is not used during minor sweeping nor used to update moved GC things.
  MOZ_ASSERT(!IsForwarded(thing));

  // Permanent things are never marked by non-owning runtimes.
  TenuredCell* cell = &thing->asTenured();
  Zone* zone = cell->zoneFromAnyThread();
#ifdef DEBUG
  if (IsOwnedByOtherRuntime(rt, thing)) {
    MOZ_ASSERT(!zone->wasGCStarted());
    MOZ_ASSERT(thing->isMarkedBlack());
  }
#endif

  return !zone->isGCMarking() || TenuredThingIsMarkedAny(thing);
}

template <typename T>
bool js::gc::IsAboutToBeFinalizedInternal(T* thing) {
  // Don't depend on the mark state of other cells during finalization.
  MOZ_ASSERT(!CurrentThreadIsGCFinalizing());
  MOZ_ASSERT(thing);
  CheckIsMarkedThing(thing);

  // This is not used during minor sweeping nor used to update moved GC things.
  MOZ_ASSERT(!IsForwarded(thing));

  if (!thing->isTenured()) {
    return false;
  }

  // Permanent things are never finalized by non-owning runtimes.
  TenuredCell* cell = &thing->asTenured();
  Zone* zone = cell->zoneFromAnyThread();
#ifdef DEBUG
  JSRuntime* rt = TlsGCContext.get()->runtimeFromAnyThread();
  if (IsOwnedByOtherRuntime(rt, thing)) {
    MOZ_ASSERT(!zone->wasGCStarted());
    MOZ_ASSERT(thing->isMarkedBlack());
  }
#endif

  return zone->isGCSweeping() && !TenuredThingIsMarkedAny(thing);
}

template <typename T>
bool js::gc::IsAboutToBeFinalizedInternal(const T& thing) {
  bool dying = false;
  ApplyGCThingTyped(
      thing, [&dying](auto t) { dying = IsAboutToBeFinalizedInternal(t); });
  return dying;
}

SweepingTracer::SweepingTracer(JSRuntime* rt)
    : GenericTracerImpl(rt, JS::TracerKind::Sweeping,
                        JS::WeakMapTraceAction::TraceKeysAndValues) {}

template <typename T>
inline void SweepingTracer::onEdge(T** thingp, const char* name) {
  T* thing = *thingp;
  if (!thing) {
    return;
  }

  CheckIsMarkedThing(thing);

  if (!thing->isTenured()) {
    return;
  }

  TenuredCell* cell = &thing->asTenured();
  Zone* zone = cell->zoneFromAnyThread();

#ifdef DEBUG
  // Permanent things are never finalized by non-owning runtimes.
  if (IsOwnedByOtherRuntime(runtime(), thing)) {
    MOZ_ASSERT(!zone->wasGCStarted());
    MOZ_ASSERT(thing->isMarkedBlack());
  }

  // Any zone can contain references to symbols so make sure we've finished
  // marking them before we try and sweep them. If this fails then we missed
  // adding a sweep group edge somewhere. This check can be disabled in places
  // where we only care about references from the current zone.
  if (cell->getTraceKind() == JS::TraceKind::Symbol && !cell->isMarkedBlack() &&
      !allowSweepingSymbolsEarly) {
    MOZ_ASSERT(!zone->isGCMarking());
  }
#endif

  // It would be nice if we could assert that the zone of the tenured cell is in
  // the Sweeping state, but that isn't always true for:
  //  - atoms
  //  - the jitcode map
  //  - the mark queue
  if ((zone->isGCSweeping() || (zone->isAtomsZone() && zone->isGCMarking())) &&
      !cell->isMarkedAny()) {
    *thingp = nullptr;
  }
}

namespace js::gc {

template <typename T>
JS_PUBLIC_API bool TraceWeakEdge(JSTracer* trc, JS::Heap<T>* thingp) {
  return TraceEdgeInternal(trc, gc::ConvertToBase(thingp->unsafeAddress()),
                           "JS::Heap edge");
}

template <typename T>
JS_PUBLIC_API bool EdgeNeedsSweepUnbarrieredSlow(T* thingp) {
  return IsAboutToBeFinalizedInternal(*ConvertToBase(thingp));
}

// Instantiate a copy of the Tracing templates for each public GC type.
#define INSTANTIATE_ALL_VALID_HEAP_TRACE_FUNCTIONS(type)            \
  template JS_PUBLIC_API bool TraceWeakEdge<type>(JSTracer * trc,   \
                                                  JS::Heap<type>*); \
  template JS_PUBLIC_API bool EdgeNeedsSweepUnbarrieredSlow<type>(type*);
JS_FOR_EACH_PUBLIC_GC_POINTER_TYPE(INSTANTIATE_ALL_VALID_HEAP_TRACE_FUNCTIONS)
JS_FOR_EACH_PUBLIC_TAGGED_GC_POINTER_TYPE(
    INSTANTIATE_ALL_VALID_HEAP_TRACE_FUNCTIONS)

#define INSTANTIATE_INTERNAL_IS_MARKED_FUNCTION(type) \
  template bool IsMarkedInternal(JSRuntime* rt, type thing);

#define INSTANTIATE_INTERNAL_IATBF_FUNCTION(type) \
  template bool IsAboutToBeFinalizedInternal(type thingp);

#define INSTANTIATE_INTERNAL_MARKING_FUNCTIONS_FROM_TRACEKIND(_1, type, _2, \
                                                              _3)           \
  INSTANTIATE_INTERNAL_IS_MARKED_FUNCTION(type*)                            \
  INSTANTIATE_INTERNAL_IATBF_FUNCTION(type*)

JS_FOR_EACH_TRACEKIND(INSTANTIATE_INTERNAL_MARKING_FUNCTIONS_FROM_TRACEKIND)

#define INSTANTIATE_IATBF_FUNCTION_FOR_TAGGED_POINTER(type) \
  INSTANTIATE_INTERNAL_IATBF_FUNCTION(const type&)

JS_FOR_EACH_PUBLIC_TAGGED_GC_POINTER_TYPE(
    INSTANTIATE_IATBF_FUNCTION_FOR_TAGGED_POINTER)

#undef INSTANTIATE_INTERNAL_IS_MARKED_FUNCTION
#undef INSTANTIATE_INTERNAL_IATBF_FUNCTION
#undef INSTANTIATE_INTERNAL_MARKING_FUNCTIONS_FROM_TRACEKIND
#undef INSTANTIATE_IATBF_FUNCTION_FOR_TAGGED_POINTER

}  // namespace js::gc

/*** Cycle Collector Barrier Implementation *********************************/

/*
 * The GC and CC are run independently. Consequently, the following sequence of
 * events can occur:
 * 1. GC runs and marks an object gray.
 * 2. The mutator runs (specifically, some C++ code with access to gray
 *    objects) and creates a pointer from a JS root or other black object to
 *    the gray object. If we re-ran a GC at this point, the object would now be
 *    black.
 * 3. Now we run the CC. It may think it can collect the gray object, even
 *    though it's reachable from the JS heap.
 *
 * To prevent this badness, we unmark the gray bit of an object when it is
 * accessed by callers outside XPConnect. This would cause the object to go
 * black in step 2 above. This must be done on everything reachable from the
 * object being returned. The following code takes care of the recursive
 * re-coloring.
 *
 * There is an additional complication for certain kinds of edges that are not
 * contained explicitly in the source object itself, such as from a weakmap key
 * to its value. These "implicit edges" are represented in some other
 * container object, such as the weakmap itself. In these
 * cases, calling unmark gray on an object won't find all of its children.
 *
 * Handling these implicit edges has two parts:
 * - A special pass enumerating all of the containers that know about the
 *   implicit edges to fix any black-gray edges that have been created. This
 *   is implemented in nsXPConnect::FixWeakMappingGrayBits.
 * - To prevent any incorrectly gray objects from escaping to live JS outside
 *   of the containers, we must add unmark-graying read barriers to these
 *   containers.
 */

#ifdef DEBUG
struct AssertNonGrayTracer final : public JS::CallbackTracer {
  // This is used by the UnmarkGray tracer only, and needs to report itself as
  // the non-gray tracer to not trigger assertions.  Do not use it in another
  // context without making this more generic.
  explicit AssertNonGrayTracer(JSRuntime* rt)
      : JS::CallbackTracer(rt, JS::TracerKind::UnmarkGray) {}
  void onChild(JS::GCCellPtr thing, const char* name) override {
    MOZ_ASSERT(!thing.asCell()->isMarkedGray());
  }
};
#endif

class js::gc::UnmarkGrayTracer final : public JS::CallbackTracer {
 public:
  // We set weakMapAction to WeakMapTraceAction::Skip because the cycle
  // collector will fix up any color mismatches involving weakmaps when it runs.
  explicit UnmarkGrayTracer(GCMarker* marker)
      : JS::CallbackTracer(marker->runtime(), JS::TracerKind::UnmarkGray,
                           JS::WeakMapTraceAction::Skip),
        unmarkedAny(false),
        oom(false),
        marker(marker),
        stack(marker->unmarkGrayStack) {}

  void unmark(JS::GCCellPtr cell);

  // Whether we unmarked anything.
  bool unmarkedAny;

  // Whether we ran out of memory.
  bool oom;

 private:
  // Marker to use if we need to unmark in zones that are currently being
  // marked.
  GCMarker* marker;

  // The source of edges traversed by onChild.
  Zone* sourceZone;

  // Stack of cells to traverse.
  Vector<JS::GCCellPtr, 0, SystemAllocPolicy>& stack;

  void onChild(JS::GCCellPtr thing, const char* name) override;
};

void UnmarkGrayTracer::onChild(JS::GCCellPtr thing, const char* name) {
  Cell* cell = thing.asCell();

  // Cells in the nursery cannot be gray, and nor can certain kinds of tenured
  // cells. These must necessarily point only to black edges.
  if (!cell->isTenured() || !TraceKindCanBeMarkedGray(thing.kind())) {
#ifdef DEBUG
    MOZ_ASSERT(!cell->isMarkedGray());
    AssertNonGrayTracer nongray(runtime());
    JS::TraceChildren(&nongray, thing);
#endif
    return;
  }

  TenuredCell& tenured = cell->asTenured();
  Zone* zone = tenured.zoneFromAnyThread();

  // As well as updating the mark bits, we may need to update the color in the
  // atom marking bitmap for symbols to record that |sourceZone| now has a black
  // edge to |thing|.
  if (zone->isAtomsZone() && sourceZone) {
    GCRuntime* gc = &runtime()->gc;
    if (tenured.is<JS::Symbol>()) {
      JS::Symbol* symbol = tenured.as<JS::Symbol>();
      gc->atomMarking.maybeUnmarkGrayAtomically(sourceZone, symbol);
    }
  }

  // If the cell is in a zone whose mark bits are being cleared, then it will
  // end up being marked black by GC marking.
  if (zone->isGCPreparing()) {
    return;
  }

  // If the cell is already marked black then there's nothing more to do.
  if (tenured.isMarkedBlack()) {
    return;
  }

  if (zone->isGCMarking()) {
    // If the cell is in a zone that we're currently marking then it's possible
    // that it is currently white (but would have ended up gray). To handle this
    // case, mark the cell with the current marking tracer. This will ensure it
    // eventually gets marked black.
    TraceEdgeForBarrier(marker, &tenured, thing.kind());
  } else if (tenured.isMarkedGray()) {
    // TODO: It may be a small improvement to only use the atomic version
    // during parallel marking.
    tenured.markBlackAtomic();
    if (!stack.append(thing)) {
      oom = true;
    }
  }

  unmarkedAny = true;
}

void UnmarkGrayTracer::unmark(JS::GCCellPtr cell) {
  MOZ_ASSERT(stack.empty());

  // TODO: We probably don't need to do anything if the gray bits are
  // invalid. However an early return here causes ExposeGCThingToActiveJS to
  // fail because it asserts that something gets unmarked.

  sourceZone = nullptr;
  onChild(cell, "unmarking root");

  while (!stack.empty() && !oom) {
    JS::GCCellPtr thing = stack.popCopy();
    sourceZone = thing.asCell()->zone();
    TraceChildren(this, thing);
  }

  if (oom) {
    // If we run out of memory, we take a drastic measure: require that we
    // GC again before the next CC.
    stack.clear();
    runtime()->gc.setGrayBitsInvalid();
  }
}

bool js::gc::UnmarkGrayGCThingUnchecked(GCMarker* marker, JS::GCCellPtr thing) {
  MOZ_ASSERT(thing);
  UnmarkGrayTracer unmarker(marker);
  unmarker.unmark(thing);
  return unmarker.unmarkedAny;
}

JS_PUBLIC_API bool JS::UnmarkGrayGCThingRecursively(JS::GCCellPtr thing) {
  MOZ_ASSERT(!JS::RuntimeHeapIsCollecting());
  MOZ_ASSERT(!JS::RuntimeHeapIsCycleCollecting());

  mozilla::Maybe<AutoGeckoProfilerEntry> profilingStackFrame;
  if (JSContext* cx = TlsContext.get()) {
    profilingStackFrame.emplace(cx, "UnmarkGrayGCThing",
                                JS::ProfilingCategoryPair::GCCC_UnmarkGray);
  }

  JSRuntime* rt = thing.asCell()->runtimeFromMainThread();
  if (thing.asCell()->zone()->isGCPreparing()) {
    // Mark bits are being cleared in preparation for GC.
    return false;
  }

  MOZ_ASSERT(thing.asCell()->isMarkedGray());
  return UnmarkGrayGCThingUnchecked(&rt->gc.marker(), thing);
}

void js::gc::UnmarkGrayGCThingRecursively(TenuredCell* cell) {
  JS::UnmarkGrayGCThingRecursively(JS::GCCellPtr(cell, cell->getTraceKind()));
}

#ifdef DEBUG
Cell* js::gc::UninlinedForwarded(const Cell* cell) { return Forwarded(cell); }
#endif

namespace js::debug {

MarkInfo GetMarkInfo(void* vp) {
  GCRuntime& gc = TlsGCContext.get()->runtime()->gc;
  if (gc.nursery().isInside(vp)) {
    ChunkBase* chunk = js::gc::detail::GetGCAddressChunkBase(vp);
    return chunk->getKind() == js::gc::ChunkKind::NurseryFromSpace
               ? MarkInfo::NURSERY_FROMSPACE
               : MarkInfo::NURSERY_TOSPACE;
  }

  if (gc.isPointerWithinBufferAlloc(vp)) {
    return MarkInfo::BUFFER;
  }

  if (!gc.isPointerWithinTenuredCell(vp)) {
    return MarkInfo::UNKNOWN;
  }

  if (!IsCellPointerValid(vp)) {
    return MarkInfo::UNKNOWN;
  }

  TenuredCell* cell = reinterpret_cast<TenuredCell*>(vp);
  if (cell->isMarkedGray()) {
    return MarkInfo::GRAY;
  }
  if (cell->isMarkedBlack()) {
    return MarkInfo::BLACK;
  }
  return MarkInfo::UNMARKED;
}

uintptr_t* GetMarkWordAddress(Cell* cell) {
  if (!cell->isTenured()) {
    return nullptr;
  }

  AtomicBitmapWord* wordp;
  uintptr_t mask;
  ArenaChunkBase* chunk = gc::detail::GetCellChunkBase(&cell->asTenured());
  chunk->markBits.getMarkWordAndMask(&cell->asTenured(), ColorBit::BlackBit,
                                     &wordp, &mask);
  return reinterpret_cast<uintptr_t*>(wordp);
}

uintptr_t GetMarkMask(Cell* cell, uint32_t colorBit) {
  MOZ_ASSERT(colorBit == 0 || colorBit == 1);

  if (!cell->isTenured()) {
    return 0;
  }

  ColorBit bit = colorBit == 0 ? ColorBit::BlackBit : ColorBit::GrayOrBlackBit;
  AtomicBitmapWord* wordp;
  uintptr_t mask;
  ArenaChunkBase* chunk = gc::detail::GetCellChunkBase(&cell->asTenured());
  chunk->markBits.getMarkWordAndMask(&cell->asTenured(), bit, &wordp, &mask);
  return mask;
}

}  // namespace js::debug
