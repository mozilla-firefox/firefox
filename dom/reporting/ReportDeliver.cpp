/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ReportDeliver.h"

#include <algorithm>

#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/EndpointForReportChild.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReportingBinding.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsGlobalWindowInner.h"
#include "nsIGlobalObject.h"
#include "nsIXPConnect.h"
#include "nsNetUtil.h"
#include "nsStringStream.h"

namespace mozilla::dom {

namespace {

StaticRefPtr<ReportDeliver> gReportDeliver;

// This is the same value as the default value of
// dom.min_timeout_value, so it's not that random.
constexpr double gMinReportAgeInMs = 4.0;

class ReportFetchHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit ReportFetchHandler(
      const nsTArray<ReportDeliver::ReportData>& aReportData)
      : mReports(aReportData.Clone()) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    if (!gReportDeliver) {
      return;
    }

    if (NS_WARN_IF(!aValue.isObject())) {
      return;
    }

    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
    MOZ_ASSERT(obj);

    {
      Response* response = nullptr;
      if (NS_WARN_IF(NS_FAILED(UNWRAP_OBJECT(Response, &obj, response)))) {
        return;
      }

      if (response->Status() == 410) {
        mozilla::ipc::PBackgroundChild* actorChild =
            mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();

        for (const auto& report : mReports) {
          mozilla::ipc::PrincipalInfo principalInfo;
          nsresult rv =
              PrincipalToPrincipalInfo(report.mPrincipal, &principalInfo);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            continue;
          }

          actorChild->SendRemoveEndpoint(report.mGroupName, report.mEndpointURL,
                                         principalInfo);
        }
      }
    }
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    if (gReportDeliver) {
      for (auto& report : mReports) {
        ++report.mFailures;
        gReportDeliver->AppendReportData(report);
      }
    }
  }

 private:
  ~ReportFetchHandler() = default;

  nsTArray<ReportDeliver::ReportData> mReports;
};

NS_IMPL_ISUPPORTS0(ReportFetchHandler)

class ReportJSONWriter final : public JSONWriter {
 public:
  explicit ReportJSONWriter(JSONStringWriteFunc<nsAutoCString>& aOutput)
      : JSONWriter(aOutput) {}

  void JSONProperty(const Span<const char>& aProperty,
                    const Span<const char>& aJSON) {
    Separator();
    PropertyNameAndColon(aProperty);
    mWriter.Write(aJSON);
  }
};

void SendReports(nsTArray<ReportDeliver::ReportData>& aReports,
                 const nsCString& aEndPointUrl, nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(aReports.IsEmpty())) {
    return;
  }

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  MOZ_ASSERT(xpc, "This should never be null!");

  nsCOMPtr<nsIGlobalObject> globalObject;
  {
    AutoJSAPI jsapi;
    jsapi.Init();

    JSContext* cx = jsapi.cx();
    JS::Rooted<JSObject*> sandbox(cx);
    nsresult rv = xpc->CreateSandbox(cx, aPrincipal, sandbox.address());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    // The JSContext is not in a realm, so CreateSandbox returned an unwrapped
    // global.
    MOZ_ASSERT(JS_IsGlobalObject(sandbox));

    globalObject = xpc::NativeGlobal(sandbox);
  }

  if (NS_WARN_IF(!globalObject)) {
    return;
  }

  // The body
  JSONStringWriteFunc<nsAutoCString> body;
  ReportJSONWriter w(body);

  w.StartArrayElement();
  for (const auto& report : aReports) {
    MOZ_ASSERT(report.mPrincipal == aPrincipal);
    MOZ_ASSERT(report.mEndpointURL == aEndPointUrl);
    w.StartObjectElement();
    // It looks like in rare cases, TimeStamp::Now() may be the same
    // as report.mCreationTime, so we introduce a constant number to
    // make sure "age" is always not 0.
    w.IntProperty(
        "age",
        std::max((TimeStamp::Now() - report.mCreationTime).ToMilliseconds(),
                 gMinReportAgeInMs));
    w.StringProperty("type", NS_ConvertUTF16toUTF8(report.mType));
    w.StringProperty("url", NS_ConvertUTF16toUTF8(report.mURL));
    w.StringProperty("user_agent", NS_ConvertUTF16toUTF8(report.mUserAgent));
    w.JSONProperty(MakeStringSpan("body"),
                   Span<const char>(report.mReportBodyJSON.Data(),
                                    report.mReportBodyJSON.Length()));
    w.EndObject();
  }
  w.EndArray();

  // The body as stream
  nsCOMPtr<nsIInputStream> streamBody;
  nsresult rv =
      NS_NewCStringInputStream(getter_AddRefs(streamBody), body.StringCRef());

  // Headers
  IgnoredErrorResult error;
  RefPtr<InternalHeaders> internalHeaders =
      new InternalHeaders(HeadersGuardEnum::Request);
  internalHeaders->Set("Content-Type"_ns, "application/reports+json"_ns, error);
  if (NS_WARN_IF(error.Failed())) {
    return;
  }

  // URL and fragments
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aEndPointUrl);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsCOMPtr<nsIURI> uriClone;
  rv = NS_GetURIWithoutRef(uri, getter_AddRefs(uriClone));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsAutoCString uriSpec;
  rv = uriClone->GetSpec(uriSpec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsAutoCString uriFragment;
  rv = uri->GetRef(uriFragment);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  auto internalRequest = MakeSafeRefPtr<InternalRequest>(uriSpec, uriFragment);

  internalRequest->SetMethod("POST"_ns);
  internalRequest->SetBody(streamBody, body.StringCRef().Length());
  internalRequest->SetHeaders(internalHeaders);
  internalRequest->SetSkipServiceWorker();
  // TODO: internalRequest->SetContentPolicyType(TYPE_REPORT);
  internalRequest->SetMode(RequestMode::Cors);
  internalRequest->SetCredentialsMode(RequestCredentials::Same_origin);

  RefPtr<Request> request =
      new Request(globalObject, std::move(internalRequest), nullptr);

  RequestOrUTF8String fetchInput;
  fetchInput.SetAsRequest() = request;

  RootedDictionary<RequestInit> requestInit(RootingCx());
  RefPtr<Promise> promise = FetchRequest(globalObject, fetchInput, requestInit,
                                         CallerType::NonSystem, error);
  if (error.Failed()) {
    for (auto& report : aReports) {
      ++report.mFailures;
      if (gReportDeliver) {
        gReportDeliver->AppendReportData(report);
      }
    }
    return;
  }

  RefPtr<ReportFetchHandler> handler = new ReportFetchHandler(aReports);
  promise->AppendNativeHandler(handler);
}

}  // namespace

