/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGUIEventIPC_h_
#define nsGUIEventIPC_h_

#include <fmt/format.h>

#include "InputData.h"
#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/ContentCache.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/WheelHandlingHelper.h"  // for WheelDeltaAdjustmentStrategy
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/ipc/URIUtils.h"  // for ParamTraits<nsIURI*>
#include "mozilla/layers/LayersMessageUtils.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::EventMessage>
    : public ContiguousEnumSerializer<
          mozilla::EventMessage, mozilla::EventMessage(0),
          mozilla::EventMessage::eEventMessage_MaxValue> {};

template <>
struct ParamTraits<mozilla::EventClassID>
    : public ContiguousEnumSerializer<
          mozilla::EventClassID, mozilla::EventClassID(0),
          mozilla::EventClassID::eEventClassUninitialized> {};

template <>
struct ParamTraits<mozilla::BaseEventFlags> {
  using paramType = mozilla::BaseEventFlags;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    aWriter->WriteBytes(&aParam, sizeof(aParam));
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return aReader->ReadBytesInto(aResult, sizeof(*aResult));
  }
};

template <>
struct ParamTraits<mozilla::WidgetEvent> {
  using paramType = mozilla::WidgetEvent;

  static void WriteForDerivedClass(MessageWriter* aWriter,
                                   const paramType& aParam) {
    // Mark the event as posted to another process.
    const_cast<mozilla::WidgetEvent&>(aParam).MarkAsPostedToRemoteProcess();

    WriteParam(aWriter, aParam.mClass);
    WriteParam(aWriter, aParam.mMessage);
    WriteParam(aWriter, aParam.mRefPoint);
    WriteParam(aWriter, aParam.mFocusSequenceNumber);
    WriteParam(aWriter, aParam.mTimeStamp);
    WriteParam(aWriter, aParam.mFlags);
    WriteParam(aWriter, aParam.mLayersId);
  }

  static bool ReadForDerivedClass(MessageReader* aReader,
                                  mozilla::EventClassID aExpectedEventClassID,
                                  paramType* aResult) {
    MOZ_ASSERT(aExpectedEventClassID != mozilla::eEventClassUninitialized);
    if (!Read(aReader, aResult)) [[unlikely]] {
      return false;
    }
    NS_WARNING_ASSERTION(
        aResult->mClass == aExpectedEventClassID,
        fmt::format(
            "Wrong event class ID: expected {}, but got {} (message: {})",
            ToChar(aExpectedEventClassID), ToChar(aResult->mClass),
            ToChar(aResult->mMessage))
            .c_str());
    if (aResult->mClass == aExpectedEventClassID &&
        mozilla::IsValidMessageForIPC(aResult->mMessage, aExpectedEventClassID))
        [[likely]] {
      return true;
    }
    // Clear mClass value to avoid the assertion failure in the destructor in
    // the debug build because it's not a fault in this process.
    aResult->mClass = mozilla::eEventClassUninitialized;
    // Don't allow illegal mClass/mMessage combination.
    return false;
  }

 private:
  static bool Read(MessageReader* aReader, paramType* aResult) {
    const bool ret = ReadParam(aReader, &aResult->mClass) &&
                     ReadParam(aReader, &aResult->mMessage) &&
                     ReadParam(aReader, &aResult->mRefPoint) &&
                     ReadParam(aReader, &aResult->mFocusSequenceNumber) &&
                     ReadParam(aReader, &aResult->mTimeStamp) &&
                     ReadParam(aReader, &aResult->mFlags) &&
                     ReadParam(aReader, &aResult->mLayersId);
    if (ret) {
      // Reset cross process dispatching state here because the event has not
      // been dispatched to different process from current process.
      aResult->ResetCrossProcessDispatchingState();
      // Mark the event comes from another process.
      aResult->MarkAsComingFromAnotherProcess();
    }
    return ret;
  }
};

template <>
struct ParamTraits<mozilla::WidgetGUIEvent> {
  using paramType = mozilla::WidgetGUIEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetEvent>;

  static void WriteForDerivedClass(MessageWriter* aWriter,
                                   const paramType& aParam) {
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
  }

  static bool ReadForDerivedClass(MessageReader* aReader,
                                  mozilla::EventClassID aExpectedEventClassID,
                                  paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(aReader, aExpectedEventClassID,
                                                aResult);
  }
};

template <>
struct ParamTraits<mozilla::WidgetInputEvent> {
  using paramType = mozilla::WidgetInputEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetGUIEvent>;

  static void WriteForDerivedClass(MessageWriter* aWriter,
                                   const paramType& aParam) {
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mModifiers);
  }

  static bool ReadForDerivedClass(MessageReader* aReader,
                                  mozilla::EventClassID aExpectedEventClassID,
                                  paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(aReader, aExpectedEventClassID,
                                                aResult) &&
           ReadParam(aReader, &aResult->mModifiers);
  }
};

template <>
struct ParamTraits<mozilla::WidgetMouseEventBase> {
  using paramType = mozilla::WidgetMouseEventBase;
  using baseParamTraits = ParamTraits<mozilla::WidgetInputEvent>;

  static void WriteForDerivedClass(MessageWriter* aWriter,
                                   const paramType& aParam) {
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mButton);
    WriteParam(aWriter, aParam.mButtons);
    WriteParam(aWriter, aParam.mPressure);
    WriteParam(aWriter, aParam.mInputSource);
  }

  static bool ReadForDerivedClass(MessageReader* aReader,
                                  mozilla::EventClassID aExpectedEventClassID,
                                  paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(aReader, aExpectedEventClassID,
                                                aResult) &&
           ReadParam(aReader, &aResult->mButton) &&
           ReadParam(aReader, &aResult->mButtons) &&
           ReadParam(aReader, &aResult->mPressure) &&
           ReadParam(aReader, &aResult->mInputSource);
  }
};

template <>
struct ParamTraits<mozilla::WidgetWheelEvent::ScrollType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::WidgetWheelEvent::ScrollType,
          mozilla::WidgetWheelEvent::SCROLL_DEFAULT,
          mozilla::WidgetWheelEvent::SCROLL_SMOOTHLY> {};

