/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIGlobalObject_h__
#define nsIGlobalObject_h__

#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientState.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/OriginTrials.h"
#include "nsContentUtils.h"
#include "nsHashKeys.h"
#include "nsISupports.h"
#include "nsRFPService.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "js/TypeDecls.h"

// Must be kept in sync with xpcom/rust/xpcom/src/interfaces/nonidl.rs
#define NS_IGLOBALOBJECT_IID \
  {0x11afa8be, 0xd997, 0x4e07, {0xa6, 0xa3, 0x6f, 0x87, 0x2e, 0xc3, 0xee, 0x7f}}

class nsCycleCollectionTraversalCallback;
class nsICookieJarSettings;
class nsIPrincipal;
class nsIURI;
class nsPIDOMWindowInner;

namespace mozilla {
class DOMEventTargetHelper;
class GlobalFreezeObserver;
class GlobalTeardownObserver;
template <typename V, typename E>
class Result;
enum class StorageAccess;
namespace dom {
class WorkerGlobalScopeBase;
class VoidFunction;
class DebuggerNotificationManager;
class FontFaceSet;
class Function;
class Report;
class ReportBody;
class ReportingObserver;
class ServiceWorker;
class ServiceWorkerContainer;
class ServiceWorkerRegistration;
class ServiceWorkerRegistrationDescriptor;
class StorageManager;
class WebTaskSchedulingState;
enum class CallerType : uint32_t;
}  // namespace dom
namespace ipc {
class PrincipalInfo;
}  // namespace ipc
}  // namespace mozilla

namespace JS::loader {
class ModuleLoaderBase;
}  // namespace JS::loader

/**
 * See <https://developer.mozilla.org/en-US/docs/Glossary/Global_object>.
 */
class nsIGlobalObject : public nsISupports {
 private:
  nsTArray<nsCString> mHostObjectURIs;

  // Raw pointers to bound DETH objects.  These are added by
  // AddGlobalTeardownObserver().
  mozilla::LinkedList<mozilla::GlobalTeardownObserver> mGlobalTeardownObservers;
  // And by AddGlobalFreezeObserver()
  mozilla::LinkedList<mozilla::GlobalFreezeObserver> mGlobalFreezeObservers;

  bool mIsDying;

 protected:
  bool mIsInnerWindow;

  nsIGlobalObject();

