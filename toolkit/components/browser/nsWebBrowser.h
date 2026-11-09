/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowser_h_
#define nsWebBrowser_h_

// Local Includes
#include "nsDocShellTreeOwner.h"

// Core Includes
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"

// Interfaces needed
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWidget.h"
#include "nsIWebProgress.h"
#include "nsIWebBrowser.h"
#include "nsIWebNavigation.h"
#include "nsIWindowWatcher.h"
#include "nsIPrintSettings.h"
#include "nsIWidgetListener.h"

#include "mozilla/BasePrincipal.h"
#include "nsTArray.h"
#include "nsIWeakReferenceUtils.h"

class nsIOpenWindowInfo;

//  {cda5863a-aa9c-411e-be49-ea0d525ab4b5} -
#define NS_WEBBROWSER_CID \
  {0xcda5863a, 0xaa9c, 0x411e, {0xbe, 0x49, 0xea, 0x0d, 0x52, 0x5a, 0xb4, 0xb5}}

class mozIDOMWindowProxy;
class nsDocShell;

namespace mozilla {
namespace dom {
class WindowGlobalChild;
}  // namespace dom
}  // namespace mozilla

class nsWebBrowser final : public nsIWebBrowser,
                           public nsIWebNavigation,
                           public nsIDocShellTreeItem,
                           public nsIInterfaceRequestor,
                           public nsSupportsWeakReference {
  friend class nsDocShellTreeOwner;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsWebBrowser, nsIWebBrowser)

  NS_DECL_NSIDOCSHELLTREEITEM
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWEBBROWSER
  NS_DECL_NSIWEBNAVIGATION

  nsDocShell* GetDocShell() const { return mDocShell; }
  void Close() { InternalDestroy(); }
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
  void EnsureDocShellTreeOwner();

  nsIWidget* EnsureWidget();
  explicit nsWebBrowser(int aItemType);

 protected:
  RefPtr<nsDocShellTreeOwner> mDocShellTreeOwner;
  RefPtr<nsDocShell> mDocShell;
  mozilla::OriginAttributes mOriginAttributes;

  const uint32_t mContentType;
  bool mWillChangeProcess;

  // Weak Reference interfaces...
  nsIWidget* mParentWidget;
};

#endif /* nsWebBrowser_h_ */
