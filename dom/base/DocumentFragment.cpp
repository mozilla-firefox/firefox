/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's DocumentFragment.
 */

#include "mozilla/dom/DocumentFragment.h"

#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragmentBinding.h"
#include "mozilla/dom/Element.h"  // for NS_IMPL_ELEMENT_CLONE
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/ShadowRoot.h"
#include "nsContentUtils.h"  // for NS_INTERFACE_MAP_ENTRY_TEAROFF
#include "nsDOMString.h"
#include "nsError.h"
#include "nsNodeInfoManager.h"
#include "nsNodeSupportsWeakRefTearoff.h"
#include "nsPIDOMWindow.h"

namespace mozilla::dom {

JSObject* DocumentFragment::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return DocumentFragment_Binding::Wrap(aCx, this, aGivenProto);
}

Element* DocumentFragment::GetElementById(const nsAString& aId) {
  if (auto* shadowRoot = ShadowRoot::FromNode(this)) {
    return shadowRoot->GetElementById(aId);
  }
  return nsINode::GetElementById(aId);
}

#ifdef MOZ_DOM_LIST
void DocumentFragment::List(FILE* out, int32_t aIndent) const {
  int32_t indent;
  for (indent = aIndent; --indent >= 0;) {
    fputs("  ", out);
  }

  fprintf(out, "DocumentFragment@%p", (void*)this);

  fprintf(out, " flags=[%08x]", static_cast<unsigned int>(GetFlags()));
  fprintf(out, " refcount=%" PRIuPTR "<", mRefCnt.get());

  nsIContent* child = GetFirstChild();
  if (child) {
    fputs("\n", out);

    for (; child; child = child->GetNextSibling()) {
      child->List(out, aIndent + 1);
    }

    for (indent = aIndent; --indent >= 0;) {
      fputs("  ", out);
    }
  }

  fputs(">\n", out);
}

void DocumentFragment::DumpContent(FILE* out, int32_t aIndent,
                                   bool aDumpAll) const {
  int32_t indent;
  for (indent = aIndent; --indent >= 0;) {
    fputs("  ", out);
  }

  fputs("<DocumentFragment>", out);

  if (aIndent) {
    fputs("\n", out);
  }

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    int32_t indent = aIndent ? aIndent + 1 : 0;
    child->DumpContent(out, indent, aDumpAll);
  }
  for (indent = aIndent; --indent >= 0;) {
    fputs("  ", out);
  }
  fputs("</DocumentFragment>", out);

  if (aIndent) {
    fputs("\n", out);
  }
}
#endif

/* static */
already_AddRefed<DocumentFragment> DocumentFragment::Constructor(
    const GlobalObject& aGlobal, ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!window || !window->GetDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return window->GetDoc()->CreateDocumentFragment();
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(DocumentFragment, FragmentOrElement, mHost)

// QueryInterface implementation for DocumentFragment
NS_INTERFACE_MAP_BEGIN(DocumentFragment)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(DocumentFragment)
  NS_INTERFACE_MAP_ENTRY(nsIContent)
  NS_INTERFACE_MAP_ENTRY(nsINode)
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::EventTarget)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsISupportsWeakReference,
                                 new nsNodeSupportsWeakRefTearoff(this))
  // DOM bindings depend on the identity pointer being the
  // same as nsINode (which nsIContent inherits).
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIContent)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF_INHERITED(DocumentFragment, FragmentOrElement)
NS_IMPL_RELEASE_INHERITED(DocumentFragment, FragmentOrElement)

NS_IMPL_ELEMENT_CLONE(DocumentFragment)

}  // namespace mozilla::dom
