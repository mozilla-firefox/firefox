/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "vm/RealmFuses.h"

#include <array>

#include "builtin/MapObject.h"
#include "builtin/Promise.h"
#include "builtin/RegExp.h"
#include "builtin/WeakMapObject.h"
#include "builtin/WeakSetObject.h"
#include "debugger/DebugScript.h"
#include "js/experimental/TypedData.h"
#include "vm/GlobalObject.h"
#include "vm/NativeObject.h"
#include "vm/ObjectOperations.h"
#include "vm/Realm.h"
#include "vm/SelfHosting.h"

#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

void js::InvalidatingRealmFuse::popFuse(JSContext* cx, RealmFuses& realmFuses) {
  // Return early if the fuse is already popped.
  if (!intact()) {
    return;
  }

  InvalidatingFuse::popFuse(cx);

  for (auto& fd : realmFuses.fuseDependencies) {
    fd.invalidateForFuse(cx, this);
  }
}

bool js::InvalidatingRealmFuse::addFuseDependency(
    JSContext* cx, const jit::IonScriptKey& ionScript) {
  MOZ_ASSERT(ionScript.script()->realm() == cx->realm());
  auto* scriptSet =
      cx->realm()->realmFuses.fuseDependencies.getOrCreateDependentScriptSet(
          cx, this);
  if (!scriptSet) {
    return false;
  }

  return scriptSet->addScriptForFuse(this, ionScript);
}

void js::PopsOptimizedGetIteratorFuse::popFuse(JSContext* cx,
                                               RealmFuses& realmFuses) {
  // Pop Self.
  RealmFuse::popFuse(cx);

  // Pop associated fuse in same realm as current object.
  realmFuses.optimizeGetIteratorFuse.popFuse(cx, realmFuses);
}

void js::PopsOptimizedArrayIteratorPrototypeFuse::popFuse(
    JSContext* cx, RealmFuses& realmFuses) {
  // Pop Self.
  RealmFuse::popFuse(cx);

  // Pop associated fuse in same realm as current object.
  realmFuses.optimizeArrayIteratorPrototypeFuse.popFuse(cx, realmFuses);
}

int32_t js::RealmFuses::fuseOffsets[uint8_t(
    RealmFuses::FuseIndex::LastFuseIndex)] = {
#define FUSE(Name, LowerName) offsetof(RealmFuses, LowerName),
    FOR_EACH_REALM_FUSE(FUSE)
#undef FUSE
};

// static
int32_t js::RealmFuses::offsetOfFuseWordRelativeToRealm(
    RealmFuses::FuseIndex index) {
  int32_t base_offset = offsetof(Realm, realmFuses);
  int32_t fuse_offset = RealmFuses::fuseOffsets[uint8_t(index)];
  int32_t fuseWordOffset = GuardFuse::fuseOffset();

  return base_offset + fuse_offset + fuseWordOffset;
}

const char* js::RealmFuses::fuseNames[] = {
#define FUSE(Name, LowerName) #LowerName,
    FOR_EACH_REALM_FUSE(FUSE)
#undef FUSE
};

// TODO: It is not elegant that we have both this mechanism, but also
// GuardFuse::name, and all the overrides for naming fuses. The issue is
// that this method is static to handle consumers that don't have a
// RealmFuses around but work with indexes (e.g. spew code).
//
// I'd love it if we had a better answer.
const char* js::RealmFuses::getFuseName(RealmFuses::FuseIndex index) {
  uint8_t rawIndex = uint8_t(index);
  MOZ_ASSERT(index < RealmFuses::FuseIndex::LastFuseIndex);
  return fuseNames[rawIndex];
}

bool js::OptimizeGetIteratorFuse::checkInvariant(JSContext* cx) {
  // Simple invariant: this fuse merely reflects the conjunction of a group of
  // fuses, so if this fuse is intact, then the invariant it asserts is that
  // these two realm fuses are also intact.
  auto& realmFuses = cx->realm()->realmFuses;
  return realmFuses.arrayPrototypeIteratorFuse.intact() &&
         realmFuses.optimizeArrayIteratorPrototypeFuse.intact();
}

void js::OptimizeGetIteratorFuse::popFuse(JSContext* cx,
                                          RealmFuses& realmFuses) {
  RealmFuse::popFuse(cx, realmFuses);
  realmFuses.optimizeGetIteratorBytecodeFuse.popFuse(cx, realmFuses);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(),
                               JSUseCounter::OPTIMIZE_GET_ITERATOR_FUSE);
}

