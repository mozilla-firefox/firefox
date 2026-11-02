/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_LayersMessageUtils
#define mozilla_layers_LayersMessageUtils

#include <stdint.h>

#include <utility>

#include "FrameMetrics.h"
#include "ImageContainer.h"
#include "VsyncSource.h"
#include "chrome/common/ipc_message_utils.h"
#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/RelativeTo.h"
#include "mozilla/ScrollSnapInfo.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ipc/ByteBuf.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/ipc/RustMessageUtils.h"
#include "mozilla/layers/APZBridge.h"
#include "mozilla/layers/AsyncDragMetrics.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/CompositorScrollUpdate.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/DoubleTapToZoom.h"
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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::VsyncEvent, mId, mTime, mOutputTime);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::MatrixMessage, mMatrix,
                                  mTopLevelViewportVisibleRectInBrowserCoords,
                                  mLayersId);

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
struct ParamTraits<mozilla::layers::ScrollOffsetUpdateType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ScrollOffsetUpdateType,
          mozilla::layers::ScrollOffsetUpdateType::None,
          mozilla::layers::kHighestScrollOffsetUpdateType> {};

template <>
struct ParamTraits<mozilla::layers::RepaintRequest::ScrollOffsetUpdateType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::RepaintRequest::ScrollOffsetUpdateType,
          mozilla::layers::RepaintRequest::ScrollOffsetUpdateType::eNone,
          mozilla::layers::RepaintRequest::sHighestScrollOffsetUpdateType> {};

template <>
struct ParamTraits<mozilla::StyleOverscrollBehavior>
    : public ContiguousEnumSerializerInclusive<
          mozilla::StyleOverscrollBehavior,
          mozilla::StyleOverscrollBehavior::Auto,
          mozilla::StyleOverscrollBehavior::None> {};

template <>
struct ParamTraits<mozilla::StyleOverflow>
    : public ContiguousEnumSerializerInclusive<mozilla::StyleOverflow,
                                               mozilla::StyleOverflow::Visible,
                                               mozilla::StyleOverflow::Clip> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::LayerHandle, mHandle);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::CompositableHandle, mHandle);

template <>
struct ParamTraits<mozilla::layers::CompositableHandleOwner>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositableHandleOwner,
          mozilla::layers::CompositableHandleOwner::WebRenderBridge,
          mozilla::layers::CompositableHandleOwner::ImageBridge> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::RemoteTextureId, mId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::RemoteTextureOwnerId, mId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::layers::SurfaceDescriptorRemoteDecoderId, mId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::GpuProcessTextureId, mId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::layers::CompositeProcessFencesHolderId, mId);

template <>
struct ParamTraits<mozilla::layers::GpuProcessAndroidImageReaderId> {
  typedef mozilla::layers::GpuProcessAndroidImageReaderId paramType;

  static void Write(MessageWriter* writer, const paramType& param) {
    WriteParam(writer, param.mId);
  }
  static bool Read(MessageReader* reader, paramType* result) {
    return ReadParam(reader, &result->mId);
  }
};

template <>
struct ParamTraits<mozilla::layers::AndroidMediaCodecFrameId> {
  typedef mozilla::layers::AndroidMediaCodecFrameId paramType;

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
    WriteParam(aWriter, aParam.mInteractiveWidget);
    WriteParam(aWriter, aParam.mIsRootContent);
    WriteParam(aWriter, aParam.mIsScrollInfoLayer);
    WriteParam(aWriter, aParam.mHasNonZeroDisplayPortMargins);
    WriteParam(aWriter, aParam.mMinimalDisplayPort);
    WriteParam(aWriter, aParam.mIsSoftwareKeyboardVisible);
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
        ReadParam(aReader, &aResult->mInteractiveWidget) &&
        ReadBoolForBitfield(aReader, aResult, &paramType::SetIsRootContent) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetIsScrollInfoLayer) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetHasNonZeroDisplayPortMargins) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetMinimalDisplayPort) &&
        ReadBoolForBitfield(aReader, aResult,
                            &paramType::SetIsSoftwareKeyboardVisible));
  }
};

template <>
struct ParamTraits<mozilla::APZScrollAnimationType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::APZScrollAnimationType, mozilla::APZScrollAnimationType::No,
          mozilla::APZScrollAnimationType::TriggeredByUserInput> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ScrollSnapTargetIds, mIdsOnX,
                                  mIdsOnY);

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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(nsSize, width, height);

template <>
struct ParamTraits<mozilla::StyleScrollSnapStop>
    : public ContiguousEnumSerializerInclusive<
          mozilla::StyleScrollSnapStop, mozilla::StyleScrollSnapStop::Normal,
          mozilla::StyleScrollSnapStop::Always> {};

