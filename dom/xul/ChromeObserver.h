/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ChromeObserver_h
#define mozilla_dom_ChromeObserver_h

#include "nsStubMutationObserver.h"

class nsIWidget;

namespace mozilla::dom {
class Document;
class Element;

class ChromeObserver final : public nsStubMutationObserver {
 public:
  NS_DECL_ISUPPORTS

  explicit ChromeObserver(Document* aDocument);
  void Init();

  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

 protected:
  already_AddRefed<nsIWidget> GetWindowWidget();
  void SetHideTitlebarSeparator(bool);
  void HideWindowChrome(bool aShouldHide);
  void SetCustomTitlebar(bool);
  void SetMica(bool);
  void RecomputeIcon(Element&);
  ~ChromeObserver() = default;
  // A weak pointer cleared when the element will be destroyed.
  Document* MOZ_NON_OWNING_REF mDocument;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ChromeObserver_h
