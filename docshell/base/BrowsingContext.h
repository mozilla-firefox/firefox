/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BrowsingContext_h
#define mozilla_dom_BrowsingContext_h

#include <tuple>
#include "GVAutoplayRequestUtils.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HalScreenConfiguration.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/LocationBase.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/ScreenOrientationBinding.h"
#include "mozilla/dom/SyncedContext.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIDocShell.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"
#include "nsILoadInfo.h"
#include "nsILoadContext.h"
#include "nsThreadUtils.h"
#include "nsIDOMGeoPosition.h"

class nsDocShellLoadState;
class nsGeolocationService;
class nsGlobalWindowInner;
class nsGlobalWindowOuter;
class nsIPrincipal;
class nsOuterWindowProxy;
struct nsPoint;
class PickleIterator;

namespace IPC {
class Message;
class MessageReader;
class MessageWriter;
}  // namespace IPC

namespace mozilla {

class ErrorResult;
class LogModule;

namespace ipc {
class IProtocol;
class IPCResult;

template <typename T>
struct IPDLParamTraits;
}  // namespace ipc

namespace dom {
class BrowsingContent;
class BrowsingContextGroup;
class CanonicalBrowsingContext;
class ChildSHistory;
class ContentParent;
class Element;
struct LoadingSessionHistoryInfo;
class Location;
template <typename>
struct Nullable;
template <typename T>
class Sequence;
class SessionHistoryInfo;
class SessionStorageManager;
class StructuredCloneHolder;
class WindowContext;
class WindowGlobalChild;
struct WindowPostMessageOptions;
class WindowProxyHolder;

enum class ExplicitActiveStatus : uint8_t {
  None,
  Active,
  Inactive,
  EndGuard_,
};

struct EmbedderColorSchemes {
  PrefersColorSchemeOverride mUsed{};
  PrefersColorSchemeOverride mPreferred{};

  bool operator==(const EmbedderColorSchemes& aOther) const {
    return mUsed == aOther.mUsed && mPreferred == aOther.mPreferred;
  }

  bool operator!=(const EmbedderColorSchemes& aOther) const {
    return !(*this == aOther);
  }
};

// Fields are, by default, settable by any process and readable by any process.
// Racy sets will be resolved as-if they occurred in the order the parent
// process finds out about them.
//
// The `DidSet` and `CanSet` methods may be overloaded to provide different
// behavior for a specific field.
//  * `DidSet` is called to run code in every process whenever the value is
//    updated (This currently occurs even if the value didn't change, though
//    this may change in the future).
//  * `CanSet` is called before attempting to set the value, in both the process
//    which calls `Set`, and the parent process, and will kill the misbehaving
//    process if it fails.
#define MOZ_EACH_BC_FIELD(FIELD)                                              \
  FIELD(Name, nsString)                                                       \
  FIELD(Closed, bool)                                                         \
  FIELD(ExplicitActive, ExplicitActiveStatus)                                 \
  /* Top()-only. If true, new-playing media will be suspended when in an      \
   * inactive browsing context. */                                            \
  FIELD(SuspendMediaWhenInactive, bool)                                       \
  /* If true, we're within the nested event loop in window.open, and this     \
   * context may not be used as the target of a load */                       \
  FIELD(PendingInitialization, bool)                                          \
  /* Indicates if the browser window is active for the purpose of the         \
   * :-moz-window-inactive pseudoclass. Only read from or set on the          \
   * top BrowsingContext. */                                                  \
  FIELD(IsActiveBrowserWindowInternal, bool)                                  \
  FIELD(OpenerPolicy, nsILoadInfo::CrossOriginOpenerPolicy)                   \
  /* Current opener for the BrowsingContext. Weak reference */                \
  FIELD(OpenerId, uint64_t)                                                   \
  FIELD(OnePermittedSandboxedNavigatorId, uint64_t)                           \
  /* WindowID of the inner window which embeds this BC */                     \
  FIELD(EmbedderInnerWindowId, uint64_t)                                      \
  FIELD(CurrentInnerWindowId, uint64_t)                                       \
  FIELD(HadOriginalOpener, bool)                                              \
  /* Was this window created by a webpage through window.open or an anchor    \
   * link? In general, windows created this way may be manipulated (e.g.      \
   * closed, resized or moved) by content JS. */                              \
  FIELD(TopLevelCreatedByWebContent, bool)                                    \
  FIELD(IsPopupSpam, bool)                                                    \
  /* Hold the audio muted state and should be used on top level browsing      \
   * contexts only */                                                         \
  FIELD(Muted, bool)                                                          \
  /* Hold the pinned/app-tab state and should be used on top level browsing   \
   * contexts only */                                                         \
  FIELD(IsAppTab, bool)                                                       \
  /* Whether there's more than 1 tab / toplevel browsing context in this      \
   * parent window. Used to determine if a given BC is allowed to resize      \
   * and/or move the window or not. */                                        \
  FIELD(HasSiblings, bool)                                                    \
  /* Indicate that whether we should delay media playback, which would only   \
     be done on an unvisited tab. And this should only be used on the top     \
     level browsing contexts */                                               \
  FIELD(ShouldDelayMediaFromStart, bool)                                      \
  /* See nsSandboxFlags.h for the possible flags. */                          \
  FIELD(SandboxFlags, uint32_t)                                               \
  /* The value of SandboxFlags when the BrowsingContext is first created.     \
   * Used for sandboxing the initial about:blank document. */                 \
  FIELD(InitialSandboxFlags, uint32_t)                                        \
  /* A non-zero unique identifier for the browser element that is hosting     \
   * this                                                                     \
   * BrowsingContext tree. Every BrowsingContext in the element's tree will   \
   * return the same ID in all processes and it will remain stable            \
   * regardless of process changes. When a browser element's frameloader is   \
   * switched to another browser element this ID will remain the same but     \
   * hosted under the under the new browser element. */                       \
  FIELD(BrowserId, uint64_t)                                                  \
  FIELD(HistoryID, nsID)                                                      \
  FIELD(InRDMPane, bool)                                                      \
  FIELD(Loading, bool)                                                        \
  /* A field only set on top browsing contexts, which indicates that either:  \
   *                                                                          \
   *  * This is a browsing context created explicitly for printing or print   \
   *    preview (thus hosting static documents).                              \
   *                                                                          \
   *  * This is a browsing context where something in this tree is calling    \
   *    window.print() (and thus showing a modal dialog).                     \
   *                                                                          \
   * We use it exclusively to block navigation for both of these cases. */    \
  FIELD(IsPrinting, bool)                                                     \
  FIELD(AncestorLoading, bool)                                                \
  FIELD(AllowContentRetargeting, bool)                                        \
  FIELD(AllowContentRetargetingOnChildren, bool)                              \
  FIELD(ForceEnableTrackingProtection, bool)                                  \
  FIELD(UseGlobalHistory, bool)                                               \
  FIELD(TargetTopLevelLinkClicksToBlankInternal, bool)                        \
  FIELD(FullscreenAllowedByOwner, bool)                                       \
  FIELD(ForceDesktopViewport, bool)                                           \
  /*                                                                          \
   * "is popup" in the spec.                                                  \
   * Set only on top browsing contexts.                                       \
   * This doesn't indicate whether this is actually a popup or not,           \
   * but whether this browsing context is created by requesting popup or not. \
   * See also: nsWindowWatcher::ShouldOpenPopup.                              \
   */                                                                         \
  FIELD(IsPopupRequested, bool)                                               \
  /* These field are used to store the states of autoplay media request on    \
   * GeckoView only, and it would only be modified on the top level browsing  \
   * context. */                                                              \
  FIELD(GVAudibleAutoplayRequestStatus, GVAutoplayRequestStatus)              \
  FIELD(GVInaudibleAutoplayRequestStatus, GVAutoplayRequestStatus)            \
  /* ScreenOrientation-related APIs */                                        \
  FIELD(CurrentOrientationAngle, float)                                       \
  FIELD(CurrentOrientationType, mozilla::dom::OrientationType)                \
  FIELD(OrientationLock, mozilla::hal::ScreenOrientation)                     \
  FIELD(UserAgentOverride, nsString)                                          \
  FIELD(TouchEventsOverrideInternal, mozilla::dom::TouchEventsOverride)       \
  FIELD(EmbedderElementType, Maybe<nsString>)                                 \
  FIELD(MessageManagerGroup, nsString)                                        \
  FIELD(MaxTouchPointsOverride, uint8_t)                                      \
  FIELD(FullZoom, float)                                                      \
  FIELD(WatchedByDevToolsInternal, bool)                                      \
  FIELD(TextZoom, float)                                                      \
  FIELD(OverrideDPPX, float)                                                  \
  /* The current in-progress load. */                                         \
  FIELD(CurrentLoadIdentifier, Maybe<uint64_t>)                               \
  /* See nsIRequest for possible flags. */                                    \
  FIELD(DefaultLoadFlags, uint32_t)                                           \
  /* Signals that session history is enabled for this browsing context tree.  \
   * This is only ever set to true on the top BC, so consumers need to get    \
   * the value from the top BC! */                                            \
  FIELD(HasSessionHistory, bool)                                              \
  FIELD(UseErrorPages, bool)                                                  \
  FIELD(PlatformOverride, nsString)                                           \
  /* Specifies if this BC has loaded documents besides the initial            \
   * about:blank document. about:privatebrowsing, about:home, about:newtab    \
   * and non-initial about:blank are not considered to be initial             \
   * documents. */                                                            \
  FIELD(HasLoadedNonInitialDocument, bool)                                    \
  /* Default value for nsIDocumentViewer::authorStyleDisabled in any new      \
   * browsing contexts created as a descendant of this one.  Valid only for   \
   * top BCs. */                                                              \
  FIELD(AuthorStyleDisabledDefault, bool)                                     \
  FIELD(ServiceWorkersTestingEnabled, bool)                                   \
  FIELD(MediumOverride, nsString)                                             \
  /* DevTools override for prefers-color-scheme */                            \
  FIELD(PrefersColorSchemeOverride, dom::PrefersColorSchemeOverride)          \
  /* DevTools override for forced-colors */                                   \
  FIELD(ForcedColorsOverride, dom::ForcedColorsOverride)                      \
  /* prefers-color-scheme override based on the color-scheme style of our     \
   * <browser> embedder element. */                                           \
  FIELD(EmbedderColorSchemes, EmbedderColorSchemes)                           \
  FIELD(DisplayMode, dom::DisplayMode)                                        \
  /* The number of entries added to the session history because of this       \
   * browsing context. */                                                     \
  FIELD(HistoryEntryCount, uint32_t)                                          \
  /* Don't use the getter of the field, but IsInBFCache() method */           \
  FIELD(IsInBFCache, bool)                                                    \
  FIELD(HasRestoreData, bool)                                                 \
  FIELD(SessionStoreEpoch, uint32_t)                                          \
  /* Whether we can execute scripts in this BrowsingContext. Has no effect    \
   * unless scripts are also allowed in the parent WindowContext. */          \
  FIELD(AllowJavascript, bool)                                                \
  /* The count of request that are used to prevent the browsing context tree  \
   * from being suspended, which would ONLY be modified on the top level      \
   * context in the chrome process because that's a non-atomic counter */     \
  FIELD(PageAwakeRequestCount, uint32_t)                                      \
  /* This field only gets incrememented when we start navigations in the      \
   * parent process. This is used for keeping track of the racing navigations \
   * between the parent and content processes. */                             \
  FIELD(ParentInitiatedNavigationEpoch, uint64_t)                             \
  /* This browsing context is for a synthetic image document wrapping an      \
   * image embedded in <object> or <embed>. */                                \
  FIELD(IsSyntheticDocumentContainer, bool)                                   \
  /* If true, this document is embedded within a content document,  either    \
   * loaded in the parent (e.g. about:addons or the devtools toolbox), or in  \
   * a content process. */                                                    \
  FIELD(EmbeddedInContentDocument, bool)                                      \
  /* If true, this browsing context is within a hidden embedded document. */  \
  FIELD(IsUnderHiddenEmbedderElement, bool)                                   \
  /* If true, this browsing context is offline */                             \
  FIELD(ForceOffline, bool)                                                   \
  /* Used to propagate window.top's inner size for RFPTarget::Window*         \
   * protections */                                                           \
  FIELD(TopInnerSizeForRFP, CSSIntSize)

// BrowsingContext, in this context, is the cross process replicated
// environment in which information about documents is stored. In
// particular the tree structure of nested browsing contexts is
// represented by the tree of BrowsingContexts.
//
// The tree of BrowsingContexts is created in step with its
// corresponding nsDocShell, and when nsDocShells are connected
// through a parent/child relationship, so are BrowsingContexts. The
// major difference is that BrowsingContexts are replicated (synced)
// to the parent process, making it possible to traverse the
// BrowsingContext tree for a tab, in both the parent and the child
// process.
//
// Trees of BrowsingContexts should only ever contain nodes of the
// same BrowsingContext::Type. This is enforced by asserts in the
// BrowsingContext::Create* methods.
class BrowsingContext : public nsILoadContext, public nsWrapperCache {
  MOZ_DECL_SYNCED_CONTEXT(BrowsingContext, MOZ_EACH_BC_FIELD)

