/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/copy_on_write_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::SizeIs;

constexpr uint8_t kTestData[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

}  // namespace

void EnsureBuffersShareData(const CopyOnWriteBuffer& buf1,
                            const CopyOnWriteBuffer& buf2) {
  // Data is shared between buffers.
  EXPECT_EQ(buf1.size(), buf2.size());
  EXPECT_EQ(buf1.capacity(), buf2.capacity());
  const uint8_t* data1 = buf1.data();
  const uint8_t* data2 = buf2.data();
  EXPECT_EQ(data1, data2);
  EXPECT_EQ(buf1, buf2);
}

void EnsureBuffersDontShareData(const CopyOnWriteBuffer& buf1,
                                const CopyOnWriteBuffer& buf2) {
  // Data is not shared between buffers.
  const uint8_t* data1 = buf1.cdata();
  const uint8_t* data2 = buf2.cdata();
  EXPECT_NE(data1, data2);
}

TEST(CopyOnWriteBufferTest, TestCreateEmptyData) {
  CopyOnWriteBuffer buf;
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 0u);
  EXPECT_EQ(buf.data(), nullptr);

  CopyOnWriteBuffer buf_from_span(std::span<const uint8_t>{});
  EXPECT_TRUE(buf_from_span.empty());
  EXPECT_EQ(buf_from_span.size(), 0u);
}

TEST(CopyOnWriteBufferTest, CreateEmptyDataWithCapacity) {
  CopyOnWriteBuffer buf(0, 16);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 16u);
  EXPECT_NE(buf.MutableData(), nullptr);
}

TEST(CopyOnWriteBufferTest, TestMoveConstruct) {
  EXPECT_TRUE(std::is_nothrow_move_constructible_v<CopyOnWriteBuffer>);

  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  size_t buf1_size = buf1.size();
  size_t buf1_capacity = buf1.capacity();
  const uint8_t* buf1_data = buf1.cdata();

  CopyOnWriteBuffer buf2(std::move(buf1));
  EXPECT_TRUE(buf1.empty());
  EXPECT_EQ(buf1.size(), 0u);
  EXPECT_EQ(buf1.capacity(), 0u);
  EXPECT_EQ(buf1.data(), nullptr);
  EXPECT_FALSE(buf2.empty());
  EXPECT_EQ(buf2.size(), buf1_size);
  EXPECT_EQ(buf2.capacity(), buf1_capacity);
  EXPECT_EQ(buf2.data(), buf1_data);
}

TEST(CopyOnWriteBufferTest, TestMoveAssign) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  size_t buf1_size = buf1.size();
  size_t buf1_capacity = buf1.capacity();
  const uint8_t* buf1_data = buf1.cdata();

  CopyOnWriteBuffer buf2;
  buf2 = std::move(buf1);
  EXPECT_EQ(buf1.size(), 0u);
  EXPECT_EQ(buf1.capacity(), 0u);
  EXPECT_EQ(buf1.data(), nullptr);
  EXPECT_EQ(buf2.size(), buf1_size);
  EXPECT_EQ(buf2.capacity(), buf1_capacity);
  EXPECT_EQ(buf2.data(), buf1_data);
}

TEST(CopyOnWriteBufferTest, TestSwap) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  size_t buf1_size = buf1.size();
  size_t buf1_capacity = buf1.capacity();
  const uint8_t* buf1_data = buf1.cdata();

  CopyOnWriteBuffer buf2(std::span(kTestData).first(6), 20);
  size_t buf2_size = buf2.size();
  size_t buf2_capacity = buf2.capacity();
  const uint8_t* buf2_data = buf2.cdata();

  std::swap(buf1, buf2);
  EXPECT_EQ(buf1.size(), buf2_size);
  EXPECT_EQ(buf1.capacity(), buf2_capacity);
  EXPECT_EQ(buf1.data(), buf2_data);
  EXPECT_EQ(buf2.size(), buf1_size);
  EXPECT_EQ(buf2.capacity(), buf1_capacity);
  EXPECT_EQ(buf2.data(), buf1_data);
}

TEST(CopyOnWriteBufferTest, Append) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  EnsureBuffersShareData(buf1, buf2);

  // Append copies the underlying buffer.
  buf2.Append({{5, 6, 7, 8}});
  EXPECT_EQ(buf2.size(), buf1.size() + 4);
  EXPECT_EQ(buf2.capacity(), buf1.capacity());
  EXPECT_NE(buf2.data(), buf1.data());

  EXPECT_THAT(buf1, ElementsAreArray(kTestData, 3));
  EXPECT_THAT(buf2, ElementsAre(0x0, 0x1, 0x2, 5, 6, 7, 8));
}

TEST(CopyOnWriteBufferTest, SetEmptyDataClearsDataButKeepsCapacity) {
  CopyOnWriteBuffer buf(/*size=*/10, /*capacity=*/10);

  buf.Set({});

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 10u);
}

