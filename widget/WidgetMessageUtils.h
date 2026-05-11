/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_WidgetMessageUtils_h
#define mozilla_WidgetMessageUtils_h

#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/DimensionRequest.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/widget/ThemeChangeKind.h"
#include "nsIClipboard.h"
#include "nsIWidget.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::widget::ThemeChangeKind>
    : public BitFlagsEnumSerializer<mozilla::widget::ThemeChangeKind,
                                    mozilla::widget::ThemeChangeKind::AllBits> {
};

template <>
struct ParamTraits<mozilla::HapticFeedbackType>
    : public ContiguousEnumSerializer<mozilla::HapticFeedbackType,
                                      mozilla::HapticFeedbackType(0),
                                      mozilla::HapticFeedbackType::End> {
  using IdType = std::underlying_type_t<mozilla::HapticFeedbackType>;
};

template <>
struct ParamTraits<mozilla::LookAndFeel::IntID>
    : ContiguousEnumSerializer<mozilla::LookAndFeel::IntID,
                               mozilla::LookAndFeel::IntID(0),
                               mozilla::LookAndFeel::IntID::End> {
  using IdType = std::underlying_type_t<mozilla::LookAndFeel::IntID>;
};

template <>
struct ParamTraits<mozilla::LookAndFeel::ColorID>
    : ContiguousEnumSerializer<mozilla::LookAndFeel::ColorID,
                               mozilla::LookAndFeel::ColorID(0),
                               mozilla::LookAndFeel::ColorID::End> {
  using IdType = std::underlying_type_t<mozilla::LookAndFeel::ColorID>;
};

template <>
struct ParamTraits<mozilla::ColorScheme>
    : ContiguousEnumSerializerInclusive<mozilla::ColorScheme,
                                        mozilla::ColorScheme::Light,
                                        mozilla::ColorScheme::Dark> {};

template <>
struct ParamTraits<mozilla::widget::TransparencyMode>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::TransparencyMode,
          mozilla::widget::TransparencyMode::Opaque,
          mozilla::widget::TransparencyMode::Transparent> {};

template <>
struct ParamTraits<nsIWidget::NativePointerLockMode>
    : ContiguousEnumSerializerInclusive<
          nsIWidget::NativePointerLockMode,
          nsIWidget::NativePointerLockMode::Regular,
          nsIWidget::NativePointerLockMode::Unadjusted> {};

template <>
struct ParamTraits<nsCursor>
    : ContiguousEnumSerializer<nsCursor, eCursor_standard, eCursorCount> {};

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND
    ParamTraits<nsIWidget::TouchpadGesturePhase>
    : ContiguousEnumSerializerInclusive<
          nsIWidget::TouchpadGesturePhase,
          nsIWidget::TouchpadGesturePhase::PHASE_BEGIN,
          nsIWidget::TouchpadGesturePhase::PHASE_END> {};

template <>
struct ParamTraits<TouchPointerState>
    : public BitFlagsEnumSerializer<TouchPointerState,
                                    TouchPointerState::ALL_BITS> {};

template <>
struct ParamTraits<mozilla::DimensionKind>
    : public ContiguousEnumSerializerInclusive<mozilla::DimensionKind,
                                               mozilla::DimensionKind::Inner,
                                               mozilla::DimensionKind::Outer> {
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::DimensionRequest, mDimensionKind, mX,
                                  mY, mWidth, mHeight);

template <>
struct ParamTraits<nsIClipboard::ClipboardType>
    : public ContiguousEnumSerializerInclusive<
          nsIClipboard::ClipboardType, nsIClipboard::kSelectionClipboard,
          nsIClipboard::kSelectionCache> {};

template <>
struct ParamTraits<mozilla::NativeKeyBindingsType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::NativeKeyBindingsType,
          mozilla::NativeKeyBindingsType::SingleLineEditor,
          mozilla::kHighestNativeKeyBindingsType> {};

}  // namespace IPC

#endif  // WidgetMessageUtils_h
