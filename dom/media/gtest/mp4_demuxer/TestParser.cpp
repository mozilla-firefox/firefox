/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BufferStream.h"
#include "MP4Metadata.h"
#include "MediaData.h"
#include "MoofParser.h"
#include "SampleIterator.h"
#include "TelemetryFixture.h"
#include "TelemetryTestHelpers.h"
#include "gtest/gtest.h"
#include "js/Conversions.h"
#include "mozilla/Preferences.h"
#include "mozilla/gtest/MozAssertions.h"

class TestStream;
namespace mozilla {
DDLoggedTypeNameAndBase(::TestStream, ByteStream);
}  // namespace mozilla

using namespace mozilla;

static const uint32_t E = MP4Metadata::NumberTracksError();

class TestStream : public ByteStream,
                   public DecoderDoctorLifeLogger<TestStream> {
 public:
  TestStream(const uint8_t* aBuffer, size_t aSize)
      : mHighestSuccessfulEndOffset(0), mBuffer(aBuffer), mSize(aSize) {}
  nsresult ReadAt(int64_t aOffset, void* aData, size_t aLength,
                  size_t* aBytesRead) override {
    if (aOffset < 0 || aOffset > static_cast<int64_t>(mSize)) {
      return NS_ERROR_DOM_MEDIA_RANGE_ERR;
    }
    // After the test, 0 <= aOffset <= mSize <= SIZE_MAX, so it's safe to cast
    // to size_t.
    size_t offset = static_cast<size_t>(aOffset);
    // Don't read past the end (but it's not an error to try).
    if (aLength > mSize - offset) {
      aLength = mSize - offset;
    }
    // Now, 0 <= offset <= offset + aLength <= mSize <= SIZE_MAX.
    *aBytesRead = aLength;
    memcpy(aData, mBuffer + offset, aLength);
    if (mHighestSuccessfulEndOffset < offset + aLength) {
      mHighestSuccessfulEndOffset = offset + aLength;
    }
    return NS_OK;
  }
  nsresult CachedReadAt(int64_t aOffset, void* aData, size_t aLength,
                        size_t* aBytesRead) override {
    return ReadAt(aOffset, aData, aLength, aBytesRead);
  }
  bool Length(int64_t* aLength) override {
    *aLength = mSize;
    return true;
  }
  void DiscardBefore(int64_t aOffset) override {}

  // Offset past the last character ever read. 0 when nothing read yet.
  size_t mHighestSuccessfulEndOffset;

 protected:
  virtual ~TestStream() = default;

  const uint8_t* mBuffer;
  size_t mSize;
};

TEST(MP4Metadata, EmptyStream)
{
  RefPtr<ByteStream> stream = new TestStream(nullptr, 0);

  MP4Metadata::ResultAndByteBuffer metadataBuffer =
      MP4Metadata::Metadata(stream);
  EXPECT_TRUE(NS_OK != metadataBuffer.Result());
  EXPECT_FALSE(static_cast<bool>(metadataBuffer.Ref()));

  MP4Metadata metadata(stream);
  EXPECT_TRUE(0u ==
                  metadata.GetNumberTracks(TrackInfo::kUndefinedTrack).Ref() ||
              E == metadata.GetNumberTracks(TrackInfo::kUndefinedTrack).Ref());
  EXPECT_TRUE(0u == metadata.GetNumberTracks(TrackInfo::kAudioTrack).Ref() ||
              E == metadata.GetNumberTracks(TrackInfo::kAudioTrack).Ref());
  EXPECT_TRUE(0u == metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref() ||
              E == metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref());
  EXPECT_TRUE(0u == metadata.GetNumberTracks(TrackInfo::kTextTrack).Ref() ||
              E == metadata.GetNumberTracks(TrackInfo::kTextTrack).Ref());
  EXPECT_FALSE(metadata.GetTrackInfo(TrackInfo::kAudioTrack, 0).Ref());
  EXPECT_FALSE(metadata.GetTrackInfo(TrackInfo::kVideoTrack, 0).Ref());
  EXPECT_FALSE(metadata.GetTrackInfo(TrackInfo::kTextTrack, 0).Ref());
  // We can seek anywhere in any MPEG4.
  EXPECT_TRUE(metadata.CanSeek());
  EXPECT_FALSE(metadata.Crypto().Ref()->valid);
}

TEST(MoofParser, EmptyStream)
{
  RefPtr<ByteStream> stream = new TestStream(nullptr, 0);

  MoofParser parser(stream, AsVariant(ParseAllTracks{}), false);
  EXPECT_EQ(0u, parser.mOffset);
  EXPECT_TRUE(parser.ReachedEnd());

  MediaByteRangeSet byteRanges;
  EXPECT_FALSE(parser.RebuildFragmentedIndex(byteRanges));

  EXPECT_TRUE(parser.GetCompositionRange(byteRanges).IsNull());
  EXPECT_TRUE(parser.mInitRange.IsEmpty());
  EXPECT_EQ(0u, parser.mOffset);
  EXPECT_TRUE(parser.ReachedEnd());
  RefPtr<MediaByteBuffer> metadataBuffer = parser.Metadata();
  EXPECT_FALSE(metadataBuffer);
  EXPECT_TRUE(parser.FirstCompleteMediaSegment().IsEmpty());
  EXPECT_TRUE(parser.FirstCompleteMediaHeader().IsEmpty());
}

nsTArray<uint8_t> ReadTestFile(const char* aFilename) {
  if (!aFilename) {
    return {};
  }
  FILE* f = fopen(aFilename, "rb");
  if (!f) {
    return {};
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return {};
  }
  long position = ftell(f);
  // I know EOF==-1, so this test is made obsolete by '<0', but I don't want
  // the code to rely on that.
  if (position == 0 || position == EOF || position < 0) {
    fclose(f);
    return {};
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return {};
  }

  size_t len = static_cast<size_t>(position);
  nsTArray<uint8_t> buffer(len);
  buffer.SetLength(len);
  size_t read = fread(buffer.Elements(), 1, len, f);
  fclose(f);
  if (read != len) {
    return {};
  }

  return buffer;
}

struct TestFileData {
  const char* mFilename;
  bool mParseResult;
  uint32_t mNumberVideoTracks;
  bool mHasVideoIndice;
  double mVideoDuration;  // For first video track, -1 if N/A, in seconds.
  int32_t mWidth;
  int32_t mHeight;
  uint32_t mNumberAudioTracks;
  double mAudioDuration;  // For first audio track, -1 if N/A, in seconds.
  bool mHasCrypto;  // Note, MP4Metadata only considers pssh box for crypto.
  uint64_t mParsedOffset;  // or 0 for the end.
  bool mValidMoofForTrack1;
  bool mValidMoofForAllTracks;
  int8_t mAudioProfile;
};

