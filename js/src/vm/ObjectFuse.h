/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_ObjectFuse_h
#define vm_ObjectFuse_h

#include "mozilla/MemoryReporting.h"

#include <bit>

#include "gc/Barrier.h"
#include "jit/InvalidationScriptSet.h"
#include "jit/JitOptions.h"
#include "js/SweepingAPI.h"
#include "vm/PropertyInfo.h"

// [SMDOC] ObjectFuse
//
// ObjectFuse contains extra data associated with a single JSObject that the
// JITs can use to optimize operations on this object.
//
// An object's ObjectFuse is allocated lazily the first time it's needed by the
// JITs and freed when the object dies.
//
// ObjectFuse is currently used to track which properties are constant (unlikely
// to be mutated) so that IC stubs can guard on this and return the constant
// property value. In Warp, the guard becomes an invalidation dependency and the
// property value is a constant in the MIR graph, enabling additional compiler
// optimizations. ObjectFuse is currently used for the global object, the
// global lexical environment, prototypes, and certain builtin constructors.
//
// Each ObjectFuse has a generation counter. When the generation is bumped, IC
// guards will fail and dependent Ion scripts that are affected by the operation
// are invalidated. The generation changes when:
//
// * Removing a tracked property.
// * Shadowing a tracked global object property on the lexical environment.
// * Shadowing a tracked property on a different prototype object (related to
//   shape teleporting).
// * Mutating the prototype of a prototype object (also related to shape
//   teleporting).
// * Swapping the object with a different object.
//
// The property state information should only be accessed by the JITs after
// checking the generation still matches.

namespace js {

class NativeObject;

// A generation counter that becomes invalid (we no longer optimize based on it)
// when it reaches a maximum value (currently UINT32_MAX).
class SaturatedGenerationCounter {
  uint32_t value_ = 0;
  static constexpr uint32_t InvalidValue = UINT32_MAX;

 public:
  bool isValid() const { return value_ != InvalidValue; }
  bool check(uint32_t v) const {
    MOZ_RELEASE_ASSERT(v != InvalidValue);
    return value_ == v;
  }
  void bump() {
    if (isValid()) {
      value_++;
    }
  }
  uint32_t value() const {
    MOZ_RELEASE_ASSERT(isValid());
    return value_;
  }
  uint32_t valueMaybeInvalid() const { return value_; }
};

class ObjectFuse {
  // State of a single property. This is encoded in two bits in
  // |propertyStateBits_|.
  //
  // Note that Untracked and Constant are different states mainly to ensure
  // global variables (property x for |var x = y;| in the global scope) can be
  // marked Constant. In this case the property is initially defined with value
  // |undefined| before bytecode assigns the actual value.
  enum class PropertyState {
    // Initial state. The JIT hasn't optimized this property as a constant.
    Untracked = 0,

    // This property is assumed to be constant. JIT code may depend on this
    // assumption.
    Constant = 1,

    // This property is no longer tracked as a constant because it was mutated
    // after being marked Constant.
    //
    // Note: IC guards rely on the fact that this value is the only enum value
    // value that has the upper bit set. See getConstantPropertyGuardData.
    NotConstant = 2,
  };
  static constexpr size_t NumPropsPerWord = 16;
  static constexpr size_t NumBitsPerProp = 2;
  static constexpr size_t PropBitsMask = BitMask(NumBitsPerProp);
  static_assert(NumPropsPerWord * NumBitsPerProp ==
                CHAR_BIT * sizeof(uint32_t));

  // Bit vector with two bits per property. Words are allocated lazily when a
  // property is marked Constant/NotConstant.
  UniquePtr<uint32_t[], JS::FreePolicy> propertyStateBits_;

  // Length of the propertiesBits_ array in words.
  uint32_t propertyStateLength_ = 0;

  // This field is set to 1 when a property is marked NotConstant and when the
  // generation counter is bumped. IC code can use a fast path based on this
  // field.
  uint32_t invalidatedConstantProperty_ = 0;

  // Generation counter of this ObjectFuse. JIT guards should only access the
  // property state bits when the generation still matches.
  SaturatedGenerationCounter generation_{};

  // This maps a uint32_t propertySlot to the Ion compilations that depend on
  // this property being a constant.
  using DepMap = GCHashMap<uint32_t, js::jit::DependentIonScriptSet,
                           DefaultHasher<uint32_t>, SystemAllocPolicy>;
  DepMap dependencies_;

  [[nodiscard]] bool ensurePropertyStateLength(uint32_t length);

  void invalidateDependentIonScriptsForProperty(JSContext* cx,
                                                PropertyInfo prop,
                                                const char* reason);
  void invalidateAllDependentIonScripts(JSContext* cx, const char* reason);

  static constexpr uint32_t propertyStateShift(uint32_t propSlot) {
    return (propSlot % NumPropsPerWord) * NumBitsPerProp;
  }
  PropertyState getPropertyState(uint32_t propSlot) const {
    uint32_t index = propSlot / NumPropsPerWord;
    if (index >= propertyStateLength_) {
      return PropertyState::Untracked;
    }
    uint32_t shift = propertyStateShift(propSlot);
    uint32_t bits = (propertyStateBits_[index] >> shift) & PropBitsMask;
    MOZ_ASSERT(bits <= uint32_t(PropertyState::NotConstant));
    return PropertyState(bits);
  }
  PropertyState getPropertyState(PropertyInfo prop) const {
    return getPropertyState(prop.slot());
  }
  void setPropertyState(PropertyInfo prop, PropertyState state) {
    uint32_t slot = prop.slot();
    uint32_t index = slot / NumPropsPerWord;
    MOZ_ASSERT(index < propertyStateLength_);
    uint32_t shift = propertyStateShift(slot);
    propertyStateBits_[index] &= ~(PropBitsMask << shift);
    propertyStateBits_[index] |= uint32_t(state) << shift;
  }

