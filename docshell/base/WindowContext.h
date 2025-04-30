/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WindowContext_h
#define mozilla_dom_WindowContext_h

#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/Span.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/SyncedContext.h"
#include "mozilla/dom/UserActivation.h"
#include "nsDOMNavigationTiming.h"
#include "nsILoadInfo.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

class nsGlobalWindowInner;

namespace mozilla {
class LogModule;
class nsRFPTargetSetIDL;

namespace dom {

class WindowGlobalChild;
class WindowGlobalParent;
class WindowGlobalInit;
class BrowsingContext;
class BrowsingContextGroup;

#define MOZ_EACH_WC_FIELD(FIELD)                                         \
  /* Whether the SHEntry associated with the current top-level           \
   * window has already seen user interaction.                           \
   * As such, this will be reset to false when a new SHEntry is          \
   * created without changing the WC (e.g. when using pushState or       \
   * sub-frame navigation)                                               \
   * This flag is set for optimization purposes, to avoid                \
   * having to get the top SHEntry and update it on every                \
   * user interaction.                                                   \
   * This is only meaningful on the top-level WC. */                     \
  FIELD(SHEntryHasUserInteraction, bool)                                 \
  FIELD(CookieBehavior, Maybe<uint32_t>)                                 \
  FIELD(IsOnContentBlockingAllowList, bool)                              \
  /* Whether the given window hierarchy is third party. See              \
   * ThirdPartyUtil::IsThirdPartyWindow for details */                   \
  FIELD(IsThirdPartyWindow, bool)                                        \
  /* Whether this window's channel has been marked as a third-party      \
   * tracking resource */                                                \
  FIELD(IsThirdPartyTrackingResourceWindow, bool)                        \
  /* Whether this window is using its unpartitioned cookies due to       \
   * the Storage Access API */                                           \
  FIELD(UsingStorageAccess, bool)                                        \
  FIELD(ShouldResistFingerprinting, bool)                                \
  FIELD(OverriddenFingerprintingSettings, Maybe<RFPTargetSet>)           \
  FIELD(IsSecureContext, bool)                                           \
  FIELD(IsOriginalFrameSource, bool)                                     \
  /* Mixed-Content: If the corresponding documentURI is https,           \
   * then this flag is true. */                                          \
  FIELD(IsSecure, bool)                                                  \
  /* Whether this window has registered a "beforeunload" event           \
   * handler */                                                          \
  FIELD(HasBeforeUnload, bool)                                           \
  /* Controls whether the WindowContext is currently considered to be    \
   * activated by a gesture */                                           \
  FIELD(UserActivationStateAndModifiers,                                 \
        UserActivation::StateAndModifiers::DataT)                        \
  FIELD(EmbedderPolicy, nsILoadInfo::CrossOriginEmbedderPolicy)          \
  /* True if this document tree contained at least a HTMLMediaElement.   \
   * This should only be set on top level context. */                    \
  FIELD(DocTreeHadMedia, bool)                                           \
  FIELD(AutoplayPermission, uint32_t)                                    \
  FIELD(ShortcutsPermission, uint32_t)                                   \
  /* Store the Id of the browsing context where active media session     \
   * exists on the top level window context */                           \
  FIELD(ActiveMediaSessionContextId, Maybe<uint64_t>)                    \
  /* ALLOW_ACTION if it is allowed to open popups for the sub-tree       \
   * starting and including the current WindowContext */                 \
  FIELD(PopupPermission, uint32_t)                                       \
  FIELD(DelegatedPermissions,                                            \
        PermissionDelegateHandler::DelegatedPermissionList)              \
  FIELD(DelegatedExactHostMatchPermissions,                              \
        PermissionDelegateHandler::DelegatedPermissionList)              \
  FIELD(HasReportedShadowDOMUsage, bool)                                 \
  /* Whether the principal of this window is for a local                 \
   * IP address */                                                       \
  FIELD(IsLocalIP, bool)                                                 \
  /* Whether any of the windows in the subtree rooted at this window has \
   * active peer connections or not (only set on the top window). */     \
  FIELD(HasActivePeerConnections, bool)                                  \
  /* Whether we can execute scripts in this WindowContext. Has no effect \
   * unless scripts are also allowed in the BrowsingContext. */          \
  FIELD(AllowJavascript, bool)                                           \
  /* If this field is `true`, it means that this WindowContext's         \
   * WindowState was saved to be stored in the legacy (non-SHIP) BFCache \
   * implementation. Always false for SHIP */                            \
  FIELD(WindowStateSaved, bool)

class WindowContext : public nsISupports, public nsWrapperCache {
  MOZ_DECL_SYNCED_CONTEXT(WindowContext, MOZ_EACH_WC_FIELD)

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WindowContext)

