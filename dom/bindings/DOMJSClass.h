/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DOMJSClass_h
#define mozilla_dom_DOMJSClass_h

#include "js/Object.h"  // JS::GetClass, JS::GetReservedSlot
#include "js/Wrapper.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/OriginTrials.h"
#include "mozilla/dom/PrototypeList.h"  // auto-generated
#include "mozilla/dom/WebIDLPrefs.h"    // auto-generated

class nsCycleCollectionParticipant;
class nsWrapperCache;
struct JSFunctionSpec;
struct JSPropertySpec;
struct JSStructuredCloneReader;
struct JSStructuredCloneWriter;
class nsIGlobalObject;

// All DOM globals must have a slot at DOM_PROTOTYPE_SLOT.
#define DOM_PROTOTYPE_SLOT JSCLASS_GLOBAL_SLOT_COUNT

// Keep this count up to date with any extra global slots added above.
#define DOM_GLOBAL_SLOTS 1

// We use these flag bits for the new bindings.
#define JSCLASS_DOM_GLOBAL JSCLASS_USERBIT1
#define JSCLASS_IS_DOMIFACEANDPROTOJSCLASS JSCLASS_USERBIT2

namespace mozilla::dom {

/**
 * Returns true if code running in the given JSContext is allowed to access
 * [SecureContext] API on the given JSObject.
 *
 * [SecureContext] API exposure is restricted to use by code in a Secure
 * Contexts:
 *
 *   https://w3c.github.io/webappsec-secure-contexts/
 *
 * Since we want [SecureContext] exposure to depend on the privileges of the
 * running code (rather than the privileges of an object's creator), this
 * function checks to see whether the given JSContext's Realm is flagged
 * as a Secure Context.  That allows us to make sure that system principal code
 * (which is marked as a Secure Context) can access Secure Context API on an
 * object in a different realm, regardless of whether the other realm is a
 * Secure Context or not.
 *
 * Checking the JSContext's Realm doesn't work for expanded principal
 * globals accessing a Secure Context web page though (e.g. those used by frame
 * scripts).  To handle that we fall back to checking whether the JSObject came
 * from a Secure Context.
 *
 * Note: We'd prefer this function to live in BindingUtils.h, but we need to
 * call it in this header, and BindingUtils.h includes us (i.e. we'd have a
 * circular dependency between headers if it lived there).
 */
inline bool IsSecureContextOrObjectIsFromSecureContext(JSContext* aCx,
                                                       JSObject* aObj) {
  MOZ_ASSERT(!js::IsWrapper(aObj));
  return JS::GetIsSecureContext(js::GetContextRealm(aCx)) ||
         JS::GetIsSecureContext(js::GetNonCCWObjectRealm(aObj));
}

typedef bool (*ResolveOwnProperty)(
    JSContext* cx, JS::Handle<JSObject*> wrapper, JS::Handle<JSObject*> obj,
    JS::Handle<jsid> id,
    JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc);

typedef bool (*EnumerateOwnProperties)(JSContext* cx,
                                       JS::Handle<JSObject*> wrapper,
                                       JS::Handle<JSObject*> obj,
                                       JS::MutableHandleVector<jsid> props);

typedef bool (*DeleteNamedProperty)(JSContext* cx,
                                    JS::Handle<JSObject*> wrapper,
                                    JS::Handle<JSObject*> obj,
                                    JS::Handle<jsid> id,
                                    JS::ObjectOpResult& opresult);

// Returns true if the given global is of a type whose bit is set in
// aGlobalSet.
bool IsGlobalInExposureSet(JSContext* aCx, JSObject* aGlobal,
                           uint32_t aGlobalSet);

struct ConstantSpec {
  const char* name;
  JS::Value value;
};

typedef bool (*PropertyEnabled)(JSContext* cx, JSObject* global);

namespace GlobalNames {
// The names of our possible globals.  These are the names of the actual
// interfaces, not of the global names used to refer to them in IDL [Exposed]
// annotations.
static const uint32_t Window = 1u << 0;
static const uint32_t DedicatedWorkerGlobalScope = 1u << 1;
static const uint32_t SharedWorkerGlobalScope = 1u << 2;
static const uint32_t ServiceWorkerGlobalScope = 1u << 3;
static const uint32_t WorkerDebuggerGlobalScope = 1u << 4;
static const uint32_t AudioWorkletGlobalScope = 1u << 5;
static const uint32_t PaintWorkletGlobalScope = 1u << 6;

static constexpr uint32_t kCount = 7;
}  // namespace GlobalNames

struct PrefableDisablers {
  inline bool isEnabled(JSContext* cx, JS::Handle<JSObject*> obj) const {
    if (nonExposedGlobals &&
        IsGlobalInExposureSet(cx, JS::GetNonCCWObjectGlobal(obj),
                              nonExposedGlobals)) {
      return false;
    }
    if (prefIndex != WebIDLPrefIndex::NoPref &&
        !sWebIDLPrefs[uint16_t(prefIndex)]()) {
      return false;
    }
    if (secureContext && !IsSecureContextOrObjectIsFromSecureContext(cx, obj)) {
      return false;
    }
    if (trial != OriginTrial(0) &&
        !OriginTrials::IsEnabled(cx, JS::GetNonCCWObjectGlobal(obj), trial)) {
      // TODO(emilio): Perhaps reconsider the interaction between [Trial=""] and
      // [Pref=""].
      //
      // In particular, it might be desirable to only check the trial if there
      // is no pref or the pref is disabled.
      return false;
    }
    if (enabledFunc && !enabledFunc(cx, JS::GetNonCCWObjectGlobal(obj))) {
      return false;
    }
    return true;
  }

