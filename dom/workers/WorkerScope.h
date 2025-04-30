/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workerscope_h__
#define mozilla_dom_workerscope_h__

#include "js/TypeDecls.h"
#include "js/loader/ModuleLoaderBase.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Maybe.h"
#include "mozilla/NotNull.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/AnimationFrameProvider.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageBitmapSource.h"
#include "mozilla/dom/PerformanceWorker.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/TrustedTypePolicyFactory.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/TimeoutManager.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIGlobalObject.h"
#include "nsISupports.h"
#include "nsWeakReference.h"

#ifdef XP_WIN
#  undef PostMessage
#endif

class nsAtom;
class nsISerialEventTarget;

namespace mozilla {
class ErrorResult;
struct VsyncEvent;

namespace extensions {

class ExtensionBrowser;

}  // namespace extensions

namespace dom {

class AnyCallback;
enum class CallerType : uint32_t;
class ClientInfo;
class ClientSource;
class Clients;
class Console;
class CookieStore;
class Crypto;
class DOMString;
class DebuggerNotificationManager;
enum class EventCallbackDebuggerNotificationType : uint8_t;
class EventHandlerNonNull;
class FontFaceSet;
class Function;
class FunctionOrTrustedScriptOrString;
class IDBFactory;
class OnErrorEventHandlerNonNull;
template <typename T>
class Optional;
class OwningTrustedScriptURLOrString;
class Performance;
class Promise;
class RequestOrUTF8String;
template <typename T>
class Sequence;
class ServiceWorkerDescriptor;
class ServiceWorkerRegistration;
class ServiceWorkerRegistrationDescriptor;
struct StructuredSerializeOptions;
class WorkerDocumentListener;
class WorkerLocation;
class WorkerNavigator;
class WorkerPrivate;
class VsyncWorkerChild;
class WebTaskScheduler;
class WebTaskSchedulerWorker;
class WebTaskSchedulingState;
struct RequestInit;

namespace cache {

class CacheStorage;

}  // namespace cache

class WorkerGlobalScopeBase : public DOMEventTargetHelper,
                              public nsSupportsWeakReference,
                              public nsIGlobalObject {
  friend class WorkerPrivate;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(WorkerGlobalScopeBase,
                                                         DOMEventTargetHelper)

  WorkerGlobalScopeBase(WorkerPrivate* aWorkerPrivate,
                        UniquePtr<ClientSource> aClientSource);

  mozilla::dom::TimeoutManager* GetTimeoutManager() override final {
    return mTimeoutManager.get();
  }

  virtual bool WrapGlobalObject(JSContext* aCx,
                                JS::MutableHandle<JSObject*> aReflector) = 0;

  // EventTarget implementation
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) final {
    MOZ_CRASH("WrapObject not supported; use WrapGlobalObject.");
  }

  // nsIGlobalObject implementation
  JSObject* GetGlobalJSObject() final;

  JSObject* GetGlobalJSObjectPreserveColor() const final;

  bool IsSharedMemoryAllowed() const final;

  bool ShouldResistFingerprinting(RFPTarget aTarget) const final;

  OriginTrials Trials() const final;

  StorageAccess GetStorageAccess() final;

  nsICookieJarSettings* GetCookieJarSettings() final;

  nsIURI* GetBaseURI() const final;

  Maybe<ClientInfo> GetClientInfo() const final;
  Maybe<ClientState> GetClientState() const final;

  Maybe<ServiceWorkerDescriptor> GetController() const final;

  mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult> GetStorageKey() final;

  virtual void Control(const ServiceWorkerDescriptor& aServiceWorker);

  // DispatcherTrait implementation
  nsresult Dispatch(already_AddRefed<nsIRunnable>&& aRunnable) const final;
  nsISerialEventTarget* SerialEventTarget() const final;

  MOZ_CAN_RUN_SCRIPT
  void ReportError(JSContext* aCx, JS::Handle<JS::Value> aError,
                   CallerType aCallerType, ErrorResult& aRv);

  // atob, btoa, and dump are declared (separately) by both WorkerGlobalScope
  // and WorkerDebuggerGlobalScope WebIDL interfaces
  void Atob(const nsAString& aAtob, nsAString& aOut, ErrorResult& aRv) const;

  void Btoa(const nsAString& aBtoa, nsAString& aOut, ErrorResult& aRv) const;

