/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TexUnpackBlob.h"

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLFormats.h"
#include "WebGLTexelConversions.h"
#include "WebGLTexture.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/gfx/CanvasManagerParent.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/VideoBridgeParent.h"
#include "nsLayoutUtils.h"

namespace mozilla {

bool webgl::PixelPackingState::AssertCurrentUnpack(gl::GLContext& gl,
                                                   const bool isWebgl2) const {
  if (!kIsDebug) return true;

  auto actual = PixelPackingState{};
  gl.GetInt(LOCAL_GL_UNPACK_ALIGNMENT, &actual.alignmentInTypeElems);
  if (isWebgl2) {
    gl.GetInt(LOCAL_GL_UNPACK_ROW_LENGTH, &actual.rowLength);
    gl.GetInt(LOCAL_GL_UNPACK_IMAGE_HEIGHT, &actual.imageHeight);

    gl.GetInt(LOCAL_GL_UNPACK_SKIP_PIXELS, &actual.skipPixels);
    gl.GetInt(LOCAL_GL_UNPACK_SKIP_ROWS, &actual.skipRows);
    gl.GetInt(LOCAL_GL_UNPACK_SKIP_IMAGES, &actual.skipImages);
  }
  if (*this == actual) return true;

  const auto ToStr = [](const PixelPackingState& x) {
    const auto text = nsPrintfCString(
        "%u,%u,%u;%u,%u,%u", x.alignmentInTypeElems, x.rowLength, x.imageHeight,
        x.skipPixels, x.skipRows, x.skipImages);
    return mozilla::ToString(text);
  };

  const auto was = ToStr(actual);
  const auto expected = ToStr(*this);
  gfxCriticalError() << "PixelUnpackStateGl was not current. Was " << was
                     << ". Expected << " << expected << ".";
  return false;
}

void webgl::PixelPackingState::ApplyUnpack(gl::GLContext& gl,
                                           const bool isWebgl2,
                                           const uvec3& uploadSize) const {
  gl.fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT,
                  AssertedCast<GLsizei>(alignmentInTypeElems));
  if (!isWebgl2) return;

  // Re-simplify. (ANGLE seems to have an issue with imageHeight ==
  // uploadSize.y)
  auto rowLengthOrZero = rowLength;
  auto imageHeightOrZero = imageHeight;
  if (rowLengthOrZero == uploadSize.x) {
    rowLengthOrZero = 0;
  }
  if (imageHeightOrZero == uploadSize.y) {
    imageHeightOrZero = 0;
  }

  gl.fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH,
                  AssertedCast<GLsizei>(rowLengthOrZero));
  gl.fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT,
                  AssertedCast<GLsizei>(imageHeightOrZero));

  gl.fPixelStorei(LOCAL_GL_UNPACK_SKIP_PIXELS,
                  AssertedCast<GLsizei>(skipPixels));
  gl.fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS, AssertedCast<GLsizei>(skipRows));
  gl.fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES,
                  AssertedCast<GLsizei>(skipImages));
}

namespace webgl {

static bool IsPIValidForDOM(const webgl::PackingInfo& pi) {
  // https://www.khronos.org/registry/webgl/specs/latest/2.0/#TEXTURE_TYPES_FORMATS_FROM_DOM_ELEMENTS_TABLE

  // Just check for invalid individual formats and types, not combinations.
  switch (pi.format) {
    case LOCAL_GL_RGB:
    case LOCAL_GL_RGBA:
    case LOCAL_GL_LUMINANCE_ALPHA:
    case LOCAL_GL_LUMINANCE:
    case LOCAL_GL_ALPHA:
    case LOCAL_GL_RED:
    case LOCAL_GL_RED_INTEGER:
    case LOCAL_GL_RG:
    case LOCAL_GL_RG_INTEGER:
    case LOCAL_GL_RGB_INTEGER:
    case LOCAL_GL_RGBA_INTEGER:
      break;

    case LOCAL_GL_SRGB:
    case LOCAL_GL_SRGB_ALPHA:
      // Allowed in WebGL1+EXT_srgb
      break;

    default:
      return false;
  }

  switch (pi.type) {
    case LOCAL_GL_UNSIGNED_BYTE:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
    case LOCAL_GL_FLOAT:
    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
      break;

    default:
      return false;
  }

  return true;
}

static bool ValidatePIForDOM(const WebGLContext* const webgl,
                             const webgl::PackingInfo& pi) {
  if (!IsPIValidForDOM(pi)) {
    webgl->ErrorInvalidValue("Format or type is invalid for DOM sources.");
    return false;
  }
  return true;
}

static WebGLTexelFormat FormatForPackingInfo(const PackingInfo& pi) {
  switch (pi.type) {
    case LOCAL_GL_UNSIGNED_BYTE:
      switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
        case LOCAL_GL_RED_INTEGER:
          return WebGLTexelFormat::R8;

        case LOCAL_GL_ALPHA:
          return WebGLTexelFormat::A8;

        case LOCAL_GL_LUMINANCE_ALPHA:
          return WebGLTexelFormat::RA8;

        case LOCAL_GL_RGB:
        case LOCAL_GL_RGB_INTEGER:
        case LOCAL_GL_SRGB:
          return WebGLTexelFormat::RGB8;

        case LOCAL_GL_RGBA:
        case LOCAL_GL_RGBA_INTEGER:
        case LOCAL_GL_SRGB_ALPHA:
          return WebGLTexelFormat::RGBA8;

        case LOCAL_GL_RG:
        case LOCAL_GL_RG_INTEGER:
          return WebGLTexelFormat::RG8;

        default:
          break;
      }
      break;

    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
      if (pi.format == LOCAL_GL_RGB) return WebGLTexelFormat::RGB565;
      break;

    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
      if (pi.format == LOCAL_GL_RGBA) return WebGLTexelFormat::RGBA5551;
      break;

    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
      if (pi.format == LOCAL_GL_RGBA) return WebGLTexelFormat::RGBA4444;
      break;

    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
      switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
          return WebGLTexelFormat::R16F;

        case LOCAL_GL_ALPHA:
          return WebGLTexelFormat::A16F;
        case LOCAL_GL_LUMINANCE_ALPHA:
          return WebGLTexelFormat::RA16F;
        case LOCAL_GL_RG:
          return WebGLTexelFormat::RG16F;
        case LOCAL_GL_RGB:
          return WebGLTexelFormat::RGB16F;
        case LOCAL_GL_RGBA:
          return WebGLTexelFormat::RGBA16F;

        default:
          break;
      }
      break;

