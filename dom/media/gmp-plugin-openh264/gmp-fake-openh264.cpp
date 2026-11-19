/*!
 * \copy
 *     Copyright (c)  2009-2014, Cisco Systems
 *     Copyright (c)  2014, Mozilla
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *************************************************************************************
 */

#include <assert.h>
#include <stdint.h>

#include <cstring>
#include <iostream>
#include <numeric>

#include "gmp-platform.h"
#include "gmp-video-decode.h"
#include "gmp-video-encode.h"
#include "gmp-video-frame-encoded.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-host.h"
#include "mozilla/PodOperations.h"

#if defined(_MSC_VER)
#  define PUBLIC_FUNC __declspec(dllexport)
#else
#  define PUBLIC_FUNC
#endif

#define BIG_FRAME 10000

#define GL_CRIT 0
#define GL_ERROR 1
#define GL_INFO 2
#define GL_DEBUG 3

const char* kLogStrings[] = {"Critical", "Error", "Info", "Debug"};

static int g_log_level = GL_CRIT;

#define GMPLOG(l, x)                                \
  do {                                              \
    if (l <= g_log_level) {                         \
      const char* log_string = "unknown";           \
      if ((l >= 0) && (l <= 3)) {                   \
        log_string = kLogStrings[l];                \
      }                                             \
      std::cerr << log_string << ": " << x << '\n'; \
    }                                               \
  } while (0)

class FakeVideoEncoder;
class FakeVideoDecoder;

// Turn off padding for this structure. Having extra bytes can result in
// bitstream parsing problems.
#pragma pack(push, 1)
struct EncodedFrame {
  struct SPSNalu {
    uint32_t size_;
    uint8_t payload[14];
  } sps_nalu;
  struct PPSNalu {
    uint32_t size_;
    uint8_t payload[4];
  } pps_nalu;
  struct IDRNalu {
    uint32_t size_;
    uint8_t h264_compat_;
    uint32_t magic_;
    uint32_t width_;
    uint32_t height_;
    uint8_t y_;
    uint8_t u_;
    uint8_t v_;
    uint64_t timestamp_;
  } idr_nalu;
};
#pragma pack(pop)

#define ENCODED_FRAME_MAGIC 0x004000b8

template <typename T>
class SelfDestruct {
 public:
  explicit SelfDestruct(T* t) : t_(t) {}
  ~SelfDestruct() {
    if (t_) {
      t_->Destroy();
    }
  }

 private:
  T* t_;
};

class FakeVideoEncoder : public GMPVideoEncoder {
 public:
  explicit FakeVideoEncoder(GMPVideoHost* hostAPI) : host_(hostAPI) {}

  void InitEncode(const GMPVideoCodec& codecSettings,
                  const uint8_t* aCodecSpecific, uint32_t aCodecSpecificSize,
                  GMPVideoEncoderCallback* callback, int32_t numberOfCores,
                  uint32_t maxPayloadSize) override {
    callback_ = callback;
    constexpr uint16_t kFrameDropCadence = 5;
    frame_drop_cadence_ =
        codecSettings.mFrameDroppingOn ? kFrameDropCadence : 0;
    num_frames_since_drop_ = 0;
    frame_size_ = (maxPayloadSize > 0 && maxPayloadSize < BIG_FRAME)
                      ? maxPayloadSize
                      : BIG_FRAME;
    frame_size_ -= 24 + 40;
    // default header+extension size is 24, but let's leave extra room if
    // we enable more extensions.
    // XXX -- why isn't the size passed in based on the size minus extensions?

    const char* env = getenv("GMP_LOGGING");
    if (env) {
      g_log_level = atoi(env);
    }
    GMPLOG(GL_INFO, "Initialized encoder");
  }

