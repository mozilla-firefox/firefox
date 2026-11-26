/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_worklet_WorkletImpl_h
#define mozilla_dom_worklet_WorkletImpl_h

#include "MainThreadUtils.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/OriginTrials.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/OffThreadCSPContext.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsRFPService.h"

class nsPIDOMWindowInner;
class nsIPrincipal;
class nsIRunnable;

namespace mozilla {

namespace dom {

class Worklet;
class WorkletGlobalScope;
class WorkletThread;

}  // namespace dom

class WorkletLoadInfo {
 public:
  explicit WorkletLoadInfo(nsPIDOMWindowInner* aWindow);

  uint64_t OuterWindowID() const { return mOuterWindowID; }
  uint64_t InnerWindowID() const { return mInnerWindowID; }

 private:
  // Modified only in constructor.
  uint64_t mOuterWindowID;
  const uint64_t mInnerWindowID;
};

/**
 * WorkletImpl is accessed from both the worklet's parent thread (on which the
 * Worklet object lives) and the worklet's execution thread.  It is owned by
 * Worklet and WorkletGlobalScope.  No parent thread cycle collected objects
 * are owned indefinitely by WorkletImpl because WorkletImpl is not cycle
 * collected.
 */
class WorkletImpl {
  using RFPTarget = mozilla::RFPTarget;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WorkletImpl);

  // Methods for parent thread only:

  virtual JSObject* WrapWorklet(JSContext* aCx, dom::Worklet* aWorklet,
                                JS::Handle<JSObject*> aGivenProto);

  virtual nsresult SendControlMessage(already_AddRefed<nsIRunnable> aRunnable);

  void NotifyWorkletFinished();

  virtual nsContentPolicyType ContentPolicyType() const = 0;

  // Execution thread only.
  dom::WorkletGlobalScope* GetGlobalScope();
  dom::OffThreadCSPContext* GetCSPContext();

  // Any thread.

  const OriginTrials& Trials() const { return mTrials; }
  const WorkletLoadInfo& LoadInfo() const { return mWorkletLoadInfo; }
  const OriginAttributes& OriginAttributesRef() const {
    return mPrincipal->OriginAttributesRef();
  }
  nsIPrincipal* Principal() const { return mPrincipal; }
  const ipc::PrincipalInfo& PrincipalInfo() const { return mPrincipalInfo; }
  const Maybe<ipc::PolicyContainerArgs>& PolicyContainer() const {
    return mPolicyContainer;
  }

  const Maybe<nsID>& GetAgentClusterId() const { return mAgentClusterId; }

  bool IsSharedMemoryAllowed() const { return mSharedMemoryAllowed; }
  bool CrossOriginIsolated() const { return mCrossOriginIsolated; }
  // The time origin of the outside settings object, and the random seed used
  // to reduce the precision of timestamps on that timeline.
  TimeStamp TimeOrigin() const { return mTimeOrigin; }
  int64_t RandomTimelineSeed() const { return mRandomTimelineSeed; }
  bool IsSystemPrincipal() const { return mPrincipal->IsSystemPrincipal(); }
  bool ShouldResistFingerprinting(RFPTarget aTarget) const {
    return mShouldResistFingerprinting &&
           nsRFPService::IsRFPEnabledFor(mIsPrivateBrowsing, aTarget,
                                         mOverriddenFingerprintingSettings);
  }

  virtual void OnAddModuleStarted() const {
    MOZ_ASSERT(NS_IsMainThread());
    // empty base impl
  }

  virtual void OnAddModulePromiseSettled() const {
    MOZ_ASSERT(NS_IsMainThread());
    // empty base impl
  }

 protected:
  WorkletImpl(nsPIDOMWindowInner* aWindow, nsIPrincipal* aPrincipal);
  virtual ~WorkletImpl();

  virtual already_AddRefed<dom::WorkletGlobalScope> ConstructGlobalScope(
      JSContext* aCx) = 0;

  /**
   * Called on the execution thread from NotifyWorkletFinished(), after the
   * global scope has been released and so once GetGlobalScope() can no longer
   * construct another one. Subclasses may release state that
   * ConstructGlobalScope() would otherwise have consumed.
   */
  virtual void OnFinishedOnExecutionThread() {}

  // Modified only in constructor.
  ipc::PrincipalInfo mPrincipalInfo;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  Maybe<ipc::PolicyContainerArgs> mPolicyContainer;

  const WorkletLoadInfo mWorkletLoadInfo;

  // Parent thread only.
  RefPtr<dom::WorkletThread> mWorkletThread;
  bool mTerminated : 1;

  // Execution thread only.
  RefPtr<dom::WorkletGlobalScope> mGlobalScope;
  UniquePtr<dom::OffThreadCSPContext> mCSPContext;
  bool mFinishedOnExecutionThread : 1;

  Maybe<nsID> mAgentClusterId;

  TimeStamp mTimeOrigin;
  int64_t mRandomTimelineSeed;

  bool mSharedMemoryAllowed : 1;
  bool mCrossOriginIsolated : 1;
  bool mShouldResistFingerprinting : 1;
  bool mIsPrivateBrowsing : 1;
  // The granular fingerprinting protection overrides applied to the worklet.
  // This will only get populated if these is one that comes from the local
  // granular override pref or WebCompat. Otherwise, a value of Nothing()
  // indicates no granular overrides are present for this workerlet.
  Maybe<RFPTargetSet> mOverriddenFingerprintingSettings;

  const OriginTrials mTrials;
};

}  // namespace mozilla

#endif  // mozilla_dom_worklet_WorkletImpl_h
