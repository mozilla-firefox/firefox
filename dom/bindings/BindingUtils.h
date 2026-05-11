/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BindingUtils_h_
#define mozilla_dom_BindingUtils_h_

#include <type_traits>

#include "js/CharacterEncoding.h"
#include "js/Conversions.h"
#include "js/MemoryFunctions.h"
#include "js/Object.h"  // JS::GetClass, JS::GetCompartment, JS::GetReservedSlot, JS::SetReservedSlot
#include "js/RealmOptions.h"
#include "js/String.h"  // JS::GetLatin1LinearStringChars, JS::GetTwoByteLinearStringChars, JS::GetLinearStringLength, JS::LinearStringHasLatin1Chars, JS::StringHasLatin1Chars
#include "js/Wrapper.h"
#include "js/Zone.h"
#include "js/experimental/BindingAllocs.h"
#include "js/experimental/JitInfo.h"  // JSJitGetterOp, JSJitInfo
#include "js/friend/WindowProxy.h"  // js::IsWindow, js::IsWindowProxy, js::ToWindowProxyIfWindow
#include "jsfriendapi.h"
#include "mozilla/Array.h"
#include "mozilla/Assertions.h"
#include "mozilla/DeferredFinalize.h"
#include "mozilla/EnumTypeTraits.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SegmentedVector.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BindingCallContext.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/dom/DOMJSProxyHandler.h"
#include "mozilla/dom/JSSlots.h"
#include "mozilla/dom/NonRefcountedDOMObject.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/PrototypeList.h"
#include "mozilla/dom/RemoteObjectProxy.h"
#include "nsIGlobalObject.h"
#include "nsISupportsImpl.h"
#include "nsIVariant.h"
#include "nsJSUtils.h"
#include "nsWrapperCacheInlines.h"
#include "xpcObjectHelper.h"
#include "xpcpublic.h"

class nsGlobalWindowInner;
class nsGlobalWindowOuter;
class nsIInterfaceRequestor;

namespace mozilla {

enum UseCounter : int16_t;
enum class UseCounterWorker : int16_t;

namespace dom {
class CustomElementReactionsStack;
class Document;
class EventTarget;
class MessageManagerGlobal;
class ObservableArrayProxyHandler;
class DedicatedWorkerGlobalScope;
template <typename KeyType, typename ValueType>
class Record;
class WindowProxyHolder;

enum class DeprecatedOperations : uint16_t;

nsresult UnwrapArgImpl(JSContext* cx, JS::Handle<JSObject*> src,
                       const nsIID& iid, void** ppArg);

/** Convert a jsval to an XPCOM pointer. Caller must not assume that src will
    keep the XPCOM pointer rooted. */
template <class Interface>
inline nsresult UnwrapArg(JSContext* cx, JS::Handle<JSObject*> src,
                          Interface** ppArg) {
  return UnwrapArgImpl(cx, src, NS_GET_IID(Interface),
                       reinterpret_cast<void**>(ppArg));
}

nsresult UnwrapWindowProxyArg(JSContext* cx, JS::Handle<JSObject*> src,
                              WindowProxyHolder& ppArg);

// Returns true if the JSClass is used for DOM objects.
inline bool IsDOMClass(const JSClass* clasp) {
  return clasp->flags & JSCLASS_IS_DOMJSCLASS;
}

// Return true if the JSClass is used for non-proxy DOM objects.
inline bool IsNonProxyDOMClass(const JSClass* clasp) {
  return IsDOMClass(clasp) && clasp->isNativeObject();
}

// Returns true if the JSClass is used for DOM interface and interface
// prototype objects.
inline bool IsDOMIfaceAndProtoClass(const JSClass* clasp) {
  return clasp->flags & JSCLASS_IS_DOMIFACEANDPROTOJSCLASS;
}

static_assert(DOM_OBJECT_SLOT == 0,
              "DOM_OBJECT_SLOT doesn't match the proxy private slot.  "
              "Expect bad things");
template <class T>
inline T* UnwrapDOMObject(JSObject* obj) {
  MOZ_ASSERT(IsDOMClass(JS::GetClass(obj)),
             "Don't pass non-DOM objects to this function");

  JS::Value val = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT);
  return static_cast<T*>(val.toPrivate());
}

template <class T>
inline T* UnwrapPossiblyNotInitializedDOMObject(JSObject* obj) {
  // This is used by the OjectMoved JSClass hook which can be called before
  // JS_NewObject has returned and so before we have a chance to set
  // DOM_OBJECT_SLOT to anything useful.

  MOZ_ASSERT(IsDOMClass(JS::GetClass(obj)),
             "Don't pass non-DOM objects to this function");

  JS::Value val = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT);
  if (val.isUndefined()) {
    return nullptr;
  }
  return static_cast<T*>(val.toPrivate());
}

inline const DOMJSClass* GetDOMClass(const JSClass* clasp) {
  return IsDOMClass(clasp) ? DOMJSClass::FromJSClass(clasp) : nullptr;
}

inline const DOMJSClass* GetDOMClass(JSObject* obj) {
  return GetDOMClass(JS::GetClass(obj));
}

inline nsISupports* UnwrapDOMObjectToISupports(JSObject* aObject) {
  const DOMJSClass* clasp = GetDOMClass(aObject);
  if (!clasp || !clasp->mDOMObjectIsISupports) {
    return nullptr;
  }

  return UnwrapPossiblyNotInitializedDOMObject<nsISupports>(aObject);
}

inline bool IsDOMObject(JSObject* obj) { return IsDOMClass(JS::GetClass(obj)); }

// There are two valid ways to use UNWRAP_OBJECT: Either obj needs to
// be a MutableHandle<JSObject*>, or value needs to be a strong-reference
// smart pointer type (OwningNonNull or RefPtr or nsCOMPtr), in which case obj
// can be anything that converts to JSObject*.
//
// This can't be used with Window, EventTarget, or Location as the "Interface"
// argument (and will fail a static_assert if you try to do that).  Use
// UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT to unwrap to those interfaces.
#define UNWRAP_OBJECT(Interface, obj, value)                        \
  mozilla::dom::binding_detail::UnwrapObjectWithCrossOriginAsserts< \
      mozilla::dom::prototypes::id::Interface,                      \
      mozilla::dom::Interface##_Binding::NativeType>(obj, value)

// UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT is just like UNWRAP_OBJECT but requires a
// JSContext in a Realm that represents "who is doing the unwrapping?" to
// properly unwrap the object.
#define UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT(Interface, obj, value, cx)          \
  mozilla::dom::UnwrapObject<mozilla::dom::prototypes::id::Interface,        \
                             mozilla::dom::Interface##_Binding::NativeType>( \
      obj, value, cx)

// Test whether the given object is an instance of the given interface.
#define IS_INSTANCE_OF(Interface, obj) \
  mozilla::dom::IsInstanceOf<mozilla::dom::prototypes::id::Interface>(obj)

// Unwrap the given non-wrapper object.  This can be used with any obj that
// converts to JSObject*; as long as that JSObject* is live the return value
// will be valid.
#define UNWRAP_NON_WRAPPER_OBJECT(Interface, obj, value) \
  mozilla::dom::UnwrapNonWrapperObject<                  \
      mozilla::dom::prototypes::id::Interface,           \
      mozilla::dom::Interface##_Binding::NativeType>(obj, value)

// Some callers don't want to set an exception when unwrapping fails
// (for example, overload resolution uses unwrapping to tell what sort
// of thing it's looking at).
// U must be something that a T* can be assigned to (e.g. T* or an RefPtr<T>).
//
// The obj argument will be mutated to point to CheckedUnwrap of itself if the
// passed-in value is not a DOM object and CheckedUnwrap succeeds.
//
// If mayBeWrapper is true, there are three valid ways to invoke
// UnwrapObjectInternal: Either obj needs to be a class wrapping a
// MutableHandle<JSObject*>, with an assignment operator that sets the handle to
// the given object, or U needs to be a strong-reference smart pointer type
// (OwningNonNull or RefPtr or nsCOMPtr), or the value being stored in "value"
// must not escape past being tested for falsiness immediately after the
// UnwrapObjectInternal call.
//
// If mayBeWrapper is false, obj can just be a JSObject*, and U anything that a
// T* can be assigned to.
//
// The cx arg is in practice allowed to be either nullptr or JSContext* or a
// BindingCallContext reference.  If it's nullptr we will do a
// CheckedUnwrapStatic and it's the caller's responsibility to make sure they're
// not trying to work with Window or Location objects.  Otherwise we'll do a
// CheckedUnwrapDynamic.  This all only matters if mayBeWrapper is true; if it's
// false just pass nullptr for the cx arg.
namespace binding_detail {
template <class T, bool mayBeWrapper, typename U, typename V, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObjectInternal(V& obj, U& value,
                                                prototypes::ID protoID,
                                                uint32_t protoDepth,
                                                const CxType& cx) {
  static_assert(std::is_same_v<CxType, JSContext*> ||
                    std::is_same_v<CxType, BindingCallContext> ||
                    std::is_same_v<CxType, decltype(nullptr)>,
                "Unexpected CxType");

  /* First check to see whether we have a DOM object */
  const DOMJSClass* domClass = GetDOMClass(obj);
  if (domClass) {
    /* This object is a DOM object.  Double-check that it is safely
       castable to T by checking whether it claims to inherit from the
       class identified by protoID. */
    if (domClass->mInterfaceChain[protoDepth] == protoID) {
      value = UnwrapDOMObject<T>(obj);
      return NS_OK;
    }
  }

  /* Maybe we have a security wrapper or outer window? */
  if (!mayBeWrapper || !js::IsWrapper(obj)) {
    // For non-cross-origin-accessible methods and properties, remote object
    // proxies should behave the same as opaque wrappers.
    if (IsRemoteObjectProxy(obj)) {
      return NS_ERROR_XPC_SECURITY_MANAGER_VETO;
    }

    /* Not a DOM object, not a wrapper, just bail */
    return NS_ERROR_XPC_BAD_CONVERT_JS;
  }

  JSObject* unwrappedObj;
  if (std::is_same_v<CxType, decltype(nullptr)>) {
    unwrappedObj = js::CheckedUnwrapStatic(obj);
  } else {
    unwrappedObj =
        js::CheckedUnwrapDynamic(obj, cx, /* stopAtWindowProxy = */ false);
  }
  if (!unwrappedObj) {
    return NS_ERROR_XPC_SECURITY_MANAGER_VETO;
  }

  if (std::is_same_v<CxType, decltype(nullptr)>) {
    // We might still have a windowproxy here.  But it shouldn't matter, because
    // that's not what the caller is looking for, so we're going to fail out
    // anyway below once we do the recursive call to ourselves with wrapper
    // unwrapping disabled.
    MOZ_ASSERT(!js::IsWrapper(unwrappedObj) || js::IsWindowProxy(unwrappedObj));
  } else {
    // We shouldn't have a wrapper by now.
    MOZ_ASSERT(!js::IsWrapper(unwrappedObj));
  }

  // Recursive call is OK, because now we're using false for mayBeWrapper and
  // we never reach this code if that boolean is false, so can't keep calling
  // ourselves.
  //
  // Unwrap into a temporary pointer, because in general unwrapping into
  // something of type U might trigger GC (e.g. release the value currently
  // stored in there, with arbitrary consequences) and invalidate the
  // "unwrappedObj" pointer.
  T* tempValue = nullptr;
  nsresult rv = UnwrapObjectInternal<T, false>(unwrappedObj, tempValue, protoID,
                                               protoDepth, nullptr);
  if (NS_SUCCEEDED(rv)) {
    // Suppress a hazard related to keeping tempValue alive across
    // UnwrapObjectInternal, because the analysis can't tell that this function
    // will not GC if maybeWrapped=False and we've already gone through a level
    // of unwrapping so unwrappedObj will be !IsWrapper.
    JS::AutoSuppressGCAnalysis suppress;

    // It's very important to not update "obj" with the "unwrappedObj" value
    // until we know the unwrap has succeeded.  Otherwise, in a situation in
    // which we have an overload of object and primitive we could end up
    // converting to the primitive from the unwrappedObj, whereas we want to do
    // it from the original object.
    obj = unwrappedObj;
    // And now assign to "value"; at this point we don't care if a GC happens
    // and invalidates unwrappedObj.
    value = tempValue;
    return NS_OK;
  }

  /* It's the wrong sort of DOM object */
  return NS_ERROR_XPC_BAD_CONVERT_JS;
}

struct MutableObjectHandleWrapper {
  explicit MutableObjectHandleWrapper(JS::MutableHandle<JSObject*> aHandle)
      : mHandle(aHandle) {}

  void operator=(JSObject* aObject) {
    MOZ_ASSERT(aObject);
    mHandle.set(aObject);
  }

  operator JSObject*() const { return mHandle; }

 private:
  JS::MutableHandle<JSObject*> mHandle;
};

struct MutableValueHandleWrapper {
  explicit MutableValueHandleWrapper(JS::MutableHandle<JS::Value> aHandle)
      : mHandle(aHandle) {}

  void operator=(JSObject* aObject) {
    MOZ_ASSERT(aObject);
    mHandle.setObject(*aObject);
  }

  operator JSObject*() const { return &mHandle.toObject(); }

 private:
  JS::MutableHandle<JS::Value> mHandle;
};

}  // namespace binding_detail

