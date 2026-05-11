/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLMemoryTracker.h"

#include "HostWebGLContext.h"
#include "WebGLBuffer.h"
#include "WebGLRenderbuffer.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"

namespace mozilla {
namespace webgl {
MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)
}  // namespace webgl

void WebGLMemoryTracker::EnsureRegistered() {
  static bool sIsRegistered = []() {
    RegisterStrongMemoryReporter(MakeAndAddRef<WebGLMemoryTracker>());
    return true;
  }();
  (void)sIsRegistered;
}

NS_IMETHODIMP
WebGLMemoryTracker::CollectReports(nsIHandleReportCallback* aHandleReport,
                                   nsISupports* aData, bool) {
  const auto locked = HostWebGLContext::OutstandingContexts();
  const auto& contexts = locked->contexts;

  const auto contextCount = contexts.size();

  size_t bufferCount = 0;
  int64_t bufferGpuSize = 0;
  int64_t bufferCacheSize = 0;

  size_t rbCount = 0;
  int64_t rbGpuSize = 0;

  size_t shaderCount = 0;
  int64_t shaderCpuSize = 0;

  size_t texCount = 0;
  size_t texHeapOverhead = 0;
  int64_t texGpuSize = 0;

  for (const auto& context : contexts) {
    bufferCount += context->mBufferMap.size();
    for (const auto& pair : context->mBufferMap) {
      const auto& buffer = *pair.second;
      bufferGpuSize += buffer.mByteLength;

      if (buffer.mIndexCache) {
        bufferCacheSize += buffer.mByteLength;
      }
      bufferCacheSize += buffer.mIndexRanges.size() *
                         sizeof(decltype(buffer.mIndexRanges)::value_type);
    }

    // -

    rbCount += context->mRenderbufferMap.size();
    for (const auto& pair : context->mRenderbufferMap) {
      const auto& rb = pair.second;
      rbGpuSize += rb->MemoryUsage();
    }

    // -

    shaderCount += context->mShaderMap.size();
    for (const auto& pair : context->mShaderMap) {
      const auto& shader = pair.second;
      shaderCpuSize += shader->SizeOfIncludingThis(webgl::MallocSizeOf);
    }

    // -

    texCount += context->mTextureMap.size();
    for (const auto& pair : context->mTextureMap) {
      const auto& texture = pair.second;
      texGpuSize += texture->MemoryUsage();
      texHeapOverhead += texture->SizeOfIncludingThis(webgl::MallocSizeOf);
    }
  }

  // -

  MOZ_COLLECT_REPORT(
      "webgl-texture-memory", KIND_OTHER, UNITS_BYTES, texGpuSize,
      "Memory used by WebGL textures. The OpenGL implementation is free to "
      "store these textures in either video memory or main memory. This "
      "measurement is only a lower bound, actual memory usage may be higher "
      "for example if the storage is strided.");

  MOZ_COLLECT_REPORT("webgl-texture-count", KIND_OTHER, UNITS_COUNT,
                     static_cast<int64_t>(texCount),
                     "Number of WebGL textures.");

  MOZ_COLLECT_REPORT(
      "webgl-buffer-memory", KIND_OTHER, UNITS_BYTES, bufferGpuSize,
      "Memory used by WebGL buffers. The OpenGL implementation is free to "
      "store these buffers in either video memory or main memory. This "
      "measurement is only a lower bound, actual memory usage may be higher "
      "for example if the storage is strided.");

  MOZ_COLLECT_REPORT(
      "explicit/webgl/buffer-cache-memory", KIND_HEAP, UNITS_BYTES,
      bufferCacheSize,
      "Memory used by WebGL buffer caches. The WebGL implementation caches "
      "the contents of element array buffers only. This adds up with the "
      "'webgl-buffer-memory' value, but contrary to it, this one represents "
      "bytes on the heap, not managed by OpenGL.");

  MOZ_COLLECT_REPORT("webgl-buffer-count", KIND_OTHER, UNITS_COUNT,
                     static_cast<int64_t>(bufferCount),
                     "Number of WebGL buffers.");

  MOZ_COLLECT_REPORT(
      "webgl-renderbuffer-memory", KIND_OTHER, UNITS_BYTES, rbGpuSize,
      "Memory used by WebGL renderbuffers. The OpenGL implementation is free "
      "to store these renderbuffers in either video memory or main memory. "
      "This measurement is only a lower bound, actual memory usage may be "
      "higher, for example if the storage is strided.");

  MOZ_COLLECT_REPORT("webgl-renderbuffer-count", KIND_OTHER, UNITS_COUNT,
                     static_cast<int64_t>(rbCount),
                     "Number of WebGL renderbuffers.");

  MOZ_COLLECT_REPORT(
      "explicit/webgl/shaders", KIND_HEAP, UNITS_BYTES, shaderCpuSize,
      "Combined size of WebGL shader ASCII sources and translation logs "
      "cached on the heap.");

  MOZ_COLLECT_REPORT("explicit/webgl/textures", KIND_HEAP, UNITS_BYTES,
                     texHeapOverhead,
                     "WebGLTexture implementation detail overhead.");

  MOZ_COLLECT_REPORT("webgl-shader-count", KIND_OTHER, UNITS_COUNT,
                     static_cast<int64_t>(shaderCount),
                     "Number of WebGL shaders.");

  MOZ_COLLECT_REPORT("webgl-context-count", KIND_OTHER, UNITS_COUNT,
                     static_cast<int64_t>(contextCount),
                     "Number of WebGL contexts.");

  return NS_OK;
}

NS_IMPL_ISUPPORTS(WebGLMemoryTracker, nsIMemoryReporter)

}  // namespace mozilla
