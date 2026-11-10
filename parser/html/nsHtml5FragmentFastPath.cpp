/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5FragmentFastPath.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <type_traits>

#include "mozAutoDocUpdate.h"
#include "mozilla/Array.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/RandomNum.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextUtils.h"
#include "mozilla/Try.h"
#include "mozilla/Utf16.h"
#include "mozilla/Vector.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/htmlaccel/htmlaccelEnabled.h"
#ifdef MOZ_MAY_HAVE_HTMLACCEL
#  include "mozilla/htmlaccel/htmlaccelNotInline.h"
#endif
#include "nsContentUtils.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsHtml5AttributeName.h"
#include "nsHtml5ElementName.h"
#include "nsHtml5Portability.h"
#include "nsHtml5String.h"
#include "nsNodeInfoManager.h"
#include "nsString.h"
#include "nsTextNode.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {

// What an element may contain, chosen so that no supported nesting triggers
// an implied end tag, foster parenting or the adoption agency algorithm.
enum class ContentModel : uint8_t {
  Flow,
  Phrasing,
  // Only <li> elements, plus text.
  ListItems,
  // Whatever the parent may contain (for <a>).
  Inherit,
};

struct TagInfo {
  nsStaticAtom* mName;
  bool mVoid;
  // Whether the element may appear in phrasing content, and not only in flow
  // content.
  bool mPhrasing;
  ContentModel mChildren;
};

// The supported tags are the HTML elements for which the "in body" insertion
// mode does nothing but insert an element and pop it at its end tag, given the
// content models that the parser enforces. Their start tags are handled like
// this (https://html.spec.whatwg.org/#parsing-main-inbody):
//
//   A start tag whose tag name is one of: "address", "article", "aside",
//   "blockquote", "center", "details", "dialog", "dir", "div", "dl",
//   "fieldset", "figcaption", "figure", "footer", "header", "hgroup", "main",
//   "menu", "nav", "ol", "p", "search", "section", "summary", "ul"
//     If the stack of open elements has a p element in button scope, then
//     close a p element.
//     Insert an HTML element for the token.
//
//   A start tag whose tag name is one of: "h1", "h2", "h3", "h4", "h5", "h6"
//     If the stack of open elements has a p element in button scope, then
//     close a p element.
//     If the current node is an HTML element whose tag name is one of "h1",
//     "h2", "h3", "h4", "h5", or "h6", then this is a parse error; pop the
//     current node off the stack of open elements.
//     Insert an HTML element for the token.
//
// These tags aren't phrasing content, and <p> and the headings may only
// contain phrasing content, so neither a <p> nor a heading is ever open
// around them.
//
//   A start tag whose tag name is "li"
//     1. Set the frameset-ok flag to "not ok".
//     2. Initialize node to be the current node (the bottommost node of the
//        stack).
//     3. Loop: If node is an li element, then run these substeps:
//        1. Generate implied end tags, except for li elements.
//        2. If the current node is not an li element, then this is a parse
//           error.
//        3. Pop elements from the stack of open elements until an li element
//           has been popped from the stack.
//        4. Jump to the step labeled done below.
//     4. If node is in the special category, but is not an address, div, or p
//        element, then jump to the step labeled done below.
//     5. Otherwise, set node to the previous entry in the stack of open
//        elements and return to the step labeled loop.
//     6. Done: If the stack of open elements has a p element in button scope,
//        then close a p element.
//     7. Finally, insert an HTML element for the token.
//
// An <li> is only allowed outside of other <li> elements, or as a child of a
// <ul> or <ol>, which are special and stop the loop in step 4 before it reaches
// an outer <li>. So step 3 never finds an <li>, and step 6 never finds a <p>
// because <li> isn't phrasing content. The frameset-ok flag only matters
// before a <body> exists.
//
//   A start tag whose tag name is "a"
//     If the list of active formatting elements contains an a element between
//     the end of the list and the last marker on the list (or the start of the
//     list if there is no marker on the list), then this is a parse error; run
//     the adoption agency algorithm for the token, then remove that element
//     from the list of active formatting elements and the stack of open
//     elements if the adoption agency algorithm didn't already remove it (it
//     might not have if the element is not in table scope).
//     Reconstruct the active formatting elements, if any.
//     Insert an HTML element for the token. Push onto the list of active
//     formatting elements that element.
//
//   A start tag whose tag name is one of: "b", "big", "code", "em", "font",
//   "i", "s", "small", "strike", "strong", "tt", "u"
//     Reconstruct the active formatting elements, if any.
//     Insert an HTML element for the token. Push onto the list of active
//     formatting elements that element.
//
// The parser rejects an <a> inside another one. Every element on the list of
// active formatting elements is still open, because their end tags have to
// match (see ParseEndTag()), so reconstructing them does nothing. Pushing onto
// the list may drop an earlier entry (the "Noah's Ark" clause), which doesn't
// change the tree either.
//
//   A start tag whose tag name is one of: "area", "br", "embed", "img",
//   "keygen", "wbr"
//     Reconstruct the active formatting elements, if any.
//     Insert an HTML element for the token. Immediately pop the current node
//     off the stack of open elements.
//     Acknowledge the token's self-closing flag, if it is set.
//     Set the frameset-ok flag to "not ok".
//
//   A start tag whose tag name is "input"
//     If the parser's fragment context element is a select element (fragment
//     case):
//       1. Parse error.
//       2. Ignore the token.
//       3. Return.
//     If the stack of open elements has a select element in scope:
//       1. Parse error.
//       2. Pop elements from the stack of open elements until a select element
//          has been popped from the stack.
//     Reconstruct the active formatting elements, if any.
//     Insert an HTML element for the token. Immediately pop the current node
//     off the stack of open elements.
//     Acknowledge the token's self-closing flag, if it is set.
//     If the token does not have an attribute with the name "type", or if it
//     does, but that attribute's value is not an ASCII case-insensitive match
//     for "hidden", then set the frameset-ok flag to "not ok".
//
//   A start tag whose tag name is one of: "param", "source", "track"
//     Insert an HTML element for the token. Immediately pop the current node
//     off the stack of open elements.
//     Acknowledge the token's self-closing flag, if it is set.
//
//   A start tag whose tag name is "hr"
//     If the stack of open elements has a p element in button scope, then
//     close a p element.
//     If the stack of open elements has a select element in scope:
//       1. Generate implied end tags.
//       2. If the stack of open elements has an option element in scope or has
//          an optgroup element in scope, then this is a parse error.
//     Insert an HTML element for the token. Immediately pop the current node
//     off the stack of open elements.
//     Acknowledge the token's self-closing flag, if it is set.
//     Set the frameset-ok flag to "not ok".
//
// <select> is neither supported nor a supported context element, and <hr>
// isn't phrasing content, so it is never inside a <p>.
//
//   A start tag whose tag name is "button"
//     1. If the stack of open elements has a button element in scope, then
//        run these substeps:
//        1. Parse error.
//        2. Generate implied end tags.
//        3. Pop elements from the stack of open elements until a button
//           element has been popped from the stack.
//     2. Reconstruct the active formatting elements, if any.
//     3. Insert an HTML element for the token.
//     4. Set the frameset-ok flag to "not ok".
//
// The parser rejects a <button> inside another one. None of the elements that
// limit a scope is supported, so that is the same as having one in scope.
//
//   Any other start tag
//     Reconstruct the active formatting elements, if any.
//     Insert an HTML element for the token.
//
// Not supported, because the full parser does more for them:
// - <script>, <style>, <textarea>, <title>, <xmp>, <iframe>, <noembed>,
//   <noframes>, <noscript> and <plaintext> switch the tokenizer to another
//   state.
// - <pre> and <listing> drop a leading newline.
// - Table elements, <select>, <option>, <optgroup>, <template>, <form>,
//   <frameset>, <html>, <head>, <body>, <image>, <nobr>, <applet>, <marquee>
//   and <object> have their own tree construction rules.
// - <dd>, <dt>, <rb>, <rp>, <rt> and <rtc> generate implied end tags.
// - <svg> and <math> start foreign content.
// - <output> needs DoneAddingChildren().
// - Custom elements are created through their registry.
const TagInfo kTags[] = {
    {nsGkAtoms::a, false, true, ContentModel::Inherit},
    {nsGkAtoms::abbr, false, true, ContentModel::Phrasing},
    {nsGkAtoms::acronym, false, true, ContentModel::Phrasing},
    {nsGkAtoms::address, false, false, ContentModel::Flow},
    {nsGkAtoms::area, true, true, ContentModel::Phrasing},
    {nsGkAtoms::article, false, false, ContentModel::Flow},
    {nsGkAtoms::aside, false, false, ContentModel::Flow},
    {nsGkAtoms::audio, false, true, ContentModel::Phrasing},
    {nsGkAtoms::b, false, true, ContentModel::Phrasing},
    {nsGkAtoms::bdi, false, true, ContentModel::Phrasing},
    {nsGkAtoms::bdo, false, true, ContentModel::Phrasing},
    {nsGkAtoms::big, false, true, ContentModel::Phrasing},
    {nsGkAtoms::blockquote, false, false, ContentModel::Flow},
    {nsGkAtoms::br, true, true, ContentModel::Phrasing},
    {nsGkAtoms::button, false, true, ContentModel::Phrasing},
    {nsGkAtoms::canvas, false, true, ContentModel::Phrasing},
    {nsGkAtoms::center, false, false, ContentModel::Flow},
    {nsGkAtoms::cite, false, true, ContentModel::Phrasing},
    {nsGkAtoms::code, false, true, ContentModel::Phrasing},
    {nsGkAtoms::data, false, true, ContentModel::Phrasing},
    {nsGkAtoms::del, false, true, ContentModel::Phrasing},
    {nsGkAtoms::details, false, false, ContentModel::Flow},
    {nsGkAtoms::dfn, false, true, ContentModel::Phrasing},
    {nsGkAtoms::dialog, false, false, ContentModel::Flow},
    {nsGkAtoms::dir, false, false, ContentModel::Flow},
    {nsGkAtoms::div, false, false, ContentModel::Flow},
    {nsGkAtoms::dl, false, false, ContentModel::Flow},
    {nsGkAtoms::em, false, true, ContentModel::Phrasing},
    {nsGkAtoms::embed, true, true, ContentModel::Phrasing},
    {nsGkAtoms::fieldset, false, false, ContentModel::Flow},
    {nsGkAtoms::figcaption, false, false, ContentModel::Flow},
    {nsGkAtoms::figure, false, false, ContentModel::Flow},
    {nsGkAtoms::font, false, true, ContentModel::Phrasing},
    {nsGkAtoms::footer, false, false, ContentModel::Flow},
    {nsGkAtoms::h1, false, false, ContentModel::Phrasing},
    {nsGkAtoms::h2, false, false, ContentModel::Phrasing},
    {nsGkAtoms::h3, false, false, ContentModel::Phrasing},
    {nsGkAtoms::h4, false, false, ContentModel::Phrasing},
    {nsGkAtoms::h5, false, false, ContentModel::Phrasing},
    {nsGkAtoms::h6, false, false, ContentModel::Phrasing},
    {nsGkAtoms::header, false, false, ContentModel::Flow},
    {nsGkAtoms::hgroup, false, false, ContentModel::Flow},
    {nsGkAtoms::hr, true, false, ContentModel::Phrasing},
    {nsGkAtoms::i, false, true, ContentModel::Phrasing},
    {nsGkAtoms::img, true, true, ContentModel::Phrasing},
    {nsGkAtoms::input, true, true, ContentModel::Phrasing},
    {nsGkAtoms::ins, false, true, ContentModel::Phrasing},
    {nsGkAtoms::kbd, false, true, ContentModel::Phrasing},
    {nsGkAtoms::keygen, true, true, ContentModel::Phrasing},
    {nsGkAtoms::label, false, true, ContentModel::Phrasing},
    {nsGkAtoms::li, false, false, ContentModel::Flow},
    {nsGkAtoms::main, false, false, ContentModel::Flow},
    {nsGkAtoms::map, false, true, ContentModel::Phrasing},
    {nsGkAtoms::mark, false, true, ContentModel::Phrasing},
    {nsGkAtoms::menu, false, false, ContentModel::Flow},
    {nsGkAtoms::meter, false, true, ContentModel::Phrasing},
    {nsGkAtoms::nav, false, false, ContentModel::Flow},
    {nsGkAtoms::ol, false, false, ContentModel::ListItems},
    {nsGkAtoms::p, false, false, ContentModel::Phrasing},
    {nsGkAtoms::param, true, true, ContentModel::Phrasing},
    {nsGkAtoms::picture, false, true, ContentModel::Phrasing},
    {nsGkAtoms::progress, false, true, ContentModel::Phrasing},
    {nsGkAtoms::q, false, true, ContentModel::Phrasing},
    {nsGkAtoms::ruby, false, true, ContentModel::Phrasing},
    {nsGkAtoms::s, false, true, ContentModel::Phrasing},
    {nsGkAtoms::samp, false, true, ContentModel::Phrasing},
    {nsGkAtoms::search, false, false, ContentModel::Flow},
    {nsGkAtoms::section, false, false, ContentModel::Flow},
    {nsGkAtoms::slot, false, true, ContentModel::Phrasing},
    {nsGkAtoms::small, false, true, ContentModel::Phrasing},
    {nsGkAtoms::source, true, true, ContentModel::Phrasing},
    {nsGkAtoms::span, false, true, ContentModel::Phrasing},
    {nsGkAtoms::strike, false, true, ContentModel::Phrasing},
    {nsGkAtoms::strong, false, true, ContentModel::Phrasing},
    {nsGkAtoms::sub, false, true, ContentModel::Phrasing},
    {nsGkAtoms::summary, false, false, ContentModel::Flow},
    {nsGkAtoms::sup, false, true, ContentModel::Phrasing},
    {nsGkAtoms::time, false, true, ContentModel::Phrasing},
    {nsGkAtoms::track, true, true, ContentModel::Phrasing},
    {nsGkAtoms::tt, false, true, ContentModel::Phrasing},
    {nsGkAtoms::u, false, true, ContentModel::Phrasing},
    {nsGkAtoms::ul, false, false, ContentModel::ListItems},
    {nsGkAtoms::var, false, true, ContentModel::Phrasing},
    {nsGkAtoms::video, false, true, ContentModel::Phrasing},
    {nsGkAtoms::wbr, true, true, ContentModel::Phrasing},
};

