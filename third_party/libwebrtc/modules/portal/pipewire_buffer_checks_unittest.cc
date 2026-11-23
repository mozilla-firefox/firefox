/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <spa/buffer/buffer.h>

#include <cstdint>

#include "modules/portal/pipewire_utils.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

class PipeWireBufferChecksTest : public ::testing::TestWithParam<uint32_t> {
 protected:
  PipeWireBufferChecksTest() {
    chunk_.size = 64;
    data_.type = GetParam();
    data_.chunk = &chunk_;
    data_.maxsize = 64;
    buffer_.n_datas = 1;
    buffer_.datas = &data_;
  }
  spa_chunk chunk_{};
  spa_data data_{};
  spa_buffer buffer_{};
};

TEST_P(PipeWireBufferChecksTest, AcceptsNonemptyPayload) {
  EXPECT_TRUE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsZeroBytes) {
  chunk_.size = 0;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, LeavesZeroBytesToCaller) {
  chunk_.size = 0;
  EXPECT_TRUE(PipeWireBufferHasUncorruptedChunk(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsCorruptedChunk) {
  chunk_.flags = SPA_CHUNK_FLAG_CORRUPTED;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, PreservesNonemptyNeutralPayload) {
  chunk_.flags = SPA_CHUNK_FLAG_EMPTY;
  EXPECT_TRUE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsEmptyNeutralPayload) {
  chunk_.flags = SPA_CHUNK_FLAG_EMPTY;
  chunk_.size = 0;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsCorruptedNeutralPayload) {
  chunk_.flags = SPA_CHUNK_FLAG_EMPTY | SPA_CHUNK_FLAG_CORRUPTED;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsMissingPlanes) {
  buffer_.n_datas = 0;
  buffer_.datas = nullptr;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsMissingDataArray) {
  buffer_.datas = nullptr;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}
TEST_P(PipeWireBufferChecksTest, RejectsMissingChunk) {
  data_.chunk = nullptr;
  EXPECT_FALSE(PipeWireBufferHasFrameData(buffer_));
}

INSTANTIATE_TEST_SUITE_P(MemoryTypes,
                         PipeWireBufferChecksTest,
                         ::testing::Values(SPA_DATA_MemPtr,
                                           SPA_DATA_MemFd,
                                           SPA_DATA_DmaBuf));

}  // namespace
}  // namespace webrtc
