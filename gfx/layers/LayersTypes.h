/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERSTYPES_H
#define GFX_LAYERSTYPES_H

#include <stdint.h>  // for uint32_t
#include <stdio.h>   // FILE

#include <iosfwd>  // for ostream
#include <tuple>

#include "Units.h"
#include "mozilla/DefineEnum.h"  // for MOZ_DEFINE_ENUM_CLASS_WITH_BASE
#include "mozilla/EnumSet.h"
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"  // for TimeStamp
#include "nsRegion.h"

#ifndef MOZ_LAYERS_HAVE_LOG
#  define MOZ_LAYERS_HAVE_LOG
#endif
#define MOZ_LAYERS_LOG(_args) \
  MOZ_LOG(LayerManager::GetLog(), LogLevel::Debug, _args)
#define MOZ_LAYERS_LOG_IF_SHADOWABLE(layer, _args)

#define INVALID_OVERLAY -1

// #define ENABLE_FRAME_LATENCY_LOG

namespace IPC {
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

enum class StyleBorderStyle : uint8_t;

namespace layers {

class TextureHost;

#undef NONE
#undef OPAQUE

struct LayersId final {
  uint64_t mId = 0;

  auto MutTiedFields() { return std::tie(mId); }

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  friend uint64_t format_as(const LayersId& aId) { return aId.mId; }

  // Implement some operators so this class can be used as a key in
  // stdlib classes.
  auto operator<=>(const LayersId& aOther) const = default;

  friend std::ostream& operator<<(std::ostream& aStream, const LayersId& aId);

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<LayersId, ValueType, LayersId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const LayersId& aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

template <typename T>
struct BaseTransactionId final {
  uint64_t mId = 0;

  auto MutTiedFields() { return std::tie(mId); }

  bool IsValid() const { return mId != 0; }

  [[nodiscard]] BaseTransactionId<T> Next() const {
    return BaseTransactionId<T>{mId + 1};
  }

  [[nodiscard]] BaseTransactionId<T> Prev() const {
    return BaseTransactionId<T>{mId - 1};
  }

