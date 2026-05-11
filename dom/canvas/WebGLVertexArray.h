/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_VERTEX_ARRAY_H_
#define WEBGL_VERTEX_ARRAY_H_

#include <array>
#include <bitset>

#include "CacheInvalidator.h"
#include "WebGLObjectModel.h"
#include "WebGLStrongTypes.h"
#include "mozilla/IntegerRange.h"

namespace mozilla {

class WebGLBuffer;
class WebGLVertexArrayFake;

namespace webgl {
struct LinkedProgramInfo;

struct VertAttribLayoutData final {
  // Keep this packed tightly!
  uint32_t divisor = 0;
  bool isArray = false;
  uint8_t byteSize = 0;
  uint8_t byteStride = 1;  // Non-zero, at-most 255
  webgl::AttribBaseType baseType = webgl::AttribBaseType::Float;
  uint64_t byteOffset = 0;
};

struct VertAttribBindingData final {
  VertAttribLayoutData layout;
  RefPtr<WebGLBuffer> buffer;
};

}  // namespace webgl

void DoVertexAttribPointer(gl::GLContext&, uint32_t index,
                           const webgl::VertAttribPointerDesc&);

class WebGLVertexArray : public WebGLContextBoundObject {
  friend class ScopedDrawHelper;
  friend class WebGLContext;
  friend class WebGLVertexArrayFake;
  friend class WebGL2Context;
  friend struct webgl::LinkedProgramInfo;

  static constexpr size_t kMaxAttribs = 32;  // gpuinfo.org says so

  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(WebGLVertexArray, override)

  RefPtr<WebGLBuffer> mElementArrayBuffer;

  std::array<webgl::VertAttribBindingData, kMaxAttribs> mBindings;  // Hot data.
  std::array<webgl::VertAttribPointerDesc, kMaxAttribs>
      mDescs;  // cold data, parallel to mBindings.

  std::bitset<kMaxAttribs> mAttribIsArrayWithNullBuffer;
  bool mHasBeenBound = false;

 public:
  static WebGLVertexArray* Create(WebGLContext* webgl);

 protected:
  explicit WebGLVertexArray(WebGLContext* webgl);
  ~WebGLVertexArray();

 public:
  virtual void BindVertexArray() = 0;

  void SetAttribIsArray(const uint32_t index, const bool val) {
    auto& binding = mBindings.at(index);
    binding.layout.isArray = val;
    mAttribIsArrayWithNullBuffer[index] =
        binding.layout.isArray && !binding.buffer;
  }

  void AttribDivisor(const uint32_t index, const uint32_t val) {
    auto& binding = mBindings.at(index);
    binding.layout.divisor = val;
  }

  void AttribPointer(const uint32_t index, WebGLBuffer* const buffer,
                     const webgl::VertAttribPointerDesc& desc,
                     const webgl::VertAttribPointerCalculated& calc) {
    mDescs[index] = desc;

    auto& binding = mBindings.at(index);
    binding.buffer = buffer;
    binding.layout.byteSize = calc.byteSize;
    binding.layout.byteStride = calc.byteStride;
    binding.layout.baseType = calc.baseType;
    binding.layout.byteOffset = desc.byteOffset;

    mAttribIsArrayWithNullBuffer[index] =
        binding.layout.isArray && !binding.buffer;
  }

  const auto& AttribBinding(const uint32_t index) const {
    return mBindings.at(index);
  }
  const auto& AttribDesc(const uint32_t index) const {
    return mDescs.at(index);
  }

  Maybe<uint32_t> GetAttribIsArrayWithNullBuffer() const {
    const auto& bitset = mAttribIsArrayWithNullBuffer;
    if (bitset.none()) [[likely]] {
      return {};
    }
    for (const auto i : IntegerRange(bitset.size())) {
      if (bitset[i]) return Some(i);
    }
    MOZ_CRASH();
  }

  Maybe<double> GetVertexAttrib(uint32_t index, GLenum pname) const;
  void DoVertexAttrib(uint32_t index, uint32_t vertOffset = 0) const;
};

}  // namespace mozilla

#endif  // WEBGL_VERTEX_ARRAY_H_
