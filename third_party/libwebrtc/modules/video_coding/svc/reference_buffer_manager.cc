/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/svc/reference_buffer_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/units/timestamp.h"
#include "api/video_codecs/video_encoder_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

using BufferSpaceType =
    ReferenceBufferManager::PredictionConstraints::BufferSpaceType;
using FrameType = VideoEncoderInterface::FrameType;

ReferenceBufferManager::ReferenceBufferManager(
    const PredictionConstraints& constraints)
    : buffer_space_type_(constraints.buffer_space_type()),
      num_spatial_layers_(std::max(1, constraints.max_spatial_layers())),
      pools_(buffer_space_type_ == BufferSpaceType::kMultiInstance
                 ? num_spatial_layers_
                 : 1,
             std::vector<Slot>(constraints.num_buffers())) {
  RTC_DCHECK_GT(constraints.num_buffers(), 0);
}

int ReferenceBufferManager::NumFreeBuffers(int spatial_id) const {
  return absl::c_count_if(Pool(spatial_id), [](const Slot& slot) {
    return !slot.reserved && !slot.metadata.has_value();
  });
}

std::optional<ReferenceBufferManager::Buffer>
ReferenceBufferManager::ReserveBuffer(int spatial_id) {
  std::span<Slot> pool = Pool(spatial_id);
  for (size_t i = 0; i < pool.size(); ++i) {
    Slot& slot = pool[i];
    if (!slot.reserved && !slot.metadata.has_value()) {
      slot.reserved = true;
      slot.spatial_id = spatial_id;
      return Buffer{.id = static_cast<int>(i), .spatial_id = spatial_id};
    }
  }
  return std::nullopt;
}

void ReferenceBufferManager::SetBufferMetadata(Buffer buffer,
                                               const Metadata& metadata) {
  RTC_DCHECK_EQ(buffer.spatial_id, metadata.spatial_id);
  Slot& slot = GetSlot(buffer);
  RTC_DCHECK(slot.reserved) << "Buffer " << buffer.id << " is not reserved.";
  RTC_DCHECK_EQ(slot.spatial_id, buffer.spatial_id);

  if (metadata.type == FrameType::kKeyframe) {
    switch (buffer_space_type_) {
      case BufferSpaceType::kMultiInstance:
      case BufferSpaceType::kSingleKeyframe:
        // Pool is either private to this layer or invalidated as a whole.
        for (Slot& other : Pool(buffer.spatial_id)) {
          other.metadata.reset();
        }
        break;
      case BufferSpaceType::kMultiKeyframe:
        ReleaseIf(buffer.spatial_id, [](const Slot&) { return true; });
        break;
    }
  }

  slot.reserved = false;
  slot.metadata = metadata;
  slot.sequence_number = next_sequence_number_++;
}

std::optional<ReferenceBufferManager::Metadata>
ReferenceBufferManager::GetMetadata(Buffer buffer) const {
  std::span<const Slot> pool = Pool(buffer.spatial_id);
  RTC_DCHECK_GE(buffer.id, 0);
  RTC_DCHECK_LT(buffer.id, pool.size());
  const Slot& slot = pool[buffer.id];
  if (slot.spatial_id != buffer.spatial_id) {
    return std::nullopt;
  }
  return slot.metadata;
}

std::optional<ReferenceBufferManager::Buffer> ReferenceBufferManager::Get(
    int spatial_id,
    int temporal_id) const {
  return FindLast(spatial_id, [&](const Metadata& metadata) {
    return metadata.temporal_id == temporal_id;
  });
}

std::optional<ReferenceBufferManager::Buffer>
ReferenceBufferManager::GetLastTl0(int spatial_id) const {
  return Get(spatial_id, /*temporal_id=*/0);
}

std::vector<ReferenceBufferManager::Buffer>
ReferenceBufferManager::GetAllLowerTemporalLayers(int spatial_id,
                                                  int temporal_id) const {
  std::vector<Buffer> buffers;
  for (int tid = 0; tid < temporal_id; ++tid) {
    if (std::optional<Buffer> buffer = Get(spatial_id, tid)) {
      buffers.push_back(*buffer);
    }
  }
  return buffers;
}

