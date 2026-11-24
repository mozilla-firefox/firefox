/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <initializer_list>

#include "ImageContainer.h"
#include "MediaFormatReader.h"
#include "MockDecoderModule.h"
#include "MockMediaDataDemuxer.h"
#include "MockMediaDecoderOwner.h"
#include "PDMFactory.h"
#include "ReaderProxy.h"
#include "TimeUnits.h"
#include "VideoFrameContainer.h"
#include "gtest/gtest.h"
#include "mozilla/Atomics.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsQueryObject.h"

using namespace mozilla;
using namespace mozilla::layers;

using DecodePromise = MediaDataDecoder::DecodePromise;
using SamplesHolder = MediaTrackDemuxer::SamplesHolder;
using SamplesPromise = MediaTrackDemuxer::SamplesPromise;
using SeekPromise = MediaTrackDemuxer::SeekPromise;
using TrackType = TrackInfo::TrackType;
using media::TimeInterval;
using media::TimeIntervals;
using media::TimeUnit;
using testing::InSequence;
using testing::MockFunction;
using testing::Return;
using testing::StrEq;

static constexpr const char* kTestVideoMime =
    "video/x-test; width=640; height=360";
static constexpr const char* kTestAudioMime = "audio/x-test";

static RefPtr<MediaRawData> MakeRawSample(const TimeUnit& aTime,
                                          const TimeUnit& aDuration,
                                          bool aKeyframe = false) {
  RefPtr sample = new MediaRawData;
  sample->mTime = aTime;
  sample->mDuration = aDuration;
  sample->mKeyframe = aKeyframe;
  return sample;
}

static RefPtr<SamplesPromise> ResolveOneSample(RefPtr<MediaRawData> aSample) {
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  samples->AppendSample(std::move(aSample));
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

// Shared setup for MediaFormatReader gtests: a single video track backed by a
// MockMediaDataDemuxer/MockMediaTrackDemuxer and a MockDecoderModule.
class TestMediaFormatReader : public ::testing::Test {
 protected:
  void SetUp() override {
    SetUpReaderDependencies();
    InitReader();
  }

  void SetUpReaderDependencies() {
    mDataDemuxer = new MockMediaDataDemuxer();
    // VideoInfo::IsValid() needs dimensions.
    mTrackDemuxer = new MockMediaTrackDemuxer(kTestVideoMime);

    ON_CALL(*mDataDemuxer, GetNumberTracks)
        .WillByDefault([this](TrackType aType) {
          if (aType == TrackType::kAudioTrack) {
            return mAudioTrackDemuxer ? 1u : 0u;
          }
          if (aType == TrackType::kVideoTrack) {
            return 1u;
          }
          return 0u;
        });
    ON_CALL(*mDataDemuxer, GetTrackDemuxer)
        .WillByDefault([this](TrackType aType, uint32_t aTrackNumber) {
          EXPECT_EQ(aTrackNumber, 0u);
          if (!mDemuxerThread) {
            mDemuxerThread = do_QueryObject(AbstractThread::GetCurrent());
          }
          if (aType == TrackType::kAudioTrack) {
            EXPECT_TRUE(mAudioTrackDemuxer);
            return do_AddRef(mAudioTrackDemuxer);
          }
          EXPECT_EQ(aType, TrackType::kVideoTrack);
          return do_AddRef(mTrackDemuxer);
        });

    mPdm = new MockDecoderModule();
  }

  void TearDown() override { FinishShutdown(); }

  void SetVideoMediaTime(const TimeUnit& aMediaTime) {
    ON_CALL(*mTrackDemuxer, GetInfo).WillByDefault([aMediaTime]() {
      auto extended = MakeMediaContainerType(kTestVideoMime).value();
      UniquePtr<TrackInfo> info =
          CreateTrackInfoWithMIMETypeAndContainerTypeExtraParameters(
              extended.Type().AsString(), extended);
      info->mMediaTime = aMediaTime;
      return info;
    });
  }

  void AddAudioTrack(uint32_t aRate = 44100, uint32_t aChannels = 2) {
    mAudioTrackDemuxer = new MockMediaTrackDemuxer(kTestAudioMime);
    ON_CALL(*mAudioTrackDemuxer, GetInfo).WillByDefault([aRate, aChannels]() {
      auto extended = MakeMediaContainerType(kTestAudioMime).value();
      UniquePtr<TrackInfo> info =
          CreateTrackInfoWithMIMETypeAndContainerTypeExtraParameters(
              extended.Type().AsString(), extended);
      info->GetAsAudioInfo()->mRate = aRate;
      info->GetAsAudioInfo()->mChannels = aChannels;
      return info;
    });
  }

  void InitReader() {
    mOwner = std::make_unique<MockMediaDecoderOwner>();
    RefPtr container = new VideoFrameContainer(
        mOwner.get(),
        MakeAndAddRef<ImageContainer>(ImageUsageType::VideoFrameContainer,
#ifdef MOZ_WIDGET_ANDROID
                                      // Work around bug 1922144
                                      ImageContainer::SYNCHRONOUS
#else
                                      ImageContainer::ASYNCHRONOUS
#endif
                                      ));
    MediaFormatReaderInit init;
    init.mVideoFrameContainer = container;
    mFrameStats = new FrameStatistics();
    init.mFrameStats = mFrameStats;
    mReader = new MediaFormatReader(init, mDataDemuxer);
    mProxy = new ReaderProxy(AbstractThread::MainThread(), mReader);
    EXPECT_NS_SUCCEEDED(mReader->Init());
  }

  void FinishShutdown() {
    if (mShutdownComplete) {
      return;
    }
    WaitForResolve(mProxy->Shutdown());
    mShutdownComplete = true;
  }

  // Wait enough for the MediaFormatReader to process at least aCount demuxer or
  // decoder operations, if pending.
  void WaitForReaderOperations(int aCount) {
    // AwaitIdle() ensures that no tasks are pending and any task for another
    // thread is already in the other thread's queue, only if dispatch across
    // threads is not via tail dispatch.  Tail dispatch is not used because
    // the demuxer and decoder threads do not support tail dispatch, even
    // though the MediaFormatReader task queue supports tail dispatch.
    // https://searchfox.org/mozilla-central/rev/126697140e711e04a9d95edae537541c3bde89cc/xpcom/threads/AbstractThread.cpp#285-289
    MOZ_ASSERT(!mDemuxerThread->SupportsTailDispatch());
    MOZ_ASSERT(!mDecoderThread->SupportsTailDispatch());
    // Check that the reader thread has dispatched the first request to
    // the demuxer or decoder thread.
    mReader->OwnerThread()->AwaitIdle();
    for (int i = 0; i < aCount; ++i) {
      mDemuxerThread->AwaitIdle();
      mDecoderThread->AwaitIdle();
      mReader->OwnerThread()->AwaitIdle();
    }
  }

  // Expect a video decoder whose priming drain after the internal seek fails
  // with a non-fatal error, driving the decode-error recovery skip.
  void ExpectDecoderWithFailingPrimingDrain() {
    EXPECT_CALL(*mPdm, CreateVideoDecoder)
        .Times(testing::AnyNumber())
        .WillRepeatedly([this](const CreateDecoderParams& aParams) {
          mVideoDecoder = new MockVideoDataDecoder(aParams);
          InSequence s;
          EXPECT_CALL(*mVideoDecoder, Drain).WillOnce([this] {
            return mVideoDecoder->DummyMediaDataDecoder::Drain();
          });
          EXPECT_CALL(*mVideoDecoder, Drain).Times(1);
          EXPECT_CALL(*mVideoDecoder, Drain).WillOnce([] {
            return DecodePromise::CreateAndReject(
                NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER, __func__);
          });
          // The mock decoder holds decoded frames back until this many are
          // queued, modelling decode-reorder (DPB) latency, so decoded output
          // emerges through the drain path this test drives rather than
          // directly from Decode(). The value only needs to exceed the number
          // of samples fed before the first drain; 8 matches the sibling
          // internal-seek test.
          mVideoDecoder->SetLatencyFrameCount(8);
          return do_AddRef(mVideoDecoder);
        });
  }

  // Expect the demux sequence that walks the reader to an internal seek that
  // re-primes the decoder. A couple of samples are decoded first so the reader
  // is unambiguously past its first-frame state (mFirstFrameTime cleared); that
  // is what makes the later non-fatal decode error take the skip-to-keyframe
  // recovery path rather than an internal seek. A single decoded frame would
  // clear that state too; two keeps this structurally identical to the sibling
  // WaitingForDemuxAfterInternalSeek test.
  void ExpectDemuxReachingInternalSeekPriming() {
    InSequence s;
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).Times(2).WillRepeatedly([this] {
      const TimeUnit time(mDecodedSampleCount, 30);
      ++mDecodedSampleCount;
      return ResolveOneSample(MakeRawSample(time, TimeUnit(1, 30)));
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([] {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
    EXPECT_CALL(*mTrackDemuxer, Seek).WillOnce([this](const TimeUnit&) {
      EXPECT_NS_SUCCEEDED(mReader->OwnerThread()->Dispatch(
          NewRunnableMethod("NotifyDataArrived", mReader.get(),
                            &MediaFormatReader::NotifyDataArrived)));
      return SeekPromise::CreateAndResolve(TimeUnit::Zero(), __func__);
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([] {
      return ResolveOneSample(MakeRawSample(TimeUnit(0, 30), TimeUnit(1, 30)));
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillRepeatedly([] {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
  }

  // Read metadata, decode the first two frames, and consume up to the first
  // WAITING_FOR_DATA rejection that begins the internal-seek re-priming.
  void ReachInternalSeekPriming() {
    (void)WaitForResolve(mProxy->ReadMetadata());
    for (int i = 0; i < 2; ++i) {
      (void)WaitForResolve(mProxy->RequestVideoData(TimeUnit(), false));
    }
    MediaResult result =
        WaitForReject(mProxy->RequestVideoData(TimeUnit(), false));
    EXPECT_EQ(result.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
  }

  RefPtr<MockMediaDataDemuxer> mDataDemuxer;
  RefPtr<MockMediaTrackDemuxer> mTrackDemuxer;
  RefPtr<MockMediaTrackDemuxer> mAudioTrackDemuxer;
  RefPtr<MockDecoderModule> mPdm;
  std::unique_ptr<MockMediaDecoderOwner> mOwner;
  RefPtr<FrameStatistics> mFrameStats;
  RefPtr<MediaFormatReader> mReader;
  RefPtr<ReaderProxy> mProxy;
  bool mShutdownComplete = false;
  // Thread scheduling provides ordering for thread initializations before
  // their first read.
  RefPtr<TaskQueue> mDemuxerThread;
  RefPtr<TaskQueue> mDecoderThread;
  RefPtr<MockVideoDataDecoder> mVideoDecoder;
  int mDecodedSampleCount = 0;
};

// MP4 edit lists use a positive media time to map decoder pre-roll at the
// matching negative offset to currentTime 0.
const TimeUnit kEditListFrameDuration = TimeUnit::FromMicroseconds(41667);

TEST_F(TestMediaFormatReader, PositiveMediaTimeIgnoresNegativePreroll) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-6375000),
                                          kEditListFrameDuration));
  });

  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());
}

TEST_F(TestMediaFormatReader, PositiveMediaTimeClipsNegativeBufferedRange) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-6375000),
                                          kEditListFrameDuration));
  });
  ON_CALL(*mTrackDemuxer, GetBuffered).WillByDefault([]() {
    return TimeIntervals(TimeInterval(TimeUnit::FromMicroseconds(-6375000),
                                      TimeUnit::FromSeconds(1)));
  });

  Mirror<TimeIntervals> buffered(AbstractThread::MainThread(), TimeIntervals(),
                                 "TestMediaFormatReader::buffered");
  buffered.Connect(mReader->CanonicalBuffered());

  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());
  SpinEventLoopUntil("buffered range updated"_ns,
                     [&]() { return !buffered.Ref().IsEmpty(); });
  EXPECT_EQ(buffered.Ref().GetStart(), TimeUnit::Zero());
  EXPECT_EQ(buffered.Ref().GetEnd(), TimeUnit::FromSeconds(1));

  buffered.DisconnectIfConnected();
  mReader->OwnerThread()->AwaitIdle();
}

