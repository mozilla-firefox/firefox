/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpChannel_h_
#define nsHttpChannel_h_

#include "AlternateServices.h"
#include "AutoClose.h"
#include "HttpBaseChannel.h"
#include "HttpTransactionShell.h"
#include "TimingStruct.h"
#include "mozilla/AtomicBitfields.h"
#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/extensions/PStreamFilterParent.h"
#include "mozilla/net/DocumentLoadListener.h"
#include "nsHttpResponseHead.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICacheEntry.h"
#include "nsICacheEntryOpenCallback.h"
#include "nsICacheStorage.h"
#include "nsICachingChannel.h"
#include "nsICorsPreflightCallback.h"
#include "nsIDNSListener.h"
#include "nsIEarlyHintObserver.h"
#include "nsIHttpAuthenticableChannel.h"
#include "nsIProtocolProxyCallback.h"
#include "nsIReplacedHttpResponse.h"
#include "nsIRequestContext.h"
#include "nsIStreamListener.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsITransport.h"
#include "nsITransportSecurityInfo.h"
#include "nsTArray.h"
#include "nsWeakReference.h"

class nsDNSPrefetch;
class nsICancelable;
class nsIDNSRecord;
class nsIDNSHTTPSSVCRecord;
class nsIHttpChannelAuthProvider;
class nsInputStreamPump;
class nsITransportSecurityInfo;

namespace mozilla {
namespace net {

class nsChannelClassifier;
class HttpChannelSecurityWarningReporter;

using DNSPromise = MozPromise<nsCOMPtr<nsIDNSRecord>, nsresult, false>;

//-----------------------------------------------------------------------------
// nsHttpChannel
//-----------------------------------------------------------------------------

// Use to support QI nsIChannel to nsHttpChannel
#define NS_HTTPCHANNEL_IID \
  {0x301bf95b, 0x7bb3, 0x4ae1, {0xa9, 0x71, 0x40, 0xbc, 0xfa, 0x81, 0xde, 0x12}}

class nsHttpChannel final : public HttpBaseChannel,
                            public HttpAsyncAborter<nsHttpChannel>,
                            public nsICachingChannel,
                            public nsICacheEntryOpenCallback,
                            public nsITransportEventSink,
                            public nsIProtocolProxyCallback,
                            public nsIHttpAuthenticableChannel,
                            public nsIAsyncVerifyRedirectCallback,
                            public nsIThreadRetargetableRequest,
                            public nsIThreadRetargetableStreamListener,
                            public nsIDNSListener,
                            public nsSupportsWeakReference,
                            public nsICorsPreflightCallback,
                            public nsIRequestTailUnblockCallback,
                            public nsIEarlyHintObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER
  NS_DECL_NSICACHEINFOCHANNEL
  NS_DECL_NSICACHINGCHANNEL
  NS_DECL_NSICACHEENTRYOPENCALLBACK
  NS_DECL_NSITRANSPORTEVENTSINK
  NS_DECL_NSIPROTOCOLPROXYCALLBACK
  NS_DECL_NSIPROXIEDCHANNEL
  NS_DECL_NSIASYNCVERIFYREDIRECTCALLBACK
  NS_DECL_NSITHREADRETARGETABLEREQUEST
  NS_DECL_NSIDNSLISTENER
  NS_INLINE_DECL_STATIC_IID(NS_HTTPCHANNEL_IID)
  NS_DECL_NSIREQUESTTAILUNBLOCKCALLBACK
  NS_DECL_NSIEARLYHINTOBSERVER

  // nsIHttpAuthenticableChannel. We can't use
  // NS_DECL_NSIHTTPAUTHENTICABLECHANNEL because it duplicates cancel() and
  // others.
  NS_IMETHOD GetIsSSL(bool* aIsSSL) override;
  NS_IMETHOD GetProxyMethodIsConnect(bool* aProxyMethodIsConnect) override;
  NS_IMETHOD GetServerResponseHeader(
      nsACString& aServerResponseHeader) override;
  NS_IMETHOD GetProxyChallenges(nsACString& aChallenges) override;
  NS_IMETHOD GetWWWChallenges(nsACString& aChallenges) override;
  NS_IMETHOD SetProxyCredentials(const nsACString& aCredentials) override;
  NS_IMETHOD SetWWWCredentials(const nsACString& aCredentials) override;
  NS_IMETHOD OnAuthAvailable() override;
  NS_IMETHOD OnAuthCancelled(bool userCancel) override;
  NS_IMETHOD CloseStickyConnection() override;
  NS_IMETHOD ConnectionRestartable(bool) override;
  // Functions we implement from nsIHttpAuthenticableChannel but are
  // declared in HttpBaseChannel must be implemented in this class. We
  // just call the HttpBaseChannel:: impls.
  NS_IMETHOD GetLoadFlags(nsLoadFlags* aLoadFlags) override;
  NS_IMETHOD GetURI(nsIURI** aURI) override;
  NS_IMETHOD GetNotificationCallbacks(
      nsIInterfaceRequestor** aCallbacks) override;
  NS_IMETHOD GetLoadGroup(nsILoadGroup** aLoadGroup) override;
  NS_IMETHOD GetRequestMethod(nsACString& aMethod) override;

  nsHttpChannel();

  [[nodiscard]] virtual nsresult Init(nsIURI* aURI, uint32_t aCaps,
                                      nsProxyInfo* aProxyInfo,
                                      uint32_t aProxyResolveFlags,
                                      nsIURI* aProxyURI, uint64_t aChannelId,
                                      nsILoadInfo* aLoadInfo) override;

  static bool IsRedirectStatus(uint32_t status);
  static bool WillRedirect(const nsHttpResponseHead& response);