constexpr size_t kTagCount = std::size(kTags);
constexpr uint8_t kNoTag = 0xFF;
constexpr size_t kMaxTagsPerLetter = 16;
constexpr uint32_t kMaxTagNameLength = 10;

struct TagTables {
  mozilla::Array<nsHtml5ElementName*, kTagCount> mElementNames;
  mozilla::Array<const char16_t*, kTagCount> mNames;
  mozilla::Array<uint8_t, kTagCount> mNameLengths;
  mozilla::Array<bool, kTagCount> mRequiresDoneCreatingElement;
  // Indices into kTags by the first letter of their name, ending with kNoTag.
  uint8_t mByFirstLetter[26][kMaxTagsPerLetter + 1];
};

const TagTables& Tables() {
  static const TagTables sTables = [] {
    TagTables tables;
    memset(tables.mByFirstLetter, kNoTag, sizeof(tables.mByFirstLetter));
    for (size_t i = 0; i < kTagCount; ++i) {
      nsStaticAtom* name = kTags[i].mName;
      tables.mNames[i] = name->GetUTF16String();
      tables.mNameLengths[i] = name->GetLength();
      MOZ_RELEASE_ASSERT(tables.mNameLengths[i] <= kMaxTagNameLength);
      tables.mRequiresDoneCreatingElement[i] =
          nsIContent::RequiresDoneCreatingElement(kNameSpaceID_XHTML, name);
      tables.mElementNames[i] = nsHtml5ElementName::elementNameByBuffer(
          const_cast<char16_t*>(tables.mNames[i]), tables.mNameLengths[i]);
      MOZ_RELEASE_ASSERT(tables.mElementNames[i]);
      // The fast path neither looks up custom element definitions nor calls
      // DoneAddingChildren().
      MOZ_RELEASE_ASSERT(!tables.mElementNames[i]->isCustom());
      MOZ_RELEASE_ASSERT(
          !nsIContent::RequiresDoneAddingChildren(kNameSpaceID_XHTML, name));
      uint8_t* bucket = tables.mByFirstLetter[tables.mNames[i][0] - 'a'];
      size_t slot = 0;
      while (bucket[slot] != kNoTag) {
        ++slot;
      }
      MOZ_RELEASE_ASSERT(slot < kMaxTagsPerLetter);
      bucket[slot] = i;
    }
    return tables;
  }();
  return sTables;
}

// Returns the index into kTags of a lowercase tag name, or kNoTag.
uint8_t LookupTagIndex(const TagTables& aTables, const char16_t* aName,
                       uint32_t aLength) {
  if (!aLength || !IsAsciiLowercaseAlpha(aName[0])) {
    return kNoTag;
  }
  for (const uint8_t index : aTables.mByFirstLetter[aName[0] - 'a']) {
    if (index == kNoTag) {
      break;
    }
    if (aTables.mNameLengths[index] == aLength &&
        !memcmp(aTables.mNames[index], aName, aLength * sizeof(char16_t))) {
      return index;
    }
  }
  return kNoTag;
}

// Bounds the recursion, and stays well below the depth from which the full
// parser stops nesting elements (see nodeFromStackWithBlinkCompat()).
constexpr uint32_t kMaxDepth = 128;
constexpr uint32_t kMaxAttributes = 16;
constexpr uint32_t kMaxAttributeNameLength = 64;
constexpr size_t kScanChunk = 256;
constexpr size_t kMaxPresizedLength = 16 * 1024;
// Most attribute values are shorter than this, and scanning them a character
// at a time is faster than starting a SIMD scan.
constexpr ptrdiff_t kScanAttributeValueAfter = 16;

char16_t ToLower(char16_t aChar) {
  return IsAsciiUppercaseAlpha(aChar) ? char16_t(aChar + ('a' - 'A')) : aChar;
}

struct Token {
  enum class Kind : uint8_t { StartTag, EndTag, Text, Comment };
  Kind mKind;
  // For start tags.
  uint8_t mTagIndex;
  uint8_t mAttributeCount;
  // Whether the data is in Parser::mDecodedData rather than in the source.
  bool mDecoded;
  // For text and comments, the offset of their data in the source. For start
  // tags, the index of their first attribute.
  uint32_t mOffset;
  uint32_t mLength;
};

// The name is atomized only once the input is known to be supported.
struct AttributeToken {
  // Null for names that nsHtml5AttributeName doesn't know.
  nsHtml5AttributeName* mKnownName;
  // For unknown names, where the name is in the source.
  uint32_t mNameOffset;
  uint8_t mNameLength;
  // Whether the tokenizer would represent the value as an atom.
  bool mUseAtom;
  // Whether the value is in Parser::mDecodedData rather than in the source.
  bool mDecoded;
  uint32_t mValueOffset;
  uint32_t mValueLength;
};

// Parses the supported subset in two passes: Tokenize() checks all of the
// input and records the nodes to create, which Build() then creates.
class Parser {
 public:
  Parser(const nsAString& aSource, nsNodeInfoManager* aNodeInfoManager)
      : mStart(aSource.BeginReading()),
        mPos(mStart),
        mEnd(aSource.EndReading()),
        mNodeInfoManager(aNodeInfoManager) {}

  // Returns false for unsupported input.
  bool Tokenize() {
    // Markup rarely has more than one token per 16 characters, or one attribute
    // per 32, so this usually saves growing them. Up to a limit, because input
    // rejected late would waste the memory.
    const size_t length = std::min(size_t(mEnd - mStart), kMaxPresizedLength);
    // Stopping before the end means an end tag without a matching start tag.
    return mTokens.reserve(length / 16) && mAttributes.reserve(length / 32) &&
           ParseChildren(ContentModel::Flow) && AtEnd();
  }

  // Appends the nodes to aTarget. Returns the error that stopped building, like
  // the full parser stops at the first tree operation that fails.
  //
  // To insert an HTML element for a token is to insert a foreign element for
  // it in the HTML namespace:
  //
  // https://html.spec.whatwg.org/#insert-a-foreign-element
  //   1. Let element be the result of creating an element for the token given
  //      token, namespace, and the target parent of the appropriate place for
  //      inserting a node.
  //   2. Insert an element at the adjusted insertion location with element.
  //   3. Push element onto the stack of open elements so that it is the new
  //      current node.
  //   4. Return element.
  //
  // https://html.spec.whatwg.org/#insert-a-character
  //   ...
  //   4. Let previousSibling be referenceChild's previous sibling if
  //      referenceChild is non-null; otherwise targetParent's last child.
  //   5. If previousSibling is a Text node, then append data to
  //      previousSibling's data.
  //      Otherwise, let text be the result of creating a text node given
  //      targetParent's node document and data, and insert text into
  //      targetParent before referenceChild.
  //
  // Without foster parenting or templates among the supported elements, the
  // adjusted insertion location is the end of the current node. A text token
  // holds a whole run of character tokens, and is never preceded by a text
  // node in the same parent, so it becomes one text node.
  nsresult Build(nsIContent* aTarget) {
    nsIContent* parents[kMaxDepth + 1];
    uint32_t depth = 0;
    parents[0] = aTarget;
    for (const Token& token : mTokens) {
      switch (token.mKind) {
        case Token::Kind::Text:
          MOZ_TRY(AppendText(parents[depth],
                             Chars(token.mDecoded, token.mOffset),
                             token.mLength));
          break;
        case Token::Kind::Comment:
          MOZ_TRY(AppendComment(parents[depth], mStart + token.mOffset,
                                token.mLength));
          break;
        case Token::Kind::EndTag:
          MOZ_ASSERT(depth);
          --depth;
          break;
        case Token::Kind::StartTag: {
          const TagInfo& tag = kTags[token.mTagIndex];
          RefPtr<Element> element = CreateElement(token);
          MOZ_TRY(Append(parents[depth], element));
          // https://html.spec.whatwg.org/#create-an-element-for-the-token
          //   20. If element is a resettable element and not a form-associated
          //       custom element, then invoke its reset algorithm. (This
          //       initializes the element's value and checkedness based on the
          //       element's attributes.)
          //
          // Gecko does this, and more, in DoneCreatingElement(), which the
          // full parser calls after inserting the element.
          if (mTables.mRequiresDoneCreatingElement[token.mTagIndex]) {
            element->DoneCreatingElement();
          }
          if (!tag.mVoid) {
            MOZ_ASSERT(depth < kMaxDepth);
            parents[++depth] = element;
          }
          break;
        }
      }
    }
    MOZ_ASSERT(!depth);
    return NS_OK;
  }

 private:
  bool AtEnd() const { return mPos == mEnd; }

  uint32_t Offset(const char16_t* aPos) const { return aPos - mStart; }

  const char16_t* Chars(bool aDecoded, uint32_t aOffset) const {
    return (aDecoded ? mDecodedData.BeginReading() : mStart) + aOffset;
  }

  void SkipWhitespace() {
    while (mPos != mEnd && IsAsciiWhitespace(*mPos)) {
      ++mPos;
    }
  }