 public:
  enum class Type { Chrome, Content };

  static void Init();
  static LogModule* GetLog();
  static LogModule* GetSyncLog();

  // Look up a BrowsingContext in the current process by ID.
  static already_AddRefed<BrowsingContext> Get(uint64_t aId);
  static already_AddRefed<BrowsingContext> Get(GlobalObject&, uint64_t aId) {
    return Get(aId);
  }
  // Look up the top-level BrowsingContext by BrowserID.
  static already_AddRefed<BrowsingContext> GetCurrentTopByBrowserId(
      uint64_t aBrowserId);
  static already_AddRefed<BrowsingContext> GetCurrentTopByBrowserId(
      GlobalObject&, uint64_t aId) {
    return GetCurrentTopByBrowserId(aId);
  }

  static void UpdateCurrentTopByBrowserId(BrowsingContext* aNewBrowsingContext);

  static already_AddRefed<BrowsingContext> GetFromWindow(
      WindowProxyHolder& aProxy);
  static already_AddRefed<BrowsingContext> GetFromWindow(
      GlobalObject&, WindowProxyHolder& aProxy) {
    return GetFromWindow(aProxy);
  }

  static void DiscardFromContentParent(ContentParent* aCP);

  // Create a brand-new toplevel BrowsingContext with no relationships to other
  // BrowsingContexts, and which is not embedded within any <browser> or frame
  // element.
  //
  // This BrowsingContext is immediately attached, and cannot have LoadContext
  // flags customized unless it is of `Type::Chrome`.
  //
  // The process which created this BrowsingContext is responsible for detaching
  // it.
  static already_AddRefed<BrowsingContext> CreateIndependent(Type aType,
                                                             bool aWindowless);

  // Options which can be passed to CreateDetached.
  struct CreateDetachedOptions {
    bool isPopupRequested = false;
    bool createdDynamically = false;
    bool topLevelCreatedByWebContent = false;
    bool isForPrinting = false;
    bool windowless = false;
  };

