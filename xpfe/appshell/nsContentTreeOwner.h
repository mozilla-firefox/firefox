/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsContentTreeOwner_h_
#define nsContentTreeOwner_h_

// Helper Classes
#include "nsCOMPtr.h"
#include "nsString.h"

// Interfaces Needed
#include "nsIBaseWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWindowProvider.h"
#include "nsWeakReference.h"

namespace mozilla {
class AppWindow;
}

class nsContentTreeOwner final : public nsIDocShellTreeOwner,
                                 public nsIBaseWindow,
                                 public nsIInterfaceRequestor,
                                 public nsIWebBrowserChrome,
                                 public nsIWindowProvider,
                                 public nsSupportsWeakReference {
  friend class mozilla::AppWindow;

 public:
  NS_DECL_ISUPPORTS

  NS_DECL_NSIBASEWINDOW
  NS_DECL_NSIDOCSHELLTREEOWNER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWINDOWPROVIDER

  /* nsIWebBrowserChrome (SetDimensions overlaps with nsIBaseWindow) */
  NS_IMETHOD SetLinkStatus(const nsAString& status) override;
  NS_IMETHOD GetChromeFlags(uint32_t* aChromeFlags) override;
  NS_IMETHOD ShowAsModal() override;
  NS_IMETHOD IsWindowModal(bool* _retval) override;
  NS_IMETHOD GetDimensions(int32_t* aX, int32_t* aY, int32_t* aCX,
                           int32_t* aCY) override;
  NS_IMETHOD Blur() override;

 protected:
  nsContentTreeOwner();
  virtual ~nsContentTreeOwner() = default;

  void AppWindow(mozilla::AppWindow* aAppWindow);
  mozilla::AppWindow* AppWindow();

 protected:
  mozilla::AppWindow* mAppWindow;
};

#endif /* nsContentTreeOwner_h_ */