    case LOCAL_GL_FLOAT:
      switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
          return WebGLTexelFormat::R32F;

        case LOCAL_GL_ALPHA:
          return WebGLTexelFormat::A32F;
        case LOCAL_GL_LUMINANCE_ALPHA:
          return WebGLTexelFormat::RA32F;
        case LOCAL_GL_RG:
          return WebGLTexelFormat::RG32F;
        case LOCAL_GL_RGB:
          return WebGLTexelFormat::RGB32F;
        case LOCAL_GL_RGBA:
          return WebGLTexelFormat::RGBA32F;

        default:
          break;
      }
      break;

    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
      if (pi.format == LOCAL_GL_RGB) return WebGLTexelFormat::RGB11F11F10F;
      break;

    default:
      break;
  }

  return WebGLTexelFormat::FormatNotSupportingAnyConversion;
}

////////////////////

static uint32_t ZeroOn2D(const GLenum target, const uint32_t val) {
  const bool is2d = !IsTexTarget3D(target);
  if (is2d) return 0;
  return val;
}

static bool ValidateUnpackPixels(const WebGLContext* webgl,
                                 const webgl::PackingInfo& pi,
                                 const uint32_t availRows,
                                 const webgl::TexUnpackBlob& blob) {
  const auto& unpackingRes = blob.mDesc.ExplicitUnpacking(pi, {});
  if (!unpackingRes.isOk()) {
    webgl->ErrorInvalidOperation("%s", unpackingRes.inspectErr().c_str());
    return false;
  }
  const auto& unpacking = unpackingRes.inspect();

  if (availRows < unpacking.metrics.totalRows) {
    webgl->ErrorInvalidOperation(
        "Desired upload requires more rows (%zu) than is"
        " available (%zu).",
        unpacking.metrics.totalRows, availRows);
    return false;
  }

  return true;
}

static bool ValidateUnpackBytes(const WebGLContext* const webgl,
                                const webgl::PackingInfo& pi,
                                const size_t availByteCount,
                                const webgl::TexUnpackBlob& blob) {
  const auto& unpackingRes = blob.mDesc.ExplicitUnpacking(pi, {});
  if (!unpackingRes.isOk()) {
    webgl->ErrorInvalidOperation("%s", unpackingRes.inspectErr().c_str());
    return false;
  }
  const auto& unpacking = unpackingRes.inspect();

  if (availByteCount < unpacking.metrics.totalBytesUsed) {
    webgl->ErrorInvalidOperation(
        "Desired upload requires more bytes (%zu) than are"
        " available (%zu).",
        unpacking.metrics.totalBytesUsed, availByteCount);
    return false;
  }

  return true;
}

////////////////////

// Check if the surface descriptor describes a memory which contains a single
// RGBA data source.
static bool SDIsRGBBuffer(const layers::SurfaceDescriptor& sd) {
  return sd.type() == layers::SurfaceDescriptor::TSurfaceDescriptorBuffer &&
         sd.get_SurfaceDescriptorBuffer().desc().type() ==
             layers::BufferDescriptor::TRGBDescriptor;
}

// Check if the surface descriptor describes a GPUVideo texture for which we
// only have an opaque source/handle from SurfaceDescriptorRemoteDecoder to
// derive the actual texture from.
static bool SDIsRemoteDecoderBuffer(const layers::SurfaceDescriptor& sd) {
  return sd.type() == layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo &&
         sd.get_SurfaceDescriptorGPUVideo()
                 .get_SurfaceDescriptorRemoteDecoder()
                 .videoType() == layers::RemoteDecoderVideoType::Buffer;
}

static bool SDIsRemoteDecoderDirectUpload(const layers::SurfaceDescriptor& sd) {
  if (sd.type() != layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo) {
    return false;
  }
  const auto& sdv = sd.get_SurfaceDescriptorGPUVideo();
  if (sdv.type() !=
      layers::SurfaceDescriptorGPUVideo::TSurfaceDescriptorRemoteDecoder) {
    return false;
  }
  const auto& sdrd = sdv.get_SurfaceDescriptorRemoteDecoder();
  if (sdrd.videoType() == layers::RemoteDecoderVideoType::TypeNone ||
      sdrd.videoType() == layers::RemoteDecoderVideoType::Buffer) {
    return false;
  }
  return true;
}

// Check if the surface descriptor describes an ExternalImage surface for which
// we only have an opaque source/handle to derive the actual surface from.
static bool SDIsExternalImage(const layers::SurfaceDescriptor& sd) {
  return sd.type() ==
             layers::SurfaceDescriptor::TSurfaceDescriptorExternalImage &&
         sd.get_SurfaceDescriptorExternalImage().source() ==
             wr::ExternalImageSource::SharedSurfaces;
}

static bool SDIsCanvasSurface(const layers::SurfaceDescriptor& sd) {
  return sd.type() ==
         layers::SurfaceDescriptor::TSurfaceDescriptorCanvasSurface;
}

// static
std::unique_ptr<TexUnpackBlob> TexUnpackBlob::Create(
    const TexUnpackBlobDesc& desc) {
  return std::unique_ptr<TexUnpackBlob>{[&]() -> TexUnpackBlob* {
    if (!IsTarget3D(desc.imageTarget) && desc.size.z != 1) {
      MOZ_ASSERT(false);
      return nullptr;
    }

    switch (desc.unpacking.alignmentInTypeElems) {
      case 1:
      case 2:
      case 4:
      case 8:
        break;
      default:
        MOZ_ASSERT(false);
        return nullptr;
    }

    if (desc.sd || desc.sourceSurf) {
      return new TexUnpackSurface(desc);
    }

    if (desc.srcAlphaType != gfxAlphaType::NonPremult) {
      MOZ_ASSERT(false);
      return nullptr;
    }
    return new TexUnpackBytes(desc);
  }()};
}

