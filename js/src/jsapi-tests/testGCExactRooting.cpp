/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"

#include "ds/TraceableFifo.h"
#include "gc/Policy.h"
#include "js/GCHashTable.h"
#include "js/GCVector.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_GetProperty, JS_SetProperty
#include "js/RootingAPI.h"

#include "jsapi-tests/tests.h"

using namespace js;

using mozilla::Maybe;
using mozilla::Some;

BEGIN_TEST(testGCExactRooting) {
  JS::RootedObject rootCx(cx, JS_NewPlainObject(cx));

  JS_GC(cx);

  /* Use the objects we just created to ensure that they are still alive. */
  JS_DefineProperty(cx, rootCx, "foo", JS::UndefinedHandleValue, 0);

  return true;
}
END_TEST(testGCExactRooting)

BEGIN_TEST(testGCSuppressions) {
  JS::AutoAssertNoGC nogc;
  JS::AutoCheckCannotGC checkgc;
  JS::AutoSuppressGCAnalysis noanalysis;

  JS::AutoAssertNoGC nogcCx(cx);
  JS::AutoCheckCannotGC checkgcCx(cx);
  JS::AutoSuppressGCAnalysis noanalysisCx(cx);

  return true;
}
END_TEST(testGCSuppressions)

struct MyContainer {
  int whichConstructor;
  HeapPtr<JSObject*> obj;
  HeapPtr<JSString*> str;

  MyContainer() : whichConstructor(1), obj(nullptr), str(nullptr) {}
  explicit MyContainer(double) : MyContainer() { whichConstructor = 2; }
  explicit MyContainer(JSContext* cx) : MyContainer() { whichConstructor = 3; }
  MyContainer(JSContext* cx, JSContext* cx2, JSContext* cx3) : MyContainer() {
    whichConstructor = 4;
  }
  MyContainer(const MyContainer& rhs)
      : whichConstructor(100 + rhs.whichConstructor),
        obj(rhs.obj),
        str(rhs.str) {}
  void trace(JSTracer* trc) {
    js::TraceEdge(trc, &obj, "test container obj");
    js::TraceEdge(trc, &str, "test container str");
  }
};

struct MyNonCopyableContainer {
  int whichConstructor;
  HeapPtr<JSObject*> obj;
  HeapPtr<JSString*> str;

  MyNonCopyableContainer() : whichConstructor(1), obj(nullptr), str(nullptr) {}
  explicit MyNonCopyableContainer(double) : MyNonCopyableContainer() {
    whichConstructor = 2;
  }
  explicit MyNonCopyableContainer(JSContext* cx) : MyNonCopyableContainer() {
    whichConstructor = 3;
  }
  explicit MyNonCopyableContainer(JSContext* cx, JSContext* cx2, JSContext* cx3)
      : MyNonCopyableContainer() {
    whichConstructor = 4;
  }

  MyNonCopyableContainer(const MyNonCopyableContainer&) = delete;
  MyNonCopyableContainer& operator=(const MyNonCopyableContainer&) = delete;

  void trace(JSTracer* trc) {
    js::TraceEdge(trc, &obj, "test container obj");
    js::TraceEdge(trc, &str, "test container str");
  }
};

namespace js {
template <typename Wrapper>
struct MutableWrappedPtrOperations<MyContainer, Wrapper> {
  HeapPtr<JSObject*>& obj() { return static_cast<Wrapper*>(this)->get().obj; }
  HeapPtr<JSString*>& str() { return static_cast<Wrapper*>(this)->get().str; }
  int constructor() {
    return static_cast<Wrapper*>(this)->get().whichConstructor;
  }
};

template <typename Wrapper>
struct MutableWrappedPtrOperations<MyNonCopyableContainer, Wrapper> {
  HeapPtr<JSObject*>& obj() { return static_cast<Wrapper*>(this)->get().obj; }
  HeapPtr<JSString*>& str() { return static_cast<Wrapper*>(this)->get().str; }
  int constructor() {
    return static_cast<Wrapper*>(this)->get().whichConstructor;
  }
};
}  // namespace js

