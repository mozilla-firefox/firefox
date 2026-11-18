/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gtest/gtest.h>
#include <string.h>

#include <algorithm>
#include <random>
#include <vector>

#include "ContainerParser.h"
#include "MPEGAudioByteStream.h"
#include "MPEGAudioByteStreamDemuxer.h"
#include "MediaData.h"
#include "SourceBufferResource.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/gtest/WaitFor.h"

#ifdef FUZZING_INTERFACES
#  include "FuzzingInterface.h"
#endif

using namespace mozilla;
using namespace mozilla::mpeg_audio;
using media::TimeUnit;

namespace {

// MPEG-1 Layer III, 128 kb/s, 44100 Hz, stereo.
constexpr uint8_t kStereoHeader[] = {0xFF, 0xFB, 0x90, 0x00};
constexpr size_t kStereoFrameLength = 417;
// MPEG-1 Layer III, 64 kb/s, 48000 Hz, mono.
constexpr uint8_t kMonoHeader[] = {0xFF, 0xFB, 0x54, 0xC0};
constexpr size_t kMonoFrameLength = 192;
// MPEG-2 Layer III, 64 kb/s, 22050 Hz, mono.
constexpr uint8_t kMPEG2Header[] = {0xFF, 0xF3, 0x80, 0xC0};
constexpr size_t kMPEG2FrameLength = 208;
// MPEG-1 Layer II, 160 kb/s, 44100 Hz, stereo.
constexpr uint8_t kLayer2Header[] = {0xFF, 0xFD, 0x90, 0x00};
constexpr size_t kLayer2FrameLength = 522;
// MPEG-1 Layer I, 288 kb/s, 44100 Hz, stereo.
constexpr uint8_t kLayer1Header[] = {0xFF, 0xFF, 0x90, 0x00};
constexpr size_t kLayer1FrameLength = 312;
// The reserved layer.
constexpr uint8_t kInvalidHeader[] = {0xFF, 0xF9, 0x90, 0x00};

using Bytes = std::vector<uint8_t>;

Bytes Header(const uint8_t (&aHeader)[4]) {
  return Bytes(aHeader, aHeader + 4);
}

// A frame whose payload bytes all equal aFill.
Bytes Frame(const uint8_t (&aHeader)[4], size_t aLength, uint8_t aFill = 0) {
  Bytes frame = Header(aHeader);
  frame.resize(aLength, aFill);
  return frame;
}

Bytes StereoFrame(uint8_t aFill = 0) {
  return Frame(kStereoHeader, kStereoFrameLength, aFill);
}

// A Xing or Info frame, which follows the 32 bytes of MPEG-1 stereo side
// information.
Bytes InfoFrame(const char* aTag) {
  Bytes frame = StereoFrame();
  memcpy(frame.data() + 4 + 32, aTag, 4);
  return frame;
}

Bytes ID3v2(uint32_t aPayloadSize, bool aFooter = false) {
  Bytes tag = {'I',
               'D',
               '3',
               4,
               0,
               uint8_t(aFooter ? 0x10 : 0),
               uint8_t((aPayloadSize >> 21) & 0x7F),
               uint8_t((aPayloadSize >> 14) & 0x7F),
               uint8_t((aPayloadSize >> 7) & 0x7F),
               uint8_t(aPayloadSize & 0x7F)};
  tag.resize(tag.size() + aPayloadSize + (aFooter ? 10 : 0), 0);
  return tag;
}

Bytes Text(const char* aText) { return Bytes(aText, aText + strlen(aText)); }

Bytes ID3v1() {
  Bytes tag = Text("TAG");
  tag.resize(128, 'x');
  return tag;
}

Bytes Garbage(size_t aLength) { return Bytes(aLength, 0x55); }

Bytes Concat(std::initializer_list<Bytes> aParts) {
  Bytes bytes;
  for (const Bytes& part : aParts) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

Span<const uint8_t> AsSpan(const Bytes& aBytes) {
  return Span(aBytes.data(), aBytes.size());
}

Bytes Slice(const Bytes& aBytes, size_t aStart, size_t aEnd) {
  return Bytes(aBytes.begin() + aStart, aBytes.begin() + aEnd);
}

MediaSpan AsMediaSpan(Span<const uint8_t> aBytes) {
  RefPtr<MediaByteBuffer> buffer = new MediaByteBuffer();
  buffer->AppendElements(aBytes.Elements(), aBytes.Length());
  return MediaSpan(buffer);
}

MediaSpan AsMediaSpan(const Bytes& aBytes) {
  return AsMediaSpan(AsSpan(aBytes));
}

MediaByteRange MakeRange(size_t aStart, size_t aEnd) {
  return MediaByteRange(int64_t(aStart), int64_t(aEnd));
}

UniquePtr<ContainerParser> CreateParser() {
  return ContainerParser::CreateForMIMEType(
      MediaContainerType(MEDIAMIMETYPE("audio/mpeg")));
}

// As TrackBuffersManager::RecreateParser(true) does.
UniquePtr<ContainerParser> CreateParserWithInitData(
    const uint8_t (&aHeader)[4]) {
  UniquePtr<ContainerParser> parser = CreateParser();
  TimeUnit start, end;
  EXPECT_EQ(parser->ParseStartAndEndTimestamps(AsMediaSpan(Header(aHeader)),
                                               start, end),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_TRUE(parser->HasCompleteInitData());
  return parser;
}

nsresult Parse(ContainerParser& aParser, const Bytes& aBytes) {
  TimeUnit start, end;
  return aParser.ParseStartAndEndTimestamps(AsMediaSpan(aBytes), start, end);
}

}  // namespace

TEST(MPEGAudioByteStream, FrameHeader)
{
  Maybe<FrameInfo> frame = ParseFrameHeader(kStereoHeader);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mLayer, 3u);
  EXPECT_EQ(frame->mSampleRate, 44100u);
  EXPECT_EQ(frame->mChannels, 2u);
  EXPECT_EQ(frame->mSamplesPerFrame, 1152u);
  EXPECT_EQ(frame->mLength, kStereoFrameLength);
  EXPECT_TRUE(frame->mIsMPEG1);

  const uint8_t padded[] = {0xFF, 0xFB, 0x92, 0x00};
  frame = ParseFrameHeader(padded);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mLength, kStereoFrameLength + 1);