 public:
  static already_AddRefed<WindowContext> GetById(uint64_t aInnerWindowId);
  static LogModule* GetLog();
  static LogModule* GetSyncLog();

  BrowsingContext* GetBrowsingContext() const { return mBrowsingContext; }
  BrowsingContextGroup* Group() const;
  uint64_t Id() const { return InnerWindowId(); }
  uint64_t InnerWindowId() const { return mInnerWindowId; }
  uint64_t OuterWindowId() const { return mOuterWindowId; }
  bool IsDiscarded() const { return mIsDiscarded; }

  // Returns `true` if this WindowContext is the current WindowContext in its
  // BrowsingContext.
  bool IsCurrent() const;

  // Returns `true` if this WindowContext is currently in the BFCache.
  bool IsInBFCache();

  bool IsInProcess() const { return mIsInProcess; }

  bool HasBeforeUnload() const { return GetHasBeforeUnload(); }

  bool IsLocalIP() const { return GetIsLocalIP(); }

  bool ShouldResistFingerprinting() const {
    return GetShouldResistFingerprinting();
  }

  bool UsingStorageAccess() const { return GetUsingStorageAccess(); }

  already_AddRefed<nsIRFPTargetSetIDL>
  GetOverriddenFingerprintingSettingsWebIDL() const;

  nsGlobalWindowInner* GetInnerWindow() const;
  Document* GetDocument() const;
  Document* GetExtantDoc() const;

  WindowGlobalChild* GetWindowGlobalChild() const;

  // Get the parent WindowContext of this WindowContext, taking the BFCache into
  // account. This will not cross chrome/content <browser> boundaries.
  WindowContext* GetParentWindowContext();
  WindowContext* TopWindowContext();

  bool SameOriginWithTop() const;

  bool IsTop() const;

  Span<RefPtr<BrowsingContext>> Children() { return mChildren; }

  // The filtered version of `Children()`, which contains no browsing contexts
  // for synthetic documents as created by object loading content.
  Span<RefPtr<BrowsingContext>> NonSyntheticChildren() {
    return mNonSyntheticChildren;
  }

  BrowsingContext* NonSyntheticLightDOMChildAt(uint32_t aIndex);
  uint32_t NonSyntheticLightDOMChildrenCount();

  // Cast this object to it's parent-process canonical form.
  WindowGlobalParent* Canonical();

  nsIGlobalObject* GetParentObject() const;
  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void Discard();

  struct IPCInitializer {
    uint64_t mInnerWindowId;
    uint64_t mOuterWindowId;
    uint64_t mBrowsingContextId;

    FieldValues mFields;
  };
  IPCInitializer GetIPCInitializer();

  static void CreateFromIPC(IPCInitializer&& aInit);

  // Add new security state flags.
  // These should be some of the nsIWebProgressListener 'HTTPS_ONLY_MODE' or
  // 'MIXED' state flags, and should only be called on the top window context.
  void AddSecurityState(uint32_t aStateFlags);

  UserActivation::State GetUserActivationState() const {
    return UserActivation::StateAndModifiers(
               GetUserActivationStateAndModifiers())
        .GetState();
  }

  // This function would be called when its corresponding window is activated
  // by user gesture.
  void NotifyUserGestureActivation(
      UserActivation::Modifiers aModifiers = UserActivation::Modifiers::None());

  // This function would be called when we want to reset the user gesture
  // activation flag.
  void NotifyResetUserGestureActivation();

  // Return true if its corresponding window has been activated by user
  // gesture.
  bool HasBeenUserGestureActivated();

  // Return true if its corresponding window has transient user gesture
  // activation and the transient user gesture activation haven't yet timed
  // out.
  bool HasValidTransientUserGestureActivation();

  // See `mLastActivationTimestamp`.
  const TimeStamp& GetUserGestureStart() const;

  // Return true if the corresponding window has valid transient user gesture
  // activation and the transient user gesture activation had been consumed
  // successfully.
  bool ConsumeTransientUserGestureActivation();

  // Return true if its corresponding window has history activation.
  bool HasValidHistoryActivation() const;

