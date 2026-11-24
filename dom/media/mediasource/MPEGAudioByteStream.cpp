/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MPEGAudioByteStream.h"

#include <string.h>

#include <algorithm>

#include "Adts.h"
#include "mozilla/Assertions.h"
#include "mozilla/Casting.h"

namespace mozilla::mpeg_audio {

namespace {

constexpr size_t kID3v1Size = 128;
// An extended tag starts with "TAG+" and is followed by an ID3v1 tag.
constexpr size_t kID3v1ExtendedSize = 227;
constexpr size_t kID3v2HeaderSize = 10;
constexpr size_t kID3v2FooterSize = 10;
constexpr uint8_t kID3v2FooterPresentFlag = 0x10;
// The MSE byte stream format doesn't bound Icecast headers. This matches
// Chromium.
constexpr size_t kMaxIcecastHeaderSize = 4096;
// The number of consecutive frame headers that resynchronize the stream after
// garbage. This matches Chromium.
constexpr size_t kSyncConfirmationFrames = 3;

// Bitrates in kb/s by bitrate index, for MPEG-1 Layers I, II and III, and for
// MPEG-2 and MPEG-2.5 Layer I and Layers II and III.
constexpr uint16_t kBitrates[5][15] = {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
};
// MPEG-1 sample rates by sample rate index. MPEG-2 halves them, and MPEG-2.5
// quarters them.
constexpr uint32_t kSampleRates[3] = {44100, 48000, 32000};

template <size_t N>
bool StartsWith(Span<const uint8_t> aData, const char (&aTag)[N]) {
  constexpr size_t length = N - 1;
  return aData.Length() >= length &&
         memcmp(aData.Elements(), aTag, length) == 0;
}

bool StartsWithFrameSync(Format aFormat, Span<const uint8_t> aData) {
  // The 11 bit frame sync of MPEG-1, MPEG-2 and MPEG-2.5, or the 12 bit
  // syncword of ADTS.
  const uint8_t mask = aFormat == Format::ADTS ? 0xF0 : 0xE0;
  return aData.Length() >= 2 && aData[0] == 0xFF && (aData[1] & mask) == mask;
}

Maybe<FrameInfo> ParseADTSHeader(Span<const uint8_t> aData) {
  // This rejects a layer that isn't 0, and the sampling frequency indexes
  // that are reserved or that mean that the rate is in the stream, which
  // ADTS has no room for.
  ADTS::FrameHeader header;
  if (!header.Parse(aData)) {
    return Nothing();
  }
  FrameInfo info;
  info.mHeaderLength = AssertedCast<uint32_t>(header.HeaderSize());
  // These aren't supported:
  // - A channel configuration of 0, where a program config element in the raw
  //   data block describes the channels. Chromium accepts the frame, but
  //   fails to create a decoder for it.
  // - Several raw data blocks in a frame, which can't be split up without
  //   decoding them. Chromium accepts them.
  // - No raw data block. Chromium accepts the frame, but fails to decode it.
  if (header.mChannelConfig == 0 || header.mNumAACFrames != 1 ||
      header.mFrameLength <= info.mHeaderLength) {
    return Nothing();
  }
  info.mObjectType = header.mObjectType;
  info.mSampleRate = header.mSampleRate;
  info.mChannels = header.mChannels;
  info.mSamplesPerFrame = header.mSamples;
  info.mLength = header.mFrameLength;
  return Some(info);
}

// The Xing or Info tag follows the Layer III side information.
size_t InfoTagOffset(const FrameInfo& aFrame) {
  const bool mono = aFrame.mChannels == 1;
  if (aFrame.mIsMPEG1) {
    return kMPEGAudioHeaderSize + (mono ? 17 : 32);
  }
  return kMPEGAudioHeaderSize + (mono ? 9 : 17);
}

bool IsInfoFrame(Span<const uint8_t> aFrame, const FrameInfo& aInfo) {
  if (aInfo.mLayer != 3) {
    return false;
  }
  const size_t offset = InfoTagOffset(aInfo);
  if (aFrame.Length() < offset + 4) {
    return false;
  }
  Span<const uint8_t> tag = aFrame.Subspan(offset, 4);
  return StartsWith(tag, "Xing") || StartsWith(tag, "Info");
}

// Returns the length of the Icecast header at the start of aData, which ends
// with an empty line.
Maybe<size_t> FindIcecastHeaderLength(Span<const uint8_t> aData) {
  bool wasLineFeed = false;
  uint8_t last = 0;
  for (size_t i = 4; i < aData.Length(); ++i) {
    const uint8_t c = aData[i];
    if (c == '\n') {
      if (wasLineFeed) {
        return Some(i + 1);
      }
      wasLineFeed = true;
    } else if (c != '\r' || last != '\n') {
      wasLineFeed = false;
    }
    last = c;
  }
  return Nothing();
}

enum class SyncState { RuledOut, Undecidable, Confirmed };

SyncState ConfirmSync(Format aFormat, Span<const uint8_t> aData) {
  size_t offset = 0;
  for (size_t i = 0; i < kSyncConfirmationFrames; ++i) {
    MOZ_ASSERT(offset <= aData.Length());
    Span<const uint8_t> rest = aData.From(offset);
    if (rest.Length() < FrameHeaderSize(aFormat)) {
      return SyncState::Undecidable;
    }
    Maybe<FrameInfo> frame = ParseFrameHeader(aFormat, rest);
    if (!frame) {
      return SyncState::RuledOut;
    }
    offset += frame->mLength;
    // The next frame must start within the data.
    if (offset >= aData.Length()) {
      return SyncState::Undecidable;
    }
  }
  return SyncState::Confirmed;
}

// aData doesn't start with a unit. Skip to the first frame sync that isn't
// ruled out.
Unit SkipGarbage(Format aFormat, Span<const uint8_t> aData) {
  for (size_t i = 1; i < aData.Length(); ++i) {
    if (aData[i] == 0xFF &&
        ConfirmSync(aFormat, aData.From(i)) != SyncState::RuledOut) {
      return {Unit::Type::Garbage, i};
    }
  }
  return {Unit::Type::Garbage, aData.Length()};
}

}  // namespace

Maybe<FrameInfo> ParseFrameHeader(Format aFormat, Span<const uint8_t> aData) {
  MOZ_ASSERT(aData.Length() >= FrameHeaderSize(aFormat));
  if (!StartsWithFrameSync(aFormat, aData)) {
    return Nothing();
  }
  if (aFormat == Format::ADTS) {
    return ParseADTSHeader(aData);
  }
  // 0 is MPEG-2.5, 1 is reserved, 2 is MPEG-2 and 3 is MPEG-1.
  const uint8_t version = (aData[1] >> 3) & 0x3;
  // 0 is reserved, and 1 to 3 are Layers III to I.
  const uint8_t layerIndex = (aData[1] >> 1) & 0x3;
  const uint8_t bitrateIndex = aData[2] >> 4;
  const uint8_t sampleRateIndex = (aData[2] >> 2) & 0x3;
  const bool padding = aData[2] & 0x2;
  const bool hasCRC = !(aData[1] & 0x1);
  // Free format bitrates (index 0) aren't supported, as in Chromium.
  if (version == 1 || layerIndex == 0 || bitrateIndex == 0 ||
      bitrateIndex == 0xF || sampleRateIndex == 3) {
    return Nothing();
  }
  FrameInfo info;
  info.mHeaderLength = kMPEGAudioHeaderSize + (hasCRC ? kCRCSize : 0);
  info.mIsMPEG1 = version == 3;
  info.mLayer = 4 - layerIndex;
  info.mSampleRate = kSampleRates[sampleRateIndex] >>
                     (info.mIsMPEG1 ? 0 : (version == 2 ? 1 : 2));
  info.mChannels = (aData[3] >> 6) == 3 ? 1 : 2;
  const size_t bitrateTable =
      info.mIsMPEG1 ? info.mLayer - 1 : (info.mLayer == 1 ? 3 : 4);
  const uint32_t bitrate = kBitrates[bitrateTable][bitrateIndex] * 1000;
  if (info.mLayer == 1) {
    // Layer I frames are made of 4 byte slots.
    info.mSamplesPerFrame = 384;
    info.mLength = (12 * bitrate / info.mSampleRate + padding) * 4;
  } else {
    info.mSamplesPerFrame = info.mLayer == 3 && !info.mIsMPEG1 ? 576 : 1152;
    info.mLength =
        info.mSamplesPerFrame / 8 * bitrate / info.mSampleRate + padding;
  }
  MOZ_ASSERT(info.mLength > info.mHeaderLength);
  MOZ_ASSERT(info.mLength <= kMaxMPEGAudioFrameLength);
  return Some(info);
}

Unit ParseUnit(Format aFormat, Span<const uint8_t> aData, bool aConfirmSync,
               bool aHeaderOnly) {
  if (aData.Length() < FrameHeaderSize(aFormat)) {
    return {};
  }

  if (StartsWithFrameSync(aFormat, aData)) {
    if (aConfirmSync) {
      switch (ConfirmSync(aFormat, aData)) {
        case SyncState::RuledOut:
          return SkipGarbage(aFormat, aData);
        case SyncState::Undecidable:
          return {};
        case SyncState::Confirmed:
          break;
      }
    }
    Maybe<FrameInfo> frame = ParseFrameHeader(aFormat, aData);
    if (!frame) {
      // A frame sync that follows a unit must start a valid frame header.
      return {Unit::Type::Error};
    }
    if (aHeaderOnly) {
      return {Unit::Type::Frame, frame->mLength, *frame};
    }
    if (aData.Length() < frame->mLength) {
      return {};
    }
    const Unit::Type type = IsInfoFrame(aData.To(frame->mLength), *frame)
                                ? Unit::Type::InfoFrame
                                : Unit::Type::Frame;
    return {type, frame->mLength, *frame};
  }

  if (StartsWith(aData, "ICY ")) {
    const size_t length = std::min(aData.Length(), kMaxIcecastHeaderSize);
    if (Maybe<size_t> headerLength =
            FindIcecastHeaderLength(aData.To(length))) {
      return {Unit::Type::Metadata, *headerLength};
    }
    return length == kMaxIcecastHeaderSize ? Unit{Unit::Type::Error} : Unit{};
  }

  if (StartsWith(aData, "TAG")) {
    if (!StartsWith(aData, "TAG+")) {
      return {Unit::Type::Metadata, kID3v1Size};
    }
    // This is either an extended tag, which an ID3v1 tag follows, or an ID3v1
    // tag with a title that starts with "+".
    if (aData.Length() < kID3v1ExtendedSize + 3) {
      return {};
    }
    return {Unit::Type::Metadata,
            StartsWith(aData.From(kID3v1ExtendedSize), "TAG")
                ? kID3v1ExtendedSize
                : kID3v1Size};
  }

  if (StartsWith(aData, "ID3")) {
    if (aData.Length() < kID3v2HeaderSize) {
      return {};
    }
    // The tag size is a 28 bit "synchsafe" integer.
    uint64_t size = 0;
    for (size_t i = 6; i < kID3v2HeaderSize; ++i) {
      if (aData[i] & 0x80) {
        return {Unit::Type::Error};
      }
      size = (size << 7) | aData[i];
    }
    if (aData[5] & kID3v2FooterPresentFlag) {
      size += kID3v2FooterSize;
    }
    return {Unit::Type::Metadata, kID3v2HeaderSize + size};
  }

  return SkipGarbage(aFormat, aData);
}

}  // namespace mozilla::mpeg_audio