BEGIN_TEST(testGCRootedStaticStructInternalStackStorageAugmented) {
  // Test Rooted constructors for a copyable type.
  JS::Rooted<MyContainer> r1(cx);
  JS::Rooted<MyContainer> r2(cx, 3.4);
  JS::Rooted<MyContainer> r3(cx, MyContainer(cx));
  JS::Rooted<MyContainer> r4(cx, cx);
  JS::Rooted<MyContainer> r5(cx, cx, cx, cx);

  JS::Rooted<Value> rv(cx);

  CHECK_EQUAL(r1.constructor(), 1);    // direct SafelyInitialized<T>
  CHECK_EQUAL(r2.constructor(), 2);    // direct MyContainer(3.4)
  CHECK_EQUAL(r3.constructor(), 103);  // copy of MyContainer(cx)
  CHECK_EQUAL(r4.constructor(), 3);    // direct MyContainer(cx)
  CHECK_EQUAL(r5.constructor(), 4);    // direct MyContainer(cx, cx, cx)

  // Test Rooted constructor forwarding for a non-copyable type.
  JS::Rooted<MyNonCopyableContainer> nc1(cx);
  JS::Rooted<MyNonCopyableContainer> nc2(cx, 3.4);
  // Compile error: cannot copy
  // JS::Rooted<MyNonCopyableContainer> nc3(cx, MyNonCopyableContainer(cx));
  JS::Rooted<MyNonCopyableContainer> nc4(cx, cx);
  JS::Rooted<MyNonCopyableContainer> nc5(cx, cx, cx, cx);

  CHECK_EQUAL(nc1.constructor(), 1);  // direct MyNonCopyableContainer()
  CHECK_EQUAL(nc2.constructor(), 2);  // direct MyNonCopyableContainer(3.4)
  CHECK_EQUAL(nc4.constructor(), 3);  // direct MyNonCopyableContainer(cx)
  CHECK_EQUAL(nc5.constructor(),
              4);  // direct MyNonCopyableContainer(cx, cx, cx)

  JS::Rooted<MyContainer> container(cx);
  container.obj() = JS_NewObject(cx, nullptr);
  container.str() = JS_NewStringCopyZ(cx, "Hello");

  JS_GC(cx);
  JS_GC(cx);

  JS::RootedObject obj(cx, container.obj());
  JS::RootedValue val(cx, StringValue(container.str()));
  CHECK(JS_SetProperty(cx, obj, "foo", val));
  obj = nullptr;
  val = UndefinedValue();

  {
    JS::RootedString actual(cx);
    bool same;

    // Automatic move from stack to heap.
    JS::PersistentRooted<MyContainer> heap(cx, container);

    // Copyable types in place.
    JS::PersistentRooted<MyContainer> cp1(cx);
    JS::PersistentRooted<MyContainer> cp2(cx, 7.8);
    JS::PersistentRooted<MyContainer> cp3(cx, cx);
    JS::PersistentRooted<MyContainer> cp4(cx, cx, cx, cx);

    CHECK_EQUAL(cp1.constructor(), 1);  // direct SafelyInitialized<T>
    CHECK_EQUAL(cp2.constructor(), 2);  // direct MyContainer(double)
    CHECK_EQUAL(cp3.constructor(), 3);  // direct MyContainer(cx)
    CHECK_EQUAL(cp4.constructor(), 4);  // direct MyContainer(cx, cx, cx)

    // Construct uncopyable type in place.
    JS::PersistentRooted<MyNonCopyableContainer> ncp1(cx);
    JS::PersistentRooted<MyNonCopyableContainer> ncp2(cx, 7.8);

    // We're not just using a 1-arg constructor, right?
    JS::PersistentRooted<MyNonCopyableContainer> ncp3(cx, cx);
    JS::PersistentRooted<MyNonCopyableContainer> ncp4(cx, cx, cx, cx);

    CHECK_EQUAL(ncp1.constructor(), 1);  // direct SafelyInitialized<T>
    CHECK_EQUAL(ncp2.constructor(), 2);  // direct Ctor(double)
    CHECK_EQUAL(ncp3.constructor(), 3);  // direct Ctor(cx)
    CHECK_EQUAL(ncp4.constructor(), 4);  // direct Ctor(cx, cx, cx)

    // clear prior rooting.
    container.obj() = nullptr;
    container.str() = nullptr;

    obj = heap.obj();
    CHECK(JS_GetProperty(cx, obj, "foo", &val));
    actual = val.toString();
    CHECK(JS_StringEqualsLiteral(cx, actual, "Hello", &same));
    CHECK(same);
    obj = nullptr;
    actual = nullptr;

    JS_GC(cx);
    JS_GC(cx);

    obj = heap.obj();
    CHECK(JS_GetProperty(cx, obj, "foo", &val));
    actual = val.toString();
    CHECK(JS_StringEqualsLiteral(cx, actual, "Hello", &same));
    CHECK(same);
    obj = nullptr;
    actual = nullptr;
  }

  return true;
}
END_TEST(testGCRootedStaticStructInternalStackStorageAugmented)

constinit static JS::PersistentRooted<JSObject*> sLongLived;
BEGIN_TEST(testGCPersistentRootedOutlivesRuntime) {
  sLongLived.init(cx, JS_NewObject(cx, nullptr));
  CHECK(sLongLived);
  return true;
}
END_TEST(testGCPersistentRootedOutlivesRuntime)

// Unlike the above, the following test is an example of an invalid usage: for
// performance and simplicity reasons, PersistentRooted<Traceable> is not
// allowed to outlive the container it belongs to. The following commented out
// test can be used to verify that the relevant assertion fires as expected.
static JS::PersistentRooted<MyContainer> sContainer;
BEGIN_TEST(testGCPersistentRootedTraceableCannotOutliveRuntime) {
  JS::Rooted<MyContainer> container(cx);
  container.obj() = JS_NewObject(cx, nullptr);
  container.str() = JS_NewStringCopyZ(cx, "Hello");
  sContainer.init(cx, container);

  // Commenting the following line will trigger an assertion that the
  // PersistentRooted outlives the runtime it is attached to.
  sContainer.reset();

  return true;
}
END_TEST(testGCPersistentRootedTraceableCannotOutliveRuntime)

using MyHashMap = js::GCHashMap<js::Shape*, JSObject*>;

