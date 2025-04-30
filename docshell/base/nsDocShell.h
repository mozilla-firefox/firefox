/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDocShell_h__
#define nsDocShell_h__

#include "Units.h"
#include "mozilla/Encoding.h"
#include "mozilla/Maybe.h"
#include "mozilla/NotNull.h"
#include "mozilla/ScrollbarPreferences.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "nsCOMPtr.h"
#include "nsCharsetSource.h"
#include "nsDocLoader.h"
#include "nsIAuthPromptProvider.h"
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadContext.h"
#include "nsINetworkInterceptController.h"
#include "nsIRefreshURI.h"
#include "nsIWebNavigation.h"
#include "nsIWebPageDescriptor.h"
#include "nsIWebProgressListener.h"
#include "nsPoint.h"  // mCurrent/mDefaultScrollbarPreferences
#include "nsRect.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "prtime.h"

// Interfaces Needed

namespace mozilla {
class Encoding;
class HTMLEditor;
class ObservedDocShell;
class ScrollContainerFrame;
enum class TaskCategory;
namespace dom {
class ClientInfo;
class ClientSource;
class EventTarget;
enum class NavigationHistoryBehavior : uint8_t;
class SessionHistoryInfo;
struct LoadingSessionHistoryInfo;
struct Wireframe;
}  // namespace dom
namespace net {
class LoadInfo;
class DocumentLoadListener;
}  // namespace net
}  // namespace mozilla

class nsIController;
class nsIDocShellTreeOwner;
class nsIDocumentViewer;
class nsIHttpChannel;
class nsIMutableArray;
class nsIPrompt;
class nsIStringBundle;
class nsIURIFixup;
class nsIURIFixupInfo;
class nsIURILoader;
class nsIWebBrowserFind;
class nsIWidget;
class nsIReferrerInfo;

class nsCommandManager;
class nsDocShellEditorData;
class nsDOMNavigationTiming;
class nsDSURIContentListener;
class nsGlobalWindowOuter;

class FramingChecker;
class OnLinkClickEvent;

/* internally used ViewMode types */
enum ViewMode { viewNormal = 0x0, viewSource = 0x1 };

enum eCharsetReloadState {
  eCharsetReloadInit,
  eCharsetReloadRequested,
  eCharsetReloadStopOrigional
};

struct SameDocumentNavigationState;

