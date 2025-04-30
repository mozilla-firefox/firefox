/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_LayersMessageUtils
#define mozilla_layers_LayersMessageUtils

#include <stdint.h>

#include <utility>

#include "FrameMetrics.h"
#include "VsyncSource.h"
#include "chrome/common/ipc_message_utils.h"
#include "ImageContainer.h"
#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/ScrollSnapInfo.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ParamTraits_IsEnumCase.h"
#include "mozilla/ParamTraits_TiedFields.h"
#include "mozilla/ipc/ByteBuf.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/ipc/RustMessageUtils.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/AsyncDragMetrics.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/CompositorScrollUpdate.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/FocusTarget.h"
#include "mozilla/layers/GeckoContentControllerTypes.h"
#include "mozilla/layers/GpuFence.h"
#include "mozilla/layers/KeyboardMap.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/MatrixMessage.h"
#include "mozilla/layers/OverlayInfo.h"
#include "mozilla/layers/RepaintRequest.h"
#include "mozilla/layers/ScrollbarData.h"
#include "nsSize.h"
#include "mozilla/layers/DoubleTapToZoom.h"

// For ParamTraits, could be moved to cpp file
#include "ipc/nsGUIEventIPC.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/ipc/ByteBufUtils.h"

#ifdef _MSC_VER
#  pragma warning(disable : 4800)
#endif

namespace IPC {

template <>
struct ParamTraits<mozilla::layers::LayersId>
    : public ParamTraits_TiedFields<mozilla::layers::LayersId> {};

template <typename T>
struct ParamTraits<mozilla::layers::BaseTransactionId<T>>
    : public ParamTraits_TiedFields<mozilla::layers::BaseTransactionId<T>> {};

template <>
struct ParamTraits<mozilla::VsyncEvent> {
  typedef mozilla::VsyncEvent paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
    WriteParam(writer, param.mTime);
    WriteParam(writer, param.mOutputTime);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId) &&
           ReadParam(reader, &result->mTime) &&
           ReadParam(reader, &result->mOutputTime);
  }
};

template <>
struct ParamTraits<mozilla::layers::MatrixMessage> {
  typedef mozilla::layers::MatrixMessage paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mMatrix);
    WriteParam(aWriter, aParam.mTopLevelViewportVisibleRectInBrowserCoords);
    WriteParam(aWriter, aParam.mLayersId);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mMatrix) &&
           ReadParam(aReader,
                     &aResult->mTopLevelViewportVisibleRectInBrowserCoords) &&
           ReadParam(aReader, &aResult->mLayersId);
  }
};

template <>
struct ParamTraits<mozilla::layers::WindowKind>
    : public ContiguousEnumSerializer<mozilla::layers::WindowKind,
                                      mozilla::layers::WindowKind::MAIN,
                                      mozilla::layers::WindowKind::LAST> {};

template <>
struct ParamTraits<mozilla::layers::LayersBackend>
    : public ContiguousEnumSerializer<
          mozilla::layers::LayersBackend,
          mozilla::layers::LayersBackend::LAYERS_NONE,
          mozilla::layers::LayersBackend::LAYERS_LAST> {};

template <>
struct ParamTraits<mozilla::layers::WebRenderBackend>
    : public ContiguousEnumSerializer<
          mozilla::layers::WebRenderBackend,
          mozilla::layers::WebRenderBackend::HARDWARE,
          mozilla::layers::WebRenderBackend::LAST> {};

template <>
struct ParamTraits<mozilla::layers::WebRenderCompositor>
    : public ContiguousEnumSerializer<
          mozilla::layers::WebRenderCompositor,
          mozilla::layers::WebRenderCompositor::DRAW,
          mozilla::layers::WebRenderCompositor::LAST> {};

template <>
struct ParamTraits<mozilla::layers::TextureType>
    : public ContiguousEnumSerializer<mozilla::layers::TextureType,
                                      mozilla::layers::TextureType::Unknown,
                                      mozilla::layers::TextureType::Last> {};

template <>
struct ParamTraits<mozilla::layers::ScaleMode>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ScaleMode, mozilla::layers::ScaleMode::SCALE_NONE,
          mozilla::layers::kHighestScaleMode> {};

template <>
struct ParamTraits<mozilla::StyleScrollSnapStrictness>
    : public ContiguousEnumSerializerInclusive<
          mozilla::StyleScrollSnapStrictness,
          mozilla::StyleScrollSnapStrictness::None,
          mozilla::StyleScrollSnapStrictness::Proximity> {};

template <>
struct ParamTraits<mozilla::layers::TextureFlags>
    : public BitFlagsEnumSerializer<mozilla::layers::TextureFlags,
                                    mozilla::layers::TextureFlags::ALL_BITS> {};

template <>
struct ParamTraits<mozilla::layers::DiagnosticTypes>
    : public BitFlagsEnumSerializer<
          mozilla::layers::DiagnosticTypes,
          mozilla::layers::DiagnosticTypes::ALL_BITS> {};

template <>
struct ParamTraits<mozilla::layers::ScrollDirection>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ScrollDirection,
          mozilla::layers::ScrollDirection::eVertical,
          mozilla::layers::kHighestScrollDirection> {};

template <>
struct ParamTraits<mozilla::layers::FrameMetrics::ScrollOffsetUpdateType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::FrameMetrics::ScrollOffsetUpdateType,
          mozilla::layers::FrameMetrics::ScrollOffsetUpdateType::eNone,
          mozilla::layers::FrameMetrics::sHighestScrollOffsetUpdateType> {};

