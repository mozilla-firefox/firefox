/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDocShellTreeOwner_h_
#define nsDocShellTreeOwner_h_

// Helper Classes
#include "nsCOMPtr.h"
#include "nsString.h"

// Interfaces Needed
#include "nsIBaseWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWebBrowserChrome.h"
#include "nsIDOMEventListener.h"
#include "nsIWebProgressListener.h"
#include "nsWeakReference.h"
#include "nsITimer.h"
#include "nsIPrompt.h"
#include "nsIAuthPrompt.h"
#include "nsITooltipTextProvider.h"
#include "nsCTooltipTextProvider.h"

namespace mozilla {
namespace dom {
class Event;
class EventTarget;
}  // namespace dom
}  // namespace mozilla

class nsIDocShellTreeItem;
class nsWebBrowser;
class ChromeTooltipListener;

class nsDocShellTreeOwner final : public nsIDocShellTreeOwner,
                                  public nsIBaseWindow,
                                  public nsIInterfaceRequestor,
                                  public nsIWebProgressListener,
                                  public nsIDOMEventListener,
                                  public nsSupportsWeakReference {
  friend class nsWebBrowser;

 public:
  NS_DECL_ISUPPORTS

  NS_DECL_NSIBASEWINDOW
  NS_DECL_NSIDOCSHELLTREEOWNER
  NS_DECL_NSIDOMEVENTLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWEBPROGRESSLISTENER

 protected:
  nsDocShellTreeOwner();
  virtual ~nsDocShellTreeOwner();

  void WebBrowser(nsWebBrowser* aWebBrowser);

  nsWebBrowser* WebBrowser();
  NS_IMETHOD SetWebBrowserChrome(nsIWebBrowserChrome* aWebBrowserChrome);

  NS_IMETHOD AddChromeListeners();
  NS_IMETHOD RemoveChromeListeners();

  void EnsurePrompter();
  void EnsureAuthPrompter();

  void AddToWatcher();
  void RemoveFromWatcher();

  void EnsureContentTreeOwner();

  // These helper functions return the correct instances of the requested
  // interfaces.  If the object passed to SetWebBrowserChrome() implements
  // nsISupportsWeakReference, then these functions call QueryReferent on
  // that object.  Otherwise, they return an addrefed pointer.  If the
  // WebBrowserChrome object doesn't exist, they return nullptr.
  already_AddRefed<nsIWebBrowserChrome> GetWebBrowserChrome();
  already_AddRefed<nsIBaseWindow> GetOwnerWin();
  already_AddRefed<nsIInterfaceRequestor> GetOwnerRequestor();

 protected:
  // Weak References
  nsWebBrowser* mWebBrowser;
  nsIDocShellTreeItem* mPrimaryContentShell;

  nsIWebBrowserChrome* mWebBrowserChrome;
  nsIBaseWindow* mOwnerWin;
  nsIInterfaceRequestor* mOwnerRequestor;

  nsWeakPtr mWebBrowserChromeWeak;  // nsIWebBrowserChrome

  // the objects that listen for chrome events like context menus and tooltips.
  // They are separate objects to avoid circular references between |this|
  // and the DOM.
  RefPtr<ChromeTooltipListener> mChromeTooltipListener;

  RefPtr<nsDocShellTreeOwner> mContentTreeOwner;

  nsCOMPtr<nsIPrompt> mPrompter;
  nsCOMPtr<nsIAuthPrompt> mAuthPrompter;
  nsCOMPtr<nsIRemoteTab> mPrimaryRemoteTab;
};

#endif /* nsDocShellTreeOwner_h_ */
