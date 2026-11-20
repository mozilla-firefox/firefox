/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/pipewire_video_sizes.h"

#include <spa/pod/iter.h>
#include <spa/utils/type.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace webrtc {
namespace videocapturemodule {
namespace {

// Common sizes offered from a size range, besides its default, minimum and
// maximum.
static constexpr spa_rectangle kCommonSizes[] = {
    {3840, 2160}, {2560, 1440}, {1920, 1080}, {1600, 1200}, {1280, 960},
    {1280, 720},  {1024, 768},  {800, 600},   {640, 480},   {640, 360},
    {352, 288},   {320, 240},   {320, 180},   {160, 120}};

// Appends |size| to |sizes| unless it is already there.
static void AddUniqueSize(std::vector<spa_rectangle>& sizes,
                          const spa_rectangle& size) {
  if (std::none_of(sizes.begin(), sizes.end(), [&size](const spa_rectangle& s) {
        return s.width == size.width && s.height == size.height;
      })) {
    sizes.push_back(size);
  }
}

// Returns the sizes to offer for a size range with default |def|, bounds
// |min| and |max| and, for a stepped range, |step|: those of the default,
// bounds and common sizes that the range allows. As in SPA, a size on a
// stepped range has a width and height that are multiples of the step; the
// bounds are checked on each side, which is stricter than SPA's comparison
// of areas: both the width and the height stay within the bounds.
static std::vector<spa_rectangle> SizesInRange(const spa_rectangle& def,
                                               const spa_rectangle& min,
                                               const spa_rectangle& max,
                                               const spa_rectangle* step) {
  auto allowed = [&min, &max, step](const spa_rectangle& size) {
    if (size.width < min.width || size.width > max.width ||
        size.height < min.height || size.height > max.height) {
      return false;
    }
    if (!step) {
      return true;
    }
    // A zero step allows no size, and the modulo below must not divide by it.
    if (step->width == 0 || step->height == 0) {
      return false;
    }
    return size.width % step->width == 0 && size.height % step->height == 0;
  };

  std::vector<spa_rectangle> candidates = {def, min, max};
  candidates.insert(candidates.end(), std::begin(kCommonSizes),
                    std::end(kCommonSizes));

  std::vector<spa_rectangle> sizes;
  for (const spa_rectangle& size : candidates) {
    if (allowed(size)) {
      AddUniqueSize(sizes, size);
    }
  }
  return sizes;
}

}  // namespace

std::vector<spa_rectangle> GetPipeWireVideoSizes(const spa_pod* value) {
  uint32_t n_items, choice;
  spa_pod* val = spa_pod_get_values(value, &n_items, &choice);
  if (val->type != SPA_TYPE_Rectangle || val->size < sizeof(spa_rectangle)) {
    return {};
  }
  const auto* rect = static_cast<const spa_rectangle*>(SPA_POD_BODY(val));
  std::vector<spa_rectangle> sizes;
  if (choice == SPA_CHOICE_None && n_items >= 1) {
    sizes.push_back(rect[0]);
  } else if (choice == SPA_CHOICE_Enum && n_items >= 2) {
    // The first value of an enum is its default; the alternatives follow.
    for (uint32_t i = 1; i < n_items; i++) {
      AddUniqueSize(sizes, rect[i]);
    }
  } else if ((choice == SPA_CHOICE_Range || choice == SPA_CHOICE_Step) &&
             n_items >= 3) {
    sizes = SizesInRange(
        rect[0], rect[1], rect[2],
        choice == SPA_CHOICE_Step && n_items >= 4 ? &rect[3] : nullptr);
  }
  return sizes;
}

}  // namespace videocapturemodule
}  // namespace webrtc
