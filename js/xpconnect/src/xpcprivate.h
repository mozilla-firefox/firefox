/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * XPConnect allows JS code to manipulate C++ object and C++ code to manipulate
 * JS objects. JS manipulation of C++ objects tends to be significantly more
 * complex. This comment explains how it is orchestrated by XPConnect.
 *
 * For each C++ object to be manipulated in JS, there is a corresponding JS
 * object. This is called the "flattened JS object". By default, there is an
 * additional C++ object involved of type XPCWrappedNative. The XPCWrappedNative
 * holds pointers to the C++ object and the flat JS object.
 *
 * All XPCWrappedNative objects belong to an XPCWrappedNativeScope. These scopes
 * are essentially in 1:1 correspondence with JS compartments. The
 * XPCWrappedNativeScope has a pointer to the JS compartment. The global of a
 * flattened JS object is one of the globals in this compartment (the exception
 * to this rule is when a PreCreate hook asks for a different global; see
 * nsIXPCScriptable below).
 *
 * Some C++ objects (notably DOM objects) have information associated with them
 * that lists the interfaces implemented by these objects. A C++ object exposes
 * this information by implementing nsIClassInfo. If a C++ object implements
 * nsIClassInfo, then JS code can call its methods without needing to use
 * QueryInterface first. Typically, all instances of a C++ class share the same
 * nsIClassInfo instance. (That is, obj->QueryInterface(nsIClassInfo) returns
 * the same result for every obj of a given class.)
 *
 * XPConnect tracks nsIClassInfo information in an XPCWrappedNativeProto object.
 * A given XPCWrappedNativeScope will have one XPCWrappedNativeProto for each
 * nsIClassInfo instance being used. The XPCWrappedNativeProto has an associated
 * JS object, which is used as the prototype of all flattened JS objects created
 * for C++ objects with the given nsIClassInfo.
 *
 * Each XPCWrappedNativeProto has a pointer to its XPCWrappedNativeScope. If an
 * XPCWrappedNative wraps a C++ object with class info, then it points to its
 * XPCWrappedNativeProto. Otherwise it points to its XPCWrappedNativeScope. (The
 * pointers are smooshed together in a tagged union.) Either way it can reach
 * its scope.
 *
 * An XPCWrappedNativeProto keeps track of the set of interfaces implemented by
 * the C++ object in an XPCNativeSet. (The list of interfaces is obtained by
 * calling a method on the nsIClassInfo.) An XPCNativeSet is a collection of
 * XPCNativeInterfaces. Each interface stores the list of members, which can be
 * methods, constants, getters, or setters.
 *
 * An XPCWrappedNative also points to an XPCNativeSet. Initially this starts out
 * the same as the XPCWrappedNativeProto's set. If there is no proto, it starts
 * out as a singleton set containing nsISupports. If JS code QI's new interfaces
 * outside of the existing set, the set will grow. All QueryInterface results
 * are cached in XPCWrappedNativeTearOff objects, which are linked off of the
 * XPCWrappedNative.
 *
 * Besides having class info, a C++ object may be "scriptable" (i.e., implement
 * nsIXPCScriptable). This allows it to implement a more DOM-like interface,
 * besides just exposing XPCOM methods and constants. An nsIXPCScriptable
 * instance has hooks that correspond to all the normal JSClass hooks. Each
 * nsIXPCScriptable instance can have pointers from XPCWrappedNativeProto and
 * XPCWrappedNative (since C++ objects can have scriptable info without having
 * class info).
 */

/* All the XPConnect private declarations - only include locally. */

#ifndef xpcprivate_h___
#define xpcprivate_h___

#include "mozilla/Alignment.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/mozalloc.h"
#include "mozilla/Preferences.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"

#include "mozilla/dom/ScriptSettings.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xpcpublic.h"
#include "js/HashTable.h"
#include "js/GCHashTable.h"
#include "js/Object.h"              // JS::GetClass, JS::GetCompartment
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/TracingAPI.h"
#include "js/WeakMapPtr.h"
#include "nscore.h"
#include "nsXPCOM.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "nsISupports.h"
#include "nsIServiceManager.h"
#include "nsIClassInfoImpl.h"
#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsISupportsPrimitives.h"
#include "nsISimpleEnumerator.h"
#include "nsIXPConnect.h"
#include "nsIXPCScriptable.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"
#include "nsCOMPtr.h"
#include "nsXPTCUtils.h"
#include "xptinfo.h"
#include "XPCForwards.h"
#include "XPCLog.h"
#include "xpccomponents.h"
#include "prenv.h"
#include "prcvar.h"
#include "nsString.h"
#include "nsReadableUtils.h"

#include "MainThreadUtils.h"

#include "nsIConsoleService.h"

#include "nsVariant.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "nsWrapperCache.h"
#include "nsDeque.h"

#include "nsIScriptSecurityManager.h"

#include "nsIPrincipal.h"
#include "nsJSPrincipals.h"
#include "nsIScriptObjectPrincipal.h"
#include "xpcObjectHelper.h"

#include "SandboxPrivate.h"
#include "SystemGlobal.h"

#ifdef XP_WIN
// Nasty MS defines
#  ifdef GetClassInfo
#    undef GetClassInfo
#  endif
#  ifdef GetClassName
#    undef GetClassName
#  endif
#endif /* XP_WIN */

namespace mozilla {
namespace dom {
class AutoEntryScript;
class Exception;
}  // namespace dom
}  // namespace mozilla

/***************************************************************************/
// data declarations...
extern const char XPC_EXCEPTION_CONTRACTID[];
extern const char XPC_CONSOLE_CONTRACTID[];
extern const char XPC_SCRIPT_ERROR_CONTRACTID[];
extern const char XPC_XPCONNECT_CONTRACTID[];

/***************************************************************************/
// Helper function.

namespace xpc {

inline bool IsWrappedNativeReflector(JSObject* obj) {
  return JS::GetClass(obj)->isWrappedNative();
}

}  // namespace xpc

/***************************************************************************
****************************************************************************
*
* Core runtime and context classes...
*
****************************************************************************
***************************************************************************/

// We have a general rule internally that getters that return addref'd interface
// pointer generally do so using an 'out' parm. When interface pointers are
// returned as function call result values they are not addref'd. Exceptions
// to this rule are noted explicitly.

class nsXPConnect final : public nsIXPConnect {
 public:
  // all the interface method declarations...
  NS_DECL_ISUPPORTS

  // non-interface implementation
 public:
  static XPCJSRuntime* GetRuntimeInstance();
  XPCJSContext* GetContext() { return mContext; }

  static nsIScriptSecurityManager* SecurityManager() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(gScriptSecurityManager);
    return gScriptSecurityManager;
  }

  static nsIPrincipal* SystemPrincipal() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(gSystemPrincipal);
    return gSystemPrincipal;
  }

  // Called by module code in dll startup
  static void InitStatics();
  // Called by module code on dll shutdown.
  static void ReleaseXPConnectSingleton();

  static void InitJSContext();

  void RecordTraversal(void* p, nsISupports* s);

 protected:
  virtual ~nsXPConnect();

  nsXPConnect();

 private:
  // Singleton instance
  static nsXPConnect* gSelf;
  static bool gOnceAliveNowDead;

  XPCJSContext* mContext = nullptr;
  XPCJSRuntime* mRuntime = nullptr;

  friend class nsIXPConnect;

 public:
  static nsIScriptSecurityManager* gScriptSecurityManager;
  static nsIPrincipal* gSystemPrincipal;
};

/***************************************************************************/

// In the current xpconnect system there can only be one XPCJSContext.
// So, xpconnect can only be used on one JSContext within the process.

class WatchdogManager;

// clang-format off
MOZ_DEFINE_ENUM(WatchdogTimestampCategory, (
    TimestampWatchdogWakeup,
    TimestampWatchdogHibernateStart,
    TimestampWatchdogHibernateStop,
    TimestampContextStateChange
));
// clang-format on

class AsyncFreeSnowWhite;
class XPCWrappedNativeScope;

using XPCWrappedNativeScopeList = mozilla::LinkedList<XPCWrappedNativeScope>;

class XPCJSContext final : public mozilla::CycleCollectedJSContext,
                           public mozilla::LinkedListElement<XPCJSContext> {
 public:
  static XPCJSContext* NewXPCJSContext();
  static XPCJSContext* Get();

  XPCJSRuntime* Runtime() const;

  virtual mozilla::CycleCollectedJSRuntime* CreateRuntime(
      JSContext* aCx) override;

  XPCCallContext* GetCallContext() const { return mCallContext; }
  XPCCallContext* SetCallContext(XPCCallContext* ccx) {
    XPCCallContext* old = mCallContext;
    mCallContext = ccx;
    return old;
  }

  jsid GetResolveName() const { return mResolveName; }
  jsid SetResolveName(jsid name) {
    jsid old = mResolveName;
    mResolveName = name;
    return old;
  }

  XPCWrappedNative* GetResolvingWrapper() const { return mResolvingWrapper; }
  XPCWrappedNative* SetResolvingWrapper(XPCWrappedNative* w) {
    XPCWrappedNative* old = mResolvingWrapper;
    mResolvingWrapper = w;
    return old;
  }

  bool JSContextInitialized(JSContext* cx);

  virtual void BeforeProcessTask(bool aMightBlock) override;
  virtual void AfterProcessTask(uint32_t aNewRecursionDepth) override;

  // Relay to the CCGCScheduler instead of queuing up an idle runnable
  // (as is done for workers in CycleCollectedJSContext).
  virtual void MaybePokeGC() override;

  ~XPCJSContext();

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  bool IsSystemCaller() const override;

  AutoMarkingPtr** GetAutoRootsAdr() { return &mAutoRoots; }

  nsresult GetPendingResult() { return mPendingResult; }
  void SetPendingResult(nsresult rv) { mPendingResult = rv; }

  PRTime GetWatchdogTimestamp(WatchdogTimestampCategory aCategory);

  static bool RecordScriptActivity(bool aActive);

  bool SetHasScriptActivity(bool aActive) {
    bool oldValue = mHasScriptActivity;
    mHasScriptActivity = aActive;
    return oldValue;
  }

  static bool InterruptCallback(JSContext* cx);

  // Mapping of often used strings to jsid atoms that live 'forever'.
  //
  // To add a new string: add to this list and to XPCJSRuntime::mStrings
  // at the top of XPCJSRuntime.cpp
  enum {
    IDX_CONSTRUCTOR = 0,
    IDX_TO_STRING,
    IDX_TO_SOURCE,
    IDX_VALUE,
    IDX_QUERY_INTERFACE,
    IDX_COMPONENTS,
    IDX_CC,
    IDX_CI,
    IDX_CR,
    IDX_CU,
    IDX_SERVICES,
    IDX_WRAPPED_JSOBJECT,
    IDX_PROTOTYPE,
    IDX_EVAL,
    IDX_CONTROLLERS,
    IDX_CONTROLLERS_CLASS,
    IDX_LENGTH,
    IDX_NAME,
    IDX_UNDEFINED,
    IDX_EMPTYSTRING,
    IDX_FILENAME,
    IDX_LINENUMBER,
    IDX_COLUMNNUMBER,
    IDX_STACK,
    IDX_MESSAGE,
    IDX_CAUSE,
    IDX_ERRORS,
    IDX_LASTINDEX,
    IDX_THEN,
    IDX_ISINSTANCE,
    IDX_INFINITY,
    IDX_NAN,
    IDX_CLASS_ID,
    IDX_INTERFACE_ID,
    IDX_INITIALIZER,
    IDX_PRINT,
    IDX_FETCH,
    IDX_CRYPTO,
    IDX_INDEXEDDB,
    IDX_STRUCTUREDCLONE,
    IDX_LOCKS,
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    IDX_SUPPRESSED,
    IDX_ERROR,
#endif
    IDX_TOTAL_COUNT  // just a count of the above
  };

  inline JS::HandleId GetStringID(unsigned index) const;
  inline const char* GetStringName(unsigned index) const;

 private:
  XPCJSContext();

  MOZ_IS_CLASS_INIT
  nsresult Initialize();

  XPCCallContext* mCallContext;
  AutoMarkingPtr* mAutoRoots;
  jsid mResolveName;
  XPCWrappedNative* mResolvingWrapper;
  WatchdogManager* mWatchdogManager;

  // Number of XPCJSContexts currently alive.
  static uint32_t sInstanceCount;
  static mozilla::StaticAutoPtr<WatchdogManager> sWatchdogInstance;
  static WatchdogManager* GetWatchdogManager();

  // If we spend too much time running JS code in an event handler, then we
  // want to show the slow script UI. The timeout T is controlled by prefs. We
  // invoke the interrupt callback once after T/2 seconds and set
  // mSlowScriptSecondHalf to true. After another T/2 seconds, we invoke the
  // interrupt callback again. Since mSlowScriptSecondHalf is now true, it
  // shows the slow script UI. The reason we invoke the callback twice is to
  // ensure that putting the computer to sleep while running a script doesn't
  // cause the UI to be shown. If the laptop goes to sleep during one of the
  // timeout periods, the script still has the other T/2 seconds to complete
  // before the slow script UI is shown.
  bool mSlowScriptSecondHalf;

  // mSlowScriptCheckpoint is set to the time when:
  // 1. We started processing the current event, or
  // 2. mSlowScriptSecondHalf was set to true
  // (whichever comes later). We use it to determine whether the interrupt
  // callback needs to do anything.
  mozilla::TimeStamp mSlowScriptCheckpoint;
  // Accumulates total time we actually waited for telemetry
  mozilla::TimeDuration mSlowScriptActualWait;
  bool mTimeoutAccumulated;
  bool mExecutedChromeScript;

  bool mHasScriptActivity;

  // mPendingResult is used to implement Components.returnCode.  Only really
  // meaningful while calling through XPCWrappedJS.
  nsresult mPendingResult;

  // These members must be accessed via WatchdogManager.
  enum { CONTEXT_ACTIVE, CONTEXT_INACTIVE } mActive;
  PRTime mLastStateChange;

  friend class XPCJSRuntime;
  friend class Watchdog;
  friend class WatchdogManager;
  friend class AutoLockWatchdog;
};

