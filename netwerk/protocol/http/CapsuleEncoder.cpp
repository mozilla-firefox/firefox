/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CapsuleEncoder.h"

#include "Capsule.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::net {

CapsuleEncoder::CapsuleEncoder() {
  NeqoEncoder::Init(getter_AddRefs(mEncoder));
  MOZ_ASSERT(mEncoder);
}

CapsuleEncoder::~CapsuleEncoder() = default;

void CapsuleEncoder::EncodeCapsule(Capsule& aCapsule) {
  if (aCapsule.mCapsule.is<UnknownCapsule>()) {
    auto& value = aCapsule.mCapsule.as<UnknownCapsule>();
    EncodeVarint(value.mType).EncodeBufferWithVarintLen(value.mData);
    return;
  }

  if (aCapsule.mCapsule.is<CloseWebTransportSessionCapsule>()) {
    const auto& value = aCapsule.mCapsule.as<CloseWebTransportSessionCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(4 + value.mReason.Length())
        .EncodeUint(4, value.mStatus)
        .EncodeString(value.mReason);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportMaxDataCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportMaxDataCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mMaxDataSize))
        .EncodeVarint(value.mMaxDataSize);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportStreamDataCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportStreamDataCapsule>();
    uint64_t length =
        CapsuleEncoder::VarintLength(value.mID) + value.mData.Length();
    EncodeVarint(value.Type())
        .EncodeVarint(length)
        .EncodeVarint(value.mID)
        .EncodeBuffer(value.mData);
    mStreamMetadata = Some(StreamMetadata{
        value.mID, value.mData.Length(),
        CapsuleEncoder::VarintLength(static_cast<uint64_t>(value.Type())) +
            CapsuleEncoder::VarintLength(length) +
            CapsuleEncoder::VarintLength(value.mID)});
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportStreamsBlockedCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportStreamsBlockedCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mLimit))
        .EncodeVarint(value.mLimit);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportMaxStreamsCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportMaxStreamsCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mLimit))
        .EncodeVarint(value.mLimit);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportStreamDataBlockedCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportStreamDataBlockedCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mID) +
                      CapsuleEncoder::VarintLength(value.mLimit))
        .EncodeVarint(value.mID)
        .EncodeVarint(value.mLimit);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportMaxStreamDataCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportMaxStreamDataCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mID) +
                      CapsuleEncoder::VarintLength(value.mLimit))
        .EncodeVarint(value.mID)
        .EncodeVarint(value.mLimit);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportDataBlockedCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportDataBlockedCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mLimit))
        .EncodeVarint(value.mLimit);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportStopSendingCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportStopSendingCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mID) +
                      CapsuleEncoder::VarintLength(value.mErrorCode))
        .EncodeVarint(value.mID)
        .EncodeVarint(value.mErrorCode);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportResetStreamCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportResetStreamCapsule>();
    EncodeVarint(value.Type())
        .EncodeVarint(CapsuleEncoder::VarintLength(value.mID) +
                      CapsuleEncoder::VarintLength(value.mErrorCode) +
                      CapsuleEncoder::VarintLength(value.mReliableSize))
        .EncodeVarint(value.mID)
        .EncodeVarint(value.mErrorCode)
        .EncodeVarint(value.mReliableSize);
    return;
  }

  if (aCapsule.mCapsule.is<WebTransportDatagramCapsule>()) {
    auto& value = aCapsule.mCapsule.as<WebTransportDatagramCapsule>();
    uint64_t length = value.mPayload.Length();
    EncodeVarint(value.Type())
        .EncodeVarint(length)
        .EncodeBuffer(value.mPayload);
    return;
  }
}

CapsuleEncoder& CapsuleEncoder::EncodeByte(uint8_t aData) {
  mEncoder->EncodeByte(aData);
  return *this;
}

template <typename T>
CapsuleEncoder& CapsuleEncoder::EncodeUint(uint32_t aSize, T aValue) {
  uint64_t value = static_cast<uint64_t>(aValue);
  mEncoder->EncodeUint(aSize, value);
  return *this;
}

template <typename T>
CapsuleEncoder& CapsuleEncoder::EncodeVarint(T aValue) {
  uint64_t value = static_cast<uint64_t>(aValue);
  mEncoder->EncodeVarint(value);
  return *this;
}

CapsuleEncoder& CapsuleEncoder::EncodeString(const nsACString& aData) {
  mEncoder->EncodeBuffer(reinterpret_cast<const uint8_t*>(aData.BeginReading()),
                         aData.Length());
  return *this;
}

CapsuleEncoder& CapsuleEncoder::EncodeBuffer(nsTArray<uint8_t>& aData) {
  mEncoder->EncodeBuffer(aData.Elements(), aData.Length());
  return *this;
}

CapsuleEncoder& CapsuleEncoder::EncodeBufferWithVarintLen(
    nsTArray<uint8_t>& aData) {
  mEncoder->EncodeBufferWithVarintLen(aData.Elements(), aData.Length());
  return *this;
}

// static
size_t CapsuleEncoder::VarintLength(uint64_t aValue) {
  return NeqoEncoder::VarintLength(aValue);
}

mozilla::Span<const uint8_t> CapsuleEncoder::GetBuffer() {
  const uint8_t* buffer = nullptr;
  uint32_t length = 0;
  mEncoder->GetData(&buffer, &length);
  return Span<const uint8_t>(buffer, length);
}

}  // namespace mozilla::net
