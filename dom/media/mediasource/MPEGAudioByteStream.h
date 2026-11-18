/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAM_H_
#define DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAM_H_

#include <stddef.h>
#include <stdint.h>

#include "mozilla/Maybe.h"
#include "mozilla/Span.h"

// Framing of the MSE byte stream format for MPEG audio ("audio/mpeg"):
// https://w3c.github.io/mse-byte-stream-format-mpeg-audio/
namespace mozilla::mpeg_audio {

static constexpr size_t kFrameHeaderSize = 4;
// The length of an MPEG-2.5 Layer II frame at 160 kb/s and 8000 Hz, with
// padding.
static constexpr size_t kMaxFrameLength = 2881;

// The properties of a frame header that MSE needs.
struct FrameInfo {
  uint32_t mSampleRate = 0;
  uint32_t mChannels = 0;
  // 1, 2 or 3.
  uint32_t mLayer = 0;
  uint32_t mSamplesPerFrame = 0;
  // Length of the whole frame, header included.
  uint32_t mLength = 0;
  bool mIsMPEG1 = false;

  // Whether a frame with this header needs a new track description.
  bool HasSameConfig(const FrameInfo& aOther) const {
    return mSampleRate == aOther.mSampleRate && mChannels == aOther.mChannels &&
           mLayer == aOther.mLayer;
  }
};

// Parses the MPEG audio frame header at the start of aData, which holds at
// least kFrameHeaderSize bytes.
Maybe<FrameInfo> ParseFrameHeader(Span<const uint8_t> aData);

struct Unit {
  enum class Type : uint8_t {
    // More bytes are needed to classify the start of the data.
    NeedMoreData,
    // An MPEG audio frame of mLength bytes.
    Frame,
    // A Xing or Info header frame of mLength bytes. It's an MPEG audio frame,
    // but it carries no audio, so it's dropped as in Chromium and file
    // playback rather than buffered as the byte stream format says.
    InfoFrame,
    // An ID3v1, ID3v2 or Icecast metadata frame of mLength bytes. mLength can
    // be larger than the data that was parsed.
    Metadata,
    // mLength bytes that belong to no frame.
    Garbage,
    // Bytes that violate the byte stream format.
    Error,
  };

  Type mType = Type::NeedMoreData;
  uint64_t mLength = 0;
  // Set for Frame and InfoFrame.
  FrameInfo mFrame;
};

// Classifies the bytes at the start of aData.
//
// aConfirmSync is true after Garbage: a frame header at the start of aData is
// then only accepted once two more frame headers follow it.
//
// If aHeaderOnly is true, a frame is returned as soon as its header is
// available, and is never classified as an InfoFrame.
Unit ParseUnit(Span<const uint8_t> aData, bool aConfirmSync,
               bool aHeaderOnly = false);

}  // namespace mozilla::mpeg_audio

#endif  // DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAM_H_
