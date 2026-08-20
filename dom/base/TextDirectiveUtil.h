/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_TEXTDIRECTIVEUTIL_H_
#define DOM_TEXTDIRECTIVEUTIL_H_

#include "mozilla/Logging.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/AbstractRange.h"
#include "mozilla/dom/Text.h"
#include "mozilla/intl/WordBreaker.h"
#include "nsStringFwd.h"

class nsIURI;
class nsINode;
class nsFind;
class nsRange;
struct TextDirective;

namespace mozilla::dom {

extern LazyLogModule gFragmentDirectiveLog;
#define TEXT_FRAGMENT_LOG_FN(msg, func, ...)                              \
  MOZ_LOG_FMT(gFragmentDirectiveLog, LogLevel::Debug, "{}(): " msg, func, \
              ##__VA_ARGS__)

// Shortcut macro for logging, which includes the current function name.
// To customize (eg. if in a lambda), use `TEXT_FRAGMENT_LOG_FN`.
#define TEXT_FRAGMENT_LOG(msg, ...) \
  TEXT_FRAGMENT_LOG_FN(msg, __FUNCTION__, ##__VA_ARGS__)

enum class TextScanDirection { Left = -1, Right = 1 };

class TextDirectiveUtil final {
 public:
  MOZ_ALWAYS_INLINE static bool ShouldLog() {
    return MOZ_LOG_TEST(gFragmentDirectiveLog, LogLevel::Debug);
  }

  static Result<nsString, ErrorResult> RangeContentAsString(
      AbstractRange* aRange);

  /**
   * @brief Return true if `aNode` is a visible Text node.
   *
   * A node is a visible text node if it is a Text node, the computed value of
   * its parent element's visibility property is visible, and it is being
   * rendered.
   *
   * see https://wicg.github.io/scroll-to-text-fragment/#visible-text-node
   */
  static bool NodeIsVisibleTextNode(const nsINode& aNode);

  /**
   * @brief Finds the search query in the given search range.
   *
   * This function parametrizes the `nsFind` instance.
   */
  static RefPtr<nsRange> FindStringInRange(nsFind* aFinder,
                                           const RangeBoundary& aSearchStart,
                                           const RangeBoundary& aSearchEnd,
                                           const nsAString& aQuery,
                                           bool aWordStartBounded,
                                           bool aWordEndBounded);

  /**
   * @brief Tests if there is whitespace at the given position.
   *
   * This algorithm tests for whitespaces and `&nbsp;` at `aPos`.
   * It returns true if whitespace was found.
   *
   * This function assumes the reading direction is "right". If trying to check
   * for whitespace to the left, the caller must adjust the offset.
   *
   */
  static bool IsWhitespaceAtPosition(const Text* aText, uint32_t aPos);

  /**
   * @brief Determine if `aNode` should be considered when traversing the DOM.
   *
   * A node is "search invisible" if it is an element in the HTML namespace and
   *  1. The computed value of its `display` property is `none`
   *  2. It serializes as void
   *  3. It is one of the following types:
   *    - HTMLIFrameElement
   *    - HTMLImageElement
   *    - HTMLMeterElement
   *    - HTMLObjectElement
   *    - HTMLProgressElement
   *    - HTMLStyleElement
   *    - HTMLScriptElement
   *    - HTMLVideoElement
   *    - HTMLAudioElement
   *  4. It is a `select` element whose `multiple` content attribute is absent
   *
   * see https://wicg.github.io/scroll-to-text-fragment/#search-invisible
   */
  static bool NodeIsSearchInvisible(nsINode& aNode);

  /**
   * @brief Returns true if `aNode` has block-level display.
   * A node has block-level display if it is an element and the computed value
   * of its display property is any of
   *  - block
   *  - table
   *  - flow-root
   *  - grid
   *  - flex
   *  - list-item
   *
   * See https://wicg.github.io/scroll-to-text-fragment/#has-block-level-display
   */
  static bool NodeHasBlockLevelDisplay(nsINode& aNode);
  /**
   * @brief Get the Block Ancestor For `aNode`.
   *
   * see https://wicg.github.io/scroll-to-text-fragment/#nearest-block-ancestor
   */
  static nsINode* GetBlockAncestorForNode(nsINode* aNode);

  /**
   * @brief Returns true if `aNode` is part of a non-searchable subtree.
   *
   * A node is part of a non-searchable subtree if it is or has a
   * shadow-including ancestor that is search invisible.
   *
   * see https://wicg.github.io/scroll-to-text-fragment/#non-searchable-subtree
   */
  static bool NodeIsPartOfNonSearchableSubTree(nsINode& aNode);

  /** Advances the start of `aRange` to the next non-whitespace position.
   * The function follows this section of the spec:
   * https://wicg.github.io/scroll-to-text-fragment/#next-non-whitespace-position
   *
   * Returns true if the range was successfully advanced to a non-whitespace
   * position in a text node, false otherwise (that indicates end of document).
   */
  static bool AdvanceStartToNextNonWhitespacePosition(nsRange& aRange);

  /**
   * @brief Returns a point moved by one character or unicode surrogate pair.
   */
  static RangeBoundary MoveToNextBoundaryPoint(const RangeBoundary& aPoint);

  template <TextScanDirection direction>
  static RangeBoundary FindNextBlockBoundary(
      const RangeBoundary& aRangeBoundary);

  template <TextScanDirection direction>
  static Maybe<RangeBoundary> FindBlockBoundaryInRange(
      const AbstractRange& aRange);

  /**
   * @brief Find the next non-whitespace point in given `direction`.
   *
   * This algorithm jumps across block boundaries.
   *
   * @param aPoint Start point
   * @return New boundary point which points at the next non-whitespace text in
   *         `direction`. If no non-whitespace content exists in `direction`,
   *         return the original boundary point.
   */
  template <TextScanDirection direction>
  static RangeBoundary FindNextNonWhitespacePosition(
      const RangeBoundary& aPoint);

  enum class BreakOnPunctuation : bool { No, Yes };
  /**
   * @brief Creates a new RangeBoundary at the nearest word boundary.
   *
   * Word boundaries are determined using `intl::WordBreaker::FindWord()`.
   * This algorithm can find word boundaries across node boundaries and stops at
   * a block boundary.
   *
   * @param aRangeBoundary[in]  The range boundary that should be moved.
   *                            Must be set and valid.
   * @param direction[in]       The direction into which to move.
   * @param aBreakOnPunctuation Whether to count punctuation as separate words.
   * @return A new `RangeBoundary` which is moved to the nearest word boundary.
   */
  template <TextScanDirection direction>
  static RangeBoundary FindWordBoundary(const RangeBoundary& aRangeBoundary,
                                        BreakOnPunctuation aBreakOnPunctuation);

  /**
   * @brief Compares the common substring between a reference string and a text
   *        node in the given direction.
   *
   * This algorithm returns the common substring across same-block visible text
   * nodes, starting at `aBoundaryPoint`. Whitespace is compressed.
   */
  template <TextScanDirection direction>
  static uint32_t ComputeCommonSubstringLength(
      const nsAString& aReferenceString, const RangeBoundary& aBoundaryPoint);

  /**
   * @brief Creates a list of all word boundary distances to the base of the
   *        string (beginning for left-to-right, end for right-to-left).
   *
   * Word boundaries are determined by iterating the string and checking for
   * word boundaries using the `intl::WordBreaker` algorithm.
   *
   * If direction is `Left`, word begin positions are used, and the distances
   * are based off the end of the string. Otherwise, the word end positions are
   * used, and the distances are based off the begin of the string.
   * The returned array is always sorted and contains monotonically increasing
   * values.
   *
   * This function is guaranteed to return at least one word boundary distance,
   * the last element always being the length of the string.
   */
  template <TextScanDirection direction>
  static nsTArray<uint32_t> ComputeWordBoundaryDistances(
      const nsAString& aString);

  /**
   * @brief Returns true if the word between `aWordBegin` and `aWordEnd` is
   *        just whitespace or punctuation.
   * @param aString The string to check. Must not be empty.
   * @param aWordBegin The start index of the word.
   * @param aWordEnd The end index of the word.
   * @return true if the word is just whitespace or punctuation, false
   * otherwise.
   */
  static bool WordIsJustWhitespaceOrPunctuation(const nsAString& aString,
                                                uint32_t aWordBegin,
                                                uint32_t aWordEnd);

  /**
   * @brief Returns true if `aString` contains at least two words.
   *
   * Words which are just whitespace or punctuation are not counted, following
   * the same definition of a word as the word boundary algorithms in this
   * class.
   */
  static bool ContainsAtLeastTwoWords(const nsAString& aString);

  /**
   * @brief Finds the position of the beginning of the second word (in
   *        `direction`), then removes everything up to that position from
   *       `aString` and `aWordDistances`.
   *
   * This function modifies both `aString` and `aWordDistances`.
   * It expects `aString` to be non-empty, and to contain at least two words,
   * as indicated by `aWordDistances` containing at least two elements.
   *
   * @tparam direction Either left-to-right or right-to-left.
   * @param aString        The string to modify. Must not be empty.
   * @param aWordDistances The array of word boundary distances. The distances
   *                       are always sorted and contain monotonically
   *                       increasing values. For LTR, the distances are based
   *                       off the beginning of the string. For RTL, the
   *                       distances are based off the end of the string. Must
   *                       contain at least two elements.
   * @return The length of the first word including whitespace and
   *         punctuation up to the beginning of the second word.
   */
  template <TextScanDirection direction>
  static uint32_t RemoveFirstWordFromStringAndDistanceArray(
      nsAString& aString, nsTArray<uint32_t>& aWordDistances);
};

class TimeoutWatchdog final {
 public:
  NS_INLINE_DECL_REFCOUNTING(TimeoutWatchdog);
  TimeoutWatchdog()
      : mStartTime(TimeStamp::Now()),
        mDuration(TimeDuration::FromSeconds(
            StaticPrefs::
                dom_text_fragments_create_text_fragment_timeout_seconds())) {}
  bool IsDone() const { return TimeStamp::Now() - mStartTime > mDuration; }

 private:
  ~TimeoutWatchdog() = default;
  TimeStamp mStartTime;
  TimeDuration mDuration;
};

/**
 * @brief Iterator for visible text nodes with the same block ancestor.
 *
 * Allows to be used in range-based iteration. Returns the next visible text
 * node (as defined by `TextDirectiveUtil::NodeIsVisibleTextNode()` and
 * `TextDirectiveUtil::NodeIsPartOfNonSearchableSubTree()`) in the given
 * direction.
 *
 * @tparam direction Either left-to-right or right-to-left.
 */
template <TextScanDirection direction>
class SameBlockVisibleTextNodeIterator final {
 public:
  explicit SameBlockVisibleTextNodeIterator(nsINode& aStart)
      : mCurrent(&aStart),
        mBlockAncestor(TextDirectiveUtil::GetBlockAncestorForNode(mCurrent)) {
    while (mCurrent->HasChildNodes()) {
      nsINode* child = direction == TextScanDirection::Left
                           ? mCurrent->GetLastChild()
                           : mCurrent->GetFirstChild();
      if (TextDirectiveUtil::GetBlockAncestorForNode(child) != mBlockAncestor) {
        break;
      }
      mCurrent = child;
    }
  }

  SameBlockVisibleTextNodeIterator& begin() { return *this; }

  std::nullptr_t end() { return nullptr; }

  bool operator!=(std::nullptr_t) const { return !!mCurrent; }

  void operator++() {
    while (mCurrent) {
      mCurrent = direction == TextScanDirection::Left ? mCurrent->GetPrevNode()
                                                      : mCurrent->GetNextNode();
      if (!mCurrent) {
        return;
      }
      if (TextDirectiveUtil::GetBlockAncestorForNode(mCurrent) !=
          mBlockAncestor) {
        mCurrent = nullptr;
        return;
      }
      if (TextDirectiveUtil::NodeIsVisibleTextNode(*mCurrent) &&
          !TextDirectiveUtil::NodeIsPartOfNonSearchableSubTree(*mCurrent)) {
        break;
      }
    }
    MOZ_ASSERT_IF(mCurrent, mCurrent->IsText());
  }

  Text* operator*() { return Text::FromNodeOrNull(mCurrent); }

 private:
  nsINode* mCurrent = nullptr;
  nsINode* mBlockAncestor = nullptr;
};

template <TextScanDirection direction>
/*static*/ RangeBoundary TextDirectiveUtil::FindNextBlockBoundary(
    const RangeBoundary& aRangeBoundary) {
  MOZ_ASSERT(aRangeBoundary.IsSetAndValid());
  nsINode* current = aRangeBoundary.GetContainer();
  uint32_t offset =
      direction == TextScanDirection::Left ? 0u : current->Length();
  for (auto* node : SameBlockVisibleTextNodeIterator<direction>(*current)) {
    if (!node) {
      continue;
    }
    current = node;
    offset = direction == TextScanDirection::Left ? 0u : current->Length();
  }
  return {current, offset};
}

template <TextScanDirection direction>
/* static */ Maybe<RangeBoundary> TextDirectiveUtil::FindBlockBoundaryInRange(
    const AbstractRange& aRange) {
  if (aRange.Collapsed()) {
    return Nothing{};
  }

  RangeBoundary boundary = FindNextBlockBoundary<direction>(
      direction == TextScanDirection::Left ? aRange.EndRef()
                                           : aRange.StartRef());

  Maybe<int32_t> compare =
      direction == TextScanDirection::Left
          ? nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                aRange.StartRef(), boundary)
          : nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                boundary, aRange.EndRef());
  if (compare && *compare == -1) {
    // *compare == -1 means that the found boundary is after the range start
    // when looking left, and before the range end when looking right.
    // This means that there is a block boundary within the range.
    return Some(boundary);
  }

  return Nothing{};
}

template <TextScanDirection direction>
/* static */ RangeBoundary TextDirectiveUtil::FindNextNonWhitespacePosition(
    const RangeBoundary& aPoint) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  nsINode* node = aPoint.GetChildAtOffset();
  uint32_t offset =
      direction == TextScanDirection::Left && node ? node->Length() : 0;
  if (!node) {
    node = aPoint.GetContainer();
    offset =
        *aPoint.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets);
  }
  while (node->HasChildNodes()) {
    if constexpr (direction == TextScanDirection::Left) {
      node = node->GetLastChild();
      MOZ_ASSERT(node);
      offset = node->Length();
    } else {
      node = node->GetFirstChild();
      offset = 0;
    }
  }

  while (node) {
    const bool nodeIsInvisible =
        !TextDirectiveUtil::NodeIsVisibleTextNode(*node) ||
        TextDirectiveUtil::NodeIsPartOfNonSearchableSubTree(*node);
    const bool offsetIsAtEnd =
        (direction == TextScanDirection::Left && offset == 0) ||
        (direction == TextScanDirection::Right && offset == node->Length());
    if (nodeIsInvisible || offsetIsAtEnd) {
      if constexpr (direction == TextScanDirection::Left) {
        node = node->GetPrevNode();
        if (node) {
          offset = node->Length();
        }
      } else {
        node = node->GetNextNode();
        offset = 0;
      }
      continue;
    }
    const Text* text = Text::FromNode(node);
    MOZ_ASSERT(text);

    if (!TextDirectiveUtil::IsWhitespaceAtPosition(
            text, direction == TextScanDirection::Left ? offset - 1 : offset)) {
      return {node, offset};
    }
    offset += int(direction);
  }

  // If there seems to be no non-whitespace text in the document in
  // `direction`, it's safest to return the original point.
  return aPoint;
}