  // Records the offsets of the "<", "&", carriage returns and U+0000 in the
  // kScanChunk characters from aFrom on, so that SkipText() doesn't need to
  // look at text.
  void Scan(const char16_t* aFrom) {
    const char16_t* end =
        size_t(mEnd - aFrom) > kScanChunk ? aFrom + kScanChunk : mEnd;
    uint32_t* out = mOffsets;
    const char16_t* pos = aFrom;
    while (pos != end) {
#ifdef MOZ_MAY_HAVE_HTMLACCEL
      if (mSimd && end - pos >= 16) {
        // A chunk has no more offsets than characters, so every stride fits.
        // Otherwise, this would loop forever at a stride that doesn't fit.
        static_assert(std::extent_v<decltype(mOffsets)> >= kScanChunk);
        pos = htmlaccel::CollectLessThanAndAmpersand(mStart, pos, end, out,
                                                     std::end(mOffsets));
        // It stops at a carriage return or U+0000 without recording it.
        if (pos == end || (*pos != '\r' && *pos)) {
          continue;
        }
      }
#endif
      if (*pos == '<' || *pos == '&' || *pos == '\r' || !*pos) {
        *out++ = Offset(pos);
      }
      ++pos;
    }
    mNextOffset = mOffsets;
    mOffsetsEnd = out;
    mScanned = end;
  }

  // Advances to the next "<", "&", carriage return or U+0000, skipping those
  // in tags and comments.
  void SkipText() {
    while (true) {
      for (; mNextOffset != mOffsetsEnd; ++mNextOffset) {
        if (mStart + *mNextOffset >= mPos) {
          mPos = mStart + *mNextOffset;
          return;
        }
      }
      if (mScanned == mEnd) {
        mPos = mEnd;
        return;
      }
      Scan(std::max(mScanned, mPos));
    }
  }

  // Tokenizes text, comments and elements until the end of the input or an end
  // tag, whose "</" is left unconsumed.
  //
  // https://html.spec.whatwg.org/#data-state
  //   Consume the next input character:
  //   U+0026 AMPERSAND (&)
  //     Set the return state to the data state. Switch to the character
  //     reference state.
  //   U+003C LESS-THAN SIGN (<)
  //     Switch to the tag open state.
  //   U+0000 NULL
  //     This is an unexpected-null-character parse error. Emit the current
  //     input character as a character token.
  //   EOF
  //     Emit an end-of-file token.
  //   Anything else
  //     Emit the current input character as a character token.
  //
  // https://html.spec.whatwg.org/#parsing-main-inbody
  //   A character token that is U+0000 NULL
  //     Parse error. Ignore the token.
  //
  // https://html.spec.whatwg.org/#preprocessing-the-input-stream
  //   Before the tokenization stage, the input stream must be preprocessed by
  //   normalizing newlines. Thus, newlines in HTML DOMs are represented by
  //   U+000A LF characters, and there are never any U+000D CR characters in
  //   the input to the tokenization stage.
  //
  // ParseDecodedText() handles character references. This leaves U+0000 and
  // carriage returns in text to the full parser.
  bool ParseChildren(ContentModel aModel) {
    while (true) {
      const char16_t* text = mPos;
      SkipText();
      if (!AtEnd() && *mPos == '&') {
        if (!ParseDecodedText(text)) {
          return false;
        }
      } else if (mPos != text &&
                 !mTokens.append(Token{Token::Kind::Text, 0, 0, false,
                                       Offset(text), uint32_t(mPos - text)})) {
        return false;
      }
      if (AtEnd()) {
        return true;
      }
      if (*mPos != '<') {
        return false;
      }
      // https://html.spec.whatwg.org/#tag-open-state
      //   Consume the next input character:
      //   U+0021 EXCLAMATION MARK (!)
      //     Switch to the markup declaration open state.
      //   U+002F SOLIDUS (/)
      //     Switch to the end tag open state.
      //   ASCII alpha
      //     Create a new start tag token, set its tag name to the empty
      //     string. Reconsume in the tag name state.
      //   U+003F QUESTION MARK (?)
      //     Set the temporary buffer to the empty string. Switch to the
      //     processing instruction open state.
      //   EOF
      //     This is an eof-before-tag-name parse error. Emit a U+003C
      //     LESS-THAN SIGN character token and an end-of-file token.
      //   Anything else
      //     This is an invalid-first-character-of-tag-name parse error. Emit a
      //     U+003C LESS-THAN SIGN character token. Reconsume in the data
      //     state.
      //
      // ParseComment() handles the markup declaration and the processing
      // instruction, ParseEndTag() the end tag, and ParseElement() rejects
      // anything but a tag name.
      if (mEnd - mPos >= 2 && mPos[1] == '/') {
        return true;
      }
      if (mEnd - mPos >= 2 && (mPos[1] == '!' || mPos[1] == '?')) {
        if (!ParseComment()) {
          return false;
        }
        continue;
      }
      ++mPos;
      if (!ParseElement(aModel)) {
        return false;
      }
    }
  }

  // Tokenizes the text from aStart on, whose character references from mPos
  // on get decoded into mDecodedData.
  bool ParseDecodedText(const char16_t* aStart) {
    const uint32_t offset = mDecodedData.Length();
    if (!mDecodedData.Append(aStart, mPos - aStart, fallible)) {
      return false;
    }
    do {
      if (!AppendCharacterReference(false)) {
        return false;
      }
      const char16_t* text = mPos;
      SkipText();
      if (!mDecodedData.Append(text, mPos - text, fallible)) {
        return false;
      }
    } while (!AtEnd() && *mPos == '&');
    return mTokens.append(Token{Token::Kind::Text, 0, 0, true, offset,
                                uint32_t(mDecodedData.Length() - offset)});
  }

  // Decodes the character reference at mPos into mDecodedData, or a lone "&"
  // that doesn't start one. Only references whose decoding doesn't depend on
  // the context are supported, except that in attribute values, an "&"
  // followed by letters and digits and then "=" is always literal.
  //
  // https://html.spec.whatwg.org/#character-reference-state
  //   Set the temporary buffer to the empty string. Append a U+0026 AMPERSAND
  //   (&) character to the temporary buffer. Consume the next input
  //   character:
  //   ASCII alphanumeric
  //     Reconsume in the named character reference state.
  //   U+0023 NUMBER SIGN (#)
  //     Append the current input character to the temporary buffer. Switch to
  //     the numeric character reference state.
  //   Anything else
  //     Flush code points consumed as a character reference. Reconsume in the
  //     return state.
  //
  // https://html.spec.whatwg.org/#flush-code-points-consumed-as-a-character-reference
  //   When a state says to flush code points consumed as a character
  //   reference, it means that for each code point in the temporary buffer
  //   (in the order they were added to the buffer), the user agent must append
  //   the code point from the buffer to the current attribute's value if the
  //   character reference was consumed as part of an attribute, or emit the
  //   code point as a character token otherwise.
  bool AppendCharacterReference(bool aInAttributeValue) {
    MOZ_ASSERT(*mPos == '&');
    const char16_t* pos = mPos + 1;
    if (pos == mEnd || !(IsAsciiAlphanumeric(*pos) || *pos == '#')) {
      ++mPos;
      return mDecodedData.Append(u'&', fallible);
    }
    // https://html.spec.whatwg.org/#numeric-character-reference-state
    //   Set the character reference code to zero (0).
    //   Consume the next input character:
    //   U+0078 LATIN SMALL LETTER X
    //   U+0058 LATIN CAPITAL LETTER X
    //     Append the current input character to the temporary buffer. Switch
    //     to the hexadecimal character reference start state.
    //   ASCII digit
    //     Reconsume in the decimal character reference state.
    //   Anything else
    //     This is an absence-of-digits-in-numeric-character-reference parse
    //     error. Flush code points consumed as a character reference.
    //     Reconsume in the return state.
    //
    // https://html.spec.whatwg.org/#hexadecimal-character-reference-start-state
    //   Consume the next input character:
    //   ASCII hex digit
    //     Reconsume in the hexadecimal character reference state.
    //   Anything else
    //     This is an absence-of-digits-in-numeric-character-reference parse
    //     error. Flush code points consumed as a character reference.
    //     Reconsume in the return state.
    //
    // https://html.spec.whatwg.org/#hexadecimal-character-reference-state
    //   Consume the next input character:
    //   ASCII digit
    //     Multiply the character reference code by 16. Add a numeric version
    //     of the current input character (subtract 0x0030 from the
    //     character's code point) to the character reference code.
    //   ASCII upper hex digit
    //     Multiply the character reference code by 16. Add a numeric version
    //     of the current input character as a hexadecimal digit (subtract
    //     0x0037 from the character's code point) to the character reference
    //     code.
    //   ASCII lower hex digit
    //     Multiply the character reference code by 16. Add a numeric version
    //     of the current input character as a hexadecimal digit (subtract
    //     0x0057 from the character's code point) to the character reference
    //     code.
    //   U+003B SEMICOLON (;)
    //     Switch to the numeric character reference end state.
    //   Anything else
    //     This is a missing-semicolon-after-character-reference parse error.
    //     Reconsume in the numeric character reference end state.
    //
    // https://html.spec.whatwg.org/#decimal-character-reference-state
    //   Consume the next input character:
    //   ASCII digit
    //     Multiply the character reference code by 10. Add a numeric version
    //     of the current input character (subtract 0x0030 from the
    //     character's code point) to the character reference code.
    //   U+003B SEMICOLON (;)
    //     Switch to the numeric character reference end state.
    //   Anything else
    //     This is a missing-semicolon-after-character-reference parse error.
    //     Reconsume in the numeric character reference end state.
    //
    // https://html.spec.whatwg.org/#numeric-character-reference-end-state
    //   Check the character reference code:
    //   - If the number is 0x00, then this is a null-character-reference parse
    //     error. Set the character reference code to 0xFFFD.
    //   - If the number is greater than 0x10FFFF, then this is a
    //     character-reference-outside-unicode-range parse error. Set the
    //     character reference code to 0xFFFD.
    //   - If the number is a surrogate, then this is a
    //     surrogate-character-reference parse error. Set the character
    //     reference code to 0xFFFD.
    //   - If the number is a noncharacter, then this is a
    //     noncharacter-character-reference parse error.
    //   - If the number is 0x0D, or a control that's not ASCII whitespace,
    //     then this is a control-character-reference parse error. If the
    //     number is one of the numbers in the first column of the following
    //     table, then find the row with that number in the first column, and
    //     set the character reference code to the number in the second column
    //     of that row.
    //     ...
    //   Set the temporary buffer to the empty string. Append a code point
    //   equal to the character reference code to the temporary buffer. Flush
    //   code points consumed as a character reference. Switch to the return
    //   state.
    //
    // Missing digits, a missing semicolon, and every code point for which the
    // end state reports a parse error, are left to the full parser. So is
    // U+000C FORM FEED, which isn't an error.
    if (*pos == '#') {
      ++pos;
      const bool hex = pos != mEnd && (*pos == 'x' || *pos == 'X');
      if (hex) {
        ++pos;
      }
      const char16_t* digits = pos;
      uint32_t value = 0;
      for (; pos != mEnd && (hex ? IsAsciiHexDigit(*pos) : IsAsciiDigit(*pos));
           ++pos) {
        value = value * (hex ? 16 : 10) + AsciiAlphanumericToNumber(*pos);
        if (value > 0x10FFFF) {
          return false;
        }
      }
      // Leave out the code points the tokenizer replaces or reports as
      // errors.
      const bool supported = value == '\t' || value == '\n' ||
                             (value >= 0x20 && value < 0x7F) ||
                             (value >= 0xA0 && value < 0xD800) ||
                             (value >= 0xE000 && value < 0xFDD0) ||
                             (value >= 0xFDF0 && (value & 0xFFFE) != 0xFFFE);
      if (pos == digits || pos == mEnd || *pos != ';' || !supported) {
        return false;
      }
      mPos = pos + 1;
      return IsInBMP(value)
                 ? mDecodedData.Append(char16_t(value), fallible)
                 : mDecodedData.Append(HighSurrogate(value), fallible) &&
                       mDecodedData.Append(LowSurrogate(value), fallible);
    }
    // https://html.spec.whatwg.org/#named-character-reference-state
    //   Consume the maximum number of characters possible, where the consumed
    //   characters are one of the identifiers in the first column of the
    //   named character references table. Append each character to the
    //   temporary buffer when it's consumed.
    //   If there is a match
    //     If the character reference was consumed as part of an attribute, and
    //     the last character matched is not a U+003B SEMICOLON character (;),
    //     and the next input character is either a U+003D EQUALS SIGN
    //     character (=) or an ASCII alphanumeric, then, for historical
    //     reasons, flush code points consumed as a character reference and
    //     switch to the return state.
    //     Otherwise:
    //       1. If the last character matched is not a U+003B SEMICOLON
    //          character (;), then this is a
    //          missing-semicolon-after-character-reference parse error.
    //       2. Set the temporary buffer to the empty string. Append one or two
    //          characters corresponding to the character reference name (as
    //          given by the second column of the named character references
    //          table) to the temporary buffer.
    //       3. Flush code points consumed as a character reference. Switch to
    //          the return state.
    //   Otherwise
    //     Flush code points consumed as a character reference. Switch to the
    //     ambiguous ampersand state.
    //
    // https://html.spec.whatwg.org/#ambiguous-ampersand-state
    //   Consume the next input character:
    //   ASCII alphanumeric
    //     If the character reference was consumed as part of an attribute,
    //     then append the current input character to the current attribute's
    //     value. Otherwise, emit the current input character as a character
    //     token.
    //   U+003B SEMICOLON (;)
    //     This is an unknown-named-character-reference parse error. Reconsume
    //     in the return state.
    //   Anything else
    //     Reconsume in the return state.
    //
    // No identifier in the table starts with one of the names below including
    // their semicolon, so these are the longest match. In an attribute value,
    // the longest match within letters and digits followed by "=" doesn't end
    // with a semicolon, so the historical rule keeps the "&" and what follows
    // literal, as does the ambiguous ampersand state if there is no match.
    // Everything else is left to the full parser.
    static constexpr struct {
      std::u16string_view mName;
      char16_t mValue;
    } kNamed[] = {{u"amp;", '&'},  {u"lt;", '<'},    {u"gt;", '>'},
                  {u"quot;", '"'}, {u"apos;", '\''}, {u"nbsp;", 0xA0}};
    for (const auto& named : kNamed) {
      if (size_t(mEnd - pos) >= named.mName.size() &&
          !memcmp(pos, named.mName.data(),
                  named.mName.size() * sizeof(char16_t))) {
        mPos = pos + named.mName.size();
        return mDecodedData.Append(named.mValue, fallible);
      }
    }
    if (aInAttributeValue) {
      while (pos != mEnd && IsAsciiAlphanumeric(*pos)) {
        ++pos;
      }
      if (pos != mEnd && *pos == '=') {
        ++mPos;
        return mDecodedData.Append(u'&', fallible);
      }
    }
    return false;
  }