TEST_F(TestMediaFormatReader, PositiveMediaTimeDropsNegativeVideoPreroll) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));
  {
    InSequence s;
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      return ResolveOneSample(
          MakeRawSample(TimeUnit::FromMicroseconds(-6375000),
                        kEditListFrameDuration, /* aKeyframe */ true));
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-41667),
                                            kEditListFrameDuration));
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      return ResolveOneSample(
          MakeRawSample(TimeUnit::Zero(), kEditListFrameDuration));
    });
  }

  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });
  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  RefPtr<VideoData> clippedVideoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(clippedVideoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(clippedVideoData->mDuration, TimeUnit::Zero());

  RefPtr<VideoData> videoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(videoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(videoData->mDuration, kEditListFrameDuration);
  EXPECT_EQ(videoData->GetEndTime(), kEditListFrameDuration);
}

TEST_F(TestMediaFormatReader, PositiveMediaTimePreservesZeroDurationAtStart) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::Zero(), TimeUnit::Zero(),
                                          /* aKeyframe */ true));
  });
  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });

  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  RefPtr<VideoData> videoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(videoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(videoData->mDuration, TimeUnit::Zero());
}

TEST_F(TestMediaFormatReader, LongVideoPrerollReturnsFirstVisibleFrame) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  // Keep this above ThreadSanitizer's 128-lock limit to catch per-frame
  // promise chaining.
  static constexpr int32_t kPrerollSampleCount = 130;
  SetVideoMediaTime(TimeUnit(kPrerollSampleCount, 30));

  int32_t sampleIndex = 0;
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples)
      .Times(kPrerollSampleCount + 1)
      .WillRepeatedly([&sampleIndex]() {
        const int32_t sampleOffset = sampleIndex - kPrerollSampleCount;
        const TimeUnit sampleTime(sampleOffset, 30);
        const bool keyframe = sampleIndex == 0;
        ++sampleIndex;
        return ResolveOneSample(
            MakeRawSample(sampleTime, TimeUnit(1, 30), keyframe));
      });

  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });
  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  RefPtr<VideoData> clippedVideoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(clippedVideoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(clippedVideoData->mDuration, TimeUnit::Zero());

  RefPtr<VideoData> videoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(videoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(videoData->mDuration, TimeUnit(1, 30));
}

TEST_F(TestMediaFormatReader, PositiveMediaTimeClipsVideoSpanningZero) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-20000),
                                          TimeUnit::FromMicroseconds(40000),
                                          /* aKeyframe */ true));
  });

  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });
  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  RefPtr<VideoData> videoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(videoData->mTime, TimeUnit::Zero());
  EXPECT_EQ(videoData->mDuration, TimeUnit::FromMicroseconds(20000));
  EXPECT_EQ(videoData->GetEndTime(), TimeUnit::FromMicroseconds(20000));
}

TEST_F(TestMediaFormatReader, NegativeOverallStartPreservesVisibleVideo) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  AddAudioTrack();
  SetVideoMediaTime(TimeUnit::FromSeconds(1));

  EXPECT_CALL(*mAudioTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-1000000),
                                          TimeUnit::FromMicroseconds(100000)));
  });
  EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-500000),
                                          TimeUnit::FromMicroseconds(100000),
                                          /* aKeyframe */ true));
  });
  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });

  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::FromMicroseconds(-1000000));

  RefPtr<VideoData> videoData =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(videoData->mTime, TimeUnit::FromMicroseconds(500000));
  EXPECT_EQ(videoData->mDuration, TimeUnit::FromMicroseconds(100000));
}

TEST_F(TestMediaFormatReader, PositiveVideoMediaTimeDoesNotDelayAudio) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  AddAudioTrack();
  SetVideoMediaTime(TimeUnit::FromMicroseconds(6375000));

  ON_CALL(*mAudioTrackDemuxer, MockGetSamples).WillByDefault([]() {
    return ResolveOneSample(
        MakeRawSample(TimeUnit::Zero(), TimeUnit::FromMicroseconds(23220)));
  });
  ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
    return ResolveOneSample(MakeRawSample(TimeUnit::FromMicroseconds(-6375000),
                                          kEditListFrameDuration,
                                          /* aKeyframe */ true));
  });

  EXPECT_CALL(*mPdm, CreateAudioDecoder)
      .WillOnce([](const CreateDecoderParams& aParams) {
        RefPtr<MediaDataDecoder> decoder = new DummyMediaDataDecoder(
            std::make_unique<BlankAudioDataCreator>(
                aParams.AudioConfig().mChannels, aParams.AudioConfig().mRate),
            "MockAudioDataDecoder"_ns, aParams);
        return decoder.forget();
      });
  MetadataHolder metadata = WaitForResolve(mProxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  RefPtr<AudioData> audioData = WaitForResolve(mProxy->RequestAudioData());
  EXPECT_EQ(audioData->mTime, TimeUnit::Zero());
}

