/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "GLContext.h"
#include "MozFramebuffer.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "nsPrintfCString.h"

namespace mozilla {

// For a Tegra workaround.
static const int MAX_DRAW_CALLS_SINCE_FLUSH = 100;

////////////////////////////////////////

class ScopedResolveTexturesForDraw {
  struct TexRebindRequest {
    uint32_t texUnit;
    WebGLTexture* tex;
  };

  WebGLContext* const mWebGL;
  std::vector<TexRebindRequest> mRebindRequests;

 public:
  ScopedResolveTexturesForDraw(WebGLContext* webgl, bool* const out_error);
  ~ScopedResolveTexturesForDraw();
};

static bool ValidateNoSamplingFeedback(const WebGLTexture& tex,
                                       const uint32_t sampledLevels,
                                       const WebGLFramebuffer* const fb,
                                       const uint32_t texUnit) {
  if (!fb) return true;

  const auto& texAttachments = fb->GetCompletenessInfo()->texAttachments;
  for (const auto& attach : texAttachments) {
    if (attach->Texture() != &tex) continue;

    const auto& srcBase = tex.Es3_level_base();
    const auto srcLast = srcBase + sampledLevels - 1;
    const auto& dstLevel = attach->MipLevel();
    if (srcBase <= dstLevel && dstLevel <= srcLast) [[unlikely]] {
      const auto& webgl = tex.mContext;
      const auto& texTargetStr = EnumString(tex.Target().get());
      const auto& attachStr = EnumString(attach->mAttachmentPoint);
      webgl->ErrorInvalidOperation(
          "Texture level %u would be read by %s unit %u,"
          " but written by framebuffer attachment %s,"
          " which would be illegal feedback.",
          dstLevel, texTargetStr.c_str(), texUnit, attachStr.c_str());
      return false;
    }
  }
  return true;
}

ScopedResolveTexturesForDraw::ScopedResolveTexturesForDraw(
    WebGLContext* webgl, bool* const out_error)
    : mWebGL(webgl) {
  const auto& fb = mWebGL->mBoundDrawFramebuffer;

  struct SamplerByTexUnit {
    uint8_t texUnit;
    const webgl::SamplerUniformInfo* sampler;
  };
  Vector<SamplerByTexUnit, 8> samplerByTexUnit;

  MOZ_ASSERT(mWebGL->mActiveProgramLinkInfo);
  const auto& samplerUniforms = mWebGL->mActiveProgramLinkInfo->samplerUniforms;
  for (const auto& pUniform : samplerUniforms) {
    const auto& uniform = *pUniform;
    const auto& texList = uniform.texListForType;

    const auto& uniformBaseType = uniform.texBaseType;
    for (const auto& texUnit : uniform.texUnits) {
      MOZ_ASSERT(texUnit < texList.Length());

      {
        decltype(SamplerByTexUnit::sampler) prevSamplerForTexUnit = nullptr;
        for (const auto& cur : samplerByTexUnit) {
          if (cur.texUnit == texUnit) {
            prevSamplerForTexUnit = cur.sampler;
          }
        }
        if (!prevSamplerForTexUnit) {
          prevSamplerForTexUnit = &uniform;
          MOZ_RELEASE_ASSERT(samplerByTexUnit.append(
              SamplerByTexUnit{texUnit, prevSamplerForTexUnit}));
        }

        if (&uniform.texListForType != &prevSamplerForTexUnit->texListForType)
            [[unlikely]] {
          // Pointing to different tex lists means different types!
          const auto linkInfo = mWebGL->mActiveProgramLinkInfo;
          const auto LocInfoBySampler = [&](const webgl::SamplerUniformInfo* p)
              -> const webgl::LocationInfo* {
            for (const auto& pair : linkInfo->locationMap) {
              const auto& locInfo = pair.second;
              if (locInfo.samplerInfo == p) {
                return &locInfo;
              }
            }
            MOZ_CRASH("Can't find sampler location.");
          };
          const auto& cur = *LocInfoBySampler(&uniform);
          const auto& prev = *LocInfoBySampler(prevSamplerForTexUnit);
          mWebGL->ErrorInvalidOperation(
              "Tex unit %u referenced by samplers of different types:"
              " %s (via %s) and %s (via %s).",
              texUnit, EnumString(cur.info.info.elemType).c_str(),
              cur.PrettyName().c_str(),
              EnumString(prev.info.info.elemType).c_str(),
              prev.PrettyName().c_str());
          *out_error = true;
          return;
        }
      }

      const auto& tex = texList[texUnit];
      if (!tex) continue;

      const auto& sampler = mWebGL->mBoundSamplers[texUnit];
      const auto& samplingInfo = tex->GetSampleableInfo(sampler.get());
      if (!samplingInfo) [[unlikely]] {  // There was an error.
        *out_error = true;
        return;
      }
      if (!samplingInfo->IsComplete()) [[unlikely]] {
        if (samplingInfo->incompleteReason) {
          const auto& targetName = GetEnumName(tex->Target().get());
          mWebGL->GenerateWarning("%s at unit %u is incomplete: %s", targetName,
                                  texUnit, samplingInfo->incompleteReason);
        }
        mRebindRequests.push_back({texUnit, tex});
        continue;
      }

      // We have more validation to do if we're otherwise complete:
      const auto& texBaseType = samplingInfo->usage->format->baseType;
      if (texBaseType != uniformBaseType) [[unlikely]] {
        const auto& targetName = GetEnumName(tex->Target().get());
        const auto& srcType = ToString(texBaseType);
        const auto& dstType = ToString(uniformBaseType);
        mWebGL->ErrorInvalidOperation(
            "%s at unit %u is of type %s, but"
            " the shader samples as %s.",
            targetName, texUnit, srcType, dstType);
        *out_error = true;
        return;
      }

      if (uniform.isShadowSampler != samplingInfo->isDepthTexCompare)
          [[unlikely]] {
        const auto& targetName = GetEnumName(tex->Target().get());
        mWebGL->ErrorInvalidOperation(
            "%s at unit %u is%s a depth texture"
            " with TEXTURE_COMPARE_MODE, but"
            " the shader sampler is%s a shadow"
            " sampler.",
            targetName, texUnit, samplingInfo->isDepthTexCompare ? "" : " not",
            uniform.isShadowSampler ? "" : " not");
        *out_error = true;
        return;
      }

      if (!ValidateNoSamplingFeedback(*tex, samplingInfo->levels, fb.get(),
                                      texUnit)) [[unlikely]] {
        *out_error = true;
        return;
      }
    }
  }

  const auto& gl = mWebGL->gl;
  for (const auto& itr : mRebindRequests) {
    gl->fActiveTexture(LOCAL_GL_TEXTURE0 + itr.texUnit);
    GLuint incompleteTex = 0;  // Tex 0 is always incomplete.
    const auto& overrideTex = webgl->mIncompleteTexOverride;
    if (overrideTex) {
      // In all but the simplest cases, this will be incomplete anyway, since
      // e.g. int-samplers need int-textures. This is useful for e.g.
      // dom-to-texture failures, though.
      incompleteTex = overrideTex->name;
    }
    gl->fBindTexture(itr.tex->Target().get(), incompleteTex);
  }
}

ScopedResolveTexturesForDraw::~ScopedResolveTexturesForDraw() {
  if (mRebindRequests.empty()) return;

  gl::GLContext* gl = mWebGL->gl;

  for (const auto& itr : mRebindRequests) {
    gl->fActiveTexture(LOCAL_GL_TEXTURE0 + itr.texUnit);
    gl->fBindTexture(itr.tex->Target().get(), itr.tex->mGLName);
  }

  gl->fActiveTexture(LOCAL_GL_TEXTURE0 + mWebGL->mActiveTexture);
}

////////////////////////////////////////

bool WebGLContext::ValidateStencilParamsForDrawCall() const {
  const auto stencilBits = [&]() -> uint8_t {
    if (!mStencilTestEnabled) return 0;

    if (!mBoundDrawFramebuffer) return mOptions.stencil ? 8 : 0;

    if (mBoundDrawFramebuffer->StencilAttachment().HasAttachment()) return 8;

    if (mBoundDrawFramebuffer->DepthStencilAttachment().HasAttachment())
      return 8;

    return 0;
  }();
  const uint32_t stencilMax = (1 << stencilBits) - 1;

  const auto fnMask = [&](const uint32_t x) { return x & stencilMax; };
  const auto fnClamp = [&](const int32_t x) {
    return std::clamp(x, 0, (int32_t)stencilMax);
  };

  bool ok = true;
  ok &= (fnMask(mStencilWriteMaskFront) == fnMask(mStencilWriteMaskBack));
  ok &= (fnMask(mStencilValueMaskFront) == fnMask(mStencilValueMaskBack));
  ok &= (fnClamp(mStencilRefFront) == fnClamp(mStencilRefBack));

  if (!ok) {
    ErrorInvalidOperation(
        "Stencil front/back state must effectively match."
        " (before front/back comparison, WRITEMASK and VALUE_MASK"
        " are masked with (2^s)-1, and REF is clamped to"
        " [0, (2^s)-1], where `s` is the number of enabled stencil"
        " bits in the draw framebuffer)");
  }
  return ok;
}

// -

void WebGLContext::GenErrorIllegalUse(const GLenum useTarget,
                                      const uint32_t useId,
                                      const GLenum boundTarget,
                                      const uint32_t boundId) const {
  const auto fnName = [&](const GLenum target, const uint32_t id) {
    auto name = nsCString(EnumString(target).c_str());
    if (id != static_cast<uint32_t>(-1)) {
      name += nsPrintfCString("[%u]", id);
    }
    return name;
  };
  const auto& useName = fnName(useTarget, useId);
  const auto& boundName = fnName(boundTarget, boundId);
  GenerateError(LOCAL_GL_INVALID_OPERATION,
                "Illegal use of buffer at %s"
                " while also bound to %s.",
                useName.get(), boundName.get());
}

bool WebGLContext::ValidateBufferForNonTf(const WebGLBuffer& nonTfBuffer,
                                          const GLenum nonTfTarget,
                                          const uint32_t nonTfId) const {
  bool dupe = false;
  const auto& tfAttribs = mBoundTransformFeedback->mIndexedBindings;
  for (const auto& cur : tfAttribs) {
    dupe |= (&nonTfBuffer == cur.mBufferBinding.get());
  }
  if (!dupe) [[likely]] {
    return true;
  }

  dupe = false;
  for (const auto tfId : IntegerRange(tfAttribs.size())) {
    const auto& tfBuffer = tfAttribs[tfId].mBufferBinding;
    if (&nonTfBuffer == tfBuffer) {
      dupe = true;
      GenErrorIllegalUse(nonTfTarget, nonTfId,
                         LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER, tfId);
    }
  }
  MOZ_ASSERT(dupe);
  return false;
}

bool WebGLContext::ValidateBuffersForTf(
    const WebGLTransformFeedback& tfo,
    const webgl::LinkedProgramInfo& linkInfo) const {
  size_t numUsed;
  switch (linkInfo.transformFeedbackBufferMode) {
    case LOCAL_GL_INTERLEAVED_ATTRIBS:
      numUsed = 1;
      break;

    case LOCAL_GL_SEPARATE_ATTRIBS:
      numUsed = linkInfo.active.activeTfVaryings.size();
      break;

    default:
      MOZ_CRASH();
  }

  std::vector<webgl::BufferAndIndex> tfBuffers;
  tfBuffers.reserve(numUsed);
  for (const auto i : IntegerRange(numUsed)) {
    tfBuffers.push_back({tfo.mIndexedBindings[i].mBufferBinding.get(),
                         static_cast<uint32_t>(i)});
  }

  return ValidateBuffersForTf(tfBuffers);
}

bool WebGLContext::ValidateBuffersForTf(
    const std::vector<webgl::BufferAndIndex>& tfBuffers) const {
  bool dupe = false;
  const auto fnCheck = [&](const WebGLBuffer* const nonTf,
                           const GLenum nonTfTarget, const uint32_t nonTfId) {
    for (const auto& tf : tfBuffers) {
      dupe |= (nonTf && tf.buffer == nonTf);
    }

    if (!dupe) [[likely]] {
      return false;
    }

    for (const auto& tf : tfBuffers) {
      if (nonTf && tf.buffer == nonTf) {
        dupe = true;
        GenErrorIllegalUse(LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER, tf.id,
                           nonTfTarget, nonTfId);
      }
    }
    return true;
  };

  fnCheck(mBoundArrayBuffer.get(), LOCAL_GL_ARRAY_BUFFER, -1);
  fnCheck(mBoundCopyReadBuffer.get(), LOCAL_GL_COPY_READ_BUFFER, -1);
  fnCheck(mBoundCopyWriteBuffer.get(), LOCAL_GL_COPY_WRITE_BUFFER, -1);
  fnCheck(mBoundPixelPackBuffer.get(), LOCAL_GL_PIXEL_PACK_BUFFER, -1);
  fnCheck(mBoundPixelUnpackBuffer.get(), LOCAL_GL_PIXEL_UNPACK_BUFFER, -1);
  // fnCheck(mBoundTransformFeedbackBuffer.get(),
  // LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER, -1);
  fnCheck(mBoundUniformBuffer.get(), LOCAL_GL_UNIFORM_BUFFER, -1);

  for (const auto i : IntegerRange(mIndexedUniformBufferBindings.size())) {
    const auto& cur = mIndexedUniformBufferBindings[i];
    fnCheck(cur.mBufferBinding.get(), LOCAL_GL_UNIFORM_BUFFER, i);
  }

  fnCheck(mBoundVertexArray->mElementArrayBuffer.get(),
          LOCAL_GL_ELEMENT_ARRAY_BUFFER, -1);
  for (const auto i : IntegerRange(MaxVertexAttribs())) {
    const auto& binding = mBoundVertexArray->AttribBinding(i);
    fnCheck(binding.buffer.get(), LOCAL_GL_ARRAY_BUFFER, i);
  }

  return !dupe;
}

////////////////////////////////////////

template <typename T>
static bool DoSetsIntersect(const std::set<T>& a, const std::set<T>& b) {
  std::vector<T> intersection;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::back_inserter(intersection));
  return !intersection.empty();
}