static const TestFileData testFiles[] = {
    // filename parses? #V hasVideoIndex vDur w h #A aDur hasCrypto? moofOffset
    // validMoof1? validMoofAll? audio_profile
    {"test_case_1156505.mp4", false, 0, false, -1, 0, 0, 0, -1., false, 152,
     false, false, 0},  // invalid ''trak box
    {"test_case_1181213.mp4", true, 1, true, 0.41699219, 320, 240, 1,
     0.44099772, true, 0, false, false, 2},
    {"test_case_1181215.mp4", true, 0, false, -1, 0, 0, 0, -1, false, 0, false,
     false, 0},
    {"test_case_1181223.mp4", false, 0, false, 0.41666666, 320, 240, 0, -1,
     false, 0, false, false, 0},
#if 0
    // Test disabled due to unsupported audio type (possibly corrupt track).
    // This file was used to verify that the parser rejected the file entirely,
    // but it is now accepted with the audio track ignored.
    {"test_case_1181719.mp4", false, 0, false, -1, 0, 0, 0, -1, false, 0, false,
     false, 0},
#endif
    {"test_case_1185230.mp4", true, 2, true, 0.41699219, 320, 240, 2,
     0.44100001, false, 0, false, false, 2},
    {"test_case_1187067.mp4", true, 1, true, 0.080000, 160, 90, 0, -1, false, 0,
     false, false, 0},
    {"test_case_1200326.mp4", false, 0, false, -1, 0, 0, 0, -1, false, 0, false,
     false, 0},
    {"test_case_1204580.mp4", true, 1, true, 0.50300002, 320, 180, 0, -1, false,
     0, false, false, 0},
    {"test_case_1216748.mp4", false, 0, false, -1, 0, 0, 0, -1, false, 152,
     false, false, 0},  // invalid 'trak' box
    {"test_case_1296473.mp4", false, 0, false, -1, 0, 0, 0, -1, false, 0, false,
     false, 0},
    {"test_case_1296532.mp4", true, 1, true, 5.589333, 560, 320, 1, 5.589333,
     true, 0, true, false, 2},
    {"test_case_1301065.mp4", true, 0, false, -1, 0, 0, 1, 100079991719, false,
     0, false, false, 2},
    {"test_case_1301065-u32max.mp4", true, 0, false, -1, 0, 0, 1, 97391.548639,
     false, 0, false, false, 2},
    {"test_case_1301065-max-ez.mp4", true, 0, false, -1, 0, 0, 1,
     209146758.205306, false, 0, false, false, 2},
    {"test_case_1301065-harder.mp4", true, 0, false, -1, 0, 0, 1,
     209146758.205328, false, 0, false, false, 2},
    {"test_case_1301065-max-ok.mp4", true, 0, false, -1, 0, 0, 1,
     9223372036854.775, false, 0, false, false, 2},
    // The duration is overflow for int64_t in TestFileData, parser uses
    // uint64_t so
    // this file is ignore.
    //{ "test_case_1301065-overfl.mp4", 0, -1,   0,   0, 1, 9223372036854775827,
    //                                                          false,   0,
    //                                                          false, 2
    //                                                          },
    {"test_case_1301065-i64max.mp4", true, 0, false, -1, 0, 0, 1,
     std::numeric_limits<double>::infinity(), false, 0, false, false, 2},
    {"test_case_1301065-i64min.mp4", true, 0, false, -1, 0, 0, 1,
     -std::numeric_limits<double>::infinity(), false, 0, false, false, 2},
    {"test_case_1301065-u64max.mp4", true, 0, false, -1, 0, 0, 1, 0, false, 0,
     false, false, 2},
    {"test_case_1329061.mov", false, 0, false, -1, 0, 0, 1, 234567981, false, 0,
     false, false, 2},
    {"test_case_1351094.mp4", true, 0, false, -1, 0, 0, 0, -1, false, 0, false,
     false, 0},
    {"test_case_1389299.mp4", true, 1, true, 5.589333, 560, 320, 1, 5.589333,
     true, 0, true, false, 2},

    {"test_case_1389527.mp4", true, 1, false, 5.005000, 80, 128, 1, 4.992000,
     false, 0, false, false, 2},
    {"test_case_1395244.mp4", true, 1, true, 0.41699219, 320, 240, 1,
     0.44099772, false, 0, false, false, 2},
    {"test_case_1388991.mp4", true, 0, false, -1, 0, 0, 1, 30.000181, false, 0,
     false, false, 2},
    {"test_case_1410565.mp4", false, 0, false, 0, 0, 0, 0, 0, false, 0, false,
     false, 2},  // negative 'timescale'
    {"test_case_1513651-2-sample-description-entries.mp4", true, 1, true,
     9.843344, 400, 300, 0, -1, true, 0, false, false, 0},
    {"test_case_1519617-cenc-init-with-track_id-0.mp4", true, 1, true, 0, 1272,
     530, 0, -1, false, 0, false, false,
     0},  // Uses bad track id 0 and has a sinf but no pssh
    {"test_case_1519617-track2-trafs-removed.mp4", true, 1, true, 10.032000,
     400, 300, 1, 10.032000, false, 0, true, false, 2},
    {"test_case_1519617-video-has-track_id-0.mp4", true, 1, true, 10.032000,
     400, 300, 1, 10.032000, false, 0, false, false, 2},  // Uses bad track id 0
    // The following file has multiple sample description entries with the same
    // crypto information. This does not cover multiple entries with different
    // crypto information which is tracked by
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1714626
    {"test_case_1714125-2-sample-description-entires-with-identical-crypto.mp4",
     true, 1, true, 0, 1920, 1080, 0, 0, true, 0, false, false, 0},
};

TEST(MP4Metadata, test_case_mp4)
{
  const TestFileData* tests = nullptr;
  size_t length = 0;

  tests = testFiles;
  length = std::size(testFiles);

  for (size_t test = 0; test < length; ++test) {
    nsTArray<uint8_t> buffer = ReadTestFile(tests[test].mFilename);
    ASSERT_FALSE(buffer.IsEmpty());
    RefPtr<ByteStream> stream =
        new TestStream(buffer.Elements(), buffer.Length());

    MP4Metadata::ResultAndByteBuffer metadataBuffer =
        MP4Metadata::Metadata(stream);
    EXPECT_EQ(NS_OK, metadataBuffer.Result());
    EXPECT_TRUE(metadataBuffer.Ref());

    MP4Metadata metadata(stream);
    nsresult res = metadata.Parse();
    EXPECT_EQ(tests[test].mParseResult, NS_SUCCEEDED(res))
        << tests[test].mFilename;
    if (!tests[test].mParseResult) {
      continue;
    }

    EXPECT_EQ(tests[test].mNumberAudioTracks,
              metadata.GetNumberTracks(TrackInfo::kAudioTrack).Ref())
        << tests[test].mFilename;
    EXPECT_EQ(tests[test].mNumberVideoTracks,
              metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref())
        << tests[test].mFilename;
    // If there is an error, we should expect an error code instead of zero
    // for non-Audio/Video tracks.
    const uint32_t None = (tests[test].mNumberVideoTracks == E) ? E : 0;
    EXPECT_EQ(None, metadata.GetNumberTracks(TrackInfo::kUndefinedTrack).Ref())
        << tests[test].mFilename;
    EXPECT_EQ(None, metadata.GetNumberTracks(TrackInfo::kTextTrack).Ref())
        << tests[test].mFilename;
    EXPECT_FALSE(metadata.GetTrackInfo(TrackInfo::kUndefinedTrack, 0).Ref());
    MP4Metadata::ResultAndTrackInfo trackInfo =
        metadata.GetTrackInfo(TrackInfo::kVideoTrack, 0);
    if (!!tests[test].mNumberVideoTracks) {
      ASSERT_TRUE(!!trackInfo.Ref());
      const VideoInfo* videoInfo = trackInfo.Ref()->GetAsVideoInfo();
      ASSERT_TRUE(!!videoInfo);
      EXPECT_TRUE(videoInfo->IsValid()) << tests[test].mFilename;
      EXPECT_TRUE(videoInfo->IsVideo()) << tests[test].mFilename;
      if (std::isinf(tests[test].mVideoDuration)) {
        ASSERT_TRUE(std::isinf(videoInfo->mDuration.ToSeconds()));
      } else {
        EXPECT_FLOAT_EQ(tests[test].mVideoDuration,
                        videoInfo->mDuration.ToSeconds())
            << tests[test].mFilename;
      }
      EXPECT_EQ(tests[test].mWidth, videoInfo->mDisplay.width)
          << tests[test].mFilename;
      EXPECT_EQ(tests[test].mHeight, videoInfo->mDisplay.height)
          << tests[test].mFilename;

      MP4Metadata::ResultAndIndice indices =
          metadata.GetTrackIndice(videoInfo->mTrackId);
      EXPECT_EQ(!!indices.Ref(), tests[test].mHasVideoIndice)
          << tests[test].mFilename;
      if (tests[test].mHasVideoIndice) {
        for (size_t i = 0; i < indices.Ref()->Length(); i++) {
          MP4SampleIndex::Indice data;
          EXPECT_TRUE(indices.Ref()->GetIndice(i, data))
              << tests[test].mFilename;
          EXPECT_TRUE(data.start_offset <= data.end_offset)
              << tests[test].mFilename;
          EXPECT_TRUE(data.start_composition <= data.end_composition)
              << tests[test].mFilename;
        }
      }
    }
    trackInfo = metadata.GetTrackInfo(TrackInfo::kAudioTrack, 0);
    if (tests[test].mNumberAudioTracks == 0 ||
        tests[test].mNumberAudioTracks == E) {
      EXPECT_TRUE(!trackInfo.Ref()) << tests[test].mFilename;
    } else {
      ASSERT_TRUE(!!trackInfo.Ref());
      const AudioInfo* audioInfo = trackInfo.Ref()->GetAsAudioInfo();
      ASSERT_TRUE(!!audioInfo);
      EXPECT_TRUE(audioInfo->IsValid()) << tests[test].mFilename;
      EXPECT_TRUE(audioInfo->IsAudio()) << tests[test].mFilename;
      if (std::isinf(tests[test].mAudioDuration)) {
        ASSERT_TRUE(std::isinf(audioInfo->mDuration.ToSeconds()))
        << tests[test].mFilename;
      } else {
        EXPECT_FLOAT_EQ(tests[test].mAudioDuration,
                        audioInfo->mDuration.ToSeconds())
            << tests[test].mFilename;
      }
      EXPECT_EQ(tests[test].mAudioProfile, audioInfo->mProfile)
          << tests[test].mFilename;

      MP4Metadata::ResultAndIndice indices =
          metadata.GetTrackIndice(audioInfo->mTrackId);
      EXPECT_TRUE(!!indices.Ref()) << tests[test].mFilename;
      for (size_t i = 0; i < indices.Ref()->Length(); i++) {
        MP4SampleIndex::Indice data;
        EXPECT_TRUE(indices.Ref()->GetIndice(i, data)) << tests[test].mFilename;
        EXPECT_TRUE(data.start_offset <= data.end_offset)
            << tests[test].mFilename;
        EXPECT_TRUE(int64_t(data.start_composition) <=
                    int64_t(data.end_composition))
            << tests[test].mFilename;
      }
    }
    EXPECT_FALSE(metadata.GetTrackInfo(TrackInfo::kTextTrack, 0).Ref())
        << tests[test].mFilename;
    // We can see anywhere in any MPEG4.
    EXPECT_TRUE(metadata.CanSeek()) << tests[test].mFilename;
    EXPECT_EQ(tests[test].mHasCrypto, metadata.Crypto().Ref()->valid)
        << tests[test].mFilename;
  }
}

