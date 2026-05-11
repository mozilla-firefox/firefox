/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Test JSObject::swap.
 *
 * This test creates proxy objects from a description of their configuration.
 * Each proxy is given a unique private value, expando object, and reserved-slot
 * values. A list of configurations is created and the result of swapping every
 * combination checked.
 */

#include "mozilla/Sprintf.h"

#include "js/AllocPolicy.h"
#include "js/Vector.h"
#include "jsapi-tests/tests.h"
#include "vm/PlainObject.h"

#include "gc/StableCellHasher-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"

using namespace js;

struct ObjectConfig {
  const JSClass* clasp;
  bool nurseryAllocated;
  bool hasUniqueId;
};

using ObjectConfigVector = Vector<ObjectConfig, 0, SystemAllocPolicy>;

static const JSClass TestProxyClasses[] = {
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(1 /* Min */)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(2)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(3)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(4)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(5)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(6)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(7)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(8)),
    PROXY_CLASS_DEF("TestProxy", JSCLASS_HAS_RESERVED_SLOTS(14 /* Max */)),
};

static bool Verbose = false;

class TenuredProxyHandler final : public Wrapper {
 public:
  static const TenuredProxyHandler singleton;
  constexpr TenuredProxyHandler() : Wrapper(0) {}
  bool canNurseryAllocate() const override { return false; }
};

const TenuredProxyHandler TenuredProxyHandler::singleton;

class NurseryProxyHandler final : public Wrapper {
 public:
  static const NurseryProxyHandler singleton;
  constexpr NurseryProxyHandler() : Wrapper(0) {}
  bool canNurseryAllocate() const override { return true; }
};

const NurseryProxyHandler NurseryProxyHandler::singleton;

BEGIN_TEST(testObjectSwap) {
  AutoLeaveZeal noZeal(cx);

  ObjectConfigVector objectConfigs = CreateObjectConfigs();

  for (const ObjectConfig& config1 : objectConfigs) {
    for (const ObjectConfig& config2 : objectConfigs) {
      // JSObject::swap requires both objects to have the same alloc kind.
      if (JSCLASS_RESERVED_SLOTS(config1.clasp) !=
          JSCLASS_RESERVED_SLOTS(config2.clasp)) {
        gc::AllocKind k1 =
            gc::GetGCObjectKind(js::detail::ProxyValueArray::allocCount(
                JSCLASS_RESERVED_SLOTS(config1.clasp)));
        gc::AllocKind k2 =
            gc::GetGCObjectKind(js::detail::ProxyValueArray::allocCount(
                JSCLASS_RESERVED_SLOTS(config2.clasp)));
        if (k1 != k2) {
          continue;
        }
      }

      {
        uint32_t id1;
        RootedObject obj1(cx, CreateObject(config1, &id1));
        CHECK(obj1);

        uint32_t id2;
        RootedObject obj2(cx, CreateObject(config2, &id2));
        CHECK(obj2);

        if (Verbose) {
          fprintf(stderr, "Swap %p (%s) and %p (%s)\n", obj1.get(),
                  GetLocation(obj1), obj2.get(), GetLocation(obj2));
        }

        uint64_t uid1 = 0;
        if (config1.hasUniqueId) {
          uid1 = gc::GetUniqueIdInfallible(obj1);
        }
        uint64_t uid2 = 0;
        if (config2.hasUniqueId) {
          uid2 = gc::GetUniqueIdInfallible(obj2);
        }

        {
          AutoEnterOOMUnsafeRegion oomUnsafe;
          JSObject::swap(cx, obj1, obj2, oomUnsafe);
        }

        CHECK(CheckObject(obj1, config2, id2));
        CHECK(CheckObject(obj2, config1, id1));

        CHECK(CheckUniqueIds(obj1, config1.hasUniqueId, uid1, obj2,
                             config2.hasUniqueId, uid2));

        // Check we can promote swapped nursery objects.
        cx->minorGC(JS::GCReason::API);
      }

      if (Verbose) {
        fprintf(stderr, "\n");
      }
    }

    // JSObject::swap can suppress GC so ensure we clean up occasionally.
    JS_GC(cx);
  }

  return true;
}

