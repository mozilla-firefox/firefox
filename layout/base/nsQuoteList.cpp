/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implementation of quotes for the CSS 'content' property */

#include "nsQuoteList.h"

#include "mozilla/ContainStyleScopeManager.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/Text.h"
#include "mozilla/intl/Quotes.h"
#include "nsContainerFrame.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsReadableUtils.h"

using namespace mozilla;

bool nsQuoteNode::InitTextFrame(nsGenConList* aList, nsIFrame* aPseudoFrame,
                                nsIFrame* aTextFrame) {
  nsGenConNode::InitTextFrame(aList, aPseudoFrame, aTextFrame);

  nsQuoteList* quoteList = static_cast<nsQuoteList*>(aList);
  bool dirty = false;
  quoteList->Insert(this);
  if (quoteList->IsLast(this)) {
    quoteList->Calc(this);
  } else {
    dirty = true;
  }

  // Don't set up text for 'no-open-quote' and 'no-close-quote'.
  if (IsRealQuote()) {
    aTextFrame->GetContent()->AsText()->SetText(Text(), false);
  }
  return dirty;
}

static nsString GetQuoteForFrame(nsIFrame* aFrame,
                                 nsCounterNode::StyleContentType aType,
                                 int32_t aDepth) {
  // Get the `quotes` property of the target (often a ::before/::after pseudo).
  const auto& quotesProp = aFrame->StyleList()->mQuotes;

  // For `match-parent` we simply recurse up the frame tree.
  if (quotesProp.IsMatchParent() && aFrame->GetParent()) {
    return GetQuoteForFrame(aFrame->GetParent(), aType, aDepth);
  }

  // For `auto` (or `match-parent` if there is no parent), choose quote marks
  // based on the content language of the frame's parent, or in the case of
  // ::before/::after pseudos, the language of the in-flow parent's parent.
  if (quotesProp.IsAuto() || quotesProp.IsMatchParent()) {
    // Frame whose parent we want to check:
    nsIFrame* frame =
        aFrame->Style()->IsPseudoElement() ? aFrame->GetInFlowParent() : aFrame;
    // Go to the parent, if any.
    if (frame->GetParent()) {
      frame = frame->GetParent();
    }
    // Look up CLDR-derived quotes for the language of the parent.
    const intl::Quotes* quotes =
        intl::QuotesForLang(frame->StyleFont()->GetLangAtom());
    // If we don't have quote-mark data for the language, use built-in
    // defaults.
    if (!quotes) {
      static const intl::Quotes sDefaultQuotes = {
          {0x201c, 0x201d, 0x2018, 0x2019}};
      quotes = &sDefaultQuotes;
    }
    // Select first or second pair based on quote nesting depth.
    size_t index = (aDepth == 0 ? 0 : 2);
    // Choose open or close quote from the pair.
    index += (aType == nsCounterNode::StyleContentType::OpenQuote ? 0 : 1);
    return nsString(&quotes->mChars[index], 1);
  }

  MOZ_ASSERT(quotesProp.IsQuoteList());
  const Span<const StyleQuotePair> quotes = quotesProp.AsQuoteList().AsSpan();

  // Reuse the last pair when the depth is greater than the number of
  // pairs of quotes.  (Also make 'quotes: none' and close-quote from
  // a depth of 0 equivalent for the next test.)
  if (aDepth >= static_cast<int32_t>(quotes.Length())) {
    aDepth = static_cast<int32_t>(quotes.Length()) - 1;
  }

  if (aDepth == -1) {
    // close-quote from a depth of 0 or 'quotes: none'
    return nsString();
  }

  const StyleQuotePair& pair = quotes[aDepth];
  const StyleOwnedStr& quote =
      aType == nsCounterNode::StyleContentType::OpenQuote ? pair.opening
                                                          : pair.closing;
  return NS_ConvertUTF8toUTF16(quote.AsString());
}

nsString nsQuoteNode::Text() {
  MOZ_ASSERT(mType == StyleContentType::OpenQuote ||
                 mType == StyleContentType::CloseQuote,
             "should only be called when mText should be non-null");
  int32_t depth = Depth();
  MOZ_ASSERT(depth >= -1);

  if (depth < 0) {
    return nsString();
  }

  return GetQuoteForFrame(mPseudoFrame, mType, depth);
}

static int32_t GetDepthBeforeFirstQuoteNode(ContainStyleScope* aScope) {
  for (auto* ancestor = aScope->GetParent(); ancestor;
       ancestor = ancestor->GetParent()) {
    auto& quoteList = ancestor->GetQuoteList();
    if (auto* node = static_cast<nsQuoteNode*>(
            aScope->GetPrecedingElementInGenConList(&quoteList))) {
      return node->DepthAfter();
    }
  }
  return 0;
}

void nsQuoteList::Calc(nsQuoteNode* aNode) {
  if (aNode == FirstNode()) {
    aNode->mDepthBefore = GetDepthBeforeFirstQuoteNode(mScope);
  } else {
    aNode->mDepthBefore = Prev(aNode)->DepthAfter();
  }
}

void nsQuoteList::RecalcAll() {
  for (nsQuoteNode* node = FirstNode(); node; node = Next(node)) {
    int32_t oldDepth = node->mDepthBefore;
    Calc(node);

    if (node->mDepthBefore != oldDepth && node->mText && node->IsRealQuote()) {
      node->mText->SetData(node->Text(), IgnoreErrors());
    }
  }
}

#ifdef DEBUG
void nsQuoteList::PrintChain() {
  using StyleContentType = nsQuoteNode::StyleContentType;

  printf("Chain: \n");
  for (nsQuoteNode* node = FirstNode(); node; node = Next(node)) {
    printf("  %p %d - ", static_cast<void*>(node), node->mDepthBefore);
    switch (node->mType) {
      case StyleContentType::OpenQuote:
        printf("open");
        break;
      case StyleContentType::NoOpenQuote:
        printf("noOpen");
        break;
      case StyleContentType::CloseQuote:
        printf("close");
        break;
      case StyleContentType::NoCloseQuote:
        printf("noClose");
        break;
      default:
        printf("unknown!!!");
    }
    printf(" %d - %d,", node->Depth(), node->DepthAfter());
    if (node->mText) {
      nsAutoString data;
      node->mText->GetData(data);
      printf(" \"%s\",", NS_ConvertUTF16toUTF8(data).get());
    }
    printf("\n");
  }
}
#endif
