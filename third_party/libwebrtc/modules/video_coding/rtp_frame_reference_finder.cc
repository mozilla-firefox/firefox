/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/rtp_frame_reference_finder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "api/field_trials_view.h"
#include "api/video/video_codec_type.h"
#include "modules/rtp_rtcp/source/frame_object.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"
#include "modules/video_coding/rtp_frame_id_only_ref_finder.h"
#include "modules/video_coding/rtp_generic_ref_finder.h"
#include "modules/video_coding/rtp_seq_num_only_ref_finder.h"
#include "modules/video_coding/rtp_vp8_ref_finder.h"
#include "modules/video_coding/rtp_vp9_ref_finder.h"
#include "rtc_base/numerics/sequence_number_util.h"

namespace webrtc {
namespace internal {
class RtpFrameReferenceFinderImpl {
 public:
  explicit RtpFrameReferenceFinderImpl(const FieldTrialsView* field_trials)
      : field_trials_(field_trials) {}

  RtpFrameReferenceFinder::ReturnVector ManageFrame(
      std::unique_ptr<RtpFrameObject> frame);
  RtpFrameReferenceFinder::ReturnVector PaddingReceived(uint16_t seq_num);
  void ClearTo(uint16_t seq_num);

 private:
  using RefFinder = std::variant<std::monostate,
                                 RtpGenericFrameRefFinder,
                                 RtpFrameIdOnlyRefFinder,
                                 RtpSeqNumOnlyRefFinder,
                                 RtpVp8RefFinder,
                                 RtpVp9RefFinder>;

  template <typename T>
  T& GetRefFinderAs();

  const FieldTrialsView* const field_trials_;
  RefFinder ref_finder_;
};

RtpFrameReferenceFinder::ReturnVector RtpFrameReferenceFinderImpl::ManageFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  const RTPVideoHeader& video_header = frame->GetRtpVideoHeader();

  if (video_header.generic.has_value()) {
    return GetRefFinderAs<RtpGenericFrameRefFinder>().ManageFrame(
        std::move(frame), *video_header.generic);
  }

  switch (frame->codec_type()) {
    case kVideoCodecVP8: {
      const RTPVideoHeaderVP8& vp8_header =
          std::get<RTPVideoHeaderVP8>(video_header.video_type_header);

      if (vp8_header.temporalIdx == kNoTemporalIdx ||
          vp8_header.tl0PicIdx == kNoTl0PicIdx) {
        if (vp8_header.pictureId == kNoPictureId) {
          return GetRefFinderAs<RtpSeqNumOnlyRefFinder>().ManageFrame(
              std::move(frame));
        }

        return GetRefFinderAs<RtpFrameIdOnlyRefFinder>().ManageFrame(
            std::move(frame), vp8_header.pictureId);
      }

      return GetRefFinderAs<RtpVp8RefFinder>().ManageFrame(std::move(frame));
    }
    case kVideoCodecVP9: {
      const RTPVideoHeaderVP9& vp9_header =
          std::get<RTPVideoHeaderVP9>(video_header.video_type_header);

      if (vp9_header.temporal_idx == kNoTemporalIdx) {
        if (vp9_header.picture_id == kNoPictureId) {
          return GetRefFinderAs<RtpSeqNumOnlyRefFinder>().ManageFrame(
              std::move(frame));
        }

        return GetRefFinderAs<RtpFrameIdOnlyRefFinder>().ManageFrame(
            std::move(frame), vp9_header.picture_id);
      }

      return GetRefFinderAs<RtpVp9RefFinder>().ManageFrame(std::move(frame));
    }
    case kVideoCodecGeneric: {
      if (auto* generic_header = std::get_if<RTPVideoHeaderLegacyGeneric>(
              &video_header.video_type_header)) {
        return GetRefFinderAs<RtpFrameIdOnlyRefFinder>().ManageFrame(
            std::move(frame), generic_header->picture_id);
      }

      return GetRefFinderAs<RtpSeqNumOnlyRefFinder>().ManageFrame(
          std::move(frame));
    }
    default: {
      return GetRefFinderAs<RtpSeqNumOnlyRefFinder>().ManageFrame(
          std::move(frame));
    }
  }
}

RtpFrameReferenceFinder::ReturnVector
RtpFrameReferenceFinderImpl::PaddingReceived(uint16_t seq_num) {
  if (auto* ref_finder = std::get_if<RtpSeqNumOnlyRefFinder>(&ref_finder_)) {
    return ref_finder->PaddingReceived(seq_num);
  }
  return {};
}

void RtpFrameReferenceFinderImpl::ClearTo(uint16_t seq_num) {
  struct ClearToVisitor {
    void operator()(std::monostate& /* ref_finder */) {}
    void operator()(RtpGenericFrameRefFinder& /* ref_finder */) {}
    void operator()(RtpFrameIdOnlyRefFinder& /* ref_finder */) {}
    void operator()(RtpSeqNumOnlyRefFinder& ref_finder) {
      ref_finder.ClearTo(seq_num);
    }
    void operator()(RtpVp8RefFinder& ref_finder) {
      ref_finder.ClearTo(seq_num);
    }
    void operator()(RtpVp9RefFinder& ref_finder) {
      ref_finder.ClearTo(seq_num);
    }
    uint16_t seq_num;
  };

  std::visit(ClearToVisitor{seq_num}, ref_finder_);
}

template <typename T>
T& RtpFrameReferenceFinderImpl::GetRefFinderAs() {
  if (auto* ref_finder = std::get_if<T>(&ref_finder_)) {
    return *ref_finder;
  }
  if constexpr (std::is_same_v<T, RtpSeqNumOnlyRefFinder>) {
    return ref_finder_.emplace<T>(field_trials_);
  } else {
    return ref_finder_.emplace<T>();
  }
}

}  // namespace internal

