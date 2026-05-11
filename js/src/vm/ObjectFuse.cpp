/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/ObjectFuse.h"

#include "gc/Barrier.h"
#include "gc/StableCellHasher.h"
#include "jit/InvalidationScriptSet.h"
#include "js/SweepingAPI.h"
#include "vm/JSScript.h"
#include "vm/NativeObject.h"
#include "vm/PropertyInfo.h"

#include "gc/StableCellHasher-inl.h"
#include "vm/JSScript-inl.h"

using namespace js;

bool ObjectFuse::ensurePropertyStateLength(uint32_t length) {
  MOZ_ASSERT(length > 0);

  if (length <= propertyStateLength_) {
    MOZ_ASSERT(propertyStateBits_);
    return true;
  }

  if (!propertyStateBits_) {
    propertyStateBits_.reset(js_pod_malloc<uint32_t>(length));
    if (!propertyStateBits_) {
      return false;
    }
  } else {
    uint32_t* bitsNew = js_pod_realloc<uint32_t>(propertyStateBits_.get(),
                                                 propertyStateLength_, length);
    if (!bitsNew) {
      return false;
    }
    (void)propertyStateBits_.release();
    propertyStateBits_.reset(bitsNew);
  }

  // Initialize the new words to 0.
  static_assert(uint32_t(PropertyState::Untracked) == 0);
  std::uninitialized_fill_n(propertyStateBits_.get() + propertyStateLength_,
                            length - propertyStateLength_, 0);
  propertyStateLength_ = length;
  return true;
}

bool ObjectFuse::addDependency(uint32_t propSlot,
                               const jit::IonScriptKey& ionScript) {
  MOZ_ASSERT(getPropertyState(propSlot) == PropertyState::Constant);

  auto p = dependencies_.lookupForAdd(propSlot);
  if (!p) {
    if (!dependencies_.add(p, propSlot, jit::DependentIonScriptSet())) {
      return false;
    }
  }
  return p->value().addToSet(ionScript);
}

void ObjectFuse::invalidateDependentIonScriptsForProperty(JSContext* cx,
                                                          PropertyInfo prop,
                                                          const char* reason) {
  if (auto p = dependencies_.lookup(prop.slot())) {
    p->value().invalidateAndClear(cx, reason);
    dependencies_.remove(p);
  }
}

void ObjectFuse::invalidateAllDependentIonScripts(JSContext* cx,
                                                  const char* reason) {
  for (auto iter = dependencies_.iter(); !iter.done(); iter.next()) {
    iter.get().value().invalidateAndClear(cx, reason);
  }
  dependencies_.clear();
}

bool ObjectFuse::markPropertyConstant(PropertyInfo prop) {
  MOZ_ASSERT(getPropertyState(prop) == PropertyState::Untracked);
  uint32_t index = prop.slot() / NumPropsPerWord;
  if (!ensurePropertyStateLength(index + 1)) {
    return false;
  }
  setPropertyState(prop, PropertyState::Constant);
  return true;
}

bool ObjectFuse::tryOptimizeConstantProperty(PropertyInfo prop) {
  if (MOZ_UNLIKELY(!generation_.isValid())) {
    return false;
  }
  PropertyState state = getPropertyState(prop);
  switch (state) {
    case PropertyState::Untracked:
      if (!markPropertyConstant(prop)) {
        return false;
      }
      MOZ_ASSERT(isConstantProperty(prop));
      return true;
    case PropertyState::Constant:
      return true;
    case PropertyState::NotConstant:
      return false;
  }
  MOZ_CRASH("Unreachable");
}

void ObjectFuse::handlePropertyValueChange(JSContext* cx, PropertyInfo prop) {
  // Custom data properties aren't optimized with object fuses.
  if (!prop.hasSlot()) {
    return;
  }

  PropertyState state = getPropertyState(prop);
  switch (state) {
    case PropertyState::Untracked:
      // Mark the property as Constant. IC code for SetProp operations relies on
      // properties getting marked NotConstant after a few sets, because we can
      // only optimize stores to NotConstant properties. We can ignore OOM here.
      (void)markPropertyConstant(prop);
      break;
    case PropertyState::Constant:
      invalidatedConstantProperty_ = 1;
      setPropertyState(prop, PropertyState::NotConstant);
      invalidateDependentIonScriptsForProperty(cx, prop,
                                               "changed constant property");
      break;
    case PropertyState::NotConstant:
      break;
  }
}

void ObjectFuse::handlePropertyRemove(JSContext* cx, PropertyInfo prop,
                                      bool* wasTrackedProp) {
  if (!prop.hasSlot() || isUntrackedProperty(prop)) {
    MOZ_ASSERT(!*wasTrackedProp);
    return;
  }

  *wasTrackedProp = true;
  bumpGeneration();
  invalidateDependentIonScriptsForProperty(cx, prop, "removed property");

  // Ensure a new property with this slot number will have the correct initial
  // state.
  setPropertyState(prop, PropertyState::Untracked);
}

void ObjectFuse::handleTeleportingShadowedProperty(JSContext* cx,
                                                   PropertyInfo prop) {
  if (!prop.hasSlot() || isUntrackedProperty(prop)) {
    return;
  }
  bumpGeneration();
  invalidateDependentIonScriptsForProperty(cx, prop,
                                           "teleporting shadowed property");
}

void ObjectFuse::handleTeleportingProtoMutation(JSContext* cx) {
  bumpGeneration();
  invalidateAllDependentIonScripts(cx, "proto mutation");
}

void ObjectFuse::handleShadowedGlobalProperty(JSContext* cx,
                                              PropertyInfo prop) {
  if (isUntrackedProperty(prop)) {
    return;
  }
  bumpGeneration();
  invalidateDependentIonScriptsForProperty(cx, prop,
                                           "shadowed global property");
}

const char* ObjectFuse::getPropertyStateString(PropertyInfo prop) const {
  PropertyState state = getPropertyState(prop);
  switch (state) {
    case PropertyState::Untracked:
      return "Untracked";
    case PropertyState::Constant:
      return "Constant";
    case PropertyState::NotConstant:
      return "NotConstant";
  }
  MOZ_CRASH("Unreachable");
}

size_t ObjectFuse::sizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t result = mallocSizeOf(this);
  if (propertyStateBits_) {
    result += mallocSizeOf(propertyStateBits_.get());
  }
  result += dependencies_.shallowSizeOfExcludingThis(mallocSizeOf);
  for (auto iter = dependencies_.iter(); !iter.done(); iter.next()) {
    result += iter.get().value().sizeOfExcludingThis(mallocSizeOf);
  }
  return result;
}

ObjectFuse* ObjectFuseMap::getOrCreate(JSContext* cx, NativeObject* obj) {
  MOZ_ASSERT(obj->hasObjectFuse());
  MOZ_ASSERT(obj->zone() == zone_);
  auto p = objectFuses_.lookupForAdd(obj);
  if (!p) {
    auto fuse = MakeUnique<ObjectFuse>();
    if (!fuse || !objectFuses_.add(p, obj, std::move(fuse))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
  }
  return p->value().get();
}

ObjectFuse* ObjectFuseMap::get(NativeObject* obj) {
  MOZ_ASSERT(obj->hasObjectFuse());
  auto p = objectFuses_.lookup(obj);
  return p ? p->value().get() : nullptr;
}

size_t ObjectFuseMap::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t result = objectFuses_.sizeOfExcludingThis(mallocSizeOf);
  for (auto iter = objectFuses_.iter(); !iter.done(); iter.next()) {
    result += iter.get().value()->sizeOfIncludingThis(mallocSizeOf);
  }
  return result;
}
