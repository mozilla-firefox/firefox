/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

#include "absl/strings/string_view.h"
#include "rtc_base/checks.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;

auto kTestData =
    std::to_array<uint8_t>({0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
                            0xa, 0xb, 0xc, 0xd, 0xe, 0xf});

uint8_t* TestDataAtIndex(int index) {
  return std::span<uint8_t>(kTestData).subspan(index).data();
}

void TestBuf(const Buffer& b1, size_t size, size_t capacity) {
  EXPECT_EQ(b1.size(), size);
  EXPECT_EQ(b1.capacity(), capacity);
}

}  // namespace

TEST(BufferTest, TestConstructEmpty) {
  TestBuf(Buffer(), 0, 0);
  TestBuf(Buffer(Buffer()), 0, 0);
  TestBuf(Buffer::CreateUninitializedWithSize(0), 0, 0);

  TestBuf(Buffer::CreateWithCapacity(10), 0, 10);

  TestBuf(Buffer(kTestData.data(), 0), 0, 0);
  TestBuf(Buffer(kTestData.data(), 0, 20), 0, 20);
}

TEST(BufferTest, TestEmptyBuffersAreEqual) {
  Buffer buf1;
  Buffer buf2;
  EXPECT_EQ(buf1, buf2);
}

TEST(BufferTest, TestConstructData) {
  Buffer buf(kTestData.data(), 7);
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.capacity(), 7u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 7));
}

TEST(BufferTest, TestConstructDataWithCapacity) {
  Buffer buf(kTestData.data(), 7, 14);
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.capacity(), 14u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 7));
}

TEST(BufferTest, TestConstructArray) {
  Buffer buf(kTestData);
  EXPECT_EQ(buf.size(), 16u);
  EXPECT_EQ(buf.capacity(), 16u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 16));
}

TEST(BufferTest, TestStringViewConversion) {
  Buffer buf(kTestData);
  absl::string_view view(buf);
  EXPECT_EQ(view, absl::string_view(
                      reinterpret_cast<const char*>(kTestData.data()), 16u));
}

TEST(BufferTest, TestSetData) {
  Buffer buf(TestDataAtIndex(4), 7);
  buf.SetData(kTestData.data(), 9);
  EXPECT_EQ(buf.size(), 9u);
  EXPECT_EQ(buf.capacity(), 7u * 3 / 2);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 9));
  Buffer buf2;
  buf2.SetData(buf);
  EXPECT_EQ(buf.size(), 9u);
  EXPECT_EQ(buf.capacity(), 7u * 3 / 2);
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 9));
}

TEST(BufferTest, TestAppendData) {
  Buffer buf(TestDataAtIndex(4), 3);
  buf.AppendData(TestDataAtIndex(10), 2);
  const int8_t exp[] = {0x4, 0x5, 0x6, 0xa, 0xb};
  EXPECT_EQ(buf, Buffer(exp));
  Buffer buf2;
  buf2.AppendData(buf);
  buf2.AppendData(std::span<uint8_t>(buf));
  const int8_t exp2[] = {0x4, 0x5, 0x6, 0xa, 0xb, 0x4, 0x5, 0x6, 0xa, 0xb};
  EXPECT_EQ(buf2, Buffer(exp2));
}

TEST(BufferTest, TestSetAndAppendWithUnknownArg) {
  struct TestDataContainer {
    size_t size() const { return 3; }
    const uint8_t* data() const { return kTestData.data(); }
  };
  Buffer buf;
  buf.SetData(TestDataContainer());
  EXPECT_EQ(3u, buf.size());
  EXPECT_EQ(Buffer(kTestData.data(), 3), buf);
  EXPECT_THAT(buf, ElementsAre(0, 1, 2));
  buf.AppendData(TestDataContainer());
  EXPECT_EQ(6u, buf.size());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 3));
  EXPECT_EQ(0, memcmp(std::span<uint8_t>(buf).subspan(3).data(),
                      kTestData.data(), 3));
  EXPECT_THAT(buf, ElementsAre(0, 1, 2, 0, 1, 2));
}