TEST_F(TestMediaFormatReader, WaitingForDemuxAfterInternalSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  RefPtr<MockVideoDataDecoder> decoder;
  MozPromiseHolder<DecodePromise> drainPromise;
  int sampleCount = 0;
  EXPECT_CALL(*mPdm, CreateVideoDecoder)
      .WillOnce([&](const CreateDecoderParams& aParams) {
        decoder = new MockVideoDataDecoder(aParams);
        InSequence s;
        // The first drain requires two calls: one to fetch the frames...
        EXPECT_CALL(*decoder, Drain).WillOnce([&] {
          MOZ_ASSERT(!mDecoderThread);
          mDecoderThread = do_QueryObject(AbstractThread::GetCurrent());
          return decoder->DummyMediaDataDecoder::Drain();
        });
        // ... and a second to confirm that no more frames are remaining.
        EXPECT_CALL(*decoder, Drain).Times(1);
        // Delay responding to the second drain request until testing is done.
        EXPECT_CALL(*decoder, Drain).WillOnce([&] {
          return drainPromise.Ensure(__func__);
        });
        decoder->SetLatencyFrameCount(8);
        return do_AddRef(decoder);
      });

  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;

    EXPECT_CALL(*mTrackDemuxer, MockGetSamples)
        .Times(2)
        .WillRepeatedly([&sampleCount]() {
          const TimeUnit time(sampleCount, 30);
          ++sampleCount;
          return ResolveOneSample(MakeRawSample(time, TimeUnit(1, 30)));
        });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
    EXPECT_CALL(*mTrackDemuxer, Seek).WillOnce([&](const TimeUnit& aTime) {
      // Reset mWaitingForDataStartTime so that OnDemuxFailed() calls
      // RequestDrain().
      EXPECT_NS_SUCCEEDED(mReader->OwnerThread()->Dispatch(
          NewRunnableMethod("NotifyDataArrived", mReader.get(),
                            &MediaFormatReader::NotifyDataArrived)));
      return SeekPromise::CreateAndResolve(TimeUnit::Zero(), __func__);
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      // Time is zero after the seek.
      return ResolveOneSample(MakeRawSample(TimeUnit(0, 30), TimeUnit(1, 30)));
    });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillOnce([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
    EXPECT_CALL(checkpoint, Call(StrEq("Internal seek waiting for data")));

    EXPECT_CALL(*mTrackDemuxer, MockGetSamples).WillRepeatedly([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
  }

  // ReadMetadata() to init demuxer.
  (void)WaitForResolve(mProxy->ReadMetadata());
  // Two samples are provided by the demuxer, but the third demux request is
  // rejected.  The first drain provides two decoded samples.
  for (int i = 0; i < 2; ++i) {
    (void)WaitForResolve(mProxy->RequestVideoData(TimeUnit(), false));
  }
  // A third sample is not available.
  MediaResult result =
      WaitForReject(mProxy->RequestVideoData(TimeUnit(), false));
  EXPECT_EQ(result.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
  // The first drain is complete.  Wait for the internal seek to begin
  // re-priming the decoder, for NotifyDataArrived to be processed by the
  // demuxer, for a successful demux, for a decode, and for a failed demux.
  // Demux failure triggers a drain.  This drain is not beneficial or
  // necessary because no samples are available for the current playback
  // position, but MediaFormatReader repeats the drain process because of the
  // NotifyDataArrived triggered by the mock Seek().
  WaitForReaderOperations(5);

  checkpoint.Call("Internal seek waiting for data");
  MOZ_ASSERT(!drainPromise.IsEmpty());
  // Request more data to check that this does not clear the status of the
  // in-progress drain, as in step 5 of
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1941164#c6
  // At the time of writing, without bug 1941164, MediaFormatReader does not
  // reject this promise until the drain completes.  However, the promise
  // could sensibly be rejected earlier because the failed demux has indicated
  // that video data is not available for the current playback position.
  (void)mProxy->RequestVideoData(TimeUnit(), false);
  // Trigger another Update() to check that another drain does not start.
  EXPECT_NS_SUCCEEDED(mReader->OwnerThread()->Dispatch(
      NewRunnableMethod("NotifyDataArrived", mReader.get(),
                        &MediaFormatReader::NotifyDataArrived)));
  // Wait for NotifyDataArrived to be processed by the demuxer and for another
  // demux request to complete.
  WaitForReaderOperations(2);
  // Clean up.
  FinishShutdown();
  drainPromise.Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);
}

// A skip-to-next-keyframe started by the decode-error recovery path has no
// pending video promise, so a later frame request that also wants to skip must
// not start a second skip on the still-live skip request.
TEST_F(TestMediaFormatReader, VideoSkipDoesNotReenterAcrossErrorRecovery) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  // Set up a reader whose decode-error recovery skips to the next keyframe. A
  // keyframe ahead of position zero means a plain frame request does not skip
  // while the recovery path and an explicit next-keyframe request do; each
  // started skip is held in flight and counted; the priming drain fails so
  // recovery is reached.
  MozPromiseHolder<MediaTrackDemuxer::SkipAccessPointPromise> skipPromise;
  Atomic<int> skipCount{0};
  ON_CALL(*mTrackDemuxer, GetNextRandomAccessPoint)
      .WillByDefault([](TimeUnit* aTime) {
        *aTime = TimeUnit(10, 30);
        return NS_OK;
      });
  ON_CALL(*mTrackDemuxer, SkipToNextRandomAccessPoint)
      .WillByDefault([&](const TimeUnit&) {
        ++skipCount;
        return skipPromise.Ensure(__func__);
      });
  ExpectDecoderWithFailingPrimingDrain();
  ExpectDemuxReachingInternalSeekPriming();

  // Drive the reader until the failing priming drain has started a recovery
  // skip that has no pending frame request.
  ReachInternalSeekPriming();
  SpinEventLoopUntil("recovery skip started"_ns,
                     [&]() { return skipCount >= 1; });

  // A frame request that also wants to skip must not start a second skip while
  // the recovery skip is still in flight.
  RefPtr pending = mProxy->RequestVideoData(TimeUnit(), true);
  mReader->OwnerThread()->AwaitIdle();
  EXPECT_EQ(static_cast<int>(skipCount), 1);

  // Complete the in-flight skip on the demuxer thread, where the skip promise
  // is created and would be resolved in production, so the deferred request is
  // serviced without touching the promise holder across threads.
  EXPECT_NS_SUCCEEDED(
      mDemuxerThread->Dispatch(NS_NewRunnableFunction("CompleteVideoSkip", [&] {
        skipPromise.Reject(MediaTrackDemuxer::SkipFailureHolder(
                               NS_ERROR_DOM_MEDIA_CANCELED, 0),
                           __func__);
      })));
  MediaResult pendingResult = WaitForReject(pending);
  EXPECT_EQ(pendingResult.Code(), NS_ERROR_DOM_MEDIA_CANCELED);
}

// Wake the main thread, so that SpinEventLoopUntil() re-checks a condition set
// on another thread.
static void WakeMainThread() {
  NS_DispatchToMainThread(NS_NewRunnableFunction("WakeMainThread", [] {}));
}

// Audio and video tracks whose demuxer seeks stay pending until the test
// completes them, for exercising seeks that outlive a partial ResetDecode().
class StaleSeekTest : public TestMediaFormatReader {
 protected:
  void SetUp() override {
    SetUpReaderDependencies();
    AddAudioTrack();
    InitReader();
    ON_CALL(*mDataDemuxer, IsSeekable).WillByDefault(Return(true));
    ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
      return ResolveOneSample(MakeRawSample(TimeUnit::Zero(), TimeUnit(1, 30),
                                            /* aKeyframe */ true));
    });
    ON_CALL(*mAudioTrackDemuxer, MockGetSamples).WillByDefault([]() {
      return ResolveOneSample(
          MakeRawSample(TimeUnit::Zero(), TimeUnit::FromMicroseconds(23220)));
    });
    // Each track expects a single seek; a further one is counted and fails.
    EXPECT_CALL(*mTrackDemuxer, Seek)
        .WillOnce([this](const TimeUnit&) {
          RefPtr p = mVideoSeekPromise.Ensure(__func__);
          ++mVideoSeekCount;
          WakeMainThread();
          return p;
        })
        .WillRepeatedly([this](const TimeUnit&) { return UnexpectedSeek(); });
    EXPECT_CALL(*mAudioTrackDemuxer, Seek)
        .WillOnce([this](const TimeUnit&) {
          RefPtr p = mAudioSeekPromise.Ensure(__func__);
          ++mAudioSeekCount;
          WakeMainThread();
          return p;
        })
        .WillRepeatedly([this](const TimeUnit&) { return UnexpectedSeek(); });
  }

  void TearDown() override {
    // Settle seeks that a failed test left pending, on the thread that owns
    // them, so that shutdown reports the failure rather than asserting.
    if (mDemuxerThread) {
      EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(
          NS_NewRunnableFunction("RejectPendingSeeks", [this] {
            mVideoSeekPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
            mAudioSeekPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
          })));
      mDemuxerThread->AwaitIdle();
    }
    TestMediaFormatReader::TearDown();
    // Run callbacks that shutdown settled while the fixture is alive.
    NS_ProcessPendingEvents(nullptr);
    EXPECT_EQ(static_cast<int>(mUnexpectedSeekCount), 0);
  }

  RefPtr<SeekPromise> UnexpectedSeek() {
    ++mUnexpectedSeekCount;
    return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  }

  RefPtr<MediaFormatReader::SeekPromise> SeekTrack(SeekTarget::Track aTrack) {
    return mProxy->Seek(
        SeekTarget(TimeUnit::Zero(), SeekTarget::Accurate, aTrack));
  }

  // Resolve a pending demuxer seek on the demuxer thread, and wait for the
  // reader to process the result.
  void CompleteDemuxerSeek(MozPromiseHolder<SeekPromise>& aHolder) {
    EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(NS_NewRunnableFunction(
        "CompleteSeek", [&] { aHolder.Resolve(TimeUnit::Zero(), __func__); })));
    mDemuxerThread->AwaitIdle();
    mReader->OwnerThread()->AwaitIdle();
    // Count any demuxer seek that the reader starts in response.
    mDemuxerThread->AwaitIdle();
  }

  MozPromiseHolder<SeekPromise> mVideoSeekPromise;
  MozPromiseHolder<SeekPromise> mAudioSeekPromise;
  Atomic<int> mVideoSeekCount{0};
  Atomic<int> mAudioSeekCount{0};
  Atomic<int> mUnexpectedSeekCount{0};
  Maybe<MediaFormatReader::SeekPromise::ResolveOrRejectValue> mVideoOnlyResult;
};

// Audio and video tracks with a video keyframe ahead of the playback position,
// and a skip to it that stays pending until CompleteSkip().
class VideoSkipTest : public TestMediaFormatReader {
 protected:
  void SetUp() override {
    SetUpReaderDependencies();
    AddAudioTrack();
    InitReader();
    ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([this]() {
      ++mVideoDemuxCount;
      return ResolveOneSample(
          MakeRawSample(kKeyframeTime, TimeUnit(1, 30), /* aKeyframe */ true));
    });
    ON_CALL(*mAudioTrackDemuxer, MockGetSamples).WillByDefault([]() {
      return ResolveOneSample(
          MakeRawSample(TimeUnit::Zero(), TimeUnit::FromMicroseconds(23220)));
    });
    ON_CALL(*mTrackDemuxer, GetNextRandomAccessPoint)
        .WillByDefault([](TimeUnit* aTime) {
          *aTime = kKeyframeTime;
          return NS_OK;
        });
    ON_CALL(*mTrackDemuxer, SkipToNextRandomAccessPoint)
        .WillByDefault([this](const TimeUnit&) {
          RefPtr p = mSkipPromise.Ensure(__func__);
          mSkipStarted = true;
          WakeMainThread();
          return p;
        });
    ON_CALL(*mPdm, CreateVideoDecoder)
        .WillByDefault([](const CreateDecoderParams& aParams) {
          return do_AddRef(new MockVideoDataDecoder(aParams));
        });
  }

  void TearDown() override {
    // Settle a skip that a failed test left pending, on the thread that owns
    // it, so that shutdown reports the failure rather than asserting.
    if (mDemuxerThread) {
      EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(
          NS_NewRunnableFunction("RejectPendingSkip", [this] {
            mSkipPromise.RejectIfExists(MediaTrackDemuxer::SkipFailureHolder(
                                            NS_ERROR_DOM_MEDIA_CANCELED, 0),
                                        __func__);
          })));
      mDemuxerThread->AwaitIdle();
    }
    TestMediaFormatReader::TearDown();
  }

  // Complete the pending skip, and wait for the reader to demux the video
  // track again if it resumes.
  void CompleteSkip() {
    EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(NS_NewRunnableFunction(
        "CompleteVideoSkip", [this] { mSkipPromise.Resolve(0, __func__); })));
    for (int i = 0; i < 2; ++i) {
      mDemuxerThread->AwaitIdle();
      mReader->OwnerThread()->AwaitIdle();
    }
  }

  static constexpr TimeUnit kKeyframeTime = TimeUnit(10, 30);
  MozPromiseHolder<MediaTrackDemuxer::SkipAccessPointPromise> mSkipPromise;
  Atomic<bool> mSkipStarted{false};
  Atomic<int> mVideoDemuxCount{0};
};

// Resetting only the audio track, as seamless looping does, must not orphan a
// video request whose skip to the next keyframe is in flight.
TEST_F(VideoSkipTest, AudioResetDoesNotOrphanVideoSkip) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  (void)WaitForResolve(mProxy->ReadMetadata());
  RefPtr video = mProxy->RequestVideoData(TimeUnit::Zero(),
                                          /* aRequestNextKeyFrame */ true);
  SpinEventLoopUntil("video skip started"_ns,
                     [&]() { return mSkipStarted.load(); });
  const int demuxCountBeforeSkip = mVideoDemuxCount;

  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  CompleteSkip();
  ASSERT_GT(static_cast<int>(mVideoDemuxCount), demuxCountBeforeSkip);
  EXPECT_EQ(WaitForResolve(video)->mTime, kKeyframeTime);
}

// Seamless looping resets and seeks only the audio track. A video-only seek
// still in flight at that point must not drive the new audio-only seek when its
// demuxer seek completes.
TEST_F(StaleSeekTest, StaleVideoSeekDoesNotRestartAudioOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  (void)WaitForResolve(mProxy->ReadMetadata());

  RefPtr videoOnlySeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });

  // Rewind only the audio track, as LoopingDecodingState does.
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  RefPtr audioOnlySeek = SeekTrack(SeekTarget::Track::AudioOnly);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });
  (void)WaitForReject(videoOnlySeek);

  CompleteDemuxerSeek(mVideoSeekPromise);
  EXPECT_EQ(static_cast<int>(mUnexpectedSeekCount), 0);

  CompleteDemuxerSeek(mAudioSeekPromise);
  EXPECT_EQ(WaitForResolve(audioOnlySeek), TimeUnit::Zero());
}

