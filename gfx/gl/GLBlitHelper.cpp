/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLBlitHelper.h"

#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "GLUploadHelpers.h"
#include "GPUVideoImage.h"
#include "HeapCopyOfStackArray.h"
#include "ImageContainer.h"
#include "ScopedGLHelpers.h"
#include "gfxEnv.h"
#include "gfxUtils.h"
#include "mozilla/Casting.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/BuildConstants.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/layers/CompositeProcessFencesHolderMap.h"
#include "mozilla/layers/GpuFence.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidSurfaceTexture.h"
#  include "GLContextEGL.h"
#  include "GLImages.h"
#  include "GLLibraryEGL.h"
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#  include "mozilla/layers/AndroidImageReader.h"
#endif

#ifdef XP_MACOSX
#  include "GLContextCGL.h"
#  include "MacIOSurfaceImage.h"
#endif

#ifdef XP_WIN
#  include "mozilla/layers/D3D11ShareHandleImage.h"
#  include "mozilla/layers/D3D11YCbCrImage.h"
#  include "mozilla/layers/D3D11ZeroCopyTextureImage.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "mozilla/layers/DMABUFSurfaceImage.h"
#  include "mozilla/widget/DMABufDevice.h"
#  include "mozilla/widget/DMABufSurface.h"
#endif

using mozilla::layers::PlanarYCbCrData;
using mozilla::layers::PlanarYCbCrImage;

namespace mozilla {
namespace gl {

// --

static const char kFragPreprocHeader[] = R"(
  #ifdef GL_ES
    #ifdef GL_FRAGMENT_PRECISION_HIGH
      #define MAXP highp
    #endif
  #else
    #define MAXP highp
  #endif
  #ifndef MAXP
    #define MAXP mediump
  #endif

  #if __VERSION__ >= 130
    #define VARYING in
  #else
    #define VARYING varying
  #endif
  #if __VERSION__ >= 120
    #define MAT4X3 mat4x3
  #else
    #define MAT4X3 mat4
  #endif
)";

// -

const char* const kFragHeader_Tex2D = R"(
    #define SAMPLER sampler2D
    #if __VERSION__ >= 130
        #define TEXTURE texture
    #else
        #define TEXTURE texture2D
    #endif
)";
const char* const kFragHeader_Tex2DRect = R"(
    #define SAMPLER sampler2DRect
    #if __VERSION__ >= 130
        #define TEXTURE texture
    #else
        #define TEXTURE texture2DRect
    #endif
)";
const char* const kFragHeader_TexExt = R"(
    #extension GL_OES_EGL_image_external : enable
    #extension GL_OES_EGL_image_external_essl3 : enable
    #if __VERSION__ >= 130
        #define TEXTURE texture
    #else
        #define TEXTURE texture2D
    #endif
    #define SAMPLER samplerExternalOES
)";

// -

static const char kFragDeclHeader[] = R"(
  precision PRECISION float;
  #if __VERSION__ >= 130
    #define FRAG_COLOR oFragColor
    out vec4 FRAG_COLOR;
  #else
    #define FRAG_COLOR gl_FragColor
  #endif
)";

// -

const char* const kFragSample_OnePlane = R"(
  VARYING mediump vec2 vTexCoord0;
  uniform PRECISION SAMPLER uTex0;

  vec4 metaSample() {
    vec4 src = TEXTURE(uTex0, vTexCoord0);
    return src;
  }
)";
// Ideally this would just change the color-matrix it uses, but this is
// acceptable debt for now.
// `extern` so that we don't get ifdef-dependent const-var-unused Werrors.
extern const char* const kFragSample_OnePlane_YUV_via_GBR = R"(
  VARYING mediump vec2 vTexCoord0;
  uniform PRECISION SAMPLER uTex0;

  vec4 metaSample() {
    vec4 yuva = TEXTURE(uTex0, vTexCoord0).gbra;
    return yuva;
  }
)";
const char* const kFragSample_TwoPlane = R"(
  VARYING mediump vec2 vTexCoord0;
  VARYING mediump vec2 vTexCoord1;
  uniform PRECISION SAMPLER uTex0;
  uniform PRECISION SAMPLER uTex1;

  vec4 metaSample() {
    vec4 src = TEXTURE(uTex0, vTexCoord0); // Keep r and a.
    src.gb = TEXTURE(uTex1, vTexCoord1).rg;
    return src;
  }
)";
const char* const kFragSample_ThreePlane = R"(
  VARYING mediump vec2 vTexCoord0;
  VARYING mediump vec2 vTexCoord1;
  uniform PRECISION SAMPLER uTex0;
  uniform PRECISION SAMPLER uTex1;
  uniform PRECISION SAMPLER uTex2;

  vec4 metaSample() {
    vec4 src = TEXTURE(uTex0, vTexCoord0); // Keep r and a.
    src.g = TEXTURE(uTex1, vTexCoord1).r;
    src.b = TEXTURE(uTex2, vTexCoord1).r;
    return src;
  }
)";

extern const char* const kFragSample_TwoPlaneUV = R"(
  VARYING mediump vec2 vTexCoord0;
  uniform PRECISION SAMPLER uTex1;
  uniform PRECISION SAMPLER uTex2;

  vec4 metaSample() {
    vec4 src = TEXTURE(uTex1, vTexCoord0);
    src.g = TEXTURE(uTex2, vTexCoord0).r;
    return src;
  }
)";

// -

const char* const kFragConvert_None = R"(
  vec3 metaConvert(vec3 src) {
    return src;
  }
)";
const char* const kFragConvert_BGR = R"(
  vec3 metaConvert(vec3 src) {
    return src.bgr;
  }
)";
const char* const kFragConvert_ColorMatrix = R"(
  uniform mediump MAT4X3 uColorMatrix;

  vec3 metaConvert(vec3 src) {
    return (uColorMatrix * vec4(src, 1)).rgb;
  }
)";
#ifdef MOZ_WIDGET_GTK
const char* const kFragConvert_ColorMatrixBGR = R"(
  uniform mediump MAT4X3 uColorMatrix;

  vec3 metaConvert(vec3 src) {
    return (uColorMatrix * vec4(src, 1)).bgr;
  }
)";
#endif
const char* const kFragConvert_ColorLut3d = R"(
  uniform PRECISION sampler3D uColorLut;

  vec3 metaConvert(vec3 src) {
    // Half-texel filtering hazard!
    // E.g. For texture size of 2,
    // E.g. x=0.25 is still sampling 100% of texel x=0, 0% of texel x=1.
    // For the LUT, we need r=0.25 to filter 75/25 from texel 0 and 1.
    // That is, we need to adjust our sampling point such that it starts in the
    // center of texel 0, and ends in the center of texel N-1.
    // We need, for N=2:
    // v=0.0|N=2 => v'=0.5/2
    // v=1.0|N=2 => v'=1.5/2
    // For N=3:
    // v=0.0|N=3 => v'=0.5/3
    // v=1.0|N=3 => v'=2.5/3
    // => v' = ( 0.5 + v * (3 - 1) )/3
    vec3 size = vec3(textureSize(uColorLut, 0));
    src = (0.5 + src * (size - 1.0)) / size;
    return texture(uColorLut, src).rgb;
  }
)";
// Delete if unused after 2024-10-01:
const char* const kFragConvert_ColorLut2d = R"(
  uniform PRECISION sampler2D uColorLut;
  uniform mediump vec3 uColorLut3dSize;

  vec3 metaConvert(vec3 src) {
    // Half-texel filtering hazard!
    // E.g. For texture size of 2,
    // E.g. x=0.25 is still sampling 100% of texel x=0, 0% of texel x=1.
    // For the LUT, we need r=0.25 to filter 75/25 from texel 0 and 1.
    // That is, we need to adjust our sampling point such that it starts in the
    // center of texel 0, and ends in the center of texel N-1.
    // We need, for N=2:
    // v=0.0|N=2 => v'=0.5/2
    // v=1.0|N=2 => v'=1.5/2
    // For N=3:
    // v=0.0|N=3 => v'=0.5/3
    // v=1.0|N=3 => v'=2.5/3
    // => v' = ( 0.5 + v * (3 - 1) )/3
    src = clamp(src, vec3(0,0,0), vec3(1,1,1));
    vec3 lut3dSize = uColorLut3dSize;
    vec2 lut2dSize = vec2(lut3dSize.x, lut3dSize.y * lut3dSize.z);
    vec3 texelSrc3d = 0.5 + src * (lut3dSize - 1.0);

    vec3 texelSrc3d_zFloor = texelSrc3d;
    texelSrc3d_zFloor.z = floor(texelSrc3d_zFloor.z);
    vec3 texelSrc3d_zNext = texelSrc3d_zFloor + vec3(0,0,1);
    texelSrc3d_zNext.z = min(texelSrc3d_zNext.z, lut3dSize.z - 1.0);

    vec2 texelSrc2d_zFloor = texelSrc3d_zFloor.xy + vec2(0, texelSrc3d_zFloor.z * lut3dSize.y);
    vec2 texelSrc2d_zNext  = texelSrc3d_zNext.xy  + vec2(0, texelSrc3d_zNext.z  * lut3dSize.y);

    vec4 dst_zFloor = texture(uColorLut, texelSrc2d_zFloor / lut2dSize);
    vec4 dst_zNext = texture(uColorLut, texelSrc2d_zNext / lut2dSize);

    return mix(dst_zFloor, dst_zNext, texelSrc3d.z - texelSrc3d_zFloor.z);
  }
)";

extern const char* const kFragConvertYUVP010 = R"(
  vec3 metaConvert(vec3 src) {
    // YUV420P10 and P010 are both 10-bit formats stored in 16-bit integer.
    // P010 has 6 lower bits 0 (value is shifted to upper bits)
    // while YUV420P10 has upper 6 bites zeroed.
    src *= 64.0;
    return src;
  }
)";

// -

const char* const kFragMixin_AlphaMultColors = R"(
  #define MIXIN_ALPHA_MULT_COLORS
)";
const char* const kFragMixin_AlphaUnpremultColors = R"(
  #define MIXIN_ALPHA_UNPREMULT_COLORS
)";
const char* const kFragMixin_AlphaClampColors = R"(
  #define MIXIN_ALPHA_CLAMP_COLORS
)";
const char* const kFragMixin_AlphaOne = R"(
  #define MIXIN_ALPHA_ONE
)";

// -

