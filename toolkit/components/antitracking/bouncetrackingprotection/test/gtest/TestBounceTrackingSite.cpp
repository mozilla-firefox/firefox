/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/BounceTrackingSite.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/OriginAttributes.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsTHashSet.h"

using namespace mozilla;

static already_AddRefed<nsIURI> MakeURI(const char* aSpec) {
  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), aSpec));
  return uri.forget();
}

static already_AddRefed<nsIPrincipal> MakePrincipal(
    const char* aSpec, const OriginAttributes& aOriginAttributes) {
  nsCOMPtr<nsIURI> uri = MakeURI(aSpec);
  return BasePrincipal::CreateContentPrincipal(uri, aOriginAttributes);
}

static OriginAttributes ContainerAttributes(uint32_t aUserContextId) {
  OriginAttributes oa;
  oa.mUserContextId = aUserContextId;
  return oa;
}

TEST(TestBounceTrackingSite, NullSite)
{
  BounceTrackingSite site;
  EXPECT_TRUE(site.IsNull());
  EXPECT_TRUE(site.SiteHost().IsEmpty());
  EXPECT_EQ(site,
            BounceTrackingSite::FromSiteHost(""_ns, ContainerAttributes(2)));
  EXPECT_EQ(fmt::format("{}", site), "null");
}

TEST(TestBounceTrackingSite, FromPrincipal)
{
  OriginAttributes oa = ContainerAttributes(2);
  oa.mFirstPartyDomain = u"example.org"_ns;
  oa.mPartitionKey = u"(https,example.org)"_ns;
  nsCOMPtr<nsIPrincipal> principal =
      MakePrincipal("https://www.example.com/path", oa);

  BounceTrackingSite site =
      BounceTrackingSite::FromPrincipal(principal).unwrap();
  EXPECT_FALSE(site.IsNull());
  EXPECT_TRUE(site.SiteHost().EqualsLiteral("example.com"));
  EXPECT_EQ(site.OriginAttributesRef(), ContainerAttributes(2));
}

TEST(TestBounceTrackingSite, FromPrincipalNotTracked)
{
  nsCOMPtr<nsIPrincipal> nullPrincipal =
      NullPrincipal::CreateWithoutOriginAttributes();
  EXPECT_TRUE(
      BounceTrackingSite::FromPrincipal(nullPrincipal).unwrap().IsNull());

  nsCOMPtr<nsIPrincipal> filePrincipal =
      MakePrincipal("file:///tmp/file.html", OriginAttributes());
  EXPECT_TRUE(
      BounceTrackingSite::FromPrincipal(filePrincipal).unwrap().IsNull());
}

TEST(TestBounceTrackingSite, FromURI)
{
  nsCOMPtr<nsIURI> uri = MakeURI("http://sub.example.co.uk/");
  BounceTrackingSite site =
      BounceTrackingSite::FromURI(uri, ContainerAttributes(2)).unwrap();
  EXPECT_TRUE(site.SiteHost().EqualsLiteral("example.co.uk"));
  EXPECT_EQ(site.OriginAttributesRef(), ContainerAttributes(2));

  nsCOMPtr<nsIURI> ipURI = MakeURI("https://127.0.0.1/");
  EXPECT_TRUE(BounceTrackingSite::FromURI(ipURI, OriginAttributes())
                  .unwrap()
                  .SiteHost()
                  .EqualsLiteral("127.0.0.1"));

  nsCOMPtr<nsIURI> aboutURI = MakeURI("about:blank");
  EXPECT_TRUE(BounceTrackingSite::FromURI(aboutURI, OriginAttributes())
                  .unwrap()
                  .IsNull());
}

TEST(TestBounceTrackingSite, PrincipalAndURIAgree)
{
  OriginAttributes oa = ContainerAttributes(2);
  nsCOMPtr<nsIPrincipal> principal = MakePrincipal("https://a.example.com", oa);
  nsCOMPtr<nsIURI> uri = MakeURI("https://b.example.com/");
  EXPECT_EQ(BounceTrackingSite::FromPrincipal(principal).unwrap(),
            BounceTrackingSite::FromURI(uri, oa).unwrap());
}

TEST(TestBounceTrackingSite, EqualityAndHashing)
{
  OriginAttributes withFirstPartyDomain = ContainerAttributes(2);
  withFirstPartyDomain.mFirstPartyDomain = u"example.org"_ns;

  BounceTrackingSite site = BounceTrackingSite::FromSiteHost(
      "example.com"_ns, ContainerAttributes(2));
  BounceTrackingSite sameSite =
      BounceTrackingSite::FromSiteHost("example.com"_ns, withFirstPartyDomain);
  BounceTrackingSite otherContainer = BounceTrackingSite::FromSiteHost(
      "example.com"_ns, ContainerAttributes(3));
  BounceTrackingSite otherHost = BounceTrackingSite::FromSiteHost(
      "example.org"_ns, ContainerAttributes(2));

  EXPECT_EQ(site, sameSite);
  EXPECT_EQ(site.Hash(), sameSite.Hash());
  EXPECT_NE(site, otherContainer);
  EXPECT_NE(site, otherHost);

  nsTHashSet<BounceTrackingSiteHashKey> set;
  set.Insert(site);
  EXPECT_TRUE(set.Contains(sameSite));
  EXPECT_FALSE(set.Contains(otherContainer));
  EXPECT_FALSE(set.Contains(otherHost));
}

TEST(TestBounceTrackingSite, Format)
{
  EXPECT_EQ(fmt::format("{}", BounceTrackingSite::FromSiteHost(
                                  "example.com"_ns, ContainerAttributes(2))),
            "example.com^userContextId=2");
  EXPECT_EQ(fmt::format("{}", BounceTrackingSite::FromSiteHost(
                                  "example.com"_ns, OriginAttributes())),
            "example.com");
}