BEGIN_TEST(testGCRootedHashMap) {
  JS::Rooted<MyHashMap> map(cx, MyHashMap(cx, 15));

  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    CHECK(JS_SetProperty(cx, obj, buffer, val));
    CHECK(map.putNew(obj->shape(), obj));
  }

  JS_GC(cx);
  JS_GC(cx);

  for (auto iter = map.iter(); !iter.done(); iter.next()) {
    RootedObject obj(cx, iter.get().value());
    CHECK(obj->shape() == iter.get().key());
  }

  return true;
}
END_TEST(testGCRootedHashMap)

// Repeat of the test above, but without rooting. This is a rooting hazard. The
// JS_EXPECT_HAZARDS annotation will cause the hazard taskcluster job to fail
// if the hazard below is *not* detected.
BEGIN_TEST_WITH_ATTRIBUTES(testUnrootedGCHashMap, JS_EXPECT_HAZARDS) {
  AutoLeaveZeal noZeal(cx);

  MyHashMap map(cx, 15);

  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    CHECK(JS_SetProperty(cx, obj, buffer, val));
    CHECK(map.putNew(obj->shape(), obj));
  }

  JS_GC(cx);

  // Access map to keep it live across the GC.
  CHECK(map.count() == 10);

  return true;
}
END_TEST(testUnrootedGCHashMap)

BEGIN_TEST(testSafelyUnrootedGCHashMap) {
  // This is not rooted, but it doesn't use GC pointers as keys or values so
  // it's ok.
  js::GCHashMap<uint64_t, uint64_t> map(cx, 15);

  JS_GC(cx);
  CHECK(map.putNew(12, 13));

  return true;
}
END_TEST(testSafelyUnrootedGCHashMap)

static bool FillMyHashMap(JSContext* cx, MutableHandle<MyHashMap> map) {
  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    if (!JS_SetProperty(cx, obj, buffer, val)) {
      return false;
    }
    if (!map.putNew(obj->shape(), obj)) {
      return false;
    }
  }
  return true;
}

static bool CheckMyHashMap(JSContext* cx, Handle<MyHashMap> map) {
  for (auto iter = map.iter(); !iter.done(); iter.next()) {
    RootedObject obj(cx, iter.get().value());
    if (obj->shape() != iter.get().key()) {
      return false;
    }
  }
  return true;
}

BEGIN_TEST(testGCHandleHashMap) {
  JS::Rooted<MyHashMap> map(cx, MyHashMap(cx, 15));

  CHECK(FillMyHashMap(cx, &map));

  JS_GC(cx);
  JS_GC(cx);

  CHECK(CheckMyHashMap(cx, map));

  return true;
}
END_TEST(testGCHandleHashMap)

using ShapeVec = GCVector<NativeShape*>;

BEGIN_TEST(testGCRootedVector) {
  JS::Rooted<ShapeVec> shapes(cx, cx);

  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    CHECK(JS_SetProperty(cx, obj, buffer, val));
    CHECK(shapes.append(obj->as<NativeObject>().shape()));
  }

  JS_GC(cx);
  JS_GC(cx);

  for (size_t i = 0; i < 10; ++i) {
    // Check the shape to ensure it did not get collected.
    char letter = 'a' + i;
    bool match;
    ShapePropertyIter<NoGC> iter(shapes[i]);
    CHECK(JS_StringEqualsAscii(cx, iter->key().toString(), &letter, 1, &match));
    CHECK(match);
  }

  // Ensure iterator enumeration works through the rooted.
  for (auto shape : shapes) {
    CHECK(shape);
  }

  CHECK(receiveConstRefToShapeVector(shapes));

  // Ensure rooted converts to handles.
  CHECK(receiveHandleToShapeVector(shapes));
  CHECK(receiveMutableHandleToShapeVector(&shapes));

  return true;
}

bool receiveConstRefToShapeVector(
    const JS::Rooted<GCVector<NativeShape*>>& rooted) {
  // Ensure range enumeration works through the reference.
  for (auto shape : rooted) {
    CHECK(shape);
  }
  return true;
}

bool receiveHandleToShapeVector(JS::Handle<GCVector<NativeShape*>> handle) {
  // Ensure range enumeration works through the handle.
  for (auto shape : handle) {
    CHECK(shape);
  }
  return true;
}

bool receiveMutableHandleToShapeVector(
    JS::MutableHandle<GCVector<NativeShape*>> handle) {
  // Ensure range enumeration works through the handle.
  for (auto shape : handle) {
    CHECK(shape);
  }
  return true;
}
END_TEST(testGCRootedVector)

BEGIN_TEST(testTraceableFifo) {
  using ShapeFifo = TraceableFifo<NativeShape*>;
  JS::Rooted<ShapeFifo> shapes(cx, ShapeFifo(cx));
  CHECK(shapes.empty());

  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    CHECK(JS_SetProperty(cx, obj, buffer, val));
    CHECK(shapes.pushBack(obj->as<NativeObject>().shape()));
  }

  CHECK(shapes.length() == 10);

  JS_GC(cx);
  JS_GC(cx);

  for (size_t i = 0; i < 10; ++i) {
    // Check the shape to ensure it did not get collected.
    char letter = 'a' + i;
    bool match;
    ShapePropertyIter<NoGC> iter(shapes.front());
    CHECK(JS_StringEqualsAscii(cx, iter->key().toString(), &letter, 1, &match));
    CHECK(match);
    shapes.popFront();
  }

  CHECK(shapes.empty());
  return true;
}
END_TEST(testTraceableFifo)

using ShapeVec = GCVector<NativeShape*>;