static const char kFragBody[] = R"(
  void main(void) {
    vec4 src = metaSample();
    vec4 dst = vec4(metaConvert(src.rgb), src.a);

  #ifdef MIXIN_ALPHA_MULT_COLORS
    dst.rgb *= dst.a;
  #endif
  #ifdef MIXIN_ALPHA_UNPREMULT_COLORS
    dst.rgb = dst.a != 0.0 ? dst.rgb / dst.a : dst.rgb;
  #endif
  #ifdef MIXIN_ALPHA_CLAMP_COLORS
    dst.rgb = min(dst.rgb, vec3(dst.a)); // Ensure valid premult-alpha colors.
  #endif
  #ifdef MIXIN_ALPHA_ONE
    dst.a = 1.0;
  #endif

    FRAG_COLOR = dst;
  }
)";

// --

Mat3 SubRectMat3(const float x, const float y, const float w, const float h) {
  auto ret = Mat3{};
  ret.at(0, 0) = w;
  ret.at(1, 1) = h;
  ret.at(2, 0) = x;
  ret.at(2, 1) = y;
  ret.at(2, 2) = 1.0f;
  return ret;
}

Mat3 SubRectMat3(const gfx::IntRect& subrect, const gfx::IntSize& size) {
  return SubRectMat3(float(subrect.X()) / size.width,
                     float(subrect.Y()) / size.height,
                     float(subrect.Width()) / size.width,
                     float(subrect.Height()) / size.height);
}

Mat3 SubRectMat3(const gfx::IntRect& bigSubrect, const gfx::IntSize& smallSize,
                 const gfx::IntSize& divisors) {
  const float x = float(bigSubrect.X()) / divisors.width;
  const float y = float(bigSubrect.Y()) / divisors.height;
  const float w = float(bigSubrect.Width()) / divisors.width;
  const float h = float(bigSubrect.Height()) / divisors.height;
  return SubRectMat3(x / smallSize.width, y / smallSize.height,
                     w / smallSize.width, h / smallSize.height);
}

Mat3 MatrixToMat3(const gfx::Matrix& aMatrix) {
  auto ret = Mat3();
  ret.at(0, 0) = aMatrix._11;
  ret.at(1, 0) = aMatrix._21;
  ret.at(2, 0) = aMatrix._31;
  ret.at(0, 1) = aMatrix._12;
  ret.at(1, 1) = aMatrix._22;
  ret.at(2, 1) = aMatrix._32;
  ret.at(0, 2) = 0.0f;
  ret.at(1, 2) = 0.0f;
  ret.at(2, 2) = 1.0f;
  return ret;
}

// --

ScopedSaveMultiTex::ScopedSaveMultiTex(GLContext* const gl,
                                       const size_t texUnits,
                                       const GLenum texTarget)
    : mGL(*gl),
      mTexUnits(texUnits),
      mTexTarget(texTarget),
      mOldTexUnit(mGL.GetIntAs<GLenum>(LOCAL_GL_ACTIVE_TEXTURE)) {
  MOZ_RELEASE_ASSERT(texUnits >= 1 && texUnits <= std::size(mOldTex));

  GLenum texBinding;
  switch (mTexTarget) {
    case LOCAL_GL_TEXTURE_2D:
      texBinding = LOCAL_GL_TEXTURE_BINDING_2D;
      break;
    case LOCAL_GL_TEXTURE_3D:
      texBinding = LOCAL_GL_TEXTURE_BINDING_3D;
      break;
    case LOCAL_GL_TEXTURE_RECTANGLE:
      texBinding = LOCAL_GL_TEXTURE_BINDING_RECTANGLE;
      break;
    case LOCAL_GL_TEXTURE_EXTERNAL:
      texBinding = LOCAL_GL_TEXTURE_BINDING_EXTERNAL;
      break;
    default:
      gfxCriticalError() << "Unhandled texTarget: " << texTarget;
      MOZ_CRASH();
  }

  for (const auto i : IntegerRange(mTexUnits)) {
    mGL.fActiveTexture(LOCAL_GL_TEXTURE0 + i);
    if (mGL.IsSupported(GLFeature::sampler_objects)) {
      mOldTexSampler[i] = mGL.GetIntAs<GLuint>(LOCAL_GL_SAMPLER_BINDING);
      mGL.fBindSampler(i, 0);
    }
    mOldTex[i] = mGL.GetIntAs<GLuint>(texBinding);
  }
}

ScopedSaveMultiTex::~ScopedSaveMultiTex() {
  // Unbind in reverse order, in case we have repeats.
  // Order matters because we unbound samplers during ctor, so now we have to
  // make sure we rebind them in the right order.
  for (const auto i : Reversed(IntegerRange(mTexUnits))) {
    mGL.fActiveTexture(LOCAL_GL_TEXTURE0 + i);
    if (mGL.IsSupported(GLFeature::sampler_objects)) {
      mGL.fBindSampler(i, mOldTexSampler[i]);
    }
    mGL.fBindTexture(mTexTarget, mOldTex[i]);
  }
  mGL.fActiveTexture(mOldTexUnit);
}

// --

class ScopedBindArrayBuffer final {
 public:
  GLContext& mGL;
  const GLuint mOldVBO;

  ScopedBindArrayBuffer(GLContext* const gl, const GLuint vbo)
      : mGL(*gl), mOldVBO(mGL.GetIntAs<GLuint>(LOCAL_GL_ARRAY_BUFFER_BINDING)) {
    mGL.fBindBuffer(LOCAL_GL_ARRAY_BUFFER, vbo);
  }

  ~ScopedBindArrayBuffer() { mGL.fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mOldVBO); }
};

// --

class ScopedShader final {
  GLContext& mGL;
  const GLuint mName;

 public:
  ScopedShader(GLContext* const gl, const GLenum shaderType)
      : mGL(*gl), mName(mGL.fCreateShader(shaderType)) {}

  ~ScopedShader() { mGL.fDeleteShader(mName); }

  operator GLuint() const { return mName; }
};

// --

class ScopedDrawBlitState final {
  GLContext& mGL;

  const bool blend;
  const bool cullFace;
  const bool depthTest;
  const bool dither;
  const bool polyOffsFill;
  const bool sampleAToC;
  const bool sampleCover;
  const bool scissor;
  const bool stencil;
  Maybe<bool> rasterizerDiscard;

  realGLboolean colorMask[4];
  GLint viewport[4];
  GLint clipOrigin = LOCAL_GL_LOWER_LEFT;
  GLint clipDepthMode = LOCAL_GL_NEGATIVE_ONE_TO_ONE;

 public:
  ScopedDrawBlitState(GLContext* const gl, const gfx::IntSize& fbSize)
      : mGL(*gl),
        blend(mGL.PushEnabled(LOCAL_GL_BLEND, false)),
        cullFace(mGL.PushEnabled(LOCAL_GL_CULL_FACE, false)),
        depthTest(mGL.PushEnabled(LOCAL_GL_DEPTH_TEST, false)),
        dither(mGL.PushEnabled(LOCAL_GL_DITHER, true)),
        polyOffsFill(mGL.PushEnabled(LOCAL_GL_POLYGON_OFFSET_FILL, false)),
        sampleAToC(mGL.PushEnabled(LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE, false)),
        sampleCover(mGL.PushEnabled(LOCAL_GL_SAMPLE_COVERAGE, false)),
        scissor(mGL.PushEnabled(LOCAL_GL_SCISSOR_TEST, false)),
        stencil(mGL.PushEnabled(LOCAL_GL_STENCIL_TEST, false)) {
    if (mGL.IsSupported(GLFeature::clip_control)) {
      mGL.fGetIntegerv(LOCAL_GL_CLIP_ORIGIN, &clipOrigin);
      mGL.fGetIntegerv(LOCAL_GL_CLIP_DEPTH_MODE, &clipDepthMode);
      mGL.fClipControl(LOCAL_GL_LOWER_LEFT, LOCAL_GL_NEGATIVE_ONE_TO_ONE);
    }

    if (mGL.IsSupported(GLFeature::transform_feedback2)) {
      // Technically transform_feedback2 requires transform_feedback, which
      // actually adds RASTERIZER_DISCARD.
      rasterizerDiscard =
          Some(mGL.PushEnabled(LOCAL_GL_RASTERIZER_DISCARD, false));
    }

    mGL.fGetBooleanv(LOCAL_GL_COLOR_WRITEMASK, colorMask);
    if (mGL.IsSupported(GLFeature::draw_buffers_indexed)) {
      mGL.fColorMaski(0, true, true, true, true);
    } else {
      mGL.fColorMask(true, true, true, true);
    }

    mGL.fGetIntegerv(LOCAL_GL_VIEWPORT, viewport);
    MOZ_ASSERT(fbSize.width && fbSize.height);
    mGL.fViewport(0, 0, fbSize.width, fbSize.height);
  }

  ~ScopedDrawBlitState() {
    mGL.SetEnabled(LOCAL_GL_BLEND, blend);
    mGL.SetEnabled(LOCAL_GL_CULL_FACE, cullFace);
    mGL.SetEnabled(LOCAL_GL_DEPTH_TEST, depthTest);
    mGL.SetEnabled(LOCAL_GL_DITHER, dither);
    mGL.SetEnabled(LOCAL_GL_POLYGON_OFFSET_FILL, polyOffsFill);
    mGL.SetEnabled(LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE, sampleAToC);
    mGL.SetEnabled(LOCAL_GL_SAMPLE_COVERAGE, sampleCover);
    mGL.SetEnabled(LOCAL_GL_SCISSOR_TEST, scissor);
    mGL.SetEnabled(LOCAL_GL_STENCIL_TEST, stencil);
    if (mGL.IsSupported(GLFeature::clip_control)) {
      mGL.fClipControl(clipOrigin, clipDepthMode);
    }
    if (rasterizerDiscard) {
      mGL.SetEnabled(LOCAL_GL_RASTERIZER_DISCARD, rasterizerDiscard.value());
    }

    if (mGL.IsSupported(GLFeature::draw_buffers_indexed)) {
      mGL.fColorMaski(0, colorMask[0], colorMask[1], colorMask[2],
                      colorMask[3]);
    } else {
      mGL.fColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    }
    mGL.fViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  }
};

// --

