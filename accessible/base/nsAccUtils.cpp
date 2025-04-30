/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAccUtils.h"

#include "AccAttributes.h"
#include "ARIAMap.h"
#include "nsCoreUtils.h"
#include "nsGenericHTMLElement.h"
#include "DocAccessible.h"
#include "DocAccessibleParent.h"
#include "HyperTextAccessible.h"
#include "nsIAccessibleTypes.h"
#include "mozilla/a11y/Role.h"
#include "States.h"
#include "TextLeafAccessible.h"

#include "nsIBaseWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDOMXULContainerElement.h"
#include "mozilla/a11y/RemoteAccessible.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInternals.h"
#include "nsAccessibilityService.h"

using namespace mozilla;
using namespace mozilla::a11y;

void nsAccUtils::SetAccGroupAttrs(AccAttributes* aAttributes, int32_t aLevel,
                                  int32_t aSetSize, int32_t aPosInSet) {
  nsAutoString value;

  if (aLevel) {
    aAttributes->SetAttribute(nsGkAtoms::level, aLevel);
  }

  if (aSetSize && aPosInSet) {
    aAttributes->SetAttribute(nsGkAtoms::posinset, aPosInSet);
    aAttributes->SetAttribute(nsGkAtoms::setsize, aSetSize);
  }
}

void nsAccUtils::SetAccGroupAttrs(AccAttributes* aAttributes,
                                  Accessible* aAcc) {
  GroupPos groupPos = aAcc->GroupPosition();
  nsAccUtils::SetAccGroupAttrs(aAttributes, groupPos.level, groupPos.setSize,
                               groupPos.posInSet);
}

int32_t nsAccUtils::GetLevelForXULContainerItem(nsIContent* aContent) {
  nsCOMPtr<nsIDOMXULContainerItemElement> item =
      aContent->AsElement()->AsXULContainerItem();
  if (!item) return 0;

  nsCOMPtr<dom::Element> containerElement;
  item->GetParentContainer(getter_AddRefs(containerElement));
  nsCOMPtr<nsIDOMXULContainerElement> container =
      containerElement ? containerElement->AsXULContainer() : nullptr;
  if (!container) return 0;

  // Get level of the item.
  int32_t level = -1;
  while (container) {
    level++;

    container->GetParentContainer(getter_AddRefs(containerElement));
    container = containerElement ? containerElement->AsXULContainer() : nullptr;
  }

  return level;
}

void nsAccUtils::SetLiveContainerAttributes(AccAttributes* aAttributes,
                                            Accessible* aStartAcc) {
  nsAutoString live, relevant, busy;
  nsStaticAtom* role = nullptr;
  Maybe<bool> atomic;
  for (Accessible* acc = aStartAcc; acc; acc = acc->Parent()) {
    // We only want the nearest value for each attribute. If we already got a
    // value, don't bother fetching it from further ancestors.
    const bool wasLiveEmpty = live.IsEmpty();
    acc->LiveRegionAttributes(wasLiveEmpty ? &live : nullptr,
                              relevant.IsEmpty() ? &relevant : nullptr,
                              atomic ? nullptr : &atomic,
                              busy.IsEmpty() ? &busy : nullptr);
    if (wasLiveEmpty) {
      const nsRoleMapEntry* roleMap = acc->ARIARoleMap();
      if (live.IsEmpty()) {
        // aria-live wasn't explicitly set. See if an aria-live value is implied
        // by an ARIA role or markup element.
        MOZ_ASSERT(GetAccService());
        if (roleMap) {
          GetLiveAttrValue(roleMap->liveAttRule, live);
        } else if (nsStaticAtom* value = GetAccService()->MarkupAttribute(
                       acc, nsGkAtoms::aria_live)) {
          value->ToString(live);
        }
      }
      if (!live.IsEmpty() && roleMap &&
          roleMap->roleAtom != nsGkAtoms::_empty) {
        role = roleMap->roleAtom;
      }
    }
    if (acc->IsDoc()) {
      break;
    }
  }
  if (!live.IsEmpty()) {
    aAttributes->SetAttribute(nsGkAtoms::containerLive, std::move(live));
  }
  if (role) {
    aAttributes->SetAttribute(nsGkAtoms::containerLiveRole, std::move(role));
  }
  if (!relevant.IsEmpty()) {
    aAttributes->SetAttribute(nsGkAtoms::containerRelevant,
                              std::move(relevant));
  }
  if (atomic) {
    aAttributes->SetAttribute(nsGkAtoms::containerAtomic, *atomic);
  }
  if (!busy.IsEmpty()) {
    aAttributes->SetAttribute(nsGkAtoms::containerBusy, std::move(busy));
  }
}