static bool FillVector(JSContext* cx, MutableHandle<ShapeVec> shapes) {
  for (size_t i = 0; i < 10; ++i) {
    RootedObject obj(cx, JS_NewObject(cx, nullptr));
    RootedValue val(cx, UndefinedValue());
    // Construct a unique property name to ensure that the object creates a
    // new shape.
    char buffer[2];
    buffer[0] = 'a' + i;
    buffer[1] = '\0';
    if (!JS_SetProperty(cx, obj, buffer, val)) {
      return false;
    }
    if (!shapes.append(obj->as<NativeObject>().shape())) {
      return false;
    }
  }

  // Ensure iterator enumeration works through the mutable handle.
  for (auto shape : shapes) {
    if (!shape) {
      return false;
    }
  }

  return true;
}

static bool CheckVector(JSContext* cx, Handle<ShapeVec> shapes) {
  for (size_t i = 0; i < 10; ++i) {
    // Check the shape to ensure it did not get collected.
    char letter = 'a' + i;
    bool match;
    ShapePropertyIter<NoGC> iter(shapes[i]);
    if (!JS_StringEqualsAscii(cx, iter->key().toString(), &letter, 1, &match)) {
      return false;
    }
    if (!match) {
      return false;
    }
  }

  // Ensure iterator enumeration works through the handle.
  for (auto shape : shapes) {
    if (!shape) {
      return false;
    }
  }

  return true;
}

BEGIN_TEST(testGCHandleVector) {
  JS::Rooted<ShapeVec> vec(cx, ShapeVec(cx));

  CHECK(FillVector(cx, &vec));

  JS_GC(cx);
  JS_GC(cx);

  CHECK(CheckVector(cx, vec));

  return true;
}
END_TEST(testGCHandleVector)

class Foo {
 public:
  Foo(int, int) {}
  void trace(JSTracer*) {}
};

using FooVector = JS::GCVector<Foo>;

BEGIN_TEST(testGCVectorEmplaceBack) {
  JS::Rooted<FooVector> vector(cx, FooVector(cx));

  CHECK(vector.emplaceBack(1, 2));

  return true;
}
END_TEST(testGCVectorEmplaceBack)

BEGIN_TEST(testRootedMaybeValue) {
  JS::Rooted<Maybe<Value>> maybeNothing(cx);
  CHECK(maybeNothing.isNothing());
  CHECK(!maybeNothing.isSome());

  JS::Rooted<Maybe<Value>> maybe(cx, Some(UndefinedValue()));
  CHECK(CheckConstOperations<Rooted<Maybe<Value>>&>(maybe));
  CHECK(CheckConstOperations<Handle<Maybe<Value>>>(maybe));
  CHECK(CheckConstOperations<MutableHandle<Maybe<Value>>>(&maybe));

  maybe = Some(JS::TrueValue());
  CHECK(CheckMutableOperations<Rooted<Maybe<Value>>&>(maybe));

  maybe = Some(JS::TrueValue());
  CHECK(CheckMutableOperations<MutableHandle<Maybe<Value>>>(&maybe));

  CHECK(JS::NothingHandleValue.isNothing());

  return true;
}

template <typename T>
bool CheckConstOperations(T someUndefinedValue) {
  CHECK(someUndefinedValue.isSome());
  CHECK(someUndefinedValue.value().isUndefined());
  CHECK(someUndefinedValue->isUndefined());
  CHECK((*someUndefinedValue).isUndefined());
  return true;
}

template <typename T>
bool CheckMutableOperations(T maybe) {
  CHECK(maybe->isTrue());

  *maybe = JS::FalseValue();
  CHECK(maybe->isFalse());

  maybe.reset();
  CHECK(maybe.isNothing());

  return true;
}

END_TEST(testRootedMaybeValue)

// Maybe<T> should inherit the attributes of T, assuming it gets emplaced.
BEGIN_TEST(testMaybeHandling_inherit) {
  JSObject* dangerous = JS_NewObject(cx, nullptr);
  CHECK(dangerous);
  mozilla::Maybe<js::gc::AutoSuppressGC> suppress;
  suppress.emplace(cx);
  JS_GC(cx);  // safe because GC is suppressed.
  return !!dangerous;
}
END_TEST(testMaybeHandling_inherit)

#if BUG_2006236_IMPLEMENTED
// Maybe<T> should have no effect if it is not emplaced.
BEGIN_TEST_WITH_ATTRIBUTES(testMaybeHandling_nothing, JS_EXPECT_HAZARDS) {
  JSObject* dangerous = JS_NewObject(cx, nullptr);
  CHECK(dangerous);
  mozilla::Maybe<js::gc::AutoSuppressGC> suppress;
  JS_GC(cx);  // bad! GC unsuppressed!
  return !!dangerous;
}
END_TEST(testMaybeHandling_nothing)
#endif

BEGIN_TEST(testMaybeHandling_uninit) {
  Rooted<JSObject*> dangerous(cx, JS_NewObject(cx, nullptr));
  CHECK(dangerous);
  mozilla::Maybe<JSObject*> safe;
  JS_GC(cx);
  safe.emplace(dangerous);  // After GC, so this doesn't matter.
  return safe.isSome();
}
END_TEST(testMaybeHandling_uninit)

