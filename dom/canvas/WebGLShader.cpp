/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLShader.h"

#include "GLContext.h"
#include "GLSLANG/ShaderLang.h"
#include "WebGLContext.h"
#include "WebGLObjectModel.h"
#include "WebGLShaderValidator.h"
#include "WebGLValidateStrings.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "prenv.h"

namespace mozilla {

static void PrintLongString(const char* const begin, const size_t len) {
  // Wow - Roll Your Own Foreach-Lines because printf_stderr has a hard-coded
  // internal size, so long strings are truncated.

  const size_t chunkSize = 1000;
  auto buf = std::vector<char>(chunkSize + 1);  // +1 for null-term
  const auto bufBegin = buf.data();

  auto chunkBegin = begin;
  const auto end = begin + len;
  while (chunkBegin + chunkSize < end) {
    memcpy(bufBegin, chunkBegin, chunkSize);
    printf_stderr("%s", bufBegin);
    chunkBegin += chunkSize;
  }
  printf_stderr("%s", chunkBegin);
}

template <size_t N>
static bool SubstringStartsWith(const std::string& testStr, size_t offset,
                                const char (&refStr)[N]) {
  for (size_t i = 0; i < N - 1; i++) {
    if (testStr[offset + i] != refStr[i]) return false;
  }
  return true;
}

static void GetCompilationStatusAndLog(gl::GLContext* gl, GLuint shader,
                                       bool* const out_success,
                                       std::string* const out_log) {
  GLint compileStatus = LOCAL_GL_FALSE;
  gl->fGetShaderiv(shader, LOCAL_GL_COMPILE_STATUS, &compileStatus);

  // It's simpler if we always get the log.
  GLint lenWithNull = 0;
  gl->fGetShaderiv(shader, LOCAL_GL_INFO_LOG_LENGTH, &lenWithNull);
  if (lenWithNull < 1) {
    lenWithNull = 1;
  }
  std::vector<char> buffer(lenWithNull);
  gl->fGetShaderInfoLog(shader, buffer.size(), nullptr, buffer.data());
  *out_log = buffer.data();

  *out_success = (compileStatus == LOCAL_GL_TRUE);
}

////////////////////////////////////////////////////////////////////////////////

WebGLShader::WebGLShader(WebGLContext* webgl, GLenum type)
    : WebGLContextBoundObject(webgl),
      mGLName(webgl->gl->fCreateShader(type)),
      mType(type) {
  mCompileResults = std::make_unique<webgl::ShaderValidatorResults>();
}

WebGLShader::~WebGLShader() {
  if (!mContext) return;
  mContext->gl->fDeleteShader(mGLName);
}

void WebGLShader::ShaderSource(const std::string& u8) {
  mSource = CrushGlslToAscii(u8);
}

void WebGLShader::CompileShader() {
  mCompilationSuccessful = false;

  gl::GLContext* gl = mContext->gl;

  static const bool kDumpShaders = PR_GetEnv("MOZ_WEBGL_DUMP_SHADERS");
  if (kDumpShaders) [[unlikely]] {
    printf_stderr("==== begin MOZ_WEBGL_DUMP_SHADERS ====\n");
    PrintLongString(mSource.c_str(), mSource.size());
  }

  {
    const auto validator = mContext->CreateShaderValidator(mType);
    MOZ_ASSERT(validator);

    mCompileResults = validator->ValidateAndTranslate(mSource.c_str());
  }

  mCompilationLog = mCompileResults->mInfoLog;
  const auto& success = mCompileResults->mValid;

  if (kDumpShaders) [[unlikely]] {
    printf_stderr("\n==== \\/ \\/ \\/ ====\n");
    if (success) {
      const auto& translated = mCompileResults->mObjectCode;
      PrintLongString(translated.data(), translated.size());
    } else {
      printf_stderr("Validation failed:\n%s",
                    mCompileResults->mInfoLog.c_str());
    }
    printf_stderr("\n==== end ====\n");
  }

  if (!success) return;

  const std::array<const char*, 1> parts = {
      mCompileResults->mObjectCode.c_str()};
  gl->fShaderSource(mGLName, parts.size(), parts.data(), nullptr);

  gl->fCompileShader(mGLName);

  GetCompilationStatusAndLog(gl, mGLName, &mCompilationSuccessful,
                             &mCompilationLog);
}

////////////////////////////////////////////////////////////////////////////////

size_t WebGLShader::CalcNumSamplerUniforms() const {
  size_t accum = 0;
  for (const auto& cur : mCompileResults->mUniforms) {
    const auto& type = cur.type;
    if (type == LOCAL_GL_SAMPLER_2D || type == LOCAL_GL_SAMPLER_CUBE) {
      accum += cur.getArraySizeProduct();
    }
  }
  return accum;
}

size_t WebGLShader::NumAttributes() const {
  return mCompileResults->mAttributes.size();
}

void WebGLShader::BindAttribLocation(GLuint prog, const std::string& userName,
                                     GLuint index) const {
  for (const auto& attrib : mCompileResults->mAttributes) {
    if (attrib.name == userName) {
      mContext->gl->fBindAttribLocation(prog, index, attrib.mappedName.c_str());
      return;
    }
  }
}

void WebGLShader::MapTransformFeedbackVaryings(
    const std::vector<std::string>& varyings,
    std::vector<std::string>* out_mappedVaryings) const {
  MOZ_ASSERT(mType == LOCAL_GL_VERTEX_SHADER);
  MOZ_ASSERT(out_mappedVaryings);

  out_mappedVaryings->clear();
  out_mappedVaryings->reserve(varyings.size());

  const auto& shaderVaryings = mCompileResults->mVaryings;

  for (const auto& userName : varyings) {
    const auto* mappedName = &userName;
    for (const auto& shaderVarying : shaderVaryings) {
      if (shaderVarying.name == userName) {
        mappedName = &shaderVarying.mappedName;
        break;
      }
    }
    out_mappedVaryings->push_back(*mappedName);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Boilerplate

size_t WebGLShader::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) const {
  return mallocSizeOf(this) + mSource.size() + 1 +
         mCompileResults->SizeOfIncludingThis(mallocSizeOf) +
         mCompilationLog.size() + 1;
}

}  // namespace mozilla
