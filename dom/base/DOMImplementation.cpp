/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DOMImplementation.h"

#include "mozilla/ContentEvents.h"
#include "mozilla/dom/DOMImplementationBinding.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentType.h"
#include "nsTextNode.h"

namespace mozilla::dom {

// QueryInterface implementation for DOMImplementation
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMImplementation)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DOMImplementation, mOwner)

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMImplementation)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMImplementation)

DOMImplementation::DOMImplementation(Document* aOwner,
                                     nsIGlobalObject* aScriptObject,
                                     nsIURI* aDocumentURI, nsIURI* aBaseURI)
    : mOwner(aOwner),
      mScriptObject(do_GetWeakReference(aScriptObject)),
      mDocumentURI(aDocumentURI),
      mBaseURI(aBaseURI) {
  MOZ_ASSERT(aOwner);
}

DOMImplementation::~DOMImplementation() = default;

JSObject* DOMImplementation::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return DOMImplementation_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<DocumentType> DOMImplementation::CreateDocumentType(
    const nsAString& aQualifiedName, const nsAString& aPublicId,
    const nsAString& aSystemId, ErrorResult& aRv) {
  if (!mOwner) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  aRv = nsContentUtils::CheckQName(aQualifiedName);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<nsAtom> name = NS_Atomize(aQualifiedName);
  if (!name) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  // Indicate that there is no internal subset (not just an empty one)
  RefPtr<DocumentType> docType = NS_NewDOMDocumentType(
      mOwner->NodeInfoManager(), name, aPublicId, aSystemId, VoidString());
  return docType.forget();
}

nsresult DOMImplementation::CreateDocument(const nsAString& aNamespaceURI,
                                           const nsAString& aQualifiedName,
                                           DocumentType* aDoctype,
                                           Document** aDocument) {
  *aDocument = nullptr;

  nsresult rv;
  if (!aQualifiedName.IsEmpty()) {
    const nsString& qName = PromiseFlatString(aQualifiedName);
    const char16_t* colon;
    rv = nsContentUtils::CheckQName(qName, true, &colon);
    NS_ENSURE_SUCCESS(rv, rv);

    if (colon && (DOMStringIsNull(aNamespaceURI) ||
                  (Substring(qName.get(), colon).EqualsLiteral("xml") &&
                   !aNamespaceURI.EqualsLiteral(
                       "http://www.w3.org/XML/1998/namespace")))) {
      return NS_ERROR_DOM_NAMESPACE_ERR;
    }
  }

  nsCOMPtr<nsIGlobalObject> scriptHandlingObject =
      do_QueryReferent(mScriptObject);

  NS_ENSURE_STATE(!mScriptObject || scriptHandlingObject);

  nsCOMPtr<Document> doc;

  rv = NS_NewDOMDocument(getter_AddRefs(doc), aNamespaceURI, aQualifiedName,
                         aDoctype, mDocumentURI, mBaseURI,
                         mOwner->NodePrincipal(), true, scriptHandlingObject,
                         DocumentFlavor::XML);
  NS_ENSURE_SUCCESS(rv, rv);

  // When DOMImplementation's createDocument method is invoked with
  // namespace set to HTML Namespace use the registry of the associated
  // document to the new instance.

  if (aNamespaceURI.EqualsLiteral("http://www.w3.org/1999/xhtml")) {
    doc->SetContentType("application/xhtml+xml"_ns);
  } else if (aNamespaceURI.EqualsLiteral("http://www.w3.org/2000/svg")) {
    doc->SetContentType("image/svg+xml"_ns);
  } else {
    doc->SetContentType("application/xml"_ns);
  }

  doc->SetReadyStateInternal(Document::READYSTATE_COMPLETE);

  doc.forget(aDocument);
  return NS_OK;
}

already_AddRefed<Document> DOMImplementation::CreateDocument(
    const nsAString& aNamespaceURI, const nsAString& aQualifiedName,
    DocumentType* aDoctype, ErrorResult& aRv) {
  nsCOMPtr<Document> document;
  aRv = CreateDocument(aNamespaceURI, aQualifiedName, aDoctype,
                       getter_AddRefs(document));
  return document.forget();
}

nsresult DOMImplementation::CreateHTMLDocument(const nsAString& aTitle,
                                               Document** aDocument) {
  *aDocument = nullptr;

  NS_ENSURE_STATE(mOwner);

  // Indicate that there is no internal subset (not just an empty one)
  RefPtr<DocumentType> doctype =
      NS_NewDOMDocumentType(mOwner->NodeInfoManager(),
                            nsGkAtoms::html,  // aName
                            u""_ns,           // aPublicId
                            u""_ns,           // aSystemId
                            VoidString());    // aInternalSubset

  nsCOMPtr<nsIGlobalObject> scriptHandlingObject =
      do_QueryReferent(mScriptObject);

  NS_ENSURE_STATE(!mScriptObject || scriptHandlingObject);

  nsCOMPtr<Document> doc;
  nsresult rv =
      NS_NewDOMDocument(getter_AddRefs(doc), u""_ns, u""_ns, doctype,
                        mDocumentURI, mBaseURI, mOwner->NodePrincipal(), true,
                        scriptHandlingObject, DocumentFlavor::LegacyGuess);
  NS_ENSURE_SUCCESS(rv, rv);

  ErrorResult error;
  nsCOMPtr<Element> root =
      doc->CreateElem(u"html"_ns, nullptr, kNameSpaceID_XHTML);
  doc->AppendChildTo(root, false, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  nsCOMPtr<Element> head =
      doc->CreateElem(u"head"_ns, nullptr, kNameSpaceID_XHTML);
  root->AppendChildTo(head, false, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  if (!DOMStringIsNull(aTitle)) {
    nsCOMPtr<Element> title =
        doc->CreateElem(u"title"_ns, nullptr, kNameSpaceID_XHTML);
    head->AppendChildTo(title, false, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }

    RefPtr<nsTextNode> titleText =
        new (doc->NodeInfoManager()) nsTextNode(doc->NodeInfoManager());
    rv = titleText->SetText(aTitle, false);
    NS_ENSURE_SUCCESS(rv, rv);
    title->AppendChildTo(titleText, false, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }
  }

  nsCOMPtr<Element> body =
      doc->CreateElem(u"body"_ns, nullptr, kNameSpaceID_XHTML);
  root->AppendChildTo(body, false, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  doc->SetReadyStateInternal(Document::READYSTATE_COMPLETE);

  doc.forget(aDocument);
  return NS_OK;
}

already_AddRefed<Document> DOMImplementation::CreateHTMLDocument(
    const Optional<nsAString>& aTitle, ErrorResult& aRv) {
  nsCOMPtr<Document> document;
  aRv = CreateHTMLDocument(aTitle.WasPassed() ? aTitle.Value() : VoidString(),
                           getter_AddRefs(document));
  return document.forget();
}

}  // namespace mozilla::dom
