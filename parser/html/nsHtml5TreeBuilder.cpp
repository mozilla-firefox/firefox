/*
 * Copyright (c) 2007 Henri Sivonen
 * Copyright (c) 2007-2017 Mozilla Foundation
 * Portions of comments Copyright 2004-2008 Apple Computer, Inc., Mozilla
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
 * Please edit TreeBuilder.java instead and regenerate.
 */

#define nsHtml5TreeBuilder_cpp__

#include "nsHtml5AttributeName.h"
#include "nsHtml5ElementName.h"
#include "nsHtml5Tokenizer.h"
#include "nsHtml5StackNode.h"
#include "nsHtml5UTF16Buffer.h"
#include "nsHtml5StateSnapshot.h"
#include "nsHtml5Portability.h"

#include "nsHtml5TreeBuilder.h"

char16_t nsHtml5TreeBuilder::REPLACEMENT_CHARACTER[] = {0xfffd};
static const char* const QUIRKY_PUBLIC_IDS_DATA[] = {
    "+//silmaril//dtd html pro v0r11 19970101//",
    "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
    "-//as//dtd html 3.0 aswedit + extensions//",
    "-//ietf//dtd html 2.0 level 1//",
    "-//ietf//dtd html 2.0 level 2//",
    "-//ietf//dtd html 2.0 strict level 1//",
    "-//ietf//dtd html 2.0 strict level 2//",
    "-//ietf//dtd html 2.0 strict//",
    "-//ietf//dtd html 2.0//",
    "-//ietf//dtd html 2.1e//",
    "-//ietf//dtd html 3.0//",
    "-//ietf//dtd html 3.2 final//",
    "-//ietf//dtd html 3.2//",
    "-//ietf//dtd html 3//",
    "-//ietf//dtd html level 0//",
    "-//ietf//dtd html level 1//",
    "-//ietf//dtd html level 2//",
    "-//ietf//dtd html level 3//",
    "-//ietf//dtd html strict level 0//",
    "-//ietf//dtd html strict level 1//",
    "-//ietf//dtd html strict level 2//",
    "-//ietf//dtd html strict level 3//",
    "-//ietf//dtd html strict//",
    "-//ietf//dtd html//",
    "-//metrius//dtd metrius presentational//",
    "-//microsoft//dtd internet explorer 2.0 html strict//",
    "-//microsoft//dtd internet explorer 2.0 html//",
    "-//microsoft//dtd internet explorer 2.0 tables//",
    "-//microsoft//dtd internet explorer 3.0 html strict//",
    "-//microsoft//dtd internet explorer 3.0 html//",
    "-//microsoft//dtd internet explorer 3.0 tables//",
    "-//netscape comm. corp.//dtd html//",
    "-//netscape comm. corp.//dtd strict html//",
    "-//o'reilly and associates//dtd html 2.0//",
    "-//o'reilly and associates//dtd html extended 1.0//",
    "-//o'reilly and associates//dtd html extended relaxed 1.0//",
    "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html "
    "4.0//",
    "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
    "-//spyglass//dtd html 2.0 extended//",
    "-//sq//dtd html 2.0 hotmetal + extensions//",
    "-//sun microsystems corp.//dtd hotjava html//",
    "-//sun microsystems corp.//dtd hotjava strict html//",
    "-//w3c//dtd html 3 1995-03-24//",
    "-//w3c//dtd html 3.2 draft//",
    "-//w3c//dtd html 3.2 final//",
    "-//w3c//dtd html 3.2//",
    "-//w3c//dtd html 3.2s draft//",
    "-//w3c//dtd html 4.0 frameset//",
    "-//w3c//dtd html 4.0 transitional//",
    "-//w3c//dtd html experimental 19960712//",
    "-//w3c//dtd html experimental 970421//",
    "-//w3c//dtd w3 html//",
    "-//w3o//dtd w3 html 3.0//",
    "-//webtechs//dtd mozilla html 2.0//",
    "-//webtechs//dtd mozilla html//"};
staticJArray<const char*, int32_t> nsHtml5TreeBuilder::QUIRKY_PUBLIC_IDS = {
    QUIRKY_PUBLIC_IDS_DATA, std::size(QUIRKY_PUBLIC_IDS_DATA)};
void nsHtml5TreeBuilder::setKeepBuffer(bool keepBuffer) {
  this->keepBuffer = keepBuffer;
}

bool nsHtml5TreeBuilder::dropBufferIfLongerThan(int32_t length) {
  if (charBuffer.length > length) {
    charBuffer = nullptr;
    return true;
  }
  return false;
}

void nsHtml5TreeBuilder::startTokenization(nsHtml5Tokenizer* self) {
  tokenizer = self;
  stackNodes = jArray<nsHtml5StackNode*, int32_t>::newJArray(64);
  stack = jArray<nsHtml5StackNode*, int32_t>::newJArray(64);
  templateModeStack = jArray<int32_t, int32_t>::newJArray(64);
  listOfActiveFormattingElements =
      jArray<nsHtml5StackNode*, int32_t>::newJArray(64);
  needToDropLF = false;
  mode = INITIAL;
  originalMode = INITIAL;
  templateModePtr = -1;
  stackNodesIdx = 0;
  numStackNodes = 0;
  currentPtr = -1;
  listPtr = -1;
  formPointer = nullptr;
  headPointer = nullptr;
  start(fragment);
  charBufferLen = 0;
  if (!keepBuffer) {
    charBuffer = nullptr;
  }
  framesetOk = true;
  if (fragment) {
    nsIContentHandle* elt;
    if (contextNode) {
      elt = contextNode;
    } else {
      elt = createHtmlElementSetAsRoot(tokenizer->emptyAttributes());
    }
    if (contextNamespace == kNameSpaceID_SVG) {
      nsHtml5ElementName* elementName = nsHtml5ElementName::ELT_SVG;
      if (nsGkAtoms::title == contextName || nsGkAtoms::desc == contextName ||
          nsGkAtoms::foreignObject == contextName) {
        elementName = nsHtml5ElementName::ELT_FOREIGNOBJECT;
      }
      nsHtml5StackNode* node =
          createStackNode(elementName, elementName->getCamelCaseName(), elt);
      currentPtr++;
      stack[currentPtr] = node;
      tokenizer->setState(nsHtml5Tokenizer::DATA);
      mode = FRAMESET_OK;
    } else if (contextNamespace == kNameSpaceID_MathML) {
      nsHtml5ElementName* elementName = nsHtml5ElementName::ELT_MATH;
      if (nsGkAtoms::mi == contextName || nsGkAtoms::mo == contextName ||
          nsGkAtoms::mn == contextName || nsGkAtoms::ms == contextName ||
          nsGkAtoms::mtext == contextName) {
        elementName = nsHtml5ElementName::ELT_MTEXT;
      } else if (nsGkAtoms::annotation_xml == contextName) {
        elementName = nsHtml5ElementName::ELT_ANNOTATION_XML;
      }
      nsHtml5StackNode* node =
          createStackNode(elementName, elt, elementName->getName(), false);
      currentPtr++;
      stack[currentPtr] = node;
      tokenizer->setState(nsHtml5Tokenizer::DATA);
      mode = FRAMESET_OK;
    } else {
      nsHtml5StackNode* node =
          createStackNode(nsHtml5ElementName::ELT_HTML, elt);
      currentPtr++;
      stack[currentPtr] = node;
      if (nsGkAtoms::_template == contextName) {
        pushTemplateMode(IN_TEMPLATE);
      }
      resetTheInsertionMode();
      formPointer = getFormPointerForContext(contextNode);
      if (nsGkAtoms::title == contextName ||
          nsGkAtoms::textarea == contextName) {
        tokenizer->setState(nsHtml5Tokenizer::RCDATA);
      } else if (nsGkAtoms::style == contextName ||
                 nsGkAtoms::xmp == contextName ||
                 nsGkAtoms::iframe == contextName ||
                 nsGkAtoms::noembed == contextName ||
                 nsGkAtoms::noframes == contextName ||
                 (scriptingEnabled && nsGkAtoms::noscript == contextName)) {
        tokenizer->setState(nsHtml5Tokenizer::RAWTEXT);
      } else if (nsGkAtoms::plaintext == contextName) {
        tokenizer->setState(nsHtml5Tokenizer::PLAINTEXT);
      } else if (nsGkAtoms::script == contextName) {
        tokenizer->setState(nsHtml5Tokenizer::SCRIPT_DATA);
      } else {
        tokenizer->setState(nsHtml5Tokenizer::DATA);
      }
    }
  } else {
    mode = INITIAL;
    if (tokenizer->isViewingXmlSource()) {
      nsIContentHandle* elt = createElement(
          kNameSpaceID_SVG, nsGkAtoms::svg, tokenizer->emptyAttributes(),
          nullptr, svgCreator(NS_NewSVGSVGElement));
      nsHtml5StackNode* node =
          createStackNode(nsHtml5ElementName::ELT_SVG, nsGkAtoms::svg, elt);
      currentPtr++;
      stack[currentPtr] = node;
    }
  }
}

void nsHtml5TreeBuilder::doctype(nsAtom* name, nsHtml5String publicIdentifier,
                                 nsHtml5String systemIdentifier,
                                 bool forceQuirks) {
  needToDropLF = false;
  if (!isInForeign() && mode == INITIAL) {
    nsHtml5String emptyString = nsHtml5Portability::newEmptyString();
    appendDoctypeToDocument(!name ? nsGkAtoms::_empty : name,
                            !publicIdentifier ? emptyString : publicIdentifier,
                            !systemIdentifier ? emptyString : systemIdentifier);
    emptyString.Release();
    if (isQuirky(name, publicIdentifier, systemIdentifier, forceQuirks)) {
      errQuirkyDoctype();
      documentModeInternal(QUIRKS_MODE, publicIdentifier, systemIdentifier);
    } else if (isAlmostStandards(publicIdentifier, systemIdentifier)) {
      errAlmostStandardsDoctype();
      documentModeInternal(ALMOST_STANDARDS_MODE, publicIdentifier,
                           systemIdentifier);
    } else {
      documentModeInternal(STANDARDS_MODE, publicIdentifier, systemIdentifier);
    }
    mode = BEFORE_HTML;
    return;
  }
  errStrayDoctype();
  return;
}

void nsHtml5TreeBuilder::comment(char16_t* buf, int32_t start, int32_t length) {
  needToDropLF = false;
  if (!isInForeign()) {
    switch (mode) {
      case INITIAL:
      case BEFORE_HTML:
      case AFTER_AFTER_BODY:
      case AFTER_AFTER_FRAMESET: {
        appendCommentToDocument(buf, start, length);
        return;
      }
      case AFTER_BODY: {
        flushCharacters();
        appendComment(stack[0]->node, buf, start, length);
        return;
      }
      default: {
        break;
      }
    }
  }
  flushCharacters();
  appendComment(stack[currentPtr]->node, buf, start, length);
  return;
}

void nsHtml5TreeBuilder::characters(const char16_t* buf, int32_t start,
                                    int32_t length) {
  if (tokenizer->isViewingXmlSource()) {
    return;
  }
  if (needToDropLF) {
    needToDropLF = false;
    if (buf[start] == '\n') {
      start++;
      length--;
      if (!length) {
        return;
      }
    }
  }
  switch (mode) {
    case IN_BODY:
    case IN_CELL:
    case IN_CAPTION: {
      if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
        reconstructTheActiveFormattingElements();
      }
      [[fallthrough]];
    }
    case TEXT: {
      accumulateCharacters(buf, start, length);
      return;
    }
    case IN_TABLE:
    case IN_TABLE_BODY:
    case IN_ROW: {
      accumulateCharactersForced(buf, start, length);
      return;
    }
    default: {
      int32_t end = start + length;
      for (int32_t i = start; i < end; i++) {
        switch (buf[i]) {
          case ' ':
          case '\t':
          case '\n':
          case '\r':
          case '\f': {
            switch (mode) {
              case INITIAL:
              case BEFORE_HTML:
              case BEFORE_HEAD: {
                start = i + 1;
                continue;
              }
              case IN_HEAD:
              case IN_HEAD_NOSCRIPT:
              case AFTER_HEAD:
              case IN_COLUMN_GROUP:
              case IN_FRAMESET:
              case AFTER_FRAMESET: {
                continue;
              }
              case FRAMESET_OK:
              case IN_TEMPLATE:
              case IN_BODY:
              case IN_CELL:
              case IN_CAPTION: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
                  flushCharacters();
                  reconstructTheActiveFormattingElements();
                }
                NS_HTML5_BREAK(charactersloop);
              }
              case IN_SELECT:
              case IN_SELECT_IN_TABLE: {
                MOZ_ASSERT(!noInSelectMode);
                NS_HTML5_BREAK(charactersloop);
              }
              case IN_TABLE:
              case IN_TABLE_BODY:
              case IN_ROW: {
                accumulateCharactersForced(buf, i, 1);
                start = i + 1;
                continue;
              }
              case AFTER_BODY:
              case AFTER_AFTER_BODY:
              case AFTER_AFTER_FRAMESET: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                flushCharacters();
                reconstructTheActiveFormattingElements();
                continue;
              }
            }
            MOZ_FALLTHROUGH_ASSERT();
          }
          default: {
            switch (mode) {
              case INITIAL: {
                documentModeInternal(QUIRKS_MODE, nullptr, nullptr);
                mode = BEFORE_HTML;
                i--;
                continue;
              }
              case BEFORE_HTML: {
                appendHtmlElementToDocumentAndPush();
                mode = BEFORE_HEAD;
                i--;
                continue;
              }
              case BEFORE_HEAD: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                flushCharacters();
                appendToCurrentNodeAndPushHeadElement(
                    nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
                mode = IN_HEAD;
                i--;
                continue;
              }
              case IN_HEAD: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                flushCharacters();
                pop();
                mode = AFTER_HEAD;
                i--;
                continue;
              }
              case IN_HEAD_NOSCRIPT: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                errNonSpaceInNoscriptInHead();
                flushCharacters();
                pop();
                mode = IN_HEAD;
                i--;
                continue;
              }
              case AFTER_HEAD: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                flushCharacters();
                appendToCurrentNodeAndPushBodyElement();
                mode = FRAMESET_OK;
                i--;
                continue;
              }
              case FRAMESET_OK: {
                framesetOk = false;
                mode = IN_BODY;
                i--;
                continue;
              }
              case IN_TEMPLATE:
              case IN_BODY:
              case IN_CELL:
              case IN_CAPTION: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
                  flushCharacters();
                  reconstructTheActiveFormattingElements();
                }
                NS_HTML5_BREAK(charactersloop);
              }
              case IN_TABLE:
              case IN_TABLE_BODY:
              case IN_ROW: {
                accumulateCharactersForced(buf, i, 1);
                start = i + 1;
                continue;
              }
              case IN_COLUMN_GROUP: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                  start = i;
                }
                if (!currentPtr || stack[currentPtr]->getGroup() ==
                                       nsHtml5TreeBuilder::TEMPLATE) {
                  errNonSpaceInColgroupInFragment();
                  start = i + 1;
                  continue;
                }
                flushCharacters();
                pop();
                mode = IN_TABLE;
                i--;
                continue;
              }
              case IN_SELECT:
              case IN_SELECT_IN_TABLE: {
                MOZ_ASSERT(!noInSelectMode);
                NS_HTML5_BREAK(charactersloop);
              }
              case AFTER_BODY: {
                errNonSpaceAfterBody();

                mode = framesetOk ? FRAMESET_OK : IN_BODY;
                i--;
                continue;
              }
              case IN_FRAMESET: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                }
                errNonSpaceInFrameset();
                start = i + 1;
                continue;
              }
              case AFTER_FRAMESET: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                }
                errNonSpaceAfterFrameset();
                start = i + 1;
                continue;
              }
              case AFTER_AFTER_BODY: {
                errNonSpaceInTrailer();
                mode = framesetOk ? FRAMESET_OK : IN_BODY;
                i--;
                continue;
              }
              case AFTER_AFTER_FRAMESET: {
                if (start < i) {
                  accumulateCharacters(buf, start, i - start);
                }
                errNonSpaceInTrailer();
                start = i + 1;
                continue;
              }
            }
          }
        }
      }
    charactersloop_end:;
      if (start < end) {
        accumulateCharacters(buf, start, end - start);
      }
    }
  }
}

void nsHtml5TreeBuilder::zeroOriginatingReplacementCharacter() {
  if (mode == TEXT) {
    accumulateCharacters(REPLACEMENT_CHARACTER, 0, 1);
    return;
  }
  if (currentPtr >= 0) {
    if (isSpecialParentInForeign(stack[currentPtr])) {
      return;
    }
    accumulateCharacters(REPLACEMENT_CHARACTER, 0, 1);
  }
}

