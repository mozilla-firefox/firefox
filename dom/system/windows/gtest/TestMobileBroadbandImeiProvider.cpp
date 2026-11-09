/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <mbnapi.h>
#include <windows.h>

#include <atomic>

#include "MobileBroadbandImeiProvider.h"
#include "gtest/gtest.h"
#include "js/Promise.h"
#include "js/PropertyAndElement.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "nsJSUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {

constexpr wchar_t kTestImei[] = L"490154203237518";

// Fake mobile broadband interface reporting aDeviceID, or failing
// GetInterfaceCapability when aCapsResult is a failure.
class FakeMbnInterface final : public IMbnInterface {
 public:
  explicit FakeMbnInterface(const wchar_t* aDeviceID,
                            HRESULT aCapsResult = S_OK)
      : mDeviceID(aDeviceID), mCapsResult(aCapsResult) {}

  ULONG RefCount() const { return mRefCnt; }

  STDMETHODIMP QueryInterface(REFIID aIid, void** aOut) override {
    if (aIid == IID_IUnknown || aIid == __uuidof(IMbnInterface)) {
      *aOut = static_cast<IMbnInterface*>(this);
      AddRef();
      return S_OK;
    }
    *aOut = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++mRefCnt; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG count = --mRefCnt;
    if (!count) {
      delete this;
    }
    return count;
  }

  STDMETHODIMP GetInterfaceCapability(MBN_INTERFACE_CAPS* aCaps) override {
    if (FAILED(mCapsResult)) {
      return mCapsResult;
    }
    *aCaps = {};
    aCaps->deviceID = mDeviceID ? SysAllocString(mDeviceID) : nullptr;
    aCaps->manufacturer = SysAllocString(L"Mozilla");
    aCaps->model = SysAllocString(L"Fake modem");
    return S_OK;
  }

  STDMETHODIMP get_InterfaceID(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSubscriberInformation(IMbnSubscriberInformation**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetReadyState(MBN_READY_STATE*) override { return E_NOTIMPL; }
  STDMETHODIMP InEmergencyMode(VARIANT_BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetHomeProvider(MBN_PROVIDER*) override { return E_NOTIMPL; }
  STDMETHODIMP GetPreferredProviders(SAFEARRAY**) override { return E_NOTIMPL; }
  STDMETHODIMP SetPreferredProviders(SAFEARRAY*, ULONG*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP GetVisibleProviders(ULONG*, SAFEARRAY**) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP ScanNetwork(ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetConnection(IMbnConnection**) override { return E_NOTIMPL; }

 private:
  ~FakeMbnInterface() = default;

  // GetImei() takes and drops references from a background thread.
  std::atomic<ULONG> mRefCnt = 0;
  const wchar_t* mDeviceID;
  HRESULT mCapsResult;
};

// Fake interface manager returning its interfaces as a SAFEARRAY, like
// MbnInterfaceManager does.
class FakeMbnInterfaceManager final : public IMbnInterfaceManager {
 public:
  enum class Result { Interfaces, NullArray, Failure };

  explicit FakeMbnInterfaceManager(Result aResult = Result::Interfaces)
      : mResult(aResult) {}

  void AddInterface(RefPtr<FakeMbnInterface> aInterface) {
    mInterfaces.AppendElement(std::move(aInterface));
  }

  STDMETHODIMP QueryInterface(REFIID aIid, void** aOut) override {
    if (aIid == IID_IUnknown || aIid == __uuidof(IMbnInterfaceManager)) {
      *aOut = static_cast<IMbnInterfaceManager*>(this);
      AddRef();
      return S_OK;
    }
    *aOut = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++mRefCnt; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG count = --mRefCnt;
    if (!count) {
      delete this;
    }
    return count;
  }

  STDMETHODIMP GetInterface(LPCWSTR, IMbnInterface**) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP GetInterfaces(SAFEARRAY** aInterfaces) override {
    *aInterfaces = nullptr;
    if (mResult == Result::Failure) {
      return E_FAIL;
    }
    if (mResult == Result::NullArray) {
      return S_OK;
    }
    SAFEARRAY* array =
        SafeArrayCreateVector(VT_UNKNOWN, 0, mInterfaces.Length());
    if (!array) {
      return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < LONG(mInterfaces.Length()); i++) {
      HRESULT hr = SafeArrayPutElement(
          array, &i, static_cast<IUnknown*>(mInterfaces[i].get()));
      if (FAILED(hr)) {
        SafeArrayDestroy(array);
        return hr;
      }
    }
    *aInterfaces = array;
    return S_OK;
  }

 private:
  ~FakeMbnInterfaceManager() = default;

  std::atomic<ULONG> mRefCnt = 0;
  Result mResult;
  nsTArray<RefPtr<FakeMbnInterface>> mInterfaces;
};

MobileBroadbandImeiProvider::ManagerFactory ReturnManager(
    RefPtr<FakeMbnInterfaceManager> aManager) {
  return [aManager]() -> already_AddRefed<IMbnInterfaceManager> {
    RefPtr<IMbnInterfaceManager> manager = aManager.get();
    return manager.forget();
  };
}

struct ImeiResult {
  Promise::PromiseState mState = Promise::PromiseState::Pending;
  nsString mImei;
  nsresult mError = NS_OK;
};

// Calls GetImei() on a provider using aFactory, and waits for the promise to
// settle.
ImeiResult GetImei(MobileBroadbandImeiProvider::ManagerFactory aFactory) {
  auto provider = MakeRefPtr<MobileBroadbandImeiProvider>(std::move(aFactory));

  AutoJSAPI jsapi;
  MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
  JSContext* cx = jsapi.cx();

  ImeiResult result;
  RefPtr<Promise> promise;
  if (NS_FAILED(provider->GetImei(cx, getter_AddRefs(promise))) || !promise) {
    ADD_FAILURE() << "GetImei() did not return a promise";
    return result;
  }

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil("TestMobileBroadbandImeiProvider"_ns, [&] {
    return promise->State() != Promise::PromiseState::Pending;
  }));
  result.mState = promise->State();

  JS::Rooted<JSObject*> promiseObj(cx, promise->PromiseObj());
  JS::Rooted<JS::Value> value(cx, JS::GetPromiseResult(promiseObj));
  if (result.mState == Promise::PromiseState::Resolved) {
    nsAutoJSString imei;
    EXPECT_TRUE(value.isString() && imei.init(cx, value));
    result.mImei = imei;
  } else {
    if (!value.isObject()) {
      ADD_FAILURE() << "Rejected with a non-object value";
      return result;
    }
    JS::Rooted<JSObject*> error(cx, &value.toObject());
    JS::Rooted<JS::Value> code(cx);
    EXPECT_TRUE(JS_GetProperty(cx, error, "result", &code) && code.isNumber());
    result.mError =
        static_cast<nsresult>(static_cast<uint32_t>(code.toNumber()));
  }
  return result;
}

}  // namespace

TEST(MobileBroadbandImeiProvider, ReturnsDeviceId)
{
  auto manager = MakeRefPtr<FakeMbnInterfaceManager>();
  auto modem = MakeRefPtr<FakeMbnInterface>(kTestImei);
  manager->AddInterface(modem);

  ImeiResult result = GetImei(ReturnManager(manager));
  EXPECT_EQ(result.mState, Promise::PromiseState::Resolved);
  EXPECT_TRUE(result.mImei.Equals(kTestImei));
  EXPECT_EQ(modem->RefCount(), 2u) << "Interface references are balanced";
}

TEST(MobileBroadbandImeiProvider, SkipsInterfacesWithoutDeviceId)
{
  auto manager = MakeRefPtr<FakeMbnInterfaceManager>();
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(kTestImei, E_FAIL));
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(nullptr));
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(L""));
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(kTestImei));

  ImeiResult result = GetImei(ReturnManager(manager));
  EXPECT_EQ(result.mState, Promise::PromiseState::Resolved);
  EXPECT_TRUE(result.mImei.Equals(kTestImei));
}

