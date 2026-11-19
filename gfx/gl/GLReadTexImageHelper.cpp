/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLReadTexImageHelper.h"

#include "GLContext.h"
#include "OGLShaderProgram.h"
#include "ScopedGLHelpers.h"
#include "gfx2DGlue.h"
#include "gfxColor.h"
#include "gfxTypes.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Swizzle.h"

namespace mozilla {
namespace gl {

using namespace mozilla::gfx;

GLReadTexImageHelper::GLReadTexImageHelper(GLContext* gl) : mGL(gl) {
  mPrograms[0] = 0;
  mPrograms[1] = 0;
  mPrograms[2] = 0;
  mPrograms[3] = 0;
}

GLReadTexImageHelper::~GLReadTexImageHelper() {
  if (!mGL->MakeCurrent()) return;

  mGL->fDeleteProgram(mPrograms[0]);
  mGL->fDeleteProgram(mPrograms[1]);
  mGL->fDeleteProgram(mPrograms[2]);
  mGL->fDeleteProgram(mPrograms[3]);
}

static const GLchar readTextureImageVS[] =
    "attribute vec2 aVertex;\n"
    "attribute vec2 aTexCoord;\n"
    "uniform mat4 uTexMatrix;\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(aVertex, 0, 1);\n"
    "  vTexCoord = (uTexMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;\n"
    "}";

static const GLchar readTextureImageFS_TEXTURE_2D[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D uTexture;\n"
    "void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }";

static const GLchar readTextureImageFS_TEXTURE_2D_BGRA[] =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D uTexture;\n"
    "void main() { gl_FragColor = texture2D(uTexture, vTexCoord).bgra; }";

static const GLchar readTextureImageFS_TEXTURE_EXTERNAL[] =
    "#extension GL_OES_EGL_image_external : require\n"
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 vTexCoord;\n"
    "uniform samplerExternalOES uTexture;\n"
    "void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }";

static const GLchar readTextureImageFS_TEXTURE_RECTANGLE[] =
    "#extension GL_ARB_texture_rectangle\n"
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2DRect uTexture;\n"
    "void main() { gl_FragColor = texture2DRect(uTexture, vTexCoord).bgra; }";

GLuint GLReadTexImageHelper::TextureImageProgramFor(GLenum aTextureTarget,
                                                    int aConfig) {
  int variant = 0;
  const GLchar* readTextureImageFS = nullptr;
  if (aTextureTarget == LOCAL_GL_TEXTURE_2D) {
    if (aConfig & mozilla::layers::ENABLE_TEXTURE_RB_SWAP) {
      // Need to swizzle R/B.
      readTextureImageFS = readTextureImageFS_TEXTURE_2D_BGRA;
      variant = 1;
    } else {
      readTextureImageFS = readTextureImageFS_TEXTURE_2D;
      variant = 0;
    }
  } else if (aTextureTarget == LOCAL_GL_TEXTURE_EXTERNAL) {
    readTextureImageFS = readTextureImageFS_TEXTURE_EXTERNAL;
    variant = 2;
  } else if (aTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE) {
    readTextureImageFS = readTextureImageFS_TEXTURE_RECTANGLE;
    variant = 3;
  }

  /* This might be overkill, but assure that we don't access out-of-bounds */
  MOZ_ASSERT((size_t)variant < std::size(mPrograms));
  if (!mPrograms[variant]) {
    GLuint vs = mGL->fCreateShader(LOCAL_GL_VERTEX_SHADER);
    const GLchar* vsSourcePtr = &readTextureImageVS[0];
    mGL->fShaderSource(vs, 1, &vsSourcePtr, nullptr);
    mGL->fCompileShader(vs);

    GLuint fs = mGL->fCreateShader(LOCAL_GL_FRAGMENT_SHADER);
    mGL->fShaderSource(fs, 1, &readTextureImageFS, nullptr);
    mGL->fCompileShader(fs);

    GLuint program = mGL->fCreateProgram();
    mGL->fAttachShader(program, vs);
    mGL->fAttachShader(program, fs);
    mGL->fBindAttribLocation(program, 0, "aVertex");
    mGL->fBindAttribLocation(program, 1, "aTexCoord");
    mGL->fLinkProgram(program);

    GLint success;
    mGL->fGetProgramiv(program, LOCAL_GL_LINK_STATUS, &success);

    if (!success) {
      mGL->fDeleteProgram(program);
      program = 0;
    }

    mGL->fDeleteShader(vs);
    mGL->fDeleteShader(fs);

    mPrograms[variant] = program;
  }

  return mPrograms[variant];
}

bool GLReadTexImageHelper::DidGLErrorOccur(const char* str) {
  GLenum error = mGL->fGetError();
  if (error != LOCAL_GL_NO_ERROR) {
    printf_stderr("GL ERROR: %s %s\n",
                  GLContext::GLErrorToString(error).c_str(), str);
    return true;
  }

  return false;
}

bool GetActualReadFormats(GLContext* gl, GLenum destFormat, GLenum destType,
                          GLenum* out_readFormat, GLenum* out_readType) {
  MOZ_ASSERT(out_readFormat);
  MOZ_ASSERT(out_readType);

  if (destFormat == LOCAL_GL_RGBA && destType == LOCAL_GL_UNSIGNED_BYTE) {
    *out_readFormat = destFormat;
    *out_readType = destType;
    return true;
  }

  bool fallback = true;
  if (gl->IsGLES()) {
    GLenum auxFormat = 0;
    GLenum auxType = 0;

    gl->fGetIntegerv(LOCAL_GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                     (GLint*)&auxFormat);
    gl->fGetIntegerv(LOCAL_GL_IMPLEMENTATION_COLOR_READ_TYPE, (GLint*)&auxType);

    if (destFormat == auxFormat && destType == auxType) {
      fallback = false;
    }
  } else {
    switch (destFormat) {
      case LOCAL_GL_RGB: {
        if (destType == LOCAL_GL_UNSIGNED_SHORT_5_6_5_REV) fallback = false;
        break;
      }
      case LOCAL_GL_BGRA: {
        if (destType == LOCAL_GL_UNSIGNED_BYTE ||
            destType == LOCAL_GL_UNSIGNED_INT_8_8_8_8_REV) {
          fallback = false;
        }
        break;
      }
    }
  }

  if (fallback) {
    *out_readFormat = LOCAL_GL_RGBA;
    *out_readType = LOCAL_GL_UNSIGNED_BYTE;
    return false;
  } else {
    *out_readFormat = destFormat;
    *out_readType = destType;
    return true;
  }
}

static int CalcRowStride(int width, int pixelSize, int alignment) {
  MOZ_ASSERT(alignment);

  int rowStride = width * pixelSize;
  if (rowStride % alignment) {  // Extra at the end of the line?
    int alignmentCount = rowStride / alignment;
    rowStride = (alignmentCount + 1) * alignment;
  }
  return rowStride;
}

static int GuessAlignment(int width, int pixelSize, int rowStride) {
  int alignment = 8;  // Max GLES allows.
  while (CalcRowStride(width, pixelSize, alignment) != rowStride) {
    alignment /= 2;
    if (!alignment) {
      NS_WARNING("Bad alignment for GLES. Will use temp surf for readback.");
      return 0;
    }
  }
  return alignment;
}

bool ReadPixelsIntoBuffer(GLContext* gl, uint8_t* aData, int32_t aStride,
                          const IntSize& aSize, SurfaceFormat aFormat) {
  if (!gl->MakeCurrent() || !aData || aSize.IsEmpty()) {
    return false;
  }

  bool hasAlpha =
      aFormat == SurfaceFormat::B8G8R8A8 || aFormat == SurfaceFormat::R8G8B8A8;

  int destPixelSize;
  GLenum destFormat;
  GLenum destType;

  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      // Needs host (little) endian ARGB.
      destFormat = LOCAL_GL_BGRA;
      destType = LOCAL_GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
    case SurfaceFormat::R8G8B8A8:
    case SurfaceFormat::R8G8B8X8:
      // Needs host (little) endian ABGR.
      destFormat = LOCAL_GL_RGBA;
      destType = LOCAL_GL_UNSIGNED_BYTE;
      break;
    case SurfaceFormat::R5G6B5_UINT16:
      destFormat = LOCAL_GL_RGB;
      destType = LOCAL_GL_UNSIGNED_SHORT_5_6_5_REV;
      break;
    default:
      MOZ_CRASH("GFX: Bad format, read pixels.");
  }
  destPixelSize = BytesPerPixel(aFormat);

  MOZ_ASSERT(aSize.width * destPixelSize <= aStride);

  GLenum readFormat = destFormat;
  GLenum readType = destType;
  bool needsTempSurf =
      !GetActualReadFormats(gl, destFormat, destType, &readFormat, &readType);

  RefPtr<DataSourceSurface> tempSurf;
  Maybe<DataSourceSurface::ScopedMap> tempMap;
  uint8_t* data = aData;
  SurfaceFormat readFormatGFX;

  int readAlignment = GuessAlignment(aSize.width, destPixelSize, aStride);
  if (!readAlignment) {
    needsTempSurf = true;
  }
  if (needsTempSurf) {
    if (GLContext::ShouldSpew()) {
      NS_WARNING(
          "Needing intermediary surface for ReadPixels. This will be slow!");
    }

    switch (readFormat) {
      case LOCAL_GL_RGBA: {
        readFormatGFX =
            hasAlpha ? SurfaceFormat::R8G8B8A8 : SurfaceFormat::R8G8B8X8;
        break;
      }
      case LOCAL_GL_BGRA: {
        readFormatGFX =
            hasAlpha ? SurfaceFormat::B8G8R8A8 : SurfaceFormat::B8G8R8X8;
        break;
      }
      case LOCAL_GL_RGB: {
        MOZ_ASSERT(destPixelSize == 2);
        MOZ_ASSERT(readType == LOCAL_GL_UNSIGNED_SHORT_5_6_5_REV);
        readFormatGFX = SurfaceFormat::R5G6B5_UINT16;
        break;
      }
      default: {
        MOZ_CRASH("GFX: Bad read format, read format.");
      }
    }

    switch (readType) {
      case LOCAL_GL_UNSIGNED_BYTE: {
        MOZ_ASSERT(readFormat == LOCAL_GL_RGBA);
        readAlignment = 1;
        break;
      }
      case LOCAL_GL_UNSIGNED_INT_8_8_8_8_REV: {
        MOZ_ASSERT(readFormat == LOCAL_GL_BGRA);
        readAlignment = 4;
        break;
      }
      case LOCAL_GL_UNSIGNED_SHORT_5_6_5_REV: {
        MOZ_ASSERT(readFormat == LOCAL_GL_RGB);
        readAlignment = 2;
        break;
      }
      default: {
        MOZ_CRASH("GFX: Bad read type, read type.");
      }
    }

    int32_t stride = aSize.width * BytesPerPixel(readFormatGFX);
    tempSurf = Factory::CreateDataSourceSurfaceWithStride(aSize, readFormatGFX,
                                                          stride);
    if (NS_WARN_IF(!tempSurf)) {
      return false;
    }

    tempMap.emplace(tempSurf, DataSourceSurface::READ_WRITE);
    if (NS_WARN_IF(!tempMap->IsMapped())) {
      return false;
    }

    data = tempMap->GetData();
  }

  MOZ_ASSERT(readAlignment);
  MOZ_ASSERT(reinterpret_cast<uintptr_t>(data) % readAlignment == 0);

  GLsizei width = aSize.width;
  GLsizei height = aSize.height;

  {
    ScopedPackState safePackState(gl);
    gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, readAlignment);

    GLContext::LocalErrorScope errorScope(*gl);
    gl->fReadPixels(0, 0, width, height, readFormat, readType, data);
    if (errorScope.GetError()) {
      return false;
    }
  }

