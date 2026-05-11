/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/AllocKind.h"
#include "gc/Cell.h"
#include "gc/GCInternals.h"
#include "gc/GCRuntime.h"
#include "js/ArrayBuffer.h"  // JS::NewArrayBuffer
#include "js/experimental/TypedData.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_GetProperty
#include "js/RootingAPI.h"
#include "jsapi-tests/tests.h"
#include "vm/Runtime.h"
#include "vm/TypedArrayObject.h"

#include "gc/Heap-inl.h"
#include "vm/JSContext-inl.h"

using namespace js;

static js::gc::CellColor GetColor(JSObject* obj) { return obj->color(); }
static js::gc::CellColor GetColor(const JS::ArrayBufferOrView& view) {
  return view.asObjectUnbarriered()->color();
}

[[maybe_unused]] static bool IsInsideNursery(gc::Cell* cell) {
  return !cell->isTenured();
}
[[maybe_unused]] static bool IsInsideNursery(
    const JS::ArrayBufferOrView& view) {
  return IsInsideNursery(view.asObjectUnbarriered());
}

// A heap-allocated structure containing one of our barriered pointer wrappers
// to test.
template <typename W, typename T>
struct TestStruct {
  W wrapper;

  void trace(JSTracer* trc) { TraceEdge(trc, &wrapper, "TestStruct::wrapper"); }

  TestStruct() {}
  explicit TestStruct(T init) : wrapper(init) {}
};

template <typename T>
static T CreateNurseryGCThing(JSContext* cx) = delete;

template <>
JSObject* CreateNurseryGCThing(JSContext* cx) {
  JS::RootedObject obj(cx, JS_NewPlainObject(cx));
  if (!obj) {
    return nullptr;
  }
  JS_DefineProperty(cx, obj, "x", 42, 0);
  MOZ_ASSERT(IsInsideNursery(obj));
  return obj;
}

template <>
JSFunction* CreateNurseryGCThing(JSContext* cx) {
  /*
   * We don't actually use the function as a function, so here we cheat and
   * cast a JSObject.
   */
  return static_cast<JSFunction*>(CreateNurseryGCThing<JSObject*>(cx));
}

template <>
JS::Uint8Array CreateNurseryGCThing(JSContext* cx) {
  JS::Rooted<JS::Uint8Array> arr(cx, JS::Uint8Array::create(cx, 100));
  JS::RootedObject obj(cx, arr.asObject());
  JS_DefineProperty(cx, obj, "x", 42, 0);
  MOZ_ASSERT(IsInsideNursery(obj));
  return arr;
}

template <typename T>
static T CreateTenuredGCThing(JSContext* cx) = delete;

template <>
JSObject* CreateTenuredGCThing(JSContext* cx) {
  // Use ArrayBuffers because they have finalizers, which allows using them in
  // TenuredHeap<> without awkward conversations about nursery allocatability.
  // Note that at some point ArrayBuffers might become nursery allocated at
  // which point this test will have to change.
  JSObject* obj = JS::NewArrayBuffer(cx, 20);
  MOZ_ASSERT(!IsInsideNursery(obj));
  MOZ_ASSERT(obj->getClass()->hasFinalize() &&
             !(obj->getClass()->flags & JSCLASS_SKIP_NURSERY_FINALIZE));
  return obj;
}

template <>
JS::ArrayBuffer CreateTenuredGCThing(JSContext* cx) {
  return JS::ArrayBuffer::fromObject(CreateTenuredGCThing<JSObject*>(cx));
}

template <>
JS::Uint8Array CreateTenuredGCThing(JSContext* cx) {
  // Use internal APIs that lets us specify the InitialHeap so we can ensure
  // that this is tenured.
  JSObject* obj = js::NewUint8ArrayWithLength(cx, 100, gc::Heap::Tenured);
  MOZ_ASSERT(!IsInsideNursery(obj));
  return JS::Uint8Array::fromObject(obj);
}

template <typename T>
void* CreateHiddenTenuredGCThing(JSContext* cx) {
  return CreateTenuredGCThing<T>(cx);
}

