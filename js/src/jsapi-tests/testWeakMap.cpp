/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/WeakSetObject.h"
#include "gc/Zone.h"
#include "js/Array.h"      // JS::GetArrayLength
#include "js/Exception.h"  // JS_IsExceptionPending
#include "js/friend/CycleCollector.h"
#include "js/GlobalObject.h"        // JS_NewGlobalObject
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/WeakMap.h"
#include "jsapi-tests/tests.h"
#include "vm/Realm.h"
#include "vm/SymbolType.h"

using namespace js;

static bool checkSize(JSContext* cx, JS::HandleObject map, uint32_t expected) {
  JS::RootedObject keys(cx);
  if (!JS_NondeterministicGetWeakMapKeys(cx, map, &keys)) {
    return false;
  }

  uint32_t length;
  if (!JS::GetArrayLength(cx, keys, &length)) {
    return false;
  }

  return length == expected;
}

JSObject* keyDelegate = nullptr;

BEGIN_TEST(testWeakMap_basicOperations) {
  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(IsWeakMapObject(map));

  JS::RootedValue key(cx, JS::ObjectOrNullValue(newKey()));
  CHECK(!key.isNull());
  CHECK(!JS::IsWeakMapObject(&key.toObject()));

  JS::RootedValue r(cx);
  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r.isUndefined());

  CHECK(checkSize(cx, map, 0));

  JS::RootedValue val(cx, JS::Int32Value(1));
  CHECK(SetWeakMapEntry(cx, map, key, val));

  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r == val);
  CHECK(checkSize(cx, map, 1));

  JS_GC(cx);

  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r == val);
  CHECK(checkSize(cx, map, 1));

  key.setUndefined();
  JS_GC(cx);

  CHECK(checkSize(cx, map, 0));

  return true;
}

JSObject* newKey() { return JS_NewPlainObject(cx); }
END_TEST(testWeakMap_basicOperations)

BEGIN_TEST(testWeakMap_setWeakMapEntry_invalid_key) {
  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(IsWeakMapObject(map));
  CHECK(checkSize(cx, map, 0));

  JS::RootedString test(cx, JS_NewStringCopyZ(cx, "test"));
  // sym is a Symbol in global Symbol registry and hence can't be used as a key.
  JS::RootedSymbol sym(cx, JS::GetSymbolFor(cx, test));
  JS::RootedValue key(cx, JS::SymbolValue(sym));
  CHECK(!key.isUndefined());

  JS::RootedValue val(cx, JS::Int32Value(1));

  CHECK(!JS_IsExceptionPending(cx));
  CHECK(SetWeakMapEntry(cx, map, key, val) == false);

  CHECK(JS_IsExceptionPending(cx));
  JS::Rooted<JS::Value> exn(cx);
  CHECK(JS_GetPendingException(cx, &exn));
  JS::Rooted<JSObject*> obj(cx, &exn.toObject());
  JS::BorrowedErrorReport err(cx);
  CHECK(JS_ErrorFromException(cx, obj, err));
  CHECK(err->exnType == JSEXN_TYPEERR);

  JS_ClearPendingException(cx);

  JS::RootedValue r(cx);
  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r == JS::UndefinedValue());
  CHECK(checkSize(cx, map, 0));

  return true;
}
END_TEST(testWeakMap_setWeakMapEntry_invalid_key)

BEGIN_TEST(testWeakMap_basicOperations_symbols_as_keys) {
  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(IsWeakMapObject(map));

  JS::RootedString test(cx, JS_NewStringCopyZ(cx, "test"));
  JS::RootedSymbol sym(cx, JS::NewSymbol(cx, test));
  CHECK(sym);
  JS::RootedValue key(cx, JS::SymbolValue(sym));

  JS::RootedValue r(cx);
  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r.isUndefined());

  CHECK(checkSize(cx, map, 0));

  JS::RootedValue val(cx, JS::Int32Value(1));
  CHECK(SetWeakMapEntry(cx, map, key, val));

  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r == val);
  CHECK(checkSize(cx, map, 1));

  JS_GC(cx);

  CHECK(GetWeakMapEntry(cx, map, key, &r));
  CHECK(r == val);
  CHECK(checkSize(cx, map, 1));

  sym = nullptr;
  key.setUndefined();
  JS_GC(cx);

  CHECK(checkSize(cx, map, 0));

  return true;
}
END_TEST(testWeakMap_basicOperations_symbols_as_keys)

