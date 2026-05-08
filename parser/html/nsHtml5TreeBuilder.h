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

#ifndef nsHtml5TreeBuilder_h
#define nsHtml5TreeBuilder_h

#include "jArray.h"
#include "mozilla/ImportScanner.h"
#include "nsAHtml5TreeBuilderState.h"
#include "nsAtom.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsHtml5ArrayCopy.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5DocumentMode.h"
#include "nsHtml5Highlighter.h"
#include "nsHtml5OplessBuilder.h"
#include "nsHtml5Parser.h"
#include "nsHtml5PlainTextUtils.h"
#include "nsHtml5StackNode.h"
#include "nsHtml5StateSnapshot.h"
#include "nsHtml5StreamParser.h"
#include "nsHtml5String.h"
#include "nsHtml5TreeOperation.h"
#include "nsHtml5TreeOpExecutor.h"
#include "nsHtml5ViewSourceUtils.h"
#include "nsIContent.h"
#include "nsIContentHandle.h"
#include "nsNameSpaceManager.h"
#include "nsTraceRefcnt.h"

class nsHtml5StreamParser;

class nsHtml5AttributeName;
class nsHtml5ElementName;
class nsHtml5Tokenizer;
class nsHtml5UTF16Buffer;
class nsHtml5StateSnapshot;
class nsHtml5Portability;

class nsHtml5TreeBuilder : public nsAHtml5TreeBuilderState {
 private:
  static char16_t REPLACEMENT_CHARACTER[];

 public:
  static const int32_t OTHER = 0;

  static const int32_t A = 1;

  static const int32_t BASE = 2;

  static const int32_t BODY = 3;

  static const int32_t BR = 4;

  static const int32_t BUTTON = 5;

  static const int32_t CAPTION = 6;

  static const int32_t COL = 7;

  static const int32_t COLGROUP = 8;

  static const int32_t FORM = 9;

  static const int32_t FRAME = 10;

  static const int32_t FRAMESET = 11;

  static const int32_t IMAGE = 12;

  static const int32_t INPUT = 13;

  static const int32_t RT_OR_RP = 14;

  static const int32_t LI = 15;

  static const int32_t LINK_OR_BASEFONT_OR_BGSOUND = 16;

  static const int32_t MATH = 17;

  static const int32_t META = 18;

  static const int32_t SVG = 19;

  static const int32_t HEAD = 20;

  static const int32_t HR = 22;

  static const int32_t HTML = 23;

  static const int32_t NOBR = 24;

  static const int32_t NOFRAMES = 25;

  static const int32_t NOSCRIPT = 26;

  static const int32_t OPTGROUP = 27;

  static const int32_t OPTION = 28;

  static const int32_t P = 29;

  static const int32_t PLAINTEXT = 30;

  static const int32_t SCRIPT = 31;

  static const int32_t SELECT = 32;

  static const int32_t STYLE = 33;

  static const int32_t TABLE = 34;

  static const int32_t TEXTAREA = 35;

  static const int32_t TITLE = 36;

  static const int32_t TR = 37;

  static const int32_t XMP = 38;

  static const int32_t TBODY_OR_THEAD_OR_TFOOT = 39;

  static const int32_t TD_OR_TH = 40;

  static const int32_t DD_OR_DT = 41;

  static const int32_t H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 = 42;

  static const int32_t MARQUEE_OR_APPLET = 43;

  static const int32_t PRE_OR_LISTING = 44;

  static const int32_t
      B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U =
          45;

  static const int32_t UL_OR_OL_OR_DL = 46;

  static const int32_t IFRAME = 47;

  static const int32_t EMBED = 48;

  static const int32_t AREA_OR_WBR = 49;

  static const int32_t DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU = 50;

  static const int32_t
      ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY =
          51;

  static const int32_t RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR = 52;

  static const int32_t RB_OR_RTC = 53;

  static const int32_t PARAM_OR_SOURCE_OR_TRACK = 55;

  static const int32_t MGLYPH_OR_MALIGNMARK = 56;

  static const int32_t MI_MO_MN_MS_MTEXT = 57;

  static const int32_t ANNOTATION_XML = 58;

  static const int32_t FOREIGNOBJECT_OR_DESC = 59;

  static const int32_t NOEMBED = 60;

  static const int32_t FIELDSET = 61;