  // Methods HttpBaseChannel didn't implement for us or that we override.
  //
  // nsIRequest
  NS_IMETHOD SetCanceledReason(const nsACString& aReason) override;
  NS_IMETHOD GetCanceledReason(nsACString& aReason) override;
  NS_IMETHOD CancelWithReason(nsresult status,
                              const nsACString& reason) override;
  NS_IMETHOD Cancel(nsresult status) override;
  NS_IMETHOD Suspend() override;
  static void StaticSuspend(nsHttpChannel* aChan);
  NS_IMETHOD Resume() override;
  // nsIChannel
  NS_IMETHOD
  GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) override;
  NS_IMETHOD AsyncOpen(nsIStreamListener* aListener) override;
  // nsIHttpChannel
  NS_IMETHOD GetEncodedBodySize(uint64_t* aEncodedBodySize) override;
  // nsIHttpChannelInternal
  NS_IMETHOD GetIsAuthChannel(bool* aIsAuthChannel) override;
  NS_IMETHOD SetChannelIsForDownload(bool aChannelIsForDownload) override;
  NS_IMETHOD GetNavigationStartTimeStamp(TimeStamp* aTimeStamp) override;
  NS_IMETHOD SetNavigationStartTimeStamp(TimeStamp aTimeStamp) override;
  NS_IMETHOD CancelByURLClassifier(nsresult aErrorCode) override;
  NS_IMETHOD GetLastTransportStatus(nsresult* aLastTransportStatus) override;
  // nsISupportsPriority
  NS_IMETHOD SetPriority(int32_t value) override;
  // nsIClassOfService
  NS_IMETHOD SetClassFlags(uint32_t inFlags) override;
  NS_IMETHOD AddClassFlags(uint32_t inFlags) override;
  NS_IMETHOD ClearClassFlags(uint32_t inFlags) override;
  NS_IMETHOD SetClassOfService(ClassOfService cos) override;
  NS_IMETHOD SetIncremental(bool incremental) override;

  // nsIResumableChannel
  NS_IMETHOD ResumeAt(uint64_t startPos, const nsACString& entityID) override;

  NS_IMETHOD SetNotificationCallbacks(
      nsIInterfaceRequestor* aCallbacks) override;
  NS_IMETHOD SetLoadGroup(nsILoadGroup* aLoadGroup) override;
  // nsITimedChannel
  NS_IMETHOD GetDomainLookupStart(
      mozilla::TimeStamp* aDomainLookupStart) override;
  NS_IMETHOD GetDomainLookupEnd(mozilla::TimeStamp* aDomainLookupEnd) override;
  NS_IMETHOD GetConnectStart(mozilla::TimeStamp* aConnectStart) override;
  NS_IMETHOD GetTcpConnectEnd(mozilla::TimeStamp* aTcpConnectEnd) override;
  NS_IMETHOD GetSecureConnectionStart(
      mozilla::TimeStamp* aSecureConnectionStart) override;
  NS_IMETHOD GetConnectEnd(mozilla::TimeStamp* aConnectEnd) override;
  NS_IMETHOD GetRequestStart(mozilla::TimeStamp* aRequestStart) override;
  NS_IMETHOD GetResponseStart(mozilla::TimeStamp* aResponseStart) override;
  NS_IMETHOD GetFirstInterimResponseStart(
      mozilla::TimeStamp* aFirstInterimResponseStart) override;
  NS_IMETHOD GetFinalResponseHeadersStart(
      mozilla::TimeStamp* aFinalResponseHeadersStart) override;
  NS_IMETHOD GetResponseEnd(mozilla::TimeStamp* aResponseEnd) override;

  NS_IMETHOD GetTransactionPending(
      mozilla::TimeStamp* aTransactionPending) override;

  // nsICorsPreflightCallback
  NS_IMETHOD OnPreflightSucceeded() override;
  NS_IMETHOD OnPreflightFailed(nsresult aError) override;

  [[nodiscard]] nsresult AddSecurityMessage(
      const nsAString& aMessageTag, const nsAString& aMessageCategory) override;
  NS_IMETHOD LogBlockedCORSRequest(const nsAString& aMessage,
                                   const nsACString& aCategory,
                                   bool aIsWarning) override;
  NS_IMETHOD LogMimeTypeMismatch(const nsACString& aMessageName, bool aWarning,
                                 const nsAString& aURL,
                                 const nsAString& aContentType) override;

  NS_IMETHOD SetEarlyHintObserver(nsIEarlyHintObserver* aObserver) override;
  NS_IMETHOD SetWebTransportSessionEventListener(
      WebTransportSessionEventListener* aListener) override;
  NS_IMETHOD SetResponseOverride(
      nsIReplacedHttpResponse* aReplacedHttpResponse) override;
  NS_IMETHOD SetResponseStatus(uint32_t aStatus,
                               const nsACString& aStatusText) override;

  NS_IMETHOD GetDecompressDictionary(
      DictionaryCacheEntry** aDictionary) override;
  NS_IMETHOD SetDecompressDictionary(
      DictionaryCacheEntry* aDictionary) override;

  void SetWarningReporter(HttpChannelSecurityWarningReporter* aReporter);
  HttpChannelSecurityWarningReporter* GetWarningReporter();

  bool DataSentToChildProcess() { return LoadDataSentToChildProcess(); }

  enum class SnifferType { Media, Image };
  void DisableIsOpaqueResponseAllowedAfterSniffCheck(SnifferType aType);

  void OnOpaqueResponseAllowed() override;

 public: /* internal necko use only */
  uint32_t GetRequestTime() const { return mRequestTime; }
  const nsACString& GetLNAPromptAction() const { return mLNAPromptAction; }

  void AsyncOpenFinal(TimeStamp aTimeStamp);