  // Create a brand-new BrowsingContext object, but does not immediately attach
  // it. State such as OriginAttributes and PrivateBrowsingId may be customized
  // to configure the BrowsingContext before it is attached.
  //
  // `EnsureAttached()` must be called before the BrowsingContext is used for a
  // DocShell, BrowserParent, or BrowserBridgeChild.
  static already_AddRefed<BrowsingContext> CreateDetached(
      nsGlobalWindowInner* aParent, BrowsingContext* aOpener,
      BrowsingContextGroup* aSpecificGroup, const nsAString& aName, Type aType,
      CreateDetachedOptions aOptions);

  void EnsureAttached();

  bool EverAttached() const { return mEverAttached; }

  // Cast this object to a canonical browsing context, and return it.
  CanonicalBrowsingContext* Canonical();

  // Is the most recent Document in this BrowsingContext loaded within this
  // process? This may be true with a null mDocShell after the Window has been
  // closed.
  bool IsInProcess() const { return mIsInProcess; }

  bool IsOwnedByProcess() const;

  bool CanHaveRemoteOuterProxies() const {
    return !mIsInProcess || mDanglingRemoteOuterProxies;
  }

  // Has this BrowsingContext been discarded. A discarded browsing context has
  // been destroyed, and may not be available on the other side of an IPC
  // message.
  bool IsDiscarded() const { return mIsDiscarded; }

  // Returns true if none of the BrowsingContext's ancestor BrowsingContexts or
  // WindowContexts are discarded or cached.
  bool AncestorsAreCurrent() const;

  bool Windowless() const { return mWindowless; }

  // Get the DocShell for this BrowsingContext if it is in-process, or
  // null if it's not.
  nsIDocShell* GetDocShell() const { return mDocShell; }
  void SetDocShell(nsIDocShell* aDocShell);
  void ClearDocShell() { mDocShell = nullptr; }

  // Get the Document for this BrowsingContext if it is in-process, or
  // null if it's not.
  Document* GetDocument() const {
    return mDocShell ? mDocShell->GetDocument() : nullptr;
  }
  Document* GetExtantDocument() const {
    return mDocShell ? mDocShell->GetExtantDocument() : nullptr;
  }

  // This cleans up remote outer window proxies that might have been left behind
  // when the browsing context went from being remote to local. It does this by
  // turning them into cross-compartment wrappers to aOuter. If there is already
  // a remote proxy in the compartment of aOuter, then aOuter will get swapped
  // to it and the value of aOuter will be set to the object that used to be the
  // remote proxy and is now an OuterWindowProxy.
  void CleanUpDanglingRemoteOuterWindowProxies(
      JSContext* aCx, JS::MutableHandle<JSObject*> aOuter);

  // Get the embedder element for this BrowsingContext if the embedder is
  // in-process, or null if it's not.
  Element* GetEmbedderElement() const { return mEmbedderElement; }
  void SetEmbedderElement(Element* aEmbedder);

  // Return true if the type of the embedder element is either object
  // or embed, false otherwise.
  bool IsEmbedderTypeObjectOrEmbed();

  // Called after the BrowingContext has been embedded in a FrameLoader. This
  // happens after `SetEmbedderElement` is called on the BrowsingContext and
  // after the BrowsingContext has been set on the FrameLoader.
  void Embed();

  // Get the outer window object for this BrowsingContext if it is in-process
  // and still has a docshell, or null otherwise.
  nsPIDOMWindowOuter* GetDOMWindow() const {
    return mDocShell ? mDocShell->GetWindow() : nullptr;
  }

  uint64_t GetRequestContextId() const { return mRequestContextId; }

  // Detach the current BrowsingContext from its parent, in both the
  // child and the parent process.
  void Detach(bool aFromIPC = false);

  // Prepare this BrowsingContext to leave the current process.
  void PrepareForProcessChange();

  // Triggers a load in the process which currently owns this BrowsingContext.
  nsresult LoadURI(nsDocShellLoadState* aLoadState,
                   bool aSetNavigating = false);

  nsresult InternalLoad(nsDocShellLoadState* aLoadState);

  // Removes the root document for this BrowsingContext tree from the BFCache,
  // if it is cached, and returns true if it was.
  bool RemoveRootFromBFCacheSync();

  // If the load state includes a source BrowsingContext has been passed, check
  // to see if we are sandboxed from it as the result of an iframe or CSP
  // sandbox.
  nsresult CheckSandboxFlags(nsDocShellLoadState* aLoadState);

  void DisplayLoadError(const nsAString& aURI);

  // Check that this browsing context is targetable for navigations (i.e. that
  // it is neither closed, cached, nor discarded).
  bool IsTargetable() const;

  // True if this browsing context is inactive and is able to be suspended.
  bool InactiveForSuspend() const;

  const nsString& Name() const { return GetName(); }
  void GetName(nsAString& aName) { aName = GetName(); }
  bool NameEquals(const nsAString& aName) { return GetName().Equals(aName); }

  Type GetType() const { return mType; }
  bool IsContent() const { return mType == Type::Content; }
  bool IsChrome() const { return !IsContent(); }

  bool IsTop() const { return !GetParent(); }
  bool IsSubframe() const { return !IsTop(); }

  bool IsTopContent() const { return IsContent() && IsTop(); }

  bool IsInSubtreeOf(BrowsingContext* aContext);

  bool IsContentSubframe() const { return IsContent() && IsSubframe(); }

  RefPtr<nsGeolocationService> GetGeolocationServiceOverride();

  // non-zero
  uint64_t Id() const { return mBrowsingContextId; }

  BrowsingContext* GetParent() const;
  BrowsingContext* Top();
  const BrowsingContext* Top() const;

  int32_t IndexOf(BrowsingContext* aChild);

  // NOTE: Unlike `GetEmbedderWindowGlobal`, `GetParentWindowContext` does not
  // cross toplevel content browser boundaries.
  WindowContext* GetParentWindowContext() const { return mParentWindow; }
  WindowContext* GetTopWindowContext() const;

  already_AddRefed<BrowsingContext> GetOpener() const {
    RefPtr<BrowsingContext> opener(Get(GetOpenerId()));
    if (!mIsDiscarded && opener && !opener->mIsDiscarded) {
      MOZ_DIAGNOSTIC_ASSERT(opener->mType == mType);
      return opener.forget();
    }
    return nullptr;
  }
  void SetOpener(BrowsingContext* aOpener);
  bool HasOpener() const;

  bool HadOriginalOpener() const { return GetHadOriginalOpener(); }

  // Returns true if the browsing context and top context are same origin
  bool SameOriginWithTop();

  /**
   * When a new browsing context is opened by a sandboxed document, it needs to
   * keep track of the browsing context that opened it, so that it can be
   * navigated by it.  This is the "one permitted sandboxed navigator".
   */
  already_AddRefed<BrowsingContext> GetOnePermittedSandboxedNavigator() const {
    return Get(GetOnePermittedSandboxedNavigatorId());
  }
  [[nodiscard]] nsresult SetOnePermittedSandboxedNavigator(
      BrowsingContext* aNavigator) {
    if (GetOnePermittedSandboxedNavigatorId()) {
      MOZ_ASSERT(false,
                 "One Permitted Sandboxed Navigator should only be set once.");
      return NS_ERROR_FAILURE;
    } else {
      return SetOnePermittedSandboxedNavigatorId(aNavigator ? aNavigator->Id()
                                                            : 0);
    }
  }

  uint32_t SandboxFlags() const { return GetSandboxFlags(); }

  Span<RefPtr<BrowsingContext>> Children() const;
  void GetChildren(nsTArray<RefPtr<BrowsingContext>>& aChildren);

  Span<RefPtr<BrowsingContext>> NonSyntheticChildren() const;

  BrowsingContext* NonSyntheticLightDOMChildAt(uint32_t aIndex) const;
  uint32_t NonSyntheticLightDOMChildrenCount() const;

  const nsTArray<RefPtr<WindowContext>>& GetWindowContexts() {
    return mWindowContexts;
  }
  void GetWindowContexts(nsTArray<RefPtr<WindowContext>>& aWindows);

