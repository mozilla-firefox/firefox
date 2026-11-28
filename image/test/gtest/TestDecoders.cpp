/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "AnimationSurfaceProvider.h"
#include "Common.h"
#include "DecodePool.h"
#include "Decoder.h"
#include "DecoderFactory.h"
#include "decoders/nsBMPDecoder.h"
#include "gtest/gtest.h"
#include "mozilla/Monitor.h"
#ifdef MOZ_JXL
#  include "decoders/nsJXLDecoder.h"
#endif
#include "IDecodingTask.h"
#include "ImageFactory.h"
#include "ImageOps.h"
#include "ProgressTracker.h"
#include "SourceBuffer.h"
#include "imgIContainer.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/gfx/2D.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIInputStream.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

static already_AddRefed<SourceSurface> CheckDecoderState(
    const ImageTestCase& aTestCase, image::Decoder* aDecoder) {
  // image::Decoder should match what we asked for in the MIME type.
  EXPECT_NE(aDecoder->GetType(), DecoderType::UNKNOWN);
  EXPECT_EQ(aDecoder->GetType(),
            DecoderFactory::GetDecoderType(aTestCase.mMimeType));

  EXPECT_TRUE(aDecoder->GetDecodeDone());
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_HAS_ERROR), aDecoder->HasError());

  // Verify that the decoder made the expected progress.
  Progress progress = aDecoder->TakeProgress();
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_HAS_ERROR),
            bool(progress & FLAG_HAS_ERROR));

  if (aTestCase.mFlags & TEST_CASE_HAS_ERROR) {
    return nullptr;  // That's all we can check for bad images.
  }

  EXPECT_TRUE(bool(progress & FLAG_SIZE_AVAILABLE));
  EXPECT_TRUE(bool(progress & FLAG_DECODE_COMPLETE));
  EXPECT_TRUE(bool(progress & FLAG_FRAME_COMPLETE));
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_TRANSPARENT),
            bool(progress & FLAG_HAS_TRANSPARENCY));
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_ANIMATED),
            bool(progress & FLAG_IS_ANIMATED));

  // The decoder should get the correct size.
  OrientedIntSize size = aDecoder->Size();
  EXPECT_EQ(aTestCase.mSize.width, size.width);
  EXPECT_EQ(aTestCase.mSize.height, size.height);

  // Get the current frame, which is always the first frame of the image
  // because CreateAnonymousDecoder() forces a first-frame-only decode.
  RawAccessFrameRef currentFrame = aDecoder->GetCurrentFrameRef();
  RefPtr<SourceSurface> surface = currentFrame->GetSourceSurface();

  // Verify that the resulting surfaces matches our expectations.
  EXPECT_TRUE(surface->IsDataSourceSurface());
  EXPECT_TRUE(surface->GetFormat() == SurfaceFormat::OS_RGBX ||
              surface->GetFormat() == SurfaceFormat::OS_RGBA);
  EXPECT_EQ(aTestCase.mOutputSize, surface->GetSize());

  return surface.forget();
}

static void CheckDecoderResults(const ImageTestCase& aTestCase,
                                image::Decoder* aDecoder) {
  RefPtr<SourceSurface> surface = CheckDecoderState(aTestCase, aDecoder);
  if (!surface) {
    return;
  }

  if (aTestCase.mFlags & TEST_CASE_IGNORE_OUTPUT) {
    return;
  }

  // Check the output.
  EXPECT_TRUE(IsSolidColor(surface, aTestCase.Color(), aTestCase.Fuzz()));
}

template <typename Func>
void WithBadBufferDecode(const ImageTestCase& aTestCase,
                         const Maybe<IntSize>& aOutputSize,
                         Func aResultChecker) {
  // Prepare a SourceBuffer with an error that will immediately move iterators
  // to COMPLETE.
  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
  sourceBuffer->ExpectLength(SIZE_MAX);

  // Create a decoder.
  DecoderType decoderType = DecoderFactory::GetDecoderType(aTestCase.mMimeType);
  RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      decoderType, sourceBuffer, aOutputSize, DecoderFlags::FIRST_FRAME_ONLY,
      aTestCase.mSurfaceFlags);
  ASSERT_TRUE(decoder != nullptr);
  auto task = MakeRefPtr<AnonymousDecodingTask>(WrapNotNull(decoder),
                                                /* aResumable */ false);

  // Run the full decoder synchronously on the main thread.
  task->Run();

  // Call the lambda to verify the expected results.
  aResultChecker(decoder);
}

static void CheckDecoderBadBuffer(const ImageTestCase& aTestCase) {
  WithBadBufferDecode(aTestCase, Nothing(), [&](image::Decoder* aDecoder) {
    CheckDecoderResults(aTestCase, aDecoder);
  });
}

/**
 * AnonymousDecodingTask but with a monitor so we can wait for it to finish
 * safely.
 */
class MonitorAnonymousDecodingTask final : public AnonymousDecodingTask {
 public:
  explicit MonitorAnonymousDecodingTask(NotNull<Decoder*> aDecoder,
                                        bool aResumable);

  void Run() override;

  void WaitUntilFinished();

 private:
  virtual ~MonitorAnonymousDecodingTask() = default;

  Monitor mMonitor MOZ_UNANNOTATED;
};

MonitorAnonymousDecodingTask::MonitorAnonymousDecodingTask(
    NotNull<Decoder*> aDecoder, bool aResumable)
    : AnonymousDecodingTask(aDecoder, aResumable),
      mMonitor("MonitorAnonymousDecodingTask") {}

void MonitorAnonymousDecodingTask::Run() {
  MonitorAutoLock lock(mMonitor);

  while (true) {
    LexerResult result = mDecoder->Decode(WrapNotNull(this));

    if (result.is<TerminalState>()) {
      mMonitor.NotifyAll();
      return;  // We're done.
    }

    if (result == LexerResult(Yield::NEED_MORE_DATA)) {
      // We can't make any more progress right now. Let the caller decide how to
      // handle it.
      mMonitor.NotifyAll();
      return;
    }

    // Right now we don't do anything special for other kinds of yields, so just
    // keep working.
    MOZ_ASSERT(result.is<Yield>());
  }
}

void MonitorAnonymousDecodingTask::WaitUntilFinished() {
  MonitorAutoLock lock(mMonitor);

  while (true) {
    if (mDecoder->GetDecodeDone()) {
      return;
    }

    // Not done yet, so we'll have to wait.
    mMonitor.Wait();
  }
}

template <typename Func>
void WithSingleChunkDecode(const ImageTestCase& aTestCase,
                           const Maybe<IntSize>& aOutputSize,
                           bool aUseDecodePool, Func aResultChecker) {
  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream != nullptr);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Write the data into a SourceBuffer.
  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
  sourceBuffer->ExpectLength(length);
  rv = sourceBuffer->AppendFromInputStream(inputStream, length);
  ASSERT_NS_SUCCEEDED(rv);
  sourceBuffer->Complete(NS_OK);

  // Create a decoder.
  DecoderType decoderType = DecoderFactory::GetDecoderType(aTestCase.mMimeType);
  DecoderFlags decoderFlags =
      DefaultDecoderFlags() | DecoderFlags::FIRST_FRAME_ONLY;
  RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      decoderType, sourceBuffer, aOutputSize, decoderFlags,
      aTestCase.mSurfaceFlags);
  ASSERT_TRUE(decoder != nullptr);
  auto task = MakeRefPtr<MonitorAnonymousDecodingTask>(WrapNotNull(decoder),
                                                       /* aResumable */ false);

  if (aUseDecodePool) {
    DecodePool::Singleton()->AsyncRun(task.get());

    task->WaitUntilFinished();
  } else {  // Run the full decoder synchronously on the main thread.
    task->Run();
  }

  // Call the lambda to verify the expected results.
  aResultChecker(decoder);
}

static void CheckDecoderSingleChunk(const ImageTestCase& aTestCase,
                                    bool aUseDecodePool = false) {
  WithSingleChunkDecode(aTestCase, Nothing(), aUseDecodePool,
                        [&](image::Decoder* aDecoder) {
                          CheckDecoderResults(aTestCase, aDecoder);
                        });
}

