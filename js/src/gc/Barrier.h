/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_Barrier_h
#define gc_Barrier_h

#include <type_traits>  // std::true_type

#include "NamespaceImports.h"

#include "gc/Cell.h"
#include "gc/GCContext.h"
#include "gc/StoreBuffer.h"
#include "js/ComparisonOperators.h"     // JS::detail::DefineComparisonOps
#include "js/experimental/TypedData.h"  // js::EnableIfABOVType
#include "js/HeapAPI.h"
#include "js/Id.h"
#include "js/RootingAPI.h"
#include "js/Value.h"
#include "util/Poison.h"

/*
 * [SMDOC] GC Barriers
 *
 * Several kinds of barrier are necessary to allow the GC to function correctly.
 * These are triggered by reading or writing to GC pointers in the heap and
 * serve to tell the collector about changes to the graph of reachable GC
 * things.
 *
 * Since it would be awkward to change every write to memory into a function
 * call, this file contains a bunch of C++ classes and templates that use
 * operator overloading to take care of barriers automatically. In most cases,
 * all that's necessary is to replace:
 *
 *     Type* field;
 *
 * with something like:
 *
 *     GCPtr<Type> field;
 *
 * All heap-based GC pointers and tagged pointer fields should use one of these
 * classes, except in a couple of exceptional cases.
 *
 * For cases where GC pointers are not stored in class fields (e.g. containers
 * with dynamically sized memory) some functions are provided to write to an
 * address while triggering the appropriate GC barriers.
 *
 * These classes are designed to be used by the internals of the JS engine.
 * Barriers designed to be used externally are provided in js/RootingAPI.h.
 *
 * Overview
 * ========
 *
 * This file implements the following concrete classes:
 *
 * HeapPtr       General wrapper for heap-based pointers that provides pre- and
 *               post-write barriers. Conservative and may trigger more barriers
 *               than necessary. Intended for fields in non-GC things.
 *
 * GCPtr         An optimisation of HeapPtr for use in objects which are only
 *               destroyed by GC finalization (this rules out use in Vector,
 *               for example). Intended for fields inside GC things.
 *
 * PreBarriered  Provides a pre-barrier but not a post-barrier. Necessary when
 *               generational GC updates are handled manually, e.g. for hash
 *               table keys that don't use StableCellHasher.
 *
 * HeapSlot      Provides pre and post-barriers, optimised for use in JSObject
 *               slots and elements.
 *
 * WeakHeapPtr   Provides read and post-write barriers, for use with weak
 *               pointers.
 *
 * UnsafeBarePtr Provides no barriers. Don't add new uses of this, or only if
 *               you really know what you are doing.
 *
 * The following classes are implemented in js/RootingAPI.h (in the JS
 * namespace):
 *
 * Heap          General wrapper for external clients. Like HeapPtr but also
 *               handles cycle collector concerns. Most external clients should
 *               use this.
 *
 * TenuredHeap   Like Heap but doesn't allow nursery pointers. Allows storing
 *               flags in unused lower bits of the pointer.
 *
 * This file implements the following functions:
 *
 * BarrieredInit      Initialize a heap-based pointer with appropriate barriers.
 * BarrieredSet       Update a previously initialized heap-based pointer.
 * BarrieredCopyRange Copy a non-overlapping memory range with barriers.
 * BarrieredMoveRange Copy a possibly-overlapping memory range with barriers.
 *
 * Which class to use?
 * -------------------
 *
 * Answer the following questions to decide which barrier class is right for
 * your use case:
 *
 * Is your code part of the JS engine?
 *   Yes, it's internal =>
 *     Is your pointer weak or strong?
 *       Strong =>
 *         Do you want automatic handling of nursery pointers?
 *           Yes, of course =>
 *             Can your object be destroyed outside of a GC?
 *               Yes => Use HeapPtr<T>
 *               No => Use GCPtr<T> (optimization)
 *           No, I'll do this myself =>
 *             Do you want pre-barriers so incremental marking works?
 *               Yes, of course => Use PreBarriered<T>
 *               No, and I'll fix all the bugs myself => Use UnsafeBarePtr<T>
 *       Weak => Use WeakHeapPtr<T>
 *   No, it's external =>
 *     Can your pointer refer to nursery objects?
 *       Yes => Use JS::Heap<T>
 *       Never => Use JS::Heap::Tenured<T> (optimization)
 *
 * If in doubt, use HeapPtr<T>.
 *
 * Write barriers
 * ==============
 *
 * A write barrier is a mechanism used by incremental or generational GCs to
 * ensure that every value that needs to be marked is marked. In general, the
 * write barrier should be invoked whenever a write can cause the set of things
 * traced through by the GC to change. This includes:
 *
 *   - writes to object properties
 *   - writes to array slots
 *   - writes to fields like JSObject::shape_ that we trace through
 *   - writes to fields in private data
 *   - writes to non-markable fields like JSObject::private that point to
 *     markable data
 *
 * The last category is the trickiest. Even though the private pointer does not
 * point to a GC thing, changing the private pointer may change the set of
 * objects that are traced by the GC. Therefore it needs a write barrier.
 *
 * Every barriered write should have the following form:
 *
 *   <pre-barrier>
 *   obj->field = value; // do the actual write
 *   <post-barrier>
 *
 * The pre-barrier is used for incremental GC and the post-barrier is for
 * generational GC.
 *
 * Pre-write barrier
 * -----------------
 *
 * To understand the pre-barrier, let's consider how incremental GC works. The
 * GC itself is divided into "slices". Between each slice, JS code is allowed to
 * run. Each slice should be short so that the user doesn't notice the
 * interruptions. In our GC, the structure of the slices is as follows:
 *
 * 1. ... JS work, which leads to a request to do GC ...
 * 2. [first GC slice, which performs all root marking and (maybe) more marking]
 * 3. ... more JS work is allowed to run ...
 * 4. [GC mark slice, which runs entirely in
 *    GCRuntime::markUntilBudgetExhausted]
 * 5. ... more JS work ...
 * 6. [GC mark slice, which runs entirely in
 *    GCRuntime::markUntilBudgetExhausted]
 * 7. ... more JS work ...
 * 8. [GC marking finishes; sweeping done non-incrementally; GC is done]
 * 9. ... JS continues uninterrupted now that GC is finishes ...
 *
 * Of course, there may be a different number of slices depending on how much
 * marking is to be done.
 *
 * The danger inherent in this scheme is that the JS code in steps 3, 5, and 7
 * might change the heap in a way that causes the GC to collect an object that
 * is actually reachable. The write barrier prevents this from happening. We use
 * a variant of incremental GC called "snapshot at the beginning." This approach
 * guarantees the invariant that if an object is reachable in step 2, then we
 * will mark it eventually. The name comes from the idea that we take a
 * theoretical "snapshot" of all reachable objects in step 2; all objects in
 * that snapshot should eventually be marked. (Note that the write barrier
 * verifier code takes an actual snapshot.)
 *
 * The basic correctness invariant of a snapshot-at-the-beginning collector is
 * that any object reachable at the end of the GC (step 9) must either:
 *   (1) have been reachable at the beginning (step 2) and thus in the snapshot
 *   (2) or must have been newly allocated, in steps 3, 5, or 7.
 * To deal with case (2), any objects allocated during an incremental GC are
 * automatically marked black.
 *
 * This strategy is actually somewhat conservative: if an object becomes
 * unreachable between steps 2 and 8, it would be safe to collect it. We won't,
 * mainly for simplicity. (Also, note that the snapshot is entirely
 * theoretical. We don't actually do anything special in step 2 that we wouldn't
 * do in a non-incremental GC.
 *
 * It's the pre-barrier's job to maintain the snapshot invariant. Consider the
 * write "obj->field = value". Let the prior value of obj->field be
 * value0. Since it's possible that value0 may have been what obj->field
 * contained in step 2, when the snapshot was taken, the barrier marks
 * value0. Note that it only does this if we're in the middle of an incremental
 * GC. Since this is rare, the cost of the write barrier is usually just an
 * extra branch.
 *
 * In practice, we implement the pre-barrier differently based on the type of
 * value0. E.g., see JSObject::preWriteBarrier, which is used if obj->field is
 * a JSObject*. It takes value0 as a parameter.
 *
 * Post-write barrier
 * ------------------
 *
 * For generational GC, we want to be able to quickly collect the nursery in a
 * minor collection.  Part of the way this is achieved is to only mark the
 * nursery itself; tenured things, which may form the majority of the heap, are
 * not traced through or marked.  This leads to the problem of what to do about
 * tenured objects that have pointers into the nursery: if such things are not
 * marked, they may be discarded while there are still live objects which
 * reference them. The solution is to maintain information about these pointers,
 * and mark their targets when we start a minor collection.
 *
 * The pointers can be thought of as edges in an object graph, and the set of
 * edges from the tenured generation into the nursery is known as the remembered
 * set. Post barriers are used to track this remembered set.
 *
 * Whenever a slot which could contain such a pointer is written, we check
 * whether the pointed-to thing is in the nursery (if storeBuffer() returns a
 * buffer).  If so we add the cell into the store buffer, which is the
 * collector's representation of the remembered set.  This means that when we
 * come to do a minor collection we can examine the contents of the store buffer
 * and mark any edge targets that are in the nursery.
 *
 * Read barriers
 * =============
 *
 * Weak pointer read barrier
 * -------------------------
 *
 * Weak pointers must have a read barrier to prevent the referent from being
 * collected if it is read after the start of an incremental GC.
 *
 * The problem happens when, during an incremental GC, some code reads a weak
 * pointer and writes it somewhere on the heap that has been marked black in a
 * previous slice. Since the weak pointer will not otherwise be marked and will
 * be swept and finalized in the last slice, this will leave the pointer just
 * written dangling after the GC. To solve this, we immediately mark black all
 * weak pointers that get read between slices so that it is safe to store them
 * in an already marked part of the heap, e.g. in Rooted.
 *
 * Cycle collector read barrier
 * ----------------------------
 *
 * Heap pointers external to the engine may be marked gray. The JS API has an
 * invariant that no gray pointers may be passed, and this maintained by a read
 * barrier that calls ExposeGCThingToActiveJS on such pointers. This is
 * implemented by JS::Heap<T> in js/RootingAPI.h.
 *
 * Implementation Details
 * ======================
 *
 * One additional note: not all object writes need to be pre-barriered. Writes
 * to newly allocated objects do not need a pre-barrier. In these cases, we use
 * the "obj->field.init(value)" method instead of "obj->field = value". We use
 * the init naming idiom in many places to signify that a field is being
 * assigned for the first time.
 *
 * This file implements the following hierarchy of classes:
 *
 * BarrieredBase             provides storage, unbarriered and atomic operations
 *  |  |  |
 *  |  | GCData              for non-pointer data; provides no barriers
 *  |  |
 *  | BarrieredPtrImpl       template class conditionally implementing barriers
 *  |  |  |  |  |
 *  |  |  |  | PreBarriered  provides pre-barriers only
 *  |  |  |  |
 *  |  |  | GCPtr            provides pre- and post-barriers
 *  |  |  |
 *  |  | HeapPtr             provides pre- and post-barriers; is relocatable
 *  |  |                     and deletable for use inside C++ managed memory
 *  |  |
 *  | WeakHeapPtr            provides read barriers only
 *  |
 * HeapSlot                  similar to GCPtr, but tailored to slots storage
 *
 * The implementation of the barrier logic is implemented in the
 * Cell/TenuredCell base classes, which are called via:
 *
 * WriteBarriered<T>::pre
 *  -> InternalBarrierMethods<T*>::preBarrier
 *      -> Cell::preWriteBarrier
 *  -> InternalBarrierMethods<Value>::preBarrier
 *  -> InternalBarrierMethods<jsid>::preBarrier
 *      -> InternalBarrierMethods<T*>::preBarrier
 *          -> Cell::preWriteBarrier
 *
 * GCPtr<T>::post and HeapPtr<T>::post
 *  -> InternalBarrierMethods<T*>::postBarrier
 *      -> gc::PostWriteBarrierImpl
 *  -> InternalBarrierMethods<Value>::postBarrier
 *      -> StoreBuffer::put
 *
 * Barriers for use outside of the JS engine call into the same barrier
 * implementations at InternalBarrierMethods<T>::post via an indirect call to
 * Heap(.+)WriteBarriers.
 *
 * These clases are designed to be used to wrap GC thing pointers or values that
 * act like them (i.e. JS::Value and jsid).  It is possible to use them for
 * other types by supplying the necessary barrier implementations but this
 * is not usually necessary and should be done with caution.
 */

