/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_SVC_REFERENCE_BUFFER_MANAGER_H_
#define MODULES_VIDEO_CODING_SVC_REFERENCE_BUFFER_MANAGER_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "api/units/timestamp.h"
#include "api/video_codecs/video_encoder_factory_interface.h"
#include "api/video_codecs/video_encoder_interface.h"

namespace webrtc {

// Keeps track of which encoder reference buffers are free, reserved for an
// ongoing encode or populated with a frame, while enforcing the rules of the
// encoder's buffer space type.
//
// Buffers are reserved before encoding and populated via `SetBufferMetadata()`
// once the encode succeeds. Reserved but unpopulated buffers are never returned
// by queries nor invalidated by keyframes. "Last" means most recently
// populated.
class ReferenceBufferManager {
 public:
  using PredictionConstraints =
      VideoEncoderFactoryInterface::Capabilities::PredictionConstraints;

  // Handle to an encoder reference buffer.
  struct Buffer {
    // Encoder buffer index.
    int id = 0;
    // Owning instance for `kMultiInstance`, otherwise the reserving layer.
    int spatial_id = 0;

    friend bool operator==(const Buffer&, const Buffer&) = default;

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Buffer& buffer) {
      sink.Append("{id=" + std::to_string(buffer.id) +
                  ", spatial_id=" + std::to_string(buffer.spatial_id) + "}");
    }
  };

  // Describes the frame stored in a buffer.
  struct Metadata {
    VideoEncoderInterface::FrameType type =
        VideoEncoderInterface::FrameType::kDeltaFrame;
    int spatial_id = 0;
    std::optional<int> temporal_id;
    Timestamp timestamp = Timestamp::Zero();
    std::optional<int> frame_id;
    bool acknowledged = false;
  };

  explicit ReferenceBufferManager(const PredictionConstraints& constraints);

  // Number of buffers that `ReserveBuffer(spatial_id)` could still reserve.
  int NumFreeBuffers(int spatial_id) const;
  // Reserves the lowest indexed free buffer, or nullopt if none is free.
  std::optional<Buffer> ReserveBuffer(int spatial_id);
  // Populates a reserved buffer. A keyframe first releases all populated
  // buffers that it invalidates according to the buffer space type.
  void SetBufferMetadata(Buffer buffer, const Metadata& metadata);
  // Nullopt unless `buffer` is populated.
  std::optional<Metadata> GetMetadata(Buffer buffer) const;

  // Last buffer with the given temporal id.
  std::optional<Buffer> Get(int spatial_id, int temporal_id) const;
  // Last buffer of the base temporal layer (`temporal_id == 0`).
  std::optional<Buffer> GetLastTl0(int spatial_id) const;
  // Last buffer of each temporal layer below `temporal_id`, in increasing
  // temporal id order. Layers without a buffer are skipped.
  std::vector<Buffer> GetAllLowerTemporalLayers(int spatial_id,
                                                int temporal_id) const;
  // Returns `buffers` ordered most recently populated first, which is the
  // reference order expected by `VideoEncoderInterface`. Unpopulated buffers
  // are skipped.
  std::vector<Buffer> SortByRecency(std::span<const Buffer> buffers) const;

  // Marks all buffers holding `frame_id` as acknowledged. Unknown ids, e.g.
  // for buffers already released, are ignored.
  void MarkFrameAsAcknowledged(int spatial_id, int frame_id);
  // Last buffer holding an acknowledged frame.
  std::optional<Buffer> GetLastAcknowledgedFrame(int spatial_id) const;

  // Frees a reserved or populated buffer. No-op if already free.
  void ReleaseBuffer(Buffer buffer);
  // Frees populated buffers with temporal id above `temporal_id`.
  void ReleaseAllHigherTemporalLayers(int spatial_id, int temporal_id);
  // Frees populated buffers with a timestamp before `cutoff`.
  void ReleaseAllPriorTemporalUnits(int spatial_id, Timestamp cutoff);
  // Frees all reserved and populated buffers of `spatial_id`.
  void ReleaseAllBuffers(int spatial_id);

 private:
  // Internal state of a single encoder reference buffer, whereas `Buffer` is a
  // lightweight public handle. The slot for `buffer` is at index `buffer.id` in
  // `Pool(buffer.spatial_id)`.
  struct Slot {
    bool reserved = false;
    int spatial_id = 0;
    std::optional<Metadata> metadata;
    int64_t sequence_number = 0;
  };

  std::span<Slot> Pool(int spatial_id);
  std::span<const Slot> Pool(int spatial_id) const;
  Slot& GetSlot(Buffer buffer);
  // Last populated buffer of `spatial_id` matching `predicate`.
  template <typename Predicate>
  std::optional<Buffer> FindLast(int spatial_id, Predicate predicate) const;
  // Frees populated buffers of `spatial_id` matching `predicate`.
  template <typename Predicate>
  void ReleaseIf(int spatial_id, Predicate predicate);

  const PredictionConstraints::BufferSpaceType buffer_space_type_;
  const int num_spatial_layers_;
  // One pool per spatial layer for `kMultiInstance`, otherwise a single one.
  std::vector<std::vector<Slot>> pools_;
  int64_t next_sequence_number_ = 0;
};

}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_SVC_REFERENCE_BUFFER_MANAGER_H_
