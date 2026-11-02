/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsContentPermissionHelper_h
#define nsContentPermissionHelper_h

#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/PContentPermissionRequestChild.h"
#include "mozilla/dom/PContentPermissionRequestParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsIContentPermissionPrompt.h"
#include "nsIPrincipal.h"
#include "nsPIDOMWindow.h"
#include "nsStringFwd.h"
#include "nsTArray.h"

// Microsoft's API Name hackery sucks
// XXXbz Doing this in a header is a gigantic footgun. See
// https://bugzilla.mozilla.org/show_bug.cgi?id=932421#c3 for why.
#undef LoadImage

class nsIMutableArray;
class nsPIDOMWindowInner;
class nsContentPermissionRequestProxy;
class RemotePermissionRequest;

namespace mozilla::dom {

class Element;
class PermissionRequest;
class ContentPermissionRequestParent;

class ContentPermissionType : public nsIContentPermissionType {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTPERMISSIONTYPE

  ContentPermissionType(const nsACString& aType,
                        const nsTArray<nsString>& aOptions);

 protected:
  virtual ~ContentPermissionType();

  nsCString mType;
  nsTArray<nsString> mOptions;
};

class nsContentPermissionUtils {
 public:
  static uint32_t ConvertPermissionRequestToArray(
      nsTArray<PermissionRequest>& aSrcArray, nsIMutableArray* aDesArray);

  // Converts blindly, that is, strings are not matched against any list.
  //
  // @param aSrcArray needs to contain elements of type
  // `nsIContentPermissionType`.
  static void ConvertArrayToPermissionRequest(
      nsIArray* aSrcArray, nsTArray<PermissionRequest>& aDesArray);

  static nsresult CreatePermissionArray(const nsACString& aType,
                                        const nsTArray<nsString>& aOptions,
                                        nsIArray** aTypesArray);

  // @param aIsRequestDelegatedToUnsafeThirdParty see
  // ContentPermissionRequestParent.
  static already_AddRefed<ContentPermissionRequestParent>
  CreateContentPermissionRequestParent(
      Element* aElement, nsIPrincipal* aPrincipal,
      nsIPrincipal* aTopLevelPrincipal,
      const bool aHasValidTransientUserGestureActivation,
      const bool aIsRequestDelegatedToUnsafeThirdParty, const TabId& aTabId,
      const bool aIgnoreAllowSitePermission);

  static nsresult AskPermission(nsIContentPermissionRequest* aRequest,
                                nsPIDOMWindowInner* aWindow);

  static nsTArray<RefPtr<ContentPermissionRequestParent>>
  GetContentPermissionRequestParentById(const TabId& aTabId);

  static void NotifyRemoveContentPermissionRequestParent(
      ContentPermissionRequestParent* aParent);

  static nsTArray<RefPtr<RemotePermissionRequest>>
  GetContentPermissionRequestChildById(const TabId& aTabId);