  // Tokenizes a comment, or a bogus comment starting with "<?", at mPos.
  bool ParseComment() {
    const char16_t* pos = mPos + 1;
    const char16_t* data;
    const char16_t* dataEnd;
    if (*pos == '?') {
      // https://html.spec.whatwg.org/#processing-instruction-open-state
      //   Consume the next input character:
      //   ASCII alpha
      //   U+005F LOW LINE (_)
      //     Reconsume in the processing instruction target state.
      //   EOF
      //     This is an eof-in-processing-instruction parse error. Emit an
      //     end-of-file token.
      //   Anything else
      //     This is an invalid-first-character-of-processing-instruction-target
      //     parse error. Convert the temporary buffer to a comment. Reconsume
      //     in the bogus comment state.
      //
      // https://html.spec.whatwg.org/#processing-instruction-target-state
      //   Consume the next input character:
      //   U+0009 CHARACTER TABULATION (tab)
      //   U+000A LINE FEED (LF)
      //   U+000C FORM FEED (FF)
      //   U+0020 SPACE
      //   U+003F QUESTION MARK (?)
      //   U+003E GREATER-THAN SIGN (>)
      //     Let target be the concatenation of the code points in the
      //     temporary buffer, in the order they were added to the buffer.
      //     If target is an ASCII case-insensitive match for "xml" or
      //     "xml-stylesheet":
      //       1. This is a disallowed-processing-instruction-target parse
      //          error.
      //       2. Convert the temporary buffer to a comment.
      //       3. Reconsume in the bogus comment state.
      //     Otherwise:
      //       1. Create a processing instruction token whose target is target
      //          and data is the empty string.
      //       2. Reconsume in the after processing instruction target state.
      //   ASCII alphanumeric
      //   U+002D HYPHEN-MINUS (-)
      //   U+005F LOW LINE (_)
      //     Append the current input character to the temporary buffer.
      //   EOF
      //     This is an eof-in-processing-instruction parse error. Emit an
      //     end-of-file token.
      //   Anything else
      //     This is an invalid-processing-instruction-target parse error.
      //     Convert the temporary buffer to a comment. Reconsume in the bogus
      //     comment state.
      //
      // https://html.spec.whatwg.org/#convert-the-temporary-buffer-to-a-comment
      //   To convert the temporary buffer to a comment, create a comment token
      //   whose data is the concatenation of "?" and the code points in the
      //   temporary buffer, in the order they were added to the buffer.
      //
      // https://html.spec.whatwg.org/#bogus-comment-state
      //   Consume the next input character:
      //   U+003E GREATER-THAN SIGN (>)
      //     Switch to the data state. Emit the current comment token.
      //   EOF
      //     Emit the current comment token. Emit an end-of-file token.
      //   U+0000 NULL
      //     This is an unexpected-null-character parse error. Append a U+FFFD
      //     REPLACEMENT CHARACTER character to the comment token's data.
      //   Anything else
      //     Append the current input character to the comment token's data.
      //
      // Gecko's tokenizer doesn't implement processing instructions yet. It
      // makes a bogus comment of everything from the "?" to the next ">",
      // like the tokenization stage before processing instructions were
      // added to it. That is the same comment as the spec's whenever the
      // target state finds a character that isn't allowed in a target, as
      // in Lit's "<?lit$1$>" markers. So this rejects every input for which
      // the spec creates a processing instruction, or a comment from the
      // "xml" and "xml-stylesheet" targets, and leaves it to the full parser,
      // which therefore must be the one to start creating processing
      // instructions.
      const char16_t* target = pos + 1;
      if (target != mEnd && (IsAsciiAlpha(*target) || *target == '_')) {
        do {
          ++target;
        } while (target != mEnd && (IsAsciiAlphanumeric(*target) ||
                                    *target == '-' || *target == '_'));
        // A carriage return is whitespace here, like the line feed it is
        // normalized to.
        if (target == mEnd || IsAsciiWhitespace(*target) || *target == '?' ||
            *target == '>') {
          return false;
        }
      }
      data = pos;
      while (pos != mEnd && *pos != '>') {
        ++pos;
      }
      if (pos == mEnd) {
        return false;
      }
      dataEnd = pos;
      mPos = pos + 1;
    } else {
      // https://html.spec.whatwg.org/#markup-declaration-open-state
      //   If the next few characters are:
      //   Two U+002D HYPHEN-MINUS characters (-)
      //     Consume those two characters, create a comment token whose data
      //     is the empty string, and switch to the comment start state.
      //   ASCII case-insensitive match for "DOCTYPE"
      //     Consume those characters and switch to the DOCTYPE state.
      //   "[CDATA["
      //     Consume those characters. If there is an adjusted current node and
      //     it is not an element in the HTML namespace, then switch to the
      //     CDATA section state. Otherwise, this is a cdata-in-html-content
      //     parse error. Create a comment token whose data is "[CDATA[".
      //     Switch to the bogus comment state.
      //   Anything else
      //     This is an incorrectly-opened-comment parse error. Create a
      //     comment token whose data is the empty string. Switch to the bogus
      //     comment state (don't consume anything in the current state).
      //
      // Only the comment is supported.
      if (mEnd - pos < 3 || pos[1] != '-' || pos[2] != '-') {
        return false;
      }
      pos += 3;
      // https://html.spec.whatwg.org/#comment-start-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment start dash state.
      //   U+003E GREATER-THAN SIGN (>)
      //     This is an abrupt-closing-of-empty-comment parse error. Switch to
      //     the data state. Emit the current comment token.
      //   Anything else
      //     Reconsume in the comment state.
      //
      // https://html.spec.whatwg.org/#comment-start-dash-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment end state.
      //   U+003E GREATER-THAN SIGN (>)
      //     This is an abrupt-closing-of-empty-comment parse error. Switch to
      //     the data state. Emit the current comment token.
      //   EOF
      //     This is an eof-in-comment parse error. Emit the current comment
      //     token. Emit an end-of-file token.
      //   Anything else
      //     Append a U+002D HYPHEN-MINUS character (-) to the comment token's
      //     data. Reconsume in the comment state.
      //
      // "<!-->" and "<!--->" are left to the full parser.
      if (pos != mEnd &&
          (*pos == '>' || (*pos == '-' && pos + 1 != mEnd && pos[1] == '>'))) {
        return false;
      }
      // https://html.spec.whatwg.org/#comment-state
      //   Consume the next input character:
      //   U+003C LESS-THAN SIGN (<)
      //     Append the current input character to the comment token's data.
      //     Switch to the comment less-than sign state.
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment end dash state.
      //   U+0000 NULL
      //     This is an unexpected-null-character parse error. Append a U+FFFD
      //     REPLACEMENT CHARACTER character to the comment token's data.
      //   EOF
      //     This is an eof-in-comment parse error. Emit the current comment
      //     token. Emit an end-of-file token.
      //   Anything else
      //     Append the current input character to the comment token's data.
      //
      // https://html.spec.whatwg.org/#comment-less-than-sign-state
      //   Consume the next input character:
      //   U+0021 EXCLAMATION MARK (!)
      //     Append the current input character to the comment token's data.
      //     Switch to the comment less-than sign bang state.
      //   U+003C LESS-THAN SIGN (<)
      //     Append the current input character to the comment token's data.
      //   Anything else
      //     Reconsume in the comment state.
      //
      // https://html.spec.whatwg.org/#comment-less-than-sign-bang-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment less-than sign bang dash state.
      //   Anything else
      //     Reconsume in the comment state.
      //
      // https://html.spec.whatwg.org/#comment-less-than-sign-bang-dash-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment less-than sign bang dash dash state.
      //   Anything else
      //     Reconsume in the comment end dash state.
      //
      // https://html.spec.whatwg.org/#comment-less-than-sign-bang-dash-dash-state
      //   Consume the next input character:
      //   U+003E GREATER-THAN SIGN (>)
      //   EOF
      //     Reconsume in the comment end state.
      //   Anything else
      //     This is a nested-comment parse error. Reconsume in the comment end
      //     state.
      //
      // https://html.spec.whatwg.org/#comment-end-dash-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Switch to the comment end state.
      //   EOF
      //     This is an eof-in-comment parse error. Emit the current comment
      //     token. Emit an end-of-file token.
      //   Anything else
      //     Append a U+002D HYPHEN-MINUS character (-) to the comment token's
      //     data. Reconsume in the comment state.
      //
      // https://html.spec.whatwg.org/#comment-end-state
      //   Consume the next input character:
      //   U+003E GREATER-THAN SIGN (>)
      //     Switch to the data state. Emit the current comment token.
      //   U+0021 EXCLAMATION MARK (!)
      //     Switch to the comment end bang state.
      //   U+002D HYPHEN-MINUS (-)
      //     Append a U+002D HYPHEN-MINUS character (-) to the comment token's
      //     data.
      //   EOF
      //     This is an eof-in-comment parse error. Emit the current comment
      //     token. Emit an end-of-file token.
      //   Anything else
      //     Append two U+002D HYPHEN-MINUS characters (-) to the comment
      //     token's data. Reconsume in the comment state.
      //
      // https://html.spec.whatwg.org/#comment-end-bang-state
      //   Consume the next input character:
      //   U+002D HYPHEN-MINUS (-)
      //     Append two U+002D HYPHEN-MINUS characters (-) and a U+0021
      //     EXCLAMATION MARK character (!) to the comment token's data. Switch
      //     to the comment end dash state.
      //   U+003E GREATER-THAN SIGN (>)
      //     This is an incorrectly-closed-comment parse error. Switch to the
      //     data state. Emit the current comment token.
      //   EOF
      //     This is an eof-in-comment parse error. Emit the current comment
      //     token. Emit an end-of-file token.
      //   Anything else
      //     Append two U+002D HYPHEN-MINUS characters (-) and a U+0021
      //     EXCLAMATION MARK character (!) to the comment token's data.
      //     Reconsume in the comment state.
      //
      // Unless the data contains "--!", these states end the comment at the
      // first "-->" and put everything before it into the data. The less-than
      // sign states only report nested comments. "--!" and the end of the
      // input are left to the full parser.
      data = pos;
      while (true) {
        if (mEnd - pos < 3) {
          return false;
        }
        if (pos[0] == '-' && pos[1] == '-') {
          if (pos[2] == '>') {
            break;
          }
          // "--!>" ends a comment too.
          if (pos[2] == '!') {
            return false;
          }
        }
        ++pos;
      }
      dataEnd = pos;
      mPos = pos + 3;
    }
    // Newline normalization and U+0000 need the full parser.
    for (const char16_t* c = data; c != dataEnd; ++c) {
      if (*c == '\r' || !*c) {
        return false;
      }
    }
    return mTokens.append(Token{Token::Kind::Comment, 0, 0, false, Offset(data),
                                uint32_t(dataEnd - data)});
  }