template <typename Func>
void WithDelayedChunkDecode(const ImageTestCase& aTestCase,
                            const Maybe<IntSize>& aOutputSize,
                            Func aResultChecker) {
  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream != nullptr);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Prepare an empty SourceBuffer.
  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();

  // Create a decoder.
  DecoderType decoderType = DecoderFactory::GetDecoderType(aTestCase.mMimeType);
  RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      decoderType, sourceBuffer, aOutputSize, DecoderFlags::FIRST_FRAME_ONLY,
      aTestCase.mSurfaceFlags);
  ASSERT_TRUE(decoder != nullptr);
  auto task = MakeRefPtr<AnonymousDecodingTask>(WrapNotNull(decoder),
                                                /* aResumable */ true);

  // Run the full decoder synchronously. It should now be waiting on
  // the iterator to yield some data since we haven't written anything yet.
  task->Run();

  // Writing all of the data should wake up the decoder to complete.
  sourceBuffer->ExpectLength(length);
  rv = sourceBuffer->AppendFromInputStream(inputStream, length);
  ASSERT_NS_SUCCEEDED(rv);
  sourceBuffer->Complete(NS_OK);

  // It would have gotten posted to the main thread to avoid mutex contention.
  SpinPendingEvents();

  // Call the lambda to verify the expected results.
  aResultChecker(decoder);
}

static void CheckDecoderDelayedChunk(const ImageTestCase& aTestCase) {
  WithDelayedChunkDecode(aTestCase, Nothing(), [&](image::Decoder* aDecoder) {
    CheckDecoderResults(aTestCase, aDecoder);
  });
}

template <typename Func>
static void WithMultiChunkDecode(const ImageTestCase& aTestCase,
                                 uint64_t aChunkSize, Func aResultChecker) {
  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream != nullptr);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Create a SourceBuffer and a decoder.
  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
  sourceBuffer->ExpectLength(length);
  DecoderType decoderType = DecoderFactory::GetDecoderType(aTestCase.mMimeType);
  DecoderFlags decoderFlags =
      DefaultDecoderFlags() | DecoderFlags::FIRST_FRAME_ONLY;
  RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      decoderType, sourceBuffer, Nothing(), decoderFlags,
      aTestCase.mSurfaceFlags);
  ASSERT_TRUE(decoder != nullptr);
  auto task = MakeRefPtr<AnonymousDecodingTask>(WrapNotNull(decoder),
                                                /* aResumable */ true);

  // Run the full decoder synchronously. It should now be waiting on
  // the iterator to yield some data since we haven't written anything yet.
  task->Run();

  while (length > 0) {
    uint64_t read = length > aChunkSize ? aChunkSize : length;
    length -= read;

    uint64_t available = 0;
    rv = inputStream->Available(&available);
    ASSERT_TRUE(available >= read);
    ASSERT_NS_SUCCEEDED(rv);

    // Writing any data should wake up the decoder to complete.
    rv = sourceBuffer->AppendFromInputStream(inputStream, read);
    ASSERT_NS_SUCCEEDED(rv);

    // It would have gotten posted to the main thread to avoid mutex contention.
    SpinPendingEvents();
  }

  sourceBuffer->Complete(NS_OK);
  SpinPendingEvents();

  aResultChecker(decoder);
}

static void CheckDecoderMultiChunk(const ImageTestCase& aTestCase,
                                   uint64_t aChunkSize = 1) {
  WithMultiChunkDecode(aTestCase, aChunkSize, [&](image::Decoder* aDecoder) {
    CheckDecoderResults(aTestCase, aDecoder);
  });
}

static uint32_t HashDecoderOutput(image::Decoder* aDecoder) {
  RawAccessFrameRef frame = aDecoder->GetCurrentFrameRef();
  if (!frame) {
    return 0;
  }
  RefPtr<SourceSurface> surface = frame->GetSourceSurface();
  if (!surface) {
    return 0;
  }
  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    return 0;
  }
  DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(DataSourceSurface::READ, &map)) {
    return 0;
  }
  IntSize size = dataSurface->GetSize();
  uint32_t hash = 5381;
  for (int32_t y = 0; y < size.height; y++) {
    // Use stride to calculate start of row location, but only include pixels
    // up to the width in the hash in case there are unused pixels at the end
    // of the row due to alignment.
    const uint8_t* row = map.mData + y * map.mStride;
    for (int32_t x = 0; x < size.width * 4; x++) {
      hash = hash * 33 ^ row[x];
    }
  }
  dataSurface->Unmap();
  return hash;
}

static void CheckIncrementalDecodeMatchesOneShot(
    const ImageTestCase& aTestCase) {
  uint32_t singleChunkHash = 0;
  WithSingleChunkDecode(aTestCase, Nothing(), false,
                        [&](image::Decoder* aDecoder) {
                          ASSERT_FALSE(aDecoder->HasError());
                          singleChunkHash = HashDecoderOutput(aDecoder);
                          ASSERT_NE(0u, singleChunkHash);
                        });

  uint32_t multiChunkHash = 0;
  WithMultiChunkDecode(aTestCase, 1, [&](image::Decoder* aDecoder) {
    ASSERT_FALSE(aDecoder->HasError());
    multiChunkHash = HashDecoderOutput(aDecoder);
  });

  EXPECT_EQ(singleChunkHash, multiChunkHash);
}

static void CheckDownscaleDuringDecode(const ImageTestCase& aTestCase) {
  // This function expects that |aTestCase| consists of 25 lines of green,
  // followed by 25 lines of red, followed by 25 lines of green, followed by 25
  // more lines of red. We'll downscale it from 100x100 to 20x20.
  IntSize outputSize(20, 20);

  WithSingleChunkDecode(
      aTestCase, Some(outputSize), /* aUseDecodePool */ false,
      [&](image::Decoder* aDecoder) {
        RefPtr<SourceSurface> surface = CheckDecoderState(aTestCase, aDecoder);

        // There are no downscale-during-decode tests that have
        // TEST_CASE_HAS_ERROR set, so we expect to always get a surface here.
        EXPECT_TRUE(surface != nullptr);

        if (aTestCase.mFlags & TEST_CASE_IGNORE_OUTPUT) {
          return;
        }

        // Check that the downscaled image is correct. Note that we skip rows
        // near the transitions between colors, since the downscaler does not
        // produce a sharp boundary at these points. Even some of the rows we
        // test need a small amount of fuzz; this is just the nature of Lanczos
        // downscaling.
        EXPECT_TRUE(RowsAreSolidColor(surface, 0, 4,
                                      aTestCase.ChooseColor(BGRAColor::Green()),
                                      /* aFuzz = */ 47));
        EXPECT_TRUE(RowsAreSolidColor(surface, 6, 3,
                                      aTestCase.ChooseColor(BGRAColor::Red()),
                                      /* aFuzz = */ 27));
        EXPECT_TRUE(RowsAreSolidColor(surface, 11, 3, BGRAColor::Green(),
                                      /* aFuzz = */ 47));
        EXPECT_TRUE(RowsAreSolidColor(surface, 16, 4,
                                      aTestCase.ChooseColor(BGRAColor::Red()),
                                      /* aFuzz = */ 27));
      });
}

static void CheckExifOrientationDownscale(const ImageTestCase& aTestCase) {
  // An EXIF-rotated JPEG decoded to a non-aspect-preserving output size with
  // libjpeg-turbo IDCT downscaling must still decode. The scale factor has to
  // be computed in the same unoriented space as the downscaling filter, or the
  // decode hard-fails.
  bool oldValue = false;
  Preferences::GetBool("image.jpeg.dct-scaling.enabled", &oldValue);
  Preferences::SetBool("image.jpeg.dct-scaling.enabled", true);
  auto resetPref = MakeScopeExit([&] {
    Preferences::SetBool("image.jpeg.dct-scaling.enabled", oldValue);
  });

  WithSingleChunkDecode(
      aTestCase, Some(aTestCase.mOutputSize), /* aUseDecodePool */ false,
      [&](image::Decoder* aDecoder) {
        // The decode must not fail: prior to the fix the surface pipe was
        // rejected here and the decoder ended in an error state.
        ASSERT_FALSE(aDecoder->HasError());
        EXPECT_TRUE(aDecoder->GetDecodeDone());

        RawAccessFrameRef currentFrame = aDecoder->GetCurrentFrameRef();
        ASSERT_TRUE(bool(currentFrame));
        RefPtr<SourceSurface> surface = currentFrame->GetSourceSurface();
        ASSERT_TRUE(surface != nullptr);

        EXPECT_EQ(aTestCase.mOutputSize, surface->GetSize());
        EXPECT_TRUE(IsSolidColor(surface, aTestCase.Color(), aTestCase.Fuzz()));
      });
}