// The video-only seek that resumes video decoding resets and seeks only the
// video track. An audio-only seek still in flight at that point must not
// resolve the new video-only seek before the video track has been sought.
TEST_F(StaleSeekTest, StaleAudioSeekDoesNotResolveVideoOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  (void)WaitForResolve(mProxy->ReadMetadata());

  RefPtr audioOnlySeek = SeekTrack(SeekTarget::Track::AudioOnly);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });

  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  SeekTrack(SeekTarget::Track::VideoOnly)
      ->Then(
          AbstractThread::MainThread(), __func__,
          [this](
              MediaFormatReader::SeekPromise::ResolveOrRejectValue&& aValue) {
            mVideoOnlyResult.emplace(std::move(aValue));
          });
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });
  (void)WaitForReject(audioOnlySeek);

  CompleteDemuxerSeek(mAudioSeekPromise);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_TRUE(mVideoOnlyResult.isNothing());

  CompleteDemuxerSeek(mVideoSeekPromise);
  SpinEventLoopUntil("video-only seek settled"_ns,
                     [&]() { return mVideoOnlyResult.isSome(); });
  ASSERT_TRUE(mVideoOnlyResult->IsResolve());
  EXPECT_EQ(mVideoOnlyResult->ResolveValue(), TimeUnit::Zero());
}

// Once the audio-only seek has completed, the video track must be usable even
// though the video-only seek it replaced has not completed on the demuxer.
TEST_F(StaleSeekTest, VideoRequestAfterAudioOnlySeekWithStaleVideoSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  ON_CALL(*mPdm, CreateVideoDecoder)
      .WillByDefault([](const CreateDecoderParams& aParams) {
        return do_AddRef(new MockVideoDataDecoder(aParams));
      });
  (void)WaitForResolve(mProxy->ReadMetadata());

  RefPtr videoOnlySeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  RefPtr audioOnlySeek = SeekTrack(SeekTarget::Track::AudioOnly);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });
  (void)WaitForReject(videoOnlySeek);
  CompleteDemuxerSeek(mAudioSeekPromise);
  EXPECT_EQ(WaitForResolve(audioOnlySeek), TimeUnit::Zero());

  RefPtr<VideoData> video =
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(video->mTime, TimeUnit::Zero());

  CompleteDemuxerSeek(mVideoSeekPromise);
}

// A decoder whose Flush(), Decode() and Shutdown() the test hooks.
class HookedDecoder final : public DummyMediaDataDecoder {
 public:
  using FlushHook = std::function<RefPtr<FlushPromise>()>;
  using DecodeHook = std::function<RefPtr<DecodePromise>()>;
  using ShutdownHook = std::function<void()>;

  HookedDecoder(UniquePtr<DummyDataCreator>&& aCreator,
                const CreateDecoderParams& aParams, FlushHook aFlushHook,
                DecodeHook aDecodeHook, ShutdownHook aShutdownHook)
      : DummyMediaDataDecoder(std::move(aCreator), "HookedDecoder"_ns, aParams),
        mFlushHook(std::move(aFlushHook)),
        mDecodeHook(std::move(aDecodeHook)),
        mShutdownHook(std::move(aShutdownHook)) {}

  RefPtr<ShutdownPromise> Shutdown() override {
    mShutdownHook();
    return DummyMediaDataDecoder::Shutdown();
  }

  RefPtr<FlushPromise> Flush() override {
    if (RefPtr<FlushPromise> p = mFlushHook()) {
      return p;
    }
    return DummyMediaDataDecoder::Flush();
  }

  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override {
    if (RefPtr<DecodePromise> p = mDecodeHook()) {
      return p;
    }
    return DummyMediaDataDecoder::Decode(aSample);
  }

 private:
  const FlushHook mFlushHook;
  const DecodeHook mDecodeHook;
  const ShutdownHook mShutdownHook;
};

// Audio and video tracks whose decoders have decoded their first frame, for
// exercising decoder errors that complete after a ResetDecode().
class FlushErrorAfterResetTest : public TestMediaFormatReader {
 protected:
  // The error a remote decoder reports when its process crashes, which is not
  // fatal.
  static constexpr nsresult kDecoderCrash =
      NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR;

  void SetUp() override {
    SetUpReaderDependencies();
    AddAudioTrack();
    InitReader();
    ON_CALL(*mDataDemuxer, IsSeekable).WillByDefault(Return(true));
    ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
      return ResolveOneSample(MakeRawSample(TimeUnit::Zero(), TimeUnit(1, 30),
                                            /* aKeyframe */ true));
    });
    ON_CALL(*mAudioTrackDemuxer, MockGetSamples).WillByDefault([]() {
      return ResolveOneSample(
          MakeRawSample(TimeUnit::Zero(), TimeUnit::FromMicroseconds(23220)));
    });
    ON_CALL(*mTrackDemuxer, Seek).WillByDefault([this](const TimeUnit& aTime) {
      ++mVideoSeekCount;
      RefPtr p = mVideoSeekHook(aTime);
      WakeMainThread();
      return p;
    });
    ON_CALL(*mAudioTrackDemuxer, Seek)
        .WillByDefault([this](const TimeUnit& aTime) {
          ++mAudioSeekCount;
          RefPtr p = mAudioSeekHook(aTime);
          WakeMainThread();
          return p;
        });
    ON_CALL(*mPdm, CreateVideoDecoder)
        .WillByDefault([this](const CreateDecoderParams& aParams) {
          return do_AddRef(new HookedDecoder(
              std::make_unique<BlankVideoDataCreator>(
                  aParams.VideoConfig().mDisplay.width,
                  aParams.VideoConfig().mDisplay.height,
                  aParams.mImageContainer),
              aParams,
              [this]() { return HoldFlushIfRequested(TrackType::kVideoTrack); },
              [this]() {
                return FailDecodeIfRequested(TrackType::kVideoTrack);
              },
              [this]() { ++mDecoderShutdownCount; }));
        });
    ON_CALL(*mPdm, CreateAudioDecoder)
        .WillByDefault([this](const CreateDecoderParams& aParams) {
          return do_AddRef(new HookedDecoder(
              std::make_unique<BlankAudioDataCreator>(
                  aParams.AudioConfig().mChannels, aParams.AudioConfig().mRate),
              aParams,
              [this]() { return HoldFlushIfRequested(TrackType::kAudioTrack); },
              [this]() {
                return FailDecodeIfRequested(TrackType::kAudioTrack);
              },
              [this]() { ++mDecoderShutdownCount; }));
        });
  }

  void TearDown() override {
    // Settle seeks and a flush that a failed test left pending, on the
    // threads that own them, so that shutdown reports the failure rather than
    // asserting.
    if (mDemuxerThread) {
      EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(
          NS_NewRunnableFunction("RejectPendingSeeks", [this] {
            mHeldVideoSeek.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                          __func__);
            mHeldAudioSeek.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                          __func__);
          })));
      mDemuxerThread->AwaitIdle();
    }
    if (mDecoderThread) {
      EXPECT_NS_SUCCEEDED(mDecoderThread->Dispatch(
          NS_NewRunnableFunction("RejectPendingFlush", [this] {
            mHeldFlush.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
          })));
      mDecoderThread->AwaitIdle();
    }
    TestMediaFormatReader::TearDown();
    // Run callbacks that shutdown settled while the fixture is alive.
    NS_ProcessPendingEvents(nullptr);
  }

  RefPtr<MediaDataDecoder::FlushPromise> HoldFlushIfRequested(
      TrackType aTrack) {
    if (!mHeldFlushTrack.compareExchange(aTrack, TrackType::kUndefinedTrack)) {
      return nullptr;
    }
    mDecoderThread = do_QueryObject(AbstractThread::GetCurrent());
    MOZ_RELEASE_ASSERT(mDecoderThread);
    RefPtr p = mHeldFlush.Ensure(__func__);
    mFlushHeld = true;
    WakeMainThread();
    return p;
  }

  RefPtr<MediaDataDecoder::DecodePromise> FailDecodeIfRequested(
      TrackType aTrack) {
    if (!mFailDecodeTrack.compareExchange(aTrack, TrackType::kUndefinedTrack)) {
      return nullptr;
    }
    mDecoderThread = do_QueryObject(AbstractThread::GetCurrent());
    MOZ_RELEASE_ASSERT(mDecoderThread);
    mDecodeFailed = true;
    WakeMainThread();
    return MediaDataDecoder::DecodePromise::CreateAndReject(kDecoderCrash,
                                                            __func__);
  }

  // Read metadata and decode one frame of each track, so that both decoders
  // exist and have cleared their first-frame state.
  void DecodeFirstFrames() {
    (void)WaitForResolve(mProxy->ReadMetadata());
    (void)WaitForResolve(mProxy->RequestAudioData());
    (void)WaitForResolve(mProxy->RequestVideoData(TimeUnit(), false));
  }

  // Reset both tracks and seek them, as AccurateSeekingState::DoSeek() does,
  // holding the flush that the reset starts on aTrack's decoder.
  RefPtr<MediaFormatReader::SeekPromise> ResetAndSeekAllTracks(
      TrackType aTrack) {
    mHeldFlushTrack = aTrack;
    mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack,
                                                    TrackType::kVideoTrack));
    RefPtr p = SeekTrack(SeekTarget::Track::All);
    SpinEventLoopUntil("flush held"_ns, [&]() { return bool(mFlushHeld); });
    return p;
  }

  // Rewind one track as seamless looping does, holding its decoder's flush.
  RefPtr<MediaFormatReader::SeekPromise> ResetAndSeekTrackHoldingFlush(
      TrackType aTrack) {
    mHeldFlushTrack = aTrack;
    mProxy->ResetDecode(MediaFormatReader::TrackSet(aTrack));
    RefPtr p = SeekTrack(aTrack == TrackType::kAudioTrack
                             ? SeekTarget::Track::AudioOnly
                             : SeekTarget::Track::VideoOnly);
    SpinEventLoopUntil("flush held"_ns, [&]() { return bool(mFlushHeld); });
    return p;
  }

  RefPtr<MediaFormatReader::SeekPromise> SeekTrack(SeekTarget::Track aTrack) {
    return mProxy->Seek(
        SeekTarget(TimeUnit::Zero(), SeekTarget::Accurate, aTrack));
  }

  // Also waits for any demuxer seek that the reader starts in response.
  void NotifyDataArrived() {
    EXPECT_NS_SUCCEEDED(mReader->OwnerThread()->Dispatch(
        NewRunnableMethod("NotifyDataArrived", mReader.get(),
                          &MediaFormatReader::NotifyDataArrived)));
    mReader->OwnerThread()->AwaitIdle();
    mDemuxerThread->AwaitIdle();
    mReader->OwnerThread()->AwaitIdle();
    mDemuxerThread->AwaitIdle();
  }

  // Models AccurateSeekingState::OnSeekRejected()'s reset before a retry. No
  // test here drives MediaDecoderStateMachine, so none catch it regressing.
  RefPtr<MediaFormatReader::SeekPromise> ResetAndRetrySeekAllTracks() {
    mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack,
                                                    TrackType::kVideoTrack));
    return SeekTrack(SeekTarget::Track::All);
  }

  // Wait for the reader to handle a decoder error. The error reaches the
  // reader through the decoder's own task queue, and its handling can go back
  // to the demuxer and decoder.
  void AwaitErrorHandled() {
    for (int i = 0; i < 3; ++i) {
      mDecoderThread->AwaitIdle();
      mReader->OwnerThread()->AwaitIdle();
      mDemuxerThread->AwaitIdle();
      mReader->OwnerThread()->AwaitIdle();
    }
  }

  void FailHeldFlush() {
    EXPECT_NS_SUCCEEDED(mDecoderThread->Dispatch(NS_NewRunnableFunction(
        "FailFlush", [this] { mHeldFlush.Reject(kDecoderCrash, __func__); })));
    AwaitErrorHandled();
  }

  // Make demuxer seeks pending until CompleteHeldSeek().
  void HoldDemuxerSeeks() {
    mVideoSeekHook = [this](const TimeUnit&) {
      return mHeldVideoSeek.Ensure(__func__);
    };
    mAudioSeekHook = [this](const TimeUnit&) {
      return mHeldAudioSeek.Ensure(__func__);
    };
  }

  void CompleteHeldSeek(MozPromiseHolder<SeekPromise>& aHolder) {
    EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(NS_NewRunnableFunction(
        "CompleteSeek",
        [&aHolder] { aHolder.Resolve(TimeUnit::Zero(), __func__); })));
  }

  void RejectHeldSeek(MozPromiseHolder<SeekPromise>& aHolder, nsresult aError) {
    EXPECT_NS_SUCCEEDED(mDemuxerThread->Dispatch(NS_NewRunnableFunction(
        "RejectSeek",
        [&aHolder, aError] { aHolder.Reject(aError, __func__); })));
  }

  // Make the first demuxer seek of a track wait for data, and later ones
  // succeed.
  static std::function<RefPtr<SeekPromise>(const TimeUnit&)>
  FirstSeekWaitsForData(Atomic<int>& aSeekCount) {
    return [&aSeekCount](const TimeUnit& aTime) {
      if (aSeekCount == 1) {
        return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                                            __func__);
      }
      return SeekPromise::CreateAndResolve(aTime, __func__);
    };
  }

  // Seek number aWaitingSeek (from 1, 0 for none) waits for data and aHeldSeek
  // stays pending in aHolder; others succeed.
  static std::function<RefPtr<SeekPromise>(const TimeUnit&)> SeekSequence(
      Atomic<int>& aSeekCount, int aWaitingSeek, int aHeldSeek,
      MozPromiseHolder<SeekPromise>& aHolder) {
    return [&aSeekCount, aWaitingSeek, aHeldSeek,
            &aHolder](const TimeUnit& aTime) -> RefPtr<SeekPromise> {
      if (aSeekCount == aWaitingSeek) {
        return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                                            __func__);
      }
      if (aSeekCount == aHeldSeek) {
        return aHolder.Ensure(__func__);
      }
      return SeekPromise::CreateAndResolve(aTime, __func__);
    };
  }

  std::function<RefPtr<SeekPromise>(const TimeUnit&)> mVideoSeekHook =
      [](const TimeUnit& aTime) {
        return SeekPromise::CreateAndResolve(aTime, __func__);
      };
  std::function<RefPtr<SeekPromise>(const TimeUnit&)> mAudioSeekHook =
      [](const TimeUnit& aTime) {
        return SeekPromise::CreateAndResolve(aTime, __func__);
      };
  Atomic<int> mVideoSeekCount{0};
  Atomic<int> mAudioSeekCount{0};
  MozPromiseHolder<SeekPromise> mHeldVideoSeek;
  MozPromiseHolder<SeekPromise> mHeldAudioSeek;
  Atomic<TrackType> mHeldFlushTrack{TrackType::kUndefinedTrack};
  Atomic<bool> mFlushHeld{false};
  MozPromiseHolder<MediaDataDecoder::FlushPromise> mHeldFlush;
  Atomic<TrackType> mFailDecodeTrack{TrackType::kUndefinedTrack};
  Atomic<bool> mDecodeFailed{false};
  Atomic<int> mDecoderShutdownCount{0};
  Maybe<MediaFormatReader::AudioDataPromise::ResolveOrRejectValue> mAudioResult;
};

