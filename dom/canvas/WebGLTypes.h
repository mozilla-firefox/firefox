/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLTYPES_H_
#define WEBGLTYPES_H_

#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "GLContextTypes.h"
#include "GLDefs.h"
#include "ImageContainer.h"
#include "gfxTypes.h"
#include "mozilla/Casting.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/Range.h"
#include "mozilla/RefCounted.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/Span.h"
#include "mozilla/TiedFields.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/BuildConstants.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "nsString.h"
#include "nsTArray.h"

// Manual reflection of WebIDL typedefs that are different from their
// OpenGL counterparts.
using WebGLsizeiptr = int64_t;
using WebGLintptr = int64_t;
using WebGLboolean = bool;

// -

namespace mozilla {
namespace gl {
class GLContext;  // This is going to be needed a lot.
}  // namespace gl

// -
// Prevent implicit conversions into calloc and malloc. (mozilla namespace
// only!)

template <typename DestT>
class ForbidNarrowing final {
  DestT mVal;

 public:
  template <typename SrcT>
  MOZ_IMPLICIT ForbidNarrowing(SrcT val) : mVal(val) {
    static_assert(
        std::numeric_limits<SrcT>::min() >= std::numeric_limits<DestT>::min(),
        "SrcT must be narrower than DestT.");
    static_assert(
        std::numeric_limits<SrcT>::max() <= std::numeric_limits<DestT>::max(),
        "SrcT must be narrower than DestT.");
  }

  explicit operator DestT() const { return mVal; }
};

inline void* malloc(const ForbidNarrowing<size_t> s) {
  return ::malloc(size_t(s));
}

inline void* calloc(const ForbidNarrowing<size_t> n,
                    const ForbidNarrowing<size_t> size) {
  return ::calloc(size_t(n), size_t(size));
}

// -

// TODO: Remove this now-mere-alias.
template <typename From>
inline auto AutoAssertCast(const From val) {
  return LazyAssertedCast(val);
}

const char* GetEnumName(GLenum val, const char* defaultRet = "<unknown>");
std::string EnumString(GLenum val);

namespace webgl {
template <typename T>
struct QueueParamTraits;
class TexUnpackBytes;
class TexUnpackSurface;
}  // namespace webgl

class ClientWebGLContext;
class WebGLTexture;
class WebGLBuffer;
class WebGLFramebuffer;
class WebGLProgram;
class WebGLQuery;
class WebGLRenderbuffer;
class WebGLSampler;
class WebGLShader;
class WebGLSync;
class WebGLTexture;
class WebGLTransformFeedback;
class WebGLVertexArray;

// -

class VRefCounted : public RefCounted<VRefCounted> {
 public:
  virtual ~VRefCounted() = default;

#ifdef MOZ_REFCOUNTED_LEAK_CHECKING
  virtual const char* typeName() const = 0;
  virtual size_t typeSize() const = 0;
#endif
};

// -

/*
 * Implementing WebGL (or OpenGL ES 2.0) on top of desktop OpenGL requires
 * emulating the vertex attrib 0 array when it's not enabled. Indeed,
 * OpenGL ES 2.0 allows drawing without vertex attrib 0 array enabled, but
 * desktop OpenGL does not allow that.
 */
enum class WebGLVertexAttrib0Status : uint8_t {
  Default,                     // default status - no emulation needed
  EmulatedUninitializedArray,  // need an artificial attrib 0 array, but
                               // contents may be left uninitialized
  EmulatedInitializedArray  // need an artificial attrib 0 array, and contents
                            // must be initialized
};

/*
 * The formats that may participate, either as source or destination formats,
 * in WebGL texture conversions. This includes:
 *  - all the formats accepted by WebGL.texImage2D, e.g. RGBA4444
 *  - additional formats provided by extensions, e.g. RGB32F
 *  - additional source formats, depending on browser details, used when
 * uploading textures from DOM elements. See gfxImageSurface::Format().
 */
enum class WebGLTexelFormat : uint8_t {
  // returned by SurfaceFromElementResultToImageSurface to indicate absence of
  // image data
  None,
  // common value for formats for which format conversions are not supported
  FormatNotSupportingAnyConversion,
  // dummy pseudo-format meaning "use the other format".
  // For example, if SrcFormat=Auto and DstFormat=RGB8, then the source
  // is implicitly treated as being RGB8 itself.
  Auto,
  // 1-channel formats
  A8,
  A16F,  // OES_texture_half_float
  A32F,  // OES_texture_float
  R8,
  R16F,  // OES_texture_half_float
  R32F,  // OES_texture_float
  // 2-channel formats
  RA8,
  RA16F,  // OES_texture_half_float
  RA32F,  // OES_texture_float
  RG8,
  RG16F,
  RG32F,
  // 3-channel formats
  RGB8,
  RGB565,
  RGB11F11F10F,
  RGB16F,  // OES_texture_half_float
  RGB32F,  // OES_texture_float
  // 4-channel formats
  RGBA8,
  RGBA5551,
  RGBA4444,
  RGBA16F,  // OES_texture_half_float
  RGBA32F,  // OES_texture_float
  // DOM element source only formats.
  RGBX8,
  BGRX8,
  BGRA8
};

enum class WebGLTexImageFunc : uint8_t {
  TexImage,
  TexSubImage,
  CopyTexImage,
  CopyTexSubImage,
  CompTexImage,
  CompTexSubImage,
};

enum class WebGLTexDimensions : uint8_t { Tex2D, Tex3D };

// Please keep extensions in alphabetic order.
enum class WebGLExtensionID : uint8_t {
  ANGLE_instanced_arrays,
  EXT_blend_minmax,
  EXT_clip_control,
  EXT_color_buffer_float,
  EXT_color_buffer_half_float,
  EXT_depth_clamp,
  EXT_disjoint_timer_query,
  EXT_float_blend,
  EXT_frag_depth,
  EXT_polygon_offset_clamp,
  EXT_shader_texture_lod,
  EXT_sRGB,
  EXT_texture_compression_bptc,
  EXT_texture_compression_rgtc,
  EXT_texture_filter_anisotropic,
  EXT_texture_mirror_clamp_to_edge,
  EXT_texture_norm16,
  MOZ_debug,
  OES_draw_buffers_indexed,
  OES_element_index_uint,
  OES_fbo_render_mipmap,
  OES_standard_derivatives,
  OES_texture_float,
  OES_texture_float_linear,
  OES_texture_half_float,
  OES_texture_half_float_linear,
  OES_vertex_array_object,
  OVR_multiview2,
  WEBGL_color_buffer_float,
  WEBGL_compressed_texture_astc,
  WEBGL_compressed_texture_etc,
  WEBGL_compressed_texture_etc1,
  WEBGL_compressed_texture_pvrtc,
  WEBGL_compressed_texture_s3tc,
  WEBGL_compressed_texture_s3tc_srgb,
  WEBGL_debug_renderer_info,
  WEBGL_debug_shaders,
  WEBGL_depth_texture,
  WEBGL_draw_buffers,
  WEBGL_explicit_present,
  WEBGL_lose_context,
  WEBGL_provoking_vertex,
  Max
};

class UniqueBuffer final {
  // Like unique_ptr<>, but for void* and malloc/calloc/free.
  void* mBuffer = nullptr;