  void RegisterWindowContext(WindowContext* aWindow);
  void UnregisterWindowContext(WindowContext* aWindow);
  WindowContext* GetCurrentWindowContext() const {
    return mCurrentWindowContext;
  }

  // Helpers to traverse this BrowsingContext subtree. Note that these will only
  // traverse active contexts, and will ignore ones in the BFCache.
  enum class WalkFlag {
    Next,
    Skip,
    Stop,
  };

  /**
   * Walk the browsing context tree in pre-order and call `aCallback`
   * for every node in the tree. PreOrderWalk accepts two types of
   * callbacks, either of the type `void(BrowsingContext*)` or
   * `WalkFlag(BrowsingContext*)`. The former traverses the entire
   * tree, but the latter let's you control if a sub-tree should be
   * skipped by returning `WalkFlag::Skip`, completely abort traversal
   * by returning `WalkFlag::Stop` or continue as normal with
   * `WalkFlag::Next`.
   */
  template <typename F>
  void PreOrderWalk(F&& aCallback) {
    if constexpr (std::is_void_v<
                      typename std::invoke_result_t<F, BrowsingContext*>>) {
      PreOrderWalkVoid(std::forward<F>(aCallback));
    } else {
      PreOrderWalkFlag(std::forward<F>(aCallback));
    }
  }

  void PreOrderWalkVoid(const std::function<void(BrowsingContext*)>& aCallback);
  WalkFlag PreOrderWalkFlag(
      const std::function<WalkFlag(BrowsingContext*)>& aCallback);

  void PostOrderWalk(const std::function<void(BrowsingContext*)>& aCallback);

  void GetAllBrowsingContextsInSubtree(
      nsTArray<RefPtr<BrowsingContext>>& aBrowsingContexts);

  BrowsingContextGroup* Group() { return mGroup; }

  // WebIDL bindings for nsILoadContext
  Nullable<WindowProxyHolder> GetAssociatedWindow();
  Nullable<WindowProxyHolder> GetTopWindow();
  Element* GetTopFrameElement();
  bool GetIsContent() { return IsContent(); }
  void SetUsePrivateBrowsing(bool aUsePrivateBrowsing, ErrorResult& aError);
  // Needs a different name to disambiguate from the xpidl method with
  // the same signature but different return value.
  void SetUseTrackingProtectionWebIDL(bool aUseTrackingProtection,
                                      ErrorResult& aRv);
  bool UseTrackingProtectionWebIDL() { return UseTrackingProtection(); }
  void GetOriginAttributes(JSContext* aCx, JS::MutableHandle<JS::Value> aVal,
                           ErrorResult& aError);

  bool InRDMPane() const { return GetInRDMPane(); }

  bool WatchedByDevTools();
  void SetWatchedByDevTools(bool aWatchedByDevTools, ErrorResult& aRv);

  void SetGeolocationServiceOverride(
      const Optional<nsIDOMGeoPosition*>& aGeolocationOverride);

  dom::TouchEventsOverride TouchEventsOverride() const;
  bool TargetTopLevelLinkClicksToBlank() const;

  bool FullscreenAllowed() const;

  float FullZoom() const { return GetFullZoom(); }
  float TextZoom() const { return GetTextZoom(); }

  float OverrideDPPX() const { return Top()->GetOverrideDPPX(); }

  bool SuspendMediaWhenInactive() const {
    return GetSuspendMediaWhenInactive();
  }

  bool IsActive() const;
  bool ForceOffline() const { return GetForceOffline(); }

  bool ForceDesktopViewport() const { return GetForceDesktopViewport(); }

  bool AuthorStyleDisabledDefault() const {
    return GetAuthorStyleDisabledDefault();
  }

  bool UseGlobalHistory() const { return GetUseGlobalHistory(); }

  bool GetIsActiveBrowserWindow();

  void SetIsActiveBrowserWindow(bool aActive);

  uint64_t BrowserId() const { return GetBrowserId(); }

  bool IsLoading();

  void GetEmbedderElementType(nsString& aElementType) {
    if (GetEmbedderElementType().isSome()) {
      aElementType = GetEmbedderElementType().value();
    }
  }

  bool IsLoadingIdentifier(uint64_t aLoadIdentifer) {
    if (GetCurrentLoadIdentifier() &&
        *GetCurrentLoadIdentifier() == aLoadIdentifer) {
      return true;
    }
    return false;
  }

  // ScreenOrientation related APIs
  [[nodiscard]] nsresult SetCurrentOrientation(OrientationType aType,
                                               float aAngle) {
    Transaction txn;
    txn.SetCurrentOrientationType(aType);
    txn.SetCurrentOrientationAngle(aAngle);
    return txn.Commit(this);
  }

  void SetRDMPaneOrientation(OrientationType aType, float aAngle,
                             ErrorResult& aRv) {
    if (InRDMPane()) {
      if (NS_FAILED(SetCurrentOrientation(aType, aAngle))) {
        aRv.ThrowInvalidStateError("Browsing context is discarded");
      }
    }
  }

  void SetRDMPaneMaxTouchPoints(uint8_t aMaxTouchPoints, ErrorResult& aRv) {
    if (InRDMPane()) {
      SetMaxTouchPointsOverride(aMaxTouchPoints, aRv);
    }
  }

  // Find a browsing context in this context's list of
  // children. Doesn't consider the special names, '_self', '_parent',
  // '_top', or '_blank'. Performs access control checks with regard to
  // 'this'.
  BrowsingContext* FindChildWithName(const nsAString& aName,
                                     WindowGlobalChild& aRequestingWindow);

  // Find a browsing context in the subtree rooted at 'this' Doesn't
  // consider the special names, '_self', '_parent', '_top', or
  // '_blank'.
  //
  // If passed, performs access control checks with regard to
  // 'aRequestingContext', otherwise performs no access checks.
  BrowsingContext* FindWithNameInSubtree(const nsAString& aName,
                                         WindowGlobalChild* aRequestingWindow);

  // Find the special browsing context if aName is '_self', '_parent',
  // '_top', but not '_blank'. The latter is handled in FindWithName
  BrowsingContext* FindWithSpecialName(const nsAString& aName,
                                       WindowGlobalChild& aRequestingWindow);

  nsISupports* GetParentObject() const;
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // Return the window proxy object that corresponds to this browsing context.
  inline JSObject* GetWindowProxy() const { return mWindowProxy; }
  inline JSObject* GetUnbarrieredWindowProxy() const {
    return mWindowProxy.unbarrieredGet();
  }

  // Set the window proxy object that corresponds to this browsing context.
  void SetWindowProxy(JS::Handle<JSObject*> aWindowProxy) {
    mWindowProxy = aWindowProxy;
  }

  Nullable<WindowProxyHolder> GetWindow();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS(BrowsingContext)
  NS_DECL_NSILOADCONTEXT

  // Window APIs that are cross-origin-accessible (from the HTML spec).
  WindowProxyHolder Window();
  BrowsingContext* GetBrowsingContext() { return this; };
  BrowsingContext* Self() { return this; }
  void Location(JSContext* aCx, JS::MutableHandle<JSObject*> aLocation,
                ErrorResult& aError);
  void Close(CallerType aCallerType, ErrorResult& aError);
  bool GetClosed(ErrorResult&) { return GetClosed(); }
  void Focus(CallerType aCallerType, ErrorResult& aError);
  void Blur(CallerType aCallerType, ErrorResult& aError);
  WindowProxyHolder GetFrames(ErrorResult& aError);
  int32_t Length() const { return Children().Length(); }
  Nullable<WindowProxyHolder> GetTop(ErrorResult& aError);
  void GetOpener(JSContext* aCx, JS::MutableHandle<JS::Value> aOpener,
                 ErrorResult& aError) const;
  Nullable<WindowProxyHolder> GetParent(ErrorResult& aError);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const nsAString& aTargetOrigin,
                      const Sequence<JSObject*>& aTransfer,
                      nsIPrincipal& aSubjectPrincipal, ErrorResult& aError);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const WindowPostMessageOptions& aOptions,
                      nsIPrincipal& aSubjectPrincipal, ErrorResult& aError);

