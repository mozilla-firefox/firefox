/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_contentanalysis_h
#define mozilla_contentanalysis_h

#include "mozilla/MoveOnlyFunction.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/WeakPtr.h"
#include "nsIClipboard.h"
#include "nsIContentAnalysis.h"
#include "nsIThreadPool.h"
#include "nsITransferable.h"
#include "nsString.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsTStringHasher.h"

#include <atomic>
#include <regex>
#include <string>

#ifdef XP_WIN
#  include <windows.h>
#endif  // XP_WIN

class nsBaseClipboard;
class nsIPrincipal;
class nsIPrintSettings;
class ContentAnalysisTest;

namespace mozilla::dom {
class CanonicalBrowsingContext;
class DataTransfer;
class WindowGlobalParent;
}  // namespace mozilla::dom

namespace content_analysis::sdk {
class Client;
class ContentAnalysisRequest;
class ContentAnalysisResponse;
}  // namespace content_analysis::sdk

namespace mozilla::contentanalysis {
class ContentAnalysisCallback;

enum class DefaultResult : uint8_t {
  eBlock = 0,
  eWarn = 1,
  eAllow = 2,
  eLastValue = 2
};

class ContentAnalysisDiagnosticInfo final
    : public nsIContentAnalysisDiagnosticInfo {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTANALYSISDIAGNOSTICINFO
  ContentAnalysisDiagnosticInfo(bool aConnectedToAgent, nsString aAgentPath,
                                bool aFailedSignatureVerification,
                                int64_t aRequestCount)
      : mConnectedToAgent(aConnectedToAgent),
        mAgentPath(std::move(aAgentPath)),
        mFailedSignatureVerification(aFailedSignatureVerification),
        mRequestCount(aRequestCount) {}

 private:
  virtual ~ContentAnalysisDiagnosticInfo() = default;
  bool mConnectedToAgent;
  nsString mAgentPath;
  bool mFailedSignatureVerification;
  int64_t mRequestCount;
};

class ContentAnalysisRequest final : public nsIContentAnalysisRequest {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTANALYSISREQUEST

  ContentAnalysisRequest(AnalysisType aAnalysisType, Reason aReason,
                         nsString aString, bool aStringIsFilePath,
                         nsCString aSha256Digest, nsCOMPtr<nsIURI> aUrl,
                         OperationType aOperationType,
                         dom::WindowGlobalParent* aWindowGlobalParent,
                         dom::WindowGlobalParent* aSourceWindowGlobal = nullptr,
                         nsCString&& aUserActionId = nsCString());

  ContentAnalysisRequest(AnalysisType aAnalysisType, Reason aReason,
                         nsITransferable* aTransferable,
                         dom::WindowGlobalParent* aWindowGlobal,
                         dom::WindowGlobalParent* aSourceWindowGlobal);

  ContentAnalysisRequest(const nsTArray<uint8_t> aPrintData,
                         nsCOMPtr<nsIURI> aUrl, nsString aPrinterName,
                         Reason aReason,
                         dom::WindowGlobalParent* aWindowGlobalParent);
  static nsresult GetFileDigest(const nsAString& aFilePath,
                                nsCString& aDigestString);

 private:
  virtual ~ContentAnalysisRequest();

  // Remove unneeded copy constructor/assignment
  ContentAnalysisRequest(const ContentAnalysisRequest&) = delete;
  ContentAnalysisRequest& operator=(ContentAnalysisRequest&) = delete;

  // See nsIContentAnalysisRequest for values
  AnalysisType mAnalysisType;

  // See nsIContentAnalysisRequest for values
  Reason mReason;

  RefPtr<nsITransferable> mTransferable;
  RefPtr<dom::DataTransfer> mDataTransfer;

  // Text content to analyze.  Only one of textContent or filePath is defined.
  nsString mTextContent;

  // Name of file to analyze.  Only one of textContent or filePath is defined.
  nsString mFilePath;

  // The URL containing the file download/upload or to which web content is
  // being uploaded.
  nsCOMPtr<nsIURI> mUrl;

  // Sha256 digest of file.
  nsCString mSha256Digest;

  // URLs involved in the download.
  nsTArray<RefPtr<nsIClientDownloadResource>> mResources;

  // Email address of user.
  nsString mEmail;

  // Unique identifier for this request
  nsCString mRequestToken;

