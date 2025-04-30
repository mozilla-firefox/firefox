/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFEncoderModule.h"

#include "WMFMediaDataEncoder.h"

namespace mozilla {
extern LazyLogModule sPEMLog;

bool WMFEncoderModule::SupportsCodec(CodecType aCodecType) const {
  if (aCodecType > CodecType::_BeginAudio_ &&
      aCodecType < CodecType::_EndAudio_) {
    return false;
  }
  return CanCreateWMFEncoder(aCodecType);
}

bool WMFEncoderModule::Supports(const EncoderConfig& aConfig) const {
  if (!CanLikelyEncode(aConfig)) {
    return false;
  }
  if (aConfig.IsAudio()) {
    return false;
  }
  if (aConfig.mScalabilityMode != ScalabilityMode::None) {
    return aConfig.mCodec == CodecType::H264;
  }
  return SupportsCodec(aConfig.mCodec);
}

already_AddRefed<MediaDataEncoder> WMFEncoderModule::CreateVideoEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  RefPtr<MediaDataEncoder> encoder(
      new WMFMediaDataEncoder(aConfig, aTaskQueue));
  return encoder.forget();
}

}  // namespace mozilla