bool nsAccUtils::HasDefinedARIAToken(nsIContent* aContent, nsAtom* aAtom) {
  NS_ASSERTION(aContent, "aContent is null in call to HasDefinedARIAToken!");

  if (!aContent->IsElement()) return false;

  dom::Element* element = aContent->AsElement();
  if (auto* htmlElement = nsGenericHTMLElement::FromNode(element);
      htmlElement && !element->HasAttr(aAtom)) {
    const auto* defaults = GetARIADefaults(htmlElement);
    if (!defaults) {
      return false;
    }
    return HasDefinedARIAToken(defaults, aAtom);
  }
  return HasDefinedARIAToken(&element->GetAttrs(), aAtom);
}

bool nsAccUtils::HasDefinedARIAToken(const AttrArray* aAttrs, nsAtom* aAtom) {
  return aAttrs->HasAttr(aAtom) &&
         !aAttrs->AttrValueIs(kNameSpaceID_None, aAtom, nsGkAtoms::_empty,
                              eCaseMatters) &&
         !aAttrs->AttrValueIs(kNameSpaceID_None, aAtom, nsGkAtoms::_undefined,
                              eCaseMatters);
}

nsStaticAtom* nsAccUtils::NormalizeARIAToken(const AttrArray* aAttrs,
                                             nsAtom* aAttr) {
  if (!HasDefinedARIAToken(aAttrs, aAttr)) {
    return nsGkAtoms::_empty;
  }

  if (aAttr == nsGkAtoms::aria_current) {
    static AttrArray::AttrValuesArray tokens[] = {
        nsGkAtoms::page, nsGkAtoms::step, nsGkAtoms::location,
        nsGkAtoms::date, nsGkAtoms::time, nsGkAtoms::_true,
        nullptr};
    int32_t idx =
        aAttrs->FindAttrValueIn(kNameSpaceID_None, aAttr, tokens, eCaseMatters);
    // If the token is present, return it, otherwise TRUE as per spec.
    return (idx >= 0) ? tokens[idx] : nsGkAtoms::_true;
  }

  static AttrArray::AttrValuesArray tokens[] = {
      nsGkAtoms::_false, nsGkAtoms::_true, nsGkAtoms::mixed, nullptr};
  int32_t idx =
      aAttrs->FindAttrValueIn(kNameSpaceID_None, aAttr, tokens, eCaseMatters);
  if (idx >= 0) {
    return tokens[idx];
  }

  return nullptr;
}

nsStaticAtom* nsAccUtils::NormalizeARIAToken(dom::Element* aElement,
                                             nsAtom* aAttr) {
  if (auto* htmlElement = nsGenericHTMLElement::FromNode(aElement);
      htmlElement && !aElement->HasAttr(aAttr)) {
    const auto* defaults = GetARIADefaults(htmlElement);
    if (!defaults) {
      return nsGkAtoms::_empty;
    }
    return NormalizeARIAToken(defaults, aAttr);
  }
  return NormalizeARIAToken(&aElement->GetAttrs(), aAttr);
}

Accessible* nsAccUtils::GetSelectableContainer(const Accessible* aAccessible,
                                               uint64_t aState) {
  if (!aAccessible) return nullptr;

  if (!(aState & states::SELECTABLE)) return nullptr;
  MOZ_ASSERT(!aAccessible->IsDoc());

  const Accessible* parent = aAccessible;
  while ((parent = parent->Parent()) && !parent->IsSelect()) {
    if (parent->IsDoc() || parent->Role() == roles::PANE) {
      return nullptr;
    }
  }
  return const_cast<Accessible*>(parent);
}