static bool HasColorAndAlpha(const WebGLTexelFormat format) {
  switch (format) {
    case WebGLTexelFormat::RA8:
    case WebGLTexelFormat::RA16F:
    case WebGLTexelFormat::RA32F:
    case WebGLTexelFormat::RGBA8:
    case WebGLTexelFormat::RGBA5551:
    case WebGLTexelFormat::RGBA4444:
    case WebGLTexelFormat::RGBA16F:
    case WebGLTexelFormat::RGBA32F:
    case WebGLTexelFormat::BGRA8:
      return true;
    default:
      return false;
  }
}

bool TexUnpackBlob::ConvertIfNeeded(
    const WebGLContext* const webgl, const size_t rowLength,
    const size_t rowCount, WebGLTexelFormat srcFormat,
    const uint8_t* const srcBegin, const ptrdiff_t srcStride,
    WebGLTexelFormat dstFormat, const ptrdiff_t dstStride,
    const bool applyUnpackColorSpace, const uint8_t** const out_begin,
    UniqueBuffer* const out_anchoredBuffer) const {
  *out_begin = srcBegin;

  // TexelBytesForFormat() has no size for these, so the stride validation below
  // would be vacuous. Bail before it rather than relying on ConvertImage() to
  // reject them.
  if (srcFormat == WebGLTexelFormat::FormatNotSupportingAnyConversion ||
      dstFormat == WebGLTexelFormat::FormatNotSupportingAnyConversion) {
    webgl->ErrorImplementationBug("Unconvertible texel format.");
    return false;
  }

  const auto& unpacking = mDesc.unpacking;

  if (!rowLength || !rowCount) return true;

  auto minSrcStride =
      CheckedInt<size_t>(
          WebGLTexelConversions::TexelBytesForFormat(srcFormat)) *
      rowLength;
  auto minDstStride =
      CheckedInt<size_t>(
          WebGLTexelConversions::TexelBytesForFormat(dstFormat)) *
      rowLength;
  if (srcStride <= 0 || dstStride <= 0 || !minSrcStride.isValid() ||
      !minDstStride.isValid() || size_t(srcStride) < minSrcStride.value() ||
      size_t(dstStride) < minDstStride.value()) {
    webgl->ErrorInvalidOperation("Invalid stride.");
    return false;
  }

  const auto srcIsPremult = (mDesc.srcAlphaType == gfxAlphaType::Premult);
  auto dstIsPremult = unpacking.premultiplyAlpha;
  const auto fnHasPremultMismatch = [&]() {
    if (mDesc.srcAlphaType == gfxAlphaType::Opaque) return false;

    if (!HasColorAndAlpha(srcFormat)) return false;

    return srcIsPremult != dstIsPremult;
  };

  const auto srcOrigin =
      (unpacking.flipY ? gl::OriginPos::TopLeft : gl::OriginPos::BottomLeft);
  auto dstOrigin = gl::OriginPos::BottomLeft;

  if (!mDesc.applyUnpackTransforms) {
    dstIsPremult = srcIsPremult;
    dstOrigin = srcOrigin;
  }

  // TODO (Bug 754256): Figure out the source colorSpace.
  dom::PredefinedColorSpace srcColorSpace = dom::PredefinedColorSpace::Srgb;
  dom::PredefinedColorSpace dstColorSpace =
      applyUnpackColorSpace && webgl->mUnpackColorSpace
          ? *webgl->mUnpackColorSpace
          : srcColorSpace;

  if (srcFormat != dstFormat) {
    webgl->GeneratePerfWarning(
        "Conversion requires pixel reformatting. (%u->%u)", uint32_t(srcFormat),
        uint32_t(dstFormat));
  } else if (fnHasPremultMismatch()) {
    webgl->GeneratePerfWarning(
        "Conversion requires change in"
        " alpha-premultiplication.");
  } else if (srcOrigin != dstOrigin) {
    webgl->GeneratePerfWarning("Conversion requires y-flip.");
  } else if (srcStride != dstStride) {
    webgl->GeneratePerfWarning("Conversion requires change in stride. (%u->%u)",
                               uint32_t(srcStride), uint32_t(dstStride));
  } else if (srcColorSpace != dstColorSpace) {
    webgl->GeneratePerfWarning(
        "Conversion requires colorSpace conversion. (%u->%u)",
        uint32_t(srcColorSpace), uint32_t(dstColorSpace));
  } else {
    return true;
  }

  ////

  const auto dstTotalBytes = CheckedInt<size_t>(rowCount) * dstStride;
  if (!dstTotalBytes.isValid()) {
    webgl->ErrorOutOfMemory("Calculation failed.");
    return false;
  }

  auto dstBuffer = UniqueBuffer::Take(calloc(1u, dstTotalBytes.value()));
  if (!dstBuffer.get()) {
    webgl->ErrorOutOfMemory("Failed to allocate dest buffer.");
    return false;
  }
  const auto dstBegin = static_cast<uint8_t*>(dstBuffer.get());

  ////

  // And go!:
  bool wasTrivial;
  if (!ConvertImage(rowLength, rowCount, srcBegin, srcStride, srcOrigin,
                    srcFormat, srcIsPremult, dstBegin, dstStride, dstOrigin,
                    dstFormat, dstIsPremult, srcColorSpace, dstColorSpace,
                    &wasTrivial)) {
    webgl->ErrorImplementationBug("ConvertImage failed.");
    return false;
  }

  *out_begin = dstBegin;
  *out_anchoredBuffer = std::move(dstBuffer);
  return true;
}

