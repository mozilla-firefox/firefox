/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/dom/Document.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsTextFragment.h"
#include "nsTextNode.h"
#include "nsString.h"

namespace mozilla::dom {

static already_AddRefed<Document> CreateHTMLDoc() {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "data:text/html,");

  RefPtr<BasePrincipal> principal =
      BasePrincipal::CreateContentPrincipal(uri, OriginAttributes());
  MOZ_RELEASE_ASSERT(principal);

  nsCOMPtr<mozilla::dom::Document> doc;
  MOZ_ALWAYS_SUCCEEDS(NS_NewDOMDocument(getter_AddRefs(doc),
                                        u""_ns,   // aNamespaceURI
                                        u""_ns,   // aQualifiedName
                                        nullptr,  // aDoctype
                                        uri, uri, principal,
                                        false,    // aLoadedAsData
                                        nullptr,  // aEventObject
                                        DocumentFlavor::HTML));
  MOZ_RELEASE_ASSERT(doc);
  return doc.forget();
}

template <typename CharType>
struct TestData {
  TestData(const CharType* aData, const CharType* aScanData,
           uint32_t aStartOffset, uint32_t aExpectedOffset)
      : mData(aData),
        mScanData(aScanData),
        mStartOffset(aStartOffset),
        mExpectedOffset(aExpectedOffset) {}

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const TestData& aData) {
    return aStream << "Scan \"" << aData.mScanData << "\" in \"" << aData.mData
                   << "\" starting from " << aData.mStartOffset;
  }

  const CharType* const mData;
  const CharType* const mScanData;
  const uint32_t mStartOffset;
  const uint32_t mExpectedOffset;
};

TEST(nsTextFragmentTest, FindFirstDifferentCharOffsetIn1b)
{
  const RefPtr<Document> doc = CreateHTMLDoc();
  const RefPtr<nsTextNode> textNode = doc->CreateTextNode(EmptyString());
  MOZ_RELEASE_ASSERT(textNode);
  const nsTextFragment& textFragment = textNode->TextFragment();

  for (const auto& testData : {
           TestData("abcdef", "abc", 0, nsTextFragment::kNotFound),
           TestData("abcdef", "Abc", 0, 0),
           TestData("abcdef", "aBc", 0, 1),
           TestData("abcdef", "abC", 0, 2),
           TestData("abcdef", "def", 3, nsTextFragment::kNotFound),
           TestData("abcdef", "Def", 3, 3),
           TestData("abcdef", "dEf", 3, 4),
           TestData("abcdef", "deF", 3, 5),
       }) {
    textNode->SetData(NS_ConvertUTF8toUTF16(testData.mData), IgnoreErrors());
    MOZ_ASSERT(!textFragment.Is2b());
    const uint32_t ret = textFragment.FindFirstDifferentCharOffset(
        nsDependentCString(testData.mScanData), testData.mStartOffset);
    EXPECT_EQ(ret, testData.mExpectedOffset) << testData;
  }
}

TEST(nsTextFragmentTest, FindFirstDifferentCharOffsetIn2b)
{
  const RefPtr<Document> doc = CreateHTMLDoc();
  const RefPtr<nsTextNode> textNode = doc->CreateTextNode(EmptyString());
  MOZ_RELEASE_ASSERT(textNode);
  textNode->MarkAsMaybeModifiedFrequently();
  const nsTextFragment& textFragment = textNode->TextFragment();

  for (const auto& testData : {
           TestData("abcdef", "abc", 0, nsTextFragment::kNotFound),
           TestData("abcdef", "Abc", 0, 0),
           TestData("abcdef", "aBc", 0, 1),
           TestData("abcdef", "abC", 0, 2),
           TestData("abcdef", "def", 3, nsTextFragment::kNotFound),
           TestData("abcdef", "Def", 3, 3),
           TestData("abcdef", "dEf", 3, 4),
           TestData("abcdef", "deF", 3, 5),
       }) {
    textNode->SetData(NS_ConvertUTF8toUTF16(testData.mData), IgnoreErrors());
    MOZ_ASSERT(textFragment.Is2b());
    const uint32_t ret = textFragment.FindFirstDifferentCharOffset(
        nsDependentCString(testData.mScanData), testData.mStartOffset);
    EXPECT_EQ(ret, testData.mExpectedOffset) << testData;
  }
}

TEST(nsTextFragmentTest, RFindFirstDifferentCharOffsetIn1b)
{
  const RefPtr<Document> doc = CreateHTMLDoc();
  const RefPtr<nsTextNode> textNode = doc->CreateTextNode(EmptyString());
  MOZ_RELEASE_ASSERT(textNode);
  const nsTextFragment& textFragment = textNode->TextFragment();

  for (const auto& testData : {
           TestData("abcdef", "abc", 3, nsTextFragment::kNotFound),
           TestData("abcdef", "Abc", 3, 0),
           TestData("abcdef", "aBc", 3, 1),
           TestData("abcdef", "abC", 3, 2),
           TestData("abcdef", "def", 6, nsTextFragment::kNotFound),
           TestData("abcdef", "Def", 6, 3),
           TestData("abcdef", "dEf", 6, 4),
           TestData("abcdef", "deF", 6, 5),
       }) {
    textNode->SetData(NS_ConvertUTF8toUTF16(testData.mData), IgnoreErrors());
    MOZ_ASSERT(!textFragment.Is2b());
    const uint32_t ret = textFragment.RFindFirstDifferentCharOffset(
        nsDependentCString(testData.mScanData), testData.mStartOffset);
    EXPECT_EQ(ret, testData.mExpectedOffset) << testData;
  }
}

TEST(nsTextFragmentTest, RFindFirstDifferentCharOffsetIn2b)
{
  const RefPtr<Document> doc = CreateHTMLDoc();
  const RefPtr<nsTextNode> textNode = doc->CreateTextNode(EmptyString());
  MOZ_RELEASE_ASSERT(textNode);
  textNode->MarkAsMaybeModifiedFrequently();
  const nsTextFragment& textFragment = textNode->TextFragment();

  for (const auto& testData : {
           TestData("abcdef", "abc", 3, nsTextFragment::kNotFound),
           TestData("abcdef", "Abc", 3, 0),
           TestData("abcdef", "aBc", 3, 1),
           TestData("abcdef", "abC", 3, 2),
           TestData("abcdef", "def", 6, nsTextFragment::kNotFound),
           TestData("abcdef", "Def", 6, 3),
           TestData("abcdef", "dEf", 6, 4),
           TestData("abcdef", "deF", 6, 5),
       }) {
    textNode->SetData(NS_ConvertUTF8toUTF16(testData.mData), IgnoreErrors());
    MOZ_ASSERT(textFragment.Is2b());
    const uint32_t ret = textFragment.RFindFirstDifferentCharOffset(
        nsDependentCString(testData.mScanData), testData.mStartOffset);
    EXPECT_EQ(ret, testData.mExpectedOffset) << testData;
  }
}

};  // namespace mozilla::dom
