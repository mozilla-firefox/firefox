/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXMLContentSink.h"

#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "mozAutoDocUpdate.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/UseCounter.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/CDATASection.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/ProcessingInstruction.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/txMozillaXSLTProcessor.h"
#include "mozilla/intl/LocaleService.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsHtml5SVGLoadDispatcher.h"
#include "nsIChannel.h"
#include "nsIContent.h"
#include "nsIContentPolicy.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIParser.h"
#include "nsIScriptContext.h"
#include "nsIScriptElement.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsMimeTypes.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsNodeInfoManager.h"
#include "nsReadableUtils.h"
#include "nsRect.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"
#include "nsXMLPrettyPrinter.h"
#include "prtime.h"

using namespace mozilla;
using namespace mozilla::dom;

// XXX Open Issues:
// 1) what's not allowed - We need to figure out which HTML tags
//    (prefixed with a HTML namespace qualifier) are explicitly not
//    allowed (if any).

nsresult NS_NewXMLContentSink(nsIXMLContentSink** aResult, Document* aDoc,
                              nsIURI* aURI, nsISupports* aContainer,
                              nsIChannel* aChannel) {
  MOZ_ASSERT(nullptr != aResult, "null ptr");
  if (nullptr == aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  RefPtr<nsXMLContentSink> it = new nsXMLContentSink();

  nsresult rv = it->Init(aDoc, aURI, aContainer, aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  it.forget(aResult);
  return NS_OK;
}

nsXMLContentSink::nsXMLContentSink() = default;
nsXMLContentSink::~nsXMLContentSink() = default;

nsresult nsXMLContentSink::Init(Document* aDoc, nsIURI* aURI,
                                nsISupports* aContainer, nsIChannel* aChannel) {
  nsresult rv = nsContentSink::Init(aDoc, aURI, aContainer, aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  aDoc->AddObserver(this);
  mIsDocumentObserver = true;

  if (!mDocShell) {
    mPrettyPrintXML = false;
  }

  mState = eXMLContentSinkState_InProlog;
  mDocElement = nullptr;

  return NS_OK;
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    nsXMLContentSink::StackNode& aField, const char* aName,
    uint32_t aFlags = 0) {
  ImplCycleCollectionTraverse(aCallback, aField.mContent, aName, aFlags);
}

inline void ImplCycleCollectionUnlink(nsXMLContentSink::StackNode& aField) {
  ImplCycleCollectionUnlink(aField.mContent);
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXMLContentSink)
  NS_INTERFACE_MAP_ENTRY(nsIContentSink)
  NS_INTERFACE_MAP_ENTRY(nsIXMLContentSink)
  NS_INTERFACE_MAP_ENTRY(nsIExpatSink)
  NS_INTERFACE_MAP_ENTRY(nsITransformObserver)
NS_INTERFACE_MAP_END_INHERITING(nsContentSink)

NS_IMPL_ADDREF_INHERITED(nsXMLContentSink, nsContentSink)
NS_IMPL_RELEASE_INHERITED(nsXMLContentSink, nsContentSink)

NS_IMPL_CYCLE_COLLECTION_INHERITED(nsXMLContentSink, nsContentSink,
                                   mCurrentHead, mDocElement, mLastTextNode,
                                   mContentStack, mDocumentChildren)

// nsIContentSink
NS_IMETHODIMP
nsXMLContentSink::WillParse(void) { return WillParseImpl(); }

NS_IMETHODIMP
nsXMLContentSink::WillBuildModel() {
  WillBuildModelImpl();

  // Notify document that the load is beginning
  mDocument->BeginLoad();

  // Check for correct load-command for maybe prettyprinting
  if (mPrettyPrintXML) {
    nsAutoCString command;
    GetParser()->GetCommand(command);
    if (!command.EqualsLiteral("view")) {
      mPrettyPrintXML = false;
    }
  }

  return NS_OK;
}

bool nsXMLContentSink::CanStillPrettyPrint() {
  return mPrettyPrintXML &&
         (!mPrettyPrintHasFactoredElements || mPrettyPrintHasSpecialRoot);
}

nsresult nsXMLContentSink::MaybePrettyPrint() {
  if (!CanStillPrettyPrint()) {
    mPrettyPrintXML = false;

    return NS_OK;
  }

  {
    // Try to perform a microtask checkpoint; this avoids always breaking
    // pretty-printing if webextensions insert new content right after the
    // document loads.
    nsAutoMicroTask mt;
  }

  // stop observing in order to avoid crashing when replacing content
  mDocument->RemoveObserver(this);
  mIsDocumentObserver = false;

  // Reenable the CSSLoader so that the prettyprinting stylesheets can load
  mDocument->EnsureCSSLoader().SetEnabled(true);

  RefPtr<nsXMLPrettyPrinter> printer;
  nsresult rv = NS_NewXMLPrettyPrinter(getter_AddRefs(printer));
  NS_ENSURE_SUCCESS(rv, rv);

  bool isPrettyPrinting;
  rv = printer->PrettyPrint(mDocument, &isPrettyPrinting);
  NS_ENSURE_SUCCESS(rv, rv);

  mPrettyPrinting = isPrettyPrinting;
  return NS_OK;
}

static void CheckXSLTParamPI(ProcessingInstruction* aPi,
                             nsIDocumentTransformer* aProcessor,
                             nsINode* aSource) {
  nsAutoString target, data;
  aPi->GetTarget(target);

  // Check for namespace declarations
  if (target.EqualsLiteral("xslt-param-namespace")) {
    aPi->GetData(data);
    nsAutoString prefix, namespaceAttr;
    nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::prefix, prefix);
    if (!prefix.IsEmpty() && nsContentUtils::GetPseudoAttributeValue(
                                 data, nsGkAtoms::_namespace, namespaceAttr)) {
      aProcessor->AddXSLTParamNamespace(prefix, namespaceAttr);
    }
  }

  // Check for actual parameters
  else if (target.EqualsLiteral("xslt-param")) {
    aPi->GetData(data);
    nsAutoString name, namespaceAttr, select, value;
    nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::name, name);
    nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::_namespace,
                                            namespaceAttr);
    if (!nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::select,
                                                 select)) {
      select.SetIsVoid(true);
    }
    if (!nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::value,
                                                 value)) {
      value.SetIsVoid(true);
    }
    if (!name.IsEmpty()) {
      aProcessor->AddXSLTParam(name, namespaceAttr, select, value, aSource);
    }
  }
}