// This test was disabled by Bug 1224019 for producing way too much output.
// This test no longer produces such output, as we've moved away from
// stagefright, but it does take a long time to run. I can be useful to enable
// as a sanity check on changes to the parser, but is too taxing to run as part
// of normal test execution.
#if 0
TEST(MP4Metadata, test_case_mp4_subsets) {
  static const size_t step = 1u;
  for (size_t test = 0; test < std::size(testFiles); ++test) {
    nsTArray<uint8_t> buffer = ReadTestFile(testFiles[test].mFilename);
    ASSERT_FALSE(buffer.IsEmpty());
    ASSERT_LE(step, buffer.Length());
    // Just exercizing the parser starting at different points through the file,
    // making sure it doesn't crash.
    // No checks because results would differ for each position.
    for (size_t offset = 0; offset < buffer.Length() - step; offset += step) {
      size_t size = buffer.Length() - offset;
      while (size > 0) {
        RefPtr<TestStream> stream =
          new TestStream(buffer.Elements() + offset, size);

        MP4Metadata::ResultAndByteBuffer metadataBuffer =
          MP4Metadata::Metadata(stream);
        MP4Metadata metadata(stream);

        if (stream->mHighestSuccessfulEndOffset <= 0) {
          // No successful reads -> Cutting down the size won't change anything.
          break;
        }
        if (stream->mHighestSuccessfulEndOffset < size) {
          // Read up to a point before the end -> Resize down to that point.
          size = stream->mHighestSuccessfulEndOffset;
        } else {
          // Read up to the end (or after?!) -> Just cut 1 byte.
          size -= 1;
        }
      }
    }
  }
}
#endif

#if !defined(XP_WIN) || !defined(MOZ_ASAN)  // OOMs on Windows ASan
TEST(MoofParser, test_case_mp4)
{
  const TestFileData* tests = nullptr;
  size_t length = 0;

  tests = testFiles;
  length = std::size(testFiles);

  for (size_t test = 0; test < length; ++test) {
    nsTArray<uint8_t> buffer = ReadTestFile(tests[test].mFilename);
    ASSERT_FALSE(buffer.IsEmpty());
    RefPtr<ByteStream> stream =
        new TestStream(buffer.Elements(), buffer.Length());

    MoofParser parser(stream, AsVariant(ParseAllTracks{}), false);
    EXPECT_EQ(0u, parser.mOffset) << tests[test].mFilename;
    EXPECT_FALSE(parser.ReachedEnd()) << tests[test].mFilename;
    EXPECT_TRUE(parser.mInitRange.IsEmpty()) << tests[test].mFilename;

    RefPtr<MediaByteBuffer> metadataBuffer = parser.Metadata();
    EXPECT_TRUE(metadataBuffer) << tests[test].mFilename;

    EXPECT_FALSE(parser.mInitRange.IsEmpty()) << tests[test].mFilename;
    const MediaByteRangeSet byteRanges(
        MediaByteRange(0, int64_t(buffer.Length())));
    EXPECT_EQ(tests[test].mValidMoofForAllTracks,
              parser.RebuildFragmentedIndex(byteRanges))
        << tests[test].mFilename;
    if (tests[test].mParsedOffset == 0) {
      EXPECT_EQ(buffer.Length(), parser.mOffset) << tests[test].mFilename;
      EXPECT_TRUE(parser.ReachedEnd()) << tests[test].mFilename;
    } else {
      EXPECT_EQ(tests[test].mParsedOffset, parser.mOffset)
          << tests[test].mFilename;
      EXPECT_FALSE(parser.ReachedEnd()) << tests[test].mFilename;
    }

    EXPECT_FALSE(parser.mInitRange.IsEmpty()) << tests[test].mFilename;
    EXPECT_TRUE(parser.GetCompositionRange(byteRanges).IsNull())
        << tests[test].mFilename;
    EXPECT_TRUE(parser.FirstCompleteMediaSegment().IsEmpty())
        << tests[test].mFilename;
    // If we expect a valid moof we should have that moof's range stored.
    EXPECT_EQ(tests[test].mValidMoofForAllTracks,
              !parser.FirstCompleteMediaHeader().IsEmpty())
        << tests[test].mFilename;
  }
}

TEST(MoofParser, test_case_sample_description_entries)
{
  const TestFileData* tests = testFiles;
  size_t length = std::size(testFiles);

  for (size_t test = 0; test < length; ++test) {
    nsTArray<uint8_t> buffer = ReadTestFile(tests[test].mFilename);
    ASSERT_FALSE(buffer.IsEmpty());
    RefPtr<ByteStream> stream =
        new TestStream(buffer.Elements(), buffer.Length());

    // Parse the first track. Treating it as audio is hacky, but this doesn't
    // affect how we read the sample description entries.
    uint32_t trackNumber = 1;
    MoofParser parser(stream, AsVariant(trackNumber), false);
    EXPECT_EQ(0u, parser.mOffset) << tests[test].mFilename;
    EXPECT_FALSE(parser.ReachedEnd()) << tests[test].mFilename;
    EXPECT_TRUE(parser.mInitRange.IsEmpty()) << tests[test].mFilename;

    // Explicitly don't call parser.Metadata() so that the parser itself will
    // read the metadata as if we're in a fragmented case. Otherwise the parser
    // won't read the sample description table.

    const MediaByteRangeSet byteRanges(
        MediaByteRange(0, int64_t(buffer.Length())));
    EXPECT_EQ(tests[test].mValidMoofForTrack1,
              parser.RebuildFragmentedIndex(byteRanges))
        << tests[test].mFilename;

    // We only care about crypto data from the samples descriptions right now.
    // This test should be expanded should we read further information.
    if (tests[test].mHasCrypto) {
      uint32_t numEncryptedEntries = 0;
      // It's possible to have multiple sample description entries. Bug
      // 1714626 tracks more robust handling of multiple entries, for now just
      // check that we have at least one.
      for (SampleDescriptionEntry entry : parser.mSampleDescriptions) {
        if (entry.mIsEncryptedEntry) {
          numEncryptedEntries++;
        }
      }
      EXPECT_GE(numEncryptedEntries, 1u) << tests[test].mFilename;
    }
  }
}
#endif  // !defined(XP_WIN) || !defined(MOZ_ASAN)