  // Unique identifier for this user action.
  // For example, all requests that come from uploading multiple files
  // or one clipboard operation should have the same value.
  nsCString mUserActionId;

  // The number of requests associated with this mUserActionId.
  int64_t mUserActionRequestsCount = 1;

  // Type of text to display, see nsIContentAnalysisRequest for values
  OperationType mOperationTypeForDisplay;

  // String to display if mOperationTypeForDisplay is
  // OPERATION_CUSTOMDISPLAYSTRING
  nsString mOperationDisplayString;

  // The name of the printer being printed to
  nsString mPrinterName;

  RefPtr<dom::WindowGlobalParent> mWindowGlobalParent;
#ifdef XP_WIN
  // The printed data to analyze, in PDF format
  HANDLE mPrintDataHandle = 0;
  // The size of the printed data in mPrintDataHandle
  uint64_t mPrintDataSize = 0;
#endif

  // WindowGlobalParent that is the origin of the data in the request, if known.
  RefPtr<mozilla::dom::WindowGlobalParent> mSourceWindowGlobal;

  // What to multiply the timeout for this request by. Only needed if there are
  // requests with multiple userActionIds that are logically grouped together.
  uint32_t mTimeoutMultiplier = 1;

  // Submit request to agent, even if it was already canceled.  Always false
  // if not in tests.
  bool mTestOnlyAlwaysSubmitToAgent = false;