DrawBlitProg::DrawBlitProg(const GLBlitHelper* const parent, const GLuint prog)
    : mParent(*parent),
      mProg(prog),
      mLoc_uDestMatrix(mParent.mGL->fGetUniformLocation(mProg, "uDestMatrix")),
      mLoc_uTexMatrix0(mParent.mGL->fGetUniformLocation(mProg, "uTexMatrix0")),
      mLoc_uTexMatrix1(mParent.mGL->fGetUniformLocation(mProg, "uTexMatrix1")),
      mLoc_uColorLut(mParent.mGL->fGetUniformLocation(mProg, "uColorLut")),
      mLoc_uColorMatrix(
          mParent.mGL->fGetUniformLocation(mProg, "uColorMatrix")) {
  const auto& gl = mParent.mGL;
  MOZ_GL_ASSERT(gl, mLoc_uDestMatrix != -1);  // Required
  MOZ_GL_ASSERT(gl, mLoc_uTexMatrix0 != -1);  // Required
  if (mLoc_uColorMatrix != -1) {
    MOZ_GL_ASSERT(gl, mLoc_uTexMatrix1 != -1);

    int32_t numActiveUniforms = 0;
    gl->fGetProgramiv(mProg, LOCAL_GL_ACTIVE_UNIFORMS, &numActiveUniforms);

    const size_t kMaxNameSize = 32;
    char name[kMaxNameSize] = {0};
    GLint size = 0;
    GLenum type = 0;
    for (int32_t i = 0; i < numActiveUniforms; i++) {
      gl->fGetActiveUniform(mProg, i, kMaxNameSize, nullptr, &size, &type,
                            name);
      if (strcmp("uColorMatrix", name) == 0) {
        mType_uColorMatrix = type;
        break;
      }
    }
    MOZ_GL_ASSERT(gl, mType_uColorMatrix);
  }
}

DrawBlitProg::~DrawBlitProg() {
  const auto& gl = mParent.mGL;
  if (!gl->MakeCurrent()) return;

  gl->fDeleteProgram(mProg);
}

void DrawBlitProg::Draw(const BaseArgs& args,
                        const YUVArgs* const argsYUV) const {
  const auto& gl = mParent.mGL;

  const ScopedBindProgram oldProg(gl);
  gl->fUseProgram(mProg);

  // --

  gfx::IntSize fbSize = args.fbSize;
  Mat3 destMatrix;
  if (!args.destRect.IsEmpty()) {
    const gfx::IntRect& destRect = args.destRect;
    if (fbSize.IsEmpty()) {
      fbSize = destRect.Size();
    }
    destMatrix = SubRectMat3(float(destRect.X()) / fbSize.width,
                             float(destRect.Y()) / fbSize.height,
                             float(destRect.Width()) / fbSize.width,
                             float(destRect.Height()) / fbSize.height);
  } else {
    MOZ_ASSERT(!fbSize.IsEmpty());
    destMatrix = Mat3::I();
  }

  if (args.yFlip) {
    // Apply the y-flip matrix before the destMatrix.
    // That is, flip y=[0-1] to y=[1-0] before we restrict to the destRect.
    destMatrix.at(2, 1) += destMatrix.at(1, 1);
    destMatrix.at(1, 1) *= -1.0f;
  }

  Mat3 texScale = Mat3::I();
  if (!args.texSize.IsEmpty()) {
    const gfx::IntSize clipSize =
        args.destRect.IsEmpty() ? fbSize : args.destRect.Size();
    const gfx::IntSize& texSize = args.texSize;
    texScale = SubRectMat3(
        gfx::IntRect(
            gfx::IntPoint(0, args.yFlip ? texSize.height - clipSize.height : 0),
            clipSize),
        texSize);
  }

  gl->fUniformMatrix3fv(mLoc_uDestMatrix, 1, false, destMatrix.m);

  Mat3 texMatrix0 = args.texMatrix0 * texScale;
  gl->fUniformMatrix3fv(mLoc_uTexMatrix0, 1, false, texMatrix0.m);

  MOZ_ASSERT(bool(argsYUV) == (mLoc_uColorMatrix != -1));
  if (argsYUV) {
    Mat3 texMatrix1 = argsYUV->texMatrix1 * texScale;
    gl->fUniformMatrix3fv(mLoc_uTexMatrix1, 1, false, texMatrix1.m);

    if (mLoc_uColorMatrix != -1) {
      const auto& colorMatrix =
          gfxUtils::YuvToRgbMatrix4x4ColumnMajor(*argsYUV->colorSpaceForMatrix);
      float mat4x3[4 * 3];
      switch (mType_uColorMatrix) {
        case LOCAL_GL_FLOAT_MAT4:
          gl->fUniformMatrix4fv(mLoc_uColorMatrix, 1, false, colorMatrix);
          break;
        case LOCAL_GL_FLOAT_MAT4x3:
          for (int x = 0; x < 4; x++) {
            for (int y = 0; y < 3; y++) {
              mat4x3[3 * x + y] = colorMatrix[4 * x + y];
            }
          }
          gl->fUniformMatrix4x3fv(mLoc_uColorMatrix, 1, false, mat4x3);
          break;
        default:
          gfxCriticalError()
              << "Bad mType_uColorMatrix: " << gfx::hexa(mType_uColorMatrix);
      }
    }
  }

  // --

  const ScopedDrawBlitState drawState(gl, fbSize);

  GLuint oldVAO;
  GLint vaa0Enabled;
  GLint vaa0Size;
  GLenum vaa0Type;
  GLint vaa0Normalized;
  GLsizei vaa0Stride;
  GLvoid* vaa0Pointer;
  GLuint vaa0Buffer;
  if (mParent.mQuadVAO) {
    oldVAO = gl->GetIntAs<GLuint>(LOCAL_GL_VERTEX_ARRAY_BINDING);
    gl->fBindVertexArray(mParent.mQuadVAO);
  } else {
    // clang-format off
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, (GLint*)&vaa0Buffer);
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vaa0Enabled);
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_SIZE, &vaa0Size);
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_TYPE, (GLint*)&vaa0Type);
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &vaa0Normalized);
    gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_STRIDE, (GLint*)&vaa0Stride);
    gl->fGetVertexAttribPointerv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER, &vaa0Pointer);
    // clang-format on

    gl->fEnableVertexAttribArray(0);
    const ScopedBindArrayBuffer bindVBO(gl, mParent.mQuadVBO);
    gl->fVertexAttribPointer(0, 2, LOCAL_GL_FLOAT, false, 0, nullptr);
  }

  gl->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 4);

  if (mParent.mQuadVAO) {
    gl->fBindVertexArray(oldVAO);
  } else {
    if (vaa0Enabled) {
      gl->fEnableVertexAttribArray(0);
    } else {
      gl->fDisableVertexAttribArray(0);
    }
    // The current VERTEX_ARRAY_BINDING is not necessarily the same as the
    // buffer set for vaa0Buffer.
    const ScopedBindArrayBuffer bindVBO(gl, vaa0Buffer);
    gl->fVertexAttribPointer(0, vaa0Size, vaa0Type, bool(vaa0Normalized),
                             vaa0Stride, vaa0Pointer);
  }
}

// --

GLBlitHelper::GLBlitHelper(GLContext* const gl)
    : mGL(gl),
      mDrawBlitProg_VertShader(mGL->fCreateShader(LOCAL_GL_VERTEX_SHADER))
//, mYuvUploads_YSize(0, 0)
//, mYuvUploads_UVSize(0, 0)
{
  mGL->fGenBuffers(1, &mQuadVBO);
  {
    const ScopedBindArrayBuffer bindVBO(mGL, mQuadVBO);

    const float quadData[] = {0, 0, 1, 0, 0, 1, 1, 1};
    const HeapCopyOfStackArray<float> heapQuadData(quadData);
    mGL->fBufferData(LOCAL_GL_ARRAY_BUFFER, heapQuadData.ByteLength(),
                     heapQuadData.Data(), LOCAL_GL_STATIC_DRAW);

    if (mGL->IsSupported(GLFeature::vertex_array_object)) {
      const auto prev = mGL->GetIntAs<GLuint>(LOCAL_GL_VERTEX_ARRAY_BINDING);

      mGL->fGenVertexArrays(1, &mQuadVAO);
      mGL->fBindVertexArray(mQuadVAO);
      mGL->fEnableVertexAttribArray(0);
      mGL->fVertexAttribPointer(0, 2, LOCAL_GL_FLOAT, false, 0, nullptr);

      mGL->fBindVertexArray(prev);
    }
  }

  // --

  const auto glslVersion = mGL->ShadingLanguageVersion();

  if (mGL->IsGLES()) {
    // If you run into problems on old android devices, it might be because some
    // devices have OES_EGL_image_external but not OES_EGL_image_external_essl3.
    // We could just use 100 in that particular case, but then we lose out on
    // e.g. sampler3D. Let's just try 300 for now, and if we get regressions
    // we'll add an essl100 fallback.
    if (glslVersion >= 300) {
      mDrawBlitProg_VersionLine = nsCString("#version 300 es\n");
    } else {
      mDrawBlitProg_VersionLine = nsCString("#version 100\n");
    }
  } else if (glslVersion >= 130) {
    mDrawBlitProg_VersionLine = nsPrintfCString("#version %u\n", glslVersion);
  }

  const char kVertSource[] =
      "\
        #if __VERSION__ >= 130                                               \n\
            #define ATTRIBUTE in                                             \n\
            #define VARYING out                                              \n\
        #else                                                                \n\
            #define ATTRIBUTE attribute                                      \n\
            #define VARYING varying                                          \n\
        #endif                                                               \n\
                                                                             \n\
        ATTRIBUTE vec2 aVert; // [0.0-1.0]                                   \n\
                                                                             \n\
        uniform mat3 uDestMatrix;                                            \n\
        uniform mat3 uTexMatrix0;                                            \n\
        uniform mat3 uTexMatrix1;                                            \n\
                                                                             \n\
        VARYING vec2 vTexCoord0;                                             \n\
        VARYING vec2 vTexCoord1;                                             \n\
                                                                             \n\
        void main(void)                                                      \n\
        {                                                                    \n\
            vec2 destPos = (uDestMatrix * vec3(aVert, 1.0)).xy;              \n\
            gl_Position = vec4(destPos * 2.0 - 1.0, 0.0, 1.0);               \n\
                                                                             \n\
            vTexCoord0 = (uTexMatrix0 * vec3(aVert, 1.0)).xy;                \n\
            vTexCoord1 = (uTexMatrix1 * vec3(aVert, 1.0)).xy;                \n\
        }                                                                    \n\
    ";
  const char* const parts[] = {mDrawBlitProg_VersionLine.get(), kVertSource};
  mGL->fShaderSource(mDrawBlitProg_VertShader, std::size(parts), parts,
                     nullptr);
  mGL->fCompileShader(mDrawBlitProg_VertShader);
}

GLBlitHelper::~GLBlitHelper() {
  mDrawBlitProgs.clear();

  if (!mGL->MakeCurrent()) return;

  mGL->fDeleteShader(mDrawBlitProg_VertShader);
  mGL->fDeleteBuffers(1, &mQuadVBO);

  if (mQuadVAO) {
    mGL->fDeleteVertexArrays(1, &mQuadVAO);
  }
}

