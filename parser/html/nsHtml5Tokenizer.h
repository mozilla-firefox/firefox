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

#ifndef nsHtml5Tokenizer_h
#define nsHtml5Tokenizer_h

#include "jArray.h"
#include "nsAHtml5TreeBuilderState.h"
#include "nsAtom.h"
#include "nsGkAtoms.h"
#include "nsHtml5ArrayCopy.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5DocumentMode.h"
#include "nsHtml5Highlighter.h"
#include "nsHtml5Macros.h"
#include "nsHtml5NamedCharacters.h"
#include "nsHtml5NamedCharactersAccel.h"
#include "nsHtml5String.h"
#include "nsHtml5TreeBuilder.h"
#include "nsIContent.h"
#include "nsTraceRefcnt.h"
#include "mozilla/htmlaccel/htmlaccelEnabled.h"

class nsHtml5StreamParser;

class nsHtml5AttributeName;
class nsHtml5ElementName;
class nsHtml5TreeBuilder;
class nsHtml5UTF16Buffer;
class nsHtml5StateSnapshot;
class nsHtml5Portability;

class nsHtml5Tokenizer {
 private:
  static const int32_t DATA_AND_RCDATA_MASK = ~1;

 public:
  static const int32_t DATA = 0;

  static const int32_t RCDATA = 1;

  static const int32_t SCRIPT_DATA = 2;

  static const int32_t RAWTEXT = 3;

  static const int32_t SCRIPT_DATA_ESCAPED = 4;

  static const int32_t ATTRIBUTE_VALUE_DOUBLE_QUOTED = 5;

  static const int32_t ATTRIBUTE_VALUE_SINGLE_QUOTED = 6;

  static const int32_t ATTRIBUTE_VALUE_UNQUOTED = 7;

  static const int32_t PLAINTEXT = 8;

  static const int32_t TAG_OPEN = 9;

  static const int32_t CLOSE_TAG_OPEN = 10;

  static const int32_t TAG_NAME = 11;

  static const int32_t BEFORE_ATTRIBUTE_NAME = 12;

  static const int32_t ATTRIBUTE_NAME = 13;

  static const int32_t AFTER_ATTRIBUTE_NAME = 14;

  static const int32_t BEFORE_ATTRIBUTE_VALUE = 15;

  static const int32_t AFTER_ATTRIBUTE_VALUE_QUOTED = 16;

  static const int32_t BOGUS_COMMENT = 17;

  static const int32_t MARKUP_DECLARATION_OPEN = 18;

  static const int32_t DOCTYPE = 19;

  static const int32_t BEFORE_DOCTYPE_NAME = 20;

  static const int32_t DOCTYPE_NAME = 21;

  static const int32_t AFTER_DOCTYPE_NAME = 22;

  static const int32_t BEFORE_DOCTYPE_PUBLIC_IDENTIFIER = 23;

  static const int32_t DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED = 24;

  static const int32_t DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED = 25;

  static const int32_t AFTER_DOCTYPE_PUBLIC_IDENTIFIER = 26;

  static const int32_t BEFORE_DOCTYPE_SYSTEM_IDENTIFIER = 27;

  static const int32_t DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED = 28;

  static const int32_t DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED = 29;

  static const int32_t AFTER_DOCTYPE_SYSTEM_IDENTIFIER = 30;

  static const int32_t BOGUS_DOCTYPE = 31;

  static const int32_t COMMENT_START = 32;

  static const int32_t COMMENT_START_DASH = 33;

  static const int32_t COMMENT = 34;

  static const int32_t COMMENT_END_DASH = 35;

  static const int32_t COMMENT_END = 36;

  static const int32_t COMMENT_END_BANG = 37;

  static const int32_t NON_DATA_END_TAG_NAME = 38;

  static const int32_t MARKUP_DECLARATION_HYPHEN = 39;

  static const int32_t MARKUP_DECLARATION_OCTYPE = 40;

  static const int32_t DOCTYPE_UBLIC = 41;

  static const int32_t DOCTYPE_YSTEM = 42;

  static const int32_t AFTER_DOCTYPE_PUBLIC_KEYWORD = 43;

  static const int32_t BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS = 44;

  static const int32_t AFTER_DOCTYPE_SYSTEM_KEYWORD = 45;

  static const int32_t CONSUME_CHARACTER_REFERENCE = 46;

  static const int32_t CONSUME_NCR = 47;

  static const int32_t CHARACTER_REFERENCE_TAIL = 48;

  static const int32_t HEX_NCR_LOOP = 49;

  static const int32_t DECIMAL_NRC_LOOP = 50;

  static const int32_t HANDLE_NCR_VALUE = 51;

  static const int32_t HANDLE_NCR_VALUE_RECONSUME = 52;

  static const int32_t CHARACTER_REFERENCE_HILO_LOOKUP = 53;

  static const int32_t SELF_CLOSING_START_TAG = 54;

  static const int32_t CDATA_START = 55;

  static const int32_t CDATA_SECTION = 56;

  static const int32_t CDATA_RSQB = 57;

  static const int32_t CDATA_RSQB_RSQB = 58;

  static const int32_t SCRIPT_DATA_LESS_THAN_SIGN = 59;

  static const int32_t SCRIPT_DATA_ESCAPE_START = 60;

  static const int32_t SCRIPT_DATA_ESCAPE_START_DASH = 61;

  static const int32_t SCRIPT_DATA_ESCAPED_DASH = 62;

  static const int32_t SCRIPT_DATA_ESCAPED_DASH_DASH = 63;

  static const int32_t BOGUS_COMMENT_HYPHEN = 64;

  static const int32_t RAWTEXT_RCDATA_LESS_THAN_SIGN = 65;

  static const int32_t SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN = 66;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPE_START = 67;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPED = 68;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN = 69;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPED_DASH = 70;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH = 71;

  static const int32_t SCRIPT_DATA_DOUBLE_ESCAPE_END = 72;

  static const int32_t PROCESSING_INSTRUCTION = 73;

  static const int32_t PROCESSING_INSTRUCTION_QUESTION_MARK = 74;

  static const int32_t COMMENT_LESSTHAN = 76;

  static const int32_t COMMENT_LESSTHAN_BANG = 77;

  static const int32_t COMMENT_LESSTHAN_BANG_DASH = 78;

  static const int32_t COMMENT_LESSTHAN_BANG_DASH_DASH = 79;

 private:
  static const int32_t LEAD_OFFSET = (0xD800 - (0x10000 >> 10));

  static char16_t LT_GT[];
  static char16_t LT_SOLIDUS[];
  static char16_t RSQB_RSQB[];
  static char16_t REPLACEMENT_CHARACTER[];
  static char16_t LF[];
  static char16_t CDATA_LSQB[];
  static char16_t OCTYPE[];
  static char16_t UBLIC[];
  static char16_t YSTEM[];
  static staticJArray<char16_t, int32_t> TITLE_ARR;
  static staticJArray<char16_t, int32_t> SCRIPT_ARR;
  static staticJArray<char16_t, int32_t> STYLE_ARR;
  static staticJArray<char16_t, int32_t> PLAINTEXT_ARR;
  static staticJArray<char16_t, int32_t> XMP_ARR;
  static staticJArray<char16_t, int32_t> TEXTAREA_ARR;
  static staticJArray<char16_t, int32_t> IFRAME_ARR;
  static staticJArray<char16_t, int32_t> NOEMBED_ARR;
  static staticJArray<char16_t, int32_t> NOSCRIPT_ARR;
  static staticJArray<char16_t, int32_t> NOFRAMES_ARR;

 protected:
  nsHtml5TreeBuilder* tokenHandler;
  nsHtml5StreamParser* encodingDeclarationHandler;
  bool lastCR;
  int32_t stateSave;