BEGIN_TEST(testWeakMap_keyDelegates) {
  AutoLeaveZeal nozeal(cx);

  AutoGCParameter param(cx, JSGC_INCREMENTAL_GC_ENABLED, true);
  JS_GC(cx);
  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(map);

  JS::RootedObject delegate(cx, newDelegate());
  JS::RootedObject key(cx, delegate);
  if (!JS_WrapObject(cx, &key)) {
    return false;
  }
  CHECK(key);
  CHECK(delegate);

  keyDelegate = delegate;

  JS::RootedObject delegateRoot(cx);
  {
    JSAutoRealm ar(cx, delegate);
    delegateRoot = JS_NewPlainObject(cx);
    CHECK(delegateRoot);
    JS::RootedValue delegateValue(cx, JS::ObjectValue(*delegate));
    CHECK(JS_DefineProperty(cx, delegateRoot, "delegate", delegateValue, 0));
  }
  delegate = nullptr;

  /*
   * Perform an incremental GC, introducing an unmarked CCW to force the map
   * zone to finish marking before the delegate zone.
   */
  CHECK(newCCW(map, delegateRoot));
  performIncrementalGC();
#ifdef DEBUG
  CHECK(map->zone()->lastSweepGroupIndex() <
        delegateRoot->zone()->lastSweepGroupIndex());
#endif

  /* Add our entry to the weakmap. */
  JS::RootedValue keyVal(cx, JS::ObjectValue(*key));
  JS::RootedValue val(cx, JS::Int32Value(1));
  CHECK(SetWeakMapEntry(cx, map, keyVal, val));
  CHECK(checkSize(cx, map, 1));

  /*
   * Check the delegate keeps the entry alive even if the key is not reachable.
   */
  key = nullptr;
  keyVal.setUndefined();
  CHECK(newCCW(map, delegateRoot));
  performIncrementalGC();
  CHECK(checkSize(cx, map, 1));

  /*
   * Check that the zones finished marking at the same time, which is
   * necessary because of the presence of the delegate and the CCW.
   */
#ifdef DEBUG
  CHECK(map->zone()->lastSweepGroupIndex() ==
        delegateRoot->zone()->lastSweepGroupIndex());
#endif

  /* Check that when the delegate becomes unreachable the entry is removed. */
  delegateRoot = nullptr;
  keyDelegate = nullptr;
  JS_GC(cx);
  CHECK(checkSize(cx, map, 0));

  return true;
}

static size_t DelegateObjectMoved(JSObject* obj, JSObject* old) {
  if (!keyDelegate) {
    return 0;  // Object got moved before we set keyDelegate to point to it.
  }

  MOZ_RELEASE_ASSERT(keyDelegate == old);
  keyDelegate = obj;
  return 0;
}

JSObject* newKey() {
  static const JSClass keyClass = {
      "keyWithDelegate", JSCLASS_HAS_RESERVED_SLOTS(1),
      JS_NULL_CLASS_OPS, JS_NULL_CLASS_SPEC,
      JS_NULL_CLASS_EXT, JS_NULL_OBJECT_OPS,
  };

  JS::RootedObject key(cx, JS_NewObject(cx, &keyClass));
  if (!key) {
    return nullptr;
  }

  return key;
}

JSObject* newCCW(JS::HandleObject sourceZone, JS::HandleObject destZone) {
  /*
   * Now ensure that this zone will be swept first by adding a cross
   * compartment wrapper to a new object in the same zone as the
   * delegate object.
   */
  JS::RootedObject object(cx);
  {
    JSAutoRealm ar(cx, destZone);
    object = JS_NewPlainObject(cx);
    if (!object) {
      return nullptr;
    }
  }
  {
    JSAutoRealm ar(cx, sourceZone);
    if (!JS_WrapObject(cx, &object)) {
      return nullptr;
    }
  }

  // In order to test the SCC algorithm, we need the wrapper/wrappee to be
  // tenured.
  cx->runtime()->gc.evictNursery();

  return object;
}