  void GetCustomUserAgent(nsAString& aUserAgent) {
    aUserAgent = Top()->GetUserAgentOverride();
  }
  nsresult SetCustomUserAgent(const nsAString& aUserAgent);
  void SetCustomUserAgent(const nsAString& aUserAgent, ErrorResult& aRv);

  void GetCustomPlatform(nsAString& aPlatform) {
    aPlatform = Top()->GetPlatformOverride();
  }
  void SetCustomPlatform(const nsAString& aPlatform, ErrorResult& aRv);

  JSObject* WrapObject(JSContext* aCx);

  static JSObject* ReadStructuredClone(JSContext* aCx,
                                       JSStructuredCloneReader* aReader,
                                       StructuredCloneHolder* aHolder);
  bool WriteStructuredClone(JSContext* aCx, JSStructuredCloneWriter* aWriter,
                            StructuredCloneHolder* aHolder);

  void StartDelayedAutoplayMediaComponents();

  [[nodiscard]] nsresult ResetGVAutoplayRequestStatus();

  /**
   * Information required to initialize a BrowsingContext in another process.
   * This object may be serialized over IPC.
   */
  struct IPCInitializer {
    uint64_t mId = 0;

    // IDs are used for Parent and Opener to allow for this object to be
    // deserialized before other BrowsingContext in the BrowsingContextGroup
    // have been initialized.
    uint64_t mParentId = 0;
    already_AddRefed<WindowContext> GetParent();
    already_AddRefed<BrowsingContext> GetOpener();

    uint64_t GetOpenerId() const { return mFields.Get<IDX_OpenerId>(); }

    bool mWindowless = false;
    bool mUseRemoteTabs = false;
    bool mUseRemoteSubframes = false;
    bool mCreatedDynamically = false;
    int32_t mChildOffset = 0;
    int32_t mSessionHistoryIndex = -1;
    int32_t mSessionHistoryCount = 0;
    OriginAttributes mOriginAttributes;
    uint64_t mRequestContextId = 0;

    FieldValues mFields;
  };

  // Create an IPCInitializer object for this BrowsingContext.
  IPCInitializer GetIPCInitializer();

  // Create a BrowsingContext object from over IPC.
  static mozilla::ipc::IPCResult CreateFromIPC(IPCInitializer&& aInitializer,
                                               BrowsingContextGroup* aGroup,
                                               ContentParent* aOriginProcess);

  bool IsSandboxedFrom(BrowsingContext* aTarget);

  // The runnable will be called once there is idle time, or the top level
  // page has been loaded or if a timeout has fired.
  // Must be called only on the top level BrowsingContext.
  void AddDeprioritizedLoadRunner(nsIRunnable* aRunner);

  RefPtr<SessionStorageManager> GetSessionStorageManager();

  // Set PendingInitialization on this BrowsingContext before the context has
  // been attached.
  void InitPendingInitialization(bool aPendingInitialization) {
    MOZ_ASSERT(!EverAttached());
    mFields.SetWithoutSyncing<IDX_PendingInitialization>(
        aPendingInitialization);
  }

  bool CreatedDynamically() const { return mCreatedDynamically; }

  // Returns true if this browsing context, or any ancestor to this browsing
  // context was created dynamically. See also `CreatedDynamically`.
  bool IsDynamic() const;

  int32_t ChildOffset() const { return mChildOffset; }

  bool GetOffsetPath(nsTArray<uint32_t>& aPath) const;

  const OriginAttributes& OriginAttributesRef() { return mOriginAttributes; }
  nsresult SetOriginAttributes(const OriginAttributes& aAttrs);

  void GetHistoryID(JSContext* aCx, JS::MutableHandle<JS::Value> aVal,
                    ErrorResult& aError);

  // This should only be called on the top browsing context.
  void InitSessionHistory();

  // This will only ever return a non-null value if called on the top browsing
  // context.
  ChildSHistory* GetChildSessionHistory();

  bool CrossOriginIsolated();

  // Check if it is allowed to open a popup from the current browsing
  // context or any of its ancestors.
  bool IsPopupAllowed();

  // aCurrentURI is only required to be non-null if the load type contains the
  // nsIWebNavigation::LOAD_FLAGS_IS_REFRESH flag and aInfo is for a refresh to
  // the current URI.
  void SessionHistoryCommit(const LoadingSessionHistoryInfo& aInfo,
                            uint32_t aLoadType, nsIURI* aCurrentURI,
                            SessionHistoryInfo* aPreviousActiveEntry,
                            bool aPersist, bool aCloneEntryChildren,
                            bool aChannelExpired, uint32_t aCacheKey);

  // Set a new active entry on this browsing context. This is used for
  // implementing history.pushState/replaceState and same document navigations.
  // The new active entry will be linked to the current active entry through
  // its shared state.
  // aPreviousScrollPos is the scroll position that needs to be saved on the
  // previous active entry.
  // aUpdatedCacheKey is the cache key to set on the new active entry. If
  // aUpdatedCacheKey is 0 then it will be ignored.
  void SetActiveSessionHistoryEntry(const Maybe<nsPoint>& aPreviousScrollPos,
                                    SessionHistoryInfo* aInfo,
                                    uint32_t aLoadType,
                                    uint32_t aUpdatedCacheKey,
                                    bool aUpdateLength = true);

  // Replace the active entry for this browsing context. This is used for
  // implementing history.replaceState and same document navigations.
  void ReplaceActiveSessionHistoryEntry(SessionHistoryInfo* aInfo);

  // Removes dynamic child entries of the active entry.
  void RemoveDynEntriesFromActiveSessionHistoryEntry();

  // Removes entries corresponding to this BrowsingContext from session history.
  void RemoveFromSessionHistory(const nsID& aChangeID);

  void SetTriggeringAndInheritPrincipals(nsIPrincipal* aTriggeringPrincipal,
                                         nsIPrincipal* aPrincipalToInherit,
                                         uint64_t aLoadIdentifier);

  // Return mTriggeringPrincipal and mPrincipalToInherit if the load id
  // saved with the principal matches the current load identifier of this BC.
  std::tuple<nsCOMPtr<nsIPrincipal>, nsCOMPtr<nsIPrincipal>>
  GetTriggeringAndInheritPrincipalsForCurrentLoad();

  MOZ_CAN_RUN_SCRIPT
  void HistoryGo(int32_t aOffset, uint64_t aHistoryEpoch,
                 bool aRequireUserInteraction, bool aUserActivation,
                 std::function<void(Maybe<int32_t>&&)>&& aResolver);

  bool ShouldUpdateSessionHistory(uint32_t aLoadType);

  // Checks if we reached the rate limit for calls to Location and History API.
  // The rate limit is controlled by the
  // "dom.navigation.navigationRateLimit" prefs.
  // Rate limit applies per BrowsingContext.
  // Returns NS_OK if we are below the rate limit and increments the counter.
  // Returns NS_ERROR_DOM_SECURITY_ERR if limit is reached.
  nsresult CheckNavigationRateLimit(CallerType aCallerType);

  void ResetNavigationRateLimit();

  mozilla::dom::DisplayMode DisplayMode() { return Top()->GetDisplayMode(); }

  // Returns canFocus, isActive
  std::tuple<bool, bool> CanFocusCheck(CallerType aCallerType);

  bool CanBlurCheck(CallerType aCallerType);

