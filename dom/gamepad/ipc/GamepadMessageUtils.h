/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_gamepad_GamepadMessageUtils_h
#define mozilla_dom_gamepad_GamepadMessageUtils_h

#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/GamepadBinding.h"
#include "mozilla/dom/GamepadHandle.h"
#include "mozilla/dom/GamepadLightIndicatorBinding.h"
#include "mozilla/dom/GamepadPoseState.h"
#include "mozilla/dom/GamepadTouchState.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::GamepadLightIndicatorType>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::GamepadLightIndicatorType> {};

template <>
struct ParamTraits<mozilla::dom::GamepadMappingType>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::GamepadMappingType> {};

template <>
struct ParamTraits<mozilla::dom::GamepadHand>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::GamepadHand> {};

template <>
struct ParamTraits<mozilla::dom::GamepadCapabilityFlags>
    : public BitFlagsEnumSerializer<
          mozilla::dom::GamepadCapabilityFlags,
          mozilla::dom::GamepadCapabilityFlags::Cap_All> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::dom::GamepadPoseState, flags,
                                  orientation, position, angularVelocity,
                                  angularAcceleration, linearVelocity,
                                  linearAcceleration, isPositionValid,
                                  isOrientationValid);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::dom::GamepadTouchState, touchId,
                                  surfaceId, position, surfaceDimensions,
                                  isSurfaceDimensionsValid);

template <>
struct ParamTraits<mozilla::dom::GamepadHandleKind>
    : public ContiguousEnumSerializerInclusive<
          mozilla::dom::GamepadHandleKind,
          mozilla::dom::GamepadHandleKind::GamepadPlatformManager,
          mozilla::dom::GamepadHandleKind::VR> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::dom::GamepadHandle, mValue, mKind);

}  // namespace IPC

#endif  // mozilla_dom_gamepad_GamepadMessageUtils_h