JSObject* newDelegate() {
  static const JSClassOps delegateClassOps = {
      .trace = JS_GlobalObjectTraceHook,
  };

  static const js::ClassExtension delegateClassExtension = {
      DelegateObjectMoved,  // objectMovedOp
  };

  static const JSClass delegateClass = {
      "delegate",
      JSCLASS_GLOBAL_FLAGS | JSCLASS_HAS_RESERVED_SLOTS(1),
      &delegateClassOps,
      JS_NULL_CLASS_SPEC,
      &delegateClassExtension,
      JS_NULL_OBJECT_OPS,
  };

  /* Create the global object. */
  JS::RealmOptions options;
  JS::RootedObject global(cx,
                          JS_NewGlobalObject(cx, &delegateClass, nullptr,
                                             JS::FireOnNewGlobalHook, options));
  if (!global) {
    return nullptr;
  }

  JS_SetReservedSlot(global, 0, JS::Int32Value(42));
  return global;
}

void performIncrementalGC() {
  JSRuntime* rt = cx->runtime();
  JS::SliceBudget budget(JS::WorkBudget(200));
  rt->gc.startDebugGC(JS::GCOptions::Normal, budget);

  // Wait until we've started sweeping before finishing the GC
  // non-incrementally otherwise we'll put all zones into one sweep group.
  while (rt->gc.state() < gc::State::Sweep) {
    rt->gc.debugGCSlice(budget);
  }
  if (JS::IsIncrementalGCInProgress(cx)) {
    rt->gc.finishGC(JS::GCReason::DEBUG_GC);
  }
}
END_TEST(testWeakMap_keyDelegates)

BEGIN_TEST(testWeakMap_traceMappings) {
  // Test the js::TraceWeakMaps API, used by the cycle collector.

  enum class KeyKind { PlainObject, Delegate, Symbol };

  enum class ValueKind { Object, String, Symbol, Int, Undefined };

  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(map);
  checkNoMappings(map);

  JS::RootedObject set(cx, NewWeakSetObject(cx));
  CHECK(set);
  checkNoMappings(set);

  for (KeyKind keyKind :
       {KeyKind::PlainObject, KeyKind::Delegate, KeyKind::Symbol}) {
    JS::RootedValue key(cx);
    switch (keyKind) {
      case KeyKind::PlainObject: {
        JSObject* object = JS_NewPlainObject(cx);
        CHECK(object);
        key = ObjectValue(*object);
        break;
      }
      case KeyKind::Delegate: {
        JS::RealmOptions options;
        JS::RootedObject global(
            cx, JS_NewGlobalObject(cx, getGlobalClass(), nullptr,
                                   JS::FireOnNewGlobalHook, options));
        CHECK(global);
        key = ObjectValue(*global);
        CHECK(JS_WrapValue(cx, &key));
        CHECK(&key.toObject() != global);
        break;
      }
      case KeyKind::Symbol: {
        JS::Symbol* symbol = JS::NewSymbol(cx, nullptr);
        CHECK(symbol);
        key = SymbolValue(symbol);
        break;
      }
    }

    for (ValueKind valueKind :
         {ValueKind::Object, ValueKind::String, ValueKind::Symbol,
          ValueKind::Int, ValueKind::Undefined}) {
      JS::RootedValue value(cx);
      switch (valueKind) {
        case ValueKind::Object: {
          JSObject* object = JS_NewPlainObject(cx);
          CHECK(object);
          value = ObjectValue(*object);
          break;
        }
        case ValueKind::String: {
          JSString* string = JS_NewStringCopyZ(cx, "weak map value");
          CHECK(string);
          value = StringValue(string);
          break;
        }
        case ValueKind::Symbol: {
          JS::Symbol* symbol = JS::NewSymbol(cx, nullptr);
          CHECK(symbol);
          value = SymbolValue(symbol);
          break;
        }
        case ValueKind::Int: {
          value = JS::Int32Value(42);
          break;
        }
        case ValueKind::Undefined: {
          value = JS::UndefinedValue();
          break;
        }
      }

      JS_GC(cx);

      CHECK(JS::SetWeakMapEntry(cx, map, key, value));
      CHECK(checkHasMapping(map, key, value));

      CHECK(AddWeakSetEntry(cx, set, key));
      CHECK(checkHasMapping(set, key, JS::NullHandleValue));
    }

    key = UndefinedValue();
    JS_GC(cx);
    checkNoMappings(map);
  }

  return true;
}