// --

const DrawBlitProg& GLBlitHelper::GetDrawBlitProg(
    const DrawBlitProg::Key& key) const {
  auto& ret = mDrawBlitProgs[key];
  if (!ret) {
    ret = CreateDrawBlitProg(key);
  }
  return *ret;
}

std::unique_ptr<const DrawBlitProg> GLBlitHelper::CreateDrawBlitProg(
    const DrawBlitProg::Key& key) const {
  const auto precisionPref = StaticPrefs::gfx_blithelper_precision();
  const char* precision;
  switch (precisionPref) {
    case 0:
      precision = "lowp";
      break;
    case 1:
      precision = "mediump";
      break;
    default:
      if (precisionPref != 2) {
        NS_WARNING("gfx.blithelper.precision clamped to 2.");
      }
      precision = "MAXP";
      break;
  }

  nsPrintfCString precisionLine("\n#define PRECISION %s\n", precision);

  // -

  const ScopedShader fs(mGL, LOCAL_GL_FRAGMENT_SHADER);

  std::vector<const char*> parts;
  {
    parts.push_back(mDrawBlitProg_VersionLine.get());
    parts.push_back(kFragPreprocHeader);
    if (key.fragHeader) {
      parts.push_back(key.fragHeader);
    }
    parts.push_back(precisionLine.get());
    parts.push_back(kFragDeclHeader);
    for (const auto& part : key.fragParts) {
      if (part) {
        parts.push_back(part);
      }
    }
    parts.push_back(kFragBody);
  }

  const auto PrintFragSource = [&]() {
    printf_stderr("Frag source:\n");
    int i = 0;
    for (const auto& part : parts) {
      printf_stderr("// parts[%i]:\n%s\n", i, part);
      i += 1;
    }
  };
  if (gfxEnv::MOZ_DUMP_GLBLITHELPER()) {
    PrintFragSource();
  }

  mGL->fShaderSource(fs, AssertedCast<GLint>(parts.size()), parts.data(),
                     nullptr);
  mGL->fCompileShader(fs);

  const auto prog = mGL->fCreateProgram();
  mGL->fAttachShader(prog, mDrawBlitProg_VertShader);
  mGL->fAttachShader(prog, fs);

  mGL->fBindAttribLocation(prog, 0, "aVert");
  mGL->fLinkProgram(prog);

  GLenum status = 0;
  mGL->fGetProgramiv(prog, LOCAL_GL_LINK_STATUS, (GLint*)&status);
  if (status == LOCAL_GL_TRUE || mGL->CheckContextLost()) {
    const ScopedBindProgram oldProg(mGL);
    mGL->fUseProgram(prog);
    const char* samplerNames[] = {"uTex0", "uTex1", "uTex2"};
    for (int i = 0; i < 3; i++) {
      const auto loc = mGL->fGetUniformLocation(prog, samplerNames[i]);
      if (loc == -1) continue;
      mGL->fUniform1i(loc, i);
    }

    return std::make_unique<DrawBlitProg>(this, prog);
  }

  GLuint progLogLen = 0;
  mGL->fGetProgramiv(prog, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&progLogLen);
  const UniquePtr<char[]> progLog(new char[progLogLen + 1]);
  mGL->fGetProgramInfoLog(prog, progLogLen, nullptr, progLog.get());
  progLog[progLogLen] = 0;

  const auto& vs = mDrawBlitProg_VertShader;
  GLuint vsLogLen = 0;
  mGL->fGetShaderiv(vs, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&vsLogLen);
  const UniquePtr<char[]> vsLog(new char[vsLogLen + 1]);
  mGL->fGetShaderInfoLog(vs, vsLogLen, nullptr, vsLog.get());
  vsLog[vsLogLen] = 0;

  GLuint fsLogLen = 0;
  mGL->fGetShaderiv(fs, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&fsLogLen);
  const UniquePtr<char[]> fsLog(new char[fsLogLen + 1]);
  mGL->fGetShaderInfoLog(fs, fsLogLen, nullptr, fsLog.get());
  fsLog[fsLogLen] = 0;

  const auto logs =
      std::string("DrawBlitProg link failed:\n") + "progLog: " + progLog.get() +
      "\n" + "vsLog: " + vsLog.get() + "\n" + "fsLog: " + fsLog.get() + "\n";
  gfxCriticalError() << logs;

  PrintFragSource();

  MOZ_CRASH("DrawBlitProg link failed");
}

// -----------------------------------------------------------------------------

#ifdef XP_MACOSX
static RefPtr<MacIOSurface> LookupSurface(
    const layers::SurfaceDescriptorMacIOSurface& sd) {
  MacIOSurface::AllowAlpha allowAlpha = sd.isOpaque()
                                            ? MacIOSurface::AllowAlpha::No
                                            : MacIOSurface::AllowAlpha::Yes;
  return MacIOSurface::LookupSurface(sd.surfaceId(), sd.yUVColorSpace(),
                                     gfx::TransferFunction::SRGB, allowAlpha);
}
#endif

bool GLBlitHelper::BlitSdToFramebuffer(const layers::SurfaceDescriptor& asd,
                                       const gfx::IntRect& destRect,
                                       const OriginPos destOrigin,
                                       const gfx::IntSize& fbSize,
                                       Maybe<gfxAlphaType> convertAlpha,
                                       gfx::SurfaceFormat aDestFormat) {
  const auto sdType = asd.type();
  switch (sdType) {
    case layers::SurfaceDescriptor::TSurfaceDescriptorBuffer: {
      const auto& sd = asd.get_SurfaceDescriptorBuffer();
      const auto yuvData = PlanarYCbCrData::From(sd);
      if (!yuvData) {
        gfxCriticalNote << "[GLBlitHelper::BlitSdToFramebuffer] "
                           "PlanarYCbCrData::From failed";
        return false;
      }
      return BlitPlanarYCbCr(*yuvData, destRect, destOrigin, fbSize,
                             convertAlpha);
    }
#ifdef XP_WIN
    case layers::SurfaceDescriptor::TSurfaceDescriptorD3D10: {
      const auto& sd = asd.get_SurfaceDescriptorD3D10();
      return BlitDescriptor(sd, destRect, destOrigin, fbSize, convertAlpha);
    }
    case layers::SurfaceDescriptor::TSurfaceDescriptorDXGIYCbCr: {
      const auto& sd = asd.get_SurfaceDescriptorDXGIYCbCr();
      return BlitDescriptor(sd, destRect, destOrigin, fbSize, convertAlpha);
    }
#endif
#ifdef XP_MACOSX
    case layers::SurfaceDescriptor::TSurfaceDescriptorMacIOSurface: {
      const auto& sd = asd.get_SurfaceDescriptorMacIOSurface();
      if (sd.fencesHolderId().isSome()) {
        auto* fencesHolderMap = layers::CompositeProcessFencesHolderMap::Get();
        RefPtr<layers::Fence> fence =
            fencesHolderMap->GetWriteFence(sd.fencesHolderId().ref());
        RefPtr<layers::GpuFence> gpuFence =
            fence ? fence->AsGpuFence() : nullptr;
        if (gpuFence && gpuFence->ServerWait(mGL, TimeDuration::Forever())) {
          return false;
        }
      }
      const auto surf = LookupSurface(sd);
      if (!surf) {
        NS_WARNING("LookupSurface(MacIOSurface) failed");
        // Sometimes that frame for our handle gone already. That's life, for
        // now.
        return false;
      }
      return BlitImage(surf, destRect, destOrigin, fbSize, convertAlpha);
    }
#endif
#ifdef MOZ_WIDGET_ANDROID
    case layers::SurfaceDescriptor::TSurfaceDescriptorAndroidHardwareBuffer: {
      const auto& sd = asd.get_SurfaceDescriptorAndroidHardwareBuffer();

      auto* manager = layers::AndroidHardwareBufferManager::Get();
      if (!manager) {
        return false;
      }

      RefPtr<layers::AndroidHardwareBuffer> buffer =
          manager->GetBuffer(sd.bufferId());
      if (!buffer) {
        return false;
      }

      return Blit(buffer, destRect, destOrigin, fbSize, convertAlpha);
    }
    case layers::SurfaceDescriptor::TAndroidImageReaderImageDescriptor: {
      const auto& sd = asd.get_AndroidImageReaderImageDescriptor();

      auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
      if (!imageReaderMap) {
        return false;
      }

      RefPtr<layers::AndroidImageReader> imageReader =
          imageReaderMap->GetImageReader(sd.imageReaderId());
      if (!imageReader) {
        return false;
      }

      return Blit(imageReader, sd.frameId(), sd.size(), destRect, destOrigin,
                  fbSize, convertAlpha);
    }
    case layers::SurfaceDescriptor::TSurfaceTextureDescriptor: {
      const auto& sd = asd.get_SurfaceTextureDescriptor();
      auto surfaceTexture = java::GeckoSurfaceTexture::Lookup(sd.handle());
      return Blit(surfaceTexture, sd.size(), destRect, destOrigin, fbSize,
                  convertAlpha);
    }
    case layers::SurfaceDescriptor::TEGLImageDescriptor: {
      const auto& sd = asd.get_EGLImageDescriptor();
      return Blit((EGLImage)sd.image(), (EGLSync)sd.fence(), sd.size(),
                  destRect, destOrigin, fbSize, convertAlpha);
    }
#endif
#ifdef MOZ_WIDGET_GTK
    case layers::SurfaceDescriptor::TSurfaceDescriptorDMABuf: {
      const auto& sd = asd.get_SurfaceDescriptorDMABuf();
      RefPtr<DMABufSurface> surface = DMABufSurface::CreateDMABufSurface(sd);
      if (!surface) {
        return false;
      }
      return Blit(surface, destRect, destOrigin, fbSize, convertAlpha,
                  aDestFormat);
    }
#endif
    default:
      return false;
  }
}

// -------------------------------------

const char* GLBlitHelper::GetAlphaMixin(
    Maybe<gfxAlphaType> convertAlpha) const {
  if (!convertAlpha) {
    return nullptr;
  }
  switch (convertAlpha.value()) {
    case gfxAlphaType::Premult:
      return kFragMixin_AlphaMultColors;
    case gfxAlphaType::NonPremult:
      return kFragMixin_AlphaUnpremultColors;
    case gfxAlphaType::Opaque:
      return kFragMixin_AlphaOne;
    default:
      return nullptr;
  }
}

