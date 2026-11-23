/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXMLContentSink_h_
#define nsXMLContentSink_h_

#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "mozilla/Span.h"
#include "mozilla/dom/FromParser.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsContentSink.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIDocumentTransformer.h"
#include "nsIExpatSink.h"
#include "nsIXMLContentSink.h"
#include "nsTArray.h"

class nsIURI;
class nsIContent;
class nsIParser;
class nsTextNode;

namespace mozilla::dom {
class Element;
class NodeInfo;
class ProcessingInstruction;
}  // namespace mozilla::dom

enum XMLContentSinkState {
  eXMLContentSinkState_InProlog,
  eXMLContentSinkState_InDocumentElement,
  eXMLContentSinkState_InEpilog
};

class nsXMLContentSink : public nsContentSink,
                         public nsIXMLContentSink,
                         public nsITransformObserver,
                         public nsIExpatSink {
 public:
  nsXMLContentSink();

  nsresult Init(mozilla::dom::Document* aDoc, nsIURI* aURL,
                nsISupports* aContainer, nsIChannel* aChannel);

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsXMLContentSink, nsContentSink)

  NS_DECL_NSIEXPATSINK

  // nsIContentSink
  NS_IMETHOD WillParse(void) override;
  NS_IMETHOD WillBuildModel() override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD
  DidBuildModel(bool aTerminated) override;
  NS_IMETHOD WillInterrupt(void) override;
  void WillResume() override;
  NS_IMETHOD SetParser(nsParserBase* aParser) override;
  virtual void InitialTranslationCompleted() override;
  virtual void FlushPendingNotifications(mozilla::FlushType aType) override;
  virtual void SetDocumentCharset(NotNull<const Encoding*> aEncoding) override;
  virtual nsISupports* GetTarget() override;
  bool IsScriptExecuting() override { return IsScriptExecutingImpl(); }
  void ContinueParsingDocumentAfterCurrentScript() override {
    ContinueParsingDocumentAfterCurrentScriptImpl();
  }
  virtual void ContinueInterruptedParsingAsync() override;
  bool IsPrettyPrintXML() const override { return mPrettyPrintXML; }
  bool IsPrettyPrintHasSpecialRoot() const override {
    return mPrettyPrintHasSpecialRoot;
  }

  // nsITransformObserver
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  OnDocumentCreated(mozilla::dom::Document* aSourceDocument,
                    mozilla::dom::Document* aResultDocument) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  OnTransformDone(mozilla::dom::Document* aSourceDocument, nsresult aResult,
                  mozilla::dom::Document* aResultDocument) override;

  // nsICSSLoaderObserver
  NS_IMETHOD StyleSheetLoaded(mozilla::StyleSheet* aSheet, bool aWasDeferred,
                              nsresult aStatus) override;
  static bool ParsePIData(const nsString& aData, nsString& aHref,
                          nsString& aTitle, nsString& aMedia,
                          bool& aIsAlternate);

 protected:
  virtual ~nsXMLContentSink();

  nsIParser* GetParser();

  void ContinueInterruptedParsingIfEnabled();

  // Start layout.  If aIgnorePendingSheets is true, this will happen even if
  // we still have stylesheet loads pending.  Otherwise, we'll wait until the
  // stylesheets are all done loading.
  virtual void MaybeStartLayout(bool aIgnorePendingSheets);

  virtual nsresult AddAttributes(const char16_t** aNode,
                                 mozilla::dom::Element* aElement);
  nsresult AddText(mozilla::Span<const char16_t> aText);

  virtual bool OnOpenContainer(const char16_t** aAtts, uint32_t aAttsCount,
                               int32_t aNameSpaceID, nsAtom* aTagName,
                               uint32_t aLineNumber) {
    return true;
  }
  // Set the given content as the root element for the created document
  //  don't set if root element was already set.
  //  return TRUE if this call set the root element
  virtual bool SetDocElement(int32_t aNameSpaceID, nsAtom* aTagName,
                             nsIContent* aContent);
  virtual nsresult CreateElement(const char16_t** aAtts, uint32_t aAttsCount,
                                 mozilla::dom::NodeInfo* aNodeInfo,
                                 uint32_t aLineNumber, uint32_t aColumnNumber,
                                 nsIContent** aResult, bool* aAppendContent,
                                 mozilla::dom::FromParser aFromParser);

  // aParent is allowed to be null here if this is the root content
  // being closed
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual nsresult CloseElement(
      nsIContent* aContent);

  virtual nsresult FlushText(bool aReleaseTextNode = true);

  nsresult AddContentAsLeaf(nsIContent* aContent);

  nsIContent* GetCurrentContent();
  void PushContent(nsIContent* aContent);
  void PopContent();

  // nsContentSink override
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual nsresult ProcessStyleLinkFromHeader(
      const nsAString& aHref, bool aAlternate, const nsAString& aTitle,
      const nsAString& aIntegrity, const nsAString& aType,
      const nsAString& aMedia, const nsAString& aCrossOrigin,
      const nsAString& aReferrerPolicy,
      const nsAString& aFetchPriority) override;

  // Try to handle an XSLT style link from an xml-stylesheet XML PI.
  virtual nsresult MaybeProcessXSLTLink(
      mozilla::dom::ProcessingInstruction* aProcessingInstruction,
      const nsAString& aHref, bool aAlternate, const nsAString& aTitle,
      const nsAString& aType, const nsAString& aMedia,
      const nsAString& aReferrerPolicy);

  nsresult LoadXSLStyleSheet(nsIURI* aUrl);

  bool CanStillPrettyPrint();

  nsresult MaybePrettyPrint();

  nsresult HandleStartElement(const char16_t* aName, const char16_t** aAtts,
                              uint32_t aAttsCount, uint32_t aLineNumber,
                              uint32_t aColumnNumber,
                              mozilla::dom::FromParser aFromParser);
  nsresult HandleEndElement(const char16_t* aName, bool aInterruptable);
  nsresult HandleCharacterData(const char16_t* aData, uint32_t aLength,
                               bool aInterruptable);

  nsCOMPtr<nsIContent> mDocElement;
  nsCOMPtr<nsIContent> mCurrentHead;  // When set, we're in an XHTML <haed>

  XMLContentSinkState mState = eXMLContentSinkState_InProlog;

  RefPtr<nsTextNode> mLastTextNode;

  // True while FlushText runs. A DOM notification in FlushText can run script
  // that stops the parser, and then DidBuildModel calls FlushText again.
  bool mFlushingText = false;

  bool mPrettyPrintXML : 1 = true;
  bool mPrettyPrintHasSpecialRoot : 1 = false;
  bool mPrettyPrintHasFactoredElements : 1 = false;
  // True if we called PrettyPrint() and it
  // decided we should in fact prettyprint.
  bool mPrettyPrinting : 1 = false;
  // True to call prevent script execution in the fragment mode.
  bool mPreventScriptExecution : 1 = false;
  // XSLT is disabled
  bool mXSLTIsDisabled : 1 = false;

  nsTArray<nsCOMPtr<nsIContent>> mContentStack;

  nsCOMPtr<nsIDocumentTransformer> mXSLTProcessor;
  RefPtr<mozilla::dom::Document> mXSLTResultDocument;

  // Holds the children in the prolog until the root element is added, after
  // which they're inserted in the document. However, if we're doing an XSLT
  // transform this will actually hold all the children of the source document,
  // until the transform is finished. After the transform is finished we'll just
  // discard the children.
  nsTArray<nsCOMPtr<nsIContent>> mDocumentChildren;

  // Our currently accumulated text that we have not flushed to a textnode yet.
  AutoTArray<char16_t, 4096> mText;
};

#endif  // nsXMLContentSink_h_