template <size_t N>
static size_t FindFirstOne(const std::bitset<N>& bs) {
  MOZ_ASSERT(bs.any());
  // We don't need this to be fast, so don't bother with CLZ intrinsics.
  for (const auto i : IntegerRange(N)) {
    if (bs[i]) return i;
  }
  return -1;
}

const webgl::CachedDrawFetchLimits* ValidateDraw(WebGLContext* const webgl,
                                                 const GLenum mode,
                                                 const uint32_t instanceCount) {
  if (!webgl->BindCurFBForDraw()) return nullptr;

  const auto& fb = webgl->mBoundDrawFramebuffer;
  if (fb) {
    const auto& info = *fb->GetCompletenessInfo();
    const auto isF32WithBlending = info.isAttachmentF32 & webgl->mBlendEnabled;
    if (isF32WithBlending.any()) {
      if (!webgl->IsExtensionEnabled(WebGLExtensionID::EXT_float_blend)) {
        const auto first = FindFirstOne(isF32WithBlending);
        webgl->ErrorInvalidOperation(
            "Attachment %u is float32 with blending enabled, which requires "
            "EXT_float_blend.",
            uint32_t(first));
        return nullptr;
      }
      webgl->WarnIfImplicit(WebGLExtensionID::EXT_float_blend);
    }
  }

  switch (mode) {
    case LOCAL_GL_TRIANGLES:
    case LOCAL_GL_TRIANGLE_STRIP:
    case LOCAL_GL_TRIANGLE_FAN:
    case LOCAL_GL_POINTS:
    case LOCAL_GL_LINE_STRIP:
    case LOCAL_GL_LINE_LOOP:
    case LOCAL_GL_LINES:
      break;
    default:
      webgl->ErrorInvalidEnumInfo("mode", mode);
      return nullptr;
  }

  if (!webgl->ValidateStencilParamsForDrawCall()) return nullptr;

  if (!webgl->mActiveProgramLinkInfo) {
    webgl->ErrorInvalidOperation("The current program is not linked.");
    return nullptr;
  }
  const auto& linkInfo = webgl->mActiveProgramLinkInfo;

  // -
  // Check UBO sizes.

  for (const auto i : IntegerRange(linkInfo->uniformBlocks.size())) {
    const auto& cur = linkInfo->uniformBlocks[i];
    const auto& dataSize = cur.info.dataSize;
    const auto& binding = cur.binding;
    if (!binding) {
      webgl->ErrorInvalidOperation("Buffer for uniform block is null.");
      return nullptr;
    }

    const auto availByteCount = binding->ByteCount();
    if (dataSize > availByteCount) {
      webgl->ErrorInvalidOperation(
          "Buffer for uniform block is smaller"
          " than UNIFORM_BLOCK_DATA_SIZE.");
      return nullptr;
    }

    if (!webgl->ValidateBufferForNonTf(binding->mBufferBinding,
                                       LOCAL_GL_UNIFORM_BUFFER, i))
      return nullptr;
  }

  // -

  const auto& tfo = webgl->mBoundTransformFeedback;
  if (tfo && tfo->IsActiveAndNotPaused()) {
    if (fb) {
      const auto& info = *fb->GetCompletenessInfo();
      if (info.isMultiview) {
        webgl->ErrorInvalidOperation(
            "Cannot render to multiview with transform feedback.");
        return nullptr;
      }
    }

    if (!webgl->ValidateBuffersForTf(*tfo, *linkInfo)) return nullptr;
  }

  // -

  const auto& fragOutputs = linkInfo->fragOutputs;
  const auto fnValidateFragOutputType =
      [&](const uint8_t loc, const webgl::TextureBaseType dstBaseType) {
        const auto itr = fragOutputs.find(loc);
        MOZ_DIAGNOSTIC_ASSERT(itr != fragOutputs.end());

        const auto& info = itr->second;
        const auto& srcBaseType = info.baseType;
        if (dstBaseType != srcBaseType) [[unlikely]] {
          const auto& srcStr = ToString(srcBaseType);
          const auto& dstStr = ToString(dstBaseType);
          webgl->ErrorInvalidOperation(
              "Program frag output at location %u is type %s,"
              " but destination draw buffer is type %s.",
              uint32_t(loc), srcStr, dstStr);
          return false;
        }
        return true;
      };

  if (!webgl->mRasterizerDiscardEnabled) {
    uint8_t fbZLayerCount = 1;
    auto hasAttachment = std::bitset<webgl::kMaxDrawBuffers>(1);
    auto drawBufferEnabled = std::bitset<webgl::kMaxDrawBuffers>();
    if (fb) {
      drawBufferEnabled = fb->DrawBufferEnabled();
      const auto& info = *fb->GetCompletenessInfo();
      fbZLayerCount = info.zLayerCount;
      hasAttachment = info.hasAttachment;
    } else {
      drawBufferEnabled[0] = (webgl->mDefaultFB_DrawBuffer0 == LOCAL_GL_BACK);
    }

    if (fbZLayerCount != linkInfo->zLayerCount) {
      webgl->ErrorInvalidOperation(
          "Multiview count mismatch: shader: %u, framebuffer: %u",
          uint32_t{linkInfo->zLayerCount}, uint32_t{fbZLayerCount});
      return nullptr;
    }

    const auto writable =
        hasAttachment & drawBufferEnabled & webgl->mColorWriteMaskNonzero;
    if (writable.any()) {
      // Do we have any undefined outputs with real attachments that
      // aren't masked-out by color write mask or drawBuffers?
      const auto wouldWriteUndefined = ~linkInfo->hasOutput & writable;
      if (wouldWriteUndefined.any()) {
        const auto first = FindFirstOne(wouldWriteUndefined);
        webgl->ErrorInvalidOperation(
            "Program has no frag output at location %u, the"
            " destination draw buffer has an attached"
            " image, and its color write mask is not all false,"
            " and DRAW_BUFFER%u is not NONE.",
            uint32_t(first), uint32_t(first));
        return nullptr;
      }

      const auto outputWrites = linkInfo->hasOutput & writable;

      if (fb) {
        for (const auto& attach : fb->ColorDrawBuffers()) {
          const auto i =
              uint8_t(attach->mAttachmentPoint - LOCAL_GL_COLOR_ATTACHMENT0);
          if (!outputWrites[i]) continue;
          const auto& imageInfo = attach->GetImageInfo();
          if (!imageInfo) continue;
          const auto& dstBaseType = imageInfo->mFormat->format->baseType;
          if (!fnValidateFragOutputType(i, dstBaseType)) return nullptr;
        }
      } else {
        if (outputWrites[0]) {
          if (!fnValidateFragOutputType(0, webgl::TextureBaseType::Float))
            return nullptr;
        }
      }
    }
  }

  // -

  const auto fetchLimits = linkInfo->GetDrawFetchLimits();
  if (!fetchLimits) return nullptr;

  if (instanceCount > fetchLimits->maxInstances) {
    webgl->ErrorInvalidOperation(
        "Instance fetch requires %u, but attribs only"
        " supply %u.",
        instanceCount, uint32_t(fetchLimits->maxInstances));
    return nullptr;
  }

  if (tfo) {
    for (const auto& used : fetchLimits->usedBuffers) {
      MOZ_ASSERT(used.buffer);
      if (!webgl->ValidateBufferForNonTf(*used.buffer, LOCAL_GL_ARRAY_BUFFER,
                                         used.id))
        return nullptr;
    }
  }

  // -

  webgl->RunContextLossTimer();

  return fetchLimits;
}

