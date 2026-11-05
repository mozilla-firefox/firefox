/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/gamepad/extensions.html#gamepadhapticactuator-interface
 */

enum GamepadHapticActuatorType {
  "vibration"
};

[Pref="dom.gamepad.haptic_actuators.enabled",
 HeaderFile="mozilla/dom/GamepadHapticActuator.h",
 Exposed=Window]
interface GamepadHapticActuator
{
  readonly attribute GamepadHapticActuatorType type;
  [Throws, NewObject]
  Promise<boolean> pulse(double value, double duration);
};
