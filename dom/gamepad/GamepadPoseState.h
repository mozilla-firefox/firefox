/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_gamepad_GamepadPoseState_h_
#define mozilla_dom_gamepad_GamepadPoseState_h_

#include <cstdint>

#include "mozilla/TypedEnumBits.h"

namespace mozilla::dom {

enum class GamepadCapabilityFlags : uint16_t {
  Cap_None = 0,
  /**
   * Cap_Position is set if the Gamepad is capable of tracking its position.
   */
  Cap_Position = 1 << 1,
  /**
   * Cap_Orientation is set if the Gamepad is capable of tracking its
   * orientation.
   */
  Cap_Orientation = 1 << 2,
  /**
   * Cap_AngularAcceleration is set if the Gamepad is capable of tracking its
   * angular acceleration.
   */
  Cap_AngularAcceleration = 1 << 3,
  /**
   * Cap_LinearAcceleration is set if the Gamepad is capable of tracking its
   * linear acceleration.
   */
  Cap_LinearAcceleration = 1 << 4,
  /**
   * Cap_GripSpacePosition is set if the Gamepad has a grip space position.
   */
  Cap_GripSpacePosition = 1 << 5,
  /**
   * Cap_PositionEmulated is set if the VRDisplay is capable of setting a
   * emulated position (e.g. neck model) even if still doesn't support 6DOF
   * tracking.
   */
  Cap_PositionEmulated = 1 << 6,
  /**
   * Cap_All used for validity checking during IPC serialization
   */
  Cap_All = (1 << 7) - 1
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(GamepadCapabilityFlags)

}  // namespace mozilla::dom

#endif  // mozilla_dom_gamepad_GamepadPoseState_h_
