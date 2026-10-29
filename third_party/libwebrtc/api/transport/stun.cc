/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/transport/stun.h"

#include <algorithm>  // IWYU pragma: keep
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/crc32.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/span_helpers.h"
#include "rtc_base/string_utils.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

namespace {

const int k127Utf8CharactersLengthInBytes = 508;
const int kMessageIntegrityAttributeLength = 20;
const int kTheoreticalMaximumAttributeLength = 65535;

uint32_t ReduceTransactionId(absl::string_view transaction_id) {
  RTC_DCHECK(transaction_id.length() == kStunTransactionIdLength ||
             transaction_id.length() == kStunLegacyTransactionIdLength)
      << transaction_id.length();
  ByteBufferReader reader(
      std::span(reinterpret_cast<const uint8_t*>(transaction_id.data()),
                transaction_id.size()));
  uint32_t result = 0;
  uint32_t next;
  while (reader.ReadUInt32(&next)) {
    result ^= next;
  }
  return result;
}

// Check the maximum length of a BYTE_STRING attribute against specifications.
bool LengthValid(int type, int length) {
  // "Less than 509 bytes" is intended to indicate a maximum of 127
  // UTF-8 characters, which may take up to 4 bytes per character.
  switch (type) {
    case STUN_ATTR_USERNAME:
      return length <=
             k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.3
    case STUN_ATTR_MESSAGE_INTEGRITY:
      return length ==
             kMessageIntegrityAttributeLength;  // RFC 8489 section 14.5
    case STUN_ATTR_REALM:
      return length <=
             k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.9
    case STUN_ATTR_NONCE:
      return length <=
             k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.10
    case STUN_ATTR_SOFTWARE:
      return length <=
             k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.14
    case STUN_ATTR_DATA:
      // No length restriction in RFC; it's the content of an UDP datagram,
      // which in theory can be up to 65.535 bytes.
      // TODO(bugs.webrtc.org/12179): Write a test to find the real limit.
      return length <= kTheoreticalMaximumAttributeLength;
    default:
      // Return an arbitrary restriction for all other types.
      return length <= kTheoreticalMaximumAttributeLength;
  }
  RTC_DCHECK_NOTREACHED();
  return true;
}

bool IsGoogPingMethod(uint16_t type) {
  return type == GOOG_PING_REQUEST || type == GOOG_PING_RESPONSE ||
         type == GOOG_PING_ERROR_RESPONSE;
}

}  // namespace

const char STUN_ERROR_REASON_TRY_ALTERNATE_SERVER[] = "Try Alternate Server";
const char STUN_ERROR_REASON_BAD_REQUEST[] = "Bad Request";
const char STUN_ERROR_REASON_UNAUTHORIZED[] = "Unauthorized";
const char STUN_ERROR_REASON_UNKNOWN_ATTRIBUTE[] = "Unknown Attribute";
const char STUN_ERROR_REASON_FORBIDDEN[] = "Forbidden";
const char STUN_ERROR_REASON_ALLOCATION_MISMATCH[] = "Allocation Mismatch";
const char STUN_ERROR_REASON_STALE_NONCE[] = "Stale Nonce";
const char STUN_ERROR_REASON_WRONG_CREDENTIALS[] = "Wrong Credentials";
const char STUN_ERROR_REASON_UNSUPPORTED_PROTOCOL[] = "Unsupported Protocol";
const char STUN_ERROR_REASON_ROLE_CONFLICT[] = "Role Conflict";
const char STUN_ERROR_REASON_SERVER_ERROR[] = "Server Error";

const char EMPTY_TRANSACTION_ID[] = "0000000000000000";
const uint32_t STUN_FINGERPRINT_XOR_VALUE = 0x5354554E;

// StunMessage

StunMessage::StunMessage()
    : StunMessage(STUN_INVALID_MESSAGE_TYPE, EMPTY_TRANSACTION_ID) {}

StunMessage::StunMessage(uint16_t type)
    : StunMessage(type, GenerateTransactionId()) {}

StunMessage::StunMessage(uint16_t type, absl::string_view transaction_id)
    : type_(type),
      transaction_id_(transaction_id),
      reduced_transaction_id_(ReduceTransactionId(transaction_id_)) {
  RTC_DCHECK(IsValidTransactionId(transaction_id_));
}

StunMessage::~StunMessage() = default;

bool StunMessage::IsLegacy() const {
  if (transaction_id_.size() == kStunLegacyTransactionIdLength)
    return true;
  RTC_DCHECK(transaction_id_.size() == kStunTransactionIdLength);
  return false;
}

static bool DesignatedExpertRange(int attr_type) {
  return (attr_type >= 0x4000 && attr_type <= 0x7FFF) ||
         (attr_type >= 0xC000 && attr_type <= 0xFFFF);
}

void StunMessage::AddAttribute(std::unique_ptr<StunAttribute> attr) {
  // Once a message is signed, adding attributes is a programming error.
  // Exceptions are the Integrity attributes and Fingerprint.
  if (attr->type() != STUN_ATTR_MESSAGE_INTEGRITY &&
      attr->type() != STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32 &&
      attr->type() != STUN_ATTR_FINGERPRINT) {
    RTC_CHECK(integrity_ != IntegrityStatus::kIntegrityOk)
        << "You cannot add attributes to a signed STUN message."
        << " Message type was 0x" << ToHex(attr->type());
  }
  // Fail any attributes that aren't valid for this type of message,
  // but allow any type for the range that in the RFC is reserved for
  // the "designated experts".
  if (!DesignatedExpertRange(attr->type())) {
    RTC_DCHECK_EQ(attr->value_type(), GetAttributeValueType(attr->type()));
  }

  attr->SetOwner(this);
  size_t attr_length = attr->length();
  if (attr_length % 4 != 0) {
    attr_length += (4 - (attr_length % 4));
  }
  length_ += static_cast<uint16_t>(attr_length + 4);

  attrs_.push_back(std::move(attr));
}

std::unique_ptr<StunAttribute> StunMessage::RemoveAttribute(int type) {
  if (type != STUN_ATTR_MESSAGE_INTEGRITY &&
      type != STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32) {
    // Removing attributes will break integrity, so don't allow
    // removing attributes from a signed message.
    RTC_CHECK(integrity_ != IntegrityStatus::kIntegrityOk);
  }
  std::unique_ptr<StunAttribute> attribute;
  for (auto it = attrs_.rbegin(); it != attrs_.rend(); ++it) {
    if ((*it)->type() == type) {
      attribute = std::move(*it);
      attrs_.erase(std::next(it).base());
      break;
    }
  }
  if (attribute) {
    attribute->SetOwner(nullptr);
    size_t attr_length = attribute->length();
    if (attr_length % 4 != 0) {
      attr_length += (4 - (attr_length % 4));
    }
    length_ -= static_cast<uint16_t>(attr_length + 4);
    if (type == STUN_ATTR_MESSAGE_INTEGRITY ||
        type == STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32) {
      // If the message was signed, it is now unsigned, and adding more
      // attributes is possible.
      integrity_ = IntegrityStatus::kNotSet;
    }
  }
  return attribute;
}