BEGIN_TEST_WITH_ATTRIBUTES(testMaybeHandling_init, JS_EXPECT_HAZARDS) {
  Rooted<JSObject*> dangerous(cx, JS_NewObject(cx, nullptr));
  CHECK(dangerous);
  mozilla::Maybe<JSObject*> safe(std::in_place, dangerous);
  JS_GC(cx);
  return safe.isSome();
}
END_TEST(testMaybeHandling_init)

BEGIN_TEST_WITH_ATTRIBUTES(testMaybeHandling_emplace, JS_EXPECT_HAZARDS) {
  Rooted<JSObject*> dangerous(cx, JS_NewObject(cx, nullptr));
  CHECK(dangerous);
  mozilla::Maybe<JSObject*> safe;
  safe.emplace(dangerous);
  JS_GC(cx);
  return safe.isSome();
}
END_TEST(testMaybeHandling_emplace)

BEGIN_TEST(testMaybeHandling_reset) {
  Rooted<JSObject*> dangerous(cx, JS_NewObject(cx, nullptr));
  CHECK(dangerous);
  mozilla::Maybe<JSObject*> safe = Some((JSObject*)dangerous);
  JS_GC(cx);
  safe.reset();  // safe is dead here and therefore also across the GC
  return safe.isNothing();
}
END_TEST(testMaybeHandling_reset)

struct TestErr {};
struct OtherTestErr {};

struct SimpleTraceable {
  // I'm using plain objects rather than Heap<T> because Heap<T> would get
  // traced via the store buffer. Heap<T> would be a more realistic example,
  // but would require compaction to test for tracing.
  JSObject* obj;
  JS::Value val;

  void trace(JSTracer* trc) {
    TraceRoot(trc, &obj, "obj");
    TraceRoot(trc, &val, "val");
  }
};

namespace JS {
template <>
struct GCPolicy<TestErr> : public IgnoreGCPolicy<TestErr> {};
}  // namespace JS

BEGIN_TEST_WITH_ATTRIBUTES(testGCRootedResult, JS_EXPECT_HAZARDS) {
  AutoLeaveZeal noZeal(cx);

  JSObject* unrootedObj = JS_NewPlainObject(cx);
  CHECK(js::gc::IsInsideNursery(unrootedObj));
  Value unrootedVal = ObjectValue(*unrootedObj);

  RootedObject obj(cx, unrootedObj);
  RootedValue val(cx, unrootedVal);

  Result<Value, TestErr> unrootedValerr(val);
  Rooted<Result<Value, TestErr>> valerr(cx, val);

  Result<mozilla::Ok, Value> unrootedOkval(val);
  Rooted<Result<mozilla::Ok, Value>> okval(cx, val);

  Result<mozilla::Ok, TestErr> simple{mozilla::Ok()};

  Result<Value, JSObject*> unrootedValobj1(val);
  Rooted<Result<Value, JSObject*>> valobj1(cx, val);
  Result<Value, JSObject*> unrootedValobj2(obj);
  Rooted<Result<Value, JSObject*>> valobj2(cx, obj);

  // Test nested traceable structures.
  Result<mozilla::Maybe<mozilla::Ok>, JSObject*> maybeobj(
      mozilla::Some(mozilla::Ok()));
  Rooted<Result<mozilla::Maybe<mozilla::Ok>, JSObject*>> rooted_maybeobj(
      cx, mozilla::Some(mozilla::Ok()));

  // This would fail to compile because Result<> deletes its copy constructor,
  // which prevents updating after tracing:
  //
  // Rooted<Result<Result<mozilla::Ok, JS::Value>, JSObject*>>

  // But this should be fine when no tracing is required.
  Result<Result<mozilla::Ok, int>, double> dummy(3.4);

  // One thing I didn't realize initially about Result<>: unwrap() takes
  // ownership of a value. In the case of Result<Maybe>, that means the
  // contained Maybe is reset to Nothing.
  Result<mozilla::Maybe<int>, int> confusing(mozilla::Some(7));
  CHECK(confusing.unwrap().isSome());
  CHECK(!confusing.unwrap().isSome());

  Result<mozilla::Maybe<JS::Value>, JSObject*> maybevalobj(
      mozilla::Some(val.get()));
  Rooted<Result<mozilla::Maybe<JS::Value>, JSObject*>> rooted_maybevalobj(
      cx, mozilla::Some(val.get()));

  // Custom types that haven't had GCPolicy explicitly specialized.
  SimpleTraceable s1{obj, val};
  Result<SimpleTraceable, TestErr> custom(s1);
  SimpleTraceable s2{obj, val};
  Rooted<Result<SimpleTraceable, TestErr>> rootedCustom(cx, s2);

  CHECK(obj == unrootedObj);
  CHECK(val == unrootedVal);
  CHECK(simple.isOk());
  CHECK(unrootedValerr.inspect() == unrootedVal);
  CHECK(valerr.get().inspect() == val);
  CHECK(unrootedOkval.inspectErr() == unrootedVal);
  CHECK(okval.get().inspectErr() == val);
  CHECK(unrootedValobj1.inspect() == unrootedVal);
  CHECK(valobj1.get().inspect() == val);
  CHECK(unrootedValobj2.inspectErr() == unrootedObj);
  CHECK(valobj2.get().inspectErr() == obj);
  CHECK(*maybevalobj.inspect() == unrootedVal);
  CHECK(*rooted_maybevalobj.get().inspect() == val);
  CHECK(custom.inspect().obj == unrootedObj);
  CHECK(custom.inspect().val == unrootedVal);
  CHECK(rootedCustom.get().inspect().obj == obj);
  CHECK(rootedCustom.get().inspect().val == val);

  JS_GC(cx);

  CHECK(obj != unrootedObj);
  CHECK(val != unrootedVal);
  CHECK(unrootedValerr.inspect() == unrootedVal);
  CHECK(valerr.get().inspect() == val);
  CHECK(unrootedOkval.inspectErr() == unrootedVal);
  CHECK(okval.get().inspectErr() == val);
  CHECK(unrootedValobj1.inspect() == unrootedVal);
  CHECK(valobj1.get().inspect() == val);
  CHECK(unrootedValobj2.inspectErr() == unrootedObj);
  CHECK(valobj2.get().inspectErr() == obj);
  CHECK(*maybevalobj.inspect() == unrootedVal);
  CHECK(*rooted_maybevalobj.get().inspect() == val);
  MOZ_ASSERT(custom.inspect().obj == unrootedObj);
  CHECK(custom.inspect().obj == unrootedObj);
  CHECK(custom.inspect().val == unrootedVal);
  CHECK(rootedCustom.get().inspect().obj == obj);
  CHECK(rootedCustom.get().inspect().val == val);

  mozilla::Result<OtherTestErr, mozilla::Ok> r(mozilla::Ok{});
  (void)r;

  return true;
}
END_TEST(testGCRootedResult)