  [[nodiscard]] nsresult OpenCacheEntry(bool isHttps);
  [[nodiscard]] nsresult OpenCacheEntryInternal(bool isHttps);
  // Records whether the entry the cache handed us is keyed on mCacheEntryURI or
  // was found through a No-Vary-Search secondary lookup.
  void NoteCacheEntryKeyMatch(nsICacheEntry* aEntry);
  // Picks the cache storage matching the channel's current persistence flags.
  [[nodiscard]] nsresult GetCacheStorage(nsICacheStorage** aStorage);
  // Before writing a response, swaps out a cache entry that belongs to another
  // URL (No-Vary-Search match) for a writable entry keyed on mCacheEntryURI.
  [[nodiscard]] nsresult MaybeReplaceNoVarySearchAliasEntry();
  [[nodiscard]] nsresult ContinueConnect();

  [[nodiscard]] nsresult StartRedirectChannelToURI(nsIURI*, uint32_t);
  [[nodiscard]] nsresult StartRedirectChannelToURI(
      nsIURI*, uint32_t, std::function<void(nsIChannel*)>&&);

  SnifferCategoryType GetSnifferCategoryType() const {
    return mSnifferCategoryType;
  }

  // Helper to keep cache callbacks wait flags consistent
  class AutoCacheWaitFlags {
   public:
    explicit AutoCacheWaitFlags(nsHttpChannel* channel)
        : mChannel(channel), mKeep(0) {
      // Flags must be set before entering any AsyncOpenCacheEntry call.
      mChannel->StoreWaitForCacheEntry(nsHttpChannel::WAIT_FOR_CACHE_ENTRY);
    }

    void Keep(uint32_t flags) {
      // Called after successful call to appropriate AsyncOpenCacheEntry call.
      mKeep |= flags;
    }

    ~AutoCacheWaitFlags() {
      // Keep only flags those are left to be wait for.
      mChannel->StoreWaitForCacheEntry(mChannel->LoadWaitForCacheEntry() &
                                       mKeep);
    }

   private:
    nsHttpChannel* mChannel;
    uint32_t mKeep : 1;
  };

  bool AwaitingCacheCallbacks();
  void SetCouldBeSynthesized();

  // Return true if the latest ODA is invoked by mCachePump.
  // Should only be called on the same thread as ODA.
  bool IsReadingFromCache() const { return mIsReadingFromCache; }

  base::ProcessId ProcessId();

  using ChildEndpointPromise =
      MozPromise<mozilla::ipc::Endpoint<extensions::PStreamFilterChild>, bool,
                 true>;
  [[nodiscard]] RefPtr<ChildEndpointPromise> AttachStreamFilter();

  already_AddRefed<WebTransportSessionEventListener>
  GetWebTransportSessionEventListener();

 public:
  CacheDisposition mCacheDisposition{kCacheUnresolved};

 protected:
  virtual ~nsHttpChannel();

 private:
  using nsContinueRedirectionFunc = nsresult (nsHttpChannel::*)(nsresult);

  // Directly call |aFunc| if the channel is not canceled and not suspended.
  // Otherwise, set |aFunc| to |mCallOnResume| and wait until the channel
  // resumes.
  nsresult CallOrWaitForResume(
      const std::function<nsresult(nsHttpChannel*)>& aFunc);

  bool RequestIsConditional();
  void HandleContinueCancellingByURLClassifier(nsresult aErrorCode);
  nsresult CancelInternal(nsresult status);
  void ContinueCancellingByURLClassifier(nsresult aErrorCode);

  // Connections will only be established in this function.
  // (including DNS prefetch and speculative connection.)
  void MaybeResolveProxyAndBeginConnect();
  void MaybeStartDNSPrefetch();

  // Based on the proxy configuration determine the strategy for resolving the
  // end server host name.
  nsIHttpChannelInternal::ProxyDNSStrategy ComputeProxyDNSStrategy();

 public:
  NS_IMETHOD GetProxyDNSStrategy(
      nsIHttpChannelInternal::ProxyDNSStrategy* aStrategy) override;

 private:
  // Add Sec-Fetch-Storage-Access headers based on cookie partitioning
  void AddStorageAccessHeadersToRequest();
  bool DispatchRelease();

 public:
  // returns whether this channel is a retry after receiving the
  // "Activate-Storage-Access"-header for a request that is eligable for
  // unpartitioned cookies. Therefore needs to have still valid
  // storage-permission granted. Public to be accible from AntiTrackingUtils.
  bool StorageAccessReloadedChannel();

  // Tells the channel to suspend after examining the response
  void PrimeSuspendAfterExamineResponse();
  // Cancel the suspension request, or resume if the suspension started
  void CancelSuspendOrResumeAfterExamineResponse();
  // Suspend if we called PrimeSuspendAfterExamineResponse
  // and not CancelSuspendOrResumeAfterExamineResponse
  void MaybeSuspendAfterExamineResponse();

 private:
  // We might synchronously or asynchronously call BeginConnect,
  // which includes DNS prefetch and speculative connection, according to
  // whether an async tracker lookup is required. If the tracker lookup
  // is required, this funciton will just return NS_OK and BeginConnect()
  // will be called when callback. See Bug 1325054 for more information.
  nsresult BeginConnect();
  [[nodiscard]] nsresult PrepareToConnect();
  [[nodiscard]] nsresult ContinuePrepareToConnect();
  [[nodiscard]] nsresult OnBeforeConnect();
  [[nodiscard]] nsresult ContinueOnBeforeConnect(
      bool aShouldUpgrade, nsresult aStatus, bool aUpgradeWithHTTPSRR = false);
  nsresult MaybeUseHTTPSRRForUpgrade(bool aShouldUpgrade, nsresult aStatus);
  void OnHTTPSRRAvailable(nsIDNSHTTPSSVCRecord* aRecord);
  [[nodiscard]] nsresult Connect();
  void SpeculativeConnect();
  [[nodiscard]] nsresult SetupChannelForTransaction();
  [[nodiscard]] nsresult InitTransaction();
  [[nodiscard]] nsresult DispatchTransaction(
      HttpTransactionShell* aTransWithStickyConn);
  [[nodiscard]] nsresult CallOnStartRequest();
  [[nodiscard]] nsresult ProcessResponse(nsHttpConnectionInfo* aConnInfo);
  void AsyncContinueProcessResponse(nsHttpConnectionInfo* aConnInfo);
  [[nodiscard]] nsresult ContinueProcessResponse1(
      nsHttpConnectionInfo* aConnInfo);
  [[nodiscard]] nsresult ContinueProcessResponse2(nsresult);
  nsresult HandleOverrideResponse();
  nsresult OnPermissionPromptResult(bool aGranted, const nsACString& aType);
  LNAPermission UpdateLocalNetworkAccessPermissions(
      const nsACString& aPermissionType);
  void MaybeUpdateDocumentIPAddressSpaceFromCache();
  nsresult ProcessLNAActions();
  void UpdateCurrentIpAddressSpace();

