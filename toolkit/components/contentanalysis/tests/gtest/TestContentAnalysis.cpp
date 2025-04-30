/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/media/MediaUtils.h"
#include "js/Object.h"
#include "js/PropertyAndElement.h"
#include "nsCOMArray.h"
#include "nsNetUtil.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsJSUtils.h"
#include "ContentAnalysis.h"
#include "SpecialSystemDirectory.h"
#include "TestContentAnalysisUtils.h"
#include <processenv.h>
#include <synchapi.h>
#include <vector>

const char* kAllowUrlPref = "browser.contentanalysis.allow_url_regex_list";
const char* kDenyUrlPref = "browser.contentanalysis.deny_url_regex_list";
const char* kPipePathNamePref = "browser.contentanalysis.pipe_path_name";
const char* kIsDLPEnabledPref = "browser.contentanalysis.enabled";
const char* kDefaultResultPref = "browser.contentanalysis.default_result";
const char* kTimeoutPref = "browser.contentanalysis.agent_timeout";
const char* kTimeoutResultPref = "browser.contentanalysis.timeout_result";
const char* kClientSignaturePref = "browser.contentanalysis.client_signature";
const char* kMaxConnections = "browser.contentanalysis.max_connections";

using namespace mozilla;
using namespace mozilla::contentanalysis;

static nsCString GenerateUUID() {
  nsID id = nsID::GenerateUUID();
  return nsCString(id.ToString().get());
}

class ContentAnalysisTest : public testing::Test {
 protected:
  ContentAnalysisTest() {
    auto* logmodule = LogModule::Get("contentanalysis");
    logmodule->SetLevel(LogLevel::Verbose);
    MOZ_ALWAYS_SUCCEEDS(
        Preferences::SetString(kPipePathNamePref, mPipeName.get()));
    MOZ_ALWAYS_SUCCEEDS(Preferences::SetBool(kIsDLPEnabledPref, true));

    nsCOMPtr<nsIContentAnalysis> caSvc =
        do_GetService("@mozilla.org/contentanalysis;1");
    MOZ_ASSERT(caSvc);
    mContentAnalysis = static_cast<ContentAnalysis*>(caSvc.get());

    // Tests run earlier could have altered these values
    mContentAnalysis->mParsedUrlLists = false;
    mContentAnalysis->mAllowUrlList = {};
    mContentAnalysis->mDenyUrlList = {};

    MOZ_ALWAYS_SUCCEEDS(mContentAnalysis->TestOnlySetCACmdLineArg(true));

    MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(kAllowUrlPref, ""));
    MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(kDenyUrlPref, ""));

    bool isActive = false;
    MOZ_ALWAYS_SUCCEEDS(mContentAnalysis->GetIsActive(&isActive));
    EXPECT_TRUE(isActive);
  }

  // Note that the constructor (and SetUp() method) get called once per test,
  // not once for the whole fixture. Because Firefox does not currently
  // reconnect to an agent after the DLP pipe is closed (bug 1888293), we only
  // want to create the agent once and make sure the same process stays alive
  // through all of these tests.
  static void SetUpTestSuite() {
    GeneratePipeName(L"contentanalysissdk-gtest-", mPipeName);
    StartAgent();
  }

  static void TearDownTestSuite() { mAgentInfo.TerminateProcess(); }

  static void StartAgent() {
    mAgentInfo = LaunchAgentNormal(L"block", L"warn", mPipeName);
  }

  void TearDown() override {
    mContentAnalysis->mParsedUrlLists = false;
    mContentAnalysis->mAllowUrlList = {};
    mContentAnalysis->mDenyUrlList = {};

    MOZ_ALWAYS_SUCCEEDS(mContentAnalysis->TestOnlySetCACmdLineArg(false));

    MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(kAllowUrlPref, ""));
    MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(kDenyUrlPref, ""));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kPipePathNamePref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kIsDLPEnabledPref));
  }

  already_AddRefed<nsIContentAnalysisRequest> CreateRequest(const char* aUrl) {
    nsCOMPtr<nsIURI> uri;
    MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), aUrl));
    // We will only use the URL and, implicitly, the analysisType
    // (behavior differs for download vs other types).
    return RefPtr(new ContentAnalysisRequest(
                      nsIContentAnalysisRequest::AnalysisType::eFileTransfer,
                      nsIContentAnalysisRequest::Reason::eFilePickerDialog,
                      EmptyString(), false, EmptyCString(), uri,
                      nsIContentAnalysisRequest::OperationType::eDroppedText,
                      nullptr))
        .forget();
  }

  nsresult SendRequestsCancelAndExpectResponse(
      RefPtr<ContentAnalysis> contentAnalysis,
      nsTArray<RefPtr<nsIContentAnalysisRequest>>& requests, bool aDelayCancel,
      bool aExpectFailure);
  // This is used to help tests clean up after terminating and restarting
  // the agent.
  void SendSimpleRequestAndWaitForResponse();
  RefPtr<ContentAnalysisDiagnosticInfo> GetDiagnosticInfo(
      RefPtr<ContentAnalysis> contentAnalysis);
  RefPtr<ContentAnalysis> mContentAnalysis;
  static nsString mPipeName;
  static MozAgentInfo mAgentInfo;

  // Proxies for private members of ContentAnalysis.  TEST_F
  // creates new subclasses -- they do not inherit `friend`s.
  // (FRIEND_TEST is another more verbose solution.)
  using UrlFilterResult = ContentAnalysis::UrlFilterResult;
  UrlFilterResult FilterByUrlLists(nsIContentAnalysisRequest* aReq) {
    // For testing, just pull the URI from the request.
    nsCOMPtr<nsIURI> uri;
    MOZ_ALWAYS_SUCCEEDS(aReq->GetUrl(getter_AddRefs(uri)));
    MOZ_ASSERT(uri);
    return mContentAnalysis->FilterByUrlLists(aReq, uri);
  }

  bool HasOutstandingCanceledRequests(const nsACString& aUserActionId) {
    auto map = mContentAnalysis->mUserActionIdToCanceledResponseMap.Lock();
    return map->Contains(aUserActionId);
  }

  auto* GetCompoundUserActions() {
    return &mContentAnalysis->mCompoundUserActions;
  }
  auto CancelAllRequestsAssociatedWithUserAction(
      const nsACString& aUserActionId) {
    return mContentAnalysis->CancelAllRequestsAssociatedWithUserAction(
        aUserActionId);
  };
};

MOZ_RUNINIT nsString ContentAnalysisTest::mPipeName;
MOZ_RUNINIT MozAgentInfo ContentAnalysisTest::mAgentInfo;

TEST_F(ContentAnalysisTest, AllowUrlList) {
  MOZ_ALWAYS_SUCCEEDS(
      Preferences::SetCString(kAllowUrlPref, ".*\\.org/match.*"));
  RefPtr<nsIContentAnalysisRequest> car =
      CreateRequest("https://example.org/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eAllow);
  car = CreateRequest("https://example.com/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
}

TEST_F(ContentAnalysisTest, DefaultAllowUrlList) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kAllowUrlPref));
  RefPtr<nsIContentAnalysisRequest> car = CreateRequest("about:home");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eAllow);
  car = CreateRequest("about:blank");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
  car = CreateRequest("about:srcdoc");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
  car = CreateRequest("https://example.com/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
}

