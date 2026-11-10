/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_EME_PROTOBUFREADER_H_
#define DOM_MEDIA_EME_PROTOBUFREADER_H_

#include <cstddef>
#include <cstdint>

#include "mozilla/Span.h"
#include "nsTArray.h"

namespace mozilla {

// A minimal reader for the protobuf wire format that walks the fields of a
// message without a schema. The data is not trusted, so every read is bounds
// checked and fails on malformed input.
class ProtobufReader {
 public:
  // The wire types of a protobuf field.
  enum class WireType : uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5,
  };

  // Returns the tag that precedes a field with the given number and wire type.
  static constexpr uint64_t MakeTag(uint32_t aFieldNumber, WireType aType) {
    return (uint64_t(aFieldNumber) << kFieldNumberShift) |
           static_cast<uint8_t>(aType);
  }

  explicit ProtobufReader(Span<const uint8_t> aData) : mData(aData) {}

  bool AtEnd() const { return mPos >= mData.Length(); }

  size_t Position() const { return mPos; }

  bool ReadVarint(uint64_t& aOut) {
    aOut = 0;
    for (uint32_t shift = 0; shift < kVarintMaxBits && !AtEnd();
         shift += kVarintPayloadBits) {
      const uint8_t byte = mData[mPos++];
      aOut |= uint64_t(byte & kVarintPayloadMask) << shift;
      if (!(byte & kVarintContinuation)) {
        return true;
      }
    }
    return false;
  }

  bool ReadField(uint32_t& aFieldNumber, Span<const uint8_t>& aOut) {
    uint64_t value;
    return ReadField(aFieldNumber, aOut, value);
  }

  // Reads the next field. For a length delimited field aOut is its payload,
  // and for a varint field aValue is its value.
  bool ReadField(uint32_t& aFieldNumber, Span<const uint8_t>& aOut,
                 uint64_t& aValue) {
    uint64_t tag;
    if (!ReadVarint(tag)) {
      return false;
    }

    aFieldNumber = static_cast<uint32_t>(tag >> kFieldNumberShift);
    aOut = Span<const uint8_t>();
    aValue = 0;
    switch (static_cast<WireType>(tag & kWireTypeMask)) {
      case WireType::Varint:
        return ReadVarint(aValue);
      case WireType::Fixed64:
        return Skip(kFixed64Size);
      case WireType::LengthDelimited: {
        uint64_t length;
        if (!ReadVarint(length) || length > mData.Length() - mPos) {
          return false;
        }
        aOut = mData.Subspan(mPos, static_cast<size_t>(length));
        mPos += static_cast<size_t>(length);
        return true;
      }
      case WireType::Fixed32:
        return Skip(kFixed32Size);
      default:
        return false;
    }
  }

 private:
  static constexpr uint32_t kWireTypeMask = 0x7;
  static constexpr uint32_t kFieldNumberShift = 3;
  static constexpr uint32_t kVarintPayloadBits = 7;
  static constexpr uint32_t kVarintMaxBits = 64;
  static constexpr uint8_t kVarintPayloadMask = 0x7f;
  static constexpr uint8_t kVarintContinuation = 0x80;
  static constexpr size_t kFixed32Size = 4;
  static constexpr size_t kFixed64Size = 8;

  bool Skip(size_t aCount) {
    if (aCount > mData.Length() - mPos) {
      return false;
    }
    mPos += aCount;
    return true;
  }

  Span<const uint8_t> mData;
  size_t mPos = 0;
};

namespace widevine {

// The field of a SignedMessage that holds its type.
constexpr uint32_t kSignedMessageTypeField = 1;

enum class SignedMessageType : uint8_t {
  ServiceCertificateRequest = 4,
  ServiceCertificate = 5,
};

// The SignedMessage field that holds a License, the License field that holds a
// KeyContainer and the KeyContainer fields that hold a key ID and its type.
constexpr uint32_t kSignedMessageMsgField = 2;
constexpr uint32_t kLicenseKeyField = 3;
constexpr uint32_t kKeyContainerIdField = 1;
constexpr uint32_t kKeyContainerTypeField = 4;

// The KeyContainer type of a key that decrypts content.
constexpr uint64_t kKeyTypeContent = 2;

constexpr size_t kKeyIdSize = 16;
constexpr size_t kMaxLicenseKeyIds = 256;

// A SignedMessage that holds only its type is the tag of the type field and
// the type as a one byte varint.
constexpr uint8_t kSignedMessageTypeTag =
    static_cast<uint8_t>(ProtobufReader::MakeTag(
        kSignedMessageTypeField, ProtobufReader::WireType::Varint));
constexpr size_t kSignedMessageTypeOnlySize = 2;

// Before Android 10, AMediaDrm_getKeyRequest returns the key request followed
// by the previous one. Both are SignedMessages, which start with the type in
// field 1, so the request ends where field 1 repeats. Returns the first
// request, or all of aRequest if it cannot be parsed.
Span<const uint8_t> TrimKeyRequest(Span<const uint8_t> aRequest);

bool IsServiceCertificateRequest(Span<const uint8_t> aRequest);

bool IsServiceCertificateResponse(Span<const uint8_t> aResponse);

// Returns the IDs of the content keys in a license response, which is
// a SignedMessage (field 2: License). The License holds the KeyContainers
// (field 3), each with the key ID in field 1 and its type in field 4. The
// response is not trusted, so at most kMaxLicenseKeyIds are returned.
nsTArray<nsTArray<uint8_t>> ParseLicenseKeyIds(Span<const uint8_t> aResponse);

}  // namespace widevine

}  // namespace mozilla

#endif  // DOM_MEDIA_EME_PROTOBUFREADER_H_
