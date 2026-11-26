/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "WorkletImpl.h"

#include "Worklet.h"
#include "WorkletThread.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/RandomNum.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/RegisterWorkletBindings.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WorkletBinding.h"
#include "mozilla/dom/WorkletGlobalScope.h"
#include "mozilla/dom/worklet/WorkletModuleLoader.h"
#include "nsGlobalWindowInner.h"

using mozilla::dom::loader::WorkletModuleLoader;
using mozilla::dom::loader::WorkletScriptLoader;

namespace mozilla {
// ---------------------------------------------------------------------------
// WorkletLoadInfo

WorkletLoadInfo::WorkletLoadInfo(nsPIDOMWindowInner* aWindow)
    : mInnerWindowID(aWindow->WindowID()) {
  MOZ_ASSERT(NS_IsMainThread());
  nsPIDOMWindowOuter* outerWindow = aWindow->GetOuterWindow();
  if (outerWindow) {
    mOuterWindowID = outerWindow->WindowID();
  } else {
    mOuterWindowID = 0;
  }
}

// ---------------------------------------------------------------------------
// WorkletImpl

WorkletImpl::WorkletImpl(nsPIDOMWindowInner* aWindow, nsIPrincipal* aPrincipal)
    : mPrincipal(NullPrincipal::CreateWithInheritedAttributes(aPrincipal)),
      mWorkletLoadInfo(aWindow),
      mTerminated(false),
      mFinishedOnExecutionThread(false),
      mTimeOrigin(aWindow->GetPerformance()
                      ? aWindow->GetPerformance()->CreationTimeStamp()
                      : TimeStamp::Now()),
      mRandomTimelineSeed(
          aWindow->GetPerformance()
              ? aWindow->GetPerformance()->GetRandomTimelineSeed()
              : static_cast<int64_t>(RandomUint64OrDie())),
      mIsPrivateBrowsing(false),
      mTrials(OriginTrials::FromWindow(nsGlobalWindowInner::Cast(aWindow))) {
  (void)NS_WARN_IF(
      NS_FAILED(ipc::PrincipalToPrincipalInfo(mPrincipal, &mPrincipalInfo)));

  if (aWindow->GetDocGroup()) {
    mAgentClusterId.emplace(aWindow->GetDocGroup()->AgentClusterId());
  }

  mSharedMemoryAllowed =
      nsGlobalWindowInner::Cast(aWindow)->IsSharedMemoryAllowed();
  mCrossOriginIsolated =
      nsGlobalWindowInner::Cast(aWindow)->CrossOriginIsolated();

  mShouldResistFingerprinting = aWindow->AsGlobal()->ShouldResistFingerprinting(
      RFPTarget::IsAlwaysEnabledForPrecompute);

  if (RefPtr<dom::Document> doc =
          nsGlobalWindowInner::Cast(aWindow)->GetDocument()) {
    mIsPrivateBrowsing = doc->IsInPrivateBrowsing();
    mOverriddenFingerprintingSettings =
        doc->GetOverriddenFingerprintingSettings();

    if (auto* policy = PolicyContainer::Cast(doc->GetPolicyContainer())) {
      ipc::PolicyContainerArgs policyContainer;
      PolicyContainer::ToArgs(policy, policyContainer);
      mPolicyContainer = Some(std::move(policyContainer));
    }
  }
}

WorkletImpl::~WorkletImpl() { MOZ_ASSERT(!mGlobalScope); }

JSObject* WorkletImpl::WrapWorklet(JSContext* aCx, dom::Worklet* aWorklet,
                                   JS::Handle<JSObject*> aGivenProto) {
  MOZ_ASSERT(NS_IsMainThread());
  return dom::Worklet_Binding::Wrap(aCx, aWorklet, aGivenProto);
}

dom::WorkletGlobalScope* WorkletImpl::GetGlobalScope() {
  dom::WorkletThread::AssertIsOnWorkletThread();

  if (mGlobalScope) {
    return mGlobalScope;
  }
  if (mFinishedOnExecutionThread) {
    return nullptr;
  }

  dom::AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();

  mGlobalScope = ConstructGlobalScope(cx);

  JS::Rooted<JSObject*> global(cx);
  NS_ENSURE_TRUE(mGlobalScope->WrapGlobalObject(cx, &global), nullptr);

  JSAutoRealm ar(cx, global);

  // Init Web IDL bindings
  if (!dom::RegisterWorkletBindings(cx, global)) {
    return nullptr;
  }

  JS_FireOnNewGlobalObject(cx, global);

  MOZ_ASSERT(!mGlobalScope->GetModuleLoader(cx));

  RefPtr<WorkletScriptLoader> scriptLoader = new WorkletScriptLoader();
  RefPtr<WorkletModuleLoader> moduleLoader =
      new WorkletModuleLoader(scriptLoader, mGlobalScope);
  mGlobalScope->InitModuleLoader(moduleLoader);

  return mGlobalScope;
}

dom::OffThreadCSPContext* WorkletImpl::GetCSPContext() {
  dom::WorkletThread::AssertIsOnWorkletThread();

  if (!mCSPContext && mPolicyContainer && mPolicyContainer->csp().isSome()) {
    ipc::CSPInfo cspInfo(mPolicyContainer->csp().ref());
    mCSPContext =
        std::make_unique<dom::OffThreadCSPContext>(std::move(cspInfo));
  }
  return mCSPContext.get();
}

void WorkletImpl::NotifyWorkletFinished() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mTerminated) {
    return;
  }

  // Release global scope on its thread.
  SendControlMessage(
      NS_NewRunnableFunction("WorkletImpl::NotifyWorkletFinished",
                             [self = RefPtr<WorkletImpl>(this)]() {
                               self->mFinishedOnExecutionThread = true;
                               self->mGlobalScope = nullptr;
                               self->mCSPContext = nullptr;
                               self->OnFinishedOnExecutionThread();
                             }));

  mTerminated = true;
  if (mWorkletThread) {
    mWorkletThread->Terminate();
    mWorkletThread = nullptr;
  }
}

nsresult WorkletImpl::SendControlMessage(
    already_AddRefed<nsIRunnable> aRunnable) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<nsIRunnable> runnable = std::move(aRunnable);

  // TODO: bug 1492011 re ConsoleWorkletRunnable.
  if (mTerminated) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!mWorkletThread) {
    // Thread creation. FIXME: this will change.
    mWorkletThread = dom::WorkletThread::Create(this);
    if (!mWorkletThread) {
      return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
    }
  }

  return mWorkletThread->DispatchRunnable(runnable.forget());
}

}  // namespace mozilla