  frame = ParseFrameHeader(kMonoHeader);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mSampleRate, 48000u);
  EXPECT_EQ(frame->mChannels, 1u);
  EXPECT_EQ(frame->mLength, kMonoFrameLength);

  frame = ParseFrameHeader(kMPEG2Header);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mSampleRate, 22050u);
  EXPECT_EQ(frame->mChannels, 1u);
  EXPECT_EQ(frame->mSamplesPerFrame, 576u);
  EXPECT_EQ(frame->mLength, kMPEG2FrameLength);
  EXPECT_FALSE(frame->mIsMPEG1);

  EXPECT_FALSE(frame->HasSameConfig(*ParseFrameHeader(kMonoHeader)));
  EXPECT_TRUE(ParseFrameHeader(kStereoHeader)
                  ->HasSameConfig(*ParseFrameHeader(padded)));

  frame = ParseFrameHeader(kLayer2Header);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mLayer, 2u);
  EXPECT_EQ(frame->mSampleRate, 44100u);
  EXPECT_EQ(frame->mChannels, 2u);
  EXPECT_EQ(frame->mSamplesPerFrame, 1152u);
  EXPECT_EQ(frame->mLength, kLayer2FrameLength);
  EXPECT_FALSE(frame->HasSameConfig(*ParseFrameHeader(kStereoHeader)));

  // MPEG-2 Layer II, 64 kb/s, 22050 Hz.
  const uint8_t mpeg2Layer2[] = {0xFF, 0xF5, 0x80, 0xC0};
  frame = ParseFrameHeader(mpeg2Layer2);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mSampleRate, 22050u);
  EXPECT_EQ(frame->mSamplesPerFrame, 1152u);
  EXPECT_EQ(frame->mLength, 417u);

  // MPEG-2.5 Layer II, 160 kb/s, 8000 Hz, padded.
  const uint8_t mpeg25Layer2[] = {0xFF, 0xE5, 0xEA, 0xC0};
  frame = ParseFrameHeader(mpeg25Layer2);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mSampleRate, 8000u);
  EXPECT_EQ(frame->mLength, kMaxFrameLength);

  // Layer I frames are made of 4 byte slots, so their length is a multiple of
  // 4.
  frame = ParseFrameHeader(kLayer1Header);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mLayer, 1u);
  EXPECT_EQ(frame->mSamplesPerFrame, 384u);
  EXPECT_EQ(frame->mLength, kLayer1FrameLength);
  const uint8_t paddedLayer1[] = {0xFF, 0xFF, 0x92, 0x00};
  frame = ParseFrameHeader(paddedLayer1);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mLength, kLayer1FrameLength + 4);

  // MPEG-2 Layer I, 256 kb/s, 16000 Hz.
  const uint8_t mpeg2Layer1[] = {0xFF, 0xF7, 0xE8, 0x00};
  frame = ParseFrameHeader(mpeg2Layer1);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->mSampleRate, 16000u);
  EXPECT_EQ(frame->mLength, 768u);

  const uint8_t invalid[][4] = {
      {0xFF, 0xF9, 0x90, 0x00},  // Reserved layer
      {0xFF, 0xEB, 0x90, 0x00},  // Reserved version
      {0xFF, 0xFB, 0x00, 0x00},  // Free format bitrate
      {0xFF, 0xFB, 0xF0, 0x00},  // Bad bitrate
      {0xFF, 0xFB, 0x9C, 0x00},  // Reserved sample rate
      {0xFE, 0xFB, 0x90, 0x00},  // No sync
  };
  for (const auto& header : invalid) {
    EXPECT_FALSE(ParseFrameHeader(header));
  }
}