// We should gracefully handle track_id 0 since Bug 1519617. We'd previously
// used id 0 to trigger special handling in the MoofParser to read multiple
// track metadata, but since muxers use track id 0 in the wild, we want to
// make sure they can't accidentally trigger such handling.
TEST(MoofParser, test_case_track_id_0_does_not_read_multitracks)
{
  const char* zeroTrackIdFileName =
      "test_case_1519617-video-has-track_id-0.mp4";
  nsTArray<uint8_t> buffer = ReadTestFile(zeroTrackIdFileName);

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  // Parse track id 0. We expect to only get metadata from that track, not the
  // other track with id 2.
  const uint32_t videoTrackId = 0;
  MoofParser parser(stream, AsVariant(videoTrackId), false);

  // Explicitly don't call parser.Metadata() so that the parser itself will
  // read the metadata as if we're in a fragmented case. Otherwise we won't
  // read the trak data.

  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_TRUE(parser.RebuildFragmentedIndex(byteRanges))
      << "MoofParser should find a valid moof as the file contains one!";

  // Verify we only have data from track 0, if we parsed multiple tracks we'd
  // find some of the audio track metadata here. Only check for values that
  // differ between tracks.
  const uint32_t videoTimescale = 90000;
  const uint32_t videoSampleDuration = 3000;
  const uint32_t videoSampleFlags = 0x10000;
  const uint32_t videoNumSampleDescriptionEntries = 1;
  EXPECT_EQ(videoTimescale, parser.mMdhd.mTimescale)
      << "Wrong timescale for video track! If value is 22050, we've read from "
         "the audio track!";
  EXPECT_EQ(videoTrackId, parser.mTrex.mTrackId)
      << "Wrong track id for video track! If value is 2, we've read from the "
         "audio track!";
  EXPECT_EQ(videoSampleDuration, parser.mTrex.mDefaultSampleDuration)
      << "Wrong sample duration for video track! If value is 1024, we've read "
         "from the audio track!";
  EXPECT_EQ(videoSampleFlags, parser.mTrex.mDefaultSampleFlags)
      << "Wrong sample flags for video track! If value is 0x2000000 (note "
         "that's hex), we've read from the audio track!";
  EXPECT_EQ(videoNumSampleDescriptionEntries,
            parser.mSampleDescriptions.Length())
      << "Wrong number of sample descriptions for video track! If value is 2, "
         "then we've read sample description information from video and audio "
         "tracks!";
}

// We should gracefully handle track_id 0 since Bug 1519617. This includes
// handling crypto data from the sinf box in the MoofParser. Note, as of the
// time of writing, MP4Metadata uses the presence of a pssh box to determine
// if its crypto member is valid. However, even on files where the pssh isn't
// in the init segment, the MoofParser should still read the sinf, as in this
// testcase.
TEST(MoofParser, test_case_track_id_0_reads_crypto_metadata)
{
  const char* zeroTrackIdFileName =
      "test_case_1519617-cenc-init-with-track_id-0.mp4";
  nsTArray<uint8_t> buffer = ReadTestFile(zeroTrackIdFileName);

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  // Parse track id 0. We expect to only get metadata from that track, not the
  // other track with id 2.
  const uint32_t videoTrackId = 0;
  MoofParser parser(stream, AsVariant(videoTrackId), false);

  // Explicitly don't call parser.Metadata() so that the parser itself will
  // read the metadata as if we're in a fragmented case. Otherwise we won't
  // read the trak data.

  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_FALSE(parser.RebuildFragmentedIndex(byteRanges))
      << "MoofParser should not find a valid moof, this is just an init "
         "segment!";

  // Verify we only have data from track 0, if we parsed multiple tracks we'd
  // find some of the audio track metadata here. Only check for values that
  // differ between tracks.
  const size_t numSampleDescriptionEntries = 1;
  const uint32_t defaultPerSampleIVSize = 8;
  const size_t keyIdLength = 16;
  const uint32_t defaultKeyId[keyIdLength] = {
      0x43, 0xbe, 0x13, 0xd0, 0x26, 0xc9, 0x41, 0x54,
      0x8f, 0xed, 0xf9, 0x54, 0x1a, 0xef, 0x6b, 0x0e};
  EXPECT_TRUE(parser.mSinf.IsValid())
      << "Should have a sinf that has crypto data!";
  EXPECT_EQ(defaultPerSampleIVSize, parser.mSinf.mDefaultIVSize)
      << "Wrong default per sample IV size for track! If 0 indicates we failed "
         "to parse some crypto info!";
  for (size_t i = 0; i < keyIdLength; i++) {
    EXPECT_EQ(defaultKeyId[i], parser.mSinf.mDefaultKeyID[i])
        << "Mismatched default key ID byte at index " << i
        << " indicates we failed to parse some crypto info!";
  }
  ASSERT_EQ(numSampleDescriptionEntries, parser.mSampleDescriptions.Length())
      << "Wrong number of sample descriptions for track! If 0, indicates we "
         "failed to parse some expected crypto!";
  EXPECT_TRUE(parser.mSampleDescriptions[0].mIsEncryptedEntry)
      << "Sample description should be marked as encrypted!";
}

// The MoofParser may be asked to parse metadata for multiple tracks, but then
// be presented with fragments/moofs that contain data for only a subset of
// those tracks. I.e. metadata contains information for tracks with ids 1 and 2,
// but then the moof parser only receives moofs with data for track id 1. We
// should parse such fragmented media. In this test the metadata contains info
// for track ids 1 and 2, but track 2's track fragment headers (traf) have been
// over written with free space boxes (free).
TEST(MoofParser, test_case_moofs_missing_trafs)
{
  const char* noTrafsForTrack2MoofsFileName =
      "test_case_1519617-track2-trafs-removed.mp4";
  nsTArray<uint8_t> buffer = ReadTestFile(noTrafsForTrack2MoofsFileName);

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  // Create parser that will read metadata from all tracks.
  MoofParser parser(stream, AsVariant(ParseAllTracks{}), false);

  // Explicitly don't call parser.Metadata() so that the parser itself will
  // read the metadata as if we're in a fragmented case. Otherwise we won't
  // read the trak data.

  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_TRUE(parser.RebuildFragmentedIndex(byteRanges))
      << "MoofParser should find a valid moof, there's 2 in the file!";

  // Verify we've found 2 moofs and that the parser was able to parse them.
  const size_t numMoofs = 2;
  EXPECT_EQ(numMoofs, parser.Moofs().Length())
      << "File has 2 moofs, we should have read both";
  for (size_t i = 0; i < parser.Moofs().Length(); i++) {
    EXPECT_TRUE(parser.Moofs()[i].IsValid()) << "All moofs should be valid";
  }
}

// This test was disabled by Bug 1224019 for producing way too much output.
// This test no longer produces such output, as we've moved away from
// stagefright, but it does take a long time to run. I can be useful to enable
// as a sanity check on changes to the parser, but is too taxing to run as part
// of normal test execution.
#if 0
TEST(MoofParser, test_case_mp4_subsets) {
  const size_t step = 1u;
  for (size_t test = 0; test < std::size(testFiles); ++test) {
    nsTArray<uint8_t> buffer = ReadTestFile(testFiles[test].mFilename);
    ASSERT_FALSE(buffer.IsEmpty());
    ASSERT_LE(step, buffer.Length());
    // Just exercizing the parser starting at different points through the file,
    // making sure it doesn't crash.
    // No checks because results would differ for each position.
    for (size_t offset = 0; offset < buffer.Length() - step; offset += step) {
      size_t size = buffer.Length() - offset;
      while (size > 0) {
        RefPtr<TestStream> stream =
          new TestStream(buffer.Elements() + offset, size);

        MoofParser parser(stream, AsVariant(ParseAllTracks{}), false);
        MediaByteRangeSet byteRanges;
        EXPECT_FALSE(parser.RebuildFragmentedIndex(byteRanges));
        parser.GetCompositionRange(byteRanges);
        RefPtr<MediaByteBuffer> metadataBuffer = parser.Metadata();
        parser.FirstCompleteMediaSegment();
        parser.FirstCompleteMediaHeader();

        if (stream->mHighestSuccessfulEndOffset <= 0) {
          // No successful reads -> Cutting down the size won't change anything.
          break;
        }
        if (stream->mHighestSuccessfulEndOffset < size) {
          // Read up to a point before the end -> Resize down to that point.
          size = stream->mHighestSuccessfulEndOffset;
        } else {
          // Read up to the end (or after?!) -> Just cut 1 byte.
          size -= 1;
        }
      }
    }
  }
}
#endif

