/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_contentanalysis_h
#define mozilla_contentanalysis_h

#include "mozilla/MozPromise.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/glean/ContentanalysisMetrics.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/WeakPtr.h"
#include "nsIClipboard.h"
#include "nsIContentAnalysis.h"
#include "nsITransferable.h"
#include "nsString.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsTStringHasher.h"

#include <regex>

class nsBaseClipboard;
class nsIPrincipal;
class nsIPrintSettings;
class ContentAnalysisTest;
class ContentAnalysisTelemetryTest;

namespace mozilla::dom {
class CanonicalBrowsingContext;
class DataTransfer;
class WindowGlobalParent;
}  // namespace mozilla::dom

namespace mozilla::contentanalysis {
class ContentAnalysisBackend;
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

  ContentAnalysisRequest(nsTArray<uint8_t> aPrintData, nsCOMPtr<nsIURI> aUrl,
                         nsString aPrinterName, Reason aReason,
                         dom::WindowGlobalParent* aWindowGlobalParent);

  ContentAnalysisRequest(const ContentAnalysisRequest&) = delete;
  ContentAnalysisRequest& operator=(ContentAnalysisRequest&) = delete;

  static nsresult GetFileDigest(const nsAString& aFilePath,
                                nsCString& aDigestString);

  static RefPtr<ContentAnalysisRequest> Clone(
      nsIContentAnalysisRequest* aRequest);

 private:
  virtual ~ContentAnalysisRequest();
  ContentAnalysisRequest() = default;

  // See nsIContentAnalysisRequest for values
  AnalysisType mAnalysisType = AnalysisType::eUnspecified;

  // See nsIContentAnalysisRequest for values
  Reason mReason = Reason::eUnknown;

  nsCOMPtr<nsITransferable> mTransferable;
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
  OperationType mOperationTypeForDisplay = OperationType::ePasteClipboard;

  // File name to display if mOperationTypeForDisplay is
  // eUpload or eDownload.
  nsString mFileNameForDisplay;

  // The name of the printer being printed to
  nsString mPrinterName;

  RefPtr<dom::WindowGlobalParent> mWindowGlobalParent;

  // The printed data to analyze, in PDF format.
  nsTArray<uint8_t> mPrintData;

  // WindowGlobalParent that is the origin of the data in the request, if known.
  RefPtr<mozilla::dom::WindowGlobalParent> mSourceWindowGlobal;

  // What to multiply the timeout for this request by. Only needed if there are
  // requests with multiple userActionIds that are logically grouped together.
  uint32_t mTimeoutMultiplier = 1;

  // Submit request to agent, even if it was already canceled.  Always false
  // if not in tests.
  bool mTestOnlyAlwaysSubmitToAgent = false;

  friend class ::ContentAnalysisTest;
  template <typename T, typename... Args>
  friend RefPtr<T> mozilla::MakeRefPtr(Args&&...);
};

#define CONTENTANALYSIS_IID \
  {0xa37bed74, 0x4b50, 0x443a, {0xbf, 0x58, 0xf4, 0xeb, 0xbd, 0x30, 0x67, 0xb4}}