TEST(MPEGAudioByteStream, Frames)
{
  const Bytes frame = StereoFrame();

  Unit unit = ParseUnit(AsSpan(frame), false);
  EXPECT_EQ(unit.mType, Unit::Type::Frame);
  EXPECT_EQ(unit.mLength, kStereoFrameLength);
  EXPECT_EQ(unit.mFrame.mSampleRate, 44100u);

  EXPECT_EQ(ParseUnit(AsSpan(frame).To(3), false).mType,
            Unit::Type::NeedMoreData);
  EXPECT_EQ(ParseUnit(AsSpan(frame).To(100), false).mType,
            Unit::Type::NeedMoreData);
  unit = ParseUnit(AsSpan(frame).To(4), false, /* aHeaderOnly = */ true);
  EXPECT_EQ(unit.mType, Unit::Type::Frame);
  EXPECT_EQ(unit.mLength, kStereoFrameLength);

  const Bytes invalid = Frame(kInvalidHeader, 100);
  EXPECT_EQ(ParseUnit(AsSpan(invalid), false).mType, Unit::Type::Error);

  for (const char* tag : {"Xing", "Info"}) {
    const Bytes info = InfoFrame(tag);
    unit = ParseUnit(AsSpan(info), false);
    EXPECT_EQ(unit.mType, Unit::Type::InfoFrame);
    EXPECT_EQ(unit.mLength, kStereoFrameLength);
    EXPECT_EQ(ParseUnit(AsSpan(info), false, true).mType, Unit::Type::Frame);
  }

  // Xing and Info tags are only defined for Layer III.
  Bytes layer2 = Frame(kLayer2Header, kLayer2FrameLength);
  memcpy(layer2.data() + 4 + 32, "Info", 4);
  unit = ParseUnit(AsSpan(layer2), false);
  EXPECT_EQ(unit.mType, Unit::Type::Frame);
  EXPECT_EQ(unit.mLength, kLayer2FrameLength);
  EXPECT_EQ(unit.mFrame.mLayer, 2u);
}

TEST(MPEGAudioByteStream, Metadata)
{
  Bytes tag = ID3v2(257);
  Unit unit = ParseUnit(AsSpan(tag).To(10), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, 267u);
  EXPECT_EQ(ParseUnit(AsSpan(tag).To(9), false).mType,
            Unit::Type::NeedMoreData);
  tag[8] = 0x80;
  EXPECT_EQ(ParseUnit(AsSpan(tag), false).mType, Unit::Type::Error);

  unit = ParseUnit(AsSpan(ID3v2(257, true)), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, 277u);

  unit = ParseUnit(AsSpan(Text("TAG.")), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, 128u);
  // "TAG+" starts an extended tag only when an ID3v1 tag follows it.
  // Otherwise it's an ID3v1 tag with a title that starts with "+".
  Bytes extended = Text("TAG+");
  extended.resize(227, 'x');
  EXPECT_EQ(ParseUnit(AsSpan(extended), false).mType, Unit::Type::NeedMoreData);
  unit = ParseUnit(AsSpan(Concat({extended, ID3v1()})), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, 227u);
  Bytes plusTitle = Text("TAG+");
  plusTitle.resize(128, 'x');
  unit = ParseUnit(AsSpan(Concat({plusTitle, StereoFrame()})), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, 128u);

  const Bytes icy = Text("ICY 200 OK\r\nicy-name: test\r\n\r\n");
  unit = ParseUnit(AsSpan(icy), false);
  EXPECT_EQ(unit.mType, Unit::Type::Metadata);
  EXPECT_EQ(unit.mLength, icy.size());
  EXPECT_EQ(ParseUnit(AsSpan(icy).To(icy.size() - 1), false).mType,
            Unit::Type::NeedMoreData);
  const Bytes longIcy = Concat({Text("ICY 200 OK\r\n"), Garbage(4096)});
  EXPECT_EQ(ParseUnit(AsSpan(longIcy), false).mType, Unit::Type::Error);
  // "ICY" only starts an Icecast header when a space follows it.
  const Bytes notIcy = Text("ICY200 OK\r\nicy-name: test\r\n\r\n");
  EXPECT_EQ(ParseUnit(AsSpan(notIcy), false).mType, Unit::Type::Garbage);
}

