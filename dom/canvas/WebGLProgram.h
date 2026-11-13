/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_PROGRAM_H_
#define WEBGL_PROGRAM_H_

#include <bitset>
#include <map>
#include <set>
#include <vector>

#include "mozilla/RefPtr.h"
#include "mozilla/Vector.h"
#include "mozilla/WeakPtr.h"

#include "CacheInvalidator.h"
#include "WebGLContext.h"
#include "WebGLObjectModel.h"

namespace mozilla {
class ErrorResult;
class WebGLContext;
class WebGLProgram;
class WebGLShader;

namespace dom {
template <typename>
struct Nullable;
class OwningUnsignedLongOrUint32ArrayOrBoolean;
template <typename>
class Sequence;
}  // namespace dom

namespace webgl {

enum class TextureBaseType : uint8_t;

struct UniformBlockInfo final {
  const ActiveUniformBlockInfo& info;
  const IndexedBufferBinding* binding = nullptr;
};

struct FragOutputInfo final {
  const uint8_t loc;
  const std::string userName;
  const std::string mappedName;
  const TextureBaseType baseType;
};

struct CachedDrawFetchLimits final {
  uint64_t maxVerts = UINT64_MAX;
  uint64_t maxInstances = UINT64_MAX;
  std::vector<BufferAndIndex> usedBuffers;

  CachedDrawFetchLimits() = default;
  explicit CachedDrawFetchLimits(const CachedDrawFetchLimits&) = delete;
  CachedDrawFetchLimits(CachedDrawFetchLimits&&) = default;

  CachedDrawFetchLimits& operator=(CachedDrawFetchLimits&&) = default;
};

// -

void UniformAs1fv(gl::GLContext& gl, GLint location, GLsizei count,
                  bool transpose, const void* any);

struct ActiveUniformValidationInfo final {
  const ActiveUniformInfo& info;
  bool isArray = false;
  uint8_t channelsPerElem = 0;
  decltype(&UniformAs1fv) pfn = nullptr;

  static ActiveUniformValidationInfo Make(const ActiveUniformInfo&);
};

struct SamplerUniformInfo final {
  const decltype(WebGLContext::mBound2DTextures)& texListForType;
  const webgl::TextureBaseType texBaseType;
  const bool isShadowSampler;
  Vector<uint8_t, 8> texUnits = decltype(texUnits)();
};

struct LocationInfo final {
  const ActiveUniformValidationInfo info;
  const uint32_t indexIntoUniform;
  SamplerUniformInfo* const samplerInfo;

  auto PrettyName() const {
    auto ret = info.info.name;
    if (info.isArray) {
      ret += "[";
      ret += std::to_string(indexIntoUniform);
      ret += "]";
    }
    return ret;
  }
};

// -

struct LinkedProgramInfo final : public RefCounted<LinkedProgramInfo>,
                                 public SupportsWeakPtr,
                                 public CacheInvalidator {
  friend class mozilla::WebGLProgram;

  MOZ_DECLARE_REFCOUNTED_TYPENAME(LinkedProgramInfo)

  //////

  WebGLProgram* const prog;
  const GLenum transformFeedbackBufferMode;

  std::bitset<kMaxDrawBuffers> hasOutput = 0;
  std::unordered_map<uint8_t, const FragOutputInfo> fragOutputs;
  uint8_t zLayerCount = 1;

  mutable std::vector<size_t> componentsPerTFVert;

  bool attrib0Active = false;
  GLint webgl_gl_VertexID_Offset = -1;  // Location

  // -

  std::map<std::string, std::string> nameMap;
  webgl::LinkActiveInfo active;

  std::vector<std::unique_ptr<SamplerUniformInfo>> samplerUniforms;
  std::unordered_map<uint32_t, LocationInfo> locationMap;

  mutable std::vector<UniformBlockInfo> uniformBlocks;

  //////

 private:
  mutable CachedDrawFetchLimits mScratchFetchLimits;

 public:
  const CachedDrawFetchLimits* GetDrawFetchLimits() const;

  //////

  explicit LinkedProgramInfo(WebGLProgram* prog);
  ~LinkedProgramInfo();
};

}  // namespace webgl

class WebGLProgram final : public WebGLContextBoundObject {
  friend class WebGLTransformFeedback;
  friend struct webgl::LinkedProgramInfo;

  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(WebGLProgram, override)

 public:
  explicit WebGLProgram(WebGLContext* webgl);

  void Delete();

  // GL funcs
  void AttachShader(WebGLShader& shader);
  void BindAttribLocation(GLuint index, const std::string& name);
  void DetachShader(const WebGLShader& shader);
  void UniformBlockBinding(GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) const;

  void LinkProgram();
  bool UseProgram() const;
  bool ValidateProgram() const;

  ////////////////

  void TransformFeedbackVaryings(const std::vector<std::string>& varyings,
                                 GLenum bufferMode);

  bool IsLinked() const { return mMostRecentLinkInfo; }

  const webgl::LinkedProgramInfo* LinkInfo() const {
    return mMostRecentLinkInfo.get();
  }

  const auto& VertShader() const { return mVertShader; }
  const auto& FragShader() const { return mFragShader; }

  const auto& LinkLog() const { return mLinkLog; }

 private:
  ~WebGLProgram();

  void LinkAndUpdate();
  bool ValidateForLink();
  bool ValidateAfterTentativeLink(std::string* const out_linkLog) const;

 public:
  const GLuint mGLName;

 private:
  RefPtr<WebGLShader> mVertShader;
  RefPtr<WebGLShader> mFragShader;
  size_t mNumActiveTFOs;

  std::map<std::string, GLuint> mNextLink_BoundAttribLocs;

  std::vector<std::string> mNextLink_TransformFeedbackVaryings;
  GLenum mNextLink_TransformFeedbackBufferMode;

  std::string mLinkLog;
  RefPtr<const webgl::LinkedProgramInfo> mMostRecentLinkInfo;
};

}  // namespace mozilla

#endif  // WEBGL_PROGRAM_H_
