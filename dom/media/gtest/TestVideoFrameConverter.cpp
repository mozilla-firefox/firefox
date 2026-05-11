/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <array>
#include <iterator>
#include <thread>

#include "MediaEventSource.h"
#include "VideoFrameConverter.h"
#include "VideoUtils.h"
#include "YUVBufferGenerator.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"
#include "mozilla/gtest/WaitFor.h"
#include "rtc_base/ref_counted_object.h"

using namespace mozilla;
using testing::Not;

class VideoFrameConverterTest;

class FrameListener : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FrameListener)

  explicit FrameListener(webrtc::VideoTrackSourceInterface* aSource)
      : mSource(aSource) {
    mSource->AddOrUpdateSink(this, {});
  }

  void OnFrame(const webrtc::VideoFrame& aVideoFrame) override {
    mVideoFrameConvertedEvent.Notify(aVideoFrame, TimeStamp::Now());
  }

  void OnDiscardedFrame() override {
    webrtc::VideoFrame frame(nullptr, webrtc::VideoRotation::kVideoRotation_0,
                             0);
    mVideoFrameConvertedEvent.Notify(frame, TimeStamp::Now());
  }

  void SetWants(const webrtc::VideoSinkWants& aWants) {
    mSource->AddOrUpdateSink(this, aWants);
  }

  MediaEventSource<webrtc::VideoFrame, TimeStamp>& VideoFrameConvertedEvent() {
    return mVideoFrameConvertedEvent;
  }

 private:
  ~FrameListener() { mSource->RemoveSink(this); }

  const RefPtr<webrtc::VideoTrackSourceInterface> mSource;
  MediaEventProducer<webrtc::VideoFrame, TimeStamp> mVideoFrameConvertedEvent;
};

class DebugVideoFrameConverter
    : public webrtc::RefCountedObject<
          VideoFrameConverterImpl<FrameDroppingPolicy::Disabled>> {
 public:
  explicit DebugVideoFrameConverter(
      const dom::RTCStatsTimestampMaker& aTimestampMaker)
      : webrtc::RefCountedObject<VideoFrameConverterImpl>(
            do_AddRef(GetMainThreadSerialEventTarget()), aTimestampMaker,
            /* aLockScaling= */ false) {}

  using VideoFrameConverterImpl::mLastFrameQueuedForProcessing;
  using VideoFrameConverterImpl::ProcessVideoFrame;
  using VideoFrameConverterImpl::QueueForProcessing;
  using VideoFrameConverterImpl::RegisterListener;
};

class VideoFrameConverterTest : public ::testing::Test {
 protected:
  const dom::RTCStatsTimestampMaker mTimestampMaker;
  RefPtr<DebugVideoFrameConverter> mConverter;
  RefPtr<FrameListener> mListener;

  VideoFrameConverterTest()
      : mTimestampMaker(dom::RTCStatsTimestampMaker::Create()),
        mConverter(MakeAndAddRef<DebugVideoFrameConverter>(mTimestampMaker)),
        mListener(MakeAndAddRef<FrameListener>(mConverter)) {
    mConverter->RegisterListener();
    mConverter->SetTrackingId({TrackingId::Source::Camera, 0});
  }

  void TearDown() override { mConverter->Shutdown(); }

  RefPtr<TakeNPromise<webrtc::VideoFrame, TimeStamp>> TakeNConvertedFrames(
      size_t aN) {
    return TakeN(mListener->VideoFrameConvertedEvent(), aN);
  }
};

static bool IsPlane(const uint8_t* aData, int aWidth, int aHeight, int aStride,
                    uint8_t aValue) {
  for (int i = 0; i < aHeight; ++i) {
    for (int j = 0; j < aWidth; ++j) {
      if (aData[i * aStride + j] != aValue) {
        return false;
      }
    }
  }
  return true;
}