  friend class ::ContentAnalysisTest;
};

#define CONTENTANALYSIS_IID \
  {0xa37bed74, 0x4b50, 0x443a, {0xbf, 0x58, 0xf4, 0xeb, 0xbd, 0x30, 0x67, 0xb4}}

class ContentAnalysisResponse;
class ContentAnalysis final : public nsIContentAnalysis,
                              public nsIObserver,
                              public SupportsWeakPtr {
 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(CONTENTANALYSIS_IID)
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTANALYSIS
  NS_DECL_NSIOBSERVER

  ContentAnalysis();

#if defined(XP_WIN)
  struct PrintAllowedResult final {
    bool mAllowed;
    dom::MaybeDiscarded<dom::BrowsingContext>
        mCachedStaticDocumentBrowsingContext;
    PrintAllowedResult(bool aAllowed, dom::MaybeDiscarded<dom::BrowsingContext>
                                          aCachedStaticDocumentBrowsingContext)
        : mAllowed(aAllowed),
          mCachedStaticDocumentBrowsingContext(
              aCachedStaticDocumentBrowsingContext) {}
    explicit PrintAllowedResult(bool aAllowed)
        : PrintAllowedResult(aAllowed, dom::MaybeDiscardedBrowsingContext()) {}
  };
  struct PrintAllowedError final {
    nsresult mError;
    dom::MaybeDiscarded<dom::BrowsingContext>
        mCachedStaticDocumentBrowsingContext;
    PrintAllowedError(nsresult aError, dom::MaybeDiscarded<dom::BrowsingContext>
                                           aCachedStaticDocumentBrowsingContext)
        : mError(aError),
          mCachedStaticDocumentBrowsingContext(
              aCachedStaticDocumentBrowsingContext) {}
    explicit PrintAllowedError(nsresult aError)
        : PrintAllowedError(aError, dom::MaybeDiscardedBrowsingContext()) {}
  };
  using PrintAllowedPromise =
      MozPromise<PrintAllowedResult, PrintAllowedError, true>;
  MOZ_CAN_RUN_SCRIPT static RefPtr<PrintAllowedPromise>
  PrintToPDFToDetermineIfPrintAllowed(
      dom::CanonicalBrowsingContext* aBrowsingContext,
      nsIPrintSettings* aPrintSettings);
#endif  // defined(XP_WIN)

  // Find the outermost browsing context that has same-origin access to
  // aBrowsingContext, and this is the URL we will pass to the Content Analysis
  // agent.
  static nsCOMPtr<nsIURI> GetURIForBrowsingContext(
      dom::CanonicalBrowsingContext* aBrowsingContext);
  static bool CheckClipboardContentAnalysisSync(
      nsBaseClipboard* aClipboard, mozilla::dom::WindowGlobalParent* aWindow,
      const nsCOMPtr<nsITransferable>& trans,
      nsIClipboard::ClipboardType aClipboardType);
  static void CheckClipboardContentAnalysis(
      nsBaseClipboard* aClipboard, mozilla::dom::WindowGlobalParent* aWindow,
      nsITransferable* aTransferable,
      nsIClipboard::ClipboardType aClipboardType,
      ContentAnalysisCallback* aResolver, bool aForFullClipboard = false);

  using FilesAllowedPromise = MozPromise<nsCOMArray<nsIFile>, nsresult, true>;
  // Checks the passed in files in "batch mode", meaning that all requests will
  // be done even if some of them are BLOCKED.  Unlike the other Check
  // methods, "batch mode" requests do not all share a user action ID.
  // This also consolidates the busy dialogs for the files into one that is
  // associated with the "primary" request's user action ID -- that is, the
  // user action ID of the first request generated.
  // Note that aURI is only necessary to pass in in gtests; otherwise we'll
  // get the URI from aWindow.
  static RefPtr<FilesAllowedPromise> CheckFilesInBatchMode(
      nsCOMArray<nsIFile>&& aFiles, mozilla::dom::WindowGlobalParent* aWindow,
      nsIContentAnalysisRequest::Reason aReason, nsIURI* aURI = nullptr);

  static RefPtr<ContentAnalysis> GetContentAnalysisFromService();

  // Cancel all outstanding requests for the given user action ID.
  // aResult is used to determine what kind of cancellation this is
  // (user-initiated, timeout, blocked user action, internal error, etc).
  // The cancellation behavior is dependent on that value.  In particular,
  // some causes lead to programmable default behaviors -- see e.g.
  // browser.contentanalysis.default_result and
  // browser.contentanalysis.timeout_result.  Oothers, like user-initiated
  // and shutdown cancellations, have fixed behavior.
  void CancelWithError(nsCString&& aUserActionId, nsresult aResult);

  // These are the MIME types that Content Analysis can analyze.
  static constexpr const char* kKnownClipboardTypes[] = {
      kTextMime, kHTMLMime, kCustomTypesMime, kFileMime};

 private:
  virtual ~ContentAnalysis();
  // Remove unneeded copy constructor/assignment
  ContentAnalysis(const ContentAnalysis&) = delete;
  ContentAnalysis& operator=(ContentAnalysis&) = delete;
  // Only call this through CreateClientIfNecessary(), as it provides
  // synchronization to avoid doing this multiple times at once.
  nsresult CreateContentAnalysisClient(nsCString&& aPipePathName,
                                       nsString&& aClientSignatureSetting,
                                       bool aIsPerUser);

  // Thread pool that all agent communications happen on.  Content Analysis
  // occasionally uses other (random) background threads for other purposes.
  nsCOMPtr<nsIThreadPool> mThreadPool;

  // Helper function to retry calling the client in case either the client
  // does not exist, or calling the client fails (indicating that the DLP agent
  // has terminated and possibly restarted)
  //
  // aClientCallFunc - gets called on a background thread after we have a
  // client. Returns a Result<T, nsresult>. An Err(nsresult) indicates
  // that the client call failed and we should try to reconnect. A successful
  // response indicates success (or at least that we should not try to
  // reconnect), and that value will be Resolve()d into the returned MozPromise.
  template <typename T, typename U>
  RefPtr<MozPromise<T, nsresult, true>> CallClientWithRetry(
      StaticString aMethodName, U&& aClientCallFunc);

  nsresult RunAnalyzeRequestTask(
      const RefPtr<nsIContentAnalysisRequest>& aRequest, bool aAutoAcknowledge,
      const RefPtr<nsIContentAnalysisCallback>& aCallback);
  nsresult RunAcknowledgeTask(
      nsIContentAnalysisAcknowledgement* aAcknowledgement,
      const nsACString& aRequestToken);
  nsresult CreateClientIfNecessary(bool aForceCreate = false);

  // Actually send the request to the client and handle the response (or error).
  // Note that the response may be for a different request!
  static Result<std::nullptr_t, nsresult> DoAnalyzeRequest(
      nsCString&& aUserActionId,
      content_analysis::sdk::ContentAnalysisRequest&& aRequest,
      bool aAutoAcknowledge,
      const std::shared_ptr<content_analysis::sdk::Client>& aClient,
      bool aTestOnlyIgnoreCanceled = false);

  static void HandleResponseFromAgent(
      content_analysis::sdk::ContentAnalysisResponse&& aResponse);

  struct UserActionIdAndAutoAcknowledge final {
    nsCString mUserActionId;
    bool mAutoAcknowledge;
  };
  DataMutex<nsTHashMap<nsCString, UserActionIdAndAutoAcknowledge>>
      mRequestTokenToUserActionIdMap;

  void IssueResponse(ContentAnalysisResponse* response,
                     nsCString&& aUserActionId, bool aAcknowledge,
                     bool aIsTooLate);
  void NotifyResponseObservers(ContentAnalysisResponse* aResponse,
                               nsCString&& aUserActionId, bool aAutoAcknowledge,
                               bool aIsTimeout);
  void NotifyObserversAndMaybeIssueResponseFromAgent(
      ContentAnalysisResponse* aResponse, nsCString&& aUserActionId,
      bool aAutoAcknowledge);

  // Destroy the service.  Happens during xpcom-shutdown-threads.
  void Close();

  // Thread-safe check whether the service is being destroyed.
  bool IsShutDown();

  // Did the URL filter completely handle the request or do we need to check
  // with the agent.
  enum UrlFilterResult { eCheck, eDeny, eAllow };

  UrlFilterResult FilterByUrlLists(nsIContentAnalysisRequest* aRequest,
                                   nsIURI* aUri);
  void EnsureParsedUrlFilters();

  using ContentAnalysisRequestArray =
      CopyableTArray<RefPtr<nsIContentAnalysisRequest>>;
  using RequestsPromise =
      MozPromise<ContentAnalysisRequestArray, nsresult, true>;

  // Counts the number of times it receives an "allow content" and (1) calls
  // ContentResult on mCallback when all requests are approved, (2) calls
  // ContentResult and cancels outstanding scans when any one request is
  // rejected, or (3) calls Error and cancels outstanding scans when any one
  // fails.
  // Once constructed, this object is required to eventually issue a response to
  // the given callback.
  // This class doesn't care if it receives more calls than there are requests.
  // Canceling issues callback calls with no initiating request.  This class
  // relays the verdicts on a first-come-first-served basis, so a cancel
  // that comes before an allow overrides that allow, and vice-versa.
  class MultipartRequestCallback : public nsIContentAnalysisCallback {
   public:
    NS_INLINE_DECL_REFCOUNTING(MultipartRequestCallback, override)
    NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;

    NS_DECL_NSICONTENTANALYSISCALLBACK

    static RefPtr<MultipartRequestCallback> Create(
        ContentAnalysis* aContentAnalysis,
        const nsTArray<ContentAnalysis::ContentAnalysisRequestArray>& aRequests,
        nsIContentAnalysisCallback* aCallback, bool aAutoAcknowledge);

    bool HasResponded() const { return mResponded; }

   private:
    MultipartRequestCallback() = default;
    virtual ~MultipartRequestCallback();

    template <typename T, typename... Args>
    friend RefPtr<T> mozilla::MakeRefPtr(Args&&...);

    void Initialize(
        ContentAnalysis* aContentAnalysis,
        const nsTArray<ContentAnalysis::ContentAnalysisRequestArray>& aRequests,
        nsIContentAnalysisCallback* aCallback, bool aAutoAcknowledge);

    void CancelRequests();
    void RemoveFromUserActionMap();

    WeakPtr<ContentAnalysis> mWeakContentAnalysis;
    RefPtr<nsIContentAnalysisCallback> mCallback;
    nsCString mUserActionId;

    // Number of CA requests remaining for this transaction.
    size_t mNumCARequestsRemaining;

    // True if we have issued a response for these requests.
    bool mResponded = false;
  };

  Result<RefPtr<RequestsPromise::AllPromiseType>,
         RefPtr<nsIContentAnalysisResult>>
  GetFinalRequestList(const ContentAnalysisRequestArray& aRequests);

  Result<RefPtr<RequestsPromise>, nsresult> ExpandFolderRequest(
      nsIContentAnalysisRequest* aRequest, nsIFile* file);

  using ClientPromise =
      MozPromise<std::shared_ptr<content_analysis::sdk::Client>, nsresult,
                 false>;
  int64_t mRequestCount = 0;
  // Must only be resolved/rejected or Then()'d on the main thread.
  //
  // Note that if this promise is resolved, the resolve value will
  // be a non-null content_analysis::sdk::Client. However, if the
  // DLP agent process has terminated, it is possible that trying to
  // call into this client will return an error. Therefore, any
  // method that wants to call into the client should go through
  // CallClientWithRetry() to make it easy to try reconnecting
  // to the client.
  RefPtr<ClientPromise::Private> mCaClientPromise
      MOZ_GUARDED_BY(sMainThreadCapability);
  bool mCreatingClient MOZ_GUARDED_BY(sMainThreadCapability) = false;
  bool mHaveResolvedClientPromise MOZ_GUARDED_BY(sMainThreadCapability) = false;

  bool mSetByEnterprise;

  struct UserActionData final {
    RefPtr<nsIContentAnalysisCallback> mCallback;
    nsTHashSet<nsCString> mRequestTokens;
    RefPtr<mozilla::CancelableRunnable> mTimeoutRunnable;
    bool mAutoAcknowledge;
    bool mIsHandlingTimeout = false;
  };

  // This map is stored so that requests can be canceled while they are
  // still being checked.  It is maintained by our inner class
  // MultipartRequestCallback.
  nsTHashMap<nsCString, UserActionData> mUserActionMap;
  void RemoveFromUserActionMap(nsCString&& aUserActionId);

  // The agent may respond to actions that we have canceled and we need to
  // remember how we handled them, whether it was to cancel (block) them,
  // or to issue a default response.
  struct CanceledResponse {
    nsIContentAnalysisAcknowledgement::FinalAction mAction;
    size_t mNumExpectedResponses;
  };
  using UserActionIdToCanceledResponseMap =
      nsTHashMap<nsCString, CanceledResponse>;
  DataMutex<UserActionIdToCanceledResponseMap>
      mUserActionIdToCanceledResponseMap{
          "ContentAnalysis::UserActionIdToCanceledResponseMap"};

  class CachedClipboardResponse {
   public:
    CachedClipboardResponse() = default;
    Maybe<nsIContentAnalysisResponse::Action> GetCachedResponse(
        nsIURI* aURI, int32_t aClipboardSequenceNumber);
    void SetCachedResponse(const nsCOMPtr<nsIURI>& aURI,
                           int32_t aClipboardSequenceNumber,
                           nsIContentAnalysisResponse::Action aAction);

   private:
    Maybe<int32_t> mClipboardSequenceNumber;
    nsTArray<std::pair<nsCOMPtr<nsIURI>, nsIContentAnalysisResponse::Action>>
        mData;
  };
  CachedClipboardResponse mCachedClipboardResponse;

  struct WarnResponseData {
    RefPtr<ContentAnalysisResponse> mResponse;
    nsCString mUserActionId;
    bool mAutoAcknowledge;
    bool mWasTimeout;
  };
  // Request token to warn response map.
  nsTHashMap<nsCString, WarnResponseData> mWarnResponseDataMap;

  std::vector<std::regex> mAllowUrlList;
  std::vector<std::regex> mDenyUrlList;
  bool mParsedUrlLists = false;
  bool mForbidFutureRequests = false;
  DataMutex<bool> mIsShutDown{false, "ContentAnalysis::IsShutDown"};

  // Set of sets of user action IDs.  Each set of IDs defines one compound
  // action.
  using UserActionSet = media::Refcountable<mozilla::HashSet<nsCString>>;
  using UserActionSets = mozilla::HashSet<RefPtr<const UserActionSet>,
                                          PointerHasher<const UserActionSet*>>;
  UserActionSets mCompoundUserActions;

  friend class ContentAnalysisResponse;
  friend class ::ContentAnalysisTest;
};

NS_DEFINE_STATIC_IID_ACCESSOR(ContentAnalysis, CONTENTANALYSIS_IID)

class ContentAnalysisResponse final : public nsIContentAnalysisResponse {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTANALYSISRESULT
  NS_DECL_NSICONTENTANALYSISRESPONSE

