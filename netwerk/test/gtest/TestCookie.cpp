/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Cookie.h"
#include "CookieDBWriteQueue.h"
#include "CookieParser.h"
#include "CookieStorage.h"
#include "TestCommon.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/CookieValidation.h"
#include "nsContentUtils.h"
#include "nsIChannel.h"
#include "nsIConsoleReportCollector.h"
#include "nsICookie.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "plstr.h"

using namespace mozilla;
using namespace mozilla::net;

static NS_DEFINE_CID(kCookieServiceCID, NS_COOKIESERVICE_CID);
static NS_DEFINE_CID(kPrefServiceCID, NS_PREFSERVICE_CID);

// various pref strings
static const char kCookiesPermissions[] = "network.cookie.cookieBehavior";
static const char kCookiesMaxPerHost[] = "network.cookie.maxPerHost";

void SetACookieInternal(nsICookieService* aCookieService, const char* aSpec,
                        const nsTArray<const char*>& aCookieStrings,
                        bool aAllowed) {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aSpec);

  // We create a dummy channel using the aSpec to simulate same-siteness
  nsresult rv0;
  nsCOMPtr<nsIScriptSecurityManager> ssm =
      do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv0);
  ASSERT_NS_SUCCEEDED(rv0);
  nsCOMPtr<nsIPrincipal> specPrincipal;
  nsCString tmpString(aSpec);
  ssm->CreateContentPrincipalFromOrigin(tmpString,
                                        getter_AddRefs(specPrincipal));

  nsCOMPtr<nsIChannel> dummyChannel;
  NS_NewChannel(getter_AddRefs(dummyChannel), uri, specPrincipal,
                nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
                nsIContentPolicy::TYPE_OTHER);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      aAllowed
          ? CookieJarSettings::Create(CookieJarSettings::eRegular,
                                      /* shouldResistFingerprinting */ false)
          : CookieJarSettings::GetBlockingAll(
                /* shouldResistFingerprinting */ false);
  MOZ_RELEASE_ASSERT(cookieJarSettings);

  nsCOMPtr<nsILoadInfo> loadInfo = dummyChannel->LoadInfo();
  loadInfo->SetCookieJarSettings(cookieJarSettings);

  for (const char* cookieString : aCookieStrings) {
    nsresult rv = aCookieService->SetCookieStringFromHttp(
        uri, nsDependentCString(cookieString), dummyChannel);
    EXPECT_NS_SUCCEEDED(rv);
  }
}

void SetACookieJarBlocked(nsICookieService* aCookieService, const char* aSpec,
                          const char* aCookieString) {
  nsTArray<const char*> cookieStrings;
  cookieStrings.AppendElement(aCookieString);
  SetACookieInternal(aCookieService, aSpec, cookieStrings, false);
}

void SetACookie(nsICookieService* aCookieService, const char* aSpec,
                const char* aCookieString) {
  nsTArray<const char*> cookieStrings;
  cookieStrings.AppendElement(aCookieString);
  SetACookieInternal(aCookieService, aSpec, cookieStrings, true);
}

void SetACookie(nsICookieService* aCookieService, const char* aSpec,
                const nsTArray<const char*>& aCookieStrings) {
  SetACookieInternal(aCookieService, aSpec, aCookieStrings, true);
}

// The cookie string is returned via aCookie.
void GetACookie(nsICookieService* aCookieService, const char* aSpec,
                nsACString& aCookie) {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aSpec);

  nsCOMPtr<nsIIOService> service = do_GetIOService();

  nsCOMPtr<nsIChannel> channel;
  (void)service->NewChannelFromURI(
      uri, nullptr, nsContentUtils::GetSystemPrincipal(),
      nsContentUtils::GetSystemPrincipal(), 0, nsIContentPolicy::TYPE_DOCUMENT,
      getter_AddRefs(channel));

  (void)aCookieService->GetCookieStringFromHttp(uri, channel, aCookie);
}

// The cookie string is returned via aCookie.
void GetACookieNoHttp(nsICookieService* aCookieService, const char* aSpec,
                      nsACString& aCookie) {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aSpec);

  RefPtr<BasePrincipal> principal =
      BasePrincipal::CreateContentPrincipal(uri, OriginAttributes());
  MOZ_RELEASE_ASSERT(principal);

  nsCOMPtr<mozilla::dom::Document> document;
  nsresult rv =
      NS_NewDOMDocument(getter_AddRefs(document),
                        u""_ns,   // aNamespaceURI
                        u""_ns,   // aQualifiedName
                        nullptr,  // aDoctype
                        uri, uri, principal,
                        mozilla::dom::LoadedAsData::No,  // aLoadedAsData
                        nullptr,                         // aEventObject
                        DocumentFlavor::HTML);
  (void)NS_WARN_IF(NS_FAILED(rv));

  nsAutoString cookie;
  ErrorResult err;
  document->GetCookie(cookie, err);
  EXPECT_TRUE(!err.Failed());

  CopyUTF16toUTF8(cookie, aCookie);
}

// some #defines for comparison rules
#define MUST_BE_NULL 0
#define MUST_EQUAL 1
#define MUST_CONTAIN 2
#define MUST_NOT_CONTAIN 3
#define MUST_NOT_EQUAL 4

// a simple helper function to improve readability:
// takes one of the #defined rules above, and performs the appropriate test.
// true means the test passed; false means the test failed.
static inline bool CheckResult(const char* aLhs, uint32_t aRule,
                               const char* aRhs = nullptr) {
  switch (aRule) {
    case MUST_BE_NULL:
      return !aLhs || !*aLhs;

    case MUST_EQUAL:
      return !PL_strcmp(aLhs, aRhs);

    case MUST_NOT_EQUAL:
      return PL_strcmp(aLhs, aRhs);

    case MUST_CONTAIN:
      return strstr(aLhs, aRhs) != nullptr;

    case MUST_NOT_CONTAIN:
      return strstr(aLhs, aRhs) == nullptr;

    default:
      return false;  // failure
  }
}

