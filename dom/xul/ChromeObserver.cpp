/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "ChromeObserver.h"

#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "nsContentUtils.h"
#include "nsIBaseWindow.h"
#include "nsIFrame.h"
#include "nsIMutationObserver.h"
#include "nsIWidget.h"
#include "nsPresContext.h"
#include "nsXULElement.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(ChromeObserver, nsIMutationObserver)

ChromeObserver::ChromeObserver(Document* aDocument) : mDocument(aDocument) {}

void ChromeObserver::Init() {
  mDocument->AddMutationObserver(this);
  Element* rootElement = mDocument->GetRootElement();
  if (!rootElement) {
    return;
  }
  nsAutoScriptBlocker scriptBlocker;
  uint32_t i = 0;
  while (BorrowedAttrInfo info = rootElement->GetAttrInfoAt(i++)) {
    const nsAttrName* name = info.mName;
    AttributeChanged(rootElement, name->NamespaceID(), name->LocalName(),
                     AttrModType::Addition, nullptr);
  }
}

already_AddRefed<nsIWidget> ChromeObserver::GetWindowWidget() {
  // only top level chrome documents can set the titlebar color
  if (mDocument && mDocument->IsRootDisplayDocument()) {
    nsCOMPtr<nsISupports> container = mDocument->GetContainer();
    if (nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(container)) {
      nsCOMPtr<nsIWidget> mainWidget = baseWindow->GetMainWidget();
      return mainWidget.forget();
    }
  }
  return nullptr;
}

void ChromeObserver::SetHideTitlebarSeparator(bool aState) {
  if (RefPtr mainWidget = GetWindowWidget()) {
    // We can do this synchronously because SetHideTitlebarSeparator doesn't
    // have any synchronous effects apart from a harmless invalidation.
    mainWidget->SetHideTitlebarSeparator(aState);
  }
}

void ChromeObserver::AttributeChanged(dom::Element* aElement,
                                      int32_t aNamespaceID, nsAtom* aName,
                                      AttrModType aModType,
                                      const nsAttrValue* aOldValue) {
  // We only care about changes to the root element.
  if (!mDocument || aElement != mDocument->GetRootElement()) {
    return;
  }

  if (IsAdditionOrRemoval(aModType)) {
    const bool added = aModType == AttrModType::Addition;
    if (aName == nsGkAtoms::hidechrome) {
      HideWindowChrome(added);
    } else if (aName == nsGkAtoms::customtitlebar) {
      SetCustomTitlebar(added);
    } else if (aName == nsGkAtoms::hidetitlebarseparator) {
      SetHideTitlebarSeparator(added);
    } else if (aName == nsGkAtoms::windowsmica) {
      SetMica(added);
    }
  }
  if (aName == nsGkAtoms::icon) {
    RecomputeIcon(*aElement);
  }
  if (aName == nsGkAtoms::localedir) {
    // if the localedir changed on the root element, reset the document
    // direction
    mDocument->ResetDocumentDirection();
  }
  if (aName == nsGkAtoms::title && aModType != AttrModType::Removal) {
    mDocument->NotifyPossibleTitleChange(false);
  }
}

void ChromeObserver::NodeWillBeDestroyed(nsINode* aNode) {
  mDocument = nullptr;
}

void ChromeObserver::RecomputeIcon(Element& aElement) {
  if (RefPtr mainWidget = GetWindowWidget()) {
    nsAutoString value;
    // TODO(emilio): Probably add support for restoring the default icon?
    if (aElement.GetAttr(nsGkAtoms::icon, value)) {
      mainWidget->SetIcon(value);
    }
  }
}

void ChromeObserver::SetMica(bool aEnable) {
  if (RefPtr mainWidget = GetWindowWidget()) {
    mainWidget->SetMicaBackdrop(aEnable);
  }
}

void ChromeObserver::SetCustomTitlebar(bool aCustomTitlebar) {
  if (RefPtr mainWidget = GetWindowWidget()) {
    // SetCustomTitlebar can dispatch native events, hence doing it off a
    // script runner
    nsContentUtils::AddScriptRunner(NewRunnableMethod<bool>(
        "SetCustomTitlebar", mainWidget, &nsIWidget::SetCustomTitlebar,
        aCustomTitlebar));
  }
}

void ChromeObserver::HideWindowChrome(bool aShouldHide) {
  if (RefPtr mainWidget = GetWindowWidget()) {
    mainWidget->HideWindowChrome(aShouldHide);
  }
}

}  // namespace mozilla::dom