class nsDocShell final : public nsDocLoader,
                         public nsIDocShell,
                         public nsIWebNavigation,
                         public nsIBaseWindow,
                         public nsIRefreshURI,
                         public nsIWebProgressListener,
                         public nsIWebPageDescriptor,
                         public nsIAuthPromptProvider,
                         public nsILoadContext,
                         public nsINetworkInterceptController,
                         public mozilla::SupportsWeakPtr {
 public:
  enum InternalLoad : uint32_t {
    INTERNAL_LOAD_FLAGS_NONE = 0x0,
    INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL = 0x1,
    INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER = 0x2,
    INTERNAL_LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP = 0x4,

    // This flag marks the first load in this object
    // @see nsIWebNavigation::LOAD_FLAGS_FIRST_LOAD
    INTERNAL_LOAD_FLAGS_FIRST_LOAD = 0x8,

    // The set of flags that should not be set before calling into
    // nsDocShell::LoadURI and other nsDocShell loading functions.
    INTERNAL_LOAD_FLAGS_LOADURI_SETUP_FLAGS = 0xf,

    INTERNAL_LOAD_FLAGS_BYPASS_CLASSIFIER = 0x10,
    INTERNAL_LOAD_FLAGS_FORCE_ALLOW_COOKIES = 0x20,

    // Whether the load should be treated as srcdoc load, rather than a URI one.
    INTERNAL_LOAD_FLAGS_IS_SRCDOC = 0x40,

    // Whether this is the load of a frame's original src attribute
    INTERNAL_LOAD_FLAGS_ORIGINAL_FRAME_SRC = 0x80,

    INTERNAL_LOAD_FLAGS_NO_OPENER = 0x100,

    // Whether a top-level data URI navigation is allowed for that load
    INTERNAL_LOAD_FLAGS_FORCE_ALLOW_DATA_URI = 0x200,

    // Whether the load should go through LoadURIDelegate.
    INTERNAL_LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE = 0x2000,
  };

  // Event type dispatched by RestorePresentation
  class RestorePresentationEvent : public mozilla::Runnable {
   public:
    NS_DECL_NSIRUNNABLE
    explicit RestorePresentationEvent(nsDocShell* aDs)
        : mozilla::Runnable("nsDocShell::RestorePresentationEvent"),
          mDocShell(aDs) {}
    void Revoke() { mDocShell = nullptr; }

   private:
    RefPtr<nsDocShell> mDocShell;
  };

  class InterfaceRequestorProxy : public nsIInterfaceRequestor {
   public:
    explicit InterfaceRequestorProxy(nsIInterfaceRequestor* aRequestor);
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIINTERFACEREQUESTOR

   private:
    virtual ~InterfaceRequestorProxy();
    InterfaceRequestorProxy() = default;
    nsWeakPtr mWeakPtr;
  };

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsDocShell, nsDocLoader)
  NS_DECL_NSIDOCSHELL
  NS_DECL_NSIDOCSHELLTREEITEM
  NS_DECL_NSIWEBNAVIGATION
  NS_DECL_NSIBASEWINDOW
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIREFRESHURI
  NS_DECL_NSIWEBPAGEDESCRIPTOR
  NS_DECL_NSIAUTHPROMPTPROVIDER
  NS_DECL_NSINETWORKINTERCEPTCONTROLLER

  // Create a new nsDocShell object.
  static already_AddRefed<nsDocShell> Create(
      mozilla::dom::BrowsingContext* aBrowsingContext,
      uint64_t aContentWindowID = 0);

  bool Initialize();

  NS_IMETHOD Stop() override {
    // Need this here because otherwise nsIWebNavigation::Stop
    // overrides the docloader's Stop()
    return nsDocLoader::Stop();
  }

  mozilla::ScrollbarPreference ScrollbarPreference() const {
    return mScrollbarPref;
  }
  void SetScrollbarPreference(mozilla::ScrollbarPreference);

  /*
   * The size, in CSS pixels, of the margins for the <body> of an HTML document
   * in this docshell; used to implement the marginwidth attribute on HTML
   * <frame>/<iframe> elements.  A value smaller than zero indicates that the
   * attribute was not set.
   */
  const mozilla::CSSIntSize& GetFrameMargins() const { return mFrameMargins; }

  bool UpdateFrameMargins(const mozilla::CSSIntSize& aMargins) {
    if (mFrameMargins == aMargins) {
      return false;
    }
    mFrameMargins = aMargins;
    return true;
  }

  /**
   * Process a click on a link.
   *
   * @param aContent the content object used for triggering the link.
   * @param aURI a URI object that defines the destination for the link
   * @param aTargetSpec indicates where the link is targeted (may be an empty
   *        string)
   * @param aFileName non-null when the link should be downloaded as the given
   * file
   * @param aPostDataStream the POST data to send
   * @param aHeadersDataStream ??? (only used for plugins)
   * @param aTriggeringPrincipal, if not passed explicitly we fall back to
   *        the document's principal.
   * @param aCsp, the CSP to be used for the load, that is the CSP of the
   *        entity responsible for causing the load to occur. Most likely
   *        this is the CSP of the document that started the load. In case
   *        aCsp was not passed explicitly we fall back to using
   *        aContent's document's CSP if that document holds any.
   */
  nsresult OnLinkClick(nsIContent* aContent, nsIURI* aURI,
                       const nsAString& aTargetSpec, const nsAString& aFileName,
                       nsIInputStream* aPostDataStream,
                       nsIInputStream* aHeadersDataStream,
                       bool aIsUserTriggered,
                       nsIPrincipal* aTriggeringPrincipal,
                       nsIContentSecurityPolicy* aCsp);
  /**
   * Process a click on a link.
   *
   * Works the same as OnLinkClick() except it happens immediately rather than
   * through an event.
   *
   * @param aContent the content object used for triggering the link.
   * @param aDocShellLoadState the extended load info for this load.
   * @param aNoOpenerImplied if the link implies "noopener"
   * @param aTriggeringPrincipal, if not passed explicitly we fall back to
   *        the document's principal.
   */
  nsresult OnLinkClickSync(nsIContent* aContent,
                           nsDocShellLoadState* aLoadState,
                           bool aNoOpenerImplied,
                           nsIPrincipal* aTriggeringPrincipal);

  /**
   * Process a mouse-over a link.
   *
   * @param aContent the linked content.
   * @param aURI an URI object that defines the destination for the link
   * @param aTargetSpec indicates where the link is targeted (it may be an empty
   *        string)
   */
  nsresult OnOverLink(nsIContent* aContent, nsIURI* aURI,
                      const nsAString& aTargetSpec);
  /**
   * Process the mouse leaving a link.
   */
  nsresult OnLeaveLink();

  // Don't use NS_DECL_NSILOADCONTEXT because some of nsILoadContext's methods
  // are shared with nsIDocShell and can't be declared twice.
  NS_IMETHOD GetAssociatedWindow(mozIDOMWindowProxy**) override;
  NS_IMETHOD GetTopWindow(mozIDOMWindowProxy**) override;
  NS_IMETHOD GetTopFrameElement(mozilla::dom::Element**) override;
  NS_IMETHOD GetIsContent(bool*) override;
  NS_IMETHOD GetUsePrivateBrowsing(bool*) override;
  NS_IMETHOD SetUsePrivateBrowsing(bool) override;
  NS_IMETHOD SetPrivateBrowsing(bool) override;
  NS_IMETHOD GetUseRemoteTabs(bool*) override;
  NS_IMETHOD SetRemoteTabs(bool) override;
  NS_IMETHOD GetUseRemoteSubframes(bool*) override;
  NS_IMETHOD SetRemoteSubframes(bool) override;
  NS_IMETHOD GetScriptableOriginAttributes(
      JSContext*, JS::MutableHandle<JS::Value>) override;
  NS_IMETHOD_(void)
  GetOriginAttributes(mozilla::OriginAttributes& aAttrs) override;

  // Restores a cached presentation from history (mLSHE).
  // This method swaps out the content viewer and simulates loads for
  // subframes. It then simulates the completion of the toplevel load.
  nsresult RestoreFromHistory();

  /**
   * Parses the passed in header string and sets up a refreshURI if a "refresh"
   * header is found. If docshell is busy loading a page currently, the request
   * will be queued and executed when the current page finishes loading.
   *
   * @param aDocument    document to which the refresh header applies.
   * @param aHeader      The meta refresh header string.
   */
  void SetupRefreshURIFromHeader(mozilla::dom::Document* aDocument,
                                 const nsAString& aHeader);

  // Perform a URI load from a refresh timer. This is just like the
  // ForceRefreshURI method on nsIRefreshURI, but makes sure to take
  // the timer involved out of mRefreshURIList if it's there.
  // aTimer must not be null.
  nsresult ForceRefreshURIFromTimer(nsIURI* aURI, nsIPrincipal* aPrincipal,
                                    uint32_t aDelay, nsITimer* aTimer);

  // We need dummy OnLocationChange in some cases to update the UI without
  // updating security info.
  void FireDummyOnLocationChange() {
    FireOnLocationChange(this, nullptr, mCurrentURI,
                         LOCATION_CHANGE_SAME_DOCUMENT);
  }

  nsresult HistoryEntryRemoved(int32_t aIndex);

  // Notify Scroll observers when an async panning/zooming transform
  // has started being applied
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void NotifyAsyncPanZoomStarted();

  // Notify Scroll observers when an async panning/zooming transform
  // is no longer applied
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void NotifyAsyncPanZoomStopped();

  void SetInFrameSwap(bool aInSwap) { mInFrameSwap = aInSwap; }
  bool InFrameSwap();

  bool GetForcedAutodetection() { return mForcedAutodetection; }

  void ResetForcedAutodetection() { mForcedAutodetection = false; }

  mozilla::HTMLEditor* GetHTMLEditorInternal();
  nsresult SetHTMLEditorInternal(mozilla::HTMLEditor* aHTMLEditor);

  // Handle page navigation due to charset changes
  nsresult CharsetChangeReloadDocument(
      mozilla::NotNull<const mozilla::Encoding*> aEncoding, int32_t aSource);
  nsresult CharsetChangeStopDocumentLoad();

  nsDOMNavigationTiming* GetNavigationTiming() const;

  nsresult SetOriginAttributes(const mozilla::OriginAttributes& aAttrs);

  const mozilla::OriginAttributes& GetOriginAttributes() {
    return mBrowsingContext->OriginAttributesRef();
  }

  // Determine whether this docshell corresponds to the given history entry,
  // via having a pointer to it in mOSHE or mLSHE.
  bool HasHistoryEntry(nsISHEntry* aEntry) const {
    return aEntry && (aEntry == mOSHE || aEntry == mLSHE);
  }

  // Update any pointers (mOSHE or mLSHE) to aOldEntry to point to aNewEntry
  void SwapHistoryEntries(nsISHEntry* aOldEntry, nsISHEntry* aNewEntry);

  bool GetCreatedDynamically() const {
    return mBrowsingContext && mBrowsingContext->CreatedDynamically();
  }

  mozilla::gfx::Matrix5x4* GetColorMatrix() { return mColorMatrix.get(); }

  static bool SandboxFlagsImplyCookies(const uint32_t& aSandboxFlags);

  // Tell the favicon service that aNewURI has the same favicon as aOldURI.
  static void CopyFavicon(nsIURI* aOldURI, nsIURI* aNewURI,
                          bool aInPrivateBrowsing);

  static nsDocShell* Cast(nsIDocShell* aDocShell) {
    return static_cast<nsDocShell*>(aDocShell);
  }

  static bool CanLoadInParentProcess(nsIURI* aURI);

  // Returns true if the current load is a force reload (started by holding
  // shift while triggering reload)
  bool IsForceReloading();

  mozilla::dom::WindowProxyHolder GetWindowProxy() {
    EnsureScriptEnvironment();
    return mozilla::dom::WindowProxyHolder(mBrowsingContext);
  }

  /**
   * Loads the given URI. See comments on nsDocShellLoadState members for more
   * information on information used.
   * `aCacheKey` gets passed to DoURILoad call.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult InternalLoad(
      nsDocShellLoadState* aLoadState,
      mozilla::Maybe<uint32_t> aCacheKey = mozilla::Nothing());

  void MaybeRestoreWindowName();

  void StoreWindowNameToSHEntries();

  void SetWillChangeProcess() { mWillChangeProcess = true; }
  bool WillChangeProcess() { return mWillChangeProcess; }

  // Create a content viewer within this nsDocShell for the given
  // `WindowGlobalChild` actor.
  nsresult CreateDocumentViewerForActor(
      mozilla::dom::WindowGlobalChild* aWindowActor);

  // Creates a real network channel (not a DocumentChannel) using the specified
  // parameters.
  // Used by nsDocShell when not using DocumentChannel, by DocumentLoadListener
  // (parent-process DocumentChannel), and by DocumentChannelChild/ContentChild
  // to transfer the resulting channel into the final process.
  static nsresult CreateRealChannelForDocument(
      nsIChannel** aChannel, nsIURI* aURI, nsILoadInfo* aLoadInfo,
      nsIInterfaceRequestor* aCallbacks, nsLoadFlags aLoadFlags,
      const nsAString& aSrcdoc, nsIURI* aBaseURI);

  // Creates a real (not DocumentChannel) channel, and configures it using the
  // supplied nsDocShellLoadState.
  // Configuration options here are ones that should be applied to only the
  // real channel, especially ones that need to QI to channel subclasses.
  static bool CreateAndConfigureRealChannelForLoadState(
      mozilla::dom::BrowsingContext* aBrowsingContext,
      nsDocShellLoadState* aLoadState, mozilla::net::LoadInfo* aLoadInfo,
      nsIInterfaceRequestor* aCallbacks, nsDocShell* aDocShell,
      const mozilla::OriginAttributes& aOriginAttributes,
      nsLoadFlags aLoadFlags, uint32_t aCacheKey, nsresult& rv,
      nsIChannel** aChannel);

  // This is used to deal with errors resulting from a failed page load.
  // Errors are handled as follows:
  //   1. Check to see if it's a file not found error or bad content
  //      encoding error.
  //   2. Send the URI to a keyword server (if enabled)
  //   3. If the error was DNS failure, then add www and .com to the URI
  //      (if appropriate).
  //   4. If the www .com additions don't work, try those with an HTTPS scheme
  //      (if appropriate).
  static already_AddRefed<nsIURI> AttemptURIFixup(
      nsIChannel* aChannel, nsresult aStatus,
      const mozilla::Maybe<nsCString>& aOriginalURIString, uint32_t aLoadType,
      bool aIsTopFrame, bool aAllowKeywordFixup, bool aUsePrivateBrowsing,
      bool aNotifyKeywordSearchLoading = false,
      nsIInputStream** aNewPostData = nullptr,
      nsILoadInfo::SchemelessInputType* outSchemelessInput = nullptr);

  static already_AddRefed<nsIURI> MaybeFixBadCertDomainErrorURI(
      nsIChannel* aChannel, nsIURI* aUrl);

  // Takes aStatus and filters out results that should not display
  // an error page.
  // If this returns a failed result, then we should display an error
  // page with that result.
  // aSkippedUnknownProtocolNavigation will be set to true if we chose
  // to skip displaying an error page for an NS_ERROR_UNKNOWN_PROTOCOL
  // navigation.
  static nsresult FilterStatusForErrorPage(
      nsresult aStatus, nsIChannel* aChannel, uint32_t aLoadType,
      bool aIsTopFrame, bool aUseErrorPages,
      bool* aSkippedUnknownProtocolNavigation = nullptr);

  // Notify consumers of a search being loaded through the observer service:
  static void MaybeNotifyKeywordSearchLoading(const nsString& aProvider,
                                              const nsString& aKeyword);

  nsDocShell* GetInProcessChildAt(int32_t aIndex);

  static bool ShouldAddURIVisit(nsIChannel* aChannel);

  /**
   * Helper function that finds the last URI and its transition flags for a
   * channel.
   *
   * This method first checks the channel's property bag to see if previous
   * info has been saved. If not, it gives back the referrer of the channel.
   *
   * @param aChannel
   *        The channel we are transitioning to
   * @param aURI
   *        Output parameter with the previous URI, not addref'd
   * @param aChannelRedirectFlags
   *        If a redirect, output parameter with the previous redirect flags
   *        from nsIChannelEventSink
   */
  static void ExtractLastVisit(nsIChannel* aChannel, nsIURI** aURI,
                               uint32_t* aChannelRedirectFlags);

  bool HasDocumentViewer() const { return !!mDocumentViewer; }

  static uint32_t ComputeURILoaderFlags(
      mozilla::dom::BrowsingContext* aBrowsingContext, uint32_t aLoadType,
      bool aIsDocumentLoad = true);

  mozilla::dom::SessionHistoryInfo* GetActiveSessionHistoryInfo() const;

  void SetLoadingSessionHistoryInfo(
      const mozilla::dom::LoadingSessionHistoryInfo& aLoadingInfo,
      bool aNeedToReportActiveAfterLoadingBecomesActive = false);
  const mozilla::dom::LoadingSessionHistoryInfo*
  GetLoadingSessionHistoryInfo() {
    return mLoadingEntry.get();
  }

  already_AddRefed<nsIInputStream> GetPostDataFromCurrentEntry() const;
  mozilla::Maybe<uint32_t> GetCacheKeyFromCurrentEntry() const;

  // Loading and/or active entries are only set when session history
  // in the parent is on.
  bool FillLoadStateFromCurrentEntry(nsDocShellLoadState& aLoadState);

  static bool ShouldAddToSessionHistory(nsIURI* aURI, nsIChannel* aChannel);

  bool IsOSHE(nsISHEntry* aEntry) const { return mOSHE == aEntry; }

  mozilla::dom::ChildSHistory* GetSessionHistory() {
    return mBrowsingContext->GetChildSessionHistory();
  }

  // This returns true only when using session history in parent.
  bool IsLoadingFromSessionHistory();

  NS_IMETHODIMP OnStartRequest(nsIRequest* aRequest) override;
  NS_IMETHODIMP OnStopRequest(nsIRequest* aRequest,
                              nsresult aStatusCode) override;

 private:  // member functions
  friend class nsAppShellService;
  friend class nsDSURIContentListener;
  friend class FramingChecker;
  friend class OnLinkClickEvent;
  friend class nsIDocShell;
  friend class mozilla::dom::BrowsingContext;
  friend class mozilla::net::DocumentLoadListener;
  friend class nsGlobalWindowOuter;

  nsDocShell(mozilla::dom::BrowsingContext* aBrowsingContext,
             uint64_t aContentWindowID);

  static inline uint32_t PRTimeToSeconds(PRTime aTimeUsec) {
    return uint32_t(aTimeUsec / PR_USEC_PER_SEC);
  }

  virtual ~nsDocShell();

  //
  // nsDocLoader
  //

  virtual void DestroyChildren() override;

  // Overridden from nsDocLoader, this provides more information than the
  // normal OnStateChange with flags STATE_REDIRECTING
  virtual void OnRedirectStateChange(nsIChannel* aOldChannel,
                                     nsIChannel* aNewChannel,
                                     uint32_t aRedirectFlags,
                                     uint32_t aStateFlags) override;

  // Override the parent setter from nsDocLoader
  virtual nsresult SetDocLoaderParent(nsDocLoader* aLoader) override;

  //
  // Content Viewer Management
  //

  nsresult EnsureDocumentViewer();

  // aPrincipal can be passed in if the caller wants. If null is
  // passed in, the about:blank principal will end up being used.
  // aCSP, if any, will be used for the new about:blank load.
  nsresult CreateAboutBlankDocumentViewer(
      nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal,
      nsIContentSecurityPolicy* aCSP, nsIURI* aBaseURI, bool aIsInitialDocument,
      const mozilla::Maybe<nsILoadInfo::CrossOriginEmbedderPolicy>& aCOEP =
          mozilla::Nothing(),
      bool aTryToSaveOldPresentation = true, bool aCheckPermitUnload = true,
      mozilla::dom::WindowGlobalChild* aActor = nullptr);

  nsresult CreateDocumentViewer(const nsACString& aContentType,
                                nsIRequest* aRequest,
                                nsIStreamListener** aContentHandler);

  nsresult NewDocumentViewerObj(const nsACString& aContentType,
                                nsIRequest* aRequest, nsILoadGroup* aLoadGroup,
                                nsIStreamListener** aContentHandler,
                                nsIDocumentViewer** aViewer);

  already_AddRefed<nsILoadURIDelegate> GetLoadURIDelegate();

  nsresult SetupNewViewer(
      nsIDocumentViewer* aNewViewer,
      mozilla::dom::WindowGlobalChild* aWindowActor = nullptr);

  //
  // Session History
  //

  // Either aChannel or aOwner must be null. If aChannel is
  // present, the owner should be gotten from it.
  // If aCloneChildren is true, then our current session history's
  // children will be cloned onto the new entry. This should be
  // used when we aren't actually changing the document while adding
  // the new session history entry.
  // aCsp is the CSP to be used for the load. That is *not* the CSP
  // that will be applied to subresource loads within that document
  // but the CSP for the document load itself. E.g. if that CSP
  // includes upgrade-insecure-requests, then the new top-level load
  // will be upgraded to HTTPS.
  nsresult AddToSessionHistory(nsIURI* aURI, nsIChannel* aChannel,
                               nsIPrincipal* aTriggeringPrincipal,
                               nsIPrincipal* aPrincipalToInherit,
                               nsIPrincipal* aPartitionedPrincipalToInherit,
                               nsIContentSecurityPolicy* aCsp,
                               bool aCloneChildren, nsISHEntry** aNewEntry);

  void UpdateActiveEntry(
      bool aReplace, const mozilla::Maybe<nsPoint>& aPreviousScrollPos,
      nsIURI* aURI, nsIURI* aOriginalURI, nsIReferrerInfo* aReferrerInfo,
      nsIPrincipal* aTriggeringPrincipal, nsIContentSecurityPolicy* aCsp,
      const nsAString& aTitle, bool aScrollRestorationIsManual,
      nsIStructuredCloneContainer* aData, bool aURIWasModified);

  nsresult AddChildSHEntry(nsISHEntry* aCloneRef, nsISHEntry* aNewEntry,
                           int32_t aChildOffset, uint32_t aLoadType,
                           bool aCloneChildren);

  nsresult AddChildSHEntryToParent(nsISHEntry* aNewEntry, int32_t aChildOffset,
                                   bool aCloneChildren);

  // Call this method to swap in a new history entry to m[OL]SHE, rather than
  // setting it directly. This completes the navigation in all docshells
  // in the case of a subframe navigation.
  // Returns old mOSHE/mLSHE.
  already_AddRefed<nsISHEntry> SetHistoryEntry(nsCOMPtr<nsISHEntry>* aPtr,
                                               nsISHEntry* aEntry);

  // This method calls SetHistoryEntry and updates mOSHE and mLSHE in BC to be
  // the same as in docshell
  void SetHistoryEntryAndUpdateBC(const mozilla::Maybe<nsISHEntry*>& aLSHE,
                                  const mozilla::Maybe<nsISHEntry*>& aOSHE);

  // If aNotifiedBeforeUnloadListeners is true, "beforeunload" event listeners
  // were notified by the caller and given the chance to abort the navigation,
  // and should not be notified again.
  static nsresult ReloadDocument(
      nsDocShell* aDocShell, mozilla::dom::Document* aDocument,
      uint32_t aLoadType, mozilla::dom::BrowsingContext* aBrowsingContext,
      nsIURI* aCurrentURI, nsIReferrerInfo* aReferrerInfo,
      bool aNotifiedBeforeUnloadListeners = false);

 public:
  bool IsAboutBlankLoadOntoInitialAboutBlank(nsIURI* aURI,
                                             bool aInheritPrincipal,
                                             nsIPrincipal* aPrincipalToInherit);

 private:
  //
  // URI Load
  //

  // Actually open a channel and perform a URI load. Callers need to pass a
  // non-null aLoadState->TriggeringPrincipal() which initiated the URI load.
  // Please note that the TriggeringPrincipal will be used for performing
  // security checks. If aLoadState->URI() is provided by the web, then please
  // do not pass a SystemPrincipal as the triggeringPrincipal. If
  // aLoadState()->PrincipalToInherit is null, then no inheritance of any sort
  // will happen and the load will get a principal based on the URI being
  // loaded. If the Srcdoc flag is set (INTERNAL_LOAD_FLAGS_IS_SRCDOC), the load
  // will be considered as a srcdoc load, and the contents of Srcdoc will be
  // loaded instead of the URI. aLoadState->OriginalURI() will be set as the
  // originalURI on the channel that does the load. If OriginalURI is null, URI
  // will be set as the originalURI. If LoadReplace is true, LOAD_REPLACE flag
  // will be set on the nsIChannel.
  // If `aCacheKey` is supplied, use it for the session history entry.
  nsresult DoURILoad(nsDocShellLoadState* aLoadState,
                     mozilla::Maybe<uint32_t> aCacheKey, nsIRequest** aRequest);

  static nsresult AddHeadersToChannel(nsIInputStream* aHeadersData,
                                      nsIChannel* aChannel);

  nsresult OpenInitializedChannel(nsIChannel* aChannel,
                                  nsIURILoader* aURILoader,
                                  uint32_t aOpenFlags);
  nsresult OpenRedirectedChannel(nsDocShellLoadState* aLoadState);

  void UpdateMixedContentChannelForNewLoad(nsIChannel* aChannel);

  MOZ_CAN_RUN_SCRIPT
  nsresult ScrollToAnchor(bool aCurHasRef, bool aNewHasRef,
                          nsACString& aNewHash, uint32_t aLoadType);

  // This returns the load type for a form submission (see
  // https://html.spec.whatwg.org/#form-submission-algorithm). The load type
  // should be set as soon as the target BC has been determined.
  uint32_t GetLoadTypeForFormSubmission(
      mozilla::dom::BrowsingContext* aTargetBC,
      nsDocShellLoadState* aLoadState);

 private:
  // Returns true if it is the caller's responsibility to ensure
  // FireOnLocationChange is called.
  // In all other cases false is returned.
  // Either aChannel or aTriggeringPrincipal must be null. If aChannel is
  // present, the owner should be gotten from it.
  // If OnNewURI calls AddToSessionHistory, it will pass its
  // aCloneSHChildren argument as aCloneChildren.
  // aCsp is the CSP to be used for the load. That is *not* the CSP
  // that will be applied to subresource loads within that document
  // but the CSP for the document load itself. E.g. if that CSP
  // includes upgrade-insecure-requests, then the new top-level load
  // will be upgraded to HTTPS.
  bool OnNewURI(nsIURI* aURI, nsIChannel* aChannel,
                nsIPrincipal* aTriggeringPrincipal,
                nsIPrincipal* aPrincipalToInherit,
                nsIPrincipal* aPartitionedPrincipalToInherit,
                nsIContentSecurityPolicy* aCsp, bool aAddToGlobalHistory,
                bool aCloneSHChildren);

 public:
  // If wireframe collection is enabled, will attempt to gather the
  // wireframe for the document.
  mozilla::Maybe<mozilla::dom::Wireframe> GetWireframe();

  // If wireframe collection is enabled, will attempt to gather the
  // wireframe for the document and stash it inside of the active history
  // entry. Returns true if wireframes were collected.
  bool CollectWireframe();

  // Helper method that is called when a new document (including any
  // sub-documents - ie. frames) has been completely loaded.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult EndPageLoad(nsIWebProgress* aProgress, nsIChannel* aChannel,
                       nsresult aResult);

  // Builds an error page URI (e.g. about:neterror?etc) for the given aURI
  // and displays it via the LoadErrorPage() overload below.
  nsresult LoadErrorPage(nsIURI* aURI, const char16_t* aURL,
                         const char* aErrorPage, const char* aErrorType,
                         const char16_t* aDescription, const char* aCSSClass,
                         nsIChannel* aFailedChannel);

  // This method directly loads aErrorURI as an error page. aFailedURI and
  // aFailedChannel come from DisplayLoadError() or the LoadErrorPage() overload
  // above.
  nsresult LoadErrorPage(nsIURI* aErrorURI, nsIURI* aFailedURI,
                         nsIChannel* aFailedChannel);

  bool DisplayLoadError(nsresult aError, nsIURI* aURI, const char16_t* aURL,
                        nsIChannel* aFailedChannel) {
    bool didDisplayLoadError = false;
    DisplayLoadError(aError, aURI, aURL, aFailedChannel, &didDisplayLoadError);
    return didDisplayLoadError;
  }

  //
  // Uncategorized
  //

  // Get the principal that we'll set on the channel if we're inheriting. If
  // aConsiderCurrentDocument is true, we try to use the current document if
  // at all possible. If that fails, we fall back on the parent document.
  // If that fails too, we force creation of a content viewer and use the
  // resulting principal. If aConsiderCurrentDocument is false, we just look
  // at the parent.
  // If aConsiderPartitionedPrincipal is true, we consider the partitioned
  // principal instead of the node principal.
  nsIPrincipal* GetInheritedPrincipal(
      bool aConsiderCurrentDocument,
      bool aConsiderPartitionedPrincipal = false);

  /**
   * Helper function that caches a URI and a transition for saving later.
   *
   * @param aChannel
   *        Channel that will have these properties saved
   * @param aURI
   *        The URI to save for later
   * @param aChannelRedirectFlags
   *        The nsIChannelEventSink redirect flags to save for later
   */
  static void SaveLastVisit(nsIChannel* aChannel, nsIURI* aURI,
                            uint32_t aChannelRedirectFlags);

  /**
   * Helper function for adding a URI visit using IHistory.
   *
   * The IHistory API maintains chains of visits, tracking both HTTP referrers
   * and redirects for a user session. VisitURI requires the current URI and
   * the previous URI in the chain.
   *
   * Visits can be saved either during a redirect or when the request has
   * reached its final destination. The previous URI in the visit may be
   * from another redirect.
   *
   * @pre aURI is not null.
   *
   * @param aURI
   *        The URI that was just visited
   * @param aPreviousURI
   *        The previous URI of this visit
   * @param aChannelRedirectFlags
   *        For redirects, the redirect flags from nsIChannelEventSink
   *        (0 otherwise)
   * @param aResponseStatus
   *        For HTTP channels, the response code (0 otherwise).
   */
  void AddURIVisit(nsIURI* aURI, nsIURI* aPreviousURI,
                   uint32_t aChannelRedirectFlags,
                   uint32_t aResponseStatus = 0);

  /**
   * Internal helper funtion
   */
  static void InternalAddURIVisit(
      nsIURI* aURI, nsIURI* aPreviousURI, uint32_t aChannelRedirectFlags,
      uint32_t aResponseStatus, mozilla::dom::BrowsingContext* aBrowsingContext,
      nsIWidget* aWidget, uint32_t aLoadType, bool aWasUpgraded);

  static already_AddRefed<nsIURIFixupInfo> KeywordToURI(
      const nsACString& aKeyword, bool aIsPrivateContext);

  // Sets the current document's current state object to the given SHEntry's
  // state object. The current state object is eventually given to the page
  // in the PopState event.
  void SetDocCurrentStateObj(nsISHEntry* aShEntry,
                             mozilla::dom::SessionHistoryInfo* aInfo);

  // Returns true if would have called FireOnLocationChange,
  // but did not because aFireOnLocationChange was false on entry.
  // In this case it is the caller's responsibility to ensure
  // FireOnLocationChange is called.
  // In all other cases false is returned.
  bool SetCurrentURI(nsIURI* aURI, nsIRequest* aRequest,
                     bool aFireOnLocationChange, bool aIsInitialAboutBlank,
                     uint32_t aLocationFlags);

  // The following methods deal with saving and restoring content viewers
  // in session history.

  // mDocumentViewer points to the current content viewer associated with
  // this docshell. When loading a new document, the content viewer is
  // either destroyed or stored into a session history entry. To make sure
  // that destruction happens in a controlled fashion, a given content viewer
  // is always owned in exactly one of these ways:
  //   1) The content viewer is active and owned by a docshell's
  //      mDocumentViewer.
  //   2) The content viewer is still being displayed while we begin loading
  //      a new document. The content viewer is owned by the _new_
  //      content viewer's mPreviousViewer, and has a pointer to the
  //      nsISHEntry where it will eventually be stored. The content viewer
  //      has been close()d by the docshell, which detaches the document from
  //      the window object.
  //   3) The content viewer is cached in session history. The nsISHEntry
  //      has the only owning reference to the content viewer. The viewer
  //      has released its nsISHEntry pointer to prevent circular ownership.
  //
  // When restoring a content viewer from session history, open() is called
  // to reattach the document to the window object. The content viewer is
  // then placed into mDocumentViewer and removed from the history entry.
  // (mDocumentViewer is put into session history as described above, if
  // applicable).

  // Determines whether we can safely cache the current mDocumentViewer in
  // session history. This checks a number of factors such as cache policy,
  // pending requests, and unload handlers.
  // |aLoadType| should be the load type that will replace the current
  // presentation. |aNewRequest| should be the request for the document to
  // be loaded in place of the current document, or null if such a request
  // has not been created yet. |aNewDocument| should be the document that will
  // replace the current document.
  bool CanSavePresentation(uint32_t aLoadType, nsIRequest* aNewRequest,
                           mozilla::dom::Document* aNewDocument,
                           bool aReportBFCacheComboTelemetry);

  static void ReportBFCacheComboTelemetry(uint32_t aCombo);

  // Captures the state of the supporting elements of the presentation
  // (the "window" object, docshell tree, meta-refresh loads, and security
  // state) and stores them on |mOSHE|.
  nsresult CaptureState();

  // Begin the toplevel restore process for |aSHEntry|.
  // This simulates a channel open, and defers the real work until
  // RestoreFromHistory is called from a PLEvent.
  nsresult RestorePresentation(nsISHEntry* aSHEntry, bool* aRestoring);

  // Call BeginRestore(nullptr, false) for each child of this shell.
  nsresult BeginRestoreChildren();

  // Method to get our current position and size without flushing
  void DoGetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aWidth,
                            int32_t* aHeight);

  // Call this when a URI load is handed to us (via OnLinkClick or
  // InternalLoad). This makes sure that we're not inside unload, or that if
  // we are it's still OK to load this URI.
  bool IsOKToLoadURI(nsIURI* aURI);

  // helpers for executing commands
  nsresult GetControllerForCommand(const char* aCommand,
                                   nsIController** aResult);

  // Possibly create a ClientSource object to represent an initial about:blank
  // window that has not been allocated yet.  Normally we try not to create
  // this about:blank window until something calls GetDocument().  We still need
  // the ClientSource to exist for this conceptual window, though.
  //
  // The ClientSource is created with the given principal if specified.  If
  // the principal is not provided we will attempt to inherit it when we
  // are sure it will match what the real about:blank window principal
  // would have been.  There are some corner cases where we cannot easily
  // determine the correct principal and will not create the ClientSource.
  // In these cases the initial about:blank will appear to not exist until
  // its real document and window are created.
  void MaybeCreateInitialClientSource(nsIPrincipal* aPrincipal = nullptr);

  // Determine if a service worker is allowed to control a window in this
  // docshell with the given URL.  If there are any reasons it should not,
  // this will return false.  If true is returned then the window *may* be
  // controlled.  The caller must still consult either the parent controller
  // or the ServiceWorkerManager to determine if a service worker should
  // actually control the window.
  bool ServiceWorkerAllowedToControlWindow(nsIPrincipal* aPrincipal,
                                           nsIURI* aURI);

  // Return the ClientInfo for the initial about:blank window, if it exists
  // or we have speculatively created a ClientSource in
  // MaybeCreateInitialClientSource().  This can return a ClientInfo object
  // even if GetExtantDoc() returns nullptr.
  mozilla::Maybe<mozilla::dom::ClientInfo> GetInitialClientInfo() const;

  /**
   * Initializes mTiming if it isn't yet.
   * After calling this, mTiming is non-null. This method returns true if the
   * initialization of the Timing can be reset (basically this is true if a new
   * Timing object is created).
   * In case the loading is aborted, MaybeResetInitTiming() can be called
   * passing the return value of MaybeInitTiming(): if it's possible to reset
   * the Timing, this method will do it.
   */
  [[nodiscard]] bool MaybeInitTiming();
  void MaybeResetInitTiming(bool aReset);

  // Convenience method for getting our parent docshell. Can return null
  already_AddRefed<nsDocShell> GetInProcessParentDocshell();

  // Internal implementation of nsIDocShell::FirePageHideNotification.
  // If aSkipCheckingDynEntries is true, it will not try to remove dynamic
  // subframe entries. This is to avoid redundant RemoveDynEntries calls in all
  // children docshells.
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FirePageHideNotificationInternal(
      bool aIsUnload, bool aSkipCheckingDynEntries);

  void ThawFreezeNonRecursive(bool aThaw);
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FirePageHideShowNonRecursive(bool aShow);

  nsresult Dispatch(already_AddRefed<nsIRunnable>&& aRunnable);

  void ReattachEditorToWindow(nsISHEntry* aSHEntry);
  void ClearFrameHistory(nsISHEntry* aEntry);
  // Determine if this type of load should update history.
  static bool ShouldUpdateGlobalHistory(uint32_t aLoadType);
  void UpdateGlobalHistoryTitle(nsIURI* aURI);
  bool IsSubframe() { return mBrowsingContext->IsSubframe(); }
  bool CanSetOriginAttributes();
  bool ShouldBlockLoadingForBackButton();
  static bool ShouldDiscardLayoutState(nsIHttpChannel* aChannel);
  bool HasUnloadedParent();
  bool JustStartedNetworkLoad();
  bool NavigationBlockedByPrinting(bool aDisplayErrorDialog = true);
  bool IsNavigationAllowed(bool aDisplayPrintErrorDialog = true,
                           bool aCheckIfUnloadFired = true);
  mozilla::ScrollContainerFrame* GetRootScrollContainerFrame();
  nsIChannel* GetCurrentDocChannel();
  nsresult EnsureScriptEnvironment();
  nsresult EnsureEditorData();
  nsresult EnsureTransferableHookData();
  nsresult EnsureFind();
  nsresult EnsureCommandHandler();
  nsresult RefreshURIFromQueue();
  void RefreshURIToQueue();
  nsresult Embed(nsIDocumentViewer* aDocumentViewer,
                 mozilla::dom::WindowGlobalChild* aWindowActor,
                 bool aIsTransientAboutBlank, bool aPersist,
                 nsIRequest* aRequest, nsIURI* aPreviousURI);
  nsPresContext* GetEldestPresContext();
  nsresult CheckLoadingPermissions();
  nsresult LoadHistoryEntry(nsISHEntry* aEntry, uint32_t aLoadType,
                            bool aUserActivation);
  nsresult LoadHistoryEntry(
      const mozilla::dom::LoadingSessionHistoryInfo& aEntry, uint32_t aLoadType,
      bool aUserActivation);
  nsresult LoadHistoryEntry(nsDocShellLoadState* aLoadState, uint32_t aLoadType,
                            bool aLoadingCurrentEntry);
  nsresult GetHttpChannel(nsIChannel* aChannel, nsIHttpChannel** aReturn);
  nsresult ConfirmRepost(bool* aRepost);
  nsresult GetPromptAndStringBundle(nsIPrompt** aPrompt,
                                    nsIStringBundle** aStringBundle);
  nsresult SetCurScrollPosEx(int32_t aCurHorizontalPos,
                             int32_t aCurVerticalPos);
  nsPoint GetCurScrollPos();

  already_AddRefed<mozilla::dom::ChildSHistory> GetRootSessionHistory();

  bool CSSErrorReportingEnabled() const { return mCSSErrorReportingEnabled; }

  // Handles retrieval of subframe session history for nsDocShell::LoadURI. If a
  // load is requested in a subframe of the current DocShell, the subframe
  // loadType may need to reflect the loadType of the parent document, or in
  // some cases (like reloads), the history load may need to be cancelled. See
  // function comments for in-depth logic descriptions.
  // Returns true if the method itself deals with the load.
  bool MaybeHandleSubframeHistory(nsDocShellLoadState* aLoadState,
                                  bool aContinueHandlingSubframeHistory);

  // If we are passed a named target during InternalLoad, this method handles
  // moving the load to the browsing context the target name resolves to.
  nsresult PerformRetargeting(nsDocShellLoadState* aLoadState);

  // Returns one of nsIContentPolicy::TYPE_DOCUMENT,
  // nsIContentPolicy::TYPE_INTERNAL_IFRAME, or
  // nsIContentPolicy::TYPE_INTERNAL_FRAME depending on who is responsible for
  // this docshell.
  nsContentPolicyType DetermineContentType();

  // If this is an iframe, and the embedder is OOP, then notifes the
  // embedder that loading has finished and we shouldn't be blocking
  // load of the embedder. Only called when we fail to load, as we wait
  // for the load event of our Document before notifying success.
  //
  // If aFireFrameErrorEvent is true, then fires an error event at the
  // embedder element, for both in-process and OOP embedders.
  void UnblockEmbedderLoadEventForFailure(bool aFireFrameErrorEvent = false);

  // Check to see if we're loading a prior history entry or doing a fragment
  // navigation in the same document.
  // NOTE: In case we are doing a fragment navigation, and HTTPS-Only/ -First
  // mode is enabled and upgraded the underlying document, we update the URI of
  // aLoadState from HTTP to HTTPS (if neccessary).
  bool IsSameDocumentNavigation(nsDocShellLoadState* aLoadState,
                                SameDocumentNavigationState& aState);

  // ... If so, handle the scrolling or other action required instead of
  // continuing with new document navigation.
  MOZ_CAN_RUN_SCRIPT
  nsresult HandleSameDocumentNavigation(nsDocShellLoadState* aLoadState,
                                        SameDocumentNavigationState& aState,
                                        bool& aSameDocument);

  uint32_t GetSameDocumentNavigationFlags(nsIURI* aNewURI);

  // Called when the Private Browsing state of a nsDocShell changes.
  void NotifyPrivateBrowsingChanged();

  // Internal helpers for BrowsingContext to pass update values to nsIDocShell's
  // LoadGroup.
  void SetLoadGroupDefaultLoadFlags(nsLoadFlags aLoadFlags);

  void SetTitleOnHistoryEntry(bool aUpdateEntryInSessionHistory);

  void SetScrollRestorationIsManualOnHistoryEntry(nsISHEntry* aSHEntry,
                                                  bool aIsManual);

  void SetCacheKeyOnHistoryEntry(nsISHEntry* aSHEntry, uint32_t aCacheKey);

  // If the LoadState's URI is a javascript: URI, checks that the triggering
  // principal subsumes the principal of the current document, and returns
  // NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI if it does not.
  nsresult CheckDisallowedJavascriptLoad(nsDocShellLoadState* aLoadState);

  nsresult LoadURI(nsDocShellLoadState* aLoadState, bool aSetNavigating,
                   bool aContinueHandlingSubframeHistory);

  // Sets the active entry to the current loading entry. aPersist is used in the
  // case a new session history entry is added to the session history.
  // aExpired is true if the relevant nsIChannel has its cache token expired.
  // aCacheKey is the channel's cache key.
  // aPreviousURI should be the URI that was previously loaded into the
  // nsDocshell
  void MoveLoadingToActiveEntry(bool aPersist, bool aExpired,
                                uint32_t aCacheKey, nsIURI* aPreviousURI);

  void ActivenessMaybeChanged();

  /**
   * Returns true if `noopener` will be force-enabled by any attempt to create
   * a popup window, even if rel="opener" is requested.
   */
  bool NoopenerForceEnabled();

  bool ShouldOpenInBlankTarget(const nsAString& aOriginalTarget,
                               nsIURI* aLinkURI, nsIContent* aContent,
                               bool aIsUserTriggered);

  void RecordSingleChannelId(bool aStartRequest, nsIRequest* aRequest);

  void SetChannelToDisconnectOnPageHide(uint64_t aChannelId) {
    MOZ_ASSERT(mChannelToDisconnectOnPageHide == 0);
    mChannelToDisconnectOnPageHide = aChannelId;
  }
  void MaybeDisconnectChildListenersOnPageHide();

  /**
   * Helper for addState and document.open that does just the
   * history-manipulation guts.
   *
   * Arguments the spec defines:
   *
   * @param aDocument the document we're manipulating.  This will get the new
   * URI.
   * @param aNewURI the new URI.
   * @param aData The serialized state data.  May be null.
   * @param aHistoryHandling how to handle updating the history entries.
   *
   * Arguments we need internally because deriving them from the
   * others is a bit complicated:
   *
   * @param aCurrentURI the current URI we're working with.  Might be null.
   * @param aEqualURIs whether the two URIs involved are equal.
   */
  nsresult UpdateURLAndHistory(
      mozilla::dom::Document* aDocument, nsIURI* aNewURI,
      nsIStructuredCloneContainer* aData,
      mozilla::dom::NavigationHistoryBehavior aHistoryHandling,
      nsIURI* aCurrentURI, bool aEqualURIs);

  bool IsSameDocumentAsActiveEntry(
      const mozilla::dom::SessionHistoryInfo& aSHInfo);

  MOZ_CAN_RUN_SCRIPT nsresult
  ReloadNavigable(JSContext* aCx, uint32_t aReloadFlags,
                  nsIStructuredCloneContainer* aNavigationAPIState = nullptr,
                  mozilla::dom::UserNavigationInvolvement aUserInvolvement =
                      mozilla::dom::UserNavigationInvolvement::None);

 private:
  void SetCurrentURIInternal(nsIURI* aURI);

  // data members
  nsString mTitle;
  nsCString mOriginalUriString;
  nsTObserverArray<nsWeakPtr> mPrivacyObservers;
  nsTObserverArray<nsWeakPtr> mReflowObservers;
  nsTObserverArray<nsWeakPtr> mScrollObservers;
  mozilla::UniquePtr<mozilla::dom::ClientSource> mInitialClientSource;
  nsCOMPtr<nsINetworkInterceptController> mInterceptController;
  RefPtr<nsDOMNavigationTiming> mTiming;
  RefPtr<nsDSURIContentListener> mContentListener;
  RefPtr<nsGlobalWindowOuter> mScriptGlobal;
  nsCOMPtr<nsIPrincipal> mParentCharsetPrincipal;
  // The following 3 lists contain either nsITimer or nsRefreshTimer objects.
  // URIs to refresh are collected to mRefreshURIList.
  nsCOMPtr<nsIMutableArray> mRefreshURIList;
  // mSavedRefreshURIList is used to move the entries from mRefreshURIList to
  // mOSHE.
  nsCOMPtr<nsIMutableArray> mSavedRefreshURIList;
  // BFCache-in-parent implementation caches the entries in
  // mBFCachedRefreshURIList.
  nsCOMPtr<nsIMutableArray> mBFCachedRefreshURIList;
  uint64_t mContentWindowID;
  nsCOMPtr<nsIDocumentViewer> mDocumentViewer;
  nsCOMPtr<nsIWidget> mParentWidget;
  RefPtr<mozilla::dom::ChildSHistory> mSessionHistory;
  nsCOMPtr<nsIWebBrowserFind> mFind;
  RefPtr<nsCommandManager> mCommandManager;
  RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;

  // Weak reference to our BrowserChild actor.
  nsWeakPtr mBrowserChild;

  // Dimensions of the docshell
  mozilla::LayoutDeviceIntRect mBounds;

  /**
   * Content-Type Hint of the most-recently initiated load. Used for
   * session history entries.
   */
  nsCString mContentTypeHint;

  // mCurrentURI should be marked immutable on set if possible.
  // Change mCurrentURI only through SetCurrentURIInternal method.
  nsCOMPtr<nsIURI> mCurrentURI;
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;

