/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A header for declaring various things that binding implementation headers
 * might need.  The idea is to make binding implementation headers safe to
 * include anywhere without running into include hell like we do with
 * BindingUtils.h
 */
#ifndef mozilla_dom_BindingDeclarations_h_
#define mozilla_dom_BindingDeclarations_h_

#include <type_traits>

#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "mozilla/CycleCollectedUniquePtr.h"
#include "mozilla/Maybe.h"
#include "mozilla/RootedOwningNonNull.h"
#include "mozilla/RootedRefPtr.h"
#include "mozilla/dom/DOMString.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIPrincipal;
class nsWrapperCache;

namespace mozilla {

class ErrorResult;
class OOMReporter;
class CopyableErrorResult;

namespace dom {

class BindingCallContext;

// Struct that serves as a base class for all dictionaries.  Particularly useful
// so we can use std::is_base_of to detect dictionary template arguments.
struct DictionaryBase {
 protected:
  bool ParseJSON(JSContext* aCx, const nsAString& aJSON,
                 JS::MutableHandle<JS::Value> aVal);
  bool ParseJSON(JSContext* aCx, const nsACString& aJSON,
                 JS::MutableHandle<JS::Value> aVal);

  bool StringifyToJSON(JSContext* aCx, JS::Handle<JSObject*> aObj,
                       nsAString& aJSON) const;
  bool StringifyToJSON(JSContext* aCx, JS::Handle<JSObject*> aObj,
                       nsACString& aJSON) const;

  // Struct used as a way to force a dictionary constructor to not init the
  // dictionary (via constructing from a pointer to this class).  We're putting
  // it here so that all the dictionaries will have access to it, but outside
  // code will not.
  struct FastDictionaryInitializer {};
};

struct MaybeEmptyDictionaryBase : DictionaryBase {
  bool IsAnyMemberPresent() const { return mIsAnyMemberPresent; }

 protected:
  bool mIsAnyMemberPresent = false;
};

template <class T>
constexpr bool is_dom_dictionary = std::is_base_of_v<DictionaryBase, T>;

template <typename T>
inline std::enable_if_t<is_dom_dictionary<T>, void> ImplCycleCollectionUnlink(
    T& aDictionary) {
  aDictionary.UnlinkForCC();
}

template <typename T>
inline std::enable_if_t<is_dom_dictionary<T>, void> ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback, T& aDictionary,
    const char* aName, uint32_t aFlags = 0) {
  aDictionary.TraverseForCC(aCallback, aFlags);
}

struct AllTypedArraysBase {};

template <class T>
constexpr bool is_dom_typed_array = std::is_base_of_v<AllTypedArraysBase, T>;

// Struct that serves as a base class for all unions.
// Particularly useful so we can use std::is_base_of to detect union
// template arguments.
struct AllUnionBase {};

template <class T>
constexpr bool is_dom_union = std::is_base_of_v<AllUnionBase, T>;

// Struct that serves as a base class for all owning unions.
// Particularly useful so we can use std::is_base_of to detect owning union
// template arguments.
struct AllOwningUnionBase : public AllUnionBase {};

template <class T>
constexpr bool is_dom_owning_union = std::is_base_of_v<AllOwningUnionBase, T>;

struct UnionWithTypedArraysBase {};

template <class T>
constexpr bool is_dom_union_with_typedarray_members =
    std::is_base_of_v<UnionWithTypedArraysBase, T>;

enum class CallerType : uint32_t;

class MOZ_STACK_CLASS GlobalObject {
 public:
  GlobalObject(JSContext* aCx, JSObject* aObject);

  JSObject* Get() const { return mGlobalJSObject; }

  nsISupports* GetAsSupports() const;

  // The context that this returns is not guaranteed to be in the compartment of
  // the object returned from Get(), in fact it's generally in the caller's
  // compartment.
  JSContext* Context() const { return mCx; }

  bool Failed() const { return !Get(); }

  // It returns the subjectPrincipal if called on the main-thread, otherwise
  // a nullptr is returned.
  nsIPrincipal* GetSubjectPrincipal() const;

  // Get the caller type.  Note that this needs to be called before anyone has
  // had a chance to mess with the JSContext.
  dom::CallerType CallerType() const;