 public:
  static inline UniqueBuffer Take(void* buffer) {
    UniqueBuffer ret;
    ret.mBuffer = buffer;
    return ret;
  }

  UniqueBuffer() = default;

  ~UniqueBuffer() { reset(); }

  UniqueBuffer(UniqueBuffer&& rhs) { *this = std::move(rhs); }

  UniqueBuffer& operator=(UniqueBuffer&& rhs) {
    reset();
    this->mBuffer = rhs.mBuffer;
    rhs.mBuffer = nullptr;
    return *this;
  }

  explicit operator bool() const { return bool(mBuffer); }

  void* get() const { return mBuffer; }

  void reset() {
    // Believe it or not, when `free` unconditional, it was showing up
    // in profiles, nearly 20% of time spent in MethodDispatcther<UniformData>
    // on Aquarium.
    if (mBuffer) {
      free(mBuffer);
      mBuffer = nullptr;
    }
  }
};

namespace webgl {
struct FormatUsageInfo;

static constexpr GLenum kErrorPerfWarning = 0x10001;

struct SampleableInfo final {
  const char* incompleteReason = nullptr;
  uint32_t levels = 0;
  const webgl::FormatUsageInfo* usage = nullptr;
  bool isDepthTexCompare = false;

  bool IsComplete() const { return bool(levels); }
};

MOZ_DEFINE_ENUM_CLASS_WITH_BASE(AttribBaseType, uint8_t,
                                (Boolean,  // Can convert from anything.
                                 Float,    // Also includes NormU?Int
                                 Int, Uint))

webgl::AttribBaseType ToAttribBaseType(GLenum);
const char* ToString(AttribBaseType);

enum class UniformBaseType : uint8_t {
  Float,
  Int,
  Uint,
};
const char* ToString(UniformBaseType);

using ObjectId = uint64_t;

enum class BufferKind : uint8_t {
  Undefined,
  Index,
  NonIndex,
};

}  // namespace webgl

// -

struct FloatOrInt final  // For TexParameter[fi] and friends.
{
  bool isFloat = false;
  uint8_t padding[3] = {};
  GLfloat f = 0;
  GLint i = 0;

  explicit FloatOrInt(GLint x = 0) : isFloat(false), f(x), i(x) {}

  explicit FloatOrInt(GLfloat x) : isFloat(true), f(x), i(roundf(x)) {}

  auto MutTiedFields() { return std::tie(isFloat, padding, f, i); }
};

// -

struct WebGLContextOptions final {
  bool alpha = true;
  bool depth = true;
  bool stencil = false;
  bool premultipliedAlpha = true;

  bool antialias = true;
  bool preserveDrawingBuffer = false;
  bool failIfMajorPerformanceCaveat = false;
  bool xrCompatible = false;