  // Index into the array of StaticPrefs
  const WebIDLPrefIndex prefIndex;

  // Bitmask of global names that we should not be exposed in.
  const uint16_t nonExposedGlobals : GlobalNames::kCount;

  // A boolean indicating whether a Secure Context is required.
  const uint16_t secureContext : 1;

  // An origin trial controlling the feature. This can be made a bitfield too if
  // needed.
  const OriginTrial trial;

  // A function pointer to a function that can say the property is disabled
  // even if "enabled" is set to true.  If the pointer is null the value of
  // "enabled" is used as-is.
  const PropertyEnabled enabledFunc;
};

template <typename T>
struct Prefable {
  inline bool isEnabled(JSContext* cx, JS::Handle<JSObject*> obj) const {
    MOZ_ASSERT(!js::IsWrapper(obj));
    if (!disablers) [[likely]] {
      return true;
    }
    return disablers->isEnabled(cx, obj);
  }

  // Things that can disable this set of specs. |nullptr| means "cannot be
  // disabled".
  const PrefableDisablers* const disablers;

  // Array of specs, terminated in whatever way is customary for T.
  // Null to indicate a end-of-array for Prefable, when such an
  // indicator is needed.
  const T* const specs;
};

enum PropertyType {
  eStaticMethod,
  eStaticAttribute,
  eMethod,
  eAttribute,
  eUnforgeableMethod,
  eUnforgeableAttribute,
  eConstant,
  ePropertyTypeCount
};

#define NUM_BITS_PROPERTY_INFO_TYPE 3
#define NUM_BITS_PROPERTY_INFO_PREF_INDEX 13
#define NUM_BITS_PROPERTY_INFO_SPEC_INDEX 16

struct PropertyInfo {
 private:
  // MSVC generates static initializers if we store a jsid here, even if
  // PropertyInfo has a constexpr constructor. See bug 1460341 and bug 1464036.
  uintptr_t mIdBits;

 public:
  // One of PropertyType, will be used for accessing the corresponding Duo in
  // NativePropertiesN.duos[].
  uint32_t type : NUM_BITS_PROPERTY_INFO_TYPE;
  // The index to the corresponding Preable in Duo.mPrefables[].
  uint32_t prefIndex : NUM_BITS_PROPERTY_INFO_PREF_INDEX;
  // The index to the corresponding spec in Duo.mPrefables[prefIndex].specs[].
  uint32_t specIndex : NUM_BITS_PROPERTY_INFO_SPEC_INDEX;

  void SetId(jsid aId) {
    static_assert(sizeof(jsid) == sizeof(mIdBits),
                  "jsid should fit in mIdBits");
    mIdBits = aId.asRawBits();
  }
  MOZ_ALWAYS_INLINE jsid Id() const { return jsid::fromRawBits(mIdBits); }