  // Consume the history-action user activation.
  void ConsumeHistoryActivation();

  bool GetTransientUserGestureActivationModifiers(
      UserActivation::Modifiers* aModifiers);

  bool CanShowPopup();

  bool AllowJavascript() const { return GetAllowJavascript(); }
  bool CanExecuteScripts() const { return mCanExecuteScripts; }

  void TransientSetHasActivePeerConnections();

 protected:
  WindowContext(BrowsingContext* aBrowsingContext, uint64_t aInnerWindowId,
                uint64_t aOuterWindowId, FieldValues&& aFields);
  virtual ~WindowContext();

  virtual void Init();

 private:
  friend class BrowsingContext;
  friend class WindowGlobalChild;
  friend class WindowGlobalActor;

  void AppendChildBrowsingContext(BrowsingContext* aBrowsingContext);
  void RemoveChildBrowsingContext(BrowsingContext* aBrowsingContext);

  // Update non-synthetic children based on whether `aBrowsingContext`
  // is synthetic or not. Regardless the synthetic of `aBrowsingContext`, it is
  // kept in this WindowContext's all children list.
  void UpdateChildSynthetic(BrowsingContext* aBrowsingContext,
                            bool aIsSynthetic);

  // Send a given `BaseTransaction` object to the correct remote.
  void SendCommitTransaction(ContentParent* aParent,
                             const BaseTransaction& aTxn, uint64_t aEpoch);
  void SendCommitTransaction(ContentChild* aChild, const BaseTransaction& aTxn,
                             uint64_t aEpoch);

  bool CheckOnlyOwningProcessCanSet(ContentParent* aSource);

  // Overload `CanSet` to get notifications for a particular field being set.
  bool CanSet(FieldIndex<IDX_IsSecure>, const bool& aIsSecure,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_HasBeforeUnload>, const bool& aHasBeforeUnload,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_CookieBehavior>, const Maybe<uint32_t>& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_IsOnContentBlockingAllowList>, const bool& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_EmbedderPolicy>, const bool& aValue,
              ContentParent* aSource) {
    return true;
  }

  bool CanSet(FieldIndex<IDX_IsThirdPartyWindow>,
              const bool& IsThirdPartyWindow, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_IsThirdPartyTrackingResourceWindow>,
              const bool& aIsThirdPartyTrackingResourceWindow,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_UsingStorageAccess>,
              const bool& aUsingStorageAccess, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_ShouldResistFingerprinting>,
              const bool& aShouldResistFingerprinting, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_OverriddenFingerprintingSettings>,
              const Maybe<RFPTargetSet>& aValue, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_IsSecureContext>, const bool& aIsSecureContext,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_IsOriginalFrameSource>,
              const bool& aIsOriginalFrameSource, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_DocTreeHadMedia>, const bool& aValue,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_AutoplayPermission>, const uint32_t& aValue,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_ShortcutsPermission>, const uint32_t& aValue,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_ActiveMediaSessionContextId>,
              const Maybe<uint64_t>& aValue, ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_PopupPermission>, const uint32_t&,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_SHEntryHasUserInteraction>,
              const bool& aSHEntryHasUserInteraction, ContentParent* aSource) {
    return true;
  }
  bool CanSet(FieldIndex<IDX_DelegatedPermissions>,
              const PermissionDelegateHandler::DelegatedPermissionList& aValue,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_DelegatedExactHostMatchPermissions>,
              const PermissionDelegateHandler::DelegatedPermissionList& aValue,
              ContentParent* aSource);
  bool CanSet(FieldIndex<IDX_UserActivationStateAndModifiers>,
              const UserActivation::StateAndModifiers::DataT&
                  aUserActivationStateAndModifiers,
              ContentParent* aSource) {
    return true;
  }

  bool CanSet(FieldIndex<IDX_HasReportedShadowDOMUsage>, const bool& aValue,
              ContentParent* aSource) {
    return true;
  }

  bool CanSet(FieldIndex<IDX_IsLocalIP>, const bool& aValue,
              ContentParent* aSource);

  bool CanSet(FieldIndex<IDX_AllowJavascript>, bool aValue,
              ContentParent* aSource);
  void DidSet(FieldIndex<IDX_AllowJavascript>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_HasActivePeerConnections>, bool, ContentParent*);

  void DidSet(FieldIndex<IDX_HasReportedShadowDOMUsage>, bool aOldValue);

