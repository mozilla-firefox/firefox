/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextDirectiveCreator.h"

#include <algorithm>

#include "AbstractRange.h"
#include "Document.h"
#include "StaticRange.h"
#include "TextDirectiveUtil.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/ResultVariant.h"
#include "nsFind.h"
#include "nsINode.h"
#include "nsRange.h"

namespace mozilla::dom {

TextDirectiveCreator::TextDirectiveCreator(Document* aDocument,
                                           AbstractRange* aRange,
                                           const TimeoutWatchdog* aWatchdog)
    : mDocument(WrapNotNull(aDocument)),
      mRange(WrapNotNull(aRange)),
      mFinder(WrapNotNull(new nsFind())),
      mWatchdog(aWatchdog) {
  mFinder->SetNodeIndexCache(&mNodeIndexCache);
}

TextDirectiveCreator::~TextDirectiveCreator() {
  mFinder->SetNodeIndexCache(nullptr);
}

/* static */
mozilla::Result<nsCString, ErrorResult>
TextDirectiveCreator::CreateTextDirectiveFromRange(
    Document* aDocument, AbstractRange* aInputRange,
    const TimeoutWatchdog* aWatchdog) {
  MOZ_ASSERT(aInputRange);
  MOZ_ASSERT(!aInputRange->Collapsed());

  const RefPtr<AbstractRange> extendedRange =
      MOZ_TRY(ExtendRangeToWordBoundaries(aInputRange));
  if (!extendedRange) {
    return VoidCString();
  }
  UniquePtr<TextDirectiveCreator> instance =
      MOZ_TRY(CreateInstance(aDocument, extendedRange, aWatchdog));

  const bool succeededBuildingContextTerms =
      MOZ_TRY(instance->CollectContextTerms());
  if (!succeededBuildingContextTerms) {
    return VoidCString();
  }
  instance->CollectContextTermWordBoundaryDistances();
  MOZ_TRY(instance->FindAllMatchingCandidates());
  return instance->CreateTextDirective();
}

/* static */ Result<bool, ErrorResult>
TextDirectiveCreator::MustUseRangeBasedMatching(AbstractRange* aRange) {
  MOZ_ASSERT(aRange);
  if (TextDirectiveUtil::FindBlockBoundaryInRange<TextScanDirection::Right>(
          *aRange)
          .isSome()) {
    TEXT_FRAGMENT_LOG(
        "Use range-based matching because the target range contains a block "
        "boundary.");
    return true;
  }
  const nsString rangeContent =
      MOZ_TRY(TextDirectiveUtil::RangeContentAsString(aRange));

  const uint32_t kMaxLength = StaticPrefs::
      dom_text_fragments_create_text_fragment_exact_match_max_length();
  if (rangeContent.Length() <= kMaxLength) {
    TEXT_FRAGMENT_LOG("Use exact matching.");
    return false;
  }
  // Range-based matching divides the range content into a start and an end
  // term, which both need to contain a word. If there is only one word, exact
  // matching is the only option, regardless of the length of the range.
  if (!TextDirectiveUtil::ContainsAtLeastTwoWords(rangeContent)) {
    TEXT_FRAGMENT_LOG(
        "Use exact matching because the target range contains only one word, "
        "even though it is too long ({} chars > {} threshold).",
        rangeContent.Length(), kMaxLength);
    return false;
  }
  TEXT_FRAGMENT_LOG(
      "Use range-based matching because the target range is too long "
      "({} chars > {} threshold)",
      rangeContent.Length(), kMaxLength);
  return true;
}

Result<UniquePtr<TextDirectiveCreator>, ErrorResult>
TextDirectiveCreator::CreateInstance(Document* aDocument, AbstractRange* aRange,
                                     const TimeoutWatchdog* aWatchdog) {
  return MOZ_TRY(MustUseRangeBasedMatching(aRange))
             ? UniquePtr<TextDirectiveCreator>(
                   new RangeBasedTextDirectiveCreator(aDocument, aRange,
                                                      aWatchdog))
             : UniquePtr<TextDirectiveCreator>(
                   new ExactMatchTextDirectiveCreator(aDocument, aRange,
                                                      aWatchdog));
}

/*static*/
Result<RefPtr<AbstractRange>, ErrorResult>
TextDirectiveCreator::ExtendRangeToWordBoundaries(AbstractRange* aRange) {
  MOZ_ASSERT(aRange && !aRange->Collapsed());
  ErrorResult rv;
  const nsString rangeContent =
      MOZ_TRY(TextDirectiveUtil::RangeContentAsString(aRange));
  TEXT_FRAGMENT_LOG("Input range :\n{}", NS_ConvertUTF16toUTF8(rangeContent));

  if (rangeContent.IsEmpty()) {
    TEXT_FRAGMENT_LOG("Input range does not contain text.");
    return {nullptr};
  }

  if (std::all_of(rangeContent.View().cbegin(), rangeContent.View().cend(),
                  nsContentUtils::IsHTMLWhitespaceOrNBSP)) {
    TEXT_FRAGMENT_LOG("Input range contains only whitespace.");
    return {nullptr};
  }
  if (std::all_of(rangeContent.View().cbegin(), rangeContent.View().cend(),
                  IsPunctuationForWordSelect)) {
    RangeBoundary startPoint = TextDirectiveUtil::FindNextNonWhitespacePosition<
        TextScanDirection::Right>(aRange->StartRef());
    RangeBoundary endPoint = TextDirectiveUtil::FindNextNonWhitespacePosition<
        TextScanDirection::Left>(aRange->EndRef());
    RefPtr range = StaticRange::Create(startPoint, endPoint, rv);
    if (MOZ_UNLIKELY(rv.Failed())) {
      return Err(std::move(rv));
    }
    return {range};
  }
  RangeBoundary startPoint = TextDirectiveUtil::FindNextNonWhitespacePosition<
      TextScanDirection::Right>(aRange->StartRef());
  startPoint = TextDirectiveUtil::FindWordBoundary<TextScanDirection::Left>(
      startPoint, TextDirectiveUtil::BreakOnPunctuation::Yes);

  RangeBoundary endPoint =
      TextDirectiveUtil::FindNextNonWhitespacePosition<TextScanDirection::Left>(
          aRange->EndRef());
  endPoint = TextDirectiveUtil::FindWordBoundary<TextScanDirection::Right>(
      endPoint, TextDirectiveUtil::BreakOnPunctuation::Yes);
#if MOZ_DIAGNOSTIC_ASSERT_ENABLED
  auto cmp = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
      startPoint, endPoint);
  MOZ_DIAGNOSTIC_ASSERT(
      cmp && *cmp != 1,
      "The new end point must not be before the start point.");
#endif