namespace js {

class NativeObject;

namespace gc {

inline void ValueReadBarrier(const Value& v) {
  MOZ_ASSERT(v.isGCThing());
  ReadBarrierImpl(v.toGCThing());
}

inline void ValuePreWriteBarrier(const Value& v) {
  MOZ_ASSERT(v.isGCThing());
  PreWriteBarrierImpl(v.toGCThing());
}

inline void IdPreWriteBarrier(jsid id) {
  MOZ_ASSERT(id.isGCThing());
  PreWriteBarrierImpl(&id.toGCThing()->asTenured());
}

inline void CellPtrPreWriteBarrier(JS::GCCellPtr thing) {
  MOZ_ASSERT(thing);
  PreWriteBarrierImpl(thing.asCell());
}

inline void WasmAnyRefPreWriteBarrier(const wasm::AnyRef& v) {
  MOZ_ASSERT(v.isGCThing());
  PreWriteBarrierImpl(v.toGCThing());
}

}  // namespace gc

#ifdef DEBUG

bool CurrentThreadIsTouchingGrayThings();

bool IsMarkedBlack(JSObject* obj);

#endif

template <typename T, typename Enable = void>
struct InternalBarrierMethods {};

template <typename T>
struct InternalBarrierMethods<T*> {
  static_assert(std::is_base_of_v<gc::Cell, T>, "Expected a GC thing type");

