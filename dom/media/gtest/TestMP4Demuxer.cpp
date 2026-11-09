/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AOMDecoder.h"
#include "BufferMediaResource.h"
#include "DecoderData.h"
#include "H265.h"
#include "MP4Demuxer.h"
#include "MP4Metadata.h"
#include "MediaDataDemuxer.h"
#include "MockMediaResource.h"
#include "VideoUtils.h"
#include "gtest/gtest.h"
#include "mozilla/MozPromise.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mp4parse.h"

using namespace mozilla;
using media::TimeUnit;

TEST(MP4SampleIndex, OversizedOffset)
{
  Mp4parseIndice indices[] = {{100, 200, 0, 1000, 0, true},
                              {200, 300, 1000, 2000, 1000, true},
                              {uint64_t(INT64_MAX) + 1, uint64_t(INT64_MAX) + 2,
                               2000, 3000, 2000, true}};
  Mp4parseByteData data{3, nullptr, indices};
  for (bool isAudio : {false, true}) {
    SCOPED_TRACE(isAudio);
    RefPtr<MP4SampleIndex> index =
        new MP4SampleIndex(IndiceWrapper(data), nullptr, 1, isAudio, 1000);
    MediaByteRangeSet ranges;
    ranges += MediaByteRange(200, 300);
    auto buffered = index->ConvertByteRangesToTimeRanges(ranges);
    ASSERT_EQ(buffered.Length(), 1u);
    EXPECT_EQ(buffered.GetStart(), TimeUnit(1000, 1000));
    EXPECT_EQ(buffered.GetEnd(), TimeUnit(2000, 1000));
  }
}

TEST(MP4SampleIndex, MaximumOffset)
{
  Mp4parseIndice indices[] = {
      {uint64_t(INT64_MAX) - 200, uint64_t(INT64_MAX) - 100, 0, 1000, 0, true},
      {uint64_t(INT64_MAX) - 100, uint64_t(INT64_MAX), 1000, 2000, 1000, true}};
  Mp4parseByteData data{2, nullptr, indices};
  for (bool isAudio : {false, true}) {
    SCOPED_TRACE(isAudio);
    RefPtr<MP4SampleIndex> index =
        new MP4SampleIndex(IndiceWrapper(data), nullptr, 1, isAudio, 1000);
    MediaByteRangeSet ranges;
    ranges += MediaByteRange(INT64_MAX - 100, INT64_MAX);
    auto buffered = index->ConvertByteRangesToTimeRanges(ranges);
    ASSERT_EQ(buffered.Length(), 1u);
    EXPECT_EQ(buffered.GetStart(), TimeUnit(1000, 1000));
    EXPECT_EQ(buffered.GetEnd(), TimeUnit(2000, 1000));
  }
}

#define DO_FAIL                           \
  [binding]() -> void {                   \
    EXPECT_TRUE(false);                   \
    binding->mTaskQueue->BeginShutdown(); \
  }