// UnwrapObject overloads that ensure we have a MutableHandle to keep it alive.
template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JS::MutableHandle<JSObject*> obj,
                                        U& value, const CxType& cx) {
  binding_detail::MutableObjectHandleWrapper wrapper(obj);
  return binding_detail::UnwrapObjectInternal<T, true>(
      wrapper, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JS::MutableHandle<JS::Value> obj,
                                        U& value, const CxType& cx) {
  MOZ_ASSERT(obj.isObject());
  binding_detail::MutableValueHandleWrapper wrapper(obj);
  return binding_detail::UnwrapObjectInternal<T, true>(
      wrapper, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

// UnwrapObject overloads that ensure we have a strong ref to keep it alive.
template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JSObject* obj, RefPtr<U>& value,
                                        const CxType& cx) {
  return binding_detail::UnwrapObjectInternal<T, true>(
      obj, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JSObject* obj, nsCOMPtr<U>& value,
                                        const CxType& cx) {
  return binding_detail::UnwrapObjectInternal<T, true>(
      obj, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JSObject* obj, OwningNonNull<U>& value,
                                        const CxType& cx) {
  return binding_detail::UnwrapObjectInternal<T, true>(
      obj, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JSObject* obj, NonNull<U>& value,
                                        const CxType& cx) {
  return binding_detail::UnwrapObjectInternal<T, true>(
      obj, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, cx);
}

// An UnwrapObject overload that just calls one of the JSObject* ones.
template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JS::Handle<JS::Value> obj, U& value,
                                        const CxType& cx) {
  MOZ_ASSERT(obj.isObject());
  return UnwrapObject<PrototypeID, T>(&obj.toObject(), value, cx);
}

template <prototypes::ID PrototypeID, class T, typename U, typename CxType>
MOZ_ALWAYS_INLINE nsresult UnwrapObject(JS::Handle<JS::Value> obj,
                                        NonNull<U>& value, const CxType& cx) {
  MOZ_ASSERT(obj.isObject());
  return UnwrapObject<PrototypeID, T>(&obj.toObject(), value, cx);
}

template <prototypes::ID PrototypeID>
MOZ_ALWAYS_INLINE void AssertStaticUnwrapOK() {
  static_assert(PrototypeID != prototypes::id::Window,
                "Can't do static unwrap of WindowProxy; use "
                "UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT or a cross-origin-object "
                "aware version of IS_INSTANCE_OF");
  static_assert(PrototypeID != prototypes::id::EventTarget,
                "Can't do static unwrap of WindowProxy (which an EventTarget "
                "might be); use UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT or a "
                "cross-origin-object aware version of IS_INSTANCE_OF");
  static_assert(PrototypeID != prototypes::id::Location,
                "Can't do static unwrap of Location; use "
                "UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT or a cross-origin-object "
                "aware version of IS_INSTANCE_OF");
}

namespace binding_detail {
// This function is just here so we can do some static asserts in a centralized
// place instead of putting them in every single UnwrapObject overload.
template <prototypes::ID PrototypeID, class T, typename U, typename V>
MOZ_ALWAYS_INLINE nsresult UnwrapObjectWithCrossOriginAsserts(V&& obj,
                                                              U& value) {
  AssertStaticUnwrapOK<PrototypeID>();
  return UnwrapObject<PrototypeID, T>(obj, value, nullptr);
}
}  // namespace binding_detail

template <prototypes::ID PrototypeID>
MOZ_ALWAYS_INLINE bool IsInstanceOf(JSObject* obj) {
  AssertStaticUnwrapOK<PrototypeID>();
  void* ignored;
  nsresult unwrapped = binding_detail::UnwrapObjectInternal<void, true>(
      obj, ignored, PrototypeID, PrototypeTraits<PrototypeID>::Depth, nullptr);
  return NS_SUCCEEDED(unwrapped);
}

template <prototypes::ID PrototypeID, class T, typename U>
MOZ_ALWAYS_INLINE nsresult UnwrapNonWrapperObject(JSObject* obj, U& value) {
  MOZ_ASSERT(!js::IsWrapper(obj));
  return binding_detail::UnwrapObjectInternal<T, false>(
      obj, value, PrototypeID, PrototypeTraits<PrototypeID>::Depth, nullptr);
}

MOZ_ALWAYS_INLINE bool IsConvertibleToDictionary(JS::Handle<JS::Value> val) {
  return val.isNullOrUndefined() || val.isObject();
}

// The items in the protoAndIfaceCache are indexed by the prototypes::id::ID,
// constructors::id::ID and namedpropertiesobjects::id::ID enums, in that order.
// The end of the prototype objects should be the start of the interface
// objects, and the end of the interface objects should be the start of the
// named properties objects.
static_assert((size_t)constructors::id::_ID_Start ==
                      (size_t)prototypes::id::_ID_Count &&
                  (size_t)namedpropertiesobjects::id::_ID_Start ==
                      (size_t)constructors::id::_ID_Count,
              "Overlapping or discontiguous indexes.");
const size_t kProtoAndIfaceCacheCount = namedpropertiesobjects::id::_ID_Count;

class ProtoAndIfaceCache {
  // The caching strategy we use depends on what sort of global we're dealing
  // with.  For a window-like global, we want everything to be as fast as
  // possible, so we use a flat array, indexed by prototype/constructor ID.
  // For everything else (e.g. globals for JSMs), space is more important than
  // speed, so we use a two-level lookup table.

  class ArrayCache
      : public Array<JS::Heap<JSObject*>, kProtoAndIfaceCacheCount> {
   public:
    bool HasEntryInSlot(size_t i) {
      // Do an explicit call to the Heap<…> bool conversion operator. Because
      // that operator is marked explicit we'd otherwise end up doing an
      // implicit cast to JSObject* first, causing an unnecessary call to
      // exposeToActiveJS().
      return bool((*this)[i]);
    }

    JS::Heap<JSObject*>& EntrySlotOrCreate(size_t i) { return (*this)[i]; }

    JS::Heap<JSObject*>& EntrySlotMustExist(size_t i) { return (*this)[i]; }

    void Trace(JSTracer* aTracer) {
      for (size_t i = 0; i < std::size(*this); ++i) {
        JS::TraceEdge(aTracer, &(*this)[i], "protoAndIfaceCache[i]");
      }
    }

    size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
      return aMallocSizeOf(this);
    }
  };

  class PageTableCache {
   public:
    PageTableCache() { memset(mPages.begin(), 0, sizeof(mPages)); }

    ~PageTableCache() {
      for (size_t i = 0; i < std::size(mPages); ++i) {
        delete mPages[i];
      }
    }

    bool HasEntryInSlot(size_t i) {
      MOZ_ASSERT(i < kProtoAndIfaceCacheCount);
      size_t pageIndex = i / kPageSize;
      size_t leafIndex = i % kPageSize;
      Page* p = mPages[pageIndex];
      if (!p) {
        return false;
      }
      // Do an explicit call to the Heap<…> bool conversion operator. Because
      // that operator is marked explicit we'd otherwise end up doing an
      // implicit cast to JSObject* first, causing an unnecessary call to
      // exposeToActiveJS().
      return bool((*p)[leafIndex]);
    }

    JS::Heap<JSObject*>& EntrySlotOrCreate(size_t i) {
      MOZ_ASSERT(i < kProtoAndIfaceCacheCount);
      size_t pageIndex = i / kPageSize;
      size_t leafIndex = i % kPageSize;
      Page* p = mPages[pageIndex];
      if (!p) {
        p = new Page;
        mPages[pageIndex] = p;
      }
      return (*p)[leafIndex];
    }

    JS::Heap<JSObject*>& EntrySlotMustExist(size_t i) {
      MOZ_ASSERT(i < kProtoAndIfaceCacheCount);
      size_t pageIndex = i / kPageSize;
      size_t leafIndex = i % kPageSize;
      Page* p = mPages[pageIndex];
      MOZ_ASSERT(p);
      return (*p)[leafIndex];
    }

    void Trace(JSTracer* trc) {
      for (size_t i = 0; i < std::size(mPages); ++i) {
        Page* p = mPages[i];
        if (p) {
          for (size_t j = 0; j < std::size(*p); ++j) {
            JS::TraceEdge(trc, &(*p)[j], "protoAndIfaceCache[i]");
          }
        }
      }
    }

    size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
      size_t n = aMallocSizeOf(this);
      for (size_t i = 0; i < std::size(mPages); ++i) {
        n += aMallocSizeOf(mPages[i]);
      }
      return n;
    }

   private:
    static const size_t kPageSize = 16;
    typedef Array<JS::Heap<JSObject*>, kPageSize> Page;
    static const size_t kNPages =
        kProtoAndIfaceCacheCount / kPageSize +
        size_t(bool(kProtoAndIfaceCacheCount % kPageSize));
    Array<Page*, kNPages> mPages;
  };

 public:
  enum Kind { WindowLike, NonWindowLike };

  explicit ProtoAndIfaceCache(Kind aKind) : mKind(aKind) {
    MOZ_COUNT_CTOR(ProtoAndIfaceCache);
    if (aKind == WindowLike) {
      mArrayCache = new ArrayCache();
    } else {
      mPageTableCache = new PageTableCache();
    }
  }

  ~ProtoAndIfaceCache() {
    if (mKind == WindowLike) {
      delete mArrayCache;
    } else {
      delete mPageTableCache;
    }
    MOZ_COUNT_DTOR(ProtoAndIfaceCache);
  }

#define FORWARD_OPERATION(opName, args)    \
  do {                                     \
    if (mKind == WindowLike) {             \
      return mArrayCache->opName args;     \
    } else {                               \
      return mPageTableCache->opName args; \
    }                                      \
  } while (0)

  // Return whether slot i contains an object.  This doesn't return the object
  // itself because in practice consumers just want to know whether it's there
  // or not, and that doesn't require barriering, which returning the object
  // pointer does.
  bool HasEntryInSlot(size_t i) { FORWARD_OPERATION(HasEntryInSlot, (i)); }

  // Return a reference to slot i, creating it if necessary.  There
  // may not be an object in the returned slot.
  JS::Heap<JSObject*>& EntrySlotOrCreate(size_t i) {
    FORWARD_OPERATION(EntrySlotOrCreate, (i));
  }

  // Return a reference to slot i, which is guaranteed to already
  // exist.  There may not be an object in the slot, if prototype and
  // constructor initialization for one of our bindings failed.
  JS::Heap<JSObject*>& EntrySlotMustExist(size_t i) {
    FORWARD_OPERATION(EntrySlotMustExist, (i));
  }

  void Trace(JSTracer* aTracer) { FORWARD_OPERATION(Trace, (aTracer)); }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
    size_t n = aMallocSizeOf(this);
    n += (mKind == WindowLike
              ? mArrayCache->SizeOfIncludingThis(aMallocSizeOf)
              : mPageTableCache->SizeOfIncludingThis(aMallocSizeOf));
    return n;
  }
#undef FORWARD_OPERATION

 private:
  union {
    ArrayCache* mArrayCache;
    PageTableCache* mPageTableCache;
  };
  Kind mKind;
};

inline void AllocateProtoAndIfaceCache(JSObject* obj,
                                       ProtoAndIfaceCache::Kind aKind) {
  MOZ_ASSERT(JS::GetClass(obj)->flags & JSCLASS_DOM_GLOBAL);
  MOZ_ASSERT(JS::GetReservedSlot(obj, DOM_PROTOTYPE_SLOT).isUndefined());

  ProtoAndIfaceCache* protoAndIfaceCache = new ProtoAndIfaceCache(aKind);

  JS::SetReservedSlot(obj, DOM_PROTOTYPE_SLOT,
                      JS::PrivateValue(protoAndIfaceCache));
}

#ifdef DEBUG
struct VerifyTraceProtoAndIfaceCacheCalledTracer : public JS::CallbackTracer {
  bool ok;

  explicit VerifyTraceProtoAndIfaceCacheCalledTracer(JSContext* cx)
      : JS::CallbackTracer(cx, JS::TracerKind::VerifyTraceProtoAndIface),
        ok(false) {}

  void onChild(JS::GCCellPtr, const char* name) override {
    // We don't do anything here, we only want to verify that
    // TraceProtoAndIfaceCache was called.
  }
};
#endif

inline void TraceProtoAndIfaceCache(JSTracer* trc, JSObject* obj) {
  MOZ_ASSERT(JS::GetClass(obj)->flags & JSCLASS_DOM_GLOBAL);

#ifdef DEBUG
  if (trc->kind() == JS::TracerKind::VerifyTraceProtoAndIface) {
    // We don't do anything here, we only want to verify that
    // TraceProtoAndIfaceCache was called.
    static_cast<VerifyTraceProtoAndIfaceCacheCalledTracer*>(trc)->ok = true;
    return;
  }
#endif

  if (!DOMGlobalHasProtoAndIFaceCache(obj)) return;
  ProtoAndIfaceCache* protoAndIfaceCache = GetProtoAndIfaceCache(obj);
  protoAndIfaceCache->Trace(trc);
}

inline void DestroyProtoAndIfaceCache(JSObject* obj) {
  MOZ_ASSERT(JS::GetClass(obj)->flags & JSCLASS_DOM_GLOBAL);

  if (!DOMGlobalHasProtoAndIFaceCache(obj)) {
    return;
  }

  ProtoAndIfaceCache* protoAndIfaceCache = GetProtoAndIfaceCache(obj);

  delete protoAndIfaceCache;
}

/**
 * Add constants to an object.
 */
bool DefineConstants(JSContext* cx, JS::Handle<JSObject*> obj,
                     const ConstantSpec* cs);

struct JSNativeHolder {
  JSNative mNative;
  const NativePropertyHooks* mPropertyHooks;
};

// Struct for holding information for WebIDL interface objects (which are
// function objects). A pointer to this struct is held in the first reserved
// slot of the function object.
struct DOMInterfaceInfo {
  JSNativeHolder nativeHolder;

  ProtoHandleGetter mGetParentProto;

  const uint32_t mDepth;

  const prototypes::ID mPrototypeID;  // uint16_t

  // Boolean indicating whether this object wants a isInstance property
  // pointing to InterfaceIsInstance defined on it.  Only ever true for
  // interfaces.
  bool wantsInterfaceIsInstance;

  uint8_t mConstructorArgs;

  const char* mConstructorName;
};

struct LegacyFactoryFunction {
  const char* mName;
  const JSNativeHolder mHolder;
  uint8_t mNargs;
};

namespace binding_detail {

void CreateInterfaceObjects(
    JSContext* cx, JS::Handle<JSObject*> global,
    JS::Handle<JSObject*> protoProto, const DOMIfaceAndProtoJSClass* protoClass,
    JS::Heap<JSObject*>* protoCache, JS::Handle<JSObject*> interfaceProto,
    const DOMInterfaceInfo* interfaceInfo, unsigned ctorNargs,
    bool isConstructorChromeOnly,
    const Span<const LegacyFactoryFunction>& legacyFactoryFunctions,
    JS::Heap<JSObject*>* constructorCache, const NativeProperties* properties,
    const NativeProperties* chromeOnlyProperties, const char* name,
    bool defineOnGlobal, const char* const* unscopableNames, bool isGlobal,
    const char* const* legacyWindowAliases);

}  // namespace binding_detail

// clang-format off
/*
 * Create a DOM interface object (if constructorClass is non-null) and/or a
 * DOM interface prototype object (if protoClass is non-null).
 *
 * global is used as the parent of the interface object and the interface
 *        prototype object
 * protoProto is the prototype to use for the interface prototype object.
 * protoClass is the JSClass to use for the interface prototype object.
 *            This is null if we should not create an interface prototype
 *            object.
 * protoCache a pointer to a JSObject pointer where we should cache the
 *            interface prototype object. This must be null if protoClass is and
 *            vice versa.
 * interfaceProto is the prototype to use for the interface object.  This can be
 *                null if interfaceInfo is null (as in, if we're not creating an
 *                interface object at all).
 * interfaceInfo is the info to use for the interface object.  This can be null
 *               if we're not creating an interface object.
 * ctorNargs is the length of the constructor function; 0 if no constructor
 * isConstructorChromeOnly if true, the constructor is ChromeOnly.
 * legacyFactoryFunctions the legacy factory functions (can be empty)
 * constructorCache a pointer to a JSObject pointer where we should cache the
 *                  interface object. This must be null if both constructorClass
 *                  and constructor are null, and non-null otherwise.
 * properties contains the methods, attributes and constants to be defined on
 *            objects in any compartment.
 * chromeProperties contains the methods, attributes and constants to be defined
 *                  on objects in chrome compartments. This must be null if the
 *                  interface doesn't have any ChromeOnly properties or if the
 *                  object is being created in non-chrome compartment.
 * name the name to use for 1) the WebIDL class string, which is the value
 *      that's used for @@toStringTag, 2) the name property for interface
 *      objects and 3) the property on the global object that would be set to
 *      the interface object. In general this is the interface identifier.
 *      LegacyNamespace would expect something different for 1), but we don't
 *      support that. The class string for default iterator objects is not
 *      usable as 2) or 3), but default iterator objects don't have an interface
 *      object.
 * defineOnGlobal controls whether properties should be defined on the given
 *                global for the interface object (if any) and named
 *                constructors (if any) for this interface.  This can be
 *                false in situations where we want the properties to only
 *                appear on privileged Xrays but not on the unprivileged
 *                underlying global.
 * unscopableNames if not null it points to a null-terminated list of const
 *                 char* names of the unscopable properties for this interface.
 * isGlobal if true, we're creating interface objects for a [Global] interface,
 *          and hence shouldn't define properties on the prototype object.
 * legacyWindowAliases if not null it points to a null-terminated list of const
 *                     char* names of the legacy window aliases for this
 *                     interface.
 *
 * At least one of protoClass or interfaceInfo should be non-null. If
 * interfaceInfo is non-null, the resulting interface object will be defined on
 * the given global with property name |name|, which must also be non-null.
 */
// clang-format on
template <size_t N>
inline void CreateInterfaceObjects(
    JSContext* cx, JS::Handle<JSObject*> global,
    JS::Handle<JSObject*> protoProto, const DOMIfaceAndProtoJSClass* protoClass,
    JS::Heap<JSObject*>* protoCache, JS::Handle<JSObject*> interfaceProto,
    const DOMInterfaceInfo* interfaceInfo, unsigned ctorNargs,
    bool isConstructorChromeOnly,
    const Span<const LegacyFactoryFunction, N>& legacyFactoryFunctions,
    JS::Heap<JSObject*>* constructorCache, const NativeProperties* properties,
    const NativeProperties* chromeOnlyProperties, const char* name,
    bool defineOnGlobal, const char* const* unscopableNames, bool isGlobal,
    const char* const* legacyWindowAliases) {
  // We're using 1 slot for the interface info already, so we only have
  // INTERFACE_OBJECT_MAX_SLOTS - 1 slots for legacy factory functions.
  static_assert(N <= INTERFACE_OBJECT_MAX_SLOTS -
                         INTERFACE_OBJECT_FIRST_LEGACY_FACTORY_FUNCTION);

  return binding_detail::CreateInterfaceObjects(
      cx, global, protoProto, protoClass, protoCache, interfaceProto,
      interfaceInfo, ctorNargs, isConstructorChromeOnly, legacyFactoryFunctions,
      constructorCache, properties, chromeOnlyProperties, name, defineOnGlobal,
      unscopableNames, isGlobal, legacyWindowAliases);
}

/*
 * Create a namespace object.
 *
 * global the global on which to install a property named with name pointing to
 *        the namespace object if defineOnGlobal is true.
 * namespaceProto is the prototype to use for the namespace object.
 * namespaceClass is the JSClass to use for the namespace object.
 * namespaceCache a pointer to a JSObject pointer where we should cache the
 *                namespace object.
 * properties contains the methods, attributes and constants to be defined on
 *            objects in any compartment.
 * chromeProperties contains the methods, attributes and constants to be defined
 *                  on objects in chrome compartments. This must be null if the
 *                  namespace doesn't have any ChromeOnly properties or if the
 *                  object is being created in non-chrome compartment.
 * name the name to use for the WebIDL class string, which is the value
 *      that's used for @@toStringTag, and the name of the property on the
 *      global object that would be set to the namespace object.
 * defineOnGlobal controls whether properties should be defined on the given
 *                global for the namespace object. This can be false in
 *                situations where we want the properties to only appear on
 *                privileged Xrays but not on the unprivileged underlying
 *                global.
 */
void CreateNamespaceObject(JSContext* cx, JS::Handle<JSObject*> global,
                           JS::Handle<JSObject*> namespaceProto,
                           const DOMIfaceAndProtoJSClass& namespaceClass,
                           JS::Heap<JSObject*>* namespaceCache,
                           const NativeProperties* properties,
                           const NativeProperties* chromeOnlyProperties,
                           const char* name, bool defineOnGlobal);

/**
 * Define the properties (regular and chrome-only) on obj.
 *
 * obj the object to install the properties on. This should be the interface
 *     prototype object for regular interfaces and the instance object for
 *     interfaces marked with Global.
 * properties contains the methods, attributes and constants to be defined on
 *            objects in any compartment.
 * chromeProperties contains the methods, attributes and constants to be defined
 *                  on objects in chrome compartments. This must be null if the
 *                  interface doesn't have any ChromeOnly properties or if the
 *                  object is being created in non-chrome compartment.
 */
bool DefineProperties(JSContext* cx, JS::Handle<JSObject*> obj,
                      const NativeProperties* properties,
                      const NativeProperties* chromeOnlyProperties);

/*
 * Define the legacy unforgeable methods on an object.
 */
bool DefineLegacyUnforgeableMethods(
    JSContext* cx, JS::Handle<JSObject*> obj,
    const Prefable<const JSFunctionSpec>* props);

/*
 * Define the legacy unforgeable attributes on an object.
 */
bool DefineLegacyUnforgeableAttributes(
    JSContext* cx, JS::Handle<JSObject*> obj,
    const Prefable<const JSPropertySpec>* props);

#define HAS_MEMBER_TYPEDEFS \
 private:                   \
  typedef char yes[1];      \
  typedef char no[2]

#ifdef _MSC_VER
#  define HAS_MEMBER_CHECK(_name) \
    template <typename V>         \
    static yes& Check##_name(char (*)[(&V::_name == 0) + 1])
#else
#  define HAS_MEMBER_CHECK(_name) \
    template <typename V>         \
    static yes& Check##_name(char (*)[sizeof(&V::_name) + 1])
#endif

#define HAS_MEMBER(_memberName, _valueName) \
 private:                                   \
  HAS_MEMBER_CHECK(_memberName);            \
  template <typename V>                     \
  static no& Check##_memberName(...);       \
                                            \
 public:                                    \
  static bool const _valueName =            \
      sizeof(Check##_memberName<T>(nullptr)) == sizeof(yes)

template <class T>
struct NativeHasMember {
  HAS_MEMBER_TYPEDEFS;

  HAS_MEMBER(GetParentObject, GetParentObject);
  HAS_MEMBER(WrapObject, WrapObject);
};

template <class T>
struct IsSmartPtr {
  HAS_MEMBER_TYPEDEFS;

  HAS_MEMBER(get, value);
};

template <class T>
struct IsRefcounted {
  HAS_MEMBER_TYPEDEFS;

  HAS_MEMBER(AddRef, HasAddref);
  HAS_MEMBER(Release, HasRelease);

 public:
  static bool const value = HasAddref && HasRelease;

 private:
  // This struct only works if T is fully declared (not just forward declared).
  // The std::is_base_of check will ensure that, we don't really need it for any
  // other reason (the static assert will of course always be true).
  static_assert(!std::is_base_of<nsISupports, T>::value || IsRefcounted::value,
                "Classes derived from nsISupports are refcounted!");
};

#undef HAS_MEMBER
#undef HAS_MEMBER_CHECK
#undef HAS_MEMBER_TYPEDEFS

namespace binding_detail {

/**
 * We support a couple of cases for the classes used as NativeType for bindings:
 *
 * 1) Classes derived from nsISupports, but not inheriting from nsWrapperCache.
 *    For these classes we'll use QI on the nsISupports pointer to get to the
 *    nsWrapperCache. The classes for native objects for the binding probably
 *    inherit from the NativeType class, and from nsWrapperCache.
 * 2) Classes derived from nsISupports, and inheriting from nsWrapperCache. For
 *    these we rely on the class or one of its ancestors on the primary
 *    inheritance chain that is identical to:
 *       class Foo : public nsISupports, public nsWrapperCache {}
 *    This allows us to calculate offsets that allow us to cast a void* pointer
 *    to the object to nsWrapperCache by adding a fixed offset.
 * 3) Classes that are not derived from nsISupports, but inherit from
 *    nsWrapperCache. For these we rely on being able to cast a void* pointer
 *    directly to nsWrapperCache.
 */

// Small helper that has access to nsWrapperCache internals.
class CastableToWrapperCacheHelper {
 public:
  template <class T>
  static constexpr uintptr_t OffsetOf() {
    return offsetof(T, mWrapper) - offsetof(nsWrapperCache, mWrapper);
  }
};

template <size_t Offset>
class CastableToWrapperCache {
 protected:
  static nsWrapperCache* GetWrapperCache(void* aObj) {
    return aObj ? reinterpret_cast<nsWrapperCache*>(uintptr_t(aObj) + Offset)
                : nullptr;
  }

 public:
  static nsWrapperCache* GetWrapperCache(JSObject* aObj) {
    return GetWrapperCache(UnwrapPossiblyNotInitializedDOMObject<void>(aObj));
  }

  static size_t ObjectMoved(JSObject* aObj, JSObject* aOld) {
    JS::AutoAssertGCCallback inCallback;
    nsWrapperCache* cache = GetWrapperCache(aObj);
    if (cache) {
      cache->UpdateWrapper(aObj, aOld);
    }

    return 0;
  }
  static constexpr js::ClassExtension sClassExtension = {ObjectMoved};
};

class NeedsQIToWrapperCache {
 protected:
  static nsWrapperCache* GetWrapperCache(nsISupports* aObj) {
    if (!aObj) {
      return nullptr;
    }
    nsWrapperCache* cache;
    CallQueryInterface(aObj, &cache);
    return cache;
  }

 public:
  static nsWrapperCache* GetWrapperCache(JSObject* aObj) {
    return GetWrapperCache(
        UnwrapPossiblyNotInitializedDOMObject<nsISupports>(aObj));
  }

  static size_t ObjectMoved(JSObject* aObj, JSObject* aOld);
  static constexpr js::ClassExtension sClassExtension = {ObjectMoved};
};

template <class T>
using ToWrapperCacheHelper = std::conditional_t<
    std::is_base_of_v<nsWrapperCache, T>,
    CastableToWrapperCache<CastableToWrapperCacheHelper::OffsetOf<T>()>,
    NeedsQIToWrapperCache>;

template <class T,
          bool CastableToWrapperCache = std::is_base_of_v<nsWrapperCache, T>>
class NativeTypeHelpers_nsISupports;

template <class T>
class NativeTypeHelpers_nsISupports<T, true>
    : public CastableToWrapperCache<
          CastableToWrapperCacheHelper::OffsetOf<T>()> {};

template <class T>
class NativeTypeHelpers_nsISupports<T, false> : public NeedsQIToWrapperCache {};

template <class T>
class NativeTypeHelpers_Other
    : public CastableToWrapperCache<
          CastableToWrapperCacheHelper::OffsetOf<T>()> {};

template <class T>
using NativeTypeHelpers = std::conditional_t<std::is_base_of_v<nsISupports, T>,
                                             NativeTypeHelpers_nsISupports<T>,
                                             NativeTypeHelpers_Other<T>>;

}  // namespace binding_detail

#ifdef DEBUG
template <class T>
struct CheckCastableWrapperCache {
  static bool Check() {
    return reinterpret_cast<uintptr_t>(
               static_cast<nsWrapperCache*>(reinterpret_cast<T*>(1))) ==
           1 + binding_detail::CastableToWrapperCacheHelper::OffsetOf<T>();
  }
};
template <class T, bool isISupports = std::is_base_of<nsISupports, T>::value>
struct CheckWrapperCacheCast : public CheckCastableWrapperCache<T> {};
template <class T>
struct CheckWrapperCacheCast<T, true> {
  static bool Check() {
    if constexpr (std::is_base_of_v<nsWrapperCache, T>) {
      return CheckCastableWrapperCache<T>::Check();
    } else {
      return true;
    }
  }
};
#endif

inline bool TryToOuterize(JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isObject());
  if (js::IsWindow(&rval.toObject())) {
    JSObject* obj = js::ToWindowProxyIfWindow(&rval.toObject());
    MOZ_ASSERT(obj);
    rval.set(JS::ObjectValue(*obj));
  }

  return true;
}

inline bool TryToOuterize(JS::MutableHandle<JSObject*> obj) {
  if (js::IsWindow(obj)) {
    JSObject* proxy = js::ToWindowProxyIfWindow(obj);
    MOZ_ASSERT(proxy);
    obj.set(proxy);
  }

  return true;
}

// Make sure to wrap the given string value into the right compartment, as
// needed.
MOZ_ALWAYS_INLINE
bool MaybeWrapStringValue(JSContext* cx, JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isString());
  JSString* str = rval.toString();
  if (JS::GetStringZone(str) != js::GetContextZone(cx)) {
    return JS_WrapValue(cx, rval);
  }
  return true;
}

