/*
 * Copyright (c) 2005-2007 Henri Sivonen
 * Copyright (c) 2007-2017 Mozilla Foundation
 * Portions of comments Copyright 2004-2010 Apple Computer, Inc., Mozilla
 * Foundation, and Opera Software ASA.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * THIS IS A GENERATED FILE. PLEASE DO NOT EDIT.
 * Please edit Tokenizer.java instead and regenerate.
 */

#define nsHtml5Tokenizer_cpp__

#include "nsHtml5AttributeName.h"
#include "nsHtml5ElementName.h"
#include "nsHtml5TreeBuilder.h"
#include "nsHtml5StackNode.h"
#include "nsHtml5UTF16Buffer.h"
#include "nsHtml5StateSnapshot.h"
#include "nsHtml5Portability.h"

#include "nsHtml5Tokenizer.h"

char16_t nsHtml5Tokenizer::LT_GT[] = {'<', '>'};
char16_t nsHtml5Tokenizer::XML_STYLESHEET[] = {
    'x', 'm', 'l', '-', 's', 't', 'y', 'l', 'e', 's', 'h', 'e', 'e', 't'};
char16_t nsHtml5Tokenizer::LT_SOLIDUS[] = {'<', '/'};
char16_t nsHtml5Tokenizer::RSQB_RSQB[] = {']', ']'};
char16_t nsHtml5Tokenizer::REPLACEMENT_CHARACTER[] = {0xfffd};
char16_t nsHtml5Tokenizer::LF[] = {'\n'};
char16_t nsHtml5Tokenizer::CDATA_LSQB[] = {'C', 'D', 'A', 'T', 'A', '['};
char16_t nsHtml5Tokenizer::OCTYPE[] = {'o', 'c', 't', 'y', 'p', 'e'};
char16_t nsHtml5Tokenizer::UBLIC[] = {'u', 'b', 'l', 'i', 'c'};
char16_t nsHtml5Tokenizer::YSTEM[] = {'y', 's', 't', 'e', 'm'};
static char16_t const TITLE_ARR_DATA[] = {'t', 'i', 't', 'l', 'e'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::TITLE_ARR = {
    TITLE_ARR_DATA, std::size(TITLE_ARR_DATA)};
static char16_t const SCRIPT_ARR_DATA[] = {'s', 'c', 'r', 'i', 'p', 't'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::SCRIPT_ARR = {
    SCRIPT_ARR_DATA, std::size(SCRIPT_ARR_DATA)};
static char16_t const STYLE_ARR_DATA[] = {'s', 't', 'y', 'l', 'e'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::STYLE_ARR = {
    STYLE_ARR_DATA, std::size(STYLE_ARR_DATA)};
static char16_t const PLAINTEXT_ARR_DATA[] = {'p', 'l', 'a', 'i', 'n',
                                              't', 'e', 'x', 't'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::PLAINTEXT_ARR = {
    PLAINTEXT_ARR_DATA, std::size(PLAINTEXT_ARR_DATA)};
static char16_t const XMP_ARR_DATA[] = {'x', 'm', 'p'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::XMP_ARR = {
    XMP_ARR_DATA, std::size(XMP_ARR_DATA)};
static char16_t const TEXTAREA_ARR_DATA[] = {'t', 'e', 'x', 't',
                                             'a', 'r', 'e', 'a'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::TEXTAREA_ARR = {
    TEXTAREA_ARR_DATA, std::size(TEXTAREA_ARR_DATA)};
static char16_t const IFRAME_ARR_DATA[] = {'i', 'f', 'r', 'a', 'm', 'e'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::IFRAME_ARR = {
    IFRAME_ARR_DATA, std::size(IFRAME_ARR_DATA)};
static char16_t const NOEMBED_ARR_DATA[] = {'n', 'o', 'e', 'm', 'b', 'e', 'd'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::NOEMBED_ARR = {
    NOEMBED_ARR_DATA, std::size(NOEMBED_ARR_DATA)};
static char16_t const NOSCRIPT_ARR_DATA[] = {'n', 'o', 's', 'c',
                                             'r', 'i', 'p', 't'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::NOSCRIPT_ARR = {
    NOSCRIPT_ARR_DATA, std::size(NOSCRIPT_ARR_DATA)};
static char16_t const NOFRAMES_ARR_DATA[] = {'n', 'o', 'f', 'r',
                                             'a', 'm', 'e', 's'};
staticJArray<char16_t, int32_t> nsHtml5Tokenizer::NOFRAMES_ARR = {
    NOFRAMES_ARR_DATA, std::size(NOFRAMES_ARR_DATA)};

nsHtml5Tokenizer::nsHtml5Tokenizer(nsHtml5TreeBuilder* tokenHandler,
                                   bool viewingXmlSource)
    : tokenHandler(tokenHandler),
      encodingDeclarationHandler(nullptr),
      lastCR(false),
      stateSave(0),
      returnStateSave(0),
      index(0),
      forceQuirks(false),
      additional('\0'),
      entCol(0),
      firstCharKey(0),
      lo(0),
      hi(0),
      candidate(0),
      charRefBufMark(0),
      value(0),
      seenDigits(false),
      suspendAfterCurrentNonTextToken(false),
      cstart(0),
      strBufLen(0),
      charRefBuf(jArray<char16_t, int32_t>::newJArray(32)),
      charRefBufLen(0),
      bmpChar(jArray<char16_t, int32_t>::newJArray(1)),
      astralChar(jArray<char16_t, int32_t>::newJArray(2)),
      endTagExpectation(nullptr),
      endTagExpectationAsArray(nullptr),
      endTag(false),
      containsHyphen(false),
      tagName(nullptr),
      nonInternedTagName(new nsHtml5ElementName()),
      attributeName(nullptr),
      nonInternedAttributeName(new nsHtml5AttributeName()),
      doctypeName(nullptr),
      publicIdentifier(nullptr),
      systemIdentifier(nullptr),
      piTarget(nullptr),
      processingInstructionsEnabled(false),
      attributes(tokenHandler->HasBuilder() ? new nsHtml5HtmlAttributes(0)
                                            : nullptr),
      newAttributesEachTime(!tokenHandler->HasBuilder()),
      shouldSuspend(false),
      keepBuffer(false),
      confident(false),
      line(0),
      attributeLine(0),
      interner(nullptr),
      viewingXmlSource(viewingXmlSource) {
  MOZ_COUNT_CTOR(nsHtml5Tokenizer);
}

void nsHtml5Tokenizer::setInterner(nsHtml5AtomTable* interner) {
  this->interner = interner;
}

void nsHtml5Tokenizer::setProcessingInstructionsEnabled(
    bool processingInstructionsEnabled) {
  this->processingInstructionsEnabled = processingInstructionsEnabled;
}

void nsHtml5Tokenizer::initLocation(nsHtml5String newPublicId,
                                    nsHtml5String newSystemId) {
  this->systemId = newSystemId;
  this->publicId = newPublicId;
}

bool nsHtml5Tokenizer::isViewingXmlSource() { return viewingXmlSource; }

void nsHtml5Tokenizer::setKeepBuffer(bool keepBuffer) {
  this->keepBuffer = keepBuffer;
}

bool nsHtml5Tokenizer::dropBufferIfLongerThan(int32_t length) {
  if (strBuf.length > length) {
    strBuf = nullptr;
    return true;
  }
  return false;
}

void nsHtml5Tokenizer::setState(int32_t specialTokenizerState) {
  this->stateSave = specialTokenizerState;
  this->endTagExpectation = nullptr;
  this->endTagExpectationAsArray = nullptr;
}

void nsHtml5Tokenizer::setStateAndEndTagExpectation(
    int32_t specialTokenizerState, nsHtml5ElementName* endTagExpectation) {
  this->stateSave = specialTokenizerState;
  this->endTagExpectation = endTagExpectation;
  endTagExpectationToArray();
}

void nsHtml5Tokenizer::endTagExpectationToArray() {
  switch (endTagExpectation->getGroup()) {
    case nsHtml5TreeBuilder::TITLE: {
      endTagExpectationAsArray = TITLE_ARR;
      return;
    }
    case nsHtml5TreeBuilder::SCRIPT: {
      endTagExpectationAsArray = SCRIPT_ARR;
      return;
    }
    case nsHtml5TreeBuilder::STYLE: {
      endTagExpectationAsArray = STYLE_ARR;
      return;
    }
    case nsHtml5TreeBuilder::PLAINTEXT: {
      endTagExpectationAsArray = PLAINTEXT_ARR;
      return;
    }
    case nsHtml5TreeBuilder::XMP: {
      endTagExpectationAsArray = XMP_ARR;
      return;
    }
    case nsHtml5TreeBuilder::TEXTAREA: {
      endTagExpectationAsArray = TEXTAREA_ARR;
      return;
    }
    case nsHtml5TreeBuilder::IFRAME: {
      endTagExpectationAsArray = IFRAME_ARR;
      return;
    }
    case nsHtml5TreeBuilder::NOEMBED: {
      endTagExpectationAsArray = NOEMBED_ARR;
      return;
    }
    case nsHtml5TreeBuilder::NOSCRIPT: {
      endTagExpectationAsArray = NOSCRIPT_ARR;
      return;
    }
    case nsHtml5TreeBuilder::NOFRAMES: {
      endTagExpectationAsArray = NOFRAMES_ARR;
      return;
    }
    default: {
      MOZ_ASSERT(false, "Bad end tag expectation.");
      return;
    }
  }
}

void nsHtml5Tokenizer::setLineNumber(int32_t line) {
  this->attributeLine = line;
  this->line = line;
}

void nsHtml5Tokenizer::appendCharRefBuf(char16_t c) {
  MOZ_RELEASE_ASSERT(charRefBufLen < charRefBuf.length,
                     "Attempted to overrun charRefBuf!");
  charRefBuf[charRefBufLen++] = c;
}

void nsHtml5Tokenizer::emitOrAppendCharRefBuf(int32_t returnState) {
  if ((returnState & DATA_AND_RCDATA_MASK)) {
    appendCharRefBufToStrBuf();
  } else {
    if (charRefBufLen > 0) {
      tokenHandler->characters(charRefBuf, 0, charRefBufLen);
      charRefBufLen = 0;
    }
  }
}

void nsHtml5Tokenizer::emitComment(int32_t provisionalHyphens, int32_t pos) {
  RememberGt(pos);
  tokenHandler->comment(strBuf, 0, strBufLen - provisionalHyphens);
  clearStrBufAfterUse();
  cstart = pos + 1;
  suspendIfRequestedAfterCurrentNonTextToken();
}

bool nsHtml5Tokenizer::isDisallowedPiTarget() {
  int32_t targetLength = strBufLen - 1;
  if (targetLength != 3 && targetLength != 14) {
    return false;
  }
  for (int32_t i = 0; i < targetLength; i++) {
    char16_t folded = strBuf[i + 1];
    if (folded >= 'A' && folded <= 'Z') {
      folded += 0x20;
    }
    if (folded != XML_STYLESHEET[i]) {
      return false;
    }
  }
  return true;
}

void nsHtml5Tokenizer::finishPiTarget() {
  piTarget = nsHtml5Portability::newStringFromBuffer(strBuf, 1, strBufLen - 1,
                                                     tokenHandler, nullptr);
  clearStrBufAfterUse();
}

void nsHtml5Tokenizer::emitProcessingInstruction(int32_t pos) {
  RememberGt(pos);
  tokenHandler->processingInstruction(piTarget, strBuf, 0, strBufLen);
  clearStrBufAfterUse();
  piTarget.Release();
  piTarget = nullptr;
  cstart = pos + 1;
  suspendIfRequestedAfterCurrentNonTextToken();
}

void nsHtml5Tokenizer::flushChars(char16_t* buf, int32_t pos) {
  if (pos > cstart) {
    tokenHandler->characters(buf, cstart, pos - cstart);
  }
  cstart = INT32_MAX;
}

void nsHtml5Tokenizer::strBufToElementNameString() {
  if (containsHyphen) {
    nsAtom* annotationName = nsHtml5ElementName::ELT_ANNOTATION_XML->getName();
    if (nsHtml5Portability::localEqualsBuffer(annotationName, strBuf,
                                              strBufLen)) {
      tagName = nsHtml5ElementName::ELT_ANNOTATION_XML;
    } else {
      nonInternedTagName->setNameForNonInterned(
          nsHtml5Portability::newLocalNameFromBuffer(strBuf, strBufLen,
                                                     interner),
          true);
      tagName = nonInternedTagName;
    }
  } else {
    tagName = nsHtml5ElementName::elementNameByBuffer(strBuf, strBufLen);
    if (!tagName) {
      nonInternedTagName->setNameForNonInterned(
          nsHtml5Portability::newLocalNameFromBuffer(strBuf, strBufLen,
                                                     interner),
          false);
      tagName = nonInternedTagName;
    }
  }
  containsHyphen = false;
  clearStrBufAfterUse();
}

int32_t nsHtml5Tokenizer::emitCurrentTagToken(bool selfClosing, int32_t pos) {
  RememberGt(pos);
  cstart = pos + 1;
  maybeErrSlashInEndTag(selfClosing);
  stateSave = nsHtml5Tokenizer::DATA;
  nsHtml5HtmlAttributes* attrs =
      (!attributes ? nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES : attributes);
  if (endTag) {
    maybeErrAttributesOnEndTag(attrs);
    if (!viewingXmlSource) {
      tokenHandler->endTag(tagName);
    }
    if (newAttributesEachTime) {
      delete attributes;
      attributes = nullptr;
    }
  } else {
    if (viewingXmlSource) {
      MOZ_ASSERT(newAttributesEachTime);
      delete attributes;
      attributes = nullptr;
    } else {
      tokenHandler->startTag(tagName, attrs, selfClosing);
    }
  }
  tagName = nullptr;
  if (newAttributesEachTime) {
    attributes = nullptr;
  } else {
    attributes->clear(0);
  }
  suspendIfRequestedAfterCurrentNonTextToken();
  return stateSave;
}

void nsHtml5Tokenizer::attributeNameComplete() {
  attributeName =
      nsHtml5AttributeName::nameByBuffer(strBuf, strBufLen, interner);
  if (!attributeName) {
    nonInternedAttributeName->setNameForNonInterned(
        nsHtml5Portability::newLocalNameFromBuffer(strBuf, strBufLen,
                                                   interner));
    attributeName = nonInternedAttributeName;
  }
  clearStrBufAfterUse();
  if (!attributes) {
    attributes = new nsHtml5HtmlAttributes(0);
  }
  if (attributes->contains(attributeName)) {
    errDuplicateAttribute();
    attributeName = nullptr;
  }
}

void nsHtml5Tokenizer::addAttributeWithoutValue() {
  if (attributeName) {
    attributes->addAttribute(
        attributeName, nsHtml5Portability::newEmptyString(), attributeLine);
    attributeName = nullptr;
  } else {
    clearStrBufAfterUse();
  }
}

void nsHtml5Tokenizer::addAttributeWithValue() {
  if (attributeName) {
    nsHtml5String val = strBufToAttributeValueString();
    if (mViewSource) {
      mViewSource->MaybeLinkifyAttributeValue(attributeName, val);
    }
    attributes->addAttribute(attributeName, val, attributeLine);
    attributeName = nullptr;
  } else {
    clearStrBufAfterUse();
  }
}

void nsHtml5Tokenizer::start() {
  initializeWithoutStarting();
  tokenHandler->startTokenization(this);
  if (mViewSource) {
    line = 1;
    col = -1;
    nextCharOnNewLine = false;
  } else if (tokenHandler->WantsLineAndColumn()) {
    line = 0;
    col = 1;
    nextCharOnNewLine = true;
  } else {
    line = -1;
    col = -1;
    nextCharOnNewLine = false;
  }
}

bool nsHtml5Tokenizer::tokenizeBuffer(nsHtml5UTF16Buffer* buffer) {
  int32_t state = stateSave;
  int32_t returnState = returnStateSave;
  char16_t c = '\0';
  shouldSuspend = false;
  lastCR = false;
  int32_t start = buffer->getStart();
  int32_t end = buffer->getEnd();
  int32_t pos = start - 1;
  switch (state) {
    case DATA:
    case RCDATA:
    case SCRIPT_DATA:
    case PLAINTEXT:
    case RAWTEXT:
    case CDATA_SECTION:
    case SCRIPT_DATA_ESCAPED:
    case SCRIPT_DATA_ESCAPE_START:
    case SCRIPT_DATA_ESCAPE_START_DASH:
    case SCRIPT_DATA_ESCAPED_DASH:
    case SCRIPT_DATA_ESCAPED_DASH_DASH:
    case SCRIPT_DATA_DOUBLE_ESCAPE_START:
    case SCRIPT_DATA_DOUBLE_ESCAPED:
    case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
    case SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
    case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
    case SCRIPT_DATA_DOUBLE_ESCAPE_END: {
      cstart = start;
      break;
    }
    default: {
      cstart = INT32_MAX;
      break;
    }
  }
  if (mViewSource) {
    mViewSource->SetBuffer(buffer);
    if (mozilla::htmlaccel::htmlaccelEnabled()) {
      pos = StateLoopViewSourceSIMD(state, c, pos, buffer->getBuffer(), false,
                                    returnState, buffer->getEnd());
    } else {
      pos = StateLoopViewSourceALU(state, c, pos, buffer->getBuffer(), false,
                                   returnState, buffer->getEnd());
    }
    mViewSource->DropBuffer((pos == buffer->getEnd()) ? pos : pos + 1);
  } else if (tokenHandler->WantsLineAndColumn()) {
    if (mozilla::htmlaccel::htmlaccelEnabled()) {
      pos = StateLoopLineColSIMD(state, c, pos, buffer->getBuffer(), false,
                                 returnState, buffer->getEnd());
    } else {
      pos = StateLoopLineColALU(state, c, pos, buffer->getBuffer(), false,
                                returnState, buffer->getEnd());
    }
  } else if (mozilla::htmlaccel::htmlaccelEnabled()) {
    pos = StateLoopFastestSIMD(state, c, pos, buffer->getBuffer(), false,
                               returnState, buffer->getEnd());
  } else {
    pos = StateLoopFastestALU(state, c, pos, buffer->getBuffer(), false,
                              returnState, buffer->getEnd());
  }
  if (pos == end) {
    buffer->setStart(pos);
  } else {
    buffer->setStart(pos + 1);
  }
  return lastCR;
}

void nsHtml5Tokenizer::initDoctypeFields() {
  clearStrBufAfterUse();
  doctypeName = nullptr;
  if (systemIdentifier) {
    systemIdentifier.Release();
    systemIdentifier = nullptr;
  }
  if (publicIdentifier) {
    publicIdentifier.Release();
    publicIdentifier = nullptr;
  }
  forceQuirks = false;
}

void nsHtml5Tokenizer::emitReplacementCharacter(char16_t* buf, int32_t pos) {
  flushChars(buf, pos);
  tokenHandler->zeroOriginatingReplacementCharacter();
  cstart = pos + 1;
}

void nsHtml5Tokenizer::maybeEmitReplacementCharacter(char16_t* buf,
                                                     int32_t pos) {
  flushChars(buf, pos);
  tokenHandler->zeroOrReplacementCharacter();
  cstart = pos + 1;
}

void nsHtml5Tokenizer::emitPlaintextReplacementCharacter(char16_t* buf,
                                                         int32_t pos) {
  flushChars(buf, pos);
  tokenHandler->characters(REPLACEMENT_CHARACTER, 0, 1);
  cstart = pos + 1;
}

void nsHtml5Tokenizer::bogusDoctype() {
  errBogusDoctype();
  forceQuirks = true;
}

void nsHtml5Tokenizer::bogusDoctypeWithoutQuirks() {
  errBogusDoctype();
  forceQuirks = false;
}

void nsHtml5Tokenizer::handleNcrValue(int32_t returnState) {
  if (value <= 0xFFFF) {
    if (value >= 0x80 && value <= 0x9f) {
      errNcrInC1Range();
      char16_t* val = nsHtml5NamedCharacters::WINDOWS_1252[value - 0x80];
      emitOrAppendOne(val, returnState);
    } else if (value == 0x0) {
      errNcrZero();
      emitOrAppendOne(nsHtml5Tokenizer::REPLACEMENT_CHARACTER, returnState);
    } else if ((value & 0xF800) == 0xD800) {
      errNcrSurrogate();
      emitOrAppendOne(nsHtml5Tokenizer::REPLACEMENT_CHARACTER, returnState);
    } else {
      char16_t ch = (char16_t)value;
      bmpChar[0] = ch;
      emitOrAppendOne(bmpChar, returnState);
    }
  } else if (value <= 0x10FFFF) {
    astralChar[0] = (char16_t)(nsHtml5Tokenizer::LEAD_OFFSET + (value >> 10));
    astralChar[1] = (char16_t)(0xDC00 + (value & 0x3FF));
    emitOrAppendTwo(astralChar, returnState);
  } else {
    errNcrOutOfRange();
    emitOrAppendOne(nsHtml5Tokenizer::REPLACEMENT_CHARACTER, returnState);
  }
}

void nsHtml5Tokenizer::eof() {
  int32_t state = stateSave;
  int32_t returnState = returnStateSave;
eofloop:
  for (;;) {
    switch (state) {
      case SCRIPT_DATA_LESS_THAN_SIGN:
      case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN: {
        tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
        NS_HTML5_BREAK(eofloop);
      }
      case TAG_OPEN: {
        errEofAfterLt();
        tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
        NS_HTML5_BREAK(eofloop);
      }
      case RAWTEXT_RCDATA_LESS_THAN_SIGN: {
        tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
        NS_HTML5_BREAK(eofloop);
      }
      case NON_DATA_END_TAG_NAME: {
        tokenHandler->characters(nsHtml5Tokenizer::LT_SOLIDUS, 0, 2);
        emitStrBuf();
        NS_HTML5_BREAK(eofloop);
      }
      case CLOSE_TAG_OPEN: {
        errEofAfterLt();
        tokenHandler->characters(nsHtml5Tokenizer::LT_SOLIDUS, 0, 2);
        NS_HTML5_BREAK(eofloop);
      }
      case TAG_NAME: {
        errEofInTagName();
        NS_HTML5_BREAK(eofloop);
      }
      case BEFORE_ATTRIBUTE_NAME:
      case AFTER_ATTRIBUTE_VALUE_QUOTED:
      case SELF_CLOSING_START_TAG: {
        errEofWithoutGt();
        NS_HTML5_BREAK(eofloop);
      }
      case ATTRIBUTE_NAME: {
        errEofInAttributeName();
        NS_HTML5_BREAK(eofloop);
      }
      case AFTER_ATTRIBUTE_NAME:
      case BEFORE_ATTRIBUTE_VALUE: {
        errEofWithoutGt();
        NS_HTML5_BREAK(eofloop);
      }
      case ATTRIBUTE_VALUE_DOUBLE_QUOTED:
      case ATTRIBUTE_VALUE_SINGLE_QUOTED:
      case ATTRIBUTE_VALUE_UNQUOTED: {
        errEofInAttributeValue();
        NS_HTML5_BREAK(eofloop);
      }
      case PROCESSING_INSTRUCTION_OPEN:
      case PROCESSING_INSTRUCTION_TARGET:
      case AFTER_PROCESSING_INSTRUCTION_TARGET:
      case PROCESSING_INSTRUCTION_DATA:
      case PROCESSING_INSTRUCTION_QUESTIONABLE: {
        errEofInProcessingInstruction();
        if (piTarget) {
          piTarget.Release();
          piTarget = nullptr;
        }
        NS_HTML5_BREAK(eofloop);
      }
      case BOGUS_COMMENT: {
        emitComment(0, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case BOGUS_COMMENT_HYPHEN: {
        emitComment(0, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case MARKUP_DECLARATION_OPEN: {
        errBogusComment();
        emitComment(0, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case MARKUP_DECLARATION_HYPHEN: {
        errBogusComment();
        emitComment(0, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case MARKUP_DECLARATION_OCTYPE: {
        if (index < 6) {
          errBogusComment();
          emitComment(0, 0);
        } else {
          errEofInDoctype();
          doctypeName = nullptr;
          if (systemIdentifier) {
            systemIdentifier.Release();
            systemIdentifier = nullptr;
          }
          if (publicIdentifier) {
            publicIdentifier.Release();
            publicIdentifier = nullptr;
          }
          forceQuirks = true;
          emitDoctypeToken(0);
          NS_HTML5_BREAK(eofloop);
        }
        NS_HTML5_BREAK(eofloop);
      }
      case COMMENT_START:
      case COMMENT:
      case COMMENT_LESSTHAN:
      case COMMENT_LESSTHAN_BANG: {
        errEofInComment();
        emitComment(0, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case COMMENT_END:
      case COMMENT_LESSTHAN_BANG_DASH_DASH: {
        errEofInComment();
        emitComment(2, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case COMMENT_END_DASH:
      case COMMENT_START_DASH:
      case COMMENT_LESSTHAN_BANG_DASH: {
        errEofInComment();
        emitComment(1, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case COMMENT_END_BANG: {
        errEofInComment();
        emitComment(3, 0);
        NS_HTML5_BREAK(eofloop);
      }
      case DOCTYPE:
      case BEFORE_DOCTYPE_NAME: {
        errEofInDoctype();
        forceQuirks = true;
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case DOCTYPE_NAME: {
        errEofInDoctype();
        strBufToDoctypeName();
        forceQuirks = true;
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case DOCTYPE_UBLIC:
      case DOCTYPE_YSTEM:
      case AFTER_DOCTYPE_NAME:
      case AFTER_DOCTYPE_PUBLIC_KEYWORD:
      case AFTER_DOCTYPE_SYSTEM_KEYWORD:
      case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER: {
        errEofInDoctype();
        forceQuirks = true;
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
      case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED: {
        errEofInPublicId();
        forceQuirks = true;
        publicIdentifier = strBufToString();
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
      case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
      case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS: {
        errEofInDoctype();
        forceQuirks = true;
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
      case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED: {
        errEofInSystemId();
        forceQuirks = true;
        systemIdentifier = strBufToString();
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case AFTER_DOCTYPE_SYSTEM_IDENTIFIER: {
        errEofInDoctype();
        forceQuirks = true;
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case BOGUS_DOCTYPE: {
        emitDoctypeToken(0);
        NS_HTML5_BREAK(eofloop);
      }
      case CONSUME_CHARACTER_REFERENCE: {
        emitOrAppendCharRefBuf(returnState);
        state = returnState;
        continue;
      }
      case CHARACTER_REFERENCE_HILO_LOOKUP: {
        emitOrAppendCharRefBuf(returnState);
        state = returnState;
        continue;
      }
      case CHARACTER_REFERENCE_TAIL: {
        for (;;) {
          char16_t c = '\0';
          entCol++;
          for (;;) {
            if (hi == -1) {
              NS_HTML5_BREAK(hiloop);
            }
            if (entCol == nsHtml5NamedCharacters::NAMES[hi].length()) {
              NS_HTML5_BREAK(hiloop);
            }
            if (entCol > nsHtml5NamedCharacters::NAMES[hi].length()) {
              NS_HTML5_BREAK(outer);
            } else if (c < nsHtml5NamedCharacters::NAMES[hi].charAt(entCol)) {
              hi--;
            } else {
              NS_HTML5_BREAK(hiloop);
            }
          }
        hiloop_end:;
          for (;;) {
            if (hi < lo) {
              NS_HTML5_BREAK(outer);
            }
            if (entCol == nsHtml5NamedCharacters::NAMES[lo].length()) {
              candidate = lo;
              charRefBufMark = charRefBufLen;
              lo++;
            } else if (entCol > nsHtml5NamedCharacters::NAMES[lo].length()) {
              NS_HTML5_BREAK(outer);
            } else if (c > nsHtml5NamedCharacters::NAMES[lo].charAt(entCol)) {
              lo++;
            } else {
              NS_HTML5_BREAK(loloop);
            }
          }
        loloop_end:;
          if (hi < lo) {
            NS_HTML5_BREAK(outer);
          }
          continue;
        }
      outer_end:;
        if (candidate == -1) {
          emitOrAppendCharRefBuf(returnState);
          state = returnState;
          NS_HTML5_CONTINUE(eofloop);
        } else {
          const nsHtml5CharacterName& candidateName =
              nsHtml5NamedCharacters::NAMES[candidate];
          if (!candidateName.length() ||
              candidateName.charAt(candidateName.length() - 1) != ';') {
            if ((returnState & DATA_AND_RCDATA_MASK)) {
              char16_t ch;
              if (charRefBufMark == charRefBufLen) {
                ch = '\0';
              } else {
                ch = charRefBuf[charRefBufMark];
              }
              if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z')) {
                appendCharRefBufToStrBuf();
                state = returnState;
                NS_HTML5_CONTINUE(eofloop);
              }
            }
            if ((returnState & DATA_AND_RCDATA_MASK)) {
              errUnescapedAmpersandInterpretedAsCharacterReference();
            } else {
              errNotSemicolonTerminated();
            }
          }
          const char16_t* val = nsHtml5NamedCharacters::VALUES[candidate];
          if (!val[1]) {
            emitOrAppendOne(val, returnState);
          } else {
            emitOrAppendTwo(val, returnState);
          }
          if (charRefBufMark < charRefBufLen) {
            if ((returnState & DATA_AND_RCDATA_MASK)) {
              appendStrBuf(charRefBuf, charRefBufMark,
                           charRefBufLen - charRefBufMark);
            } else {
              tokenHandler->characters(charRefBuf, charRefBufMark,
                                       charRefBufLen - charRefBufMark);
            }
          }
          charRefBufLen = 0;
          state = returnState;
          NS_HTML5_CONTINUE(eofloop);
        }
      }
      case CONSUME_NCR:
      case DECIMAL_NRC_LOOP:
      case HEX_NCR_LOOP: {
        if (!seenDigits) {
          errNoDigitsInNCR();
          emitOrAppendCharRefBuf(returnState);
          state = returnState;
          continue;
        } else {
          errCharRefLacksSemicolon();
        }
        handleNcrValue(returnState);
        state = returnState;
        continue;
      }
      case CDATA_RSQB: {
        tokenHandler->characters(nsHtml5Tokenizer::RSQB_RSQB, 0, 1);
        NS_HTML5_BREAK(eofloop);
      }
      case CDATA_RSQB_RSQB: {
        tokenHandler->characters(nsHtml5Tokenizer::RSQB_RSQB, 0, 2);
        NS_HTML5_BREAK(eofloop);
      }
      case DATA:
      default: {
        NS_HTML5_BREAK(eofloop);
      }
    }
  }
eofloop_end:;
  tokenHandler->eof();
  return;
}

void nsHtml5Tokenizer::emitDoctypeToken(int32_t pos) {
  RememberGt(pos);
  cstart = pos + 1;
  tokenHandler->doctype(doctypeName, publicIdentifier, systemIdentifier,
                        forceQuirks);
  doctypeName = nullptr;
  publicIdentifier.Release();
  publicIdentifier = nullptr;
  systemIdentifier.Release();
  systemIdentifier = nullptr;
  suspendIfRequestedAfterCurrentNonTextToken();
}

void nsHtml5Tokenizer::suspendAfterCurrentTokenIfNotInText() {
  switch (stateSave) {
    case DATA:
    case RCDATA:
    case SCRIPT_DATA:
    case RAWTEXT:
    case SCRIPT_DATA_ESCAPED:
    case PLAINTEXT:
    case NON_DATA_END_TAG_NAME:
    case SCRIPT_DATA_LESS_THAN_SIGN:
    case SCRIPT_DATA_ESCAPE_START:
    case SCRIPT_DATA_ESCAPE_START_DASH:
    case SCRIPT_DATA_ESCAPED_DASH:
    case SCRIPT_DATA_ESCAPED_DASH_DASH:
    case RAWTEXT_RCDATA_LESS_THAN_SIGN:
    case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
    case SCRIPT_DATA_DOUBLE_ESCAPE_START:
    case SCRIPT_DATA_DOUBLE_ESCAPED:
    case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
    case SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
    case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
    case SCRIPT_DATA_DOUBLE_ESCAPE_END: {
      return;
    }
    case TAG_NAME:
    case BEFORE_ATTRIBUTE_NAME:
    case ATTRIBUTE_NAME:
    case AFTER_ATTRIBUTE_NAME:
    case BEFORE_ATTRIBUTE_VALUE:
    case AFTER_ATTRIBUTE_VALUE_QUOTED:
    case BOGUS_COMMENT:
    case MARKUP_DECLARATION_OPEN:
    case DOCTYPE:
    case BEFORE_DOCTYPE_NAME:
    case DOCTYPE_NAME:
    case AFTER_DOCTYPE_NAME:
    case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
    case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
    case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
    case AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
    case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
    case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
    case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
    case AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
    case BOGUS_DOCTYPE:
    case COMMENT_START:
    case COMMENT_START_DASH:
    case COMMENT:
    case COMMENT_END_DASH:
    case COMMENT_END:
    case COMMENT_END_BANG:
    case TAG_OPEN:
    case CLOSE_TAG_OPEN:
    case MARKUP_DECLARATION_HYPHEN:
    case MARKUP_DECLARATION_OCTYPE:
    case DOCTYPE_UBLIC:
    case DOCTYPE_YSTEM:
    case AFTER_DOCTYPE_PUBLIC_KEYWORD:
    case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
    case AFTER_DOCTYPE_SYSTEM_KEYWORD:
    case SELF_CLOSING_START_TAG:
    case ATTRIBUTE_VALUE_DOUBLE_QUOTED:
    case ATTRIBUTE_VALUE_SINGLE_QUOTED:
    case ATTRIBUTE_VALUE_UNQUOTED:
    case BOGUS_COMMENT_HYPHEN:
    case COMMENT_LESSTHAN:
    case COMMENT_LESSTHAN_BANG:
    case COMMENT_LESSTHAN_BANG_DASH:
    case COMMENT_LESSTHAN_BANG_DASH_DASH:
    case CDATA_START:
    case CDATA_SECTION:
    case CDATA_RSQB:
    case CDATA_RSQB_RSQB:
    case PROCESSING_INSTRUCTION:
    case PROCESSING_INSTRUCTION_QUESTION_MARK:
    case PROCESSING_INSTRUCTION_OPEN:
    case PROCESSING_INSTRUCTION_TARGET:
    case AFTER_PROCESSING_INSTRUCTION_TARGET:
    case PROCESSING_INSTRUCTION_DATA:
    case PROCESSING_INSTRUCTION_QUESTIONABLE: {
      break;
    }
    case CONSUME_CHARACTER_REFERENCE:
    case CONSUME_NCR:
    case CHARACTER_REFERENCE_TAIL:
    case HEX_NCR_LOOP:
    case DECIMAL_NRC_LOOP:
    case HANDLE_NCR_VALUE:
    case HANDLE_NCR_VALUE_RECONSUME:
    case CHARACTER_REFERENCE_HILO_LOOKUP: {
      if (returnStateSave == DATA || returnStateSave == RCDATA) {
        return;
      }
      break;
    }
    default: {
      MOZ_ASSERT(false, "Incomplete switch");
      return;
    }
  }
  suspendAfterCurrentNonTextToken = true;
}

bool nsHtml5Tokenizer::suspensionAfterCurrentNonTextTokenPending() {
  return suspendAfterCurrentNonTextToken;
}

bool nsHtml5Tokenizer::internalEncodingDeclaration(
    nsHtml5String internalCharset) {
  if (encodingDeclarationHandler) {
    return encodingDeclarationHandler->internalEncodingDeclaration(
        internalCharset);
  }
  return false;
}

void nsHtml5Tokenizer::end() {
  if (!keepBuffer) {
    strBuf = nullptr;
  }
  doctypeName = nullptr;
  if (systemIdentifier) {
    systemIdentifier.Release();
    systemIdentifier = nullptr;
  }
  if (publicIdentifier) {
    publicIdentifier.Release();
    publicIdentifier = nullptr;
  }
  if (piTarget) {
    piTarget.Release();
    piTarget = nullptr;
  }
  tagName = nullptr;
  nonInternedTagName->setNameForNonInterned(nullptr, false);
  attributeName = nullptr;
  nonInternedAttributeName->setNameForNonInterned(nullptr);
  tokenHandler->endTokenization();
  if (attributes) {
    attributes->clear(0);
  }
}

void nsHtml5Tokenizer::resetToDataState() {
  clearStrBufAfterUse();
  if (piTarget) {
    piTarget.Release();
    piTarget = nullptr;
  }
  charRefBufLen = 0;
  stateSave = nsHtml5Tokenizer::DATA;
  lastCR = false;
  index = 0;
  forceQuirks = false;
  additional = '\0';
  entCol = -1;
  firstCharKey = -1;
  lo = 0;
  hi = 0;
  candidate = -1;
  charRefBufMark = 0;
  value = 0;
  seenDigits = false;
  suspendAfterCurrentNonTextToken = false;
  endTag = false;
  shouldSuspend = false;
  initDoctypeFields();
  containsHyphen = false;
  tagName = nullptr;
  attributeName = nullptr;
  if (newAttributesEachTime) {
    if (attributes) {
      delete attributes;
      attributes = nullptr;
    }
  }
}

void nsHtml5Tokenizer::loadState(nsHtml5Tokenizer* other) {
  strBufLen = other->strBufLen;
  if (strBufLen > strBuf.length) {
    strBuf = jArray<char16_t, int32_t>::newJArray(strBufLen);
  }
  nsHtml5ArrayCopy::arraycopy(other->strBuf, strBuf, strBufLen);
  charRefBufLen = other->charRefBufLen;
  nsHtml5ArrayCopy::arraycopy(other->charRefBuf, charRefBuf, charRefBufLen);
  stateSave = other->stateSave;
  returnStateSave = other->returnStateSave;
  endTagExpectation = other->endTagExpectation;
  endTagExpectationAsArray = other->endTagExpectationAsArray;
  lastCR = other->lastCR;
  index = other->index;
  forceQuirks = other->forceQuirks;
  additional = other->additional;
  entCol = other->entCol;
  firstCharKey = other->firstCharKey;
  lo = other->lo;
  hi = other->hi;
  candidate = other->candidate;
  charRefBufMark = other->charRefBufMark;
  value = other->value;
  seenDigits = other->seenDigits;
  endTag = other->endTag;
  shouldSuspend = false;
  suspendAfterCurrentNonTextToken = false;
  doctypeName = other->doctypeName;
  systemIdentifier.Release();
  if (!other->systemIdentifier) {
    systemIdentifier = nullptr;
  } else {
    systemIdentifier =
        nsHtml5Portability::newStringFromString(other->systemIdentifier);
  }
  publicIdentifier.Release();
  if (!other->publicIdentifier) {
    publicIdentifier = nullptr;
  } else {
    publicIdentifier =
        nsHtml5Portability::newStringFromString(other->publicIdentifier);
  }
  piTarget.Release();
  if (!other->piTarget) {
    piTarget = nullptr;
  } else {
    piTarget = nsHtml5Portability::newStringFromString(other->piTarget);
  }
  processingInstructionsEnabled = other->processingInstructionsEnabled;
  containsHyphen = other->containsHyphen;
  if (!other->tagName) {
    tagName = nullptr;
  } else if (other->tagName->isInterned()) {
    tagName = other->tagName;
  } else {
    nonInternedTagName->setNameForNonInterned(other->tagName->getName(),
                                              other->tagName->isCustom());
    tagName = nonInternedTagName;
  }
  if (!other->attributeName) {
    attributeName = nullptr;
  } else if (other->attributeName->isInterned()) {
    attributeName = other->attributeName;
  } else {
    nonInternedAttributeName->setNameForNonInterned(
        other->attributeName->getLocal(nsHtml5AttributeName::HTML));
    attributeName = nonInternedAttributeName;
  }
  delete attributes;
  if (!other->attributes) {
    attributes = nullptr;
  } else {
    attributes = other->attributes->cloneAttributes();
  }
}

void nsHtml5Tokenizer::initializeWithoutStarting() {
  confident = false;
  if (!keepBuffer) {
    strBuf = nullptr;
  }
  line = 1;
  attributeLine = 1;
  resetToDataState();
}

void nsHtml5Tokenizer::setEncodingDeclarationHandler(
    nsHtml5StreamParser* encodingDeclarationHandler) {
  this->encodingDeclarationHandler = encodingDeclarationHandler;
}

nsHtml5Tokenizer::~nsHtml5Tokenizer() {
  MOZ_COUNT_DTOR(nsHtml5Tokenizer);
  delete nonInternedTagName;
  nonInternedTagName = nullptr;
  delete nonInternedAttributeName;
  nonInternedAttributeName = nullptr;
  delete attributes;
  attributes = nullptr;
}

void nsHtml5Tokenizer::initializeStatics() {}

void nsHtml5Tokenizer::releaseStatics() {}

#include "nsHtml5TokenizerCppSupplement.h"