TEST_F(ContentAnalysisTest, MultipleAllowUrlList) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(
      kAllowUrlPref, ".*\\.org/match.* .*\\.net/match.*"));
  RefPtr<nsIContentAnalysisRequest> car =
      CreateRequest("https://example.org/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eAllow);
  car = CreateRequest("https://example.net/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eAllow);
  car = CreateRequest("https://example.com/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
}

TEST_F(ContentAnalysisTest, DenyUrlList) {
  MOZ_ALWAYS_SUCCEEDS(
      Preferences::SetCString(kDenyUrlPref, ".*\\.com/match.*"));
  RefPtr<nsIContentAnalysisRequest> car =
      CreateRequest("https://example.org/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
  car = CreateRequest("https://example.com/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eDeny);
}

TEST_F(ContentAnalysisTest, MultipleDenyUrlList) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(
      kDenyUrlPref, ".*\\.com/match.* .*\\.biz/match.*"));
  RefPtr<nsIContentAnalysisRequest> car =
      CreateRequest("https://example.org/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eCheck);
  car = CreateRequest("https://example.com/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eDeny);
  car = CreateRequest("https://example.biz/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eDeny);
}

TEST_F(ContentAnalysisTest, DenyOverridesAllowUrlList) {
  MOZ_ALWAYS_SUCCEEDS(
      Preferences::SetCString(kAllowUrlPref, ".*\\.org/match.*"));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetCString(kDenyUrlPref, ".*.org/match.*"));
  RefPtr<nsIContentAnalysisRequest> car =
      CreateRequest("https://example.org/matchme/");
  ASSERT_EQ(FilterByUrlLists(car), UrlFilterResult::eDeny);
}

nsCOMPtr<nsIURI> GetExampleDotComURI() {
  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), "https://example.com"));
  return uri;
}
nsCOMPtr<nsIURI> GetExampleDotComWithPathURI() {
  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(
      NS_NewURI(getter_AddRefs(uri), "https://example.com/path"));
  return uri;
}

struct BoolStruct {
  bool mValue = false;
};

RefPtr<CancelableRunnable> QueueTimeoutToMainThread(
    RefPtr<media::Refcountable<BoolStruct>> aTimedOut) {
  RefPtr<CancelableRunnable> timer = NS_NewCancelableRunnableFunction(
      "timeout", [&] { aTimedOut->mValue = true; });
#if defined(MOZ_ASAN)
  // This can be pretty slow on ASAN builds (bug 1895256)
  constexpr uint32_t kCATimeout = 25000;
#else
  constexpr uint32_t kCATimeout = 10000;
#endif
  EXPECT_EQ(NS_OK,
            NS_DelayedDispatchToCurrentThread(do_AddRef(timer), kCATimeout));
  return timer;
}

RefPtr<ContentAnalysisDiagnosticInfo> ContentAnalysisTest::GetDiagnosticInfo(
    RefPtr<ContentAnalysis> contentAnalysis) {
  dom::AutoJSAPI jsapi;
  // We're using this context to deserialize, stringify, and print a message
  // manager message here. Since the messages are always sent from and to system
  // scopes, we need to do this in a system scope, or attempting to deserialize
  // certain privileged objects will fail.
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();
  bool gotResponse = false;
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  dom::Promise* promise = nullptr;
  RefPtr<ContentAnalysisDiagnosticInfo> diagnosticInfo = nullptr;
  MOZ_ALWAYS_SUCCEEDS(mContentAnalysis->GetDiagnosticInfo(cx, &promise));
  auto result = promise->ThenWithCycleCollectedArgs(
      [&, timedOut](JSContext* aCx, JS::Handle<JS::Value> aValue,
                    ErrorResult& aRv) -> already_AddRefed<dom::Promise> {
        if (timedOut->mValue) {
          return nullptr;
        }
        EXPECT_TRUE(aValue.isObject());
        JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
        JS::Rooted<JS::Value> value(aCx);
        EXPECT_TRUE(JS_GetProperty(aCx, obj, "connectedToAgent", &value));
        bool connectedToAgent = JS::ToBoolean(value);
        EXPECT_TRUE(JS_GetProperty(aCx, obj, "agentPath", &value));
        nsAutoJSString agentPath;
        EXPECT_TRUE(agentPath.init(aCx, value));
        EXPECT_TRUE(
            JS_GetProperty(aCx, obj, "failedSignatureVerification", &value));
        bool failedSignatureVerification = JS::ToBoolean(value);
        EXPECT_TRUE(JS_GetProperty(aCx, obj, "requestCount", &value));
        int64_t requestCount;
        EXPECT_TRUE(JS::ToInt64(aCx, value, &requestCount));
        diagnosticInfo = MakeRefPtr<ContentAnalysisDiagnosticInfo>(
            connectedToAgent, agentPath, failedSignatureVerification,
            requestCount);

        gotResponse = true;
        return nullptr;
      });

  RefPtr<CancelableRunnable> timer =
      NS_NewCancelableRunnableFunction("GetDiagnosticInfo timeout", [&] {
        if (!gotResponse) {
          timedOut->mValue = true;
        }
      });
  constexpr uint32_t kDiagnosticTimeout = 10000;
  NS_DelayedDispatchToCurrentThread(do_AddRef(timer), kDiagnosticTimeout);
  mozilla::SpinEventLoopUntil(
      "Waiting for GetDiagnosticInfo result"_ns,
      [&, timedOut]() { return gotResponse || timedOut->mValue; });
  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(timedOut->mValue);

  return diagnosticInfo;
}

template <typename T>
void ParseFromWideModifiedString(T* aTarget, const char16_t* aData) {
  std::wstring dataWideString(reinterpret_cast<const wchar_t*>(aData));
  std::vector<uint8_t> dataVector(dataWideString.size());
  for (size_t i = 0; i < dataWideString.size(); ++i) {
    // Since this data is really bytes and not a null-terminated string, the
    // calling code adds 0xFF00 to every member to ensure there are no 0 values.
    dataVector[i] = static_cast<uint8_t>(dataWideString[i] - 0xFF00);
  }
  EXPECT_TRUE(aTarget->ParseFromArray(dataVector.data(), dataVector.size()));
}

class RawAcknowledgementObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  const std::vector<content_analysis::sdk::ContentAnalysisAcknowledgement>&
  GetAcknowledgements() {
    return mAcknowledgements;
  }

 private:
  ~RawAcknowledgementObserver() = default;
  std::vector<content_analysis::sdk::ContentAnalysisAcknowledgement>
      mAcknowledgements;
};

NS_IMPL_ISUPPORTS(RawAcknowledgementObserver, nsIObserver);

NS_IMETHODIMP RawAcknowledgementObserver::Observe(nsISupports* aSubject,
                                                  const char* aTopic,
                                                  const char16_t* aData) {
  content_analysis::sdk::ContentAnalysisAcknowledgement acknowledgement;
  ParseFromWideModifiedString(&acknowledgement, aData);
  mAcknowledgements.push_back(std::move(acknowledgement));
  return NS_OK;
}

class RawRequestObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  // @param aCancelOnFirstRequest  If true, the user action is canceled when
  //                               the first request is observed.
  explicit RawRequestObserver(nsIContentAnalysis* aContentAnalysis,
                              bool aCancelOnFirstRequest = false)
      : mContentAnalysis(aContentAnalysis),
        mCancelOnFirstRequest(aCancelOnFirstRequest) {}

  const std::vector<content_analysis::sdk::ContentAnalysisRequest>&
  GetRequests() {
    return mRequests;
  }

 private:
  ~RawRequestObserver() = default;
  std::vector<content_analysis::sdk::ContentAnalysisRequest> mRequests;
  RefPtr<nsIContentAnalysis> mContentAnalysis;
  bool mCancelOnFirstRequest;
  bool mHasCanceled = false;
};