MATCHER(IsFrameBlack,
        std::string(nsPrintfCString("%s all black pixels",
                                    negation ? "doesn't have" : "has")
                        .get())) {
  static_assert(
      std::is_same_v<webrtc::VideoFrame, std::decay_t<decltype(arg)>>);
  RefPtr<webrtc::I420BufferInterface> buffer =
      arg.video_frame_buffer()->ToI420().get();
  return IsPlane(buffer->DataY(), buffer->width(), buffer->height(),
                 buffer->StrideY(), 0x00) &&
         IsPlane(buffer->DataU(), buffer->ChromaWidth(), buffer->ChromaHeight(),
                 buffer->StrideU(), 0x80) &&
         IsPlane(buffer->DataV(), buffer->ChromaWidth(), buffer->ChromaHeight(),
                 buffer->StrideV(), 0x80);
}

static std::tuple</*multiples*/ int64_t, /*remainder*/ int64_t>
CalcMultiplesInMillis(TimeDuration aArg, TimeDuration aDenom) {
  int64_t denom = llround(aDenom.ToMilliseconds());
  int64_t arg = llround(aArg.ToMilliseconds());
  const auto multiples = arg / denom;
  const auto remainder = arg % denom;
  return {multiples, remainder};
}

MATCHER_P(
    IsDurationInMillisMultipleOf, aDenom,
    std::string(
        nsPrintfCString("%s a multiple of %sms", negation ? "isn't" : "is",
                        testing::PrintToString(aDenom.ToMilliseconds()).data())
            .get())) {
  using T = std::decay_t<decltype(arg)>;
  using U = std::decay_t<decltype(aDenom)>;
  static_assert(std::is_same_v<T, TimeDuration>);
  static_assert(std::is_same_v<U, TimeDuration>);
  auto [multiples, remainder] = CalcMultiplesInMillis(arg, aDenom);
  return multiples >= 0 && remainder == 0;
}

MATCHER_P(
    IsDurationInMillisPositiveMultipleOf, aDenom,
    std::string(
        nsPrintfCString("%s a positive non-zero multiple of %sms",
                        negation ? "isn't" : "is",
                        testing::PrintToString(aDenom.ToMilliseconds()).data())
            .get())) {
  using T = std::decay_t<decltype(arg)>;
  using U = std::decay_t<decltype(aDenom)>;
  static_assert(std::is_same_v<T, TimeDuration>);
  static_assert(std::is_same_v<U, TimeDuration>);
  auto [multiples, remainder] = CalcMultiplesInMillis(arg, aDenom);
  return multiples > 0 && remainder == 0;
}

VideoChunk GenerateChunk(int32_t aWidth, int32_t aHeight, TimeStamp aTime) {
  YUVBufferGenerator generator;
  generator.Init(gfx::IntSize(aWidth, aHeight));
  mozilla::VideoFrame f(generator.GenerateI420Image(),
                        gfx::IntSize(aWidth, aHeight));
  VideoChunk c;
  c.mFrame.TakeFrom(&f);
  c.mTimeStamp = aTime;
  c.mDuration = 0;
  return c;
}

TEST_F(VideoFrameConverterTest, BasicConversion) {
  auto framesPromise = TakeNConvertedFrames(1);
  TimeStamp now = TimeStamp::Now();
  VideoChunk chunk = GenerateChunk(640, 480, now);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(chunk, false);
  auto frames = WaitFor(framesPromise).unwrap();
  ASSERT_EQ(frames.size(), 1U);
  const auto& [frame, conversionTime] = frames[0];
  EXPECT_EQ(frame.width(), 640);
  EXPECT_EQ(frame.height(), 480);
  EXPECT_THAT(frame, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime - now, TimeDuration::FromMilliseconds(0));
}

TEST_F(VideoFrameConverterTest, BasicPacing) {
  auto framesPromise = TakeNConvertedFrames(1);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future = now + TimeDuration::FromMilliseconds(100);
  VideoChunk chunk = GenerateChunk(640, 480, future);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(chunk, false);
  auto frames = WaitFor(framesPromise).unwrap();
  EXPECT_GT(TimeStamp::Now() - now, future - now);
  ASSERT_EQ(frames.size(), 1U);
  const auto& [frame, conversionTime] = frames[0];
  EXPECT_EQ(frame.width(), 640);
  EXPECT_EQ(frame.height(), 480);
  EXPECT_THAT(frame, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime - now, future - now);
}