class MP4DemuxerBinding {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MP4DemuxerBinding);

  RefPtr<MockMediaResource> resource;
  RefPtr<MP4Demuxer> mDemuxer;
  RefPtr<TaskQueue> mTaskQueue;
  RefPtr<MediaTrackDemuxer> mAudioTrack;
  RefPtr<MediaTrackDemuxer> mVideoTrack;
  uint32_t mIndex;
  nsTArray<RefPtr<MediaRawData>> mSamples;
  nsTArray<int64_t> mKeyFrameTimecodes;
  MozPromiseHolder<GenericPromise> mCheckTrackKeyFramePromise;
  MozPromiseHolder<GenericPromise> mCheckTrackSamples;

  explicit MP4DemuxerBinding(const char* aFileName = "dash_dashinit.mp4")
      : resource(new MockMediaResource(aFileName)),
        mDemuxer(new MP4Demuxer(resource)),
        mTaskQueue(TaskQueue::Create(
            GetMediaThreadPool(MediaThreadType::SUPERVISOR), "TestMP4Demuxer")),
        mIndex(0) {
    EXPECT_EQ(NS_OK, resource->Open());
  }

  explicit MP4DemuxerBinding(MediaResource* aResource)
      : resource(nullptr),
        mDemuxer(new MP4Demuxer(aResource)),
        mTaskQueue(TaskQueue::Create(
            GetMediaThreadPool(MediaThreadType::SUPERVISOR), "TestMP4Demuxer")),
        mIndex(0) {}

  template <typename Function>
  void RunTestAndWait(const Function& aFunction) {
    Function func(aFunction);
    RefPtr<MP4DemuxerBinding> binding = this;
    mDemuxer->Init()->Then(mTaskQueue, __func__, std::move(func), DO_FAIL);
    mTaskQueue->AwaitShutdownAndIdle();
  }

  RefPtr<GenericPromise> CheckTrackKeyFrame(MediaTrackDemuxer* aTrackDemuxer) {
    MOZ_RELEASE_ASSERT(mTaskQueue->IsCurrentThreadIn());

    RefPtr<MediaTrackDemuxer> track = aTrackDemuxer;
    RefPtr<MP4DemuxerBinding> binding = this;

    auto time = TimeUnit::Invalid();
    while (mIndex < mSamples.Length()) {
      uint32_t i = mIndex++;
      if (mSamples[i]->mKeyframe) {
        time = mSamples[i]->mTime;
        break;
      }
    }

    RefPtr<GenericPromise> p = mCheckTrackKeyFramePromise.Ensure(__func__);

    if (!time.IsValid()) {
      mCheckTrackKeyFramePromise.Resolve(true, __func__);
      return p;
    }

    DispatchTask([track, time, binding]() {
      track->Seek(time)->Then(
          binding->mTaskQueue, __func__,
          [track, time, binding]() {
            track->GetSamples()->Then(
                binding->mTaskQueue, __func__,
                [track, time,
                 binding](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
                  EXPECT_EQ(time, aSamples->GetSamples()[0]->mTime);
                  binding->CheckTrackKeyFrame(track);
                },
                DO_FAIL);
          },
          DO_FAIL);
    });

    return p;
  }

  RefPtr<GenericPromise> CheckTrackSamples(MediaTrackDemuxer* aTrackDemuxer) {
    MOZ_RELEASE_ASSERT(mTaskQueue->IsCurrentThreadIn());

    RefPtr<MediaTrackDemuxer> track = aTrackDemuxer;
    RefPtr<MP4DemuxerBinding> binding = this;

    RefPtr<GenericPromise> p = mCheckTrackSamples.Ensure(__func__);

    DispatchTask([track, binding]() {
      track->GetSamples()->Then(
          binding->mTaskQueue, __func__,
          [track, binding](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
            if (aSamples->GetSamples().Length()) {
              binding->mSamples.AppendElements(aSamples->GetSamples());
              binding->CheckTrackSamples(track);
            }
          },
          [binding](const MediaResult& aError) {
            if (aError == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
              EXPECT_TRUE(binding->mSamples.Length() > 1);
              for (uint32_t i = 0; i < (binding->mSamples.Length() - 1); i++) {
                EXPECT_LT(binding->mSamples[i]->mTimecode,
                          binding->mSamples[i + 1]->mTimecode);
                if (binding->mSamples[i]->mKeyframe) {
                  binding->mKeyFrameTimecodes.AppendElement(
                      binding->mSamples[i]->mTimecode.ToMicroseconds());
                }
              }
              binding->mCheckTrackSamples.Resolve(true, __func__);
            } else {
              EXPECT_TRUE(false);
              binding->mCheckTrackSamples.Reject(aError, __func__);
            }
          });
    });

    return p;
  }

 private:
  template <typename FunctionType>
  void DispatchTask(FunctionType aFun) {
    RefPtr<Runnable> r =
        NS_NewRunnableFunction("MP4DemuxerBinding::DispatchTask", aFun);
    (void)mTaskQueue->Dispatch(r.forget());
  }

  virtual ~MP4DemuxerBinding() = default;
};

TEST(MP4Demuxer, Seek)
{
  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding();

  binding->RunTestAndWait([binding]() {
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->CheckTrackSamples(binding->mVideoTrack)
        ->Then(
            binding->mTaskQueue, __func__,
            [binding]() {
              binding->CheckTrackKeyFrame(binding->mVideoTrack)
                  ->Then(
                      binding->mTaskQueue, __func__,
                      [binding]() { binding->mTaskQueue->BeginShutdown(); },
                      DO_FAIL);
            },
            DO_FAIL);
  });
}

static nsCString ToCryptoString(const CryptoSample& aCrypto) {
  nsCString res;
  if (aCrypto.IsEncrypted()) {
    res.AppendPrintf("%d ", aCrypto.mIVSize);
    for (size_t i = 0; i < aCrypto.mKeyId.Length(); i++) {
      res.AppendPrintf("%02x", aCrypto.mKeyId[i]);
    }
    res.AppendLiteral(" ");
    for (size_t i = 0; i < aCrypto.mIV.Length(); i++) {
      res.AppendPrintf("%02x", aCrypto.mIV[i]);
    }
    EXPECT_EQ(aCrypto.mPlainSizes.Length(), aCrypto.mEncryptedSizes.Length());
    for (size_t i = 0; i < aCrypto.mPlainSizes.Length(); i++) {
      res.AppendPrintf(" %d,%d", aCrypto.mPlainSizes[i],
                       aCrypto.mEncryptedSizes[i]);
    }
  } else {
    res.AppendLiteral("no crypto");
  }
  return res;
}