ObjectConfigVector CreateObjectConfigs() {
  ObjectConfigVector configs;

  ObjectConfig config;

  for (bool nurseryAllocated : {false, true}) {
    config.nurseryAllocated = nurseryAllocated;

    for (bool hasUniqueId : {false, true}) {
      config.hasUniqueId = hasUniqueId;

      for (const JSClass& jsClass : TestProxyClasses) {
        config.clasp = &jsClass;
        MOZ_RELEASE_ASSERT(configs.append(config));
      }
    }
  }

  return configs;
}

const char* GetLocation(JSObject* obj) {
  return obj->isTenured() ? "tenured heap" : "nursery";
}

// Counter used to give slots and property names unique values.
uint32_t nextId = 0;

JSObject* CreateObject(const ObjectConfig& config, uint32_t* idOut) {
  *idOut = nextId;
  JSObject* obj = CreateProxy(config);

  if (config.hasUniqueId) {
    uint64_t unused;
    if (!gc::GetOrCreateUniqueId(obj, &unused)) {
      return nullptr;
    }
  }

  return obj;
}

JSObject* CreateProxy(const ObjectConfig& config) {
  RootedValue priv(cx, Int32Value(nextId++));

  RootedObject expando(cx, NewPlainObject(cx));
  RootedValue expandoId(cx, Int32Value(nextId++));
  if (!expando || !JS_SetProperty(cx, expando, "id", expandoId)) {
    return nullptr;
  }

  ProxyOptions options;
  options.setClass(config.clasp);
  options.setLazyProto(true);

  const Wrapper* handler;
  if (config.nurseryAllocated) {
    handler = &NurseryProxyHandler::singleton;
  } else {
    handler = &TenuredProxyHandler::singleton;
  }

  RootedObject obj(cx, NewProxyObject(cx, handler, priv, nullptr, options));
  if (!obj) {
    return nullptr;
  }

  Rooted<ProxyObject*> proxy(cx, &obj->as<ProxyObject>());
  proxy->setExpando(expando);

  for (uint32_t i = 0; i < JSCLASS_RESERVED_SLOTS(config.clasp); i++) {
    JS::SetReservedSlot(proxy, i, Int32Value(nextId++));
  }

  MOZ_RELEASE_ASSERT(IsInsideNursery(proxy) == config.nurseryAllocated);

  return proxy;
}

bool CheckObject(HandleObject obj, const ObjectConfig& config, uint32_t id) {
  CHECK(obj->is<ProxyObject>());
  CHECK(obj->getClass() == config.clasp);

  uint32_t reservedSlots = JSCLASS_RESERVED_SLOTS(config.clasp);

  if (Verbose) {
    fprintf(stderr, "Check %p is a proxy object with %u reserved slots\n",
            obj.get(), reservedSlots);
  }

  CHECK(GetProxyPrivate(obj) == Int32Value(id++));

  Value expandoValue = GetProxyExpando(obj);
  CHECK(expandoValue.isObject());

  RootedObject expando(cx, &expandoValue.toObject());
  RootedValue expandoId(cx);
  JS_GetProperty(cx, expando, "id", &expandoId);
  CHECK(expandoId == Int32Value(id++));

  for (uint32_t i = 0; i < reservedSlots; i++) {
    CHECK(JS::GetReservedSlot(obj, i) == Int32Value(id++));
  }

  return true;
}

bool CheckUniqueIds(HandleObject obj1, bool hasUniqueId1, uint64_t uid1,
                    HandleObject obj2, bool hasUniqueId2, uint64_t uid2) {
  if (uid1 && uid2) {
    MOZ_RELEASE_ASSERT(uid1 != uid2);
  }

  // Check unique IDs are NOT swapped.
  CHECK(CheckUniqueId(obj1, hasUniqueId1, uid1));
  CHECK(CheckUniqueId(obj2, hasUniqueId2, uid2));

  // Check unique IDs are different if present.
  if (gc::HasUniqueId(obj1) && gc::HasUniqueId(obj2)) {
    CHECK(gc::GetUniqueIdInfallible(obj1) != gc::GetUniqueIdInfallible(obj2));
  }

  return true;
}

bool CheckUniqueId(HandleObject obj, bool hasUniqueId, uint64_t uid) {
  if (hasUniqueId) {
    CHECK(gc::HasUniqueId(obj));
    CHECK(gc::GetUniqueIdInfallible(obj) == uid);
  } else {
    // Swap may add a unique ID to an object.
  }

  return true;
}

END_TEST(testObjectSwap)
