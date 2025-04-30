/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TestCommon.h"
#include "gtest/gtest.h"
#include "nsContentUtils.h"
#include "nsICookieService.h"
#include "nsICookieManager.h"
#include "nsICookie.h"
#include <stdio.h>
#include "plstr.h"
#include "nsNetUtil.h"
#include "nsIChannel.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsServiceManagerUtils.h"
#include "nsNetCID.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/Preferences.h"
#include "mozilla/Unused.h"
#include "mozilla/net/CookieJarSettings.h"
#include "Cookie.h"
#include "CookieParser.h"
#include "nsIURI.h"
#include "nsIConsoleReportCollector.h"

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
  Unused << service->NewChannelFromURI(
      uri, nullptr, nsContentUtils::GetSystemPrincipal(),
      nsContentUtils::GetSystemPrincipal(), 0, nsIContentPolicy::TYPE_DOCUMENT,
      getter_AddRefs(channel));

  Unused << aCookieService->GetCookieStringFromHttp(uri, channel, aCookie);
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
  nsresult rv = NS_NewDOMDocument(getter_AddRefs(document),
                                  u""_ns,   // aNamespaceURI
                                  u""_ns,   // aQualifiedName
                                  nullptr,  // aDoctype
                                  uri, uri, principal,
                                  false,    // aLoadedAsData
                                  nullptr,  // aEventObject
                                  DocumentFlavor::HTML);
  Unused << NS_WARN_IF(NS_FAILED(rv));

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
             "test=domain; domain=domain.com");
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

  // test the handling of VALUE-only cookies (see bug 169091),
  // i.e. "six" should assume an empty NAME, which allows other VALUE-only
  // cookies to overwrite it
  SetACookie(cookieService, "http://parser.test/", "six");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "six"));
  SetACookie(cookieService, "http://parser.test/", "seven");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "seven"));
  SetACookie(cookieService, "http://parser.test/", " =eight");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_EQUAL, "eight"));
  SetACookie(cookieService, "http://parser.test/", "test=six");
  GetACookie(cookieService, "http://parser.test/", cookie);
  EXPECT_TRUE(CheckResult(cookie.get(), MUST_CONTAIN, "test=six"));

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
             "__Host-test=test; secure");
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
  // add some cookies
  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->AddNative(uri,
                            "cookiemgr.test"_ns,  // domain
                            "/foo"_ns,            // path
                            "test1"_ns,           // name
                            "yes"_ns,             // value
                            false,                // is secure
                            false,                // is httponly
                            true,                 // is session
                            INT64_MAX,            // expiry time
                            &attrs,               // originAttributes
                            nsICookie::SAMESITE_NONE, nsICookie::SCHEME_HTTPS,
                            false,    // is partitioned
                            nullptr,  // operation ID
                            [](CookieStruct&) -> bool { return true; })));
  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->AddNative(uri,
                            "cookiemgr.test"_ns,             // domain
                            "/foo"_ns,                       // path
                            "test2"_ns,                      // name
                            "yes"_ns,                        // value
                            false,                           // is secure
                            true,                            // is httponly
                            true,                            // is session
                            PR_Now() / PR_USEC_PER_SEC + 2,  // expiry time
                            &attrs,                          // originAttributes
                            nsICookie::SAMESITE_NONE, nsICookie::SCHEME_HTTPS,
                            false,    // is partitioned
                            nullptr,  // operation ID
                            [](CookieStruct&) -> bool { return true; })));
  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->AddNative(uri,
                            "new.domain"_ns,  // domain
                            "/rabbit"_ns,     // path
                            "test3"_ns,       // name
                            "yes"_ns,         // value
                            false,            // is secure
                            false,            // is httponly
                            true,             // is session
                            INT64_MAX,        // expiry time
                            &attrs,           // originAttributes
                            nsICookie::SAMESITE_NONE, nsICookie::SCHEME_HTTPS,
                            false,    // is partitioned
                            nullptr,  // operation ID
                            [](CookieStruct&) -> bool { return true; })));
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
  // check CountCookiesFromHost()
  uint32_t hostCookies = 0;
  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->CountCookiesFromHost("cookiemgr.test"_ns, &hostCookies)));
  EXPECT_EQ(hostCookies, 2u);
  // check CookieExistsNative() using the third cookie
  bool found;
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->CookieExistsNative(
      "new.domain"_ns, "/rabbit"_ns, "test3"_ns, &attrs, &found)));
  EXPECT_TRUE(found);

  // sleep four seconds, to make sure the second cookie has expired
  PR_Sleep(4 * PR_TicksPerSecond());
  // check that both CountCookiesFromHost() and CookieExistsNative() count the
  // expired cookie
  EXPECT_TRUE(NS_SUCCEEDED(
      cookieMgr2->CountCookiesFromHost("cookiemgr.test"_ns, &hostCookies)));
  EXPECT_EQ(hostCookies, 2u);
  EXPECT_TRUE(NS_SUCCEEDED(cookieMgr2->CookieExistsNative(
      "cookiemgr.test"_ns, "/foo"_ns, "test2"_ns, &attrs, &found)));
  EXPECT_TRUE(found);
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
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_NONE);
    } else if (name.EqualsLiteral("unspecified")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_NONE);
    } else if (name.EqualsLiteral("empty")) {
      EXPECT_TRUE(sameSiteAttr == nsICookie::SAMESITE_NONE);
    } else if (name.EqualsLiteral("bogus")) {
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

TEST(TestCookie, SameSiteLax)
{
  Preferences::SetBool("network.cookie.sameSite.laxByDefault", true);

  nsresult rv;

  nsCOMPtr<nsICookieService> cookieService =
      do_GetService(kCookieServiceCID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsICookieManager> cookieMgr =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID, &rv);
  ASSERT_NS_SUCCEEDED(rv);

  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  SetACookie(cookieService, "http://samesite.test", "unset=yes");

  nsTArray<RefPtr<nsICookie>> cookies;
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  EXPECT_EQ(cookies.Length(), (uint64_t)1);

  Cookie* cookie = static_cast<Cookie*>(cookies[0].get());
  EXPECT_EQ(cookie->RawSameSite(), nsICookie::SAMESITE_NONE);
  EXPECT_EQ(cookie->SameSite(), nsICookie::SAMESITE_LAX);

  Preferences::SetCString("network.cookie.sameSite.laxByDefault.disabledHosts",
                          "foo.com,samesite.test,bar.net");

  EXPECT_NS_SUCCEEDED(cookieMgr->RemoveAll());

  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  EXPECT_EQ(cookies.Length(), (uint64_t)0);

  SetACookie(cookieService, "http://samesite.test", "unset=yes");

  cookies.SetLength(0);
  EXPECT_NS_SUCCEEDED(cookieMgr->GetCookies(cookies));
  EXPECT_EQ(cookies.Length(), (uint64_t)1);

  cookie = static_cast<Cookie*>(cookies[0].get());
  EXPECT_EQ(cookie->RawSameSite(), nsICookie::SAMESITE_NONE);
  EXPECT_EQ(cookie->SameSite(), nsICookie::SAMESITE_LAX);
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
  Unused << service->NewChannelFromURI(
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