template <>
void* CreateHiddenTenuredGCThing<JS::ArrayBuffer>(JSContext* cx) {
  return CreateTenuredGCThing<JS::ArrayBuffer>(cx).asObjectUnbarriered();
}

template <>
void* CreateHiddenTenuredGCThing<JS::Uint8Array>(JSContext* cx) {
  return CreateTenuredGCThing<JS::Uint8Array>(cx).asObjectUnbarriered();
}

static uintptr_t UnbarrieredCastToInt(gc::Cell* cell) {
  return reinterpret_cast<uintptr_t>(cell);
}
static uintptr_t UnbarrieredCastToInt(const JS::ArrayBufferOrView& view) {
  return UnbarrieredCastToInt(view.asObjectUnbarriered());
}

template <typename T>
T RecoverHiddenGCThing(void* ptr) {
  return reinterpret_cast<T>(ptr);
}

template <>
JS::ArrayBuffer RecoverHiddenGCThing(void* ptr) {
  return JS::ArrayBuffer::fromObject(RecoverHiddenGCThing<JSObject*>(ptr));
}

template <>
JS::Uint8Array RecoverHiddenGCThing(void* ptr) {
  return JS::Uint8Array::fromObject(RecoverHiddenGCThing<JSObject*>(ptr));
}

static void MakeGray(JSObject* obj) {
  gc::TenuredCell* cell = &obj->asTenured();
  cell->unmark();
  cell->markIfUnmarked(gc::MarkColor::Gray);
  MOZ_ASSERT(obj->isMarkedGray());
}

static void MakeGray(const JS::ArrayBufferOrView& view) {
  MakeGray(view.asObjectUnbarriered());
}

// Test post-barrier implementation on wrapper types. The following wrapper
// types have post barriers:
//  - JS::Heap
//  - GCPtr
//  - HeapPtr
//  - WeakHeapPtr
BEGIN_TEST(testGCHeapPostBarriers) {
  AutoLeaveZeal nozeal(cx);
  AutoGCParameter disableSemispace(cx, JSGC_SEMISPACE_NURSERY_ENABLED, 0);

  /* Sanity check - objects start in the nursery and then become tenured. */
  JS_GC(cx);
  JS::RootedObject obj(cx, CreateNurseryGCThing<JSObject*>(cx));
  CHECK(IsInsideNursery(obj.get()));
  JS_GC(cx);
  CHECK(!IsInsideNursery(obj.get()));
  JS::RootedObject tenuredObject(cx, obj);

  /* JSObject and JSFunction objects are nursery allocated. */
  CHECK(TestHeapPostBarriersForType<JSObject*>());
  CHECK(TestHeapPostBarriersForType<JSFunction*>());
  CHECK(TestHeapPostBarriersForType<JS::Uint8Array>());
  // Bug 1599378: Add string tests.

  return true;
}

[[nodiscard]] bool TestCanAccessObject(JSObject* obj) {
  JS::RootedObject rootedObj(cx, obj);
  JS::RootedValue value(cx);
  CHECK(JS_GetProperty(cx, rootedObj, "x", &value));
  CHECK(value.isInt32());
  CHECK(value.toInt32() == 42);
  return true;
}
[[nodiscard]] bool TestCanAccessObject(const JS::ArrayBufferOrView& view) {
  return TestCanAccessObject(view.asObject());
}

template <typename T>
[[nodiscard]] bool TestHeapPostBarriersForType() {
  CHECK((TestHeapPostBarriersForWrapper<js::GCPtr, T>()));
  CHECK((TestHeapPostBarriersForMovableWrapper<JS::Heap, T>()));
  CHECK((TestHeapPostBarriersForMovableWrapper<js::HeapPtr, T>()));
  CHECK((TestHeapPostBarriersForMovableWrapper<js::WeakHeapPtr, T>()));
  return true;
}

template <template <typename> class W, typename T>
[[nodiscard]] bool TestHeapPostBarriersForMovableWrapper() {
  CHECK((TestHeapPostBarriersForWrapper<W, T>()));
  CHECK((TestHeapPostBarrierMoveConstruction<W<T>, T>()));
  CHECK((TestHeapPostBarrierMoveAssignment<W<T>, T>()));
  return true;
}