  static bool isMarkable(const T* v) { return v != nullptr; }

  static void preBarrier(T* v) { gc::PreWriteBarrier(v); }

  static void postBarrier(T** vp, T* prev, T* next) {
    gc::PostWriteBarrier(vp, prev, next);
  }

  static void readBarrier(T* v) { gc::ReadBarrier(v); }

#ifdef DEBUG
  static void assertThingIsNotGray(T* v) { return T::assertThingIsNotGray(v); }
#endif
};

template <typename T>
struct AtomicMethods {};

template <typename T>
struct AtomicMethods<T*> {
  static T* atomicGet(T* const* vp) {
    return __atomic_load_n(vp, __ATOMIC_RELAXED);
  }
  static void atomicSet(T** vp, T* v) {
    __atomic_store_n(vp, v, __ATOMIC_RELAXED);
  }
};

template <typename T>
  requires std::integral<T> && (!std::same_as<T, bool>)
struct AtomicMethods<T> {
  static T atomicGet(T const* vp) {
    return __atomic_load_n(vp, __ATOMIC_RELAXED);
  }
  static void atomicSet(T* vp, T v) {
    __atomic_store_n(vp, v, __ATOMIC_RELAXED);
  }
};

namespace gc {

MOZ_ALWAYS_INLINE void ValuePostWriteBarrier(Value* vp, const Value& prev,
                                             const Value& next) {
  MOZ_ASSERT(!CurrentThreadIsOffThreadCompiling());
  MOZ_ASSERT(vp);

  // If the target needs an entry, add it.
  js::gc::StoreBuffer* sb;
  if (next.isGCThing() && (sb = next.toGCThing()->storeBuffer())) {
    // If we know that the prev has already inserted an entry, we can
    // skip doing the lookup to add the new entry. Note that we cannot
    // safely assert the presence of the entry because it may have been
    // added via a different store buffer.
    if (prev.isGCThing() && prev.toGCThing()->storeBuffer()) {
      return;
    }
    sb->putValue(vp);
    return;
  }
  // Remove the prev entry if the new value does not need it.
  if (prev.isGCThing() && (sb = prev.toGCThing()->storeBuffer())) {
    sb->unputValue(vp);
  }
}

}  // namespace gc

template <>
struct InternalBarrierMethods<Value> {
  static bool isMarkable(const Value& v) { return v.isGCThing(); }

  static void preBarrier(const Value& v) {
    if (v.isGCThing()) {
      gc::ValuePreWriteBarrier(v);
    }
  }

  static MOZ_ALWAYS_INLINE void postBarrier(Value* vp, const Value& prev,
                                            const Value& next) {
    gc::ValuePostWriteBarrier(vp, prev, next);
  }

  static void readBarrier(const Value& v) {
    if (v.isGCThing()) {
      gc::ValueReadBarrier(v);
    }
  }

#ifdef DEBUG
  static void assertThingIsNotGray(const Value& v) {
    JS::AssertValueIsNotGray(v);
  }
#endif
};

template <>
struct AtomicMethods<Value> {
#if JS_BITS_PER_WORD == 64
  static Value atomicGet(Value const* vp) { return vp->atomicGet(); }
  static void atomicSet(Value* vp, const Value& v) { vp->atomicSet(v); }
#endif
};

template <>
struct InternalBarrierMethods<jsid> {
  static bool isMarkable(jsid id) { return id.isGCThing(); }
  static void preBarrier(jsid id) {
    if (id.isGCThing()) {
      gc::IdPreWriteBarrier(id);
    }
  }
  static void postBarrier(jsid* idp, jsid prev, jsid next) {}

#ifdef DEBUG
  static void assertThingIsNotGray(jsid id) { JS::AssertIdIsNotGray(id); }
#endif
};

template <>
struct AtomicMethods<jsid> {
  static jsid atomicGet(jsid const* idp) { return idp->atomicGet(); }
  static void atomicSet(jsid* idp, const jsid& id) { idp->atomicSet(id); }
};

// Specialization for JS::ArrayBufferOrView subclasses.
template <typename T>
struct InternalBarrierMethods<T, EnableIfABOVType<T>> {
  using BM = BarrierMethods<T>;

  static bool isMarkable(const T& thing) { return bool(thing); }
  static void preBarrier(const T& thing) {
    gc::PreWriteBarrier(thing.asObjectUnbarriered());
  }
  static void postBarrier(T* tp, const T& prev, const T& next) {
    BM::postWriteBarrier(tp, prev, next);
  }
  static void readBarrier(const T& thing) { BM::readBarrier(thing); }
#ifdef DEBUG
  static void assertThingIsNotGray(const T& thing) {
    JSObject* obj = thing.asObjectUnbarriered();
    if (obj) {
      JS::AssertValueIsNotGray(JS::ObjectValue(*obj));
    }
  }
#endif
};

template <typename T>
static inline void AssertTargetIsNotGray(const T& v) {
#ifdef DEBUG
  if (!CurrentThreadIsTouchingGrayThings()) {
    InternalBarrierMethods<T>::assertThingIsNotGray(v);
  }
#endif
}

// Base class of all barrier types.
//
// This is marked non-memmovable since post barriers added by derived classes
// can add pointers to class instances to the store buffer.
template <typename T>
class MOZ_NON_MEMMOVABLE BarrieredBase {
  // Storage for all barrier classes. |value| must be a GC thing reference
  // type: either a direct pointer to a GC thing or a supported tagged
  // pointer that can reference GC things, such as JS::Value or jsid. Nested
  // barrier types are NOT supported.
  T value;

