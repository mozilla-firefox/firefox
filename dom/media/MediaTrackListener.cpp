/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaTrackListener.h"

#include "VideoSegment.h"

namespace mozilla {

#ifdef LOG
#  undef LOG
#endif

#define LOG(type, ...) \
  MOZ_LOG_FMT(gMediaTrackGraphLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)

void DirectMediaTrackListener::MirrorAndDisableSegment(
    VideoSegment& aFrom, VideoSegment& aTo, DisabledTrackMode aMode) {
  if (aMode == DisabledTrackMode::SILENCE_BLACK) {
    for (VideoSegment::ChunkIterator it(aFrom); !it.IsEnded(); it.Next()) {
      aTo.AppendFrame(*it, Some(true));
      aTo.ExtendLastFrameBy(it->GetDuration());
    }
  } else if (aMode == DisabledTrackMode::SILENCE_FREEZE) {
    aTo.AppendNullData(aFrom.GetDuration());
  }
}

void DirectMediaTrackListener::NotifyRealtimeTrackDataAndApplyTrackDisabling(
    MediaTrackGraph* aGraph, TrackTime aTrackOffset, MediaSegment& aMedia) {
  if (mDisabledFreezeCount == 0 && mDisabledBlackCount == 0) {
    NotifyRealtimeTrackData(aGraph, aTrackOffset, aMedia);
    return;
  }

  DisabledTrackMode mode = mDisabledBlackCount > 0
                               ? DisabledTrackMode::SILENCE_BLACK
                               : DisabledTrackMode::SILENCE_FREEZE;
  UniquePtr<MediaSegment> media(aMedia.CreateEmptyClone());
  if (aMedia.GetType() == MediaSegment::VIDEO) {
    MirrorAndDisableSegment(static_cast<VideoSegment&>(aMedia),
                            static_cast<VideoSegment&>(*media), mode);
  } else {
    MOZ_CRASH("Unsupported media type");
  }
  NotifyRealtimeTrackData(aGraph, aTrackOffset, *media);
}

void DirectMediaTrackListener::IncreaseDisabled(DisabledTrackMode aMode) {
  if (aMode == DisabledTrackMode::SILENCE_FREEZE) {
    ++mDisabledFreezeCount;
  } else if (aMode == DisabledTrackMode::SILENCE_BLACK) {
    ++mDisabledBlackCount;
  } else {
    MOZ_ASSERT(false, "Unknown disabled mode");
  }

  LOG(LogLevel::Debug,
      ("DirectMediaTrackListener {} increased disabled "
       "mode {}. Current counts are: freeze={}, black={}",
       fmt::ptr(this),
       aMode == DisabledTrackMode::SILENCE_FREEZE ? "freeze" : "black",
       int32_t(mDisabledFreezeCount), int32_t(mDisabledBlackCount)));
}

void DirectMediaTrackListener::DecreaseDisabled(DisabledTrackMode aMode) {
  if (aMode == DisabledTrackMode::SILENCE_FREEZE) {
    --mDisabledFreezeCount;
    MOZ_ASSERT(mDisabledFreezeCount >= 0, "Double decrease");
  } else if (aMode == DisabledTrackMode::SILENCE_BLACK) {
    --mDisabledBlackCount;
    MOZ_ASSERT(mDisabledBlackCount >= 0, "Double decrease");
  } else {
    MOZ_ASSERT(false, "Unknown disabled mode");
  }

  LOG(LogLevel::Debug,
      ("DirectMediaTrackListener {} decreased disabled "
       "mode {}. Current counts are: freeze={}, black={}",
       fmt::ptr(this),
       aMode == DisabledTrackMode::SILENCE_FREEZE ? "freeze" : "black",
       int32_t(mDisabledFreezeCount), int32_t(mDisabledBlackCount)));
}

}  // namespace mozilla
