/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>
#include <string.h>

#include <iterator>

#include "gtest/gtest.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsComponentManagerUtils.h"
#include "nsIContentSecurityPolicy.h"
#include "nsNetUtil.h"
#include "nsStringFwd.h"

/*
 * Testing the parser is non trivial, especially since we can not call
 * parser functionality directly in compiled code tests.
 * All the tests (except the fuzzy tests at the end) follow the same schemata:
 *   a) create an nsIContentSecurityPolicy object
 *   b) set the selfURI in SetRequestContextWithPrincipal
 *   c) append one or more policies by calling AppendPolicy
 *   d) check if the policy count is correct by calling GetPolicyCount
 *   e) compare the result of the policy with the expected output
 *      using the struct PolicyTest;
 *
 * In general we test:
 * a) policies that the parser should accept
 * b) policies that the parser should reject
 * c) policies that are randomly generated (fuzzy tests)
 *
 * Please note that fuzzy tests are *DISABLED* by default and shold only
 * be run *OFFLINE* whenever code in nsCSPParser changes.
 * To run fuzzy tests, flip RUN_OFFLINE_TESTS to 1.
 *
 */

#define RUN_OFFLINE_TESTS 0

/*
 * Offline tests are separated in three different groups:
 *  * TestFuzzyPolicies - complete random ASCII input
 *  * TestFuzzyPoliciesIncDir - a directory name followed by random ASCII
 *  * TestFuzzyPoliciesIncDirLimASCII - a directory name followed by limited
 * ASCII which represents more likely user input.
 *
 *  We run each of this categories |kFuzzyRuns| times.
 */

#if RUN_OFFLINE_TESTS
static const uint32_t kFuzzyRuns = 10000;
#endif

// For fuzzy testing we actually do not care about the output,
// we just want to make sure that the parser can handle random
// input, therefore we use kFuzzyExpectedPolicyCount to return early.
static const uint32_t kFuzzyExpectedPolicyCount = 111;

static const uint32_t kMaxPolicyLength = 96;

struct PolicyTest {
  char policy[kMaxPolicyLength];
  char expectedResult[kMaxPolicyLength];
};

// Sets a bool pref for its lifetime, and restores its value afterwards.
class MOZ_RAII AutoBoolPref {
 public:
  AutoBoolPref(const char* aName, bool aValue)
      : mName(aName), mOldValue(mozilla::Preferences::GetBool(aName)) {
    mozilla::Preferences::SetBool(aName, aValue);
  }

  ~AutoBoolPref() { mozilla::Preferences::SetBool(mName, mOldValue); }

 private:
  const char* mName;
  bool mOldValue;
};

constexpr auto kUrlHashesEnabledPrefName = "security.csp.url-hashes.enabled";
constexpr auto kStrictDynamicUrlEnabledPrefName =
    "security.csp.strict-dynamic-url.enabled";