#ifdef MOZ_WIDGET_ANDROID
bool GLBlitHelper::Blit(layers::AndroidHardwareBuffer* const buffer,
                        const gfx::IntRect& destRect,
                        const OriginPos destOrigin, const gfx::IntSize& fbSize,
                        const Maybe<gfxAlphaType> convertAlpha) const {
  MOZ_ASSERT(buffer);

  if (!buffer) {
    return false;
  }

  if (!mGL->MakeCurrent()) {
    return false;
  }

  const auto& gle = GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;

  auto fenceFd = buffer->GetAcquireFence();
  if (fenceFd) {
    const EGLint attribs[] = {
        LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
        fenceFd.get(),
        LOCAL_EGL_NONE,
    };

    EGLSync sync =
        egl->fCreateSyncKHR(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
    if (!sync) {
      gfxCriticalNote
          << "GLBlitHelper::Blit: Failed to create EGLSync from acquire fence";
      return false;
    }

    // EGL owns the fd after a successful eglCreateSyncKHR().
    (void)fenceFd.release();

    if (egl->IsExtensionSupported(EGLExtension::KHR_wait_sync)) {
      egl->fWaitSync(sync, 0);
    } else {
      egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER);
    }
    egl->fDestroySync(sync);
  }

  EGLClientBuffer clientBuffer =
      egl->mLib->fGetNativeClientBufferANDROID(buffer->GetNativeBuffer());
  if (!clientBuffer) {
    gfxCriticalNote
        << "GLBlitHelper::Blit: eglGetNativeClientBufferANDROID failed";
    return false;
  }

  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_TRUE,
      LOCAL_EGL_NONE,
  };

  const EGLImage image = egl->fCreateImage(
      EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
  if (!image) {
    gfxCriticalNote << "GLBlitHelper::Blit: eglCreateImage failed";
    return false;
  }

  const bool ret = Blit(image, EGL_NO_SYNC, buffer->mSize, destRect, destOrigin,
                        fbSize, convertAlpha);

  egl->fDestroyImage(image);

  return ret;
}

bool GLBlitHelper::Blit(layers::AndroidImageReader* imageReader,
                        const layers::AndroidMediaCodecFrameId frameId,
                        const gfx::IntSize& texSize,
                        const gfx::IntRect& destRect, OriginPos destOrigin,
                        const gfx::IntSize& fbSize,
                        Maybe<gfxAlphaType> convertAlpha) const {
  MOZ_ASSERT(imageReader);

  if (!mGL->MakeCurrent()) {
    return false;
  }

  const ScopedBindTextureUnit boundTU(mGL, LOCAL_GL_TEXTURE0);
  ScopedTexture tex(mGL);
  ScopedBindTexture bindTex(mGL, tex.Texture(), LOCAL_GL_TEXTURE_EXTERNAL);

  mGL->TexParams_SetClampNoMips(LOCAL_GL_TEXTURE_EXTERNAL);

  RefPtr<layers::AndroidImageWrapper> image;
  if (!imageReader->UpdateTexImage(frameId, mGL, tex.Texture(),
                                   getter_AddRefs(image))) {
    return false;
  }

  const auto srcOrigin = OriginPos::BottomLeft;
  const bool yFlip = (srcOrigin != destOrigin);

  const auto& prog = GetDrawBlitProg(
      {kFragHeader_TexExt,
       {kFragSample_OnePlane, kFragConvert_None, GetAlphaMixin(convertAlpha)}});

  const DrawBlitProg::BaseArgs baseArgs = {SubRectMat3(0, 0, 1, 1), yFlip,
                                           fbSize, destRect, texSize};
  prog.Draw(baseArgs);

  return true;
}

bool GLBlitHelper::Blit(const java::GeckoSurfaceTexture::Ref& surfaceTexture,
                        const gfx::IntSize& texSize,
                        const gfx::IntRect& destRect,
                        const OriginPos destOrigin, const gfx::IntSize& fbSize,
                        Maybe<gfxAlphaType> convertAlpha) const {
  if (!surfaceTexture) {
    return false;
  }

  const ScopedBindTextureUnit boundTU(mGL, LOCAL_GL_TEXTURE0);

  if (!surfaceTexture->IsAttachedToGLContext((int64_t)mGL)) {
    GLuint tex;
    mGL->MakeCurrent();
    mGL->fGenTextures(1, &tex);

    if (NS_FAILED(surfaceTexture->AttachToGLContext((int64_t)mGL, tex))) {
      mGL->fDeleteTextures(1, &tex);
      return false;
    }
  }

  const ScopedBindTexture savedTex(mGL, surfaceTexture->GetTexName(),
                                   LOCAL_GL_TEXTURE_EXTERNAL);
  surfaceTexture->UpdateTexImage();

  gfx::Matrix4x4 transform;
  const auto surf = java::sdk::SurfaceTexture::LocalRef::From(surfaceTexture);
  gl::AndroidSurfaceTexture::GetTransformMatrix(surf, &transform);
  // SurfaceTexture transforms should always be 2D
  MOZ_DIAGNOSTIC_ASSERT(transform.Is2D());
  const auto transform3 = MatrixToMat3(transform.As2D());

  // The origin is top-left here due to the transform supplied in the surface.
  const auto srcOrigin = OriginPos::TopLeft;
  const bool yFlip = (srcOrigin != destOrigin);
  const auto& prog = GetDrawBlitProg(
      {kFragHeader_TexExt,
       {kFragSample_OnePlane, kFragConvert_None, GetAlphaMixin(convertAlpha)}});
  const DrawBlitProg::BaseArgs baseArgs = {transform3, yFlip, fbSize, destRect,
                                           texSize};
  prog.Draw(baseArgs, nullptr);

  if (surfaceTexture->IsSingleBuffer()) {
    surfaceTexture->ReleaseTexImage();
  }

  return true;
}

bool GLBlitHelper::Blit(EGLImage image, EGLSync fence,
                        const gfx::IntSize& texSize,
                        const gfx::IntRect& destRect,
                        const OriginPos destOrigin, const gfx::IntSize& fbSize,
                        Maybe<gfxAlphaType> convertAlpha) const {
  if (!image) {
    return false;
  }

  GLenum target = mGL->GetPreferredEGLImageTextureTarget();
  const ScopedBindTextureUnit boundTU(mGL, LOCAL_GL_TEXTURE0);
  ScopedTexture tex(mGL);
  ScopedBindTexture bindTex(mGL, tex.Texture(), target);

  mGL->TexParams_SetClampNoMips(target);
  mGL->fEGLImageTargetTexture2D(target, image);

  const auto srcOrigin = OriginPos::BottomLeft;
  const bool yFlip = (srcOrigin != destOrigin);
  const auto& prog = GetDrawBlitProg(
      {target == LOCAL_GL_TEXTURE_2D ? kFragHeader_Tex2D : kFragHeader_TexExt,
       {kFragSample_OnePlane, kFragConvert_None, GetAlphaMixin(convertAlpha)}});
  const DrawBlitProg::BaseArgs baseArgs = {SubRectMat3(0, 0, 1, 1), yFlip,
                                           fbSize, destRect, texSize};
  prog.Draw(baseArgs);

  return true;
}
#endif

// -------------------------------------

bool GuessDivisors(const gfx::IntSize& ySize, const gfx::IntSize& uvSize,
                   gfx::IntSize* const out_divisors) {
  const gfx::IntSize divisors((ySize.width == uvSize.width) ? 1 : 2,
                              (ySize.height == uvSize.height) ? 1 : 2);
  if (uvSize.width * divisors.width != ySize.width ||
      uvSize.height * divisors.height != ySize.height) {
    return false;
  }
  *out_divisors = divisors;
  return true;
}

bool GLBlitHelper::BlitPlanarYCbCr(const PlanarYCbCrData& yuvData,
                                   const gfx::IntRect& destRect,
                                   const OriginPos destOrigin,
                                   const gfx::IntSize& fbSize,
                                   Maybe<gfxAlphaType> convertAlpha) {
  const auto& prog =
      GetDrawBlitProg({kFragHeader_Tex2D,
                       {kFragSample_ThreePlane, kFragConvert_ColorMatrix,
                        GetAlphaMixin(convertAlpha)}});

  if (!mYuvUploads[0]) {
    mGL->fGenTextures(3, mYuvUploads);
    const ScopedBindTexture bindTex(mGL, mYuvUploads[0], LOCAL_GL_TEXTURE_2D);
    mGL->TexParams_SetClampNoMips(LOCAL_GL_TEXTURE_2D);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[1]);
    mGL->TexParams_SetClampNoMips(LOCAL_GL_TEXTURE_2D);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[2]);
    mGL->TexParams_SetClampNoMips(LOCAL_GL_TEXTURE_2D);
  }

  // --

  auto ySize = yuvData.YDataSize();
  auto cbcrSize = yuvData.CbCrDataSize();
  if (yuvData.mYSkip || yuvData.mCbSkip || yuvData.mCrSkip || ySize.width < 0 ||
      ySize.height < 0 || cbcrSize.width < 0 || cbcrSize.height < 0 ||
      yuvData.mYStride < 0 || yuvData.mCbCrStride < 0) {
    gfxCriticalError() << "Unusual PlanarYCbCrData: " << yuvData.mYSkip << ","
                       << yuvData.mCbSkip << "," << yuvData.mCrSkip << ", "
                       << ySize.width << "," << ySize.height << ", "
                       << cbcrSize.width << "," << cbcrSize.height << ", "
                       << yuvData.mYStride << "," << yuvData.mCbCrStride;
    return false;
  }

  gfx::IntSize divisors;
  switch (yuvData.mChromaSubsampling) {
    case gfx::ChromaSubsampling::FULL:
      divisors = gfx::IntSize(1, 1);
      break;
    case gfx::ChromaSubsampling::HALF_WIDTH:
      divisors = gfx::IntSize(2, 1);
      break;
    case gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:
      divisors = gfx::IntSize(2, 2);
      break;
    default:
      gfxCriticalError() << "Unknown chroma subsampling:"
                         << int(yuvData.mChromaSubsampling);
      return false;
  }

  // --

  // RED textures aren't valid in GLES2, and ALPHA textures are not valid in
  // desktop GL Core Profiles. So use R8 textures on GL3.0+ and GLES3.0+, but
  // LUMINANCE/LUMINANCE/UNSIGNED_BYTE otherwise.
  GLenum internalFormat;
  GLenum unpackFormat;
  if (mGL->IsAtLeast(gl::ContextProfile::OpenGLCore, 300) ||
      mGL->IsAtLeast(gl::ContextProfile::OpenGLES, 300)) {
    internalFormat = LOCAL_GL_R8;
    unpackFormat = LOCAL_GL_RED;
  } else {
    internalFormat = LOCAL_GL_LUMINANCE;
    unpackFormat = LOCAL_GL_LUMINANCE;
  }

  // --

  const ScopedSaveMultiTex saveTex(mGL, 3, LOCAL_GL_TEXTURE_2D);
  const ResetUnpackState reset(mGL);
  const gfx::IntSize yTexSize(yuvData.mYStride, yuvData.YDataSize().height);
  const gfx::IntSize uvTexSize(yuvData.mCbCrStride,
                               yuvData.CbCrDataSize().height);

  if (yTexSize != mYuvUploads_YSize || uvTexSize != mYuvUploads_UVSize) {
    mYuvUploads_YSize = yTexSize;
    mYuvUploads_UVSize = uvTexSize;

    mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[0]);
    mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat, yTexSize.width,
                     yTexSize.height, 0, unpackFormat, LOCAL_GL_UNSIGNED_BYTE,
                     nullptr);
    for (int i = 1; i < 3; i++) {
      mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
      mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[i]);
      mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat, uvTexSize.width,
                       uvTexSize.height, 0, unpackFormat,
                       LOCAL_GL_UNSIGNED_BYTE, nullptr);
    }
  }

  // --

  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[0]);
  mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0, yTexSize.width,
                      yTexSize.height, unpackFormat, LOCAL_GL_UNSIGNED_BYTE,
                      yuvData.mYChannel);
  mGL->fActiveTexture(LOCAL_GL_TEXTURE1);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[1]);
  mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0, uvTexSize.width,
                      uvTexSize.height, unpackFormat, LOCAL_GL_UNSIGNED_BYTE,
                      yuvData.mCbChannel);
  mGL->fActiveTexture(LOCAL_GL_TEXTURE2);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[2]);
  mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0, uvTexSize.width,
                      uvTexSize.height, unpackFormat, LOCAL_GL_UNSIGNED_BYTE,
                      yuvData.mCrChannel);

  // --

  const auto& clipRect = yuvData.mPictureRect;
  const auto srcOrigin = OriginPos::BottomLeft;
  const bool yFlip = (destOrigin != srcOrigin);

  const DrawBlitProg::BaseArgs baseArgs = {SubRectMat3(clipRect, yTexSize),
                                           yFlip, fbSize, destRect,
                                           clipRect.Size()};
  const DrawBlitProg::YUVArgs yuvArgs = {
      SubRectMat3(clipRect, uvTexSize, divisors), Some(yuvData.mYUVColorSpace)};
  prog.Draw(baseArgs, &yuvArgs);
  return true;
}