  void SetOwner(ContentAnalysis* aOwner);
  void DoNotAcknowledge() { mDoNotAcknowledge = true; }
  void SetCancelError(CancelError aCancelError);
  void SetIsCachedResponse() { mIsCachedResponse = true; }

 private:
  virtual ~ContentAnalysisResponse() = default;
  // Remove unneeded copy constructor/assignment
  ContentAnalysisResponse(const ContentAnalysisResponse&) = delete;
  ContentAnalysisResponse& operator=(ContentAnalysisResponse&) = delete;
  explicit ContentAnalysisResponse(
      content_analysis::sdk::ContentAnalysisResponse&& aResponse,
      const nsCString& aUserActionId);
  ContentAnalysisResponse(Action aAction, const nsACString& aRequestToken,
                          const nsACString& aUserActionId);

  // Use MakeRefPtr as factory.
  template <typename T, typename... Args>
  friend RefPtr<T> mozilla::MakeRefPtr(Args&&...);

  static already_AddRefed<ContentAnalysisResponse> FromProtobuf(
      content_analysis::sdk::ContentAnalysisResponse&& aResponse,
      const nsCString& aUserActionId);

  void ResolveWarnAction(bool aAllowContent);

  // Action requested by the agent
  Action mAction;

  // Identifiers for the corresponding nsIContentAnalysisRequest
  nsCString mRequestToken;
  nsCString mUserActionId;