  bool IsStaticMethod() const { return type == eStaticMethod; }

  static int Compare(const PropertyInfo& aInfo1, const PropertyInfo& aInfo2) {
    // IdToIndexComparator needs to be updated if the order here is changed!
    if (aInfo1.mIdBits == aInfo2.mIdBits) [[unlikely]] {
      MOZ_ASSERT((aInfo1.type == eMethod || aInfo1.type == eStaticMethod) &&
                 (aInfo2.type == eMethod || aInfo2.type == eStaticMethod));

      bool isStatic1 = aInfo1.IsStaticMethod();

      MOZ_ASSERT(isStatic1 != aInfo2.IsStaticMethod(),
                 "We shouldn't have 2 static methods with the same name!");

      return isStatic1 ? -1 : 1;
    }

    return aInfo1.mIdBits < aInfo2.mIdBits ? -1 : 1;
  }
};

static_assert(
    ePropertyTypeCount <= 1ull << NUM_BITS_PROPERTY_INFO_TYPE,
    "We have property type count that is > (1 << NUM_BITS_PROPERTY_INFO_TYPE)");

// Conceptually, NativeProperties has seven (Prefable<T>*, PropertyInfo*) duos
// (where T is one of JSFunctionSpec, JSPropertySpec, or ConstantSpec), one for
// each of: static methods and attributes, methods and attributes, unforgeable
// methods and attributes, and constants.
//
// That's 14 pointers, but in most instances most of the duos are all null, and
// there are many instances. To save space we use a variable-length type,
// NativePropertiesN<N>, to hold the data and getters to access it. It has N
// actual duos (stored in duos[]), plus four bits for each of the 7 possible
// duos: 1 bit that states if that duo is present, and 3 that state that duo's
// offset (if present) in duos[].
//
// All duo accesses should be done via the getters, which contain assertions
// that check we don't overrun the end of the struct. (The duo data members are
// public only so they can be statically initialized.) These assertions should
// never fail so long as (a) accesses to the variable-length part are guarded by
// appropriate Has*() calls, and (b) all instances are well-formed, i.e. the
// value of N matches the number of mHas* members that are true.
//
// We store all the property ids a NativePropertiesN owns in a single array of
// PropertyInfo structs. Each struct contains an id and the information needed
// to find the corresponding Prefable for the enabled check, as well as the
// information needed to find the correct property descriptor in the
// Prefable. We also store an array of indices into the PropertyInfo array,
// sorted by bits of the corresponding jsid. Given a jsid, this allows us to
// binary search for the index of the corresponding PropertyInfo, if any.
//
// Finally, we define a typedef of NativePropertiesN<7>, NativeProperties, which
// we use as a "base" type used to refer to all instances of NativePropertiesN.
// (7 is used because that's the maximum valid parameter, though any other
// value 1..6 could also be used.) This is reasonable because of the
// aforementioned assertions in the getters. Upcast() is used to convert
// specific instances to this "base" type.
//
// An example
// ----------
// NativeProperties points to various things, and it can be hard to keep track.
// The following example shows the layout.
//
// Imagine an example interface, with:
// - 10 properties
//   - 6 methods, 3 with no disablers struct, 2 sharing the same disablers
//     struct, 1 using a different disablers struct
//   - 4 attributes, all with no disablers
// - The property order is such that those using the same disablers structs are
//   together. (This is not guaranteed, but it makes the example simpler.)
//
// Each PropertyInfo also contain indices into sMethods/sMethods_specs (for
// method infos) and sAttributes/sAttributes_specs (for attributes), which let
// them find their spec, but these are not shown.
//
//   sNativeProperties             sNativeProperties_        sNativeProperties_
//   ----                          sortedPropertyIndices[10] propertyInfos[10]
//   - <several scalar fields>     ----                      ----
//   - sortedPropertyIndices ----> <10 indices>         +--> 0 info (method)
//   - duos[2]                     ----                 |    1 info (method)
//     ----(methods)                                    |    2 info (method)
//     0 - mPrefables -------> points to sMethods below |    3 info (method)
//       - mPropertyInfos ------------------------------+    4 info (method)
//     1 - mPrefables -------> points to sAttributes below   5 info (method)
//       - mPropertyInfos ---------------------------------> 6 info (attr)
//     ----                                                  7 info (attr)
//   ----                                                    8 info (attr)
//                                                           9 info (attr)
//                                                           ----
//
// sMethods has three entries (excluding the terminator) because there are
// three disablers structs. The {nullptr,nullptr} serves as the terminator.
// There are also END terminators within sMethod_specs; the need for these
// terminators (as opposed to a length) is deeply embedded in SpiderMonkey.
// Disablers structs are suffixed with the index of the first spec they cover.
//
//   sMethods                               sMethods_specs
//   ----                                   ----
//   0 - nullptr                     +----> 0 spec
//     - specs ----------------------+      1 spec
//   1 - disablers ---> disablers4          2 spec
//     - specs ------------------------+    3 END
//   2 - disablers ---> disablers7     +--> 4 spec
//     - specs ----------------------+      5 spec
//   3 - nullptr                     |      6 END
//     - nullptr                     +----> 7 spec
//   ----                                   8 END
//
// sAttributes has a single entry (excluding the terminator) because all of the
// specs lack disablers.
//
//   sAttributes                            sAttributes_specs
//   ----                                   ----
//   0 - nullptr                     +----> 0 spec
//     - specs ----------------------+      1 spec
//   1 - nullptr                            2 spec
//     - nullptr                            3 spec
//   ----                                   4 END
//                                          ----
template <int N>
struct NativePropertiesN {
  // Duo structs are stored in the duos[] array, and each element in the array
  // could require a different T. Therefore, we can't use the correct type for
  // mPrefables. Instead we use void* and cast to the correct type in the
  // getters.
  struct Duo {
    const /*Prefable<const T>*/ void* const mPrefables;
    PropertyInfo* const mPropertyInfos;
  };