  if (startPoint.IsSetAndValid() && endPoint.IsSetAndValid()) {
    ErrorResult rv;
    RefPtr<AbstractRange> range = StaticRange::Create(startPoint, endPoint, rv);
    if (MOZ_UNLIKELY(rv.Failed())) {
      return Err(std::move(rv));
    }
    if (!range->Collapsed()) {
      TEXT_FRAGMENT_LOG(
          "Expanded target range to word boundaries:\n{}",
          NS_ConvertUTF16toUTF8(
              TextDirectiveUtil::RangeContentAsString(range).unwrapOr(
                  u"<Could not be converted to string>"_ns)));

      return range;
    }
  }
  TEXT_FRAGMENT_LOG("Extending to word boundaries collapsed the range.");
  return {nullptr};
}

Result<bool, ErrorResult>
ExactMatchTextDirectiveCreator::CollectContextTerms() {
  if (MOZ_UNLIKELY(mRange->Collapsed())) {
    return false;
  }
  TEXT_FRAGMENT_LOG("Collecting context terms for the target range.");
  MOZ_TRY(CollectPrefixContextTerm());
  MOZ_TRY(CollectSuffixContextTerm());
  mStartContent = MOZ_TRY(TextDirectiveUtil::RangeContentAsString(mRange));
  if (mStartContent.Length() > kMaxContextTermLength) {
    TEXT_FRAGMENT_LOG(
        "Start term is too long ({} chars > {} threshold) and cannot be "
        "truncated. Aborting.",
        mStartContent.Length(), kMaxContextTermLength);
    return false;
  }
  TEXT_FRAGMENT_LOG("Start term:\n{}", NS_ConvertUTF16toUTF8(mStartContent));
  TEXT_FRAGMENT_LOG("No end term present (exact match).");
  return true;
}