  // Tokenizes an element whose "<" was consumed.
  bool ParseElement(ContentModel aModel) {
    if (++mDepth > kMaxDepth) {
      return false;
    }
    // https://html.spec.whatwg.org/#tag-name-state
    //   Consume the next input character:
    //   U+0009 CHARACTER TABULATION (tab)
    //   U+000A LINE FEED (LF)
    //   U+000C FORM FEED (FF)
    //   U+0020 SPACE
    //     Switch to the before attribute name state.
    //   U+002F SOLIDUS (/)
    //     Switch to the self-closing start tag state.
    //   U+003E GREATER-THAN SIGN (>)
    //     Switch to the data state. Emit the current tag token.
    //   ASCII upper alpha
    //     Append the lowercase version of the current input character (add
    //     0x0020 to the character's code point) to the current tag token's tag
    //     name.
    //   U+0000 NULL
    //     This is an unexpected-null-character parse error. Append a U+FFFD
    //     REPLACEMENT CHARACTER character to the current tag token's tag name.
    //   EOF
    //     This is an eof-in-tag parse error. Emit an end-of-file token.
    //   Anything else
    //     Append the current input character to the current tag token's tag
    //     name.
    //
    // The supported names consist of letters and digits, so this stops at
    // anything else and rejects the tag unless that ends the name. A carriage
    // return ends it like the line feed it is normalized to.
    char16_t name[kMaxTagNameLength];
    uint32_t length = 0;
    while (mPos != mEnd && length < kMaxTagNameLength &&
           (IsAsciiAlpha(*mPos) || (length && IsAsciiDigit(*mPos)))) {
      name[length++] = ToLower(*mPos++);
    }
    if (AtEnd() ||
        !(IsAsciiWhitespace(*mPos) || *mPos == '>' || *mPos == '/')) {
      return false;
    }
    const uint8_t tagIndex = LookupTagIndex(mTables, name, length);
    if (tagIndex == kNoTag) {
      return false;
    }
    const TagInfo& tag = kTags[tagIndex];
    // Reject any nesting for which the tree builder would close an open
    // element or reparent nodes, see the rules quoted above kTags.
    const bool allowed = aModel == ContentModel::ListItems
                             ? tag.mName == nsGkAtoms::li
                         : aModel == ContentModel::Phrasing ? tag.mPhrasing
                                                            : true;
    if (!allowed || (tag.mName == nsGkAtoms::a && mInsideA) ||
        (tag.mName == nsGkAtoms::button && mInsideButton) ||
        (tag.mName == nsGkAtoms::li && mInsideLi &&
         aModel != ContentModel::ListItems)) {
      return false;
    }

    const size_t tokenIndex = mTokens.length();
    if (!mTokens.append(Token{Token::Kind::StartTag, tagIndex, 0, false,
                              uint32_t(mAttributes.length()), 0})) {
      return false;
    }
    SkipWhitespace();
    uint8_t attributeCount;
    if (!ParseAttributes(attributeCount)) {
      return false;
    }
    mTokens[tokenIndex].mAttributeCount = attributeCount;
    if (tag.mVoid) {
      --mDepth;
      return true;
    }

    const ContentModel childModel =
        tag.mChildren == ContentModel::Inherit
            ? (aModel == ContentModel::Phrasing ? ContentModel::Phrasing
                                                : ContentModel::Flow)
            : tag.mChildren;
    const bool wasInsideA = mInsideA;
    const bool wasInsideButton = mInsideButton;
    const bool wasInsideLi = mInsideLi;
    mInsideA |= tag.mName == nsGkAtoms::a;
    mInsideButton |= tag.mName == nsGkAtoms::button;
    mInsideLi |= tag.mName == nsGkAtoms::li;
    const bool parsed = ParseChildren(childModel);
    mInsideA = wasInsideA;
    mInsideButton = wasInsideButton;
    mInsideLi = wasInsideLi;
    if (!parsed || !ParseEndTag(tagIndex)) {
      return false;
    }
    --mDepth;
    return true;
  }

  // Tokenizes the end tag of the element with index aTagIndex, and rejects any
  // other end tag.
  //
  // https://html.spec.whatwg.org/#end-tag-open-state
  //   Consume the next input character:
  //   ASCII alpha
  //     Create a new end tag token, set its tag name to the empty string.
  //     Reconsume in the tag name state.
  //   U+003E GREATER-THAN SIGN (>)
  //     This is a missing-end-tag-name parse error. Switch to the data state.
  //   EOF
  //     This is an eof-before-tag-name parse error. Emit a U+003C LESS-THAN
  //     SIGN character token, a U+002F SOLIDUS character token and an
  //     end-of-file token.
  //   Anything else
  //     This is an invalid-first-character-of-tag-name parse error. Create a
  //     comment token whose data is the empty string. Reconsume in the bogus
  //     comment state.
  //
  // The tag name state is quoted in ParseElement(). Only whitespace may
  // follow the name, since anything else would be an attribute or a "/",
  // which end tags may not have.
  //
  // With only matching end tags, the element is the current node when its end
  // tag comes, and these rules of the "in body" insertion mode just pop it
  // (https://html.spec.whatwg.org/#parsing-main-inbody):
  //
  //   An end tag whose tag name is one of: "address", "article", "aside",
  //   "blockquote", "button", "center", "details", "dialog", "dir", "div",
  //   "dl", "fieldset", "figcaption", "figure", "footer", "header", "hgroup",
  //   "listing", "main", "menu", "nav", "ol", "pre", "search", "section",
  //   "select", "summary", "ul"
  //     If the stack of open elements does not have an element in scope that
  //     is an HTML element with the same tag name as that of the token, then
  //     this is a parse error; ignore the token.
  //     Otherwise, run these steps:
  //       1. Generate implied end tags.
  //       2. If the current node is not an HTML element with the same tag name
  //          as that of the token, then this is a parse error.
  //       3. Pop elements from the stack of open elements until an HTML
  //          element with the same tag name as the token has been popped from
  //          the stack.
  //
  //   An end tag whose tag name is "p"
  //     If the stack of open elements does not have a p element in button
  //     scope, then this is a parse error; insert an HTML element for a "p"
  //     start tag token with no attributes.
  //     Close a p element.
  //
  //   An end tag whose tag name is "li"
  //     If the stack of open elements does not have an li element in list item
  //     scope, then this is a parse error; ignore the token.
  //     Otherwise, run these steps:
  //       1. Generate implied end tags, except for li elements.
  //       2. If the current node is not an li element, then this is a parse
  //          error.
  //       3. Pop elements from the stack of open elements until an li element
  //          has been popped from the stack.
  //
  //   An end tag whose tag name is one of: "h1", "h2", "h3", "h4", "h5", "h6"
  //     If the stack of open elements does not have an element in scope that
  //     is an HTML element and whose tag name is one of "h1", "h2", "h3", "h4",
  //     "h5", or "h6", then this is a parse error; ignore the token.
  //     Otherwise, run these steps:
  //       1. Generate implied end tags.
  //       2. If the current node is not an HTML element with the same tag name
  //          as that of the token, then this is a parse error.
  //       3. Pop elements from the stack of open elements until an HTML
  //          element whose tag name is one of "h1", "h2", "h3", "h4", "h5", or
  //          "h6" has been popped from the stack.
  //
  //   An end tag whose tag name is one of: "a", "b", "big", "code", "em",
  //   "font", "i", "nobr", "s", "small", "strike", "strong", "tt", "u"
  //     Run the adoption agency algorithm for the token.
  //
  //   Any other end tag
  //     1. Initialize node to be the current node (the bottommost node of the
  //        stack).
  //     2. Loop: If node is an HTML element with the same tag name as the
  //        token:
  //        1. Generate implied end tags, except for HTML elements with the
  //           same tag name as the token.
  //        2. If node is not the current node, then this is a parse error.
  //        3. Pop all the nodes from the current node up to node, including
  //           node, then stop these steps.
  //     ...
  //
  // When the steps above say the user agent is to close a p element, it
  // means that the user agent must run the following steps:
  //   1. Generate implied end tags, except for p elements.
  //   2. If the current node is not a p element, then this is a parse error.
  //   3. Pop elements from the stack of open elements until a p element has
  //      been popped from the stack.
  //
  // Generating implied end tags pops a current node that is one of the
  // elements it lists, of which only <p> and <li> are supported, and the
  // rules for their end tags except them.
  //
  // https://html.spec.whatwg.org/#adoption-agency-algorithm
  //   1. Let subject be token's tag name.
  //   2. If the current node is an HTML element whose tag name is subject, and
  //      the current node is not in the list of active formatting elements,
  //      then pop the current node off the stack of open elements and return.
  //   3. Let outerLoopCounter be 0.
  //   4. While true:
  //      1. If outerLoopCounter is greater than or equal to 8, then return.
  //      2. Increment outerLoopCounter by 1.
  //      3. Let formattingElement be the last element in the list of active
  //         formatting elements that:
  //         - is between the end of the list and the last marker in the list,
  //           if any, or the start of the list otherwise, and
  //         - has the tag name subject.
  //         If there is no such element, then act as described in the "any
  //         other end tag" entry above and return.
  //      4. If formattingElement is not in the stack of open elements, then
  //         this is a parse error; remove the element from the list, and
  //         return.
  //      5. If formattingElement is in the stack of open elements, but the
  //         element is not in scope, then this is a parse error; return.
  //      6. If formattingElement is not the current node, this is a parse
  //         error. (But do not return.)
  //      7. Let furthestBlock be the topmost node in the stack of open
  //         elements that is lower in the stack than formattingElement, and is
  //         an element in the special category. There might not be one.
  //      8. If there is no furthestBlock, then the UA must first pop all the
  //         nodes from the bottom of the stack of open elements, from the
  //         current node up to and including formattingElement, then remove
  //         formattingElement from the list of active formatting elements, and
  //         finally return.
  //      ...
  //
  // If the "Noah's Ark" clause dropped the current node from the list, step 2
  // pops it. Otherwise, step 4.3 finds the current node, and since nothing is
  // lower in the stack, step 4.8 pops it.
  bool ParseEndTag(uint8_t aTagIndex) {
    // ParseChildren() stopped at "</" or at the end of the input.
    if (AtEnd()) {
      return false;
    }
    mPos += 2;
    const char16_t* name = mTables.mNames[aTagIndex];
    const uint32_t length = mTables.mNameLengths[aTagIndex];
    if (uint32_t(mEnd - mPos) <= length) {
      return false;
    }
    for (uint32_t i = 0; i < length; ++i) {
      if (ToLower(mPos[i]) != name[i]) {
        return false;
      }
    }
    mPos += length;
    if (!IsAsciiWhitespace(*mPos) && *mPos != '>') {
      return false;
    }
    SkipWhitespace();
    if (AtEnd() || *mPos != '>') {
      return false;
    }
    ++mPos;
    return mTokens.append(Token{Token::Kind::EndTag, 0, 0, false, 0, 0});
  }