TEST(MP4Demuxer, CENCFragVideo)
{
  const char* video[] = {
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000000 "
      "5,684 5,16980",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000450 "
      "5,1826",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000004c3 "
      "5,1215",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000050f "
      "5,1302",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000561 "
      "5,939",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000059c "
      "5,763",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000005cc "
      "5,672",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000005f6 "
      "5,748",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000625 "
      "5,1025",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000666 "
      "5,730",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000694 "
      "5,897",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000006cd "
      "5,643",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000006f6 "
      "5,556",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000719 "
      "5,527",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000073a "
      "5,606",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000760 "
      "5,701",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000078c "
      "5,531",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000007ae "
      "5,562",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000007d2 "
      "5,576",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000007f6 "
      "5,514",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000817 "
      "5,404",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000831 "
      "5,635",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000859 "
      "5,433",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000875 "
      "5,478",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000893 "
      "5,474",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000008b1 "
      "5,462",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000008ce "
      "5,473",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000008ec "
      "5,437",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000908 "
      "5,418",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000923 "
      "5,475",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000941 "
      "5,23133",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000ee7 "
      "5,475",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f05 "
      "5,402",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f1f "
      "5,415",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f39 "
      "5,408",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f53 "
      "5,442",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f6f "
      "5,385",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f88 "
      "5,368",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000f9f "
      "5,354",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000fb6 "
      "5,400",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000fcf "
      "5,399",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000000fe8 "
      "5,1098",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000102d "
      "5,1508",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000108c "
      "5,1345",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000010e1 "
      "5,1945",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000115b "
      "5,1824",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000011cd "
      "5,2133",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001253 "
      "5,2486",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000012ef "
      "5,1739",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000135c "
      "5,1836",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000013cf "
      "5,2367",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001463 "
      "5,2571",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001504 "
      "5,3008",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000015c0 "
      "5,3255",
      "16 7e571d037e571d037e571d037e571d03 0000000000000000000000000000168c "
      "5,3225",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001756 "
      "5,3118",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001819 "
      "5,2407",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000018b0 "
      "5,2400",
      "16 7e571d037e571d037e571d037e571d03 00000000000000000000000000001946 "
      "5,2158",
      "16 7e571d037e571d037e571d037e571d03 000000000000000000000000000019cd "
      "5,2392",
  };

  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding("gizmo-frag.mp4");

  binding->RunTestAndWait([binding, video]() {
    // grab all video samples.
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->CheckTrackSamples(binding->mVideoTrack)
        ->Then(
            binding->mTaskQueue, __func__,
            [binding, video]() {
              for (uint32_t i = 0; i < binding->mSamples.Length(); i++) {
                nsCString text = ToCryptoString(binding->mSamples[i]->mCrypto);
                EXPECT_STREQ(video[i++], text.get());
              }
              EXPECT_EQ(std::size(video), binding->mSamples.Length());
              binding->mTaskQueue->BeginShutdown();
            },
            DO_FAIL);
  });
}

TEST(MP4Demuxer, CENCFragAudio)
{
  const char* audio[] = {
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000000 "
      "0,281",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000012 "
      "0,257",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000023 "
      "0,246",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000033 "
      "0,257",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000044 "
      "0,260",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000055 "
      "0,260",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000066 "
      "0,272",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000077 "
      "0,280",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000089 "
      "0,284",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000009b "
      "0,290",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000000ae "
      "0,278",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000000c0 "
      "0,268",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000000d1 "
      "0,307",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000000e5 "
      "0,290",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000000f8 "
      "0,304",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000010b "
      "0,316",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000011f "
      "0,308",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000133 "
      "0,301",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000146 "
      "0,318",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000015a "
      "0,311",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000016e "
      "0,303",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000181 "
      "0,325",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000196 "
      "0,334",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000001ab "
      "0,344",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000001c1 "
      "0,344",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000001d7 "
      "0,387",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000001f0 "
      "0,396",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000209 "
      "0,368",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000220 "
      "0,373",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000238 "
      "0,425",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000253 "
      "0,428",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000026e "
      "0,426",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000289 "
      "0,427",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000002a4 "
      "0,424",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000002bf "
      "0,447",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000002db "
      "0,446",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000002f7 "
      "0,442",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000313 "
      "0,444",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000032f "
      "0,374",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000347 "
      "0,405",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000361 "
      "0,372",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000379 "
      "0,395",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000392 "
      "0,435",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000003ae "
      "0,426",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000003c9 "
      "0,430",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000003e4 "
      "0,390",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000003fd "
      "0,335",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000412 "
      "0,339",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000428 "
      "0,352",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000043e "
      "0,364",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000455 "
      "0,398",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000046e "
      "0,451",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000048b "
      "0,448",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000004a7 "
      "0,436",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000004c3 "
      "0,424",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000004de "
      "0,428",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000004f9 "
      "0,413",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000513 "
      "0,430",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000052e "
      "0,450",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000054b "
      "0,386",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000564 "
      "0,320",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000578 "
      "0,347",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000058e "
      "0,382",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000005a6 "
      "0,437",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000005c2 "
      "0,387",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000005db "
      "0,340",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000005f1 "
      "0,337",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000607 "
      "0,389",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000620 "
      "0,428",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000063b "
      "0,426",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000656 "
      "0,446",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000672 "
      "0,456",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000068f "
      "0,468",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000006ad "
      "0,468",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000006cb "
      "0,463",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000006e8 "
      "0,467",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000706 "
      "0,460",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000723 "
      "0,446",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000073f "
      "0,453",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000075c "
      "0,448",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000778 "
      "0,446",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000794 "
      "0,439",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000007b0 "
      "0,436",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000007cc "
      "0,441",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000007e8 "
      "0,465",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000806 "
      "0,448",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000822 "
      "0,448",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000083e "
      "0,469",
      "16 7e571d047e571d047e571d047e571d04 0000000000000000000000000000085c "
      "0,431",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000877 "
      "0,437",
      "16 7e571d047e571d047e571d047e571d04 00000000000000000000000000000893 "
      "0,474",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000008b1 "
      "0,436",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000008cd "
      "0,433",
      "16 7e571d047e571d047e571d047e571d04 000000000000000000000000000008e9 "
      "0,481",
  };

  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding("gizmo-frag.mp4");

  binding->RunTestAndWait([binding, audio]() {
    // grab all audio samples.
    binding->mAudioTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
    binding->CheckTrackSamples(binding->mAudioTrack)
        ->Then(
            binding->mTaskQueue, __func__,
            [binding, audio]() {
              EXPECT_TRUE(binding->mSamples.Length() > 1);
              for (uint32_t i = 0; i < binding->mSamples.Length(); i++) {
                nsCString text = ToCryptoString(binding->mSamples[i]->mCrypto);
                EXPECT_STREQ(audio[i++], text.get());
              }
              EXPECT_EQ(std::size(audio), binding->mSamples.Length());
              binding->mTaskQueue->BeginShutdown();
            },
            DO_FAIL);
  });
}