bool js::OptimizeGetIteratorBytecodeFuse::checkInvariant(JSContext* cx) {
  auto& realmFuses = cx->realm()->realmFuses;
  if (!realmFuses.optimizeGetIteratorFuse.intact()) {
    return false;
  }
  // If there's a DebugScript for a script in this realm, this fuse should have
  // been popped.
  if (DebugScriptMap* map = cx->zone()->debugScriptMap) {
    for (auto iter = map->iter(); !iter.done(); iter.next()) {
      JSScript* script = iter.get().key();
      if (script->realm() == cx->realm()) {
        return false;
      }
    }
  }
  return true;
}

bool js::OptimizeArrayIteratorPrototypeFuse::checkInvariant(JSContext* cx) {
  // Simple invariant: this fuse merely reflects the conjunction of a group of
  // fuses, so if this fuse is intact, then the invariant it asserts is that
  // these realm fuses are also intact.
  auto& realmFuses = cx->realm()->realmFuses;
  return realmFuses.arrayPrototypeIteratorNextFuse.intact() &&
         realmFuses.arrayIteratorPrototypeHasNoReturnProperty.intact() &&
         realmFuses.iteratorPrototypeHasNoReturnProperty.intact() &&
         realmFuses.arrayIteratorPrototypeHasIteratorProto.intact() &&
         realmFuses.iteratorPrototypeHasObjectProto.intact() &&
         realmFuses.objectPrototypeHasNoReturnProperty.intact();
}

static bool ObjectHasDataProperty(NativeObject* obj, PropertyKey key,
                                  Value* val) {
  mozilla::Maybe<PropertyInfo> prop = obj->lookupPure(key);
  if (prop.isNothing() || !prop->isDataProperty()) {
    return false;
  }
  *val = obj->getSlot(prop->slot());
  return true;
}

// Returns true if `obj` has a data property with the given `key` and its value
// is `expectedValue`.
static bool ObjectHasDataPropertyValue(NativeObject* obj, PropertyKey key,
                                       const Value& expectedValue) {
  Value v;
  if (!ObjectHasDataProperty(obj, key, &v)) {
    return false;
  }
  return v == expectedValue;
}

// Returns true if `obj` has a data property with the given `key` and its value
// is a native function that matches `expectedFunction`.
static bool ObjectHasDataPropertyFunction(NativeObject* obj, PropertyKey key,
                                          JSNative expectedFunction) {
  Value v;
  if (!ObjectHasDataProperty(obj, key, &v)) {
    return false;
  }
  if (!IsNativeFunction(v, expectedFunction)) {
    return false;
  }
  if (obj->realm() != v.toObject().as<JSFunction>().realm()) {
    return false;
  }
  return true;
}

// Returns true if `obj` has a data property with the given `key` and its value
// is a self-hosted function with `selfHostedName`.
static bool ObjectHasDataPropertyFunction(NativeObject* obj, PropertyKey key,
                                          PropertyName* selfHostedName) {
  Value v;
  if (!ObjectHasDataProperty(obj, key, &v)) {
    return false;
  }
  if (!IsSelfHostedFunctionWithName(v, selfHostedName)) {
    return false;
  }
  if (obj->realm() != v.toObject().as<JSFunction>().realm()) {
    return false;
  }
  return true;
}

static bool ObjectHasGetterProperty(NativeObject* obj, PropertyKey key,
                                    JSFunction** getter) {
  mozilla::Maybe<PropertyInfo> prop = obj->lookupPure(key);
  if (prop.isNothing() || !prop->isAccessorProperty()) {
    return false;
  }
  JSObject* getterObject = obj->getGetter(*prop);
  if (!getterObject || !getterObject->is<JSFunction>()) {
    return false;
  }
  if (obj->realm() != getterObject->as<JSFunction>().realm()) {
    return false;
  }
  *getter = &getterObject->as<JSFunction>();
  return true;
}

// Returns true if `obj` has an accessor property with the given `key` and the
// getter is a native function that matches `expectedFunction`.
static bool ObjectHasGetterFunction(NativeObject* obj, PropertyKey key,
                                    JSNative expectedGetter) {
  JSFunction* getter;
  if (!ObjectHasGetterProperty(obj, key, &getter)) {
    return false;
  }
  return IsNativeFunction(getter, expectedGetter);
}