// Bug 2004835 / Bug 2026875: fMP4 fragments whose tfdt encodes a negative
// pre-roll via unsigned overflow (2^64 - N, e.g. AAC encoder delay) must be
// accepted. The signed reinterpretation gives a small negative decode time
// that the existing edit-list mechanism handles as pre-roll.
TEST(MoofParser, overflow_tfdt)
{
  static const char* kTestFilename = "test_case_2004835-overflow-tfdt.mp4";
  nsTArray<uint8_t> buffer = ReadTestFile(kTestFilename);

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  // File has one track (video, ID=1) with tfdt = 2^64 - 2048.
  // After the fix this is reinterpreted as int64_t(-2048), which is valid for
  // CheckedInt64; the edit-list subtraction produces correct PTS values.
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_TRUE(parser.RebuildFragmentedIndex(byteRanges));
  EXPECT_FALSE(parser.Moofs().IsEmpty());
}

// Bug 2027038: AAC encoder delay is encoded as 2^64-N
// in tfdt.baseMediaDecodeTime; the matching edit list has mediaTime=-N.
// After the static_cast fix: decodeTime(-N) - mMediaStart(-N) = 0, so the
// first sample's presentation timestamp must be exactly 0.
TEST(MoofParser, overflow_tfdt_aac_encoder_delay)
{
  static const char* kTestFilename = "test_case_aac_encoder_delay_tfdt.mp4";
  nsTArray<uint8_t> buffer = ReadTestFile(kTestFilename);

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  // Audio track (ID=1), mdhd timescale=48000, tfdt=2^64-2048 (0xFFFFF800),
  // elst mediaTime=-2048. After fix: decodeTime=-2048, -2048-(-2048)=0.
  const uint32_t audioTrackId = 1;
  MoofParser parser(stream, AsVariant(audioTrackId), true);
  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_TRUE(parser.RebuildFragmentedIndex(byteRanges));
  ASSERT_FALSE(parser.Moofs().IsEmpty());
  ASSERT_FALSE(parser.Moofs()[0].mIndex.IsEmpty());
  EXPECT_EQ(parser.Moofs()[0].mIndex[0].mDecodeTime.ToMicroseconds(), 0LL);
}

// 30fps fMP4 with timescale 15360, so a frame lasts 512 ticks. Each moof has
// three frames in decode order, so the frame at 1536 comes in moof 2 and
// leaves a gap in moof 1:
//
//   PTS     512   1024  1536  2048  2560
//           |-----|-----|-----|-----|
//   moof 1  [ I ] [ B ]       [ P ]
//   moof 2              [ B ]       [ B ] ...
//
// The frame at 1024 must end at 1536, not stretch over the gap to 2048.
TEST(MoofParser, CompositionGapFilledByNextMoof)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_1955763-split-minigop.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 4u);

  // Every sample keeps the duration from its trun.
  const int64_t kTimescale = 15360;
  const int64_t kFrameTicks = 512;
  for (const Moof& moof : parser.Moofs()) {
    ASSERT_EQ(moof.mIndex.Length(), 3u);
    for (const Sample& sample : moof.mIndex) {
      const int64_t start =
          sample.mCompositionRange.start.ToTicksAtRate(kTimescale);
      const int64_t end =
          sample.mCompositionRange.end.ToTicksAtRate(kTimescale);
      EXPECT_EQ(end - start, kFrameTicks) << "cts " << start;
    }
  }

  // Decode times step by one frame across moofs, as in the trun.
  Maybe<int64_t> lastDts;
  for (const Moof& moof : parser.Moofs()) {
    for (const Sample& sample : moof.mIndex) {
      const int64_t dts = sample.mDecodeTime.ToTicksAtRate(kTimescale);
      if (lastDts) {
        EXPECT_EQ(dts - *lastDts, kFrameTicks) << "dts " << dts;
      }
      lastDts = Some(dts);
    }
  }
}

// The trun durations are decode deltas and do not match the composition
// spacing, so the gaps between samples must still be closed:
//
//   PTS       0      3001   6000   ...
//   trun end  3000   9000   6001
//   end       3001   6000   9001
TEST(MoofParser, CompositionGapClosedWithinMoof)
{
  nsTArray<uint8_t> buffer = ReadTestFile("bipbop_videoinit.mp4");
  nsTArray<uint8_t> segment = ReadTestFile("bipbop_video1.m4s");
  ASSERT_FALSE(buffer.IsEmpty());
  ASSERT_FALSE(segment.IsEmpty());
  buffer.AppendElements(segment);
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 1u);
  const nsTArray<Sample>& samples = parser.Moofs()[0].mIndex;
  ASSERT_EQ(samples.Length(), 24u);

  nsTArray<MP4Interval<media::TimeUnit>> ranges;
  for (const Sample& sample : samples) {
    ranges.AppendElement(sample.mCompositionRange);
  }
  ranges.Sort([](const auto& aA, const auto& aB) {
    return aA.start == aB.start ? 0 : (aA.start < aB.start ? -1 : 1);
  });
  for (size_t i = 1; i < ranges.Length(); i++) {
    EXPECT_EQ(ranges[i - 1].end, ranges[i].start)
        << "sample " << ranges[i - 1].start.ToMicroseconds();
  }
}

// The bound is the longest duration of the moof. The file is the one of
// CompositionGapFilledByNextMoof, except that the frame at 1024 has a duration
// of 7680 ticks. The bound is then 7680, so in moof 1 alone that frame covers
// the gap. When moof 2 is parsed, it ends at the frame of moof 2:
//
//   PTS                512   1024  1536  2048  2560
//   moof 1 alone       [ I ] [ B ------ ][ P ]
//   with moof 2        [ I ] [ B ][ B ] [ P ]
TEST(MoofParser, CompositionGapBoundRaisedByLongSample)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_1955763-long-sample-duration.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 4u);

  // Sample 1024 ends at 1536, where sample 1536 of the next moof starts.
  const int64_t kTimescale = 15360;
  const nsTArray<Sample>& first = parser.Moofs()[0].mIndex;
  ASSERT_EQ(first.Length(), 3u);
  const int64_t kExpected[][2] = {{512, 1024}, {2048, 2560}, {1024, 1536}};
  for (size_t i = 0; i < first.Length(); i++) {
    const int64_t start =
        first[i].mCompositionRange.start.ToTicksAtRate(kTimescale);
    const int64_t end =
        first[i].mCompositionRange.end.ToTicksAtRate(kTimescale);
    EXPECT_EQ(start, kExpected[i][0]);
    EXPECT_EQ(end, kExpected[i][1]);
  }

  // The other moofs keep one-frame samples.
  for (size_t m = 1; m < parser.Moofs().Length(); m++) {
    for (const Sample& sample : parser.Moofs()[m].mIndex) {
      const int64_t start =
          sample.mCompositionRange.start.ToTicksAtRate(kTimescale);
      const int64_t end =
          sample.mCompositionRange.end.ToTicksAtRate(kTimescale);
      EXPECT_EQ(end - start, 512) << "cts " << start;
    }
  }
}