LocalAccessible* nsAccUtils::GetSelectableContainer(
    LocalAccessible* aAccessible, uint64_t aState) {
  Accessible* selectable =
      GetSelectableContainer(static_cast<Accessible*>(aAccessible), aState);
  return selectable ? selectable->AsLocal() : nullptr;
}

bool nsAccUtils::IsDOMAttrTrue(const LocalAccessible* aAccessible,
                               nsAtom* aAttr) {
  dom::Element* el = aAccessible->Elm();
  return el && ARIAAttrValueIs(el, aAttr, nsGkAtoms::_true, eCaseMatters);
}

Accessible* nsAccUtils::TableFor(Accessible* aAcc) {
  if (!aAcc ||
      (!aAcc->IsTable() && !aAcc->IsTableRow() && !aAcc->IsTableCell())) {
    return nullptr;
  }
  Accessible* table = aAcc;
  for (; table && !table->IsTable(); table = table->Parent()) {
  }
  // We don't assert (table && table->IsTable()) here because
  // it's possible for this tree walk to yield no table at all
  // ex. because a table part has been moved in the tree
  // using aria-owns.
  return table;
}

LocalAccessible* nsAccUtils::TableFor(LocalAccessible* aRow) {
  Accessible* table = TableFor(static_cast<Accessible*>(aRow));
  return table ? table->AsLocal() : nullptr;
}

HyperTextAccessible* nsAccUtils::GetTextContainer(nsINode* aNode) {
  // Get text accessible containing the result node.
  DocAccessible* doc = GetAccService()->GetDocAccessible(aNode->OwnerDoc());
  LocalAccessible* accessible =
      doc ? doc->GetAccessibleOrContainer(aNode) : nullptr;
  if (!accessible) return nullptr;

  do {
    HyperTextAccessible* textAcc = accessible->AsHyperText();
    if (textAcc) return textAcc;

    accessible = accessible->LocalParent();
  } while (accessible);

  return nullptr;
}

LayoutDeviceIntPoint nsAccUtils::ConvertToScreenCoords(
    int32_t aX, int32_t aY, uint32_t aCoordinateType, Accessible* aAccessible) {
  LayoutDeviceIntPoint coords(aX, aY);

  switch (aCoordinateType) {
    // Regardless of coordinate type, the coords returned
    // are in dev pixels.
    case nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE:
      break;

    case nsIAccessibleCoordinateType::COORDTYPE_WINDOW_RELATIVE: {
      coords += GetScreenCoordsForWindow(aAccessible);
      break;
    }

    case nsIAccessibleCoordinateType::COORDTYPE_PARENT_RELATIVE: {
      coords += GetScreenCoordsForParent(aAccessible);
      break;
    }

    default:
      MOZ_ASSERT_UNREACHABLE("invalid coord type!");
  }

  return coords;
}

void nsAccUtils::ConvertScreenCoordsTo(int32_t* aX, int32_t* aY,
                                       uint32_t aCoordinateType,
                                       Accessible* aAccessible) {
  switch (aCoordinateType) {
    // Regardless of coordinate type, the values returned for
    // aX and aY are in dev pixels.
    case nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE:
      break;

    case nsIAccessibleCoordinateType::COORDTYPE_WINDOW_RELATIVE: {
      LayoutDeviceIntPoint coords = GetScreenCoordsForWindow(aAccessible);
      *aX -= coords.x;
      *aY -= coords.y;
      break;
    }

    case nsIAccessibleCoordinateType::COORDTYPE_PARENT_RELATIVE: {
      LayoutDeviceIntPoint coords = GetScreenCoordsForParent(aAccessible);
      *aX -= coords.x;
      *aY -= coords.y;
      break;
    }

    default:
      MOZ_ASSERT_UNREACHABLE("invalid coord type!");
  }
}