static void CheckAnimationDecoderResults(const ImageTestCase& aTestCase,
                                         AnimationSurfaceProvider* aProvider,
                                         image::Decoder* aDecoder) {
  EXPECT_TRUE(aDecoder->GetDecodeDone());
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_HAS_ERROR), aDecoder->HasError());

  if (aTestCase.mFlags & TEST_CASE_HAS_ERROR) {
    return;  // That's all we can check for bad images.
  }

  // The decoder should get the correct size.
  OrientedIntSize size = aDecoder->Size();
  EXPECT_EQ(aTestCase.mSize.width, size.width);
  EXPECT_EQ(aTestCase.mSize.height, size.height);

  if (aTestCase.mFlags & TEST_CASE_IGNORE_OUTPUT) {
    return;
  }

  // Check the output.
  AutoTArray<BGRAColor, 2> framePixels;
  framePixels.AppendElement(aTestCase.ChooseColor(BGRAColor::Green()));
  framePixels.AppendElement(
      aTestCase.ChooseColor(BGRAColor(0x7F, 0x7F, 0x7F, 0xFF)));

  DrawableSurface drawableSurface(WrapNotNull(aProvider));
  for (size_t i = 0; i < framePixels.Length(); ++i) {
    nsresult rv = drawableSurface.Seek(i);
    EXPECT_NS_SUCCEEDED(rv);

    // Check the first frame, all green.
    RawAccessFrameRef rawFrame = drawableSurface->RawAccessRef();
    RefPtr<SourceSurface> surface = rawFrame->GetSourceSurface();

    // Verify that the resulting surfaces matches our expectations.
    EXPECT_TRUE(surface->IsDataSourceSurface());
    EXPECT_TRUE(surface->GetFormat() == SurfaceFormat::OS_RGBX ||
                surface->GetFormat() == SurfaceFormat::OS_RGBA);
    EXPECT_EQ(aTestCase.mOutputSize, surface->GetSize());
    EXPECT_TRUE(IsSolidColor(surface, framePixels[i], aTestCase.Fuzz()));
  }

  // Should be no more frames.
  nsresult rv = drawableSurface.Seek(framePixels.Length());
  EXPECT_NS_FAILED(rv);
}

template <typename Func>
static void WithSingleChunkAnimationDecode(const ImageTestCase& aTestCase,
                                           Func aResultChecker) {
  // Create an image.
  RefPtr<Image> image = ImageFactory::CreateAnonymousImage(
      nsDependentCString(aTestCase.mMimeType));
  ASSERT_TRUE(!image->HasError());

  NotNull<RefPtr<RasterImage>> rasterImage =
      WrapNotNull(static_cast<RasterImage*>(image.get()));

  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream != nullptr);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Write the data into a SourceBuffer.
  NotNull<RefPtr<SourceBuffer>> sourceBuffer = WrapNotNull(new SourceBuffer());
  sourceBuffer->ExpectLength(length);
  rv = sourceBuffer->AppendFromInputStream(inputStream, length);
  ASSERT_NS_SUCCEEDED(rv);
  sourceBuffer->Complete(NS_OK);

  // Create a metadata decoder first, because otherwise RasterImage will get
  // unhappy about finding out the image is animated during a full decode.
  DecoderType decoderType = DecoderFactory::GetDecoderType(aTestCase.mMimeType);
  DecoderFlags decoderFlags = DefaultDecoderFlags();
  RefPtr<IDecodingTask> task = DecoderFactory::CreateMetadataDecoder(
      decoderType, rasterImage, decoderFlags, sourceBuffer);
  ASSERT_TRUE(task != nullptr);

  // Run the metadata decoder synchronously.
  task->Run();

  // Create a decoder.
  SurfaceFlags surfaceFlags = aTestCase.mSurfaceFlags;
  RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      decoderType, sourceBuffer, Nothing(), decoderFlags, surfaceFlags);
  ASSERT_TRUE(decoder != nullptr);

  // Create an AnimationSurfaceProvider which will manage the decoding process
  // and make this decoder's output available in the surface cache.
  SurfaceKey surfaceKey = RasterSurfaceKey(aTestCase.mOutputSize, surfaceFlags,
                                           PlaybackType::eAnimated);
  auto provider = MakeRefPtr<AnimationSurfaceProvider>(rasterImage, surfaceKey,
                                                       WrapNotNull(decoder),
                                                       /* aCurrentFrame */ 0);

  // Run the full decoder synchronously.
  provider->Run();

  // Call the lambda to verify the expected results.
  aResultChecker(provider, decoder);
}

static void CheckAnimationDecoderSingleChunk(const ImageTestCase& aTestCase) {
  WithSingleChunkAnimationDecode(
      aTestCase,
      [&](AnimationSurfaceProvider* aProvider, image::Decoder* aDecoder) {
        CheckAnimationDecoderResults(aTestCase, aProvider, aDecoder);
      });
}

static void CheckDecoderFrameFirst(const ImageTestCase& aTestCase) {
  // Verify that we can decode this test case and retrieve the first frame using
  // imgIContainer::FRAME_FIRST. This ensures that we correctly trigger a
  // single-frame decode rather than an animated decode when
  // imgIContainer::FRAME_FIRST is requested.

  // Create an image.
  RefPtr<Image> image = ImageFactory::CreateAnonymousImage(
      nsDependentCString(aTestCase.mMimeType));
  ASSERT_TRUE(!image->HasError());

  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Write the data into the image.
  rv = image->OnImageDataAvailable(nullptr, inputStream, 0,
                                   static_cast<uint32_t>(length));
  ASSERT_NS_SUCCEEDED(rv);

  // Let the image know we've sent all the data.
  rv = image->OnImageDataComplete(nullptr, NS_OK, true);
  ASSERT_NS_SUCCEEDED(rv);

  RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
  tracker->SyncNotifyProgress(FLAG_LOAD_COMPLETE);

  // Lock the image so its surfaces don't disappear during the test.
  image->LockImage();

  auto unlock = mozilla::MakeScopeExit([&] { image->UnlockImage(); });

  // Use GetFrame() to force a sync decode of the image, specifying FRAME_FIRST
  // to ensure that we don't get an animated decode.
  RefPtr<SourceSurface> surface = image->GetFrame(
      imgIContainer::FRAME_FIRST, imgIContainer::FLAG_SYNC_DECODE);

  // Ensure that the image's metadata meets our expectations.
  IntSize imageSize(0, 0);
  rv = image->GetWidth(&imageSize.width);
  EXPECT_NS_SUCCEEDED(rv);
  rv = image->GetHeight(&imageSize.height);
  EXPECT_NS_SUCCEEDED(rv);

  EXPECT_EQ(aTestCase.mSize.width, imageSize.width);
  EXPECT_EQ(aTestCase.mSize.height, imageSize.height);

  Progress imageProgress = tracker->GetProgress();

  EXPECT_TRUE(bool(imageProgress & FLAG_IS_ANIMATED) == true);

  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_TRANSPARENT),
            bool(imageProgress & FLAG_HAS_TRANSPARENCY));
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_ANIMATED),
            bool(imageProgress & FLAG_IS_ANIMATED));

  // Ensure that we decoded the static version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eStatic),
        /* aMarkUsed = */ false);
    ASSERT_EQ(MatchType::EXACT, result.Type());
    EXPECT_TRUE(bool(result.Surface()));
  }

  // Ensure that we didn't decode the animated version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eAnimated),
        /* aMarkUsed = */ false);
    ASSERT_EQ(MatchType::NOT_FOUND, result.Type());
  }

  // Use GetFrame() to force a sync decode of the image, this time specifying
  // FRAME_CURRENT to ensure that we get an animated decode.
  RefPtr<SourceSurface> animatedSurface = image->GetFrame(
      imgIContainer::FRAME_CURRENT, imgIContainer::FLAG_SYNC_DECODE);

  // Ensure that we decoded both frames of the animated version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eAnimated),
        /* aMarkUsed = */ true);
    ASSERT_EQ(MatchType::EXACT, result.Type());

    EXPECT_NS_SUCCEEDED(result.Surface().Seek(0));
    EXPECT_TRUE(bool(result.Surface()));

    RefPtr<imgFrame> partialFrame = result.Surface().GetFrame(1);
    EXPECT_TRUE(bool(partialFrame));
  }

  // Ensure that the static version is still around.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eStatic),
        /* aMarkUsed = */ true);
    ASSERT_EQ(MatchType::EXACT, result.Type());
    EXPECT_TRUE(bool(result.Surface()));
  }
}

