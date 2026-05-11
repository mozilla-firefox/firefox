/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLBuffer.h"

#include "GLContext.h"
#include "WebGLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

namespace mozilla {

WebGLBuffer::WebGLBuffer(WebGLContext* webgl, GLuint buf)
    : WebGLContextBoundObject(webgl), mGLName(buf) {}

WebGLBuffer::~WebGLBuffer() {
  mByteLength = 0;
  mFetchInvalidator.InvalidateCaches();

  mIndexCache.reset();
  mIndexRanges.clear();

  if (!mContext) return;
  mContext->gl->fDeleteBuffers(1, &mGLName);
}

void WebGLBuffer::SetContentAfterBind(GLenum target) {
  if (mContent != Kind::Undefined) return;

  switch (target) {
    case LOCAL_GL_ELEMENT_ARRAY_BUFFER:
      mContent = Kind::ElementArray;
      break;

    case LOCAL_GL_ARRAY_BUFFER:
    case LOCAL_GL_PIXEL_PACK_BUFFER:
    case LOCAL_GL_PIXEL_UNPACK_BUFFER:
    case LOCAL_GL_UNIFORM_BUFFER:
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
    case LOCAL_GL_COPY_READ_BUFFER:
    case LOCAL_GL_COPY_WRITE_BUFFER:
      mContent = Kind::OtherData;
      break;

    default:
      MOZ_CRASH("GFX: invalid target");
  }
}

////////////////////////////////////////

static bool ValidateBufferUsageEnum(WebGLContext* webgl, GLenum usage) {
  switch (usage) {
    case LOCAL_GL_STREAM_DRAW:
    case LOCAL_GL_STATIC_DRAW:
    case LOCAL_GL_DYNAMIC_DRAW:
      return true;

    case LOCAL_GL_DYNAMIC_COPY:
    case LOCAL_GL_DYNAMIC_READ:
    case LOCAL_GL_STATIC_COPY:
    case LOCAL_GL_STATIC_READ:
    case LOCAL_GL_STREAM_COPY:
    case LOCAL_GL_STREAM_READ:
      if (webgl->IsWebGL2()) [[likely]] {
        return true;
      }
      break;

    default:
      break;
  }

  webgl->ErrorInvalidEnumInfo("usage", usage);
  return false;
}

void WebGLBuffer::BufferData(const GLenum target, const uint64_t size,
                             const void* const maybeData, const GLenum usage,
                             bool allowUninitialized) {
  // The driver knows only GLsizeiptr, which is int32_t on 32bit!
  bool sizeValid = CheckedInt<GLsizeiptr>(size).isValid();

  if (mContext->gl->WorkAroundDriverBugs()) {
    // Bug 790879
#if defined(XP_MACOSX) || defined(MOZ_WIDGET_GTK)
    sizeValid &= CheckedInt<int32_t>(size).isValid();
#endif

    // Bug 1610383
    if (mContext->gl->IsANGLE()) {
      // While ANGLE seems to support up to `unsigned int`, UINT32_MAX-4 causes
      // GL_OUT_OF_MEMORY in glFlush??
      sizeValid &= CheckedInt<int32_t>(size).isValid();
    }
  }

  if (!sizeValid) {
    mContext->ErrorOutOfMemory("Size not valid for platform: %" PRIu64, size);
    return;
  }

  // -

  if (!ValidateBufferUsageEnum(mContext, usage)) return;

  const void* uploadData = maybeData;
  UniqueBuffer maybeCalloc;
  if (!uploadData && !allowUninitialized) {
    maybeCalloc = UniqueBuffer::Take(calloc(1, AssertedCast<size_t>(size)));
    if (!maybeCalloc) {
      mContext->ErrorOutOfMemory("Failed to alloc zeros.");
      return;
    }
    uploadData = maybeCalloc.get();
  }
  MOZ_ASSERT(uploadData || allowUninitialized);

  UniqueBuffer newIndexCache;
  const bool needsIndexCache = mContext->mNeedsIndexValidation ||
                               mContext->mMaybeNeedsLegacyVertexAttrib0Handling;
  if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER && needsIndexCache) {
    newIndexCache = UniqueBuffer::Take(malloc(AssertedCast<size_t>(size)));
    if (!newIndexCache) {
      mContext->ErrorOutOfMemory("Failed to alloc index cache.");
      return;
    }
    // memcpy out of SharedArrayBuffers can be racey, and should generally use
    // memcpySafeWhenRacy. But it's safe here:
    // * We only memcpy in one place.
    // * We only read out of the single copy, and only after copying.
    // * If we get data value corruption from racing read-during-write, that's
    // fine.
    memcpy(newIndexCache.get(), uploadData, size);
    uploadData = newIndexCache.get();
  }

