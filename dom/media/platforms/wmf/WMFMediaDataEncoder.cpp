/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFMediaDataEncoder.h"

#include "ImageContainer.h"
#include "ImageConversion.h"
#include "MFTEncoder.h"
#include "PlatformEncoderModule.h"
#include "TimeUnits.h"
#include "WMFDataEncoderUtils.h"
#include "WMFUtils.h"
#include <comdef.h>
#include "mozilla/WindowsProcessMitigations.h"

namespace mozilla {

using InitPromise = MediaDataEncoder::InitPromise;
using EncodePromise = MediaDataEncoder::EncodePromise;
using ReconfigurationPromise = MediaDataEncoder::ReconfigurationPromise;

WMFMediaDataEncoder::WMFMediaDataEncoder(const EncoderConfig& aConfig,
                                         const RefPtr<TaskQueue>& aTaskQueue)
    : mConfig(aConfig),
      mTaskQueue(aTaskQueue),
      mHardwareNotAllowed(aConfig.mHardwarePreference ==
                              HardwarePreference::RequireSoftware ||
                          IsWin32kLockedDown()) {
  WMF_ENC_LOGE("WMFMediaDataEncoder ctor: %s, (hw not allowed: %s)",
               aConfig.ToString().get(), mHardwareNotAllowed ? "yes" : "no");
  MOZ_ASSERT(mTaskQueue);
}

RefPtr<InitPromise> WMFMediaDataEncoder::Init() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &WMFMediaDataEncoder::ProcessInit);
}
RefPtr<EncodePromise> WMFMediaDataEncoder::Encode(const MediaData* aSample) {
  WMF_ENC_LOGD("Encode ts=%s", aSample->mTime.ToString().get());
  MOZ_ASSERT(aSample);

  RefPtr<const VideoData> sample(aSample->As<const VideoData>());

  return InvokeAsync<RefPtr<const VideoData>>(
      mTaskQueue, this, __func__, &WMFMediaDataEncoder::ProcessEncode,
      std::move(sample));
}
RefPtr<EncodePromise> WMFMediaDataEncoder::Drain() {
  WMF_ENC_LOGD("Drain");
  return InvokeAsync(mTaskQueue, __func__,
                     [self = RefPtr<WMFMediaDataEncoder>(this)]() {
                       nsTArray<RefPtr<IMFSample>> outputs;
                       return SUCCEEDED(self->mEncoder->Drain(outputs))
                                  ? self->ProcessOutputSamples(outputs)
                                  : EncodePromise::CreateAndReject(
                                        NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
                     });
}
RefPtr<ShutdownPromise> WMFMediaDataEncoder::Shutdown() {
  WMF_ENC_LOGD("Shutdown");
  return InvokeAsync(mTaskQueue, __func__,
                     [self = RefPtr<WMFMediaDataEncoder>(this)]() {
                       if (self->mEncoder) {
                         self->mEncoder->Destroy();
                         self->mEncoder = nullptr;
                       }
                       return ShutdownPromise::CreateAndResolve(true, __func__);
                     });
}
RefPtr<GenericPromise> WMFMediaDataEncoder::SetBitrate(uint32_t aBitsPerSec) {
  return InvokeAsync(
      mTaskQueue, __func__,
      [self = RefPtr<WMFMediaDataEncoder>(this), aBitsPerSec]() {
        MOZ_ASSERT(self->mEncoder);
        return SUCCEEDED(self->mEncoder->SetBitrate(aBitsPerSec))
                   ? GenericPromise::CreateAndResolve(true, __func__)
                   : GenericPromise::CreateAndReject(
                         NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR, __func__);
      });
}

RefPtr<ReconfigurationPromise> WMFMediaDataEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  // General reconfiguration interface not implemented right now
  return MediaDataEncoder::ReconfigurationPromise::CreateAndReject(
      NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
};

nsCString WMFMediaDataEncoder::GetDescriptionName() const {
  return MFTEncoder::GetFriendlyName(CodecToSubtype(mConfig.mCodec));
}

RefPtr<InitPromise> WMFMediaDataEncoder::ProcessInit() {
  AssertOnTaskQueue();

  WMF_ENC_LOGD("ProcessInit");

  MOZ_ASSERT(!mEncoder,
             "Should not initialize encoder again without shutting down");

  if (!wmf::MediaFoundationInitializer::HasInitialized()) {
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Can't create the MFT encoder.")),
        __func__);
  }

  RefPtr<MFTEncoder> encoder = new MFTEncoder(mHardwareNotAllowed);
  HRESULT hr;
  mscom::EnsureMTA([&]() { hr = InitMFTEncoder(encoder); });

  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("init MFTEncoder: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Can't create the MFT encoder.")),
        __func__);
  }

  mEncoder = std::move(encoder);
  FillConfigData();
  return InitPromise::CreateAndResolve(TrackInfo::TrackType::kVideoTrack,
                                       __func__);
}