void StunMessage::ClearAttributes() {
  for (auto it = attrs_.rbegin(); it != attrs_.rend(); ++it) {
    (*it)->SetOwner(nullptr);
  }
  attrs_.clear();
  length_ = 0;
  integrity_ = IntegrityStatus::kNotSet;
}

std::vector<uint16_t> StunMessage::GetNonComprehendedAttributes() const {
  std::vector<uint16_t> unknown_attributes;
  for (auto& attr : attrs_) {
    // "comprehension-required" range is 0x0000-0x7FFF.
    if (attr->type() >= 0x0000 && attr->type() <= 0x7FFF &&
        GetAttributeValueType(attr->type()) == STUN_VALUE_UNKNOWN) {
      unknown_attributes.push_back(attr->type());
    }
  }
  return unknown_attributes;
}

const StunAddressAttribute* StunMessage::GetAddress(int type) const {
  switch (type) {
    case STUN_ATTR_MAPPED_ADDRESS: {
      // Return XOR-MAPPED-ADDRESS when MAPPED-ADDRESS attribute is
      // missing.
      const StunAttribute* mapped_address =
          GetAttribute(STUN_ATTR_MAPPED_ADDRESS);
      if (!mapped_address)
        mapped_address = GetAttribute(STUN_ATTR_XOR_MAPPED_ADDRESS);
      return reinterpret_cast<const StunAddressAttribute*>(mapped_address);
    }

    default:
      return static_cast<const StunAddressAttribute*>(GetAttribute(type));
  }
}

const StunUInt32Attribute* StunMessage::GetUInt32(int type) const {
  return static_cast<const StunUInt32Attribute*>(GetAttribute(type));
}

const StunUInt64Attribute* StunMessage::GetUInt64(int type) const {
  return static_cast<const StunUInt64Attribute*>(GetAttribute(type));
}

const StunByteStringAttribute* StunMessage::GetByteString(int type) const {
  return static_cast<const StunByteStringAttribute*>(GetAttribute(type));
}

const StunUInt16ListAttribute* StunMessage::GetUInt16List(int type) const {
  return static_cast<const StunUInt16ListAttribute*>(GetAttribute(type));
}

const StunErrorCodeAttribute* StunMessage::GetErrorCode() const {
  return static_cast<const StunErrorCodeAttribute*>(
      GetAttribute(STUN_ATTR_ERROR_CODE));
}

int StunMessage::GetErrorCodeValue() const {
  const StunErrorCodeAttribute* error_attribute = GetErrorCode();
  return error_attribute ? error_attribute->code() : STUN_ERROR_GLOBAL_FAILURE;
}

const StunUInt16ListAttribute* StunMessage::GetUnknownAttributes() const {
  return static_cast<const StunUInt16ListAttribute*>(
      GetAttribute(STUN_ATTR_UNKNOWN_ATTRIBUTES));
}

StunMessage::IntegrityStatus StunMessage::ValidateMessageIntegrity(
    const std::string& password) {
  RTC_DCHECK(integrity_ == IntegrityStatus::kNotSet)
      << "Usage error: Verification should only be done once";
  password_ = password;
  if (GetByteString(STUN_ATTR_MESSAGE_INTEGRITY)) {
    if (ValidateMessageIntegrityOfType(STUN_ATTR_MESSAGE_INTEGRITY,
                                       kStunMessageIntegritySize, buffer_,
                                       password)) {
      integrity_ = IntegrityStatus::kIntegrityOk;
    } else {
      integrity_ = IntegrityStatus::kIntegrityBad;
    }
  } else if (IsGoogPingMethod(type_) &&
             GetByteString(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32)) {
    if (ValidateMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32,
                                       kStunMessageIntegrity32Size, buffer_,
                                       password)) {
      integrity_ = IntegrityStatus::kIntegrityOk;
    } else {
      integrity_ = IntegrityStatus::kIntegrityBad;
    }
  } else {
    integrity_ = IntegrityStatus::kNoIntegrity;
  }
  // Log the result of integrity checking. See crbug.com/1177125 for background.
  // Convert args to integer for the benefit of the macros.
  int bucket_count = static_cast<int>(IntegrityStatus::kMaxValue) + 1;
  int integrity = static_cast<int>(integrity_);
  if (IsStunRequestType(type_)) {
    RTC_HISTOGRAM_ENUMERATION("WebRTC.Stun.Integrity.Request", integrity,
                              bucket_count);
  } else if (IsStunSuccessResponseType(type_)) {
    RTC_HISTOGRAM_ENUMERATION("WebRTC.Stun.Integrity.Response", integrity,
                              bucket_count);
  } else if (IsStunIndicationType(type_)) {
    RTC_HISTOGRAM_ENUMERATION("WebRTC.Stun.Integrity.Indication", integrity,
                              bucket_count);
  } else {
    RTC_DCHECK(IsStunErrorResponseType(type_));
    auto* error_attribute = GetErrorCode();
    if (!error_attribute) {
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.Stun.Integrity.ErrorResponse.NoErrorAttribute", integrity,
          bucket_count);
    } else {
      switch (error_attribute->code()) {
        case STUN_ERROR_TRY_ALTERNATE:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.TryAlternate", integrity,
              bucket_count);
          break;
        case STUN_ERROR_BAD_REQUEST:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.BadRequest", integrity,
              bucket_count);
          break;
        case STUN_ERROR_UNAUTHORIZED:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.Unauthorized", integrity,
              bucket_count);
          break;
        case STUN_ERROR_UNKNOWN_ATTRIBUTE:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.UnknownAttribute", integrity,
              bucket_count);
          break;
        case STUN_ERROR_STALE_NONCE:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.StaleNonce", integrity,
              bucket_count);
          break;
        case STUN_ERROR_SERVER_ERROR:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.ServerError", integrity,
              bucket_count);
          break;
        case STUN_ERROR_GLOBAL_FAILURE:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.GlobalFailure", integrity,
              bucket_count);
          break;
        default:
          RTC_HISTOGRAM_ENUMERATION(
              "WebRTC.Stun.Integrity.ErrorResponse.ErrorOther", integrity,
              bucket_count);
          break;
      }
    }
  }
  return integrity_;
}

StunMessage::IntegrityStatus StunMessage::RevalidateMessageIntegrity(
    const std::string& password) {
  RTC_LOG(LS_INFO) << "Message revalidation, old status was "
                   << static_cast<int>(integrity_);
  integrity_ = IntegrityStatus::kNotSet;
  return ValidateMessageIntegrity(password);
}