  dom::WebGLPowerPreference powerPreference =
      dom::WebGLPowerPreference::Default;
  bool forceSoftwareRendering = false;
  bool shouldResistFingerprinting = true;
  bool enableDebugRendererInfo = false;

  auto MutTiedFields() {
    // clang-format off
    return std::tie(
      alpha,
      depth,
      stencil,
      premultipliedAlpha,

      antialias,
      preserveDrawingBuffer,
      failIfMajorPerformanceCaveat,
      xrCompatible,

      powerPreference,
      forceSoftwareRendering,
      shouldResistFingerprinting,
      enableDebugRendererInfo);
    // clang-format on
  }

  // -

  WebGLContextOptions();
  WebGLContextOptions(const WebGLContextOptions&) = default;

  using Self = WebGLContextOptions;
  friend bool operator==(const Self& a, const Self& b) {
    return TiedFields(a) == TiedFields(b);
  }
  friend bool operator!=(const Self& a, const Self& b) { return !(a == b); }
};

namespace gfx {

inline ColorSpace2 ToColorSpace2(const dom::PredefinedColorSpace cs) {
  switch (cs) {
    case dom::PredefinedColorSpace::Srgb:
      return ColorSpace2::SRGB;
    case dom::PredefinedColorSpace::Display_p3:
      return ColorSpace2::DISPLAY_P3;
  }
  MOZ_CRASH("Exhaustive switch");
}

}  // namespace gfx

// -

template <typename _T>
struct avec2 {
  using T = _T;

  T x = T();
  T y = T();

  auto MutTiedFields() { return std::tie(x, y); }

  template <typename U, typename V>
  static Maybe<avec2> From(const U _x, const V _y) {
    const auto x = CheckedInt<T>(_x);
    const auto y = CheckedInt<T>(_y);
    if (!x.isValid() || !y.isValid()) return {};
    return Some(avec2(x.value(), y.value()));
  }

  template <typename U>
  static auto From(const U& val) {
    return From(val.x, val.y);
  }
  template <typename U>
  static auto FromSize(const U& val) {
    return From(val.width, val.height);
  }

  avec2() = default;
  avec2(const T _x, const T _y) : x(_x), y(_y) {}

  bool operator==(const avec2& rhs) const = default;

#define _(OP)                                 \
  avec2 operator OP(const avec2& rhs) const { \
    return {x OP rhs.x, y OP rhs.y};          \
  }                                           \
  avec2 operator OP(const T rhs) const { return {x OP rhs, y OP rhs}; }

  _(+)
  _(-)
  _(*)
  _(/)

#undef _

  avec2 Clamp(const avec2& min, const avec2& max) const {
    return {std::clamp(x, min.x, max.x), std::clamp(y, min.y, max.y)};
  }

  template <typename U>
  U StaticCast() const {
    return {static_cast<typename U::T>(x), static_cast<typename U::T>(y)};
  }
};

template <typename T>
avec2<T> MinExtents(const avec2<T>& a, const avec2<T>& b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y)};
}