template <>
struct ParamTraits<mozilla::layers::RepaintRequest::ScrollOffsetUpdateType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::RepaintRequest::ScrollOffsetUpdateType,
          mozilla::layers::RepaintRequest::ScrollOffsetUpdateType::eNone,
          mozilla::layers::RepaintRequest::sHighestScrollOffsetUpdateType> {};

template <>
struct ParamTraits<mozilla::layers::OverscrollBehavior>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::OverscrollBehavior,
          mozilla::layers::OverscrollBehavior::Auto,
          mozilla::layers::kHighestOverscrollBehavior> {};

template <>
struct ParamTraits<mozilla::StyleOverflow>
    : public ContiguousEnumSerializerInclusive<mozilla::StyleOverflow,
                                               mozilla::StyleOverflow::Visible,
                                               mozilla::StyleOverflow::Clip> {};

template <>
struct ParamTraits<mozilla::layers::LayerHandle> {
  typedef mozilla::layers::LayerHandle paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mHandle);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mHandle);
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositableHandle> {
  typedef mozilla::layers::CompositableHandle paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mHandle);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mHandle);
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositableHandleOwner>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositableHandleOwner,
          mozilla::layers::CompositableHandleOwner::WebRenderBridge,
          mozilla::layers::CompositableHandleOwner::ImageBridge> {};

template <>
struct ParamTraits<mozilla::layers::RemoteTextureId> {
  typedef mozilla::layers::RemoteTextureId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::RemoteTextureOwnerId> {
  typedef mozilla::layers::RemoteTextureOwnerId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::SurfaceDescriptorRemoteDecoderId> {
  typedef mozilla::layers::SurfaceDescriptorRemoteDecoderId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::GpuProcessTextureId> {
  typedef mozilla::layers::GpuProcessTextureId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositeProcessFencesHolderId> {
  typedef mozilla::layers::CompositeProcessFencesHolderId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::FrameMetrics>
    : BitfieldHelper<mozilla::layers::FrameMetrics> {
  typedef mozilla::layers::FrameMetrics paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mScrollId);
    WriteParam(aWriter, aParam.mPresShellResolution);
    WriteParam(aWriter, aParam.mCompositionBounds);
    WriteParam(aWriter, aParam.mCompositionBoundsWidthIgnoringScrollbars);
    WriteParam(aWriter, aParam.mDisplayPort);
    WriteParam(aWriter, aParam.mScrollableRect);
    WriteParam(aWriter, aParam.mCumulativeResolution);
    WriteParam(aWriter, aParam.mDevPixelsPerCSSPixel);
    WriteParam(aWriter, aParam.mScrollOffset);
    WriteParam(aWriter, aParam.mZoom);
    WriteParam(aWriter, aParam.mScrollGeneration);
    WriteParam(aWriter, aParam.mBoundingCompositionSize);
    WriteParam(aWriter, aParam.mPresShellId);
    WriteParam(aWriter, aParam.mLayoutViewport);
    WriteParam(aWriter, aParam.mTransformToAncestorScale);
    WriteParam(aWriter, aParam.mPaintRequestTime);
    WriteParam(aWriter, aParam.mVisualDestination);
    WriteParam(aWriter, aParam.mVisualScrollUpdateType);
    WriteParam(aWriter, aParam.mFixedLayerMargins);
    WriteParam(aWriter, aParam.mCompositionSizeWithoutDynamicToolbar);
    WriteParam(aWriter, aParam.mIsRootContent);
    WriteParam(aWriter, aParam.mIsScrollInfoLayer);
    WriteParam(aWriter, aParam.mHasNonZeroDisplayPortMargins);
    WriteParam(aWriter, aParam.mMinimalDisplayPort);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (
        ReadParam(aReader, &aResult->mScrollId) &&
        ReadParam(aReader, &aResult->mPresShellResolution) &&
        ReadParam(aReader, &aResult->mCompositionBounds) &&
        ReadParam(aReader,
                  &aResult->mCompositionBoundsWidthIgnoringScrollbars) &&
        ReadParam(aReader, &aResult->mDisplayPort) &&
        ReadParam(aReader, &aResult->mScrollableRect) &&
        ReadParam(aReader, &aResult->mCumulativeResolution) &&
        ReadParam(aReader, &aResult->mDevPixelsPerCSSPixel) &&
        ReadParam(aReader, &aResult->mScrollOffset) &&
        ReadParam(aReader, &aResult->mZoom) &&
        ReadParam(aReader, &aResult->mScrollGeneration) &&
        ReadParam(aReader, &aResult->mBoundingCompositionSize) &&
        ReadParam(aReader, &aResult->mPresShellId) &&
        ReadParam(aReader, &aResult->mLayoutViewport) &&
        ReadParam(aReader, &aResult->mTransformToAncestorScale) &&
        ReadParam(aReader, &aResult->mPaintRequestTime) &&
        ReadParam(aReader, &aResult->mVisualDestination) &&
        ReadParam(aReader, &aResult->mVisualScrollUpdateType) &&
        ReadParam(aReader, &aResult->mFixedLayerMargins) &&
        ReadParam(aReader, &aResult->mCompositionSizeWithoutDynamicToolbar) &&
        ReadBoolForBitfield(aReader, aResult, &paramType::SetIsRootContent) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetIsScrollInfoLayer) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetHasNonZeroDisplayPortMargins) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetMinimalDisplayPort));
  }
};