bool StunMessage::ValidateMessageIntegrityForTesting(
    const std::string& password,
    std::span<const uint8_t> data) {
  return ValidateMessageIntegrityOfType(
      STUN_ATTR_MESSAGE_INTEGRITY, kStunMessageIntegritySize, data, password);
}

bool StunMessage::ValidateMessageIntegrity32ForTesting(
    const std::string& password,
    std::span<const uint8_t> data) {
  return ValidateMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32,
                                        kStunMessageIntegrity32Size, data,
                                        password);
}

// Verifies a STUN message has a valid MESSAGE-INTEGRITY attribute, using the
// procedure outlined in RFC 5389, section 15.4.
bool StunMessage::ValidateMessageIntegrityOfType(int mi_attr_type,
                                                 size_t mi_attr_size,
                                                 std::span<const uint8_t> data,
                                                 const std::string& password) {
  RTC_DCHECK(mi_attr_size <= kStunMessageIntegritySize);

  // Verifying the size of the message.
  if ((data.size() % 4) != 0 || data.size() < kStunHeaderSize) {
    return false;
  }

  // Getting the message length from the STUN header.
  uint16_t msg_length = GetBE16(data.subspan(2, 2));
  if (data.size() != (msg_length + kStunHeaderSize)) {
    return false;
  }

  // Finding Message Integrity attribute in stun message.
  size_t current_pos = kStunHeaderSize;
  bool has_message_integrity_attr = false;
  while (current_pos + 4 <= data.size()) {
    uint16_t attr_type, attr_length;
    // Getting attribute type and length.
    attr_type = GetBE16(data.subspan(current_pos, 2));
    attr_length = GetBE16(data.subspan(current_pos + sizeof(attr_type), 2));

    // If M-I, sanity check it, and break out.
    if (attr_type == mi_attr_type) {
      if (attr_length != mi_attr_size ||
          current_pos + sizeof(attr_type) + sizeof(attr_length) + attr_length >
              data.size()) {
        return false;
      }
      has_message_integrity_attr = true;
      break;
    }

    // Otherwise, skip to the next attribute.
    current_pos += sizeof(attr_type) + sizeof(attr_length) + attr_length;
    if ((attr_length % 4) != 0) {
      current_pos += (4 - (attr_length % 4));
    }
  }

  if (!has_message_integrity_attr) {
    return false;
  }

  // Getting length of the message to calculate Message Integrity.
  size_t mi_pos = current_pos;
  std::unique_ptr<uint8_t[]> temp_data(new uint8_t[current_pos]);
  memcpy(temp_data.get(), data.data(), current_pos);
  if (data.size() > mi_pos + kStunAttributeHeaderSize + mi_attr_size) {
    // Stun message has other attributes after message integrity.
    // Adjust the length parameter in stun message to calculate HMAC.
    size_t extra_offset =
        data.size() - (mi_pos + kStunAttributeHeaderSize + mi_attr_size);
    size_t new_adjusted_len = data.size() - extra_offset - kStunHeaderSize;

    // Writing new length of the STUN message @ Message Length in temp buffer.
    //      0                   1                   2                   3
    //      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //     |0 0|     STUN Message Type     |         Message Length        |
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    SetBE16(std::span<uint8_t>(temp_data.get() + 2, 2),
            static_cast<uint16_t>(new_adjusted_len));
  }

  std::array<uint8_t, kStunMessageIntegritySize> hmac;
  size_t ret = ComputeHmac(DIGEST_SHA_1, AsUint8Span(password),
                           std::span(temp_data.get(), mi_pos), hmac);
  if (ret != hmac.size()) {
    RTC_DCHECK_NOTREACHED() << "hmac return != hmac.size()";
    return false;
  }

  // Comparing the calculated HMAC with the one present in the message.
  return std::ranges::equal(
      data.subspan(current_pos + kStunAttributeHeaderSize, mi_attr_size),
      std::span(hmac).first(mi_attr_size));
}

bool StunMessage::AddMessageIntegrity(absl::string_view password) {
  return AddMessageIntegrityOfType(STUN_ATTR_MESSAGE_INTEGRITY,
                                   kStunMessageIntegritySize, password);
}

bool StunMessage::AddMessageIntegrity32(absl::string_view password) {
  return AddMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32,
                                   kStunMessageIntegrity32Size, password);
}

bool StunMessage::AddMessageIntegrityOfType(int attr_type,
                                            size_t attr_size,
                                            absl::string_view key) {
  // Add the attribute with a dummy value. Since this is a known attribute, it
  // can't fail.
  RTC_DCHECK(attr_size <= kStunMessageIntegritySize);
  auto msg_integrity_attr_ptr = std::make_unique<StunByteStringAttribute>(
      attr_type, std::string(attr_size, '0'));
  auto* msg_integrity_attr = msg_integrity_attr_ptr.get();
  AddAttribute(std::move(msg_integrity_attr_ptr));

  // Calculate the HMAC for the message.
  ByteBufferWriter buf;
  if (!Write(&buf))
    return false;

  int msg_len_for_hmac = static_cast<int>(
      buf.Length() - kStunAttributeHeaderSize - msg_integrity_attr->length());
  std::array<uint8_t, kStunMessageIntegritySize> hmac;
  size_t ret = ComputeHmac(DIGEST_SHA_1, AsUint8Span(key),
                           std::span(buf.Data(), msg_len_for_hmac), hmac);
  if (ret != hmac.size()) {
    RTC_DCHECK_NOTREACHED();
    RTC_LOG(LS_ERROR) << "HMAC computation failed. Message-Integrity "
                         "has dummy value.";
    return false;
  }

  // Insert correct HMAC into the attribute.
  msg_integrity_attr->CopyBytes(std::span(hmac).first(attr_size));
  password_ = std::string(key);
  integrity_ = IntegrityStatus::kIntegrityOk;
  return true;
}

// Verifies a message is in fact a STUN message, by performing the checks
// outlined in RFC 5389, section 7.3, including the FINGERPRINT check detailed
// in section 15.5.
bool StunMessage::ValidateFingerprint(std::span<const uint8_t> data) {
  // Check the message length.
  size_t fingerprint_attr_size =
      kStunAttributeHeaderSize + StunUInt32Attribute::SIZE;
  if (data.size() % 4 != 0 ||
      data.size() < kStunHeaderSize + fingerprint_attr_size)
    return false;

  // Skip the rest if the magic cookie isn't present.
  size_t magic_cookie_offset =
      kStunTransactionIdOffset - kStunMagicCookieLength;
  if (GetBE32(data.subspan(magic_cookie_offset, 4)) != kStunMagicCookie)
    return false;

  // Check the fingerprint type and length.
  size_t fingerprint_attr_offset = data.size() - fingerprint_attr_size;
  if (GetBE16(data.subspan(fingerprint_attr_offset, 2)) !=
          STUN_ATTR_FINGERPRINT ||
      GetBE16(data.subspan(fingerprint_attr_offset + sizeof(uint16_t), 2)) !=
          StunUInt32Attribute::SIZE)
    return false;

  // Check the fingerprint value.
  uint32_t fingerprint = GetBE32(
      data.subspan(fingerprint_attr_offset + kStunAttributeHeaderSize, 4));
  return ((fingerprint ^ STUN_FINGERPRINT_XOR_VALUE) ==
          ComputeCrc32(data.first(data.size() - fingerprint_attr_size)));
}

