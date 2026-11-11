/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHtml5TreeOperation_h
#define nsHtml5TreeOperation_h

#include "nsHtml5DocumentMode.h"
#include "nsHtml5HtmlAttributes.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "mozilla/NotNull.h"
#include "mozilla/Variant.h"
#include "nsCharsetSource.h"

class nsIContent;
class nsHtml5TreeOpExecutor;
class nsHtml5DocumentBuilder;
namespace mozilla {
class Encoding;
template <typename T>
class Maybe;
namespace dom {
class CustomElementRegistry;
class Text;
}  // namespace dom
}  // namespace mozilla

struct uninitialized {};

// main HTML5 ops
struct opDetach {
  nsIContent** mElement;

  explicit opDetach(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opAppend {
  nsIContent** mChild;
  nsIContent** mParent;
  mozilla::dom::FromParser mFromNetwork;

  explicit opAppend(nsIContentHandle* aChild, nsIContentHandle* aParent,
                    mozilla::dom::FromParser aFromNetwork)
      : mFromNetwork(aFromNetwork) {
    mChild = static_cast<nsIContent**>(aChild);
    mParent = static_cast<nsIContent**>(aParent);
  };
};

struct opAppendChildrenToNewParent {
  nsIContent** mOldParent;
  nsIContent** mNewParent;

  explicit opAppendChildrenToNewParent(nsIContentHandle* aOldParent,
                                       nsIContentHandle* aNewParent) {
    mOldParent = static_cast<nsIContent**>(aOldParent);
    mNewParent = static_cast<nsIContent**>(aNewParent);
  };
};

struct opFosterParent {
  nsIContent** mChild;
  nsIContent** mStackParent;
  nsIContent** mTable;

  explicit opFosterParent(nsIContentHandle* aChild,
                          nsIContentHandle* aStackParent,
                          nsIContentHandle* aTable) {
    mChild = static_cast<nsIContent**>(aChild);
    mStackParent = static_cast<nsIContent**>(aStackParent);
    mTable = static_cast<nsIContent**>(aTable);
  };
};

struct opAppendToDocument {
  nsIContent** mContent;

  explicit opAppendToDocument(nsIContentHandle* aContent) {
    mContent = static_cast<nsIContent**>(aContent);
  };
};

struct opAddAttributes {
  nsIContent** mElement;
  nsHtml5HtmlAttributes* mAttributes;

  explicit opAddAttributes(nsIContentHandle* aElement,
                           nsHtml5HtmlAttributes* aAttributes)
      : mAttributes(aAttributes) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opCreateHTMLElement {
  nsIContent** mContent;
  nsAtom* mName;
  nsHtml5HtmlAttributes* mAttributes;
  mozilla::dom::HTMLContentCreatorFunction mCreator;
  nsIContent** mIntendedParent;
  mozilla::dom::FromParser mFromNetwork;

  explicit opCreateHTMLElement(
      nsIContentHandle* aContent, nsAtom* aName,
      nsHtml5HtmlAttributes* aAttributes,
      mozilla::dom::HTMLContentCreatorFunction aCreator,
      nsIContentHandle* aIntendedParent, mozilla::dom::FromParser mFromNetwork)
      : mName(aName),
        mAttributes(aAttributes),
        mCreator(aCreator),
        mFromNetwork(mFromNetwork) {
    mContent = static_cast<nsIContent**>(aContent);
    mIntendedParent = static_cast<nsIContent**>(aIntendedParent);
    aName->AddRef();
    if (mAttributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
      mAttributes = nullptr;
    }
  };
};

struct opCreateSVGElement {
  nsIContent** mContent;
  nsAtom* mName;
  nsHtml5HtmlAttributes* mAttributes;
  mozilla::dom::SVGContentCreatorFunction mCreator;
  nsIContent** mIntendedParent;
  mozilla::dom::FromParser mFromNetwork;

  explicit opCreateSVGElement(nsIContentHandle* aContent, nsAtom* aName,
                              nsHtml5HtmlAttributes* aAttributes,
                              mozilla::dom::SVGContentCreatorFunction aCreator,
                              nsIContentHandle* aIntendedParent,
                              mozilla::dom::FromParser mFromNetwork)
      : mName(aName),
        mAttributes(aAttributes),
        mCreator(aCreator),
        mFromNetwork(mFromNetwork) {
    mContent = static_cast<nsIContent**>(aContent);
    mIntendedParent = static_cast<nsIContent**>(aIntendedParent);
    aName->AddRef();
    if (mAttributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
      mAttributes = nullptr;
    }
  };
};

struct opCreateMathMLElement {
  nsIContent** mContent;
  nsAtom* mName;
  nsHtml5HtmlAttributes* mAttributes;
  nsIContent** mIntendedParent;

  explicit opCreateMathMLElement(nsIContentHandle* aContent, nsAtom* aName,
                                 nsHtml5HtmlAttributes* aAttributes,
                                 nsIContentHandle* aIntendedParent)
      : mName(aName), mAttributes(aAttributes) {
    mContent = static_cast<nsIContent**>(aContent);
    mIntendedParent = static_cast<nsIContent**>(aIntendedParent);
    aName->AddRef();
    if (mAttributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
      mAttributes = nullptr;
    }
  };
};

struct opSetFormElement {
  nsIContent** mContent;
  nsIContent** mFormElement;
  nsIContent** mIntendedParent;

  explicit opSetFormElement(nsIContentHandle* aContent,
                            nsIContentHandle* aFormElement,
                            nsIContentHandle* aIntendedParent) {
    mContent = static_cast<nsIContent**>(aContent);
    mFormElement = static_cast<nsIContent**>(aFormElement);
    mIntendedParent = static_cast<nsIContent**>(aIntendedParent);
  };
};

struct opAppendText {
  nsIContent** mParent;
  char16_t* mBuffer;
  int32_t mLength;

  explicit opAppendText(nsIContentHandle* aParent, char16_t* aBuffer,
                        int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {
    mParent = static_cast<nsIContent**>(aParent);
  };
};

struct opFosterParentText {
  nsIContent** mStackParent;
  char16_t* mBuffer;
  nsIContent** mTable;
  int32_t mLength;

  explicit opFosterParentText(nsIContentHandle* aStackParent, char16_t* aBuffer,
                              nsIContentHandle* aTable, int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {
    mStackParent = static_cast<nsIContent**>(aStackParent);
    mTable = static_cast<nsIContent**>(aTable);
  };
};

struct opAppendComment {
  nsIContent** mParent;
  char16_t* mBuffer;
  int32_t mLength;

  explicit opAppendComment(nsIContentHandle* aParent, char16_t* aBuffer,
                           int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {
    mParent = static_cast<nsIContent**>(aParent);
  };
};

// mBuffer holds the target characters followed by the data characters.
struct opAppendProcessingInstruction {
  nsIContent** mParent;
  char16_t* mBuffer;
  int32_t mTargetLength;
  int32_t mDataLength;

  explicit opAppendProcessingInstruction(nsIContentHandle* aParent,
                                         char16_t* aBuffer,
                                         int32_t aTargetLength,
                                         int32_t aDataLength)
      : mBuffer(aBuffer),
        mTargetLength(aTargetLength),
        mDataLength(aDataLength) {
    mParent = static_cast<nsIContent**>(aParent);
  };
};

struct opAppendProcessingInstructionToDocument {
  char16_t* mBuffer;
  int32_t mTargetLength;
  int32_t mDataLength;

  explicit opAppendProcessingInstructionToDocument(char16_t* aBuffer,
                                                   int32_t aTargetLength,
                                                   int32_t aDataLength)
      : mBuffer(aBuffer),
        mTargetLength(aTargetLength),
        mDataLength(aDataLength) {};
};

struct opAppendCommentToDocument {
  char16_t* mBuffer;
  int32_t mLength;

  explicit opAppendCommentToDocument(char16_t* aBuffer, int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {};
};

class nsHtml5TreeOperationStringPair {
 private:
  nsString mPublicId;
  nsString mSystemId;

 public:
  nsHtml5TreeOperationStringPair(const nsAString& aPublicId,
                                 const nsAString& aSystemId)
      : mPublicId(aPublicId), mSystemId(aSystemId) {
    MOZ_COUNT_CTOR(nsHtml5TreeOperationStringPair);
  }

  ~nsHtml5TreeOperationStringPair() {
    MOZ_COUNT_DTOR(nsHtml5TreeOperationStringPair);
  }

  inline void Get(nsAString& aPublicId, nsAString& aSystemId) {
    aPublicId.Assign(mPublicId);
    aSystemId.Assign(mSystemId);
  }
};

struct opAppendDoctypeToDocument {
  nsAtom* mName;
  nsHtml5TreeOperationStringPair* mStringPair;

  explicit opAppendDoctypeToDocument(nsAtom* aName, const nsAString& aPublicId,
                                     const nsAString& aSystemId) {
    mName = aName;
    aName->AddRef();
    mStringPair = new nsHtml5TreeOperationStringPair(aPublicId, aSystemId);
  }
};

struct opGetDocumentFragmentForTemplate {
  nsIContent** mTemplate;
  nsIContent** mFragHandle;

  explicit opGetDocumentFragmentForTemplate(nsIContentHandle* aTemplate,
                                            nsIContentHandle* aFragHandle) {
    mTemplate = static_cast<nsIContent**>(aTemplate);
    mFragHandle = static_cast<nsIContent**>(aFragHandle);
  }
};

struct opSetDocumentFragmentForTemplate {
  nsIContent** mTemplate;
  nsIContent** mFragment;

  explicit opSetDocumentFragmentForTemplate(nsIContentHandle* aTemplate,
                                            nsIContentHandle* aFragment) {
    mTemplate = static_cast<nsIContent**>(aTemplate);
    mFragment = static_cast<nsIContent**>(aFragment);
  }
};

struct opGetShadowRootFromHost {
  nsIContent** mHost;
  nsIContent** mFragHandle;
  nsIContent** mTemplateNode;
  nsString mShadowRootReferenceTarget;
  mozilla::dom::ShadowRootMode mShadowRootMode;
  mozilla::dom::SlotAssignmentMode mShadowRootSlotAssignment;
  bool mShadowRootIsClonable;
  bool mShadowRootIsSerializable;
  bool mShadowRootDelegatesFocus;
  bool mShadowRootCustomElementRegistry;

  explicit opGetShadowRootFromHost(
      nsIContentHandle* aHost, nsIContentHandle* aFragHandle,
      nsIContentHandle* aTemplateNode,
      mozilla::dom::ShadowRootMode aShadowRootMode, bool aShadowRootIsClonable,
      bool aShadowRootIsSerializable, bool aShadowRootDelegatesFocus,
      bool aShadowRootCustomElementRegistry,
      mozilla::dom::SlotAssignmentMode aShadowRootSlotAssignment,
      nsAString& aShadowRootReferenceTarget) {
    mHost = static_cast<nsIContent**>(aHost);
    mFragHandle = static_cast<nsIContent**>(aFragHandle);
    mTemplateNode = static_cast<nsIContent**>(aTemplateNode);
    mShadowRootMode = aShadowRootMode;
    mShadowRootIsClonable = aShadowRootIsClonable;
    mShadowRootIsSerializable = aShadowRootIsSerializable;
    mShadowRootDelegatesFocus = aShadowRootDelegatesFocus;
    mShadowRootCustomElementRegistry = aShadowRootCustomElementRegistry;
    mShadowRootSlotAssignment = aShadowRootSlotAssignment;
    mShadowRootReferenceTarget = aShadowRootReferenceTarget;
  }
};

struct opGetFosterParent {
  nsIContent** mTable;
  nsIContent** mStackParent;
  nsIContent** mParentHandle;

  explicit opGetFosterParent(nsIContentHandle* aTable,
                             nsIContentHandle* aStackParent,
                             nsIContentHandle* aParentHandle) {
    mTable = static_cast<nsIContent**>(aTable);
    mStackParent = static_cast<nsIContent**>(aStackParent);
    mParentHandle = static_cast<nsIContent**>(aParentHandle);
  };
};

// Gecko-specific on-pop ops
struct opMarkAsBroken {
  nsresult mResult;

  explicit opMarkAsBroken(nsresult aResult) : mResult(aResult) {};
};

struct opRunScriptThatMayDocumentWriteOrBlock {
  nsIContent** mElement;
  nsAHtml5TreeBuilderState* mBuilderState;
  int32_t mLineNumber;

  explicit opRunScriptThatMayDocumentWriteOrBlock(
      nsIContentHandle* aElement, nsAHtml5TreeBuilderState* aBuilderState)
      : mBuilderState(aBuilderState), mLineNumber(0) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opRunScriptThatCannotDocumentWriteOrBlock {
  nsIContent** mElement;

  explicit opRunScriptThatCannotDocumentWriteOrBlock(
      nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opPreventScriptExecution {
  nsIContent** mElement;

  explicit opPreventScriptExecution(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opDoneAddingChildren {
  nsIContent** mElement;

  explicit opDoneAddingChildren(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opDoneCreatingElement {
  nsIContent** mElement;

  explicit opDoneCreatingElement(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opUpdateCharsetSource {
  nsCharsetSource mCharsetSource;

  explicit opUpdateCharsetSource(nsCharsetSource aCharsetSource)
      : mCharsetSource(aCharsetSource) {};
};

struct opCharsetSwitchTo {
  const mozilla::Encoding* mEncoding;
  int32_t mCharsetSource;
  int32_t mLineNumber;

  explicit opCharsetSwitchTo(const mozilla::Encoding* aEncoding,
                             int32_t aCharsetSource, int32_t aLineNumber)
      : mEncoding(aEncoding),
        mCharsetSource(aCharsetSource),
        mLineNumber(aLineNumber) {};
};

struct opUpdateStyleSheet {
  nsIContent** mElement;

  explicit opUpdateStyleSheet(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opProcessOfflineManifest {
  char16_t* mUrl;

  explicit opProcessOfflineManifest(char16_t* aUrl) : mUrl(aUrl) {};
};

struct opMarkMalformedIfScript {
  nsIContent** mElement;

  explicit opMarkMalformedIfScript(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  }
};

struct opStreamEnded {};

struct opSetStyleLineNumber {
  nsIContent** mContent;
  int32_t mLineNumber;

  explicit opSetStyleLineNumber(nsIContentHandle* aContent, int32_t aLineNumber)
      : mLineNumber(aLineNumber) {
    mContent = static_cast<nsIContent**>(aContent);
  };
};

struct opSetScriptLineAndColumnNumberAndFreeze {
  nsIContent** mContent;
  int32_t mLineNumber;
  int32_t mColumnNumber;

  explicit opSetScriptLineAndColumnNumberAndFreeze(nsIContentHandle* aContent,
                                                   int32_t aLineNumber,
                                                   int32_t aColumnNumber)
      : mLineNumber(aLineNumber), mColumnNumber(aColumnNumber) {
    mContent = static_cast<nsIContent**>(aContent);
  };
};

struct opSvgLoad {
  nsIContent** mElement;

  explicit opSvgLoad(nsIContentHandle* aElement) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opMaybeComplainAboutCharset {
  char* mMsgId;
  bool mError;
  int32_t mLineNumber;

  explicit opMaybeComplainAboutCharset(char* aMsgId, bool aError,
                                       int32_t aLineNumber)
      : mMsgId(aMsgId), mError(aError), mLineNumber(aLineNumber) {};
};

struct opMaybeComplainAboutDeepTree {
  int32_t mLineNumber;

  explicit opMaybeComplainAboutDeepTree(int32_t aLineNumber)
      : mLineNumber(aLineNumber) {};
};

struct opAddClass {
  nsIContent** mElement;
  char16_t* mClass;

  explicit opAddClass(nsIContentHandle* aElement, char16_t* aClass)
      : mClass(aClass) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opAddViewSourceHref {
  nsIContent** mElement;
  char16_t* mBuffer;
  int32_t mLength;

  explicit opAddViewSourceHref(nsIContentHandle* aElement, char16_t* aBuffer,
                               int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opAddViewSourceBase {
  char16_t* mBuffer;
  int32_t mLength;

  explicit opAddViewSourceBase(char16_t* aBuffer, int32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {};
};

struct opAddErrorType {
  nsIContent** mElement;
  char* mMsgId;
  nsAtom* mName;
  nsAtom* mOther;

  explicit opAddErrorType(nsIContentHandle* aElement, char* aMsgId,
                          nsAtom* aName = nullptr, nsAtom* aOther = nullptr)
      : mMsgId(aMsgId), mName(aName), mOther(aOther) {
    mElement = static_cast<nsIContent**>(aElement);
    if (aName) {
      aName->AddRef();
    }
    if (aOther) {
      aOther->AddRef();
    }
  };
};

struct opShallowCloneInto {
  nsIContent** mSrc;
  nsIContent** mDst;
  nsIContent** mIntendedParent;
  mozilla::dom::FromParser mFromParser;

  opShallowCloneInto(nsIContentHandle* aSrc, nsIContentHandle* aDst,
                     nsIContentHandle* aIntendedParent,
                     mozilla::dom::FromParser aFromParser)
      : mFromParser(aFromParser) {
    mSrc = static_cast<nsIContent**>(aSrc);
    mDst = static_cast<nsIContent**>(aDst);
    mIntendedParent = static_cast<nsIContent**>(aIntendedParent);
  };
};

struct opAddLineNumberId {
  nsIContent** mElement;
  int32_t mLineNumber;

  explicit opAddLineNumberId(nsIContentHandle* aElement, int32_t aLineNumber)
      : mLineNumber(aLineNumber) {
    mElement = static_cast<nsIContent**>(aElement);
  };
};

struct opStartLayout {};

struct opEnableEncodingMenu {};

struct opMicrotaskCheckpoint {};

typedef mozilla::Variant<
    uninitialized,
    // main HTML5 ops
    opAppend, opDetach, opAppendChildrenToNewParent, opFosterParent,
    opAppendToDocument, opAddAttributes, nsHtml5DocumentMode,
    opCreateHTMLElement, opCreateSVGElement, opCreateMathMLElement,
    opSetFormElement, opAppendText, opFosterParentText, opAppendComment,
    opAppendCommentToDocument, opAppendDoctypeToDocument,
    opAppendProcessingInstruction, opAppendProcessingInstructionToDocument,
    opGetDocumentFragmentForTemplate, opSetDocumentFragmentForTemplate,
    opGetShadowRootFromHost, opGetFosterParent,
    // Gecko-specific on-pop ops
    opMarkAsBroken, opRunScriptThatMayDocumentWriteOrBlock,
    opRunScriptThatCannotDocumentWriteOrBlock, opPreventScriptExecution,
    opDoneAddingChildren, opDoneCreatingElement, opUpdateCharsetSource,
    opCharsetSwitchTo, opUpdateStyleSheet, opProcessOfflineManifest,
    opMarkMalformedIfScript, opStreamEnded, opSetStyleLineNumber,
    opSetScriptLineAndColumnNumberAndFreeze, opSvgLoad,
    opMaybeComplainAboutCharset, opMaybeComplainAboutDeepTree, opAddClass,
    opAddViewSourceHref, opAddViewSourceBase, opAddErrorType, opAddLineNumberId,
    opStartLayout, opEnableEncodingMenu, opMicrotaskCheckpoint,
    opShallowCloneInto>
    treeOperation;

class nsHtml5TreeOperation final {
  template <typename T>
  using NotNull = mozilla::NotNull<T>;
  using Encoding = mozilla::Encoding;

 public:
  static nsresult AppendTextToTextNode(const char16_t* aBuffer,
                                       uint32_t aLength,
                                       mozilla::dom::Text* aTextNode,
                                       nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendText(const char16_t* aBuffer, uint32_t aLength,
                             nsIContent* aParent,
                             nsHtml5DocumentBuilder* aBuilder);

  static nsresult Append(nsIContent* aNode, nsIContent* aParent,
                         nsHtml5DocumentBuilder* aBuilder);

  static nsresult Append(nsIContent* aNode, nsIContent* aParent,
                         mozilla::dom::FromParser aFromParser,
                         nsHtml5DocumentBuilder* aBuilder);

  // The insertion variants below insert before aBefore, which has to be a
  // child of aParent, or append when aBefore is null. Unlike Append() above
  // they never run custom element reactions. Only sanitizing while parsing
  // uses them, and that is limited to the parses that nsHtml5StringParser
  // drives through the op-less builder, i.e. the fragment and document parses
  // behind setHTML() and Document.parseHTML(), which run no custom element
  // reactions in the first place.
  static nsresult InsertBefore(nsIContent* aNode, nsIContent* aParent,
                               nsIContent* aBefore,
                               nsHtml5DocumentBuilder* aBuilder);

  static nsresult InsertTextBefore(const char16_t* aBuffer, uint32_t aLength,
                                   nsIContent* aParent, nsIContent* aBefore,
                                   nsHtml5DocumentBuilder* aBuilder);

  static nsresult InsertCommentBefore(nsIContent* aParent, char16_t* aBuffer,
                                      int32_t aLength, nsIContent* aBefore,
                                      nsHtml5DocumentBuilder* aBuilder);

  // Undoes the form association that element creation gave aNode, for a node
  // that ends up not being inserted after all.
  static void AbortNodeInsertion(nsINode* aNode);

  // "If it is not possible to insert element at the adjusted insertion
  // location, abort these steps." Reports whether inserting aNode into
  // aParent is possible, without touching either of them.
  static bool CanInsert(nsIContent* aNode, nsIContent* aParent);

  static nsresult AppendToDocument(nsIContent* aNode,
                                   nsHtml5DocumentBuilder* aBuilder);

  static void Detach(nsIContent* aNode, nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendChildrenToNewParent(nsIContent* aNode,
                                            nsIContent* aParent,
                                            nsHtml5DocumentBuilder* aBuilder);

  static nsresult FosterParent(nsIContent* aNode, nsIContent* aParent,
                               nsIContent* aTable,
                               nsHtml5DocumentBuilder* aBuilder);

  static nsresult AddAttributes(nsIContent* aNode,
                                nsHtml5HtmlAttributes* aAttributes,
                                nsHtml5DocumentBuilder* aBuilder);

  static void SetHTMLElementAttributes(mozilla::dom::Element* aElement,
                                       nsHtml5HtmlAttributes* aAttributes,
                                       bool aNotify);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void SetHTMLElementAttributesFast(
      mozilla::dom::Element* aElement, nsHtml5HtmlAttributes* aAttributes);

  static nsIContent* CreateHTMLElement(
      nsAtom* aName, nsHtml5HtmlAttributes* aAttributes,
      mozilla::dom::FromParser aFromParser, nsNodeInfoManager* aNodeInfoManager,
      nsHtml5DocumentBuilder* aBuilder,
      mozilla::dom::HTMLContentCreatorFunction aCreator,
      nsINode* aIntendedParent,
      mozilla::Maybe<RefPtr<mozilla::dom::CustomElementRegistry>>
          aContextRegistry);

  static nsIContent* CreateSVGElement(
      nsAtom* aName, nsHtml5HtmlAttributes* aAttributes,
      mozilla::dom::FromParser aFromParser, nsNodeInfoManager* aNodeInfoManager,
      nsHtml5DocumentBuilder* aBuilder,
      mozilla::dom::SVGContentCreatorFunction aCreator);

  static nsIContent* CreateMathMLElement(nsAtom* aName,
                                         nsHtml5HtmlAttributes* aAttributes,
                                         nsNodeInfoManager* aNodeInfoManager,
                                         nsHtml5DocumentBuilder* aBuilder);

  static void SetFormElement(nsIContent* aNode, nsIContent* aForm,
                             nsIContent* aParent);

  static nsresult AppendIsindexPrompt(nsIContent* parent,
                                      nsHtml5DocumentBuilder* aBuilder);

  static nsresult FosterParentText(nsIContent* aStackParent, char16_t* aBuffer,
                                   uint32_t aLength, nsIContent* aTable,
                                   nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendComment(nsIContent* aParent, char16_t* aBuffer,
                                int32_t aLength,
                                nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendCommentToDocument(char16_t* aBuffer, int32_t aLength,
                                          nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendDoctypeToDocument(nsAtom* aName,
                                          const nsAString& aPublicId,
                                          const nsAString& aSystemId,
                                          nsHtml5DocumentBuilder* aBuilder);

  static nsIContent* GetDocumentFragmentForTemplate(nsIContent* aNode);
  static void SetDocumentFragmentForTemplate(nsIContent* aNode,
                                             nsIContent* aDocumentFragment);

  // The node that foster parented content is inserted into immediately before
  // aTable, or nullptr when there is none, in which case the content is
  // appended to the stack parent instead.
  static nsIContent* GetFosterParentForInsertBefore(nsIContent* aTable);

  static nsresult AppendProcessingInstruction(nsIContent* aParent,
                                              const nsAString& aTarget,
                                              const nsAString& aData,
                                              nsHtml5DocumentBuilder* aBuilder);

  static nsresult InsertProcessingInstructionBefore(
      nsIContent* aParent, const nsAString& aTarget, const nsAString& aData,
      nsIContent* aBefore, nsHtml5DocumentBuilder* aBuilder);

  static nsresult AppendProcessingInstructionToDocument(
      const nsAString& aTarget, const nsAString& aData,
      nsHtml5DocumentBuilder* aBuilder);

  static nsIContent* GetFosterParent(nsIContent* aTable,
                                     nsIContent* aStackParent);

  static void PreventScriptExecution(nsIContent* aNode);

  static void DoneAddingChildren(nsIContent* aNode);

  static void DoneCreatingElement(nsIContent* aNode);

  static void SvgLoad(nsIContent* aNode);

  static void MarkMalformedIfScript(nsIContent* aNode);

  nsHtml5TreeOperation();

  ~nsHtml5TreeOperation();

  nsHtml5TreeOperation(const nsHtml5TreeOperation&) = delete;
  nsHtml5TreeOperation& operator=(const nsHtml5TreeOperation&) = delete;

  inline void Init(const treeOperation& aOperation) {
    NS_ASSERTION(mOperation.is<uninitialized>(),
                 "Op code must be uninitialized when initializing.");
    mOperation = aOperation;
  }

  inline bool IsRunScriptThatMayDocumentWriteOrBlock() {
    return mOperation.is<opRunScriptThatMayDocumentWriteOrBlock>();
  }

  inline bool IsMarkAsBroken() { return mOperation.is<opMarkAsBroken>(); }

  inline void SetSnapshot(nsAHtml5TreeBuilderState* aSnapshot, int32_t aLine) {
    MOZ_ASSERT(
        IsRunScriptThatMayDocumentWriteOrBlock(),
        "Setting a snapshot for a tree operation other than eTreeOpRunScript!");
    MOZ_ASSERT(aSnapshot, "Initialized tree op with null snapshot.");
    opRunScriptThatMayDocumentWriteOrBlock data =
        mOperation.as<opRunScriptThatMayDocumentWriteOrBlock>();
    data.mBuilderState = aSnapshot;
    data.mLineNumber = aLine;
    mOperation = mozilla::AsVariant(data);
  }

  nsresult Perform(nsHtml5TreeOpExecutor* aBuilder, nsIContent** aScriptElement,
                   bool* aInterrupted, bool* aStreamEnded);

 private:
  treeOperation mOperation;
};

#endif  // nsHtml5TreeOperation_h
