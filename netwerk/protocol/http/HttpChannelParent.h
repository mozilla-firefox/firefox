/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_HttpChannelParent_h
#define mozilla_net_HttpChannelParent_h

#include "HttpBaseChannel.h"
#include "nsHttp.h"
#include "mozilla/net/PHttpChannelParent.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/MozPromise.h"
#include "nsIParentRedirectingChannel.h"
#include "nsIProgressEventSink.h"
#include "nsIChannelEventSink.h"
#include "nsIRedirectResultListener.h"
#include "nsHttpChannel.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsIMultiPartChannel.h"
#include "nsIURI.h"

class nsICacheEntry;

#define HTTP_CHANNEL_PARENT_IID \
  {0x982b2372, 0x7aa5, 0x4e8a, {0xbd, 0x9f, 0x89, 0x74, 0xd7, 0xf0, 0x58, 0xeb}}

namespace mozilla {

namespace dom {
class BrowserParent;
}  // namespace dom

namespace net {

class HttpBackgroundChannelParent;
class ParentChannelListener;
class ChannelEventQueue;
class CacheEntryWriteHandleParent;

class HttpChannelParent final : public nsIInterfaceRequestor,
                                public PHttpChannelParent,
                                public nsIParentRedirectingChannel,
                                public nsIProgressEventSink,
                                public HttpChannelSecurityWarningReporter,
                                public nsIAsyncVerifyRedirectReadyCallback,
                                public nsIChannelEventSink,
                                public nsIRedirectResultListener,
                                public nsIMultiPartChannelListener {
  virtual ~HttpChannelParent();

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIPARENTCHANNEL
  NS_DECL_NSIPARENTREDIRECTINGCHANNEL
  NS_DECL_NSIPROGRESSEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIASYNCVERIFYREDIRECTREADYCALLBACK
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIREDIRECTRESULTLISTENER
  NS_DECL_NSIMULTIPARTCHANNELLISTENER

  NS_INLINE_DECL_STATIC_IID(HTTP_CHANNEL_PARENT_IID)

  HttpChannelParent(dom::BrowserParent* iframeEmbedding,
                    nsILoadContext* aLoadContext,
                    PBOverrideStatus aOverrideStatus);

  [[nodiscard]] bool Init(const HttpChannelCreationArgs& aArgs);

  // Forwarded to nsHttpChannel::SetApplyConversion.
  void SetApplyConversion(bool aApplyConversion) {
    if (mChannel) {
      mChannel->SetApplyConversion(aApplyConversion);
    }
  }

  [[nodiscard]] nsresult OpenAlternativeOutputStream(
      const nsACString& type, int64_t predictedSize,
      nsIAsyncOutputStream** _retval);

  [[nodiscard]] CacheEntryWriteHandleParent* AllocCacheEntryWriteHandle();

  // Callbacks for each asynchronous tasks required in AsyncOpen
  // procedure, will call InvokeAsyncOpen when all the expected
  // tasks is finished successfully or when any failure happened.
  // @see mAsyncOpenBarrier.
  void TryInvokeAsyncOpen(nsresult aRv);

  void InvokeAsyncOpen(nsresult rv);

  void InvokeEarlyHintPreloader(nsresult rv, uint64_t aEarlyHintPreloaderId);

  // Calls SendSetPriority if mIPCClosed is false.
  void DoSendSetPriority(int16_t aValue);

  // Calls SendReportLNAToConsole if mIPCClosed is false.
  void DoSendReportLNAToConsole(const NetAddr& aPeerAddr,
                                const nsACString& aMessageType,
                                const nsACString& aPromptAction,
                                const nsACString& aTopLevelSite);

  // The content process this channel parent belongs to. Used by
  // BackgroundChannelRegistrar to ensure a background channel is only linked
  // to a channel from the same process.
  dom::ContentParentId GetContentParentId() const;

  // Callback while background channel is ready.
  void OnBackgroundParentReady(HttpBackgroundChannelParent* aBgParent);
  // Callback while background channel is destroyed.
  void OnBackgroundParentDestroyed();

  base::ProcessId OtherPid() const;

  // Inform the child actor that our referrer info was modified late during
  // BeginConnect.
  void OverrideReferrerInfoDuringBeginConnect(nsIReferrerInfo* aReferrerInfo);

  // Set the cookie changes triggered by the channel, which will be applied by
  // the child actor during PHttpBackgroundChannel::OnStartRequest. Note that
  // CookieService also sends the information to all actors via PContent, a main
  // thread IPC, which could be slower than background IPC
  // PHttpBackgroundChannel::OnStartRequest.
  void SetCookieChanges(nsTArray<CookieChange>&& aCookieChanges);

  using ChildEndpointPromise =
      MozPromise<ipc::Endpoint<extensions::PStreamFilterChild>, bool, true>;
  [[nodiscard]] RefPtr<ChildEndpointPromise> AttachStreamFilter(
      Endpoint<extensions::PStreamFilterParent>&& aParentEndpoint,
      Endpoint<extensions::PStreamFilterChild>&& aChildEndpoint);
  [[nodiscard]] RefPtr<GenericPromise> DetachStreamFilters();

  // Should only be called from EarlyHintPreloader. mChannel should be null at
  // the point of calling. Sets mChannel to aChannel. Used by the
  // EarlyHintPreloader to redirect the channel to this parent as soon as the
  // final channel becomes available after all http redirects.
  void SetHttpChannelFromEarlyHintPreloader(HttpBaseChannel* aChannel);

 protected:
  // used to connect redirected-to channel in parent with just created
  // ChildChannel.  Used during redirects.
  [[nodiscard]] bool ConnectChannel(const uint32_t& registrarId);

  [[nodiscard]] bool DoAsyncOpen(
      nsIURI* uri, nsIURI* originalUri, nsIURI* docUri,
      nsIReferrerInfo* aReferrerInfo, nsIURI* topWindowUri,
      const uint32_t& loadFlags, const RequestHeaderTuples& requestHeaders,
      const nsCString& requestMethod, const Maybe<IPCStream>& uploadStream,
      const bool& uploadStreamHasHeaders, const int16_t& priority,
      const ClassOfService& classOfService, const uint8_t& redirectionLimit,
      const uint32_t& thirdPartyFlags, const bool& doResumeAt,
      const uint64_t& startPos, const nsCString& entityID,
      const bool& allowSpdy, const bool& allowHttp3, const bool& allowAltSvc,
      const bool& beConservative, const bool& bypassProxy,
      const uint32_t& tlsFlags, const LoadInfoArgs& aLoadInfoArgs,
      const uint32_t& aCacheKey, const uint64_t& aRequestContextID,
      const Maybe<CorsPreflightArgs>& aCorsPreflightArgs,
      const uint32_t& aInitialRwin, const bool& aBlockAuthPrompt,
      const bool& aAllowStaleCacheContent,
      const bool& aPreferCacheLoadOverBypass, const nsCString& aContentTypeHint,
      const dom::RequestMode& aRequestMode, const uint32_t& aRedirectMode,
      const uint64_t& aChannelId, const uint64_t& aContentWindowId,
      const nsTArray<PreferredAlternativeDataTypeParams>&
          aPreferredAlternativeTypes,
      const uint64_t& aBrowserId, const TimeStamp& aLaunchServiceWorkerStart,
      const TimeStamp& aLaunchServiceWorkerEnd,
      const TimeStamp& aDispatchFetchEventStart,
      const TimeStamp& aDispatchFetchEventEnd,
      const TimeStamp& aHandleFetchEventStart,
      const TimeStamp& aHandleFetchEventEnd,
      const bool& aForceMainDocumentChannel,
      const TimeStamp& aNavigationStartTimeStamp,
      const uint64_t& aEarlyHintPreloaderId,
      const nsAString& aClassicScriptHintCharset,
      const nsAString& aDocumentCharacterSet,
      const bool& aIsUserAgentHeaderModified, const nsString& aInitiatorType);

  virtual mozilla::ipc::IPCResult RecvSetPriority(
      const int16_t& priority) override;
  virtual mozilla::ipc::IPCResult RecvSetClassOfService(
      const ClassOfService& cos) override;
  virtual mozilla::ipc::IPCResult RecvSuspend() override;
  virtual mozilla::ipc::IPCResult RecvResume() override;
  virtual mozilla::ipc::IPCResult RecvCancel(
      const nsresult& status, const uint32_t& requestBlockingReason,
      const nsACString& reason,
      const mozilla::Maybe<nsCString>& logString) override;
  virtual mozilla::ipc::IPCResult RecvRedirect2Verify(
      const nsresult& result, const RequestHeaderTuples& changedHeaders,
      const uint32_t& aSourceRequestBlockingReason,
      const Maybe<ChildLoadInfoForwarderArgs>& aTargetLoadInfoForwarder,
      const uint32_t& loadFlags, nsIReferrerInfo* aReferrerInfo,
      const Maybe<CorsPreflightArgs>& aCorsPreflightArgs) override;
  virtual mozilla::ipc::IPCResult RecvDocumentChannelCleanup(
      const bool& clearCacheEntry) override;
  virtual mozilla::ipc::IPCResult RecvRemoveCorsPreflightCacheEntry(
      nsIURI* uri, const mozilla::ipc::PrincipalInfo& requestingPrincipal,
      const OriginAttributes& originAttributes) override;
  virtual mozilla::ipc::IPCResult RecvBytesRead(const int32_t& aCount) override;
  virtual mozilla::ipc::IPCResult RecvOpenOriginalCacheInputStream() override;
  virtual void ActorDestroy(ActorDestroyReason why) override;

  friend class ParentChannelListener;
  RefPtr<mozilla::dom::BrowserParent> mBrowserParent;

  [[nodiscard]] nsresult ReportSecurityMessage(
      const nsAString& aMessageTag, const nsAString& aMessageCategory) override;
  nsresult LogBlockedCORSRequest(const nsAString& aMessage,
                                 const nsACString& aCategory,
                                 bool aIsWarning = false) override;
  nsresult LogMimeTypeMismatch(const nsACString& aMessageName, bool aWarning,
                               const nsAString& aURL,
                               const nsAString& aContentType) override;

  // Calls SendDeleteSelf and sets mIPCClosed to true because we should not
  // send any more messages after that. Bug 1274886
  [[nodiscard]] bool DoSendDeleteSelf();
  // Called to notify the parent channel to not send any more IPC messages.
  virtual mozilla::ipc::IPCResult RecvDeletingChannel() override;

 private:
  already_AddRefed<nsITransportSecurityInfo> SecurityInfo();

  // final step for Redirect2Verify procedure, will be invoked while both
  // redirecting and redirected channel are ready or any error happened.
  // OnRedirectVerifyCallback will be invoked for finishing the async
  // redirect verification procedure.
  void ContinueRedirect2Verify(const nsresult& aResult);

  void AsyncOpenFailed(nsresult aRv);

  // Request to pair with a HttpBackgroundChannelParent with the same channel
  // id, a promise will be returned so the caller can append callbacks on it.
  // If called multiple times before mBgParent is available, the same promise
  // will be returned and the callbacks will be invoked in order.
  [[nodiscard]] RefPtr<GenericNonExclusivePromise> WaitForBgParent(
      uint64_t aChannelId);

  // Remove the association with background channel after main-thread IPC
  // is about to be destroyed or no further event is going to be sent, i.e.,
  // DocumentChannelCleanup.
  void CleanupBackgroundChannel();

  // Check if the channel needs to enable the flow control on the IPC channel.
  // That is, we may suspend the channel if the ODA-s to child process are not
  // consumed quickly enough. Otherwise, memory explosion could happen.
  bool NeedFlowControl();

  // Get the appropriate event target for background parent operations based on
  // channel's class of service flags: synchronous event target for urgent
  // channels, queued for others to balance responsiveness and prevent
  // head-of-line blocking.
  nsCOMPtr<nsISerialEventTarget> GetEventTargetForBgParentWait();

  bool IsRedirectDueToAuthRetry(uint32_t redirectFlags);

  int32_t mSendWindowSize;

  friend class HttpBackgroundChannelParent;

  uint64_t mEarlyHintPreloaderId{};

  RefPtr<HttpBaseChannel> mChannel;
  nsCOMPtr<nsICacheEntry> mCacheEntry;

  nsCOMPtr<nsIChannel> mRedirectChannel;
  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;

  nsCOMPtr<nsILoadContext> mLoadContext;
  RefPtr<nsHttpHandler> mHttpHandler;

  RefPtr<ParentChannelListener> mParentListener;

  RefPtr<ChannelEventQueue> mEventQ;

  RefPtr<HttpBackgroundChannelParent> mBgParent;

  MozPromiseHolder<GenericNonExclusivePromise> mPromise;
  MozPromiseRequestHolder<GenericNonExclusivePromise> mRequest;

  // To calculate the delay caused by the e10s back-pressure suspension
  TimeStamp mResumedTimestamp;

  Atomic<bool> mIPCClosed;  // PHttpChannel actor has been Closed()

  // Corresponding redirect channel registrar Id. 0 means redirection is not
  // started.
  uint64_t mRedirectChannelId = 0;

  PBOverrideStatus mPBOverride;

  // Set to the canceled status value if the main channel was canceled.
  nsresult mStatus;

  // The referrer info, set during nsHttpChannel::BeginConnect, to override the
  // original one. This info will be sent in OnStartRequest.
  nsCOMPtr<nsIReferrerInfo> mOverrideReferrerInfo;

  // The cookie changes received while processing the Set-Cookie header. This
  // info will be sent in OnStartRequest.
  nsTArray<CookieChange> mCookieChanges;

  // OnStatus is always called before OnProgress.
  // Set true in OnStatus if next OnProgress can be ignored
  // since the information can be recontructed from ODA.
  uint8_t mIgnoreProgress : 1;

  uint8_t mHasSuspendedByBackPressure : 1;

  // Set if we get the result of and cache |mNeedFlowControl|
  uint8_t mCacheNeedFlowControlInitialized : 1;
  uint8_t mNeedFlowControl : 1;
  uint8_t mSuspendedForFlowControl : 1;

  // Defaults to false. Is set to true at the begining of OnStartRequest.
  // Used to ensure methods can't be called before OnStartRequest.
  uint8_t mAfterOnStartRequestBegun : 1;

  // Number of events to wait before actually invoking AsyncOpen on the main
  // channel. For each asynchronous step required before InvokeAsyncOpen, should
  // increase 1 to mAsyncOpenBarrier and invoke TryInvokeAsyncOpen after
  // finished. This attribute is main thread only.
  uint8_t mAsyncOpenBarrier = 0;

  // When true, ODAs are sent from the socket process to the child process
  // directly.
  uint8_t mDataSentToChildProcess : 1;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_HttpChannelParent_h