////////////////////////////////////////

static uint32_t UsedVertsForTFDraw(GLenum mode, uint32_t vertCount) {
  uint8_t vertsPerPrim;

  switch (mode) {
    case LOCAL_GL_POINTS:
      vertsPerPrim = 1;
      break;
    case LOCAL_GL_LINES:
      vertsPerPrim = 2;
      break;
    case LOCAL_GL_TRIANGLES:
      vertsPerPrim = 3;
      break;
    default:
      MOZ_CRASH("`mode`");
  }

  return vertCount / vertsPerPrim * vertsPerPrim;
}

class ScopedDrawWithTransformFeedback final {
  WebGLContext* const mWebGL;
  WebGLTransformFeedback* const mTFO;
  const bool mWithTF;
  uint32_t mUsedVerts;

 public:
  ScopedDrawWithTransformFeedback(WebGLContext* webgl, GLenum mode,
                                  uint32_t vertCount, uint32_t instanceCount,
                                  bool* const out_error)
      : mWebGL(webgl),
        mTFO(mWebGL->mBoundTransformFeedback),
        mWithTF(mTFO && mTFO->mIsActive && !mTFO->mIsPaused),
        mUsedVerts(0) {
    *out_error = false;
    if (!mWithTF) return;

    if (mode != mTFO->mActive_PrimMode) {
      mWebGL->ErrorInvalidOperation(
          "Drawing with transform feedback requires"
          " `mode` to match BeginTransformFeedback's"
          " `primitiveMode`.");
      *out_error = true;
      return;
    }

    const auto usedVertsPerInstance = UsedVertsForTFDraw(mode, vertCount);
    const auto usedVerts =
        CheckedInt<uint32_t>(usedVertsPerInstance) * instanceCount;

    const auto remainingCapacity =
        mTFO->mActive_VertCapacity - mTFO->mActive_VertPosition;
    if (!usedVerts.isValid() || usedVerts.value() > remainingCapacity) {
      mWebGL->ErrorInvalidOperation(
          "Insufficient buffer capacity remaining for"
          " transform feedback.");
      *out_error = true;
      return;
    }

    mUsedVerts = usedVerts.value();
  }