  static void NotifyRemoveContentPermissionRequestChild(
      RemotePermissionRequest* aChild);
};

nsresult TranslateChoices(
    JS::Handle<JS::Value> aChoices,
    const nsTArray<PermissionRequest>& aPermissionRequests,
    nsTArray<PermissionChoice>& aTranslatedChoices);

class ContentPermissionRequestBaseNoWindow
    : public nsIContentPermissionRequest {
 public:
  ContentPermissionRequestBaseNoWindow(
      nsIPrincipal* aPrincipal, nsIPrincipal* aTopLevelPrincipal,
      const nsACString& aType, bool aHasValidTransientUserGestureActivation,
      bool aIsRequestDelegatedToUnsafeThirdParty)
      : mPrincipal(aPrincipal),
        mTopLevelPrincipal(aTopLevelPrincipal),
        mType(aType),
        mHasValidTransientUserGestureActivation(
            aHasValidTransientUserGestureActivation),
        mIsRequestDelegatedToUnsafeThirdParty(
            aIsRequestDelegatedToUnsafeThirdParty) {}

  NS_IMETHOD GetTypes(nsIArray** aTypes) final;
  virtual void GetPermissionOptions(nsTArray<nsString>& aOptions) const {}

  NS_IMETHOD GetPrincipal(nsIPrincipal** aPrincipal) final;
  nsIPrincipal* Principal() const { return mPrincipal; }
  NS_IMETHOD GetDelegatePrincipal(const nsACString& aType,
                                  nsIPrincipal** aPrincipal) final;
  NS_IMETHOD GetTopLevelPrincipal(nsIPrincipal** aTopLevelPrincipal) final;
  NS_IMETHOD GetWindow(mozIDOMWindow** aWindow) override {
    *aWindow = nullptr;
    return NS_OK;
  }
  NS_IMETHOD GetElement(mozilla::dom::Element** aElement) override {
    *aElement = nullptr;
    return NS_OK;
  }
  NS_IMETHOD GetHasValidTransientUserGestureActivation(
      bool* aHasValidTransientUserGestureActivation) final;
  NS_IMETHOD GetIsRequestDelegatedToUnsafeThirdParty(
      bool* aIsRequestDelegatedToUnsafeThirdParty) final;
  NS_IMETHOD GetIgnoreAllowSitePermission(
      bool* aIgnoreAllowSitePermission) override;
  NS_IMETHOD NotifyShown(void) override { return NS_OK; }
  // Overrides for Allow() and Cancel() aren't provided by this class.
  // That is the responsibility of the subclasses.

 protected:
  virtual ~ContentPermissionRequestBaseNoWindow() = default;

  nsCOMPtr<nsIPrincipal> mPrincipal;

  nsCOMPtr<nsIPrincipal> mTopLevelPrincipal;

  // The type of the request, such as "autoplay-media-audible".
  const nsCString mType;

  bool mHasValidTransientUserGestureActivation;

  // See nsIPermissionDelegateHandler.maybeUnsafePermissionDelegate`.
  bool mIsRequestDelegatedToUnsafeThirdParty;
};

class ContentPermissionRequestBase
    : public ContentPermissionRequestBaseNoWindow {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(ContentPermissionRequestBase)

  NS_IMETHOD GetWindow(mozIDOMWindow** aWindow) override;
  // Overrides for Allow() and Cancel() aren't provided by this class.
  // That is the responsibility of the subclasses.

  enum class PromptResult {
    Granted,
    Denied,
    Pending,
  };
  nsresult ShowPrompt(PromptResult& aResult);

  PromptResult CheckPromptPrefs() const;

  // Check if the permission has an opportunity to request.
  bool CheckPermissionDelegate() const;

  enum class DelayedTaskType {
    Allow,
    Deny,
    Request,
  };
  void RequestDelayedTask(nsIEventTarget* aTarget, DelayedTaskType aType);

 protected:
  // @param aPrefName see `mPrefName`.
  // @param aType see `mType`.
  ContentPermissionRequestBase(nsIPrincipal* aPrincipal,
                               nsPIDOMWindowInner* aWindow,
                               const nsACString& aPrefName,
                               const nsACString& aType);
  virtual ~ContentPermissionRequestBase() = default;

  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  RefPtr<PermissionDelegateHandler> mPermissionHandler;

  // The prefix of a pref which allows tests to bypass showing the prompt.
  // Tests will have to set both of
  // ${mPrefName}.prompt.testing and
  // ${mPrefName}.prompt.testing.allow
  // to either true or false. If no such testing is required, mPrefName may be
  // empty.
  const nsCString mPrefName;
};

class ContentPermissionRequestParent final
    : public PContentPermissionRequestParent,
      public SupportsWeakPtr {
  friend class PContentPermissionRequestParent;

 public:
  NS_INLINE_DECL_REFCOUNTING(ContentPermissionRequestParent, override)

  // @param aIsRequestDelegatedToUnsafeThirdParty see
  // mIsRequestDelegatedToUnsafeThirdParty.
  ContentPermissionRequestParent(
      Element* aElement, nsIPrincipal* aPrincipal,
      nsIPrincipal* aTopLevelPrincipal,
      const bool aHasValidTransientUserGestureActivation,
      const bool aIsRequestDelegatedToUnsafeThirdParty,
      const bool aIgnoreAllowSitePermission = false);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void Init(nsTArray<PermissionRequest>&& aRequests);

  bool IsBeingDestroyed();

  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIPrincipal> mTopLevelPrincipal;
  nsCOMPtr<Element> mElement;
  bool mHasValidTransientUserGestureActivation;

  // See nsIPermissionDelegateHandler.maybeUnsafePermissionDelegate.
  bool mIsRequestDelegatedToUnsafeThirdParty;

  bool mIgnoreAllowSitePermission;

  RefPtr<nsContentPermissionRequestProxy> mProxy;
  nsTArray<PermissionRequest> mRequests;

 private:
  ~ContentPermissionRequestParent();

  mozilla::ipc::IPCResult RecvDestroy();
  void ActorDestroy(ActorDestroyReason why) override;
};

}  // namespace mozilla::dom

using mozilla::dom::ContentPermissionRequestParent;

class nsContentPermissionRequestProxy : public nsIContentPermissionRequest {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTPERMISSIONREQUEST

  explicit nsContentPermissionRequestProxy(
      ContentPermissionRequestParent* parent);

  nsresult Init(const nsTArray<mozilla::dom::PermissionRequest>& requests);

  void OnParentDestroyed();

 private:
  virtual ~nsContentPermissionRequestProxy();

  // The ContentPermissionRequestParent object which owns this proxy.
  mozilla::WeakPtr<ContentPermissionRequestParent> mParent;
  nsTArray<mozilla::dom::PermissionRequest> mPermissionRequests;
};

/**
 * RemotePermissionRequest will send a prompt ipdl request to the chrome process
 * (https://wiki.mozilla.org/Security/Sandbox/Process_model#Chrome_process_.28Parent.29).
 */
class RemotePermissionRequest final
    : public mozilla::dom::PContentPermissionRequestChild {
 public:
  NS_INLINE_DECL_REFCOUNTING(RemotePermissionRequest, override)

  RemotePermissionRequest(nsIContentPermissionRequest* aRequest,
                          nsPIDOMWindowInner* aWindow);

  // It will be called when prompt dismissed.  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  // because we don't have MOZ_CAN_RUN_SCRIPT bits in IPC code yet.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvNotifyResult(
      const bool& aAllow, nsTArray<PermissionChoice>&& aChoices);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  void Destroy();

  bool IPCOpen() const { return CanSend() && !mDestroyed; }

 private:
  virtual ~RemotePermissionRequest();

  MOZ_CAN_RUN_SCRIPT
  void DoAllow(JS::Handle<JS::Value> aChoices);
  MOZ_CAN_RUN_SCRIPT
  void DoCancel();

  nsCOMPtr<nsIContentPermissionRequest> mRequest;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  bool mDestroyed;
};

#endif  // nsContentPermissionHelper_h
