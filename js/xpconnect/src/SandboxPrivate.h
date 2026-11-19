/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SANDBOXPRIVATE_H_
#define SANDBOXPRIVATE_H_

#include "mozilla/LinkedList.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/WeakPtr.h"

#include "nsContentUtils.h"
#include "nsIGlobalObject.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsWeakReference.h"
#include "nsWrapperCache.h"

#include "js/Object.h"  // JS::GetPrivate, JS::SetPrivate
#include "js/RootingAPI.h"

namespace JS::loader {
class ModuleLoaderBase;
}  // namespace JS::loader

class SandboxPrivate final : public nsIGlobalObject,
                             public nsIScriptObjectPrincipal,
                             public nsSupportsWeakReference,
                             public mozilla::SupportsWeakPtr,
                             public nsWrapperCache,
                             public mozilla::LinkedListElement<SandboxPrivate> {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS_AMBIGUOUS(SandboxPrivate,
                                                        nsIGlobalObject)

  static void Create(nsIPrincipal* principal, JS::Handle<JSObject*> global) {
    RefPtr<SandboxPrivate> sbp = new SandboxPrivate(principal);
    sbp->SetWrapper(global);
    sbp->PreserveWrapper(ToSupports(sbp.get()));

    // Pass on ownership of sbp to |global|.
    // The type used to cast to void needs to match the one in GetPrivate.
    nsIScriptObjectPrincipal* sop =
        static_cast<nsIScriptObjectPrincipal*>(sbp.forget().take());
    JS::SetObjectISupports(global, sop);

    GetPrivate(global)->RefreshRTPCallerType();
  }

  void RefreshRTPCallerType() {
    JSObject* global = GetWrapperPreserveColor();
    if (!global) {
      return;
    }
    JS::SetRealmReduceTimerPrecisionCallerType(
        js::GetNonCCWObjectRealm(global),
        RTPCallerTypeToToken(GetRTPCallerType()));
  }

  // Refreshes the realm RTP caller type of every live sandbox, so that it
  // stays in sync with GetRTPCallerType() when the RFP prefs change.
  static void RefreshAllRTPCallerTypes();

  static SandboxPrivate* GetPrivate(JSObject* obj) {
    // The type used to cast to void needs to match the one in Create.
    nsIScriptObjectPrincipal* sop =
        JS::GetObjectISupports<nsIScriptObjectPrincipal>(obj);
    return static_cast<SandboxPrivate*>(sop);
  }

  mozilla::OriginTrials Trials() const final { return {}; }

  nsIPrincipal* GetPrincipal() override { return mPrincipal; }

  nsIPrincipal* GetEffectiveCookiePrincipal() override { return mPrincipal; }

  nsIPrincipal* GetEffectiveStoragePrincipal() override { return mPrincipal; }

  nsIPrincipal* PartitionedPrincipal() override { return mPrincipal; }

  JSObject* GetGlobalJSObject() override { return GetWrapper(); }
  JSObject* GetGlobalJSObjectPreserveColor() const override {
    return GetWrapperPreserveColor();
  }

  nsICookieJarSettings* GetCookieJarSettings() override {
    MOZ_ASSERT(NS_IsMainThread());
    return mCookieJarSettings;
  }

  mozilla::StorageAccess GetStorageAccess() final {
    MOZ_ASSERT(NS_IsMainThread());
    if (mozilla::StaticPrefs::dom_serviceWorkers_testing_enabled()) {
      // XXX: This is a hack to workaround bug 1732159 and is not intended
      return mozilla::StorageAccess::eAllow;
    }

    return mozilla::StorageAllowedForServiceWorker(mPrincipal,
                                                   mCookieJarSettings);
  }

  void ForgetGlobalObject(JSObject* obj) {
    ClearWrapper(obj);
    if (isInList()) {
      remove();
    }
  }

  nsISerialEventTarget* SerialEventTarget() const final {
    return mozilla::GetMainThreadSerialEventTarget();
  }
  nsresult Dispatch(already_AddRefed<nsIRunnable> aRunnable) const final {
    return mozilla::SchedulerGroup::Dispatch(std::move(aRunnable));
  }

  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> aGivenProto) override {
    MOZ_CRASH("SandboxPrivate doesn't use DOM bindings!");
  }

  JS::loader::ModuleLoaderBase* GetModuleLoader(JSContext* aCx) override;

  mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult> GetStorageKey()
      override;

  size_t ObjectMoved(JSObject* obj, JSObject* old) {
    UpdateWrapper(obj, old);
    return 0;
  }

  bool ShouldResistFingerprinting(RFPTarget aTarget) const override {
    return nsContentUtils::ShouldResistFingerprinting(
        "Presently we don't have enough context to make an informed decision"
        "on JS Sandboxes. See 1782853",
        aTarget);
  }

  bool IsXPCSandbox() override { return true; }

  void SetAssociatedWindow(nsPIDOMWindowInner* aWindow);
  already_AddRefed<nsPIDOMWindowInner> GetAssociatedWindow();

 private:
  explicit SandboxPrivate(nsIPrincipal* principal);

  virtual ~SandboxPrivate();

  nsCOMPtr<nsIPrincipal> mPrincipal;

  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;

  RefPtr<JS::loader::ModuleLoaderBase> mModuleLoader;

  // Weak to avoid keeping the associated window alive.
  nsWeakPtr mAssociatedWindow;
};

#endif  // SANDBOXPRIVATE_H_
