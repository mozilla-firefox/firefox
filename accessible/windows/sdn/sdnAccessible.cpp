/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sdnAccessible.h"

#include "ISimpleDOM_i.c"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/a11y/RemoteAccessible.h"
#include "mozilla/dom/Element.h"

using namespace mozilla;
using namespace mozilla::a11y;

sdnAccessible::~sdnAccessible() = default;

IMPL_IUNKNOWN_QUERY_HEAD(sdnAccessible)
IMPL_IUNKNOWN_QUERY_IFACE(ISimpleDOMNode)
IMPL_IUNKNOWN_QUERY_TAIL_AGGREGATED(mMsaa)

STDMETHODIMP
sdnAccessible::get_nodeInfo(BSTR __RPC_FAR* aNodeName,
                            short __RPC_FAR* aNameSpaceID,
                            BSTR __RPC_FAR* aNodeValue,
                            unsigned int __RPC_FAR* aNumChildren,
                            unsigned int __RPC_FAR* aUniqueID,
                            unsigned short __RPC_FAR* aNodeType) {
  if (!aNodeName || !aNameSpaceID || !aNodeValue || !aNumChildren ||
      !aUniqueID || !aNodeType)
    return E_INVALIDARG;

  *aNodeName = nullptr;
  *aNameSpaceID = 0;
  *aNodeValue = nullptr;
  *aNumChildren = 0;
  *aUniqueID = 0;
  *aNodeType = 0;

  Accessible* acc = mMsaa->Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }

  // This is a unique ID for every content node. The 3rd party accessibility
  // application can compare this to the childID we return for events such as
  // focus events, to correlate back to data nodes in their internal object
  // model.
  *aUniqueID = MsaaAccessible::GetChildIDFor(acc);
  if (acc->IsText()) {
    *aNodeType = nsINode::TEXT_NODE;
  } else if (acc->IsDoc()) {
    *aNodeType = nsINode::DOCUMENT_NODE;
  } else {
    *aNodeType = nsINode::ELEMENT_NODE;
  }
  if (nsAtom* tag = acc->TagName()) {
    nsAutoString nodeName;
    tag->ToString(nodeName);
    *aNodeName = ::SysAllocString(nodeName.get());
  }
  return S_OK;
}

STDMETHODIMP
sdnAccessible::get_attributes(unsigned short aMaxAttribs,
                              BSTR __RPC_FAR* aAttribNames,
                              short __RPC_FAR* aNameSpaceIDs,
                              BSTR __RPC_FAR* aAttribValues,
                              unsigned short __RPC_FAR* aNumAttribs) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_attributesForNames(unsigned short aMaxAttribs,
                                      BSTR __RPC_FAR* aAttribNames,
                                      short __RPC_FAR* aNameSpaceID,
                                      BSTR __RPC_FAR* aAttribValues) {
  if (!aAttribNames || !aNameSpaceID || !aAttribValues) return E_INVALIDARG;

  if (!mMsaa->Acc()) {
    return CO_E_OBJNOTCONNECTED;
  }
  // NVDA expects this to succeed for MathML and won't call innerHTML if this
  // fails. Therefore, return S_FALSE here instead of E_NOTIMPL, indicating
  // that the attributes aren't present.
  return S_FALSE;
}

STDMETHODIMP
sdnAccessible::get_computedStyle(
    unsigned short aMaxStyleProperties, boolean aUseAlternateView,
    BSTR __RPC_FAR* aStyleProperties, BSTR __RPC_FAR* aStyleValues,
    unsigned short __RPC_FAR* aNumStyleProperties) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_computedStyleForProperties(
    unsigned short aNumStyleProperties, boolean aUseAlternateView,
    BSTR __RPC_FAR* aStyleProperties, BSTR __RPC_FAR* aStyleValues) {
  return E_NOTIMPL;
}

// XXX Use MOZ_CAN_RUN_SCRIPT_BOUNDARY for now due to bug 1543294.
MOZ_CAN_RUN_SCRIPT_BOUNDARY STDMETHODIMP
sdnAccessible::scrollTo(boolean aScrollTopLeft) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_parentNode(ISimpleDOMNode __RPC_FAR * __RPC_FAR * aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_firstChild(ISimpleDOMNode __RPC_FAR * __RPC_FAR * aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_lastChild(ISimpleDOMNode __RPC_FAR * __RPC_FAR * aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_previousSibling(ISimpleDOMNode __RPC_FAR * __RPC_FAR *
                                   aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_nextSibling(ISimpleDOMNode __RPC_FAR * __RPC_FAR * aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_childAt(unsigned aChildIndex,
                           ISimpleDOMNode __RPC_FAR * __RPC_FAR * aNode) {
  return E_NOTIMPL;
}

STDMETHODIMP
sdnAccessible::get_innerHTML(BSTR __RPC_FAR* aInnerHTML) {
  if (!aInnerHTML) return E_INVALIDARG;
  *aInnerHTML = nullptr;

  if (!mMsaa->Acc()) {
    return CO_E_OBJNOTCONNECTED;
  }

  nsAutoString innerHTML;
  mMsaa->GetInnerHTML(innerHTML);
  if (innerHTML.IsEmpty()) return S_FALSE;

  *aInnerHTML = ::SysAllocStringLen(innerHTML.get(), innerHTML.Length());
  if (!*aInnerHTML) return E_OUTOFMEMORY;

  return S_OK;
}

STDMETHODIMP
sdnAccessible::get_localInterface(void __RPC_FAR * __RPC_FAR *
                                  aLocalInterface) {
  if (!aLocalInterface) return E_INVALIDARG;
  *aLocalInterface = nullptr;

  if (!mMsaa->Acc()) {
    return CO_E_OBJNOTCONNECTED;
  }

  *aLocalInterface = this;
  AddRef();

  return S_OK;
}

STDMETHODIMP
sdnAccessible::get_language(BSTR __RPC_FAR* aLanguage) { return E_NOTIMPL; }