template <>
struct ParamTraits<mozilla::WidgetWheelEvent> {
  using paramType = mozilla::WidgetWheelEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetMouseEventBase>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::eWheelEventClass,
                         fmt::format("got {}, but expected eWheelEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eWheelEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mDeltaX);
    WriteParam(aWriter, aParam.mDeltaY);
    WriteParam(aWriter, aParam.mDeltaZ);
    WriteParam(aWriter, aParam.mDeltaMode);
    WriteParam(aWriter, aParam.mWheelTicksX);
    WriteParam(aWriter, aParam.mWheelTicksY);
    WriteParam(aWriter, aParam.mCustomizedByUserPrefs);
    WriteParam(aWriter, aParam.mMayHaveMomentum);
    WriteParam(aWriter, aParam.mIsMomentum);
    WriteParam(aWriter, aParam.mIsNoLineOrPageDelta);
    WriteParam(aWriter, aParam.mLineOrPageDeltaX);
    WriteParam(aWriter, aParam.mLineOrPageDeltaY);
    WriteParam(aWriter, aParam.mScrollType);
    WriteParam(aWriter, aParam.mOverflowDeltaX);
    WriteParam(aWriter, aParam.mOverflowDeltaY);
    WriteParam(aWriter, aParam.mViewPortIsOverscrolled);
    WriteParam(aWriter, aParam.mCanTriggerSwipe);
    WriteParam(aWriter, aParam.mAllowToOverrideSystemScrollSpeed);
    WriteParam(aWriter, aParam.mDeltaValuesHorizontalizedForDefaultHandler);
    WriteParam(aWriter, aParam.mCallbackId);

    // Mark the event as stopped to notify callback.
    const_cast<mozilla::WidgetWheelEvent&>(aParam).mCallbackId.reset();
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(
               aReader, mozilla::eWheelEventClass, aResult) &&
           ReadParam(aReader, &aResult->mDeltaX) &&
           ReadParam(aReader, &aResult->mDeltaY) &&
           ReadParam(aReader, &aResult->mDeltaZ) &&
           ReadParam(aReader, &aResult->mDeltaMode) &&
           ReadParam(aReader, &aResult->mWheelTicksX) &&
           ReadParam(aReader, &aResult->mWheelTicksY) &&
           ReadParam(aReader, &aResult->mCustomizedByUserPrefs) &&
           ReadParam(aReader, &aResult->mMayHaveMomentum) &&
           ReadParam(aReader, &aResult->mIsMomentum) &&
           ReadParam(aReader, &aResult->mIsNoLineOrPageDelta) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaX) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaY) &&
           ReadParam(aReader, &aResult->mScrollType) &&
           ReadParam(aReader, &aResult->mOverflowDeltaX) &&
           ReadParam(aReader, &aResult->mOverflowDeltaY) &&
           ReadParam(aReader, &aResult->mViewPortIsOverscrolled) &&
           ReadParam(aReader, &aResult->mCanTriggerSwipe) &&
           ReadParam(aReader, &aResult->mAllowToOverrideSystemScrollSpeed) &&
           ReadParam(aReader,
                     &aResult->mDeltaValuesHorizontalizedForDefaultHandler) &&
           ReadParam(aReader, &aResult->mCallbackId);
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::WidgetPointerHelper::Tilt, mX, mY);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::WidgetPointerHelper::Angle,
                                  mAltitude, mAzimuth);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::WidgetPointerHelper, pointerId, mTilt, twist, tangentialPressure
    // We don't serialize convertToPointer since it's temporarily variable and
    // should be reset to default.
);

template <>
struct ParamTraits<mozilla::WidgetMouseEvent::Reason>
    : public ContiguousEnumSerializerInclusive<
          mozilla::WidgetMouseEvent::Reason, mozilla::WidgetMouseEvent::eReal,
          mozilla::WidgetMouseEvent::eSynthesized> {};

template <>
struct ParamTraits<mozilla::WidgetMouseEvent::ContextMenuTrigger>
    : public ContiguousEnumSerializerInclusive<
          mozilla::WidgetMouseEvent::ContextMenuTrigger,
          mozilla::WidgetMouseEvent::eNormal,
          mozilla::WidgetMouseEvent::eControlClick> {};

template <>
struct ParamTraits<mozilla::WidgetMouseEvent::ExitFrom>
    : public ContiguousEnumSerializerInclusive<
          mozilla::WidgetMouseEvent::ExitFrom,
          mozilla::WidgetMouseEvent::ePlatformChild,
          mozilla::WidgetMouseEvent::ePuppetParentToPuppetChild> {};

template <>
struct ParamTraits<mozilla::WidgetMouseEvent> {
  using paramType = mozilla::WidgetMouseEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetMouseEventBase>;

  // We don't need to copy the following members:
  // - mIgnoreCapturingContent: When this is `true`, the remote process should
  //   not be capturing the pointer because this is used to dispatch boundary
  //   events outside the capturing element after handling ePointerUp/eMouseUp.
  // - mSynthesizeMoveAfterDispatch: When this is `true`, the event needs to
  //   synthesize a move event to dispatch corresponding boundary events.
  //   However, when a remote content is under the pointer, it should occur
  //   before dispatching this event in the remote process, but there is no
  //   path to do that.  Therefore, this flag is not required for now.

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::eMouseEventClass,
                         fmt::format("got {}, but expected eMouseEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eMouseEventClass);
    WriteForDerivedClass(aWriter, aParam);
  }

  static void WriteForDerivedClass(MessageWriter* aWriter,
                                   const paramType& aParam) {
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter,
               static_cast<const mozilla::WidgetPointerHelper&>(aParam));
    WriteParam(aWriter, aParam.mIgnoreRootScrollFrame);
    WriteParam(aWriter, aParam.mClickEventPrevented);
    WriteParam(aWriter, aParam.mReason);
    WriteParam(aWriter, aParam.mContextMenuTrigger);
    WriteParam(aWriter, aParam.mExitFrom);
    WriteParam(aWriter, aParam.mClickCount);
    WriteParam(aWriter, aParam.mCallbackId);
    WriteParam(aWriter, aParam.mMovement);