  void Advance() const {
    if (!mWithTF) return;

    mTFO->mActive_VertPosition += mUsedVerts;

    for (const auto& cur : mTFO->mIndexedBindings) {
      const auto& buffer = cur.mBufferBinding;
      if (buffer) {
        buffer->ResetLastUpdateFenceId();
      }
    }
  }
};

static bool HasInstancedDrawing(const WebGLContext& webgl) {
  return webgl.IsWebGL2() ||
         webgl.IsExtensionEnabled(WebGLExtensionID::ANGLE_instanced_arrays);
}

////////////////////////////////////////

void WebGLContext::DrawArraysInstanced(const GLenum mode, const GLint first,
                                       const GLsizei iVertCount,
                                       const GLsizei instanceCount) {
  const FuncScope funcScope(*this, "drawArraysInstanced");
  // AUTO_PROFILER_LABEL("WebGLContext::DrawArraysInstanced", GRAPHICS);
  if (IsContextLost()) return;
  const gl::GLContext::TlsScope inTls(gl);

  // -

  if (!ValidateNonNegative("first", first) ||
      !ValidateNonNegative("vertCount", iVertCount) ||
      !ValidateNonNegative("instanceCount", instanceCount)) {
    return;
  }
  const auto vertCount = AssertedCast<uint32_t>(iVertCount);

  if (IsWebGL2() && !gl->IsSupported(gl::GLFeature::prim_restart_fixed)) {
    MOZ_ASSERT(gl->IsSupported(gl::GLFeature::prim_restart));
    if (mPrimRestartTypeBytes != 0) {
      mPrimRestartTypeBytes = 0;

      // OSX appears to have severe perf issues with leaving this enabled.
      gl->fDisable(LOCAL_GL_PRIMITIVE_RESTART);
    }
  }

  // -

  const auto fetchLimits = ValidateDraw(this, mode, instanceCount);
  if (!fetchLimits) return;

  // -

  const auto totalVertCount_safe = CheckedInt<uint32_t>(first) + vertCount;
  if (!totalVertCount_safe.isValid()) {
    ErrorOutOfMemory("`first+vertCount` out of range.");
    return;
  }
  auto totalVertCount = totalVertCount_safe.value();

  if (vertCount && instanceCount && totalVertCount > fetchLimits->maxVerts) {
    ErrorInvalidOperation(
        "Vertex fetch requires %u, but attribs only supply %u.", totalVertCount,
        uint32_t(fetchLimits->maxVerts));
    return;
  }

  if (vertCount > mMaxVertIdsPerDraw) {
    ErrorOutOfMemory(
        "Context's max vertCount is %u, but %u requested. "
        "[webgl.max-vert-ids-per-draw]",
        mMaxVertIdsPerDraw, vertCount);
    return;
  }

  // -

  bool error = false;

  // -

  const ScopedResolveTexturesForDraw scopedResolve(this, &error);
  if (error) return;

  const ScopedDrawWithTransformFeedback scopedTF(this, mode, vertCount,
                                                 instanceCount, &error);
  if (error) return;

  // On MacOS (Intel?), `first` in glDrawArrays also increases where instanced
  // attribs are fetched from. There are two ways to fix this:
  // 1. DrawElements with a [0,1,2,...] index buffer, converting `first` to
  // `byteOffset`
  // 2. OR offset all non-instanced vertex attrib pointers back, and call
  // DrawArrays with first:0.
  //   * But now gl_VertexID will be wrong! So we inject a uniform to offset it
  //   back correctly.
  // #1 ought to be the lowest overhead for any first>0,
  // but DrawElements can't be used with transform-feedback,
  // so we need #2 to also work.
  // For now, only implement #2.

  const auto& activeAttribs = mActiveProgramLinkInfo->active.activeAttribs;

  auto driverFirst = first;

  if (first && mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst) {
    // This is not particularly optimized, but we can if we need to.
    bool hasInstancedUserAttrib = false;
    bool hasVertexAttrib = false;
    for (const auto& a : activeAttribs) {
      if (a.location == -1) {
        if (a.name == "gl_VertexID") {
          hasVertexAttrib = true;
        }
        continue;
      }
      const auto& binding = mBoundVertexArray->AttribBinding(a.location);
      if (binding.layout.divisor) {
        hasInstancedUserAttrib = true;
      } else {
        hasVertexAttrib = true;
      }
    }
    if (hasInstancedUserAttrib && hasVertexAttrib) {
      driverFirst = 0;
    }
  }
  const bool needsFix_InstancedUserAttribFetch = (driverFirst != first);

  if (needsFix_InstancedUserAttribFetch) {
    for (const auto& a : activeAttribs) {
      if (a.location == -1) continue;
      const auto& binding = mBoundVertexArray->AttribBinding(a.location);
      if (binding.layout.divisor) continue;

      mBoundVertexArray->DoVertexAttrib(a.location, first);
    }

    gl->fUniform1i(mActiveProgramLinkInfo->webgl_gl_VertexID_Offset, first);
  }
  const auto undoFix_InstancedUserAttribFetch = MakeScopeExit([&]() {
    if (needsFix_InstancedUserAttribFetch) {
      gl->fUniform1i(mActiveProgramLinkInfo->webgl_gl_VertexID_Offset, 0);

      for (const auto& a : activeAttribs) {
        if (a.location == -1) continue;
        const auto& binding = mBoundVertexArray->AttribBinding(a.location);
        if (binding.layout.divisor) continue;

        mBoundVertexArray->DoVertexAttrib(a.location, 0);
      }
    }
  });

  // -

  {
    const auto whatDoesAttrib0Need = WhatDoesVertexAttrib0Need();
    auto fakeVertCount = uint64_t(driverFirst) + vertCount;
    if (whatDoesAttrib0Need == WebGLVertexAttrib0Status::Default) {
      fakeVertCount = 0;
    }
    if (!(vertCount && instanceCount)) {
      fakeVertCount = 0;
    }

    const bool needsFix_FakeVertexAttrib0 = bool(fakeVertCount);
    const auto undoFix_FakeVertexAttrib0 = MakeScopeExit([&]() {
      if (needsFix_FakeVertexAttrib0) {
        mBoundVertexArray->DoVertexAttrib(0);
      }
    });
    if (needsFix_FakeVertexAttrib0) {
      // fmt::println(FMT_STRING("DoFakeVertexAttrib0(fakeVertCount: {},
      // whatDoesAttrib0Need: {})"), fakeVertCount, (int)whatDoesAttrib0Need);
      if (!DoFakeVertexAttrib0(fakeVertCount, whatDoesAttrib0Need)) return;
    }

    ScopedDrawCallWrapper wrapper(*this);

    if (vertCount && instanceCount) {
      if (HasInstancedDrawing(*this)) {
        gl->fDrawArraysInstanced(mode, driverFirst, vertCount, instanceCount);
      } else {
        MOZ_ASSERT(instanceCount == 1);
        gl->fDrawArrays(mode, driverFirst, vertCount);
      }
    }
  }

  Draw_cleanup();
  scopedTF.Advance();
}