  void DidSet(FieldIndex<IDX_SHEntryHasUserInteraction>, bool aOldValue);

  bool CanSet(FieldIndex<IDX_WindowStateSaved>, bool aValue,
              ContentParent* aSource);

  // Overload `DidSet` to get notifications for a particular field being set.
  //
  // You can also overload the variant that gets the old value if you need it.
  template <size_t I>
  void DidSet(FieldIndex<I>) {}
  template <size_t I, typename T>
  void DidSet(FieldIndex<I>, T&& aOldValue) {}
  void DidSet(FieldIndex<IDX_UserActivationStateAndModifiers>);

  // Recomputes whether we can execute scripts in this WindowContext based on
  // the value of AllowJavascript() and whether scripts are allowed in the
  // BrowsingContext.
  void RecomputeCanExecuteScripts(bool aApplyChanges = true);

  void ClearLightDOMChildren();

  void EnsureLightDOMChildren();

  const uint64_t mInnerWindowId;
  const uint64_t mOuterWindowId;
  RefPtr<BrowsingContext> mBrowsingContext;
  WeakPtr<WindowGlobalChild> mWindowGlobalChild;

  // --- NEVER CHANGE `mChildren` DIRECTLY! ---
  // Changes to this list need to be synchronized to the list within our
  // `mBrowsingContext`, and should only be performed through the
  // `AppendChildBrowsingContext` and `RemoveChildBrowsingContext` methods.
  nsTArray<RefPtr<BrowsingContext>> mChildren;

  // --- NEVER CHANGE `mNonSyntheticChildren` DIRECTLY! ---
  // Same reason as for mChildren.
  // mNonSyntheticChildren contains the same browsing contexts except browsing
  // contexts created by the synthetic document for object loading contents
  // loading images. This is used to discern browsing contexts created when
  // loading images in <object> or <embed> elements, so that they can be hidden
  // from named targeting, `Window.frames` etc.
  nsTArray<RefPtr<BrowsingContext>> mNonSyntheticChildren;

  // mNonSyntheticLightDOMChildren is otherwise the same as
  // mNonSyntheticChildren, but it contains only those BrowsingContexts where
  // embedder is in light DOM. The contents of the array are computed lazily and
  // cleared if there are changes to mChildren.
  Maybe<nsTArray<RefPtr<BrowsingContext>>> mNonSyntheticLightDOMChildren;

  bool mIsDiscarded = false;
  bool mIsInProcess = false;

  // Determines if we can execute scripts in this WindowContext. True if
  // AllowJavascript() is true and script execution is allowed in the
  // BrowsingContext.
  bool mCanExecuteScripts = true;

  // https://html.spec.whatwg.org/multipage/interaction.html#last-activation-timestamp
  // The start time of user gesture, this is only available if the window
  // context is in process.
  TimeStamp mLastActivationTimestamp;

  // https://html.spec.whatwg.org/#history-action-activation
  // This is set to mLastActivationTimestamp every time ConsumeHistoryActivation
  // is called.
  TimeStamp mHistoryActivation;
};

using WindowContextTransaction = WindowContext::BaseTransaction;
using WindowContextInitializer = WindowContext::IPCInitializer;
using MaybeDiscardedWindowContext = MaybeDiscarded<WindowContext>;

// Don't specialize the `Transaction` object for every translation unit it's
// used in. This should help keep code size down.
extern template class syncedcontext::Transaction<WindowContext>;

}  // namespace dom

namespace ipc {
template <>
struct IPDLParamTraits<dom::MaybeDiscarded<dom::WindowContext>> {
  static void Write(IPC::MessageWriter* aWriter, IProtocol* aActor,
                    const dom::MaybeDiscarded<dom::WindowContext>& aParam);
  static bool Read(IPC::MessageReader* aReader, IProtocol* aActor,
                   dom::MaybeDiscarded<dom::WindowContext>* aResult);
};

template <>
struct IPDLParamTraits<dom::WindowContext::IPCInitializer> {
  static void Write(IPC::MessageWriter* aWriter, IProtocol* aActor,
                    const dom::WindowContext::IPCInitializer& aInitializer);

  static bool Read(IPC::MessageReader* aReader, IProtocol* aActor,
                   dom::WindowContext::IPCInitializer* aInitializer);
};
}  // namespace ipc
}  // namespace mozilla

#endif  // !defined(mozilla_dom_WindowContext_h)
