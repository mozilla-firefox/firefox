/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLTexture.h"

#include <algorithm>
#include <bit>

#include "GLContext.h"
#include "ScopedGLHelpers.h"
#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLSampler.h"
#include "WebGLTexelConversions.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/gfx/Logging.h"

namespace mozilla {
namespace webgl {

constinit /*static*/ const ImageInfo ImageInfo::kUndefined;

size_t ImageInfo::MemoryUsage() const {
  if (!IsDefined()) return 0;

  size_t samples = mSamples;
  if (!samples) {
    samples = 1;
  }

  const size_t bytesPerTexel = mFormat->format->estimatedBytesPerPixel;
  return size_t(mWidth) * size_t(mHeight) * size_t(mDepth) * samples *
         bytesPerTexel;
}

Maybe<ImageInfo> ImageInfo::NextMip(const GLenum target) const {
  MOZ_ASSERT(IsDefined());

  auto next = *this;

  if (target == LOCAL_GL_TEXTURE_3D) {
    if (mWidth <= 1 && mHeight <= 1 && mDepth <= 1) {
      return {};
    }

    next.mDepth = std::max(uint32_t(1), next.mDepth / 2);
  } else {
    // TEXTURE_2D_ARRAY may have depth != 1, but that's normal.
    if (mWidth <= 1 && mHeight <= 1) {
      return {};
    }
  }
  if (next.mUninitializedSlices) {
    next.mUninitializedSlices.emplace(next.mDepth, true);
  }

  next.mWidth = std::max(uint32_t(1), next.mWidth / 2);
  next.mHeight = std::max(uint32_t(1), next.mHeight / 2);
  return Some(next);
}

}  // namespace webgl

////////////////////////////////////////

WebGLTexture::WebGLTexture(WebGLContext* webgl, GLuint tex)
    : WebGLContextBoundObject(webgl),
      mGLName(tex),
      mTarget(LOCAL_GL_NONE),
      mFaceCount(0),
      mImmutable(false),
      mImmutableLevelCount(0),
      mBaseMipmapLevel(0),
      mMaxMipmapLevel(1000) {}

WebGLTexture::~WebGLTexture() {
  for (auto& cur : mImageInfoArr) {
    cur = webgl::ImageInfo();
  }
  InvalidateCaches();

  if (!mContext) return;
  mContext->gl->fDeleteTextures(1, &mGLName);
}

size_t WebGLTexture::MemoryUsage() const {
  size_t accum = 0;
  for (const auto& cur : mImageInfoArr) {
    accum += cur.MemoryUsage();
  }
  return accum;
}

// ---------------------------

void WebGLTexture::PopulateMipChain(const uint32_t maxLevel) {
  // Used by GenerateMipmap and TexStorage.
  // Populates based on mBaseMipmapLevel.

  auto ref = BaseImageInfo();
  MOZ_ASSERT(ref.mWidth && ref.mHeight && ref.mDepth);

  for (auto level = mBaseMipmapLevel; level <= maxLevel; ++level) {
    // GLES 3.0.4, p161
    // "A cube map texture is mipmap complete if each of the six texture images,
    //  considered individually, is mipmap complete."

    for (uint8_t face = 0; face < mFaceCount; face++) {
      auto& cur = ImageInfoAtFace(face, level);
      cur = ref;
    }

    const auto next = ref.NextMip(mTarget.get());
    if (!next) break;
    ref = next.ref();
  }
  InvalidateCaches();
}

static bool ZeroTextureData(const WebGLContext* webgl, GLuint tex,
                            TexImageTarget target, uint32_t level,
                            const webgl::ImageInfo& info);

bool WebGLTexture::IsMipAndCubeComplete(const uint32_t maxLevel,
                                        const bool ensureInit,
                                        bool* const out_initFailed) const {
  *out_initFailed = false;

  // Reference dimensions based on baseLevel.
  auto ref = BaseImageInfo();
  MOZ_ASSERT(ref.mWidth && ref.mHeight && ref.mDepth);

  for (auto level = mBaseMipmapLevel; level <= maxLevel; ++level) {
    // GLES 3.0.4, p161
    // "A cube map texture is mipmap complete if each of the six texture images,
    // considered individually, is mipmap complete."

    for (uint8_t face = 0; face < mFaceCount; face++) {
      auto& cur = ImageInfoAtFace(face, level);

      // "* The set of mipmap arrays `level_base` through `q` (where `q`
      //    is defined the "Mipmapping" discussion of section 3.8.10) were
      //    each specified with the same effective internal format."

      // "* The dimensions of the arrays follow the sequence described in
      //    the "Mipmapping" discussion of section 3.8.10."

      if (cur.mWidth != ref.mWidth || cur.mHeight != ref.mHeight ||
          cur.mDepth != ref.mDepth || cur.mFormat != ref.mFormat) {
        return false;
      }

      if (ensureInit && cur.mUninitializedSlices) [[unlikely]] {
        auto imageTarget = mTarget.get();
        if (imageTarget == LOCAL_GL_TEXTURE_CUBE_MAP) {
          imageTarget = LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X + face;
        }
        if (!ZeroTextureData(mContext, mGLName, imageTarget, level, cur)) {
          mContext->ErrorOutOfMemory("Failed to zero tex image data.");
          *out_initFailed = true;
          return false;
        }
        cur.mUninitializedSlices.reset();
      }
    }

    const auto next = ref.NextMip(mTarget.get());
    if (!next) break;
    ref = next.ref();
  }

  return true;
}

Maybe<const WebGLTexture::CompletenessInfo> WebGLTexture::CalcCompletenessInfo(
    const bool ensureInit, const bool skipMips) const {
  Maybe<CompletenessInfo> ret = Some(CompletenessInfo());

  // -

  const auto level_base = Es3_level_base();
  if (level_base > kMaxLevelCount - 1) {
    ret->incompleteReason = "`level_base` too high.";
    return ret;
  }

  // Texture completeness is established at GLES 3.0.4, p160-161.
  // "[A] texture is complete unless any of the following conditions hold true:"

  // "* Any dimension of the `level_base` array is not positive."
  const auto& baseImageInfo = ImageInfoAtFace(0, level_base);
  if (!baseImageInfo.IsDefined()) {
    // In case of undefined texture image, we don't print any message because
    // this is a very common and often legitimate case (asynchronous texture
    // loading).
    ret->incompleteReason = nullptr;
    return ret;
  }

  if (!baseImageInfo.mWidth || !baseImageInfo.mHeight ||
      !baseImageInfo.mDepth) {
    ret->incompleteReason =
        "The dimensions of `level_base` are not all positive.";
    return ret;
  }

  // "* The texture is a cube map texture, and is not cube complete."
  bool initFailed = false;
  if (!IsMipAndCubeComplete(level_base, ensureInit, &initFailed)) {
    if (initFailed) return {};

    // Can only fail if not cube-complete.
    ret->incompleteReason = "Cubemaps must be \"cube complete\".";
    return ret;
  }
  ret->levels = 1;
  ret->usage = baseImageInfo.mFormat;
  RefreshSwizzle();

  ret->powerOfTwo = std::has_single_bit(baseImageInfo.mWidth) &&
                    std::has_single_bit(baseImageInfo.mHeight);
  if (mTarget == LOCAL_GL_TEXTURE_3D) {
    ret->powerOfTwo &= std::has_single_bit(baseImageInfo.mDepth);
  }

  // -

  if (!mContext->IsWebGL2() && !ret->powerOfTwo) {
    // WebGL 1 mipmaps require POT.
    ret->incompleteReason = "Mipmapping requires power-of-two sizes.";
    return ret;
  }

  // "* `level_base <= level_max`"

  const auto level_max = Es3_level_max();
  const auto maxLevel_aka_q = Es3_q();
  if (level_base > level_max) {  // `level_max` not `q`!
    ret->incompleteReason = "`level_base > level_max`.";
    return ret;
  }

  if (skipMips) return ret;

  if (!IsMipAndCubeComplete(maxLevel_aka_q, ensureInit, &initFailed)) {
    if (initFailed) return {};

    ret->incompleteReason = "Bad mipmap dimension or format.";
    return ret;
  }
  ret->levels = AutoAssertCast(maxLevel_aka_q - level_base + 1);
  ret->mipmapComplete = true;

  // -

  return ret;
}

Maybe<const webgl::SampleableInfo> WebGLTexture::CalcSampleableInfo(
    const WebGLSampler* const sampler) const {
  Maybe<webgl::SampleableInfo> ret = Some(webgl::SampleableInfo());

  const bool ensureInit = true;
  const auto completeness = CalcCompletenessInfo(ensureInit);
  if (!completeness) return {};

  ret->incompleteReason = completeness->incompleteReason;

  if (!completeness->levels) return ret;

  const auto* sampling = &mSamplingState;
  if (sampler) {
    sampling = &sampler->State();
  }
  const auto isDepthTex = bool(completeness->usage->format->d);
  ret->isDepthTexCompare = isDepthTex & bool(sampling->compareMode.get());
  // Because if it's not a depth texture, we always ignore compareMode.

  const auto& minFilter = sampling->minFilter;
  const auto& magFilter = sampling->magFilter;

  // -

  const bool needsMips = (minFilter == LOCAL_GL_NEAREST_MIPMAP_NEAREST ||
                          minFilter == LOCAL_GL_NEAREST_MIPMAP_LINEAR ||
                          minFilter == LOCAL_GL_LINEAR_MIPMAP_NEAREST ||
                          minFilter == LOCAL_GL_LINEAR_MIPMAP_LINEAR);
  if (needsMips & !completeness->mipmapComplete) return ret;

  const bool isMinFilteringNearest =
      (minFilter == LOCAL_GL_NEAREST ||
       minFilter == LOCAL_GL_NEAREST_MIPMAP_NEAREST);
  const bool isMagFilteringNearest = (magFilter == LOCAL_GL_NEAREST);
  const bool isFilteringNearestOnly =
      (isMinFilteringNearest && isMagFilteringNearest);
  if (!isFilteringNearestOnly) {
    bool isFilterable = completeness->usage->isFilterable;

    // "* The effective internal format specified for the texture arrays is a
    //    sized internal depth or depth and stencil format, the value of
    //    TEXTURE_COMPARE_MODE is NONE[1], and either the magnification filter
    //    is not NEAREST, or the minification filter is neither NEAREST nor
    //    NEAREST_MIPMAP_NEAREST."
    // [1]: This sounds suspect, but is explicitly noted in the change log for
    //      GLES 3.0.1:
    //      "* Clarify that a texture is incomplete if it has a depth component,
    //         no shadow comparison, and linear filtering (also Bug 9481)."
    // In short, depth formats are not filterable, but shadow-samplers are.
    if (ret->isDepthTexCompare) {
      isFilterable = true;

      if (mContext->mWarnOnce_DepthTexCompareFilterable) {
        mContext->mWarnOnce_DepthTexCompareFilterable = false;
        mContext->GenerateWarning(
            "Depth texture comparison requests (e.g. `LINEAR`) Filtering, but"
            " behavior is implementation-defined, and so on some systems will"
            " sometimes behave as `NEAREST`. (warns once)");
      }
    }

    // "* The effective internal format specified for the texture arrays is a
    //    sized internal color format that is not texture-filterable, and either
    //    the magnification filter is not NEAREST or the minification filter is
    //    neither NEAREST nor NEAREST_MIPMAP_NEAREST."
    // Since all (GLES3) unsized color formats are filterable just like their
    // sized equivalents, we don't have to care whether its sized or not.
    if (!isFilterable) {
      ret->incompleteReason =
          "Minification or magnification filtering is not"
          " NEAREST or NEAREST_MIPMAP_NEAREST, and the"
          " texture's format is not \"texture-filterable\".";
      return ret;
    }
  }

  // Texture completeness is effectively (though not explicitly) amended for
  // GLES2 by the "Texture Access" section under $3.8 "Fragment Shaders". This
  // also applies to vertex shaders, as noted on GLES 2.0.25, p41.
  if (!mContext->IsWebGL2() && !completeness->powerOfTwo) {
    // GLES 2.0.25, p87-88:
    // "Calling a sampler from a fragment shader will return (R,G,B,A)=(0,0,0,1)
    // if
    //  any of the following conditions are true:"

    // "* A two-dimensional sampler is called, the minification filter is one
    //    that requires a mipmap[...], and the sampler's associated texture
    //    object is not complete[.]"
    // (already covered)

    // "* A two-dimensional sampler is called, the minification filter is
    //    not one that requires a mipmap (either NEAREST nor[sic] LINEAR), and
    //    either dimension of the level zero array of the associated texture
    //    object is not positive."
    // (already covered)

    // "* A two-dimensional sampler is called, the corresponding texture
    //    image is a non-power-of-two image[...], and either the texture wrap
    //    mode is not CLAMP_TO_EDGE, or the minification filter is neither
    //    NEAREST nor LINEAR."

    // "* A cube map sampler is called, any of the corresponding texture
    //    images are non-power-of-two images, and either the texture wrap mode
    //    is not CLAMP_TO_EDGE, or the minification filter is neither NEAREST
    //    nor LINEAR."
    // "either the texture wrap mode is not CLAMP_TO_EDGE"
    if (sampling->wrapS != LOCAL_GL_CLAMP_TO_EDGE ||
        sampling->wrapT != LOCAL_GL_CLAMP_TO_EDGE) {
      ret->incompleteReason =
          "Non-power-of-two textures must have a wrap mode of"
          " CLAMP_TO_EDGE.";
      return ret;
    }

    // "* A cube map sampler is called, and either the corresponding cube
    //    map texture image is not cube complete, or TEXTURE_MIN_FILTER is one
    //    that requires a mipmap and the texture is not mipmap cube complete."
    // (already covered)
  }

  // Mark complete.
  ret->incompleteReason =
      nullptr;  // NB: incompleteReason is also null for undefined
  ret->levels = completeness->levels;  //   textures.
  if (!needsMips && ret->levels) {
    ret->levels = 1;
  }
  ret->usage = completeness->usage;
  return ret;
}

const webgl::SampleableInfo* WebGLTexture::GetSampleableInfo(
    const WebGLSampler* const sampler) const {
  auto itr = mSamplingCache.Find(sampler);
  if (!itr) {
    const auto info = CalcSampleableInfo(sampler);
    if (!info) return nullptr;

    auto entry = mSamplingCache.MakeEntry(sampler, info.value());
    entry->AddInvalidator(*this);
    if (sampler) {
      entry->AddInvalidator(*sampler);
    }
    itr = mSamplingCache.Insert(std::move(entry));
  }
  return itr;
}

// ---------------------------

uint32_t WebGLTexture::Es3_q() const {
  const auto& imageInfo = BaseImageInfo();
  if (!imageInfo.IsDefined()) return mBaseMipmapLevel;

  uint32_t largestDim = std::max(imageInfo.mWidth, imageInfo.mHeight);
  if (mTarget == LOCAL_GL_TEXTURE_3D) {
    largestDim = std::max(largestDim, imageInfo.mDepth);
  }
  if (!largestDim) return mBaseMipmapLevel;

  // GLES 3.0.4, 3.8 - Mipmapping: `floor(log2(largest_of_dims)) + 1`
  const auto numLevels = FloorLog2Size(largestDim) + 1;

  const auto maxLevelBySize = mBaseMipmapLevel + numLevels - 1;
  return std::min<uint32_t>(maxLevelBySize, mMaxMipmapLevel);
}

// -

static void SetSwizzle(gl::GLContext* gl, TexTarget target,
                       const GLint* swizzle) {
  static const GLint kNoSwizzle[4] = {LOCAL_GL_RED, LOCAL_GL_GREEN,
                                      LOCAL_GL_BLUE, LOCAL_GL_ALPHA};
  if (!swizzle) {
    swizzle = kNoSwizzle;
  } else if (!gl->IsSupported(gl::GLFeature::texture_swizzle)) {
    MOZ_CRASH("GFX: Needs swizzle feature to swizzle!");
  }

  gl->fTexParameteri(target.get(), LOCAL_GL_TEXTURE_SWIZZLE_R, swizzle[0]);
  gl->fTexParameteri(target.get(), LOCAL_GL_TEXTURE_SWIZZLE_G, swizzle[1]);
  gl->fTexParameteri(target.get(), LOCAL_GL_TEXTURE_SWIZZLE_B, swizzle[2]);
  gl->fTexParameteri(target.get(), LOCAL_GL_TEXTURE_SWIZZLE_A, swizzle[3]);
}

void WebGLTexture::RefreshSwizzle() const {
  const auto& imageInfo = BaseImageInfo();
  const auto& swizzle = imageInfo.mFormat->textureSwizzleRGBA;

  if (swizzle != mCurSwizzle) {
    const gl::ScopedBindTexture scopeBindTexture(mContext->gl, mGLName,
                                                 mTarget.get());
    SetSwizzle(mContext->gl, mTarget, swizzle);
    mCurSwizzle = swizzle;
  }
}

bool WebGLTexture::EnsureImageDataInitialized(const TexImageTarget target,
                                              const uint32_t level) {
  auto& imageInfo = ImageInfoAt(target, level);
  if (!imageInfo.IsDefined()) return true;

  if (!imageInfo.mUninitializedSlices) return true;

  if (!ZeroTextureData(mContext, mGLName, target, level, imageInfo)) {
    return false;
  }
  imageInfo.mUninitializedSlices.reset();
  return true;
}

static bool ClearDepthTexture(const WebGLContext& webgl, const GLuint tex,
                              const TexImageTarget imageTarget,
                              const uint32_t level,
                              const webgl::ImageInfo& info) {
  const auto& gl = webgl.gl;
  const auto& usage = info.mFormat;
  const auto& format = usage->format;

  // Depth resources actually clear to 1.0f, not 0.0f!
  // They are also always renderable.
  MOZ_ASSERT(usage->IsRenderable());
  MOZ_ASSERT(info.mUninitializedSlices);

  GLenum attachPoint = LOCAL_GL_DEPTH_ATTACHMENT;
  GLbitfield clearBits = LOCAL_GL_DEPTH_BUFFER_BIT;

  if (format->s) {
    attachPoint = LOCAL_GL_DEPTH_STENCIL_ATTACHMENT;
    clearBits |= LOCAL_GL_STENCIL_BUFFER_BIT;
  }

  // -

  gl::ScopedFramebuffer scopedFB(gl);
  const gl::ScopedBindFramebuffer scopedBindFB(gl, scopedFB.FB());
  const webgl::ScopedPrepForResourceClear scopedPrep(webgl);

  const auto fnAttach = [&](const uint32_t z) {
    switch (imageTarget.get()) {
      case LOCAL_GL_TEXTURE_3D:
      case LOCAL_GL_TEXTURE_2D_ARRAY:
        gl->fFramebufferTextureLayer(LOCAL_GL_FRAMEBUFFER, attachPoint, tex,
                                     level, z);
        break;
      default:
        if (attachPoint == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
          gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                    LOCAL_GL_DEPTH_ATTACHMENT,
                                    imageTarget.get(), tex, level);
          gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                    LOCAL_GL_STENCIL_ATTACHMENT,
                                    imageTarget.get(), tex, level);
        } else {
          gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER, attachPoint,
                                    imageTarget.get(), tex, level);
        }
        break;
    }
  };

  for (const auto z : IntegerRange(info.mDepth)) {
    if ((*info.mUninitializedSlices)[z]) {
      fnAttach(z);
      gl->fClear(clearBits);
    }
  }
  const auto& status = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
  const bool isComplete = (status == LOCAL_GL_FRAMEBUFFER_COMPLETE);
  MOZ_ASSERT(isComplete);
  return isComplete;
}