template <typename T>
avec2<T> MaxExtents(const avec2<T>& a, const avec2<T>& b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

// -

template <typename _T>
struct avec3 {
  using T = _T;

  T x = T();
  T y = T();
  T z = T();

  auto MutTiedFields() { return std::tie(x, y, z); }

  template <typename U, typename V>
  static Maybe<avec3> From(const U _x, const V _y, const V _z) {
    const auto x = CheckedInt<T>(_x);
    const auto y = CheckedInt<T>(_y);
    const auto z = CheckedInt<T>(_z);
    if (!x.isValid() || !y.isValid() || !z.isValid()) return {};
    return Some(avec3(x.value(), y.value(), z.value()));
  }

  template <typename U>
  static auto From(const U& val) {
    return From(val.x, val.y, val.z);
  }

  avec3() = default;
  avec3(const T _x, const T _y, const T _z) : x(_x), y(_y), z(_z) {}

  bool operator==(const avec3& rhs) const = default;
};

using ivec2 = avec2<int32_t>;
using ivec3 = avec3<int32_t>;
using uvec2 = avec2<uint32_t>;
using uvec3 = avec3<uint32_t>;

inline ivec2 AsVec(const gfx::IntSize& s) { return {s.width, s.height}; }

// -

namespace webgl {

struct PackingInfo final {
  GLenum format = 0;
  GLenum type = 0;

  auto MutTiedFields() { return std::tie(format, type); }

  using Self = PackingInfo;
  friend bool operator<(const Self& a, const Self& b) {
    return TiedFields(a) < TiedFields(b);
  }
  friend bool operator==(const Self& a, const Self& b) {
    return TiedFields(a) == TiedFields(b);
  }
  friend bool operator!=(const Self& a, const Self& b) {
    return TiedFields(a) != TiedFields(b);
  }

  template <class T>
  friend T& operator<<(T& s, const PackingInfo& pi) {
    s << "PackingInfo{format: " << EnumString(pi.format)
      << ", type: " << EnumString(pi.type) << "}";
    return s;
  }
};
std::string format_as(const PackingInfo& pi);

struct DriverUnpackInfo final {
  using Self = DriverUnpackInfo;

  GLenum internalFormat = 0;
  GLenum unpackFormat = 0;
  GLenum unpackType = 0;

  PackingInfo ToPacking() const { return {unpackFormat, unpackType}; }

  template <class ConstOrMutSelf>
  static constexpr auto Fields(ConstOrMutSelf& self) {
    return std::tie(self.internalFormat, self.unpackFormat, self.unpackType);
  }

  constexpr bool operator==(const Self& rhs) const {
    return Fields(*this) == Fields(rhs);
  }
  constexpr bool operator!=(const Self& rhs) const {
    return Fields(*this) != Fields(rhs);
  }
};

// -

template <typename E>
class EnumMask {
 public:
  uint64_t mBits = 0;

 private:
  struct BitRef final {
    EnumMask& bits;
    const uint64_t mask;

    explicit operator bool() const { return bits.mBits & mask; }

    auto& operator=(const bool val) {
      if (val) {
        bits.mBits |= mask;
      } else {
        bits.mBits &= ~mask;
      }
      return *this;
    }
  };

  uint64_t Mask(const E i) const {
    return uint64_t{1} << static_cast<uint64_t>(i);
  }

 public:
  BitRef operator[](const E i) { return {*this, Mask(i)}; }
  bool operator[](const E i) const { return mBits & Mask(i); }

  // -

  auto MutTiedFields() { return std::tie(mBits); }
};

using ExtensionBits = EnumMask<WebGLExtensionID>;

// -

enum class ContextLossReason : uint8_t {
  None,
  Manual,
  Guilty,
};

// -

struct InitContextDesc final {
  bool isWebgl2 = false;
  bool resistFingerprinting = false;
  std::array<uint8_t, 2> _padding;
  uint32_t principalKey = 0;
  uvec2 size = {};
  WebGLContextOptions options;

  auto MutTiedFields() {
    return std::tie(isWebgl2, resistFingerprinting, _padding, principalKey,
                    size, options);
  }
};

constexpr uint32_t kMaxTransformFeedbackSeparateAttribs = 4;

// This matches ANGLE's IMPLEMENTATION_MAX_UNIFORM_BLOCK_SIZE.
constexpr uint32_t kMaxUniformBlockSize = 64 * 1024;

struct Limits final {
  ExtensionBits supportedExtensions;

  // WebGL 1
  uint32_t maxTexUnits = 0;
  uint32_t maxTex2dSize = 0;
  uint32_t maxTexCubeSize = 0;
  uint32_t maxVertexAttribs = 0;
  uint32_t maxViewportDim = 0;
  std::array<float, 2> pointSizeRange = {{1, 1}};
  std::array<float, 2> lineWidthRange = {{1, 1}};

  // WebGL 2
  uint32_t maxTexArrayLayers = 0;
  uint32_t maxTex3dSize = 0;
  uint32_t maxUniformBufferBindings = 0;
  uint32_t uniformBufferOffsetAlignment = 0;

  // Exts
  bool astcHdr = false;
  std::array<uint8_t, 3> _padding = {};
  uint32_t maxColorDrawBuffers = 1;
  uint32_t maxMultiviewLayers = 0;
  uint64_t queryCounterBitsTimeElapsed = 0;
  uint64_t queryCounterBitsTimestamp = 0;

  auto MutTiedFields() {
    return std::tie(supportedExtensions,

                    maxTexUnits, maxTex2dSize, maxTexCubeSize, maxVertexAttribs,
                    maxViewportDim, pointSizeRange, lineWidthRange,

                    maxTexArrayLayers, maxTex3dSize, maxUniformBufferBindings,
                    uniformBufferOffsetAlignment,

                    astcHdr, _padding, maxColorDrawBuffers, maxMultiviewLayers,
                    queryCounterBitsTimeElapsed, queryCounterBitsTimestamp);
  }
};

// -
namespace details {
template <class T, size_t Padding>
struct PaddedBase {
 protected:
  T val = {};

 private:
  uint8_t padding[Padding] = {};
};

template <class T>
struct PaddedBase<T, 0> {
 protected:
  T val = {};
};
}  // namespace details

template <class T, size_t PaddedSize>
struct Padded : details::PaddedBase<T, PaddedSize - sizeof(T)> {
  static_assert(PaddedSize >= sizeof(T));

  // Try to be invisible:
  operator T&() { return this->val; }
  operator const T&() const { return this->val; }

  auto& operator=(const T& rhs) { return this->val = rhs; }
  auto& operator=(T&& rhs) { return this->val = std::move(rhs); }

  auto& operator*() { return this->val; }
  auto& operator*() const { return this->val; }
  auto operator->() { return &this->val; }
  auto operator->() const { return &this->val; }
};

// -

enum class OptionalRenderableFormatBits : uint8_t {
  RGB8 = (1 << 0),
  SRGB8 = (1 << 1),
};
constexpr auto kAllOptionalRenderableFormatBits =
    OptionalRenderableFormatBits((1 << 2) - 1);

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(OptionalRenderableFormatBits)

// -

using GetShaderPrecisionFormatArgs = std::tuple<GLenum, GLenum>;

template <class Tuple>
struct TupleStdHash {
  size_t operator()(const Tuple& t) const {
    return std::apply(
        [](const auto&... field) {
          return (std::hash<std::remove_cvref_t<decltype(field)>>{}(field) ^
                  ...);
        },
        t);
  }
};

struct ShaderPrecisionFormat final {
  // highp float: [127, 127, 23]
  // highp int: [31, 30, 0]
  uint8_t rangeMin = 0;  // highp float: +127 (meaning 2^-127)
  uint8_t rangeMax = 0;
  uint8_t precision = 0;
  uint8_t _padding = 0;

  auto MutTiedFields() {
    return std::tie(rangeMin, rangeMax, precision, _padding);
  }
};

// -

struct InitContextResult final {
  Padded<std::string, 32> error;  // MINGW 32-bit needs this padding.
  WebGLContextOptions options;
  gl::GLVendor vendor;
  OptionalRenderableFormatBits optionalRenderableFormatBits;
  std::array<uint8_t, 2> _padding = {};
  Limits limits;
  EnumMask<layers::SurfaceDescriptor::Type> uploadableSdTypes;
  // Padded because of "Android 5.0 ARMv7" builds:
  Padded<std::unordered_map<GetShaderPrecisionFormatArgs, ShaderPrecisionFormat,
                            TupleStdHash<GetShaderPrecisionFormatArgs>>,
         64>
      shaderPrecisions;

  auto MutTiedFields() {
    return std::tie(error, options, vendor, optionalRenderableFormatBits,
                    _padding, limits, uploadableSdTypes, shaderPrecisions);
  }
};

// -

struct ErrorInfo final {
  GLenum type;
  std::string info;
};

// -

enum class LossStatus {
  Ready,

  Lost,
  LostForever,
  LostManually,
};

// -

struct CompileResult final {
  bool pending = true;
  nsCString log;
  nsCString translatedSource;
  bool success = false;
};

// -

struct OpaqueFramebufferOptions final {
  bool depthStencil = true;
  bool antialias = true;
  std::array<uint8_t, 2> _padding;
  uint32_t width = 0;
  uint32_t height = 0;

  auto MutTiedFields() {
    return std::tie(depthStencil, antialias, _padding, width, height);
  }
};

// -

struct SwapChainOptions final {
  layers::RemoteTextureId remoteTextureId;
  layers::RemoteTextureOwnerId remoteTextureOwnerId;
  bool bgra = false;
  bool forceAsyncPresent = false;
  // Pad to sizeof(u64):
  uint16_t padding1 = 0;
  uint32_t padding2 = 0;

  auto MutTiedFields() {
    return std::tie(remoteTextureId, remoteTextureOwnerId, bgra,
                    forceAsyncPresent, padding1, padding2);
  }
};

// -

struct ActiveInfo {
  GLenum elemType = 0;     // `type`
  uint32_t elemCount = 0;  // `size`
  std::string name;
};

struct ActiveAttribInfo final : public ActiveInfo {
  int32_t location = -1;
  AttribBaseType baseType = AttribBaseType::Float;
};

struct ActiveUniformInfo final : public ActiveInfo {
  std::unordered_map<uint32_t, uint32_t>
      locByIndex;  // Uniform array locations are sparse.
  int32_t block_index = -1;
  int32_t block_offset = -1;  // In block, offset.
  int32_t block_arrayStride = -1;
  int32_t block_matrixStride = -1;
  bool block_isRowMajor = false;
};

struct ActiveUniformBlockInfo final {
  std::string name;
  // BLOCK_BINDING is dynamic state
  uint32_t dataSize = 0;
  std::vector<uint32_t> activeUniformIndices;
  bool referencedByVertexShader = false;
  bool referencedByFragmentShader = false;
};

struct LinkActiveInfo final {
  std::vector<ActiveAttribInfo> activeAttribs;
  std::vector<ActiveUniformInfo> activeUniforms;
  std::vector<ActiveUniformBlockInfo> activeUniformBlocks;
  std::vector<ActiveInfo> activeTfVaryings;
};

struct LinkResult final : public SupportsWeakPtr {
  LinkResult() = default;
  ~LinkResult() = default;

  bool pending = true;
  nsCString log;
  bool success = false;
  LinkActiveInfo active;
  GLenum tfBufferMode = 0;
};

// -

/// 4x32-bit primitives, with a type tag.
struct TypedQuad final {
  alignas(alignof(float)) std::array<uint8_t, 4 * sizeof(float)> data = {};
  webgl::AttribBaseType type = webgl::AttribBaseType::Float;
  uint8_t padding[3] = {};

  constexpr auto MutTiedFields() { return std::tie(data, type, padding); }
};

/// [1-16]x32-bit primitives, with a type tag.
struct GetUniformData final {
  alignas(alignof(float)) std::array<uint8_t, 4 * 4 * sizeof(float)> data = {};
  GLenum type = 0;
};

struct FrontBufferSnapshotIpc final {
  uvec2 surfSize = {};
  size_t byteStride = 0;
  mozilla::ipc::MutableSharedMemoryHandle shmem;
};

struct ReadPixelsResult {
  gfx::IntRect subrect = {};
  size_t byteStride = 0;
};

struct ReadPixelsResultIpc final : public ReadPixelsResult {
  Maybe<mozilla::ipc::Shmem> shmem = {};
};

struct VertAttribPointerDesc final {
  bool intFunc = false;
  uint8_t channels = 4;
  bool normalized = false;
  uint8_t byteStrideOrZero = 0;
  GLenum type = LOCAL_GL_FLOAT;
  uint64_t byteOffset = 0;

  auto MutTiedFields() {
    return std::tie(intFunc, channels, normalized, byteStrideOrZero, type,
                    byteOffset);
  }
};

struct VertAttribPointerCalculated final {
  uint8_t byteSize = 4 * 4;
  uint8_t byteStride = 4 * 4;  // at-most 255
  webgl::AttribBaseType baseType = webgl::AttribBaseType::Float;
};

}  // namespace webgl

template <class T>
inline mozilla::Range<T> ShmemRange(const mozilla::ipc::Shmem& shmem) {
  return {shmem.get<T>(), shmem.Size<T>()};
}

// -

template <typename C, typename K>
inline auto MaybeFind(C& container, const K& key)
    -> decltype(&(container.find(key)->second)) {
  const auto itr = container.find(key);
  if (itr == container.end()) return nullptr;
  return &(itr->second);
}

template <typename C, typename K>
inline typename C::mapped_type Find(
    const C& container, const K& key,
    const typename C::mapped_type notFound = {}) {
  const auto itr = container.find(key);
  if (itr == container.end()) return notFound;
  return itr->second;
}

// -

template <typename T, typename U>
inline Maybe<T> MaybeAs(const U val) {
  const auto checked = CheckedInt<T>(val);
  if (!checked.isValid()) return {};
  return Some(checked.value());
}

// -

inline GLenum IsTexMipmapFilter(const GLenum texFilter) {
  switch (texFilter) {
    case LOCAL_GL_NEAREST_MIPMAP_NEAREST:
    case LOCAL_GL_LINEAR_MIPMAP_NEAREST:
    case LOCAL_GL_NEAREST_MIPMAP_LINEAR:
    case LOCAL_GL_LINEAR_MIPMAP_LINEAR:
      return true;
  }
  return false;
}

inline GLenum IsTexImageTarget(const GLenum imageTarget) {
  switch (imageTarget) {
    case LOCAL_GL_TEXTURE_2D:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case LOCAL_GL_TEXTURE_3D:
    case LOCAL_GL_TEXTURE_2D_ARRAY:
      return true;
  }
  return false;
}

inline GLenum ImageToTexTarget(const GLenum imageTarget) {
  switch (imageTarget) {
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
      return LOCAL_GL_TEXTURE_CUBE_MAP;
  }
  if (IsTexImageTarget(imageTarget)) {
    return imageTarget;
  }
  return 0;
}

inline bool IsTexTarget3D(const GLenum texTarget) {
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_2D_ARRAY:
    case LOCAL_GL_TEXTURE_3D:
      return true;

    default:
      return false;
  }
}

