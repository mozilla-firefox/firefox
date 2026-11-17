/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/DOMParser.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsString.h"
#include "nsStubMutationObserver.h"
#include "nsTextNode.h"

namespace mozilla::dom {

class CharacterDataObserver final : public nsStubMutationObserver {
 public:
  NS_DECL_ISUPPORTS

  explicit CharacterDataObserver(nsINode& aNode) : mNode(&aNode) {
    mNode->AddMutationObserver(this);
  }

  void CharacterDataWillChange(nsIContent*,
                               const CharacterDataChangeInfo&) override {
    EXPECT_FALSE(nsContentUtils::IsSafeToRunScript());
    ++mWillChangeCount;
  }

  void CharacterDataChanged(nsIContent*,
                            const CharacterDataChangeInfo&) override {
    EXPECT_FALSE(nsContentUtils::IsSafeToRunScript());
    ++mChangedCount;
  }

  uint32_t mWillChangeCount = 0;
  uint32_t mChangedCount = 0;

 private:
  ~CharacterDataObserver() { mNode->RemoveMutationObserver(this); }

  RefPtr<nsINode> mNode;
};

NS_IMPL_ISUPPORTS(CharacterDataObserver, nsIMutationObserver)

class CharacterDataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IgnoredErrorResult rv;
    RefPtr<DOMParser> parser = DOMParser::CreateWithoutGlobal(rv);
    ASSERT_FALSE(rv.Failed());
    mDocument = parser->ParseFromStringInternal(
        u"<div id=parent dir=auto><span id=child>abc</span></div>"_ns,
        SupportedType::Text_html, rv);
    ASSERT_FALSE(rv.Failed());
    ASSERT_TRUE(mDocument);
    mParent = mDocument->GetElementById(u"parent"_ns);
    mChild = mDocument->GetElementById(u"child"_ns);
    ASSERT_TRUE(mParent);
    ASSERT_TRUE(mChild);
    mText = mChild->GetFirstChild()->AsText();
    ASSERT_TRUE(mText);
  }

  RefPtr<Document> mDocument;
  RefPtr<Element> mParent;
  RefPtr<Element> mChild;
  RefPtr<Text> mText;
};

TEST_F(CharacterDataTest, NonNotifyingChanges) {
  ASSERT_TRUE(nsContentUtils::IsSafeToRunScript());
  ASSERT_TRUE(mText->GetComposedDoc());
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Ltr);
  EXPECT_FALSE(mDocument->GetBidiEnabled());
  RefPtr<CharacterDataObserver> observer = new CharacterDataObserver(*mText);

  ASSERT_EQ(mText->SetText(u"\u05d0"_ns, false), NS_OK);
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Rtl);
  EXPECT_EQ(mChild->GetDirectionality(), Directionality::Rtl);
  EXPECT_TRUE(mDocument->GetBidiEnabled());
  EXPECT_FALSE(mText->TextIsOnlyWhitespace());

  ASSERT_EQ(mText->SetText(u" "_ns, false), NS_OK);
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Ltr);
  EXPECT_TRUE(mText->TextIsOnlyWhitespace());

  ASSERT_EQ(mText->AppendText(u"\u05d0", 1, false), NS_OK);
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Rtl);
  EXPECT_FALSE(mText->TextIsOnlyWhitespace());
  EXPECT_EQ(observer->mWillChangeCount, 0u);
  EXPECT_EQ(observer->mChangedCount, 0u);
}

TEST_F(CharacterDataTest, NonNotifyingSlottedChanges) {
  IgnoredErrorResult rv;
  ShadowRootInit init;
  init.mMode = ShadowRootMode::Open;
  RefPtr<ShadowRoot> shadow = mParent->AttachShadow(init, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_TRUE(shadow);
  RefPtr<Element> slot = mDocument->CreateHTMLElement(nsGkAtoms::slot);
  ASSERT_EQ(slot->SetAttr(kNameSpaceID_None, nsGkAtoms::dir, u"auto"_ns, true,
                          IsKnownNewAttr::Yes),
            NS_OK);
  shadow->AppendChild(*slot, rv);
  ASSERT_FALSE(rv.Failed());
  ASSERT_EQ(static_cast<Element*>(mChild->GetAssignedSlot()), slot.get());
  ASSERT_TRUE(nsContentUtils::IsSafeToRunScript());

  ASSERT_EQ(mText->SetText(u"\u05d0"_ns, false), NS_OK);
  EXPECT_EQ(slot->GetDirectionality(), Directionality::Rtl);
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Rtl);
  EXPECT_EQ(mChild->GetDirectionality(), Directionality::Rtl);

  ASSERT_EQ(mText->SetText(u"abc"_ns, false), NS_OK);
  EXPECT_EQ(slot->GetDirectionality(), Directionality::Ltr);
  EXPECT_EQ(mParent->GetDirectionality(), Directionality::Ltr);
  EXPECT_EQ(mChild->GetDirectionality(), Directionality::Ltr);
}

TEST_F(CharacterDataTest, NonNotifyingDetachedChanges) {
  ASSERT_TRUE(nsContentUtils::IsSafeToRunScript());
  RefPtr<nsTextNode> text = mDocument->CreateTextNode(u"\u05d0"_ns);
  ASSERT_FALSE(text->GetParentNode());
  EXPECT_TRUE(text->DataBuffer().IsBidi());
  EXPECT_FALSE(mDocument->GetBidiEnabled());

  ASSERT_EQ(text->SetText(u"abc"_ns, false), NS_OK);
  EXPECT_TRUE(text->DataBuffer().Equals(u"abc"_ns));
  EXPECT_FALSE(text->DataBuffer().IsBidi());
}

TEST_F(CharacterDataTest, NotifyingChangesBlockScript) {
  RefPtr<Text> detached = mDocument->CreateTextNode(u"abc"_ns);
  for (const RefPtr<Text>& text : {mText, detached}) {
    ASSERT_TRUE(nsContentUtils::IsSafeToRunScript());
    RefPtr<CharacterDataObserver> observer = new CharacterDataObserver(*text);
    ASSERT_EQ(text->SetText(u"changed"_ns, true), NS_OK);
    EXPECT_EQ(observer->mWillChangeCount, 1u);
    EXPECT_EQ(observer->mChangedCount, 1u);
    EXPECT_TRUE(nsContentUtils::IsSafeToRunScript());
  }
}

}  // namespace mozilla::dom