NS_IMETHODIMP
nsXMLContentSink::DidBuildModel(bool aTerminated) {
  if (!mParser) {
    // If mParser is null, this parse has already been terminated and must
    // not been terminated again. However, Document may still think that
    // the parse has not been terminated and call back into here in the case
    // where the XML parser has finished but the XSLT transform associated
    // with the document has not.
    return NS_OK;
  }

  FlushTags();

  DidBuildModelImpl(aTerminated);

  if (mXSLTProcessor) {
    // stop observing in order to avoid crashing when replacing content
    mDocument->RemoveObserver(this);
    mIsDocumentObserver = false;

    ErrorResult rv;
    RefPtr<DocumentFragment> source = mDocument->CreateDocumentFragment();
    for (nsIContent* child : mDocumentChildren) {
      // XPath data model doesn't have DocumentType nodes.
      if (child->NodeType() != nsINode::DOCUMENT_TYPE_NODE) {
        source->AppendChild(*child, rv);
        if (rv.Failed()) {
          return rv.StealNSResult();
        }
      }
    }

    // Check for xslt-param and xslt-param-namespace PIs
    for (nsIContent* child : mDocumentChildren) {
      if (auto pi = ProcessingInstruction::FromNode(child)) {
        CheckXSLTParamPI(pi, mXSLTProcessor, source);
      } else if (child->IsElement()) {
        // Only honor PIs in the prolog
        break;
      }
    }

    mXSLTProcessor->SetSourceContentModel(source);
    // Since the processor now holds a reference to us we drop our reference
    // to it to avoid owning cycles
    mXSLTProcessor = nullptr;
  } else {
    // Kick off layout for non-XSLT transformed documents.

    // Check if we want to prettyprint
    MaybePrettyPrint();

    bool startLayout = true;

    if (mPrettyPrinting) {
      NS_ASSERTION(!mPendingSheetCount, "Shouldn't have pending sheets here!");

      // We're pretty-printing now.  See whether we should wait up on
      // stylesheet loads
      css::Loader* cssLoader = mDocument->GetExistingCSSLoader();
      if (cssLoader && cssLoader->HasPendingLoads()) {
        cssLoader->AddObserver(this);
        // wait for those sheets to load
        startLayout = false;
      }
    }

    if (startLayout) {
      StartLayout(false);

      ScrollToRef();
    }

    mDocument->RemoveObserver(this);
    mIsDocumentObserver = false;

    RefPtr<Document> doc = mDocument;
    if (!mDeferredLayoutStart && doc->IsBeingUsedAsImage()) {
      // Eagerly layout image documents, so that layout-triggered loads have a
      // chance of blocking the load event, see bug 1901414.
      doc->FlushPendingNotifications(FlushType::Layout);
    }

    doc->EndLoad();

    DropParserAndPerfHint();
  }

  return NS_OK;
}

nsresult nsXMLContentSink::OnDocumentCreated(Document* aSourceDocument,
                                             Document* aResultDocument) {
  aResultDocument->SetDocWriteDisabled(true);

  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));
  // Make sure that we haven't loaded a new document into the documentviewer
  // after starting the XSLT transform.
  if (viewer && viewer->GetDocument() == aSourceDocument) {
    return viewer->SetDocumentInternal(aResultDocument, true);
  }
  return NS_OK;
}

nsresult nsXMLContentSink::OnTransformDone(Document* aSourceDocument,
                                           nsresult aResult,
                                           Document* aResultDocument) {
  MOZ_ASSERT(aResultDocument,
             "Don't notify about transform end without a document.");

  mDocumentChildren.Clear();

  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));

  RefPtr<Document> originalDocument = mDocument;
  bool blockingOnload = mIsBlockingOnload;

  auto IsXSLTError = [](nsresult aResult, nsIDocumentViewer* aViewer,
                        Document* aResultDocument) -> bool {
    return NS_FAILED(aResult) && aViewer->GetDocument() && aResultDocument &&
           aViewer->GetDocument()->GetPrincipal() ==
               aResultDocument->GetPrincipal() &&
           aResultDocument->GetDocumentElement() &&
           aResultDocument->GetDocumentElement()->NodeInfo()->Equals(
               nsGkAtoms::parsererror) &&
           aResultDocument->GetDocumentElement()->NodeInfo()->NamespaceEquals(
               nsDependentAtomString(nsGkAtoms::nsuri_parsererror));
  };

  // Make sure that we haven't loaded a new document into the documentviewer
  // after starting the XSLT transform.
  if (viewer && (viewer->GetDocument() == aSourceDocument ||
                 viewer->GetDocument() == aResultDocument ||
                 IsXSLTError(aResult, viewer, aResultDocument))) {
    if (NS_FAILED(aResult)) {
      // Transform failed.
      aResultDocument->SetMayStartLayout(false);
      // We have an error document.
      viewer->SetDocument(aResultDocument);
    }

    if (!mRunsToCompletion) {
      // This BlockOnload call corresponds to the UnblockOnload call in
      // nsContentSink::DropParserAndPerfHint.
      aResultDocument->BlockOnload();
      mIsBlockingOnload = true;
    }
    // Transform succeeded, or it failed and we have an error document to
    // display.
    mDocument = aResultDocument;
    aResultDocument->SetDocWriteDisabled(false);

    // Notify document observers that all the content has been stuck
    // into the document.
    // XXX do we need to notify for things like PIs?  Or just the
    // documentElement?
    nsIContent* rootElement = mDocument->GetRootElement();
    if (rootElement) {
      NS_ASSERTION(mDocument->ComputeIndexOf(rootElement).isSome(),
                   "rootElement not in doc?");
      mDocument->BeginUpdate();
      MutationObservers::NotifyContentInserted(mDocument, rootElement, {});
      mDocument->EndUpdate();
    }

    // Start the layout process
    StartLayout(false);

    ScrollToRef();
  }

  originalDocument->EndLoad();
  if (blockingOnload) {
    // This UnblockOnload call corresponds to the BlockOnload call in
    // nsContentSink::WillBuildModelImpl.
    originalDocument->UnblockOnload(true);
  }

  DropParserAndPerfHint();

  // By this point, the result document has been set in the content viewer.  But
  // the content viewer does not call Destroy on the original document, so we
  // won't end up reporting document use counters.  It's possible we should be
  // detaching the document from the window, but for now, we call
  // ReportDocumentUseCounters on the original document here, to avoid
  // assertions in ~Document about not having reported them.
  originalDocument->ReportDocumentUseCounters();

  return NS_OK;
}