class XPCJSRuntime final : public mozilla::CycleCollectedJSRuntime {
 public:
  static XPCJSRuntime* Get();

  void RemoveWrappedJS(nsXPCWrappedJS* wrapper);
  void AssertInvalidWrappedJSNotInTable(nsXPCWrappedJS* wrapper) const;

  JSObject2WrappedJSMap* GetMultiCompartmentWrappedJSMap() const {
    return mWrappedJSMap.get();
  }

  IID2NativeInterfaceMap* GetIID2NativeInterfaceMap() const {
    return mIID2NativeInterfaceMap.get();
  }

  ClassInfo2NativeSetMap* GetClassInfo2NativeSetMap() const {
    return mClassInfo2NativeSetMap.get();
  }

  NativeSetMap* GetNativeSetMap() const { return mNativeSetMap.get(); }

  using WrappedNativeProtoVector =
      mozilla::Vector<mozilla::UniquePtr<XPCWrappedNativeProto>, 0,
                      InfallibleAllocPolicy>;
  WrappedNativeProtoVector& GetDyingWrappedNativeProtos() {
    return mDyingWrappedNativeProtos;
  }

  XPCWrappedNativeScopeList& GetWrappedNativeScopes() {
    return mWrappedNativeScopes;
  }

  bool InitializeStrings(JSContext* cx);

  virtual bool DescribeCustomObjects(JSObject* aObject, const JSClass* aClasp,
                                     char (&aName)[72]) const override;
  virtual bool NoteCustomGCThingXPCOMChildren(
      const JSClass* aClasp, JSObject* aObj,
      nsCycleCollectionTraversalCallback& aCb) const override;

  /**
   * Infrastructure for classes that need to defer part of the finalization
   * until after the GC has run, for example for objects that we don't want to
   * destroy during the GC.
   */

 public:
  bool GetDoingFinalization() const { return mDoingFinalization; }

  JS::HandleId GetStringID(unsigned index) const {
    MOZ_ASSERT(index < XPCJSContext::IDX_TOTAL_COUNT, "index out of range");
    // fromMarkedLocation() is safe because the string is interned.
    return JS::HandleId::fromMarkedLocation(&mStrIDs[index]);
  }
  const char* GetStringName(unsigned index) const {
    MOZ_ASSERT(index < XPCJSContext::IDX_TOTAL_COUNT, "index out of range");
    return mStrings[index];
  }

  virtual bool UsefulToMergeZones() const override;
  void TraceAdditionalNativeBlackRoots(JSTracer* trc) override;
  void TraceAdditionalNativeGrayRoots(JSTracer* aTracer) override;
  void TraverseAdditionalNativeRoots(
      nsCycleCollectionNoteRootCallback& cb) override;
  void UnmarkSkippableJSHolders();
  void PrepareForForgetSkippable() override;
  void BeginCycleCollectionCallback(mozilla::CCReason aReason) override;
  void EndCycleCollectionCallback(
      mozilla::CycleCollectorResults& aResults) override;
  void DispatchDeferredDeletion(bool aContinuation,
                                bool aPurge = false) override;

  void CustomGCCallback(JSGCStatus status) override;
  void CustomOutOfMemoryCallback() override;
  void OnLargeAllocationFailure();
  static void GCSliceCallback(JSContext* cx, JS::GCProgress progress,
                              const JS::GCDescription& desc);
  static void DoCycleCollectionCallback(JSContext* cx);
  static void FinalizeCallback(JS::GCContext* gcx, JSFinalizeStatus status,
                               void* data);
  static void WeakPointerZonesCallback(JSTracer* trc, void* data);
  static void WeakPointerCompartmentCallback(JSTracer* trc,
                                             JS::Compartment* comp, void* data);

  inline void AddSubjectToFinalizationWJS(nsXPCWrappedJS* wrappedJS);

  void DebugDump(int16_t depth);

  bool GCIsRunning() const { return mGCIsRunning; }

  ~XPCJSRuntime();

  void AddGCCallback(xpcGCCallback cb);
  void RemoveGCCallback(xpcGCCallback cb);

  JSObject* GetUAWidgetScope(JSContext* cx, nsIPrincipal* principal);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  /**
   * The unprivileged junk scope is an unprivileged sandbox global used for
   * convenience by certain operations which need an unprivileged global but
   * don't have one immediately handy. It should generally be avoided when
   * possible.
   *
   * The scope is created lazily when it is needed, and held weakly so that it
   * is destroyed when there are no longer any remaining external references to
   * it. This means that under low memory conditions, when the scope does not
   * already exist, we may not be able to create one. In these circumstances,
   * the infallible version of this API will abort, and the fallible version
   * will return null. Callers should therefore prefer the fallible version when
   * on a codepath which can already return failure, but may use the infallible
   * one otherwise.
   */
  JSObject* UnprivilegedJunkScope();
  JSObject* UnprivilegedJunkScope(const mozilla::fallible_t&);

  bool IsUnprivilegedJunkScope(JSObject*);

  void DeleteSingletonScopes();

 private:
  explicit XPCJSRuntime(JSContext* aCx);

  MOZ_IS_CLASS_INIT
  void Initialize(JSContext* cx);
  void Shutdown(JSContext* cx) override;

  static const char* const mStrings[XPCJSContext::IDX_TOTAL_COUNT];
  jsid mStrIDs[XPCJSContext::IDX_TOTAL_COUNT];

  struct Hasher {
    using Key = RefPtr<mozilla::BasePrincipal>;
    using Lookup = Key;
    static uint32_t hash(const Lookup& l) { return l->GetOriginNoSuffixHash(); }
    static bool match(const Key& k, const Lookup& l) {
      return k->FastEquals(l);
    }
  };

  struct MapEntryGCPolicy {
    static bool traceWeak(JSTracer* trc,
                          RefPtr<mozilla::BasePrincipal>* /* unused */,
                          JS::Heap<JSObject*>* value) {
      return JS::GCPolicy<JS::Heap<JSObject*>>::traceWeak(trc, value);
    }
  };

  typedef JS::GCHashMap<RefPtr<mozilla::BasePrincipal>, JS::Heap<JSObject*>,
                        Hasher, js::SystemAllocPolicy, MapEntryGCPolicy>
      Principal2JSObjectMap;

  mozilla::UniquePtr<JSObject2WrappedJSMap> mWrappedJSMap;
  mozilla::UniquePtr<IID2NativeInterfaceMap> mIID2NativeInterfaceMap;
  mozilla::UniquePtr<ClassInfo2NativeSetMap> mClassInfo2NativeSetMap;
  mozilla::UniquePtr<NativeSetMap> mNativeSetMap;
  Principal2JSObjectMap mUAWidgetScopeMap;
  XPCWrappedNativeScopeList mWrappedNativeScopes;
  WrappedNativeProtoVector mDyingWrappedNativeProtos;
  bool mGCIsRunning;
  nsTArray<nsISupports*> mNativesToReleaseArray;
  bool mDoingFinalization;
  mozilla::LinkedList<nsXPCWrappedJS> mSubjectToFinalizationWJS;
  nsTArray<xpcGCCallback> extraGCCallbacks;
  JS::GCSliceCallback mPrevGCSliceCallback;
  JS::DoCycleCollectionCallback mPrevDoCycleCollectionCallback;
  mozilla::WeakPtr<SandboxPrivate> mUnprivilegedJunkScope;
  RefPtr<AsyncFreeSnowWhite> mAsyncSnowWhiteFreer;

  friend class XPCJSContext;
  friend class XPCIncrementalReleaseRunnable;
};

inline JS::HandleId XPCJSContext::GetStringID(unsigned index) const {
  return Runtime()->GetStringID(index);
}

inline const char* XPCJSContext::GetStringName(unsigned index) const {
  return Runtime()->GetStringName(index);
}

/***************************************************************************/

// No virtuals
// XPCCallContext is ALWAYS declared as a local variable in some function;
// i.e. instance lifetime is always controled by some C++ function returning.
//
// These things are created frequently in many places. We *intentionally* do
// not inialialize all members in order to save on construction overhead.
// Some constructor pass more valid params than others. We init what must be
// init'd and leave other members undefined. In debug builds the accessors
// use a CHECK_STATE macro to track whether or not the object is in a valid
// state to answer the question a caller might be asking. As long as this
// class is maintained correctly it can do its job without a bunch of added
// overhead from useless initializations and non-DEBUG error checking.
//
// Note that most accessors are inlined.

class MOZ_STACK_CLASS XPCCallContext final {
 public:
  enum : unsigned { NO_ARGS = (unsigned)-1 };

  explicit XPCCallContext(JSContext* cx, JS::HandleObject obj = nullptr,
                          JS::HandleObject funobj = nullptr,
                          JS::HandleId id = JS::VoidHandlePropertyKey,
                          unsigned argc = NO_ARGS, JS::Value* argv = nullptr,
                          JS::Value* rval = nullptr);

  virtual ~XPCCallContext();

  inline bool IsValid() const;

  inline XPCJSContext* GetContext() const;
  inline JSContext* GetJSContext() const;
  inline bool GetContextPopRequired() const;
  inline XPCCallContext* GetPrevCallContext() const;

  inline JSObject* GetFlattenedJSObject() const;
  inline XPCWrappedNative* GetWrapper() const;

  inline bool CanGetTearOff() const;
  inline XPCWrappedNativeTearOff* GetTearOff() const;

  inline nsIXPCScriptable* GetScriptable() const;
  inline XPCNativeSet* GetSet() const;
  inline bool CanGetInterface() const;
  inline XPCNativeInterface* GetInterface() const;
  inline XPCNativeMember* GetMember() const;
  inline bool HasInterfaceAndMember() const;
  inline bool GetStaticMemberIsLocal() const;
  inline unsigned GetArgc() const;
  inline JS::Value* GetArgv() const;

  inline uint16_t GetMethodIndex() const;

  inline jsid GetResolveName() const;
  inline jsid SetResolveName(JS::HandleId name);

  inline XPCWrappedNative* GetResolvingWrapper() const;
  inline XPCWrappedNative* SetResolvingWrapper(XPCWrappedNative* w);

  inline void SetRetVal(const JS::Value& val);

  void SetName(jsid name);
  void SetArgsAndResultPtr(unsigned argc, JS::Value* argv, JS::Value* rval);
  void SetCallInfo(XPCNativeInterface* iface, XPCNativeMember* member,
                   bool isSetter);

  nsresult CanCallNow();

  void SystemIsBeingShutDown();

  operator JSContext*() const { return GetJSContext(); }

 private:
  // no copy ctor or assignment allowed
  XPCCallContext(const XPCCallContext& r) = delete;
  XPCCallContext& operator=(const XPCCallContext& r) = delete;

 private:
  // posible values for mState
  enum State {
    INIT_FAILED,
    SYSTEM_SHUTDOWN,
    HAVE_CONTEXT,
    HAVE_OBJECT,
    HAVE_NAME,
    HAVE_ARGS,
    READY_TO_CALL,
    CALL_DONE
  };

#ifdef DEBUG
  inline void CHECK_STATE(int s) const { MOZ_ASSERT(mState >= s, "bad state"); }
#else
#  define CHECK_STATE(s) ((void)0)
#endif

 private:
  State mState;

  nsCOMPtr<nsIXPConnect> mXPC;

  XPCJSContext* mXPCJSContext;
  JSContext* mJSContext;

  // ctor does not necessarily init the following. BEWARE!

  XPCCallContext* mPrevCallContext;

  XPCWrappedNative* mWrapper;
  XPCWrappedNativeTearOff* mTearOff;