static int copies = 0;

struct DontCopyMe_Variant {
  JSObject* obj;
  explicit DontCopyMe_Variant(JSObject* objArg) : obj(objArg) {}
  DontCopyMe_Variant(const DontCopyMe_Variant& other) : obj(other.obj) {
    copies++;
  }
  DontCopyMe_Variant(DontCopyMe_Variant&& other) : obj(std::move(other.obj)) {
    other.obj = nullptr;
  }
  void trace(JSTracer* trc) { TraceRoot(trc, &obj, "obj"); }
};

enum struct TestUnusedZeroEnum : int16_t { Ok = 0, NotOk = 1 };

namespace mozilla::detail {
template <>
struct UnusedZero<TestUnusedZeroEnum> : UnusedZeroEnum<TestUnusedZeroEnum> {};
}  // namespace mozilla::detail

namespace JS {
template <>
struct GCPolicy<TestUnusedZeroEnum>
    : public IgnoreGCPolicy<TestUnusedZeroEnum> {};
}  // namespace JS

struct DontCopyMe_NullIsOk {
  JS::Value val;
  DontCopyMe_NullIsOk() : val(UndefinedValue()) {}
  explicit DontCopyMe_NullIsOk(const JS::Value& valArg) : val(valArg) {}
  DontCopyMe_NullIsOk(const DontCopyMe_NullIsOk& other) = delete;
  DontCopyMe_NullIsOk(DontCopyMe_NullIsOk&& other)
      : val(std::move(other.val)) {}
  DontCopyMe_NullIsOk& operator=(DontCopyMe_NullIsOk&& other) {
    val = std::move(other.val);
    other.val = UndefinedValue();
    return *this;
  }
  void trace(JSTracer* trc) { TraceRoot(trc, &val, "val"); }
};

struct Failed {};

namespace mozilla::detail {
template <>
struct UnusedZero<Failed> {
  using StorageType = uintptr_t;

  static constexpr bool value = true;
  static constexpr StorageType nullValue = 0;
  static constexpr StorageType GetDefaultValue() { return 2; }

  static constexpr void AssertValid(StorageType aValue) {}
  static constexpr Failed Inspect(const StorageType& aValue) {
    return Failed{};
  }
  static constexpr Failed Unwrap(StorageType aValue) { return Failed{}; }
  static constexpr StorageType Store(Failed aValue) {
    return GetDefaultValue();
  }
};
}  // namespace mozilla::detail

namespace JS {
template <>
struct GCPolicy<Failed> : public IgnoreGCPolicy<Failed> {};
}  // namespace JS

struct TriviallyCopyable_LowBitTagIsError {
  JSObject* obj;
  TriviallyCopyable_LowBitTagIsError() : obj(nullptr) {}
  explicit TriviallyCopyable_LowBitTagIsError(JSObject* objArg) : obj(objArg) {}
  TriviallyCopyable_LowBitTagIsError(
      const TriviallyCopyable_LowBitTagIsError& other) = default;
  void trace(JSTracer* trc) { TraceRoot(trc, &obj, "obj"); }
};

namespace mozilla::detail {
template <>
struct HasFreeLSB<TriviallyCopyable_LowBitTagIsError> : HasFreeLSB<JSObject*> {
};
}  // namespace mozilla::detail