 public:
  void UpdateCacheDisposition(bool aSuccessfulReval, bool aPartialContentUsed);
  [[nodiscard]] nsresult ContinueProcessResponse3(nsresult);
  [[nodiscard]] nsresult ContinueProcessResponse4(nsresult);
  [[nodiscard]] nsresult ProcessNormal();
  [[nodiscard]] nsresult ContinueProcessNormal(nsresult);
  [[nodiscard]] nsresult ContinueProcessNormal2(nsresult);
  [[nodiscard]] nsresult ContinueProcessNormal3();
  void ProcessAltService(nsHttpConnectionInfo* aTransConnInfo = nullptr);
  bool ShouldBypassProcessNotModified();
  [[nodiscard]] nsresult ProcessNotModified(
      const std::function<nsresult(nsHttpChannel*, nsresult)>&
          aContinueProcessResponseFunc);
  [[nodiscard]] nsresult ContinueProcessResponseAfterNotModified(nsresult aRv);

  [[nodiscard]] nsresult AsyncProcessRedirection(uint32_t redirectType);
  [[nodiscard]] nsresult ContinueProcessRedirection(nsresult);
  [[nodiscard]] nsresult ContinueProcessRedirectionAfterFallback(nsresult);
  [[nodiscard]] nsresult ProcessFailedProxyConnect(uint32_t httpStatus);
  void HandleAsyncAbort();
  [[nodiscard]] nsresult EnsureAssocReq();
  void ProcessSSLInformation();
  bool IsHTTPS();

  [[nodiscard]] nsresult ContinueOnStartRequest1(nsresult);
  [[nodiscard]] nsresult ContinueOnStartRequest2(nsresult);
  [[nodiscard]] nsresult ContinueOnStartRequest3(nsresult);

  void OnClassOfServiceUpdated();

  // redirection specific methods
  void HandleAsyncRedirect();
  void HandleAsyncAPIRedirect();
  [[nodiscard]] nsresult ContinueHandleAsyncRedirect(nsresult);
  void HandleAsyncNotModified();
  [[nodiscard]] nsresult PromptTempRedirect();
  [[nodiscard]] virtual nsresult SetupReplacementChannel(
      nsIURI*, nsIChannel*, bool preserveMethod,
      uint32_t redirectFlags) override;
  void HandleAsyncRedirectToUnstrippedURI();

  // proxy specific methods
  [[nodiscard]] nsresult ProxyFailover();
  [[nodiscard]] nsresult AsyncDoReplaceWithProxy(nsIProxyInfo*);
  [[nodiscard]] nsresult ResolveProxy();

  // cache specific methods
  [[nodiscard]] nsresult OnNormalCacheEntryAvailable(nsICacheEntry* aEntry,
                                                     bool aNew,
                                                     nsresult aEntryStatus);
  [[nodiscard]] nsresult OnCacheEntryAvailableInternal(nsICacheEntry* entry,
                                                       bool aNew,
                                                       nsresult status);
  [[nodiscard]] nsresult GenerateCacheKey(uint32_t postID, nsACString& key);
  [[nodiscard]] nsresult UpdateExpirationTime();
  [[nodiscard]] nsresult CheckPartial(nsICacheEntry* aEntry, int64_t* aSize,
                                      int64_t* aContentLength);
  [[nodiscard]] nsresult ReadFromCache(void);
  void CloseCacheEntry(bool doomOnFailure);
  [[nodiscard]] nsresult InitCacheEntry();
  void UpdateInhibitPersistentCachingFlag();
  bool ParseDictionary(nsICacheEntry* aEntry, nsHttpResponseHead* aResponseHead,
                       bool aModified);
  [[nodiscard]] nsresult AddCacheEntryHeaders(nsICacheEntry* entry,
                                              bool aModified);
  [[nodiscard]] nsresult UpdateCacheEntryHeaders(nsICacheEntry* entry,
                                                 const nsHttpAtom* aAtom);
  [[nodiscard]] nsresult FinalizeCacheEntry();
  [[nodiscard]] nsresult InstallCacheListener(int64_t offset = 0);
  [[nodiscard]] nsresult DoInstallCacheListener(bool aSaveDecompressed,
                                                int64_t offset = 0);
  void MaybeInvalidateCacheEntryForSubsequentGet();
  void AsyncOnExamineCachedResponse();

  // byte range request specific methods
  [[nodiscard]] nsresult ProcessPartialContent(
      const std::function<nsresult(nsHttpChannel*, nsresult)>&
          aContinueProcessResponseFunc);
  [[nodiscard]] nsresult ContinueProcessResponseAfterPartialContent(
      nsresult aRv);
  [[nodiscard]] nsresult OnDoneReadingPartialCacheEntry(bool* streamDone);