nsresult runTest(
    uint32_t aExpectedPolicyCount,  // this should be 0 for policies which
                                    // should fail to parse
    const char* aPolicy, const char* aExpectedResult) {
  nsresult rv;

  // we init the csp with http://www.selfuri.com
  nsCOMPtr<nsIURI> selfURI;
  rv = NS_NewURI(getter_AddRefs(selfURI), "http://www.selfuri.com");
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> selfURIPrincipal;
  mozilla::OriginAttributes attrs;
  selfURIPrincipal =
      mozilla::BasePrincipal::CreateContentPrincipal(selfURI, attrs);
  NS_ENSURE_TRUE(selfURIPrincipal, NS_ERROR_FAILURE);

  // create a CSP object
  nsCOMPtr<nsIContentSecurityPolicy> csp =
      do_CreateInstance(NS_CSPCONTEXT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // for testing the parser we only need to set a principal which is needed
  // to translate the keyword 'self' into an actual URI.
  rv = csp->SetRequestContextWithPrincipal(selfURIPrincipal, selfURI, ""_ns, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  // append a policy
  nsString policyStr;
  policyStr.AssignASCII(aPolicy);
  rv = csp->AppendPolicy(policyStr, false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  // when executing fuzzy tests we do not care about the actual output
  // of the parser, we just want to make sure that the parser is not crashing.
  if (aExpectedPolicyCount == kFuzzyExpectedPolicyCount) {
    return NS_OK;
  }

  // verify that the expected number of policies exists
  uint32_t actualPolicyCount;
  rv = csp->GetPolicyCount(&actualPolicyCount);
  NS_ENSURE_SUCCESS(rv, rv);
  if (actualPolicyCount != aExpectedPolicyCount) {
    EXPECT_TRUE(false)
        << "Actual policy count not equal to expected policy count ("
        << actualPolicyCount << " != " << aExpectedPolicyCount
        << ") for policy: " << aPolicy;
    return NS_ERROR_UNEXPECTED;
  }

  // if the expected policy count is 0, we can return, because
  // we can not compare any output anyway. Used when parsing
  // errornous policies.
  if (aExpectedPolicyCount == 0) {
    return NS_OK;
  }

  // compare the parsed policy against the expected result
  nsString parsedPolicyStr;
  // checking policy at index 0, which is the one what we appended.
  rv = csp->GetPolicyString(0, parsedPolicyStr);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!NS_ConvertUTF16toUTF8(parsedPolicyStr).EqualsASCII(aExpectedResult)) {
    EXPECT_TRUE(false) << "Actual policy does not match expected policy ("
                       << NS_ConvertUTF16toUTF8(parsedPolicyStr).get()
                       << " != " << aExpectedResult << ")";
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

// ============================= run Tests ========================

nsresult runTestSuite(const PolicyTest* aPolicies, uint32_t aPolicyCount,
                      uint32_t aExpectedPolicyCount) {
  nsresult rv;

  for (uint32_t i = 0; i < aPolicyCount; i++) {
    rv = runTest(aExpectedPolicyCount, aPolicies[i].policy,
                 aPolicies[i].expectedResult);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// ============================= TestDirectives ========================

TEST(CSPParser, Directives)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "connect-src xn--mnchen-3ya.de",
      "connect-src http://xn--mnchen-3ya.de"},
    { "default-src http://www.example.com",
      "default-src http://www.example.com" },
    { "script-src http://www.example.com",
      "script-src http://www.example.com" },
    { "object-src http://www.example.com",
      "object-src http://www.example.com" },
    { "style-src http://www.example.com",
      "style-src http://www.example.com" },
    { "img-src http://www.example.com",
      "img-src http://www.example.com" },
    { "media-src http://www.example.com",
      "media-src http://www.example.com" },
    { "frame-src http://www.example.com",
      "frame-src http://www.example.com" },
    { "font-src http://www.example.com",
      "font-src http://www.example.com" },
    { "connect-src http://www.example.com",
      "connect-src http://www.example.com" },
    { "report-uri http://www.example.com",
      "report-uri http://www.example.com/" },
    { "script-src 'nonce-correctscriptnonce'",
      "script-src 'nonce-correctscriptnonce'" },
    { "script-src 'nonce-a'",
      "script-src 'nonce-a'" },
    { "script-src 'sha256-a'",
      "script-src 'sha256-a'" },
    { "script-src 'sha256-siVR8vAcqP06h2ppeNwqgjr0yZ6yned4X2VF84j4GmI='",
      "script-src 'sha256-siVR8vAcqP06h2ppeNwqgjr0yZ6yned4X2VF84j4GmI='" },
    { "script-src 'nonce-foo' 'unsafe-inline' ",
      "script-src 'nonce-foo' 'unsafe-inline'" },
    { "script-src 'nonce-foo' 'strict-dynamic' 'unsafe-inline' https:  ",
      "script-src 'nonce-foo' 'strict-dynamic' 'unsafe-inline' https:" },
    { "script-src 'nonce-foo' 'strict-dynamic' 'unsafe-inline' 'report-sample' https:  ",
      "script-src 'nonce-foo' 'strict-dynamic' 'unsafe-inline' 'report-sample' https:" },
    { "default-src 'sha256-siVR8' 'strict-dynamic' 'unsafe-inline' https:  ",
      "default-src 'sha256-siVR8' 'strict-dynamic' 'unsafe-inline' https:" },
    { "worker-src https://example.com",
      "worker-src https://example.com" },
    { "worker-src http://worker.com; frame-src http://frame.com; child-src http://child.com",
      "worker-src http://worker.com; frame-src http://frame.com; child-src http://child.com" },
    { "script-src 'unsafe-allow-redirects' http://example.com",
      "script-src http://example.com"},
    { "require-trusted-types-for 'script'",
      "require-trusted-types-for 'script'" },
    { "require-trusted-types-for 'script' invalid",
      "require-trusted-types-for 'script' invalid" }, // bug 1956731
    { "require-trusted-types-for 'script' 'invalid'",
      "require-trusted-types-for 'script' 'invalid'" }, // bug 1956731
    { "trusted-types somePolicyName", "trusted-types somePolicyName" },
    { "trusted-types somePolicyName anotherPolicyName 1 - # = _ / @ . % *",
      "trusted-types somePolicyName anotherPolicyName 1 - # = _ / @ . % *" },
    { "trusted-types $", "trusted-types $" }, // bug 1935434
    { "trusted-types 'invalid'", "trusted-types 'invalid'" }, // bug 1935434
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ======================= TestDirectivesUrlHashes ===================

TEST(CSPParser, DirectivesUrlHashes)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'url-sha256-idUt1lkA7P9ehz7Yz1P5USBar6neNbzkUPH+WQoM52A='",
      "script-src 'url-sha256-idUt1lkA7P9ehz7Yz1P5USBar6neNbzkUPH+WQoM52A='" },
    { "script-src-elem 'url-sha256-abc'",
      "script-src-elem 'url-sha256-abc'" },
    { "default-src 'url-sha256-abc'",
      "default-src 'url-sha256-abc'" },
    { "worker-src 'url-sha256-abc'",
      "worker-src 'url-sha256-abc'" },
    { "script-src 'url-sha384-abc' 'url-sha512-abc'",
      "script-src 'url-sha384-abc' 'url-sha512-abc'" },
    { "script-src 'nonce-foo' 'sha256-abc' 'url-sha256-abc' 'self'",
      "script-src 'nonce-foo' 'sha256-abc' 'url-sha256-abc' 'self'" },
    // base64url characters are serialized in base64, like for hash-source.
    { "script-src 'url-sha256-a-b_c='",
      "script-src 'url-sha256-a+b/c='" },
      // clang-format on
  };

  static const PolicyTest policiesWithoutPref[] = {
      // clang-format off
    { "script-src 'url-sha256-idUt1lkA7P9ehz7Yz1P5USBar6neNbzkUPH+WQoM52A='",
      "script-src 'none'" },
    { "script-src-elem 'url-sha256-abc'",
      "script-src-elem 'none'" },
    { "default-src 'url-sha256-abc'",
      "default-src 'none'" },
    { "worker-src 'url-sha256-abc'",
      "worker-src 'none'" },
    { "script-src 'url-sha384-abc' 'url-sha512-abc'",
      "script-src 'none'" },
    { "script-src 'nonce-foo' 'sha256-abc' 'url-sha256-abc' 'self'",
      "script-src 'nonce-foo' 'sha256-abc' 'self'" },
    { "script-src 'url-sha256-a-b_c='",
      "script-src 'none'" },
      // clang-format on
  };

  {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, true);
    ASSERT_NS_SUCCEEDED(runTestSuite(policies, std::size(policies), 1));
  }
  {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, false);
    ASSERT_NS_SUCCEEDED(
        runTestSuite(policiesWithoutPref, std::size(policiesWithoutPref), 1));
  }
}

// ============================= TestKeywords ========================

TEST(CSPParser, Keywords)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'self'",
      "script-src 'self'" },
    { "script-src 'unsafe-inline'",
      "script-src 'unsafe-inline'" },
    { "script-src 'unsafe-eval'",
      "script-src 'unsafe-eval'" },
    { "script-src 'unsafe-inline' 'unsafe-eval'",
      "script-src 'unsafe-inline' 'unsafe-eval'" },
    { "script-src 'none'",
      "script-src 'none'" },
    { "script-src 'wasm-unsafe-eval'",
      "script-src 'wasm-unsafe-eval'" },
    { "img-src 'none'; script-src 'unsafe-eval' 'unsafe-inline'; default-src 'self'",
      "img-src 'none'; script-src 'unsafe-eval' 'unsafe-inline'; default-src 'self'" },
    { "trusted-types somePolicyName 'allow-duplicates'",
      "trusted-types somePolicyName 'allow-duplicates'" },
    { "trusted-types 'none'", "trusted-types 'none'" },
    { "trusted-types", "trusted-types 'none'" },
    { "trusted-types 'none' somePolicyName", "trusted-types somePolicyName" },
    { "trusted-types 'none' 'none'", "trusted-types 'none'" },
    { "trusted-types 'none' 'allow-duplicates'", "trusted-types 'allow-duplicates'" },
    { "trusted-types 'none' *", "trusted-types *" },
    { "trusted-types *", "trusted-types *" },
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ===================== TestKeywordsStrictDynamicUrl =================

