/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORMS_ANDROID_ANDROIDENCODERMODULE_H_
#define DOM_MEDIA_PLATFORMS_ANDROID_ANDROIDENCODERMODULE_H_

#include "PlatformEncoderModule.h"

namespace mozilla {

class AndroidEncoderModule final : public PlatformEncoderModule {
 public:
  AndroidEncoderModule() = default;
  virtual ~AndroidEncoderModule() = default;
  // aCodec is the full codec string
  media::EncodeSupportSet Supports(const EncoderConfig& aConfig) const override;
  media::EncodeSupportSet SupportsCodec(CodecType aCodec) const override;
  Maybe<uint32_t> MaxPixelsPerFrame(CodecType aCodec) const override;

  const char* GetName() const override { return "Android Encoder Module"; }

  already_AddRefed<MediaDataEncoder> CreateVideoEncoder(
      const EncoderConfig& aConfig,
      const RefPtr<TaskQueue>& aTaskQueue) const override;
};

}  // namespace mozilla

#endif