NS_IMPL_ISUPPORTS(RawRequestObserver, nsIObserver);

NS_IMETHODIMP RawRequestObserver::Observe(nsISupports* aSubject,
                                          const char* aTopic,
                                          const char16_t* aData) {
  content_analysis::sdk::ContentAnalysisRequest request;
  ParseFromWideModifiedString(&request, aData);
  mRequests.push_back(std::move(request));
  if (mCancelOnFirstRequest && !mHasCanceled) {
    nsAutoCString userActionId(mRequests[0].user_action_id().c_str());
    mContentAnalysis->CancelRequestsByUserAction(userActionId);
    mHasCanceled = true;
  }
  return NS_OK;
}

class RawAgentResponseObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  const std::vector<content_analysis::sdk::ContentAnalysisResponse>&
  GetResponses() {
    return mResponses;
  }

 private:
  ~RawAgentResponseObserver() = default;
  std::vector<content_analysis::sdk::ContentAnalysisResponse> mResponses;
};

NS_IMPL_ISUPPORTS(RawAgentResponseObserver, nsIObserver);

NS_IMETHODIMP RawAgentResponseObserver::Observe(nsISupports* aSubject,
                                                const char* aTopic,
                                                const char16_t* aData) {
  content_analysis::sdk::ContentAnalysisResponse response;
  ParseFromWideModifiedString(&response, aData);
  mResponses.push_back(std::move(response));
  return NS_OK;
}

class ResponseObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  nsCOMArray<nsIContentAnalysisResponse>& GetResponses() { return mResponses; }

 private:
  ~ResponseObserver() = default;
  nsCOMArray<nsIContentAnalysisResponse> mResponses;
};

NS_IMPL_ISUPPORTS(ResponseObserver, nsIObserver);

NS_IMETHODIMP ResponseObserver::Observe(nsISupports* aSubject,
                                        const char* aTopic,
                                        const char16_t* aData) {
  nsCOMPtr<nsIContentAnalysisResponse> response = do_QueryInterface(aSubject);
  mResponses.AppendElement(response.get());
  return NS_OK;
}

// @param aDelayCancel   Internally, GetFinalRequests expands the request
//                       list asynchronously.  If this is true, delay
//                       canceling until that happens.
nsresult ContentAnalysisTest::SendRequestsCancelAndExpectResponse(
    RefPtr<ContentAnalysis> contentAnalysis,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>& requests, bool aDelayCancel,
    bool aExpectFailure) {
  bool gotResponse = false;
  // Make timedOut a RefPtr so if we get a response from content analysis
  // after this function has finished we can safely check that (and don't
  // start accessing stack values that don't exist anymore)
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  auto callback = MakeRefPtr<ContentAnalysisCallback>(
      [&, timedOut, aExpectFailure](nsIContentAnalysisResult* result) {
        if (timedOut->mValue) {
          return;
        }
        EXPECT_EQ(false, result->GetShouldAllowContent());
        EXPECT_EQ(false, aExpectFailure);
        gotResponse = true;
      },
      [&gotResponse, timedOut, aExpectFailure](nsresult error) {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        gotResponse = true;
        EXPECT_EQ(true, aExpectFailure);
      });

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));

  nsresult rv = contentAnalysis->AnalyzeContentRequestsCallback(
      requests, false /* autoAcknowledge */, callback);
  if (NS_FAILED(rv)) {
    MOZ_ALWAYS_SUCCEEDS(
        obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
    return rv;
  }

  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);

  // The user action ID should be set by now, whether we set it or not.
  nsAutoCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(requests[0]->GetUserActionId(userActionId));
  EXPECT_TRUE(!userActionId.IsEmpty());

  bool hasCanceledRequest = false;
  if (!aDelayCancel) {
    MOZ_ALWAYS_SUCCEEDS(
        contentAnalysis->CancelRequestsByUserAction(userActionId));
    hasCanceledRequest = true;
  }

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis result"_ns, [&, timedOut]() {
        if (timedOut->mValue) {
          return true;
        }
        if (!hasCanceledRequest) {
          // (In the case of this test, nothing actually needs to be expanded.)
          if (!rawRequestObserver->GetRequests().empty()) {
            MOZ_ALWAYS_SUCCEEDS(
                contentAnalysis->CancelRequestsByUserAction(userActionId));
            hasCanceledRequest = true;
          }
        }
        return gotResponse;
      });

  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(timedOut->mValue);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
  return NS_OK;
}

void SendRequestAndExpectResponse(
    RefPtr<ContentAnalysis> contentAnalysis,
    const nsCOMPtr<nsIContentAnalysisRequest>& request,
    Maybe<bool> expectedShouldAllow,
    Maybe<nsIContentAnalysisResponse::Action> expectedAction,
    Maybe<bool> expectedIsCached) {
  std::atomic<bool> gotResponse = false;
  std::atomic<bool> gotAcknowledgement = false;
  nsCString requestToken;
  MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
  if (requestToken.IsEmpty()) {
    MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(GenerateUUID()));
  }

  // Make timedOut a RefPtr so if we get a response from content analysis
  // after this function has finished we can safely check that (and don't
  // start accessing stack values that don't exist anymore)
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  auto callback = MakeRefPtr<ContentAnalysisCallback>(
      [&, timedOut](nsIContentAnalysisResult* result) {
        if (timedOut->mValue) {
          return;
        }
        nsCOMPtr<nsIContentAnalysisResponse> response =
            do_QueryInterface(result);
        EXPECT_TRUE(response);
        if (expectedShouldAllow.isSome()) {
          EXPECT_EQ(*expectedShouldAllow, response->GetShouldAllowContent());
        }
        if (expectedAction.isSome()) {
          EXPECT_EQ(*expectedAction, response->GetAction());
        }
        if (expectedIsCached.isSome()) {
          bool isCached;
          MOZ_ALWAYS_SUCCEEDS(response->GetIsCachedResponse(&isCached));
          EXPECT_EQ(*expectedIsCached, isCached);
        }
        nsCString requestToken, originalRequestToken;
        MOZ_ALWAYS_SUCCEEDS(response->GetRequestToken(requestToken));
        MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(originalRequestToken));
        EXPECT_EQ(originalRequestToken, requestToken);
        nsCString userActionId, originalUserActionId;
        MOZ_ALWAYS_SUCCEEDS(response->GetUserActionId(userActionId));
        MOZ_ALWAYS_SUCCEEDS(request->GetUserActionId(originalUserActionId));
        EXPECT_EQ(originalUserActionId, userActionId);
        gotResponse = true;
      },
      [&gotResponse, &gotAcknowledgement, timedOut](nsresult error) {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        gotResponse = true;
        // An acknowledgement won't be sent, so don't wait for one
        gotAcknowledgement = true;
        FAIL() << "Got error response";
      });

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));

  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 1> requests{request.get()};
  MOZ_ALWAYS_SUCCEEDS(contentAnalysis->AnalyzeContentRequestsCallback(
      requests, true, callback));
  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis result"_ns, [&, timedOut]() {
        if (timedOut->mValue) {
          return true;
        }

        auto acknowledgements =
            rawAcknowledgementObserver->GetAcknowledgements();
        nsCString requestToken;
        MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
        for (const auto& acknowledgement : acknowledgements) {
          if (nsCString(acknowledgement.request_token()) == requestToken) {
            // Wait for the acknowledgement to happen to avoid background
            // activity that might interfere with other tests.
            gotAcknowledgement = true;
            break;
          }
        }

        return gotResponse.load() && gotAcknowledgement.load();
      });
  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_TRUE(gotAcknowledgement);
  EXPECT_FALSE(timedOut->mValue);
  obsServ->RemoveObserver(rawAcknowledgementObserver,
                          "dlp-acknowledgement-sent-raw");
}

