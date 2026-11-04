/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLProgram.h"

#include "GLContext.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLFormats.h"
#include "WebGLShader.h"
#include "WebGLShaderValidator.h"
#include "WebGLTransformFeedback.h"
#include "WebGLValidateStrings.h"
#include "WebGLVertexArray.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/WebGL2RenderingContextBinding.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/gfx/Logging.h"
#include "nsPrintfCString.h"

namespace mozilla {

static bool IsShadowSampler(const GLenum elemType) {
  switch (elemType) {
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
      return true;
    default:
      return false;
  }
}

static Maybe<webgl::TextureBaseType> SamplerBaseType(const GLenum elemType) {
  switch (elemType) {
    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
      return Some(webgl::TextureBaseType::Float);

    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
      return Some(webgl::TextureBaseType::Int);

    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
      return Some(webgl::TextureBaseType::UInt);

    default:
      return {};
  }
}

//////////

static webgl::TextureBaseType FragOutputBaseType(const GLenum type) {
  switch (type) {
    case LOCAL_GL_FLOAT:
    case LOCAL_GL_FLOAT_VEC2:
    case LOCAL_GL_FLOAT_VEC3:
    case LOCAL_GL_FLOAT_VEC4:
      return webgl::TextureBaseType::Float;

    case LOCAL_GL_INT:
    case LOCAL_GL_INT_VEC2:
    case LOCAL_GL_INT_VEC3:
    case LOCAL_GL_INT_VEC4:
      return webgl::TextureBaseType::Int;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_VEC2:
    case LOCAL_GL_UNSIGNED_INT_VEC3:
    case LOCAL_GL_UNSIGNED_INT_VEC4:
      return webgl::TextureBaseType::UInt;

    default:
      break;
  }

  const auto& str = EnumString(type);
  gfxCriticalError() << "Unhandled enum for FragOutputBaseType: "
                     << str.c_str();
  return webgl::TextureBaseType::Float;
}

// -----------------------------------------

namespace webgl {

void UniformAs1fv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform1fv(location, count, static_cast<const float*>(any));
}
void UniformAs2fv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform2fv(location, count, static_cast<const float*>(any));
}
void UniformAs3fv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform3fv(location, count, static_cast<const float*>(any));
}
void UniformAs4fv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform4fv(location, count, static_cast<const float*>(any));
}

void UniformAs1iv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform1iv(location, count, static_cast<const int32_t*>(any));
}
void UniformAs2iv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform2iv(location, count, static_cast<const int32_t*>(any));
}
void UniformAs3iv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform3iv(location, count, static_cast<const int32_t*>(any));
}
void UniformAs4iv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any) {
  gl.fUniform4iv(location, count, static_cast<const int32_t*>(any));
}

void UniformAs1uiv(gl::GLContext& gl, GLint location, GLsizei count,
                   bool transpose, const void* any) {
  gl.fUniform1uiv(location, count, static_cast<const uint32_t*>(any));
}
void UniformAs2uiv(gl::GLContext& gl, GLint location, GLsizei count,
                   bool transpose, const void* any) {
  gl.fUniform2uiv(location, count, static_cast<const uint32_t*>(any));
}
void UniformAs3uiv(gl::GLContext& gl, GLint location, GLsizei count,
                   bool transpose, const void* any) {
  gl.fUniform3uiv(location, count, static_cast<const uint32_t*>(any));
}
void UniformAs4uiv(gl::GLContext& gl, GLint location, GLsizei count,
                   bool transpose, const void* any) {
  gl.fUniform4uiv(location, count, static_cast<const uint32_t*>(any));
}

void UniformAsMatrix2x2fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix2fv(location, count, transpose,
                       static_cast<const float*>(any));
}
void UniformAsMatrix2x3fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix2x3fv(location, count, transpose,
                         static_cast<const float*>(any));
}
void UniformAsMatrix2x4fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix2x4fv(location, count, transpose,
                         static_cast<const float*>(any));
}

void UniformAsMatrix3x2fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix3x2fv(location, count, transpose,
                         static_cast<const float*>(any));
}
void UniformAsMatrix3x3fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix3fv(location, count, transpose,
                       static_cast<const float*>(any));
}
void UniformAsMatrix3x4fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix3x4fv(location, count, transpose,
                         static_cast<const float*>(any));
}

void UniformAsMatrix4x2fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix4x2fv(location, count, transpose,
                         static_cast<const float*>(any));
}
void UniformAsMatrix4x3fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix4x3fv(location, count, transpose,
                         static_cast<const float*>(any));
}
void UniformAsMatrix4x4fv(gl::GLContext& gl, GLint location, GLsizei count,
                          bool transpose, const void* any) {
  gl.fUniformMatrix4fv(location, count, transpose,
                       static_cast<const float*>(any));
}

// -

static bool EndsWith(const std::string& str, const std::string& needle) {
  if (str.length() < needle.length()) return false;
  return str.compare(str.length() - needle.length(), needle.length(), needle) ==
         0;
}