  int64_t operator-(const BaseTransactionId<T>& aOther) const {
    return mId - aOther.mId;
  }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  auto operator<=>(const BaseTransactionId<T>& aOther) const = default;
};

class TransactionIdType {};
typedef BaseTransactionId<TransactionIdType> TransactionId;

// CompositionOpportunityId is a counter that goes up every time we have an
// opportunity to composite. It increments even on no-op composites (if nothing
// has changed) and while compositing is paused. It does not skip values if a
// composite is delayed. It is meaningful per window.
// This counter is used to differentiate intentionally-skipped video frames from
// unintentionally-skipped video frames: If CompositionOpportunityIds are
// observed by the video in +1 increments, then the video was onscreen the
// entire time and compositing was not paused. But if gaps in
// CompositionOpportunityIds are observed, that must mean that the video was not
// considered during some composition opportunities, because compositing was
// paused or because the video was not part of the on-screen scene.
class CompositionOpportunityType {};
typedef BaseTransactionId<CompositionOpportunityType> CompositionOpportunityId;

/// We make different decisions about resource allocation sizes in WebRender
/// depending on whether we are going to render web pages or simpler
/// content in the window.
enum class WindowKind : int8_t { MAIN = 0, SECONDARY, LAST };

enum class LayersBackend : int8_t { LAYERS_NONE = 0, LAYERS_WR, LAYERS_LAST };

enum class WebRenderBackend : int8_t { HARDWARE = 0, SOFTWARE, LAST };

enum class WebRenderCompositor : int8_t {
  DRAW = 0,
  DIRECT_COMPOSITION,
  CORE_ANIMATION,
  SOFTWARE,
  D3D11,
  OPENGL,
  WAYLAND,
  Unknown,
  LAST
};

const char* GetLayersBackendName(LayersBackend aBackend);

enum class TextureType : int8_t {
  Unknown = 0,
  D3D11,
  MacIOSurface,
  AndroidHardwareBuffer,
  DMABUF,
  EGLImage,
  Last
};

enum class DrawRegionClip : int8_t { DRAW, NONE };

enum class SurfaceMode : int8_t {
  SURFACE_NONE = 0,
  SURFACE_OPAQUE,
  SURFACE_SINGLE_CHANNEL_ALPHA,
  SURFACE_COMPONENT_ALPHA
};

// clang-format off
MOZ_DEFINE_ENUM_CLASS_WITH_BASE(
  ScaleMode, int8_t, (
    SCALE_NONE,
    STRETCH
// Unimplemented - PRESERVE_ASPECT_RATIO_CONTAIN
));
// clang-format on

// Bit flags that go on a RefLayer and override the
// event regions in the entire subtree below. This is needed for propagating
// various flags across processes since the child-process layout code doesn't
// know about parent-process listeners or CSS rules.
enum EventRegionsOverride {
  // The default, no flags set
  NoOverride = 0,
  // Treat all hit regions in the subtree as dispatch-to-content
  ForceDispatchToContent = (1 << 0),
  // Treat all hit regions in the subtree as empty
  ForceEmptyHitRegion = (1 << 1),
  // OR union of all valid bit flags, for use in BitFlagsEnumSerializer
  ALL_BITS = (1 << 2) - 1
};

MOZ_ALWAYS_INLINE EventRegionsOverride operator|(EventRegionsOverride a,
                                                 EventRegionsOverride b) {
  return (EventRegionsOverride)((int)a | (int)b);
}

MOZ_ALWAYS_INLINE EventRegionsOverride& operator|=(EventRegionsOverride& a,
                                                   EventRegionsOverride b) {
  a = a | b;
  return a;
}

// Flags used as an argument to functions that dump textures.
enum TextureDumpMode {
  Compress,      // dump texture with LZ4 compression
  DoNotCompress  // dump texture uncompressed
};

// Corresponding bit masks for allowed touch behaviors
// are defined in AllowedTouchBehavior
typedef uint32_t TouchBehaviorFlags;

// Some specialized typedefs of Matrix4x4Typed.
typedef gfx::Matrix4x4Typed<LayerPixel, CSSTransformedLayerPixel>
    CSSTransformMatrix;
// Several different async transforms can contribute to a layer's transform
// (specifically, an async animation can contribute a transform, and each APZC
// that scrolls a layer can contribute async scroll/zoom and overscroll
// transforms).
// To try to model this with typed units, we represent individual async
// transforms as ParentLayer -> ParentLayer transforms (aliased as
// AsyncTransformComponentMatrix), and we represent the product of all of them
// as a CSSTransformLayer -> ParentLayer transform (aliased as
// AsyncTransformMatrix). To create an AsyncTransformMatrix from component
// matrices, a ViewAs operation is needed. A MultipleAsyncTransforms
// PixelCastJustification is provided for this purpose.
typedef gfx::Matrix4x4Typed<ParentLayerPixel, ParentLayerPixel>
    AsyncTransformComponentMatrix;
typedef gfx::Matrix4x4Typed<CSSTransformedLayerPixel, ParentLayerPixel>
    AsyncTransformMatrix;

typedef Array<gfx::DeviceColor, 4> BorderColors;
typedef Array<LayerSize, 4> BorderCorners;
typedef Array<LayerCoord, 4> BorderWidths;
typedef Array<StyleBorderStyle, 4> BorderStyles;

typedef Maybe<LayerRect> MaybeLayerRect;

// This is used to communicate Layers across IPC channels. The Handle is valid
// for layers in the same PLayerTransaction. Handles are created by
// ClientLayerManager, and are cached in LayerTransactionParent on first use.
class LayerHandle final {
  friend struct IPC::ParamTraits<mozilla::layers::LayerHandle>;