template <TextScanDirection direction>
/*static*/ RangeBoundary TextDirectiveUtil::FindWordBoundary(
    const RangeBoundary& aRangeBoundary,
    BreakOnPunctuation aBreakOnPunctuation) {
  MOZ_ASSERT(aRangeBoundary.IsSetAndValid());
  nsINode* node = aRangeBoundary.GetContainer();
  uint32_t offset = *aRangeBoundary.Offset(
      RangeBoundary::OffsetFilter::kValidOrInvalidOffsets);

  // Collect text content into this buffer.
  // The following algorithm pulls in the next text node if required
  // (if the next word boundary would be at the beginning/end of the text node)
  nsString textBuffer;
  for (Text* textNode : SameBlockVisibleTextNodeIterator<direction>(*node)) {
    if (!textNode || textNode->Length() == 0) {
      continue;
    }
    nsString data;
    textNode->GetWholeText(data);
    const uint32_t bufferLength = textBuffer.Length();
    if constexpr (direction == TextScanDirection::Left) {
      textBuffer.Insert(data, 0);
    } else {
      textBuffer.Append(data);
    }
    if (bufferLength) {
      auto newOffset =
          direction == TextScanDirection::Left ? textNode->Length() - 1 : 0u;
      if (nsContentUtils::IsHTMLWhitespace(data.CharAt(newOffset)) ||
          mozilla::IsPunctuationForWordSelect(data.CharAt(newOffset))) {
        break;
      }
      offset = newOffset;
    } else {
      offset = std::max(std::min(offset, textNode->Length() - 1), 0u);
    }
    if constexpr (direction == TextScanDirection::Right) {
      // if not at the beginning of a word, go left by one character.
      // Otherwise, if offset is already at the end of the word, the word
      // breaker will match the whitespace or the next word.
      if (offset &&
          !(nsContentUtils::IsHTMLWhitespace(data.CharAt(offset - 1)) ||
            mozilla::IsPunctuationForWordSelect(data.CharAt(offset - 1)))) {
        --offset;
      }
    } else {
      if (offset &&
          (nsContentUtils::IsHTMLWhitespace(data.CharAt(offset)) ||
           mozilla::IsPunctuationForWordSelect(data.CharAt(offset)))) {
        --offset;
      }
    }
    uint32_t pos =
        direction == TextScanDirection::Left ? offset : bufferLength + offset;
    while (true) {
      const auto [wordStart, wordEnd] =
          intl::WordBreaker::FindWord(textBuffer, pos);
      offset = direction == TextScanDirection::Left ? wordStart
                                                    : wordEnd - bufferLength;
      node = textNode;
      if (offset == 0 || offset >= textNode->Length()) {
        // need to include more text nodes to be sure of word boundary
        break;
      }
      if (aBreakOnPunctuation == BreakOnPunctuation::Yes ||
          !WordIsJustWhitespaceOrPunctuation(textBuffer, wordStart, wordEnd)) {
        return {node, offset};
      }
      // Word is just punctuation - continue to next word break
      if constexpr (direction == TextScanDirection::Left) {
        if (wordStart == 0) {
          break;
        }
        pos = wordStart - 1;
      } else {
        if (wordEnd == textBuffer.Length()) {
          break;
        }
        pos = wordEnd;
      }
    }
  }
  return {node, offset};
}