template <>
struct ParamTraits<mozilla::APZScrollAnimationType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::APZScrollAnimationType, mozilla::APZScrollAnimationType::No,
          mozilla::APZScrollAnimationType::TriggeredByUserInput> {};

template <>
struct ParamTraits<mozilla::ScrollSnapTargetIds> {
  typedef mozilla::ScrollSnapTargetIds paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mIdsOnX);
    WriteParam(aWriter, aParam.mIdsOnY);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mIdsOnX) &&
           ReadParam(aReader, &aResult->mIdsOnY);
  }
};

template <>
struct ParamTraits<mozilla::layers::RepaintRequest>
    : BitfieldHelper<mozilla::layers::RepaintRequest> {
  typedef mozilla::layers::RepaintRequest paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mScrollId);
    WriteParam(aWriter, aParam.mPresShellResolution);
    WriteParam(aWriter, aParam.mCompositionBounds);
    WriteParam(aWriter, aParam.mCumulativeResolution);
    WriteParam(aWriter, aParam.mDevPixelsPerCSSPixel);
    WriteParam(aWriter, aParam.mScrollOffset);
    WriteParam(aWriter, aParam.mZoom);
    WriteParam(aWriter, aParam.mScrollGeneration);
    WriteParam(aWriter, aParam.mScrollGenerationOnApz);
    WriteParam(aWriter, aParam.mDisplayPortMargins);
    WriteParam(aWriter, aParam.mPresShellId);
    WriteParam(aWriter, aParam.mLayoutViewport);
    WriteParam(aWriter, aParam.mTransformToAncestorScale);
    WriteParam(aWriter, aParam.mPaintRequestTime);
    WriteParam(aWriter, aParam.mScrollUpdateType);
    WriteParam(aWriter, aParam.mScrollAnimationType);
    WriteParam(aWriter, aParam.mLastSnapTargetIds);
    WriteParam(aWriter, aParam.mIsRootContent);
    WriteParam(aWriter, aParam.mIsScrollInfoLayer);
    WriteParam(aWriter, aParam.mIsInScrollingGesture);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (
        ReadParam(aReader, &aResult->mScrollId) &&
        ReadParam(aReader, &aResult->mPresShellResolution) &&
        ReadParam(aReader, &aResult->mCompositionBounds) &&
        ReadParam(aReader, &aResult->mCumulativeResolution) &&
        ReadParam(aReader, &aResult->mDevPixelsPerCSSPixel) &&
        ReadParam(aReader, &aResult->mScrollOffset) &&
        ReadParam(aReader, &aResult->mZoom) &&
        ReadParam(aReader, &aResult->mScrollGeneration) &&
        ReadParam(aReader, &aResult->mScrollGenerationOnApz) &&
        ReadParam(aReader, &aResult->mDisplayPortMargins) &&
        ReadParam(aReader, &aResult->mPresShellId) &&
        ReadParam(aReader, &aResult->mLayoutViewport) &&
        ReadParam(aReader, &aResult->mTransformToAncestorScale) &&
        ReadParam(aReader, &aResult->mPaintRequestTime) &&
        ReadParam(aReader, &aResult->mScrollUpdateType) &&
        ReadParam(aReader, &aResult->mScrollAnimationType) &&
        ReadParam(aReader, &aResult->mLastSnapTargetIds) &&
        ReadBoolForBitfield(aReader, aResult, &paramType::SetIsRootContent) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetIsScrollInfoLayer) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetIsInScrollingGesture));
  }
};

template <>
struct ParamTraits<nsSize> {
  typedef nsSize paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.width);
    WriteParam(aWriter, aParam.height);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->width) &&
           ReadParam(aReader, &aResult->height);
  }
};

template <>
struct ParamTraits<mozilla::StyleScrollSnapStop>
    : public ContiguousEnumSerializerInclusive<
          mozilla::StyleScrollSnapStop, mozilla::StyleScrollSnapStop::Normal,
          mozilla::StyleScrollSnapStop::Always> {};

template <>
struct ParamTraits<mozilla::ScrollSnapTargetId>
    : public ParamTraits_IsEnumCase<mozilla::ScrollSnapTargetId> {};

template <>
struct ParamTraits<mozilla::SnapPoint> {
  typedef mozilla::SnapPoint paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mX);
    WriteParam(aWriter, aParam.mY);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mX) && ReadParam(aReader, &aResult->mY);
  }
};

template <>
struct ParamTraits<mozilla::ScrollSnapInfo::SnapTarget> {
  typedef mozilla::ScrollSnapInfo::SnapTarget paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mSnapPoint);
    WriteParam(aWriter, aParam.mSnapArea);
    WriteParam(aWriter, aParam.mScrollSnapStop);
    WriteParam(aWriter, aParam.mTargetId);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mSnapPoint) &&
           ReadParam(aReader, &aResult->mSnapArea) &&
           ReadParam(aReader, &aResult->mScrollSnapStop) &&
           ReadParam(aReader, &aResult->mTargetId);
  }
};

template <>
struct ParamTraits<mozilla::ScrollSnapInfo::ScrollSnapRange> {
  typedef mozilla::ScrollSnapInfo::ScrollSnapRange paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mDirection);
    WriteParam(aWriter, aParam.mSnapArea);
    WriteParam(aWriter, aParam.mTargetId);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mDirection) &&
           ReadParam(aReader, &aResult->mSnapArea) &&
           ReadParam(aReader, &aResult->mTargetId);
  }
};

