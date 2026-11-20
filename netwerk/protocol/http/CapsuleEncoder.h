/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_capsule_encoder_h
#define mozilla_net_capsule_encoder_h

#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"
#include "mozilla/net/NeqoHttp3Conn.h"
#include "nsTArray.h"

namespace mozilla::net {

class Capsule;

struct StreamMetadata {
  uint64_t mID{0};
  size_t mDataSize{0};
  uint64_t mStartOfData{0};
};

// A wrapper of neqo_common::Encoder.
class CapsuleEncoder final {
 public:
  CapsuleEncoder();
  ~CapsuleEncoder();

  // Serializes the given capsule into the internal buffer owned by mEncoder.
  void EncodeCapsule(Capsule& aCapsule);
  // Provides external access to the mEncoder's internal buffer which contains
  // the serialized capsule data. This function outputs a pointer to the start
  // of the buffer along with its current length.
  mozilla::Span<const uint8_t> GetBuffer();

  Maybe<StreamMetadata>& GetStreamMetadata() { return mStreamMetadata; }

 private:
  RefPtr<NeqoEncoder> mEncoder;

  CapsuleEncoder& EncodeByte(uint8_t aData);

  template <typename T>
  CapsuleEncoder& EncodeUint(uint32_t aSize, T aValue);

  template <typename T>
  CapsuleEncoder& EncodeVarint(T aValue);

  CapsuleEncoder& EncodeString(const nsACString& aData);
  CapsuleEncoder& EncodeBuffer(nsTArray<uint8_t>& aData);
  CapsuleEncoder& EncodeBufferWithVarintLen(nsTArray<uint8_t>& aData);

  static size_t VarintLength(uint64_t aValue);

  // When this encoder contains a WT_STREAM capsule, this field tracks metadata
  // for the stream data being sent.
  Maybe<StreamMetadata> mStreamMetadata;
};

}  // namespace mozilla::net

#endif