static bool ZeroTextureData(const WebGLContext* webgl, GLuint tex,
                            TexImageTarget target, uint32_t level,
                            const webgl::ImageInfo& info) {
  // This has one usecase:
  // Lazy zeroing of uninitialized textures:
  //    a. Before draw.
  //    b. Before partial upload. (TexStorage + TexSubImage)

  // We have no sympathy for this case.

  // "Doctor, it hurts when I do this!" "Well don't do that!"
  MOZ_ASSERT(info.mUninitializedSlices);

  const auto targetStr = EnumString(target.get());
  webgl->GenerateWarning(
      "Tex image %s level %u is incurring lazy initialization.",
      targetStr.c_str(), level);

  gl::GLContext* gl = webgl->GL();
  const auto& width = info.mWidth;
  const auto& height = info.mHeight;
  const auto& depth = info.mDepth;
  const auto& usage = info.mFormat;

  GLenum scopeBindTarget;
  switch (target.get()) {
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      scopeBindTarget = LOCAL_GL_TEXTURE_CUBE_MAP;
      break;
    default:
      scopeBindTarget = target.get();
      break;
  }
  const gl::ScopedBindTexture scopeBindTexture(gl, tex, scopeBindTarget);
  const auto& compression = usage->format->compression;
  if (compression) {
    auto sizedFormat = usage->format->sizedFormat;
    MOZ_RELEASE_ASSERT(sizedFormat, "GFX: texture sized format not set");

    const auto fnSizeInBlocks = [](CheckedUint32 pixels,
                                   uint8_t pixelsPerBlock) {
      return RoundUpToMultipleOf(pixels, pixelsPerBlock) / pixelsPerBlock;
    };

    const auto widthBlocks = fnSizeInBlocks(width, compression->blockWidth);
    const auto heightBlocks = fnSizeInBlocks(height, compression->blockHeight);

    CheckedUint32 checkedByteCount = compression->bytesPerBlock;
    checkedByteCount *= widthBlocks;
    checkedByteCount *= heightBlocks;

    if (!checkedByteCount.isValid()) return false;

    const size_t sliceByteCount = checkedByteCount.value();

    const auto zeros = UniqueBuffer::Take(calloc(1u, sliceByteCount));
    if (!zeros) return false;

    // Don't bother with striding it well.
    // TODO: We shouldn't need to do this for CompressedTexSubImage.
    webgl::PixelPackingState{}.AssertCurrentUnpack(*gl, webgl->IsWebGL2());
    gl->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 1);
    const auto revert = MakeScopeExit(
        [&]() { gl->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 4); });

    GLenum error = 0;
    for (const auto z : IntegerRange(depth)) {
      if ((*info.mUninitializedSlices)[z]) {
        error = DoCompressedTexSubImage(gl, target.get(), level, 0, 0, z, width,
                                        height, 1, sizedFormat, sliceByteCount,
                                        zeros.get());
        if (error) break;
      }
    }
    return !error;
  }

  const auto driverUnpackInfo = usage->idealUnpack;
  MOZ_RELEASE_ASSERT(driverUnpackInfo, "GFX: ideal unpack info not set.");

  if (usage->format->d) {
    // ANGLE_depth_texture does not allow uploads, so we have to clear.
    // (Restriction because of D3D9)
    // Also, depth resources are cleared to 1.0f and are always renderable, so
    // just use FB clears.
    return ClearDepthTexture(*webgl, tex, target, level, info);
  }

  const webgl::PackingInfo packing = driverUnpackInfo->ToPacking();

  const auto bytesPerPixel = webgl::BytesPerPixel(packing);

  CheckedUint32 checkedByteCount = bytesPerPixel;
  checkedByteCount *= width;
  checkedByteCount *= height;

  if (!checkedByteCount.isValid()) return false;

  const size_t sliceByteCount = checkedByteCount.value();

  const auto zeros = UniqueBuffer::Take(calloc(1u, sliceByteCount));
  if (!zeros) return false;

  // Don't bother with striding it well.
  webgl::PixelPackingState{}.AssertCurrentUnpack(*gl, webgl->IsWebGL2());
  gl->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 1);
  const auto revert =
      MakeScopeExit([&]() { gl->fPixelStorei(LOCAL_GL_UNPACK_ALIGNMENT, 4); });

  GLenum error = 0;
  for (const auto z : IntegerRange(depth)) {
    if ((*info.mUninitializedSlices)[z]) {
      error = DoTexSubImage(gl, target, level, 0, 0, z, width, height, 1,
                            packing, zeros.get());
      if (error) break;
    }
  }
  return !error;
}