LayoutDeviceIntPoint nsAccUtils::GetScreenCoordsForParent(
    Accessible* aAccessible) {
  if (!aAccessible) return LayoutDeviceIntPoint();

  if (Accessible* parent = aAccessible->Parent()) {
    LayoutDeviceIntRect parentBounds = parent->Bounds();
    // The rect returned from Bounds() is already in dev
    // pixels, so we don't need to do any conversion here.
    return parentBounds.TopLeft();
  }

  return LayoutDeviceIntPoint();
}

LayoutDeviceIntPoint nsAccUtils::GetScreenCoordsForWindow(
    Accessible* aAccessible) {
  LayoutDeviceIntPoint coords(0, 0);
  a11y::LocalAccessible* localAcc = aAccessible->AsLocal();
  if (!localAcc) {
    localAcc = aAccessible->AsRemote()->OuterDocOfRemoteBrowser();
    if (!localAcc) {
      // This could be null if the tab is closing but the document is still
      // being shut down.
      return coords;
    }
  }

  nsCOMPtr<nsIDocShellTreeItem> treeItem(
      nsCoreUtils::GetDocShellFor(localAcc->GetNode()));
  if (!treeItem) return coords;

  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  treeItem->GetTreeOwner(getter_AddRefs(treeOwner));
  if (!treeOwner) return coords;

  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(treeOwner);
  if (baseWindow) {
    baseWindow->GetPosition(&coords.x.value,
                            &coords.y.value);  // in device pixels
  }

  return coords;
}

bool nsAccUtils::GetLiveAttrValue(uint32_t aRule, nsAString& aValue) {
  switch (aRule) {
    case eOffLiveAttr:
      aValue = u"off"_ns;
      return true;
    case ePoliteLiveAttr:
      aValue = u"polite"_ns;
      return true;
    case eAssertiveLiveAttr:
      aValue = u"assertive"_ns;
      return true;
  }

  return false;
}

#ifdef DEBUG

bool nsAccUtils::IsTextInterfaceSupportCorrect(LocalAccessible* aAccessible) {
  // Don't test for accessible docs, it makes us create accessibles too
  // early and fire mutation events before we need to
  if (aAccessible->IsDoc()) return true;

  bool foundText = false;
  uint32_t childCount = aAccessible->ChildCount();
  for (uint32_t childIdx = 0; childIdx < childCount; childIdx++) {
    LocalAccessible* child = aAccessible->LocalChildAt(childIdx);
    if (child && child->IsText()) {
      foundText = true;
      break;
    }
  }

  return !foundText || aAccessible->IsHyperText();
}
#endif

uint32_t nsAccUtils::TextLength(Accessible* aAccessible) {
  if (!aAccessible->IsText()) {
    return 1;
  }

  if (LocalAccessible* localAcc = aAccessible->AsLocal()) {
    TextLeafAccessible* textLeaf = localAcc->AsTextLeaf();
    if (textLeaf) {
      return textLeaf->Text().Length();
    }
  } else if (aAccessible->IsText()) {
    RemoteAccessible* remoteAcc = aAccessible->AsRemote();
    MOZ_ASSERT(remoteAcc);
    return remoteAcc->GetCachedTextLength();
  }

  // For list bullets (or anything other accessible which would compute its own
  // text. They don't have their own frame.
  // XXX In the future, list bullets may have frame and anon content, so
  // we should be able to remove this at that point
  nsAutoString text;
  aAccessible->AppendTextTo(text);  // Get all the text
  return text.Length();
}

bool nsAccUtils::MustPrune(Accessible* aAccessible) {
  MOZ_ASSERT(aAccessible);
  roles::Role role = aAccessible->Role();

  if (role == roles::SLIDER || role == roles::PROGRESSBAR ||
      role == roles::METER) {
    // Always prune the tree for sliders and progressbars, as it doesn't make
    // sense for either to have descendants. Per the ARIA spec, children of
    // these elements are presentational. They also confuse NVDA.
    return true;
  }

  if (role != roles::MENUITEM && role != roles::COMBOBOX_OPTION &&
      role != roles::OPTION && role != roles::ENTRY &&
      role != roles::FLAT_EQUATION && role != roles::PASSWORD_TEXT &&
      role != roles::PUSHBUTTON && role != roles::TOGGLE_BUTTON &&
      role != roles::GRAPHIC && role != roles::SEPARATOR) {
    // If it doesn't match any of these roles, don't prune its children.
    return false;
  }

  if (aAccessible->ChildCount() != 1) {
    // If the accessible has more than one child, don't prune it.
    return false;
  }

  roles::Role childRole = aAccessible->FirstChild()->Role();
  // If the accessible's child is a text leaf, prune the accessible.
  return childRole == roles::TEXT_LEAF || childRole == roles::STATICTEXT;
}