TEST_F(VideoFrameConverterTest, MultiPacing) {
  auto framesPromise = TakeNConvertedFrames(2);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(100);
  TimeStamp future2 = now + TimeDuration::FromMilliseconds(200);
  VideoChunk chunk = GenerateChunk(640, 480, future1);
  mConverter->SetActive(true);
  mConverter->SetIdleFrameDuplicationInterval(TimeDuration::FromSeconds(1));
  mConverter->QueueVideoChunk(chunk, false);
  chunk = GenerateChunk(640, 480, future2);
  mConverter->QueueVideoChunk(chunk, false);
  auto frames = WaitFor(framesPromise).unwrap();
  EXPECT_GT(TimeStamp::Now(), future2);
  ASSERT_EQ(frames.size(), 2U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime0 - now, future1 - now);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime1, future2);
  EXPECT_GT(conversionTime1 - now, conversionTime0 - now);
}

TEST_F(VideoFrameConverterTest, Duplication) {
  auto framesPromise = TakeNConvertedFrames(2);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(100);
  TimeDuration duplicationInterval = TimeDuration::FromMilliseconds(20);
  VideoChunk chunk = GenerateChunk(640, 480, future1);
  mConverter->SetActive(true);
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval);
  mConverter->QueueVideoChunk(chunk, false);
  auto frames = WaitFor(framesPromise).unwrap();
  EXPECT_GT(TimeStamp::Now() - now, future1 + duplicationInterval - now);
  ASSERT_EQ(frames.size(), 2U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime0, future1);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime1 - now, future1 + duplicationInterval - now);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame1.timestamp_us() -
                                             frame0.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval));

  // Check that we re-used the old buffer.
  EXPECT_EQ(frame0.video_frame_buffer(), frame1.video_frame_buffer());
}

TEST_F(VideoFrameConverterTest, MutableDuplication) {
  auto framesPromise = TakeNConvertedFrames(1);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(20);
  TimeDuration noDuplicationPeriod = TimeDuration::FromMilliseconds(100);
  TimeDuration duplicationInterval1 = TimeDuration::FromMilliseconds(50);
  TimeDuration duplicationInterval2 = TimeDuration::FromMilliseconds(10);
  VideoChunk chunk = GenerateChunk(640, 480, future1);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(chunk, false);
  while (TimeStamp::Now() < future1 + noDuplicationPeriod) {
    if (!NS_ProcessNextEvent(nullptr, false)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  auto frames = WaitFor(framesPromise).unwrap();
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval1);
  auto frames1 = WaitFor(TakeNConvertedFrames(2)).unwrap();
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval2);
  auto frames2 = WaitFor(TakeNConvertedFrames(2)).unwrap();
  frames.insert(frames.end(), frames1.begin(), frames1.end());
  frames.insert(frames.end(), frames2.begin(), frames2.end());

  EXPECT_GT(TimeStamp::Now() - now, noDuplicationPeriod + duplicationInterval1 +
                                        duplicationInterval2 * 2);
  ASSERT_EQ(frames.size(), 5U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime0 - now, future1 - now);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_EQ(frame0.video_frame_buffer(), frame1.video_frame_buffer());
  EXPECT_GT(conversionTime1 - now, future1 - now + noDuplicationPeriod);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame1.timestamp_us() -
                                             frame0.timestamp_us()) -
                  noDuplicationPeriod,
              IsDurationInMillisMultipleOf(duplicationInterval1));

  const auto& [frame2, conversionTime2] = frames[2];
  EXPECT_EQ(frame2.width(), 640);
  EXPECT_EQ(frame2.height(), 480);
  EXPECT_THAT(frame2, Not(IsFrameBlack()));
  EXPECT_EQ(frame0.video_frame_buffer(), frame2.video_frame_buffer());
  EXPECT_GT(conversionTime2 - now, noDuplicationPeriod + duplicationInterval1);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame2.timestamp_us() -
                                             frame1.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval1));

  const auto& [frame3, conversionTime3] = frames[3];
  EXPECT_EQ(frame3.width(), 640);
  EXPECT_EQ(frame3.height(), 480);
  EXPECT_THAT(frame3, Not(IsFrameBlack()));
  EXPECT_EQ(frame0.video_frame_buffer(), frame3.video_frame_buffer());
  EXPECT_GT(conversionTime3 - now,
            noDuplicationPeriod + duplicationInterval1 + duplicationInterval2);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame3.timestamp_us() -
                                             frame2.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval2));

  const auto& [frame4, conversionTime4] = frames[4];
  EXPECT_EQ(frame4.width(), 640);
  EXPECT_EQ(frame4.height(), 480);
  EXPECT_THAT(frame4, Not(IsFrameBlack()));
  EXPECT_EQ(frame0.video_frame_buffer(), frame4.video_frame_buffer());
  EXPECT_GT(conversionTime4 - now, noDuplicationPeriod + duplicationInterval1 +
                                       duplicationInterval2 * 2);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame4.timestamp_us() -
                                             frame3.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval2));
}