template <>
struct ParamTraits<mozilla::ScrollSnapInfo> {
  typedef mozilla::ScrollSnapInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mScrollSnapStrictnessX);
    WriteParam(aWriter, aParam.mScrollSnapStrictnessY);
    WriteParam(aWriter, aParam.mSnapTargets);
    WriteParam(aWriter, aParam.mXRangeWiderThanSnapport);
    WriteParam(aWriter, aParam.mYRangeWiderThanSnapport);
    WriteParam(aWriter, aParam.mSnapportSize);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mScrollSnapStrictnessX) &&
            ReadParam(aReader, &aResult->mScrollSnapStrictnessY) &&
            ReadParam(aReader, &aResult->mSnapTargets) &&
            ReadParam(aReader, &aResult->mXRangeWiderThanSnapport) &&
            ReadParam(aReader, &aResult->mYRangeWiderThanSnapport) &&
            ReadParam(aReader, &aResult->mSnapportSize));
  }
};

template <>
struct ParamTraits<mozilla::layers::OverscrollBehaviorInfo>
    : public ParamTraits_TiedFields<mozilla::layers::OverscrollBehaviorInfo> {};

template <>
struct ParamTraits<mozilla::layers::OverflowInfo>
    : public ParamTraits_TiedFields<mozilla::layers::OverflowInfo> {};

template <typename T>
struct ParamTraits<mozilla::ScrollGeneration<T>>
    : public ParamTraits_TiedFields<mozilla::ScrollGeneration<T>> {};

template <>
struct ParamTraits<mozilla::ScrollUpdateType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ScrollUpdateType, mozilla::ScrollUpdateType::Absolute,
          mozilla::ScrollUpdateType::PureRelative> {};

template <>
struct ParamTraits<mozilla::ScrollMode>
    : public ContiguousEnumSerializerInclusive<mozilla::ScrollMode,
                                               mozilla::ScrollMode::Instant,
                                               mozilla::ScrollMode::Normal> {};

template <>
struct ParamTraits<mozilla::ScrollOrigin>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ScrollOrigin, mozilla::ScrollOrigin::None,
          mozilla::ScrollOrigin::Scrollbars> {};

template <>
struct ParamTraits<mozilla::ScrollTriggeredByScript>
    : public ContiguousEnumSerializerInclusive<
          mozilla::ScrollTriggeredByScript,
          mozilla::ScrollTriggeredByScript::No,
          mozilla::ScrollTriggeredByScript::Yes> {};

template <>
struct ParamTraits<mozilla::ScrollPositionUpdate> {
  typedef mozilla::ScrollPositionUpdate paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mScrollGeneration);
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mScrollMode);
    WriteParam(aWriter, aParam.mScrollOrigin);
    WriteParam(aWriter, aParam.mDestination);
    WriteParam(aWriter, aParam.mSource);
    WriteParam(aWriter, aParam.mDelta);
    WriteParam(aWriter, aParam.mTriggeredByScript);
    WriteParam(aWriter, aParam.mSnapTargetIds);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mScrollGeneration) &&
           ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mScrollMode) &&
           ReadParam(aReader, &aResult->mScrollOrigin) &&
           ReadParam(aReader, &aResult->mDestination) &&
           ReadParam(aReader, &aResult->mSource) &&
           ReadParam(aReader, &aResult->mDelta) &&
           ReadParam(aReader, &aResult->mTriggeredByScript) &&
           ReadParam(aReader, &aResult->mSnapTargetIds);
  }
};

template <>
struct ParamTraits<mozilla::dom::InteractiveWidget>
    : public ContiguousEnumSerializerInclusive<
          mozilla::dom::InteractiveWidget,
          mozilla::dom::InteractiveWidget::OverlaysContent,
          mozilla::dom::InteractiveWidget::ResizesVisual> {};

