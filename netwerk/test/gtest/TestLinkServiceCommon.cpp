/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>

#include "LinkServiceCommon.h"
#include "gtest/gtest.h"
#include "mozilla/Base64.h"
#include "mozilla/Maybe.h"
#include "mozilla/SHA1.h"
#include "nsID.h"
#include "nsString.h"

using namespace mozilla;

// The hash state of a network's inputs before a seed is added.
static SHA1Sum UnseededNetworkId(const char* aInputs) {
  SHA1Sum sha1;
  sha1.update(aInputs, strlen(aInputs));
  return sha1;
}

static nsCString Finish(SHA1Sum& aSha1) {
  SHA1Sum::Hash digest;
  aSha1.finish(digest);
  nsCString id;
  MOZ_ALWAYS_SUCCEEDS(Base64Encode(reinterpret_cast<const char*>(digest),
                                   SHA1Sum::kHashSize, id));
  return id;
}

// The network ID that a process whose seed is aSeed computes.
static nsCString SeededNetworkId(const SHA1Sum& aUnseeded, const nsID& aSeed) {
  SHA1Sum sha1 = aUnseeded;
  sha1.update(&aSeed, sizeof(aSeed));
  return Finish(sha1);
}

// The network ID that this process computes.
static nsCString CurrentNetworkId(const SHA1Sum& aUnseeded) {
  SHA1Sum sha1 = aUnseeded;
  SeedNetworkId(sha1);
  return Finish(sha1);
}

static nsCString SeedString(const nsID& aSeed) {
  return nsCString(aSeed.ToString().get());
}

TEST(TestLinkServiceCommon, SameSeed)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);
  nsCString seed = GetNetworkIdSeed();
  ASSERT_FALSE(seed.IsEmpty());

  nsCString match;
  EXPECT_TRUE(MatchNetworkId(current, seed, Some(unseeded), current, match));
  EXPECT_STREQ(match.get(), current.get());
}

TEST(TestLinkServiceCommon, OtherSeedSameNetwork)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);
  nsID otherSeed = nsID::GenerateUUID();
  nsCString earlier = SeededNetworkId(unseeded, otherSeed);
  EXPECT_STRNE(earlier.get(), current.get());

  nsCString match;
  EXPECT_TRUE(MatchNetworkId(earlier, SeedString(otherSeed), Some(unseeded),
                             current, match));
  EXPECT_STREQ(match.get(), current.get());
}

TEST(TestLinkServiceCommon, OtherSeedOtherNetwork)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);
  nsID otherSeed = nsID::GenerateUUID();
  nsCString earlier =
      SeededNetworkId(UnseededNetworkId("network B"), otherSeed);

  nsCString match("stale"_ns);
  EXPECT_TRUE(MatchNetworkId(earlier, SeedString(otherSeed), Some(unseeded),
                             current, match));
  EXPECT_TRUE(match.IsEmpty());
}

TEST(TestLinkServiceCommon, WrongSeed)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);
  nsCString earlier = SeededNetworkId(unseeded, nsID::GenerateUUID());

  nsCString match("stale"_ns);
  EXPECT_TRUE(MatchNetworkId(earlier, SeedString(nsID::GenerateUUID()),
                             Some(unseeded), current, match));
  EXPECT_TRUE(match.IsEmpty());
}

TEST(TestLinkServiceCommon, EmptyOrUnparseableSeed)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);

  nsCString match("stale"_ns);
  EXPECT_TRUE(MatchNetworkId(current, ""_ns, Some(unseeded), current, match));
  EXPECT_TRUE(match.IsEmpty());
  match.AssignLiteral("stale");
  EXPECT_TRUE(
      MatchNetworkId(current, "not a seed"_ns, Some(unseeded), current, match));
  EXPECT_TRUE(match.IsEmpty());
}

TEST(TestLinkServiceCommon, EmptyNetworkId)
{
  SHA1Sum unseeded = UnseededNetworkId("network A");
  nsCString current = CurrentNetworkId(unseeded);

  nsCString match("stale"_ns);
  EXPECT_TRUE(MatchNetworkId(""_ns, GetNetworkIdSeed(), Some(unseeded), current,
                             match));
  EXPECT_TRUE(match.IsEmpty());
}

TEST(TestLinkServiceCommon, NoCurrentNetwork)
{
  nsCString match("stale"_ns);
  EXPECT_FALSE(MatchNetworkId("anything"_ns, GetNetworkIdSeed(), Nothing(),
                              ""_ns, match));
  EXPECT_TRUE(match.IsEmpty());
}
