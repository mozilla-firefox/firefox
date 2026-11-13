/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gtest/gtest.h>
#include <mfidl.h>
#include <windows.h>
#include <wrl.h>

#include "MockSessionManagerWrapper.h"
// The mock must come first: it replaces the CDM header that the source
// includes.
#include "../WMFClearKeySession.cpp"

using namespace Microsoft::WRL;
using namespace mozilla;

namespace {

class WMFClearKeySessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_HRESULT_SUCCEEDED(
        MakeAndInitialize<MockSessionCallbacks>(&mCallbacks));
    // Assign from a raw pointer so RefPtr::operator=(T*) runs and correctly
    // AddRefs the object (avoids a UAF with this RefPtr impl, as this is not
    // Mozilla's standard RefPtr).
    SessionManagerWrapper* mgr = new SessionManagerWrapper();
    mMgr = mgr;
    ASSERT_HRESULT_SUCCEEDED(MakeAndInitialize<WMFClearKeySession>(
        &mSession, MF_MEDIAKEYSESSION_TYPE_TEMPORARY, mCallbacks.Get(), mgr));
    mSession->SetSessionIdForTesting("test-session");
  }

  class MockSessionCallbacks
      : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                            IMFContentDecryptionModuleSessionCallbacks> {
   public:
    STDMETHODIMP KeyMessage(MF_MEDIAKEYSESSION_MESSAGETYPE, const BYTE*, DWORD,
                            LPCWSTR) override {
      return S_OK;
    }
    STDMETHODIMP KeyStatusChanged() override { return S_OK; }
  };

  static void ExpectSingleKeyStatusEquals(WMFClearKeySession* session,
                                          const uint8_t* keyId,
                                          size_t keyIdSize) {
    cdm::KeyInformation keyInfo;
    keyInfo.key_id = keyId;
    keyInfo.key_id_size = static_cast<uint32_t>(keyIdSize);
    keyInfo.status = cdm::KeyStatus::kUsable;
    keyInfo.system_code = 0;

    session->OnKeyStatusChanged(&keyInfo, 1);

    MFMediaKeyStatus* statuses = nullptr;
    UINT count = 0;
    ASSERT_HRESULT_SUCCEEDED(session->GetKeyStatuses(&statuses, &count));
    ASSERT_NE(statuses, nullptr);

    ASSERT_EQ(count, 1u);
    EXPECT_EQ(statuses[0].cbKeyId, keyIdSize);
    ASSERT_NE(statuses[0].pbKeyId, nullptr);
    EXPECT_EQ(memcmp(statuses[0].pbKeyId, keyId, keyIdSize), 0);

    CoTaskMemFree(statuses[0].pbKeyId);
    CoTaskMemFree(statuses);
  }

  ComPtr<MockSessionCallbacks> mCallbacks;
  RefPtr<SessionManagerWrapper> mMgr;
  ComPtr<WMFClearKeySession> mSession;
};

}  // namespace

TEST_F(WMFClearKeySessionTest, GetKeyStatusesSixteenByteKeyId) {
  const uint8_t kKeyId[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

  ExpectSingleKeyStatusEquals(mSession.Get(), kKeyId, sizeof(kKeyId));
}

TEST_F(WMFClearKeySessionTest, GetKeyStatusesEightByteKeyId) {
  const uint8_t kKeyId[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

  ExpectSingleKeyStatusEquals(mSession.Get(), kKeyId, sizeof(kKeyId));
}

TEST_F(WMFClearKeySessionTest, GetKeyStatusesThirtyTwoByteKeyId) {
  const uint8_t kKeyId[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

  ExpectSingleKeyStatusEquals(mSession.Get(), kKeyId, sizeof(kKeyId));
}