// Returns true if `obj` has an accessor property with the given `key` and the
// getter is a self-hosted function with `selfHostedName`.
static bool ObjectHasGetterFunction(NativeObject* obj, PropertyKey key,
                                    PropertyName* selfHostedName) {
  JSFunction* getter;
  if (!ObjectHasGetterProperty(obj, key, &getter)) {
    return false;
  }
  return IsSelfHostedFunctionWithName(getter, selfHostedName);
}

bool js::ArrayPrototypeIteratorFuse::checkInvariant(JSContext* cx) {
  // Prototype must be Array.prototype.
  auto* proto = cx->global()->maybeGetArrayPrototype();
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }

  PropertyKey iteratorKey =
      PropertyKey::Symbol(cx->wellKnownSymbols().iterator);

  // Ensure that Array.prototype's @@iterator slot is unchanged.
  return ObjectHasDataPropertyFunction(proto, iteratorKey,
                                       cx->names().dollar_ArrayValues_);
}

/* static */
bool js::ArrayPrototypeIteratorNextFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetArrayIteratorPrototype();

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  // Ensure that %ArrayIteratorPrototype%'s "next" slot is unchanged.
  return ObjectHasDataPropertyFunction(proto, NameToId(cx->names().next),
                                       cx->names().ArrayIteratorNext);
}

static bool HasNoReturnName(JSContext* cx, JS::HandleObject proto) {
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  JS::RootedId returnName(cx, NameToId(cx->names().return_));

  // An alternative design here would chain together all the has-return-property
  // fuses such that the fuses each express a stronger invariant; for now these
  // fuses have only the invariant that each object -itself- has no return
  // property.
  bool found = true;
  if (!HasOwnProperty(cx, proto, returnName, &found)) {
    cx->recoverFromOutOfMemory();
    return true;
  }

  return !found;
}

/* static */
bool js::ArrayIteratorPrototypeHasNoReturnProperty::checkInvariant(
    JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetArrayIteratorPrototype());

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return HasNoReturnName(cx, proto);
}

/* static */
bool js::IteratorPrototypeHasNoReturnProperty::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetIteratorPrototype());

  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return HasNoReturnName(cx, proto);
}

/* static */
bool js::ArrayIteratorPrototypeHasIteratorProto::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetArrayIteratorPrototype());
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  RootedObject iterProto(cx, cx->global()->maybeGetIteratorPrototype());
  if (!iterProto) {
    MOZ_CRASH("Can we have the array iter proto without the iterator proto?");
    return true;
  }

  return proto->staticPrototype() == iterProto;
}

/* static */
bool js::IteratorPrototypeHasObjectProto::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, cx->global()->maybeGetIteratorPrototype());
  if (!proto) {
    // Invariant holds if there is no array iterator proto.
    return true;
  }

  return proto->staticPrototype() == &cx->global()->getObjectPrototype();
}

/* static */
bool js::ObjectPrototypeHasNoReturnProperty::checkInvariant(JSContext* cx) {
  RootedObject proto(cx, &cx->global()->getObjectPrototype());
  return HasNoReturnName(cx, proto);
}

void js::OptimizeArraySpeciesFuse::popFuse(JSContext* cx,
                                           RealmFuses& realmFuses) {
  InvalidatingRealmFuse::popFuse(cx, realmFuses);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(),
                               JSUseCounter::OPTIMIZE_ARRAY_SPECIES_FUSE);
}

static bool SpeciesFuseCheckInvariant(JSContext* cx, JSProtoKey protoKey,
                                      PropertyName* selfHostedSpeciesAccessor) {
  // Prototype must be initialized.
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(protoKey);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }

  auto* ctor = cx->global()->maybeGetConstructor<NativeObject>(protoKey);
  MOZ_ASSERT(ctor);

  // Ensure the prototype's `constructor` slot is the original constructor.
  if (!ObjectHasDataPropertyValue(proto, NameToId(cx->names().constructor),
                                  ObjectValue(*ctor))) {
    return false;
  }

  // Ensure constructor's `@@species` slot is the original species getter.
  PropertyKey speciesKey = PropertyKey::Symbol(cx->wellKnownSymbols().species);
  return ObjectHasGetterFunction(ctor, speciesKey, selfHostedSpeciesAccessor);
}

bool js::OptimizeArraySpeciesFuse::checkInvariant(JSContext* cx) {
  return SpeciesFuseCheckInvariant(cx, JSProto_Array,
                                   cx->names().dollar_ArraySpecies_);
}

