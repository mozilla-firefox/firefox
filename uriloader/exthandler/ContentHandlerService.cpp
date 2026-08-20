/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentHandlerService.h"
#include "HandlerServiceChild.h"
#include "ContentChild.h"
#include "nsIMutableArray.h"
#include "nsIMIMEInfo.h"
#include "nsIStringEnumerator.h"
#include "nsReadableUtils.h"
#include "nsMIMEInfoImpl.h"
#include "nsMIMEInfoChild.h"

using mozilla::dom::ContentChild;
using mozilla::dom::HandlerInfo;
using mozilla::dom::PHandlerServiceChild;

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS(ContentHandlerService, nsIHandlerService)

ContentHandlerService::ContentHandlerService() = default;

/* static */ already_AddRefed<nsIHandlerService>
ContentHandlerService::Create() {
  if (XRE_IsContentProcess()) {
    RefPtr service = new ContentHandlerService();
    if (NS_SUCCEEDED(service->Init())) {
      return service.forget();
    }
    return nullptr;
  }

  nsCOMPtr<nsIHandlerService> service =
      do_GetService("@mozilla.org/uriloader/handler-service-parent;1");
  return service.forget();
}

nsresult ContentHandlerService::Init() {
  if (!XRE_IsContentProcess()) {
    return NS_ERROR_FAILURE;
  }
  ContentChild* cpc = ContentChild::GetSingleton();

  mHandlerServiceChild = new HandlerServiceChild();
  if (!cpc->SendPHandlerServiceConstructor(mHandlerServiceChild)) {
    mHandlerServiceChild = nullptr;
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

void ContentHandlerService::nsIHandlerInfoToHandlerInfo(
    nsIHandlerInfo* aInfo, HandlerInfo* aHandlerInfo) {
  nsCString type;
  aInfo->GetType(type);
  nsCOMPtr<nsIMIMEInfo> mimeInfo = do_QueryInterface(aInfo);
  bool isMIMEInfo = !!mimeInfo;
  nsString description;
  aInfo->GetDescription(description);
  bool alwaysAskBeforeHandling;
  aInfo->GetAlwaysAskBeforeHandling(&alwaysAskBeforeHandling);
  nsCOMPtr<nsIHandlerApp> app;
  aInfo->GetPreferredApplicationHandler(getter_AddRefs(app));
  nsString name;
  nsString detailedDescription;
  if (app) {
    app->GetName(name);
    app->GetDetailedDescription(detailedDescription);
  }
  HandlerApp happ(name, detailedDescription);
  nsTArray<HandlerApp> happs;
  nsCOMPtr<nsIMutableArray> apps;
  aInfo->GetPossibleApplicationHandlers(getter_AddRefs(apps));
  if (apps) {
    unsigned int length;
    apps->GetLength(&length);
    for (unsigned int i = 0; i < length; i++) {
      apps->QueryElementAt(i, NS_GET_IID(nsIHandlerApp), getter_AddRefs(app));
      app->GetName(name);
      app->GetDetailedDescription(detailedDescription);
      happs.AppendElement(HandlerApp(name, detailedDescription));
    }
  }

  nsTArray<nsCString> extensions;

  if (isMIMEInfo) {
    nsCOMPtr<nsIUTF8StringEnumerator> extensionsIter;
    mimeInfo->GetFileExtensions(getter_AddRefs(extensionsIter));
    if (extensionsIter) {
      bool hasMore = false;
      while (NS_SUCCEEDED(extensionsIter->HasMore(&hasMore)) && hasMore) {
        nsAutoCString extension;
        if (NS_SUCCEEDED(extensionsIter->GetNext(extension))) {
          extensions.AppendElement(std::move(extension));
        }
      }
    }
  }

  nsHandlerInfoAction action;
  aInfo->GetPreferredAction(&action);
  *aHandlerInfo =
      HandlerInfo(type, isMIMEInfo, description, alwaysAskBeforeHandling,
                  std::move(extensions), happ, happs, action);
}

NS_IMETHODIMP RemoteHandlerApp::GetName(nsAString& aName) {
  aName.Assign(mAppChild.name());
  return NS_OK;
}

NS_IMETHODIMP RemoteHandlerApp::SetName(const nsAString& aName) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP RemoteHandlerApp::GetDetailedDescription(
    nsAString& aDetailedDescription) {
  aDetailedDescription.Assign(mAppChild.detailedDescription());
  return NS_OK;
}

NS_IMETHODIMP RemoteHandlerApp::SetDetailedDescription(
    const nsAString& aDetailedDescription) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP RemoteHandlerApp::Equals(nsIHandlerApp* aHandlerApp,
                                       bool* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP RemoteHandlerApp::LaunchWithURI(
    nsIURI* aURI, BrowsingContext* aBrowsingContext) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMPL_ISUPPORTS(RemoteHandlerApp, nsIHandlerApp)

static inline void CopyHandlerInfoTonsIHandlerInfo(
    const HandlerInfo& info, nsIHandlerInfo* aHandlerInfo) {
  const HandlerApp& preferredApplicationHandler =
      info.preferredApplicationHandler();
  nsCOMPtr<nsIHandlerApp> preferredApp(
      new RemoteHandlerApp(preferredApplicationHandler));
  aHandlerInfo->SetPreferredApplicationHandler(preferredApp);
  nsCOMPtr<nsIMutableArray> possibleHandlers;
  aHandlerInfo->GetPossibleApplicationHandlers(
      getter_AddRefs(possibleHandlers));
  possibleHandlers->AppendElement(preferredApp);

  aHandlerInfo->SetPreferredAction(info.preferredAction());
  aHandlerInfo->SetAlwaysAskBeforeHandling(info.alwaysAskBeforeHandling());

  if (info.isMIMEInfo()) {
    nsCOMPtr<nsIMIMEInfo> mimeInfo(do_QueryInterface(aHandlerInfo));
    MOZ_ASSERT(mimeInfo,
               "parent and child don't agree on whether this is a MIME info");
    mimeInfo->SetFileExtensions(StringJoin(","_ns, info.extensions()));
  }
}

ContentHandlerService::~ContentHandlerService() = default;

NS_IMETHODIMP ContentHandlerService::AsyncInit() {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP ContentHandlerService::Enumerate(nsISimpleEnumerator** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP ContentHandlerService::FillHandlerInfo(
    nsIHandlerInfo* aHandlerInfo, const nsACString& aOverrideType) {
  HandlerInfo info, returnedInfo;
  nsIHandlerInfoToHandlerInfo(aHandlerInfo, &info);
  mHandlerServiceChild->SendFillHandlerInfo(info, aOverrideType, &returnedInfo);
  CopyHandlerInfoTonsIHandlerInfo(returnedInfo, aHandlerInfo);
  return NS_OK;
}

NS_IMETHODIMP ContentHandlerService::GetMIMEInfoFromOS(
    const nsACString& aMIMEType, const nsACString& aFileExt, bool* aFound,
    nsIMIMEInfo** aMIMEInfo) {
  nsresult rv = NS_ERROR_FAILURE;
  HandlerInfo returnedInfo;
  if (!mHandlerServiceChild->SendGetMIMEInfoFromOS(aMIMEType, aFileExt, &rv,
                                                   &returnedInfo, aFound)) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr mimeInfo = MakeRefPtr<nsChildProcessMIMEInfo>(returnedInfo.type());
  CopyHandlerInfoTonsIHandlerInfo(returnedInfo, mimeInfo);
  mimeInfo.forget(aMIMEInfo);
  return NS_OK;
}

NS_IMETHODIMP ContentHandlerService::Store(nsIHandlerInfo* aHandlerInfo) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP ContentHandlerService::Exists(nsIHandlerInfo* aHandlerInfo,
                                            bool* _retval) {
  HandlerInfo info;
  nsIHandlerInfoToHandlerInfo(aHandlerInfo, &info);
  mHandlerServiceChild->SendExists(info, _retval);
  return NS_OK;
}

NS_IMETHODIMP ContentHandlerService::Remove(nsIHandlerInfo* aHandlerInfo) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
ContentHandlerService::ExistsForProtocolOS(const nsACString& aProtocolScheme,
                                           bool* aRetval) {
  if (!mHandlerServiceChild->SendExistsForProtocolOS(aProtocolScheme,
                                                     aRetval)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
ContentHandlerService::ExistsForProtocol(const nsACString& aProtocolScheme,
                                         bool* aRetval) {
  if (!mHandlerServiceChild->SendExistsForProtocol(aProtocolScheme, aRetval)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP ContentHandlerService::GetTypeFromExtension(
    const nsACString& aFileExtension, nsACString& _retval) {
  _retval.Assign(*mExtToTypeMap.LookupOrInsertWith(aFileExtension, [&] {
    nsCString type;
    mHandlerServiceChild->SendGetTypeFromExtension(aFileExtension, &type);
    return MakeUnique<nsCString>(type);
  }));
  return NS_OK;
}

NS_IMETHODIMP ContentHandlerService::GetApplicationDescription(
    const nsACString& aProtocolScheme, nsAString& aRetVal) {
  nsresult rv = NS_ERROR_FAILURE;
  nsAutoCString scheme(aProtocolScheme);
  nsAutoString desc;
  mHandlerServiceChild->SendGetApplicationDescription(scheme, &rv, &desc);
  aRetVal.Assign(desc);
  return rv;
}

}  // namespace dom
}  // namespace mozilla
