/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Adts.h"

#include "ADTSDemuxer.h"
#include "BitWriter.h"
#include "MediaData.h"
#include "MediaDataDemuxer.h"
#include "PlatformDecoderModule.h"
#include "mozilla/Array.h"
#include "mozilla/Logging.h"

#define LOG(...)                                 \
  MOZ_LOG_FMT(gMediaDemuxerLog, LogLevel::Debug, \
              MOZ_LOG_EXPAND_ARGS __VA_ARGS__)
#define ADTSLOG(msg, ...) \
  DDMOZ_LOG_FMT(gMediaDemuxerLog, LogLevel::Debug, msg, ##__VA_ARGS__)
#define ADTSLOGV(msg, ...) \
  DDMOZ_LOG_FMT(gMediaDemuxerLog, LogLevel::Verbose, msg, ##__VA_ARGS__)

namespace mozilla {
namespace ADTS {

static const int kADTSHeaderSize = 7;

constexpr std::array FREQ_LOOKUP{96000, 88200, 64000, 48000, 44100,
                                 32000, 24000, 22050, 16000, 12000,
                                 11025, 8000,  7350,  0};

Result<uint8_t, bool> GetFrequencyIndex(uint32_t aSamplesPerSecond) {
  auto found =
      std::find(FREQ_LOOKUP.begin(), FREQ_LOOKUP.end(), aSamplesPerSecond);

  if (found == FREQ_LOOKUP.end()) {
    return Err(false);
  }

  return std::distance(FREQ_LOOKUP.begin(), found);
}

bool ConvertSample(uint16_t aChannelCount, uint8_t aFrequencyIndex,
                   uint8_t aProfile, MediaRawData* aSample) {
  size_t newSize = aSample->Size() + kADTSHeaderSize;

  MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
              "Converting sample to ADTS format: newSize: {}, ch: {}, "
              "profile: {}, freq index: {}",
              newSize, aChannelCount, aProfile, aFrequencyIndex);

  // ADTS header uses 13 bits for packet size.
  if (newSize >= (1 << 13) || aChannelCount > 15 || aProfile < 1 ||
      aProfile > 4 || aFrequencyIndex >= FREQ_LOOKUP.size()) {
    MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                "Couldn't convert sample to ADTS format: newSize: {}, ch: {}, "
                "profile: {}, freq index: {}",
                newSize, aChannelCount, aProfile, aFrequencyIndex);
    return false;
  }

  Array<uint8_t, kADTSHeaderSize> header;
  header[0] = 0xff;
  header[1] = 0xf1;
  header[2] =
      ((aProfile - 1) << 6) + (aFrequencyIndex << 2) + (aChannelCount >> 2);
  header[3] = ((aChannelCount & 0x3) << 6) + (newSize >> 11);
  header[4] = (newSize & 0x7ff) >> 3;
  header[5] = ((newSize & 7) << 5) + 0x1f;
  header[6] = 0xfc;

  UniquePtr<MediaRawDataWriter> writer(aSample->CreateWriter());
  if (!writer->Prepend(&header[0], std::size(header))) {
    return false;
  }

  if (aSample->mCrypto.IsEncrypted()) {
    if (aSample->mCrypto.mPlainSizes.Length() == 0) {
      writer->mCrypto.mPlainSizes.AppendElement(kADTSHeaderSize);
      writer->mCrypto.mEncryptedSizes.AppendElement(aSample->Size() -
                                                    kADTSHeaderSize);
    } else {
      writer->mCrypto.mPlainSizes[0] += kADTSHeaderSize;
    }
  }

  return true;
}

bool StripHeader(MediaRawData* aSample) {
  if (aSample->Size() < kADTSHeaderSize) {
    return false;
  }

  FrameHeader header;
  auto data = Span{aSample->Data(), aSample->Size()};
  MOZ_ASSERT(FrameHeader::MatchesSync(data),
             "Don't attempt to strip the ADTS header of a raw AAC packet.");

  bool crcPresent = header.mHaveCrc;

  LOG(("Stripping ADTS, crc {}present", crcPresent ? "" : "not "));

  size_t toStrip = crcPresent ? kADTSHeaderSize + 2 : kADTSHeaderSize;

  UniquePtr<MediaRawDataWriter> writer(aSample->CreateWriter());
  writer->PopFront(toStrip);

  if (aSample->mCrypto.IsEncrypted()) {
    if (aSample->mCrypto.mPlainSizes.Length() > 0 &&
        writer->mCrypto.mPlainSizes[0] >= kADTSHeaderSize) {
      writer->mCrypto.mPlainSizes[0] -= kADTSHeaderSize;
    }
  }

  return true;
}

bool RevertSample(MediaRawData* aSample) {
  if (aSample->Size() < kADTSHeaderSize) {
    return false;
  }

  {
    const uint8_t* header = aSample->Data();
    if (header[0] != 0xff || header[1] != 0xf1 || header[6] != 0xfc) {
      // Not ADTS.
      return false;
    }
  }

  UniquePtr<MediaRawDataWriter> writer(aSample->CreateWriter());
  writer->PopFront(kADTSHeaderSize);

  if (aSample->mCrypto.IsEncrypted()) {
    if (aSample->mCrypto.mPlainSizes.Length() > 0 &&
        writer->mCrypto.mPlainSizes[0] >= kADTSHeaderSize) {
      writer->mCrypto.mPlainSizes[0] -= kADTSHeaderSize;
    }
  }

  return true;
}

bool FrameHeader::MatchesSync(const Span<const uint8_t>& aData) {
  return aData.Length() >= 2 && aData[0] == 0xFF && (aData[1] & 0xF6) == 0xF0;
}

FrameHeader::FrameHeader() { Reset(); }

// Header size
uint64_t FrameHeader::HeaderSize() const { return (mHaveCrc) ? 9 : 7; }

bool FrameHeader::IsValid() const { return mFrameLength > 0; }

// Resets the state to allow for a new parsing session.
void FrameHeader::Reset() { PodZero(this); }

// Returns whether the byte creates a valid sequence up to this point.
bool FrameHeader::Parse(const Span<const uint8_t>& aData) {
  if (!MatchesSync(aData)) {
    return false;
  }

  // AAC has 1024 samples per frame per channel.
  mSamples = 1024;

  mHaveCrc = !(aData[1] & 0x01);
  mObjectType = ((aData[2] & 0xC0) >> 6) + 1;
  mSamplingIndex = (aData[2] & 0x3C) >> 2;
  mChannelConfig = (aData[2] & 0x01) << 2 | (aData[3] & 0xC0) >> 6;
  mFrameLength =
      static_cast<uint32_t>((aData[3] & 0x03) << 11 | (aData[4] & 0xFF) << 3 |
                            (aData[5] & 0xE0) >> 5);
  mNumAACFrames = (aData[6] & 0x03) + 1;

  static const uint32_t SAMPLE_RATES[] = {96000, 88200, 64000, 48000, 44100,
                                          32000, 24000, 22050, 16000, 12000,
                                          11025, 8000,  7350};
  if (mSamplingIndex >= std::size(SAMPLE_RATES)) {
    LOG(("ADTS: Init() failure: invalid sample-rate index value: {}.",
         mSamplingIndex));
    // This marks the header as invalid.
    mFrameLength = 0;
    return false;
  }
  mSampleRate = SAMPLE_RATES[mSamplingIndex];

  MOZ_ASSERT(mChannelConfig < 8);
  mChannels = (mChannelConfig == 7) ? 8 : mChannelConfig;

  return true;
}

Frame::Frame() : mOffset(0), mHeader() {}
uint64_t Frame::Offset() const { return mOffset; }
size_t Frame::Length() const {
  // TODO: If fields are zero'd when invalid, this check wouldn't be
  // necessary.
  if (!mHeader.IsValid()) {
    return 0;
  }

  return mHeader.mFrameLength;
}

// Returns the offset to the start of frame's raw data.
uint64_t Frame::PayloadOffset() const { return mOffset + mHeader.HeaderSize(); }

// Returns the length of the frame's raw data (excluding the header) in bytes.
size_t Frame::PayloadLength() const {
  // TODO: If fields are zero'd when invalid, this check wouldn't be
  // necessary.
  if (!mHeader.IsValid()) {
    return 0;
  }

  return mHeader.mFrameLength - mHeader.HeaderSize();
}

// Returns the parsed frame header.
const FrameHeader& Frame::Header() const { return mHeader; }

bool Frame::IsValid() const { return mHeader.IsValid(); }

// Resets the frame header and data.
void Frame::Reset() {
  mHeader.Reset();
  mOffset = 0;
}

// Returns whether the valid
bool Frame::Parse(uint64_t aOffset, const uint8_t* aStart,
                  const uint8_t* aEnd) {
  MOZ_ASSERT(aStart && aEnd && aStart <= aEnd);

  bool found = false;
  const uint8_t* ptr = aStart;
  // Require at least 7 bytes of data at the end of the buffer for the minimum
  // ADTS frame header.
  while (ptr < aEnd - 7 && !found) {
    found = mHeader.Parse(Span(ptr, aEnd));
    ptr++;
  }

  mOffset = aOffset + (static_cast<size_t>(ptr - aStart)) - 1u;

  return found;
}

const Frame& FrameParser::CurrentFrame() { return mFrame; }

const Frame& FrameParser::FirstFrame() const { return mFirstFrame; }

void FrameParser::Reset() {
  EndFrameSession();
  mFirstFrame.Reset();
}

void FrameParser::EndFrameSession() { mFrame.Reset(); }

bool FrameParser::Parse(uint64_t aOffset, const uint8_t* aStart,
                        const uint8_t* aEnd) {
  const bool found = mFrame.Parse(aOffset, aStart, aEnd);

  if (mFrame.Length() && !mFirstFrame.Length()) {
    mFirstFrame = mFrame;
  }

  return found;
}

// Initialize the AAC AudioSpecificConfig.
// Only handles two-byte version for AAC-LC.
void InitAudioSpecificConfig(const ADTS::Frame& frame,
                             MediaByteBuffer* aBuffer) {
  InitAudioSpecificConfig(frame.Header(), aBuffer);
}

void InitAudioSpecificConfig(const ADTS::FrameHeader& aHeader,
                             MediaByteBuffer* aBuffer) {
  MOZ_ASSERT(aHeader.IsValid());

  int audioObjectType = aHeader.mObjectType;
  int samplingFrequencyIndex = aHeader.mSamplingIndex;
  int channelConfig = aHeader.mChannelConfig;

  uint8_t asc[2];
  asc[0] = (audioObjectType & 0x1F) << 3 | (samplingFrequencyIndex & 0x0E) >> 1;
  asc[1] = (samplingFrequencyIndex & 0x01) << 7 | (channelConfig & 0x0F) << 3;

  aBuffer->AppendElements(asc, 2);
}

// https://wiki.multimedia.cx/index.php/MPEG-4_Audio#Audio_Specific_Config
Result<already_AddRefed<MediaByteBuffer>, nsresult> MakeSpecificConfig(
    uint8_t aObjectType, uint32_t aFrequency, uint32_t aChannelCount) {
  if (aObjectType > 45 /* USAC */ || aObjectType == 0x1F /* Escape value */) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  if (aFrequency > 0x00FFFFFF /* max value of 24 bits */) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  if (aChannelCount > 8 || aChannelCount == 7) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  uint8_t index = GetFrequencyIndex(aFrequency)
                      .unwrapOr(0x0F /* frequency is written explictly */);
  MOZ_ASSERT(index <= 0x0F /* index needs only 4 bits */);

  uint8_t channelConfig =
      aChannelCount == 8 ? aChannelCount - 1 : aChannelCount;

  RefPtr<MediaByteBuffer> buffer = new MediaByteBuffer();
  BitWriter bw(buffer);

  if (aObjectType < 0x1F /* Escape value */) {
    bw.WriteBits(aObjectType, 5);
  } else {  // If object type needs more than 5 bits
    MOZ_ASSERT(aObjectType >= 32);
    bw.WriteBits(0x1F, 5);
    // Since aObjectType < 0x3F + 32, it's safe to put it into 6 bits.
    bw.WriteBits(aObjectType - 32, 6);
  }

  bw.WriteBits(index, 4);
  if (index == 0x0F /* frequency is written explictly */) {
    bw.WriteBits(aFrequency, 24);
  }

  bw.WriteBits(channelConfig, 4);

  // Skip extension configuration for now.

  return buffer.forget();
}

};  // namespace ADTS
};  // namespace mozilla

#undef LOG
#undef ADTSLOG
#undef ADTSLOGV