/* static */
void ReportDeliver::Record(nsPIDOMWindowInner* aWindow, const nsAString& aType,
                           const nsAString& aGroupName, const nsAString& aURL,
                           ReportBody* aBody) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aBody);

  nsString reportBodyString;
  aBody->ToJSON(reportBodyString);

  nsCOMPtr<nsIPrincipal> principal =
      nsGlobalWindowInner::Cast(aWindow)->GetPrincipal();
  if (NS_WARN_IF(!principal)) {
    return;
  }

  mozilla::ipc::PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(principal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  mozilla::ipc::PBackgroundChild* actorChild =
      mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();

  PEndpointForReportChild* actor =
      actorChild->SendPEndpointForReportConstructor(nsString(aGroupName),
                                                    principalInfo);
  if (NS_WARN_IF(!actor)) {
    return;
  }

  ReportData data;
  data.mType = aType;
  data.mGroupName = aGroupName;
  data.mURL = aURL;
  data.mCreationTime = TimeStamp::Now();
  data.mReportBodyJSON = NS_ConvertUTF16toUTF8(reportBodyString).get();
  data.mPrincipal = principal;
  data.mFailures = 0;

  Navigator* navigator = aWindow->Navigator();
  MOZ_ASSERT(navigator);

  IgnoredErrorResult error;
  navigator->GetUserAgent(data.mUserAgent, CallerType::NonSystem, error);
  if (NS_WARN_IF(error.Failed())) {
    return;
  }

  static_cast<EndpointForReportChild*>(actor)->Initialize(data);
}

/* static */
void ReportDeliver::Fetch(const ReportData& aReportData) {
  if (!gReportDeliver) {
    RefPtr<ReportDeliver> rd = new ReportDeliver();

    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (NS_WARN_IF(!obs)) {
      return;
    }

    obs->AddObserver(rd, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
    gReportDeliver = rd;
  }

  gReportDeliver->AppendReportData(aReportData);
}

void ReportDeliver::AppendReportData(const ReportData& aReportData) {
  if (aReportData.mFailures >
      StaticPrefs::dom_reporting_delivering_maxFailures()) {
    return;
  }

  if (NS_WARN_IF(!mReportQueue.AppendElement(aReportData, fallible))) {
    return;
  }

  while (mReportQueue.Length() >
         StaticPrefs::dom_reporting_delivering_maxReports()) {
    mReportQueue.RemoveElementAt(0);
  }

  RefPtr<ReportDeliver> self{this};
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "ReportDeliver::CallNotify", [self]() { self->Notify(); });

  NS_DispatchToCurrentThreadQueue(
      runnable.forget(), StaticPrefs::dom_reporting_delivering_timeout() * 1000,
      EventQueuePriority::Idle);
}

void ReportDeliver::Notify() {
  nsTArray<ReportData> reports = std::move(mReportQueue);

  // group reports by endpoint and nsIPrincipal
  std::map<std::pair<nsCString, nsCOMPtr<nsIPrincipal>>, nsTArray<ReportData>>
      reportsByPrincipal;
  for (ReportData& report : reports) {
    auto already_seen =
        reportsByPrincipal.find({report.mEndpointURL, report.mPrincipal});
    if (already_seen == reportsByPrincipal.end()) {
      reportsByPrincipal.emplace(
          std::make_pair(report.mEndpointURL, report.mPrincipal),
          nsTArray<ReportData>({report}));
    } else {
      already_seen->second.AppendElement(report);
    }
  }

  for (auto& iter : reportsByPrincipal) {
    std::pair<nsCString, nsCOMPtr<nsIPrincipal>> key = iter.first;
    nsTArray<ReportData>& value = iter.second;
    nsCString url = key.first;
    nsCOMPtr<nsIPrincipal> principal = key.second;
    nsAutoCString u(url);
    SendReports(value, url, principal);
  }
}

NS_IMETHODIMP
ReportDeliver::GetName(nsACString& aName) {
  aName.AssignLiteral("ReportDeliver");
  return NS_OK;
}

NS_IMETHODIMP
ReportDeliver::Observe(nsISupports* aSubject, const char* aTopic,
                       const char16_t* aData) {
  MOZ_ASSERT(!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID));

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return NS_OK;
  }

  obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);

  gReportDeliver = nullptr;
  return NS_OK;
}

ReportDeliver::ReportDeliver() = default;

ReportDeliver::~ReportDeliver() = default;

NS_INTERFACE_MAP_BEGIN(ReportDeliver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(ReportDeliver)
NS_IMPL_RELEASE(ReportDeliver)

}  // namespace mozilla::dom
