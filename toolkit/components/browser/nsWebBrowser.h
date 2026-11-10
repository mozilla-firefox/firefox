/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowser_h_
#define nsWebBrowser_h_

// Core Includes
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"

// Interfaces needed
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWidget.h"
#include "nsIWebProgress.h"
#include "nsIWebNavigation.h"
#include "nsIWebProgressListener.h"
#include "nsIDOMEventListener.h"
#include "nsIWebBrowserChrome.h"
#include "nsIPrompt.h"
#include "nsIAuthPrompt.h"
#include "nsIWindowWatcher.h"
#include "nsIPrintSettings.h"
#include "nsIWidgetListener.h"

#include "nsTArray.h"
#include "nsIWeakReferenceUtils.h"

class nsIOpenWindowInfo;

class mozIDOMWindowProxy;
class nsDocShell;
class nsIRemoteTab;
class ChromeTooltipListener;

namespace mozilla {
namespace dom {
class WindowGlobalChild;
}  // namespace dom
}  // namespace mozilla

class nsWebBrowser final : public nsIWebNavigation,
                           public nsIDocShellTreeItem,
                           public nsIDocShellTreeOwner,
                           public nsIBaseWindow,
                           public nsIInterfaceRequestor,
                           public nsIWebProgressListener,
                           public nsIDOMEventListener,
                           public nsSupportsWeakReference {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsWebBrowser, nsIWebNavigation)

  NS_DECL_NSIBASEWINDOW
  NS_DECL_NSIDOCSHELLTREEITEM
  NS_DECL_NSIDOCSHELLTREEOWNER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWEBNAVIGATION
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIDOMEVENTLISTENER

  nsDocShell* GetDocShell() const { return mDocShell; }
  void Close() { InternalDestroy(); }
  void SetWebBrowserChrome(nsIWebBrowserChrome* aWebBrowserChrome);
  void SetAllowDNSPrefetch(bool aAllowPrefetch);
  // TODO: Convert FocusDeactivate() to MOZ_CAN_RUN_SCRIPT
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FocusActivate(uint64_t aActionId);
  // TODO: Convert FocusDeactivate() to MOZ_CAN_RUN_SCRIPT
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FocusDeactivate(uint64_t aActionId);
  void SetWillChangeProcess();

  static nsresult Create(nsIWebBrowserChrome* aContainerWindow,
                         nsIWidget* aParentWidget,
                         mozilla::dom::BrowsingContext* aBrowsingContext,
                         mozilla::dom::WindowGlobalChild* aInitialWindowChild,
                         nsIOpenWindowInfo* aOpenWindowInfo,
                         nsWebBrowser** aWebBrowser);

 protected:
  virtual ~nsWebBrowser();
  void InternalDestroy();

  void SetDocShell(nsDocShell* aDocShell);

  nsIWidget* EnsureWidget();
  explicit nsWebBrowser(int aItemType);

  nsresult AddChromeListeners();
  nsresult RemoveChromeListeners();

  void EnsurePrompter();
  void EnsureAuthPrompter();

  void AddToWatcher();
  void RemoveFromWatcher();

  // These helper functions return the correct instances of the requested
  // interfaces.  If the object passed to SetWebBrowserChrome() implements
  // nsISupportsWeakReference, then these functions call QueryReferent on
  // that object.  Otherwise, they return an addrefed pointer.  If the
  // WebBrowserChrome object doesn't exist, they return nullptr.
  already_AddRefed<nsIWebBrowserChrome> GetWebBrowserChrome();
  already_AddRefed<nsIBaseWindow> GetOwnerWin();
  already_AddRefed<nsIInterfaceRequestor> GetOwnerRequestor();

 protected:
  RefPtr<nsDocShell> mDocShell;

  const uint32_t mContentType;
  bool mWillChangeProcess;

  // Weak Reference interfaces...
  nsIWidget* mParentWidget;
  nsIDocShellTreeItem* mPrimaryContentShell;

  nsIWebBrowserChrome* mWebBrowserChrome;
  nsIBaseWindow* mOwnerWin;
  nsIInterfaceRequestor* mOwnerRequestor;

  nsWeakPtr mWebBrowserChromeWeak;  // nsIWebBrowserChrome

  // Listens for the events needed to show tooltips in the embedding chrome.
  RefPtr<ChromeTooltipListener> mChromeTooltipListener;

  nsCOMPtr<nsIPrompt> mPrompter;
  nsCOMPtr<nsIAuthPrompt> mAuthPrompter;
  nsCOMPtr<nsIRemoteTab> mPrimaryRemoteTab;
};

#endif /* nsWebBrowser_h_ */