static GLenum DoTexOrSubImage(bool isSubImage, gl::GLContext* gl,
                              TexImageTarget target, GLint level,
                              const DriverUnpackInfo* dui, GLint xOffset,
                              GLint yOffset, GLint zOffset, GLsizei width,
                              GLsizei height, GLsizei depth, const void* data) {
  if (isSubImage) {
    return DoTexSubImage(gl, target, level, xOffset, yOffset, zOffset, width,
                         height, depth, dui->ToPacking(), data);
  } else {
    return DoTexImage(gl, target, level, dui, width, height, depth, data);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
// TexUnpackBytes

bool TexUnpackBytes::Validate(const WebGLContext* const webgl,
                              const webgl::PackingInfo& pi) {
  if (!HasData()) return true;

  CheckedInt<size_t> availBytes = 0;
  if (mDesc.cpuData) {
    availBytes = mDesc.cpuData->size();
  } else if (mDesc.pboOffset) {
    const auto& pboOffset = *mDesc.pboOffset;

    const auto& pbo =
        webgl->ValidateBufferSelection(LOCAL_GL_PIXEL_UNPACK_BUFFER);
    if (!pbo) return false;  // Might be invalid e.g. due to in-use by TF.
    availBytes = pbo->ByteLength();
    availBytes -= pboOffset;
  } else {
    MOZ_ASSERT(false, "Must be one of the above");
  }
  if (!availBytes.isValid()) {
    webgl->ErrorInvalidOperation("Offset is passed end of buffer.");
    return false;
  }

  return ValidateUnpackBytes(webgl, pi, availBytes.value(), *this);
}

bool TexUnpackBytes::TexOrSubImage(bool isSubImage, bool needsRespec,
                                   WebGLTexture* tex, GLint level,
                                   const webgl::DriverUnpackInfo* dui,
                                   GLint xOffset, GLint yOffset, GLint zOffset,
                                   const webgl::PackingInfo& pi,
                                   GLenum* const out_error) const {
  const auto& webgl = tex->mContext;
  const auto& target = mDesc.imageTarget;
  const auto& size = mDesc.size;
  const auto& webglUnpackState = mDesc.unpacking;

  const auto unpackingRes = mDesc.ExplicitUnpacking(pi, {});

  const auto format = FormatForPackingInfo(pi);

  const uint8_t* uploadPtr = nullptr;
  if (mDesc.cpuData) {
    uploadPtr = mDesc.cpuData->data();
  } else if (mDesc.pboOffset) {
    uploadPtr = reinterpret_cast<const uint8_t*>(*mDesc.pboOffset);
  }

  UniqueBuffer tempBuffer;

  do {
    if (mDesc.pboOffset || !uploadPtr) break;

    if (!webglUnpackState.flipY && !webglUnpackState.premultiplyAlpha) {
      break;
    }

    webgl->GenerateWarning(
        "Alpha-premult and y-flip are deprecated for"
        " non-DOM-Element uploads.");

    MOZ_RELEASE_ASSERT(unpackingRes.isOk());
    const auto& unpacking = unpackingRes.inspect();
    const auto stride = unpacking.metrics.bytesPerRowStride;
    // unpackColorSpace only applies to TexImageSource uploads.
    // clang-format off
    if (!ConvertIfNeeded(webgl, unpacking.metrics.usedPixelsPerRow,
                         unpacking.metrics.totalRows,
                         format, uploadPtr, AutoAssertCast(stride),
                         format, AutoAssertCast(stride),
                         /* applyUnpackColorSpace */ false,
                         &uploadPtr, &tempBuffer)) {
      return false;
    }
    // clang-format on
  } while (false);

  //////

  const auto& gl = webgl->gl;

  bool useParanoidHandling = false;
  if (mNeedsExactUpload && webgl->mBoundPixelUnpackBuffer) {
    webgl->GenerateWarning(
        "Uploads from a buffer with a final row with a byte"
        " count smaller than the row stride can incur extra"
        " overhead.");

    if (gl->WorkAroundDriverBugs()) {
      useParanoidHandling |= (gl->Vendor() == gl::GLVendor::NVIDIA);
    }
  }

  if (!useParanoidHandling) {
    const ScopedLazyBind bindPBO(gl, LOCAL_GL_PIXEL_UNPACK_BUFFER,
                                 webgl->mBoundPixelUnpackBuffer);

    *out_error =
        DoTexOrSubImage(isSubImage, gl, target, level, dui, xOffset, yOffset,
                        zOffset, size.x, size.y, size.z, uploadPtr);
    return true;
  }

  //////

  MOZ_ASSERT(webgl->mBoundPixelUnpackBuffer);

  if (!isSubImage) {
    // Alloc first to catch OOMs.
    AssertUintParamCorrect(gl, LOCAL_GL_PIXEL_UNPACK_BUFFER_BINDING, 0);
    *out_error =
        DoTexOrSubImage(false, gl, target, level, dui, xOffset, yOffset,
                        zOffset, size.x, size.y, size.z, nullptr);
    if (*out_error) return true;
  }
  if (!size.x || !size.y || !size.z) {
    // Nothing to do.
    return true;
  }

  MOZ_RELEASE_ASSERT(unpackingRes.isOk());
  const auto& unpacking = unpackingRes.inspect();

  const ScopedLazyBind bindPBO(gl, LOCAL_GL_PIXEL_UNPACK_BUFFER,
                               webgl->mBoundPixelUnpackBuffer);

  //////

  // Make our sometimes-implicit values explicit. Also this keeps them constant
  // when we ask for height=mHeight-1 and such.
  gl->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH,
                   AutoAssertCast(unpacking.state.rowLength));
  gl->fPixelStorei(LOCAL_GL_UNPACK_IMAGE_HEIGHT,
                   AutoAssertCast(unpacking.state.imageHeight));

  if (size.z > 1) {
    *out_error =
        DoTexOrSubImage(true, gl, target, level, dui, xOffset, yOffset, zOffset,
                        size.x, size.y, size.z - 1, uploadPtr);
  }

  // Skip the images we uploaded.
  const auto skipImages = ZeroOn2D(target, unpacking.state.skipImages);
  gl->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES, skipImages + size.z - 1);

  if (size.y > 1) {
    *out_error =
        DoTexOrSubImage(true, gl, target, level, dui, xOffset, yOffset,
                        zOffset + size.z - 1, size.x, size.y - 1, 1, uploadPtr);
  }

  // -

  const auto lastRowOffset =
      unpacking.metrics.totalBytesStrided - unpacking.metrics.bytesPerRowStride;
  const auto lastRowPtr =
      mDesc.pboOffset
          ? reinterpret_cast<const uint8_t*>(*mDesc.pboOffset + lastRowOffset)
          : (uploadPtr ? uploadPtr + lastRowOffset : nullptr);

  gl->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 1);    // No stride padding.
  gl->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH, 0);   // No padding in general.
  gl->fPixelStorei(LOCAL_GL_UNPACK_SKIP_IMAGES, 0);  // Don't skip images,
  gl->fPixelStorei(LOCAL_GL_UNPACK_SKIP_ROWS,
                   0);  // or rows.
                        // Keep skipping pixels though!
  *out_error = DoTexOrSubImage(true, gl, target, level, dui, xOffset,
                               yOffset + size.y - 1, zOffset + size.z - 1,
                               AutoAssertCast(size.x), 1, 1, lastRowPtr);

  // Caller will reset all our modified PixelStorei state.

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// TexUnpackSurface

