/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/ProtobufReader.h"

using namespace mozilla;

using WireType = ProtobufReader::WireType;

TEST(ProtobufReader, MakeTag)
{
  EXPECT_EQ(ProtobufReader::MakeTag(1, WireType::Varint), 0x08u);
  EXPECT_EQ(ProtobufReader::MakeTag(2, WireType::LengthDelimited), 0x12u);
  EXPECT_EQ(ProtobufReader::MakeTag(3, WireType::Fixed64), 0x19u);
  EXPECT_EQ(ProtobufReader::MakeTag(4, WireType::Fixed32), 0x25u);
  EXPECT_EQ(ProtobufReader::MakeTag(16, WireType::Varint), 0x80u);
}

TEST(ProtobufReader, ReadVarint)
{
  const uint8_t data[] = {0x00, 0x7f, 0xac, 0x02, 0xff, 0xff, 0xff,
                          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
  ProtobufReader reader(data);
  uint64_t value;

  ASSERT_TRUE(reader.ReadVarint(value));
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(reader.Position(), 1u);

  ASSERT_TRUE(reader.ReadVarint(value));
  EXPECT_EQ(value, 0x7fu);
  EXPECT_EQ(reader.Position(), 2u);

  ASSERT_TRUE(reader.ReadVarint(value));
  EXPECT_EQ(value, 300u);
  EXPECT_EQ(reader.Position(), 4u);

  ASSERT_TRUE(reader.ReadVarint(value));
  EXPECT_EQ(value, UINT64_MAX);
  EXPECT_TRUE(reader.AtEnd());

  EXPECT_FALSE(reader.ReadVarint(value));
}

TEST(ProtobufReader, ReadVarintTruncated)
{
  const uint8_t data[] = {0x80, 0x80};
  ProtobufReader reader(data);
  uint64_t value;
  EXPECT_FALSE(reader.ReadVarint(value));
}

TEST(ProtobufReader, ReadVarintTooLong)
{
  const uint8_t data[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                          0x80, 0x80, 0x80, 0x80, 0x01};
  ProtobufReader reader(data);
  uint64_t value;
  EXPECT_FALSE(reader.ReadVarint(value));
}

TEST(ProtobufReader, ReadFields)
{
  const uint8_t data[] = {// Field 1, varint 150.
                          0x08, 0x96, 0x01,
                          // Field 2, length delimited "abc".
                          0x12, 0x03, 'a', 'b', 'c',
                          // Field 3, fixed64.
                          0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          // Field 4, fixed32.
                          0x25, 0x01, 0x02, 0x03, 0x04,
                          // Field 5, empty length delimited.
                          0x2a, 0x00,
                          // Field 16, varint 1.
                          0x80, 0x01, 0x01};
  ProtobufReader reader(data);
  uint32_t field;
  Span<const uint8_t> payload;
  uint64_t value;

  ASSERT_TRUE(reader.ReadField(field, payload, value));
  EXPECT_EQ(field, 1u);
  EXPECT_TRUE(payload.IsEmpty());
  EXPECT_EQ(value, 150u);

  ASSERT_TRUE(reader.ReadField(field, payload, value));
  EXPECT_EQ(field, 2u);
  ASSERT_EQ(payload.Length(), 3u);
  EXPECT_EQ(payload.Elements(), data + 5);
  EXPECT_EQ(value, 0u);

  ASSERT_TRUE(reader.ReadField(field, payload, value));
  EXPECT_EQ(field, 3u);
  EXPECT_TRUE(payload.IsEmpty());
  EXPECT_EQ(value, 0u);

  ASSERT_TRUE(reader.ReadField(field, payload, value));
  EXPECT_EQ(field, 4u);
  EXPECT_TRUE(payload.IsEmpty());
  EXPECT_EQ(value, 0u);

  ASSERT_TRUE(reader.ReadField(field, payload, value));
  EXPECT_EQ(field, 5u);
  EXPECT_TRUE(payload.IsEmpty());

  const size_t start = reader.Position();
  ASSERT_TRUE(reader.ReadField(field, payload));
  EXPECT_EQ(field, 16u);
  EXPECT_EQ(start, sizeof(data) - 3);

  EXPECT_TRUE(reader.AtEnd());
  EXPECT_FALSE(reader.ReadField(field, payload, value));
}

TEST(ProtobufReader, ReadFieldNested)
{
  const uint8_t data[] = {0x12, 0x04, 0x08, 0x05, 0x10, 0x07};
  ProtobufReader outer(data);
  uint32_t field;
  Span<const uint8_t> payload;
  uint64_t value;

  ASSERT_TRUE(outer.ReadField(field, payload));
  EXPECT_EQ(field, 2u);
  EXPECT_TRUE(outer.AtEnd());

  ProtobufReader inner(payload);
  ASSERT_TRUE(inner.ReadField(field, payload, value));
  EXPECT_EQ(field, 1u);
  EXPECT_EQ(value, 5u);
  ASSERT_TRUE(inner.ReadField(field, payload, value));
  EXPECT_EQ(field, 2u);
  EXPECT_EQ(value, 7u);
  EXPECT_TRUE(inner.AtEnd());
}

TEST(ProtobufReader, ReadFieldLengthOverrun)
{
  const uint8_t data[] = {0x12, 0x04, 'a', 'b', 'c'};
  ProtobufReader reader(data);
  uint32_t field;
  Span<const uint8_t> payload;
  EXPECT_FALSE(reader.ReadField(field, payload));
}

TEST(ProtobufReader, ReadFieldHugeLength)
{
  const uint8_t data[] = {0x12, 0xff, 0xff, 0xff, 0xff, 0xff,
                          0xff, 0xff, 0xff, 0xff, 0x01, 'a'};
  ProtobufReader reader(data);
  uint32_t field;
  Span<const uint8_t> payload;
  EXPECT_FALSE(reader.ReadField(field, payload));
}

TEST(ProtobufReader, ReadFieldFixedTruncated)
{
  uint32_t field;
  Span<const uint8_t> payload;

  const uint8_t fixed64[] = {0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  ProtobufReader reader64(fixed64);
  EXPECT_FALSE(reader64.ReadField(field, payload));

  const uint8_t fixed32[] = {0x25, 0x01, 0x02, 0x03};
  ProtobufReader reader32(fixed32);
  EXPECT_FALSE(reader32.ReadField(field, payload));
}

TEST(ProtobufReader, ReadFieldVarintTruncated)
{
  const uint8_t data[] = {0x08, 0x96};
  ProtobufReader reader(data);
  uint32_t field;
  Span<const uint8_t> payload;
  EXPECT_FALSE(reader.ReadField(field, payload));
}

TEST(ProtobufReader, ReadFieldTagTruncated)
{
  const uint8_t data[] = {0x80};
  ProtobufReader reader(data);
  uint32_t field;
  Span<const uint8_t> payload;
  EXPECT_FALSE(reader.ReadField(field, payload));
}

TEST(ProtobufReader, ReadFieldUnsupportedWireType)
{
  uint32_t field;
  Span<const uint8_t> payload;

  // Start group, end group and the unassigned wire types 6 and 7.
  for (uint8_t wireType : {3, 4, 6, 7}) {
    const uint8_t data[] = {static_cast<uint8_t>(0x08 | wireType), 0x00};
    ProtobufReader reader(data);
    EXPECT_FALSE(reader.ReadField(field, payload)) << int(wireType);
  }
}

TEST(ProtobufReader, Empty)
{
  ProtobufReader reader(Span<const uint8_t>{});
  uint32_t field;
  Span<const uint8_t> payload;
  uint64_t value;
  EXPECT_TRUE(reader.AtEnd());
  EXPECT_EQ(reader.Position(), 0u);
  EXPECT_FALSE(reader.ReadVarint(value));
  EXPECT_FALSE(reader.ReadField(field, payload));
}

TEST(Widevine, TrimKeyRequest)
{
  const uint8_t data[] = {// The key request: type 1, msg "ab".
                          0x08, 0x01, 0x12, 0x02, 'a', 'b',
                          // The previous key request: type 1, msg "c".
                          0x08, 0x01, 0x12, 0x01, 'c'};
  auto request = widevine::TrimKeyRequest(data);
  EXPECT_EQ(request.Elements(), data);
  EXPECT_EQ(request.Length(), 6u);
}

TEST(Widevine, TrimKeyRequestSingle)
{
  const uint8_t data[] = {0x08, 0x01, 0x12, 0x02, 'a', 'b', 0x1a, 0x00};
  auto request = widevine::TrimKeyRequest(data);
  EXPECT_EQ(request.Length(), sizeof(data));
}

TEST(Widevine, TrimKeyRequestTypeNotFirst)
{
  const uint8_t data[] = {0x12, 0x01, 'a', 0x08, 0x01,
                          0x12, 0x01, 'b', 0x08, 0x01};
  auto request = widevine::TrimKeyRequest(data);
  EXPECT_EQ(request.Length(), 8u);
}

TEST(Widevine, TrimKeyRequestMalformed)
{
  const uint8_t data[] = {0x08, 0x01, 0x12, 0x10, 'a', 'b', 0x08, 0x01};
  auto request = widevine::TrimKeyRequest(data);
  EXPECT_EQ(request.Length(), sizeof(data));
}

TEST(Widevine, TrimKeyRequestEmpty)
{
  EXPECT_TRUE(widevine::TrimKeyRequest(Span<const uint8_t>{}).IsEmpty());
}

TEST(Widevine, IsServiceCertificateRequest)
{
  const uint8_t request[] = {0x08, 0x04};
  EXPECT_TRUE(widevine::IsServiceCertificateRequest(request));

  const uint8_t licenseRequest[] = {0x08, 0x01};
  EXPECT_FALSE(widevine::IsServiceCertificateRequest(licenseRequest));

  const uint8_t certificate[] = {0x08, 0x05};
  EXPECT_FALSE(widevine::IsServiceCertificateRequest(certificate));

  const uint8_t wrongTag[] = {0x10, 0x04};
  EXPECT_FALSE(widevine::IsServiceCertificateRequest(wrongTag));

  const uint8_t withMsg[] = {0x08, 0x04, 0x12, 0x00};
  EXPECT_FALSE(widevine::IsServiceCertificateRequest(withMsg));

  EXPECT_FALSE(widevine::IsServiceCertificateRequest(Span(request).First(1)));
  EXPECT_FALSE(widevine::IsServiceCertificateRequest(Span<const uint8_t>{}));
}

TEST(Widevine, IsServiceCertificateResponse)
{
  const uint8_t certificate[] = {0x08, 0x05, 0x12, 0x02, 'a', 'b'};
  EXPECT_TRUE(widevine::IsServiceCertificateResponse(certificate));

  const uint8_t typeOnly[] = {0x08, 0x05};
  EXPECT_TRUE(widevine::IsServiceCertificateResponse(typeOnly));

  const uint8_t license[] = {0x08, 0x02, 0x12, 0x00};
  EXPECT_FALSE(widevine::IsServiceCertificateResponse(license));

  const uint8_t request[] = {0x08, 0x04};
  EXPECT_FALSE(widevine::IsServiceCertificateResponse(request));

  const uint8_t wrongTag[] = {0x10, 0x05};
  EXPECT_FALSE(widevine::IsServiceCertificateResponse(wrongTag));

  EXPECT_FALSE(
      widevine::IsServiceCertificateResponse(Span(certificate).First(1)));
  EXPECT_FALSE(widevine::IsServiceCertificateResponse(Span<const uint8_t>{}));
}

namespace {

void AppendVarint(nsTArray<uint8_t>& aOut, uint64_t aValue) {
  while (aValue >= 0x80) {
    aOut.AppendElement(static_cast<uint8_t>(aValue | 0x80));
    aValue >>= 7;
  }
  aOut.AppendElement(static_cast<uint8_t>(aValue));
}

void AppendVarintField(nsTArray<uint8_t>& aOut, uint32_t aField,
                       uint64_t aValue) {
  AppendVarint(aOut, ProtobufReader::MakeTag(aField, WireType::Varint));
  AppendVarint(aOut, aValue);
}

void AppendBytesField(nsTArray<uint8_t>& aOut, uint32_t aField,
                      Span<const uint8_t> aValue) {
  AppendVarint(aOut,
               ProtobufReader::MakeTag(aField, WireType::LengthDelimited));
  AppendVarint(aOut, aValue.Length());
  aOut.AppendElements(aValue);
}

nsTArray<uint8_t> MakeKeyId(uint8_t aSeed) {
  nsTArray<uint8_t> id;
  for (size_t i = 0; i < widevine::kKeyIdSize; ++i) {
    id.AppendElement(static_cast<uint8_t>(aSeed + i));
  }
  return id;
}

nsTArray<uint8_t> MakeKeyContainer(Span<const uint8_t> aId, uint64_t aType) {
  nsTArray<uint8_t> container;
  AppendBytesField(container, widevine::kKeyContainerIdField, aId);
  AppendVarintField(container, widevine::kKeyContainerTypeField, aType);
  return container;
}

nsTArray<uint8_t> MakeLicenseResponse(
    const nsTArray<nsTArray<uint8_t>>& aContainers) {
  nsTArray<uint8_t> license;
  for (const auto& container : aContainers) {
    AppendBytesField(license, widevine::kLicenseKeyField, container);
  }

  // A LICENSE SignedMessage with a signature after the License.
  nsTArray<uint8_t> response;
  AppendVarintField(response, widevine::kSignedMessageTypeField, 2);
  AppendBytesField(response, widevine::kSignedMessageMsgField, license);
  const uint8_t signature[] = {0xaa, 0xbb};
  AppendBytesField(response, 3, signature);
  return response;
}

constexpr uint64_t kKeyTypeSigning = 1;

}  // namespace

TEST(Widevine, ParseLicenseKeyIds)
{
  nsTArray<nsTArray<uint8_t>> containers;
  containers.AppendElement(MakeKeyContainer(MakeKeyId(0x00), kKeyTypeSigning));
  containers.AppendElement(
      MakeKeyContainer(MakeKeyId(0x10), widevine::kKeyTypeContent));
  containers.AppendElement(
      MakeKeyContainer(MakeKeyId(0x20), widevine::kKeyTypeContent));

  auto keyIds = widevine::ParseLicenseKeyIds(MakeLicenseResponse(containers));
  ASSERT_EQ(keyIds.Length(), 2u);
  EXPECT_EQ(keyIds[0], MakeKeyId(0x10));
  EXPECT_EQ(keyIds[1], MakeKeyId(0x20));
}

TEST(Widevine, ParseLicenseKeyIdsFieldOrder)
{
  nsTArray<uint8_t> container;
  AppendVarintField(container, widevine::kKeyContainerTypeField,
                    widevine::kKeyTypeContent);
  AppendBytesField(container, 2, MakeKeyId(0x40));
  AppendBytesField(container, widevine::kKeyContainerIdField, MakeKeyId(0x30));

  nsTArray<nsTArray<uint8_t>> containers;
  containers.AppendElement(std::move(container));

  auto keyIds = widevine::ParseLicenseKeyIds(MakeLicenseResponse(containers));
  ASSERT_EQ(keyIds.Length(), 1u);
  EXPECT_EQ(keyIds[0], MakeKeyId(0x30));
}

TEST(Widevine, ParseLicenseKeyIdsInvalidContainers)
{
  nsTArray<nsTArray<uint8_t>> containers;

  nsTArray<uint8_t> shortId = MakeKeyId(0x00);
  shortId.RemoveLastElement();
  containers.AppendElement(
      MakeKeyContainer(shortId, widevine::kKeyTypeContent));

  nsTArray<uint8_t> noType;
  AppendBytesField(noType, widevine::kKeyContainerIdField, MakeKeyId(0x10));
  containers.AppendElement(std::move(noType));

  nsTArray<uint8_t> noId;
  AppendVarintField(noId, widevine::kKeyContainerTypeField,
                    widevine::kKeyTypeContent);
  containers.AppendElement(std::move(noId));

  nsTArray<uint8_t> truncated =
      MakeKeyContainer(MakeKeyId(0x20), widevine::kKeyTypeContent);
  truncated.RemoveLastElement();
  containers.AppendElement(std::move(truncated));

  containers.AppendElement(
      MakeKeyContainer(MakeKeyId(0x30), widevine::kKeyTypeContent));

  auto keyIds = widevine::ParseLicenseKeyIds(MakeLicenseResponse(containers));
  ASSERT_EQ(keyIds.Length(), 1u);
  EXPECT_EQ(keyIds[0], MakeKeyId(0x30));
}

TEST(Widevine, ParseLicenseKeyIdsLimit)
{
  nsTArray<nsTArray<uint8_t>> containers;
  for (size_t i = 0; i <= widevine::kMaxLicenseKeyIds; ++i) {
    containers.AppendElement(MakeKeyContainer(
        MakeKeyId(static_cast<uint8_t>(i)), widevine::kKeyTypeContent));
  }

  auto keyIds = widevine::ParseLicenseKeyIds(MakeLicenseResponse(containers));
  ASSERT_EQ(keyIds.Length(), widevine::kMaxLicenseKeyIds);
  EXPECT_EQ(keyIds.LastElement(),
            MakeKeyId(static_cast<uint8_t>(widevine::kMaxLicenseKeyIds - 1)));
}

TEST(Widevine, ParseLicenseKeyIdsMalformed)
{
  nsTArray<nsTArray<uint8_t>> containers;
  containers.AppendElement(
      MakeKeyContainer(MakeKeyId(0x10), widevine::kKeyTypeContent));
  nsTArray<uint8_t> response = MakeLicenseResponse(containers);

  // The License length now overruns the response.
  response.TruncateLength(response.Length() - 6);
  EXPECT_TRUE(widevine::ParseLicenseKeyIds(response).IsEmpty());

  EXPECT_TRUE(widevine::ParseLicenseKeyIds(Span<const uint8_t>{}).IsEmpty());

  const uint8_t certificate[] = {0x08, 0x05, 0x12, 0x00};
  EXPECT_TRUE(widevine::ParseLicenseKeyIds(certificate).IsEmpty());
}