bool js::OptimizeRegExpSpeciesFuse::checkInvariant(JSContext* cx) {
  return SpeciesFuseCheckInvariant(cx, JSProto_RegExp,
                                   cx->names().dollar_RegExpSpecies_);
}

bool js::OptimizeArrayBufferSpeciesFuse::checkInvariant(JSContext* cx) {
  return SpeciesFuseCheckInvariant(cx, JSProto_ArrayBuffer,
                                   cx->names().dollar_ArrayBufferSpecies_);
}

bool js::OptimizeSharedArrayBufferSpeciesFuse::checkInvariant(JSContext* cx) {
  return SpeciesFuseCheckInvariant(
      cx, JSProto_SharedArrayBuffer,
      cx->names().dollar_SharedArrayBufferSpecies_);
}

bool js::OptimizeTypedArraySpeciesFuse::checkInvariant(JSContext* cx) {
  // Check `constructor` and `@@species` on %TypedArray%.
  if (!SpeciesFuseCheckInvariant(cx, JSProto_TypedArray,
                                 cx->names().dollar_TypedArraySpecies_)) {
    return false;
  }

  auto typedArrayProtoKeys = std::array{
#define PROTO_KEY(_, T, N) JSProto_##N##Array,
      JS_FOR_EACH_TYPED_ARRAY(PROTO_KEY)
#undef PROTO_KEY
  };

  auto* typedArrayProto =
      cx->global()->maybeGetPrototype<NativeObject>(JSProto_TypedArray);

  // Check all concrete TypedArray prototypes.
  for (auto protoKey : typedArrayProtoKeys) {
    // Prototype must be initialized.
    auto* proto = cx->global()->maybeGetPrototype<NativeObject>(protoKey);
    if (!proto) {
      // No proto, invariant still holds
      continue;
    }
    MOZ_ASSERT(typedArrayProto,
               "%TypedArray%.prototype must be initialized when TypedArray "
               "subclass is initialized");

    // Ensure the prototype's prototype is %TypedArray%.prototype.
    if (proto->staticPrototype() != typedArrayProto) {
      return false;
    }

    auto* ctor = cx->global()->maybeGetConstructor<NativeObject>(protoKey);
    MOZ_ASSERT(ctor);

    // Ensure the prototype's `constructor` slot is the original constructor.
    if (!ObjectHasDataPropertyValue(proto, NameToId(cx->names().constructor),
                                    ObjectValue(*ctor))) {
      return false;
    }
  }

  auto* typedArrayCtor =
      cx->global()->maybeGetConstructor<NativeObject>(JSProto_TypedArray);

  // Check all concrete TypedArray constructors.
  for (auto protoKey : typedArrayProtoKeys) {
    // Constructor must be initialized.
    NativeObject* ctor =
        cx->global()->maybeGetConstructor<NativeObject>(protoKey);
    if (!ctor) {
      // No ctor, invariant still holds
      continue;
    }
    MOZ_ASSERT(typedArrayCtor,
               "%TypedArray% must be initialized when TypedArray subclass is "
               "initialized");

    // Ensure the constructor's prototype is %TypedArray%.
    if (ctor->staticPrototype() != typedArrayCtor) {
      return false;
    }

    // Ensure the constructor has no own @@species property.
    auto speciesKey = PropertyKey::Symbol(cx->wellKnownSymbols().species);
    if (ctor->lookupPure(speciesKey).isSome()) {
      return false;
    }
  }

  return true;
}

void js::OptimizePromiseLookupFuse::popFuse(JSContext* cx,
                                            RealmFuses& realmFuses) {
  RealmFuse::popFuse(cx, realmFuses);
  MOZ_ASSERT(cx->global());
  cx->runtime()->setUseCounter(cx->global(),
                               JSUseCounter::OPTIMIZE_PROMISE_LOOKUP_FUSE);
}

