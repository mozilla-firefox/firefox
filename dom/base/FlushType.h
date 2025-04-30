/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_FlushType_h
#define mozilla_FlushType_h

#include <stdint.h>
#include "X11UndefineNone.h"
#include "mozilla/EnumeratedArray.h"

namespace mozilla {

/**
 * This is the enum used by Document::FlushPendingNotifications to
 * decide what to flush.
 *
 * Please note that if you change these values, you should sync it with the
 * kFlushTypeNames array below.
 */
enum class FlushType : uint8_t {
  None,             /* Actually don't flush anything */
  Event,            /* Flush pending events before notify other observers */
  Content,          /* flush the content model construction */
  ContentAndNotify, /* As above, plus flush the frame model
                       construction and other nsIMutationObserver
                       notifications. */
  Style,            /* As above, plus flush style reresolution */
  Frames,           /* As above, plus flush frame construction */
  EnsurePresShellInitAndFrames, /* As above, plus ensure the pres shell is alive
                                 */
  InterruptibleLayout, /* As above, plus flush reflow, but allow it to be
                          interrupted (so an incomplete layout may result) */
  Layout,              /* As above, but layout must run to completion */
  Display,             /* As above, plus flush painting */
  Count
};

// Flush type strings that will be displayed in the profiler
// clang-format off
const EnumeratedArray<FlushType, const char*, size_t(FlushType::Count)>
    kFlushTypeNames = {
  "",
  "Event",
  "Content",
  "ContentAndNotify",
  "Style",
  // As far as the profiler is concerned, EnsurePresShellInitAndFrames and
  // Frames are the same
  "Style",
  "Style",
  "InterruptibleLayout",
  "Layout",
  "Display"
};
// clang-format on

struct ChangesToFlush {
  ChangesToFlush(FlushType aFlushType, bool aFlushAnimations,
                 bool aUpdateRelevancy)
      : mFlushType(aFlushType),
        mFlushAnimations(aFlushAnimations),
        mUpdateRelevancy(aUpdateRelevancy) {
          MOZ_ASSERT_IF(mUpdateRelevancy, mFlushType >= FlushType::Layout);
        }

  FlushType mFlushType;
  bool mFlushAnimations;
  bool mUpdateRelevancy;
};

}  // namespace mozilla

#endif  // mozilla_FlushType_h
