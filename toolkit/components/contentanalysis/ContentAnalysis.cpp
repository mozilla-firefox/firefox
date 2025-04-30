/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentAnalysis.h"
#include "ContentAnalysisIPCTypes.h"
#include "content_analysis/sdk/analysis_client.h"

#include "base/process_util.h"
#include "GMPUtils.h"  // ToHexString
#include "MainThreadUtils.h"
#include "mozilla/Array.h"
#include "mozilla/Components.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/DragEvent.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/GetFilesHelper.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/Logging.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_browser.h"
#include "nsAppRunner.h"
#include "nsBaseClipboard.h"
#include "nsComponentManagerUtils.h"
#include "nsIClassInfoImpl.h"
#include "nsIFile.h"
#include "nsIGlobalObject.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIPrintSettings.h"
#include "nsIStorageStream.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "nsProxyRelease.h"
#include "nsThreadPool.h"
#include "ScopedNSSTypes.h"
#include "xpcpublic.h"

#include <algorithm>
#include <sstream>
#include <string>

#ifdef XP_WIN
#  include <windows.h>
#  define SECURITY_WIN32 1
#  include <security.h>
#  include "mozilla/NativeNt.h"
#  include "mozilla/WinDllServices.h"
#endif  // XP_WIN

namespace mozilla::contentanalysis {

LazyLogModule gContentAnalysisLog("contentanalysis");
#define LOGD(...)                                        \
  MOZ_LOG(mozilla::contentanalysis::gContentAnalysisLog, \
          mozilla::LogLevel::Debug, (__VA_ARGS__))

#define LOGE(...)                                        \
  MOZ_LOG(mozilla::contentanalysis::gContentAnalysisLog, \
          mozilla::LogLevel::Error, (__VA_ARGS__))

}  // namespace mozilla::contentanalysis

namespace {

const char* kPipePathNamePref = "browser.contentanalysis.pipe_path_name";
const char* kClientSignature = "browser.contentanalysis.client_signature";
const char* kAllowUrlPref = "browser.contentanalysis.allow_url_regex_list";
const char* kDenyUrlPref = "browser.contentanalysis.deny_url_regex_list";

// Allow up to this many threads to be concurrently engaged in synchronous
// communcations with the agent.  That limit is set by
// browser.contentanalysis.max_connections but is clamped to not exceed
// this value.
const unsigned long kMaxContentAnalysisAgentThreads = 256;
// Max number of threads that we keep even if they have no tasks to run.
const unsigned long kMaxIdleContentAnalysisAgentThreads = 2;
// Time (ms) we wait before declaring a thread idle.  100ms is the
// threadpool default.
const unsigned long kIdleContentAnalysisAgentTimeoutMs = 100;
// Time we wait before destroying the kMaxIdleContentAnalysisAgentThreads
// threads.  Content Analysis never does this, which is what UINT32_MAX
// means.
const unsigned long kMaxIdleContentAnalysisAgentTimeoutMs = UINT32_MAX;

// How long the threadpool will wait at shutdown for the agent to complete any
// in-progress operations before it abandons the threads (they will keep
// running).
const uint32_t kShutdownThreadpoolTimeoutMs = 2 * 1000;

// kTextMime must be the first entry.
auto kTextFormatsToAnalyze = {kTextMime, kHTMLMime};

const char* SafeGetStaticErrorName(nsresult aRv) {
  const auto* ret = mozilla::GetStaticErrorName(aRv);
  return ret ? ret : "<illegal value>";
}

nsresult MakePromise(JSContext* aCx, mozilla::dom::Promise** aPromise) {
  nsIGlobalObject* go = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!go)) {
    return NS_ERROR_UNEXPECTED;
  }
  mozilla::ErrorResult result;
  RefPtr promise = mozilla::dom::Promise::Create(go, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }
  promise.forget(aPromise);
  return NS_OK;
}

static nsCString GenerateUUID() {
  nsID id = nsID::GenerateUUID();
  return nsCString(id.ToString().get());
}

static nsresult GetFileDisplayName(const nsString& aFilePath,
                                   nsString& aFileDisplayName) {
  nsCOMPtr<nsIFile> file;
  MOZ_TRY(NS_NewLocalFile(aFilePath, getter_AddRefs(file)));
  return file->GetDisplayName(aFileDisplayName);
}

nsIContentAnalysisAcknowledgement::FinalAction ConvertResult(
    nsIContentAnalysisResponse::Action aResponseResult) {
  switch (aResponseResult) {
    case nsIContentAnalysisResponse::Action::eReportOnly:
      return nsIContentAnalysisAcknowledgement::FinalAction::eReportOnly;
    case nsIContentAnalysisResponse::Action::eWarn:
      return nsIContentAnalysisAcknowledgement::FinalAction::eWarn;
    case nsIContentAnalysisResponse::Action::eBlock:
    case nsIContentAnalysisResponse::Action::eCanceled:
      return nsIContentAnalysisAcknowledgement::FinalAction::eBlock;
    case nsIContentAnalysisResponse::Action::eAllow:
      return nsIContentAnalysisAcknowledgement::FinalAction::eAllow;
    case nsIContentAnalysisResponse::Action::eUnspecified:
      return nsIContentAnalysisAcknowledgement::FinalAction::eUnspecified;
    default:
      LOGE(
          "ConvertResult got unexpected responseResult "
          "%d",
          static_cast<uint32_t>(aResponseResult));
      return nsIContentAnalysisAcknowledgement::FinalAction::eUnspecified;
  }
}

bool SourceIsSameTab(nsIContentAnalysisRequest* aRequest) {
  RefPtr<mozilla::dom::WindowGlobalParent> sourceWindowGlobal;
  MOZ_ALWAYS_SUCCEEDS(
      aRequest->GetSourceWindowGlobal(getter_AddRefs(sourceWindowGlobal)));
  if (!sourceWindowGlobal) {
    return false;
  }

  RefPtr<mozilla::dom::WindowGlobalParent> windowGlobal;
  MOZ_ALWAYS_SUCCEEDS(
      aRequest->GetWindowGlobalParent(getter_AddRefs(windowGlobal)));
  return windowGlobal->GetBrowsingContext()->Top() ==
             sourceWindowGlobal->GetBrowsingContext()->Top() &&
         windowGlobal->DocumentPrincipal() &&
         windowGlobal->DocumentPrincipal()->Subsumes(
             sourceWindowGlobal->DocumentPrincipal());
}

}  // anonymous namespace

/* static */ bool nsIContentAnalysis::MightBeActive() {
  // A DLP connection is not permitted to be added/removed while the
  // browser is running, so we can cache this.
  // Furthermore, if this is set via enterprise policy the pref will be locked
  // so users won't be able to change it.
  // Ideally we would make this a mirror: once pref, but this interacts in
  // some weird ways with the enterprise policy for testing purposes.
  static bool sIsEnabled =
      mozilla::StaticPrefs::browser_contentanalysis_enabled();
  // Note that we can't check gAllowContentAnalysis here because it
  // only gets set in the parent process.
  return sIsEnabled;
}

