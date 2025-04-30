/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uiaRawElmProvider.h"

#include <comdef.h>
#include <uiautomationcoreapi.h>

#include "AccAttributes.h"
#include "AccessibleWrap.h"
#include "ApplicationAccessible.h"
#include "ARIAMap.h"
#include "ia2AccessibleHypertext.h"
#include "ia2AccessibleTable.h"
#include "ia2AccessibleTableCell.h"
#include "LocalAccessible-inl.h"
#include "mozilla/a11y/Compatibility.h"
#include "mozilla/a11y/RemoteAccessible.h"
#include "MsaaAccessible.h"
#include "MsaaRootAccessible.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsIAccessiblePivot.h"
#include "nsTextEquivUtils.h"
#include "Pivot.h"
#include "Relation.h"
#include "RootAccessible.h"
#include "TextLeafRange.h"
#include "UiaText.h"
#include "UiaTextRange.h"

using namespace mozilla;
using namespace mozilla::a11y;

// Helper functions

static ToggleState ToToggleState(uint64_t aState) {
  if (aState & states::MIXED) {
    return ToggleState_Indeterminate;
  }
  if (aState & (states::CHECKED | states::PRESSED)) {
    return ToggleState_On;
  }
  return ToggleState_Off;
}

static ExpandCollapseState ToExpandCollapseState(uint64_t aState) {
  if (aState & states::EXPANDED) {
    return ExpandCollapseState_Expanded;
  }
  // If aria-haspopup is specified without aria-expanded, we should still expose
  // collapsed, since aria-haspopup infers that it can be expanded. The
  // alternative is ExpandCollapseState_LeafNode, but that means that the
  // element can't be expanded nor collapsed.
  if (aState & (states::COLLAPSED | states::HASPOPUP)) {
    return ExpandCollapseState_Collapsed;
  }
  return ExpandCollapseState_LeafNode;
}

static bool IsRadio(Accessible* aAcc) {
  role r = aAcc->Role();
  return r == roles::RADIOBUTTON || r == roles::RADIO_MENU_ITEM;
}

// Used to search for a text leaf descendant for the LabeledBy property.
class LabelTextLeafRule : public PivotRule {
 public:
  virtual uint16_t Match(Accessible* aAcc) override {
    if (aAcc->IsTextLeaf()) {
      nsAutoString name;
      aAcc->Name(name);
      if (name.IsEmpty() || name.EqualsLiteral(" ")) {
        // An empty or white space text leaf isn't useful as a label.
        return nsIAccessibleTraversalRule::FILTER_IGNORE;
      }
      return nsIAccessibleTraversalRule::FILTER_MATCH;
    }
    if (!nsTextEquivUtils::HasNameRule(aAcc, eNameFromSubtreeIfReqRule)) {
      // Don't descend into things that can't be used as label content; e.g.
      // text boxes.
      return nsIAccessibleTraversalRule::FILTER_IGNORE |
             nsIAccessibleTraversalRule::FILTER_IGNORE_SUBTREE;
    }
    return nsIAccessibleTraversalRule::FILTER_IGNORE;
  }
};

static void MaybeRaiseUiaLiveRegionEvent(Accessible* aAcc,
                                         uint32_t aGeckoEvent) {
  if (Accessible* live = nsAccUtils::GetLiveRegionRoot(aAcc)) {
    auto* uia = MsaaAccessible::GetFrom(live);
    ::UiaRaiseAutomationEvent(uia, UIA_LiveRegionChangedEventId);
  }
}

static bool HasTextPattern(Accessible* aAcc) {
  // The Text pattern must be supported for documents and editable text controls
  // on the web:
  // https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-textpattern-and-embedded-objects-overview#webpage-and-text-input-controls-in-edge
  // It is also recommended that the Text pattern be supported for the Text
  // control type:
  // https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-about-text-and-textrange-patterns#control-types
  // If we don't support this for the Text control type, when Narrator is
  // continuously reading a document, it doesn't respect the starting position
  // of the cursor and doesn't move the cursor as it reads. See bug 1949920.
  constexpr uint64_t editableRootStates = states::EDITABLE | states::FOCUSABLE;
  return aAcc->IsText() || (aAcc->IsDoc() && !aAcc->IsRoot()) ||
         (aAcc->IsHyperText() &&
          (aAcc->State() & editableRootStates) == editableRootStates);
}

