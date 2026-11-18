/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGlobalWindowInner_h_
#define nsGlobalWindowInner_h_

#include "Units.h"
#include "mozilla/Attributes.h"
#include "mozilla/CallState.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/FlushType.h"
#include "mozilla/LinkedList.h"
#include "mozilla/MozPromise.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/ChromeMessageBroadcaster.h"
#include "mozilla/dom/DebuggerNotificationManager.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageBitmapSource.h"
#include "mozilla/dom/Location.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/StorageEvent.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "nsCOMPtr.h"
#include "nsCheapSets.h"
#include "nsCycleCollectionParticipant.h"
#include "nsHashKeys.h"
#include "nsIInterfaceRequestor.h"
#include "nsIPrincipal.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsPIDOMWindow.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"
#include "nsWrapperCacheInlines.h"
#include "prclist.h"

class nsIArray;
class nsIBrowserDOMWindow;
class nsIBaseWindow;
class nsIContent;
class nsICookieJarSettings;
class nsIDocShellTreeOwner;
class nsIDOMWindowUtils;
class nsIControllers;
class nsIScriptContext;
class nsIScriptTimeoutHandler;
class nsIBrowserChild;
class nsIPrintSettings;
class nsITimeoutHandler;
class nsIWebBrowserChrome;
class nsIWebProgressListener;
class mozIDOMWindowProxy;

class nsScreen;
class nsHistory;
class nsGlobalWindowObserver;
class nsGlobalWindowOuter;
class nsDOMWindowUtils;
class nsIUserIdleService;
struct nsRect;
class nsWindowRoot;
class nsWindowSizes;

class IdleRequestExecutor;

class PromiseDocumentFlushedResolver;

namespace mozilla {
class AbstractThread;
class ErrorResult;
template <class T>
class OwningNonNull;
class ScrollContainerFrame;

namespace glean {
class Glean;
class GleanPings;
}  // namespace glean

namespace hal {
enum class ScreenOrientation : uint32_t;
}

namespace dom {
class BarProp;
class BrowsingContext;
struct ChannelPixelLayout;
class Credential;
class ClientSource;
class Console;
class CookieStore;
class Crypto;
class CustomElementRegistry;
class DataTransfer;
class DocGroup;
class DocumentPictureInPicture;
class External;
class FunctionOrTrustedScriptOrString;
class Gamepad;
class ContentMediaController;
enum class ImageBitmapFormat : uint8_t;
class IdleRequest;
class IdleRequestCallback;
class IdleRequestMap;
class IntlUtils;
class MediaQueryList;
class OwningExternalOrWindowProxy;
class Promise;
class PostMessageEvent;
class PushManager;
struct RequestInit;
class RequestOrUTF8String;
class SharedWorker;
class Selection;
struct SizeToContentConstraints;
class WebTaskScheduler;
class WebTaskSchedulerMainThread;
class WebTaskSchedulingState;
class SpeechSynthesis;
class Timeout;
class TrustedTypePolicyFactory;
class VisualViewport;
class VoidFunction;
class VRDisplay;
enum class VRDisplayEventReason : uint8_t;
class VREventObserver;
struct WindowPostMessageOptions;
class Worklet;
namespace cache {
class CacheStorage;
}  // namespace cache
class IDBFactory;
}  // namespace dom
}  // namespace mozilla

extern const JSClass OuterWindowProxyClass;

//*****************************************************************************
// nsGlobalWindowInner: Global Object for Scripting
//*****************************************************************************

// nsGlobalWindowInner inherits PRCList for maintaining a list of all inner
// windows still in memory for any given outer window. This list is needed to
// ensure that mOuterWindow doesn't end up dangling. The nature of PRCList means
// that the window itself is always in the list, and an outer window's list will
// also contain all inner window objects that are still in memory (and in
// reality all inner window object's lists also contain its outer and all other
// inner windows belonging to the same outer window, but that's an unimportant
// side effect of inheriting PRCList).

