/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ADTS_H_
#define ADTS_H_

#include <stdint.h>

#include "MediaData.h"
#include "mozilla/Result.h"

namespace mozilla {
class MediaRawData;

namespace ADTS {

// adts::FrameHeader - Holds the ADTS frame header and its parsing
// state.
//
// ADTS Frame Structure
//
// 11111111 1111BCCD EEFFFFGH HHIJKLMM MMMMMMMM MMMOOOOO OOOOOOPP(QQQQQQQQ
// QQQQQQQQ)
//
// Header consists of 7 or 9 bytes(without or with CRC).
// Letter   Length(bits)  Description
// { sync } 12            syncword 0xFFF, all bits must be 1
// B        1             MPEG Version: 0 for MPEG-4, 1 for MPEG-2
// C        2             Layer: always 0
// D        1             protection absent, Warning, set to 1 if there is no
//                        CRC and 0 if there is CRC
// E        2             profile, the MPEG-4 Audio Object Type minus 1
// F        4             MPEG-4 Sampling Frequency Index (15 is forbidden)
// H        3             MPEG-4 Channel Configuration (in the case of 0, the
//                        channel configuration is sent via an in-band PCE)
// M        13            frame length, this value must include 7 or 9 bytes of
//                        header length: FrameLength =
//                          (ProtectionAbsent == 1 ? 7 : 9) + size(AACFrame)
// O        11            Buffer fullness
// P        2             Number of AAC frames(RDBs) in ADTS frame minus 1, for
//                        maximum compatibility always use 1 AAC frame per ADTS
//                        frame
// Q        16            CRC if protection absent is 0
class FrameHeader {
 public:
  uint32_t mFrameLength{};
  uint32_t mSampleRate{};
  uint32_t mSamples{};
  uint32_t mChannels{};
  uint8_t mObjectType{};
  uint8_t mSamplingIndex{};
  uint8_t mChannelConfig{};
  uint8_t mNumAACFrames{};
  bool mHaveCrc{};

  // Returns whether aPtr matches a valid ADTS header sync marker
  static bool MatchesSync(const Span<const uint8_t>& aData);
  FrameHeader();
  // Header size
  uint64_t HeaderSize() const;
  bool IsValid() const;
  // Resets the state to allow for a new parsing session.
  void Reset();

  // Returns whether the byte creates a valid sequence up to this point.
  bool Parse(const Span<const uint8_t>& aData);
};
class Frame {
 public:
  Frame();

  uint64_t Offset() const;
  size_t Length() const;
  // Returns the offset to the start of frame's raw data.
  uint64_t PayloadOffset() const;

  size_t PayloadLength() const;
  // Returns the parsed frame header.
  const FrameHeader& Header() const;
  bool IsValid() const;
  // Resets the frame header and data.
  void Reset();
  // Returns whether the valid
  bool Parse(uint64_t aOffset, const uint8_t* aStart, const uint8_t* aEnd);

 private:
  // The offset to the start of the header.
  uint64_t mOffset;
  // The currently parsed frame header.
  FrameHeader mHeader;
};

class FrameParser {
 public:
  // Returns the currently parsed frame. Reset via Reset or EndFrameSession.
  const Frame& CurrentFrame();
  // Returns the first parsed frame. Reset via Reset.
  const Frame& FirstFrame() const;
  // Resets the parser. Don't use between frames as first frame data is reset.
  void Reset();
  // Clear the last parsed frame to allow for next frame parsing, i.e.:
  // - sets PrevFrame to CurrentFrame
  // - resets the CurrentFrame
  // - resets ID3Header if no valid header was parsed yet
  void EndFrameSession();
  // Parses contents of given ByteReader for a valid frame header and returns
  // true if one was found. After returning, the variable passed to
  // 'aBytesToSkip' holds the amount of bytes to be skipped (if any) in order to
  // jump across a large ID3v2 tag spanning multiple buffers.
  bool Parse(uint64_t aOffset, const uint8_t* aStart, const uint8_t* aEnd);

 private:
  // We keep the first parsed frame around for static info access, the
  // previously parsed frame for debugging and the currently parsed frame.
  Frame mFirstFrame;
  Frame mFrame;
};

// Extract the audiospecificconfig from an ADTS header
void InitAudioSpecificConfig(const FrameHeader& aHeader,
                             MediaByteBuffer* aBuffer);
void InitAudioSpecificConfig(const Frame& aFrame, MediaByteBuffer* aBuffer);
bool StripHeader(MediaRawData* aSample);
Result<uint8_t, bool> GetFrequencyIndex(uint32_t aSamplesPerSecond);
bool ConvertSample(uint16_t aChannelCount, uint8_t aFrequencyIndex,
                   uint8_t aProfile, mozilla::MediaRawData* aSample);
bool RevertSample(MediaRawData* aSample);
Result<already_AddRefed<MediaByteBuffer>, nsresult> MakeSpecificConfig(
    uint8_t aObjectType, uint32_t aFrequency, uint32_t aChannelCount);
}  // namespace ADTS
}  // namespace mozilla

#endif