TEST(CSPParser, KeywordsStrictDynamicUrl)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'strict-dynamic-url'",
      "script-src 'strict-dynamic-url'" },
    { "script-src 'nonce-foo' 'strict-dynamic-url' 'unsafe-inline' https:",
      "script-src 'nonce-foo' 'strict-dynamic-url' 'unsafe-inline' https:" },
    { "script-src 'strict-dynamic' 'strict-dynamic-url'",
      "script-src 'strict-dynamic' 'strict-dynamic-url'" },
    { "script-src 'url-sha256-abc' 'strict-dynamic-url'",
      "script-src 'url-sha256-abc' 'strict-dynamic-url'" },
    { "default-src 'sha256-siVR8' 'strict-dynamic-url' https:",
      "default-src 'sha256-siVR8' 'strict-dynamic-url' https:" },
    // Like 'strict-dynamic', it's kept outside of script directives, with a
    // warning.
    { "img-src 'strict-dynamic-url'",
      "img-src 'strict-dynamic-url'" },
      // clang-format on
  };

  static const PolicyTest policiesWithoutPrefs[] = {
      // clang-format off
    { "script-src 'strict-dynamic-url'",
      "script-src 'none'" },
    { "script-src 'nonce-foo' 'strict-dynamic-url' 'unsafe-inline' https:",
      "script-src 'nonce-foo' 'unsafe-inline' https:" },
    { "script-src 'strict-dynamic' 'strict-dynamic-url'",
      "script-src 'strict-dynamic'" },
    { "script-src 'url-sha256-abc' 'strict-dynamic-url'",
      "script-src 'none'" },
    { "default-src 'sha256-siVR8' 'strict-dynamic-url' https:",
      "default-src 'sha256-siVR8' https:" },
    { "img-src 'strict-dynamic-url'",
      "img-src 'none'" },
      // clang-format on
  };

  {
    AutoBoolPref strictDynamicUrl(kStrictDynamicUrlEnabledPrefName, true);
    AutoBoolPref urlHashes(kUrlHashesEnabledPrefName, true);
    ASSERT_NS_SUCCEEDED(runTestSuite(policies, std::size(policies), 1));
  }
  {
    AutoBoolPref strictDynamicUrl(kStrictDynamicUrlEnabledPrefName, false);
    AutoBoolPref urlHashes(kUrlHashesEnabledPrefName, false);
    ASSERT_NS_SUCCEEDED(
        runTestSuite(policiesWithoutPrefs, std::size(policiesWithoutPrefs), 1));
  }
}

// =================== TestIgnoreUpperLowerCasePolicies ==============

TEST(CSPParser, IgnoreUpperLowerCasePolicies)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'SELF'",
      "script-src 'self'" },
    { "sCriPt-src 'Unsafe-Inline'",
      "script-src 'unsafe-inline'" },
    { "SCRIPT-src 'unsafe-eval'",
      "script-src 'unsafe-eval'" },
    { "default-SRC 'unsafe-inline' 'unsafe-eval'",
      "default-src 'unsafe-inline' 'unsafe-eval'" },
    { "script-src 'NoNe'",
      "script-src 'none'" },
    { "img-sRc 'noNe'; scrIpt-src 'unsafe-EVAL' 'UNSAFE-inline'; deFAULT-src 'Self'",
      "img-src 'none'; script-src 'unsafe-eval' 'unsafe-inline'; default-src 'self'" },
    { "default-src HTTP://www.example.com",
      "default-src http://www.example.com" },
    { "default-src HTTP://WWW.EXAMPLE.COM",
      "default-src http://www.example.com" },
    { "default-src HTTPS://*.example.COM",
      "default-src https://*.example.com" },
    { "script-src 'none' test.com;",
      "script-src http://test.com" },
    { "script-src 'NoNCE-correctscriptnonce'",
      "script-src 'nonce-correctscriptnonce'" },
    { "script-src 'NoncE-NONCENEEDSTOBEUPPERCASE'",
      "script-src 'nonce-NONCENEEDSTOBEUPPERCASE'" },
    { "script-src 'SHA256-siVR8vAcqP06h2ppeNwqgjr0yZ6yned4X2VF84j4GmI='",
      "script-src 'sha256-siVR8vAcqP06h2ppeNwqgjr0yZ6yned4X2VF84j4GmI='" },
    { "upgrade-INSECURE-requests",
      "upgrade-insecure-requests" },
    { "sanDBox alloW-foRMs",
      "sandbox allow-forms"},
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ================ TestIgnoreUpperLowerCaseUrlHashes ================

TEST(CSPParser, IgnoreUpperLowerCaseUrlHashes)
{
  static const PolicyTest policies[] = {
      // clang-format off
    // The prefix and the algorithm are serialized in lowercase, but the hash
    // is case-sensitive.
    { "script-src 'URL-SHA256-AbC'",
      "script-src 'url-sha256-AbC'" },
    { "script-src 'Url-Sha384-AbC'",
      "script-src 'url-sha384-AbC'" },
      // clang-format on
  };

  static const PolicyTest policiesWithoutPref[] = {
      // clang-format off
    { "script-src 'URL-SHA256-AbC'",
      "script-src 'none'" },
    { "script-src 'Url-Sha384-AbC'",
      "script-src 'none'" },
      // clang-format on
  };

  {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, true);
    ASSERT_NS_SUCCEEDED(runTestSuite(policies, std::size(policies), 1));
  }
  {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, false);
    ASSERT_NS_SUCCEEDED(
        runTestSuite(policiesWithoutPref, std::size(policiesWithoutPref), 1));
  }
}

// ============= TestIgnoreUpperLowerCaseStrictDynamicUrl =============

TEST(CSPParser, IgnoreUpperLowerCaseStrictDynamicUrl)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'STRICT-DYNAMIC-URL'",
      "script-src 'strict-dynamic-url'" },
    { "script-src 'Strict-Dynamic-Url'",
      "script-src 'strict-dynamic-url'" },
      // clang-format on
  };

  static const PolicyTest policiesWithoutPref[] = {
      // clang-format off
    { "script-src 'STRICT-DYNAMIC-URL'",
      "script-src 'none'" },
    { "script-src 'Strict-Dynamic-Url'",
      "script-src 'none'" },
      // clang-format on
  };

  {
    AutoBoolPref pref(kStrictDynamicUrlEnabledPrefName, true);
    ASSERT_NS_SUCCEEDED(runTestSuite(policies, std::size(policies), 1));
  }
  {
    AutoBoolPref pref(kStrictDynamicUrlEnabledPrefName, false);
    ASSERT_NS_SUCCEEDED(
        runTestSuite(policiesWithoutPref, std::size(policiesWithoutPref), 1));
  }
}

// ========================= TestPaths ===============================