struct ScrollSnapTargetIdValidator {
  using IntegralType = std::underlying_type_t<mozilla::ScrollSnapTargetId>;

  static bool IsLegalValue(const IntegralType e) { return true; }
};

template <>
struct ParamTraits<mozilla::ScrollSnapTargetId>
    : public EnumSerializer<mozilla::ScrollSnapTargetId,
                            ScrollSnapTargetIdValidator> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::SnapPoint, mX, mY);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ScrollSnapInfo::SnapTarget,
                                  mSnapPoint, mSnapArea, mScrollSnapStop,
                                  mTargetId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ScrollSnapRange, mDirection,
                                  mSnapArea, mTargetId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ScrollSnapInfo,
                                  mScrollSnapStrictnessX,
                                  mScrollSnapStrictnessY, mSnapTargets,
                                  mXRangeWiderThanSnapport,
                                  mYRangeWiderThanSnapport, mSnapportSize);

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
          mozilla::ScrollUpdateType::ZeroDeltaLayoutScroll> {};

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
struct ParamTraits<mozilla::ViewportType>
    : public ContiguousEnumSerializerInclusive<mozilla::ViewportType,
                                               mozilla::ViewportType::Layout,
                                               mozilla::ViewportType::Visual> {
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::ScrollPositionUpdate,
                                  mScrollGeneration, mType, mScrollMode,
                                  mScrollOrigin, mDestination, mSource, mDelta,
                                  mViewportType, mTriggeredByScript,
                                  mSnapTargetIds);

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
    WriteParam(aWriter, aParam.mIsLayersIdRoot);
    WriteParam(aWriter, aParam.mIsAutoDirRootContentRTL);
    WriteParam(aWriter, aParam.mForceDisableApz);
    WriteParam(aWriter, aParam.mResolutionUpdated);
    WriteParam(aWriter, aParam.mIsRDMTouchSimulationActive);
    WriteParam(aWriter, aParam.mDidContentGetPainted);
    WriteParam(aWriter, aParam.mForceMousewheelAutodir);
    WriteParam(aWriter, aParam.mForceMousewheelAutodirHonourRoot);
    WriteParam(aWriter, aParam.mIsPaginatedPresentation);
    WriteParam(aWriter, aParam.mDisregardedDirection);
    WriteParam(aWriter, aParam.mOverscrollBehavior);
    WriteParam(aWriter, aParam.mOverflow);
    WriteParam(aWriter, aParam.mScrollUpdates);
    WriteParam(aWriter, aParam.mWritingMode);
    WriteParam(aWriter, aParam.mScrollGenerationOnApz);
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
           ReadParam(aReader, &aResult->mDisregardedDirection) &&
           ReadParam(aReader, &aResult->mOverscrollBehavior) &&
           ReadParam(aReader, &aResult->mOverflow) &&
           ReadParam(aReader, &aResult->mScrollUpdates) &&
           ReadParam(aReader, &aResult->mWritingMode) &&
           ReadParam(aReader, &aResult->mScrollGenerationOnApz);
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::TextureFactoryIdentifier,
                                  mParentBackend, mWebRenderBackend,
                                  mWebRenderCompositor, mParentProcessType,
                                  mMaxTextureSize, mCompositorUseANGLE,
                                  mCompositorUseDComp, mUseLayerCompositor,
                                  mUseCompositorWnd, mSupportsTextureBlitting,
                                  mSupportsPartialUploads,
                                  mSupportsComponentAlpha, mSupportsD3D11NV12,
                                  mSyncHandle);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::TextureInfo,
                                  mCompositableType, mUsageType, mTextureFlags);

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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::ScrollableLayerGuid,
                                  mLayersId, mPresShellId, mScrollId);

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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::APZHandledResult, mPlace,
                                  mScrollableDirections, mOverscrollDirections);

template <>
struct ParamTraits<mozilla::layers::APZEventResult> {
  typedef mozilla::layers::APZEventResult paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.GetStatus());
    WriteParam(aWriter, aParam.GetHandledResult());
    WriteParam(aWriter, aParam.mTargetGuid);
    WriteParam(aWriter, aParam.mInputBlockId);
    WriteParam(aWriter, aParam.mTargetCanScrollHorizontally);
    WriteParam(aWriter, aParam.mSwipableDirectionsForTouch);
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
            ReadParam(aReader, &aResult->mInputBlockId) &&
            ReadParam(aReader, &aResult->mTargetCanScrollHorizontally) &&
            ReadParam(aReader, &aResult->mSwipableDirectionsForTouch));
  }
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::ZoomConstraints, mAllowZoom,
                                  mAllowDoubleTapZoom, mMinZoom, mMaxZoom);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::FocusTarget::ScrollTargets,
                                  mHorizontal, mVertical);