template <>
struct ParamTraits<mozilla::layers::ScrollMetadata>
    : BitfieldHelper<mozilla::layers::ScrollMetadata> {
  typedef mozilla::layers::ScrollMetadata paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mMetrics);
    WriteParam(aWriter, aParam.mSnapInfo);
    WriteParam(aWriter, aParam.mScrollParentId);
    WriteParam(aWriter, aParam.GetContentDescription());
    WriteParam(aWriter, aParam.mLineScrollAmount);
    WriteParam(aWriter, aParam.mPageScrollAmount);
    WriteParam(aWriter, aParam.mInteractiveWidget);
    WriteParam(aWriter, aParam.mIsLayersIdRoot);
    WriteParam(aWriter, aParam.mIsAutoDirRootContentRTL);
    WriteParam(aWriter, aParam.mForceDisableApz);
    WriteParam(aWriter, aParam.mResolutionUpdated);
    WriteParam(aWriter, aParam.mIsRDMTouchSimulationActive);
    WriteParam(aWriter, aParam.mDidContentGetPainted);
    WriteParam(aWriter, aParam.mForceMousewheelAutodir);
    WriteParam(aWriter, aParam.mForceMousewheelAutodirHonourRoot);
    WriteParam(aWriter, aParam.mIsPaginatedPresentation);
    WriteParam(aWriter, aParam.mIsSoftwareKeyboardVisible);
    WriteParam(aWriter, aParam.mDisregardedDirection);
    WriteParam(aWriter, aParam.mOverscrollBehavior);
    WriteParam(aWriter, aParam.mOverflow);
    WriteParam(aWriter, aParam.mScrollUpdates);
  }

  static bool ReadContentDescription(MessageReader* aReader,
                                     paramType* aResult) {
    nsCString str;
    if (!ReadParam(aReader, &str)) {
      return false;
    }
    aResult->SetContentDescription(str);
    return true;
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mMetrics) &&
            ReadParam(aReader, &aResult->mSnapInfo) &&
            ReadParam(aReader, &aResult->mScrollParentId) &&
            ReadContentDescription(aReader, aResult) &&
            ReadParam(aReader, &aResult->mLineScrollAmount) &&
            ReadParam(aReader, &aResult->mPageScrollAmount) &&
            ReadParam(aReader, &aResult->mInteractiveWidget) &&
            ReadBoolForBitfield(aReader, aResult,
                                &paramType::SetIsLayersIdRoot) &&
            ReadBoolForBitfield(aReader, aResult,
                                &paramType::SetIsAutoDirRootContentRTL) &&
            ReadBoolForBitfield(aReader, aResult,
                                &paramType::SetForceDisableApz) &&
            ReadBoolForBitfield(aReader, aResult,
                                &paramType::SetResolutionUpdated) &&
            ReadBoolForBitfield(aReader, aResult,
                                &paramType::SetIsRDMTouchSimulationActive)) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetDidContentGetPainted) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetForceMousewheelAutodir) &&
           ReadBoolForBitfield(
               aReader, aResult,
               &paramType::SetForceMousewheelAutodirHonourRoot) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetIsPaginatedPresentation) &&
           ReadBoolForBitfield(aReader, aResult,
                               &paramType::SetIsSoftwareKeyboardVisible) &&
           ReadParam(aReader, &aResult->mDisregardedDirection) &&
           ReadParam(aReader, &aResult->mOverscrollBehavior) &&
           ReadParam(aReader, &aResult->mOverflow) &&
           ReadParam(aReader, &aResult->mScrollUpdates);
  }
};

template <>
struct ParamTraits<mozilla::layers::TextureFactoryIdentifier> {
  typedef mozilla::layers::TextureFactoryIdentifier paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mParentBackend);
    WriteParam(aWriter, aParam.mWebRenderBackend);
    WriteParam(aWriter, aParam.mWebRenderCompositor);
    WriteParam(aWriter, aParam.mParentProcessType);
    WriteParam(aWriter, aParam.mMaxTextureSize);
    WriteParam(aWriter, aParam.mCompositorUseANGLE);
    WriteParam(aWriter, aParam.mCompositorUseDComp);
    WriteParam(aWriter, aParam.mUseCompositorWnd);
    WriteParam(aWriter, aParam.mSupportsTextureBlitting);
    WriteParam(aWriter, aParam.mSupportsPartialUploads);
    WriteParam(aWriter, aParam.mSupportsComponentAlpha);
    WriteParam(aWriter, aParam.mSupportsD3D11NV12);
    WriteParam(aWriter, aParam.mSyncHandle);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    bool result = ReadParam(aReader, &aResult->mParentBackend) &&
                  ReadParam(aReader, &aResult->mWebRenderBackend) &&
                  ReadParam(aReader, &aResult->mWebRenderCompositor) &&
                  ReadParam(aReader, &aResult->mParentProcessType) &&
                  ReadParam(aReader, &aResult->mMaxTextureSize) &&
                  ReadParam(aReader, &aResult->mCompositorUseANGLE) &&
                  ReadParam(aReader, &aResult->mCompositorUseDComp) &&
                  ReadParam(aReader, &aResult->mUseCompositorWnd) &&
                  ReadParam(aReader, &aResult->mSupportsTextureBlitting) &&
                  ReadParam(aReader, &aResult->mSupportsPartialUploads) &&
                  ReadParam(aReader, &aResult->mSupportsComponentAlpha) &&
                  ReadParam(aReader, &aResult->mSupportsD3D11NV12) &&
                  ReadParam(aReader, &aResult->mSyncHandle);
    return result;
  }
};

template <>
struct ParamTraits<mozilla::layers::TextureInfo> {
  typedef mozilla::layers::TextureInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mCompositableType);
    WriteParam(aWriter, aParam.mUsageType);
    WriteParam(aWriter, aParam.mTextureFlags);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mCompositableType) &&
           ReadParam(aReader, &aResult->mUsageType) &&
           ReadParam(aReader, &aResult->mTextureFlags);
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositableType>
    : public ContiguousEnumSerializer<
          mozilla::layers::CompositableType,
          mozilla::layers::CompositableType::UNKNOWN,
          mozilla::layers::CompositableType::COUNT> {};

template <>
struct ParamTraits<mozilla::layers::ImageUsageType>
    : public ContiguousEnumSerializer<mozilla::layers::ImageUsageType,
                                      mozilla::layers::ImageUsageType::UNKNOWN,
                                      mozilla::layers::ImageUsageType::COUNT> {
};

template <>
struct ParamTraits<mozilla::layers::ScrollableLayerGuid> {
  typedef mozilla::layers::ScrollableLayerGuid paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mLayersId);
    WriteParam(aWriter, aParam.mPresShellId);
    WriteParam(aWriter, aParam.mScrollId);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mLayersId) &&
            ReadParam(aReader, &aResult->mPresShellId) &&
            ReadParam(aReader, &aResult->mScrollId));
  }
};