// -------------------------------------

#ifdef XP_MACOSX
static std::string IntAsAscii(const int x) {
  std::string str;
  str.reserve(6);
  auto u = static_cast<unsigned int>(x);
  while (u) {
    str.insert(str.begin(), u & 0xff);
    u >>= 8;
  }
  str.insert(str.begin(), '\'');
  str.push_back('\'');
  return str;
}

bool GLBlitHelper::BlitImage(MacIOSurface* const iosurf,
                             const gfx::IntRect& destRect,
                             const OriginPos destOrigin,
                             const gfx::IntSize& fbSize,
                             Maybe<gfxAlphaType> convertAlpha) const {
  if (!iosurf) {
    gfxCriticalError() << "Null MacIOSurface for GLBlitHelper::BlitImage";
    return false;
  }
  if (mGL->GetContextType() != GLContextType::CGL &&
      mGL->GetContextType() != GLContextType::EGL) {
    MOZ_ASSERT_UNREACHABLE();
    return false;
  }

  const auto& srcOrigin = OriginPos::BottomLeft;

  DrawBlitProg::BaseArgs baseArgs;
  baseArgs.yFlip = (destOrigin != srcOrigin);
  baseArgs.fbSize = fbSize;
  baseArgs.destRect = destRect;

  // TODO: The colorspace is known by the IOSurface, why override it?
  // See GetYUVColorSpace/GetFullRange()
  DrawBlitProg::YUVArgs yuvArgs;
  yuvArgs.colorSpaceForMatrix = Some(iosurf->GetYUVColorSpace());

  auto planes = iosurf->GetPlaneCount();
  if (!planes) {
    planes = 1;  // Bad API. No cookie.
  }

  const auto pixelFormat = iosurf->GetPixelFormat();
  if (mGL->ShouldSpew()) {
    const auto formatStr = IntAsAscii(pixelFormat);
    printf_stderr("iosurf format: %s (0x%08x)\n", formatStr.c_str(),
                  pixelFormat);
  }

  const char* fragSample = nullptr;
  bool yuv = true;
  switch (planes) {
    case 1:
      switch (pixelFormat) {
        case kCVPixelFormatType_24RGB:
        case kCVPixelFormatType_24BGR:
        case kCVPixelFormatType_32ARGB:
        case kCVPixelFormatType_32BGRA:
        case kCVPixelFormatType_32ABGR:
        case kCVPixelFormatType_32RGBA:
        case kCVPixelFormatType_64ARGB:
        case kCVPixelFormatType_48RGB:
          fragSample = kFragSample_OnePlane;
          yuv = false;
          break;
        case kCVPixelFormatType_422YpCbCr8:
        case kCVPixelFormatType_422YpCbCr8_yuvs:
          fragSample = kFragSample_OnePlane_YUV_via_GBR;
          break;
        default: {
          std::string str;
          if (pixelFormat <= 0xff) {
            str = std::to_string(pixelFormat);
          } else {
            str = IntAsAscii(pixelFormat);
          }
          gfxCriticalError() << "Unhandled kCVPixelFormatType_*: " << str;
          // Probably YUV though
          fragSample = kFragSample_OnePlane_YUV_via_GBR;
          break;
        }
      }
      break;
    case 2:
      fragSample = kFragSample_TwoPlane;
      break;
    case 3:
      fragSample = kFragSample_ThreePlane;
      break;
    default:
      gfxCriticalError() << "Unexpected plane count: " << planes;
      return false;
  }

  const GLenum texTarget = mGL->GetPreferredMacIOSurfaceTextureTarget();
  const ScopedSaveMultiTex saveTex(mGL, planes, texTarget);
  Maybe<ScopedTexture> texs[3];

  for (uint32_t p = 0; p < planes; p++) {
    texs[p].emplace(mGL);
    mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + p);
    mGL->fBindTexture(texTarget, texs[p]->Texture());
    mGL->TexParams_SetClampNoMips(texTarget);

    if (!iosurf->BindTexImage(mGL, p)) {
      return false;
    }
  }

  const auto width = iosurf->GetDevicePixelWidth(0);
  const auto height = iosurf->GetDevicePixelHeight(0);
  baseArgs.texSize = gfx::IntSize(width, height);
  const char* texHeader;
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_RECTANGLE_ARB:
      texHeader = kFragHeader_Tex2DRect;
      baseArgs.texMatrix0 = SubRectMat3(0, 0, width, height);
      yuvArgs.texMatrix1 = SubRectMat3(0, 0, iosurf->GetDevicePixelWidth(1),
                                       iosurf->GetDevicePixelHeight(1));
      break;
    case LOCAL_GL_TEXTURE_2D:
      texHeader = kFragHeader_Tex2D;
      baseArgs.texMatrix0 = Mat3::I();
      yuvArgs.texMatrix1 = Mat3::I();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE();
      return false;
  }

  const char* fragConvert = yuv ? kFragConvert_ColorMatrix : kFragConvert_None;
  const auto& prog = GetDrawBlitProg({
      texHeader,
      {fragSample, fragConvert, GetAlphaMixin(convertAlpha)},
  });
  prog.Draw(baseArgs, yuv ? &yuvArgs : nullptr);
  return true;
}
#endif

// -----------------------------------------------------------------------------

void GLBlitHelper::DrawBlitTextureToFramebuffer(
    const GLuint srcTex, const gfx::IntSize& srcSize,
    const gfx::IntSize& destSize, const GLenum srcTarget, const bool srcIsBGRA,
    const bool yFlip, const Maybe<gfxAlphaType> convertAlpha) const {
  const char* fragHeader = nullptr;
  Mat3 texMatrix0;
  switch (srcTarget) {
    case LOCAL_GL_TEXTURE_2D:
      fragHeader = kFragHeader_Tex2D;
      texMatrix0 = Mat3::I();
      break;
    case LOCAL_GL_TEXTURE_RECTANGLE_ARB:
      fragHeader = kFragHeader_Tex2DRect;
      texMatrix0 = SubRectMat3(0, 0, srcSize.width, srcSize.height);
      break;
    default:
      gfxCriticalError() << "Unexpected srcTarget: " << srcTarget;
      return;
  }
  const auto fragConvert = srcIsBGRA ? kFragConvert_BGR : kFragConvert_None;
  const auto& prog = GetDrawBlitProg(
      {fragHeader,
       {kFragSample_OnePlane, fragConvert, GetAlphaMixin(convertAlpha)}});

  const ScopedSaveMultiTex saveTex(mGL, 1, srcTarget);
  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->fBindTexture(srcTarget, srcTex);

  const DrawBlitProg::BaseArgs baseArgs = {texMatrix0, yFlip, destSize, {}};
  prog.Draw(baseArgs);
}

// -----------------------------------------------------------------------------

void GLBlitHelper::BlitFramebuffer(const gfx::IntRect& srcRect,
                                   const gfx::IntRect& destRect,
                                   GLuint filter) const {
  MOZ_ASSERT(mGL->IsSupported(GLFeature::framebuffer_blit));

  const ScopedGLState scissor(mGL, LOCAL_GL_SCISSOR_TEST, false);
  mGL->fBlitFramebuffer(srcRect.x, srcRect.y, srcRect.XMost(), srcRect.YMost(),
                        destRect.x, destRect.y, destRect.XMost(),
                        destRect.YMost(), LOCAL_GL_COLOR_BUFFER_BIT, filter);
}

// --

void GLBlitHelper::BlitFramebufferToFramebuffer(const GLuint srcFB,
                                                const GLuint destFB,
                                                const gfx::IntRect& srcRect,
                                                const gfx::IntRect& destRect,
                                                GLuint filter) const {
  MOZ_ASSERT(mGL->IsSupported(GLFeature::framebuffer_blit));
  MOZ_GL_ASSERT(mGL, !srcFB || mGL->fIsFramebuffer(srcFB));
  MOZ_GL_ASSERT(mGL, !destFB || mGL->fIsFramebuffer(destFB));

  const ScopedBindFramebuffer boundFB(mGL);
  mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, srcFB);
  mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destFB);

  BlitFramebuffer(srcRect, destRect, filter);
}