// One sample per color, at 15360 ticks per second. Every sample duration is
// 512, but the red sample at 2048 lasts until the green sample of moof 2:
//
//   cts     512   1024  1536  2048           17920
//   moof 1  [red] [grn]       [red ..........]
//   moof 2              [blu]                [grn]
TEST(MoofParser, SampleEndsInNextMoof)
{
  nsTArray<uint8_t> buffer = ReadTestFile("test_case_1955763-long-frame.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 2u);

  // Only the red sample at 2048 changes; the samples are in decode order.
  const int64_t kTimescale = 15360;
  const int64_t kExpected[2][3][2] = {
      {{512, 1024}, {2048, 17920}, {1024, 1536}},
      {{1536, 2048}, {17920, 18432}, {0, 0}}};
  const int64_t kMoofEnds[2] = {17920, 18432};
  for (size_t m = 0; m < 2; m++) {
    const Moof& moof = parser.Moofs()[m];
    ASSERT_EQ(moof.mIndex.Length(), m == 0 ? 3u : 2u);
    for (size_t i = 0; i < moof.mIndex.Length(); i++) {
      const MP4Interval<media::TimeUnit>& range =
          moof.mIndex[i].mCompositionRange;
      EXPECT_EQ(range.start.ToTicksAtRate(kTimescale), kExpected[m][i][0]);
      EXPECT_EQ(range.end.ToTicksAtRate(kTimescale), kExpected[m][i][1]);
    }
    EXPECT_EQ(moof.mTimeRange.end.ToTicksAtRate(kTimescale), kMoofEnds[m]);
  }
}

// The same file, but read as all tracks of a segment, as the MSE container
// parser does: the samples keep the ends set inside each moof.
TEST(MoofParser, SampleEndsInNextMoofNotForAllTracks)
{
  nsTArray<uint8_t> buffer = ReadTestFile("test_case_1955763-long-frame.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  MoofParser parser(stream, AsVariant(ParseAllTracks{}), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 2u);

  const int64_t kTimescale = 15360;
  const Moof& first = parser.Moofs()[0];
  ASSERT_EQ(first.mIndex.Length(), 3u);
  EXPECT_EQ(first.mIndex[1].mCompositionRange.end.ToTicksAtRate(kTimescale),
            2560);
}

// 12 samples with two B-frames and one sample per moof, at 15360 ticks per
// second. Moof 2 first extends the I-frame to the P-frame, then the B-frames of
// moofs 3 and 4 come in between:
//
//   cts     512   1024  1536  2048
//   moof 1  [ I ]
//   moof 2                    [ P ]
//   moof 3        [ B ]
//   moof 4              [ B ]
//
// So at the end, each sample ends where the next one in composition order
// starts.
TEST(MoofParser, SampleEndsWithBFramesInLaterMoofs)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_1955763-one-frame-per-moof.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_EQ(parser.Moofs().Length(), 12u);

  const int64_t kTimescale = 15360;
  for (const Moof& moof : parser.Moofs()) {
    ASSERT_EQ(moof.mIndex.Length(), 1u);
    const MP4Interval<media::TimeUnit>& range =
        moof.mIndex[0].mCompositionRange;
    const int64_t start = range.start.ToTicksAtRate(kTimescale);
    EXPECT_EQ(range.end.ToTicksAtRate(kTimescale) - start, 512)
        << "cts " << start;
  }
}

// The first moof starts at T, near INT64_MAX ticks:
//
//   sample  start    trun duration
//   B       T        0xFFFFFFFF
//   A       T+2^32-1 1
//   C       T+2^32   1
//
// The start of A plus the longest duration is above INT64_MAX.
TEST(MoofParser, CompositionEndOverflow)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_1955763-composition-end-overflow.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_FALSE(parser.Moofs().IsEmpty());
  const nsTArray<Sample>& samples = parser.Moofs()[0].mIndex;
  ASSERT_EQ(samples.Length(), 3u);

  // Without a representable bound, each sample ends where the next starts.
  for (size_t i = 1; i < samples.Length(); i++) {
    EXPECT_EQ(samples[i - 1].mCompositionRange.end,
              samples[i].mCompositionRange.start);
  }
  EXPECT_TRUE(samples.LastElement().mCompositionRange.end.IsValid());
}

// Every trun duration of the first moof is 0, so the bound is 0:
//
//   PTS       0     512    1536
//   trun end  0     512    1536
//   end       512   1536   1536
TEST(MoofParser, ZeroSampleDurations)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_1955763-zero-sample-durations.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());
  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  EXPECT_TRUE(parser.RebuildFragmentedIndex(
      MediaByteRangeSet(MediaByteRange(0, int64_t(buffer.Length())))));
  ASSERT_FALSE(parser.Moofs().IsEmpty());
  const nsTArray<Sample>& samples = parser.Moofs()[0].mIndex;
  ASSERT_EQ(samples.Length(), 3u);

  nsTArray<MP4Interval<media::TimeUnit>> ranges;
  for (const Sample& sample : samples) {
    ranges.AppendElement(sample.mCompositionRange);
  }
  ranges.Sort([](const auto& aA, const auto& aB) {
    return aA.start == aB.start ? 0 : (aA.start < aB.start ? -1 : 1);
  });
  for (size_t i = 1; i < ranges.Length(); i++) {
    EXPECT_EQ(ranges[i - 1].end, ranges[i].start);
    EXPECT_TRUE(ranges[i - 1].end > ranges[i - 1].start);
  }
}