  static const int32_t OUTPUT = 62;

  static const int32_t OBJECT = 63;

  static const int32_t FONT = 64;

  static const int32_t KEYGEN = 65;

  static const int32_t TEMPLATE = 66;

  static const int32_t IMG = 67;

 private:
  static const int32_t IN_ROW = 0;

  static const int32_t IN_TABLE_BODY = 1;

  static const int32_t IN_TABLE = 2;

  static const int32_t IN_CAPTION = 3;

  static const int32_t IN_CELL = 4;

  static const int32_t FRAMESET_OK = 5;

  static const int32_t IN_BODY = 6;

  static const int32_t IN_HEAD = 7;

  static const int32_t IN_HEAD_NOSCRIPT = 8;

  static const int32_t IN_COLUMN_GROUP = 9;

  static const int32_t IN_SELECT_IN_TABLE = 10;

  static const int32_t IN_SELECT = 11;

  static const int32_t AFTER_BODY = 12;

  static const int32_t IN_FRAMESET = 13;

  static const int32_t AFTER_FRAMESET = 14;

  static const int32_t INITIAL = 15;

  static const int32_t BEFORE_HTML = 16;

  static const int32_t BEFORE_HEAD = 17;

  static const int32_t AFTER_HEAD = 18;

  static const int32_t AFTER_AFTER_BODY = 19;

  static const int32_t AFTER_AFTER_FRAMESET = 20;

  static const int32_t TEXT = 21;

  static const int32_t IN_TEMPLATE = 22;

  static const int32_t CHARSET_INITIAL = 0;

  static const int32_t CHARSET_C = 1;

  static const int32_t CHARSET_H = 2;

  static const int32_t CHARSET_A = 3;

  static const int32_t CHARSET_R = 4;

  static const int32_t CHARSET_S = 5;

  static const int32_t CHARSET_E = 6;

  static const int32_t CHARSET_T = 7;

  static const int32_t CHARSET_EQUALS = 8;

  static const int32_t CHARSET_SINGLE_QUOTED = 9;

  static const int32_t CHARSET_DOUBLE_QUOTED = 10;

  static const int32_t CHARSET_UNQUOTED = 11;

  static staticJArray<const char*, int32_t> QUIRKY_PUBLIC_IDS;
  static const int32_t NOT_FOUND_ON_STACK = INT32_MAX;

  int32_t mode;
  int32_t originalMode;
  bool framesetOk;

 protected:
  nsHtml5Tokenizer* tokenizer;

 private:
  bool scriptingEnabled;
  bool needToDropLF;
  bool fragment;
  RefPtr<nsAtom> contextName;
  int32_t contextNamespace;
  nsIContentHandle* contextNode;
  autoJArray<int32_t, int32_t> templateModeStack;
  int32_t templateModePtr;
  autoJArray<nsHtml5StackNode*, int32_t> stackNodes;
  int32_t stackNodesIdx;
  int32_t numStackNodes;
  autoJArray<nsHtml5StackNode*, int32_t> stack;
  int32_t currentPtr;
  autoJArray<nsHtml5StackNode*, int32_t> listOfActiveFormattingElements;
  int32_t listPtr;
  nsIContentHandle* formPointer;
  nsIContentHandle* headPointer;

 protected:
  autoJArray<char16_t, int32_t> charBuffer;
  int32_t charBufferLen;

 private:
  bool quirks;
  bool forceNoQuirks;
  bool allowDeclarativeShadowRoots;
  bool noInSelectMode;
  bool keepBuffer;
  inline nsHtml5ContentCreatorFunction htmlCreator(
      mozilla::dom::HTMLContentCreatorFunction htmlCreator) {
    nsHtml5ContentCreatorFunction creator;
    creator.html = htmlCreator;
    return creator;
  }

  inline nsHtml5ContentCreatorFunction svgCreator(
      mozilla::dom::SVGContentCreatorFunction svgCreator) {
    nsHtml5ContentCreatorFunction creator;
    creator.svg = svgCreator;
    return creator;
  }