#ifdef DEBUG
  // We're counting the number of |nsDocShells| to help find leaks
  static unsigned long gNumberOfDocShells;

  nsCOMPtr<nsIURI> mLastOpenedURI;
#endif

  // Reference to the SHEntry for this docshell until the page is destroyed.
  // Somebody give me better name
  // Only used when SHIP is disabled.
  nsCOMPtr<nsISHEntry> mOSHE;

  // Reference to the SHEntry for this docshell until the page is loaded
  // Somebody give me better name.
  // If mLSHE is non-null, non-pushState subframe loads don't create separate
  // root history entries. That is, frames loaded during the parent page
  // load don't generate history entries the way frame navigation after the
  // parent has loaded does. (This isn't the only purpose of mLSHE.)
  // Only used when SHIP is disabled.
  nsCOMPtr<nsISHEntry> mLSHE;

  // These are only set when fission.sessionHistoryInParent is set.
  mozilla::UniquePtr<mozilla::dom::SessionHistoryInfo> mActiveEntry;
  bool mActiveEntryIsLoadingFromSessionHistory = false;
  // mLoadingEntry is set when we're about to start loading. Whenever
  // setting mLoadingEntry, be sure to also set
  // mNeedToReportActiveAfterLoadingBecomesActive.
  mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo> mLoadingEntry;

  // Holds a weak pointer to a RestorePresentationEvent object if any that
  // holds a weak pointer back to us. We use this pointer to possibly revoke
  // the event whenever necessary.
  nsRevocableEventPtr<RestorePresentationEvent> mRestorePresentationEvent;

  // Editor data, if this document is designMode or contentEditable.
  mozilla::UniquePtr<nsDocShellEditorData> mEditorData;

  // The URI we're currently loading. This is only relevant during the
  // firing of a pagehide/unload. The caller of FirePageHideNotification()
  // is responsible for setting it and unsetting it. It may be null if the
  // pagehide/unload is happening for some reason other than just loading a
  // new URI.
  nsCOMPtr<nsIURI> mLoadingURI;

  // Set in LoadErrorPage from the method argument and used later
  // in CreateDocumentViewer. We have to delay an shistory entry creation
  // for which these objects are needed.
  nsCOMPtr<nsIURI> mFailedURI;
  nsCOMPtr<nsIChannel> mFailedChannel;

  mozilla::UniquePtr<mozilla::gfx::Matrix5x4> mColorMatrix;

  const mozilla::Encoding* mParentCharset;

  // WEAK REFERENCES BELOW HERE.
  // Note these are intentionally not addrefd. Doing so will create a cycle.
  // For that reasons don't use nsCOMPtr.

  nsIDocShellTreeOwner* mTreeOwner;  // Weak Reference

  RefPtr<mozilla::dom::EventTarget> mChromeEventHandler;

  mozilla::ScrollbarPreference mScrollbarPref;  // persistent across doc loads

  eCharsetReloadState mCharsetReloadState;

  int32_t mParentCharsetSource;
  mozilla::CSSIntSize mFrameMargins;

  // This can either be a content docshell or a chrome docshell.
  const int32_t mItemType;

  // Index into the nsISHEntry array, indicating the previous and current
  // entry at the time that this DocShell begins to load. Consequently
  // root docshell's indices can differ from child docshells'.
  int32_t mPreviousEntryIndex;
  int32_t mLoadedEntryIndex;

  BusyFlags mBusyFlags;
  AppType mAppType;
  uint32_t mLoadType;
  uint32_t mFailedLoadType;

  // See WindowGlobalParent::mSingleChannelId.
  mozilla::Maybe<uint64_t> mSingleChannelId;
  uint32_t mRequestForBlockingFromBFCacheCount = 0;

  uint64_t mChannelToDisconnectOnPageHide;

  uint32_t mPendingReloadCount = 0;

  // The following two fields cannot be declared as bit fields
  // because of uses with AutoRestore.
  bool mCreatingDocument;  // (should be) debugging only
