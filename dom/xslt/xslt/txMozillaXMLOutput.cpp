/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "txMozillaXMLOutput.h"

#include <algorithm>

#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/Encoding.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/Try.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/PermissionsPolicy.h"
#include "mozilla/dom/ProcessingInstruction.h"
#include "mozilla/dom/ScriptLoader.h"
#include "nsCharsetSource.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentSink.h"
#include "nsContentUtils.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "nsIDocumentTransformer.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsIRefreshURI.h"
#include "nsIScriptElement.h"
#include "nsNameSpaceManager.h"
#include "nsPIDOMWindow.h"
#include "nsStringFlags.h"
#include "nsStyleUtil.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"
#include "txLog.h"
#include "txStringUtils.h"
#include "txURIUtils.h"
#include "txXMLUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

#define TX_ENSURE_CURRENTNODE                            \
  NS_ASSERTION(mCurrentNode, "mCurrentNode is nullptr"); \
  if (!mCurrentNode) return NS_ERROR_UNEXPECTED

txMozillaXMLOutput::txMozillaXMLOutput(Document* aSourceDocument,
                                       txOutputFormat* aFormat,
                                       nsITransformObserver* aObserver)
    : mTreeDepth(0),
      mBadChildLevel(0),
      mTableState(NORMAL),
      mCreatingNewDocument(true),
      mOpenedElementIsHTML(false),
      mRootContentCreated(false),
      mNoFixup(false) {
  MOZ_COUNT_CTOR(txMozillaXMLOutput);
  if (aObserver) {
    mNotifier = new txTransformNotifier(aSourceDocument);
    if (mNotifier) {
      mNotifier->Init(aObserver);
    }
  }

  mOutputFormat.merge(*aFormat);
  mOutputFormat.setFromDefaults();
}

txMozillaXMLOutput::txMozillaXMLOutput(txOutputFormat* aFormat,
                                       DocumentFragment* aFragment,
                                       bool aNoFixup)
    : mTreeDepth(0),
      mBadChildLevel(0),
      mTableState(NORMAL),
      mCreatingNewDocument(false),
      mOpenedElementIsHTML(false),
      mRootContentCreated(false),
      mNoFixup(aNoFixup) {
  MOZ_COUNT_CTOR(txMozillaXMLOutput);
  mOutputFormat.merge(*aFormat);
  mOutputFormat.setFromDefaults();

  mCurrentNode = aFragment;
  mDocument = mCurrentNode->OwnerDoc();
  mNodeInfoManager = mDocument->NodeInfoManager();
}

txMozillaXMLOutput::~txMozillaXMLOutput() {
  MOZ_COUNT_DTOR(txMozillaXMLOutput);
}

nsresult txMozillaXMLOutput::attribute(nsAtom* aPrefix, nsAtom* aLocalName,
                                       nsAtom* aLowercaseLocalName,
                                       const int32_t aNsID,
                                       const nsString& aValue) {
  RefPtr<nsAtom> owner;
  if (mOpenedElementIsHTML && aNsID == kNameSpaceID_None) {
    if (aLowercaseLocalName) {
      aLocalName = aLowercaseLocalName;
    } else {
      owner = TX_ToLowerCaseAtom(aLocalName);
      NS_ENSURE_TRUE(owner, NS_ERROR_OUT_OF_MEMORY);

      aLocalName = owner;
    }
  }

  return attributeInternal(aPrefix, aLocalName, aNsID, aValue);
}

nsresult txMozillaXMLOutput::attribute(nsAtom* aPrefix,
                                       const nsAString& aLocalName,
                                       const int32_t aNsID,
                                       const nsString& aValue) {
  RefPtr<nsAtom> lname;

  if (mOpenedElementIsHTML && aNsID == kNameSpaceID_None) {
    nsAutoString lnameStr;
    nsContentUtils::ASCIIToLower(aLocalName, lnameStr);
    lname = NS_Atomize(lnameStr);
  } else {
    lname = NS_Atomize(aLocalName);
  }

  NS_ENSURE_TRUE(lname, NS_ERROR_OUT_OF_MEMORY);

  // Check that it's a valid name
  if (!nsContentUtils::IsValidNodeName(lname, aPrefix, aNsID)) {
    // Try without prefix
    aPrefix = nullptr;
    if (!nsContentUtils::IsValidNodeName(lname, aPrefix, aNsID)) {
      // Don't return error here since the callers don't deal
      return NS_OK;
    }
  }

  return attributeInternal(aPrefix, lname, aNsID, aValue);
}