// -

namespace dom {
class Element;
class ImageBitmap;
class ImageData;
class OffscreenCanvas;
class VideoFrame;
}  // namespace dom

struct TexImageSource {
  const dom::ArrayBufferView* mView = nullptr;
  GLuint mViewElemOffset = 0;
  GLuint mViewElemLengthOverride = 0;

  const WebGLintptr* mPboOffset = nullptr;

  const dom::ImageBitmap* mImageBitmap = nullptr;
  const dom::ImageData* mImageData = nullptr;

  const dom::OffscreenCanvas* mOffscreenCanvas = nullptr;

  const dom::VideoFrame* mVideoFrame = nullptr;

  const dom::Element* mDomElem = nullptr;
  ErrorResult* mOut_error = nullptr;
};

namespace webgl {

template <class DerivedT>
struct DeriveNotEq {
  bool operator!=(const DerivedT& rhs) const {
    const auto self = reinterpret_cast<const DerivedT*>(this);
    return !(*self == rhs);
  }
};

struct PixelPackingState : public DeriveNotEq<PixelPackingState> {
  uint32_t alignmentInTypeElems = 4;  // ALIGNMENT isn't naive byte alignment!
  uint32_t rowLength = 0;
  uint32_t imageHeight = 0;
  uint32_t skipPixels = 0;
  uint32_t skipRows = 0;
  uint32_t skipImages = 0;

