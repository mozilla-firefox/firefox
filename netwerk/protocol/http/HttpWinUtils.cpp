/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HttpWinUtils.h"

#include <proofofpossessioncookieinfo.h>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "nsHttpChannel.h"
#include "nsIURI.h"
#include "xpcpublic.h"

namespace mozilla {
namespace net {

static StaticRefPtr<IProofOfPossessionCookieInfoManager> sPopCookieManager;
static bool sPopCookieManagerAvailable = true;

struct SSOCredential {
  nsString mName;
  nsString mData;
};

// In automation, network.http.windows-sso.credentials-for-testing can supply
// credentials as "name=data|name=data" instead of asking Windows.
static bool GetTestingCredentials(nsTArray<SSOCredential>& aCredentials) {
  if (!xpc::IsInAutomation()) {
    return false;
  }
  nsAutoCString pref;
  if (NS_FAILED(Preferences::GetCString(
          "network.http.windows-sso.credentials-for-testing", pref)) ||
      pref.IsEmpty()) {
    return false;
  }
  for (const nsACString& entry : pref.Split('|')) {
    int32_t equals = entry.FindChar('=');
    if (equals <= 0) {
      continue;
    }
    aCredentials.AppendElement(
        SSOCredential{NS_ConvertUTF8toUTF16(Substring(entry, 0, equals)),
                      NS_ConvertUTF8toUTF16(Substring(entry, equals + 1))});
  }
  return true;
}

static bool GetWindowsCredentials(nsIURI* aURI,
                                  nsTArray<SSOCredential>& aCredentials) {
  if (!sPopCookieManagerAvailable) {
    MOZ_LOG(gHttpLog, mozilla::LogLevel::Debug,
            ("HttpWinUtils::AddWindowsSSO: sPopCookieManager is not available "
             "from the beginning"));
    return false;
  }
  HRESULT hr;
  if (!sPopCookieManager) {
    GUID CLSID_ProofOfPossessionCookieInfoManager;
    GUID IID_IProofOfPossessionCookieInfoManager;

    CLSIDFromString(L"{A9927F85-A304-4390-8B23-A75F1C668600}",
                    &CLSID_ProofOfPossessionCookieInfoManager);
    IIDFromString(L"{CDAECE56-4EDF-43DF-B113-88E4556FA1BB}",
                  &IID_IProofOfPossessionCookieInfoManager);

    hr = CoCreateInstance(CLSID_ProofOfPossessionCookieInfoManager, NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_IProofOfPossessionCookieInfoManager,
                          reinterpret_cast<void**>(&sPopCookieManager));
    if (FAILED(hr)) {
      sPopCookieManagerAvailable = false;
      MOZ_LOG(
          gHttpLog, mozilla::LogLevel::Debug,
          ("HttpWinUtils::AddWindowsSSO: sPopCookieManager is not available"));
      return false;
    }

    RunOnShutdown([&] {
      if (sPopCookieManager) {
        sPopCookieManager = nullptr;
      }
    });
  }

  DWORD cookieCount = 0;
  ProofOfPossessionCookieInfo* cookieInfo = nullptr;

  nsAutoCString urispec;
  aURI->GetSpec(urispec);

  hr = sPopCookieManager->GetCookieInfoForUri(
      NS_ConvertUTF8toUTF16(urispec).get(), &cookieCount, &cookieInfo);
  if (FAILED(hr)) {
    MOZ_LOG(gHttpLog, mozilla::LogLevel::Debug,
            ("HttpWinUtils::AddWindowsSSO: GetCookieInfoForUri failed"));
    return false;
  }

  for (DWORD i = 0; i < cookieCount; i++) {
    SSOCredential* credential = aCredentials.AppendElement();
    credential->mName.Assign(cookieInfo[i].name);
    credential->mData.Assign(cookieInfo[i].data);
  }
  if (cookieInfo) {
    FreeProofOfPossessionCookieInfoArray(cookieInfo, cookieCount);
  }
  return true;
}

void AddWindowsSSO(nsHttpChannel* channel, bool aDeviceOnly) {
  nsCOMPtr<nsIURI> uri;
  channel->GetURI(getter_AddRefs(uri));

  nsTArray<SSOCredential> credentials;
  if (!GetTestingCredentials(credentials) &&
      !GetWindowsCredentials(uri, credentials)) {
    return;
  }

  nsAutoCString host;
  uri->GetHost(host);
  bool addCookies = false;
  if (StringEndsWith(host, ".live.com"_ns)) {
    addCookies = true;
  }

  nsAutoString allCookies;

  for (const SSOCredential& credential : credentials) {
    if (aDeviceOnly &&
        !credential.mName.LowerCaseEqualsLiteral("x-ms-devicecredential")) {
      continue;
    }
    nsAutoString cookieData(credential.mData);
    // Strip old Set-Cookie info for WinInet
    int32_t semicolon = cookieData.FindChar(';');
    if (semicolon >= 0) {
      cookieData.SetLength(semicolon);
    }
    const nsString& name = credential.mName;
    if (StringBeginsWith(name, u"x-ms-"_ns)) {
      channel->SetRequestHeader(NS_ConvertUTF16toUTF8(name),
                                NS_ConvertUTF16toUTF8(cookieData),
                                true /* merge */);
    } else if (addCookies) {
      if (!allCookies.IsEmpty()) {
        allCookies.AppendLiteral("; ");
      }
      allCookies.Append(name);
      allCookies.AppendLiteral("=");
      allCookies.Append(cookieData);
    }
  }

  // Merging cookie headers doesn't work correctly as it separates the new
  // cookies using commas instead of semicolons, so we have to replace
  // the entire header.
  if (!allCookies.IsEmpty()) {
    nsAutoCString cookieHeader;
    channel->GetRequestHeader(nsHttp::Cookie.val(), cookieHeader);
    if (!cookieHeader.IsEmpty()) {
      cookieHeader.AppendLiteral("; ");
    }
    cookieHeader.Append(NS_ConvertUTF16toUTF8(allCookies));
    channel->SetRequestHeader(nsHttp::Cookie.val(), cookieHeader, false);
  }
}

}  // namespace net
}  // namespace mozilla