 protected:
  // BarrieredBase is not directly instantiable.
  explicit constexpr BarrieredBase(const T& v) : value(v) {}
  constexpr BarrieredBase(const BarrieredBase<T>& other) = default;

  // Accessors without barriers, protected by default. Subclasses implement
  // public accessors in terms of these.
  constexpr const T& unbarrieredGet() const { return value; }
  void unbarrieredSet(const T& newValue) { value = newValue; }

#if JS_BITS_PER_WORD == 64
  T unbarrieredAtomicGet() const { return AtomicMethods<T>::atomicGet(&value); }
  void unbarrieredAtomicSet(const T& newValue) {
    AtomicMethods<T>::atomicSet(&value, newValue);
  }
#endif

 public:
  using ElementType = T;

  // Getter used for tracing: no barriers and atomic when concurrent marking is
  // used.
#ifdef JS_GC_CONCURRENT_MARKING
  T getForTracing() const { return unbarrieredAtomicGet(); }
#else
  T getForTracing() const { return unbarrieredGet(); }
#endif

  // Note: this is public because C++ cannot friend to a specific template
  // instantiation. Friending to the generic template leads to a number of
  // unintended consequences, including template resolution ambiguity and a
  // circular dependency with Tracing.h.
  T* unbarrieredAddress() const { return const_cast<T*>(&value); }
};

// Wrapper class for fields accessed by the GC that are not GC edges (as opposed
// to GCPtr, used for fields that are). Has no barriers but supports atomic
// operations.
template <typename T>
class GCData : public BarrieredBase<T> {
  using Base = BarrieredBase<T>;
  using Self = GCData<T>;

 public:
  constexpr GCData() : Base(defaultValue()) {}

  explicit constexpr GCData(const T& value) : Base(value) {}
  Self& operator=(const T& newValue) {
    set(newValue);
    return *this;
  }

  void set(const T& newValue) {
#ifdef JS_GC_CONCURRENT_MARKING
    this->unbarrieredAtomicSet(newValue);
#else
    this->unbarrieredSet(newValue);
#endif
  }

  constexpr T get() const { return this->unbarrieredGet(); }

#if JS_BITS_PER_WORD == 64
  void atomicSet(const T& newValue) { this->unbarrieredAtomicSet(newValue); }
  T atomicGet() const { return this->unbarrieredAtomicGet(); }
#endif

  constexpr operator T() const { return get(); }
  constexpr T operator->() const { return get(); }

  template <typename U>
  Self& operator+=(const U& rhs) {
    set(get() + rhs);
    return *this;
  }
  template <typename U>
  Self& operator-=(const U& rhs) {
    set(get() - rhs);
    return *this;
  }
  template <typename U>
  Self& operator&=(const U& rhs) {
    set(get() & rhs);
    return *this;
  }
  template <typename U>
  Self& operator|=(const U& rhs) {
    set(get() | rhs);
    return *this;
  }

  static T defaultValue() { return JS::SafelyInitialized<T>::create(); }
};

namespace gc {

// Bitfield of options for BarrieredPtrImpl.
enum BarrierOption : uint32_t {
  // No barriers will be applied.
  BarrierOption_None = 0,

  // Pre-write barrier: mark the previous value on write.
  BarrierOption_PreWriteBarrier = Bit(0),

  // Post-write barrier: maintain store buffer entries for tenured to nursery
  // edges.
  BarrierOption_PostWriteBarrier = Bit(1),

  // Read barrier: mark the current value on read.
  BarrierOption_ReadBarrier = Bit(2),

  // The wrapper has GC lifetime. Since it will only be destroyed by the GC no
  // pre-write barrier is necessary on destruction.
  BarrierOption_HasGCLifetime = Bit(3),