Result<bool, ErrorResult>
RangeBasedTextDirectiveCreator::CollectContextTerms() {
  if (MOZ_UNLIKELY(mRange->Collapsed())) {
    return false;
  }
  TEXT_FRAGMENT_LOG("Collecting context terms for the target range.");
  MOZ_TRY(CollectPrefixContextTerm());
  MOZ_TRY(CollectSuffixContextTerm());
  if (const Maybe<RangeBoundary> firstBlockBoundaryInRange =
          TextDirectiveUtil::FindBlockBoundaryInRange<TextScanDirection::Right>(
              *mRange)) {
    TEXT_FRAGMENT_LOG(
        "Target range contains a block boundary, collecting start and end "
        "terms by considering the closest block boundaries inside the range.");
    ErrorResult rv;
    RefPtr startRange =
        StaticRange::Create(mRange->StartRef(), *firstBlockBoundaryInRange, rv);
    if (MOZ_UNLIKELY(rv.Failed())) {
      return Err(std::move(rv));
    }
    MOZ_DIAGNOSTIC_ASSERT(!startRange->Collapsed());
    mStartContent =
        MOZ_TRY(TextDirectiveUtil::RangeContentAsString(startRange));
    if (MOZ_UNLIKELY(mStartContent.IsEmpty())) {
      TEXT_FRAGMENT_LOG("Somehow got empty start term. Aborting.");
      return false;
    }
    const Maybe<RangeBoundary> lastBlockBoundaryInRange =
        TextDirectiveUtil::FindBlockBoundaryInRange<TextScanDirection::Left>(
            *mRange);
    MOZ_DIAGNOSTIC_ASSERT(
        lastBlockBoundaryInRange.isSome(),
        "If the target range contains a block boundary looking left-to-right, "
        "it must also contain one looking right-to-left");
    RefPtr endRange =
        StaticRange::Create(*lastBlockBoundaryInRange, mRange->EndRef(), rv);
    if (MOZ_UNLIKELY(rv.Failed())) {
      return Err(std::move(rv));
    }
    MOZ_DIAGNOSTIC_ASSERT(!endRange->Collapsed());
    mEndContent = MOZ_TRY(TextDirectiveUtil::RangeContentAsString(endRange));
    if (MOZ_UNLIKELY(mEndContent.IsEmpty())) {
      TEXT_FRAGMENT_LOG("Somehow got empty end term. Aborting.");
      return false;
    }
  } else {
    TEXT_FRAGMENT_LOG(
        "Target range is too long, collecting start and end by dividing "
        "content in the middle.");
    mStartContent = MOZ_TRY(TextDirectiveUtil::RangeContentAsString(mRange));
    MOZ_DIAGNOSTIC_ASSERT(
        mStartContent.Length() >
        StaticPrefs::
            dom_text_fragments_create_text_fragment_exact_match_max_length());
    const auto [wordStart, wordEnd] =
        intl::WordBreaker::FindWord(mStartContent, mStartContent.Length() / 2);
    // This check is fine because the range content strings have compressed
    // whitespace, therefore first and last character cannot be whitespace.
    if (wordStart == 0 && wordEnd == mStartContent.Length()) {
      TEXT_FRAGMENT_LOG(
          "Target range only contains one word, which is longer than the "
          "maximum length. Aborting.");
      return false;
    }

    // These cases are hit when `mStartContent` contains a very large (>50% of
    // the total length) word. Then the wordbreaker would return wordEnd=length
    // if the long word is at the end of the string. In that case, use the
    // wordStart position to break, so that mEndContent is not empty.
    if (wordEnd == mStartContent.Length()) {
      mEndContent = Substring(mStartContent, wordStart);
      mStartContent = Substring(mStartContent, 0, wordStart);
    } else {
      mEndContent = Substring(mStartContent, wordEnd);
      mStartContent = Substring(mStartContent, 0, wordEnd);
    }
  }
  if (mStartContent.Length() > kMaxContextTermLength) {
    TEXT_FRAGMENT_LOG(
        "Start term seems very long ({} chars), "
        "only considering the first {} chars.",
        mStartContent.Length(), kMaxContextTermLength);
    mStartContent = Substring(mStartContent, 0, kMaxContextTermLength);
  }
  mStartContent.CompressWhitespace();
  mStartFoldCaseContent = mStartContent;
  ToFoldedCase(mStartFoldCaseContent);
  TEXT_FRAGMENT_LOG("Maximum possible start term:\n{}",
                    NS_ConvertUTF16toUTF8(mStartContent));
  if (mEndContent.Length() > kMaxContextTermLength) {
    TEXT_FRAGMENT_LOG(
        "End term seems very long ({} chars), "
        "only considering the last {} chars.",
        mEndContent.Length(), kMaxContextTermLength);
    mEndContent =
        Substring(mEndContent, mEndContent.Length() - kMaxContextTermLength);
  }
  mEndContent.CompressWhitespace();
  mEndFoldCaseContent = mEndContent;
  ToFoldedCase(mEndFoldCaseContent);
  TEXT_FRAGMENT_LOG("Maximum possible end term:\n{}",
                    NS_ConvertUTF16toUTF8(mEndContent));
  return true;
}

Result<Ok, ErrorResult> TextDirectiveCreator::CollectPrefixContextTerm() {
  TEXT_FRAGMENT_LOG("Collecting prefix term for the target range.");
  ErrorResult rv;
  RangeBoundary prefixEnd =
      TextDirectiveUtil::FindNextNonWhitespacePosition<TextScanDirection::Left>(
          mRange->StartRef());
  RangeBoundary prefixStart =
      TextDirectiveUtil::FindNextBlockBoundary<TextScanDirection::Left>(
          prefixEnd);
  RefPtr prefixRange = StaticRange::Create(prefixStart, prefixEnd, rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return Err(std::move(rv));
  }
  MOZ_ASSERT(prefixRange);
  mPrefixContent =
      MOZ_TRY(TextDirectiveUtil::RangeContentAsString(prefixRange));
  if (mPrefixContent.Length() > kMaxContextTermLength) {
    // cut off the prefix content at a word boundary near the max context term
    // length to make sure the term does not start with whitespace.
    auto [wordBegin, wordEnd] = intl::WordBreaker::FindWord(
        mPrefixContent, mPrefixContent.Length() - kMaxContextTermLength);
    while (nsContentUtils::IsHTMLWhitespace(mPrefixContent.CharAt(wordBegin))) {
      ++wordBegin;
    }
    TEXT_FRAGMENT_LOG(
        "Prefix term seems very long ({} chars), "
        "only considering the last {} chars.",
        mPrefixContent.Length(), wordBegin);
    mPrefixContent = Substring(mPrefixContent, wordBegin);
  }
  mPrefixFoldCaseContent = mPrefixContent;
  ToFoldedCase(mPrefixFoldCaseContent);
  TEXT_FRAGMENT_LOG("Maximum possible prefix term:\n{}",
                    NS_ConvertUTF16toUTF8(mPrefixContent));
  return Ok();
}