TEST(BufferTest, TestSetSizeSmaller) {
  Buffer buf;
  buf.SetData(kTestData.data(), 15);
  buf.SetSize(10);
  EXPECT_EQ(buf.size(), 10u);
  EXPECT_EQ(buf.capacity(), 15u);  // Hasn't shrunk.
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf, Buffer(kTestData.data(), 10));
}

TEST(BufferTest, TestSetSizeLarger) {
  Buffer buf;
  buf.SetData(kTestData.data(), 15);
  EXPECT_EQ(buf.size(), 15u);
  EXPECT_EQ(buf.capacity(), 15u);
  EXPECT_FALSE(buf.empty());
  buf.SetSize(20);
  EXPECT_EQ(buf.size(), 20u);
  EXPECT_EQ(buf.capacity(), 15u * 3 / 2);  // Has grown.
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(0, memcmp(buf.data(), kTestData.data(), 15));
}

TEST(BufferTest, TestEnsureCapacitySmaller) {
  Buffer buf(kTestData);
  const char* data = buf.data<char>();
  buf.EnsureCapacity(4);
  EXPECT_EQ(buf.capacity(), 16u);     // Hasn't shrunk.
  EXPECT_EQ(buf.data<char>(), data);  // No reallocation.
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf, Buffer(kTestData));
}

TEST(BufferTest, TestEnsureCapacityLarger) {
  Buffer buf(kTestData.data(), 5);
  buf.EnsureCapacity(10);
  const int8_t* data = buf.data<int8_t>();
  EXPECT_EQ(buf.capacity(), 10u);
  buf.AppendData(TestDataAtIndex(5), 5);
  EXPECT_EQ(buf.data<int8_t>(), data);  // No reallocation.
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf, Buffer(kTestData.data(), 10));
}

TEST(BufferTest, TestMoveConstruct) {
  Buffer buf1(kTestData.data(), 3, 40);
  const uint8_t* data = buf1.data();
  Buffer buf2(std::move(buf1));
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 40u);
  EXPECT_EQ(buf2.data(), data);
  EXPECT_FALSE(buf2.empty());
  buf1.Clear();
  EXPECT_EQ(buf1.size(), 0u);
  EXPECT_EQ(buf1.capacity(), 0u);
  EXPECT_EQ(buf1.data(), nullptr);
  EXPECT_TRUE(buf1.empty());
}

TEST(BufferTest, TestMoveAssign) {
  Buffer buf1(kTestData.data(), 3, 40);
  const uint8_t* data = buf1.data();
  Buffer buf2(kTestData);
  buf2 = std::move(buf1);
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 40u);
  EXPECT_EQ(buf2.data(), data);
  EXPECT_FALSE(buf2.empty());
  buf1.Clear();
  EXPECT_EQ(buf1.size(), 0u);
  EXPECT_EQ(buf1.capacity(), 0u);
  EXPECT_EQ(buf1.data(), nullptr);
  EXPECT_TRUE(buf1.empty());
}

TEST(BufferTest, TestMoveAssignSelf) {
  // Move self-assignment isn't required to produce a meaningful state, but
  // should not leave the object in an inconsistent state. (Such inconsistent
  // state could be caught by the DCHECKs and/or by the leak checker.) We need
  // to be sneaky when testing this; if we're doing a too-obvious
  // move-assign-to-self, clang's -Wself-move triggers at compile time.
  Buffer buf(kTestData.data(), 3, 40);
  Buffer* buf_ptr = &buf;
  buf = std::move(*buf_ptr);
}

TEST(BufferTest, TestSwap) {
  Buffer buf1(kTestData.data(), 3);
  Buffer buf2(kTestData.data(), 6, 40);
  uint8_t* data1 = buf1.data();
  uint8_t* data2 = buf2.data();
  using std::swap;
  swap(buf1, buf2);
  EXPECT_EQ(buf1.size(), 6u);
  EXPECT_EQ(buf1.capacity(), 40u);
  EXPECT_EQ(buf1.data(), data2);
  EXPECT_FALSE(buf1.empty());
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2.capacity(), 3u);
  EXPECT_EQ(buf2.data(), data1);
  EXPECT_FALSE(buf2.empty());
}