  // Examine the current document state to see if we're in a way that is
  // typically abused by web designers. The window.open code uses this
  // routine to determine whether to allow the new window.
  // Returns a value from the PopupControlState enum.
  PopupBlocker::PopupControlState RevisePopupAbuseLevel(
      PopupBlocker::PopupControlState aControl);

  // Get the modifiers associated with the user activation for relevant
  // documents. The window.open code uses this routine to determine where the
  // new window should be located.
  void GetUserActivationModifiersForPopup(
      UserActivation::Modifiers* aModifiers);

  void IncrementHistoryEntryCountForBrowsingContext();

  bool ServiceWorkersTestingEnabled() const {
    return GetServiceWorkersTestingEnabled();
  }

  void GetMediumOverride(nsAString& aOverride) const {
    aOverride = GetMediumOverride();
  }

  dom::PrefersColorSchemeOverride PrefersColorSchemeOverride() const {
    return GetPrefersColorSchemeOverride();
  }

  dom::ForcedColorsOverride ForcedColorsOverride() const {
    return GetForcedColorsOverride();
  }

  bool IsInBFCache() const;

  bool AllowJavascript() const { return GetAllowJavascript(); }
  bool CanExecuteScripts() const { return mCanExecuteScripts; }

  uint32_t DefaultLoadFlags() const { return GetDefaultLoadFlags(); }

  // When request for page awake, it would increase a count that is used to
  // prevent whole browsing context tree from being suspended. The request can
  // be called multiple times. When calling the revoke, it would decrease the
  // count and once the count reaches to zero, the browsing context tree could
  // be suspended when the tree is inactive.
  void RequestForPageAwake();
  void RevokeForPageAwake();

  void AddDiscardListener(std::function<void(uint64_t)>&& aListener);

  bool IsAppTab() { return GetIsAppTab(); }
  bool HasSiblings() { return GetHasSiblings(); }

  bool IsUnderHiddenEmbedderElement() const {
    return GetIsUnderHiddenEmbedderElement();
  }

  void LocationCreated(dom::Location* aLocation);
  void ClearCachedValuesOfLocations();

  void GetContiguousHistoryEntries(SessionHistoryInfo& aActiveEntry,
                                   Navigation* aNavigation);

 protected:
  virtual ~BrowsingContext();
  BrowsingContext(WindowContext* aParentWindow, BrowsingContextGroup* aGroup,
                  uint64_t aBrowsingContextId, Type aType, FieldValues&& aInit);

  void SetChildSHistory(ChildSHistory* aChildSHistory);
  already_AddRefed<ChildSHistory> ForgetChildSHistory() {
    // FIXME Do we need to unset mHasSessionHistory?
    return mChildSessionHistory.forget();
  }

  static bool ShouldAddEntryForRefresh(nsIURI* aCurrentURI,
                                       const SessionHistoryInfo& aInfo);
  static bool ShouldAddEntryForRefresh(nsIURI* aCurrentURI, nsIURI* aNewURI,
                                       bool aHasPostData);

 private:
  // Assert that this BrowsingContext is coherent relative to related
  // BrowsingContexts. This will be run before the BrowsingContext is attached.
  //
  // A non-null string return value indicates that there was a coherency check
  // failure, which will be handled with either a crash or IPC failure.
  //
  // If provided, `aOriginProcess` is the process which is responsible for the
  // creation of this BrowsingContext.
  [[nodiscard]] const char* BrowsingContextCoherencyChecks(
      ContentParent* aOriginProcess);

  void Attach(bool aFromIPC, ContentParent* aOriginProcess);

  // Recomputes whether we can execute scripts in this BrowsingContext based on
  // the value of AllowJavascript() and whether scripts are allowed in the
  // parent WindowContext. Called whenever the AllowJavascript() flag or the
  // parent WC changes.
  void RecomputeCanExecuteScripts();

  // Is it early enough in the BrowsingContext's lifecycle that it is still
  // OK to set OriginAttributes?
  bool CanSetOriginAttributes();

  void AssertOriginAttributesMatchPrivateBrowsing();

  friend class ::nsOuterWindowProxy;
  friend class ::nsGlobalWindowOuter;
  friend class WindowContext;

  // Update the window proxy object that corresponds to this browsing context.
  // This should be called from the window proxy object's objectMoved hook, if
  // the object mWindowProxy points to was moved by the JS GC.
  void UpdateWindowProxy(JSObject* obj, JSObject* old) {
    if (mWindowProxy) {
      MOZ_ASSERT(mWindowProxy == old);
      mWindowProxy = obj;
    }
  }
  // Clear the window proxy object that corresponds to this browsing context.
  // This should be called if the window proxy object is finalized, or it can't
  // reach its browsing context anymore.
  void ClearWindowProxy() { mWindowProxy = nullptr; }

  friend class Location;
  friend class RemoteLocationProxy;
  /**
   * LocationProxy is the class for the native object stored as a private in a
   * RemoteLocationProxy proxy representing a Location object in a different
   * process. It forwards all operations to its BrowsingContext and aggregates
   * its refcount to that BrowsingContext.
   */
  class LocationProxy final : public LocationBase {
   public:
    MozExternalRefCountType AddRef() { return GetBrowsingContext()->AddRef(); }
    MozExternalRefCountType Release() {
      return GetBrowsingContext()->Release();
    }

   protected:
    friend class RemoteLocationProxy;
    BrowsingContext* GetBrowsingContext() override {
      return reinterpret_cast<BrowsingContext*>(
          uintptr_t(this) - offsetof(BrowsingContext, mLocation));
    }

    nsIDocShell* GetDocShell() override { return nullptr; }
  };

  // Send a given `BaseTransaction` object to the correct remote.
  void SendCommitTransaction(ContentParent* aParent,
                             const BaseTransaction& aTxn, uint64_t aEpoch);
  void SendCommitTransaction(ContentChild* aChild, const BaseTransaction& aTxn,
                             uint64_t aEpoch);

  bool CanSet(FieldIndex<IDX_SessionStoreEpoch>, uint32_t aEpoch,
              ContentParent* aSource) {
    return IsTop() && !aSource;
  }

  void DidSet(FieldIndex<IDX_SessionStoreEpoch>, uint32_t aOldValue);

  using CanSetResult = syncedcontext::CanSetResult;

  // Ensure that opener is in the same BrowsingContextGroup.
  bool CanSet(FieldIndex<IDX_OpenerId>, const uint64_t& aValue,
              ContentParent* aSource) {
    if (aValue != 0) {
      RefPtr<BrowsingContext> opener = Get(aValue);
      return opener && opener->Group() == Group();
    }
    return true;
  }

  bool CanSet(FieldIndex<IDX_OpenerPolicy>,
              nsILoadInfo::CrossOriginOpenerPolicy, ContentParent*);

  bool CanSet(FieldIndex<IDX_ServiceWorkersTestingEnabled>, bool,
              ContentParent*) {
    return IsTop();
  }

  bool CanSet(FieldIndex<IDX_MediumOverride>, const nsString&, ContentParent*) {
    return IsTop();
  }

  bool CanSet(FieldIndex<IDX_EmbedderColorSchemes>, const EmbedderColorSchemes&,
              ContentParent* aSource) {
    return CheckOnlyEmbedderCanSet(aSource);
  }

  bool CanSet(FieldIndex<IDX_PrefersColorSchemeOverride>,
              dom::PrefersColorSchemeOverride, ContentParent*) {
    return IsTop();
  }

  bool CanSet(FieldIndex<IDX_ForcedColorsOverride>, dom::ForcedColorsOverride,
              ContentParent*) {
    return IsTop();
  }

  void DidSet(FieldIndex<IDX_InRDMPane>, bool aOldValue);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DidSet(FieldIndex<IDX_ForceDesktopViewport>,
                                          bool aOldValue);