RtpFrameReferenceFinder::RtpFrameReferenceFinder(
    const FieldTrialsView* field_trials)
    : RtpFrameReferenceFinder(field_trials, 0) {}

RtpFrameReferenceFinder::RtpFrameReferenceFinder(
    const FieldTrialsView* field_trials,
    int64_t picture_id_offset)
    : picture_id_offset_(picture_id_offset),
      impl_(std::make_unique<internal::RtpFrameReferenceFinderImpl>(
          field_trials)) {}

RtpFrameReferenceFinder::~RtpFrameReferenceFinder() = default;

RtpFrameReferenceFinder::ReturnVector RtpFrameReferenceFinder::ManageFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  // Drop frames cleared past -- but only when the 32-bit RTP timestamp also
  // confirms the frame is older. The 16-bit AheadOf is ambiguous across a
  // sequence-number wrap after prolonged decode stalls
  // (issues.webrtc.org/516639936).
  if (cleared_to_seq_num_ != -1 &&
      AheadOf<uint16_t>(cleared_to_seq_num_, frame->first_seq_num()) &&
      (!cleared_to_timestamp_.has_value() ||
       AheadOf<uint32_t>(*cleared_to_timestamp_, frame->RtpTimestamp()))) {
    return {};
  }

  auto frames = impl_->ManageFrame(std::move(frame));
  AddPictureIdOffset(frames);
  return frames;
}

RtpFrameReferenceFinder::ReturnVector RtpFrameReferenceFinder::PaddingReceived(
    uint16_t seq_num) {
  auto frames = impl_->PaddingReceived(seq_num);
  AddPictureIdOffset(frames);
  return frames;
}

void RtpFrameReferenceFinder::ClearTo(uint16_t seq_num,
                                      std::optional<uint32_t> rtp_timestamp) {
  cleared_to_seq_num_ = seq_num;
  cleared_to_timestamp_ = rtp_timestamp;
  impl_->ClearTo(seq_num);
}

void RtpFrameReferenceFinder::AddPictureIdOffset(ReturnVector& frames) {
  for (auto& frame : frames) {
    frame->SetId(frame->Id() + picture_id_offset_);
    for (size_t i = 0; i < frame->num_references; ++i) {
      frame->references[i] += picture_id_offset_;
    }
  }
}

}  // namespace webrtc