NS_IMETHODIMP
nsXMLContentSink::StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                                   nsresult aStatus) {
  if (!mPrettyPrinting) {
    return nsContentSink::StyleSheetLoaded(aSheet, aWasDeferred, aStatus);
  }

  if (mDocument->GetExistingCSSLoader() &&
      !mDocument->GetExistingCSSLoader()->HasPendingLoads()) {
    mDocument->GetExistingCSSLoader()->RemoveObserver(this);
    StartLayout(false);
    ScrollToRef();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXMLContentSink::WillInterrupt(void) { return WillInterruptImpl(); }

void nsXMLContentSink::WillResume() { WillResumeImpl(); }

NS_IMETHODIMP
nsXMLContentSink::SetParser(nsParserBase* aParser) {
  MOZ_ASSERT(aParser, "Should have a parser here!");
  mParser = aParser;
  return NS_OK;
}

static bool FindIsAttrValue(const char16_t** aAtts, const char16_t** aResult) {
  RefPtr<nsAtom> prefix, localName;
  for (; *aAtts; aAtts += 2) {
    int32_t nameSpaceID;
    nsContentUtils::SplitExpatName(aAtts[0], getter_AddRefs(prefix),
                                   getter_AddRefs(localName), &nameSpaceID);
    if (nameSpaceID == kNameSpaceID_None && localName == nsGkAtoms::is) {
      *aResult = aAtts[1];

      return true;
    }
  }

  return false;
}

nsresult nsXMLContentSink::CreateElement(
    const char16_t** aAtts, uint32_t aAttsCount,
    mozilla::dom::NodeInfo* aNodeInfo, uint32_t aLineNumber,
    uint32_t aColumnNumber, nsIContent** aResult, bool* aAppendContent,
    FromParser aFromParser) {
  NS_ASSERTION(aNodeInfo, "can't create element without nodeinfo");

  *aResult = nullptr;
  *aAppendContent = true;
  nsresult rv = NS_OK;

  RefPtr<mozilla::dom::NodeInfo> ni = aNodeInfo;
  RefPtr<Element> element;

  // https://html.spec.whatwg.org/#create-an-element-for-the-token
  // Step 5: Let is be the value of the "is" attribute in the given token, if
  // such an attribute exists, or null otherwise.
  const char16_t* is = nullptr;
  RefPtr<nsAtom> isAtom;
  uint32_t namespaceID = ni->NamespaceID();
  bool isXHTMLOrXUL =
      namespaceID == kNameSpaceID_XHTML || namespaceID == kNameSpaceID_XUL;
  if (isXHTMLOrXUL && FindIsAttrValue(aAtts, &is)) {
    isAtom = NS_AtomizeMainThread(nsDependentString(is));
  }

  // Step 6: Let definition be the result of looking up a custom element
  // definition given document, given namespace, local name, and is.
  // Step 7: If definition is non-null and the parser was not created as part of
  // the HTML fragment parsing algorithm, then let will execute script be true.
  // Otherwise, let it be false.
  //
  // Note that the check that the parser was not created as part of the HTML
  // fragment parsing algorithm is done by the check for a non-null mDocument.
  CustomElementDefinition* customElementDefinition = nullptr;
  nsAtom* nameAtom = ni->NameAtom();
  if (mDocument && !mDocument->IsLoadedAsData() && isXHTMLOrXUL &&
      (isAtom || nsContentUtils::IsCustomElementName(nameAtom, namespaceID))) {
    nsAtom* typeAtom = is ? isAtom.get() : nameAtom;

    MOZ_ASSERT(nameAtom->Equals(ni->LocalName()));
    customElementDefinition = nsContentUtils::LookupCustomElementDefinition(
        mDocument, nameAtom, namespaceID, typeAtom);
  }

  if (customElementDefinition) {
    // Since we are possibly going to run a script for the custom element
    // constructor, we should first flush any remaining elements.
    FlushTags();
    {
      nsAutoMicroTask mt;
    }

    Maybe<AutoCEReaction> autoCEReaction;
    if (auto* docGroup = mDocument->GetDocGroup()) {
      autoCEReaction.emplace(docGroup->CustomElementReactionsStack(), nullptr);
    }
    rv = NS_NewElement(getter_AddRefs(element), ni.forget(), aFromParser,
                       isAtom, customElementDefinition);
  } else {
    rv = NS_NewElement(getter_AddRefs(element), ni.forget(), aFromParser,
                       isAtom);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  if (aNodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_XHTML) ||
      aNodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_SVG)) {
    if (nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(element)) {
      sele->SetScriptLineNumber(aLineNumber);
      sele->SetScriptColumnNumber(
          JS::ColumnNumberOneOrigin::fromZeroOrigin(aColumnNumber));
      sele->SetCreatorParser(GetParser());
    } else {
      MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
                 "Node didn't QI to script, but SVG wasn't disabled.");
    }
  }

  // XHTML needs some special attention
  if (aNodeInfo->NamespaceEquals(kNameSpaceID_XHTML)) {
    mPrettyPrintHasFactoredElements = true;
  } else {
    // If we care, find out if we just used a special factory.
    if (!mPrettyPrintHasFactoredElements && !mPrettyPrintHasSpecialRoot &&
        mPrettyPrintXML) {
      mPrettyPrintHasFactoredElements =
          nsNameSpaceManager::GetInstance()->HasElementCreator(
              aNodeInfo->NamespaceID());
    }

    if (!aNodeInfo->NamespaceEquals(kNameSpaceID_SVG)) {
      element.forget(aResult);
      return NS_OK;
    }
  }

  if (auto* linkStyle = LinkStyle::FromNode(*element)) {
    if (aFromParser) {
      linkStyle->DisableUpdates();
    }
    if (!aNodeInfo->Equals(nsGkAtoms::link, kNameSpaceID_XHTML)) {
      linkStyle->SetLineNumber(aFromParser ? aLineNumber : 0);
      linkStyle->SetColumnNumber(aFromParser ? aColumnNumber + 1 : 1);
    }
  }

  element.forget(aResult);
  return NS_OK;
}

nsresult nsXMLContentSink::CloseElement(nsIContent* aContent) {
  NS_ASSERTION(aContent, "missing element to close");

  mozilla::dom::NodeInfo* nodeInfo = aContent->NodeInfo();

  // Some HTML nodes need DoneAddingChildren() called to initialize
  // properly (eg form state restoration).
  if (nsIContent::RequiresDoneAddingChildren(nodeInfo->NamespaceID(),
                                             nodeInfo->NameAtom())) {
    aContent->DoneAddingChildren(HaveNotifiedForCurrentContent());
  }

  if (IsMonolithicContainer(nodeInfo)) {
    mInMonolithicContainer--;
  }

  if (!nodeInfo->NamespaceEquals(kNameSpaceID_XHTML) &&
      !nodeInfo->NamespaceEquals(kNameSpaceID_SVG)) {
    return NS_OK;
  }

  if (nodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_XHTML) ||
      nodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_SVG)) {
    nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(aContent);
    if (!sele) {
      MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
                 "Node didn't QI to script, but SVG wasn't disabled.");
      return NS_OK;
    }

    if (mPreventScriptExecution) {
      sele->PreventExecution();
      return NS_OK;
    }

    // Always check the clock in nsContentSink right after a script
    StopDeflecting();

    // Flush any previously parsed elements before executing a script, in
    // order to prevent a script that adds a mutation observer from observing
    // that script element being adding to the tree.
    FlushTags();

    // https://html.spec.whatwg.org/#parsing-xhtml-documents
    // When the element's end tag is subsequently parsed, the user agent must
    // perform a microtask checkpoint, and then prepare the script element.
    {
      nsAutoMicroTask mt;
    }

    // Now tell the script that it's ready to go. This may execute the script
    // or return true, or neither if the script doesn't need executing.
    bool block = sele->AttemptToExecute(GetParser());

    // If the parser got blocked, make sure to return the appropriate rv.
    // I'm not sure if this is actually needed or not.
    if (mParser && !mParser->IsParserEnabled()) {
      block = true;
    }
    return block ? NS_ERROR_HTMLPARSER_BLOCK : NS_OK;
  }

  if (auto* linkStyle = LinkStyle::FromNode(*aContent)) {
    auto updateOrError = linkStyle->EnableUpdatesAndUpdateStyleSheet(
        mRunsToCompletion ? nullptr : this);
    if (updateOrError.isErr()) {
      return updateOrError.unwrapErr();
    }
    if (updateOrError.unwrap().ShouldBlock() && !mRunsToCompletion) {
      ++mPendingSheetCount;
      if (mScriptLoader) {
        mScriptLoader->AddParserBlockingScriptExecutionBlocker();
      }
    }
  }
  return NS_OK;
}