void InitPrefs(nsIPrefBranch* aPrefBranch) {
  // init some relevant prefs, so the tests don't go awry.
  // we use the most restrictive set of prefs we can;
  // however, we don't test third party blocking here.
  aPrefBranch->SetIntPref(kCookiesPermissions, 0);  // accept all
  // Set quotaPerHost to maxPerHost, so there is only one cookie
  // will be evicted everytime.
  aPrefBranch->SetIntPref(kPrefCookieQuotaPerHost, 50);
  // Set the base domain limit to 50 so we have a known value.
  aPrefBranch->SetIntPref(kCookiesMaxPerHost, 50);

  // SameSite=None by default. We have other tests for lax-by-default.
  // XXX: Bug 1617611 - Fix all the tests broken by "cookies SameSite=Lax by
  // default"
  Preferences::SetBool("network.cookie.sameSite.laxByDefault", false);
  Preferences::SetBool("network.cookieJarSettings.unblocked_for_testing", true);
  Preferences::SetBool("dom.securecontext.allowlist_onions", false);
  Preferences::SetBool("network.cookie.sameSite.schemeful", false);

  // Disable a few security checks for document.cookie
  Preferences::SetBool("dom.cookie.testing.enabled", true);
}

TEST(TestCookie, TestCookieMain)
{
  nsresult rv0;

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv0);
  ASSERT_NS_SUCCEEDED(rv0);

  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(kPrefServiceCID, &rv0);
  ASSERT_NS_SUCCEEDED(rv0);

  InitPrefs(prefBranch);

  nsCString cookie;

  /* The basic idea behind these tests is the following:
   *
   * we set() some cookie, then try to get() it in various ways. we have
   * several possible tests we perform on the cookie string returned from
   * get():
   *
   * a) check whether the returned string is null (i.e. we got no cookies
   *    back). this is used e.g. to ensure a given cookie was deleted
   *    correctly, or to ensure a certain cookie wasn't returned to a given
   *    host.
   * b) check whether the returned string exactly matches a given string.
   *    this is used where we want to make sure our cookie service adheres to
   *    some strict spec (e.g. ordering of multiple cookies), or where we
   *    just know exactly what the returned string should be.
   * c) check whether the returned string contains/does not contain a given
   *    string. this is used where we don't know/don't care about the
   *    ordering of multiple cookies - we just want to make sure the cookie
   *    string contains them all, in some order.
   *
   * NOTE: this testsuite is not yet comprehensive or complete, and is
   * somewhat contrived - still under development, and needs improving!
   */

  // test some basic variations of the domain & path
  SetACookie(cookieService, "http://www.basic.com", "test=basic");
  GetACookie(cookieService, "http://www.basic.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=basic"));
  GetACookie(cookieService, "http://www.basic.com/testPath/testfile.txt",
             cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=basic"));
  GetACookie(cookieService, "http://www.basic.com./", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://www.basic.com.", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://www.basic.com./testPath/testfile.txt",
             cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://www.basic2.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://www.basic.com", "test=basic; max-age=-1");
  GetACookie(cookieService, "http://www.basic.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // *** domain tests

  // test some variations of the domain & path, for different domains of
  // a domain cookie
  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=domain.com; sameSite=lax");
  GetACookie(cookieService, "http://domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  GetACookie(cookieService, "http://domain.com.", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://www.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=domain.com; max-age=-1");
  GetACookie(cookieService, "http://domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=.domain.com");
  GetACookie(cookieService, "http://domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  GetACookie(cookieService, "http://www.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  GetACookie(cookieService, "http://bah.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=domain"));
  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=.domain.com; max-age=-1");
  GetACookie(cookieService, "http://domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=.foo.domain.com");
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=moose.com");
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=domain.com.");
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=..domain.com");
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.domain.com",
             "test=domain; domain=..domain.com.");
  GetACookie(cookieService, "http://foo.domain.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://path.net/path/file",
             R"(test=taco; path="/bogus")");
  GetACookie(cookieService, "http://path.net/path/file", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=taco"));
  SetACookie(cookieService, "http://path.net/path/file",
             "test=taco; max-age=-1");
  GetACookie(cookieService, "http://path.net/path/file", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // *** path tests

  // test some variations of the domain & path, for different paths of
  // a path cookie
  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/path");
  GetACookie(cookieService, "http://path.net/path", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  GetACookie(cookieService, "http://path.net/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  GetACookie(cookieService, "http://path.net/path/hithere.foo", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  GetACookie(cookieService, "http://path.net/path?hithere/foo", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  GetACookie(cookieService, "http://path.net/path2", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://path.net/path2/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/path; max-age=-1");
  GetACookie(cookieService, "http://path.net/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/path/");
  GetACookie(cookieService, "http://path.net/path", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://path.net/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/path/; max-age=-1");
  GetACookie(cookieService, "http://path.net/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // note that a site can set a cookie for a path it's not on.
  // this is an intentional deviation from spec (see comments in
  // CookieService::CheckPath()), so we test this functionality too
  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/foo/");
  GetACookie(cookieService, "http://path.net/path", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  GetACookie(cookieService, "http://path.net/foo", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://path.net/path/file",
             "test=path; path=/foo/; max-age=-1");
  GetACookie(cookieService, "http://path.net/foo/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // attributes with size > 1024 are ignored.
  SetACookie(
      cookieService, "http://path.net/",
      "test=path; "
      "path=/"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "9012345678901234567890/");
  GetACookie(
      cookieService,
      "http://path.net/"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "9012345678901234567890",
      cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  GetACookie(cookieService, "http://path.net/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=path"));
  // the following cookie has a path > 1024 bytes implicitly specified by the
  // uri path
  SetACookie(
      cookieService,
      "http://longpath.net/"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "9012345678901234567890/",
      "test=path");
  GetACookie(
      cookieService,
      "http://longpath.net/"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "901234567890123456789012345678901234567890123456789012345678901234567890"
      "123456789012345678901234567890123456789012345678901234567890123456789012"
      "345678901234567890123456789012345678901234567890123456789012345678901234"
      "567890123456789012345678901234567890123456789012345678901234567890123456"
      "789012345678901234567890123456789012345678901234567890123456789012345678"
      "9012345678901234567890/",
      cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  // the following cookie includes a tab in the path
  SetACookie(cookieService, "http://pathwithtab.net/",
             "test=path; path=/foo\tbar/");
  GetACookie(cookieService, "http://pathwithtab.net/foo\tbar/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  // the following cookie includes a tab in the name
  SetACookie(cookieService, "http://pathwithtab.net/", "test\ttabs=tab");
  GetACookie(cookieService, "http://pathwithtab.net/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test\ttabs=tab"));
  // the following cookie includes a tab in the value - allowed
  SetACookie(cookieService, "http://pathwithtab.net/", "test=tab\ttest");
  GetACookie(cookieService, "http://pathwithtab.net/", cookie);
  EXPECT_TRUE(
      CheckResult(cookie.get(), MUST_EQUAL, "test\ttabs=tab; test=tab\ttest"));
  SetACookie(cookieService, "http://pathwithtab.net/",
             "test=tab\ttest; max-age=-1");
  GetACookie(cookieService, "http://pathwithtab.net/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test\ttabs=tab"));

  // *** expiry & deletion tests
  // XXX add server time str parsing tests here

  // test some variations of the expiry time,
  // and test deletion of previously set cookies
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=-1");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=0");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; expires=bad");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=expiry"));
  SetACookie(cookieService, "http://expireme.org/",
             "test=expiry; expires=Thu, 10 Apr 1980 16:33:12 GMT");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/",
             R"(test=expiry; expires="Thu, 10 Apr 1980 16:33:12 GMT)");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/",
             R"(test=expiry; expires="Thu, 10 Apr 1980 16:33:12 GMT")");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=60");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=expiry"));
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=-20");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=60");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=expiry"));
  SetACookie(cookieService, "http://expireme.org/",
             "test=expiry; expires=Thu, 10 Apr 1980 16:33:12 GMT");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://expireme.org/", "test=expiry; max-age=60");
  SetACookie(cookieService, "http://expireme.org/",
             "newtest=expiry; max-age=60");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=expiry"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "newtest=expiry"));
  SetACookie(cookieService, "http://expireme.org/",
             "test=differentvalue; max-age=0");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "newtest=expiry"));
  SetACookie(cookieService, "http://expireme.org/",
             "newtest=evendifferentvalue; max-age=0");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://foo.expireme.org/",
             "test=expiry; domain=.expireme.org; max-age=60");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=expiry"));
  SetACookie(cookieService, "http://bar.expireme.org/",
             "test=differentvalue; domain=.expireme.org; max-age=0");
  GetACookie(cookieService, "http://expireme.org/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  nsAutoCString ServerTime;
  nsAutoCString CookieString;

  // *** multiple cookie tests

  // test the setting of multiple cookies, and test the order of precedence
  // (a later cookie overwriting an earlier one, in the same header string)
  SetACookie(cookieService, "http://multiple.cookies/",
             nsTArray<const char*>{
                 "test=multiple; domain=.multiple.cookies ", " test=different ",
                 " test=same; domain=.multiple.cookies ", "newtest=ciao ",
                 "newtest=foo; max-age=-6 ", " newtest=reincarnated"});
  GetACookie(cookieService, "http://multiple.cookies/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "test=multiple"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=different"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=same"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "newtest=ciao"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "newtest=foo"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "newtest=reincarnated"));
  SetACookie(cookieService, "http://multiple.cookies/",
             "test=expiry; domain=.multiple.cookies; max-age=0");
  GetACookie(cookieService, "http://multiple.cookies/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "test=same"));
  SetACookie(cookieService, "http://multiple.cookies/",
             nsTArray<const char*>{"", " test=different; max-age=0 ", ""});
  GetACookie(cookieService, "http://multiple.cookies/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "test=different"));
  SetACookie(cookieService, "http://multiple.cookies/",
             "newtest=dead; max-age=0");
  GetACookie(cookieService, "http://multiple.cookies/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // *** parser tests

  // test the cookie header parser, under various circumstances.
  SetACookie(cookieService, "http://parser.test/",
             "test=parser; domain=.parser.test; ;; ;=; ,,, ===,abc,=; "
             "abracadabra! max-age=20;=;;");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=parser"));
  SetACookie(cookieService, "http://parser.test/",
             "test=parser; domain=.parser.test; max-age=0");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "http://parser.test/",
             nsTArray<const char*>{
                 "test=\"fubar! = foo;bar\\\";\" parser; domain=.parser.test; "
                 "max-age=6",
                 "five; max-age=2.63,"});
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, R"(test="fubar! = foo)"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "five"));
  SetACookie(cookieService, "http://parser.test/",
             nsTArray<const char*>{"test=kill; domain=.parser.test; max-age=0 ",
                                   " five; max-age=0"});
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // test the handling of valueless cookies (no '=' in cookie string).
  // With network.cookie.valueless_cookie=true (default), "six" is parsed as
  // name="six", value="" and serialized as "six=". Each valueless cookie has a
  // distinct name, so they don't overwrite each other.
  Preferences::SetBool("network.cookie.valueless_cookie", true);
  SetACookie(cookieService, "http://valueless.parser.test/", "six");
  GetACookie(cookieService, "http://valueless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "six="));
  SetACookie(cookieService, "http://valueless.parser.test/", "seven");
  GetACookie(cookieService, "http://valueless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "six="));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "seven="));
  // " =eight" parses as name="" value="eight" (nameless), which is rejected
  // when valueless_cookie=true, so only the previous two cookies remain.
  SetACookie(cookieService, "http://valueless.parser.test/", " =eight");
  GetACookie(cookieService, "http://valueless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "six="));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "seven="));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "eight"));
  SetACookie(cookieService, "http://valueless.parser.test/", "test=six");
  GetACookie(cookieService, "http://valueless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=six"));

  // With network.cookie.valueless_cookie=false (legacy), "six" is parsed as
  // name="", value="six", so valueless cookies overwrite each other.
  Preferences::SetBool("network.cookie.valueless_cookie", false);
  SetACookie(cookieService, "http://nameless.parser.test/", "six");
  GetACookie(cookieService, "http://nameless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "six"));
  SetACookie(cookieService, "http://nameless.parser.test/", "seven");
  GetACookie(cookieService, "http://nameless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "seven"));
  SetACookie(cookieService, "http://nameless.parser.test/", " =eight");
  GetACookie(cookieService, "http://nameless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "eight"));
  SetACookie(cookieService, "http://nameless.parser.test/", "test=six");
  GetACookie(cookieService, "http://nameless.parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=six"));
  Preferences::SetBool("network.cookie.valueless_cookie", true);

  // *** path ordering tests

  // test that cookies are returned in path order - longest to shortest.
  // if the header doesn't specify a path, it's taken from the host URI.
  SetACookie(cookieService, "http://multi.path.tests/",
             "test1=path; path=/one/two/three");
  SetACookie(cookieService, "http://multi.path.tests/",
             nsTArray<const char*>{"test2=path; path=/one ",
                                   " test3=path; path=/one/two/three/four ",
                                   " test4=path; path=/one/two ",
                                   " test5=path; path=/one/two/"});
  SetACookie(cookieService, "http://multi.path.tests/one/two/three/four/five/",
             "test6=path");
  SetACookie(cookieService,
             "http://multi.path.tests/one/two/three/four/five/six/",
             "test7=path; path=");
  SetACookie(cookieService, "http://multi.path.tests/", "test8=path; path=/");
  GetACookie(cookieService,
             "http://multi.path.tests/one/two/three/four/five/six/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL,
                          "test7=path; test6=path; test3=path; test1=path; "
                          "test5=path; test4=path; test2=path; test8=path"));

  // *** Cookie prefix tests

  // prefixed cookies can't be set from insecure HTTP
  SetACookie(cookieService, "http://prefixed.test/", "__Secure-test1=test");
  SetACookie(cookieService, "http://prefixed.test/",
             "__Secure-test2=test; secure");
  SetACookie(cookieService, "http://prefixed.test/", "__Host-test1=test");
  SetACookie(cookieService, "http://prefixed.test/",
             "__Host-test2=test; secure");
  GetACookie(cookieService, "http://prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // prefixed cookies won't be set without the secure flag
  SetACookie(cookieService, "https://prefixed.test/", "__Secure-test=test");
  SetACookie(cookieService, "https://prefixed.test/", "__Host-test=test");
  GetACookie(cookieService, "https://prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // prefixed cookies can be set when done correctly
  SetACookie(cookieService, "https://prefixed.test/",
             "__Secure-test=test; secure");
  SetACookie(cookieService, "https://prefixed.test/",
             "__Host-test=test; secure; path=/");
  GetACookie(cookieService, "https://prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "__Secure-test=test"));
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "__Host-test=test"));

  // but when set must not be returned to the host insecurely
  GetACookie(cookieService, "http://prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // Host-prefixed cookies cannot specify a domain
  SetACookie(cookieService, "https://host.prefixed.test/",
             "__Host-a=test; secure; domain=prefixed.test");
  SetACookie(cookieService, "https://host.prefixed.test/",
             "__Host-b=test; secure; domain=.prefixed.test");
  SetACookie(cookieService, "https://host.prefixed.test/",
             "__Host-c=test; secure; domain=host.prefixed.test");
  SetACookie(cookieService, "https://host.prefixed.test/",
             "__Host-d=test; secure; domain=.host.prefixed.test");
  GetACookie(cookieService, "https://host.prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // Host-prefixed cookies can only have a path of "/"
  SetACookie(cookieService, "https://host.prefixed.test/some/path",
             "__Host-e=test; secure");
  SetACookie(cookieService, "https://host.prefixed.test/some/path",
             "__Host-f=test; secure; path=/");
  SetACookie(cookieService, "https://host.prefixed.test/some/path",
             "__Host-g=test; secure; path=/some");
  GetACookie(cookieService, "https://host.prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "__Host-f=test"));

  // Host-prefixed cookies require an explicit Path attribute of "/": a default
  // path that happens to be "/" is not enough.
  SetACookie(cookieService, "https://explicit.prefixed.test/",
             "__Host-h=test; secure");
  SetACookie(cookieService, "https://explicit.prefixed.test/",
             "__Host-i=test; secure; path=");
  SetACookie(cookieService, "https://explicit.prefixed.test/",
             "__Host-j=test; secure; path");
  SetACookie(cookieService, "https://explicit.prefixed.test/",
             "__Host-k=test; secure; path=relative");
  SetACookie(cookieService, "https://explicit.prefixed.test/",
             "__Host-l=test; secure; path=/");
  GetACookie(cookieService, "https://explicit.prefixed.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "__Host-l=test"));

  // *** leave-secure-alone tests

  // testing items 0 & 1 for 3.1 of spec Deprecate modification of ’secure’
  // cookies from non-secure origins
  SetACookie(cookieService, "http://www.security.test/",
             "test=non-security; secure");
  GetACookieNoHttp(cookieService, "https://www.security.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
  SetACookie(cookieService, "https://www.security.test/path/",
             "test=security; secure; path=/path/");
  GetACookieNoHttp(cookieService, "https://www.security.test/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=security"));
  // testing items 2 & 3 & 4 for 3.2 of spec Deprecate modification of ’secure’
  // cookies from non-secure origins
  // Secure site can modify cookie value
  SetACookie(cookieService, "https://www.security.test/path/",
             "test=security2; secure; path=/path/");
  GetACookieNoHttp(cookieService, "https://www.security.test/path/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=security2"));
  // If new cookie contains same name, same host and partially matching path
  // with an existing security cookie on non-security site, it can't modify an
  // existing security cookie.
  SetACookie(cookieService, "http://www.security.test/path/foo/",
             "test=non-security; path=/path/foo");
  GetACookieNoHttp(cookieService, "https://www.security.test/path/foo/",
                   cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=security2"));
  // Non-secure cookie can set by same name, same host and non-matching path.
  SetACookie(cookieService, "http://www.security.test/bar/",
             "test=non-security; path=/bar");
  GetACookieNoHttp(cookieService, "http://www.security.test/bar/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=non-security"));
  // Modify value and downgrade secure level.
  SetACookie(
      cookieService, "https://www.security.test/",
      "test_modify_cookie=security-cookie; secure; domain=.security.test");
  GetACookieNoHttp(cookieService, "https://www.security.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL,
                          "test_modify_cookie=security-cookie"));
  SetACookie(cookieService, "https://www.security.test/",
             "test_modify_cookie=non-security-cookie; domain=.security.test");
  GetACookieNoHttp(cookieService, "https://www.security.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL,
                          "test_modify_cookie=non-security-cookie"));

  // Test the non-security cookie can set when domain or path not same to secure
  // cookie of same name.
  SetACookie(cookieService, "https://www.security.test/", "test=security3");
  GetACookieNoHttp(cookieService, "http://www.security.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=security3"));
  SetACookie(cookieService, "http://www.security.test/",
             "test=non-security2; domain=security.test");
  GetACookieNoHttp(cookieService, "http://www.security.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=non-security2"));

  // *** nsICookieManager interface tests
  nsCOMPtr<nsICookieManager> cookieMgr =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID, &rv0);
  ASSERT_NS_SUCCEEDED(rv0);

  const nsCOMPtr<nsICookieManager>& cookieMgr2 = cookieMgr;
  ASSERT_TRUE(cookieMgr2);

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "https://cookie.test"_ns);

  mozilla::OriginAttributes attrs;

  // first, ensure a clean slate
  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  nsCOMPtr<nsICookieValidation> cv;

  // add some cookies
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->AddNative(uri,
                                                 "cookiemgr.test"_ns,  // domain
                                                 "/foo"_ns,            // path
                                                 "test1"_ns,           // name
                                                 "yes"_ns,             // value
                                                 false,      // is secure
                                                 false,      // is httponly
                                                 true,       // is session
                                                 INT64_MAX,  // expiry time
                                                 &attrs,     // originAttributes
                                                 nsICookie::SAMESITE_LAX,
                                                 nsICookie::SCHEME_HTTP,
                                                 false,    // is partitioned
                                                 true,     // from http
                                                 nullptr,  // operation ID
                                                 getter_AddRefs(cv))));
  EXPECT_TRUE(!!cv);
  EXPECT_EQ(CookieValidation::Cast(cv)->Result(), nsICookieValidation::eOK);

  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->AddNative(uri,
                            "cookiemgr.test"_ns,                 // domain
                            "/foo"_ns,                           // path
                            "test2"_ns,                          // name
                            "yes"_ns,                            // value
                            false,                               // is secure
                            true,                                // is httponly
                            true,                                // is session
                            PR_Now() / PR_USEC_PER_MSEC + 2000,  // expiry time
                            &attrs,  // originAttributes
                            nsICookie::SAMESITE_LAX, nsICookie::SCHEME_HTTP,
                            false,    // is partitioned
                            true,     // from http
                            nullptr,  // operation ID
                            getter_AddRefs(cv))));
  EXPECT_TRUE(!!cv);
  EXPECT_EQ(CookieValidation::Cast(cv)->Result(), nsICookieValidation::eOK);

  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->AddNative(uri,
                                                 "new.domain"_ns,  // domain
                                                 "/rabbit"_ns,     // path
                                                 "test3"_ns,       // name
                                                 "yes"_ns,         // value
                                                 false,            // is secure
                                                 false,      // is httponly
                                                 true,       // is session
                                                 INT64_MAX,  // expiry time
                                                 &attrs,     // originAttributes
                                                 nsICookie::SAMESITE_LAX,
                                                 nsICookie::SCHEME_HTTP,
                                                 false,    // is partitioned
                                                 true,     // from http
                                                 nullptr,  // operation ID
                                                 getter_AddRefs(cv))));
  EXPECT_TRUE(!!cv);
  EXPECT_EQ(CookieValidation::Cast(cv)->Result(), nsICookieValidation::eOK);

  // confirm using enumerator
  nsTArray<RefPtr<nsICookie>> cookies;
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  nsCOMPtr<nsICookie> expiredCookie, newDomainCookie;
  for (const auto& cookie : cookies) {
    nsAutoCString name;
    cookie->GetName(name);
    if (name.EqualsLiteral("test2")) {
      expiredCookie = cookie;
    } else if (name.EqualsLiteral("test3")) {
      newDomainCookie = cookie;
    }
  }
  EXPECT_EQ(cookies.Length(), 3ul);
  // check the httpOnly attribute of the second cookie is honored
  GetACookie(cookieService, "http://cookiemgr.test/foo/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test2=yes"));
  GetACookieNoHttp(cookieService, "http://cookiemgr.test/foo/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_NOT_CONTAIN, "test2=yes"));
  // check GetCookiesFromHostNative()
  nsTArray<RefPtr<nsICookie>> hostCookies;
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->GetCookiesFromHostNative(
      "cookiemgr.test"_ns, &attrs, false, hostCookies)));
  EXPECT_EQ(hostCookies.Length(), 2u);
  // check CookieExistsNative() using the third cookie
  bool found;
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->CookieExistsNative(
      "new.domain"_ns, "/rabbit"_ns, "test3"_ns, &attrs, &found)));
  EXPECT_TRUE(found);

  // sleep four seconds, to make sure the second cookie has expired
  PR_Sleep(4 * PR_TicksPerSecond());
  // check that the expired cookie is not exposed by the cookie manager
  hostCookies.Clear();
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->GetCookiesFromHostNative(
      "cookiemgr.test"_ns, &attrs, false, hostCookies)));
  EXPECT_EQ(hostCookies.Length(), 1u);
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->CookieExistsNative(
      "cookiemgr.test"_ns, "/foo"_ns, "test2"_ns, &attrs, &found)));
  EXPECT_FALSE(found);
  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  EXPECT_EQ(cookies.Length(), 2ul);
  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetSessionCookies(cookies));
  EXPECT_EQ(cookies.Length(), 2ul);
  // double-check RemoveAll() using the enumerator
  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());
  cookies.SetLength(0);
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr->GetCookies(cookies)) &&
              cookies.IsEmpty());

  // *** eviction and creation ordering tests

  // test that cookies are
  // a) returned by order of creation time (oldest first, newest last)
  // b) evicted by order of lastAccessed time, if the limit on cookies per host
  // (50) is reached
  nsAutoCString name;
  nsAutoCString expected;
  for (int32_t i = 0; i < 60; ++i) {
    name = "test"_ns;
    name.AppendInt(i);
    name += "=creation"_ns;
    SetACookie(cookieService, "http://creation.ordering.tests/", name.get());

    if (i >= 10) {
      expected += name;
      if (i < 59) expected += "; "_ns;
    }
  }
  GetACookie(cookieService, "http://creation.ordering.tests/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, expected.get()));

  cookieMgr->RemoveAll();

  for (int32_t i = 0; i < 60; ++i) {
    name = "test"_ns;
    name.AppendInt(i);
    name += "=delete_non_security"_ns;

    // Create 50 cookies that include the secure flag.
    if (i < 50) {
      name += "; secure"_ns;
      SetACookie(cookieService, "https://creation.ordering.tests/", name.get());
    } else {
      // non-security cookies will be removed beside the latest cookie that be
      // created.
      SetACookie(cookieService, "http://creation.ordering.tests/", name.get());
    }
  }
  GetACookie(cookieService, "http://creation.ordering.tests/", cookie);

  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  // *** SameSite attribute - parsing and cookie storage tests
  // Clear the cookies
  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  // None of these cookies will be set because using
  // CookieJarSettings::GetBlockingAll().
  SetACookieJarBlocked(cookieService, "http://samesite.test", "unset=yes");
  SetACookieJarBlocked(cookieService, "http://samesite.test",
                       "unspecified=yes; samesite");
  SetACookieJarBlocked(cookieService, "http://samesite.test",
                       "empty=yes; samesite=");
  SetACookieJarBlocked(cookieService, "http://samesite.test",
                       "bogus=yes; samesite=bogus");
  SetACookieJarBlocked(cookieService, "http://samesite.test",
                       "strict=yes; samesite=strict");
  SetACookieJarBlocked(cookieService, "http://samesite.test",
                       "lax=yes; samesite=lax");

  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));

  EXPECT_TRUE(cookies.IsEmpty());

  // Set cookies with various incantations of the samesite attribute:
  // No same site attribute present
  SetACookie(cookieService, "http://samesite.test", "unset=yes");
  // samesite attribute present but with no value
  SetACookie(cookieService, "http://samesite.test",
             "unspecified=yes; samesite");
  // samesite attribute present but with an empty value
  SetACookie(cookieService, "http://samesite.test", "empty=yes; samesite=");
  // samesite attribute present but with an invalid value
  SetACookie(cookieService, "http://samesite.test",
             "bogus=yes; samesite=bogus");
  // samesite=strict
  SetACookie(cookieService, "http://samesite.test",
             "strict=yes; samesite=strict");
  // samesite=lax
  SetACookie(cookieService, "http://samesite.test", "lax=yes; samesite=lax");

  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));

  // check the cookies for the required samesite value
  for (const auto& cookie : cookies) {
    nsAutoCString name;
    cookie->GetName(name);
    int32_t sameSiteAttr;
    cookie->GetSameSite(&sameSiteAttr);
    if (name.EqualsLiteral("unset")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_UNSET);
    } else if (name.EqualsLiteral("unspecified")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_UNSET);
    } else if (name.EqualsLiteral("empty")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_UNSET);
    } else if (name.EqualsLiteral("bogus")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_UNSET);
    } else if (name.EqualsLiteral("none")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_NONE);
    } else if (name.EqualsLiteral("strict")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_STRICT);
    } else if (name.EqualsLiteral("lax")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_LAX);
    }
  }

  EXPECT_TRUE(cookies.Length() == 6);

  // *** SameSite attribute
  // Clear the cookies
  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  // please note that the flag aForeign is always set to true using this test
  // setup because no nsIChannel is passed to SetCookieString(). therefore we
  // can only test that no cookies are sent for cross origin requests using
  // same-site cookies.
  SetACookie(cookieService, "http://www.samesite.com",
             "test=sameSiteStrictVal; samesite=strict");
  GetACookie(cookieService, "http://www.notsamesite.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://www.samesite.test",
             "test=sameSiteLaxVal; samesite=lax");
  GetACookie(cookieService, "http://www.notsamesite.com", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  static const char* secureURIs[] = {
      "http://localhost", "http://localhost:1234", "http://127.0.0.1",
      "http://127.0.0.2", "http://127.1.0.1",      "http://[::1]",
      // TODO bug 1220810 "http://xyzzy.localhost"
  };

  uint32_t numSecureURIs = sizeof(secureURIs) / sizeof(const char*);
  for (uint32_t i = 0; i < numSecureURIs; ++i) {
    SetACookie(cookieService, secureURIs[i], "test=basic; secure");
    GetACookie(cookieService, secureURIs[i], cookie);
    EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=basic"));
    SetACookie(cookieService, secureURIs[i], "test=basic1");
    GetACookie(cookieService, secureURIs[i], cookie);
    EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=basic1"));
  }

  // XXX the following are placeholders: add these tests please!
  // *** "noncompliant cookie" tests
  // *** IP address tests
  // *** speed tests
}