void WebGLTexture::ClampLevelBaseAndMax() {
  if (!mImmutable) return;

  // GLES 3.0.4, p158:
  // "For immutable-format textures, `level_base` is clamped to the range
  //  `[0, levels-1]`, `level_max` is then clamped to the range `
  //  `[level_base, levels-1]`, where `levels` is the parameter passed to
  //   TexStorage* for the texture object."
  MOZ_ASSERT(mImmutableLevelCount > 0);
  const auto oldBase = mBaseMipmapLevel;
  const auto oldMax = mMaxMipmapLevel;
  mBaseMipmapLevel =
      std::clamp(mBaseMipmapLevel, 0u, mImmutableLevelCount - 1u);
  mMaxMipmapLevel =
      std::clamp(mMaxMipmapLevel, mBaseMipmapLevel, mImmutableLevelCount - 1u);
  if (oldBase != mBaseMipmapLevel &&
      mBaseMipmapLevelState != MIPMAP_LEVEL_DEFAULT) {
    mBaseMipmapLevelState = MIPMAP_LEVEL_DIRTY;
  }
  if (oldMax != mMaxMipmapLevel &&
      mMaxMipmapLevelState != MIPMAP_LEVEL_DEFAULT) {
    mMaxMipmapLevelState = MIPMAP_LEVEL_DIRTY;
  }

  // Note: This means that immutable textures are *always* texture-complete!
}