TEST(MPEGAudioByteStream, Garbage)
{
  const Bytes frames = Concat({StereoFrame(), StereoFrame(), StereoFrame()});
  const Bytes data = Concat({Garbage(100), frames, Garbage(1)});

  Unit unit = ParseUnit(AsSpan(data), false);
  EXPECT_EQ(unit.mType, Unit::Type::Garbage);
  EXPECT_EQ(unit.mLength, 100u);
  unit = ParseUnit(AsSpan(data).From(100), true);
  EXPECT_EQ(unit.mType, Unit::Type::Frame);

  // Resynchronizing needs the third frame to end within the data.
  unit = ParseUnit(AsSpan(frames), true);
  EXPECT_EQ(unit.mType, Unit::Type::NeedMoreData);
  // Garbage is skipped up to a frame sync that may start the next frame.
  unit = ParseUnit(AsSpan(Concat({Garbage(10), frames})), false);
  EXPECT_EQ(unit.mType, Unit::Type::Garbage);
  EXPECT_EQ(unit.mLength, 10u);

  // A frame sync that isn't followed by more frames is garbage.
  const Bytes falseSync = Concat({Header(kStereoHeader), Garbage(1000)});
  unit = ParseUnit(AsSpan(falseSync), true);
  EXPECT_EQ(unit.mType, Unit::Type::Garbage);
  EXPECT_EQ(unit.mLength, falseSync.size());
}

TEST(MPEGAudioContainerParser, InitSegment)
{
  UniquePtr<ContainerParser> parser = CreateParser();
  EXPECT_TRUE(parser->HasImplicitInitSegment());
  EXPECT_EQ(parser->GetRoundingError(), 0);

  const Bytes data = Concat({StereoFrame(), StereoFrame()});
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(data)), 0u);
  EXPECT_EQ(parser->IsInitSegmentPresent(AsMediaSpan(AsSpan(data).To(3))),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->IsMediaSegmentPresent(AsMediaSpan(data)),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_NS_SUCCEEDED(parser->IsInitSegmentPresent(AsMediaSpan(data)));

  EXPECT_EQ(Parse(*parser, data), NS_ERROR_NOT_AVAILABLE);
  ASSERT_TRUE(parser->HasCompleteInitData());
  ASSERT_EQ(parser->InitData()->Length(), 4u);
  EXPECT_EQ(memcmp(parser->InitData()->Elements(), kStereoHeader, 4), 0);
  // The frame header is also part of the media segment.
  EXPECT_TRUE(parser->InitSegmentRange().IsEmpty());
  EXPECT_TRUE(parser->MediaSegmentRange().IsEmpty());

  parser = CreateParser();
  const Bytes invalid = Frame(kInvalidHeader, 100);
  MediaResult rv = parser->IsInitSegmentPresent(AsMediaSpan(invalid));
  EXPECT_TRUE(NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE);
}