TEST_F(VideoFrameConverterTest, DropsOld) {
  auto framesPromise = TakeNConvertedFrames(1);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(1000);
  TimeStamp future2 = now + TimeDuration::FromMilliseconds(100);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future1), false);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, future2), false);
  auto frames = WaitFor(framesPromise).unwrap();
  EXPECT_GT(TimeStamp::Now(), future2);
  ASSERT_EQ(frames.size(), 1U);
  const auto& [frame, conversionTime] = frames[0];
  EXPECT_EQ(frame.width(), 640);
  EXPECT_EQ(frame.height(), 480);
  EXPECT_THAT(frame, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime - now, future2 - now);
}

// We check that the disabling code was triggered by sending multiple,
// different, frames to the converter within one second. While black, it shall
// treat all frames identical and issue one black frame per second.
// This version disables before queuing a frame. A frame will have to be
// invented.
TEST_F(VideoFrameConverterTest, BlackOnDisableCreated) {
  auto framesPromise = TakeNConvertedFrames(2);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(10);
  TimeStamp future2 = now + TimeDuration::FromMilliseconds(20);
  TimeStamp future3 = now + TimeDuration::FromMilliseconds(40);
  TimeDuration duplicationInterval = TimeDuration::FromMilliseconds(10);
  mConverter->SetActive(true);
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval);
  mConverter->SetTrackEnabled(false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future1), false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future2), false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future3), false);
  auto frames = WaitFor(framesPromise).unwrap();
  EXPECT_GT(TimeStamp::Now() - now, duplicationInterval);
  ASSERT_EQ(frames.size(), 2U);
  // The first frame was created instantly by SetTrackEnabled().
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, IsFrameBlack());
  EXPECT_GT(conversionTime0 - now, TimeDuration::FromSeconds(0));
  // The second frame was created by the same-frame timer. (We check multiples
  // because timing and scheduling can make it slower than requested)
  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, IsFrameBlack());
  EXPECT_GT(conversionTime1 - now, duplicationInterval);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame1.timestamp_us() -
                                             frame0.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval));
}

// We check that the disabling code was triggered by sending multiple,
// different, frames to the converter within a duplicationInterval. While black,
// it shall treat all frames identical and issue one black frame per
// duplicationInterval. This version queues a frame before disabling.
TEST_F(VideoFrameConverterTest, BlackOnDisableDuplicated) {
  TimeStamp now = TimeStamp::Now();
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, now), false);
  const auto [frame0, conversionTime0] =
      WaitFor(TakeNConvertedFrames(1)).unwrap()[0];

  // The first frame was queued.
  EXPECT_EQ(frame0.width(), 800);
  EXPECT_EQ(frame0.height(), 600);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));

  TimeStamp then = TimeStamp::Now();
  TimeStamp future1 = then + TimeDuration::FromMilliseconds(20);
  TimeStamp future2 = then + TimeDuration::FromMilliseconds(40);
  TimeDuration duplicationInterval = TimeDuration::FromMilliseconds(100);

  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future1), false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future2), false);

  const auto framesPromise = TakeNConvertedFrames(2);
  mConverter->SetTrackEnabled(false);
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval);

  auto frames = WaitFor(framesPromise).unwrap();
  ASSERT_EQ(frames.size(), 2U);
  // The second frame was duplicated by SetTrackEnabled.
  const auto& [frame1, conversionTime1] = frames[0];
  EXPECT_EQ(frame1.width(), 800);
  EXPECT_EQ(frame1.height(), 600);
  EXPECT_THAT(frame1, IsFrameBlack());
  EXPECT_GT(conversionTime1 - now, TimeDuration::Zero());
  // The third frame was created by the same-frame timer.
  const auto& [frame2, conversionTime2] = frames[1];
  EXPECT_EQ(frame2.width(), 800);
  EXPECT_EQ(frame2.height(), 600);
  EXPECT_THAT(frame2, IsFrameBlack());
  EXPECT_GT(conversionTime2 - now, duplicationInterval);
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame2.timestamp_us() -
                                             frame1.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval));
}

