/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

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
#include "mozilla/Preferences.h"
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
using media::TimeIntervals;
using media::TimeUnit;
using testing::InSequence;
using testing::MockFunction;
using testing::Return;
using testing::StrEq;

TEST(TestMediaFormatReader, PositiveMediaTimeIgnoresNegativePreroll)
{
  RefPtr dataDemuxer = new MockMediaDataDemuxer();
  RefPtr trackDemuxer =
      new MockMediaTrackDemuxer("video/x-test; width=640; height=360");

  ON_CALL(*dataDemuxer, GetNumberTracks(TrackType::kVideoTrack))
      .WillByDefault(Return(1));

  ON_CALL(*dataDemuxer, GetTrackDemuxer)
      .WillByDefault([&](TrackType aType, uint32_t aTrackNumber) {
        EXPECT_EQ(aTrackNumber, 0u);
        EXPECT_EQ(aType, TrackType::kVideoTrack);
        return do_AddRef(trackDemuxer);
      });

  ON_CALL(*trackDemuxer, GetInfo).WillByDefault([]() {
    auto extended =
        MakeMediaContainerType("video/x-test; width=640; height=360").value();
    UniquePtr<TrackInfo> info =
        CreateTrackInfoWithMIMETypeAndContainerTypeExtraParameters(
            extended.Type().AsString(), extended);
    info->mMediaTime = TimeUnit::FromMicroseconds(6375000);
    return info;
  });

  EXPECT_CALL(*trackDemuxer, MockGetSamples).WillOnce([]() {
    RefPtr sample = new MediaRawData;
    sample->mTime = TimeUnit::FromMicroseconds(-6375000);
    sample->mDuration = TimeUnit::FromMicroseconds(41667);
    RefPtr<SamplesHolder> samples = new SamplesHolder;
    samples->AppendSample(std::move(sample));
    return SamplesPromise::CreateAndResolve(samples, __func__);
  });

  RefPtr pdm = new MockDecoderModule();
  PDMFactory::AutoForcePDM autoForcePDM(pdm);
  auto owner = std::make_unique<MockMediaDecoderOwner>();
  RefPtr container = new VideoFrameContainer(
      owner.get(),
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
  RefPtr reader = new MediaFormatReader(init, dataDemuxer);
  RefPtr proxy = new ReaderProxy(AbstractThread::MainThread(), reader);
  EXPECT_NS_SUCCEEDED(reader->Init());

  MetadataHolder metadata = WaitForResolve(proxy->ReadMetadata());
  EXPECT_EQ(metadata.mInfo->mStartTime, TimeUnit::Zero());

  WaitForResolve(proxy->Shutdown());
}

TEST(TestMediaFormatReader, WaitingForDemuxAfterInternalSeek)
{
  RefPtr<MediaFormatReader> reader;
  // Thread scheduling provides ordering for thread initializations before
  // their first read.
  RefPtr<TaskQueue> demuxerThread;
  RefPtr<TaskQueue> decoderThread;

  // Wait enough for the MediaFormatReader to process at least
  // aCount demuxer or decoder operations, if pending.
  auto WaitForReaderOperations = [&](int aCount) {
    // AwaitIdle() ensures that no tasks are pending and any task for another
    // thread is already in the other thread's queue, only if dispatch across
    // threads is not via tail dispatch.  Tail dispatch is not used because
    // the demuxer and decoder threads do not support tail dispatch, even
    // though the MediaFormatReader task queue supports tail dispatch.
    // https://searchfox.org/mozilla-central/rev/126697140e711e04a9d95edae537541c3bde89cc/xpcom/threads/AbstractThread.cpp#285-289
    MOZ_ASSERT(!demuxerThread->SupportsTailDispatch());
    MOZ_ASSERT(!decoderThread->SupportsTailDispatch());
    // Check that the reader thread has dispatched the first request to
    // the demuxer or decoder thread.
    reader->OwnerThread()->AwaitIdle();
    for (int i = 0; i < aCount; ++i) {
      demuxerThread->AwaitIdle();
      decoderThread->AwaitIdle();
      reader->OwnerThread()->AwaitIdle();
    }
  };

  RefPtr dataDemuxer = new MockMediaDataDemuxer();
  RefPtr trackDemuxer =
      // VideoInfo::IsValid() needs dimensions.
      new MockMediaTrackDemuxer("video/x-test; width=640; height=360");

  ON_CALL(*dataDemuxer, GetNumberTracks(TrackType::kVideoTrack))
      .WillByDefault(Return(1));

  ON_CALL(*dataDemuxer, GetTrackDemuxer)
      .WillByDefault([&](TrackType aType, uint32_t aTrackNumber) {
        EXPECT_EQ(aTrackNumber, 0u);
        EXPECT_EQ(aType, TrackType::kVideoTrack);
        if (!demuxerThread) {
          demuxerThread = do_QueryObject(AbstractThread::GetCurrent());
        }
        return do_AddRef(trackDemuxer);
      });

  RefPtr pdm = new MockDecoderModule();
  PDMFactory::AutoForcePDM autoForcePDM(pdm);
  RefPtr<MockVideoDataDecoder> decoder;
  MozPromiseHolder<DecodePromise> drainPromise;
  EXPECT_CALL(*pdm, CreateVideoDecoder)
      .WillOnce([&](const CreateDecoderParams& aParams) {
        decoder = new MockVideoDataDecoder(aParams);
        InSequence s;
        // The first drain requires two calls: one to fetch the frames...
        EXPECT_CALL(*decoder, Drain).WillOnce([&] {
          MOZ_ASSERT(!decoderThread);
          decoderThread = do_QueryObject(AbstractThread::GetCurrent());
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

    EXPECT_CALL(*trackDemuxer, MockGetSamples).Times(2).WillRepeatedly([]() {
      static int count = 0;
      RefPtr sample = new MediaRawData;
      sample->mTime = TimeUnit(count, 30);
      ++count;
      RefPtr<SamplesHolder> samples = new SamplesHolder;
      samples->AppendSample(std::move(sample));
      return SamplesPromise::CreateAndResolve(samples, __func__);
    });
    EXPECT_CALL(*trackDemuxer, MockGetSamples).WillOnce([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
    EXPECT_CALL(*trackDemuxer, Seek).WillOnce([&](const TimeUnit& aTime) {
      // Reset mWaitingForDataStartTime so that OnDemuxFailed() calls
      // RequestDrain().
      EXPECT_NS_SUCCEEDED(reader->OwnerThread()->Dispatch(
          NewRunnableMethod("NotifyDataArrived", reader.get(),
                            &MediaFormatReader::NotifyDataArrived)));
      return SeekPromise::CreateAndResolve(TimeUnit::Zero(), __func__);
    });
    EXPECT_CALL(*trackDemuxer, MockGetSamples).WillOnce([]() {
      RefPtr sample = new MediaRawData;
      // Time is zero after the seek.
      sample->mTime = TimeUnit(0, 30);
      RefPtr<SamplesHolder> samples = new SamplesHolder;
      samples->AppendSample(std::move(sample));
      return SamplesPromise::CreateAndResolve(samples, __func__);
    });
    EXPECT_CALL(*trackDemuxer, MockGetSamples).WillOnce([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
    EXPECT_CALL(checkpoint, Call(StrEq("Internal seek waiting for data")));

    EXPECT_CALL(*trackDemuxer, MockGetSamples).WillRepeatedly([]() {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    });
  }

  auto owner = std::make_unique<MockMediaDecoderOwner>();
  RefPtr container = new VideoFrameContainer(
      owner.get(),
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
  reader = new MediaFormatReader(init, dataDemuxer);
  RefPtr proxy = new ReaderProxy(AbstractThread::MainThread(), reader);
  EXPECT_NS_SUCCEEDED(reader->Init());

  // ReadMetadata() to init demuxer.
  (void)WaitForResolve(proxy->ReadMetadata());
  // Two samples are provided by the demuxer, but the third demux request is
  // rejected.  The first drain provides two decoded samples.
  for (int i = 0; i < 2; ++i) {
    (void)WaitForResolve(proxy->RequestVideoData(TimeUnit(), false));
  }
  // A third sample is not available.
  MediaResult result =
      WaitForReject(proxy->RequestVideoData(TimeUnit(), false));
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
  (void)proxy->RequestVideoData(TimeUnit(), false);
  // Trigger another Update() to check that another drain does not start.
  EXPECT_NS_SUCCEEDED(reader->OwnerThread()->Dispatch(
      NewRunnableMethod("NotifyDataArrived", reader.get(),
                        &MediaFormatReader::NotifyDataArrived)));
  // Wait for NotifyDataArrived to be processed by the demuxer and for another
  // demux request to complete.
  WaitForReaderOperations(2);
  // Clean up.
  WaitForResolve(proxy->Shutdown());
  drainPromise.Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);
}