webgl::ActiveUniformValidationInfo webgl::ActiveUniformValidationInfo::Make(
    const webgl::ActiveUniformInfo& info) {
  auto ret = webgl::ActiveUniformValidationInfo{info};
  ret.isArray = EndsWith(info.name, "[0]");

  switch (info.elemType) {
    case LOCAL_GL_FLOAT:
      ret.channelsPerElem = 1;
      ret.pfn = &UniformAs1fv;
      break;
    case LOCAL_GL_FLOAT_VEC2:
      ret.channelsPerElem = 2;
      ret.pfn = &UniformAs2fv;
      break;
    case LOCAL_GL_FLOAT_VEC3:
      ret.channelsPerElem = 3;
      ret.pfn = &UniformAs3fv;
      break;
    case LOCAL_GL_FLOAT_VEC4:
      ret.channelsPerElem = 4;
      ret.pfn = &UniformAs4fv;
      break;

    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_BOOL:
    case LOCAL_GL_INT:
      ret.channelsPerElem = 1;
      ret.pfn = &UniformAs1iv;
      break;
    case LOCAL_GL_BOOL_VEC2:
    case LOCAL_GL_INT_VEC2:
      ret.channelsPerElem = 2;
      ret.pfn = &UniformAs2iv;
      break;
    case LOCAL_GL_BOOL_VEC3:
    case LOCAL_GL_INT_VEC3:
      ret.channelsPerElem = 3;
      ret.pfn = &UniformAs3iv;
      break;
    case LOCAL_GL_BOOL_VEC4:
    case LOCAL_GL_INT_VEC4:
      ret.channelsPerElem = 4;
      ret.pfn = &UniformAs4iv;
      break;

    case LOCAL_GL_UNSIGNED_INT:
      ret.channelsPerElem = 1;
      ret.pfn = &UniformAs1uiv;
      break;
    case LOCAL_GL_UNSIGNED_INT_VEC2:
      ret.channelsPerElem = 2;
      ret.pfn = &UniformAs2uiv;
      break;
    case LOCAL_GL_UNSIGNED_INT_VEC3:
      ret.channelsPerElem = 3;
      ret.pfn = &UniformAs3uiv;
      break;
    case LOCAL_GL_UNSIGNED_INT_VEC4:
      ret.channelsPerElem = 4;
      ret.pfn = &UniformAs4uiv;
      break;

      // -

    case LOCAL_GL_FLOAT_MAT2:
      ret.channelsPerElem = 2 * 2;
      ret.pfn = &UniformAsMatrix2x2fv;
      break;
    case LOCAL_GL_FLOAT_MAT2x3:
      ret.channelsPerElem = 2 * 3;
      ret.pfn = &UniformAsMatrix2x3fv;
      break;
    case LOCAL_GL_FLOAT_MAT2x4:
      ret.channelsPerElem = 2 * 4;
      ret.pfn = &UniformAsMatrix2x4fv;
      break;

    case LOCAL_GL_FLOAT_MAT3x2:
      ret.channelsPerElem = 3 * 2;
      ret.pfn = &UniformAsMatrix3x2fv;
      break;
    case LOCAL_GL_FLOAT_MAT3:
      ret.channelsPerElem = 3 * 3;
      ret.pfn = &UniformAsMatrix3x3fv;
      break;
    case LOCAL_GL_FLOAT_MAT3x4:
      ret.channelsPerElem = 3 * 4;
      ret.pfn = &UniformAsMatrix3x4fv;
      break;

    case LOCAL_GL_FLOAT_MAT4x2:
      ret.channelsPerElem = 4 * 2;
      ret.pfn = &UniformAsMatrix4x2fv;
      break;
    case LOCAL_GL_FLOAT_MAT4x3:
      ret.channelsPerElem = 4 * 3;
      ret.pfn = &UniformAsMatrix4x3fv;
      break;
    case LOCAL_GL_FLOAT_MAT4:
      ret.channelsPerElem = 4 * 4;
      ret.pfn = &UniformAsMatrix4x4fv;
      break;

    default:
      gfxCriticalError() << "Bad `elemType`: " << EnumString(info.elemType);
      MOZ_CRASH("`elemType`");
  }
  return ret;
}

}  // namespace webgl

// -------------------------

// #define DUMP_SHADERVAR_MAPPINGS