template <template <typename> class W, typename T>
[[nodiscard]] bool TestHeapPostBarriersForWrapper() {
  CHECK((TestHeapPostBarrierConstruction<W<T>, T>()));
  CHECK((TestHeapPostBarrierConstruction<const W<T>, T>()));
  CHECK((TestHeapPostBarrierUpdate<W<T>, T>()));
  if constexpr (!std::is_same_v<W<T>, GCPtr<T>>) {
    // It is not allowed to delete heap memory containing GCPtrs on
    // initialization failure like this and doing so will cause an assertion to
    // fail in the GCPtr destructor (although we disable this in some places in
    // this test).
    CHECK((TestHeapPostBarrierInitFailure<W<T>, T>()));
    CHECK((TestHeapPostBarrierInitFailure<const W<T>, T>()));
  }
  return true;
}

template <typename W, typename T>
[[nodiscard]] bool TestHeapPostBarrierConstruction() {
  T initialObj = CreateNurseryGCThing<T>(cx);
  CHECK(initialObj);
  CHECK(IsInsideNursery(initialObj));
  uintptr_t initialObjAsInt = UnbarrieredCastToInt(initialObj);

  {
    // We don't root our structure because that would end up tracing it on minor
    // GC and we're testing that heap post barrier works for things that aren't
    // roots.
    JS::AutoSuppressGCAnalysis noAnalysis(cx);

    auto* testStruct = js_new<TestStruct<W, T>>(initialObj);
    CHECK(testStruct);

    auto& wrapper = testStruct->wrapper;
    CHECK(wrapper == initialObj);

    cx->minorGC(JS::GCReason::API);

    CHECK(UnbarrieredCastToInt(wrapper.get()) != initialObjAsInt);
    CHECK(!IsInsideNursery(wrapper.get()));
    CHECK(TestCanAccessObject(wrapper.get()));

    // Disable the check that GCPtrs are only destroyed by the GC. What happens
    // on destruction isn't relevant to the test.
    gc::AutoSetThreadIsFinalizing threadIsFinalizing;

    js_delete(testStruct);
  }

  cx->minorGC(JS::GCReason::API);

  return true;
}

template <typename W, typename T>
[[nodiscard]] bool TestHeapPostBarrierUpdate() {
  // Normal case - allocate a heap object, write a nursery pointer into it and
  // check that it gets updated on minor GC.

  T initialObj = CreateNurseryGCThing<T>(cx);
  CHECK(initialObj);
  CHECK(IsInsideNursery(initialObj));
  uintptr_t initialObjAsInt = UnbarrieredCastToInt(initialObj);

  {
    // We don't root our structure because that would end up tracing it on minor
    // GC and we're testing that heap post barrier works for things that aren't
    // roots.
    JS::AutoSuppressGCAnalysis noAnalysis(cx);

    auto* testStruct = js_new<TestStruct<W, T>>();
    CHECK(testStruct);

    auto& wrapper = testStruct->wrapper;
    CHECK(!wrapper.get());

    wrapper = initialObj;
    CHECK(wrapper == initialObj);

    cx->minorGC(JS::GCReason::API);

    CHECK(UnbarrieredCastToInt(wrapper.get()) != initialObjAsInt);
    CHECK(!IsInsideNursery(wrapper.get()));
    CHECK(TestCanAccessObject(wrapper.get()));

    // Disable the check that GCPtrs are only destroyed by the GC. What happens
    // on destruction isn't relevant to the test.
    gc::AutoSetThreadIsFinalizing threadIsFinalizing;

    js_delete(testStruct);
  }

  cx->minorGC(JS::GCReason::API);

  return true;
}