nsresult txMozillaXMLOutput::attributeInternal(nsAtom* aPrefix,
                                               nsAtom* aLocalName,
                                               int32_t aNsID,
                                               const nsString& aValue) {
  if (!mOpenedElement) {
    // XXX Signal this? (can't add attributes after element closed)
    return NS_OK;
  }

  NS_ASSERTION(!mBadChildLevel, "mBadChildLevel set when element is opened");

  return mOpenedElement->SetAttr(aNsID, aLocalName, aPrefix, aValue, true,
                                 IsKnownNewAttr::No);
}

nsresult txMozillaXMLOutput::characters(const nsAString& aData, bool aDOE) {
  nsresult rv = closePrevious(false);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mBadChildLevel) {
    mText.Append(aData);
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::comment(const nsString& aData) {
  nsresult rv = closePrevious(true);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mBadChildLevel) {
    return NS_OK;
  }

  TX_ENSURE_CURRENTNODE;

  RefPtr<Comment> comment = new (mNodeInfoManager) Comment(mNodeInfoManager);

  rv = comment->SetText(aData, false);
  NS_ENSURE_SUCCESS(rv, rv);

  ErrorResult error;
  mCurrentNode->AppendChildTo(comment, true, error);
  return error.StealNSResult();
}

nsresult txMozillaXMLOutput::endDocument(nsresult aResult)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  TX_ENSURE_CURRENTNODE;

  nsresult rv = NS_OK;
  if (NS_SUCCEEDED(aResult)) {
    rv = closePrevious(true);
  }

  if (mCreatingNewDocument) {
    // This should really be handled by Document::EndLoad
    MOZ_ASSERT(mDocument->GetReadyStateEnum() == Document::READYSTATE_LOADING,
               "Bad readyState");
    mDocument->SetReadyStateInternal(Document::READYSTATE_INTERACTIVE);
    if (const RefPtr<ScriptLoader> loader = mDocument->GetScriptLoader()) {
      loader->ParsingComplete(false);
    }
  }

  if (mNotifier) {
    mNotifier->OnTransformEnd(NS_FAILED(aResult) ? aResult : rv);
  }

  return rv;
}