RefPtr<const webgl::LinkedProgramInfo> QueryProgramInfo(WebGLProgram* prog,
                                                        gl::GLContext* gl) {
  WebGLContext* const webgl = prog->mContext;

  RefPtr<webgl::LinkedProgramInfo> info(new webgl::LinkedProgramInfo(prog));

  // Frag outputs

  {
    const auto& fragShader = prog->FragShader();
    const auto& compileResults = fragShader->CompileResults();
    const auto version = compileResults->mShaderVersion;

    const auto fnAddInfo = [&](const webgl::FragOutputInfo& x) {
      info->hasOutput[x.loc] = true;
      info->fragOutputs.insert({x.loc, x});
    };

    if (version == 300) {
      for (const auto& cur : compileResults->mOutputVariables) {
        auto loc = cur.location;
        if (loc == -1) loc = 0;

        const auto info =
            webgl::FragOutputInfo{uint8_t(loc), cur.name, cur.mappedName,
                                  FragOutputBaseType(cur.type)};
        if (!cur.isArray()) {
          fnAddInfo(info);
          continue;
        }
        MOZ_ASSERT(cur.arraySizes.size() == 1);
        for (uint32_t i = 0; i < cur.arraySizes[0]; ++i) {
          const auto indexStr = std::string("[") + std::to_string(i) + "]";

          const auto userName = info.userName + indexStr;
          const auto mappedName = info.mappedName + indexStr;

          const auto indexedInfo = webgl::FragOutputInfo{
              uint8_t(info.loc + i), userName, mappedName, info.baseType};
          fnAddInfo(indexedInfo);
        }
      }
    } else {
      // ANGLE's translator doesn't tell us about non-user frag outputs. :(

      const auto& translatedSource = compileResults->mObjectCode;
      uint32_t drawBuffers = 1;
      if (translatedSource.find("(gl_FragData[1]") != std::string::npos ||
          translatedSource.find("(webgl_FragData[1]") != std::string::npos) {
        // The matching with the leading '(' prevents cleverly-named user vars
        // breaking this. Since ANGLE initializes all outputs, if this is an MRT
        // shader, FragData[1] will be present. FragData[0] is valid for non-MRT
        // shaders.
        drawBuffers = webgl->GLMaxDrawBuffers();
      } else if (translatedSource.find("(gl_FragColor") == std::string::npos &&
                 translatedSource.find("(webgl_FragColor") ==
                     std::string::npos &&
                 translatedSource.find("(gl_FragData") == std::string::npos &&
                 translatedSource.find("(webgl_FragData") ==
                     std::string::npos) {
        // We have to support no-color-output shaders?
        drawBuffers = 0;
      }

      for (uint32_t i = 0; i < drawBuffers; ++i) {
        const auto name = std::string("gl_FragData[") + std::to_string(i) + "]";
        const auto info = webgl::FragOutputInfo{uint8_t(i), name, name,
                                                webgl::TextureBaseType::Float};
        fnAddInfo(info);
      }
    }
  }

  const auto& vertShader = prog->VertShader();
  const auto& vertCompileResults = vertShader->CompileResults();
  const auto numViews = vertCompileResults->mVertexShaderNumViews;
  if (numViews != -1) {
    info->zLayerCount = AssertedCast<uint8_t>(numViews);
  }

  // -

  auto& nameMap = info->nameMap;

  const auto fnAccum = [&](WebGLShader& shader) {
    const auto& compRes = shader.CompileResults();
    for (const auto& pair : compRes->mNameMap) {
      nameMap.insert(pair);
    }
  };
  fnAccum(*prog->VertShader());
  fnAccum(*prog->FragShader());

  // -

  std::unordered_map<std::string, std::string> nameUnmap;
  for (const auto& pair : nameMap) {
    nameUnmap.insert({pair.second, pair.first});
  }

  info->active =
      GetLinkActiveInfo(*gl, prog->mGLName, webgl->IsWebGL2(), nameUnmap);

  // -

  for (const auto& attrib : info->active.activeAttribs) {
    if (attrib.location == 0) {
      info->attrib0Active = true;
      break;
    }
  }

  info->webgl_gl_VertexID_Offset =
      gl->fGetUniformLocation(prog->mGLName, "webgl_gl_VertexID_Offset");

  // -

  for (const auto& uniform : info->active.activeUniforms) {
    const auto& elemType = uniform.elemType;
    webgl::SamplerUniformInfo* samplerInfo = nullptr;
    const auto baseType = SamplerBaseType(elemType);
    if (baseType) {
      const bool isShadowSampler = IsShadowSampler(elemType);

      auto* texList = &webgl->mBound2DTextures;

      switch (elemType) {
        case LOCAL_GL_SAMPLER_2D:
        case LOCAL_GL_SAMPLER_2D_SHADOW:
        case LOCAL_GL_INT_SAMPLER_2D:
        case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
          break;

        case LOCAL_GL_SAMPLER_CUBE:
        case LOCAL_GL_SAMPLER_CUBE_SHADOW:
        case LOCAL_GL_INT_SAMPLER_CUBE:
        case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
          texList = &webgl->mBoundCubeMapTextures;
          break;

        case LOCAL_GL_SAMPLER_3D:
        case LOCAL_GL_INT_SAMPLER_3D:
        case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
          texList = &webgl->mBound3DTextures;
          break;

        case LOCAL_GL_SAMPLER_2D_ARRAY:
        case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
        case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
        case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
          texList = &webgl->mBound2DArrayTextures;
          break;
      }

      auto curInfo = std::unique_ptr<webgl::SamplerUniformInfo>(
          new webgl::SamplerUniformInfo{*texList, *baseType, isShadowSampler});
      MOZ_RELEASE_ASSERT(curInfo->texUnits.resize(uniform.elemCount));
      samplerInfo = curInfo.get();
      info->samplerUniforms.push_back(std::move(curInfo));
    }

    const auto valInfo = webgl::ActiveUniformValidationInfo::Make(uniform);

    for (const auto& pair : uniform.locByIndex) {
      info->locationMap.insert(
          {pair.second, {valInfo, pair.first, samplerInfo}});
    }
  }

  // -

  {
    const auto& activeBlocks = info->active.activeUniformBlocks;
    info->uniformBlocks.reserve(activeBlocks.size());
    for (const auto& cur : activeBlocks) {
      const auto curInfo = webgl::UniformBlockInfo{
          cur, &webgl->mIndexedUniformBufferBindings[0]};
      info->uniformBlocks.push_back(curInfo);
    }
  }

  return info;
}