//////////////////////////////////////////////////////////////////////////////////////////
// GL calls

bool WebGLTexture::BindTexture(TexTarget texTarget) {
  const bool isFirstBinding = !mTarget;
  if (!isFirstBinding && mTarget != texTarget) {
    mContext->ErrorInvalidOperation(
        "bindTexture: This texture has already been bound"
        " to a different target.");
    return false;
  }

  mTarget = texTarget;

  mContext->gl->fBindTexture(mTarget.get(), mGLName);

  if (isFirstBinding) {
    mFaceCount = IsCubeMap() ? 6 : 1;

    gl::GLContext* gl = mContext->gl;

    // Thanks to the WebKit people for finding this out: GL_TEXTURE_WRAP_R
    // is not present in GLES 2, but is present in GL and it seems as if for
    // cube maps we need to set it to GL_CLAMP_TO_EDGE to get the expected
    // GLES behavior.
    // If we are WebGL 2 though, we'll want to leave it as REPEAT.
    const bool hasWrapR = gl->IsSupported(gl::GLFeature::texture_3D);
    if (IsCubeMap() && hasWrapR && !mContext->IsWebGL2()) {
      gl->fTexParameteri(texTarget.get(), LOCAL_GL_TEXTURE_WRAP_R,
                         LOCAL_GL_CLAMP_TO_EDGE);
    }
  }

  return true;
}