 private:
  int32_t returnStateSave;

 protected:
  int32_t index;

 private:
  bool forceQuirks;
  char16_t additional;
  int32_t entCol;
  int32_t firstCharKey;
  int32_t lo;
  int32_t hi;
  int32_t candidate;
  int32_t charRefBufMark;

 protected:
  int32_t value;

 private:
  bool seenDigits;
  bool suspendAfterCurrentNonTextToken;

 protected:
  int32_t cstart;

 private:
  nsHtml5String publicId;
  nsHtml5String systemId;
  autoJArray<char16_t, int32_t> strBuf;
  int32_t strBufLen;
  autoJArray<char16_t, int32_t> charRefBuf;
  int32_t charRefBufLen;
  autoJArray<char16_t, int32_t> bmpChar;
  autoJArray<char16_t, int32_t> astralChar;

 protected:
  nsHtml5ElementName* endTagExpectation;

 private:
  jArray<char16_t, int32_t> endTagExpectationAsArray;

 protected:
  bool endTag;

 private:
  bool containsHyphen;
  nsHtml5ElementName* tagName;
  nsHtml5ElementName* nonInternedTagName;

 protected:
  nsHtml5AttributeName* attributeName;

 private:
  nsHtml5AttributeName* nonInternedAttributeName;
  RefPtr<nsAtom> doctypeName;
  nsHtml5String publicIdentifier;
  nsHtml5String systemIdentifier;
  nsHtml5HtmlAttributes* attributes;
  bool newAttributesEachTime;
  bool shouldSuspend;
  bool keepBuffer;

 protected:
  bool confident;

 private:
  int32_t line;
  int32_t attributeLine;
  nsHtml5AtomTable* interner;
  bool viewingXmlSource;

 public:
  nsHtml5Tokenizer(nsHtml5TreeBuilder* tokenHandler, bool viewingXmlSource);
  void setInterner(nsHtml5AtomTable* interner);
  void initLocation(nsHtml5String newPublicId, nsHtml5String newSystemId);
  bool isViewingXmlSource();
  void setKeepBuffer(bool keepBuffer);
  bool dropBufferIfLongerThan(int32_t length);
  void setState(int32_t specialTokenizerState);
  void setStateAndEndTagExpectation(int32_t specialTokenizerState,
                                    nsHtml5ElementName* endTagExpectation);

 private:
  void endTagExpectationToArray();

 public:
  void setLineNumber(int32_t line);
  inline int32_t getLineNumber() { return line; }

  inline nsHtml5HtmlAttributes* emptyAttributes() {
    return nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES;
  }

 private:
  void appendCharRefBuf(char16_t c);
  void emitOrAppendCharRefBuf(int32_t returnState);
  inline void clearStrBufAfterUse() { strBufLen = 0; }

  inline void clearStrBufBeforeUse() {
    MOZ_ASSERT(!strBufLen, "strBufLen not reset after previous use!");
    strBufLen = 0;
  }

  inline void clearStrBufAfterOneHyphen() {
    MOZ_ASSERT(strBufLen == 1, "strBufLen length not one!");
    MOZ_ASSERT(strBuf[0] == '-', "strBuf does not start with a hyphen!");
    strBufLen = 0;
  }

  inline void appendStrBuf(char16_t c) {
    if (MOZ_UNLIKELY(strBufLen == strBuf.length)) {
      EnsureBufferSpaceShouldNeverHappen(1);
    }
    strBuf[strBufLen++] = c;
  }

 protected:
  inline nsHtml5String strBufToString() {
    nsHtml5String digitAtom = TryAtomizeForSingleDigit();
    if (digitAtom) {
      return digitAtom;
    }
    bool maybeAtomize = false;
    if (!newAttributesEachTime) {
      if (attributeName == nsHtml5AttributeName::ATTR_CLASS ||
          attributeName == nsHtml5AttributeName::ATTR_TYPE) {
        maybeAtomize = true;
      }
    }
    nsHtml5String str = nsHtml5Portability::newStringFromBuffer(
        strBuf, 0, strBufLen, tokenHandler, maybeAtomize);
    clearStrBufAfterUse();
    return str;
  }

 private:
  inline void strBufToDoctypeName() {
    doctypeName =
        nsHtml5Portability::newLocalNameFromBuffer(strBuf, strBufLen, interner);
    clearStrBufAfterUse();
  }

  inline void emitStrBuf() {
    if (strBufLen > 0) {
      tokenHandler->characters(strBuf, 0, strBufLen);
      clearStrBufAfterUse();
    }
  }

  inline void appendSecondHyphenToBogusComment() { appendStrBuf('-'); }

  inline void adjustDoubleHyphenAndAppendToStrBufAndErr(
      char16_t c, bool reportedConsecutiveHyphens) {
    appendStrBuf(c);
  }

  inline void appendStrBuf(char16_t* buffer, int32_t offset, int32_t length) {
    int32_t newLen = strBufLen + length;
    if (MOZ_UNLIKELY(strBuf.length < newLen)) {
      EnsureBufferSpaceShouldNeverHappen(length);
    }
    nsHtml5ArrayCopy::arraycopy(buffer, offset, strBuf, strBufLen, length);
    strBufLen = newLen;
  }

  inline void appendCharRefBufToStrBuf() {
    appendStrBuf(charRefBuf, 0, charRefBufLen);
    charRefBufLen = 0;
  }

  void emitComment(int32_t provisionalHyphens, int32_t pos);

 protected:
  void flushChars(char16_t* buf, int32_t pos);

 private:
  void strBufToElementNameString();
  int32_t emitCurrentTagToken(bool selfClosing, int32_t pos);
  void attributeNameComplete();
  void addAttributeWithoutValue();
  void addAttributeWithValue();

 public:
  void start();
  bool tokenizeBuffer(nsHtml5UTF16Buffer* buffer);

