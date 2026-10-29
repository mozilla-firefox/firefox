/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_FEC_PRIVATE_TABLES_RANDOM_H_
#define MODULES_RTP_RTCP_SOURCE_FEC_PRIVATE_TABLES_RANDOM_H_

// This file contains a set of packets masks for the FEC code. The masks in
// this table are specifically designed to favor recovery to random loss.
// These packet masks are defined to protect up to maximum of 48 media packets.

#include <array>
#include <cstddef>
#include <cstdint>

namespace webrtc {
namespace fec_private_tables {

// Size of the flattened table for up to N=12 media packets.
// The flattened table keeps 12 square tables sizes from 1 to 12.
// Sum of all squared numbers N^2 from 1 to 12: 650.
// Each line in each table is prepended by its size.
// Sum of all numbers N from 1 to 12: 78
// Each 2d table is prepended by its size, adding 12 more entries.
// Table itself is prepended by the number of subtables, adding 1 more entry.
// Total number of entries = 1 + 12 + 78 + 650 = 741.
inline constexpr size_t kPacketMaskRandomTblSize = 741;
extern const std::array<uint8_t, kPacketMaskRandomTblSize> kPacketMaskRandomTbl;

}  // namespace fec_private_tables
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_FEC_PRIVATE_TABLES_RANDOM_H_