BEGIN_TEST_WITH_ATTRIBUTES(testRootedResultCtors, JS_EXPECT_HAZARDS) {
  JSObject* unrootedObj = JS_NewPlainObject(cx);
  CHECK(unrootedObj);
  Rooted<JSObject*> obj(cx, unrootedObj);

  using mozilla::detail::PackingStrategy;

  static_assert(Result<DontCopyMe_Variant, TestErr>::Strategy ==
                PackingStrategy::Variant);
  Rooted<Result<DontCopyMe_Variant, TestErr>> vv(cx, DontCopyMe_Variant{obj});
  static_assert(Result<mozilla::Ok, DontCopyMe_Variant>::Strategy ==
                PackingStrategy::Variant);
  Rooted<Result<mozilla::Ok, DontCopyMe_Variant>> ve(cx,
                                                     DontCopyMe_Variant{obj});

  static_assert(Result<DontCopyMe_NullIsOk, TestUnusedZeroEnum>::Strategy ==
                PackingStrategy::NullIsOk);
  Rooted<Result<DontCopyMe_NullIsOk, TestUnusedZeroEnum>> nv(
      cx, DontCopyMe_NullIsOk{JS::ObjectValue(*obj)});

  static_assert(Result<TriviallyCopyable_LowBitTagIsError, Failed>::Strategy ==
                PackingStrategy::LowBitTagIsError);
  Rooted<Result<TriviallyCopyable_LowBitTagIsError, Failed>> lv(
      cx, TriviallyCopyable_LowBitTagIsError{obj});

  CHECK(obj == unrootedObj);

  CHECK(vv.get().inspect().obj == obj);
  CHECK(ve.get().inspectErr().obj == obj);
  CHECK(nv.get().inspect().val.toObjectOrNull() == obj);
  CHECK(lv.get().inspect().obj == obj);

  JS_GC(cx);
  CHECK(obj != unrootedObj);

  CHECK(vv.get().inspect().obj == obj);
  CHECK(ve.get().inspectErr().obj == obj);
  CHECK(nv.get().inspect().val.toObjectOrNull() == obj);
  CHECK(lv.get().inspect().obj == obj);
  CHECK(copies == 0);
  return true;
}
END_TEST(testRootedResultCtors)

#if defined(HAVE_64BIT_BUILD) && !defined(XP_WIN)

// This depends on a pointer fitting in 48 bits, leaving space for an empty
// struct and a bool in a packed struct. Windows doesn't seem to do this
// packing, so we'll skip this test here. We're primarily checking whether
// copy constructors get called, which should be cross-platform, and
// secondarily making sure that the Rooted/tracing stuff is compiled and
// executed properly. There are certainly more clever ways to do this that
// would work cross-platform, but it doesn't seem worth the bother right now.

struct __attribute__((packed)) DontCopyMe_PackedVariant {
  uintptr_t obj : 48;
  static JSObject* Unwrap(uintptr_t packed) {
    return reinterpret_cast<JSObject*>(packed);
  }
  static uintptr_t Store(JSObject* obj) {
    return reinterpret_cast<uintptr_t>(obj);
  }

  DontCopyMe_PackedVariant() : obj(0) {}
  explicit DontCopyMe_PackedVariant(JSObject* objArg)
      : obj(reinterpret_cast<uintptr_t>(objArg)) {}
  DontCopyMe_PackedVariant(const DontCopyMe_PackedVariant& other)
      : obj(other.obj) {
    copies++;
  }
  DontCopyMe_PackedVariant(DontCopyMe_PackedVariant&& other) : obj(other.obj) {
    other.obj = 0;
  }
  void trace(JSTracer* trc) {
    JSObject* realObj = Unwrap(obj);
    TraceRoot(trc, &realObj, "obj");
    obj = Store(realObj);
  }
};

static_assert(std::is_default_constructible_v<DontCopyMe_PackedVariant>);
static_assert(std::is_default_constructible_v<TestErr>);
static_assert(mozilla::detail::IsPackableVariant<DontCopyMe_PackedVariant,
                                                 TestErr>::value);

BEGIN_TEST_WITH_ATTRIBUTES(testResultPackedVariant, JS_EXPECT_HAZARDS) {
  JSObject* unrootedObj = JS_NewPlainObject(cx);
  CHECK(unrootedObj);
  Rooted<JSObject*> obj(cx, unrootedObj);

  using mozilla::detail::PackingStrategy;

  static_assert(Result<DontCopyMe_PackedVariant, TestErr>::Strategy ==
                PackingStrategy::PackedVariant);
  Rooted<Result<DontCopyMe_PackedVariant, TestErr>> pv(
      cx, DontCopyMe_PackedVariant{obj});
  static_assert(Result<mozilla::Ok, DontCopyMe_PackedVariant>::Strategy ==
                PackingStrategy::PackedVariant);
  Rooted<Result<mozilla::Ok, DontCopyMe_PackedVariant>> pe(
      cx, DontCopyMe_PackedVariant{obj});

  CHECK(obj == unrootedObj);

  CHECK(DontCopyMe_PackedVariant::Unwrap(pv.get().inspect().obj) == obj);
  CHECK(DontCopyMe_PackedVariant::Unwrap(pe.get().inspectErr().obj) == obj);

  JS_GC(cx);
  CHECK(obj != unrootedObj);

  CHECK(DontCopyMe_PackedVariant::Unwrap(pv.get().inspect().obj) == obj);
  CHECK(DontCopyMe_PackedVariant::Unwrap(pe.get().inspectErr().obj) == obj);

  return true;
}
END_TEST(testResultPackedVariant)

#endif  // HAVE_64BIT_BUILD