void nsAccUtils::GetLiveRegionSetting(Accessible* aAcc, nsAString& aLive) {
  MOZ_ASSERT(aAcc);
  aAcc->LiveRegionAttributes(&aLive, nullptr, nullptr, nullptr);
  // aria-live wasn't explicitly set. See if an aria-live value is implied
  // by an ARIA role or markup element.
  if (const nsRoleMapEntry* roleMap = aAcc->ARIARoleMap()) {
    GetLiveAttrValue(roleMap->liveAttRule, aLive);
  } else if (nsStaticAtom* value = GetAccService()
                                       ? GetAccService()->MarkupAttribute(
                                             aAcc, nsGkAtoms::aria_live)
                                       : nullptr) {
    value->ToString(aLive);
  }
}

Accessible* nsAccUtils::GetLiveRegionRoot(Accessible* aAcc) {
  MOZ_ASSERT(aAcc);
  nsAutoString live;
  Accessible* acc;
  for (acc = aAcc; acc; acc = acc->Parent()) {
    GetLiveRegionSetting(acc, live);
    if (!live.IsEmpty()) {
      break;
    }
    if (acc->IsDoc()) {
      // A document can be the root of a live region, but a live region cannot
      // cross document boundaries.
      return nullptr;
    }
  }
  if (live.IsEmpty() || live.EqualsLiteral("off")) {
    return nullptr;
  }
  return acc;
}

Accessible* nsAccUtils::DocumentFor(Accessible* aAcc) {
  if (!aAcc) {
    return nullptr;
  }
  if (LocalAccessible* localAcc = aAcc->AsLocal()) {
    return localAcc->Document();
  }
  return aAcc->AsRemote()->Document();
}

Accessible* nsAccUtils::GetAccessibleByID(Accessible* aDoc, uint64_t aID) {
  if (!aDoc) {
    return nullptr;
  }
  if (LocalAccessible* localAcc = aDoc->AsLocal()) {
    if (DocAccessible* doc = localAcc->AsDoc()) {
      if (!aID) {
        // GetAccessibleByUniqueID doesn't treat 0 as the document.
        return aDoc;
      }
      return doc->GetAccessibleByUniqueID(
          reinterpret_cast<void*>(static_cast<uintptr_t>(aID)));
    }
  } else if (DocAccessibleParent* doc = aDoc->AsRemote()->AsDoc()) {
    return doc->GetAccessible(aID);
  }
  return nullptr;
}

void nsAccUtils::DocumentURL(Accessible* aDoc, nsAString& aURL) {
  MOZ_ASSERT(aDoc && aDoc->IsDoc());
  if (LocalAccessible* localAcc = aDoc->AsLocal()) {
    return localAcc->AsDoc()->URL(aURL);
  }
  return aDoc->AsRemote()->AsDoc()->URL(aURL);
}

void nsAccUtils::DocumentMimeType(Accessible* aDoc, nsAString& aMimeType) {
  MOZ_ASSERT(aDoc && aDoc->IsDoc());
  if (LocalAccessible* localAcc = aDoc->AsLocal()) {
    return localAcc->AsDoc()->MimeType(aMimeType);
  }
  return aDoc->AsRemote()->AsDoc()->MimeType(aMimeType);
}

// ARIA Accessibility Default Accessors
const AttrArray* nsAccUtils::GetARIADefaults(dom::Element* aElement) {
  auto* element = nsGenericHTMLElement::FromNode(aElement);
  if (!element) {
    return nullptr;
  }
  auto* internals = element->GetInternals();
  if (!internals) {
    return nullptr;
  }
  return &internals->GetAttrs();
}