////////////////////////////////////////

WebGLBuffer* WebGLContext::DrawElements_check(const GLsizei rawIndexCount,
                                              const GLenum type,
                                              const WebGLintptr byteOffset,
                                              const GLsizei instanceCount) {
  if (mBoundTransformFeedback && mBoundTransformFeedback->mIsActive &&
      !mBoundTransformFeedback->mIsPaused) {
    ErrorInvalidOperation(
        "DrawElements* functions are incompatible with"
        " transform feedback.");
    return nullptr;
  }

  if (!ValidateNonNegative("vertCount", rawIndexCount) ||
      !ValidateNonNegative("byteOffset", byteOffset) ||
      !ValidateNonNegative("instanceCount", instanceCount)) {
    return nullptr;
  }
  const auto indexCount = uint32_t(rawIndexCount);

  uint8_t bytesPerIndex = 0;
  switch (type) {
    case LOCAL_GL_UNSIGNED_BYTE:
      bytesPerIndex = 1;
      break;

    case LOCAL_GL_UNSIGNED_SHORT:
      bytesPerIndex = 2;
      break;

    case LOCAL_GL_UNSIGNED_INT:
      if (IsWebGL2() ||
          IsExtensionEnabled(WebGLExtensionID::OES_element_index_uint)) {
        bytesPerIndex = 4;
      }
      break;
  }
  if (!bytesPerIndex) {
    ErrorInvalidEnumInfo("type", type);
    return nullptr;
  }
  if (byteOffset % bytesPerIndex != 0) {
    ErrorInvalidOperation(
        "`byteOffset` must be a multiple of the size of `type`");
    return nullptr;
  }

  ////

  if (IsWebGL2() && !gl->IsSupported(gl::GLFeature::prim_restart_fixed)) {
    MOZ_ASSERT(gl->IsSupported(gl::GLFeature::prim_restart));
    if (mPrimRestartTypeBytes != bytesPerIndex) {
      mPrimRestartTypeBytes = bytesPerIndex;

      const uint32_t ones = UINT32_MAX >> (32 - 8 * mPrimRestartTypeBytes);
      gl->fEnable(LOCAL_GL_PRIMITIVE_RESTART);
      gl->fPrimitiveRestartIndex(ones);
    }
  }

  ////
  // Index fetching

  const auto& indexBuffer = mBoundVertexArray->mElementArrayBuffer;
  if (!indexBuffer) {
    ErrorInvalidOperation("Index buffer not bound.");
    return nullptr;
  }

  const size_t availBytes = indexBuffer->ByteLength();
  const auto availIndices =
      AvailGroups(availBytes, byteOffset, bytesPerIndex, bytesPerIndex);
  if (instanceCount && indexCount > availIndices) {
    ErrorInvalidOperation("Index buffer too small.");
    return nullptr;
  }

  return indexBuffer.get();
}