  // If mAction is eCanceled, this is the error explaining why the request was
  // canceled, or eUserInitiated if the user canceled it.
  CancelError mCancelError = CancelError::eUserInitiated;

  // ContentAnalysis (or, more precisely, its Client object) must outlive
  // the transaction.
  RefPtr<ContentAnalysis> mOwner;

  // Whether the response has been acknowledged
  bool mHasAcknowledged = false;

  // If true, the request was completely handled by URL filter lists, so it
  // was not sent to the agent and should not send an Acknowledge.
  bool mDoNotAcknowledge = false;

  // Whether this is a cached result that wasn't actually sent to the DLP agent.
  // This indicates that the request was a duplicate of a previously sent one,
  // so any dialogs (for block/warn) should not be shown.
  bool mIsCachedResponse = false;

  // Whether this is a response from an agent or one synthesized by Firefox.
  // Synthetic responses ignore browser.contentanalysis.show_blocked_result and
  // always show a blocked result for blocked content, since there is no agent
  // that could have shown one for us.
  bool mIsAgentResponse = false;

  friend class ContentAnalysis;
};

class ContentAnalysisAcknowledgement final
    : public nsIContentAnalysisAcknowledgement {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTANALYSISACKNOWLEDGEMENT

  ContentAnalysisAcknowledgement(Result aResult, FinalAction aFinalAction);

 private:
  virtual ~ContentAnalysisAcknowledgement() = default;

  Result mResult;
  FinalAction mFinalAction;
};

/**
 * This class:
 * 1. Asserts if the callback is not called before destruction.
 * 2. Takes a strong reference to the nsIContentAnalysisResult when
 *    calling the callback, which guarantees that someone does.  Otherwise,
 *    if neither the caller nor the callback did, then the result would leak.
 */
class ContentAnalysisCallback final : public nsIContentAnalysisCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTANALYSISCALLBACK
  ContentAnalysisCallback(
      std::function<void(nsIContentAnalysisResult*)>&& aContentResponseCallback,
      std::function<void(nsresult)>&& aErrorCallback)
      : mContentResponseCallback(std::move(aContentResponseCallback)),
        mErrorCallback(std::move(aErrorCallback)) {}