  auto MutTiedFields() {
    return std::tie(alignmentInTypeElems, rowLength, imageHeight, skipPixels,
                    skipRows, skipImages);
  }

  using Self = PixelPackingState;
  friend bool operator==(const Self& a, const Self& b) {
    return TiedFields(a) == TiedFields(b);
  }

  static void AssertDefaultUnpack(gl::GLContext& gl, const bool isWebgl2) {
    PixelPackingState{}.AssertCurrentUnpack(gl, isWebgl2);
  }

  void ApplyUnpack(gl::GLContext&, bool isWebgl2,
                   const uvec3& uploadSize) const;
  bool AssertCurrentUnpack(gl::GLContext&, bool isWebgl2) const;
};

struct PixelUnpackStateWebgl final : public PixelPackingState {
  GLenum colorspaceConversion =
      dom::WebGLRenderingContext_Binding::BROWSER_DEFAULT_WEBGL;
  bool flipY = false;
  bool premultiplyAlpha = false;
  bool requireFastPath = false;
  uint8_t padding = {};

  auto MutTiedFields() {
    return std::tuple_cat(PixelPackingState::MutTiedFields(),
                          std::tie(colorspaceConversion, flipY,
                                   premultiplyAlpha, requireFastPath, padding));
  }
};

struct ExplicitPixelPackingState final {
  struct Metrics final {
    uvec3 usedSize = {};
    size_t bytesPerPixel = 0;