TEST(MP4Demuxer, GetNextKeyframe)
{
  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding("gizmo-frag.mp4");

  binding->RunTestAndWait([binding]() {
    // Insert a [0,end] buffered range, to simulate Moof's being buffered
    // via MSE.
    auto len = binding->resource->GetLength();
    binding->resource->MockAddBufferedRange(0, len);

    // gizmp-frag has two keyframes; one at dts=cts=0, and another at
    // dts=cts=1000000. Verify we get expected results.
    TimeUnit time;
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->mVideoTrack->Reset();
    binding->mVideoTrack->GetNextRandomAccessPoint(&time);
    EXPECT_EQ(time.ToMicroseconds(), 0);
    binding->mVideoTrack->GetSamples()->Then(
        binding->mTaskQueue, __func__,
        [binding]() {
          TimeUnit time;
          binding->mVideoTrack->GetNextRandomAccessPoint(&time);
          EXPECT_EQ(time.ToMicroseconds(), 1000000);
          binding->mTaskQueue->BeginShutdown();
        },
        DO_FAIL);
  });
}

// Generated with open GOP so non-first keyframes are CRA_NUT, not IDR:
//   ffmpeg -f lavfi -i testsrc=duration=4:size=128x96:rate=30
//     -c:v libx265 -x265-params keyint=30:min-keyint=30:open-gop=1:info=0
//     -an test_hevc_open_gop.mp4
// Sample 1 is IDR_N_LP; samples 28, 60, 88 are CRA_NUT.
// H265::IsKeyFrame() returns true only for IDR_W_RADL/IDR_N_LP, not CRA.
TEST(MP4Demuxer, SeekHEVC)
{
  RefPtr<MP4DemuxerBinding> binding =
      new MP4DemuxerBinding("test_hevc_open_gop.mp4");

  // Seek past the first GOP. The nearest stss sync sample is a CRA_NUT.
  // VideoToolbox can return a bad data error when a CRA is the first sample
  // fed to a fresh decode session, analogous to non-IDR I-frames for H.264.
  // The demuxer must only mark IDR frames as keyframes on macOS.
  // On other platforms the seek should resolve to a CRA_NUT sample.
  const TimeUnit seekTime = TimeUnit::FromSeconds(2.0);

  binding->RunTestAndWait([binding, seekTime]() {
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->mVideoTrack->Seek(seekTime)->Then(
        binding->mTaskQueue, __func__,
        [binding, seekTime](TimeUnit aActualTime) {
          EXPECT_LE(aActualTime, seekTime);
          binding->mVideoTrack->GetSamples()->Then(
              binding->mTaskQueue, __func__,
              [binding,
               aActualTime](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
                EXPECT_GT(aSamples->GetSamples().Length(), 0u);
                RefPtr<MediaRawData> first = aSamples->GetSamples()[0];
                EXPECT_TRUE(first->mKeyframe);
                EXPECT_EQ(first->mTime, aActualTime);

                auto isIDR = H265::IsKeyFrame(first);
                EXPECT_TRUE(isIDR.isOk());
#ifdef MOZ_APPLEMEDIA
                // CRA keyframe flags are stripped on Apple platforms, so the
                // seek falls back to the preceding IDR.
                EXPECT_TRUE(isIDR.unwrap());
#else
                // On other platforms it should not resolve to the first IDR.
                EXPECT_FALSE(isIDR.unwrap());
                auto isRAP = H265::IsRandomAccessPoint(first);
                EXPECT_TRUE(isRAP.isOk() && isRAP.unwrap());
#endif
                binding->mTaskQueue->BeginShutdown();
              },
              [binding](const MediaResult&) {
                EXPECT_TRUE(false) << "GetSamples failed after seek";
                binding->mTaskQueue->BeginShutdown();
              });
        },
        [binding](const MediaResult&) {
          EXPECT_TRUE(false) << "Seek failed";
          binding->mTaskQueue->BeginShutdown();
        });
  });
}