template <TextScanDirection direction>
void LogCommonSubstringLengths(const char* aFunc,
                               const nsAString& aReferenceString,
                               const nsTArray<nsString>& aTextContentPieces,
                               uint32_t aCommonLength) {
  if (!TextDirectiveUtil::ShouldLog()) {
    return;
  }
  nsString concatenatedTextContents;
  for (const auto& textContent : aTextContentPieces) {
    concatenatedTextContents.Append(textContent);
  }
  // the algorithm expects `aReferenceString` to be whitespace-compressed,
  // and ignores leading whitespace when looking at the DOM nodes. So,
  // whitespace needs to be compressed here as well.
  concatenatedTextContents.CompressWhitespace();
  const uint32_t maxLength =
      std::max(aReferenceString.Length(), concatenatedTextContents.Length());
  TEXT_FRAGMENT_LOG_FN("Direction: {}.", aFunc,
                       direction == TextScanDirection::Left ? "left" : "right");

  if constexpr (direction == TextScanDirection::Left) {
    TEXT_FRAGMENT_LOG_FN("Ref:    {:>{}}", aFunc,
                         NS_ConvertUTF16toUTF8(aReferenceString), maxLength);
    TEXT_FRAGMENT_LOG_FN("Other:  {:>{}}", aFunc,
                         NS_ConvertUTF16toUTF8(concatenatedTextContents),
                         maxLength);
    TEXT_FRAGMENT_LOG_FN(
        "Common: {:>{}} ({} chars)", aFunc,
        NS_ConvertUTF16toUTF8(Substring(aReferenceString, aCommonLength)),
        maxLength, aCommonLength);
  } else {
    TEXT_FRAGMENT_LOG_FN("Ref:    {:<{}}", aFunc,
                         NS_ConvertUTF16toUTF8(aReferenceString), maxLength);
    TEXT_FRAGMENT_LOG_FN("Other:  {:<{}}", aFunc,
                         NS_ConvertUTF16toUTF8(concatenatedTextContents),
                         maxLength);
    TEXT_FRAGMENT_LOG_FN(
        "Common: {:<{}} ({} chars)", aFunc,
        NS_ConvertUTF16toUTF8(Substring(aReferenceString, 0, aCommonLength)),
        maxLength, aCommonLength);
  }
}