 protected:
  JS::Rooted<JSObject*> mGlobalJSObject;
  JSContext* mCx;
  mutable nsISupports* MOZ_UNSAFE_REF(
      "Valid because GlobalObject is a stack "
      "class, and mGlobalObject points to the "
      "global, so it won't be destroyed as long "
      "as GlobalObject lives on the stack") mGlobalObject;
};

// Class for representing optional arguments.
template <typename T, typename InternalType>
class Optional_base {
 public:
  Optional_base() = default;

  Optional_base(Optional_base&&) = default;
  Optional_base& operator=(Optional_base&&) = default;

  explicit Optional_base(const T& aValue) { mImpl.emplace(aValue); }
  explicit Optional_base(T&& aValue) { mImpl.emplace(std::move(aValue)); }

  // Forbid copy-construction and assignment
  Optional_base(const Optional_base& other) = delete;
  const Optional_base& operator=(const Optional_base& other) = delete;

  bool operator==(const Optional_base<T, InternalType>& aOther) const {
    return mImpl == aOther.mImpl;
  }

  bool operator!=(const Optional_base<T, InternalType>& aOther) const {
    return mImpl != aOther.mImpl;
  }

  template <typename T1, typename T2>
  explicit Optional_base(const T1& aValue1, const T2& aValue2) {
    mImpl.emplace(aValue1, aValue2);
  }

  bool WasPassed() const { return mImpl.isSome(); }

  // Return InternalType here so we can work with it usefully.
  template <typename... Args>
  InternalType& Construct(Args&&... aArgs) {
    mImpl.emplace(std::forward<Args>(aArgs)...);
    return *mImpl;
  }

  void Reset() { mImpl.reset(); }

  const T& Value() const { return *mImpl; }

  // Return InternalType here so we can work with it usefully.
  InternalType& Value() { return *mImpl; }

  // And an explicit way to get the InternalType even if we're const.
  const InternalType& InternalValue() const { return *mImpl; }

  // If we ever decide to add conversion operators for optional arrays
  // like the ones Nullable has, we'll need to ensure that Maybe<> has
  // the boolean before the actual data.

 protected:
  Maybe<InternalType> mImpl;
};

template <typename T>
class Optional : public Optional_base<T, T> {
 public:
  MOZ_ALLOW_TEMPORARY Optional() : Optional_base<T, T>() {}

  explicit Optional(const T& aValue) : Optional_base<T, T>(aValue) {}
  Optional(Optional&&) = default;
};

template <typename T>
class Optional<JS::Handle<T>>
    : public Optional_base<JS::Handle<T>, JS::Rooted<T>> {
 public:
  MOZ_ALLOW_TEMPORARY Optional()
      : Optional_base<JS::Handle<T>, JS::Rooted<T>>() {}

  explicit Optional(JSContext* cx)
      : Optional_base<JS::Handle<T>, JS::Rooted<T>>() {
    this->Construct(cx);
  }

  Optional(JSContext* cx, const T& aValue)
      : Optional_base<JS::Handle<T>, JS::Rooted<T>>(cx, aValue) {}

  // Override the const Value() to return the right thing so we're not
  // returning references to temporaries.
  JS::Handle<T> Value() const { return *this->mImpl; }

  // And we have to override the non-const one too, since we're
  // shadowing the one on the superclass.
  JS::Rooted<T>& Value() { return *this->mImpl; }
};

// A specialization of Optional for JSObject* to make sure that when someone
// calls Construct() on it we will pre-initialized the JSObject* to nullptr so
// it can be traced safely.
template <>
class Optional<JSObject*> : public Optional_base<JSObject*, JSObject*> {
 public:
  Optional() = default;

  explicit Optional(JSObject* aValue)
      : Optional_base<JSObject*, JSObject*>(aValue) {}

  // Don't allow us to have an uninitialized JSObject*
  JSObject*& Construct() {
    // The Android compiler sucks and thinks we're trying to construct
    // a JSObject* from an int if we don't cast here.  :(
    return Optional_base<JSObject*, JSObject*>::Construct(
        static_cast<JSObject*>(nullptr));
  }

  template <class T1>
  JSObject*& Construct(const T1& t1) {
    return Optional_base<JSObject*, JSObject*>::Construct(t1);
  }
};

// A specialization of Optional for JS::Value to make sure no one ever uses it.
template <>
class Optional<JS::Value> {
 public:
  Optional() = delete;