nsresult nsXMLContentSink::AddContentAsLeaf(nsIContent* aContent) {
  nsresult result = NS_OK;

  if (mState == eXMLContentSinkState_InProlog) {
    NS_ASSERTION(mDocument, "Fragments have no prolog");
    mDocumentChildren.AppendElement(aContent);
  } else if (mState == eXMLContentSinkState_InEpilog) {
    NS_ASSERTION(mDocument, "Fragments have no epilog");
    if (mXSLTProcessor) {
      mDocumentChildren.AppendElement(aContent);
    } else {
      mDocument->AppendChildTo(aContent, false, IgnoreErrors());
    }
  } else {
    nsCOMPtr<nsIContent> parent = GetCurrentContent();

    if (parent) {
      ErrorResult rv;
      parent->AppendChildTo(aContent, false, rv);
      result = rv.StealNSResult();
    }
  }
  return result;
}

// Create an XML parser and an XSL content sink and start parsing
// the XSL stylesheet located at the given URI.
nsresult nsXMLContentSink::LoadXSLStyleSheet(nsIURI* aUrl) {
  nsCOMPtr<nsIDocumentTransformer> processor = new txMozillaXSLTProcessor();
  mDocument->SetUseCounter(eUseCounter_custom_XSLStylesheet);
  mDocument->WarnOnceAbout(DeprecatedOperations::eXSLTDeprecated);

  processor->SetTransformObserver(this);

  if (NS_SUCCEEDED(processor->LoadStyleSheet(aUrl, mDocument))) {
    mXSLTProcessor.swap(processor);
  }

  // Intentionally ignore errors here, we should continue loading the
  // XML document whether we're able to load the XSLT stylesheet or
  // not.

  return NS_OK;
}

nsresult nsXMLContentSink::ProcessStyleLinkFromHeader(
    const nsAString& aHref, bool aAlternate, const nsAString& aTitle,
    const nsAString& aIntegrity, const nsAString& aType,
    const nsAString& aMedia, const nsAString& aReferrerPolicy,
    const nsAString& aFetchPriority) {
  mPrettyPrintXML = false;

  nsAutoCString cmd;
  if (mParser) GetParser()->GetCommand(cmd);
  if (cmd.EqualsASCII(kLoadAsData))
    return NS_OK;  // Do not load stylesheets when loading as data

  bool wasXSLT;
  nsresult rv = MaybeProcessXSLTLink(nullptr, aHref, aAlternate, aType, aType,
                                     aMedia, aReferrerPolicy, &wasXSLT);
  NS_ENSURE_SUCCESS(rv, rv);
  if (wasXSLT) {
    // We're done here.
    return NS_OK;
  }

  // Otherwise fall through to nsContentSink to handle CSS Link headers.
  return nsContentSink::ProcessStyleLinkFromHeader(
      aHref, aAlternate, aTitle, aIntegrity, aType, aMedia, aReferrerPolicy,
      aFetchPriority);
}

nsresult nsXMLContentSink::MaybeProcessXSLTLink(
    ProcessingInstruction* aProcessingInstruction, const nsAString& aHref,
    bool aAlternate, const nsAString& aTitle, const nsAString& aType,
    const nsAString& aMedia, const nsAString& aReferrerPolicy, bool* aWasXSLT) {
  bool wasXSLT = StaticPrefs::dom_xslt_enabled() &&
                 (aType.LowerCaseEqualsLiteral(TEXT_XSL) ||
                  aType.LowerCaseEqualsLiteral(APPLICATION_XSLT_XML) ||
                  aType.LowerCaseEqualsLiteral(TEXT_XML) ||
                  aType.LowerCaseEqualsLiteral(APPLICATION_XML));

  if (aWasXSLT) {
    *aWasXSLT = wasXSLT;
  }

  if (!wasXSLT) {
    return NS_OK;
  }

  if (aAlternate) {
    // don't load alternate XSLT
    return NS_OK;
  }
  // LoadXSLStyleSheet needs a mDocShell.
  if (!mDocShell) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> url;
  nsresult rv = NS_NewURI(getter_AddRefs(url), aHref, nullptr,
                          mDocument->GetDocBaseURI());
  NS_ENSURE_SUCCESS(rv, rv);

  // Do security check
  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  rv = secMan->CheckLoadURIWithPrincipal(mDocument->NodePrincipal(), url,
                                         nsIScriptSecurityManager::ALLOW_CHROME,
                                         mDocument->InnerWindowID());
  NS_ENSURE_SUCCESS(rv, NS_OK);

  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = MOZ_TRY(
      net::LoadInfo::Create(mDocument->NodePrincipal(),  // loading principal
                            mDocument->NodePrincipal(),  // triggering principal
                            aProcessingInstruction,
                            nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
                            nsIContentPolicy::TYPE_XSLT));

  // Do content policy check
  int16_t decision = nsIContentPolicy::ACCEPT;
  rv = NS_CheckContentLoadPolicy(url, secCheckLoadInfo, &decision,
                                 nsContentUtils::GetContentPolicy());

  NS_ENSURE_SUCCESS(rv, rv);

  if (NS_CP_REJECTED(decision)) {
    return NS_OK;
  }

  return LoadXSLStyleSheet(url);
}

void nsXMLContentSink::SetDocumentCharset(NotNull<const Encoding*> aEncoding) {
  if (mDocument) {
    mDocument->SetDocumentCharacterSet(aEncoding);
  }
}

nsISupports* nsXMLContentSink::GetTarget() { return ToSupports(mDocument); }