// Generated with 1-second GOPs, then stss rewritten so later sync samples
// are not KEY_FRAME (same shape as test_hevc_open_gop.mp4 CRA vs IDR):
//   ffmpeg -f lavfi -i testsrc=duration=4:size=128x128:rate=30
//     -c:v libaom-av1 -cpu-used 8 -crf 40 -g 30 -keyint_min 30 -an
//     test_av1.mp4
//   python3 - <<'PY'
//   from pathlib import Path
//   p = Path("test_av1.mp4")
//   b = bytearray(p.read_bytes())
//   i = b.find(b"stss")
//   for k, s in enumerate((1, 28, 60, 88)):
//       b[i+12+4*k:i+16+4*k] = s.to_bytes(4, "big")
//   p.write_bytes(b)
//   PY
// Bitstream KEY_FRAME is samples 1, 31, 61, 91. stss is 1, 28, 60, 88.
TEST(MP4Demuxer, SeekAV1)
{
  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding("test_av1.mp4");

  // Seek past the first GOP. The nearest stss sync sample is not a KEY_FRAME.
  // The demuxer must mark the first sample from the bitstream, not stss.
  const TimeUnit seekTime = TimeUnit::FromSeconds(2.0);

  binding->RunTestAndWait([binding, seekTime]() {
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->mVideoTrack->Seek(seekTime)->Then(
        binding->mTaskQueue, __func__,
        [binding, seekTime](TimeUnit aActualTime) {
          EXPECT_LE(aActualTime, seekTime);
          binding->mVideoTrack->GetSamples()->Then(
              binding->mTaskQueue, __func__,
              [binding,
               aActualTime](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
                EXPECT_GT(aSamples->GetSamples().Length(), 0u);
                RefPtr<MediaRawData> first = aSamples->GetSamples()[0];
                EXPECT_TRUE(first->mKeyframe);
                EXPECT_EQ(first->mTime, aActualTime);
                EXPECT_TRUE(AOMDecoder::IsKeyframe(
                    Span<const uint8_t>(first->Data(), first->Size())));
                binding->mTaskQueue->BeginShutdown();
              },
              [binding](const MediaResult&) {
                EXPECT_TRUE(false) << "GetSamples failed after seek";
                binding->mTaskQueue->BeginShutdown();
              });
        },
        [binding](const MediaResult&) {
          EXPECT_TRUE(false) << "Seek failed";
          binding->mTaskQueue->BeginShutdown();
        });
  });
}

TEST(MP4Demuxer, ZeroInLastMoov)
{
  RefPtr<MP4DemuxerBinding> binding =
      new MP4DemuxerBinding("short-zero-in-moov.mp4");
  binding->RunTestAndWait([binding]() {
    // It demuxes without error. That is sufficient.
    binding->mTaskQueue->BeginShutdown();
  });
}

TEST(MP4Demuxer, ZeroInMoovQuickTime)
{
  RefPtr<MP4DemuxerBinding> binding =
      new MP4DemuxerBinding("short-zero-inband.mov");
  binding->RunTestAndWait([binding]() {
    // It demuxes without error. That is sufficient.
    binding->mTaskQueue->BeginShutdown();
  });
}

TEST(MP4Demuxer, IgnoreMinus1Duration)
{
  RefPtr<MP4DemuxerBinding> binding =
      new MP4DemuxerBinding("negative_duration.mp4");
  binding->RunTestAndWait([binding]() {
    // It demuxes without error. That is sufficient.
    binding->mTaskQueue->BeginShutdown();
  });
}

static nsTArray<uint8_t> ReadFileToBuffer(const char* aFilename) {
  FILE* f = fopen(aFilename, "rb");
  if (!f) {
    return {};
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size <= 0) {
    fclose(f);
    return {};
  }
  fseek(f, 0, SEEK_SET);
  nsTArray<uint8_t> buffer;
  buffer.SetLength(size);
  size_t bytesRead = fread(buffer.Elements(), 1, size, f);
  fclose(f);
  if (bytesRead != static_cast<size_t>(size)) {
    return {};
  }
  return buffer;
}

// Scan for "mdhd" box types and overwrite timescale fields with aNewTimescale.
static bool PatchMdhdTimescales(nsTArray<uint8_t>& aBuffer,
                                uint32_t aNewTimescale) {
  const uint8_t kMdhd[] = {'m', 'd', 'h', 'd'};
  bool patched = false;
  // mdhd fullbox layout after the 4-byte type field:
  //   v0: ver/flags(4) + creation(4) + modification(4) + timescale(4) = 16
  //   v1: ver/flags(4) + creation(8) + modification(8) + timescale(4) = 24
  // The loop guard ensures aBuffer[i + 4] (version byte) is valid;
  // offset + 4 is checked below to cover the full timescale field.
  for (size_t i = 0; i + 4 < aBuffer.Length(); i++) {
    if (memcmp(aBuffer.Elements() + i, kMdhd, 4) != 0) {
      continue;
    }
    uint8_t version = aBuffer[i + 4];
    size_t offset = (version == 0) ? i + 16 : i + 24;
    if (offset + 4 > aBuffer.Length()) {
      continue;
    }
    aBuffer[offset + 0] = (aNewTimescale >> 24) & 0xFF;
    aBuffer[offset + 1] = (aNewTimescale >> 16) & 0xFF;
    aBuffer[offset + 2] = (aNewTimescale >> 8) & 0xFF;
    aBuffer[offset + 3] = aNewTimescale & 0xFF;
    patched = true;
  }
  return patched;
}

