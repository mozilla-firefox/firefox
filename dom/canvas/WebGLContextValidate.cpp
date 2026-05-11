/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "CanvasUtils.h"
#include "GLContext.h"
#include "GLSLANG/ShaderLang.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"
#include "WebGLValidateStrings.h"
#include "WebGLVertexArray.h"
#include "gfxEnv.h"
#include "jsfriendapi.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "nsPrintfCString.h"

////////////////////
// Minimum value constants defined in GLES 2.0.25 $6.2 "State Tables":
const uint32_t kMinMaxVertexAttribs = 8;            // Page 164
const uint32_t kMinMaxVertexUniformVectors = 128;   // Page 164
const uint32_t kMinMaxFragmentUniformVectors = 16;  // Page 164
const uint32_t kMinMaxVaryingVectors = 8;           // Page 164

const uint32_t kMinMaxVertexTextureImageUnits = 0;    // Page 164
const uint32_t kMinMaxFragmentTextureImageUnits = 8;  // Page 164
const uint32_t kMinMaxCombinedTextureImageUnits = 8;  // Page 164

const uint32_t kMinMaxDrawBuffers = 4;

// These few deviate from the spec: (The minimum values in the spec are
// ridiculously low)
const uint32_t kMinMaxTextureSize = 1024;        // ES2 spec says `64` (p162)
const uint32_t kMinMaxCubeMapTextureSize = 512;  // ES2 spec says `16` (p162)
const uint32_t kMinMaxRenderbufferSize = 1024;   // ES2 spec says `1` (p164)

// Minimum value constants defined in GLES 3.0.4 $6.2 "State Tables":
const uint32_t kMinMax3DTextureSize = 256;
const uint32_t kMinMaxArrayTextureLayers = 256;

////////////////////
// "Common" but usable values to avoid WebGL fingerprinting:
const uint32_t kCommonMaxTextureSize = 2048;
const uint32_t kCommonMaxCubeMapTextureSize = 2048;
const uint32_t kCommonMaxRenderbufferSize = 2048;

const uint32_t kCommonMaxVertexTextureImageUnits = 8;
const uint32_t kCommonMaxFragmentTextureImageUnits = 8;
const uint32_t kCommonMaxCombinedTextureImageUnits = 16;

const uint32_t kCommonMaxVertexAttribs = 16;
const uint32_t kCommonMaxVertexUniformVectors = 256;
const uint32_t kCommonMaxFragmentUniformVectors = 224;
const uint32_t kCommonMaxVaryingVectors = 8;

const uint32_t kCommonMaxViewportDims = 4096;

// The following ranges came from a 2013 Moto E and an old macbook.
const float kCommonAliasedPointSizeRangeMin = 1;
const float kCommonAliasedPointSizeRangeMax = 63;
const float kCommonAliasedLineWidthRangeMin = 1;
const float kCommonAliasedLineWidthRangeMax = 1;

template <class T>
static bool RestrictCap(T* const cap, const T restrictedVal) {
  if (*cap < restrictedVal) {
    return false;  // already too low!
  }

  *cap = restrictedVal;
  return true;
}

////////////////////