Result<Ok, ErrorResult> TextDirectiveCreator::CollectSuffixContextTerm() {
  TEXT_FRAGMENT_LOG("Collecting suffix term for the target range.");
  ErrorResult rv;
  RangeBoundary suffixBegin = TextDirectiveUtil::FindNextNonWhitespacePosition<
      TextScanDirection::Right>(mRange->EndRef());
  RangeBoundary suffixEnd =
      TextDirectiveUtil::FindNextBlockBoundary<TextScanDirection::Right>(
          suffixBegin);
  RefPtr suffixRange = StaticRange::Create(suffixBegin, suffixEnd, rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return Err(std::move(rv));
  }
  MOZ_ASSERT(suffixRange);
  mSuffixContent =
      MOZ_TRY(TextDirectiveUtil::RangeContentAsString(suffixRange));
  if (mSuffixContent.Length() > kMaxContextTermLength) {
    // cut off the suffix content at a word boundary near the max context term
    // length to make sure the term does not end with whitespace.
    auto [wordBegin, wordEnd] =
        intl::WordBreaker::FindWord(mSuffixContent, kMaxContextTermLength);
    while (
        nsContentUtils::IsHTMLWhitespace(mSuffixContent.CharAt(wordEnd - 1))) {
      --wordEnd;
    }
    TEXT_FRAGMENT_LOG(
        "Suffix term seems very long ({} chars), "
        "only considering the first {} chars.",
        mSuffixContent.Length(), wordEnd + 1);
    mSuffixContent = Substring(mSuffixContent, 0, wordEnd + 1);
  }
  mSuffixFoldCaseContent = mSuffixContent;
  ToFoldedCase(mSuffixFoldCaseContent);
  TEXT_FRAGMENT_LOG("Maximum possible suffix term:\n{}",
                    NS_ConvertUTF16toUTF8(mSuffixContent));
  return Ok();
}

void ExactMatchTextDirectiveCreator::CollectContextTermWordBoundaryDistances() {
  mPrefixWordBeginDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Left>(
          mPrefixContent);
  TEXT_FRAGMENT_LOG("Word begin distances for prefix term: {}",
                    mPrefixWordBeginDistances);
  mSuffixWordEndDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Right>(
          mSuffixContent);
  TEXT_FRAGMENT_LOG("Word end distances for suffix term: {}",
                    mSuffixWordEndDistances);
}

void RangeBasedTextDirectiveCreator::CollectContextTermWordBoundaryDistances() {
  mPrefixWordBeginDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Left>(
          mPrefixContent);
  TEXT_FRAGMENT_LOG("Word begin distances for prefix term: {}",
                    mPrefixWordBeginDistances);
  MOZ_DIAGNOSTIC_ASSERT(!mStartContent.IsEmpty());
  mStartWordEndDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Right>(
          mStartContent);
  MOZ_DIAGNOSTIC_ASSERT(!mStartWordEndDistances.IsEmpty(),
                        "There must be at least one word in the start term.");
  MOZ_DIAGNOSTIC_ASSERT(mStartWordEndDistances[0] > 0);
  mFirstWordOfStartContent =
      Substring(mStartContent, 0, mStartWordEndDistances[0]);
  TEXT_FRAGMENT_LOG("First word of start term: {}",
                    NS_ConvertUTF16toUTF8(mFirstWordOfStartContent));
  if (mStartWordEndDistances[0] == mStartContent.Length()) {
    mStartFirstWordLengthIncludingWhitespace = mStartContent.Length();
    mStartWordEndDistances.Clear();
    TEXT_FRAGMENT_LOG("Start term cannot be extended.");
  } else {
    mStartFirstWordLengthIncludingWhitespace =
        TextDirectiveUtil::RemoveFirstWordFromStringAndDistanceArray<
            TextScanDirection::Right>(mStartFoldCaseContent,
                                      mStartWordEndDistances);
    TEXT_FRAGMENT_LOG(
        "Word end distances for start term, starting at the beginning of the "
        "second word: {}",
        mStartWordEndDistances);
  }

  MOZ_DIAGNOSTIC_ASSERT(!mEndContent.IsEmpty());
  mEndWordBeginDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Left>(
          mEndContent);
  MOZ_DIAGNOSTIC_ASSERT(!mEndWordBeginDistances.IsEmpty(),
                        "There must be at least one word in the end term.");
  MOZ_DIAGNOSTIC_ASSERT(mEndWordBeginDistances[0] > 0);
  mLastWordOfEndContent =
      Substring(mEndContent, mEndContent.Length() - mEndWordBeginDistances[0]);
  TEXT_FRAGMENT_LOG("Last word of end term: {}",
                    NS_ConvertUTF16toUTF8(mLastWordOfEndContent));
  if (mEndWordBeginDistances[0] == mEndContent.Length()) {
    mEndLastWordLengthIncludingWhitespace = mEndContent.Length();
    mEndWordBeginDistances.Clear();
    TEXT_FRAGMENT_LOG("End term cannot be extended.");
  } else {
    mEndLastWordLengthIncludingWhitespace =
        TextDirectiveUtil::RemoveFirstWordFromStringAndDistanceArray<
            TextScanDirection::Left>(mEndFoldCaseContent,
                                     mEndWordBeginDistances);
    TEXT_FRAGMENT_LOG(
        "Word begin distances for end term, starting at the end of the second "
        "last word: {}",
        mEndWordBeginDistances);
  }

  mSuffixWordEndDistances =
      TextDirectiveUtil::ComputeWordBoundaryDistances<TextScanDirection::Right>(
          mSuffixContent);
  TEXT_FRAGMENT_LOG("Word end distances for suffix term: {}",
                    mSuffixWordEndDistances);
}

