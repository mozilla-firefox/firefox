/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/gamepad/
 * https://w3c.github.io/gamepad/extensions.html
 * https://w3c.github.io/webvr/spec/1.1/#interface-gamepad
 */

[Pref="dom.gamepad.enabled",
 Exposed=Window]
interface GamepadButton {
  readonly    attribute boolean pressed;
  readonly    attribute boolean touched;
  readonly    attribute double  value;
};

enum GamepadHand {
  "",
  "left",
  "right"
};

/**
 * https://www.w3.org/TR/gamepad/#gamepadmappingtype-enum
 * https://immersive-web.github.io/webxr-gamepads-module/#enumdef-gamepadmappingtype
 */
enum GamepadMappingType {
  "",
  "standard",
  "xr-standard"
};

[Pref="dom.gamepad.enabled",
 Exposed=Window]
interface Gamepad {
  /**
   * An identifier, unique per type of device.
   */
  readonly attribute DOMString id;

  /**
   * The game port index for the device. Unique per device
   * attached to this system.
   */
  readonly attribute long index;

  /**
   * The mapping in use for this device. The empty string
   * indicates that no mapping is in use.
   */
  readonly attribute GamepadMappingType mapping;

  /**
   * The hand in use for this device. The empty string
   * indicates that unknown, both hands, or not applicable
   */
  [Pref="dom.gamepad.hand.enabled", Deprecated=GamepadHand]
  readonly attribute GamepadHand hand;

  /**
   * true if this gamepad is currently connected to the system.
   */
  readonly attribute boolean connected;

  /**
   * The current state of all buttons on the device, an
   * array of GamepadButton.
   */
  [Pure, Cached, Frozen]
  readonly attribute sequence<GamepadButton> buttons;

  /**
   * The current position of all axes on the device, an
   * array of doubles.
   */
  [Pure, Cached, Frozen]
  readonly attribute sequence<double> axes;

  /**
   * Timestamp from when the data of this device was last updated.
   */
  readonly attribute DOMHighResTimeStamp timestamp;

  /**
   * The current pose of the device, a GamepadPose.
   */
  [Pref="dom.gamepad.pose.enabled", Deprecated=GamepadPose]
  readonly attribute GamepadPose? pose;

  /**
   * The current haptic actuator of the device, an array of
   * GamepadHapticActuator.
   */
  [Constant, Cached, Frozen, Pref="dom.gamepad.haptic_actuators.enabled", Deprecated=GamepadHapticActuators]
  readonly attribute sequence<GamepadHapticActuator> hapticActuators;

  [Constant, Cached, Frozen, Pref="dom.gamepad.extensions.lightindicator"]
  readonly attribute sequence<GamepadLightIndicator> lightIndicators;

  [Constant, Cached, Frozen, Pref="dom.gamepad.extensions.multitouch"]
  readonly attribute sequence<GamepadTouch> touchEvents;
};
