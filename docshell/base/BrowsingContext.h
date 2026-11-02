/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BrowsingContext_h
#define mozilla_dom_BrowsingContext_h

#include <tuple>
#include "GVAutoplayRequestUtils.h"
#include "Units.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HalScreenConfiguration.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"
#include "mozilla/WeakPtr.h"

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/LocationBase.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/NavigationBinding.h"
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
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

class ErrorResult;
class GeolocationService;
class LogModule;

namespace ipc {
class IProtocol;
class IPCResult;
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
class NavigationReactivationData;
template <typename>
struct Nullable;
class PreviousSessionHistoryInfo;
template <typename T>
class Sequence;
class SessionHistoryInfo;
class SessionStorageManager;
class StructuredCloneHolder;
struct NavigationAPIMethodTracker;
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

  bool operator==(const EmbedderColorSchemes& aOther) const = default;
};

// Fields are readable by any process. Racy sets will be resolved as-if they
// occurred in the order the parent process finds out about them.
//
// The `DidSet` method may be overloaded. It is called to run code in every
// process whenever the value is updated. This currently occurs even if the
// value didn't change, though this may change in the future.
//
// Field validation is customized by specifying `.mCanSet` to a specific value.
// If this check fails, the offending process will be killed.
//
//  * If custom behaviour is needed, `CanSet::Custom` can be specified, which
//    will invoke a `CanSet` overload on `BrowsingContext`.
//
// Initial values are configured with `.mDefault` and `ComputeInitialFields`.
#define MOZ_EACH_BC_FIELD(FIELD)                                               \
  FIELD(Name, nsString, {.mCanSet = CanSet::Unrestricted})                     \
  FIELD(Closed, bool, {.mCanSet = CanSet::Unrestricted})                       \
  FIELD(ExplicitActive, ExplicitActiveStatus,                                  \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  /* Top()-only. If true, new-playing media will be suspended when in an       \
   * inactive browsing context. */                                             \
  FIELD(SuspendMediaWhenInactive, bool,                                        \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* If true, we're within the nested event loop in window.open, and this      \
   * context may not be used as the target of a load */                        \
  FIELD(PendingInitialization, bool,                                           \
        {.mTopOnly = true, .mCanSet = CanSet::Custom})                         \
  /* Indicates if the browser window is active for the purpose of the          \
   * :-moz-window-inactive pseudoclass. Only read from or set on the           \
   * top BrowsingContext. */                                                   \
  FIELD(IsActiveBrowserWindowInternal, bool,                                   \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(OpenerPolicy, nsILoadInfo::CrossOriginOpenerPolicy,                    \
        {.mCanSet = CanSet::Custom})                                           \
  /* Current opener for the BrowsingContext. Weak reference */                 \
  FIELD(OpenerId, uint64_t, {.mCanSet = CanSet::Custom})                       \
  FIELD(OnePermittedSandboxedNavigatorId, uint64_t,                            \
        {.mCanSet = CanSet::Unrestricted})                                     \
  FIELD(CurrentInnerWindowId, uint64_t, {.mCanSet = CanSet::Custom})           \
  FIELD(HadOriginalOpener, bool, {.mCanSet = CanSet::Unrestricted})            \
  /* Was this window created by a webpage through window.open or an anchor     \
   * link? In general, windows created this way may be manipulated (e.g.       \
   * closed, resized or moved) by content JS. */                               \
  FIELD(TopLevelCreatedByWebContent, bool,                                     \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(IsPopupSpam, bool, {.mCanSet = CanSet::Custom})                        \
  /* Hold the audio muted state and should be used on top level browsing       \
   * contexts only */                                                          \
  FIELD(Muted, bool, {.mCanSet = CanSet::Unrestricted})                        \
  /* Hold the pinned/app-tab state and should be used on top level browsing    \
   * contexts only */                                                          \
  FIELD(IsAppTab, bool, {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})     \
  /* Whether this is a captive portal tab. Should be used on top level         \
   * browsing contexts only */                                                 \
  FIELD(IsCaptivePortalTab, bool, {.mCanSet = CanSet::Unrestricted})           \
  /* Whether there's more than 1 tab / toplevel browsing context in this       \
   * parent window. Used to determine if a given BC is allowed to resize       \
   * and/or move the window or not. */                                         \
  FIELD(HasSiblings, bool, {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})  \
  /* Indicate that whether we should delay media playback, which would only    \
     be done on an unvisited tab. And this should only be used on the top      \
     level browsing contexts */                                                \
  FIELD(ShouldDelayMediaFromStart, bool,                                       \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* See nsSandboxFlags.h for the possible flags. */                           \
  FIELD(SandboxFlags, uint32_t, {.mCanSet = CanSet::Unrestricted})             \
  /* The value of SandboxFlags when the BrowsingContext is first created.      \
   * Used for sandboxing the initial about:blank document. */                  \
  FIELD(InitialSandboxFlags, uint32_t, {.mCanSet = CanSet::Unrestricted})      \
  /* A non-zero unique identifier for the browser element that is hosting      \
   * this                                                                      \
   * BrowsingContext tree. Every BrowsingContext in the element's tree will    \
   * return the same ID in all processes and it will remain stable             \
   * regardless of process changes. When a browser element's frameloader is    \
   * switched to another browser element this ID will remain the same but      \
   * hosted under the under the new browser element. */                        \
  FIELD(BrowserId, uint64_t, {.mCanSet = CanSet::Custom})                      \
  FIELD(HistoryID, nsID, {.mCanSet = CanSet::ParentOnly})                      \
  FIELD(InRDMPane, bool, {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})    \
  FIELD(Loading, bool, {.mCanSet = CanSet::Unrestricted})                      \
  /* A field only set on top browsing contexts, which indicates that either:   \
   *                                                                           \
   *  * This is a browsing context created explicitly for printing or print    \
   *    preview (thus hosting static documents).                               \
   *                                                                           \
   *  * This is a browsing context where something in this tree is calling     \
   *    window.print() (and thus showing a modal dialog).                      \
   *                                                                           \
   * We use it exclusively to block navigation for both of these cases. */     \
  FIELD(IsPrinting, bool, {.mCanSet = CanSet::Unrestricted})                   \
  FIELD(AncestorLoading, bool, {.mCanSet = CanSet::Unrestricted})              \
  FIELD(AllowContentRetargeting, bool,                                         \
        {.mCanSet = CanSet::Custom, .mDefault = [] { return true; }})          \
  FIELD(AllowContentRetargetingOnChildren, bool,                               \
        {.mCanSet = CanSet::Custom, .mDefault = [] { return true; }})          \
  FIELD(ForceEnableTrackingProtection, bool,                                   \
        {.mCanSet = CanSet::Unrestricted})                                     \
  FIELD(UseGlobalHistory, bool, {.mCanSet = CanSet::Unrestricted})             \
  FIELD(TargetTopLevelLinkClicksToBlankInternal, bool,                         \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(FullscreenAllowedByOwner, bool, {.mCanSet = CanSet::EmbedderOnly})     \
  FIELD(ForceDesktopViewport, bool,                                            \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  /*                                                                           \
   * "is popup" in the spec.                                                   \
   * Set only on top browsing contexts.                                        \
   * This doesn't indicate whether this is actually a popup or not,            \
   * but whether this browsing context is created by requesting popup or not.  \
   * See also: nsWindowWatcher::ShouldOpenPopup.                               \
   */                                                                          \
  FIELD(IsPopupRequested, bool, {.mCanSet = CanSet::Unrestricted})             \
  /* These field are used to store the states of autoplay media request on     \
   * GeckoView only, and it would only be modified on the top level browsing   \
   * context. */                                                               \
  FIELD(GVAudibleAutoplayRequestStatus, GVAutoplayRequestStatus,               \
        {.mCanSet = CanSet::Unrestricted})                                     \
  FIELD(GVInaudibleAutoplayRequestStatus, GVAutoplayRequestStatus,             \
        {.mCanSet = CanSet::Unrestricted})                                     \
  FIELD(ScreenHeightOverride, uint64_t, {.mCanSet = CanSet::Unrestricted})     \
  FIELD(ScreenWidthOverride, uint64_t, {.mCanSet = CanSet::Unrestricted})      \
  FIELD(HasScreenAreaOverride, bool, {.mCanSet = CanSet::Unrestricted})        \
  /* ScreenOrientation-related APIs */                                         \
  FIELD(CurrentOrientationAngle, float, {.mCanSet = CanSet::Unrestricted})     \
  FIELD(CurrentOrientationType, mozilla::dom::OrientationType,                 \
        {.mCanSet = CanSet::Unrestricted})                                     \
  FIELD(OrientationLock, mozilla::hal::ScreenOrientation,                      \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted, .mDefault = [] {   \
           return mozilla::hal::ScreenOrientation::None;                       \
         }})                                                                   \
  FIELD(HasOrientationOverride, bool, {.mCanSet = CanSet::Unrestricted})       \
  FIELD(UserAgentOverride, nsCString,                                          \
        {.mTopOnly = true, .mCanSet = CanSet::Custom})                         \
  FIELD(TouchEventsOverrideInternal, mozilla::dom::TouchEventsOverride,        \
        {.mCanSet = CanSet::ParentOnly,                                        \
         .mDefault = [] { return TouchEventsOverride::None; }})                \
  FIELD(EmbedderElementType, Maybe<nsString>,                                  \
        {.mCanSet = CanSet::EmbedderOnly})                                     \
  FIELD(MessageManagerGroup, nsString,                                         \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(MaxTouchPointsOverride, uint8_t, {.mCanSet = CanSet::Unrestricted})    \
  FIELD(FullZoom, float,                                                       \
        {.mCanSet = CanSet::Unrestricted, .mDefault = [] { return 1.0f; }})    \
  FIELD(WatchedByDevToolsInternal, bool,                                       \
        {.mTopOnly = true, .mCanSet = CanSet::Custom})                         \
  FIELD(TextZoom, float,                                                       \
        {.mCanSet = CanSet::Unrestricted, .mDefault = [] { return 1.0f; }})    \
  FIELD(OverrideDPPX, float,                                                   \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  /* The current in-progress load. */                                          \
  FIELD(CurrentLoadIdentifier, Maybe<uint64_t>,                                \
        {.mCanSet = CanSet::Unrestricted})                                     \
  /* The android load identifier. Used to map to applink type */               \
  FIELD(AndroidAppLinkLoadIdentifier, Maybe<uint64_t>,                         \
        {.mCanSet = CanSet::Unrestricted})                                     \
  /* See nsIRequest for possible flags. */                                     \
  FIELD(DefaultLoadFlags, uint32_t, {.mCanSet = CanSet::Custom})               \
  /* Signals that session history is enabled for this browsing context tree.   \
   * This is only ever set to true on the top BC, so consumers need to get     \
   * the value from the top BC! */                                             \
  FIELD(HasSessionHistory, bool, {.mCanSet = CanSet::Unrestricted})            \
  FIELD(UseErrorPages, bool,                                                   \
        {.mCanSet = CanSet::EmbedderOnly, .mDefault = [] { return true; }})    \
  FIELD(PlatformOverride, nsString,                                            \
        {.mTopOnly = true, .mCanSet = CanSet::Custom})                         \
  /* Specifies if this BC has loaded documents besides the initial             \
   * about:blank document. about:privatebrowsing, about:home, about:newtab     \
   * and non-initial about:blank are not considered to be initial              \
   * documents. */                                                             \
  FIELD(HasLoadedNonInitialDocument, bool, {.mCanSet = CanSet::Unrestricted})  \
  /* Default value for nsIDocumentViewer::authorStyleDisabled in any new       \
   * browsing contexts created as a descendant of this one.  Valid only for    \
   * top BCs. */                                                               \
  FIELD(AuthorStyleDisabledDefault, bool, {.mCanSet = CanSet::Unrestricted})   \
  FIELD(ServiceWorkersTestingEnabled, bool,                                    \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(ServiceWorkersDisabledByPolicy, bool,                                  \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  FIELD(MediumOverride, nsString,                                              \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* DevTools override for prefers-color-scheme */                             \
  FIELD(PrefersColorSchemeOverride, dom::PrefersColorSchemeOverride,           \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  FIELD(LanguageOverride, nsCString,                                           \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  FIELD(TimezoneOverride, nsString,                                            \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* DevTools override for prefers-reduced-motion */                           \
  FIELD(PrefersReducedMotionOverride, dom::PrefersReducedMotionOverride,       \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* DevTools override for forced-colors */                                    \
  FIELD(ForcedColorsOverride, dom::ForcedColorsOverride,                       \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* DevTools override for prefers-contrast */                                 \
  FIELD(PrefersContrastOverride, dom::PrefersContrastOverride,                 \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* DevTools multiplier for animations playback rate */                       \
  FIELD(AnimationsPlayBackRateMultiplier, double,                              \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted, .mDefault = [] {   \
           return 1.0;                                                         \
         }})                                                                   \
  /* prefers-color-scheme override based on the color-scheme style of our      \
   * <browser> embedder element. */                                            \
  FIELD(EmbedderColorSchemes, EmbedderColorSchemes,                            \
        {.mCanSet = CanSet::EmbedderOnly})                                     \
  /* Content-area scrollbar insets forwarded from the <browser> embedder's     \
   * -moz-scrollbar-inset-{block,inline}, so the top-level content viewport    \
   * scrollbars clear the rounded content-area corners. */                     \
  FIELD(EmbedderScrollbarInset, LayoutDeviceIntMargin,                         \
        {.mCanSet = CanSet::EmbedderOnly})                                     \
  FIELD(DisplayMode, dom::DisplayMode,                                         \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* The number of entries added to the session history because of this        \
   * browsing context. */                                                      \
  FIELD(HistoryEntryCount, uint32_t, {.mCanSet = CanSet::Unrestricted})        \
  FIELD(HasRestoreData, bool,                                                  \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  FIELD(SessionStoreEpoch, uint32_t,                                           \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  /* Whether we can execute scripts in this BrowsingContext. Has no effect     \
   * unless scripts are also allowed in the parent WindowContext. */           \
  FIELD(AllowJavascript, bool,                                                 \
        {.mCanSet = CanSet::ParentOnly, .mDefault = [] { return true; }})      \
  /* The count of request that are used to prevent the browsing context tree   \
   * from being suspended, which would ONLY be modified on the top level       \
   * context in the chrome process because that's a non-atomic counter */      \
  FIELD(PageAwakeRequestCount, uint32_t,                                       \
        {.mTopOnly = true, .mCanSet = CanSet::ParentOnly})                     \
  /* This field only gets incrememented when we start navigations in the       \
   * parent process. This is used for keeping track of the racing navigations  \
   * between the parent and content processes. */                              \
  FIELD(ParentInitiatedNavigationEpoch, uint64_t,                              \
        {.mCanSet = CanSet::ParentOnly})                                       \
  /* This browsing context is for a synthetic image document wrapping an       \
   * image embedded in <object> or <embed>. */                                 \
  FIELD(IsSyntheticDocumentContainer, bool, {.mCanSet = CanSet::Unrestricted}) \
  /* If true, this document is embedded within a content document,  either     \
   * loaded in the parent (e.g. about:addons or the devtools toolbox), or in   \
   * a content process. */                                                     \
  FIELD(EmbeddedInContentDocument, bool, {.mCanSet = CanSet::EmbedderOnly})    \
  /* If true, this browsing context is within a hidden embedded document. */   \
  FIELD(IsUnderHiddenEmbedderElement, bool, {.mCanSet = CanSet::Unrestricted}) \
  /* If true, this browsing context is offline */                              \
  FIELD(ForceOffline, bool, {.mCanSet = CanSet::ParentOnly})                   \
  /* Used to propagate window.top's inner size for RFPTarget::Window*          \
   * protections */                                                            \
  FIELD(InnerSizeSpoofedForRFP, CSSIntSize,                                    \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* Used to propagate document's IPAddressSpace  */                           \
  FIELD(IPAddressSpace, nsILoadInfo::IPAddressSpace,                           \
        {.mCanSet = CanSet::ParentOnly})                                       \
  /* This is true if we should redirect to an error page when inserting *      \
   * meta tags flagging adult content into our documents */                    \
  FIELD(ParentalControlsEnabled, bool, {.mCanSet = CanSet::ParentOnly})        \
  /* If true, this traversable is a Document Picture-in-Picture and            \
     is subject to certain restrictions */                                     \
  FIELD(IsDocumentPiP, bool,                                                   \
        {.mTopOnly = true, .mCanSet = CanSet::Unrestricted})                   \
  /* True if this is a content browsing context whose page has an open         \
     Document Picture-in-Picture window */                                     \
  FIELD(ControlsDocumentPiP, bool,                                             \
        {.mTopOnly = true, .mCanSet = CanSet::Custom})

#define NS_DOM_BROWSINGCONTEXT_IID \
  {0x5059a6aa, 0xf09, 0x415c, {0x89, 0xbd, 0x63, 0xfd, 0xe5, 0xab, 0x1a, 0x66}};

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
class BrowsingContext : public nsILoadContext,
                        public nsWrapperCache,
                        public SupportsWeakPtr {
  MOZ_DECL_SYNCED_CONTEXT(BrowsingContext, MOZ_EACH_BC_FIELD)
  NS_INLINE_DECL_STATIC_IID(NS_DOM_BROWSINGCONTEXT_IID)

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

  // https://html.spec.whatwg.org/#script-closable
  bool IsScriptClosable() const;

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

  // Returns the current Document PiP window opened from this BrowsingContext,
  // if there is one. Only works in the content process that opened it.
  nsGlobalWindowInner* GetOpenedDocumentPiPWindow() const;

  uint64_t GetRequestContextId() const { return mRequestContextId; }

  // Detach the current BrowsingContext from its parent, in both the
  // child and the parent process.
  void Detach(bool aFromIPC = false);

  // Prepare this BrowsingContext to leave the current process.
  void PrepareForProcessChange();

  // Triggers a load in the process which currently owns this BrowsingContext.
  MOZ_CAN_RUN_SCRIPT
  nsresult LoadURI(nsDocShellLoadState* aLoadState,
                   bool aSetNavigating = false);

  nsresult InternalLoad(nsDocShellLoadState* aLoadState);

  MOZ_CAN_RUN_SCRIPT void Navigate(
      nsIURI* aURI, Document* aSourceDocument, nsIPrincipal& aSubjectPrincipal,
      ErrorResult& aRv,
      NavigationHistoryBehavior aHistoryHandling =
          NavigationHistoryBehavior::Auto,
      bool aNeedsCompletelyLoadedDocument = false,
      nsIStructuredCloneContainer* aNavigationAPIState = nullptr,
      dom::NavigationAPIMethodTracker* aNavigationAPIMethodTracker = nullptr);

  // Removes the root document for this BrowsingContext tree from the BFCache,
  // if it is cached, and returns true if it was.
  bool RemoveRootFromBFCacheSync();

  // Check to see if we are sandboxed from the source BrowsingContext as the
  // result of an iframe or CSP sandbox. Reports a console error if so.
  // This also applies to window.close(), aForClose adjusts the error.
  nsresult EnsureSourceSandboxAllowsNavigation(BrowsingContext* aSourceBC,
                                               bool aForClose = false);
  nsresult EnsureSourceSandboxAllowsNavigation(nsDocShellLoadState* aLoadState,
                                               bool aForClose = false);

  // If the current BrowsingContext is top-level, we run checks to see if
  // the source BrowsingContext is allowed to perform the navigation.
  nsresult CheckFramebusting(nsDocShellLoadState* aLoadState);

  // Compute whether this browsing context is allowed to navigate the
  // top-level without user interaction. Must be called in-process.
  bool ComputeIsFramebustingAllowed();

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

  RefPtr<GeolocationService> GetGeolocationServiceOverride();

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

  nsILoadInfo::IPAddressSpace GetCurrentIPAddressSpace() const {
    return GetIPAddressSpace();
  }

  void SetCurrentIPAddressSpace(nsILoadInfo::IPAddressSpace aIPAddressSpace) {
    (void)SetIPAddressSpace(aIPAddressSpace);
  }

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

  CSSIntSize TopInnerSizeSpoofedForRFP() const {
    return Top()->GetInnerSizeSpoofedForRFP();
  }

  [[nodiscard]] nsresult SetScreenAreaOverride(uint64_t aScreenWidth,
                                               uint64_t aScreenHeight) {
    if (GetHasScreenAreaOverride() &&
        GetScreenWidthOverride() == aScreenWidth &&
        GetScreenHeightOverride() == aScreenHeight) {
      return NS_OK;
    }

    Transaction txn;
    txn.SetScreenWidthOverride(aScreenWidth);
    txn.SetScreenHeightOverride(aScreenHeight);
    txn.SetHasScreenAreaOverride(true);
    return txn.Commit(this);
  }

  void SetScreenAreaOverride(uint64_t aScreenWidth, uint64_t aScreenHeight,
                             ErrorResult& aRv) {
    MOZ_ASSERT(IsTop());

    if (NS_FAILED(SetScreenAreaOverride(aScreenWidth, aScreenHeight))) {
      aRv.ThrowInvalidStateError("Browsing context is discarded");
    }
  }

  void ResetScreenAreaOverride() {
    MOZ_ASSERT(IsTop());

    (void)SetHasScreenAreaOverride(false);
  }

  bool HasScreenAreaOverride() const {
    return Top()->GetHasScreenAreaOverride();
  }

  Maybe<CSSIntSize> GetScreenAreaOverride() {
    if (!HasScreenAreaOverride()) {
      return Nothing();
    }
    CSSIntSize screenSize(Top()->GetScreenWidthOverride(),
                          Top()->GetScreenHeightOverride());
    return Some(screenSize);
  }

  // ScreenOrientation related APIs
  [[nodiscard]] nsresult SetCurrentOrientation(OrientationType aType,
                                               float aAngle) {
    Transaction txn;
    txn.SetCurrentOrientationType(aType);
    txn.SetCurrentOrientationAngle(aAngle);
    return txn.Commit(this);
  }

  bool HasOrientationOverride() const {
    return Top()->GetHasOrientationOverride();
  }

  [[nodiscard]] nsresult SetOrientationOverride(OrientationType aType,
                                                float aAngle) {
    if (GetHasOrientationOverride() && GetCurrentOrientationType() == aType &&
        GetCurrentOrientationAngle() == aAngle) {
      return NS_OK;
    }

    Transaction txn;
    txn.SetCurrentOrientationType(aType);
    txn.SetCurrentOrientationAngle(aAngle);
    txn.SetHasOrientationOverride(true);
    return txn.Commit(this);
  }

  void SetOrientationOverride(OrientationType aType, float aAngle,
                              ErrorResult& aRv) {
    MOZ_ASSERT(IsTop());

    if (NS_FAILED(SetOrientationOverride(aType, aAngle))) {
      aRv.ThrowInvalidStateError("Browsing context is discarded");
    }
  }

  void ResetOrientationOverride() {
    MOZ_ASSERT(IsTop());

    (void)SetHasOrientationOverride(false);
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

  // Since mWindowProxy is a weak pointer it has to be updated during sweeping.
  static void SweepWindowProxies(JSTracer* aTrc);

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
  MOZ_CAN_RUN_SCRIPT void Close(CallerType aCallerType, ErrorResult& aError);
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

  void GetCustomUserAgent(nsACString& aUserAgent) {
    aUserAgent = Top()->GetUserAgentOverride();
  }
  nsresult SetCustomUserAgent(const nsACString& aUserAgent);
  void SetCustomUserAgent(const nsACString& aUserAgent, ErrorResult& aRv);

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
    uint64_t GetOpenerId() const { return mFields.Get<IDX_OpenerId>(); }
    uint64_t GetBrowserId() const { return mFields.Get<IDX_BrowserId>(); }

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
                            bool aCloneEntryChildren, bool aChannelExpired,
                            uint32_t aCacheKey);

  // Set a new active entry on this browsing context. This is used for
  // implementing history.pushState/replaceState and same document navigations.
  // The new active entry will be linked to the current active entry through
  // its shared state.
  // aPreviousScrollPos is the scroll position that needs to be saved on the
  // previous active entry.
  // aPreviousActiveEntry should be the best available approximation of the
  // current active entry in the parent, which the child process cannot access.
  // aUpdatedCacheKey is the cache key to set on the new active entry. If
  // aUpdatedCacheKey is 0 then it will be ignored.
  void SetActiveSessionHistoryEntry(const Maybe<nsPoint>& aPreviousScrollPos,
                                    SessionHistoryInfo* aInfo,
                                    SessionHistoryInfo* aPreviousActiveEntry,
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

  // HistoryGo is a content process entry point for #apply-the-history-step
  MOZ_CAN_RUN_SCRIPT
  void HistoryGo(int32_t aOffset, uint64_t aHistoryEpoch,
                 bool aRequireUserInteraction, bool aUserActivation,
                 std::function<void(Maybe<int32_t>&&)>&& aResolver);

  // NavigationTraverse is a content process entry point for
  // #apply-the-history-step. It differs mainly in that it's using a
  // navigation entry key for navigation, and that it's possible to
  // pass `aCheckForCancelation`, which controls if we should run
  // the onbeforeunload and traversable onnavigate checks.
  MOZ_CAN_RUN_SCRIPT
  void NavigationTraverse(const nsID& aKey, uint64_t aHistoryEpoch,
                          bool aUserActivation, bool aCheckForCancelation,
                          std::function<void(nsresult)>&& aResolver);

  bool ShouldUpdateSessionHistory(uint32_t aLoadType);

  // Checks if we reached the rate limit for navigations, which includes calls
  // to the Location and History APIs.
  // The rate limit is controlled by the "dom.navigation.navigationRateLimit"
  // prefs. Rate limit applies per BrowsingContext. Returns true if we are below
  // the rate limit and increments the counter. Returns false if the limit is
  // reached
  bool CheckNavigationRateLimit(CallerType aCallerType);

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

  bool ServiceWorkersDisabledByPolicy() const {
    return GetServiceWorkersDisabledByPolicy();
  }

  void GetMediumOverride(nsAString& aOverride) const {
    aOverride = GetMediumOverride();
  }

  void GetLanguageOverride(nsACString& aLanguageOverride) const {
    aLanguageOverride = GetLanguageOverride();
  }

  void GetTimezoneOverride(nsAString& aTimezoneOverride) const {
    aTimezoneOverride = GetTimezoneOverride();
  }

  dom::PrefersColorSchemeOverride PrefersColorSchemeOverride() const {
    return GetPrefersColorSchemeOverride();
  }

  dom::ForcedColorsOverride ForcedColorsOverride() const {
    return GetForcedColorsOverride();
  }

  dom::PrefersReducedMotionOverride PrefersReducedMotionOverride() const {
    return GetPrefersReducedMotionOverride();
  }

  dom::PrefersContrastOverride PrefersContrastOverride() const {
    return GetPrefersContrastOverride();
  }

  double AnimationsPlayBackRateMultiplier() const {
    return Top()->GetAnimationsPlayBackRateMultiplier();
  }

  bool IsInBFCache() const;
  bool IsEnteringBFCache() const { return mIsEnteringBFCache; }
  // aForUnload means that the old document unloads instead of entering BFCache.
  void DeactivateDocuments(const SessionHistoryInfo* aPageSwapTargetInfo,
                           NavigationType aNavigationType,
                           bool aForUnload = false);

  MOZ_CAN_RUN_SCRIPT
  void ReactivateDocuments(
      const nsTArray<NavigationReactivationData>& aReactivationData,
      const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation);

  MOZ_CAN_RUN_SCRIPT
  void UpdateForReactivation(
      const Maybe<SessionHistoryInfo>& aReactivatedEntry,
      const nsTArray<SessionHistoryInfo>& aNewSHEs,
      const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation);

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

  void ConsumeHistoryActivation();
  void SynchronizeNavigationAPIState(nsIStructuredCloneContainer* aState);

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

  void SetIsInBFCache(bool aIsInBFCache);

  void SetIsEnteringBFCache(bool aIsEnteringBFCache);

 private:
  // Check whether it's OK to load the given url with the given subject
  // principal, and if so construct the right nsDocShellLoadInfo for the load
  // and return it.
  already_AddRefed<nsDocShellLoadState> CheckURLAndCreateLoadState(
      nsIURI* aURI, nsIPrincipal& aSubjectPrincipal, Document* aSourceDocument,
      ErrorResult& aRv);

  bool AddSHEntryWouldIncreaseLength(SessionHistoryInfo* aCurrentEntry) const;

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

  // Compute the initial synced field values for a new context from what every
  // process can know: field defaults, the related contexts and the shape of
  // the tree. Runs in the creating process, and again in the parent process for
  // a context created by a content process, whose sent values are then
  // reconciled against the result by `Transaction::ReconcileInitialFields`.
  static FieldValues ComputeInitialFields(WindowContext* aParentWindow,
                                          BrowsingContext* aOpener,
                                          BrowsingContextGroup* aGroup,
                                          Type aType);

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

    // Deleted catch-all overload: every field must provide a `CanSet` whose
    // value parameter exactly matches the field's type.
    template <size_t I, typename T>
    bool CanSet(FieldIndex<I>, const T&, ContentParent*) = delete;

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

  // Update dependents if the activeness of this BC was explicitly changed.
  void ActivenessChanged(bool aIsActive);

  using CanSetResult = syncedcontext::CanSetResult;

  // Overload `DidSet` to get notifications for a particular field being set.
  //
  // You can also overload the variant that gets the old value if you need it.
  template <size_t I>
  void DidSet(FieldIndex<I>) {}
  template <size_t I, typename T>
  void DidSet(FieldIndex<I>, T&& aOldValue) {}

  void DidSet(FieldIndex<IDX_SessionStoreEpoch>, uint32_t aOldValue);

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

  void DidSet(FieldIndex<IDX_InRDMPane>, bool aOldValue);
  void DidSet(FieldIndex<IDX_HasOrientationOverride>, bool aOldValue);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DidSet(FieldIndex<IDX_ForceDesktopViewport>,
                                          bool aOldValue);

  void DidSet(FieldIndex<IDX_EmbedderColorSchemes>,
              EmbedderColorSchemes&& aOldValue);

  void DidSet(FieldIndex<IDX_EmbedderScrollbarInset>,
              LayoutDeviceIntMargin&& aOldValue);

  void DidSet(FieldIndex<IDX_PrefersColorSchemeOverride>,
              dom::PrefersColorSchemeOverride aOldValue);

  void DidSet(FieldIndex<IDX_ForcedColorsOverride>,
              dom::ForcedColorsOverride aOldValue);

  void DidSet(FieldIndex<IDX_PrefersReducedMotionOverride>,
              dom::PrefersReducedMotionOverride aOldValue);

  void DidSet(FieldIndex<IDX_PrefersContrastOverride>,
              dom::PrefersContrastOverride aOldValue);

  void DidSet(FieldIndex<IDX_AnimationsPlayBackRateMultiplier>,
              double aOldValue);

  void PresContextAffectingFieldChanged();

  void DidSet(FieldIndex<IDX_LanguageOverride>, nsCString&& aOldValue);

  void DidSet(FieldIndex<IDX_TimezoneOverride>, nsString&& aOldValue);

  void DidSet(FieldIndex<IDX_MediumOverride>, nsString&& aOldValue);

  void DidSet(FieldIndex<IDX_TouchEventsOverrideInternal>,
              dom::TouchEventsOverride&& aOldValue);

  void DidSet(FieldIndex<IDX_DisplayMode>, enum DisplayMode aOldValue);

  void DidSet(FieldIndex<IDX_ExplicitActive>, ExplicitActiveStatus aOldValue);

  bool CanSet(FieldIndex<IDX_ControlsDocumentPiP>, bool,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_ControlsDocumentPiP>, bool aOldValue);

  void DidSet(FieldIndex<IDX_IsActiveBrowserWindowInternal>, bool aOldValue);

  // Ensure that we only set the flag on the top level browsingContext.
  // And then, we do a pre-order walk in the tree to refresh the
  // volume of all media elements.
  void DidSet(FieldIndex<IDX_Muted>);

  void DidSet(FieldIndex<IDX_ShouldDelayMediaFromStart>, bool aOldValue);

  void DidSet(FieldIndex<IDX_OverrideDPPX>, float aOldValue);

  CanSetResult CanSet(FieldIndex<IDX_CurrentInnerWindowId>,
                      const uint64_t& aValue, ContentParent* aSource);

  void DidSet(FieldIndex<IDX_CurrentInnerWindowId>);

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
                      const nsCString& aUserAgent, ContentParent* aSource);

  CanSetResult CanSet(FieldIndex<IDX_AllowContentRetargeting>,
                      const bool& aAllowContentRetargeting,
                      ContentParent* aSource);
  CanSetResult CanSet(FieldIndex<IDX_AllowContentRetargetingOnChildren>,
                      const bool& aAllowContentRetargetingOnChildren,
                      ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_WatchedByDevToolsInternal>,
              const bool& aWatchedByDevToolsInternal, ContentParent* aSource);

  CanSetResult CanSet(FieldIndex<IDX_DefaultLoadFlags>,
                      const uint32_t& aDefaultLoadFlags,
                      ContentParent* aSource);
  void DidSet(FieldIndex<IDX_DefaultLoadFlags>);

  void DidSet(FieldIndex<IDX_HasSessionHistory>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_BrowserId>, const uint64_t& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_PendingInitialization>, bool aNewValue,
              ContentParent* aSource);

  void DidSet(FieldIndex<IDX_PageAwakeRequestCount>, uint32_t aOldValue);

  void DidSet(FieldIndex<IDX_AllowJavascript>, bool aOldValue);

  // TODO(emilio): Maybe handle the flag being set dynamically without
  // navigating? The previous code didn't do it tho, and a reload is probably
  // worth it regardless.
  // void DidSet(FieldIndex<IDX_ForceDesktopViewport>, bool aOldValue);

  void DidSet(FieldIndex<IDX_FullZoom>, float aOldValue);
  void DidSet(FieldIndex<IDX_TextZoom>, float aOldValue);
  void DidSet(FieldIndex<IDX_AuthorStyleDisabledDefault>);

  void DidSet(FieldIndex<IDX_IsSyntheticDocumentContainer>);

  void DidSet(FieldIndex<IDX_IsUnderHiddenEmbedderElement>, bool aOldValue);

  void DidSet(FieldIndex<IDX_ForceOffline>, bool aOldValue);

  void DidSet(FieldIndex<IDX_IsDocumentPiP>, bool aWasPiP) {
    if (GetIsDocumentPiP() && !aWasPiP) {
      SetDisplayMode(DisplayMode::Picture_in_picture, IgnoreErrors());
    } else if (!GetIsDocumentPiP() && aWasPiP) {
      MOZ_ASSERT_UNREACHABLE("BrowsingContext should never leave PiP mode");
    }
  }

  static void UnregisterBrowserId(BrowsingContext* aBrowsingContext);

  // Allow if the process attemping to set field is the same as the owning
  // process. Deprecated. New code that might use this should generally be moved
  // to WindowContext or be settable only by the parent process.
  CanSetResult LegacyRevertIfNotOwningOrParentProcess(ContentParent* aSource);

  // True if the process attempting to set field is the same as the embedder's
  // process.
  bool CheckOnlyEmbedderCanSet(ContentParent* aSource);

  bool CheckOnlyParentProcessCanSet(ContentParent* aSource) {
    return XRE_IsParentProcess() && !aSource;
  }

  void CreateChildSHistory();

  using PrincipalWithLoadIdentifierTuple =
      std::tuple<nsCOMPtr<nsIPrincipal>, uint64_t>;

  nsIPrincipal* GetSavedPrincipal(
      Maybe<PrincipalWithLoadIdentifierTuple> aPrincipalTuple);

  // Type of BrowsingContent
  const Type mType;

  // Unique id identifying BrowsingContext
  const uint64_t mBrowsingContextId;

  // The key under which this context was last added to the current top by
  // BrowserId map.
  Maybe<uint64_t> mCurrentTopBrowserIdKey;

  RefPtr<BrowsingContextGroup> mGroup;
  RefPtr<WindowContext> mParentWindow;
  nsCOMPtr<nsIDocShell> mDocShell;

  RefPtr<Element> mEmbedderElement;

  nsTArray<RefPtr<WindowContext>> mWindowContexts;
  RefPtr<WindowContext> mCurrentWindowContext;

  RefPtr<GeolocationService> mGeolocationServiceOverride;

  // This is a weak reference. It will be updated automatically during sweeping
  // by SweepWindowProxies.
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

  // Set to true if the browsing context is in the bfcache and pagehide has not
  // been dispatched.
  bool mIsEnteringBFCache : 1 = false;

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
}  // namespace mozilla

// Allow sending BrowsingContext objects over IPC.
namespace IPC {
template <>
struct ParamTraits<
    mozilla::dom::MaybeDiscarded<mozilla::dom::BrowsingContext>> {
  using paramType = mozilla::dom::MaybeDiscarded<mozilla::dom::BrowsingContext>;
  static void Write(IPC::MessageWriter* aWriter, const paramType& aParam);
  static bool Read(IPC::MessageReader* aReader, paramType* aResult);
};

template <>
struct ParamTraits<mozilla::dom::BrowsingContext::IPCInitializer> {
  using paramType = mozilla::dom::BrowsingContext::IPCInitializer;
  static void Write(IPC::MessageWriter* aWriter, const paramType& aInitializer);
  static bool Read(IPC::MessageReader* aReader, paramType* aInitializer);
};
}  // namespace IPC

#endif  // !defined(mozilla_dom_BrowsingContext_h)