template <typename W, typename T>
[[nodiscard]] bool TestHeapPostBarrierInitFailure() {
  // Failure case - allocate a heap object, write a nursery pointer into it
  // and fail to complete initialization.

  T initialObj = CreateNurseryGCThing<T>(cx);
  CHECK(initialObj);
  CHECK(IsInsideNursery(initialObj));

  {
    // We don't root our structure because that would end up tracing it on minor
    // GC and we're testing that heap post barrier works for things that aren't
    // roots.
    JS::AutoSuppressGCAnalysis noAnalysis(cx);

    auto testStruct = cx->make_unique<TestStruct<W, T>>(initialObj);
    CHECK(testStruct);

    auto& wrapper = testStruct->wrapper;
    CHECK(wrapper == initialObj);

    // testStruct deleted here, as if we left this block due to an error.
  }

  cx->minorGC(JS::GCReason::API);

  return true;
}

template <typename W, typename T>
[[nodiscard]] bool TestHeapPostBarrierMoveConstruction() {
  T initialObj = CreateNurseryGCThing<T>(cx);
  CHECK(initialObj);
  CHECK(IsInsideNursery(initialObj));
  uintptr_t initialObjAsInt = UnbarrieredCastToInt(initialObj);

  {
    // We don't root our structure because that would end up tracing it on minor
    // GC and we're testing that heap post barrier works for things that aren't
    // roots.
    JS::AutoSuppressGCAnalysis noAnalysis(cx);

    W wrapper1(initialObj);
    CHECK(wrapper1 == initialObj);

    W wrapper2(std::move(wrapper1));
    CHECK(wrapper2 == initialObj);

    cx->minorGC(JS::GCReason::API);

    CHECK(UnbarrieredCastToInt(wrapper1.get()) != initialObjAsInt);
    CHECK(UnbarrieredCastToInt(wrapper2.get()) != initialObjAsInt);
    CHECK(!IsInsideNursery(wrapper2.get()));
    CHECK(TestCanAccessObject(wrapper2.get()));
  }

  cx->minorGC(JS::GCReason::API);

  return true;
}

template <typename W, typename T>
[[nodiscard]] bool TestHeapPostBarrierMoveAssignment() {
  T initialObj = CreateNurseryGCThing<T>(cx);
  CHECK(initialObj);
  CHECK(IsInsideNursery(initialObj));
  uintptr_t initialObjAsInt = UnbarrieredCastToInt(initialObj);

  {
    // We don't root our structure because that would end up tracing it on minor
    // GC and we're testing that heap post barrier works for things that aren't
    // roots.
    JS::AutoSuppressGCAnalysis noAnalysis(cx);

    W wrapper1(initialObj);
    CHECK(wrapper1 == initialObj);

    W wrapper2;
    wrapper2 = std::move(wrapper1);
    CHECK(wrapper2 == initialObj);

    cx->minorGC(JS::GCReason::API);

    CHECK(UnbarrieredCastToInt(wrapper1.get()) != initialObjAsInt);
    CHECK(UnbarrieredCastToInt(wrapper2.get()) != initialObjAsInt);
    CHECK(!IsInsideNursery(wrapper2.get()));
    CHECK(TestCanAccessObject(wrapper2.get()));
  }

  cx->minorGC(JS::GCReason::API);

  return true;
}

END_TEST(testGCHeapPostBarriers)

// Test read barrier implementation on wrapper types. The following wrapper
// types have read barriers:
//  - JS::Heap
//  - JS::TenuredHeap
//  - WeakHeapPtr
//
// Also check that equality comparisons on wrappers do not trigger the read
// barrier.
BEGIN_TEST(testGCHeapReadBarriers) {
  AutoLeaveZeal nozeal(cx);

  CHECK((TestWrapperType<JS::Heap<JSObject*>, JSObject*>()));
  CHECK((TestWrapperType<JS::TenuredHeap<JSObject*>, JSObject*>()));
  CHECK((TestWrapperType<WeakHeapPtr<JSObject*>, JSObject*>()));
  CHECK((TestWrapperType<JS::Heap<JS::ArrayBuffer>, JS::ArrayBuffer>()));
  CHECK((TestWrapperType<JS::Heap<JS::Uint8Array>, JS::Uint8Array>()));

  return true;
}

