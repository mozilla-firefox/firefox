/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_GamepadRemapping_h_
#define mozilla_dom_GamepadRemapping_h_

#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadLightIndicator.h"
#include "mozilla/dom/GamepadPoseState.h"
#include "mozilla/dom/GamepadTouchState.h"

namespace mozilla::dom {

// GamepadId is (vendorId << 16) | productId)
enum class GamepadId : uint32_t {
  // Nexus Player Controller
  kAsusTekProduct4500 = 0x0b054500,
  // 2Axes 8Keys Game Pad
  kDragonRiseProduct0011 = 0x00790011,
  // ADT-1 Controller
  kGoogleProduct2c40 = 0x18d12c40,
  // Stadia Controller
  kGoogleProduct9400 = 0x18d19400,
  // Logitech F310, D-mode
  kLogitechProductc216 = 0x046dc216,
  // Logitech F510, D-mode
  kLogitechProductc218 = 0x046dc218,
  // Logitech F710, D-mode
  kLogitechProductc219 = 0x046dc219,
  // Microsoft Xbox 360
  kMicrosoftProductXbox360 = 0x045e028e,
  // Microsoft Xbox 360 Wireless
  kMicrosoftProductXbox360Wireless = 0x045e028f,
  // Microsoft Xbox 360 Wireless
  kMicrosoftProductXbox360Wireless2 = 0x045e0719,
  // Microsoft Xbox One 2013
  kMicrosoftProductXbox2013 = 0x045e02d1,
  // Microsoft Xbox One (2015 FW)
  kMicrosoftProductXbox2015 = 0x045e02dd,
  // Microsoft Xbox One S
  kMicrosoftProductXboxOneS = 0x045e02ea,
  // Microsoft Xbox One S Wireless
  kMicrosoftProductXboxOneSWireless = 0x045e02e0,
  // Microsoft Xbox One Elite
  kMicrosoftProductXboxOneElite = 0x045e02e3,
  // Microsoft Xbox One Elite 2
  kMicrosoftProductXboxOneElite2 = 0x045e0b00,
  // Microsoft Xbox One Elite 2 Wireless
  kMicrosoftProductXboxOneElite2Wireless = 0x045e0b05,
  // Xbox One S Wireless (2016 FW)
  kMicrosoftProductXboxOneSWireless2016 = 0x045e02fd,
  // Microsoft Xbox Adaptive
  kMicrosoftProductXboxAdaptive = 0x045e0b0a,
  // Microsoft Xbox Adaptive Wireless
  kMicrosoftProductXboxAdaptiveWireless = 0x045e0b0c,
  // Microsoft Xbox Series X Wireless
  kMicrosoftProductXboxSeriesXWireless = 0x045e0b13,
  // Switch Joy-Con L
  kNintendoProduct2006 = 0x057e2006,
  // Switch Joy-Con R
  kNintendoProduct2007 = 0x057e2007,
  // Switch Pro Controller
  kNintendoProduct2009 = 0x057e2009,
  // Switch Charging Grip
  kNintendoProduct200e = 0x057e200e,
  // Nvidia Shield gamepad (2015)
  kNvidiaProduct7210 = 0x09557210,
  // Nvidia Shield gamepad (2017)
  kNvidiaProduct7214 = 0x09557214,
  // iBuffalo Classic
  kPadixProduct2060 = 0x05832060,
  // XSkills Gamecube USB adapter
  kPlayComProduct0005 = 0x0b430005,
  // boom PSX+N64 USB Converter
  kPrototypeVendorProduct0667 = 0x66660667,
  // Analog game controller
  kPrototypeVendorProduct9401 = 0x66669401,
  // Razer Serval Controller
  kRazer1532Product0900 = 0x15320900,
  // Playstation 3 Controller
  kSonyProduct0268 = 0x054c0268,
  // Playstation Dualshock 4
  kSonyProduct05c4 = 0x054c05c4,
  // Dualshock 4 (PS4 Slim)
  kSonyProduct09cc = 0x054c09cc,
  // Dualshock 4 USB receiver
  kSonyProduct0ba0 = 0x054c0ba0,
  // DualSense
  kSonyProduct0ce6 = 0x054c0ce6,
  // DualSense Edge
  kSonyProduct0df2 = 0x054c0df2,
  // Moga Pro Controller (HID mode)
  kVendor20d6Product6271 = 0x20d66271,
  // OnLive Controller (Bluetooth)
  kVendor2378Product1008 = 0x23781008,
  // OnLive Controller (Wired)
  kVendor2378Product100a = 0x2378100a,
  // OUYA Controller
  kVendor2836Product0001 = 0x28360001,
};

// Follow the canonical ordering recommendation for the "Standard Gamepad"
// from https://www.w3.org/TR/gamepad/#remapping.
enum CanonicalButtonIndex {
  BUTTON_INDEX_PRIMARY,
  BUTTON_INDEX_SECONDARY,
  BUTTON_INDEX_TERTIARY,
  BUTTON_INDEX_QUATERNARY,
  BUTTON_INDEX_LEFT_SHOULDER,
  BUTTON_INDEX_RIGHT_SHOULDER,
  BUTTON_INDEX_LEFT_TRIGGER,
  BUTTON_INDEX_RIGHT_TRIGGER,
  BUTTON_INDEX_BACK_SELECT,
  BUTTON_INDEX_START,
  BUTTON_INDEX_LEFT_THUMBSTICK,
  BUTTON_INDEX_RIGHT_THUMBSTICK,
  BUTTON_INDEX_DPAD_UP,
  BUTTON_INDEX_DPAD_DOWN,
  BUTTON_INDEX_DPAD_LEFT,
  BUTTON_INDEX_DPAD_RIGHT,
  BUTTON_INDEX_META,
  BUTTON_INDEX_COUNT
};

enum CanonicalAxisIndex {
  AXIS_INDEX_LEFT_STICK_X,
  AXIS_INDEX_LEFT_STICK_Y,
  AXIS_INDEX_RIGHT_STICK_X,
  AXIS_INDEX_RIGHT_STICK_Y,
  AXIS_INDEX_COUNT
};

const float BUTTON_THRESHOLD_VALUE = 0.1f;

static inline bool AxisNegativeAsButton(double input) { return input < -0.5; }

static inline bool AxisPositiveAsButton(double input) { return input > 0.5; }

static inline double AxisToButtonValue(double aValue) {
  // Mapping axis value range from (-1, +1) to (0, +1).
  return (aValue + 1.0f) * 0.5f;
}

class GamepadRemapper {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GamepadRemapper)