Result<nsTArray<RefPtr<AbstractRange>>, ErrorResult>
TextDirectiveCreator::FindAllMatchingRanges(const nsString& aSearchQuery,
                                            const RangeBoundary& aSearchStart,
                                            const RangeBoundary& aSearchEnd) {
  MOZ_ASSERT(!aSearchQuery.IsEmpty());
  RangeBoundary searchStart = aSearchStart;
  nsTArray<RefPtr<AbstractRange>> matchingRanges;

  while (true) {
    if (mWatchdog && mWatchdog->IsDone()) {
      return matchingRanges;
    }
    RefPtr<AbstractRange> searchResult = TextDirectiveUtil::FindStringInRange(
        mFinder, searchStart, aSearchEnd, aSearchQuery, true, true);
    if (!searchResult || searchResult->Collapsed()) {
      break;
    }
    searchStart = searchResult->StartRef();
    if (auto cmp = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
            searchStart, aSearchEnd, &mNodeIndexCache);
        !cmp || *cmp != -1) {
      // this means hitting a bug in nsFind which apparently does not stop
      // exactly where it is told to. There are cases where it might
      // overshoot, e.g. if `aSearchEnd` is  a text node with offset=0.
      // However, due to reusing the cache used by nsFind this additional call
      // to ComparePoints should be very cheap.
      break;
    }
    matchingRanges.AppendElement(searchResult);

    MOZ_DIAGNOSTIC_ASSERT(searchResult->GetStartContainer()->IsText());
    auto newSearchStart =
        TextDirectiveUtil::MoveToNextBoundaryPoint(searchStart);
    MOZ_DIAGNOSTIC_ASSERT(newSearchStart != searchStart);
    searchStart = newSearchStart;
    if (auto cmp = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
            searchStart, aSearchEnd, &mNodeIndexCache);
        !cmp || *cmp != -1) {
      break;
    }
  }

  TEXT_FRAGMENT_LOG(
      "Found {} matches for the input '{}' in the partial document.",
      matchingRanges.Length(), NS_ConvertUTF16toUTF8(aSearchQuery));
  return matchingRanges;
}

Result<Ok, ErrorResult>
ExactMatchTextDirectiveCreator::FindAllMatchingCandidates() {
  if (MOZ_UNLIKELY(mRange->Collapsed())) {
    return Ok();
  }

  TEXT_FRAGMENT_LOG(
      "Searching all occurrences of range content ({}) in the partial document "
      "from document begin to begin of target range.",
      NS_ConvertUTF16toUTF8(mStartContent));
  const nsTArray<RefPtr<AbstractRange>> matchRanges =
      MOZ_TRY(FindAllMatchingRanges(mStartContent, {mDocument, 0u},
                                    mRange->StartRef()));
  FindCommonSubstringLengths(matchRanges);
  return Ok();
}

void ExactMatchTextDirectiveCreator::FindCommonSubstringLengths(
    const nsTArray<RefPtr<AbstractRange>>& aMatchRanges) {
  if (mWatchdog && mWatchdog->IsDone()) {
    return;
  }
  size_t loopCounter = 0;
  for (const auto& range : aMatchRanges) {
    TEXT_FRAGMENT_LOG("Computing common prefix substring length for match {}.",
                      ++loopCounter);
    const uint32_t commonPrefixLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Left>(
            mPrefixFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Left>(range->StartRef()));

    TEXT_FRAGMENT_LOG("Computing common suffix substring length for match {}.",
                      loopCounter);
    const uint32_t commonSuffixLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Right>(
            mSuffixFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Right>(range->EndRef()));

    mCommonSubstringLengths.EmplaceBack(commonPrefixLength, commonSuffixLength);
  }
}