 private:
  template <class P>
  inline int32_t stateLoop(int32_t state, char16_t c, int32_t pos,
                           char16_t* buf, bool reconsume, int32_t returnState,
                           int32_t endPos) {
    bool reportedConsecutiveHyphens = false;
  stateloop:
    for (;;) {
      switch (state) {
        case DATA: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementData(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '&': {
                flushChars(buf, pos);
                MOZ_ASSERT(!charRefBufLen,
                           "charRefBufLen not reset after previous use!");
                appendCharRefBuf(c);
                setAdditionalAndRememberAmpersandLocation('\0');
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CONSUME_CHARACTER_REFERENCE,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '<': {
                flushChars(buf, pos);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::TAG_OPEN,
                                  reconsume, pos);
                NS_HTML5_BREAK(dataloop);
              }
              case '\0': {
                maybeEmitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        dataloop_end:;
          [[fallthrough]];
        }
        case TAG_OPEN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (c >= 'A' && c <= 'Z') {
              endTag = false;
              clearStrBufBeforeUse();
              appendStrBuf((char16_t)(c + 0x20));
              containsHyphen = false;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::TAG_NAME, reconsume, pos);
              NS_HTML5_BREAK(tagopenloop);
            } else if (c >= 'a' && c <= 'z') {
              endTag = false;
              clearStrBufBeforeUse();
              appendStrBuf(c);
              containsHyphen = false;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::TAG_NAME, reconsume, pos);
              NS_HTML5_BREAK(tagopenloop);
            }
            switch (c) {
              case '!': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::MARKUP_DECLARATION_OPEN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '/': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::CLOSE_TAG_OPEN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\?': {
                if (viewingXmlSource) {
                  state = P::transition(
                      mViewSource.get(),
                      nsHtml5Tokenizer::PROCESSING_INSTRUCTION, reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
                if (P::reportErrors) {
                  errProcessingInstruction();
                }
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errLtGt();
                }
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 2);
                cstart = pos + 1;
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                if (P::reportErrors) {
                  errBadCharAfterLt(c);
                }
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        tagopenloop_end:;
          [[fallthrough]];
        }
        case TAG_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                strBufToElementNameString();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                strBufToElementNameString();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(tagnameloop);
              }
              case '/': {
                strBufToElementNameString();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                strBufToElementNameString();
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                if (c >= 'A' && c <= 'Z') {
                  c += 0x20;
                } else if (c == '-') {
                  containsHyphen = true;
                }
                appendStrBuf(c);
                continue;
              }
            }
          }
        tagnameloop_end:;
          [[fallthrough]];
        }
        case BEFORE_ATTRIBUTE_NAME: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '/': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              case '\"':
              case '\'':
              case '<':
              case '=': {
                if (P::reportErrors) {
                  errBadCharBeforeAttributeNameOrNull(c);
                }
                [[fallthrough]];
              }
              default: {
                if (c >= 'A' && c <= 'Z') {
                  c += 0x20;
                }
                attributeLine = line;
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(beforeattributenameloop);
              }
            }
          }
        beforeattributenameloop_end:;
          [[fallthrough]];
        }
        case ATTRIBUTE_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                attributeNameComplete();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::AFTER_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                attributeNameComplete();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::AFTER_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '/': {
                attributeNameComplete();
                addAttributeWithoutValue();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '=': {
                attributeNameComplete();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_VALUE,
                                      reconsume, pos);
                NS_HTML5_BREAK(attributenameloop);
              }
              case '>': {
                attributeNameComplete();
                addAttributeWithoutValue();
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              case '\"':
              case '\'':
              case '<': {
                if (P::reportErrors) {
                  errQuoteOrLtInAttributeNameOrNull(c);
                }
                [[fallthrough]];
              }
              default: {
                if (c >= 'A' && c <= 'Z') {
                  c += 0x20;
                }
                appendStrBuf(c);
                continue;
              }
            }
          }
        attributenameloop_end:;
          [[fallthrough]];
        }
        case BEFORE_ATTRIBUTE_VALUE: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '\"': {
                attributeLine = line;
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::ATTRIBUTE_VALUE_DOUBLE_QUOTED, reconsume,
                    pos);
                NS_HTML5_BREAK(beforeattributevalueloop);
              }
              case '&': {
                attributeLine = line;
                clearStrBufBeforeUse();
                reconsume = true;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::ATTRIBUTE_VALUE_UNQUOTED, reconsume, pos);

                NS_HTML5_CONTINUE(stateloop);
              }
              case '\'': {
                attributeLine = line;
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::ATTRIBUTE_VALUE_SINGLE_QUOTED, reconsume,
                    pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errAttributeValueMissing();
                }
                addAttributeWithoutValue();
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              case '<':
              case '=':
              case '`': {
                if (P::reportErrors) {
                  errLtOrEqualsOrGraveInUnquotedAttributeOrNull(c);
                }
                [[fallthrough]];
              }
              default: {
                attributeLine = line;
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::ATTRIBUTE_VALUE_UNQUOTED, reconsume, pos);

                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        beforeattributevalueloop_end:;
          [[fallthrough]];
        }
        case ATTRIBUTE_VALUE_DOUBLE_QUOTED: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementAttributeValueDoubleQuoted(
                  this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\"': {
                addAttributeWithValue();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_ATTRIBUTE_VALUE_QUOTED, reconsume,
                    pos);
                NS_HTML5_BREAK(attributevaluedoublequotedloop);
              }
              case '&': {
                MOZ_ASSERT(!charRefBufLen,
                           "charRefBufLen not reset after previous use!");
                appendCharRefBuf(c);
                setAdditionalAndRememberAmpersandLocation('\"');
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CONSUME_CHARACTER_REFERENCE,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        attributevaluedoublequotedloop_end:;
          [[fallthrough]];
        }
        case AFTER_ATTRIBUTE_VALUE_QUOTED: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '/': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG,
                                      reconsume, pos);
                NS_HTML5_BREAK(afterattributevaluequotedloop);
              }
              case '>': {
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                if (P::reportErrors) {
                  errNoSpaceBetweenAttributes();
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        afterattributevaluequotedloop_end:;
          [[fallthrough]];
        }
        case SELF_CLOSING_START_TAG: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '>': {
              state =
                  P::transition(mViewSource.get(),
                                emitCurrentTagToken(true, pos), reconsume, pos);
              if (shouldSuspend) {
                NS_HTML5_BREAK(stateloop);
              }
              NS_HTML5_CONTINUE(stateloop);
            }
            default: {
              if (P::reportErrors) {
                errSlashNotFollowedByGt();
              }
              reconsume = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
        }
        case ATTRIBUTE_VALUE_UNQUOTED: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                addAttributeWithValue();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                addAttributeWithValue();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '&': {
                MOZ_ASSERT(!charRefBufLen,
                           "charRefBufLen not reset after previous use!");
                appendCharRefBuf(c);
                setAdditionalAndRememberAmpersandLocation('>');
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CONSUME_CHARACTER_REFERENCE,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                addAttributeWithValue();
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              case '<':
              case '\"':
              case '\'':
              case '=':
              case '`': {
                if (P::reportErrors) {
                  errUnquotedAttributeValOrNull(c);
                }
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        }
        case AFTER_ATTRIBUTE_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '/': {
                addAttributeWithoutValue();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '=': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_ATTRIBUTE_VALUE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                addAttributeWithoutValue();
                state = P::transition(mViewSource.get(),
                                      emitCurrentTagToken(false, pos),
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              case '\"':
              case '\'':
              case '<': {
                if (P::reportErrors) {
                  errQuoteOrLtInAttributeNameOrNull(c);
                }
                [[fallthrough]];
              }
              default: {
                addAttributeWithoutValue();
                if (c >= 'A' && c <= 'Z') {
                  c += 0x20;
                }
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::ATTRIBUTE_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case MARKUP_DECLARATION_OPEN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::MARKUP_DECLARATION_HYPHEN,
                                  reconsume, pos);
                NS_HTML5_BREAK(markupdeclarationopenloop);
              }
              case 'd':
              case 'D': {
                clearStrBufBeforeUse();
                appendStrBuf(c);
                index = 0;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::MARKUP_DECLARATION_OCTYPE,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '[': {
                if (tokenHandler->cdataSectionAllowed()) {
                  clearStrBufBeforeUse();
                  appendStrBuf(c);
                  index = 0;
                  state = P::transition(mViewSource.get(),
                                        nsHtml5Tokenizer::CDATA_START,
                                        reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
                [[fallthrough]];
              }
              default: {
                if (P::reportErrors) {
                  errBogusComment();
                }
                clearStrBufBeforeUse();
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        markupdeclarationopenloop_end:;
          [[fallthrough]];
        }
        case MARKUP_DECLARATION_HYPHEN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                clearStrBufAfterOneHyphen();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_START,
                                      reconsume, pos);
                NS_HTML5_BREAK(markupdeclarationhyphenloop);
              }
              default: {
                if (P::reportErrors) {
                  errBogusComment();
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        markupdeclarationhyphenloop_end:;
          [[fallthrough]];
        }
        case COMMENT_START: {
          reportedConsecutiveHyphens = false;
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_START_DASH,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errPrematureEndOfComment();
                }
                emitComment(0, pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '<': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(commentstartloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(commentstartloop);
              }
            }
          }
        commentstartloop_end:;
          [[fallthrough]];
        }
        case COMMENT: {
          for (;;) {
            ++pos;
            pos += P::accelerateAdvancementComment(this, buf, pos, endPos);
            if (pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_END_DASH,
                                      reconsume, pos);
                NS_HTML5_BREAK(commentloop);
              }
              case '<': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        commentloop_end:;
          [[fallthrough]];
        }
        case COMMENT_END_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_END, reconsume,
                                      pos);
                NS_HTML5_BREAK(commentenddashloop);
              }
              case '<': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        commentenddashloop_end:;
          [[fallthrough]];
        }
        case COMMENT_END: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '>': {
                emitComment(2, pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '-': {
                adjustDoubleHyphenAndAppendToStrBufAndErr(
                    c, reportedConsecutiveHyphens);
                reportedConsecutiveHyphens = true;
                continue;
              }
              case '<': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                adjustDoubleHyphenAndAppendToStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                adjustDoubleHyphenAndAppendToStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '!': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_END_BANG,
                                      reconsume, pos);
                NS_HTML5_BREAK(commentendloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                adjustDoubleHyphenAndAppendToStrBufAndErr(
                    c, reportedConsecutiveHyphens);
                reportedConsecutiveHyphens = true;
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        commentendloop_end:;
          [[fallthrough]];
        }
        case COMMENT_END_BANG: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '>': {
                emitComment(3, pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_END_DASH,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case COMMENT_LESSTHAN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '!': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN_BANG,
                                      reconsume, pos);
                NS_HTML5_BREAK(commentlessthanloop);
              }
              case '<': {
                appendStrBuf(c);
                continue;
              }
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_END_DASH,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        commentlessthanloop_end:;
          [[fallthrough]];
        }
        case COMMENT_LESSTHAN_BANG: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::COMMENT_LESSTHAN_BANG_DASH,
                                  reconsume, pos);
                NS_HTML5_BREAK(commentlessthanbangloop);
              }
              case '<': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::COMMENT,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        commentlessthanbangloop_end:;
          [[fallthrough]];
        }
        case COMMENT_LESSTHAN_BANG_DASH: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '-': {
              appendStrBuf(c);
              state = P::transition(
                  mViewSource.get(),
                  nsHtml5Tokenizer::COMMENT_LESSTHAN_BANG_DASH_DASH, reconsume,
                  pos);
              break;
            }
            case '<': {
              appendStrBuf(c);
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\r': {
              appendStrBufCarriageReturn<P>();
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_BREAK(stateloop);
            }
            case '\n': {
              appendStrBufLineFeed<P>();
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\0': {
              c = 0xfffd;
              [[fallthrough]];
            }
            default: {
              appendStrBuf(c);
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
          [[fallthrough]];
        }
        case COMMENT_LESSTHAN_BANG_DASH_DASH: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '>': {
              appendStrBuf(c);
              emitComment(3, pos);
              state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                    reconsume, pos);
              if (shouldSuspend) {
                NS_HTML5_BREAK(stateloop);
              }
              NS_HTML5_CONTINUE(stateloop);
            }
            case '-': {
              if (P::reportErrors) {
                errNestedComment();
              }
              adjustDoubleHyphenAndAppendToStrBufAndErr(
                  c, reportedConsecutiveHyphens);
              reportedConsecutiveHyphens = true;
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::COMMENT_END, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\r': {
              c = '\n';
              P::silentCarriageReturn(this);
              if (P::reportErrors) {
                errNestedComment();
              }
              adjustDoubleHyphenAndAppendToStrBufAndErr(
                  c, reportedConsecutiveHyphens);
              reportedConsecutiveHyphens = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_BREAK(stateloop);
            }
            case '\n': {
              P::silentLineFeed(this);
              if (P::reportErrors) {
                errNestedComment();
              }
              adjustDoubleHyphenAndAppendToStrBufAndErr(
                  c, reportedConsecutiveHyphens);
              reportedConsecutiveHyphens = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '!': {
              if (P::reportErrors) {
                errNestedComment();
              }
              adjustDoubleHyphenAndAppendToStrBufAndErr(
                  c, reportedConsecutiveHyphens);
              reportedConsecutiveHyphens = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT_END_BANG,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\0': {
              c = 0xfffd;
              [[fallthrough]];
            }
            default: {
              if (P::reportErrors) {
                errNestedComment();
              }
              adjustDoubleHyphenAndAppendToStrBufAndErr(
                  c, reportedConsecutiveHyphens);
              reportedConsecutiveHyphens = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
        }
        case COMMENT_START_DASH: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '-': {
              appendStrBuf(c);
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::COMMENT_END, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '>': {
              if (P::reportErrors) {
                errPrematureEndOfComment();
              }
              emitComment(1, pos);
              state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                    reconsume, pos);
              if (shouldSuspend) {
                NS_HTML5_BREAK(stateloop);
              }
              NS_HTML5_CONTINUE(stateloop);
            }
            case '<': {
              appendStrBuf(c);
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT_LESSTHAN,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\r': {
              appendStrBufCarriageReturn<P>();
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_BREAK(stateloop);
            }
            case '\n': {
              appendStrBufLineFeed<P>();
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\0': {
              c = 0xfffd;
              [[fallthrough]];
            }
            default: {
              appendStrBuf(c);
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::COMMENT, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
        }
        case CDATA_START: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (index < 6) {
              if (c == nsHtml5Tokenizer::CDATA_LSQB[index]) {
                appendStrBuf(c);
              } else {
                if (P::reportErrors) {
                  errBogusComment();
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              continue;
            } else {
              clearStrBufAfterUse();
              cstart = pos;
              reconsume = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::CDATA_SECTION, reconsume,
                                    pos);
              break;
            }
          }
          [[fallthrough]];
        }
        case CDATA_SECTION: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos +=
                  P::accelerateAdvancementCdataSection(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case ']': {
                flushChars(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CDATA_RSQB, reconsume, pos);
                NS_HTML5_BREAK(cdatasectionloop);
              }
              case '\0': {
                maybeEmitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        cdatasectionloop_end:;
          [[fallthrough]];
        }
        case CDATA_RSQB: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case ']': {
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::CDATA_RSQB_RSQB,
                                    reconsume, pos);
              break;
            }
            default: {
              tokenHandler->characters(nsHtml5Tokenizer::RSQB_RSQB, 0, 1);
              cstart = pos;
              reconsume = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::CDATA_SECTION, reconsume,
                                    pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
          [[fallthrough]];
        }
        case CDATA_RSQB_RSQB: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case ']': {
                tokenHandler->characters(nsHtml5Tokenizer::RSQB_RSQB, 0, 1);
                continue;
              }
              case '>': {
                cstart = pos + 1;
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                suspendIfRequestedAfterCurrentNonTextToken();
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                tokenHandler->characters(nsHtml5Tokenizer::RSQB_RSQB, 0, 2);
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::CDATA_SECTION,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case ATTRIBUTE_VALUE_SINGLE_QUOTED: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementAttributeValueSingleQuoted(
                  this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\'': {
                addAttributeWithValue();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_ATTRIBUTE_VALUE_QUOTED, reconsume,
                    pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '&': {
                MOZ_ASSERT(!charRefBufLen,
                           "charRefBufLen not reset after previous use!");
                appendCharRefBuf(c);
                setAdditionalAndRememberAmpersandLocation('\'');
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CONSUME_CHARACTER_REFERENCE,
                                  reconsume, pos);
                NS_HTML5_BREAK(attributevaluesinglequotedloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        attributevaluesinglequotedloop_end:;
          [[fallthrough]];
        }
        case CONSUME_CHARACTER_REFERENCE: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '<':
            case '&':
            case '\0':
            case ';': {
              emitOrAppendCharRefBuf(returnState);
              if (!(returnState & DATA_AND_RCDATA_MASK)) {
                cstart = pos;
              }
              reconsume = true;
              state =
                  P::transition(mViewSource.get(), returnState, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '#': {
              appendCharRefBuf('#');
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::CONSUME_NCR, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            default: {
              if (c == additional) {
                emitOrAppendCharRefBuf(returnState);
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              if (c >= 'a' && c <= 'z') {
                firstCharKey = c - 'a' + 26;
              } else if (c >= 'A' && c <= 'Z') {
                firstCharKey = c - 'A';
              } else {
                if (c == ';') {
                  if (P::reportErrors) {
                    errNoNamedCharacterMatch();
                  }
                }
                emitOrAppendCharRefBuf(returnState);
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos;
                }
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              appendCharRefBuf(c);
              state = P::transition(
                  mViewSource.get(),
                  nsHtml5Tokenizer::CHARACTER_REFERENCE_HILO_LOOKUP, reconsume,
                  pos);
              break;
            }
          }
          [[fallthrough]];
        }
        case CHARACTER_REFERENCE_HILO_LOOKUP: {
          {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            int32_t hilo = 0;
            if (c <= 'z') {
              const int32_t* row = nsHtml5NamedCharactersAccel::HILO_ACCEL[c];
              if (row) {
                hilo = row[firstCharKey];
              }
            }
            if (!hilo) {
              if (c == ';') {
                if (P::reportErrors) {
                  errNoNamedCharacterMatch();
                }
              }
              emitOrAppendCharRefBuf(returnState);
              if (!(returnState & DATA_AND_RCDATA_MASK)) {
                cstart = pos;
              }
              reconsume = true;
              state =
                  P::transition(mViewSource.get(), returnState, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            appendCharRefBuf(c);
            lo = hilo & 0xFFFF;
            hi = hilo >> 16;
            entCol = -1;
            candidate = -1;
            charRefBufMark = 0;
            state = P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CHARACTER_REFERENCE_TAIL,
                                  reconsume, pos);
          }
          [[fallthrough]];
        }
        case CHARACTER_REFERENCE_TAIL: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            entCol++;
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
            for (;;) {
              if (hi < lo) {
                NS_HTML5_BREAK(outer);
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
            if (c == ';') {
              if (entCol + 1 == nsHtml5NamedCharacters::NAMES[lo].length()) {
                candidate = lo;
                charRefBufMark = charRefBufLen;
              }
              NS_HTML5_BREAK(outer);
            }
            if (hi < lo) {
              NS_HTML5_BREAK(outer);
            }
            appendCharRefBuf(c);
            continue;
          }
        outer_end:;
          if (candidate == -1) {
            if (c == ';') {
              if (P::reportErrors) {
                errNoNamedCharacterMatch();
              }
            }
            emitOrAppendCharRefBuf(returnState);
            if (!(returnState & DATA_AND_RCDATA_MASK)) {
              cstart = pos;
            }
            reconsume = true;
            state =
                P::transition(mViewSource.get(), returnState, reconsume, pos);
            NS_HTML5_CONTINUE(stateloop);
          } else {
            const nsHtml5CharacterName& candidateName =
                nsHtml5NamedCharacters::NAMES[candidate];
            if (!candidateName.length() ||
                candidateName.charAt(candidateName.length() - 1) != ';') {
              if ((returnState & DATA_AND_RCDATA_MASK)) {
                char16_t ch;
                if (charRefBufMark == charRefBufLen) {
                  ch = c;
                } else {
                  ch = charRefBuf[charRefBufMark];
                }
                if (ch == '=' || (ch >= '0' && ch <= '9') ||
                    (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                  if (c == ';') {
                    if (P::reportErrors) {
                      errNoNamedCharacterMatch();
                    }
                  }
                  appendCharRefBufToStrBuf();
                  reconsume = true;
                  state = P::transition(mViewSource.get(), returnState,
                                        reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
              }
              if ((returnState & DATA_AND_RCDATA_MASK)) {
                if (P::reportErrors) {
                  errUnescapedAmpersandInterpretedAsCharacterReference();
                }
              } else {
                if (P::reportErrors) {
                  errNotSemicolonTerminated();
                }
              }
            }
            P::completedNamedCharacterReference(mViewSource.get());
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
            bool earlyBreak = (c == ';' && charRefBufMark == charRefBufLen);
            charRefBufLen = 0;
            if (!(returnState & DATA_AND_RCDATA_MASK)) {
              cstart = earlyBreak ? pos + 1 : pos;
            }
            reconsume = !earlyBreak;
            state =
                P::transition(mViewSource.get(), returnState, reconsume, pos);
            NS_HTML5_CONTINUE(stateloop);
          }
        }
        case CONSUME_NCR: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          value = 0;
          seenDigits = false;
          switch (c) {
            case 'x':
            case 'X': {
              appendCharRefBuf(c);
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::HEX_NCR_LOOP, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            default: {
              reconsume = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::DECIMAL_NRC_LOOP,
                                    reconsume, pos);
              break;
            }
          }
          [[fallthrough]];
        }
        case DECIMAL_NRC_LOOP: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            MOZ_ASSERT(value >= 0, "value must not become negative.");
            if (c >= '0' && c <= '9') {
              seenDigits = true;
              if (value <= 0x10FFFF) {
                value *= 10;
                value += c - '0';
              }
              continue;
            } else if (c == ';') {
              if (seenDigits) {
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos + 1;
                }
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::HANDLE_NCR_VALUE,
                                      reconsume, pos);
                NS_HTML5_BREAK(decimalloop);
              } else {
                if (P::reportErrors) {
                  errNoDigitsInNCR();
                }
                appendCharRefBuf(';');
                emitOrAppendCharRefBuf(returnState);
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos + 1;
                }
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            } else {
              if (!seenDigits) {
                if (P::reportErrors) {
                  errNoDigitsInNCR();
                }
                emitOrAppendCharRefBuf(returnState);
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos;
                }
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              } else {
                if (P::reportErrors) {
                  errCharRefLacksSemicolon();
                }
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos;
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::HANDLE_NCR_VALUE,
                                      reconsume, pos);
                NS_HTML5_BREAK(decimalloop);
              }
            }
          }
        decimalloop_end:;
          [[fallthrough]];
        }
        case HANDLE_NCR_VALUE: {
          charRefBufLen = 0;
          handleNcrValue(returnState);
          state = P::transition(mViewSource.get(), returnState, reconsume, pos);
          NS_HTML5_CONTINUE(stateloop);
        }
        case HEX_NCR_LOOP: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            MOZ_ASSERT(value >= 0, "value must not become negative.");
            if (c >= '0' && c <= '9') {
              seenDigits = true;
              if (value <= 0x10FFFF) {
                value *= 16;
                value += c - '0';
              }
              continue;
            } else if (c >= 'A' && c <= 'F') {
              seenDigits = true;
              if (value <= 0x10FFFF) {
                value *= 16;
                value += c - 'A' + 10;
              }
              continue;
            } else if (c >= 'a' && c <= 'f') {
              seenDigits = true;
              if (value <= 0x10FFFF) {
                value *= 16;
                value += c - 'a' + 10;
              }
              continue;
            } else if (c == ';') {
              if (seenDigits) {
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos + 1;
                }
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::HANDLE_NCR_VALUE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              } else {
                if (P::reportErrors) {
                  errNoDigitsInNCR();
                }
                appendCharRefBuf(';');
                emitOrAppendCharRefBuf(returnState);
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos + 1;
                }
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            } else {
              if (!seenDigits) {
                if (P::reportErrors) {
                  errNoDigitsInNCR();
                }
                emitOrAppendCharRefBuf(returnState);
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos;
                }
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              } else {
                if (P::reportErrors) {
                  errCharRefLacksSemicolon();
                }
                if (!(returnState & DATA_AND_RCDATA_MASK)) {
                  cstart = pos;
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::HANDLE_NCR_VALUE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case PLAINTEXT: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementPlaintext(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\0': {
                emitPlaintextReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        }
        case CLOSE_TAG_OPEN: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '>': {
              if (P::reportErrors) {
                errLtSlashGt();
              }
              cstart = pos + 1;
              state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\r': {
              P::silentCarriageReturn(this);
              if (P::reportErrors) {
                errGarbageAfterLtSlash();
              }
              clearStrBufBeforeUse();
              appendStrBuf('\n');
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::BOGUS_COMMENT, reconsume,
                                    pos);
              NS_HTML5_BREAK(stateloop);
            }
            case '\n': {
              P::silentLineFeed(this);
              if (P::reportErrors) {
                errGarbageAfterLtSlash();
              }
              clearStrBufBeforeUse();
              appendStrBuf(c);
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::BOGUS_COMMENT, reconsume,
                                    pos);
              NS_HTML5_CONTINUE(stateloop);
            }
            case '\0': {
              c = 0xfffd;
              [[fallthrough]];
            }
            default: {
              if (c >= 'A' && c <= 'Z') {
                c += 0x20;
              }
              if (c >= 'a' && c <= 'z') {
                endTag = true;
                clearStrBufBeforeUse();
                appendStrBuf(c);
                containsHyphen = false;
                state =
                    P::transition(mViewSource.get(), nsHtml5Tokenizer::TAG_NAME,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              } else {
                if (P::reportErrors) {
                  errGarbageAfterLtSlash();
                }
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case RCDATA: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementData(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '&': {
                flushChars(buf, pos);
                MOZ_ASSERT(!charRefBufLen,
                           "charRefBufLen not reset after previous use!");
                appendCharRefBuf(c);
                setAdditionalAndRememberAmpersandLocation('\0');
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::CONSUME_CHARACTER_REFERENCE,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '<': {
                flushChars(buf, pos);
                returnState = state;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::RAWTEXT_RCDATA_LESS_THAN_SIGN, reconsume,
                    pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        }
        case RAWTEXT: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementRawtext(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '<': {
                flushChars(buf, pos);
                returnState = state;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::RAWTEXT_RCDATA_LESS_THAN_SIGN, reconsume,
                    pos);
                NS_HTML5_BREAK(rawtextloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        rawtextloop_end:;
          [[fallthrough]];
        }
        case RAWTEXT_RCDATA_LESS_THAN_SIGN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '/': {
                index = 0;
                clearStrBufBeforeUse();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::NON_DATA_END_TAG_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(rawtextrcdatalessthansignloop);
              }
              default: {
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        rawtextrcdatalessthansignloop_end:;
          [[fallthrough]];
        }
        case NON_DATA_END_TAG_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (!endTagExpectationAsArray) {
              tokenHandler->characters(nsHtml5Tokenizer::LT_SOLIDUS, 0, 2);
              cstart = pos;
              reconsume = true;
              state =
                  P::transition(mViewSource.get(), returnState, reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            } else if (index < endTagExpectationAsArray.length) {
              char16_t e = endTagExpectationAsArray[index];
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded != e) {
                tokenHandler->characters(nsHtml5Tokenizer::LT_SOLIDUS, 0, 2);
                emitStrBuf();
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(), returnState, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              appendStrBuf(c);
              index++;
              continue;
            } else {
              endTag = true;
              tagName = endTagExpectation;
              switch (c) {
                case '\r': {
                  P::silentCarriageReturn(this);
                  clearStrBufAfterUse();
                  state = P::transition(mViewSource.get(),
                                        nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                        reconsume, pos);
                  NS_HTML5_BREAK(stateloop);
                }
                case '\n': {
                  P::silentLineFeed(this);
                  [[fallthrough]];
                }
                case ' ':
                case '\t':
                case '\f': {
                  clearStrBufAfterUse();
                  state = P::transition(mViewSource.get(),
                                        nsHtml5Tokenizer::BEFORE_ATTRIBUTE_NAME,
                                        reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
                case '/': {
                  clearStrBufAfterUse();
                  state = P::transition(
                      mViewSource.get(),
                      nsHtml5Tokenizer::SELF_CLOSING_START_TAG, reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
                case '>': {
                  clearStrBufAfterUse();
                  state = P::transition(mViewSource.get(),
                                        emitCurrentTagToken(false, pos),
                                        reconsume, pos);
                  if (shouldSuspend) {
                    NS_HTML5_BREAK(stateloop);
                  }
                  NS_HTML5_CONTINUE(stateloop);
                }
                default: {
                  tokenHandler->characters(nsHtml5Tokenizer::LT_SOLIDUS, 0, 2);
                  emitStrBuf();
                  cstart = pos;
                  reconsume = true;
                  state = P::transition(mViewSource.get(), returnState,
                                        reconsume, pos);
                  NS_HTML5_CONTINUE(stateloop);
                }
              }
            }
          }
        }
        case BOGUS_COMMENT: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '>': {
                emitComment(0, pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '-': {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT_HYPHEN,
                                      reconsume, pos);
                NS_HTML5_BREAK(boguscommentloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        boguscommentloop_end:;
          [[fallthrough]];
        }
        case BOGUS_COMMENT_HYPHEN: {
        boguscommenthyphenloop:
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '>': {
                emitComment(0, pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '-': {
                appendSecondHyphenToBogusComment();
                NS_HTML5_CONTINUE(boguscommenthyphenloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case SCRIPT_DATA: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementRawtext(this, buf, pos, endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '<': {
                flushChars(buf, pos);
                returnState = state;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_LESS_THAN_SIGN,
                                  reconsume, pos);
                NS_HTML5_BREAK(scriptdataloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        scriptdataloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_LESS_THAN_SIGN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '/': {
                index = 0;
                clearStrBufBeforeUse();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::NON_DATA_END_TAG_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '!': {
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPE_START, reconsume, pos);
                NS_HTML5_BREAK(scriptdatalessthansignloop);
              }
              default: {
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdatalessthansignloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPE_START: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPE_START_DASH, reconsume,
                    pos);
                NS_HTML5_BREAK(scriptdataescapestartloop);
              }
              default: {
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdataescapestartloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPE_START_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_DASH_DASH, reconsume,
                    pos);
                NS_HTML5_BREAK(scriptdataescapestartdashloop);
              }
              default: {
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdataescapestartdashloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPED_DASH_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                continue;
              }
              case '<': {
                flushChars(buf, pos);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_BREAK(scriptdataescapeddashdashloop);
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_BREAK(scriptdataescapeddashdashloop);
              }
            }
          }
        scriptdataescapeddashdashloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPED: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              ++pos;
              pos += P::accelerateAdvancementScriptDataEscaped(this, buf, pos,
                                                               endPos);
              if (pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_DASH, reconsume, pos);
                NS_HTML5_BREAK(scriptdataescapedloop);
              }
              case '<': {
                flushChars(buf, pos);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        scriptdataescapedloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPED_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_DASH_DASH, reconsume,
                    pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '<': {
                flushChars(buf, pos);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_BREAK(scriptdataescapeddashloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdataescapeddashloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '/': {
                index = 0;
                clearStrBufBeforeUse();
                returnState = nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::NON_DATA_END_TAG_NAME,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case 'S':
              case 's': {
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                index = 1;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPE_START,
                    reconsume, pos);
                NS_HTML5_BREAK(scriptdataescapedlessthanloop);
              }
              default: {
                tokenHandler->characters(nsHtml5Tokenizer::LT_GT, 0, 1);
                cstart = pos;
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdataescapedlessthanloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPE_START: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            MOZ_ASSERT(index > 0);
            if (index < 6) {
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded != nsHtml5Tokenizer::SCRIPT_ARR[index]) {
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              continue;
            }
            switch (c) {
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f':
              case '/':
              case '>': {
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_BREAK(scriptdatadoubleescapestartloop);
              }
              default: {
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdatadoubleescapestartloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPED: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED_DASH,
                    reconsume, pos);
                NS_HTML5_BREAK(scriptdatadoubleescapedloop);
              }
              case '<': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                continue;
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        scriptdatadoubleescapedloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPED_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH,
                    reconsume, pos);
                NS_HTML5_BREAK(scriptdatadoubleescapeddashloop);
              }
              case '<': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdatadoubleescapeddashloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '-': {
                continue;
              }
              case '<': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN,
                    reconsume, pos);
                NS_HTML5_BREAK(scriptdatadoubleescapeddashdashloop);
              }
              case '>': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA, reconsume,
                                      pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                emitReplacementCharacter(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdatadoubleescapeddashdashloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '/': {
                index = 0;
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPE_END, reconsume,
                    pos);
                NS_HTML5_BREAK(scriptdatadoubleescapedlessthanloop);
              }
              default: {
                reconsume = true;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        scriptdatadoubleescapedlessthanloop_end:;
          [[fallthrough]];
        }
        case SCRIPT_DATA_DOUBLE_ESCAPE_END: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (index < 6) {
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded != nsHtml5Tokenizer::SCRIPT_ARR[index]) {
                reconsume = true;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              continue;
            }
            switch (c) {
              case '\r': {
                emitCarriageReturn<P>(buf, pos);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f':
              case '/':
              case '>': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::SCRIPT_DATA_ESCAPED,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                reconsume = true;
                state =
                    P::transition(mViewSource.get(),
                                  nsHtml5Tokenizer::SCRIPT_DATA_DOUBLE_ESCAPED,
                                  reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        }
        case MARKUP_DECLARATION_OCTYPE: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (index < 6) {
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded == nsHtml5Tokenizer::OCTYPE[index]) {
                appendStrBuf(c);
              } else {
                if (P::reportErrors) {
                  errBogusComment();
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_COMMENT,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              continue;
            } else {
              reconsume = true;
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::DOCTYPE, reconsume, pos);
              NS_HTML5_BREAK(markupdeclarationdoctypeloop);
            }
          }
        markupdeclarationdoctypeloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            initDoctypeFields();
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_DOCTYPE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_DOCTYPE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(doctypeloop);
              }
              default: {
                if (P::reportErrors) {
                  errMissingSpaceBeforeDoctypeName();
                }
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BEFORE_DOCTYPE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(doctypeloop);
              }
            }
          }
        doctypeloop_end:;
          [[fallthrough]];
        }
        case BEFORE_DOCTYPE_NAME: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '>': {
                if (P::reportErrors) {
                  errNamelessDoctype();
                }
                forceQuirks = true;
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                if (c >= 'A' && c <= 'Z') {
                  c += 0x20;
                }
                clearStrBufBeforeUse();
                appendStrBuf(c);
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::DOCTYPE_NAME, reconsume,
                                      pos);
                NS_HTML5_BREAK(beforedoctypenameloop);
              }
            }
          }
        beforedoctypenameloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                strBufToDoctypeName();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::AFTER_DOCTYPE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                strBufToDoctypeName();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::AFTER_DOCTYPE_NAME,
                                      reconsume, pos);
                NS_HTML5_BREAK(doctypenameloop);
              }
              case '>': {
                strBufToDoctypeName();
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                if (c >= 'A' && c <= 'Z') {
                  c += 0x0020;
                }
                appendStrBuf(c);
                continue;
              }
            }
          }
        doctypenameloop_end:;
          [[fallthrough]];
        }
        case AFTER_DOCTYPE_NAME: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '>': {
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case 'p':
              case 'P': {
                index = 0;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::DOCTYPE_UBLIC,
                                      reconsume, pos);
                NS_HTML5_BREAK(afterdoctypenameloop);
              }
              case 's':
              case 'S': {
                index = 0;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::DOCTYPE_YSTEM,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        afterdoctypenameloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE_UBLIC: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (index < 5) {
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded != nsHtml5Tokenizer::UBLIC[index]) {
                bogusDoctype();
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              continue;
            } else {
              reconsume = true;
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::AFTER_DOCTYPE_PUBLIC_KEYWORD,
                                reconsume, pos);
              NS_HTML5_BREAK(doctypeublicloop);
            }
          }
        doctypeublicloop_end:;
          [[fallthrough]];
        }
        case AFTER_DOCTYPE_PUBLIC_KEYWORD: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::BEFORE_DOCTYPE_PUBLIC_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::BEFORE_DOCTYPE_PUBLIC_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(afterdoctypepublickeywordloop);
              }
              case '\"': {
                if (P::reportErrors) {
                  errNoSpaceBetweenDoctypePublicKeywordAndQuote();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\'': {
                if (P::reportErrors) {
                  errNoSpaceBetweenDoctypePublicKeywordAndQuote();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errExpectedPublicId();
                }
                forceQuirks = true;
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        afterdoctypepublickeywordloop_end:;
          [[fallthrough]];
        }
        case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '\"': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_BREAK(beforedoctypepublicidentifierloop);
              }
              case '\'': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errExpectedPublicId();
                }
                forceQuirks = true;
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        beforedoctypepublicidentifierloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\"': {
                publicIdentifier = strBufToString();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_DOCTYPE_PUBLIC_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(doctypepublicidentifierdoublequotedloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errGtInPublicId();
                }
                forceQuirks = true;
                publicIdentifier = strBufToString();
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        doctypepublicidentifierdoublequotedloop_end:;
          [[fallthrough]];
        }
        case AFTER_DOCTYPE_PUBLIC_IDENTIFIER: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::
                        BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS,
                    reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::
                        BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS,
                    reconsume, pos);
                NS_HTML5_BREAK(afterdoctypepublicidentifierloop);
              }
              case '>': {
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\"': {
                if (P::reportErrors) {
                  errNoSpaceBetweenPublicAndSystemIds();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\'': {
                if (P::reportErrors) {
                  errNoSpaceBetweenPublicAndSystemIds();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        afterdoctypepublicidentifierloop_end:;
          [[fallthrough]];
        }
        case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '>': {
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\"': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_BREAK(betweendoctypepublicandsystemidentifiersloop);
              }
              case '\'': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        betweendoctypepublicandsystemidentifiersloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\"': {
                systemIdentifier = strBufToString();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_DOCTYPE_SYSTEM_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(doctypesystemidentifierdoublequotedloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errGtInSystemId();
                }
                forceQuirks = true;
                systemIdentifier = strBufToString();
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        doctypesystemidentifierdoublequotedloop_end:;
          [[fallthrough]];
        }
        case AFTER_DOCTYPE_SYSTEM_IDENTIFIER: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '>': {
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctypeWithoutQuirks();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_BREAK(afterdoctypesystemidentifierloop);
              }
            }
          }
        afterdoctypesystemidentifierloop_end:;
          [[fallthrough]];
        }
        case BOGUS_DOCTYPE: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '>': {
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              default: {
                continue;
              }
            }
          }
        }
        case DOCTYPE_YSTEM: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            if (index < 5) {
              char16_t folded = c;
              if (c >= 'A' && c <= 'Z') {
                folded += 0x20;
              }
              if (folded != nsHtml5Tokenizer::YSTEM[index]) {
                bogusDoctype();
                reconsume = true;
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              index++;
              NS_HTML5_CONTINUE(stateloop);
            } else {
              reconsume = true;
              state =
                  P::transition(mViewSource.get(),
                                nsHtml5Tokenizer::AFTER_DOCTYPE_SYSTEM_KEYWORD,
                                reconsume, pos);
              NS_HTML5_BREAK(doctypeystemloop);
            }
          }
        doctypeystemloop_end:;
          [[fallthrough]];
        }
        case AFTER_DOCTYPE_SYSTEM_KEYWORD: {
          for (;;) {
            if (reconsume) {
              reconsume = false;
            } else {
              if (++pos == endPos) {
                NS_HTML5_BREAK(stateloop);
              }
              c = P::checkChar(this, buf, pos);
            }
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::BEFORE_DOCTYPE_SYSTEM_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::BEFORE_DOCTYPE_SYSTEM_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_BREAK(afterdoctypesystemkeywordloop);
              }
              case '\"': {
                if (P::reportErrors) {
                  errNoSpaceBetweenDoctypeSystemKeywordAndQuote();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\'': {
                if (P::reportErrors) {
                  errNoSpaceBetweenDoctypeSystemKeywordAndQuote();
                }
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errExpectedPublicId();
                }
                forceQuirks = true;
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        afterdoctypesystemkeywordloop_end:;
          [[fallthrough]];
        }
        case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\r': {
                P::silentCarriageReturn(this);
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                P::silentLineFeed(this);
                [[fallthrough]];
              }
              case ' ':
              case '\t':
              case '\f': {
                continue;
              }
              case '\"': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\'': {
                clearStrBufBeforeUse();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED,
                    reconsume, pos);
                NS_HTML5_BREAK(beforedoctypesystemidentifierloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errExpectedSystemId();
                }
                forceQuirks = true;
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              default: {
                bogusDoctype();
                state = P::transition(mViewSource.get(),
                                      nsHtml5Tokenizer::BOGUS_DOCTYPE,
                                      reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
            }
          }
        beforedoctypesystemidentifierloop_end:;
          [[fallthrough]];
        }
        case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\'': {
                systemIdentifier = strBufToString();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_DOCTYPE_SYSTEM_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errGtInSystemId();
                }
                forceQuirks = true;
                systemIdentifier = strBufToString();
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        }
        case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\'': {
                publicIdentifier = strBufToString();
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::AFTER_DOCTYPE_PUBLIC_IDENTIFIER,
                    reconsume, pos);
                NS_HTML5_CONTINUE(stateloop);
              }
              case '>': {
                if (P::reportErrors) {
                  errGtInPublicId();
                }
                forceQuirks = true;
                publicIdentifier = strBufToString();
                emitDoctypeToken(pos);
                state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                      reconsume, pos);
                if (shouldSuspend) {
                  NS_HTML5_BREAK(stateloop);
                }
                NS_HTML5_CONTINUE(stateloop);
              }
              case '\r': {
                appendStrBufCarriageReturn<P>();
                NS_HTML5_BREAK(stateloop);
              }
              case '\n': {
                appendStrBufLineFeed<P>();
                continue;
              }
              case '\0': {
                c = 0xfffd;
                [[fallthrough]];
              }
              default: {
                appendStrBuf(c);
                continue;
              }
            }
          }
        }
        case PROCESSING_INSTRUCTION: {
          for (;;) {
            if (++pos == endPos) {
              NS_HTML5_BREAK(stateloop);
            }
            c = P::checkChar(this, buf, pos);
            switch (c) {
              case '\?': {
                state = P::transition(
                    mViewSource.get(),
                    nsHtml5Tokenizer::PROCESSING_INSTRUCTION_QUESTION_MARK,
                    reconsume, pos);
                NS_HTML5_BREAK(processinginstructionloop);
              }
              default: {
                continue;
              }
            }
          }
        processinginstructionloop_end:;
          [[fallthrough]];
        }
        case PROCESSING_INSTRUCTION_QUESTION_MARK: {
          if (++pos == endPos) {
            NS_HTML5_BREAK(stateloop);
          }
          c = P::checkChar(this, buf, pos);
          switch (c) {
            case '>': {
              state = P::transition(mViewSource.get(), nsHtml5Tokenizer::DATA,
                                    reconsume, pos);
              suspendIfRequestedAfterCurrentNonTextToken();
              if (shouldSuspend) {
                NS_HTML5_BREAK(stateloop);
              }
              NS_HTML5_CONTINUE(stateloop);
            }
            default: {
              state = P::transition(mViewSource.get(),
                                    nsHtml5Tokenizer::PROCESSING_INSTRUCTION,
                                    reconsume, pos);
              NS_HTML5_CONTINUE(stateloop);
            }
          }
        }
      }
    }
  stateloop_end:;
    flushChars(buf, pos);
    stateSave = state;
    returnStateSave = returnState;
    return pos;
  }

  void initDoctypeFields();
  template <class P>
  inline void adjustDoubleHyphenAndAppendToStrBufCarriageReturn() {
    P::silentCarriageReturn(this);
    adjustDoubleHyphenAndAppendToStrBufAndErr('\n', false);
  }

  template <class P>
  inline void adjustDoubleHyphenAndAppendToStrBufLineFeed() {
    P::silentLineFeed(this);
    adjustDoubleHyphenAndAppendToStrBufAndErr('\n', false);
  }

  template <class P>
  inline void appendStrBufLineFeed() {
    P::silentLineFeed(this);
    appendStrBuf('\n');
  }

  template <class P>
  inline void appendStrBufCarriageReturn() {
    P::silentCarriageReturn(this);
    appendStrBuf('\n');
  }

  template <class P>
  inline void emitCarriageReturn(char16_t* buf, int32_t pos) {
    P::silentCarriageReturn(this);
    flushChars(buf, pos);
    tokenHandler->characters(nsHtml5Tokenizer::LF, 0, 1);
    cstart = INT32_MAX;
  }

  void emitReplacementCharacter(char16_t* buf, int32_t pos);
  void maybeEmitReplacementCharacter(char16_t* buf, int32_t pos);
  void emitPlaintextReplacementCharacter(char16_t* buf, int32_t pos);
  inline void setAdditionalAndRememberAmpersandLocation(char16_t add) {
    additional = add;
  }

  void bogusDoctype();
  void bogusDoctypeWithoutQuirks();
  void handleNcrValue(int32_t returnState);

 public:
  void eof();

 private:
  void emitDoctypeToken(int32_t pos);
  inline void suspendIfRequestedAfterCurrentNonTextToken() {
    if (suspendAfterCurrentNonTextToken) {
      suspendAfterCurrentNonTextToken = false;
      shouldSuspend = true;
    }
  }

  void suspendAfterCurrentTokenIfNotInText();
  bool suspensionAfterCurrentNonTextTokenPending();

 public:
  bool internalEncodingDeclaration(nsHtml5String internalCharset);

 private:
  inline void emitOrAppendTwo(const char16_t* val, int32_t returnState) {
    if ((returnState & DATA_AND_RCDATA_MASK)) {
      appendStrBuf(val[0]);
      appendStrBuf(val[1]);
    } else {
      tokenHandler->characters(val, 0, 2);
    }
  }

  inline void emitOrAppendOne(const char16_t* val, int32_t returnState) {
    if ((returnState & DATA_AND_RCDATA_MASK)) {
      appendStrBuf(val[0]);
    } else {
      tokenHandler->characters(val, 0, 1);
    }
  }

 public:
  void end();
  inline void requestSuspension() { shouldSuspend = true; }

  inline bool isInDataState() { return (stateSave == DATA); }

  void resetToDataState();
  void loadState(nsHtml5Tokenizer* other);
  void initializeWithoutStarting();
  void setEncodingDeclarationHandler(
      nsHtml5StreamParser* encodingDeclarationHandler);
  ~nsHtml5Tokenizer();
  static void initializeStatics();
  static void releaseStatics();

#include "nsHtml5TokenizerHSupplement.h"
};

#endif