 public:
  void setKeepBuffer(bool keepBuffer);
  bool dropBufferIfLongerThan(int32_t length);
  void startTokenization(nsHtml5Tokenizer* self);
  void doctype(nsAtom* name, nsHtml5String publicIdentifier,
               nsHtml5String systemIdentifier, bool forceQuirks);
  void comment(char16_t* buf, int32_t start, int32_t length);
  void characters(const char16_t* buf, int32_t start, int32_t length);
  void zeroOriginatingReplacementCharacter();
  void zeroOrReplacementCharacter();
  void eof();
  void endTokenization();
  void startTag(nsHtml5ElementName* elementName,
                nsHtml5HtmlAttributes* attributes, bool selfClosing);

 private:
  void startTagTitleInHead(nsHtml5ElementName* elementName,
                           nsHtml5HtmlAttributes* attributes);
  void startTagGenericRawText(nsHtml5ElementName* elementName,
                              nsHtml5HtmlAttributes* attributes);
  void startTagScriptInHead(nsHtml5ElementName* elementName,
                            nsHtml5HtmlAttributes* attributes);
  void startTagTemplateInHead(nsHtml5ElementName* elementName,
                              nsHtml5HtmlAttributes* attributes);
  bool isTemplateContents();
  bool isTemplateModeStackEmpty();
  bool isSpecialParentInForeign(nsHtml5StackNode* stackNode);
  nsIContentHandle* getDeclarativeShadowRoot(nsIContentHandle* currentNode,
                                             nsIContentHandle* templateNode,
                                             nsHtml5HtmlAttributes* attributes);

 public:
  static nsHtml5String extractCharsetFromContent(nsHtml5String attributeValue,
                                                 nsHtml5TreeBuilder* tb);

 private:
  void checkMetaCharset(nsHtml5HtmlAttributes* attributes);

 public:
  void endTag(nsHtml5ElementName* elementName);

 private:
  void endTagTemplateInHead();
  int32_t findLastInTableScopeOrRootTemplateTbodyTheadTfoot();
  int32_t findLast(nsAtom* name);
  int32_t findLastInTableScope(nsAtom* name);
  int32_t findLastInButtonScope(nsAtom* name);
  int32_t findLastInScope(nsAtom* name);
  int32_t findLastInListScope(nsAtom* name);
  int32_t findLastInScopeHn();
  void generateImpliedEndTagsExceptFor(nsAtom* name);
  void generateImpliedEndTags();
  void generateImpliedEndTagsThoroughly();
  bool isSecondOnStackBody();
  void documentModeInternal(nsHtml5DocumentMode m,
                            nsHtml5String publicIdentifier,
                            nsHtml5String systemIdentifier);
  bool isAlmostStandards(nsHtml5String publicIdentifier,
                         nsHtml5String systemIdentifier);
  bool isQuirky(nsAtom* name, nsHtml5String publicIdentifier,
                nsHtml5String systemIdentifier, bool forceQuirks);
  void closeTheCell(int32_t eltPos);
  int32_t findLastInTableScopeTdTh();
  void clearStackBackTo(int32_t eltPos);
  void resetTheInsertionMode();
  void implicitlyCloseP();
  bool debugOnlyClearLastStackSlot();
  bool debugOnlyClearLastListSlot();
  void pushTemplateMode(int32_t mode);
  void push(nsHtml5StackNode* node);
  void silentPush(nsHtml5StackNode* node);
  void append(nsHtml5StackNode* node);
  inline void insertMarker() { append(nullptr); }

  void clearTheListOfActiveFormattingElementsUpToTheLastMarker();
  inline bool isCurrent(nsAtom* name) {
    return stack[currentPtr]->ns == kNameSpaceID_XHTML &&
           name == stack[currentPtr]->name;
  }

  void removeFromStack(int32_t pos);
  void removeFromStack(nsHtml5StackNode* node);
  void removeFromListOfActiveFormattingElements(int32_t pos);
  bool adoptionAgencyEndTag(nsAtom* name);
  void insertIntoStack(nsHtml5StackNode* node, int32_t position);
  void insertIntoListOfActiveFormattingElements(
      nsHtml5StackNode* formattingClone, int32_t bookmark);
  int32_t findInListOfActiveFormattingElements(nsHtml5StackNode* node);
  int32_t findInListOfActiveFormattingElementsContainsBetweenEndAndLastMarker(
      nsAtom* name);
  void maybeForgetEarlierDuplicateFormattingElement(
      nsAtom* name, nsHtml5HtmlAttributes* attributes);
  int32_t findLastOrRoot(nsAtom* name);
  int32_t findLastOrRoot(int32_t group);
  bool addAttributesToBody(nsHtml5HtmlAttributes* attributes);
  void addAttributesToHtml(nsHtml5HtmlAttributes* attributes);
  void pushHeadPointerOntoStack();
  void reconstructTheActiveFormattingElements();