TEST(MPEGAudioContainerParser, MediaSegments)
{
  UniquePtr<ContainerParser> parser = CreateParserWithInitData(kStereoHeader);
  const Bytes frames = Concat({StereoFrame(), StereoFrame(), StereoFrame()});
  const Bytes partial = Concat({frames, StereoFrame()});

  // A frame header with the current configuration needs no new track.
  EXPECT_EQ(parser->IsInitSegmentPresent(AsMediaSpan(frames)),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_NS_SUCCEEDED(parser->IsMediaSegmentPresent(AsMediaSpan(frames)));
  // The media segment starts with the coded audio that follows the header.
  EXPECT_EQ(parser->IsMediaSegmentPresent(
                AsMediaSpan(Slice(frames, 0, kFrameHeaderSize))),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_NS_SUCCEEDED(parser->IsMediaSegmentPresent(
      AsMediaSpan(Slice(frames, 0, kFrameHeaderSize + 1))));

  // The media segment ends after the last complete frame.
  const size_t split = frames.size() + 100;
  EXPECT_EQ(Parse(*parser, Slice(partial, 0, split)), NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 3 * kStereoFrameLength));
  EXPECT_EQ(Parse(*parser, Slice(partial, split, partial.size())),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 4 * kStereoFrameLength));
}

TEST(MPEGAudioContainerParser, ConfigChange)
{
  UniquePtr<ContainerParser> parser = CreateParserWithInitData(kStereoHeader);
  const Bytes mono = Frame(kMonoHeader, kMonoFrameLength);
  EXPECT_NS_SUCCEEDED(parser->IsInitSegmentPresent(AsMediaSpan(mono)));
  EXPECT_EQ(parser->IsMediaSegmentPresent(AsMediaSpan(mono)),
            NS_ERROR_NOT_AVAILABLE);

  // The media segment ends before the frame that needs a new init segment.
  EXPECT_EQ(Parse(*parser, Concat({StereoFrame(), StereoFrame(), mono})),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 2 * kStereoFrameLength));
  EXPECT_EQ(Parse(*parser, StereoFrame()), NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 2 * kStereoFrameLength));

  // A frame of another layer also needs a new init segment.
  parser = CreateParserWithInitData(kStereoHeader);
  const Bytes layer2 = Frame(kLayer2Header, kLayer2FrameLength);
  EXPECT_NS_SUCCEEDED(parser->IsInitSegmentPresent(AsMediaSpan(layer2)));
  EXPECT_EQ(parser->IsMediaSegmentPresent(AsMediaSpan(layer2)),
            NS_ERROR_NOT_AVAILABLE);
}

TEST(MPEGAudioContainerParser, Metadata)
{
  UniquePtr<ContainerParser> parser = CreateParser();
  const Bytes tag = ID3v2(1000);
  const Bytes icy = Text("ICY 200 OK\r\n\r\n");
  const Bytes frame = StereoFrame();
  const Bytes data = Concat({tag, icy, ID3v1(), frame});

  // Metadata frames are ignored, including the rest of one that is appended
  // later.
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(Slice(data, 0, 5))),
            0u);
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(Slice(data, 0, 100))),
            100u);
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(
                AsMediaSpan(Slice(data, 100, data.size()))),
            data.size() - 100 - frame.size());
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(frame)), 0u);
  EXPECT_NS_SUCCEEDED(parser->IsInitSegmentPresent(AsMediaSpan(frame)));

  // A media segment ends before a metadata frame.
  parser = CreateParserWithInitData(kStereoHeader);
  EXPECT_EQ(Parse(*parser, Concat({frame, frame, ID3v1(), frame})),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 2 * kStereoFrameLength));

  // An ID3v1 tag with a title that starts with "+" doesn't take the start of
  // the frame that follows it.
  Bytes plusTitle = Text("TAG+");
  plusTitle.resize(128, 'x');
  const Bytes rest = Concat({plusTitle, frame});
  parser = CreateParserWithInitData(kStereoHeader);
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(rest)), 128u);
  EXPECT_NS_SUCCEEDED(parser->IsMediaSegmentPresent(AsMediaSpan(frame)));
}

TEST(MPEGAudioContainerParser, Garbage)
{
  UniquePtr<ContainerParser> parser = CreateParserWithInitData(kStereoHeader);
  const Bytes frames = Concat({StereoFrame(), StereoFrame(), StereoFrame()});
  const Bytes data = Concat({Garbage(50), frames, Garbage(1)});

  // Garbage is ignored, and a frame header that follows it only starts a
  // media segment once two more follow it.
  const Bytes start = Slice(data, 0, 50 + 2 * kStereoFrameLength);
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(start)), 50u);
  const Bytes rest = Slice(data, 50, data.size());
  EXPECT_EQ(parser->IsMediaSegmentPresent(
                AsMediaSpan(Slice(start, 50, start.size()))),
            NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(rest)), 0u);
  EXPECT_NS_SUCCEEDED(parser->IsMediaSegmentPresent(AsMediaSpan(rest)));

  // A media segment ends before garbage.
  EXPECT_EQ(Parse(*parser, rest), NS_ERROR_NOT_AVAILABLE);
  EXPECT_EQ(parser->MediaSegmentRange(),
            MakeRange(4, 4 + 3 * kStereoFrameLength));

  // A frame header that isn't followed by more frames is garbage.
  parser = CreateParserWithInitData(kStereoHeader);
  const Bytes falseSync =
      Concat({Garbage(10), Header(kStereoHeader), Garbage(1000), rest});
  EXPECT_EQ(parser->SkipIgnorableBytesAtStart(AsMediaSpan(falseSync)), 1014u);

  parser = CreateParserWithInitData(kStereoHeader);
  const Bytes invalid = Frame(kInvalidHeader, 100);
  MediaResult rv = Parse(*parser, Concat({StereoFrame(), invalid}));
  EXPECT_TRUE(NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE);
}