  const auto& gl = mContext->gl;
  const ScopedLazyBind lazyBind(gl, target, this);

  const bool sizeChanges = (size != ByteLength());
  if (sizeChanges) {
    gl::GLContext::LocalErrorScope errorScope(*gl);
    gl->fBufferData(target, size, uploadData, usage);
    const auto error = errorScope.GetError();

    if (error) {
      MOZ_ASSERT(error == LOCAL_GL_OUT_OF_MEMORY);
      mContext->ErrorOutOfMemory("Error from driver: 0x%04x", error);

      // Truncate
      mByteLength = 0;
      mFetchInvalidator.InvalidateCaches();
      mIndexCache.reset();
      return;
    }
  } else {
    gl->fBufferData(target, size, uploadData, usage);
  }

  mContext->OnDataAllocCall();

  mUsage = usage;
  mByteLength = size;
  mFetchInvalidator.InvalidateCaches();
  mIndexCache = std::move(newIndexCache);

  if (mIndexCache) {
    if (!mIndexRanges.empty()) {
      mContext->GeneratePerfWarning("[%p] Invalidating %u ranges.", this,
                                    uint32_t(mIndexRanges.size()));
      mIndexRanges.clear();
    }
  }

  ResetLastUpdateFenceId();
}

void WebGLBuffer::BufferSubData(GLenum target, uint64_t rawDstByteOffset,
                                uint64_t rawDataLen, const void* data,
                                bool unsynchronized) const {
  if (!ValidateRange(rawDstByteOffset, rawDataLen)) return;

  const CheckedInt<GLintptr> dstByteOffset = rawDstByteOffset;
  const CheckedInt<GLsizeiptr> dataLen = rawDataLen;
  if (!dstByteOffset.isValid() || !dataLen.isValid()) {
    return mContext->ErrorOutOfMemory("offset or size too large for platform.");
  }

  ////

  if (!rawDataLen) return;  // With validation successful, nothing else to do.

  const void* uploadData = data;
  if (mIndexCache) {
    auto* const cachedDataBegin =
        (uint8_t*)mIndexCache.get() + rawDstByteOffset;
    memcpy(cachedDataBegin, data, dataLen.value());
    uploadData = cachedDataBegin;

    InvalidateCacheRange(dstByteOffset.value(), dataLen.value());
  }

  ////

  const auto& gl = mContext->gl;
  const ScopedLazyBind lazyBind(gl, target, this);

  void* mapping = nullptr;
  // Repeated calls to glMapBufferRange is slow on ANGLE, so fall back to the
  // glBufferSubData path. See bug 1827047.
  if (unsynchronized && gl->IsSupported(gl::GLFeature::map_buffer_range) &&
      !gl->IsANGLE()) {
    GLbitfield access = LOCAL_GL_MAP_WRITE_BIT |
                        LOCAL_GL_MAP_UNSYNCHRONIZED_BIT |
                        LOCAL_GL_MAP_INVALIDATE_RANGE_BIT;
    // On some devices there are known performance issues with the combination
    // of GL_MAP_UNSYNCHRONIZED_BIT and GL_MAP_INVALIDATE_RANGE_BIT, so omit the
    // latter.
    if (gl->Renderer() == gl::GLRenderer::MaliT ||
        gl->Vendor() == gl::GLVendor::Qualcomm) {
      access &= ~LOCAL_GL_MAP_INVALIDATE_RANGE_BIT;
    }
    mapping = gl->fMapBufferRange(target, dstByteOffset.value(),
                                  dataLen.value(), access);
  }

  if (mapping) {
    memcpy(mapping, uploadData, dataLen.value());
    gl->fUnmapBuffer(target);
  } else {
    gl->fBufferSubData(target, dstByteOffset.value(), dataLen.value(),
                       uploadData);
  }

  ResetLastUpdateFenceId();
}