  // Writes to the value are performed as relaxed atomic stores.
  BarrierOption_AtomicWrites = Bit(4),
};

// Template class implementing the barriers specified in |barrierOptions| on a
// wrapped value of type |T|.
template <typename T, uint32_t barrierOptions = BarrierOption_None>
class BarrieredPtrImpl
    : public BarrieredBase<T>,
      public WrappedPtrOperations<T, BarrieredPtrImpl<T, barrierOptions>> {
  using Self = BarrieredPtrImpl<T, barrierOptions>;
  using Base = BarrieredBase<T>;

 public:
  BarrieredPtrImpl() : Base(defaultValue()) {}

  explicit BarrieredPtrImpl(const T& value) : Base(value) {
    maybePostWriteBarrier(defaultValue(), value);
  }
  Self& operator=(const T& newValue) {
    set(newValue);
    return *this;
  }

  BarrieredPtrImpl(const Self& other) : Base(other.unbarrieredGet()) {
    maybePostWriteBarrier(defaultValue(), this->unbarrieredGet());
  }
  Self& operator=(const Self& other) {
    // There's no read barrier when copying an edge.
    set(other.unbarrieredGet());
    return *this;
  }

  // Conditionally declare move constructor and assignment operator. These are
  // not declared for wrappers with GC lifetime.
  //
  // This introduces a template parameter |opts| to make the method into a
  // function template, which is required in order to use SFINAE.
  template <uint32_t opts = barrierOptions,
            typename = std::enable_if_t<!(opts & BarrierOption_HasGCLifetime)>>
  explicit BarrieredPtrImpl(Self&& other) : Base(other.release()) {
    maybePostWriteBarrier(defaultValue(), this->unbarrieredGet());
  }
  template <uint32_t opts = barrierOptions,
            typename = std::enable_if_t<!(opts & BarrierOption_HasGCLifetime)>>
  Self& operator=(Self&& other) noexcept {
    uncheckedSet(other.release());
    return *this;
  }

  ~BarrieredPtrImpl() {
    if constexpr (hasOption(BarrierOption_HasGCLifetime)) {
#ifdef DEBUG
      // No barriers are necessary as this only happens when the GC is sweeping
      // or before this has been initialized.
      checkGCLifetime();
      Poison(this, JS_FREED_HEAP_PTR_PATTERN, sizeof(*this),
             MemCheckKind::MakeNoAccess);
#endif
    } else {
      this->maybePreWriteBarrier();
      this->maybePostWriteBarrier(this->unbarrieredGet(), defaultValue());
    }
  }

  DECLARE_POINTER_CONSTREF_OPS(T);

  void init(const T& newValue) {
    checkValue(newValue);
    // No pre-barrier is required for init.
    this->unbarrieredSet(newValue);
    this->maybePostWriteBarrier(defaultValue(), newValue);
  }

 public:
  void set(const T& newValue) {
    checkValue(newValue);
    uncheckedSet(newValue);
  }

 private:
  void uncheckedSet(const T& newValue) {
    T oldValue = this->unbarrieredGet();
    this->maybePreWriteBarrier();
    this->unbarrieredSet(newValue);
    this->maybePostWriteBarrier(oldValue, newValue);
  }

 public:
  // Public for hash table rekey operations.
  void unbarrieredSet(const T& newValue) {
    if constexpr (hasOption(BarrierOption_AtomicWrites)) {
      this->unbarrieredAtomicSet(newValue);
    } else {
      Base::unbarrieredSet(newValue);
    }
  }

  const T& get() const {
    this->maybeReadBarrier();
    return this->unbarrieredGet();
  }

  explicit operator bool() const {
    return this->unbarrieredGet() != defaultValue();
  }

  using Base::unbarrieredGet;

#if JS_BITS_PER_WORD == 64
  using Base::unbarrieredAtomicGet;
#endif

  T release() {
    // Release skips any read or prebarrier.
    T oldValue = this->unbarrieredGet();
    this->unbarrieredSet(defaultValue());
    this->maybePostWriteBarrier(oldValue, defaultValue());
    return oldValue;
  }

  static T defaultValue() { return JS::SafelyInitialized<T>::create(); }

 private:
  void checkGCLifetime() {
    // If this assertion fails you may need to make the containing object use a
    // e.g. HeapPtr instead of GCPtr, as this can be deleted from outside of GC.
    MOZ_ASSERT(CurrentThreadIsGCSweeping() || CurrentThreadIsGCFinalizing() ||
               this->unbarrieredGet() == defaultValue());
  }

  MOZ_ALWAYS_INLINE void checkValue(const T& value) {
    AssertTargetIsNotGray(value);
  }

  MOZ_ALWAYS_INLINE void maybeReadBarrier() const {
    if constexpr (hasOption(BarrierOption_ReadBarrier)) {
      InternalBarrierMethods<T>::readBarrier(this->unbarrieredGet());
    }
  }

  MOZ_ALWAYS_INLINE void maybePreWriteBarrier() const {
    if constexpr (hasOption(BarrierOption_PreWriteBarrier)) {
      InternalBarrierMethods<T>::preBarrier(this->unbarrieredGet());
    }
  }

  MOZ_ALWAYS_INLINE void maybePostWriteBarrier(const T& prev,
                                               const T& next) const {
    if constexpr (hasOption(BarrierOption_PostWriteBarrier)) {
      T* ptr = this->unbarrieredAddress();
      InternalBarrierMethods<T>::postBarrier(ptr, prev, next);
    }
  }

 public:
  constexpr static bool hasOption(BarrierOption option) {
    return barrierOptions & uint32_t(option);
  }
};

}  // namespace gc

// Declare a barriered pointer template.
//
// This is done by inheritance rather than a using declaration so that the name
// shows up error messages and in the debugger.
#define DEFINE_BARRIERED_PTR(Name, Options)              \
  template <typename T>                                  \
  class Name : public gc::BarrieredPtrImpl<T, Options> { \
    using Base = gc::BarrieredPtrImpl<T, Options>;       \
                                                         \
   public:                                               \
    using Base::Base;                                    \
    using Base::operator=;                               \
  }

/*
 * PreBarriered only automatically handles pre-barriers. Post-barriers must be
 * manually implemented when using this class. GCPtr and HeapPtr should be used
 * in all cases that do not require explicit low-level control of moving
 * behavior.
 *
 * This class is useful for example for HashMap keys where automatically
 * updating a moved nursery pointer would break the hash table.
 */
DEFINE_BARRIERED_PTR(PreBarriered, gc::BarrierOption_PreWriteBarrier);

/*
 * A pre- and post-barriered heap pointer, for use inside the JS engine.
 *
 * It must only be stored in memory that has GC lifetime. GCPtr must not be
 * used in contexts where it may be implicitly moved or deleted, e.g. most
 * containers.
 *
 * The post-barriers implemented by this class are faster than those
 * implemented by js::HeapPtr<T> or JS::Heap<T> at the cost of not
 * automatically handling deletion or movement.
 */
DEFINE_BARRIERED_PTR(GCPtr, gc::BarrierOption_PreWriteBarrier |
                                gc::BarrierOption_PostWriteBarrier |
#ifdef JS_GC_CONCURRENT_MARKING
                                gc::BarrierOption_AtomicWrites |
#endif
                                gc::BarrierOption_HasGCLifetime);

/*
 * A pre- and post-barriered heap pointer, for use inside the JS engine. These
 * heap pointers can be stored in C++ containers like GCVector and GCHashMap.
 *
 * The GC sometimes keeps pointers to pointers to GC things --- for example, to
 * track references into the nursery. However, C++ containers like GCVector and
 * GCHashMap usually reserve the right to relocate their elements any time
 * they're modified, invalidating all pointers to the elements. HeapPtr
 * has a move constructor which knows how to keep the GC up to date if it is
 * moved to a new location.
 *
 * However, because of this additional communication with the GC, HeapPtr
 * is somewhat slower, so it should only be used in contexts where this ability
 * is necessary.
 *
 * Obviously, JSObjects, JSStrings, and the like get tenured and compacted, so
 * whatever pointers they contain get relocated, in the sense used here.
 * However, since the GC itself is moving those values, it takes care of its
 * internal pointers to those pointers itself. HeapPtr is only necessary
 * when the relocation would otherwise occur without the GC's knowledge.
 */
DEFINE_BARRIERED_PTR(HeapPtr, gc::BarrierOption_PreWriteBarrier |
                                  gc::BarrierOption_PostWriteBarrier);

/*
 * A pre-barriered heap pointer, for use inside the JS engine.
 *
 * Similar to GCPtr, but used for a pointer to a buffer-allocated structure
 * containing GC thing pointers.
 *
 * It should only be stored in memory that has GC lifetime, or it must perform
 * the necessary pre-barrier if the memory is freed outside the GC. It must not
 * be used in contexts where it may be implicitly moved or deleted, e.g. most
 * containers.
 *
 * A post-barrier is unnecessary since buffer allocations are always in the same
 * heap as their owning cell; there can be no tenured heap to nursery buffer
 * edges.
 */
template <class T>
class GCBuffer : public BarrieredBase<T> {
  static_assert(std::is_pointer_v<T>);

