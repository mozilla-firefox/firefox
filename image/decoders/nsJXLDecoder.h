/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_nsJXLDecoder_h
#define mozilla_image_decoders_nsJXLDecoder_h

#include "Decoder.h"
#include "SurfacePipe.h"
#include "mozilla/Vector.h"
#include "mozilla/image/jxl_decoder_ffi.h"

namespace mozilla::image {

struct JxlDecoderDeleter {
  void operator()(JxlApiDecoder* ptr) { jxl_decoder_destroy(ptr); }
};

class nsJXLDecoder final : public Decoder {
 public:
  ~nsJXLDecoder() override;

  DecoderType GetType() const override { return DecoderType::JXL; }

#ifdef DEBUG
  // Debug-only (for tests): number of times WritePixelRowsToPipe has run on
  // this decoder. Counts both partial flushes and the final frame write.
  uint32_t GetWritePixelRowsCount() const { return mWritePixelRowsCount; }
#endif

 protected:
  nsresult InitInternal() override;
  LexerResult DoDecode(SourceBufferIterator& aIterator,
                       IResumable* aOnResume) override;
  Maybe<glean::impl::MemoryDistributionMetric> SpeedMetric() const override;

 private:
  friend class DecoderFactory;

  explicit nsJXLDecoder(RasterImage* aImage);

  enum class DecoderState { Initial, HaveBasicInfo };

  // jxl-rs output pixel format. Determines buffer stride, the conversion path
  // in WritePixelRowsToPipe, and the SurfacePipe input format.
  enum class PixelFormat {
    Rgba8,       // u8 R,G,B,A (A=255 when no alpha channel)
    Gray8,       // u8 G
    GrayAlpha8,  // u8 G,A
    Cmyk8,       // u8 C,M,Y,_ (4th byte unused); u8 K in mKBuffer
    Rgba16f,     // f16 R,G,B,A, native-endian; HDR images when CMS was
                 // requested (mTransform may still be null if transform
                 // creation failed)
  };

  enum class FrameOutputResult {
    BufferAllocated,
    FrameAdvanced,
    DecodeComplete,
    NoOutput,
    Error
  };

  enum class ProcessResult { NeedMoreData, YieldOutput, Complete, Error };

  // Failure reason recorded to jxl.decode_result. Defaults to DecodeError (the
  // opaque jxl-rs failure) and is overwritten at more specific failure sites.
  enum class DecodeResult : uint8_t {
    DecodeError,
    SizeOverflow,
    OutOfMemory,
    PipeInitError,
    InvalidFrameDuration,
    WriteError,
    NoBasicInfo,
  };

  // Runs the actual decode; DoDecode wraps this to record decode telemetry
  // once the decode reaches a terminal state.
  LexerResult DoDecodeInternal(SourceBufferIterator& aIterator,
                               IResumable* aOnResume);
  void RecordDecodeTelemetry(TerminalState aState);

  JxlDecoderStatus ProcessInput(const uint8_t** aData, size_t* aLength);
  FrameOutputResult HandleFrameOutput();
  /// @aData and @aLength are in/out: on return they point to and describe the
  /// unconsumed remainder of the input.
  ProcessResult ProcessAvailableData(const uint8_t** aData, size_t* aLength);

  LexerResult ScanForFrameCount(SourceBufferIterator& aIterator,
                                IResumable* aOnResume);

  static PixelFormat DetectPixelFormat(JxlApiDecoder* aDecoder,
                                       const JxlBasicInfo& aBasicInfo);
  // Allocate the pixel / K / scratch buffers, detect the output pixel
  // format, and perform any CMS setup needed for this image. The SurfacePipe
  // (and the surface it exposes) is deferred to EnsureSurfacePipe until we
  // actually have pixels to output.
  nsresult AllocateFrameBuffers();
  // Create mCurrentPipe if it doesn't already exist. Idempotent.
  // Preconditions: AllocateFrameBuffers has already run (mPixelFormat /
  // mTransform are read here); for animated images the caller must also
  // have observed frame_ready so that AnimationParams is available.
  nsresult EnsureSurfacePipe();
  // Create the qcms input profile from the output color space's CICP code
  // points, or null if it isn't a CICP-representable RGB space (or is PQ/HLG,
  // which we keep on the ICC path for its baked-in tone mapping).
  qcms_profile* MaybeCreateInputProfileFromCICP();
  // Create the qcms input profile from the ICC profile jxl-rs provides, or null
  // if none is available or it fails to parse.
  qcms_profile* MaybeCreateInputProfileFromICC();
  void BuildCMSTransform();
  nsresult FinishFrame();
  void FlushPartialFrame();
  bool WritePixelRowsToPipe();

  LexerResult DrainFrames();

  size_t BytesPerPixel() const {
    switch (mPixelFormat.value()) {
      case PixelFormat::Rgba8:
        return 4;
      case PixelFormat::Gray8:
        return 1;
      case PixelFormat::GrayAlpha8:
        return 2;
      case PixelFormat::Cmyk8:
        return 4;
      case PixelFormat::Rgba16f:
        return 8;
    }
    MOZ_ASSERT_UNREACHABLE("unhandled PixelFormat");
    return 4;
  }

  std::unique_ptr<JxlApiDecoder, JxlDecoderDeleter> mDecoder;
  std::unique_ptr<JxlApiDecoder, JxlDecoderDeleter> mScanner;

  DecoderState mDecoderState = DecoderState::Initial;

  uint32_t mFrameIndex = 0;

  // Field wrapper that asserts on read before first write and asserts on any
  // write after the first.
  template <typename T>
  class WriteOnce {
   public:
    T value() const {
      MOZ_ASSERT(mIsSet);
      return mValue;
    }
    void set(T aVal) {
      MOZ_ASSERT(!mIsSet);
      mIsSet = true;
      mValue = aVal;
    }

   private:
    bool mIsSet = false;
    T mValue{};
  };

  WriteOnce<PixelFormat> mPixelFormat;

  // The row written to the SurfacePipe when the pipe can't read mPixelBuffer
  // directly: mPixelFormat converted to 4 bytes per pixel (RGBA8, or RGB8 plus
  // padding for CMYK with CMS). Allocated for every format except Rgba8.
  Vector<uint8_t> mPipeInputRowBuf;

  // Full-frame decoded pixel buffer; allocated in AllocateFrameBuffers, sized
  // width * height * BytesPerPixel(). Passed to jxl-rs as the output buffer.
  Vector<uint8_t> mPixelBuffer;
  Vector<uint8_t> mKBuffer;  // K (Black) channel, 1 byte/pixel, for CMYK images
  Maybe<SurfacePipe> mCurrentPipe;

  bool mIteratorComplete : 1 = false;

  // Used together to pick success / partial_frame / no_frame for
  // jxl.decode_result on a successful terminal state.
  // Whether a frame was fully decoded.
  bool mFrameCompleted : 1 = false;
  // True once at least a partial frame has been rendered; the rendered pixels
  // could amount to a complete frame too.
  bool mPartialFrameRendered : 1 = false;

  DecodeResult mDecodeResult = DecodeResult::DecodeError;

#ifdef DEBUG
  uint32_t mWritePixelRowsCount = 0;
#endif
};

}  // namespace mozilla::image

#endif  // mozilla_image_decoders_nsJXLDecoder_h