HRESULT WMFMediaDataEncoder::InitMFTEncoder(RefPtr<MFTEncoder>& aEncoder) {
  HRESULT hr = aEncoder->Create(CodecToSubtype(mConfig.mCodec));
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("MFTEncoder::Create: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return hr;
  }

  hr = aEncoder->SetModes(mConfig);
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("MFTEncoder::SetMode: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return hr;
  }

  hr = SetMediaTypes(aEncoder, mConfig);
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("MFTEncoder::SetMediaType: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return hr;
  }

  return S_OK;
}

void WMFMediaDataEncoder::FillConfigData() {
  nsTArray<UINT8> header;
  NS_ENSURE_TRUE_VOID(SUCCEEDED(mEncoder->GetMPEGSequenceHeader(header)));

  if (mConfig.mCodec != CodecType::H264) {
    return;
  }

  mConfigData =
      header.Length() > 0 ? ParseH264Parameters(header, IsAnnexB()) : nullptr;
}

RefPtr<EncodePromise> WMFMediaDataEncoder::ProcessEncode(
    RefPtr<const VideoData>&& aSample) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(aSample);

  WMF_ENC_LOGD("ProcessEncode ts=%s duration=%s",
               aSample->mTime.ToString().get(),
               aSample->mDuration.ToString().get());

  RefPtr<IMFSample> nv12 = ConvertToNV12InputSample(std::move(aSample));

  MFTEncoder::InputSample inputSample{nv12, aSample->mKeyframe};

  if (!nv12 || FAILED(mEncoder->PushInput(inputSample))) {
    WMF_ENC_LOGE("failed to process input sample");
    return EncodePromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Failed to process input.")),
        __func__);
  }

  nsTArray<RefPtr<IMFSample>> outputs;
  HRESULT hr = mEncoder->TakeOutput(outputs);
  if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
    FillConfigData();
  } else if (FAILED(hr)) {
    WMF_ENC_LOGE("failed to process output");
    return EncodePromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Failed to process output.")),
        __func__);
  }

  return ProcessOutputSamples(outputs);
}

already_AddRefed<IMFSample> WMFMediaDataEncoder::ConvertToNV12InputSample(
    RefPtr<const VideoData>&& aData) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mEncoder);

  size_t mBufferLength = 0;

  const int32_t ySrtride = mConfig.mSize.width;
  const int32_t uvStride = ySrtride;

  const int32_t yHeight = mConfig.mSize.height;
  const int32_t uvHeight = yHeight / 2 + (yHeight % 2);

  CheckedInt<size_t> yLength(ySrtride);
  yLength *= yHeight;
  if (!yLength.isValid()) {
    WMF_ENC_LOGE("dest yLength overflows");
    return nullptr;
  }

  CheckedInt<size_t> uvLength(uvStride);
  uvLength *= uvHeight;
  if (!uvLength.isValid()) {
    WMF_ENC_LOGE("dest uvLength overflows");
    return nullptr;
  }

  CheckedInt<size_t> length(yLength);
  length += uvLength;
  if (!length.isValid()) {
    WMF_ENC_LOGE("dest length overflows");
    return nullptr;
  }
  mBufferLength = length.value();

  RefPtr<IMFSample> input;
  HRESULT hr = mEncoder->CreateInputSample(&input, mBufferLength);
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("CreateInputSample: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return nullptr;
  }

  RefPtr<IMFMediaBuffer> buffer;
  hr = input->GetBufferByIndex(0, getter_AddRefs(buffer));
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("GetBufferByIndex: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return nullptr;
  }

  hr = buffer->SetCurrentLength(mBufferLength);
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("SetCurrentLength: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return nullptr;
  }

  LockBuffer lockBuffer(buffer);
  hr = lockBuffer.Result();
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("LockBuffer: error = 0x%lX, %ls", hr, error.ErrorMessage());
    return nullptr;
  }

  nsresult rv = ConvertToNV12(aData->mImage, lockBuffer.Data(), ySrtride,
                              lockBuffer.Data() + yLength.value(), uvStride,
                              mConfig.mSize);
  if (NS_FAILED(rv)) {
    WMF_ENC_LOGE("Failed to convert to NV12");
    return nullptr;
  }

  hr = input->SetSampleTime(UsecsToHNs(aData->mTime.ToMicroseconds()));
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("SetSampleTime: error = 0x%lX, %ls", hr, error.ErrorMessage());
    return nullptr;
  }

  hr = input->SetSampleDuration(UsecsToHNs(aData->mDuration.ToMicroseconds()));
  if (FAILED(hr)) {
    _com_error error(hr);
    WMF_ENC_LOGE("SetSampleDuration: error = 0x%lX, %ls", hr,
                 error.ErrorMessage());
    return nullptr;
  }

  return input.forget();
}