bool nsAccUtils::HasARIAAttr(dom::Element* aElement, const nsAtom* aName) {
  if (aElement->HasAttr(aName)) {
    return true;
  }
  const auto* defaults = GetARIADefaults(aElement);
  if (!defaults) {
    return false;
  }
  return defaults->HasAttr(aName);
}

bool nsAccUtils::GetARIAAttr(dom::Element* aElement, const nsAtom* aName,
                             nsAString& aResult) {
  if (aElement->GetAttr(aName, aResult)) {
    return true;
  }
  const auto* defaults = GetARIADefaults(aElement);
  if (!defaults) {
    return false;
  }
  return defaults->GetAttr(aName, aResult);
}

const nsAttrValue* nsAccUtils::GetARIAAttr(dom::Element* aElement,
                                           const nsAtom* aName) {
  if (const auto* val = aElement->GetParsedAttr(aName, kNameSpaceID_None)) {
    return val;
  }
  const auto* defaults = GetARIADefaults(aElement);
  if (!defaults) {
    return nullptr;
  }
  return defaults->GetAttr(aName, kNameSpaceID_None);
}

bool nsAccUtils::GetARIAElementsAttr(dom::Element* aElement, nsAtom* aName,
                                     nsTArray<dom::Element*>& aElements) {
  if (aElement->HasAttr(aName)) {
    aElement->GetExplicitlySetAttrElements(aName, aElements);
    return true;
  }

  if (auto* element = nsGenericHTMLElement::FromNode(aElement)) {
    if (auto* internals = element->GetInternals()) {
      return internals->GetAttrElements(aName, aElements);
    }
  }

  return false;
}

bool nsAccUtils::ARIAAttrValueIs(dom::Element* aElement, const nsAtom* aName,
                                 const nsAString& aValue,
                                 nsCaseTreatment aCaseSensitive) {
  if (aElement->HasAttr(kNameSpaceID_None, aName)) {
    return aElement->AttrValueIs(kNameSpaceID_None, aName, aValue,
                                 aCaseSensitive);
  }
  const auto* defaults = GetARIADefaults(aElement);
  if (!defaults) {
    return false;
  }
  return defaults->AttrValueIs(kNameSpaceID_None, aName, aValue,
                               aCaseSensitive);
}

bool nsAccUtils::ARIAAttrValueIs(dom::Element* aElement, const nsAtom* aName,
                                 const nsAtom* aValue,
                                 nsCaseTreatment aCaseSensitive) {
  if (aElement->HasAttr(kNameSpaceID_None, aName)) {
    return aElement->AttrValueIs(kNameSpaceID_None, aName, aValue,
                                 aCaseSensitive);
  }
  const auto* defaults = GetARIADefaults(aElement);
  if (!defaults) {
    return false;
  }
  return defaults->AttrValueIs(kNameSpaceID_None, aName, aValue,
                               aCaseSensitive);
}

int32_t nsAccUtils::FindARIAAttrValueIn(dom::Element* aElement,
                                        const nsAtom* aName,
                                        AttrArray::AttrValuesArray* aValues,
                                        nsCaseTreatment aCaseSensitive) {
  int32_t index = aElement->FindAttrValueIn(kNameSpaceID_None, aName, aValues,
                                            aCaseSensitive);
  if (index == AttrArray::ATTR_MISSING) {
    const auto* defaults = GetARIADefaults(aElement);
    if (!defaults) {
      return index;
    }
    index = defaults->FindAttrValueIn(kNameSpaceID_None, aName, aValues,
                                      aCaseSensitive);
  }
  return index;
}

bool nsAccUtils::IsEditableARIACombobox(const LocalAccessible* aAccessible) {
  const nsRoleMapEntry* roleMap = aAccessible->ARIARoleMap();
  if (!roleMap || roleMap->role != roles::EDITCOMBOBOX) {
    return false;
  }

  return aAccessible->IsTextField() ||
         aAccessible->Elm()->State().HasState(dom::ElementState::READWRITE);
}