 public:
  using RTPCallerType = mozilla::RTPCallerType;
  using RFPTarget = mozilla::RFPTarget;
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_IGLOBALOBJECT_IID)

  /**
   * This check is added to deal with Promise microtask queues. On the main
   * thread, we do not impose restrictions about when script stops running or
   * when runnables can no longer be dispatched to the main thread. This means
   * it is possible for a Promise chain to keep resolving an infinite chain of
   * promises, preventing the browser from shutting down. See Bug 1058695. To
   * prevent this, the nsGlobalWindow subclass sets this flag when it is
   * closed. The Promise implementation checks this and prohibits new runnables
   * from being dispatched.
   *
   * We pair this with checks during processing the promise microtask queue
   * that pops up the slow script dialog when the Promise queue is preventing
   * a window from going away.
   */
  bool IsDying() const { return mIsDying; }

  /**
   * Is it currently forbidden to call into script?  JS-implemented WebIDL is
   * a special case that's always allowed because it has the system principal,
   * and callers should indicate this.
   */
  bool IsScriptForbidden(JSObject* aCallback,
                         bool aIsJSImplementedWebIDL = false) const;

  /**
   * Return the JSObject for this global, if it still has one.  Otherwise return
   * null.
   *
   * If non-null is returned, then the returned object will have been already
   * exposed to active JS, so callers do not need to do it.
   */
  virtual JSObject* GetGlobalJSObject() = 0;

  /**
   * Return the JSObject for this global _without_ exposing it to active JS.
   * This may return a gray object.
   *
   * This method is appropriate to use in assertions (so there is less of a
   * difference in GC/CC marking between debug and optimized builds) and in
   * situations where we are sure no CC activity can happen while the return
   * value is used and the return value does not end up escaping to the heap in
   * any way.  In all other cases, and in particular in cases where the return
   * value is held in a JS::Rooted or passed to the JSAutoRealm constructor, use
   * GetGlobalJSObject.
   */
  virtual JSObject* GetGlobalJSObjectPreserveColor() const = 0;

  /**
   * Check whether this nsIGlobalObject still has a JSObject associated with it,
   * or whether it's torn-down enough that the JSObject is gone.
   */
  bool HasJSGlobal() const { return GetGlobalJSObjectPreserveColor(); }

  virtual nsISerialEventTarget* SerialEventTarget() const = 0;
  virtual nsresult Dispatch(already_AddRefed<nsIRunnable>&&) const = 0;

  // This method is not meant to be overridden.
  nsIPrincipal* PrincipalOrNull() const;

  void RegisterHostObjectURI(const nsACString& aURI);

  void UnregisterHostObjectURI(const nsACString& aURI);

  // Any CC class inheriting nsIGlobalObject should call these 2 methods to
  // cleanup objects stored in nsIGlobalObject such as blobURLs and Reports.
  void UnlinkObjectsInGlobal();
  void TraverseObjectsInGlobal(nsCycleCollectionTraversalCallback& aCb);

  // GlobalTeardownObservers must register themselves on the global when they
  // bind to it in order to get the DisconnectFromOwner() method
  // called correctly.  RemoveGlobalTeardownObserver() must be called
  // before the GlobalTeardownObserver is destroyed.
  void AddGlobalTeardownObserver(mozilla::GlobalTeardownObserver* aObject);
  void RemoveGlobalTeardownObserver(mozilla::GlobalTeardownObserver* aObject);

  // Iterate the registered GlobalTeardownObservers and call the given function
  // for each one.
  void ForEachGlobalTeardownObserver(
      const std::function<void(mozilla::GlobalTeardownObserver*,
                               bool* aDoneOut)>& aFunc) const;

  // GlobalFreezeObservers must register themselves on the global too for
  // FreezeCallback and optionally ThawCallback.
  void AddGlobalFreezeObserver(mozilla::GlobalFreezeObserver* aObserver);
  void RemoveGlobalFreezeObserver(mozilla::GlobalFreezeObserver* aObserver);

  // Iterate the registered GlobalFreezeObservers and call the given function
  // for each one.
  void ForEachGlobalFreezeObserver(
      const std::function<void(mozilla::GlobalFreezeObserver*, bool* aDoneOut)>&
          aFunc) const;

  virtual bool IsInSyncOperation() { return false; }

  virtual mozilla::dom::DebuggerNotificationManager*
  GetOrCreateDebuggerNotificationManager() {
    return nullptr;
  }

  virtual mozilla::dom::DebuggerNotificationManager*
  GetExistingDebuggerNotificationManager() {
    return nullptr;
  }

  virtual void SetWebTaskSchedulingState(
      mozilla::dom::WebTaskSchedulingState* aState) {}
  virtual mozilla::dom::WebTaskSchedulingState* GetWebTaskSchedulingState()
      const {
    return nullptr;
  }

  // For globals with a concept of a Base URI (windows, workers), the base URI,
  // nullptr otherwise.
  virtual nsIURI* GetBaseURI() const;

  virtual mozilla::Maybe<mozilla::dom::ClientInfo> GetClientInfo() const;
  virtual mozilla::Maybe<mozilla::dom::ClientState> GetClientState() const;

  virtual mozilla::Maybe<nsID> GetAgentClusterId() const;

  virtual bool CrossOriginIsolated() const { return false; }

  virtual bool IsSharedMemoryAllowed() const { return false; }

  virtual mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor> GetController()
      const;

  virtual already_AddRefed<mozilla::dom::ServiceWorkerContainer>
  GetServiceWorkerContainer();

  // Get the DOM object for the given descriptor or attempt to create one.
  // Creation can still fail and return nullptr during shutdown, etc.
  virtual RefPtr<mozilla::dom::ServiceWorker> GetOrCreateServiceWorker(
      const mozilla::dom::ServiceWorkerDescriptor& aDescriptor);

  // Get the DOM object for the given descriptor or return nullptr if it does
  // not exist.
  virtual RefPtr<mozilla::dom::ServiceWorkerRegistration>
  GetServiceWorkerRegistration(
      const mozilla::dom::ServiceWorkerRegistrationDescriptor& aDescriptor)
      const;

  // Get the DOM object for the given descriptor or attempt to create one.
  // Creation can still fail and return nullptr during shutdown, etc.
  virtual RefPtr<mozilla::dom::ServiceWorkerRegistration>
  GetOrCreateServiceWorkerRegistration(
      const mozilla::dom::ServiceWorkerRegistrationDescriptor& aDescriptor);

  /**
   * Returns the storage access of this global.
   *
   * If you have a global that needs storage access, you should be overriding
   * this method in your subclass of this class!
   */
  virtual mozilla::StorageAccess GetStorageAccess();

  // For globals with cookie jars (windows, workers), the cookie jar settings;
  // will likely be null on other global types.
  virtual nsICookieJarSettings* GetCookieJarSettings();

  // Returns the set of active origin trials for this global.
  virtual mozilla::OriginTrials Trials() const = 0;

  // Returns a pointer to this object as an inner window if this is one or
  // nullptr otherwise.
  nsPIDOMWindowInner* GetAsInnerWindow();

  virtual void TriggerUpdateCCFlag() {}

  void QueueMicrotask(mozilla::dom::VoidFunction& aCallback);

  void RegisterReportingObserver(mozilla::dom::ReportingObserver* aObserver,
                                 bool aBuffered);

  void UnregisterReportingObserver(mozilla::dom::ReportingObserver* aObserver);

  void BroadcastReport(mozilla::dom::Report* aReport);

  MOZ_CAN_RUN_SCRIPT void NotifyReportingObservers();

  void RemoveReportRecords();

  // https://streams.spec.whatwg.org/#count-queuing-strategy-size-function
  // This function is set once by CountQueuingStrategy::GetSize.
  already_AddRefed<mozilla::dom::Function>
  GetCountQueuingStrategySizeFunction();
  void SetCountQueuingStrategySizeFunction(mozilla::dom::Function* aFunction);

  already_AddRefed<mozilla::dom::Function>
  GetByteLengthQueuingStrategySizeFunction();
  void SetByteLengthQueuingStrategySizeFunction(
      mozilla::dom::Function* aFunction);

  /**
   * Check whether we should avoid leaking distinguishing information to JS/CSS.
   * https://w3c.github.io/fingerprinting-guidance/
   */
  virtual bool ShouldResistFingerprinting(RFPTarget aTarget) const = 0;

  // CallerType::System callers never have to resist fingerprinting.
  bool ShouldResistFingerprinting(mozilla::dom::CallerType aCallerType,
                                  RFPTarget aTarget) const;

  RTPCallerType GetRTPCallerType() const;

  /**
   * Get the module loader to use for this global, if any. By default this
   * returns null.
   */
  virtual JS::loader::ModuleLoaderBase* GetModuleLoader(JSContext* aCx) {
    return nullptr;
  }

  virtual mozilla::dom::FontFaceSet* GetFonts() { return nullptr; }

  virtual mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult>
  GetStorageKey();
  mozilla::Result<bool, nsresult> HasEqualStorageKey(
      const mozilla::ipc::PrincipalInfo& aStorageKey);

  virtual mozilla::dom::StorageManager* GetStorageManager() { return nullptr; }

  /**
   * https://html.spec.whatwg.org/multipage/web-messaging.html#eligible-for-messaging
   * * a Window object whose associated Document is fully active, or
   * * a WorkerGlobalScope object whose closing flag is false and whose worker
   *   is not a suspendable worker.
   */
  virtual bool IsEligibleForMessaging() { return false; };
  virtual bool IsBackgroundInternal() const { return false; }
  virtual mozilla::dom::TimeoutManager* GetTimeoutManager() { return nullptr; }
  virtual bool IsRunningTimeout() { return false; }
  virtual bool IsPlayingAudio() { return false; }
  // Determine if the window is suspended or frozen.  Outer windows
  // will forward this call to the inner window for convenience.  If
  // there is no inner window then the outer window is considered
  // suspended and frozen by default.
  virtual bool IsSuspended() const { return false; }
  virtual bool IsFrozen() const { return false; }
  MOZ_CAN_RUN_SCRIPT
  virtual bool RunTimeoutHandler(mozilla::dom::Timeout* aTimeout) {
    return false;
  }
  // Return true if there is any active IndexedDB databases which could block
  // timeout-throttling.
  virtual bool HasActiveIndexedDBDatabases() const { return false; }
  /**
   * Check whether the active peer connection count is non-zero.
   */
  virtual bool HasActivePeerConnections() { return false; }
  // Return true if there are any open WebSockets that could block
  // timeout-throttling.
  virtual bool HasOpenWebSockets() const { return false; }

  virtual bool IsXPCSandbox() { return false; }

  virtual bool HasScheduledNormalOrHighPriorityWebTasks() const {
    return false;
  }

  virtual void UpdateWebSocketCount(int32_t aDelta) {};
  // Increase/Decrease the number of active IndexedDB databases for the
  // decision making of timeout-throttling.
  virtual void UpdateActiveIndexedDBDatabaseCount(int32_t aDelta) {}

  /**
   * Report a localized error message to the error console.  Currently this
   * amounts to a wrapper around nsContentUtils::ReportToConsole for window
   * globals and a runnable bounced to the main thread to call
   * nsContentUtils::ReportToConsole for workers but the intent is to migrate
   * towards logging the messages to the `dom::Console` for the global.  See
   * bug 1900706 for more context.
   *
   * This method returns void because there is no reasonable action for a caller
   * for dynamic failure and we can assert on things like erroneous message
   * names.
   *
   *   @param aErrorFlags See nsIScriptError.
   *   @param aCategory Name of module reporting error.
   *   @param aFile Properties file containing localized message.
   *   @param aMessageName Name of localized message.
   *   @param [aParams=empty-array] (Optional) Parameters to be substituted into
   *          localized message.
   *   @param [aURI=nullptr] (Optional) URI of resource containing error; if
   *          omitted, an attempt will be made to use the URI associated with
   *          the global (ex: the document URI).
   *   @param [aSourceLine=u""_ns] (Optional) The text of the line that
   *          contains the error (may be empty).
   *   @param [aLineNumber=0] (Optional) Line number within resource
   *          containing error.
   *   @param [aColumnNumber=0] (Optional) Column number within resource
   *          containing error.
   */
  virtual void ReportToConsole(
      uint32_t aErrorFlags, const nsCString& aCategory,
      nsContentUtils::PropertiesFile aFile, const nsCString& aMessageName,
      const nsTArray<nsString>& aParams = nsTArray<nsString>(),
      const mozilla::SourceLocation& aLocation =
          mozilla::JSCallingLocation::Get());

 protected:
  virtual ~nsIGlobalObject();

  void StartDying() { mIsDying = true; }

  void DisconnectGlobalTeardownObservers();
  void DisconnectGlobalFreezeObservers();
  void NotifyGlobalFrozen();
  void NotifyGlobalThawed();

  size_t ShallowSizeOfExcludingThis(mozilla::MallocSizeOf aSizeOf) const;

 private:
  // List of Report objects for ReportingObservers.
  nsTArray<RefPtr<mozilla::dom::ReportingObserver>> mReportingObservers;
  nsTArray<RefPtr<mozilla::dom::Report>> mReportRecords;

  // https://streams.spec.whatwg.org/#count-queuing-strategy-size-function
  RefPtr<mozilla::dom::Function> mCountQueuingStrategySizeFunction;

  // https://streams.spec.whatwg.org/#byte-length-queuing-strategy-size-function
  RefPtr<mozilla::dom::Function> mByteLengthQueuingStrategySizeFunction;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIGlobalObject, NS_IGLOBALOBJECT_IID)

#endif  // nsIGlobalObject_h__