// static
std::string StunMessage::GenerateTransactionId() {
  return CreateRandomString(kStunTransactionIdLength);
}

bool StunMessage::IsStunMethod(std::span<int> methods,
                               std::span<const uint8_t> data) {
  // Check the message length.
  if (data.size() % 4 != 0 || data.size() < kStunHeaderSize)
    return false;

  // Skip the rest if the magic cookie isn't present.
  size_t magic_cookie_offset =
      kStunTransactionIdOffset - kStunMagicCookieLength;
  if (GetBE32(data.subspan(magic_cookie_offset, 4)) != kStunMagicCookie)
    return false;

  int method = GetBE16(data);
  for (int m : methods) {
    if (m == method) {
      return true;
    }
  }
  return false;
}

bool StunMessage::AddFingerprint() {
  // Add the attribute with a dummy value. Since this is a known attribute,
  // it can't fail.
  auto fingerprint_attr_ptr =
      std::make_unique<StunUInt32Attribute>(STUN_ATTR_FINGERPRINT, 0);
  auto* fingerprint_attr = fingerprint_attr_ptr.get();
  AddAttribute(std::move(fingerprint_attr_ptr));

  // Calculate the CRC-32 for the message and insert it.
  ByteBufferWriter buf;
  if (!Write(&buf))
    return false;

  int msg_len_for_crc32 = static_cast<int>(
      buf.Length() - kStunAttributeHeaderSize - fingerprint_attr->length());
  uint32_t c = ComputeCrc32(buf.Data(), msg_len_for_crc32);

  // Insert the correct CRC-32, XORed with a constant, into the attribute.
  fingerprint_attr->SetValue(c ^ STUN_FINGERPRINT_XOR_VALUE);
  return true;
}

bool StunMessage::Read(ByteBufferReader* buf) {
  // Keep a copy of the buffer data around for later verification.
  buffer_.assign(buf->DataView().begin(), buf->DataView().end());

  if (!buf->ReadUInt16(&type_)) {
    return false;
  }

  if (type_ & 0x8000) {
    // RTP and RTCP set the MSB of first byte, since first two bits are version,
    // and version is always 2 (10). If set, this is not a STUN packet.
    return false;
  }

  if (!buf->ReadUInt16(&length_)) {
    return false;
  }

  absl::string_view magic_cookie;
  if (!buf->ReadStringView(&magic_cookie, kStunMagicCookieLength)) {
    return false;
  }

  std::string transaction_id;
  if (!buf->ReadString(&transaction_id, kStunTransactionIdLength)) {
    return false;
  }

  uint32_t magic_cookie_int;
  static_assert(sizeof(magic_cookie_int) == kStunMagicCookieLength,
                "Integer size mismatch: magic_cookie_int and kStunMagicCookie");
  std::memcpy(&magic_cookie_int, magic_cookie.data(), sizeof(magic_cookie_int));
  if (NetworkToHost32(magic_cookie_int) != kStunMagicCookie) {
    // If magic cookie is invalid it means that the peer implements
    // RFC3489 instead of RFC5389.
    transaction_id.insert(0, magic_cookie);
  }
  RTC_DCHECK(IsValidTransactionId(transaction_id));
  transaction_id_ = transaction_id;
  reduced_transaction_id_ = ReduceTransactionId(transaction_id_);

  if (length_ != buf->Length()) {
    return false;
  }

  attrs_.resize(0);

  size_t rest = buf->Length() - length_;
  bool seen_message_integrity = false;
  while (buf->Length() > rest) {
    uint16_t attr_type, attr_length;
    if (!buf->ReadUInt16(&attr_type))
      return false;
    if (!buf->ReadUInt16(&attr_length))
      return false;

    // Per RFC 5389 Section 15.4, with the exception of the FINGERPRINT
    // attribute, which appears after MESSAGE-INTEGRITY, agents MUST ignore
    // all other attributes that follow MESSAGE-INTEGRITY.
    std::unique_ptr<StunAttribute> attr;
    if (!seen_message_integrity || attr_type == STUN_ATTR_FINGERPRINT) {
      attr.reset(CreateAttribute(attr_type, attr_length));
    }
    if (!attr) {
      // Skip any unknown, malformed, or post-MESSAGE-INTEGRITY attributes.
      if ((attr_length % 4) != 0) {
        attr_length += (4 - (attr_length % 4));
      }
      if (!buf->Consume(attr_length)) {
        return false;
      }
    } else {
      if (!attr->Read(buf)) {
        return false;
      }
      if (attr_type == STUN_ATTR_MESSAGE_INTEGRITY ||
          attr_type == STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32) {
        seen_message_integrity = true;
      }
      attrs_.push_back(std::move(attr));
    }
  }

  RTC_DCHECK(buf->Length() == rest);
  return true;
}

bool StunMessage::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt16(type_);
  buf->WriteUInt16(length_);
  if (!IsLegacy())
    buf->WriteUInt32(stun_magic_cookie_);
  buf->WriteString(transaction_id_);

  for (const auto& attr : attrs_) {
    buf->WriteUInt16(attr->type());
    buf->WriteUInt16(static_cast<uint16_t>(attr->length()));
    if (!attr->Write(buf)) {
      return false;
    }
  }

  return true;
}

StunMessage* StunMessage::CreateNew() const {
  return new StunMessage();
}

void StunMessage::SetStunMagicCookie(uint32_t val) {
  stun_magic_cookie_ = val;
}

void StunMessage::SetTransactionIdForTesting(absl::string_view transaction_id) {
  RTC_DCHECK(IsValidTransactionId(transaction_id));
  transaction_id_ = std::string(transaction_id);
  reduced_transaction_id_ = ReduceTransactionId(transaction_id_);
}