class nsGlobalWindowInner final : public mozilla::dom::EventTarget,
                                  public nsPIDOMWindowInner,
                                  private nsIDOMWindow,
                                  public nsIScriptGlobalObject,
                                  public nsIScriptObjectPrincipal,
                                  public nsSupportsWeakReference,
                                  public nsIInterfaceRequestor,
                                  public PRCListStr {
 public:
  using RemoteProxy = mozilla::dom::BrowsingContext;

  using TimeStamp = mozilla::TimeStamp;
  using TimeDuration = mozilla::TimeDuration;

  using InnerWindowByIdTable =
      nsTHashMap<nsUint64HashKey, nsGlobalWindowInner*>;

  static void AssertIsOnMainThread()
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  bool IsInnerWindow() const final { return true; }  // Overriding EventTarget

  static nsGlobalWindowInner* Cast(nsPIDOMWindowInner* aPIWin) {
    return static_cast<nsGlobalWindowInner*>(aPIWin);
  }
  static const nsGlobalWindowInner* Cast(const nsPIDOMWindowInner* aPIWin) {
    return static_cast<const nsGlobalWindowInner*>(aPIWin);
  }
  static nsGlobalWindowInner* Cast(mozIDOMWindow* aWin) {
    return Cast(nsPIDOMWindowInner::From(aWin));
  }

  static nsGlobalWindowInner* GetInnerWindowWithId(uint64_t aInnerWindowID) {
    AssertIsOnMainThread();

    if (!sInnerWindowsById) {
      return nullptr;
    }

    nsGlobalWindowInner* innerWindow = sInnerWindowsById->Get(aInnerWindowID);
    return innerWindow;
  }

  static InnerWindowByIdTable* GetWindowsTable() {
    AssertIsOnMainThread();

    return sInnerWindowsById;
  }

  static nsGlobalWindowInner* FromSupports(nsISupports* supports) {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsGlobalWindowInner*)(mozilla::dom::EventTarget*)supports;
  }

  static already_AddRefed<nsGlobalWindowInner> Create(
      nsGlobalWindowOuter* aOuter, bool aIsChrome,
      mozilla::dom::WindowGlobalChild* aActor);

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHOD_(void) DeleteCycleCollectable() override;

  // nsWrapperCache
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> aGivenProto) override {
    return GetWrapper();
  }

  // nsIGlobalObject
  bool ShouldResistFingerprinting(RFPTarget aTarget) const final;
  mozilla::OriginTrials Trials() const final;
  mozilla::dom::FontFaceSet* GetFonts() final;

  JSObject* GetGlobalJSObject() final { return GetWrapper(); }
  JSObject* GetGlobalJSObjectPreserveColor() const final {
    return GetWrapperPreserveColor();
  }
  // The HasJSGlobal on nsIGlobalObject ends up having to do a virtual
  // call to GetGlobalJSObjectPreserveColor(), because when it's
  // making the call it doesn't know it's doing it on an
  // nsGlobalWindowInner.  Add a version here that can be entirely
  // non-virtual.
  bool HasJSGlobal() const { return GetGlobalJSObjectPreserveColor(); }

  mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult> GetStorageKey()
      override;

  mozilla::dom::StorageManager* GetStorageManager() override;

  bool IsEligibleForMessaging() override;

  void ReportToConsole(uint32_t aErrorFlags, const nsCString& aCategory,
                       PropertiesFile aFile, const nsCString& aMessageName,
                       const nsTArray<nsString>& aParams,
                       const mozilla::SourceLocation& aLocation) override;

  void TraceGlobalJSObject(JSTracer* aTrc);

  virtual nsresult EnsureScriptEnvironment() override;

  virtual nsIScriptContext* GetScriptContext() override;

  virtual bool IsBlackForCC(bool aTracingNeeded = true) override;

  // nsIScriptObjectPrincipal
  virtual nsIPrincipal* GetPrincipal() override;

  virtual nsIPrincipal* GetEffectiveCookiePrincipal() override;

  virtual nsIPrincipal* GetEffectiveStoragePrincipal() override;

  virtual nsIPrincipal* PartitionedPrincipal() override;

  mozilla::dom::TimeoutManager* GetTimeoutManager() override;

  bool IsRunningTimeout() override;

  // nsIDOMWindow
  NS_DECL_NSIDOMWINDOW

  void CaptureEvents();
  void ReleaseEvents();
  void Dump(const nsAString& aStr);
  void SetResizable(bool aResizable) const;

  virtual mozilla::EventListenerManager* GetExistingListenerManager()
      const override;

  virtual mozilla::EventListenerManager* GetOrCreateListenerManager() override;

  mozilla::Maybe<mozilla::dom::EventCallbackDebuggerNotificationType>
  GetDebuggerNotificationType() const override;

  bool ComputeDefaultWantsUntrusted(mozilla::ErrorResult& aRv) final;

  nsIGlobalObject* GetRelevantGlobal() const override;

  EventTarget* GetTargetForDOMEvent() override;

  using mozilla::dom::EventTarget::DispatchEvent;
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool DispatchEvent(
      mozilla::dom::Event& aEvent, mozilla::dom::CallerType aCallerType,
      mozilla::ErrorResult& aRv) override;

  void GetEventTargetParent(mozilla::EventChainPreVisitor& aVisitor) override;

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  PostHandleEvent(mozilla::EventChainPostVisitor& aVisitor) override;

  void Suspend(bool aIncludeSubWindows = true);
  void Resume(bool aIncludeSubWindows = true);
  virtual bool IsSuspended() const override;

  // Calling Freeze() on a window will automatically Suspend() it.  In
  // addition, the window and its children (if aIncludeSubWindows is true) are
  // further treated as no longer suitable for interaction with the user.  For
  // example, it may be marked non-visible, cannot be focused, etc.  All worker
  // threads are also frozen bringing them to a complete stop.  A window can
  // have Freeze() called multiple times and will only thaw after a matching
  // number of Thaw() calls.
  void Freeze(bool aIncludeSubWindows = true);
  void Thaw(bool aIncludeSubWindows = true);
  virtual bool IsFrozen() const override;
  virtual bool HasActiveIndexedDBDatabases() const override;
  virtual bool HasActivePeerConnections() override;
  virtual bool HasOpenWebSockets() const override;
  void AudioPlaybackChanged(bool aIsPlayingAudio);
  virtual bool HasScheduledNormalOrHighPriorityWebTasks() const override;
  void SyncStateFromParentWindow();
  virtual void UpdateWebSocketCount(int32_t aDelta) override;
  // Increase/Decrease the number of active IndexedDB databases for the
  // decision making of timeout-throttling.
  virtual void UpdateActiveIndexedDBDatabaseCount(int32_t aDelta) override;

  // Called on the current inner window of a browsing context when its
  // background state changes according to selected tab or visibility of the
  // browser window.  Used with Suspend()/Resume() or Freeze()/Thaw() because
  // background state may change while the inner window is not current.
  void UpdateBackgroundState();

  mozilla::dom::DebuggerNotificationManager*
  GetOrCreateDebuggerNotificationManager() override;

  mozilla::dom::DebuggerNotificationManager*
  GetExistingDebuggerNotificationManager() override;

  nsIURI* GetBaseURI() const final;

  mozilla::Maybe<mozilla::dom::ClientInfo> GetClientInfo() const override;
  mozilla::Maybe<mozilla::dom::ClientState> GetClientState() const final;
  mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor> GetController()
      const override;

  void SetPolicyContainer(nsIPolicyContainer* aPolicyContainer);
  nsIPolicyContainer* GetPolicyContainer();
  void SetPreloadCsp(nsIContentSecurityPolicy* aPreloadCsp);

  virtual already_AddRefed<mozilla::dom::ServiceWorkerContainer>
  GetServiceWorkerContainer() override;

  virtual RefPtr<mozilla::dom::ServiceWorker> GetOrCreateServiceWorker(
      const mozilla::dom::ServiceWorkerDescriptor& aDescriptor) override;

  RefPtr<mozilla::dom::ServiceWorkerRegistration> GetServiceWorkerRegistration(
      const mozilla::dom::ServiceWorkerRegistrationDescriptor& aDescriptor)
      const override;

  RefPtr<mozilla::dom::ServiceWorkerRegistration>
  GetOrCreateServiceWorkerRegistration(
      const mozilla::dom::ServiceWorkerRegistrationDescriptor& aDescriptor)
      override;

  mozilla::StorageAccess GetStorageAccess() final;

  nsICookieJarSettings* GetCookieJarSettings() final;

  void NoteCalledRegisterForServiceWorkerScope(const nsACString& aScope);

  void NoteDOMContentLoaded();

  MOZ_CAN_RUN_SCRIPT virtual nsresult FireDelayedDOMEvents(
      bool aIncludeSubWindows) override;

  virtual void MaybeUpdateTouchState() override;

  // Inner windows only.
  void RefreshRealmPrincipal();
  void RefreshReduceTimerPrecisionCallerType();

  // For accessing protected field mFullscreen
  friend class FullscreenTransitionTask;

  // Inner windows only.
  virtual void SetHasGamepadEventListener(bool aHasGamepad = true) override;
  void NotifyHasXRSession();
  bool HasUsedVR() const;
  bool IsVRContentDetected() const;
  bool IsVRContentPresenting() const;
  void RequestXRPermission();
  void OnXRPermissionRequestAllow();
  void OnXRPermissionRequestCancel();

  using EventTarget::EventListenerAdded;
  virtual void EventListenerAdded(nsAtom* aType) override;
  using EventTarget::EventListenerRemoved;
  virtual void EventListenerRemoved(nsAtom* aType) override;

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // WebIDL interface.
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> IndexedGetter(
      uint32_t aIndex);

  static bool IsPrivilegedChromeWindow(JSContext*, JSObject* aObj);

  static bool DeviceSensorsEnabled(JSContext*, JSObject*);

  bool DoResolve(
      JSContext* aCx, JS::Handle<JSObject*> aObj, JS::Handle<jsid> aId,
      JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> aDesc);
  // The return value is whether DoResolve might end up resolving the given id.
  // If in doubt, return true.
  static bool MayResolve(jsid aId);

  void GetOwnPropertyNames(JSContext* aCx, JS::MutableHandleVector<jsid> aNames,
                           bool aEnumerableOnly, mozilla::ErrorResult& aRv);

  nsPIDOMWindowOuter* GetInProcessScriptableTop() override;
  inline nsGlobalWindowOuter* GetInProcessTopInternal();

  inline nsGlobalWindowOuter* GetInProcessScriptableTopInternal();

  already_AddRefed<mozilla::dom::BrowsingContext> GetChildWindow(
      const nsAString& aName);

  inline nsIBrowserChild* GetBrowserChild() { return mBrowserChild.get(); }

  nsIScriptContext* GetContextInternal();

  nsGlobalWindowOuter* GetOuterWindowInternal() const;

  bool IsChromeWindow() const { return mIsChrome; }

  // GetScrollContainerFrame does not flush. Callers should do it themselves as
  // needed, depending on which info they actually want off the scroll container
  // frame.
  mozilla::ScrollContainerFrame* GetScrollContainerFrame();

  MOZ_CAN_RUN_SCRIPT nsresult Observe(nsISupports* aSubject, const char* aTopic,
                                      const char16_t* aData);

  void ObserveStorageNotification(mozilla::dom::StorageEvent* aEvent,
                                  const char16_t* aStorageType,
                                  bool aPrivateBrowsing);

  // Called by DocGroup when managing MediaSource URLs.
  void NoteMediaSourceURL(const nsACString& aURL);
  void UnnoteMediaSourceURL(const nsACString& aURL);

  static void Init();
  static void ShutDown();
  static bool IsCallerChrome();

  // Called on a trusted mouseup or dragend, and when a drag session ends, as
  // the mouse button is up even if no dragend was dispatched. Lifts the drag
  // service suppression that CanMoveResizeWindows added while it was down.
  static void MouseButtonReleased();

  friend class WindowStateHolder;

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(
      nsGlobalWindowInner, mozilla::dom::EventTarget)