 public:
  void notifyUnusedStackNode(int32_t idxInStackNodes);

 private:
  nsHtml5StackNode* getUnusedStackNode();
  nsHtml5StackNode* createStackNode(
      int32_t flags, int32_t ns, nsAtom* name, nsIContentHandle* node,
      nsAtom* popName, nsHtml5HtmlAttributes* attributes,
      mozilla::dom::HTMLContentCreatorFunction htmlCreator);
  nsHtml5StackNode* createStackNode(nsHtml5ElementName* elementName,
                                    nsIContentHandle* node);
  nsHtml5StackNode* createStackNode(nsHtml5ElementName* elementName,
                                    nsIContentHandle* node,
                                    nsHtml5HtmlAttributes* attributes);
  nsHtml5StackNode* createStackNode(nsHtml5ElementName* elementName,
                                    nsIContentHandle* node, nsAtom* popName);
  nsHtml5StackNode* createStackNode(nsHtml5ElementName* elementName,
                                    nsAtom* popName, nsIContentHandle* node);
  nsHtml5StackNode* createStackNode(nsHtml5ElementName* elementName,
                                    nsIContentHandle* node, nsAtom* popName,
                                    bool markAsIntegrationPoint);
  void insertIntoFosterParent(nsIContentHandle* child);
  nsIContentHandle* createAndInsertFosterParentedElement(
      int32_t ns, nsAtom* name, nsHtml5HtmlAttributes* attributes,
      nsHtml5ContentCreatorFunction creator);
  nsIContentHandle* createAndInsertFosterParentedElement(
      int32_t ns, nsAtom* name, nsHtml5HtmlAttributes* attributes,
      nsIContentHandle* form, nsHtml5ContentCreatorFunction creator);
  bool isInStack(nsHtml5StackNode* node);
  void popTemplateMode();
  void pop();
  void popForeign(int32_t origPos, int32_t eltPos);
  void silentPop();
  void popOnEof();
  void appendHtmlElementToDocumentAndPush(nsHtml5HtmlAttributes* attributes);
  void appendHtmlElementToDocumentAndPush();
  void appendToCurrentNodeAndPushHeadElement(nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushBodyElement(nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushBodyElement();
  void appendToCurrentNodeAndPushFormElementMayFoster(
      nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushFormattingElementMayFoster(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushElement(nsHtml5ElementName* elementName,
                                         nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushElementMayFoster(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushElementMayFosterMathML(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  bool annotationXmlEncodingPermitsHtml(nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushElementMayFosterSVG(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  void appendToCurrentNodeAndPushElementMayFoster(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes,
      nsIContentHandle* form);
  void appendVoidElementToCurrent(nsHtml5ElementName* elementName,
                                  nsHtml5HtmlAttributes* attributes);
  void appendVoidElementToCurrentMayFoster(nsHtml5ElementName* elementName,
                                           nsHtml5HtmlAttributes* attributes,
                                           nsIContentHandle* form);
  void appendVoidElementToCurrentMayFoster(nsHtml5ElementName* elementName,
                                           nsHtml5HtmlAttributes* attributes);
  void appendVoidElementToCurrentMayFosterSVG(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  void appendVoidElementToCurrentMayFosterMathML(
      nsHtml5ElementName* elementName, nsHtml5HtmlAttributes* attributes);
  void appendVoidInputToCurrent(nsHtml5HtmlAttributes* attributes,
                                nsIContentHandle* form);
  void appendVoidFormToCurrent(nsHtml5HtmlAttributes* attributes);

 protected:
  void accumulateCharacters(const char16_t* buf, int32_t start, int32_t length);
  void requestSuspension();
  nsIContentHandle* createElement(int32_t ns, nsAtom* name,
                                  nsHtml5HtmlAttributes* attributes,
                                  nsIContentHandle* intendedParent,
                                  nsHtml5ContentCreatorFunction creator);
  nsIContentHandle* createElement(int32_t ns, nsAtom* name,
                                  nsHtml5HtmlAttributes* attributes,
                                  nsIContentHandle* form,
                                  nsIContentHandle* intendedParent,
                                  nsHtml5ContentCreatorFunction creator);
  nsIContentHandle* createHtmlElementSetAsRoot(
      nsHtml5HtmlAttributes* attributes);
  void detachFromParent(nsIContentHandle* element);
  bool hasChildren(nsIContentHandle* element);
  void appendElement(nsIContentHandle* child, nsIContentHandle* newParent);
  void appendChildrenToNewParent(nsIContentHandle* oldParent,
                                 nsIContentHandle* newParent);
  void insertFosterParentedChild(nsIContentHandle* child,
                                 nsIContentHandle* table,
                                 nsIContentHandle* stackParent);
  nsIContentHandle* createAndInsertFosterParentedElement(
      int32_t ns, nsAtom* name, nsHtml5HtmlAttributes* attributes,
      nsIContentHandle* form, nsIContentHandle* table,
      nsIContentHandle* stackParent, nsHtml5ContentCreatorFunction creator);
  ;
  void insertFosterParentedCharacters(char16_t* buf, int32_t start,
                                      int32_t length, nsIContentHandle* table,
                                      nsIContentHandle* stackParent);
  void appendCharacters(nsIContentHandle* parent, char16_t* buf, int32_t start,
                        int32_t length);
  void appendComment(nsIContentHandle* parent, char16_t* buf, int32_t start,
                     int32_t length);
  void appendCommentToDocument(char16_t* buf, int32_t start, int32_t length);
  void addAttributesToElement(nsIContentHandle* element,
                              nsHtml5HtmlAttributes* attributes);
  void markMalformedIfScript(nsIContentHandle* elt);
  void start(bool fragmentMode);
  void end();
  void appendDoctypeToDocument(nsAtom* name, nsHtml5String publicIdentifier,
                               nsHtml5String systemIdentifier);
  void elementPushed(int32_t ns, nsAtom* name, nsIContentHandle* node);
  void elementPopped(int32_t ns, nsAtom* name, nsIContentHandle* node);

 public:
  inline bool cdataSectionAllowed() { return isInForeign(); }

 private:
  bool isInForeign();
  bool isInForeignButNotHtmlOrMathTextIntegrationPoint();

 public:
  void setFragmentContext(nsAtom* context, int32_t ns, nsIContentHandle* node,
                          bool quirks);

 protected:
  nsIContentHandle* currentNode();

 public:
  bool isScriptingEnabled();
  void setScriptingEnabled(bool scriptingEnabled);
  void setForceNoQuirks(bool forceNoQuirks);
  void setIsSrcdocDocument(bool isSrcdocDocument);
  bool isAllowDeclarativeShadowRoots();
  void setAllowDeclarativeShadowRoots(bool allow);
  bool isNoInSelectMode();
  void setNoInSelectMode(bool mode);
  void flushCharacters();

 private:
  bool charBufferContainsNonWhitespace();

 public:
  nsAHtml5TreeBuilderState* newSnapshot();
  bool snapshotMatches(nsAHtml5TreeBuilderState* snapshot);
  void loadState(nsAHtml5TreeBuilderState* snapshot);

 private:
  int32_t findInArray(nsHtml5StackNode* node,
                      jArray<nsHtml5StackNode*, int32_t> arr);
  nsIContentHandle* nodeFromStackWithBlinkCompat(int32_t stackPos);

 public:
  nsIContentHandle* getFormPointer() override;
  nsIContentHandle* getHeadPointer() override;
  jArray<nsHtml5StackNode*, int32_t> getListOfActiveFormattingElements()
      override;
  jArray<nsHtml5StackNode*, int32_t> getStack() override;
  jArray<int32_t, int32_t> getTemplateModeStack() override;
  int32_t getMode() override;
  int32_t getOriginalMode() override;
  bool isFramesetOk() override;
  bool isNeedToDropLF() override;
  bool isQuirks() override;
  int32_t getListOfActiveFormattingElementsLength() override;
  int32_t getStackLength() override;
  int32_t getTemplateModeStackLength() override;
  static void initializeStatics();
  static void releaseStatics();

#include "nsHtml5TreeBuilderHSupplement.h"
};

#endif