StunAttributeValueType StunMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_MAPPED_ADDRESS:
      return STUN_VALUE_ADDRESS;
    case STUN_ATTR_USERNAME:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_MESSAGE_INTEGRITY:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ERROR_CODE:
      return STUN_VALUE_ERROR_CODE;
    case STUN_ATTR_UNKNOWN_ATTRIBUTES:
      return STUN_VALUE_UINT16_LIST;
    case STUN_ATTR_REALM:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_NONCE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_XOR_MAPPED_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_SOFTWARE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ALTERNATE_SERVER:
      return STUN_VALUE_ADDRESS;
    case STUN_ATTR_FINGERPRINT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_RETRANSMIT_COUNT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_GOOG_MISC_INFO:
      return STUN_VALUE_UINT16_LIST;
    case STUN_ATTR_GOOG_DELTA:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_GOOG_DELTA_ACK:
      return STUN_VALUE_UINT64;
    default:
      return STUN_VALUE_UNKNOWN;
  }
}

StunAttribute* StunMessage::CreateAttribute(int type, size_t length) /*const*/ {
  StunAttributeValueType value_type = GetAttributeValueType(type);
  if (value_type != STUN_VALUE_UNKNOWN) {
    return StunAttribute::Create(value_type, type,
                                 static_cast<uint16_t>(length), this);
  } else if (DesignatedExpertRange(type)) {
    // Read unknown attributes as STUN_VALUE_BYTE_STRING
    return StunAttribute::Create(STUN_VALUE_BYTE_STRING, type,
                                 static_cast<uint16_t>(length), this);
  } else {
    return nullptr;
  }
}

const StunAttribute* StunMessage::GetAttribute(int type) const {
  for (const auto& attr : attrs_) {
    if (attr->type() == type) {
      return attr.get();
    }
  }
  return nullptr;
}

bool StunMessage::IsValidTransactionId(absl::string_view transaction_id) {
  return transaction_id.size() == kStunTransactionIdLength ||
         transaction_id.size() == kStunLegacyTransactionIdLength;
}

bool StunMessage::EqualAttributes(
    const StunMessage* other,
    std::function<bool(int type)> attribute_type_mask) const {
  RTC_DCHECK(other != nullptr);
  ByteBufferWriter tmp_buffer_ptr1;
  ByteBufferWriter tmp_buffer_ptr2;
  for (const auto& attr : attrs_) {
    if (attribute_type_mask(attr->type())) {
      const StunAttribute* other_attr = other->GetAttribute(attr->type());
      if (other_attr == nullptr) {
        return false;
      }
      tmp_buffer_ptr1.Clear();
      tmp_buffer_ptr2.Clear();
      attr->Write(&tmp_buffer_ptr1);
      other_attr->Write(&tmp_buffer_ptr2);
      if (tmp_buffer_ptr1.Length() != tmp_buffer_ptr2.Length()) {
        return false;
      }
      if (memcmp(tmp_buffer_ptr1.Data(), tmp_buffer_ptr2.Data(),
                 tmp_buffer_ptr1.Length()) != 0) {
        return false;
      }
    }
  }

  for (const auto& attr : other->attrs_) {
    if (attribute_type_mask(attr->type())) {
      const StunAttribute* own_attr = GetAttribute(attr->type());
      if (own_attr == nullptr) {
        return false;
      }
      // we have already compared all values...
    }
  }
  return true;
}

// StunAttribute

StunAttribute::StunAttribute(uint16_t type, uint16_t length)
    : type_(type), length_(length) {}

void StunAttribute::ConsumePadding(ByteBufferReader* buf) const {
  int remainder = length_ % 4;
  if (remainder > 0) {
    buf->Consume(4 - remainder);
  }
}

void StunAttribute::WritePadding(ByteBufferWriter* buf) const {
  int remainder = length_ % 4;
  if (remainder > 0) {
    uint8_t zeroes[4] = {0};
    buf->Write(std::span<const uint8_t>(zeroes, 4 - remainder));
  }
}

StunAttribute* StunAttribute::Create(StunAttributeValueType value_type,
                                     uint16_t type,
                                     uint16_t length,
                                     StunMessage* owner) {
  switch (value_type) {
    case STUN_VALUE_ADDRESS:
      return new StunAddressAttribute(type, length);
    case STUN_VALUE_XOR_ADDRESS:
      return new StunXorAddressAttribute(type, length, owner);
    case STUN_VALUE_UINT32:
      return new StunUInt32Attribute(type);
    case STUN_VALUE_UINT64:
      return new StunUInt64Attribute(type);
    case STUN_VALUE_BYTE_STRING:
      return new StunByteStringAttribute(type, length);
    case STUN_VALUE_ERROR_CODE:
      return new StunErrorCodeAttribute(type, length);
    case STUN_VALUE_UINT16_LIST:
      return new StunUInt16ListAttribute(type, length);
    default:
      return nullptr;
  }
}

std::unique_ptr<StunAddressAttribute> StunAttribute::CreateAddress(
    uint16_t type) {
  return std::make_unique<StunAddressAttribute>(type, 0);
}

std::unique_ptr<StunXorAddressAttribute> StunAttribute::CreateXorAddress(
    uint16_t type) {
  return std::make_unique<StunXorAddressAttribute>(type, 0, nullptr);
}

std::unique_ptr<StunUInt64Attribute> StunAttribute::CreateUInt64(
    uint16_t type) {
  return std::make_unique<StunUInt64Attribute>(type);
}

std::unique_ptr<StunUInt32Attribute> StunAttribute::CreateUInt32(
    uint16_t type) {
  return std::make_unique<StunUInt32Attribute>(type);
}

std::unique_ptr<StunByteStringAttribute> StunAttribute::CreateByteString(
    uint16_t type) {
  return std::make_unique<StunByteStringAttribute>(type, 0);
}

std::unique_ptr<StunErrorCodeAttribute> StunAttribute::CreateErrorCode() {
  return std::make_unique<StunErrorCodeAttribute>(
      STUN_ATTR_ERROR_CODE, StunErrorCodeAttribute::MIN_SIZE);
}

std::unique_ptr<StunUInt16ListAttribute>
StunAttribute::CreateUInt16ListAttribute(uint16_t type) {
  return std::make_unique<StunUInt16ListAttribute>(type, 0);
}

std::unique_ptr<StunUInt16ListAttribute>
StunAttribute::CreateUnknownAttributes() {
  return std::make_unique<StunUInt16ListAttribute>(STUN_ATTR_UNKNOWN_ATTRIBUTES,
                                                   0);
}

StunAddressAttribute::StunAddressAttribute(uint16_t type,
                                           const SocketAddress& addr)
    : StunAttribute(type, 0) {
  SetAddress(addr);
}

StunAddressAttribute::StunAddressAttribute(uint16_t type, uint16_t length)
    : StunAttribute(type, length) {}

StunAttributeValueType StunAddressAttribute::value_type() const {
  return STUN_VALUE_ADDRESS;
}