  void SendFrame(GMPVideoi420Frame* inputImage, GMPVideoFrameType frame_type,
                 int nal_type) {
    if (frame_drop_cadence_ > 0) {
      const auto frame_drop_id = ++num_frames_since_drop_;
      num_frames_since_drop_ %= frame_drop_cadence_;
      GMPLOG(GL_DEBUG,
             "Frame dropping is on, id="
                 << frame_drop_id << "/" << frame_drop_cadence_ << ". "
                 << (frame_drop_id == frame_drop_cadence_ ? "Dropping"
                                                          : "Keeping"));
      if (frame_drop_id == frame_drop_cadence_) {
        return;
      }
    }
    // Encode this in a frame that looks a little bit like H.264.
    // Send SPS/PPS/IDR to avoid confusing people
    // Copy the data. This really should convert this to network byte order.
    EncodedFrame eframe;

    // These values were chosen to force a SPS id of 0
    eframe.sps_nalu = {sizeof(EncodedFrame::SPSNalu) - sizeof(uint32_t),
                       {0x67, 0x42, 0xc0, 0xd, 0x8c, 0x8d, 0x40, 0xa0, 0xf9,
                        0x0, 0xf0, 0x88, 0x46, 0xa0}};

    // These values were chosen to force a PPS id of 0
    eframe.pps_nalu = {sizeof(EncodedFrame::PPSNalu) - sizeof(uint32_t),
                       {0x68, 0xce, 0x3c, 0x80}};

    eframe.idr_nalu.size_ = sizeof(EncodedFrame::IDRNalu) - sizeof(uint32_t);
    // We force IFrame here - if we send PFrames, the webrtc.org code gets
    // tripped up attempting to find non-existent previous IFrames.
    eframe.idr_nalu.h264_compat_ =
        nal_type;  // 5 = IFrame/IDR slice, 1=PFrame/slice
    eframe.idr_nalu.magic_ = ENCODED_FRAME_MAGIC;
    eframe.idr_nalu.width_ = inputImage->Width();
    eframe.idr_nalu.height_ = inputImage->Height();
    eframe.idr_nalu.y_ = AveragePlane(inputImage->Buffer(kGMPYPlane),
                                      inputImage->AllocatedSize(kGMPYPlane));
    eframe.idr_nalu.u_ = AveragePlane(inputImage->Buffer(kGMPUPlane),
                                      inputImage->AllocatedSize(kGMPUPlane));
    eframe.idr_nalu.v_ = AveragePlane(inputImage->Buffer(kGMPVPlane),
                                      inputImage->AllocatedSize(kGMPVPlane));

    eframe.idr_nalu.timestamp_ = inputImage->Timestamp();

    // Now return the encoded data back to the parent.
    GMPVideoFrame* ftmp;
    GMPErr err = host_->CreateFrame(kGMPEncodedVideoFrame, &ftmp);
    if (err != GMPNoErr) {
      GMPLOG(GL_ERROR, "Error creating encoded frame");
      return;
    }

    GMPVideoEncodedFrame* f = static_cast<GMPVideoEncodedFrame*>(ftmp);

    err = f->CreateEmptyFrame(sizeof(eframe));
    if (err != GMPNoErr) {
      GMPLOG(GL_ERROR, "Error allocating frame data");
      f->Destroy();
      return;
    }
    memcpy(f->Buffer(), &eframe, sizeof(eframe));
    f->SetEncodedWidth(eframe.idr_nalu.width_);
    f->SetEncodedHeight(eframe.idr_nalu.height_);
    f->SetTimeStamp(eframe.idr_nalu.timestamp_);
    f->SetFrameType(frame_type);
    f->SetCompleteFrame(true);
    f->SetBufferType(GMP_BufferLength32);

    GMPLOG(GL_DEBUG, "Encoding complete. type="
                         << f->FrameType()
                         << " NAL_type=" << (int)eframe.idr_nalu.h264_compat_
                         << " length=" << f->Size()
                         << " timestamp=" << f->TimeStamp()
                         << " width/height=" << eframe.idr_nalu.width_ << "x"
                         << eframe.idr_nalu.height_);

    // Return the encoded frame.
    GMPCodecSpecificInfo info;
    mozilla::PodZero(&info);
    info.mCodecType = kGMPVideoCodecH264;
    info.mBufferType = GMP_BufferLength32;
    info.mCodecSpecific.mH264.mSimulcastIdx = 0;
    GMPLOG(GL_DEBUG, "Calling callback");
    callback_->Encoded(f, reinterpret_cast<uint8_t*>(&info), sizeof(info));
    GMPLOG(GL_DEBUG, "Callback called");
  }