void nsHtml5TreeBuilder::zeroOrReplacementCharacter() {
  zeroOriginatingReplacementCharacter();
}

void nsHtml5TreeBuilder::eof() {
  flushCharacters();
  for (;;) {
    switch (mode) {
      case INITIAL: {
        documentModeInternal(QUIRKS_MODE, nullptr, nullptr);
        mode = BEFORE_HTML;
        continue;
      }
      case BEFORE_HTML: {
        appendHtmlElementToDocumentAndPush();
        mode = BEFORE_HEAD;
        continue;
      }
      case BEFORE_HEAD: {
        appendToCurrentNodeAndPushHeadElement(
            nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
        mode = IN_HEAD;
        continue;
      }
      case IN_HEAD: {
        while (currentPtr > 0) {
          popOnEof();
        }
        mode = AFTER_HEAD;
        continue;
      }
      case IN_HEAD_NOSCRIPT: {
        while (currentPtr > 1) {
          popOnEof();
        }
        mode = IN_HEAD;
        continue;
      }
      case AFTER_HEAD: {
        appendToCurrentNodeAndPushBodyElement();
        mode = IN_BODY;
        continue;
      }
      case IN_TABLE_BODY:
      case IN_ROW:
      case IN_TABLE:
      case IN_SELECT_IN_TABLE:
      case IN_SELECT:
      case IN_COLUMN_GROUP:
      case FRAMESET_OK:
      case IN_CAPTION:
      case IN_CELL:
      case IN_BODY: {
        if (isTemplateModeStackEmpty()) {
          NS_HTML5_BREAK(eofloop);
        }
        [[fallthrough]];
      }
      case IN_TEMPLATE: {
        int32_t eltPos = findLast(nsGkAtoms::_template);
        if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
          MOZ_ASSERT(fragment);
          NS_HTML5_BREAK(eofloop);
        }
        if (MOZ_UNLIKELY(mViewSource)) {
          errListUnclosedStartTags(0);
        }
        while (currentPtr >= eltPos) {
          pop();
        }
        clearTheListOfActiveFormattingElementsUpToTheLastMarker();
        popTemplateMode();
        resetTheInsertionMode();
        continue;
      }
      case TEXT: {
        if (originalMode == AFTER_HEAD) {
          popOnEof();
        }
        popOnEof();
        mode = originalMode;
        continue;
      }
      case IN_FRAMESET: {
        NS_HTML5_BREAK(eofloop);
      }
      case AFTER_BODY:
      case AFTER_FRAMESET:
      case AFTER_AFTER_BODY:
      case AFTER_AFTER_FRAMESET:
      default: {
        NS_HTML5_BREAK(eofloop);
      }
    }
  }
eofloop_end:;
  while (currentPtr > 0) {
    popOnEof();
  }
  if (!fragment) {
    popOnEof();
  }
}

void nsHtml5TreeBuilder::endTokenization() {
  formPointer = nullptr;
  headPointer = nullptr;
  contextName = nullptr;
  contextNode = nullptr;
  templateModeStack = nullptr;
  if (stack) {
    while (currentPtr > -1) {
      stack[currentPtr]->release(this);
      currentPtr--;
    }
    stack = nullptr;
  }
  if (listOfActiveFormattingElements) {
    while (listPtr > -1) {
      if (listOfActiveFormattingElements[listPtr]) {
        listOfActiveFormattingElements[listPtr]->release(this);
      }
      listPtr--;
    }
    listOfActiveFormattingElements = nullptr;
  }
  if (stackNodes) {
    for (int32_t i = 0; i < numStackNodes; i++) {
      MOZ_ASSERT(stackNodes[i]->isUnused());
      delete stackNodes[i];
    }
    numStackNodes = 0;
    stackNodesIdx = 0;
    stackNodes = nullptr;
  }
  if (!keepBuffer) {
    charBuffer = nullptr;
  }
  end();
}

