/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AboutWindowsMessages.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/StaticPtr.h"
#include "nsThreadUtils.h"
#include "nsPIDOMWindow.h"
#include "nsServiceManagerUtils.h"
#include "nsIWindowMediator.h"
#include "nsDocShell.h"
#include "nsPIDOMWindowInlines.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/widget/nsWindowLoggedMessages.h"

namespace mozilla {

static StaticRefPtr<AboutWindowsMessages> sSingleton;

NS_IMPL_ISUPPORTS(AboutWindowsMessages, nsIAboutWindowsMessages);

/*static*/
already_AddRefed<AboutWindowsMessages> AboutWindowsMessages::GetSingleton() {
  if (!sSingleton) {
    sSingleton = new AboutWindowsMessages;
    ClearOnShutdown(&sSingleton);
  }

  return do_AddRef(sSingleton);
}

static bool GetWindowTitleFromWindow(const nsCOMPtr<nsPIDOMWindowOuter>& window,
                                     nsAutoString& title) {
  nsDocShell* docShell = nsDocShell::Cast(window->GetDocShell());
  if (!docShell) {
    return false;
  }
  docShell->GetTitle(title);
  return true;
}

NS_IMETHODIMP
AboutWindowsMessages::GetMessages(mozIDOMWindowProxy* currentWindow,
                                  nsTArray<nsTArray<nsCString>>& messages,
                                  nsTArray<nsString>& windowTitles) {
  messages.Clear();
  // Display the current window's messages first
  auto currentWindowOuter = nsPIDOMWindowOuter::From(currentWindow);
  RefPtr<nsIWidget> currentWidget =
      widget::WidgetUtils::DOMWindowToWidget(currentWindowOuter);
  nsAutoString currentWindowTitle;
  if (GetWindowTitleFromWindow(currentWindowOuter, currentWindowTitle)) {
    nsTArray<nsCString> windowMessages;
    widget::GetLatestWindowMessages(currentWidget, windowMessages);
    windowTitles.AppendElement(currentWindowTitle);
    messages.EmplaceBack(std::move(windowMessages));
  }
  nsCOMPtr<nsIWindowMediator> mediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  nsCOMPtr<nsISimpleEnumerator> windowEnumerator;
  mediator->GetEnumerator(nullptr, getter_AddRefs(windowEnumerator));
  if (!windowEnumerator) return NS_ERROR_FAILURE;
  bool more;
  while (NS_SUCCEEDED(windowEnumerator->HasMoreElements(&more)) && more) {
    nsCOMPtr<nsISupports> isupports;
    if (NS_FAILED(windowEnumerator->GetNext(getter_AddRefs(isupports)))) break;
    nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(isupports);
    NS_ASSERTION(window, "not an nsPIDOMWindowOuter");
    RefPtr<nsIWidget> windowWidget =
        widget::WidgetUtils::DOMWindowToWidget(window);
    if (&*windowWidget != &*currentWidget) {
      nsAutoString title;
      if (GetWindowTitleFromWindow(window, title)) {
        nsTArray<nsCString> windowMessages;
        widget::GetLatestWindowMessages(windowWidget, windowMessages);
        windowTitles.AppendElement(title);
        messages.EmplaceBack(std::move(windowMessages));
      }
    }
  }
  return NS_OK;
}
}  // namespace mozilla