TexUnpackSurface::~TexUnpackSurface() = default;

bool TexUnpackSurface::Validate(const WebGLContext* const webgl,
                                const webgl::PackingInfo& pi) {
  if (!ValidatePIForDOM(webgl, pi)) return false;

  if (!mDesc.structuredSrcSize) {
    gfxCriticalError() << "TexUnpackSurface missing structuredSrcSize.";
    return false;
  }
  const auto& elemSize = *mDesc.structuredSrcSize;
  if (mDesc.sourceSurf) {
    const auto& surfSize = mDesc.sourceSurf->GetSize();
    const auto surfSize2 = ivec2::FromSize(surfSize)->StaticCast<uvec2>();
    if (uvec2{elemSize.x, elemSize.y} != surfSize2) {
      gfxCriticalError()
          << "TexUnpackSurface mismatched structuredSrcSize for sourceSurf.";
      return false;
    }
  }

  const auto fullRows = elemSize.y;
  return ValidateUnpackPixels(webgl, pi, fullRows, *this);
}

const char* BlitPreventReason(
    GLenum target, int32_t level, const ivec3& offset, GLenum internalFormat,
    const webgl::PackingInfo& pi, const TexUnpackBlobDesc& desc,
    OptionalRenderableFormatBits optionalRenderableFormatBits,
    bool sameColorSpace, bool allowConversion, bool allowSRGB, bool allow3D) {
  const auto& size = desc.size;
  const auto& unpacking = desc.unpacking;

  if (size.z != 1) {
    return "depth is not 1";
  }
  if (offset.z != 0) {
    return "zOffset is not 0";
  }

  if (unpacking.skipPixels || unpacking.skipRows || unpacking.skipImages) {
    return "non-zero UNPACK_SKIP_* not yet supported";
  }

  if (desc.srcAlphaType != gfxAlphaType::Opaque) {
    const bool srcIsPremult = (desc.srcAlphaType == gfxAlphaType::Premult);
    const auto& dstIsPremult = unpacking.premultiplyAlpha;
    if (srcIsPremult != dstIsPremult && !allowConversion) {
      return dstIsPremult ? "UNPACK_PREMULTIPLY_ALPHA_WEBGL is not true"
                          : "UNPACK_PREMULTIPLY_ALPHA_WEBGL is not false";
    }
  }

  if (!sameColorSpace) {
    return "not same colorSpace";
  }

  const auto formatReason = [&]() -> const char* {
    if (pi.type != LOCAL_GL_UNSIGNED_BYTE) {
      return "`unpackType` must be `UNSIGNED_BYTE`";
    }

    // GL_FRAMEBUFFER_SRGB is not available in some GLES implementations,
    // which is required for blitting to emulate Tex(Sub)Image, which does no
    // encoding when uploading to an sRGB texture. In GLES (but not desktop
    // GL) blitting to an sRGB framebuffer encodes by default unless
    // explicitly disabled. To workaround the availability of this extension,
    // just disallow it for now. Note, this is also an optional boolean as
    // some internal data transfers intentionally rely on this conversion.
    switch (internalFormat) {
      case LOCAL_GL_SRGB:
      case LOCAL_GL_SRGB8:
      case LOCAL_GL_SRGB_ALPHA:
      case LOCAL_GL_SRGB8_ALPHA8:
        if (!allowSRGB) {
          return "sRGB-encoded internal formats are not supported";
        }
        break;
    }

    switch (pi.format) {
      case LOCAL_GL_RGBA:
        return nullptr;  // All internalFormats for unpackFormat=RGBA are
                         // renderable.

      case LOCAL_GL_RGB:
        break;

      default:
        return "`unpackFormat` must be `RGBA` or maybe `RGB`";
    }

    // -

    struct {
      OptionalRenderableFormatBits bits;
      const char* errorMsg;
    } required;

    switch (internalFormat) {
      case LOCAL_GL_RGB565:
        return nullptr;
      case LOCAL_GL_RGB:
      case LOCAL_GL_RGB8:
        required = {
            OptionalRenderableFormatBits::RGB8,
            "Unavailable, as blitting internalFormats RGB or RGB8 requires "
            "that RGB8 must be a renderable format.",
        };
        break;
      case LOCAL_GL_SRGB:
      case LOCAL_GL_SRGB8:
        required = {
            OptionalRenderableFormatBits::SRGB8,
            "Unavailable, as blitting internalFormats SRGB or SRGB8 requires "
            "that SRGB8 must be a renderable format.",
        };
        break;
      case 0:
        // texSubImage, so internalFormat is unknown, and could be anything!
        required = {
            OptionalRenderableFormatBits::RGB8 |
                OptionalRenderableFormatBits::SRGB8,
            "Unavailable, as blitting texSubImage with unpackFormat=RGB "
            "requires that RGB8 and SRGB8 must be renderable formats.",
        };
        break;
      default:
        gfxCriticalError()
            << "Unexpected internalFormat for unpackFormat=RGB: 0x"
            << gfx::hexa(internalFormat);
        return "Unexpected internalFormat for unpackFormat=RGB";
    }

    const auto availableBits = optionalRenderableFormatBits;
    if ((required.bits | availableBits) != availableBits) {
      return required.errorMsg;
    }

    // -

    return nullptr;
  }();
  if (formatReason) return formatReason;

  // If the texture has multiple faces or layers that are not allocated,
  // then the framebuffer attachment required to blit the descriptor may
  // be determined incomplete. Avoid this for now by restricting to 2D
  // targets.
  if (!allow3D) {
    bool is2D = ImageToTexTarget(target) == target && !IsTexTarget3D(target);
    if (!is2D) {
      return "texture is not 2D";
    }
  }

  return nullptr;
}