////////////////////////////////////////////////////////////////////////////////

webgl::LinkedProgramInfo::LinkedProgramInfo(WebGLProgram* prog)
    : prog(prog),
      transformFeedbackBufferMode(prog->mNextLink_TransformFeedbackBufferMode) {
}

webgl::LinkedProgramInfo::~LinkedProgramInfo() = default;

webgl::AttribBaseType webgl::ToAttribBaseType(const GLenum elemType) {
  switch (elemType) {
    case LOCAL_GL_BOOL:
    case LOCAL_GL_BOOL_VEC2:
    case LOCAL_GL_BOOL_VEC3:
    case LOCAL_GL_BOOL_VEC4:
      return webgl::AttribBaseType::Boolean;

    case LOCAL_GL_FLOAT:
    case LOCAL_GL_FLOAT_VEC2:
    case LOCAL_GL_FLOAT_VEC3:
    case LOCAL_GL_FLOAT_VEC4:
    case LOCAL_GL_FLOAT_MAT2:
    case LOCAL_GL_FLOAT_MAT2x3:
    case LOCAL_GL_FLOAT_MAT3x2:
    case LOCAL_GL_FLOAT_MAT2x4:
    case LOCAL_GL_FLOAT_MAT4x2:
    case LOCAL_GL_FLOAT_MAT3:
    case LOCAL_GL_FLOAT_MAT3x4:
    case LOCAL_GL_FLOAT_MAT4x3:
    case LOCAL_GL_FLOAT_MAT4:
      return webgl::AttribBaseType::Float;

    case LOCAL_GL_INT:
    case LOCAL_GL_INT_VEC2:
    case LOCAL_GL_INT_VEC3:
    case LOCAL_GL_INT_VEC4:
    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
      return webgl::AttribBaseType::Int;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_VEC2:
    case LOCAL_GL_UNSIGNED_INT_VEC3:
    case LOCAL_GL_UNSIGNED_INT_VEC4:
      return webgl::AttribBaseType::Uint;

    default:
      gfxCriticalError() << "Bad `elemType`: " << EnumString(elemType);
      MOZ_CRASH("`elemType`");
  }
}

const char* webgl::ToString(const webgl::AttribBaseType x) {
  switch (x) {
    case webgl::AttribBaseType::Float:
      return "FLOAT";
    case webgl::AttribBaseType::Int:
      return "INT";
    case webgl::AttribBaseType::Uint:
      return "UINT";
    case webgl::AttribBaseType::Boolean:
      return "BOOL";
  }
  MOZ_CRASH("pacify gcc6 warning");
}

const char* webgl::ToString(const webgl::UniformBaseType x) {
  switch (x) {
    case webgl::UniformBaseType::Float:
      return "FLOAT";
    case webgl::UniformBaseType::Int:
      return "INT";
    case webgl::UniformBaseType::Uint:
      return "UINT";
  }
  MOZ_CRASH("pacify gcc6 warning");
}

static uint8_t NumUsedLocationsByElemType(GLenum elemType);