  void DidSet(FieldIndex<IDX_EmbedderColorSchemes>,
              EmbedderColorSchemes&& aOldValue);

  void DidSet(FieldIndex<IDX_PrefersColorSchemeOverride>,
              dom::PrefersColorSchemeOverride aOldValue);

  void DidSet(FieldIndex<IDX_ForcedColorsOverride>,
              dom::ForcedColorsOverride aOldValue);

  template <typename Callback>
  void WalkPresContexts(Callback&&);
  void PresContextAffectingFieldChanged();

  void DidSet(FieldIndex<IDX_MediumOverride>, nsString&& aOldValue);

  bool CanSet(FieldIndex<IDX_SuspendMediaWhenInactive>, bool, ContentParent*) {
    return IsTop();
  }

  bool CanSet(FieldIndex<IDX_TouchEventsOverrideInternal>,
              dom::TouchEventsOverride aTouchEventsOverride,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_TouchEventsOverrideInternal>,
              dom::TouchEventsOverride&& aOldValue);

  bool CanSet(FieldIndex<IDX_DisplayMode>, const enum DisplayMode& aDisplayMode,
              ContentParent* aSource) {
    return IsTop();
  }

  void DidSet(FieldIndex<IDX_DisplayMode>, enum DisplayMode aOldValue);

  bool CanSet(FieldIndex<IDX_ExplicitActive>, const ExplicitActiveStatus&,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_ExplicitActive>, ExplicitActiveStatus aOldValue);

  bool CanSet(FieldIndex<IDX_IsActiveBrowserWindowInternal>, const bool& aValue,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_IsActiveBrowserWindowInternal>, bool aOldValue);

  // Ensure that we only set the flag on the top level browsingContext.
  // And then, we do a pre-order walk in the tree to refresh the
  // volume of all media elements.
  void DidSet(FieldIndex<IDX_Muted>);

  bool CanSet(FieldIndex<IDX_IsAppTab>, const bool& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_HasSiblings>, const bool& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_ShouldDelayMediaFromStart>, const bool& aValue,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_ShouldDelayMediaFromStart>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_OverrideDPPX>, const float& aValue,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_OverrideDPPX>, float aOldValue);

  bool CanSet(FieldIndex<IDX_EmbedderInnerWindowId>, const uint64_t& aValue,
              ContentParent* aSource);

  CanSetResult CanSet(FieldIndex<IDX_CurrentInnerWindowId>,
                      const uint64_t& aValue, ContentParent* aSource);

  void DidSet(FieldIndex<IDX_CurrentInnerWindowId>);

  bool CanSet(FieldIndex<IDX_ParentInitiatedNavigationEpoch>,
              const uint64_t& aValue, ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_IsPopupSpam>, const bool& aValue,
              ContentParent* aSource);

  void DidSet(FieldIndex<IDX_IsPopupSpam>);

  void DidSet(FieldIndex<IDX_GVAudibleAutoplayRequestStatus>);
  void DidSet(FieldIndex<IDX_GVInaudibleAutoplayRequestStatus>);

  void DidSet(FieldIndex<IDX_Loading>);

  void DidSet(FieldIndex<IDX_AncestorLoading>);

  void DidSet(FieldIndex<IDX_PlatformOverride>);
  CanSetResult CanSet(FieldIndex<IDX_PlatformOverride>,
                      const nsString& aPlatformOverride,
                      ContentParent* aSource);

  void DidSet(FieldIndex<IDX_UserAgentOverride>);
  CanSetResult CanSet(FieldIndex<IDX_UserAgentOverride>,
                      const nsString& aUserAgent, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_OrientationLock>,
              const mozilla::hal::ScreenOrientation& aOrientationLock,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_EmbedderElementType>,
              const Maybe<nsString>& aInitiatorType, ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_MessageManagerGroup>,
              const nsString& aMessageManagerGroup, ContentParent* aSource);

  CanSetResult CanSet(FieldIndex<IDX_AllowContentRetargeting>,
                      const bool& aAllowContentRetargeting,
                      ContentParent* aSource);
  CanSetResult CanSet(FieldIndex<IDX_AllowContentRetargetingOnChildren>,
                      const bool& aAllowContentRetargetingOnChildren,
                      ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_FullscreenAllowedByOwner>, const bool&,
              ContentParent*);
  bool CanSet(FieldIndex<IDX_WatchedByDevToolsInternal>,
              const bool& aWatchedByDevToolsInternal, ContentParent* aSource);

  CanSetResult CanSet(FieldIndex<IDX_DefaultLoadFlags>,
                      const uint32_t& aDefaultLoadFlags,
                      ContentParent* aSource);
  void DidSet(FieldIndex<IDX_DefaultLoadFlags>);

  bool CanSet(FieldIndex<IDX_UseGlobalHistory>, const bool& aUseGlobalHistory,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_TargetTopLevelLinkClicksToBlankInternal>,
              const bool& aTargetTopLevelLinkClicksToBlankInternal,
              ContentParent* aSource);

  void DidSet(FieldIndex<IDX_HasSessionHistory>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_BrowserId>, const uint32_t& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_UseErrorPages>, const bool& aUseErrorPages,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_PendingInitialization>, bool aNewValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_TopLevelCreatedByWebContent>,
              const bool& aNewValue, ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_PageAwakeRequestCount>, uint32_t aNewValue,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_PageAwakeRequestCount>, uint32_t aOldValue);

  CanSetResult CanSet(FieldIndex<IDX_AllowJavascript>, bool aValue,
                      ContentParent* aSource);
  void DidSet(FieldIndex<IDX_AllowJavascript>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_ForceDesktopViewport>, bool aValue,
              ContentParent* aSource) {
    return IsTop() && XRE_IsParentProcess();
  }

