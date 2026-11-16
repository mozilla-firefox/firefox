/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MsaaAccessible.h"

#include "ARIAMap.h"
#include "EnumVariant.h"
#include "HyperTextAccessible-inl.h"
#include "LocalAccessible-inl.h"
#include "MsaaDocAccessible.h"
#include "MsaaRootAccessible.h"
#include "MsaaXULMenuAccessible.h"
#include "Relation.h"
#include "ServiceProvider.h"
#include "ia2AccessibleApplication.h"
#include "ia2AccessibleHypertext.h"
#include "ia2AccessibleImage.h"
#include "ia2AccessibleTable.h"
#include "ia2AccessibleTableCell.h"
#include "mozilla/PresShell.h"
#include "mozilla/a11y/AccessibleWrap.h"
#include "mozilla/a11y/Compatibility.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/dom/Element.h"
#include "nsEventMap.h"
#include "nsWinUtils.h"
#include "sdnAccessible.h"

using namespace mozilla;
using namespace mozilla::a11y;

static const VARIANT kVarChildIdSelf = {{{VT_I4}}};

// Used internally to safely get an MsaaAccessible from a COM pointer provided
// to us by a client.
static const GUID IID_MsaaAccessible = {
    /* a94aded3-1a9c-4afc-a32c-d6b5c010046b */
    0xa94aded3,
    0x1a9c,
    0x4afc,
    {0xa3, 0x2c, 0xd6, 0xb5, 0xc0, 0x10, 0x04, 0x6b}};

constinit MsaaIdGenerator MsaaAccessible::sIDGen;
ITypeInfo* MsaaAccessible::gTypeInfo = nullptr;

/* static */
MsaaAccessible* MsaaAccessible::Create(Accessible* aAcc) {
  // This should only ever be called in the parent process.
  MOZ_ASSERT(XRE_IsParentProcess());
  // The order of some of these is important! For example, when isRoot is true,
  // IsDoc will also be true, so we must check IsRoot first. IsTable/Cell and
  // IsHyperText are a similar case.
  if (aAcc->IsRoot()) {
    MOZ_ASSERT(aAcc->IsLocal());
    return new MsaaRootAccessible(aAcc);
  }
  if (aAcc->IsDoc()) {
    return new MsaaDocAccessible(aAcc);
  }
  if (aAcc->IsTable()) {
    return new ia2AccessibleTable(aAcc);
  }
  if (aAcc->IsTableCell()) {
    return new ia2AccessibleTableCell(aAcc);
  }
  if (aAcc->IsApplication()) {
    MOZ_ASSERT(aAcc->IsLocal());
    return new ia2AccessibleApplication(aAcc);
  }
  if (aAcc->IsImage()) {
    return new ia2AccessibleImage(aAcc);
  }
  if (LocalAccessible* localAcc = aAcc->AsLocal()) {
    if (nsIContent* content = localAcc->GetContent()) {
      if (content->IsXULElement(nsGkAtoms::menuitem)) {
        return new MsaaXULMenuitemAccessible(aAcc);
      }
      if (content->IsXULElement(nsGkAtoms::box) ||
          content->IsXULElement(nsGkAtoms::hbox)) {
        // Bug 2069276: Exposing text interfaces for XUL boxes causes problems
        // for some clients; e.g. NVDA mouse tracking on address bar action
        // buttons. Generally, XUL is more like desktop UI anyway, which usually
        // doesn't expose text interfaces for buttons. Therefore, don't use
        // ia2AccessibleHypertext for these.
        return new MsaaAccessible(aAcc);
      }
    }
  }
  if (aAcc->IsHyperText()) {
    return new ia2AccessibleHypertext(aAcc);
  }
  return new MsaaAccessible(aAcc);
}

MsaaAccessible::MsaaAccessible(Accessible* aAcc) : mAcc(aAcc), mID(kNoID) {}

MsaaAccessible::~MsaaAccessible() {
  MOZ_ASSERT(!mAcc, "MsaaShutdown wasn't called!");
  if (mID != kNoID) {
    sIDGen.ReleaseID(WrapNotNull(this));
  }
}

void MsaaAccessible::MsaaShutdown() {
  // Accessibles can be shut down twice in some cases. If that happens,
  // MsaaShutdown will also be called twice because AccessibleWrap holds
  // the reference until its destructor is called; see the comments in
  // AccessibleWrap::Shutdown.
  if (!mAcc) {
    return;
  }

  if (mID != kNoID) {
    auto doc = MsaaDocAccessible::GetFromOwned(mAcc);
    MOZ_ASSERT(doc);
    doc->RemoveID(mID);
  }

  mAcc = nullptr;
}

int32_t MsaaAccessible::GetChildIDFor(Accessible* aAccessible) {
  // A child ID of the window is required, when we use NotifyWinEvent,
  // so that the 3rd party application can call back and get the IAccessible
  // the event occurred on.

  if (!aAccessible) {
    return 0;
  }

  auto doc = MsaaDocAccessible::GetFromOwned(aAccessible);
  if (!doc) {
    return 0;
  }

  uint32_t* id = &MsaaAccessible::GetFrom(aAccessible)->mID;
  if (*id != kNoID) return *id;

  *id = sIDGen.GetID();
  doc->AddID(*id, aAccessible);

  return *id;
}