  [[nodiscard]] nsresult DoAuthRetry(
      HttpTransactionShell* aTransWithStickyConn,
      const std::function<nsresult(nsHttpChannel*, nsresult)>&
          aContinueOnStopRequestFunc);
  [[nodiscard]] nsresult ContinueDoAuthRetry(
      HttpTransactionShell* aTransWithStickyConn,
      const std::function<nsresult(nsHttpChannel*, nsresult)>&
          aContinueOnStopRequestFunc);
  [[nodiscard]] MOZ_NEVER_INLINE nsresult
  DoConnect(HttpTransactionShell* aTransWithStickyConn = nullptr);
  [[nodiscard]] nsresult DoConnectActual(
      HttpTransactionShell* aTransWithStickyConn);
  [[nodiscard]] nsresult ContinueOnStopRequestAfterAuthRetry(
      nsresult aStatus, bool aAuthRetry, bool aIsFromNet, bool aContentComplete,
      HttpTransactionShell* aTransWithStickyConn);
  [[nodiscard]] nsresult ContinueOnStopRequest(nsresult status, bool aIsFromNet,
                                               bool aContentComplete);

  void HandleAsyncRedirectChannelToHttps();
  [[nodiscard]] nsresult StartRedirectChannelToHttps();
  [[nodiscard]] nsresult ContinueAsyncRedirectChannelToURI(nsresult rv);
  [[nodiscard]] nsresult OpenRedirectChannel(nsresult rv);

  HttpTrafficCategory CreateTrafficCategory();

  /**
   * A function that takes care of reading STS and PKP headers and enforcing
   * STS and PKP load rules. After a secure channel is erected, STS and PKP
   * requires the channel to be trusted or any STS or PKP header data on
   * the channel is ignored. This is called from ProcessResponse.
   */
  [[nodiscard]] nsresult ProcessSecurityHeaders();

  /**
   * Taking care of the Content-Signature header and fail the channel if
   * the signature verification fails or is required but the header is not
   * present.
   * This sets mListener to ContentVerifier, which buffers the entire response
   * before verifying the Content-Signature header. If the verification is
   * successful, the load proceeds as usual. If the verification fails, a
   * NS_ERROR_INVALID_SIGNATURE is thrown and a fallback loaded in nsDocShell
   */
  [[nodiscard]] nsresult ProcessContentSignatureHeader(
      nsHttpResponseHead* aResponseHead);

  /**
   * A function to process HTTP Strict Transport Security (HSTS) headers.
   * Some basic consistency checks have been applied to the channel. Called
   * from ProcessSecurityHeaders.
   */
  [[nodiscard]] nsresult ProcessHSTSHeader(nsITransportSecurityInfo* aSecInfo);

  [[nodiscard]] nsresult ProcessWAICTHeader();

  void InvalidateCacheEntryForLocation(const char* location);
  void AssembleCacheKey(const char* spec, uint32_t postID, nsACString& key);
  [[nodiscard]] nsresult CreateNewURI(const char* loc, nsIURI** newURI);
  void DoInvalidateCacheEntry(nsIURI* aURI);

  // Ref RFC2616 13.10: "invalidation... MUST only be performed if
  // the host part is the same as in the Request-URI"
  inline bool HostPartIsTheSame(nsIURI* uri) {
    nsAutoCString tmpHost1, tmpHost2;
    return (NS_SUCCEEDED(mURI->GetAsciiHost(tmpHost1)) &&
            NS_SUCCEEDED(uri->GetAsciiHost(tmpHost2)) &&
            (tmpHost1 == tmpHost2));
  }

  inline static bool DoNotRender3xxBody(nsresult rv) {
    return rv == NS_ERROR_REDIRECT_LOOP || rv == NS_ERROR_CORRUPTED_CONTENT ||
           rv == NS_ERROR_UNKNOWN_PROTOCOL || rv == NS_ERROR_MALFORMED_URI ||
           rv == NS_ERROR_PORT_ACCESS_NOT_ALLOWED;
  }

  // Report telemetry for system principal request success rate
  void ReportSystemChannelTelemetry(nsresult status);

  // Create a aggregate set of the current notification callbacks
  // and ensure the transaction is updated to use it.
  void UpdateAggregateCallbacks();

  static bool HasQueryString(nsHttpRequestHead::ParsedMethodType method,
                             nsIURI* uri);
  bool ResponseWouldVary(nsICacheEntry* entry);
  bool IsResumable(int64_t partialLen, int64_t contentLength,
                   bool ignoreMissingPartialLen = false) const;
  [[nodiscard]] nsresult MaybeSetupByteRangeRequest(
      int64_t partialLen, int64_t contentLength,
      bool ignoreMissingPartialLen = false);
  [[nodiscard]] nsresult SetupByteRangeRequest(int64_t partialLen);
  void UntieByteRangeRequest();
  void UntieValidationRequest();
  [[nodiscard]] nsresult OpenCacheInputStream(nsICacheEntry* cacheEntry,
                                              bool startBuffering);

  void SetOriginHeader();
  void SetDoNotTrack();
  void SetGlobalPrivacyControl();

  already_AddRefed<nsChannelClassifier> GetOrCreateChannelClassifier();

  // Start an internal redirect to a new InterceptedHttpChannel which will
  // resolve in firing a ServiceWorker FetchEvent.
  [[nodiscard]] nsresult RedirectToInterceptedChannel();

  // Start an internal redirect to a new channel for auth retry
  [[nodiscard]] nsresult RedirectToNewChannelForAuthRetry();

  // Determines and sets content type in the cache entry. It's called when
  // writing a new entry. The content type is used in cache internally only.
  void SetCachedContentType();

  bool IsAuthRedirectedChannel() { return !!LoadAuthRedirectedChannel(); }

 private:
  // --- MAIN THREAD ONLY OBJECTS ---
  // this section is for main-thread-only objects
  // all the references need to be released on main thread.
  // auth specific data
  nsCOMPtr<nsIHttpChannelAuthProvider> mAuthProvider;
  nsCOMPtr<nsIURI> mRedirectURI;
  nsCOMPtr<nsIURI> mUnstrippedRedirectURI;
  nsCOMPtr<nsIChannel> mRedirectChannel;
  nsCOMPtr<nsIChannel> mPreflightChannel;