template <>
struct ParamTraits<nsEventStatus>
    : public ContiguousEnumSerializer<nsEventStatus, nsEventStatus_eIgnore,
                                      nsEventStatus_eSentinel> {};

template <>
struct ParamTraits<mozilla::layers::APZHandledPlace>
    : public ContiguousEnumSerializer<
          mozilla::layers::APZHandledPlace,
          mozilla::layers::APZHandledPlace::Unhandled,
          mozilla::layers::APZHandledPlace::Last> {};

template <>
struct ParamTraits<mozilla::layers::ScrollDirections> {
  typedef mozilla::layers::ScrollDirections paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.serialize());
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    uint8_t value;
    if (!ReadParam(aReader, &value)) {
      return false;
    }
    aResult->deserialize(value);
    return true;
  }
};

template <>
struct ParamTraits<mozilla::layers::APZHandledResult> {
  typedef mozilla::layers::APZHandledResult paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mPlace);
    WriteParam(aWriter, aParam.mScrollableDirections);
    WriteParam(aWriter, aParam.mOverscrollDirections);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mPlace) &&
            ReadParam(aReader, &aResult->mScrollableDirections) &&
            ReadParam(aReader, &aResult->mOverscrollDirections));
  }
};

template <>
struct ParamTraits<mozilla::layers::APZEventResult> {
  typedef mozilla::layers::APZEventResult paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.GetStatus());
    WriteParam(aWriter, aParam.GetHandledResult());
    WriteParam(aWriter, aParam.mTargetGuid);
    WriteParam(aWriter, aParam.mInputBlockId);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    nsEventStatus status;
    if (!ReadParam(aReader, &status)) {
      return false;
    }
    aResult->UpdateStatus(status);

    mozilla::Maybe<mozilla::layers::APZHandledResult> handledResult;
    if (!ReadParam(aReader, &handledResult)) {
      return false;
    }
    aResult->UpdateHandledResult(handledResult);

    return (ReadParam(aReader, &aResult->mTargetGuid) &&
            ReadParam(aReader, &aResult->mInputBlockId));
  }
};

template <>
struct ParamTraits<mozilla::layers::ZoomConstraints> {
  typedef mozilla::layers::ZoomConstraints paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mAllowZoom);
    WriteParam(aWriter, aParam.mAllowDoubleTapZoom);
    WriteParam(aWriter, aParam.mMinZoom);
    WriteParam(aWriter, aParam.mMaxZoom);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mAllowZoom) &&
            ReadParam(aReader, &aResult->mAllowDoubleTapZoom) &&
            ReadParam(aReader, &aResult->mMinZoom) &&
            ReadParam(aReader, &aResult->mMaxZoom));
  }
};

template <>
struct ParamTraits<mozilla::layers::FocusTarget::ScrollTargets> {
  typedef mozilla::layers::FocusTarget::ScrollTargets paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mHorizontal);
    WriteParam(aWriter, aParam.mVertical);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mHorizontal) &&
           ReadParam(aReader, &aResult->mVertical);
  }
};

template <>
struct ParamTraits<mozilla::layers::FocusTarget::NoFocusTarget>
    : public EmptyStructSerializer<
          mozilla::layers::FocusTarget::NoFocusTarget> {};

template <>
struct ParamTraits<mozilla::layers::FocusTarget> {
  typedef mozilla::layers::FocusTarget paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mSequenceNumber);
    WriteParam(aWriter, aParam.mFocusHasKeyEventListeners);
    WriteParam(aWriter, aParam.mData);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    if (!ReadParam(aReader, &aResult->mSequenceNumber) ||
        !ReadParam(aReader, &aResult->mFocusHasKeyEventListeners) ||
        !ReadParam(aReader, &aResult->mData)) {
      return false;
    }
    return true;
  }
};

template <>
struct ParamTraits<
    mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType,
          mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType::
              eScrollCharacter,
          mozilla::layers::KeyboardScrollAction::
              sHighestKeyboardScrollActionType> {};

template <>
struct ParamTraits<mozilla::layers::KeyboardScrollAction> {
  typedef mozilla::layers::KeyboardScrollAction paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mForward);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mForward);
  }
};

template <>
struct ParamTraits<mozilla::layers::KeyboardShortcut> {
  typedef mozilla::layers::KeyboardShortcut paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mAction);
    WriteParam(aWriter, aParam.mKeyCode);
    WriteParam(aWriter, aParam.mCharCode);
    WriteParam(aWriter, aParam.mModifiers);
    WriteParam(aWriter, aParam.mModifiersMask);
    WriteParam(aWriter, aParam.mEventType);
    WriteParam(aWriter, aParam.mDispatchToContent);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mAction) &&
           ReadParam(aReader, &aResult->mKeyCode) &&
           ReadParam(aReader, &aResult->mCharCode) &&
           ReadParam(aReader, &aResult->mModifiers) &&
           ReadParam(aReader, &aResult->mModifiersMask) &&
           ReadParam(aReader, &aResult->mEventType) &&
           ReadParam(aReader, &aResult->mDispatchToContent);
  }
};

template <>
struct ParamTraits<mozilla::layers::KeyboardMap> {
  typedef mozilla::layers::KeyboardMap paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.Shortcuts());
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    nsTArray<mozilla::layers::KeyboardShortcut> shortcuts;
    if (!ReadParam(aReader, &shortcuts)) {
      return false;
    }
    *aResult = mozilla::layers::KeyboardMap(std::move(shortcuts));
    return true;
  }
};