    // Mark the event as stopped to notify callback.
    const_cast<mozilla::WidgetMouseEvent&>(aParam).mCallbackId.reset();
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadForDerivedClass(aReader, mozilla::eMouseEventClass, aResult);
  }

  static bool ReadForDerivedClass(MessageReader* aReader,
                                  mozilla::EventClassID aExpectedEventClassID,
                                  paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(aReader, aExpectedEventClassID,
                                                aResult) &&
           ReadParam(aReader,
                     static_cast<mozilla::WidgetPointerHelper*>(aResult)) &&
           ReadParam(aReader, &aResult->mIgnoreRootScrollFrame) &&
           ReadParam(aReader, &aResult->mClickEventPrevented) &&
           ReadParam(aReader, &aResult->mReason) &&
           ReadParam(aReader, &aResult->mContextMenuTrigger) &&
           ReadParam(aReader, &aResult->mExitFrom) &&
           ReadParam(aReader, &aResult->mClickCount) &&
           ReadParam(aReader, &aResult->mCallbackId) &&
           ReadParam(aReader, &aResult->mMovement);
  }
};

template <>
struct ParamTraits<mozilla::WidgetDragEvent> {
  using paramType = mozilla::WidgetDragEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetMouseEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::eDragEventClass,
                         fmt::format("got {}, but expected eDragEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eDragEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mUserCancelled);
    WriteParam(aWriter, aParam.mDefaultPreventedOnContent);
    WriteParam(aWriter, aParam.mInHTMLEditorEventListener);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(
               aReader, mozilla::eDragEventClass, aResult) &&
           ReadParam(aReader, &aResult->mUserCancelled) &&
           ReadParam(aReader, &aResult->mDefaultPreventedOnContent) &&
           ReadParam(aReader, &aResult->mInHTMLEditorEventListener);
  }
};

template <>
struct ParamTraits<mozilla::WidgetPointerEvent> {
  using paramType = mozilla::WidgetPointerEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetMouseEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::ePointerEventClass,
                         fmt::format("got {}, but expected ePointerEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::ePointerEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mWidth);
    WriteParam(aWriter, aParam.mHeight);
    WriteParam(aWriter, aParam.mIsPrimary);
    WriteParam(aWriter, aParam.mFromTouchEvent);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(
               aReader, mozilla::ePointerEventClass, aResult) &&
           ReadParam(aReader, &aResult->mWidth) &&
           ReadParam(aReader, &aResult->mHeight) &&
           ReadParam(aReader, &aResult->mIsPrimary) &&
           ReadParam(aReader, &aResult->mFromTouchEvent);
  }
};

template <>
struct ParamTraits<mozilla::WidgetTouchEvent> {
  using paramType = mozilla::WidgetTouchEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetInputEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::eTouchEventClass,
                         fmt::format("got {}, but expected eTouchEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eTouchEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mInputSource);
    WriteParam(aWriter, aParam.mButton);
    WriteParam(aWriter, aParam.mButtons);
    WriteParam(aWriter, aParam.mCallbackId);
    // Mark the event as stopped to notify callback.
    const_cast<mozilla::WidgetTouchEvent&>(aParam).mCallbackId.reset();
    // Sigh, Touch bites us again!  We want to be able to do
    //   WriteParam(aWriter, aParam.mTouches);
    const paramType::TouchArray& touches = aParam.mTouches;
    WriteParam(aWriter, touches.Length());
    for (uint32_t i = 0; i < touches.Length(); ++i) {
      mozilla::dom::Touch* touch = touches[i];
      WriteParam(aWriter, touch->mIdentifier);
      WriteParam(aWriter, touch->mRefPoint);
      WriteParam(aWriter, touch->mRadius);
      WriteParam(aWriter, touch->mRotationAngle);
      WriteParam(aWriter, touch->mForce);
      WriteParam(aWriter, touch->mTilt);
      WriteParam(aWriter, touch->twist);
      WriteParam(aWriter, touch->mAngle);
    }
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    paramType::TouchArray::size_type numTouches;
    if (!baseParamTraits::ReadForDerivedClass(
            aReader, mozilla::eTouchEventClass, aResult) ||
        !ReadParam(aReader, &aResult->mInputSource) ||
        !ReadParam(aReader, &aResult->mButton) ||
        !ReadParam(aReader, &aResult->mButtons) ||
        !ReadParam(aReader, &aResult->mCallbackId) ||
        !ReadParam(aReader, &numTouches)) {
      return false;
    }
    for (uint32_t i = 0; i < numTouches; ++i) {
      int32_t identifier;
      mozilla::LayoutDeviceIntPoint refPoint;
      mozilla::LayoutDeviceIntPoint radius;
      float rotationAngle;
      float force;
      mozilla::Maybe<mozilla::WidgetPointerHelper::Tilt> tilt;
      int32_t twist;
      mozilla::Maybe<mozilla::WidgetPointerHelper::Angle> angle;
      if (!ReadParam(aReader, &identifier) || !ReadParam(aReader, &refPoint) ||
          !ReadParam(aReader, &radius) || !ReadParam(aReader, &rotationAngle) ||
          !ReadParam(aReader, &force) || !ReadParam(aReader, &tilt) ||
          !ReadParam(aReader, &twist) || !ReadParam(aReader, &angle)) {
        return false;
      }
      auto* touch = new mozilla::dom::Touch(identifier, refPoint, radius,
                                            rotationAngle, force);
      touch->mTilt = std::move(tilt);
      touch->twist = twist;
      touch->mAngle = std::move(angle);
      aResult->mTouches.AppendElement(touch);
    }
    return true;
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::AlternativeCharCode,
                                  mUnshiftedCharCode, mShiftedCharCode);

template <>
struct ParamTraits<mozilla::ShortcutKeyCandidate::ShiftState>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ShortcutKeyCandidate::ShiftState,
          mozilla::ShortcutKeyCandidate::ShiftState::Ignorable,
          mozilla::ShortcutKeyCandidate::ShiftState::MatchExactly> {};

template <>
struct ParamTraits<mozilla::ShortcutKeyCandidate::SkipIfEarlierHandlerDisabled>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ShortcutKeyCandidate::SkipIfEarlierHandlerDisabled,
          mozilla::ShortcutKeyCandidate::SkipIfEarlierHandlerDisabled::No,
          mozilla::ShortcutKeyCandidate::SkipIfEarlierHandlerDisabled::Yes> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ShortcutKeyCandidate, mCharCode,
                                  mShiftState, mSkipIfEarlierHandlerDisabled);