Result<Ok, ErrorResult>
RangeBasedTextDirectiveCreator::FindAllMatchingCandidates() {
  MOZ_DIAGNOSTIC_ASSERT(!mFirstWordOfStartContent.IsEmpty(),
                        "Minimal start content must not be empty.");
  MOZ_DIAGNOSTIC_ASSERT(!mLastWordOfEndContent.IsEmpty(),
                        "Minimal end content must not be empty.");

  TEXT_FRAGMENT_LOG(
      "Searching all occurrences of first word of start content ({}) in the "
      "partial document from document begin to begin of the target range.",
      NS_ConvertUTF16toUTF8(mFirstWordOfStartContent));

  const nsTArray<RefPtr<AbstractRange>> startContentRanges =
      MOZ_TRY(FindAllMatchingRanges(mFirstWordOfStartContent, {mDocument, 0u},
                                    mRange->StartRef()));
  FindStartMatchCommonSubstringLengths(startContentRanges);

  if (mWatchdog && mWatchdog->IsDone()) {
    return Ok();
  }
  // Skip past the first word of the start content. Per "find a range from a
  // text directive", the search for the end content should start at the end of
  // the start content.
  // https://wicg.github.io/scroll-to-text-fragment/#find-a-range-from-a-text-directive
  // 2.4 Let rangeEndSearchRange be a range whose start is potentialMatch’s end
  //     and whose end is searchRange’s end.
  auto searchStart =
      TextDirectiveUtil::FindWordBoundary<TextScanDirection::Right>(
          mRange->StartRef(), TextDirectiveUtil::BreakOnPunctuation::No);

  auto searchEnd =
      TextDirectiveUtil::FindNextNonWhitespacePosition<TextScanDirection::Left>(
          mRange->EndRef());
  searchEnd = TextDirectiveUtil::FindWordBoundary<TextScanDirection::Left>(
      searchEnd, TextDirectiveUtil::BreakOnPunctuation::No);

  TEXT_FRAGMENT_LOG(
      "Searching all occurrences of last word of end content ({}) in the "
      "partial document from after the first word of the start content to "
      "the end of the target range, excluding the last word.",
      NS_ConvertUTF16toUTF8(mLastWordOfEndContent));

  const nsTArray<RefPtr<AbstractRange>> endContentRanges = MOZ_TRY(
      FindAllMatchingRanges(mLastWordOfEndContent, searchStart, searchEnd));
  FindEndMatchCommonSubstringLengths(endContentRanges);
  return Ok();
}

void RangeBasedTextDirectiveCreator::FindStartMatchCommonSubstringLengths(
    const nsTArray<RefPtr<AbstractRange>>& aMatchRanges) {
  size_t loopCounter = 0;
  for (const auto& range : aMatchRanges) {
    if (mWatchdog && mWatchdog->IsDone()) {
      return;
    }
    ++loopCounter;
    TEXT_FRAGMENT_LOG(
        "Computing common prefix substring length for start match {}.",
        loopCounter);
    const uint32_t commonPrefixLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Left>(
            mPrefixFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Left>(range->StartRef()));
    TEXT_FRAGMENT_LOG("Common prefix length: {}", commonPrefixLength);

    TEXT_FRAGMENT_LOG(
        "Computing common start substring length for start match {}.",
        loopCounter);
    const uint32_t commonStartLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Right>(
            mStartFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Right>(range->EndRef()));

    TEXT_FRAGMENT_LOG("Common length: {}", commonStartLength);
    mStartMatchCommonSubstringLengths.EmplaceBack(commonPrefixLength,
                                                  commonStartLength);
  }
}

void RangeBasedTextDirectiveCreator::FindEndMatchCommonSubstringLengths(
    const nsTArray<RefPtr<AbstractRange>>& aMatchRanges) {
  size_t loopCounter = 0;
  for (const auto& range : aMatchRanges) {
    if (mWatchdog && mWatchdog->IsDone()) {
      return;
    }
    ++loopCounter;
    TEXT_FRAGMENT_LOG("Computing common end substring length for end match {}.",
                      loopCounter);
    const uint32_t commonEndLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Left>(
            mEndFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Left>(range->StartRef()));
    TEXT_FRAGMENT_LOG("Common end term length: {}", commonEndLength);
    TEXT_FRAGMENT_LOG(
        "Computing common suffix substring length for end match {}.",
        loopCounter);
    const uint32_t commonSuffixLength =
        TextDirectiveUtil::ComputeCommonSubstringLength<
            TextScanDirection::Right>(
            mSuffixFoldCaseContent,
            TextDirectiveUtil::FindNextNonWhitespacePosition<
                TextScanDirection::Right>(range->EndRef()));
    TEXT_FRAGMENT_LOG("Common suffix length: {}", commonSuffixLength);

    mEndMatchCommonSubstringLengths.EmplaceBack(commonEndLength,
                                                commonSuffixLength);
  }
}

Result<nsCString, ErrorResult> TextDirectiveCreator::CreateTextDirective() {
  if (mWatchdog && mWatchdog->IsDone()) {
    TEXT_FRAGMENT_LOG("Hitting timeout.");
    return VoidCString();
  }
  if (mRange->Collapsed()) {
    TEXT_FRAGMENT_LOG("Input range collapsed.");
    return VoidCString();
  }
  if (mStartContent.IsEmpty()) {
    TEXT_FRAGMENT_LOG("Input range is empty.");
    return VoidCString();
  }

  if (const Maybe<TextDirective> textDirective = FindShortestCombination()) {
    nsCString textDirectiveString;
    DebugOnly<bool> ret =
        create_text_directive(&*textDirective, &textDirectiveString);
    MOZ_ASSERT(ret);
    TEXT_FRAGMENT_LOG("Created text directive: {}", textDirectiveString);
    return textDirectiveString;
  }
  TEXT_FRAGMENT_LOG(
      "It's not possible to create a text directive for the given range.");
  return nsCString{};
}