nsresult nsXMLContentSink::FlushText(bool aReleaseTextNode) {
  nsresult rv = NS_OK;

  if (!mText.IsEmpty()) {
    if (mLastTextNode) {
      bool notify = HaveNotifiedForCurrentContent();
      // We could probably always increase mInNotification here since
      // if AppendText doesn't notify it shouldn't trigger evil code.
      // But just in case it does, we don't want to mask any notifications.
      if (notify) {
        ++mInNotification;
      }
      rv = mLastTextNode->AppendText(mText.Elements(), mText.Length(), notify);
      if (notify) {
        --mInNotification;
      }

      mText.ClearAndRetainStorage();
    } else {
      RefPtr<nsTextNode> textContent =
          new (mNodeInfoManager) nsTextNode(mNodeInfoManager);

      mLastTextNode = textContent;

      // Set the text in the text node
      textContent->SetText(mText.Elements(), mText.Length(), false);
      mText.ClearAndRetainStorage();

      // Add text to its parent
      rv = AddContentAsLeaf(textContent);
    }
  }

  if (aReleaseTextNode) {
    mLastTextNode = nullptr;
  }

  return rv;
}

nsIContent* nsXMLContentSink::GetCurrentContent() {
  if (mContentStack.Length() == 0) {
    return nullptr;
  }
  return GetCurrentStackNode()->mContent;
}

nsXMLContentSink::StackNode* nsXMLContentSink::GetCurrentStackNode() {
  int32_t count = mContentStack.Length();
  return count != 0 ? &mContentStack[count - 1] : nullptr;
}

nsresult nsXMLContentSink::PushContent(nsIContent* aContent) {
  MOZ_ASSERT(aContent, "Null content being pushed!");
  StackNode* sn = mContentStack.AppendElement();
  NS_ENSURE_TRUE(sn, NS_ERROR_OUT_OF_MEMORY);

  nsIContent* contentToPush = aContent;

  // When an XML parser would append a node to a template element, it
  // must instead append it to the template element's template contents.
  if (contentToPush->IsHTMLElement(nsGkAtoms::_template)) {
    HTMLTemplateElement* templateElement =
        static_cast<HTMLTemplateElement*>(contentToPush);
    contentToPush = templateElement->Content();
  }

  sn->mContent = contentToPush;
  sn->mNumFlushed = 0;
  return NS_OK;
}

void nsXMLContentSink::PopContent() {
  if (mContentStack.IsEmpty()) {
    NS_WARNING("Popping empty stack");
    return;
  }

  mContentStack.RemoveLastElement();
}

bool nsXMLContentSink::HaveNotifiedForCurrentContent() const {
  uint32_t stackLength = mContentStack.Length();
  if (stackLength) {
    const StackNode& stackNode = mContentStack[stackLength - 1];
    nsIContent* parent = stackNode.mContent;
    return stackNode.mNumFlushed == parent->GetChildCount();
  }
  return true;
}

void nsXMLContentSink::MaybeStartLayout(bool aIgnorePendingSheets) {
  // XXXbz if aIgnorePendingSheets is true, what should we do when
  // mXSLTProcessor or CanStillPrettyPrint()?
  if (mLayoutStarted || mXSLTProcessor || CanStillPrettyPrint()) {
    return;
  }
  StartLayout(aIgnorePendingSheets);
}

////////////////////////////////////////////////////////////////////////

bool nsXMLContentSink::SetDocElement(int32_t aNameSpaceID, nsAtom* aTagName,
                                     nsIContent* aContent) {
  if (mDocElement) return false;

  mDocElement = aContent;

  if (mXSLTProcessor) {
    mDocumentChildren.AppendElement(aContent);
    return true;
  }

  auto documentChildren = std::move(mDocumentChildren);
  MOZ_ASSERT(mDocumentChildren.IsEmpty());
  for (nsIContent* child : documentChildren) {
    auto* linkStyle = LinkStyle::FromNode(*child);
    if (linkStyle) {
      linkStyle->DisableUpdates();
    }
    mDocument->AppendChildTo(child, false, IgnoreErrors());
    if (linkStyle) {
      auto updateOrError = linkStyle->EnableUpdatesAndUpdateStyleSheet(
          mRunsToCompletion ? nullptr : this);
      if (updateOrError.isErr()) {
        continue;
      }
      auto update = updateOrError.unwrap();
      // Successfully started a stylesheet load
      if (update.ShouldBlock() && !mRunsToCompletion) {
        ++mPendingSheetCount;
        if (mScriptLoader) {
          mScriptLoader->AddParserBlockingScriptExecutionBlocker();
        }
      }
    }
  }

  // check for root elements that needs special handling for
  // prettyprinting
  if (aNameSpaceID == kNameSpaceID_XSLT &&
      (aTagName == nsGkAtoms::stylesheet || aTagName == nsGkAtoms::transform)) {
    mPrettyPrintHasSpecialRoot = true;
    if (mPrettyPrintXML) {
      // In this case, disable script execution, stylesheet
      // loading, and auto XLinks since we plan to prettyprint.
      if (dom::ScriptLoader* scriptLoader = mDocument->GetScriptLoader()) {
        scriptLoader->SetEnabled(false);
      }
      // Sadly, we need to create the CSSLoader to disable it so that
      // something else doesn't create it in an enabled state after
      // this point but before it is OK to re-enable.
      mDocument->EnsureCSSLoader().SetEnabled(false);
    }
  }

  IgnoredErrorResult rv;
  mDocument->AppendChildTo(mDocElement, NotifyForDocElement(), rv);
  if (rv.Failed()) {
    // If we return false here, the caller will bail out because it won't
    // find a parent content node to append to, which is fine.
    return false;
  }

  return true;
}

NS_IMETHODIMP
nsXMLContentSink::HandleStartElement(const char16_t* aName,
                                     const char16_t** aAtts,
                                     uint32_t aAttsCount, uint32_t aLineNumber,
                                     uint32_t aColumnNumber) {
  return HandleStartElement(aName, aAtts, aAttsCount, aLineNumber,
                            aColumnNumber, true);
}