bool StunAddressAttribute::Read(ByteBufferReader* buf) {
  uint8_t dummy;
  if (!buf->ReadUInt8(&dummy))
    return false;

  uint8_t stun_family;
  if (!buf->ReadUInt8(&stun_family)) {
    return false;
  }
  uint16_t port;
  if (!buf->ReadUInt16(&port))
    return false;
  if (stun_family == STUN_ADDRESS_IPV4) {
    in_addr v4addr;
    if (length() != SIZE_IP4) {
      return false;
    }
    if (!buf->ReadBytes(
            std::span(reinterpret_cast<uint8_t*>(&v4addr), sizeof(v4addr)))) {
      return false;
    }
    IPAddress ipaddr(v4addr);
    SetAddress(SocketAddress(ipaddr, port));
  } else if (stun_family == STUN_ADDRESS_IPV6) {
    in6_addr v6addr;
    if (length() != SIZE_IP6) {
      return false;
    }
    if (!buf->ReadBytes(
            std::span(reinterpret_cast<uint8_t*>(&v6addr), sizeof(v6addr)))) {
      return false;
    }
    IPAddress ipaddr(v6addr);
    SetAddress(SocketAddress(ipaddr, port));
  } else {
    return false;
  }
  return true;
}

bool StunAddressAttribute::Write(ByteBufferWriter* buf) const {
  StunAddressFamily address_family = family();
  if (address_family == STUN_ADDRESS_UNDEF) {
    RTC_LOG(LS_ERROR) << "Error writing address attribute: unknown family.";
    return false;
  }
  buf->WriteUInt8(0);
  buf->WriteUInt8(address_family);
  buf->WriteUInt16(address_.port());
  switch (address_.family()) {
    case AF_INET: {
      in_addr v4addr = address_.ipaddr().ipv4_address();
      buf->Write(std::span<const uint8_t>(reinterpret_cast<uint8_t*>(&v4addr),
                                          sizeof(v4addr)));
      break;
    }
    case AF_INET6: {
      in6_addr v6addr = address_.ipaddr().ipv6_address();
      buf->Write(std::span<const uint8_t>(reinterpret_cast<uint8_t*>(&v6addr),
                                          sizeof(v6addr)));
      break;
    }
  }
  return true;
}

StunXorAddressAttribute::StunXorAddressAttribute(uint16_t type,
                                                 const SocketAddress& addr)
    : StunAddressAttribute(type, addr), owner_(nullptr) {}

StunXorAddressAttribute::StunXorAddressAttribute(uint16_t type,
                                                 uint16_t length,
                                                 StunMessage* owner)
    : StunAddressAttribute(type, length), owner_(owner) {}

StunAttributeValueType StunXorAddressAttribute::value_type() const {
  return STUN_VALUE_XOR_ADDRESS;
}

void StunXorAddressAttribute::SetOwner(StunMessage* owner) {
  owner_ = owner;
}

IPAddress StunXorAddressAttribute::GetXoredIP() const {
  if (owner_) {
    IPAddress ip = ipaddr();
    switch (ip.family()) {
      case AF_INET: {
        in_addr v4addr = ip.ipv4_address();
        v4addr.s_addr = (v4addr.s_addr ^ HostToNetwork32(kStunMagicCookie));
        return IPAddress(v4addr);
      }
      case AF_INET6: {
        in6_addr v6addr = ip.ipv6_address();
        const std::string& transaction_id = owner_->transaction_id();
        if (transaction_id.length() == kStunTransactionIdLength) {
          uint32_t transactionid_as_ints[3];
          memcpy(&transactionid_as_ints[0], transaction_id.c_str(),
                 transaction_id.length());
          uint32_t* ip_as_ints = reinterpret_cast<uint32_t*>(&v6addr.s6_addr);
          // Transaction ID is in network byte order, but magic cookie
          // is stored in host byte order.
          ip_as_ints[0] = (ip_as_ints[0] ^ HostToNetwork32(kStunMagicCookie));
          ip_as_ints[1] = (ip_as_ints[1] ^ transactionid_as_ints[0]);
          ip_as_ints[2] = (ip_as_ints[2] ^ transactionid_as_ints[1]);
          ip_as_ints[3] = (ip_as_ints[3] ^ transactionid_as_ints[2]);
          return IPAddress(v6addr);
        }
        break;
      }
    }
  }
  // Invalid ip family or transaction ID, or missing owner.
  // Return an AF_UNSPEC address.
  return IPAddress();
}

bool StunXorAddressAttribute::Read(ByteBufferReader* buf) {
  if (!StunAddressAttribute::Read(buf))
    return false;
  uint16_t xoredport = port() ^ (kStunMagicCookie >> 16);
  IPAddress xored_ip = GetXoredIP();
  SetAddress(SocketAddress(xored_ip, xoredport));
  return true;
}

bool StunXorAddressAttribute::Write(ByteBufferWriter* buf) const {
  StunAddressFamily address_family = family();
  if (address_family == STUN_ADDRESS_UNDEF) {
    RTC_LOG(LS_ERROR) << "Error writing xor-address attribute: unknown family.";
    return false;
  }
  IPAddress xored_ip = GetXoredIP();
  if (xored_ip.family() == AF_UNSPEC) {
    return false;
  }
  buf->WriteUInt8(0);
  buf->WriteUInt8(family());
  buf->WriteUInt16(port() ^ (kStunMagicCookie >> 16));
  switch (xored_ip.family()) {
    case AF_INET: {
      in_addr v4addr = xored_ip.ipv4_address();
      buf->Write(std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(&v4addr), sizeof(v4addr)));
      break;
    }
    case AF_INET6: {
      in6_addr v6addr = xored_ip.ipv6_address();
      buf->Write(std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(&v6addr), sizeof(v6addr)));
      break;
    }
  }
  return true;
}

StunUInt32Attribute::StunUInt32Attribute(uint16_t type, uint32_t value)
    : StunAttribute(type, SIZE), bits_(value) {}

StunUInt32Attribute::StunUInt32Attribute(uint16_t type)
    : StunAttribute(type, SIZE), bits_(0) {}

StunAttributeValueType StunUInt32Attribute::value_type() const {
  return STUN_VALUE_UINT32;
}

bool StunUInt32Attribute::GetBit(size_t index) const {
  RTC_DCHECK(index < 32);
  return static_cast<bool>((bits_ >> index) & 0x1);
}

void StunUInt32Attribute::SetBit(size_t index, bool value) {
  RTC_DCHECK(index < 32);
  bits_ &= ~(1 << index);
  bits_ |= value ? (1 << index) : 0;
}

bool StunUInt32Attribute::Read(ByteBufferReader* buf) {
  if (length() != SIZE || !buf->ReadUInt32(&bits_))
    return false;
  return true;
}

bool StunUInt32Attribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt32(bits_);
  return true;
}

StunUInt64Attribute::StunUInt64Attribute(uint16_t type, uint64_t value)
    : StunAttribute(type, SIZE), bits_(value) {}