static constexpr GLint ClampMipmapLevelForDriver(uint32_t level) {
  return static_cast<GLint>(
      std::clamp(level, 0u, (uint32_t)WebGLTexture::kMaxLevelCount));
}

void WebGLTexture::GenerateMipmap() {
  // GLES 3.0.4 p160:
  // "Mipmap generation replaces texel array levels level base + 1 through q
  //  with arrrays derived from the level base array, regardless of their
  //  previous contents. All other mipmap arrays, including the level base
  //  array, are left unchanged by this computation."
  // But only check and init the base level.
  const bool ensureInit = true;
  const bool skipMips = true;
  const auto completeness = CalcCompletenessInfo(ensureInit, skipMips);
  if (!completeness || !completeness->levels) {
    mContext->ErrorInvalidOperation(
        "The texture's base level must be complete.");
    return;
  }
  const auto& usage = completeness->usage;
  const auto& format = usage->format;
  if (!mContext->IsWebGL2()) {
    if (!completeness->powerOfTwo) {
      mContext->ErrorInvalidOperation(
          "The base level of the texture does not"
          " have power-of-two dimensions.");
      return;
    }
    if (format->isSRGB) {
      mContext->ErrorInvalidOperation(
          "EXT_sRGB forbids GenerateMipmap with"
          " sRGB.");
      return;
    }
  }

  if (format->compression) {
    mContext->ErrorInvalidOperation(
        "Texture data at base level is compressed.");
    return;
  }

  if (format->d) {
    mContext->ErrorInvalidOperation("Depth textures are not supported.");
    return;
  }

  // OpenGL ES 3.0.4 p160:
  // If the level base array was not specified with an unsized internal format
  // from table 3.3 or a sized internal format that is both color-renderable and
  // texture-filterable according to table 3.13, an INVALID_OPERATION error
  // is generated.
  bool canGenerateMipmap = (usage->IsRenderable() && usage->isFilterable);
  switch (usage->format->effectiveFormat) {
    case webgl::EffectiveFormat::Luminance8:
    case webgl::EffectiveFormat::Alpha8:
    case webgl::EffectiveFormat::Luminance8Alpha8:
      // Non-color-renderable formats from Table 3.3.
      canGenerateMipmap = true;
      break;
    default:
      break;
  }

  if (!canGenerateMipmap) {
    mContext->ErrorInvalidOperation(
        "Texture at base level is not unsized"
        " internal format or is not"
        " color-renderable or texture-filterable.");
    return;
  }

  if (usage->IsRenderable() && !usage->IsExplicitlyRenderable()) {
    mContext->WarnIfImplicit(usage->GetExtensionID());
  }

  // Done with validation. Do the operation.

  gl::GLContext* gl = mContext->gl;

  if (gl->WorkAroundDriverBugs()) {
    // If we first set GL_TEXTURE_BASE_LEVEL to a number such as 20, then set
    // MGL_TEXTURE_MAX_LEVEL to a smaller number like 8, our copy of the
    // base level will be lowered, but we havn't yet updated the driver, we
    // should do so now, before calling glGenerateMipmap().
    if (mBaseMipmapLevelState == MIPMAP_LEVEL_DIRTY) {
      gl->fTexParameteri(mTarget.get(), LOCAL_GL_TEXTURE_BASE_LEVEL,
                         ClampMipmapLevelForDriver(mBaseMipmapLevel));
      mBaseMipmapLevelState = MIPMAP_LEVEL_CLEAN;
    }
    if (mMaxMipmapLevelState == MIPMAP_LEVEL_DIRTY) {
      gl->fTexParameteri(mTarget.get(), LOCAL_GL_TEXTURE_MAX_LEVEL,
                         ClampMipmapLevelForDriver(mMaxMipmapLevel));
      mMaxMipmapLevelState = MIPMAP_LEVEL_CLEAN;
    }

    // bug 696495 - to work around failures in the texture-mips.html test on
    // various drivers, we set the minification filter before calling
    // glGenerateMipmap. This should not carry a significant performance
    // overhead so we do it unconditionally.
    //
    // note that the choice of GL_NEAREST_MIPMAP_NEAREST really matters. See
    // Chromium bug 101105.
    gl->fTexParameteri(mTarget.get(), LOCAL_GL_TEXTURE_MIN_FILTER,
                       LOCAL_GL_NEAREST_MIPMAP_NEAREST);
    gl->fGenerateMipmap(mTarget.get());
    gl->fTexParameteri(mTarget.get(), LOCAL_GL_TEXTURE_MIN_FILTER,
                       mSamplingState.minFilter.get());
  } else {
    gl->fGenerateMipmap(mTarget.get());
  }

  // Record the results.

  const auto maxLevel = Es3_q();
  PopulateMipChain(maxLevel);
}