HWND MsaaAccessible::GetHWNDFor(Accessible* aAccessible) {
  if (!aAccessible) {
    return nullptr;
  }

  LocalAccessible* localAcc = aAccessible->AsLocal();
  if (!localAcc) {
    RemoteAccessible* proxy = aAccessible->AsRemote();
    if (!proxy) {
      return nullptr;
    }

    // If window emulation is enabled, retrieve the emulated window from the
    // containing document document proxy.
    if (nsWinUtils::IsWindowEmulationStarted()) {
      DocAccessibleParent* doc = proxy->Document();
      HWND hWnd = doc->GetEmulatedWindowHandle();
      if (hWnd) {
        return hWnd;
      }
    }

    // Accessibles in child processes are said to have the HWND of the window
    // their tab is within.  Popups are always in the parent process, and so
    // never proxied, which means this is basically correct.
    LocalAccessible* outerDoc = proxy->OuterDocOfRemoteBrowser();
    if (!outerDoc) {
      // In some cases, the outer document accessible may be unattached from its
      // document at this point, if it is scheduled for removal. Do not assert
      // in such case. An example: putting aria-hidden="true" on HTML:iframe
      // element will destroy iframe's document asynchroniously, but
      // the document may be a target of selection events until then, and thus
      // it may attempt to deliever these events to MSAA clients.
      return nullptr;
    }

    return GetHWNDFor(outerDoc);
  }

  DocAccessible* document = localAcc->Document();
  if (!document) return nullptr;

  // Popup lives in own windows, use its HWND until the popup window is
  // hidden to make old JAWS versions work with collapsed comboboxes (see
  // discussion in bug 379678).
  if (nsIFrame* frame = localAcc->GetFrame()) {
    nsIWidget* widget = frame->GetNearestWidget();
    if (widget && widget->IsVisible()) {
      nsCOMPtr<nsIWidget> rootWidget =
          document->PresShellPtr()->GetRootWidget();
      // Make sure the accessible belongs to popup. If not then use
      // document HWND (which might be different from root widget in the
      // case of window emulation).
      if (rootWidget != widget) {
        return static_cast<HWND>(widget->GetNativeData(NS_NATIVE_WINDOW));
      }
    }
  }

  return static_cast<HWND>(document->GetNativeWindow());
}

void MsaaAccessible::FireWinEvent(Accessible* aTarget, uint32_t aEventType) {
  MOZ_ASSERT(XRE_IsParentProcess());
  static_assert(sizeof(gWinEventMap) / sizeof(gWinEventMap[0]) ==
                    nsIAccessibleEvent::EVENT_LAST_ENTRY,
                "MSAA event map skewed");

  if (aEventType == 0 || aEventType >= std::size(gWinEventMap)) {
    MOZ_ASSERT_UNREACHABLE("invalid event type");
    return;
  }

  uint32_t winEvent = gWinEventMap[aEventType];
  if (!winEvent) return;

  int32_t childID = MsaaAccessible::GetChildIDFor(aTarget);
  if (!childID) return;  // Can't fire an event without a child ID

  HWND hwnd = GetHWNDFor(aTarget);
  if (!hwnd) {
    return;
  }

  // Fire MSAA event for client area window.
  ::NotifyWinEvent(winEvent, hwnd, OBJID_CLIENT, childID);
}

AccessibleWrap* MsaaAccessible::LocalAcc() {
  if (!mAcc || mAcc->IsRemote()) {
    return nullptr;
  }
  auto acc = static_cast<AccessibleWrap*>(mAcc);
  MOZ_ASSERT(!acc || !acc->IsDefunct(),
             "mAcc defunct but MsaaShutdown wasn't called");
  return acc;
}

/**
 * This function is a helper for implementing IAccessible methods that accept
 * a Child ID as a parameter. If the child ID is CHILDID_SELF, the function
 * returns S_OK but a null *aOutInterface. Otherwise, *aOutInterface points
 * to the resolved IAccessible.
 *
 * The CHILDID_SELF case is special because in that case we actually execute
 * the implementation of the IAccessible method, whereas in the non-self case,
 * we delegate the method call to that object for execution.
 *
 * A sample invocation of this would look like:
 *
 *  RefPtr<IAccessible> accessible;
 *  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
 *  if (FAILED(hr)) {
 *    return hr;
 *  }
 *
 *  if (accessible) {
 *    return accessible->get_accFoo(kVarChildIdSelf, pszName);
 *  }
 *
 *  // Implementation for CHILDID_SELF case goes here
 */
HRESULT
MsaaAccessible::ResolveChild(const VARIANT& aVarChild,
                             IAccessible** aOutInterface) {
  MOZ_ASSERT(aOutInterface);
  *aOutInterface = nullptr;

  if (aVarChild.vt != VT_I4) {
    return E_INVALIDARG;
  }

  if (!mAcc) {
    return CO_E_OBJNOTCONNECTED;
  }

  if (aVarChild.lVal == CHILDID_SELF) {
    return S_OK;
  }

  bool isDefunct = false;
  RefPtr<IAccessible> accessible = GetIAccessibleFor(aVarChild, &isDefunct);
  if (!accessible) {
    return E_INVALIDARG;
  }

  if (isDefunct) {
    return CO_E_OBJNOTCONNECTED;
  }

  accessible.forget(aOutInterface);
  return S_OK;
}