  nsCOMPtr<nsIXPCScriptable> mScriptable;

  RefPtr<XPCNativeSet> mSet;
  RefPtr<XPCNativeInterface> mInterface;
  XPCNativeMember* mMember;

  JS::RootedId mName;
  bool mStaticMemberIsLocal;

  unsigned mArgc;
  JS::Value* mArgv;
  JS::Value* mRetVal;

  uint16_t mMethodIndex;
};

/***************************************************************************
****************************************************************************
*
* Core classes for wrapped native objects for use from JavaScript...
*
****************************************************************************
***************************************************************************/

// These are the various JSClasses and callbacks whose use that required
// visibility from more than one .cpp file.

extern const JSClass XPC_WN_NoHelper_JSClass;
extern const JSClass XPC_WN_Proto_JSClass;
extern const JSClass XPC_WN_Tearoff_JSClass;
extern const JSClass XPC_WN_NoHelper_Proto_JSClass;

extern bool XPC_WN_CallMethod(JSContext* cx, unsigned argc, JS::Value* vp);

extern bool XPC_WN_GetterSetter(JSContext* cx, unsigned argc, JS::Value* vp);

/***************************************************************************/
// XPCWrappedNativeScope is one-to-one with a JS compartment.

class XPCWrappedNativeScope final
    : public mozilla::LinkedListElement<XPCWrappedNativeScope> {
 public:
  XPCJSRuntime* GetRuntime() const { return XPCJSRuntime::Get(); }

  Native2WrappedNativeMap* GetWrappedNativeMap() const {
    return mWrappedNativeMap.get();
  }

  ClassInfo2WrappedNativeProtoMap* GetWrappedNativeProtoMap() const {
    return mWrappedNativeProtoMap.get();
  }

  nsXPCComponents* GetComponents() const { return mComponents; }

  bool AttachComponentsObject(JSContext* aCx);

  bool AttachJSServices(JSContext* aCx);

  // Returns the JS object reflection of the Components object.
  bool GetComponentsJSObject(JSContext* cx, JS::MutableHandleObject obj);

  JSObject* GetExpandoChain(JS::HandleObject target);

  JSObject* DetachExpandoChain(JS::HandleObject target);

  bool SetExpandoChain(JSContext* cx, JS::HandleObject target,
                       JS::HandleObject chain);

  static void SystemIsBeingShutDown();

  static void TraceWrappedNativesInAllScopes(XPCJSRuntime* xpcrt,
                                             JSTracer* trc);

  void TraceInside(JSTracer* trc) {
    if (mXrayExpandos.initialized()) {
      mXrayExpandos.trace(trc);
    }
    JS::TraceEdge(trc, &mIDProto, "XPCWrappedNativeScope::mIDProto");
    JS::TraceEdge(trc, &mIIDProto, "XPCWrappedNativeScope::mIIDProto");
    JS::TraceEdge(trc, &mCIDProto, "XPCWrappedNativeScope::mCIDProto");
  }

  static void SuspectAllWrappers(nsCycleCollectionNoteRootCallback& cb);

  static void SweepAllWrappedNativeTearOffs();

  void UpdateWeakPointersAfterGC(JSTracer* trc);

  static void DebugDumpAllScopes(int16_t depth);

  void DebugDump(int16_t depth);

  struct ScopeSizeInfo {
    explicit ScopeSizeInfo(mozilla::MallocSizeOf mallocSizeOf)
        : mMallocSizeOf(mallocSizeOf),
          mScopeAndMapSize(0),
          mProtoAndIfaceCacheSize(0) {}

    mozilla::MallocSizeOf mMallocSizeOf;
    size_t mScopeAndMapSize;
    size_t mProtoAndIfaceCacheSize;
  };

  static void AddSizeOfAllScopesIncludingThis(JSContext* cx,
                                              ScopeSizeInfo* scopeSizeInfo);

  void AddSizeOfIncludingThis(JSContext* cx, ScopeSizeInfo* scopeSizeInfo);

  // Check whether our mAllowContentXBLScope state matches the given
  // principal.  This is used to avoid sharing compartments on
  // mismatch.
  bool XBLScopeStateMatches(nsIPrincipal* aPrincipal);

  XPCWrappedNativeScope(JS::Compartment* aCompartment,
                        JS::HandleObject aFirstGlobal);
  virtual ~XPCWrappedNativeScope();

  mozilla::UniquePtr<JSObject2JSObjectMap> mWaiverWrapperMap;

  JS::Compartment* Compartment() const { return mCompartment; }

  // Returns the global to use for new WrappedNative objects allocated in this
  // compartment. This is better than using arbitrary globals we happen to be in
  // because it prevents leaks (objects keep their globals alive).
  JSObject* GetGlobalForWrappedNatives() {
    return js::GetFirstGlobalInCompartment(Compartment());
  }

  bool AllowContentXBLScope(JS::Realm* aRealm);

  // ID Object prototype caches.
  JS::Heap<JSObject*> mIDProto;
  JS::Heap<JSObject*> mIIDProto;
  JS::Heap<JSObject*> mCIDProto;

 protected:
  XPCWrappedNativeScope() = delete;

 private:
  mozilla::UniquePtr<Native2WrappedNativeMap> mWrappedNativeMap;
  mozilla::UniquePtr<ClassInfo2WrappedNativeProtoMap> mWrappedNativeProtoMap;
  RefPtr<nsXPCComponents> mComponents;
  JS::Compartment* mCompartment;

  JS::WeakMapPtr<JSObject*, JSObject*> mXrayExpandos;

  // For remote XUL domains, we run all XBL in the content scope for compat
  // reasons (though we sometimes pref this off for automation). We
  // track the result of this decision (mAllowContentXBLScope) for now.
  bool mAllowContentXBLScope;
};

/***************************************************************************/
// Slots we use for our functions
#define XPC_FUNCTION_NATIVE_MEMBER_SLOT 0
#define XPC_FUNCTION_PARENT_OBJECT_SLOT 1

/***************************************************************************/
// XPCNativeMember represents a single idl declared method, attribute or
// constant.

// Tight. No virtual methods. Can be bitwise copied (until any resolution done).

class XPCNativeMember final {
 public:
  static bool GetCallInfo(JSObject* funobj,
                          RefPtr<XPCNativeInterface>* pInterface,
                          XPCNativeMember** pMember);

  jsid GetName() const { return mName; }

  uint16_t GetIndex() const { return mIndex; }

  bool GetConstantValue(XPCCallContext& ccx, XPCNativeInterface* iface,
                        JS::Value* pval) {
    MOZ_ASSERT(IsConstant(),
               "Only call this if you're sure this is a constant!");
    return Resolve(ccx, iface, nullptr, pval);
  }

  bool NewFunctionObject(XPCCallContext& ccx, XPCNativeInterface* iface,
                         JS::HandleObject parent, JS::Value* pval);

  bool IsMethod() const { return 0 != (mFlags & METHOD); }

  bool IsConstant() const { return 0 != (mFlags & CONSTANT); }

  bool IsAttribute() const { return 0 != (mFlags & GETTER); }

  bool IsWritableAttribute() const { return 0 != (mFlags & SETTER_TOO); }

  bool IsReadOnlyAttribute() const {
    return IsAttribute() && !IsWritableAttribute();
  }

  void SetName(jsid a) { mName = a; }

  void SetMethod(uint16_t index) {
    mFlags = METHOD;
    mIndex = index;
  }

  void SetConstant(uint16_t index) {
    mFlags = CONSTANT;
    mIndex = index;
  }

  void SetReadOnlyAttribute(uint16_t index) {
    mFlags = GETTER;
    mIndex = index;
  }

  void SetWritableAttribute() {
    MOZ_ASSERT(mFlags == GETTER, "bad");
    mFlags = GETTER | SETTER_TOO;
  }

  static uint16_t GetMaxIndexInInterface() { return (1 << 12) - 1; }

  inline XPCNativeInterface* GetInterface() const;

  void SetIndexInInterface(uint16_t index) { mIndexInInterface = index; }

  /* default ctor - leave random contents */
  MOZ_COUNTED_DEFAULT_CTOR(XPCNativeMember)
  MOZ_COUNTED_DTOR(XPCNativeMember)

  XPCNativeMember(const XPCNativeMember& other)
      : mName(other.mName),
        mIndex(other.mIndex),
        mFlags(other.mFlags),
        mIndexInInterface(other.mIndexInInterface) {
    MOZ_COUNT_CTOR(XPCNativeMember);
  }

 private:
  bool Resolve(XPCCallContext& ccx, XPCNativeInterface* iface,
               JS::HandleObject parent, JS::Value* vp);

  enum {
    METHOD = 0x01,
    CONSTANT = 0x02,
    GETTER = 0x04,
    SETTER_TOO = 0x08
    // If you add a flag here, you may need to make mFlags wider and either
    // make mIndexInInterface narrower (and adjust
    // XPCNativeInterface::NewInstance accordingly) or make this object
    // bigger.
  };

 private:
  // our only data...
  jsid mName;
  uint16_t mIndex;
  // mFlags needs to be wide enough to hold the flags in the above enum.
  uint16_t mFlags : 4;
  // mIndexInInterface is the index of this in our XPCNativeInterface's
  // mMembers.  In theory our XPCNativeInterface could have as many as 2^15-1
  // members (since mMemberCount is 15-bit) but in practice we prevent
  // creation of XPCNativeInterfaces which have more than 2^12 members.
  // If the width of this field changes, update GetMaxIndexInInterface.
  uint16_t mIndexInInterface : 12;
};

/***************************************************************************/
// XPCNativeInterface represents a single idl declared interface. This is
// primarily the set of XPCNativeMembers.

// Tight. No virtual methods.

class XPCNativeInterface final {
 public:
  NS_INLINE_DECL_REFCOUNTING_WITH_DESTROY(XPCNativeInterface,
                                          DestroyInstance(this))

  static already_AddRefed<XPCNativeInterface> GetNewOrUsed(JSContext* cx,
                                                           const nsIID* iid);
  static already_AddRefed<XPCNativeInterface> GetNewOrUsed(
      JSContext* cx, const nsXPTInterfaceInfo* info);
  static already_AddRefed<XPCNativeInterface> GetNewOrUsed(JSContext* cx,
                                                           const char* name);
  static already_AddRefed<XPCNativeInterface> GetISupports(JSContext* cx);

  inline const nsXPTInterfaceInfo* GetInterfaceInfo() const { return mInfo; }
  inline jsid GetName() const { return mName; }

  inline const nsIID* GetIID() const;
  inline const char* GetNameString() const;
  inline XPCNativeMember* FindMember(jsid name) const;

  static inline size_t OffsetOfMembers();

  uint16_t GetMemberCount() const { return mMemberCount; }
  XPCNativeMember* GetMemberAt(uint16_t i) {
    MOZ_ASSERT(i < mMemberCount, "bad index");
    return &mMembers[i];
  }

  void DebugDump(int16_t depth);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  void Trace(JSTracer* trc);

 protected:
  static already_AddRefed<XPCNativeInterface> NewInstance(
      JSContext* cx, IID2NativeInterfaceMap* aMap,
      const nsXPTInterfaceInfo* aInfo);

  XPCNativeInterface() = delete;
  XPCNativeInterface(const nsXPTInterfaceInfo* aInfo, jsid aName)
      : mInfo(aInfo), mName(aName), mMemberCount(0) {}
  ~XPCNativeInterface();

  void* operator new(size_t, void* p) noexcept(true) { return p; }

  XPCNativeInterface(const XPCNativeInterface& r) = delete;
  XPCNativeInterface& operator=(const XPCNativeInterface& r) = delete;

  static void DestroyInstance(XPCNativeInterface* inst);

 private:
  const nsXPTInterfaceInfo* mInfo;
  jsid mName;
  uint16_t mMemberCount;
  XPCNativeMember mMembers[1];  // always last - object sized for array
};

/***************************************************************************/
// XPCNativeSetKey is used to key a XPCNativeSet in a NativeSetMap.
// It represents a new XPCNativeSet we are considering constructing, without
// requiring that the set actually be built.

class MOZ_STACK_CLASS XPCNativeSetKey final {
 public:
  // This represents an existing set |baseSet|.
  explicit XPCNativeSetKey(XPCNativeSet* baseSet)
      : mCx(nullptr), mBaseSet(baseSet), mAddition(nullptr) {
    MOZ_ASSERT(baseSet);
  }

  // This represents a new set containing only nsISupports and
  // |addition|.  This needs a JSContext because it may need to
  // construct some data structures that need one to construct them.
  explicit XPCNativeSetKey(JSContext* cx, XPCNativeInterface* addition)
      : mCx(cx), mBaseSet(nullptr), mAddition(addition) {
    MOZ_ASSERT(cx);
    MOZ_ASSERT(addition);
  }