namespace mozilla {

bool WebGLContext::ValidateBlendEquationEnum(GLenum mode, const char* info) {
  switch (mode) {
    case LOCAL_GL_FUNC_ADD:
    case LOCAL_GL_FUNC_SUBTRACT:
    case LOCAL_GL_FUNC_REVERSE_SUBTRACT:
      return true;

    case LOCAL_GL_MIN:
    case LOCAL_GL_MAX:
      if (IsWebGL2() ||
          IsExtensionEnabled(WebGLExtensionID::EXT_blend_minmax)) {
        return true;
      }

      break;

    default:
      break;
  }

  ErrorInvalidEnumInfo(info, mode);
  return false;
}

bool WebGLContext::ValidateBlendFuncEnumsCompatibility(GLenum sfactor,
                                                       GLenum dfactor,
                                                       const char* info) {
  bool sfactorIsConstantColor = sfactor == LOCAL_GL_CONSTANT_COLOR ||
                                sfactor == LOCAL_GL_ONE_MINUS_CONSTANT_COLOR;
  bool sfactorIsConstantAlpha = sfactor == LOCAL_GL_CONSTANT_ALPHA ||
                                sfactor == LOCAL_GL_ONE_MINUS_CONSTANT_ALPHA;
  bool dfactorIsConstantColor = dfactor == LOCAL_GL_CONSTANT_COLOR ||
                                dfactor == LOCAL_GL_ONE_MINUS_CONSTANT_COLOR;
  bool dfactorIsConstantAlpha = dfactor == LOCAL_GL_CONSTANT_ALPHA ||
                                dfactor == LOCAL_GL_ONE_MINUS_CONSTANT_ALPHA;
  if ((sfactorIsConstantColor && dfactorIsConstantAlpha) ||
      (dfactorIsConstantColor && sfactorIsConstantAlpha)) {
    ErrorInvalidOperation(
        "%s are mutually incompatible, see section 6.8 in"
        " the WebGL 1.0 spec",
        info);
    return false;
  }

  return true;
}

bool WebGLContext::ValidateStencilOpEnum(GLenum action, const char* info) {
  switch (action) {
    case LOCAL_GL_KEEP:
    case LOCAL_GL_ZERO:
    case LOCAL_GL_REPLACE:
    case LOCAL_GL_INCR:
    case LOCAL_GL_INCR_WRAP:
    case LOCAL_GL_DECR:
    case LOCAL_GL_DECR_WRAP:
    case LOCAL_GL_INVERT:
      return true;

    default:
      ErrorInvalidEnumInfo(info, action);
      return false;
  }
}

bool WebGLContext::ValidateFaceEnum(const GLenum face) {
  switch (face) {
    case LOCAL_GL_FRONT:
    case LOCAL_GL_BACK:
    case LOCAL_GL_FRONT_AND_BACK:
      return true;

    default:
      ErrorInvalidEnumInfo("face", face);
      return false;
  }
}

bool WebGLContext::ValidateAttribArraySetter(uint32_t setterElemSize,
                                             uint32_t arrayLength) {
  if (IsContextLost()) return false;

  if (arrayLength < setterElemSize) {
    ErrorInvalidValue("Array must have >= %d elements.", setterElemSize);
    return false;
  }

  return true;
}

// ---------------------

static webgl::Limits MakeLimits(const WebGLContext& webgl) {
  webgl::Limits limits;

  gl::GLContext& gl = *webgl.GL();

  // -

  for (const auto i : IntegerRange(UnderlyingValue(WebGLExtensionID::Max))) {
    const auto ext = WebGLExtensionID(i);
    limits.supportedExtensions[ext] = webgl.IsExtensionSupported(ext);
  }

  // -
  // WebGL 1

  // Note: GL_MAX_TEXTURE_UNITS is fixed at 4 for most desktop hardware,
  // even though the hardware supports much more.  The
  // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS value is the accurate value.
  gl.GetUIntegerv(LOCAL_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                  &limits.maxTexUnits);
  limits.maxTexUnits = std::min(
      limits.maxTexUnits, uint32_t{UINT8_MAX});  // We want to use uint8_t.

  gl.GetUIntegerv(LOCAL_GL_MAX_TEXTURE_SIZE, &limits.maxTex2dSize);
  gl.GetUIntegerv(LOCAL_GL_MAX_CUBE_MAP_TEXTURE_SIZE, &limits.maxTexCubeSize);
  gl.GetUIntegerv(LOCAL_GL_MAX_VERTEX_ATTRIBS, &limits.maxVertexAttribs);

  auto dims = std::array<uint32_t, 2>{};
  gl.GetUIntegerv(LOCAL_GL_MAX_VIEWPORT_DIMS, dims.data());
  limits.maxViewportDim = std::min(dims[0], dims[1]);

  if (!gl.IsCoreProfile()) {
    gl.fGetFloatv(LOCAL_GL_ALIASED_LINE_WIDTH_RANGE,
                  limits.lineWidthRange.data());
  }

  {
    const GLenum driverPName = gl.IsCoreProfile()
                                   ? LOCAL_GL_POINT_SIZE_RANGE
                                   : LOCAL_GL_ALIASED_POINT_SIZE_RANGE;
    gl.fGetFloatv(driverPName, limits.pointSizeRange.data());
  }

  if (webgl.IsWebGL2()) {
    gl.GetUIntegerv(LOCAL_GL_MAX_ARRAY_TEXTURE_LAYERS,
                    &limits.maxTexArrayLayers);
    gl.GetUIntegerv(LOCAL_GL_MAX_3D_TEXTURE_SIZE, &limits.maxTex3dSize);
    gl.GetUIntegerv(LOCAL_GL_MAX_UNIFORM_BUFFER_BINDINGS,
                    &limits.maxUniformBufferBindings);
    gl.GetUIntegerv(LOCAL_GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
                    &limits.uniformBufferOffsetAlignment);
  }

  if (limits.supportedExtensions
          [WebGLExtensionID::WEBGL_compressed_texture_astc]) {
    limits.astcHdr = gl.IsExtensionSupported(
        gl::GLContext::KHR_texture_compression_astc_hdr);
  }

  if (webgl.IsWebGL2() ||
      limits.supportedExtensions[WebGLExtensionID::WEBGL_draw_buffers]) {
    gl.GetUIntegerv(LOCAL_GL_MAX_DRAW_BUFFERS, &limits.maxColorDrawBuffers);
  }

  if (limits.supportedExtensions[WebGLExtensionID::EXT_disjoint_timer_query]) {
    gl.fGetQueryiv(LOCAL_GL_TIME_ELAPSED_EXT, LOCAL_GL_QUERY_COUNTER_BITS,
                   (int32_t*)&limits.queryCounterBitsTimeElapsed);
    gl.fGetQueryiv(LOCAL_GL_TIMESTAMP_EXT, LOCAL_GL_QUERY_COUNTER_BITS,
                   (int32_t*)&limits.queryCounterBitsTimestamp);
  }

  if (limits.supportedExtensions[WebGLExtensionID::OVR_multiview2]) {
    gl.GetUIntegerv(LOCAL_GL_MAX_VIEWS_OVR, &limits.maxMultiviewLayers);
  }

  return limits;
}

bool WebGLContext::InitAndValidateGL(FailureReason* const out_failReason) {
  MOZ_RELEASE_ASSERT(gl, "GFX: GL not initialized");

  // Unconditionally create a new format usage authority. This is
  // important when restoring contexts and extensions need to add
  // formats back into the authority.
  mFormatUsage = CreateFormatUsage(gl);
  if (!mFormatUsage) {
    *out_failReason = {"FEATURE_FAILURE_WEBGL_FORMAT",
                       "Failed to create mFormatUsage."};
    return false;
  }

  GLenum error = gl->fGetError();
  if (error != LOCAL_GL_NO_ERROR) {
    const nsPrintfCString reason(
        "GL error 0x%x occurred during OpenGL context"
        " initialization, before WebGL initialization!",
        error);
    *out_failReason = {"FEATURE_FAILURE_WEBGL_GLERR_1", reason};
    return false;
  }

  mLoseContextOnMemoryPressure =
      StaticPrefs::webgl_lose_context_on_memory_pressure();
  mCanLoseContextInForeground =
      StaticPrefs::webgl_can_lose_context_in_foreground();

  /*
  // Technically, we should be setting mStencil[...] values to
  // `allOnes`, but either ANGLE breaks or the SGX540s on Try break.
  GLuint stencilBits = 0;
  gl->GetUIntegerv(LOCAL_GL_STENCIL_BITS, &stencilBits);
  GLuint allOnes = ~(UINT32_MAX << stencilBits);
  mStencilValueMaskFront = allOnes;
  mStencilValueMaskBack  = allOnes;
  mStencilWriteMaskFront = allOnes;
  mStencilWriteMaskBack  = allOnes;
  */

  gl->GetUIntegerv(LOCAL_GL_STENCIL_VALUE_MASK, &mStencilValueMaskFront);
  gl->GetUIntegerv(LOCAL_GL_STENCIL_BACK_VALUE_MASK, &mStencilValueMaskBack);
  gl->GetUIntegerv(LOCAL_GL_STENCIL_WRITEMASK, &mStencilWriteMaskFront);
  gl->GetUIntegerv(LOCAL_GL_STENCIL_BACK_WRITEMASK, &mStencilWriteMaskBack);

  AssertUintParamCorrect(gl, LOCAL_GL_STENCIL_VALUE_MASK,
                         mStencilValueMaskFront);
  AssertUintParamCorrect(gl, LOCAL_GL_STENCIL_BACK_VALUE_MASK,
                         mStencilValueMaskBack);
  AssertUintParamCorrect(gl, LOCAL_GL_STENCIL_WRITEMASK,
                         mStencilWriteMaskFront);
  AssertUintParamCorrect(gl, LOCAL_GL_STENCIL_BACK_WRITEMASK,
                         mStencilWriteMaskBack);

  // Bindings, etc.

  mBound2DTextures.Clear();
  mBoundCubeMapTextures.Clear();
  mBound3DTextures.Clear();
  mBound2DArrayTextures.Clear();
  mBoundSamplers.Clear();

  mBoundArrayBuffer = nullptr;
  mCurrentProgram = nullptr;

  mBoundDrawFramebuffer = nullptr;
  mBoundReadFramebuffer = nullptr;

  // -----------------------

  auto limits = MakeLimits(*this);

  // -

  if (limits.maxVertexAttribs < 8) {
    const nsPrintfCString reason("GL_MAX_VERTEX_ATTRIBS: %d is < 8!",
                                 limits.maxVertexAttribs);
    *out_failReason = {"FEATURE_FAILURE_WEBGL_V_ATRB", reason};
    return false;
  }

  if (limits.maxTexUnits < 8) {
    const nsPrintfCString reason(
        "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: %u is < 8!", limits.maxTexUnits);
    *out_failReason = {"FEATURE_FAILURE_WEBGL_T_UNIT", reason};
    return false;
  }

  mBound2DTextures.SetLength(limits.maxTexUnits);
  mBoundCubeMapTextures.SetLength(limits.maxTexUnits);
  mBound3DTextures.SetLength(limits.maxTexUnits);
  mBound2DArrayTextures.SetLength(limits.maxTexUnits);
  mBoundSamplers.SetLength(limits.maxTexUnits);

  ////////////////

  gl->GetUIntegerv(LOCAL_GL_MAX_RENDERBUFFER_SIZE, &mGLMaxRenderbufferSize);
  gl->GetUIntegerv(LOCAL_GL_MAX_TEXTURE_IMAGE_UNITS,
                   &mGLMaxFragmentTextureImageUnits);
  gl->GetUIntegerv(LOCAL_GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
                   &mGLMaxVertexTextureImageUnits);

  ////////////////

  if (gl->IsGLES()) {
    mGLMaxFragmentUniformVectors =
        gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_FRAGMENT_UNIFORM_VECTORS);
    mGLMaxVertexUniformVectors =
        gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VERTEX_UNIFORM_VECTORS);
    if (gl->Version() >= 300) {
      mGLMaxVertexOutputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VERTEX_OUTPUT_COMPONENTS) / 4;
      mGLMaxFragmentInputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_FRAGMENT_INPUT_COMPONENTS) / 4;
    } else {
      mGLMaxFragmentInputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VARYING_VECTORS);
      mGLMaxVertexOutputVectors = mGLMaxFragmentInputVectors;
    }
  } else {
    mGLMaxFragmentUniformVectors =
        gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_FRAGMENT_UNIFORM_COMPONENTS) / 4;
    mGLMaxVertexUniformVectors =
        gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VERTEX_UNIFORM_COMPONENTS) / 4;

    if (gl->Version() >= 320) {
      mGLMaxVertexOutputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VERTEX_OUTPUT_COMPONENTS) / 4;
      mGLMaxFragmentInputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_FRAGMENT_INPUT_COMPONENTS) / 4;
    } else {
      // Same enum val as GL2's GL_MAX_VARYING_FLOATS.
      mGLMaxFragmentInputVectors =
          gl->GetIntAs<uint32_t>(LOCAL_GL_MAX_VARYING_COMPONENTS) / 4;
      mGLMaxVertexOutputVectors = mGLMaxFragmentInputVectors;
    }
  }

  ////////////////

  if (StaticPrefs::webgl_min_capability_mode()) {
    bool ok = true;

    ok &= RestrictCap(&mGLMaxVertexTextureImageUnits,
                      kMinMaxVertexTextureImageUnits);
    ok &= RestrictCap(&mGLMaxFragmentTextureImageUnits,
                      kMinMaxFragmentTextureImageUnits);
    ok &= RestrictCap(&limits.maxTexUnits, kMinMaxCombinedTextureImageUnits);

    ok &= RestrictCap(&limits.maxVertexAttribs, kMinMaxVertexAttribs);
    ok &= RestrictCap(&mGLMaxVertexUniformVectors, kMinMaxVertexUniformVectors);
    ok &= RestrictCap(&mGLMaxFragmentUniformVectors,
                      kMinMaxFragmentUniformVectors);
    ok &= RestrictCap(&mGLMaxVertexOutputVectors, kMinMaxVaryingVectors);
    ok &= RestrictCap(&mGLMaxFragmentInputVectors, kMinMaxVaryingVectors);

    ok &= RestrictCap(&limits.maxColorDrawBuffers, kMinMaxDrawBuffers);

    ok &= RestrictCap(&limits.maxTex2dSize, kMinMaxTextureSize);
    ok &= RestrictCap(&limits.maxTexCubeSize, kMinMaxCubeMapTextureSize);
    ok &= RestrictCap(&limits.maxTex3dSize, kMinMax3DTextureSize);

    ok &= RestrictCap(&limits.maxTexArrayLayers, kMinMaxArrayTextureLayers);
    ok &= RestrictCap(&mGLMaxRenderbufferSize, kMinMaxRenderbufferSize);

    if (!ok) {
      GenerateWarning("Unable to restrict WebGL limits to minimums.");
      return false;
    }

    mDisableFragHighP = true;
  } else if (mResistFingerprinting) {
    bool ok = true;

    ok &= RestrictCap(&limits.maxTex2dSize, kCommonMaxTextureSize);
    ok &= RestrictCap(&limits.maxTexCubeSize, kCommonMaxCubeMapTextureSize);
    ok &= RestrictCap(&mGLMaxRenderbufferSize, kCommonMaxRenderbufferSize);

    ok &= RestrictCap(&mGLMaxVertexTextureImageUnits,
                      kCommonMaxVertexTextureImageUnits);
    ok &= RestrictCap(&mGLMaxFragmentTextureImageUnits,
                      kCommonMaxFragmentTextureImageUnits);
    ok &= RestrictCap(&limits.maxTexUnits, kCommonMaxCombinedTextureImageUnits);

    ok &= RestrictCap(&limits.maxVertexAttribs, kCommonMaxVertexAttribs);
    ok &= RestrictCap(&mGLMaxVertexUniformVectors,
                      kCommonMaxVertexUniformVectors);
    ok &= RestrictCap(&mGLMaxFragmentUniformVectors,
                      kCommonMaxFragmentUniformVectors);
    ok &= RestrictCap(&mGLMaxVertexOutputVectors, kCommonMaxVaryingVectors);
    ok &= RestrictCap(&mGLMaxFragmentInputVectors, kCommonMaxVaryingVectors);

    if (limits.lineWidthRange[0] <= kCommonAliasedLineWidthRangeMin) {
      limits.lineWidthRange[0] = kCommonAliasedLineWidthRangeMin;
    } else {
      ok = false;
    }
    if (limits.pointSizeRange[0] <= kCommonAliasedPointSizeRangeMin) {
      limits.pointSizeRange[0] = kCommonAliasedPointSizeRangeMin;
    } else {
      ok = false;
    }

    ok &=
        RestrictCap(&limits.lineWidthRange[1], kCommonAliasedLineWidthRangeMax);
    ok &=
        RestrictCap(&limits.pointSizeRange[1], kCommonAliasedPointSizeRangeMax);
    ok &= RestrictCap(&limits.maxViewportDim, kCommonMaxViewportDims);

    if (!ok) {
      GenerateWarning(
          "Unable to restrict WebGL limits in order to resist fingerprinting");
      return false;
    }
  }

  mLimits = Some(limits);

  ////////////////

  if (gl->IsCompatibilityProfile()) {
    gl->fEnable(LOCAL_GL_POINT_SPRITE);
  }

  if (!gl->IsGLES()) {
    gl->fEnable(LOCAL_GL_PROGRAM_POINT_SIZE);
  }

  if (gl->IsSupported(gl::GLFeature::seamless_cube_map_opt_in)) {
    gl->fEnable(LOCAL_GL_TEXTURE_CUBE_MAP_SEAMLESS);
  }

  // initialize shader translator
  if (!sh::Initialize()) {
    *out_failReason = {"FEATURE_FAILURE_WEBGL_GLSL",
                       "GLSL translator initialization failed!"};
    return false;
  }

  // Mesa can only be detected with the GL_VERSION string, of the form
  // "2.1 Mesa 7.11.0"
  const char* versionStr = (const char*)(gl->fGetString(LOCAL_GL_VERSION));
  mIsMesa = strstr(versionStr, "Mesa");

  // Notice that the point of calling fGetError here is not only to check for
  // errors, but also to reset the error flags so that a subsequent WebGL
  // getError call will give the correct result.
  error = gl->fGetError();
  if (error != LOCAL_GL_NO_ERROR) {
    const nsPrintfCString reason(
        "GL error 0x%x occurred during WebGL context"
        " initialization!",
        error);
    *out_failReason = {"FEATURE_FAILURE_WEBGL_GLERR_2", reason};
    return false;
  }

  if (IsWebGL2() && !InitWebGL2(out_failReason)) {
    // Todo: Bug 898404: Only allow WebGL2 on GL>=3.0 on desktop GL.
    return false;
  }

  // OpenGL core profiles remove the default VAO object from version
  // 4.0.0. We create a default VAO for all core profiles,
  // regardless of version.
  //
  // GL Spec 4.0.0:
  // (https://www.opengl.org/registry/doc/glspec40.core.20100311.pdf)
  // in Section E.2.2 "Removed Features", pg 397: "[...] The default
  // vertex array object (the name zero) is also deprecated. [...]"
  mDefaultVertexArray = WebGLVertexArray::Create(this);
  mDefaultVertexArray->BindVertexArray();

  mPrimRestartTypeBytes = 0;

  // -

  mGenericVertexAttribTypes.assign(limits.maxVertexAttribs,
                                   webgl::AttribBaseType::Float);
  mGenericVertexAttribTypeInvalidator.InvalidateCaches();

  static const float kDefaultGenericVertexAttribData[4] = {0, 0, 0, 1};
  memcpy(mGenericVertexAttrib0Data, kDefaultGenericVertexAttribData,
         sizeof(mGenericVertexAttrib0Data));

  mFakeVertexAttrib0BufferObject = 0;

  mNeedsLegacyVertexAttrib0Handling = gl->IsCompatibilityProfile();
  if (gl->WorkAroundDriverBugs() && kIsMacOS) {
    // Failures in conformance/attribs/gl-disabled-vertex-attrib.
    // Even in Core profiles on NV. Sigh.
    mNeedsLegacyVertexAttrib0Handling |= (gl->Vendor() == gl::GLVendor::NVIDIA);

    mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst |=
        (gl->Vendor() == gl::GLVendor::Intel);

    // Failures for programs with no attribs:
    // conformance/attribs/gl-vertex-attrib-unconsumed-out-of-bounds.html
    mMaybeNeedsLegacyVertexAttrib0Handling = true;
  }
  mMaybeNeedsLegacyVertexAttrib0Handling |= mNeedsLegacyVertexAttrib0Handling;

  if (const auto& env =
          gfxEnv::MOZ_WEBGL_WORKAROUND_FIRST_AFFECTS_INSTANCE_ID()) {
    const auto was = mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst;
    mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst =
        (env.as_str != "0");
    printf_stderr(
        "mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst: %i -> %i\n",
        int(was), int(mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst));
  }

  // -

  mNeedsIndexValidation =
      !gl->IsSupported(gl::GLFeature::robust_buffer_access_behavior);
  switch (StaticPrefs::webgl_force_index_validation()) {
    case -1:
      mNeedsIndexValidation = false;
      break;
    case 1:
      mNeedsIndexValidation = true;
      break;
    default:
      MOZ_ASSERT(StaticPrefs::webgl_force_index_validation() == 0);
      break;
  }

  for (auto& cur : mExtensions) {
    cur = {};
  }

  return true;
}

bool WebGLContext::ValidateFramebufferTarget(GLenum target) const {
  bool isValid = true;
  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      break;

    case LOCAL_GL_DRAW_FRAMEBUFFER:
    case LOCAL_GL_READ_FRAMEBUFFER:
      isValid = IsWebGL2();
      break;

    default:
      isValid = false;
      break;
  }

  if (isValid) [[likely]] {
    return true;
  }

  ErrorInvalidEnumArg("target", target);
  return false;
}

}  // namespace mozilla