// 1833896.mp4 has mdhd timescale 0xF800001E which exceeds INT32_MAX.
// ISO 14496-12 defines mdhd timescale as unsigned int(32), so the value is
// preserved as-is. With such a large timescale, all 73 samples (ticks 0-72)
// map to 0 microseconds, producing duplicate timestamps at the demuxer level.
TEST(MP4Demuxer, DuplicateTimestampsWithLargeTimescale)
{
  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding("1833896.mp4");

  binding->RunTestAndWait([binding]() {
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->CheckTrackSamples(binding->mVideoTrack)
        ->Then(
            binding->mTaskQueue, __func__,
            [binding]() {
              EXPECT_GT(binding->mSamples.Length(), 1u);
              for (uint32_t i = 0; i < binding->mSamples.Length(); i++) {
                EXPECT_EQ(binding->mSamples[i]->mTime.ToMicroseconds(), 0)
                    << "Sample " << i
                    << " should map to 0 us with large timescale";
              }
              binding->mTaskQueue->BeginShutdown();
            },
            DO_FAIL);
  });
}

// Patch gizmo-frag.mp4's mdhd timescale to 0xF800001E. Per ISO 14496-12 the
// full uint32 range is valid, so the timescale is preserved, producing
// duplicate timestamps. Original ticks are 0-177000 (step 3000, 60 samples).
// With timescale ~4.16 billion, every ~3rd consecutive pair shares a
// microsecond value, yielding 43 unique timestamps from 60 samples.
TEST(MP4Demuxer, DuplicateTimestampsWithLargeTimescaleFragmented)
{
  nsTArray<uint8_t> buffer = ReadFileToBuffer("gizmo-frag.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  ASSERT_TRUE(PatchMdhdTimescales(buffer, 0xF800001E));

  RefPtr<BufferMediaResource> bufferResource =
      new BufferMediaResource(buffer.Elements(), buffer.Length());
  RefPtr<MP4DemuxerBinding> binding = new MP4DemuxerBinding(bufferResource);

  binding->RunTestAndWait([binding]() {
    binding->mVideoTrack =
        binding->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    binding->CheckTrackSamples(binding->mVideoTrack)
        ->Then(
            binding->mTaskQueue, __func__,
            [binding]() {
              EXPECT_EQ(binding->mSamples.Length(), 60u);
              size_t duplicates = 0;
              for (uint32_t i = 1; i < binding->mSamples.Length(); i++) {
                if (binding->mSamples[i]->mTime.ToMicroseconds() ==
                    binding->mSamples[i - 1]->mTime.ToMicroseconds()) {
                  duplicates++;
                }
              }
              EXPECT_EQ(duplicates, 17u);
              binding->mTaskQueue->BeginShutdown();
            },
            DO_FAIL);
  });
}

// Helpers for synthetic MP4VideoInfo::Update() tests.
static Mp4parseTrackInfo MakeTrackInfo() {
  Mp4parseTrackInfo track{};
  track.track_id = 1;
  track.duration = 1000;
  track.time_scale = 1000;
  track.media_time = 0;
  return track;
}

static Mp4parseTrackVideoInfo MakeVideoInfo(
    Mp4parseTrackVideoSampleInfo* aSampleInfo) {
  Mp4parseTrackVideoInfo video{};
  video.display_width = 1920;
  video.display_height = 1080;
  video.rotation = 0;
  video.sample_info_count = 1;
  video.sample_info = aSampleInfo;
  return video;
}

TEST(MP4Demuxer, VideoInfoDisplaySizeBound)
{
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_VP9;
  si.image_width = 1920;
  si.image_height = 1080;

  Mp4parseTrackInfo track = MakeTrackInfo();

  // A display size with both dimensions within the maximum image dimension and
  // a total area at the maximum video area is accepted (16384 * 2304 ==
  // MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT).
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 16384;
    video.display_height = 2304;
    MP4VideoInfo info;
    EXPECT_TRUE(NS_SUCCEEDED(info.Update(&track, &video).Code()));
  }

  // A display area one past the maximum video area is rejected by the area
  // bound alone: both dimensions stay within the maximum image dimension, so
  // only the width * height limit can reject it, unlike the per-axis cases
  // below.
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 16384;
    video.display_height = 2305;
    MP4VideoInfo info;
    EXPECT_EQ(NS_ERROR_DOM_MEDIA_METADATA_ERR,
              info.Update(&track, &video).Code());
  }

  // A width one past the maximum image dimension is rejected on its own, even
  // when the total area is within bounds.
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 16385;
    video.display_height = 1;
    MP4VideoInfo info;
    EXPECT_EQ(NS_ERROR_DOM_MEDIA_METADATA_ERR,
              info.Update(&track, &video).Code());
  }

  // A height one past the maximum image dimension is rejected on its own, even
  // when the total area is within bounds.
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 1;
    video.display_height = 16385;
    MP4VideoInfo info;
    EXPECT_EQ(NS_ERROR_DOM_MEDIA_METADATA_ERR,
              info.Update(&track, &video).Code());
  }

  // A zero display dimension is rejected.
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 0;
    video.display_height = 1080;
    MP4VideoInfo info;
    EXPECT_EQ(NS_ERROR_DOM_MEDIA_METADATA_ERR,
              info.Update(&track, &video).Code());
  }

  // A display size far beyond the maximum image dimension is rejected.
  {
    Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);
    video.display_width = 65535;
    video.display_height = 65535;
    MP4VideoInfo info;
    EXPECT_EQ(NS_ERROR_DOM_MEDIA_METADATA_ERR,
              info.Update(&track, &video).Code());
  }
}

