/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/dom/L10nOverlays.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/L10nOverlaysBinding.h"
#include "mozilla/dom/Element.h"
#include "mozilla/NullPrincipal.h"
#include "nsNetUtil.h"

using mozilla::NullPrincipal;
using namespace mozilla::dom;

static already_AddRefed<Document> SetUpDocument() {
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "about:blank");
  nsCOMPtr<nsIPrincipal> principal =
      NullPrincipal::CreateWithoutOriginAttributes();
  nsCOMPtr<Document> document;
  nsresult rv = NS_NewDOMDocument(getter_AddRefs(document),
                                  u""_ns,   // aNamespaceURI
                                  u""_ns,   // aQualifiedName
                                  nullptr,  // aDoctype
                                  uri, uri, principal,
                                  false,    // aLoadedAsData
                                  nullptr,  // aEventObject
                                  DocumentFlavor::HTML);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }
  return document.forget();
}

/**
 * This test verifies that the basic C++ DOM L10nOverlays API
 * works correctly.
 */
TEST(DOM_L10n_Overlays, Initial)
{
  mozilla::ErrorResult rv;

  // 1. Set up an HTML document.
  nsCOMPtr<Document> doc = SetUpDocument();

  // 2. Create a simple Element with a child.
  //
  //   <div>
  //     <a data-l10n-name="link" href="https://www.mozilla.org"></a>
  //   </div>
  //
  RefPtr<Element> elem = doc->CreateHTMLElement(nsGkAtoms::div);
  RefPtr<Element> span = doc->CreateHTMLElement(nsGkAtoms::a);
  span->SetAttribute(u"data-l10n-name"_ns, u"link"_ns, rv);
  span->SetAttribute(u"href"_ns, u"https://www.mozilla.org"_ns, rv);
  elem->AppendChild(*span, rv);

  // 3. Create an L10nMessage with a translation for the element.
  L10nMessage translation;
  translation.mValue.AssignLiteral(
      "Hello <a data-l10n-name=\"link\">World</a>.");

  // 4. Translate the element.
  nsTArray<L10nOverlaysError> errors;
  L10nOverlays::TranslateElement(*elem, translation, errors, rv);

  nsAutoString textContent;
  elem->GetInnerHTML(textContent, rv);

  // 5. Verify that the innerHTML matches the expectations.
  ASSERT_STREQ(NS_ConvertUTF16toUTF8(textContent).get(),
               "Hello <a data-l10n-name=\"link\" "
               "href=\"https://www.mozilla.org\">World</a>.");
}