  bool isUntrackedProperty(PropertyInfo prop) const {
    return getPropertyState(prop) == PropertyState::Untracked;
  }
  bool isConstantProperty(PropertyInfo prop) const {
    return getPropertyState(prop) == PropertyState::Constant;
  }

  [[nodiscard]] bool markPropertyConstant(PropertyInfo prop);

  void bumpGeneration() {
    invalidatedConstantProperty_ = 1;
    generation_.bump();
  }

 public:
  uint32_t generationMaybeInvalid() const {
    return generation_.valueMaybeInvalid();
  }
  bool hasInvalidatedConstantProperty() const {
    return invalidatedConstantProperty_;
  }

  bool tryOptimizeConstantProperty(PropertyInfo prop);

  // Data needed for guards in IC code. We use a bitmask to check the
  // PropertyState's upper bit isn't set.
  struct GuardData {
    uint32_t generation;
    uint32_t propIndex;
    uint32_t propMask;
  };
  GuardData getConstantPropertyGuardData(PropertyInfo prop) const {
    MOZ_ASSERT(isConstantProperty(prop));

    GuardData data;
    data.generation = generation_.value();
    data.propIndex = prop.slot() / NumPropsPerWord;
    static_assert(size_t(PropertyState::NotConstant) == 2);
    data.propMask = uint32_t(0b10) << propertyStateShift(prop.slot());

    // Make sure propertySlotFromIndexAndMask will return the original slot
    // number.
    MOZ_ASSERT(propertySlotFromIndexAndMask(data.propIndex, data.propMask) ==
               prop.slot());

    return data;
  }

  // The inverse of getConstantPropertyGuardData: it computes the property slot
  // from the index and mask pair stored in an IC stub.
  static uint32_t propertySlotFromIndexAndMask(uint32_t propIndex,
                                               uint32_t propMask) {
    MOZ_ASSERT(std::has_single_bit(propMask));
    uint32_t slot = propIndex * NumPropsPerWord;
    slot += std::countr_zero(propMask) / NumBitsPerProp;
    return slot;
  }

  // We can only optimize SetProp operations for non-constant properties.
  bool canOptimizeSetSlot(PropertyInfo prop) const {
    return getPropertyState(prop) == PropertyState::NotConstant;
  }

  void handlePropertyValueChange(JSContext* cx, PropertyInfo prop);
  void handlePropertyRemove(JSContext* cx, PropertyInfo prop,
                            bool* wasTrackedProp);
  void handleTeleportingShadowedProperty(JSContext* cx, PropertyInfo prop);
  void handleTeleportingProtoMutation(JSContext* cx);
  void handleShadowedGlobalProperty(JSContext* cx, PropertyInfo prop);

  bool addDependency(uint32_t propSlot, const jit::IonScriptKey& ionScript);

  bool checkPropertyIsConstant(uint32_t generation, uint32_t propSlot) const {
    if (!generation_.check(generation)) {
      return false;
    }
    PropertyState state = getPropertyState(propSlot);
    if (state == PropertyState::NotConstant) {
      MOZ_ASSERT(invalidatedConstantProperty_);
      return false;
    }
    MOZ_ASSERT(state == PropertyState::Constant);
    return true;
  }

  const char* getPropertyStateString(PropertyInfo prop) const;

  size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  // We should sweep ObjectFuseMap entries based on the key (the object) but
  // never based on the ObjectFuse. We do need to trace weak pointers in the
  // DependentIonScriptSets.
  bool needsSweep(JSTracer* trc) const { return false; }
  bool traceWeak(JSTracer* trc) {
    dependencies_.traceWeak(trc);
    return true;
  }

  static constexpr size_t offsetOfInvalidatedConstantProperty() {
    return offsetof(ObjectFuse, invalidatedConstantProperty_);
  }
  static constexpr size_t offsetOfGeneration() {
    return offsetof(ObjectFuse, generation_);
  }
  static constexpr size_t offsetOfPropertyStateBits() {
    return offsetof(ObjectFuse, propertyStateBits_);
  }
};

class ObjectFuseMap {
  using Map =
      GCHashMap<WeakHeapPtr<JSObject*>, UniquePtr<ObjectFuse>,
                StableCellHasher<WeakHeapPtr<JSObject*>>, SystemAllocPolicy>;
  JS::WeakCache<Map> objectFuses_;
#ifdef DEBUG
  JS::Zone* zone_;
#endif

 public:
  explicit ObjectFuseMap(JS::Zone* zone) : objectFuses_(zone) {
#ifdef DEBUG
    zone_ = zone;
#endif
  }

  ObjectFuse* getOrCreate(JSContext* cx, NativeObject* obj);
  ObjectFuse* get(NativeObject* obj);

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

// ObjectFuses can have some performance overhead due to the Watchtower code for
// property changes, so we only use them if we can take advantage of object
// fuses with IC stubs. Note that we don't check this for the |addObjectFuse|
// testing function.
inline bool ShouldUseObjectFuses() {
  return jit::IsBaselineInterpreterEnabled();
}

}  // namespace js

#endif  // vm_ObjectFuse_h