static Accessible* GetAccessibleInSubtree(DocAccessible* aDoc, uint32_t aID) {
  Accessible* child = MsaaDocAccessible::GetFrom(aDoc)->GetAccessibleByID(aID);
  if (child) return child;

  uint32_t childDocCount = aDoc->ChildDocumentCount();
  for (uint32_t i = 0; i < childDocCount; i++) {
    child = GetAccessibleInSubtree(aDoc->GetChildDocumentAt(i), aID);
    if (child) return child;
  }

  return nullptr;
}

static Accessible* GetAccessibleInSubtree(DocAccessibleParent* aDoc,
                                          uint32_t aID) {
  Accessible* child = MsaaDocAccessible::GetFrom(aDoc)->GetAccessibleByID(aID);
  if (child) {
    return child;
  }

  size_t childDocCount = aDoc->ChildDocCount();
  for (size_t i = 0; i < childDocCount; i++) {
    child = GetAccessibleInSubtree(aDoc->ChildDocAt(i), aID);
    if (child) {
      return child;
    }
  }

  return nullptr;
}

static bool IsInclusiveDescendantOf(DocAccessible* aAncestor,
                                    DocAccessible* aDescendant) {
  for (DocAccessible* doc = aDescendant; doc; doc = doc->ParentDocument()) {
    if (doc == aAncestor) {
      return true;
    }
  }
  return false;
}

already_AddRefed<IAccessible> MsaaAccessible::GetIAccessibleFor(
    const VARIANT& aVarChild, bool* aIsDefunct) {
  if (aVarChild.vt != VT_I4) return nullptr;

  VARIANT varChild = aVarChild;

  MOZ_ASSERT(aIsDefunct);
  *aIsDefunct = false;

  RefPtr<IAccessible> result;

  if (!mAcc) {
    *aIsDefunct = true;
    return nullptr;
  }

  if (varChild.lVal == CHILDID_SELF) {
    result = this;
    return result.forget();
  }

  if (varChild.ulVal != GetExistingID() && nsAccUtils::MustPrune(mAcc)) {
    // This accessible should have no subtree in platform, return null for its
    // children.
    return nullptr;
  }

  if (varChild.lVal > 0) {
    // Gecko child indices are 0-based in contrast to indices used in MSAA.
    Accessible* xpAcc = mAcc->ChildAt(varChild.lVal - 1);
    if (!xpAcc) {
      return nullptr;
    }
    MOZ_ASSERT(xpAcc->IsRemote() || !xpAcc->AsLocal()->IsDefunct(),
               "Shouldn't get a defunct child");
    result = MsaaAccessible::GetFrom(xpAcc);
    return result.forget();
  }

  // If lVal negative then it is treated as child ID and we should look for
  // accessible through whole accessible subtree including subdocuments.
  Accessible* doc = nullptr;
  Accessible* child = nullptr;
  auto id = static_cast<uint32_t>(varChild.lVal);
  if (LocalAccessible* localAcc = mAcc->AsLocal()) {
    DocAccessible* localDoc = localAcc->Document();
    doc = localDoc;
    child = GetAccessibleInSubtree(localDoc, id);
    if (!child) {
      // Search remote documents which are descendants of this local document.
      const auto remoteDocs = DocManager::TopLevelRemoteDocs();
      if (!remoteDocs) {
        return nullptr;
      }
      for (DocAccessibleParent* remoteDoc : *remoteDocs) {
        if (remoteDoc->IsPrintDoc()) {
          // Print documents aren't exposed to platform accessibility APIs.
          continue;
        }
        LocalAccessible* outerDoc = remoteDoc->OuterDocOfRemoteBrowser();
        if (!outerDoc ||
            !IsInclusiveDescendantOf(localDoc, outerDoc->Document())) {
          continue;
        }
        child = GetAccessibleInSubtree(remoteDoc, id);
        if (child) {
          break;
        }
      }
    }
  } else {
    DocAccessibleParent* remoteDoc = mAcc->AsRemote()->Document();
    doc = remoteDoc;
    child = GetAccessibleInSubtree(remoteDoc, id);
  }
  if (!child) {
    return nullptr;
  }

  MOZ_ASSERT(child->IsRemote() || !child->AsLocal()->IsDefunct(),
             "Shouldn't get a defunct child");
  // If this method is being called on the document we searched, we can just
  // return child.
  if (mAcc == doc) {
    result = MsaaAccessible::GetFrom(child);
    return result.forget();
  }

  // Otherwise, this method was called on a descendant, so we searched an
  // ancestor. We must check whether child is really a descendant. This is used
  // for ARIA documents and popups.
  Accessible* parent = child;
  while (parent && parent != doc) {
    if (parent == mAcc) {
      result = MsaaAccessible::GetFrom(child);
      return result.forget();
    }

    parent = parent->Parent();
  }

  return nullptr;
}

IDispatch* MsaaAccessible::NativeAccessible(Accessible* aAccessible) {
  if (!aAccessible) {
    NS_WARNING("Not passing in an aAccessible");
    return nullptr;
  }

  RefPtr<IDispatch> disp;
  disp = MsaaAccessible::GetFrom(aAccessible);
  IDispatch* rawDisp;
  disp.forget(&rawDisp);
  return rawDisp;
}

ITypeInfo* MsaaAccessible::GetTI(LCID lcid) {
  if (gTypeInfo) return gTypeInfo;

  ITypeLib* typeLib = nullptr;
  HRESULT hr = LoadRegTypeLib(LIBID_Accessibility, 1, 0, lcid, &typeLib);
  if (FAILED(hr)) return nullptr;

  hr = typeLib->GetTypeInfoOfGuid(IID_IAccessible, &gTypeInfo);
  typeLib->Release();

  if (FAILED(hr)) return nullptr;

  return gTypeInfo;
}