// Make sure to wrap the given object value into the right compartment as
// needed.  This will work correctly, but possibly slowly, on all objects.
MOZ_ALWAYS_INLINE
bool MaybeWrapObjectValue(JSContext* cx, JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isObject());

  // Cross-compartment always requires wrapping.
  JSObject* obj = &rval.toObject();
  if (JS::GetCompartment(obj) != js::GetContextCompartment(cx)) {
    return JS_WrapValue(cx, rval);
  }

  // We're same-compartment, but we might still need to outerize if we
  // have a Window.
  return TryToOuterize(rval);
}

// Like MaybeWrapObjectValue, but working with a
// JS::MutableHandle<JSObject*> which must be non-null.
MOZ_ALWAYS_INLINE
bool MaybeWrapObject(JSContext* cx, JS::MutableHandle<JSObject*> obj) {
  if (JS::GetCompartment(obj) != js::GetContextCompartment(cx)) {
    return JS_WrapObject(cx, obj);
  }

  // We're same-compartment, but we might still need to outerize if we
  // have a Window.
  return TryToOuterize(obj);
}

// Like MaybeWrapObjectValue, but also allows null
MOZ_ALWAYS_INLINE
bool MaybeWrapObjectOrNullValue(JSContext* cx,
                                JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isObjectOrNull());
  if (rval.isNull()) {
    return true;
  }
  return MaybeWrapObjectValue(cx, rval);
}

// Wrapping for objects that are known to not be DOM objects
MOZ_ALWAYS_INLINE
bool MaybeWrapNonDOMObjectValue(JSContext* cx,
                                JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isObject());
  // Compared to MaybeWrapObjectValue we just skip the TryToOuterize call.  The
  // only reason it would be needed is if we have a Window object, which would
  // have a DOM class.  Assert that we don't have any DOM-class objects coming
  // through here.
  MOZ_ASSERT(!GetDOMClass(&rval.toObject()));

  JSObject* obj = &rval.toObject();
  if (JS::GetCompartment(obj) == js::GetContextCompartment(cx)) {
    return true;
  }
  return JS_WrapValue(cx, rval);
}

// Like MaybeWrapNonDOMObjectValue but allows null
MOZ_ALWAYS_INLINE
bool MaybeWrapNonDOMObjectOrNullValue(JSContext* cx,
                                      JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(rval.isObjectOrNull());
  if (rval.isNull()) {
    return true;
  }
  return MaybeWrapNonDOMObjectValue(cx, rval);
}

// If rval is a gcthing and is not in the compartment of cx, wrap rval
// into the compartment of cx (typically by replacing it with an Xray or
// cross-compartment wrapper around the original object).
MOZ_ALWAYS_INLINE bool MaybeWrapValue(JSContext* cx,
                                      JS::MutableHandle<JS::Value> rval) {
  if (rval.isGCThing()) {
    if (rval.isString()) {
      return MaybeWrapStringValue(cx, rval);
    }
    if (rval.isObject()) {
      return MaybeWrapObjectValue(cx, rval);
    }
    // This could be optimized by checking the zone first, similar to
    // the way strings are handled. At present, this is used primarily
    // for structured cloning, so avoiding the overhead of JS_WrapValue
    // calls is less important than for other types.
    if (rval.isBigInt()) {
      return JS_WrapValue(cx, rval);
    }
    MOZ_ASSERT(rval.isSymbol());
    JS_MarkCrossZoneId(cx, JS::PropertyKey::Symbol(rval.toSymbol()));
  }
  return true;
}

namespace binding_detail {
enum GetOrCreateReflectorWrapBehavior {
  eWrapIntoContextCompartment,
  eDontWrapIntoContextCompartment
};

template <class T>
struct TypeNeedsOuterization {
  // We only need to outerize Window objects, so anything inheriting from
  // nsGlobalWindow (which inherits from EventTarget itself).
  static const bool value = std::is_base_of<nsGlobalWindowInner, T>::value ||
                            std::is_base_of<nsGlobalWindowOuter, T>::value ||
                            std::is_same_v<EventTarget, T>;
};

#ifdef DEBUG
template <typename T, bool isISupports = std::is_base_of<nsISupports, T>::value>
struct CheckWrapperCacheTracing {
  static inline void Check(T* aObject) {}
};

template <typename T>
struct CheckWrapperCacheTracing<T, true> {
  static void Check(T* aObject) {
    // Rooting analysis thinks QueryInterface may GC, but we're dealing with
    // a subset of QueryInterface, C++ only types here.
    JS::AutoSuppressGCAnalysis nogc;

    nsWrapperCache* wrapperCacheFromQI = nullptr;
    aObject->QueryInterface(NS_GET_IID(nsWrapperCache),
                            reinterpret_cast<void**>(&wrapperCacheFromQI));

    MOZ_ASSERT(wrapperCacheFromQI,
               "Missing nsWrapperCache from QueryInterface implementation?");

    if (!wrapperCacheFromQI->GetWrapperPreserveColor()) {
      // Can't assert that we trace the wrapper, since we don't have any
      // wrapper to trace.
      return;
    }

    nsISupports* ccISupports = nullptr;
    aObject->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                            reinterpret_cast<void**>(&ccISupports));
    MOZ_ASSERT(ccISupports,
               "nsWrapperCache object which isn't cycle collectable?");

    nsXPCOMCycleCollectionParticipant* participant = nullptr;
    CallQueryInterface(ccISupports, &participant);
    MOZ_ASSERT(participant, "Can't QI to CycleCollectionParticipant?");

    wrapperCacheFromQI->CheckCCWrapperTraversal(ccISupports, participant);
  }
};

void AssertReflectorHasGivenProto(JSContext* aCx, JSObject* aReflector,
                                  JS::Handle<JSObject*> aGivenProto);
#endif  // DEBUG

template <class T, GetOrCreateReflectorWrapBehavior wrapBehavior>
MOZ_ALWAYS_INLINE bool DoGetOrCreateDOMReflector(
    JSContext* cx, T* value, JS::Handle<JSObject*> givenProto,
    JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(value);
  MOZ_ASSERT_IF(givenProto, js::IsObjectInContextCompartment(givenProto, cx));
  JSObject* obj = value->GetWrapper();
  if (obj) {
#ifdef DEBUG
    AssertReflectorHasGivenProto(cx, obj, givenProto);
    // Have to reget obj because AssertReflectorHasGivenProto can
    // trigger gc so the pointer may now be invalid.
    obj = value->GetWrapper();
#endif
  } else {
    obj = value->WrapObject(cx, givenProto);
    if (!obj) {
      // At this point, obj is null, so just return false.
      // Callers seem to be testing JS_IsExceptionPending(cx) to
      // figure out whether WrapObject() threw.
      return false;
    }

#ifdef DEBUG
    if (std::is_base_of<nsWrapperCache, T>::value) {
      CheckWrapperCacheTracing<T>::Check(value);
    }
#endif
  }

#ifdef DEBUG
  const DOMJSClass* clasp = GetDOMClass(obj);
  // clasp can be null if the cache contained a non-DOM object.
  if (clasp) {
    // Some sanity asserts about our object.  Specifically:
    // 1)  If our class claims we're nsISupports, we better be nsISupports
    //     XXXbz ideally, we could assert that reinterpret_cast to nsISupports
    //     does the right thing, but I don't see a way to do it.  :(
    // 2)  If our class doesn't claim we're nsISupports we better be
    //     reinterpret_castable to nsWrapperCache.
    MOZ_ASSERT(clasp, "What happened here?");
    MOZ_ASSERT_IF(clasp->mDOMObjectIsISupports,
                  (std::is_base_of<nsISupports, T>::value));
    MOZ_ASSERT(CheckWrapperCacheCast<T>::Check());
  }
#endif

  rval.set(JS::ObjectValue(*obj));

  if (JS::GetCompartment(obj) == js::GetContextCompartment(cx)) {
    return TypeNeedsOuterization<T>::value ? TryToOuterize(rval) : true;
  }

  if (wrapBehavior == eDontWrapIntoContextCompartment) {
    if (TypeNeedsOuterization<T>::value) {
      JSAutoRealm ar(cx, obj);
      return TryToOuterize(rval);
    }

    return true;
  }

  return JS_WrapValue(cx, rval);
}

}  // namespace binding_detail