TEST_F(VideoFrameConverterTest, ClearFutureFramesOnJumpingBack) {
  TimeStamp start = TimeStamp::Now();
  TimeStamp future1 = start + TimeDuration::FromMilliseconds(10);

  auto framesPromise = TakeNConvertedFrames(1);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, future1), false);
  auto frames = WaitFor(framesPromise).unwrap();

  // We are now at t=10ms+. Queue a future frame and jump back in time to
  // signal a reset.

  framesPromise = TakeNConvertedFrames(1);
  TimeStamp step1 = TimeStamp::Now();
  ASSERT_GT(step1 - start, future1 - start);
  TimeStamp future2 = step1 + TimeDuration::FromMilliseconds(20);
  TimeStamp future3 = step1 + TimeDuration::FromMilliseconds(10);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future2), false);
  VideoChunk nullChunk;
  nullChunk.mFrame = mozilla::VideoFrame(nullptr, gfx::IntSize(800, 600));
  nullChunk.mTimeStamp = step1;
  mConverter->QueueVideoChunk(nullChunk, false);

  // We queue one more chunk after the reset so we don't have to wait for the
  // same-frame timer. It has a different time and resolution so we can
  // differentiate them.
  mConverter->QueueVideoChunk(GenerateChunk(320, 240, future3), false);

  {
    auto newFrames = WaitFor(framesPromise).unwrap();
    frames.insert(frames.end(), std::make_move_iterator(newFrames.begin()),
                  std::make_move_iterator(newFrames.end()));
  }
  TimeStamp step2 = TimeStamp::Now();
  EXPECT_GT(step2 - start, future3 - start);
  ASSERT_EQ(frames.size(), 2U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime0 - start, future1 - start);
  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 320);
  EXPECT_EQ(frame1.height(), 240);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_GT(conversionTime1 - start, future3 - start);
}

// We check that no frame is converted while inactive, and that on
// activating the most recently queued frame gets converted.
TEST_F(VideoFrameConverterTest, NoConversionsWhileInactive) {
  auto framesPromise = TakeNConvertedFrames(1);
  TimeStamp now = TimeStamp::Now();
  TimeStamp future1 = now + TimeDuration::FromMilliseconds(10);
  TimeStamp future2 = now + TimeDuration::FromMilliseconds(20);
  TimeDuration activeDelay = TimeDuration::FromMilliseconds(100);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, future1), false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, future2), false);

  // SetActive needs to follow the same async path as the frames to be in sync.
  auto q = TaskQueue::Create(GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER),
                             "VideoFrameConverterTest");
  auto timer = MakeRefPtr<MediaTimer<TimeStamp>>(false);
  timer->WaitUntil(now + activeDelay, __func__)
      ->Then(q, __func__,
             [converter = mConverter] { converter->SetActive(true); });

  auto frames = WaitFor(framesPromise).unwrap();
  ASSERT_EQ(frames.size(), 1U);
  const auto& [frame, conversionTime] = frames[0];
  (void)conversionTime;
  EXPECT_EQ(frame.width(), 800);
  EXPECT_EQ(frame.height(), 600);
  EXPECT_GT(frame.timestamp_us(), dom::RTCStatsTimestamp::FromMozTime(
                                      mTimestampMaker, now + activeDelay)
                                      .ToRealtime()
                                      .us());
  EXPECT_THAT(frame, Not(IsFrameBlack()));
}

