/*
 *  Copyright (c) 2026 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <vector>

#include "gtest/gtest.h"
#include "vp9/common/vp9_scan.h"

namespace {

constexpr int kNumValues[] = { 4 * 4, 8 * 8, 16 * 16, 32 * 32 };

TEST(VP9Scan, DefaultScanOrderValuesInBound) {
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    const ScanOrder &scan_order = vp9_default_scan_orders[tx_size];
    for (int i = 0; i < kNumValues[tx_size]; ++i) {
      EXPECT_LE(scan_order.scan[i], MAX_SCAN_VALUE)
          << "vp9_default_scan_orders[" << tx_size << "].scan[" << i << "]";
      EXPECT_GE(scan_order.scan[i], 0)
          << "vp9_default_scan_orders[" << tx_size << "].scan[" << i << "]";
      EXPECT_LE(scan_order.iscan[i], MAX_SCAN_VALUE)
          << "vp9_default_scan_orders[" << tx_size << "].iscan[" << i << "]";
      EXPECT_GE(scan_order.iscan[i], 0)
          << "vp9_default_scan_orders[" << tx_size << "].iscan[" << i << "]";
    }
  }
}

TEST(VP9Scan, ScanOrderValuesInBound) {
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    for (int tx_type = 0; tx_type < TX_TYPES; ++tx_type) {
      const ScanOrder &scan_order = vp9_scan_orders[tx_size][tx_type];
      for (int i = 0; i < kNumValues[tx_size]; ++i) {
        EXPECT_LE(scan_order.scan[i], MAX_SCAN_VALUE)
            << "vp9_scan_orders[" << tx_size << "][" << tx_type << "].scan["
            << i << "]";
        EXPECT_GE(scan_order.scan[i], 0)
            << "vp9_scan_orders[" << tx_size << "][" << tx_type << "].scan["
            << i << "]";
        EXPECT_LE(scan_order.iscan[i], MAX_SCAN_VALUE)
            << "vp9_scan_orders[" << tx_size << "][" << tx_type << "].iscan["
            << i << "]";
        EXPECT_GE(scan_order.iscan[i], 0)
            << "vp9_scan_orders[" << tx_size << "][" << tx_type << "].iscan["
            << i << "]";
      }
    }
  }
}

// Both neighbors of every scan position 1 <= c <= num_values must precede c in
// scan order, including the trailing sentinel at c == num_values read by
// decode_coefs() and tokenize_b() after ++c on the final coefficient. The
// decoder, tokenizer, and vp9_optimize_b() rely on this to fill token_cache in
// scan order without initializing it first: every entry that
// get_coef_context() reads has already been written.
void CheckNeighborsPrecedeInScanOrder(const ScanOrder &scan_order,
                                      int num_values, const char *name) {
  // Map each coefficient index back to its scan position. This is derived
  // from scan[] rather than read from iscan[], which is 1-based.
  std::vector<int> scan_position(num_values, -1);
  for (int c = 0; c < num_values; ++c) {
    const int coeff = scan_order.scan[c];
    ASSERT_GE(coeff, 0) << name << " c=" << c;
    ASSERT_LT(coeff, num_values) << name << " c=" << c;
    ASSERT_EQ(scan_position[coeff], -1)
        << name << " c=" << c << " coeff=" << coeff << " scanned twice";
    scan_position[coeff] = c;
  }
  for (int c = 1; c <= num_values; ++c) {
    for (int k = 0; k < MAX_NEIGHBORS; ++k) {
      const int neighbor = scan_order.neighbors[MAX_NEIGHBORS * c + k];
      ASSERT_GE(neighbor, 0) << name << " c=" << c << " k=" << k;
      ASSERT_LT(neighbor, num_values) << name << " c=" << c << " k=" << k;
      EXPECT_LT(scan_position[neighbor], c)
          << name << " c=" << c << " k=" << k << " neighbor=" << neighbor;
    }
  }
}

TEST(VP9Scan, DefaultScanOrderNeighborsPrecedeInScanOrder) {
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    CheckNeighborsPrecedeInScanOrder(vp9_default_scan_orders[tx_size],
                                     kNumValues[tx_size],
                                     "vp9_default_scan_orders");
  }
}

TEST(VP9Scan, NeighborsPrecedeInScanOrder) {
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    for (int tx_type = 0; tx_type < TX_TYPES; ++tx_type) {
      CheckNeighborsPrecedeInScanOrder(vp9_scan_orders[tx_size][tx_type],
                                       kNumValues[tx_size], "vp9_scan_orders");
    }
  }
}

}  // namespace
