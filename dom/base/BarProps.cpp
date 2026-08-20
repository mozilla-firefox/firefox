/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BarProps.h"

#include "mozilla/dom/BarPropBinding.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"
#include "nsIWebBrowserChrome.h"
#include "nsPIDOMWindowInlines.h"

namespace mozilla::dom {

//
//  Basic (virtual) BarProp class implementation
//
BarProp::BarProp(nsGlobalWindowInner* aWindow) : mDOMWindow(aWindow) {}

BarProp::~BarProp() = default;

nsPIDOMWindowInner* BarProp::GetParentObject() const { return mDOMWindow; }

JSObject* BarProp::WrapObject(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) {
  return BarProp_Binding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(BarProp, mDOMWindow)
NS_IMPL_CYCLE_COLLECTING_ADDREF(BarProp)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BarProp)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BarProp)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

bool BarProp::GetVisibleByIsPopup() {
  // For web content, return the value defined by the spec, instead of
  // the actual visibility of each bar.
  //
  // If this browsing context is created by requesting a popup, all
  // `BarProp.visible` should return false.
  RefPtr<BrowsingContext> bc = GetBrowsingContext();
  if (!bc || bc->IsDiscarded()) {
    return true;
  }
  bc = bc->Top();
  bool isPopup = bc->GetIsPopupRequested();
  return !isPopup;
}

bool BarProp::GetVisibleByFlag(uint32_t aChromeFlag, CallerType aCallerType,
                               ErrorResult& aRv) {
  if (aCallerType != CallerType::System) {
    return GetVisibleByIsPopup();
  }

  nsCOMPtr<nsIWebBrowserChrome> browserChrome = GetBrowserChrome();
  if (!browserChrome) {
    return false;
  }

  uint32_t chromeFlags;

  if (NS_FAILED(browserChrome->GetChromeFlags(&chromeFlags))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  return (chromeFlags & aChromeFlag);
}

already_AddRefed<nsIWebBrowserChrome> BarProp::GetBrowserChrome() {
  if (!mDOMWindow) {
    return nullptr;
  }

  return mDOMWindow->GetWebBrowserChrome();
}

BrowsingContext* BarProp::GetBrowsingContext() {
  if (!mDOMWindow) {
    return nullptr;
  }

  return mDOMWindow->GetBrowsingContext();
}

//
// MenubarProp class implementation
//

MenubarProp::MenubarProp(nsGlobalWindowInner* aWindow) : BarProp(aWindow) {}

MenubarProp::~MenubarProp() = default;

bool MenubarProp::GetVisible(CallerType aCallerType, ErrorResult& aRv) {
  return BarProp::GetVisibleByFlag(nsIWebBrowserChrome::CHROME_MENUBAR,
                                   aCallerType, aRv);
}

//
// ToolbarProp class implementation
//

ToolbarProp::ToolbarProp(nsGlobalWindowInner* aWindow) : BarProp(aWindow) {}

ToolbarProp::~ToolbarProp() = default;

bool ToolbarProp::GetVisible(CallerType aCallerType, ErrorResult& aRv) {
  return BarProp::GetVisibleByFlag(nsIWebBrowserChrome::CHROME_TOOLBAR,
                                   aCallerType, aRv);
}

//
// LocationbarProp class implementation
//

LocationbarProp::LocationbarProp(nsGlobalWindowInner* aWindow)
    : BarProp(aWindow) {}

LocationbarProp::~LocationbarProp() = default;

bool LocationbarProp::GetVisible(CallerType aCallerType, ErrorResult& aRv) {
  if (aCallerType != CallerType::System) {
    return GetVisibleByIsPopup();
  }
  return true;
}

//
// PersonalbarProp class implementation
//

PersonalbarProp::PersonalbarProp(nsGlobalWindowInner* aWindow)
    : BarProp(aWindow) {}

PersonalbarProp::~PersonalbarProp() = default;

bool PersonalbarProp::GetVisible(CallerType aCallerType, ErrorResult& aRv) {
  return BarProp::GetVisibleByFlag(nsIWebBrowserChrome::CHROME_PERSONAL_TOOLBAR,
                                   aCallerType, aRv);
}

//
// StatusbarProp class implementation
//

StatusbarProp::StatusbarProp(nsGlobalWindowInner* aWindow) : BarProp(aWindow) {}

StatusbarProp::~StatusbarProp() = default;

bool StatusbarProp::GetVisible(CallerType, ErrorResult&) {
  return BarProp::GetVisibleByIsPopup();
}

//
// ScrollbarsProp class implementation
//

ScrollbarsProp::ScrollbarsProp(nsGlobalWindowInner* aWindow)
    : BarProp(aWindow) {}

ScrollbarsProp::~ScrollbarsProp() = default;

bool ScrollbarsProp::GetVisible(CallerType aCallerType, ErrorResult& aRv) {
  if (aCallerType != CallerType::System) {
    return BarProp::GetVisibleByIsPopup();
  }

  if (!mDOMWindow) {
    return true;
  }

  nsIDocShell* ds = mDOMWindow->GetDocShell();
  if (!ds) {
    return true;
  }

  ScrollbarPreference pref = nsDocShell::Cast(ds)->ScrollbarPreference();
  return pref != ScrollbarPreference::Never;
}

}  // namespace mozilla::dom