TEST_F(VideoFrameConverterTest, TimestampPropagation) {
  auto framesPromise = TakeNConvertedFrames(2);
  TimeStamp now = TimeStamp::Now();
  TimeDuration d1 = TimeDuration::FromMilliseconds(1);
  TimeDuration d2 = TimeDuration::FromMilliseconds(29);

  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, now + d1), false);
  mConverter->QueueVideoChunk(GenerateChunk(800, 600, now + d2), false);

  auto frames = WaitFor(framesPromise).unwrap();
  ASSERT_EQ(frames.size(), 2U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_NEAR(frame0.timestamp_us(),
              dom::RTCStatsTimestamp::FromMozTime(mTimestampMaker, now + d1)
                  .ToRealtime()
                  .us(),
              1);
  EXPECT_GE(conversionTime0 - now, d1);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 800);
  EXPECT_EQ(frame1.height(), 600);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_NEAR(frame1.timestamp_us(),
              dom::RTCStatsTimestamp::FromMozTime(mTimestampMaker, now + d2)
                  .ToRealtime()
                  .us(),
              1);
  EXPECT_GE(conversionTime1 - now, d2);
}

TEST_F(VideoFrameConverterTest, IgnoreOldFrames) {
  // Do this in a task on the converter's TaskQueue, so it can call into
  // QueueForProcessing directly.
  TimeStamp now = TimeStamp::Now();
  TimeDuration d1 = TimeDuration::FromMilliseconds(10);
  TimeDuration duplicationInterval = TimeDuration::FromMilliseconds(50);
  TimeDuration d2 = d1 * 2;
  TimeDuration d3 = d2 - TimeDuration::FromMilliseconds(1);

  auto framesPromise = TakeNConvertedFrames(1);
  mConverter->SetActive(true);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, now + d1), false);
  auto frames = WaitFor(framesPromise).unwrap();

  framesPromise = TakeNConvertedFrames(2);

  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval);
  (void)WaitFor(InvokeAsync(mConverter->mTarget, __func__, [&] {
    // Time is now ~t1. This processes an extra frame similar to what
    // `SetActive(false); SetActive(true);` (using t=now()) would do.
    mConverter->mLastFrameQueuedForProcessing.mTime = now + d2;
    mConverter->ProcessVideoFrame(mConverter->mLastFrameQueuedForProcessing);

    // This queues a new chunk with an earlier timestamp than the extra frame
    // above. But it gets processed after the extra frame, so time will appear
    // to go backwards. This simulates a frame from the pacer being in flight
    // when we flip SetActive() above, for time t' < t. This frame is expected
    // to get ignored.
    mConverter->QueueForProcessing(
        GenerateChunk(800, 600, now + d3).mFrame.GetImage(), now + d3,
        gfx::IntSize(800, 600), false);
    return GenericPromise::CreateAndResolve(true, __func__);
  }));

  {
    auto newFrames = WaitFor(framesPromise).unwrap();
    frames.insert(frames.end(), std::make_move_iterator(newFrames.begin()),
                  std::make_move_iterator(newFrames.end()));
  }

  auto t0 = dom::RTCStatsTimestamp::FromMozTime(mTimestampMaker, now)
                .ToRealtime()
                .us();
  ASSERT_EQ(frames.size(), 3U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_NEAR(frame0.timestamp_us() - t0,
              static_cast<int64_t>(d1.ToMicroseconds()), 1);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_NEAR(frame1.timestamp_us() - t0,
              static_cast<int64_t>(d2.ToMicroseconds()), 1);
  EXPECT_GE(conversionTime1 - now, d1);

  const auto& [frame2, conversionTime2] = frames[2];
  EXPECT_EQ(frame2.width(), 640);
  EXPECT_EQ(frame2.height(), 480);
  EXPECT_THAT(frame2, Not(IsFrameBlack()));
  EXPECT_NEAR(frame2.timestamp_us() - t0,
              static_cast<int64_t>((d2 + duplicationInterval).ToMicroseconds()),
              1);
  EXPECT_GE(conversionTime2 - now, d2 + duplicationInterval);
}