TEST(MP4Demuxer, VideoInfoMdcvClli)
{
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_VP9;
  si.image_width = 1920;
  si.image_height = 1080;

  // mdcv: R(34000/50000, 16000/50000) G(13250/50000, 34500/50000)
  //       B(7500/50000, 3000/50000) WP(15635/50000, 16450/50000)
  //       maxLum=10000000/10000 minLum=50/10000
  si.has_mastering_display = true;
  si.mastering_display.display_primaries_x[0] = 34000;  // R
  si.mastering_display.display_primaries_y[0] = 16000;
  si.mastering_display.display_primaries_x[1] = 13250;  // G
  si.mastering_display.display_primaries_y[1] = 34500;
  si.mastering_display.display_primaries_x[2] = 7500;  // B
  si.mastering_display.display_primaries_y[2] = 3000;
  si.mastering_display.white_point_x = 15635;
  si.mastering_display.white_point_y = 16450;
  si.mastering_display.max_display_mastering_luminance = 10000000;
  si.mastering_display.min_display_mastering_luminance = 50;

  // clli: MaxCLL=1000, MaxFALL=400
  si.has_content_light_level = true;
  si.content_light_level.max_content_light_level = 1000;
  si.content_light_level.max_pic_average_light_level = 400;

  Mp4parseTrackInfo track = MakeTrackInfo();
  Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);

  MP4VideoInfo info;
  ASSERT_NS_SUCCEEDED(info.Update(&track, &video));

  ASSERT_TRUE(info.mHDRMetadata.isSome());

  ASSERT_TRUE(info.mHDRMetadata->mSmpte2086.isSome());
  const auto& smpte = info.mHDRMetadata->mSmpte2086.value();
  EXPECT_FLOAT_EQ(smpte.displayPrimaryRed.x, 34000.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.displayPrimaryRed.y, 16000.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.displayPrimaryGreen.x, 13250.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.displayPrimaryGreen.y, 34500.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.displayPrimaryBlue.x, 7500.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.displayPrimaryBlue.y, 3000.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.whitePoint.x, 15635.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.whitePoint.y, 16450.0f / 50000.0f);
  EXPECT_FLOAT_EQ(smpte.maxLuminance, 10000000.0f / 10000.0f);
  EXPECT_FLOAT_EQ(smpte.minLuminance, 50.0f / 10000.0f);

  ASSERT_TRUE(info.mHDRMetadata->mContentLightLevel.isSome());
  const auto& cll = info.mHDRMetadata->mContentLightLevel.value();
  EXPECT_EQ(cll.maxContentLightLevel, 1000u);
  EXPECT_EQ(cll.maxFrameAverageLightLevel, 400u);
}

TEST(MP4Demuxer, VideoInfoMdcvOnly)
{
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_HEVC;
  si.image_width = 3840;
  si.image_height = 2160;
  si.has_mastering_display = true;
  si.mastering_display.display_primaries_x[0] = 17000;
  si.mastering_display.display_primaries_y[0] = 8000;
  si.mastering_display.display_primaries_x[1] = 13250;
  si.mastering_display.display_primaries_y[1] = 34500;
  si.mastering_display.display_primaries_x[2] = 7500;
  si.mastering_display.display_primaries_y[2] = 3000;
  si.mastering_display.white_point_x = 15635;
  si.mastering_display.white_point_y = 16450;
  si.mastering_display.max_display_mastering_luminance = 10000000;
  si.mastering_display.min_display_mastering_luminance = 1;

  Mp4parseTrackInfo track = MakeTrackInfo();
  Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);

  MP4VideoInfo info;
  ASSERT_NS_SUCCEEDED(info.Update(&track, &video));

  ASSERT_TRUE(info.mHDRMetadata.isSome());
  EXPECT_TRUE(info.mHDRMetadata->mSmpte2086.isSome());
  EXPECT_TRUE(info.mHDRMetadata->mContentLightLevel.isSome());
}

TEST(MP4Demuxer, VideoInfoClliOnly)
{
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_VP9;
  si.image_width = 1920;
  si.image_height = 1080;
  si.has_content_light_level = true;
  si.content_light_level.max_content_light_level = 500;
  si.content_light_level.max_pic_average_light_level = 200;

  Mp4parseTrackInfo track = MakeTrackInfo();
  Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);

  MP4VideoInfo info;
  ASSERT_NS_SUCCEEDED(info.Update(&track, &video));

  ASSERT_TRUE(info.mHDRMetadata.isSome());
  EXPECT_TRUE(info.mHDRMetadata->mSmpte2086.isSome());
  ASSERT_TRUE(info.mHDRMetadata->mContentLightLevel.isSome());
  const auto& cll = info.mHDRMetadata->mContentLightLevel.value();
  EXPECT_EQ(cll.maxContentLightLevel, 500u);
  EXPECT_EQ(cll.maxFrameAverageLightLevel, 200u);
}