// A recovery seek of a track that a single-track seek does not cover must
// survive the reset that abandons that seek.
TEST_F(FlushErrorAfterResetTest,
       AudioRecoverySeekSurvivesAbandonedVideoOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = SeekSequence(mAudioSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 2, mHeldAudioSeek);
  mVideoSeekHook = SeekSequence(mVideoSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 1, mHeldVideoSeek);
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kAudioTrack)),
      TimeUnit::Zero());
  mProxy->RequestAudioData()->Then(
      AbstractThread::MainThread(), __func__,
      [this](
          MediaFormatReader::AudioDataPromise::ResolveOrRejectValue&& aValue) {
        mAudioResult.emplace(std::move(aValue));
      });
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  RefPtr abandonedSeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });

  FailHeldFlush();
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  ASSERT_EQ(static_cast<int>(mAudioSeekCount), 2);

  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  (void)WaitForReject(abandonedSeek);
  EXPECT_EQ(WaitForResolve(SeekTrack(SeekTarget::Track::VideoOnly)),
            TimeUnit::Zero());

  // Bounded wait, so that a cancelled recovery seek fails rather than hangs.
  CompleteHeldSeek(mHeldAudioSeek);
  for (int i = 0; i < 3; ++i) {
    AwaitErrorHandled();
  }
  NS_ProcessPendingEvents(nullptr);
  ASSERT_TRUE(mAudioResult.isSome());
  ASSERT_TRUE(mAudioResult->IsResolve());
  EXPECT_EQ(mAudioResult->ResolveValue()->mTime, TimeUnit::Zero());
}

// An audio decoder error that completes while the video demuxer seek of an
// all-tracks seek is in flight must not start an audio internal seek, which
// the seek would then collide with when it moves on to the audio track.
TEST_F(FlushErrorAfterResetTest, AudioDoesNotInternalSeekDuringVideoSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  HoldDemuxerSeeks();
  DecodeFirstFrames();

  RefPtr seek = ResetAndSeekAllTracks(TrackType::kAudioTrack);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });

  FailHeldFlush();
  // The crash makes the reader replace the decoder.
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 0);

  CompleteHeldSeek(mHeldVideoSeek);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });
  CompleteHeldSeek(mHeldAudioSeek);
  EXPECT_EQ(WaitForResolve(seek), TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 1);
}

// The video track of an all-tracks seek is still being sought after its own
// demuxer seek completes, so a video decoder error that completes while the
// audio demuxer seek is in flight must not start a video internal seek.
TEST_F(FlushErrorAfterResetTest, VideoDoesNotInternalSeekDuringAudioSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = [this](const TimeUnit&) {
    return mHeldAudioSeek.Ensure(__func__);
  };
  DecodeFirstFrames();

  RefPtr seek = ResetAndSeekAllTracks(TrackType::kVideoTrack);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 1);

  FailHeldFlush();
  // The crash makes the reader replace the decoder.
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 1);

  CompleteHeldSeek(mHeldAudioSeek);
  EXPECT_EQ(WaitForResolve(seek), TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 1);
}

// While an all-tracks seek waits for data, the reader may recover from an
// audio decoder error by seeking the track itself. This models
// AccurateSeekingState resetting the sought tracks again before it retries, so
// the retry must succeed while that recovery seek is still in flight.
TEST_F(FlushErrorAfterResetTest, AudioErrorWhileSeekWaitsDoesNotBreakRetry) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mVideoSeekHook = FirstSeekWaitsForData(mVideoSeekCount);
  mAudioSeekHook = SeekSequence(mAudioSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 1, mHeldAudioSeek);
  DecodeFirstFrames();

  SeekRejectValue rejection =
      WaitForReject(ResetAndSeekAllTracks(TrackType::kAudioTrack));
  EXPECT_EQ(rejection.mError.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  FailHeldFlush();
  // The crash makes the reader replace the decoder and seek the track back.
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 1);

  EXPECT_EQ(WaitForResolve(ResetAndRetrySeekAllTracks()), TimeUnit::Zero());
  CompleteHeldSeek(mHeldAudioSeek);
  mDemuxerThread->AwaitIdle();
}

// As above, for a video decoder error.
TEST_F(FlushErrorAfterResetTest, VideoErrorWhileSeekWaitsDoesNotBreakRetry) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mVideoSeekHook = SeekSequence(mVideoSeekCount, /* aWaitingSeek */ 1,
                                /* aHeldSeek */ 2, mHeldVideoSeek);
  DecodeFirstFrames();

  SeekRejectValue rejection =
      WaitForReject(ResetAndSeekAllTracks(TrackType::kVideoTrack));
  EXPECT_EQ(rejection.mError.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  FailHeldFlush();
  // The crash makes the reader replace the decoder and seek the track back.
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 2);

  EXPECT_EQ(WaitForResolve(ResetAndRetrySeekAllTracks()), TimeUnit::Zero());
  CompleteHeldSeek(mHeldVideoSeek);
  mDemuxerThread->AwaitIdle();
}

// As above, for a video decoder error while the audio half of the seek waits
// for data, after the video half has completed.
TEST_F(FlushErrorAfterResetTest,
       VideoErrorWhileAudioSeekWaitsDoesNotBreakRetry) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = FirstSeekWaitsForData(mAudioSeekCount);
  mVideoSeekHook = SeekSequence(mVideoSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 2, mHeldVideoSeek);
  DecodeFirstFrames();

  SeekRejectValue rejection =
      WaitForReject(ResetAndSeekAllTracks(TrackType::kVideoTrack));
  EXPECT_EQ(rejection.mError.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  FailHeldFlush();
  // The crash makes the reader replace the decoder and seek the track back.
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 2);

  EXPECT_EQ(WaitForResolve(ResetAndRetrySeekAllTracks()), TimeUnit::Zero());
  CompleteHeldSeek(mHeldVideoSeek);
  mDemuxerThread->AwaitIdle();
}

// Once its seek has completed, a track recovers from a decoder error by
// seeking back to its first frame.
TEST_F(FlushErrorAfterResetTest, AudioRecoversAfterSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  DecodeFirstFrames();
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack,
                                                  TrackType::kVideoTrack));
  EXPECT_EQ(WaitForResolve(SeekTrack(SeekTarget::Track::All)),
            TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 1);

  mFailDecodeTrack = TrackType::kAudioTrack;
  EXPECT_EQ(WaitForResolve(mProxy->RequestAudioData())->mTime,
            TimeUnit::Zero());
  EXPECT_TRUE(mDecodeFailed);
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 2);
}