  // Tokenizes the attributes and the end of a start tag. A carriage return in
  // a tag is whitespace like the line feed it is normalized to.
  //
  // https://html.spec.whatwg.org/#before-attribute-name-state
  //   Consume the next input character:
  //   U+0009 CHARACTER TABULATION (tab)
  //   U+000A LINE FEED (LF)
  //   U+000C FORM FEED (FF)
  //   U+0020 SPACE
  //     Ignore the character.
  //   U+002F SOLIDUS (/)
  //   U+003E GREATER-THAN SIGN (>)
  //   EOF
  //     Reconsume in the after attribute name state.
  //   U+003D EQUALS SIGN (=)
  //     This is an unexpected-equals-sign-before-attribute-name parse error.
  //     Start a new attribute in the current tag token. Set that attribute's
  //     name to the current input character, and its value to the empty
  //     string. Switch to the attribute name state.
  //   Anything else
  //     Start a new attribute in the current tag token. Set that attribute
  //     name and value to the empty string. Reconsume in the attribute name
  //     state.
  //
  // https://html.spec.whatwg.org/#after-attribute-name-state
  //   Consume the next input character:
  //   U+0009 CHARACTER TABULATION (tab)
  //   U+000A LINE FEED (LF)
  //   U+000C FORM FEED (FF)
  //   U+0020 SPACE
  //     Ignore the character.
  //   U+002F SOLIDUS (/)
  //     Switch to the self-closing start tag state.
  //   U+003D EQUALS SIGN (=)
  //     Switch to the before attribute value state.
  //   U+003E GREATER-THAN SIGN (>)
  //     Switch to the data state. Emit the current tag token.
  //   EOF
  //     This is an eof-in-tag parse error. Emit an end-of-file token.
  //   Anything else
  //     Start a new attribute in the current tag token. Set that attribute
  //     name and value to the empty string. Reconsume in the attribute name
  //     state.
  //
  // https://html.spec.whatwg.org/#before-attribute-value-state
  //   Consume the next input character:
  //   U+0009 CHARACTER TABULATION (tab)
  //   U+000A LINE FEED (LF)
  //   U+000C FORM FEED (FF)
  //   U+0020 SPACE
  //     Ignore the character.
  //   U+0022 QUOTATION MARK (")
  //     Switch to the attribute value (double-quoted) state.
  //   U+0027 APOSTROPHE (')
  //     Switch to the attribute value (single-quoted) state.
  //   U+003E GREATER-THAN SIGN (>)
  //     This is a missing-attribute-value parse error. Switch to the data
  //     state. Emit the current tag token.
  //   Anything else
  //     Reconsume in the attribute value (unquoted) state.
  //
  // https://html.spec.whatwg.org/#self-closing-start-tag-state
  //   Consume the next input character:
  //   U+003E GREATER-THAN SIGN (>)
  //     Set the self-closing flag of the current tag token. Switch to the data
  //     state. Emit the current tag token.
  //   EOF
  //     This is an eof-in-tag parse error. Emit an end-of-file token.
  //   Anything else
  //     This is an unexpected-solidus-in-tag parse error. Reconsume in the
  //     before attribute name state.
  //
  // https://html.spec.whatwg.org/#acknowledge-self-closing-flag
  //   When a start tag token is emitted with its self-closing flag set, if the
  //   flag is not acknowledged when it is processed by the tree construction
  //   stage, that is a non-void-html-element-start-tag-with-trailing-solidus
  //   parse error.
  //
  // So "/>" only ends the tag, and a non-void element stays open. Whitespace
  // between the "/" and the ">" goes through the before attribute name state,
  // which ignores it. Anything else after a "/", and an attribute name that
  // starts with "=", are left to the full parser.
  bool ParseAttributes(uint8_t& aCount) {
    const size_t first = mAttributes.length();
    while (true) {
      if (AtEnd()) {
        return false;
      }
      if (*mPos == '>') {
        ++mPos;
        break;
      }
      if (*mPos == '/') {
        ++mPos;
        SkipWhitespace();
        if (AtEnd() || *mPos != '>') {
          return false;
        }
        ++mPos;
        break;
      }
      if (mAttributes.length() - first == kMaxAttributes) {
        return false;
      }
      // https://html.spec.whatwg.org/#attribute-name-state
      //   Consume the next input character:
      //   U+0009 CHARACTER TABULATION (tab)
      //   U+000A LINE FEED (LF)
      //   U+000C FORM FEED (FF)
      //   U+0020 SPACE
      //   U+002F SOLIDUS (/)
      //   U+003E GREATER-THAN SIGN (>)
      //   EOF
      //     Reconsume in the after attribute name state.
      //   U+003D EQUALS SIGN (=)
      //     Switch to the before attribute value state.
      //   ASCII upper alpha
      //     Append the lowercase version of the current input character (add
      //     0x0020 to the character's code point) to the current attribute's
      //     name.
      //   U+0000 NULL
      //     This is an unexpected-null-character parse error. Append a U+FFFD
      //     REPLACEMENT CHARACTER character to the current attribute's name.
      //   U+0022 QUOTATION MARK (")
      //   U+0027 APOSTROPHE (')
      //   U+003C LESS-THAN SIGN (<)
      //     This is an unexpected-character-in-attribute-name parse error.
      //     Treat it as per the "anything else" entry below.
      //   Anything else
      //     Append the current input character to the current attribute's
      //     name.
      const char16_t* const nameStart = mPos;
      char16_t name[kMaxAttributeNameLength];
      uint32_t length = 0;
      while (mPos != mEnd && length < kMaxAttributeNameLength) {
        const char16_t c = *mPos;
        // The tokenizer also accepts quotes and "<" in names, but reports
        // them as errors, and replaces U+0000.
        if (IsAsciiWhitespace(c) || c == '/' || c == '>' || c == '=' ||
            c == '"' || c == '\'' || c == '<' || !c) {
          break;
        }
        name[length++] = ToLower(c);
        ++mPos;
      }
      if (!length || AtEnd() ||
          !(IsAsciiWhitespace(*mPos) || *mPos == '=' || *mPos == '>' ||
            *mPos == '/')) {
        return false;
      }
      // "is" and "customelementregistry" change how elements are created.
      //
      // https://html.spec.whatwg.org/#create-an-element-for-the-token
      //   8. Let is be the value of the "is" attribute in token, if such an
      //      attribute exists; otherwise null.
      const nsDependentSubstring nameString(name, length);
      if (nameString.EqualsLiteral("is") ||
          nameString.EqualsLiteral("customelementregistry")) {
        return false;
      }
      nsHtml5AttributeName* const known =
          nsHtml5AttributeName::nameByBuffer(name, int32_t(length), nullptr);
      AttributeToken attribute{known,
                               Offset(nameStart),
                               uint8_t(length),
                               known && known->isUseAtom(),
                               false,
                               0,
                               0};
      // https://html.spec.whatwg.org/#attribute-name-state
      //   When the user agent leaves the attribute name state (and before
      //   emitting the tag token, if appropriate), the complete attribute's
      //   name must be compared to the other attributes on the same token; if
      //   there is already an attribute on the token with the exact same name,
      //   then this is a duplicate-attribute parse error and the new attribute
      //   must be removed from the token.
      //
      // Removing it is left to the full parser.
      // A known name never equals an unknown one.
      for (size_t i = first; i < mAttributes.length(); ++i) {
        const AttributeToken& other = mAttributes[i];
        if (other.mKnownName == known &&
            (known || UnknownNameEquals(other, name, length))) {
          return false;
        }
      }
      SkipWhitespace();
      if (!AtEnd() && *mPos == '=') {
        ++mPos;
        SkipWhitespace();
        if (!ParseAttributeValue(attribute)) {
          return false;
        }
        SkipWhitespace();
      }
      if (!mAttributes.append(attribute)) {
        return false;
      }
    }
    aCount = mAttributes.length() - first;
    return true;
  }

  // Tokenizes the attribute value at mPos, after the before attribute value
  // state (quoted in ParseAttributes()) skipped the whitespace.
  bool ParseAttributeValue(AttributeToken& aAttribute) {
    if (AtEnd()) {
      return false;
    }
    const char16_t quote = *mPos;
    if (quote == '"' || quote == '\'') {
      // https://html.spec.whatwg.org/#attribute-value-(double-quoted)-state
      //   Consume the next input character:
      //   U+0022 QUOTATION MARK (")
      //     Switch to the after attribute value (quoted) state.
      //   U+0026 AMPERSAND (&)
      //     Set the return state to the attribute value (double-quoted) state.
      //     Switch to the character reference state.
      //   U+0000 NULL
      //     This is an unexpected-null-character parse error. Append a U+FFFD
      //     REPLACEMENT CHARACTER character to the current attribute's value.
      //   EOF
      //     This is an eof-in-tag parse error. Emit an end-of-file token.
      //   Anything else
      //     Append the current input character to the current attribute's
      //     value.
      //
      // The attribute value (single-quoted) state is the same with U+0027
      // APOSTROPHE (') instead of U+0022 QUOTATION MARK (").
      //
      // https://html.spec.whatwg.org/#after-attribute-value-(quoted)-state
      //   Consume the next input character:
      //   U+0009 CHARACTER TABULATION (tab)
      //   U+000A LINE FEED (LF)
      //   U+000C FORM FEED (FF)
      //   U+0020 SPACE
      //     Switch to the before attribute name state.
      //   U+002F SOLIDUS (/)
      //     Switch to the self-closing start tag state.
      //   U+003E GREATER-THAN SIGN (>)
      //     Switch to the data state. Emit the current tag token.
      //   EOF
      //     This is an eof-in-tag parse error. Emit an end-of-file token.
      //   Anything else
      //     This is a missing-whitespace-between-attributes parse error.
      //     Reconsume in the before attribute name state.
      //
      // ParseAttributes() goes on in the before attribute name state in all of
      // these cases.
      const char16_t* value = ++mPos;
      while (mPos != mEnd && *mPos != quote) {
        if (*mPos == '&') {
          return ParseDecodedAttributeValue(aAttribute, value, quote);
        }
        // Newline normalization and U+0000 need the full parser.
        if (*mPos == '\r' || !*mPos) {
          return false;
        }
        ++mPos;
#ifdef MOZ_MAY_HAVE_HTMLACCEL
        // Skip the rest of long values, like URLs or SVG path data, 16
        // characters at a time.
        if (mSimd && mPos - value == kScanAttributeValueAfter &&
            mEnd - mPos >= 16) {
          mPos += quote == '"'
                      ? htmlaccel::SkipAttributeValueDoubleQuoted(mPos, mEnd)
                      : htmlaccel::SkipAttributeValueSingleQuoted(mPos, mEnd);
        }
#endif
      }
      if (AtEnd()) {
        return false;
      }
      aAttribute.mValueOffset = Offset(value);
      aAttribute.mValueLength = mPos - value;
      ++mPos;
      return true;
    }
    // https://html.spec.whatwg.org/#attribute-value-(unquoted)-state
    //   Consume the next input character:
    //   U+0009 CHARACTER TABULATION (tab)
    //   U+000A LINE FEED (LF)
    //   U+000C FORM FEED (FF)
    //   U+0020 SPACE
    //     Switch to the before attribute name state.
    //   U+0026 AMPERSAND (&)
    //     Set the return state to the attribute value (unquoted) state. Switch
    //     to the character reference state.
    //   U+003E GREATER-THAN SIGN (>)
    //     Switch to the data state. Emit the current tag token.
    //   U+0000 NULL
    //     This is an unexpected-null-character parse error. Append a U+FFFD
    //     REPLACEMENT CHARACTER character to the current attribute's value.
    //   U+0022 QUOTATION MARK (")
    //   U+0027 APOSTROPHE (')
    //   U+003C LESS-THAN SIGN (<)
    //   U+003D EQUALS SIGN (=)
    //   U+0060 GRAVE ACCENT (`)
    //     This is an unexpected-character-in-unquoted-attribute-value parse
    //     error. Treat it as per the "anything else" entry below.
    //   EOF
    //     This is an eof-in-tag parse error. Emit an end-of-file token.
    //   Anything else
    //     Append the current input character to the current attribute's
    //     value.
    const char16_t* value = mPos;
    // Stop at the characters that end the value, start a character reference,
    // or that the tokenizer reports as errors or replaces.
    while (mPos != mEnd) {
      const char16_t c = *mPos;
      if (IsAsciiWhitespace(c) || c == '>' || c == '&' || c == '"' ||
          c == '\'' || c == '<' || c == '=' || c == '`' || !c) {
        break;
      }
      ++mPos;
    }
    if (AtEnd() || !(IsAsciiWhitespace(*mPos) || *mPos == '>')) {
      return false;
    }
    aAttribute.mValueOffset = Offset(value);
    aAttribute.mValueLength = mPos - value;
    return true;
  }