void nsHtml5TreeBuilder::startTag(nsHtml5ElementName* elementName,
                                  nsHtml5HtmlAttributes* attributes,
                                  bool selfClosing) {
  flushCharacters();
  int32_t eltPos;
  needToDropLF = false;
starttagloop:
  for (;;) {
    int32_t group = elementName->getGroup();
    nsAtom* name = elementName->getName();
    if (isInForeign()) {
      nsHtml5StackNode* currentNode = stack[currentPtr];
      int32_t currNs = currentNode->ns;
      if (!(currentNode->isHtmlIntegrationPoint() ||
            (currNs == kNameSpaceID_MathML &&
             ((currentNode->getGroup() == MI_MO_MN_MS_MTEXT &&
               group != MGLYPH_OR_MALIGNMARK) ||
              (currentNode->getGroup() == ANNOTATION_XML && group == SVG))))) {
        switch (group) {
          case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
          case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
          case BODY:
          case BR:
          case RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR:
          case DD_OR_DT:
          case UL_OR_OL_OR_DL:
          case EMBED:
          case IMG:
          case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6:
          case HEAD:
          case HR:
          case LI:
          case META:
          case NOBR:
          case P:
          case PRE_OR_LISTING:
          case TABLE:
          case FONT: {
            if (!(group == FONT &&
                  !(attributes->contains(nsHtml5AttributeName::ATTR_COLOR) ||
                    attributes->contains(nsHtml5AttributeName::ATTR_FACE) ||
                    attributes->contains(nsHtml5AttributeName::ATTR_SIZE)))) {
              errHtmlStartTagInForeignContext(name);
              if (!fragment) {
                while (!isSpecialParentInForeign(stack[currentPtr])) {
                  popForeign(-1, -1);
                }
                NS_HTML5_CONTINUE(starttagloop);
              }
            }
            [[fallthrough]];
          }
          default: {
            if (kNameSpaceID_SVG == currNs) {
              attributes->adjustForSvg();
              if (selfClosing) {
                appendVoidElementToCurrentMayFosterSVG(elementName, attributes);
                selfClosing = false;
              } else {
                appendToCurrentNodeAndPushElementMayFosterSVG(elementName,
                                                              attributes);
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            } else {
              attributes->adjustForMath();
              if (selfClosing) {
                appendVoidElementToCurrentMayFosterMathML(elementName,
                                                          attributes);
                selfClosing = false;
              } else {
                appendToCurrentNodeAndPushElementMayFosterMathML(elementName,
                                                                 attributes);
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
          }
        }
      }
    }
    switch (mode) {
      case IN_TEMPLATE: {
        switch (group) {
          case COL: {
            popTemplateMode();
            pushTemplateMode(IN_COLUMN_GROUP);
            mode = IN_COLUMN_GROUP;
            continue;
          }
          case CAPTION:
          case COLGROUP:
          case TBODY_OR_THEAD_OR_TFOOT: {
            popTemplateMode();
            pushTemplateMode(IN_TABLE);
            mode = IN_TABLE;
            continue;
          }
          case TR: {
            popTemplateMode();
            pushTemplateMode(IN_TABLE_BODY);
            mode = IN_TABLE_BODY;
            continue;
          }
          case TD_OR_TH: {
            popTemplateMode();
            pushTemplateMode(IN_ROW);
            mode = IN_ROW;
            continue;
          }
          case META: {
            checkMetaCharset(attributes);
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TITLE: {
            startTagTitleInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case BASE:
          case LINK_OR_BASEFONT_OR_BGSOUND: {
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case SCRIPT: {
            startTagScriptInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case NOFRAMES:
          case STYLE: {
            startTagGenericRawText(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TEMPLATE: {
            startTagTemplateInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            popTemplateMode();
            pushTemplateMode(IN_BODY);
            mode = IN_BODY;
            continue;
          }
        }
      }
      case IN_ROW: {
        switch (group) {
          case TD_OR_TH: {
            clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TR));
            appendToCurrentNodeAndPushElement(elementName, attributes);
            mode = IN_CELL;
            insertMarker();
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case CAPTION:
          case COL:
          case COLGROUP:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR: {
            eltPos = findLastOrRoot(nsHtml5TreeBuilder::TR);
            if (!eltPos) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errNoTableRowToClose();
              NS_HTML5_BREAK(starttagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE_BODY;
            continue;
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_TABLE_BODY: {
        switch (group) {
          case TR: {
            clearStackBackTo(
                findLastInTableScopeOrRootTemplateTbodyTheadTfoot());
            appendToCurrentNodeAndPushElement(elementName, attributes);
            mode = IN_ROW;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TD_OR_TH: {
            errStartTagInTableBody(name);
            clearStackBackTo(
                findLastInTableScopeOrRootTemplateTbodyTheadTfoot());
            appendToCurrentNodeAndPushElement(
                nsHtml5ElementName::ELT_TR,
                nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
            mode = IN_ROW;
            continue;
          }
          case CAPTION:
          case COL:
          case COLGROUP:
          case TBODY_OR_THEAD_OR_TFOOT: {
            eltPos = findLastInTableScopeOrRootTemplateTbodyTheadTfoot();
            if (!eltPos || stack[eltPos]->getGroup() == TEMPLATE) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayStartTag(name);
              NS_HTML5_BREAK(starttagloop);
            } else {
              clearStackBackTo(eltPos);
              pop();
              mode = IN_TABLE;
              continue;
            }
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_TABLE: {
        for (;;) {
          switch (group) {
            case CAPTION: {
              clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TABLE));
              insertMarker();
              appendToCurrentNodeAndPushElement(elementName, attributes);
              mode = IN_CAPTION;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case COLGROUP: {
              clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TABLE));
              appendToCurrentNodeAndPushElement(elementName, attributes);
              mode = IN_COLUMN_GROUP;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case COL: {
              clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TABLE));
              appendToCurrentNodeAndPushElement(
                  nsHtml5ElementName::ELT_COLGROUP,
                  nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
              mode = IN_COLUMN_GROUP;
              NS_HTML5_CONTINUE(starttagloop);
            }
            case TBODY_OR_THEAD_OR_TFOOT: {
              clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TABLE));
              appendToCurrentNodeAndPushElement(elementName, attributes);
              mode = IN_TABLE_BODY;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case TR:
            case TD_OR_TH: {
              clearStackBackTo(findLastOrRoot(nsHtml5TreeBuilder::TABLE));
              appendToCurrentNodeAndPushElement(
                  nsHtml5ElementName::ELT_TBODY,
                  nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
              mode = IN_TABLE_BODY;
              NS_HTML5_CONTINUE(starttagloop);
            }
            case TEMPLATE: {
              NS_HTML5_BREAK(intableloop);
            }
            case TABLE: {
              errTableSeenWhileTableOpen();
              eltPos = findLastInTableScope(name);
              if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                MOZ_ASSERT(fragment || isTemplateContents());
                NS_HTML5_BREAK(starttagloop);
              }
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(nsGkAtoms::table)) {
                errNoCheckUnclosedElementsOnStack();
              }
              while (currentPtr >= eltPos) {
                pop();
              }
              resetTheInsertionMode();
              NS_HTML5_CONTINUE(starttagloop);
            }
            case SCRIPT: {
              appendToCurrentNodeAndPushElement(elementName, attributes);
              originalMode = mode;
              mode = TEXT;
              tokenizer->setStateAndEndTagExpectation(
                  nsHtml5Tokenizer::SCRIPT_DATA, elementName);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case STYLE: {
              appendToCurrentNodeAndPushElement(elementName, attributes);
              originalMode = mode;
              mode = TEXT;
              tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                                      elementName);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case INPUT: {
              errStartTagInTable(name);
              if (!nsHtml5Portability::
                      lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                          "hidden", attributes->getValue(
                                        nsHtml5AttributeName::ATTR_TYPE))) {
                NS_HTML5_BREAK(intableloop);
              }
              appendVoidInputToCurrent(attributes, formPointer);
              selfClosing = false;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case FORM: {
              if (!!formPointer || isTemplateContents()) {
                errFormWhenFormOpen();
                NS_HTML5_BREAK(starttagloop);
              } else {
                errStartTagInTable(name);
                appendVoidFormToCurrent(attributes);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
            }
            default: {
              errStartTagInTable(name);
              NS_HTML5_BREAK(intableloop);
            }
          }
        }
      intableloop_end:;
        [[fallthrough]];
      }
      case IN_CAPTION: {
        switch (group) {
          case CAPTION:
          case COL:
          case COLGROUP:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR:
          case TD_OR_TH: {
            eltPos = findLastInTableScope(nsGkAtoms::caption);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayStartTag(name);
              NS_HTML5_BREAK(starttagloop);
            }
            generateImpliedEndTags();
            if (!!MOZ_UNLIKELY(mViewSource) && currentPtr != eltPos) {
              errNoCheckUnclosedElementsOnStack();
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
            mode = IN_TABLE;
            continue;
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_CELL: {
        switch (group) {
          case CAPTION:
          case COL:
          case COLGROUP:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR:
          case TD_OR_TH: {
            eltPos = findLastInTableScopeTdTh();
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errNoCellToClose();
              NS_HTML5_BREAK(starttagloop);
            } else {
              closeTheCell(eltPos);
              continue;
            }
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case FRAMESET_OK: {
        switch (group) {
          case FRAMESET: {
            if (mode == FRAMESET_OK) {
              if (!currentPtr || stack[1]->getGroup() != BODY) {
                MOZ_ASSERT(fragment || isTemplateContents());
                errStrayStartTag(name);
                NS_HTML5_BREAK(starttagloop);
              } else {
                errFramesetStart();
                detachFromParent(stack[1]->node);
                while (currentPtr > 0) {
                  pop();
                }
                appendToCurrentNodeAndPushElement(elementName, attributes);
                mode = IN_FRAMESET;
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
            } else {
              errStrayStartTag(name);
              NS_HTML5_BREAK(starttagloop);
            }
          }
          case PRE_OR_LISTING:
          case LI:
          case DD_OR_DT:
          case BUTTON:
          case MARQUEE_OR_APPLET:
          case OBJECT:
          case TABLE:
          case AREA_OR_WBR:
          case KEYGEN:
          case BR:
          case EMBED:
          case IMG:
          case INPUT:
          case HR:
          case TEXTAREA:
          case XMP:
          case IFRAME:
          case SELECT: {
            if (mode == FRAMESET_OK &&
                !(group == INPUT &&
                  nsHtml5Portability::
                      lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                          "hidden", attributes->getValue(
                                        nsHtml5AttributeName::ATTR_TYPE)))) {
              framesetOk = false;
              mode = IN_BODY;
            }
            [[fallthrough]];
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_BODY: {
        for (;;) {
          switch (group) {
            case HTML: {
              errStrayStartTag(name);
              if (!fragment && !isTemplateContents()) {
                addAttributesToHtml(attributes);
                attributes = nullptr;
              }
              NS_HTML5_BREAK(starttagloop);
            }
            case BASE:
            case LINK_OR_BASEFONT_OR_BGSOUND:
            case META:
            case STYLE:
            case SCRIPT:
            case TITLE:
            case TEMPLATE: {
              NS_HTML5_BREAK(inbodyloop);
            }
            case BODY: {
              if (!currentPtr || stack[1]->getGroup() != BODY ||
                  isTemplateContents()) {
                MOZ_ASSERT(fragment || isTemplateContents());
                errStrayStartTag(name);
                NS_HTML5_BREAK(starttagloop);
              }
              errFooSeenWhenFooOpen(name);
              framesetOk = false;
              if (mode == FRAMESET_OK) {
                mode = IN_BODY;
              }
              if (addAttributesToBody(attributes)) {
                attributes = nullptr;
              }
              NS_HTML5_BREAK(starttagloop);
            }
            case P:
            case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
            case UL_OR_OL_OR_DL:
            case ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY: {
              implicitlyCloseP();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6: {
              implicitlyCloseP();
              if (stack[currentPtr]->getGroup() ==
                  H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6) {
                errHeadingWhenHeadingOpen();
                pop();
              }
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case FIELDSET: {
              implicitlyCloseP();
              appendToCurrentNodeAndPushElementMayFoster(
                  elementName, attributes, formPointer);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case PRE_OR_LISTING: {
              implicitlyCloseP();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              needToDropLF = true;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case FORM: {
              if (!!formPointer && !isTemplateContents()) {
                errFormWhenFormOpen();
                NS_HTML5_BREAK(starttagloop);
              } else {
                implicitlyCloseP();
                appendToCurrentNodeAndPushFormElementMayFoster(attributes);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
            }
            case LI:
            case DD_OR_DT: {
              eltPos = currentPtr;
              for (;;) {
                nsHtml5StackNode* node = stack[eltPos];
                if (node->getGroup() == group) {
                  generateImpliedEndTagsExceptFor(node->name);
                  if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
                    errUnclosedElementsImplied(eltPos, name);
                  }
                  while (currentPtr >= eltPos) {
                    pop();
                  }
                  break;
                } else if (!eltPos || (node->isSpecial() &&
                                       (node->ns != kNameSpaceID_XHTML ||
                                        (node->name != nsGkAtoms::p &&
                                         node->name != nsGkAtoms::address &&
                                         node->name != nsGkAtoms::div)))) {
                  break;
                }
                eltPos--;
              }
              implicitlyCloseP();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case PLAINTEXT: {
              implicitlyCloseP();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              tokenizer->setStateAndEndTagExpectation(
                  nsHtml5Tokenizer::PLAINTEXT, elementName);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case A: {
              int32_t activeAPos =
                  findInListOfActiveFormattingElementsContainsBetweenEndAndLastMarker(
                      nsGkAtoms::a);
              if (activeAPos != -1) {
                errFooSeenWhenFooOpen(name);
                nsHtml5StackNode* activeA =
                    listOfActiveFormattingElements[activeAPos];
                activeA->retain();
                adoptionAgencyEndTag(nsGkAtoms::a);
                removeFromStack(activeA);
                activeAPos = findInListOfActiveFormattingElements(activeA);
                if (activeAPos != -1) {
                  removeFromListOfActiveFormattingElements(activeAPos);
                }
                activeA->release(this);
              }
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushFormattingElementMayFoster(elementName,
                                                                   attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
            case FONT: {
              reconstructTheActiveFormattingElements();
              maybeForgetEarlierDuplicateFormattingElement(
                  elementName->getName(), attributes);
              appendToCurrentNodeAndPushFormattingElementMayFoster(elementName,
                                                                   attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case NOBR: {
              reconstructTheActiveFormattingElements();
              if (nsHtml5TreeBuilder::NOT_FOUND_ON_STACK !=
                  findLastInScope(nsGkAtoms::nobr)) {
                errFooSeenWhenFooOpen(name);
                adoptionAgencyEndTag(nsGkAtoms::nobr);
                reconstructTheActiveFormattingElements();
              }
              appendToCurrentNodeAndPushFormattingElementMayFoster(elementName,
                                                                   attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case BUTTON: {
              eltPos = findLastInScope(name);
              if (eltPos != nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                errFooSeenWhenFooOpen(name);
                generateImpliedEndTags();
                if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                  errUnclosedElementsImplied(eltPos, name);
                }
                while (currentPtr >= eltPos) {
                  pop();
                }
                NS_HTML5_CONTINUE(starttagloop);
              } else {
                reconstructTheActiveFormattingElements();
                appendToCurrentNodeAndPushElementMayFoster(
                    elementName, attributes, formPointer);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
            }
            case OBJECT: {
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(
                  elementName, attributes, formPointer);
              insertMarker();
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case MARQUEE_OR_APPLET: {
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              insertMarker();
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case TABLE: {
              if (!quirks) {
                implicitlyCloseP();
              }
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              mode = IN_TABLE;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case BR:
            case EMBED:
            case AREA_OR_WBR:
            case KEYGEN: {
              reconstructTheActiveFormattingElements();
              [[fallthrough]];
            }
            case PARAM_OR_SOURCE_OR_TRACK: {
              appendVoidElementToCurrentMayFoster(elementName, attributes);
              selfClosing = false;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case HR: {
              implicitlyCloseP();
              if (noInSelectMode &&
                  findLastInScope(nsGkAtoms::select) !=
                      nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                generateImpliedEndTags();
                if (!!MOZ_UNLIKELY(mViewSource) &&
                    (findLastInScope(nsGkAtoms::option) !=
                         nsHtml5TreeBuilder::NOT_FOUND_ON_STACK ||
                     findLastInScope(nsGkAtoms::optgroup) !=
                         nsHtml5TreeBuilder::NOT_FOUND_ON_STACK)) {
                  errUnclosedElements(currentPtr, name);
                }
              }
              appendVoidElementToCurrentMayFoster(elementName, attributes);
              selfClosing = false;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case IMAGE: {
              errImage();
              elementName = nsHtml5ElementName::ELT_IMG;
              NS_HTML5_CONTINUE(starttagloop);
            }
            case INPUT: {
              if (noInSelectMode) {
                if (fragment && nsGkAtoms::select == contextName) {
                  errStartTagWithSelectOpen(name);
                  NS_HTML5_BREAK(starttagloop);
                }
                eltPos = findLastInScope(nsGkAtoms::select);
                if (eltPos != nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                  errStartTagWithSelectOpen(name);
                  while (currentPtr >= eltPos) {
                    pop();
                  }
                  resetTheInsertionMode();
                  NS_HTML5_CONTINUE(starttagloop);
                }
              }
              [[fallthrough]];
            }
            case IMG: {
              reconstructTheActiveFormattingElements();
              appendVoidElementToCurrentMayFoster(elementName, attributes,
                                                  formPointer);
              selfClosing = false;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case TEXTAREA: {
              appendToCurrentNodeAndPushElementMayFoster(
                  elementName, attributes, formPointer);
              tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RCDATA,
                                                      elementName);
              originalMode = mode;
              mode = TEXT;
              needToDropLF = true;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case XMP: {
              implicitlyCloseP();
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              originalMode = mode;
              mode = TEXT;
              tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                                      elementName);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case NOSCRIPT: {
              if (!scriptingEnabled) {
                reconstructTheActiveFormattingElements();
                appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                           attributes);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
              [[fallthrough]];
            }
            case NOFRAMES:
            case IFRAME:
            case NOEMBED: {
              startTagGenericRawText(elementName, attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case SELECT: {
              if (noInSelectMode) {
                if (fragment && nsGkAtoms::select == contextName) {
                  errStartSelectWhereEndSelectExpected();
                  NS_HTML5_BREAK(starttagloop);
                }
                eltPos = findLastInScope(nsGkAtoms::select);
                if (eltPos != nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                  errStartSelectWhereEndSelectExpected();
                  while (currentPtr >= eltPos) {
                    pop();
                  }
                  NS_HTML5_BREAK(starttagloop);
                }
                reconstructTheActiveFormattingElements();
                appendToCurrentNodeAndPushElementMayFoster(
                    elementName, attributes, formPointer);
                framesetOk = false;
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(
                  elementName, attributes, formPointer);
              if (!noInSelectMode) {
                switch (mode) {
                  case IN_TABLE:
                  case IN_CAPTION:
                  case IN_COLUMN_GROUP:
                  case IN_TABLE_BODY:
                  case IN_ROW:
                  case IN_CELL: {
                    mode = IN_SELECT_IN_TABLE;
                    break;
                  }
                  default: {
                    mode = IN_SELECT;
                    break;
                  }
                }
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case OPTION: {
              if (noInSelectMode) {
                if (findLastInScope(nsGkAtoms::select) !=
                    nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                  generateImpliedEndTagsExceptFor(nsGkAtoms::optgroup);
                  if (!!MOZ_UNLIKELY(mViewSource) &&
                      findLastInScope(nsGkAtoms::option) !=
                          nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                    errUnclosedElements(findLastInScope(nsGkAtoms::option),
                                        name);
                  }
                } else {
                  if (isCurrent(nsGkAtoms::option)) {
                    pop();
                  }
                }
                reconstructTheActiveFormattingElements();
                appendToCurrentNodeAndPushElement(elementName, attributes);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
              [[fallthrough]];
            }
            case OPTGROUP: {
              if (noInSelectMode) {
                if (findLastInScope(nsGkAtoms::select) !=
                    nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                  generateImpliedEndTags();
                  if (!!MOZ_UNLIKELY(mViewSource) &&
                      (findLastInScope(nsGkAtoms::option) !=
                           nsHtml5TreeBuilder::NOT_FOUND_ON_STACK ||
                       findLastInScope(nsGkAtoms::optgroup) !=
                           nsHtml5TreeBuilder::NOT_FOUND_ON_STACK)) {
                    errUnclosedElements(currentPtr, name);
                  }
                } else if (isCurrent(nsGkAtoms::option)) {
                  pop();
                }
                reconstructTheActiveFormattingElements();
                appendToCurrentNodeAndPushElement(elementName, attributes);
                attributes = nullptr;
                NS_HTML5_BREAK(starttagloop);
              }
              if (isCurrent(nsGkAtoms::option)) {
                pop();
              }
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case RB_OR_RTC: {
              eltPos = findLastInScope(nsGkAtoms::ruby);
              if (eltPos != NOT_FOUND_ON_STACK) {
                generateImpliedEndTags();
              }
              if (eltPos != currentPtr) {
                if (eltPos == NOT_FOUND_ON_STACK) {
                  errStartTagSeenWithoutRuby(name);
                } else {
                  errUnclosedChildrenInRuby();
                }
              }
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case RT_OR_RP: {
              eltPos = findLastInScope(nsGkAtoms::ruby);
              if (eltPos != NOT_FOUND_ON_STACK) {
                generateImpliedEndTagsExceptFor(nsGkAtoms::rtc);
              }
              if (eltPos != currentPtr) {
                if (!isCurrent(nsGkAtoms::rtc)) {
                  if (eltPos == NOT_FOUND_ON_STACK) {
                    errStartTagSeenWithoutRuby(name);
                  } else {
                    errUnclosedChildrenInRuby();
                  }
                }
              }
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case MATH: {
              reconstructTheActiveFormattingElements();
              attributes->adjustForMath();
              if (selfClosing) {
                appendVoidElementToCurrentMayFosterMathML(elementName,
                                                          attributes);
                selfClosing = false;
              } else {
                appendToCurrentNodeAndPushElementMayFosterMathML(elementName,
                                                                 attributes);
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case SVG: {
              reconstructTheActiveFormattingElements();
              attributes->adjustForSvg();
              if (selfClosing) {
                appendVoidElementToCurrentMayFosterSVG(elementName, attributes);
                selfClosing = false;
              } else {
                appendToCurrentNodeAndPushElementMayFosterSVG(elementName,
                                                              attributes);
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case CAPTION:
            case COL:
            case COLGROUP:
            case TBODY_OR_THEAD_OR_TFOOT:
            case TR:
            case TD_OR_TH:
            case FRAME:
            case FRAMESET:
            case HEAD: {
              errStrayStartTag(name);
              NS_HTML5_BREAK(starttagloop);
            }
            case OUTPUT: {
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(
                  elementName, attributes, formPointer);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            default: {
              reconstructTheActiveFormattingElements();
              appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                         attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
          }
        }
      inbodyloop_end:;
        [[fallthrough]];
      }
      case IN_HEAD: {
        for (;;) {
          switch (group) {
            case HTML: {
              errStrayStartTag(name);
              if (!fragment && !isTemplateContents()) {
                addAttributesToHtml(attributes);
                attributes = nullptr;
              }
              NS_HTML5_BREAK(starttagloop);
            }
            case BASE:
            case LINK_OR_BASEFONT_OR_BGSOUND: {
              appendVoidElementToCurrentMayFoster(elementName, attributes);
              selfClosing = false;
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case META: {
              NS_HTML5_BREAK(inheadloop);
            }
            case TITLE: {
              startTagTitleInHead(elementName, attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case NOSCRIPT: {
              if (scriptingEnabled) {
                appendToCurrentNodeAndPushElement(elementName, attributes);
                originalMode = mode;
                mode = TEXT;
                tokenizer->setStateAndEndTagExpectation(
                    nsHtml5Tokenizer::RAWTEXT, elementName);
              } else {
                appendToCurrentNodeAndPushElementMayFoster(elementName,
                                                           attributes);
                mode = IN_HEAD_NOSCRIPT;
              }
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case SCRIPT: {
              startTagScriptInHead(elementName, attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case STYLE:
            case NOFRAMES: {
              startTagGenericRawText(elementName, attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            case HEAD: {
              errFooSeenWhenFooOpen(name);
              NS_HTML5_BREAK(starttagloop);
            }
            case TEMPLATE: {
              startTagTemplateInHead(elementName, attributes);
              attributes = nullptr;
              NS_HTML5_BREAK(starttagloop);
            }
            default: {
              pop();
              mode = AFTER_HEAD;
              NS_HTML5_CONTINUE(starttagloop);
            }
          }
        }
      inheadloop_end:;
        [[fallthrough]];
      }
      case IN_HEAD_NOSCRIPT: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case LINK_OR_BASEFONT_OR_BGSOUND: {
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case META: {
            checkMetaCharset(attributes);
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case STYLE:
          case NOFRAMES: {
            appendToCurrentNodeAndPushElement(elementName, attributes);
            originalMode = mode;
            mode = TEXT;
            tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                                    elementName);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case HEAD: {
            errFooSeenWhenFooOpen(name);
            NS_HTML5_BREAK(starttagloop);
          }
          case NOSCRIPT: {
            errFooSeenWhenFooOpen(name);
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errBadStartTagInNoscriptInHead(name);
            pop();
            mode = IN_HEAD;
            continue;
          }
        }
      }
      case IN_COLUMN_GROUP: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case COL: {
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TEMPLATE: {
            startTagTemplateInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            if (!currentPtr || stack[currentPtr]->getGroup() == TEMPLATE) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errGarbageInColgroup();
              NS_HTML5_BREAK(starttagloop);
            }
            pop();
            mode = IN_TABLE;
            continue;
          }
        }
      }
      case IN_SELECT_IN_TABLE: {
        MOZ_ASSERT(!noInSelectMode);
        switch (group) {
          case CAPTION:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR:
          case TD_OR_TH:
          case TABLE: {
            errStartTagWithSelectOpen(name);
            eltPos = findLastInTableScope(nsGkAtoms::select);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment);
              NS_HTML5_BREAK(starttagloop);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            resetTheInsertionMode();
            continue;
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_SELECT: {
        MOZ_ASSERT(!noInSelectMode);
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case OPTION: {
            if (isCurrent(nsGkAtoms::option)) {
              pop();
            }
            appendToCurrentNodeAndPushElement(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case OPTGROUP: {
            if (isCurrent(nsGkAtoms::option)) {
              pop();
            }
            if (isCurrent(nsGkAtoms::optgroup)) {
              pop();
            }
            appendToCurrentNodeAndPushElement(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case SELECT: {
            errStartSelectWhereEndSelectExpected();
            eltPos = findLastInTableScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment);
              errNoSelectInTableScope();
              NS_HTML5_BREAK(starttagloop);
            } else {
              while (currentPtr >= eltPos) {
                pop();
              }
              resetTheInsertionMode();
              NS_HTML5_BREAK(starttagloop);
            }
          }
          case INPUT:
          case TEXTAREA: {
            errStartTagWithSelectOpen(name);
            eltPos = findLastInTableScope(nsGkAtoms::select);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment);
              NS_HTML5_BREAK(starttagloop);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            resetTheInsertionMode();
            continue;
          }
          case SCRIPT: {
            startTagScriptInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TEMPLATE: {
            startTagTemplateInHead(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case HR: {
            if (isCurrent(nsGkAtoms::option)) {
              pop();
            }
            if (isCurrent(nsGkAtoms::optgroup)) {
              pop();
            }
            appendVoidElementToCurrent(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errStrayStartTag(name);
            NS_HTML5_BREAK(starttagloop);
          }
        }
      }
      case AFTER_BODY: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errStrayStartTag(name);
            mode = framesetOk ? FRAMESET_OK : IN_BODY;
            continue;
          }
        }
      }
      case IN_FRAMESET: {
        switch (group) {
          case FRAMESET: {
            appendToCurrentNodeAndPushElement(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case FRAME: {
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case AFTER_FRAMESET: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case NOFRAMES: {
            appendToCurrentNodeAndPushElement(elementName, attributes);
            originalMode = mode;
            mode = TEXT;
            tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                                    elementName);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errStrayStartTag(name);
            NS_HTML5_BREAK(starttagloop);
          }
        }
      }
      case INITIAL: {
        errStartTagWithoutDoctype();
        documentModeInternal(QUIRKS_MODE, nullptr, nullptr);
        mode = BEFORE_HTML;
        continue;
      }
      case BEFORE_HTML: {
        switch (group) {
          case HTML: {
            if (attributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
              appendHtmlElementToDocumentAndPush();
            } else {
              appendHtmlElementToDocumentAndPush(attributes);
            }
            mode = BEFORE_HEAD;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            appendHtmlElementToDocumentAndPush();
            mode = BEFORE_HEAD;
            continue;
          }
        }
      }
      case BEFORE_HEAD: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case HEAD: {
            appendToCurrentNodeAndPushHeadElement(attributes);
            mode = IN_HEAD;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            appendToCurrentNodeAndPushHeadElement(
                nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
            mode = IN_HEAD;
            continue;
          }
        }
      }
      case AFTER_HEAD: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case BODY: {
            if (!attributes->getLength()) {
              appendToCurrentNodeAndPushBodyElement();
            } else {
              appendToCurrentNodeAndPushBodyElement(attributes);
            }
            framesetOk = false;
            mode = IN_BODY;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case FRAMESET: {
            appendToCurrentNodeAndPushElement(elementName, attributes);
            mode = IN_FRAMESET;
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TEMPLATE: {
            errFooBetweenHeadAndBody(name);
            pushHeadPointerOntoStack();
            nsHtml5StackNode* headOnStack = stack[currentPtr];
            startTagTemplateInHead(elementName, attributes);
            removeFromStack(headOnStack);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case BASE:
          case LINK_OR_BASEFONT_OR_BGSOUND: {
            errFooBetweenHeadAndBody(name);
            pushHeadPointerOntoStack();
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            pop();
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case META: {
            errFooBetweenHeadAndBody(name);
            checkMetaCharset(attributes);
            pushHeadPointerOntoStack();
            appendVoidElementToCurrentMayFoster(elementName, attributes);
            selfClosing = false;
            pop();
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case SCRIPT: {
            errFooBetweenHeadAndBody(name);
            pushHeadPointerOntoStack();
            appendToCurrentNodeAndPushElement(elementName, attributes);
            originalMode = mode;
            mode = TEXT;
            tokenizer->setStateAndEndTagExpectation(
                nsHtml5Tokenizer::SCRIPT_DATA, elementName);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case STYLE:
          case NOFRAMES: {
            errFooBetweenHeadAndBody(name);
            pushHeadPointerOntoStack();
            appendToCurrentNodeAndPushElement(elementName, attributes);
            originalMode = mode;
            mode = TEXT;
            tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                                    elementName);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case TITLE: {
            errFooBetweenHeadAndBody(name);
            pushHeadPointerOntoStack();
            appendToCurrentNodeAndPushElement(elementName, attributes);
            originalMode = mode;
            mode = TEXT;
            tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RCDATA,
                                                    elementName);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          case HEAD: {
            errStrayStartTag(name);
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            appendToCurrentNodeAndPushBodyElement();
            mode = FRAMESET_OK;
            continue;
          }
        }
      }
      case AFTER_AFTER_BODY: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errStrayStartTag(name);

            mode = framesetOk ? FRAMESET_OK : IN_BODY;
            continue;
          }
        }
      }
      case AFTER_AFTER_FRAMESET: {
        switch (group) {
          case HTML: {
            errStrayStartTag(name);
            if (!fragment && !isTemplateContents()) {
              addAttributesToHtml(attributes);
              attributes = nullptr;
            }
            NS_HTML5_BREAK(starttagloop);
          }
          case NOFRAMES: {
            startTagGenericRawText(elementName, attributes);
            attributes = nullptr;
            NS_HTML5_BREAK(starttagloop);
          }
          default: {
            errStrayStartTag(name);
            NS_HTML5_BREAK(starttagloop);
          }
        }
      }
      case TEXT: {
        MOZ_ASSERT(false);
        NS_HTML5_BREAK(starttagloop);
      }
    }
  }
starttagloop_end:;
  if (selfClosing) {
    errSelfClosing();
  }
  if (!mBuilder && attributes != nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
    delete attributes;
  }
}

void nsHtml5TreeBuilder::startTagTitleInHead(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
  originalMode = mode;
  mode = TEXT;
  tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RCDATA,
                                          elementName);
}

void nsHtml5TreeBuilder::startTagGenericRawText(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
  originalMode = mode;
  mode = TEXT;
  tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::RAWTEXT,
                                          elementName);
}

void nsHtml5TreeBuilder::startTagScriptInHead(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
  originalMode = mode;
  mode = TEXT;
  tokenizer->setStateAndEndTagExpectation(nsHtml5Tokenizer::SCRIPT_DATA,
                                          elementName);
}

void nsHtml5TreeBuilder::startTagTemplateInHead(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  appendToCurrentNodeAndPushElement(elementName, attributes);
  insertMarker();
  framesetOk = false;
  originalMode = mode;
  mode = IN_TEMPLATE;
  pushTemplateMode(IN_TEMPLATE);
}

bool nsHtml5TreeBuilder::isTemplateContents() {
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK !=
         findLast(nsGkAtoms::_template);
}

bool nsHtml5TreeBuilder::isTemplateModeStackEmpty() {
  return templateModePtr == -1;
}

bool nsHtml5TreeBuilder::isSpecialParentInForeign(nsHtml5StackNode* stackNode) {
  int32_t ns = stackNode->ns;
  return (kNameSpaceID_XHTML == ns) || (stackNode->isHtmlIntegrationPoint()) ||
         ((kNameSpaceID_MathML == ns) &&
          (stackNode->getGroup() == MI_MO_MN_MS_MTEXT));
}

nsIContentHandle* nsHtml5TreeBuilder::getDeclarativeShadowRoot(
    nsIContentHandle* currentNode, nsIContentHandle* templateNode,
    nsHtml5HtmlAttributes* attributes) {
  if (!isAllowDeclarativeShadowRoots()) {
    return nullptr;
  }
  nsHtml5String shadowRootMode =
      attributes->getValue(nsHtml5AttributeName::ATTR_SHADOWROOTMODE);
  if (!shadowRootMode) {
    return nullptr;
  }
  bool shadowRootIsClonable =
      attributes->contains(nsHtml5AttributeName::ATTR_SHADOWROOTCLONABLE);
  bool shadowRootIsSerializable =
      attributes->contains(nsHtml5AttributeName::ATTR_SHADOWROOTSERIALIZABLE);
  bool shadowRootDelegatesFocus =
      attributes->contains(nsHtml5AttributeName::ATTR_SHADOWROOTDELEGATESFOCUS);
  bool shadowRootCustomElementRegistry = attributes->contains(
      nsHtml5AttributeName::ATTR_SHADOWROOTCUSTOMELEMENTREGISTRY);
  nsHtml5String shadowRootReferenceTarget = attributes->getValue(
      nsHtml5AttributeName::ATTR_SHADOWROOTREFERENCETARGET);
  nsHtml5String shadowRootSlotAssignment =
      attributes->getValue(nsHtml5AttributeName::ATTR_SHADOWROOTSLOTASSIGNMENT);
  return getShadowRootFromHost(
      currentNode, templateNode, shadowRootMode, shadowRootIsClonable,
      shadowRootIsSerializable, shadowRootDelegatesFocus,
      shadowRootCustomElementRegistry, shadowRootSlotAssignment,
      shadowRootReferenceTarget);
}

nsHtml5String nsHtml5TreeBuilder::extractCharsetFromContent(
    nsHtml5String attributeValue, nsHtml5TreeBuilder* tb) {
  int32_t charsetState = CHARSET_INITIAL;
  int32_t start = -1;
  int32_t end = -1;
  autoJArray<char16_t, int32_t> buffer =
      nsHtml5Portability::newCharArrayFromString(attributeValue);
  for (int32_t i = 0; i < buffer.length; i++) {
    char16_t c = buffer[i];
    switch (charsetState) {
      case CHARSET_INITIAL: {
        switch (c) {
          case 'c':
          case 'C': {
            charsetState = CHARSET_C;
            continue;
          }
          default: {
            continue;
          }
        }
      }
      case CHARSET_C: {
        switch (c) {
          case 'h':
          case 'H': {
            charsetState = CHARSET_H;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_H: {
        switch (c) {
          case 'a':
          case 'A': {
            charsetState = CHARSET_A;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_A: {
        switch (c) {
          case 'r':
          case 'R': {
            charsetState = CHARSET_R;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_R: {
        switch (c) {
          case 's':
          case 'S': {
            charsetState = CHARSET_S;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_S: {
        switch (c) {
          case 'e':
          case 'E': {
            charsetState = CHARSET_E;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_E: {
        switch (c) {
          case 't':
          case 'T': {
            charsetState = CHARSET_T;
            continue;
          }
          default: {
            charsetState = CHARSET_INITIAL;
            continue;
          }
        }
      }
      case CHARSET_T: {
        switch (c) {
          case '\t':
          case '\n':
          case '\f':
          case '\r':
          case ' ': {
            continue;
          }
          case '=': {
            charsetState = CHARSET_EQUALS;
            continue;
          }
          default: {
            return nullptr;
          }
        }
      }
      case CHARSET_EQUALS: {
        switch (c) {
          case '\t':
          case '\n':
          case '\f':
          case '\r':
          case ' ': {
            continue;
          }
          case '\'': {
            start = i + 1;
            charsetState = CHARSET_SINGLE_QUOTED;
            continue;
          }
          case '\"': {
            start = i + 1;
            charsetState = CHARSET_DOUBLE_QUOTED;
            continue;
          }
          default: {
            start = i;
            charsetState = CHARSET_UNQUOTED;
            continue;
          }
        }
      }
      case CHARSET_SINGLE_QUOTED: {
        switch (c) {
          case '\'': {
            end = i;
            NS_HTML5_BREAK(charsetloop);
          }
          default: {
            continue;
          }
        }
      }
      case CHARSET_DOUBLE_QUOTED: {
        switch (c) {
          case '\"': {
            end = i;
            NS_HTML5_BREAK(charsetloop);
          }
          default: {
            continue;
          }
        }
      }
      case CHARSET_UNQUOTED: {
        switch (c) {
          case '\t':
          case '\n':
          case '\f':
          case '\r':
          case ' ':
          case ';': {
            end = i;
            NS_HTML5_BREAK(charsetloop);
          }
          default: {
            continue;
          }
        }
      }
    }
  }
charsetloop_end:;
  if (start != -1) {
    if (end == -1) {
      if (charsetState == CHARSET_UNQUOTED) {
        end = buffer.length;
      } else {
        return nullptr;
      }
    }
    return nsHtml5Portability::newStringFromBuffer(buffer, start, end - start,
                                                   tb, false);
  }
  return nullptr;
}

void nsHtml5TreeBuilder::checkMetaCharset(nsHtml5HtmlAttributes* attributes) {
  nsHtml5String charset =
      attributes->getValue(nsHtml5AttributeName::ATTR_CHARSET);
  if (charset) {
    if (tokenizer->internalEncodingDeclaration(charset)) {
      requestSuspension();
      return;
    }
    return;
  }
  if (!nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
          "content-type",
          attributes->getValue(nsHtml5AttributeName::ATTR_HTTP_EQUIV))) {
    return;
  }
  nsHtml5String content =
      attributes->getValue(nsHtml5AttributeName::ATTR_CONTENT);
  if (content) {
    nsHtml5String extract =
        nsHtml5TreeBuilder::extractCharsetFromContent(content, this);
    if (extract) {
      if (tokenizer->internalEncodingDeclaration(extract)) {
        requestSuspension();
      }
    }
    extract.Release();
  }
}

void nsHtml5TreeBuilder::endTag(nsHtml5ElementName* elementName) {
  flushCharacters();
  needToDropLF = false;
  int32_t eltPos;
  int32_t group = elementName->getGroup();
  nsAtom* name = elementName->getName();
  for (;;) {
    if (isInForeign()) {
      if (stack[currentPtr]->name != name) {
        if (!currentPtr) {
          errStrayEndTag(name);
        } else {
          errEndTagDidNotMatchCurrentOpenElement(name,
                                                 stack[currentPtr]->popName);
        }
      }
      eltPos = currentPtr;
      int32_t origPos = currentPtr;
      for (;;) {
        if (!eltPos) {
          MOZ_ASSERT(fragment,
                     "We can get this close to the root of the stack in "
                     "foreign content only in the fragment case.");
          NS_HTML5_BREAK(endtagloop);
        }
        if (stack[eltPos]->name == name) {
          while (currentPtr >= eltPos) {
            popForeign(origPos, eltPos);
          }
          NS_HTML5_BREAK(endtagloop);
        }
        if (stack[--eltPos]->ns == kNameSpaceID_XHTML) {
          break;
        }
      }
    }
    switch (mode) {
      case IN_TEMPLATE: {
        switch (group) {
          case TEMPLATE: {
            break;
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
        [[fallthrough]];
      }
      case IN_ROW: {
        switch (group) {
          case TR: {
            eltPos = findLastOrRoot(nsHtml5TreeBuilder::TR);
            if (!eltPos) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errNoTableRowToClose();
              NS_HTML5_BREAK(endtagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE_BODY;
            NS_HTML5_BREAK(endtagloop);
          }
          case TABLE: {
            eltPos = findLastOrRoot(nsHtml5TreeBuilder::TR);
            if (!eltPos) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errNoTableRowToClose();
              NS_HTML5_BREAK(endtagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE_BODY;
            continue;
          }
          case TBODY_OR_THEAD_OR_TFOOT: {
            if (findLastInTableScope(name) ==
                nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            eltPos = findLastOrRoot(nsHtml5TreeBuilder::TR);
            if (!eltPos) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errNoTableRowToClose();
              NS_HTML5_BREAK(endtagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE_BODY;
            continue;
          }
          case BODY:
          case CAPTION:
          case COL:
          case COLGROUP:
          case HTML:
          case TD_OR_TH: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_TABLE_BODY: {
        switch (group) {
          case TBODY_OR_THEAD_OR_TFOOT: {
            eltPos = findLastOrRoot(name);
            if (!eltPos) {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE;
            NS_HTML5_BREAK(endtagloop);
          }
          case TABLE: {
            eltPos = findLastInTableScopeOrRootTemplateTbodyTheadTfoot();
            if (!eltPos || stack[eltPos]->getGroup() == TEMPLATE) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            clearStackBackTo(eltPos);
            pop();
            mode = IN_TABLE;
            continue;
          }
          case BODY:
          case CAPTION:
          case COL:
          case COLGROUP:
          case HTML:
          case TD_OR_TH:
          case TR: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_TABLE: {
        switch (group) {
          case TABLE: {
            eltPos = findLast(nsGkAtoms::table);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            resetTheInsertionMode();
            NS_HTML5_BREAK(endtagloop);
          }
          case BODY:
          case CAPTION:
          case COL:
          case COLGROUP:
          case HTML:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TD_OR_TH:
          case TR: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          case TEMPLATE: {
            break;
          }
          default: {
            errStrayEndTag(name);
          }
        }
        [[fallthrough]];
      }
      case IN_CAPTION: {
        switch (group) {
          case CAPTION: {
            eltPos = findLastInTableScope(nsGkAtoms::caption);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              NS_HTML5_BREAK(endtagloop);
            }
            generateImpliedEndTags();
            if (!!MOZ_UNLIKELY(mViewSource) && currentPtr != eltPos) {
              errUnclosedElements(eltPos, name);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
            mode = IN_TABLE;
            NS_HTML5_BREAK(endtagloop);
          }
          case TABLE: {
            eltPos = findLastInTableScope(nsGkAtoms::caption);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            generateImpliedEndTags();
            if (!!MOZ_UNLIKELY(mViewSource) && currentPtr != eltPos) {
              errUnclosedElements(eltPos, name);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
            mode = IN_TABLE;
            continue;
          }
          case BODY:
          case COL:
          case COLGROUP:
          case HTML:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TD_OR_TH:
          case TR: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_CELL: {
        switch (group) {
          case TD_OR_TH: {
            eltPos = findLastInTableScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            generateImpliedEndTags();
            if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
              errUnclosedElements(eltPos, name);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
            mode = IN_ROW;
            NS_HTML5_BREAK(endtagloop);
          }
          case TABLE:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR: {
            if (findLastInTableScope(name) ==
                nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(name == nsGkAtoms::tbody || name == nsGkAtoms::tfoot ||
                         name == nsGkAtoms::thead || fragment ||
                         isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            closeTheCell(findLastInTableScopeTdTh());
            continue;
          }
          case BODY:
          case CAPTION:
          case COL:
          case COLGROUP:
          case HTML: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case FRAMESET_OK:
      case IN_BODY: {
        switch (group) {
          case BODY: {
            if (!isSecondOnStackBody()) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            MOZ_ASSERT(currentPtr >= 1);
            if (MOZ_UNLIKELY(mViewSource)) {
              for (int32_t i = 2; i <= currentPtr; i++) {
                switch (stack[i]->getGroup()) {
                  case DD_OR_DT:
                  case LI:
                  case OPTGROUP:
                  case OPTION:
                  case P:
                  case RB_OR_RTC:
                  case RT_OR_RP:
                  case TD_OR_TH:
                  case TBODY_OR_THEAD_OR_TFOOT: {
                    break;
                  }
                  default: {
                    errEndWithUnclosedElements(name);
                    NS_HTML5_BREAK(uncloseloop1);
                  }
                }
              }
            uncloseloop1_end:;
            }
            mode = AFTER_BODY;
            NS_HTML5_BREAK(endtagloop);
          }
          case HTML: {
            if (!isSecondOnStackBody()) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            if (MOZ_UNLIKELY(mViewSource)) {
              for (int32_t i = 0; i <= currentPtr; i++) {
                switch (stack[i]->getGroup()) {
                  case DD_OR_DT:
                  case LI:
                  case P:
                  case RB_OR_RTC:
                  case RT_OR_RP:
                  case TBODY_OR_THEAD_OR_TFOOT:
                  case TD_OR_TH:
                  case BODY:
                  case HTML: {
                    break;
                  }
                  default: {
                    errEndWithUnclosedElements(name);
                    NS_HTML5_BREAK(uncloseloop2);
                  }
                }
              }
            uncloseloop2_end:;
            }
            mode = AFTER_BODY;
            continue;
          }
          case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
          case UL_OR_OL_OR_DL:
          case PRE_OR_LISTING:
          case FIELDSET:
          case BUTTON:
          case ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY: {
            eltPos = findLastInScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errStrayEndTag(name);
            } else {
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case FORM: {
            if (!isTemplateContents()) {
              if (!formPointer) {
                errStrayEndTag(name);
                NS_HTML5_BREAK(endtagloop);
              }
              formPointer = nullptr;
              eltPos = findLastInScope(name);
              if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                errStrayEndTag(name);
                NS_HTML5_BREAK(endtagloop);
              }
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                errUnclosedElements(eltPos, name);
              }
              removeFromStack(eltPos);
              NS_HTML5_BREAK(endtagloop);
            } else {
              eltPos = findLastInScope(name);
              if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                errStrayEndTag(name);
                NS_HTML5_BREAK(endtagloop);
              }
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
              NS_HTML5_BREAK(endtagloop);
            }
          }
          case P: {
            eltPos = findLastInButtonScope(nsGkAtoms::p);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errNoElementToCloseButEndTagSeen(nsGkAtoms::p);
              if (isInForeign()) {
                errHtmlStartTagInForeignContext(name);
                while (currentPtr >= 0 &&
                       stack[currentPtr]->ns != kNameSpaceID_XHTML) {
                  pop();
                }
              }
              appendVoidElementToCurrentMayFoster(
                  elementName, nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
              NS_HTML5_BREAK(endtagloop);
            }
            generateImpliedEndTagsExceptFor(nsGkAtoms::p);
            MOZ_ASSERT(eltPos != nsHtml5TreeBuilder::NOT_FOUND_ON_STACK);
            if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
              errUnclosedElements(eltPos, name);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case LI: {
            eltPos = findLastInListScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errNoElementToCloseButEndTagSeen(name);
            } else {
              generateImpliedEndTagsExceptFor(name);
              if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case DD_OR_DT: {
            eltPos = findLastInScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errNoElementToCloseButEndTagSeen(name);
            } else {
              generateImpliedEndTagsExceptFor(name);
              if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6: {
            eltPos = findLastInScopeHn();
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errStrayEndTag(name);
            } else {
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case OBJECT:
          case MARQUEE_OR_APPLET: {
            eltPos = findLastInScope(name);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              errStrayEndTag(name);
            } else {
              generateImpliedEndTags();
              if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                errUnclosedElements(eltPos, name);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
              clearTheListOfActiveFormattingElementsUpToTheLastMarker();
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case BR: {
            errEndTagBr();
            if (isInForeign()) {
              errHtmlStartTagInForeignContext(name);
              while (currentPtr >= 0 &&
                     stack[currentPtr]->ns != kNameSpaceID_XHTML) {
                pop();
              }
            }
            reconstructTheActiveFormattingElements();
            appendVoidElementToCurrentMayFoster(
                elementName, nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
            NS_HTML5_BREAK(endtagloop);
          }
          case TEMPLATE: {
            break;
          }
          case SELECT: {
            if (noInSelectMode) {
              eltPos = findLastInScope(name);
              if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                errStrayEndTag(name);
              } else {
                generateImpliedEndTags();
                if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                  errUnclosedElements(eltPos, name);
                }
                while (currentPtr >= eltPos) {
                  pop();
                }
              }
              NS_HTML5_BREAK(endtagloop);
            }
            [[fallthrough]];
          }
          case AREA_OR_WBR:
          case KEYGEN:
          case PARAM_OR_SOURCE_OR_TRACK:
          case EMBED:
          case IMG:
          case IMAGE:
          case INPUT:
          case HR:
          case IFRAME:
          case NOEMBED:
          case NOFRAMES:
          case TABLE:
          case TEXTAREA: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          case NOSCRIPT: {
            if (scriptingEnabled) {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            [[fallthrough]];
          }
          case A:
          case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
          case FONT:
          case NOBR: {
            if (adoptionAgencyEndTag(name)) {
              NS_HTML5_BREAK(endtagloop);
            }
            [[fallthrough]];
          }
          default: {
            if (isCurrent(name)) {
              pop();
              NS_HTML5_BREAK(endtagloop);
            }
            eltPos = currentPtr;
            for (;;) {
              nsHtml5StackNode* node = stack[eltPos];
              if (node->ns == kNameSpaceID_XHTML && node->name == name) {
                generateImpliedEndTags();
                if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(name)) {
                  errUnclosedElements(eltPos, name);
                }
                while (currentPtr >= eltPos) {
                  pop();
                }
                NS_HTML5_BREAK(endtagloop);
              } else if (!eltPos || node->isSpecial()) {
                errStrayEndTag(name);
                NS_HTML5_BREAK(endtagloop);
              }
              eltPos--;
            }
          }
        }
        [[fallthrough]];
      }
      case IN_HEAD: {
        switch (group) {
          case HEAD: {
            pop();
            mode = AFTER_HEAD;
            NS_HTML5_BREAK(endtagloop);
          }
          case BR:
          case HTML:
          case BODY: {
            pop();
            mode = AFTER_HEAD;
            continue;
          }
          case TEMPLATE: {
            endTagTemplateInHead();
            NS_HTML5_BREAK(endtagloop);
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case IN_HEAD_NOSCRIPT: {
        switch (group) {
          case NOSCRIPT: {
            pop();
            mode = IN_HEAD;
            NS_HTML5_BREAK(endtagloop);
          }
          case BR: {
            errStrayEndTag(name);
            pop();
            mode = IN_HEAD;
            continue;
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case IN_COLUMN_GROUP: {
        switch (group) {
          case COLGROUP: {
            if (!currentPtr ||
                stack[currentPtr]->getGroup() == nsHtml5TreeBuilder::TEMPLATE) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errGarbageInColgroup();
              NS_HTML5_BREAK(endtagloop);
            }
            pop();
            mode = IN_TABLE;
            NS_HTML5_BREAK(endtagloop);
          }
          case COL: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
          case TEMPLATE: {
            endTagTemplateInHead();
            NS_HTML5_BREAK(endtagloop);
          }
          default: {
            if (!currentPtr ||
                stack[currentPtr]->getGroup() == nsHtml5TreeBuilder::TEMPLATE) {
              MOZ_ASSERT(fragment || isTemplateContents());
              errGarbageInColgroup();
              NS_HTML5_BREAK(endtagloop);
            }
            pop();
            mode = IN_TABLE;
            continue;
          }
        }
      }
      case IN_SELECT_IN_TABLE: {
        MOZ_ASSERT(!noInSelectMode);
        switch (group) {
          case CAPTION:
          case TABLE:
          case TBODY_OR_THEAD_OR_TFOOT:
          case TR:
          case TD_OR_TH: {
            errEndTagSeenWithSelectOpen(name);
            if (findLastInTableScope(name) !=
                nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              eltPos = findLastInTableScope(nsGkAtoms::select);
              if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
                MOZ_ASSERT(fragment);
                NS_HTML5_BREAK(endtagloop);
              }
              while (currentPtr >= eltPos) {
                pop();
              }
              resetTheInsertionMode();
              continue;
            } else {
              NS_HTML5_BREAK(endtagloop);
            }
          }
          default:;  // fall through
        }
        [[fallthrough]];
      }
      case IN_SELECT: {
        MOZ_ASSERT(!noInSelectMode);
        switch (group) {
          case OPTION: {
            if (isCurrent(nsGkAtoms::option)) {
              pop();
              NS_HTML5_BREAK(endtagloop);
            } else {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
          }
          case OPTGROUP: {
            if (isCurrent(nsGkAtoms::option) &&
                nsGkAtoms::optgroup == stack[currentPtr - 1]->name) {
              pop();
            }
            if (isCurrent(nsGkAtoms::optgroup)) {
              pop();
            } else {
              errStrayEndTag(name);
            }
            NS_HTML5_BREAK(endtagloop);
          }
          case SELECT: {
            eltPos = findLastInTableScope(nsGkAtoms::select);
            if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
              MOZ_ASSERT(fragment);
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            while (currentPtr >= eltPos) {
              pop();
            }
            resetTheInsertionMode();
            NS_HTML5_BREAK(endtagloop);
          }
          case TEMPLATE: {
            endTagTemplateInHead();
            NS_HTML5_BREAK(endtagloop);
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case AFTER_BODY: {
        switch (group) {
          case HTML: {
            if (fragment) {
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            } else {
              mode = AFTER_AFTER_BODY;
              NS_HTML5_BREAK(endtagloop);
            }
          }
          default: {
            errEndTagAfterBody();
            mode = framesetOk ? FRAMESET_OK : IN_BODY;
            continue;
          }
        }
      }
      case IN_FRAMESET: {
        switch (group) {
          case FRAMESET: {
            if (!currentPtr) {
              MOZ_ASSERT(fragment);
              errStrayEndTag(name);
              NS_HTML5_BREAK(endtagloop);
            }
            pop();
            if ((!fragment) && !isCurrent(nsGkAtoms::frameset)) {
              mode = AFTER_FRAMESET;
            }
            NS_HTML5_BREAK(endtagloop);
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case AFTER_FRAMESET: {
        switch (group) {
          case HTML: {
            mode = AFTER_AFTER_FRAMESET;
            NS_HTML5_BREAK(endtagloop);
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case INITIAL: {
        errEndTagSeenWithoutDoctype();
        documentModeInternal(QUIRKS_MODE, nullptr, nullptr);
        mode = BEFORE_HTML;
        continue;
      }
      case BEFORE_HTML: {
        switch (group) {
          case HEAD:
          case BR:
          case HTML:
          case BODY: {
            appendHtmlElementToDocumentAndPush();
            mode = BEFORE_HEAD;
            continue;
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case BEFORE_HEAD: {
        switch (group) {
          case HEAD:
          case BR:
          case HTML:
          case BODY: {
            appendToCurrentNodeAndPushHeadElement(
                nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES);
            mode = IN_HEAD;
            continue;
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case AFTER_HEAD: {
        switch (group) {
          case TEMPLATE: {
            endTagTemplateInHead();
            NS_HTML5_BREAK(endtagloop);
          }
          case HTML:
          case BODY:
          case BR: {
            appendToCurrentNodeAndPushBodyElement();
            mode = FRAMESET_OK;
            continue;
          }
          default: {
            errStrayEndTag(name);
            NS_HTML5_BREAK(endtagloop);
          }
        }
      }
      case AFTER_AFTER_BODY: {
        errStrayEndTag(name);
        mode = framesetOk ? FRAMESET_OK : IN_BODY;
        continue;
      }
      case AFTER_AFTER_FRAMESET: {
        errStrayEndTag(name);
        NS_HTML5_BREAK(endtagloop);
      }
      case TEXT: {
        pop();
        if (originalMode == AFTER_HEAD) {
          silentPop();
        }
        mode = originalMode;
        NS_HTML5_BREAK(endtagloop);
      }
    }
  }
endtagloop_end:;
}

void nsHtml5TreeBuilder::endTagTemplateInHead() {
  int32_t eltPos = findLast(nsGkAtoms::_template);
  if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
    errStrayEndTag(nsGkAtoms::_template);
    return;
  }
  generateImpliedEndTagsThoroughly();
  if (!!MOZ_UNLIKELY(mViewSource) && !isCurrent(nsGkAtoms::_template)) {
    errUnclosedElements(eltPos, nsGkAtoms::_template);
  }
  while (currentPtr >= eltPos) {
    pop();
  }
  clearTheListOfActiveFormattingElementsUpToTheLastMarker();
  popTemplateMode();
  resetTheInsertionMode();
}

int32_t
nsHtml5TreeBuilder::findLastInTableScopeOrRootTemplateTbodyTheadTfoot() {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML &&
        (stack[i]->getGroup() == nsHtml5TreeBuilder::TBODY_OR_THEAD_OR_TFOOT ||
         stack[i]->getGroup() == nsHtml5TreeBuilder::TEMPLATE)) {
      return i;
    }
  }
  return 0;
}

int32_t nsHtml5TreeBuilder::findLast(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML && stack[i]->name == name) {
      return i;
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

int32_t nsHtml5TreeBuilder::findLastInTableScope(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML) {
      if (stack[i]->name == name) {
        return i;
      } else if (stack[i]->name == nsGkAtoms::table ||
                 stack[i]->name == nsGkAtoms::_template) {
        return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
      }
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

int32_t nsHtml5TreeBuilder::findLastInButtonScope(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML) {
      if (stack[i]->name == name) {
        return i;
      } else if (stack[i]->name == nsGkAtoms::button) {
        return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
      }
    }
    if (stack[i]->isScoping()) {
      return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

int32_t nsHtml5TreeBuilder::findLastInScope(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML && stack[i]->name == name) {
      return i;
    } else if (stack[i]->isScoping()) {
      return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

int32_t nsHtml5TreeBuilder::findLastInListScope(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML) {
      if (stack[i]->name == name) {
        return i;
      } else if (stack[i]->name == nsGkAtoms::ul ||
                 stack[i]->name == nsGkAtoms::ol) {
        return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
      }
    }
    if (stack[i]->isScoping()) {
      return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

int32_t nsHtml5TreeBuilder::findLastInScopeHn() {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->getGroup() ==
        nsHtml5TreeBuilder::H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6) {
      return i;
    } else if (stack[i]->isScoping()) {
      return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

void nsHtml5TreeBuilder::generateImpliedEndTagsExceptFor(nsAtom* name) {
  for (;;) {
    nsHtml5StackNode* node = stack[currentPtr];
    switch (node->getGroup()) {
      case P:
      case LI:
      case DD_OR_DT:
      case OPTION:
      case OPTGROUP:
      case RB_OR_RTC:
      case RT_OR_RP: {
        if (node->ns == kNameSpaceID_XHTML && node->name == name) {
          return;
        }
        pop();
        continue;
      }
      default: {
        return;
      }
    }
  }
}

void nsHtml5TreeBuilder::generateImpliedEndTags() {
  for (;;) {
    switch (stack[currentPtr]->getGroup()) {
      case P:
      case LI:
      case DD_OR_DT:
      case OPTION:
      case OPTGROUP:
      case RB_OR_RTC:
      case RT_OR_RP: {
        pop();
        continue;
      }
      default: {
        return;
      }
    }
  }
}

void nsHtml5TreeBuilder::generateImpliedEndTagsThoroughly() {
  for (;;) {
    switch (stack[currentPtr]->getGroup()) {
      case CAPTION:
      case COLGROUP:
      case DD_OR_DT:
      case LI:
      case OPTGROUP:
      case OPTION:
      case P:
      case RB_OR_RTC:
      case RT_OR_RP:
      case TBODY_OR_THEAD_OR_TFOOT:
      case TD_OR_TH:
      case TR: {
        pop();
        continue;
      }
      default: {
        return;
      }
    }
  }
}

bool nsHtml5TreeBuilder::isSecondOnStackBody() {
  return currentPtr >= 1 && stack[1]->getGroup() == nsHtml5TreeBuilder::BODY;
}

void nsHtml5TreeBuilder::documentModeInternal(nsHtml5DocumentMode m,
                                              nsHtml5String publicIdentifier,
                                              nsHtml5String systemIdentifier) {
  if (forceNoQuirks) {
    quirks = false;
    this->documentMode(STANDARDS_MODE);
    return;
  }
  quirks = (m == QUIRKS_MODE);
  this->documentMode(m);
}

bool nsHtml5TreeBuilder::isAlmostStandards(nsHtml5String publicIdentifier,
                                           nsHtml5String systemIdentifier) {
  if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
          "-//w3c//dtd xhtml 1.0 transitional//", publicIdentifier)) {
    return true;
  }
  if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
          "-//w3c//dtd xhtml 1.0 frameset//", publicIdentifier)) {
    return true;
  }
  if (systemIdentifier) {
    if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
            "-//w3c//dtd html 4.01 transitional//", publicIdentifier)) {
      return true;
    }
    if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
            "-//w3c//dtd html 4.01 frameset//", publicIdentifier)) {
      return true;
    }
  }
  return false;
}

bool nsHtml5TreeBuilder::isQuirky(nsAtom* name, nsHtml5String publicIdentifier,
                                  nsHtml5String systemIdentifier,
                                  bool forceQuirks) {
  if (forceQuirks) {
    return true;
  }
  if (name != nsGkAtoms::html) {
    return true;
  }
  if (publicIdentifier) {
    for (int32_t i = 0; i < nsHtml5TreeBuilder::QUIRKY_PUBLIC_IDS.length; i++) {
      if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
              nsHtml5TreeBuilder::QUIRKY_PUBLIC_IDS[i], publicIdentifier)) {
        return true;
      }
    }
    if (nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
            "-//w3o//dtd w3 html strict 3.0//en//", publicIdentifier) ||
        nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
            "-/w3c/dtd html 4.0 transitional/en", publicIdentifier) ||
        nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
            "html", publicIdentifier)) {
      return true;
    }
  }
  if (!systemIdentifier) {
    if (nsHtml5Portability::lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
            "-//w3c//dtd html 4.01 transitional//", publicIdentifier)) {
      return true;
    } else if (nsHtml5Portability::
                   lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                       "-//w3c//dtd html 4.01 frameset//", publicIdentifier)) {
      return true;
    }
  } else if (nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                 "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd",
                 systemIdentifier)) {
    return true;
  }
  return false;
}

void nsHtml5TreeBuilder::closeTheCell(int32_t eltPos) {
  generateImpliedEndTags();
  if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
    errUnclosedElementsCell(eltPos);
  }
  while (currentPtr >= eltPos) {
    pop();
  }
  clearTheListOfActiveFormattingElementsUpToTheLastMarker();
  mode = IN_ROW;
  return;
}

int32_t nsHtml5TreeBuilder::findLastInTableScopeTdTh() {
  for (int32_t i = currentPtr; i > 0; i--) {
    nsAtom* name = stack[i]->name;
    if (stack[i]->ns == kNameSpaceID_XHTML) {
      if (nsGkAtoms::td == name || nsGkAtoms::th == name) {
        return i;
      } else if (name == nsGkAtoms::table || name == nsGkAtoms::_template) {
        return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
      }
    }
  }
  return nsHtml5TreeBuilder::NOT_FOUND_ON_STACK;
}

void nsHtml5TreeBuilder::clearStackBackTo(int32_t eltPos) {
  int32_t eltGroup = stack[eltPos]->getGroup();
  while (currentPtr > eltPos) {
    if (stack[currentPtr]->ns == kNameSpaceID_XHTML &&
        stack[currentPtr]->getGroup() == TEMPLATE &&
        (eltGroup == TABLE || eltGroup == TBODY_OR_THEAD_OR_TFOOT ||
         eltGroup == TR || !eltPos)) {
      return;
    }
    pop();
  }
}

void nsHtml5TreeBuilder::resetTheInsertionMode() {
  nsHtml5StackNode* node;
  nsAtom* name;
  int32_t ns;
  for (int32_t i = currentPtr; i >= 0; i--) {
    node = stack[i];
    name = node->name;
    ns = node->ns;
    if (!i) {
      if (!(contextNamespace == kNameSpaceID_XHTML &&
            (contextName == nsGkAtoms::td || contextName == nsGkAtoms::th))) {
        if (fragment) {
          name = contextName;
          ns = contextNamespace;
        }
      } else {
        mode = framesetOk ? FRAMESET_OK : IN_BODY;
        return;
      }
    }
    if (nsGkAtoms::select == name) {
      if (!noInSelectMode) {
        int32_t ancestorIndex = i;
        while (ancestorIndex > 0) {
          nsHtml5StackNode* ancestor = stack[ancestorIndex--];
          if (kNameSpaceID_XHTML == ancestor->ns) {
            if (nsGkAtoms::_template == ancestor->name) {
              break;
            }
            if (nsGkAtoms::table == ancestor->name) {
              mode = IN_SELECT_IN_TABLE;
              return;
            }
          }
        }
        mode = IN_SELECT;
        return;
      }
      if (!i) {
        mode = framesetOk ? FRAMESET_OK : IN_BODY;
        return;
      }
      continue;
    } else if (nsGkAtoms::td == name || nsGkAtoms::th == name) {
      mode = IN_CELL;
      return;
    } else if (nsGkAtoms::tr == name) {
      mode = IN_ROW;
      return;
    } else if (nsGkAtoms::tbody == name || nsGkAtoms::thead == name ||
               nsGkAtoms::tfoot == name) {
      mode = IN_TABLE_BODY;
      return;
    } else if (nsGkAtoms::caption == name) {
      mode = IN_CAPTION;
      return;
    } else if (nsGkAtoms::colgroup == name) {
      mode = IN_COLUMN_GROUP;
      return;
    } else if (nsGkAtoms::table == name) {
      mode = IN_TABLE;
      return;
    } else if (kNameSpaceID_XHTML != ns) {
      mode = framesetOk ? FRAMESET_OK : IN_BODY;
      return;
    } else if (nsGkAtoms::_template == name) {
      MOZ_ASSERT(templateModePtr >= 0);
      mode = templateModeStack[templateModePtr];
      return;
    } else if (nsGkAtoms::head == name) {
      if (name == contextName) {
        mode = framesetOk ? FRAMESET_OK : IN_BODY;
      } else {
        mode = IN_HEAD;
      }
      return;
    } else if (nsGkAtoms::body == name) {
      mode = framesetOk ? FRAMESET_OK : IN_BODY;
      return;
    } else if (nsGkAtoms::frameset == name) {
      mode = IN_FRAMESET;
      return;
    } else if (nsGkAtoms::html == name) {
      if (!headPointer) {
        mode = BEFORE_HEAD;
      } else {
        mode = AFTER_HEAD;
      }
      return;
    } else if (!i) {
      mode = framesetOk ? FRAMESET_OK : IN_BODY;
      return;
    }
  }
}

void nsHtml5TreeBuilder::implicitlyCloseP() {
  int32_t eltPos = findLastInButtonScope(nsGkAtoms::p);
  if (eltPos == nsHtml5TreeBuilder::NOT_FOUND_ON_STACK) {
    return;
  }
  generateImpliedEndTagsExceptFor(nsGkAtoms::p);
  if (!!MOZ_UNLIKELY(mViewSource) && eltPos != currentPtr) {
    errUnclosedElementsImplied(eltPos, nsGkAtoms::p);
  }
  while (currentPtr >= eltPos) {
    pop();
  }
}

bool nsHtml5TreeBuilder::debugOnlyClearLastStackSlot() {
  stack[currentPtr] = nullptr;
  return true;
}

bool nsHtml5TreeBuilder::debugOnlyClearLastListSlot() {
  listOfActiveFormattingElements[listPtr] = nullptr;
  return true;
}

void nsHtml5TreeBuilder::pushTemplateMode(int32_t mode) {
  templateModePtr++;
  if (templateModePtr == templateModeStack.length) {
    jArray<int32_t, int32_t> newStack =
        jArray<int32_t, int32_t>::newJArray(templateModeStack.length + 64);
    nsHtml5ArrayCopy::arraycopy(templateModeStack, newStack,
                                templateModeStack.length);
    templateModeStack = newStack;
  }
  templateModeStack[templateModePtr] = mode;
}

void nsHtml5TreeBuilder::push(nsHtml5StackNode* node) {
  currentPtr++;
  if (currentPtr == stack.length) {
    jArray<nsHtml5StackNode*, int32_t> newStack =
        jArray<nsHtml5StackNode*, int32_t>::newJArray(stack.length + 64);
    nsHtml5ArrayCopy::arraycopy(stack, newStack, stack.length);
    stack = newStack;
  }
  stack[currentPtr] = node;
  elementPushed(node->ns, node->popName, node->node);
}

void nsHtml5TreeBuilder::silentPush(nsHtml5StackNode* node) {
  currentPtr++;
  if (currentPtr == stack.length) {
    jArray<nsHtml5StackNode*, int32_t> newStack =
        jArray<nsHtml5StackNode*, int32_t>::newJArray(stack.length + 64);
    nsHtml5ArrayCopy::arraycopy(stack, newStack, stack.length);
    stack = newStack;
  }
  stack[currentPtr] = node;
}

void nsHtml5TreeBuilder::append(nsHtml5StackNode* node) {
  listPtr++;
  if (listPtr == listOfActiveFormattingElements.length) {
    jArray<nsHtml5StackNode*, int32_t> newList =
        jArray<nsHtml5StackNode*, int32_t>::newJArray(
            listOfActiveFormattingElements.length + 64);
    nsHtml5ArrayCopy::arraycopy(listOfActiveFormattingElements, newList,
                                listOfActiveFormattingElements.length);
    listOfActiveFormattingElements = newList;
  }
  listOfActiveFormattingElements[listPtr] = node;
}

void nsHtml5TreeBuilder::
    clearTheListOfActiveFormattingElementsUpToTheLastMarker() {
  while (listPtr > -1) {
    if (!listOfActiveFormattingElements[listPtr]) {
      --listPtr;
      return;
    }
    listOfActiveFormattingElements[listPtr]->release(this);
    --listPtr;
  }
}

void nsHtml5TreeBuilder::removeFromStack(int32_t pos) {
  if (currentPtr == pos) {
    pop();
  } else {
    stack[pos]->release(this);
    nsHtml5ArrayCopy::arraycopy(stack, pos + 1, pos, currentPtr - pos);
    MOZ_ASSERT(debugOnlyClearLastStackSlot());
    currentPtr--;
  }
}

void nsHtml5TreeBuilder::removeFromStack(nsHtml5StackNode* node) {
  if (stack[currentPtr] == node) {
    pop();
  } else {
    int32_t pos = currentPtr - 1;
    while (pos >= 0 && stack[pos] != node) {
      pos--;
    }
    if (pos == -1) {
      return;
    }

    node->release(this);
    nsHtml5ArrayCopy::arraycopy(stack, pos + 1, pos, currentPtr - pos);
    currentPtr--;
  }
}

void nsHtml5TreeBuilder::removeFromListOfActiveFormattingElements(int32_t pos) {
  MOZ_ASSERT(!!listOfActiveFormattingElements[pos]);
  listOfActiveFormattingElements[pos]->release(this);
  if (pos == listPtr) {
    MOZ_ASSERT(debugOnlyClearLastListSlot());
    listPtr--;
    return;
  }
  MOZ_ASSERT(pos < listPtr);
  nsHtml5ArrayCopy::arraycopy(listOfActiveFormattingElements, pos + 1, pos,
                              listPtr - pos);
  MOZ_ASSERT(debugOnlyClearLastListSlot());
  listPtr--;
}

bool nsHtml5TreeBuilder::adoptionAgencyEndTag(nsAtom* name) {
  if (stack[currentPtr]->ns == kNameSpaceID_XHTML &&
      stack[currentPtr]->name == name &&
      findInListOfActiveFormattingElements(stack[currentPtr]) == -1) {
    pop();
    return true;
  }
  for (int32_t i = 0; i < 8; ++i) {
    int32_t formattingEltListPos = listPtr;
    while (formattingEltListPos > -1) {
      nsHtml5StackNode* listNode =
          listOfActiveFormattingElements[formattingEltListPos];
      if (!listNode) {
        formattingEltListPos = -1;
        break;
      } else if (listNode->name == name) {
        break;
      }
      formattingEltListPos--;
    }
    if (formattingEltListPos == -1) {
      return false;
    }
    nsHtml5StackNode* formattingElt =
        listOfActiveFormattingElements[formattingEltListPos];
    int32_t formattingEltStackPos = currentPtr;
    bool inScope = true;
    while (formattingEltStackPos > -1) {
      nsHtml5StackNode* node = stack[formattingEltStackPos];
      if (node == formattingElt) {
        break;
      } else if (node->isScoping()) {
        inScope = false;
      }
      formattingEltStackPos--;
    }
    if (formattingEltStackPos == -1) {
      errNoElementToCloseButEndTagSeen(name);
      removeFromListOfActiveFormattingElements(formattingEltListPos);
      return true;
    }
    if (!inScope) {
      errNoElementToCloseButEndTagSeen(name);
      return true;
    }
    if (formattingEltStackPos != currentPtr) {
      errEndTagViolatesNestingRules(name);
    }
    int32_t furthestBlockPos = formattingEltStackPos + 1;
    while (furthestBlockPos <= currentPtr) {
      nsHtml5StackNode* node = stack[furthestBlockPos];
      MOZ_ASSERT(furthestBlockPos > 0,
                 "How is formattingEltStackPos + 1 not > 0?");
      if (node->isSpecial()) {
        break;
      }
      furthestBlockPos++;
    }
    if (furthestBlockPos > currentPtr) {
      while (currentPtr >= formattingEltStackPos) {
        pop();
      }
      removeFromListOfActiveFormattingElements(formattingEltListPos);
      return true;
    }
    nsHtml5StackNode* commonAncestor = stack[formattingEltStackPos - 1];
    nsIContentHandle* insertionCommonAncestor =
        nodeFromStackWithBlinkCompat(formattingEltStackPos - 1);
    nsHtml5StackNode* furthestBlock = stack[furthestBlockPos];
    int32_t bookmark = formattingEltListPos;
    int32_t nodePos = furthestBlockPos;
    nsHtml5StackNode* lastNode = furthestBlock;
    int32_t j = 0;
    for (;;) {
      ++j;
      nodePos--;
      if (nodePos == formattingEltStackPos) {
        break;
      }
      nsHtml5StackNode* node = stack[nodePos];
      int32_t nodeListPos = findInListOfActiveFormattingElements(node);
      if (j > 3 && nodeListPos != -1) {
        removeFromListOfActiveFormattingElements(nodeListPos);
        if (nodeListPos <= formattingEltListPos) {
          formattingEltListPos--;
        }
        if (nodeListPos <= bookmark) {
          bookmark--;
        }
        nodeListPos = -1;
      }
      if (nodeListPos == -1) {
        MOZ_ASSERT(formattingEltStackPos < nodePos);
        MOZ_ASSERT(bookmark < nodePos);
        MOZ_ASSERT(furthestBlockPos > nodePos);
        removeFromStack(nodePos);
        furthestBlockPos--;
        continue;
      }
      if (nodePos == furthestBlockPos) {
        bookmark = nodeListPos + 1;
      }
      MOZ_ASSERT(node == listOfActiveFormattingElements[nodeListPos]);
      MOZ_ASSERT(node == stack[nodePos]);
      nsIContentHandle* clone = createElement(
          kNameSpaceID_XHTML, node->name, node->attributes->cloneAttributes(),
          insertionCommonAncestor, htmlCreator(node->getHtmlCreator()));
      nsHtml5StackNode* newNode = createStackNode(
          node->getFlags(), node->ns, node->name, clone, node->popName,
          node->attributes, node->getHtmlCreator());
      node->dropAttributes();
      stack[nodePos] = newNode;
      newNode->retain();
      listOfActiveFormattingElements[nodeListPos] = newNode;
      node->release(this);
      node->release(this);
      node = newNode;
      detachFromParent(lastNode->node);
      appendElement(lastNode->node, nodeFromStackWithBlinkCompat(nodePos));
      lastNode = node;
    }
    if (commonAncestor->isFosterParenting()) {
      detachFromParent(lastNode->node);
      insertIntoFosterParent(lastNode->node);
    } else {
      detachFromParent(lastNode->node);
      appendElement(lastNode->node, insertionCommonAncestor);
    }
    nsIContentHandle* clone = createElement(
        kNameSpaceID_XHTML, formattingElt->name,
        formattingElt->attributes->cloneAttributes(), furthestBlock->node,
        htmlCreator(formattingElt->getHtmlCreator()));
    nsHtml5StackNode* formattingClone = createStackNode(
        formattingElt->getFlags(), formattingElt->ns, formattingElt->name,
        clone, formattingElt->popName, formattingElt->attributes,
        formattingElt->getHtmlCreator());
    formattingElt->dropAttributes();
    appendChildrenToNewParent(furthestBlock->node, clone);
    appendElement(clone, furthestBlock->node);
    removeFromListOfActiveFormattingElements(formattingEltListPos);
    insertIntoListOfActiveFormattingElements(formattingClone, bookmark);
    MOZ_ASSERT(formattingEltStackPos < furthestBlockPos);
    removeFromStack(formattingEltStackPos);
    insertIntoStack(formattingClone, furthestBlockPos);
  }
  return true;
}

void nsHtml5TreeBuilder::insertIntoStack(nsHtml5StackNode* node,
                                         int32_t position) {
  MOZ_ASSERT(currentPtr + 1 < stack.length);
  MOZ_ASSERT(position <= currentPtr + 1);
  if (position == currentPtr + 1) {
    push(node);
  } else {
    nsHtml5ArrayCopy::arraycopy(stack, position, position + 1,
                                (currentPtr - position) + 1);
    currentPtr++;
    stack[position] = node;
  }
}

void nsHtml5TreeBuilder::insertIntoListOfActiveFormattingElements(
    nsHtml5StackNode* formattingClone, int32_t bookmark) {
  formattingClone->retain();
  MOZ_ASSERT(listPtr + 1 < listOfActiveFormattingElements.length);
  if (bookmark <= listPtr) {
    nsHtml5ArrayCopy::arraycopy(listOfActiveFormattingElements, bookmark,
                                bookmark + 1, (listPtr - bookmark) + 1);
  }
  listPtr++;
  listOfActiveFormattingElements[bookmark] = formattingClone;
}

int32_t nsHtml5TreeBuilder::findInListOfActiveFormattingElements(
    nsHtml5StackNode* node) {
  for (int32_t i = listPtr; i >= 0; i--) {
    if (node == listOfActiveFormattingElements[i]) {
      return i;
    }
  }
  return -1;
}

int32_t nsHtml5TreeBuilder::
    findInListOfActiveFormattingElementsContainsBetweenEndAndLastMarker(
        nsAtom* name) {
  for (int32_t i = listPtr; i >= 0; i--) {
    nsHtml5StackNode* node = listOfActiveFormattingElements[i];
    if (!node) {
      return -1;
    } else if (node->name == name) {
      return i;
    }
  }
  return -1;
}

void nsHtml5TreeBuilder::maybeForgetEarlierDuplicateFormattingElement(
    nsAtom* name, nsHtml5HtmlAttributes* attributes) {
  int32_t candidate = -1;
  int32_t count = 0;
  for (int32_t i = listPtr; i >= 0; i--) {
    nsHtml5StackNode* node = listOfActiveFormattingElements[i];
    if (!node) {
      break;
    }
    if (node->name == name && node->attributes->equalsAnother(attributes)) {
      candidate = i;
      ++count;
    }
  }
  if (count >= 3) {
    removeFromListOfActiveFormattingElements(candidate);
  }
}

int32_t nsHtml5TreeBuilder::findLastOrRoot(nsAtom* name) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML && stack[i]->name == name) {
      return i;
    }
  }
  return 0;
}

int32_t nsHtml5TreeBuilder::findLastOrRoot(int32_t group) {
  for (int32_t i = currentPtr; i > 0; i--) {
    if (stack[i]->ns == kNameSpaceID_XHTML && stack[i]->getGroup() == group) {
      return i;
    }
  }
  return 0;
}

bool nsHtml5TreeBuilder::addAttributesToBody(
    nsHtml5HtmlAttributes* attributes) {
  if (currentPtr >= 1) {
    nsHtml5StackNode* body = stack[1];
    if (body->getGroup() == nsHtml5TreeBuilder::BODY) {
      addAttributesToElement(body->node, attributes);
      return true;
    }
  }
  return false;
}

void nsHtml5TreeBuilder::addAttributesToHtml(
    nsHtml5HtmlAttributes* attributes) {
  addAttributesToElement(stack[0]->node, attributes);
}

void nsHtml5TreeBuilder::pushHeadPointerOntoStack() {
  MOZ_ASSERT(!!headPointer);
  MOZ_ASSERT(mode == AFTER_HEAD);

  silentPush(createStackNode(nsHtml5ElementName::ELT_HEAD, headPointer));
}

void nsHtml5TreeBuilder::reconstructTheActiveFormattingElements() {
  if (listPtr == -1) {
    return;
  }
  nsHtml5StackNode* mostRecent = listOfActiveFormattingElements[listPtr];
  if (!mostRecent || isInStack(mostRecent)) {
    return;
  }
  int32_t entryPos = listPtr;
  for (;;) {
    entryPos--;
    if (entryPos == -1) {
      break;
    }
    if (!listOfActiveFormattingElements[entryPos]) {
      break;
    }
    if (isInStack(listOfActiveFormattingElements[entryPos])) {
      break;
    }
  }
  while (entryPos < listPtr) {
    entryPos++;
    nsHtml5StackNode* entry = listOfActiveFormattingElements[entryPos];
    nsHtml5StackNode* current = stack[currentPtr];
    nsIContentHandle* clone;
    if (current->isFosterParenting()) {
      clone = createAndInsertFosterParentedElement(
          kNameSpaceID_XHTML, entry->name, entry->attributes->cloneAttributes(),
          htmlCreator(entry->getHtmlCreator()));
    } else {
      nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
      clone = createElement(kNameSpaceID_XHTML, entry->name,
                            entry->attributes->cloneAttributes(), currentNode,
                            htmlCreator(entry->getHtmlCreator()));
      appendElement(clone, currentNode);
    }
    nsHtml5StackNode* entryClone = createStackNode(
        entry->getFlags(), entry->ns, entry->name, clone, entry->popName,
        entry->attributes, entry->getHtmlCreator());
    entry->dropAttributes();
    push(entryClone);
    listOfActiveFormattingElements[entryPos] = entryClone;
    entry->release(this);
    entryClone->retain();
  }
}

void nsHtml5TreeBuilder::notifyUnusedStackNode(int32_t idxInStackNodes) {
  if (idxInStackNodes < stackNodesIdx) {
    stackNodesIdx = idxInStackNodes;
  }
}

nsHtml5StackNode* nsHtml5TreeBuilder::getUnusedStackNode() {
  while (stackNodesIdx < numStackNodes) {
    if (stackNodes[stackNodesIdx]->isUnused()) {
      return stackNodes[stackNodesIdx++];
    }
    stackNodesIdx++;
  }
  if (stackNodesIdx < stackNodes.length) {
    stackNodes[stackNodesIdx] = new nsHtml5StackNode(stackNodesIdx);
    numStackNodes++;
    return stackNodes[stackNodesIdx++];
  }
  jArray<nsHtml5StackNode*, int32_t> newStack =
      jArray<nsHtml5StackNode*, int32_t>::newJArray(stackNodes.length + 64);
  nsHtml5ArrayCopy::arraycopy(stackNodes, newStack, stackNodes.length);
  stackNodes = newStack;
  stackNodes[stackNodesIdx] = new nsHtml5StackNode(stackNodesIdx);
  numStackNodes++;
  return stackNodes[stackNodesIdx++];
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    int32_t flags, int32_t ns, nsAtom* name, nsIContentHandle* node,
    nsAtom* popName, nsHtml5HtmlAttributes* attributes,
    mozilla::dom::HTMLContentCreatorFunction htmlCreator) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(flags, ns, name, node, popName, attributes, htmlCreator);
  return instance;
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    nsHtml5ElementName* elementName, nsIContentHandle* node) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(elementName, node);
  return instance;
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    nsHtml5ElementName* elementName, nsIContentHandle* node,
    nsHtml5HtmlAttributes* attributes) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(elementName, node, attributes);
  return instance;
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    nsHtml5ElementName* elementName, nsIContentHandle* node, nsAtom* popName) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(elementName, node, popName);
  return instance;
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    nsHtml5ElementName* elementName, nsAtom* popName, nsIContentHandle* node) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(elementName, popName, node);
  return instance;
}

nsHtml5StackNode* nsHtml5TreeBuilder::createStackNode(
    nsHtml5ElementName* elementName, nsIContentHandle* node, nsAtom* popName,
    bool markAsIntegrationPoint) {
  nsHtml5StackNode* instance = getUnusedStackNode();
  instance->setValues(elementName, node, popName, markAsIntegrationPoint);
  return instance;
}

void nsHtml5TreeBuilder::insertIntoFosterParent(nsIContentHandle* child) {
  int32_t tablePos = findLastOrRoot(nsHtml5TreeBuilder::TABLE);
  int32_t templatePos = findLastOrRoot(nsHtml5TreeBuilder::TEMPLATE);
  if (templatePos >= tablePos) {
    appendElement(child, stack[templatePos]->node);
    return;
  }
  nsHtml5StackNode* node = stack[tablePos];
  insertFosterParentedChild(child, node->node, stack[tablePos - 1]->node);
}

nsIContentHandle* nsHtml5TreeBuilder::createAndInsertFosterParentedElement(
    int32_t ns, nsAtom* name, nsHtml5HtmlAttributes* attributes,
    nsHtml5ContentCreatorFunction creator) {
  return createAndInsertFosterParentedElement(ns, name, attributes, nullptr,
                                              creator);
}

nsIContentHandle* nsHtml5TreeBuilder::createAndInsertFosterParentedElement(
    int32_t ns, nsAtom* name, nsHtml5HtmlAttributes* attributes,
    nsIContentHandle* form, nsHtml5ContentCreatorFunction creator) {
  int32_t tablePos = findLastOrRoot(nsHtml5TreeBuilder::TABLE);
  int32_t templatePos = findLastOrRoot(nsHtml5TreeBuilder::TEMPLATE);
  if (templatePos >= tablePos) {
    nsIContentHandle* child = createElement(ns, name, attributes, form,
                                            stack[templatePos]->node, creator);
    appendElement(child, stack[templatePos]->node);
    return child;
  }
  nsHtml5StackNode* node = stack[tablePos];
  return createAndInsertFosterParentedElement(
      ns, name, attributes, form, node->node, stack[tablePos - 1]->node,
      creator);
}

bool nsHtml5TreeBuilder::isInStack(nsHtml5StackNode* node) {
  for (int32_t i = currentPtr; i >= 0; i--) {
    if (stack[i] == node) {
      return true;
    }
  }
  return false;
}

void nsHtml5TreeBuilder::popTemplateMode() { templateModePtr--; }

void nsHtml5TreeBuilder::pop() {
  nsHtml5StackNode* node = stack[currentPtr];
  MOZ_ASSERT(debugOnlyClearLastStackSlot());
  if (node->getGroup() == OPTION) {
    optionElementPopped(node->node);
  }
  currentPtr--;
  elementPopped(node->ns, node->popName, node->node);
  node->release(this);
}

void nsHtml5TreeBuilder::popForeign(int32_t origPos, int32_t eltPos) {
  nsHtml5StackNode* node = stack[currentPtr];
  if (origPos != currentPtr || eltPos != currentPtr) {
    markMalformedIfScript(node->node);
  }
  MOZ_ASSERT(debugOnlyClearLastStackSlot());
  if (node->getGroup() == OPTION) {
    optionElementPopped(node->node);
  }
  currentPtr--;
  elementPopped(node->ns, node->popName, node->node);
  node->release(this);
}

void nsHtml5TreeBuilder::silentPop() {
  nsHtml5StackNode* node = stack[currentPtr];
  MOZ_ASSERT(debugOnlyClearLastStackSlot());
  MOZ_ASSERT(node->getGroup() != OPTION);
  currentPtr--;
  node->release(this);
}

void nsHtml5TreeBuilder::popOnEof() {
  nsHtml5StackNode* node = stack[currentPtr];
  MOZ_ASSERT(debugOnlyClearLastStackSlot());
  if (node->getGroup() == OPTION) {
    optionElementPopped(node->node);
  }
  currentPtr--;
  markMalformedIfScript(node->node);
  elementPopped(node->ns, node->popName, node->node);
  node->release(this);
}

void nsHtml5TreeBuilder::appendHtmlElementToDocumentAndPush(
    nsHtml5HtmlAttributes* attributes) {
  nsIContentHandle* elt = createHtmlElementSetAsRoot(attributes);
  nsHtml5StackNode* node = createStackNode(nsHtml5ElementName::ELT_HTML, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendHtmlElementToDocumentAndPush() {
  appendHtmlElementToDocumentAndPush(tokenizer->emptyAttributes());
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushHeadElement(
    nsHtml5HtmlAttributes* attributes) {
  nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
  nsIContentHandle* elt =
      createElement(kNameSpaceID_XHTML, nsGkAtoms::head, attributes,
                    currentNode, htmlCreator(NS_NewHTMLSharedElement));
  appendElement(elt, currentNode);
  headPointer = elt;
  nsHtml5StackNode* node = createStackNode(nsHtml5ElementName::ELT_HEAD, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushBodyElement(
    nsHtml5HtmlAttributes* attributes) {
  appendToCurrentNodeAndPushElement(nsHtml5ElementName::ELT_BODY, attributes);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushBodyElement() {
  appendToCurrentNodeAndPushBodyElement(tokenizer->emptyAttributes());
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushFormElementMayFoster(
    nsHtml5HtmlAttributes* attributes) {
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, nsGkAtoms::form, attributes,
        htmlCreator(NS_NewHTMLFormElement));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_XHTML, nsGkAtoms::form, attributes,
                        currentNode, htmlCreator(NS_NewHTMLFormElement));
    appendElement(elt, currentNode);
  }
  if (!isTemplateContents()) {
    formPointer = elt;
  }
  nsHtml5StackNode* node = createStackNode(nsHtml5ElementName::ELT_FORM, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushFormattingElementMayFoster(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsHtml5HtmlAttributes* clone = attributes->cloneAttributes();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, elementName->getName(), attributes,
        htmlCreator(elementName->getHtmlCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt =
        createElement(kNameSpaceID_XHTML, elementName->getName(), attributes,
                      currentNode, htmlCreator(elementName->getHtmlCreator()));
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node = createStackNode(elementName, elt, clone);
  push(node);
  append(node);
  node->retain();
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushElement(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
  nsIContentHandle* elt =
      createElement(kNameSpaceID_XHTML, elementName->getName(), attributes,
                    currentNode, htmlCreator(elementName->getHtmlCreator()));
  if (nsHtml5ElementName::ELT_TEMPLATE == elementName) {
    nsIContentHandle* root =
        getDeclarativeShadowRoot(currentNode, elt, attributes);
    if (root) {
      setDocumentFragmentForTemplate(elt, root);
      elt = root;
    } else {
      appendElement(elt, currentNode);
      elt = getDocumentFragmentForTemplate(elt);
    }
  } else {
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node = createStackNode(elementName, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushElementMayFoster(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getName();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, popName, attributes,
        htmlCreator(elementName->getHtmlCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_XHTML, popName, attributes, currentNode,
                        htmlCreator(elementName->getHtmlCreator()));
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node = createStackNode(elementName, elt, popName);
  push(node);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushElementMayFosterMathML(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getName();
  bool markAsHtmlIntegrationPoint = false;
  if (nsHtml5ElementName::ELT_ANNOTATION_XML == elementName &&
      annotationXmlEncodingPermitsHtml(attributes)) {
    markAsHtmlIntegrationPoint = true;
  }
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_MathML, popName, attributes, htmlCreator(nullptr));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_MathML, popName, attributes, currentNode,
                        htmlCreator(nullptr));
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node =
      createStackNode(elementName, elt, popName, markAsHtmlIntegrationPoint);
  push(node);
}

bool nsHtml5TreeBuilder::annotationXmlEncodingPermitsHtml(
    nsHtml5HtmlAttributes* attributes) {
  nsHtml5String encoding =
      attributes->getValue(nsHtml5AttributeName::ATTR_ENCODING);
  if (!encoding) {
    return false;
  }
  return nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
             "application/xhtml+xml", encoding) ||
         nsHtml5Portability::lowerCaseLiteralEqualsIgnoreAsciiCaseString(
             "text/html", encoding);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushElementMayFosterSVG(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getCamelCaseName();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_SVG, popName, attributes,
        svgCreator(elementName->getSvgCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_SVG, popName, attributes, currentNode,
                        svgCreator(elementName->getSvgCreator()));
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node = createStackNode(elementName, popName, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendToCurrentNodeAndPushElementMayFoster(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes,
    nsIContentHandle* form) {
  nsIContentHandle* elt;
  nsIContentHandle* formOwner =
      !form || fragment || isTemplateContents() ? nullptr : form;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, elementName->getName(), attributes, formOwner,
        htmlCreator(elementName->getHtmlCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_XHTML, elementName->getName(), attributes,
                        formOwner, currentNode,
                        htmlCreator(elementName->getHtmlCreator()));
    appendElement(elt, currentNode);
  }
  nsHtml5StackNode* node = createStackNode(elementName, elt);
  push(node);
}

void nsHtml5TreeBuilder::appendVoidElementToCurrent(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getName();
  nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
  nsIContentHandle* elt =
      createElement(kNameSpaceID_XHTML, popName, attributes, currentNode,
                    htmlCreator(elementName->getHtmlCreator()));
  appendElement(elt, currentNode);
  elementPushed(kNameSpaceID_XHTML, popName, elt);
  elementPopped(kNameSpaceID_XHTML, popName, elt);
}

void nsHtml5TreeBuilder::appendVoidElementToCurrentMayFoster(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes,
    nsIContentHandle* form) {
  nsAtom* name = elementName->getName();
  nsIContentHandle* elt;
  nsIContentHandle* formOwner =
      !form || fragment || isTemplateContents() ? nullptr : form;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, name, attributes, formOwner,
        htmlCreator(elementName->getHtmlCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt =
        createElement(kNameSpaceID_XHTML, name, attributes, formOwner,
                      currentNode, htmlCreator(elementName->getHtmlCreator()));
    appendElement(elt, currentNode);
  }
  elementPushed(kNameSpaceID_XHTML, name, elt);
  elementPopped(kNameSpaceID_XHTML, name, elt);
}

void nsHtml5TreeBuilder::appendVoidElementToCurrentMayFoster(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getName();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_XHTML, popName, attributes,
        htmlCreator(elementName->getHtmlCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_XHTML, popName, attributes, currentNode,
                        htmlCreator(elementName->getHtmlCreator()));
    appendElement(elt, currentNode);
  }
  elementPushed(kNameSpaceID_XHTML, popName, elt);
  elementPopped(kNameSpaceID_XHTML, popName, elt);
}

void nsHtml5TreeBuilder::appendVoidElementToCurrentMayFosterSVG(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getCamelCaseName();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_SVG, popName, attributes,
        svgCreator(elementName->getSvgCreator()));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_SVG, popName, attributes, currentNode,
                        svgCreator(elementName->getSvgCreator()));
    appendElement(elt, currentNode);
  }
  elementPushed(kNameSpaceID_SVG, popName, elt);
  elementPopped(kNameSpaceID_SVG, popName, elt);
}

void nsHtml5TreeBuilder::appendVoidElementToCurrentMayFosterMathML(
    nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes) {
  nsAtom* popName = elementName->getName();
  nsIContentHandle* elt;
  nsHtml5StackNode* current = stack[currentPtr];
  if (current->isFosterParenting()) {
    elt = createAndInsertFosterParentedElement(
        kNameSpaceID_MathML, popName, attributes, htmlCreator(nullptr));
  } else {
    nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
    elt = createElement(kNameSpaceID_MathML, popName, attributes, currentNode,
                        htmlCreator(nullptr));
    appendElement(elt, currentNode);
  }
  elementPushed(kNameSpaceID_MathML, popName, elt);
  elementPopped(kNameSpaceID_MathML, popName, elt);
}

void nsHtml5TreeBuilder::appendVoidInputToCurrent(
    nsHtml5HtmlAttributes* attributes, nsIContentHandle* form) {
  nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
  nsIContentHandle* elt =
      createElement(kNameSpaceID_XHTML, nsGkAtoms::input, attributes,
                    !form || fragment || isTemplateContents() ? nullptr : form,
                    currentNode, htmlCreator(NS_NewHTMLInputElement));
  appendElement(elt, currentNode);
  elementPushed(kNameSpaceID_XHTML, nsGkAtoms::input, elt);
  elementPopped(kNameSpaceID_XHTML, nsGkAtoms::input, elt);
}

void nsHtml5TreeBuilder::appendVoidFormToCurrent(
    nsHtml5HtmlAttributes* attributes) {
  nsIContentHandle* currentNode = nodeFromStackWithBlinkCompat(currentPtr);
  nsIContentHandle* elt =
      createElement(kNameSpaceID_XHTML, nsGkAtoms::form, attributes,
                    currentNode, htmlCreator(NS_NewHTMLFormElement));
  formPointer = elt;
  appendElement(elt, currentNode);
  elementPushed(kNameSpaceID_XHTML, nsGkAtoms::form, elt);
  elementPopped(kNameSpaceID_XHTML, nsGkAtoms::form, elt);
}

void nsHtml5TreeBuilder::requestSuspension() {
  tokenizer->requestSuspension();
}

;
bool nsHtml5TreeBuilder::isInForeign() {
  return currentPtr >= 0 && stack[currentPtr]->ns != kNameSpaceID_XHTML;
}

bool nsHtml5TreeBuilder::isInForeignButNotHtmlOrMathTextIntegrationPoint() {
  if (currentPtr < 0) {
    return false;
  }
  return !isSpecialParentInForeign(stack[currentPtr]);
}

void nsHtml5TreeBuilder::setFragmentContext(nsAtom* context, int32_t ns,
                                            nsIContentHandle* node,
                                            bool quirks) {
  this->contextName = context;
  this->contextNamespace = ns;
  this->contextNode = node;
  this->fragment = (!!contextName);
  this->quirks = quirks;
}

nsIContentHandle* nsHtml5TreeBuilder::currentNode() {
  return stack[currentPtr]->node;
}

bool nsHtml5TreeBuilder::isScriptingEnabled() { return scriptingEnabled; }

void nsHtml5TreeBuilder::setScriptingEnabled(bool scriptingEnabled) {
  this->scriptingEnabled = scriptingEnabled;
}

void nsHtml5TreeBuilder::setForceNoQuirks(bool forceNoQuirks) {
  this->forceNoQuirks = forceNoQuirks;
}

void nsHtml5TreeBuilder::setIsSrcdocDocument(bool isSrcdocDocument) {
  this->setForceNoQuirks(isSrcdocDocument);
}

bool nsHtml5TreeBuilder::isAllowDeclarativeShadowRoots() {
  return allowDeclarativeShadowRoots;
}

void nsHtml5TreeBuilder::setAllowDeclarativeShadowRoots(bool allow) {
  allowDeclarativeShadowRoots = allow;
}

bool nsHtml5TreeBuilder::isNoInSelectMode() { return noInSelectMode; }

void nsHtml5TreeBuilder::setNoInSelectMode(bool mode) { noInSelectMode = mode; }

void nsHtml5TreeBuilder::flushCharacters() {
  if (charBufferLen > 0) {
    if ((mode == IN_TABLE || mode == IN_TABLE_BODY || mode == IN_ROW) &&
        charBufferContainsNonWhitespace()) {
      errNonSpaceInTable();
      reconstructTheActiveFormattingElements();
      if (!stack[currentPtr]->isFosterParenting()) {
        appendCharacters(currentNode(), charBuffer, 0, charBufferLen);
        charBufferLen = 0;
        return;
      }
      int32_t tablePos = findLastOrRoot(nsHtml5TreeBuilder::TABLE);
      int32_t templatePos = findLastOrRoot(nsHtml5TreeBuilder::TEMPLATE);
      if (templatePos >= tablePos) {
        appendCharacters(stack[templatePos]->node, charBuffer, 0,
                         charBufferLen);
        charBufferLen = 0;
        return;
      }
      nsHtml5StackNode* tableElt = stack[tablePos];
      insertFosterParentedCharacters(charBuffer, 0, charBufferLen,
                                     tableElt->node, stack[tablePos - 1]->node);
      charBufferLen = 0;
      return;
    }
    appendCharacters(currentNode(), charBuffer, 0, charBufferLen);
    charBufferLen = 0;
  }
}

bool nsHtml5TreeBuilder::charBufferContainsNonWhitespace() {
  for (int32_t i = 0; i < charBufferLen; i++) {
    switch (charBuffer[i]) {
      case ' ':
      case '\t':
      case '\n':
      case '\r':
      case '\f': {
        continue;
      }
      default: {
        return true;
      }
    }
  }
  return false;
}

nsAHtml5TreeBuilderState* nsHtml5TreeBuilder::newSnapshot() {
  jArray<nsHtml5StackNode*, int32_t> listCopy =
      jArray<nsHtml5StackNode*, int32_t>::newJArray(listPtr + 1);
  for (int32_t i = 0; i < listCopy.length; i++) {
    nsHtml5StackNode* node = listOfActiveFormattingElements[i];
    if (node) {
      nsHtml5StackNode* newNode = new nsHtml5StackNode(-1);
      newNode->setValues(node->getFlags(), node->ns, node->name, node->node,
                         node->popName, node->attributes->cloneAttributes(),
                         node->getHtmlCreator());
      listCopy[i] = newNode;
    } else {
      listCopy[i] = nullptr;
    }
  }
  jArray<nsHtml5StackNode*, int32_t> stackCopy =
      jArray<nsHtml5StackNode*, int32_t>::newJArray(currentPtr + 1);
  for (int32_t i = 0; i < stackCopy.length; i++) {
    nsHtml5StackNode* node = stack[i];
    int32_t listIndex = findInListOfActiveFormattingElements(node);
    if (listIndex == -1) {
      nsHtml5StackNode* newNode = new nsHtml5StackNode(-1);
      newNode->setValues(node->getFlags(), node->ns, node->name, node->node,
                         node->popName, nullptr, node->getHtmlCreator());
      stackCopy[i] = newNode;
    } else {
      stackCopy[i] = listCopy[listIndex];
      stackCopy[i]->retain();
    }
  }
  jArray<int32_t, int32_t> templateModeStackCopy =
      jArray<int32_t, int32_t>::newJArray(templateModePtr + 1);
  nsHtml5ArrayCopy::arraycopy(templateModeStack, templateModeStackCopy,
                              templateModeStackCopy.length);
  return new nsHtml5StateSnapshot(stackCopy, listCopy, templateModeStackCopy,
                                  formPointer, headPointer, mode, originalMode,
                                  framesetOk, needToDropLF, quirks);
}

bool nsHtml5TreeBuilder::snapshotMatches(nsAHtml5TreeBuilderState* snapshot) {
  jArray<nsHtml5StackNode*, int32_t> stackCopy = snapshot->getStack();
  int32_t stackLen = snapshot->getStackLength();
  jArray<nsHtml5StackNode*, int32_t> listCopy =
      snapshot->getListOfActiveFormattingElements();
  int32_t listLen = snapshot->getListOfActiveFormattingElementsLength();
  jArray<int32_t, int32_t> templateModeStackCopy =
      snapshot->getTemplateModeStack();
  int32_t templateModeStackLen = snapshot->getTemplateModeStackLength();
  if (stackLen != currentPtr + 1 || listLen != listPtr + 1 ||
      templateModeStackLen != templateModePtr + 1 ||
      formPointer != snapshot->getFormPointer() ||
      headPointer != snapshot->getHeadPointer() ||
      mode != snapshot->getMode() ||
      originalMode != snapshot->getOriginalMode() ||
      framesetOk != snapshot->isFramesetOk() ||
      needToDropLF != snapshot->isNeedToDropLF() ||
      quirks != snapshot->isQuirks()) {
    return false;
  }
  for (int32_t i = listLen - 1; i >= 0; i--) {
    if (!listCopy[i] && !listOfActiveFormattingElements[i]) {
      continue;
    } else if (!listCopy[i] || !listOfActiveFormattingElements[i]) {
      return false;
    }
    if (listCopy[i]->node != listOfActiveFormattingElements[i]->node) {
      return false;
    }
  }
  for (int32_t i = stackLen - 1; i >= 0; i--) {
    if (stackCopy[i]->node != stack[i]->node) {
      return false;
    }
  }
  for (int32_t i = templateModeStackLen - 1; i >= 0; i--) {
    if (templateModeStackCopy[i] != templateModeStack[i]) {
      return false;
    }
  }
  return true;
}

void nsHtml5TreeBuilder::loadState(nsAHtml5TreeBuilderState* snapshot) {
  mCurrentHtmlScriptCannotDocumentWriteOrBlock = false;
  jArray<nsHtml5StackNode*, int32_t> stackCopy = snapshot->getStack();
  int32_t stackLen = snapshot->getStackLength();
  jArray<nsHtml5StackNode*, int32_t> listCopy =
      snapshot->getListOfActiveFormattingElements();
  int32_t listLen = snapshot->getListOfActiveFormattingElementsLength();
  jArray<int32_t, int32_t> templateModeStackCopy =
      snapshot->getTemplateModeStack();
  int32_t templateModeStackLen = snapshot->getTemplateModeStackLength();
  for (int32_t i = 0; i <= listPtr; i++) {
    if (listOfActiveFormattingElements[i]) {
      listOfActiveFormattingElements[i]->release(this);
    }
  }
  if (listOfActiveFormattingElements.length < listLen) {
    listOfActiveFormattingElements =
        jArray<nsHtml5StackNode*, int32_t>::newJArray(listLen);
  }
  listPtr = listLen - 1;
  for (int32_t i = 0; i <= currentPtr; i++) {
    stack[i]->release(this);
  }
  if (stack.length < stackLen) {
    stack = jArray<nsHtml5StackNode*, int32_t>::newJArray(stackLen);
  }
  currentPtr = stackLen - 1;
  if (templateModeStack.length < templateModeStackLen) {
    templateModeStack =
        jArray<int32_t, int32_t>::newJArray(templateModeStackLen);
  }
  templateModePtr = templateModeStackLen - 1;
  for (int32_t i = 0; i < listLen; i++) {
    nsHtml5StackNode* node = listCopy[i];
    if (node) {
      nsHtml5StackNode* newNode = createStackNode(
          node->getFlags(), node->ns, node->name, node->node, node->popName,
          node->attributes->cloneAttributes(), node->getHtmlCreator());
      listOfActiveFormattingElements[i] = newNode;
    } else {
      listOfActiveFormattingElements[i] = nullptr;
    }
  }
  for (int32_t i = 0; i < stackLen; i++) {
    nsHtml5StackNode* node = stackCopy[i];
    int32_t listIndex = findInArray(node, listCopy);
    if (listIndex == -1) {
      nsHtml5StackNode* newNode =
          createStackNode(node->getFlags(), node->ns, node->name, node->node,
                          node->popName, nullptr, node->getHtmlCreator());
      stack[i] = newNode;
    } else {
      stack[i] = listOfActiveFormattingElements[listIndex];
      stack[i]->retain();
    }
  }
  nsHtml5ArrayCopy::arraycopy(templateModeStackCopy, templateModeStack,
                              templateModeStackLen);
  formPointer = snapshot->getFormPointer();
  headPointer = snapshot->getHeadPointer();
  mode = snapshot->getMode();
  originalMode = snapshot->getOriginalMode();
  framesetOk = snapshot->isFramesetOk();
  needToDropLF = snapshot->isNeedToDropLF();
  quirks = snapshot->isQuirks();
}

int32_t nsHtml5TreeBuilder::findInArray(
    nsHtml5StackNode* node, jArray<nsHtml5StackNode*, int32_t> arr) {
  for (int32_t i = listPtr; i >= 0; i--) {
    if (node == arr[i]) {
      return i;
    }
  }
  return -1;
}

nsIContentHandle* nsHtml5TreeBuilder::nodeFromStackWithBlinkCompat(
    int32_t stackPos) {
  if (stackPos > 511) {
    errDeepTree();
    return stack[511]->node;
  }
  return stack[stackPos]->node;
}

nsIContentHandle* nsHtml5TreeBuilder::getFormPointer() { return formPointer; }

nsIContentHandle* nsHtml5TreeBuilder::getHeadPointer() { return headPointer; }

jArray<nsHtml5StackNode*, int32_t>
nsHtml5TreeBuilder::getListOfActiveFormattingElements() {
  return listOfActiveFormattingElements;
}

jArray<nsHtml5StackNode*, int32_t> nsHtml5TreeBuilder::getStack() {
  return stack;
}

jArray<int32_t, int32_t> nsHtml5TreeBuilder::getTemplateModeStack() {
  return templateModeStack;
}

int32_t nsHtml5TreeBuilder::getMode() { return mode; }

int32_t nsHtml5TreeBuilder::getOriginalMode() { return originalMode; }

bool nsHtml5TreeBuilder::isFramesetOk() { return framesetOk; }

bool nsHtml5TreeBuilder::isNeedToDropLF() { return needToDropLF; }

bool nsHtml5TreeBuilder::isQuirks() { return quirks; }

int32_t nsHtml5TreeBuilder::getListOfActiveFormattingElementsLength() {
  return listPtr + 1;
}

int32_t nsHtml5TreeBuilder::getStackLength() { return currentPtr + 1; }

int32_t nsHtml5TreeBuilder::getTemplateModeStackLength() {
  return templateModePtr + 1;
}

void nsHtml5TreeBuilder::initializeStatics() {}

void nsHtml5TreeBuilder::releaseStatics() {}

#include "nsHtml5TreeBuilderCppSupplement.h"