  // This represents the existing set |baseSet| with the interface
  // |addition| inserted after existing interfaces. |addition| must
  // not already be present in |baseSet|.
  explicit XPCNativeSetKey(XPCNativeSet* baseSet, XPCNativeInterface* addition);
  ~XPCNativeSetKey() = default;

  XPCNativeSet* GetBaseSet() const { return mBaseSet; }
  XPCNativeInterface* GetAddition() const { return mAddition; }

  mozilla::HashNumber Hash() const;

  // Allow shallow copy

 private:
  JSContext* mCx;
  RefPtr<XPCNativeSet> mBaseSet;
  RefPtr<XPCNativeInterface> mAddition;
};

/***************************************************************************/
// XPCNativeSet represents an ordered collection of XPCNativeInterface pointers.

class XPCNativeSet final {
 public:
  NS_INLINE_DECL_REFCOUNTING_WITH_DESTROY(XPCNativeSet, DestroyInstance(this))

  static already_AddRefed<XPCNativeSet> GetNewOrUsed(JSContext* cx,
                                                     const nsIID* iid);
  static already_AddRefed<XPCNativeSet> GetNewOrUsed(JSContext* cx,
                                                     nsIClassInfo* classInfo);
  static already_AddRefed<XPCNativeSet> GetNewOrUsed(JSContext* cx,
                                                     XPCNativeSetKey* key);

  // This generates a union set.
  //
  // If preserveFirstSetOrder is true, the elements from |firstSet| come first,
  // followed by any non-duplicate items from |secondSet|. If false, the same
  // algorithm is applied; but if we detect that |secondSet| is a superset of
  // |firstSet|, we return |secondSet| without worrying about whether the
  // ordering might differ from |firstSet|.
  static already_AddRefed<XPCNativeSet> GetNewOrUsed(
      JSContext* cx, XPCNativeSet* firstSet, XPCNativeSet* secondSet,
      bool preserveFirstSetOrder);

  static void ClearCacheEntryForClassInfo(nsIClassInfo* classInfo);

  inline bool FindMember(jsid name, XPCNativeMember** pMember,
                         uint16_t* pInterfaceIndex) const;

  inline bool FindMember(jsid name, XPCNativeMember** pMember,
                         RefPtr<XPCNativeInterface>* pInterface) const;

  inline bool FindMember(JS::HandleId name, XPCNativeMember** pMember,
                         RefPtr<XPCNativeInterface>* pInterface,
                         XPCNativeSet* protoSet, bool* pIsLocal) const;

  inline bool HasInterface(XPCNativeInterface* aInterface) const;

  uint16_t GetInterfaceCount() const { return mInterfaceCount; }
  XPCNativeInterface** GetInterfaceArray() { return mInterfaces; }

  XPCNativeInterface* GetInterfaceAt(uint16_t i) {
    MOZ_ASSERT(i < mInterfaceCount, "bad index");
    return mInterfaces[i];
  }

  inline bool MatchesSetUpToInterface(const XPCNativeSet* other,
                                      XPCNativeInterface* iface) const;

  void DebugDump(int16_t depth);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

 protected:
  static already_AddRefed<XPCNativeSet> NewInstance(
      JSContext* cx, nsTArray<RefPtr<XPCNativeInterface>>&& array);
  static already_AddRefed<XPCNativeSet> NewInstanceMutate(XPCNativeSetKey* key);

  XPCNativeSet() : mInterfaceCount(0) {}
  ~XPCNativeSet();
  void* operator new(size_t, void* p) noexcept(true) { return p; }

  static void DestroyInstance(XPCNativeSet* inst);

 private:
  uint16_t mInterfaceCount;
  // Always last - object sized for array.
  // These are strong references.
  XPCNativeInterface* mInterfaces[1];
};

/***********************************************/
// XPCWrappedNativeProtos hold the additional shared wrapper data for
// XPCWrappedNative whose native objects expose nsIClassInfo.
//
// The XPCWrappedNativeProto is owned by its mJSProtoObject, until that object
// is finalized. After that, it is owned by XPCJSRuntime's
// mDyingWrappedNativeProtos. See XPCWrappedNativeProto::JSProtoObjectFinalized
// and XPCJSRuntime::FinalizeCallback.

class XPCWrappedNativeProto final {
 public:
  enum Slots { ProtoSlot, SlotCount };

  static XPCWrappedNativeProto* GetNewOrUsed(JSContext* cx,
                                             XPCWrappedNativeScope* scope,
                                             nsIClassInfo* classInfo,
                                             nsIXPCScriptable* scriptable);

  XPCWrappedNativeScope* GetScope() const { return mScope; }

  XPCJSRuntime* GetRuntime() const { return mScope->GetRuntime(); }

  JSObject* GetJSProtoObject() const { return mJSProtoObject; }

  JSObject* GetJSProtoObjectPreserveColor() const {
    return mJSProtoObject.unbarrieredGet();
  }

  nsIClassInfo* GetClassInfo() const { return mClassInfo; }

  XPCNativeSet* GetSet() const { return mSet; }

  nsIXPCScriptable* GetScriptable() const { return mScriptable; }

  void JSProtoObjectFinalized(JS::GCContext* gcx, JSObject* obj);
  void JSProtoObjectMoved(JSObject* obj, const JSObject* old);

  static XPCWrappedNativeProto* Get(JSObject* obj);

  void SystemIsBeingShutDown();

  void DebugDump(int16_t depth);

  void TraceSelf(JSTracer* trc) {
    if (mJSProtoObject) {
      TraceEdge(trc, &mJSProtoObject, "XPCWrappedNativeProto::mJSProtoObject");
    }
  }

  void TraceJS(JSTracer* trc) { TraceSelf(trc); }

  // NOP. This is just here to make the AutoMarkingPtr code compile.
  void Mark() const {}
  inline void AutoTrace(JSTracer* trc) {}

  ~XPCWrappedNativeProto();

 protected:
  // disable copy ctor and assignment
  XPCWrappedNativeProto(const XPCWrappedNativeProto& r) = delete;
  XPCWrappedNativeProto& operator=(const XPCWrappedNativeProto& r) = delete;

  // hide ctor
  XPCWrappedNativeProto(XPCWrappedNativeScope* Scope, nsIClassInfo* ClassInfo,
                        RefPtr<XPCNativeSet>&& Set);

  bool Init(JSContext* cx, nsIXPCScriptable* scriptable);

 private:
#ifdef DEBUG
  static int32_t gDEBUG_LiveProtoCount;
#endif

 private:
  XPCWrappedNativeScope* mScope;
  JS::Heap<JSObject*> mJSProtoObject;
  nsCOMPtr<nsIClassInfo> mClassInfo;
  RefPtr<XPCNativeSet> mSet;
  nsCOMPtr<nsIXPCScriptable> mScriptable;
};

/***********************************************/
// XPCWrappedNativeTearOff represents the info needed to make calls to one
// interface on the underlying native object of a XPCWrappedNative.

class XPCWrappedNativeTearOff final {
 public:
  enum Slots { FlatObjectSlot, TearOffSlot, SlotCount };

  bool IsAvailable() const { return mInterface == nullptr; }
  bool IsReserved() const { return mInterface == (XPCNativeInterface*)1; }
  bool IsValid() const { return !IsAvailable() && !IsReserved(); }
  void SetReserved() { mInterface = (XPCNativeInterface*)1; }

  XPCNativeInterface* GetInterface() const { return mInterface; }
  nsISupports* GetNative() const { return mNative; }
  JSObject* GetJSObject();
  JSObject* GetJSObjectPreserveColor() const;
  void SetInterface(XPCNativeInterface* Interface) { mInterface = Interface; }
  void SetNative(nsISupports* Native) { mNative = Native; }
  already_AddRefed<nsISupports> TakeNative() { return mNative.forget(); }
  void SetJSObject(JSObject* JSObj);

  void JSObjectFinalized() { SetJSObject(nullptr); }
  void JSObjectMoved(JSObject* obj, const JSObject* old);

  static XPCWrappedNativeTearOff* Get(JSObject* obj);

  XPCWrappedNativeTearOff() : mInterface(nullptr), mJSObject(nullptr) {
    MOZ_COUNT_CTOR(XPCWrappedNativeTearOff);
  }
  ~XPCWrappedNativeTearOff();

  // NOP. This is just here to make the AutoMarkingPtr code compile.
  inline void TraceJS(JSTracer* trc) {}
  inline void AutoTrace(JSTracer* trc) {}

  void Mark() { mJSObject.setFlags(1); }
  void Unmark() { mJSObject.unsetFlags(1); }
  bool IsMarked() const { return mJSObject.hasFlag(1); }

  XPCWrappedNativeTearOff* AddTearOff() {
    MOZ_ASSERT(!mNextTearOff);
    mNextTearOff = mozilla::MakeUnique<XPCWrappedNativeTearOff>();
    return mNextTearOff.get();
  }

  XPCWrappedNativeTearOff* GetNextTearOff() { return mNextTearOff.get(); }

 private:
  XPCWrappedNativeTearOff(const XPCWrappedNativeTearOff& r) = delete;
  XPCWrappedNativeTearOff& operator=(const XPCWrappedNativeTearOff& r) = delete;

 private:
  XPCNativeInterface* mInterface;
  // mNative is an nsRefPtr not an nsCOMPtr because it may not be the canonical
  // nsISupports pointer.
  RefPtr<nsISupports> mNative;
  JS::TenuredHeap<JSObject*> mJSObject;
  mozilla::UniquePtr<XPCWrappedNativeTearOff> mNextTearOff;
};

/***************************************************************************/
// XPCWrappedNative the wrapper around one instance of a native xpcom object
// to be used from JavaScript.

class XPCWrappedNative final : public nsIXPConnectWrappedNative {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_CYCLE_COLLECTION_CLASS(XPCWrappedNative)

  JSObject* GetJSObject() override;

  bool IsValid() const { return mFlatJSObject.hasFlag(FLAT_JS_OBJECT_VALID); }

  nsresult DebugDump(int16_t depth);

#define XPC_SCOPE_WORD(s) (intptr_t(s))
#define XPC_SCOPE_MASK (intptr_t(0x3))
#define XPC_SCOPE_TAG (intptr_t(0x1))
#define XPC_WRAPPER_EXPIRED (intptr_t(0x2))

  static inline bool IsTaggedScope(XPCWrappedNativeScope* s) {
    return XPC_SCOPE_WORD(s) & XPC_SCOPE_TAG;
  }

  static inline XPCWrappedNativeScope* TagScope(XPCWrappedNativeScope* s) {
    MOZ_ASSERT(!IsTaggedScope(s), "bad pointer!");
    return (XPCWrappedNativeScope*)(XPC_SCOPE_WORD(s) | XPC_SCOPE_TAG);
  }

  static inline XPCWrappedNativeScope* UnTagScope(XPCWrappedNativeScope* s) {
    return (XPCWrappedNativeScope*)(XPC_SCOPE_WORD(s) & ~XPC_SCOPE_TAG);
  }

  inline bool IsWrapperExpired() const {
    return XPC_SCOPE_WORD(mMaybeScope) & XPC_WRAPPER_EXPIRED;
  }

  bool HasProto() const { return !IsTaggedScope(mMaybeScope); }

  XPCWrappedNativeProto* GetProto() const {
    return HasProto() ? (XPCWrappedNativeProto*)(XPC_SCOPE_WORD(mMaybeProto) &
                                                 ~XPC_SCOPE_MASK)
                      : nullptr;
  }

  XPCWrappedNativeScope* GetScope() const {
    return GetProto() ? GetProto()->GetScope()
                      : (XPCWrappedNativeScope*)(XPC_SCOPE_WORD(mMaybeScope) &
                                                 ~XPC_SCOPE_MASK);
  }

  nsISupports* GetIdentityObject() const { return mIdentity; }

  /**
   * This getter clears the gray bit before handing out the JSObject which
   * means that the object is guaranteed to be kept alive past the next CC.
   */
  JSObject* GetFlatJSObject() const { return mFlatJSObject; }

  /**
   * This getter does not change the color of the JSObject meaning that the
   * object returned is not guaranteed to be kept alive past the next CC.
   *
   * This should only be called if you are certain that the return value won't
   * be passed into a JS API function and that it won't be stored without
   * being rooted (or otherwise signaling the stored value to the CC).
   */
  JSObject* GetFlatJSObjectPreserveColor() const {
    return mFlatJSObject.unbarrieredGetPtr();
  }

  XPCNativeSet* GetSet() const { return mSet; }

  void SetSet(already_AddRefed<XPCNativeSet> set) { mSet = set; }

  static XPCWrappedNative* Get(JSObject* obj) {
    MOZ_ASSERT(xpc::IsWrappedNativeReflector(obj));
    return JS::GetObjectISupports<XPCWrappedNative>(obj);
  }

 private:
  void SetFlatJSObject(JSObject* object);
  void UnsetFlatJSObject();

