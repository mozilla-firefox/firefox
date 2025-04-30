/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XMLDocument.h"
#include "nsCharsetSource.h"
#include "nsIXMLContentSink.h"
#include "nsPresContext.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "nsHTMLParts.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsError.h"
#include "nsIPrincipal.h"
#include "mozilla/dom/Attr.h"
#include "nsCExternalHandlerService.h"
#include "nsMimeTypes.h"
#include "nsContentUtils.h"
#include "nsThreadUtils.h"
#include "nsJSUtils.h"
#include "nsCRT.h"
#include "nsComponentManagerUtils.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentPolicyUtils.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsHTMLDocument.h"
#include "nsParser.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/Encoding.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/XMLDocumentBinding.h"
#include "mozilla/dom/DocumentBinding.h"

using namespace mozilla;
using namespace mozilla::dom;

// ==================================================================
// =
// ==================================================================

nsresult NS_NewDOMDocument(Document** aInstancePtrResult,
                           const nsAString& aNamespaceURI,
                           const nsAString& aQualifiedName,
                           DocumentType* aDoctype, nsIURI* aDocumentURI,
                           nsIURI* aBaseURI, nsIPrincipal* aPrincipal,
                           bool aLoadedAsData, nsIGlobalObject* aEventObject,
                           DocumentFlavor aFlavor) {
  // Note: can't require that aDocumentURI/aBaseURI/aPrincipal be non-null,
  // since at least one caller (XMLHttpRequest) doesn't have decent args to
  // pass in.

  nsresult rv;

  *aInstancePtrResult = nullptr;

  nsCOMPtr<Document> d;
  bool isHTML = false;
  bool isXHTML = false;
  if (aFlavor == DocumentFlavor::SVG) {
    rv = NS_NewSVGDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
  } else if (aFlavor == DocumentFlavor::HTML) {
    rv = NS_NewHTMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
    isHTML = true;
  } else if (aFlavor == DocumentFlavor::XML) {
    rv = NS_NewXMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
  } else if (aFlavor == DocumentFlavor::Plain) {
    rv = NS_NewXMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal,
                           aLoadedAsData, true);
  } else if (aDoctype) {
    MOZ_ASSERT(aFlavor == DocumentFlavor::LegacyGuess);
    nsAutoString publicId, name;
    aDoctype->GetPublicId(publicId);
    if (publicId.IsEmpty()) {
      aDoctype->GetName(name);
    }
    if (name.EqualsLiteral("html") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.01//EN") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.01 Frameset//EN") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.01 Transitional//EN") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.0//EN") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.0 Frameset//EN") ||
        publicId.EqualsLiteral("-//W3C//DTD HTML 4.0 Transitional//EN")) {
      rv = NS_NewHTMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
      isHTML = true;
    } else if (publicId.EqualsLiteral("-//W3C//DTD XHTML 1.0 Strict//EN") ||
               publicId.EqualsLiteral(
                   "-//W3C//DTD XHTML 1.0 Transitional//EN") ||
               publicId.EqualsLiteral("-//W3C//DTD XHTML 1.0 Frameset//EN")) {
      rv = NS_NewHTMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
      isHTML = true;
      isXHTML = true;
    } else if (publicId.EqualsLiteral("-//W3C//DTD SVG 1.1//EN")) {
      rv = NS_NewSVGDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
    } else {
      rv = NS_NewXMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
    }
  } else {
    MOZ_ASSERT(aFlavor == DocumentFlavor::LegacyGuess);
    rv = NS_NewXMLDocument(getter_AddRefs(d), aPrincipal, aPrincipal);
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  if (isHTML) {
    d->SetCompatibilityMode(eCompatibility_FullStandards);
    d->AsHTMLDocument()->SetIsXHTML(isXHTML);
  }
  d->SetLoadedAsData(aLoadedAsData, /* aConsiderForMemoryReporting */ true);
  d->SetDocumentURI(aDocumentURI);
  d->SetBaseURI(aBaseURI);

  // We need to set the script handling object after we set the principal such
  // that the doc group is assigned correctly.
  if (nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aEventObject)) {
    d->SetScriptHandlingObject(sgo);
  } else if (aEventObject) {
    d->SetScopeObject(aEventObject);
  }

  // XMLDocuments and documents "created in memory" get to be UTF-8 by default,
  // unlike the legacy HTML mess
  d->SetDocumentCharacterSet(UTF_8_ENCODING);

  if (aDoctype) {
    ErrorResult result;
    d->AppendChild(*aDoctype, result);
    // Need to WouldReportJSException() if our callee can throw a JS
    // exception (which it can) and we're neither propagating the
    // error out nor unconditionally suppressing it.
    result.WouldReportJSException();
    if (NS_WARN_IF(result.Failed())) {
      return result.StealNSResult();
    }
  }

  if (!aQualifiedName.IsEmpty()) {
    ErrorResult result;
    ElementCreationOptionsOrString options;
    Unused << options.SetAsString();

    nsCOMPtr<Element> root =
        d->CreateElementNS(aNamespaceURI, aQualifiedName, options, result);
    if (NS_WARN_IF(result.Failed())) {
      return result.StealNSResult();
    }

    d->AppendChild(*root, result);
    // Need to WouldReportJSException() if our callee can throw a JS
    // exception (which it can) and we're neither propagating the
    // error out nor unconditionally suppressing it.
    result.WouldReportJSException();
    if (NS_WARN_IF(result.Failed())) {
      return result.StealNSResult();
    }
  }

  d.forget(aInstancePtrResult);

  return NS_OK;
}