  using Base = BarrieredBase<T>;

 public:
  GCBuffer() : Base(nullptr) {}

  explicit GCBuffer(T ptr) : Base(ptr) {}

  GCBuffer(const GCBuffer<T>& other) : Base(other) {}

  ~GCBuffer() {
    // No barriers are necessary as this only happens when the GC is sweeping.
    MOZ_ASSERT_IF(isTraceable(),
                  CurrentThreadIsGCSweeping() || CurrentThreadIsGCFinalizing());
  }

  void init(T ptr) {
    MOZ_ASSERT(this->get() == JS::SafelyInitialized<T>());
    AssertTargetIsNotGray(ptr);
    this->unbarrieredSet(ptr);
  }

  void set(JS::Zone* zone, T ptr) {
    pre(zone);
    this->unbarrieredSet(ptr);
  }

#ifdef JS_GC_CONCURRENT_MARKING
  void unbarrieredSet(T ptr) { this->unbarrieredAtomicSet(ptr); }
#else
  using Base::unbarrieredSet;
#endif

  T get() const { return this->unbarrieredGet(); }
  operator T() const { return get(); }
  T operator->() const { return get(); }

#if JS_BITS_PER_WORD == 64
  T atomicGet() const { return this->unbarrieredAtomicGet(); }
#endif

 protected:
  bool isTraceable() const { return uintptr_t(get()) > MaxTaggedPointer; }

  void pre(JS::Zone* zone) {
    if (isTraceable()) {
      auto* shadowZone = JS::shadow::Zone::from(zone);
      if (!shadowZone->needsMarkingBarrier()) {
        return;
      }

      JSTracer* trc = shadowZone->barrierTracer();
      TraceEdgeAndBuffer(trc, this, "GCBuffer::pre");
    }
  }
};

// Incremental GC requires that weak pointers have read barriers. See the block
// comment at the top of Barrier.h for a complete discussion of why.
//
// Note that this class also has post-barriers, so is safe to use with nursery
// pointers. However, when used as a hashtable key, care must still be taken to
// insert manual post-barriers on the table for rekeying if the key is based in
// any way on the address of the object.

DEFINE_BARRIERED_PTR(WeakHeapPtr, gc::BarrierOption_ReadBarrier |
                                      gc::BarrierOption_PostWriteBarrier);

// A wrapper for a bare pointer, with no barriers.
//
// This should only be necessary in a limited number of cases. Please don't add
// more uses of this if at all possible.
DEFINE_BARRIERED_PTR(UnsafeBarePtr, gc::BarrierOption_None);

// A pre- and post-barriered Value that is specialized to be aware that it
// resides in a slots or elements vector. This allows it to be relocated in
// memory, but with substantially less overhead than a HeapPtr.
class HeapSlot : public BarrieredBase<Value>,
                 public WrappedPtrOperations<Value, HeapSlot> {
  using Base = BarrieredBase<Value>;

 public:
  enum Kind { Slot = 0, Element = 1 };

  template <bool MayBeGCThing = true>
  void init(NativeObject* owner, Kind kind, uint32_t slot, const Value& v) {
    MOZ_ASSERT_IF(!MayBeGCThing, !v.isGCThing());

    this->unbarrieredSet(v);
    if constexpr (MayBeGCThing) {
      post(owner, kind, slot, v);
    }
  }

  // Initialization (so no pre-barrier) but where the barriers will be applied
  // later (e.g. in bulk)
  void unbarrieredInit(const Value& v) { this->unbarrieredSet(v); }

  void initAsUndefined() { this->unbarrieredSet(UndefinedValue()); }

  DECLARE_POINTER_CONSTREF_OPS(Value);

  // Use this if the automatic coercion to T isn't working.
  const Value& get() const { return this->unbarrieredGet(); }
  using Base::unbarrieredGet;

#if JS_BITS_PER_WORD == 64
  using Base::unbarrieredAtomicGet;
#endif

  // Getter used for tracing: no barriers and atomic when concurrent marking is
  // used.
#ifdef JS_GC_CONCURRENT_MARKING
  Value getForTracing() const { return unbarrieredAtomicGet(); }
#else
  Value getForTracing() const { return unbarrieredGet(); }
#endif

  // Use this if you want to change the value without invoking barriers.
  // Obviously this is dangerous unless you know the barrier is not needed.
#ifdef JS_GC_CONCURRENT_MARKING
  void unbarrieredSet(const Value& newValue) {
    this->unbarrieredAtomicSet(newValue);
  }
#else
  using Base::unbarrieredSet;
#endif

  // For users who need to manually barrier the raw types.
  static void preWriteBarrier(const Value& v) {
    InternalBarrierMethods<Value>::preBarrier(v);
  }

  void destroy() { pre(); }

  void setUndefinedUnchecked() {
    pre();
    this->unbarrieredSet(UndefinedValue());
  }

#ifdef DEBUG
  bool preconditionForSet(NativeObject* owner, Kind kind, uint32_t slot) const;
  void assertPreconditionForPostWriteBarrier(NativeObject* obj, Kind kind,
                                             uint32_t slot,
                                             const Value& target) const;
#endif

  template <bool MayBeGCThing = true>
  MOZ_ALWAYS_INLINE void set(NativeObject* owner, Kind kind, uint32_t slot,
                             const Value& v) {
    MOZ_ASSERT(preconditionForSet(owner, kind, slot));
    MOZ_ASSERT_IF(!MayBeGCThing, !this->unbarrieredGet().isGCThing());
    MOZ_ASSERT_IF(!MayBeGCThing, !v.isGCThing());

    if constexpr (MayBeGCThing) {
      pre();
    }
    this->unbarrieredSet(v);
    if constexpr (MayBeGCThing) {
      post(owner, kind, slot, v);
    }
  }

 private:
  void pre() {
    InternalBarrierMethods<Value>::preBarrier(this->unbarrieredGet());
  }

  void post(NativeObject* owner, Kind kind, uint32_t slot,
            const Value& target) {
#ifdef DEBUG
    assertPreconditionForPostWriteBarrier(owner, kind, slot, target);
#endif
    if (this->unbarrieredGet().isGCThing()) {
      gc::Cell* cell = this->unbarrieredGet().toGCThing();
      if (cell->storeBuffer()) {
        cell->storeBuffer()->putSlot(owner, kind, slot, 1);
      }
    }
  }
};

class HeapSlotArray {
  HeapSlot* array;

 public:
  explicit HeapSlotArray(HeapSlot* array) : array(array) {}

  HeapSlot* begin() const { return array; }