template <>
struct ParamTraits<mozilla::layers::FocusTarget::NoFocusTarget>
    : public EmptyStructSerializer<
          mozilla::layers::FocusTarget::NoFocusTarget> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::FocusTarget, mSequenceNumber,
                                  mFocusHasKeyEventListeners, mData);

template <>
struct ParamTraits<
    mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType,
          mozilla::layers::KeyboardScrollAction::KeyboardScrollActionType::
              eScrollCharacter,
          mozilla::layers::KeyboardScrollAction::
              sHighestKeyboardScrollActionType> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::KeyboardScrollAction, mType,
                                  mForward);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::KeyboardShortcut, mAction,
                                  mKeyCode, mCharCode, mModifiers,
                                  mModifiersMask, mEventType,
                                  mDispatchToContent);

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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::AsyncDragMetrics, mViewId,
                                  mPresShellId, mDragStartSequenceNumber,
                                  mScrollbarDragOffset, mDirection);

template <>
struct ParamTraits<mozilla::layers::BrowserGestureResponse>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::BrowserGestureResponse,
          mozilla::layers::BrowserGestureResponse::NotConsumed,
          mozilla::layers::BrowserGestureResponse::Consumed> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::CompositorOptions, mUseAPZ,
                                  mUseSoftwareWebRender,
                                  mAllowSoftwareWebRenderD3D11,
                                  mAllowSoftwareWebRenderOGL, mInitiallyPaused,
                                  mNeedFastSnaphot, mAllowNativeCompositor);

template <>
struct ParamTraits<mozilla::layers::OverlaySupportType>
    : public ContiguousEnumSerializer<
          mozilla::layers::OverlaySupportType,
          mozilla::layers::OverlaySupportType::None,
          mozilla::layers::OverlaySupportType::MAX> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::OverlayInfo,
                                  mSupportsOverlays, mNv12Overlay, mYuy2Overlay,
                                  mBgra8Overlay, mRgb10a2Overlay,
                                  mRgba16fOverlay, mSupportsVpSuperResolution,
                                  mSupportsVpAutoHDR, mSupportsHDR);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::SwapChainInfo,
                                  mTearingSupported);

template <>
struct ParamTraits<mozilla::layers::ScrollbarLayerType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::ScrollbarLayerType,
          mozilla::layers::ScrollbarLayerType::None,
          mozilla::layers::kHighestScrollbarLayerType> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::ScrollbarData, mDirection,
                                  mScrollbarLayerType, mThumbRatio, mThumbStart,
                                  mThumbLength, mThumbMinLength,
                                  mThumbIsAsyncDraggable, mScrollTrackStart,
                                  mScrollTrackLength, mTargetViewId);

template <>
struct ParamTraits<mozilla::layers::CompositionPayloadType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositionPayloadType,
          mozilla::layers::CompositionPayloadType::eKeyPress,
          mozilla::layers::kHighestCompositionPayloadType> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::CompositionPayload, mType,
                                  mTimeStamp);

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

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::ZoomTarget, targetRect,
                                  cantZoomOutBehavior, elementBoundingRect,
                                  documentRelativePointerPosition);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::DoubleTapToZoomMetrics,
                                  mVisualViewport, mRootScrollableRect,
                                  mTransformMatrix);

template <>
struct ParamTraits<mozilla::layers::CompositorScrollUpdate::Source>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::CompositorScrollUpdate::Source,
          mozilla::layers::CompositorScrollUpdate::Source::UserInteraction,
          mozilla::layers::CompositorScrollUpdate::Source::Other> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    mozilla::layers::CompositorScrollUpdate::Metrics, mVisualScrollOffset,
    mZoom);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::layers::CompositorScrollUpdate,
                                  mMetrics, mSource);

template <>
struct ParamTraits<mozilla::layers::GpuFence*> {
  static void Write(MessageWriter* aWriter, mozilla::layers::GpuFence* aParam) {
    if (aParam) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
    WriteParam(aWriter, false);
  }

  static bool Read(MessageReader* aReader,
                   RefPtr<mozilla::layers::GpuFence>* aResult) {
    *aResult = nullptr;
    bool notnull = false;
    if (!ReadParam(aReader, &notnull)) {
      return false;
    }

    if (!notnull) {
      return true;
    }

    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return true;
  }
};

template <>
struct ParamTraits<mozilla::layers::RemoteDecoderVideoType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::layers::RemoteDecoderVideoType,
          mozilla::layers::RemoteDecoderVideoType::TypeNone,
          mozilla::layers::kHighestRemoteDecoderVideoType> {};

} /* namespace IPC */

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