static Accessible* GetTextContainer(Accessible* aDescendant) {
  // "An element that implements the TextChild control pattern must be a child,
  // or descendent, of an element that supports the Text control pattern."
  // https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nn-uiautomationcore-itextchildprovider#remarks
  for (Accessible* ancestor = aDescendant->Parent(); ancestor;
       ancestor = ancestor->Parent()) {
    if (HasTextPattern(ancestor)) {
      return ancestor;
    }
  }
  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// uiaRawElmProvider
////////////////////////////////////////////////////////////////////////////////

Accessible* uiaRawElmProvider::Acc() const {
  return static_cast<const MsaaAccessible*>(this)->Acc();
}

/* static */
void uiaRawElmProvider::RaiseUiaEventForGeckoEvent(Accessible* aAcc,
                                                   uint32_t aGeckoEvent) {
  if (!Compatibility::IsUiaEnabled() || !::UiaClientsAreListening()) {
    return;
  }
  auto* uia = MsaaAccessible::GetFrom(aAcc);
  if (!uia) {
    return;
  }
  // Some UIA events include or depend on data that might not be cached yet. We
  // shouldn't request additional cache domains in this case because a client
  // might not even care about these events. Instead, we use explicit client
  // queries as a signal to request domains.
  CacheDomainActivationBlocker cacheBlocker;
  PROPERTYID property = 0;
  _variant_t newVal;
  bool gotNewVal = false;
  // For control pattern properties, we can't use GetPropertyValue. In those
  // cases, we must set newVal appropriately and set gotNewVal to true.
  switch (aGeckoEvent) {
    case nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE:
      property = UIA_FullDescriptionPropertyId;
      break;
    case nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE:
      // There is a UiaRaiseAsyncContentLoadedEvent function, but the client API
      // doesn't have a specialized event handler for this event. Also, Chromium
      // uses UiaRaiseAutomationEvent for this event.
      ::UiaRaiseAutomationEvent(uia, UIA_AsyncContentLoadedEventId);
      return;
    case nsIAccessibleEvent::EVENT_FOCUS:
      ::UiaRaiseAutomationEvent(uia, UIA_AutomationFocusChangedEventId);
      return;
    case nsIAccessibleEvent::EVENT_NAME_CHANGE:
      property = UIA_NamePropertyId;
      MaybeRaiseUiaLiveRegionEvent(aAcc, aGeckoEvent);
      break;
    case nsIAccessibleEvent::EVENT_SELECTION:
      ::UiaRaiseAutomationEvent(uia, UIA_SelectionItem_ElementSelectedEventId);
      return;
    case nsIAccessibleEvent::EVENT_SELECTION_ADD:
      ::UiaRaiseAutomationEvent(
          uia, UIA_SelectionItem_ElementAddedToSelectionEventId);
      return;
    case nsIAccessibleEvent::EVENT_SELECTION_REMOVE:
      ::UiaRaiseAutomationEvent(
          uia, UIA_SelectionItem_ElementRemovedFromSelectionEventId);
      return;
    case nsIAccessibleEvent::EVENT_SELECTION_WITHIN:
      ::UiaRaiseAutomationEvent(uia, UIA_Selection_InvalidatedEventId);
      return;
    case nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED:
    case nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED:
      ::UiaRaiseAutomationEvent(uia, UIA_Text_TextSelectionChangedEventId);
      return;
    case nsIAccessibleEvent::EVENT_TEXT_INSERTED:
    case nsIAccessibleEvent::EVENT_TEXT_REMOVED:
      ::UiaRaiseAutomationEvent(uia, UIA_Text_TextChangedEventId);
      MaybeRaiseUiaLiveRegionEvent(aAcc, aGeckoEvent);
      return;
    case nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE:
      property = UIA_ValueValuePropertyId;
      newVal.vt = VT_BSTR;
      uia->get_Value(&newVal.bstrVal);
      gotNewVal = true;
      break;
    case nsIAccessibleEvent::EVENT_VALUE_CHANGE:
      property = UIA_RangeValueValuePropertyId;
      newVal.vt = VT_R8;
      uia->get_Value(&newVal.dblVal);
      gotNewVal = true;
      break;
  }
  if (property) {
    // We can't get the old value. Thankfully, clients don't seem to need it.
    _variant_t oldVal;
    if (!gotNewVal) {
      // This isn't a pattern property, so we can use GetPropertyValue.
      uia->GetPropertyValue(property, &newVal);
    }
    ::UiaRaiseAutomationPropertyChangedEvent(uia, property, oldVal, newVal);
  }
}

/* static */
void uiaRawElmProvider::RaiseUiaEventForStateChange(Accessible* aAcc,
                                                    uint64_t aState,
                                                    bool aEnabled) {
  if (!Compatibility::IsUiaEnabled() || !::UiaClientsAreListening()) {
    return;
  }
  auto* uia = MsaaAccessible::GetFrom(aAcc);
  if (!uia) {
    return;
  }
  PROPERTYID property = 0;
  _variant_t newVal;
  switch (aState) {
    case states::CHECKED:
      if (aEnabled && IsRadio(aAcc)) {
        ::UiaRaiseAutomationEvent(uia,
                                  UIA_SelectionItem_ElementSelectedEventId);
        return;
      }
      // For other checkable things, the Toggle pattern is used.
      [[fallthrough]];
    case states::MIXED:
    case states::PRESSED:
      property = UIA_ToggleToggleStatePropertyId;
      newVal.vt = VT_I4;
      newVal.lVal = ToToggleState(aEnabled ? aState : 0);
      break;
    case states::COLLAPSED:
    case states::EXPANDED:
    case states::HASPOPUP:
      property = UIA_ExpandCollapseExpandCollapseStatePropertyId;
      newVal.vt = VT_I4;
      newVal.lVal = ToExpandCollapseState(aEnabled ? aState : 0);
      break;
    case states::UNAVAILABLE:
      property = UIA_IsEnabledPropertyId;
      newVal.vt = VT_BOOL;
      newVal.boolVal = aEnabled ? VARIANT_FALSE : VARIANT_TRUE;
      break;
    default:
      return;
  }
  MOZ_ASSERT(property);
  // We can't get the old value. Thankfully, clients don't seem to need it.
  _variant_t oldVal;
  ::UiaRaiseAutomationPropertyChangedEvent(uia, property, oldVal, newVal);
}

// IUnknown

STDMETHODIMP
uiaRawElmProvider::QueryInterface(REFIID aIid, void** aInterface) {
  *aInterface = nullptr;
  if (aIid == IID_IAccessibleEx) {
    *aInterface = static_cast<IAccessibleEx*>(this);
  } else if (aIid == IID_IRawElementProviderSimple) {
    *aInterface = static_cast<IRawElementProviderSimple*>(this);
  } else if (aIid == IID_IRawElementProviderFragment) {
    *aInterface = static_cast<IRawElementProviderFragment*>(this);
  } else if (aIid == IID_IExpandCollapseProvider) {
    *aInterface = static_cast<IExpandCollapseProvider*>(this);
  } else if (aIid == IID_IInvokeProvider) {
    *aInterface = static_cast<IInvokeProvider*>(this);
  } else if (aIid == IID_IRangeValueProvider) {
    *aInterface = static_cast<IRangeValueProvider*>(this);
  } else if (aIid == IID_IScrollItemProvider) {
    *aInterface = static_cast<IScrollItemProvider*>(this);
  } else if (aIid == IID_ISelectionItemProvider) {
    *aInterface = static_cast<ISelectionItemProvider*>(this);
  } else if (aIid == IID_ISelectionProvider) {
    *aInterface = static_cast<ISelectionProvider*>(this);
  } else if (aIid == IID_ITextChildProvider) {
    *aInterface = static_cast<ITextChildProvider*>(this);
  } else if (aIid == IID_IToggleProvider) {
    *aInterface = static_cast<IToggleProvider*>(this);
  } else if (aIid == IID_IValueProvider) {
    *aInterface = static_cast<IValueProvider*>(this);
  } else {
    return E_NOINTERFACE;
  }
  MOZ_ASSERT(*aInterface);
  static_cast<MsaaAccessible*>(this)->AddRef();
  return S_OK;
}

////////////////////////////////////////////////////////////////////////////////
// IAccessibleEx

STDMETHODIMP
uiaRawElmProvider::GetObjectForChild(
    long aIdChild, __RPC__deref_out_opt IAccessibleEx** aAccEx) {
  if (!aAccEx) return E_INVALIDARG;

  *aAccEx = nullptr;

  return Acc() ? S_OK : CO_E_OBJNOTCONNECTED;
}

STDMETHODIMP
uiaRawElmProvider::GetIAccessiblePair(__RPC__deref_out_opt IAccessible** aAcc,
                                      __RPC__out long* aIdChild) {
  if (!aAcc || !aIdChild) return E_INVALIDARG;

  *aAcc = nullptr;
  *aIdChild = 0;

  if (!Acc()) {
    return CO_E_OBJNOTCONNECTED;
  }

  *aIdChild = CHILDID_SELF;
  RefPtr<IAccessible> copy = static_cast<MsaaAccessible*>(this);
  copy.forget(aAcc);

  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::GetRuntimeId(__RPC__deref_out_opt SAFEARRAY** aRuntimeIds) {
  if (!aRuntimeIds) return E_INVALIDARG;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }

  int ids[] = {UiaAppendRuntimeId, MsaaAccessible::GetChildIDFor(acc)};
  *aRuntimeIds = SafeArrayCreateVector(VT_I4, 0, 2);
  if (!*aRuntimeIds) return E_OUTOFMEMORY;

  for (LONG i = 0; i < (LONG)std::size(ids); i++)
    SafeArrayPutElement(*aRuntimeIds, &i, (void*)&(ids[i]));

  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::ConvertReturnedElement(
    __RPC__in_opt IRawElementProviderSimple* aRawElmProvider,
    __RPC__deref_out_opt IAccessibleEx** aAccEx) {
  if (!aRawElmProvider || !aAccEx) return E_INVALIDARG;

  *aAccEx = nullptr;

  void* instancePtr = nullptr;
  HRESULT hr = aRawElmProvider->QueryInterface(IID_IAccessibleEx, &instancePtr);
  if (SUCCEEDED(hr)) *aAccEx = static_cast<IAccessibleEx*>(instancePtr);

  return hr;
}

////////////////////////////////////////////////////////////////////////////////
// IRawElementProviderSimple

STDMETHODIMP
uiaRawElmProvider::get_ProviderOptions(
    __RPC__out enum ProviderOptions* aOptions) {
  if (!aOptions) return E_INVALIDARG;

  *aOptions = kProviderOptions;
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::GetPatternProvider(
    PATTERNID aPatternId, __RPC__deref_out_opt IUnknown** aPatternProvider) {
  if (!aPatternProvider) return E_INVALIDARG;
  *aPatternProvider = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  switch (aPatternId) {
    case UIA_ExpandCollapsePatternId:
      if (HasExpandCollapsePattern()) {
        RefPtr<IExpandCollapseProvider> expand = this;
        expand.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_GridPatternId:
      if (acc->IsTable()) {
        auto grid = GetPatternFromDerived<ia2AccessibleTable, IGridProvider>();
        grid.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_GridItemPatternId:
      if (acc->IsTableCell()) {
        auto item =
            GetPatternFromDerived<ia2AccessibleTableCell, IGridItemProvider>();
        item.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_InvokePatternId:
      // Per the UIA documentation, we should only expose the Invoke pattern "if
      // the same behavior is not exposed through another control pattern
      // provider".
      if (acc->ActionCount() > 0 && !HasTogglePattern() &&
          !HasExpandCollapsePattern() && !HasSelectionItemPattern()) {
        RefPtr<IInvokeProvider> invoke = this;
        invoke.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_RangeValuePatternId:
      if (acc->HasNumericValue()) {
        RefPtr<IValueProvider> value = this;
        value.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_ScrollItemPatternId: {
      RefPtr<IScrollItemProvider> scroll = this;
      scroll.forget(aPatternProvider);
      return S_OK;
    }
    case UIA_SelectionItemPatternId:
      if (HasSelectionItemPattern()) {
        RefPtr<ISelectionItemProvider> item = this;
        item.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_SelectionPatternId:
      // According to the UIA documentation, radio button groups should support
      // the Selection pattern. However:
      // 1. The Core AAM spec doesn't specify the Selection pattern for
      // the radiogroup role.
      // 2. HTML radio buttons might not be contained by a dedicated group.
      // 3. Chromium exposes the Selection pattern on radio groups, but it
      // doesn't expose any selected items, even when there is a checked radio
      // child.
      // 4. Radio menu items are similar to radio buttons and all the above
      // also applies to menus.
      // For now, we don't support the Selection pattern for radio groups or
      // menus, only for list boxes, tab lists, etc.
      if (acc->IsSelect()) {
        RefPtr<ISelectionProvider> selection = this;
        selection.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_TablePatternId:
      if (acc->IsTable()) {
        auto table =
            GetPatternFromDerived<ia2AccessibleTable, ITableProvider>();
        table.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_TableItemPatternId:
      if (acc->IsTableCell()) {
        auto item =
            GetPatternFromDerived<ia2AccessibleTableCell, ITableItemProvider>();
        item.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_TextChildPatternId:
      if (GetTextContainer(acc)) {
        RefPtr<ITextChildProvider> textChild = this;
        textChild.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_TextPatternId:
      if (HasTextPattern(acc)) {
        RefPtr<ITextProvider> text =
            new UiaText(static_cast<MsaaAccessible*>(this));
        text.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_TogglePatternId:
      if (HasTogglePattern()) {
        RefPtr<IToggleProvider> toggle = this;
        toggle.forget(aPatternProvider);
      }
      return S_OK;
    case UIA_ValuePatternId:
      if (HasValuePattern()) {
        RefPtr<IValueProvider> value = this;
        value.forget(aPatternProvider);
      }
      return S_OK;
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::GetPropertyValue(PROPERTYID aPropertyId,
                                    __RPC__out VARIANT* aPropertyValue) {
  if (!aPropertyValue) return E_INVALIDARG;

  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  LocalAccessible* localAcc = acc->AsLocal();

  aPropertyValue->vt = VT_EMPTY;

  switch (aPropertyId) {
    // Accelerator Key / shortcut.
    case UIA_AcceleratorKeyPropertyId: {
      if (!localAcc) {
        // KeyboardShortcut is only currently relevant for LocalAccessible.
        break;
      }
      nsAutoString keyString;

      localAcc->KeyboardShortcut().ToString(keyString);

      if (!keyString.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(keyString.get());
        return S_OK;
      }

      break;
    }

    // Access Key / mneumonic.
    case UIA_AccessKeyPropertyId: {
      nsAutoString keyString;

      acc->AccessKey().ToString(keyString);

      if (!keyString.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(keyString.get());
        return S_OK;
      }

      break;
    }

    case UIA_AriaRolePropertyId: {
      nsAutoString role;
      if (acc->HasARIARole()) {
        RefPtr<AccAttributes> attributes = acc->Attributes();
        attributes->GetAttribute(nsGkAtoms::xmlroles, role);
      } else if (nsStaticAtom* computed = acc->ComputedARIARole()) {
        computed->ToString(role);
      }
      if (!role.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(role.get());
        return S_OK;
      }
      break;
    }

    // ARIA Properties
    case UIA_AriaPropertiesPropertyId: {
      nsAutoString ariaProperties;
      // We only expose the properties we need to expose here.
      nsAutoString live;
      nsAccUtils::GetLiveRegionSetting(acc, live);
      if (!live.IsEmpty()) {
        // This is a live region root. The live setting is already exposed via
        // the LiveSetting property. However, there is no UIA property for
        // atomic.
        Maybe<bool> atomic;
        acc->LiveRegionAttributes(nullptr, nullptr, &atomic, nullptr);
        if (atomic && *atomic) {
          ariaProperties.AppendLiteral("atomic=true");
        } else {
          // Narrator assumes a default of true, so we need to output the
          // correct default (false) even if the attribute isn't specified.
          ariaProperties.AppendLiteral("atomic=false");
        }
      }
      if (!ariaProperties.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(ariaProperties.get());
        return S_OK;
      }
      break;
    }

    case UIA_AutomationIdPropertyId: {
      nsAutoString id;
      acc->DOMNodeID(id);
      if (!id.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(id.get());
        return S_OK;
      }
      break;
    }

    case UIA_ClassNamePropertyId: {
      nsAutoString className;
      acc->DOMNodeClass(className);
      if (!className.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(className.get());
        return S_OK;
      }
      break;
    }

    case UIA_ControllerForPropertyId:
      aPropertyValue->vt = VT_UNKNOWN | VT_ARRAY;
      aPropertyValue->parray = AccRelationsToUiaArray(
          {RelationType::CONTROLLER_FOR, RelationType::ERRORMSG});
      return S_OK;

    case UIA_ControlTypePropertyId:
      aPropertyValue->vt = VT_I4;
      aPropertyValue->lVal = GetControlType();
      break;

    case UIA_DescribedByPropertyId:
      aPropertyValue->vt = VT_UNKNOWN | VT_ARRAY;
      aPropertyValue->parray = AccRelationsToUiaArray(
          {RelationType::DESCRIBED_BY, RelationType::DETAILS});
      return S_OK;

    case UIA_FlowsFromPropertyId:
      aPropertyValue->vt = VT_UNKNOWN | VT_ARRAY;
      aPropertyValue->parray =
          AccRelationsToUiaArray({RelationType::FLOWS_FROM});
      return S_OK;

    case UIA_FlowsToPropertyId:
      aPropertyValue->vt = VT_UNKNOWN | VT_ARRAY;
      aPropertyValue->parray = AccRelationsToUiaArray({RelationType::FLOWS_TO});
      return S_OK;

    case UIA_FrameworkIdPropertyId:
      if (ApplicationAccessible* app = ApplicationAcc()) {
        nsAutoString name;
        app->PlatformName(name);
        if (!name.IsEmpty()) {
          aPropertyValue->vt = VT_BSTR;
          aPropertyValue->bstrVal = ::SysAllocString(name.get());
          return S_OK;
        }
      }
      break;

    case UIA_FullDescriptionPropertyId: {
      nsAutoString desc;
      acc->Description(desc);
      if (!desc.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(desc.get());
        return S_OK;
      }
      break;
    }

    case UIA_HasKeyboardFocusPropertyId:
      aPropertyValue->vt = VT_BOOL;
      aPropertyValue->boolVal = VARIANT_FALSE;
      if (auto* focusMgr = FocusMgr()) {
        if (focusMgr->IsFocused(acc)) {
          aPropertyValue->boolVal = VARIANT_TRUE;
        }
      }
      return S_OK;

    case UIA_IsContentElementPropertyId:
    case UIA_IsControlElementPropertyId:
      aPropertyValue->vt = VT_BOOL;
      aPropertyValue->boolVal = IsControl() ? VARIANT_TRUE : VARIANT_FALSE;
      return S_OK;

    case UIA_IsEnabledPropertyId:
      aPropertyValue->vt = VT_BOOL;
      aPropertyValue->boolVal =
          (acc->State() & states::UNAVAILABLE) ? VARIANT_FALSE : VARIANT_TRUE;
      return S_OK;

    case UIA_IsKeyboardFocusablePropertyId:
      aPropertyValue->vt = VT_BOOL;
      aPropertyValue->boolVal =
          (acc->State() & states::FOCUSABLE) ? VARIANT_TRUE : VARIANT_FALSE;
      return S_OK;

    case UIA_LabeledByPropertyId:
      if (Accessible* target = GetLabeledBy()) {
        aPropertyValue->vt = VT_UNKNOWN;
        RefPtr<IRawElementProviderSimple> uia = MsaaAccessible::GetFrom(target);
        uia.forget(&aPropertyValue->punkVal);
        return S_OK;
      }
      break;

    case UIA_LandmarkTypePropertyId:
      if (long type = GetLandmarkType()) {
        aPropertyValue->vt = VT_I4;
        aPropertyValue->lVal = type;
        return S_OK;
      }
      break;

    case UIA_LevelPropertyId:
      aPropertyValue->vt = VT_I4;
      aPropertyValue->lVal = acc->GroupPosition().level;
      return S_OK;

    case UIA_LiveSettingPropertyId:
      aPropertyValue->vt = VT_I4;
      aPropertyValue->lVal = GetLiveSetting();
      return S_OK;

    case UIA_LocalizedLandmarkTypePropertyId: {
      nsAutoString landmark;
      GetLocalizedLandmarkType(landmark);
      if (!landmark.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(landmark.get());
        return S_OK;
      }
      break;
    }

    case UIA_NamePropertyId: {
      nsAutoString name;
      acc->Name(name);
      if (!name.IsEmpty()) {
        aPropertyValue->vt = VT_BSTR;
        aPropertyValue->bstrVal = ::SysAllocString(name.get());
        return S_OK;
      }
      break;
    }

    case UIA_PositionInSetPropertyId:
      aPropertyValue->vt = VT_I4;
      aPropertyValue->lVal = acc->GroupPosition().posInSet;
      return S_OK;

    case UIA_SizeOfSetPropertyId:
      aPropertyValue->vt = VT_I4;
      aPropertyValue->lVal = acc->GroupPosition().setSize;
      return S_OK;
  }

  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_HostRawElementProvider(
    __RPC__deref_out_opt IRawElementProviderSimple** aRawElmProvider) {
  if (!aRawElmProvider) return E_INVALIDARG;
  *aRawElmProvider = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (acc->IsRoot()) {
    HWND hwnd = MsaaAccessible::GetHWNDFor(acc);
    return UiaHostProviderFromHwnd(hwnd, aRawElmProvider);
  }
  return S_OK;
}

// IRawElementProviderFragment

STDMETHODIMP
uiaRawElmProvider::Navigate(
    enum NavigateDirection aDirection,
    __RPC__deref_out_opt IRawElementProviderFragment** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  Accessible* target = nullptr;
  switch (aDirection) {
    case NavigateDirection_Parent:
      if (!acc->IsRoot()) {
        target = acc->Parent();
      }
      break;
    case NavigateDirection_NextSibling:
      if (!acc->IsRoot()) {
        target = acc->NextSibling();
      }
      break;
    case NavigateDirection_PreviousSibling:
      if (!acc->IsRoot()) {
        target = acc->PrevSibling();
      }
      break;
    case NavigateDirection_FirstChild:
      if (!nsAccUtils::MustPrune(acc)) {
        target = acc->FirstChild();
      }
      break;
    case NavigateDirection_LastChild:
      if (!nsAccUtils::MustPrune(acc)) {
        target = acc->LastChild();
      }
      break;
    default:
      return E_INVALIDARG;
  }
  RefPtr<IRawElementProviderFragment> fragment =
      MsaaAccessible::GetFrom(target);
  fragment.forget(aRetVal);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_BoundingRectangle(__RPC__out struct UiaRect* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  LayoutDeviceIntRect rect = acc->Bounds();
  aRetVal->left = rect.X();
  aRetVal->top = rect.Y();
  aRetVal->width = rect.Width();
  aRetVal->height = rect.Height();
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::GetEmbeddedFragmentRoots(
    __RPC__deref_out_opt SAFEARRAY** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::SetFocus() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  acc->TakeFocus();
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_FragmentRoot(
    __RPC__deref_out_opt IRawElementProviderFragmentRoot** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  LocalAccessible* localAcc = acc->AsLocal();
  if (!localAcc) {
    localAcc = acc->AsRemote()->OuterDocOfRemoteBrowser();
    if (!localAcc) {
      return CO_E_OBJNOTCONNECTED;
    }
  }
  MsaaAccessible* msaa = MsaaAccessible::GetFrom(localAcc->RootAccessible());
  RefPtr<IRawElementProviderFragmentRoot> fragRoot =
      static_cast<MsaaRootAccessible*>(msaa);
  fragRoot.forget(aRetVal);
  return S_OK;
}

// IInvokeProvider methods

STDMETHODIMP
uiaRawElmProvider::Invoke() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (acc->DoAction(0)) {
    // We don't currently have a way to notify when the action was actually
    // handled. The UIA documentation says it's okay to fire this immediately if
    // it "is not possible or practical to wait until the action is complete".
    ::UiaRaiseAutomationEvent(this, UIA_Invoke_InvokedEventId);
  }
  return S_OK;
}

// IToggleProvider methods

STDMETHODIMP
uiaRawElmProvider::Toggle() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  acc->DoAction(0);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_ToggleState(__RPC__out enum ToggleState* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = ToToggleState(acc->State());
  return S_OK;
}

// IExpandCollapseProvider methods

STDMETHODIMP
uiaRawElmProvider::Expand() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (acc->State() & states::EXPANDED) {
    return UIA_E_INVALIDOPERATION;
  }
  acc->DoAction(0);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::Collapse() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (acc->State() & states::COLLAPSED) {
    return UIA_E_INVALIDOPERATION;
  }
  acc->DoAction(0);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_ExpandCollapseState(
    __RPC__out enum ExpandCollapseState* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = ToExpandCollapseState(acc->State());
  return S_OK;
}

// IScrollItemProvider methods

MOZ_CAN_RUN_SCRIPT_BOUNDARY STDMETHODIMP uiaRawElmProvider::ScrollIntoView() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  acc->ScrollTo(nsIAccessibleScrollType::SCROLL_TYPE_ANYWHERE);
  return S_OK;
}

// IValueProvider methods

STDMETHODIMP
uiaRawElmProvider::SetValue(__RPC__in LPCWSTR aVal) {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  HyperTextAccessibleBase* ht = acc->AsHyperTextBase();
  if (!ht || !acc->IsTextRole()) {
    return UIA_E_INVALIDOPERATION;
  }
  if (acc->State() & (states::READONLY | states::UNAVAILABLE)) {
    return UIA_E_INVALIDOPERATION;
  }
  nsAutoString text(aVal);
  ht->ReplaceText(text);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_Value(__RPC__deref_out_opt BSTR* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  nsAutoString value;
  acc->Value(value);
  if (value.IsEmpty() && acc->IsDoc()) {
    // Exposing the URl via the Value pattern doesn't seem to be documented
    // anywhere. However, Chromium does it, as does the IA2 -> UIA proxy.
    nsAccUtils::DocumentURL(acc, value);
  }
  *aRetVal = ::SysAllocStringLen(value.get(), value.Length());
  if (!*aRetVal) {
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_IsReadOnly(__RPC__out BOOL* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->State() & states::READONLY;
  return S_OK;
}

// IRangeValueProvider methods

STDMETHODIMP
uiaRawElmProvider::SetValue(double aVal) {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (!acc->SetCurValue(aVal)) {
    return UIA_E_INVALIDOPERATION;
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_Value(__RPC__out double* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->CurValue();
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_Maximum(__RPC__out double* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->MaxValue();
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_Minimum(
    /* [retval][out] */ __RPC__out double* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->MinValue();
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_LargeChange(
    /* [retval][out] */ __RPC__out double* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  // We want the change that would occur if the user pressed page up or page
  // down. For HTML input elements, this is 10% of the total range unless step
  // is larger. See:
  // https://searchfox.org/mozilla-central/rev/c7df16ffad1f12a19c81c16bce0b65e4a15304d0/dom/html/HTMLInputElement.cpp#3878
  double step = acc->Step();
  double min = acc->MinValue();
  double max = acc->MaxValue();
  if (std::isnan(step) || std::isnan(min) || std::isnan(max)) {
    *aRetVal = UnspecifiedNaN<double>();
  } else {
    *aRetVal = std::max(step, (max - min) / 10);
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_SmallChange(
    /* [retval][out] */ __RPC__out double* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->Step();
  return S_OK;
}

// ISelectionProvider methods

STDMETHODIMP
uiaRawElmProvider::GetSelection(__RPC__deref_out_opt SAFEARRAY** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  AutoTArray<Accessible*, 10> items;
  acc->SelectedItems(&items);
  *aRetVal = AccessibleArrayToUiaArray(items);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_CanSelectMultiple(__RPC__out BOOL* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->State() & states::MULTISELECTABLE;
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_IsSelectionRequired(__RPC__out BOOL* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  *aRetVal = acc->State() & states::REQUIRED;
  return S_OK;
}

// ISelectionItemProvider methods

STDMETHODIMP
uiaRawElmProvider::Select() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (IsRadio(acc)) {
    acc->DoAction(0);
  } else {
    acc->TakeSelection();
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::AddToSelection() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (IsRadio(acc)) {
    acc->DoAction(0);
  } else {
    acc->SetSelected(true);
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::RemoveFromSelection() {
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (IsRadio(acc)) {
    return UIA_E_INVALIDOPERATION;
  }
  acc->SetSelected(false);
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_IsSelected(__RPC__out BOOL* aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (IsRadio(acc)) {
    *aRetVal = acc->State() & states::CHECKED;
  } else {
    *aRetVal = acc->State() & states::SELECTED;
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_SelectionContainer(
    __RPC__deref_out_opt IRawElementProviderSimple** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  Accessible* container = nsAccUtils::GetSelectableContainer(acc, acc->State());
  if (!container) {
    return E_FAIL;
  }
  RefPtr<IRawElementProviderSimple> uia = MsaaAccessible::GetFrom(container);
  uia.forget(aRetVal);
  return S_OK;
}

// ITextChildProvider methods

STDMETHODIMP
uiaRawElmProvider::get_TextContainer(
    __RPC__deref_out_opt IRawElementProviderSimple** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  if (Accessible* container = GetTextContainer(acc)) {
    RefPtr<IRawElementProviderSimple> uia = MsaaAccessible::GetFrom(container);
    uia.forget(aRetVal);
  }
  return S_OK;
}

STDMETHODIMP
uiaRawElmProvider::get_TextRange(
    __RPC__deref_out_opt ITextRangeProvider** aRetVal) {
  if (!aRetVal) {
    return E_INVALIDARG;
  }
  *aRetVal = nullptr;
  Accessible* acc = Acc();
  if (!acc) {
    return CO_E_OBJNOTCONNECTED;
  }
  TextLeafRange range = TextLeafRange::FromAccessible(acc);
  RefPtr uiaRange = new UiaTextRange(range);
  uiaRange.forget(aRetVal);
  return S_OK;
}

// Private methods

bool uiaRawElmProvider::IsControl() {
  // UIA provides multiple views of the tree: raw, control and content. The
  // control and content views should only contain elements which a user cares
  // about when navigating.
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  if (acc->IsTextLeaf()) {
    // If an ancestor control allows the name to be generated from content, do
    // not expose this text leaf as a control. Otherwise, the user will see the
    // text twice: once as the label of the control and once for the text leaf.
    for (Accessible* ancestor = acc->Parent(); ancestor && !ancestor->IsDoc();
         ancestor = ancestor->Parent()) {
      if (nsTextEquivUtils::HasNameRule(ancestor, eNameFromSubtreeRule)) {
        return false;
      }
    }
    return true;
  }

  if (acc->HasNumericValue() || acc->ActionCount() > 0) {
    return true;
  }
  uint64_t state = acc->State();
  if (state & states::FOCUSABLE) {
    return true;
  }
  if (state & states::EDITABLE) {
    Accessible* parent = acc->Parent();
    if (parent && !(parent->State() & states::EDITABLE)) {
      // This is the root of a rich editable control.
      return true;
    }
  }

  // Don't treat generic or text containers as controls except in specific
  // cases.
  switch (acc->Role()) {
    case roles::EMPHASIS:
    case roles::MARK:
    case roles::PARAGRAPH:
    case roles::SECTION:
    case roles::STRONG:
    case roles::SUBSCRIPT:
    case roles::SUPERSCRIPT:
    case roles::TEXT:
    case roles::TEXT_CONTAINER: {
      // If there is a name or a description, treat it as a control.
      if (!acc->NameIsEmpty()) {
        return true;
      }
      nsAutoString text;
      acc->Description(text);
      if (!text.IsEmpty()) {
        return true;
      }
      // If this is the root of a live region, treat it as a control, since
      // Narrator won't correctly traverse the live region's content when
      // handling changes otherwise.
      nsAutoString live;
      nsAccUtils::GetLiveRegionSetting(acc, live);
      if (!live.IsEmpty()) {
        return true;
      }
      return false;
    }
    default:
      break;
  }

  return true;
}

long uiaRawElmProvider::GetControlType() const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
#define ROLE(_geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType,  \
             nameRule)                                                       \
  case roles::_geckoRole:                                                    \
    return uiaControlType;                                                   \
    break;
  switch (acc->Role()) {
#include "RoleMap.h"
  }
#undef ROLE
  MOZ_CRASH("Unknown role.");
  return 0;
}

bool uiaRawElmProvider::HasTogglePattern() {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  return acc->State() & states::CHECKABLE ||
         acc->Role() == roles::TOGGLE_BUTTON;
}

bool uiaRawElmProvider::HasExpandCollapsePattern() {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  return acc->State() & (states::EXPANDABLE | states::HASPOPUP);
}

bool uiaRawElmProvider::HasValuePattern() const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  if (acc->HasNumericValue() || acc->IsCombobox() || acc->IsHTMLLink() ||
      acc->IsTextField() || acc->IsDoc()) {
    return true;
  }
  const nsRoleMapEntry* roleMapEntry = acc->ARIARoleMap();
  return roleMapEntry && roleMapEntry->Is(nsGkAtoms::textbox);
}

template <class Derived, class Interface>
RefPtr<Interface> uiaRawElmProvider::GetPatternFromDerived() {
  // MsaaAccessible inherits from uiaRawElmProvider. Derived
  // inherits from MsaaAccessible and Interface. The compiler won't let us
  // directly static_cast to Interface, hence the intermediate casts.
  auto* msaa = static_cast<MsaaAccessible*>(this);
  auto* derived = static_cast<Derived*>(msaa);
  return derived;
}

bool uiaRawElmProvider::HasSelectionItemPattern() {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  // In UIA, radio buttons and radio menu items are exposed as selected or
  // unselected.
  return acc->State() & states::SELECTABLE || IsRadio(acc);
}

SAFEARRAY* uiaRawElmProvider::AccRelationsToUiaArray(
    std::initializer_list<RelationType> aTypes) const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  AutoTArray<Accessible*, 10> targets;
  for (RelationType type : aTypes) {
    Relation rel = acc->RelationByType(type);
    while (Accessible* target = rel.Next()) {
      targets.AppendElement(target);
    }
  }
  return AccessibleArrayToUiaArray(targets);
}

Accessible* uiaRawElmProvider::GetLabeledBy() const {
  // Per the UIA documentation, some control types should never get a value for
  // the LabeledBy property.
  switch (GetControlType()) {
    case UIA_ButtonControlTypeId:
    case UIA_CheckBoxControlTypeId:
    case UIA_DataItemControlTypeId:
    case UIA_MenuControlTypeId:
    case UIA_MenuBarControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_ScrollBarControlTypeId:
    case UIA_SeparatorControlTypeId:
    case UIA_StatusBarControlTypeId:
    case UIA_TabItemControlTypeId:
    case UIA_TextControlTypeId:
    case UIA_ToolBarControlTypeId:
    case UIA_ToolTipControlTypeId:
    case UIA_TreeItemControlTypeId:
      return nullptr;
  }

  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  // Even when LabeledBy is supported, it can only return a single "static text"
  // element.
  Relation rel = acc->RelationByType(RelationType::LABELLED_BY);
  LabelTextLeafRule rule;
  while (Accessible* target = rel.Next()) {
    // If target were a text leaf, we should return that, but that shouldn't be
    // possible because only an element (not a text node) can be the target of a
    // relation.
    MOZ_ASSERT(!target->IsTextLeaf());
    Pivot pivot(target);
    if (Accessible* leaf = pivot.Next(target, rule)) {
      return leaf;
    }
  }
  return nullptr;
}

long uiaRawElmProvider::GetLandmarkType() const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  nsStaticAtom* landmark = acc->LandmarkRole();
  if (!landmark) {
    return 0;
  }
  if (landmark == nsGkAtoms::form) {
    return UIA_FormLandmarkTypeId;
  }
  if (landmark == nsGkAtoms::main) {
    return UIA_MainLandmarkTypeId;
  }
  if (landmark == nsGkAtoms::navigation) {
    return UIA_NavigationLandmarkTypeId;
  }
  if (landmark == nsGkAtoms::search) {
    return UIA_SearchLandmarkTypeId;
  }
  return UIA_CustomLandmarkTypeId;
}

void uiaRawElmProvider::GetLocalizedLandmarkType(nsAString& aLocalized) const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  nsStaticAtom* landmark = acc->LandmarkRole();
  // The system provides strings for landmarks explicitly supported by the UIA
  // LandmarkType property; i.e. form, main, navigation and search. We must
  // provide strings for landmarks considered custom by UIA. For now, we only
  // support landmarks in the core ARIA specification, not other ARIA modules
  // such as DPub.
  if (landmark == nsGkAtoms::banner || landmark == nsGkAtoms::complementary ||
      landmark == nsGkAtoms::contentinfo || landmark == nsGkAtoms::region) {
    nsAutoString unlocalized;
    landmark->ToString(unlocalized);
    Accessible::TranslateString(unlocalized, aLocalized);
  }
}

long uiaRawElmProvider::GetLiveSetting() const {
  Accessible* acc = Acc();
  MOZ_ASSERT(acc);
  nsAutoString live;
  nsAccUtils::GetLiveRegionSetting(acc, live);
  if (live.EqualsLiteral("polite")) {
    return LiveSetting::Polite;
  }
  if (live.EqualsLiteral("assertive")) {
    return LiveSetting::Assertive;
  }
  return LiveSetting::Off;
}

SAFEARRAY* a11y::AccessibleArrayToUiaArray(const nsTArray<Accessible*>& aAccs) {
  // The UIA client framework seems to treat a null value the same as an empty
  // array most of the time, but not always. In particular, Narrator breaks if
  // ITextRangeProvider::GetChildren returns null instead of an empty array.
  // Therefore, don't return null for an empty array.
  SAFEARRAY* uias = SafeArrayCreateVector(VT_UNKNOWN, 0, aAccs.Length());
  LONG indices[1] = {0};
  for (Accessible* acc : aAccs) {
    // SafeArrayPutElement calls AddRef on the element, so we use a raw pointer
    // here.
    IRawElementProviderSimple* uia = MsaaAccessible::GetFrom(acc);
    SafeArrayPutElement(uias, indices, uia);
    ++indices[0];
  }
  return uias;
}
