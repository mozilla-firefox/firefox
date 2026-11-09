/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsChromeTreeOwner_h_
#define nsChromeTreeOwner_h_

// Helper Classes
#include "nsCOMPtr.h"

// Interfaces Needed
#include "nsIBaseWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsWeakReference.h"

namespace mozilla {
class AppWindow;
}

class nsChromeTreeOwner : public nsIDocShellTreeOwner,
                          public nsIBaseWindow,
                          public nsIInterfaceRequestor,
                          public nsSupportsWeakReference {
  friend class mozilla::AppWindow;

 public:
  NS_DECL_ISUPPORTS

  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIBASEWINDOW
  NS_DECL_NSIDOCSHELLTREEOWNER

 protected:
  nsChromeTreeOwner();
  virtual ~nsChromeTreeOwner();

  void AppWindow(mozilla::AppWindow* aAppWindow);

  mozilla::AppWindow* mAppWindow = nullptr;
};

#endif /* nsChromeTreeOwner_h_ */