// 1833896.mp4 has mdhd timescale 0xF800001E (4,160,749,598 as uint32_t).
// This exceeds INT32_MAX. ISO 14496-12 defines mdhd timescale as unsigned
// int(32), so values > INT32_MAX are spec-compliant and not clamped.
// With such a large timescale, consecutive samples with small tick values
// (0-72) all map to 0 microseconds, producing duplicate timestamps.
TEST(MP4Metadata, DuplicateTimestampsWithLargeTimescale)
{
  nsTArray<uint8_t> buffer = ReadTestFile("1833896.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  MP4Metadata metadata(stream);
  ASSERT_EQ(NS_OK, metadata.Parse());
  ASSERT_EQ(1u, metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref());

  MP4Metadata::ResultAndTrackInfo trackInfo =
      metadata.GetTrackInfo(TrackInfo::kVideoTrack, 0);
  ASSERT_TRUE(!!trackInfo.Ref());

  uint32_t timescale = trackInfo.Ref()->mTimeScale;
  EXPECT_GT(timescale, uint32_t(INT32_MAX))
      << "Timescale should be preserved as-is (not clamped)";

  MP4Metadata::ResultAndIndice indices =
      metadata.GetTrackIndice(trackInfo.Ref()->mTrackId);
  ASSERT_TRUE(!!indices.Ref());
  ASSERT_GT(indices.Ref()->Length(), 1u);

  MP4SampleIndex::Indice first{};
  ASSERT_TRUE(indices.Ref()->GetIndice(0, first));
  MP4SampleIndex::Indice second{};
  ASSERT_TRUE(indices.Ref()->GetIndice(1, second));

  ASSERT_NE(first.start_composition, second.start_composition);

  media::TimeUnit t1(CheckedInt64(first.start_composition), int64_t(timescale));
  media::TimeUnit t2(CheckedInt64(second.start_composition),
                     int64_t(timescale));
  EXPECT_EQ(t1.ToMicroseconds(), t2.ToMicroseconds())
      << "Large timescale causes consecutive samples to have duplicate "
         "microsecond timestamps";
}

uint8_t media_gtest_video_init_mp4[] = {
    0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6f, 0x6d,
    0x00, 0x00, 0x00, 0x01, 0x69, 0x73, 0x6f, 0x6d, 0x61, 0x76, 0x63, 0x31,
    0x00, 0x00, 0x02, 0xd1, 0x6d, 0x6f, 0x6f, 0x76, 0x00, 0x00, 0x00, 0x6c,
    0x6d, 0x76, 0x68, 0x64, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x49, 0x73, 0xf8,
    0xc8, 0x4a, 0xc5, 0x7a, 0x00, 0x00, 0x02, 0x58, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x18,
    0x69, 0x6f, 0x64, 0x73, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x80, 0x80,
    0x07, 0x00, 0x4f, 0xff, 0xff, 0x29, 0x15, 0xff, 0x00, 0x00, 0x02, 0x0d,
    0x74, 0x72, 0x61, 0x6b, 0x00, 0x00, 0x00, 0x5c, 0x74, 0x6b, 0x68, 0x64,
    0x00, 0x00, 0x00, 0x01, 0xc8, 0x49, 0x73, 0xf8, 0xc8, 0x49, 0x73, 0xf9,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x02, 0x80, 0x00, 0x00, 0x01, 0x68, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xa9, 0x6d, 0x64, 0x69, 0x61, 0x00, 0x00, 0x00, 0x20,
    0x6d, 0x64, 0x68, 0x64, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x49, 0x73, 0xf8,
    0xc8, 0x49, 0x73, 0xf9, 0x00, 0x00, 0x75, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x55, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x68, 0x64, 0x6c, 0x72,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x65,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x50, 0x41, 0x43, 0x20, 0x49, 0x53, 0x4f, 0x20, 0x56, 0x69, 0x64,
    0x65, 0x6f, 0x20, 0x48, 0x61, 0x6e, 0x64, 0x6c, 0x65, 0x72, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x49, 0x6d, 0x69, 0x6e, 0x66, 0x00, 0x00, 0x00, 0x14,
    0x76, 0x6d, 0x68, 0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x64, 0x69, 0x6e, 0x66,
    0x00, 0x00, 0x00, 0x1c, 0x64, 0x72, 0x65, 0x66, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0c, 0x75, 0x72, 0x6c, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x09, 0x73, 0x74, 0x62, 0x6c,
    0x00, 0x00, 0x00, 0xad, 0x73, 0x74, 0x73, 0x64, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x9d, 0x61, 0x76, 0x63, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x80, 0x01, 0x68, 0x00, 0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0xff, 0xff, 0x00, 0x00, 0x00, 0x33, 0x61, 0x76,
    0x63, 0x43, 0x01, 0x64, 0x00, 0x1f, 0xff, 0xe1, 0x00, 0x1b, 0x67, 0x64,
    0x00, 0x1f, 0xac, 0x2c, 0xc5, 0x02, 0x80, 0xbf, 0xe5, 0xc0, 0x44, 0x00,
    0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf2, 0x3c, 0x60, 0xc6,
    0x58, 0x01, 0x00, 0x05, 0x68, 0xe9, 0x2b, 0x2c, 0x8b, 0x00, 0x00, 0x00,
    0x14, 0x62, 0x74, 0x72, 0x74, 0x00, 0x01, 0x5a, 0xc2, 0x00, 0x24, 0x74,
    0x38, 0x00, 0x09, 0x22, 0x00, 0x00, 0x00, 0x00, 0x10, 0x73, 0x74, 0x74,
    0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x63, 0x74, 0x74, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x73, 0x74, 0x73, 0x63, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x73, 0x74, 0x73,
    0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x73, 0x74, 0x63, 0x6f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6d, 0x76, 0x65,
    0x78, 0x00, 0x00, 0x00, 0x10, 0x6d, 0x65, 0x68, 0x64, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x05, 0x76, 0x18, 0x00, 0x00, 0x00, 0x20, 0x74, 0x72, 0x65,
    0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x03, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00};

const uint32_t media_gtest_video_init_mp4_len = 745;

// Patch the inline init segment's mdhd timescale to 0xF800001E and verify
// MoofParser preserves it. ISO 14496-12 defines mdhd timescale as unsigned
// int(32), so values > INT32_MAX are spec-compliant.
TEST(MoofParser, LargeTimescalePreserved)
{
  nsTArray<uint8_t> buffer;
  buffer.AppendElements(media_gtest_video_init_mp4,
                        media_gtest_video_init_mp4_len);

  // mdhd timescale is at byte offset 292: "mdhd" type at 276, then +4
  // version/flags, +4 creation time, +4 modification time, +4 = timescale.
  const size_t kMdhdTimescaleOffset = 292;
  ASSERT_EQ(buffer[kMdhdTimescaleOffset + 0], 0x00);
  ASSERT_EQ(buffer[kMdhdTimescaleOffset + 1], 0x00);
  ASSERT_EQ(buffer[kMdhdTimescaleOffset + 2], 0x75);
  ASSERT_EQ(buffer[kMdhdTimescaleOffset + 3], 0x30);

  buffer[kMdhdTimescaleOffset + 0] = 0xF8;
  buffer[kMdhdTimescaleOffset + 1] = 0x00;
  buffer[kMdhdTimescaleOffset + 2] = 0x00;
  buffer[kMdhdTimescaleOffset + 3] = 0x1E;

  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  parser.RebuildFragmentedIndex(byteRanges);

  EXPECT_EQ(parser.mMdhd.mTimescale, 0xF800001Eu)
      << "Timescale > INT32_MAX should be preserved as-is";
}

TEST(MP4Metadata, EmptyCTTS)
{
  RefPtr<MediaByteBuffer> buffer =
      new MediaByteBuffer(media_gtest_video_init_mp4_len);
  buffer->AppendElements(media_gtest_video_init_mp4,
                         media_gtest_video_init_mp4_len);
  RefPtr<BufferStream> stream = new BufferStream(buffer);

  MP4Metadata::ResultAndByteBuffer metadataBuffer =
      MP4Metadata::Metadata(stream);
  EXPECT_EQ(NS_OK, metadataBuffer.Result());
  EXPECT_TRUE(metadataBuffer.Ref());

  MP4Metadata metadata(stream);
  EXPECT_EQ(metadata.Parse(), NS_OK);
  EXPECT_EQ(1u, metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref());
  MP4Metadata::ResultAndTrackInfo track =
      metadata.GetTrackInfo(TrackInfo::kVideoTrack, 0);
  EXPECT_TRUE(track.Ref() != nullptr);
  // We can seek anywhere in any MPEG4.
  EXPECT_TRUE(metadata.CanSeek());
  EXPECT_FALSE(metadata.Crypto().Ref()->valid);
}

// Fixture so we test telemetry probes.
class MP4MetadataTelemetryFixture : public TelemetryTestFixture {};

TEST_F(MP4MetadataTelemetryFixture, Telemetry) {
  // Helper to fetch the metadata from a file and send telemetry in the process.
  auto UpdateMetadataAndHistograms = [](const char* testFileName) {
    nsTArray<uint8_t> buffer = ReadTestFile(testFileName);
    ASSERT_FALSE(buffer.IsEmpty());
    RefPtr<ByteStream> stream =
        new TestStream(buffer.Elements(), buffer.Length());

    MP4Metadata::ResultAndByteBuffer metadataBuffer =
        MP4Metadata::Metadata(stream);
    EXPECT_EQ(NS_OK, metadataBuffer.Result());
    EXPECT_TRUE(metadataBuffer.Ref());

    MP4Metadata metadata(stream);
    nsresult res = metadata.Parse();
    EXPECT_NS_SUCCEEDED(res);
    auto audioTrackCount = metadata.GetNumberTracks(TrackInfo::kAudioTrack);
    ASSERT_NE(audioTrackCount.Ref(), MP4Metadata::NumberTracksError());
    auto videoTrackCount = metadata.GetNumberTracks(TrackInfo::kVideoTrack);
    ASSERT_NE(videoTrackCount.Ref(), MP4Metadata::NumberTracksError());

    // Need to read the track data to get telemetry to fire.
    for (uint32_t i = 0; i < audioTrackCount.Ref(); i++) {
      metadata.GetTrackInfo(TrackInfo::kAudioTrack, i);
    }
    for (uint32_t i = 0; i < videoTrackCount.Ref(); i++) {
      metadata.GetTrackInfo(TrackInfo::kVideoTrack, i);
    }
  };

  AutoJSContextWithGlobal cx(mCleanGlobal);

  // Checks the current state of the histograms relating to sample description
  // entries and verifies they're in an expected state.
  // aExpectedMultipleCodecCounts is a tuple where the first value represents
  // the number of expected 'false' count, and the second the expected 'true'
  // count for the sample description entries have multiple codecs histogram.
  // aExpectedMultipleCryptoCounts is the same, but for the sample description
  // entires have multiple crypto histogram.
  // aExpectedSampleDescriptionEntryCounts is a tuple with 6 values, each is
  // the expected number of sample description seen. I.e, the first value in the
  // tuple is the number of tracks we've seen with 0 sample descriptions, the
  // second value with 1 sample description, and so on up to 5 sample
  // descriptions. aFileName is the name of the most recent file we've parsed,
  // and is used to log if our telem counts are not in an expected state.
  auto CheckHistograms =
      [this, &cx](
          const std::tuple<uint32_t, uint32_t>& aExpectedMultipleCodecCounts,
          const std::tuple<uint32_t, uint32_t>& aExpectedMultipleCryptoCounts,
          const std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t>& aExpectedSampleDescriptionEntryCounts,
          const char* aFileName) {
        // Get a snapshot of the current histograms
        JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
        TelemetryTestHelpers::GetSnapshots(cx.GetJSContext(), mTelemetry,
                                           "" /* this string is unused */,
                                           &snapshot, false /* is_keyed */);

        // We'll use these to pull values out of the histograms.
        JS::Rooted<JS::Value> values(cx.GetJSContext());
        JS::Rooted<JS::Value> value(cx.GetJSContext());

        // Verify our multiple codecs count histogram.
        JS::Rooted<JS::Value> multipleCodecsHistogram(cx.GetJSContext());
        TelemetryTestHelpers::GetProperty(
            cx.GetJSContext(),
            "MEDIA_MP4_PARSE_SAMPLE_DESCRIPTION_ENTRIES_HAVE_MULTIPLE_CODECS",
            snapshot, &multipleCodecsHistogram);
        ASSERT_TRUE(multipleCodecsHistogram.isObject())
        << "Multiple codecs histogram should exist!";

        TelemetryTestHelpers::GetProperty(cx.GetJSContext(), "values",
                                          multipleCodecsHistogram, &values);
        // False count.
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 0, values, &value);
        uint32_t uValue = 0;
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<0>(aExpectedMultipleCodecCounts), uValue)
            << "Unexpected number of false multiple codecs after parsing "
            << aFileName;
        // True count.
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 1, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<1>(aExpectedMultipleCodecCounts), uValue)
            << "Unexpected number of true multiple codecs after parsing "
            << aFileName;

        // Verify our multiple crypto count histogram.
        JS::Rooted<JS::Value> multipleCryptoHistogram(cx.GetJSContext());
        TelemetryTestHelpers::GetProperty(
            cx.GetJSContext(),
            "MEDIA_MP4_PARSE_SAMPLE_DESCRIPTION_ENTRIES_HAVE_MULTIPLE_CRYPTO",
            snapshot, &multipleCryptoHistogram);
        ASSERT_TRUE(multipleCryptoHistogram.isObject())
        << "Multiple crypto histogram should exist!";

        TelemetryTestHelpers::GetProperty(cx.GetJSContext(), "values",
                                          multipleCryptoHistogram, &values);
        // False count.
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 0, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<0>(aExpectedMultipleCryptoCounts), uValue)
            << "Unexpected number of false multiple cryptos after parsing "
            << aFileName;
        // True count.
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 1, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<1>(aExpectedMultipleCryptoCounts), uValue)
            << "Unexpected number of true multiple cryptos after parsing "
            << aFileName;

        // Verify our sample description entry count histogram.
        JS::Rooted<JS::Value> numSamplesHistogram(cx.GetJSContext());
        TelemetryTestHelpers::GetProperty(
            cx.GetJSContext(), "MEDIA_MP4_PARSE_NUM_SAMPLE_DESCRIPTION_ENTRIES",
            snapshot, &numSamplesHistogram);
        ASSERT_TRUE(numSamplesHistogram.isObject())
        << "Num sample description entries histogram should exist!";

        TelemetryTestHelpers::GetProperty(cx.GetJSContext(), "values",
                                          numSamplesHistogram, &values);

        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 0, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<0>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 0 sample entry descriptions after parsing "
            << aFileName;
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 1, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<1>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 1 sample entry descriptions after parsing "
            << aFileName;
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 2, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<2>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 2 sample entry descriptions after parsing "
            << aFileName;
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 3, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<3>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 3 sample entry descriptions after parsing "
            << aFileName;
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 4, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<4>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 4 sample entry descriptions after parsing "
            << aFileName;
        TelemetryTestHelpers::GetElement(cx.GetJSContext(), 5, values, &value);
        JS::ToUint32(cx.GetJSContext(), value, &uValue);
        EXPECT_EQ(std::get<5>(aExpectedSampleDescriptionEntryCounts), uValue)
            << "Unexpected number of 5 sample entry descriptions after parsing "
            << aFileName;
      };

  // Clear histograms
  TelemetryTestHelpers::GetAndClearHistogram(
      cx.GetJSContext(), mTelemetry,
      nsLiteralCString(
          "MEDIA_MP4_PARSE_SAMPLE_DESCRIPTION_ENTRIES_HAVE_MULTIPLE_CODECS"),
      false /* is_keyed */);

  TelemetryTestHelpers::GetAndClearHistogram(
      cx.GetJSContext(), mTelemetry,
      nsLiteralCString(
          "MEDIA_MP4_PARSE_SAMPLE_DESCRIPTION_ENTRIES_HAVE_MULTIPLE_CRYPTO"),
      false /* is_keyed */);

  TelemetryTestHelpers::GetAndClearHistogram(
      cx.GetJSContext(), mTelemetry,
      "MEDIA_MP4_PARSE_NUM_SAMPLE_DESCRIPTION_ENTRIES"_ns,
      false /* is_keyed */);

  // The snapshot won't have any data in it until we populate our histograms, so
  // we don't check for a baseline here. Just read out first MP4 metadata.

  // Grab one of the test cases we know should parse and parse it, this should
  // trigger telemetry gathering.

  // This file contains 2 moovs, each with a video and audio track with one
  // sample description entry. So we should see 4 tracks, each with a single
  // codec, no crypto, and a single sample description entry.
  UpdateMetadataAndHistograms("test_case_1185230.mp4");

  // Verify our histograms are updated.
  CheckHistograms(std::make_tuple<uint32_t, uint32_t>(4, 0),
                  std::make_tuple<uint32_t, uint32_t>(4, 0),
                  std::make_tuple<uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t>(0, 4, 0, 0, 0, 0),
                  "test_case_1185230.mp4");

  // Parse another test case. This one has a single moov with a single video
  // track. However, the track has two sample description entries, and our
  // updated telemetry should reflect that.
  UpdateMetadataAndHistograms(
      "test_case_1513651-2-sample-description-entries.mp4");

  // Verify our histograms are updated.
  CheckHistograms(std::make_tuple<uint32_t, uint32_t>(5, 0),
                  std::make_tuple<uint32_t, uint32_t>(5, 0),
                  std::make_tuple<uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t>(0, 4, 1, 0, 0, 0),
                  "test_case_1513651-2-sample-description-entries.mp4");

  // Parse another test case. This one has 2 sample decription entries, both
  // with crypto information, which should be reflected in our telemetry.
  UpdateMetadataAndHistograms(
      "test_case_1714125-2-sample-description-entires-with-identical-crypto."
      "mp4");

  // Verify our histograms are updated.
  CheckHistograms(
      std::make_tuple<uint32_t, uint32_t>(6, 0),
      std::make_tuple<uint32_t, uint32_t>(5, 1),
      std::make_tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                      uint32_t>(0, 4, 2, 0, 0, 0),
      "test_case_1714125-2-sample-description-entires-with-identical-crypto."
      "mp4");
}