  inline void ExpireWrapper() {
    mMaybeScope = (XPCWrappedNativeScope*)(XPC_SCOPE_WORD(mMaybeScope) |
                                           XPC_WRAPPER_EXPIRED);
  }

 public:
  nsIXPCScriptable* GetScriptable() const { return mScriptable; }

  nsIClassInfo* GetClassInfo() const {
    return IsValid() && HasProto() ? GetProto()->GetClassInfo() : nullptr;
  }

  bool HasMutatedSet() const {
    return IsValid() && (!HasProto() || GetSet() != GetProto()->GetSet());
  }

  XPCJSRuntime* GetRuntime() const {
    XPCWrappedNativeScope* scope = GetScope();
    return scope ? scope->GetRuntime() : nullptr;
  }

  static nsresult WrapNewGlobal(JSContext* cx, xpcObjectHelper& nativeHelper,
                                nsIPrincipal* principal,
                                JS::RealmOptions& aOptions,
                                XPCWrappedNative** wrappedGlobal);

  static nsresult GetNewOrUsed(JSContext* cx, xpcObjectHelper& helper,
                               XPCWrappedNativeScope* Scope,
                               XPCNativeInterface* Interface,
                               XPCWrappedNative** wrapper);

  void FlatJSObjectFinalized();
  void FlatJSObjectMoved(JSObject* obj, const JSObject* old);

  void SystemIsBeingShutDown();

  enum CallMode { CALL_METHOD, CALL_GETTER, CALL_SETTER };

  static bool CallMethod(XPCCallContext& ccx, CallMode mode = CALL_METHOD);

  static bool GetAttribute(XPCCallContext& ccx) {
    return CallMethod(ccx, CALL_GETTER);
  }

  static bool SetAttribute(XPCCallContext& ccx) {
    return CallMethod(ccx, CALL_SETTER);
  }

  XPCWrappedNativeTearOff* FindTearOff(JSContext* cx,
                                       XPCNativeInterface* aInterface,
                                       bool needJSObject = false,
                                       nsresult* pError = nullptr);
  XPCWrappedNativeTearOff* FindTearOff(JSContext* cx, const nsIID& iid);

  void Mark() const {}

  inline void TraceInside(JSTracer* trc) {
    if (HasProto()) {
      GetProto()->TraceSelf(trc);
    }

    JSObject* obj = mFlatJSObject.unbarrieredGetPtr();
    if (obj && JS_IsGlobalObject(obj)) {
      xpc::TraceXPCGlobal(trc, obj);
    }
  }

  void TraceJS(JSTracer* trc) { TraceInside(trc); }

  void TraceSelf(JSTracer* trc) {
    // If this got called, we're being kept alive by someone who really
    // needs us alive and whole.  Do not let our mFlatJSObject go away.
    // This is the only time we should be tracing our mFlatJSObject,
    // normally somebody else is doing that.
    JS::TraceEdge(trc, &mFlatJSObject, "XPCWrappedNative::mFlatJSObject");
  }

  static void Trace(JSTracer* trc, JSObject* obj);

  void AutoTrace(JSTracer* trc) { TraceSelf(trc); }

  inline void SweepTearOffs();

  // Returns a string that should be freed with js_free, or nullptr on
  // failure.
  char* ToString(XPCWrappedNativeTearOff* to = nullptr) const;

  static nsIXPCScriptable* GatherProtoScriptable(nsIClassInfo* classInfo);

  bool HasExternalReference() const { return mRefCnt > 1; }

  void Suspect(nsCycleCollectionNoteRootCallback& cb);
  void NoteTearoffs(nsCycleCollectionTraversalCallback& cb);

  // Make ctor and dtor protected (rather than private) to placate nsCOMPtr.
 protected:
  XPCWrappedNative() = delete;

  // This ctor is used if this object will have a proto.
  XPCWrappedNative(nsCOMPtr<nsISupports>&& aIdentity,
                   XPCWrappedNativeProto* aProto);

  // This ctor is used if this object will NOT have a proto.
  XPCWrappedNative(nsCOMPtr<nsISupports>&& aIdentity,
                   XPCWrappedNativeScope* aScope, RefPtr<XPCNativeSet>&& aSet);

  virtual ~XPCWrappedNative();
  void Destroy();

 private:
  enum {
    // Flags bits for mFlatJSObject:
    FLAT_JS_OBJECT_VALID = js::Bit(0)
  };

  bool Init(JSContext* cx, nsIXPCScriptable* scriptable);
  bool FinishInit(JSContext* cx);

  bool ExtendSet(JSContext* aCx, XPCNativeInterface* aInterface);

  nsresult InitTearOff(JSContext* cx, XPCWrappedNativeTearOff* aTearOff,
                       XPCNativeInterface* aInterface, bool needJSObject);

  bool InitTearOffJSObject(JSContext* cx, XPCWrappedNativeTearOff* to);

 public:
  static void GatherScriptable(nsISupports* obj, nsIClassInfo* classInfo,
                               nsIXPCScriptable** scrProto,
                               nsIXPCScriptable** scrWrapper);

 private:
  union {
    XPCWrappedNativeScope* mMaybeScope;
    XPCWrappedNativeProto* mMaybeProto;
  };
  RefPtr<XPCNativeSet> mSet;
  JS::TenuredHeap<JSObject*> mFlatJSObject;
  nsCOMPtr<nsIXPCScriptable> mScriptable;
  XPCWrappedNativeTearOff mFirstTearOff;
};

/***************************************************************************
****************************************************************************
*
* Core classes for wrapped JSObject for use from native code...
*
****************************************************************************
***************************************************************************/

/*************************/
// nsXPCWrappedJS is a wrapper for a single JSObject for use from native code.
// nsXPCWrappedJS objects are chained together to represent the various
// interface on the single underlying (possibly aggregate) JSObject.

class nsXPCWrappedJS final : protected nsAutoXPTCStub,
                             public nsIXPConnectWrappedJSUnmarkGray,
                             public nsSupportsWeakReference,
                             public mozilla::LinkedListElement<nsXPCWrappedJS> {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSISUPPORTSWEAKREFERENCE

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(
      nsXPCWrappedJS, nsIXPConnectWrappedJS)

  JSObject* GetJSObject() override;

  // This method is defined in XPCWrappedJSClass.cpp to preserve VCS blame.
  NS_IMETHOD CallMethod(uint16_t methodIndex, const nsXPTMethodInfo* info,
                        nsXPTCMiniVariant* nativeParams) override;

  /*
   * This is rarely called directly. Instead one usually calls
   * XPCConvert::JSObject2NativeInterface which will handles cases where the
   * JS object is already a wrapped native or a DOM object.
   */

  static nsresult GetNewOrUsed(JSContext* cx, JS::HandleObject aJSObj,
                               REFNSIID aIID, nsXPCWrappedJS** wrapper);

  nsISomeInterface* GetXPTCStub() { return mXPTCStub; }

  nsresult DebugDump(int16_t depth);

  /**
   * This getter does not change the color of the JSObject meaning that the
   * object returned is not guaranteed to be kept alive past the next CC.
   *
   * This should only be called if you are certain that the return value won't
   * be passed into a JS API function and that it won't be stored without
   * being rooted (or otherwise signaling the stored value to the CC).
   */
  JSObject* GetJSObjectPreserveColor() const { return mJSObj.unbarrieredGet(); }

  // Returns true if the wrapper chain contains references to multiple
  // compartments. If the wrapper chain contains references to multiple
  // compartments, then it must be registered on the XPCJSContext. Otherwise,
  // it should be registered in the CompartmentPrivate for the compartment of
  // the root's JS object. This will only return correct results when called
  // on the root wrapper and will assert if not called on a root wrapper.
  bool IsMultiCompartment() const;

  const nsXPTInterfaceInfo* GetInfo() const { return mInfo; }
  REFNSIID GetIID() const { return mInfo->IID(); }
  nsXPCWrappedJS* GetRootWrapper() const { return mRoot; }
  nsXPCWrappedJS* GetNextWrapper() const { return mNext; }

  nsXPCWrappedJS* Find(REFNSIID aIID);
  nsXPCWrappedJS* FindInherited(REFNSIID aIID);
  nsXPCWrappedJS* FindOrFindInherited(REFNSIID aIID) {
    nsXPCWrappedJS* wrapper = Find(aIID);
    if (wrapper) {
      return wrapper;
    }
    return FindInherited(aIID);
  }

  bool IsRootWrapper() const { return mRoot == this; }
  bool IsValid() const { return bool(mJSObj); }
  void SystemIsBeingShutDown();

  // These two methods are used by JSObject2WrappedJSMap::FindDyingJSObjects
  // to find non-rooting wrappers for dying JS objects. See the top of
  // XPCWrappedJS.cpp for more details.
  bool IsSubjectToFinalization() const { return IsValid() && mRefCnt == 1; }

  void UpdateObjectPointerAfterGC(JSTracer* trc) {
    MOZ_ASSERT(IsRootWrapper());
    JS_UpdateWeakPointerAfterGC(trc, &mJSObj);
  }

  bool IsAggregatedToNative() const { return mRoot->mOuter != nullptr; }
  nsISupports* GetAggregatedNativeObject() const { return mRoot->mOuter; }
  void SetAggregatedNativeObject(nsISupports* aNative) {
    MOZ_ASSERT(aNative);
    if (mRoot->mOuter) {
      MOZ_ASSERT(mRoot->mOuter == aNative,
                 "Only one aggregated native can be set");
      return;
    }
    mRoot->mOuter = aNative;
  }

  // This method is defined in XPCWrappedJSClass.cpp to preserve VCS blame.
  static void DebugDumpInterfaceInfo(const nsXPTInterfaceInfo* aInfo,
                                     int16_t depth);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  virtual ~nsXPCWrappedJS();

 protected:
  nsXPCWrappedJS() = delete;
  nsXPCWrappedJS(JSContext* cx, JSObject* aJSObj,
                 const nsXPTInterfaceInfo* aInfo, nsXPCWrappedJS* root,
                 nsresult* rv);

  bool CanSkip();
  void Destroy();
  void Unlink();

 private:
  friend class nsIXPConnectWrappedJS;

  JS::Compartment* Compartment() const {
    return JS::GetCompartment(mJSObj.unbarrieredGet());
  }

  // These methods are defined in XPCWrappedJSClass.cpp to preserve VCS blame.
  static const nsXPTInterfaceInfo* GetInterfaceInfo(REFNSIID aIID);

  nsresult DelegatedQueryInterface(REFNSIID aIID, void** aInstancePtr);

  static JSObject* GetRootJSObject(JSContext* cx, JSObject* aJSObj);

  static JSObject* CallQueryInterfaceOnJSObject(JSContext* cx, JSObject* jsobj,
                                                JS::HandleObject scope,
                                                REFNSIID aIID);

  // aObj is the nsXPCWrappedJS's object. We used this as the callee (or |this|
  // if getter or setter).
  // aSyntheticException, if not null, is the exception we should be using.
  // If null, look for an exception on the JSContext hanging off the
  // XPCCallContext.
  static nsresult CheckForException(
      XPCCallContext& ccx, mozilla::dom::AutoEntryScript& aes,
      JS::HandleObject aObj, const char* aPropertyName,
      const char* anInterfaceName,
      mozilla::dom::Exception* aSyntheticException = nullptr);

  static bool GetArraySizeFromParam(const nsXPTMethodInfo* method,
                                    const nsXPTType& type,
                                    nsXPTCMiniVariant* params,
                                    uint32_t* result);

  static bool GetInterfaceTypeFromParam(const nsXPTMethodInfo* method,
                                        const nsXPTType& type,
                                        nsXPTCMiniVariant* params,
                                        nsID* result);

  static void CleanupOutparams(const nsXPTMethodInfo* info,
                               nsXPTCMiniVariant* nativeParams, bool inOutOnly,
                               uint8_t count);

  JS::Heap<JSObject*> mJSObj;
  const nsXPTInterfaceInfo* const mInfo;
  nsXPCWrappedJS* mRoot;  // If mRoot != this, it is an owning pointer.
  nsXPCWrappedJS* mNext;
  nsCOMPtr<nsISupports> mOuter;  // only set in root
};

/***************************************************************************
****************************************************************************
*
* All manner of utility classes follow...
*
****************************************************************************
***************************************************************************/

namespace xpc {

// A wrapper around JS iterators which presents an equivalent
// nsISimpleEnumerator interface for their contents.
class XPCWrappedJSIterator final : public nsISimpleEnumerator {
 public:
  NS_DECL_CYCLE_COLLECTION_CLASS(XPCWrappedJSIterator)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSISIMPLEENUMERATOR
  NS_DECL_NSISIMPLEENUMERATORBASE

  explicit XPCWrappedJSIterator(nsIJSEnumerator* aEnum);

 private:
  ~XPCWrappedJSIterator() = default;