TEST(CSPParser, Paths)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src http://www.example.com",
      "script-src http://www.example.com" },
    { "script-src http://www.example.com/",
      "script-src http://www.example.com/" },
    { "script-src http://www.example.com/path-1",
      "script-src http://www.example.com/path-1" },
    { "script-src http://www.example.com/path-1/",
      "script-src http://www.example.com/path-1/" },
    { "script-src http://www.example.com/path-1/path_2",
      "script-src http://www.example.com/path-1/path_2" },
    { "script-src http://www.example.com/path-1/path_2/",
      "script-src http://www.example.com/path-1/path_2/" },
    { "script-src http://www.example.com/path-1/path_2/file.js",
      "script-src http://www.example.com/path-1/path_2/file.js" },
    { "script-src http://www.example.com/path-1/path_2/file_1.js",
      "script-src http://www.example.com/path-1/path_2/file_1.js" },
    { "script-src http://www.example.com/path-1/path_2/file-2.js",
      "script-src http://www.example.com/path-1/path_2/file-2.js" },
    { "script-src http://www.example.com/path-1/path_2/f.js",
      "script-src http://www.example.com/path-1/path_2/f.js" },
    { "script-src http://www.example.com:88",
      "script-src http://www.example.com:88" },
    { "script-src http://www.example.com:88/",
      "script-src http://www.example.com:88/" },
    { "script-src http://www.example.com:88/path-1",
      "script-src http://www.example.com:88/path-1" },
    { "script-src http://www.example.com:88/path-1/",
      "script-src http://www.example.com:88/path-1/" },
    { "script-src http://www.example.com:88/path-1/path_2",
      "script-src http://www.example.com:88/path-1/path_2" },
    { "script-src http://www.example.com:88/path-1/path_2/",
      "script-src http://www.example.com:88/path-1/path_2/" },
    { "script-src http://www.example.com:88/path-1/path_2/file.js",
      "script-src http://www.example.com:88/path-1/path_2/file.js" },
    { "script-src http://www.example.com:*",
      "script-src http://www.example.com:*" },
    { "script-src http://www.example.com:*/",
      "script-src http://www.example.com:*/" },
    { "script-src http://www.example.com:*/path-1",
      "script-src http://www.example.com:*/path-1" },
    { "script-src http://www.example.com:*/path-1/",
      "script-src http://www.example.com:*/path-1/" },
    { "script-src http://www.example.com:*/path-1/path_2",
      "script-src http://www.example.com:*/path-1/path_2" },
    { "script-src http://www.example.com:*/path-1/path_2/",
      "script-src http://www.example.com:*/path-1/path_2/" },
    { "script-src http://www.example.com:*/path-1/path_2/file.js",
      "script-src http://www.example.com:*/path-1/path_2/file.js" },
    { "script-src http://www.example.com#foo",
      "script-src http://www.example.com" },
    { "script-src http://www.example.com?foo=bar",
      "script-src http://www.example.com" },
    { "script-src http://www.example.com:8888#foo",
      "script-src http://www.example.com:8888" },
    { "script-src http://www.example.com:8888?foo",
      "script-src http://www.example.com:8888" },
    { "script-src http://www.example.com/#foo",
      "script-src http://www.example.com/" },
    { "script-src http://www.example.com/?foo",
      "script-src http://www.example.com/" },
    { "script-src http://www.example.com/path-1/file.js#foo",
      "script-src http://www.example.com/path-1/file.js" },
    { "script-src http://www.example.com/path-1/file.js?foo",
      "script-src http://www.example.com/path-1/file.js" },
    { "script-src http://www.example.com/path-1/file.js?foo#bar",
      "script-src http://www.example.com/path-1/file.js" },
    { "report-uri http://www.example.com/",
      "report-uri http://www.example.com/" },
    { "report-uri http://www.example.com:8888/asdf",
      "report-uri http://www.example.com:8888/asdf" },
    { "report-uri http://www.example.com:8888/path_1/path_2",
      "report-uri http://www.example.com:8888/path_1/path_2" },
    { "report-uri http://www.example.com:8888/path_1/path_2/report.sjs&301",
      "report-uri http://www.example.com:8888/path_1/path_2/report.sjs&301" },
    { "report-uri /examplepath",
      "report-uri http://www.selfuri.com/examplepath" },
    { "connect-src http://www.example.com/foo%3Bsessionid=12%2C34",
      "connect-src http://www.example.com/foo;sessionid=12,34" },
    { "connect-src http://www.example.com/foo%3bsessionid=12%2c34",
      "connect-src http://www.example.com/foo;sessionid=12,34" },
    { "connect-src http://test.com/pathIncludingAz19-._~!$&'()*+=:@",
      "connect-src http://test.com/pathIncludingAz19-._~!$&'()*+=:@" },
    { "script-src http://www.example.com:88/.js",
      "script-src http://www.example.com:88/.js" },
    { "script-src https://foo.com/_abc/abc_/_/_a_b_c_",
      "script-src https://foo.com/_abc/abc_/_/_a_b_c_" }  // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ======================== TestSimplePolicies =======================

TEST(CSPParser, SimplePolicies)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "frame-src intent:",
      "frame-src intent:" },
    { "frame-src intent://host.name",
      "frame-src intent://host.name" },
    { "frame-src intent://my.host.link/",
      "frame-src intent://my.host.link/" },
    { "default-src *",
      "default-src *" },
    { "default-src https:",
      "default-src https:" },
    { "default-src https://*",
      "default-src https://*" },
    { "default-src *:*",
      "default-src http://*:*" },
    { "default-src *:80",
      "default-src http://*:80" },
    { "default-src http://*:80",
      "default-src http://*:80" },
    { "default-src javascript:",
      "default-src javascript:" },
    { "default-src data:",
      "default-src data:" },
    { "script-src 'unsafe-eval' 'unsafe-inline' http://www.example.com",
      "script-src 'unsafe-eval' 'unsafe-inline' http://www.example.com" },
    { "object-src 'self'",
      "object-src 'self'" },
    { "style-src http://www.example.com 'self'",
      "style-src http://www.example.com 'self'" },
    { "media-src http://www.example.com http://www.test.com",
      "media-src http://www.example.com http://www.test.com" },
    { "connect-src http://www.test.com example.com *.other.com;",
      "connect-src http://www.test.com http://example.com http://*.other.com"},
    { "connect-src example.com *.other.com",
      "connect-src http://example.com http://*.other.com"},
    { "style-src *.other.com example.com",
      "style-src http://*.other.com http://example.com"},
    { "default-src 'self'; img-src *;",
      "default-src 'self'; img-src *" },
    { "object-src media1.example.com media2.example.com *.cdn.example.com;",
      "object-src http://media1.example.com http://media2.example.com http://*.cdn.example.com" },
    { "script-src trustedscripts.example.com",
      "script-src http://trustedscripts.example.com" },
    { "script-src 'self' ; default-src trustedscripts.example.com",
      "script-src 'self'; default-src http://trustedscripts.example.com" },
    { "default-src 'none'; report-uri http://localhost:49938/test",
      "default-src 'none'; report-uri http://localhost:49938/test" },
    { "   ;   default-src abc",
      "default-src http://abc" },
    { " ; ; ; ;     default-src            abc    ; ; ; ;",
      "default-src http://abc" },
    { "script-src 'none' 'none' 'none';",
      "script-src 'none'" },
    { "script-src http://www.example.com/path-1//",
      "script-src http://www.example.com/path-1//" },
    { "script-src http://www.example.com/path-1//path_2",
      "script-src http://www.example.com/path-1//path_2" },
    { "default-src 127.0.0.1",
      "default-src http://127.0.0.1" },
    { "default-src 127.0.0.1:*",
      "default-src http://127.0.0.1:*" },
    { "default-src -; ",
      "default-src http://-" },
    { "script-src 1",
      "script-src http://1" },
    { "upgrade-insecure-requests",
      "upgrade-insecure-requests" },
    { "upgrade-insecure-requests https:",
      "upgrade-insecure-requests" },
    { "sandbox allow-scripts allow-forms  ",
      "sandbox allow-scripts allow-forms" },
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// =================== TestPoliciesWithInvalidSrc ====================