#ifdef DEBUG
  // Call Unlink on this window. This may cause bad things to happen, so use
  // with caution.
  void RiskyUnlink();
#endif

  virtual bool TakeFocus(bool aFocus, uint32_t aFocusMethod) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void SetReadyForFocus() override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void PageHidden(
      bool aIsEnteringBFCache) override;
  virtual nsresult DispatchAsyncHashchange(nsIURI* aOldURI,
                                           nsIURI* aNewURI) override;
  virtual nsresult DispatchSyncPopState() override;

  // Inner windows only.
  virtual void EnableDeviceSensor(uint32_t aType) override;
  virtual void DisableDeviceSensor(uint32_t aType) override;

#if defined(MOZ_WIDGET_ANDROID)
  virtual void EnableOrientationChangeListener() override;
  virtual void DisableOrientationChangeListener() override;
#endif

  void AddSizeOfIncludingThis(nsWindowSizes& aWindowSizes) const;

  void CollectDOMSizesForDataDocuments(nsWindowSizes&) const;
  void RegisterDataDocumentForMemoryReporting(Document*);
  void UnregisterDataDocumentForMemoryReporting(Document*);

  enum SlowScriptResponse {
    ContinueSlowScript = 0,
    ContinueSlowScriptAndKeepNotifying,
    AlwaysContinueSlowScript,
    KillSlowScript,
    KillScriptGlobal
  };
  SlowScriptResponse ShowSlowScriptDialog(JSContext* aCx,
                                          const nsString& aAddonId,
                                          const double aDuration);

  // Inner windows only.
  void AddGamepad(mozilla::dom::GamepadHandle aHandle,
                  mozilla::dom::Gamepad* aGamepad);
  void RemoveGamepad(mozilla::dom::GamepadHandle aHandle);
  void GetGamepads(nsTArray<RefPtr<mozilla::dom::Gamepad>>& aGamepads);
  already_AddRefed<mozilla::dom::Promise> RequestAllGamepads(
      mozilla::ErrorResult& aRv);
  already_AddRefed<mozilla::dom::Gamepad> GetGamepad(
      mozilla::dom::GamepadHandle aHandle);
  void SetHasSeenGamepadInput(bool aHasSeen);
  bool HasSeenGamepadInput();
  void SyncGamepadState();
  void StopGamepadHaptics();

  // Inner windows only.
  // Enable/disable updates for gamepad input.
  void EnableGamepadUpdates();
  void DisableGamepadUpdates();

  // Inner windows only.
  // Enable/disable updates for VR
  void EnableVRUpdates();
  void DisableVRUpdates();

  void StartVRActivity();
  void StopVRActivity();

  // Update the VR displays for this window
  bool UpdateVRDisplays(nsTArray<RefPtr<mozilla::dom::VRDisplay>>& aDisplays);

  // Inner windows only.
  // Called to inform that the set of active VR displays has changed.
  void NotifyActiveVRDisplaysChanged();
  void NotifyDetectXRRuntimesCompleted();
  void NotifyPresentationGenerationChanged(uint32_t aDisplayID);

#define EVENT(name_, id_, type_, struct_)                              \
  mozilla::dom::EventHandlerNonNull* GetOn##name_() {                  \
    mozilla::EventListenerManager* elm = GetExistingListenerManager(); \
    return elm ? elm->GetEventHandler(nsGkAtoms::on##name_) : nullptr; \
  }                                                                    \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler) {      \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager(); \
    if (elm) {                                                         \
      elm->SetEventHandler(nsGkAtoms::on##name_, handler);             \
    }                                                                  \
  }
#define ERROR_EVENT(name_, id_, type_, struct_)                          \
  mozilla::dom::OnErrorEventHandlerNonNull* GetOn##name_() {             \
    mozilla::EventListenerManager* elm = GetExistingListenerManager();   \
    return elm ? elm->GetOnErrorEventHandler() : nullptr;                \
  }                                                                      \
  void SetOn##name_(mozilla::dom::OnErrorEventHandlerNonNull* handler) { \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager();   \
    if (elm) {                                                           \
      elm->SetEventHandler(handler);                                     \
    }                                                                    \
  }
#define BEFOREUNLOAD_EVENT(name_, id_, type_, struct_)                 \
  mozilla::dom::OnBeforeUnloadEventHandlerNonNull* GetOn##name_() {    \
    mozilla::EventListenerManager* elm = GetExistingListenerManager(); \
    return elm ? elm->GetOnBeforeUnloadEventHandler() : nullptr;       \
  }                                                                    \
  void SetOn##name_(                                                   \
      mozilla::dom::OnBeforeUnloadEventHandlerNonNull* handler) {      \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager(); \
    if (elm) {                                                         \
      elm->SetEventHandler(handler);                                   \
    }                                                                  \
  }