    // (srcStrideAndRowOverride.x, otherwise ROW_LENGTH != 0, otherwise size.x)
    // ...aligned to ALIGNMENT.
    size_t bytesPerRowStride = 0;

    // SKIP_PIXELS+size.x
    size_t usedPixelsPerRow = 0;

    // structuredSrcSize.y, otherwise IMAGE_HEIGHT*(SKIP_IMAGES+size.z)
    size_t totalRows = 0;

    // This ensures that no one else needs to do CheckedInt math.
    size_t totalBytesUsed = 0;
    size_t totalBytesStrided = 0;
  };

  // It's so important that these aren't modified once evaluated.
  const PixelPackingState state;
  const Metrics metrics;

  static Result<ExplicitPixelPackingState, std::string> ForUseWith(
      const PixelPackingState&, GLenum target, const uvec3& subrectSize,
      const webgl::PackingInfo&, const Maybe<size_t> bytesPerRowStrideOverride);
};

struct ReadPixelsDesc final {
  ivec2 srcOffset;
  uvec2 size;
  PackingInfo pi = {LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE};
  PixelPackingState packState;

  auto MutTiedFields() { return std::tie(srcOffset, size, pi, packState); }
};

}  // namespace webgl

namespace webgl {

struct TexUnpackBlobDesc final {
  GLenum imageTarget = LOCAL_GL_TEXTURE_2D;
  uvec3 size;
  gfxAlphaType srcAlphaType = gfxAlphaType::NonPremult;

  Maybe<Span<const uint8_t>> cpuData;
  Maybe<uint64_t> pboOffset;

  Maybe<uvec2> structuredSrcSize;
  RefPtr<layers::Image> image;
  Maybe<layers::SurfaceDescriptor> sd;
  RefPtr<gfx::SourceSurface> sourceSurf;

  webgl::PixelUnpackStateWebgl unpacking;
  bool applyUnpackTransforms = true;
  gfx::SurfaceFormat destFormat = gfx::SurfaceFormat::UNKNOWN;

  // -

  auto ExplicitUnpacking(const webgl::PackingInfo& pi,
                         const Maybe<size_t> bytesPerRowStrideOverride) const {
    return ExplicitPixelPackingState::ForUseWith(this->unpacking,
                                                 this->imageTarget, this->size,
                                                 pi, bytesPerRowStrideOverride);
  }

  void Shrink(const webgl::PackingInfo&);
};

}  // namespace webgl

// ---------------------------------------
// MakeRange

template <typename T, size_t N>
inline mozilla::Range<const T> MakeRange(T (&arr)[N]) {
  return {arr, N};
}

template <typename T>
inline mozilla::Range<const T> MakeRange(const dom::Sequence<T>& seq) {
  return {seq.Elements(), seq.Length()};
}

// -

constexpr auto kUniversalAlignment = alignof(std::max_align_t);

template <typename T>
inline size_t AlignmentOffset(const size_t alignment, const T posOrPtr) {
  MOZ_ASSERT(alignment);
  const auto begin = reinterpret_cast<uintptr_t>(posOrPtr);
  const auto wholeMultiples = (begin + (alignment - 1)) / alignment;
  const auto aligned = wholeMultiples * alignment;
  return aligned - begin;
}

template <typename T>
inline size_t ByteSize(const mozilla::Range<T>& range) {
  return range.length() * sizeof(T);
}

// -

Maybe<webgl::ErrorInfo> CheckBindBufferRange(
    const GLenum target, const GLuint index, const bool isBuffer,
    const uint64_t offset, const uint64_t size, const webgl::Limits& limits);

Maybe<webgl::ErrorInfo> CheckFramebufferAttach(const GLenum bindImageTarget,
                                               const GLenum curTexTarget,
                                               const uint32_t mipLevel,
                                               const uint32_t zLayerBase,
                                               const uint32_t zLayerCount,
                                               const webgl::Limits& limits);

Result<webgl::VertAttribPointerCalculated, webgl::ErrorInfo>
CheckVertexAttribPointer(bool isWebgl2, const webgl::VertAttribPointerDesc&);

uint8_t ElemTypeComponents(GLenum elemType);