  void Encode(GMPVideoi420Frame* inputImage, const uint8_t* aCodecSpecificInfo,
              uint32_t aCodecSpecificInfoLength,
              const GMPVideoFrameType* aFrameTypes,
              uint32_t aFrameTypesLength) override {
    GMPLOG(GL_DEBUG, __FUNCTION__ << " size=" << inputImage->Width() << "x"
                                  << inputImage->Height());

    assert(aFrameTypesLength != 0);
    GMPVideoFrameType frame_type = aFrameTypes[0];

    SelfDestruct<GMPVideoi420Frame> ifd(inputImage);

    if (frame_type == kGMPKeyFrame) {
      if (!inputImage) return;
    }
    if (!inputImage) {
      GMPLOG(GL_ERROR, "no input image");
      return;
    }

    if (frame_type == kGMPKeyFrame ||
        frames_encoded_++ % 10 == 0) {  // periodically send iframes anyways
      // 5 = IFrame/IDR slice
      SendFrame(inputImage, kGMPKeyFrame, 5);
    } else {
      // 1 = PFrame/slice
      SendFrame(inputImage, frame_type, 1);
    }
  }

  void SetChannelParameters(uint32_t aPacketLoss, uint32_t aRTT) override {}

  void SetRates(uint32_t aNewBitRate, uint32_t aFrameRate) override {}

  void SetPeriodicKeyFrames(bool aEnable) override {}

  void EncodingComplete() override { delete this; }

 private:
  uint8_t AveragePlane(uint8_t* ptr, size_t len) {
    uint64_t val = 0;

    for (size_t i = 0; i < len; ++i) {
      val += ptr[i];
    }

    return (val / len) % 0xff;
  }

  GMPVideoHost* host_;
  GMPVideoEncoderCallback* callback_ = nullptr;
  uint16_t frame_drop_cadence_ = 0;
  uint16_t num_frames_since_drop_ = 0;
  uint32_t frame_size_ = BIG_FRAME;
  uint32_t frames_encoded_ = 0;
};

class FakeVideoDecoder : public GMPVideoDecoder {
 public:
  explicit FakeVideoDecoder(GMPVideoHost* hostAPI)
      : host_(hostAPI), callback_(nullptr) {}

  ~FakeVideoDecoder() override = default;

  void InitDecode(const GMPVideoCodec& codecSettings,
                  const uint8_t* aCodecSpecific, uint32_t aCodecSpecificSize,
                  GMPVideoDecoderCallback* callback,
                  int32_t coreCount) override {
    GMPLOG(GL_INFO, "InitDecode");

    const char* env = getenv("GMP_LOGGING");
    if (env) {
      g_log_level = atoi(env);
    }
    callback_ = callback;
  }