  nsCOMPtr<nsIJSEnumerator> mEnum;
  nsCOMPtr<nsIGlobalObject> mGlobal;
  nsCOMPtr<nsISupports> mNext;
  mozilla::Maybe<bool> mHasNext;
};

}  // namespace xpc

/***************************************************************************/
// class here just for static methods
class XPCConvert {
 public:
  /**
   * Convert a native object into a JS::Value.
   *
   * @param cx the JSContext representing the global we want the value in
   * @param d [out] the resulting JS::Value
   * @param s the native object we're working with
   * @param type the type of object that s is
   * @param iid the interface of s that we want
   * @param scope the default scope to put on the new JSObject's parent
   *        chain
   * @param pErr [out] relevant error code, if any.
   */

  static bool NativeData2JS(JSContext* cx, JS::MutableHandleValue d,
                            const void* s, const nsXPTType& type,
                            const nsID* iid, uint32_t arrlen, nsresult* pErr);

  static bool JSData2Native(JSContext* cx, void* d, JS::HandleValue s,
                            const nsXPTType& type, const nsID* iid,
                            uint32_t arrlen, nsresult* pErr);

  /**
   * Convert a native nsISupports into a JSObject.
   *
   * @param cx the JSContext representing the global we want the object in.
   * @param dest [out] the resulting JSObject
   * @param src the native object we're working with
   * @param iid the interface of src that we want (may be null)
   * @param cache the wrapper cache for src (may be null, in which case src
   *              will be QI'ed to get the cache)
   * @param allowNativeWrapper if true, this method may wrap the resulting
   *        JSObject in an XPCNativeWrapper and return that, as needed.
   * @param pErr [out] relevant error code, if any.
   * @param src_is_identity optional performance hint. Set to true only
   *                        if src is the identity pointer.
   */
  static bool NativeInterface2JSObject(JSContext* cx,
                                       JS::MutableHandleValue dest,
                                       xpcObjectHelper& aHelper,
                                       const nsID* iid, bool allowNativeWrapper,
                                       nsresult* pErr);

  static bool GetNativeInterfaceFromJSObject(void** dest, JSObject* src,
                                             const nsID* iid, nsresult* pErr);
  static bool JSObject2NativeInterface(JSContext* cx, void** dest,
                                       JS::HandleObject src, const nsID* iid,
                                       nsISupports* aOuter, nsresult* pErr);

  // Note - This return the XPCWrappedNative, rather than the native itself,
  // for the WN case. You probably want UnwrapReflectorToISupports.
  static bool GetISupportsFromJSObject(JSObject* obj, nsISupports** iface);

  static nsresult JSValToXPCException(JSContext* cx, JS::MutableHandleValue s,
                                      const char* ifaceName,
                                      const char* methodName,
                                      mozilla::dom::Exception** exception);

  static nsresult ConstructException(nsresult rv, const char* message,
                                     const char* ifaceName,
                                     const char* methodName, nsISupports* data,
                                     mozilla::dom::Exception** exception,
                                     JSContext* cx, JS::Value* jsExceptionPtr);

 private:
  /**
   * Convert a native array into a JS::Value.
   *
   * @param cx the JSContext we're working with and in whose global the array
   *           should be created.
   * @param d [out] the resulting JS::Value
   * @param buf the native buffer containing input values
   * @param type the type of objects in the array
   * @param iid the interface of each object in the array that we want
   * @param count the number of items in the array
   * @param scope the default scope to put on the new JSObjects' parent chain
   * @param pErr [out] relevant error code, if any.
   */
  static bool NativeArray2JS(JSContext* cx, JS::MutableHandleValue d,
                             const void* buf, const nsXPTType& type,
                             const nsID* iid, uint32_t count, nsresult* pErr);

  using ArrayAllocFixupLen = std::function<void*(uint32_t*)>;

  /**
   * Convert a JS::Value into a native array.
   *
   * @param cx the JSContext we're working with
   * @param aJSVal the JS::Value to convert
   * @param aEltType the type of objects in the array
   * @param aIID the interface of each object in the array
   * @param pErr [out] relevant error code, if any
   * @param aAllocFixupLen function called with the JS Array's length to
   *                       allocate the backing buffer. This function may
   *                       modify the length of array to be converted.
   */
  static bool JSArray2Native(JSContext* cx, JS::HandleValue aJSVal,
                             const nsXPTType& aEltType, const nsIID* aIID,
                             nsresult* pErr,
                             const ArrayAllocFixupLen& aAllocFixupLen);

  XPCConvert() = delete;
};

/***************************************************************************/
// code for throwing exceptions into JS

class nsXPCException;

class XPCThrower {
 public:
  static void Throw(nsresult rv, JSContext* cx);
  static void Throw(nsresult rv, XPCCallContext& ccx);
  static void ThrowBadResult(nsresult rv, nsresult result, XPCCallContext& ccx);
  static void ThrowBadParam(nsresult rv, unsigned paramNum,
                            XPCCallContext& ccx);
  static bool SetVerbosity(bool state) {
    bool old = sVerbose;
    sVerbose = state;
    return old;
  }

  static bool CheckForPendingException(nsresult result, JSContext* cx);

 private:
  static void Verbosify(XPCCallContext& ccx, char** psz, bool own);

 private:
  static bool sVerbose;
};

/***************************************************************************/

class nsXPCException {
 public:
  static bool NameAndFormatForNSResult(nsresult rv, const char** name,
                                       const char** format);

  static const void* IterateNSResults(nsresult* rv, const char** name,
                                      const char** format, const void** iterp);

  static uint32_t GetNSResultCount();
};

/***************************************************************************/
// 'Components' object implementation.

class nsXPCComponents final : public nsIXPCComponents {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIXPCCOMPONENTS

 public:
  void SystemIsBeingShutDown() { ClearMembers(); }

  XPCWrappedNativeScope* GetScope() { return mScope; }

 protected:
  ~nsXPCComponents();

  explicit nsXPCComponents(XPCWrappedNativeScope* aScope);
  void ClearMembers();

  XPCWrappedNativeScope* mScope;

  RefPtr<nsXPCComponents_Interfaces> mInterfaces;
  RefPtr<nsXPCComponents_Results> mResults;
  RefPtr<nsXPCComponents_Classes> mClasses;
  RefPtr<nsXPCComponents_ID> mID;
  RefPtr<nsXPCComponents_Exception> mException;
  RefPtr<nsXPCComponents_Constructor> mConstructor;
  RefPtr<nsXPCComponents_Utils> mUtils;

  friend class XPCWrappedNativeScope;
};

/******************************************************************************
 * Handles pre/post script processing.
 */
class MOZ_RAII AutoScriptEvaluate {
 public:
  /**
   * Saves the JSContext as well as initializing our state
   * @param cx The JSContext, this can be null, we don't do anything then
   */
  explicit AutoScriptEvaluate(JSContext* cx)
      : mJSContext(cx), mEvaluated(false) {}

  /**
   * Does the pre script evaluation.
   * This function should only be called once, and will assert if called
   * more than once
   */

  bool StartEvaluating(JS::HandleObject scope);

  /**
   * Does the post script evaluation.
   */
  ~AutoScriptEvaluate();

 private:
  JSContext* mJSContext;
  mozilla::Maybe<JS::AutoSaveExceptionState> mState;
  bool mEvaluated;
  mozilla::Maybe<JSAutoRealm> mAutoRealm;

  // No copying or assignment allowed
  AutoScriptEvaluate(const AutoScriptEvaluate&) = delete;
  AutoScriptEvaluate& operator=(const AutoScriptEvaluate&) = delete;
};

/***************************************************************************/
class MOZ_RAII AutoResolveName {
 public:
  AutoResolveName(XPCCallContext& ccx, JS::HandleId name)
      : mContext(ccx.GetContext()),
        mOld(ccx, mContext->SetResolveName(name))
#ifdef DEBUG
        ,
        mCheck(ccx, name)
#endif
  {
  }

  ~AutoResolveName() {
    mozilla::DebugOnly<jsid> old = mContext->SetResolveName(mOld);
    MOZ_ASSERT(old == mCheck, "Bad Nesting!");
  }

 private:
  XPCJSContext* mContext;
  JS::RootedId mOld;
#ifdef DEBUG
  JS::RootedId mCheck;
#endif
};

/***************************************************************************/
// AutoMarkingPtr is the base class for the various AutoMarking pointer types
// below. This system allows us to temporarily protect instances of our garbage
// collected types after they are constructed but before they are safely
// attached to other rooted objects.
// This base class has pure virtual support for marking.

class AutoMarkingPtr {
 public:
  explicit AutoMarkingPtr(JSContext* cx) {
    mRoot = XPCJSContext::Get()->GetAutoRootsAdr();
    mNext = *mRoot;
    *mRoot = this;
  }

  virtual ~AutoMarkingPtr() {
    if (mRoot) {
      MOZ_ASSERT(*mRoot == this);
      *mRoot = mNext;
    }
  }

  void TraceJSAll(JSTracer* trc) {
    for (AutoMarkingPtr* cur = this; cur; cur = cur->mNext) {
      cur->TraceJS(trc);
    }
  }

  void MarkAfterJSFinalizeAll() {
    for (AutoMarkingPtr* cur = this; cur; cur = cur->mNext) {
      cur->MarkAfterJSFinalize();
    }
  }

 protected:
  virtual void TraceJS(JSTracer* trc) = 0;
  virtual void MarkAfterJSFinalize() = 0;

 private:
  AutoMarkingPtr** mRoot;
  AutoMarkingPtr* mNext;
};

template <class T>
class TypedAutoMarkingPtr : public AutoMarkingPtr {
 public:
  explicit TypedAutoMarkingPtr(JSContext* cx)
      : AutoMarkingPtr(cx), mPtr(nullptr) {}
  TypedAutoMarkingPtr(JSContext* cx, T* ptr) : AutoMarkingPtr(cx), mPtr(ptr) {}

  T* get() const { return mPtr; }
  operator T*() const { return mPtr; }
  T* operator->() const { return mPtr; }

  TypedAutoMarkingPtr<T>& operator=(T* ptr) {
    mPtr = ptr;
    return *this;
  }

 protected:
  virtual void TraceJS(JSTracer* trc) override {
    if (mPtr) {
      mPtr->TraceJS(trc);
      mPtr->AutoTrace(trc);
    }
  }

  virtual void MarkAfterJSFinalize() override {
    if (mPtr) {
      mPtr->Mark();
    }
  }

 private:
  T* mPtr;
};

using AutoMarkingWrappedNativePtr = TypedAutoMarkingPtr<XPCWrappedNative>;
using AutoMarkingWrappedNativeTearOffPtr =
    TypedAutoMarkingPtr<XPCWrappedNativeTearOff>;
using AutoMarkingWrappedNativeProtoPtr =
    TypedAutoMarkingPtr<XPCWrappedNativeProto>;

/***************************************************************************/
// Definitions in XPCVariant.cpp.

// {1809FD50-91E8-11d5-90F9-0010A4E73D9A}
#define XPCVARIANT_IID \
  {0x1809fd50, 0x91e8, 0x11d5, {0x90, 0xf9, 0x0, 0x10, 0xa4, 0xe7, 0x3d, 0x9a}}

// {DC524540-487E-4501-9AC7-AAA784B17C1C}
#define XPCVARIANT_CID \
  {0xdc524540, 0x487e, 0x4501, {0x9a, 0xc7, 0xaa, 0xa7, 0x84, 0xb1, 0x7c, 0x1c}}

class XPCVariant : public nsIVariant {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIVARIANT
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(XPCVariant)

  // If this class ever implements nsIWritableVariant, take special care with
  // the case when mJSVal is JSVAL_STRING, since we don't own the data in
  // that case.

  // We #define and iid so that out module local code can use QI to detect
  // if a given nsIVariant is in fact an XPCVariant.
  NS_DECLARE_STATIC_IID_ACCESSOR(XPCVARIANT_IID)

  static already_AddRefed<XPCVariant> newVariant(JSContext* cx,
                                                 const JS::Value& aJSVal);

  /**
   * This getter clears the gray bit before handing out the Value if the Value
   * represents a JSObject. That means that the object is guaranteed to be
   * kept alive past the next CC.
   */
  JS::Value GetJSVal() const { return mJSVal; }

 protected:
  /**
   * This getter does not change the color of the Value (if it represents a
   * JSObject) meaning that the value returned is not guaranteed to be kept
   * alive past the next CC.
   *
   * This should only be called if you are certain that the return value won't
   * be passed into a JS API function and that it won't be stored without
   * being rooted (or otherwise signaling the stored value to the CC).
   */
  JS::Value GetJSValPreserveColor() const { return mJSVal.unbarrieredGet(); }

  XPCVariant(JSContext* cx, const JS::Value& aJSVal);