bool WebGLBuffer::ValidateRange(size_t byteOffset, size_t byteLen) const {
  auto availLength = mByteLength;
  if (byteOffset > availLength) {
    mContext->ErrorInvalidValue("Offset passes the end of the buffer.");
    return false;
  }
  availLength -= byteOffset;

  if (byteLen > availLength) {
    mContext->ErrorInvalidValue("Offset+size passes the end of the buffer.");
    return false;
  }

  return true;
}

////////////////////////////////////////

static uint8_t IndexByteSizeByType(GLenum type) {
  switch (type) {
    case LOCAL_GL_UNSIGNED_BYTE:
      return 1;
    case LOCAL_GL_UNSIGNED_SHORT:
      return 2;
    case LOCAL_GL_UNSIGNED_INT:
      return 4;
    default:
      MOZ_CRASH();
  }
}

void WebGLBuffer::InvalidateCacheRange(uint64_t byteOffset,
                                       uint64_t byteLength) const {
  MOZ_ASSERT(mIndexCache);

  std::vector<IndexRange> invalids;
  const uint64_t updateBegin = byteOffset;
  const uint64_t updateEnd = updateBegin + byteLength;
  for (const auto& cur : mIndexRanges) {
    const auto& range = cur.first;
    const auto& indexByteSize = IndexByteSizeByType(range.type);
    const auto rangeBegin = range.byteOffset * indexByteSize;
    const auto rangeEnd =
        rangeBegin + uint64_t(range.indexCount) * indexByteSize;
    if (rangeBegin >= updateEnd || rangeEnd <= updateBegin) continue;
    invalids.push_back(range);
  }

  if (!invalids.empty()) {
    mContext->GeneratePerfWarning("[%p] Invalidating %u/%u ranges.", this,
                                  uint32_t(invalids.size()),
                                  uint32_t(mIndexRanges.size()));

    for (const auto& cur : invalids) {
      mIndexRanges.erase(cur);
    }
  }
}

size_t WebGLBuffer::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t size = mallocSizeOf(this);
  if (mIndexCache) {
    size += mByteLength;
  }
  return size;
}

template <typename T>
static Maybe<uint32_t> MaxForRange(const void* const start,
                                   const uint32_t count,
                                   const Maybe<uint32_t>& untypedIgnoredVal) {
  const Maybe<T> ignoredVal =
      (untypedIgnoredVal ? Some(T(untypedIgnoredVal.value())) : Nothing());
  Maybe<uint32_t> maxVal;

  auto itr = (const T*)start;
  const auto end = itr + count;

  for (; itr != end; ++itr) {
    const auto& val = *itr;
    if (ignoredVal && val == ignoredVal.value()) continue;

    if (maxVal && val <= maxVal.value()) continue;

    maxVal = Some(val);
  }

  return maxVal;
}

static const uint32_t kMaxIndexRanges = 256;