void ContentAnalysisTest::SendSimpleRequestAndWaitForResponse() {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allowCleanup");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Some(false));
}

void SendRequestsAndExpectNoAgentResponseNoAwait(
    RefPtr<ContentAnalysis> contentAnalysis,
    nsTArray<RefPtr<nsIContentAnalysisRequest>>& requests,
    bool expectedShouldAllow,
    nsIContentAnalysisResponse::CancelError expectedCancelError,
    bool* gotResponse, RefPtr<media::Refcountable<BoolStruct>> timedOut) {
  auto callback = MakeRefPtr<ContentAnalysisCallback>(
      [=](nsIContentAnalysisResult* result) mutable {
        if (timedOut->mValue) {
          return;
        }
        nsCOMPtr<nsIContentAnalysisResponse> response =
            do_QueryInterface(result);
        EXPECT_TRUE(response);
        EXPECT_EQ(expectedCancelError, response->GetCancelError());
        EXPECT_EQ(expectedShouldAllow, response->GetShouldAllowContent());
        *gotResponse = true;
      },
      [=](nsresult error) mutable {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        *gotResponse = true;
        FAIL() << "Got error response";
      });

  MOZ_ALWAYS_SUCCEEDS(contentAnalysis->AnalyzeContentRequestsCallback(
      requests, false, callback));
}

void SendRequestAndExpectNoAgentResponseNoAwait(
    RefPtr<ContentAnalysis> contentAnalysis, nsIContentAnalysisRequest* request,
    bool expectedShouldAllow,
    nsIContentAnalysisResponse::CancelError expectedCancelError,
    bool* gotResponse, RefPtr<media::Refcountable<BoolStruct>> timedOut) {
  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 1> requests = {request};
  SendRequestsAndExpectNoAgentResponseNoAwait(
      contentAnalysis, requests, expectedShouldAllow, expectedCancelError,
      gotResponse, timedOut);
}

void SendRequestAndExpectNoAgentResponse(
    RefPtr<ContentAnalysis> contentAnalysis,
    nsCOMPtr<nsIContentAnalysisRequest> request,
    bool expectedShouldAllow = false,
    nsIContentAnalysisResponse::CancelError expectedCancelError =
        nsIContentAnalysisResponse::CancelError::eNoAgent) {
  bool gotResponse = false;
  // Make timedOut a RefPtr so if we get a response from content analysis
  // after this function has finished we can safely check that (and don't
  // start accessing stack values that don't exist anymore)
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  SendRequestAndExpectNoAgentResponseNoAwait(
      contentAnalysis, request, expectedShouldAllow, expectedCancelError,
      &gotResponse, timedOut);

  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);
  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis result"_ns,
      [&, timedOut]() { return gotResponse || timedOut->mValue; });
  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(timedOut->mValue);
}

nsCOMPtr<nsIFile> GetFileFromLocalDirectory(const std::wstring& filename) {
  nsCOMPtr<nsIFile> file;
  MOZ_ALWAYS_SUCCEEDS(GetSpecialSystemDirectory(OS_CurrentWorkingDirectory,
                                                getter_AddRefs(file)));
  nsString relativePath(filename.c_str(), filename.length());
  MOZ_ALWAYS_SUCCEEDS(file->AppendRelativePath(relativePath));
  return file;
}

TEST_F(ContentAnalysisTest, SendAllowedTextToAgent_GetAllowedResponse) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Some(false));
}

TEST_F(ContentAnalysisTest, SendBlockedTextToAgent_GetBlockResponse) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString block(L"block");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(block),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  SendRequestAndExpectResponse(mContentAnalysis, request, Some(false),
                               Some(nsIContentAnalysisResponse::eBlock),
                               Some(false));
}

TEST_F(ContentAnalysisTest,
       RestartAgent_SendAllowedTextToAgent_GetAllowedResponse) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  mAgentInfo.TerminateProcess();
  StartAgent();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Some(false));
}

TEST_F(ContentAnalysisTest, TerminateAgent_SendAllowedTextToAgent_GetError) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  mAgentInfo.TerminateProcess();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  SendRequestAndExpectNoAgentResponse(mContentAnalysis, request);
  StartAgent();
  // NB: We are re-using the user action ID here.  That is not required to
  // work, but currently does.  Alt: we could clear request.userActionId.
  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Some(false));
}

TEST_F(ContentAnalysisTest,
       TerminateAgent_SendAllowedTextToAgentWithDefaultAllow_GetAllowResponse) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kDefaultResultPref, 2));
  auto ignore = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kDefaultResultPref)); });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  mAgentInfo.TerminateProcess();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  SendRequestAndExpectNoAgentResponse(mContentAnalysis, request, true);
  StartAgent();

  SendSimpleRequestAndWaitForResponse();
}

TEST_F(ContentAnalysisTest, CheckRawRequestWithText) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, 65));
  auto ignore = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref)); });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(
        obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
  });
  time_t now = time(nullptr);

  SendRequestAndExpectResponse(mContentAnalysis, request, Nothing(), Nothing(),
                               Some(false));
  auto requests = rawRequestObserver->GetRequests();
  EXPECT_EQ(static_cast<size_t>(1), requests.size());
  time_t t = requests[0].expires_at();
  time_t secs_remaining = t - now;
  // There should be around 65 seconds remaining
  EXPECT_LE(abs(secs_remaining - 65), 8);
  const auto& request_url = requests[0].request_data().url();
  EXPECT_EQ(uri->GetSpecOrDefault(),
            nsCString(request_url.data(), request_url.size()));
  const auto& request_text = requests[0].text_content();
  EXPECT_EQ(nsCString("allow"),
            nsCString(request_text.data(), request_text.size()));
}

TEST_F(ContentAnalysisTest, CheckRawRequestWithFile) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsCOMPtr<nsIFile> file = GetFileFromLocalDirectory(L"allowedFile.txt");
  nsString allowPath;
  MOZ_ALWAYS_SUCCEEDS(file->GetPath(allowPath));

  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, allowPath, true,
      EmptyCString(), uri, nsIContentAnalysisRequest::OperationType::eClipboard,
      nullptr);
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));

  SendRequestAndExpectResponse(mContentAnalysis, request, Nothing(), Nothing(),
                               Some(false));
  auto requests = rawRequestObserver->GetRequests();
  EXPECT_EQ(static_cast<size_t>(1), requests.size());
  const auto& request_url = requests[0].request_data().url();
  EXPECT_EQ(uri->GetSpecOrDefault(),
            nsCString(request_url.data(), request_url.size()));
  const auto& request_file_path = requests[0].file_path();
  EXPECT_EQ(NS_ConvertUTF16toUTF8(allowPath),
            nsCString(request_file_path.data(), request_file_path.size()));

  MOZ_ALWAYS_SUCCEEDS(
      obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
}

