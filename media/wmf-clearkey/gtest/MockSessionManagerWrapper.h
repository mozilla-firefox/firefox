/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIA_WMF_CLEARKEY_GTEST_MOCKSESSIONMANAGERWRAPPER_H
#define MEDIA_WMF_CLEARKEY_GTEST_MOCKSESSIONMANAGERWRAPPER_H

#include <mfidl.h>
#include <windows.h>

#include <string>
#include <vector>

#include "RefCounted.h"
#include "WMFDecryptedBlock.h"
#include "content_decryption_module.h"

// Define the include guard of WMFClearKeyCDM.h before including a
// WMFClearKey*.cpp source so the preprocessor skips the real header when that
// source includes it from its own directory. This allows the source to compile
// without pulling in the full CDM stack.
#define DOM_MEDIA_PLATFORM_WMF_CLEARKEY_WMFCLEARKEYCDM_H

namespace mozilla {

class WMFClearKeySession;

class SessionManagerWrapper : public RefCounted {
 public:
  HRESULT GenerateRequest(cdm::InitDataType, const BYTE*, DWORD,
                          cdm::SessionType, WMFClearKeySession*, std::string&) {
    return E_NOTIMPL;
  }
  HRESULT UpdateSession(const std::string&, const BYTE*, DWORD) {
    return E_NOTIMPL;
  }
  HRESULT CloseSession(const std::string&) { return E_NOTIMPL; }
  HRESULT RemoveSession(const std::string&) { return E_NOTIMPL; }
  void Shutdown() {}
  bool IsShutdown() { return false; }

  // Records the input and returns it unchanged as the decrypted output.
  HRESULT Decrypt(const cdm::InputBuffer_2& aBuffer,
                  cdm::DecryptedBlock* aDecryptedBlock) {
    mLastEncryptionScheme = aBuffer.encryption_scheme;
    mLastPattern = aBuffer.pattern;
    mLastIV.assign(aBuffer.iv, aBuffer.iv + aBuffer.iv_size);
    WMFDecryptedBuffer* buffer = new WMFDecryptedBuffer(aBuffer.data_size);
    memcpy(buffer->Data(), aBuffer.data, aBuffer.data_size);
    aDecryptedBlock->SetDecryptedBuffer(buffer);
    return S_OK;
  }

  cdm::EncryptionScheme mLastEncryptionScheme =
      cdm::EncryptionScheme::kUnencrypted;
  cdm::Pattern mLastPattern = {};
  std::vector<uint8_t> mLastIV;

 private:
  ~SessionManagerWrapper() override = default;
};

}  // namespace mozilla

#endif  // MEDIA_WMF_CLEARKEY_GTEST_MOCKSESSIONMANAGERWRAPPER_H