// A track that is read after its seek was rejected while waiting for data, as
// seamless looping does, has no seek pending, so it recovers from a decoder
// error by seeking back to its first frame.
TEST_F(FlushErrorAfterResetTest, AudioRecoversWhenReadWithoutSeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = FirstSeekWaitsForData(mAudioSeekCount);
  DecodeFirstFrames();
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  (void)WaitForReject(SeekTrack(SeekTarget::Track::AudioOnly));
  // Data arrives, so the looping state requests audio again.
  NotifyDataArrived();

  mFailDecodeTrack = TrackType::kAudioTrack;
  mProxy->RequestAudioData()->Then(
      AbstractThread::MainThread(), __func__,
      [this](
          MediaFormatReader::AudioDataPromise::ResolveOrRejectValue&& aValue) {
        mAudioResult.emplace(std::move(aValue));
      });
  SpinEventLoopUntil("decode failed"_ns, [&]() {
    return mDecodeFailed.load() || mAudioResult.isSome();
  });
  ASSERT_TRUE(mDecodeFailed);
  AwaitErrorHandled();
  ASSERT_EQ(static_cast<int>(mAudioSeekCount), 2);

  SpinEventLoopUntil("audio request settled"_ns,
                     [&]() { return mAudioResult.isSome(); });
  ASSERT_TRUE(mAudioResult->IsResolve());
  EXPECT_EQ(mAudioResult->ResolveValue()->mTime, TimeUnit::Zero());
}

// The track a single-track seek does not cover recovers from a decoder error
// by seeking itself: here audio during a video-only seek.
TEST_F(FlushErrorAfterResetTest, AudioRecoversDuringVideoOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mVideoSeekHook = [this](const TimeUnit&) {
    return mHeldVideoSeek.Ensure(__func__);
  };
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kAudioTrack)),
      TimeUnit::Zero());
  RefPtr audio = mProxy->RequestAudioData();
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  RefPtr videoOnlySeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });
  // New data must not make the seek handle the audio track.
  NotifyDataArrived();

  FailHeldFlush();
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 2);
  EXPECT_EQ(WaitForResolve(audio)->mTime, TimeUnit::Zero());

  CompleteHeldSeek(mHeldVideoSeek);
  EXPECT_EQ(WaitForResolve(videoOnlySeek), TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 1);
}

// As above, for video during an audio-only seek.
TEST_F(FlushErrorAfterResetTest, VideoRecoversDuringAudioOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = [this](const TimeUnit&) {
    return mHeldAudioSeek.Ensure(__func__);
  };
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kVideoTrack)),
      TimeUnit::Zero());
  RefPtr video = mProxy->RequestVideoData(TimeUnit::Zero(), false);
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  RefPtr audioOnlySeek = SeekTrack(SeekTarget::Track::AudioOnly);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });
  // New data must not make the seek handle the video track.
  NotifyDataArrived();

  FailHeldFlush();
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 2);
  EXPECT_EQ(WaitForResolve(video)->mTime, TimeUnit::Zero());

  CompleteHeldSeek(mHeldAudioSeek);
  EXPECT_EQ(WaitForResolve(audioOnlySeek), TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 1);
}

// A single-track seek can wait for data while the other track recovers.
TEST_F(FlushErrorAfterResetTest, VideoOnlySeekWaitsForDataWhileAudioRecovers) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = SeekSequence(mAudioSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 2, mHeldAudioSeek);
  mVideoSeekHook = SeekSequence(mVideoSeekCount, /* aWaitingSeek */ 0,
                                /* aHeldSeek */ 1, mHeldVideoSeek);
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kAudioTrack)),
      TimeUnit::Zero());
  RefPtr audio = mProxy->RequestAudioData();
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  RefPtr videoOnlySeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });

  FailHeldFlush();
  EXPECT_EQ(static_cast<int>(mDecoderShutdownCount), 1);
  ASSERT_EQ(static_cast<int>(mAudioSeekCount), 2);

  RejectHeldSeek(mHeldVideoSeek, NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
  SeekRejectValue rejection = WaitForReject(videoOnlySeek);
  EXPECT_EQ(rejection.mError.Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  EXPECT_EQ(WaitForResolve(SeekTrack(SeekTarget::Track::VideoOnly)),
            TimeUnit::Zero());
  CompleteHeldSeek(mHeldAudioSeek);
  EXPECT_EQ(WaitForResolve(audio)->mTime, TimeUnit::Zero());
}

// The uncovered track's recovery seek waits for data and is retried when data
// arrives, as in bug 2076235: here video during an audio-only seek.
TEST_F(FlushErrorAfterResetTest,
       VideoRecoveryWaitingForDataDuringAudioOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mAudioSeekHook = [this](const TimeUnit&) {
    return mHeldAudioSeek.Ensure(__func__);
  };
  mVideoSeekHook = SeekSequence(mVideoSeekCount, /* aWaitingSeek */ 2,
                                /* aHeldSeek */ 0, mHeldVideoSeek);
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kVideoTrack)),
      TimeUnit::Zero());
  RefPtr video = mProxy->RequestVideoData(TimeUnit::Zero(), false);
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kAudioTrack));
  RefPtr audioOnlySeek = SeekTrack(SeekTarget::Track::AudioOnly);
  SpinEventLoopUntil("audio seek started"_ns,
                     [&]() { return mAudioSeekCount >= 1; });

  FailHeldFlush();
  ASSERT_EQ(static_cast<int>(mVideoSeekCount), 2);
  EXPECT_EQ(WaitForReject(video).Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  NotifyDataArrived();
  ASSERT_EQ(static_cast<int>(mVideoSeekCount), 3);

  CompleteHeldSeek(mHeldAudioSeek);
  EXPECT_EQ(WaitForResolve(audioOnlySeek), TimeUnit::Zero());
  EXPECT_EQ(
      WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false))->mTime,
      TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mAudioSeekCount), 1);
}

// As above, for audio during a video-only seek.
TEST_F(FlushErrorAfterResetTest,
       AudioRecoveryWaitingForDataDuringVideoOnlySeek) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  mVideoSeekHook = [this](const TimeUnit&) {
    return mHeldVideoSeek.Ensure(__func__);
  };
  mAudioSeekHook = SeekSequence(mAudioSeekCount, /* aWaitingSeek */ 2,
                                /* aHeldSeek */ 0, mHeldAudioSeek);
  DecodeFirstFrames();
  EXPECT_EQ(
      WaitForResolve(ResetAndSeekTrackHoldingFlush(TrackType::kAudioTrack)),
      TimeUnit::Zero());
  RefPtr audio = mProxy->RequestAudioData();
  mProxy->ResetDecode(MediaFormatReader::TrackSet(TrackType::kVideoTrack));
  RefPtr videoOnlySeek = SeekTrack(SeekTarget::Track::VideoOnly);
  SpinEventLoopUntil("video seek started"_ns,
                     [&]() { return mVideoSeekCount >= 1; });

  FailHeldFlush();
  ASSERT_EQ(static_cast<int>(mAudioSeekCount), 2);
  EXPECT_EQ(WaitForReject(audio).Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);

  NotifyDataArrived();
  ASSERT_EQ(static_cast<int>(mAudioSeekCount), 3);

  CompleteHeldSeek(mHeldVideoSeek);
  EXPECT_EQ(WaitForResolve(videoOnlySeek), TimeUnit::Zero());
  EXPECT_EQ(WaitForResolve(mProxy->RequestAudioData())->mTime,
            TimeUnit::Zero());
  EXPECT_EQ(static_cast<int>(mVideoSeekCount), 1);
}

class RecreateOnNthDecodeDecoder final : public MockVideoDataDecoder {
 public:
  RecreateOnNthDecodeDecoder(const CreateDecoderParams& aParams,
                             uint32_t aDecodeNumber)
      : MockVideoDataDecoder(aParams), mDecodeNumber(aDecodeNumber) {
    MOZ_ASSERT(aDecodeNumber > 0);
  }

  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override {
    if (++mDecodeCount == mDecodeNumber) {
      return DecodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER,
                                            __func__);
    }
    return DummyMediaDataDecoder::Decode(aSample);
  }

 private:
  ~RecreateOnNthDecodeDecoder() override = default;
  const uint32_t mDecodeNumber;
  uint32_t mDecodeCount = 0;
};

class VideoRateTest : public TestMediaFormatReader {
 protected:
  enum class SampleType { Keyframe, DeltaFrame };

  static constexpr uint32_t kFirstStreamID = 1;
  static constexpr uint32_t kSecondStreamID = 2;

  static double ExpectedRate(std::initializer_list<TimeUnit> aDurations) {
    MOZ_ASSERT(aDurations.size() > 0);
    double totalDuration = 0.0;
    for (const auto& duration : aDurations) {
      MOZ_ASSERT(duration.IsValid() && duration.IsPositive() &&
                 !duration.IsInfinite());
      totalDuration += duration.ToSeconds();
    }
    return static_cast<double>(aDurations.size()) / totalDuration;
  }

  struct SampleSpec {
    TimeUnit mTime;
    TimeUnit mDuration;
    Maybe<uint32_t> mStreamID;
    SampleType mType;
    Maybe<gfx::IntSize> mVideoSize;
  };

  void SetUp() override {
    SetUpReaderDependencies();
    mTrackInfo = mTrackDemuxer->GetInfo();
    ASSERT_TRUE(mTrackInfo);
    ON_CALL(*mDataDemuxer, IsSeekable).WillByDefault(testing::Return(true));
    ON_CALL(*mTrackDemuxer, Seek).WillByDefault([](const TimeUnit& aTime) {
      return SeekPromise::CreateAndResolve(aTime, __func__);
    });
  }

  void TearDown() override { ShutdownReader(); }

  void ShutdownReader() {
    if (!mProxy) {
      return;
    }
    WaitForResolve(mProxy->Shutdown());
    mProxy = nullptr;
    mReader = nullptr;
  }

  void AddSample(const TimeUnit& aDuration, uint32_t aStreamID,
                 SampleType aType = SampleType::Keyframe) {
    AddSampleAt(mNextSampleTime, aDuration, Some(aStreamID), aType);
  }

  void AddSample(const TimeUnit& aDuration, uint32_t aStreamID,
                 const gfx::IntSize& aVideoSize,
                 SampleType aType = SampleType::Keyframe) {
    AddSampleAt(mNextSampleTime, aDuration, Some(aStreamID), aType,
                Some(aVideoSize));
  }

  void AddSampleAt(const TimeUnit& aTime, const TimeUnit& aDuration,
                   Maybe<uint32_t> aStreamID,
                   SampleType aType = SampleType::Keyframe,
                   Maybe<gfx::IntSize> aVideoSize = Nothing()) {
    mSamples.AppendElement(
        SampleSpec{aTime, aDuration, aStreamID, aType, aVideoSize});
    mNextSampleTime = aTime + aDuration;
  }

  void AddSampleWithoutTrackInfo(const TimeUnit& aDuration) {
    AddSampleAt(mNextSampleTime, aDuration, Nothing());
  }

  void RecreateFirstDecoderOnDecode(uint32_t aDecodeNumber) {
    MOZ_ASSERT(aDecodeNumber > 0);
    mFirstDecoderFailureAtDecode = Some(aDecodeNumber);
  }