TEST_F(VideoFrameConverterTest, SameFrameTimerRacingWithPacing) {
  TimeStamp now = TimeStamp::Now();
  TimeDuration d1 = TimeDuration::FromMilliseconds(10);
  TimeDuration duplicationInterval = TimeDuration::FromMilliseconds(5);
  TimeDuration d2 =
      d1 + duplicationInterval - TimeDuration::FromMilliseconds(1);

  auto framesPromise = TakeNConvertedFrames(3);
  mConverter->SetActive(true);
  mConverter->SetIdleFrameDuplicationInterval(duplicationInterval);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, now + d1), false);
  mConverter->QueueVideoChunk(GenerateChunk(640, 480, now + d2), false);
  auto frames = WaitFor(framesPromise).unwrap();

  // The expected order here (in timestamps) is t1, t2, t2+5ms.
  //
  // If the same-frame timer doesn't check what is queued we could end up with
  // t1, t1+5ms, t2.

  auto t0 = dom::RTCStatsTimestamp::FromMozTime(mTimestampMaker, now)
                .ToRealtime()
                .us();
  ASSERT_EQ(frames.size(), 3U);
  const auto& [frame0, conversionTime0] = frames[0];
  EXPECT_EQ(frame0.width(), 640);
  EXPECT_EQ(frame0.height(), 480);
  EXPECT_THAT(frame0, Not(IsFrameBlack()));
  EXPECT_NEAR(frame0.timestamp_us() - t0,
              static_cast<int64_t>(d1.ToMicroseconds()), 1);
  EXPECT_GE(conversionTime0 - now, d1);

  const auto& [frame1, conversionTime1] = frames[1];
  EXPECT_EQ(frame1.width(), 640);
  EXPECT_EQ(frame1.height(), 480);
  EXPECT_THAT(frame1, Not(IsFrameBlack()));
  EXPECT_NEAR(frame1.timestamp_us() - t0,
              static_cast<int64_t>(d2.ToMicroseconds()), 1);
  EXPECT_GE(conversionTime1 - now, d2);

  const auto& [frame2, conversionTime2] = frames[2];
  EXPECT_EQ(frame2.width(), 640);
  EXPECT_EQ(frame2.height(), 480);
  EXPECT_THAT(frame2, Not(IsFrameBlack()));
  EXPECT_THAT(TimeDuration::FromMicroseconds(frame2.timestamp_us() -
                                             frame1.timestamp_us()),
              IsDurationInMillisPositiveMultipleOf(duplicationInterval));
  EXPECT_GE(conversionTime2 - now, d2 + duplicationInterval);
}

TEST_F(VideoFrameConverterTest, SinkWantsResolutionAlignment) {
  const std::array<int, 5> alignments{2, 16, 39, 400, 1000};
  const int width = 640;
  const int height = 480;

  TimeStamp now = TimeStamp::Now();
  TimeDuration interval = TimeDuration::FromMilliseconds(1);
  mConverter->SetActive(true);
  webrtc::VideoSinkWants wants;
  for (uint32_t i = 0; i < alignments.size(); ++i) {
    const TimeStamp t = now + interval * (i + 1);
    // Test that requesting specific alignment always results in the expected
    // number of layers and valid alignment.
    wants.resolution_alignment = alignments[i];
    mListener->SetWants(wants);
    auto framesPromise = TakeNConvertedFrames(1);
    mConverter->QueueVideoChunk(GenerateChunk(width, height, t), false);
    const auto [frame, time] = WaitFor(framesPromise).unwrap()[0];

    EXPECT_EQ(frame.width() % alignments[i], 0)
        << " for width " << frame.width() << " and alignment " << alignments[i];
    EXPECT_EQ(frame.height() % alignments[i], 0)
        << " for height " << frame.height() << " and alignment "
        << alignments[i];
  }
}