TEST(CSPParser, PoliciesWithInvalidSrc)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'self'; SCRIPT-SRC http://www.example.com",
      "script-src 'self'" },
    { "script-src 'none' test.com; script-src example.com",
      "script-src http://test.com" },
    { "default-src **",
      "default-src 'none'" },
    { "default-src 'self",
      "default-src 'none'" },
    { "default-src 'unsafe-inlin' ",
      "default-src 'none'" },
    { "default-src */",
      "default-src 'none'" },
    { "default-src",
      "default-src 'none'" },
    { "default-src 'unsafe-inlin' ",
      "default-src 'none'" },
    { "default-src :88",
      "default-src 'none'" },
    { "script-src abc::::::88",
      "script-src 'none'" },
    { "script-src *.*:*",
      "script-src 'none'" },
    { "img-src *::88",
      "img-src 'none'" },
    { "object-src http://localhost:",
      "object-src 'none'" },
    { "script-src test..com",
      "script-src 'none'" },
    { "script-src sub1.sub2.example+",
      "script-src 'none'" },
    { "script-src http://www.example.com//",
      "script-src 'none'" },
    { "script-src http://www.example.com:88path-1/",
      "script-src 'none'" },
    { "script-src http://www.example.com:88//",
      "script-src 'none'" },
    { "script-src http://www.example.com:88//path-1",
      "script-src 'none'" },
    { "script-src http://www.example.com:88//path-1",
      "script-src 'none'" },
    { "script-src http://www.example.com:88.js",
      "script-src 'none'" },
    { "script-src http://www.example.com:*.js",
      "script-src 'none'" },
    { "script-src http://www.example.com:*.",
      "script-src 'none'" },
    { "script-src 'nonce-{invalid}'",
      "script-src 'none'" },
    { "script-src 'sha256-{invalid}'",
      "script-src 'none'" },
    { "script-src 'nonce-in$valid'",
      "script-src 'none'" },
    { "script-src 'sha256-in$valid'",
      "script-src 'none'" },
    { "script-src 'nonce-invalid==='",
      "script-src 'none'" },
    { "script-src 'sha256-invalid==='",
      "script-src 'none'" },
    { "script-src 'nonce-==='",
      "script-src 'none'" },
    { "script-src 'sha256-==='",
      "script-src 'none'" },
    { "script-src 'nonce-=='",
      "script-src 'none'" },
    { "script-src 'sha256-=='",
      "script-src 'none'" },
    { "script-src 'nonce-='",
      "script-src 'none'" },
    { "script-src 'sha256-='",
      "script-src 'none'" },
    { "script-src 'nonce-'",
      "script-src 'none'" },
    { "script-src 'sha256-'",
      "script-src 'none'" },
    { "connect-src http://www.example.com/foo%zz;",
      "connect-src 'none'" },
    { "script-src https://foo.com/%$",
      "script-src 'none'" },
    { "sandbox    foo",
      "sandbox"},
      // clang-format on
  };

  // amount of tests - 1, because the latest should be ignored.
  uint32_t policyCount = (sizeof(policies) / sizeof(PolicyTest)) - 1;
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ================= TestPoliciesWithInvalidUrlHashes =================

TEST(CSPParser, PoliciesWithInvalidUrlHashes)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-src 'url-sha256-{invalid}'",
      "script-src 'none'" },
    { "script-src 'url-sha256-in$valid'",
      "script-src 'none'" },
    { "script-src 'url-sha256-invalid==='",
      "script-src 'none'" },
    { "script-src 'url-sha256-=='",
      "script-src 'none'" },
    { "script-src 'url-sha256-'",
      "script-src 'none'" },
    { "script-src 'url-md5-abc'",
      "script-src 'none'" },
    { "script-src 'url-abc'",
      "script-src 'none'" },
      // clang-format on
  };

  // They're ignored both with and without the pref.
  for (bool enabled : {true, false}) {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, enabled);
    ASSERT_NS_SUCCEEDED(runTestSuite(policies, std::size(policies), 1));
  }
}

// ============================= TestBadPolicies =======================

TEST(CSPParser, BadPolicies)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "script-sr 'self", "" },
    { "", "" },
    { "; ; ; ; ; ; ;", "" },
    { "defaut-src asdf", "" },
    { "default-src: aaa", "" },
    { "asdf http://test.com", ""},
    { "report-uri", ""},
    { "report-uri http://:foo", ""},
    { "require-sri-for", ""},
    { "require-sri-for style", ""},
    { "require-trusted-types-for invalid" },
    { "require-trusted-types-for 'invalid'" },

      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 0));
}

// ======================= TestGoodGeneratedPolicies =================