// Create a JSObject wrapping "value", if there isn't one already, and store it
// in rval.  "value" must be a concrete class that implements a
// GetWrapperPreserveColor() which can return its existing wrapper, if any, and
// a WrapObject() which will try to create a wrapper. Typically, this is done by
// having "value" inherit from nsWrapperCache.
//
// The value stored in rval will be ready to be exposed to whatever JS
// is running on cx right now.  In particular, it will be in the
// compartment of cx, and outerized as needed.
template <class T>
MOZ_ALWAYS_INLINE bool GetOrCreateDOMReflector(
    JSContext* cx, T* value, JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  using namespace binding_detail;
  return DoGetOrCreateDOMReflector<T, eWrapIntoContextCompartment>(
      cx, value, givenProto, rval);
}

// Like GetOrCreateDOMReflector but doesn't wrap into the context compartment,
// and hence does not actually require cx to be in a compartment.
template <class T>
MOZ_ALWAYS_INLINE bool GetOrCreateDOMReflectorNoWrap(
    JSContext* cx, T* value, JS::MutableHandle<JS::Value> rval) {
  using namespace binding_detail;
  return DoGetOrCreateDOMReflector<T, eDontWrapIntoContextCompartment>(
      cx, value, nullptr, rval);
}

// Helper for different overloadings of WrapNewBindingNonWrapperCachedObject()
inline bool FinishWrapping(JSContext* cx, JS::Handle<JSObject*> obj,
                           JS::MutableHandle<JS::Value> rval) {
  // We can end up here in all sorts of compartments, per comments in
  // WrapNewBindingNonWrapperCachedObject(). Make sure to JS_WrapValue!
  rval.set(JS::ObjectValue(*obj));
  return MaybeWrapObjectValue(cx, rval);
}

// Create a JSObject wrapping "value", for cases when "value" is a
// non-wrapper-cached object using WebIDL bindings.  "value" must implement a
// WrapObject() method taking a JSContext and a prototype (possibly null) and
// returning the resulting object via a MutableHandle<JSObject*> outparam.
template <class T>
inline bool WrapNewBindingNonWrapperCachedObject(
    JSContext* cx, JS::Handle<JSObject*> scopeArg, T* value,
    JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  static_assert(IsRefcounted<T>::value, "Don't pass owned classes in here.");
  MOZ_ASSERT(value);
  // We try to wrap in the realm of the underlying object of "scope"
  JS::Rooted<JSObject*> obj(cx);
  {
    // scope for the JSAutoRealm so that we restore the realm
    // before we call JS_WrapValue.
    Maybe<JSAutoRealm> ar;
    // Maybe<Handle> doesn't so much work, and in any case, adding
    // more Maybe (one for a Rooted and one for a Handle) adds more
    // code (and branches!) than just adding a single rooted.
    JS::Rooted<JSObject*> scope(cx, scopeArg);
    JS::Rooted<JSObject*> proto(cx, givenProto);
    if (js::IsWrapper(scope)) {
      // We are working in the Realm of cx and will be producing our reflector
      // there, so we need to succeed if that realm has access to the scope.
      scope =
          js::CheckedUnwrapDynamic(scope, cx, /* stopAtWindowProxy = */ false);
      if (!scope) return false;
      ar.emplace(cx, scope);
      if (!JS_WrapObject(cx, &proto)) {
        return false;
      }
    } else {
      // cx and scope are same-compartment, but they might still be
      // different-Realm.  Enter the Realm of scope, since that's
      // where we want to create our object.
      ar.emplace(cx, scope);
    }

    MOZ_ASSERT_IF(proto, js::IsObjectInContextCompartment(proto, cx));
    MOZ_ASSERT(js::IsObjectInContextCompartment(scope, cx));
    if (!value->WrapObject(cx, proto, &obj)) {
      return false;
    }
  }

  return FinishWrapping(cx, obj, rval);
}

// Create a JSObject wrapping "value", for cases when "value" is a
// non-wrapper-cached owned object using WebIDL bindings.  "value" must
// implement a WrapObject() method taking a taking a JSContext and a prototype
// (possibly null) and returning two pieces of information: the resulting object
// via a MutableHandle<JSObject*> outparam and a boolean return value that is
// true if the JSObject took ownership
template <class T>
inline bool WrapNewBindingNonWrapperCachedObject(
    JSContext* cx, JS::Handle<JSObject*> scopeArg, UniquePtr<T>& value,
    JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  static_assert(!IsRefcounted<T>::value, "Only pass owned classes in here.");
  // We do a runtime check on value, because otherwise we might in
  // fact end up wrapping a null and invoking methods on it later.
  if (!value) {
    MOZ_CRASH("Don't try to wrap null objects");
  }
  // We try to wrap in the realm of the underlying object of "scope"
  JS::Rooted<JSObject*> obj(cx);
  {
    // scope for the JSAutoRealm so that we restore the realm
    // before we call JS_WrapValue.
    Maybe<JSAutoRealm> ar;
    // Maybe<Handle> doesn't so much work, and in any case, adding
    // more Maybe (one for a Rooted and one for a Handle) adds more
    // code (and branches!) than just adding a single rooted.
    JS::Rooted<JSObject*> scope(cx, scopeArg);
    JS::Rooted<JSObject*> proto(cx, givenProto);
    if (js::IsWrapper(scope)) {
      // We are working in the Realm of cx and will be producing our reflector
      // there, so we need to succeed if that realm has access to the scope.
      scope =
          js::CheckedUnwrapDynamic(scope, cx, /* stopAtWindowProxy = */ false);
      if (!scope) return false;
      ar.emplace(cx, scope);
      if (!JS_WrapObject(cx, &proto)) {
        return false;
      }
    } else {
      // cx and scope are same-compartment, but they might still be
      // different-Realm.  Enter the Realm of scope, since that's
      // where we want to create our object.
      ar.emplace(cx, scope);
    }

    MOZ_ASSERT_IF(proto, js::IsObjectInContextCompartment(proto, cx));
    MOZ_ASSERT(js::IsObjectInContextCompartment(scope, cx));
    if (!value->WrapObject(cx, proto, &obj)) {
      return false;
    }

    // JS object took ownership
    (void)value.release();
  }

  return FinishWrapping(cx, obj, rval);
}

// Helper for smart pointers (nsRefPtr/nsCOMPtr).
template <template <typename> class SmartPtr, typename T,
          typename U = std::enable_if_t<IsRefcounted<T>::value, T>,
          typename V = std::enable_if_t<IsSmartPtr<SmartPtr<T>>::value, T>>
inline bool WrapNewBindingNonWrapperCachedObject(
    JSContext* cx, JS::Handle<JSObject*> scope, const SmartPtr<T>& value,
    JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  return WrapNewBindingNonWrapperCachedObject(cx, scope, value.get(), rval,
                                              givenProto);
}

// Helper for object references (as opposed to pointers).
template <typename T, typename U = std::enable_if_t<!IsSmartPtr<T>::value, T>>
inline bool WrapNewBindingNonWrapperCachedObject(
    JSContext* cx, JS::Handle<JSObject*> scope, T& value,
    JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  return WrapNewBindingNonWrapperCachedObject(cx, scope, &value, rval,
                                              givenProto);
}

template <bool Fatal>
inline bool EnumValueNotFound(BindingCallContext& cx, JS::Handle<JSString*> str,
                              const char* type, const char* sourceDescription);

template <>
inline bool EnumValueNotFound<false>(BindingCallContext& cx,
                                     JS::Handle<JSString*> str,
                                     const char* type,
                                     const char* sourceDescription) {
  // TODO: Log a warning to the console.
  return true;
}

template <>
inline bool EnumValueNotFound<true>(BindingCallContext& cx,
                                    JS::Handle<JSString*> str, const char* type,
                                    const char* sourceDescription) {
  JS::UniqueChars deflated = JS_EncodeStringToUTF8(cx, str);
  if (!deflated) {
    return false;
  }
  return cx.ThrowErrorMessage<MSG_INVALID_ENUM_VALUE>(sourceDescription,
                                                      deflated.get(), type);
}

namespace binding_detail {

template <typename CharT>
inline int FindEnumStringIndexImpl(const CharT* chars, size_t length,
                                   const Span<const nsLiteralCString>& values) {
  for (size_t i = 0; i < values.Length(); ++i) {
    const nsLiteralCString& value = values[i];
    if (length != value.Length()) {
      continue;
    }

    bool equal = true;
    for (size_t j = 0; j != length; ++j) {
      if (unsigned(value.CharAt(j)) != unsigned(chars[j])) {
        equal = false;
        break;
      }
    }

    if (equal) {
      return (int)i;
    }
  }

  return -1;
}

template <bool InvalidValueFatal>
inline bool FindEnumStringIndex(BindingCallContext& cx, JS::Handle<JS::Value> v,
                                const Span<const nsLiteralCString>& values,
                                const char* type, const char* sourceDescription,
                                int* index) {
  // JS_StringEqualsAscii is slow as molasses, so don't use it here.
  JS::Rooted<JSString*> str(cx, JS::ToString(cx, v));
  if (!str) {
    return false;
  }

  {
    size_t length;
    JS::AutoCheckCannotGC nogc;
    if (JS::StringHasLatin1Chars(str)) {
      const JS::Latin1Char* chars =
          JS_GetLatin1StringCharsAndLength(cx, nogc, str, &length);
      if (!chars) {
        return false;
      }
      *index = FindEnumStringIndexImpl(chars, length, values);
    } else {
      const char16_t* chars =
          JS_GetTwoByteStringCharsAndLength(cx, nogc, str, &length);
      if (!chars) {
        return false;
      }
      *index = FindEnumStringIndexImpl(chars, length, values);
    }
    if (*index >= 0) {
      return true;
    }
  }

  return EnumValueNotFound<InvalidValueFatal>(cx, str, type, sourceDescription);
}

}  // namespace binding_detail

template <typename Enum, class StringT>
inline Maybe<Enum> StringToEnum(const StringT& aString) {
  int index = binding_detail::FindEnumStringIndexImpl(
      aString.BeginReading(), aString.Length(),
      binding_detail::EnumStrings<Enum>::Values);
  return index >= 0 ? Some(static_cast<Enum>(index)) : Nothing();
}

template <typename Enum>
inline constexpr const nsLiteralCString& GetEnumString(Enum stringId) {
  MOZ_RELEASE_ASSERT(static_cast<size_t>(stringId) <
                     std::size(binding_detail::EnumStrings<Enum>::Values));
  return binding_detail::EnumStrings<Enum>::Values[static_cast<size_t>(
      stringId)];
}

template <typename Enum>
constexpr mozilla::detail::EnumeratedRange<Enum> MakeWebIDLEnumeratedRange() {
  return MakeInclusiveEnumeratedRange(ContiguousEnumValues<Enum>::min,
                                      ContiguousEnumValues<Enum>::max);
}

inline nsWrapperCache* GetWrapperCache(const ParentObject& aParentObject) {
  return aParentObject.mWrapperCache;
}

template <class T>
inline T* GetParentPointer(T* aObject) {
  return aObject;
}

inline nsISupports* GetParentPointer(const ParentObject& aObject) {
  return aObject.mObject;
}

template <typename T>
inline mozilla::dom::ReflectionScope GetReflectionScope(T* aParentObject) {
  return mozilla::dom::ReflectionScope::Content;
}

inline mozilla::dom::ReflectionScope GetReflectionScope(
    const ParentObject& aParentObject) {
  return aParentObject.mReflectionScope;
}

template <class T>
inline void ClearWrapper(T* p, nsWrapperCache* cache, JSObject* obj) {
  MOZ_ASSERT(cache->GetWrapperMaybeDead() == obj ||
             (js::RuntimeIsBeingDestroyed() && !cache->GetWrapperMaybeDead()));
  cache->ClearWrapper(obj);
}

template <class T>
inline void ClearWrapper(T* p, void*, JSObject* obj) {
  // QueryInterface to nsWrapperCache can't GC, we hope.
  JS::AutoSuppressGCAnalysis nogc;

  nsWrapperCache* cache;
  CallQueryInterface(p, &cache);
  ClearWrapper(p, cache, obj);
}

template <class T>
inline void UpdateWrapper(T* p, nsWrapperCache* cache, JSObject* obj,
                          const JSObject* old) {
  JS::AutoAssertGCCallback inCallback;
  cache->UpdateWrapper(obj, old);
}

template <class T>
inline void UpdateWrapper(T* p, void*, JSObject* obj, const JSObject* old) {
  JS::AutoAssertGCCallback inCallback;
  nsWrapperCache* cache;
  CallQueryInterface(p, &cache);
  UpdateWrapper(p, cache, obj, old);
}

// Attempt to preserve the wrapper, if any, for a Paris DOM bindings object.
// Return true if we successfully preserved the wrapper, or there is no wrapper
// to preserve. In the latter case we don't need to preserve the wrapper,
// because the object can only be obtained by JS once, or they cannot be
// meaningfully owned from the native side.
//
// This operation will return false only for non-nsISupports cycle-collected
// objects, because we cannot determine if they are wrappercached or not.
bool TryPreserveWrapper(JS::Handle<JSObject*> obj);

bool HasReleasedWrapper(JS::Handle<JSObject*> obj);

// Can only be called with a DOM JSClass.
bool InstanceClassHasProtoAtDepth(const JSClass* clasp, uint32_t protoID,
                                  uint32_t depth);

// Only set allowNativeWrapper to false if you really know you need it; if in
// doubt use true. Setting it to false disables security wrappers.
bool XPCOMObjectToJsval(JSContext* cx, JS::Handle<JSObject*> scope,
                        xpcObjectHelper& helper, const nsIID* iid,
                        bool allowNativeWrapper,
                        JS::MutableHandle<JS::Value> rval);

// Special-cased wrapping for variants
bool VariantToJsval(JSContext* aCx, nsIVariant* aVariant,
                    JS::MutableHandle<JS::Value> aRetval);

// Wrap an object "p" which is not using WebIDL bindings yet.  This _will_
// actually work on WebIDL binding objects that are wrappercached, but will be
// much slower than GetOrCreateDOMReflector.  "cache" must either be null or be
// the nsWrapperCache for "p".
template <class T>
inline bool WrapObject(JSContext* cx, T* p, nsWrapperCache* cache,
                       const nsIID* iid, JS::MutableHandle<JS::Value> rval) {
  if (xpc_FastGetCachedWrapper(cx, cache, rval)) return true;
  xpcObjectHelper helper(ToSupports(p), cache);
  JS::Rooted<JSObject*> scope(cx, JS::CurrentGlobalOrNull(cx));
  return XPCOMObjectToJsval(cx, scope, helper, iid, true, rval);
}

// A specialization of the above for nsIVariant, because that needs to
// do something different.
template <>
inline bool WrapObject<nsIVariant>(JSContext* cx, nsIVariant* p,
                                   nsWrapperCache* cache, const nsIID* iid,
                                   JS::MutableHandle<JS::Value> rval) {
  MOZ_ASSERT(iid);
  MOZ_ASSERT(iid->Equals(NS_GET_IID(nsIVariant)));
  return VariantToJsval(cx, p, rval);
}

// Wrap an object "p" which is not using WebIDL bindings yet.  Just like the
// variant that takes an nsWrapperCache above, but will try to auto-derive the
// nsWrapperCache* from "p".
template <class T>
inline bool WrapObject(JSContext* cx, T* p, const nsIID* iid,
                       JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p, GetWrapperCache(p), iid, rval);
}

// Just like the WrapObject above, but without requiring you to pick which
// interface you're wrapping as.  This should only be used for objects that have
// classinfo, for which it doesn't matter what IID is used to wrap.
template <class T>
inline bool WrapObject(JSContext* cx, T* p, JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p, nullptr, rval);
}

// Helper to make it possible to wrap directly out of an nsCOMPtr
template <class T>
inline bool WrapObject(JSContext* cx, const nsCOMPtr<T>& p, const nsIID* iid,
                       JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p.get(), iid, rval);
}

