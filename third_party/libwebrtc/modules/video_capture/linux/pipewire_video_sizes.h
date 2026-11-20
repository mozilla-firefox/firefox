/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CAPTURE_LINUX_PIPEWIRE_VIDEO_SIZES_H_
#define MODULES_VIDEO_CAPTURE_LINUX_PIPEWIRE_VIDEO_SIZES_H_

#include <spa/pod/pod.h>
#include <spa/utils/defs.h>

#include <vector>

namespace webrtc {
namespace videocapturemodule {

// Selects concrete sizes from a SPA_FORMAT_VIDEO_size value. Ranges yield a
// finite selection, filtered by their bounds and absolute SPA step multiples.
std::vector<spa_rectangle> GetPipeWireVideoSizes(const spa_pod* value);

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // MODULES_VIDEO_CAPTURE_LINUX_PIPEWIRE_VIDEO_SIZES_H_