TEST(CSPParser, GoodGeneratedPolicies)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "default-src 'self'; img-src *",
      "default-src 'self'; img-src *" },
    { "report-uri /policy",
      "report-uri http://www.selfuri.com/policy"},
    { "img-src *",
      "img-src *" },
    { "media-src foo.bar",
      "media-src http://foo.bar" },
    { "frame-src *.bar",
      "frame-src http://*.bar" },
    { "font-src com",
      "font-src http://com" },
    { "connect-src f00b4r.com",
      "connect-src http://f00b4r.com" },
    { "script-src *.a.b.c",
      "script-src http://*.a.b.c" },
    { "object-src *.b.c",
      "object-src http://*.b.c" },
    { "style-src a.b.c",
      "style-src http://a.b.c" },
    { "img-src a.com",
      "img-src http://a.com" },
    { "media-src http://abc.com",
      "media-src http://abc.com" },
    { "frame-src a2-c.com",
      "frame-src http://a2-c.com" },
    { "font-src https://a.com",
      "font-src https://a.com" },
    { "connect-src *.a.com",
      "connect-src http://*.a.com" },
    { "default-src a.com:23",
      "default-src http://a.com:23" },
    { "script-src https://a.com:200",
      "script-src https://a.com:200" },
    { "object-src data:",
      "object-src data:" },
    { "style-src javascript:",
      "style-src javascript:" },
    { "frame-src https://foobar.com:443",
      "frame-src https://foobar.com:443" },
    { "font-src https://a.com:443",
      "font-src https://a.com:443" },
    { "connect-src http://a.com:80",
      "connect-src http://a.com:80" },
    { "default-src http://foobar.com",
      "default-src http://foobar.com" },
    { "script-src https://foobar.com",
      "script-src https://foobar.com" },
    { "style-src 'none'",
      "style-src 'none'" },
    { "img-src foo.bar:21 https://ras.bar",
      "img-src http://foo.bar:21 https://ras.bar" },
    { "media-src http://foo.bar:21 https://ras.bar:443",
      "media-src http://foo.bar:21 https://ras.bar:443" },
    { "frame-src http://self.com:80",
      "frame-src http://self.com:80" },
    { "font-src http://self.com",
      "font-src http://self.com" },
    { "connect-src https://foo.com http://bar.com:88",
      "connect-src https://foo.com http://bar.com:88" },
    { "default-src * https://bar.com 'none'",
      "default-src * https://bar.com" },
    { "script-src *.foo.com",
      "script-src http://*.foo.com" },
    { "object-src http://b.com",
      "object-src http://b.com" },
    { "style-src http://bar.com:88",
      "style-src http://bar.com:88" },
    { "img-src https://bar.com:88",
      "img-src https://bar.com:88" },
    { "media-src http://bar.com:443",
      "media-src http://bar.com:443" },
    { "frame-src https://foo.com:88",
      "frame-src https://foo.com:88" },
    { "font-src http://foo.com",
      "font-src http://foo.com" },
    { "connect-src http://x.com:23",
      "connect-src http://x.com:23" },
    { "default-src http://barbaz.com",
      "default-src http://barbaz.com" },
    { "script-src http://somerandom.foo.com",
      "script-src http://somerandom.foo.com" },
    { "default-src *",
      "default-src *" },
    { "style-src http://bar.com:22",
      "style-src http://bar.com:22" },
    { "img-src https://foo.com:443",
      "img-src https://foo.com:443" },
    { "script-src https://foo.com; ",
      "script-src https://foo.com" },
    { "img-src bar.com:*",
      "img-src http://bar.com:*" },
    { "font-src https://foo.com:400",
      "font-src https://foo.com:400" },
    { "connect-src http://bar.com:400",
      "connect-src http://bar.com:400" },
    { "default-src http://evil.com",
      "default-src http://evil.com" },
    { "script-src https://evil.com:100",
      "script-src https://evil.com:100" },
    { "default-src bar.com; script-src https://foo.com",
      "default-src http://bar.com; script-src https://foo.com" },
    { "default-src 'self'; script-src 'self' https://*:*",
      "default-src 'self'; script-src 'self' https://*:*" },
    { "img-src http://self.com:34",
      "img-src http://self.com:34" },
    { "media-src http://subd.self.com:34",
      "media-src http://subd.self.com:34" },
    { "default-src 'none'",
      "default-src 'none'" },
    { "connect-src http://self",
      "connect-src http://self" },
    { "default-src http://foo",
      "default-src http://foo" },
    { "script-src http://foo:80",
      "script-src http://foo:80" },
    { "object-src http://bar",
      "object-src http://bar" },
    { "style-src http://three:80",
      "style-src http://three:80" },
    { "img-src https://foo:400",
      "img-src https://foo:400" },
    { "media-src https://self:34",
      "media-src https://self:34" },
    { "frame-src https://bar",
      "frame-src https://bar" },
    { "font-src http://three:81",
      "font-src http://three:81" },
    { "connect-src https://three:81",
      "connect-src https://three:81" },
    { "script-src http://self.com:80/foo",
      "script-src http://self.com:80/foo" },
    { "object-src http://self.com/foo",
      "object-src http://self.com/foo" },
    { "report-uri /report.py",
      "report-uri http://www.selfuri.com/report.py"},
    { "img-src http://foo.org:34/report.py",
      "img-src http://foo.org:34/report.py" },
    { "media-src foo/bar/report.py",
      "media-src http://foo/bar/report.py" },
    { "report-uri /",
      "report-uri http://www.selfuri.com/"},
    { "font-src https://self.com/report.py",
      "font-src https://self.com/report.py" },
    { "connect-src https://foo.com/report.py",
      "connect-src https://foo.com/report.py" },
    { "default-src *; report-uri  http://www.reporturi.com/",
      "default-src *; report-uri http://www.reporturi.com/" },
    { "default-src http://first.com",
      "default-src http://first.com" },
    { "script-src http://second.com",
      "script-src http://second.com" },
    { "object-src http://third.com",
      "object-src http://third.com" },
    { "style-src https://foobar.com:4443",
      "style-src https://foobar.com:4443" },
    { "img-src http://foobar.com:4443",
      "img-src http://foobar.com:4443" },
    { "media-src bar.com",
      "media-src http://bar.com" },
    { "frame-src http://bar.com",
      "frame-src http://bar.com" },
    { "font-src http://self.com/",
      "font-src http://self.com/" },
    { "script-src 'self'",
      "script-src 'self'" },
    { "default-src http://self.com/foo.png",
      "default-src http://self.com/foo.png" },
    { "script-src http://self.com/foo.js",
      "script-src http://self.com/foo.js" },
    { "object-src http://bar.com/foo.js",
      "object-src http://bar.com/foo.js" },
    { "style-src http://FOO.COM",
      "style-src http://foo.com" },
    { "img-src HTTP",
      "img-src http://http" },
    { "media-src http",
      "media-src http://http" },
    { "frame-src 'SELF'",
      "frame-src 'self'" },
    { "DEFAULT-src 'self';",
      "default-src 'self'" },
    { "default-src 'self' http://FOO.COM",
      "default-src 'self' http://foo.com" },
    { "default-src 'self' HTTP://foo.com",
      "default-src 'self' http://foo.com" },
    { "default-src 'NONE'",
      "default-src 'none'" },
    { "script-src policy-uri ",
      "script-src http://policy-uri" },
    { "img-src 'self'; ",
      "img-src 'self'" },
    { "frame-ancestors foo-bar.com",
      "frame-ancestors http://foo-bar.com" },
    { "frame-ancestors http://a.com",
      "frame-ancestors http://a.com" },
    { "frame-ancestors 'self'",
      "frame-ancestors 'self'" },
    { "frame-ancestors http://self.com:88",
      "frame-ancestors http://self.com:88" },
    { "frame-ancestors http://a.b.c.d.e.f.g.h.i.j.k.l.x.com",
      "frame-ancestors http://a.b.c.d.e.f.g.h.i.j.k.l.x.com" },
    { "frame-ancestors https://self.com:34",
      "frame-ancestors https://self.com:34" },
    { "frame-ancestors http://sampleuser:samplepass@example.com",
      "frame-ancestors 'none'" },
    { "default-src 'none'; frame-ancestors 'self'",
      "default-src 'none'; frame-ancestors 'self'" },
    { "frame-ancestors http://self:80",
      "frame-ancestors http://self:80" },
    { "frame-ancestors http://self.com/bar",
      "frame-ancestors http://self.com/bar" },
    { "default-src 'self'; frame-ancestors 'self'",
      "default-src 'self'; frame-ancestors 'self'" },
    { "frame-ancestors http://bar.com/foo.png",
      "frame-ancestors http://bar.com/foo.png" },
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ==================== TestBadGeneratedPolicies ====================

TEST(CSPParser, BadGeneratedPolicies)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "foo.*.bar", ""},
    { "foo!bar.com", ""},
    { "x.*.a.com", ""},
    { "a#2-c.com", ""},
    { "http://foo.com:bar.com:23", ""},
    { "f!oo.bar", ""},
    { "ht!ps://f-oo.bar", ""},
    { "https://f-oo.bar:3f", ""},
    { "**", ""},
    { "*a", ""},
    { "http://username:password@self.com/foo", ""},
    { "http://other:pass1@self.com/foo", ""},
    { "http://user1:pass1@self.com/foo", ""},
    { "http://username:password@self.com/bar", ""},
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 0));
}