namespace mozilla::contentanalysis {
ContentAnalysisRequest::~ContentAnalysisRequest() {
#ifdef XP_WIN
  CloseHandle(mPrintDataHandle);
#endif
}

NS_IMETHODIMP
ContentAnalysisRequest::GetAnalysisType(AnalysisType* aAnalysisType) {
  *aAnalysisType = mAnalysisType;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetReason(Reason* aReason) {
  *aReason = mReason;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetTextContent(nsAString& aTextContent) {
  aTextContent = mTextContent;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetFilePath(nsAString& aFilePath) {
  aFilePath = mFilePath;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetPrintDataHandle(uint64_t* aPrintDataHandle) {
#ifdef XP_WIN
  uintptr_t printDataHandle = reinterpret_cast<uintptr_t>(mPrintDataHandle);
  uint64_t printDataValue = static_cast<uint64_t>(printDataHandle);
  *aPrintDataHandle = printDataValue;
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
ContentAnalysisRequest::GetPrinterName(nsAString& aPrinterName) {
  aPrinterName = mPrinterName;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetPrintDataSize(uint64_t* aPrintDataSize) {
#ifdef XP_WIN
  *aPrintDataSize = mPrintDataSize;
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
ContentAnalysisRequest::GetUrl(nsIURI** aUrl) {
  NS_ENSURE_ARG_POINTER(aUrl);
  NS_IF_ADDREF(*aUrl = mUrl);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetEmail(nsAString& aEmail) {
  aEmail = mEmail;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetSha256Digest(nsACString& aSha256Digest) {
  aSha256Digest = mSha256Digest;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetResources(
    nsTArray<RefPtr<nsIClientDownloadResource>>& aResources) {
  aResources = mResources.Clone();
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetRequestToken(nsACString& aRequestToken) {
  aRequestToken = mRequestToken;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::SetRequestToken(const nsACString& aRequestToken) {
  mRequestToken = aRequestToken;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetUserActionId(nsACString& aUserActionId) {
  aUserActionId = mUserActionId;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::SetUserActionId(const nsACString& aUserActionId) {
  mUserActionId = aUserActionId;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetUserActionRequestsCount(
    int64_t* aUserActionRequestsCount) {
  NS_ENSURE_ARG_POINTER(aUserActionRequestsCount);
  *aUserActionRequestsCount = mUserActionRequestsCount;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::SetUserActionRequestsCount(
    int64_t aUserActionRequestsCount) {
  mUserActionRequestsCount = aUserActionRequestsCount;
  return NS_OK;
}
NS_IMETHODIMP
ContentAnalysisRequest::GetOperationTypeForDisplay(
    OperationType* aOperationType) {
  *aOperationType = mOperationTypeForDisplay;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetOperationDisplayString(
    nsAString& aOperationDisplayString) {
  aOperationDisplayString = mOperationDisplayString;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetWindowGlobalParent(
    dom::WindowGlobalParent** aWindowGlobalParent) {
  NS_IF_ADDREF(*aWindowGlobalParent = mWindowGlobalParent);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetSourceWindowGlobal(
    mozilla::dom::WindowGlobalParent** aSourceWindowGlobal) {
  NS_IF_ADDREF(*aSourceWindowGlobal = mSourceWindowGlobal);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetTransferable(nsITransferable** aTransferable) {
  NS_IF_ADDREF(*aTransferable = mTransferable);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetDataTransfer(
    mozilla::dom::DataTransfer** aDataTransfer) {
  NS_IF_ADDREF(*aDataTransfer = mDataTransfer);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetTimeoutMultiplier(uint32_t* aTimeoutMultiplier) {
  NS_ENSURE_ARG_POINTER(aTimeoutMultiplier);
  *aTimeoutMultiplier = mTimeoutMultiplier;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::SetTimeoutMultiplier(uint32_t aTimeoutMultiplier) {
  mTimeoutMultiplier = aTimeoutMultiplier;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::GetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(
    bool* aAlwaysSubmitToAgent) {
  *aAlwaysSubmitToAgent = mTestOnlyAlwaysSubmitToAgent;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisRequest::SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(
    bool aAlwaysSubmitToAgent) {
  mTestOnlyAlwaysSubmitToAgent = aAlwaysSubmitToAgent;
  return NS_OK;
}

nsresult ContentAnalysis::CreateContentAnalysisClient(
    nsCString&& aPipePathName, nsString&& aClientSignatureSetting,
    bool aIsPerUser) {
  MOZ_ASSERT(!NS_IsMainThread());

  std::shared_ptr<content_analysis::sdk::Client> client;
  if (!IsShutDown()) {
    client.reset(content_analysis::sdk::Client::Create(
                     {aPipePathName.Data(), aIsPerUser})
                     .release());
    LOGD("Content analysis is %s", client ? "connected" : "not available");
  } else {
    LOGD("ContentAnalysis::IsShutDown is true");
  }

#ifdef XP_WIN
  if (client && !aClientSignatureSetting.IsEmpty()) {
    std::string agentPath = client->GetAgentInfo().binary_path;
    nsString agentWidePath = NS_ConvertUTF8toUTF16(agentPath);
    UniquePtr<wchar_t[]> orgName =
        mozilla::DllServices::Get()->GetBinaryOrgName(agentWidePath.Data());
    bool signatureMatches = false;
    if (orgName) {
      auto dependentOrgName = nsDependentString(orgName.get());
      LOGD("Content analysis client signed with organization name \"%S\"",
           dependentOrgName.getW());
      signatureMatches = aClientSignatureSetting.Equals(dependentOrgName);
    } else {
      LOGD("Content analysis client has no signature");
    }
    if (!signatureMatches) {
      LOGE(
          "Got mismatched content analysis client signature! All content "
          "analysis operations will fail.");
      NS_DispatchToMainThread(
          NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
            AssertIsOnMainThread();
            self->mCaClientPromise->Reject(NS_ERROR_INVALID_SIGNATURE,
                                           __func__);
            self->mCreatingClient = false;
          }));

      return NS_OK;
    }
  }
#endif  // XP_WIN
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, client = std::move(client)]() {
        AssertIsOnMainThread();
        // Note that if mCaClientPromise has been resolved or rejected
        // calling Resolve() or Reject() is a noop.
        if (client) {
          self->mHaveResolvedClientPromise = true;
          self->mCaClientPromise->Resolve(client, __func__);
        } else {
          self->mCaClientPromise->Reject(NS_ERROR_CONNECTION_REFUSED, __func__);
        }
        self->mCreatingClient = false;
      }));

  return NS_OK;
}

ContentAnalysisRequest::ContentAnalysisRequest(
    AnalysisType aAnalysisType, Reason aReason, nsString aString,
    bool aStringIsFilePath, nsCString aSha256Digest, nsCOMPtr<nsIURI> aUrl,
    OperationType aOperationType, dom::WindowGlobalParent* aWindowGlobalParent,
    dom::WindowGlobalParent* aSourceWindowGlobal, nsCString&& aUserActionId)
    : mAnalysisType(aAnalysisType),
      mReason(aReason),
      mUrl(std::move(aUrl)),
      mSha256Digest(std::move(aSha256Digest)),
      mUserActionId(std::move(aUserActionId)),
      mOperationTypeForDisplay(aOperationType),
      mWindowGlobalParent(aWindowGlobalParent),
      mSourceWindowGlobal(aSourceWindowGlobal) {
  MOZ_ASSERT(aAnalysisType != AnalysisType::ePrint,
             "Print should use other ContentAnalysisRequest constructor!");
  MOZ_ASSERT(aReason != nsIContentAnalysisRequest::Reason::ePrintPreviewPrint &&
             aReason != nsIContentAnalysisRequest::Reason::eSystemDialogPrint);
  if (aStringIsFilePath) {
    mFilePath = std::move(aString);
  } else {
    mTextContent = std::move(aString);
  }
  if (mOperationTypeForDisplay == OperationType::eCustomDisplayString) {
    MOZ_ASSERT(aStringIsFilePath);
    nsresult rv = GetFileDisplayName(mFilePath, mOperationDisplayString);
    if (NS_FAILED(rv)) {
      mOperationDisplayString = u"file";
    }
  }
}

ContentAnalysisRequest::ContentAnalysisRequest(
    AnalysisType aAnalysisType, Reason aReason, nsITransferable* aTransferable,
    dom::WindowGlobalParent* aWindowGlobalParent,
    dom::WindowGlobalParent* aSourceWindowGlobal)
    : mAnalysisType(aAnalysisType),
      mReason(aReason),
      mTransferable(aTransferable),
      mOperationTypeForDisplay(
          nsIContentAnalysisRequest::OperationType::eClipboard),
      mWindowGlobalParent(aWindowGlobalParent),
      mSourceWindowGlobal(aSourceWindowGlobal) {}

ContentAnalysisRequest::ContentAnalysisRequest(
    const nsTArray<uint8_t> aPrintData, nsCOMPtr<nsIURI> aUrl,
    nsString aPrinterName, Reason aReason,
    dom::WindowGlobalParent* aWindowGlobalParent)
    : mAnalysisType(AnalysisType::ePrint),
      mReason(aReason),
      mUrl(std::move(aUrl)),
      mPrinterName(std::move(aPrinterName)),
      mWindowGlobalParent(aWindowGlobalParent) {
#ifdef XP_WIN
  LARGE_INTEGER dataContentLength;
  dataContentLength.QuadPart = static_cast<LONGLONG>(aPrintData.Length());
  mPrintDataHandle = ::CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, dataContentLength.HighPart,
      dataContentLength.LowPart, nullptr);
  if (mPrintDataHandle) {
    mozilla::nt::AutoMappedView view(mPrintDataHandle, FILE_MAP_ALL_ACCESS);
    memcpy(view.as<uint8_t>(), aPrintData.Elements(), aPrintData.Length());
    mPrintDataSize = aPrintData.Length();
  }
#else
  MOZ_ASSERT_UNREACHABLE(
      "Content Analysis is not supported on non-Windows platforms");
#endif
  // We currently only use this constructor when printing.
  MOZ_ASSERT(aReason == nsIContentAnalysisRequest::Reason::ePrintPreviewPrint ||
             aReason == nsIContentAnalysisRequest::Reason::eSystemDialogPrint);
  mOperationTypeForDisplay = OperationType::eOperationPrint;
}

nsresult ContentAnalysisRequest::GetFileDigest(const nsAString& aFilePath,
                                               nsCString& aDigestString) {
  MOZ_DIAGNOSTIC_ASSERT(
      !NS_IsMainThread(),
      "ContentAnalysisRequest::GetFileDigest does file IO and should "
      "not run on the main thread");
  nsresult rv;
  mozilla::Digest digest;
  digest.Begin(SEC_OID_SHA256);
  PRFileDesc* fd = nullptr;
  nsCOMPtr<nsIFile> file;
  MOZ_TRY(NS_NewLocalFile(aFilePath, getter_AddRefs(file)));
  rv = file->OpenNSPRFileDesc(PR_RDONLY | nsIFile::OS_READAHEAD, 0, &fd);
  NS_ENSURE_SUCCESS(rv, rv);
  auto closeFile = MakeScopeExit([fd]() { PR_Close(fd); });
  constexpr uint32_t kBufferSize = 1024 * 1024;
  auto buffer = mozilla::MakeUnique<uint8_t[]>(kBufferSize);
  if (!buffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  PRInt32 bytesRead = PR_Read(fd, buffer.get(), kBufferSize);
  while (bytesRead != 0) {
    if (bytesRead == -1) {
      return NS_ErrorAccordingToNSPR();
    }
    digest.Update(mozilla::Span<const uint8_t>(buffer.get(), bytesRead));
    bytesRead = PR_Read(fd, buffer.get(), kBufferSize);
  }
  nsTArray<uint8_t> digestResults;
  rv = digest.End(digestResults);
  NS_ENSURE_SUCCESS(rv, rv);
  aDigestString = mozilla::ToHexString(digestResults);
  return NS_OK;
}

static nsresult ConvertToProtobuf(
    nsIClientDownloadResource* aIn,
    content_analysis::sdk::ClientDownloadRequest_Resource* aOut) {
  nsString url;
  nsresult rv = aIn->GetUrl(url);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_url(NS_ConvertUTF16toUTF8(url).get());

  uint32_t resourceType;
  rv = aIn->GetType(&resourceType);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_type(
      static_cast<content_analysis::sdk::ClientDownloadRequest_ResourceType>(
          resourceType));

  return NS_OK;
}

#if defined(DEBUG)
static bool IsRequestReadyForAgent(nsIContentAnalysisRequest* aRequest) {
  NS_ENSURE_TRUE(aRequest, false);

  // The windowGlobal is allowed to be null at this point in gtests (only).
  // The URL must be set in that case.  We check that below.
  RefPtr<dom::WindowGlobalParent> windowGlobal;
  NS_ENSURE_SUCCESS(
      aRequest->GetWindowGlobalParent(getter_AddRefs(windowGlobal)), false);

  // Any DataTransfer should have been expanded into individual requests.
  nsCOMPtr<dom::DataTransfer> dataTransfer;
  NS_ENSURE_SUCCESS(aRequest->GetDataTransfer(getter_AddRefs(dataTransfer)),
                    false);
  NS_ENSURE_TRUE(!dataTransfer, false);

  // Any nsITransferable should have been expanded into individual requests.
  nsCOMPtr<nsITransferable> transferable;
  NS_ENSURE_SUCCESS(aRequest->GetTransferable(getter_AddRefs(transferable)),
                    false);
  NS_ENSURE_TRUE(!transferable, false);

  nsCString userActionId;
  NS_ENSURE_SUCCESS(aRequest->GetUserActionId(userActionId), false);
  NS_ENSURE_TRUE(!userActionId.IsEmpty(), false);

  int64_t userActionRequestsCount;
  NS_ENSURE_SUCCESS(
      aRequest->GetUserActionRequestsCount(&userActionRequestsCount), false);
  NS_ENSURE_TRUE(userActionRequestsCount, false);

  nsCOMPtr<nsIURI> url;
  NS_ENSURE_SUCCESS(aRequest->GetUrl(getter_AddRefs(url)), false);
  if (!url) {
    // If no URL is given then we use the one for the window.
    NS_ENSURE_TRUE(windowGlobal, false);
    url = ContentAnalysis::GetURIForBrowsingContext(
        windowGlobal->Canonical()->GetBrowsingContext());
    NS_ENSURE_TRUE(url, false);
  }

  return true;
}
#endif  // defined(DEBUG)

static nsresult ConvertToProtobuf(
    nsIContentAnalysisRequest* aIn,
    content_analysis::sdk::ContentAnalysisRequest* aOut) {
  MOZ_ASSERT(IsRequestReadyForAgent(aIn));

  nsIContentAnalysisRequest::AnalysisType analysisType;
  nsresult rv = aIn->GetAnalysisType(&analysisType);
  NS_ENSURE_SUCCESS(rv, rv);
  auto connector =
      static_cast<content_analysis::sdk::AnalysisConnector>(analysisType);
  aOut->set_analysis_connector(connector);

  nsIContentAnalysisRequest::Reason reason;
  rv = aIn->GetReason(&reason);
  NS_ENSURE_SUCCESS(rv, rv);
  auto sdkReason =
      static_cast<content_analysis::sdk::ContentAnalysisRequest::Reason>(
          reason);
  aOut->set_reason(sdkReason);

  nsCString requestToken;
  rv = aIn->GetRequestToken(requestToken);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_request_token(requestToken.get(), requestToken.Length());
  nsCString userActionId;
  rv = aIn->GetUserActionId(userActionId);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_user_action_id(userActionId.get(), userActionId.Length());
  int64_t userActionRequestsCount;
  rv = aIn->GetUserActionRequestsCount(&userActionRequestsCount);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_user_action_requests_count(userActionRequestsCount);

  int32_t timeout = StaticPrefs::browser_contentanalysis_agent_timeout();
  // Non-positive timeout values indicate testing, and the test agent does not
  // care about this value.
  timeout = std::max(timeout, 1);
  uint32_t timeoutMultiplier;
  rv = aIn->GetTimeoutMultiplier(&timeoutMultiplier);
  NS_ENSURE_SUCCESS(rv, rv);
  timeoutMultiplier = std::max(timeoutMultiplier, static_cast<uint32_t>(1));
  auto checkedTimeout = CheckedInt64(time(nullptr)) +
                        timeout * userActionRequestsCount * timeoutMultiplier;
  if (!checkedTimeout.isValid()) {
    return NS_ERROR_FAILURE;
  }
  aOut->set_expires_at(checkedTimeout.value());

  const std::string tag = "dlp";  // TODO:
  *aOut->add_tags() = tag;

  auto* requestData = aOut->mutable_request_data();

  RefPtr<dom::WindowGlobalParent> windowGlobal;
  rv = aIn->GetWindowGlobalParent(getter_AddRefs(windowGlobal));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIURI> url;
  rv = aIn->GetUrl(getter_AddRefs(url));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!url) {
    // We already checked that this exists.
    MOZ_ASSERT(windowGlobal);
    // If no URL is given then we use the one for the window.
    url = ContentAnalysis::GetURIForBrowsingContext(
        windowGlobal->Canonical()->GetBrowsingContext());
    // We also already checked for this.
    MOZ_ASSERT(url);
  }
  nsCString urlString;
  rv = url->GetSpec(urlString);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!urlString.IsEmpty()) {
    requestData->set_url(urlString.get());
  }

  if (windowGlobal) {
    nsString title;
    windowGlobal->GetDocumentTitle(title);
    requestData->set_tab_title(NS_ConvertUTF16toUTF8(title).get());
  }

  nsString email;
  rv = aIn->GetEmail(email);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!email.IsEmpty()) {
    requestData->set_email(NS_ConvertUTF16toUTF8(email).get());
  }

  nsCString sha256Digest;
  rv = aIn->GetSha256Digest(sha256Digest);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!sha256Digest.IsEmpty()) {
    requestData->set_digest(sha256Digest.get());
  }

  if (analysisType == nsIContentAnalysisRequest::AnalysisType::ePrint) {
#if XP_WIN
    uint64_t printDataHandle;
    MOZ_TRY(aIn->GetPrintDataHandle(&printDataHandle));
    if (!printDataHandle) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    aOut->mutable_print_data()->set_handle(printDataHandle);

    uint64_t printDataSize;
    MOZ_TRY(aIn->GetPrintDataSize(&printDataSize));
    aOut->mutable_print_data()->set_size(printDataSize);

    nsString printerName;
    MOZ_TRY(aIn->GetPrinterName(printerName));
    requestData->mutable_print_metadata()->set_printer_name(
        NS_ConvertUTF16toUTF8(printerName).get());
#else
    return NS_ERROR_NOT_IMPLEMENTED;
#endif
  } else {
    nsString filePath;
    rv = aIn->GetFilePath(filePath);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!filePath.IsEmpty()) {
      std::string filePathStr = NS_ConvertUTF16toUTF8(filePath).get();
      aOut->set_file_path(filePathStr);
      auto filename = filePathStr.substr(filePathStr.find_last_of("/\\") + 1);
      if (!filename.empty()) {
        requestData->set_filename(filename);
      }
    } else {
      nsString textContent;
      rv = aIn->GetTextContent(textContent);
      NS_ENSURE_SUCCESS(rv, rv);
      MOZ_ASSERT(!textContent.IsEmpty());
      aOut->set_text_content(NS_ConvertUTF16toUTF8(textContent).get());
    }
  }

#ifdef XP_WIN
  ULONG userLen = 0;
  GetUserNameExW(NameSamCompatible, nullptr, &userLen);
  if (GetLastError() == ERROR_MORE_DATA && userLen > 0) {
    auto user = mozilla::MakeUnique<wchar_t[]>(userLen);
    if (GetUserNameExW(NameSamCompatible, user.get(), &userLen)) {
      auto* clientMetadata = aOut->mutable_client_metadata();
      auto* browser = clientMetadata->mutable_browser();
      browser->set_machine_user(NS_ConvertUTF16toUTF8(user.get()).get());
    }
  }
#endif

  nsTArray<RefPtr<nsIClientDownloadResource>> resources;
  rv = aIn->GetResources(resources);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!resources.IsEmpty()) {
    auto* pbClientDownloadRequest = requestData->mutable_csd();
    for (auto& nsResource : resources) {
      rv = ConvertToProtobuf(nsResource.get(),
                             pbClientDownloadRequest->add_resources());
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

namespace {
// We don't want this overload to be called for string parameters, so
// use std::enable_if
template <typename T>
typename std::enable_if_t<!std::is_same<std::string, std::decay_t<T>>::value,
                          void>
LogWithMaxLength(std::stringstream& ss, T value, size_t maxLength) {
  ss << value;
}

// 0 indicates no max length
template <typename T>
typename std::enable_if_t<std::is_same<std::string, std::decay_t<T>>::value,
                          void>
LogWithMaxLength(std::stringstream& ss, T value, size_t maxLength) {
  if (!maxLength || value.length() < maxLength) {
    ss << value;
  } else {
    ss << value.substr(0, maxLength) << " (truncated)";
  }
}
}  // namespace

static void LogRequest(
    const content_analysis::sdk::ContentAnalysisRequest* aPbRequest) {
  // We cannot use Protocol Buffer's DebugString() because we optimize for
  // lite runtime.
  if (!static_cast<LogModule*>(gContentAnalysisLog)
           ->ShouldLog(LogLevel::Debug)) {
    return;
  }

  std::stringstream ss;
  ss << "ContentAnalysisRequest:"
     << "\n";

#define ADD_FIELD(PBUF, NAME, FUNC)            \
  ss << "  " << (NAME) << ": ";                \
  if ((PBUF)->has_##FUNC()) {                  \
    LogWithMaxLength(ss, (PBUF)->FUNC(), 500); \
    ss << "\n";                                \
  } else                                       \
    ss << "<none>"                             \
       << "\n";

#define ADD_EXISTS(PBUF, NAME, FUNC) \
  ss << "  " << (NAME) << ": "       \
     << ((PBUF)->has_##FUNC() ? "<exists>" : "<none>") << "\n";

  ADD_FIELD(aPbRequest, "Expires", expires_at);
  ADD_FIELD(aPbRequest, "Analysis Type", analysis_connector);
  ADD_FIELD(aPbRequest, "Request Token", request_token);
  ADD_FIELD(aPbRequest, "User Action ID", user_action_id);
  ADD_FIELD(aPbRequest, "User Action Requests Count",
            user_action_requests_count);
  ADD_FIELD(aPbRequest, "File Path", file_path);
  ADD_FIELD(aPbRequest, "Text Content", text_content);
  // TODO: Tags
  ADD_EXISTS(aPbRequest, "Request Data Struct", request_data);
  const auto* requestData =
      aPbRequest->has_request_data() ? &aPbRequest->request_data() : nullptr;
  if (requestData) {
    ADD_FIELD(requestData, "  Url", url);
    ADD_FIELD(requestData, "  Email", email);
    ADD_FIELD(requestData, "  SHA-256 Digest", digest);
    ADD_FIELD(requestData, "  Filename", filename);
    ADD_EXISTS(requestData, "  Client Download Request struct", csd);
    const auto* csd = requestData->has_csd() ? &requestData->csd() : nullptr;
    if (csd) {
      uint32_t i = 0;
      for (const auto& resource : csd->resources()) {
        ss << "      Resource " << i << ":"
           << "\n";
        ADD_FIELD(&resource, "      Url", url);
        ADD_FIELD(&resource, "      Type", type);
        ++i;
      }
    }
  }
  ADD_EXISTS(aPbRequest, "Client Metadata Struct", client_metadata);
  const auto* clientMetadata = aPbRequest->has_client_metadata()
                                   ? &aPbRequest->client_metadata()
                                   : nullptr;
  if (clientMetadata) {
    ADD_EXISTS(clientMetadata, "  Browser Struct", browser);
    const auto* browser =
        clientMetadata->has_browser() ? &clientMetadata->browser() : nullptr;
    if (browser) {
      ADD_FIELD(browser, "    Machine User", machine_user);
    }
  }

#undef ADD_EXISTS
#undef ADD_FIELD

  LOGD("%s", ss.str().c_str());
}

ContentAnalysisResponse::ContentAnalysisResponse(
    content_analysis::sdk::ContentAnalysisResponse&& aResponse,
    const nsCString& aUserActionId)
    : mUserActionId(aUserActionId), mIsAgentResponse(true) {
  mAction = Action::eUnspecified;
  for (const auto& result : aResponse.results()) {
    if (!result.has_status() ||
        result.status() !=
            content_analysis::sdk::ContentAnalysisResponse::Result::SUCCESS) {
      mAction = Action::eUnspecified;
      return;
    }
    // The action values increase with severity, so the max is the most severe.
    for (const auto& rule : result.triggered_rules()) {
      mAction =
          static_cast<Action>(std::max(static_cast<uint32_t>(mAction),
                                       static_cast<uint32_t>(rule.action())));
    }
  }

  // If no rules blocked then we should allow.
  if (mAction == Action::eUnspecified) {
    mAction = Action::eAllow;
  }

  const auto& requestToken = aResponse.request_token();
  mRequestToken.Assign(requestToken.data(), requestToken.size());
}

ContentAnalysisResponse::ContentAnalysisResponse(
    Action aAction, const nsACString& aRequestToken,
    const nsACString& aUserActionId)
    : mAction(aAction),
      mRequestToken(aRequestToken),
      mUserActionId(aUserActionId) {
  MOZ_ASSERT(mAction != Action::eUnspecified);
}

/* static */
already_AddRefed<ContentAnalysisResponse> ContentAnalysisResponse::FromProtobuf(
    content_analysis::sdk::ContentAnalysisResponse&& aResponse,
    const nsCString& aUserActionId) {
  auto ret = RefPtr<ContentAnalysisResponse>(
      new ContentAnalysisResponse(std::move(aResponse), aUserActionId));

  if (ret->mAction == Action::eUnspecified) {
    return nullptr;
  }

  return ret.forget();
}

NS_IMETHODIMP
ContentAnalysisResponse::GetRequestToken(nsACString& aRequestToken) {
  aRequestToken = mRequestToken;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisResponse::GetUserActionId(nsACString& aUserActionId) {
  aUserActionId = mUserActionId;
  return NS_OK;
}

static void LogResponse(
    content_analysis::sdk::ContentAnalysisResponse* aPbResponse) {
  if (!static_cast<LogModule*>(gContentAnalysisLog)
           ->ShouldLog(LogLevel::Debug)) {
    return;
  }

  std::stringstream ss;
  ss << "ContentAnalysisResponse:"
     << "\n";

#define ADD_FIELD(PBUF, NAME, FUNC) \
  ss << "  " << (NAME) << ": ";     \
  if ((PBUF)->has_##FUNC())         \
    ss << (PBUF)->FUNC() << "\n";   \
  else                              \
    ss << "<none>"                  \
       << "\n";

  ADD_FIELD(aPbResponse, "Request Token", request_token);
  uint32_t i = 0;
  for (const auto& result : aPbResponse->results()) {
    ss << "  Result " << i << ":"
       << "\n";
    ADD_FIELD(&result, "    Status", status);
    uint32_t j = 0;
    for (const auto& rule : result.triggered_rules()) {
      ss << "    Rule " << j << ":"
         << "\n";
      ADD_FIELD(&rule, "    action", action);
      ++j;
    }
    ++i;
  }

#undef ADD_FIELD

  LOGD("%s", ss.str().c_str());
}

static nsresult ConvertToProtobuf(
    nsIContentAnalysisAcknowledgement* aIn, const nsACString& aRequestToken,
    content_analysis::sdk::ContentAnalysisAcknowledgement* aOut) {
  aOut->set_request_token(aRequestToken.Data(), aRequestToken.Length());

  nsIContentAnalysisAcknowledgement::Result result;
  nsresult rv = aIn->GetResult(&result);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_status(
      static_cast<content_analysis::sdk::ContentAnalysisAcknowledgement_Status>(
          result));

  nsIContentAnalysisAcknowledgement::FinalAction finalAction;
  rv = aIn->GetFinalAction(&finalAction);
  NS_ENSURE_SUCCESS(rv, rv);
  aOut->set_final_action(
      static_cast<
          content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction>(
          finalAction));

  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisResponse::GetAction(Action* aAction) {
  *aAction = mAction;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisResponse::GetCancelError(CancelError* aCancelError) {
  *aCancelError = mCancelError;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisResponse::GetIsCachedResponse(bool* aIsCachedResponse) {
  *aIsCachedResponse = mIsCachedResponse;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisResponse::GetIsAgentResponse(bool* aIsAgentResponse) {
  *aIsAgentResponse = mIsAgentResponse;
  return NS_OK;
}

static void LogAcknowledgement(
    content_analysis::sdk::ContentAnalysisAcknowledgement* aPbAck) {
  if (!static_cast<LogModule*>(gContentAnalysisLog)
           ->ShouldLog(LogLevel::Debug)) {
    return;
  }

  std::stringstream ss;
  ss << "ContentAnalysisAcknowledgement:"
     << "\n";

#define ADD_FIELD(PBUF, NAME, FUNC) \
  ss << "  " << (NAME) << ": ";     \
  if ((PBUF)->has_##FUNC())         \
    ss << (PBUF)->FUNC() << "\n";   \
  else                              \
    ss << "<none>"                  \
       << "\n";

  ADD_FIELD(aPbAck, "Request Token", request_token);
  ADD_FIELD(aPbAck, "Status", status);
  ADD_FIELD(aPbAck, "Final Action", final_action);

#undef ADD_FIELD

  LOGD("%s", ss.str().c_str());
}

void ContentAnalysisResponse::SetOwner(ContentAnalysis* aOwner) {
  mOwner = std::move(aOwner);
}

void ContentAnalysisResponse::SetCancelError(CancelError aCancelError) {
  mCancelError = aCancelError;
}

void ContentAnalysisResponse::ResolveWarnAction(bool aAllowContent) {
  MOZ_ASSERT(mAction == Action::eWarn);
  mAction = aAllowContent ? Action::eAllow : Action::eBlock;
}

ContentAnalysisAcknowledgement::ContentAnalysisAcknowledgement(
    Result aResult, FinalAction aFinalAction)
    : mResult(aResult), mFinalAction(aFinalAction) {}

NS_IMETHODIMP
ContentAnalysisAcknowledgement::GetResult(Result* aResult) {
  *aResult = mResult;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysisAcknowledgement::GetFinalAction(FinalAction* aFinalAction) {
  *aFinalAction = mFinalAction;
  return NS_OK;
}

namespace {
static bool ShouldAllowAction(
    nsIContentAnalysisResponse::Action aResponseCode) {
  return aResponseCode == nsIContentAnalysisResponse::Action::eAllow ||
         aResponseCode == nsIContentAnalysisResponse::Action::eReportOnly ||
         aResponseCode == nsIContentAnalysisResponse::Action::eWarn;
}

static DefaultResult GetDefaultResultFromPref(bool isTimeout) {
  uint32_t value = isTimeout
                       ? StaticPrefs::browser_contentanalysis_timeout_result()
                       : StaticPrefs::browser_contentanalysis_default_result();
  if (value > static_cast<uint32_t>(DefaultResult::eLastValue)) {
    LOGE(
        "Invalid value for browser.contentanalysis.%s pref "
        "value",
        isTimeout ? "default_timeout_result" : "default_result");
    return DefaultResult::eBlock;
  }
  return static_cast<DefaultResult>(value);
}
}  // namespace

NS_IMETHODIMP ContentAnalysisResponse::GetShouldAllowContent(
    bool* aShouldAllowContent) {
  *aShouldAllowContent = ShouldAllowAction(mAction);
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysisActionResult::GetShouldAllowContent(
    bool* aShouldAllowContent) {
  *aShouldAllowContent = ShouldAllowAction(mValue);
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysisNoResult::GetShouldAllowContent(
    bool* aShouldAllowContent) {
  // Make sure to use the non-timeout pref here, because timeouts won't
  // go through this code path.
  if (GetDefaultResultFromPref(/* isTimeout */ false) ==
      DefaultResult::eAllow) {
    *aShouldAllowContent =
        mValue != NoContentAnalysisResult::DENY_DUE_TO_CANCELED;
  } else {
    // Note that we allow content if we're unable to get it (for example, if
    // there's clipboard content that is not text or file)
    *aShouldAllowContent =
        mValue ==
            NoContentAnalysisResult::ALLOW_DUE_TO_CONTENT_ANALYSIS_NOT_ACTIVE ||
        mValue == NoContentAnalysisResult::
                      ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS ||
        mValue == NoContentAnalysisResult::ALLOW_DUE_TO_SAME_TAB_SOURCE ||
        mValue == NoContentAnalysisResult::ALLOW_DUE_TO_COULD_NOT_GET_DATA;
  }
  return NS_OK;
}

void ContentAnalysis::EnsureParsedUrlFilters() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mParsedUrlLists) {
    return;
  }

  mParsedUrlLists = true;
  nsAutoCString allowList;
  MOZ_ALWAYS_SUCCEEDS(Preferences::GetCString(kAllowUrlPref, allowList));
  for (const nsACString& regexSubstr : allowList.Split(u' ')) {
    if (!regexSubstr.IsEmpty()) {
      auto flatStr = PromiseFlatCString(regexSubstr);
      const char* regex = flatStr.get();
      LOGD("CA will allow URLs that match %s", regex);
      mAllowUrlList.push_back(std::regex(regex));
    }
  }

  nsAutoCString denyList;
  MOZ_ALWAYS_SUCCEEDS(Preferences::GetCString(kDenyUrlPref, denyList));
  for (const nsACString& regexSubstr : denyList.Split(u' ')) {
    if (!regexSubstr.IsEmpty()) {
      auto flatStr = PromiseFlatCString(regexSubstr);
      const char* regex = flatStr.get();
      LOGD("CA will block URLs that match %s", regex);
      mDenyUrlList.push_back(std::regex(regex));
    }
  }
}

ContentAnalysis::UrlFilterResult ContentAnalysis::FilterByUrlLists(
    nsIContentAnalysisRequest* aRequest, nsIURI* aUri) {
  EnsureParsedUrlFilters();

  nsCString urlString;
  nsresult rv = aUri->GetSpec(urlString);
  NS_ENSURE_SUCCESS(rv, UrlFilterResult::eDeny);
  MOZ_ASSERT(!urlString.IsEmpty());
  LOGD("Content Analysis checking URL against URL filter list | URL: %s",
       urlString.get());

  std::string url = urlString.BeginReading();
  size_t count = 0;
  for (const auto& denyFilter : mDenyUrlList) {
    if (std::regex_match(url, denyFilter)) {
      LOGD("Denying CA request : Deny filter %zu matched url %s", count,
           url.c_str());
      return UrlFilterResult::eDeny;
    }
    ++count;
  }

  count = 0;
  UrlFilterResult result = UrlFilterResult::eCheck;
  for (const auto& allowFilter : mAllowUrlList) {
    if (std::regex_match(url, allowFilter)) {
      LOGD("CA request : Allow filter %zu matched %s", count, url.c_str());
      result = UrlFilterResult::eAllow;
      break;
    }
    ++count;
  }

  // The rest only applies to download resources.
  nsIContentAnalysisRequest::AnalysisType analysisType;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetAnalysisType(&analysisType));
  if (analysisType != ContentAnalysisRequest::AnalysisType::eFileDownloaded) {
    MOZ_ASSERT(result == UrlFilterResult::eCheck ||
               result == UrlFilterResult::eAllow);
    LOGD("CA request filter result: %s",
         result == UrlFilterResult::eCheck ? "check" : "allow");
    return result;
  }

  nsTArray<RefPtr<nsIClientDownloadResource>> resources;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetResources(resources));
  for (size_t resourceIdx = 0; resourceIdx < resources.Length();
       /* noop */) {
    auto& resource = resources[resourceIdx];
    nsAutoString nsUrl;
    MOZ_ALWAYS_SUCCEEDS(resource->GetUrl(nsUrl));
    std::string url = NS_ConvertUTF16toUTF8(nsUrl).get();
    count = 0;
    for (auto& denyFilter : mDenyUrlList) {
      if (std::regex_match(url, denyFilter)) {
        LOGD(
            "Denying CA request : Deny filter %zu matched download resource "
            "at url %s",
            count, url.c_str());
        return UrlFilterResult::eDeny;
      }
      ++count;
    }

    count = 0;
    bool removed = false;
    for (auto& allowFilter : mAllowUrlList) {
      if (std::regex_match(url, allowFilter)) {
        LOGD(
            "CA request : Allow filter %zu matched download resource "
            "at url %s",
            count, url.c_str());
        resources.RemoveElementAt(resourceIdx);
        removed = true;
        break;
      }
      ++count;
    }
    if (!removed) {
      ++resourceIdx;
    }
  }

  // Check unless all were allowed.
  return resources.Length() ? UrlFilterResult::eCheck : UrlFilterResult::eAllow;
}

NS_IMPL_ISUPPORTS(ContentAnalysisRequest, nsIContentAnalysisRequest);
NS_IMPL_ISUPPORTS(ContentAnalysisResponse, nsIContentAnalysisResponse,
                  nsIContentAnalysisResult);
NS_IMPL_ISUPPORTS(ContentAnalysisActionResult, nsIContentAnalysisResult);
NS_IMPL_ISUPPORTS(ContentAnalysisNoResult, nsIContentAnalysisResult);

NS_IMPL_ISUPPORTS(ContentAnalysisAcknowledgement,
                  nsIContentAnalysisAcknowledgement);
NS_IMPL_ISUPPORTS(ContentAnalysisCallback, nsIContentAnalysisCallback);
NS_IMPL_ISUPPORTS(ContentAnalysisDiagnosticInfo,
                  nsIContentAnalysisDiagnosticInfo);
NS_IMPL_ISUPPORTS(ContentAnalysis, nsIContentAnalysis, nsIObserver,
                  ContentAnalysis);

ContentAnalysis::ContentAnalysis()
    : mThreadPool(new nsThreadPool()),
      mRequestTokenToUserActionIdMap(
          "ContentAnalysis::mRequestTokenToUserActionIdMap"),
      mCaClientPromise(
          new ClientPromise::Private("ContentAnalysis::ContentAnalysis")),
      mSetByEnterprise(false) {
  // Limit one per process
  [[maybe_unused]] static bool sCreated = false;
  MOZ_ASSERT(!sCreated);
  sCreated = true;

  MOZ_ALWAYS_SUCCEEDS(
      mThreadPool->SetName(nsAutoCString("ContentAnalysisAgentIO")));

  unsigned long threadLimit =
      std::min(static_cast<unsigned long>(
                   StaticPrefs::browser_contentanalysis_max_connections()),
               kMaxContentAnalysisAgentThreads);
  MOZ_ALWAYS_SUCCEEDS(mThreadPool->SetThreadLimit(threadLimit));

  // Update thread limit if the pref changes, for testing (otherwise it is
  // locked).  We cannot use RegisterCallbackAndCall since the callback needs
  // to get the service that we are currently constructing.
  Preferences::RegisterCallback(
      [](const char* aPref, void*) {
        auto self = GetContentAnalysisFromService();
        if (!self) {
          return;
        }
        unsigned long threadLimit = std::min(
            static_cast<unsigned long>(
                StaticPrefs::browser_contentanalysis_max_connections()),
            kMaxContentAnalysisAgentThreads);
        MOZ_ALWAYS_SUCCEEDS(self->mThreadPool->SetThreadLimit(threadLimit));
      },
      nsDependentCString(
          StaticPrefs::GetPrefName_browser_contentanalysis_max_connections()));

  MOZ_ALWAYS_SUCCEEDS(
      mThreadPool->SetIdleThreadLimit(kMaxIdleContentAnalysisAgentThreads));
  MOZ_ALWAYS_SUCCEEDS(mThreadPool->SetIdleThreadGraceTimeout(
      kIdleContentAnalysisAgentTimeoutMs));
  MOZ_ALWAYS_SUCCEEDS(mThreadPool->SetIdleThreadMaximumTimeout(
      kMaxIdleContentAnalysisAgentTimeoutMs));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  obsServ->AddObserver(this, "xpcom-shutdown-threads", false);
}

ContentAnalysis::~ContentAnalysis() {
  LOGD("ContentAnalysis::~ContentAnalysis");
  AssertIsOnMainThread();
  MOZ_ASSERT(mUserActionMap.IsEmpty());
  MOZ_ASSERT(!mThreadPool);
  DebugOnly lock = mIsShutDown.Lock();
  MOZ_ASSERT(*lock.inspect());
}

NS_IMETHODIMP
ContentAnalysis::Observe(nsISupports* subject, const char* topic,
                         const char16_t* data) {
  AssertIsOnMainThread();
  MOZ_ASSERT(nsCString("xpcom-shutdown-threads") == topic);
  LOGD("Content Analysis received xpcom-shutdown-threads");
  Close();
  return NS_OK;
}

void ContentAnalysis::Close() {
  AssertIsOnMainThread();
  {
    // Make sure that we don't try to reconnect to the agent.
    auto lock = mIsShutDown.Lock();
    if (*lock) {
      // was previously called
      return;
    }
    *lock = true;
  }

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  obsServ->RemoveObserver(this, "xpcom-shutdown-threads");

  // Reject the promise to avoid assertions when it gets destroyed
  // Note that if the promise has already been resolved or rejected this is a
  // noop
  mCaClientPromise->Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);

  // In case the promise _was_ resolved before, create a new one and reject
  // that.
  mCaClientPromise =
      new ClientPromise::Private("ContentAnalysis:ShutdownReject");
  mCaClientPromise->Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);

  // The userActionMap must be cleared before the object is destroyed.
  mUserActionMap.Clear();

  mThreadPool->ShutdownWithTimeout(kShutdownThreadpoolTimeoutMs);
  mThreadPool = nullptr;
  LOGD("Content Analysis service is closed");
}

bool ContentAnalysis::IsShutDown() {
  auto lock = mIsShutDown.ConstLock();
  return *lock;
}

nsresult ContentAnalysis::CreateClientIfNecessary(
    bool aForceCreate /* = false */) {
  AssertIsOnMainThread();

  if (IsShutDown()) {
    return NS_OK;
  }

  nsCString pipePathName;
  nsresult rv = Preferences::GetCString(kPipePathNamePref, pipePathName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mCaClientPromise->Reject(rv, __func__);
    return rv;
  }
  if (mHaveResolvedClientPromise && !aForceCreate) {
    return NS_OK;
  }
  // mCreatingClient is only accessed on the main thread
  if (mCreatingClient) {
    return NS_OK;
  }
  mCreatingClient = true;
  mHaveResolvedClientPromise = false;
  // Reject the promise to avoid assertions when it gets destroyed
  // Note that if the promise has already been resolved or rejected this is a
  // noop
  mCaClientPromise->Reject(NS_ERROR_FAILURE, __func__);
  mCaClientPromise =
      new ClientPromise::Private("ContentAnalysis::ContentAnalysis");

  bool isPerUser = StaticPrefs::browser_contentanalysis_is_per_user();
  nsString clientSignature;
  // It's OK if this fails, we will default to the empty string
  Preferences::GetString(kClientSignature, clientSignature);
  LOGD("Dispatching background task to create Content Analysis client");
  rv = NS_DispatchBackgroundTask(NS_NewCancelableRunnableFunction(
      "ContentAnalysis::CreateContentAnalysisClient",
      [owner = RefPtr{this}, pipePathName = std::move(pipePathName),
       clientSignature = std::move(clientSignature), isPerUser]() mutable {
        owner->CreateContentAnalysisClient(
            std::move(pipePathName), std::move(clientSignature), isPerUser);
      }));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mCaClientPromise->Reject(rv, __func__);
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::GetIsActive(bool* aIsActive) {
  *aIsActive = false;
  if (!StaticPrefs::browser_contentanalysis_enabled()) {
    LOGD("Local DLP Content Analysis is not enabled");
    return NS_OK;
  }
  // Accessing mSetByEnterprise and non-static prefs
  // so need to be on the main thread
  AssertIsOnMainThread();
  // gAllowContentAnalysisArgPresent is only set in the parent process
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!gAllowContentAnalysisArgPresent && !mSetByEnterprise) {
    LOGE(
        "The content analysis pref is enabled but not by an enterprise "
        "policy and -allow-content-analysis was not present on the "
        "command-line.  Content Analysis will not be active.");
    return NS_OK;
  }

  *aIsActive = true;
  LOGD("Local DLP Content Analysis is enabled");
  return CreateClientIfNecessary();
}

NS_IMETHODIMP
ContentAnalysis::GetMightBeActive(bool* aMightBeActive) {
  *aMightBeActive = nsIContentAnalysis::MightBeActive();
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::GetIsSetByEnterprisePolicy(bool* aSetByEnterprise) {
  *aSetByEnterprise = mSetByEnterprise;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::SetIsSetByEnterprisePolicy(bool aSetByEnterprise) {
  mSetByEnterprise = aSetByEnterprise;
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::TestOnlySetCACmdLineArg(bool aVal) {
#ifdef ENABLE_TESTS
  gAllowContentAnalysisArgPresent = aVal;
  return NS_OK;
#else
  LOGE("ContentAnalysis::TestOnlySetCACmdLineArg is test-only");
  return NS_ERROR_UNEXPECTED;
#endif
}

Maybe<nsIContentAnalysisResponse::Action>
ContentAnalysis::CachedClipboardResponse::GetCachedResponse(
    nsIURI* aURI, int32_t aClipboardSequenceNumber) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Expecting main thread access only to avoid synchronization");
  if (Some(aClipboardSequenceNumber) != mClipboardSequenceNumber) {
    LOGD("CachedClipboardResponse seqno does not match cached value");
    return Nothing();
  }
  for (const auto& entry : mData) {
    bool uriEquals = false;
    // URI will not be set for some chrome contexts
    if ((!aURI && !entry.first) ||
        (aURI && NS_SUCCEEDED(aURI->Equals(entry.first, &uriEquals)) &&
         uriEquals)) {
      LOGD("CachedClipboardResponse match");
      return Some(entry.second);
    }
  }
  LOGD("CachedClipboardResponse did not match any cached URI");
  return Nothing();
}

void ContentAnalysis::CachedClipboardResponse::SetCachedResponse(
    const nsCOMPtr<nsIURI>& aURI, int32_t aClipboardSequenceNumber,
    nsIContentAnalysisResponse::Action aAction) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Expecting main thread access only to avoid synchronization");
  if (mClipboardSequenceNumber != Some(aClipboardSequenceNumber)) {
    LOGD("CachedClipboardResponse caching new clipboard seqno");
    mData.Clear();
    mClipboardSequenceNumber = Some(aClipboardSequenceNumber);
  } else {
    LOGD(
        "CachedClipboardResponse caching new URI for existing cached clipboard "
        "seqno");
  }

  // Update the cached action for this URI if it already exists in the cache,
  // otherwise add a new cache entry for this URI.
  for (auto& entry : mData) {
    bool uriEquals = false;
    // URI will not be set for some chrome contexts
    if ((!aURI && !entry.first) ||
        (aURI && NS_SUCCEEDED(aURI->Equals(entry.first, &uriEquals)) &&
         uriEquals)) {
      entry.second = aAction;
      return;
    }
  }

  mData.AppendElement(std::make_pair(aURI, aAction));
}

NS_IMETHODIMP ContentAnalysis::SetCachedResponse(
    nsIURI* aURI, int32_t aClipboardSequenceNumber,
    nsIContentAnalysisResponse::Action aAction) {
  mCachedClipboardResponse.SetCachedResponse(aURI, aClipboardSequenceNumber,
                                             aAction);
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysis::GetCachedResponse(
    nsIURI* aURI, int32_t aClipboardSequenceNumber,
    nsIContentAnalysisResponse::Action* aAction, bool* aIsValid) {
  auto action = mCachedClipboardResponse.GetCachedResponse(
      aURI, aClipboardSequenceNumber);
  *aIsValid = action.isSome();
  if (action.isSome()) {
    *aAction = *action;
  }
  return NS_OK;
}

void ContentAnalysis::CancelWithError(nsCString&& aUserActionId,
                                      nsresult aResult) {
  MOZ_ASSERT(!aUserActionId.IsEmpty());
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewCancelableRunnableFunction(
        "CancelWithError",
        [aUserActionId = std::move(aUserActionId), aResult]() mutable {
          auto self = GetContentAnalysisFromService();
          if (!self) {
            // May be shutting down
            return;
          }
          self->CancelWithError(std::move(aUserActionId), aResult);
        }));
    return;
  }
  AssertIsOnMainThread();
  LOGD("CancelWithError | aUserActionId: %s | aResult: %s\n",
       aUserActionId.get(), SafeGetStaticErrorName(aResult));

  AutoTArray<nsCString, 1> tokens;
  RefPtr<nsIContentAnalysisCallback> callback;
  bool autoAcknowledge;
  if (auto maybeUserActionData = mUserActionMap.Lookup(aUserActionId)) {
    // We are cancelling all existing requests for this user action.
    tokens =
        ToTArray<AutoTArray<nsCString, 1>>(maybeUserActionData->mRequestTokens);
    callback = maybeUserActionData->mCallback;
    autoAcknowledge = maybeUserActionData->mAutoAcknowledge;
  } else {
    LOGD(
        "ContentAnalysis::CancelWithError user action not found -- already "
        "responded | userActionId: %s",
        aUserActionId.get());
    auto userActionIdToCanceledResponseMap =
        mUserActionIdToCanceledResponseMap.Lock();
    if (auto entry = userActionIdToCanceledResponseMap->Lookup(aUserActionId)) {
      entry->mNumExpectedResponses--;
      if (!entry->mNumExpectedResponses) {
        entry.Remove();
      }
    }
    return;
  }

  if (tokens.IsEmpty()) {
    // There are two cases where this happens.
    // (1) This Cancel was for the last request in the user action.  We don't
    // have any other tokens to cancel and we have nothing to tell the agent to
    // cancel.  Note that this case is only possible if this cancel call is
    // due to a negative verdict from the agent, and that handler will remove
    // our userActionId from mUserActionMap, so there is nothing left to do.
    // (2) We canceled before the final request list was formed.  We still
    // need to call the callback -- we do this when the final request list
    // is complete.
    MOZ_ASSERT(
        aResult == NS_ERROR_ABORT,
        "Token list can only be empty when canceling all remaining requests");
    LOGD(
        "ContentAnalysis::CancelWithError user action not found -- either was "
        "after last response or before first request was submitted | "
        "userActionId: %s",
        aUserActionId.get());
    RemoveFromUserActionMap(std::move(aUserActionId));
    return;
  }

  LOGD(
      "ContentAnalysis::CancelWithError cancelling user action: %s with error: "
      "%s",
      aUserActionId.get(), SafeGetStaticErrorName(aResult));

  bool isShutdown = aResult == NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  bool isCancel = aResult == NS_ERROR_ABORT;
  bool isTimeout = aResult == NS_ERROR_DOM_TIMEOUT_ERR;

  // Propagate shutdown error to the callback as that same error.  All other
  // cases use the default response, except user cancel, which always uses
  // cancel response.
  // Note that, for shutdown errors, if we returned a default warn response
  // (as opposed to some other value -- we currently return the error),
  // the result would be a shutdown hang while the dialog waited for a user
  // response (bug 1912245).
  nsIContentAnalysisResponse::Action action =
      nsIContentAnalysisResponse::Action::eCanceled;
  if (!isShutdown && !isCancel) {
    DefaultResult defaultResponse = GetDefaultResultFromPref(isTimeout);
    switch (defaultResponse) {
      case DefaultResult::eAllow:
        action = nsIContentAnalysisResponse::Action::eAllow;
        break;
      case DefaultResult::eWarn:
        action = nsIContentAnalysisResponse::Action::eWarn;
        break;
      case DefaultResult::eBlock:
        // eBlock would show a block dialog but eCanceled will not.
        action = nsIContentAnalysisResponse::Action::eCanceled;
        break;
      default:
        MOZ_ASSERT(false);
        action = nsIContentAnalysisResponse::Action::eCanceled;
    }
  }

  nsIContentAnalysisResponse::CancelError cancelError;
  switch (aResult) {
    case NS_ERROR_NOT_AVAILABLE:
    case NS_ERROR_CONNECTION_REFUSED:
      cancelError = nsIContentAnalysisResponse::CancelError::eNoAgent;
      break;
    case NS_ERROR_INVALID_SIGNATURE:
      cancelError =
          nsIContentAnalysisResponse::CancelError::eInvalidAgentSignature;
      break;
    case NS_ERROR_WONT_HANDLE_CONTENT:
    case NS_ERROR_ABORT:
      cancelError = nsIContentAnalysisResponse::CancelError::
          eOtherRequestInGroupCancelled;
      break;
    case NS_ERROR_ILLEGAL_DURING_SHUTDOWN:
      cancelError = nsIContentAnalysisResponse::CancelError::eShutdown;
      break;
    case NS_ERROR_DOM_TIMEOUT_ERR:
      cancelError = nsIContentAnalysisResponse::CancelError::eTimeout;
      break;
    default:
      cancelError = nsIContentAnalysisResponse::CancelError::eErrorOther;
      break;
  }

  bool calledError = false;
  for (const auto& token : tokens) {
    auto response =
        MakeRefPtr<ContentAnalysisResponse>(action, token, aUserActionId);
    response->SetCancelError(cancelError);
    // Alert the UI and (if action is not warn) the callback.  We aren't
    // handling an actual response so we have nothing to acknowledge.
    NotifyResponseObservers(response, nsCString(aUserActionId), autoAcknowledge,
                            isTimeout);
    if (action != nsIContentAnalysisResponse::Action::eWarn) {
      if (callback) {
        if (isShutdown) {
          // One Error response call is sufficient to complete the
          // MultipartRequestCallback.
          if (!calledError) {
            callback->Error(aResult);
            calledError = true;
          }
        } else {
          callback->ContentResult(response);
        }
      }
    }
  }

  if (action == nsIContentAnalysisResponse::Action::eWarn) {
    // A default warn response will handle the rest after the user chooses
    // a result.
    return;
  }

  RemoveFromUserActionMap(nsCString(aUserActionId));

  // NS_ERROR_WONT_HANDLE_CONTENT and NS_ERROR_CONNECTION_REFUSED mean the
  // request was never sent to the agent, so we don't cancel it.
  if (aResult != NS_ERROR_WONT_HANDLE_CONTENT &&
      aResult != NS_ERROR_CONNECTION_REFUSED) {
    auto userActionIdToCanceledResponseMap =
        mUserActionIdToCanceledResponseMap.Lock();
    userActionIdToCanceledResponseMap->InsertOrUpdate(
        aUserActionId,
        CanceledResponse{ConvertResult(action), tokens.Length()});
  } else {
    LOGD("CancelWithError cancelling unsubmitted request with error %s.",
         SafeGetStaticErrorName(aResult));
    return;
  }

  // Re-get service in case the registered service is mocked for testing.
  nsCOMPtr<nsIContentAnalysis> contentAnalysis =
      mozilla::components::nsIContentAnalysis::Service();
  if (contentAnalysis) {
    contentAnalysis->SendCancelToAgent(aUserActionId);
  } else {
    LOGD(
        "Content Analysis Service has been shut down.  Cancel will not be "
        "sent to agent.");
  }
}

NS_IMETHODIMP ContentAnalysis::SendCancelToAgent(
    const nsACString& aUserActionId) {
  CallClientWithRetry<std::nullptr_t>(
      __func__,
      [userActionId = nsCString(aUserActionId)](
          std::shared_ptr<content_analysis::sdk::Client> client) mutable
      -> Result<std::nullptr_t, nsresult> {
        MOZ_ASSERT(!NS_IsMainThread());
        auto owner = GetContentAnalysisFromService();
        if (!owner) {
          // May be shutting down
          return nullptr;
        }
        content_analysis::sdk::ContentAnalysisCancelRequests cancelRequest;
        cancelRequest.set_user_action_id(userActionId.get(),
                                         userActionId.Length());
        int err = client->CancelRequests(cancelRequest);
        if (err != 0) {
          LOGE(
              "SendCancelToAgent got error %d for "
              "user_action_id: %s",
              err, userActionId.get());
          return Err(NS_ERROR_FAILURE);
        }
        LOGD(
            "SendCancelToAgent successfully sent CancelRequests to "
            "agent for user_action_id: %s",
            userActionId.get());
        return nullptr;
      })
      ->Then(
          GetCurrentSerialEventTarget(), __func__, []() { /* nothing to do */ },
          [](nsresult rv) {
            LOGE("SendCancelToAgent failed to get the client with error %s",
                 SafeGetStaticErrorName(rv));
          });
  return NS_OK;
}

RefPtr<ContentAnalysis> ContentAnalysis::GetContentAnalysisFromService() {
  RefPtr<ContentAnalysis> contentAnalysisService =
      mozilla::components::nsIContentAnalysis::Service();
  return contentAnalysisService;
}

static bool ShouldCheckReason(nsIContentAnalysisRequest::Reason aReason) {
  switch (aReason) {
    case nsIContentAnalysisRequest::Reason::eFilePickerDialog:
      return mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_file_upload_enabled();
    case nsIContentAnalysisRequest::Reason::eClipboardPaste:
      return mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_clipboard_enabled();
    case nsIContentAnalysisRequest::Reason::ePrintPreviewPrint:
    case nsIContentAnalysisRequest::Reason::eSystemDialogPrint:
      return mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_print_enabled();
    case nsIContentAnalysisRequest::Reason::eDragAndDrop:
      return mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_drag_and_drop_enabled();
    default:
      MOZ_ASSERT_UNREACHABLE("Unrecognized content analysis request reason");
      return false;  // don't try to check it
  }
}

template <typename T, typename U>
RefPtr<MozPromise<T, nsresult, true>> ContentAnalysis::CallClientWithRetry(
    StaticString aMethodName, U&& aClientCallFunc) {
  AssertIsOnMainThread();
  auto promise =
      MakeRefPtr<typename MozPromise<T, nsresult, true>::Private>(aMethodName);
  auto reconnectAndRetry = [aClientCallFunc, aMethodName,
                            promise](nsresult rv) {
    AssertIsOnMainThread();
    LOGD("Failed to get client - trying to reconnect: %s",
         SafeGetStaticErrorName(rv));
    RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
    if (!owner) {
      // May be shutting down
      promise->Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, aMethodName);
      return;
    }
    // try to reconnect
    rv = owner->CreateClientIfNecessary(/* aForceCreate */ true);
    if (NS_FAILED(rv)) {
      LOGD("Failed to reconnect to client: %s", SafeGetStaticErrorName(rv));
      owner->mCaClientPromise->Reject(rv, aMethodName);
      promise->Reject(rv, aMethodName);
      return;
    }
    owner->mCaClientPromise->Then(
        GetCurrentSerialEventTarget(), aMethodName,
        [aMethodName, promise, clientCallFunc = std::move(aClientCallFunc)](
            std::shared_ptr<content_analysis::sdk::Client> client) mutable {
          auto contentAnalysis = GetContentAnalysisFromService();
          if (!contentAnalysis) {
            promise->Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, aMethodName);
            return;
          }
          nsresult rv = contentAnalysis->mThreadPool->Dispatch(
              NS_NewCancelableRunnableFunction(
                  aMethodName, [aMethodName, promise,
                                clientCallFunc = std::move(clientCallFunc),
                                client = std::move(client)]() mutable {
                    auto result = clientCallFunc(client);
                    if (result.isOk()) {
                      promise->Resolve(result.unwrap(), aMethodName);
                    } else {
                      promise->Reject(result.unwrapErr(), aMethodName);
                    }
                  }));
          if (NS_FAILED(rv)) {
            LOGE(
                "Failed to launch background task in second call for %s, "
                "error=%s",
                aMethodName.get(), SafeGetStaticErrorName(rv));
            promise->Reject(rv, aMethodName);
          }
        },
        [aMethodName, promise](nsresult rv) {
          LOGE("Failed to get client again for %s, error=%s", aMethodName.get(),
               SafeGetStaticErrorName(rv));
          promise->Reject(rv, aMethodName);
        });
  };

  mCaClientPromise->Then(
      GetCurrentSerialEventTarget(), aMethodName,
      [aMethodName, promise, aClientCallFunc, reconnectAndRetry](
          std::shared_ptr<content_analysis::sdk::Client> client) mutable {
        auto contentAnalysis = GetContentAnalysisFromService();
        if (!contentAnalysis) {
          promise->Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, aMethodName);
          return;
        }
        nsresult rv = contentAnalysis->mThreadPool->Dispatch(
            NS_NewCancelableRunnableFunction(
                aMethodName, [aMethodName, promise, aClientCallFunc,
                              reconnectAndRetry = std::move(reconnectAndRetry),
                              client = std::move(client)]() mutable {
                  auto result = aClientCallFunc(client);
                  if (result.isOk()) {
                    promise->Resolve(result.unwrap(), aMethodName);
                    return;
                  }
                  nsresult rv = result.unwrapErr();
                  NS_DispatchToMainThread(NS_NewCancelableRunnableFunction(
                      "reconnect to Content Analysis client",
                      [rv, reconnectAndRetry = std::move(reconnectAndRetry)]() {
                        reconnectAndRetry(rv);
                      }));
                }));
        if (NS_FAILED(rv)) {
          LOGE(
              "Failed to launch background task in first call for %s, error=%s",
              aMethodName.get(), SafeGetStaticErrorName(rv));
          promise->Reject(rv, aMethodName);
        }
      },
      [reconnectAndRetry](nsresult rv) mutable { reconnectAndRetry(rv); });
  return promise.forget();
}

nsresult ContentAnalysis::RunAnalyzeRequestTask(
    const RefPtr<nsIContentAnalysisRequest>& aRequest, bool aAutoAcknowledge,
    const RefPtr<nsIContentAnalysisCallback>& aCallback) {
  AssertIsOnMainThread();

  nsresult rv = NS_ERROR_FAILURE;
  // Set up the scope exit before checking the return
  // value so we will call Error() if this call failed.
  auto callbackCopy = aCallback;
  auto se = MakeScopeExit([&]() {
    if (!NS_SUCCEEDED(rv)) {
      LOGE("RunAnalyzeRequestTask failed");
      callbackCopy->Error(rv);
    }
  });

  nsCString requestToken;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetRequestToken(requestToken));
  nsCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetUserActionId(userActionId));

  // We will need to submit the request to the agent.
  content_analysis::sdk::ContentAnalysisRequest pbRequest;
  rv = ConvertToProtobuf(aRequest, &pbRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  LOGD("Issuing ContentAnalysisRequest for token %s", requestToken.get());
  LogRequest(&pbRequest);
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  // Avoid serializing the string here if no one is observing this message
  if (obsServ->HasObservers("dlp-request-sent-raw")) {
    std::string requestString = pbRequest.SerializeAsString();
    nsTArray<char16_t> requestArray;
    requestArray.SetLength(requestString.size() + 1);
    for (size_t i = 0; i < requestString.size(); ++i) {
      // Since NotifyObservers() expects a null-terminated string,
      // make sure none of these values are 0.
      requestArray[i] = requestString[i] + 0xFF00;
    }
    requestArray[requestString.size()] = 0;
    obsServ->NotifyObservers(static_cast<nsIContentAnalysis*>(this),
                             "dlp-request-sent-raw", requestArray.Elements());
  }

  bool ignoreCanceled;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(
      &ignoreCanceled));

  CallClientWithRetry<std::nullptr_t>(
      __func__,
      [userActionId, pbRequest = std::move(pbRequest), aAutoAcknowledge,
       ignoreCanceled](
          std::shared_ptr<content_analysis::sdk::Client> client) mutable {
        MOZ_ASSERT(!NS_IsMainThread());
        return DoAnalyzeRequest(std::move(userActionId), std::move(pbRequest),
                                aAutoAcknowledge, client, ignoreCanceled);
      })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__, []() { /* do nothing */ },
          [userActionId, requestToken](nsresult rv) mutable {
            LOGD(
                "RunAnalyzeRequestTask failed to get client a second time for "
                "requestToken=%s, userActionId=%s",
                requestToken.get(), userActionId.get());
            RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
            if (!owner) {
              // May be shutting down
              return;
            }
            owner->CancelWithError(std::move(userActionId), rv);
          });

  return NS_OK;
}

Result<std::nullptr_t, nsresult> ContentAnalysis::DoAnalyzeRequest(
    nsCString&& aUserActionId,
    content_analysis::sdk::ContentAnalysisRequest&& aRequest,
    bool aAutoAcknowledge,
    const std::shared_ptr<content_analysis::sdk::Client>& aClient,
    bool aTestOnlyIgnoreCanceled) {
  MOZ_ASSERT(!NS_IsMainThread());
  RefPtr<ContentAnalysis> owner =
      ContentAnalysis::GetContentAnalysisFromService();
  if (!owner) {
    // May be shutting down
    // Don't return an error because we don't want to retry
    return nullptr;
  }

  if (aRequest.has_file_path() && !aRequest.file_path().empty() &&
      (!aRequest.request_data().has_digest() ||
       aRequest.request_data().digest().empty())) {
    // Calculate the digest
    nsCString digest;
    nsCString fileCPath(aRequest.file_path().data(),
                        aRequest.file_path().length());
    nsString filePath = NS_ConvertUTF8toUTF16(fileCPath);
    nsresult rv = ContentAnalysisRequest::GetFileDigest(filePath, digest);
    if (NS_FAILED(rv)) {
      owner->CancelWithError(std::move(aUserActionId), rv);
      // Don't return an error because we don't want to retry
      return nullptr;
    }
    if (!digest.IsEmpty()) {
      aRequest.mutable_request_data()->set_digest(digest.get());
    }
  }

  bool actionWasCanceled = false;
  if (!aTestOnlyIgnoreCanceled) {
    auto userActionIdToCanceledResponseMap =
        owner->mUserActionIdToCanceledResponseMap.Lock();
    actionWasCanceled =
        userActionIdToCanceledResponseMap->Contains(aUserActionId);
  }
  if (actionWasCanceled) {
    LOGD(
        "DoAnalyzeRequest | userAction: %s | requestToken: %s | was already "
        "canceled",
        aUserActionId.get(), aRequest.request_token().c_str());
    return Err(NS_ERROR_WONT_HANDLE_CONTENT);
  }

  // Run request, then dispatch back to main thread to resolve
  // aCallback
  content_analysis::sdk::ContentAnalysisResponse pbResponse;
  {
    // Insert this into the map before calling Send() because another thread
    // calling Send() may get a response before our Send() call finishes.
    auto map = owner->mRequestTokenToUserActionIdMap.Lock();
    map->InsertOrUpdate(
        nsCString(aRequest.request_token()),
        UserActionIdAndAutoAcknowledge{aUserActionId, aAutoAcknowledge});
  }

  LOGD(
      "DoAnalyzeRequest | userAction: %s | requestToken: %s | sending request "
      "to agent",
      aUserActionId.get(), aRequest.request_token().c_str());
  int err = aClient->Send(aRequest, &pbResponse);
  if (err != 0) {
    LOGE("DoAnalyzeRequest got err=%d for request_token=%s, user_action_id=%s",
         err, aRequest.request_token().c_str(), aUserActionId.get());
    {
      auto map = owner->mRequestTokenToUserActionIdMap.Lock();
      map->Remove(nsCString(aRequest.request_token()));
    }

    return Err(NS_ERROR_FAILURE);
  }
  HandleResponseFromAgent(std::move(pbResponse));
  return nullptr;
}

void ContentAnalysis::HandleResponseFromAgent(
    content_analysis::sdk::ContentAnalysisResponse&& aResponse) {
  MOZ_ASSERT(!NS_IsMainThread());
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [aResponse = std::move(aResponse)]() mutable {
        LOGD("RunAnalyzeRequestTask on main thread about to send response");
        LogResponse(&aResponse);
        RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
        if (!owner) {
          // May be shutting down
          return;
        }

        nsCOMPtr<nsIObserverService> obsServ =
            mozilla::services::GetObserverService();
        // This message is only used for testing purposes, so avoid
        // serializing the string here if no one is observing this message.
        // This message is only really useful if we're in a timeout
        // situation, otherwise dlp-response is fine.
        if (obsServ->HasObservers("dlp-response-received-raw")) {
          std::string responseString = aResponse.SerializeAsString();
          nsTArray<char16_t> responseArray;
          responseArray.SetLength(responseString.size() + 1);
          for (size_t i = 0; i < responseString.size(); ++i) {
            // Since NotifyObservers() expects a null-terminated string,
            // make sure none of these values are 0.
            responseArray[i] = responseString[i] + 0xFF00;
          }
          responseArray[responseString.size()] = 0;
          obsServ->NotifyObservers(static_cast<nsIContentAnalysis*>(owner),
                                   "dlp-response-received-raw",
                                   responseArray.Elements());
        }

        Maybe<UserActionIdAndAutoAcknowledge>
            maybeUserActionIdAndAutoAcknowledge;
        {
          auto map = owner->mRequestTokenToUserActionIdMap.Lock();
          maybeUserActionIdAndAutoAcknowledge =
              map->Extract(nsCString(aResponse.request_token()));
        }
        if (maybeUserActionIdAndAutoAcknowledge.isNothing()) {
          LOGE(
              "RunAnalyzeRequestTask could not find userActionId for "
              "request token %s",
              aResponse.request_token().c_str());
          // We have no hope of doing anything useful, so just early return.
          return;
        }
        nsCString userActionId =
            maybeUserActionIdAndAutoAcknowledge->mUserActionId;

        RefPtr<ContentAnalysisResponse> response =
            ContentAnalysisResponse::FromProtobuf(std::move(aResponse),
                                                  userActionId);
        if (!response) {
          LOGE("Content analysis got invalid response!");
          return;
        }

        // Normally, if we timeout/user-cancel a request, we remove the
        // adjacent entry in mUserActionMap.  However, we don't do that if
        // the chosen default behavior is to warn.  We don't want to issue
        // a response in that case.
        nsCString requestToken;
        MOZ_ALWAYS_SUCCEEDS(response->GetRequestToken(requestToken));
        if (owner->mWarnResponseDataMap.Contains(requestToken)) {
          return;
        }

        owner->NotifyObserversAndMaybeIssueResponseFromAgent(
            response, std::move(userActionId),
            maybeUserActionIdAndAutoAcknowledge->mAutoAcknowledge);
      }));
}

void ContentAnalysis::NotifyResponseObservers(
    ContentAnalysisResponse* aResponse, nsCString&& aUserActionId,
    bool aAutoAcknowledge, bool aIsTimeout) {
  MOZ_ASSERT(NS_IsMainThread());
  aResponse->SetOwner(this);

  if (aResponse->GetAction() == nsIContentAnalysisResponse::Action::eWarn) {
    // Store data so we can asynchronously run the warn dialog, then call
    // IssueResponse with the result.
    nsCString requestToken;
    MOZ_ALWAYS_SUCCEEDS(aResponse->GetRequestToken(requestToken));

    mWarnResponseDataMap.InsertOrUpdate(
        requestToken, WarnResponseData{aResponse, std::move(aUserActionId),
                                       aAutoAcknowledge, aIsTimeout});
  }

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  obsServ->NotifyObservers(aResponse, "dlp-response", nullptr);
}

void ContentAnalysis::IssueResponse(ContentAnalysisResponse* aResponse,
                                    nsCString&& aUserActionId,
                                    bool aAcknowledge, bool aIsTimeout) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aResponse->GetAction() !=
             nsIContentAnalysisResponse::Action::eWarn);

  // Call the callback and maybe send an auto acknowledge.
  nsCString token;
  MOZ_ALWAYS_SUCCEEDS(aResponse->GetRequestToken(token));
  RefPtr<nsIContentAnalysisCallback> callback;
  if (auto maybeUserActionData = mUserActionMap.Lookup(aUserActionId)) {
    callback = maybeUserActionData->mCallback;
  } else {
    LOGD(
        "ContentAnalysis::IssueResponse user action not found -- already "
        "responded | userActionId: %s",
        aUserActionId.get());

    if (aAcknowledge) {
      // Respond to the agent with TOO_LATE because the response arrived
      // after the request was cancelled (for any reason).
      nsIContentAnalysisAcknowledgement::FinalAction action;
      auto userActionIdToCanceledResponseMap =
          mUserActionIdToCanceledResponseMap.Lock();
      userActionIdToCanceledResponseMap->WithEntryHandle(
          aUserActionId, [&](auto&& canceledResponseEntry) {
            if (canceledResponseEntry) {
              action = canceledResponseEntry->mAction;
              --canceledResponseEntry->mNumExpectedResponses;
              if (!canceledResponseEntry->mNumExpectedResponses) {
                // We've handled all responses for canceled requests for this
                // user action.
                canceledResponseEntry.Remove();
              }
            } else {
              if (mWarnResponseDataMap.Contains(token)) {
                // We got a response from the agent but we're still waiting
                // for a warn response from the user. This can basically only
                // happen if the request timed out but TimeoutResult=1 (i.e.
                // warn) is set.
                LOGD(
                    "Got response from agent for token %s but user hasn't "
                    "replied to warn dialog yet",
                    token.get());
                return;
              }
              MOZ_ASSERT_UNREACHABLE("missing canceled response action");
              action =
                  nsIContentAnalysisAcknowledgement::FinalAction::eUnspecified;
            }
            RefPtr<ContentAnalysisAcknowledgement> acknowledgement =
                MakeRefPtr<ContentAnalysisAcknowledgement>(
                    nsIContentAnalysisAcknowledgement::Result::eTooLate,
                    action);
            aResponse->Acknowledge(acknowledgement);
          });
    }
    return;
  }

  if (aAcknowledge) {
    // Acknowledge every response we receive.
    auto acknowledgement = MakeRefPtr<ContentAnalysisAcknowledgement>(
        aIsTimeout ? nsIContentAnalysisAcknowledgement::Result::eTooLate
                   : nsIContentAnalysisAcknowledgement::Result::eSuccess,
        ConvertResult(aResponse->GetAction()));
    aResponse->Acknowledge(acknowledgement);
  }

  LOGD("Content analysis notifying observers and calling callback for token %s",
       token.get());
  callback->ContentResult(aResponse);

  // A negative verdict should have removed our user action.  (This method
  // is not called for warn verdicts.)
  MOZ_ASSERT(aResponse->GetShouldAllowContent() ||
             !mUserActionMap.Contains(aUserActionId));
}

void ContentAnalysis::NotifyObserversAndMaybeIssueResponseFromAgent(
    ContentAnalysisResponse* aResponse, nsCString&& aUserActionId,
    bool aAutoAcknowledge) {
  NotifyResponseObservers(aResponse, nsCString(aUserActionId), aAutoAcknowledge,
                          false /* isTimeout */);

  // For warn responses, IssueResponse will be called later by
  // RespondToWarnDialog, with the action replaced with the user's selection.
  if (aResponse->GetAction() != nsIContentAnalysisResponse::Action::eWarn) {
    // This is a response from the agent, so not a timeout.
    IssueResponse(aResponse, std::move(aUserActionId), aAutoAcknowledge,
                  false /* aIsTimeout */);
  }
}

static void AddCARForText(
    nsString&& text, nsIContentAnalysisRequest::Reason aReason,
    nsIContentAnalysisRequest::OperationType aOperationType, nsIURI* aURI,
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsCString&& aUserActionId,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aRequests) {
  if (text.IsEmpty()) {
    // Content Analysis doesn't expect to analyze an empty string.
    // Just skip it.
    return;
  }

  LOGD("Adding CA request for text: '%s'", NS_ConvertUTF16toUTF8(text).get());
  auto contentAnalysisRequest = MakeRefPtr<ContentAnalysisRequest>(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry, aReason,
      std::move(text), false, EmptyCString(), aURI, aOperationType,
      aWindowGlobal, aSourceWindowGlobal, std::move(aUserActionId));
  aRequests->AppendElement(contentAnalysisRequest);
}

void AddCARForFile(nsString&& filePath,
                   nsIContentAnalysisRequest::Reason aReason, nsIURI* aURI,
                   mozilla::dom::WindowGlobalParent* aWindowGlobal,
                   mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
                   nsCString&& aUserActionId,
                   nsTArray<RefPtr<nsIContentAnalysisRequest>>* aRequests) {
  if (filePath.IsEmpty()) {
    return;
  }

  // Let the content analysis code calculate the digest
  LOGD("Adding CA request for file: '%s'",
       NS_ConvertUTF16toUTF8(filePath).get());
  auto contentAnalysisRequest = MakeRefPtr<ContentAnalysisRequest>(
      nsIContentAnalysisRequest::AnalysisType::eFileAttached, aReason,
      std::move(filePath), true, EmptyCString(), aURI,
      nsIContentAnalysisRequest::OperationType::eCustomDisplayString,
      aWindowGlobal, aSourceWindowGlobal, std::move(aUserActionId));
  aRequests->AppendElement(contentAnalysisRequest);
}

static nsresult AddClipboardCARForCustomData(
    mozilla::dom::WindowGlobalParent* aWindowGlobal, nsITransferable* aTrans,
    nsIURI* aURI, mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsCString&& aUserActionId,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aRequests) {
  nsCOMPtr<nsISupports> transferData;
  if (StaticPrefs::
          browser_contentanalysis_interception_point_clipboard_plain_text_only()) {
    return NS_OK;
  }

  if (NS_FAILED(aTrans->GetTransferData(kCustomTypesMime,
                                        getter_AddRefs(transferData)))) {
    return NS_OK;  // nothing to check and not an error
  }
  nsCOMPtr<nsISupportsCString> cStringData = do_QueryInterface(transferData);
  if (!cStringData) {
    return NS_OK;  // nothing to check and not an error
  }
  nsCString str;
  nsresult rv = cStringData->GetData(str);
  if (NS_FAILED(rv)) {
    return NS_OK;  // nothing to check and not an error
  }
  nsTArray<nsString> texts;
  dom::DataTransfer::ParseExternalCustomTypesString(
      mozilla::Span(str.Data(), str.Length()),
      [&](dom::DataTransfer::ParseExternalCustomTypesStringData&& aData) {
        texts.AppendElement(std::move(std::move(aData).second));
      });
  for (auto& text : texts) {
    AddCARForText(std::move(text),
                  nsIContentAnalysisRequest::Reason::eClipboardPaste,
                  nsIContentAnalysisRequest::OperationType::eClipboard, aURI,
                  aWindowGlobal, aSourceWindowGlobal, nsCString(aUserActionId),
                  aRequests);
  }
  return NS_OK;
}

static nsresult AddClipboardCARForText(
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    nsITransferable* aTextTrans, const char* aFlavor, nsIURI* aURI,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsCString&& aUserActionId,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aRequests) {
  nsCOMPtr<nsISupports> transferData;
  if (NS_FAILED(
          aTextTrans->GetTransferData(aFlavor, getter_AddRefs(transferData)))) {
    return NS_OK;  // nothing to check and not an error
  }
  nsString text;
  nsCOMPtr<nsISupportsString> textData = do_QueryInterface(transferData);
  if (MOZ_LIKELY(textData)) {
    if (NS_FAILED(textData->GetData(text))) {
      return NS_ERROR_FAILURE;
    }
  }
  if (text.IsEmpty()) {
    nsCOMPtr<nsISupportsCString> cStringData = do_QueryInterface(transferData);
    if (cStringData) {
      nsCString cText;
      if (NS_FAILED(cStringData->GetData(cText))) {
        return NS_ERROR_FAILURE;
      }
      text = NS_ConvertUTF8toUTF16(cText);
    }
  }

  AddCARForText(
      std::move(text), nsIContentAnalysisRequest::Reason::eClipboardPaste,
      nsIContentAnalysisRequest::OperationType::eClipboard, aURI, aWindowGlobal,
      aSourceWindowGlobal, std::move(aUserActionId), aRequests);
  return NS_OK;
}

static nsresult AddClipboardCARForFile(
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    nsITransferable* aFileTrans, nsIURI* aURI,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsCString&& aUserActionId,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aRequests) {
  nsCOMPtr<nsISupports> transferData;
  nsresult rv =
      aFileTrans->GetTransferData(kFileMime, getter_AddRefs(transferData));
  if (NS_SUCCEEDED(rv)) {
    if (nsCOMPtr<nsIFile> file = do_QueryInterface(transferData)) {
      nsString filePath;
      NS_ENSURE_SUCCESS(file->GetPath(filePath), NS_ERROR_FAILURE);
      AddCARForFile(std::move(filePath),
                    nsIContentAnalysisRequest::Reason::eClipboardPaste, aURI,
                    aWindowGlobal, aSourceWindowGlobal,
                    std::move(aUserActionId), aRequests);
    } else {
      MOZ_ASSERT_UNREACHABLE("clipboard data had kFileMime but no nsIFile!");
      return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}

static Result<bool, nsresult> AddRequestsFromTransferableIfAny(
    nsIContentAnalysisRequest* aOriginalRequest, nsIURI* aUri,
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aNewRequests) {
  NS_ENSURE_TRUE(aNewRequests, Err(NS_ERROR_INVALID_ARG));

  nsCOMPtr<nsITransferable> transferable;
  NS_ENSURE_SUCCESS(
      aOriginalRequest->GetTransferable(getter_AddRefs(transferable)),
      Err(NS_ERROR_FAILURE));
  if (!transferable) {
    return false;
  }

  nsAutoCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(aOriginalRequest->GetUserActionId(userActionId));

  nsresult rv = AddClipboardCARForCustomData(
      aWindowGlobal, transferable, aUri, aSourceWindowGlobal,
      nsCString(userActionId), aNewRequests);
  NS_ENSURE_SUCCESS(rv, Err(rv));

  for (const auto& textFormat : kTextFormatsToAnalyze) {
    rv = AddClipboardCARForText(aWindowGlobal, transferable, textFormat, aUri,
                                aSourceWindowGlobal, nsCString(userActionId),
                                aNewRequests);
    NS_ENSURE_SUCCESS(rv, Err(rv));
    if (StaticPrefs::
            browser_contentanalysis_interception_point_clipboard_plain_text_only()) {
      // kTextMime is the first entry in kTextFormatsToAnalyze
      break;
    }
  }

  rv = AddClipboardCARForFile(aWindowGlobal, transferable, aUri,
                              aSourceWindowGlobal, std::move(userActionId),
                              aNewRequests);
  NS_ENSURE_SUCCESS(rv, Err(rv));
  return true;
}

static Result<bool, nsresult> AddRequestsFromDataTransferIfAny(
    nsIContentAnalysisRequest* aOriginalRequest, nsIURI* aUri,
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>* aNewRequests) {
  NS_ENSURE_TRUE(aNewRequests, Err(NS_ERROR_INVALID_ARG));

  nsCOMPtr<dom::DataTransfer> dataTransfer;
  NS_ENSURE_SUCCESS(
      aOriginalRequest->GetDataTransfer(getter_AddRefs(dataTransfer)),
      Err(NS_ERROR_FAILURE));
  if (!dataTransfer) {
    return false;
  }

  nsAutoCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(aOriginalRequest->GetUserActionId(userActionId));

  auto& principal = *nsContentUtils::GetSystemPrincipal();
  for (const auto& textFormat : kTextFormatsToAnalyze) {
    nsAutoString text;
    ErrorResult error;
    // If format is not found then 'text' will be empty.
    dataTransfer->GetData(nsString(NS_ConvertUTF8toUTF16(textFormat)), text,
                          principal, error);
    NS_ENSURE_TRUE(!error.Failed(), Err(error.StealNSResult()));

    AddCARForText(std::move(text),
                  nsIContentAnalysisRequest::Reason::eDragAndDrop,
                  nsIContentAnalysisRequest::OperationType::eDroppedText, aUri,
                  aWindowGlobal, aSourceWindowGlobal, nsCString(userActionId),
                  aNewRequests);
    if (StaticPrefs::
            browser_contentanalysis_interception_point_drag_and_drop_plain_text_only()) {
      // kTextMime is the first entry in kTextFormatsToAnalyze
      break;
    }
  }

  if (dataTransfer->HasFile()) {
    RefPtr fileList = dataTransfer->GetFiles(principal);
    for (uint32_t i = 0; i < fileList->Length(); ++i) {
      auto* file = fileList->Item(i);
      if (!file) {
        continue;
      }
      nsString filePath;
      ErrorResult error;
      file->GetMozFullPathInternal(filePath, error);
      NS_ENSURE_TRUE(!error.Failed(), Err(error.StealNSResult()));

      AddCARForFile(std::move(filePath),
                    nsIContentAnalysisRequest::Reason::eDragAndDrop, aUri,
                    aWindowGlobal, aSourceWindowGlobal, nsCString(userActionId),
                    aNewRequests);
    }
  }
  return true;
}

Result<already_AddRefed<nsIContentAnalysisRequest>, nsresult>
MakeRequestForFileInFolder(dom::File* aFile,
                           nsIContentAnalysisRequest* aFolderRequest) {
  nsCOMPtr<nsIURI> url;
  nsresult rv = aFolderRequest->GetUrl(getter_AddRefs(url));
  NS_ENSURE_SUCCESS(rv, Err(rv));
  nsIContentAnalysisRequest::AnalysisType analysisType;
  rv = aFolderRequest->GetAnalysisType(&analysisType);
  NS_ENSURE_SUCCESS(rv, Err(rv));
  nsIContentAnalysisRequest::Reason reason;
  rv = aFolderRequest->GetReason(&reason);
  NS_ENSURE_SUCCESS(rv, Err(rv));
  nsIContentAnalysisRequest::OperationType operationType;
  rv = aFolderRequest->GetOperationTypeForDisplay(&operationType);
  NS_ENSURE_SUCCESS(rv, Err(rv));
  RefPtr<dom::WindowGlobalParent> windowGlobal;
  rv = aFolderRequest->GetWindowGlobalParent(getter_AddRefs(windowGlobal));
  NS_ENSURE_SUCCESS(rv, Err(rv));
  RefPtr<mozilla::dom::WindowGlobalParent> sourceWindowGlobal;
  rv =
      aFolderRequest->GetSourceWindowGlobal(getter_AddRefs(sourceWindowGlobal));
  NS_ENSURE_SUCCESS(rv, Err(rv));
  nsCString userActionId;
  rv = aFolderRequest->GetUserActionId(userActionId);
  NS_ENSURE_SUCCESS(rv, Err(rv));

  nsAutoString pathString;
  mozilla::ErrorResult error;
  aFile->GetMozFullPathInternal(pathString, error);
  rv = error.StealNSResult();
  NS_ENSURE_SUCCESS(rv, Err(rv));

  return MakeRefPtr<ContentAnalysisRequest>(
             analysisType, reason, pathString, true, EmptyCString(), url,
             operationType, windowGlobal, sourceWindowGlobal,
             std::move(userActionId))
      .forget()
      .downcast<nsIContentAnalysisRequest>();
}

RefPtr<ContentAnalysis::MultipartRequestCallback>
ContentAnalysis::MultipartRequestCallback::Create(
    ContentAnalysis* aContentAnalysis,
    const nsTArray<ContentAnalysis::ContentAnalysisRequestArray>& aRequests,
    nsIContentAnalysisCallback* aCallback, bool aAutoAcknowledge) {
  auto mpcb = MakeRefPtr<MultipartRequestCallback>();
  mpcb->Initialize(aContentAnalysis, aRequests, aCallback, aAutoAcknowledge);
  return mpcb;
}

void ContentAnalysis::MultipartRequestCallback::Initialize(
    ContentAnalysis* aContentAnalysis,
    const nsTArray<ContentAnalysis::ContentAnalysisRequestArray>& aRequests,
    nsIContentAnalysisCallback* aCallback, bool aAutoAcknowledge) {
  MOZ_ASSERT(aContentAnalysis);
  MOZ_ASSERT(aCallback);
  MOZ_ASSERT(NS_IsMainThread());

  mWeakContentAnalysis = aContentAnalysis;
  mCallback = aCallback;

  mNumCARequestsRemaining = 0;
  nsTHashSet<nsCString> requestTokens;
  if (!aRequests.IsEmpty()) {
    for (const auto& requests : aRequests) {
      mNumCARequestsRemaining += requests.Length();
    }

    for (const auto& requests : aRequests) {
      for (const auto& request : requests) {
        // Pull the user action ID from the first entry we find.  They will
        // all have the same ID.  If that ID isn't in the user action map
        // then we were canceled while we were building the request list.
        // In that case, we haven't called the callback, so do that here.
        if (mUserActionId.IsEmpty()) {
          MOZ_ALWAYS_SUCCEEDS(request->GetUserActionId(mUserActionId));
          MOZ_ASSERT(!mUserActionId.IsEmpty());
          if (!mWeakContentAnalysis->mUserActionMap.Contains(mUserActionId)) {
            LOGD(
                "ContentAnalysis::MultipartRequestCallback created after "
                "request was canceled.  Calling callback.");
            RefPtr result = MakeRefPtr<ContentAnalysisActionResult>(
                nsIContentAnalysisResponse::Action::eCanceled);
            mCallback->ContentResult(result);
            mResponded = true;
            return;
          }
        }
        MOZ_ALWAYS_SUCCEEDS(
            request->SetUserActionRequestsCount(mNumCARequestsRemaining));
        nsCString requestToken;
        MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
        if (requestToken.IsEmpty()) {
          requestToken = GenerateUUID();
          MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
        }
        requestTokens.Insert(requestToken);
      }
    }
  }

  if (mNumCARequestsRemaining == 0) {
    // No requests will be submitted so no response will be sent by agent.
    // Respond now instead.
    LOGD(
        "Content analysis requested but nothing needs to be checked. "
        "Request is approved.");
    RefPtr result = MakeRefPtr<ContentAnalysisActionResult>(
        nsIContentAnalysisResponse::Action::eAllow);
    aCallback->ContentResult(result);
    return;
  }

  LOGD("ContentAnalysis processing %zu given and synthesized requests",
       mNumCARequestsRemaining);

  MOZ_ASSERT(!mUserActionId.IsEmpty());
  MOZ_ASSERT(!requestTokens.IsEmpty());

  auto checkedTimeoutMs =
      CheckedInt32(StaticPrefs::browser_contentanalysis_agent_timeout()) *
      1000 * mNumCARequestsRemaining;
  auto timeoutMs = checkedTimeoutMs.isValid()
                       ? checkedTimeoutMs.value()
                       : std::numeric_limits<int32_t>::max();
  // Non-positive timeout values indicate testing, and the test agent does not
  // care about this value.  Use 25ms (unscaled) in that case.
  timeoutMs = std::max(timeoutMs, 25);
  RefPtr timeoutRunnable = NS_NewCancelableRunnableFunction(
      "ContentAnalysis timeout",
      [userActionId = mUserActionId,
       weakContentAnalysis = mWeakContentAnalysis]() mutable {
        if (!weakContentAnalysis) {
          return;
        }
        // Entries awaiting a warn-dialog-selection should not be
        // considered as part of timeout.  Ignore timeout if all remaining
        // requests are awaiting a warn respones.  Otherwise cancel all of
        // them (including any awaiting a warn response) as timed out.
        bool found = false;
        if (auto remainingEntry =
                weakContentAnalysis->mUserActionMap.Lookup(userActionId)) {
          MOZ_ASSERT(!remainingEntry->mIsHandlingTimeout);
          for (const auto& remainingToken : remainingEntry->mRequestTokens) {
            if (!weakContentAnalysis->mWarnResponseDataMap.Contains(
                    remainingToken)) {
              // This request is not awaiting warn so cancel the entire user
              // action.
              found = true;
              // We do not allow calling Cancel() on runnables while they are
              // running, so this makes sure that CA does not do that.
              remainingEntry->mIsHandlingTimeout = true;
              break;
            }
          }
        }
        if (found) {
          weakContentAnalysis->CancelWithError(std::move(userActionId),
                                               NS_ERROR_DOM_TIMEOUT_ERR);
        }
      });
  NS_DelayedDispatchToCurrentThread((RefPtr{timeoutRunnable}).forget(),
                                    timeoutMs);

  // Update our entry in the user action map with the request tokens and a
  // timeout event.
  auto uaData = UserActionData{this, std::move(requestTokens), timeoutRunnable,
                               aAutoAcknowledge};
  MOZ_ASSERT(mWeakContentAnalysis->mUserActionMap.Lookup(mUserActionId));
  mWeakContentAnalysis->mUserActionMap.InsertOrUpdate(mUserActionId,
                                                      std::move(uaData));
}

NS_IMETHODIMP
ContentAnalysis::MultipartRequestCallback::ContentResult(
    nsIContentAnalysisResult* aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mWeakContentAnalysis) {
    // Remove aResult's request token from the remaining requests list.
    if (auto maybeUserActionData =
            mWeakContentAnalysis->mUserActionMap.Lookup(mUserActionId)) {
      nsCOMPtr<nsIContentAnalysisResponse> response =
          do_QueryInterface(aResult);
      MOZ_ASSERT(response);
      nsAutoCString token;
      MOZ_ALWAYS_SUCCEEDS(response->GetRequestToken(token));
      DebugOnly<bool> removed =
          maybeUserActionData->mRequestTokens.EnsureRemoved(token);
      // Either we removed the token or it was previously removed, along with
      // all others, as part of a cancellation.
      MOZ_ASSERT(removed || maybeUserActionData->mRequestTokens.IsEmpty(),
                 "Request token was not found");
    }
  }

  if (mResponded) {
    return NS_OK;
  }

  bool allow = aResult->GetShouldAllowContent();
  --mNumCARequestsRemaining;
  if (allow && mNumCARequestsRemaining > 0) {
    LOGD(
        "MultipartRequestCallback received allow response.  Awaiting "
        "%zu remaining responses",
        mNumCARequestsRemaining);
    return NS_OK;
  }

  LOGD("MultipartRequestCallback issuing response.  Permitted? %s",
       allow ? "yes" : "no");

  mResponded = true;
  mCallback->ContentResult(aResult);
  if (!allow) {
    CancelRequests();
  } else {
    RemoveFromUserActionMap();
  }
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::MultipartRequestCallback::Error(nsresult aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mResponded) {
    return NS_OK;
  }
  LOGD(
      "MultipartRequestCallback received %s while awaiting "
      "%zu remaining responses",
      SafeGetStaticErrorName(aRv), mNumCARequestsRemaining);

  mResponded = true;
  mCallback->Error(aRv);
  CancelRequests();
  return NS_OK;
}

ContentAnalysis::MultipartRequestCallback::~MultipartRequestCallback() {
  MOZ_ASSERT(NS_IsMainThread());

  // Either we have called our callback and removed our userActionId or we are
  // shutting down.
  MOZ_ASSERT(!mWeakContentAnalysis ||
             !mWeakContentAnalysis->mUserActionMap.Contains(mUserActionId) ||
             mWeakContentAnalysis->IsShutDown());
}

void ContentAnalysis::MultipartRequestCallback::CancelRequests() {
  MOZ_ASSERT(mResponded);
  // If any request fails to be submitted or is rejected then we need to
  // cancel all of the other outstanding requests.  Note that we may be
  // getting here as part of being cancelled already, in which case we
  // have nothing to cancel but our caller may still be cancelling requests
  // from our user action, which is fine.
  if (mWeakContentAnalysis) {
    mWeakContentAnalysis->CancelRequestsByUserAction(mUserActionId);
  }
}

void ContentAnalysis::MultipartRequestCallback::RemoveFromUserActionMap() {
  if (mWeakContentAnalysis) {
    mWeakContentAnalysis->RemoveFromUserActionMap(nsCString(mUserActionId));
  }
}

void ContentAnalysis::RemoveFromUserActionMap(nsCString&& aUserActionId) {
  if (auto entry = mUserActionMap.Lookup(aUserActionId)) {
    // Implementation note: we need mIsHandlingTimeout because this is called
    // during mTimeoutRunnable and CancelableRunnable is not robust to having
    // Cancel called at that time.
    if (entry->mTimeoutRunnable && !entry->mIsHandlingTimeout) {
      // Timeout may or may not have been called.
      entry->mTimeoutRunnable->Cancel();
    }
    entry.Remove();
  }
}

NS_IMPL_QUERY_INTERFACE(ContentAnalysis::MultipartRequestCallback,
                        nsIContentAnalysisCallback)

Result<RefPtr<ContentAnalysis::RequestsPromise>, nsresult>
ContentAnalysis::ExpandFolderRequest(nsIContentAnalysisRequest* aRequest,
                                     nsIFile* file) {
  // We just need to iterate over the directory, so use the junk scope
  RefPtr<mozilla::dom::Directory> directory = mozilla::dom::Directory::Create(
      xpc::NativeGlobal(xpc::PrivilegedJunkScope()), file);
  NS_ENSURE_TRUE(directory, Err(NS_ERROR_FAILURE));

  mozilla::dom::OwningFileOrDirectory owningDirectory;
  owningDirectory.SetAsDirectory() = directory;
  nsTArray<mozilla::dom::OwningFileOrDirectory> directoryArray{
      std::move(owningDirectory)};

  using mozilla::dom::GetFilesHelper;
  mozilla::ErrorResult error;
  RefPtr<GetFilesHelper> helper =
      GetFilesHelper::Create(directoryArray, true /* aRecursiveFlag */, error);
  nsresult rv = error.StealNSResult();
  NS_ENSURE_SUCCESS(rv, Err(rv));

  auto gfhPromise = MakeRefPtr<GetFilesHelper::MozPromiseType>(__func__);
  helper->AddMozPromise(gfhPromise,
                        xpc::NativeGlobal(xpc::PrivilegedJunkScope()));

  // Use MozPromise chaining (the undocumented feature where returning a
  // MozPromise from handlers chains to that new promise).  The chained
  // promise is the RequestsPromise that will resolve to requests for each
  // file in the folder.
  RefPtr<RequestsPromise> requestPromise = gfhPromise->Then(
      GetMainThreadSerialEventTarget(), "make ca file requests",
      [request = RefPtr{aRequest}](
          const nsTArray<RefPtr<mozilla::dom::File>>& aFiles) {
        ContentAnalysisRequestArray requests(aFiles.Length());
        for (const auto& file : aFiles) {
          auto requestOrError = MakeRequestForFileInFolder(file, request);
          if (requestOrError.isErr()) {
            return RequestsPromise::CreateAndReject(requestOrError.unwrapErr(),
                                                    __func__);
          }
          requests.AppendElement(requestOrError.unwrap());
        }
        return RequestsPromise::CreateAndResolve(requests, __func__);
      },
      [](nsresult rv) {
        return RequestsPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
      });

  return requestPromise;
}

// Asynchronously expand/filter requests based on policies that bypass
// the agent.  This includes replacing folder requests with requests to scan
// their contents (files), etc.  Returns either promises for all remaining
// requests (provided and synthetic) or a ContentAnalysisResult if no
// requests need to be run.
Result<RefPtr<ContentAnalysis::RequestsPromise::AllPromiseType>,
       RefPtr<nsIContentAnalysisResult>>
ContentAnalysis::GetFinalRequestList(
    const ContentAnalysisRequestArray& aRequests) {
  Maybe<NoContentAnalysisResult> allowResult;

  // We keep allowResult just in case all requests end up getting filtered.
  // It gives us an explanation for that.  If any requests survive this
  // function then allowResult isn't returned.  Negative results should
  // be returned early.  They should not set allowResult.
  auto setAllowResult = [&allowResult](NoContentAnalysisResult aVal) {
    DebugOnly checkResult = [aVal]() {
      return MakeRefPtr<ContentAnalysisNoResult>(aVal)->GetShouldAllowContent();
    };
    // shouldAllowContent must be true.
    MOZ_ASSERT(checkResult.value());

    if (!allowResult) {
      allowResult = Some(aVal);
      return;
    }
    if (*allowResult == NoContentAnalysisResult::
                            ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS) {
      // Allow aVal to override the prior allow result.
      allowResult = Some(aVal);
    }
  };

  // Expand the DataTransfer and Transferable requests into requests for
  // their individual contents.  Also filter out the requests that don't
  // need to be run.
  ContentAnalysisRequestArray expandedTransferRequests(aRequests.Length());
  for (const auto& request : aRequests) {
    // Check request's reason to see if prefs always permit this operation.
    nsIContentAnalysisRequest::Reason reason;
    MOZ_ALWAYS_SUCCEEDS(request->GetReason(&reason));
    if (!ShouldCheckReason(reason)) {
      LOGD("Allowing request -- operations of this type are always permitted.");
      setAllowResult(NoContentAnalysisResult::
                         ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS);
      continue;
    }

    // Content analysis is only needed if an outside webpage has access to
    // the data. So, skip content analysis if there is:
    //  - the window is a chrome docshell
    //  - the window is being rendered in the parent process (for example,
    //  about:support and the like)
    RefPtr<mozilla::dom::WindowGlobalParent> windowGlobal;
    request->GetWindowGlobalParent(getter_AddRefs(windowGlobal));
    nsCOMPtr<nsIURI> uri;
    request->GetUrl(getter_AddRefs(uri));
    // NOTE: We only consider uri here (when windowGlobal isn't specified)
    // for current tests to work.  gtests specify URI but no window.
    // We should never "really" hit that condition.
    if ((!windowGlobal && !uri) ||
        (windowGlobal && (windowGlobal->GetBrowsingContext()->IsChrome() ||
                          windowGlobal->IsInProcess()))) {
      LOGD("Allowing request -- window was null or chrome or in-process.");
      setAllowResult(NoContentAnalysisResult::
                         ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS);
      continue;
    }

    // Maybe skip check if source of operation is same tab.
    if (mozilla::StaticPrefs::
            browser_contentanalysis_bypass_for_same_tab_operations() &&
        SourceIsSameTab(request)) {
      // ALLOW_DUE_TO_SAME_TAB_SOURCE may replace a result of
      // ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS from an earlier
      // request.
      LOGD(
          "Allowing request -- same tab operations are always permitted by "
          "pref.");
      setAllowResult(NoContentAnalysisResult::ALLOW_DUE_TO_SAME_TAB_SOURCE);
      continue;
    }

    // Check if the context is privileged.
    if (!uri) {
      // If no URL is given then use the one for the window.
      uri = ContentAnalysis::GetURIForBrowsingContext(
          windowGlobal->Canonical()->GetBrowsingContext());
      if (!uri) {
        // if we still have no URL then the request is from a privileged window
        LOGD("Allowing request -- priviledged window.");
        setAllowResult(NoContentAnalysisResult::
                           ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS);
        continue;
      }
    }

    // Check URLs of requested info against
    // browser.contentanalysis.allow_url_regex_list/deny_url_regex_list.
    // Build the list once since creating regexs is slow.
    // Requests with URLs that match the allow list are removed from the check.
    // There is only one URL in all cases except downloads.  If all contents
    // are removed or the page URL is allowed (for downloads) then the
    // operation is allowed.
    // Requests with URLs that match the deny list block the entire operation.
    auto filterResult = FilterByUrlLists(request, uri);
    if (filterResult == ContentAnalysis::UrlFilterResult::eDeny) {
      LOGD("Blocking request due to deny URL filter.");
      return Err(MakeRefPtr<ContentAnalysisActionResult>(
          nsIContentAnalysisResponse::Action::eBlock));
    }
    if (filterResult == ContentAnalysis::UrlFilterResult::eAllow) {
      LOGD("Allowing request -- all operations match allow URL filter.");
      setAllowResult(NoContentAnalysisResult::
                         ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS);
      continue;
    }

    RefPtr<dom::WindowGlobalParent> sourceWindowGlobal;
    request->GetSourceWindowGlobal(getter_AddRefs(sourceWindowGlobal));

    Result<bool, nsresult> hadTransferOrError =
        AddRequestsFromTransferableIfAny(request, uri, windowGlobal,
                                         sourceWindowGlobal,
                                         &expandedTransferRequests);
    if (hadTransferOrError.isOk() && !hadTransferOrError.unwrap()) {
      // Request didn't have a Transferable with contents.  Check for a
      // DataTransfer.
      hadTransferOrError = AddRequestsFromDataTransferIfAny(
          request, uri, windowGlobal, sourceWindowGlobal,
          &expandedTransferRequests);
      if (hadTransferOrError.isOk() && !hadTransferOrError.unwrap()) {
        // Request didn't have a Transferable or DataTransfer with contents.
        // Copy it as-is.
        expandedTransferRequests.AppendElement(request);
      }
    }
    if (hadTransferOrError.isErr()) {
      LOGD(
          "Denying request -- error expanding nsITransferable or "
          "DataTransfer.");
      return RequestsPromise::AllPromiseType::CreateAndReject(
          hadTransferOrError.unwrapErr(), __func__);
    }
  }

  // We have expanded all Transferable and DataTransfer requests.  We now
  // look for folder requests to expand.
  ContentAnalysisRequestArray nonFolderRequests;
  nsTArray<RefPtr<RequestsPromise>> promises;
  for (auto& request : expandedTransferRequests) {
    // Always add request to nonFolderRequests unless we process a folder for
    // it. Note that the scope for this MakeScopeExit is the for loop, not the
    // function.
    auto copyRequest =
        MakeScopeExit([&]() { nonFolderRequests.AppendElement(request); });
    nsAutoString filename;
    nsresult rv = request->GetFilePath(filename);
    NS_ENSURE_SUCCESS(
        rv, RequestsPromise::AllPromiseType::CreateAndReject(rv, __func__));
    if (filename.IsEmpty()) {
      // Not a file so just copy the request to nonFolderRequests.
      continue;
    }

#ifdef DEBUG
    // Confirm that there is no text content to analyze.  See comment on
    // mFilePath.
    nsAutoString textContent;
    rv = request->GetTextContent(textContent);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(textContent.IsEmpty());
#endif

    RefPtr<nsIFile> file;
    rv = NS_NewLocalFile(filename, getter_AddRefs(file));
    NS_ENSURE_SUCCESS(
        rv, RequestsPromise::AllPromiseType::CreateAndReject(rv, __func__));

    bool exists;
    rv = file->Exists(&exists);
    NS_ENSURE_SUCCESS(
        rv, RequestsPromise::AllPromiseType::CreateAndReject(rv, __func__));
    if (!exists) {
      continue;
    }

    bool isDir;
    rv = file->IsDirectory(&isDir);
    NS_ENSURE_SUCCESS(
        rv, RequestsPromise::AllPromiseType::CreateAndReject(rv, __func__));
    if (!isDir) {
      continue;
    }

    // Don't copy the folder request.
    copyRequest.release();

    LOGD("GetFinalRequestList expanding folder: %s",
         NS_ConvertUTF16toUTF8(filename.get()).get());
    Result<RefPtr<RequestsPromise>, nsresult> requestPromiseOrError =
        ExpandFolderRequest(request, file);
    if (requestPromiseOrError.isErr()) {
      LOGD("Denying request -- error expanding folder.");
      return RequestsPromise::AllPromiseType::CreateAndReject(
          requestPromiseOrError.unwrapErr(), __func__);
    }
    promises.AppendElement(requestPromiseOrError.unwrap());
  }

  // We have expanded all requests to check folders, Transferables and
  // DataTransfers.
  if (!nonFolderRequests.IsEmpty()) {
    promises.AppendElement(RequestsPromise::CreateAndResolve(
        std::move(nonFolderRequests), "non folder requests"));
  }

  if (promises.IsEmpty()) {
    if (allowResult) {
      LOGD(
          "Allowing request -- all requests were permitted early.  "
          "NoContentAnalysisResult = %d",
          (int)*allowResult);
      return Err(MakeRefPtr<ContentAnalysisNoResult>(*allowResult));
    }

    // This can happen e.g. if the requests were for empty folders, etc.
    LOGD("Allowing request -- no requests need to be checked.");
    return Err(MakeRefPtr<ContentAnalysisNoResult>(
        NoContentAnalysisResult::
            ALLOW_DUE_TO_CONTEXT_EXEMPT_FROM_CONTENT_ANALYSIS));
  }

  // If there were any requests then ignore any allowResult because we still
  // have to do the remaining checks.
  return RequestsPromise::All(GetMainThreadSerialEventTarget(), promises);
}

NS_IMETHODIMP
ContentAnalysis::AnalyzeContentRequests(
    const nsTArray<RefPtr<nsIContentAnalysisRequest>>& aRequests,
    bool aAutoAcknowledge, JSContext* aCx, mozilla::dom::Promise** aPromise) {
  RefPtr<mozilla::dom::Promise> promise;
  nsresult rv = MakePromise(aCx, getter_AddRefs(promise));
  NS_ENSURE_SUCCESS(rv, rv);
  RefPtr<ContentAnalysisCallback> callback =
      new ContentAnalysisCallback(promise);
  promise.forget(aPromise);
  return AnalyzeContentRequestsCallback(aRequests, aAutoAcknowledge, callback);
}

NS_IMETHODIMP
ContentAnalysis::AnalyzeContentRequestsCallback(
    const nsTArray<RefPtr<nsIContentAnalysisRequest>>& aRequests,
    bool aAutoAcknowledge, nsIContentAnalysisCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aCallback);
  LOGD("ContentAnalysis::AnalyzeContentRequestsCallback received %zu requests",
       aRequests.Length());

  // Wrap callback in a ContentAnalysisCallback, which will assert if the
  // callback is not called exactly once.
  auto safeCallback = MakeRefPtr<ContentAnalysisCallback>(aCallback);

  // If any member of aRequests has a different user action ID than another,
  // throw an error.  If the user action IDs are empty, generate one and set
  // it for the requests.
  nsAutoCString userActionId;
  bool isSettingId = false;
  if (!aRequests.IsEmpty()) {
    MOZ_ALWAYS_SUCCEEDS(aRequests[0]->GetUserActionId(userActionId));
    if (userActionId.IsEmpty()) {
      userActionId = GenerateUUID();
      isSettingId = true;
    }
  }

  for (const auto& request : aRequests) {
    if (isSettingId) {
      MOZ_ALWAYS_SUCCEEDS(request->SetUserActionId(userActionId));
    } else {
      nsAutoCString givenUserActionId;
      MOZ_ALWAYS_SUCCEEDS(request->GetUserActionId(givenUserActionId));
      if (givenUserActionId != userActionId) {
        safeCallback->Error(NS_ERROR_INVALID_ARG);
        return NS_ERROR_INVALID_ARG;
      }
    }
  }
  mUserActionMap.InsertOrUpdate(
      userActionId, UserActionData{aCallback, {}, nullptr, aAutoAcknowledge});

  Result<RefPtr<RequestsPromise::AllPromiseType>,
         RefPtr<nsIContentAnalysisResult>>
      requestListResult = GetFinalRequestList(aRequests);
  if (requestListResult.isErr()) {
    auto result = requestListResult.unwrapErr();
    LOGD(
        "ContentAnalysis::AnalyzeContentRequestsCallback received early result "
        "before creating the final request list | shouldAllow = %s",
        result->GetShouldAllowContent() ? "yes" : "no");
    // On a negative result, create only one failure dialog.  For a positive
    // result, we don't bother since there is no visual indication needed.
    if (!result->GetShouldAllowContent()) {
      if (!aRequests.IsEmpty()) {
        ShowBlockedRequestDialog(aRequests[0]);
      } else {
        // No dialog could be shown since we have no window.
        LOGD("Got a negative response for an empty request?");
      }
    }
    safeCallback->ContentResult(result);
    mUserActionMap.Remove(userActionId);
    return NS_OK;
  }

  // We need to pass this object to the lambda below because we need to
  // guarantee that we can get this "real" object, not a mock, for
  // MultipartRequestCallback.
  WeakPtr<ContentAnalysis> weakThis = this;
  RefPtr<RequestsPromise::AllPromiseType> finalRequests =
      requestListResult.unwrap();
  finalRequests->Then(
      GetMainThreadSerialEventTarget(), "issue ca requests",
      [aAutoAcknowledge, safeCallback, weakThis,
       userActionId](nsTArray<ContentAnalysisRequestArray>&& aRequests) {
        // We already have weakThis but we also get the nsIContentAnalysis
        // object from the service, since we do want the mock service (if
        // any) for the call to AnalyzeContentRequestPrivate.
        // In non-test runs, they will always be the same object.
        nsCOMPtr<nsIContentAnalysis> contentAnalysis =
            mozilla::components::nsIContentAnalysis::Service();
        if (!contentAnalysis || !weakThis) {
          LOGD(
              "ContentAnalysis::AnalyzeContentRequestsCallback received "
              "response during shutdown | userActionId = %s",
              userActionId.get());
          safeCallback->Error(NS_ERROR_NOT_AVAILABLE);
          return;
        }
        RefPtr<MultipartRequestCallback> mpcb =
            MultipartRequestCallback::Create(weakThis, aRequests, safeCallback,
                                             aAutoAcknowledge);
        if (mpcb->HasResponded()) {
          // Already responded because the request has been canceled already
          // (or some other error)
          return;
        }

        for (const auto& requests : aRequests) {
          for (const auto& request : requests) {
            contentAnalysis->AnalyzeContentRequestPrivate(
                request, aAutoAcknowledge, mpcb);
          }
        }
      },
      [safeCallback, weakThis, userActionId](nsresult rv) {
        LOGD(
            "ContentAnalysis::AnalyzeContentRequestsCallback received error "
            "response: %s | userActionId = %s",
            SafeGetStaticErrorName(rv), userActionId.get());
        safeCallback->Error(rv);
        if (weakThis) {
          weakThis->mUserActionMap.Remove(userActionId);
        }
      });
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysis::AnalyzeContentRequestPrivate(
    nsIContentAnalysisRequest* aRequest, bool aAutoAcknowledge,
    nsIContentAnalysisCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  // We check this here so that async calls to this method (e.g. via a promise
  // resolve) don't send requests after being told not to.
  if (mForbidFutureRequests) {
    nsCString requestToken;
    nsresult rv = aRequest->GetRequestToken(requestToken);
    NS_ENSURE_SUCCESS(rv, rv);
    LOGD(
        "ContentAnalysis received request [%p](%s) "
        "after forbidding future requests.  Request is rejected.",
        aRequest, requestToken.get());
    aCallback->Error(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return NS_OK;
  }

  LOGD(
      "ContentAnalysis::AnalyzeContentRequestPrivate analyzing request [%p] "
      "with callback [%p]",
      aRequest, aCallback);
  auto se = MakeScopeExit([&]() {
    LOGE("AnalyzeContentRequestPrivate failed");
    aCallback->Error(NS_ERROR_FAILURE);
  });

  // Make sure we send the notification first, so if we later return
  // an error the JS will handle it correctly.
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  obsServ->NotifyObservers(aRequest, "dlp-request-made", nullptr);

  bool isActive;
  nsresult rv = GetIsActive(&isActive);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!isActive) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  ++mRequestCount;
  se.release();

  // since we're on the main thread, don't need to synchronize this
  return RunAnalyzeRequestTask(aRequest, aAutoAcknowledge, aCallback);
}

NS_IMETHODIMP
ContentAnalysis::CancelAllRequestsAssociatedWithUserAction(
    const nsACString& aUserActionId) {
  MOZ_ASSERT(NS_IsMainThread());
  // Find the compound action containing aUserActionId, if any.
  RefPtr<const UserActionSet> compoundUserAction;
  for (auto iter = mCompoundUserActions.iter(); !iter.done(); iter.next()) {
    auto& entry = iter.get();
    if (entry->has(nsCString(aUserActionId))) {
      compoundUserAction = entry;
      break;
    }
  }

  if (!compoundUserAction) {
    // It was not a compound request, just a single one.
    return CancelRequestsByUserAction(aUserActionId);
  }
  MOZ_ASSERT(!compoundUserAction->empty());

  // NB: We don't filter out completed user actions from the compound list
  // since we may need to look them up for this function later.  So we may
  // end up canceling requests that are already completed here -- that is a
  // no-op.
  LOGD("Cancelling %u requests associated with user action ID: %s",
       compoundUserAction->count(), aUserActionId.Data());
  nsresult rv = NS_OK;
  for (auto iter = compoundUserAction->iter(); !iter.done(); iter.next()) {
    nsresult rv2 = CancelRequestsByUserAction(iter.get());
    if (NS_FAILED(rv2)) {
      rv = rv2;
    }
    // If we find a user action ID for a request that is not yet complete then
    // canceling it will cancel and remove the entire compound action.  In that
    // case, we are done.
    if (!mCompoundUserActions.has(compoundUserAction)) {
      break;
    }
  }

  LOGD(
      "Cancelling compound request associated with user action ID: %s %s | "
      "Error code: %s",
      aUserActionId.Data(),
      (!mCompoundUserActions.has(compoundUserAction)) ? "succeeded" : "failed",
      SafeGetStaticErrorName(rv));
  return rv;
}

NS_IMETHODIMP
ContentAnalysis::CancelRequestsByUserAction(const nsACString& aUserActionId) {
  MOZ_ASSERT(NS_IsMainThread());
  CancelWithError(nsCString(aUserActionId), NS_ERROR_ABORT);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::CancelAllRequests(bool aForbidFutureRequests) {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD(
      "CancelAllRequests running | aForbidFutureRequests: %s | number of "
      "outstanding UserActions: %u",
      aForbidFutureRequests ? "yes" : "no", mUserActionMap.Count());
  MOZ_ASSERT(!mForbidFutureRequests);
  mForbidFutureRequests = mForbidFutureRequests | aForbidFutureRequests;

  // Keys() iterates in-place and we will change the map so we need a copy.
  for (const auto& userActionId :
       mozilla::ToTArray<nsTArray<nsCString>>(mUserActionMap.Keys())) {
    CancelRequestsByUserAction(userActionId);
  }

  // Again, Keys() iterates in-place and we change the map so we need a copy.
  for (const auto& requestToken :
       mozilla::ToTArray<nsTArray<nsCString>>(mWarnResponseDataMap.Keys())) {
    LOGD(
        "Responding to warn dialog (from CancelAllRequests) for "
        "request %s",
        requestToken.get());
    RespondToWarnDialog(requestToken, false);
  }
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::RespondToWarnDialog(const nsACString& aRequestToken,
                                     bool aAllowContent) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString token(aRequestToken);
  LOGD("Content analysis getting warn response %d for request %s",
       aAllowContent ? 1 : 0, token.get());
  auto entry = mWarnResponseDataMap.Extract(token);
  if (!entry) {
    LOGD(
        "Content analysis request not found when trying to send warn "
        "response for request %s",
        token.get());
    return NS_OK;
  }

  entry->mResponse->ResolveWarnAction(aAllowContent);
  if (entry->mWasTimeout) {
    LOGD(
        "Warn response was for a previous timeout, inserting into "
        "mUserActionIdToCanceledResponseMap for "
        "userActionId %s",
        entry->mUserActionId.get());
    size_t count = 1;
    auto userActionIdToCanceledResponseMap =
        mUserActionIdToCanceledResponseMap.Lock();
    if (auto maybeData =
            userActionIdToCanceledResponseMap->Lookup(entry->mUserActionId)) {
      count += maybeData->mNumExpectedResponses;
    }

    userActionIdToCanceledResponseMap->InsertOrUpdate(
        entry->mUserActionId,
        CanceledResponse{ConvertResult(entry->mResponse->GetAction()), count});
  }
  bool haveGottenResponse;
  {
    auto map = mRequestTokenToUserActionIdMap.Lock();
    haveGottenResponse = !map->Contains(aRequestToken);
  }

  // Don't acknowledge if we haven't gotten a response from the agent yet
  IssueResponse(entry->mResponse, nsCString(entry->mUserActionId),
                entry->mAutoAcknowledge && haveGottenResponse,
                entry->mWasTimeout);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::ShowBlockedRequestDialog(nsIContentAnalysisRequest* aRequest) {
  RefPtr<mozilla::dom::WindowGlobalParent> windowGlobal;
  MOZ_ALWAYS_SUCCEEDS(
      aRequest->GetWindowGlobalParent(getter_AddRefs(windowGlobal)));
  if (!windowGlobal) {
    // Privileged context or gtest.  Either way we show no dialog.
    return NS_OK;
  }

  nsCString token;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetRequestToken(token));
  if (token.IsEmpty()) {
    token = GenerateUUID();
    aRequest->SetRequestToken(token);
  }

  nsCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetUserActionId(userActionId));
  if (userActionId.IsEmpty()) {
    userActionId = GenerateUUID();
    aRequest->SetUserActionId(userActionId);
  }

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  obsServ->NotifyObservers(aRequest, "dlp-request-made", nullptr);
  auto response = MakeRefPtr<ContentAnalysisResponse>(
      nsIContentAnalysisResponse::Action::eBlock, std::move(token),
      std::move(userActionId));
  response->SetOwner(this);
  obsServ->NotifyObservers(response, "dlp-response", nullptr);
  return NS_OK;
}

#if defined(XP_WIN)
RefPtr<ContentAnalysis::PrintAllowedPromise>
ContentAnalysis::PrintToPDFToDetermineIfPrintAllowed(
    dom::CanonicalBrowsingContext* aBrowsingContext,
    nsIPrintSettings* aPrintSettings) {
  if (!mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_print_enabled()) {
    return PrintAllowedPromise::CreateAndResolve(PrintAllowedResult(true),
                                                 __func__);
  }
  // Note that the IsChrome() check here excludes a few
  // common about pages like about:config, about:preferences,
  // and about:support, but other about: pages may still
  // go through content analysis.
  if (aBrowsingContext->IsChrome()) {
    return PrintAllowedPromise::CreateAndResolve(PrintAllowedResult(true),
                                                 __func__);
  }
  nsCOMPtr<nsIPrintSettings> contentAnalysisPrintSettings;
  if (NS_WARN_IF(NS_FAILED(aPrintSettings->Clone(
          getter_AddRefs(contentAnalysisPrintSettings)))) ||
      NS_WARN_IF(!aBrowsingContext->GetCurrentWindowGlobal())) {
    return PrintAllowedPromise::CreateAndReject(
        PrintAllowedError(NS_ERROR_FAILURE), __func__);
  }
  contentAnalysisPrintSettings->SetOutputDestination(
      nsIPrintSettings::OutputDestinationType::kOutputDestinationStream);
  contentAnalysisPrintSettings->SetOutputFormat(
      nsIPrintSettings::kOutputFormatPDF);
  nsCOMPtr<nsIStorageStream> storageStream =
      do_CreateInstance("@mozilla.org/storagestream;1");
  if (!storageStream) {
    return PrintAllowedPromise::CreateAndReject(
        PrintAllowedError(NS_ERROR_FAILURE), __func__);
  }
  // Use segment size of 512K
  nsresult rv = storageStream->Init(0x80000, UINT32_MAX);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return PrintAllowedPromise::CreateAndReject(PrintAllowedError(rv),
                                                __func__);
  }

  nsCOMPtr<nsIOutputStream> outputStream;
  storageStream->QueryInterface(NS_GET_IID(nsIOutputStream),
                                getter_AddRefs(outputStream));
  MOZ_ASSERT(outputStream);

  contentAnalysisPrintSettings->SetOutputStream(outputStream.get());
  RefPtr<dom::CanonicalBrowsingContext> browsingContext = aBrowsingContext;
  auto promise = MakeRefPtr<PrintAllowedPromise::Private>(__func__);
  nsCOMPtr<nsIPrintSettings> finalPrintSettings(aPrintSettings);
  aBrowsingContext
      ->PrintWithNoContentAnalysis(contentAnalysisPrintSettings, true, nullptr)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [browsingContext, contentAnalysisPrintSettings, finalPrintSettings,
           promise](
              dom::MaybeDiscardedBrowsingContext cachedStaticBrowsingContext)
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA mutable {
                nsCOMPtr<nsIOutputStream> outputStream;
                contentAnalysisPrintSettings->GetOutputStream(
                    getter_AddRefs(outputStream));
                nsCOMPtr<nsIStorageStream> storageStream =
                    do_QueryInterface(outputStream);
                MOZ_ASSERT(storageStream);
                nsTArray<uint8_t> printData;
                uint32_t length = 0;
                storageStream->GetLength(&length);
                if (!printData.SetLength(length, fallible)) {
                  promise->Reject(
                      PrintAllowedError(NS_ERROR_OUT_OF_MEMORY,
                                        cachedStaticBrowsingContext),
                      __func__);
                  return;
                }
                nsCOMPtr<nsIInputStream> inputStream;
                nsresult rv = storageStream->NewInputStream(
                    0, getter_AddRefs(inputStream));
                if (NS_FAILED(rv)) {
                  promise->Reject(
                      PrintAllowedError(rv, cachedStaticBrowsingContext),
                      __func__);
                  return;
                }
                uint32_t currentPosition = 0;
                while (currentPosition < length) {
                  uint32_t elementsRead = 0;
                  // Make sure the reinterpret_cast<> below is safe
                  static_assert(std::is_trivially_assignable_v<
                                decltype(*printData.Elements()), char>);
                  rv = inputStream->Read(
                      reinterpret_cast<char*>(printData.Elements()) +
                          currentPosition,
                      length - currentPosition, &elementsRead);
                  if (NS_WARN_IF(NS_FAILED(rv) || !elementsRead)) {
                    promise->Reject(
                        PrintAllowedError(NS_FAILED(rv) ? rv : NS_ERROR_FAILURE,
                                          cachedStaticBrowsingContext),
                        __func__);
                    return;
                  }
                  currentPosition += elementsRead;
                }

                nsString printerName;
                rv = contentAnalysisPrintSettings->GetPrinterName(printerName);
                if (NS_WARN_IF(NS_FAILED(rv))) {
                  promise->Reject(
                      PrintAllowedError(rv, cachedStaticBrowsingContext),
                      __func__);
                  return;
                }

                auto* windowParent = browsingContext->GetCurrentWindowGlobal();
                if (!windowParent) {
                  // The print window may have been closed by the user by now.
                  // Cancel the print.
                  promise->Reject(
                      PrintAllowedError(NS_ERROR_ABORT,
                                        cachedStaticBrowsingContext),
                      __func__);
                  return;
                }
                nsCOMPtr<nsIURI> uri = GetURIForBrowsingContext(
                    windowParent->Canonical()->GetBrowsingContext());
                if (!uri) {
                  promise->Reject(
                      PrintAllowedError(NS_ERROR_FAILURE,
                                        cachedStaticBrowsingContext),
                      __func__);
                  return;
                }
                // It's a little unclear what we should pass to the agent if
                // print.always_print_silent is true, because in that case we
                // don't show the print preview dialog or the system print
                // dialog.
                //
                // I'm thinking of the print preview dialog case as the "normal"
                // one, so to me printing without a dialog is closer to the
                // system print dialog case.
                bool isFromPrintPreviewDialog =
                    !Preferences::GetBool("print.prefer_system_dialog") &&
                    !Preferences::GetBool("print.always_print_silent");
                RefPtr<nsIContentAnalysisRequest> contentAnalysisRequest =
                    new contentanalysis::ContentAnalysisRequest(
                        std::move(printData), std::move(uri),
                        std::move(printerName),
                        isFromPrintPreviewDialog
                            ? nsIContentAnalysisRequest::Reason::
                                  ePrintPreviewPrint
                            : nsIContentAnalysisRequest::Reason::
                                  eSystemDialogPrint,
                        windowParent);
                auto callback =
                    MakeRefPtr<contentanalysis::ContentAnalysisCallback>(
                        [browsingContext, cachedStaticBrowsingContext, promise,
                         finalPrintSettings = std::move(finalPrintSettings)](
                            nsIContentAnalysisResult* aResult)
                            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA mutable {
                              promise->Resolve(
                                  PrintAllowedResult(
                                      aResult->GetShouldAllowContent(),
                                      cachedStaticBrowsingContext),
                                  __func__);
                            },
                        [promise,
                         cachedStaticBrowsingContext](nsresult aError) {
                          promise->Reject(
                              PrintAllowedError(aError,
                                                cachedStaticBrowsingContext),
                              __func__);
                        });
                nsCOMPtr<nsIContentAnalysis> contentAnalysis =
                    mozilla::components::nsIContentAnalysis::Service();
                if (NS_WARN_IF(!contentAnalysis)) {
                  promise->Reject(
                      PrintAllowedError(rv, cachedStaticBrowsingContext),
                      __func__);
                } else {
                  bool isActive = false;
                  nsresult rv = contentAnalysis->GetIsActive(&isActive);
                  // Should not be called if content analysis is not active
                  MOZ_ASSERT(isActive);
                  Unused << NS_WARN_IF(NS_FAILED(rv));
                  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 1> requests{
                      contentAnalysisRequest};
                  rv = contentAnalysis->AnalyzeContentRequestsCallback(
                      requests, /* aAutoAcknowledge */ true, callback);
                  if (NS_WARN_IF(NS_FAILED(rv))) {
                    promise->Reject(
                        PrintAllowedError(rv, cachedStaticBrowsingContext),
                        __func__);
                  }
                }
              },
          [promise](nsresult aError) {
            promise->Reject(PrintAllowedError(aError), __func__);
          });
  return promise;
}
#endif

static nsresult CheckClipboard(
    ContentAnalysisCallback* aCallback, Maybe<int32_t> aClipboardSequenceNumber,
    bool aStoreInCache, nsITransferable* aTransferable,
    mozilla::dom::WindowGlobalParent* aWindowGlobal,
    mozilla::dom::WindowGlobalParent* aSourceWindowGlobal) {
  NoContentAnalysisResult caResult =
      NoContentAnalysisResult::DENY_DUE_TO_OTHER_ERROR;
  auto respondOnFailure = MakeScopeExit([&]() {
    LOGD("CheckClipboard skipping CA.  Response = %d", (int)caResult);
    RefPtr result = MakeRefPtr<ContentAnalysisNoResult>(caResult);
    aCallback->ContentResult(result);
  });

  nsCOMPtr<nsIContentAnalysis> contentAnalysis =
      mozilla::components::nsIContentAnalysis::Service();
  if (!contentAnalysis) {
    caResult = NoContentAnalysisResult::DENY_DUE_TO_OTHER_ERROR;
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIURI> uri =
      aWindowGlobal ? ContentAnalysis::GetURIForBrowsingContext(
                          aWindowGlobal->Canonical()->GetBrowsingContext())
                    : nullptr;

  auto request = MakeRefPtr<ContentAnalysisRequest>(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, aTransferable,
      aWindowGlobal, aSourceWindowGlobal);

  // Don't use the cache if the request can store to the cache -- that
  // is an indication that this is a separate operation from the previous
  // one.
  if (!aStoreInCache && aClipboardSequenceNumber.isSome()) {
    bool isValid = false;
    nsIContentAnalysisResponse::Action action =
        nsIContentAnalysisResponse::Action::eUnspecified;
    contentAnalysis->GetCachedResponse(uri, *aClipboardSequenceNumber, &action,
                                       &isValid);
    if (isValid) {
      LOGD("Content analysis returning cached clipboard response %d", action);
      respondOnFailure.release();
      RefPtr actionResult = MakeRefPtr<ContentAnalysisActionResult>(action);
      if (!actionResult->GetShouldAllowContent()) {
        contentAnalysis->ShowBlockedRequestDialog(request);
      }
      aCallback->ContentResult(actionResult);
      return NS_OK;
    }
  }

  RefPtr wrapperCallback = aCallback;
  if (aStoreInCache && aClipboardSequenceNumber.isSome()) {
    // Add the result to the result cache before we call the caller's callback.
    wrapperCallback = MakeRefPtr<ContentAnalysisCallback>(
        [aClipboardSequenceNumber, uri,
         callback = RefPtr(aCallback)](nsIContentAnalysisResult* aResult) {
          bool allow = aResult->GetShouldAllowContent();
          nsCOMPtr<nsIContentAnalysis> contentAnalysis =
              mozilla::components::nsIContentAnalysis::Service();
          if (contentAnalysis) {
            LOGD("Content analysis setting cached clipboard response: %s",
                 allow ? "allow" : "block");
            contentAnalysis->SetCachedResponse(
                uri, *aClipboardSequenceNumber,
                allow ? nsIContentAnalysisResponse::Action::eAllow
                      : nsIContentAnalysisResponse::Action::eBlock);
          }

          callback->ContentResult(aResult);
        },
        [callback = RefPtr(aCallback)](nsresult rv) { callback->Error(rv); });
  }

  respondOnFailure.release();

  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 1> requests{request};
  return contentAnalysis->AnalyzeContentRequestsCallback(
      requests, true /* autoAcknowledge */, wrapperCallback);
}

// This method must stay in sync with ContentAnalysis::kKnownClipboardTypes. All
// of those types must be analyzed here, and if we start analyzing more types
// here we should add it to ContentAnalysis::kKnownClipboardTypes.
void ContentAnalysis::CheckClipboardContentAnalysis(
    nsBaseClipboard* aClipboard, mozilla::dom::WindowGlobalParent* aWindow,
    nsITransferable* aTransferable, nsIClipboard::ClipboardType aClipboardType,
    ContentAnalysisCallback* aResolver, bool aForFullClipboard) {
  // Make sure we call aResolver on error.  Use the current value of
  // noCAResult.
  NoContentAnalysisResult noCAResult =
      NoContentAnalysisResult::DENY_DUE_TO_OTHER_ERROR;
  auto issueNoAnalysisResponse = MakeScopeExit([&]() {
    LOGD("CheckClipboardContentAnalysis skipping CA.  Response = %d",
         (int)noCAResult);
    auto result = MakeRefPtr<ContentAnalysisNoResult>(noCAResult);
    aResolver->ContentResult(result);
  });

  nsCOMPtr<nsIContentAnalysis> contentAnalysis =
      mozilla::components::nsIContentAnalysis::Service();
  if (!contentAnalysis) {
    noCAResult = NoContentAnalysisResult::DENY_DUE_TO_OTHER_ERROR;
    return;
  }

  bool contentAnalysisIsActive;
  nsresult rv = contentAnalysis->GetIsActive(&contentAnalysisIsActive);
  if (MOZ_LIKELY(NS_FAILED(rv) || !contentAnalysisIsActive)) {
    noCAResult =
        NoContentAnalysisResult::ALLOW_DUE_TO_CONTENT_ANALYSIS_NOT_ACTIVE;
    return;
  }

  mozilla::Maybe<uint64_t> cacheInnerWindowId =
      aClipboard->GetClipboardCacheInnerWindowId(aClipboardType);
  RefPtr<mozilla::dom::WindowGlobalParent> sourceWindowGlobal;
  if (cacheInnerWindowId.isSome()) {
    sourceWindowGlobal = mozilla::dom::WindowGlobalParent::GetByInnerWindowId(
        *cacheInnerWindowId);
  }

  Maybe<int32_t> maybeSequenceNumber =
      aClipboard->GetNativeClipboardSequenceNumber(aClipboardType)
          .map<decltype(Some<int>)>(Some)
          .unwrapOr(Nothing());

  CheckClipboard(aResolver, maybeSequenceNumber, aForFullClipboard,
                 aTransferable, aWindow, sourceWindowGlobal);

  issueNoAnalysisResponse.release();
}

bool ContentAnalysis::CheckClipboardContentAnalysisSync(
    nsBaseClipboard* aClipboard, mozilla::dom::WindowGlobalParent* aWindow,
    const nsCOMPtr<nsITransferable>& trans,
    nsIClipboard::ClipboardType aClipboardType) {
  bool requestDone = false;
  bool result;
  auto callback = MakeRefPtr<ContentAnalysisCallback>(
      [&requestDone, &result](nsIContentAnalysisResult* aResult) {
        result = aResult->GetShouldAllowContent();
        requestDone = true;
      });
  CheckClipboardContentAnalysis(aClipboard, aWindow, trans, aClipboardType,
                                callback);
  mozilla::SpinEventLoopUntil("CheckClipboardContentAnalysisSync"_ns,
                              [&requestDone]() -> bool { return requestDone; });
  return result;
}

RefPtr<ContentAnalysis::FilesAllowedPromise>
ContentAnalysis::CheckFilesInBatchMode(
    nsCOMArray<nsIFile>&& aFiles, mozilla::dom::WindowGlobalParent* aWindow,
    nsIContentAnalysisRequest::Reason aReason, nsIURI* aURI /* = nullptr */) {
  nsresult rv;
  auto contentAnalysis = GetContentAnalysisFromService();
  // Ideally the caller would check all of this before going through the work
  // of building up aFiles, but we'll double-check here.
  if (NS_WARN_IF(!contentAnalysis)) {
    return FilesAllowedPromise::CreateAndReject(rv, __func__);
  }
  bool contentAnalysisIsActive = false;
  rv = contentAnalysis->GetIsActive(&contentAnalysisIsActive);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return FilesAllowedPromise::CreateAndReject(rv, __func__);
  }
  if (!contentAnalysisIsActive) {
    return FilesAllowedPromise::CreateAndResolve(std::move(aFiles), __func__);
  }

  auto numberOfRequestsLeft = std::make_shared<size_t>(aFiles.Length());
  auto allowedFiles = MakeRefPtr<media::Refcountable<nsCOMArray<nsIFile>>>();
  auto userActionIds =
      MakeRefPtr<media::Refcountable<mozilla::HashSet<nsCString>>>();
  auto promise = MakeRefPtr<FilesAllowedPromise::Private>(__func__);
  nsCOMPtr<nsIURI> uri;
  if (aWindow) {
    uri = aWindow->GetDocumentURI();
    // Clients should only pass aURI if they're not passing aWindow.
    MOZ_ASSERT(!aURI);
  } else {
    // Should only be used in tests
    uri = aURI;
  }

  if (!contentAnalysis->mCompoundUserActions.put(userActionIds)) {
    return FilesAllowedPromise::CreateAndReject(NS_ERROR_OUT_OF_MEMORY,
                                                __func__);
  }

  auto cancelOnError = MakeScopeExit([&]() {
    // Cancel one request to cancel the compound request.
    if (!userActionIds->empty()) {
      contentAnalysis->CancelRequestsByUserAction(userActionIds->iter().get());
    }
  });

  for (auto* file : aFiles) {
#ifdef XP_WIN
    nsString pathString(file->NativePath());
#else
    nsString pathString = NS_ConvertUTF8toUTF16(file->NativePath());
#endif
    RefPtr<nsIContentAnalysisRequest> request =
        new mozilla::contentanalysis::ContentAnalysisRequest(
            nsIContentAnalysisRequest::AnalysisType::eFileAttached, aReason,
            pathString, true /* aStringIsFilePath */, EmptyCString(), uri,
            nsIContentAnalysisRequest::OperationType::eCustomDisplayString,
            aWindow);
    nsCString userActionId = GenerateUUID();
    MOZ_ALWAYS_SUCCEEDS(request->SetUserActionId(userActionId));
    if (!userActionIds->put(userActionId)) {
      return FilesAllowedPromise::CreateAndReject(NS_ERROR_OUT_OF_MEMORY,
                                                  __func__);
    }

    // For requests with the same userActionId, we multiply the timeout by the
    // number of requests to make sure the agent has enough time to handle all
    // of them. However, in this case we're using separate userActionIds for
    // each of these files to get the batch mode behavior, so set a timeout
    // multiplier to get the correct timeout.
    //
    // Note that this could theoretically be wrong, because if one of these
    // files is actually a folder this could expand into many more requests, and
    // using aFiles.Count() will undercount the total number of requests. But in
    // practice, from the Windows file dialog users can only select multiple
    // individual files that are not folders, or one single folder.
    request->SetTimeoutMultiplier(static_cast<uint32_t>(aFiles.Count()));
    nsTArray<RefPtr<nsIContentAnalysisRequest>> singleRequest{
        std::move(request)};
    auto callback =
        mozilla::MakeRefPtr<mozilla::contentanalysis::ContentAnalysisCallback>(
            // Note that this gets coerced to a std::function<>, which means it
            // has to be copyable, so everything captured here must be copyable,
            // which is why allowedFiles needs to be wrapped in a RefPtr and not
            // simply std::move()d.
            [promise, allowedFiles, numberOfRequestsLeft, file = RefPtr{file},
             userActionIds](nsIContentAnalysisResult* aResult) {
              // Since we're on the main thread, don't need to synchronize
              // access to allowedFiles or numberOfRequestsLeft
              AssertIsOnMainThread();
              nsCOMPtr<nsIContentAnalysisResponse> response =
                  do_QueryInterface(aResult);
              LOGD(
                  "Processing callback for batched file request, "
                  "numberOfRequestsLeft=%zu",
                  *(numberOfRequestsLeft.get()));
              RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
              if (response && response->GetAction() ==
                                  nsIContentAnalysisResponse::eCanceled) {
                // This was cancelled, so even if some other files have been
                // allowed we want to return an empty result.
                LOGD("Batched file request got cancel response");
                // Some of these may have finished already, but that's OK.
                // Remove the userActionIds array, then cancel its entries, so
                // that we only cancel them once.
                if (owner) {
                  if (auto entry =
                          owner->mCompoundUserActions.lookup(userActionIds)) {
                    owner->mCompoundUserActions.remove(entry);
                    for (auto iter = userActionIds->iter(); !iter.done();
                         iter.next()) {
                      owner->CancelRequestsByUserAction(iter.get());
                    }
                  }
                }
                nsCOMArray<nsIFile> emptyFiles;
                // Note that Resolve() will do nothing if the promise has
                // already been resolved.
                promise->Resolve(std::move(emptyFiles), __func__);
                return;
              }
              if (aResult->GetShouldAllowContent()) {
                allowedFiles->AppendElement(file);
              }
              (*numberOfRequestsLeft)--;
              if (*numberOfRequestsLeft == 0) {
                promise->Resolve(std::move(*allowedFiles), __func__);
                if (owner) {
                  owner->mCompoundUserActions.remove(userActionIds);
                }
              }
            },
            [promise, userActionIds](nsresult aError) {
              // cancel all requests
              AssertIsOnMainThread();
              LOGE("Batched file request got error %s",
                   SafeGetStaticErrorName(aError));
              RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
              // Some of these may have finished already, but that's OK.
              // Remove the userActionIds array, then cancel its entries, so
              // that we only cancel these once.
              if (owner) {
                if (auto entry =
                        owner->mCompoundUserActions.lookup(userActionIds)) {
                  owner->mCompoundUserActions.remove(entry);
                  for (auto iter = userActionIds->iter(); !iter.done();
                       iter.next()) {
                    owner->CancelRequestsByUserAction(iter.get());
                  }
                }
              }
              nsCOMArray<nsIFile> emptyFiles;
              // Note that Resolve() will do nothing if the promise has already
              // been resolved.
              promise->Resolve(std::move(emptyFiles), __func__);
            });
    contentAnalysis->AnalyzeContentRequestsCallback(singleRequest, true,
                                                    callback);
  }

  cancelOnError.release();
  return promise;
}

NS_IMETHODIMP
ContentAnalysisResponse::Acknowledge(
    nsIContentAnalysisAcknowledgement* aAcknowledgement) {
  MOZ_ASSERT(mOwner);
  if (mHasAcknowledged) {
    MOZ_ASSERT(false, "Already acknowledged this ContentAnalysisResponse!");
    return NS_ERROR_FAILURE;
  }
  mHasAcknowledged = true;

  if (mDoNotAcknowledge) {
    return NS_OK;
  }
  return mOwner->RunAcknowledgeTask(aAcknowledgement, mRequestToken);
};

nsresult ContentAnalysis::RunAcknowledgeTask(
    nsIContentAnalysisAcknowledgement* aAcknowledgement,
    const nsACString& aRequestToken) {
  bool isActive;
  nsresult rv = GetIsActive(&isActive);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!isActive) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  AssertIsOnMainThread();

  content_analysis::sdk::ContentAnalysisAcknowledgement pbAck;
  rv = ConvertToProtobuf(aAcknowledgement, aRequestToken, &pbAck);
  NS_ENSURE_SUCCESS(rv, rv);

  LOGD("Issuing ContentAnalysisAcknowledgement");
  LogAcknowledgement(&pbAck);

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  // Avoid serializing the string here if no one is observing this message
  if (obsServ->HasObservers("dlp-acknowledgement-sent-raw")) {
    std::string acknowledgementString = pbAck.SerializeAsString();
    nsTArray<char16_t> acknowledgementArray;
    acknowledgementArray.SetLength(acknowledgementString.size() + 1);
    for (size_t i = 0; i < acknowledgementString.size(); ++i) {
      // Since NotifyObservers() expects a null-terminated string,
      // make sure none of these values are 0.
      acknowledgementArray[i] = acknowledgementString[i] + 0xFF00;
    }
    acknowledgementArray[acknowledgementString.size()] = 0;
    obsServ->NotifyObservers(static_cast<nsIContentAnalysis*>(this),
                             "dlp-acknowledgement-sent-raw",
                             acknowledgementArray.Elements());
  }

  // The content analysis connection is synchronous so run in the background.
  LOGD("RunAcknowledgeTask dispatching acknowledge task");
  CallClientWithRetry<std::nullptr_t>(
      __func__,
      [pbAck = std::move(pbAck)](
          std::shared_ptr<content_analysis::sdk::Client> client) mutable
      -> Result<std::nullptr_t, nsresult> {
        MOZ_ASSERT(!NS_IsMainThread());
        RefPtr<ContentAnalysis> owner = GetContentAnalysisFromService();
        if (!owner) {
          // May be shutting down
          return nullptr;
        }

        int err = client->Acknowledge(pbAck);
        LOGD(
            "RunAcknowledgeTask sent transaction acknowledgement, "
            "err=%d",
            err);
        if (err != 0) {
          return Err(NS_ERROR_FAILURE);
        }
        return nullptr;
      })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__, []() { /* do nothing */ },
          [](nsresult rv) {
            LOGE("RunAcknowledgeTask failed to get the client");
          });
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::GetDiagnosticInfo(JSContext* aCx, dom::Promise** aPromise) {
  RefPtr<dom::Promise> promise;
  nsresult rv = MakePromise(aCx, getter_AddRefs(promise));
  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "ContentAnalysis::GetDiagnosticInfo promise", promise));
  NS_ENSURE_SUCCESS(rv, rv);
  AssertIsOnMainThread();
  CallClientWithRetry<std::nullptr_t>(
      __func__,
      [promiseHolder](
          std::shared_ptr<content_analysis::sdk::Client> client) mutable
      -> Result<std::nullptr_t, nsresult> {
        MOZ_ASSERT(!NS_IsMainThread());
        // I don't think this will be slow, but do it on the background thread
        // just to be safe
        std::string agentPath = client->GetAgentInfo().binary_path;
        // Need to switch back to main thread to create the
        // ContentAnalysisDiagnosticInfo and resolve the promise
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            __func__, [promiseHolder = std::move(promiseHolder),
                       agentPath = std::move(agentPath)]() {
              RefPtr<ContentAnalysis> self = GetContentAnalysisFromService();
              if (!self) {
                // may be quitting
                promiseHolder->MaybeReject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
                return;
              }
              nsString agentWidePath = NS_ConvertUTF8toUTF16(agentPath);
              // Note that if we made it here, we have successfully connected to
              // the agent.
              auto info = MakeRefPtr<ContentAnalysisDiagnosticInfo>(
                  /* mConnectedToAgent */ true, std::move(agentWidePath), false,
                  self ? self->mRequestCount : 0);
              promiseHolder->MaybeResolve(info);
            }));
        return nullptr;
      })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__, []() {},
          [promiseHolder](nsresult rv) {
            RefPtr<ContentAnalysis> self = GetContentAnalysisFromService();
            auto info = MakeRefPtr<ContentAnalysisDiagnosticInfo>(
                false, EmptyString(), rv == NS_ERROR_INVALID_SIGNATURE,
                self ? self->mRequestCount : 0);
            promiseHolder->MaybeResolve(info);
          });
  promise.forget(aPromise);
  return NS_OK;
}

/* static */ nsCOMPtr<nsIURI> ContentAnalysis::GetURIForBrowsingContext(
    dom::CanonicalBrowsingContext* aBrowsingContext) {
  dom::WindowGlobalParent* windowGlobal =
      aBrowsingContext->GetCurrentWindowGlobal();
  if (!windowGlobal) {
    return nullptr;
  }
  dom::CanonicalBrowsingContext* oldBrowsingContext = aBrowsingContext;
  nsIPrincipal* principal = windowGlobal->DocumentPrincipal();
  dom::CanonicalBrowsingContext* curBrowsingContext =
      aBrowsingContext->GetParent();
  while (curBrowsingContext) {
    dom::WindowGlobalParent* newWindowGlobal =
        curBrowsingContext->GetCurrentWindowGlobal();
    if (!newWindowGlobal) {
      break;
    }
    nsIPrincipal* newPrincipal = newWindowGlobal->DocumentPrincipal();
    if (!(newPrincipal->Subsumes(principal))) {
      break;
    }
    principal = newPrincipal;
    oldBrowsingContext = curBrowsingContext;
    curBrowsingContext = curBrowsingContext->GetParent();
  }
  if (nsContentUtils::IsPDFJS(principal)) {
    // the principal's URI is the URI of the pdf.js reader
    // so get the document's URI
    dom::WindowContext* windowContext =
        oldBrowsingContext->GetCurrentWindowContext();
    if (!windowContext) {
      return nullptr;
    }
    return windowContext->Canonical()->GetDocumentURI();
  }
  return principal->GetURI();
}

// IDL implementation
NS_IMETHODIMP ContentAnalysis::GetURIForBrowsingContext(
    dom::BrowsingContext* aBrowsingContext, nsIURI** aURI) {
  NS_ENSURE_ARG_POINTER(aBrowsingContext);
  NS_ENSURE_ARG_POINTER(aURI);
  nsCOMPtr<nsIURI> uri =
      GetURIForBrowsingContext(aBrowsingContext->Canonical());
  if (!uri) {
    return NS_ERROR_FAILURE;
  }
  uri.forget(aURI);
  return NS_OK;
}

NS_IMETHODIMP
ContentAnalysis::GetURIForDropEvent(dom::DragEvent* aEvent, nsIURI** aURI) {
  MOZ_ASSERT(XRE_IsParentProcess());
  *aURI = nullptr;
  auto* widgetEvent = aEvent->WidgetEventPtr();
  MOZ_ASSERT(widgetEvent);
  MOZ_ASSERT(widgetEvent->mClass == eDragEventClass &&
             widgetEvent->mMessage == eDrop);
  auto* bp =
      dom::BrowserParent::GetBrowserParentFromLayersId(widgetEvent->mLayersId);
  NS_ENSURE_TRUE(bp, NS_ERROR_FAILURE);
  auto* bc = bp->GetBrowsingContext();
  NS_ENSURE_TRUE(bc, NS_ERROR_FAILURE);
  return GetURIForBrowsingContext(bc, aURI);
}

NS_IMETHODIMP ContentAnalysis::MakeResponseForTest(
    nsIContentAnalysisResponse::Action aAction, const nsACString& aToken,
    const nsACString& aUserActionId,
    nsIContentAnalysisResponse** aNewResponse) {
  MakeRefPtr<ContentAnalysisResponse>(aAction, aToken, aUserActionId)
      .forget(aNewResponse);
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysisCallback::ContentResult(
    nsIContentAnalysisResult* aResult) {
  LOGD("[%p] Called ContentAnalysisCallback::ContentResult", this);
  // Grab a reference to the parameter.
  RefPtr result = aResult;
  if (mPromise) {
    mPromise->MaybeResolve(aResult);
  } else if (mContentResponseCallback) {
    mContentResponseCallback(aResult);
  } else {
    MOZ_ASSERT_UNREACHABLE("ContentAnalysisCallback called multiple times");
  }

  ClearCallbacks();
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysisCallback::Error(nsresult aError) {
  LOGD("[%p] Called ContentAnalysisCallback::Error", this);
  if (mPromise) {
    mPromise->MaybeReject(aError);
  } else if (mErrorCallback) {
    mErrorCallback(aError);
  } else {
    MOZ_ASSERT_UNREACHABLE("ContentAnalysisCallback called multiple times");
  }

  ClearCallbacks();
  return NS_OK;
}

ContentAnalysisCallback::ContentAnalysisCallback(dom::Promise* aPromise)
    : mPromise(aPromise) {}

ContentAnalysisCallback::ContentAnalysisCallback(
    std::function<void(nsIContentAnalysisResult*)>&& aContentResponseCallback) {
  mErrorCallback = [aContentResponseCallback](nsresult) {
    RefPtr noResult = MakeRefPtr<ContentAnalysisNoResult>(
        NoContentAnalysisResult::DENY_DUE_TO_OTHER_ERROR);
    aContentResponseCallback(noResult);
  };
  mContentResponseCallback = std::move(aContentResponseCallback);
}

NS_IMETHODIMP ContentAnalysisDiagnosticInfo::GetConnectedToAgent(
    bool* aConnectedToAgent) {
  *aConnectedToAgent = mConnectedToAgent;
  return NS_OK;
}
NS_IMETHODIMP ContentAnalysisDiagnosticInfo::GetAgentPath(
    nsAString& aAgentPath) {
  aAgentPath = mAgentPath;
  return NS_OK;
}
NS_IMETHODIMP ContentAnalysisDiagnosticInfo::GetFailedSignatureVerification(
    bool* aFailedSignatureVerification) {
  *aFailedSignatureVerification = mFailedSignatureVerification;
  return NS_OK;
}

NS_IMETHODIMP ContentAnalysisDiagnosticInfo::GetRequestCount(
    int64_t* aRequestCount) {
  *aRequestCount = mRequestCount;
  return NS_OK;
}

#undef LOGD
#undef LOGE
}  // namespace mozilla::contentanalysis