/* static */
MsaaAccessible* MsaaAccessible::GetFrom(Accessible* aAcc) {
  if (!aAcc) {
    return nullptr;
  }

  if (RemoteAccessible* remoteAcc = aAcc->AsRemote()) {
    return reinterpret_cast<MsaaAccessible*>(remoteAcc->GetWrapper());
  }
  return static_cast<AccessibleWrap*>(aAcc)->GetMsaa();
}

/* static */
Accessible* MsaaAccessible::GetAccessibleFrom(IUnknown* aUnknown) {
  RefPtr<MsaaAccessible> msaa;
  aUnknown->QueryInterface(IID_MsaaAccessible, getter_AddRefs(msaa));
  if (!msaa) {
    return nullptr;
  }
  return msaa->Acc();
}

void MsaaAccessible::GetInnerHTML(nsAString& aInnerHTML) {
  if (!mAcc) {
    return;
  }
  if (RemoteAccessible* remoteAcc = mAcc->AsRemote()) {
    if (remoteAcc->Document()->RequestDomainsIfInactive(
            CacheDomain::InnerHTML)) {
      return;
    }
    if (!remoteAcc->mCachedFields) {
      return;
    }
    remoteAcc->mCachedFields->GetAttribute(CacheKey::InnerHTML, aInnerHTML);
  } else if (dom::Element* el = mAcc->AsLocal()->Elm()) {
    el->GetInnerHTML(aInnerHTML, IgnoreErrors());
  }
}

// IUnknown methods
STDMETHODIMP
MsaaAccessible::QueryInterface(REFIID iid, void** ppv) {
  if (!ppv) return E_INVALIDARG;

  *ppv = nullptr;

  if (IID_IClientSecurity == iid) {
    // Some code might QI(IID_IClientSecurity) to detect whether or not we are
    // a proxy. Right now that can potentially happen off the main thread, so we
    // look for this condition immediately so that we don't trigger other code
    // that might not be thread-safe.
    return E_NOINTERFACE;
  }

  if (NS_WARN_IF(!NS_IsMainThread())) {
    // Bug 1896816: JAWS sometimes traverses into Gecko UI from a file dialog
    // thread. It shouldn't do that, but let's fail gracefully instead of
    // crashing.
    return RPC_E_WRONG_THREAD;
  }

  // These interfaces are always available. We can support querying to them
  // even if the Accessible is dead.
  if (IID_IUnknown == iid) {
    *ppv = static_cast<IAccessible*>(this);
  } else if (IID_MsaaAccessible == iid) {
    *ppv = static_cast<MsaaAccessible*>(this);
  } else if (IID_IDispatch == iid || IID_IAccessible == iid) {
    *ppv = static_cast<IAccessible*>(this);
  } else if (IID_IServiceProvider == iid) {
    *ppv = new ServiceProvider(this);
  } else {
    HRESULT hr = ia2Accessible::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr)) {
      return hr;
    }
    if (Compatibility::IsUiaEnabled()) {
      hr = uiaRawElmProvider::QueryInterface(iid, ppv);
      if (SUCCEEDED(hr)) {
        return hr;
      }
    }
  }
  if (*ppv) {
    (reinterpret_cast<IUnknown*>(*ppv))->AddRef();
    return S_OK;
  }

  // For interfaces below this point, we have to query the Accessible to
  // determine if they are available.
  if (!mAcc) {
    // Some callers expect either S_OK or E_NOINTERFACE, so don't return
    // CO_E_OBJNOTCONNECTED like we normally would for a dead object.
    return E_NOINTERFACE;
  }
  AccessibleWrap* localAcc = LocalAcc();
  if (IID_IEnumVARIANT == iid) {
    // We don't support this interface for leaf elements.
    if (!mAcc->HasChildren() || nsAccUtils::MustPrune(mAcc)) {
      return E_NOINTERFACE;
    }
    *ppv = static_cast<IEnumVARIANT*>(new ChildrenEnumVariant(this));
  } else if (IID_ISimpleDOMNode == iid) {
    if (mAcc->IsDoc() || (localAcc && !localAcc->HasOwnContent())) {
      return E_NOINTERFACE;
    }

    *ppv = static_cast<ISimpleDOMNode*>(new sdnAccessible(WrapNotNull(this)));
  }

  if (!*ppv && localAcc) {
    HRESULT hr = ia2AccessibleComponent::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr)) return hr;
  }

  if (!*ppv) {
    HRESULT hr = ia2AccessibleHyperlink::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr)) return hr;
  }

  if (!*ppv) {
    HRESULT hr = ia2AccessibleValue::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr)) return hr;
  }

  if (nullptr == *ppv) return E_NOINTERFACE;

  (reinterpret_cast<IUnknown*>(*ppv))->AddRef();
  return S_OK;
}

// IAccessible methods