TEST(TestCookie, InvalidCharsInNameAndValue)
{
  nsresult rv;

  nsCOMPtr<nsICookieManager> cookieMgr =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "https://cookie.test"_ns);

  mozilla::OriginAttributes attrs;

  // Test some invalid chars
#define TEST_INVALID_CHARS(name, value, error)                                 \
  {                                                                            \
    nsCOMPtr<nsICookieValidation> cv;                                          \
    EXPECT_EQ(                                                                 \
        cookieMgr->AddNative(uri, "cookiemgr.test"_ns, "/foo"_ns, name, value, \
                             false, false, true, INT64_MAX, &attrs,            \
                             nsICookie::SAMESITE_LAX, nsICookie::SCHEME_HTTP,  \
                             false, true, nullptr, getter_AddRefs(cv)),        \
        NS_ERROR_ILLEGAL_VALUE);                                               \
    EXPECT_TRUE(!!cv);                                                         \
    EXPECT_EQ(CookieValidation::Cast(cv)->Result(), error);                    \
  }

  TEST_INVALID_CHARS(" test invalid name"_ns, "test valid value"_ns,
                     nsICookieValidation::eRejectedInvalidCharName)
  TEST_INVALID_CHARS("test invalid name "_ns, "test valid value"_ns,
                     nsICookieValidation::eRejectedInvalidCharName)
  TEST_INVALID_CHARS("test valid name"_ns, " test invalid value"_ns,
                     nsICookieValidation::eRejectedInvalidCharValue)
  TEST_INVALID_CHARS("test valid name"_ns, "test invalid value "_ns,
                     nsICookieValidation::eRejectedInvalidCharValue)