 public:
  /**
   * Convert a variant into a JS::Value.
   *
   * @param cx the context for the whole procedure
   * @param variant the variant to convert
   * @param scope the default scope to put on the new JSObject's parent chain
   * @param pErr [out] relevant error code, if any.
   * @param pJSVal [out] the resulting jsval.
   */
  static bool VariantDataToJS(JSContext* cx, nsIVariant* variant,
                              nsresult* pErr, JS::MutableHandleValue pJSVal);

 protected:
  virtual ~XPCVariant();

  bool InitializeData(JSContext* cx);

  void Cleanup();

  nsDiscriminatedUnion mData;
  JS::Heap<JS::Value> mJSVal;
  bool mReturnRawObject;
};

NS_DEFINE_STATIC_IID_ACCESSOR(XPCVariant, XPCVARIANT_IID)

/***************************************************************************/
// Utilities

inline JSContext* xpc_GetSafeJSContext() {
  return XPCJSContext::Get()->Context();
}

namespace xpc {

// JSNatives to expose atob and btoa in various non-DOM XPConnect scopes.
bool Atob(JSContext* cx, unsigned argc, JS::Value* vp);

bool Btoa(JSContext* cx, unsigned argc, JS::Value* vp);

// Helper function that creates a JSFunction that wraps a native function that
// forwards the call to the original 'callable'.
class FunctionForwarderOptions;
bool NewFunctionForwarder(JSContext* cx, JS::HandleId id,
                          JS::HandleObject callable,
                          FunctionForwarderOptions& options,
                          JS::MutableHandleValue vp);

// Old fashioned xpc error reporter. Try to use JS_ReportError instead.
nsresult ThrowAndFail(nsresult errNum, JSContext* cx, bool* retval);

struct GlobalProperties {
  GlobalProperties() { mozilla::PodZero(this); }
  bool Parse(JSContext* cx, JS::HandleObject obj);
  bool DefineInXPCComponents(JSContext* cx, JS::HandleObject obj);
  bool DefineInSandbox(JSContext* cx, JS::HandleObject obj);

  // Interface objects we can expose.
  bool AbortController : 1;
  bool Blob : 1;
  bool ChromeUtils : 1;
  bool CSS : 1;
  bool CSSRule : 1;
  bool CustomStateSet : 1;
  bool Directory : 1;
  bool Document : 1;
  bool DOMException : 1;
  bool DOMParser : 1;
  bool DOMTokenList : 1;
  bool Element : 1;
  bool Event : 1;
  bool File : 1;
  bool FileReader : 1;
  bool FormData : 1;
  bool Headers : 1;
  bool IOUtils : 1;
  bool InspectorCSSParser : 1;
  bool InspectorUtils : 1;
  bool MessageChannel : 1;
  bool MIDIInputMap : 1;
  bool MIDIOutputMap : 1;
  bool Node : 1;
  bool NodeFilter : 1;
  bool PathUtils : 1;
  bool Performance : 1;
  bool PromiseDebugging : 1;
  bool Range : 1;
  bool Selection : 1;
  bool TextDecoder : 1;
  bool TextEncoder : 1;
  bool TrustedHTML : 1;
  bool TrustedScript : 1;
  bool TrustedScriptURL : 1;
  bool URL : 1;
  bool URLSearchParams : 1;
  bool XMLHttpRequest : 1;
  bool WebSocket : 1;
  bool Window : 1;
  bool XMLSerializer : 1;
  bool ReadableStream : 1;

  // Ad-hoc property names we implement.
  bool atob : 1;
  bool btoa : 1;
  bool caches : 1;
  bool crypto : 1;
  bool fetch : 1;
  bool storage : 1;
  bool structuredClone : 1;
  bool locks : 1;
  bool indexedDB : 1;
  bool isSecureContext : 1;
  bool rtcIdentityProvider : 1;

 private:
  bool Define(JSContext* cx, JS::HandleObject obj);
};

// Infallible.
already_AddRefed<nsIXPCComponents_utils_Sandbox> NewSandboxConstructor();

// Returns true if class of 'obj' is SandboxClass.
bool IsSandbox(JSObject* obj);

class MOZ_STACK_CLASS OptionsBase {
 public:
  explicit OptionsBase(JSContext* cx = xpc_GetSafeJSContext(),
                       JSObject* options = nullptr)
      : mCx(cx), mObject(cx, options) {}

  virtual bool Parse() = 0;

 protected:
  bool ParseValue(const char* name, JS::MutableHandleValue prop,
                  bool* found = nullptr);
  bool ParseBoolean(const char* name, bool* prop);
  bool ParseObject(const char* name, JS::MutableHandleObject prop);
  bool ParseJSString(const char* name, JS::MutableHandleString prop);
  bool ParseString(const char* name, nsCString& prop);
  bool ParseString(const char* name, nsString& prop);
  bool ParseOptionalString(const char* name, mozilla::Maybe<nsString>& prop);
  bool ParseId(const char* name, JS::MutableHandleId id);
  bool ParseUInt32(const char* name, uint32_t* prop);

  JSContext* mCx;
  JS::RootedObject mObject;
};

class MOZ_STACK_CLASS SandboxOptions : public OptionsBase {
 public:
  explicit SandboxOptions(JSContext* cx = xpc_GetSafeJSContext(),
                          JSObject* options = nullptr)
      : OptionsBase(cx, options),
        wantXrays(true),
        allowWaivers(true),
        wantComponents(true),
        wantExportHelpers(false),
        isWebExtensionContentScript(false),
        proto(cx),
        sameZoneAs(cx),
        forceSecureContext(false),
        freshCompartment(false),
        freshZone(false),
        isUAWidgetScope(false),
        invisibleToDebugger(false),
        discardSource(false),
        metadata(cx),
        userContextId(0),
        originAttributes(cx),
        alwaysUseFdlibm(false) {}

  virtual bool Parse() override;

  bool wantXrays;
  bool allowWaivers;
  bool wantComponents;
  bool wantExportHelpers;
  bool isWebExtensionContentScript;
  JS::RootedObject proto;
  mozilla::Maybe<nsString> sandboxContentSecurityPolicy;
  nsCString sandboxName;
  JS::RootedObject sameZoneAs;
  bool forceSecureContext;
  bool freshCompartment;
  bool freshZone;
  bool isUAWidgetScope;
  bool invisibleToDebugger;
  bool discardSource;
  GlobalProperties globalProperties;
  JS::RootedValue metadata;
  uint32_t userContextId;
  JS::RootedObject originAttributes;
  bool alwaysUseFdlibm;

 protected:
  bool ParseGlobalProperties();
};

class MOZ_STACK_CLASS CreateObjectInOptions : public OptionsBase {
 public:
  explicit CreateObjectInOptions(JSContext* cx = xpc_GetSafeJSContext(),
                                 JSObject* options = nullptr)
      : OptionsBase(cx, options), defineAs(cx, JS::PropertyKey::Void()) {}

  virtual bool Parse() override { return ParseId("defineAs", &defineAs); }

  JS::RootedId defineAs;
};

class MOZ_STACK_CLASS ExportFunctionOptions : public OptionsBase {
 public:
  explicit ExportFunctionOptions(JSContext* cx = xpc_GetSafeJSContext(),
                                 JSObject* options = nullptr)
      : OptionsBase(cx, options),
        defineAs(cx, JS::PropertyKey::Void()),
        allowCrossOriginArguments(false) {}

  virtual bool Parse() override {
    return ParseId("defineAs", &defineAs) &&
           ParseBoolean("allowCrossOriginArguments",
                        &allowCrossOriginArguments);
  }

  JS::RootedId defineAs;
  bool allowCrossOriginArguments;
};

class MOZ_STACK_CLASS FunctionForwarderOptions : public OptionsBase {
 public:
  explicit FunctionForwarderOptions(JSContext* cx = xpc_GetSafeJSContext(),
                                    JSObject* options = nullptr)
      : OptionsBase(cx, options), allowCrossOriginArguments(false) {}

  JSObject* ToJSObject(JSContext* cx) {
    JS::RootedObject obj(cx, JS_NewObjectWithGivenProto(cx, nullptr, nullptr));
    if (!obj) {
      return nullptr;
    }

    JS::RootedValue val(cx);
    unsigned attrs = JSPROP_READONLY | JSPROP_PERMANENT;
    val = JS::BooleanValue(allowCrossOriginArguments);
    if (!JS_DefineProperty(cx, obj, "allowCrossOriginArguments", val, attrs)) {
      return nullptr;
    }

    return obj;
  }

  virtual bool Parse() override {
    return ParseBoolean("allowCrossOriginArguments",
                        &allowCrossOriginArguments);
  }

  bool allowCrossOriginArguments;
};

class MOZ_STACK_CLASS StackScopedCloneOptions : public OptionsBase {
 public:
  explicit StackScopedCloneOptions(JSContext* cx = xpc_GetSafeJSContext(),
                                   JSObject* options = nullptr)
      : OptionsBase(cx, options),
        wrapReflectors(false),
        cloneFunctions(false),
        deepFreeze(false) {}

  virtual bool Parse() override {
    return ParseBoolean("wrapReflectors", &wrapReflectors) &&
           ParseBoolean("cloneFunctions", &cloneFunctions) &&
           ParseBoolean("deepFreeze", &deepFreeze);
  }

  // When a reflector is encountered, wrap it rather than aborting the clone.
  bool wrapReflectors;

  // When a function is encountered, clone it (exportFunction-style) rather than
  // aborting the clone.
  bool cloneFunctions;

  // If true, the resulting object is deep-frozen after being cloned.
  bool deepFreeze;
};

JSObject* CreateGlobalObject(JSContext* cx, const JSClass* clasp,
                             nsIPrincipal* principal,
                             JS::RealmOptions& aOptions);

// Finish initializing an already-created, not-yet-exposed-to-script global
// object.  This will attach a Components object (if necessary) and call
// |JS_FireOnNewGlobalObject| (if necessary).
//
// If you must modify compartment options, see InitGlobalObjectOptions above.
bool InitGlobalObject(JSContext* aJSContext, JS::Handle<JSObject*> aGlobal,
                      uint32_t aFlags);

// Helper for creating a sandbox object to use for evaluating
// untrusted code completely separated from all other code in the
// system using EvalInSandbox(). Takes the JSContext on which to
// do setup etc on, puts the sandbox object in *vp (which must be
// rooted by the caller), and uses the principal that's either
// directly passed in prinOrSop or indirectly as an
// nsIScriptObjectPrincipal holding the principal. If no principal is
// reachable through prinOrSop, a new null principal will be created
// and used.
nsresult CreateSandboxObject(JSContext* cx, JS::MutableHandleValue vp,
                             nsISupports* prinOrSop,
                             xpc::SandboxOptions& options);
// Helper for evaluating scripts in a sandbox object created with
// CreateSandboxObject(). The caller is responsible of ensuring
// that *rval doesn't get collected during the call or usage after the
// call. This helper will use filename and lineNo for error reporting,
// and if no filename is provided it will use the codebase from the
// principal and line number 1 as a fallback.
nsresult EvalInSandbox(JSContext* cx, JS::HandleObject sandbox,
                       const nsAString& source, const nsACString& filename,
                       int32_t lineNo, bool enforceFilenameRestrictions,
                       JS::MutableHandleValue rval);

// Helper for retrieving metadata stored in a reserved slot. The metadata
// is set during the sandbox creation using the "metadata" option.
nsresult GetSandboxMetadata(JSContext* cx, JS::HandleObject sandboxArg,
                            JS::MutableHandleValue rval);

[[nodiscard]] nsresult SetSandboxMetadata(JSContext* cx,
                                          JS::HandleObject sandboxArg,
                                          JS::HandleValue metadata);

bool CreateObjectIn(JSContext* cx, JS::HandleValue vobj,
                    CreateObjectInOptions& options,
                    JS::MutableHandleValue rval);

bool EvalInWindow(JSContext* cx, const nsAString& source,
                  JS::HandleObject scope, JS::MutableHandleValue rval);

bool ExportFunction(JSContext* cx, JS::HandleValue vscope,
                    JS::HandleValue vfunction, JS::HandleValue voptions,
                    JS::MutableHandleValue rval);

bool CloneInto(JSContext* cx, JS::HandleValue vobj, JS::HandleValue vscope,
               JS::HandleValue voptions, JS::MutableHandleValue rval);

bool StackScopedClone(JSContext* cx, StackScopedCloneOptions& options,
                      JS::HandleObject sourceScope, JS::MutableHandleValue val);

} /* namespace xpc */

/***************************************************************************/
// Inlined utilities.

inline bool xpc_ForcePropertyResolve(JSContext* cx, JS::HandleObject obj,
                                     jsid id);

inline jsid GetJSIDByIndex(JSContext* cx, unsigned index);

namespace xpc {

enum WrapperDenialType {
  WrapperDenialForXray = 0,
  WrapperDenialForCOW,
  WrapperDenialTypeCount
};
bool ReportWrapperDenial(JSContext* cx, JS::HandleId id, WrapperDenialType type,
                         const char* reason);

class CompartmentOriginInfo {
 public:
  CompartmentOriginInfo(const CompartmentOriginInfo&) = delete;