// Helper to make it possible to wrap directly out of an nsCOMPtr
template <class T>
inline bool WrapObject(JSContext* cx, const nsCOMPtr<T>& p,
                       JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p, nullptr, rval);
}

// Helper to make it possible to wrap directly out of an nsRefPtr
template <class T>
inline bool WrapObject(JSContext* cx, const RefPtr<T>& p, const nsIID* iid,
                       JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p.get(), iid, rval);
}

// Helper to make it possible to wrap directly out of an nsRefPtr
template <class T>
inline bool WrapObject(JSContext* cx, const RefPtr<T>& p,
                       JS::MutableHandle<JS::Value> rval) {
  return WrapObject(cx, p, nullptr, rval);
}

// Specialization to make it easy to use WrapObject in codegen.
template <>
inline bool WrapObject<JSObject>(JSContext* cx, JSObject* p,
                                 JS::MutableHandle<JS::Value> rval) {
  rval.set(JS::ObjectOrNullValue(p));
  return true;
}

inline bool WrapObject(JSContext* cx, JSObject& p,
                       JS::MutableHandle<JS::Value> rval) {
  rval.set(JS::ObjectValue(p));
  return true;
}

bool WrapObject(JSContext* cx, const WindowProxyHolder& p,
                JS::MutableHandle<JS::Value> rval);

// Given an object "p" that inherits from nsISupports, wrap it and return the
// result.  Null is returned on wrapping failure.  This is somewhat similar to
// WrapObject() above, but does NOT allow Xrays around the result, since we
// don't want those for our parent object.
template <typename T>
static inline JSObject* WrapNativeISupports(JSContext* cx, T* p,
                                            nsWrapperCache* cache) {
  JS::Rooted<JSObject*> retval(cx);
  {
    xpcObjectHelper helper(ToSupports(p), cache);
    JS::Rooted<JSObject*> scope(cx, JS::CurrentGlobalOrNull(cx));
    JS::Rooted<JS::Value> v(cx);
    retval = XPCOMObjectToJsval(cx, scope, helper, nullptr, false, &v)
                 ? v.toObjectOrNull()
                 : nullptr;
  }
  return retval;
}

// Wrapping of our native parent, for cases when it's a WebIDL object.
template <typename T, bool hasWrapObject = NativeHasMember<T>::WrapObject>
struct WrapNativeHelper {
  static inline JSObject* Wrap(JSContext* cx, T* parent,
                               nsWrapperCache* cache) {
    MOZ_ASSERT(cache);

    JSObject* obj;
    if ((obj = cache->GetWrapper())) {
      // GetWrapper always unmarks gray.
      JS::AssertObjectIsNotGray(obj);
      return obj;
    }

    // WrapObject never returns a gray thing.
    obj = parent->WrapObject(cx, nullptr);
    JS::AssertObjectIsNotGray(obj);

    return obj;
  }
};

// Wrapping of our native parent, for cases when it's not a WebIDL object.  In
// this case it must be nsISupports.
template <typename T>
struct WrapNativeHelper<T, false> {
  static inline JSObject* Wrap(JSContext* cx, T* parent,
                               nsWrapperCache* cache) {
    JSObject* obj;
    if (cache && (obj = cache->GetWrapper())) {
#ifdef DEBUG
      JS::Rooted<JSObject*> rootedObj(cx, obj);
      NS_ASSERTION(WrapNativeISupports(cx, parent, cache) == rootedObj,
                   "Unexpected object in nsWrapperCache");
      obj = rootedObj;
#endif
      JS::AssertObjectIsNotGray(obj);
      return obj;
    }

    obj = WrapNativeISupports(cx, parent, cache);
    JS::AssertObjectIsNotGray(obj);
    return obj;
  }
};

// Finding the associated global for an object.
template <typename T>
static inline JSObject* FindAssociatedGlobal(
    JSContext* cx, T* p, nsWrapperCache* cache,
    mozilla::dom::ReflectionScope scope =
        mozilla::dom::ReflectionScope::Content) {
  if (!p) {
    return JS::CurrentGlobalOrNull(cx);
  }

  JSObject* obj = WrapNativeHelper<T>::Wrap(cx, p, cache);
  if (!obj) {
    return nullptr;
  }
  JS::AssertObjectIsNotGray(obj);

  // The object is never a CCW but it may not be in the current compartment of
  // the JSContext.
  obj = JS::GetNonCCWObjectGlobal(obj);

  switch (scope) {
    case mozilla::dom::ReflectionScope::NAC: {
      return xpc::NACScope(obj);
    }

    case mozilla::dom::ReflectionScope::UAWidget: {
      // If scope is set to UAWidgetScope, it means that the canonical reflector
      // for this native object should live in the UA widget scope.
      if (xpc::IsInUAWidgetScope(obj)) {
        return obj;
      }
      JS::Rooted<JSObject*> rootedObj(cx, obj);
      JSObject* uaWidgetScope = xpc::GetUAWidgetScope(cx, rootedObj);
      MOZ_ASSERT_IF(uaWidgetScope, JS_IsGlobalObject(uaWidgetScope));
      JS::AssertObjectIsNotGray(uaWidgetScope);
      return uaWidgetScope;
    }

    case ReflectionScope::Content:
      return obj;
  }

  MOZ_CRASH("Unknown ReflectionScope variant");

  return nullptr;
}

// Finding of the associated global for an object, when we don't want to
// explicitly pass in things like the nsWrapperCache for it.
template <typename T>
static inline JSObject* FindAssociatedGlobal(JSContext* cx, const T& p) {
  return FindAssociatedGlobal(cx, GetParentPointer(p), GetWrapperCache(p),
                              GetReflectionScope(p));
}

// Specialization for the case of nsIGlobalObject, since in that case
// we can just get the JSObject* directly.
template <>
inline JSObject* FindAssociatedGlobal(JSContext* cx,
                                      nsIGlobalObject* const& p) {
  if (!p) {
    return JS::CurrentGlobalOrNull(cx);
  }

  JSObject* global = p->GetGlobalJSObject();
  if (!global) {
    // nsIGlobalObject doesn't have a JS object anymore,
    // fallback to the current global.
    return JS::CurrentGlobalOrNull(cx);
  }

  MOZ_ASSERT(JS_IsGlobalObject(global));
  JS::AssertObjectIsNotGray(global);
  return global;
}

// Helper for calling GetOrCreateDOMReflector with smart pointers
// (UniquePtr/RefPtr/nsCOMPtr) or references.
template <class T, bool isSmartPtr = IsSmartPtr<T>::value>
struct GetOrCreateDOMReflectorHelper {
  static inline bool GetOrCreate(JSContext* cx, const T& value,
                                 JS::Handle<JSObject*> givenProto,
                                 JS::MutableHandle<JS::Value> rval) {
    return GetOrCreateDOMReflector(cx, value.get(), rval, givenProto);
  }
};

template <class T>
struct GetOrCreateDOMReflectorHelper<T, false> {
  static inline bool GetOrCreate(JSContext* cx, T& value,
                                 JS::Handle<JSObject*> givenProto,
                                 JS::MutableHandle<JS::Value> rval) {
    static_assert(IsRefcounted<T>::value, "Don't pass owned classes in here.");
    return GetOrCreateDOMReflector(cx, &value, rval, givenProto);
  }
};

template <class T>
inline bool GetOrCreateDOMReflector(
    JSContext* cx, T& value, JS::MutableHandle<JS::Value> rval,
    JS::Handle<JSObject*> givenProto = nullptr) {
  return GetOrCreateDOMReflectorHelper<T>::GetOrCreate(cx, value, givenProto,
                                                       rval);
}

// Helper for calling GetOrCreateDOMReflectorNoWrap with smart pointers
// (UniquePtr/RefPtr/nsCOMPtr) or references.
template <class T, bool isSmartPtr = IsSmartPtr<T>::value>
struct GetOrCreateDOMReflectorNoWrapHelper {
  static inline bool GetOrCreate(JSContext* cx, const T& value,
                                 JS::MutableHandle<JS::Value> rval) {
    return GetOrCreateDOMReflectorNoWrap(cx, value.get(), rval);
  }
};

template <class T>
struct GetOrCreateDOMReflectorNoWrapHelper<T, false> {
  static inline bool GetOrCreate(JSContext* cx, T& value,
                                 JS::MutableHandle<JS::Value> rval) {
    return GetOrCreateDOMReflectorNoWrap(cx, &value, rval);
  }
};

template <class T>
inline bool GetOrCreateDOMReflectorNoWrap(JSContext* cx, T& value,
                                          JS::MutableHandle<JS::Value> rval) {
  return GetOrCreateDOMReflectorNoWrapHelper<T>::GetOrCreate(cx, value, rval);
}

template <class T>
inline JSObject* GetCallbackFromCallbackObject(JSContext* aCx, T* aObj) {
  return aObj->Callback(aCx);
}

// Helper for getting the callback JSObject* of a smart ptr around a
// CallbackObject or a reference to a CallbackObject or something like
// that.
template <class T, bool isSmartPtr = IsSmartPtr<T>::value>
struct GetCallbackFromCallbackObjectHelper {
  static inline JSObject* Get(JSContext* aCx, const T& aObj) {
    return GetCallbackFromCallbackObject(aCx, aObj.get());
  }
};

template <class T>
struct GetCallbackFromCallbackObjectHelper<T, false> {
  static inline JSObject* Get(JSContext* aCx, T& aObj) {
    return GetCallbackFromCallbackObject(aCx, &aObj);
  }
};

template <class T>
inline JSObject* GetCallbackFromCallbackObject(JSContext* aCx, T& aObj) {
  return GetCallbackFromCallbackObjectHelper<T>::Get(aCx, aObj);
}

static inline bool AtomizeAndPinJSString(JSContext* cx, jsid& id,
                                         const char* chars) {
  if (JSString* str = ::JS_AtomizeAndPinString(cx, chars)) {
    id = JS::PropertyKey::fromPinnedString(str);
    return true;
  }
  return false;
}

void GetInterfaceImpl(JSContext* aCx, nsIInterfaceRequestor* aRequestor,
                      nsWrapperCache* aCache, JS::Handle<JS::Value> aIID,
                      JS::MutableHandle<JS::Value> aRetval,
                      ErrorResult& aError);

template <class T>
void GetInterface(JSContext* aCx, T* aThis, JS::Handle<JS::Value> aIID,
                  JS::MutableHandle<JS::Value> aRetval, ErrorResult& aError) {
  GetInterfaceImpl(aCx, aThis, aThis, aIID, aRetval, aError);
}

bool ThrowingConstructor(JSContext* cx, unsigned argc, JS::Value* vp);

bool ThrowConstructorWithoutNew(JSContext* cx, const char* name);

// Helper for throwing an "invalid this" exception.
bool ThrowInvalidThis(JSContext* aCx, const JS::CallArgs& aArgs,
                      bool aSecurityError, prototypes::ID aProtoId);

bool GetPropertyOnPrototype(JSContext* cx, JS::Handle<JSObject*> proxy,
                            JS::Handle<JS::Value> receiver, JS::Handle<jsid> id,
                            bool* found, JS::MutableHandle<JS::Value> vp);

//
bool HasPropertyOnPrototype(JSContext* cx, JS::Handle<JSObject*> proxy,
                            JS::Handle<jsid> id, bool* has);

// Append the property names in "names" to "props". If
// shadowPrototypeProperties is false then skip properties that are also
// present on the proto chain of proxy.  If shadowPrototypeProperties is true,
// then the "proxy" argument is ignored.
bool AppendNamedPropertyIds(JSContext* cx, JS::Handle<JSObject*> proxy,
                            nsTArray<nsString>& names,
                            bool shadowPrototypeProperties,
                            JS::MutableHandleVector<jsid> props);

enum StringificationBehavior { eStringify, eEmpty, eNull };

static inline JSString* ConvertJSValueToJSString(JSContext* cx,
                                                 JS::Handle<JS::Value> v) {
  if (v.isString()) [[likely]] {
    return v.toString();
  }
  return JS::ToString(cx, v);
}

template <typename T>
static inline bool ConvertJSValueToString(
    JSContext* cx, JS::Handle<JS::Value> v,
    StringificationBehavior nullBehavior,
    StringificationBehavior undefinedBehavior, T& result) {
  JSString* s;
  if (v.isString()) {
    s = v.toString();
  } else {
    StringificationBehavior behavior;
    if (v.isNull()) {
      behavior = nullBehavior;
    } else if (v.isUndefined()) {
      behavior = undefinedBehavior;
    } else {
      behavior = eStringify;
    }

    if (behavior != eStringify) {
      if (behavior == eEmpty) {
        result.Truncate();
      } else {
        result.SetIsVoid(true);
      }
      return true;
    }

    s = JS::ToString(cx, v);
    if (!s) {
      return false;
    }
  }

  return AssignJSString(cx, result, s);
}

template <typename T>
static inline bool ConvertJSValueToString(
    JSContext* cx, JS::Handle<JS::Value> v,
    const char* /* unused sourceDescription */, T& result) {
  return ConvertJSValueToString(cx, v, eStringify, eStringify, result);
}

[[nodiscard]] bool NormalizeUSVString(nsAString&);

template <typename T>
static inline bool ConvertJSValueToUSVString(
    JSContext* cx, JS::Handle<JS::Value> v,
    const char* /* unused sourceDescription */, T& result) {
  if (!ConvertJSValueToString(cx, v, eStringify, eStringify, result)) {
    return false;
  }

  if (!NormalizeUSVString(result)) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

template <typename T>
inline bool ConvertIdToString(JSContext* cx, JS::Handle<JS::PropertyKey> id,
                              T& result, bool& isSymbol) {
  if (id.isString()) [[likely]] {
    if (!AssignJSString(cx, result, id.toString())) {
      return false;
    }
  } else if (id.isSymbol()) {
    isSymbol = true;
    return true;
  } else {
    JS::Rooted<JS::Value> nameVal(cx, js::IdToValue(id));
    if (!ConvertJSValueToString(cx, nameVal, eStringify, eStringify, result)) {
      return false;
    }
  }
  isSymbol = false;
  return true;
}

bool ConvertJSValueToByteString(BindingCallContext& cx, JS::Handle<JS::Value> v,
                                bool nullable, const char* sourceDescription,
                                nsACString& result);

inline bool ConvertJSValueToByteString(BindingCallContext& cx,
                                       JS::Handle<JS::Value> v,
                                       const char* sourceDescription,
                                       nsACString& result) {
  return ConvertJSValueToByteString(cx, v, false, sourceDescription, result);
}

template <typename T>
void DoTraceSequence(JSTracer* trc, FallibleTArray<T>& seq);
template <typename T>
void DoTraceSequence(JSTracer* trc, nsTArray<T>& seq);

// Class used to trace sequences, with specializations for various
// sequence types.
template <typename T, bool isDictionary = is_dom_dictionary<T>,
          bool isTypedArray = is_dom_typed_array<T>,
          bool isOwningUnion = is_dom_owning_union<T>>
class SequenceTracer {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated
};

// sequence<object> or sequence<object?>
template <>
class SequenceTracer<JSObject*, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, JSObject** objp, JSObject** end) {
    for (; objp != end; ++objp) {
      JS::TraceRoot(trc, objp, "sequence<object>");
    }
  }
};

// sequence<any>
template <>
class SequenceTracer<JS::Value, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, JS::Value* valp, JS::Value* end) {
    for (; valp != end; ++valp) {
      JS::TraceRoot(trc, valp, "sequence<any>");
    }
  }
};

// sequence<sequence<T>>
template <typename T>
class SequenceTracer<Sequence<T>, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, Sequence<T>* seqp,
                            Sequence<T>* end) {
    for (; seqp != end; ++seqp) {
      DoTraceSequence(trc, *seqp);
    }
  }
};

// sequence<sequence<T>> as return value
template <typename T>
class SequenceTracer<nsTArray<T>, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, nsTArray<T>* seqp,
                            nsTArray<T>* end) {
    for (; seqp != end; ++seqp) {
      DoTraceSequence(trc, *seqp);
    }
  }
};

// sequence<someDictionary>
template <typename T>
class SequenceTracer<T, true, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, T* dictp, T* end) {
    for (; dictp != end; ++dictp) {
      dictp->TraceDictionary(trc);
    }
  }
};

// sequence<SomeTypedArray>
template <typename T>
class SequenceTracer<T, false, true, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, T* arrayp, T* end) {
    for (; arrayp != end; ++arrayp) {
      arrayp->TraceSelf(trc);
    }
  }
};

// sequence<SomeOwningUnion>
template <typename T>
class SequenceTracer<T, false, false, true> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, T* arrayp, T* end) {
    for (; arrayp != end; ++arrayp) {
      arrayp->TraceUnion(trc);
    }
  }
};