 public:
  LayerHandle() : mHandle(0) {}
  LayerHandle(const LayerHandle& aOther) = default;
  explicit LayerHandle(uint64_t aHandle) : mHandle(aHandle) {}
  bool IsValid() const { return mHandle != 0; }
  explicit operator bool() const { return IsValid(); }
  bool operator==(const LayerHandle& aOther) const = default;
  uint64_t Value() const { return mHandle; }

 private:
  uint64_t mHandle;
};

// This is used to communicate Compositables across IPC channels. The Handle is
// valid for layers in the same PLayerTransaction or PImageBridge. Handles are
// created by ClientLayerManager or ImageBridgeChild, and are cached in the
// parent side on first use.
class CompositableHandle final {
  friend struct IPC::ParamTraits<mozilla::layers::CompositableHandle>;

 public:
  static CompositableHandle GetNext();

  CompositableHandle() : mHandle(0) {}
  CompositableHandle(const CompositableHandle& aOther) = default;
  explicit CompositableHandle(uint64_t aHandle) : mHandle(aHandle) {}
  bool IsValid() const { return mHandle != 0; }
  explicit operator bool() const { return IsValid(); }
  explicit operator uint64_t() const { return mHandle; }
  bool operator==(const CompositableHandle& aOther) const = default;
  uint64_t Value() const { return mHandle; }

 private:
  uint64_t mHandle;
};

enum class CompositableHandleOwner : uint8_t {
  WebRenderBridge,
  ImageBridge,
};

struct RemoteTextureId {
  uint64_t mId = 0;

  auto MutTiedFields() { return std::tie(mId); }

  static RemoteTextureId GetNext();