  // Tokenizes the quoted attribute value from aStart on, whose character
  // references from mPos on get decoded into mDecodedData.
  bool ParseDecodedAttributeValue(AttributeToken& aAttribute,
                                  const char16_t* aStart, char16_t aQuote) {
    aAttribute.mDecoded = true;
    aAttribute.mValueOffset = mDecodedData.Length();
    if (!mDecodedData.Append(aStart, mPos - aStart, fallible)) {
      return false;
    }
    while (true) {
      const char16_t* segment = mPos;
      while (mPos != mEnd && *mPos != aQuote && *mPos != '&' && *mPos != '\r' &&
             *mPos) {
        ++mPos;
      }
      if (!mDecodedData.Append(segment, mPos - segment, fallible)) {
        return false;
      }
      if (AtEnd() || *mPos == '\r' || !*mPos) {
        return false;
      }
      if (*mPos == aQuote) {
        break;
      }
      if (!AppendCharacterReference(true)) {
        return false;
      }
    }
    aAttribute.mValueLength = mDecodedData.Length() - aAttribute.mValueOffset;
    ++mPos;
    return true;
  }

  static nsresult Append(nsIContent* aParent, nsIContent* aChild) {
    ErrorResult rv;
    aParent->AppendChildTo(aChild, false, rv);
    return rv.StealNSResult();
  }

  nsresult AppendText(nsIContent* aParent, const char16_t* aText,
                      uint32_t aLength) {
    RefPtr<nsTextNode> text =
        new (mNodeInfoManager) nsTextNode(mNodeInfoManager);
    MOZ_TRY(text->SetText(aText, aLength, false));
    return Append(aParent, text);
  }

  // https://html.spec.whatwg.org/#parsing-main-inbody
  //   A comment token
  //     Insert a comment.
  //
  // https://html.spec.whatwg.org/#insert-a-comment
  //   1. If the parser's parser sanitizer configuration is non-null and the
  //      parser's parser sanitizer configuration does not allow comments,
  //      then return.
  //   2. Let data be the data given in the comment token being processed.
  //   3. Let (targetParent, referenceChild) be the adjusted insertion location
  //      given insertionLocation.
  //   4. Let comment be the result of creating a comment node given
  //      targetParent's node document and data.
  //   5. Insert comment into targetParent before referenceChild.
  //
  // The fast path is only used without a sanitizer configuration.
  nsresult AppendComment(nsIContent* aParent, const char16_t* aData,
                         uint32_t aLength) {
    RefPtr<Comment> comment = new (mNodeInfoManager) Comment(mNodeInfoManager);
    MOZ_TRY(comment->SetText(aData, aLength, false));
    return Append(aParent, comment);
  }

  already_AddRefed<Element> CreateElement(const Token& aToken) {
    RefPtr<NodeInfo>& nodeInfo = mNodeInfos[aToken.mTagIndex];
    if (!nodeInfo) {
      nodeInfo = mNodeInfoManager->GetNodeInfo(kTags[aToken.mTagIndex].mName,
                                               nullptr, kNameSpaceID_XHTML,
                                               nsINode::ELEMENT_NODE);
    }
    RefPtr<Element> element =
        mTables.mElementNames[aToken.mTagIndex]->getHtmlCreator()(
            do_AddRef(nodeInfo), FROM_PARSER_FRAGMENT);
    if (aToken.mAttributeCount) {
      SetAttributes(element, aToken);
    }
    return element.forget();
  }

  // Sets the attributes the way
  // nsHtml5TreeOperation::SetHTMLElementAttributesFast() does.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void SetAttributes(Element* aElement,
                                                 const Token& aToken) {
    aElement->ReserveAttributeCount(aToken.mAttributeCount);
    // Element::SetNoNameSpaceAttrOnNewlyCreatedElement() may call AfterSetAttr
    // and its callers assume that the script is blocked.
    const nsAutoScriptBlocker scriptBlocker;
    bool isPendingMappedAttributeEvaluation = false;
    for (const AttributeToken& attribute :
         Span(mAttributes.begin() + aToken.mOffset, aToken.mAttributeCount)) {
      nsHtml5String value = MakeValue(attribute);
      aElement->SetNoNameSpaceAttrOnNewlyCreatedElement(
          MakeName(attribute), value, isPendingMappedAttributeEvaluation,
          scriptBlocker);
      value.Release();
    }
  }

  already_AddRefed<nsAtom> MakeName(const AttributeToken& aAttribute) {
    if (aAttribute.mKnownName) {
      return do_AddRef(
          aAttribute.mKnownName->getLocal(nsHtml5AttributeName::HTML));
    }
    char16_t name[kMaxAttributeNameLength];
    const char16_t* source = mStart + aAttribute.mNameOffset;
    for (uint32_t i = 0; i < aAttribute.mNameLength; ++i) {
      name[i] = ToLower(source[i]);
    }
    return NS_AtomizeMainThread(
        nsDependentSubstring(name, aAttribute.mNameLength));
  }

  // Whether the unknown name of aAttribute is the lowercase aName.
  bool UnknownNameEquals(const AttributeToken& aAttribute,
                         const char16_t* aName, uint32_t aLength) const {
    if (aAttribute.mNameLength != aLength) {
      return false;
    }
    const char16_t* source = mStart + aAttribute.mNameOffset;
    for (uint32_t i = 0; i < aLength; ++i) {
      if (ToLower(source[i]) != aName[i]) {
        return false;
      }
    }
    return true;
  }

  // Represents the value the way nsHtml5Tokenizer does, which
  // Element::SetNoNameSpaceAttrOnNewlyCreatedElement() relies on.
  nsHtml5String MakeValue(const AttributeToken& aAttribute) {
    char16_t* value = const_cast<char16_t*>(
        Chars(aAttribute.mDecoded, aAttribute.mValueOffset));
    const int32_t length = int32_t(aAttribute.mValueLength);
    if (nsHtml5String digit =
            nsHtml5Portability::newSingleDigitAttributeValue(value, length)) {
      return digit;
    }
    if (!length) {
      return nsHtml5String::EmptyString();
    }
    if (aAttribute.mUseAtom) {
      return nsHtml5String::FromAtom(
          NS_AtomizeMainThread(nsDependentSubstring(value, value + length)));
    }
    return nsHtml5String::FromBuffer(value, length, nullptr);
  }

  const char16_t* const mStart;
  const char16_t* mPos;
  const char16_t* const mEnd;
  nsNodeInfoManager* const mNodeInfoManager;
  const TagTables& mTables = Tables();
#ifdef MOZ_MAY_HAVE_HTMLACCEL
  const bool mSimd = htmlaccel::htmlaccelEnabled();
#endif
  uint32_t mDepth = 0;
  bool mInsideA = false;
  bool mInsideButton = false;
  bool mInsideLi = false;
  // The offsets the last Scan() recorded, up to mScanned.
  uint32_t mOffsets[kScanChunk];
  const uint32_t* mNextOffset = mOffsets;
  const uint32_t* mOffsetsEnd = mOffsets;
  const char16_t* mScanned = mStart;
  Vector<Token, 64> mTokens;
  Vector<AttributeToken, 32> mAttributes;
  // The node infos of the elements created so far, by index into kTags.
  RefPtr<NodeInfo> mNodeInfos[kTagCount];
  // Text and attribute values with character references, decoded.
  nsAutoString mDecodedData;
};

uint32_t sSuccessCount = 0;

}  // namespace

bool nsHtml5FragmentFastPath::TryParse(const nsAString& aSource,
                                       nsIContent* aTarget, Element* aContext,
                                       ErrorResult& aError) {
  MOZ_ASSERT(!aTarget->HasChildren());
  // The context element isn't on the parser's stack of open elements, so it
  // doesn't restrict what the markup may contain. The supported ones leave
  // the tokenizer in the data state, and the insertion mode in "in body", or
  // in "in template", which switches to "in body" for every supported tag.
  //
  // https://html.spec.whatwg.org/#html-fragment-parsing-algorithm
  //   6. If contextDocument is in quirks mode, then set document's mode to
  //      "quirks".
  //   ...
  //   10. Set the state of the HTML parser's tokenization stage as follows,
  //       switching on context:
  //       title
  //       textarea
  //         Switch the tokenizer to the RCDATA state.
  //       style
  //       xmp
  //       iframe
  //       noembed
  //       noframes
  //         Switch the tokenizer to the RAWTEXT state.
  //       script
  //         Switch the tokenizer to the script data state.
  //       noscript
  //         If scripting mode is not Disabled, switch the tokenizer to the
  //         RAWTEXT state. Otherwise, leave the tokenizer in the data state.
  //       plaintext
  //         Switch the tokenizer to the PLAINTEXT state.
  //       Any other element
  //         Leave the tokenizer in the data state.
  //   11. Let root be the result of creating an element given document,
  //       "html", the HTML namespace, null, null, false, and the result of
  //       looking up a custom element registry given target.
  //   12. Append root to document.
  //   13. Set up the HTML parser's stack of open elements so that it contains
  //       just the single element root.
  //   ...
  //   16. If context is a template element, then push "in template" onto the
  //       stack of template insertion modes so that it is the new current
  //       template insertion mode.
  //   ...
  //   18. Reset the parser's insertion mode appropriately.
  //   19. Set the HTML parser's form element pointer to the nearest node to
  //       context that is a form element (going straight up the ancestor
  //       chain, and including the element itself, if it is a form element),
  //       if any. (If there is no such form element, the form element pointer
  //       keeps its initial value, null.)
  //
  // https://html.spec.whatwg.org/#reset-the-insertion-mode-appropriately
  //   ...
  //   3. Loop: If node is the first node in the stack of open elements, then
  //      set last to true, and, if the parser's fragment context element is
  //      non-null, then set node to that element (fragment case).
  //   ...
  //   10. If node is a template element, then switch the insertion mode to
  //       the current template insertion mode and return.
  //   ...
  //   15. If last is true, then switch the insertion mode to "in body" and
  //       return. (fragment case)
  //
  // The stack of open elements holds only root, so node is the context
  // element. Steps 4 to 14 look for elements that aren't supported as context
  // elements, except for step 10.
  //
  // https://html.spec.whatwg.org/#parsing-main-intemplate
  //   A character token
  //   A comment token
  //   A processing instruction token
  //   A DOCTYPE token
  //     Process the token using the rules for the "in body" insertion mode.
  //   ...
  //   Any other start tag
  //     Pop the current template insertion mode off the stack of template
  //     insertion modes.
  //     Push "in body" onto the stack of template insertion modes so that it
  //     is the new current template insertion mode.
  //     Switch the insertion mode to "in body", and reprocess the token.
  //
  // The only rule of the "in body" insertion mode that depends on quirks mode
  // is the one for <table> start tags. The form element pointer only affects
  // <form> start tags and creating form-associated elements. The full parser
  // associates those with it when it is in the same tree as their parent,
  // which makes it an ancestor of the target, the context element. Inserting
  // the elements gives them the same form owner.
  if (!aContext->IsHTMLElement()) {
    return false;
  }
  nsAtom* contextName = aContext->NodeInfo()->NameAtom();
  if (contextName != nsGkAtoms::_template) {
    const uint8_t index = LookupTagIndex(
        Tables(), contextName->GetUTF16String(), contextName->GetLength());
    if (index == kNoTag || kTags[index].mVoid) {
      return false;
    }
  }
  // nsContentUtils::ParseFragmentHTML() fails when it is reentered, and
  // nsHtml5StringParser::ParseFragment() without a document URI.
  if (nsContentUtils::IsFragmentParsingActive() ||
      !aTarget->OwnerDoc()->GetDocumentURI()) {
    return false;
  }
  Parser parser(aSource, aTarget->NodeInfoManager());
  if (!parser.Tokenize()) {
    return false;
  }
  aError = parser.Build(aTarget);
  if (!aError.Failed()) {
    ++sSuccessCount;
  }
  return true;
}