  constexpr const NativePropertiesN<7>* Upcast() const {
    return reinterpret_cast<const NativePropertiesN<7>*>(this);
  }

  const PropertyInfo* PropertyInfos() const { return duos[0].mPropertyInfos; }

#define DO(SpecT, FieldName)                                                 \
 public:                                                                     \
  /* The bitfields indicating the duo's presence and (if present) offset. */ \
  const uint32_t mHas##FieldName##s : 1;                                     \
  const uint32_t m##FieldName##sOffset : 3;                                  \
                                                                             \
 private:                                                                    \
  const Duo* FieldName##sDuo() const {                                       \
    MOZ_ASSERT(Has##FieldName##s());                                         \
    return &duos[m##FieldName##sOffset];                                     \
  }                                                                          \
                                                                             \
 public:                                                                     \
  bool Has##FieldName##s() const { return mHas##FieldName##s; }              \
  const Prefable<const SpecT>* FieldName##s() const {                        \
    return static_cast<const Prefable<const SpecT>*>(                        \
        FieldName##sDuo()->mPrefables);                                      \
  }                                                                          \
  PropertyInfo* FieldName##PropertyInfos() const {                           \
    return FieldName##sDuo()->mPropertyInfos;                                \
  }

  DO(JSFunctionSpec, StaticMethod)
  DO(JSPropertySpec, StaticAttribute)
  DO(JSFunctionSpec, Method)
  DO(JSPropertySpec, Attribute)
  DO(JSFunctionSpec, UnforgeableMethod)
  DO(JSPropertySpec, UnforgeableAttribute)
  DO(ConstantSpec, Constant)

#undef DO

  // The index to the iterator method in MethodPropertyInfos() array.
  const int16_t iteratorAliasMethodIndex;
  // The number of PropertyInfo structs that the duos manage. This is the total
  // count across all duos.
  const uint16_t propertyInfoCount;
  // The sorted indices array from sorting property ids, which will be used when
  // we binary search for a property.
  uint16_t* sortedPropertyIndices;