  already_AddRefed<Console> GetConsole(ErrorResult& aRv);

  Console* GetConsoleIfExists() const { return mConsole; }

  void InitModuleLoader(JS::loader::ModuleLoaderBase* aModuleLoader) {
    if (!mModuleLoader) {
      mModuleLoader = aModuleLoader;
    }
  }

  // The nullptr here is not used, but is required to make the override method
  // have the same signature as other GetModuleLoader methods on globals.
  JS::loader::ModuleLoaderBase* GetModuleLoader(
      JSContext* aCx = nullptr) override {
    return mModuleLoader;
  };

  uint64_t WindowID() const;

  // Usually global scope dies earlier than the WorkerPrivate, but if we see
  // it leak at least we can tell it to not carry away a dead pointer.
  void NoteWorkerTerminated() { mWorkerPrivate = nullptr; }

  ClientSource& MutableClientSourceRef() const { return *mClientSource; }

  bool IsBackgroundInternal() const override {
    MOZ_ASSERT(mWorkerPrivate);
    return mWorkerPrivate->IsRunningInBackground();
  }

  MOZ_CAN_RUN_SCRIPT bool RunTimeoutHandler(
      mozilla::dom::Timeout* aTimeout) override;
  bool IsFrozen() const override {
    return mWorkerPrivate->IsFrozenForWorkerThread();
  }

  // workers don't support both frozen and suspended,
  // either both are true, or both are false.
  bool IsSuspended() const override {
    return mWorkerPrivate->IsFrozenForWorkerThread();
  }

  void UpdateWebSocketCount(int32_t aDelta) override {
    if (aDelta == 0) {
      return;
    }

    MOZ_DIAGNOSTIC_ASSERT(
        aDelta > 0 || ((aDelta + mNumOfOpenWebSockets) < mNumOfOpenWebSockets));

    mNumOfOpenWebSockets += aDelta;
  }

  // Increase/Decrease the number of active IndexedDB databases for the
  // decision making of timeout-throttling.
  void UpdateActiveIndexedDBDatabaseCount(int32_t aDelta) override {
    AssertIsOnWorkerThread();
    mNumOfIndexedDBDatabases += aDelta;
  }

  bool HasOpenWebSockets() const override { return mNumOfOpenWebSockets; }

  bool HasActiveIndexedDBDatabases() const override {
    AssertIsOnWorkerThread();
    return mNumOfIndexedDBDatabases;
  }

  void TriggerUpdateCCFlag() override {
    mWorkerPrivate->UpdateCCFlag(WorkerPrivate::CCFlag::EligibleForTimeout);
  }

  static WorkerGlobalScopeBase* Cast(nsIGlobalObject* obj) {
    return static_cast<WorkerGlobalScopeBase*>(obj);
  }

 protected:
  ~WorkerGlobalScopeBase();

  CheckedUnsafePtr<WorkerPrivate> mWorkerPrivate;

  void AssertIsOnWorkerThread() const {
    MOZ_ASSERT(mWorkerThreadUsedOnlyForAssert == PR_GetCurrentThread());
  }

 private:
  RefPtr<Console> mConsole;
  RefPtr<JS::loader::ModuleLoaderBase> mModuleLoader;
  const UniquePtr<ClientSource> mClientSource;
  nsCOMPtr<nsISerialEventTarget> mSerialEventTarget;
#ifdef DEBUG
  PRThread* mWorkerThreadUsedOnlyForAssert;
#endif
  mozilla::UniquePtr<mozilla::dom::TimeoutManager> mTimeoutManager;
  uint32_t mNumOfOpenWebSockets{};
  uint32_t mNumOfIndexedDBDatabases{};
};

namespace workerinternals {

class NamedWorkerGlobalScopeMixin {
 public:
  explicit NamedWorkerGlobalScopeMixin(const nsAString& aName) : mName(aName) {}

  void GetName(DOMString& aName) const;

 protected:
  ~NamedWorkerGlobalScopeMixin() = default;

 private:
  const nsString mName;
};

}  // namespace workerinternals

class WorkerGlobalScope : public WorkerGlobalScopeBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(WorkerGlobalScope,
                                           WorkerGlobalScopeBase)

  using WorkerGlobalScopeBase::WorkerGlobalScopeBase;

  void NoteTerminating();

  void NoteShuttingDown();