  // nsChannelClassifier checks this channel's URI against
  // the URI classifier service.
  // nsChannelClassifier will be invoked twice in InitLocalBlockList() and
  // BeginConnect(), so save the nsChannelClassifier here to keep the
  // state of whether tracking protection is enabled or not.
  RefPtr<nsChannelClassifier> mChannelClassifier;

  // Dictionary entry for the entry being used to decompress this stream
  // (i.e. we added Dictionary-Available to the request).
  RefPtr<DictionaryCacheEntry> mDictDecompress;
  // This is for channels we're going to use a dictionaries in the future
  // (i.e. ResponseHeaders has Use-As-Dictionary)
  RefPtr<DictionaryCacheEntry> mDictSaving;
  // Note that in the case of using a file to be a dictionary for future
  // versions of itself, these may have the same URI (but likely different
  // hashes).

  // --- END OF MAIN THREAD ONLY OBJECTS SECTION ---

  // Called after the channel is made aware of its tracking status in order
  // to readjust the referrer if needed according to the referrer default
  // policy preferences.
  void ReEvaluateReferrerAfterTrackingStatusIsKnown();

  // Create a dummy channel for the same principal, out of the load group
  // just to revalidate the cache entry.  We don't care if this fails.
  // This method can be called on any thread, and creates an idle task
  // to perform the revalidation with delay.
  void PerformBackgroundCacheRevalidation();
  // This method can only be called on the main thread.
  void PerformBackgroundCacheRevalidationNow();

  void SetPriorityHeader();

 private:
  nsCOMPtr<nsICancelable> mProxyRequest;

  nsCOMPtr<nsIRequest> mTransactionPump;
  RefPtr<HttpTransactionShell> mTransaction;
  RefPtr<HttpTransactionShell> mTransactionSticky;

  uint64_t mLogicalOffset{0};

  // cache specific data
  nsCOMPtr<nsICacheEntry> mCacheEntry;
  // This will be set during OnStopRequest() before calling CloseCacheEntry(),
  // but only if the listener wants to use alt-data (signaled by
  // HttpBaseChannel::mPreferredCachedAltDataType being not empty)
  // Needed because calling openAlternativeOutputStream needs a reference
  // to the cache entry.
  nsCOMPtr<nsICacheEntry> mAltDataCacheEntry;
  // Holds the cache entry whose ORB validation is still pending when
  // CloseCacheEntry() runs. ORB's JavaScript validation completes
  // asynchronously, after the entry has already been committed, so we keep a
  // reference here in order to doom the entry if the response ends up blocked.
  nsCOMPtr<nsICacheEntry> mORBValidationCacheEntry;

  nsCOMPtr<nsIURI> mCacheEntryURI;
  nsCString mCacheIdExtension;

  // We must close mCacheInputStream explicitly to avoid leaks.
  AutoClose<nsIInputStream> mCacheInputStream;
  RefPtr<nsInputStreamPump> mCachePump;
  UniquePtr<nsHttpResponseHead> mCachedResponseHead;
  nsCOMPtr<nsITransportSecurityInfo> mCachedSecurityInfo;
  uint32_t mPostID{0};
  uint32_t mRequestTime{0};
  nsresult mLastTransportStatus{NS_OK};

  nsTArray<StreamFilterRequest> mStreamFilterRequests;

  mozilla::TimeStamp mOnStartRequestTimestamp;
  // Timestamp of the time the channel was suspended.
  mozilla::TimeStamp mSuspendTimestamp;

  // Properties used for the profiler markers
  // This keeps the timestamp for the start marker, to be reused for the end
  // marker.
  mozilla::TimeStamp mLastStatusReported;
  // This is true when one end marker is output, so that we never output more
  // than one.
  bool mEndMarkerAdded = false;
  // Is set to true when the NEL report is queued.
  bool mReportedNEL = false;

  // Total time the channel spent suspended. This value is reported to
  // telemetry in nsHttpChannel::OnStartRequest().
  TimeDuration mSuspendTotalTime{nullptr};

  friend class AutoRedirectVetoNotifier;
  friend class HttpAsyncAborter<nsHttpChannel>;

  uint32_t mRedirectType{0};

  static const uint32_t WAIT_FOR_CACHE_ENTRY = 1;

  bool mCacheOpenWithPriority{false};
  uint32_t mCacheQueueSizeWhenOpen{0};

  Atomic<bool> mIsAuthChannel{false};
  Atomic<bool> mAuthRetryPending{false};