/*static*/ std::tuple<nsTArray<uint32_t>, nsTArray<uint32_t>>
TextDirectiveCreator::ExtendSubstringLengthsToWordBoundaries(
    const nsTArray<std::tuple<uint32_t, uint32_t>>& aExactSubstringLengths,
    const Span<const uint32_t>& aFirstWordPositions,
    const Span<const uint32_t>& aSecondWordPositions) {
  const auto getNextWordBoundaryPosition =
      [](const Span<const uint32_t>& distances, uint32_t length) {
        // Note: This algorithm works for word begins and word ends,
        //       since the position arrays for properties that go right-to-left
        //       (prefix, end) are reversed and start from the end of the
        //       strings.
        for (const uint32_t distance : distances) {
          if (distance > length) {
            return distance;
          }
        }
        return distances.IsEmpty() ? 0 : distances.at(distances.Length() - 1);
      };

  const auto hashSetToSortedArray = [](const nsTHashSet<uint32_t>& aHashSet) {
    AutoTArray<uint32_t, 64> array;
    for (auto value : aHashSet) {
      array.InsertElementSorted(value);
    }
    return array;
  };
  nsTHashSet<uint32_t> firstSet;
  nsTHashSet<uint32_t> secondSet;
  firstSet.Insert(0);
  secondSet.Insert(0);
  // This loop is O(n^2) in the worst case, but the number of
  // aFirstWordPositions and aSecondWordPositions is small (< 32).
  // Also, one of the purposes of this algorithm is to bucket the exact lengths
  // (which represent the amount of matches for the target range) into word
  // bounded lengths. This means that the number of unique word bounded lengths
  // is < 32.
  for (const auto& [first, second] : aExactSubstringLengths) {
    firstSet.Insert(getNextWordBoundaryPosition(aFirstWordPositions, first));
    secondSet.Insert(getNextWordBoundaryPosition(aSecondWordPositions, second));
  }
  return {hashSetToSortedArray(firstSet), hashSetToSortedArray(secondSet)};
}

/*static*/
Maybe<std::tuple<uint32_t, uint32_t>>
TextDirectiveCreator::CheckAllCombinations(
    const nsTArray<std::tuple<uint32_t, uint32_t>>& aExactWordLengths,
    const nsTArray<uint32_t>& aFirstExtendedToWordBoundaries,
    const nsTArray<uint32_t>& aSecondExtendedToWordBoundaries) {
  nsTArray<std::tuple<uint32_t, uint32_t, uint32_t>> sortedCandidates;
  sortedCandidates.SetCapacity(aFirstExtendedToWordBoundaries.Length() *
                               aSecondExtendedToWordBoundaries.Length());

  // Create all combinations of the extended values and sort them by their
  // cost function value (sum of the two values).
  // The cost function value is used to sort the candidates, so that the
  // candidates with the lowest cost function value are checked first. Since the
  // algorithm searches for the shortest possible combination, it can return as
  // soon as it finds a valid combination.
  for (const uint32_t firstExtendedToWordBoundary :
       aFirstExtendedToWordBoundaries) {
    for (const uint32_t secondExtendedToWordBoundary :
         aSecondExtendedToWordBoundaries) {
      const uint32_t costFunctionValue =
          firstExtendedToWordBoundary + secondExtendedToWordBoundary;
      sortedCandidates.InsertElementSorted(
          std::tuple{firstExtendedToWordBoundary, secondExtendedToWordBoundary,
                     costFunctionValue},
          [](const auto& a, const auto& b) -> int {
            return std::get<2>(a) - std::get<2>(b);
          });
    }
  }
  for (auto [firstExtendedToWordBoundary, secondExtendedToWordBoundary,
             costFunctionValue] : sortedCandidates) {
    TEXT_FRAGMENT_LOG("Checking candidate ({},{}). Score: {}",
                      firstExtendedToWordBoundary, secondExtendedToWordBoundary,
                      costFunctionValue);
    const bool isInvalid = std::any_of(
        aExactWordLengths.begin(), aExactWordLengths.end(),
        [firstExtended = firstExtendedToWordBoundary,
         secondExtended = secondExtendedToWordBoundary](
            const std::tuple<uint32_t, uint32_t>& exactWordLengths) {
          const auto [firstExact, secondExact] = exactWordLengths;
          return firstExtended <= firstExact && secondExtended <= secondExact;
        });
    if (isInvalid) {
      TEXT_FRAGMENT_LOG(
          "Current candidate doesn't eliminate all matches. Discarding this "
          "candidate.");
      continue;
    }
    TEXT_FRAGMENT_LOG("Current candidate ({},{}) is the best candidate.",
                      firstExtendedToWordBoundary,
                      secondExtendedToWordBoundary);
    return Some(
        std::tuple{firstExtendedToWordBoundary, secondExtendedToWordBoundary});
  }
  return Nothing{};
}