template <TextScanDirection direction>
/*static*/ nsTArray<uint32_t> TextDirectiveUtil::ComputeWordBoundaryDistances(
    const nsAString& aString) {
  AutoTArray<uint32_t, 32> wordBoundaryDistances;
  uint32_t pos =
      direction == TextScanDirection::Left ? aString.Length() - 1 : 0;

  // This loop relies on underflowing `pos` when going left as stop condition.
  while (pos < aString.Length()) {
    auto [wordBegin, wordEnd] = intl::WordBreaker::FindWord(aString, pos);
    pos = direction == TextScanDirection::Left ? wordBegin - 1 : wordEnd + 1;
    if (WordIsJustWhitespaceOrPunctuation(aString, wordBegin, wordEnd)) {
      // The WordBreaker algorithm breaks at punctuation, so that "foo bar. baz"
      // would be split into four words: [foo, bar, ., baz].
      // To avoid this, we skip words which are just whitespace or punctuation
      // and add the punctuation to the previous word, so that the above example
      // would yield three words: [foo, bar., baz].
      continue;
    }

    wordBoundaryDistances.AppendElement(direction == TextScanDirection::Left
                                            ? aString.Length() - wordBegin
                                            : wordEnd);
  }
  if (wordBoundaryDistances.IsEmpty() ||
      wordBoundaryDistances.LastElement() != aString.Length()) {
    wordBoundaryDistances.AppendElement(aString.Length());
  }
  return std::move(wordBoundaryDistances);
}

