/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/fec_private_tables_bursty.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "modules/include/module_fec_types.h"
#include "modules/rtp_rtcp/source/fec_private_tables_random.h"
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "test/gtest.h"

namespace webrtc {
namespace {
constexpr std::array<uint8_t, 12> kMaskRandom15_6 = {
    0x82, 0x08, 0x41, 0x04, 0x20, 0x82, 0x10, 0x40, 0x08, 0x20, 0x04, 0x10};
}  // namespace

namespace fec_private_tables {

using internal::LookUpInFecTable;

TEST(FecTable, TestBurstyLookup) {
  std::span<const uint8_t> result;
  result = LookUpInFecTable(kPacketMaskBurstyTbl, 0, 0);
  // Should match kMaskBursty1_1.
  EXPECT_EQ(2u, result.size());
  EXPECT_EQ(0x80u, result[0]);

  result = LookUpInFecTable(kPacketMaskBurstyTbl, 3, 0);
  // Should match kMaskBursty4_1.
  EXPECT_EQ(2u, result.size());
  EXPECT_EQ(0xf0u, result[0]);
  EXPECT_EQ(0x00u, result[1]);

  result = LookUpInFecTable(kPacketMaskBurstyTbl, 1, 1);
  // Should match kMaskBursty2_2.
  EXPECT_EQ(4u, result.size());
  EXPECT_EQ(0x80u, result[0]);
  EXPECT_EQ(0xc0u, result[2]);

  result = LookUpInFecTable(kPacketMaskBurstyTbl, 11, 11);
  // Should match kMaskBursty12_12.
  EXPECT_EQ(24u, result.size());
  EXPECT_EQ(0x80u, result[0]);
  EXPECT_EQ(0x30u, result[23]);
}

TEST(FecTable, TestRandomLookup) {
  std::span<const uint8_t> result;
  result = LookUpInFecTable(kPacketMaskRandomTbl, 0, 0);
  EXPECT_EQ(2u, result.size());
  EXPECT_EQ(0x80u, result[0]);
  EXPECT_EQ(0x00u, result[1]);

  result = LookUpInFecTable(kPacketMaskRandomTbl, 4, 1);
  // kMaskRandom5_2.
  EXPECT_EQ(4u, result.size());
  EXPECT_EQ(0xa8u, result[0]);
  EXPECT_EQ(0xd0u, result[2]);
}

TEST(FecTable, TestRandomGenerated) {
  FecMaskType fec_mask_type = kFecMaskRandom;
  int num_media_packets = 15;
  int num_fec_packets = 6;
  size_t mask_size = sizeof(kMaskRandom15_6) / sizeof(uint8_t);
  internal::PacketMaskTable mask_table(fec_mask_type, num_media_packets);
  std::span<const uint8_t> mask =
      mask_table.LookUp(num_media_packets, num_fec_packets);
  EXPECT_EQ(mask.size(), mask_size);
  for (size_t i = 0; i < mask_size; ++i) {
    EXPECT_EQ(mask[i], kMaskRandom15_6[i]);
  }
}

}  // namespace fec_private_tables
}  // namespace webrtc