void GLBlitHelper::BlitTextureToFramebuffer(GLuint srcTex,
                                            const gfx::IntSize& srcSize,
                                            const gfx::IntSize& destSize,
                                            GLenum srcTarget) const {
  MOZ_GL_ASSERT(mGL, mGL->fIsTexture(srcTex));

  if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
    const ScopedFramebufferForTexture srcWrapper(mGL, srcTex, srcTarget);
    const ScopedBindFramebuffer bindFB(mGL);
    mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, srcWrapper.FB());
    BlitFramebuffer(gfx::IntRect({}, srcSize), gfx::IntRect({}, destSize));
    return;
  }

  DrawBlitTextureToFramebuffer(srcTex, srcSize, destSize, srcTarget);
}

void GLBlitHelper::BlitFramebufferToTexture(GLuint destTex,
                                            const gfx::IntSize& srcSize,
                                            const gfx::IntSize& destSize,
                                            GLenum destTarget) const {
  MOZ_GL_ASSERT(mGL, mGL->fIsTexture(destTex));

  if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
    const ScopedFramebufferForTexture destWrapper(mGL, destTex, destTarget);
    const ScopedBindFramebuffer bindFB(mGL);
    mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destWrapper.FB());
    BlitFramebuffer(gfx::IntRect({}, srcSize), gfx::IntRect({}, destSize));
    return;
  }

  ScopedBindTexture autoTex(mGL, destTex, destTarget);
  ScopedGLState scissor(mGL, LOCAL_GL_SCISSOR_TEST, false);
  mGL->fCopyTexSubImage2D(destTarget, 0, 0, 0, 0, 0, srcSize.width,
                          srcSize.height);
}

void GLBlitHelper::BlitTextureToTexture(GLuint srcTex, GLuint destTex,
                                        const gfx::IntSize& srcSize,
                                        const gfx::IntSize& destSize,
                                        GLenum srcTarget,
                                        GLenum destTarget) const {
  MOZ_GL_ASSERT(mGL, mGL->fIsTexture(srcTex));
  MOZ_GL_ASSERT(mGL, mGL->fIsTexture(destTex));

  // Start down the CopyTexSubImage path, not the DrawBlit path.
  const ScopedFramebufferForTexture srcWrapper(mGL, srcTex, srcTarget);
  const ScopedBindFramebuffer bindFB(mGL, srcWrapper.FB());
  BlitFramebufferToTexture(destTex, srcSize, destSize, destTarget);
}

// -------------------------------------
#ifdef MOZ_WIDGET_GTK
bool GLBlitHelper::Blit(DMABufSurface* surface, const gfx::IntRect& destRect,
                        OriginPos destOrigin, const gfx::IntSize& fbSize,
                        Maybe<gfxAlphaType> convertAlpha,
                        gfx::SurfaceFormat aDestFormat) const {
  const auto& srcOrigin = OriginPos::BottomLeft;

  DrawBlitProg::BaseArgs baseArgs;
  baseArgs.yFlip = (destOrigin != srcOrigin);
  baseArgs.fbSize = fbSize;
  baseArgs.destRect = destRect;

  // TODO: The colorspace is known by the DMABUFSurface, why override it?
  // See GetYUVColorSpace/GetFullRange()
  DrawBlitProg::YUVArgs yuvArgs;
  yuvArgs.colorSpaceForMatrix = Some(surface->GetYUVColorSpace());

  const DrawBlitProg::YUVArgs* pYuvArgs = nullptr;
  const auto planes = surface->GetTextureCount();

  // The shaders used below currently only support 1-3 planes.
  if (planes < 1 || planes > 3) {
    gfxCriticalError() << "Unexpected DMABuf planes count: " << planes;
    return false;
  }

  // -
  // Ensure textures for all planes have been created.

  const bool createTextures = [&]() {
    for (int i = 0; i < planes; i++) {
      if (!surface->GetTexture(i)) {
        return true;
      }
    }
    return false;
  }();

  bool didCreateTexture = false;
  auto releaseTextures = mozilla::MakeScopeExit([&] {
    if (didCreateTexture) {
      surface->ReleaseTextures();
    }
  });

  if (createTextures) {
    if (!surface->CreateTextures(mGL)) {
      LOGDMABUF(("GLBlitHelper::Blit(): Failed to create DMABuf textures."));
      return false;
    }
    didCreateTexture = true;
  }

  // -

  const GLenum texTarget = LOCAL_GL_TEXTURE_2D;

  const ScopedSaveMultiTex saveTex(mGL, planes, texTarget);
  const auto pixelFormat = surface->GetSurfaceType();

  const char* fragSample = nullptr;
  auto fragConvert = kFragConvert_None;
  // Determine the reorder from source and destination formats. YUV sources
  // are converted to RGB by the color matrix, while single-plane RGBA
  // sources are sampled raw, so a BGRA-ordered DMABuf (e.g. a WebGL
  // backbuffer) already matches a BGRA destination. Swap R/B exactly when
  // the two orders differ.
  const auto srcFormat = surface->GetFormat();
  const bool srcIsBGRA = srcFormat == gfx::SurfaceFormat::B8G8R8A8 ||
                         srcFormat == gfx::SurfaceFormat::B8G8R8X8;
  const bool dstIsBGRA = aDestFormat == gfx::SurfaceFormat::B8G8R8A8 ||
                         aDestFormat == gfx::SurfaceFormat::B8G8R8X8;
  const bool swapRB = srcIsBGRA != dstIsBGRA;
  switch (pixelFormat) {
    case DMABufSurface::SURFACE_RGBA:
      fragSample = kFragSample_OnePlane;
      if (swapRB) {
        fragConvert = kFragConvert_BGR;
      }
      break;
    case DMABufSurface::SURFACE_YUV:
      if (surface->GetTextureCount() == 2) {
        fragSample = kFragSample_TwoPlane;
      } else if (surface->GetTextureCount() == 3) {
        fragSample = kFragSample_ThreePlane;
      } else {
        gfxCriticalError() << "Unexpected planes count: "
                           << surface->GetTextureCount();
        return false;
      }
      pYuvArgs = &yuvArgs;
      fragConvert =
          swapRB ? kFragConvert_ColorMatrixBGR : kFragConvert_ColorMatrix;
      break;
    default:
      return false;
  }

  for (const auto p : IntegerRange(planes)) {
    mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + p);
    mGL->fBindTexture(texTarget, surface->GetTexture(p));
    mGL->TexParams_SetClampNoMips(texTarget);
  }

  // Crop to visible region: VA-API surfaces may have macroblock-aligned
  // textures larger than the visible size (bug 2054811).
  baseArgs.texMatrix0 = SubRectMat3(
      0, 0, float(surface->GetWidth(0)) / float(surface->GetWidthAligned(0)),
      float(surface->GetHeight(0)) / float(surface->GetHeightAligned(0)));
  baseArgs.texSize = gfx::IntSize(surface->GetWidth(), surface->GetHeight());
  yuvArgs.texMatrix1 = SubRectMat3(
      0, 0, float(surface->GetWidth(1)) / float(surface->GetWidthAligned(1)),
      float(surface->GetHeight(1)) / float(surface->GetHeightAligned(1)));

  const auto& prog =
      GetDrawBlitProg({kFragHeader_Tex2D,
                       {fragSample, fragConvert, GetAlphaMixin(convertAlpha)}});
  prog.Draw(baseArgs, pYuvArgs);

  return true;
}

bool GLBlitHelper::BlitYCbCrTexturesToDMABuf(BufferSurface* aSource,
                                             DMABufSurface* aDest) {
  if (aSource->GetTextureCount() != 3) {
    gfxCriticalError() << "BlitYCbCrTexturesToDMABuf: unexpected source planes "
                       << aSource->GetTextureCount();
    return false;
  }

  const ScopedSaveMultiTex saveTex(mGL, 3, LOCAL_GL_TEXTURE_2D);
  for (int i = 0; i < 3; i++) {
    mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, aSource->GetTexture(i));
  }

  DrawBlitProg::BaseArgs baseArgs;
  baseArgs.yFlip = false;
  // Source plane textures hold the picture only, there's nothing to crop.
  baseArgs.texMatrix0 = SubRectMat3(0, 0, 1, 1);

  const bool is10Bit = aSource->GetColorDepth() == gfx::ColorDepth::COLOR_10;
  const char* fragConvert = is10Bit ? kFragConvertYUVP010 : kFragConvert_None;

  // Blit Y plane
  {
    ScopedFramebufferForTexture autoFBForTex(mGL, aDest->GetTexture(0));
    if (!autoFBForTex.IsComplete()) {
      gfxCriticalError() << "GLBlitHelper::BlitYCbCrTexturesToDMABuf: "
                            "ScopedFramebufferForTexture failed.";
      return false;
    }
    const ScopedBindFramebuffer bindFB(mGL, autoFBForTex.FB());

    baseArgs.fbSize = gfx::IntSize(aDest->GetWidth(), aDest->GetHeight());
    const auto& prog = GetDrawBlitProg(
        {kFragHeader_Tex2D, {kFragSample_OnePlane, fragConvert}});
    prog.Draw(baseArgs);
  }

  // Blit UV planes
  {
    ScopedFramebufferForTexture autoFBForTex(mGL, aDest->GetTexture(1));
    if (!autoFBForTex.IsComplete()) {
      gfxCriticalError() << "GLBlitHelper::BlitYCbCrTexturesToDMABuf: "
                            "ScopedFramebufferForTexture failed.";
      return false;
    }
    const ScopedBindFramebuffer bindFB(mGL, autoFBForTex.FB());

    baseArgs.fbSize = gfx::IntSize(aDest->GetWidth(1), aDest->GetHeight(1));
    const auto& prog = GetDrawBlitProg(
        {kFragHeader_Tex2D, {kFragSample_TwoPlaneUV, fragConvert}});
    prog.Draw(baseArgs);
  }

  return true;
}
#endif

// -

template <size_t N>
static void PushUnorm(uint32_t* const out, const float inVal) {
  const uint32_t mask = (1 << N) - 1;
  auto fval = inVal;
  fval = std::clamp(fval, 0.0f, 1.0f);
  fval *= mask;
  fval = roundf(fval);
  auto ival = static_cast<uint32_t>(fval);
  ival &= mask;

  *out <<= N;
  *out |= ival;
}

static uint32_t toRgb10A2(const color::vec4& val) {
  // R in LSB
  uint32_t ret = 0;
  PushUnorm<2>(&ret, val.w());
  PushUnorm<10>(&ret, val.z());
  PushUnorm<10>(&ret, val.y());
  PushUnorm<10>(&ret, val.x());
  return ret;
}

// -

