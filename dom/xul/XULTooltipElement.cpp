/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XULTooltipElement.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/NodeInfo.h"
#include "nsCOMPtr.h"
#include "nsCTooltipTextProvider.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsITooltipTextProvider.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

nsXULElement* NS_NewXULTooltipElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  RefPtr<XULTooltipElement> tooltip =
      new (nim) XULTooltipElement(nodeInfo.forget());
  NS_ENSURE_SUCCESS(tooltip->Init(), nullptr);
  return tooltip;
}

nsresult XULTooltipElement::Init() {
  // Create the default child label node that will contain the text of the
  // tooltip.
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfo->NodeInfoManager()->GetNodeInfo(
      nsGkAtoms::description, nullptr, kNameSpaceID_XUL, nsINode::ELEMENT_NODE);
  nsCOMPtr<Element> description;
  nsresult rv = NS_NewXULElement(getter_AddRefs(description), nodeInfo.forget(),
                                 dom::NOT_FROM_PARSER);
  NS_ENSURE_SUCCESS(rv, rv);
  description->SetAttr(kNameSpaceID_None, nsGkAtoms::_class,
                       u"tooltip-label"_ns, false, IsKnownNewAttr::Yes);
  ErrorResult error;
  AppendChild(*description, error);

  return error.StealNSResult();
}

void XULTooltipElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::label) {
    // When the label attribute of this node changes propagate the text down
    // into child description element.
    nsCOMPtr<nsIContent> description = GetFirstChild();
    if (description && description->IsXULElement(nsGkAtoms::description)) {
      nsAutoString value;
      if (aValue) {
        aValue->ToString(value);
      }
      nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
          "XULTooltipElement::AfterSetAttr",
          [description, value = std::move(value)]() {
            Element* descriptionElement = description->AsElement();
            descriptionElement->SetTextContent(value, IgnoreErrors());
          }));
    }
  }
  return nsXULElement::AfterSetAttr(aNameSpaceID, aName, aValue, aOldValue,
                                    aSubjectPrincipal, aNotify);
}

static bool WantsPostHandleEvent(EventChainVisitor& aVisitor) {
  return aVisitor.mEvent->mMessage == eXULPopupShowing &&
         aVisitor.mEvent->IsTrusted();
}

void XULTooltipElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mWantsPostHandleEvent = WantsPostHandleEvent(aVisitor);
  XULPopupElement::GetEventTargetParent(aVisitor);
}

nsresult XULTooltipElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  MOZ_ASSERT(WantsPostHandleEvent(aVisitor));

  if (!aVisitor.mEvent->DefaultPrevented() &&
      AttrValueIs(kNameSpaceID_None, nsGkAtoms::page, nsGkAtoms::_true,
                  eCaseMatters) &&
      !AttrValueIs(kNameSpaceID_None, nsGkAtoms::titletip, nsGkAtoms::_true,
                   eCaseMatters)) {
    // When the tooltip node has the "page" attribute set to "true" the
    // tooltip text provider is used to find the tooltip text from page where
    // mouse is hovering over.
    nsCOMPtr<nsITooltipTextProvider> textProvider =
        do_GetService(NS_DEFAULTTOOLTIPTEXTPROVIDER_CONTRACTID);
    nsString text;
    nsString direction;
    bool shouldChange = false;
    if (textProvider) {
      textProvider->GetNodeText(GetTriggerNode(), getter_Copies(text),
                                getter_Copies(direction), &shouldChange);
    }
    if (shouldChange) {
      SetAttr(kNameSpaceID_None, nsGkAtoms::label, text, true,
              IsKnownNewAttr::No);
      SetAttr(kNameSpaceID_None, nsGkAtoms::direction, direction, true,
              IsKnownNewAttr::No);
    } else {
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      aVisitor.mEvent->PreventDefault();
    }
  }
  return NS_OK;
}

}  // namespace mozilla::dom