TEST_F(ContentAnalysisTest, CheckTwoRequestsHaveDifferentUserActionId) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow1(L"allowMe");
  nsCOMPtr<nsIContentAnalysisRequest> request1 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  // Use different text so the request doesn't match the cache
  nsString allow2(L"allowMeAgain1");
  nsCOMPtr<nsIContentAnalysisRequest> request2 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow2),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));

  SendRequestAndExpectResponse(mContentAnalysis, request1, Nothing(), Nothing(),
                               Some(false));
  SendRequestAndExpectResponse(mContentAnalysis, request2, Nothing(), Nothing(),
                               Some(false));
  auto requests = rawRequestObserver->GetRequests();
  EXPECT_EQ(static_cast<size_t>(2), requests.size());
  EXPECT_NE(requests[0].user_action_id(), requests[1].user_action_id());

  MOZ_ALWAYS_SUCCEEDS(
      obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
}

TEST_F(ContentAnalysisTest,
       CheckRequestTokensCanCancelAndHaveSameUserActionId) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow1(L"allowMe");
  RefPtr<nsIContentAnalysisRequest> request1 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  // Use different text so the request doesn't match the cache
  nsString allow2(L"allowMeAgain2");
  RefPtr<nsIContentAnalysisRequest> request2 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow2),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  nsTArray<RefPtr<nsIContentAnalysisRequest>> requests{request1, request2};
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));

  nsresult rv = SendRequestsCancelAndExpectResponse(mContentAnalysis, requests,
                                                    true /* aDelayCancel */,
                                                    false /* aExpectFailure */);
  EXPECT_EQ(rv, NS_OK);

  auto rawRequests = rawRequestObserver->GetRequests();
  EXPECT_EQ(static_cast<size_t>(2), rawRequests.size());
  EXPECT_EQ(rawRequests[0].user_action_id(), rawRequests[1].user_action_id());

  MOZ_ALWAYS_SUCCEEDS(
      obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
}

TEST_F(ContentAnalysisTest, CheckAssignedUserActionIdCanCancel) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow1(L"allowMe");
  RefPtr<nsIContentAnalysisRequest> request1 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);

  // Use different text so the request doesn't match the cache
  nsString allow2(L"allowMeAgain3");
  RefPtr<nsIContentAnalysisRequest> request2 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow2),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  nsTArray<RefPtr<nsIContentAnalysisRequest>> requests{request1, request2};

  nsresult rv = SendRequestsCancelAndExpectResponse(mContentAnalysis, requests,
                                                    false /* aDelayCancel*/,
                                                    false /* aExpectFailure */);
  EXPECT_EQ(rv, NS_OK);
}

TEST_F(ContentAnalysisTest, CheckGivenUserActionIdCanCancel) {
  nsCString userActionId = GenerateUUID();
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();

  nsString allow1(L"allowMe");
  RefPtr<nsIContentAnalysisRequest> request1 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr, nullptr,
      nsCString(userActionId));

  // Use different text so the request doesn't match the cache
  nsString allow2(L"allowMeAgain4");
  RefPtr<nsIContentAnalysisRequest> request2 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow2),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr, nullptr,
      nsCString(userActionId));
  nsTArray<RefPtr<nsIContentAnalysisRequest>> requests{request1, request2};
  nsresult rv = SendRequestsCancelAndExpectResponse(mContentAnalysis, requests,
                                                    false /* aDelayCancel */,
                                                    false /* aExpectFailure */);
  EXPECT_EQ(rv, NS_OK);
}

TEST_F(ContentAnalysisTest, CheckGivenUserActionIdsMustMatch) {
  nsCString userActionId1 = GenerateUUID();
  nsCString userActionId2 = GenerateUUID();
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();

  nsString allow1(L"allowMe");
  RefPtr<nsIContentAnalysisRequest> request1 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr, nullptr,
      nsCString(userActionId1));

  // Use different text so the request doesn't match the cache
  nsString allow2(L"allowMeAgain5");
  RefPtr<nsIContentAnalysisRequest> request2 = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow2),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr, nullptr,
      nsCString(userActionId2));
  nsTArray<RefPtr<nsIContentAnalysisRequest>> requests{request1, request2};

  nsresult rv = SendRequestsCancelAndExpectResponse(mContentAnalysis, requests,
                                                    false /* aDelayCancel */,
                                                    true /* aExpectFailure */);
  EXPECT_EQ(rv, NS_ERROR_INVALID_ARG);
}

enum class WarnDialogResponse {
  // Simulate clicking "Allow" on warn dialog
  Allow,
  // Simulate clicking "Block" on warn dialog
  Block
};

enum class AutoAcknowledge { Yes, No };

enum class WaitForAgentResponseToRespondToWarn { Yes, No };

void SendRequestAndExpectWarnResponse(
    RefPtr<ContentAnalysis> contentAnalysis,
    nsCOMPtr<nsIContentAnalysisRequest>& request,
    WarnDialogResponse aWarnDialogResponse,
    WaitForAgentResponseToRespondToWarn aWaitForAgent =
        WaitForAgentResponseToRespondToWarn::No,
    AutoAcknowledge aAutoAcknowledge = AutoAcknowledge::No) {
  nsCString requestToken;
  MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
  if (requestToken.IsEmpty()) {
    requestToken = GenerateUUID();
    MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
  }
  std::atomic<bool> gotResponse = false;
  // Make timedOut a RefPtr so if we get a response from content analysis
  // after this function has finished we can safely check that (and don't
  // start accessing stack values that don't exist anymore)
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  bool warnDialogResponseIsAllow =
      aWarnDialogResponse == WarnDialogResponse::Allow;
  auto callback = MakeRefPtr<ContentAnalysisCallback>(
      [&, timedOut](nsIContentAnalysisResult* result) {
        if (timedOut->mValue) {
          return;
        }
        nsCOMPtr<nsIContentAnalysisResponse> response =
            do_QueryInterface(result);
        EXPECT_TRUE(response);
        EXPECT_EQ(warnDialogResponseIsAllow, response->GetShouldAllowContent());
        EXPECT_EQ(warnDialogResponseIsAllow
                      ? nsIContentAnalysisResponse::Action::eAllow
                      : nsIContentAnalysisResponse::Action::eBlock,
                  response->GetAction());
        nsCString responseRequestToken;
        MOZ_ALWAYS_SUCCEEDS(response->GetRequestToken(responseRequestToken));
        EXPECT_EQ(requestToken, responseRequestToken);
        gotResponse = true;
      },
      [&gotResponse, timedOut](nsresult error) {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        gotResponse = true;
        FAIL() << "Got error response";
      });

  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 1> requests{request.get()};

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto responseObserver = MakeRefPtr<ResponseObserver>();
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(responseObserver, "dlp-response", false));
  auto agentResponseObserver = MakeRefPtr<RawAgentResponseObserver>();
  if (aWaitForAgent == WaitForAgentResponseToRespondToWarn::Yes) {
    MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
        agentResponseObserver, "dlp-response-received-raw", false));
  }

  MOZ_ALWAYS_SUCCEEDS(contentAnalysis->AnalyzeContentRequestsCallback(
      requests, aAutoAcknowledge == AutoAcknowledge::Yes, callback));

  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis warn response"_ns, [&, timedOut]() {
        if (timedOut->mValue) {
          return true;
        }
        for (auto* response : responseObserver->GetResponses()) {
          nsCString responseRequestToken;
          MOZ_ALWAYS_SUCCEEDS(response->GetRequestToken(responseRequestToken));
          if (requestToken == responseRequestToken) {
            EXPECT_EQ(nsIContentAnalysisResponse::Action::eWarn,
                      response->GetAction());
            return true;
          }
        }
        return false;
      });
  if (aWaitForAgent == WaitForAgentResponseToRespondToWarn::Yes) {
    mozilla::SpinEventLoopUntil(
        "Waiting for agent response"_ns, [&, timedOut]() {
          if (timedOut->mValue) {
            return true;
          }
          for (const auto& response : agentResponseObserver->GetResponses()) {
            nsCString responseRequestToken(response.request_token());
            if (requestToken == responseRequestToken) {
              return true;
            }
          }
          return false;
        });
  }
  EXPECT_EQ(NS_OK, contentAnalysis->RespondToWarnDialog(
                       requestToken, warnDialogResponseIsAllow));
  // Result should happen immediately
  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(timedOut->mValue);
  if (aWaitForAgent == WaitForAgentResponseToRespondToWarn::Yes) {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(agentResponseObserver,
                                                "dlp-response-received-raw"));
  }
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->RemoveObserver(responseObserver, "dlp-response"));
}