template <>
struct ParamTraits<mozilla::KeyNameIndex>
    : public ContiguousEnumSerializerInclusive<
          mozilla::KeyNameIndex, static_cast<mozilla::KeyNameIndex>(0),
          mozilla::KeyNameIndex::KEY_NAME_INDEX_USE_STRING> {};

template <>
struct ParamTraits<mozilla::CodeNameIndex>
    : public ContiguousEnumSerializerInclusive<
          mozilla::CodeNameIndex, static_cast<mozilla::CodeNameIndex>(0),
          mozilla::CodeNameIndex::CODE_NAME_INDEX_USE_STRING> {};

template <>
struct ParamTraits<mozilla::WidgetKeyboardEvent> {
  using paramType = mozilla::WidgetKeyboardEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetInputEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(aParam.mClass == mozilla::eKeyboardEventClass,
                         fmt::format("got {}, but expected eKeyboardEventClass",
                                     mozilla::ToChar(aParam.mClass))
                             .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eKeyboardEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mKeyNameIndex);
    WriteParam(aWriter, aParam.mCodeNameIndex);
    WriteParam(aWriter, aParam.mKeyValue);
    WriteParam(aWriter, aParam.mCodeValue);
    WriteParam(aWriter, aParam.mKeyCode);
    WriteParam(aWriter, aParam.mCharCode);
    WriteParam(aWriter, aParam.mPseudoCharCode);
    WriteParam(aWriter, aParam.mAlternativeCharCodes);
    WriteParam(aWriter, aParam.mIsRepeat);
    WriteParam(aWriter, aParam.mLocation);
    WriteParam(aWriter, aParam.mUniqueId);
    WriteParam(aWriter, aParam.mIsSynthesizedByTIP);
    WriteParam(aWriter, aParam.mMaybeSkippableInRemoteProcess);
    WriteParam(aWriter, aParam.mRelevantCommand);

    // An OS-specific native event might be attached in |mNativeKeyEvent|,  but
    // that cannot be copied across process boundaries.

    WriteParam(aWriter, aParam.mEditCommandsForSingleLineEditor);
    WriteParam(aWriter, aParam.mEditCommandsForMultiLineEditor);
    WriteParam(aWriter, aParam.mEditCommandsForRichTextEditor);
    WriteParam(aWriter, aParam.mEditCommandsForSingleLineEditorInitialized);
    WriteParam(aWriter, aParam.mEditCommandsForMultiLineEditorInitialized);
    WriteParam(aWriter, aParam.mEditCommandsForRichTextEditorInitialized);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    if (baseParamTraits::ReadForDerivedClass(
            aReader, mozilla::eKeyboardEventClass, aResult) &&
        ReadParam(aReader, &aResult->mKeyNameIndex) &&
        ReadParam(aReader, &aResult->mCodeNameIndex) &&
        ReadParam(aReader, &aResult->mKeyValue) &&
        ReadParam(aReader, &aResult->mCodeValue) &&
        ReadParam(aReader, &aResult->mKeyCode) &&
        ReadParam(aReader, &aResult->mCharCode) &&
        ReadParam(aReader, &aResult->mPseudoCharCode) &&
        ReadParam(aReader, &aResult->mAlternativeCharCodes) &&
        ReadParam(aReader, &aResult->mIsRepeat) &&
        ReadParam(aReader, &aResult->mLocation) &&
        ReadParam(aReader, &aResult->mUniqueId) &&
        ReadParam(aReader, &aResult->mIsSynthesizedByTIP) &&
        ReadParam(aReader, &aResult->mMaybeSkippableInRemoteProcess) &&
        ReadParam(aReader, &aResult->mRelevantCommand) &&
        ReadParam(aReader, &aResult->mEditCommandsForSingleLineEditor) &&
        ReadParam(aReader, &aResult->mEditCommandsForMultiLineEditor) &&
        ReadParam(aReader, &aResult->mEditCommandsForRichTextEditor) &&
        ReadParam(aReader,
                  &aResult->mEditCommandsForSingleLineEditorInitialized) &&
        ReadParam(aReader,
                  &aResult->mEditCommandsForMultiLineEditorInitialized) &&
        ReadParam(aReader,
                  &aResult->mEditCommandsForRichTextEditorInitialized)) {
      aResult->mNativeKeyEvent = nullptr;
      return true;
    }
    return false;
  }
};

template <>
struct ParamTraits<mozilla::TextRangeStyle::LineStyle>
    : ContiguousEnumSerializerInclusive<
          mozilla::TextRangeStyle::LineStyle,
          mozilla::TextRangeStyle::LineStyle::None,
          mozilla::TextRangeStyle::LineStyle::Wavy> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::TextRangeStyle, mDefinedStyles,
                                  mLineStyle, mIsBoldLine, mForegroundColor,
                                  mBackgroundColor, mUnderlineColor);

template <>
struct ParamTraits<mozilla::TextRangeType>
    : ContiguousEnumSerializerInclusive<
          mozilla::TextRangeType, mozilla::TextRangeType::eUninitialized,
          mozilla::TextRangeType::eSelectedClause> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::TextRange, mStartOffset, mEndOffset,
                                  mRangeType, mRangeStyle);

template <>
struct ParamTraits<mozilla::TextRangeArray>
    : ParamTraits<AutoTArray<mozilla::TextRange, 10>> {};

