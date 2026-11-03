/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPIDOMWindow_h_
#define nsPIDOMWindow_h_

#include "Units.h"
#include "js/TypeDecls.h"
#include "mozIDOMWindow.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/EventTarget.h"
#include "nsCOMPtr.h"
#include "nsIDOMWindow.h"
#include "nsILoadInfo.h"
#include "nsRefPtrHashtable.h"
#include "nsTArray.h"

class nsDOMCSSDeclaration;
class nsGlobalWindowInner;
class nsGlobalWindowOuter;
class nsIArray;
class nsIBaseWindow;
class nsIChannel;
class nsIContent;
class nsIContentSecurityPolicy;
class nsIDocShell;
class nsIDocShellTreeOwner;
class nsDocShellLoadState;
class nsIPolicyContainer;
class nsIPrincipal;
class nsIRunnable;
class nsIScriptTimeoutHandler;
class nsISerialEventTarget;
class nsIURI;
class nsIPrompt;
class nsIControllers;
class nsIWebBrowserChrome;
class nsPIDOMWindowInner;
class nsPIDOMWindowOuter;
class nsPIWindowRoot;

using SuspendTypes = uint32_t;

namespace mozilla::dom {
class AudioContext;
class BrowsingContext;
class BrowsingContextGroup;
class ClientInfo;
class ClientState;
class ContentFrameMessageManager;
class CloseWatcherManager;
class DocGroup;
class Document;
class Element;
class Location;
class MediaDevices;
class MediaKeys;
class Navigation;
class Navigator;
class Performance;
class Selection;
class ServiceWorker;
class ServiceWorkerDescriptor;
class Timeout;
class TimeoutManager;
class WebIdentityHandler;
class WindowContext;
class WindowGlobalChild;
class CustomElementRegistry;
class DocumentPictureInPicture;
enum class CallerType : uint32_t;
}  // namespace mozilla::dom

enum class FullscreenReason {
  // Toggling the fullscreen mode requires trusted context.
  ForFullscreenMode,
  // Fullscreen API is the API provided to untrusted content.
  ForFullscreenAPI,
  // This reason can only be used with exiting fullscreen.
  // It is otherwise identical to eForFullscreenAPI except it would
  // suppress the fullscreen transition.
  ForForceExitFullscreen
};

// Must be kept in sync with xpcom/rust/xpcom/src/interfaces/nonidl.rs
#define NS_PIDOMWINDOWINNER_IID \
  {0x775dabc9, 0x8f43, 0x4277, {0x9a, 0xdb, 0xf1, 0x99, 0x0d, 0x77, 0xcf, 0xfb}}

// Must be kept in sync with xpcom/rust/xpcom/src/interfaces/nonidl.rs
#define NS_PIDOMWINDOWOUTER_IID \
  {0x769693d4, 0xb009, 0x4fe2, {0xaf, 0x18, 0x7d, 0xc8, 0xdf, 0x74, 0x96, 0xdf}}

class nsPIDOMWindowInner : public mozIDOMWindow {
 protected:
  using Document = mozilla::dom::Document;
  friend nsGlobalWindowInner;
  friend nsGlobalWindowOuter;

  nsPIDOMWindowInner(nsPIDOMWindowOuter* aOuterWindow,
                     mozilla::dom::WindowGlobalChild* aActor);

  ~nsPIDOMWindowInner();

 public:
  NS_INLINE_DECL_STATIC_IID(NS_PIDOMWINDOWINNER_IID)

  nsIGlobalObject* AsGlobal();
  const nsIGlobalObject* AsGlobal() const;

  nsPIDOMWindowOuter* GetOuterWindow() const { return mOuterWindow; }

  static nsPIDOMWindowInner* From(mozIDOMWindow* aFrom) {
    return static_cast<nsPIDOMWindowInner*>(aFrom);
  }

  NS_IMPL_FROMEVENTTARGET_HELPER_WITH_GETTER(nsPIDOMWindowInner,
                                             GetAsInnerWindow())

  // Returns true if this object is the currently-active inner window for its
  // BrowsingContext.
  bool IsCurrentInnerWindow() const;

  // Returns true if the document of this window is the active document.  This
  // is identical to IsCurrentInnerWindow() now that document.open() no longer
  // creates new inner windows for the document it is called on.
  inline bool HasActiveDocument() const;

  // Return true if this object is the currently-active inner window for its
  // BrowsingContext and any container document is also fully active.
  // For https://html.spec.whatwg.org/multipage/browsers.html#fully-active
  bool IsFullyActive() const;

  // Returns true if this window is the same as mTopInnerWindow
  inline bool IsTopInnerWindow() const;

  // Returns true if this was the current window for its BrowsingContext when it
  // was discarded.
  virtual bool WasCurrentInnerWindow() const = 0;