 public:
  virtual uint32_t GetAxisCount() const = 0;
  virtual uint32_t GetButtonCount() const = 0;
  virtual uint32_t GetLightIndicatorCount() const { return 0; }
  virtual void GetLightIndicators(
      nsTArray<GamepadLightIndicatorType>& aTypes) const {}
  virtual uint32_t GetTouchEventCount() const { return 0; }
  virtual void GetLightColorReport(uint8_t aRed, uint8_t aGreen, uint8_t aBlue,
                                   std::vector<uint8_t>& aReport) const {}
  virtual uint32_t GetMaxInputReportLength() const { return 0; }

  virtual void SetAxisCount(uint32_t aButtonCount) {}
  virtual void SetButtonCount(uint32_t aButtonCount) {}
  virtual GamepadMappingType GetMappingType() const {
    return GamepadMappingType::Standard;
  }
  virtual void ProcessTouchData(GamepadHandle aHandle, const uint8_t* aInput,
                                size_t aInputLen) {}
  virtual void RemapAxisMoveEvent(GamepadHandle aHandle, uint32_t aAxis,
                                  double aValue) const = 0;
  virtual void RemapButtonEvent(GamepadHandle aHandle, uint32_t aButton,
                                bool aPressed) const = 0;

 protected:
  GamepadRemapper() = default;
  virtual ~GamepadRemapper() = default;
};

struct GamepadRemappingData {
  GamepadId id;
  RefPtr<GamepadRemapper> remapping;
};

already_AddRefed<GamepadRemapper> GetGamepadRemapper(const uint16_t aVendorId,
                                                     const uint16_t aProductId,
                                                     bool& aUsingDefault);

}  // namespace mozilla::dom

#endif  // mozilla_dom_GamepadRemapping_h_