// sequence<T?> with T? being a Nullable<T>
template <typename T>
class SequenceTracer<Nullable<T>, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, Nullable<T>* seqp,
                            Nullable<T>* end) {
    for (; seqp != end; ++seqp) {
      if (!seqp->IsNull()) {
        // Pretend like we actually have a length-one sequence here so
        // we can do template instantiation correctly for T.
        T& val = seqp->Value();
        T* ptr = &val;
        SequenceTracer<T>::TraceSequence(trc, ptr, ptr + 1);
      }
    }
  }
};

template <typename K, typename V>
void TraceRecord(JSTracer* trc, Record<K, V>& record) {
  for (auto& entry : record.Entries()) {
    // Act like it's a one-element sequence to leverage all that infrastructure.
    SequenceTracer<V>::TraceSequence(trc, &entry.mValue, &entry.mValue + 1);
  }
}

// sequence<record>
template <typename K, typename V>
class SequenceTracer<Record<K, V>, false, false, false> {
 public:
  explicit SequenceTracer() = delete;  // Should never be instantiated

  static void TraceSequence(JSTracer* trc, Record<K, V>* seqp,
                            Record<K, V>* end) {
    for (; seqp != end; ++seqp) {
      TraceRecord(trc, *seqp);
    }
  }
};

template <typename T>
void DoTraceSequence(JSTracer* trc, FallibleTArray<T>& seq) {
  SequenceTracer<T>::TraceSequence(trc, seq.Elements(),
                                   seq.Elements() + seq.Length());
}

template <typename T>
void DoTraceSequence(JSTracer* trc, nsTArray<T>& seq) {
  SequenceTracer<T>::TraceSequence(trc, seq.Elements(),
                                   seq.Elements() + seq.Length());
}

// Rooter class for sequences; this is what we mostly use in the codegen
template <typename T>
class MOZ_RAII SequenceRooter final : private JS::CustomAutoRooter {
 public:
  template <typename CX>
  SequenceRooter(const CX& cx, FallibleTArray<T>* aSequence)
      : JS::CustomAutoRooter(cx),
        mFallibleArray(aSequence),
        mSequenceType(eFallibleArray) {}

  template <typename CX>
  SequenceRooter(const CX& cx, nsTArray<T>* aSequence)
      : JS::CustomAutoRooter(cx),
        mInfallibleArray(aSequence),
        mSequenceType(eInfallibleArray) {}

  template <typename CX>
  SequenceRooter(const CX& cx, Nullable<nsTArray<T>>* aSequence)
      : JS::CustomAutoRooter(cx),
        mNullableArray(aSequence),
        mSequenceType(eNullableArray) {}

 private:
  enum SequenceType { eInfallibleArray, eFallibleArray, eNullableArray };

  virtual void trace(JSTracer* trc) override {
    if (mSequenceType == eFallibleArray) {
      DoTraceSequence(trc, *mFallibleArray);
    } else if (mSequenceType == eInfallibleArray) {
      DoTraceSequence(trc, *mInfallibleArray);
    } else {
      MOZ_ASSERT(mSequenceType == eNullableArray);
      if (!mNullableArray->IsNull()) {
        DoTraceSequence(trc, mNullableArray->Value());
      }
    }
  }

  union {
    nsTArray<T>* mInfallibleArray;
    FallibleTArray<T>* mFallibleArray;
    Nullable<nsTArray<T>>* mNullableArray;
  };

  SequenceType mSequenceType;
};

// Rooter class for Record; this is what we mostly use in the codegen.
template <typename K, typename V>
class MOZ_RAII RecordRooter final : private JS::CustomAutoRooter {
 public:
  template <typename CX>
  RecordRooter(const CX& cx, Record<K, V>* aRecord)
      : JS::CustomAutoRooter(cx), mRecord(aRecord), mRecordType(eRecord) {}

  template <typename CX>
  RecordRooter(const CX& cx, Nullable<Record<K, V>>* aRecord)
      : JS::CustomAutoRooter(cx),
        mNullableRecord(aRecord),
        mRecordType(eNullableRecord) {}

 private:
  enum RecordType { eRecord, eNullableRecord };

  virtual void trace(JSTracer* trc) override {
    if (mRecordType == eRecord) {
      TraceRecord(trc, *mRecord);
    } else {
      MOZ_ASSERT(mRecordType == eNullableRecord);
      if (!mNullableRecord->IsNull()) {
        TraceRecord(trc, mNullableRecord->Value());
      }
    }
  }

  union {
    Record<K, V>* mRecord;
    Nullable<Record<K, V>>* mNullableRecord;
  };

  RecordType mRecordType;
};

template <typename T>
class MOZ_RAII RootedUnion : public T, private JS::CustomAutoRooter {
 public:
  template <typename CX>
  explicit RootedUnion(const CX& cx) : T(), JS::CustomAutoRooter(cx) {}

  virtual void trace(JSTracer* trc) override { this->TraceUnion(trc); }
};

template <typename T>
class MOZ_STACK_CLASS NullableRootedUnion : public Nullable<T>,
                                            private JS::CustomAutoRooter {
 public:
  template <typename CX>
  explicit NullableRootedUnion(const CX& cx)
      : Nullable<T>(), JS::CustomAutoRooter(cx) {}

  virtual void trace(JSTracer* trc) override {
    if (!this->IsNull()) {
      this->Value().TraceUnion(trc);
    }
  }
};

inline bool AddStringToIDVector(JSContext* cx,
                                JS::MutableHandleVector<jsid> vector,
                                const char* name) {
  return vector.growBy(1) &&
         AtomizeAndPinJSString(cx, *(vector[vector.length() - 1]).address(),
                               name);
}

// We use one JSNative to represent all DOM interface objects (so we can easily
// detect when we need to wrap them in an Xray wrapper). A pointer to the
// relevant DOMInterfaceInfo is stored in the
// INTERFACE_OBJECT_INFO_RESERVED_SLOT slot of the JSFunction object for a
// specific interface object. We store the real JSNative and the
// NativeProperties in a JSNativeHolder, held by the DOMInterfaceInfo.
bool InterfaceObjectJSNative(JSContext* cx, unsigned argc, JS::Value* vp);

inline bool IsInterfaceObject(JSObject* obj) {
  return JS_IsNativeFunction(obj, InterfaceObjectJSNative);
}

inline const DOMInterfaceInfo* InterfaceInfoFromObject(JSObject* obj) {
  MOZ_ASSERT(IsInterfaceObject(obj));
  const JS::Value& v =
      js::GetFunctionNativeReserved(obj, INTERFACE_OBJECT_INFO_RESERVED_SLOT);
  return static_cast<const DOMInterfaceInfo*>(v.toPrivate());
}

inline const JSNativeHolder* NativeHolderFromInterfaceObject(JSObject* obj) {
  MOZ_ASSERT(IsInterfaceObject(obj));
  return &InterfaceInfoFromObject(obj)->nativeHolder;
}

// We use one JSNative to represent all legacy factory functions (so we can
// easily detect when we need to wrap them in an Xray wrapper). We store the
// real JSNative and the NativeProperties in a JSNativeHolder in a
// LegacyFactoryFunction in the LEGACY_FACTORY_FUNCTION_RESERVED_SLOT slot of
// the JSFunction object.
bool LegacyFactoryFunctionJSNative(JSContext* cx, unsigned argc, JS::Value* vp);

inline bool IsLegacyFactoryFunction(JSObject* obj) {
  return JS_IsNativeFunction(obj, LegacyFactoryFunctionJSNative);
}

inline const LegacyFactoryFunction* LegacyFactoryFunctionFromObject(
    JSObject* obj) {
  MOZ_ASSERT(IsLegacyFactoryFunction(obj));
  const JS::Value& v =
      js::GetFunctionNativeReserved(obj, LEGACY_FACTORY_FUNCTION_RESERVED_SLOT);
  return static_cast<const LegacyFactoryFunction*>(v.toPrivate());
}

inline const JSNativeHolder* NativeHolderFromLegacyFactoryFunction(
    JSObject* obj) {
  return &LegacyFactoryFunctionFromObject(obj)->mHolder;
}

inline const JSNativeHolder* NativeHolderFromObject(JSObject* obj) {
  return IsInterfaceObject(obj) ? NativeHolderFromInterfaceObject(obj)
                                : NativeHolderFromLegacyFactoryFunction(obj);
}

// Implementation of the bits that XrayWrapper needs

/**
 * This resolves operations, attributes and constants of the interfaces for obj.
 *
 * wrapper is the Xray JS object.
 * obj is the target object of the Xray, a binding's instance object or a
 *     interface or interface prototype object.
 */
bool XrayResolveOwnProperty(
    JSContext* cx, JS::Handle<JSObject*> wrapper, JS::Handle<JSObject*> obj,
    JS::Handle<jsid> id,
    JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc,
    bool& cacheOnHolder);

/**
 * Define a property on obj through an Xray wrapper.
 *
 * wrapper is the Xray JS object.
 * obj is the target object of the Xray, a binding's instance object or a
 *     interface or interface prototype object.
 * id and desc are the parameters for the property to be defined.
 * result is the out-parameter indicating success (read it only if
 *     this returns true and also sets *done to true).
 * done will be set to true if a property was set as a result of this call
 *      or if we want to always avoid setting this property
 *      (i.e. indexed properties on DOM objects)
 */
bool XrayDefineProperty(JSContext* cx, JS::Handle<JSObject*> wrapper,
                        JS::Handle<JSObject*> obj, JS::Handle<jsid> id,
                        JS::Handle<JS::PropertyDescriptor> desc,
                        JS::ObjectOpResult& result, bool* done);

/**
 * Add to props the property keys of all indexed or named properties of obj and
 * operations, attributes and constants of the interfaces for obj.
 *
 * wrapper is the Xray JS object.
 * obj is the target object of the Xray, a binding's instance object or a
 *     interface or interface prototype object.
 * flags are JSITER_* flags.
 */
bool XrayOwnPropertyKeys(JSContext* cx, JS::Handle<JSObject*> wrapper,
                         JS::Handle<JSObject*> obj, unsigned flags,
                         JS::MutableHandleVector<jsid> props);

/**
 * Returns the prototype to use for an Xray for a DOM object, wrapped in cx's
 * compartment. This always returns the prototype that would be used for a DOM
 * object if we ignore any changes that might have been done to the prototype
 * chain by JS, the XBL code or plugins.
 *
 * cx should be in the Xray's compartment.
 * obj is the target object of the Xray, a binding's instance object or an
 *     interface or interface prototype object.
 */
inline bool XrayGetNativeProto(JSContext* cx, JS::Handle<JSObject*> obj,
                               JS::MutableHandle<JSObject*> protop) {
  JS::Rooted<JSObject*> global(cx, JS::GetNonCCWObjectGlobal(obj));
  {
    JSAutoRealm ar(cx, global);
    const DOMJSClass* domClass = GetDOMClass(obj);
    if (domClass) {
      ProtoHandleGetter protoGetter = domClass->mGetProto;
      if (protoGetter) {
        protop.set(protoGetter(cx));
      } else {
        protop.set(JS::GetRealmObjectPrototype(cx));
      }
    } else if (JS_ObjectIsFunction(obj)) {
      if (IsLegacyFactoryFunction(obj)) {
        protop.set(JS::GetRealmFunctionPrototype(cx));
      } else {
        protop.set(InterfaceInfoFromObject(obj)->mGetParentProto(cx));
      }
    } else {
      const JSClass* clasp = JS::GetClass(obj);
      MOZ_ASSERT(IsDOMIfaceAndProtoClass(clasp));
      ProtoGetter protoGetter =
          DOMIfaceAndProtoJSClass::FromJSClass(clasp)->mGetParentProto;
      protop.set(protoGetter(cx));
    }
  }

  return JS_WrapObject(cx, protop);
}

/**
 * Get the Xray expando class to use for the given DOM object.
 */
const JSClass* XrayGetExpandoClass(JSContext* cx, JS::Handle<JSObject*> obj);

/**
 * Delete a named property, if any.  Return value is false if exception thrown,
 * true otherwise.  The caller should not do any more work after calling this
 * function, because it has no way whether a deletion was performed and hence
 * opresult already has state set on it.  If callers ever need to change that,
 * add a "bool* found" argument and change the generated DeleteNamedProperty to
 * use it instead of a local variable.
 */
bool XrayDeleteNamedProperty(JSContext* cx, JS::Handle<JSObject*> wrapper,
                             JS::Handle<JSObject*> obj, JS::Handle<jsid> id,
                             JS::ObjectOpResult& opresult);

namespace binding_detail {

// Default implementations of the NativePropertyHooks' mResolveOwnProperty and
// mEnumerateOwnProperties for WebIDL bindings implemented as proxies.
bool ResolveOwnProperty(
    JSContext* cx, JS::Handle<JSObject*> wrapper, JS::Handle<JSObject*> obj,
    JS::Handle<jsid> id,
    JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc);
bool EnumerateOwnProperties(JSContext* cx, JS::Handle<JSObject*> wrapper,
                            JS::Handle<JSObject*> obj,
                            JS::MutableHandleVector<jsid> props);

}  // namespace binding_detail

/**
 * Get the object which should be used to cache the return value of a property
 * getter in the case of a [Cached] or [StoreInSlot] property.  `obj` is the
 * `this` value for our property getter that we're working with.
 *
 * This function can return null on failure to allocate the object, throwing on
 * the JSContext in the process.
 *
 * The isXray outparam will be set to true if obj is an Xray and false
 * otherwise.
 *
 * Note that the Slow version should only be called from
 * GetCachedSlotStorageObject.
 */
JSObject* GetCachedSlotStorageObjectSlow(JSContext* cx,
                                         JS::Handle<JSObject*> obj,
                                         bool* isXray);

inline JSObject* GetCachedSlotStorageObject(JSContext* cx,
                                            JS::Handle<JSObject*> obj,
                                            bool* isXray) {
  if (IsDOMObject(obj)) {
    *isXray = false;
    return obj;
  }

  return GetCachedSlotStorageObjectSlow(cx, obj, isXray);
}

extern NativePropertyHooks sEmptyNativePropertyHooks;

inline bool IsDOMConstructor(JSObject* obj) {
  return IsInterfaceObject(obj) || IsLegacyFactoryFunction(obj);
}

inline bool UseDOMXray(JSObject* obj) {
  const JSClass* clasp = JS::GetClass(obj);
  return IsDOMClass(clasp) || IsDOMConstructor(obj) ||
         IsDOMIfaceAndProtoClass(clasp);
}

// Helpers for creating a const version of a type.
template <typename T>
const T& Constify(T& arg) {
  return arg;
}

// Helper for turning (Owning)NonNull<T> into T&
template <typename T>
T& NonNullHelper(T& aArg) {
  return aArg;
}

template <typename T>
T& NonNullHelper(NonNull<T>& aArg) {
  return aArg;
}

template <typename T>
const T& NonNullHelper(const NonNull<T>& aArg) {
  return aArg;
}

template <typename T>
T& NonNullHelper(OwningNonNull<T>& aArg) {
  return aArg;
}

template <typename T>
const T& NonNullHelper(const OwningNonNull<T>& aArg) {
  return aArg;
}

// These overloads are here to make sure that we never end up applying
// NonNullHelper to a NonNull<nsAutoString>. If we try to, it should fail to
// compile, since presumably the caller will try to use our nonexistent return
// value.
template <typename CharT>
inline void NonNullHelper(NonNull<nsTAutoString<CharT>>&) {}
template <typename CharT>
inline void NonNullHelper(const NonNull<nsTAutoString<CharT>>&) {}
template <typename CharT>
inline void NonNullHelper(nsTAutoString<CharT>&) {}

template <typename CharT>
MOZ_ALWAYS_INLINE const nsTSubstring<CharT>& NonNullHelper(
    const nsTAutoString<CharT>& aArg) {
  return aArg;
}

// Helper for lenient getters/setters to report to console.  If this
// returns false, we couldn't even get a global.
bool ReportLenientThisUnwrappingFailure(JSContext* cx, JSObject* obj);

// Given a JSObject* that represents the chrome side of a JS-implemented WebIDL
// interface, get the nsIGlobalObject corresponding to the content side, if any.
// A false return means an exception was thrown.
bool GetContentGlobalForJSImplementedObject(BindingCallContext& cx,
                                            JS::Handle<JSObject*> obj,
                                            nsIGlobalObject** global);

void ConstructJSImplementation(const char* aContractId,
                               nsIGlobalObject* aGlobal,
                               JS::MutableHandle<JSObject*> aObject,
                               ErrorResult& aRv);

// XXX Avoid pulling in the whole ScriptSettings.h, however there should be a
// unique declaration of this function somewhere else.
JS::RootingContext* RootingCx();