TEST(MobileBroadbandImeiProvider, NoUsableInterface)
{
  auto manager = MakeRefPtr<FakeMbnInterfaceManager>();
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(kTestImei, E_FAIL));
  manager->AddInterface(MakeRefPtr<FakeMbnInterface>(L""));

  ImeiResult result = GetImei(ReturnManager(manager));
  EXPECT_EQ(result.mState, Promise::PromiseState::Rejected);
  EXPECT_EQ(result.mError, NS_ERROR_NOT_IMPLEMENTED);
}

TEST(MobileBroadbandImeiProvider, NoInterfaces)
{
  ImeiResult result =
      GetImei(ReturnManager(MakeRefPtr<FakeMbnInterfaceManager>()));
  EXPECT_EQ(result.mState, Promise::PromiseState::Rejected);
  EXPECT_EQ(result.mError, NS_ERROR_NOT_IMPLEMENTED);

  result = GetImei(ReturnManager(MakeRefPtr<FakeMbnInterfaceManager>(
      FakeMbnInterfaceManager::Result::NullArray)));
  EXPECT_EQ(result.mState, Promise::PromiseState::Rejected);
  EXPECT_EQ(result.mError, NS_ERROR_NOT_IMPLEMENTED);
}

TEST(MobileBroadbandImeiProvider, GetInterfacesFails)
{
  ImeiResult result = GetImei(ReturnManager(MakeRefPtr<FakeMbnInterfaceManager>(
      FakeMbnInterfaceManager::Result::Failure)));
  EXPECT_EQ(result.mState, Promise::PromiseState::Rejected);
  EXPECT_EQ(result.mError, NS_ERROR_FAILURE);
}

TEST(MobileBroadbandImeiProvider, CreateManagerFails)
{
  ImeiResult result =
      GetImei([] { return already_AddRefed<IMbnInterfaceManager>(); });
  EXPECT_EQ(result.mState, Promise::PromiseState::Rejected);
  EXPECT_EQ(result.mError, NS_ERROR_FAILURE);
}