STDMETHODIMP
MsaaAccessible::get_accParent(IDispatch __RPC_FAR * __RPC_FAR * ppdispParent) {
  if (!ppdispParent) return E_INVALIDARG;

  *ppdispParent = nullptr;

  if (!mAcc) {
    return CO_E_OBJNOTCONNECTED;
  }

  Accessible* xpParentAcc = mAcc->Parent();
  if (!xpParentAcc) return S_FALSE;

  *ppdispParent = NativeAccessible(xpParentAcc);
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accChildCount(long __RPC_FAR* pcountChildren) {
  if (!pcountChildren) return E_INVALIDARG;

  *pcountChildren = 0;

  if (!mAcc) return CO_E_OBJNOTCONNECTED;

  if ((Compatibility::A11ySuppressionReasons() &
       SuppressionReasons::Clipboard) &&
      mAcc->IsDoc()) {
    // Bug 1798098: Windows Suggested Actions (introduced in Windows 11 22H2)
    // might walk the entire a11y tree using UIA whenever anything is copied to
    // the clipboard. This causes an unacceptable hang. We prevent this tree
    // walk by returning a 0 child count for documents, from which Windows might
    // walk.
    return S_OK;
  }

  if (nsAccUtils::MustPrune(mAcc)) {
    return S_OK;
  }

  *pcountChildren = mAcc->ChildCount();
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accChild(
    /* [in] */ VARIANT varChild,
    /* [retval][out] */ IDispatch __RPC_FAR * __RPC_FAR * ppdispChild) {
  if (!ppdispChild) return E_INVALIDARG;

  *ppdispChild = nullptr;
  if (!mAcc) return CO_E_OBJNOTCONNECTED;

  // IAccessible::accChild is used to return this accessible or child accessible
  // at the given index or to get an accessible by child ID in the case of
  // document accessible.
  // The getting an accessible by child ID is used by
  // AccessibleObjectFromEvent() called by AT when AT handles our MSAA event.
  bool isDefunct = false;
  RefPtr<IAccessible> child = GetIAccessibleFor(varChild, &isDefunct);
  if (!child) {
    return E_INVALIDARG;
  }

  if (isDefunct) {
    return CO_E_OBJNOTCONNECTED;
  }

  child.forget(ppdispChild);
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accName(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ BSTR __RPC_FAR* pszName) {
  if (!pszName || varChild.vt != VT_I4) return E_INVALIDARG;

  *pszName = nullptr;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accName(kVarChildIdSelf, pszName);
  }

  nsAutoString name;
  Acc()->Name(name);

  if (name.IsVoid()) return S_FALSE;

  *pszName = ::SysAllocStringLen(name.get(), name.Length());
  if (!*pszName) return E_OUTOFMEMORY;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accValue(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ BSTR __RPC_FAR* pszValue) {
  if (!pszValue) return E_INVALIDARG;

  *pszValue = nullptr;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accValue(kVarChildIdSelf, pszValue);
  }

  nsAutoString value;
  Acc()->Value(value);

  // See bug 438784: need to expose URL on doc's value attribute. For this,
  // reverting part of fix for bug 425693 to make this MSAA method behave
  // IAccessible2-style.
  if (value.IsEmpty()) return S_FALSE;

  *pszValue = ::SysAllocStringLen(value.get(), value.Length());
  if (!*pszValue) return E_OUTOFMEMORY;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accDescription(VARIANT varChild,
                                   BSTR __RPC_FAR* pszDescription) {
  if (!pszDescription) return E_INVALIDARG;

  *pszDescription = nullptr;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accDescription(kVarChildIdSelf, pszDescription);
  }

  nsAutoString description;
  Acc()->Description(description);

  *pszDescription =
      ::SysAllocStringLen(description.get(), description.Length());
  return *pszDescription ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP
MsaaAccessible::get_accRole(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ VARIANT __RPC_FAR* pvarRole) {
  if (!pvarRole) return E_INVALIDARG;

  VariantInit(pvarRole);

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accRole(kVarChildIdSelf, pvarRole);
  }

  a11y::role geckoRole;
#ifdef DEBUG
  if (mAcc->IsLocal()) {
    NS_ASSERTION(nsAccUtils::IsTextInterfaceSupportCorrect(mAcc->AsLocal()),
                 "Does not support Text when it should");
  }
#endif
  geckoRole = mAcc->Role();

  uint32_t msaaRole = 0;

#define ROLE(_geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             _msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType, \
             nameRule)                                                       \
  case roles::_geckoRole:                                                    \
    msaaRole = _msaaRole;                                                    \
    break;

  switch (geckoRole) {
#include "RoleMap.inc"
    default:
      MOZ_CRASH("Unknown role.");
  }

#undef ROLE

  // Special case, if there is a ROLE_ROW inside of a ROLE_TREE_TABLE, then call
  // the MSAA role a ROLE_OUTLINEITEM for consistency and compatibility. We need
  // this because ARIA has a role of "row" for both grid and treegrid
  if (geckoRole == roles::ROW) {
    Accessible* xpParent = mAcc->Parent();
    if (xpParent && xpParent->Role() == roles::TREE_TABLE)
      msaaRole = ROLE_SYSTEM_OUTLINEITEM;
  }

  pvarRole->vt = VT_I4;
  pvarRole->lVal = msaaRole;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accState(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ VARIANT __RPC_FAR* pvarState) {
  if (!pvarState) return E_INVALIDARG;

  VariantInit(pvarState);
  pvarState->vt = VT_I4;
  pvarState->lVal = 0;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accState(kVarChildIdSelf, pvarState);
  }

  // MSAA only has 31 states and the lowest 31 bits of our state bit mask
  // are the same states as MSAA.
  // Note: we map the following Gecko states to different MSAA states:
  //   REQUIRED -> ALERT_LOW
  //   ALERT -> ALERT_MEDIUM
  //   INVALID -> ALERT_HIGH
  //   CHECKABLE -> MARQUEED

  uint64_t state = Acc()->State();

  uint32_t msaaState = 0;
  nsAccUtils::To32States(state, &msaaState, nullptr);
  pvarState->lVal = msaaState;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accHelp(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ BSTR __RPC_FAR* pszHelp) {
  if (!pszHelp) return E_INVALIDARG;

  *pszHelp = nullptr;
  return S_FALSE;
}

STDMETHODIMP
MsaaAccessible::get_accHelpTopic(
    /* [out] */ BSTR __RPC_FAR* pszHelpFile,
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ long __RPC_FAR* pidTopic) {
  if (!pszHelpFile || !pidTopic) return E_INVALIDARG;

  *pszHelpFile = nullptr;
  *pidTopic = 0;
  return S_FALSE;
}

STDMETHODIMP
MsaaAccessible::get_accKeyboardShortcut(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ BSTR __RPC_FAR* pszKeyboardShortcut) {
  if (!pszKeyboardShortcut) return E_INVALIDARG;
  *pszKeyboardShortcut = nullptr;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accKeyboardShortcut(kVarChildIdSelf,
                                               pszKeyboardShortcut);
  }

  nsAutoString shortcut;

  if (!mAcc->GetStringARIAAttr(nsGkAtoms::aria_keyshortcuts, shortcut)) {
    KeyBinding keyBinding = mAcc->AccessKey();
    if (keyBinding.IsEmpty()) {
      if (LocalAccessible* localAcc = mAcc->AsLocal()) {
        keyBinding = localAcc->KeyboardShortcut();
      }
    }

    keyBinding.ToString(shortcut);
  }

  *pszKeyboardShortcut = ::SysAllocStringLen(shortcut.get(), shortcut.Length());
  return *pszKeyboardShortcut ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP
MsaaAccessible::get_accFocus(
    /* [retval][out] */ VARIANT __RPC_FAR* pvarChild) {
  if (!pvarChild) return E_INVALIDARG;

  VariantInit(pvarChild);

  // clang-format off
  // VT_EMPTY:    None. This object does not have the keyboard focus itself
  //              and does not contain a child that has the keyboard focus.
  // VT_I4:       lVal is CHILDID_SELF. The object itself has the keyboard focus.
  // VT_I4:       lVal contains the child ID of the child element with the keyboard focus.
  // VT_DISPATCH: pdispVal member is the address of the IDispatch interface
  //              for the child object with the keyboard focus.
  // clang-format on
  if (!mAcc) {
    return CO_E_OBJNOTCONNECTED;
  }
  // Return the current IAccessible child that has focus
  Accessible* focusedAccessible = mAcc->FocusedChild();
  if (focusedAccessible == mAcc) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = CHILDID_SELF;
  } else if (focusedAccessible) {
    pvarChild->vt = VT_DISPATCH;
    pvarChild->pdispVal = NativeAccessible(focusedAccessible);
  } else {
    pvarChild->vt = VT_EMPTY;  // No focus or focus is not a child
  }

  return S_OK;
}

/**
 * This helper class implements IEnumVARIANT for a nsTArray containing
 * accessible objects.
 */
class AccessibleEnumerator final : public IEnumVARIANT {
 public:
  explicit AccessibleEnumerator(const nsTArray<Accessible*>& aArray)
      : mCurIndex(0) {
    mArray.SetCapacity(aArray.Length());
    for (Accessible* acc : aArray) {
      mArray.AppendElement(MsaaAccessible::GetFrom(acc));
    }
  }

  AccessibleEnumerator(const AccessibleEnumerator& toCopy)
      : mArray(toCopy.mArray.Clone()), mCurIndex(toCopy.mCurIndex) {}
  ~AccessibleEnumerator() = default;

  // IUnknown
  DECL_IUNKNOWN

  // IEnumVARIANT
  STDMETHODIMP Next(unsigned long celt, VARIANT FAR* rgvar,
                    unsigned long FAR* pceltFetched);
  STDMETHODIMP Skip(unsigned long celt);
  STDMETHODIMP Reset() {
    mCurIndex = 0;
    return S_OK;
  }
  STDMETHODIMP Clone(IEnumVARIANT FAR * FAR * ppenum);

 private:
  nsTArray<RefPtr<MsaaAccessible>> mArray;
  uint32_t mCurIndex;
};

STDMETHODIMP
AccessibleEnumerator::QueryInterface(REFIID iid, void** ppvObject) {
  if (iid == IID_IEnumVARIANT) {
    *ppvObject = static_cast<IEnumVARIANT*>(this);
    AddRef();
    return S_OK;
  }
  if (iid == IID_IUnknown) {
    *ppvObject = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }

  *ppvObject = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP
AccessibleEnumerator::Next(unsigned long celt, VARIANT FAR* rgvar,
                           unsigned long FAR* pceltFetched) {
  uint32_t length = mArray.Length();
  HRESULT hr = S_OK;

  // Can't get more elements than there are...
  if (celt > length - mCurIndex) {
    hr = S_FALSE;
    celt = length - mCurIndex;
  }

  // Copy the elements of the array into rgvar.
  for (uint32_t i = 0; i < celt; ++i, ++mCurIndex) {
    RefPtr<IDispatch> disp = mArray[mCurIndex];
    rgvar[i].vt = VT_DISPATCH;
    disp.forget(&rgvar[i].pdispVal);
  }

  if (pceltFetched) *pceltFetched = celt;

  return hr;
}

STDMETHODIMP
AccessibleEnumerator::Clone(IEnumVARIANT FAR * FAR * ppenum) {
  auto newEnum = MakeRefPtr<AccessibleEnumerator>(*this);
  newEnum.forget(ppenum);
  return S_OK;
}

STDMETHODIMP
AccessibleEnumerator::Skip(unsigned long celt) {
  uint32_t length = mArray.Length();
  // Check if we can skip the requested number of elements
  if (celt > length - mCurIndex) {
    mCurIndex = length;
    return S_FALSE;
  }
  mCurIndex += celt;
  return S_OK;
}

/**
 * This method is called when a client wants to know which children of a node
 *  are selected. Note that this method can only find selected children for
 *  accessible object which implement SelectAccessible.
 *
 * The VARIANT return value arguement is expected to either contain a single
 * IAccessible or an IEnumVARIANT of IAccessibles. We return the IEnumVARIANT
 * regardless of the number of children selected, unless there are none selected
 * in which case we return an empty VARIANT.
 *
 * We get the selected options from the select's accessible object and wrap
 *  those in an AccessibleEnumerator which we then put in the return VARIANT.
 *
 * returns a VT_EMPTY VARIANT if:
 *  - there are no selected children for this object
 *  - the object is not the type that can have children selected
 */
STDMETHODIMP
MsaaAccessible::get_accSelection(VARIANT __RPC_FAR* pvarChildren) {
  if (!pvarChildren) return E_INVALIDARG;

  VariantInit(pvarChildren);
  pvarChildren->vt = VT_EMPTY;

  if (!mAcc) {
    return CO_E_OBJNOTCONNECTED;
  }
  Accessible* acc = Acc();

  if (!acc->IsSelect()) {
    return S_OK;
  }

  AutoTArray<Accessible*, 10> selectedItems;
  acc->SelectedItems(&selectedItems);
  uint32_t count = selectedItems.Length();
  if (count == 1) {
    pvarChildren->vt = VT_DISPATCH;
    pvarChildren->pdispVal = NativeAccessible(selectedItems[0]);
  } else if (count > 1) {
    auto pEnum = MakeRefPtr<AccessibleEnumerator>(selectedItems);
    pvarChildren->vt =
        VT_UNKNOWN;  // this must be VT_UNKNOWN for an IEnumVARIANT
    NS_ADDREF(pvarChildren->punkVal = pEnum);
  }
  // If count == 0, vt is already VT_EMPTY, so there's nothing else to do.

  return S_OK;
}

STDMETHODIMP
MsaaAccessible::get_accDefaultAction(
    /* [optional][in] */ VARIANT varChild,
    /* [retval][out] */ BSTR __RPC_FAR* pszDefaultAction) {
  if (!pszDefaultAction) return E_INVALIDARG;

  *pszDefaultAction = nullptr;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->get_accDefaultAction(kVarChildIdSelf, pszDefaultAction);
  }

  nsAutoString defaultAction;
  mAcc->ActionNameAt(0, defaultAction);

  *pszDefaultAction =
      ::SysAllocStringLen(defaultAction.get(), defaultAction.Length());
  return *pszDefaultAction ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP
MsaaAccessible::accSelect(
    /* [in] */ long flagsSelect,
    /* [optional][in] */ VARIANT varChild) {
  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->accSelect(flagsSelect, kVarChildIdSelf);
  }

  if (flagsSelect & SELFLAG_TAKEFOCUS) {
    mAcc->TakeFocus();
    return S_OK;
  }

  if (flagsSelect & SELFLAG_TAKESELECTION) {
    mAcc->TakeSelection();
    return S_OK;
  }

  if (flagsSelect & SELFLAG_ADDSELECTION) {
    mAcc->SetSelected(true);
    return S_OK;
  }

  if (flagsSelect & SELFLAG_REMOVESELECTION) {
    mAcc->SetSelected(false);
    return S_OK;
  }

  return E_FAIL;
}

STDMETHODIMP
MsaaAccessible::accLocation(
    /* [out] */ long __RPC_FAR* pxLeft,
    /* [out] */ long __RPC_FAR* pyTop,
    /* [out] */ long __RPC_FAR* pcxWidth,
    /* [out] */ long __RPC_FAR* pcyHeight,
    /* [optional][in] */ VARIANT varChild) {
  if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) return E_INVALIDARG;

  *pxLeft = 0;
  *pyTop = 0;
  *pcxWidth = 0;
  *pcyHeight = 0;

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->accLocation(pxLeft, pyTop, pcxWidth, pcyHeight,
                                   kVarChildIdSelf);
  }

  LayoutDeviceIntRect rect = Acc()->Bounds();
  *pxLeft = rect.X();
  *pyTop = rect.Y();
  *pcxWidth = rect.Width();
  *pcyHeight = rect.Height();
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::accNavigate(
    /* [in] */ long navDir,
    /* [optional][in] */ VARIANT varStart,
    /* [retval][out] */ VARIANT __RPC_FAR* pvarEndUpAt) {
  if (!pvarEndUpAt) return E_INVALIDARG;

  VariantInit(pvarEndUpAt);

  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varStart, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->accNavigate(navDir, kVarChildIdSelf, pvarEndUpAt);
  }

  Accessible* navAccessible = nullptr;
  Maybe<RelationType> xpRelation;