  void InitReader() {
    EXPECT_CALL(*mPdm, CreateVideoDecoder)
        .Times(testing::AnyNumber())
        .WillRepeatedly([this](const CreateDecoderParams& aParams) {
          mDecoderRates.AppendElement(aParams.mRate.mValue);
          mDecoderConfigs.AppendElement(aParams.VideoConfig());
          RefPtr<MockVideoDataDecoder> decoder;
          if (mFirstDecoderFailureAtDecode.isSome() &&
              mDecoderRates.Length() == 1) {
            decoder = new RecreateOnNthDecodeDecoder(
                aParams, mFirstDecoderFailureAtDecode.ref());
          } else {
            decoder = new MockVideoDataDecoder(aParams);
          }
          EXPECT_CALL(*decoder, Drain).Times(testing::AnyNumber());
          EXPECT_CALL(*decoder, IsHardwareAccelerated)
              .Times(testing::AnyNumber());
          return do_AddRef(decoder);
        });
    EXPECT_CALL(*mTrackDemuxer, MockGetSamples)
        .Times(testing::AnyNumber())
        .WillRepeatedly([this]() {
          if (mNextSample == mSamples.Length()) {
            return SamplesPromise::CreateAndReject(
                NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
          }
          const SampleSpec& spec = mSamples[mNextSample++];
          RefPtr sample = new MediaRawData;
          sample->mKeyframe = spec.mType == SampleType::Keyframe;
          sample->mTime = spec.mTime;
          sample->mTimecode = TimeUnit::Zero();
          sample->mDuration = spec.mDuration;
          if (spec.mStreamID.isSome()) {
            if (spec.mVideoSize.isSome()) {
              VideoInfo info(*mTrackInfo->GetAsVideoInfo());
              info.mDisplay = info.mImage = spec.mVideoSize.ref();
              sample->mTrackInfo =
                  new TrackInfoSharedPtr(info, spec.mStreamID.ref());
            } else {
              sample->mTrackInfo =
                  new TrackInfoSharedPtr(*mTrackInfo, spec.mStreamID.ref());
            }
          }
          RefPtr<SamplesHolder> samples = new SamplesHolder;
          samples->AppendSample(std::move(sample));
          return SamplesPromise::CreateAndResolve(samples, __func__);
        });

    TestMediaFormatReader::InitReader();
    (void)WaitForResolve(mProxy->ReadMetadata());
  }

  void ReadFrame() {
    (void)WaitForResolve(mProxy->RequestVideoData(TimeUnit(), false));
  }

  void ReadToEnd() {
    bool reachedEndOfStream = false;
    for (size_t i = 0; i < mSamples.Length() + 10; ++i) {
      auto result = WaitFor(mProxy->RequestVideoData(TimeUnit(), false));
      if (result.isErr()) {
        EXPECT_EQ(result.unwrapErr().Code(), NS_ERROR_DOM_MEDIA_END_OF_STREAM);
        reachedEndOfStream = true;
        break;
      }
    }
    EXPECT_TRUE(reachedEndOfStream);
    EXPECT_EQ(mNextSample, mSamples.Length());
  }

  void SetNullDecode(bool aEnabled) {
    mProxy->SetVideoBlankDecode(aEnabled);
    mReader->OwnerThread()->AwaitIdle();
  }

  void SeekToStart() {
    (void)WaitForResolve(mProxy->Seek(SeekTarget(
        TimeUnit::Zero(), SeekTarget::Accurate, SeekTarget::Track::VideoOnly)));
  }

  double VideoRate() {
    dom::MediaFormatReaderDebugInfo info;
    (void)WaitForResolve(mReader->RequestDebugInfo(info));
    return info.mVideoRate;
  }

  UniquePtr<TrackInfo> mTrackInfo;
  nsTArray<SampleSpec> mSamples;
  nsTArray<float> mDecoderRates;
  nsTArray<VideoInfo> mDecoderConfigs;
  TimeUnit mNextSampleTime = TimeUnit::Zero();
  size_t mNextSample = 0;
  Maybe<uint32_t> mFirstDecoderFailureAtDecode;
};

TEST_F(VideoRateTest, InitialDecoderAndFinalDiagnosticShareEstimator) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto shortDuration = TimeUnit(10, 1000);
  const auto longDuration = TimeUnit(100, 1000);
  AddSample(shortDuration, kFirstStreamID);
  AddSample(shortDuration, kFirstStreamID);
  AddSample(longDuration, kFirstStreamID);
  InitReader();
  ReadToEnd();