bool TexUnpackSurface::AllowBlitSd(WebGLContext* const webgl,
                                   const GLenum target, const int32_t level,
                                   const ivec3& offset,
                                   const GLenum internalFormat,
                                   const webgl::PackingInfo& pi,
                                   bool allowConversion, bool allowSRGB,
                                   bool allow3D, bool warn) const {
  dom::PredefinedColorSpace srcColorSpace = dom::PredefinedColorSpace::Srgb;
  dom::PredefinedColorSpace dstColorSpace =
      webgl->mUnpackColorSpace ? *webgl->mUnpackColorSpace
                               : dom::PredefinedColorSpace::Srgb;
  bool sameColorSpace = srcColorSpace == dstColorSpace;
  if (const char* reason = BlitPreventReason(
          target, level, offset, internalFormat, pi, mDesc,
          webgl->mOptionalRenderableFormatBits, sameColorSpace, allowConversion,
          allowSRGB, allow3D)) {
    if (warn) {
      webgl->GeneratePerfWarning(
          "Failed to hit GPU-copy fast-path."
          " (%s) Falling back to CPU upload.",
          reason);
    }
    return false;
  }
  return true;
}

// The texture may be mipmap incomplete which will prevent the framebuffer
// from being complete while drawing to it. To avoid this scenario, override
// the texture base and max level temporarily to ignore incomplete mipmaps
// while blitting to it. Depending on GL implementation (desktop vs ES), the
// min filter may contribute to mipmap completeness.
struct AutoRestoreMipmapState {
  AutoRestoreMipmapState(gl::GLContext* gl, GLenum target, GLint level)
      : mGL(gl), mTarget(target), mLevel(level) {
    mGL->fGetTexParameteriv(mTarget, LOCAL_GL_TEXTURE_MIN_FILTER, &mMinFilter);
    if (IsTexMipmapFilter(mMinFilter)) {
      mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_MIN_FILTER,
                          LOCAL_GL_NEAREST);
    }
    if (mGL->HasTexParamMipmapLevel()) {
      mGL->fGetTexParameteriv(mTarget, LOCAL_GL_TEXTURE_BASE_LEVEL,
                              &mLevelBase);
      mGL->fGetTexParameteriv(mTarget, LOCAL_GL_TEXTURE_MAX_LEVEL, &mLevelMax);
      if (mLevelBase != mLevel) {
        mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_BASE_LEVEL, mLevel);
      }
      if (mLevelMax != mLevel) {
        mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_MAX_LEVEL, mLevel);
      }
    }
  }

  ~AutoRestoreMipmapState() {
    if (IsTexMipmapFilter(mMinFilter)) {
      mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_MIN_FILTER, mMinFilter);
    }
    if (mGL->HasTexParamMipmapLevel()) {
      if (mLevelBase != mLevel) {
        mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_BASE_LEVEL, mLevelBase);
      }
      if (mLevelMax != mLevel) {
        mGL->fTexParameteri(mTarget, LOCAL_GL_TEXTURE_MAX_LEVEL, mLevelMax);
      }
    }
  }

  gl::GLContext* mGL = nullptr;
  GLenum mTarget = 0;
  GLint mLevel = 0;
  GLint mMinFilter = 0;
  GLint mLevelBase = 0;
  GLint mLevelMax = 0;
};

bool TexUnpackSurface::BlitSd(
    const layers::SurfaceDescriptor& sd, bool isSubImage, bool needsRespec,
    WebGLTexture* tex, GLint level, const webgl::DriverUnpackInfo* dui,
    GLint xOffset, GLint yOffset, GLint zOffset, const webgl::PackingInfo& pi,
    GLenum* const out_error, bool allowFallback) const {
  MOZ_ASSERT_IF(needsRespec, !isSubImage);

  const auto& webgl = tex->mContext;
  const auto& target = mDesc.imageTarget;
  const auto& size = mDesc.size;
  const auto& unpacking = mDesc.unpacking;

  const auto& gl = webgl->GL();

  // -

  if (needsRespec) {
    *out_error =
        DoTexOrSubImage(isSubImage, gl, target, level, dui, xOffset, yOffset,
                        zOffset, size.x, size.y, size.z, nullptr);
    if (*out_error) return true;
  }

  {
    gl::ScopedBindTexture scopedTex(gl, tex->mGLName, target);
    AutoRestoreMipmapState restoreMipmapState(gl, target, level);

    gl::ScopedFramebuffer scopedFB(gl);
    gl::ScopedBindFramebuffer bindFB(gl, scopedFB.FB());

    {
      gl::GLContext::LocalErrorScope errorScope(*gl);

      gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                LOCAL_GL_COLOR_ATTACHMENT0, target,
                                tex->mGLName, level);

      const auto err = errorScope.GetError();
      if (err) {
        if (allowFallback) {
          return false;
        }
        MOZ_DIAGNOSTIC_CRASH("BlitSd failed attaching texture to framebuffer");
      }
    }

    const GLenum status = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
    if (status != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
      if (allowFallback) {
        return false;
      }
      MOZ_DIAGNOSTIC_CRASH("BlitSd framebuffer is not complete");
    }

    const auto dstOrigin =
        (unpacking.flipY ? gl::OriginPos::TopLeft : gl::OriginPos::BottomLeft);
    gfx::IntSize fbSize(size.x, size.y);
    if (isSubImage) {
      const auto& imageInfo = tex->ImageInfoAt(target, level);
      fbSize = gfx::IntSize(imageInfo.mWidth, imageInfo.mHeight);
    }
    Maybe<gfxAlphaType> convertAlpha;
    if (mDesc.srcAlphaType != gfxAlphaType::Opaque) {
      const bool srcIsPremult = mDesc.srcAlphaType == gfxAlphaType::Premult;
      const bool dstIsPremult = unpacking.premultiplyAlpha;
      if (srcIsPremult != dstIsPremult) {
        convertAlpha = Some(dstIsPremult ? gfxAlphaType::Premult
                                         : gfxAlphaType::NonPremult);
      }
    }
    if (!gl->BlitHelper()->BlitSdToFramebuffer(
            sd, {xOffset, yOffset, size.x, size.y}, dstOrigin, fbSize,
            convertAlpha, mDesc.destFormat)) {
      gfxCriticalNote << "BlitSdToFramebuffer failed for type "
                      << int(sd.type());
      if (allowFallback) {
        return false;
      }
      // Maybe the resource isn't valid anymore?
      gl->fClearColor(0.2, 0.0, 0.2, 1.0);
      gl->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
      const auto& cur = webgl->mColorClearValue;
      gl->fClearColor(cur[0], cur[1], cur[2], cur[3]);
      webgl->GenerateWarning(
          "Fast Tex(Sub)Image upload failed without recourse, clearing to "
          "[0.2, 0.0, 0.2, 1.0]. Please file a bug!");
    }
  }

  return true;
}