  // nsIGlobalObject implementation
  already_AddRefed<ServiceWorkerContainer> GetServiceWorkerContainer() final;

  RefPtr<ServiceWorker> GetOrCreateServiceWorker(
      const ServiceWorkerDescriptor& aDescriptor) final;

  RefPtr<ServiceWorkerRegistration> GetServiceWorkerRegistration(
      const ServiceWorkerRegistrationDescriptor& aDescriptor) const final;

  RefPtr<ServiceWorkerRegistration> GetOrCreateServiceWorkerRegistration(
      const ServiceWorkerRegistrationDescriptor& aDescriptor) final;

  DebuggerNotificationManager* GetOrCreateDebuggerNotificationManager() final;

  DebuggerNotificationManager* GetExistingDebuggerNotificationManager() final;

  Maybe<EventCallbackDebuggerNotificationType> GetDebuggerNotificationType()
      const final;

  mozilla::dom::StorageManager* GetStorageManager() final;

  void SetIsNotEligibleForMessaging() { mIsEligibleForMessaging = false; }

  bool IsEligibleForMessaging() final;

  void ReportToConsole(uint32_t aErrorFlags, const nsCString& aCategory,
                       nsContentUtils::PropertiesFile aFile,
                       const nsCString& aMessageName,
                       const nsTArray<nsString>& aParams,
                       const mozilla::SourceLocation& aLocation) final;

  // WorkerGlobalScope WebIDL implementation
  WorkerGlobalScope* Self() { return this; }

  already_AddRefed<WorkerLocation> Location();

  already_AddRefed<WorkerNavigator> Navigator();

  already_AddRefed<WorkerNavigator> GetExistingNavigator() const;

  FontFaceSet* GetFonts(ErrorResult&);
  FontFaceSet* GetFonts() final { return GetFonts(IgnoreErrors()); }

  MOZ_CAN_RUN_SCRIPT void ImportScripts(
      JSContext* aCx,
      const Sequence<OwningTrustedScriptURLOrString>& aScriptURLs,
      ErrorResult& aRv);

  OnErrorEventHandlerNonNull* GetOnerror();

  void SetOnerror(OnErrorEventHandlerNonNull* aHandler);

  IMPL_EVENT_HANDLER(languagechange)
  IMPL_EVENT_HANDLER(offline)
  IMPL_EVENT_HANDLER(online)
  IMPL_EVENT_HANDLER(rejectionhandled)
  IMPL_EVENT_HANDLER(unhandledrejection)

  void Dump(const Optional<nsAString>& aString) const;

  Performance* GetPerformance();

  Performance* GetPerformanceIfExists() const { return mPerformance; }

  static bool IsInAutomation(JSContext* aCx, JSObject*);

  void GetJSTestingFunctions(JSContext* aCx,
                             JS::MutableHandle<JSObject*> aFunctions,
                             ErrorResult& aRv);

  // GlobalCrypto WebIDL implementation
  Crypto* GetCrypto(ErrorResult& aError);

  // WindowOrWorkerGlobalScope WebIDL implementation
  void GetOrigin(nsAString& aOrigin) const;

  bool CrossOriginIsolated() const final;