template <>
struct ParamTraits<mozilla::layers::GeckoContentController_TapType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::GeckoContentController_TapType,
          mozilla::layers::GeckoContentController_TapType::eSingleTap,
          mozilla::layers::kHighestGeckoContentController_TapType> {};

template <>
struct ParamTraits<mozilla::layers::GeckoContentController_APZStateChange>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::GeckoContentController_APZStateChange,
          mozilla::layers::GeckoContentController_APZStateChange::
              eTransformBegin,
          mozilla::layers::kHighestGeckoContentController_APZStateChange> {};

template <>
struct ParamTraits<mozilla::layers::EventRegionsOverride>
    : public BitFlagsEnumSerializer<
          mozilla::layers::EventRegionsOverride,
          mozilla::layers::EventRegionsOverride::ALL_BITS> {};

template <>
struct ParamTraits<mozilla::layers::AsyncDragMetrics> {
  typedef mozilla::layers::AsyncDragMetrics paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mViewId);
    WriteParam(aWriter, aParam.mPresShellId);
    WriteParam(aWriter, aParam.mDragStartSequenceNumber);
    WriteParam(aWriter, aParam.mScrollbarDragOffset);
    WriteParam(aWriter, aParam.mDirection);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mViewId) &&
            ReadParam(aReader, &aResult->mPresShellId) &&
            ReadParam(aReader, &aResult->mDragStartSequenceNumber) &&
            ReadParam(aReader, &aResult->mScrollbarDragOffset) &&
            ReadParam(aReader, &aResult->mDirection));
  }
};

template <>
struct ParamTraits<mozilla::layers::BrowserGestureResponse>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::BrowserGestureResponse,
          mozilla::layers::BrowserGestureResponse::NotConsumed,
          mozilla::layers::BrowserGestureResponse::Consumed> {};

template <>
struct ParamTraits<mozilla::layers::CompositorOptions> {
  typedef mozilla::layers::CompositorOptions paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mUseAPZ);
    WriteParam(aWriter, aParam.mUseSoftwareWebRender);
    WriteParam(aWriter, aParam.mAllowSoftwareWebRenderD3D11);
    WriteParam(aWriter, aParam.mAllowSoftwareWebRenderOGL);
    WriteParam(aWriter, aParam.mInitiallyPaused);
    WriteParam(aWriter, aParam.mNeedFastSnaphot);
    WriteParam(aWriter, aParam.mAllowNativeCompositor);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mUseAPZ) &&
           ReadParam(aReader, &aResult->mUseSoftwareWebRender) &&
           ReadParam(aReader, &aResult->mAllowSoftwareWebRenderD3D11) &&
           ReadParam(aReader, &aResult->mAllowSoftwareWebRenderOGL) &&
           ReadParam(aReader, &aResult->mInitiallyPaused) &&
           ReadParam(aReader, &aResult->mNeedFastSnaphot) &&
           ReadParam(aReader, &aResult->mAllowNativeCompositor);
  }
};

template <>
struct ParamTraits<mozilla::layers::OverlaySupportType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::OverlaySupportType,
          mozilla::layers::OverlaySupportType::None,
          mozilla::layers::OverlaySupportType::MAX> {};

template <>
struct ParamTraits<mozilla::layers::OverlayInfo> {
  typedef mozilla::layers::OverlayInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mSupportsOverlays);
    WriteParam(aWriter, aParam.mNv12Overlay);
    WriteParam(aWriter, aParam.mYuy2Overlay);
    WriteParam(aWriter, aParam.mBgra8Overlay);
    WriteParam(aWriter, aParam.mRgb10a2Overlay);
    WriteParam(aWriter, aParam.mSupportsVpSuperResolution);
    WriteParam(aWriter, aParam.mSupportsVpAutoHDR);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mSupportsOverlays) &&
           ReadParam(aReader, &aResult->mNv12Overlay) &&
           ReadParam(aReader, &aResult->mYuy2Overlay) &&
           ReadParam(aReader, &aResult->mBgra8Overlay) &&
           ReadParam(aReader, &aResult->mRgb10a2Overlay) &&
           ReadParam(aReader, &aResult->mSupportsVpSuperResolution) &&
           ReadParam(aReader, &aResult->mSupportsVpAutoHDR);
  }
};

template <>
struct ParamTraits<mozilla::layers::SwapChainInfo> {
  typedef mozilla::layers::SwapChainInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mTearingSupported);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mTearingSupported);
  }
};

template <>
struct ParamTraits<mozilla::layers::ScrollbarLayerType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ScrollbarLayerType,
          mozilla::layers::ScrollbarLayerType::None,
          mozilla::layers::kHighestScrollbarLayerType> {};