Maybe<double> WebGLTexture::GetTexParameter(GLenum pname) const {
  GLint i = 0;
  GLfloat f = 0.0f;

  switch (pname) {
    case LOCAL_GL_TEXTURE_BASE_LEVEL:
      return Some(mBaseMipmapLevel);

    case LOCAL_GL_TEXTURE_MAX_LEVEL:
      return Some(mMaxMipmapLevel);

    case LOCAL_GL_TEXTURE_IMMUTABLE_FORMAT:
      return Some(mImmutable);

    case LOCAL_GL_TEXTURE_IMMUTABLE_LEVELS:
      return Some(uint32_t(mImmutableLevelCount));

    case LOCAL_GL_TEXTURE_MIN_FILTER:
    case LOCAL_GL_TEXTURE_MAG_FILTER:
    case LOCAL_GL_TEXTURE_WRAP_S:
    case LOCAL_GL_TEXTURE_WRAP_T:
    case LOCAL_GL_TEXTURE_WRAP_R:
    case LOCAL_GL_TEXTURE_COMPARE_MODE:
    case LOCAL_GL_TEXTURE_COMPARE_FUNC: {
      MOZ_ASSERT(mTarget);
      const gl::ScopedBindTexture autoTex(mContext->gl, mGLName, mTarget.get());
      mContext->gl->fGetTexParameteriv(mTarget.get(), pname, &i);
      return Some(i);
    }

    case LOCAL_GL_TEXTURE_MAX_ANISOTROPY_EXT:
    case LOCAL_GL_TEXTURE_MAX_LOD:
    case LOCAL_GL_TEXTURE_MIN_LOD: {
      MOZ_ASSERT(mTarget);
      const gl::ScopedBindTexture autoTex(mContext->gl, mGLName, mTarget.get());
      mContext->gl->fGetTexParameterfv(mTarget.get(), pname, &f);
      return Some(f);
    }

    default:
      MOZ_CRASH("GFX: Unhandled pname.");
  }
}