  MOZ_CAN_RUN_SCRIPT
  int32_t SetTimeout(JSContext* aCx,
                     const FunctionOrTrustedScriptOrString& aHandler,
                     int32_t aTimeout, const Sequence<JS::Value>& aArguments,
                     ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT
  void ClearTimeout(int32_t aHandle);

  MOZ_CAN_RUN_SCRIPT
  int32_t SetInterval(JSContext* aCx,
                      const FunctionOrTrustedScriptOrString& aHandler,
                      int32_t aTimeout, const Sequence<JS::Value>& aArguments,
                      ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT
  void ClearInterval(int32_t aHandle);

  already_AddRefed<Promise> CreateImageBitmap(
      const ImageBitmapSource& aImage, const ImageBitmapOptions& aOptions,
      ErrorResult& aRv);

  already_AddRefed<Promise> CreateImageBitmap(
      const ImageBitmapSource& aImage, int32_t aSx, int32_t aSy, int32_t aSw,
      int32_t aSh, const ImageBitmapOptions& aOptions, ErrorResult& aRv);

  void StructuredClone(JSContext* aCx, JS::Handle<JS::Value> aValue,
                       const StructuredSerializeOptions& aOptions,
                       JS::MutableHandle<JS::Value> aRetval,
                       ErrorResult& aError);

  already_AddRefed<Promise> Fetch(const RequestOrUTF8String& aInput,
                                  const RequestInit& aInit,
                                  CallerType aCallerType, ErrorResult& aRv);

  bool IsSecureContext() const;

  already_AddRefed<IDBFactory> GetIndexedDB(JSContext* aCx,
                                            ErrorResult& aErrorResult);

  already_AddRefed<cache::CacheStorage> GetCaches(ErrorResult& aRv);

  WebTaskScheduler* Scheduler();
  WebTaskScheduler* GetExistingScheduler() const;
  void SetWebTaskSchedulingState(WebTaskSchedulingState* aState) override;
  bool HasScheduledNormalOrHighPriorityWebTasks() const override;

  WebTaskSchedulingState* GetWebTaskSchedulingState() const override {
    return mWebTaskSchedulingState;
  }

  bool WindowInteractionAllowed() const;

  void AllowWindowInteraction();

  void ConsumeWindowInteraction();

  void StorageAccessPermissionGranted();

  virtual void OnDocumentVisible(bool aVisible) {}

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual void OnVsync(const VsyncEvent& aVsync) {}

  TrustedTypePolicyFactory* TrustedTypes();

 protected:
  ~WorkerGlobalScope();

 private:
  MOZ_CAN_RUN_SCRIPT
  int32_t SetTimeoutOrInterval(JSContext* aCx,
                               const FunctionOrTrustedScriptOrString& aHandler,
                               int32_t aTimeout,
                               const Sequence<JS::Value>& aArguments,
                               bool aIsInterval, ErrorResult& aRv);

  RefPtr<Crypto> mCrypto;
  RefPtr<WorkerLocation> mLocation;
  RefPtr<WorkerNavigator> mNavigator;
  RefPtr<FontFaceSet> mFontFaceSet;
  RefPtr<Performance> mPerformance;
  RefPtr<IDBFactory> mIndexedDB;
  RefPtr<cache::CacheStorage> mCacheStorage;
  RefPtr<DebuggerNotificationManager> mDebuggerNotificationManager;
  RefPtr<WebTaskSchedulerWorker> mWebTaskScheduler;
  RefPtr<WebTaskSchedulingState> mWebTaskSchedulingState;
  RefPtr<TrustedTypePolicyFactory> mTrustedTypePolicyFactory;
  uint32_t mWindowInteractionsAllowed = 0;
  bool mIsEligibleForMessaging{true};
};

class DedicatedWorkerGlobalScope final
    : public WorkerGlobalScope,
      public workerinternals::NamedWorkerGlobalScopeMixin {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
      DedicatedWorkerGlobalScope, WorkerGlobalScope)

  DedicatedWorkerGlobalScope(WorkerPrivate* aWorkerPrivate,
                             UniquePtr<ClientSource> aClientSource,
                             const nsString& aName);

  bool WrapGlobalObject(JSContext* aCx,
                        JS::MutableHandle<JSObject*> aReflector) override;

  void PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                   const Sequence<JSObject*>& aTransferable, ErrorResult& aRv);

  void PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                   const StructuredSerializeOptions& aOptions,
                   ErrorResult& aRv);

  void Close();

  MOZ_CAN_RUN_SCRIPT
  uint32_t RequestAnimationFrame(FrameRequestCallback& aCallback,
                                 ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void CancelAnimationFrame(uint32_t aHandle, ErrorResult& aError);

  void OnDocumentVisible(bool aVisible) override;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void OnVsync(const VsyncEvent& aVsync) override;

  IMPL_EVENT_HANDLER(message)
  IMPL_EVENT_HANDLER(messageerror)
  IMPL_EVENT_HANDLER(rtctransform)

 private:
  ~DedicatedWorkerGlobalScope() = default;

  FrameRequestManager mFrameRequestManager;
  RefPtr<VsyncWorkerChild> mVsyncChild;
  RefPtr<WorkerDocumentListener> mDocListener;
  bool mDocumentVisible = false;
};

class SharedWorkerGlobalScope final
    : public WorkerGlobalScope,
      public workerinternals::NamedWorkerGlobalScopeMixin {
 public:
  SharedWorkerGlobalScope(WorkerPrivate* aWorkerPrivate,
                          UniquePtr<ClientSource> aClientSource,
                          const nsString& aName);

  bool WrapGlobalObject(JSContext* aCx,
                        JS::MutableHandle<JSObject*> aReflector) override;

  void Close();

  IMPL_EVENT_HANDLER(connect)

 private:
  ~SharedWorkerGlobalScope() = default;
};