const webgl::CachedDrawFetchLimits*
webgl::LinkedProgramInfo::GetDrawFetchLimits() const {
  const auto& webgl = prog->mContext;
  const auto& vao = webgl->mBoundVertexArray;

  {
    // We have to ensure that every enabled attrib array (not just the active
    // ones) has a non-null buffer.
    const auto badIndex = vao->GetAttribIsArrayWithNullBuffer();
    if (badIndex) {
      webgl->ErrorInvalidOperation(
          "Vertex attrib array %u is enabled but"
          " has no buffer bound.",
          *badIndex);
      return nullptr;
    }
  }

  const auto& activeAttribs = active.activeAttribs;

  webgl::CachedDrawFetchLimits fetchLimits;
  fetchLimits.usedBuffers =
      std::move(mScratchFetchLimits.usedBuffers);  // Avoid realloc.
  fetchLimits.usedBuffers.clear();
  fetchLimits.usedBuffers.reserve(activeAttribs.size());

  bool hasActiveAttrib = false;
  bool hasActiveDivisor0 = false;

  for (const auto& progAttrib : activeAttribs) {
    const auto& loc = progAttrib.location;
    if (loc == -1) continue;
    hasActiveAttrib |= true;

    const auto numUsedLocs = NumUsedLocationsByElemType(progAttrib.elemType);
    for (uint8_t locOffset = 0; locOffset < numUsedLocs; locOffset++) {
      const auto usedLoc = static_cast<uint32_t>(loc) + locOffset;

      const auto& binding = vao->AttribBinding(usedLoc);
      const auto& buffer = binding.buffer;
      const auto& layout = binding.layout;
      hasActiveDivisor0 |= (layout.divisor == 0);

      webgl::AttribBaseType attribDataBaseType;
      if (layout.isArray) {
        MOZ_ASSERT(buffer);
        fetchLimits.usedBuffers.push_back({buffer.get(), usedLoc});

        attribDataBaseType = layout.baseType;

        const auto availBytes = buffer->ByteLength();
        const auto availElems = AvailGroups(availBytes, layout.byteOffset,
                                            layout.byteSize, layout.byteStride);
        if (layout.divisor) {
          const auto availInstances =
              CheckedInt<uint64_t>(availElems) * layout.divisor;
          if (availInstances.isValid()) {
            fetchLimits.maxInstances =
                std::min(fetchLimits.maxInstances, availInstances.value());
          }  // If not valid, it overflowed too large, so we're super safe.
        } else {
          fetchLimits.maxVerts = std::min(fetchLimits.maxVerts, availElems);
        }
      } else {
        attribDataBaseType = webgl->mGenericVertexAttribTypes[usedLoc];
      }

      const auto& progBaseType = progAttrib.baseType;
      if ((attribDataBaseType != progBaseType) &&
          (progBaseType != webgl::AttribBaseType::Boolean)) {
        const auto& dataType = ToString(attribDataBaseType);
        const auto& progType = ToString(progBaseType);
        webgl->ErrorInvalidOperation(
            "Vertex attrib %u requires data of type %s,"
            " but is being supplied with type %s.",
            usedLoc, progType, dataType);
        return nullptr;
      }
    }
  }

  if (!webgl->IsWebGL2() && hasActiveAttrib && !hasActiveDivisor0) {
    webgl->ErrorInvalidOperation(
        "One active vertex attrib (if any are active)"
        " must have a divisor of 0.");
    return nullptr;
  }

  // -

  mScratchFetchLimits = std::move(fetchLimits);
  return &mScratchFetchLimits;
}

////////////////////////////////////////////////////////////////////////////////
// WebGLProgram

WebGLProgram::WebGLProgram(WebGLContext* webgl)
    : WebGLContextBoundObject(webgl),
      mGLName(webgl->gl->fCreateProgram()),
      mNumActiveTFOs(0),
      mNextLink_TransformFeedbackBufferMode(LOCAL_GL_INTERLEAVED_ATTRIBS) {}

WebGLProgram::~WebGLProgram() {
  mVertShader = nullptr;
  mFragShader = nullptr;

  mMostRecentLinkInfo = nullptr;

  if (!mContext) return;
  mContext->gl->fDeleteProgram(mGLName);
}

////////////////////////////////////////////////////////////////////////////////
// GL funcs

void WebGLProgram::AttachShader(WebGLShader& shader) {
  RefPtr<WebGLShader>* shaderSlot = nullptr;
  switch (shader.mType) {
    case LOCAL_GL_VERTEX_SHADER:
      shaderSlot = &mVertShader;
      break;
    case LOCAL_GL_FRAGMENT_SHADER:
      shaderSlot = &mFragShader;
      break;
  }
  MOZ_ASSERT(shaderSlot);

  *shaderSlot = &shader;

  mContext->gl->fAttachShader(mGLName, shader.mGLName);
}

void WebGLProgram::BindAttribLocation(GLuint loc, const std::string& name) {
  const auto err = CheckGLSLVariableName(mContext->IsWebGL2(), name);
  if (err) {
    mContext->GenerateError(err->type, "%s", err->info.c_str());
    return;
  }

  if (loc >= mContext->MaxVertexAttribs()) {
    mContext->ErrorInvalidValue(
        "`location` must be less than"
        " MAX_VERTEX_ATTRIBS.");
    return;
  }

  if (name.starts_with("gl_")) {
    mContext->ErrorInvalidOperation(
        "Can't set the location of a"
        " name that starts with 'gl_'.");
    return;
  }

  auto res = mNextLink_BoundAttribLocs.insert({name, loc});

  const auto& wasInserted = res.second;
  if (!wasInserted) {
    const auto& itr = res.first;
    itr->second = loc;
  }
}

void WebGLProgram::DetachShader(const WebGLShader& shader) {
  RefPtr<WebGLShader>* shaderSlot = nullptr;
  switch (shader.mType) {
    case LOCAL_GL_VERTEX_SHADER:
      shaderSlot = &mVertShader;
      break;
    case LOCAL_GL_FRAGMENT_SHADER:
      shaderSlot = &mFragShader;
      break;
  }
  MOZ_ASSERT(shaderSlot);

  if (*shaderSlot != &shader) return;

  *shaderSlot = nullptr;

  mContext->gl->fDetachShader(mGLName, shader.mGLName);
}

