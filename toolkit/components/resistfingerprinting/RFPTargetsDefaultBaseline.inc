/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// #ifdef NIGHTLY_BUILD is supported as a preprocessor macro.
// If any others are needed, update extract_rfp_targets.py

DESKTOP_DEFAULT(EfficientCanvasRandomization)
DESKTOP_DEFAULT(ScreenAvailToResolution)
DESKTOP_DEFAULT(MaxTouchPointsCollapse)

ANDROID_DEFAULT(EfficientCanvasRandomization)
ANDROID_DEFAULT(ScreenAvailToResolution)
ANDROID_DEFAULT(MaxTouchPointsCollapse)
