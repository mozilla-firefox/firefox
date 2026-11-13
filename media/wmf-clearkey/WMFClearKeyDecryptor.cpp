/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFClearKeyDecryptor.h"

#include <mfapi.h>
#include <mferror.h>

#include <vector>

#include "WMFClearKeyCDM.h"
#include "WMFDecryptedBlock.h"
#include "content_decryption_module.h"

namespace mozilla {

using Microsoft::WRL::ComPtr;

WMFClearKeyDecryptor::~WMFClearKeyDecryptor() { ENTRY_LOG(); }

HRESULT WMFClearKeyDecryptor::RuntimeClassInitialize(
    SessionManagerWrapper* aSessionManager) {
  ENTRY_LOG();
  MOZ_ASSERT(aSessionManager);
  mSessionManager = aSessionManager;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetStreamLimits(DWORD* aInputMin,
                                                   DWORD* aInputMax,
                                                   DWORD* aOutputMin,
                                                   DWORD* aOutputMax) {
  *aInputMin = *aInputMax = *aOutputMin = *aOutputMax = 1;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetStreamCount(DWORD* aInputStreams,
                                                  DWORD* aOutputStreams) {
  *aInputStreams = *aOutputStreams = 1;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetStreamIDs(DWORD aInputSize,
                                                DWORD* aInputIDs,
                                                DWORD aOutputSize,
                                                DWORD* aOutputIDs) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::GetInputStreamInfo(
    DWORD aInputStreamID, MFT_INPUT_STREAM_INFO* aStreamInfo) {
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  aStreamInfo->hnsMaxLatency = 0;
  aStreamInfo->dwFlags = 0;
  aStreamInfo->cbSize = 0;
  aStreamInfo->cbMaxLookahead = 0;
  aStreamInfo->cbAlignment = 0;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetOutputStreamInfo(
    DWORD aOutputStreamID, MFT_OUTPUT_STREAM_INFO* aStreamInfo) {
  if (aOutputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  aStreamInfo->dwFlags = MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
  aStreamInfo->cbSize = 0;
  aStreamInfo->cbAlignment = 0;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetAttributes(IMFAttributes** aAttributes) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::GetInputStreamAttributes(
    DWORD aInputStreamID, IMFAttributes** aAttributes) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::GetOutputStreamAttributes(
    DWORD aOutputStreamID, IMFAttributes** aAttributes) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::DeleteInputStream(DWORD aStreamID) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::AddInputStreams(DWORD aStreams,
                                                   DWORD* aStreamIDs) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::GetInputAvailableType(DWORD aInputStreamID,
                                                         DWORD aTypeIndex,
                                                         IMFMediaType** aType) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  if (aTypeIndex != 0 || !mInputType) {
    return MF_E_NO_MORE_TYPES;
  }
  return mInputType.CopyTo(aType);
}

STDMETHODIMP WMFClearKeyDecryptor::GetOutputAvailableType(
    DWORD aOutputStreamID, DWORD aTypeIndex, IMFMediaType** aType) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (aOutputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  if (aTypeIndex != 0 || !mOutputType) {
    return MF_E_NO_MORE_TYPES;
  }
  return mOutputType.CopyTo(aType);
}

STDMETHODIMP WMFClearKeyDecryptor::SetInputType(DWORD aInputStreamID,
                                                IMFMediaType* aType,
                                                DWORD aFlags) {
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  std::lock_guard<std::mutex> lock(mMutex);
  if (aFlags & MFT_SET_TYPE_TEST_ONLY) {
    return S_OK;
  }
  // Unwrap the protected media type to get the real underlying media type.
  ComPtr<IMFMediaType> unwrapped;
  if (aType && SUCCEEDED(MFUnwrapMediaType(aType, &unwrapped))) {
    mInputType = unwrapped;
  } else {
    mInputType = aType;
  }
  if (!mOutputType && mInputType) {
    // Mirror input type as output type.
    RETURN_IF_FAILED(MFCreateMediaType(&mOutputType));
    RETURN_IF_FAILED(mInputType->CopyAllItems(mOutputType.Get()));
  }
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::SetOutputType(DWORD aOutputStreamID,
                                                 IMFMediaType* aType,
                                                 DWORD aFlags) {
  if (aOutputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  std::lock_guard<std::mutex> lock(mMutex);
  if (aFlags & MFT_SET_TYPE_TEST_ONLY) {
    return S_OK;
  }
  mOutputType = aType;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetInputCurrentType(DWORD aInputStreamID,
                                                       IMFMediaType** aType) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  if (!mInputType) {
    return MF_E_TRANSFORM_TYPE_NOT_SET;
  }
  return mInputType.CopyTo(aType);
}

STDMETHODIMP WMFClearKeyDecryptor::GetOutputCurrentType(DWORD aOutputStreamID,
                                                        IMFMediaType** aType) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (aOutputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  if (!mOutputType) {
    return MF_E_TRANSFORM_TYPE_NOT_SET;
  }
  return mOutputType.CopyTo(aType);
}

STDMETHODIMP WMFClearKeyDecryptor::GetInputStatus(DWORD aInputStreamID,
                                                  DWORD* aFlags) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  *aFlags = mInputSample ? 0 : MFT_INPUT_STATUS_ACCEPT_DATA;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::GetOutputStatus(DWORD* aFlags) {
  std::lock_guard<std::mutex> lock(mMutex);
  *aFlags = mInputSample ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::SetOutputBounds(LONGLONG aLowerBound,
                                                   LONGLONG aUpperBound) {
  return E_NOTIMPL;
}

STDMETHODIMP WMFClearKeyDecryptor::ProcessEvent(DWORD aInputStreamID,
                                                IMFMediaEvent* aEvent) {
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::ProcessMessage(MFT_MESSAGE_TYPE aMessage,
                                                  ULONG_PTR aParam) {
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::ProcessInput(DWORD aInputStreamID,
                                                IMFSample* aSample,
                                                DWORD aFlags) {
  ENTRY_LOG();
  if (aInputStreamID != 0) {
    return MF_E_INVALIDSTREAMNUMBER;
  }
  if (!aSample) {
    return E_INVALIDARG;
  }
  std::lock_guard<std::mutex> lock(mMutex);
  if (mInputSample) {
    return MF_E_NOTACCEPTING;
  }
  mInputSample = aSample;
  return S_OK;
}

HRESULT WMFClearKeyDecryptor::DecryptSample(IMFSample* aEncryptedSample,
                                            IMFSample** aDecryptedSample) {
  ENTRY_LOG();
  // Extract IV (stored as the SampleID blob, 16 bytes).
  BYTE* ivData = nullptr;
  UINT32 ivSize = 0;
  HRESULT hr = aEncryptedSample->GetAllocatedBlob(
      MFSampleExtension_Encryption_SampleID, &ivData, &ivSize);
  const bool isEncrypted = SUCCEEDED(hr) && ivSize > 0;
  LOG("isEncrypted=%d, ivSize=%u", isEncrypted, ivSize);

  // Copy all buffer data from the sample.
  DWORD totalLen = 0;
  RETURN_IF_FAILED(aEncryptedSample->GetTotalLength(&totalLen));

  std::vector<uint8_t> sampleData(totalLen);
  {
    ComPtr<IMFMediaBuffer> contiguousBuffer;
    RETURN_IF_FAILED(
        aEncryptedSample->ConvertToContiguousBuffer(&contiguousBuffer));
    BYTE* bufferData = nullptr;
    DWORD currentLen = 0;
    RETURN_IF_FAILED(contiguousBuffer->Lock(&bufferData, nullptr, &currentLen));
    if (currentLen > 0) {
      memcpy(sampleData.data(), bufferData, currentLen);
    }
    contiguousBuffer->Unlock();
  }

  WMFDecryptedBlock decryptedBlock;

  if (isEncrypted) {
    // Extract key ID (stored as a GUID).
    GUID keyIdGuid = GUID_NULL;
    RETURN_IF_FAILED(
        aEncryptedSample->GetGUID(MFSampleExtension_Content_KeyID, &keyIdGuid));
    uint8_t keyId[16];
    GuidToKeyId(keyIdGuid, keyId);

    // Extract subsample mapping if present.
    BYTE* subsampleData = nullptr;
    UINT32 subsampleDataSize = 0;
    std::vector<cdm::SubsampleEntry> subsamples;
    if (SUCCEEDED(aEncryptedSample->GetAllocatedBlob(
            MFSampleExtension_Encryption_SubSample_Mapping, &subsampleData,
            &subsampleDataSize)) &&
        subsampleDataSize >= 8) {
      // Each entry is {DWORD clearBytes, DWORD cipherBytes} = 8 bytes.
      const DWORD numEntries = subsampleDataSize / 8;
      subsamples.resize(numEntries);
      for (DWORD i = 0; i < numEntries; i++) {
        DWORD clearBytes = 0, cipherBytes = 0;
        memcpy(&clearBytes, subsampleData + i * 8, sizeof(DWORD));
        memcpy(&cipherBytes, subsampleData + i * 8 + sizeof(DWORD),
               sizeof(DWORD));
        subsamples[i].clear_bytes = clearBytes;
        subsamples[i].cipher_bytes = cipherBytes;
      }
      CoTaskMemFree(subsampleData);
    } else {
      if (subsampleData) {
        CoTaskMemFree(subsampleData);
      }
      // No subsample info: treat the whole buffer as cipher bytes.
      cdm::SubsampleEntry entry;
      entry.clear_bytes = 0;
      entry.cipher_bytes = totalLen;
      subsamples.push_back(entry);
    }

    // The pattern attributes are only set when both values are non-zero.
    UINT32 protectionScheme = MF_SAMPLE_ENCRYPTION_PROTECTION_SCHEME_AES_CTR;
    UINT32 cryptByteBlock = 0;
    UINT32 skipByteBlock = 0;
    (void)aEncryptedSample->GetUINT32(
        MFSampleExtension_Encryption_ProtectionScheme, &protectionScheme);
    (void)aEncryptedSample->GetUINT32(
        MFSampleExtension_Encryption_CryptByteBlock, &cryptByteBlock);
    (void)aEncryptedSample->GetUINT32(
        MFSampleExtension_Encryption_SkipByteBlock, &skipByteBlock);
    const bool isCbcs =
        protectionScheme == MF_SAMPLE_ENCRYPTION_PROTECTION_SCHEME_AES_CBC;
    LOG("scheme=%s, pattern=%u:%u", isCbcs ? "cbcs" : "cenc", cryptByteBlock,
        skipByteBlock);

    LONGLONG sampleTime = 0;
    aEncryptedSample->GetSampleTime(&sampleTime);

    cdm::InputBuffer_2 inputBuffer = {};
    inputBuffer.data = sampleData.data();
    inputBuffer.data_size = static_cast<uint32_t>(sampleData.size());
    inputBuffer.encryption_scheme =
        isCbcs ? cdm::EncryptionScheme::kCbcs : cdm::EncryptionScheme::kCenc;
    inputBuffer.pattern = {cryptByteBlock, skipByteBlock};
    inputBuffer.key_id = keyId;
    inputBuffer.key_id_size = 16;
    inputBuffer.iv = ivData;
    inputBuffer.iv_size = ivSize;
    inputBuffer.subsamples = subsamples.data();
    inputBuffer.num_subsamples = static_cast<uint32_t>(subsamples.size());
    inputBuffer.timestamp = sampleTime;

    HRESULT decryptHr = mSessionManager->Decrypt(inputBuffer, &decryptedBlock);
    LOG("Decrypt hr=%lx", static_cast<long>(decryptHr));
    CoTaskMemFree(ivData);
    RETURN_IF_FAILED(decryptHr);
  } else {
    if (ivData) {
      CoTaskMemFree(ivData);
    }
    LOG("Clear sample passthrough, size=%lu", totalLen);
    WMFDecryptedBuffer* buffer =
        new WMFDecryptedBuffer(static_cast<uint32_t>(sampleData.size()));
    memcpy(buffer->Data(), sampleData.data(), sampleData.size());
    decryptedBlock.SetDecryptedBuffer(buffer);
  }

  cdm::Buffer* decryptedBuffer = decryptedBlock.DecryptedBuffer();
  if (!decryptedBuffer || !decryptedBuffer->Data()) {
    LOG("Decrypt produced null or empty buffer");
    return E_FAIL;
  }

  // Wrap decrypted data in a new IMFSample.
  ComPtr<IMFSample> outputSample;
  RETURN_IF_FAILED(MFCreateSample(&outputSample));

  ComPtr<IMFMediaBuffer> outputBuffer;
  RETURN_IF_FAILED(MFCreateMemoryBuffer(
      static_cast<DWORD>(decryptedBuffer->Size()), &outputBuffer));

  BYTE* outputData = nullptr;
  RETURN_IF_FAILED(outputBuffer->Lock(&outputData, nullptr, nullptr));
  memcpy(outputData, decryptedBuffer->Data(), decryptedBuffer->Size());
  outputBuffer->Unlock();
  RETURN_IF_FAILED(outputBuffer->SetCurrentLength(
      static_cast<DWORD>(decryptedBuffer->Size())));

  RETURN_IF_FAILED(outputSample->AddBuffer(outputBuffer.Get()));

  LONGLONG sampleTime = 0;
  LONGLONG sampleDuration = 0;
  if (SUCCEEDED(aEncryptedSample->GetSampleTime(&sampleTime))) {
    outputSample->SetSampleTime(sampleTime);
  }
  if (SUCCEEDED(aEncryptedSample->GetSampleDuration(&sampleDuration))) {
    outputSample->SetSampleDuration(sampleDuration);
  }

  // Propagate the keyframe flag so the downstream H264 decoder knows which
  // samples are IDR frames.
  UINT32 isCleanPoint = 0;
  if (SUCCEEDED(aEncryptedSample->GetUINT32(MFSampleExtension_CleanPoint,
                                            &isCleanPoint)) &&
      isCleanPoint) {
    outputSample->SetUINT32(MFSampleExtension_CleanPoint, 1);
  }

  *aDecryptedSample = outputSample.Detach();
  return S_OK;
}

STDMETHODIMP WMFClearKeyDecryptor::ProcessOutput(
    DWORD aFlags, DWORD aOutputBufferCount,
    MFT_OUTPUT_DATA_BUFFER* aOutputSamples, DWORD* aStatus) {
  ENTRY_LOG();
  *aStatus = 0;

  if (aOutputBufferCount != 1) {
    LOG("Invalid output buffer count: %lu", aOutputBufferCount);
    return E_INVALIDARG;
  }

  ComPtr<IMFSample> encryptedSample;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mInputSample) {
      LOG("No input sample available");
      return MF_E_TRANSFORM_NEED_MORE_INPUT;
    }
    encryptedSample = std::move(mInputSample);
  }

  // The MFT allocates output samples (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES).
  MOZ_ASSERT(!aOutputSamples[0].pSample);

  ComPtr<IMFSample> decryptedSample;
  RETURN_IF_FAILED(DecryptSample(encryptedSample.Get(), &decryptedSample));

  aOutputSamples[0].pSample = decryptedSample.Detach();
  return S_OK;
}

}  // namespace mozilla