class ServiceWorkerGlobalScope final : public WorkerGlobalScope {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ServiceWorkerGlobalScope,
                                           WorkerGlobalScope)

  ServiceWorkerGlobalScope(
      WorkerPrivate* aWorkerPrivate, UniquePtr<ClientSource> aClientSource,
      const ServiceWorkerRegistrationDescriptor& aRegistrationDescriptor);

  bool WrapGlobalObject(JSContext* aCx,
                        JS::MutableHandle<JSObject*> aReflector) override;

  already_AddRefed<Clients> GetClients();

  ServiceWorkerRegistration* Registration();

  already_AddRefed<Promise> SkipWaiting(ErrorResult& aRv);

  SafeRefPtr<extensions::ExtensionBrowser> AcquireExtensionBrowser();

  IMPL_EVENT_HANDLER(install)
  IMPL_EVENT_HANDLER(activate)

  EventHandlerNonNull* GetOnfetch();

  void SetOnfetch(EventHandlerNonNull* aCallback);

  void EventListenerAdded(nsAtom* aType) override;

  already_AddRefed<mozilla::dom::CookieStore> CookieStore();

  IMPL_EVENT_HANDLER(message)
  IMPL_EVENT_HANDLER(messageerror)

  IMPL_EVENT_HANDLER(notificationclick)
  IMPL_EVENT_HANDLER(notificationclose)

  IMPL_EVENT_HANDLER(push)
  IMPL_EVENT_HANDLER(pushsubscriptionchange)

  IMPL_EVENT_HANDLER(cookiechange)

 private:
  ~ServiceWorkerGlobalScope();

  void NoteFetchHandlerWasAdded() const;

  RefPtr<Clients> mClients;
  const nsString mScope;
  RefPtr<ServiceWorkerRegistration> mRegistration;
  SafeRefPtr<extensions::ExtensionBrowser> mExtensionBrowser;
  RefPtr<mozilla::dom::CookieStore> mCookieStore;
};

class WorkerDebuggerGlobalScope final : public WorkerGlobalScopeBase {
 public:
  using WorkerGlobalScopeBase::WorkerGlobalScopeBase;

  bool WrapGlobalObject(JSContext* aCx,
                        JS::MutableHandle<JSObject*> aReflector) override;

  void Control(const ServiceWorkerDescriptor& aServiceWorker) override {
    MOZ_CRASH("Can't control debugger workers.");
  }

  void GetGlobal(JSContext* aCx, JS::MutableHandle<JSObject*> aGlobal,
                 ErrorResult& aRv);

  void CreateSandbox(JSContext* aCx, const nsAString& aName,
                     JS::Handle<JSObject*> aPrototype,
                     JS::MutableHandle<JSObject*> aResult, ErrorResult& aRv);

  void LoadSubScript(JSContext* aCx, const nsAString& aUrl,
                     const Optional<JS::Handle<JSObject*>>& aSandbox,
                     ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void EnterEventLoop();

  void LeaveEventLoop();

  void PostMessage(const nsAString& aMessage);

  void SetImmediate(Function& aHandler, ErrorResult& aRv);

  void ReportError(JSContext* aCx, const nsAString& aMessage);

  void RetrieveConsoleEvents(JSContext* aCx, nsTArray<JS::Value>& aEvents,
                             ErrorResult& aRv);

  void ClearConsoleEvents(JSContext* aCx, ErrorResult& aRv);

  void SetConsoleEventHandler(JSContext* aCx, AnyCallback* aHandler,
                              ErrorResult& aRv);

  void Dump(JSContext* aCx, const Optional<nsAString>& aString) const;

  IMPL_EVENT_HANDLER(message)
  IMPL_EVENT_HANDLER(messageerror)

 private:
  ~WorkerDebuggerGlobalScope() = default;
};

}  // namespace dom
}  // namespace mozilla

inline nsISupports* ToSupports(mozilla::dom::WorkerGlobalScope* aScope) {
  return static_cast<mozilla::dom::EventTarget*>(aScope);
}

#endif /* mozilla_dom_workerscope_h__ */