TEST(BufferTest, TestClear) {
  Buffer buf;
  buf.SetData(kTestData.data(), 15);
  EXPECT_EQ(buf.size(), 15u);
  EXPECT_EQ(buf.capacity(), 15u);
  EXPECT_FALSE(buf.empty());
  const char* data = buf.data<char>();
  buf.Clear();
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 15u);     // Hasn't shrunk.
  EXPECT_EQ(buf.data<char>(), data);  // No reallocation.
  EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, TestLambdaSetAppend) {
  auto setter = [](std::span<uint8_t> av) {
    for (int i = 0; i != 15; ++i)
      av[i] = kTestData[i];
    return 15;
  };

  Buffer buf1;
  buf1.SetData(kTestData.data(), 15);
  buf1.AppendData(kTestData.data(), 15);

  Buffer buf2;
  EXPECT_EQ(buf2.SetData(15, setter), 15u);
  EXPECT_EQ(buf2.AppendData(15, setter), 15u);
  EXPECT_EQ(buf1, buf2);
  EXPECT_EQ(buf1.capacity(), buf2.capacity());
  EXPECT_FALSE(buf1.empty());
  EXPECT_FALSE(buf2.empty());
}

TEST(BufferTest, TestLambdaSetAppendSigned) {
  auto setter = [](std::span<int8_t> av) {
    for (int i = 0; i != 15; ++i)
      av[i] = kTestData[i];
    return 15;
  };

  Buffer buf1;
  buf1.SetData(kTestData.data(), 15);
  buf1.AppendData(kTestData.data(), 15);

  Buffer buf2;
  EXPECT_EQ(buf2.SetData<int8_t>(15, setter), 15u);
  EXPECT_EQ(buf2.AppendData<int8_t>(15, setter), 15u);
  EXPECT_EQ(buf1, buf2);
  EXPECT_EQ(buf1.capacity(), buf2.capacity());
  EXPECT_FALSE(buf1.empty());
  EXPECT_FALSE(buf2.empty());
}

TEST(BufferTest, TestLambdaAppendEmpty) {
  auto setter = [](std::span<uint8_t> av) {
    for (int i = 0; i != 15; ++i)
      av[i] = kTestData[i];
    return 15;
  };

  Buffer buf1;
  buf1.SetData(kTestData.data(), 15);

  Buffer buf2;
  EXPECT_EQ(buf2.AppendData(15, setter), 15u);
  EXPECT_EQ(buf1, buf2);
  EXPECT_EQ(buf1.capacity(), buf2.capacity());
  EXPECT_FALSE(buf1.empty());
  EXPECT_FALSE(buf2.empty());
}

TEST(BufferTest, TestLambdaAppendPartial) {
  auto setter = [](std::span<uint8_t> av) {
    for (int i = 0; i != 7; ++i)
      av[i] = kTestData[i];
    return 7;
  };

  Buffer buf;
  EXPECT_EQ(buf.AppendData(15, setter), 7u);
  EXPECT_EQ(buf.size(), 7u);             // Size is exactly what we wrote.
  EXPECT_GE(buf.capacity(), 7u);         // Capacity is valid.
  EXPECT_NE(buf.data<char>(), nullptr);  // Data is actually stored.
  EXPECT_FALSE(buf.empty());
}

TEST(BufferTest, TestMutableLambdaSetAppend) {
  uint8_t magic_number = 17;
  auto setter = [magic_number](std::span<uint8_t> av) mutable {
    for (int i = 0; i != 15; ++i) {
      av[i] = magic_number;
      ++magic_number;
    }
    return 15;
  };

  EXPECT_EQ(magic_number, 17);

  Buffer buf;
  EXPECT_EQ(buf.SetData(15, setter), 15u);
  EXPECT_EQ(buf.AppendData(15, setter), 15u);
  EXPECT_EQ(buf.size(), 30u);            // Size is exactly what we wrote.
  EXPECT_GE(buf.capacity(), 30u);        // Capacity is valid.
  EXPECT_NE(buf.data<char>(), nullptr);  // Data is actually stored.
  EXPECT_FALSE(buf.empty());

  for (uint8_t i = 0; i != buf.size(); ++i) {
    EXPECT_EQ(buf[i], magic_number + i);
  }
}