  const Duo duos[N];
};

// Ensure the struct has the expected size. The 8 is for the bitfields plus
// iteratorAliasMethodIndex and idsLength; the rest is for the idsSortedIndex,
// and duos[].
static_assert(sizeof(NativePropertiesN<1>) == 8 + 3 * sizeof(void*), "1 size");
static_assert(sizeof(NativePropertiesN<2>) == 8 + 5 * sizeof(void*), "2 size");
static_assert(sizeof(NativePropertiesN<3>) == 8 + 7 * sizeof(void*), "3 size");
static_assert(sizeof(NativePropertiesN<4>) == 8 + 9 * sizeof(void*), "4 size");
static_assert(sizeof(NativePropertiesN<5>) == 8 + 11 * sizeof(void*), "5 size");
static_assert(sizeof(NativePropertiesN<6>) == 8 + 13 * sizeof(void*), "6 size");
static_assert(sizeof(NativePropertiesN<7>) == 8 + 15 * sizeof(void*), "7 size");

// The "base" type.
typedef NativePropertiesN<7> NativeProperties;

struct NativePropertiesHolder {
  const NativeProperties* regular;
  const NativeProperties* chromeOnly;
  // Points to a static bool that's set to true once the regular and chromeOnly
  // NativeProperties have been inited. This is a pointer to a bool instead of
  // a bool value because NativePropertiesHolder is stored by value in
  // a static const NativePropertyHooks.
  bool* inited;
};

struct NativeNamedOrIndexedPropertyHooks {
  // The hook to call for resolving indexed or named properties.
  ResolveOwnProperty mResolveOwnProperty;
  // The hook to call for enumerating indexed or named properties.
  EnumerateOwnProperties mEnumerateOwnProperties;
  // The hook to call to delete a named property.  May be null if there are no
  // named properties or no named property deleter.  On success (true return)
  // the "found" argument will be set to true if there was in fact such a named
  // property and false otherwise.  If it's set to false, the caller is expected
  // to proceed with whatever deletion behavior it would have if there were no
  // named properties involved at all (i.e. if the hook were null).  If it's set
  // to true, it will indicate via opresult whether the delete actually
  // succeeded.
  DeleteNamedProperty mDeleteNamedProperty;
};

// Helper structure for Xrays for DOM binding objects. The same instance is used
// for instances, interface objects and interface prototype objects of a
// specific interface.
struct NativePropertyHooks {
  const NativeNamedOrIndexedPropertyHooks* mIndexedOrNamedNativeProperties;

  // The property arrays for this interface.
  NativePropertiesHolder mNativeProperties;

  // This will be set to the ID of the interface prototype object for the
  // interface, if it has one. If it doesn't have one it will be set to
  // prototypes::id::_ID_Count.
  prototypes::ID mPrototypeID;

  // This will be set to the ID of the interface object for the interface, if it
  // has one. If it doesn't have one it will be set to
  // constructors::id::_ID_Count.
  constructors::ID mConstructorID;

  // The JSClass to use for expandos on our Xrays.  Can be null, in which case
  // Xrays will use a default class of their choice.
  const JSClass* mXrayExpandoClass;
};

enum DOMObjectType : uint8_t {
  eInstance,
  eGlobalInstance,
  eInterface,
  eInterfacePrototype,
  eGlobalInterfacePrototype,
  eNamespace,
  eNamedPropertiesObject
};

inline bool IsInstance(DOMObjectType type) {
  return type == eInstance || type == eGlobalInstance;
}

inline bool IsInterfacePrototype(DOMObjectType type) {
  return type == eInterfacePrototype || type == eGlobalInterfacePrototype;
}

typedef JSObject* (*ProtoGetter)(JSContext* aCx);

/**
 * Returns a handle to the relevant WebIDL prototype object for the current
 * compartment global (which may be a handle to null on out of memory).  Once
 * allocated, the prototype object is guaranteed to exist as long as the global
 * does, since the global traces its array of WebIDL prototypes and
 * constructors.
 */
typedef JS::Handle<JSObject*> (*ProtoHandleGetter)(JSContext* aCx);

/**
 * Serializes a WebIDL object for structured cloning.  aObj may not be in the
 * compartment of aCx in cases when we were working with a cross-compartment
 * wrapper.  aObj is expected to be an object of the DOMJSClass that we got the
 * serializer from.
 */
typedef bool (*WebIDLSerializer)(JSContext* aCx,
                                 JSStructuredCloneWriter* aWriter,
                                 JS::Handle<JSObject*> aObj);

/**
 * Deserializes a WebIDL object from a structured clone serialization.
 */
typedef JSObject* (*WebIDLDeserializer)(JSContext* aCx,
                                        nsIGlobalObject* aGlobal,
                                        JSStructuredCloneReader* aReader);

using WrapperCacheGetter = nsWrapperCache* (*)(JSObject*);

// Special JSClass for reflected DOM objects.
struct DOMJSClass {
  // It would be nice to just inherit from JSClass, but that precludes pure
  // compile-time initialization of the form |DOMJSClass = {...};|, since C++
  // only allows brace initialization for aggregate/POD types.
  const JSClass mBase;