TEST(MP4Metadata, BadCo64Offset)
{
  nsTArray<uint8_t> buffer = ReadTestFile("test_case_bad_co64_offset.mp4");
  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  MP4Metadata metadata(stream);
  EXPECT_EQ(metadata.Parse(), NS_OK);
  EXPECT_EQ(1u, metadata.GetNumberTracks(TrackInfo::kVideoTrack).Ref());

  MP4Metadata::ResultAndTrackInfo track =
      metadata.GetTrackInfo(TrackInfo::kVideoTrack, 0);
  ASSERT_TRUE(!!track.Ref());
  const VideoInfo* videoInfo = track.Ref()->GetAsVideoInfo();
  ASSERT_TRUE(!!videoInfo);

  MP4Metadata::ResultAndIndice indices =
      metadata.GetTrackIndice(videoInfo->mTrackId);
  ASSERT_TRUE(!!indices.Ref());
  ASSERT_EQ(1u, indices.Ref()->Length());

  MP4SampleIndex::Indice data;
  EXPECT_TRUE(indices.Ref()->GetIndice(0, data));
  EXPECT_GT(data.start_offset, uint64_t(INT64_MAX));

  RefPtr<MP4SampleIndex> sampleIndex = new MP4SampleIndex(
      *indices.Ref(), stream, videoInfo->mTrackId, false, 1000);
  SampleIterator iter(sampleIndex);
  EXPECT_FALSE(iter.HasNext());
}

TEST(MoofParser, BadFragmentedTrunOffset)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_bad_fragmented_trun_offset.mp4");

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  const uint32_t videoTrackId = 0;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  EXPECT_FALSE(parser.RebuildFragmentedIndex(byteRanges));
}

TEST(MoofParser, BadFragmentedSaioOffset)
{
  nsTArray<uint8_t> buffer =
      ReadTestFile("test_case_bad_fragmented_saio_offset.mp4");

  ASSERT_FALSE(buffer.IsEmpty());
  RefPtr<ByteStream> stream =
      new TestStream(buffer.Elements(), buffer.Length());

  const uint32_t videoTrackId = 1;
  MoofParser parser(stream, AsVariant(videoTrackId), false);
  const MediaByteRangeSet byteRanges(
      MediaByteRange(0, int64_t(buffer.Length())));
  ASSERT_TRUE(parser.RebuildFragmentedIndex(byteRanges));
  ASSERT_FALSE(parser.Moofs().IsEmpty());
  EXPECT_FALSE(parser.Moofs()[0].SencIsValid());
  ASSERT_FALSE(parser.Moofs()[0].mIndex.IsEmpty());
  EXPECT_TRUE(parser.Moofs()[0].mIndex[0].mCencRange.IsEmpty());
}