  if (tempMap) {
    return SwizzleData(tempMap->GetData(), tempMap->GetStride(), readFormatGFX,
                       aData, aStride, aFormat, aSize);
  }
  return true;
}

bool ReadPixelsIntoDataSurface(GLContext* gl, DataSourceSurface* dest) {
  if (!dest || !gl->MakeCurrent()) {
    return false;
  }
  DataSourceSurface::ScopedMap map(dest, DataSourceSurface::WRITE);
  if (!map.IsMapped()) {
    return false;
  }
  return ReadPixelsIntoBuffer(gl, map.GetData(), map.GetStride(),
                              dest->GetSize(), dest->GetFormat());
}

already_AddRefed<DataSourceSurface> ReadBackSurface(GLContext* gl,
                                                    GLuint aTexture,
                                                    bool aYInvert,
                                                    SurfaceFormat aFormat) {
  gl->MakeCurrent();
  gl->fActiveTexture(LOCAL_GL_TEXTURE0);
  gl->fBindTexture(LOCAL_GL_TEXTURE_2D, aTexture);

  IntSize size;
  gl->fGetTexLevelParameteriv(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_TEXTURE_WIDTH,
                              &size.width);
  gl->fGetTexLevelParameteriv(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_TEXTURE_HEIGHT,
                              &size.height);

  const auto dstFormat = SurfaceFormat::B8G8R8A8;
  auto stride = GetAlignedStride<4>(size.width, BytesPerPixel(dstFormat));
  if (stride.isNothing()) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> surf = Factory::CreateDataSourceSurfaceWithStride(
      size, dstFormat, stride.value());

  if (NS_WARN_IF(!surf)) {
    return nullptr;
  }

  uint32_t currentPackAlignment = 0;
  gl->fGetIntegerv(LOCAL_GL_PACK_ALIGNMENT, (GLint*)&currentPackAlignment);
  if (currentPackAlignment != 4) {
    gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, 4);
  }