// ============ TestGoodGeneratedPoliciesForPathHandling =============

TEST(CSPParser, GoodGeneratedPoliciesForPathHandling)
{
  // Once bug 808292 (Implement path-level host-source matching to CSP)
  // lands we have to update the expected output to include the parsed path

  static const PolicyTest policies[] = {
      // clang-format off
    { "img-src http://test1.example.com",
      "img-src http://test1.example.com" },
    { "img-src http://test1.example.com/",
      "img-src http://test1.example.com/" },
    { "img-src http://test1.example.com/path-1",
      "img-src http://test1.example.com/path-1" },
    { "img-src http://test1.example.com/path-1/",
      "img-src http://test1.example.com/path-1/" },
    { "img-src http://test1.example.com/path-1/path_2/",
      "img-src http://test1.example.com/path-1/path_2/" },
    { "img-src http://test1.example.com/path-1/path_2/file.js",
      "img-src http://test1.example.com/path-1/path_2/file.js" },
    { "img-src http://test1.example.com/path-1/path_2/file_1.js",
      "img-src http://test1.example.com/path-1/path_2/file_1.js" },
    { "img-src http://test1.example.com/path-1/path_2/file-2.js",
      "img-src http://test1.example.com/path-1/path_2/file-2.js" },
    { "img-src http://test1.example.com/path-1/path_2/f.js",
      "img-src http://test1.example.com/path-1/path_2/f.js" },
    { "img-src http://test1.example.com/path-1/path_2/f.oo.js",
      "img-src http://test1.example.com/path-1/path_2/f.oo.js" },
    { "img-src test1.example.com",
      "img-src http://test1.example.com" },
    { "img-src test1.example.com/",
      "img-src http://test1.example.com/" },
    { "img-src test1.example.com/path-1",
      "img-src http://test1.example.com/path-1" },
    { "img-src test1.example.com/path-1/",
      "img-src http://test1.example.com/path-1/" },
    { "img-src test1.example.com/path-1/path_2/",
      "img-src http://test1.example.com/path-1/path_2/" },
    { "img-src test1.example.com/path-1/path_2/file.js",
      "img-src http://test1.example.com/path-1/path_2/file.js" },
    { "img-src test1.example.com/path-1/path_2/file_1.js",
      "img-src http://test1.example.com/path-1/path_2/file_1.js" },
    { "img-src test1.example.com/path-1/path_2/file-2.js",
      "img-src http://test1.example.com/path-1/path_2/file-2.js" },
    { "img-src test1.example.com/path-1/path_2/f.js",
      "img-src http://test1.example.com/path-1/path_2/f.js" },
    { "img-src test1.example.com/path-1/path_2/f.oo.js",
      "img-src http://test1.example.com/path-1/path_2/f.oo.js" },
    { "img-src *.example.com",
      "img-src http://*.example.com" },
    { "img-src *.example.com/",
      "img-src http://*.example.com/" },
    { "img-src *.example.com/path-1",
      "img-src http://*.example.com/path-1" },
    { "img-src *.example.com/path-1/",
      "img-src http://*.example.com/path-1/" },
    { "img-src *.example.com/path-1/path_2/",
      "img-src http://*.example.com/path-1/path_2/" },
    { "img-src *.example.com/path-1/path_2/file.js",
      "img-src http://*.example.com/path-1/path_2/file.js" },
    { "img-src *.example.com/path-1/path_2/file_1.js",
      "img-src http://*.example.com/path-1/path_2/file_1.js" },
    { "img-src *.example.com/path-1/path_2/file-2.js",
      "img-src http://*.example.com/path-1/path_2/file-2.js" },
    { "img-src *.example.com/path-1/path_2/f.js",
      "img-src http://*.example.com/path-1/path_2/f.js" },
    { "img-src *.example.com/path-1/path_2/f.oo.js",
      "img-src http://*.example.com/path-1/path_2/f.oo.js" },
    { "img-src test1.example.com:80",
      "img-src http://test1.example.com:80" },
    { "img-src test1.example.com:80/",
      "img-src http://test1.example.com:80/" },
    { "img-src test1.example.com:80/path-1",
      "img-src http://test1.example.com:80/path-1" },
    { "img-src test1.example.com:80/path-1/",
      "img-src http://test1.example.com:80/path-1/" },
    { "img-src test1.example.com:80/path-1/path_2",
      "img-src http://test1.example.com:80/path-1/path_2" },
    { "img-src test1.example.com:80/path-1/path_2/",
      "img-src http://test1.example.com:80/path-1/path_2/" },
    { "img-src test1.example.com:80/path-1/path_2/file.js",
      "img-src http://test1.example.com:80/path-1/path_2/file.js" },
    { "img-src test1.example.com:80/path-1/path_2/f.ile.js",
      "img-src http://test1.example.com:80/path-1/path_2/f.ile.js" },
    { "img-src test1.example.com:*",
      "img-src http://test1.example.com:*" },
    { "img-src test1.example.com:*/",
      "img-src http://test1.example.com:*/" },
    { "img-src test1.example.com:*/path-1",
      "img-src http://test1.example.com:*/path-1" },
    { "img-src test1.example.com:*/path-1/",
      "img-src http://test1.example.com:*/path-1/" },
    { "img-src test1.example.com:*/path-1/path_2",
      "img-src http://test1.example.com:*/path-1/path_2" },
    { "img-src test1.example.com:*/path-1/path_2/",
      "img-src http://test1.example.com:*/path-1/path_2/" },
    { "img-src test1.example.com:*/path-1/path_2/file.js",
      "img-src http://test1.example.com:*/path-1/path_2/file.js" },
    { "img-src test1.example.com:*/path-1/path_2/f.ile.js",
      "img-src http://test1.example.com:*/path-1/path_2/f.ile.js" },
    { "img-src http://test1.example.com/abc//",
      "img-src http://test1.example.com/abc//" },
    { "img-src https://test1.example.com/abc/def//",
      "img-src https://test1.example.com/abc/def//" },
    { "img-src https://test1.example.com/abc/def/ghi//",
      "img-src https://test1.example.com/abc/def/ghi//" },
    { "img-src http://test1.example.com:80/abc//",
      "img-src http://test1.example.com:80/abc//" },
    { "img-src https://test1.example.com:80/abc/def//",
      "img-src https://test1.example.com:80/abc/def//" },
    { "img-src https://test1.example.com:80/abc/def/ghi//",
      "img-src https://test1.example.com:80/abc/def/ghi//" },
    { "img-src https://test1.example.com/abc////////////def/",
      "img-src https://test1.example.com/abc////////////def/" },
    { "img-src https://test1.example.com/abc////////////",
      "img-src https://test1.example.com/abc////////////" },
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ============== TestBadGeneratedPoliciesForPathHandling ============

TEST(CSPParser, BadGeneratedPoliciesForPathHandling)
{
  static const PolicyTest policies[] = {
      // clang-format off
    { "img-src test1.example.com:88path-1/",
      "img-src 'none'" },
    { "img-src test1.example.com:80.js",
      "img-src 'none'" },
    { "img-src test1.example.com:*.js",
      "img-src 'none'" },
    { "img-src test1.example.com:*.",
      "img-src 'none'" },
    { "img-src http://test1.example.com//",
      "img-src 'none'" },
    { "img-src http://test1.example.com:80//",
      "img-src 'none'" },
    { "img-src http://test1.example.com:80abc",
      "img-src 'none'" },
      // clang-format on
  };

  uint32_t policyCount = sizeof(policies) / sizeof(PolicyTest);
  ASSERT_NS_SUCCEEDED(runTestSuite(policies, policyCount, 1));
}

// ======================== TestFuzzyPolicies ========================

// Use a policy, eliminate one character at a time,
// and feed it as input to the parser.

TEST(CSPParser, ShorteningPolicies)
{
  char pol[] =
      "default-src http://www.sub1.sub2.example.com:88/path1/path2/ "
      "'unsafe-inline' 'none'";
  uint32_t len = static_cast<uint32_t>(sizeof(pol));

  PolicyTest testPol[1];
  memset(&testPol[0].policy, '\0', kMaxPolicyLength * sizeof(char));

  while (--len) {
    memset(&testPol[0].policy, '\0', kMaxPolicyLength * sizeof(char));
    memcpy(&testPol[0].policy, &pol, len * sizeof(char));
    ASSERT_TRUE(
        NS_SUCCEEDED(runTestSuite(testPol, 1, kFuzzyExpectedPolicyCount)));
  }
}

// Like ShorteningPolicies, for a url-hash-source, with and without its pref.

TEST(CSPParser, ShorteningUrlHashPolicies)
{
  char pol[] =
      "script-src 'url-sha256-idUt1lkA7P9ehz7Yz1P5USBar6neNbzkUPH+WQoM52A=' "
      "'self'";

  for (bool enabled : {true, false}) {
    AutoBoolPref pref(kUrlHashesEnabledPrefName, enabled);
    uint32_t len = static_cast<uint32_t>(sizeof(pol));
    PolicyTest testPol[1];
    while (--len) {
      memset(&testPol[0].policy, '\0', kMaxPolicyLength * sizeof(char));
      memcpy(&testPol[0].policy, &pol, len * sizeof(char));
      ASSERT_TRUE(
          NS_SUCCEEDED(runTestSuite(testPol, 1, kFuzzyExpectedPolicyCount)));
    }
  }
}

// ============================= TestFuzzyPolicies ===================

// We generate kFuzzyRuns inputs by (pseudo) randomly picking from the 128
// ASCII characters; feed them to the parser and verfy that the parser
// handles the input gracefully.
//
// Please note, that by using srand(0) we get deterministic results!

#if RUN_OFFLINE_TESTS

TEST(CSPParser, FuzzyPolicies)
{
  // init srand with 0 so we get same results
  srand(0);

  PolicyTest testPol[1];
  memset(&testPol[0].policy, '\0', kMaxPolicyLength);

  for (uint32_t index = 0; index < kFuzzyRuns; index++) {
    // randomly select the length of the next policy
    uint32_t polLength = rand() % kMaxPolicyLength;
    // reset memory of the policy string
    memset(&testPol[0].policy, '\0', kMaxPolicyLength * sizeof(char));

    for (uint32_t i = 0; i < polLength; i++) {
      // fill the policy array with random ASCII chars
      testPol[0].policy[i] = static_cast<char>(rand() % 128);
    }
    ASSERT_TRUE(
        NS_SUCCEEDED(runTestSuite(testPol, 1, kFuzzyExpectedPolicyCount)));
  }
}

#endif

// ======================= TestFuzzyPoliciesIncDir ===================

// In a similar fashion as in TestFuzzyPolicies, we again (pseudo) randomly
// generate input for the parser, but this time also include a valid directive
// followed by the random input.

#if RUN_OFFLINE_TESTS

TEST(CSPParser, FuzzyPoliciesIncDir)
{
  // init srand with 0 so we get same results
  srand(0);

  PolicyTest testPol[1];
  memset(&testPol[0].policy, '\0', kMaxPolicyLength);

  char defaultSrc[] = "default-src ";
  int defaultSrcLen = sizeof(defaultSrc) - 1;
  // copy default-src into the policy array
  memcpy(&testPol[0].policy, &defaultSrc, (defaultSrcLen * sizeof(char)));

  for (uint32_t index = 0; index < kFuzzyRuns; index++) {
    // randomly select the length of the next policy
    uint32_t polLength = rand() % (kMaxPolicyLength - defaultSrcLen);
    // reset memory of the policy string, but leave default-src.
    memset((&(testPol[0].policy) + (defaultSrcLen * sizeof(char))), '\0',
           (kMaxPolicyLength - defaultSrcLen) * sizeof(char));

    // do not start at index 0 so we do not overwrite 'default-src'
    for (uint32_t i = defaultSrcLen; i < polLength; i++) {
      // fill the policy array with random ASCII chars
      testPol[0].policy[i] = static_cast<char>(rand() % 128);
    }
    ASSERT_TRUE(
        NS_SUCCEEDED(runTestSuite(testPol, 1, kFuzzyExpectedPolicyCount)));
  }
}

#endif

// ====================== TestFuzzyPoliciesIncDirLimASCII ============

// Same as TestFuzzyPoliciesIncDir() but using limited ASCII,
// which represents more likely input.

#if RUN_OFFLINE_TESTS

TEST(CSPParser, FuzzyPoliciesIncDirLimASCII)
{
  char input[] =
      "1234567890"
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWZYZ"
      "!@#^&*()-+_=";

  // init srand with 0 so we get same results
  srand(0);

  PolicyTest testPol[1];
  memset(&testPol[0].policy, '\0', kMaxPolicyLength);

  char defaultSrc[] = "default-src ";
  int defaultSrcLen = sizeof(defaultSrc) - 1;
  // copy default-src into the policy array
  memcpy(&testPol[0].policy, &defaultSrc, (defaultSrcLen * sizeof(char)));

  for (uint32_t index = 0; index < kFuzzyRuns; index++) {
    // randomly select the length of the next policy
    uint32_t polLength = rand() % (kMaxPolicyLength - defaultSrcLen);
    // reset memory of the policy string, but leave default-src.
    memset((&(testPol[0].policy) + (defaultSrcLen * sizeof(char))), '\0',
           (kMaxPolicyLength - defaultSrcLen) * sizeof(char));

    // do not start at index 0 so we do not overwrite 'default-src'
    for (uint32_t i = defaultSrcLen; i < polLength; i++) {
      // fill the policy array with chars from the pre-defined input
      uint32_t inputIndex = rand() % sizeof(input);
      testPol[0].policy[i] = input[inputIndex];
    }
    ASSERT_TRUE(
        NS_SUCCEEDED(runTestSuite(testPol, 1, kFuzzyExpectedPolicyCount)));
  }
}
#endif