static void CheckDecoderFrameCurrent(const ImageTestCase& aTestCase) {
  // Verify that we can decode this test case and retrieve the entire sequence
  // of frames using imgIContainer::FRAME_CURRENT. This ensures that we
  // correctly trigger an animated decode rather than a single-frame decode when
  // imgIContainer::FRAME_CURRENT is requested.

  // Create an image.
  RefPtr<Image> image = ImageFactory::CreateAnonymousImage(
      nsDependentCString(aTestCase.mMimeType));
  ASSERT_TRUE(!image->HasError());

  nsCOMPtr<nsIInputStream> inputStream = LoadFile(aTestCase.mPath);
  ASSERT_TRUE(inputStream);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Write the data into the image.
  rv = image->OnImageDataAvailable(nullptr, inputStream, 0,
                                   static_cast<uint32_t>(length));
  ASSERT_NS_SUCCEEDED(rv);

  // Let the image know we've sent all the data.
  rv = image->OnImageDataComplete(nullptr, NS_OK, true);
  ASSERT_NS_SUCCEEDED(rv);

  RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
  tracker->SyncNotifyProgress(FLAG_LOAD_COMPLETE);

  // Lock the image so its surfaces don't disappear during the test.
  image->LockImage();

  // Use GetFrame() to force a sync decode of the image, specifying
  // FRAME_CURRENT to ensure we get an animated decode.
  RefPtr<SourceSurface> surface = image->GetFrame(
      imgIContainer::FRAME_CURRENT, imgIContainer::FLAG_SYNC_DECODE);

  // Ensure that the image's metadata meets our expectations.
  IntSize imageSize(0, 0);
  rv = image->GetWidth(&imageSize.width);
  EXPECT_NS_SUCCEEDED(rv);
  rv = image->GetHeight(&imageSize.height);
  EXPECT_NS_SUCCEEDED(rv);

  EXPECT_EQ(aTestCase.mSize.width, imageSize.width);
  EXPECT_EQ(aTestCase.mSize.height, imageSize.height);

  Progress imageProgress = tracker->GetProgress();

  EXPECT_TRUE(bool(imageProgress & FLAG_IS_ANIMATED) == true);

  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_TRANSPARENT),
            bool(imageProgress & FLAG_HAS_TRANSPARENCY));
  EXPECT_EQ(bool(aTestCase.mFlags & TEST_CASE_IS_ANIMATED),
            bool(imageProgress & FLAG_IS_ANIMATED));

  // Ensure that we decoded both frames of the animated version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eAnimated),
        /* aMarkUsed = */ true);
    ASSERT_EQ(MatchType::EXACT, result.Type());

    EXPECT_NS_SUCCEEDED(result.Surface().Seek(0));
    EXPECT_TRUE(bool(result.Surface()));

    RefPtr<imgFrame> partialFrame = result.Surface().GetFrame(1);
    EXPECT_TRUE(bool(partialFrame));
  }

  // Ensure that we didn't decode the static version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eStatic),
        /* aMarkUsed = */ false);
    ASSERT_EQ(MatchType::NOT_FOUND, result.Type());
  }

  // Use GetFrame() to force a sync decode of the image, this time specifying
  // FRAME_FIRST to ensure that we get a single-frame decode.
  RefPtr<SourceSurface> animatedSurface = image->GetFrame(
      imgIContainer::FRAME_FIRST, imgIContainer::FLAG_SYNC_DECODE);

  // Ensure that we decoded the static version of the image.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eStatic),
        /* aMarkUsed = */ true);
    ASSERT_EQ(MatchType::EXACT, result.Type());
    EXPECT_TRUE(bool(result.Surface()));
  }

  // Ensure that both frames of the animated version are still around.
  {
    LookupResult result = SurfaceCache::Lookup(
        ImageKey(image.get()),
        RasterSurfaceKey(imageSize, aTestCase.mSurfaceFlags,
                         PlaybackType::eAnimated),
        /* aMarkUsed = */ true);
    ASSERT_EQ(MatchType::EXACT, result.Type());

    EXPECT_NS_SUCCEEDED(result.Surface().Seek(0));
    EXPECT_TRUE(bool(result.Surface()));

    RefPtr<imgFrame> partialFrame = result.Surface().GetFrame(1);
    EXPECT_TRUE(bool(partialFrame));
  }
}

class ImageDecoders : public ::testing::Test {
 protected:
  AutoInitializeImageLib mInit;
};