void WebGLProgram::UniformBlockBinding(GLuint uniformBlockIndex,
                                       GLuint uniformBlockBinding) const {
  if (!IsLinked()) {
    mContext->ErrorInvalidOperation("`program` must be linked.");
    return;
  }

  auto& uniformBlocks = LinkInfo()->uniformBlocks;
  if (uniformBlockIndex >= uniformBlocks.size()) {
    mContext->ErrorInvalidValue("Index %u invalid.", uniformBlockIndex);
    return;
  }
  auto& uniformBlock = uniformBlocks[uniformBlockIndex];

  const auto& indexedBindings = mContext->mIndexedUniformBufferBindings;
  if (uniformBlockBinding >= indexedBindings.size()) {
    mContext->ErrorInvalidValue("Binding %u invalid.", uniformBlockBinding);
    return;
  }
  const auto& indexedBinding = indexedBindings[uniformBlockBinding];

  ////

  gl::GLContext* gl = mContext->GL();
  gl->fUniformBlockBinding(mGLName, uniformBlockIndex, uniformBlockBinding);

  ////

  uniformBlock.binding = &indexedBinding;
}

bool WebGLProgram::ValidateForLink() {
  const auto AppendCompileLog = [&](const WebGLShader* const shader) {
    if (!shader) {
      mLinkLog += " Missing shader.";
      return;
    }
    mLinkLog += "\nSHADER_INFO_LOG:\n";
    mLinkLog += shader->CompileLog();
  };

  if (!mVertShader || !mVertShader->IsCompiled()) {
    mLinkLog = "Must have a compiled vertex shader attached:";
    AppendCompileLog(mVertShader);
    return false;
  }
  const auto& vertInfo = *mVertShader->CompileResults();

  if (!mFragShader || !mFragShader->IsCompiled()) {
    mLinkLog = "Must have a compiled fragment shader attached:";
    AppendCompileLog(mFragShader);
    return false;
  }
  const auto& fragInfo = *mFragShader->CompileResults();

  nsCString errInfo;
  if (!fragInfo.CanLinkTo(vertInfo, &errInfo)) {
    mLinkLog = errInfo.get();
    return false;
  }

  const auto& gl = mContext->gl;

  if (gl->WorkAroundDriverBugs() && mContext->mIsMesa) {
    // Bug 1203135: Mesa crashes internally if we exceed the reported maximum
    // attribute count.
    if (mVertShader->NumAttributes() > mContext->MaxVertexAttribs()) {
      mLinkLog =
          "Number of attributes exceeds Mesa's reported max"
          " attribute count.";
      return false;
    }
  }

  return true;
}

void WebGLProgram::LinkProgram() {
  if (mNumActiveTFOs) {
    mContext->ErrorInvalidOperation(
        "Program is in-use by one or more active"
        " transform feedback objects.");
    return;
  }

  // as some of the validation changes program state

  mLinkLog = {};
  mMostRecentLinkInfo = nullptr;

  if (!ValidateForLink()) {
    mContext->GenerateWarning("%s", mLinkLog.c_str());
    return;
  }

  // Bind the attrib locations.
  // This can't be done trivially, because we have to deal with mapped attrib
  // names.
  for (const auto& pair : mNextLink_BoundAttribLocs) {
    const auto& name = pair.first;
    const auto& index = pair.second;

    mVertShader->BindAttribLocation(mGLName, name, index);
  }

  // Storage for transform feedback varyings before link.
  // (Work around for bug seen on nVidia drivers.)
  std::vector<std::string> scopedMappedTFVaryings;

  bool allowLink = true;
  if (mContext->IsWebGL2()) {
    mVertShader->MapTransformFeedbackVaryings(
        mNextLink_TransformFeedbackVaryings, &scopedMappedTFVaryings);

    std::vector<const char*> driverVaryings;
    driverVaryings.reserve(scopedMappedTFVaryings.size());
    for (const auto& cur : scopedMappedTFVaryings) {
      driverVaryings.push_back(cur.c_str());
    }

    // If the driver rejects the varyings (e.g. reserved gl_* names), the
    // program object keeps the previous link's pending transform-feedback
    // state. Instead, avoid trying to link with the stale state.
    gl::GLContext::LocalErrorScope errorScope(*mContext->gl);
    mContext->gl->fTransformFeedbackVaryings(
        mGLName, driverVaryings.size(), driverVaryings.data(),
        mNextLink_TransformFeedbackBufferMode);
    if (errorScope.GetError()) {
      allowLink = false;
      mLinkLog = "Driver rejected the requested transform feedback varyings.";
    }
  }

  if (allowLink) {
    LinkAndUpdate();

    if (mMostRecentLinkInfo) {
      std::string postLinkLog;
      if (ValidateAfterTentativeLink(&postLinkLog)) return;

      mMostRecentLinkInfo = nullptr;
      mLinkLog = std::move(postLinkLog);
    }
  }

  // Failed link.
  if (mContext->ShouldGenerateWarnings()) {
    // report shader/program infoLogs as warnings.
    // note that shader compilation errors can be deferred to linkProgram,
    // which is why we can't do anything in compileShader. In practice we could
    // report in compileShader the translation errors generated by ANGLE,
    // but it seems saner to keep a single way of obtaining shader infologs.
    if (!mLinkLog.empty()) {
      mContext->GenerateWarning(
          "Failed to link, leaving the following"
          " log:\n%s\n",
          mLinkLog.c_str());
    }
  }
}