template <typename WrapperT, typename ObjectT>
[[nodiscard]] bool TestWrapperType() {
  // Check that the read barrier normally marks gray things black.
  CHECK((TestReadBarrierUnmarksGray<WrapperT, ObjectT>()));

  // Check that the read barrier marks gray and white things black during an
  // incremental GC.
  CHECK((TestReadBarrierMarksBlack<WrapperT, ObjectT>(true)));
  CHECK((TestReadBarrierMarksBlack<WrapperT, ObjectT>(false)));

  // Allocate test objects and make them gray. We will make sure they stay
  // gray. (For most reads, the barrier will unmark gray.)
  Rooted<ObjectT> obj1(cx, CreateTenuredGCThing<ObjectT>(cx));
  Rooted<ObjectT> obj2(cx, CreateTenuredGCThing<ObjectT>(cx));
  MakeGray(obj1);
  MakeGray(obj2);

  WrapperT wrapper1(obj1);
  WrapperT wrapper2(obj2);
  const ObjectT constobj1 = obj1;
  const ObjectT constobj2 = obj2;
  CHECK((TestUnbarrieredOperations<WrapperT, ObjectT>(obj1, obj2, wrapper1,
                                                      wrapper2)));
  CHECK((TestUnbarrieredOperations<WrapperT, ObjectT>(constobj1, constobj2,
                                                      wrapper1, wrapper2)));

  return true;
}

template <typename WrapperT, typename ObjectT>
void Access(const WrapperT& wrapper) {
  if constexpr (std::is_base_of_v<JS::ArrayBufferOrView, ObjectT>) {
    (void)wrapper.asObject();
  } else {
    (void)*wrapper;
  }
}

template <typename WrapperT, typename ObjectT>
[[nodiscard]] bool TestReadBarrierUnmarksGray() {
  Rooted<ObjectT> obj(cx, CreateTenuredGCThing<ObjectT>(cx));
  WrapperT wrapper(obj);

  CHECK(GetColor(obj) == gc::CellColor::White);

  Access<WrapperT, ObjectT>(wrapper);

  CHECK(GetColor(obj) == gc::CellColor::White);

  MakeGray(obj);
  Access<WrapperT, ObjectT>(wrapper);

  CHECK(GetColor(obj) == gc::CellColor::Black);

  return true;
}

// Execute thunk |f| between two slices of an incremental GC controlled by zeal
// mode |mode|.
template <typename F>
[[nodiscard]] bool CallDuringIncrementalGC(uint32_t mode, F&& f) {
#ifndef JS_GC_ZEAL
  fprintf(stderr, "This test requires building with --enable-gczeal\n");
#else
  AutoGCParameter incremental(cx, JSGC_INCREMENTAL_GC_ENABLED, true);

  const int64_t BudgetMS = 10000;  // 10S should be long enough for anyone.

  JS::SetGCZeal(cx, mode, 0);
  JS::PrepareZoneForGC(cx, js::GetContextZone(cx));
  JS::SliceBudget budget{JS::TimeBudget(BudgetMS)};
  JS::StartIncrementalGC(cx, JS::GCOptions(), JS::GCReason::DEBUG_GC, budget);
  CHECK(JS::IsIncrementalGCInProgress(cx));

  CHECK(f());

  JS::FinishIncrementalGC(cx, JS::GCReason::DEBUG_GC);
#endif

  return true;
}

template <typename WrapperT, typename ObjectT>
[[nodiscard]] bool TestReadBarrierMarksBlack(bool fromWhite) {
  AutoLeaveZeal noZeal(cx);

  // Create an object and hide it from the hazard analysis.
  void* ptr = CreateHiddenTenuredGCThing<ObjectT>(cx);
  CHECK(ptr);

  CHECK(CallDuringIncrementalGC(9 /* YieldBeforeSweeping */, [&]() -> bool {
    CHECK(JS::IsIncrementalBarrierNeeded(cx));

    auto obj = RecoverHiddenGCThing<ObjectT>(ptr);

    WrapperT wrapper(obj);

    CHECK(GetColor(obj) == gc::CellColor::White);
    if (!fromWhite) {
      MakeGray(obj);
    }

    Access<WrapperT, ObjectT>(wrapper);

    CHECK(GetColor(obj) == gc::CellColor::Black);

    return true;
  }));

  return true;
}