  DataSourceSurface::ScopedMap map(surf, DataSourceSurface::READ_WRITE);
  if (!map.IsMapped()) {
    return nullptr;
  }

  gl->fGetTexImage(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA,
                   LOCAL_GL_UNSIGNED_BYTE, map.GetData());

  if (currentPackAlignment != 4) {
    gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, currentPackAlignment);
  }

  if (aYInvert) {
    if (!SwizzleYFlipData(map.GetData(), map.GetStride(), aFormat,
                          map.GetData(), map.GetStride(), dstFormat, size)) {
      MOZ_ASSERT_UNREACHABLE("Swizzle not supported?");
      return nullptr;
    }
  } else if (aFormat != dstFormat) {
    if (!SwizzleData(map.GetData(), map.GetStride(), aFormat, map.GetData(),
                     map.GetStride(), dstFormat, size)) {
      MOZ_ASSERT_UNREACHABLE("Swizzle not supported?");
      return nullptr;
    }
  }

  return surf.forget();
}

#define CLEANUP_IF_GLERROR_OCCURRED(x) \
  if (DidGLErrorOccur(x)) {            \
    return false;                      \
  }

already_AddRefed<DataSourceSurface> GLReadTexImageHelper::ReadTexImage(
    GLuint aTextureId, GLenum aTextureTarget, const gfx::IntSize& aSize,
    const gfx::Matrix4x4& aTexMatrix,
    /* ShaderConfigOGL.mFeature */ int aConfig, bool aYInvert) {
  /* Allocate resulting image surface */
  int32_t stride = aSize.width * BytesPerPixel(SurfaceFormat::R8G8B8A8);
  RefPtr<DataSourceSurface> isurf = Factory::CreateDataSourceSurfaceWithStride(
      aSize, SurfaceFormat::R8G8B8A8, stride);
  if (NS_WARN_IF(!isurf)) {
    return nullptr;
  }

  if (!ReadTexImage(isurf, aTextureId, aTextureTarget, aSize, aTexMatrix,
                    aConfig, aYInvert)) {
    return nullptr;
  }

  return isurf.forget();
}