nsresult nsXMLContentSink::HandleStartElement(
    const char16_t* aName, const char16_t** aAtts, uint32_t aAttsCount,
    uint32_t aLineNumber, uint32_t aColumnNumber, bool aInterruptable) {
  MOZ_RELEASE_ASSERT(aAttsCount % 2 == 0, "incorrect aAttsCount");
  // Adjust aAttsCount so it's the actual number of attributes
  aAttsCount /= 2;

  nsresult result = NS_OK;
  bool appendContent = true;
  nsCOMPtr<nsIContent> content;

  // XXX Hopefully the parser will flag this before we get
  // here. If we're in the epilog, there should be no
  // new elements
  MOZ_ASSERT(eXMLContentSinkState_InEpilog != mState);

  FlushText();
  DidAddContent();

  mState = eXMLContentSinkState_InDocumentElement;

  int32_t nameSpaceID;
  RefPtr<nsAtom> prefix, localName;
  nsContentUtils::SplitExpatName(aName, getter_AddRefs(prefix),
                                 getter_AddRefs(localName), &nameSpaceID);

  if (!OnOpenContainer(aAtts, aAttsCount, nameSpaceID, localName,
                       aLineNumber)) {
    return NS_OK;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfoManager->GetNodeInfo(localName, prefix, nameSpaceID,
                                           nsINode::ELEMENT_NODE);

  result = CreateElement(aAtts, aAttsCount, nodeInfo, aLineNumber,
                         aColumnNumber, getter_AddRefs(content), &appendContent,
                         FROM_PARSER_NETWORK);
  NS_ENSURE_SUCCESS(result, result);

  // Have to do this before we push the new content on the stack... and have to
  // do that before we set attributes, call BindToTree, etc.  Ideally we'd push
  // on the stack inside CreateElement (which is effectively what the HTML sink
  // does), but that's hard with all the subclass overrides going on.
  nsCOMPtr<nsIContent> parent = GetCurrentContent();

  result = PushContent(content);
  NS_ENSURE_SUCCESS(result, result);

  // Set the attributes on the new content element
  result = AddAttributes(aAtts, content->AsElement());

  if (NS_OK == result) {
    // Store the element
    if (!SetDocElement(nameSpaceID, localName, content) && appendContent) {
      NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);

      parent->AppendChildTo(content, false, IgnoreErrors());
    }
  }

  // Some HTML nodes need DoneCreatingElement() called to initialize
  // properly (eg form state restoration).
  if (nsIContent::RequiresDoneCreatingElement(nodeInfo->NamespaceID(),
                                              nodeInfo->NameAtom())) {
    content->DoneCreatingElement();
  }

  if (nodeInfo->NamespaceID() == kNameSpaceID_XHTML &&
      nodeInfo->NameAtom() == nsGkAtoms::head && !mCurrentHead) {
    mCurrentHead = content;
  }

  if (IsMonolithicContainer(nodeInfo)) {
    mInMonolithicContainer++;
  }

  if (!mXSLTProcessor) {
    if (content == mDocElement) {
      nsContentUtils::AddScriptRunner(
          MakeAndAddRef<nsDocElementCreatedNotificationRunner>(mDocument));

      if (aInterruptable && NS_SUCCEEDED(result) && mParser &&
          !mParser->IsParserEnabled()) {
        return NS_ERROR_HTMLPARSER_BLOCK;
      }
    } else if (!mCurrentHead) {
      // This isn't the root and we're not inside an XHTML <head>.
      // Might need to start layout
      MaybeStartLayout(false);
    }
  }

  return aInterruptable && NS_SUCCEEDED(result) ? DidProcessATokenImpl()
                                                : result;
}

NS_IMETHODIMP
nsXMLContentSink::HandleEndElement(const char16_t* aName) {
  return HandleEndElement(aName, true);
}

nsresult nsXMLContentSink::HandleEndElement(const char16_t* aName,
                                            bool aInterruptable) {
  nsresult result = NS_OK;

  // XXX Hopefully the parser will flag this before we get
  // here. If we're in the prolog or epilog, there should be
  // no close tags for elements.
  MOZ_ASSERT(eXMLContentSinkState_InDocumentElement == mState);

  FlushText();

  StackNode* sn = GetCurrentStackNode();
  if (!sn) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIContent> content;
  sn->mContent.swap(content);
  uint32_t numFlushed = sn->mNumFlushed;

  PopContent();
  NS_ASSERTION(content, "failed to pop content");
#ifdef DEBUG
  // Check that we're closing the right thing
  RefPtr<nsAtom> debugNameSpacePrefix, debugTagAtom;
  int32_t debugNameSpaceID;
  nsContentUtils::SplitExpatName(aName, getter_AddRefs(debugNameSpacePrefix),
                                 getter_AddRefs(debugTagAtom),
                                 &debugNameSpaceID);
  // Check if we are closing a template element because template
  // elements do not get pushed on the stack, the template
  // element content is pushed instead.
  bool isTemplateElement = debugTagAtom == nsGkAtoms::_template &&
                           debugNameSpaceID == kNameSpaceID_XHTML;
  NS_ASSERTION(
      content->NodeInfo()->Equals(debugTagAtom, debugNameSpaceID) ||
          (debugNameSpaceID == kNameSpaceID_MathML &&
           content->NodeInfo()->NamespaceID() == kNameSpaceID_disabled_MathML &&
           content->NodeInfo()->Equals(debugTagAtom)) ||
          (debugNameSpaceID == kNameSpaceID_SVG &&
           content->NodeInfo()->NamespaceID() == kNameSpaceID_disabled_SVG &&
           content->NodeInfo()->Equals(debugTagAtom)) ||
          isTemplateElement,
      "Wrong element being closed");
#endif

  // Make sure to notify on our kids before we call out to any other code that
  // might reenter us and call FlushTags, in a state in which we've already
  // popped "content" from the stack but haven't notified on its kids yet.
  int32_t stackLen = mContentStack.Length();
  if (mNotifyLevel >= stackLen) {
    if (numFlushed < content->GetChildCount()) {
      NotifyAppend(content, numFlushed);
    }
    mNotifyLevel = stackLen - 1;
  }

  result = CloseElement(content);

  if (mCurrentHead == content) {
    mCurrentHead = nullptr;
  }

  if (mDocElement == content) {
    // XXXbz for roots that don't want to be appended on open, we
    // probably need to deal here.... (and stop appending them on open).
    mState = eXMLContentSinkState_InEpilog;

    mDocument->OnParsingCompleted();

    // We might have had no occasion to start layout yet.  Do so now.
    MaybeStartLayout(false);
  }

  DidAddContent();

  if (content->IsSVGElement(nsGkAtoms::svg)) {
    FlushTags();
    nsCOMPtr<nsIRunnable> event = new nsHtml5SVGLoadDispatcher(content);
    if (NS_FAILED(content->OwnerDoc()->Dispatch(event.forget()))) {
      NS_WARNING("failed to dispatch svg load dispatcher");
    }
  }

  return aInterruptable && NS_SUCCEEDED(result) ? DidProcessATokenImpl()
                                                : result;
}

NS_IMETHODIMP
nsXMLContentSink::HandleComment(const char16_t* aName) {
  FlushText();

  RefPtr<Comment> comment = new (mNodeInfoManager) Comment(mNodeInfoManager);
  comment->SetText(nsDependentString(aName), false);
  nsresult rv = AddContentAsLeaf(comment);
  DidAddContent();

  return NS_SUCCEEDED(rv) ? DidProcessATokenImpl() : rv;
}

NS_IMETHODIMP
nsXMLContentSink::HandleCDataSection(const char16_t* aData, uint32_t aLength) {
  // XSLT doesn't differentiate between text and cdata and wants adjacent
  // textnodes merged, so add as text.
  if (mXSLTProcessor) {
    return AddText(Span(aData, aLength));
  }

  FlushText();

  RefPtr<CDATASection> cdata =
      new (mNodeInfoManager) CDATASection(mNodeInfoManager);
  cdata->SetText(aData, aLength, false);
  nsresult rv = AddContentAsLeaf(cdata);
  DidAddContent();

  return NS_SUCCEEDED(rv) ? DidProcessATokenImpl() : rv;
}