TEST(BufferTest, TestBracketRead) {
  Buffer buf(kTestData.data(), 7);
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.capacity(), 7u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());

  for (size_t i = 0; i != 7u; ++i) {
    EXPECT_EQ(buf[i], kTestData[i]);
  }
}

TEST(BufferTest, TestBracketReadConst) {
  Buffer buf(kTestData.data(), 7);
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.capacity(), 7u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());

  const Buffer& cbuf = buf;

  for (size_t i = 0; i != 7u; ++i) {
    EXPECT_EQ(cbuf[i], kTestData[i]);
  }
}

TEST(BufferTest, TestBracketWrite) {
  Buffer buf = Buffer::CreateUninitializedWithSize(7);
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf.capacity(), 7u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());

  for (size_t i = 0; i != 7u; ++i) {
    buf[i] = kTestData[i];
  }

  EXPECT_THAT(buf, ElementsAreArray(kTestData.data(), 7));
}

TEST(BufferTest, TestBeginEnd) {
  const Buffer cbuf(kTestData);
  Buffer buf(kTestData);
  auto b1 = cbuf.begin();
  for (auto& x : buf) {
    EXPECT_EQ(*b1, x);
    ASSERT_NE(b1, cbuf.end());
    ++b1;
    ++x;
  }
  EXPECT_EQ(cbuf.end(), b1);
  auto b2 = buf.begin();
  for (auto& y : cbuf) {
    EXPECT_EQ(*b2, y + 1);
    ++b2;
  }
  EXPECT_EQ(buf.end(), b2);
}

TEST(BufferTest, TestInt16) {
  static constexpr int16_t test_data[] = {14, 15, 16, 17, 18};
  BufferT<int16_t> buf(test_data);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.capacity(), 5u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());
  EXPECT_THAT(buf, ElementsAreArray(test_data));
  BufferT<int16_t> buf2(test_data);
  EXPECT_EQ(buf, buf2);
  buf2[0] = 9;
  EXPECT_NE(buf, buf2);
}

TEST(BufferTest, TestFloat) {
  static constexpr float test_data[] = {14, 15, 16, 17, 18};
  BufferT<float> buf;
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 0u);
  EXPECT_EQ(buf.data(), nullptr);
  EXPECT_TRUE(buf.empty());
  buf.SetData(test_data);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.capacity(), 5u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());
  float* p1 = buf.data();
  while (buf.data() == p1) {
    buf.AppendData(test_data);
  }
  EXPECT_EQ(buf.size(), buf.capacity());
  EXPECT_GT(buf.size(), 5u);
  EXPECT_EQ(buf.size() % 5, 0u);
  EXPECT_NE(buf.data(), nullptr);
  for (size_t i = 0; i != buf.size(); ++i) {
    EXPECT_EQ(test_data[i % 5], buf[i]);
  }
}

TEST(BufferTest, TestStruct) {
  struct BloodStone {
    bool blood;
    const char* stone;
  };
  BufferT<BloodStone> buf = BufferT<BloodStone>::CreateUninitializedWithSize(4);
  EXPECT_EQ(buf.size(), 4u);
  EXPECT_EQ(buf.capacity(), 4u);
  EXPECT_NE(buf.data(), nullptr);
  EXPECT_FALSE(buf.empty());
  BufferT<BloodStone*> buf2 =
      BufferT<BloodStone*>::CreateUninitializedWithSize(4);
  for (size_t i = 0; i < buf2.size(); ++i) {
    buf2[i] = &buf[i];
  }
  static const char kObsidian[] = "obsidian";
  buf2[2]->stone = kObsidian;
  EXPECT_EQ(kObsidian, buf[2].stone);
}