static uint8_t NumUsedLocationsByElemType(GLenum elemType) {
  // GLES 3.0.4 p55

  switch (elemType) {
    case LOCAL_GL_FLOAT_MAT2:
    case LOCAL_GL_FLOAT_MAT2x3:
    case LOCAL_GL_FLOAT_MAT2x4:
      return 2;

    case LOCAL_GL_FLOAT_MAT3x2:
    case LOCAL_GL_FLOAT_MAT3:
    case LOCAL_GL_FLOAT_MAT3x4:
      return 3;

    case LOCAL_GL_FLOAT_MAT4x2:
    case LOCAL_GL_FLOAT_MAT4x3:
    case LOCAL_GL_FLOAT_MAT4:
      return 4;

    default:
      return 1;
  }
}

uint8_t ElemTypeComponents(const GLenum elemType) {
  switch (elemType) {
    case LOCAL_GL_BOOL:
    case LOCAL_GL_FLOAT:
    case LOCAL_GL_INT:
    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_SAMPLER_2D:
    case LOCAL_GL_SAMPLER_3D:
    case LOCAL_GL_SAMPLER_CUBE:
    case LOCAL_GL_SAMPLER_2D_SHADOW:
    case LOCAL_GL_SAMPLER_2D_ARRAY:
    case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
    case LOCAL_GL_SAMPLER_CUBE_SHADOW:
    case LOCAL_GL_INT_SAMPLER_2D:
    case LOCAL_GL_INT_SAMPLER_3D:
    case LOCAL_GL_INT_SAMPLER_CUBE:
    case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
    case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
      return 1;

    case LOCAL_GL_BOOL_VEC2:
    case LOCAL_GL_FLOAT_VEC2:
    case LOCAL_GL_INT_VEC2:
    case LOCAL_GL_UNSIGNED_INT_VEC2:
      return 2;

    case LOCAL_GL_BOOL_VEC3:
    case LOCAL_GL_FLOAT_VEC3:
    case LOCAL_GL_INT_VEC3:
    case LOCAL_GL_UNSIGNED_INT_VEC3:
      return 3;

    case LOCAL_GL_BOOL_VEC4:
    case LOCAL_GL_FLOAT_VEC4:
    case LOCAL_GL_INT_VEC4:
    case LOCAL_GL_UNSIGNED_INT_VEC4:
    case LOCAL_GL_FLOAT_MAT2:
      return 4;

    case LOCAL_GL_FLOAT_MAT2x3:
    case LOCAL_GL_FLOAT_MAT3x2:
      return 2 * 3;

    case LOCAL_GL_FLOAT_MAT2x4:
    case LOCAL_GL_FLOAT_MAT4x2:
      return 2 * 4;

    case LOCAL_GL_FLOAT_MAT3:
      return 3 * 3;

    case LOCAL_GL_FLOAT_MAT3x4:
    case LOCAL_GL_FLOAT_MAT4x3:
      return 3 * 4;

    case LOCAL_GL_FLOAT_MAT4:
      return 4 * 4;

    default:
      return 0;
  }
}

bool WebGLProgram::ValidateAfterTentativeLink(
    std::string* const out_linkLog) const {
  const auto& linkInfo = mMostRecentLinkInfo;
  const auto& gl = mContext->gl;

  // Check for overlapping attrib locations.
  {
    std::unordered_map<uint32_t, const std::string&> nameByLoc;
    for (const auto& attrib : linkInfo->active.activeAttribs) {
      if (attrib.location == -1) continue;

      const auto& elemType = attrib.elemType;
      const auto numUsedLocs = NumUsedLocationsByElemType(elemType);
      for (uint32_t i = 0; i < numUsedLocs; i++) {
        const uint32_t usedLoc = attrib.location + i;

        const auto res = nameByLoc.insert({usedLoc, attrib.name});
        const bool& didInsert = res.second;
        if (!didInsert) {
          const auto& aliasingName = attrib.name;
          const auto& itrExisting = res.first;
          const auto& existingName = itrExisting->second;
          *out_linkLog = nsPrintfCString(
                             "Attrib \"%s\" aliases locations used by"
                             " attrib \"%s\".",
                             aliasingName.c_str(), existingName.c_str())
                             .get();
          return false;
        }
      }
    }
  }

  // Forbid too many components for specified buffer mode
  const auto& activeTfVaryings = linkInfo->active.activeTfVaryings;
  // The driver reported different varyings than requested, which might occur
  // if there was somehow stale or invalid transform-feedback state.
  if (mNextLink_TransformFeedbackVaryings.size() != activeTfVaryings.size()) {
    *out_linkLog =
        "Mismatch between requested and driver-reported active transform"
        " feedback varyings.";
    return false;
  }
  if (!activeTfVaryings.empty()) {
    GLuint maxComponentsPerIndex = 0;
    switch (linkInfo->transformFeedbackBufferMode) {
      case LOCAL_GL_INTERLEAVED_ATTRIBS:
        gl->GetUIntegerv(LOCAL_GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS,
                         &maxComponentsPerIndex);
        break;

      case LOCAL_GL_SEPARATE_ATTRIBS:
        gl->GetUIntegerv(LOCAL_GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS,
                         &maxComponentsPerIndex);
        break;

      default:
        MOZ_CRASH("`bufferMode`");
    }

    std::vector<size_t> componentsPerVert;
    for (const auto& cur : activeTfVaryings) {
      if (componentsPerVert.empty() ||
          linkInfo->transformFeedbackBufferMode == LOCAL_GL_SEPARATE_ATTRIBS) {
        componentsPerVert.push_back(0);
      }

      const size_t varyingComponents =
          ElemTypeComponents(cur.elemType) * cur.elemCount;
      if (!varyingComponents) {
        *out_linkLog = nsPrintfCString(
                           "Transform feedback varying \"%s\" has an"
                           " unsupported type.",
                           cur.name.c_str())
                           .get();
        return false;
      }

      auto& totalComponentsForIndex = *(componentsPerVert.rbegin());
      totalComponentsForIndex += varyingComponents;

      if (totalComponentsForIndex > maxComponentsPerIndex) {
        *out_linkLog = nsPrintfCString(
                           "Transform feedback varying \"%s\""
                           " pushed `componentsForIndex` over the"
                           " limit of %u.",
                           cur.name.c_str(), maxComponentsPerIndex)
                           .get();
        return false;
      }
    }

    linkInfo->componentsPerTFVert = std::move(componentsPerVert);
  }

  return true;
}