struct MappingTracer : public js::WeakMapTracer {
  JSObject* mapToMatch;
  JS::GCCellPtr keyToMatch;
  JS::GCCellPtr valueToMatch;
  size_t entryCount = 0;
  size_t matchCount = 0;

  MappingTracer(JSRuntime* rt, JSObject* map, JS::GCCellPtr key,
                JS::GCCellPtr value)
      : js::WeakMapTracer(rt),
        mapToMatch(map),
        keyToMatch(key),
        valueToMatch(value) {}

  void trace(JSObject* map, JS::GCCellPtr key, JS::GCCellPtr delegate,
             JS::GCCellPtr value) override {
    if (map == mapToMatch) {
      entryCount++;
      if (key == keyToMatch && value == valueToMatch) {
        matchCount++;
      }
    }
  }
};

bool checkNoMappings(JS::HandleObject map) {
  JS_GC(cx);
  MappingTracer tracer(cx->runtime(), map, JS::GCCellPtr(), JS::GCCellPtr());
  js::TraceWeakMaps(&tracer);
  CHECK(tracer.entryCount == 0);
  CHECK(tracer.matchCount == 0);
  return true;
}

bool checkHasMapping(JS::HandleObject map, JS::HandleValue key,
                     JS::HandleValue value) {
  JS_GC(cx);
  MappingTracer tracer(
      cx->runtime(), map, key.toGCCellPtr(),
      value.isGCThing() ? value.toGCCellPtr() : JS::GCCellPtr());
  js::TraceWeakMaps(&tracer);
  CHECK(tracer.entryCount == 1);
  CHECK(tracer.matchCount == 1);
  return true;
}
END_TEST(testWeakMap_traceMappings)

struct DelegateTracer : public js::WeakMapTracer {
  JSObject* mapToMatch;
  size_t entryCount = 0;
  JS::GCCellPtr lastDelegate;

  DelegateTracer(JSRuntime* rt, JSObject* map)
      : js::WeakMapTracer(rt), mapToMatch(map) {}

  void trace(JSObject* map, JS::GCCellPtr key, JS::GCCellPtr delegate,
             JS::GCCellPtr value) override {
    if (map == mapToMatch) {
      entryCount++;
      lastDelegate = delegate;
    }
  }
};

BEGIN_TEST(testWeakMap_traceMappingsDelegate) {
  // Test that js::TraceWeakMaps passes the correct delegate to the tracer.

  // A plain object has no delegate.
  JS::RootedValue key(cx, ObjectValue(*JS_NewPlainObject(cx)));
  Rooted<JS::GCCellPtr> expectedDelegate(cx);
  CHECK(checkDelegate(key, expectedDelegate));

  // The delegate of a CCW is its target.
  JS::RealmOptions options;
  JS::RootedObject global(cx,
                          JS_NewGlobalObject(cx, getGlobalClass(), nullptr,
                                             JS::FireOnNewGlobalHook, options));
  CHECK(global);
  key = ObjectValue(*global);
  CHECK(JS_WrapValue(cx, &key));
  CHECK(&key.toObject() != global);
  expectedDelegate = JS::GCCellPtr(global.get());
  CHECK(checkDelegate(key, expectedDelegate));

  // The delegate of a unique symbol is its shared symbol.
  JS::Symbol* symbol = JS::NewSymbol(cx, nullptr);
  CHECK(symbol);
  CHECK(symbol->isUnique());
  key = SymbolValue(symbol);
  expectedDelegate = JS::GCCellPtr(symbol->sharedSymbol());
  CHECK(checkDelegate(key, expectedDelegate));

  return true;
}

bool checkDelegate(JS::HandleValue key,
                   Handle<JS::GCCellPtr> expectedDelegate) {
  JS::RootedObject map(cx, JS::NewWeakMapObject(cx));
  CHECK(map);

  JS::RootedValue value(cx, JS::Int32Value(1));
  CHECK(JS::SetWeakMapEntry(cx, map, key, value));

  DelegateTracer tracer(cx->runtime(), map);
  js::TraceWeakMaps(&tracer);
  CHECK(tracer.entryCount == 1);
  CHECK(tracer.lastDelegate == expectedDelegate);
  return true;
}
END_TEST(testWeakMap_traceMappingsDelegate)