TEST(BufferDeathTest, DieOnUseAfterMove) {
  Buffer buf = Buffer::CreateUninitializedWithSize(17);
  Buffer buf2 = std::move(buf);
  EXPECT_EQ(buf2.size(), 17u);
#if RTC_DCHECK_IS_ON
#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
  EXPECT_DEATH(buf.empty(), "");
#endif
#else
  EXPECT_TRUE(buf.empty());
#endif
}

TEST(ZeroOnFreeBufferTest, TestZeroOnSetData) {
  ZeroOnFreeBuffer<uint8_t> buf(kTestData.data(), 7);
  const uint8_t* old_data = buf.data();
  const size_t old_capacity = buf.capacity();
  constexpr size_t offset = 1;
  // Pointer to the last five bytes of the underlying buffer.
  auto to_be_zeroed = std::span<const uint8_t>(buf).subspan(2);
  buf.SetData(TestDataAtIndex(offset), 2);
  // Sanity checks to make sure the underlying heap memory was not reallocated.
  EXPECT_EQ(old_data, buf.data());
  EXPECT_EQ(old_capacity, buf.capacity());
  // The first two elements have been overwritten, and the remaining five have
  // been zeroed.
  EXPECT_EQ(kTestData[offset], buf[0]);
  EXPECT_EQ(kTestData[offset + 1], buf[1]);
  EXPECT_THAT(to_be_zeroed, Each(Eq(0)));
}

TEST(ZeroOnFreeBufferTest, TestZeroOnSetDataFromSetter) {
  static constexpr size_t offset = 1;
  const auto setter = [](std::span<uint8_t> av) {
    for (int i = 0; i != 2; ++i)
      av[i] = kTestData[offset + i];
    return 2;
  };

  ZeroOnFreeBuffer<uint8_t> buf(kTestData.data(), 7);
  const uint8_t* old_data = buf.data();
  auto to_be_zeroed = std::span<const uint8_t>(buf).subspan(2);
  const size_t old_capacity = buf.capacity();
  buf.SetData(2, setter);
  // Sanity checks to make sure the underlying heap memory was not reallocated.
  EXPECT_EQ(old_data, buf.data());
  EXPECT_EQ(old_capacity, buf.capacity());
  // The first two elements have been overwritten, and the remaining five have
  // been zeroed.
  EXPECT_EQ(kTestData[offset], buf[0]);
  EXPECT_EQ(kTestData[offset + 1], buf[1]);
  EXPECT_THAT(to_be_zeroed, Each(Eq(0)));
}

TEST(ZeroOnFreeBufferTest, TestZeroOnSetSize) {
  ZeroOnFreeBuffer<uint8_t> buf(kTestData.data(), 7);
  auto to_be_zeroed = std::span<const uint8_t>(buf).subspan(2);
  const uint8_t* old_data = buf.data();
  const size_t old_capacity = buf.capacity();
  buf.SetSize(2);
  // Sanity checks to make sure the underlying heap memory was not reallocated.
  EXPECT_EQ(old_data, buf.data());
  EXPECT_EQ(old_capacity, buf.capacity());
  // The first two elements have not been modified and the remaining five have
  // been zeroed.
  EXPECT_EQ(kTestData[0], buf[0]);
  EXPECT_EQ(kTestData[1], buf[1]);
  EXPECT_THAT(to_be_zeroed, Each(Eq(0)));
}

TEST(ZeroOnFreeBufferTest, TestZeroOnClear) {
  ZeroOnFreeBuffer<uint8_t> buf(kTestData.data(), 7);
  auto to_be_zeroed = std::span<const uint8_t>(buf);
  const uint8_t* old_data = buf.data();
  const size_t old_capacity = buf.capacity();
  buf.Clear();
  // Sanity checks to make sure the underlying heap memory was not reallocated.
  EXPECT_EQ(old_data, buf.data());
  EXPECT_EQ(old_capacity, buf.capacity());
  // The underlying memory was not released but cleared.
  EXPECT_THAT(to_be_zeroed, Each(Eq(0)));
}

}  // namespace webrtc