TEST(MPEGAudioByteStreamDemuxer, Samples)
{
  RefPtr<SourceBufferResource> resource = new SourceBufferResource();
  resource->AppendData(AsMediaSpan(Header(kStereoHeader)));
  RefPtr<MPEGAudioByteStreamDemuxer> demuxer =
      new MPEGAudioByteStreamDemuxer(resource);
  EXPECT_EQ(WaitForResolve(demuxer->Init()), NS_OK);
  ASSERT_EQ(demuxer->GetNumberTracks(TrackInfo::kAudioTrack), 1u);
  EXPECT_EQ(demuxer->GetNumberTracks(TrackInfo::kVideoTrack), 0u);
  RefPtr<MediaTrackDemuxer> track =
      demuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
  ASSERT_TRUE(track);
  UniquePtr<TrackInfo> info = track->GetInfo();
  ASSERT_TRUE(info->GetAsAudioInfo());
  EXPECT_EQ(info->mMimeType, "audio/mpeg"_ns);
  EXPECT_EQ(info->GetAsAudioInfo()->mRate, 44100u);
  EXPECT_EQ(info->GetAsAudioInfo()->mChannels, 2u);
  // The byte stream format doesn't specify a duration.
  EXPECT_TRUE(info->mDuration.IsZero());
  // The generated timestamps don't account for the decoder delay.
  ASSERT_TRUE(
      info->GetAsAudioInfo()->mCodecSpecificConfig.is<Mp3CodecSpecificData>());
  EXPECT_EQ(info->GetAsAudioInfo()
                ->mCodecSpecificConfig.as<Mp3CodecSpecificData>()
                .mDecoderDelayFrames,
            529u);

  // TrackBuffersManager evicts the initialization segment once the demuxer
  // is initialized.
  resource->EvictAll();
  const Bytes data = Concat(
      {InfoFrame("Info"), StereoFrame(1), StereoFrame(2), StereoFrame(3)});
  const size_t split = 2 * kStereoFrameLength + 200;
  resource->AppendData(AsMediaSpan(AsSpan(data).To(split)));

  // The Info frame carries no audio.
  auto samples = WaitFor(track->GetSamples(-1));
  ASSERT_TRUE(samples.isOk());
  {
    const auto& list = samples.inspect()->GetSamples();
    ASSERT_EQ(list.Length(), 1u);
    EXPECT_EQ(list[0]->Size(), kStereoFrameLength);
    EXPECT_EQ(list[0]->Data()[4], 1);
    EXPECT_EQ(list[0]->mOffset, int64_t(4 + kStereoFrameLength));
    EXPECT_EQ(list[0]->mTime, TimeUnit::Zero());
    EXPECT_EQ(list[0]->mDuration, TimeUnit(1152, 44100));
    EXPECT_TRUE(list[0]->mKeyframe);
  }
  auto waiting = WaitFor(track->GetSamples(-1));
  ASSERT_TRUE(waiting.isErr());
  EXPECT_EQ(waiting.inspectErr().Code(), NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
  EXPECT_EQ(track->GetEvictionOffset(TimeUnit::Zero()),
            int64_t(4 + 2 * kStereoFrameLength));

  resource->AppendData(AsMediaSpan(AsSpan(data).From(split)));
  auto moreSamples = WaitFor(track->GetSamples(-1));
  ASSERT_TRUE(moreSamples.isOk());
  {
    const auto& list = moreSamples.inspect()->GetSamples();
    ASSERT_EQ(list.Length(), 2u);
    for (size_t i = 0; i < list.Length(); ++i) {
      EXPECT_EQ(list[i]->Data()[4], i + 2);
      EXPECT_EQ(list[i]->mTime, TimeUnit(1152 * (i + 1), 44100));
    }
  }
  EXPECT_EQ(track->GetEvictionOffset(TimeUnit::Zero()),
            int64_t(4 + data.size()));
}

TEST(MPEGAudioByteStreamDemuxer, Layer1)
{
  RefPtr<SourceBufferResource> resource = new SourceBufferResource();
  resource->AppendData(AsMediaSpan(Header(kLayer1Header)));
  RefPtr<MPEGAudioByteStreamDemuxer> demuxer =
      new MPEGAudioByteStreamDemuxer(resource);
  EXPECT_EQ(WaitForResolve(demuxer->Init()), NS_OK);
  RefPtr<MediaTrackDemuxer> track =
      demuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
  ASSERT_TRUE(track);
  UniquePtr<TrackInfo> info = track->GetInfo();
  ASSERT_TRUE(info->GetAsAudioInfo());
  ASSERT_TRUE(
      info->GetAsAudioInfo()->mCodecSpecificConfig.is<Mp3CodecSpecificData>());
  EXPECT_EQ(info->GetAsAudioInfo()
                ->mCodecSpecificConfig.as<Mp3CodecSpecificData>()
                .mDecoderDelayFrames,
            241u);

  resource->EvictAll();
  const Bytes frame = Frame(kLayer1Header, kLayer1FrameLength);
  resource->AppendData(AsMediaSpan(Concat({frame, frame})));
  auto samples = WaitFor(track->GetSamples(-1));
  ASSERT_TRUE(samples.isOk());
  const auto& list = samples.inspect()->GetSamples();
  ASSERT_EQ(list.Length(), 2u);
  for (size_t i = 0; i < list.Length(); ++i) {
    EXPECT_EQ(list[i]->Size(), kLayer1FrameLength);
    EXPECT_EQ(list[i]->mTime, TimeUnit(384 * i, 44100));
    EXPECT_EQ(list[i]->mDuration, TimeUnit(384, 44100));
  }
}

namespace {

// As TrackBuffersManager::RecreateParser(true) does, after an initialization
// segment.
UniquePtr<ContainerParser> CreateParserWithInitData(
    const MediaByteBuffer& aInitData) {
  UniquePtr<ContainerParser> parser = CreateParser();
  Parse(*parser, Bytes(aInitData.begin(), aInitData.end()));
  return parser;
}

// Appends aData to MPEGAudioContainerParser in chunks, and demuxes each media
// segment that it finds with MPEGAudioByteStreamDemuxer, as
// TrackBuffersManager does. The first byte sets the chunk size. Returns false
// if the demuxer disagrees with the parser about a media segment, which must
// only hold complete frames with the current configuration.
bool AppendAndDemux(Span<const uint8_t> aData) {
  if (aData.IsEmpty()) {
    return true;
  }
  const size_t chunkSize = 1 + aData[0] * 8;
  aData = aData.From(1);

  UniquePtr<ContainerParser> parser = CreateParser();
  RefPtr<MediaByteBuffer> initData;
  RefPtr<SourceBufferResource> resource;
  RefPtr<MediaTrackDemuxer> track;
  // The end of the data appended to resource.
  int64_t appended = 0;
  Bytes input;
  // Whether input starts with a media segment, of which the first parsed
  // bytes have been passed to the parser.
  bool parsingMediaSegment = false;
  size_t parsed = 0;

  for (size_t offset = 0; offset < aData.Length(); offset += chunkSize) {
    const Span<const uint8_t> chunk =
        aData.Subspan(offset, std::min(chunkSize, aData.Length() - offset));
    input.insert(input.end(), chunk.begin(), chunk.end());
    while (!input.empty()) {
      if (!parsingMediaSegment) {
        if (size_t ignored =
                parser->SkipIgnorableBytesAtStart(AsMediaSpan(input))) {
          input.erase(input.begin(), input.begin() + ignored);
          continue;
        }
        MediaResult rv = parser->IsInitSegmentPresent(AsMediaSpan(input));
        if (NS_SUCCEEDED(rv)) {
          parser = CreateParser();
          if (Parse(*parser, input) != NS_ERROR_NOT_AVAILABLE ||
              !parser->HasCompleteInitData()) {
            return false;
          }
          initData = parser->InitData();
          resource = new SourceBufferResource();
          resource->AppendData(initData);
          appended = int64_t(initData->Length());
          RefPtr<MPEGAudioByteStreamDemuxer> demuxer =
              new MPEGAudioByteStreamDemuxer(resource);
          // The demuxer crashes this if it rejects the initialization
          // segment.
          WaitForResolve(demuxer->Init());
          track = demuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
          resource->EvictAll();
          parser = CreateParserWithInitData(*initData);
          continue;
        }
        if (rv != NS_ERROR_NOT_AVAILABLE) {
          // The append is rejected.
          return true;
        }
        rv = parser->IsMediaSegmentPresent(AsMediaSpan(input));
        if (rv == NS_ERROR_NOT_AVAILABLE) {
          break;
        }
        if (NS_FAILED(rv)) {
          return true;
        }
        parsingMediaSegment = true;
        parsed = 0;
      }

      if (parsed < input.size()) {
        if (Parse(*parser, Slice(input, parsed, input.size())) !=
            NS_ERROR_NOT_AVAILABLE) {
          return true;
        }
        parsed = input.size();
      }
      const MediaByteRange range = parser->MediaSegmentRange();
      if (range.IsEmpty()) {
        break;
      }
      // The parser's offsets count the initialization data it was created
      // with.
      if (range.mStart != int64_t(initData->Length()) ||
          size_t(range.Length()) > input.size()) {
        return false;
      }
      const size_t length = size_t(range.Length());
      resource->AppendData(AsMediaSpan(Slice(input, 0, length)));
      appended += int64_t(length);
      while (true) {
        auto samples = WaitFor(track->GetSamples(-1));
        if (samples.isErr()) {
          if (samples.inspectErr().Code() !=
              NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
            return false;
          }
          break;
        }
      }
      const int64_t demuxed = track->GetEvictionOffset(TimeUnit::Zero());
      if (demuxed != appended) {
        return false;
      }
      resource->EvictBefore(demuxed);
      input.erase(input.begin(), input.begin() + length);
      parsingMediaSegment = false;
      parser = CreateParserWithInitData(*initData);
    }
  }
  return true;
}

}  // namespace

TEST(MPEGAudioByteStream, MutatedStreams)
{
  const Bytes mono = Frame(kMonoHeader, kMonoFrameLength);
  const Bytes mpeg2 = Frame(kMPEG2Header, kMPEG2FrameLength);
  const Bytes layer2 = Frame(kLayer2Header, kLayer2FrameLength);
  const Bytes layer1 = Frame(kLayer1Header, kLayer1FrameLength);
  const std::vector<Bytes> streams = {
      Concat({ID3v2(300), InfoFrame("Info"), StereoFrame(1), StereoFrame(2),
              StereoFrame(3), ID3v1()}),
      Concat({Text("ICY 200 OK\r\n\r\n"), StereoFrame(), StereoFrame(),
              Garbage(50), StereoFrame(), StereoFrame(), StereoFrame(), mono,
              mono, mono, mpeg2, mpeg2, layer2, layer2, layer1, layer1}),
  };
  for (const Bytes& stream : streams) {
    for (uint8_t chunkSize : {0, 3, 50, 255}) {
      Bytes input = Concat({{chunkSize}, stream});
      EXPECT_TRUE(AppendAndDemux(AsSpan(input)));
    }
  }

  std::mt19937 random(1169485);
  for (int i = 0; i < 2000; ++i) {
    Bytes input = streams[random() % streams.size()];
    for (uint32_t edits = 1 + random() % 8; edits; --edits) {
      const size_t at = random() % input.size();
      switch (random() % 3) {
        case 0:
          input[at] = uint8_t(random());
          break;
        case 1:
          input.insert(input.begin() + at, uint8_t(random()));
          break;
        default:
          input.erase(input.begin() + at);
          break;
      }
    }
    input.insert(input.begin(), uint8_t(random()));
    EXPECT_TRUE(AppendAndDemux(AsSpan(input))) << "mutation " << i;
  }
}

#ifdef FUZZING_INTERFACES
static int FuzzingRunMediaSourceMPEGAudio(const uint8_t* aData, size_t aSize) {
  MOZ_RELEASE_ASSERT(AppendAndDemux(Span(aData, aSize)));
  return 0;
}

MOZ_FUZZING_INTERFACE_RAW(nullptr, FuzzingRunMediaSourceMPEGAudio,
                          MediaSourceMPEGAudio);
#endif