#define RELATIONTYPE(geckoType, stringType, atkType, msaaType, ia2Type) \
  case msaaType:                                                        \
    xpRelation.emplace(RelationType::geckoType);                        \
    break;

  switch (navDir) {
    case NAVDIR_FIRSTCHILD:
      if (!nsAccUtils::MustPrune(mAcc)) {
        navAccessible = mAcc->FirstChild();
      }
      break;
    case NAVDIR_LASTCHILD:
      if (!nsAccUtils::MustPrune(mAcc)) {
        navAccessible = mAcc->LastChild();
      }
      break;
    case NAVDIR_NEXT:
      navAccessible = mAcc->NextSibling();
      break;
    case NAVDIR_PREVIOUS:
      navAccessible = mAcc->PrevSibling();
      break;
    case NAVDIR_DOWN:
    case NAVDIR_LEFT:
    case NAVDIR_RIGHT:
    case NAVDIR_UP:
      return E_NOTIMPL;

      // MSAA relationship extensions to accNavigate
#include "RelationTypeMap.inc"

    default:
      return E_INVALIDARG;
  }

#undef RELATIONTYPE

  pvarEndUpAt->vt = VT_EMPTY;

  if (xpRelation) {
    Relation rel = mAcc->RelationByType(*xpRelation);
    navAccessible = rel.Next();
  }

  if (!navAccessible) return E_FAIL;

  pvarEndUpAt->pdispVal = NativeAccessible(navAccessible);
  pvarEndUpAt->vt = VT_DISPATCH;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::accHitTest(
    /* [in] */ long xLeft,
    /* [in] */ long yTop,
    /* [retval][out] */ VARIANT __RPC_FAR* pvarChild) {
  if (!pvarChild) return E_INVALIDARG;

  VariantInit(pvarChild);

  if (!mAcc) {
    return CO_E_OBJNOTCONNECTED;
  }

  // The MSAA documentation says accHitTest should return a child. However,
  // clients call AccessibleObjectFromPoint, which ends up walking the
  // descendants calling accHitTest on each one. Since clients want the
  // deepest descendant anyway, it's faster and probably more accurate to
  // just do this ourselves.
  Accessible* accessible = mAcc->ChildAtPoint(
      xLeft, yTop, Accessible::EWhichChildAtPoint::DeepestChild);

  // if we got a child
  if (accessible) {
    if (accessible == mAcc) {
      pvarChild->vt = VT_I4;
      pvarChild->lVal = CHILDID_SELF;
    } else {
      pvarChild->vt = VT_DISPATCH;
      pvarChild->pdispVal = NativeAccessible(accessible);
    }
  } else {
    // no child at that point
    pvarChild->vt = VT_EMPTY;
    return S_FALSE;
  }
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::accDoDefaultAction(
    /* [optional][in] */ VARIANT varChild) {
  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->accDoDefaultAction(kVarChildIdSelf);
  }

  return mAcc->DoAction(0) ? S_OK : E_INVALIDARG;
}