  // Check whether a document is currently loading (really checks if the
  // load event has completed).  May not be reset to false on errors.
  inline bool IsLoading() const;
  inline bool IsHandlingResizeEvent() const;

  // Note: not related to IsLoading.  Set to false if there's an error, etc.
  virtual void SetActiveLoadingState(bool aIsActiveLoading) = 0;

  bool AddAudioContext(mozilla::dom::AudioContext* aAudioContext);
  void RemoveAudioContext(mozilla::dom::AudioContext* aAudioContext);
  void MuteAudioContexts();
  void UnmuteAudioContexts();

  void SetAudioCapture(bool aCapture);

  /**
   * Associate this inner window with a MediaKeys instance.
   */
  void AddMediaKeysInstance(mozilla::dom::MediaKeys* aMediaKeysInstance);

  /**
   * Remove an association between this inner window and a MediaKeys instance.
   */
  void RemoveMediaKeysInstance(mozilla::dom::MediaKeys* aMediaKeysInstance);

  /**
   * Return if any MediaKeys instances are associated with this window.
   */
  bool HasActiveMediaKeysInstance();

  mozilla::dom::Performance* GetPerformance();

  mozilla::dom::Performance* GetPerformanceIfExists() const {
    return mPerformance;
  }

  void QueuePerformanceNavigationTiming();

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a mouseenter/leave event listener.
   */
  bool HasMouseEnterLeaveEventListeners() const {
    return mMayHaveMouseEnterLeaveEventListener;
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a mouseenter/leave event listener.
   */
  void SetHasMouseEnterLeaveEventListeners() {
    mMayHaveMouseEnterLeaveEventListener = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a Pointerenter/leave event listener.
   */
  bool HasPointerEnterLeaveEventListeners() const {
    return mMayHavePointerEnterLeaveEventListener;
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a Pointerenter/leave event listener.
   */
  void SetHasPointerEnterLeaveEventListeners() {
    mMayHavePointerEnterLeaveEventListener = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a pointerrawupdate event listener.
   */
  bool HasPointerRawUpdateEventListeners() const {
    return mMayHavePointerRawUpdateEventListener;
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a pointerrawupdate event listener.
   * This may not accept that if the event is not available in this window.
   */
  void MaybeSetHasPointerRawUpdateEventListeners();

 protected:
  /**
   * Call this to clear whether some nodes has a pointerrawupdate event
   * listener.
   */
  void ClearHasPointerRawUpdateEventListeners();

 public:
  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a transition* event listeners.
   */
  bool HasTransitionEventListeners() { return mMayHaveTransitionEventListener; }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a transition* event listener.
   */
  void SetHasTransitionEventListeners() {
    mMayHaveTransitionEventListener = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a SMILTime* event listeners.
   */
  bool HasSMILTimeEventListeners() { return mMayHaveSMILTimeEventListener; }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a SMILTime* event listener.
   */
  void SetHasSMILTimeEventListeners() { mMayHaveSMILTimeEventListener = true; }

  // Sets the event for window.event. Does NOT take ownership, so
  // the caller is responsible for clearing the event before the
  // event gets deallocated. Pass nullptr to set window.event to
  // undefined. Returns the previous value.
  mozilla::dom::Event* SetEvent(mozilla::dom::Event* aEvent) {
    mozilla::dom::Event* old = mEvent;
    mEvent = aEvent;
    return old;
  }

  /**
   * Check whether this window is a secure context.
   */
  bool IsSecureContext() const;
  bool IsSecureContextIfOpenerIgnored() const;

  // Calling suspend should prevent any asynchronous tasks from
  // executing javascript for this window.  This means setTimeout,
  // requestAnimationFrame, and events should not be fired. Suspending
  // a window maybe also suspends its children.  Workers may
  // continue to perform computations in the background.  A window
  // can have Suspend() called multiple times and will only resume after
  // a matching number of Resume() calls.
  void Suspend(bool aIncludeSubWindows = true);
  void Resume(bool aIncludeSubWindows = true);

  // Whether or not this window was suspended by the BrowserContextGroup
  bool GetWasSuspendedByGroup() const { return mWasSuspendedByGroup; }
  void SetWasSuspendedByGroup(bool aSuspended) {
    mWasSuspendedByGroup = aSuspended;
  }

  // Apply the parent window's suspend, freeze, and modal state to the current
  // window.
  void SyncStateFromParentWindow();

  /**
   * Increment active peer connection count.
   */
  void AddPeerConnection();

  /**
   * Decrement active peer connection count.
   */
  void RemovePeerConnection();

  bool IsDocumentLoaded() const;

  // To cache top inner-window if available after constructed for tab-wised
  // indexedDB counters.
  void TryToCacheTopInnerWindow();

  mozilla::Maybe<mozilla::dom::ClientInfo> GetClientInfo() const;
  mozilla::Maybe<mozilla::dom::ClientState> GetClientState() const;
  mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor> GetController() const;
  mozilla::dom::ClientSource* GetClientSource() const;

  void SetPolicyContainer(nsIPolicyContainer* aPolicyContainer);
  nsIPolicyContainer* GetPolicyContainer();

  void SetPreloadCsp(nsIContentSecurityPolicy* aPreloadCsp);

  void NoteCalledRegisterForServiceWorkerScope(const nsACString& aScope);

  void NoteDOMContentLoaded();

  virtual mozilla::dom::CustomElementRegistry* CustomElements() = 0;

  // XXX: This is called on inner windows
  virtual nsPIDOMWindowOuter* GetInProcessScriptableTop() = 0;
  virtual nsPIDOMWindowOuter* GetInProcessScriptableParent() = 0;
  virtual already_AddRefed<nsPIWindowRoot> GetTopWindowRoot() = 0;

  mozilla::dom::EventTarget* GetChromeEventHandler() const {
    return mChromeEventHandler;
  }

  mozilla::dom::EventTarget* GetParentTarget() {
    if (!mParentTarget) {
      UpdateParentTarget();
    }
    return mParentTarget;
  }

  virtual void MaybeUpdateTouchState() {}

  Document* GetExtantDoc() const { return mDoc; }
  nsIURI* GetDocumentURI() const;
  nsIURI* GetDocBaseURI() const;

  Document* GetDoc() {
    if (!mDoc) {
      MaybeCreateDoc();
    }
    return mDoc;
  }

  mozilla::dom::WindowContext* GetWindowContext() const;
  mozilla::dom::WindowGlobalChild* GetWindowGlobalChild() const {
    return mWindowGlobalChild;
  }

  // Removes this inner window from the BFCache, if it is cached, and returns
  // true if it was.
  bool RemoveFromBFCacheSync();

  // Fire any DOM notification events related to things that happened while
  // the window was frozen.
  MOZ_CAN_RUN_SCRIPT virtual nsresult FireDelayedDOMEvents(
      bool aIncludeSubWindows) = 0;

  /**
   * Get the docshell in this window.
   */
  inline nsIDocShell* GetDocShell() const;

  /**
   * Get the browsing context in this window.
   */
  inline mozilla::dom::BrowsingContext* GetBrowsingContext() const;

  /**
   * Get the browsing context group this window belongs to.
   */
  mozilla::dom::BrowsingContextGroup* GetBrowsingContextGroup() const;

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a DOMActivate event listener.
   */
  void SetHasDOMActivateEventListeners() {
    mMayHaveDOMActivateEventListeners = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a DOMActivate event listener.
   */
  bool HasDOMActivateEventListeners() const {
    return mMayHaveDOMActivateEventListeners;
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a touch event listener.
   */
  void SetHasTouchEventListeners() {
    if (!mMayHaveTouchEventListener) {
      mMayHaveTouchEventListener = true;
      MaybeUpdateTouchState();
    }
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a selectionchange event listener.
   */
  void SetHasSelectionChangeEventListeners() {
    mMayHaveSelectionChangeEventListener = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a selectionchange event listener.
   */
  bool HasSelectionChangeEventListeners() const {
    return mMayHaveSelectionChangeEventListener;
  }

  /**
   * Call this to indicate that some node (this window, its document,
   * or content in that document) has a select event listener of form controls.
   */
  void SetHasFormSelectEventListeners() {
    mMayHaveFormSelectEventListener = true;
  }

  /**
   * Call this to check whether some node (this window, its document,
   * or content in that document) has a select event listener of form controls.
   */
  bool HasFormSelectEventListeners() const {
    return mMayHaveFormSelectEventListener;
  }

  /*
   * Get and set the currently focused element within the document. If
   * aNeedsFocus is true, then set mNeedsFocus to true to indicate that a
   * document focus event is needed.
   *
   * DO NOT CALL EITHER OF THESE METHODS DIRECTLY. USE THE FOCUS MANAGER
   * INSTEAD.
   */
  mozilla::dom::Element* GetFocusedElement() const {
    return mFocusedElement.get();
  }

  virtual void SetFocusedElement(mozilla::dom::Element* aElement,
                                 uint32_t aFocusMethod = 0,
                                 bool aNeedsFocus = false) = 0;

  bool UnknownFocusMethodShouldShowOutline() const {
    return mUnknownFocusMethodShouldShowOutline;
  }

  /**
   * Retrieves the method that was used to focus the current node.
   */
  virtual uint32_t GetFocusMethod() = 0;

  /*
   * Tells the window that it now has focus or has lost focus, based on the
   * state of aFocus. If this method returns true, then the document loaded
   * in the window has never received a focus event and expects to receive
   * one. If false is returned, the document has received a focus event before
   * and should only receive one if the window is being focused.
   *
   * aFocusMethod may be set to one of the focus method constants in
   * nsIFocusManager to indicate how focus was set.
   */
  virtual bool TakeFocus(bool aFocus, uint32_t aFocusMethod) = 0;

  /**
   * Indicates that the window may now accept a document focus event. This
   * should be called once a document has been loaded into the window.
   */
  virtual void SetReadyForFocus() = 0;

  /**
   * Whether the focused content within the window should show a focus ring.
   */
  virtual bool ShouldShowFocusRing() = 0;

  /**
   * Indicates that the page in the window has been hidden. This is used to
   * reset the focus state.
   */
  virtual void PageHidden(bool aIsEnteringBFCacheInParent) = 0;

  /**
   * Instructs this window to asynchronously dispatch a hashchange event.  This
   * method must be called on an inner window.
   */
  virtual nsresult DispatchAsyncHashchange(nsIURI* aOldURI,
                                           nsIURI* aNewURI) = 0;

  /**
   * Instructs this window to synchronously dispatch a popState event.
   */
  virtual nsresult DispatchSyncPopState() = 0;

  /**
   * Tell this window that it should listen for sensor changes of the given
   * type.
   */
  virtual void EnableDeviceSensor(uint32_t aType) = 0;

  /**
   * Tell this window that it should remove itself from sensor change
   * notifications.
   */
  virtual void DisableDeviceSensor(uint32_t aType) = 0;

#if defined(MOZ_WIDGET_ANDROID)
  virtual void EnableOrientationChangeListener() = 0;
  virtual void DisableOrientationChangeListener() = 0;
#endif

  /**
   * Tell this window that there is an observer for gamepad input
   *
   * Inner windows only.
   */
  virtual void SetHasGamepadEventListener(bool aHasGamepad = true) = 0;

  /**
   * Return the window id of this window
   */
  uint64_t WindowID() const { return mWindowID; }

  // WebIDL-ish APIs
  void MarkUncollectableForCCGeneration(uint32_t aGeneration) {
    mMarkedCCGeneration = aGeneration;
  }

  uint32_t GetMarkedCCGeneration() { return mMarkedCCGeneration; }

  mozilla::dom::Navigation* Navigation();
  mozilla::dom::Navigator* Navigator();
  mozilla::dom::MediaDevices* GetExtantMediaDevices() const;
  virtual mozilla::dom::Location* Location() = 0;

  virtual nsresult GetControllers(nsIControllers** aControllers) = 0;

  MOZ_CAN_RUN_SCRIPT virtual nsresult GetInnerWidth(
      mozilla::dom::CallerType aCallerType, double* aWidth) = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult GetInnerHeight(
      mozilla::dom::CallerType aCallerType, double* aHeight) = 0;

  virtual already_AddRefed<nsDOMCSSDeclaration> GetComputedStyle(
      mozilla::dom::Element& aElt, const nsAString& aPseudoElt,
      mozilla::ErrorResult& aError) = 0;

  virtual bool GetFullScreen() = 0;

  virtual nsresult Focus(mozilla::dom::CallerType aCallerType) = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult Close() = 0;

  mozilla::dom::DocGroup* GetDocGroup() const;

  RefPtr<mozilla::GenericPromise> SaveStorageAccessPermissionGranted();
  RefPtr<mozilla::GenericPromise> SaveStorageAccessPermissionRevoked();

  bool UsingStorageAccess();

  mozilla::dom::WebIdentityHandler* GetOrCreateWebIdentityHandler();

  uint32_t UpdateLockCount(bool aIncrement) {
    MOZ_ASSERT_IF(!aIncrement, mLockCount > 0);
    mLockCount += aIncrement ? 1 : -1;
    return mLockCount;
  };
  bool HasActiveLocks() { return mLockCount > 0; }

  uint32_t UpdateWebTransportCount(bool aIncrement) {
    MOZ_ASSERT_IF(!aIncrement, mWebTransportCount > 0);
    mWebTransportCount += aIncrement ? 1 : -1;
    return mWebTransportCount;
  };
  bool HasActiveWebTransports() { return mWebTransportCount > 0; }

  mozilla::dom::CloseWatcherManager* EnsureCloseWatcherManager();

  // Called when a CloseWatcher is added to the manager
  void NotifyCloseWatcherAdded();

  // Called when a CloseWatcher is removed from the manager
  void NotifyCloseWatcherRemoved();

  virtual mozilla::dom::DocumentPictureInPicture*
  GetExtantDocumentPictureInPicture() const = 0;

 protected:
  void CreatePerformanceObjectIfNeeded();

  // Lazily instantiate an about:blank document if necessary, and if
  // we have what it takes to do so.
  void MaybeCreateDoc();

  void SetChromeEventHandlerInternal(
      mozilla::dom::EventTarget* aChromeEventHandler) {
    mChromeEventHandler = aChromeEventHandler;
    // mParentTarget will be set when the next event is dispatched.
    mParentTarget = nullptr;
  }

  virtual void UpdateParentTarget() = 0;

  // These two variables are special in that they're set to the same
  // value on both the outer window and the current inner window. Make
  // sure you keep them in sync!
  nsCOMPtr<mozilla::dom::EventTarget> mChromeEventHandler;  // strong
  RefPtr<Document> mDoc;
  // Cache the URI when mDoc is cleared.
  nsCOMPtr<nsIURI> mDocumentURI;  // strong
  nsCOMPtr<nsIURI> mDocBaseURI;   // strong

  nsCOMPtr<mozilla::dom::EventTarget> mParentTarget;  // strong

  RefPtr<mozilla::dom::Performance> mPerformance;
  mozilla::UniquePtr<mozilla::dom::TimeoutManager> mTimeoutManager;
  RefPtr<mozilla::dom::WebIdentityHandler> mWebIdentityHandler;

  RefPtr<mozilla::dom::Navigation> mNavigation;

  RefPtr<mozilla::dom::Navigator> mNavigator;

  // These variables are only used on inner windows.
  uint32_t mActivePeerConnections = 0;

  bool mIsDocumentLoaded = false;
  bool mIsHandlingResizeEvent = false;
  bool mMayHaveDOMActivateEventListeners = false;
  bool mMayHaveTouchEventListener = false;
  bool mMayHaveSelectionChangeEventListener = false;
  bool mMayHaveFormSelectEventListener = false;
  bool mMayHaveMouseEnterLeaveEventListener = false;
  bool mMayHavePointerEnterLeaveEventListener = false;
  bool mMayHavePointerRawUpdateEventListener = false;
  bool mMayHaveTransitionEventListener = false;
  bool mMayHaveSMILTimeEventListener = false;

  // Our inner window's outer window.
  nsCOMPtr<nsPIDOMWindowOuter> mOuterWindow;

  // The element within the document that is currently focused when this
  // window is active.
  RefPtr<mozilla::dom::Element> mFocusedElement;

  // The AudioContexts created for the current document, if any.
  nsTArray<mozilla::dom::AudioContext*> mAudioContexts;  // Weak

  // Instances of MediaKeys created in this inner window. Storing these allows
  // us to shutdown MediaKeys when an inner windows is destroyed. We can also
  // use the presence of MediaKeys to assess if a window has EME activity.
  nsTArray<mozilla::dom::MediaKeys*> mMediaKeysInstances;  // Weak

  RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;

  // A unique (as long as our 64-bit counter doesn't roll over) id for
  // this window.
  uint64_t mWindowID = 0;

  // Set to true once we've sent the (chrome|content)-document-global-created
  // notification.
  bool mHasNotifiedGlobalCreated = false;

  // Whether when focused via an "unknown" focus method, we should show outlines
  // by default or not. The initial value of this is true (so as to show
  // outlines for stuff like html autofocus, or initial programmatic focus
  // without any other user interaction).
  bool mUnknownFocusMethodShouldShowOutline = true;

  uint32_t mMarkedCCGeneration = 0;

  // mTopInnerWindow is used for tab-wise check by timeout throttling. It could
  // be null.
  nsCOMPtr<nsPIDOMWindowInner> mTopInnerWindow;

  // The evidence that we have tried to cache mTopInnerWindow only once from
  // SetNewDocument(). Note: We need this extra flag because mTopInnerWindow
  // could be null and we don't want it to be set multiple times.
  bool mHasTriedToCacheTopInnerWindow = false;

  // The number of active IndexedDB databases.
  uint32_t mNumOfIndexedDBDatabases = 0;

  // The number of open WebSockets.
  uint32_t mNumOfOpenWebSockets = 0;

  // The event dispatch code sets and unsets this while keeping
  // the event object alive.
  mozilla::dom::Event* mEvent = nullptr;

  // The WindowGlobalChild actor for this window.
  //
  // This will be non-null during the full lifetime of the window, initialized
  // during SetNewDocument, and cleared during FreeInnerObjects.
  RefPtr<mozilla::dom::WindowGlobalChild> mWindowGlobalChild;

  bool mWasSuspendedByGroup = false;

  /**
   * Count of the number of active LockRequest objects, including ones from
   * workers.
   */
  uint32_t mLockCount = 0;
  /**
   * Count of the number of active WebTransport objects, including ones from
   * workers.
   */
  uint32_t mWebTransportCount = 0;

  // The CloseWatcherManager for this window.
  RefPtr<mozilla::dom::CloseWatcherManager> mCloseWatcherManager;
};

class nsPIDOMWindowOuter : public mozIDOMWindowProxy {
 protected:
  using Document = mozilla::dom::Document;

  explicit nsPIDOMWindowOuter(uint64_t aWindowID);

  ~nsPIDOMWindowOuter();

  void NotifyResumingDelayedMedia();

 public:
  NS_INLINE_DECL_STATIC_IID(NS_PIDOMWINDOWOUTER_IID)

  NS_IMPL_FROMEVENTTARGET_HELPER_WITH_GETTER(nsPIDOMWindowOuter,
                                             GetAsOuterWindow())

  static nsPIDOMWindowOuter* From(mozIDOMWindowProxy* aFrom) {
    return static_cast<nsPIDOMWindowOuter*>(aFrom);
  }

  // Given an inner window, return its outer if the inner is the current inner.
  // Otherwise (argument null or not an inner or not current) return null.
  static nsPIDOMWindowOuter* GetFromCurrentInner(nsPIDOMWindowInner* aInner);

  // Check whether a document is currently loading
  inline bool IsLoading() const;
  inline bool IsHandlingResizeEvent() const;

  nsPIDOMWindowInner* GetCurrentInnerWindow() const { return mInnerWindow; }

  nsPIDOMWindowInner* EnsureInnerWindow() {
    // GetDoc forces inner window creation if there isn't one already
    GetDoc();
    return GetCurrentInnerWindow();
  }

  bool IsRootOuterWindow() { return mIsRootOuterWindow; }

  // Internal getter/setter for the frame element, this version of the
  // getter crosses chrome boundaries whereas the public scriptable
  // one doesn't for security reasons.
  mozilla::dom::Element* GetFrameElementInternal() const;
  void SetFrameElementInternal(mozilla::dom::Element* aFrameElement);

  bool IsBackground() { return mIsBackground; }

  // Audio API
  bool GetAudioMuted() const;

  // No longer to delay media from starting for this window.
  void ActivateMediaComponents();
  bool ShouldDelayMediaFromStart() const;

  void RefreshMediaElementsVolume();

  virtual nsPIDOMWindowOuter* GetPrivateRoot() = 0;

  /**
   * |top| gets the root of the window hierarchy.
   *
   * This function does not cross chrome-content boundaries, so if this
   * window's parent is of a different type, |top| will return this window.
   *
   * When script reads the top property, we run GetInProcessScriptableTop,
   * which will not cross an <iframe mozbrowser> boundary.
   *
   * In contrast, C++ calls to GetTop are forwarded to GetRealTop, which
   * ignores <iframe mozbrowser> boundaries.
   */

  virtual already_AddRefed<nsPIDOMWindowOuter>
  GetInProcessTop() = 0;  // Outer only
  virtual already_AddRefed<nsPIDOMWindowOuter> GetInProcessParent() = 0;
  virtual nsPIDOMWindowOuter* GetInProcessScriptableTop() = 0;
  virtual nsPIDOMWindowOuter* GetInProcessScriptableParent() = 0;
  virtual already_AddRefed<nsPIWindowRoot> GetTopWindowRoot() = 0;

  /**
   * Behaves identically to GetInProcessScriptableParent except that it
   * returns null if GetInProcessScriptableParent would return this window.
   */
  virtual nsPIDOMWindowOuter* GetInProcessScriptableParentOrNull() = 0;

  virtual void SetIsBackground(bool aIsBackground) = 0;

  mozilla::dom::EventTarget* GetChromeEventHandler() const {
    return mChromeEventHandler;
  }

  virtual void SetChromeEventHandler(
      mozilla::dom::EventTarget* aChromeEventHandler) = 0;

  mozilla::dom::EventTarget* GetParentTarget() {
    if (!mParentTarget) {
      UpdateParentTarget();
    }
    return mParentTarget;
  }

  mozilla::dom::ContentFrameMessageManager* GetMessageManager() {
    // We maintain our mMessageManager state alongside mParentTarget.
    if (!mParentTarget) {
      UpdateParentTarget();
    }
    return mMessageManager;
  }

  Document* GetExtantDoc() const { return mDoc; }
  nsIURI* GetDocumentURI() const;

  Document* GetDoc() {
    if (!mDoc) {
      MaybeCreateDoc();
    }
    return mDoc;
  }

  // Set the window up with an about:blank document with the given principal.
  // Base URI, COEP and PolicyContainer of the current document will be
  // retained.
  MOZ_CAN_RUN_SCRIPT virtual void SetInitialPrincipal(
      nsIPrincipal* aNewWindowPrincipal) = 0;

  // Fire any DOM notification events related to things that happened while
  // the window was frozen.
  MOZ_CAN_RUN_SCRIPT virtual nsresult FireDelayedDOMEvents(
      bool aIncludeSubWindows) = 0;

  /**
   * Get the docshell in this window.
   */
  inline nsIDocShell* GetDocShell() const;

  /**
   * Get the browsing context in this window.
   */
  inline mozilla::dom::BrowsingContext* GetBrowsingContext() const;

  /**
   * Get the browsing context group this window belongs to.
   */
  mozilla::dom::BrowsingContextGroup* GetBrowsingContextGroup() const;

  /**
   * Set a new document in the window. Calling this method will in most cases
   * create a new inner window. This may be called with a pointer to the current
   * document, in that case the document remains unchanged, but a new inner
   * window will be created.
   *
   * aDocument must not be null.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult SetNewDocument(
      Document* aDocument, nsISupports* aState, bool aForceReuseInnerWindow,
      mozilla::dom::WindowGlobalChild* aActor = nullptr) = 0;

  /**
   * Ensure the size and position of this window are up-to-date by doing
   * a layout flush in the parent (which will in turn, do a layout flush
   * in its parent, etc.).
   */
  virtual void EnsureSizeAndPositionUpToDate() = 0;

  /**
   * Suppresses/unsuppresses user initiated event handling in window's document
   * and all in-process descendant documents.
   */
  virtual void SuppressEventHandling() = 0;
  virtual void UnsuppressEventHandling() = 0;

  /**
   * Callback for notifying a window about a modal dialog being
   * opened/closed with the window as a parent.
   *
   * If any script can run between the enter and leave modal states, and the
   * window isn't top, the LeaveModalState() should be called on the window
   * returned by EnterModalState().
   */
  virtual nsPIDOMWindowOuter* EnterModalState() = 0;
  virtual void LeaveModalState() = 0;

  virtual bool CanClose() = 0;
  MOZ_CAN_RUN_SCRIPT virtual void ForceClose() = 0;

  /**
   * Moves the top-level window into fullscreen mode if aIsFullScreen is true,
   * otherwise exits fullscreen.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult SetFullscreenInternal(
      FullscreenReason aReason, bool aIsFullscreen) = 0;
  MOZ_CAN_RUN_SCRIPT virtual void FullscreenWillChange(bool aIsFullscreen) = 0;
  /**
   * This function should be called when the fullscreen state is flipped.
   * If no widget is involved the fullscreen change, this method is called
   * by SetFullscreenInternal, otherwise, it is called when the widget
   * finishes its change to or from fullscreen.
   *
   * @param aIsFullscreen indicates whether the widget is in fullscreen.
   */
  MOZ_CAN_RUN_SCRIPT virtual void FinishFullscreenChange(
      bool aIsFullscreen) = 0;

  virtual void MacFullscreenMenubarOverlapChanged(
      mozilla::DesktopCoord aOverlapAmount) = 0;

  // XXX: These focus methods all forward to the inner, could we change
  // consumers to call these on the inner directly?

  /*
   * Get and set the currently focused element within the document. If
   * aNeedsFocus is true, then set mNeedsFocus to true to indicate that a
   * document focus event is needed.
   *
   * DO NOT CALL EITHER OF THESE METHODS DIRECTLY. USE THE FOCUS MANAGER
   * INSTEAD.
   */
  inline mozilla::dom::Element* GetFocusedElement() const;

  virtual void SetFocusedElement(mozilla::dom::Element* aElement,
                                 uint32_t aFocusMethod = 0,
                                 bool aNeedsFocus = false) = 0;
  /**
   * Get whether a focused element focused by unknown reasons (like script
   * focus) should match the :focus-visible pseudo-class.
   */
  bool UnknownFocusMethodShouldShowOutline() const;

  /**
   * Retrieves the method that was used to focus the current node.
   */
  virtual uint32_t GetFocusMethod() = 0;

  /*
   * Tells the window that it now has focus or has lost focus, based on the
   * state of aFocus. If this method returns true, then the document loaded
   * in the window has never received a focus event and expects to receive
   * one. If false is returned, the document has received a focus event before
   * and should only receive one if the window is being focused.
   *
   * aFocusMethod may be set to one of the focus method constants in
   * nsIFocusManager to indicate how focus was set.
   */
  virtual bool TakeFocus(bool aFocus, uint32_t aFocusMethod) = 0;

  /**
   * Indicates that the window may now accept a document focus event. This
   * should be called once a document has been loaded into the window.
   */
  virtual void SetReadyForFocus() = 0;

  /**
   * Whether the focused content within the window should show a focus ring.
   */
  virtual bool ShouldShowFocusRing() = 0;

  /**
   * Indicates that the page in the window has been hidden. This is used to
   * reset the focus state.
   */
  virtual void PageHidden(bool aIsEnteringBFCacheInParent) = 0;

  /**
   * Return the window id of this window
   */
  uint64_t WindowID() const { return mWindowID; }

  /**
   * Dispatch a custom event with name aEventName targeted at this window.
   * Returns whether the default action should be performed.
   *
   * Outer windows only.
   */
  MOZ_CAN_RUN_SCRIPT virtual bool DispatchCustomEvent(
      const nsAString& aEventName,
      mozilla::ChromeOnlyDispatch aChromeOnlyDispatch =
          mozilla::ChromeOnlyDispatch::eNo) = 0;

  /**
   * Like nsIDOMWindow::Open, except that we don't navigate to the given URL.
   *
   * Outer windows only.
   */
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult OpenNoNavigate(const nsACString& aUrl,
                                  const nsAString& aName,
                                  const nsAString& aOptions,
                                  mozilla::dom::BrowsingContext** _retval) = 0;

  /**
   * Fire a popup blocked event.
   */
  virtual void FirePopupBlockedEvent(nsIURI* aPopupURI,
                                     const nsAString& aPopupWindowName,
                                     const nsAString& aPopupWindowFeatures) = 0;

  /**
   * Fire a redirect blocked event.
   */
  virtual void FireRedirectBlockedEvent(nsIURI* aRedirectURI) = 0;

  // WebIDL-ish APIs
  void MarkUncollectableForCCGeneration(uint32_t aGeneration) {
    mMarkedCCGeneration = aGeneration;
  }

  uint32_t GetMarkedCCGeneration() { return mMarkedCCGeneration; }

  // XXX(nika): These feel like they should be inner window only, but they're
  // called on the outer window.
  virtual mozilla::dom::Navigator* GetNavigator() = 0;
  virtual mozilla::dom::Location* GetLocation() = 0;

  virtual nsresult GetPrompter(nsIPrompt** aPrompt) = 0;
  virtual nsresult GetControllers(nsIControllers** aControllers) = 0;
  virtual already_AddRefed<mozilla::dom::Selection> GetSelection() = 0;
  virtual mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder>
  GetOpener() = 0;

  // aLoadState will be passed on through to the windowwatcher.
  // aForceNoOpener will act just like a "noopener" feature in aOptions except
  //                will not affect any other window features.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult Open(const nsACString& aUrl, const nsAString& aName,
                        const nsAString& aOptions,
                        nsDocShellLoadState* aLoadState, bool aForceNoOpener,
                        mozilla::dom::BrowsingContext** _retval) = 0;
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult OpenDialog(const nsACString& aUrl, const nsAString& aName,
                              const nsAString& aOptions, nsIArray* aArguments,
                              mozilla::dom::BrowsingContext** _retval) = 0;

  virtual mozilla::dom::Element* GetFrameElement() = 0;

  virtual bool Closed() = 0;
  virtual bool GetFullScreen() = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult SetFullScreen(bool aFullscreen) = 0;

  virtual nsresult Focus(mozilla::dom::CallerType aCallerType) = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult Close() = 0;

  virtual nsresult MoveBy(int32_t aXDif, int32_t aYDif) = 0;

  virtual void UpdateCommands(const nsAString& anAction) = 0;

  mozilla::dom::DocGroup* GetDocGroup() const;

  already_AddRefed<nsIDocShellTreeOwner> GetTreeOwner();
  already_AddRefed<nsIBaseWindow> GetTreeOwnerWindow();
  already_AddRefed<nsIWebBrowserChrome> GetWebBrowserChrome();

  virtual void UpdateParentTarget() = 0;

 protected:
  // Lazily instantiate an about:blank document if necessary, and if
  // we have what it takes to do so.
  void MaybeCreateDoc();

  void SetChromeEventHandlerInternal(
      mozilla::dom::EventTarget* aChromeEventHandler);

  // These two variables are special in that they're set to the same
  // value on both the outer window and the current inner window. Make
  // sure you keep them in sync!
  nsCOMPtr<mozilla::dom::EventTarget> mChromeEventHandler;  // strong
  RefPtr<Document> mDoc;
  // Cache the URI when mDoc is cleared.
  nsCOMPtr<nsIURI> mDocumentURI;  // strong

  nsCOMPtr<mozilla::dom::EventTarget> mParentTarget;                 // strong
  RefPtr<mozilla::dom::ContentFrameMessageManager> mMessageManager;  // strong

  nsCOMPtr<mozilla::dom::Element> mFrameElement;

  // These references are used by nsGlobalWindow.
  nsCOMPtr<nsIDocShell> mDocShell;
  RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;

  uint32_t mModalStateDepth;

  uint32_t mSuppressEventHandlingDepth;

  // Tracks whether our docshell is active.  If it is, mIsBackground
  // is false.  Too bad we have so many different concepts of
  // "active".
  bool mIsBackground;

  bool mIsRootOuterWindow;

  // And these are the references between inner and outer windows.
  nsPIDOMWindowInner* MOZ_NON_OWNING_REF mInnerWindow;

  // A unique (as long as our 64-bit counter doesn't roll over) id for
  // this window.
  uint64_t mWindowID;

  uint32_t mMarkedCCGeneration;
};

#endif  // nsPIDOMWindow_h_