  explicit Optional(const JS::Value& aValue) = delete;
};

// A specialization of Optional for NonNull that lets us get a T& from Value()
template <typename U>
class NonNull;
template <typename T>
class Optional<NonNull<T>> : public Optional_base<T, NonNull<T>> {
 public:
  // We want our Value to actually return a non-const reference, even
  // if we're const.  At least for things that are normally pointer
  // types...
  T& Value() const { return *this->mImpl->get(); }

  // And we have to override the non-const one too, since we're
  // shadowing the one on the superclass.
  NonNull<T>& Value() { return *this->mImpl; }
};

// A specialization of Optional for OwningNonNull that lets us get a
// T& from Value()
template <typename T>
class Optional<OwningNonNull<T>> : public Optional_base<T, OwningNonNull<T>> {
 public:
  // We want our Value to actually return a non-const reference, even
  // if we're const.  At least for things that are normally pointer
  // types...
  T& Value() const { return *this->mImpl->get(); }

  // And we have to override the non-const one too, since we're
  // shadowing the one on the superclass.
  OwningNonNull<T>& Value() { return *this->mImpl; }
};

// Specialization for strings.
template <typename CharT>
class Optional<nsTSubstring<CharT>> {
  using AString = nsTSubstring<CharT>;

 public:
  Optional() : mStr(nullptr) {}

  // Forbid copy-construction and assignment
  Optional(const Optional& other) = delete;
  const Optional& operator=(const Optional& other) = delete;

  bool WasPassed() const { return !!mStr; }

  void operator=(const AString* str) {
    MOZ_ASSERT(str);
    mStr = str;
  }

  const AString& Value() const {
    MOZ_ASSERT(WasPassed());
    return *mStr;
  }

 private:
  const AString* mStr;
};

template <typename T>
inline void ImplCycleCollectionUnlink(Optional<T>& aField) {
  if (aField.WasPassed()) {
    ImplCycleCollectionUnlink(aField.Value());
  }
}

template <typename T>
inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback, Optional<T>& aField,
    const char* aName, uint32_t aFlags = 0) {
  if (aField.WasPassed()) {
    ImplCycleCollectionTraverse(aCallback, aField.Value(), aName, aFlags);
  }
}

template <class T>
class NonNull {
 public:
  NonNull() = default;

  // This is no worse than get() in terms of const handling.
  operator T&() const {
    MOZ_ASSERT(inited);
    MOZ_ASSERT(ptr, "NonNull<T> was set to null");
    return *ptr;
  }

  operator T*() const {
    MOZ_ASSERT(inited);
    MOZ_ASSERT(ptr, "NonNull<T> was set to null");
    return ptr;
  }

  void operator=(T* t) {
    ptr = t;
    MOZ_ASSERT(ptr);
#ifdef DEBUG
    inited = true;
#endif
  }

  template <typename U>
  void operator=(U* t) {
    ptr = t->ToAStringPtr();
    MOZ_ASSERT(ptr);
#ifdef DEBUG
    inited = true;
#endif
  }

  T** Slot() {
#ifdef DEBUG
    inited = true;
#endif
    return &ptr;
  }

  T* Ptr() {
    MOZ_ASSERT(inited);
    MOZ_ASSERT(ptr, "NonNull<T> was set to null");
    return ptr;
  }

  // Make us work with smart-ptr helpers that expect a get()
  T* get() const {
    MOZ_ASSERT(inited);
    MOZ_ASSERT(ptr);
    return ptr;
  }

 protected:
  // ptr is left uninitialized for optimization purposes.
  MOZ_INIT_OUTSIDE_CTOR T* ptr;
#ifdef DEBUG
  bool inited = false;
#endif
};

// Class for representing sequences in arguments.  We use a non-auto array
// because that allows us to use sequences of sequences and the like.  This
// needs to be fallible because web content controls the length of the array,
// and can easily try to create very large lengths.
template <typename T>
class Sequence : public FallibleTArray<T> {
 public:
  Sequence() : FallibleTArray<T>() {}
  MOZ_IMPLICIT Sequence(FallibleTArray<T>&& aArray)
      : FallibleTArray<T>(std::move(aArray)) {}
  MOZ_IMPLICIT Sequence(nsTArray<T>&& aArray)
      : FallibleTArray<T>(std::move(aArray)) {}

  Sequence(Sequence&&) = default;
  Sequence& operator=(Sequence&&) = default;

