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