nsresult txMozillaXMLOutput::endElement() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  TX_ENSURE_CURRENTNODE;

  if (mBadChildLevel) {
    --mBadChildLevel;
    MOZ_LOG(txLog::xslt, LogLevel::Debug,
            ("endElement, mBadChildLevel = %d\n", mBadChildLevel));
    return NS_OK;
  }

  --mTreeDepth;

  nsresult rv = closePrevious(true);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(mCurrentNode->IsElement(), "borked mCurrentNode");
  NS_ENSURE_TRUE(mCurrentNode->IsElement(), NS_ERROR_UNEXPECTED);

  Element* element = mCurrentNode->AsElement();

  // Handle html-elements
  if (!mNoFixup) {
    if (element->IsHTMLElement()) {
      endHTMLElement(element);
    }

    // Handle elements that are different when parser-created
    if (nsIContent::RequiresDoneCreatingElement(
            element->NodeInfo()->NamespaceID(),
            element->NodeInfo()->NameAtom())) {
      element->DoneCreatingElement();
    } else if (element->IsSVGElement(nsGkAtoms::script) ||
               element->IsHTMLElement(nsGkAtoms::script)) {
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(element);
      if (sele) {
        // https://html.spec.whatwg.org/#parsing-xhtml-documents
        // When the element's end tag is subsequently parsed, the user agent
        // must perform a microtask checkpoint, and then prepare the script
        // element.
        {
          nsAutoMicroTaskForDocument mt(mDocument);
        }
        bool block = sele->AttemptToExecute(nullptr /* aParser */);
        // If the act of insertion evaluated the script, we're fine.
        // Else, add this script element to the array of loading scripts.
        if (block) {
          mNotifier->AddScriptElement(sele);
        }
      } else {
        MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
                   "Script elements need to implement nsIScriptElement and SVG "
                   "wasn't disabled.");
      }
    } else if (nsIContent::RequiresDoneAddingChildren(
                   element->NodeInfo()->NamespaceID(),
                   element->NodeInfo()->NameAtom())) {
      nsAutoScriptBlocker scriptBlocker;
      element->DoneAddingChildren(true);
    }
  }

  if (mCreatingNewDocument) {
    // Handle all sorts of stylesheets
    if (auto* linkStyle = LinkStyle::FromNode(*mCurrentNode)) {
      auto updateOrError =
          linkStyle->EnableUpdatesAndUpdateStyleSheet(mNotifier);
      if (mNotifier && updateOrError.isOk() &&
          updateOrError.unwrap().ShouldBlock()) {
        mNotifier->AddPendingStylesheet();
      }
    }
  }

  // Add the element to the tree if it wasn't added before and take one step
  // up the tree
  MOZ_ASSERT(!mCurrentNodeStack.IsEmpty(), "empty stack");
  nsCOMPtr<nsINode> parent;
  if (!mCurrentNodeStack.IsEmpty()) {
    parent = mCurrentNodeStack.PopLastElement();
  }

  if (mCurrentNode == mNonAddedNode) {
    if (parent == mDocument) {
      NS_ASSERTION(!mRootContentCreated,
                   "Parent to add to shouldn't be a document if we "
                   "have a root content");
      mRootContentCreated = true;
    }

    // Check to make sure that script hasn't inserted the node somewhere
    // else in the tree
    if (!mCurrentNode->GetParentNode()) {
      parent->AppendChildTo(mNonAddedNode, true, IgnoreErrors());
    }
    mNonAddedNode = nullptr;
  }

  mCurrentNode = std::move(parent);

  mTableState =
      static_cast<TableState>(NS_PTR_TO_INT32(mTableStateStack.pop()));

  return NS_OK;
}

void txMozillaXMLOutput::getOutputDocument(Document** aDocument) {
  NS_IF_ADDREF(*aDocument = mDocument);
}