inline std::string ToString(const nsACString& text) {
  return {text.BeginReading(), text.Length()};
}

inline void Memcpy(const RangedPtr<uint8_t>& destBytes,
                   const RangedPtr<const uint8_t>& srcBytes,
                   const size_t byteSize) {
  // Trigger range asserts
  (void)(srcBytes + byteSize);
  (void)(destBytes + byteSize);

  memcpy(destBytes.get(), srcBytes.get(), byteSize);
}

template <class T, class U>
inline void Memcpy(const mozilla::Range<T>* const destRange,
                   const RangedPtr<U>& srcBegin) {
  Memcpy(destRange->begin(), srcBegin, destRange->length());
}
template <class T, class U>
inline void Memcpy(const RangedPtr<T>* const destBegin,
                   const mozilla::Range<U>& srcRange) {
  Memcpy(destBegin, srcRange->begin(), srcRange->length());
}

template <typename Dst, typename Src>
inline void Memcpy(const Span<Dst>* const dest, const Span<Src>& src) {
  MOZ_RELEASE_ASSERT(src.size_bytes() >= dest->size_bytes());
  MOZ_ASSERT(src.size_bytes() == dest->size_bytes());
  memcpy(dest->data(), src.data(), dest->size_bytes());
}

// -

inline bool StartsWith(const std::string_view str,
                       const std::string_view part) {
  return str.find(part) == 0;
}

// -

namespace webgl {

// In theory, this number can be unbounded based on the driver. However, no
// driver appears to expose more than 8. We might as well stop there too, for
// now.
// (http://opengl.gpuinfo.org/gl_stats_caps_single.php?listreportsbycap=GL_MAX_COLOR_ATTACHMENTS)
inline constexpr size_t kMaxDrawBuffers = 8;

union UniformDataVal {
  float f32;
  int32_t i32;
  uint32_t u32;
};

enum class ProvokingVertex : GLenum {
  FirstVertex = LOCAL_GL_FIRST_VERTEX_CONVENTION,
  LastVertex = LOCAL_GL_LAST_VERTEX_CONVENTION,
};

// -

struct BufferAndIndex final {
  const WebGLBuffer* buffer = nullptr;
  uint32_t id = -1;
};

}  // namespace webgl

struct IndexedBufferBinding final {
  RefPtr<WebGLBuffer> mBufferBinding;
  uint64_t mRangeStart = 0;
  uint64_t mRangeSize = 0;

  IndexedBufferBinding();
  ~IndexedBufferBinding();

  uint64_t ByteCount() const;
};

// -

template <class... Args>
inline std::string PrintfStdString(const char* const format,
                                   const Args&... args) {
  const auto nsStr = nsPrintfCString(format, args...);
  return ToString(nsStr);
}

inline const char* ToChars(const bool val) {
  if (val) return "true";
  return "false";
}

template <class To>
struct ReinterpretToSpan {
  template <class FromT>
  static inline constexpr Span<To> From(const Span<FromT>& from) {
    static_assert(sizeof(FromT) == sizeof(To));
    return {reinterpret_cast<To*>(from.data()), from.size()};
  }
};

// -

inline std::string Join(Span<const std::string> ss,
                        const std::string_view& delim) {
  if (!ss.size()) return "";
  auto ret = std::string();
  {
    auto chars = delim.size() * (ss.size() - 1);
    for (const auto& s : ss) {
      chars += s.size();
    }
    ret.reserve(chars);
  }

  ret = ss[0];
  ss = ss.subspan(1);
  for (const auto& s : ss) {
    ret += delim;
    ret += s;
  }
  return ret;
}

inline std::string ToStringWithCommas(uint64_t v) {
  if (!v) return "0";
  std::vector<std::string> chunks;
  while (v) {
    const auto chunk = v % 1000;
    v /= 1000;
    chunks.insert(chunks.begin(), std::to_string(chunk));
  }
  return Join(chunks, ",");
}

// -
// C++17 polyfill implementation from:
// https://en.cppreference.com/w/cpp/container/array/to_array

namespace detail {
template <class T, size_t N, size_t... I>
constexpr std::array<std::remove_cv_t<T>, N> to_array_impl(
    T (&a)[N], std::index_sequence<I...>) {
  return {{a[I]...}};
}

template <class T, size_t N, size_t... I>
constexpr std::array<std::remove_cv_t<T>, N> to_array_impl(
    T (&&a)[N], std::index_sequence<I...>) {
  return {{std::move(a[I])...}};
}
}  // namespace detail

template <class T, size_t N>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N]) {
  return detail::to_array_impl(a, std::make_index_sequence<N>{});
}

template <class T, size_t N>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&&a)[N]) {
  return detail::to_array_impl(std::move(a), std::make_index_sequence<N>{});
}

// -

namespace webgl {

std::unordered_map<GLenum, bool> MakeIsEnabledMap(bool webgl2);

static constexpr uint32_t kMaxClientWaitSyncTimeoutNS =
    1000 * 1000 * 1000;  // 1000ms in ns.

}  // namespace webgl
}  // namespace mozilla

#endif