STDMETHODIMP
MsaaAccessible::put_accName(
    /* [optional][in] */ VARIANT varChild,
    /* [in] */ BSTR szName) {
  return E_NOTIMPL;
}

STDMETHODIMP
MsaaAccessible::put_accValue(
    /* [optional][in] */ VARIANT varChild,
    /* [in] */ BSTR szValue) {
  RefPtr<IAccessible> accessible;
  HRESULT hr = ResolveChild(varChild, getter_AddRefs(accessible));
  if (FAILED(hr)) {
    return hr;
  }

  if (accessible) {
    return accessible->put_accValue(kVarChildIdSelf, szValue);
  }

  HyperTextAccessibleBase* ht = mAcc->AsHyperTextBase();
  if (!ht) {
    return E_NOTIMPL;
  }

  uint32_t length = ::SysStringLen(szValue);
  nsAutoString text(szValue, length);
  ht->ReplaceText(text);
  return S_OK;
}

// IDispatch methods

STDMETHODIMP
MsaaAccessible::GetTypeInfoCount(UINT* pctinfo) {
  if (!pctinfo) return E_INVALIDARG;

  *pctinfo = 1;
  return S_OK;
}

STDMETHODIMP
MsaaAccessible::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) {
  if (!ppTInfo) return E_INVALIDARG;

  *ppTInfo = nullptr;

  if (iTInfo != 0) return DISP_E_BADINDEX;

  ITypeInfo* typeInfo = GetTI(lcid);
  if (!typeInfo) return E_FAIL;

  typeInfo->AddRef();
  *ppTInfo = typeInfo;

  return S_OK;
}

STDMETHODIMP
MsaaAccessible::GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames,
                              LCID lcid, DISPID* rgDispId) {
  ITypeInfo* typeInfo = GetTI(lcid);
  if (!typeInfo) return E_FAIL;

  HRESULT hr = DispGetIDsOfNames(typeInfo, rgszNames, cNames, rgDispId);
  return hr;
}

STDMETHODIMP
MsaaAccessible::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                       DISPPARAMS* pDispParams, VARIANT* pVarResult,
                       EXCEPINFO* pExcepInfo, UINT* puArgErr) {
  ITypeInfo* typeInfo = GetTI(lcid);
  if (!typeInfo) return E_FAIL;

  return typeInfo->Invoke(static_cast<IAccessible*>(this), dispIdMember, wFlags,
                          pDispParams, pVarResult, pExcepInfo, puArgErr);
}
