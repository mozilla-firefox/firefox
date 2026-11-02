/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/NoVarySearchUtils.h"
#include "nsNetUtil.h"

using namespace mozilla::net;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static NoVarySearchData Parse(const char* aHeader) {
  return ParseNoVarySearchHeader(nsDependentCString(aHeader));
}

static nsCOMPtr<nsIURI> MakeURI(const char* aSpec) {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), nsDependentCString(aSpec));
  return uri;
}

static bool Equiv(const char* aURIA, const char* aURIB, const char* aHeader) {
  auto data = Parse(aHeader);
  nsCOMPtr<nsIURI> uriA = MakeURI(aURIA);
  nsCOMPtr<nsIURI> uriB = MakeURI(aURIB);
  return URLsAreEquivalentModuloVariationConfig(uriA, uriB, data);
}

// ---------------------------------------------------------------------------
// ParseNoVarySearchHeader tests
// ---------------------------------------------------------------------------

TEST(TestNoVarySearchParse, EmptyHeader)
{
  auto d = Parse("");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
  EXPECT_TRUE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, InvalidSF)
{
  auto d = Parse("!!!not valid sf!!!");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, ParamsInvalidType)
{
  // params with an integer value is not a valid type → ExactMatch
  auto d = Parse("params=42");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, UnknownKeysOnly)
{
  // Header with only unknown keys → ExactMatch (no params, no except)
  auto d = Parse("x-custom=?1, x-other=?0");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
  EXPECT_TRUE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, ParamsAndExceptTogether)
{
  // params=(inner list) + except → invalid → ExactMatch
  auto d = Parse(R"(params=("a"), except=("b"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, ExceptWithoutParams)
{
  // except without params → invalid → ExactMatch
  auto d = Parse(R"(except=("a"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, ExceptEmptyWithoutParams)
{
  // except with empty inner list, no params → invalid → ExactMatch
  auto d = Parse("except=()");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, ParamsBareToken)
{
  // bare `params` token → IgnoreAll
  // https://httpwg.org/http-extensions/draft-ietf-httpbis-no-vary-search.html#section-5.1-6
  auto d = Parse("params");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::IgnoreAll);
  EXPECT_TRUE(d.paramNames.IsEmpty());
  EXPECT_TRUE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, ParamsExplicitBooleanTrue)
{
  // params=?1 (explicit boolean true) → IgnoreAll, same as bare params
  auto d = Parse("params=?1");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::IgnoreAll);
  EXPECT_TRUE(d.paramNames.IsEmpty());
}

TEST(TestNoVarySearchParse, ParamsExplicitBooleanFalse)
{
  // params=?0 (boolean false) → ExactMatch (not IgnoreAll)
  auto d = Parse("params=?0");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
}

TEST(TestNoVarySearchParse, ParamsSingleItem)
{
  auto d = Parse(R"(params=("a"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Blocklist);
  ASSERT_EQ(d.paramNames.Length(), 1u);
  EXPECT_EQ(d.paramNames[0], "a"_ns);
}

TEST(TestNoVarySearchParse, ParamsMultipleItems)
{
  auto d = Parse(R"(params=("a" "b" "c"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Blocklist);
  ASSERT_EQ(d.paramNames.Length(), 3u);
  EXPECT_EQ(d.paramNames[0], "a"_ns);
  EXPECT_EQ(d.paramNames[1], "b"_ns);
  EXPECT_EQ(d.paramNames[2], "c"_ns);
}

TEST(TestNoVarySearchParse, ExceptAllowlist)
{
  // params, except=("id") → Allowlist
  // https://httpwg.org/http-extensions/draft-ietf-httpbis-no-vary-search.html#section-5.1-7
  auto d = Parse(R"(params, except=("id"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Allowlist);
  ASSERT_EQ(d.paramNames.Length(), 1u);
  EXPECT_EQ(d.paramNames[0], "id"_ns);
}

TEST(TestNoVarySearchParse, ExceptAllowlistExplicitBoolTrue)
{
  // params=?1 + except=(...) → Allowlist (same as bare params + except)
  auto d = Parse(R"(params=?1, except=("id"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Allowlist);
  ASSERT_EQ(d.paramNames.Length(), 1u);
  EXPECT_EQ(d.paramNames[0], "id"_ns);
}

TEST(TestNoVarySearchParse, ExceptMultipleKeys)
{
  auto d = Parse(R"(params, except=("id" "type"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Allowlist);
  ASSERT_EQ(d.paramNames.Length(), 2u);
}

TEST(TestNoVarySearchParse, KeyOrderTrue)
{
  // bare key-order → varyOnKeyOrder=false
  // https://httpwg.org/http-extensions/draft-ietf-httpbis-no-vary-search.html#section-5.1-4
  auto d = Parse("key-order");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::ExactMatch);
  EXPECT_FALSE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, KeyOrderExplicitTrue)
{
  // key-order=?1 (explicit boolean true) → varyOnKeyOrder=false
  auto d = Parse("key-order=?1");
  EXPECT_FALSE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, KeyOrderExplicitFalse)
{
  // key-order=?0 → varyOnKeyOrder=true (order matters, explicit default)
  auto d = Parse("key-order=?0");
  EXPECT_TRUE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, KeyOrderCombinedWithParams)
{
  auto d = Parse(R"(params=("utm"), key-order)");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Blocklist);
  ASSERT_EQ(d.paramNames.Length(), 1u);
  EXPECT_EQ(d.paramNames[0], "utm"_ns);
  EXPECT_FALSE(d.varyOnKeyOrder);
}

TEST(TestNoVarySearchParse, SFExtensionParamsOnItemsIgnored)
{
  // Unknown SF parameters on items must be ignored
  auto d = Parse(R"(params=("a";unknown="x"))");
  EXPECT_EQ(d.paramsRule, NoVarySearchData::ParamsRule::Blocklist);
  ASSERT_EQ(d.paramNames.Length(), 1u);
  EXPECT_EQ(d.paramNames[0], "a"_ns);
}

// ---------------------------------------------------------------------------
// URLsAreEquivalentModuloVariationConfig — parametrized tests
// ---------------------------------------------------------------------------

struct EquivTestData {
  const char* description;
  const char* urlA;
  const char* urlB;
  const char* header;
  bool expected;
};

static const EquivTestData kEquivTests[] = {
    // ExactMatch (no header)
    {"ExactMatch: identical URLs", "https://example.com/page?a=1&b=2",
     "https://example.com/page?a=1&b=2", "", true},
    {"ExactMatch: different query value", "https://example.com/page?a=1&b=2",
     "https://example.com/page?a=1&b=3", "", false},

    // Structural mismatches always return false regardless of NVS
    {"Different scheme", "https://example.com/page?a=1",
     "http://example.com/page?a=1", "params", false},
    {"Different host", "https://example.com/page?a=1",
     "https://other.com/page?a=1", "params", false},
    {"Different path", "https://example.com/page?a=1",
     "https://example.com/other?a=1", "params", false},

    // Blocklist: params=("a")
    {"Blocklist: ignored param differs → HIT", "https://example.com/p?a=1&b=2",
     "https://example.com/p?a=99&b=2", R"(params=("a"))", true},
    {"Blocklist: non-listed param differs → MISS",
     "https://example.com/p?a=1&b=2", "https://example.com/p?a=1&b=99",
     R"(params=("a"))", false},
    {"Blocklist: multiple ignored params", "https://example.com/p?a=1&b=2",
     "https://example.com/p?a=9&b=8", R"(params=("a" "b"))", true},

    // Blocklist: many params in query string, only some in NVS list
    {"Blocklist: many params, only listed ones ignored → HIT",
     "https://example.com/p?a=1&b=2&c=3&d=4&e=5",
     "https://example.com/p?a=9&b=8&c=3&d=4&e=5", R"(params=("a" "b"))", true},
    {"Blocklist: many params, non-listed param differs → MISS",
     "https://example.com/p?a=1&b=2&c=3&d=4&e=5",
     "https://example.com/p?a=9&b=8&c=3&d=4&e=99", R"(params=("a" "b"))",
     false},

    // Blocklist: params in query don't match any NVS listed params
    {"Blocklist: no query params match NVS list → treated as ExactMatch",
     "https://example.com/p?x=1&y=2", "https://example.com/p?x=1&y=3",
     R"(params=("a" "b"))", false},
    {"Blocklist: no query params match NVS list, identical → HIT",
     "https://example.com/p?x=1&y=2", "https://example.com/p?x=1&y=2",
     R"(params=("a" "b"))", true},

    // IgnoreAll: params
    {"IgnoreAll: completely different params → HIT",
     "https://example.com/p?a=1&b=2", "https://example.com/p?x=9&y=8", "params",
     true},
    {"IgnoreAll: no query vs query → HIT", "https://example.com/p",
     "https://example.com/p?a=1&b=2", "params", true},
    {"IgnoreAll: params=?1 explicit → HIT", "https://example.com/p?a=1",
     "https://example.com/p?b=99", "params=?1", true},

    // Allowlist: params, except=("id")
    {"Allowlist: kept param same → HIT",
     "https://example.com/p?id=42&noise=abc",
     "https://example.com/p?id=42&noise=xyz", R"(params, except=("id"))", true},
    {"Allowlist: kept param differs → MISS", "https://example.com/p?id=42",
     "https://example.com/p?id=99", R"(params, except=("id"))", false},
    {"Allowlist: params=?1 + except, kept param same → HIT",
     "https://example.com/p?id=42&noise=abc",
     "https://example.com/p?id=42&noise=xyz", R"(params=?1, except=("id"))",
     true},

    // key-order
    {"key-order: reordered params → HIT", "https://example.com/p?a=1&b=2",
     "https://example.com/p?b=2&a=1", "key-order", true},
    {"key-order: reordered but value differs → MISS",
     "https://example.com/p?a=1&b=2", "https://example.com/p?b=9&a=1",
     "key-order", false},
    {"key-order=?0: reordered → MISS", "https://example.com/p?a=1&b=2",
     "https://example.com/p?b=2&a=1", "key-order=?0", false},
    {"key-order=?1: reordered → HIT", "https://example.com/p?a=1&b=2",
     "https://example.com/p?b=2&a=1", "key-order=?1", true},
    {"key-order: extra param → MISS", "https://example.com/p?a=1&b=2",
     "https://example.com/p?a=1&b=2&c=3", "key-order", false},

    // Combined
    {"Combined params+key-order: ignored param and order both differ → HIT",
     "https://example.com/p?utm=x&b=2&a=1",
     "https://example.com/p?a=1&b=2&utm=y", R"(params=("utm"), key-order)",
     true},

    // Invalid SF → ExactMatch fallback
    {"Invalid SF (params inner list + except) → ExactMatch → MISS",
     "https://example.com/p?a=1", "https://example.com/p?a=2",
     R"(params=("a"), except=("b"))", false},

    // multi-value params
    {"key-order: multi-value, same values reordered by key → HIT",
     "https://example.com/p?a=1&a=2&b=3", "https://example.com/p?b=3&a=1&a=2",
     "key-order", true},
    {"key-order: multi-value, values for same key in different order → MISS",
     "https://example.com/p?a=1&a=2", "https://example.com/p?a=2&a=1",
     "key-order", false},
};

class TestNoVarySearchEquivalence
    : public testing::TestWithParam<EquivTestData> {};

TEST_P(TestNoVarySearchEquivalence, Check) {
  const EquivTestData& t = GetParam();
  EXPECT_EQ(Equiv(t.urlA, t.urlB, t.header), t.expected) << t.description;
  // Equivalence must be symmetric
  EXPECT_EQ(Equiv(t.urlB, t.urlA, t.header), t.expected)
      << t.description << " (symmetric)";
}

INSTANTIATE_TEST_SUITE_P(TestNoVarySearch, TestNoVarySearchEquivalence,
                         testing::ValuesIn(kEquivTests));