  // clang-format off
  // state flags
  MOZ_ATOMIC_BITFIELDS(mAtomicBitfields5, 32, (
    (uint32_t, CachedContentIsPartial, 1),
    (uint32_t, CacheOnlyMetadata, 1),
    (uint32_t, TransactionReplaced, 1),
    (uint32_t, ProxyAuthPending, 1),
    // Set if before the first authentication attempt a custom authorization
    // header has been set on the channel.  This will make that custom header
    // go to the server instead of any cached credentials.
    (uint32_t, CustomAuthHeader, 1),
    (uint32_t, Resuming, 1),
    (uint32_t, InitedCacheEntry, 1),
    // True if consumer added its own If-None-Match or If-Modified-Since
    // headers. In such a case we must not override them in the cache code
    // and also we want to pass possible 304 code response through.
    (uint32_t, CustomConditionalRequest, 1),
    (uint32_t, WaitingForRedirectCallback, 1),
    // True if mRequestTime has been set. In such a case it is safe to update
    // the cache entry's expiration time. Otherwise, it is not(see bug 567360).
    (uint32_t, RequestTimeInitialized, 1),
    (uint32_t, CacheEntryIsReadOnly, 1),
    (uint32_t, CacheEntryIsWriteOnly, 1),
    // see WAIT_FOR_* constants above
    (uint32_t, WaitForCacheEntry, 1),
    // whether cache entry data write was in progress during cache entry check
    // when true, after we finish read from cache we must check all data
    // had been loaded from cache. If not, then an error has to be propagated
    // to the consumer.
    (uint32_t, ConcurrentCacheAccess, 1),
    // whether the request is setup be byte-range
    (uint32_t, IsPartialRequest, 1),
    // true iff there is AutoRedirectVetoNotifier on the stack
    (uint32_t, HasAutoRedirectVetoNotifier, 1),
    // consumers set this to true to use cache pinning, this has effect
    // only when the channel is in an app context
    (uint32_t, PinCacheContent, 1),
    // True if CORS preflight has been performed
    (uint32_t, IsCorsPreflightDone, 1),

    // if the http transaction was performed (i.e. not cached) and
    // the result in OnStopRequest was known to be correctly delimited
    // by chunking, content-length, or h2 end-stream framing
    (uint32_t, StronglyFramed, 1),

    // true if an HTTP transaction is created for the socket thread
    (uint32_t, UsedNetwork, 1),

    // the next authentication request can be sent on a whole new connection
    (uint32_t, AuthConnectionRestartable, 1),

    // True if the channel classifier has marked the channel to be cancelled due
    // to the safe-browsing classifier rules, but the asynchronous cancellation
    // process hasn't finished yet.
    (uint32_t, ChannelClassifierCancellationPending, 1),

    // True only when we are between Resume and async fire of mCallOnResume.
    // Used to suspend any newly created pumps in mCallOnResume handler.
    (uint32_t, AsyncResumePending, 1),

    // True if the data will be sent from the socket process to the
    // content process directly.
    (uint32_t, DataSentToChildProcess, 1),

    (uint32_t, UseHTTPSSVC, 1),
    (uint32_t, WaitHTTPSSVCRecord, 1)
  ))

  // Broken up into two bitfields to avoid alignment requirements of uint64_t.
  // (Too many bits used for one uint32_t.)
  MOZ_ATOMIC_BITFIELDS(mAtomicBitfields6, 32, (
    // True if network request gets to OnStart before we get a response from the cache
    (uint32_t, NetworkWonRace, 1),
    // Valid values are CachedContentValidity::Unset/Invalid/Valid
    (uint32_t, CachedContentIsValid, 2),
    // Only set to true when we receive an HTTPSSVC record before the
    // transaction is created.
    (uint32_t, HTTPSSVCTelemetryReported, 1),
    (uint32_t, EchConfigUsed, 1),
    (uint32_t, AuthRedirectedChannel, 1),
    (uint32_t, StorageAccessReloadChannel, 1),
    // True when the cache handed us an entry that is stored under a URL other
    // than mCacheEntryURI, which only happens when the No-Vary-Search secondary
    // lookup matched. Such an entry may be revalidated against, but a full
    // response must not be written back to it.
    (uint32_t, CacheEntryIsNoVarySearchMatch, 1)
  ))
  // clang-format on
  enum CachedContentValidity : uint8_t { Unset = 0, Invalid = 1, Valid = 2 };

  bool CachedContentIsValid() {
    return LoadCachedContentIsValid() == CachedContentValidity::Valid;
  }

  nsTArray<nsContinueRedirectionFunc> mRedirectFuncStack;

  // Needed for accurate DNS timing
  RefPtr<nsDNSPrefetch> mDNSPrefetch;

  // True if the channel's principal was found on a phishing, malware, or
  // tracking (if tracking protection is enabled) blocklist
  bool mLocalBlocklist{false};

  [[nodiscard]] nsresult WaitForRedirectCallback();
  void PushRedirectAsyncFunc(nsContinueRedirectionFunc func);
  void PopRedirectAsyncFunc(nsContinueRedirectionFunc func);

  // If this resource is eligible for tailing based on class-of-service flags
  // and load flags.  We don't tail Leaders/Unblocked/UrgentStart and
  // top-level loads.
  bool EligibleForTailing();

  // Called exclusively only from AsyncOpen or after all classification
  // callbacks. If this channel is 1) Tail, 2) assigned a request context, 3)
  // the context is still in the tail-blocked phase, then the method will
  // queue this channel. OnTailUnblock will be called after the context is
  // tail-unblocked or canceled.
  bool WaitingForTailUnblock();

  // A function we trigger when untail callback is triggered by our request
  // context in case this channel was tail-blocked.
  using TailUnblockCallback = nsresult (nsHttpChannel::*)();
  TailUnblockCallback mOnTailUnblock{nullptr};
  // Called on untail when tailed during AsyncOpen execution.
  nsresult AsyncOpenOnTailUnblock();
  // Called on untail when tailed because of being a tracking resource.
  nsresult ConnectOnTailUnblock();

  nsCString mUsername;

  // If non-null, warnings should be reported to this object.
  RefPtr<HttpChannelSecurityWarningReporter> mWarningReporter;

  // True if the channel is reading from cache.
  Atomic<bool> mIsReadingFromCache{false};

  // nsITimerCallback is implemented on a subclass so that the name attribute
  // doesn't conflict with the name attribute of the nsIRequest interface that
  // might be present on the same object (as seen from JavaScript code).
  class TimerCallback final : public nsITimerCallback, public nsINamed {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSITIMERCALLBACK
    NS_DECL_NSINAMED

    explicit TimerCallback(nsHttpChannel* aChannel);

   private:
    ~TimerCallback() = default;

    RefPtr<nsHttpChannel> mChannel;
  };

  // We need to remember which is the source of the response we are using.
  enum ResponseSource {
    RESPONSE_PENDING = 0,      // response is pending
    RESPONSE_FROM_CACHE = 1,   // response coming from cache. no network.
    RESPONSE_FROM_NETWORK = 2  // response coming from the network
  };
  Atomic<ResponseSource, Relaxed> mFirstResponseSource{RESPONSE_PENDING};