Maybe<TextDirective> ExactMatchTextDirectiveCreator::FindShortestCombination()
    const {
  const auto [prefixLengths, suffixLengths] =
      TextDirectiveCreator::ExtendSubstringLengthsToWordBoundaries(
          mCommonSubstringLengths, mPrefixWordBeginDistances,
          mSuffixWordEndDistances);
  TEXT_FRAGMENT_LOG("Find shortest combination based on prefix and suffix.");
  TEXT_FRAGMENT_LOG("Matches to eliminate: {}, Total combinations: {}",
                    mCommonSubstringLengths.Length(),
                    prefixLengths.Length() * suffixLengths.Length());
  TEXT_FRAGMENT_LOG("Checking prefix lengths (extended to word boundaries): {}",
                    prefixLengths);
  TEXT_FRAGMENT_LOG("Checking suffix lengths (extended to word boundaries): {}",
                    suffixLengths);
  TEXT_FRAGMENT_LOG("Matches: {}", mCommonSubstringLengths);
  return CheckAllCombinations(mCommonSubstringLengths, prefixLengths,
                              suffixLengths)
      .andThen([&](std::tuple<uint32_t, uint32_t> bestMatch) {
        const auto [prefixLength, suffixLength] = bestMatch;
        TextDirective td;
        if (prefixLength) {
          td.prefix =
              Substring(mPrefixContent, mPrefixContent.Length() - prefixLength);
        }
        td.start = mStartContent;
        if (suffixLength) {
          td.suffix = Substring(mSuffixContent, 0, suffixLength);
        }
        return Some(td);
      });
}

Maybe<TextDirective> RangeBasedTextDirectiveCreator::FindShortestCombination()
    const {
  // For this algorithm, ignore the first word of the start term  and the last
  // word of the end term (which are required). This allows the optimization
  // algorithm to minimize to 0.
  auto [prefixLengths, startLengths] = ExtendSubstringLengthsToWordBoundaries(
      mStartMatchCommonSubstringLengths, mPrefixWordBeginDistances,
      mStartWordEndDistances);

  TEXT_FRAGMENT_LOG(
      "Find shortest combination for start match based on prefix and start");
  TEXT_FRAGMENT_LOG("Matches to eliminate: {}, Total combinations: {}",
                    mStartMatchCommonSubstringLengths.Length(),
                    prefixLengths.Length() * startLengths.Length());
  TEXT_FRAGMENT_LOG("Checking prefix lengths (extended to word boundaries): {}",
                    prefixLengths);
  TEXT_FRAGMENT_LOG("Checking start lengths (extended to word boundaries): {}",
                    startLengths);
  TEXT_FRAGMENT_LOG("Matches: {}", mStartMatchCommonSubstringLengths);

  const auto bestStartMatch = CheckAllCombinations(
      mStartMatchCommonSubstringLengths, prefixLengths, startLengths);
  if (MOZ_UNLIKELY(bestStartMatch.isNothing())) {
    TEXT_FRAGMENT_LOG(
        "Could not find unique start match. It's not possible to create a text "
        "directive for the target range.");
    return Nothing{};
  }
  auto [endLengths, suffixLengths] = ExtendSubstringLengthsToWordBoundaries(
      mEndMatchCommonSubstringLengths, mEndWordBeginDistances,
      mSuffixWordEndDistances);

  TEXT_FRAGMENT_LOG(
      "Find shortest combination for end match based on end and suffix");
  TEXT_FRAGMENT_LOG("Matches to eliminate: {}, Total combinations: {}",
                    mEndMatchCommonSubstringLengths.Length(),
                    endLengths.Length() * suffixLengths.Length());
  TEXT_FRAGMENT_LOG("Checking end lengths (extended to word boundaries): {}",
                    endLengths);
  TEXT_FRAGMENT_LOG("Checking suffix lengths (extended to word boundaries): {}",
                    suffixLengths);
  TEXT_FRAGMENT_LOG("Matches: {}", mEndMatchCommonSubstringLengths);
  const auto bestEndMatch = CheckAllCombinations(
      mEndMatchCommonSubstringLengths, endLengths, suffixLengths);
  if (MOZ_UNLIKELY(bestEndMatch.isNothing())) {
    TEXT_FRAGMENT_LOG(
        "Could not find unique end match. It's not possible to create a text "
        "directive for the target range.");
    return Nothing{};
  }
  const auto [prefixLength, startLength] = *bestStartMatch;
  const auto [endLength, suffixLength] = *bestEndMatch;
  TextDirective td;
  if (prefixLength) {
    td.prefix =
        Substring(mPrefixContent, mPrefixContent.Length() - prefixLength);
  }

  if (startLength) {
    const uint32_t startLengthIncludingFirstWord =
        mStartFirstWordLengthIncludingWhitespace + startLength;
    MOZ_DIAGNOSTIC_ASSERT(startLengthIncludingFirstWord <=
                          mStartContent.Length());
    td.start = Substring(mStartContent, 0, startLengthIncludingFirstWord);
  } else {
    td.start = mFirstWordOfStartContent;
  }
  if (endLength) {
    const uint32_t endLengthIncludingLastWord =
        mEndLastWordLengthIncludingWhitespace + endLength;

    MOZ_DIAGNOSTIC_ASSERT(endLengthIncludingLastWord <= mEndContent.Length());
    td.end = Substring(mEndContent,
                       mEndContent.Length() - endLengthIncludingLastWord);
  } else {
    td.end = mLastWordOfEndContent;
  }

  if (suffixLength) {
    td.suffix = Substring(mSuffixContent, 0, suffixLength);
  }

  return Some(td);
}

}  // namespace mozilla::dom