bool WebGLProgram::UseProgram() const {
  if (!mMostRecentLinkInfo) {
    mContext->ErrorInvalidOperation(
        "Program has not been successfully linked.");
    return false;
  }

  if (mContext->mBoundTransformFeedback &&
      mContext->mBoundTransformFeedback->mIsActive &&
      !mContext->mBoundTransformFeedback->mIsPaused) {
    mContext->ErrorInvalidOperation(
        "Transform feedback active and not paused.");
    return false;
  }

  mContext->gl->fUseProgram(mGLName);
  return true;
}

bool WebGLProgram::ValidateProgram() const {
  gl::GLContext* gl = mContext->gl;

#ifdef XP_MACOSX
  // See bug 593867 for NVIDIA and bug 657201 for ATI. The latter is confirmed
  // with Mac OS 10.6.7.
  if (gl->WorkAroundDriverBugs()) {
    mContext->GenerateWarning(
        "Implemented as a no-op on"
        " Mac to work around crashes.");
    return true;
  }
#endif

  gl->fValidateProgram(mGLName);
  GLint ok = 0;
  gl->fGetProgramiv(mGLName, LOCAL_GL_VALIDATE_STATUS, &ok);
  return bool(ok);
}

////////////////////////////////////////////////////////////////////////////////

void WebGLProgram::LinkAndUpdate() {
  mMostRecentLinkInfo = nullptr;

  gl::GLContext* gl = mContext->gl;
  gl->fLinkProgram(mGLName);

  // Grab the program log.
  {
    GLuint logLenWithNull = 0;
    gl->fGetProgramiv(mGLName, LOCAL_GL_INFO_LOG_LENGTH,
                      (GLint*)&logLenWithNull);
    if (logLenWithNull > 1) {
      std::vector<char> buffer(logLenWithNull);
      gl->fGetProgramInfoLog(mGLName, buffer.size(), nullptr, buffer.data());
      mLinkLog = buffer.data();
    } else {
      mLinkLog.clear();
    }
  }

  GLint ok = 0;
  gl->fGetProgramiv(mGLName, LOCAL_GL_LINK_STATUS, &ok);
  if (!ok) return;

  mMostRecentLinkInfo =
      QueryProgramInfo(this, gl);  // Fallible after context loss.
}

void WebGLProgram::TransformFeedbackVaryings(
    const std::vector<std::string>& varyings, GLenum bufferMode) {
  const auto& gl = mContext->gl;

  switch (bufferMode) {
    case LOCAL_GL_INTERLEAVED_ATTRIBS:
      break;

    case LOCAL_GL_SEPARATE_ATTRIBS: {
      GLuint maxAttribs = 0;
      gl->GetUIntegerv(LOCAL_GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS,
                       &maxAttribs);
      // While we report the conservative constant limit, the driver might
      // support less. Try to catch that here early rather than at link.
      maxAttribs = std::min(
          maxAttribs, GLuint(webgl::kMaxTransformFeedbackSeparateAttribs));
      if (varyings.size() > maxAttribs) {
        mContext->ErrorInvalidValue("Length of `varyings` exceeds %s.",
                                    "TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS");
        return;
      }
    } break;

    default:
      mContext->ErrorInvalidEnumInfo("bufferMode", bufferMode);
      return;
  }

  ////

  mNextLink_TransformFeedbackVaryings = varyings;
  mNextLink_TransformFeedbackBufferMode = bufferMode;
}

}  // namespace mozilla
