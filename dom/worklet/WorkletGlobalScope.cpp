/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkletGlobalScope.h"

#include "js/RealmOptions.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Console.h"
#include "mozilla/dom/WorkletGlobalScopeBinding.h"
#include "mozilla/dom/WorkletImpl.h"
#include "mozilla/dom/WorkletThread.h"
#include "mozilla/dom/worklet/WorkletModuleLoader.h"
#include "nsContentUtils.h"
#include "nsJSUtils.h"
#include "nsRFPService.h"
#include "nsThreadUtils.h"

using JS::loader::ModuleLoaderBase;
using mozilla::dom::loader::WorkletModuleLoader;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WorkletGlobalScope)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(WorkletGlobalScope)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mConsole)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mModuleLoader)
  tmp->UnlinkObjectsInGlobal();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(WorkletGlobalScope)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConsole)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mModuleLoader)
  tmp->TraverseObjectsInGlobal(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(WorkletGlobalScope)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WorkletGlobalScope)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkletGlobalScope)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIGlobalObject)
  NS_INTERFACE_MAP_ENTRY(WorkletGlobalScope)
NS_INTERFACE_MAP_END

WorkletGlobalScope::WorkletGlobalScope(WorkletImpl* aImpl)
    : mImpl(aImpl), mCreationTimeStamp(TimeStamp::Now()) {}

WorkletGlobalScope::~WorkletGlobalScope() = default;

JSObject* WorkletGlobalScope::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  MOZ_CRASH("We should never get here!");
  return nullptr;
}

nsISerialEventTarget* WorkletGlobalScope::SerialEventTarget() const {
  WorkletThread::AssertIsOnWorkletThread();
  return NS_GetCurrentThread();
}

nsresult WorkletGlobalScope::Dispatch(
    already_AddRefed<nsIRunnable> aRunnable) const {
  WorkletThread::AssertIsOnWorkletThread();
  return SerialEventTarget()->Dispatch(std::move(aRunnable));
}

already_AddRefed<Console> WorkletGlobalScope::GetConsole(JSContext* aCx,
                                                         ErrorResult& aRv) {
  if (!mConsole) {
    MOZ_ASSERT(Impl());
    const WorkletLoadInfo& loadInfo = Impl()->LoadInfo();
    mConsole = Console::CreateForWorklet(aCx, this, loadInfo.OuterWindowID(),
                                         loadInfo.InnerWindowID(), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  RefPtr<Console> console = mConsole;
  return console.forget();
}

void WorkletGlobalScope::InitModuleLoader(WorkletModuleLoader* aModuleLoader) {
  MOZ_ASSERT(!mModuleLoader);
  mModuleLoader = aModuleLoader;
}

ModuleLoaderBase* WorkletGlobalScope::GetModuleLoader(JSContext* aCx) {
  return mModuleLoader;
};

OriginTrials WorkletGlobalScope::Trials() const { return mImpl->Trials(); }

Maybe<nsID> WorkletGlobalScope::GetAgentClusterId() const {
  return mImpl->GetAgentClusterId();
}

bool WorkletGlobalScope::IsSharedMemoryAllowed() const {
  return mImpl->IsSharedMemoryAllowed();
}

bool WorkletGlobalScope::ShouldResistFingerprinting(RFPTarget aTarget) const {
  return mImpl->ShouldResistFingerprinting(aTarget);
}

bool WorkletGlobalScope::CrossOriginIsolated() const {
  return mImpl->CrossOriginIsolated();
}

void WorkletGlobalScope::Dump(const Optional<nsAString>& aString) const {
  WorkletThread::AssertIsOnWorkletThread();

  if (!nsJSUtils::DumpEnabled()) {
    return;
  }

  if (!aString.WasPassed()) {
    return;
  }

  NS_ConvertUTF16toUTF8 str(aString.Value());

  MOZ_LOG(nsContentUtils::DOMDumpLog(), mozilla::LogLevel::Debug,
          ("[Worklet.Dump] %s", str.get()));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "Gecko", "%s", str.get());
#endif
  fputs(str.get(), stdout);
  fflush(stdout);
}

JS::RealmOptions WorkletGlobalScope::CreateRealmOptions() const {
  JS::RealmOptions options;

  if (ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC)) {
    nsCString timeZone = nsRFPService::GetSpoofedJSTimeZone();
    options.behaviors().setTimeZoneOverride(timeZone.get());
  }
  options.creationOptions().setAlwaysUseFdlibm(
      ShouldResistFingerprinting(RFPTarget::JSMathFdlibm));
  if (ShouldResistFingerprinting(RFPTarget::JSLocale)) {
    nsCString locale = nsRFPService::GetSpoofedJSLocale();
    options.behaviors().setLocaleOverride(locale.get());
  }

  // The SharedArrayBuffer global constructor property should not be present in
  // a fresh global object when shared memory objects aren't allowed (because
  // COOP/COEP support isn't enabled, or because COOP/COEP don't act to isolate
  // this worklet to a separate process).
  options.creationOptions().setDefineSharedArrayBufferConstructor(
      IsSharedMemoryAllowed());

  return options;
}

bool IsWorkletGlobal(JSObject* aObj) {
  return IS_INSTANCE_OF(WorkletGlobalScope, aObj);
}

}  // namespace mozilla::dom