TEST(MP4Demuxer, VideoInfoNoHDR)
{
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_AVC;
  si.image_width = 1920;
  si.image_height = 1080;

  Mp4parseTrackInfo track = MakeTrackInfo();
  Mp4parseTrackVideoInfo video = MakeVideoInfo(&si);

  MP4VideoInfo info;
  ASSERT_NS_SUCCEEDED(info.Update(&track, &video));
  EXPECT_TRUE(info.mHDRMetadata.isNothing());
}

#undef DO_FAIL

static Mp4parseTrackVideoSampleInfo MakeSampleInfo(bool aHasColourInfo,
                                                   uint8_t aCp, uint8_t aTc,
                                                   uint8_t aMc,
                                                   bool aFullRange) {
  Mp4parseTrackVideoSampleInfo si{};
  si.codec_type = MP4PARSE_CODEC_AV1;
  si.image_width = 1920;
  si.image_height = 1080;
  si.has_colour_info = aHasColourInfo;
  si.colour_primaries = aCp;
  si.transfer_characteristics = aTc;
  si.matrix_coefficients = aMc;
  si.full_range_flag = aFullRange;
  return si;
}

// HDR10: cp=9 (BT.2020), tc=16 (PQ), mc=9, limited range
TEST(MP4Demuxer, VideoInfoNclxHDR10)
{
  auto si = MakeSampleInfo(true, 9, 16, 9, false);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Some(gfx::TransferFunction::PQ));
  EXPECT_EQ(info.mColorPrimaries, Some(gfx::ColorSpace2::BT2020));
  EXPECT_EQ(info.mColorSpace, Some(gfx::YUVColorSpace::BT2020));
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::LIMITED);
}

// HDR10 full-range: cp=9, tc=16, mc=9, full range
TEST(MP4Demuxer, VideoInfoNclxHDR10FullRange)
{
  auto si = MakeSampleInfo(true, 9, 16, 9, true);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Some(gfx::TransferFunction::PQ));
  EXPECT_EQ(info.mColorPrimaries, Some(gfx::ColorSpace2::BT2020));
  EXPECT_EQ(info.mColorSpace, Some(gfx::YUVColorSpace::BT2020));
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::FULL);
}

// HLG: cp=9, tc=18, mc=9, limited range
TEST(MP4Demuxer, VideoInfoNclxHLG)
{
  auto si = MakeSampleInfo(true, 9, 18, 9, false);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Some(gfx::TransferFunction::HLG));
  EXPECT_EQ(info.mColorPrimaries, Some(gfx::ColorSpace2::BT2020));
  EXPECT_EQ(info.mColorSpace, Some(gfx::YUVColorSpace::BT2020));
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::LIMITED);
}

// HLG full-range: cp=9, tc=18, mc=9, full range
TEST(MP4Demuxer, VideoInfoNclxHLGFullRange)
{
  auto si = MakeSampleInfo(true, 9, 18, 9, true);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Some(gfx::TransferFunction::HLG));
  EXPECT_EQ(info.mColorPrimaries, Some(gfx::ColorSpace2::BT2020));
  EXPECT_EQ(info.mColorSpace, Some(gfx::YUVColorSpace::BT2020));
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::FULL);
}

// RGB/Identity: cp=1 (BT.709), tc=1, mc=0 (Identity), limited range
TEST(MP4Demuxer, VideoInfoNclxRGBIdentity)
{
  auto si = MakeSampleInfo(true, 1, 1, 0, false);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Some(gfx::TransferFunction::BT709));
  EXPECT_EQ(info.mColorPrimaries, Some(gfx::ColorSpace2::BT709));
  EXPECT_EQ(info.mColorSpace, Some(gfx::YUVColorSpace::Identity));
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::LIMITED);
}

// No colr box: has_colour_info=false, colour fields must stay Nothing()
TEST(MP4Demuxer, VideoInfoNclxAbsent)
{
  auto si = MakeSampleInfo(false, 0, 0, 0, false);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Nothing());
  EXPECT_EQ(info.mColorPrimaries, Nothing());
  EXPECT_EQ(info.mColorSpace, Nothing());
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::LIMITED);
}

// Unrecognised CICP values: colour fields with no gecko mapping stay Nothing()
TEST(MP4Demuxer, VideoInfoNclxUnrecognisedCICP)
{
  auto si = MakeSampleInfo(true, 22, 22, 22, false);
  auto vi = MakeVideoInfo(&si);
  auto ti = MakeTrackInfo();
  MP4VideoInfo info;
  EXPECT_TRUE(NS_SUCCEEDED(info.Update(&ti, &vi)));
  EXPECT_EQ(info.mTransferFunction, Nothing());
  EXPECT_EQ(info.mColorPrimaries, Nothing());
  EXPECT_EQ(info.mColorSpace, Nothing());
  EXPECT_EQ(info.mColorRange, gfx::ColorRange::LIMITED);
}