template <typename WrapperT, typename ObjectT>
[[nodiscard]] bool TestUnbarrieredOperations(ObjectT obj, ObjectT obj2,
                                             WrapperT& wrapper,
                                             WrapperT& wrapper2) {
  (void)bool(wrapper);
  (void)bool(wrapper2);
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);

  int x = 0;

  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += obj == obj2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += obj == wrapper2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += wrapper == obj2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += wrapper == wrapper2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);

  CHECK(x == 0);

  x += obj != obj2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += obj != wrapper2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += wrapper != obj2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);
  x += wrapper != wrapper2;
  CHECK(GetColor(obj) == gc::CellColor::Gray);
  CHECK(GetColor(obj2) == gc::CellColor::Gray);

  CHECK(x == 4);

  return true;
}

END_TEST(testGCHeapReadBarriers)

using ObjectVector = Vector<JSObject*, 0, SystemAllocPolicy>;

// Test pre-barrier implementation on wrapper types. The following wrapper types
// have a pre-barrier:
//  - GCPtr
//  - HeapPtr
//  - PreBarriered
BEGIN_TEST(testGCHeapPreBarriers) {
  AutoLeaveZeal nozeal(cx);

  AutoGCParameter param1(cx, JSGC_INCREMENTAL_GC_ENABLED, true);

  // Create a bunch of objects. These are unrooted and will be used to test
  // whether barriers have fired by checking whether they have been marked
  // black.
  size_t objectCount = 100;  // Increase this if necessary when adding tests.
  ObjectVector testObjects;
  for (size_t i = 0; i < objectCount; i++) {
    JSObject* obj = CreateTenuredGCThing<JSObject*>(cx);
    CHECK(obj);
    CHECK(testObjects.append(obj));
  }

  // Start an incremental GC so we can detect if we cause barriers to fire, as
  // these will mark objects black.
  JS::PrepareForFullGC(cx);
  JS::SliceBudget budget(JS::WorkBudget(1));
  gc::GCRuntime* gc = &cx->runtime()->gc;
  gc->startDebugGC(JS::GCOptions::Normal, budget);
  while (gc->state() != gc::State::Mark) {
    gc->debugGCSlice(budget);
  }
  MOZ_ASSERT(cx->zone()->needsMarkingBarrier());

  CHECK(TestWrapper<HeapPtr<JSObject*>>(testObjects));
  CHECK(TestWrapper<PreBarriered<JSObject*>>(testObjects));

  // GCPtr is different because 1) it doesn't support move operations as it's
  // supposed to be part of a GC thing and 2) it doesn't perform a pre-barrier
  // in its destructor because these are only destroyed as part of a GC where
  // the barrier is unnecessary.
  CHECK(TestGCPtr(testObjects));

  gc::FinishGC(cx, JS::GCReason::API);

  return true;
}

template <typename Wrapper>
[[nodiscard]] bool TestWrapper(ObjectVector& testObjects) {
  CHECK(TestCopyConstruction<Wrapper>(testObjects.popCopy()));
  CHECK(TestMoveConstruction<Wrapper>(testObjects.popCopy()));
  CHECK(TestAssignment<Wrapper>(testObjects.popCopy(), testObjects.popCopy()));
  CHECK(TestMoveAssignment<Wrapper>(testObjects.popCopy(),
                                    testObjects.popCopy()));
  return true;
}

template <typename Wrapper>
[[nodiscard]] bool TestCopyConstruction(JSObject* obj) {
  CHECK(GetColor(obj) == gc::CellColor::White);

  {
    Wrapper wrapper1(obj);
    Wrapper wrapper2(wrapper1);
    CHECK(wrapper1 == obj);
    CHECK(wrapper2 == obj);
    CHECK(GetColor(obj) == gc::CellColor::White);
  }

  // Check destructor performs pre-barrier.
  CHECK(GetColor(obj) == gc::CellColor::Black);

  return true;
}