static bool GetFormatForSurf(const gfx::SourceSurface* surf,
                             WebGLTexelFormat* const out_texelFormat,
                             uint8_t* const out_bpp) {
  const auto surfFormat = surf->GetFormat();
  switch (surfFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
      *out_texelFormat = WebGLTexelFormat::BGRA8;
      *out_bpp = 4;
      return true;

    case gfx::SurfaceFormat::B8G8R8X8:
      *out_texelFormat = WebGLTexelFormat::BGRX8;
      *out_bpp = 4;
      return true;

    case gfx::SurfaceFormat::R8G8B8A8:
      *out_texelFormat = WebGLTexelFormat::RGBA8;
      *out_bpp = 4;
      return true;

    case gfx::SurfaceFormat::R8G8B8X8:
      *out_texelFormat = WebGLTexelFormat::RGBX8;
      *out_bpp = 4;
      return true;

    case gfx::SurfaceFormat::R5G6B5_UINT16:
      *out_texelFormat = WebGLTexelFormat::RGB565;
      *out_bpp = 2;
      return true;

    case gfx::SurfaceFormat::A8:
      *out_texelFormat = WebGLTexelFormat::A8;
      *out_bpp = 1;
      return true;

    case gfx::SurfaceFormat::YUV420:
      // Ugh...
      NS_ERROR("We don't handle uploads from YUV sources yet.");
      // When we want to, check out gfx/ycbcr/YCbCrUtils.h. (specifically
      // GetYCbCrToRGBDestFormatAndSize and ConvertYCbCrToRGB)
      return false;

    default:
      return false;
  }
}