NS_IMETHODIMP
nsXMLContentSink::HandleDoctypeDecl(const nsAString& aSubset,
                                    const nsAString& aName,
                                    const nsAString& aSystemId,
                                    const nsAString& aPublicId,
                                    nsISupports* aCatalogData) {
  FlushText();

  NS_ASSERTION(mDocument, "Shouldn't get here from a document fragment");

  RefPtr<nsAtom> name = NS_Atomize(aName);
  NS_ENSURE_TRUE(name, NS_ERROR_OUT_OF_MEMORY);

  // Create a new doctype node
  RefPtr<DocumentType> docType = NS_NewDOMDocumentType(
      mNodeInfoManager, name, aPublicId, aSystemId, aSubset);

  MOZ_ASSERT(!aCatalogData,
             "Need to add back support for catalog style "
             "sheets");

  mDocumentChildren.AppendElement(docType);
  DidAddContent();
  return DidProcessATokenImpl();
}

NS_IMETHODIMP
nsXMLContentSink::HandleCharacterData(const char16_t* aData, uint32_t aLength) {
  return HandleCharacterData(aData, aLength, true);
}

nsresult nsXMLContentSink::HandleCharacterData(const char16_t* aData,
                                               uint32_t aLength,
                                               bool aInterruptable) {
  nsresult rv = NS_OK;
  if (aData && mState != eXMLContentSinkState_InProlog &&
      mState != eXMLContentSinkState_InEpilog) {
    rv = AddText(Span(aData, aLength));
  }
  return aInterruptable && NS_SUCCEEDED(rv) ? DidProcessATokenImpl() : rv;
}

NS_IMETHODIMP
nsXMLContentSink::HandleProcessingInstruction(const char16_t* aTarget,
                                              const char16_t* aData) {
  FlushText();

  const nsDependentString target(aTarget);
  const nsDependentString data(aData);

  RefPtr<ProcessingInstruction> node =
      NS_NewXMLProcessingInstruction(mNodeInfoManager, target, data);

  if (LinkStyle::FromNode(*node)) {
    // TODO(emilio): can we move this check to SetDocElement?
    mPrettyPrintXML = false;
  }

  nsresult rv = AddContentAsLeaf(node);
  NS_ENSURE_SUCCESS(rv, rv);
  DidAddContent();

  // Handles the special chrome-only <?csp ?> PI, which will be handled before
  // creating any element with potential inline style or scripts.
  if (mState == eXMLContentSinkState_InProlog && target.EqualsLiteral("csp") &&
      mDocument->NodePrincipal()->IsSystemPrincipal()) {
    CSP_ApplyMetaCSPToDoc(*mDocument, data);
  }

  // Check whether this is a CSS stylesheet PI.  Make sure the type
  // handling here matches
  // XMLStylesheetProcessingInstruction::GetStyleSheetInfo.
  nsAutoString type;
  nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::type, type);
  nsAutoString mimeType, notUsed;
  nsContentUtils::SplitMimeType(type, mimeType, notUsed);

  if (mState != eXMLContentSinkState_InProlog ||
      !target.EqualsLiteral("xml-stylesheet") || mimeType.IsEmpty() ||
      mimeType.LowerCaseEqualsLiteral("text/css")) {
    // Either not a useful stylesheet PI, or a regular CSS stylesheet PI that
    // will get handled when appending mDocumentChildren.
    return DidProcessATokenImpl();
  }

  // If it's not a CSS stylesheet PI...
  nsAutoString href, title, media;
  bool isAlternate = false;

  // If there was no href, we can't do anything with this PI
  if (!ParsePIData(data, href, title, media, isAlternate)) {
    return DidProcessATokenImpl();
  }

  // <?xml-stylesheet?> processing instructions don't have a referrerpolicy
  // pseudo-attribute, so we pass in an empty string
  rv =
      MaybeProcessXSLTLink(node, href, isAlternate, title, type, media, u""_ns);
  return NS_SUCCEEDED(rv) ? DidProcessATokenImpl() : rv;
}

/* static */
bool nsXMLContentSink::ParsePIData(const nsString& aData, nsString& aHref,
                                   nsString& aTitle, nsString& aMedia,
                                   bool& aIsAlternate) {
  // If there was no href, we can't do anything with this PI
  if (!nsContentUtils::GetPseudoAttributeValue(aData, nsGkAtoms::href, aHref)) {
    return false;
  }

  nsContentUtils::GetPseudoAttributeValue(aData, nsGkAtoms::title, aTitle);

  nsContentUtils::GetPseudoAttributeValue(aData, nsGkAtoms::media, aMedia);

  nsAutoString alternate;
  nsContentUtils::GetPseudoAttributeValue(aData, nsGkAtoms::alternate,
                                          alternate);

  aIsAlternate = alternate.EqualsLiteral("yes");

  return true;
}

NS_IMETHODIMP
nsXMLContentSink::HandleXMLDeclaration(const char16_t* aVersion,
                                       const char16_t* aEncoding,
                                       int32_t aStandalone) {
  mDocument->SetXMLDeclaration(aVersion, aEncoding, aStandalone);

  return DidProcessATokenImpl();
}