  operator const Value*() const {
    static_assert(sizeof(GCPtr<Value>) == sizeof(Value));
    static_assert(sizeof(HeapSlot) == sizeof(Value));
    return reinterpret_cast<const Value*>(array);
  }
  operator HeapSlot*() const { return begin(); }

  HeapSlotArray operator+(int offset) const {
    return HeapSlotArray(array + offset);
  }
  HeapSlotArray operator+(uint32_t offset) const {
    return HeapSlotArray(array + offset);
  }
};

// Initialize a field in the heap, with appropriate GC write barriers. This adds
// a store buffer entry for this specific edge if necessary.

template <typename T>
MOZ_ALWAYS_INLINE void BarrieredInit(bool nurseryOwned, void* dst, T value) {
  AssertTargetIsNotGray(value);

  // No pre-barrier necessary for initialization.

  T* ptr = reinterpret_cast<T*>(dst);
  *ptr = value;

  if (!nurseryOwned) {
    T prev = JS::SafelyInitialized<T>::create();
    InternalBarrierMethods<T>::postBarrier(ptr, prev, value);
  }
}
template <typename T>
MOZ_ALWAYS_INLINE void BarrieredInit(gc::Cell* owner, void* dst, T value) {
  BarrieredInit(!owner->isTenured(), dst, value);
}

namespace gc {
template <typename T, bool PreBarrier = true, bool PostBarrier = true>
MOZ_ALWAYS_INLINE void BarrieredSetImpl(bool nurseryOwned, void* dst, T value) {
  AssertTargetIsNotGray(value);

  T* ptr = reinterpret_cast<T*>(dst);
  T prev = *ptr;
  if constexpr (PreBarrier) {
    InternalBarrierMethods<T>::preBarrier(prev);
  }

  *ptr = value;

  if constexpr (PostBarrier) {
    if (!nurseryOwned) {
      InternalBarrierMethods<T>::postBarrier(ptr, prev, value);
    }
  }
}
}  // namespace gc

// Assign to a field in the heap, with appropriate GC write barriers. This adds
// a store buffer entry for this specific edge if necessary.

template <typename T>
MOZ_ALWAYS_INLINE void BarrieredSet(bool nurseryOwned, void* dst, T value) {
  gc::BarrieredSetImpl(nurseryOwned, dst, value);
}
template <typename T>
MOZ_ALWAYS_INLINE void BarrieredSet(gc::Cell* owner, void* dst, T value) {
  BarrieredSet(!owner->isTenured(), dst, value);
}

namespace gc {

template <typename T, bool PreBarrier = true, bool PostBarrier = true>
MOZ_ALWAYS_INLINE void BarrieredMoveRangeInner(bool nurseryOwned, void* dst,
                                               const T* src, size_t count) {
  T* ptr = reinterpret_cast<T*>(dst);
  if (uintptr_t(dst) - uintptr_t(src) < count * sizeof(T)) {
    for (size_t i = count; i != 0; i--) {
      BarrieredSetImpl<T, PreBarrier, PostBarrier>(nurseryOwned, ptr + i - 1,
                                                   src[i - 1]);
    }
    return;
  }

  for (size_t i = 0; i < count; i++) {
    BarrieredSetImpl<T, PreBarrier, PostBarrier>(nurseryOwned, ptr + i, src[i]);
  }
}

template <typename T>
void BarrieredMoveRangeImpl(gc::Cell* owner, void* dst, const T* src,
                            size_t count) {
  // This function is called frequently in some WasmGC benchmarks.
  //
  // Most of the time no barriers are required. The most frequent use of the
  // post barrier is for 12% of calls in Kotlin-compose-wasm. The most frequent
  // use of the pre barrier is for 1.5% of calls in Dart-flute-complex-wasm.

  bool nurseryOwned = !owner->isTenured();
  if (owner->shadowZone()->needsMarkingBarrier()) {
    // Needs pre barrier and maybe post barrier. The less likely case.
    BarrieredMoveRangeInner<T, true, true>(nurseryOwned, dst, src, count);
    return;
  }

  // Needs post barrier only. The most likely case.
  MOZ_ASSERT(!nurseryOwned);
  BarrieredMoveRangeInner<T, false, true>(false, dst, src, count);
}

}  // namespace gc

// Move a range of elements in the heap, with appropriate GC barriers. The
// destination may overlap the source.
//
// TODO: This can add a store buffer entry for every element, which is not very
// efficient. In future we could consider adding something like the
// slots/elements buffer for callers of this, so that we can add ranges to the
// store buffer with a single entry.

template <typename T>
void BarrieredMoveRange(gc::Cell* owner, void* dst, const T* src,
                        size_t count) {
  if (dst == src) {
    return;
  }

  if (owner->isTenured() || owner->shadowZone()->needsMarkingBarrier()) {
    gc::BarrieredMoveRangeImpl(owner, dst, src, count);
    return;
  }

  memmove(dst, src, count * sizeof(T));
}

// Copy a range of elements in the heap, with appropriate GC barriers. The
// destination may not overlap the source.
template <typename T>
void BarrieredCopyRange(gc::Cell* owner, T* dst, T* src, size_t count) {
  MOZ_ASSERT(uintptr_t(dst) >= uintptr_t(src) + count * sizeof(T) ||
             uintptr_t(dst) + count * sizeof(T) <= uintptr_t(src));

  bool nurseryOwned = !owner->isTenured();
  for (size_t i = 0; i < count; i++) {
    BarrieredSet(nurseryOwned, dst + i, src[i]);
  }
}

/*
 * ImmutableTenuredPtr is designed for one very narrow case: replacing
 * immutable raw pointers to GC-managed things, implicitly converting to a
 * handle type for ease of use. Pointers encapsulated by this type must:
 *
 *   be immutable (no incremental write barriers),
 *   never point into the nursery (no generational write barriers), and
 *   be traced via MarkRuntime (we use fromMarkedLocation).
 *
 * In short: you *really* need to know what you're doing before you use this
 * class!
 */
template <typename T>
class MOZ_HEAP_CLASS ImmutableTenuredPtr {
  T value;

 public:
  operator T() const { return value; }
  T operator->() const { return value; }

  // `ImmutableTenuredPtr<T>` is implicitly convertible to `Handle<T>`.
  //
  // In case you need to convert to `Handle<U>` where `U` is base class of `T`,
  // convert this to `Handle<T>` by `toHandle()` and then use implicit
  // conversion from `Handle<T>` to `Handle<U>`.
  operator Handle<T>() const { return toHandle(); }
  Handle<T> toHandle() const { return Handle<T>::fromMarkedLocation(&value); }