TEST_F(ContentAnalysisTest, WarnWithUserRespondingAllow) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString warn(L"warn");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(warn),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectWarnResponse(mContentAnalysis, request,
                                   WarnDialogResponse::Allow);
}

TEST_F(ContentAnalysisTest, WarnWithUserRespondingBlock) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString warn(L"warn");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(warn),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectWarnResponse(mContentAnalysis, request,
                                   WarnDialogResponse::Block);
}

TEST_F(ContentAnalysisTest, CheckBrowserReportsTimeout) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we acknowledge as TOO_LATE.
  // A negative timeout tells Firefox to timeout after 25ms.  The agent
  // always takes 100ms for requests in tests.  TODO: can we further reduce
  // these?
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  auto ignore = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref)); });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow1(L"allowMe");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });
  SendRequestAndExpectResponse(
      mContentAnalysis, request, Some(false) /* expectedShouldAllow */,
      Some(nsIContentAnalysisResponse::Action::eCanceled),
      Some(false) /* expectIsCached */);

  // The request returns before the ack is sent.  Give it some time to catch up.
  RefPtr hitTimeout = MakeRefPtr<media::Refcountable<BoolStruct>>();
  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(hitTimeout);

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis acknowledgement"_ns, [&]() {
        auto acknowledgements =
            rawAcknowledgementObserver->GetAcknowledgements();
        nsCString requestToken;
        MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
        for (const auto& acknowledgement : acknowledgements) {
          if (nsCString(acknowledgement.request_token()) == requestToken) {
            EXPECT_EQ(::content_analysis::sdk::
                          ContentAnalysisAcknowledgement_FinalAction::
                              ContentAnalysisAcknowledgement_FinalAction_BLOCK,
                      acknowledgement.final_action());
            EXPECT_EQ(
                ::content_analysis::sdk::ContentAnalysisAcknowledgement_Status::
                    ContentAnalysisAcknowledgement_Status_TOO_LATE,
                acknowledgement.status());
            return true;
          }
        }
        return hitTimeout->mValue;
      });

  timer->Cancel();
  EXPECT_FALSE(hitTimeout->mValue);
}

TEST_F(ContentAnalysisTest, CheckBrowserReportsTimeoutWithDefaultTimeoutAllow) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we respect the timeout_result
  // pref.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutResultPref, 2));
  auto ignore = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutResultPref));
  });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow1(L"allowMe");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow1),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });
  SendRequestAndExpectResponse(mContentAnalysis, request,
                               Some(true) /* expectedShouldAllow */,
                               Some(nsIContentAnalysisResponse::Action::eAllow),
                               Some(false) /* expectIsCached */);

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis acknowledgement"_ns, [&]() {
        auto acknowledgements =
            rawAcknowledgementObserver->GetAcknowledgements();
        nsCString requestToken;
        MOZ_ALWAYS_SUCCEEDS(request->GetRequestToken(requestToken));
        for (const auto& acknowledgement : acknowledgements) {
          if (nsCString(acknowledgement.request_token()) == requestToken) {
            EXPECT_EQ(::content_analysis::sdk::
                          ContentAnalysisAcknowledgement_FinalAction::
                              ContentAnalysisAcknowledgement_FinalAction_ALLOW,
                      acknowledgement.final_action());
            EXPECT_EQ(
                ::content_analysis::sdk::ContentAnalysisAcknowledgement_Status::
                    ContentAnalysisAcknowledgement_Status_TOO_LATE,
                acknowledgement.status());
            return true;
          }
        }
        return false;
      });
}

void WaitForTooLateAcknowledgement(
    RawAcknowledgementObserver* aObserver, const nsCString& aRequestToken,
    content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction
        aExpectedFinalAction) {
  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis acknowledgement"_ns, [&]() {
        auto acknowledgements = aObserver->GetAcknowledgements();
        nsCString requestToken;
        for (const auto& acknowledgement : acknowledgements) {
          if (nsCString(acknowledgement.request_token()) == aRequestToken) {
            EXPECT_EQ(aExpectedFinalAction, acknowledgement.final_action());
            EXPECT_EQ(
                ::content_analysis::sdk::ContentAnalysisAcknowledgement_Status::
                    ContentAnalysisAcknowledgement_Status_TOO_LATE,
                acknowledgement.status());
            return true;
          }
        }
        return false;
      });
}

TEST_F(ContentAnalysisTest,
       CheckBrowserReportsTimeoutWithDefaultTimeoutWarnAndUserAllow) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we respect the timeout_result
  // pref.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutResultPref, 1));
  auto ignore = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutResultPref));
  });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allowMe");
  nsCString requestToken = GenerateUUID();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });

  SendRequestAndExpectWarnResponse(
      mContentAnalysis, request, WarnDialogResponse::Allow,
      WaitForAgentResponseToRespondToWarn::No, AutoAcknowledge::Yes);

  WaitForTooLateAcknowledgement(
      rawAcknowledgementObserver, requestToken,
      ::content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction::
          ContentAnalysisAcknowledgement_FinalAction_ALLOW);
}

TEST_F(
    ContentAnalysisTest,
    CheckBrowserReportsTimeoutWithDefaultTimeoutWarnAndUserAllowAfterAgentResponse) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we respect the timeout_result
  // pref.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutResultPref, 1));
  auto ignore = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutResultPref));
  });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allowMe");
  nsCString requestToken = GenerateUUID();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });

  SendRequestAndExpectWarnResponse(
      mContentAnalysis, request, WarnDialogResponse::Allow,
      WaitForAgentResponseToRespondToWarn::Yes, AutoAcknowledge::Yes);

  WaitForTooLateAcknowledgement(
      rawAcknowledgementObserver, requestToken,
      ::content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction::
          ContentAnalysisAcknowledgement_FinalAction_ALLOW);
}