#define WINDOW_ONLY_EVENT EVENT
#define TOUCH_EVENT EVENT
#include "mozilla/EventNameList.inc"
#undef TOUCH_EVENT
#undef WINDOW_ONLY_EVENT
#undef BEFOREUNLOAD_EVENT
#undef ERROR_EVENT
#undef EVENT

  nsISupports* GetParentObject() { return nullptr; }

  static JSObject* CreateNamedPropertiesObject(JSContext* aCx,
                                               JS::Handle<JSObject*> aProto);

  mozilla::dom::WindowProxyHolder Window();
  mozilla::dom::WindowProxyHolder Self() { return Window(); }
  Document* GetDocument() { return GetDoc(); }
  void GetName(nsAString& aName, mozilla::ErrorResult& aError);
  void SetName(const nsAString& aName, mozilla::ErrorResult& aError);
  mozilla::dom::Location* Location() override;
  nsHistory* GetHistory(mozilla::ErrorResult& aError);
  mozilla::dom::CustomElementRegistry* CustomElements() override;
  mozilla::dom::CustomElementRegistry* GetExistingCustomElements();
  mozilla::dom::BarProp* GetLocationbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetMenubar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetPersonalbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetScrollbars(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetStatusbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetToolbar(mozilla::ErrorResult& aError);
  void GetStatus(nsAString& aStatus, mozilla::ErrorResult& aError);
  void SetStatus(const nsAString& aStatus, mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT void Close(mozilla::dom::CallerType aCallerType,
                                mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT nsresult Close() override;
  bool GetClosed(mozilla::ErrorResult& aError);
  void Stop(mozilla::ErrorResult& aError);
  void Focus(mozilla::dom::CallerType aCallerType,
             mozilla::ErrorResult& aError);
  nsresult Focus(mozilla::dom::CallerType aCallerType) override;
  void Blur(mozilla::dom::CallerType aCallerType, mozilla::ErrorResult& aError);
  mozilla::dom::WindowProxyHolder GetFrames(mozilla::ErrorResult& aError);
  uint32_t Length();
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> GetTop(
      mozilla::ErrorResult& aError);

 protected:
  explicit nsGlobalWindowInner(nsGlobalWindowOuter* aOuterWindow,
                               mozilla::dom::WindowGlobalChild* aActor);
  // Initializes the mWasOffline member variable
  void InitWasOffline();

 public:
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> GetOpenerWindow(
      mozilla::ErrorResult& aError);
  void GetOpener(JSContext* aCx, JS::MutableHandle<JS::Value> aRetval,
                 mozilla::ErrorResult& aError);
  void SetOpener(JSContext* aCx, JS::Handle<JS::Value> aOpener,
                 mozilla::ErrorResult& aError);
  void GetEvent(mozilla::dom::OwningEventOrUndefined& aRetval);
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> GetParent(
      mozilla::ErrorResult& aError);
  nsPIDOMWindowOuter* GetInProcessScriptableParent() override;
  mozilla::dom::Element* GetFrameElement(nsIPrincipal& aSubjectPrincipal,
                                         mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> Open(
      const nsAString& aUrl, const nsAString& aName, const nsAString& aOptions,
      mozilla::ErrorResult& aError);
  int16_t Orientation(mozilla::dom::CallerType aCallerType);

  already_AddRefed<mozilla::dom::Console> GetConsole(JSContext* aCx,
                                                     mozilla::ErrorResult& aRv);

  already_AddRefed<mozilla::dom::CookieStore> CookieStore();

  mozilla::dom::DocumentPictureInPicture* GetExtantDocumentPictureInPicture()
      const override {
    return mDocumentPiP;
  }

  mozilla::dom::DocumentPictureInPicture* DocumentPictureInPicture();
  mozilla::dom::PushManager* GetPushManager(JSContext* aCx,
                                            mozilla::ErrorResult& aRv);

  // https://w3c.github.io/webappsec-secure-contexts/#dom-window-issecurecontext
  bool IsSecureContext() const;

  mozilla::dom::External* External();

  mozilla::dom::Worklet* GetPaintWorklet(mozilla::ErrorResult& aRv);

  void GetWebExposedLocales(nsTArray<nsString>& aLocales);

  mozilla::dom::IntlUtils* GetIntlUtils(mozilla::ErrorResult& aRv);

  void StoreSharedWorker(mozilla::dom::SharedWorker* aSharedWorker);

  void ForgetSharedWorker(mozilla::dom::SharedWorker* aSharedWorker);

  void UpdateSharedWorkersLanguageOverride(const nsCString& aLanguageOverride);

  void UpdateSharedWorkerTimezoneOverride(const nsAString& aTimezoneOverride);

 public:
  void Alert(nsIPrincipal& aSubjectPrincipal, mozilla::ErrorResult& aError);
  void Alert(const nsAString& aMessage, nsIPrincipal& aSubjectPrincipal,
             mozilla::ErrorResult& aError);
  bool Confirm(const nsAString& aMessage, nsIPrincipal& aSubjectPrincipal,
               mozilla::ErrorResult& aError);
  void Prompt(const nsAString& aMessage, const nsAString& aInitial,
              nsAString& aReturn, nsIPrincipal& aSubjectPrincipal,
              mozilla::ErrorResult& aError);
  already_AddRefed<mozilla::dom::cache::CacheStorage> GetCaches(
      mozilla::ErrorResult& aRv);
  already_AddRefed<mozilla::dom::Promise> Fetch(
      const mozilla::dom::RequestOrUTF8String& aInput,
      const mozilla::dom::RequestInit& aInit,
      mozilla::dom::CallerType aCallerType, mozilla::ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void Print(mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder>
  PrintPreview(nsIPrintSettings*, nsIWebProgressListener*, nsIDocShell*,
               mozilla::ErrorResult&);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const nsAString& aTargetOrigin,
                      const mozilla::dom::Sequence<JSObject*>& aTransfer,
                      nsIPrincipal& aSubjectPrincipal,
                      mozilla::ErrorResult& aError);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const mozilla::dom::WindowPostMessageOptions& aOptions,
                      nsIPrincipal& aSubjectPrincipal,
                      mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  int32_t SetTimeout(
      JSContext* aCx,
      const mozilla::dom::FunctionOrTrustedScriptOrString& aHandler,
      int32_t aTimeout, const mozilla::dom::Sequence<JS::Value>& /* unused */,
      nsIPrincipal* aSubjectPrincipal, mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void ClearTimeout(int32_t aHandle);

  MOZ_CAN_RUN_SCRIPT
  int32_t SetInterval(
      JSContext* aCx,
      const mozilla::dom::FunctionOrTrustedScriptOrString& aHandler,
      const int32_t aTimeout,
      const mozilla::dom::Sequence<JS::Value>& /* unused */,
      nsIPrincipal* aSubjectPrincipal, mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void ClearInterval(int32_t aHandle);
  void GetOrigin(nsAString& aOrigin);

  MOZ_CAN_RUN_SCRIPT
  void ReportError(JSContext* aCx, JS::Handle<JS::Value> aError,
                   mozilla::dom::CallerType aCallerType,
                   mozilla::ErrorResult& aRv);

  void Atob(const nsAString& aAsciiBase64String, nsAString& aBinaryData,
            mozilla::ErrorResult& aError);
  void Btoa(const nsAString& aBinaryData, nsAString& aAsciiBase64String,
            mozilla::ErrorResult& aError);

  mozilla::dom::Storage* GetSessionStorage(mozilla::ErrorResult& aError);
  mozilla::dom::Storage* GetLocalStorage(mozilla::ErrorResult& aError);
  mozilla::dom::Selection* GetSelection(mozilla::ErrorResult& aError);
  mozilla::dom::IDBFactory* GetIndexedDB(JSContext* aCx,
                                         mozilla::ErrorResult& aError);
  already_AddRefed<nsDOMCSSDeclaration> GetComputedStyle(
      mozilla::dom::Element& aElt, const nsAString& aPseudoElt,
      mozilla::ErrorResult& aError) override;
  mozilla::dom::VisualViewport* VisualViewport();
  already_AddRefed<mozilla::dom::MediaQueryList> MatchMedia(
      const nsACString& aQuery, mozilla::dom::CallerType aCallerType,
      mozilla::ErrorResult& aError);
  nsScreen* Screen();
  bool HasScreen() const { return !!mScreen; }
  void MoveTo(int32_t aXPos, int32_t aYPos,
              mozilla::dom::CallerType aCallerType,
              mozilla::ErrorResult& aError);
  void MoveBy(int32_t aXDif, int32_t aYDif,
              mozilla::dom::CallerType aCallerType,
              mozilla::ErrorResult& aError);
  void ResizeTo(int32_t aWidth, int32_t aHeight,
                mozilla::dom::CallerType aCallerType,
                mozilla::ErrorResult& aError);
  void ResizeBy(int32_t aWidthDif, int32_t aHeightDif,
                mozilla::dom::CallerType aCallerType,
                mozilla::ErrorResult& aError);
  void MoveResize(int32_t aX, int32_t aY, int32_t aWidth, int32_t aHeight,
                  mozilla::ErrorResult& aError);
  void Scroll(double aXScroll, double aYScroll) {
    ScrollTo(aXScroll, aYScroll);
  }
  void Scroll(const mozilla::dom::ScrollToOptions& aOptions) {
    ScrollTo(aOptions);
  }
  void ScrollTo(double aXScroll, double aYScroll);
  void ScrollTo(const mozilla::dom::ScrollToOptions& aOptions);
  void ScrollBy(double aXScrollDif, double aYScrollDif);
  void ScrollBy(const mozilla::dom::ScrollToOptions& aOptions);
  void ScrollByLines(int32_t numLines,
                     const mozilla::dom::ScrollOptions& aOptions);
  void ScrollByPages(int32_t numPages,
                     const mozilla::dom::ScrollOptions& aOptions);
  void MozScrollSnap();
  double GetScrollX(mozilla::ErrorResult& aError);
  double GetPageXOffset(mozilla::ErrorResult& aError) {
    return GetScrollX(aError);
  }
  double GetScrollY(mozilla::ErrorResult& aError);
  double GetPageYOffset(mozilla::ErrorResult& aError) {
    return GetScrollY(aError);
  }

  int32_t GetScreenLeft(mozilla::dom::CallerType aCallerType,
                        mozilla::ErrorResult& aError) {
    return GetScreenX(aCallerType, aError);
  }

  double ScreenEdgeSlopX() const;
  double ScreenEdgeSlopY() const;

  int32_t GetScreenTop(mozilla::dom::CallerType aCallerType,
                       mozilla::ErrorResult& aError) {
    return GetScreenY(aCallerType, aError);
  }

  void GetScreenX(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                  mozilla::dom::CallerType aCallerType,
                  mozilla::ErrorResult& aError);
  void GetScreenY(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                  mozilla::dom::CallerType aCallerType,
                  mozilla::ErrorResult& aError);
  void GetOuterWidth(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                     mozilla::dom::CallerType aCallerType,
                     mozilla::ErrorResult& aError);
  void GetOuterHeight(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                      mozilla::dom::CallerType aCallerType,
                      mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  uint32_t RequestAnimationFrame(mozilla::dom::FrameRequestCallback& aCallback,
                                 mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void CancelAnimationFrame(uint32_t aHandle, mozilla::ErrorResult& aError);

  uint32_t RequestIdleCallback(JSContext* aCx,
                               mozilla::dom::IdleRequestCallback& aCallback,
                               const mozilla::dom::IdleRequestOptions& aOptions,
                               mozilla::ErrorResult& aError);
  void CancelIdleCallback(uint32_t aHandle);

#ifdef MOZ_WEBSPEECH
  mozilla::dom::SpeechSynthesis* GetSpeechSynthesis(
      mozilla::ErrorResult& aError);
  bool HasActiveSpeechSynthesis();
#endif

  mozilla::glean::Glean* Glean();
  mozilla::glean::GleanPings* GleanPings();

  already_AddRefed<nsDOMCSSDeclaration> GetDefaultComputedStyle(
      mozilla::dom::Element& aElt, const nsAString& aPseudoElt,
      mozilla::ErrorResult& aError);
  void SizeToContent(const mozilla::dom::SizeToContentConstraints&,
                     mozilla::ErrorResult&);
  mozilla::dom::Crypto* GetCrypto(mozilla::ErrorResult& aError);
  nsIControllers* GetControllers(mozilla::ErrorResult& aError);
  nsresult GetControllers(nsIControllers** aControllers) override;
  mozilla::dom::Element* GetRealFrameElement(mozilla::ErrorResult& aError);
  float GetMozInnerScreenX(mozilla::dom::CallerType aCallerType,
                           mozilla::ErrorResult& aError);
  float GetMozInnerScreenY(mozilla::dom::CallerType aCallerType,
                           mozilla::ErrorResult& aError);
  double GetDevicePixelRatio(mozilla::dom::CallerType aCallerType,
                             mozilla::ErrorResult& aError);
  double GetDesktopToDeviceScale(mozilla::ErrorResult& aError);
  int32_t GetScrollMinX(mozilla::ErrorResult& aError);
  int32_t GetScrollMinY(mozilla::ErrorResult& aError);
  int32_t GetScrollMaxX(mozilla::ErrorResult& aError);
  int32_t GetScrollMaxY(mozilla::ErrorResult& aError);
  bool GetFullScreen(mozilla::dom::CallerType aCallerType,
                     mozilla::ErrorResult& aError);
  bool GetFullScreen() override;
  MOZ_CAN_RUN_SCRIPT void SetFullScreen(bool aFullscreen,
                                        mozilla::dom::CallerType aCallerType,
                                        mozilla::ErrorResult& aError);
  bool Find(const nsAString& aString, bool aCaseSensitive, bool aBackwards,
            bool aWrapAround, bool aWholeWord, bool aSearchInFrames,
            bool aShowDialog, mozilla::ErrorResult& aError);

  bool DidFireDocElemInserted() const { return mDidFireDocElemInserted; }
  void SetDidFireDocElemInserted() { mDidFireDocElemInserted = true; }

  MOZ_CAN_RUN_SCRIPT
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> OpenDialog(
      JSContext* aCx, const nsAString& aUrl, const nsAString& aName,
      const nsAString& aOptions,
      const mozilla::dom::Sequence<JS::Value>& aExtraArgument,
      mozilla::ErrorResult& aError);
  void UpdateCommands(const nsAString& anAction);

  void GetContent(JSContext* aCx, JS::MutableHandle<JSObject*> aRetval,
                  mozilla::dom::CallerType aCallerType,
                  mozilla::ErrorResult& aError);

  already_AddRefed<mozilla::dom::Promise> CreateImageBitmap(
      const mozilla::dom::ImageBitmapSource& aImage,
      const mozilla::dom::ImageBitmapOptions& aOptions,
      mozilla::ErrorResult& aRv);

  already_AddRefed<mozilla::dom::Promise> CreateImageBitmap(
      const mozilla::dom::ImageBitmapSource& aImage, int32_t aSx, int32_t aSy,
      int32_t aSw, int32_t aSh,
      const mozilla::dom::ImageBitmapOptions& aOptions,
      mozilla::ErrorResult& aRv);

  void StructuredClone(JSContext* aCx, JS::Handle<JS::Value> aValue,
                       const mozilla::dom::StructuredSerializeOptions& aOptions,
                       JS::MutableHandle<JS::Value> aRetval,
                       mozilla::ErrorResult& aError);

  // ChromeWindow bits.  Do NOT call these unless your window is in
  // fact chrome.
  uint16_t WindowState();
  bool IsFullyOccluded();
  nsIBrowserDOMWindow* GetBrowserDOMWindow(mozilla::ErrorResult& aError);
  void SetBrowserDOMWindow(nsIBrowserDOMWindow* aBrowserWindow,
                           mozilla::ErrorResult& aError);
  void GetAttention(mozilla::ErrorResult& aError);
  void GetAttentionWithCycleCount(int32_t aCycleCount,
                                  mozilla::ErrorResult& aError);
  void SetCursor(const nsACString& aCursor, mozilla::ErrorResult& aError);
  void Maximize();
  void Minimize();
  void Restore();
  void GetWorkspaceID(nsAString& workspaceID);
  void MoveToWorkspace(const nsAString& workspaceID);
  bool IsCloaked() const;
  void NotifyDefaultButtonLoaded(mozilla::dom::Element& aDefaultButton,
                                 mozilla::ErrorResult& aError);
  mozilla::dom::ChromeMessageBroadcaster* MessageManager();
  mozilla::dom::ChromeMessageBroadcaster* GetGroupMessageManager(
      const nsAString& aGroup);

  already_AddRefed<mozilla::dom::Promise> PromiseDocumentFlushed(
      mozilla::dom::PromiseDocumentFlushedCallback& aCallback,
      mozilla::ErrorResult& aError);

  void GetInterface(JSContext* aCx, JS::Handle<JS::Value> aIID,
                    JS::MutableHandle<JS::Value> aRetval,
                    mozilla::ErrorResult& aError);

  already_AddRefed<nsWindowRoot> GetWindowRoot(mozilla::ErrorResult& aError);

  bool ShouldReportForServiceWorkerScope(const nsAString& aScope);

  void GetInstallTrigger(JSContext* aCx, JS::MutableHandle<JSObject*> aResult);

  nsIDOMWindowUtils* GetWindowUtils(mozilla::ErrorResult& aRv);

  void UpdateTopInnerWindow();

  virtual bool IsInSyncOperation() override;

  // Early during inner window creation, `IsSharedMemoryAllowedInternal`
  // is called before the `mDoc` field has been initialized in order to
  // determine whether to expose the `SharedArrayBuffer` constructor on the
  // JS global. We still want to consider the document's principal to see if
  // it is a privileged extension which should be exposed to
  // `SharedArrayBuffer`, however the inner window doesn't know the document's
  // principal yet. `aPrincipalOverride` is used in that situation to provide
  // the principal for the to-be-loaded document.
  bool IsSharedMemoryAllowed() const override {
    return IsSharedMemoryAllowedInternal(
        const_cast<nsGlobalWindowInner*>(this)->GetPrincipal());
  }

  bool IsSharedMemoryAllowedInternal(nsIPrincipal* aPrincipal = nullptr) const;

  // https://whatpr.org/html/4734/structured-data.html#cross-origin-isolated
  bool CrossOriginIsolated() const override;
  bool OriginAgentCluster() const;

  mozilla::dom::WebTaskScheduler* Scheduler();

  MOZ_CAN_RUN_SCRIPT bool SynthesizeMouseEvent(
      const nsAString& aType, float aOffsetX, float aOffsetY,
      const mozilla::dom::SynthesizeMouseEventData& aMouseEventData,
      const mozilla::dom::SynthesizeMouseEventOptions& aOptions,
      const mozilla::dom::Optional<
          mozilla::OwningNonNull<mozilla::dom::VoidFunction>>& aCallback,
      mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT bool SynthesizeTouchEvent(
      const nsAString& aType,
      const nsTArray<mozilla::dom::SynthesizeTouchEventData>& aTouches,
      const int32_t aModifiers,
      const mozilla::dom::SynthesizeTouchEventOptions& aOptions,
      const mozilla::dom::Optional<
          mozilla::OwningNonNull<mozilla::dom::VoidFunction>>& aCallback,
      mozilla::ErrorResult& aError);

 protected:
  // Web IDL helpers

  // Redefine the property called aPropName on this window object to be a value
  // property with the value aValue, much like we would do for a [Replaceable]
  // property in IDL.
  void RedefineProperty(JSContext* aCx, const char* aPropName,
                        JS::Handle<JS::Value> aValue,
                        mozilla::ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT nsresult
  GetInnerWidth(mozilla::dom::CallerType aCallerType, double* aWidth) override;
  MOZ_CAN_RUN_SCRIPT nsresult GetInnerHeight(
      mozilla::dom::CallerType aCallerType, double* aHeight) override;

 public:
  MOZ_CAN_RUN_SCRIPT double GetInnerWidth(mozilla::dom::CallerType aCallerType,
                                          mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT double GetInnerHeight(mozilla::dom::CallerType aCallerType,
                                           mozilla::ErrorResult& aError);
  int32_t GetScreenX(mozilla::dom::CallerType aCallerType,
                     mozilla::ErrorResult& aError);
  int32_t GetScreenY(mozilla::dom::CallerType aCallerType,
                     mozilla::ErrorResult& aError);
  int32_t GetOuterWidth(mozilla::dom::CallerType aCallerType,
                        mozilla::ErrorResult& aError);
  int32_t GetOuterHeight(mozilla::dom::CallerType aCallerType,
                         mozilla::ErrorResult& aError);

 protected:
  friend class HashchangeCallback;
  friend class mozilla::dom::BarProp;

  // Object Management
  virtual ~nsGlobalWindowInner();

  void FreeInnerObjects();

  // Initialize state that depends on the document.  By this point, mDoc should
  // be set correctly and have us set as its script global object.
  void InitDocumentDependentState(JSContext* aCx);

  nsresult EnsureClientSource();
  nsresult ExecutionReady();

  // Inner windows only.
  nsresult DefineArgumentsProperty(nsIArray* aArguments);

  // Get the parent, returns null if this is a toplevel window
  nsPIDOMWindowOuter* GetInProcessParentInternal();

 private:
  template <typename Method, typename... Args>
  mozilla::CallState CallOnInProcessDescendantsInternal(
      mozilla::dom::BrowsingContext* aBrowsingContext, bool aChildOnly,
      Method aMethod, Args&&... aArgs);

  // Call the given method on the immediate children of this window.  The
  // CallState returned by the last child method invocation is returned or
  // CallState::Continue if the method returns void.
  template <typename Method, typename... Args>
  mozilla::CallState CallOnInProcessChildren(Method aMethod, Args&&... aArgs) {
    MOZ_ASSERT(IsCurrentInnerWindow());
    return CallOnInProcessDescendantsInternal(GetBrowsingContext(), true,
                                              aMethod, aArgs...);
  }

  // Call the given method on the descendant of this window.  The CallState
  // returned by the last descendant method invocation is returned or
  // CallState::Continue if the method returns void.
  template <typename Method, typename... Args>
  mozilla::CallState CallOnInProcessDescendants(Method aMethod,
                                                Args&&... aArgs) {
    return CallOnInProcessDescendantsInternal(GetBrowsingContext(), false,
                                              aMethod, aArgs...);
  }

  // Helper to convert a void returning child method into an implicit
  // CallState::Continue value.
  template <typename Return, typename Method, typename... Args>
  std::enable_if_t<std::is_void_v<Return>, mozilla::CallState> CallDescendant(
      nsGlobalWindowInner* aWindow, Method aMethod, Args&&... aArgs) {
    (aWindow->*aMethod)(aArgs...);
    return mozilla::CallState::Continue;
  }

  // Helper that passes through the CallState value from a child method.
  template <typename Return, typename Method, typename... Args>
  std::enable_if_t<std::is_same_v<Return, mozilla::CallState>,
                   mozilla::CallState>
  CallDescendant(nsGlobalWindowInner* aWindow, Method aMethod,
                 Args&&... aArgs) {
    return (aWindow->*aMethod)(aArgs...);
  }

  void FreezeInternal(bool aIncludeSubWindows);
  void ThawInternal(bool aIncludeSubWindows);

  mozilla::CallState ShouldReportForServiceWorkerScopeInternal(
      const nsACString& aScope, bool* aResultOut);

 public:
  // Timeout Functions
  // |interval| is in milliseconds.
  MOZ_CAN_RUN_SCRIPT
  int32_t SetTimeoutOrInterval(
      JSContext* aCx,
      const mozilla::dom::FunctionOrTrustedScriptOrString& aHandler,
      int32_t aTimeout, const mozilla::dom::Sequence<JS::Value>& aArguments,
      bool aIsInterval, nsIPrincipal* aSubjectPrincipal,
      mozilla::ErrorResult& aError);

  // Return true if |aTimeout| was cleared while its handler ran.
  MOZ_CAN_RUN_SCRIPT
  bool RunTimeoutHandler(mozilla::dom::Timeout* aTimeout) override;

  // Helper Functions
  already_AddRefed<nsIDocShellTreeOwner> GetTreeOwner();
  already_AddRefed<nsIWebBrowserChrome> GetWebBrowserChrome();
  bool IsPrivateBrowsing();

  MOZ_CAN_RUN_SCRIPT void FireOfflineStatusEventIfChanged();

 public:
  // Inner windows only.
  nsresult FireHashchange(const nsAString& aOldURL, const nsAString& aNewURL);

  void FlushPendingNotifications(mozilla::FlushType aType);

  void ScrollTo(const mozilla::CSSPoint& aScroll,
                const mozilla::dom::ScrollOptions& aOptions);

  already_AddRefed<nsIWidget> GetMainWidget() const;
  nsIWidget* GetNearestWidget() const;

  bool IsInModalState();

  void SetFocusedElement(mozilla::dom::Element* aElement,
                         uint32_t aFocusMethod = 0,
                         bool aNeedsFocus = false) override;

  uint32_t GetFocusMethod() override;

  bool ShouldShowFocusRing() override;

  // Inner windows only.
  void UpdateCanvasFocus(bool aFocusChanged, nsIContent* aNewContent);

 public:
  virtual already_AddRefed<nsPIWindowRoot> GetTopWindowRoot() override;

  // Get the toplevel principal, returns null if this is a toplevel window.
  nsIPrincipal* GetTopLevelAntiTrackingPrincipal();

  // Get the client principal, returns null if the clientSource is not
  // available.
  nsIPrincipal* GetClientPrincipal();

  // Whether the chrome window is currently in a full screen transition. This
  // flag is updated from FullscreenTransitionTask.
  bool IsInFullScreenTransition();

  // This method is called if this window loads a 3rd party tracking resource
  // and the storage is just been changed. The window can reset the partitioned
  // storage objects and switch to the first party cookie jar.
  RefPtr<mozilla::GenericPromise> StorageAccessPermissionChanged(bool aGranted);

 protected:
  void NotifyWindowIDDestroyed(const char* aTopic);

  virtual void UpdateParentTarget() override;

  // Clear the document-dependent slots on our JS wrapper.  Inner windows only.
  void ClearDocumentDependentSlots(JSContext* aCx);

  // Inner windows only.
  already_AddRefed<mozilla::dom::StorageEvent> CloneStorageEvent(
      const nsAString& aType, const RefPtr<mozilla::dom::StorageEvent>& aEvent,
      mozilla::ErrorResult& aRv);

 protected:
  already_AddRefed<nsDOMCSSDeclaration> GetComputedStyleHelper(
      mozilla::dom::Element& aElt, const nsAString& aPseudoElt,
      bool aDefaultStylesOnly, mozilla::ErrorResult& aError);

  nsGlobalWindowInner* InnerForSetTimeoutOrInterval(
      mozilla::ErrorResult& aError);

  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const nsAString& aTargetOrigin,
                      JS::Handle<JS::Value> aTransfer,
                      nsIPrincipal& aSubjectPrincipal,
                      mozilla::ErrorResult& aError);

 private:
  // Fire the JS engine's onNewGlobalObject hook.  Only used on inner windows.
  void FireOnNewGlobalObject();

  // Helper for resolving the components shim.
  bool ResolveComponentsShim(
      JSContext* aCx, JS::Handle<JSObject*> aObj,
      JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> aDesc);

  // nsPIDOMWindow{Inner,Outer} should be able to see these helper methods.
  friend class nsPIDOMWindowInner;
  friend class nsPIDOMWindowOuter;

  bool IsBackgroundInternal() const override;

  // NOTE: Chrome Only
  void DisconnectAndClearGroupMessageManagers() {
    MOZ_RELEASE_ASSERT(IsChromeWindow());
    for (const auto& entry : mChromeFields.mGroupMessageManagers) {
      mozilla::dom::ChromeMessageBroadcaster* mm = entry.GetRawPtr();
      if (mm) {
        mm->Disconnect();
      }
    }
    mChromeFields.mGroupMessageManagers.Clear();
  }

  // Call mDocumentFlushedResolvers items, and perform MicroTask checkpoint
  // after that.
  //
  // If aUntilExhaustion is true, then we call resolvers that get added as a
  // result synchronously, otherwise we wait until the next refresh driver tick.
  void CallDocumentFlushedResolvers(bool aUntilExhaustion);

  // Called after a refresh driver tick. See documentation of
  // CallDocumentFlushedResolvers for the meaning of aUntilExhaustion.
  //
  // Returns whether we need to keep observing the refresh driver or not.
  bool MaybeCallDocumentFlushedResolvers(bool aUntilExhaustion);

  // Try to fire the "load" event on our content embedder if we're an iframe.
  MOZ_CAN_RUN_SCRIPT void FireFrameLoadEvent();

  void UpdateAutoplayPermission();
  void UpdateShortcutsPermission();
  void UpdatePopupPermission();

  void UpdatePermissions();

 public:
  static uint32_t GetShortcutsPermission(nsIPrincipal* aPrincipal);
  bool IsPlayingAudio() override;

  // Dispatch a runnable related to the global.
  nsresult Dispatch(already_AddRefed<nsIRunnable> aRunnable) const final;
  nsISerialEventTarget* SerialEventTarget() const final;

  void DisableIdleCallbackRequests();
  uint32_t LastIdleRequestHandle() const {
    return mIdleRequestCallbackCounter - 1;
  }
  MOZ_CAN_RUN_SCRIPT
  void RunIdleRequest(mozilla::dom::IdleRequest* aRequest,
                      DOMHighResTimeStamp aDeadline, bool aDidTimeout);
  MOZ_CAN_RUN_SCRIPT
  void ExecuteIdleRequest(TimeStamp aDeadline);
  void ScheduleIdleRequestDispatch();
  void SuspendIdleRequests();
  void ResumeIdleRequests();

  using IdleRequests = mozilla::LinkedList<RefPtr<mozilla::dom::IdleRequest>>;
  void RemoveIdleCallback(mozilla::dom::IdleRequest* aRequest);

  void SetActiveLoadingState(bool aIsLoading) override;

  // Hint to the JS engine whether we are currently loading.
  void HintIsLoading(bool aIsLoading);

  mozilla::dom::ContentMediaController* GetContentMediaController();

  bool TryOpenExternalProtocolIframe() {
    if (mHasOpenedExternalProtocolFrame) {
      return false;
    }
    mHasOpenedExternalProtocolFrame = true;
    return true;
  }

  nsTArray<uint32_t>& GetScrollMarks() { return mScrollMarks; }
  bool GetScrollMarksOnHScrollbar() const { return mScrollMarksOnHScrollbar; }
  void SetScrollMarks(const nsTArray<uint32_t>& aScrollMarks,
                      bool aOnHScrollbar);

  // Don't use this value directly, call StorageAccess::StorageAllowedForWindow
  // instead.
  mozilla::Maybe<mozilla::StorageAccess> GetStorageAllowedCache(
      uint32_t& aRejectedReason) {
    if (mStorageAllowedCache.isSome()) {
      aRejectedReason = mStorageAllowedReasonCache;
    }
    return mStorageAllowedCache;
  }
  void SetStorageAllowedCache(const mozilla::StorageAccess& storageAllowed,
                              uint32_t aRejectedReason) {
    mStorageAllowedCache = Some(storageAllowed);
    mStorageAllowedReasonCache = aRejectedReason;
  }
  void ClearStorageAllowedCache() {
    mStorageAllowedCache = mozilla::Nothing();
    mStorageAllowedReasonCache = 0;
  }

  virtual JS::loader::ModuleLoaderBase* GetModuleLoader(
      JSContext* aCx) override;

  mozilla::dom::TrustedTypePolicyFactory* TrustedTypes();

  void SetCurrentPasteDataTransfer(mozilla::dom::DataTransfer* aDataTransfer);
  mozilla::dom::DataTransfer* GetCurrentPasteDataTransfer() const;

  mozilla::dom::ClientSource* GetClientSource() const {
    return mClientSource.get();
  }

  void HoldOldPrincipal(nsIPrincipal* aPrincipal);

 private:
  void ClearOldPrincipalsIfNotNestedEventLoop();

  RefPtr<mozilla::dom::ContentMediaController> mContentMediaController;

  RefPtr<mozilla::dom::WebTaskSchedulerMainThread> mWebTaskScheduler;

  RefPtr<mozilla::dom::TrustedTypePolicyFactory> mTrustedTypePolicyFactory;

 protected:
  // Whether we need to care about orientation changes.
  bool mHasOrientationChangeListeners : 1;

  // Window offline status. Checked to see if we need to fire offline event
  bool mWasOffline : 1;

  // Represents whether the inner window's page has had a slow script notice.
  // Only used by inner windows; will always be false for outer windows.
  // This is used to implement Telemetry measures such as
  // SLOW_SCRIPT_PAGE_COUNT (glean::dom::slow_script_page_count).
  bool mHasHadSlowScript : 1;

  // Fast way to tell if this is a chrome window (without having to QI).
  bool mIsChrome : 1;

  // Hack to indicate whether a chrome window needs its message manager
  // to be disconnected, since clean up code is shared in the global
  // window superclass.
  bool mCleanMessageManager : 1;

  // Indicates that the current document has never received a document focus
  // event.
  bool mNeedsFocus : 1;

  // true if tab navigation has occurred for this window. Focus rings
  // should be displayed.
  bool mFocusByKeyOccurred : 1;

  // True if we have notified document-element-inserted observers for this
  // document.
  bool mDidFireDocElemInserted : 1;

  // Indicates whether this window wants gamepad input events
  bool mHasGamepad : 1;

  // Indicates whether this window has content that has an XR session
  // An XR session results in enumeration and activation of XR devices.
  bool mHasXRSession : 1;

  // Indicates whether this window wants VRDisplayActivate events
  bool mHasVRDisplayActivateEvents : 1;

  // Indicates that a request for XR runtime detection has been
  // requested, but has not yet been resolved
  bool mXRRuntimeDetectionInFlight : 1;

  // Indicates that an XR permission request has been requested
  // but has not yet been resolved.
  bool mXRPermissionRequestInFlight : 1;

  // Indicates that an XR permission request has been granted.
  // The page should not request permission multiple times.
  bool mXRPermissionGranted : 1;

  // True if this was the currently-active inner window for a BrowsingContext at
  // the time it was discarded.
  bool mWasCurrentInnerWindow : 1;
  void SetWasCurrentInnerWindow() { mWasCurrentInnerWindow = true; }
  bool WasCurrentInnerWindow() const override { return mWasCurrentInnerWindow; }

  bool mHasSeenGamepadInput : 1;

  // Whether we told the JS engine that we were in pageload.
  bool mHintedWasLoading : 1;

  // Whether this window has opened an external-protocol iframe without user
  // activation once already. Only relevant for top windows.
  bool mHasOpenedExternalProtocolFrame : 1;

  bool mScrollMarksOnHScrollbar : 1;

  nsCheapSet<nsUint32HashKey> mGamepadIndexSet;
  nsRefPtrHashtable<nsGenericHashKey<mozilla::dom::GamepadHandle>,
                    mozilla::dom::Gamepad>
      mGamepads;

  RefPtr<nsScreen> mScreen;

  RefPtr<mozilla::dom::BarProp> mMenubar;
  RefPtr<mozilla::dom::BarProp> mToolbar;
  RefPtr<mozilla::dom::BarProp> mLocationbar;
  RefPtr<mozilla::dom::BarProp> mPersonalbar;
  RefPtr<mozilla::dom::BarProp> mStatusbar;
  RefPtr<mozilla::dom::BarProp> mScrollbars;

  RefPtr<nsGlobalWindowObserver> mObserver;
  RefPtr<mozilla::dom::Crypto> mCrypto;
  RefPtr<mozilla::dom::cache::CacheStorage> mCacheStorage;
  RefPtr<mozilla::dom::Console> mConsole;
  RefPtr<mozilla::dom::CookieStore> mCookieStore;
  RefPtr<mozilla::dom::DocumentPictureInPicture> mDocumentPiP;
  RefPtr<mozilla::dom::Worklet> mPaintWorklet;
  RefPtr<mozilla::dom::External> mExternal;
  RefPtr<mozilla::dom::PushManager> mPushManager;

  RefPtr<mozilla::dom::Storage> mLocalStorage;
  RefPtr<mozilla::dom::Storage> mSessionStorage;

  RefPtr<mozilla::EventListenerManager> mListenerManager;
  RefPtr<mozilla::dom::Location> mLocation;
  RefPtr<nsHistory> mHistory;
  RefPtr<mozilla::dom::CustomElementRegistry> mCustomElements;

  nsTObserverArray<RefPtr<mozilla::dom::SharedWorker>> mSharedWorkers;

  RefPtr<mozilla::dom::VisualViewport> mVisualViewport;

  // The document's principals and policyContainer are only stored if
  // FreeInnerObjects has been called.
  nsCOMPtr<nsIPrincipal> mDocumentPrincipal;
  nsCOMPtr<nsIPrincipal> mDocumentCookiePrincipal;
  nsCOMPtr<nsIPrincipal> mDocumentStoragePrincipal;
  nsCOMPtr<nsIPrincipal> mDocumentPartitionedPrincipal;
  nsCOMPtr<nsIPolicyContainer> mDocumentPolicyContainer;

  // Used to cache the result of StorageAccess::StorageAllowedForWindow.
  // Don't use this field directly, use StorageAccess::StorageAllowedForWindow
  // instead.
  mozilla::Maybe<mozilla::StorageAccess> mStorageAllowedCache;
  uint32_t mStorageAllowedReasonCache;

  RefPtr<mozilla::dom::DebuggerNotificationManager>
      mDebuggerNotificationManager;

  // mBrowserChild is only ever populated in the content process.
  nsCOMPtr<nsIBrowserChild> mBrowserChild;

  uint32_t mSuspendDepth;
  uint32_t mFreezeDepth;

#ifdef DEBUG
  uint32_t mSerial;
#endif

  // the method that was used to focus mFocusedElement
  uint32_t mFocusMethod;

  // Only relevant if we're listening for orientation changes.
  int16_t mOrientationAngle = 0;

  // The current idle request callback handle
  uint32_t mIdleRequestCallbackCounter;
  IdleRequests mIdleRequestCallbacks;
  RefPtr<mozilla::dom::IdleRequestMap> mIdleRequestCallbacksByHandle;
  RefPtr<IdleRequestExecutor> mIdleRequestExecutor;

#ifdef DEBUG
  nsCOMPtr<nsIURI> mLastOpenedURI;
#endif

  RefPtr<mozilla::dom::IDBFactory> mIndexedDB;

  // This flag keeps track of whether this window is currently
  // observing refresh notifications from the refresh driver.
  bool mObservingRefresh;

  bool mIteratingDocumentFlushedResolvers;

  bool TryToObserveRefresh();

  nsTArray<uint32_t> mEnabledSensors;

#ifdef MOZ_WEBSPEECH
  RefPtr<mozilla::dom::SpeechSynthesis> mSpeechSynthesis;
#endif

  RefPtr<mozilla::glean::Glean> mGlean;
  RefPtr<mozilla::glean::GleanPings> mGleanPings;

  // This is the CC generation the last time we called CanSkip.
  uint32_t mCanSkipCCGeneration;

  // The VR Displays for this window
  nsTArray<RefPtr<mozilla::dom::VRDisplay>> mVRDisplays;

  RefPtr<mozilla::dom::VREventObserver> mVREventObserver;

  // The number of unload and beforeunload event listeners registered on this
  // window.
  uint64_t mUnloadOrBeforeUnloadListenerCount = 0;

  RefPtr<mozilla::dom::IntlUtils> mIntlUtils;

  mozilla::UniquePtr<mozilla::dom::ClientSource> mClientSource;

  nsTArray<mozilla::UniquePtr<PromiseDocumentFlushedResolver>>
      mDocumentFlushedResolvers;

  nsTArray<uint32_t> mScrollMarks;

  nsTArray<mozilla::WeakPtr<Document>> mDataDocumentsForMemoryReporting;

  static mozilla::StaticAutoPtr<InnerWindowByIdTable> sInnerWindowsById;

  // Members in the mChromeFields member should only be used in chrome windows.
  // All accesses to this field should be guarded by a check of mIsChrome.
  struct ChromeFields {
    RefPtr<mozilla::dom::ChromeMessageBroadcaster> mMessageManager;
    nsRefPtrHashtable<nsStringHashKey, mozilla::dom::ChromeMessageBroadcaster>
        mGroupMessageManagers{1};
  } mChromeFields;

  // Cache the DataTransfer created for a paste event, this will be reset after
  // the event is dispatched.
  RefPtr<mozilla::dom::DataTransfer> mCurrentPasteDataTransfer;

  nsTArray<nsCString> mMediaSourceURLs;

  // Holder for past principals to keep them alive so that we need to refcount
  // them only when we actually swap principals and we don't need to refcount
  // them on hot code paths in bindings that use raw pointers and must not
  // get the principal deleted from underneath them.
  nsTArray<nsCOMPtr<nsIPrincipal>> mOldPrincipals;

  // These fields are used by the inner and outer windows to prevent
  // programatically moving the window while the mouse is down.
  static bool sMouseDown;
  static bool sDragServiceDisabled;

  friend class nsDOMWindowUtils;
  friend class mozilla::dom::PostMessageEvent;
  friend class mozilla::dom::TimeoutManager;
  friend class IdleRequestExecutor;
  friend class nsGlobalWindowOuter;
};

inline nsISupports* ToSupports(nsGlobalWindowInner* p) {
  return static_cast<mozilla::dom::EventTarget*>(p);
}

inline nsISupports* ToCanonicalSupports(nsGlobalWindowInner* p) {
  return static_cast<mozilla::dom::EventTarget*>(p);
}

#endif /* nsGlobalWindowInner_h_ */