  // A list of interfaces that this object implements, in order of decreasing
  // derivedness.
  const prototypes::ID mInterfaceChain[MAX_PROTOTYPE_CHAIN_LENGTH];

  // We store the DOM object in reserved slot with index DOM_OBJECT_SLOT or in
  // the proxy private if we use a proxy object.
  // Sometimes it's an nsISupports and sometimes it's not; this class tells
  // us which it is.
  const bool mDOMObjectIsISupports;

  const NativePropertyHooks* mNativeHooks;

  ProtoHandleGetter mGetProto;

  // This stores the CC participant for the native, null if this class does not
  // implement cycle collection or if it inherits from nsISupports (we can get
  // the CC participant by QI'ing in that case).
  nsCycleCollectionParticipant* mParticipant;

  // The serializer for this class if the relevant object is [Serializable].
  // Null otherwise.
  WebIDLSerializer mSerializer;

  // A callback to get the wrapper cache for C++ objects that don't inherit from
  // nsISupports, or null.
  WrapperCacheGetter mWrapperCacheGetter;

  static const DOMJSClass* FromJSClass(const JSClass* base) {
    MOZ_ASSERT(base->flags & JSCLASS_IS_DOMJSCLASS);
    return reinterpret_cast<const DOMJSClass*>(base);
  }

  const JSClass* ToJSClass() const { return &mBase; }
};

// Special JSClass for DOM interface and interface prototype objects.
struct DOMIfaceAndProtoJSClass {
  // It would be nice to just inherit from JSClass, but that precludes pure
  // compile-time initialization of the form
  // |DOMJSInterfaceAndPrototypeClass = {...};|, since C++ only allows brace
  // initialization for aggregate/POD types.
  const JSClass mBase;

  // Either eNamespace, eInterfacePrototype,
  // eGlobalInterfacePrototype or eNamedPropertiesObject.
  DOMObjectType mType;  // uint8_t

  const prototypes::ID mPrototypeID;  // uint16_t
  const uint32_t mDepth;

  const NativePropertyHooks* mNativeHooks;

  ProtoGetter mGetParentProto;

  static const DOMIfaceAndProtoJSClass* FromJSClass(const JSClass* base) {
    MOZ_ASSERT(base->flags & JSCLASS_IS_DOMIFACEANDPROTOJSCLASS);
    return reinterpret_cast<const DOMIfaceAndProtoJSClass*>(base);
  }

  const JSClass* ToJSClass() const { return &mBase; }
};

class ProtoAndIfaceCache;

inline bool DOMGlobalHasProtoAndIFaceCache(const JSObject* global) {
  MOZ_DIAGNOSTIC_ASSERT(JS::GetClass(global)->flags & JSCLASS_DOM_GLOBAL);
  // This can be undefined if we GC while creating the global
  return !JS::GetReservedSlot(global, DOM_PROTOTYPE_SLOT).isUndefined();
}

inline bool HasProtoAndIfaceCache(const JSObject* global) {
  if (!(JS::GetClass(global)->flags & JSCLASS_DOM_GLOBAL)) {
    return false;
  }
  return DOMGlobalHasProtoAndIFaceCache(global);
}

inline ProtoAndIfaceCache* GetProtoAndIfaceCache(JSObject* global) {
  MOZ_DIAGNOSTIC_ASSERT(JS::GetClass(global)->flags & JSCLASS_DOM_GLOBAL);
  return static_cast<ProtoAndIfaceCache*>(
      JS::GetReservedSlot(global, DOM_PROTOTYPE_SLOT).toPrivate());
}

}  // namespace mozilla::dom

#endif /* mozilla_dom_DOMJSClass_h */