template <>
struct ParamTraits<mozilla::WidgetCompositionEvent> {
  using paramType = mozilla::WidgetCompositionEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetGUIEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(
        aParam.mClass == mozilla::eCompositionEventClass,
        fmt::format("got {}, but expected eCompositionEventClass",
                    mozilla::ToChar(aParam.mClass))
            .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eCompositionEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mData);
    WriteParam(aWriter, aParam.mNativeIMEContext);
    WriteParam(aWriter, aParam.mCompositionId);
    bool hasRanges = !!aParam.mRanges;
    WriteParam(aWriter, hasRanges);
    if (hasRanges) {
      WriteParam(aWriter, *aParam.mRanges.get());
    }
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    bool hasRanges;
    if (!baseParamTraits::ReadForDerivedClass(
            aReader, mozilla::eCompositionEventClass, aResult) ||
        !ReadParam(aReader, &aResult->mData) ||
        !ReadParam(aReader, &aResult->mNativeIMEContext) ||
        !ReadParam(aReader, &aResult->mCompositionId) ||
        !ReadParam(aReader, &hasRanges)) {
      return false;
    }

    if (!hasRanges) {
      aResult->mRanges = nullptr;
    } else {
      aResult->mRanges = new mozilla::TextRangeArray();
      if (!ReadParam(aReader, aResult->mRanges.get())) {
        return false;
      }
    }
    return true;
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::FontRange, mStartOffset, mFontName,
                                  mFontSize);

template <>
struct ParamTraits<mozilla::RangeDirection>
    : ContiguousEnumSerializerInclusive<mozilla::RangeDirection,
                                        mozilla::RangeDirection::Normal,
                                        mozilla::RangeDirection::Reversed> {};

template <>
struct ParamTraits<mozilla::ExpandToClusterBoundary>
    : ContiguousEnumSerializerInclusive<mozilla::ExpandToClusterBoundary,
                                        mozilla::ExpandToClusterBoundary::No,
                                        mozilla::ExpandToClusterBoundary::Yes> {
};

template <>
struct ParamTraits<mozilla::WidgetSelectionEvent> {
  using paramType = mozilla::WidgetSelectionEvent;
  using baseParamTraits = ParamTraits<mozilla::WidgetGUIEvent>;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    NS_WARNING_ASSERTION(
        aParam.mClass == mozilla::eSelectionEventClass,
        fmt::format("got {}, but expected eSelectionEventClass",
                    mozilla::ToChar(aParam.mClass))
            .c_str());
    MOZ_DIAGNOSTIC_ASSERT(aParam.mClass == mozilla::eSelectionEventClass);
    baseParamTraits::WriteForDerivedClass(aWriter, aParam);
    WriteParam(aWriter, aParam.mOffset);
    WriteParam(aWriter, aParam.mLength);
    WriteParam(aWriter, aParam.mDirection);
    WriteParam(aWriter, aParam.mExpandToClusterBoundary);
    WriteParam(aWriter, aParam.mSucceeded);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return baseParamTraits::ReadForDerivedClass(
               aReader, mozilla::eSelectionEventClass, aResult) &&
           ReadParam(aReader, &aResult->mOffset) &&
           ReadParam(aReader, &aResult->mLength) &&
           ReadParam(aReader, &aResult->mDirection) &&
           ReadParam(aReader, &aResult->mExpandToClusterBoundary) &&
           ReadParam(aReader, &aResult->mSucceeded);
  }
};

template <>
struct ParamTraits<mozilla::PreventSetSelection>
    : ContiguousEnumSerializerInclusive<mozilla::PreventSetSelection,
                                        mozilla::PreventSetSelection::No,
                                        mozilla::PreventSetSelection::Yes> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::widget::NativeIMEContext,
                                  mRawNativeIMEContext, mOriginProcessID);

template <>
struct ParamTraits<mozilla::widget::IMENotification::SelectionChangeDataBase> {
  using paramType = mozilla::widget::IMENotification::SelectionChangeDataBase;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    MOZ_RELEASE_ASSERT(aParam.mString);
    WriteParam(aWriter, aParam.mOffset);
    WriteParam(aWriter, *aParam.mString);
    WriteParam(aWriter, aParam.mWritingModeBits);
    WriteParam(aWriter, aParam.mIsInitialized);
    WriteParam(aWriter, aParam.mHasRange);
    WriteParam(aWriter, aParam.mReversed);
    WriteParam(aWriter, aParam.mCausedByComposition);
    WriteParam(aWriter, aParam.mCausedBySelectionEvent);
    WriteParam(aWriter, aParam.mOccurredDuringComposition);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    aResult->mString = new nsString();
    return ReadParam(aReader, &aResult->mOffset) &&
           ReadParam(aReader, aResult->mString) &&
           ReadParam(aReader, &aResult->mWritingModeBits) &&
           ReadParam(aReader, &aResult->mIsInitialized) &&
           ReadParam(aReader, &aResult->mHasRange) &&
           ReadParam(aReader, &aResult->mReversed) &&
           ReadParam(aReader, &aResult->mCausedByComposition) &&
           ReadParam(aReader, &aResult->mCausedBySelectionEvent) &&
           ReadParam(aReader, &aResult->mOccurredDuringComposition);
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::widget::IMENotification::TextChangeDataBase, mStartOffset,
    mRemovedEndOffset, mAddedEndOffset, mCausedOnlyByComposition,
    mIncludingChangesDuringComposition, mIncludingChangesWithoutComposition);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::widget::IMENotification::MouseButtonEventData, mEventMessage,
    mOffset, mCursorPos, mCharRect, mButton, mButtons, mModifiers);

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_MIN_MISMATCH
    ParamTraits<mozilla::widget::IMEMessage>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::IMEMessage,
          // FYI: mozilla::widget::NOTIFY_IME_OF_NOTHING is the actual lowest
          // value, but it shouldn't be set at crossing the process boundary
          // since it's odd to notify the process of "nothing happened".
          mozilla::widget::NOTIFY_IME_OF_FOCUS,
          mozilla::widget::REQUEST_TO_CANCEL_COMPOSITION> {};