TEST_F(ContentAnalysisTest,
       CheckBrowserReportsTimeoutWithDefaultTimeoutWarnAndUserBlock) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we respect the timeout_result
  // pref.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutResultPref, 1));
  auto ignore = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutResultPref));
  });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allowMe");
  nsCString requestToken = GenerateUUID();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });

  SendRequestAndExpectWarnResponse(
      mContentAnalysis, request, WarnDialogResponse::Block,
      WaitForAgentResponseToRespondToWarn::No, AutoAcknowledge::Yes);

  WaitForTooLateAcknowledgement(
      rawAcknowledgementObserver, requestToken,
      ::content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction::
          ContentAnalysisAcknowledgement_FinalAction_BLOCK);
}

TEST_F(
    ContentAnalysisTest,
    CheckBrowserReportsTimeoutWithDefaultTimeoutWarnAndUserBlockAfterAgentResponse) {
  // Submit a request to the agent and then timeout before we get a response.
  // When we do get a response later, check that we respect the timeout_result
  // pref.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, -1));
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutResultPref, 1));
  auto ignore = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref));
    MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutResultPref));
  });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allowMe");
  nsCString requestToken = GenerateUUID();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  MOZ_ALWAYS_SUCCEEDS(request->SetRequestToken(requestToken));
  // Make sure that, if the timeout happens before the agent thread submits
  // the request, we don't skip the submission.
  MOZ_ALWAYS_SUCCEEDS(
      request->SetTestOnlyIgnoreCanceledAndAlwaysSubmitToAgent(true));

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawAcknowledgementObserver = MakeRefPtr<RawAcknowledgementObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(
      rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(
        rawAcknowledgementObserver, "dlp-acknowledgement-sent-raw"));
  });

  SendRequestAndExpectWarnResponse(
      mContentAnalysis, request, WarnDialogResponse::Block,
      WaitForAgentResponseToRespondToWarn::Yes, AutoAcknowledge::Yes);

  WaitForTooLateAcknowledgement(
      rawAcknowledgementObserver, requestToken,
      ::content_analysis::sdk::ContentAnalysisAcknowledgement_FinalAction::
          ContentAnalysisAcknowledgement_FinalAction_BLOCK);
}

TEST_F(ContentAnalysisTest,
       SendMultipleBatchFilesToAgent_GetResponsesAndCheckTimeouts) {
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetInt(kTimeoutPref, 65));
  auto ignore = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kTimeoutPref)); });
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsCOMPtr<nsIFile> blockFile = GetFileFromLocalDirectory(L"blockedFile.txt");
  nsCOMPtr<nsIFile> allowFile = GetFileFromLocalDirectory(L"allowedFile.txt");
  nsCOMArray<nsIFile> files;
  files.AppendElement(blockFile);
  files.AppendElement(allowFile);

  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  std::atomic<bool> gotResponse = false;

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(mContentAnalysis);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));
  auto ignore2 = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(
        obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
  });
  time_t now = time(nullptr);

  auto promise = ContentAnalysis::CheckFilesInBatchMode(
      std::move(files), nullptr,
      nsIContentAnalysisRequest::Reason::eFilePickerDialog, uri);
  promise->Then(
      mozilla::GetMainThreadSerialEventTarget(), __func__,
      [&, timedOut](nsCOMArray<nsIFile> aAllowedFiles) {
        if (timedOut->mValue) {
          return;
        }
        EXPECT_EQ(1, aAllowedFiles.Count());
        nsString allowedLeafName;
        EXPECT_EQ(NS_OK, aAllowedFiles[0]->GetLeafName(allowedLeafName));
        EXPECT_EQ(nsString(L"allowedFile.txt"), allowedLeafName);
        gotResponse = true;
      },
      [&gotResponse, timedOut](nsresult error) {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        gotResponse = true;
        FAIL() << "Got error response";
      });

  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis results"_ns,
      [&, timedOut]() { return gotResponse.load() || timedOut->mValue; });
  timer->Cancel();
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(timedOut->mValue);

  auto requests = rawRequestObserver->GetRequests();
  EXPECT_EQ(static_cast<size_t>(2), requests.size());
  // There should be around 65*2 seconds remaining for each request
  time_t t = requests[0].expires_at();
  time_t secs_remaining = t - now;
  EXPECT_LE(abs(secs_remaining - (65 * 2)), 8);
  t = requests[1].expires_at();
  secs_remaining = t - now;
  EXPECT_LE(abs(secs_remaining - (65 * 2)), 8);
}

TEST_F(ContentAnalysisTest,
       SendMultipartRequestThenCancel_CheckAgentIsNotContacted) {
  // Sets the request thread pool to handle 2 simultaneous requests,
  // sends 3 requests, and cancels after the first request is generated but
  // before it is sent.
  // All three requests will be queued to the thread pool (this is not
  // independently checked) but none will be submitted to the agent.  We confirm
  // that the requests were not submitted to the agent by checking that the
  // callback is alerted, the dlp-request-sent-raw messages were received,
  // no dlp-response-received-raw has been received, and the service is not
  // expecting any responses from the agent for the canceled user action.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetUint(kMaxConnections, 2));
  auto removePref = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kMaxConnections)); });

  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();

  const wchar_t* texts[] = {L"string1", L"string2", L"string3"};
  AutoTArray<RefPtr<nsIContentAnalysisRequest>, 3> requests;
  for (auto& text : texts) {
    requests.AppendElement(new ContentAnalysisRequest(
        nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
        nsIContentAnalysisRequest::Reason::eClipboardPaste, nsString(text),
        false /* isFilePath */, EmptyCString() /* sha1 */, uri,
        nsIContentAnalysisRequest::OperationType::eClipboard, nullptr));
  }

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  auto rawRequestObserver = MakeRefPtr<RawRequestObserver>(
      mContentAnalysis, true /* aCancelOnFirstRequest */);
  MOZ_ALWAYS_SUCCEEDS(
      obsServ->AddObserver(rawRequestObserver, "dlp-request-sent-raw", false));
  auto removeRequestSent = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(
        obsServ->RemoveObserver(rawRequestObserver, "dlp-request-sent-raw"));
  });

  auto rawResponseObserver = MakeRefPtr<RawAgentResponseObserver>();
  MOZ_ALWAYS_SUCCEEDS(obsServ->AddObserver(rawResponseObserver,
                                           "dlp-response-received-raw", false));
  auto removeResponseSent = MakeScopeExit([&] {
    MOZ_ALWAYS_SUCCEEDS(obsServ->RemoveObserver(rawResponseObserver,
                                                "dlp-response-received-raw"));
  });

  bool gotResponse = false;
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);
  SendRequestsAndExpectNoAgentResponseNoAwait(
      mContentAnalysis, requests, false /* expectShouldAllow */,
      nsIContentAnalysisResponse::CancelError::
          eOtherRequestInGroupCancelled /* expectedCancelError */,
      &gotResponse, timedOut);

  nsAutoCString userActionId;
  MOZ_ALWAYS_SUCCEEDS(requests[0]->GetUserActionId(userActionId));
  EXPECT_TRUE(!userActionId.IsEmpty());

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis result"_ns, [&, timedOut]() {
        return (gotResponse && !HasOutstandingCanceledRequests(userActionId)) ||
               timedOut->mValue;
      });
  timer->Cancel();
  EXPECT_FALSE(timedOut->mValue);
  EXPECT_TRUE(gotResponse);
  EXPECT_FALSE(HasOutstandingCanceledRequests(userActionId));
  EXPECT_EQ(3ull, rawRequestObserver->GetRequests().size());
  EXPECT_EQ(0ull, rawResponseObserver->GetResponses().size());
}