static void HandleDrawElementsErrors(
    WebGLContext* webgl, gl::GLContext::LocalErrorScope& errorScope) {
  const auto err = errorScope.GetError();
  if (err == LOCAL_GL_INVALID_OPERATION) {
    webgl->ErrorInvalidOperation(
        "Driver rejected indexed draw call, possibly"
        " due to out-of-bounds indices.");
    return;
  }

  MOZ_ASSERT(!err);
  if (err) {
    webgl->ErrorImplementationBug(
        "Unexpected driver error during indexed draw"
        " call. Please file a bug.");
    return;
  }
}

void WebGLContext::DrawElementsInstanced(const GLenum mode,
                                         const GLsizei iIndexCount,
                                         const GLenum type,
                                         const WebGLintptr byteOffset,
                                         const GLsizei instanceCount) {
  const FuncScope funcScope(*this, "drawElementsInstanced");
  // AUTO_PROFILER_LABEL("WebGLContext::DrawElementsInstanced", GRAPHICS);
  if (IsContextLost()) return;

  const gl::GLContext::TlsScope inTls(gl);

  const auto indexBuffer =
      DrawElements_check(iIndexCount, type, byteOffset, instanceCount);
  if (!indexBuffer) return;
  const auto indexCount = AssertedCast<uint32_t>(iIndexCount);

  // -

  const auto fetchLimits = ValidateDraw(this, mode, instanceCount);
  if (!fetchLimits) return;

  const auto whatDoesAttrib0Need = WhatDoesVertexAttrib0Need();

  uint64_t fakeVertCount = 0;
  if (whatDoesAttrib0Need != WebGLVertexAttrib0Status::Default) {
    fakeVertCount = fetchLimits->maxVerts;
  }
  if (!indexCount || !instanceCount) {
    fakeVertCount = 0;
  }
  if (fakeVertCount == UINT64_MAX) {  // Ok well that's too many!
    const auto exactMaxVertId =
        indexBuffer->GetIndexedFetchMaxVert(type, byteOffset, indexCount);
    MOZ_RELEASE_ASSERT(exactMaxVertId);
    fakeVertCount = uint32_t{*exactMaxVertId};
    fakeVertCount += 1;
  }

  // -

  {
    uint64_t indexCapacity = indexBuffer->ByteLength();
    switch (type) {
      case LOCAL_GL_UNSIGNED_BYTE:
        break;
      case LOCAL_GL_UNSIGNED_SHORT:
        indexCapacity /= 2;
        break;
      case LOCAL_GL_UNSIGNED_INT:
        indexCapacity /= 4;
        break;
    }

    uint32_t maxVertId = 0;
    const auto isFetchValid = [&]() {
      if (!indexCount || !instanceCount) return true;

      const auto globalMaxVertId =
          indexBuffer->GetIndexedFetchMaxVert(type, 0, indexCapacity);
      if (!globalMaxVertId) return true;
      if (globalMaxVertId.value() < fetchLimits->maxVerts) return true;

      const auto exactMaxVertId =
          indexBuffer->GetIndexedFetchMaxVert(type, byteOffset, indexCount);
      maxVertId = exactMaxVertId.value();
      return maxVertId < fetchLimits->maxVerts;
    }();
    if (!isFetchValid) {
      ErrorInvalidOperation(
          "Indexed vertex fetch requires %u vertices, but"
          " attribs only supply %u.",
          maxVertId + 1, uint32_t(fetchLimits->maxVerts));
      return;
    }
  }

  if (indexCount > mMaxVertIdsPerDraw) {
    ErrorOutOfMemory(
        "Context's max indexCount is %u, but %u requested. "
        "[webgl.max-vert-ids-per-draw]",
        mMaxVertIdsPerDraw, indexCount);
    return;
  }

  // -

  const bool needsFix_FakeVertexAttrib0 = bool(fakeVertCount);
  const auto undoFix_FakeVertexAttrib0 = MakeScopeExit([&]() {
    if (needsFix_FakeVertexAttrib0) {
      mBoundVertexArray->DoVertexAttrib(0);
    }
  });
  if (needsFix_FakeVertexAttrib0) {
    if (!DoFakeVertexAttrib0(fakeVertCount, whatDoesAttrib0Need)) return;
  }

  // -

  bool error = false;
  const ScopedResolveTexturesForDraw scopedResolve(this, &error);
  if (error) return;

  {
    ScopedDrawCallWrapper wrapper(*this);
    {
      std::unique_ptr<gl::GLContext::LocalErrorScope> errorScope;
      if (gl->IsANGLE() &&
          (gl->mDebugFlags & gl::GLContext::DebugFlagAbortOnError))
          [[unlikely]] {
        // ANGLE does range validation even when it doesn't need to.
        // With MOZ_GL_ABORT_ON_ERROR, we need to catch it or hit assertions.
        errorScope.reset(new gl::GLContext::LocalErrorScope(*gl));
      }

      if (indexCount && instanceCount) {
        if (HasInstancedDrawing(*this)) {
          gl->fDrawElementsInstanced(mode, indexCount, type,
                                     reinterpret_cast<GLvoid*>(byteOffset),
                                     instanceCount);
        } else {
          MOZ_ASSERT(instanceCount == 1);
          gl->fDrawElements(mode, indexCount, type,
                            reinterpret_cast<GLvoid*>(byteOffset));
        }
      }

      if (errorScope) {
        HandleDrawElementsErrors(this, *errorScope);
      }
    }
  }

  Draw_cleanup();
}