template <>
struct ParamTraits<mozilla::widget::IMENotification> {
  using paramType = mozilla::widget::IMENotification;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mMessage);
    switch (aParam.mMessage) {
      case mozilla::widget::NOTIFY_IME_OF_SELECTION_CHANGE:
        WriteParam(aWriter, aParam.mSelectionChangeData);
        return;
      case mozilla::widget::NOTIFY_IME_OF_TEXT_CHANGE:
        WriteParam(aWriter, aParam.mTextChangeData);
        return;
      case mozilla::widget::NOTIFY_IME_OF_MOUSE_BUTTON_EVENT:
        WriteParam(aWriter, aParam.mMouseButtonEventData);
        return;
      default:
        return;
    }
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    if (!ReadParam(aReader, &aResult->mMessage)) {
      return false;
    }
    switch (aResult->mMessage) {
      case mozilla::widget::NOTIFY_IME_OF_NOTHING:
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE(
            "NOTIFY_IME_OF_NOTHING shouldn't cross the process boundary");
        return false;
      case mozilla::widget::NOTIFY_IME_OF_SELECTION_CHANGE:
        return ReadParam(aReader, &aResult->mSelectionChangeData);
      case mozilla::widget::NOTIFY_IME_OF_TEXT_CHANGE:
        return ReadParam(aReader, &aResult->mTextChangeData);
      case mozilla::widget::NOTIFY_IME_OF_MOUSE_BUTTON_EVENT:
        return ReadParam(aReader, &aResult->mMouseButtonEventData);
      case mozilla::widget::NOTIFY_IME_OF_FOCUS:
      case mozilla::widget::NOTIFY_IME_OF_BLUR:
      case mozilla::widget::NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED:
      case mozilla::widget::NOTIFY_IME_OF_POSITION_CHANGE:
      case mozilla::widget::REQUEST_TO_COMMIT_COMPOSITION:
      case mozilla::widget::REQUEST_TO_CANCEL_COMPOSITION:
        return true;
    }
    return false;
  }
};

template <>
struct ParamTraits<mozilla::widget::IMEEnabled>
    : ContiguousEnumSerializer<mozilla::widget::IMEEnabled,
                               mozilla::widget::IMEEnabled::Disabled,
                               mozilla::widget::IMEEnabled::Unknown> {};

template <>
struct ParamTraits<mozilla::widget::IMEState::Open>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::IMEState::Open,
          mozilla::widget::IMEState::Open::OPEN_STATE_NOT_SUPPORTED,
          mozilla::widget::IMEState::Open::CLOSED> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::widget::IMEState, mEnabled, mOpen);

template <>
struct ParamTraits<mozilla::widget::InputContext::Origin>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::InputContext::Origin,
          mozilla::widget::InputContext::Origin::ORIGIN_MAIN,
          mozilla::widget::InputContext::Origin::ORIGIN_CONTENT> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::widget::InputContext, mIMEState,
                                  mHTMLInputType, mHTMLInputMode, mActionHint,
                                  mAutocapitalize, mAutocorrect, mOrigin,
                                  mHasHandledUserInput, mInPrivateBrowsing,
                                  mURI);

template <>
struct ParamTraits<mozilla::widget::InputContextAction::Cause>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::InputContextAction::Cause,
          mozilla::widget::InputContextAction::Cause::CAUSE_UNKNOWN,
          mozilla::widget::InputContextAction::Cause::
              CAUSE_UNKNOWN_DURING_KEYBOARD_INPUT> {};

template <>
struct ParamTraits<mozilla::widget::InputContextAction::FocusChange>
    : ContiguousEnumSerializerInclusive<
          mozilla::widget::InputContextAction::FocusChange,
          mozilla::widget::InputContextAction::FocusChange::FOCUS_NOT_CHANGED,
          mozilla::widget::InputContextAction::FocusChange::WIDGET_CREATED> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::widget::InputContextAction, mCause,
                                  mFocusChange);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::WritingMode, mWritingMode._0);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ContentCache::Selection, mAnchor,
                                  mFocus, mWritingMode, mHasRange,
                                  mAnchorCharRects, mFocusCharRects, mRect);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ContentCache::Caret, mOffset, mRect);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ContentCache::TextRectArray, mStart,
                                  mRects);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ContentCache, mCompositionStart,
                                  mText, mSelection, mFirstCharRect, mCaret,
                                  mTextRectArray,
                                  mLastCommitStringTextRectArray, mEditorRect);

// InputData.h

template <>
struct ParamTraits<mozilla::InputType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::InputType, mozilla::InputType::MULTITOUCH_INPUT,
          mozilla::kHighestInputType> {};

template <>
struct ParamTraits<mozilla::InputData> {
  using paramType = mozilla::InputData;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mInputType);
    WriteParam(aWriter, aParam.mTimeStamp);
    WriteParam(aWriter, aParam.modifiers);
    WriteParam(aWriter, aParam.mFocusSequenceNumber);
    WriteParam(aWriter, aParam.mLayersId);
    WriteParam(aWriter, aParam.mCallbackId);
  }

  template <typename T>
  static bool Read(MessageReader* aReader, mozilla::InputType aInputType,
                   T* aResult) {
    static_assert(std::derived_from<T, mozilla::InputData> == true);
    if (!Read(aReader, static_cast<mozilla::InputData*>(aResult))) {
      return false;
    }
    return aResult->mInputType == aInputType;
  }

 private:
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mInputType) &&
           ReadParam(aReader, &aResult->mTimeStamp) &&
           ReadParam(aReader, &aResult->modifiers) &&
           ReadParam(aReader, &aResult->mFocusSequenceNumber) &&
           ReadParam(aReader, &aResult->mLayersId) &&
           ReadParam(aReader, &aResult->mCallbackId);
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::SingleTouchData::HistoricalTouchData,
                                  mTimeStamp, mScreenPoint, mLocalScreenPoint,
                                  mRadius, mRotationAngle, mForce);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::SingleTouchData, mHistoricalData,
                                  mIdentifier, mScreenPoint, mLocalScreenPoint,
                                  mRadius, mRotationAngle, mForce, mTiltX,
                                  mTiltY, mTwist, mAngle);

template <>
struct ParamTraits<mozilla::MultiTouchInput::MultiTouchType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::MultiTouchInput::MultiTouchType,
          mozilla::MultiTouchInput::MultiTouchType::MULTITOUCH_START,
          mozilla::MultiTouchInput::sHighestMultiTouchType> {};