TEST(CopyOnWriteBufferTest, SetNoMoreThanCapacityDoesntCauseReallocation) {
  CopyOnWriteBuffer buf1(/*size=*/3, /*capacity=*/10);
  const uint8_t* const original_allocation = buf1.data();

  buf1.Set(std::span(kTestData).first(10));

  EXPECT_EQ(buf1.data(), original_allocation);
  EXPECT_THAT(buf1, ElementsAreArray(kTestData, 10));
  EXPECT_EQ(buf1.capacity(), 10u);
}

TEST(CopyOnWriteBufferTest, AssignMakeReferenceCopy) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2;

  buf2 = buf1;

  EnsureBuffersShareData(buf1, buf2);
}

TEST(CopyOnWriteBufferTest, SetOnSharedKeepsOriginal) {
  const uint8_t data[] = "foo";
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  const uint8_t* const original_allocation = buf1.cdata();
  CopyOnWriteBuffer buf2(buf1);

  buf2.Set(data);

  EnsureBuffersDontShareData(buf1, buf2);
  EXPECT_EQ(buf1.data(), original_allocation);
  EXPECT_THAT(buf1, ElementsAreArray(kTestData, 3));
  EXPECT_EQ(buf2, CopyOnWriteBuffer(data));
}

TEST(CopyOnWriteBufferTest, SetOnSharedKeepsCapacity) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);
  EnsureBuffersShareData(buf1, buf2);

  buf2.Set(std::span(kTestData).first(2));

  EnsureBuffersDontShareData(buf1, buf2);
  EXPECT_EQ(buf2.size(), 2u);
  EXPECT_EQ(buf2.capacity(), 10u);
}

TEST(CopyOnWriteBufferTest, TestEnsureCapacity) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  // Smaller than existing capacity -> no change and still same contents.
  buf2.EnsureCapacity(8);
  EnsureBuffersShareData(buf1, buf2);
  EXPECT_EQ(buf1.size(), 3u);
  EXPECT_EQ(buf1.capacity(), 10u);
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 10u);

  // Lager than existing capacity -> data is cloned.
  buf2.EnsureCapacity(16);
  EnsureBuffersDontShareData(buf1, buf2);
  EXPECT_EQ(buf1.size(), 3u);
  EXPECT_EQ(buf1.capacity(), 10u);
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 16u);
  // The size and contents are still the same.
  EXPECT_EQ(buf1, buf2);
}

TEST(CopyOnWriteBufferTest, SetSizeDoesntChangeOriginal) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  const uint8_t* const original_allocation = buf1.cdata();
  CopyOnWriteBuffer buf2(buf1);

  buf2.SetSize(16);

  EnsureBuffersDontShareData(buf1, buf2);
  EXPECT_EQ(original_allocation, buf1.cdata());
  EXPECT_EQ(3u, buf1.size());
  EXPECT_EQ(10u, buf1.capacity());
}

TEST(CopyOnWriteBufferTest, SetSizeCloneContent) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  buf2.SetSize(16);

  EXPECT_EQ(buf2.size(), 16u);
  EXPECT_EQ(0, memcmp(buf2.data(), kTestData, 3));
}

TEST(CopyOnWriteBufferTest, SetSizeMayIncreaseCapacity) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(3), 10);

  buf.SetSize(16);

  EXPECT_EQ(16u, buf.size());
  EXPECT_EQ(16u, buf.capacity());
}

TEST(CopyOnWriteBufferTest, SetSizeDoesntDecreaseCapacity) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(5), 10);
  CopyOnWriteBuffer buf2(buf1);

  buf2.SetSize(2);

  EXPECT_EQ(2u, buf2.size());
  EXPECT_EQ(10u, buf2.capacity());
}

TEST(CopyOnWriteBufferTest, ClearDoesntChangeOriginal) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  const uint8_t* const original_allocation = buf1.cdata();
  CopyOnWriteBuffer buf2(buf1);

  buf2.Clear();

  EnsureBuffersDontShareData(buf1, buf2);
  EXPECT_EQ(3u, buf1.size());
  EXPECT_EQ(10u, buf1.capacity());
  EXPECT_EQ(original_allocation, buf1.cdata());
  EXPECT_EQ(0u, buf2.size());
}

TEST(CopyOnWriteBufferTest, ClearDoesntChangeCapacity) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  buf2.Clear();

  EXPECT_EQ(0u, buf2.size());
  EXPECT_EQ(10u, buf2.capacity());
}

TEST(CopyOnWriteBufferTest, DataAccessorDoesntCloneData) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  EXPECT_EQ(buf1.data(), buf2.data());
}

TEST(CopyOnWriteBufferTest, MutableDataClonesDataWhenShared) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);
  const uint8_t* cdata = buf1.data();

  uint8_t* data1 = buf1.MutableData();
  uint8_t* data2 = buf2.MutableData();
  // buf1 was cloned above.
  EXPECT_NE(data1, cdata);
  // Therefore buf2 was no longer sharing data and was not cloned.
  EXPECT_EQ(data2, cdata);
}