Maybe<uint32_t> WebGLBuffer::GetIndexedFetchMaxVert(
    const GLenum type, const uint64_t byteOffset,
    const uint32_t indexCount) const {
  if (!mIndexCache) return Nothing();

  const IndexRange range = {type, byteOffset, indexCount};
  auto res = mIndexRanges.insert({range, Nothing()});
  if (mIndexRanges.size() > kMaxIndexRanges) {
    mContext->GeneratePerfWarning(
        "[%p] Clearing mIndexRanges after exceeding %u.", this,
        kMaxIndexRanges);
    mIndexRanges.clear();
    res = mIndexRanges.insert({range, Nothing()});
  }

  const auto& itr = res.first;
  const auto& didInsert = res.second;

  auto& maxFetchIndex = itr->second;
  if (didInsert) {
    const auto& data = mIndexCache.get();

    const auto start = (const uint8_t*)data + byteOffset;

    Maybe<uint32_t> ignoredVal;
    if (mContext->IsWebGL2()) {
      ignoredVal = Some(UINT32_MAX);
    }

    switch (type) {
      case LOCAL_GL_UNSIGNED_BYTE:
        maxFetchIndex = MaxForRange<uint8_t>(start, indexCount, ignoredVal);
        break;
      case LOCAL_GL_UNSIGNED_SHORT:
        maxFetchIndex = MaxForRange<uint16_t>(start, indexCount, ignoredVal);
        break;
      case LOCAL_GL_UNSIGNED_INT:
        maxFetchIndex = MaxForRange<uint32_t>(start, indexCount, ignoredVal);
        break;
      default:
        MOZ_CRASH();
    }
    const auto displayMaxVertIndex =
        maxFetchIndex ? int64_t(maxFetchIndex.value()) : -1;
    mContext->GeneratePerfWarning("[%p] New range #%u: (0x%04x, %" PRIu64
                                  ", %u):"
                                  " %" PRIi64,
                                  this, uint32_t(mIndexRanges.size()),
                                  range.type, range.byteOffset,
                                  range.indexCount, displayMaxVertIndex);
  }

  return maxFetchIndex;
}

////

bool WebGLBuffer::ValidateCanBindToTarget(GLenum target) {
  /* https://www.khronos.org/registry/webgl/specs/latest/2.0/#5.1
   *
   * In the WebGL 2 API, buffers have their WebGL buffer type
   * initially set to undefined. Calling bindBuffer, bindBufferRange
   * or bindBufferBase with the target argument set to any buffer
   * binding point except COPY_READ_BUFFER or COPY_WRITE_BUFFER will
   * then set the WebGL buffer type of the buffer being bound
   * according to the table above.
   *
   * Any call to one of these functions which attempts to bind a
   * WebGLBuffer that has the element array WebGL buffer type to a
   * binding point that falls under other data, or bind a
   * WebGLBuffer which has the other data WebGL buffer type to
   * ELEMENT_ARRAY_BUFFER will generate an INVALID_OPERATION error,
   * and the state of the binding point will remain untouched.
   */

  if (mContent == WebGLBuffer::Kind::Undefined) return true;

  switch (target) {
    case LOCAL_GL_COPY_READ_BUFFER:
    case LOCAL_GL_COPY_WRITE_BUFFER:
      return true;

    case LOCAL_GL_ELEMENT_ARRAY_BUFFER:
      if (mContent == WebGLBuffer::Kind::ElementArray) return true;
      break;

    case LOCAL_GL_ARRAY_BUFFER:
    case LOCAL_GL_PIXEL_PACK_BUFFER:
    case LOCAL_GL_PIXEL_UNPACK_BUFFER:
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
    case LOCAL_GL_UNIFORM_BUFFER:
      if (mContent == WebGLBuffer::Kind::OtherData) return true;
      break;

    default:
      MOZ_CRASH();
  }

  const auto dataType =
      (mContent == WebGLBuffer::Kind::OtherData) ? "other" : "element";
  mContext->ErrorInvalidOperation("Buffer already contains %s data.", dataType);
  return false;
}

void WebGLBuffer::ResetLastUpdateFenceId() const {
  mLastUpdateFenceId = mContext->mNextFenceId;
}

}  // namespace mozilla