bool nsHtml5FragmentFastPath::TryParseDocument(const nsAString& aSource,
                                               Document* aDocument,
                                               ErrorResult& aError) {
  MOZ_ASSERT(!aDocument->HasChildren());
  // The insertion modes before "in body" drop whitespace, also from character
  // references, and don't put comments, doctypes or end tags in the body, so
  // the content has to start with a start tag or with text other than a
  // character reference after the leading whitespace. Without a doctype, the
  // document is in quirks mode, which doesn't change how the supported tags
  // nest.
  //
  // https://html.spec.whatwg.org/#parse-html-from-a-string
  //   1. Set document's type to "html".
  //   2. Let parser be a new HTML parser whose allow declarative shadow roots
  //      is document's allow declarative shadow roots and parser sanitizer
  //      configuration is configuration, associated with document.
  //   3. Place html into the input stream for parser. The encoding confidence
  //      is irrelevant.
  //   4. Start parser and let it run until it has consumed all the characters
  //      just inserted into the input stream.
  //
  // https://html.spec.whatwg.org/#the-initial-insertion-mode
  //   A character token that is one of U+0009 CHARACTER TABULATION, U+000A
  //   LINE FEED (LF), U+000C FORM FEED (FF), U+000D CARRIAGE RETURN (CR), or
  //   U+0020 SPACE
  //     Ignore the token.
  //   A comment token
  //     Insert a comment given (the Document object, null).
  //   ...
  //   Anything else
  //     If the document is not an iframe srcdoc document, then this is a
  //     parse error; if the parser cannot change the mode flag is false, set
  //     the Document to quirks mode.
  //     In any case, switch the insertion mode to "before html", then
  //     reprocess the token.
  //
  // https://html.spec.whatwg.org/#the-before-html-insertion-mode
  //   ...
  //   Anything else
  //     Create an html element whose node document is the Document object.
  //     Insert an element at the adjusted insertion location with that
  //     element and (the Document, null). Put this element in the stack of
  //     open elements.
  //     Switch the insertion mode to "before head", then reprocess the token.
  //
  // https://html.spec.whatwg.org/#the-before-head-insertion-mode
  //   ...
  //   Anything else
  //     Insert an HTML element for a "head" start tag token with no
  //     attributes.
  //     Set the head element pointer to the newly created head element.
  //     Switch the insertion mode to "in head".
  //     Reprocess the current token.
  //
  // https://html.spec.whatwg.org/#parsing-main-inhead
  //   ...
  //   Anything else
  //     Pop the current node (which will be the head element) off the stack
  //     of open elements.
  //     Switch the insertion mode to "after head".
  //     Reprocess the token.
  //
  // https://html.spec.whatwg.org/#the-after-head-insertion-mode
  //   ...
  //   Anything else
  //     Insert an HTML element for a "body" start tag token with no
  //     attributes.
  //     Set the frameset-ok flag to "ok".
  //     Switch the insertion mode to "in body".
  //     Reprocess the current token.
  //
  // DOMParser documents aren't iframe srcdoc documents, and their parser can
  // change the mode. None of the supported tags has a rule of its own in these
  // insertion modes, so the first start tag or non-whitespace character
  // creates <html>, <head> and <body> and goes to "in body", like the end of
  // the input does for input that is only whitespace.
  const char16_t* start = aSource.BeginReading();
  const char16_t* const end = aSource.EndReading();
  while (start != end && IsAsciiWhitespace(*start)) {
    ++start;
  }
  if (start != end && (*start == '&' || (*start == '<' && end - start >= 2 &&
                                         (start[1] == '!' || start[1] == '?' ||
                                          start[1] == '/')))) {
    return false;
  }
  // nsContentUtils::ParseDocumentHTML() fails when it is reentered.
  if (nsContentUtils::IsFragmentParsingActive()) {
    return false;
  }
  nsNodeInfoManager* nodeInfoManager = aDocument->NodeInfoManager();
  Parser parser(Substring(start, end), nodeInfoManager);
  if (!parser.Tokenize()) {
    return false;
  }

  aDocument->SetCompatibilityMode(eCompatibility_NavQuirks);
  // Like nsHtml5OplessBuilder, which notifies that the document element was
  // created once the update ends.
  mozAutoDocUpdate update(aDocument, true);
  auto create = [&](nsHtml5ElementName* aName) -> RefPtr<Element> {
    return aName->getHtmlCreator()(
        nodeInfoManager->GetNodeInfo(aName->getName(), nullptr,
                                     kNameSpaceID_XHTML, nsINode::ELEMENT_NODE),
        FROM_PARSER_FRAGMENT);
  };
  const RefPtr<Element> html = create(nsHtml5ElementName::ELT_HTML);
  const RefPtr<Element> head = create(nsHtml5ElementName::ELT_HEAD);
  const RefPtr<Element> body = create(nsHtml5ElementName::ELT_BODY);
  aDocument->SuspendDOMNotifications();
  aDocument->AppendChildTo(html, false, aError);
  if (!aError.Failed()) {
    nsContentUtils::AddScriptRunner(
        MakeAndAddRef<nsDocElementCreatedNotificationRunner>(aDocument));
    html->AppendChildTo(head, false, aError);
  }
  if (!aError.Failed()) {
    html->AppendChildTo(body, false, aError);
  }
  if (!aError.Failed()) {
    aError = parser.Build(body);
  }
  aDocument->ResumeDOMNotifications();
  if (html->GetParentNode()) {
    MutationObservers::NotifyContentInserted(
        aDocument, html, {MutationEffectOnScript::KeepTrustWorthiness});
  }
  if (!aError.Failed()) {
    ++sSuccessCount;
  }
  return true;
}

uint32_t nsHtml5FragmentFastPath::SuccessCount() { return sSuccessCount; }

namespace {

namespace metrics = glean::dom_fragment_parsing;

// The index of the size label of aLength code units.
size_t SizeIndex(uint32_t aLength) {
  return aLength < 1000 ? 0 : aLength < 10000 ? 1 : aLength < 100000 ? 2 : 3;
}

// By outcome (fast, fallback, full), then by SizeIndex().
constexpr metrics::InnerHtmlLabel kInnerHtmlLabels[3][4] = {
    {metrics::InnerHtmlLabel::eFastUnder1k,
     metrics::InnerHtmlLabel::eFastUnder10k,
     metrics::InnerHtmlLabel::eFastUnder100k,
     metrics::InnerHtmlLabel::eFastOver100k},
    {metrics::InnerHtmlLabel::eFallbackUnder1k,
     metrics::InnerHtmlLabel::eFallbackUnder10k,
     metrics::InnerHtmlLabel::eFallbackUnder100k,
     metrics::InnerHtmlLabel::eFallbackOver100k},
    {metrics::InnerHtmlLabel::eFullUnder1k,
     metrics::InnerHtmlLabel::eFullUnder10k,
     metrics::InnerHtmlLabel::eFullUnder100k,
     metrics::InnerHtmlLabel::eFullOver100k},
};

constexpr metrics::DomParserLabel kDomParserLabels[3][4] = {
    {metrics::DomParserLabel::eFastUnder1k,
     metrics::DomParserLabel::eFastUnder10k,
     metrics::DomParserLabel::eFastUnder100k,
     metrics::DomParserLabel::eFastOver100k},
    {metrics::DomParserLabel::eFallbackUnder1k,
     metrics::DomParserLabel::eFallbackUnder10k,
     metrics::DomParserLabel::eFallbackUnder100k,
     metrics::DomParserLabel::eFallbackOver100k},
    {metrics::DomParserLabel::eFullUnder1k,
     metrics::DomParserLabel::eFullUnder10k,
     metrics::DomParserLabel::eFullUnder100k,
     metrics::DomParserLabel::eFullOver100k},
};

constexpr metrics::InnerHtmlFastPathRejectedLabel kInnerHtmlRejectedLabels[4] =
    {
        metrics::InnerHtmlFastPathRejectedLabel::eUnder1k,
        metrics::InnerHtmlFastPathRejectedLabel::eUnder10k,
        metrics::InnerHtmlFastPathRejectedLabel::eUnder100k,
        metrics::InnerHtmlFastPathRejectedLabel::eOver100k,
};

constexpr metrics::DomParserFastPathRejectedLabel kDomParserRejectedLabels[4] =
    {
        metrics::DomParserFastPathRejectedLabel::eUnder1k,
        metrics::DomParserFastPathRejectedLabel::eUnder10k,
        metrics::DomParserFastPathRejectedLabel::eUnder100k,
        metrics::DomParserFastPathRejectedLabel::eOver100k,
};

// Whether to time this call. A pseudo-random sample, rather than every nth
// call, so that it doesn't line up with calls that alternate.
bool ShouldSample() {
  MOZ_ASSERT(NS_IsMainThread());
  // xorshift32, seeded per process so that processes don't all sample the
  // calls at the same positions.
  static uint32_t sState = [] {
    const uint32_t seed = uint32_t(RandomUint64OrDie());
    return seed ? seed : 2463534242;
  }();
  sState ^= sState << 13;
  sState ^= sState >> 17;
  sState ^= sState << 5;
  const uint32_t shift = std::min(
      StaticPrefs::dom_html_fragment_fast_path_telemetry_sample_shift(), 31u);
  return !(sState & ((1u << shift) - 1));
}

}  // namespace

nsHtml5FragmentFastPath::ParseTimer::ParseTimer(Caller aCaller,
                                                uint32_t aLength)
    : mLength(aLength), mCaller(aCaller), mSampled(ShouldSample()) {
  if (mSampled) {
    mStart = TimeStamp::Now();
  }
}

void nsHtml5FragmentFastPath::ParseTimer::FastPathTried(bool aParsed) {
  if (!mSampled) {
    return;
  }
  if (aParsed) {
    mOutcome = Outcome::Fast;
    return;
  }
  mOutcome = Outcome::Fallback;
  const TimeDuration elapsed = TimeStamp::Now() - mStart;
  const size_t size = SizeIndex(mLength);
  if (mCaller == Caller::InnerHTML) {
    metrics::inner_html_fast_path_rejected
        .EnumGet(kInnerHtmlRejectedLabels[size])
        .AccumulateRawDuration(elapsed);
  } else {
    metrics::dom_parser_fast_path_rejected
        .EnumGet(kDomParserRejectedLabels[size])
        .AccumulateRawDuration(elapsed);
  }
}

void nsHtml5FragmentFastPath::ParseTimer::Finish() {
  if (!mSampled) {
    return;
  }
  const TimeDuration elapsed = TimeStamp::Now() - mStart;
  const size_t outcome = size_t(mOutcome);
  const size_t size = SizeIndex(mLength);
  if (mCaller == Caller::InnerHTML) {
    metrics::inner_html.EnumGet(kInnerHtmlLabels[outcome][size])
        .AccumulateRawDuration(elapsed);
  } else {
    metrics::dom_parser.EnumGet(kDomParserLabels[outcome][size])
        .AccumulateRawDuration(elapsed);
  }
}