template <>
struct ParamTraits<mozilla::MultiTouchInput> {
  using paramType = mozilla::MultiTouchInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mTouches);
    WriteParam(aWriter, aParam.mHandledByAPZ);
    WriteParam(aWriter, aParam.mMayTriggerSwipe);
    WriteParam(aWriter, aParam.mScreenOffset);
    WriteParam(aWriter, aParam.mButton);
    WriteParam(aWriter, aParam.mButtons);
    WriteParam(aWriter, aParam.mInputSource);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::MULTITOUCH_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mTouches) &&
           ReadParam(aReader, &aResult->mHandledByAPZ) &&
           ReadParam(aReader, &aResult->mMayTriggerSwipe) &&
           ReadParam(aReader, &aResult->mScreenOffset) &&
           ReadParam(aReader, &aResult->mButton) &&
           ReadParam(aReader, &aResult->mButtons) &&
           ReadParam(aReader, &aResult->mInputSource);
  }
};

template <>
struct ParamTraits<mozilla::MouseInput::MouseType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::MouseInput::MouseType,
          mozilla::MouseInput::MouseType::MOUSE_NONE,
          mozilla::MouseInput::sHighestMouseType> {};

template <>
struct ParamTraits<mozilla::MouseInput::ButtonType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::MouseInput::ButtonType,
          mozilla::MouseInput::ButtonType::PRIMARY_BUTTON,
          mozilla::MouseInput::sHighestButtonType> {};

template <>
struct ParamTraits<mozilla::MouseInput> {
  using paramType = mozilla::MouseInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mButtonType);
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mClickCount);
    WriteParam(aWriter, aParam.mInputSource);
    WriteParam(aWriter, aParam.mButtons);
    WriteParam(aWriter, aParam.mOrigin);
    WriteParam(aWriter, aParam.mLocalOrigin);
    WriteParam(aWriter, aParam.mHandledByAPZ);
    WriteParam(aWriter, aParam.mPreventClickEvent);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(aReader, mozilla::MOUSE_INPUT,
                                                 aResult) &&
           ReadParam(aReader, &aResult->mButtonType) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mClickCount) &&
           ReadParam(aReader, &aResult->mInputSource) &&
           ReadParam(aReader, &aResult->mButtons) &&
           ReadParam(aReader, &aResult->mOrigin) &&
           ReadParam(aReader, &aResult->mLocalOrigin) &&
           ReadParam(aReader, &aResult->mHandledByAPZ) &&
           ReadParam(aReader, &aResult->mPreventClickEvent);
  }
};

template <>
struct ParamTraits<mozilla::PanGestureInput::PanGestureType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::PanGestureInput::PanGestureType,
          mozilla::PanGestureInput::PanGestureType::PANGESTURE_MAYSTART,
          mozilla::PanGestureInput::sHighestPanGestureType> {};

template <>
struct ParamTraits<mozilla::PanGestureInput::PanDeltaType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::PanGestureInput::PanDeltaType,
          mozilla::PanGestureInput::PanDeltaType::PANDELTA_PAGE,
          mozilla::PanGestureInput::sHighestPanDeltaType> {};

template <>
struct ParamTraits<mozilla::PanGestureInput>
    : BitfieldHelper<mozilla::PanGestureInput> {
  using paramType = mozilla::PanGestureInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mPanStartPoint);
    WriteParam(aWriter, aParam.mPanDisplacement);
    WriteParam(aWriter, aParam.mLocalPanStartPoint);
    WriteParam(aWriter, aParam.mLocalPanDisplacement);
    WriteParam(aWriter, aParam.mLineOrPageDeltaX);
    WriteParam(aWriter, aParam.mLineOrPageDeltaY);
    WriteParam(aWriter, aParam.mUserDeltaMultiplierX);
    WriteParam(aWriter, aParam.mUserDeltaMultiplierY);
    WriteParam(aWriter, aParam.mDeltaType);
    WriteParam(aWriter, aParam.mHandledByAPZ);
    WriteParam(aWriter, aParam.mMayTriggerSwipe);
    WriteParam(aWriter, aParam.mOverscrollBehaviorAllowsSwipe);
    WriteParam(aWriter, aParam.mSimulateMomentum);
    WriteParam(aWriter, aParam.mIsNoLineOrPageDelta);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::PANGESTURE_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mPanStartPoint) &&
           ReadParam(aReader, &aResult->mPanDisplacement) &&
           ReadParam(aReader, &aResult->mLocalPanStartPoint) &&
           ReadParam(aReader, &aResult->mLocalPanDisplacement) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaX) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaY) &&
           ReadParam(aReader, &aResult->mUserDeltaMultiplierX) &&
           ReadParam(aReader, &aResult->mUserDeltaMultiplierY) &&
           ReadParam(aReader, &aResult->mDeltaType) &&
           ReadBoolForBitfield(aReader, aResult, &paramType::SetHandledByAPZ) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetMayTriggerSwipe) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetOverscrollBehaviorAllowsSwipe) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetSimulateMomentum) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetIsNoLineOrPageDelta);
  }
};

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND
    ParamTraits<mozilla::PinchGestureInput::PinchGestureType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::PinchGestureInput::PinchGestureType,
          mozilla::PinchGestureInput::PinchGestureType::PINCHGESTURE_START,
          mozilla::PinchGestureInput::sHighestPinchGestureType> {};

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_MIN_MISMATCH
    ParamTraits<mozilla::PinchGestureInput::PinchGestureSource>
    : public ContiguousEnumSerializerInclusive<
          mozilla::PinchGestureInput::PinchGestureSource,
          // Set the min to TOUCH, to ensure UNKNOWN is never sent over IPC
          mozilla::PinchGestureInput::PinchGestureSource::TOUCH,
          mozilla::PinchGestureInput::sHighestPinchGestureSource> {};

template <>
struct ParamTraits<mozilla::PinchGestureInput> {
  using paramType = mozilla::PinchGestureInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mSource);
    WriteParam(aWriter, aParam.mScreenOffset);
    WriteParam(aWriter, aParam.mFocusPoint);
    WriteParam(aWriter, aParam.mLocalFocusPoint);
    WriteParam(aWriter, aParam.mCurrentSpan);
    WriteParam(aWriter, aParam.mPreviousSpan);
    WriteParam(aWriter, aParam.mLineOrPageDeltaY);
    WriteParam(aWriter, aParam.mHandledByAPZ);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::PINCHGESTURE_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mSource) &&
           ReadParam(aReader, &aResult->mScreenOffset) &&
           ReadParam(aReader, &aResult->mFocusPoint) &&
           ReadParam(aReader, &aResult->mLocalFocusPoint) &&
           ReadParam(aReader, &aResult->mCurrentSpan) &&
           ReadParam(aReader, &aResult->mPreviousSpan) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaY) &&
           ReadParam(aReader, &aResult->mHandledByAPZ);
  }
};