color::ColorspaceDesc ToColorspaceDesc(const gfx::YUVRangedColorSpace cs) {
  switch (cs) {
    case gfx::YUVRangedColorSpace::BT601_Narrow:
      return {
          .chrom = color::Chromaticities::Rec601_525_Ntsc(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT601_Full:
      return {
          .chrom = color::Chromaticities::Rec601_525_Ntsc(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT709_Narrow:
      return {
          .chrom = color::Chromaticities::Rec709(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT709_Full:
      return {
          .chrom = color::Chromaticities::Rec709(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2020_Narrow:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2020_12bit(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2020_Full:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2020_12bit(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2100_PQ_Narrow:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_PQ(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2100_PQ_Full:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_PQ(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2100_HLG_Narrow:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_HLG(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
    case gfx::YUVRangedColorSpace::BT2100_HLG_Full:
      return {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_HLG(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
    case gfx::YUVRangedColorSpace::GbrIdentity:
      return {
          .chrom = color::Chromaticities::Rec709(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Gbr(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
  }
  MOZ_CRASH("Bad YUVRangedColorSpace.");
}

}  // namespace gl
namespace gfx {

color::ColorProfileDesc QueryOutputColorProfile();

}  // namespace gfx
namespace gl {

// -

/* static */
std::optional<color::ColorProfileDesc> GLBlitHelper::ToColorProfileDesc(
    const gfx::ColorSpace2 cspace, const gfx::TransferFunction tf) {
  color::ColorspaceDesc cspaceDesc;

  const bool rec709GammaAsSrgb =
      StaticPrefs::gfx_color_management_rec709_gamma_as_srgb();
  const bool rec2020GammaAsRec709 =
      StaticPrefs::gfx_color_management_rec2020_gamma_as_rec709();

  // Pairings of Colorspace2 and TransferFunction have a few unique interactions
  switch (cspace) {
    case gfx::ColorSpace2::Display:
      if (kIsWindows) {
#ifdef XP_WIN
        return gfx::QueryOutputColorProfile();
#endif
      }
      return {};

    case gfx::ColorSpace2::SRGB:
      cspaceDesc.chrom = color::Chromaticities::Srgb();
      cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
      switch (tf) {
        case gfx::TransferFunction::SRGB:
          cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          break;
        case gfx::TransferFunction::BT709:
          if (rec709GammaAsSrgb) {
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          } else {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
          }
          break;
        case gfx::TransferFunction::HLG:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
          break;
        case gfx::TransferFunction::PQ:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
          break;
        case gfx::TransferFunction::LINEAR:
          cspaceDesc.tf = color::TransferFunctionDesc::Linear();
          break;
      }
      break;
    case gfx::ColorSpace2::DISPLAY_P3:
      cspaceDesc.chrom = color::Chromaticities::DisplayP3();
      cspaceDesc.tf = color::TransferFunctionDesc::DisplayP3();
      switch (tf) {
        case gfx::TransferFunction::SRGB:
          cspaceDesc.tf = color::TransferFunctionDesc::DisplayP3();
          break;
        case gfx::TransferFunction::BT709:
          if (rec709GammaAsSrgb) {
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          } else {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
          }
          break;
        case gfx::TransferFunction::HLG:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
          break;
        case gfx::TransferFunction::PQ:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
          break;
        case gfx::TransferFunction::LINEAR:
          cspaceDesc.tf = color::TransferFunctionDesc::Linear();
          break;
      }
      break;
    case gfx::ColorSpace2::BT601_525:  // aka smpte170m NTSC
      cspaceDesc.chrom = color::Chromaticities::Rec601_525_Ntsc();
      cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
      switch (tf) {
        case gfx::TransferFunction::SRGB:
          cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          break;
        case gfx::TransferFunction::BT709:
          if (rec709GammaAsSrgb) {
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          } else {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
          }
          break;
        case gfx::TransferFunction::HLG:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
          break;
        case gfx::TransferFunction::PQ:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
          break;
        case gfx::TransferFunction::LINEAR:
          cspaceDesc.tf = color::TransferFunctionDesc::Linear();
          break;
      }
      break;
    case gfx::ColorSpace2::BT709:  // Same gamut as SRGB, but different gamma.
      cspaceDesc.chrom = color::Chromaticities::Rec709();
      cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
      switch (tf) {
        case gfx::TransferFunction::SRGB:
          cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          break;
        case gfx::TransferFunction::BT709:
          if (rec709GammaAsSrgb) {
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          } else {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
          }
          break;
        case gfx::TransferFunction::HLG:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
          break;
        case gfx::TransferFunction::PQ:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
          break;
        case gfx::TransferFunction::LINEAR:
          cspaceDesc.tf = color::TransferFunctionDesc::Linear();
          break;
      }
      break;
    case gfx::ColorSpace2::BT2020:
      cspaceDesc.chrom = color::Chromaticities::Rec2020();
      cspaceDesc.tf = color::TransferFunctionDesc::Rec2020_12bit();
      switch (tf) {
        case gfx::TransferFunction::SRGB:
          cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          break;
        case gfx::TransferFunction::BT709:
          // BT2020 uses a higher precision version of BT709/BT1886 values.
          if (rec2020GammaAsRec709 && rec709GammaAsSrgb) {
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
          } else if (rec2020GammaAsRec709) {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
          } else {
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2020_12bit();
          }
          break;
        case gfx::TransferFunction::HLG:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
          break;
        case gfx::TransferFunction::PQ:
          cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
          break;
        case gfx::TransferFunction::LINEAR:
          cspaceDesc.tf = color::TransferFunctionDesc::Linear();
          break;
      }
      break;
  }
  const auto profileDesc = color::ColorProfileDesc::From(cspaceDesc);
  return profileDesc;
}

// -

// For std::visit
template <class... Ts>
struct MOZ_EMPTY_BASES overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// -

template <typename C, typename K>
inline auto MaybeFind(C& container, const K& key)
    -> decltype(&(container.find(key)->second)) {
  const auto itr = container.find(key);
  if (itr == container.end()) return nullptr;
  return &(itr->second);
}

// -

std::shared_ptr<gl::Texture> GLBlitHelper::GetColorLutTex(
    const ColorLutKey& request) const {
  if (const auto found = gl::MaybeFind(mColorLutTexMap, request)) {
    return *found;  // Might be *Some(nullptr) -> nullptr!
  }

  return mColorLutTexMap[request] = [&]() -> std::shared_ptr<gl::Texture> {
    auto& gl = *mGL;
    const auto tex = std::make_shared<gl::Texture>(gl);

    // -

    const std::optional<color::ColorProfileDesc> srcProfile =
        std::visit(overloaded{
                       [&](const GLBlitHelper::CSTF& cs)
                           -> std::optional<color::ColorProfileDesc> {
                         MOZ_ASSERT(cs != request.dst);
                         const auto cpd = ToColorProfileDesc(cs.cs, cs.tf);
                         return cpd;
                       },
                       [&](const gfx::YUVRangedColorSpace& cs)
                           -> std::optional<color::ColorProfileDesc> {
                         const auto csd = ToColorspaceDesc(cs);
                         const auto cpd = color::ColorProfileDesc::From(csd);
                         return cpd;
                       },
                   },
                   request.src);
    MOZ_ASSERT(srcProfile);

    const auto dstProfile = ToColorProfileDesc(request.dst.cs, request.dst.tf);
    if (kIsWindows) {
      MOZ_ASSERT(dstProfile);
    }
    if (!srcProfile || !dstProfile) return nullptr;
    const auto conversion = color::ColorProfileConversionDesc::From({
        .src = *srcProfile,
        .dst = *dstProfile,
    });

    // -

    const auto minLutSize = color::ivec3{2};
    const auto maxLutSize = color::ivec3{256};
    auto lutSize = minLutSize;
    const bool isYcbcr =
        (conversion.srcRgbFromSrcYuv != color::mat4::Identity());
    if (isYcbcr) {
      lutSize.x(int(StaticPrefs::gfx_blithelper_lut_size_ycbcr_y()));
      lutSize.y(int(StaticPrefs::gfx_blithelper_lut_size_ycbcr_cb()));
      lutSize.z(int(StaticPrefs::gfx_blithelper_lut_size_ycbcr_cr()));
    } else {
      lutSize.x(int(StaticPrefs::gfx_blithelper_lut_size_rgb_r()));
      lutSize.y(int(StaticPrefs::gfx_blithelper_lut_size_rgb_g()));
      lutSize.z(int(StaticPrefs::gfx_blithelper_lut_size_rgb_b()));
    }
    lutSize = clamp(lutSize, minLutSize, maxLutSize);

    const auto lut = [&]() {
      auto lut = color::Lut3::Create(lutSize);
      lut.SetMap(
          [&](const color::vec3& src) { return conversion.DstFromSrc(src); });
      return lut;
    }();
    const auto& size = lut.size;

    // -

    constexpr GLenum target = LOCAL_GL_TEXTURE_3D;
    const auto bind = gl::ScopedBindTexture(&gl, tex->name, target);
    gl.fTexParameteri(target, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    gl.fTexParameteri(target, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
    gl.fTexParameteri(target, LOCAL_GL_TEXTURE_WRAP_R, LOCAL_GL_CLAMP_TO_EDGE);
    gl.fTexParameteri(target, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
    gl.fTexParameteri(target, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);

    bool useFloat16 = true;
    if (useFloat16) {
      // Use rgba16f, which we can thankfully upload as rgba32f
      static_assert(sizeof(color::vec4) == sizeof(float) * 4);
      std::vector<color::vec4> uploadData;
      uploadData.reserve(lut.data.size());
      for (const auto& src : lut.data) {
        const auto dst = color::vec4{src, 1};
        uploadData.push_back(dst);
      }

      gl.fTexStorage3D(target, 1, LOCAL_GL_RGBA16F, size.x(), size.y(),
                       size.z());
      gl.fTexSubImage3D(target, 0, 0, 0, 0, size.x(), size.y(), size.z(),
                        LOCAL_GL_RGBA, LOCAL_GL_FLOAT, uploadData.data());
    } else {
      // Use Rgb10A2
      std::vector<uint32_t> uploadData;
      uploadData.reserve(lut.data.size());
      for (const auto& src : lut.data) {
        const auto dst = toRgb10A2({src, 1});
        uploadData.push_back(dst);
      }

      gl.fTexStorage3D(target, 1, LOCAL_GL_RGB10_A2, size.x(), size.y(),
                       size.z());
      gl.fTexSubImage3D(target, 0, 0, 0, 0, size.x(), size.y(), size.z(),
                        LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV,
                        uploadData.data());
    }
    return tex;
  }();
}

}  // namespace gl
}  // namespace mozilla