  void Decode(GMPVideoEncodedFrame* inputFrame, bool missingFrames,
              const uint8_t* aCodecSpecificInfo,
              uint32_t aCodecSpecificInfoLength,
              int64_t renderTimeMs = -1) override {
    GMPLOG(GL_DEBUG, __FUNCTION__
                         << "Decoding frame size=" << inputFrame->Size()
                         << " timestamp=" << inputFrame->TimeStamp());

    // Attach a self-destructor so that the input frame is destroyed on return.
    SelfDestruct<GMPVideoEncodedFrame> ifd(inputFrame);

    EncodedFrame* eframe;
    eframe = reinterpret_cast<EncodedFrame*>(inputFrame->Buffer());
    GMPLOG(GL_DEBUG, "magic=" << eframe->idr_nalu.magic_ << " h264_compat="
                              << (int)eframe->idr_nalu.h264_compat_
                              << " width=" << eframe->idr_nalu.width_
                              << " height=" << eframe->idr_nalu.height_
                              << " timestamp=" << inputFrame->TimeStamp()
                              << " y/u/v=" << (int)eframe->idr_nalu.y_ << ":"
                              << (int)eframe->idr_nalu.u_ << ":"
                              << (int)eframe->idr_nalu.v_);
    if (inputFrame->Size() != (sizeof(*eframe))) {
      GMPLOG(GL_ERROR, "Couldn't decode frame. Size=" << inputFrame->Size());
      return;
    }

    if (eframe->idr_nalu.magic_ != ENCODED_FRAME_MAGIC) {
      GMPLOG(GL_ERROR,
             "Couldn't decode frame. Magic=" << eframe->idr_nalu.magic_);
      return;
    }
    if (eframe->idr_nalu.h264_compat_ != 5 &&
        eframe->idr_nalu.h264_compat_ != 1) {
      // only return video for iframes or pframes
      GMPLOG(GL_DEBUG, "Not a video frame: NAL type "
                           << (int)eframe->idr_nalu.h264_compat_);
      return;
    }

    int width = eframe->idr_nalu.width_;
    int height = eframe->idr_nalu.height_;
    int ystride = eframe->idr_nalu.width_;
    // Round up so the data fits, or CreateEmptyFrame will fail on odd width and
    // height.
    int uvstride = std::midpoint(ystride, 0);

    GMPLOG(GL_DEBUG, "Video frame ready for display "
                         << width << "x" << height
                         << " timestamp=" << inputFrame->TimeStamp());

    GMPVideoFrame* ftmp = nullptr;

    // Translate the image.
    GMPErr err = host_->CreateFrame(kGMPI420VideoFrame, &ftmp);
    if (err != GMPNoErr) {
      GMPLOG(GL_ERROR, "Couldn't allocate empty I420 frame");
      return;
    }

    GMPVideoi420Frame* frame = static_cast<GMPVideoi420Frame*>(ftmp);
    err = frame->CreateEmptyFrame(width, height, ystride, uvstride, uvstride);
    if (err != GMPNoErr) {
      GMPLOG(GL_ERROR, "Couldn't make decoded frame");
      return;
    }

    memset(frame->Buffer(kGMPYPlane), eframe->idr_nalu.y_,
           frame->AllocatedSize(kGMPYPlane));
    memset(frame->Buffer(kGMPUPlane), eframe->idr_nalu.u_,
           frame->AllocatedSize(kGMPUPlane));
    memset(frame->Buffer(kGMPVPlane), eframe->idr_nalu.v_,
           frame->AllocatedSize(kGMPVPlane));

    GMPLOG(GL_DEBUG, "Allocated size = " << frame->AllocatedSize(kGMPYPlane));
    frame->SetTimestamp(inputFrame->TimeStamp());
    frame->SetDuration(inputFrame->Duration());
    callback_->Decoded(frame);
  }

  void Reset() override { callback_->ResetComplete(); }

  void Drain() override { callback_->DrainComplete(); }

  void DecodingComplete() override { delete this; }

  GMPVideoHost* host_;
  GMPVideoDecoderCallback* callback_;
};

extern "C" {

PUBLIC_FUNC GMPErr GMPInit(const GMPPlatformAPI* aPlatformAPI) {
  return GMPNoErr;
}

PUBLIC_FUNC GMPErr GMPGetAPI(const char* aApiName, void* aHostAPI,
                             void** aPluginApi) {
  if (!strcmp(aApiName, GMP_API_VIDEO_DECODER)) {
    *aPluginApi = new FakeVideoDecoder(static_cast<GMPVideoHost*>(aHostAPI));
    return GMPNoErr;
  }
  if (!strcmp(aApiName, GMP_API_VIDEO_ENCODER)) {
    *aPluginApi = new FakeVideoEncoder(static_cast<GMPVideoHost*>(aHostAPI));
    return GMPNoErr;
  }
  return GMPGenericErr;
}

PUBLIC_FUNC void GMPShutdown(void) {}

}  // extern "C"