RefPtr<EncodePromise> WMFMediaDataEncoder::ProcessOutputSamples(
    nsTArray<RefPtr<IMFSample>>& aSamples) {
  EncodedData frames;

  WMF_ENC_LOGD("ProcessOutputSamples: %zu frames", aSamples.Length());

  for (auto sample : aSamples) {
    RefPtr<MediaRawData> frame = IMFSampleToMediaData(sample);
    if (frame) {
      frames.AppendElement(std::move(frame));
    } else {
      WMF_ENC_LOGE("failed to convert output frame");
    }
  }
  aSamples.Clear();
  return EncodePromise::CreateAndResolve(std::move(frames), __func__);
}

already_AddRefed<MediaRawData> WMFMediaDataEncoder::IMFSampleToMediaData(
    RefPtr<IMFSample>& aSample) {
  AssertOnTaskQueue();
  MOZ_ASSERT(aSample);

  RefPtr<IMFMediaBuffer> buffer;
  HRESULT hr = aSample->GetBufferByIndex(0, getter_AddRefs(buffer));
  NS_ENSURE_TRUE(SUCCEEDED(hr), nullptr);

  LockBuffer lockBuffer(buffer);
  NS_ENSURE_TRUE(SUCCEEDED(lockBuffer.Result()), nullptr);

  LONGLONG time = 0;
  hr = aSample->GetSampleTime(&time);
  NS_ENSURE_TRUE(SUCCEEDED(hr), nullptr);

  LONGLONG duration = 0;
  hr = aSample->GetSampleDuration(&duration);
  NS_ENSURE_TRUE(SUCCEEDED(hr), nullptr);

  bool isKeyframe =
      MFGetAttributeUINT32(aSample, MFSampleExtension_CleanPoint, false);

  auto frame = MakeRefPtr<MediaRawData>();

  if (mConfig.mCodec == CodecType::H264 &&
      mConfig.mScalabilityMode != ScalabilityMode::None) {
    auto maybeId =
        H264::ExtractSVCTemporalId(lockBuffer.Data(), lockBuffer.Length());
    frame->mTemporalLayerId = Some(maybeId.unwrapOr(0));
  }

  if (!WriteFrameData(frame, lockBuffer, isKeyframe)) {
    return nullptr;
  }

  frame->mTime = media::TimeUnit::FromMicroseconds(HNsToUsecs(time));
  frame->mDuration = media::TimeUnit::FromMicroseconds(HNsToUsecs(duration));
  frame->mKeyframe = isKeyframe;

  WMF_ENC_LOGD("IMGSampleToMediaData: ts=%s", frame->mTime.ToString().get());

  return frame.forget();
}

bool WMFMediaDataEncoder::IsAnnexB() const {
  MOZ_ASSERT(mConfig.mCodec == CodecType::H264);
  return mConfig.mCodecSpecific->as<H264Specific>().mFormat ==
         H264BitStreamFormat::ANNEXB;
}

bool WMFMediaDataEncoder::WriteFrameData(RefPtr<MediaRawData>& aDest,
                                         LockBuffer& aSrc, bool aIsKeyframe) {
  // From raw encoded data, write in avCC or AnnexB format depending on the
  // config.

  if (mConfig.mCodec == CodecType::H264) {
    size_t prependLength = 0;
    RefPtr<MediaByteBuffer> avccHeader;
    if (aIsKeyframe && mConfigData) {
      if (IsAnnexB()) {
        const nsTArray<NAL_TYPES> aTypes = {H264_NAL_SPS, H264_NAL_PPS};
        if (!AnnexB::FindAllNalTypes(
                Span<const uint8_t>(aSrc.Data(), aSrc.Length()), aTypes)) {
          prependLength = mConfigData->Length();
        }
      } else {
        avccHeader = mConfigData;
      }
    }

    UniquePtr<MediaRawDataWriter> writer(aDest->CreateWriter());
    if (!writer->SetSize(prependLength + aSrc.Length())) {
      WMF_ENC_LOGE("fail to allocate output buffer");
      return false;
    }

    if (prependLength > 0) {
      PodCopy(writer->Data(), mConfigData->Elements(), prependLength);
    }
    PodCopy(writer->Data() + prependLength, aSrc.Data(), aSrc.Length());

    if (!IsAnnexB() && !AnnexB::ConvertSampleToAVCC(aDest, avccHeader)) {
      WMF_ENC_LOGE("fail to convert annex-b sample to AVCC");
      return false;
    }

    return true;
  }
  UniquePtr<MediaRawDataWriter> writer(aDest->CreateWriter());
  if (!writer->SetSize(aSrc.Length())) {
    WMF_ENC_LOGE("fail to allocate output buffer");
    return false;
  }

  PodCopy(writer->Data(), aSrc.Data(), aSrc.Length());
  return true;
}

}  // namespace mozilla