// Here we have to support all pnames with both int and float params.
// See this discussion:
//   https://www.khronos.org/webgl/public-mailing-list/archives/1008/msg00014.html
void WebGLTexture::TexParameter(TexTarget texTarget, GLenum pname,
                                const FloatOrInt& param) {
  bool isPNameValid = false;
  switch (pname) {
    // GLES 2.0.25 p76:
    case LOCAL_GL_TEXTURE_WRAP_S:
    case LOCAL_GL_TEXTURE_WRAP_T:
    case LOCAL_GL_TEXTURE_MIN_FILTER:
    case LOCAL_GL_TEXTURE_MAG_FILTER:
      isPNameValid = true;
      break;

    // GLES 3.0.4 p149-150:
    case LOCAL_GL_TEXTURE_BASE_LEVEL:
    case LOCAL_GL_TEXTURE_COMPARE_MODE:
    case LOCAL_GL_TEXTURE_COMPARE_FUNC:
    case LOCAL_GL_TEXTURE_MAX_LEVEL:
    case LOCAL_GL_TEXTURE_MAX_LOD:
    case LOCAL_GL_TEXTURE_MIN_LOD:
    case LOCAL_GL_TEXTURE_WRAP_R:
      if (mContext->IsWebGL2()) isPNameValid = true;
      break;

    case LOCAL_GL_TEXTURE_MAX_ANISOTROPY_EXT:
      if (mContext->IsExtensionEnabled(
              WebGLExtensionID::EXT_texture_filter_anisotropic))
        isPNameValid = true;
      break;
  }

  if (!isPNameValid) {
    mContext->ErrorInvalidEnumInfo("texParameter: pname", pname);
    return;
  }

  ////////////////
  // Validate params and invalidate if needed.

  bool paramBadEnum = false;
  bool paramBadValue = false;

  switch (pname) {
    case LOCAL_GL_TEXTURE_BASE_LEVEL:
    case LOCAL_GL_TEXTURE_MAX_LEVEL:
      paramBadValue = (param.i < 0);
      break;

    case LOCAL_GL_TEXTURE_COMPARE_MODE:
      paramBadValue = (param.i != LOCAL_GL_NONE &&
                       param.i != LOCAL_GL_COMPARE_REF_TO_TEXTURE);
      break;

    case LOCAL_GL_TEXTURE_COMPARE_FUNC:
      switch (param.i) {
        case LOCAL_GL_LEQUAL:
        case LOCAL_GL_GEQUAL:
        case LOCAL_GL_LESS:
        case LOCAL_GL_GREATER:
        case LOCAL_GL_EQUAL:
        case LOCAL_GL_NOTEQUAL:
        case LOCAL_GL_ALWAYS:
        case LOCAL_GL_NEVER:
          break;

        default:
          paramBadValue = true;
          break;
      }
      break;

    case LOCAL_GL_TEXTURE_MIN_FILTER:
      switch (param.i) {
        case LOCAL_GL_NEAREST:
        case LOCAL_GL_LINEAR:
        case LOCAL_GL_NEAREST_MIPMAP_NEAREST:
        case LOCAL_GL_LINEAR_MIPMAP_NEAREST:
        case LOCAL_GL_NEAREST_MIPMAP_LINEAR:
        case LOCAL_GL_LINEAR_MIPMAP_LINEAR:
          break;

        default:
          paramBadEnum = true;
          break;
      }
      break;

    case LOCAL_GL_TEXTURE_MAG_FILTER:
      switch (param.i) {
        case LOCAL_GL_NEAREST:
        case LOCAL_GL_LINEAR:
          break;

        default:
          paramBadEnum = true;
          break;
      }
      break;

    case LOCAL_GL_TEXTURE_WRAP_S:
    case LOCAL_GL_TEXTURE_WRAP_T:
    case LOCAL_GL_TEXTURE_WRAP_R:
      switch (param.i) {
        case LOCAL_GL_CLAMP_TO_EDGE:
        case LOCAL_GL_MIRRORED_REPEAT:
        case LOCAL_GL_REPEAT:
          break;

        default:
          paramBadEnum = true;
          break;
      }
      break;

    case LOCAL_GL_TEXTURE_MAX_ANISOTROPY_EXT:
      if (param.f < 1.0f) paramBadValue = true;

      break;
  }

  if (paramBadEnum) {
    if (!param.isFloat) {
      mContext->ErrorInvalidEnum(
          "pname 0x%04x: Invalid param"
          " 0x%04x.",
          pname, param.i);
    } else {
      mContext->ErrorInvalidEnum("pname 0x%04x: Invalid param %g.", pname,
                                 param.f);
    }
    return;
  }

  if (paramBadValue) {
    if (!param.isFloat) {
      mContext->ErrorInvalidValue(
          "pname 0x%04x: Invalid param %i"
          " (0x%x).",
          pname, param.i, param.i);
    } else {
      mContext->ErrorInvalidValue("pname 0x%04x: Invalid param %g.", pname,
                                  param.f);
    }
    return;
  }

  ////////////////
  // Store any needed values

  FloatOrInt clamped = param;
  bool invalidate = true;
  switch (pname) {
    case LOCAL_GL_TEXTURE_BASE_LEVEL: {
      mBaseMipmapLevel = clamped.i;
      mBaseMipmapLevelState = MIPMAP_LEVEL_CLEAN;
      ClampLevelBaseAndMax();
      clamped = FloatOrInt(ClampMipmapLevelForDriver(mBaseMipmapLevel));
      break;
    }

    case LOCAL_GL_TEXTURE_MAX_LEVEL: {
      mMaxMipmapLevel = clamped.i;
      mMaxMipmapLevelState = MIPMAP_LEVEL_CLEAN;
      ClampLevelBaseAndMax();
      clamped = FloatOrInt(ClampMipmapLevelForDriver(mMaxMipmapLevel));
      break;
    }

    case LOCAL_GL_TEXTURE_MIN_FILTER:
      mSamplingState.minFilter = clamped.i;
      break;

    case LOCAL_GL_TEXTURE_MAG_FILTER:
      mSamplingState.magFilter = clamped.i;
      break;

    case LOCAL_GL_TEXTURE_WRAP_S:
      mSamplingState.wrapS = clamped.i;
      break;

    case LOCAL_GL_TEXTURE_WRAP_T:
      mSamplingState.wrapT = clamped.i;
      break;

    case LOCAL_GL_TEXTURE_COMPARE_MODE:
      mSamplingState.compareMode = clamped.i;
      break;

    default:
      invalidate = false;  // Texture completeness will not change.
      break;
  }

  if (invalidate) {
    InvalidateCaches();
  }

  ////////////////

  if (!clamped.isFloat) {
    mContext->gl->fTexParameteri(texTarget.get(), pname, clamped.i);
  } else {
    mContext->gl->fTexParameterf(texTarget.get(), pname, clamped.f);
  }
}

void WebGLTexture::Truncate() {
  for (auto& cur : mImageInfoArr) {
    cur = {};
  }
  InvalidateCaches();
}

}  // namespace mozilla