template <typename Wrapper>
[[nodiscard]] bool TestMoveConstruction(JSObject* obj) {
  CHECK(GetColor(obj) == gc::CellColor::White);

  {
    Wrapper wrapper1(obj);
    MakeGray(obj);  // Check that we allow move of gray GC thing.
    Wrapper wrapper2(std::move(wrapper1));
    CHECK(!wrapper1);
    CHECK(wrapper2 == obj);
    CHECK(GetColor(obj) == gc::CellColor::Gray);
  }

  // Check destructor performs pre-barrier.
  CHECK(GetColor(obj) == gc::CellColor::Black);

  return true;
}

template <typename Wrapper>
[[nodiscard]] bool TestAssignment(JSObject* obj1, JSObject* obj2) {
  CHECK(GetColor(obj1) == gc::CellColor::White);
  CHECK(GetColor(obj2) == gc::CellColor::White);

  {
    Wrapper wrapper1(obj1);
    Wrapper wrapper2(obj2);

    wrapper2 = wrapper1;

    CHECK(wrapper1 == obj1);
    CHECK(wrapper2 == obj1);
    CHECK(GetColor(obj1) == gc::CellColor::White);  // No barrier fired.
    CHECK(GetColor(obj2) == gc::CellColor::Black);  // Pre barrier fired.
  }

  // Check destructor performs pre-barrier.
  CHECK(GetColor(obj1) == gc::CellColor::Black);

  return true;
}

template <typename Wrapper>
[[nodiscard]] bool TestMoveAssignment(JSObject* obj1, JSObject* obj2) {
  CHECK(GetColor(obj1) == gc::CellColor::White);
  CHECK(GetColor(obj2) == gc::CellColor::White);

  {
    Wrapper wrapper1(obj1);
    Wrapper wrapper2(obj2);

    MakeGray(obj1);  // Check we allow move of gray thing.
    wrapper2 = std::move(wrapper1);

    CHECK(!wrapper1);
    CHECK(wrapper2 == obj1);
    CHECK(GetColor(obj1) == gc::CellColor::Gray);   // No barrier fired.
    CHECK(GetColor(obj2) == gc::CellColor::Black);  // Pre barrier fired.
  }

  // Check destructor performs pre-barrier.
  CHECK(GetColor(obj1) == gc::CellColor::Black);

  return true;
}

[[nodiscard]] bool TestGCPtr(ObjectVector& testObjects) {
  CHECK(TestGCPtrCopyConstruction(testObjects.popCopy()));
  CHECK(TestGCPtrAssignment(testObjects.popCopy(), testObjects.popCopy()));
  return true;
}

[[nodiscard]] bool TestGCPtrCopyConstruction(JSObject* obj) {
  CHECK(GetColor(obj) == gc::CellColor::White);

  {
    // Let us destroy GCPtrs ourselves for testing purposes.
    gc::AutoSetThreadIsFinalizing threadIsFinalizing;

    GCPtr<JSObject*> wrapper1(obj);
    GCPtr<JSObject*> wrapper2(wrapper1);
    CHECK(wrapper1 == obj);
    CHECK(wrapper2 == obj);
    CHECK(GetColor(obj) == gc::CellColor::White);
  }

  // GCPtr doesn't perform pre-barrier in destructor.
  CHECK(GetColor(obj) == gc::CellColor::White);

  return true;
}

[[nodiscard]] bool TestGCPtrAssignment(JSObject* obj1, JSObject* obj2) {
  CHECK(GetColor(obj1) == gc::CellColor::White);
  CHECK(GetColor(obj2) == gc::CellColor::White);

  {
    // Let us destroy GCPtrs ourselves for testing purposes.
    gc::AutoSetThreadIsFinalizing threadIsFinalizing;

    GCPtr<JSObject*> wrapper1(obj1);
    GCPtr<JSObject*> wrapper2(obj2);

    wrapper2 = wrapper1;

    CHECK(wrapper1 == obj1);
    CHECK(wrapper2 == obj1);
    CHECK(GetColor(obj1) == gc::CellColor::White);  // No barrier fired.
    CHECK(GetColor(obj2) == gc::CellColor::Black);  // Pre barrier fired.
  }

  // GCPtr doesn't perform pre-barrier in destructor.
  CHECK(GetColor(obj1) == gc::CellColor::White);

  return true;
}

END_TEST(testGCHeapPreBarriers)