template <>
struct ParamTraits<mozilla::layers::ScrollbarData> {
  typedef mozilla::layers::ScrollbarData paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mDirection);
    WriteParam(aWriter, aParam.mScrollbarLayerType);
    WriteParam(aWriter, aParam.mThumbRatio);
    WriteParam(aWriter, aParam.mThumbStart);
    WriteParam(aWriter, aParam.mThumbLength);
    WriteParam(aWriter, aParam.mThumbMinLength);
    WriteParam(aWriter, aParam.mThumbIsAsyncDraggable);
    WriteParam(aWriter, aParam.mScrollTrackStart);
    WriteParam(aWriter, aParam.mScrollTrackLength);
    WriteParam(aWriter, aParam.mTargetViewId);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mDirection) &&
           ReadParam(aReader, &aResult->mScrollbarLayerType) &&
           ReadParam(aReader, &aResult->mThumbRatio) &&
           ReadParam(aReader, &aResult->mThumbStart) &&
           ReadParam(aReader, &aResult->mThumbLength) &&
           ReadParam(aReader, &aResult->mThumbMinLength) &&
           ReadParam(aReader, &aResult->mThumbIsAsyncDraggable) &&
           ReadParam(aReader, &aResult->mScrollTrackStart) &&
           ReadParam(aReader, &aResult->mScrollTrackLength) &&
           ReadParam(aReader, &aResult->mTargetViewId);
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositionPayloadType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositionPayloadType,
          mozilla::layers::CompositionPayloadType::eKeyPress,
          mozilla::layers::kHighestCompositionPayloadType> {};

template <>
struct ParamTraits<mozilla::layers::CompositionPayload> {
  typedef mozilla::layers::CompositionPayload paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mTimeStamp);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mTimeStamp);
  }
};

template <>
struct ParamTraits<mozilla::layers::ClearImagesType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ClearImagesType,
          mozilla::layers::ClearImagesType::All,
          mozilla::layers::ClearImagesType::CacheOnly> {};

template <>
struct ParamTraits<mozilla::layers::CantZoomOutBehavior>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CantZoomOutBehavior,
          mozilla::layers::CantZoomOutBehavior::Nothing,
          mozilla::layers::CantZoomOutBehavior::ZoomIn> {};

template <>
struct ParamTraits<mozilla::layers::ZoomTarget> {
  typedef mozilla::layers::ZoomTarget paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.targetRect);
    WriteParam(aWriter, aParam.cantZoomOutBehavior);
    WriteParam(aWriter, aParam.elementBoundingRect);
    WriteParam(aWriter, aParam.documentRelativePointerPosition);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->targetRect) &&
            ReadParam(aReader, &aResult->cantZoomOutBehavior) &&
            ReadParam(aReader, &aResult->elementBoundingRect) &&
            ReadParam(aReader, &aResult->documentRelativePointerPosition));
  }
};

template <>
struct ParamTraits<mozilla::layers::DoubleTapToZoomMetrics> {
  typedef mozilla::layers::DoubleTapToZoomMetrics paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mVisualViewport);
    WriteParam(aWriter, aParam.mRootScrollableRect);
    WriteParam(aWriter, aParam.mTransformMatrix);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mVisualViewport) &&
            ReadParam(aReader, &aResult->mRootScrollableRect) &&
            ReadParam(aReader, &aResult->mTransformMatrix));
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositorScrollUpdate::Source>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositorScrollUpdate::Source,
          mozilla::layers::CompositorScrollUpdate::Source::UserInteraction,
          mozilla::layers::CompositorScrollUpdate::Source::Other> {};

template <>
struct ParamTraits<mozilla::layers::CompositorScrollUpdate::Metrics> {
  typedef mozilla::layers::CompositorScrollUpdate::Metrics paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mVisualScrollOffset);
    WriteParam(aWriter, aParam.mZoom);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mVisualScrollOffset) &&
            ReadParam(aReader, &aResult->mZoom));
  }
};

template <>
struct ParamTraits<mozilla::layers::CompositorScrollUpdate> {
  typedef mozilla::layers::CompositorScrollUpdate paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mMetrics);
    WriteParam(aWriter, aParam.mSource);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return (ReadParam(aReader, &aResult->mMetrics) &&
            ReadParam(aReader, &aResult->mSource));
  }
};

} /* namespace IPC */

namespace mozilla {
namespace ipc {

template <>
struct IPDLParamTraits<layers::GpuFence*> {
  static void Write(IPC::MessageWriter* aWriter, IProtocol* aActor,
                    layers::GpuFence* aParam) {
    if (aParam) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
    WriteIPDLParam(aWriter, aActor, false);
  }

  static bool Read(IPC::MessageReader* aReader, IProtocol* aActor,
                   RefPtr<layers::GpuFence>* aResult) {
    *aResult = nullptr;
    bool notnull = false;
    if (!ReadIPDLParam(aReader, aActor, &notnull)) {
      return false;
    }

    if (!notnull) {
      return true;
    }

    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return true;
  }
};

}  // namespace ipc
}  // namespace mozilla

#define DEFINE_SERVO_PARAMTRAITS(ty_)                                \
  MOZ_DEFINE_RUST_PARAMTRAITS(mozilla::ty_, Servo_##ty_##_Serialize, \
                              Servo_##ty_##_Deserialize)

DEFINE_SERVO_PARAMTRAITS(LengthPercentage)
DEFINE_SERVO_PARAMTRAITS(StyleOffsetPath)
DEFINE_SERVO_PARAMTRAITS(StyleOffsetRotate)
DEFINE_SERVO_PARAMTRAITS(StylePositionOrAuto)
DEFINE_SERVO_PARAMTRAITS(StyleOffsetPosition)
DEFINE_SERVO_PARAMTRAITS(StyleRotate)
DEFINE_SERVO_PARAMTRAITS(StyleScale)
DEFINE_SERVO_PARAMTRAITS(StyleTranslate)
DEFINE_SERVO_PARAMTRAITS(StyleTransform)
DEFINE_SERVO_PARAMTRAITS(StyleComputedTimingFunction)

#undef DEFINE_SERVO_PARAMTRAITS

#endif /* mozilla_layers_LayersMessageUtils */