template <TextScanDirection direction>
/*static*/ uint32_t TextDirectiveUtil::ComputeCommonSubstringLength(
    const nsAString& aReferenceString, const RangeBoundary& aBoundaryPoint) {
  MOZ_ASSERT(aBoundaryPoint.IsSetAndValid());
  if (aReferenceString.IsEmpty()) {
    TEXT_FRAGMENT_LOG("Reference string is empty.");
    return 0;
  }

  MOZ_ASSERT(!nsContentUtils::IsHTMLWhitespace(aReferenceString.First()));
  MOZ_ASSERT(!nsContentUtils::IsHTMLWhitespace(aReferenceString.Last()));
  uint32_t referenceStringPosition =
      direction == TextScanDirection::Left ? aReferenceString.Length() - 1 : 0;

  bool foundMismatch = false;

  // `aReferenceString` is expected to have its whitespace compressed.
  // The raw text from the DOM nodes does not have compressed whitespace.
  // Therefore, the algorithm needs to skip multiple whitespace characters.
  // Setting this flag to true initially makes this algorithm tolerant to
  // preceding whitespace in the DOM nodes and the reference string.
  bool isInWhitespace = true;
  nsTArray<nsString> textContentForLogging;
  for (Text* text : SameBlockVisibleTextNodeIterator<direction>(
           *aBoundaryPoint.GetContainer())) {
    if (!text || text->Length() == 0) {
      continue;
    }
    uint32_t offset =
        direction == TextScanDirection::Left ? text->Length() - 1 : 0;
    if (text == aBoundaryPoint.GetContainer()) {
      offset = *aBoundaryPoint.Offset(
          RangeBoundary::OffsetFilter::kValidOrInvalidOffsets);
      if (offset && direction == TextScanDirection::Left) {
        // when looking left, the offset is _behind_ the actual char.
        // Therefore, the value is decremented, and incremented when returning.
        --offset;
      }
    }
    if (TextDirectiveUtil::ShouldLog()) {
      nsString textContent;
      text->GetWholeText(textContent);
      if constexpr (direction == TextScanDirection::Left) {
        if (offset) {
          textContent = Substring(textContent, 0, offset + 1);
        } else {
          textContent.Truncate();
        }
      } else {
        textContent = Substring(textContent, offset);
      }
      textContentForLogging.AppendElement(std::move(textContent));
    }
    const CharacterDataBuffer* characterDataBuffer =
        text->GetCharacterDataBuffer();
    MOZ_DIAGNOSTIC_ASSERT(characterDataBuffer);
    const uint32_t textLength = characterDataBuffer->GetLength();
    while (offset < textLength &&
           referenceStringPosition < aReferenceString.Length()) {
      char16_t ch = characterDataBuffer->CharAt(offset);
      char16_t refCh = aReferenceString.CharAt(referenceStringPosition);
      const bool chIsWhitespace = nsContentUtils::IsHTMLWhitespace(ch);
      const bool refChIsWhitespace = nsContentUtils::IsHTMLWhitespace(refCh);
      if (chIsWhitespace) {
        if (refChIsWhitespace) {
          offset += int(direction);
          referenceStringPosition += int(direction);
          isInWhitespace = true;
          continue;
        }
        if (isInWhitespace) {
          offset += int(direction);
          continue;
        }
      }
      isInWhitespace = false;
      if (refCh == ToFoldedCase(ch)) {
        offset += int(direction);
        referenceStringPosition += int(direction);
        continue;
      }
      foundMismatch = true;
      break;
    }
    if (foundMismatch) {
      break;
    }
  }
  uint32_t commonLength = 0;
  if constexpr (direction == TextScanDirection::Left) {
    ++referenceStringPosition;
    commonLength = aReferenceString.Length() - referenceStringPosition;
    if (TextDirectiveUtil::ShouldLog()) {
      textContentForLogging.Reverse();
    }
  } else {
    commonLength = referenceStringPosition;
  }
  LogCommonSubstringLengths<direction>(__FUNCTION__, aReferenceString,
                                       textContentForLogging, commonLength);
  return commonLength;
}