#undef TEST_INVALID_CHARS
}

TEST(TestCookie, OnionSite)
{
  Preferences::SetBool("dom.securecontext.allowlist_onions", true);
  Preferences::SetBool("network.cookie.sameSite.laxByDefault", false);

  nsresult rv;
  nsCString cookie;

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  // .onion secure cookie tests
  SetACookie(cookieService, "http://123456789abcdef.onion/",
             "test=onion-security; secure");
  GetACookieNoHttp(cookieService, "https://123456789abcdef.onion/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=onion-security"));
  SetACookie(cookieService, "http://123456789abcdef.onion/",
             "test=onion-security2; secure");
  GetACookieNoHttp(cookieService, "http://123456789abcdef.onion/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=onion-security2"));
  SetACookie(cookieService, "https://123456789abcdef.onion/",
             "test=onion-security3; secure");
  GetACookieNoHttp(cookieService, "http://123456789abcdef.onion/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=onion-security3"));
  SetACookie(cookieService, "http://123456789abcdef.onion/",
             "test=onion-security4");
  GetACookieNoHttp(cookieService, "http://123456789abcdef.onion/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "test=onion-security4"));
}

TEST(TestCookie, HiddenPrefix)
{
  nsresult rv;
  nsCString cookie;

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  SetACookie(cookieService, "http://hiddenprefix.test/", "=__Host-test=a");
  GetACookie(cookieService, "http://hiddenprefix.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://hiddenprefix.test/", "=__Secure-test=a");
  GetACookie(cookieService, "http://hiddenprefix.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://hiddenprefix.test/", "=__Host-check");
  GetACookie(cookieService, "http://hiddenprefix.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://hiddenprefix.test/", "=__Secure-check");
  GetACookie(cookieService, "http://hiddenprefix.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));
}

TEST(TestCookie, BlockUnicode)
{
  Preferences::SetBool("network.cookie.blockUnicode", true);

  nsresult rv;
  nsCString cookie;

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  SetACookie(cookieService, "http://unicode.com/", "name=🍪");
  GetACookie(cookieService, "http://unicode.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  SetACookie(cookieService, "http://unicode.com/", "🍪=value");
  GetACookie(cookieService, "http://unicode.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_BE_NULL));

  Preferences::SetBool("network.cookie.blockUnicode", false);

  SetACookie(cookieService, "http://unicode.com/", "name=🍪");
  GetACookie(cookieService, "http://unicode.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "name=🍪"));

  nsCOMPtr<nsICookieManager> cookieMgr =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID);
  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  SetACookie(cookieService, "http://unicode.com/", "🍪=value");
  GetACookie(cookieService, "http://unicode.com/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "🍪=value"));

  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());
  Preferences::ClearUser("network.cookie.blockUnicode");
}

TEST(TestCookie, MaxAgeParser)
{
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "https://maxage.net");

  nsCOMPtr<nsIIOService> service = do_GetIOService();

  nsCOMPtr<nsIChannel> channel;
  (void)service->NewChannelFromURI(
      uri, nullptr, nsContentUtils::GetSystemPrincipal(),
      nsContentUtils::GetSystemPrincipal(), 0, nsIContentPolicy::TYPE_DOCUMENT,
      getter_AddRefs(channel));

  nsCOMPtr<nsIConsoleReportCollector> crc = do_QueryInterface(channel);

  CookieParser cp(crc, uri);

  int64_t value;
  EXPECT_FALSE(cp.ParseMaxAgeAttribute(""_ns, &value));

  EXPECT_TRUE(cp.ParseMaxAgeAttribute("0"_ns, &value));
  EXPECT_EQ(value, 0);

  EXPECT_TRUE(cp.ParseMaxAgeAttribute("1"_ns, &value));
  EXPECT_EQ(value, 1);

  EXPECT_TRUE(cp.ParseMaxAgeAttribute("1234"_ns, &value));
  EXPECT_EQ(value, 1234);

  EXPECT_TRUE(cp.ParseMaxAgeAttribute("00000000000000001234"_ns, &value));
  EXPECT_EQ(value, 1234);

  EXPECT_TRUE(cp.ParseMaxAgeAttribute("-1234"_ns, &value));
  EXPECT_EQ(value, INT64_MIN);

  {
    nsCString str;
    for (int i = 0; i < 1024; ++i) {
      str.Append("9");
    }
    EXPECT_TRUE(cp.ParseMaxAgeAttribute(str, &value));
    EXPECT_EQ(value, INT64_MAX);
  }

  EXPECT_FALSE(cp.ParseMaxAgeAttribute("1234a"_ns, &value));
  EXPECT_FALSE(cp.ParseMaxAgeAttribute("12a34"_ns, &value));
  EXPECT_FALSE(cp.ParseMaxAgeAttribute("12🌊34"_ns, &value));

  nsresult rv;
  nsCOMPtr<nsICookieManager> cookieMgr =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  SetACookie(cookieService, "http://maxage.net/", "a=1; max-age=1234");

  nsCString cookieStr;
  GetACookie(cookieService, "http://maxage.net/", cookieStr);
  EXPECT_TRUE(CheckResult(cookieStr.get(), MUST_EQUAL, "a=1"));

  nsTArray<RefPtr<nsICookie>> cookies;
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  EXPECT_EQ(cookies.Length(), (uint64_t)1);

  Cookie* cookie = static_cast<Cookie*>(cookies[0].get());
  EXPECT_FALSE(cookie->IsSession());

  SetACookie(cookieService, "http://maxage.net/", "a=1; max-age=-1");
  GetACookie(cookieService, "http://maxage.net/", cookieStr);
  EXPECT_TRUE(CheckResult(cookieStr.get(), MUST_EQUAL, ""));
}

// Minimal CookieStorage subclass to expose internals for testing.
class TestableCookieStorage final : public CookieStorage {
 public:
  static already_AddRefed<TestableCookieStorage> Create() {
    RefPtr<TestableCookieStorage> storage = new TestableCookieStorage();
    storage->Init();
    return storage.forget();
  }

  using CookieStorage::AddCookieToList;
  using CookieStorage::mHostTable;

  void StaleCookies(const nsTArray<RefPtr<Cookie>>&, int64_t) override {}
  void Close() override {}
  void EnsureInitialized() override {}

 protected:
  const char* NotificationTopic() const override { return "test-cookie"; }
  void NotifyChangedInternal(nsICookieNotification*, bool) override {}
  void RemoveAllInternal() override {}
  void RemoveCookieFromDB(Cookie*) override {}
  void StoreCookie(const nsACString&, const OriginAttributes&,
                   Cookie*) override {}

 private:
  ~TestableCookieStorage() = default;
  void CollectCookieJarSizeData() override {}
  already_AddRefed<nsIArray> PurgeCookies(int64_t, uint16_t, int64_t) override {
    return nullptr;
  }
};

// Regression test for Bug 2022369: RemoveOlderCookiesByBytes must re-lookup the
// CookieEntry after pass 1 (insecure removal) because it may have been freed if
// pass 1 emptied the entry.
TEST(TestCookie, RemoveOlderCookiesByBytesEntryFreed)
{
  RefPtr<TestableCookieStorage> storage = TestableCookieStorage::Create();

  nsAutoCString baseDomain("localhost"_ns);
  OriginAttributes attrs;
  attrs.mPartitionKey = u"(http,example.com)"_ns;

  // Add a single insecure cookie to the partition.
  CookieStruct cookieData;
  cookieData.name() = "B"_ns;
  cookieData.value() = "xx"_ns;
  cookieData.host() = "localhost"_ns;
  cookieData.path() = "/"_ns;
  cookieData.expiryInMSec() = PR_Now() / PR_USEC_PER_MSEC + 86400 * 1000;
  cookieData.lastAccessedInUSec() = 0;
  cookieData.creationTimeInUSec() = 0;
  cookieData.updateTimeInUSec() = 0;
  cookieData.isHttpOnly() = false;
  cookieData.isSession() = true;
  cookieData.isSecure() = false;
  cookieData.isPartitioned() = true;
  cookieData.sameSite() = nsICookie::SAMESITE_LAX;
  cookieData.schemeMap() = 2;

  RefPtr<Cookie> cookie = Cookie::Create(cookieData, attrs);
  ASSERT_TRUE(cookie);

  storage->AddCookieToList(baseDomain, attrs, cookie);

  CookieKey key(baseDomain, attrs);
  CookieEntry* entry = storage->mHostTable.GetEntry(key);
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->GetCookies().Length(), 1u);

  uint32_t cookieBytes = cookie->NameAndValueBytes();
  ASSERT_GT(cookieBytes, 0u);

  // Ask to remove more bytes than the single insecure cookie provides.
  // Pass 1 removes the insecure cookie (the only one), freeing the entry.
  // Without the fix, pass 2 would dereference the freed entry (UAF).
  nsCOMPtr<nsIArray> purgedList;
  storage->RemoveOlderCookiesByBytes(entry, cookieBytes + 100, purgedList);

  // The entry should be gone from the hash table.
  EXPECT_FALSE(storage->mHostTable.GetEntry(key));
}

// Regression test for Bug 2041131: HasCookiesForSite must find cookies stored
// under any OriginAttributes matching the pattern (containers, partitionKey,
// …), not just the default-OA bucket.
TEST(TestCookie, HasCookiesForSite)
{
  RefPtr<TestableCookieStorage> storage = TestableCookieStorage::Create();

  nsAutoCString baseDomain("example.com"_ns);

  const int64_t nowInMSec = PR_Now() / PR_USEC_PER_MSEC;
  const int64_t kFuture = nowInMSec + 86400 * 1000;
  const int64_t kPast = nowInMSec - 1;

  auto addCookie = [&](const OriginAttributes& aAttrs, int64_t aExpiryInMSec) {
    CookieStruct cookieData;
    cookieData.name() = "c"_ns;
    cookieData.value() = "v"_ns;
    cookieData.host() = "example.com"_ns;
    cookieData.path() = "/"_ns;
    cookieData.expiryInMSec() = aExpiryInMSec;
    cookieData.lastAccessedInUSec() = 0;
    cookieData.creationTimeInUSec() = 0;
    cookieData.updateTimeInUSec() = 0;
    cookieData.isHttpOnly() = false;
    cookieData.isSession() = true;
    cookieData.isSecure() = false;
    cookieData.isPartitioned() = false;
    cookieData.sameSite() = nsICookie::SAMESITE_LAX;
    cookieData.schemeMap() = 2;

    RefPtr<Cookie> cookie = Cookie::Create(cookieData, aAttrs);
    ASSERT_TRUE(cookie);
    storage->AddCookieToList(baseDomain, aAttrs, cookie);
  };

  OriginAttributesPattern nonPbPattern;
  nonPbPattern.mPrivateBrowsingId.Construct(0);

  // No cookies at all → false.
  EXPECT_FALSE(storage->HasCookies(baseDomain, nonPbPattern));
  EXPECT_FALSE(storage->HasCookies("other.test"_ns, nonPbPattern));

  // Cookie in the default-OA bucket → true.
  OriginAttributes defaultAttrs;
  addCookie(defaultAttrs, kFuture);
  EXPECT_TRUE(storage->HasCookies(baseDomain, nonPbPattern));

  // Different base domain still gets false.
  EXPECT_FALSE(storage->HasCookies("other.test"_ns, nonPbPattern));

  // Cookie only in a container (userContextId != 0) → true. This is the
  // direct regression case from Bug 2041131.
  RefPtr<TestableCookieStorage> containerOnly = TestableCookieStorage::Create();
  storage = containerOnly;
  OriginAttributes containerAttrs;
  containerAttrs.mUserContextId = 1;
  addCookie(containerAttrs, kFuture);
  EXPECT_TRUE(storage->HasCookies(baseDomain, nonPbPattern));

  // PB-only cookie must NOT be visible to a non-PB pattern.
  RefPtr<TestableCookieStorage> pbOnly = TestableCookieStorage::Create();
  storage = pbOnly;
  OriginAttributes pbAttrs;
  pbAttrs.mPrivateBrowsingId = 1;
  addCookie(pbAttrs, kFuture);
  EXPECT_FALSE(storage->HasCookies(baseDomain, nonPbPattern));
  // And IS visible to a PB-matching pattern.
  OriginAttributesPattern pbPattern;
  pbPattern.mPrivateBrowsingId.Construct(1);
  EXPECT_TRUE(storage->HasCookies(baseDomain, pbPattern));

  // Partitioned-only cookie (non-default partitionKey) → true under
  // privateBrowsingId=0 pattern.
  RefPtr<TestableCookieStorage> partitionedOnly =
      TestableCookieStorage::Create();
  storage = partitionedOnly;
  OriginAttributes partAttrs;
  partAttrs.mPartitionKey = u"(http,other.test)"_ns;
  addCookie(partAttrs, kFuture);
  EXPECT_TRUE(storage->HasCookies(baseDomain, nonPbPattern));

  RefPtr<TestableCookieStorage> expiredOnly = TestableCookieStorage::Create();
  storage = expiredOnly;
  addCookie(defaultAttrs, kPast);
  EXPECT_FALSE(storage->HasCookies(baseDomain, nonPbPattern));

  OriginAttributes otherAttrs;
  otherAttrs.mUserContextId = 2;
  addCookie(otherAttrs, kFuture);
  EXPECT_TRUE(storage->HasCookies(baseDomain, nonPbPattern));
}

namespace mozilla::net {

TEST(TestCookieDBWriteQueue, Coalesce)
{
  using OpType = CookieDBWriteQueue::OpType;

  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Remove, OpType::Insert),
            Some(OpType::RemoveAndInsert));
  EXPECT_EQ(
      CookieDBWriteQueue::Coalesce(OpType::RemoveAndInsert, OpType::Insert),
      Some(OpType::RemoveAndInsert));

  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Insert, OpType::Update),
            Some(OpType::Insert));
  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Update, OpType::Update),
            Some(OpType::Update));
  EXPECT_EQ(
      CookieDBWriteQueue::Coalesce(OpType::RemoveAndInsert, OpType::Update),
      Some(OpType::RemoveAndInsert));

  // The insertion is still in this batch, so it never reached the disk and
  // the row cancels out entirely.
  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Insert, OpType::Remove),
            Nothing());

  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Update, OpType::Remove),
            Some(OpType::Remove));
  EXPECT_EQ(CookieDBWriteQueue::Coalesce(OpType::Remove, OpType::Remove),
            Some(OpType::Remove));
  EXPECT_EQ(
      CookieDBWriteQueue::Coalesce(OpType::RemoveAndInsert, OpType::Remove),
      Some(OpType::Remove));
}

}  // namespace mozilla::net