template <>
struct ParamTraits<mozilla::TapGestureInput::TapGestureType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::TapGestureInput::TapGestureType,
          mozilla::TapGestureInput::TapGestureType::TAPGESTURE_LONG,
          mozilla::TapGestureInput::sHighestTapGestureType> {};

template <>
struct ParamTraits<mozilla::TapGestureInput> {
  using paramType = mozilla::TapGestureInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mPoint);
    WriteParam(aWriter, aParam.mLocalPoint);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::TAPGESTURE_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mPoint) &&
           ReadParam(aReader, &aResult->mLocalPoint);
  }
};

template <>
struct ParamTraits<mozilla::ScrollWheelInput::ScrollDeltaType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ScrollWheelInput::ScrollDeltaType,
          mozilla::ScrollWheelInput::ScrollDeltaType::SCROLLDELTA_LINE,
          mozilla::ScrollWheelInput::sHighestScrollDeltaType> {};

template <>
struct ParamTraits<mozilla::ScrollWheelInput::ScrollMode>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ScrollWheelInput::ScrollMode,
          mozilla::ScrollWheelInput::ScrollMode::SCROLLMODE_INSTANT,
          mozilla::ScrollWheelInput::sHighestScrollMode> {};

template <>
struct ParamTraits<mozilla::WheelDeltaAdjustmentStrategy>
    : public ContiguousEnumSerializer<
          mozilla::WheelDeltaAdjustmentStrategy,
          mozilla::WheelDeltaAdjustmentStrategy(0),
          mozilla::WheelDeltaAdjustmentStrategy::eSentinel> {};

template <>
struct ParamTraits<mozilla::layers::APZWheelAction>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::APZWheelAction,
          mozilla::layers::APZWheelAction::Scroll,
          mozilla::layers::kHighestAPZWheelAction> {};

template <>
struct ParamTraits<mozilla::ScrollWheelInput> {
  using paramType = mozilla::ScrollWheelInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mDeltaType);
    WriteParam(aWriter, aParam.mScrollMode);
    WriteParam(aWriter, aParam.mOrigin);
    WriteParam(aWriter, aParam.mHandledByAPZ);
    WriteParam(aWriter, aParam.mDeltaX);
    WriteParam(aWriter, aParam.mDeltaY);
    WriteParam(aWriter, aParam.mWheelTicksX);
    WriteParam(aWriter, aParam.mWheelTicksY);
    WriteParam(aWriter, aParam.mLocalOrigin);
    WriteParam(aWriter, aParam.mLineOrPageDeltaX);
    WriteParam(aWriter, aParam.mLineOrPageDeltaY);
    WriteParam(aWriter, aParam.mScrollSeriesNumber);
    WriteParam(aWriter, aParam.mUserDeltaMultiplierX);
    WriteParam(aWriter, aParam.mUserDeltaMultiplierY);
    WriteParam(aWriter, aParam.mMayHaveMomentum);
    WriteParam(aWriter, aParam.mIsMomentum);
    WriteParam(aWriter, aParam.mAllowToOverrideSystemScrollSpeed);
    WriteParam(aWriter, aParam.mWheelDeltaAdjustmentStrategy);
    WriteParam(aWriter, aParam.mAPZAction);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::SCROLLWHEEL_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mDeltaType) &&
           ReadParam(aReader, &aResult->mScrollMode) &&
           ReadParam(aReader, &aResult->mOrigin) &&
           ReadParam(aReader, &aResult->mHandledByAPZ) &&
           ReadParam(aReader, &aResult->mDeltaX) &&
           ReadParam(aReader, &aResult->mDeltaY) &&
           ReadParam(aReader, &aResult->mWheelTicksX) &&
           ReadParam(aReader, &aResult->mWheelTicksY) &&
           ReadParam(aReader, &aResult->mLocalOrigin) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaX) &&
           ReadParam(aReader, &aResult->mLineOrPageDeltaY) &&
           ReadParam(aReader, &aResult->mScrollSeriesNumber) &&
           ReadParam(aReader, &aResult->mUserDeltaMultiplierX) &&
           ReadParam(aReader, &aResult->mUserDeltaMultiplierY) &&
           ReadParam(aReader, &aResult->mMayHaveMomentum) &&
           ReadParam(aReader, &aResult->mIsMomentum) &&
           ReadParam(aReader, &aResult->mAllowToOverrideSystemScrollSpeed) &&
           ReadParam(aReader, &aResult->mWheelDeltaAdjustmentStrategy) &&
           ReadParam(aReader, &aResult->mAPZAction);
  }
};

template <>
struct ParamTraits<mozilla::KeyboardInput::KeyboardEventType>
    : public ContiguousEnumSerializer<
          mozilla::KeyboardInput::KeyboardEventType,
          mozilla::KeyboardInput::KeyboardEventType::KEY_DOWN,
          mozilla::KeyboardInput::KeyboardEventType::KEY_SENTINEL> {};

template <>
struct ParamTraits<mozilla::KeyboardInput> {
  using paramType = mozilla::KeyboardInput;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, static_cast<const mozilla::InputData&>(aParam));
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mKeyCode);
    WriteParam(aWriter, aParam.mCharCode);
    WriteParam(aWriter, aParam.mShortcutCandidates);
    WriteParam(aWriter, aParam.mHandledByAPZ);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ParamTraits<mozilla::InputData>::Read(
               aReader, mozilla::KEYBOARD_INPUT, aResult) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mKeyCode) &&
           ReadParam(aReader, &aResult->mCharCode) &&
           ReadParam(aReader, &aResult->mShortcutCandidates) &&
           ReadParam(aReader, &aResult->mHandledByAPZ);
  }
};

}  // namespace IPC

#endif  // nsGUIEventIPC_h_