template <TextScanDirection direction>
/*static*/ uint32_t
TextDirectiveUtil::RemoveFirstWordFromStringAndDistanceArray(
    nsAString& aString, nsTArray<uint32_t>& aWordDistances) {
  MOZ_DIAGNOSTIC_ASSERT(!aString.IsEmpty());
  MOZ_DIAGNOSTIC_ASSERT(aWordDistances.Length() > 1);
  auto lengthOfFirstWordPlusWhitespaceAndPunctuation = aWordDistances[0];
  auto chIsWhitespaceOrPunctuation = [&](uint32_t distance) {
    const char16_t ch = aString.CharAt(direction == TextScanDirection::Right
                                           ? distance
                                           : aString.Length() - distance - 1);
    return nsContentUtils::IsHTMLWhitespace(ch) ||
           mozilla::IsPunctuationForWordSelect(ch);
  };
  while (lengthOfFirstWordPlusWhitespaceAndPunctuation < aString.Length() &&
         chIsWhitespaceOrPunctuation(
             lengthOfFirstWordPlusWhitespaceAndPunctuation)) {
    ++lengthOfFirstWordPlusWhitespaceAndPunctuation;
  }
  if (lengthOfFirstWordPlusWhitespaceAndPunctuation == aString.Length()) {
    // In this case the string only contains whitespace or punctuation after the
    // first word.
    aWordDistances.Clear();
    return lengthOfFirstWordPlusWhitespaceAndPunctuation;
  }
  // Adjust all distances to be relative to the new start position.
  // In the case that the loop above jumps over punctuation which is actually
  // considered to be a word, the distance underflows (or becomes zero).
  // These obsolete distances are then removed.
  for (auto& wordDistance : aWordDistances) {
    wordDistance -= lengthOfFirstWordPlusWhitespaceAndPunctuation;
  }
  aWordDistances.RemoveElementsBy([&aString](uint32_t distance) {
    return distance == 0 || distance > aString.Length();
  });
  if constexpr (direction == TextScanDirection::Right) {
    aString = Substring(aString, lengthOfFirstWordPlusWhitespaceAndPunctuation);
  } else {
    aString = Substring(
        aString, 0,
        aString.Length() - lengthOfFirstWordPlusWhitespaceAndPunctuation);
  }
  return lengthOfFirstWordPlusWhitespaceAndPunctuation;
}
}  // namespace mozilla::dom

#endif