TEST_F(
    ContentAnalysisTest,
    SendBatchFileRequestThenCancelOneAndItsAssociatedRequests_CheckAllAreCanceled) {
  // Sets the request thread pool to handle 2 simultaneous requests,
  // sends 3 file requests, and cancels one at random before CA could
  // process any responses, or even send them to the agent.
  MOZ_ALWAYS_SUCCEEDS(Preferences::SetUint(kMaxConnections, 2));
  auto removePref = MakeScopeExit(
      [&] { MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kMaxConnections)); });

  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();

  nsCOMPtr<nsIFile> allowFile = GetFileFromLocalDirectory(L"allowedFile.txt");
  nsCOMArray<nsIFile> files;
  files.AppendElement(allowFile);
  files.AppendElement(allowFile);
  files.AppendElement(allowFile);

  bool gotResponse = false;
  RefPtr timedOut = MakeRefPtr<media::Refcountable<BoolStruct>>();
  RefPtr<CancelableRunnable> timer = QueueTimeoutToMainThread(timedOut);

  auto promise = ContentAnalysis::CheckFilesInBatchMode(
      std::move(files), nullptr,
      nsIContentAnalysisRequest::Reason::eFilePickerDialog, uri);
  promise->Then(
      mozilla::GetMainThreadSerialEventTarget(), __func__,
      [&, timedOut](nsCOMArray<nsIFile> aAllowedFiles) {
        if (timedOut->mValue) {
          return;
        }
        EXPECT_EQ(0, aAllowedFiles.Count());
        gotResponse = true;
      },
      [&gotResponse, timedOut](nsresult error) {
        if (timedOut->mValue) {
          return;
        }
        const char* errorName = mozilla::GetStaticErrorName(error);
        errorName = errorName ? errorName : "";
        printf("Got error response code %s(%x)\n", errorName, error);
        // Errors should not have errorCode NS_OK
        EXPECT_NE(NS_OK, error);
        gotResponse = true;
        FAIL() << "Got error response";
      });

  auto* compoundActions = GetCompoundUserActions();
  MOZ_ASSERT(compoundActions);
  EXPECT_EQ(compoundActions->count(), 1u);
  if (!compoundActions->empty()) {
    const auto& compoundActionIds = compoundActions->iter().get();
    EXPECT_EQ(compoundActionIds->count(), 3u);
    if (!compoundActionIds->empty()) {
      nsAutoCString userActionId(compoundActionIds->iter().get());
      MOZ_ALWAYS_SUCCEEDS(
          CancelAllRequestsAssociatedWithUserAction(userActionId));
    }
  }

  mozilla::SpinEventLoopUntil(
      "Waiting for ContentAnalysis cancel"_ns,
      [&, timedOut]() { return gotResponse || timedOut->mValue; });
  timer->Cancel();
  EXPECT_FALSE(timedOut->mValue);
  EXPECT_TRUE(gotResponse);
}

TEST_F(ContentAnalysisTest, GetDiagnosticInfo_Initial) {
  RefPtr<ContentAnalysisDiagnosticInfo> info =
      GetDiagnosticInfo(mContentAnalysis);
  EXPECT_TRUE(info->GetConnectedToAgent());
  EXPECT_FALSE(info->GetFailedSignatureVerification());
  nsString agentPath;
  MOZ_ALWAYS_SUCCEEDS(info->GetAgentPath(agentPath));
  int32_t index = agentPath.Find(u"content_analysis_sdk_agent.exe");
  EXPECT_EQ(agentPath.Length() - (sizeof("content_analysis_sdk_agent.exe") - 1),
            static_cast<size_t>(index));
  EXPECT_GE(info->GetRequestCount(), 0);
}

TEST_F(ContentAnalysisTest,
       GetDiagnosticInfo_AfterAgentTerminateAndOneRequest) {
  mAgentInfo.TerminateProcess();

  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectNoAgentResponse(mContentAnalysis, request);

  RefPtr<ContentAnalysisDiagnosticInfo> info =
      GetDiagnosticInfo(mContentAnalysis);
  EXPECT_FALSE(info->GetConnectedToAgent());
  EXPECT_FALSE(info->GetFailedSignatureVerification());
  nsString agentPath;
  MOZ_ALWAYS_SUCCEEDS(info->GetAgentPath(agentPath));
  EXPECT_TRUE(agentPath.IsEmpty());
  EXPECT_GE(info->GetRequestCount(), 0);

  StartAgent();
  SendSimpleRequestAndWaitForResponse();
}

TEST_F(ContentAnalysisTest, GetDiagnosticInfo_AfterAgentTerminateAndReconnect) {
  mAgentInfo.TerminateProcess();
  StartAgent();

  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Nothing());

  RefPtr<ContentAnalysisDiagnosticInfo> info =
      GetDiagnosticInfo(mContentAnalysis);
  EXPECT_TRUE(info->GetConnectedToAgent());
  EXPECT_FALSE(info->GetFailedSignatureVerification());
  nsString agentPath;
  MOZ_ALWAYS_SUCCEEDS(info->GetAgentPath(agentPath));
  int32_t index = agentPath.Find(u"content_analysis_sdk_agent.exe");
  EXPECT_EQ(agentPath.Length() - (sizeof("content_analysis_sdk_agent.exe") - 1),
            static_cast<size_t>(index));
  EXPECT_GE(info->GetRequestCount(), 0);
}

TEST_F(ContentAnalysisTest, GetDiagnosticInfo_RequestCountIncreases) {
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  RefPtr<ContentAnalysisDiagnosticInfo> info =
      GetDiagnosticInfo(mContentAnalysis);
  int64_t firstRequestCount = info->GetRequestCount();
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectResponse(mContentAnalysis, request, Some(true),
                               Some(nsIContentAnalysisResponse::eAllow),
                               Nothing());

  info = GetDiagnosticInfo(mContentAnalysis);
  EXPECT_EQ(firstRequestCount + 1, info->GetRequestCount());
}

TEST_F(ContentAnalysisTest, GetDiagnosticInfo_FailedSignatureVerification) {
  MOZ_ALWAYS_SUCCEEDS(
      Preferences::SetCString(kClientSignaturePref, "anInvalidSignature"));
  mAgentInfo.TerminateProcess();
  StartAgent();
  nsCOMPtr<nsIURI> uri = GetExampleDotComURI();
  nsString allow(L"allow");
  nsCOMPtr<nsIContentAnalysisRequest> request = new ContentAnalysisRequest(
      nsIContentAnalysisRequest::AnalysisType::eBulkDataEntry,
      nsIContentAnalysisRequest::Reason::eClipboardPaste, std::move(allow),
      false, EmptyCString(), uri,
      nsIContentAnalysisRequest::OperationType::eClipboard, nullptr);
  SendRequestAndExpectNoAgentResponse(
      mContentAnalysis, request, false,
      nsIContentAnalysisResponse::CancelError::eInvalidAgentSignature);

  RefPtr<ContentAnalysisDiagnosticInfo> info =
      GetDiagnosticInfo(mContentAnalysis);
  EXPECT_FALSE(info->GetConnectedToAgent());
  EXPECT_TRUE(info->GetFailedSignatureVerification());

  MOZ_ALWAYS_SUCCEEDS(Preferences::ClearUser(kClientSignaturePref));
  // Reset the agent so it's working for future tests
  mAgentInfo.TerminateProcess();
  StartAgent();
  SendSimpleRequestAndWaitForResponse();
}