BEGIN_TEST(testIndexOf) {
  // Test template metaprogramming for finding the index of a type in a
  // parameter pack. This is used by RootedField.
  //
  // Missing types fail to compile.

  CHECK((JS::detail::IndexOfTypeV<int, int> == 0));
  CHECK((JS::detail::IndexOfTypeV<int, float, int, float> == 1));
  CHECK((JS::detail::IndexOfTypeV<int, float, double, int> == 2));

  // IndexOfType doesn't check for duplicates.
  CHECK((JS::detail::IndexOfTypeV<float, float, int, float> == 0));

  return true;
}
END_TEST(testIndexOf)

BEGIN_TEST(testRootedTuple) {
  // Tuple with a single GC thing field.
  {
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> x(roots);
    CHECK(!x);
    CHECK(x == nullptr);
    x = JS_NewPlainObject(cx);
    CHECK(x);
    CHECK(IsInsideNursery(x));
    JS_GC(cx);
    CHECK(x);
    CHECK(!IsInsideNursery(x));
  }

  // Tuple with multiple fields of different types.
  {
    RootedTuple<JSObject*, JSString*> roots(cx);
    RootedField<JSObject*> x(roots);
    RootedField<JSString*> y(roots);
    CHECK(!x);
    CHECK(!y);
    x = JS_NewPlainObject(cx);
    y = JS_NewStringCopyZ(cx, "foobar");
    CHECK(x);
    CHECK(y);
    CHECK(IsInsideNursery(x));
    CHECK(IsInsideNursery(y));
    JS_GC(cx);
    CHECK(x);
    CHECK(y);
    CHECK(!IsInsideNursery(x));
    CHECK(!IsInsideNursery(y));
  }

  // Tuple with multiple fields of the same type.
  {
    RootedTuple<JSObject*, JSObject*> roots(cx);
    RootedField<JSObject*, 0> x(roots);
    RootedField<JSObject*, 1> y(roots);
    CHECK(!x);
    CHECK(!y);
    x = JS_NewPlainObject(cx);
    y = JS_NewPlainObject(cx);
    CHECK(x);
    CHECK(y);
    CHECK(x != y);
    CHECK(IsInsideNursery(x));
    CHECK(IsInsideNursery(y));
    JS_GC(cx);
    CHECK(x);
    CHECK(y);
    CHECK(x != y);
    CHECK(!IsInsideNursery(x));
    CHECK(!IsInsideNursery(y));
  }

  // Test initialization by RootedField.
  {
    Rooted<JSObject*> obj(cx, JS_NewPlainObject(cx));
    CHECK(obj);
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> x(roots, obj);
    CHECK(x == obj);
  }

  // Test RootedField converts to Handle.
  {
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> array(roots, JS::NewArrayObject(cx, 1));
    CHECK(array);
    uint32_t length = 0;
    CHECK(JS::GetArrayLength(cx, array, &length));
    CHECK(length == 1);
  }

  // Test &RootedField converts to MutableHandle.
  {
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> obj(roots);
    CHECK(!obj);
    CHECK(JS_GetClassObject(cx, JSProto_Object, &obj));
    CHECK(obj);
  }

  // Test RootedField converts to Handle.
  {
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> array(roots, JS::NewArrayObject(cx, 1));
    CHECK(array);
    uint32_t length = 0;
    CHECK(JS::GetArrayLength(cx, array, &length));
    CHECK(length == 1);
  }

  // Test &RootedField converts to MutableHandle.
  {
    RootedTuple<JSObject*> roots(cx);
    RootedField<JSObject*> obj(roots);
    CHECK(!obj);
    CHECK(JS_GetClassObject(cx, JSProto_Object, &obj));
    CHECK(obj);
  }

  return true;
}
END_TEST(testRootedTuple)

BEGIN_TEST(testRootedCopying) {
  // Make sure that when you write JS::Rooted<T> obj(cx, some_argument)
  // some_argument is not copied.

  struct NoCopy {
    NoCopy() = default;
    NoCopy(NoCopy&) = delete;
    NoCopy(const NoCopy&) = delete;
    NoCopy& operator=(const NoCopy&) = delete;
  };

  struct StructWithNoCopyArg {
    StructWithNoCopyArg(void* a, NoCopy& nc) {}
    void trace(JSTracer* trace) {}
  };

  // Two arguments to choose
  //   Rooted(const RootingContext& cx, CtorArgs... args)
  // over
  //   Rooted(const RootingContext& cx, S&& initial)
  void* a = nullptr;
  NoCopy nc;

  // This compiling is a test pass.
  JS::Rooted<StructWithNoCopyArg> rooted(cx, a, nc);
  return true;
}
END_TEST(testRootedCopying)

BEGIN_TEST(testRootedRealm) {
  // Create a new global and use Rooted<Realm*> to keep it alive.
  Rooted<Realm*> realm(cx);
  {
    JS::RealmOptions globalOptions;
    JSObject* otherGlobal = JS_NewGlobalObject(
        cx, getGlobalClass(), nullptr, JS::FireOnNewGlobalHook, globalOptions);
    CHECK(otherGlobal);
    realm = JS::GetObjectRealmOrNull(otherGlobal);
    CHECK(realm);
  }

  JS_GC(cx);

  // Use the realm.
  JSAutoRealm ar(cx, JS::GetRealmGlobalOrNull(realm));
  JS::RootedValue v(cx);
  EVAL("let x = -1234; Math.abs(x)", &v);
  CHECK(v.toNumber() == 1234);

  return true;
}
END_TEST(testRootedRealm)