StunUInt64Attribute::StunUInt64Attribute(uint16_t type)
    : StunAttribute(type, SIZE), bits_(0) {}

StunAttributeValueType StunUInt64Attribute::value_type() const {
  return STUN_VALUE_UINT64;
}

bool StunUInt64Attribute::Read(ByteBufferReader* buf) {
  if (length() != SIZE || !buf->ReadUInt64(&bits_))
    return false;
  return true;
}

bool StunUInt64Attribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt64(bits_);
  return true;
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type)
    : StunAttribute(type, 0), bytes_(nullptr) {}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type,
                                                 absl::string_view str)
    : StunAttribute(type, 0), bytes_(nullptr) {
  CopyBytes(str);
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type,
                                                 std::span<const uint8_t> bytes)
    : StunAttribute(type, 0), bytes_(nullptr) {
  CopyBytes(bytes);
}

StunByteStringAttribute::StunByteStringAttribute(
    uint16_t type,
    std::span<const uint32_t> values)
    : StunAttribute(type, 0), bytes_(nullptr) {
  ByteBufferWriter writer;
  for (uint32_t value : values) {
    writer.WriteUInt32(value);
  }
  CopyBytes(writer.DataView());
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type, uint16_t length)
    : StunAttribute(type, length), bytes_(nullptr) {}

StunByteStringAttribute::~StunByteStringAttribute() {
  delete[] bytes_;
}

StunAttributeValueType StunByteStringAttribute::value_type() const {
  return STUN_VALUE_BYTE_STRING;
}

std::optional<std::vector<uint32_t>> StunByteStringAttribute::GetUInt32Vector()
    const {
  if (length() % 4 != 0) {
    return std::nullopt;
  }
  std::vector<uint32_t> values;
  ByteBufferReader reader(array_view());
  uint32_t value;
  while (reader.ReadUInt32(&value)) {
    values.push_back(value);
  }
  return values;
}

void StunByteStringAttribute::CopyBytes(absl::string_view bytes) {
  CopyBytes(AsUint8Span(bytes));
}

void StunByteStringAttribute::CopyBytes(std::span<const uint8_t> bytes) {
  uint8_t* new_bytes = new uint8_t[bytes.size()];
  memcpy(new_bytes, bytes.data(), bytes.size());
  SetBytes(new_bytes, bytes.size());
}

uint8_t StunByteStringAttribute::GetByte(size_t index) const {
  RTC_DCHECK(bytes_ != nullptr);
  RTC_DCHECK(index < length());
  return bytes_[index];
}

void StunByteStringAttribute::SetByte(size_t index, uint8_t value) {
  RTC_DCHECK(bytes_ != nullptr);
  RTC_DCHECK(index < length());
  bytes_[index] = value;
}

bool StunByteStringAttribute::Read(ByteBufferReader* buf) {
  bytes_ = new uint8_t[length()];
  if (!buf->ReadBytes(std::span<uint8_t>(bytes_, length()))) {
    return false;
  }

  ConsumePadding(buf);
  return true;
}

bool StunByteStringAttribute::Write(ByteBufferWriter* buf) const {
  // Check that length is legal according to specs
  if (!LengthValid(type(), length())) {
    return false;
  }
  buf->Write(std::span<const uint8_t>(bytes_, length()));
  WritePadding(buf);
  return true;
}

void StunByteStringAttribute::SetBytes(uint8_t* bytes, size_t length) {
  delete[] bytes_;
  bytes_ = bytes;
  SetLength(static_cast<uint16_t>(length));
}

const uint16_t StunErrorCodeAttribute::MIN_SIZE = 4;

StunErrorCodeAttribute::StunErrorCodeAttribute(uint16_t type,
                                               int code,
                                               const std::string& reason)
    : StunAttribute(type, 0) {
  SetCode(code);
  SetReason(reason);
}

StunErrorCodeAttribute::StunErrorCodeAttribute(uint16_t type, uint16_t length)
    : StunAttribute(type, length), class_(0), number_(0) {}

StunErrorCodeAttribute::~StunErrorCodeAttribute() {}

StunAttributeValueType StunErrorCodeAttribute::value_type() const {
  return STUN_VALUE_ERROR_CODE;
}

int StunErrorCodeAttribute::code() const {
  return class_ * 100 + number_;
}

void StunErrorCodeAttribute::SetCode(int code) {
  class_ = static_cast<uint8_t>(code / 100);
  number_ = static_cast<uint8_t>(code % 100);
}

void StunErrorCodeAttribute::SetReason(const std::string& reason) {
  SetLength(MIN_SIZE + static_cast<uint16_t>(reason.size()));
  reason_ = reason;
}

bool StunErrorCodeAttribute::Read(ByteBufferReader* buf) {
  uint32_t val;
  if (length() < MIN_SIZE || !buf->ReadUInt32(&val))
    return false;

  if ((val >> 11) != 0)
    RTC_LOG(LS_ERROR) << "error-code bits not zero";

  class_ = ((val >> 8) & 0x7);
  number_ = (val & 0xff);

  if (!buf->ReadString(&reason_, length() - 4))
    return false;

  ConsumePadding(buf);
  return true;
}

bool StunErrorCodeAttribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt32(class_ << 8 | number_);
  buf->WriteString(reason_);
  WritePadding(buf);
  return true;
}

StunUInt16ListAttribute::StunUInt16ListAttribute(uint16_t type, uint16_t length)
    : StunAttribute(type, length) {
  attr_types_ = new std::vector<uint16_t>();
}

StunUInt16ListAttribute::~StunUInt16ListAttribute() {
  delete attr_types_;
}

StunAttributeValueType StunUInt16ListAttribute::value_type() const {
  return STUN_VALUE_UINT16_LIST;
}

size_t StunUInt16ListAttribute::Size() const {
  return attr_types_->size();
}

uint16_t StunUInt16ListAttribute::GetType(int index) const {
  return (*attr_types_)[index];
}

void StunUInt16ListAttribute::SetType(int index, uint16_t value) {
  (*attr_types_)[index] = value;
}

void StunUInt16ListAttribute::AddType(uint16_t value) {
  attr_types_->push_back(value);
  SetLength(static_cast<uint16_t>(attr_types_->size() * 2));
}

void StunUInt16ListAttribute::AddTypeAtIndex(uint16_t index, uint16_t value) {
  if (attr_types_->size() < static_cast<size_t>(index + 1)) {
    attr_types_->resize(index + 1);
  }
  (*attr_types_)[index] = value;
  SetLength(static_cast<uint16_t>(attr_types_->size() * 2));
}