NS_IMETHODIMP
nsXMLContentSink::ReportError(const char16_t* aErrorText,
                              const char16_t* aSourceText,
                              nsIScriptError* aError, bool* _retval) {
  MOZ_ASSERT(aError && aSourceText && aErrorText, "Check arguments!!!");
  nsresult rv = NS_OK;

  // The expat driver should report the error.  We're just cleaning up the mess.
  *_retval = true;

  mPrettyPrintXML = false;

  mState = eXMLContentSinkState_InProlog;

  // XXX need to stop scripts here -- hsivonen

  // stop observing in order to avoid crashing when removing content
  mDocument->RemoveObserver(this);
  mIsDocumentObserver = false;

  // Clear the current content
  mDocumentChildren.Clear();
  while (mDocument->GetLastChild()) {
    mDocument->GetLastChild()->Remove();
  }
  mDocElement = nullptr;

  // Clear any buffered-up text we have.
  mText.ClearAndRetainStorage();

  if (mXSLTProcessor) {
    // Get rid of the XSLT processor.
    mXSLTProcessor->CancelLoads();
    mXSLTProcessor = nullptr;
  }

  // release the nodes on stack
  mContentStack.Clear();
  mNotifyLevel = 0;

  // return leaving the document empty if we're asked to not add a <parsererror>
  // root node
  if (mDocument->SuppressParserErrorElement()) {
    return NS_OK;
  }

  // prepare to set <parsererror> as the document root

  constexpr auto errorNs =
      u"http://www.mozilla.org/newlayout/xml/parsererror.xml"_ns;

  nsAutoString parsererror(errorNs);
  parsererror.Append((char16_t)0xFFFF);
  parsererror.AppendLiteral("parsererror");

  const char16_t* dirAttr[] = {u"dir", u"ltr", 0, 0};
  if (intl::LocaleService::GetInstance()->IsAppLocaleRTL() &&
      !mDocument->ShouldResistFingerprinting(RFPTarget::JSLocale)) {
    dirAttr[1] = u"rtl";
  }
  rv = HandleStartElement(parsererror.get(), dirAttr, 0, 2, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = HandleCharacterData(aErrorText, NS_strlen(aErrorText), false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString sourcetext(errorNs);
  sourcetext.Append((char16_t)0xFFFF);
  sourcetext.AppendLiteral("sourcetext");

  const char16_t* noAtts[] = {0, 0};
  rv = HandleStartElement(sourcetext.get(), noAtts, 0, (uint32_t)-1, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = HandleCharacterData(aSourceText, NS_strlen(aSourceText), false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = HandleEndElement(sourcetext.get(), false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = HandleEndElement(parsererror.get(), false);
  NS_ENSURE_SUCCESS(rv, rv);

  FlushTags();

  return NS_OK;
}

nsresult nsXMLContentSink::AddAttributes(const char16_t** aAtts,
                                         Element* aContent) {
  // Add tag attributes to the content attributes
  RefPtr<nsAtom> prefix, localName;
  while (*aAtts) {
    int32_t nameSpaceID;
    nsContentUtils::SplitExpatName(aAtts[0], getter_AddRefs(prefix),
                                   getter_AddRefs(localName), &nameSpaceID);

    // Add attribute to content
    aContent->SetAttr(nameSpaceID, localName, prefix,
                      nsDependentString(aAtts[1]), false);
    aAtts += 2;
  }

  return NS_OK;
}

nsresult nsXMLContentSink::AddText(mozilla::Span<const char16_t> aNewText) {
  // Copy data from string into our buffer; flush buffer when it fills up.
  while (!aNewText.IsEmpty()) {
    size_t spaceRemaining = mText.Capacity() - mText.Length();
    if (spaceRemaining == 0) {
      nsresult rv = FlushText(false);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      MOZ_ASSERT(mText.IsEmpty());
      spaceRemaining = mText.Capacity();
    }

    size_t numCharsToCopy = std::min(spaceRemaining, aNewText.Length());
    const auto [newText1, newText2] = aNewText.SplitAt(numCharsToCopy);
    mText.AppendElements(newText1);
    aNewText = newText2;
  }

  return NS_OK;
}

void nsXMLContentSink::InitialTranslationCompleted() { StartLayout(false); }

void nsXMLContentSink::FlushPendingNotifications(FlushType aType) {
  // Only flush tags if we're not doing the notification ourselves
  // (since we aren't reentrant)
  if (!mInNotification) {
    if (mIsDocumentObserver) {
      // Only flush if we're still a document observer (so that our child
      // counts should be correct).
      if (aType >= FlushType::ContentAndNotify) {
        FlushTags();
      } else {
        FlushText(false);
      }
    }
    if (aType >= FlushType::EnsurePresShellInitAndFrames) {
      // Make sure that layout has started so that the reflow flush
      // will actually happen.
      MaybeStartLayout(true);
    }
  }
}

/**
 * NOTE!! Forked from SinkContext. Please keep in sync.
 *
 * Flush all elements that have been seen so far such that
 * they are visible in the tree. Specifically, make sure
 * that they are all added to their respective parents.
 * Also, do notification at the top for all content that
 * has been newly added so that the frame tree is complete.
 */
nsresult nsXMLContentSink::FlushTags() {
  mDeferredFlushTags = false;
  uint32_t oldUpdates = mUpdatesInNotification;

  mUpdatesInNotification = 0;
  ++mInNotification;
  {
    // Scope so we call EndUpdate before we decrease mInNotification
    mozAutoDocUpdate updateBatch(mDocument, true);

    // Don't release last text node in case we need to add to it again
    FlushText(false);

    // Start from the base of the stack (growing downward) and do
    // a notification from the node that is closest to the root of
    // tree for any content that has been added.

    int32_t stackPos;
    int32_t stackLen = mContentStack.Length();
    bool flushed = false;
    uint32_t childCount;
    nsIContent* content;

    for (stackPos = 0; stackPos < stackLen; ++stackPos) {
      content = mContentStack[stackPos].mContent;
      childCount = content->GetChildCount();

      if (!flushed && (mContentStack[stackPos].mNumFlushed < childCount)) {
        NotifyAppend(content, mContentStack[stackPos].mNumFlushed);
        flushed = true;
      }

      mContentStack[stackPos].mNumFlushed = childCount;
    }
    mNotifyLevel = stackLen - 1;
  }
  --mInNotification;

  if (mUpdatesInNotification > 1) {
    UpdateChildCounts();
  }

  mUpdatesInNotification = oldUpdates;
  return NS_OK;
}

/**
 * NOTE!! Forked from SinkContext. Please keep in sync.
 */
void nsXMLContentSink::UpdateChildCounts() {
  // Start from the top of the stack (growing upwards) and see if any
  // new content has been appended. If so, we recognize that reflows
  // have been generated for it and we should make sure that no
  // further reflows occur.  Note that we have to include stackPos == 0
  // to properly notify on kids of <html>.
  int32_t stackLen = mContentStack.Length();
  int32_t stackPos = stackLen - 1;
  while (stackPos >= 0) {
    StackNode& node = mContentStack[stackPos];
    node.mNumFlushed = node.mContent->GetChildCount();

    stackPos--;
  }
  mNotifyLevel = stackLen - 1;
}

bool nsXMLContentSink::IsMonolithicContainer(
    mozilla::dom::NodeInfo* aNodeInfo) {
  return ((aNodeInfo->NamespaceID() == kNameSpaceID_XHTML &&
           (aNodeInfo->NameAtom() == nsGkAtoms::tr ||
            aNodeInfo->NameAtom() == nsGkAtoms::select ||
            aNodeInfo->NameAtom() == nsGkAtoms::object)) ||
          (aNodeInfo->NamespaceID() == kNameSpaceID_MathML &&
           (aNodeInfo->NameAtom() == nsGkAtoms::math)));
}

void nsXMLContentSink::ContinueInterruptedParsingIfEnabled() {
  if (mParser && mParser->IsParserEnabled()) {
    GetParser()->ContinueInterruptedParsing();
  }
}

void nsXMLContentSink::ContinueInterruptedParsingAsync() {
  nsCOMPtr<nsIRunnable> ev = NewRunnableMethod(
      "nsXMLContentSink::ContinueInterruptedParsingIfEnabled", this,
      &nsXMLContentSink::ContinueInterruptedParsingIfEnabled);
  mDocument->Dispatch(ev.forget());
}

nsIParser* nsXMLContentSink::GetParser() {
  return static_cast<nsIParser*>(mParser.get());
}
