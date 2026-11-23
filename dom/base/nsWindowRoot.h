/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWindowRoot_h_
#define nsWindowRoot_h_

class nsIGlobalObject;

#include "mozilla/Attributes.h"
#include "mozilla/EventListenerManager.h"
#include "nsCycleCollectionParticipant.h"
#include "nsHashKeys.h"
#include "nsIWeakReferenceUtils.h"
#include "nsPIWindowRoot.h"
#include "nsTHashSet.h"

class nsWindowRoot final : public nsPIWindowRoot {
 public:
  explicit nsWindowRoot(nsPIDOMWindowOuter* aWindow);

  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHOD_(void) DeleteCycleCollectable() override;

  mozilla::EventListenerManager* GetExistingListenerManager() const override;
  mozilla::EventListenerManager* GetOrCreateListenerManager() override;

  bool ComputeDefaultWantsUntrusted(mozilla::ErrorResult& aRv) final;

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool DispatchEvent(
      mozilla::dom::Event& aEvent, mozilla::dom::CallerType aCallerType,
      mozilla::ErrorResult& aRv) override;

  void GetEventTargetParent(mozilla::EventChainPreVisitor&) override;

  MOZ_CAN_RUN_SCRIPT nsresult
  PreHandleEvent(mozilla::EventChainVisitor&) override;
  // FYI: PostHandleEvent would be a pure virtual method if we didn't define it
  // here.
  nsresult PostHandleEvent(mozilla::EventChainPostVisitor&) override {
    MOZ_ASSERT_UNREACHABLE("Does not want PostHandleEvent");
    return NS_OK;
  }

  // nsPIWindowRoot

  nsPIDOMWindowOuter* GetWindow() override;
  nsGlobalWindowInner* GetInnerWindow();

  nsresult GetControllers(bool aForVisibleWindow,
                          nsIControllers** aResult) override;
  nsresult GetControllerForCommand(const char* aCommand, bool aForVisibleWindow,
                                   nsIController** _retval) override;

  void GetEnabledDisabledCommands(
      nsTArray<nsCString>& aEnabledCommands,
      nsTArray<nsCString>& aDisabledCommands) override;

  already_AddRefed<nsINode> GetPopupNode() override;
  void SetPopupNode(nsINode* aNode) override;

  void SetParentTarget(mozilla::dom::EventTarget* aTarget) override {
    mParent = aTarget;
  }
  mozilla::dom::EventTarget* GetParentTarget() override { return mParent; }
  nsIGlobalObject* GetRelevantGlobal() const override;

  nsIGlobalObject* GetParentObject();

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(nsWindowRoot)

  void AddBrowser(nsIRemoteTab* aBrowser) override;
  void RemoveBrowser(nsIRemoteTab* aBrowser) override;
  void EnumerateBrowsers(BrowserEnumerator aEnumFunc, void* aArg) override;

 protected:
  virtual ~nsWindowRoot();

  void GetEnabledDisabledCommandsForControllers(
      nsIControllers* aControllers, nsTHashSet<nsCString>& aCommandsHandled,
      nsTArray<nsCString>& aEnabledCommands,
      nsTArray<nsCString>& aDisabledCommands);

  // Members
  nsCOMPtr<nsPIDOMWindowOuter> mWindow;
  // We own the manager, which owns event listeners attached to us.
  RefPtr<mozilla::EventListenerManager> mListenerManager;  // [Strong]
  nsWeakPtr mPopupNode;

  nsCOMPtr<mozilla::dom::EventTarget> mParent;

  // The BrowserParents that are currently registered with this top-level
  // window.
  using WeakBrowserTable = nsTHashSet<RefPtr<nsIWeakReference>>;
  WeakBrowserTable mWeakBrowsers;
};

extern already_AddRefed<mozilla::dom::EventTarget> NS_NewWindowRoot(
    nsPIDOMWindowOuter* aWindow);

#endif