bool StunUInt16ListAttribute::Read(ByteBufferReader* buf) {
  if (length() % 2) {
    return false;
  }

  for (size_t i = 0; i < length() / 2; i++) {
    uint16_t attr;
    if (!buf->ReadUInt16(&attr))
      return false;
    attr_types_->push_back(attr);
  }
  // Padding of these attributes is done in RFC 5389 style. This is
  // slightly different from RFC3489, but it shouldn't be important.
  // RFC3489 pads out to a 32 bit boundary by duplicating one of the
  // entries in the list (not necessarily the last one - it's unspecified).
  // RFC5389 pads on the end, and the bytes are always ignored.
  ConsumePadding(buf);
  return true;
}

bool StunUInt16ListAttribute::Write(ByteBufferWriter* buf) const {
  for (size_t i = 0; i < attr_types_->size(); ++i) {
    buf->WriteUInt16((*attr_types_)[i]);
  }
  WritePadding(buf);
  return true;
}

std::string StunMethodToString(int msg_type) {
  switch (msg_type) {
    case STUN_BINDING_REQUEST:
      return "STUN BINDING request";
    case STUN_BINDING_INDICATION:
      return "STUN BINDING indication";
    case STUN_BINDING_RESPONSE:
      return "STUN BINDING response";
    case STUN_BINDING_ERROR_RESPONSE:
      return "STUN BINDING error response";
    case GOOG_PING_REQUEST:
      return "GOOG PING request";
    case GOOG_PING_RESPONSE:
      return "GOOG PING response";
    case GOOG_PING_ERROR_RESPONSE:
      return "GOOG PING error response";
    case STUN_ALLOCATE_REQUEST:
      return "TURN ALLOCATE request";
    case STUN_ALLOCATE_RESPONSE:
      return "TURN ALLOCATE response";
    case STUN_ALLOCATE_ERROR_RESPONSE:
      return "TURN ALLOCATE error response";
    case TURN_REFRESH_REQUEST:
      return "TURN REFRESH request";
    case TURN_REFRESH_RESPONSE:
      return "TURN REFRESH response";
    case TURN_REFRESH_ERROR_RESPONSE:
      return "TURN REFRESH error response";
    case TURN_SEND_INDICATION:
      return "TURN SEND INDICATION";
    case TURN_DATA_INDICATION:
      return "TURN DATA INDICATION";
    case TURN_CREATE_PERMISSION_REQUEST:
      return "TURN CREATE PERMISSION request";
    case TURN_CREATE_PERMISSION_RESPONSE:
      return "TURN CREATE PERMISSION response";
    case TURN_CREATE_PERMISSION_ERROR_RESPONSE:
      return "TURN CREATE PERMISSION error response";
    case TURN_CHANNEL_BIND_REQUEST:
      return "TURN CHANNEL BIND request";
    case TURN_CHANNEL_BIND_RESPONSE:
      return "TURN CHANNEL BIND response";
    case TURN_CHANNEL_BIND_ERROR_RESPONSE:
      return "TURN CHANNEL BIND error response";
    default:
      return "UNKNOWN<" + std::to_string(msg_type) + ">";
  }
}

int GetStunSuccessResponseType(int req_type) {
  return IsStunRequestType(req_type) ? (req_type | 0x100) : -1;
}

int GetStunErrorResponseType(int req_type) {
  return IsStunRequestType(req_type) ? (req_type | 0x110) : -1;
}

bool IsStunRequestType(int msg_type) {
  return ((msg_type & kStunTypeMask) == 0x000);
}

bool IsStunIndicationType(int msg_type) {
  return ((msg_type & kStunTypeMask) == 0x010);
}

bool IsStunSuccessResponseType(int msg_type) {
  return ((msg_type & kStunTypeMask) == 0x100);
}

bool IsStunErrorResponseType(int msg_type) {
  return ((msg_type & kStunTypeMask) == 0x110);
}

bool ComputeStunCredentialHash(const std::string& username,
                               const std::string& realm,
                               const std::string& password,
                               std::string* hash) {
  // http://tools.ietf.org/html/rfc5389#section-15.4
  // long-term credentials will be calculated using the key and key is
  // key = MD5(username ":" realm ":" SASLprep(password))
  std::string input = username;
  input += ':';
  input += realm;
  input += ':';
  input += password;

  std::array<uint8_t, MessageDigest::kMaxSize> digest;
  size_t size = ComputeDigest(DIGEST_MD5, AsUint8Span(input), digest);

  if (size == 0) {
    return false;
  }

  *hash = std::string(AsStringView(std::span(digest).first(size)));
  return true;
}

std::unique_ptr<StunAttribute> CopyStunAttribute(
    const StunAttribute& attribute,
    ByteBufferWriter* tmp_buffer_ptr) {
  ByteBufferWriter tmpBuffer;
  if (tmp_buffer_ptr == nullptr) {
    tmp_buffer_ptr = &tmpBuffer;
  }

  std::unique_ptr<StunAttribute> copy(StunAttribute::Create(
      attribute.value_type(), attribute.type(),
      static_cast<uint16_t>(attribute.length()), nullptr));

  if (!copy) {
    return nullptr;
  }
  tmp_buffer_ptr->Clear();
  if (!attribute.Write(tmp_buffer_ptr)) {
    return nullptr;
  }
  ByteBufferReader reader(*tmp_buffer_ptr);
  if (!copy->Read(&reader)) {
    return nullptr;
  }

  return copy;
}

StunAttributeValueType TurnMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_CHANNEL_NUMBER:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_LIFETIME:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_XOR_PEER_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_DATA:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_XOR_RELAYED_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_EVEN_PORT:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_REQUESTED_TRANSPORT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_DONT_FRAGMENT:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_RESERVATION_TOKEN:
      return STUN_VALUE_BYTE_STRING;
    default:
      return StunMessage::GetAttributeValueType(type);
  }
}

StunMessage* TurnMessage::CreateNew() const {
  return new TurnMessage();
}

StunAttributeValueType IceMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_PRIORITY:
    case STUN_ATTR_GOOG_NETWORK_INFO:
    case STUN_ATTR_NOMINATION:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_USE_CANDIDATE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ICE_CONTROLLED:
      return STUN_VALUE_UINT64;
    case STUN_ATTR_ICE_CONTROLLING:
      return STUN_VALUE_UINT64;
    default:
      return StunMessage::GetAttributeValueType(type);
  }
}

StunMessage* IceMessage::CreateNew() const {
  return new IceMessage();
}

std::unique_ptr<StunMessage> StunMessage::Clone() const {
  std::unique_ptr<StunMessage> copy(CreateNew());
  if (!copy) {
    return nullptr;
  }
  ByteBufferWriter buf;
  if (!Write(&buf)) {
    return nullptr;
  }
  ByteBufferReader reader(buf);
  if (!copy->Read(&reader)) {
    return nullptr;
  }
  return copy;
}

}  // namespace webrtc