class ContentAnalysisResponse;
class ContentAnalysis final : public nsIContentAnalysis,
                              public nsIObserver,
                              public SupportsWeakPtr {
 public:
  NS_INLINE_DECL_STATIC_IID(CONTENTANALYSIS_IID)
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTANALYSIS
  NS_DECL_NSIOBSERVER

  ContentAnalysis();

  ContentAnalysis(const ContentAnalysis&) = delete;
  ContentAnalysis& operator=(ContentAnalysis&) = delete;

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

  // Checks data that aWindow is writing to the clipboard. aWindow must be
  // non-null, and parent-process and chrome copies are not analyzed. Only the
  // global clipboard should be checked -- see nsBaseClipboard::SetData.
  static void CheckClipboardCopyContentAnalysis(
      mozilla::dom::WindowGlobalParent* aWindow, nsITransferable* aTransferable,
      ContentAnalysisCallback* aResolver);

  // Whether aRequesting is web content in the page whose top-level document
  // has inner window id aSourceTopInnerWindowId and whose document principal
  // subsumes aSourcePrincipal. null, chrome and system-principal windows return
  // false.
  static bool IsSamePageAndSite(dom::WindowGlobalParent* aRequesting,
                                uint64_t aSourceTopInnerWindowId,
                                nsIPrincipal* aSourcePrincipal);

  using FilesAllowedPromise = MozPromise<nsCOMArray<nsIFile>, nsresult, true>;
  // Checks the passed in files in "batch mode", meaning that all requests will
  // be done even if some of them are BLOCKED.  Unlike the other Check
  // methods, "batch mode" requests do not all share a user action ID.
  // This also consolidates the busy dialogs for the files into one that is
  // associated with the "primary" request's user action ID -- that is, the
  // user action ID of the first request generated.
  // Note that aURI is only necessary to pass in in gtests; otherwise we'll
  // get the URI from aWindow.
  static RefPtr<FilesAllowedPromise> CheckUploadsInBatchMode(
      nsCOMArray<nsIFile>&& aFiles, bool aAutoAcknowledge,
      mozilla::dom::WindowGlobalParent* aWindow,
      nsIContentAnalysisRequest::Reason aReason, nsIURI* aURI = nullptr);

  static RefPtr<ContentAnalysis> GetContentAnalysisFromService();

  void HandleResponseFromAgent(ContentAnalysisResponse* aResponse,
                               bool aAutoAcknowledge);

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

  // Returns whether the backend is currently creating its agent connection.
  // Only to be called from tests.
  bool GetCreatingClientForTest();

  // Thread-safe check whether the service is being destroyed.
  bool IsShutDown();

  // Thread-safe check whether the given user action has already been
  // canceled (either by user action or because a request earlier in the same
  // user action was canceled).
  bool WasUserActionCanceled(const nsACString& aUserActionId);

  // Check whether the given request token is waiting for a WARN dialog to be
  // responded to.
  bool IsRequestWaitingForWarnDialog(const nsACString& aRequestToken);

 private:
  virtual ~ContentAnalysis();

  nsresult RunAnalyzeRequestTask(
      const nsCOMPtr<nsIContentAnalysisRequest>& aRequest,
      bool aAutoAcknowledge,
      const nsCOMPtr<nsIContentAnalysisCallback>& aCallback);
  nsresult RunAcknowledgeTask(
      nsIContentAnalysisAcknowledgement* aAcknowledgement,
      const nsACString& aRequestToken);

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
    nsCOMPtr<nsIContentAnalysisCallback> mCallback;
    nsCString mUserActionId;

    // Number of CA requests remaining for this transaction.
    size_t mNumCARequestsRemaining = 0;

    // True if we have issued a response for these requests.
    bool mResponded = false;
  };

  Result<RefPtr<RequestsPromise::AllPromiseType>,
         nsCOMPtr<nsIContentAnalysisResult>>
  GetFinalRequestList(const ContentAnalysisRequestArray& aRequests);

  Result<RefPtr<RequestsPromise>, nsresult> ExpandFolderRequest(
      nsIContentAnalysisRequest* aRequest, nsIFile* file);

  bool mSetByEnterprise;

  // Backend engine that produces analysis verdicts.
  RefPtr<ContentAnalysisBackend> mBackend;

  struct UserActionData final {
    nsCOMPtr<nsIContentAnalysisCallback> mCallback;
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
  friend class ::ContentAnalysisTelemetryTest;
};

class ContentAnalysisResponse final : public nsIContentAnalysisResponse,
                                      public nsIClassInfo {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICLASSINFO
  NS_DECL_NSICONTENTANALYSISRESULT
  NS_DECL_NSICONTENTANALYSISRESPONSE

  void SetOwner(ContentAnalysis* aOwner);
  void DoNotAcknowledge() { mDoNotAcknowledge = true; }
  void SetCancelError(CancelError aCancelError);
  void SetIsCachedResponse() { mIsCachedResponse = true; }
  // Remove unneeded copy constructor/assignment
  ContentAnalysisResponse(const ContentAnalysisResponse&) = delete;
  ContentAnalysisResponse& operator=(ContentAnalysisResponse&) = delete;

 private:
  virtual ~ContentAnalysisResponse() = default;
  ContentAnalysisResponse(Action aAction, const nsACString& aRequestToken,
                          const nsACString& aUserActionId,
                          bool aIsSynthetic = false);

  // Use MakeRefPtr as factory.
  template <typename T, typename... Args>
  friend RefPtr<T> mozilla::MakeRefPtr(Args&&...);

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

  // Whether this is a synthesizic response from Firefox (as opposed to a
  // response from a DLP agent).
  // Synthetic responses ignore browser.contentanalysis.show_blocked_result and
  // always show a blocked result for blocked content, since there is no agent
  // that could have shown one for us.
  bool mIsSyntheticResponse = false;

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