  const double initialRate = ExpectedRate({shortDuration});
  const double finalRate =
      ExpectedRate({shortDuration, shortDuration, longDuration});
  ASSERT_EQ(mDecoderRates.Length(), 1U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(initialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), finalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, StreamChangeResetsRateAndRecreatesDecoder) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto firstSparseDuration = TimeUnit(90, 1000);
  const auto secondDuration = TimeUnit(100, 1000);
  const auto secondSparseDuration = TimeUnit(200, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSampleWithoutTrackInfo(firstSparseDuration);
  AddSample(secondDuration, kSecondStreamID);
  AddSampleWithoutTrackInfo(secondSparseDuration);
  InitReader();
  ReadToEnd();

  const double firstRate = ExpectedRate({firstDuration});
  const double secondInitialRate = ExpectedRate({secondDuration});
  const double secondFinalRate =
      ExpectedRate({secondDuration, secondSparseDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(firstRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(secondInitialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), secondFinalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, AnonymousSamplesRemainInOneEstimatorEpoch) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto secondDuration = TimeUnit(90, 1000);
  AddSampleWithoutTrackInfo(firstDuration);
  AddSampleWithoutTrackInfo(secondDuration);
  InitReader();
  ReadToEnd();

  const double initialRate = ExpectedRate({firstDuration});
  const double finalRate = ExpectedRate({firstDuration, secondDuration});
  ASSERT_EQ(mDecoderRates.Length(), 1U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(initialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), finalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, FirstExplicitStreamIDResetsAnonymousRate) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  constexpr uint32_t explicitStreamID = 0;
  const auto anonymousDuration = TimeUnit(10, 1000);
  const auto explicitDuration = TimeUnit(100, 1000);
  const auto sparseDuration = TimeUnit(200, 1000);
  AddSampleWithoutTrackInfo(anonymousDuration);
  AddSample(explicitDuration, explicitStreamID);
  AddSampleWithoutTrackInfo(sparseDuration);
  InitReader();
  ReadToEnd();

  const double anonymousRate = ExpectedRate({anonymousDuration});
  const double explicitInitialRate = ExpectedRate({explicitDuration});
  const double explicitFinalRate =
      ExpectedRate({explicitDuration, sparseDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(anonymousRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(explicitInitialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), explicitFinalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, SameStreamDecoderRecreationUsesCurrentRate) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto secondDuration = TimeUnit(90, 1000);
  const auto thirdDuration = TimeUnit(100, 1000);
  const auto fourthDuration = TimeUnit(200, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSample(secondDuration, kFirstStreamID);
  AddSample(thirdDuration, kFirstStreamID);
  AddSample(fourthDuration, kFirstStreamID);
  InitReader();

  // Decode two frames with the initial decoder, then consume the third through
  // a null decoder to force recreation without changing the stream ID.
  ReadFrame();
  ReadFrame();
  SetNullDecode(true);
  ReadFrame();
  // Resume real decoding on the fourth frame so the recreated decoder receives
  // the current rate accumulated for the same stream.
  SetNullDecode(false);
  ReadFrame();
  ReadToEnd();

  const double initialRate = ExpectedRate({firstDuration});
  const double recreatedRate = ExpectedRate(
      {firstDuration, secondDuration, thirdDuration, fourthDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(initialRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(recreatedRate));
  EXPECT_DOUBLE_EQ(VideoRate(), recreatedRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, DecodeErrorRecreationUsesCurrentRate) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto secondDuration = TimeUnit(90, 1000);
  const auto thirdDuration = TimeUnit(100, 1000);
  const auto fourthDuration = TimeUnit(200, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSample(secondDuration, kFirstStreamID);
  AddSample(thirdDuration, kFirstStreamID);
  AddSample(fourthDuration, kFirstStreamID);
  RecreateFirstDecoderOnDecode(2);
  ON_CALL(*mTrackDemuxer, GetNextRandomAccessPoint)
      .WillByDefault([](TimeUnit* aTime) {
        *aTime = TimeUnit(100, 1000);
        return NS_OK;
      });
  EXPECT_CALL(*mTrackDemuxer, SkipToNextRandomAccessPoint)
      .WillOnce([](const TimeUnit&) {
        return MediaTrackDemuxer::SkipAccessPointPromise::CreateAndResolve(
            0, __func__);
      });
  InitReader();
  ReadToEnd();

  const double initialRate = ExpectedRate({firstDuration});
  const double recreatedRate =
      ExpectedRate({firstDuration, secondDuration, thirdDuration});
  const double finalRate = ExpectedRate(
      {firstDuration, secondDuration, thirdDuration, fourthDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(initialRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(recreatedRate));
  EXPECT_DOUBLE_EQ(VideoRate(), finalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, NonKeyframeStreamChangeCountsDemuxedSample) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstStreamDuration = TimeUnit(40, 1000);
  const auto nonKeyframeDuration = TimeUnit(100, 1000);
  const auto keyframeDuration = TimeUnit(20, 1000);
  AddSample(firstStreamDuration, kFirstStreamID);
  AddSample(nonKeyframeDuration, kSecondStreamID, SampleType::DeltaFrame);
  AddSample(keyframeDuration, kSecondStreamID);
  EXPECT_CALL(*mTrackDemuxer, Seek).WillOnce([](const TimeUnit& aTime) {
    return SeekPromise::CreateAndResolve(aTime, __func__);
  });
  InitReader();
  ReadToEnd();

  const double firstRate = ExpectedRate({firstStreamDuration});
  const double secondRate =
      ExpectedRate({nonKeyframeDuration, keyframeDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(firstRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(secondRate));
  EXPECT_DOUBLE_EQ(VideoRate(), secondRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, ZeroDurationSamplesAreIgnored) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto zeroDuration = TimeUnit::Zero();
  const auto firstValidDuration = TimeUnit(10, 1000);
  const auto secondValidDuration = TimeUnit(100, 1000);
  AddSample(zeroDuration, kFirstStreamID);
  AddSample(firstValidDuration, kFirstStreamID);
  AddSample(secondValidDuration, kFirstStreamID);
  InitReader();
  ReadToEnd();

  const double finalRate =
      ExpectedRate({firstValidDuration, secondValidDuration});
  ASSERT_EQ(mDecoderRates.Length(), 1U);
  EXPECT_EQ(mDecoderRates[0], 0.0f);
  EXPECT_DOUBLE_EQ(VideoRate(), finalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, SeekReplayCountsAsAnotherDemuxObservation) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto replayedDuration = TimeUnit(10, 1000);
  const auto finalDuration = TimeUnit(90, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSampleAt(TimeUnit::Zero(), replayedDuration, Some(kFirstStreamID));
  AddSample(finalDuration, kFirstStreamID);
  EXPECT_CALL(*mTrackDemuxer, Seek).WillOnce([](const TimeUnit& aTime) {
    return SeekPromise::CreateAndResolve(aTime, __func__);
  });
  InitReader();

  ReadFrame();
  SeekToStart();
  ReadToEnd();

  const double initialRate = ExpectedRate({firstDuration});
  const double finalRate =
      ExpectedRate({firstDuration, replayedDuration, finalDuration});
  ASSERT_EQ(mDecoderRates.Length(), 1U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(initialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), finalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, StreamAndConfigChangeUseNewRateAndConfig) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto* initialInfo = mTrackInfo->GetAsVideoInfo();
  ASSERT_TRUE(initialInfo);
  const auto initialSize = initialInfo->mDisplay;
  const gfx::IntSize newSize{320, 180};
  ASSERT_NE(initialSize, newSize);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto firstSparseDuration = TimeUnit(90, 1000);
  const auto secondDuration = TimeUnit(100, 1000);
  const auto secondSparseDuration = TimeUnit(200, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSample(firstSparseDuration, kFirstStreamID);
  AddSample(secondDuration, kSecondStreamID, newSize);
  AddSample(secondSparseDuration, kSecondStreamID, newSize);
  InitReader();
  ReadToEnd();

  const double firstRate = ExpectedRate({firstDuration});
  const double secondInitialRate = ExpectedRate({secondDuration});
  const double secondFinalRate =
      ExpectedRate({secondDuration, secondSparseDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  ASSERT_EQ(mDecoderConfigs.Length(), 2U);
  EXPECT_EQ(mDecoderConfigs[0].mDisplay, initialSize);
  EXPECT_EQ(mDecoderConfigs[1].mDisplay, newSize);
  EXPECT_EQ(mDecoderConfigs[1].mImage, newSize);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(firstRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(secondInitialRate));
  EXPECT_DOUBLE_EQ(VideoRate(), secondFinalRate);
  ShutdownReader();
}

TEST_F(VideoRateTest, StreamChangeWhileNullDecodingUsesNewRateOnResume) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  const auto firstDuration = TimeUnit(10, 1000);
  const auto firstSparseDuration = TimeUnit(90, 1000);
  const auto secondDuration = TimeUnit(100, 1000);
  const auto secondSparseDuration = TimeUnit(200, 1000);
  AddSample(firstDuration, kFirstStreamID);
  AddSample(firstSparseDuration, kFirstStreamID);
  AddSample(secondDuration, kSecondStreamID);
  AddSample(secondSparseDuration, kSecondStreamID);
  InitReader();

  // Decode the first frame with a real decoder, then consume the rest of the
  // first stream and the first frame of the second through a null decoder.
  ReadFrame();
  SetNullDecode(true);
  ReadFrame();
  ReadFrame();
  const double secondInitialRate = ExpectedRate({secondDuration});
  EXPECT_DOUBLE_EQ(VideoRate(), secondInitialRate);
  // Resume real decoding on the second frame of the new stream so the recreated
  // decoder receives the rate accumulated since the estimator reset.
  SetNullDecode(false);
  ReadFrame();
  ReadToEnd();

  const double firstRate = ExpectedRate({firstDuration});
  const double secondFinalRate =
      ExpectedRate({secondDuration, secondSparseDuration});
  ASSERT_EQ(mDecoderRates.Length(), 2U);
  EXPECT_FLOAT_EQ(mDecoderRates[0], static_cast<float>(firstRate));
  EXPECT_FLOAT_EQ(mDecoderRates[1], static_cast<float>(secondFinalRate));
  EXPECT_DOUBLE_EQ(VideoRate(), secondFinalRate);
  ShutdownReader();
}

// Shared setup for crypto subsample check tests. A single video track backed
// by a MockMediaDataDemuxer/MockMediaTrackDemuxer and a MockDecoderModule.
// Each test sets its MockGetSamples expectation, then calls InitReader().
class TestMFRCryptoSampleSizeCheck : public ::testing::Test {
 protected:
  static constexpr const char* kMaxDecodeErrorPref =
      "media.video-max-decode-error";

  RefPtr<MockMediaDataDemuxer> mDataDemuxer;
  RefPtr<MockMediaTrackDemuxer> mTrackDemuxer;
  RefPtr<MockDecoderModule> mPdm;
  std::unique_ptr<MockMediaDecoderOwner> mOwner;
  RefPtr<VideoFrameContainer> mContainer;
  RefPtr<MediaFormatReader> mReader;
  RefPtr<ReaderProxy> mProxy;
  Maybe<uint32_t> mMaxDecodeErrorUserValue;

  TestMFRCryptoSampleSizeCheck()
      : mDataDemuxer(new MockMediaDataDemuxer()),
        mTrackDemuxer(
            new MockMediaTrackDemuxer("video/x-test; width=640; height=360")),
        mPdm(new MockDecoderModule()),
        mOwner(std::make_unique<MockMediaDecoderOwner>()) {}

  void SetUp() override {
    if (Preferences::HasUserValue(kMaxDecodeErrorPref)) {
      mMaxDecodeErrorUserValue.emplace(
          Preferences::GetUint(kMaxDecodeErrorPref));
    }
    ASSERT_NS_SUCCEEDED(Preferences::SetUint(kMaxDecodeErrorPref, 0));

    ON_CALL(*mDataDemuxer, GetNumberTracks(TrackType::kVideoTrack))
        .WillByDefault(Return(1));
    ON_CALL(*mDataDemuxer, GetTrackDemuxer)
        .WillByDefault([this](TrackType, uint32_t) {
          return do_AddRef(mTrackDemuxer.get());
        });
    ON_CALL(*mPdm, CreateVideoDecoder)
        .WillByDefault([](const CreateDecoderParams& aParams) {
          return do_AddRef(new MockVideoDataDecoder(aParams));
        });
  }

  void TearDown() override {
    nsresult rv = mMaxDecodeErrorUserValue
                      ? Preferences::SetUint(kMaxDecodeErrorPref,
                                             *mMaxDecodeErrorUserValue)
                      : Preferences::ClearUser(kMaxDecodeErrorPref);
    EXPECT_NS_SUCCEEDED(rv);
  }

  void InitReader() {
    // VideoFrameContainer's constructor reads gfxVars on Android; initialize
    // them if the gtest harness hasn't done so.
    if (!gfx::gfxVars::IsInitialized()) {
      gfx::gfxVars::Initialize();
    }
    mContainer = new VideoFrameContainer(
        mOwner.get(),
        MakeAndAddRef<ImageContainer>(ImageUsageType::VideoFrameContainer,
#ifdef MOZ_WIDGET_ANDROID
                                      // Work around bug 1922144
                                      ImageContainer::SYNCHRONOUS
#else
                                      ImageContainer::ASYNCHRONOUS
#endif
                                      ));
    MediaFormatReaderInit init;
    init.mVideoFrameContainer = mContainer;
    mReader = new MediaFormatReader(init, mDataDemuxer);
    mProxy = new ReaderProxy(AbstractThread::MainThread(), mReader);
    EXPECT_NS_SUCCEEDED(mReader->Init());
  }
};

namespace {

// Returns a SamplesHolder containing one encrypted sample with the given
// data size and subsample arrays.
RefPtr<SamplesHolder> MakeCryptoSamplesHolder(size_t aDataSize, TimeUnit aTime,
                                              nsTArray<uint32_t> aPlainSizes,
                                              nsTArray<uint32_t> aEncSizes) {
  auto sample = MakeRefPtr<MediaRawData>();
  sample->mTime = aTime;
  auto writer = sample->CreateWriter();
  writer->mCrypto.mCryptoScheme = CryptoScheme::Cenc;
  writer->mCrypto.mPlainSizes = std::move(aPlainSizes);
  writer->mCrypto.mEncryptedSizes = std::move(aEncSizes);
  [[maybe_unused]] auto unused = writer->SetSize(aDataSize);
  RefPtr<SamplesHolder> holder = new SamplesHolder;
  holder->AppendSample(std::move(sample));
  return holder;
}

}  // namespace

// Verify that a sample where mPlainSizes and mEncryptedSizes have different
// lengths is rejected before reaching the decoder.
TEST_F(TestMFRCryptoSampleSizeCheck, CryptoSubsampleArrayLengthMismatch) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  // Configure before InitReader() -- start-time detection calls GetSamples().
  ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
    // mPlainSizes has 1 entry; mEncryptedSizes has 2 -- lengths don't match.
    return SamplesPromise::CreateAndResolve(
        MakeCryptoSamplesHolder(100, TimeUnit::Zero(), {50u}, {25u, 25u}),
        __func__);
  });
  InitReader();
  (void)WaitForResolve(mProxy->ReadMetadata());

  MediaResult result =
      WaitForReject(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(result.Code(), NS_ERROR_DOM_MEDIA_DECODE_ERR);

  WaitForResolve(mProxy->Shutdown());
}

// Verify that a sample where sum(mPlainSizes + mEncryptedSizes) != Size()
// is rejected before reaching the decoder.
TEST_F(TestMFRCryptoSampleSizeCheck, CryptoSubsampleTotalSizeMismatch) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
    // 50 + 30 = 80, but the buffer is 100 bytes.
    return SamplesPromise::CreateAndResolve(
        MakeCryptoSamplesHolder(100, TimeUnit::Zero(), {50u}, {30u}), __func__);
  });
  InitReader();
  (void)WaitForResolve(mProxy->ReadMetadata());

  MediaResult result =
      WaitForReject(mProxy->RequestVideoData(TimeUnit::Zero(), false));
  EXPECT_EQ(result.Code(), NS_ERROR_DOM_MEDIA_DECODE_ERR);

  WaitForResolve(mProxy->Shutdown());
}

// Verify that a sample with valid crypto subsamples (sum == Size()) passes
// the check and is decoded successfully.
TEST_F(TestMFRCryptoSampleSizeCheck, CryptoSubsampleValid) {
  PDMFactory::AutoForcePDM autoForcePDM(mPdm);
  ON_CALL(*mTrackDemuxer, MockGetSamples).WillByDefault([]() {
    // 50 + 50 = 100 == buffer size -- subsamples are valid.
    return SamplesPromise::CreateAndResolve(
        MakeCryptoSamplesHolder(100, TimeUnit::Zero(), {50u}, {50u}), __func__);
  });
  InitReader();
  (void)WaitForResolve(mProxy->ReadMetadata());

  // The sample should pass the check and be decoded without error.
  (void)WaitForResolve(mProxy->RequestVideoData(TimeUnit::Zero(), false));

  WaitForResolve(mProxy->Shutdown());
}