std::vector<ReferenceBufferManager::Buffer>
ReferenceBufferManager::SortByRecency(std::span<const Buffer> buffers) const {
  std::vector<Buffer> sorted;
  sorted.reserve(buffers.size());
  for (Buffer buffer : buffers) {
    if (!GetMetadata(buffer).has_value()) {
      RTC_LOG(LS_WARNING) << "Ignoring unpopulated buffer " << buffer.id
                          << " (spatial_id=" << buffer.spatial_id << ").";
      continue;
    }
    sorted.push_back(buffer);
  }
  absl::c_stable_sort(sorted, [&](Buffer a, Buffer b) {
    return Pool(a.spatial_id)[a.id].sequence_number >
           Pool(b.spatial_id)[b.id].sequence_number;
  });
  return sorted;
}

void ReferenceBufferManager::MarkFrameAsAcknowledged(int spatial_id,
                                                     int frame_id) {
  for (Slot& slot : Pool(spatial_id)) {
    if (slot.spatial_id == spatial_id && slot.metadata.has_value() &&
        slot.metadata->frame_id == frame_id) {
      slot.metadata->acknowledged = true;
    }
  }
}

std::optional<ReferenceBufferManager::Buffer>
ReferenceBufferManager::GetLastAcknowledgedFrame(int spatial_id) const {
  return FindLast(spatial_id, [](const Metadata& metadata) {
    return metadata.acknowledged;
  });
}

void ReferenceBufferManager::ReleaseBuffer(Buffer buffer) {
  Slot& slot = GetSlot(buffer);
  if (slot.spatial_id != buffer.spatial_id) {
    // Already released and reserved by another layer.
    return;
  }
  slot.reserved = false;
  slot.metadata.reset();
}

void ReferenceBufferManager::ReleaseAllHigherTemporalLayers(int spatial_id,
                                                            int temporal_id) {
  ReleaseIf(spatial_id, [&](const Slot& slot) {
    return slot.metadata->temporal_id.has_value() &&
           *slot.metadata->temporal_id > temporal_id;
  });
}

void ReferenceBufferManager::ReleaseAllPriorTemporalUnits(int spatial_id,
                                                          Timestamp cutoff) {
  ReleaseIf(spatial_id, [&](const Slot& slot) {
    return slot.metadata->timestamp < cutoff;
  });
}

void ReferenceBufferManager::ReleaseAllBuffers(int spatial_id) {
  for (Slot& slot : Pool(spatial_id)) {
    if (slot.spatial_id == spatial_id) {
      slot.reserved = false;
      slot.metadata.reset();
    }
  }
}

std::span<ReferenceBufferManager::Slot> ReferenceBufferManager::Pool(
    int spatial_id) {
  RTC_DCHECK_GE(spatial_id, 0);
  RTC_DCHECK_LT(spatial_id, num_spatial_layers_);
  return pools_[std::clamp<int>(spatial_id, 0, pools_.size() - 1)];
}

std::span<const ReferenceBufferManager::Slot> ReferenceBufferManager::Pool(
    int spatial_id) const {
  RTC_DCHECK_GE(spatial_id, 0);
  RTC_DCHECK_LT(spatial_id, num_spatial_layers_);
  return pools_[std::clamp<int>(spatial_id, 0, pools_.size() - 1)];
}

ReferenceBufferManager::Slot& ReferenceBufferManager::GetSlot(Buffer buffer) {
  std::span<Slot> pool = Pool(buffer.spatial_id);
  RTC_DCHECK_GE(buffer.id, 0);
  RTC_DCHECK_LT(buffer.id, pool.size());
  return pool[buffer.id];
}

template <typename Predicate>
std::optional<ReferenceBufferManager::Buffer> ReferenceBufferManager::FindLast(
    int spatial_id,
    Predicate predicate) const {
  std::span<const Slot> pool = Pool(spatial_id);
  std::optional<Buffer> last;
  int64_t last_sequence_number = -1;
  for (size_t i = 0; i < pool.size(); ++i) {
    const Slot& slot = pool[i];
    if (slot.spatial_id == spatial_id && slot.metadata.has_value() &&
        slot.sequence_number > last_sequence_number &&
        predicate(*slot.metadata)) {
      last = Buffer{.id = static_cast<int>(i), .spatial_id = spatial_id};
      last_sequence_number = slot.sequence_number;
    }
  }
  return last;
}

template <typename Predicate>
void ReferenceBufferManager::ReleaseIf(int spatial_id, Predicate predicate) {
  for (Slot& slot : Pool(spatial_id)) {
    if (slot.spatial_id == spatial_id && slot.metadata.has_value() &&
        predicate(slot)) {
      slot.metadata.reset();
    }
  }
}

}  // namespace webrtc
