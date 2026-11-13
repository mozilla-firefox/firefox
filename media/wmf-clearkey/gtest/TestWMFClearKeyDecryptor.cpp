/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gtest/gtest.h>
#include <mfapi.h>
#include <mfidl.h>
#include <windows.h>
#include <wrl.h>

#include <vector>

#include "MockSessionManagerWrapper.h"
// The mock must come first: it replaces the CDM header that the source
// includes.
#include "../WMFClearKeyDecryptor.cpp"

using namespace Microsoft::WRL;
using namespace mozilla;

namespace {

struct EncryptedSampleCase {
  const char* mName;
  MFSampleEncryptionProtectionScheme mProtectionScheme;
  // Zero means the pattern attributes are not set on the sample.
  UINT32 mCryptByteBlock;
  UINT32 mSkipByteBlock;
  std::vector<uint8_t> mIV;
  cdm::EncryptionScheme mExpectedScheme;
};

class WMFClearKeyDecryptorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_HRESULT_SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    SessionManagerWrapper* mgr = new SessionManagerWrapper();
    mMgr = mgr;
    ASSERT_HRESULT_SUCCEEDED(
        MakeAndInitialize<WMFClearKeyDecryptor>(&mDecryptor, mgr));
  }

  void TearDown() override { MFShutdown(); }

  ComPtr<IMFSample> CreateEncryptedSample(const EncryptedSampleCase& aCase) {
    ComPtr<IMFSample> sample;
    EXPECT_HRESULT_SUCCEEDED(MFCreateSample(&sample));
    ComPtr<IMFMediaBuffer> buffer;
    EXPECT_HRESULT_SUCCEEDED(MFCreateMemoryBuffer(kSampleSize, &buffer));
    EXPECT_HRESULT_SUCCEEDED(buffer->SetCurrentLength(kSampleSize));
    EXPECT_HRESULT_SUCCEEDED(sample->AddBuffer(buffer.Get()));

    EXPECT_HRESULT_SUCCEEDED(
        sample->SetUINT32(MFSampleExtension_Encryption_ProtectionScheme,
                          aCase.mProtectionScheme));
    if (aCase.mCryptByteBlock > 0 && aCase.mSkipByteBlock > 0) {
      EXPECT_HRESULT_SUCCEEDED(sample->SetUINT32(
          MFSampleExtension_Encryption_CryptByteBlock, aCase.mCryptByteBlock));
      EXPECT_HRESULT_SUCCEEDED(sample->SetUINT32(
          MFSampleExtension_Encryption_SkipByteBlock, aCase.mSkipByteBlock));
    }
    EXPECT_HRESULT_SUCCEEDED(
        sample->SetGUID(MFSampleExtension_Content_KeyID, kKeyId));
    EXPECT_HRESULT_SUCCEEDED(
        sample->SetBlob(MFSampleExtension_Encryption_SampleID, aCase.mIV.data(),
                        aCase.mIV.size()));
    return sample;
  }

  HRESULT Decrypt(IMFSample* aSample) {
    HRESULT hr = mDecryptor->ProcessInput(0, aSample, 0);
    if (FAILED(hr)) {
      return hr;
    }
    MFT_OUTPUT_DATA_BUFFER output = {};
    DWORD status = 0;
    hr = mDecryptor->ProcessOutput(0, 1, &output, &status);
    if (output.pSample) {
      output.pSample->Release();
    }
    return hr;
  }

  static constexpr DWORD kSampleSize = 64;
  static constexpr GUID kKeyId = {
      0x11111111,
      0x2222,
      0x3333,
      {0x44, 0x44, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55}};

  RefPtr<SessionManagerWrapper> mMgr;
  ComPtr<WMFClearKeyDecryptor> mDecryptor;
};

TEST_F(WMFClearKeyDecryptorTest, PassesProtectionSchemeAndPattern) {
  const EncryptedSampleCase cases[] = {
      {"cenc", MF_SAMPLE_ENCRYPTION_PROTECTION_SCHEME_AES_CTR, 0, 0,
       std::vector<uint8_t>(8, 0x01), cdm::EncryptionScheme::kCenc},
      {"cbcs 1:9", MF_SAMPLE_ENCRYPTION_PROTECTION_SCHEME_AES_CBC, 1, 9,
       std::vector<uint8_t>(16, 0x02), cdm::EncryptionScheme::kCbcs},
      {"cbcs without pattern", MF_SAMPLE_ENCRYPTION_PROTECTION_SCHEME_AES_CBC,
       0, 0, std::vector<uint8_t>(16, 0x03), cdm::EncryptionScheme::kCbcs},
  };

  for (const EncryptedSampleCase& testCase : cases) {
    SCOPED_TRACE(testCase.mName);

    // Decrypt one sample carrying the per-sample attributes the media engine
    // sets.
    ComPtr<IMFSample> sample = CreateEncryptedSample(testCase);
    ASSERT_HRESULT_SUCCEEDED(Decrypt(sample.Get()));

    // The CDM must receive the sample's scheme, pattern and IV.
    EXPECT_EQ(mMgr->mLastEncryptionScheme, testCase.mExpectedScheme);
    EXPECT_EQ(mMgr->mLastPattern.crypt_byte_block, testCase.mCryptByteBlock);
    EXPECT_EQ(mMgr->mLastPattern.skip_byte_block, testCase.mSkipByteBlock);
    EXPECT_EQ(mMgr->mLastIV, testCase.mIV);
  }
}

}  // namespace
