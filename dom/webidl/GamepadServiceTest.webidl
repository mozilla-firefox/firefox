/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

[Pref="dom.gamepad.test.enabled",
 Exposed=Window]
interface GamepadServiceTest
{
  readonly attribute GamepadMappingType noMapping;
  readonly attribute GamepadMappingType standardMapping;

  [NewObject]
  Promise<unsigned long> addGamepad(DOMString id,
                                    GamepadMappingType mapping,
                                    unsigned long numButtons,
                                    unsigned long numAxes,
                                    unsigned long numHaptics,
                                    unsigned long numLightIndicator,
                                    unsigned long numTouchEvents);

  [NewObject]
  Promise<unsigned long> removeGamepad(unsigned long index);

  [NewObject]
  Promise<unsigned long> newButtonEvent(unsigned long index,
                      unsigned long button,
                      boolean pressed,
                      boolean touched);

  [NewObject]
  Promise<unsigned long> newButtonValueEvent(unsigned long index,
                                             unsigned long button,
                                             boolean pressed,
                                             boolean touched,
                                             double value);

  [NewObject]
  Promise<unsigned long> newAxisMoveEvent(unsigned long index,
                        unsigned long axis,
                        double value);

  [NewObject]
  Promise<unsigned long> newTouch(unsigned long index, unsigned long aTouchArrayIndex,
                unsigned long touchId, octet surfaceId,
                Float32Array position, Float32Array? surfaceDimension);
};