bool TexUnpackSurface::TexOrSubImage(bool isSubImage, bool needsRespec,
                                     WebGLTexture* tex, GLint level,
                                     const webgl::DriverUnpackInfo* dui,
                                     GLint xOffset, GLint yOffset,
                                     GLint zOffset,
                                     const webgl::PackingInfo& dstPI,
                                     GLenum* const out_error) const {
  const auto& webgl = tex->mContext;
  const auto& size = mDesc.size;
  RefPtr<gfx::DataSourceSurface> surf;
  if (mDesc.sd) {
    // First check if the SD describes an RGBA Shmem.
    const auto& sd = *(mDesc.sd);
    if (SDIsCanvasSurface(sd)) {
      // The canvas surface resides on a 2D canvas within the same content
      // process as the WebGL canvas. Query it for the surface.
      const auto& sdc = sd.get_SurfaceDescriptorCanvasSurface();
      uint32_t managerId = sdc.managerId();
      mozilla::ipc::ActorId canvasId = sdc.canvasId();
      uintptr_t surfaceId = sdc.surfaceId();
      Maybe<layers::SurfaceDescriptor> exportSd;
      // If the texture has multiple faces or layers that are not allocated,
      // then the framebuffer attachment required to blit the descriptor may
      // be determined incomplete. Avoid this for now by restricting to 2D
      // targets.
      bool allowBlit = AllowBlitSd(
          webgl, mDesc.imageTarget, level, {xOffset, yOffset, zOffset},
          dui->internalFormat, dstPI, true, false, false, false);
      if (RefPtr<gfx::SourceSurface> data =
              gfx::CanvasManagerParent::TryResolveCanvasSurface(
                  webgl->GetContentId(), managerId, canvasId, surfaceId,
                  allowBlit ? &exportSd : nullptr)) {
        if (exportSd && !SDIsRGBBuffer(*exportSd) &&
            BlitSd(*exportSd, isSubImage, needsRespec, tex, level, dui, xOffset,
                   yOffset, zOffset, dstPI, out_error, true)) {
          return true;
        }
        surf = data->GetDataSurface();
      }
      if (!surf) {
        gfxCriticalError() << "TexUnpackSurface failed to get CanvasSurface";
        return false;
      }
    } else if (SDIsRGBBuffer(sd)) {
      const auto& sdb = sd.get_SurfaceDescriptorBuffer();
      const auto& rgb = sdb.desc().get_RGBDescriptor();
      const auto& data = sdb.data();
      MOZ_ASSERT(data.type() == layers::MemoryOrShmem::TShmem);
      const auto& shmem = data.get_Shmem();
      size_t shmemSize = shmem.Size<uint8_t>();
      auto stride = layers::ImageDataSerializer::GetRGBStride(rgb);
      if (stride.isNothing()) {
        gfxCriticalError() << "TexUnpackSurface failed to get rgb stride";
        return false;
      }
      Maybe<size_t> bufSize = layers::ImageDataSerializer::ComputeRGBBufferSize(
          rgb.size(), rgb.format());
      if (bufSize.isNothing() || bufSize.value() > shmemSize) {
        gfxCriticalError() << "TexUnpackSurface failed to get rgb buffer size";
        return false;
      }
      surf = gfx::Factory::CreateWrappingDataSourceSurface(
          shmem.get<uint8_t>(), stride.value(), rgb.size(), rgb.format());
    } else if (SDIsRemoteDecoderBuffer(sd)) {
      const auto& sdrd = sd.get_SurfaceDescriptorGPUVideo()
                             .get_SurfaceDescriptorRemoteDecoder();
      RefPtr<layers::VideoBridgeParent> parent =
          layers::VideoBridgeParent::GetSingleton(sdrd.source());
      if (!parent) {
        gfxCriticalNote << "TexUnpackSurface failed to get VideoBridgeParent";
        return false;
      }
      RefPtr<layers::TextureHost> texture =
          parent->LookupTexture(webgl->GetContentId(), sdrd.handle());
      if (!texture) {
        gfxCriticalNote << "TexUnpackSurface failed to get TextureHost";
        return false;
      }
      surf = texture->GetAsSurface();
    } else if (SDIsExternalImage(sd)) {
      const auto& sdei = sd.get_SurfaceDescriptorExternalImage();
      if (auto* sharedSurfacesHolder = webgl->GetSharedSurfacesHolder()) {
        surf = sharedSurfacesHolder->Get(sdei.id());
      }
      if (!surf) {
        // Most likely the content process crashed before it was able to finish
        // sharing the surface with the compositor process.
        gfxCriticalNote << "TexUnpackSurface failed to get ExternalImage";
        return false;
      }
    } else if (SDIsRemoteDecoderDirectUpload(sd)) {
      const auto& sdrd = sd.get_SurfaceDescriptorGPUVideo()
                             .get_SurfaceDescriptorRemoteDecoder();
      RefPtr<layers::VideoBridgeParent> parent =
          layers::VideoBridgeParent::GetSingleton(sdrd.source());
      if (!parent) {
        gfxCriticalNote << "TexUnpackSurface failed to get VideoBridgeParent";
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }
      RefPtr<layers::TextureHost> textureHost =
          parent->LookupTexture(webgl->GetContentId(), sdrd.handle());
      if (!textureHost) {
        gfxCriticalNote << "TexUnpackSurface failed to get TextureHost";
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }

      auto hostSd = textureHost->GetSurfaceDescriptor();
      if (hostSd.type() == layers::SurfaceDescriptor::T__None) {
        gfxCriticalNote
            << "TexUnpackSurface failed to get actual SurfaceDescriptor";
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }

      if (!webgl->IsUploadableSdType(hostSd)) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }

      if (!AllowBlitSd(webgl, mDesc.imageTarget, level,
                       {xOffset, yOffset, zOffset}, dui->internalFormat, dstPI,
                       false, true, true, true)) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }

      if (!BlitSd(hostSd, isSubImage, needsRespec, tex, level, dui, xOffset,
                  yOffset, zOffset, dstPI, out_error)) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return false;
      }

      return true;
    } else if (webgl->IsUploadableSdType(sd) &&
               AllowBlitSd(webgl, mDesc.imageTarget, level,
                           {xOffset, yOffset, zOffset}, dui->internalFormat,
                           dstPI, false, true, true, true) &&
               BlitSd(sd, isSubImage, needsRespec, tex, level, dui, xOffset,
                      yOffset, zOffset, dstPI, out_error)) {
      // The SD wasn't an RGBA shmem, but were able to blit the SD directly to
      // the texture.
      return true;
    } else if (mDesc.sourceSurf) {
      // In case neither a wrapper was created nor a blit succeeded, check for a
      // backup source surface.
      surf = mDesc.sourceSurf->GetDataSurface();
    }
    if (!surf) {
      gfxCriticalNote << "TexUnpackSurface failed to create wrapping "
                         "DataSourceSurface.";
      return false;
    }
  } else if (mDesc.sourceSurf) {
    surf = mDesc.sourceSurf->GetDataSurface();
    if (!surf) {
      gfxCriticalError() << "TexUnpackSurface failed to get data for "
                            "sourceSurf.";
      return false;
    }
  }

  ////

  const auto surfSize = surf->GetSize();

  WebGLTexelFormat srcFormat;
  uint8_t srcBPP;
  if (!GetFormatForSurf(surf, &srcFormat, &srcBPP)) {
    webgl->ErrorImplementationBug(
        "GetFormatForSurf failed for"
        " WebGLTexelFormat::%u.",
        uint32_t(surf->GetFormat()));
    return false;
  }

  gfx::DataSourceSurface::ScopedMap map(surf,
                                        gfx::DataSourceSurface::MapType::READ);
  if (!map.IsMapped()) {
    webgl->ErrorOutOfMemory("Failed to map source surface for upload.");
    return false;
  }

  const auto& srcBegin = map.GetData();
  const auto srcStride = static_cast<size_t>(map.GetStride());

  // -

  const auto dstFormat = FormatForPackingInfo(dstPI);
  const size_t dstBpp = BytesPerPixel(dstPI);
  const size_t dstUsedBytesPerRow = dstBpp * surfSize.width;
  size_t dstStride = dstFormat == srcFormat ? srcStride  // Try To match
                                            : dstUsedBytesPerRow;

  // -

  auto dstUnpackingRes = mDesc.ExplicitUnpacking(dstPI, Some(dstStride));
  if (dstUnpackingRes.isOk()) {
    const auto& dstUnpacking = dstUnpackingRes.inspect();
    if (!webgl->IsWebGL2() && dstUnpacking.state.rowLength != size.x) {
      dstUnpackingRes = Err("WebGL1 can't handle rowLength != size.x");
    }
  }
  if (!dstUnpackingRes.isOk()) {
    dstStride = dstUsedBytesPerRow;
    dstUnpackingRes = mDesc.ExplicitUnpacking(dstPI, Some(dstStride));
  }
  if (!dstUnpackingRes.isOk()) {
    gfxCriticalError() << dstUnpackingRes.inspectErr();
    webgl->ErrorImplementationBug("ExplicitUnpacking failed: %s",
                                  dstUnpackingRes.inspectErr().c_str());
    return false;
  }
  const auto& dstUnpacking = dstUnpackingRes.inspect();
  MOZ_ASSERT(dstUnpacking.metrics.bytesPerRowStride == dstStride);

  if (uint32_t(surfSize.width) < dstUnpacking.metrics.usedPixelsPerRow ||
      uint32_t(surfSize.height) < dstUnpacking.metrics.totalRows) {
    gfxCriticalError() << "Source surface size too small for upload.";
    return false;
  }

  // -

  const uint8_t* dstBegin = srcBegin;
  UniqueBuffer tempBuffer;
  // clang-format off
  if (!ConvertIfNeeded(webgl, surfSize.width, surfSize.height,
                       srcFormat, srcBegin, AutoAssertCast(srcStride),
                       dstFormat, AutoAssertCast(dstUnpacking.metrics.bytesPerRowStride),
                       /* applyUnpackColorSpace */ true, &dstBegin, &tempBuffer)) {
    return false;
  }
  // clang-format on

  ////

  const auto& gl = webgl->gl;
  if (!gl->MakeCurrent()) {
    *out_error = LOCAL_GL_CONTEXT_LOST;
    return true;
  }

  dstUnpacking.state.ApplyUnpack(*gl, webgl->IsWebGL2(), size);

  *out_error =
      DoTexOrSubImage(isSubImage, gl, mDesc.imageTarget, level, dui, xOffset,
                      yOffset, zOffset, size.x, size.y, size.z, dstBegin);

  // Caller will reset all our modified PixelStorei state.

  return true;
}

}  // namespace webgl
}  // namespace mozilla