bool js::OptimizePromiseLookupFuse::checkInvariant(JSContext* cx) {
  // Prototype must be Promise.prototype.
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_Promise);
  if (!proto) {
    // No proto, invariant still holds.
    return true;
  }

  auto* ctor = cx->global()->maybeGetConstructor<NativeObject>(JSProto_Promise);
  MOZ_ASSERT(ctor);

  // Ensure Promise.prototype's `constructor` slot is the `Promise` constructor.
  if (!ObjectHasDataPropertyValue(proto, NameToId(cx->names().constructor),
                                  ObjectValue(*ctor))) {
    return false;
  }

  // Ensure Promise.prototype's `then` slot is the original function.
  if (!ObjectHasDataPropertyFunction(proto, NameToId(cx->names().then),
                                     js::Promise_then)) {
    return false;
  }

  // Ensure Promise's `@@species` slot is the original getter.
  PropertyKey speciesKey = PropertyKey::Symbol(cx->wellKnownSymbols().species);
  if (!ObjectHasGetterFunction(ctor, speciesKey, js::Promise_static_species)) {
    return false;
  }

  // Ensure Promise's `resolve` slot is the original function.
  if (!ObjectHasDataPropertyFunction(ctor, NameToId(cx->names().resolve),
                                     js::Promise_static_resolve)) {
    return false;
  }

  return true;
}

bool js::OptimizeRegExpPrototypeFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_RegExp);
  if (!proto) {
    // No proto, invariant still holds.
    return true;
  }

  // Check getters are unchanged.
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().flags),
                               cx->names().dollar_RegExpFlagsGetter_)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().global),
                               regexp_global)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().hasIndices),
                               regexp_hasIndices)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().ignoreCase),
                               regexp_ignoreCase)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().multiline),
                               regexp_multiline)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().sticky),
                               regexp_sticky)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().unicode),
                               regexp_unicode)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().unicodeSets),
                               regexp_unicodeSets)) {
    return false;
  }
  if (!ObjectHasGetterFunction(proto, NameToId(cx->names().dotAll),
                               regexp_dotAll)) {
    return false;
  }

  // Check data properties are unchanged.
  if (!ObjectHasDataPropertyFunction(proto, NameToId(cx->names().exec),
                                     cx->names().RegExp_prototype_Exec)) {
    return false;
  }
  if (!ObjectHasDataPropertyFunction(
          proto, PropertyKey::Symbol(cx->wellKnownSymbols().match),
          cx->names().RegExpMatch)) {
    return false;
  }
  if (!ObjectHasDataPropertyFunction(
          proto, PropertyKey::Symbol(cx->wellKnownSymbols().matchAll),
          cx->names().RegExpMatchAll)) {
    return false;
  }
  if (!ObjectHasDataPropertyFunction(
          proto, PropertyKey::Symbol(cx->wellKnownSymbols().replace),
          cx->names().RegExpReplace)) {
    return false;
  }
  if (!ObjectHasDataPropertyFunction(
          proto, PropertyKey::Symbol(cx->wellKnownSymbols().search),
          cx->names().RegExpSearch)) {
    return false;
  }
  if (!ObjectHasDataPropertyFunction(
          proto, PropertyKey::Symbol(cx->wellKnownSymbols().split),
          cx->names().RegExpSplit)) {
    return false;
  }

  return true;
}

bool js::OptimizeMapObjectIteratorFuse::checkInvariant(JSContext* cx) {
  // Ensure Map.prototype's @@iterator slot is unchanged.
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_Map);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  PropertyKey iteratorKey =
      PropertyKey::Symbol(cx->wellKnownSymbols().iterator);
  if (!ObjectHasDataPropertyFunction(proto, iteratorKey, MapObject::entries)) {
    return false;
  }

  // Ensure %MapIteratorPrototype%'s `next` slot is unchanged.
  auto* iterProto = cx->global()->maybeBuiltinProto(
      GlobalObject::ProtoKind::MapIteratorProto);
  if (!iterProto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(&iterProto->as<NativeObject>(),
                                       NameToId(cx->names().next),
                                       cx->names().MapIteratorNext);
}

bool js::OptimizeSetObjectIteratorFuse::checkInvariant(JSContext* cx) {
  // Ensure Set.prototype's @@iterator slot is unchanged.
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_Set);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  PropertyKey iteratorKey =
      PropertyKey::Symbol(cx->wellKnownSymbols().iterator);
  if (!ObjectHasDataPropertyFunction(proto, iteratorKey, SetObject::values)) {
    return false;
  }

  // Ensure %SetIteratorPrototype%'s `next` slot is unchanged.
  auto* iterProto = cx->global()->maybeBuiltinProto(
      GlobalObject::ProtoKind::SetIteratorProto);
  if (!iterProto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(&iterProto->as<NativeObject>(),
                                       NameToId(cx->names().next),
                                       cx->names().SetIteratorNext);
}

bool js::OptimizeMapPrototypeSetFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_Map);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(proto, NameToId(cx->names().set),
                                       MapObject::set);
}

bool js::OptimizeSetPrototypeAddFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_Set);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(proto, NameToId(cx->names().add),
                                       SetObject::add);
}

bool js::OptimizeWeakMapPrototypeSetFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_WeakMap);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(proto, NameToId(cx->names().set),
                                       WeakMapObject::set);
}

bool js::OptimizeWeakSetPrototypeAddFuse::checkInvariant(JSContext* cx) {
  auto* proto = cx->global()->maybeGetPrototype<NativeObject>(JSProto_WeakSet);
  if (!proto) {
    // No proto, invariant still holds
    return true;
  }
  return ObjectHasDataPropertyFunction(proto, NameToId(cx->names().add),
                                       WeakSetObject::add);
}

static bool ProtoHasNoIndexedProps(NativeObject* proto) {
  return proto->getDenseInitializedLength() == 0 &&
         proto->getDenseCapacity() == 0 && !proto->isIndexed();
}

bool js::ObjectProtoHasNoIndexedPropsFuse::checkInvariant(JSContext* cx) {
  auto* objectProto = &cx->global()->getObjectPrototype().as<NativeObject>();
  MOZ_ASSERT(objectProto->staticPrototype() == nullptr,
             "Object.prototype has an immutable null [[Prototype]]");
  return ProtoHasNoIndexedProps(objectProto);
}

void js::ObjectProtoHasNoIndexedPropsFuse::popFuse(JSContext* cx,
                                                   RealmFuses& realmFuses) {
  InvalidatingRealmFuse::popFuse(cx, realmFuses);
  realmFuses.arrayProtoHasNoIndexedPropsFuse.popFuse(cx, realmFuses);
  realmFuses.stringProtoHasNoIndexedPropsFuse.popFuse(cx, realmFuses);
}

bool js::ArrayProtoHasNoIndexedPropsFuse::checkInvariant(JSContext* cx) {
  NativeObject* arrayProto = cx->global()->maybeGetArrayPrototype();
  if (!arrayProto) {
    return true;
  }
  auto* objectProto = &cx->global()->getObjectPrototype().as<NativeObject>();
  return ProtoHasNoIndexedProps(arrayProto) &&
         arrayProto->staticPrototype() == objectProto &&
         cx->realm()->realmFuses.objectProtoHasNoIndexedPropsFuse.intact();
}

bool js::StringProtoHasNoIndexedPropsFuse::checkInvariant(JSContext* cx) {
  auto* stringProto =
      cx->global()->maybeGetPrototype<NativeObject>(JSProto_String);
  if (!stringProto) {
    return true;
  }
  auto* objectProto = &cx->global()->getObjectPrototype().as<NativeObject>();
  return ProtoHasNoIndexedProps(stringProto) &&
         stringProto->staticPrototype() == objectProto &&
         cx->realm()->realmFuses.objectProtoHasNoIndexedPropsFuse.intact();
}

mozilla::Maybe<RealmFuses::FuseIndex> js::MaybeGetHasNoIndexedPropsFuseIndex(
    GlobalObject* global, JSObject* proto) {
  if (!proto) {
    return mozilla::Nothing();
  }
  if (proto == global->maybeGetPrototype(JSProto_Object)) {
    return mozilla::Some(
        RealmFuses::FuseIndex::ObjectProtoHasNoIndexedPropsFuse);
  }
  if (proto == global->maybeGetArrayPrototype()) {
    return mozilla::Some(
        RealmFuses::FuseIndex::ArrayProtoHasNoIndexedPropsFuse);
  }
  if (proto == global->maybeGetPrototype(JSProto_String)) {
    return mozilla::Some(
        RealmFuses::FuseIndex::StringProtoHasNoIndexedPropsFuse);
  }
  return mozilla::Nothing();
}

InvalidatingRealmFuse* js::MaybeGetHasNoIndexedPropsFuse(NativeObject* proto) {
  auto index = MaybeGetHasNoIndexedPropsFuseIndex(&proto->global(), proto);
  if (index.isNothing()) {
    return nullptr;
  }
  MOZ_ASSERT(RealmFuses::isInvalidatingFuse(*index));
  GuardFuse* fuse = proto->realm()->realmFuses.getFuseByIndex(*index);
  return static_cast<InvalidatingRealmFuse*>(fuse);
}