////////////////////////////////////////

void WebGLContext::Draw_cleanup() {
  if (gl->WorkAroundDriverBugs()) {
    if (gl->Renderer() == gl::GLRenderer::Tegra) {
      mDrawCallsSinceLastFlush++;

      if (mDrawCallsSinceLastFlush >= MAX_DRAW_CALLS_SINCE_FLUSH) {
        gl->fFlush();
        mDrawCallsSinceLastFlush = 0;
      }
    }
  }

  // Let's check for a really common error: Viewport is larger than the actual
  // destination framebuffer.
  uint32_t destWidth;
  uint32_t destHeight;
  if (mBoundDrawFramebuffer) {
    const auto& info = mBoundDrawFramebuffer->GetCompletenessInfo();
    destWidth = info->width;
    destHeight = info->height;
  } else {
    destWidth = mDefaultFB->mSize.width;
    destHeight = mDefaultFB->mSize.height;
  }

  if (mViewportWidth > int32_t(destWidth) ||
      mViewportHeight > int32_t(destHeight)) {
    if (!mAlreadyWarnedAboutViewportLargerThanDest) {
      GenerateWarning(
          "Drawing to a destination rect smaller than the viewport"
          " rect. (This warning will only be given once)");
      mAlreadyWarnedAboutViewportLargerThanDest = true;
    }
  }
}

WebGLVertexAttrib0Status WebGLContext::WhatDoesVertexAttrib0Need() const {
  MOZ_ASSERT(mCurrentProgram);
  MOZ_ASSERT(mActiveProgramLinkInfo);

  bool legacyAttrib0 = mNeedsLegacyVertexAttrib0Handling;
  if (gl->WorkAroundDriverBugs() && kIsMacOS) {
    // Also programs with no attribs:
    // conformance/attribs/gl-vertex-attrib-unconsumed-out-of-bounds.html
    const auto& activeAttribs = mActiveProgramLinkInfo->active.activeAttribs;
    bool hasNonInstancedUserAttrib = false;
    for (const auto& a : activeAttribs) {
      if (a.location == -1) continue;
      const auto& layout = mBoundVertexArray->AttribBinding(a.location).layout;
      if (layout.divisor == 0) {
        hasNonInstancedUserAttrib = true;
      }
    }
    legacyAttrib0 |= !hasNonInstancedUserAttrib;
  }

  if (!legacyAttrib0) return WebGLVertexAttrib0Status::Default;
  MOZ_RELEASE_ASSERT(mMaybeNeedsLegacyVertexAttrib0Handling,
                     "Invariant need because this turns on index buffer "
                     "validation, needed for fake-attrib0.");

  if (!mActiveProgramLinkInfo->attrib0Active) {
    // Attrib0 unused, so just ensure that the legacy code has enough buffer.
    return WebGLVertexAttrib0Status::EmulatedUninitializedArray;
  }

  const auto& isAttribArray0Enabled =
      mBoundVertexArray->AttribBinding(0).layout.isArray;
  return isAttribArray0Enabled
             ? WebGLVertexAttrib0Status::Default
             : WebGLVertexAttrib0Status::EmulatedInitializedArray;
}