  // XXX(Bug 1631461) Codegen.py must be adapted to allow making Sequence
  // uncopyable.
  Sequence(const Sequence& aOther) {
    if (!this->AppendElements(aOther, fallible)) {
      MOZ_CRASH("Out of memory");
    }
  }
  Sequence& operator=(const Sequence& aOther) {
    if (this != &aOther) {
      this->Clear();
      if (!this->AppendElements(aOther, fallible)) {
        MOZ_CRASH("Out of memory");
      }
    }
    return *this;
  }
};

inline nsWrapperCache* GetWrapperCache(nsWrapperCache* cache) { return cache; }

inline nsWrapperCache* GetWrapperCache(void* p) { return nullptr; }

// Helper template for smart pointers to resolve ambiguity between
// GetWrappeCache(void*) and GetWrapperCache(const ParentObject&).
template <template <typename> class SmartPtr, typename T>
inline nsWrapperCache* GetWrapperCache(const SmartPtr<T>& aObject) {
  return GetWrapperCache(aObject.get());
}

enum class ReflectionScope { Content, NAC, UAWidget };

struct MOZ_STACK_CLASS ParentObject {
  template <class T>
  MOZ_IMPLICIT ParentObject(T* aObject)
      : mObject(ToSupports(aObject)),
        mWrapperCache(GetWrapperCache(aObject)),
        mReflectionScope(ReflectionScope::Content) {}

  template <class T, template <typename> class SmartPtr>
  MOZ_IMPLICIT ParentObject(const SmartPtr<T>& aObject)
      : mObject(aObject.get()),
        mWrapperCache(GetWrapperCache(aObject.get())),
        mReflectionScope(ReflectionScope::Content) {}

  ParentObject(nsISupports* aObject, nsWrapperCache* aCache)
      : mObject(aObject),
        mWrapperCache(aCache),
        mReflectionScope(ReflectionScope::Content) {}

  // We don't want to make this an nsCOMPtr because of performance reasons, but
  // it's safe because ParentObject is a stack class.
  nsISupports* const MOZ_NON_OWNING_REF mObject;
  nsWrapperCache* const mWrapperCache;
  ReflectionScope mReflectionScope;
};

namespace binding_detail {

// Class for simple sequence arguments, only used internally by codegen.
template <typename T>
class AutoSequence : public AutoTArray<T, 16> {
 public:
  AutoSequence() : AutoTArray<T, 16>() {}

  // Allow converting to const sequences as needed
  operator const Sequence<T>&() const {
    return *reinterpret_cast<const Sequence<T>*>(this);
  }
};

}  // namespace binding_detail

// Enum to represent a system or non-system caller type.
enum class CallerType : uint32_t { System, NonSystem };

// A class that can be passed (by value or const reference) to indicate that the
// caller is always a system caller.  This can be used as the type of an
// argument to force only system callers to call a function.
class SystemCallerGuarantee {
 public:
  operator CallerType() const { return CallerType::System; }
};

enum class DefineInterfaceProperty {
  No,
  CheckExposure,
  Always,
};

class ProtoAndIfaceCache;
using CreateInterfaceObjectsMethod =
    void (*)(JSContext*, JS::Handle<JSObject*>, ProtoAndIfaceCache&,
             DefineInterfaceProperty aDefineOnGlobal);

// GetPerInterfaceObjectHandle has 3 possible behaviours for defining the named
// properties on the global for an interface or namespace when it creates an
// interface or namespace object. aDefineOnGlobal can be used to pick the
// behaviour. GetPerInterfaceObjectHandle either:
//
//  * does not define any properties on the global object
//    (for DefineInterfaceProperty::No),
//  * checks whether the interface is exposed in the global object before
//    defining properties (for DefineInterfaceProperty::CheckExposure),
//  * always defines properties (for DefineInterfaceProperty::Always).
//
// Callers should be careful when passing DefineInterfaceProperty::Always and
// make sure to check exposure themselves if needed.
JS::Handle<JSObject*> GetPerInterfaceObjectHandle(
    JSContext* aCx, size_t aSlotId, CreateInterfaceObjectsMethod aCreator,
    DefineInterfaceProperty aDefineOnGlobal);

namespace binding_detail {

template <typename Enum>
struct EnumStrings;

template <size_t SlotIndex, size_t XrayExpandoSlotIndex, size_t Count>
class ReflectedHTMLAttributeSlots;

}  // namespace binding_detail

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_BindingDeclarations_h_