  static constexpr RemoteTextureId Max() { return RemoteTextureId{UINT64_MAX}; }

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  // Implement some operators so this class can be used as a key in
  // stdlib classes.
  auto operator<=>(const RemoteTextureId& aOther) const = default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<RemoteTextureId, ValueType, RemoteTextureId::HashFn>
  //   myMap;
  struct HashFn {
    std::size_t operator()(const RemoteTextureId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

struct RemoteTextureOwnerId {
  uint64_t mId = 0;

  auto MutTiedFields() { return std::tie(mId); }

  static RemoteTextureOwnerId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  // Implement some operators so this class can be used as a key in
  // stdlib classes.
  auto operator<=>(const RemoteTextureOwnerId& aOther) const = default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<RemoteTextureOwnerId, ValueType,
  //   RemoteTextureOwnerId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const RemoteTextureOwnerId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

struct SurfaceDescriptorRemoteDecoderId {
  uint64_t mId = 0;

  auto MutTiedFields() { return std::tie(mId); }

  static SurfaceDescriptorRemoteDecoderId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  // Implement some operators so this class can be used as a key in
  // stdlib classes.
  auto operator<=>(const SurfaceDescriptorRemoteDecoderId& aOther) const =
      default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<SurfaceDescriptorRemoteDecoderId, ValueType,
  //   SurfaceDescriptorRemoteDecoderId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const SurfaceDescriptorRemoteDecoderId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

typedef uint32_t RemoteTextureTxnType;
typedef uint64_t RemoteTextureTxnId;

// TextureId allocated in GPU process
struct GpuProcessTextureId {
  uint64_t mId = 0;

  static GpuProcessTextureId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  auto operator<=>(const GpuProcessTextureId& aOther) const = default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<GpuProcessTextureId, ValueType,
  //   GpuProcessTextureId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const GpuProcessTextureId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

// FencesHolderId allocated in GPU process
struct CompositeProcessFencesHolderId {
  uint64_t mId = 0;

  static CompositeProcessFencesHolderId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  auto operator<=>(const CompositeProcessFencesHolderId& aOther) const =
      default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<GpuProcessQueryId, ValueType,
  //   GpuProcessQueryId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const CompositeProcessFencesHolderId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

// AndroidImageReaderId allocated in GPU process
struct GpuProcessAndroidImageReaderId {
  uint64_t mId = 0;

  static GpuProcessAndroidImageReaderId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  auto operator<=>(const GpuProcessAndroidImageReaderId& aOther) const =
      default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<GpuProcessAndroidImageReaderId, ValueType,
  //   GpuProcessAndroidImageReaderId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const GpuProcessAndroidImageReaderId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

struct AndroidMediaCodecFrameId {
  uint64_t mId = 0;

  static AndroidMediaCodecFrameId GetNext();

  bool IsValid() const { return mId != 0; }

  // Allow explicit cast to a uint64_t for now
  explicit operator uint64_t() const { return mId; }

  auto operator<=>(const AndroidMediaCodecFrameId& aOther) const = default;

  // Helper struct that allow this class to be used as a key in
  // std::unordered_map like so:
  //   std::unordered_map<AndroidMediaCodecFrameId, ValueType,
  //   AndroidMediaCodecFrameId::HashFn> myMap;
  struct HashFn {
    std::size_t operator()(const AndroidMediaCodecFrameId aKey) const {
      return std::hash<uint64_t>{}(aKey.mId);
    }
  };
};

// clang-format off
MOZ_DEFINE_ENUM_CLASS_WITH_BASE(ScrollDirection, uint8_t, (
  eVertical,
  eHorizontal
));

std::ostream& operator<<(std::ostream& os, ScrollDirection aDirection);

using ScrollDirections = EnumSet<ScrollDirection, uint8_t>;

constexpr ScrollDirections EitherScrollDirection(ScrollDirection::eVertical,ScrollDirection::eHorizontal);
constexpr ScrollDirections HorizontalScrollDirection(ScrollDirection::eHorizontal);
constexpr ScrollDirections VerticalScrollDirection(ScrollDirection::eVertical);

// Return the scroll directions which have a nonzero component in |aDelta|.
template <typename Point>
ScrollDirections DirectionsInDelta(const Point& aDelta) {
  ScrollDirections result;
  if (aDelta.x != 0) {
    result += ScrollDirection::eHorizontal;
  }
  if (aDelta.y != 0) {
    result += ScrollDirection::eVertical;
  }
  return result;
}

MOZ_DEFINE_ENUM_CLASS_WITH_BASE(CompositionPayloadType, uint8_t, (
  /**
   * A |CompositionPayload| with this type indicates a key press happened
   * before composition and will be used to determine latency between key press
   * and presentation in |mozilla::Telemetry::KEYPRESS_PRESENT_LATENCY|
   */
  eKeyPress,

  /**
   * A |CompositionPayload| with this type indicates that an APZ scroll event
   * occurred that will be included in the composition.
   */
  eAPZScroll,

  /**
   * A |CompositionPayload| with this type indicates that an APZ pinch-to-zoom
   * event occurred that will be included in the composition.
   */
  eAPZPinchZoom,

  /**
   * A |CompositionPayload| with this type indicates that content was painted
   * that will be included in the composition.
   */
  eContentPaint,

  /**
   * A |CompositionPayload| with this type indicates a mouse up (which caused
   * a click to happen) happened before composition and will be used to determine latency
   * between mouse up and presentation in
   * |mozilla::Telemetry::MOUSEUP_FOLLOWED_BY_CLICK_PRESENT_LATENCY|
   */
  eMouseUpFollowedByClick
));
// clang-format on

extern const char* kCompositionPayloadTypeNames[kCompositionPayloadTypeCount];

struct CompositionPayload {
  bool operator==(const CompositionPayload& aOther) const = default;
  /* The type of payload that is in this composition */
  CompositionPayloadType mType;
  /* When this payload was generated */
  TimeStamp mTimeStamp;
};

// clang-format off
MOZ_DEFINE_ENUM_CLASS_WITH_BASE(
  RemoteDecoderVideoType, int8_t, (
    TypeNone,
    Buffer,
    D3D10,
    DXGIYCbCr,
    DMABuf,
    MacIOSurface,
    DcompSurface,
    AndroidImageReaderImage,
    AndroidSurfaceTexture
));
// clang-format on

}  // namespace layers
}  // namespace mozilla

#endif /* GFX_LAYERSTYPES_H */