  nsresult TriggerNetwork();
  nsresult OnSuspendTimeout();
  void CancelNetworkRequest(nsresult aStatus);

  // Backstop for a wedged cache entry: arm/cancel a one-shot timer while the
  // channel is parked in AwaitingCacheCallbacks(), and force the load to the
  // network if the cache callback never arrives.  See
  // network.cache.entry_wait_timeout_ms.
  void MaybeStartCacheWaitTimer();
  void CancelCacheWaitTimer();
  nsresult OnCacheWaitTimeout();

  nsresult LogConsoleError(const char* aTag);

  void SetHTTPSSVCRecord(already_AddRefed<nsIDNSHTTPSSVCRecord> aRecord);

  void RecordOnStartTelemetry(nsresult aStatus, bool aIsNavigation);

  void MaybeGenerateNELReport();

  // Is true if the network request has been triggered.
  bool mNetworkTriggered = false;

  // Timer to detect if channel has been suspended too long while writing to
  // cache. When the timer fires we'll notify the cache entry to make
  // all other listeners continue.
  nsCOMPtr<nsITimer> mSuspendTimer;
  // Backstop timer armed while parked in AwaitingCacheCallbacks(); if the
  // cache entry callback never arrives we give up and race to the network.
  nsCOMPtr<nsITimer> mCacheWaitTimer;
  // Set once the cache-wait backstop has fired and we've forced the network,
  // so a late OnCacheEntryAvailable callback is ignored.
  bool mCacheWaitTimedOut{false};
  // Tri-state to track whether anti-tracking classification happened
  // and has completed or not.
  // Nothing: No anti-tracking classification
  // Some(true): classification ongoing
  // Some(false): classification done
  Maybe<Atomic<bool>> mSuspendAfterExamineResponse;
  // Set while a Suspend() taken by MaybeSuspendAfterExamineResponse() is
  // outstanding, i.e. not yet undone by
  // CancelSuspendOrResumeAfterExamineResponse(). Guards the compensating
  // Resume() so it fires at most once per real Suspend(), even though
  // CancelSuspendOrResumeAfterExamineResponse() can now be reached both from
  // the classification callback and from CancelInternal.
  Atomic<bool> mSuspendedForExamineResponse{false};
  bool mWritingToCache = false;
  bool mWaitingForProxy = false;
  bool mStaleRevalidation = false;
  // Set if this is dictionary-compressed
  bool mIsDictionaryCompressed = false;

  // Set to true when OnSuspendTimeout calls SetBypassWriterLock(true)
  // for the cache entry. Gets reset back to false when Resume calls
  // SetBypassWriterLock(false)
  bool mBypassCacheWriterSet{false};

  TimeStamp mNavigationStartTimeStamp;

  // Promise that blocks connection creation when we want to resolve the
  // origin host name to be able to give the configured proxy only the
  // resolved IP to not leak names.
  MozPromiseHolder<DNSPromise> mDNSBlockingPromise;
  // When we hit DoConnect before the resolution is done, Then() will be set
  // here to resume DoConnect.
  RefPtr<DNSPromise> mDNSBlockingThenable;

  // We update the value of mProxyConnectResponseHead when OnStartRequest is
  // called and reset the value when we switch to another failover proxy. It is
  // a shared pointer to the head owned by the connection/transaction, so this
  // is an addref rather than a deep copy. See bug 2045419.
  RefPtr<ProxyConnectResponseHead> mProxyConnectResponseHead;

  // If mHTTPSSVCRecord has value, it means OnHTTPSRRAvailable() is called and
  // we got the result of HTTPS RR query. Otherwise, it means we are still
  // waiting for the result or the query is not performed.
  Maybe<nsCOMPtr<nsIDNSHTTPSSVCRecord>> mHTTPSSVCRecord;

  enum class EssentialDomainCategory {
    SubAddonsMozillaOrg,
    AddonsMozillaOrg,
    Aus5MozillaOrg,
    RemoteSettings,
    Telemetry,
    Other,
  };

  // When an essential domain request gets retried this variable is set on the
  // redirected channel. This is important so we can track the success
  // rates of retried channels to the fallback domain.
  Maybe<EssentialDomainCategory> mEssentialDomainCategory;
  static EssentialDomainCategory GetEssentialDomainCategory(nsCString& domain);

  // Permissions for the request to make local network access
  LNAPerms mLNAPermission{};

  // Track if we are waiting for OnPermissionPromptResult callback
  // Used to handle cancellation while suspended waiting for LNA permission
  bool mWaitingForLNAPermission{false};

  bool mUsingDictionary{false};  // we added Available-Dictionary
  bool mShouldSuspendForDictionary{false};
  bool mSuspendedForDictionary{false};

 protected:
  virtual void DoNotifyListenerCleanup() override;

  // Override ReleaseListeners() because mChannelClassifier only exists
  // in nsHttpChannel and it will be released in ReleaseListeners().
  virtual void ReleaseListeners() override;

  virtual void DoAsyncAbort(nsresult aStatus) override;

 private:  // cache telemetry
  bool mDidReval{false};

  nsCOMPtr<nsIEarlyHintObserver> mEarlyHintObserver;
  Maybe<nsCString> mOpenerCallingScriptLocation;
  RefPtr<WebTransportSessionEventListener> mWebTransportSessionEventListener;
  nsMainThreadPtrHandle<nsIReplacedHttpResponse> mOverrideResponse;
  // LNA telemetry: stores the user's action on the permission prompt
  // Values: "allow", "deny", or empty string (no prompt shown)
  nsCString mLNAPromptAction;
};

}  // namespace net
}  // namespace mozilla

inline nsISupports* ToSupports(mozilla::net::nsHttpChannel* aChannel) {
  return static_cast<nsIHttpChannel*>(aChannel);
}

#endif  // nsHttpChannel_h_