  explicit ContentAnalysisCallback(
      std::function<void(nsIContentAnalysisResult*)>&&
          aContentResponseCallback);

  // Wrap a given callback, in case it doesn't provide the guarantees that
  // this one does (such as checking that it is eventually called).
  explicit ContentAnalysisCallback(nsIContentAnalysisCallback* aDecoratedCB) {
    mContentResponseCallback = [decoratedCB = RefPtr{aDecoratedCB}](
                                   nsIContentAnalysisResult* aResult) {
      decoratedCB->ContentResult(aResult);
    };
    mErrorCallback = [decoratedCB = RefPtr{aDecoratedCB}](nsresult aRv) {
      decoratedCB->Error(aRv);
    };
  }

 private:
  virtual ~ContentAnalysisCallback() {
    MOZ_ASSERT(!mContentResponseCallback && !mErrorCallback && !mPromise,
               "ContentAnalysisCallback never called!");
  }

  // Called after callbacks are called.
  void ClearCallbacks() {
    mContentResponseCallback = nullptr;
    mErrorCallback = nullptr;
    mPromise = nullptr;
  }

  explicit ContentAnalysisCallback(dom::Promise* aPromise);
  std::function<void(nsIContentAnalysisResult*)> mContentResponseCallback;
  std::function<void(nsresult)> mErrorCallback;
  RefPtr<dom::Promise> mPromise;
  friend class ContentAnalysis;
};

}  // namespace mozilla::contentanalysis

#endif  // mozilla_contentanalysis_h