template <typename T>
already_AddRefed<T> ConstructJSImplementation(const char* aContractId,
                                              nsIGlobalObject* aGlobal,
                                              ErrorResult& aRv) {
  JS::RootingContext* cx = RootingCx();
  JS::Rooted<JSObject*> jsImplObj(cx);
  ConstructJSImplementation(aContractId, aGlobal, &jsImplObj, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(!js::IsWrapper(jsImplObj));
  JS::Rooted<JSObject*> jsImplGlobal(cx, JS::GetNonCCWObjectGlobal(jsImplObj));
  RefPtr<T> newObj = new T(jsImplObj, jsImplGlobal, aGlobal);
  return newObj.forget();
}

template <typename T>
already_AddRefed<T> ConstructJSImplementation(const char* aContractId,
                                              const GlobalObject& aGlobal,
                                              ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return ConstructJSImplementation<T>(aContractId, global, aRv);
}

/**
 * Convert an nsCString to jsval, returning true on success.
 * These functions are intended for ByteString implementations.
 * As such, the string is not UTF-8 encoded.  Any UTF8 strings passed to these
 * methods will be mangled.
 */
inline bool NonVoidByteStringToJsval(JSContext* cx, const nsACString& str,
                                     JS::MutableHandle<JS::Value> rval) {
  return xpc::NonVoidLatin1StringToJsval(cx, str, rval);
}
inline bool ByteStringToJsval(JSContext* cx, const nsACString& str,
                              JS::MutableHandle<JS::Value> rval) {
  if (str.IsVoid()) {
    rval.setNull();
    return true;
  }
  return NonVoidByteStringToJsval(cx, str, rval);
}

// Convert an utf-8 encoded nsCString to jsval, returning true on success.
//
// TODO(bug 1606957): This could probably be better.
inline bool NonVoidUTF8StringToJsval(JSContext* cx, const nsACString& str,
                                     JS::MutableHandle<JS::Value> rval) {
  return xpc::NonVoidUTF8StringToJsval(cx, str, rval);
}

inline bool UTF8StringToJsval(JSContext* cx, const nsACString& str,
                              JS::MutableHandle<JS::Value> rval) {
  if (str.IsVoid()) {
    rval.setNull();
    return true;
  }
  return NonVoidUTF8StringToJsval(cx, str, rval);
}

template <class T, bool isISupports = std::is_base_of<nsISupports, T>::value>
struct PreserveWrapperHelper {
  static void PreserveWrapper(T* aObject) {
    aObject->PreserveWrapper(aObject, NS_CYCLE_COLLECTION_PARTICIPANT(T));
  }
};

template <class T>
struct PreserveWrapperHelper<T, true> {
  static void PreserveWrapper(T* aObject) {
    aObject->PreserveWrapper(reinterpret_cast<nsISupports*>(aObject));
  }
};

template <class T>
void PreserveWrapper(T* aObject) {
  PreserveWrapperHelper<T>::PreserveWrapper(aObject);
}

template <class T, bool isISupports = std::is_base_of<nsISupports, T>::value>
struct CastingAssertions {
  static bool ToSupportsIsCorrect(T*) { return true; }
  static bool ToSupportsIsOnPrimaryInheritanceChain(T*, nsWrapperCache*) {
    return true;
  }
};

template <class T>
struct CastingAssertions<T, true> {
  static bool ToSupportsIsCorrect(T* aObject) {
    return ToSupports(aObject) == reinterpret_cast<nsISupports*>(aObject);
  }
  static bool ToSupportsIsOnPrimaryInheritanceChain(T* aObject,
                                                    nsWrapperCache* aCache) {
    return reinterpret_cast<void*>(aObject) != aCache;
  }
};

template <class T>
bool ToSupportsIsCorrect(T* aObject) {
  return CastingAssertions<T>::ToSupportsIsCorrect(aObject);
}

template <class T>
bool ToSupportsIsOnPrimaryInheritanceChain(T* aObject, nsWrapperCache* aCache) {
  return CastingAssertions<T>::ToSupportsIsOnPrimaryInheritanceChain(aObject,
                                                                     aCache);
}

// Get the size of allocated memory to associate with a binding JSObject for a
// native object. This is supplied to the JS engine to allow it to schedule GC
// when necessary.
//
// This function supplies a default value and is overloaded for specific native
// object types.
inline size_t BindingJSObjectMallocBytes(void* aNativePtr) { return 0; }

// The BindingJSObjectCreator class is supposed to be used by a caller that
// wants to create and initialise a binding JSObject. After initialisation has
// been successfully completed it should call InitializationSucceeded().
// The BindingJSObjectCreator object will root the JSObject until
// InitializationSucceeded() is called on it. If the native object for the
// binding is refcounted it will also hold a strong reference to it, that
// reference is transferred to the JSObject (which holds the native in a slot)
// when InitializationSucceeded() is called. If the BindingJSObjectCreator
// object is destroyed and InitializationSucceeded() was never called on it then
// the JSObject's slot holding the native will be set to undefined, and for a
// refcounted native the strong reference will be released.
template <class T>
class MOZ_STACK_CLASS BindingJSObjectCreator {
 public:
  explicit BindingJSObjectCreator(JSContext* aCx) : mReflector(aCx) {}

  ~BindingJSObjectCreator() {
    if (mReflector) {
      JS::SetReservedSlot(mReflector, DOM_OBJECT_SLOT, JS::UndefinedValue());
    }
  }

  void CreateProxyObject(JSContext* aCx, const JSClass* aClass,
                         const DOMProxyHandler* aHandler,
                         JS::Handle<JSObject*> aProto, bool aLazyProto,
                         T* aNative, JS::Handle<JS::Value> aExpandoValue,
                         JS::MutableHandle<JSObject*> aReflector) {
    js::ProxyOptions options;
    options.setClass(aClass);
    options.setLazyProto(aLazyProto);

    aReflector.set(
        js::NewProxyObject(aCx, aHandler, aExpandoValue, aProto, options));
    if (aReflector) {
      js::SetProxyReservedSlot(aReflector, DOM_OBJECT_SLOT,
                               JS::PrivateValue(aNative));
      mNative = aNative;
      mReflector = aReflector;

      if (size_t mallocBytes = BindingJSObjectMallocBytes(aNative)) {
        JS::AddAssociatedMemory(aReflector, mallocBytes,
                                JS::MemoryUse::DOMBinding);
      }
    }
  }

  void CreateObject(JSContext* aCx, const JSClass* aClass,
                    JS::Handle<JSObject*> aProto, T* aNative,
                    JS::MutableHandle<JSObject*> aReflector) {
    aReflector.set(
        JS_NewObjectWithGivenProtoAndUseAllocSite(aCx, aClass, aProto));
    if (aReflector) {
      JS::SetReservedSlot(aReflector, DOM_OBJECT_SLOT,
                          JS::PrivateValue(aNative));
      mNative = aNative;
      mReflector = aReflector;

      if (size_t mallocBytes = BindingJSObjectMallocBytes(aNative)) {
        JS::AddAssociatedMemory(aReflector, mallocBytes,
                                JS::MemoryUse::DOMBinding);
      }
    }
  }

  void InitializationSucceeded() {
    T* pointer;
    mNative.forget(&pointer);
    mReflector = nullptr;
  }

 private:
  struct OwnedNative {
    // Make sure the native objects inherit from NonRefcountedDOMObject so
    // that we log their ctor and dtor.
    static_assert(std::is_base_of<NonRefcountedDOMObject, T>::value,
                  "Non-refcounted objects with DOM bindings should inherit "
                  "from NonRefcountedDOMObject.");

    OwnedNative& operator=(T* aNative) {
      mNative = aNative;
      return *this;
    }

    // This signature sucks, but it's the only one that will make a nsRefPtr
    // just forget about its pointer without warning.
    void forget(T** aResult) {
      *aResult = mNative;
      mNative = nullptr;
    }

    // Keep track of the pointer for use in InitializationSucceeded().
    // The caller (or, after initialization succeeds, the JS object) retains
    // ownership of the object.
    T* mNative;
  };

  JS::Rooted<JSObject*> mReflector;
  std::conditional_t<IsRefcounted<T>::value, RefPtr<T>, OwnedNative> mNative;
};

template <class T>
struct DeferredFinalizerImpl {
  using SmartPtr = std::conditional_t<
      std::is_same_v<T, nsISupports>, nsCOMPtr<T>,
      std::conditional_t<IsRefcounted<T>::value, RefPtr<T>, UniquePtr<T>>>;
  typedef SegmentedVector<SmartPtr> SmartPtrArray;

  static_assert(
      std::is_same_v<T, nsISupports> || !std::is_base_of<nsISupports, T>::value,
      "nsISupports classes should all use the nsISupports instantiation");

  static inline void AppendAndTake(
      SegmentedVector<nsCOMPtr<nsISupports>>& smartPtrArray, nsISupports* ptr) {
    smartPtrArray.InfallibleAppend(dont_AddRef(ptr));
  }
  template <class U>
  static inline void AppendAndTake(SegmentedVector<RefPtr<U>>& smartPtrArray,
                                   U* ptr) {
    smartPtrArray.InfallibleAppend(dont_AddRef(ptr));
  }
  template <class U>
  static inline void AppendAndTake(SegmentedVector<UniquePtr<U>>& smartPtrArray,
                                   U* ptr) {
    smartPtrArray.InfallibleAppend(ptr);
  }

  static void* AppendDeferredFinalizePointer(void* aData, void* aObject) {
    SmartPtrArray* pointers = static_cast<SmartPtrArray*>(aData);
    if (!pointers) {
      pointers = new SmartPtrArray();
    }
    AppendAndTake(*pointers, static_cast<T*>(aObject));
    return pointers;
  }
  static bool DeferredFinalize(uint32_t aSlice, void* aData) {
    MOZ_ASSERT(aSlice > 0, "nonsensical/useless call with aSlice == 0");
    SmartPtrArray* pointers = static_cast<SmartPtrArray*>(aData);
    pointers->PopLastN(aSlice);
    if (pointers->IsEmpty()) {
      delete pointers;
      return true;
    }
    return false;
  }
};

template <class T, bool isISupports = std::is_base_of<nsISupports, T>::value>
struct DeferredFinalizer {
  static void AddForDeferredFinalization(T* aObject) {
    typedef DeferredFinalizerImpl<T> Impl;
    DeferredFinalize(Impl::AppendDeferredFinalizePointer,
                     Impl::DeferredFinalize, aObject);
  }
};

template <class T>
struct DeferredFinalizer<T, true> {
  static void AddForDeferredFinalization(T* aObject) {
    DeferredFinalize(reinterpret_cast<nsISupports*>(aObject));
  }
};

template <class T>
static void AddForDeferredFinalization(T* aObject) {
  DeferredFinalizer<T>::AddForDeferredFinalization(aObject);
}

// This returns T's CC participant if it participates in CC and does not inherit
// from nsISupports. Otherwise, it returns null. QI should be used to get the
// participant if T inherits from nsISupports.
template <class T, bool isISupports = std::is_base_of<nsISupports, T>::value>
class GetCCParticipant {
  // Helper for GetCCParticipant for classes that participate in CC.
  template <class U>
  static constexpr nsCycleCollectionParticipant* GetHelper(
      int, typename U::NS_CYCLE_COLLECTION_INNERCLASS* dummy = nullptr) {
    return T::NS_CYCLE_COLLECTION_INNERCLASS::GetParticipant();
  }
  // Helper for GetCCParticipant for classes that don't participate in CC.
  template <class U>
  static constexpr nsCycleCollectionParticipant* GetHelper(double) {
    return nullptr;
  }

 public:
  static constexpr nsCycleCollectionParticipant* Get() {
    // Passing int() here will try to call the GetHelper that takes an int as
    // its first argument. If T doesn't participate in CC then substitution for
    // the second argument (with a default value) will fail and because of
    // SFINAE the next best match (the variant taking a double) will be called.
    return GetHelper<T>(int());
  }
};

template <class T>
class GetCCParticipant<T, true> {
 public:
  static constexpr nsCycleCollectionParticipant* Get() { return nullptr; }
};

void FinalizeGlobal(JS::GCContext* aGcx, JSObject* aObj);

bool ResolveGlobal(JSContext* aCx, JS::Handle<JSObject*> aObj,
                   JS::Handle<jsid> aId, bool* aResolvedp);

bool MayResolveGlobal(const JSAtomState& aNames, jsid aId, JSObject* aMaybeObj);

bool EnumerateGlobal(JSContext* aCx, JS::Handle<JSObject*> aObj,
                     JS::MutableHandleVector<jsid> aProperties,
                     bool aEnumerableOnly);

struct CreateGlobalOptionsGeneric {
  static void TraceGlobal(JSTracer* aTrc, JSObject* aObj) {
    mozilla::dom::TraceProtoAndIfaceCache(aTrc, aObj);
  }
  static bool PostCreateGlobal(JSContext* aCx, JS::Handle<JSObject*> aGlobal) {
    MOZ_ALWAYS_TRUE(TryPreserveWrapper(aGlobal));

    return true;
  }
};

struct CreateGlobalOptionsWithXPConnect {
  static void TraceGlobal(JSTracer* aTrc, JSObject* aObj);
  static bool PostCreateGlobal(JSContext* aCx, JS::Handle<JSObject*> aGlobal);
};

template <class T>
using IsGlobalWithXPConnect =
    std::disjunction<std::is_base_of<nsGlobalWindowInner, T>,
                     std::is_base_of<MessageManagerGlobal, T>>;

template <class T>
struct CreateGlobalOptions
    : std::conditional_t<IsGlobalWithXPConnect<T>::value,
                         CreateGlobalOptionsWithXPConnect,
                         CreateGlobalOptionsGeneric> {
  static constexpr ProtoAndIfaceCache::Kind ProtoAndIfaceCacheKind =
      ProtoAndIfaceCache::NonWindowLike;
};

template <>
struct CreateGlobalOptions<nsGlobalWindowInner>
    : public CreateGlobalOptionsWithXPConnect {
  static constexpr ProtoAndIfaceCache::Kind ProtoAndIfaceCacheKind =
      ProtoAndIfaceCache::WindowLike;
};

uint64_t GetWindowID(void* aGlobal);
uint64_t GetWindowID(nsGlobalWindowInner* aGlobal);
uint64_t GetWindowID(DedicatedWorkerGlobalScope* aGlobal);

// The return value is true if we created and successfully performed our part of
// the setup for the global, false otherwise.
//
// Typically this method's caller will want to ensure that
// xpc::InitGlobalObjectOptions is called before, and xpc::InitGlobalObject is
// called after, this method, to ensure that this global object and its
// compartment are consistent with other global objects.
template <class T, ProtoHandleGetter GetProto>
bool CreateGlobal(JSContext* aCx, T* aNative, nsWrapperCache* aCache,
                  const JSClass* aClass, JS::RealmOptions& aOptions,
                  JSPrincipals* aPrincipal,
                  JS::MutableHandle<JSObject*> aGlobal) {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Create global object", JS);
  aOptions.creationOptions()
      .setTrace(CreateGlobalOptions<T>::TraceGlobal)
      .setProfilerRealmID(GetWindowID(aNative));
  xpc::SetPrefableRealmOptions(aOptions);

  aGlobal.set(JS_NewGlobalObject(aCx, aClass, aPrincipal,
                                 JS::DontFireOnNewGlobalHook, aOptions));
  if (!aGlobal) {
    NS_WARNING("Failed to create global");
    return false;
  }

  JSAutoRealm ar(aCx, aGlobal);

  {
    JS::SetReservedSlot(aGlobal, DOM_OBJECT_SLOT, JS::PrivateValue(aNative));
    NS_ADDREF(aNative);

    aCache->SetWrapper(aGlobal);

    dom::AllocateProtoAndIfaceCache(
        aGlobal, CreateGlobalOptions<T>::ProtoAndIfaceCacheKind);

    if (!CreateGlobalOptions<T>::PostCreateGlobal(aCx, aGlobal)) {
      return false;
    }

    // Initializing this at this point for nsGlobalWindowInner makes no sense,
    // because GetRTPCallerType doesn't return the correct result before
    // the global is completely initialized with a document.
    if constexpr (!std::is_base_of_v<nsGlobalWindowInner, T>) {
      JS::SetRealmReduceTimerPrecisionCallerType(
          js::GetNonCCWObjectRealm(aGlobal),
          RTPCallerTypeToToken(aNative->GetRTPCallerType()));
    }
  }

  JS::Handle<JSObject*> proto = GetProto(aCx);
  if (!proto || !JS_SetPrototype(aCx, aGlobal, proto)) {
    NS_WARNING("Failed to set proto");
    return false;
  }

  bool succeeded;
  if (!JS_SetImmutablePrototype(aCx, aGlobal, &succeeded)) {
    return false;
  }
  MOZ_ASSERT(succeeded,
             "making a fresh global object's [[Prototype]] immutable can "
             "internally fail, but it should never be unsuccessful");

  return true;
}

namespace binding_detail {
/**
 * WebIDL getters have a "generic" JSNative that is responsible for the
 * following things:
 *
 * 1) Determining the "this" pointer for the C++ call.
 * 2) Extracting the "specialized" getter from the jitinfo on the JSFunction.
 * 3) Calling the specialized getter.
 * 4) Handling exceptions as needed.
 *
 * There are several variants of (1) depending on the interface involved and
 * there are two variants of (4) depending on whether the return type is a
 * Promise.  We handle this by templating our generic getter on a
 * this-determination policy and an exception handling policy, then explicitly
 * instantiating the relevant template specializations.
 */
template <typename ThisPolicy, typename ExceptionPolicy>
bool GenericGetter(JSContext* cx, unsigned argc, JS::Value* vp);

/**
 * WebIDL setters have a "generic" JSNative that is responsible for the
 * following things:
 *
 * 1) Determining the "this" pointer for the C++ call.
 * 2) Extracting the "specialized" setter from the jitinfo on the JSFunction.
 * 3) Calling the specialized setter.
 *
 * There are several variants of (1) depending on the interface
 * involved.  We handle this by templating our generic setter on a
 * this-determination policy, then explicitly instantiating the
 * relevant template specializations.
 */
template <typename ThisPolicy>
bool GenericSetter(JSContext* cx, unsigned argc, JS::Value* vp);

/**
 * WebIDL methods have a "generic" JSNative that is responsible for the
 * following things:
 *
 * 1) Determining the "this" pointer for the C++ call.
 * 2) Extracting the "specialized" method from the jitinfo on the JSFunction.
 * 3) Calling the specialized methodx.
 * 4) Handling exceptions as needed.
 *
 * There are several variants of (1) depending on the interface involved and
 * there are two variants of (4) depending on whether the return type is a
 * Promise.  We handle this by templating our generic method on a
 * this-determination policy and an exception handling policy, then explicitly
 * instantiating the relevant template specializations.
 */
template <typename ThisPolicy, typename ExceptionPolicy>
bool GenericMethod(JSContext* cx, unsigned argc, JS::Value* vp);

// A this-extraction policy for normal getters/setters/methods.
struct NormalThisPolicy;

// A this-extraction policy for getters/setters/methods on interfaces
// that are on some global's proto chain.
struct MaybeGlobalThisPolicy;

// A this-extraction policy for lenient getters/setters.
struct LenientThisPolicy;

// A this-extraction policy for cross-origin getters/setters/methods.
struct CrossOriginThisPolicy;

// A this-extraction policy for getters/setters/methods that should
// not be allowed to be called cross-origin but expect objects that
// _can_ be cross-origin.
struct MaybeCrossOriginObjectThisPolicy;

// A this-extraction policy which is just like
// MaybeCrossOriginObjectThisPolicy but has lenient-this behavior.
struct MaybeCrossOriginObjectLenientThisPolicy;

// An exception-reporting policy for normal getters/setters/methods.
struct ThrowExceptions;

// An exception-handling policy for Promise-returning getters/methods.
struct ConvertExceptionsToPromises;
}  // namespace binding_detail

bool StaticMethodPromiseWrapper(JSContext* cx, unsigned argc, JS::Value* vp);

// ConvertExceptionToPromise should only be called when we have an error
// condition (e.g. returned false from a JSAPI method).  Note that there may be
// no exception on cx, in which case this is an uncatchable failure that will
// simply be propagated.  Otherwise this method will attempt to convert the
// exception to a Promise rejected with the exception that it will store in
// rval.
bool ConvertExceptionToPromise(JSContext* cx,
                               JS::MutableHandle<JS::Value> rval);

#ifdef DEBUG
void AssertReturnTypeMatchesJitinfo(const JSJitInfo* aJitinfo,
                                    JS::Handle<JS::Value> aValue);
#endif

bool CallerSubsumes(JSObject* aObject);

MOZ_ALWAYS_INLINE bool CallerSubsumes(JS::Handle<JS::Value> aValue) {
  if (!aValue.isObject()) {
    return true;
  }
  return CallerSubsumes(&aValue.toObject());
}

template <class T, class S>
inline RefPtr<T> StrongOrRawPtr(already_AddRefed<S>&& aPtr) {
  return std::move(aPtr);
}

template <class T, class S>
inline RefPtr<T> StrongOrRawPtr(RefPtr<S>&& aPtr) {
  return std::move(aPtr);
}

template <class T, typename = std::enable_if_t<IsRefcounted<T>::value>>
inline T* StrongOrRawPtr(T* aPtr) {
  return aPtr;
}

template <class T, class S,
          typename = std::enable_if_t<!IsRefcounted<S>::value>>
inline UniquePtr<T> StrongOrRawPtr(UniquePtr<S>&& aPtr) {
  return std::move(aPtr);
}

template <class T, template <typename> class SmartPtr, class S>
inline void StrongOrRawPtr(SmartPtr<S>&& aPtr) = delete;

template <class T>
using StrongPtrForMember =
    std::conditional_t<IsRefcounted<T>::value, RefPtr<T>, UniquePtr<T>>;

namespace binding_detail {
inline JSObject* GetHackedNamespaceProtoObject(JSContext* aCx) {
  return JS_NewPlainObject(aCx);
}
}  // namespace binding_detail

// Resolve an id on the given global object that wants to be included in
// Exposed=System webidl annotations.  False return value means exception
// thrown.
bool SystemGlobalResolve(JSContext* cx, JS::Handle<JSObject*> obj,
                         JS::Handle<jsid> id, bool* resolvedp);

// Enumerate all ids on the given global object that wants to be included in
// Exposed=System webidl annotations.  False return value means exception
// thrown.
bool SystemGlobalEnumerate(JSContext* cx, JS::Handle<JSObject*> obj);

// Slot indexes for maplike/setlike forEach functions
#define FOREACH_CALLBACK_SLOT 0
#define FOREACH_MAPLIKEORSETLIKEOBJ_SLOT 1

// Backing function for running .forEach() on maplike/setlike interfaces.
// Unpacks callback and maplike/setlike object from reserved slots, then runs
// callback for each key (and value, for maplikes)
bool ForEachHandler(JSContext* aCx, unsigned aArgc, JS::Value* aVp);

// Unpacks backing object (ES6 map/set) from the reserved slot of a reflector
// for a maplike/setlike interface. If backing object does not exist, creates
// backing object in the compartment of the reflector involved, making this safe
// to use across compartments/via xrays. Return values of these methods will
// always be in the context compartment.
bool GetMaplikeBackingObject(JSContext* aCx, JS::Handle<JSObject*> aObj,
                             size_t aSlotIndex,
                             JS::MutableHandle<JSObject*> aBackingObj,
                             bool* aBackingObjCreated);
bool GetSetlikeBackingObject(JSContext* aCx, JS::Handle<JSObject*> aObj,
                             size_t aSlotIndex,
                             JS::MutableHandle<JSObject*> aBackingObj,
                             bool* aBackingObjCreated);

// Unpacks backing object (ES Proxy exotic object) from the reserved slot of a
// reflector for a observableArray attribute. If backing object does not exist,
// creates backing object in the compartment of the reflector involved, making
// this safe to use across compartments/via xrays. Return values of these
// methods will always be in the context compartment.
bool GetObservableArrayBackingObject(
    JSContext* aCx, JS::Handle<JSObject*> aObj, size_t aSlotIndex,
    JS::MutableHandle<JSObject*> aBackingObj, bool* aBackingObjCreated,
    const ObservableArrayProxyHandler* aHandler, void* aOwner);

// Get the desired prototype object for an object construction from the given
// CallArgs.  The CallArgs must be for a constructor call.  The
// aProtoId/aCreator arguments are used to get a default if we don't find a
// prototype on the newTarget of the callargs.
bool GetDesiredProto(JSContext* aCx, const JS::CallArgs& aCallArgs,
                     prototypes::id::ID aProtoId,
                     CreateInterfaceObjectsMethod aCreator,
                     JS::MutableHandle<JSObject*> aDesiredProto);

// This function is expected to be called from the constructor function for an
// HTML or XUL element interface; the global/callargs need to be whatever was
// passed to that constructor function.
already_AddRefed<Element> CreateXULOrHTMLElement(
    const GlobalObject& aGlobal, const JS::CallArgs& aCallArgs,
    JS::Handle<JSObject*> aGivenProto, ErrorResult& aRv);

void SetUseCounter(JSObject* aObject, UseCounter aUseCounter);
void SetUseCounter(UseCounterWorker aUseCounter);

// Warnings
void DeprecationWarning(JSContext* aCx, JSObject* aObject,
                        DeprecatedOperations aOperation);

void DeprecationWarning(const GlobalObject& aGlobal,
                        DeprecatedOperations aOperation);

namespace binding_detail {
// Get a JS global object that can be used for some temporary allocations.  The
// idea is that this should be used for situations when you need to operate in
// _some_ compartment but don't care which one.  A typical example is when you
// have non-JS input, non-JS output, but have to go through some sort of JS
// representation in the middle, so need a compartment to allocate things in.
//
// It's VERY important that any consumers of this function only do things that
// are guaranteed to be side-effect-free, even in the face of a script
// environment controlled by a hostile adversary.  This is because in the worker
// case the global is in fact the worker global, so it and its standard objects
// are controlled by the worker script.  This is why this function is in the
// binding_detail namespace.  Any use of this function MUST be very carefully
// reviewed by someone who is sufficiently devious and has a very good
// understanding of all the code that will run while we're using the return
// value, including the SpiderMonkey parts.
JSObject* UnprivilegedJunkScopeOrWorkerGlobal(const fallible_t&);

// Implementation of the [HTMLConstructor] extended attribute.
bool HTMLConstructor(JSContext* aCx, unsigned aArgc, JS::Value* aVp,
                     constructors::id::ID aConstructorId,
                     prototypes::id::ID aProtoId,
                     CreateInterfaceObjectsMethod aCreator);

// A method to test whether an attribute with the given JSJitGetterOp getter is
// enabled in the given set of prefable proeprty specs.  For use for toJSON
// conversions.  aObj is the object that would be used as the "this" value.
bool IsGetterEnabled(JSContext* aCx, JS::Handle<JSObject*> aObj,
                     JSJitGetterOp aGetter,
                     const Prefable<const JSPropertySpec>* aAttributes);

// A class that can be used to examine the chars of a linear string.
class StringIdChars {
 public:
  // Require a non-const ref to an AutoRequireNoGC to prevent callers
  // from passing temporaries.
  StringIdChars(JS::AutoRequireNoGC& nogc, JSLinearString* str) {
    mIsLatin1 = JS::LinearStringHasLatin1Chars(str);
    if (mIsLatin1) {
      mLatin1Chars = JS::GetLatin1LinearStringChars(nogc, str);
    } else {
      mTwoByteChars = JS::GetTwoByteLinearStringChars(nogc, str);
    }
#ifdef DEBUG
    mLength = JS::GetLinearStringLength(str);
#endif  // DEBUG
  }

  MOZ_ALWAYS_INLINE char16_t operator[](size_t index) {
    MOZ_ASSERT(index < mLength);
    if (mIsLatin1) {
      return mLatin1Chars[index];
    }
    return mTwoByteChars[index];
  }

 private:
  bool mIsLatin1;
  union {
    const JS::Latin1Char* mLatin1Chars;
    const char16_t* mTwoByteChars;
  };
#ifdef DEBUG
  size_t mLength;
#endif  // DEBUG
};

already_AddRefed<Promise> CreateRejectedPromiseFromThrownException(
    JSContext* aCx, ErrorResult& aError);

template <auto ConstructorEnabled>
inline bool ShouldExpose(JSContext* aCx, JS::Handle<JSObject*> aGlobal,
                         DefineInterfaceProperty aDefine) {
  return aDefine == DefineInterfaceProperty::Always ||
         (aDefine == DefineInterfaceProperty::CheckExposure &&
          ConstructorEnabled(aCx, aGlobal));
}

class ReflectedHTMLAttributeSlotsBase {
 protected:
  static void ForEachXrayReflectedHTMLAttributeSlots(
      JS::RootingContext* aCx, JSObject* aObject, size_t aSlotIndex,
      size_t aArrayIndex, void (*aFunc)(void* aSlots, size_t aArrayIndex));
  static void XrayExpandoObjectFinalize(JS::GCContext* aCx, JSObject* aObject);
};

template <size_t SlotIndex, size_t XrayExpandoSlotIndex, size_t Count>
class ReflectedHTMLAttributeSlots : public Array<JS::Heap<JS::Value>, Count>,
                                    private ReflectedHTMLAttributeSlotsBase {
 public:
  using Array<JS::Heap<JS::Value>, Count>::Array;

  static ReflectedHTMLAttributeSlots& GetOrCreate(JSObject* aSlotStorage,
                                                  bool aIsXray) {
    size_t slotIndex = aIsXray ? XrayExpandoSlotIndex : SlotIndex;
    JS::Value v = JS::GetReservedSlot(aSlotStorage, slotIndex);
    ReflectedHTMLAttributeSlots* array;
    if (v.isUndefined()) {
      array = new ReflectedHTMLAttributeSlots();
      JS::SetReservedSlot(aSlotStorage, slotIndex, JS::PrivateValue(array));
    } else {
      array = static_cast<ReflectedHTMLAttributeSlots*>(v.toPrivate());
    }
    return *array;
  }

  static void Clear(JSObject* aObject, size_t aArrayIndex) {
    JS::Value array = JS::GetReservedSlot(aObject, SlotIndex);
    if (!array.isUndefined()) {
      ReflectedHTMLAttributeSlots& slots =
          *static_cast<ReflectedHTMLAttributeSlots*>(array.toPrivate());
      slots[aArrayIndex] = JS::UndefinedValue();
    }
  }
  static void ClearInXrays(JS::RootingContext* aCx, JSObject* aObject,
                           size_t aArrayIndex) {
    ReflectedHTMLAttributeSlotsBase::ForEachXrayReflectedHTMLAttributeSlots(
        aCx, aObject, XrayExpandoSlotIndex, aArrayIndex,
        [](void* aSlots, size_t aArrayIndex) {
          ReflectedHTMLAttributeSlots& slots =
              *static_cast<ReflectedHTMLAttributeSlots*>(aSlots);
          slots[aArrayIndex] = JS::UndefinedValue();
        });
  }

  static void Trace(JSTracer* aTracer, JSObject* aObject) {
    Trace(aTracer, aObject, SlotIndex);
  }

  static void Finalize(JSObject* aObject) { Finalize(aObject, SlotIndex); }

  static void XrayExpandoObjectTrace(JSTracer* aTracer, JSObject* aObject) {
    Trace(aTracer, aObject, XrayExpandoSlotIndex);
  }

  static void XrayExpandoObjectFinalize(JS::GCContext* aCx, JSObject* aObject) {
    Finalize(aObject, XrayExpandoSlotIndex);
    ReflectedHTMLAttributeSlotsBase::XrayExpandoObjectFinalize(aCx, aObject);
  }

  static constexpr JSClassOps sXrayExpandoObjectClassOps = {
      nullptr, /* addProperty */
      nullptr, /* delProperty */
      nullptr, /* enumerate */
      nullptr, /* newEnumerate */
      nullptr, /* resolve */
      nullptr, /* mayResolve */
      XrayExpandoObjectFinalize,
      nullptr, /* call */
      nullptr, /* construct */
      XrayExpandoObjectTrace,
  };

 private:
  static void Trace(JSTracer* aTracer, JSObject* aObject, size_t aSlotIndex) {
    JS::Value slotValue = JS::GetReservedSlot(aObject, aSlotIndex);
    if (!slotValue.isUndefined()) {
      auto* array =
          static_cast<ReflectedHTMLAttributeSlots*>(slotValue.toPrivate());
      for (JS::Heap<JS::Value>& v : *array) {
        JS::TraceEdge(aTracer, &v, "ReflectedHTMLAttributeSlots[i]");
      }
    }
  }
  static void Finalize(JSObject* aObject, size_t aSlotIndex) {
    JS::Value slotValue = JS::GetReservedSlot(aObject, aSlotIndex);
    if (!slotValue.isUndefined()) {
      delete static_cast<ReflectedHTMLAttributeSlots*>(slotValue.toPrivate());
      JS::SetReservedSlot(aObject, aSlotIndex, JS::UndefinedValue());
    }
  }
};

void ClearXrayExpandoSlots(JS::RootingContext* aCx, JSObject* aObject,
                           size_t aSlotIndex);

}  // namespace binding_detail

}  // namespace dom

}  // namespace mozilla

#endif /* mozilla_dom_BindingUtils_h_ */