bool GLReadTexImageHelper::ReadTexImage(
    DataSourceSurface* aDest, GLuint aTextureId, GLenum aTextureTarget,
    const gfx::IntSize& aSize, const gfx::Matrix4x4& aTexMatrix,
    /* ShaderConfigOGL.mFeature */ int aConfig, bool aYInvert) {
  MOZ_ASSERT(aTextureTarget == LOCAL_GL_TEXTURE_2D ||
             aTextureTarget == LOCAL_GL_TEXTURE_EXTERNAL ||
             aTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE_ARB);

  if (!mGL->MakeCurrent()) {
    return false;
  }

  GLint oldrb, oldfb, oldprog, oldTexUnit, oldTex;
  GLuint rb = 0, fb = 0;

  do {
    mGL->fGetIntegerv(LOCAL_GL_RENDERBUFFER_BINDING, &oldrb);
    mGL->fGetIntegerv(LOCAL_GL_FRAMEBUFFER_BINDING, &oldfb);
    mGL->fGetIntegerv(LOCAL_GL_CURRENT_PROGRAM, &oldprog);
    mGL->fGetIntegerv(LOCAL_GL_ACTIVE_TEXTURE, &oldTexUnit);
    mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
    switch (aTextureTarget) {
      case LOCAL_GL_TEXTURE_2D:
        mGL->fGetIntegerv(LOCAL_GL_TEXTURE_BINDING_2D, &oldTex);
        break;
      case LOCAL_GL_TEXTURE_EXTERNAL:
        mGL->fGetIntegerv(LOCAL_GL_TEXTURE_BINDING_EXTERNAL, &oldTex);
        break;
      case LOCAL_GL_TEXTURE_RECTANGLE:
        mGL->fGetIntegerv(LOCAL_GL_TEXTURE_BINDING_RECTANGLE, &oldTex);
        break;
      default: /* Already checked above */
        break;
    }

    auto restore = MakeScopeExit([&] {
      mGL->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, oldrb);
      mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, oldfb);
      mGL->fUseProgram(oldprog);
      mGL->fDeleteRenderbuffers(1, &rb);
      mGL->fDeleteFramebuffers(1, &fb);
      if (aTextureId) {
        mGL->fBindTexture(aTextureTarget, oldTex);
      }
      if (oldTexUnit != LOCAL_GL_TEXTURE0) {
        mGL->fActiveTexture(oldTexUnit);
      }
    });

    ScopedGLState scopedScissorTestState(mGL, LOCAL_GL_SCISSOR_TEST, false);
    ScopedGLState scopedBlendState(mGL, LOCAL_GL_BLEND, false);
    ScopedViewportRect scopedViewportRect(mGL, 0, 0, aSize.width, aSize.height);

    /* Setup renderbuffer */
    mGL->fGenRenderbuffers(1, &rb);
    mGL->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, rb);

    GLenum rbInternalFormat =
        mGL->IsGLES() ? (mGL->IsExtensionSupported(GLContext::OES_rgb8_rgba8)
                             ? LOCAL_GL_RGBA8
                             : LOCAL_GL_RGBA4)
                      : LOCAL_GL_RGBA;
    mGL->fRenderbufferStorage(LOCAL_GL_RENDERBUFFER, rbInternalFormat,
                              aSize.width, aSize.height);
    CLEANUP_IF_GLERROR_OCCURRED("when binding and creating renderbuffer");

    /* Setup framebuffer */
    mGL->fGenFramebuffers(1, &fb);
    mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fb);
    mGL->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER,
                                  LOCAL_GL_COLOR_ATTACHMENT0,
                                  LOCAL_GL_RENDERBUFFER, rb);
    CLEANUP_IF_GLERROR_OCCURRED("when binding and creating framebuffer");

    MOZ_ASSERT(mGL->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER) ==
               LOCAL_GL_FRAMEBUFFER_COMPLETE);

    /* Setup vertex and fragment shader */
    GLuint program = TextureImageProgramFor(aTextureTarget, aConfig);
    MOZ_ASSERT(program);

    mGL->fUseProgram(program);
    CLEANUP_IF_GLERROR_OCCURRED("when using program");
    mGL->fUniform1i(mGL->fGetUniformLocation(program, "uTexture"), 0);
    CLEANUP_IF_GLERROR_OCCURRED("when setting uniform uTexture");
    mGL->fUniformMatrix4fv(mGL->fGetUniformLocation(program, "uTexMatrix"), 1,
                           false, aTexMatrix.components);
    CLEANUP_IF_GLERROR_OCCURRED("when setting uniform uTexMatrix");

    /* Setup quad geometry */
    mGL->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

    float w = (aTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE)
                  ? (float)aSize.width
                  : 1.0f;
    float h = (aTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE)
                  ? (float)aSize.height
                  : 1.0f;

    const float vertexArray[4 * 2] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                      -1.0f, 1.0f,  1.0f, 1.0f};
    ScopedVertexAttribPointer autoAttrib0(mGL, 0, 2, LOCAL_GL_FLOAT,
                                          LOCAL_GL_FALSE, 0, 0, vertexArray);

    const float u0 = 0.0f;
    const float u1 = w;
    const float v0 = aYInvert ? h : 0.0f;
    const float v1 = aYInvert ? 0.0f : h;
    const float texCoordArray[8] = {u0, v0, u1, v0, u0, v1, u1, v1};
    ScopedVertexAttribPointer autoAttrib1(mGL, 1, 2, LOCAL_GL_FLOAT,
                                          LOCAL_GL_FALSE, 0, 0, texCoordArray);

    /* Bind the texture */
    if (aTextureId) {
      mGL->fBindTexture(aTextureTarget, aTextureId);
      CLEANUP_IF_GLERROR_OCCURRED("when binding texture");
    }

    mGL->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 4);
    CLEANUP_IF_GLERROR_OCCURRED("when drawing texture");

    /* Read-back draw results */
    if (!ReadPixelsIntoDataSurface(mGL, aDest)) {
      return false;
    }
    CLEANUP_IF_GLERROR_OCCURRED("when reading pixels into surface");
  } while (false);

  return true;
}

#undef CLEANUP_IF_GLERROR_OCCURRED

}  // namespace gl
}  // namespace mozilla