  CompartmentOriginInfo(mozilla::BasePrincipal* aOrigin,
                        const mozilla::SiteIdentifier& aSite)
      : mOrigin(aOrigin), mSite(aSite) {
    MOZ_ASSERT(aOrigin);
    MOZ_ASSERT(aSite.IsInitialized());
  }

  bool IsSameOrigin(nsIPrincipal* aOther) const;

  // Does the principal of compartment a subsume the principal of compartment b?
  static bool Subsumes(JS::Compartment* aCompA, JS::Compartment* aCompB);
  static bool SubsumesIgnoringFPD(JS::Compartment* aCompA,
                                  JS::Compartment* aCompB);

  bool MightBeWebContent() const;

  // Note: this principal must not be used for subsumes/equality checks
  // considering document.domain. See mOrigin.
  mozilla::BasePrincipal* GetPrincipalIgnoringDocumentDomain() const {
    return mOrigin;
  }

  const mozilla::SiteIdentifier& SiteRef() const { return mSite; }

  bool HasChangedDocumentDomain() const { return mChangedDocumentDomain; }
  void SetChangedDocumentDomain() { mChangedDocumentDomain = true; }

 private:
  // All globals in the compartment must have this origin. Note that
  // individual globals and principals can have their domain changed via
  // document.domain, so this principal must not be used for things like
  // subsumesConsideringDomain or equalsConsideringDomain. Use the realm's
  // principal for that.
  RefPtr<mozilla::BasePrincipal> mOrigin;

  // In addition to the origin we also store the SiteIdentifier. When realms
  // in different compartments can become same-origin (via document.domain),
  // these compartments must have equal SiteIdentifiers. (This is derived from
  // mOrigin but we cache it here for performance reasons.)
  mozilla::SiteIdentifier mSite;

  // True if any global in this compartment mutated document.domain.
  bool mChangedDocumentDomain = false;
};

// The CompartmentPrivate contains XPConnect-specific stuff related to each JS
// compartment. Since compartments are trust domains, this means mostly
// information needed to select the right security policy for cross-compartment
// wrappers.
class CompartmentPrivate {
  CompartmentPrivate() = delete;
  CompartmentPrivate(const CompartmentPrivate&) = delete;

 public:
  CompartmentPrivate(JS::Compartment* c,
                     mozilla::UniquePtr<XPCWrappedNativeScope> scope,
                     mozilla::BasePrincipal* origin,
                     const mozilla::SiteIdentifier& site);

  ~CompartmentPrivate();

  static CompartmentPrivate* Get(JS::Compartment* compartment) {
    MOZ_ASSERT(compartment);
    void* priv = JS_GetCompartmentPrivate(compartment);
    return static_cast<CompartmentPrivate*>(priv);
  }

  static CompartmentPrivate* Get(JS::Realm* realm) {
    MOZ_ASSERT(realm);
    JS::Compartment* compartment = JS::GetCompartmentForRealm(realm);
    return Get(compartment);
  }

  static CompartmentPrivate* Get(JSObject* object) {
    JS::Compartment* compartment = JS::GetCompartment(object);
    return Get(compartment);
  }

  bool CanShareCompartmentWith(nsIPrincipal* principal) {
    // Only share if we're same-origin with the principal.
    if (!originInfo.IsSameOrigin(principal)) {
      return false;
    }

    // Don't share if we have any weird state set.
    return !wantXrays && !isWebExtensionContentScript &&
           !isUAWidgetCompartment && mScope->XBLScopeStateMatches(principal);
  }

  CompartmentOriginInfo originInfo;

  // Controls whether this compartment gets Xrays to same-origin. This behavior
  // is deprecated, but is still the default for sandboxes for compatibity
  // reasons.
  bool wantXrays;

  // Controls whether this compartment is allowed to waive Xrays to content
  // that it subsumes. This should generally be true, except in cases where we
  // want to prevent code from depending on Xray Waivers (which might make it
  // more portable to other browser architectures).
  bool allowWaivers;

  // This compartment corresponds to a WebExtension content script, and
  // receives various bits of special compatibility behavior.
  bool isWebExtensionContentScript;

  // True if this compartment is a UA widget compartment.
  bool isUAWidgetCompartment;

  // See CompartmentHasExclusiveExpandos.
  bool hasExclusiveExpandos;

  // Whether SystemIsBeingShutDown has been called on this compartment.
  bool wasShutdown;

  JSObject2WrappedJSMap* GetWrappedJSMap() const { return mWrappedJSMap.get(); }
  void UpdateWeakPointersAfterGC(JSTracer* trc);

  void SystemIsBeingShutDown();

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  struct MapEntryGCPolicy {
    static bool traceWeak(JSTracer* trc, const void* /* unused */,
                          JS::Heap<JSObject*>* value) {
      return JS::GCPolicy<JS::Heap<JSObject*>>::traceWeak(trc, value);
    }
  };

  typedef JS::GCHashMap<const void*, JS::Heap<JSObject*>,
                        mozilla::PointerHasher<const void*>,
                        js::SystemAllocPolicy, MapEntryGCPolicy>
      RemoteProxyMap;
  RemoteProxyMap& GetRemoteProxyMap() { return mRemoteProxies; }

  XPCWrappedNativeScope* GetScope() { return mScope.get(); }

 private:
  mozilla::UniquePtr<JSObject2WrappedJSMap> mWrappedJSMap;

  // Cache holding proxy objects for Window objects (and their Location object)
  // that are loaded in a different process.
  RemoteProxyMap mRemoteProxies;

  // Our XPCWrappedNativeScope.
  mozilla::UniquePtr<XPCWrappedNativeScope> mScope;
};

inline void CrashIfNotInAutomation() { MOZ_RELEASE_ASSERT(IsInAutomation()); }

// XPConnect-specific data associated with each JavaScript realm. Per-Window
// settings live here; security-wrapper-related settings live in the
// CompartmentPrivate.
//
// Following the ECMAScript spec, a realm contains a global (e.g. an inner
// Window) and its associated scripts and objects; a compartment may contain
// several same-origin realms.
class RealmPrivate {
  RealmPrivate() = delete;
  RealmPrivate(const RealmPrivate&) = delete;

 public:
  enum LocationHint { LocationHintRegular, LocationHintAddon };

  explicit RealmPrivate(JS::Realm* realm);

  // Creates the RealmPrivate and CompartmentPrivate (if needed) for a new
  // global.
  static void Init(JS::HandleObject aGlobal,
                   const mozilla::SiteIdentifier& aSite);

  static RealmPrivate* Get(JS::Realm* realm) {
    MOZ_ASSERT(realm);
    void* priv = JS::GetRealmPrivate(realm);
    return static_cast<RealmPrivate*>(priv);
  }

  // Get the RealmPrivate for a given object.  `object` must not be a
  // cross-compartment wrapper, as CCWs aren't dedicated to a particular
  // realm.
  static RealmPrivate* Get(JSObject* object) {
    JS::Realm* realm = JS::GetObjectRealmOrNull(object);
    return Get(realm);
  }

  // The scriptability of this realm.
  Scriptability scriptability;

  // Whether we've emitted a warning about a property that was filtered out
  // by a security wrapper. See XrayWrapper.cpp.
  bool wrapperDenialWarnings[WrapperDenialTypeCount];

  const nsACString& GetLocation() {
    if (location.IsEmpty() && locationURI) {
      nsCOMPtr<nsIXPConnectWrappedJS> jsLocationURI =
          do_QueryInterface(locationURI);
      if (jsLocationURI) {
        // We cannot call into JS-implemented nsIURI objects, because
        // we are iterating over the JS heap at this point.
        location = "<JS-implemented nsIURI location>"_ns;
      } else if (NS_FAILED(locationURI->GetSpec(location))) {
        location = "<unknown location>"_ns;
      }
    }
    return location;
  }
  bool GetLocationURI(LocationHint aLocationHint, nsIURI** aURI) {
    if (locationURI) {
      nsCOMPtr<nsIURI> rval = locationURI;
      rval.forget(aURI);
      return true;
    }
    return TryParseLocationURI(aLocationHint, aURI);
  }
  bool GetLocationURI(nsIURI** aURI) {
    return GetLocationURI(LocationHintRegular, aURI);
  }

  void SetLocation(const nsACString& aLocation) {
    if (aLocation.IsEmpty()) {
      return;
    }
    if (!location.IsEmpty() || locationURI) {
      return;
    }
    location = aLocation;
  }
  void SetLocationURI(nsIURI* aLocationURI) {
    if (!aLocationURI) {
      return;
    }
    if (locationURI) {
      return;
    }
    locationURI = aLocationURI;
  }

  // JSStackFrames are tracked on a per-realm basis so they
  // can be cleared when the associated window goes away.
  void RegisterStackFrame(JSStackFrameBase* aFrame);
  void UnregisterStackFrame(JSStackFrameBase* aFrame);
  void NukeJSStackFrames();

 private:
  nsCString location;
  nsCOMPtr<nsIURI> locationURI;

  bool TryParseLocationURI(LocationHint aType, nsIURI** aURI);

  nsTHashtable<nsPtrHashKey<JSStackFrameBase>> mJSStackFrames;
};

inline XPCWrappedNativeScope* ObjectScope(JSObject* obj) {
  return CompartmentPrivate::Get(obj)->GetScope();
}

JSObject* NewOutObject(JSContext* cx);
bool IsOutObject(JSContext* cx, JSObject* obj);

nsresult HasInstance(JSContext* cx, JS::HandleObject objArg, const nsID* iid,
                     bool* bp);

// Returns the principal associated with |obj|'s realm. The object must not be a
// cross-compartment wrapper.
nsIPrincipal* GetObjectPrincipal(JSObject* obj);

// Attempt to clean up the passed in value pointer. The pointer `value` must be
// a pointer to a value described by the type `nsXPTType`.
//
// This method expects a value of the following types:
//   TD_NSIDPTR
//     value : nsID* (free)
//   TD_ASTRING, TD_CSTRING, TD_UTF8STRING
//     value : ns[C]String* (truncate)
//   TD_PSTRING, TD_PWSTRING, TD_PSTRING_SIZE_IS, TD_PWSTRING_SIZE_IS
//     value : char[16_t]** (free)
//   TD_INTERFACE_TYPE, TD_INTERFACE_IS_TYPE
//     value : nsISupports** (release)
//   TD_LEGACY_ARRAY (NOTE: aArrayLen should be passed)
//     value : void** (destroy elements & free)
//   TD_ARRAY
//     value : nsTArray<T>* (destroy elements & Clear)
//   TD_DOMOBJECT
//     value : T** (cleanup)
//   TD_PROMISE
//     value : dom::Promise** (release)
//
// Other types are ignored.
inline void CleanupValue(const nsXPTType& aType, void* aValue,
                         uint32_t aArrayLen = 0);

// Out-of-line internals for xpc::CleanupValue. Defined in XPCConvert.cpp.
void InnerCleanupValue(const nsXPTType& aType, void* aValue,
                       uint32_t aArrayLen);

// In order to be able to safely call CleanupValue on a generated value, the
// data behind it needs to be initialized to a safe value. This method handles
// initializing the backing data to a safe value to use as an argument to
// XPCConvert methods, or xpc::CleanupValue.
//
// The pointer `aValue` must point to a block of memory at least aType.Stride()
// bytes large, and correctly aligned.
//
// This method accepts the same types as xpc::CleanupValue.
void InitializeValue(const nsXPTType& aType, void* aValue);

// If a value was initialized with InitializeValue, it should be destroyed with
// DestructValue. This method acts like CleanupValue, except that destructors
// for complex types are also invoked, leaving them in an invalid state.
//
// This method should be called when destroying types initialized with
// InitializeValue.
//
// The pointer 'aValue' must point to a valid value of type 'aType'.
void DestructValue(const nsXPTType& aType, void* aValue,
                   uint32_t aArrayLen = 0);

bool SandboxCreateCrypto(JSContext* cx, JS::Handle<JSObject*> obj);
bool SandboxCreateFetch(JSContext* cx, JS::Handle<JSObject*> obj);
bool SandboxCreateStructuredClone(JSContext* cx, JS::Handle<JSObject*> obj);
bool SandboxCreateLocks(JSContext* cx, JS::Handle<JSObject*> obj);

}  // namespace xpc

namespace mozilla {
namespace dom {
extern bool DefineStaticJSVals(JSContext* cx);
}  // namespace dom
}  // namespace mozilla

bool xpc_LocalizeRuntime(JSRuntime* rt);
void xpc_DelocalizeRuntime(JSRuntime* rt);

/***************************************************************************/
// Inlines use the above - include last.

#include "XPCInlines.h"

/***************************************************************************/

#endif /* xpcprivate_h___ */