nsresult txMozillaXMLOutput::processingInstruction(const nsString& aTarget,
                                                   const nsString& aData) {
  nsresult rv = closePrevious(true);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mOutputFormat.mMethod == eHTMLOutput) return NS_OK;

  TX_ENSURE_CURRENTNODE;

  rv = nsContentUtils::CheckQName(aTarget, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContent> pi =
      NS_NewXMLProcessingInstruction(mNodeInfoManager, aTarget, aData);

  LinkStyle* linkStyle = nullptr;
  if (mCreatingNewDocument) {
    linkStyle = LinkStyle::FromNode(*pi);
    if (linkStyle) {
      linkStyle->DisableUpdates();
    }
  }

  ErrorResult error;
  mCurrentNode->AppendChildTo(pi, true, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  if (linkStyle) {
    auto updateOrError = linkStyle->EnableUpdatesAndUpdateStyleSheet(mNotifier);
    if (mNotifier && updateOrError.isOk() &&
        updateOrError.unwrap().ShouldBlock()) {
      mNotifier->AddPendingStylesheet();
    }
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::startDocument() {
  if (mNotifier) {
    mNotifier->OnTransformStart();
  }

  if (mCreatingNewDocument) {
    ScriptLoader* loader = mDocument->GetScriptLoader();
    if (loader) {
      loader->BeginDeferringScripts();
    }
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::startElement(nsAtom* aPrefix, nsAtom* aLocalName,
                                          nsAtom* aLowercaseLocalName,
                                          const int32_t aNsID) {
  MOZ_ASSERT(aNsID != kNameSpaceID_None || !aPrefix,
             "Can't have prefix without namespace");

  if (mOutputFormat.mMethod == eHTMLOutput && aNsID == kNameSpaceID_None) {
    RefPtr<nsAtom> owner;
    if (!aLowercaseLocalName) {
      owner = TX_ToLowerCaseAtom(aLocalName);
      NS_ENSURE_TRUE(owner, NS_ERROR_OUT_OF_MEMORY);

      aLowercaseLocalName = owner;
    }
    return startElementInternal(nullptr, aLowercaseLocalName,
                                kNameSpaceID_XHTML);
  }

  return startElementInternal(aPrefix, aLocalName, aNsID);
}

nsresult txMozillaXMLOutput::startElement(nsAtom* aPrefix,
                                          const nsAString& aLocalName,
                                          const int32_t aNsID) {
  int32_t nsId = aNsID;
  RefPtr<nsAtom> lname;

  if (mOutputFormat.mMethod == eHTMLOutput && aNsID == kNameSpaceID_None) {
    nsId = kNameSpaceID_XHTML;

    nsAutoString lnameStr;
    nsContentUtils::ASCIIToLower(aLocalName, lnameStr);
    lname = NS_Atomize(lnameStr);
  } else {
    lname = NS_Atomize(aLocalName);
  }

  // No biggie if we lose the prefix due to OOM
  NS_ENSURE_TRUE(lname, NS_ERROR_OUT_OF_MEMORY);

  // Check that it's a valid name
  if (!nsContentUtils::IsValidNodeName(lname, aPrefix, nsId)) {
    // Try without prefix
    aPrefix = nullptr;
    if (!nsContentUtils::IsValidNodeName(lname, aPrefix, nsId)) {
      return NS_ERROR_XSLT_BAD_NODE_NAME;
    }
  }

  return startElementInternal(aPrefix, lname, nsId);
}

nsresult txMozillaXMLOutput::startElementInternal(nsAtom* aPrefix,
                                                  nsAtom* aLocalName,
                                                  int32_t aNsID) {
  TX_ENSURE_CURRENTNODE;

  if (mBadChildLevel) {
    ++mBadChildLevel;
    MOZ_LOG(txLog::xslt, LogLevel::Debug,
            ("startElement, mBadChildLevel = %d\n", mBadChildLevel));
    return NS_OK;
  }

  MOZ_TRY(closePrevious(true));

  // Push and init state
  if (mTreeDepth == MAX_REFLOW_DEPTH) {
    // eCloseElement couldn't add the parent so we fail as well or we've
    // reached the limit of the depth of the tree that we allow.
    ++mBadChildLevel;
    MOZ_LOG(txLog::xslt, LogLevel::Debug,
            ("startElement, mBadChildLevel = %d\n", mBadChildLevel));
    return NS_OK;
  }

  ++mTreeDepth;

  mTableStateStack.push(NS_INT32_TO_PTR(mTableState));

  mCurrentNodeStack.AppendElement(mCurrentNode);

  mTableState = NORMAL;
  mOpenedElementIsHTML = false;

  // Create the element
  RefPtr<NodeInfo> ni = mNodeInfoManager->GetNodeInfo(
      aLocalName, aPrefix, aNsID, nsINode::ELEMENT_NODE);

  NS_NewElement(getter_AddRefs(mOpenedElement), ni.forget(),
                mCreatingNewDocument ? FROM_PARSER_XSLT : FROM_PARSER_FRAGMENT);

  // Set up the element and adjust state
  if (!mNoFixup && aNsID == kNameSpaceID_XHTML) {
    mOpenedElementIsHTML = (mOutputFormat.mMethod == eHTMLOutput);
    MOZ_TRY(startHTMLElement(mOpenedElement, mOpenedElementIsHTML));
  }

  if (mCreatingNewDocument) {
    // Handle all sorts of stylesheets
    if (auto* linkStyle = LinkStyle::FromNode(*mOpenedElement)) {
      linkStyle->DisableUpdates();
    }
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::closePrevious(bool aFlushText) {
  TX_ENSURE_CURRENTNODE;

  if (mOpenedElement) {
    bool currentIsDoc = mCurrentNode == mDocument;
    if (currentIsDoc && mRootContentCreated) {
      // We already have a document element, but the XSLT spec allows this.
      // As a workaround, create a wrapper object and use that as the
      // document element.

      MOZ_TRY(createTxWrapper());
    }

    ErrorResult error;
    mCurrentNode->AppendChild(*mOpenedElement, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }

    if (currentIsDoc) {
      mRootContentCreated = true;
      nsContentUtils::AddScriptRunner(
          MakeAndAddRef<nsDocElementCreatedNotificationRunner>(mDocument));
    }

    mCurrentNode = mOpenedElement;
    mOpenedElement = nullptr;
  } else if (aFlushText && !mText.IsEmpty()) {
    // Text can't appear in the root of a document
    if (mDocument == mCurrentNode) {
      if (XMLUtils::isWhitespace(mText)) {
        mText.Truncate();

        return NS_OK;
      }

      MOZ_TRY(createTxWrapper());
    }
    RefPtr<nsTextNode> text =
        new (mNodeInfoManager) nsTextNode(mNodeInfoManager);

    MOZ_TRY(text->SetText(mText, false));

    ErrorResult error;
    mCurrentNode->AppendChildTo(text, true, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }

    mText.Truncate();
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::createTxWrapper() {
  NS_ASSERTION(mDocument == mCurrentNode,
               "creating wrapper when document isn't parent");

  int32_t namespaceID;
  MOZ_TRY(nsNameSpaceManager::GetInstance()->RegisterNameSpace(
      nsLiteralString(kTXNameSpaceURI), namespaceID));

  nsCOMPtr<Element> wrapper =
      mDocument->CreateElem(nsDependentAtomString(nsGkAtoms::result),
                            nsGkAtoms::transformiix, namespaceID);

#ifdef DEBUG
  // Keep track of the location of the current documentElement, if there is
  // one, so we can verify later
  uint32_t j = 0, rootLocation = 0;
#endif
  for (nsCOMPtr<nsIContent> childContent = mDocument->GetFirstChild();
       childContent; childContent = childContent->GetNextSibling()) {
#ifdef DEBUG
    if (childContent->IsElement()) {
      rootLocation = j;
    }
#endif

    if (childContent->NodeInfo()->NameAtom() ==
        nsGkAtoms::documentTypeNodeName) {
#ifdef DEBUG
      // The new documentElement should go after the document type.
      // This is needed for cases when there is no existing
      // documentElement in the document.
      rootLocation = std::max(rootLocation, j + 1);
      ++j;
#endif
    } else {
      ErrorResult error;
      wrapper->AppendChild(*childContent, error);
      if (error.Failed()) {
        return error.StealNSResult();
      }
      break;
    }
  }

  mCurrentNodeStack.AppendElement(wrapper);
  mCurrentNode = wrapper;
  mRootContentCreated = true;
  NS_ASSERTION(rootLocation == mDocument->GetChildCount(),
               "Incorrect root location");
  ErrorResult error;
  mDocument->AppendChild(*wrapper, error);
  return error.StealNSResult();
}

nsresult txMozillaXMLOutput::startHTMLElement(nsIContent* aElement,
                                              bool aIsHTML) {
  if ((!aElement->IsHTMLElement(nsGkAtoms::tr) || !aIsHTML) &&
      NS_PTR_TO_INT32(mTableStateStack.peek()) == ADDED_TBODY) {
    MOZ_ASSERT(!mCurrentNodeStack.IsEmpty(), "empty stack");
    if (mCurrentNodeStack.IsEmpty()) {
      mCurrentNode = nullptr;
    } else {
      mCurrentNode = mCurrentNodeStack.PopLastElement();
    }
    mTableStateStack.pop();
  }

  if (aElement->IsHTMLElement(nsGkAtoms::table) && aIsHTML) {
    mTableState = TABLE;
  } else if (aElement->IsHTMLElement(nsGkAtoms::tr) && aIsHTML &&
             NS_PTR_TO_INT32(mTableStateStack.peek()) == TABLE) {
    RefPtr<Element> tbody;
    MOZ_TRY(createHTMLElement(nsGkAtoms::tbody, getter_AddRefs(tbody)));

    ErrorResult error;
    mCurrentNode->AppendChildTo(tbody, true, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }

    mTableStateStack.push(NS_INT32_TO_PTR(ADDED_TBODY));

    mCurrentNodeStack.AppendElement(tbody);
    mCurrentNode = tbody;
  } else if (aElement->IsHTMLElement(nsGkAtoms::head) &&
             mOutputFormat.mMethod == eHTMLOutput) {
    // Insert META tag, according to spec, 16.2, like
    // <META http-equiv="Content-Type" content="text/html; charset=EUC-JP">
    RefPtr<Element> meta;
    MOZ_TRY(createHTMLElement(nsGkAtoms::meta, getter_AddRefs(meta)));

    MOZ_TRY(meta->SetAttr(kNameSpaceID_None, nsGkAtoms::httpEquiv,
                          u"Content-Type"_ns, false, IsKnownNewAttr::Yes));

    nsAutoString metacontent;
    CopyUTF8toUTF16(mOutputFormat.mMediaType, metacontent);
    metacontent.AppendLiteral("; charset=");
    metacontent.Append(mOutputFormat.mEncoding);
    MOZ_TRY(meta->SetAttr(kNameSpaceID_None, nsGkAtoms::content, metacontent,
                          false, IsKnownNewAttr::Yes));

    // No need to notify since aElement hasn't been inserted yet
    NS_ASSERTION(!aElement->IsInUncomposedDoc(), "should not be in doc");
    ErrorResult error;
    aElement->AppendChildTo(meta, false, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }
  }

  return NS_OK;
}

void txMozillaXMLOutput::endHTMLElement(nsIContent* aElement) {
  if (mTableState == ADDED_TBODY) {
    NS_ASSERTION(aElement->IsHTMLElement(nsGkAtoms::tbody),
                 "Element flagged as added tbody isn't a tbody");
    MOZ_ASSERT(!mCurrentNodeStack.IsEmpty(), "empty stack");
    if (mCurrentNodeStack.IsEmpty()) {
      mCurrentNode = nullptr;
    } else {
      mCurrentNode = mCurrentNodeStack.PopLastElement();
    }
    mTableState =
        static_cast<TableState>(NS_PTR_TO_INT32(mTableStateStack.pop()));
  }
}

nsresult txMozillaXMLOutput::createResultDocument(const nsAString& aName,
                                                  int32_t aNsID,
                                                  Document* aSourceDocument,
                                                  bool aLoadedAsData) {
  // Create the document
  if (mOutputFormat.mMethod == eHTMLOutput) {
    MOZ_TRY(NS_NewHTMLDocument(
        getter_AddRefs(mDocument), nullptr, nullptr,
        aLoadedAsData ? LoadedAsData::AsData : LoadedAsData::No));
  } else {
    // We should check the root name/namespace here and create the
    // appropriate document
    MOZ_TRY(NS_NewXMLDocument(
        getter_AddRefs(mDocument), nullptr, nullptr,
        aLoadedAsData ? LoadedAsData::AsData : LoadedAsData::No));
  }
  // This should really be handled by Document::BeginLoad
  MOZ_ASSERT(
      mDocument->GetReadyStateEnum() == Document::READYSTATE_UNINITIALIZED,
      "Bad readyState");
  mDocument->SetReadyStateInternal(Document::READYSTATE_LOADING);
  mDocument->SetMayStartLayout(false);
  bool hasHadScriptObject = false;
  nsIScriptGlobalObject* sgo =
      aSourceDocument->GetScriptHandlingObject(hasHadScriptObject);
  NS_ENSURE_STATE(sgo || !hasHadScriptObject);

  mCurrentNode = mDocument;
  mNodeInfoManager = mDocument->NodeInfoManager();

  // Reset and set up the document
  URIUtils::ResetWithSource(mDocument, aSourceDocument);

  // Make sure we set the script handling object after resetting with the
  // source, so that we have the right principal.
  mDocument->SetScriptHandlingObject(sgo);

  mDocument->SetStateObjectFrom(aSourceDocument);

  // Set the charset
  if (!mOutputFormat.mEncoding.IsEmpty()) {
    const Encoding* encoding = Encoding::ForLabel(mOutputFormat.mEncoding);
    if (encoding) {
      mDocument->SetDocumentCharacterSetSource(kCharsetFromOtherComponent);
      mDocument->SetDocumentCharacterSet(WrapNotNull(encoding));
    }
  }

  // Set the mime-type
  if (!mOutputFormat.mMediaType.IsEmpty()) {
    mDocument->SetContentType(mOutputFormat.mMediaType);
  } else if (mOutputFormat.mMethod == eHTMLOutput) {
    mDocument->SetContentType("text/html"_ns);
  } else {
    mDocument->SetContentType("application/xml"_ns);
  }

  if (mOutputFormat.mMethod == eXMLOutput &&
      mOutputFormat.mOmitXMLDeclaration != eTrue) {
    int32_t standalone;
    if (mOutputFormat.mStandalone == eNotSet) {
      standalone = -1;
    } else if (mOutputFormat.mStandalone == eFalse) {
      standalone = 0;
    } else {
      standalone = 1;
    }

    // Could use mOutputFormat.mVersion.get() when we support
    // versions > 1.0.
    static const char16_t kOneDotZero[] = {'1', '.', '0', '\0'};
    mDocument->SetXMLDeclaration(kOneDotZero, mOutputFormat.mEncoding.get(),
                                 standalone);
  }

  // Set up script loader of the result document.
  ScriptLoader* loader = mDocument->GetScriptLoader();
  if (loader) {
    if (mNotifier) {
      loader->AddObserver(mNotifier);
    } else {
      // Don't load scripts, we can't notify the caller when they're loaded.
      loader->SetEnabled(false);
    }
  }

  if (mNotifier) {
    MOZ_TRY(mNotifier->SetOutputDocument(mDocument));
    MOZ_TRY(mDocument->InitPermissionsPolicy(mDocument->GetChannel()));
  }

  // Do this after calling OnDocumentCreated to ensure that the
  // PresShell/PresContext has been hooked up and get notified.
  mDocument->SetCompatibilityMode(eCompatibility_FullStandards);

  // Add a doc-type if requested
  if (!mOutputFormat.mSystemId.IsEmpty()) {
    nsAutoString qName;
    if (mOutputFormat.mMethod == eHTMLOutput) {
      qName.AssignLiteral("html");
    } else {
      qName.Assign(aName);
    }

    nsresult rv = nsContentUtils::CheckQName(qName);
    if (NS_SUCCEEDED(rv)) {
      RefPtr<nsAtom> doctypeName = NS_Atomize(qName);
      if (!doctypeName) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      // Indicate that there is no internal subset (not just an empty one)
      RefPtr<DocumentType> documentType = NS_NewDOMDocumentType(
          mNodeInfoManager, doctypeName, mOutputFormat.mPublicId,
          mOutputFormat.mSystemId, VoidString());

      ErrorResult error;
      mDocument->AppendChildTo(documentType, true, error);
      if (error.Failed()) {
        return error.StealNSResult();
      }
    }
  }

  return NS_OK;
}

nsresult txMozillaXMLOutput::createHTMLElement(nsAtom* aName,
                                               Element** aResult) {
  NS_ASSERTION(mOutputFormat.mMethod == eHTMLOutput,
               "need to adjust createHTMLElement");

  *aResult = nullptr;

  RefPtr<NodeInfo> ni;
  ni = mNodeInfoManager->GetNodeInfo(aName, nullptr, kNameSpaceID_XHTML,
                                     nsINode::ELEMENT_NODE);

  nsCOMPtr<Element> el;
  nsresult rv = NS_NewHTMLElement(
      getter_AddRefs(el), ni.forget(),
      mCreatingNewDocument ? FROM_PARSER_XSLT : FROM_PARSER_FRAGMENT);
  el.forget(aResult);
  return rv;
}

txTransformNotifier::txTransformNotifier(Document* aSourceDocument)
    : mSourceDocument(aSourceDocument),
      mPendingStylesheetCount(0),
      mInTransform(false) {}

txTransformNotifier::~txTransformNotifier() = default;

NS_IMPL_ISUPPORTS(txTransformNotifier, nsIScriptLoaderObserver,
                  nsICSSLoaderObserver)

NS_IMETHODIMP
txTransformNotifier::ScriptAvailable(nsresult aResult,
                                     nsIScriptElement* aElement,
                                     bool aIsInlineClassicScript, nsIURI* aURI,
                                     uint32_t aLineNo) {
  if (NS_FAILED(aResult) && mScriptElements.RemoveElement(aElement)) {
    SignalTransformEnd();
  }

  return NS_OK;
}

NS_IMETHODIMP
txTransformNotifier::ScriptEvaluated(nsresult aResult,
                                     nsIScriptElement* aElement,
                                     bool aIsInline) {
  if (mScriptElements.RemoveElement(aElement)) {
    SignalTransformEnd();
  }

  return NS_OK;
}

NS_IMETHODIMP
txTransformNotifier::StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                                      nsresult aStatus) {
  if (mPendingStylesheetCount == 0) {
    // We weren't waiting on this stylesheet anyway.  This can happen if
    // SignalTransformEnd got called with an error aResult.  See
    // http://bugzilla.mozilla.org/show_bug.cgi?id=215465.
    return NS_OK;
  }

  // We're never waiting for alternate stylesheets
  if (!aWasDeferred) {
    --mPendingStylesheetCount;
    SignalTransformEnd();
  }

  return NS_OK;
}

void txTransformNotifier::Init(nsITransformObserver* aObserver) {
  mObserver = aObserver;
}

void txTransformNotifier::AddScriptElement(nsIScriptElement* aElement) {
  mScriptElements.AppendElement(aElement);
}

void txTransformNotifier::AddPendingStylesheet() { ++mPendingStylesheetCount; }

void txTransformNotifier::OnTransformEnd(nsresult aResult) {
  mInTransform = false;
  SignalTransformEnd(aResult);
}

void txTransformNotifier::OnTransformStart() { mInTransform = true; }

nsresult txTransformNotifier::SetOutputDocument(Document* aDocument) {
  mDocument = aDocument;

  // Notify the contentsink that the document is created
  return mObserver->OnDocumentCreated(mSourceDocument, mDocument);
}

void txTransformNotifier::SignalTransformEnd(nsresult aResult) {
  if (mInTransform ||
      (NS_SUCCEEDED(aResult) &&
       (!mScriptElements.IsEmpty() || mPendingStylesheetCount > 0))) {
    return;
  }

  // mPendingStylesheetCount is nonzero at this point only if aResult is an
  // error.  Set it to 0 so we won't reenter this code when we stop the
  // CSSLoader.
  mPendingStylesheetCount = 0;
  mScriptElements.Clear();

  // Make sure that we don't get deleted while this function is executed and
  // we remove ourselfs from the scriptloader
  nsCOMPtr<nsIScriptLoaderObserver> kungFuDeathGrip(this);

  if (mDocument) {
    if (const RefPtr<dom::ScriptLoader> scriptLoader =
            mDocument->GetScriptLoader()) {
      scriptLoader->DeferCheckpointReached();
      scriptLoader->RemoveObserver(this);
      // XXX Maybe we want to cancel script loads if NS_FAILED(rv)?
    }
    if (NS_FAILED(aResult)) {
      if (css::Loader* cssLoader = mDocument->GetExistingCSSLoader()) {
        cssLoader->Stop();
      }
    }
  }

  if (NS_SUCCEEDED(aResult)) {
    mObserver->OnTransformDone(mSourceDocument, aResult, mDocument);
  }
}