#define IMAGE_GTEST_DECODER_BASE_F(test_prefix)                              \
  TEST_F(ImageDecoders, test_prefix##SingleChunk) {                          \
    CheckDecoderSingleChunk(Green##test_prefix##TestCase());                 \
  }                                                                          \
                                                                             \
  TEST_F(ImageDecoders, test_prefix##DelayedChunk) {                         \
    CheckDecoderDelayedChunk(Green##test_prefix##TestCase());                \
  }                                                                          \
                                                                             \
  TEST_F(ImageDecoders, test_prefix##MultiChunk) {                           \
    CheckDecoderMultiChunk(Green##test_prefix##TestCase());                  \
  }                                                                          \
                                                                             \
  TEST_F(ImageDecoders, test_prefix##DownscaleDuringDecode) {                \
    CheckDownscaleDuringDecode(Downscaled##test_prefix##TestCase());         \
  }                                                                          \
                                                                             \
  TEST_F(ImageDecoders, test_prefix##ForceSRGB) {                            \
    CheckDecoderSingleChunk(Green##test_prefix##TestCase().WithSurfaceFlags( \
        SurfaceFlags::TO_SRGB_COLORSPACE));                                  \
  }                                                                          \
                                                                             \
  TEST_F(ImageDecoders, test_prefix##BadBuffer) {                            \
    CheckDecoderBadBuffer(Green##test_prefix##TestCase().WithFlags(          \
        TEST_CASE_HAS_ERROR | TEST_CASE_IGNORE_OUTPUT));                     \
  }

IMAGE_GTEST_DECODER_BASE_F(PNG)
IMAGE_GTEST_DECODER_BASE_F(GIF)
IMAGE_GTEST_DECODER_BASE_F(JPG)
IMAGE_GTEST_DECODER_BASE_F(BMP)
IMAGE_GTEST_DECODER_BASE_F(ICO)
IMAGE_GTEST_DECODER_BASE_F(Icon)
IMAGE_GTEST_DECODER_BASE_F(WebP)
IMAGE_GTEST_DECODER_BASE_F(AVIF)
#ifdef MOZ_JXL
IMAGE_GTEST_DECODER_BASE_F(JXL)
#endif

TEST_F(ImageDecoders, ICOWithANDMaskDownscaleDuringDecode) {
  CheckDownscaleDuringDecode(DownscaledTransparentICOWithANDMaskTestCase());
}

static void CheckWebPDownscaleDuringDecode(const ImageTestCase& aTestCase,
                                           bool aScaledDecoding) {
  bool oldValue = false;
  Preferences::GetBool("image.webp.scaled-decoding.enabled", &oldValue);
  Preferences::SetBool("image.webp.scaled-decoding.enabled", aScaledDecoding);
  auto resetPref = MakeScopeExit([&] {
    Preferences::SetBool("image.webp.scaled-decoding.enabled", oldValue);
  });

  CheckDownscaleDuringDecode(aTestCase);
}

TEST_F(ImageDecoders, WebPUnscaledDownscaleDuringDecode) {
  CheckWebPDownscaleDuringDecode(DownscaledWebPTestCase(), false);
}

TEST_F(ImageDecoders, WebPLossyUnscaledDownscaleDuringDecode) {
  CheckWebPDownscaleDuringDecode(DownscaledLossyWebPTestCase(), false);
}

TEST_F(ImageDecoders, WebPScaledDownscaleDuringDecode) {
  CheckWebPDownscaleDuringDecode(DownscaledWebPTestCase(), true);
}

TEST_F(ImageDecoders, WebPLossyScaledDownscaleDuringDecode) {
  CheckWebPDownscaleDuringDecode(DownscaledLossyWebPTestCase(), true);
}

TEST_F(ImageDecoders, TransparentWebPScaledDownscaleDuringDecode) {
  // Images with alpha are meant to take the full-size path even with the pref
  // on. A solid image cannot tell the two paths apart, so this only checks
  // that the decode still succeeds and produces the right colour.
  bool oldValue = false;
  Preferences::GetBool("image.webp.scaled-decoding.enabled", &oldValue);
  Preferences::SetBool("image.webp.scaled-decoding.enabled", true);
  auto resetPref = MakeScopeExit([&] {
    Preferences::SetBool("image.webp.scaled-decoding.enabled", oldValue);
  });

  ImageTestCase testCase = TransparentWebPTestCase();
  IntSize outputSize(20, 20);

  WithSingleChunkDecode(
      testCase, Some(outputSize), /* aUseDecodePool */ false,
      [&](image::Decoder* aDecoder) {
        ASSERT_FALSE(aDecoder->HasError());
        EXPECT_TRUE(aDecoder->GetDecodeDone());

        RawAccessFrameRef currentFrame = aDecoder->GetCurrentFrameRef();
        ASSERT_TRUE(bool(currentFrame));
        RefPtr<SourceSurface> surface = currentFrame->GetSourceSurface();
        ASSERT_TRUE(surface != nullptr);

        EXPECT_EQ(outputSize, surface->GetSize());
        EXPECT_TRUE(IsSolidColor(surface, testCase.Color().Premultiply(),
                                 /* aFuzz = */ 2));
      });
}

TEST_F(ImageDecoders, JPGExifOrientationDctDownscale) {
  CheckExifOrientationDownscale(ExifOrientationDownscaleJPGTestCase());
}

TEST_F(ImageDecoders, WebPLargeMultiChunk) {
  CheckDecoderMultiChunk(LargeWebPTestCase(), /* aChunkSize */ 64);
}

TEST_F(ImageDecoders, WebPIccSrgbMultiChunk) {
  CheckDecoderMultiChunk(GreenWebPIccSrgbTestCase());
}

TEST_F(ImageDecoders, WebPTransparentSingleChunk) {
  CheckDecoderSingleChunk(TransparentWebPTestCase());
}

TEST_F(ImageDecoders, WebPTransparentNoAlphaHeaderSingleChunk) {
  CheckDecoderSingleChunk(TransparentNoAlphaHeaderWebPTestCase());
}

TEST_F(ImageDecoders, AVIFTransparentSingleChunk) {
  CheckDecoderSingleChunk(TransparentAVIFTestCase());
}

TEST_F(ImageDecoders, PNGTransparentSingleChunk) {
  CheckDecoderSingleChunk(TransparentPNGTestCase());
}

TEST_F(ImageDecoders, GIFTransparentSingleChunk) {
  CheckDecoderSingleChunk(TransparentGIFTestCase());
}

#ifdef MOZ_JXL
TEST_F(ImageDecoders, JXLTransparentSingleChunk) {
  CheckDecoderSingleChunk(TransparentJXLTestCase());
}
#endif

TEST_F(ImageDecoders, AVIFSingleChunkNonzeroReserved) {
  CheckDecoderSingleChunk(NonzeroReservedAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkMultipleColr) {
  CheckDecoderSingleChunk(MultipleColrAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent10bit420) {
  CheckDecoderSingleChunk(Transparent10bit420AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent10bit422) {
  CheckDecoderSingleChunk(Transparent10bit422AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent10bit444) {
  CheckDecoderSingleChunk(Transparent10bit444AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent12bit420) {
  CheckDecoderSingleChunk(Transparent12bit420AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent12bit422) {
  CheckDecoderSingleChunk(Transparent12bit422AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent12bit444) {
  CheckDecoderSingleChunk(Transparent12bit444AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent8bit420) {
  CheckDecoderSingleChunk(Transparent8bit420AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent8bit422) {
  CheckDecoderSingleChunk(Transparent8bit422AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkTransparent8bit444) {
  CheckDecoderSingleChunk(Transparent8bit444AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitLimitedRangeBT601) {
  CheckDecoderSingleChunk(Gray8bitLimitedRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitLimitedRangeBT709) {
  CheckDecoderSingleChunk(Gray8bitLimitedRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitLimitedRangeBT2020) {
  CheckDecoderSingleChunk(Gray8bitLimitedRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitFullRangeBT601) {
  CheckDecoderSingleChunk(Gray8bitFullRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitFullRangeBT709) {
  CheckDecoderSingleChunk(Gray8bitFullRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitFullRangeBT2020) {
  CheckDecoderSingleChunk(Gray8bitFullRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitLimitedRangeBT601) {
  CheckDecoderSingleChunk(Gray10bitLimitedRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitLimitedRangeBT709) {
  CheckDecoderSingleChunk(Gray10bitLimitedRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitLimitedRangeBT2020) {
  CheckDecoderSingleChunk(Gray10bitLimitedRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitFullRangeBT601) {
  CheckDecoderSingleChunk(Gray10bitFullRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitFullRangeBT709) {
  CheckDecoderSingleChunk(Gray10bitFullRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitFullRangeBT2020) {
  CheckDecoderSingleChunk(Gray10bitFullRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitLimitedRangeBT601) {
  CheckDecoderSingleChunk(Gray12bitLimitedRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitLimitedRangeBT709) {
  CheckDecoderSingleChunk(Gray12bitLimitedRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitLimitedRangeBT2020) {
  CheckDecoderSingleChunk(Gray12bitLimitedRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitFullRangeBT601) {
  CheckDecoderSingleChunk(Gray12bitFullRangeBT601AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitFullRangeBT709) {
  CheckDecoderSingleChunk(Gray12bitFullRangeBT709AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitFullRangeBT2020) {
  CheckDecoderSingleChunk(Gray12bitFullRangeBT2020AVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitLimitedRangeGrayscale) {
  CheckDecoderSingleChunk(Gray8bitLimitedRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray8bitFullRangeGrayscale) {
  CheckDecoderSingleChunk(Gray8bitFullRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitLimitedRangeGrayscale) {
  CheckDecoderSingleChunk(Gray10bitLimitedRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray10bitFullRangeGrayscale) {
  CheckDecoderSingleChunk(Gray10bitFullRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitLimitedRangeGrayscale) {
  CheckDecoderSingleChunk(Gray12bitLimitedRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFSingleChunkGray12bitFullRangeGrayscale) {
  CheckDecoderSingleChunk(Gray12bitFullRangeGrayscaleAVIFTestCase());
}

TEST_F(ImageDecoders, AVIFMultiLayerSingleChunk) {
  CheckDecoderSingleChunk(MultiLayerAVIFTestCase());
}

// This test must use the decode pool in order to check for regressions
// of crashing the dav1d decoder when the ImgDecoder threads have a standard-
// sized stack.
TEST_F(ImageDecoders, AVIFStackCheck) {
  CheckDecoderSingleChunk(StackCheckAVIFTestCase(), /* aUseDecodePool */ true);
}

TEST_F(ImageDecoders, AVIFLargeMultiChunk) {
  CheckDecoderMultiChunk(LargeAVIFTestCase(), /* aChunkSize */ 64);
}

#ifdef MOZ_JXL
TEST_F(ImageDecoders, JXLLargeMultiChunk) {
  CheckDecoderMultiChunk(LargeJXLTestCase(), /* aChunkSize */ 64);
}

#  ifdef DEBUG
// Feeding a large JXL in tiny chunks must not cause WritePixelRowsToPipe to run
// once per chunk or too much. The decoder should only do that work when
// flush_pixels reports new pixels.
TEST_F(ImageDecoders, JXLLargeMultiChunkPipeWriteCount) {
  ImageTestCase testCase = LargeJXLTestCase();
  WithMultiChunkDecode(testCase, /* aChunkSize */ 64,
                       [&](image::Decoder* aDecoder) {
                         ASSERT_FALSE(aDecoder->HasError());
                         ASSERT_EQ(aDecoder->GetType(), DecoderType::JXL);
                         auto* jxl = static_cast<nsJXLDecoder*>(aDecoder);
                         EXPECT_LE(jxl->GetWritePixelRowsCount(), 16u);
                       });
}
#  endif /* DEBUG */

// Regression test for bug 2054317: feeding a progressive (multi-pass) lossy
// RGBA JXL that spans more than one coded group in small chunks must not
// panic. FlushPartialFrame's re-render pass could re-flush a group's modular
// alpha buffer that a previous flush had already fully consumed. The exact
// chunk boundaries that trigger this depend on decoder-internal timing, so
// this sweeps several chunk sizes rather than relying on a single guess.
TEST_F(ImageDecoders, JXLProgressiveAlphaMultiGroupMultiChunk) {
  for (uint64_t chunkSize : {8, 16, 32, 64, 128, 256}) {
    CheckDecoderMultiChunk(ProgressiveAlphaMultiGroupJXLTestCase(), chunkSize);
  }
}

// aParticipants determines the number of participants (threads).
// aUseDecodePool picks which of the two places a decode can run: false is a
// synchronous decode on the main thread, true puts it on a DecodePool thread.
// The helpers go to the JXL decode pool either way.
static already_AddRefed<SourceSurface> DecodeLargeJXL(uint32_t aParticipants,
                                                      bool aUseDecodePool) {
  uint32_t oldValue = Preferences::GetUint("image.jxl.decode_participants", 1);
  auto restore = MakeScopeExit(
      [&] { Preferences::SetUint("image.jxl.decode_participants", oldValue); });
  Preferences::SetUint("image.jxl.decode_participants", aParticipants);

  ImageTestCase testCase = LargeJXLTestCase();
  RefPtr<SourceSurface> surface;
  WithSingleChunkDecode(testCase, Nothing(), aUseDecodePool,
                        [&](image::Decoder* aDecoder) {
                          surface = CheckDecoderState(testCase, aDecoder);
                        });
  return surface.forget();
}

// aMaxPixels is how many pixels may differ at all, aMaxChannelDiff how far any
// one channel of those may be off.
//
// Walks row by row rather than comparing in one go, because the two surfaces
// are allocated separately and need not share a stride, and to not compare
// padding bytes.
static void ExpectSurfacesSimilar(SourceSurface* aExpected,
                                  SourceSurface* aActual, uint32_t aMaxPixels,
                                  uint8_t aMaxChannelDiff) {
  ASSERT_TRUE(aExpected && aActual);
  ASSERT_EQ(aExpected->GetSize(), aActual->GetSize());
  ASSERT_EQ(aExpected->GetFormat(), aActual->GetFormat());

  RefPtr<DataSourceSurface> expected = aExpected->GetDataSurface();
  RefPtr<DataSourceSurface> actual = aActual->GetDataSurface();
  ASSERT_TRUE(expected && actual);

  DataSourceSurface::ScopedMap expectedMap(expected, DataSourceSurface::READ);
  DataSourceSurface::ScopedMap actualMap(actual, DataSourceSurface::READ);
  ASSERT_TRUE(expectedMap.IsMapped() && actualMap.IsMapped());

  const IntSize size = expected->GetSize();
  const size_t bytesPerPixel = BytesPerPixel(expected->GetFormat());
  // On an opaque surface the fourth byte is padding, and two decoders need not
  // agree on what they leave there.
  const size_t channels =
      expected->GetFormat() == SurfaceFormat::OS_RGBX ? 3 : bytesPerPixel;

  uint32_t differingPixels = 0;
  uint32_t worstChannelDiff = 0;
  for (int32_t row = 0; row < size.height; ++row) {
    const uint8_t* expectedRow =
        expectedMap.GetData() + row * expectedMap.GetStride();
    const uint8_t* actualRow =
        actualMap.GetData() + row * actualMap.GetStride();
    for (int32_t col = 0; col < size.width; ++col) {
      uint32_t pixelDiff = 0;
      for (size_t channel = 0; channel < channels; ++channel) {
        const size_t i = col * bytesPerPixel + channel;
        const uint32_t diff = expectedRow[i] > actualRow[i]
                                  ? expectedRow[i] - actualRow[i]
                                  : actualRow[i] - expectedRow[i];
        pixelDiff = std::max(pixelDiff, diff);
      }
      if (pixelDiff > 0) {
        ++differingPixels;
        worstChannelDiff = std::max(worstChannelDiff, pixelDiff);
      }
    }
  }

  EXPECT_LE(differingPixels, aMaxPixels)
      << "worst channel difference " << worstChannelDiff;
  EXPECT_LE(worstChannelDiff, uint32_t(aMaxChannelDiff))
      << differingPixels << " pixels differ";
}

static void ExpectSurfacesIdentical(SourceSurface* aExpected,
                                    SourceSurface* aActual) {
  ExpectSurfacesSimilar(aExpected, aActual, /* aMaxPixels */ 0,
                        /* aMaxChannelDiff */ 0);
}

// Run a jxl decode serially and in parallel and check the result is identical.
TEST_F(ImageDecoders, JXLParallelDecodeMatchesSerial) {
  // Make the pool at full size if it's not already made so that it's not
  // created with fewer threads. The first parallel decode determines the pool
  // size for the remainder of the process lifetime. This only works if the pref
  // defaults to either 1 or 0, otherwise the preceding jxl tests would create
  // a pool at the default pref value.
  RefPtr<SourceSurface> discarded =
      DecodeLargeJXL(0, /* aUseDecodePool */ false);

  // reference surface = 1 decode thread which is the main thread.
  RefPtr<SourceSurface> reference =
      DecodeLargeJXL(1, /* aUseDecodePool */ false);

  {
    // Compare to the same image in another format to make sure it's the right
    // reference.
#  if defined(XP_WIN) && !defined(HAVE_64BIT_BUILD)
    const uint32_t kReferenceMaxDifferingPixels = 7;
    const uint8_t kReferenceMaxChannelDiff = 1;
#  elif defined(ANDROID)
    const uint32_t kReferenceMaxDifferingPixels = 4;
    const uint8_t kReferenceMaxChannelDiff = 1;
#  else
    const uint32_t kReferenceMaxDifferingPixels = 0;
    const uint8_t kReferenceMaxChannelDiff = 0;
#  endif
    ImageTestCase referenceCase = LargeJXLReferenceWebPTestCase();
    RefPtr<SourceSurface> webpReference;
    WithSingleChunkDecode(referenceCase, Nothing(), /* aUseDecodePool */ false,
                          [&](image::Decoder* aDecoder) {
                            webpReference =
                                CheckDecoderState(referenceCase, aDecoder);
                          });
    ExpectSurfacesSimilar(webpReference, reference,
                          kReferenceMaxDifferingPixels,
                          kReferenceMaxChannelDiff);
  }

  for (bool useDecodePool : {false, true}) {
    for (uint32_t participants : {1u, 0u, 2u, 8u, 64u}) {
      SCOPED_TRACE(testing::Message() << "participants=" << participants
                                      << " useDecodePool=" << useDecodePool);
      RefPtr<SourceSurface> surface =
          DecodeLargeJXL(participants, useDecodePool);
      ExpectSurfacesIdentical(reference, surface);
    }
  }
}

// Stress test the pool with many more tasks than it has threads.
TEST_F(ImageDecoders, JXLConcurrentDecodes) {
  uint32_t oldValue = Preferences::GetUint("image.jxl.decode_participants", 1);
  auto restore = MakeScopeExit(
      [&] { Preferences::SetUint("image.jxl.decode_participants", oldValue); });

  RefPtr<SourceSurface> reference =
      DecodeLargeJXL(1, /* aUseDecodePool */ false);

  Preferences::SetUint("image.jxl.decode_participants", 0);

  ImageTestCase testCase = LargeJXLTestCase();
  nsTArray<RefPtr<image::Decoder>> decoders;
  nsTArray<RefPtr<MonitorAnonymousDecodingTask>> tasks;

  for (size_t i = 0; i < 16; ++i) {
    nsCOMPtr<nsIInputStream> inputStream = LoadFile(testCase.mPath);
    ASSERT_TRUE(inputStream != nullptr);

    uint64_t length;
    ASSERT_NS_SUCCEEDED(inputStream->Available(&length));

    auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
    sourceBuffer->ExpectLength(length);
    ASSERT_NS_SUCCEEDED(
        sourceBuffer->AppendFromInputStream(inputStream, length));
    sourceBuffer->Complete(NS_OK);

    RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
        DecoderFactory::GetDecoderType(testCase.mMimeType), sourceBuffer,
        Nothing(), DefaultDecoderFlags() | DecoderFlags::FIRST_FRAME_ONLY,
        testCase.mSurfaceFlags);
    ASSERT_TRUE(decoder != nullptr);

    decoders.AppendElement(decoder);
    tasks.AppendElement(MakeRefPtr<MonitorAnonymousDecodingTask>(
        WrapNotNull(decoder.get()), /* aResumable */ false));
  }

  // Queue them all before waiting on any, or the pool is never contended.
  for (auto& task : tasks) {
    DecodePool::Singleton()->AsyncRun(task.get());
  }
  for (auto& task : tasks) {
    task->WaitUntilFinished();
  }

  for (size_t i = 0; i < decoders.Length(); ++i) {
    SCOPED_TRACE(testing::Message() << "decode=" << i);
    RefPtr<SourceSurface> surface = CheckDecoderState(testCase, decoders[i]);
    ExpectSurfacesIdentical(reference, surface);
  }
}
#endif /* MOZ_JXL */

TEST_F(ImageDecoders, AnimatedGIFSingleChunk) {
  CheckDecoderSingleChunk(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedGIFMultiChunk) {
  CheckDecoderMultiChunk(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedGIFWithBlendedFrames) {
  CheckAnimationDecoderSingleChunk(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedPNGSingleChunk) {
  CheckDecoderSingleChunk(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedPNGMultiChunk) {
  CheckDecoderMultiChunk(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedPNGWithBlendedFrames) {
  CheckAnimationDecoderSingleChunk(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedWebPSingleChunk) {
  CheckDecoderSingleChunk(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedWebPMultiChunk) {
  CheckDecoderMultiChunk(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedWebPWithBlendedFrames) {
  CheckAnimationDecoderSingleChunk(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIFSingleChunk) {
  CheckDecoderSingleChunk(GreenFirstFrameAnimatedAVIFTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIFMultiChunk) {
  CheckDecoderMultiChunk(GreenFirstFrameAnimatedAVIFTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIFWithBlendedFrames) {
  CheckAnimationDecoderSingleChunk(GreenFirstFrameAnimatedAVIFTestCase());
}

#ifdef MOZ_JXL
TEST_F(ImageDecoders, AnimatedJXLSingleChunk) {
  CheckDecoderSingleChunk(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, AnimatedJXLMultiChunk) {
  CheckDecoderMultiChunk(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, AnimatedJXLWithBlendedFrames) {
  CheckAnimationDecoderSingleChunk(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, CorruptJXLSingleChunk) {
  CheckDecoderSingleChunk(CorruptJXLTestCase());
}
#endif

static void CheckTruncatedMultiScanNoLeak(const ImageTestCase& aTestCase) {
  WithSingleChunkDecode(
      aTestCase, Nothing(), /* aUseDecodePool = */ false,
      [&](image::Decoder* aDecoder) {
        EXPECT_TRUE(aDecoder->GetDecodeDone());

        RawAccessFrameRef frame = aDecoder->GetCurrentFrameRef();
        ASSERT_TRUE(frame);
        RefPtr<SourceSurface> surface = frame->GetSourceSurface();
        ASSERT_TRUE(surface);
        EXPECT_TRUE(IsSolidColor(surface, BGRAColor(0xFF, 0xFF, 0xFF, 0xFF)));
      });
}

TEST_F(ImageDecoders, LosslessNonInterleavedSingleChunk) {
  CheckDecoderSingleChunk(LosslessNonInterleavedJPGTestCase());
}

TEST_F(ImageDecoders, LosslessNonInterleavedMultiChunk) {
  CheckDecoderMultiChunk(LosslessNonInterleavedJPGTestCase());
}

TEST_F(ImageDecoders, TruncatedLosslessNonInterleavedNoLeak) {
  CheckTruncatedMultiScanNoLeak(TruncatedLosslessNonInterleavedJPGTestCase());
}

TEST_F(ImageDecoders, CorruptSingleChunk) {
  CheckDecoderSingleChunk(CorruptTestCase());
}

TEST_F(ImageDecoders, CorruptMultiChunk) {
  CheckDecoderMultiChunk(CorruptTestCase());
}

TEST_F(ImageDecoders, CorruptBMPWithTruncatedHeaderSingleChunk) {
  CheckDecoderSingleChunk(CorruptBMPWithTruncatedHeader());
}

TEST_F(ImageDecoders, CorruptBMPWithTruncatedHeaderMultiChunk) {
  CheckDecoderMultiChunk(CorruptBMPWithTruncatedHeader());
}

TEST_F(ImageDecoders, CorruptICOWithBadBMPWidthSingleChunk) {
  CheckDecoderSingleChunk(CorruptICOWithBadBMPWidthTestCase());
}

TEST_F(ImageDecoders, CorruptICOWithBadBMPWidthMultiChunk) {
  CheckDecoderMultiChunk(CorruptICOWithBadBMPWidthTestCase());
}

TEST_F(ImageDecoders, CorruptICOWithBadBMPHeightSingleChunk) {
  CheckDecoderSingleChunk(CorruptICOWithBadBMPHeightTestCase());
}

TEST_F(ImageDecoders, CorruptICOWithBadBMPHeightMultiChunk) {
  CheckDecoderMultiChunk(CorruptICOWithBadBMPHeightTestCase());
}

TEST_F(ImageDecoders, CorruptICOWithBadBppSingleChunk) {
  CheckDecoderSingleChunk(CorruptICOWithBadBppTestCase());
}

// Running this test under emulation for Android 7 on x86_64 seems to result
// in the large allocation succeeding, but leaving so little memory left the
// system falls over and it kills the test run, so we skip it instead.
// See bug 1655846 for more details.
#ifndef ANDROID
TEST_F(ImageDecoders, CorruptAVIFSingleChunk) {
  CheckDecoderSingleChunk(CorruptAVIFTestCase());
}
#endif

TEST_F(ImageDecoders, AnimatedGIFWithFRAME_FIRST) {
  CheckDecoderFrameFirst(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedGIFWithFRAME_CURRENT) {
  CheckDecoderFrameCurrent(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedWebPWithFRAME_FIRST) {
  CheckDecoderFrameFirst(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedWebPWithFRAME_CURRENT) {
  CheckDecoderFrameCurrent(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedPNGWithFRAME_FIRST) {
  CheckDecoderFrameFirst(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedPNGWithFRAME_CURRENT) {
  CheckDecoderFrameCurrent(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIFWithFRAME_FIRST) {
  CheckDecoderFrameFirst(GreenFirstFrameAnimatedAVIFTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIFWithFRAME_CURRENT) {
  CheckDecoderFrameCurrent(GreenFirstFrameAnimatedAVIFTestCase());
}

#ifdef MOZ_JXL
TEST_F(ImageDecoders, AnimatedJXLWithFRAME_FIRST) {
  CheckDecoderFrameFirst(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, AnimatedJXLWithFRAME_CURRENT) {
  CheckDecoderFrameCurrent(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, JXL_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenJXLTestCase());
}

TEST_F(ImageDecoders, JXLProgressive_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(ProgressiveJXLTestCase());
}

TEST_F(ImageDecoders, AnimatedJXL_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenFirstFrameAnimatedJXLTestCase());
}

TEST_F(ImageDecoders, LongAnimatedJXL_IncrementalFrameCountUpdates) {
  ImageTestCase testCase = LongAnimatedJXLTestCase();

  nsCOMPtr<nsIInputStream> inputStream = LoadFile(testCase.mPath);
  ASSERT_TRUE(inputStream != nullptr);

  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
  sourceBuffer->ExpectLength(length);

  DecoderType decoderType = DecoderFactory::GetDecoderType(testCase.mMimeType);
  DecoderFlags decoderFlags =
      DefaultDecoderFlags() | DecoderFlags::COUNT_FRAMES;
  RefPtr<image::Decoder> decoder =
      DecoderFactory::CreateAnonymousMetadataDecoder(decoderType, sourceBuffer,
                                                     decoderFlags);
  ASSERT_TRUE(decoder != nullptr);

  auto task = MakeRefPtr<AnonymousDecodingTask>(WrapNotNull(decoder),
                                                /* aResumable */ true);
  task->Run();

  // Feed only the first half of the data in fixed-size chunks, tracking how
  // many times the scanned frame count (reported via PostFrameCount) increases.
  const uint64_t kChunkSize = 4096;
  uint64_t halfLength = length / 2;
  uint64_t remaining = halfLength;
  uint32_t lastFrameCount = 0;
  uint32_t frameCountIncreases = 0;

  while (remaining > 0) {
    uint64_t read = std::min(remaining, kChunkSize);
    remaining -= read;

    rv = sourceBuffer->AppendFromInputStream(inputStream, read);
    ASSERT_NS_SUCCEEDED(rv);

    SpinPendingEvents();

    const ImageMetadata& metadata = decoder->GetImageMetadata();
    if (metadata.HasFrameCount()) {
      uint32_t currentFrameCount = metadata.GetFrameCount();
      if (currentFrameCount > lastFrameCount) {
        frameCountIncreases++;
        lastFrameCount = currentFrameCount;
      }
    }
  }

  EXPECT_GE(frameCountIncreases, 4u);
  EXPECT_GE(lastFrameCount, 300u);
}

#endif

TEST_F(ImageDecoders, BMP_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenBMPTestCase());
}

TEST_F(ImageDecoders, GIF_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenGIFTestCase());
}

TEST_F(ImageDecoders, AnimatedGIF_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenFirstFrameAnimatedGIFTestCase());
}

TEST_F(ImageDecoders, ICO_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenICOTestCase());
}

TEST_F(ImageDecoders, LargeICOWithPNG_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(LargeICOWithPNGTestCase());
}

TEST_F(ImageDecoders, JPEG_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenJPGTestCase());
}

TEST_F(ImageDecoders, PNG_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenPNGTestCase());
}

TEST_F(ImageDecoders, AnimatedPNG_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenFirstFrameAnimatedPNGTestCase());
}

TEST_F(ImageDecoders, TransparentPNG_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(TransparentPNGTestCase());
}

TEST_F(ImageDecoders, WebP_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenWebPTestCase());
}

TEST_F(ImageDecoders, AnimatedWebP_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenFirstFrameAnimatedWebPTestCase());
}

TEST_F(ImageDecoders, TransparentWebP_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(TransparentWebPTestCase());
}

TEST_F(ImageDecoders, AVIF_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenAVIFTestCase());
}

TEST_F(ImageDecoders, AnimatedAVIF_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(GreenFirstFrameAnimatedAVIFTestCase());
}

TEST_F(ImageDecoders, TransparentAVIF_IncrementalDecodeMatchesOneShot) {
  CheckIncrementalDecodeMatchesOneShot(TransparentAVIFTestCase());
}

TEST_F(ImageDecoders, AnimatedGIFWithExtraImageSubBlocks) {
  ImageTestCase testCase = ExtraImageSubBlocksAnimatedGIFTestCase();

  // Verify that we can decode this test case and get two frames, even though
  // there are extra image sub blocks between the first and second frame. The
  // extra data shouldn't confuse the decoder or cause the decode to fail.

  // Create an image.
  RefPtr<Image> image = TestCaseToDecodedImage(testCase);

  // Ensure that the image's metadata meets our expectations.
  IntSize imageSize(0, 0);
  nsresult rv = image->GetWidth(&imageSize.width);
  EXPECT_NS_SUCCEEDED(rv);
  rv = image->GetHeight(&imageSize.height);
  EXPECT_NS_SUCCEEDED(rv);

  EXPECT_EQ(testCase.mSize.width, imageSize.width);
  EXPECT_EQ(testCase.mSize.height, imageSize.height);

  RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
  Progress imageProgress = tracker->GetProgress();

  EXPECT_TRUE(bool(imageProgress & FLAG_IS_ANIMATED) == true);

  EXPECT_EQ(bool(testCase.mFlags & TEST_CASE_IS_TRANSPARENT),
            bool(imageProgress & FLAG_HAS_TRANSPARENCY));
  EXPECT_EQ(bool(testCase.mFlags & TEST_CASE_IS_ANIMATED),
            bool(imageProgress & FLAG_IS_ANIMATED));

  // Ensure that we decoded both frames of the image.
  LookupResult result =
      SurfaceCache::Lookup(ImageKey(image.get()),
                           RasterSurfaceKey(imageSize, testCase.mSurfaceFlags,
                                            PlaybackType::eAnimated),
                           /* aMarkUsed = */ true);
  ASSERT_EQ(MatchType::EXACT, result.Type());

  EXPECT_NS_SUCCEEDED(result.Surface().Seek(0));
  EXPECT_TRUE(bool(result.Surface()));

  RefPtr<imgFrame> partialFrame = result.Surface().GetFrame(1);
  EXPECT_TRUE(bool(partialFrame));
}

TEST_F(ImageDecoders, TruncatedSmallGIFSingleChunk) {
  CheckDecoderSingleChunk(TruncatedSmallGIFTestCase());
}

TEST_F(ImageDecoders, LargeICOWithBMPSingleChunk) {
  CheckDecoderSingleChunk(LargeICOWithBMPTestCase());
}

TEST_F(ImageDecoders, LargeICOWithBMPMultiChunk) {
  CheckDecoderMultiChunk(LargeICOWithBMPTestCase(), /* aChunkSize */ 64);
}

TEST_F(ImageDecoders, LargeICOWithPNGSingleChunk) {
  CheckDecoderSingleChunk(LargeICOWithPNGTestCase());
}

TEST_F(ImageDecoders, LargeICOWithPNGMultiChunk) {
  CheckDecoderMultiChunk(LargeICOWithPNGTestCase());
}

TEST_F(ImageDecoders, MultipleSizesICOSingleChunk) {
  ImageTestCase testCase = GreenMultipleSizesICOTestCase();

  // Create an image.
  RefPtr<Image> image = ImageFactory::CreateAnonymousImage(
      nsDependentCString(testCase.mMimeType));
  ASSERT_TRUE(!image->HasError());

  nsCOMPtr<nsIInputStream> inputStream = LoadFile(testCase.mPath);
  ASSERT_TRUE(inputStream);

  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = inputStream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);

  // Write the data into the image.
  rv = image->OnImageDataAvailable(nullptr, inputStream, 0,
                                   static_cast<uint32_t>(length));
  ASSERT_NS_SUCCEEDED(rv);

  // Let the image know we've sent all the data.
  rv = image->OnImageDataComplete(nullptr, NS_OK, true);
  ASSERT_NS_SUCCEEDED(rv);

  RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
  tracker->SyncNotifyProgress(FLAG_LOAD_COMPLETE);

  // Use GetFrame() to force a sync decode of the image.
  RefPtr<SourceSurface> surface = image->GetFrame(
      imgIContainer::FRAME_CURRENT, imgIContainer::FLAG_SYNC_DECODE);

  // Ensure that the image's metadata meets our expectations.
  IntSize imageSize(0, 0);
  rv = image->GetWidth(&imageSize.width);
  EXPECT_NS_SUCCEEDED(rv);
  rv = image->GetHeight(&imageSize.height);
  EXPECT_NS_SUCCEEDED(rv);

  EXPECT_EQ(testCase.mSize.width, imageSize.width);
  EXPECT_EQ(testCase.mSize.height, imageSize.height);

  nsTArray<IntSize> nativeSizes;
  rv = image->GetNativeSizes(nativeSizes);
  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_EQ(6u, nativeSizes.Length());

  IntSize expectedSizes[] = {IntSize(16, 16),   IntSize(32, 32),
                             IntSize(64, 64),   IntSize(128, 128),
                             IntSize(256, 256), IntSize(256, 128)};

  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(expectedSizes[i], nativeSizes[i]);
  }

  RefPtr<Image> image90 =
      ImageOps::Orient(image, Orientation(Angle::D90, Flip::Unflipped));
  rv = image90->GetNativeSizes(nativeSizes);
  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_EQ(6u, nativeSizes.Length());

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(expectedSizes[i], nativeSizes[i]);
  }
  EXPECT_EQ(IntSize(128, 256), nativeSizes[5]);

  RefPtr<Image> image180 =
      ImageOps::Orient(image, Orientation(Angle::D180, Flip::Unflipped));
  rv = image180->GetNativeSizes(nativeSizes);
  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_EQ(6u, nativeSizes.Length());

  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(expectedSizes[i], nativeSizes[i]);
  }
}

TEST_F(ImageDecoders, ExifResolutionEven) {
  RefPtr<Image> image = TestCaseToDecodedImage(ExifResolutionTestCase());
  EXPECT_EQ(image->GetResolution(), Resolution(2.0, 2.0));
}

TEST_F(ImageDecoders, AVIFConcurrentFilmGrainDecode) {
  // Route AVIF through libaom instead of dav1d to hit the vulnerable path.
  nsresult rv = Preferences::SetBool("image.avif.use-dav1d", false);
  ASSERT_NS_SUCCEEDED(rv);
  auto cleanup =
      MakeScopeExit([] { Preferences::SetBool("image.avif.use-dav1d", true); });

  ImageTestCase testCase420("avif_420_film_grain.avif", "image/avif",
                            IntSize(128, 128), TEST_CASE_IGNORE_OUTPUT);
  ImageTestCase testCase444("avif_444_film_grain.avif", "image/avif",
                            IntSize(128, 128), TEST_CASE_IGNORE_OUTPUT);

  const int kIterations = 50;
  nsTArray<RefPtr<MonitorAnonymousDecodingTask>> tasks;

  for (int i = 0; i < kIterations; ++i) {
    const ImageTestCase& tc = (i % 2 == 0) ? testCase420 : testCase444;

    nsCOMPtr<nsIInputStream> inputStream = LoadFile(tc.mPath);
    ASSERT_TRUE(inputStream != nullptr);

    uint64_t length;
    rv = inputStream->Available(&length);
    ASSERT_NS_SUCCEEDED(rv);

    auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
    sourceBuffer->ExpectLength(length);
    rv = sourceBuffer->AppendFromInputStream(inputStream, length);
    ASSERT_NS_SUCCEEDED(rv);
    sourceBuffer->Complete(NS_OK);

    DecoderType decoderType = DecoderFactory::GetDecoderType(tc.mMimeType);
    RefPtr<image::Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
        decoderType, sourceBuffer, Nothing(),
        DefaultDecoderFlags() | DecoderFlags::FIRST_FRAME_ONLY,
        tc.mSurfaceFlags);
    ASSERT_TRUE(decoder != nullptr);

    RefPtr<MonitorAnonymousDecodingTask> task =
        new MonitorAnonymousDecodingTask(WrapNotNull(decoder), false);
    tasks.AppendElement(task);
    DecodePool::Singleton()->AsyncRun(task.get());
  }

  for (auto& task : tasks) {
    task->WaitUntilFinished();
  }
}
