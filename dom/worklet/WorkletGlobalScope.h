/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WorkletGlobalScope_h
#define mozilla_dom_WorkletGlobalScope_h

#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "nsDOMNavigationTiming.h"
#include "nsIGlobalObject.h"
#include "nsWrapperCache.h"

#define WORKLET_IID \
  {0x1b3f62e7, 0xe357, 0x44be, {0xbf, 0xe0, 0xdf, 0x85, 0xe6, 0x56, 0x85, 0xac}}

namespace JS {
class RealmOptions;
}

namespace JS::loader {
class ModuleLoaderBase;
}

namespace mozilla {

class ErrorResult;
class WorkletImpl;

namespace dom {

namespace loader {
class WorkletModuleLoader;
}

class Console;

class WorkletGlobalScope : public nsIGlobalObject, public nsWrapperCache {
 public:
  NS_INLINE_DECL_STATIC_IID(WORKLET_IID)

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WorkletGlobalScope)

  WorkletGlobalScope(WorkletImpl*);

  nsIGlobalObject* GetParentObject() const { return nullptr; }

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;
  virtual bool WrapGlobalObject(JSContext* aCx,
                                JS::MutableHandle<JSObject*> aReflector) = 0;

  JSObject* GetGlobalJSObject() override { return GetWrapper(); }
  JSObject* GetGlobalJSObjectPreserveColor() const override {
    return GetWrapperPreserveColor();
  }

  nsISerialEventTarget* SerialEventTarget() const final;
  nsresult Dispatch(already_AddRefed<nsIRunnable>) const final;

  already_AddRefed<Console> GetConsole(JSContext* aCx, ErrorResult& aRv);

  WorkletImpl* Impl() const { return mImpl.get(); }

  void Dump(const Optional<nsAString>& aString) const;

  DOMHighResTimeStamp TimeStampToDOMHighRes(const TimeStamp& aTimeStamp) const {
    MOZ_ASSERT(!aTimeStamp.IsNull());
    TimeDuration duration = aTimeStamp - mCreationTimeStamp;
    return duration.ToMilliseconds();
  }

  TimeStamp CreationTimeStamp() const { return mCreationTimeStamp; }

  void InitModuleLoader(loader::WorkletModuleLoader* aModuleLoader);

  JS::loader::ModuleLoaderBase* GetModuleLoader(
      JSContext* aCx = nullptr) override;

  OriginTrials Trials() const override;
  Maybe<nsID> GetAgentClusterId() const override;
  bool IsSharedMemoryAllowed() const override;
  bool ShouldResistFingerprinting(RFPTarget aTarget) const override;
  bool CrossOriginIsolated() const override;

 protected:
  ~WorkletGlobalScope();

  JS::RealmOptions CreateRealmOptions() const;

  const RefPtr<WorkletImpl> mImpl;

 private:
  TimeStamp mCreationTimeStamp;
  RefPtr<Console> mConsole;
  RefPtr<loader::WorkletModuleLoader> mModuleLoader;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_WorkletGlobalScope_h