nsresult NS_NewXMLDocument(Document** aInstancePtrResult,
                           nsIPrincipal* aPrincipal,
                           nsIPrincipal* aPartitionedPrincipal,
                           bool aLoadedAsData, bool aIsPlainDocument) {
  RefPtr<XMLDocument> doc = new XMLDocument();

  nsresult rv = doc->Init(aPrincipal, aPartitionedPrincipal);

  if (NS_FAILED(rv)) {
    *aInstancePtrResult = nullptr;
    return rv;
  }

  doc->SetLoadedAsData(aLoadedAsData, /* aConsiderForMemoryReporting */ true);
  doc->mIsPlainDocument = aIsPlainDocument;
  doc.forget(aInstancePtrResult);

  return NS_OK;
}

namespace mozilla::dom {

XMLDocument::XMLDocument(const char* aContentType)
    : Document(aContentType),
      mChannelIsPending(false),
      mIsPlainDocument(false),
      mSuppressParserErrorElement(false),
      mSuppressParserErrorConsoleMessages(false) {
  mType = eGenericXML;
}

nsresult XMLDocument::Init(nsIPrincipal* aPrincipal,
                           nsIPrincipal* aPartitionedPrincipal) {
  nsresult rv = Document::Init(aPrincipal, aPartitionedPrincipal);
  NS_ENSURE_SUCCESS(rv, rv);

  return rv;
}

void XMLDocument::Reset(nsIChannel* aChannel, nsILoadGroup* aLoadGroup) {
  Document::Reset(aChannel, aLoadGroup);
}

void XMLDocument::ResetToURI(nsIURI* aURI, nsILoadGroup* aLoadGroup,
                             nsIPrincipal* aPrincipal,
                             nsIPrincipal* aPartitionedPrincipal) {
  if (mChannelIsPending) {
    StopDocumentLoad();
    mChannel->CancelWithReason(NS_BINDING_ABORTED,
                               "XMLDocument::ResetToURI"_ns);
    mChannelIsPending = false;
  }

  Document::ResetToURI(aURI, aLoadGroup, aPrincipal, aPartitionedPrincipal);
}

void XMLDocument::SetSuppressParserErrorElement(bool aSuppress) {
  mSuppressParserErrorElement = aSuppress;
}

bool XMLDocument::SuppressParserErrorElement() {
  return mSuppressParserErrorElement;
}

void XMLDocument::SetSuppressParserErrorConsoleMessages(bool aSuppress) {
  mSuppressParserErrorConsoleMessages = aSuppress;
}

bool XMLDocument::SuppressParserErrorConsoleMessages() {
  return mSuppressParserErrorConsoleMessages;
}

nsresult XMLDocument::StartDocumentLoad(
    const char* aCommand, nsIChannel* aChannel, nsILoadGroup* aLoadGroup,
    nsISupports* aContainer, nsIStreamListener** aDocListener, bool aReset) {
  nsresult rv = Document::StartDocumentLoad(aCommand, aChannel, aLoadGroup,
                                            aContainer, aDocListener, aReset);
  if (NS_FAILED(rv)) return rv;

  int32_t charsetSource = kCharsetFromDocTypeDefault;
  NotNull<const Encoding*> encoding = UTF_8_ENCODING;
  TryChannelCharset(aChannel, charsetSource, encoding, nullptr);

  nsCOMPtr<nsIURI> aUrl;
  rv = aChannel->GetURI(getter_AddRefs(aUrl));
  if (NS_FAILED(rv)) return rv;

  mParser = new nsParser();

  nsCOMPtr<nsIXMLContentSink> sink;

  nsCOMPtr<nsIDocShell> docShell;
  if (aContainer) {
    docShell = do_QueryInterface(aContainer);
    NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);
  }
  rv = NS_NewXMLContentSink(getter_AddRefs(sink), this, aUrl, docShell,
                            aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the parser as the stream listener for the document loader...
  rv = CallQueryInterface(mParser, aDocListener);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(mChannel, "How can we not have a channel here?");
  mChannelIsPending = true;

  SetDocumentCharacterSet(encoding);
  mParser->SetDocumentCharset(encoding, charsetSource);
  mParser->SetCommand(aCommand);
  mParser->SetContentSink(sink);
  mParser->Parse(aUrl);

  return NS_OK;
}

void XMLDocument::EndLoad() {
  mChannelIsPending = false;

  mSynchronousDOMContentLoaded = mLoadedAsData;
  Document::EndLoad();
  if (mSynchronousDOMContentLoaded) {
    mSynchronousDOMContentLoaded = false;
    Document::SetReadyStateInternal(Document::READYSTATE_COMPLETE);
    // Generate a document load event for the case when an XML
    // document was loaded as pure data without any presentation
    // attached to it.
    WidgetEvent event(true, eLoad);
    EventDispatcher::Dispatch(this, nullptr, &event);
  }
}

/* virtual */
void XMLDocument::DocAddSizeOfExcludingThis(nsWindowSizes& aWindowSizes) const {
  Document::DocAddSizeOfExcludingThis(aWindowSizes);
}

// Document interface

nsresult XMLDocument::Clone(dom::NodeInfo* aNodeInfo, nsINode** aResult) const {
  NS_ASSERTION(aNodeInfo->NodeInfoManager() == mNodeInfoManager,
               "Can't import this document into another document!");

  RefPtr<XMLDocument> clone = new XMLDocument();
  nsresult rv = CloneDocHelper(clone);
  NS_ENSURE_SUCCESS(rv, rv);

  // State from XMLDocument
  clone->mIsPlainDocument = mIsPlainDocument;

  clone.forget(aResult);
  return NS_OK;
}

JSObject* XMLDocument::WrapNode(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  if (mIsPlainDocument) {
    return Document_Binding::Wrap(aCx, this, aGivenProto);
  }

  return XMLDocument_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
