/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieService_h
#define mozilla_net_CookieService_h

#include "Cookie.h"
#include "CookieCommons.h"
#include "ThirdPartyCookieBlockingExceptions.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MozPromise.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsICookieValidation.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsString.h"
#include "nsWeakReference.h"

class nsIConsoleReportCollector;
class nsICookieJarSettings;
class nsIEffectiveTLDService;
class nsIURI;
class nsIChannel;
class mozIThirdPartyUtil;

namespace mozilla {
namespace net {

class CookiePersistentStorage;
class CookiePrivateStorage;
class CookieStorage;

/******************************************************************************
 * CookieService:
 * class declaration
 ******************************************************************************/

class CookieService final : public nsICookieService,
                            public nsICookieManager,
                            public nsIObserver,
                            public nsSupportsWeakReference,
                            public nsIMemoryReporter {
 private:
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSICOOKIESERVICE
  NS_DECL_NSICOOKIEMANAGER
  NS_DECL_NSIMEMORYREPORTER

  static already_AddRefed<CookieService> GetSingleton();

  CookieService();
  static already_AddRefed<nsICookieService> GetXPCOMSingleton();
  nsresult Init();

  static void Update3PCBExceptionInfo(nsIChannel* aChannel);

  ThirdPartyCookieBlockingExceptions& ThirdPartyCookieBlockingExceptionsRef() {
    return mThirdPartyCookieBlockingExceptions;
  }

  /**
   * Start watching the observer service for messages indicating that an app has
   * been uninstalled.  When an app is uninstalled, we get the cookie service
   * (thus instantiating it, if necessary) and clear all the cookies for that
   * app.
   */

  static CookieStatus CheckPrefs(
      nsIConsoleReportCollector* aCRC, nsICookieJarSettings* aCookieJarSettings,
      nsIURI* aHostURI, bool aIsForeign, bool aIsThirdPartyTrackingResource,
      bool aIsThirdPartySocialTrackingResource,
      bool aStorageAccessPermissionGranted, const nsACString& aCookieHeader,
      bool aHasExistingCookies, const OriginAttributes& aOriginAttrs,
      uint32_t* aRejectedReason);

  void GetCookiesForURI(nsIURI* aHostURI, nsIChannel* aChannel, bool aIsForeign,
                        bool aIsThirdPartyTrackingResource,
                        bool aIsThirdPartySocialTrackingResource,
                        bool aStorageAccessPermissionGranted,
                        uint32_t aRejectedReason, bool aIsSafeTopLevelNav,
                        bool aIsSameSiteForeign, bool aHadCrossSiteRedirects,
                        bool aHttpBound,
                        bool aAllowSecureCookiesToInsecureOrigin,
                        const nsTArray<OriginAttributes>& aOriginAttrsList,
                        nsTArray<RefPtr<Cookie>>& aCookieList);

  /**
   * This method is a helper that allows calling nsICookieManager::Remove()
   * with OriginAttributes parameter.
   */
  nsresult Remove(const nsACString& aHost, const OriginAttributes& aAttrs,
                  const nsACString& aName, const nsACString& aPath,
                  bool aFromHttp, const nsID* aOperationID);

  nsICookieValidation::ValidationError SetCookiesFromIPC(
      const nsACString& aBaseDomain, const OriginAttributes& aAttrs,
      nsIURI* aHostURI, bool aIsThirdParty,
      const nsTArray<CookieStruct>& aCookies,
      dom::BrowsingContext* aBrowsingContext);

 protected:
  virtual ~CookieService();

  bool IsInitialized() const;

  void InitCookieStorages();
  void CloseCookieStorages();

  nsresult NormalizeHost(nsCString& aHost);
  void NotifyAccepted(nsIChannel* aChannel, uint32_t aRejectedReason);

  nsresult GetCookiesWithOriginAttributes(
      const OriginAttributesPattern& aPattern, const nsCString& aBaseDomain,
      bool aSorted, nsTArray<RefPtr<nsICookie>>& aResult);
  nsresult RemoveCookiesWithOriginAttributes(
      const OriginAttributesPattern& aPattern, const nsCString& aBaseDomain);

 protected:
  CookieStorage* PickStorage(const OriginAttributes& aAttrs);
  CookieStorage* PickStorage(const OriginAttributesPattern& aAttrs);

  nsresult RemoveCookiesFromExactHost(const nsACString& aHost,
                                      const OriginAttributesPattern& aPattern);

  // cached members.
  nsCOMPtr<mozIThirdPartyUtil> mThirdPartyUtil;
  nsCOMPtr<nsIEffectiveTLDService> mTLDService;

  ThirdPartyCookieBlockingExceptions mThirdPartyCookieBlockingExceptions;

  // we have two separate Cookie Storages: one for normal browsing and one for
  // private browsing.
  RefPtr<CookieStorage> mPersistentStorage;
  RefPtr<CookieStorage> mPrivateStorage;

  // Holds the real persistent storage after shutdown swap so it is not
  // destroyed (and its in-memory cookie tree torn down) on the main thread
  // during the critical shutdown window. Releases naturally with the service.
  RefPtr<CookieStorage> mRetiredStorage;

  void RetirePersistentStorageForShutdown();

 private:
  nsresult AddInternal(nsIURI* aCookieURI, const nsACString& aHost,
                       const nsACString& aPath, const nsACString& aName,
                       const nsACString& aValue, bool aIsSecure,
                       bool aIsHttpOnly, bool aIsSession, int64_t aExpiry,
                       OriginAttributes* aOriginAttributes, int32_t aSameSite,
                       nsICookie::schemeType aSchemeMap, bool aIsPartitioned,
                       bool aFromHttp, const nsID* aOperationID,
                       nsICookieValidation** aValidation);
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_CookieService_h