bool WebGLContext::DoFakeVertexAttrib0(
    const uint64_t fakeVertexCount,
    const WebGLVertexAttrib0Status whatDoesAttrib0Need) {
  MOZ_ASSERT(fakeVertexCount);
  MOZ_RELEASE_ASSERT(whatDoesAttrib0Need != WebGLVertexAttrib0Status::Default);

  if (gl->WorkAroundDriverBugs() && gl->IsMesa()) {
    // Padded/strided to vec4, so 4x4bytes.
    const auto effectiveVertAttribBytes =
        CheckedInt<int32_t>(fakeVertexCount) * 4 * 4;
    if (!effectiveVertAttribBytes.isValid()) {
      ErrorOutOfMemory("`offset + count` too large for Mesa.");
      return false;
    }
  }

  if (!mAlreadyWarnedAboutFakeVertexAttrib0) {
    GenerateWarning(
        "Drawing without vertex attrib 0 array enabled forces the browser "
        "to do expensive emulation work when running on desktop OpenGL "
        "platforms, for example on Mac. It is preferable to always draw "
        "with vertex attrib 0 array enabled, by using bindAttribLocation "
        "to bind some always-used attribute to location 0.");
    mAlreadyWarnedAboutFakeVertexAttrib0 = true;
  }

  if (!mFakeVertexAttrib0BufferObject) {
    gl->fGenBuffers(1, &mFakeVertexAttrib0BufferObject);
    mFakeVertexAttrib0BufferAllocSize = 0;
    mFakeVertexAttrib0BufferInitializedSize = 0;
  }

  ////

  const auto maxFakeVerts = StaticPrefs::webgl_fake_verts_max();
  if (fakeVertexCount > maxFakeVerts) {
    ErrorOutOfMemory(
        "Draw requires faking a vertex attrib 0 array, but required vert count"
        " (%" PRIu64 ") is more than webgl.fake-verts.max (%u).",
        fakeVertexCount, maxFakeVerts);
    return false;
  }

  const auto bytesPerVert = sizeof(mFakeVertexAttrib0Data);
  const auto checked_dataSize =
      CheckedInt<intptr_t>(fakeVertexCount) * bytesPerVert;
  if (!checked_dataSize.isValid()) {
    ErrorOutOfMemory(
        "Integer overflow trying to construct a fake vertex attrib 0"
        " array for a draw-operation with %" PRIu64
        " vertices. Try"
        " reducing the number of vertices.",
        fakeVertexCount);
    return false;
  }
  const auto dataSize = checked_dataSize.value();

  if (mFakeVertexAttrib0BufferAllocSize < dataSize) {
    gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mFakeVertexAttrib0BufferObject);
    gl::GLContext::LocalErrorScope errorScope(*gl);

    gl->fBufferData(LOCAL_GL_ARRAY_BUFFER, dataSize, nullptr,
                    LOCAL_GL_STATIC_DRAW);

    const auto err = errorScope.GetError();
    if (err) {
      ErrorOutOfMemory(
          "Failed to allocate fake vertex attrib 0 buffer: %zi bytes",
          dataSize);
      return false;
    }

    mFakeVertexAttrib0BufferAllocSize = dataSize;
    mFakeVertexAttrib0BufferInitializedSize = 0;
  }

  ////

  const auto FillWithGenericVertexAttrib0Data = [&]() {
    if (dataSize <= mFakeVertexAttrib0BufferInitializedSize &&
        memcmp(mFakeVertexAttrib0Data, mGenericVertexAttrib0Data,
               bytesPerVert) == 0) {
      return true;
    }
    memcpy(mFakeVertexAttrib0Data, mGenericVertexAttrib0Data, bytesPerVert);
    mFakeVertexAttrib0BufferInitializedSize = 0;

    using DataPerVertT = decltype(mFakeVertexAttrib0Data);
    static_assert(sizeof(DataPerVertT) == 4 * sizeof(float));
    MOZ_RELEASE_ASSERT(dataSize % sizeof(DataPerVertT) == 0);
    const size_t vertCount = dataSize / sizeof(DataPerVertT);
    const auto uploadData = std::unique_ptr<DataPerVertT[]>{
        new (std::nothrow) DataPerVertT[vertCount]};
    if (!uploadData) {
      ErrorOutOfMemory("Failed to allocate fake vertex attrib 0 upload data.");
      return false;
    }
    const auto uploadMutSpan = Span{uploadData.get(), vertCount};
    for (auto& vert : uploadMutSpan) {
      memcpy(&vert, mFakeVertexAttrib0Data, bytesPerVert);
    }

    {
      gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mFakeVertexAttrib0BufferObject);
      gl::GLContext::LocalErrorScope errorScope(*gl);

      const auto uploadBytes = AsBytes(uploadMutSpan);
      MOZ_ASSERT(uploadBytes.size() == (size_t)dataSize);
      gl->fBufferSubData(LOCAL_GL_ARRAY_BUFFER, 0, uploadBytes.size(),
                         uploadBytes.data());

      const auto err = errorScope.GetError();
      if (err) {
        ErrorOutOfMemory("Failed to upload fake vertex attrib 0 data.");
        return false;
      }
    }

    mFakeVertexAttrib0BufferInitializedSize = dataSize;
    return true;
  };

  if (whatDoesAttrib0Need ==
      WebGLVertexAttrib0Status::EmulatedInitializedArray) {
    if (!FillWithGenericVertexAttrib0Data()) return false;
  }

  ////

  const auto& attrib0 = mBoundVertexArray->AttribBinding(0);
  if (attrib0.layout.divisor) {
    gl->fVertexAttribDivisor(0, 0);
  }

  gl->fEnableVertexAttribArray(0);

  gl->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mFakeVertexAttrib0BufferObject);
  switch (mGenericVertexAttribTypes[0]) {
    case webgl::AttribBaseType::Boolean:
    case webgl::AttribBaseType::Float:
      gl->fVertexAttribPointer(0, 4, LOCAL_GL_FLOAT, false, 0, 0);
      break;

    case webgl::AttribBaseType::Int:
      gl->fVertexAttribIPointer(0, 4, LOCAL_GL_INT, 0, 0);
      break;

    case webgl::AttribBaseType::Uint:
      gl->fVertexAttribIPointer(0, 4, LOCAL_GL_UNSIGNED_INT, 0, 0);
      break;
  }

  ////

  return true;
}

}  // namespace mozilla