  // TODO(emilio): Maybe handle the flag being set dynamically without
  // navigating? The previous code didn't do it tho, and a reload is probably
  // worth it regardless.
  // void DidSet(FieldIndex<IDX_ForceDesktopViewport>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_HasRestoreData>, bool aNewValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_IsUnderHiddenEmbedderElement>,
              const bool& aIsUnderHiddenEmbedderElement,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_ForceOffline>, bool aNewValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_TopInnerSizeForRFP>, bool, ContentParent*) {
    return IsTop();
  }

  bool CanSet(FieldIndex<IDX_EmbeddedInContentDocument>, bool,
              ContentParent* aSource) {
    return CheckOnlyEmbedderCanSet(aSource);
  }

  template <size_t I, typename T>
  bool CanSet(FieldIndex<I>, const T&, ContentParent*) {
    return true;
  }

  // Overload `DidSet` to get notifications for a particular field being set.
  //
  // You can also overload the variant that gets the old value if you need it.
  template <size_t I>
  void DidSet(FieldIndex<I>) {}
  template <size_t I, typename T>
  void DidSet(FieldIndex<I>, T&& aOldValue) {}

  void DidSet(FieldIndex<IDX_FullZoom>, float aOldValue);
  void DidSet(FieldIndex<IDX_TextZoom>, float aOldValue);
  void DidSet(FieldIndex<IDX_AuthorStyleDisabledDefault>);

  bool CanSet(FieldIndex<IDX_IsInBFCache>, bool, ContentParent* aSource);
  void DidSet(FieldIndex<IDX_IsInBFCache>);

  void DidSet(FieldIndex<IDX_IsSyntheticDocumentContainer>);

  void DidSet(FieldIndex<IDX_IsUnderHiddenEmbedderElement>, bool aOldValue);

  // Allow if the process attemping to set field is the same as the owning
  // process. Deprecated. New code that might use this should generally be moved
  // to WindowContext or be settable only by the parent process.
  CanSetResult LegacyRevertIfNotOwningOrParentProcess(ContentParent* aSource);

  // True if the process attempting to set field is the same as the embedder's
  // process.
  bool CheckOnlyEmbedderCanSet(ContentParent* aSource);

  void CreateChildSHistory();

  using PrincipalWithLoadIdentifierTuple =
      std::tuple<nsCOMPtr<nsIPrincipal>, uint64_t>;

  nsIPrincipal* GetSavedPrincipal(
      Maybe<PrincipalWithLoadIdentifierTuple> aPrincipalTuple);

  // Type of BrowsingContent
  const Type mType;

  // Unique id identifying BrowsingContext
  const uint64_t mBrowsingContextId;

  RefPtr<BrowsingContextGroup> mGroup;
  RefPtr<WindowContext> mParentWindow;
  nsCOMPtr<nsIDocShell> mDocShell;

  RefPtr<Element> mEmbedderElement;

  nsTArray<RefPtr<WindowContext>> mWindowContexts;
  RefPtr<WindowContext> mCurrentWindowContext;

  RefPtr<nsGeolocationService> mGeolocationServiceOverride;

  // This is not a strong reference, but using a JS::Heap for that should be
  // fine. The JSObject stored in here should be a proxy with a
  // nsOuterWindowProxy handler, which will update the pointer from its
  // objectMoved hook and clear it from its finalize hook.
  JS::Heap<JSObject*> mWindowProxy;
  LocationProxy mLocation;

  // OriginAttributes for this BrowsingContext. May not be changed after this
  // BrowsingContext is attached.
  OriginAttributes mOriginAttributes;

  // The network request context id, representing the nsIRequestContext
  // associated with this BrowsingContext, and LoadGroups created for it.
  uint64_t mRequestContextId = 0;

  // Determines if private browsing should be used. May not be changed after
  // this BrowsingContext is attached. This field matches mOriginAttributes in
  // content Browsing Contexts. Currently treated as a binary value: 1 - in
  // private mode, 0 - not private mode.
  uint32_t mPrivateBrowsingId;

  // True if Attach() has been called on this BrowsingContext already.
  bool mEverAttached : 1;

  // Is the most recent Document in this BrowsingContext loaded within this
  // process? This may be true with a null mDocShell after the Window has been
  // closed.
  bool mIsInProcess : 1;

  // Has this browsing context been discarded? BrowsingContexts should
  // only be discarded once.
  bool mIsDiscarded : 1;

  // True if this BrowsingContext has no associated visible window, and is owned
  // by whichever process created it, even if top-level.
  bool mWindowless : 1;

  // This is true if the BrowsingContext was out of process, but is now in
  // process, and might have remote window proxies that need to be cleaned up.
  bool mDanglingRemoteOuterProxies : 1;

  // True if this BrowsingContext has been embedded in a element in this
  // process.
  bool mEmbeddedByThisProcess : 1;

  // Determines if remote (out-of-process) tabs should be used. May not be
  // changed after this BrowsingContext is attached.
  bool mUseRemoteTabs : 1;

  // Determines if out-of-process iframes should be used. May not be changed
  // after this BrowsingContext is attached.
  bool mUseRemoteSubframes : 1;

  // True if this BrowsingContext is for a frame that was added dynamically.
  bool mCreatedDynamically : 1;

  // Set to true if the browsing context is in the bfcache and pagehide has been
  // dispatched. When coming out from the bfcache, the value is set to false
  // before dispatching pageshow.
  bool mIsInBFCache : 1;

  // Determines if we can execute scripts in this BrowsingContext. True if
  // AllowJavascript() is true and script execution is allowed in the parent
  // WindowContext.
  bool mCanExecuteScripts : 1;

  // The original offset of this context in its container. This property is -1
  // if this BrowsingContext is for a frame that was added dynamically.
  int32_t mChildOffset;

  // The start time of user gesture, this is only available if the browsing
  // context is in process.
  TimeStamp mUserGestureStart;

  // Triggering principal and principal to inherit need to point to original
  // principal instances if the document is loaded in the same process as the
  // process that initiated the load. When the load starts we save the
  // principals along with the current load id.
  // These principals correspond to the most recent load that took place within
  // the process of this browsing context.
  Maybe<PrincipalWithLoadIdentifierTuple> mTriggeringPrincipal;
  Maybe<PrincipalWithLoadIdentifierTuple> mPrincipalToInherit;

  class DeprioritizedLoadRunner
      : public mozilla::Runnable,
        public mozilla::LinkedListElement<DeprioritizedLoadRunner> {
   public:
    explicit DeprioritizedLoadRunner(nsIRunnable* aInner)
        : Runnable("DeprioritizedLoadRunner"), mInner(aInner) {}

    NS_IMETHOD Run() override {
      if (mInner) {
        RefPtr<nsIRunnable> inner = std::move(mInner);
        inner->Run();
      }

      return NS_OK;
    }

   private:
    RefPtr<nsIRunnable> mInner;
  };

  mozilla::LinkedList<DeprioritizedLoadRunner> mDeprioritizedLoadRunner;

  RefPtr<SessionStorageManager> mSessionStorageManager;
  RefPtr<ChildSHistory> mChildSessionHistory;

  nsTArray<std::function<void(uint64_t)>> mDiscardListeners;

  // Counter and time span for rate limiting Location and History API calls.
  // Used by CheckNavigationRateLimit. Do not apply cross-process.
  uint32_t mNavigationRateLimitCount;
  mozilla::TimeStamp mNavigationRateLimitSpanStart;

  mozilla::LinkedList<dom::Location> mLocations;
};

/**
 * Gets a WindowProxy object for a BrowsingContext that lives in a different
 * process (creating the object if it doesn't already exist). The WindowProxy
 * object will be in the compartment that aCx is currently in. This should only
 * be called if aContext doesn't hold a docshell, otherwise the BrowsingContext
 * lives in this process, and a same-process WindowProxy should be used (see
 * nsGlobalWindowOuter). This should only be called by bindings code, ToJSValue
 * is the right API to get a WindowProxy for a BrowsingContext.
 *
 * If aTransplantTo is non-null, then the WindowProxy object will eventually be
 * transplanted onto it. Therefore it should be used as the value in the remote
 * proxy map. We assume that in this case the failure is unrecoverable, so we
 * crash immediately rather than return false.
 */
extern bool GetRemoteOuterWindowProxy(JSContext* aCx, BrowsingContext* aContext,
                                      JS::Handle<JSObject*> aTransplantTo,
                                      JS::MutableHandle<JSObject*> aRetVal);

using BrowsingContextTransaction = BrowsingContext::BaseTransaction;
using BrowsingContextInitializer = BrowsingContext::IPCInitializer;
using MaybeDiscardedBrowsingContext = MaybeDiscarded<BrowsingContext>;

// Specialize the transaction object for every translation unit it's used in.
extern template class syncedcontext::Transaction<BrowsingContext>;

}  // namespace dom

// Allow sending BrowsingContext objects over IPC.
namespace ipc {
template <>
struct IPDLParamTraits<dom::MaybeDiscarded<dom::BrowsingContext>> {
  static void Write(IPC::MessageWriter* aWriter, IProtocol* aActor,
                    const dom::MaybeDiscarded<dom::BrowsingContext>& aParam);
  static bool Read(IPC::MessageReader* aReader, IProtocol* aActor,
                   dom::MaybeDiscarded<dom::BrowsingContext>* aResult);
};

template <>
struct IPDLParamTraits<dom::BrowsingContext::IPCInitializer> {
  static void Write(IPC::MessageWriter* aWriter, IProtocol* aActor,
                    const dom::BrowsingContext::IPCInitializer& aInitializer);

  static bool Read(IPC::MessageReader* aReader, IProtocol* aActor,
                   dom::BrowsingContext::IPCInitializer* aInitializer);
};
}  // namespace ipc
}  // namespace mozilla

#endif  // !defined(mozilla_dom_BrowsingContext_h)