  void init(T ptr) {
    MOZ_ASSERT(ptr->isTenured());
    AssertTargetIsNotGray(ptr);
    value = ptr;
  }

  T get() const { return value; }
  const T* address() { return &value; }
};

// Template to remove any barrier wrapper and get the underlying type.
// For non-barriered types this is just original type.
template <typename T>
struct RemoveBarrier {
  using Type = T;
};

#define DEFINE_REMOVE_BARRIER(BarrieredPtr) \
  template <typename T>                     \
  struct RemoveBarrier<BarrieredPtr<T>> {   \
    using Type = T;                         \
  }

DEFINE_REMOVE_BARRIER(PreBarriered);
DEFINE_REMOVE_BARRIER(HeapPtr);
DEFINE_REMOVE_BARRIER(GCPtr);
DEFINE_REMOVE_BARRIER(WeakHeapPtr);

#undef DEFINE_REMOVE_BARRIER

template <typename T>
using IsBarriered =
    std::negation<std::is_same<T, typename RemoveBarrier<T>::Type>>;

#if MOZ_IS_GCC
template struct JS_PUBLIC_API StableCellHasher<JSObject*>;
template struct JS_PUBLIC_API StableCellHasher<JSScript*>;
#endif

// Declare a StableCellHasher specialization to allow use of barriered pointers
// as keys in a hash table without requiring rekeying when GC cells are
// moved. Barriers are skipped for hash table lookup.
//
// Barriered pointers with GC lifetime are not suitable for use as hashtable
// keys.
#define DEFINE_STABLE_CELL_HASHER(BarrieredPtr)                      \
  template <typename T>                                              \
  struct StableCellHasher<BarrieredPtr<T>> {                         \
    using Key = BarrieredPtr<T>;                                     \
    using Lookup = T;                                                \
                                                                     \
    static_assert(!Key::hasOption(gc::BarrierOption_HasGCLifetime)); \
                                                                     \
    static bool maybeGetHash(const Lookup& l, HashNumber* hashOut) { \
      return StableCellHasher<T>::maybeGetHash(l, hashOut);          \
    }                                                                \
    static bool ensureHash(const Lookup& l, HashNumber* hashOut) {   \
      return StableCellHasher<T>::ensureHash(l, hashOut);            \
    }                                                                \
    static HashNumber hash(const Lookup& l) {                        \
      return StableCellHasher<T>::hash(l);                           \
    }                                                                \
    static bool match(const Key& k, const Lookup& l) {               \
      return StableCellHasher<T>::match(k.unbarrieredGet(), l);      \
    }                                                                \
  }

DEFINE_STABLE_CELL_HASHER(PreBarriered);
DEFINE_STABLE_CELL_HASHER(HeapPtr);
DEFINE_STABLE_CELL_HASHER(WeakHeapPtr);

#undef DEFINE_STABLE_CELL_HASHER

// Declare a hasher struct to allow use of barriered pointers as keys in a
// rekeyable hash table. Barriers are skipped for hash table lookup and rekey.
#define DEFINE_BARRIERED_PTR_HASHER(Name, BarrieredPtr)                    \
  template <class T>                                                       \
  struct Name {                                                            \
    using Key = BarrieredPtr<T>;                                           \
    using Lookup = T;                                                      \
                                                                           \
    static_assert(!Key::hasOption(gc::BarrierOption_HasGCLifetime));       \
                                                                           \
    static HashNumber hash(Lookup l) { return DefaultHasher<T>::hash(l); } \
    static bool match(const Key& k, Lookup l) {                            \
      return k.unbarrieredGet() == l;                                      \
    }                                                                      \
    static void rekey(Key& k, T newKey) { k.unbarrieredSet(newKey); }      \
  }

DEFINE_BARRIERED_PTR_HASHER(PreBarrieredHasher, PreBarriered);
DEFINE_BARRIERED_PTR_HASHER(HeapPtrHasher, HeapPtr);
DEFINE_BARRIERED_PTR_HASHER(WeakHeapPtrHasher, WeakHeapPtr);
DEFINE_BARRIERED_PTR_HASHER(UnsafeBarePtrHasher, UnsafeBarePtr);

#undef DEFINE_BARRIERED_PTR_HASHER

// Set up descriptive type aliases.
template <class T>
using PreBarrierWrapper = PreBarriered<T>;
template <class T>
using PreAndPostBarrierWrapper = GCPtr<T>;

}  // namespace js

// Define comparison ops for all barrier wrappers.

namespace JS::detail {

// Define comparison ops for barriered pointer wrappers. Comparisons don't
// trigger read barriers. See js/public/ComparisonOperators.h for details.
#define DEFINE_BARRIERED_PTR_COMPARISON_OPS(BarrieredPtr)        \
  template <typename T>                                          \
  struct DefineComparisonOps<BarrieredPtr<T>> : std::true_type { \
    static const T& get(const BarrieredPtr<T>& v) {              \
      return v.unbarrieredGet();                                 \
    }                                                            \
  }

DEFINE_BARRIERED_PTR_COMPARISON_OPS(js::PreBarriered);
DEFINE_BARRIERED_PTR_COMPARISON_OPS(js::GCPtr);
DEFINE_BARRIERED_PTR_COMPARISON_OPS(js::HeapPtr);
DEFINE_BARRIERED_PTR_COMPARISON_OPS(js::WeakHeapPtr);

#undef DEFINE_BARRIERED_PTR_COMPARISON_OPS

template <>
struct DefineComparisonOps<js::HeapSlot> : std::true_type {
  static const Value& get(const js::HeapSlot& v) { return v.get(); }
};

}  // namespace JS::detail

namespace mozilla {

#define DEFINE_DEFAULT_HASHER(BarrieredPtr, Hasher) \
  template <class T>                                \
  struct DefaultHasher<BarrieredPtr<T>> : Hasher<T> {}

DEFINE_DEFAULT_HASHER(js::HeapPtr, js::HeapPtrHasher);
DEFINE_DEFAULT_HASHER(js::PreBarriered, js::PreBarrieredHasher);
DEFINE_DEFAULT_HASHER(js::WeakHeapPtr, js::WeakHeapPtrHasher);
DEFINE_DEFAULT_HASHER(js::UnsafeBarePtr, js::UnsafeBarePtrHasher);

#undef DEFINE_DEFAULT_HASHER

}  // namespace mozilla

#endif /* gc_Barrier_h */