TEST(CopyOnWriteBufferTest, MutableSpanReturnsEmptyOnEmptyBuffer) {
  CopyOnWriteBuffer buf;
  EXPECT_THAT(buf.AsMutableSpan(), IsEmpty());

  CopyOnWriteBuffer buf_with_capacity(/*size=*/0, /*capacity=*/10);
  EXPECT_THAT(buf_with_capacity.AsMutableSpan(), IsEmpty());
}

TEST(CopyOnWriteBufferTest, MutableSpanReturnsSizedSpan) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(3), /*capacity=*/10);

  EXPECT_THAT(buf.AsMutableSpan(), SizeIs(3));
}

TEST(CopyOnWriteBufferTest, MutableSpanClonesDataWhenShared) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);
  const uint8_t* cdata = buf1.data();

  std::span<uint8_t> data1 = buf1.AsMutableSpan();
  std::span<uint8_t> data2 = buf2.AsMutableSpan();
  // buf1 was cloned above.
  EXPECT_NE(data1.data(), cdata);
  // Therefore buf2 was no longer sharing data and was not cloned.
  EXPECT_EQ(data2.data(), cdata);
}

TEST(CopyOnWriteBufferTest, SeveralReads) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  EnsureBuffersShareData(buf1, buf2);
  for (size_t i = 0; i != 3u; ++i) {
    EXPECT_EQ(buf1[i], kTestData[i]);
  }
  EnsureBuffersShareData(buf1, buf2);
}

TEST(CopyOnWriteBufferTest, SeveralWrites) {
  CopyOnWriteBuffer buf1(std::span(kTestData).first(3), 10);
  CopyOnWriteBuffer buf2(buf1);

  EnsureBuffersShareData(buf1, buf2);
  for (size_t i = 0; i != 3u; ++i) {
    buf1.MutableData()[i] = kTestData[i] + 1;
  }
  EXPECT_EQ(buf1.size(), 3u);
  EXPECT_EQ(buf1.capacity(), 10u);
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 10u);
  EXPECT_EQ(0, memcmp(buf2.cdata(), kTestData, 3));
}

TEST(CopyOnWriteBufferTest, CreateSlice) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 4);
  EXPECT_EQ(slice.size(), 4u);
  EXPECT_EQ(0, memcmp(buf.cdata() + 3, slice.cdata(), 4));
}

TEST(CopyOnWriteBufferTest, NoCopyDataOnSlice) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 4);
  EXPECT_EQ(buf.cdata() + 3, slice.cdata());
}

TEST(CopyOnWriteBufferTest, WritingCopiesData) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 4);
  slice.MutableData()[0] = 0xaa;
  EXPECT_NE(buf.cdata() + 3, slice.cdata());
  EXPECT_EQ(0, memcmp(buf.cdata(), kTestData, 10));
}

TEST(CopyOnWriteBufferTest, WritingToBufferDoesntAffectsSlice) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 4);
  buf.MutableData()[0] = 0xaa;
  EXPECT_NE(buf.cdata() + 3, slice.cdata());
  EXPECT_EQ(0, memcmp(slice.cdata(), kTestData + 3, 4));
}

TEST(CopyOnWriteBufferTest, SliceOfASlice) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 7);
  CopyOnWriteBuffer slice2 = slice.Slice(2, 3);
  EXPECT_EQ(slice2.size(), 3u);
  EXPECT_EQ(slice.cdata() + 2, slice2.cdata());
  EXPECT_EQ(buf.cdata() + 5, slice2.cdata());
}

TEST(CopyOnWriteBufferTest, SlicesAreIndependent) {
  CopyOnWriteBuffer buf(std::span(kTestData).first(10));
  CopyOnWriteBuffer slice = buf.Slice(3, 7);
  CopyOnWriteBuffer slice2 = buf.Slice(3, 7);
  slice2.MutableData()[0] = 0xaa;
  EXPECT_EQ(buf.cdata() + 3, slice.cdata());
}

TEST(CopyOnWriteBufferTest, AcceptsVectorLikeTypes) {
  std::vector<uint8_t> a = {1, 2};
  std::array<uint8_t, 2> b = {3, 4};
  std::span<uint8_t> c(a);
  uint8_t d[] = {5, 7};

  CopyOnWriteBuffer a_buf(a);
  CopyOnWriteBuffer b_buf(b);
  CopyOnWriteBuffer c_buf(c);
  CopyOnWriteBuffer d_buf(d);

  CopyOnWriteBuffer all;
  all.Append(a);
  all.Append(b);
  all.Append(c);
  all.Append(d);

  EXPECT_EQ(all.size(), 8U);
}

}  // namespace webrtc