#ifdef DEBUG
  bool mInEnsureScriptEnv;
  uint64_t mDocShellID = 0;
#endif

  bool mInitialized : 1;
  bool mAllowSubframes : 1;
  bool mAllowMetaRedirects : 1;
  bool mAllowImages : 1;
  bool mAllowMedia : 1;
  bool mAllowDNSPrefetch : 1;
  bool mAllowWindowControl : 1;
  bool mCSSErrorReportingEnabled : 1;
  bool mAllowAuth : 1;
  bool mAllowKeywordFixup : 1;
  bool mDisableMetaRefreshWhenInactive : 1;
  bool mIsAppTab : 1;
  bool mWindowDraggingAllowed : 1;
  bool mInFrameSwap : 1;

  // This boolean is set to true right before we fire pagehide and generally
  // unset when we embed a new content viewer. While it's true no navigation
  // is allowed in this docshell.
  bool mFiredUnloadEvent : 1;

  // this flag is for bug #21358. a docshell may load many urls
  // which don't result in new documents being created (i.e. a new
  // content viewer) we want to make sure we don't call a on load
  // event more than once for a given content viewer.
  bool mEODForCurrentDocument : 1;
  bool mURIResultedInDocument : 1;

  bool mIsBeingDestroyed : 1;

  bool mIsExecutingOnLoadHandler : 1;

  // Indicates to CreateDocumentViewer() that it is safe to cache the old
  // presentation of the page, and to SetupNewViewer() that the old viewer
  // should be passed a SHEntry to save itself into.
  // Only used with SHIP disabled.
  bool mSavingOldViewer : 1;

  bool mInvisible : 1;
  bool mHasLoadedNonBlankURI : 1;

  // This flag means that mTiming has been initialized but nulled out.
  // We will check the innerWin's timing before creating a new one
  // in MaybeInitTiming()
  bool mBlankTiming : 1;

  // This flag indicates when the title is valid for the current URI.
  bool mTitleValidForCurrentURI : 1;

  // If mWillChangeProcess is set to true, then when the docshell is destroyed,
  // we prepare the browsing context to change process.
  bool mWillChangeProcess : 1;

  // This flag indicates whether or not the DocShell is currently executing an
  // nsIWebNavigation navigation method.
  bool mIsNavigating : 1;

  // Whether we have a pending encoding autodetection request from the
  // menu for all encodings.
  bool mForcedAutodetection : 1;

  /*
   * Set to true if we're checking session history (in the parent process) for
   * a possible history load. Used only with iframes.
   */
  bool mCheckingSessionHistory : 1;

  // Whether mBrowsingContext->SetActiveSessionHistoryEntry() needs to be called
  // when the loading entry becomes the active entry. This is used for the
  // initial about:blank-replacing about:blank in order to make the history
  // length WPTs pass.
  bool mNeedToReportActiveAfterLoadingBecomesActive : 1;
};

inline nsISupports* ToSupports(nsDocShell* aDocShell) {
  return static_cast<nsIDocumentLoader*>(aDocShell);
}

#endif /* nsDocShell_h__ */
