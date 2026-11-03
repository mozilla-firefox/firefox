//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Buffer11.cpp: Defines the Buffer11 class.

#ifdef UNSAFE_BUFFERS_BUILD
#    pragma allow_unsafe_buffers
#endif

#include "libANGLE/renderer/d3d/d3d11/Buffer11.h"

#include <memory>

#include "common/MemoryBuffer.h"
#include "libANGLE/Context.h"
#include "libANGLE/renderer/d3d/IndexDataManager.h"
#include "libANGLE/renderer/d3d/VertexDataManager.h"
#include "libANGLE/renderer/d3d/d3d11/Context11.h"
#include "libANGLE/renderer/d3d/d3d11/RenderTarget11.h"
#include "libANGLE/renderer/d3d/d3d11/Renderer11.h"
#include "libANGLE/renderer/d3d/d3d11/formatutils11.h"
#include "libANGLE/renderer/d3d/d3d11/renderer11_utils.h"
#include "libANGLE/renderer/renderer_utils.h"

namespace rx
{

namespace
{
template <typename T>
GLuint ReadIndexValueFromIndices(const uint8_t *data, size_t index)
{
    return reinterpret_cast<const T *>(data)[index];
}
typedef GLuint (*ReadIndexValueFunction)(const uint8_t *data, size_t index);

enum class CopyResult
{
    RECREATED,
    NOT_RECREATED,
};

constexpr size_t kConstantBufferOffsetAlignment = 256;

void CalculateConstantBufferParams(GLintptr offset,
                                   GLsizeiptr size,
                                   UINT *outFirstConstant,
                                   UINT *outNumConstants)
{
    // The offset must be aligned to 256 bytes (should have been enforced by glBindBufferRange).
    ASSERT(offset % kConstantBufferOffsetAlignment == 0);

    // firstConstant and numConstants are expressed in constants of 16-bytes. Furthermore they must
    // be a multiple of 16 constants.
    *outFirstConstant = static_cast<UINT>(offset / 16);

    // The GL size is not required to be aligned to a 256 bytes boundary.
    // Round the size up to a 256 bytes boundary then express the results in constants of 16-bytes.
    *outNumConstants = static_cast<UINT>(
        rx::roundUpPow2(size, static_cast<GLsizeiptr>(kConstantBufferOffsetAlignment)) / 16);
    ASSERT(*outNumConstants <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT);

    // Since the size is rounded up, firstConstant + numConstants may be bigger than the actual size
    // of the buffer. This behaviour is explictly allowed according to the documentation on
    // ID3D11DeviceContext1::PSSetConstantBuffers1
    // https://msdn.microsoft.com/en-us/library/windows/desktop/hh404649%28v=vs.85%29.aspx
}

}  // anonymous namespace

namespace gl_d3d11
{

D3D11_MAP GetD3DMapTypeFromBits(BufferUsage usage, GLbitfield access)
{
    bool readBit  = ((access & GL_MAP_READ_BIT) != 0);
    bool writeBit = ((access & GL_MAP_WRITE_BIT) != 0);

    ASSERT(readBit || writeBit);

    // Note : we ignore the discard bit, because in D3D11, staging buffers
    //  don't accept the map-discard flag (discard only works for DYNAMIC usage)

    if (readBit && !writeBit)
    {
        return D3D11_MAP_READ;
    }
    else if (writeBit && !readBit)
    {
        // Special case for uniform storage - we only allow full buffer updates.
        return usage == BUFFER_USAGE_UNIFORM || usage == BUFFER_USAGE_STRUCTURED
                   ? D3D11_MAP_WRITE_DISCARD
                   : D3D11_MAP_WRITE;
    }
    else if (writeBit && readBit)
    {
        return D3D11_MAP_READ_WRITE;
    }
    else
    {
        UNREACHABLE();
        return D3D11_MAP_READ;
    }
}
}  // namespace gl_d3d11

// Each instance of Buffer11::BufferStorage is specialized for a class of D3D binding points
// - vertex/transform feedback buffers
// - index buffers
// - pixel unpack buffers
// - uniform buffers
class Buffer11::BufferStorage : angle::NonCopyable
{
  public:
    virtual ~BufferStorage() {}

    DataRevision getDataRevision() const { return mRevision; }
    BufferUsage getUsage() const { return mUsage; }
    size_t getSize() const { return mBufferSize; }
    void setDataRevision(DataRevision rev) { mRevision = rev; }

    virtual bool isCPUAccessible(GLbitfield access) const = 0;

    virtual bool isGPUAccessible() const = 0;

    virtual angle::Result copyFromStorage(const gl::Context *context,
                                          BufferStorage *source,
                                          size_t sourceOffset,
                                          size_t size,
                                          size_t destOffset,
                                          CopyResult *resultOut,
                                          BufferFeedback *feedback) = 0;
    virtual angle::Result resize(const gl::Context *context,
                                 size_t size,
                                 bool preserveData,
                                 BufferFeedback *feedback)          = 0;

    virtual angle::Result map(const gl::Context *context,
                              size_t offset,
                              size_t length,
                              GLbitfield access,
                              uint8_t **mapPointerOut) = 0;
    virtual void unmap()                               = 0;

    angle::Result setData(const gl::Context *context,
                          const uint8_t *data,
                          size_t offset,
                          size_t size);

    void setStructureByteStride(unsigned int structureByteStride);

  protected:
    BufferStorage(Renderer11 *renderer, BufferUsage usage);

    Renderer11 *mRenderer;
    DataRevision mRevision;
    const BufferUsage mUsage;
    size_t mBufferSize;
};

// A native buffer storage represents an underlying D3D11 buffer for a particular
// type of storage.
class Buffer11::NativeStorage : public Buffer11::BufferStorage
{
  public:
    NativeStorage(Renderer11 *renderer, BufferUsage usage);
    ~NativeStorage() override;

    bool isCPUAccessible(GLbitfield access) const override;

    bool isGPUAccessible() const override { return true; }

    const d3d11::Buffer &getBuffer() const { return mBuffer; }
    angle::Result copyFromStorage(const gl::Context *context,
                                  BufferStorage *source,
                                  size_t sourceOffset,
                                  size_t size,
                                  size_t destOffset,
                                  CopyResult *resultOut,
                                  BufferFeedback *feedback) override;
    angle::Result resize(const gl::Context *context,
                         size_t size,
                         bool preserveData,
                         BufferFeedback *feedback) override;

    angle::Result map(const gl::Context *context,
                      size_t offset,
                      size_t length,
                      GLbitfield access,
                      uint8_t **mapPointerOut) override;
    void unmap() override;

    angle::Result getSRVForFormat(const gl::Context *context,
                                  DXGI_FORMAT srvFormat,
                                  const d3d11::ShaderResourceView **srvOut);
    angle::Result getRawUAV(const gl::Context *context,
                            unsigned int offset,
                            unsigned int size,
                            d3d11::UnorderedAccessView **uavOut);

  protected:
    d3d11::Buffer mBuffer;

  private:
    static void FillBufferDesc(D3D11_BUFFER_DESC *bufferDesc,
                               Renderer11 *renderer,
                               BufferUsage usage,
                               unsigned int bufferSize);
    void clearSRVs();
    void clearUAVs();

    std::map<DXGI_FORMAT, d3d11::ShaderResourceView> mBufferResourceViews;
    std::map<std::pair<unsigned int, unsigned int>, d3d11::UnorderedAccessView> mBufferRawUAVs;
};

class Buffer11::StructuredBufferStorage : public Buffer11::NativeStorage
{
  public:
    StructuredBufferStorage(Renderer11 *renderer, BufferUsage usage);
    ~StructuredBufferStorage() override;
    angle::Result resizeStructuredBuffer(const gl::Context *context,
                                         unsigned int size,
                                         unsigned int structureByteStride,
                                         BufferFeedback *feedback);
    angle::Result getStructuredBufferRangeSRV(const gl::Context *context,
                                              unsigned int offset,
                                              unsigned int size,
                                              unsigned int structureByteStride,
                                              const d3d11::ShaderResourceView **bufferOut);

  private:
    d3d11::ShaderResourceView mStructuredBufferResourceView;
};

// Pack storage represents internal storage for pack buffers. We implement pack buffers
// as CPU memory, tied to a staging texture, for asynchronous texture readback.
class Buffer11::PackStorage : public Buffer11::BufferStorage
{
  public:
    explicit PackStorage(Renderer11 *renderer);
    ~PackStorage() override;

    bool isCPUAccessible(GLbitfield access) const override { return true; }

    bool isGPUAccessible() const override { return false; }

    angle::Result copyFromStorage(const gl::Context *context,
                                  BufferStorage *source,
                                  size_t sourceOffset,
                                  size_t size,
                                  size_t destOffset,
                                  CopyResult *resultOut,
                                  BufferFeedback *feedback) override;
    angle::Result resize(const gl::Context *context,
                         size_t size,
                         bool preserveData,
                         BufferFeedback *feedback) override;

    angle::Result map(const gl::Context *context,
                      size_t offset,
                      size_t length,
                      GLbitfield access,
                      uint8_t **mapPointerOut) override;
    void unmap() override;

    angle::Result packPixels(const gl::Context *context,
                             const gl::FramebufferAttachment &readAttachment,
                             const PackPixelsParams &params);

  private:
    angle::Result flushQueuedPackCommand(const gl::Context *context);

    TextureHelper11 mStagingTexture;
    angle::MemoryBuffer mMemoryBuffer;
    std::unique_ptr<PackPixelsParams> mQueuedPackCommand;
    bool mDataModified;
};

// System memory storage stores a CPU memory buffer with our buffer data.
// For dynamic data, it's much faster to update the CPU memory buffer than
// it is to update a D3D staging buffer and read it back later.
class Buffer11::SystemMemoryStorage : public Buffer11::BufferStorage
{
  public:
    explicit SystemMemoryStorage(Renderer11 *renderer);
    ~SystemMemoryStorage() override {}

    bool isCPUAccessible(GLbitfield access) const override { return true; }

    bool isGPUAccessible() const override { return false; }

    angle::Result copyFromStorage(const gl::Context *context,
                                  BufferStorage *source,
                                  size_t sourceOffset,
                                  size_t size,
                                  size_t destOffset,
                                  CopyResult *resultOut,
                                  BufferFeedback *feedback) override;
    angle::Result resize(const gl::Context *context,
                         size_t size,
                         bool preserveData,
                         BufferFeedback *feedback) override;

    angle::Result map(const gl::Context *context,
                      size_t offset,
                      size_t length,
                      GLbitfield access,
                      uint8_t **mapPointerOut) override;
    void unmap() override;

    angle::MemoryBuffer *getSystemCopy() { return &mSystemCopy; }

  protected:
    angle::MemoryBuffer mSystemCopy;
};

Buffer11::Buffer11(const gl::BufferState &state, Renderer11 *renderer)
    : BufferD3D(state, renderer),
      mRenderer(renderer),
      mSize(0),
      mMappedStorage(nullptr),
      mBufferStorages({}),
      mLatestBufferStorage(nullptr),
      mDeallocThresholds({}),
      mIdleness({}),
      mConstantBufferStorageAdditionalSize(0),
      mMaxConstantBufferLruCount(0),
      mStructuredBufferStorageAdditionalSize(0),
      mMaxStructuredBufferLruCount(0)
{}

Buffer11::~Buffer11()
{
    for (BufferStorage *&storage : mBufferStorages)
    {
        SafeDelete(storage);
    }

    for (auto &p : mConstantBufferRangeStoragesCache)
    {
        SafeDelete(p.second.storage);
    }

    for (auto &p : mStructuredBufferRangeStoragesCache)
    {
        SafeDelete(p.second.storage);
    }

    mRenderer->onBufferDelete(this);
}

angle::Result Buffer11::setData(const gl::Context *context,
                                gl::BufferBinding target,
                                const void *data,
                                size_t size,
                                gl::BufferUsage usage,
                                BufferFeedback *feedback,
                                gl::ZeroFillRequired zeroFillRequired)
{
    const void *dataForImpl = data;
    if (zeroFillRequired == gl::ZeroFillRequired::Yes)
    {
        const angle::MemoryBuffer *scratchBuffer = nullptr;
        ANGLE_CHECK_GL_ALLOC(
            GetImplAs<Context11>(context),
            context->getZeroFilledBuffer(static_cast<size_t>(size), &scratchBuffer));
        dataForImpl = scratchBuffer->data();
    }

    updateD3DBufferUsage(context, usage, feedback);
    return setSubData(context, target, dataForImpl, size, 0, feedback);
}

angle::Result Buffer11::getData(const gl::Context *context, const uint8_t **outData)
{
    if (mSize == 0)
    {
        // TODO(http://anglebug.com/42261543): This ensures that we don't crash or assert in robust
        // buffer access behavior mode if there are buffers without any data. However, technically
        // it should still be possible to draw, with fetches from this buffer returning zero.
        return angle::Result::Stop;
    }

    SystemMemoryStorage *systemMemoryStorage = nullptr;
    BufferFeedback feedback;
    ANGLE_TRY(
        getBufferStorage(context, BUFFER_USAGE_SYSTEM_MEMORY, &systemMemoryStorage, &feedback));
    ASSERT(systemMemoryStorage->getSize() >= mSize);

    *outData = systemMemoryStorage->getSystemCopy()->data();
    return angle::Result::Continue;
}

angle::Result Buffer11::setSubData(const gl::Context *context,
                                   gl::BufferBinding target,
                                   const void *data,
                                   size_t size,
                                   size_t offset,
                                   BufferFeedback *feedback)
{
    size_t requiredSize = size + offset;

    if (data && size > 0)
    {
        // Use system memory storage for dynamic buffers.
        // Try using a constant storage for constant buffers
        BufferStorage *writeBuffer = nullptr;
        if (target == gl::BufferBinding::Uniform)
        {
            // If we are a very large uniform buffer, keep system memory storage around so that we
            // aren't forced to read back from a constant buffer. We also check the workaround for
            // Intel - this requires us to use system memory so we don't end up having to copy from
            // a constant buffer to a staging buffer.
            // TODO(jmadill): Use Context caps.
            if (offset == 0 && size >= mSize &&
                size <= static_cast<UINT>(mRenderer->getNativeCaps().maxUniformBlockSize) &&
                !mRenderer->getFeatures().useSystemMemoryForConstantBuffers.enabled)
            {
                // Always use BUFFER_USAGE_UNIFORM here. StructuredBufferStorage requires a
                // shader-dependent structureByteStride at draw time, lives exclusively in
                // mStructuredBufferRangeStoragesCache, and is always synced downstream from
                // mLatestBufferStorage (SYSTEM_MEMORY, STAGING, or UNIFORM) in
                // getStructuredBufferRangeSRV. Because onCopyStorage only updates
                // mLatestBufferStorage to lower BufferUsage enum values and onStorageUpdate is
                // never called on range-cache entries, mLatestBufferStorage can never be
                // BUFFER_USAGE_STRUCTURED.
                ASSERT(!mLatestBufferStorage ||
                       mLatestBufferStorage->getUsage() != BUFFER_USAGE_STRUCTURED);
                ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_UNIFORM, &writeBuffer, feedback));
            }
            else
            {
                ANGLE_TRY(
                    getBufferStorage(context, BUFFER_USAGE_SYSTEM_MEMORY, &writeBuffer, feedback));
            }
        }
        else if (supportsDirectBinding())
        {
            ANGLE_TRY(getStagingStorage(context, &writeBuffer));
        }
        else
        {
            ANGLE_TRY(
                getBufferStorage(context, BUFFER_USAGE_SYSTEM_MEMORY, &writeBuffer, feedback));
        }

        ASSERT(writeBuffer);

        // Explicitly resize the staging buffer, preserving data if the new data will not
        // completely fill the buffer
        if (writeBuffer->getSize() < requiredSize)
        {
            bool preserveData = (offset > 0);
            ANGLE_TRY(writeBuffer->resize(context, requiredSize, preserveData, feedback));
        }

        ANGLE_TRY(writeBuffer->setData(context, static_cast<const uint8_t *>(data), offset, size));
        onStorageUpdate(writeBuffer);
    }

    mSize = std::max(mSize, requiredSize);
    invalidateStaticData(context, feedback);

    return angle::Result::Continue;
}

angle::Result Buffer11::copySubData(const gl::Context *context,
                                    BufferImpl *source,
                                    GLintptr sourceOffset,
                                    GLintptr destOffset,
                                    GLsizeiptr size,
                                    BufferFeedback *feedback)
{
    Buffer11 *sourceBuffer = GetAs<Buffer11>(source);
    ASSERT(sourceBuffer != nullptr);

    BufferStorage *copyDest = nullptr;
    ANGLE_TRY(getLatestBufferStorage(context, &copyDest, feedback));
    // copyDest comes from mLatestBufferStorage, which only tracks canonical storages in
    // mBufferStorages and can never be a BUFFER_USAGE_STRUCTURED range-cache entry.
    ASSERT(!copyDest || copyDest->getUsage() != BUFFER_USAGE_STRUCTURED);

    // BUFFER_USAGE_UNIFORM uses D3D11_USAGE_DYNAMIC + D3D11_BIND_CONSTANT_BUFFER, which maps with
    // D3D11_MAP_WRITE_DISCARD (discarding the entire buffer on partial CPU writes) and cannot
    // preserve existing data when copyFromStorage uses the CPU setData path. Route through
    // BUFFER_USAGE_STAGING so existing bytes outside [destOffset, destOffset + size) are preserved.
    if (!copyDest || copyDest->getUsage() == BUFFER_USAGE_UNIFORM)
    {
        ANGLE_TRY(getStagingStorage(context, &copyDest));
    }

    BufferStorage *copySource = nullptr;
    ANGLE_TRY(sourceBuffer->getLatestBufferStorage(context, &copySource, feedback));

    if (!copySource)
    {
        ANGLE_TRY(sourceBuffer->getStagingStorage(context, &copySource));
    }

    ASSERT(copySource && copyDest);

    // A staging buffer is needed if there is no cpu-cpu or gpu-gpu copy path avaiable.
    if (!copyDest->isGPUAccessible() && !copySource->isCPUAccessible(GL_MAP_READ_BIT))
    {
        ANGLE_TRY(sourceBuffer->getStagingStorage(context, &copySource));
    }
    else if (!copySource->isGPUAccessible() && !copyDest->isCPUAccessible(GL_MAP_WRITE_BIT))
    {
        ANGLE_TRY(getStagingStorage(context, &copyDest));
    }

    // D3D11 does not allow overlapped copies until 11.1, and only if the
    // device supports D3D11_FEATURE_DATA_D3D11_OPTIONS::CopyWithOverlap
    // Get around this via a different source buffer
    if (copySource == copyDest)
    {
        if (copySource->getUsage() == BUFFER_USAGE_STAGING)
        {
            ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_VERTEX_OR_TRANSFORM_FEEDBACK,
                                       &copySource, feedback));
        }
        else
        {
            ANGLE_TRY(getStagingStorage(context, &copySource));
        }
    }

    CopyResult copyResult = CopyResult::NOT_RECREATED;
    ANGLE_TRY(copyDest->copyFromStorage(context, copySource, sourceOffset, size, destOffset,
                                        &copyResult, feedback));
    onStorageUpdate(copyDest);

    mSize = std::max<size_t>(mSize, destOffset + size);
    invalidateStaticData(context, feedback);

    return angle::Result::Continue;
}

angle::Result Buffer11::map(const gl::Context *context,
                            GLenum access,
                            void **mapPtr,
                            BufferFeedback *feedback)
{
    // GL_OES_mapbuffer uses an enum instead of a bitfield for it's access, convert to a bitfield
    // and call mapRange.
    ASSERT(access == GL_WRITE_ONLY_OES);
    return mapRange(context, 0, mSize, GL_MAP_WRITE_BIT, mapPtr, feedback);
}

angle::Result Buffer11::mapRange(const gl::Context *context,
                                 size_t offset,
                                 size_t length,
                                 GLbitfield access,
                                 void **mapPtr,
                                 BufferFeedback *feedback)
{
    ASSERT(!mMappedStorage);

    BufferStorage *latestStorage = nullptr;
    ANGLE_TRY(getLatestBufferStorage(context, &latestStorage, feedback));

    if (latestStorage && (latestStorage->getUsage() == BUFFER_USAGE_PIXEL_PACK ||
                          latestStorage->getUsage() == BUFFER_USAGE_STAGING))
    {
        // Latest storage is mappable.
        mMappedStorage = latestStorage;
    }
    else
    {
        // Fall back to using the staging buffer if the latest storage does not exist or is not
        // CPU-accessible.
        ANGLE_TRY(getStagingStorage(context, &mMappedStorage));
    }

    Context11 *context11 = GetImplAs<Context11>(context);
    ANGLE_CHECK_GL_ALLOC(context11, mMappedStorage);

    if ((access & GL_MAP_WRITE_BIT) > 0)
    {
        // Update the data revision immediately, since the data might be changed at any time
        onStorageUpdate(mMappedStorage);
        invalidateStaticData(context, feedback);
    }

    uint8_t *mappedBuffer = nullptr;
    ANGLE_TRY(mMappedStorage->map(context, offset, length, access, &mappedBuffer));
    ASSERT(mappedBuffer);

    *mapPtr = static_cast<void *>(mappedBuffer);
    return angle::Result::Continue;
}

angle::Result Buffer11::unmap(const gl::Context *context,
                              GLboolean *result,
                              BufferFeedback *feedback)
{
    ASSERT(mMappedStorage);
    mMappedStorage->unmap();
    mMappedStorage = nullptr;

    // TODO: detect if we had corruption. if so, return false.
    *result = GL_TRUE;

    return angle::Result::Continue;
}

angle::Result Buffer11::markTransformFeedbackUsage(const gl::Context *context,
                                                   BufferFeedback *feedback)
{
    ANGLE_TRY(markBufferUsage(context, BUFFER_USAGE_VERTEX_OR_TRANSFORM_FEEDBACK, feedback));
    return angle::Result::Continue;
}

void Buffer11::updateDeallocThreshold(BufferUsage usage)
{
    // The following strategy was tuned on the Oort online benchmark (http://oortonline.gl/)
    // as well as a custom microbenchmark (IndexConversionPerfTest.Run/index_range_d3d11)

    // First readback: 8 unmodified uses before we free buffer memory.
    // After that, double the threshold each time until we reach the max.
    if (mDeallocThresholds[usage] == 0)
    {
        mDeallocThresholds[usage] = 8;
    }
    else if (mDeallocThresholds[usage] < std::numeric_limits<unsigned int>::max() / 2u)
    {
        mDeallocThresholds[usage] *= 2u;
    }
    else
    {
        mDeallocThresholds[usage] = std::numeric_limits<unsigned int>::max();
    }
}

// Free the storage if we decide it isn't being used very often.
angle::Result Buffer11::checkForDeallocation(const gl::Context *context,
                                             BufferUsage usage,
                                             BufferFeedback *feedback)
{
    mIdleness[usage]++;

    BufferStorage *&storage = mBufferStorages[usage];

    // TODO(http://anglebug.com/505771894): Add validation that a buffer is not mapped in calls that
    // use it (draw, etc.). Once fixed, turn this into an assert.
    if (storage != nullptr && storage == mMappedStorage)
    {
        ANGLE_TRY_HR(SafeGetImplAs<Context11>(context), E_FAIL,
                     "Error deallocating mapped storage");
    }

    if (storage != nullptr && mIdleness[usage] > mDeallocThresholds[usage])
    {
        BufferStorage *latestStorage = nullptr;
        ANGLE_TRY(getLatestBufferStorage(context, &latestStorage, feedback));
        if (latestStorage != storage)
        {
            SafeDelete(storage);
        }
    }

    return angle::Result::Continue;
}

// Keep system memory when we are using it for the canonical version of data.
bool Buffer11::canDeallocateSystemMemory() const
{
    // Must keep system memory on Intel.
    if (mRenderer->getFeatures().useSystemMemoryForConstantBuffers.enabled)
    {
        return false;
    }

    return (!mBufferStorages[BUFFER_USAGE_UNIFORM] ||
            mSize <= static_cast<size_t>(mRenderer->getNativeCaps().maxUniformBlockSize));
}

void Buffer11::markBufferUsage(BufferUsage usage)
{
    mIdleness[usage] = 0;
}

angle::Result Buffer11::markBufferUsage(const gl::Context *context,
                                        BufferUsage usage,
                                        BufferFeedback *feedback)
{
    BufferStorage *bufferStorage = nullptr;
    ANGLE_TRY(getBufferStorage(context, usage, &bufferStorage, feedback));

    if (bufferStorage)
    {
        onStorageUpdate(bufferStorage);
    }

    invalidateStaticData(context, feedback);
    return angle::Result::Continue;
}

angle::Result Buffer11::garbageCollection(const gl::Context *context, BufferUsage currentUsage)
{
    BufferFeedback feedback;

    if (currentUsage != BUFFER_USAGE_SYSTEM_MEMORY && canDeallocateSystemMemory())
    {
        ANGLE_TRY(checkForDeallocation(context, BUFFER_USAGE_SYSTEM_MEMORY, &feedback));
    }

    if (currentUsage != BUFFER_USAGE_STAGING)
    {
        ANGLE_TRY(checkForDeallocation(context, BUFFER_USAGE_STAGING, &feedback));
    }

    ASSERT(feedback.bufferStateChanged == false);

    return angle::Result::Continue;
}

angle::Result Buffer11::getBuffer(const gl::Context *context,
                                  BufferUsage usage,
                                  ID3D11Buffer **bufferOut,
                                  BufferFeedback *feedback)
{
    NativeStorage *storage = nullptr;
    ANGLE_TRY(getBufferStorage(context, usage, &storage, feedback));
    *bufferOut = storage->getBuffer().get();
    return angle::Result::Continue;
}

angle::Result Buffer11::getConstantBufferRange(const gl::Context *context,
                                               GLintptr offset,
                                               GLsizeiptr size,
                                               const d3d11::Buffer **bufferOut,
                                               UINT *firstConstantOut,
                                               UINT *numConstantsOut,
                                               BufferFeedback *feedback)
{
    const GLsizeiptr maxUniformBlockSize =
        static_cast<GLsizeiptr>(mRenderer->getNativeCaps().maxUniformBlockSize);
    // D3D11 constant buffer bindings are limited to D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT
    // (maxUniformBlockSize bytes). Passing a larger count to *SetConstantBuffers1 causes the D3D11
    // runtime to drop the call, leaving the previously bound constant buffer in the slot.
    const GLsizeiptr clampedSize = std::min(size, maxUniformBlockSize);

    NativeStorage *bufferStorage = nullptr;
    if ((offset == 0 && size < maxUniformBlockSize) ||
        (mRenderer->getRenderer11DeviceCaps().supportsConstantBufferOffsets &&
         static_cast<size_t>(offset) < mSize))
    {
        ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_UNIFORM, &bufferStorage, feedback));
        CalculateConstantBufferParams(offset, clampedSize, firstConstantOut, numConstantsOut);
    }
    else
    {
        ANGLE_TRY(
            getConstantBufferRangeStorage(context, offset, clampedSize, &bufferStorage, feedback));
        *firstConstantOut = 0;
        *numConstantsOut  = 0;
    }

    *bufferOut = &bufferStorage->getBuffer();
    return angle::Result::Continue;
}

angle::Result Buffer11::markRawBufferUsage(const gl::Context *context, BufferFeedback *feedback)
{
    ANGLE_TRY(markBufferUsage(context, BUFFER_USAGE_RAW_UAV, feedback));
    return angle::Result::Continue;
}

angle::Result Buffer11::markTypedBufferUsage(const gl::Context *context, BufferFeedback *feedback)
{
    ANGLE_TRY(markBufferUsage(context, BUFFER_USAGE_TYPED_UAV, feedback));
    return angle::Result::Continue;
}

angle::Result Buffer11::getRawUAVRange(const gl::Context *context,
                                       GLintptr offset,
                                       GLsizeiptr size,
                                       d3d11::UnorderedAccessView **uavOut,
                                       BufferFeedback *feedback)
{
    NativeStorage *nativeStorage = nullptr;
    ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_RAW_UAV, &nativeStorage, feedback));

    return nativeStorage->getRawUAV(context, static_cast<unsigned int>(offset),
                                    static_cast<unsigned int>(size), uavOut);
}

angle::Result Buffer11::getSRV(const gl::Context *context,
                               DXGI_FORMAT srvFormat,
                               const d3d11::ShaderResourceView **srvOut,
                               BufferFeedback *feedback)
{
    NativeStorage *nativeStorage = nullptr;
    ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_PIXEL_UNPACK, &nativeStorage, feedback));
    return nativeStorage->getSRVForFormat(context, srvFormat, srvOut);
}

angle::Result Buffer11::packPixels(const gl::Context *context,
                                   const gl::FramebufferAttachment &readAttachment,
                                   const PackPixelsParams &params,
                                   BufferFeedback *feedback)
{
    PackStorage *packStorage = nullptr;
    ANGLE_TRY(getBufferStorage(context, BUFFER_USAGE_PIXEL_PACK, &packStorage, feedback));

    ASSERT(packStorage);
    ANGLE_TRY(packStorage->packPixels(context, readAttachment, params));
    onStorageUpdate(packStorage);

    // Pixel pack writes new data into this buffer. If the buffer is also bound as a vertex or
    // index buffer, we must invalidate any cached static translated vertex/index buffers and set
    // feedback->bufferStateChanged so bound VertexArrays re-sync on the next draw.
    invalidateStaticData(context, feedback);

    return angle::Result::Continue;
}

size_t Buffer11::getTotalCPUBufferMemoryBytes() const
{
    size_t allocationSize = 0;

    BufferStorage *staging = mBufferStorages[BUFFER_USAGE_STAGING];
    allocationSize += staging ? staging->getSize() : 0;

    BufferStorage *sysMem = mBufferStorages[BUFFER_USAGE_SYSTEM_MEMORY];
    allocationSize += sysMem ? sysMem->getSize() : 0;

    return allocationSize;
}

template <typename StorageOutT>
angle::Result Buffer11::getBufferStorage(const gl::Context *context,
                                         BufferUsage usage,
                                         StorageOutT **storageOut,
                                         BufferFeedback *feedback)
{
    ASSERT(0 <= usage && usage < BUFFER_USAGE_COUNT);
    // Structured buffers require a shader-dependent structureByteStride (via
    // resizeStructuredBuffer) and are managed exclusively in mStructuredBufferRangeStoragesCache.
    ASSERT(usage != BUFFER_USAGE_STRUCTURED);
    BufferStorage *&newStorage = mBufferStorages[usage];

    if (!newStorage)
    {
        newStorage = allocateStorage(usage);
    }

    markBufferUsage(usage);

    // resize buffer
    if (newStorage->getSize() < mSize)
    {
        ANGLE_TRY(newStorage->resize(context, mSize, true, feedback));
    }

    ASSERT(newStorage);

    ANGLE_TRY(updateBufferStorage(context, newStorage, 0, mSize, feedback));
    ANGLE_TRY(garbageCollection(context, usage));

    *storageOut = GetAs<StorageOutT>(newStorage);
    return angle::Result::Continue;
}

Buffer11::BufferStorage *Buffer11::allocateStorage(BufferUsage usage)
{
    updateDeallocThreshold(usage);
    switch (usage)
    {
        case BUFFER_USAGE_PIXEL_PACK:
            return new PackStorage(mRenderer);
        case BUFFER_USAGE_SYSTEM_MEMORY:
            return new SystemMemoryStorage(mRenderer);
        case BUFFER_USAGE_INDEX:
        case BUFFER_USAGE_VERTEX_OR_TRANSFORM_FEEDBACK:
            return new NativeStorage(mRenderer, usage);
        case BUFFER_USAGE_STRUCTURED:
            return new StructuredBufferStorage(mRenderer, usage);
        default:
            return new NativeStorage(mRenderer, usage);
    }
}

angle::Result Buffer11::getConstantBufferRangeStorage(const gl::Context *context,
                                                      GLintptr offset,
                                                      GLsizeiptr size,
                                                      Buffer11::NativeStorage **storageOut,
                                                      BufferFeedback *feedback)
{
    BufferStorage *newStorage;
    {
        // Keep the cacheEntry in a limited scope because it may be invalidated later in the code if
        // we need to reclaim some space.
        ConstantBufferKey constantBufferKey(offset, size);
        BufferCacheEntry *cacheEntry = &mConstantBufferRangeStoragesCache[constantBufferKey];

        if (!cacheEntry->storage)
        {
            cacheEntry->storage  = allocateStorage(BUFFER_USAGE_UNIFORM);
            cacheEntry->lruCount = ++mMaxConstantBufferLruCount;
        }

        cacheEntry->lruCount = ++mMaxConstantBufferLruCount;
        newStorage           = cacheEntry->storage;
    }

    markBufferUsage(BUFFER_USAGE_UNIFORM);

    if (newStorage->getSize() < static_cast<size_t>(size))
    {
        size_t maximumAllowedAdditionalSize = 2 * getSize();

        const size_t alignedSize =
            rx::roundUpPow2(static_cast<size_t>(size), static_cast<size_t>(16));
        size_t sizeDelta = alignedSize - newStorage->getSize();

        while (mConstantBufferStorageAdditionalSize + sizeDelta > maximumAllowedAdditionalSize)
        {
            auto iter = std::min_element(
                std::begin(mConstantBufferRangeStoragesCache),
                std::end(mConstantBufferRangeStoragesCache),
                [](const BufferCache::value_type &a, const BufferCache::value_type &b) {
                    return a.second.lruCount < b.second.lruCount;
                });

            // Don't remove the newly added storage. This can only happen if it is the last entry
            // since it has the most recent LRU value.
            if (iter->second.storage == newStorage)
            {
                ASSERT(mConstantBufferRangeStoragesCache.size() == 1);
                break;
            }

            ASSERT(mConstantBufferStorageAdditionalSize >= iter->second.storage->getSize());

            mConstantBufferStorageAdditionalSize -= iter->second.storage->getSize();
            SafeDelete(iter->second.storage);
            mConstantBufferRangeStoragesCache.erase(iter);
        }

        ANGLE_TRY(newStorage->resize(context, size, false, feedback));
        mConstantBufferStorageAdditionalSize += sizeDelta;

        // Zero-initialize the newly resized range storage in case mLatestBufferStorage is null or
        // empty (where updateBufferStorage returns early without calling copyFromStorage).
        ANGLE_TRY(newStorage->setData(context, nullptr, 0, 0));

        // We don't copy the old data when resizing the constant buffer because the data may be
        // out-of-date therefore we reset the data revision and let updateBufferStorage() handle the
        // copy.
        newStorage->setDataRevision(0);
    }

    ANGLE_TRY(updateBufferStorage(context, newStorage, offset, size, feedback));
    ANGLE_TRY(garbageCollection(context, BUFFER_USAGE_UNIFORM));
    *storageOut = GetAs<NativeStorage>(newStorage);
    return angle::Result::Continue;
}

angle::Result Buffer11::getStructuredBufferRangeSRV(const gl::Context *context,
                                                    unsigned int offset,
                                                    unsigned int size,
                                                    unsigned int structureByteStride,
                                                    const d3d11::ShaderResourceView **srvOut,
                                                    BufferFeedback *feedback)
{
    BufferStorage *newStorage;

    {
        // Keep the cacheEntry in a limited scope because it may be invalidated later in the code if
        // we need to reclaim some space.
        StructuredBufferKey structuredBufferKey(offset, size, structureByteStride);
        BufferCacheEntry *cacheEntry = &mStructuredBufferRangeStoragesCache[structuredBufferKey];

        if (!cacheEntry->storage)
        {
            cacheEntry->storage  = allocateStorage(BUFFER_USAGE_STRUCTURED);
            cacheEntry->lruCount = ++mMaxStructuredBufferLruCount;
        }

        cacheEntry->lruCount = ++mMaxStructuredBufferLruCount;
        newStorage           = cacheEntry->storage;
    }

    StructuredBufferStorage *structuredBufferStorage = GetAs<StructuredBufferStorage>(newStorage);

    markBufferUsage(BUFFER_USAGE_STRUCTURED);

    if (newStorage->getSize() < static_cast<size_t>(size))
    {
        size_t maximumAllowedAdditionalSize = 2 * getSize();

        size_t sizeDelta = static_cast<size_t>(size) - newStorage->getSize();

        while (mStructuredBufferStorageAdditionalSize + sizeDelta > maximumAllowedAdditionalSize)
        {
            auto iter = std::min_element(std::begin(mStructuredBufferRangeStoragesCache),
                                         std::end(mStructuredBufferRangeStoragesCache),
                                         [](const StructuredBufferCache::value_type &a,
                                            const StructuredBufferCache::value_type &b) {
                                             return a.second.lruCount < b.second.lruCount;
                                         });

            // Don't remove the newly added storage. This can only happen if it is the last entry
            // since it has the most recent LRU value.
            if (iter->second.storage == newStorage)
            {
                ASSERT(mStructuredBufferRangeStoragesCache.size() == 1);
                break;
            }

            ASSERT(mStructuredBufferStorageAdditionalSize >= iter->second.storage->getSize());

            mStructuredBufferStorageAdditionalSize -= iter->second.storage->getSize();
            SafeDelete(iter->second.storage);
            mStructuredBufferRangeStoragesCache.erase(iter);
        }

        ANGLE_TRY(structuredBufferStorage->resizeStructuredBuffer(context, size,
                                                                  structureByteStride, feedback));
        mStructuredBufferStorageAdditionalSize += sizeDelta;

        // We don't copy the old data when resizing the structured buffer because the data may be
        // out-of-date therefore we reset the data revision and let updateBufferStorage() handle the
        // copy.
        newStorage->setDataRevision(0);
    }

    ANGLE_TRY(
        updateBufferStorage(context, newStorage, offset, static_cast<size_t>(size), feedback));
    ANGLE_TRY(garbageCollection(context, BUFFER_USAGE_STRUCTURED));
    ANGLE_TRY(structuredBufferStorage->getStructuredBufferRangeSRV(context, offset, size,
                                                                   structureByteStride, srvOut));
    return angle::Result::Continue;
}

angle::Result Buffer11::updateBufferStorage(const gl::Context *context,
                                            BufferStorage *storage,
                                            size_t sourceOffset,
                                            size_t storageSize,
                                            BufferFeedback *feedback)
{
    BufferStorage *latestBuffer = nullptr;
    ANGLE_TRY(getLatestBufferStorage(context, &latestBuffer, feedback));

    ASSERT(storage);

    if (!latestBuffer)
    {
        // updateBufferStorage is called by getBufferStorage (for canonical full-buffer storages in
        // mBufferStorages) as well as getConstantBufferRangeStorage and
        // getStructuredBufferRangeSRV (for sub-range cache entries stored in
        // mConstantBufferRangeStoragesCache and mStructuredBufferRangeStoragesCache).
        //
        // When mLatestBufferStorage is nullptr (e.g., after glBufferData with data == nullptr), we
        // must only promote |storage| to mLatestBufferStorage if:
        // 1. mBufferStorages[storage->getUsage()] == storage: |storage| is a canonical storage in
        //    mBufferStorages and not a range-cache entry (range-cache entries only cover a
        //    sub-range, can be freed by LRU eviction without checking mLatestBufferStorage, and
        //    StructuredBufferStorage cannot be resized via NativeStorage::resize).
        // 2. storage->getSize() >= mSize: |storage| covers the full buffer size. For
        //    BUFFER_USAGE_UNIFORM on D3D11.0 devices (!supportsConstantBufferOffsets),
        //    FillBufferDesc clamps ByteWidth to maxUniformBlockSize. If
        //    mSize > maxUniformBlockSize, a clamped uniform storage has getSize() < mSize and would
        //    cause getLatestBufferStorage to repeatedly reallocate the buffer on every call while
        //    truncating data above maxUniformBlockSize.
        if (mBufferStorages[storage->getUsage()] == storage && storage->getSize() >= mSize)
        {
            onStorageUpdate(storage);
        }
        return angle::Result::Continue;
    }

    if (latestBuffer->getDataRevision() <= storage->getDataRevision())
    {
        return angle::Result::Continue;
    }

    if (latestBuffer->getSize() == 0 || storage->getSize() == 0)
    {
        return angle::Result::Continue;
    }

    // Copy through a staging buffer if we're copying from or to a non-staging, mappable
    // buffer storage. This is because we can't map a GPU buffer, and copy CPU
    // data directly. If we're already using a staging buffer we're fine.
    if (latestBuffer->getUsage() != BUFFER_USAGE_STAGING &&
        storage->getUsage() != BUFFER_USAGE_STAGING &&
        (!latestBuffer->isCPUAccessible(GL_MAP_READ_BIT) ||
         !storage->isCPUAccessible(GL_MAP_WRITE_BIT)))
    {
        NativeStorage *stagingBuffer = nullptr;
        ANGLE_TRY(getStagingStorage(context, &stagingBuffer));

        CopyResult copyResult = CopyResult::NOT_RECREATED;
        ANGLE_TRY(stagingBuffer->copyFromStorage(context, latestBuffer, 0, latestBuffer->getSize(),
                                                 0, &copyResult, feedback));
        onCopyStorage(stagingBuffer, latestBuffer);

        latestBuffer = stagingBuffer;
    }

    CopyResult copyResult = CopyResult::NOT_RECREATED;
    ANGLE_TRY(storage->copyFromStorage(context, latestBuffer, sourceOffset, storageSize, 0,
                                       &copyResult, feedback));
    // If the D3D buffer has been recreated, we should update our serial.
    if (copyResult == CopyResult::RECREATED)
    {
        updateSerial();
    }
    onCopyStorage(storage, latestBuffer);
    return angle::Result::Continue;
}

angle::Result Buffer11::getLatestBufferStorage(const gl::Context *context,
                                               Buffer11::BufferStorage **storageOut,
                                               BufferFeedback *feedback) const
{
    // resize buffer
    if (mLatestBufferStorage && mLatestBufferStorage->getSize() < mSize)
    {
        ANGLE_TRY(mLatestBufferStorage->resize(context, mSize, true, feedback));
    }

    *storageOut = mLatestBufferStorage;
    return angle::Result::Continue;
}

template <typename StorageOutT>
angle::Result Buffer11::getStagingStorage(const gl::Context *context, StorageOutT **storageOut)
{
    BufferFeedback feedback;
    return getBufferStorage(context, BUFFER_USAGE_STAGING, storageOut, &feedback);
}

size_t Buffer11::getSize() const
{
    return mSize;
}

bool Buffer11::supportsDirectBinding() const
{
    // Do not support direct buffers for dynamic data. The streaming buffer
    // offers better performance for data which changes every frame.
    return (mUsage == D3DBufferUsage::STATIC);
}

void Buffer11::initializeStaticData(const gl::Context *context, BufferFeedback *feedback)
{
    BufferD3D::initializeStaticData(context, feedback);
    feedback->bufferStateChanged = true;
}

void Buffer11::invalidateStaticData(const gl::Context *context, BufferFeedback *feedback)
{
    BufferD3D::invalidateStaticData(context, feedback);
    feedback->bufferStateChanged = true;
}

void Buffer11::onCopyStorage(BufferStorage *dest, BufferStorage *source)
{
    ASSERT(source && mLatestBufferStorage);
    dest->setDataRevision(source->getDataRevision());

    // Only update the latest buffer storage if our usage index is lower. See comment in header.
    if (dest->getUsage() < mLatestBufferStorage->getUsage())
    {
        mLatestBufferStorage = dest;
    }
}

void Buffer11::onStorageUpdate(BufferStorage *updatedStorage)
{
    // StructuredBufferStorage instances live exclusively in mStructuredBufferRangeStoragesCache as
    // downstream range caches (which may only cover a sub-range of the buffer and can be evicted by
    // LRU). They cannot be read back or copied from (copyFromStorage only supports
    // SystemMemoryStorage and NativeStorage sources), so they must never become
    // mLatestBufferStorage.
    ASSERT(updatedStorage->getUsage() != BUFFER_USAGE_STRUCTURED);
    updatedStorage->setDataRevision(updatedStorage->getDataRevision() + 1);
    mLatestBufferStorage = updatedStorage;
}

// Buffer11::BufferStorage implementation
Buffer11::BufferStorage::BufferStorage(Renderer11 *renderer, BufferUsage usage)
    : mRenderer(renderer), mRevision(0), mUsage(usage), mBufferSize(0)
{}

angle::Result Buffer11::BufferStorage::setData(const gl::Context *context,
                                               const uint8_t *data,
                                               size_t offset,
                                               size_t size)
{
    ASSERT(isCPUAccessible(GL_MAP_WRITE_BIT));

    // Uniform storage can have a different internal size than the buffer size. Ensure we don't
    // overflow.
    const size_t copySize = std::min(size, mBufferSize - offset);

    // Uniform and structured buffers are D3D11_USAGE_DYNAMIC and mapped with
    // D3D11_MAP_WRITE_DISCARD, which discards the entire buffer and returns fresh uninitialized
    // memory. Therefore, when copySize < mBufferSize (e.g., due to constant buffer alignment
    // padding or a range binding extending past the source buffer), we must map up to the
    // end of the buffer and zero-fill the remaining tail bytes. Non-discard storages
    // (SYSTEM_MEMORY, STAGING, PIXEL_PACK) preserve untouched bytes and only map copySize bytes.
    const bool zeroTail  = (mUsage == BUFFER_USAGE_UNIFORM || mUsage == BUFFER_USAGE_STRUCTURED);
    const size_t mapSize = zeroTail ? (mBufferSize - offset) : copySize;
    if (mapSize == 0)
    {
        return angle::Result::Continue;
    }

    uint8_t *writePointer = nullptr;
    ANGLE_TRY(map(context, offset, mapSize, GL_MAP_WRITE_BIT, &writePointer));

    if (copySize > 0)
    {
        memcpy(writePointer, data, copySize);
    }

    if (zeroTail)
    {
        ASSERT(offset == 0);
        if (copySize < mapSize)
        {
            memset(writePointer + copySize, 0, mapSize - copySize);
        }
    }

    unmap();

    return angle::Result::Continue;
}

// Buffer11::NativeStorage implementation

Buffer11::NativeStorage::NativeStorage(Renderer11 *renderer, BufferUsage usage)
    : BufferStorage(renderer, usage), mBuffer()
{}

Buffer11::NativeStorage::~NativeStorage()
{
    clearSRVs();
    clearUAVs();
}

bool Buffer11::NativeStorage::isCPUAccessible(GLbitfield access) const
{
    if ((access & GL_MAP_READ_BIT) != 0)
    {
        // Read is more exclusive than write mappability.
        return (mUsage == BUFFER_USAGE_STAGING);
    }
    ASSERT((access & GL_MAP_WRITE_BIT) != 0);
    return (mUsage == BUFFER_USAGE_STAGING || mUsage == BUFFER_USAGE_UNIFORM ||
            mUsage == BUFFER_USAGE_STRUCTURED);
}

// Returns true if it recreates the direct buffer
angle::Result Buffer11::NativeStorage::copyFromStorage(const gl::Context *context,
                                                       BufferStorage *source,
                                                       size_t sourceOffset,
                                                       size_t size,
                                                       size_t destOffset,
                                                       CopyResult *resultOut,
                                                       BufferFeedback *feedback)
{
    size_t requiredSize = destOffset + size;

    // (Re)initialize D3D buffer if needed
    bool preserveData = (destOffset > 0);
    if (!mBuffer.valid() || mBufferSize < requiredSize)
    {
        ANGLE_TRY(resize(context, requiredSize, preserveData, feedback));
        *resultOut = CopyResult::RECREATED;
    }
    else
    {
        *resultOut = CopyResult::NOT_RECREATED;
    }

    // Clamp the copy size to both the available source bytes and destination capacity:
    // - sourceOffset + size can exceed source->getSize() when a shader uniform/structured block or
    //   glBindBufferRange window is larger than the source storage.
    // - destOffset + size can exceed mBufferSize when BUFFER_USAGE_UNIFORM is capped at
    //   maxUniformBlockSize in FillBufferDesc.
    size_t sourceAvailable =
        (sourceOffset < source->getSize()) ? (source->getSize() - sourceOffset) : 0;
    size_t destAvailable = (destOffset < mBufferSize) ? (mBufferSize - destOffset) : 0;
    size_t clampedSize   = std::min({size, sourceAvailable, destAvailable});

    if (clampedSize == 0)
    {
        // If no source bytes are available (e.g., sourceOffset >= source->getSize()), uniform and
        // structured buffers must still be zero-filled via setData so the shader does not read
        // uninitialized D3D11 memory.
        if (mUsage == BUFFER_USAGE_UNIFORM || mUsage == BUFFER_USAGE_STRUCTURED)
        {
            ANGLE_TRY(setData(context, nullptr, destOffset, 0));
        }
        return angle::Result::Continue;
    }

    // Route uniform and structured buffers with clampedSize < mBufferSize through CPU map + setData
    // instead of GPU CopySubresourceRegion. CopySubresourceRegion can only copy clampedSize bytes
    // from the source buffer (since srcBox must fit within source), which would leave the remaining
    // [clampedSize, mBufferSize) tail bytes uninitialized in a newly allocated or previously
    // discarded buffer. setData maps with D3D11_MAP_WRITE_DISCARD and zero-fills the tail.
    if (source->getUsage() == BUFFER_USAGE_PIXEL_PACK ||
        source->getUsage() == BUFFER_USAGE_SYSTEM_MEMORY ||
        ((mUsage == BUFFER_USAGE_UNIFORM || mUsage == BUFFER_USAGE_STRUCTURED) &&
         clampedSize < mBufferSize))
    {
        ASSERT(source->isCPUAccessible(GL_MAP_READ_BIT) && isCPUAccessible(GL_MAP_WRITE_BIT));

        // Uniform and structured buffers must be mapped with write/discard.
        ASSERT(!(preserveData &&
                 (mUsage == BUFFER_USAGE_UNIFORM || mUsage == BUFFER_USAGE_STRUCTURED)));

        uint8_t *sourcePointer = nullptr;
        ANGLE_TRY(source->map(context, sourceOffset, clampedSize, GL_MAP_READ_BIT, &sourcePointer));

        auto err = setData(context, sourcePointer, destOffset, clampedSize);
        source->unmap();
        ANGLE_TRY(err);
    }
    else
    {
        D3D11_BOX srcBox;
        srcBox.left   = static_cast<unsigned int>(sourceOffset);
        srcBox.right  = static_cast<unsigned int>(sourceOffset + clampedSize);
        srcBox.top    = 0;
        srcBox.bottom = 1;
        srcBox.front  = 0;
        srcBox.back   = 1;

        const d3d11::Buffer *sourceBuffer = &GetAs<NativeStorage>(source)->getBuffer();

        ID3D11DeviceContext *deviceContext = mRenderer->getDeviceContext();
        deviceContext->CopySubresourceRegion(mBuffer.get(), 0,
                                             static_cast<unsigned int>(destOffset), 0, 0,
                                             sourceBuffer->get(), 0, &srcBox);
    }

    return angle::Result::Continue;
}

angle::Result Buffer11::NativeStorage::resize(const gl::Context *context,
                                              size_t size,
                                              bool preserveData,
                                              BufferFeedback *feedback)
{
    if (size == 0)
    {
        mBuffer.reset();
        mBufferSize = 0;
        return angle::Result::Continue;
    }

    D3D11_BUFFER_DESC bufferDesc;
    FillBufferDesc(&bufferDesc, mRenderer, mUsage, static_cast<unsigned int>(size));

    d3d11::Buffer newBuffer;
    ANGLE_TRY(
        mRenderer->allocateResource(SafeGetImplAs<Context11>(context), bufferDesc, &newBuffer));
    newBuffer.setInternalName("Buffer11::NativeStorage");

    if (mBuffer.valid() && preserveData)
    {
        // We don't call resize if the buffer is big enough already.
        ASSERT(mBufferSize <= size);

        D3D11_BOX srcBox;
        srcBox.left   = 0;
        srcBox.right  = static_cast<unsigned int>(mBufferSize);
        srcBox.top    = 0;
        srcBox.bottom = 1;
        srcBox.front  = 0;
        srcBox.back   = 1;

        ID3D11DeviceContext *deviceContext = mRenderer->getDeviceContext();
        deviceContext->CopySubresourceRegion(newBuffer.get(), 0, 0, 0, 0, mBuffer.get(), 0,
                                             &srcBox);
    }

    // No longer need the old buffer
    mBuffer = std::move(newBuffer);

    mBufferSize = bufferDesc.ByteWidth;

    // Free the SRVs.
    clearSRVs();

    // Free the UAVs.
    clearUAVs();

    feedback->bufferStateChanged = true;

    return angle::Result::Continue;
}

// static
void Buffer11::NativeStorage::FillBufferDesc(D3D11_BUFFER_DESC *bufferDesc,
                                             Renderer11 *renderer,
                                             BufferUsage usage,
                                             unsigned int bufferSize)
{
    bufferDesc->ByteWidth           = bufferSize;
    bufferDesc->MiscFlags           = 0;
    bufferDesc->StructureByteStride = 0;

    switch (usage)
    {
        case BUFFER_USAGE_STAGING:
            bufferDesc->Usage          = D3D11_USAGE_STAGING;
            bufferDesc->BindFlags      = 0;
            bufferDesc->CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            break;

        case BUFFER_USAGE_VERTEX_OR_TRANSFORM_FEEDBACK:
            bufferDesc->Usage     = D3D11_USAGE_DEFAULT;
            bufferDesc->BindFlags = D3D11_BIND_VERTEX_BUFFER;

            if (renderer->isES3Capable())
            {
                bufferDesc->BindFlags |= D3D11_BIND_STREAM_OUTPUT;
            }

            bufferDesc->CPUAccessFlags = 0;
            break;

        case BUFFER_USAGE_INDEX:
            bufferDesc->Usage          = D3D11_USAGE_DEFAULT;
            bufferDesc->BindFlags      = D3D11_BIND_INDEX_BUFFER;
            bufferDesc->CPUAccessFlags = 0;
            break;

        case BUFFER_USAGE_INDIRECT:
            bufferDesc->MiscFlags      = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            bufferDesc->Usage          = D3D11_USAGE_DEFAULT;
            bufferDesc->BindFlags      = 0;
            bufferDesc->CPUAccessFlags = 0;
            break;

        case BUFFER_USAGE_PIXEL_UNPACK:
            bufferDesc->Usage          = D3D11_USAGE_DEFAULT;
            bufferDesc->BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            bufferDesc->CPUAccessFlags = 0;
            break;

        case BUFFER_USAGE_UNIFORM:
            bufferDesc->Usage          = D3D11_USAGE_DYNAMIC;
            bufferDesc->BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc->CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            // Constant buffers must be of a limited size, and aligned to 16 byte boundaries
            // For our purposes we ignore any buffer data past the maximum constant buffer size
            bufferDesc->ByteWidth = roundUpPow2(bufferDesc->ByteWidth, 16u);

            // Note: it seems that D3D11 allows larger buffers on some platforms, but not all.
            // (Windows 10 seems to allow larger constant buffers, but not Windows 7)
            if (!renderer->getRenderer11DeviceCaps().supportsConstantBufferOffsets)
            {
                bufferDesc->ByteWidth = std::min<UINT>(
                    bufferDesc->ByteWidth,
                    static_cast<UINT>(renderer->getNativeCaps().maxUniformBlockSize));
            }
            break;

        case BUFFER_USAGE_RAW_UAV:
            bufferDesc->MiscFlags      = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            bufferDesc->BindFlags      = D3D11_BIND_UNORDERED_ACCESS;
            bufferDesc->Usage          = D3D11_USAGE_DEFAULT;
            bufferDesc->CPUAccessFlags = 0;
            break;
        case BUFFER_USAGE_TYPED_UAV:
            bufferDesc->BindFlags      = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            bufferDesc->Usage          = D3D11_USAGE_DEFAULT;
            bufferDesc->CPUAccessFlags = 0;
            bufferDesc->MiscFlags      = 0;
            break;

        default:
            UNREACHABLE();
    }
}

angle::Result Buffer11::NativeStorage::map(const gl::Context *context,
                                           size_t offset,
                                           size_t length,
                                           GLbitfield access,
                                           uint8_t **mapPointerOut)
{
    ASSERT(isCPUAccessible(access));

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    D3D11_MAP d3dMapType = gl_d3d11::GetD3DMapTypeFromBits(mUsage, access);

    ANGLE_TRY(mRenderer->mapResource(context, mBuffer.get(), 0, d3dMapType, 0, &mappedResource));
    ASSERT(mappedResource.pData);
    *mapPointerOut = static_cast<uint8_t *>(mappedResource.pData) + offset;
    return angle::Result::Continue;
}

void Buffer11::NativeStorage::unmap()
{
    ASSERT(isCPUAccessible(GL_MAP_WRITE_BIT) || isCPUAccessible(GL_MAP_READ_BIT));
    ID3D11DeviceContext *context = mRenderer->getDeviceContext();
    context->Unmap(mBuffer.get(), 0);
}

angle::Result Buffer11::NativeStorage::getSRVForFormat(const gl::Context *context,
                                                       DXGI_FORMAT srvFormat,
                                                       const d3d11::ShaderResourceView **srvOut)
{
    auto bufferSRVIt = mBufferResourceViews.find(srvFormat);

    if (bufferSRVIt != mBufferResourceViews.end())
    {
        *srvOut = &bufferSRVIt->second;
        return angle::Result::Continue;
    }

    const d3d11::DXGIFormatSize &dxgiFormatInfo = d3d11::GetDXGIFormatSizeInfo(srvFormat);

    D3D11_SHADER_RESOURCE_VIEW_DESC bufferSRVDesc;
    bufferSRVDesc.Buffer.ElementOffset = 0;
    bufferSRVDesc.Buffer.ElementWidth  = static_cast<UINT>(mBufferSize) / dxgiFormatInfo.pixelBytes;
    bufferSRVDesc.ViewDimension        = D3D11_SRV_DIMENSION_BUFFER;
    bufferSRVDesc.Format               = srvFormat;

    ANGLE_TRY(mRenderer->allocateResource(GetImplAs<Context11>(context), bufferSRVDesc,
                                          mBuffer.get(), &mBufferResourceViews[srvFormat]));

    *srvOut = &mBufferResourceViews[srvFormat];
    return angle::Result::Continue;
}

angle::Result Buffer11::NativeStorage::getRawUAV(const gl::Context *context,
                                                 unsigned int offset,
                                                 unsigned int size,
                                                 d3d11::UnorderedAccessView **uavOut)
{
    ASSERT(offset + size <= mBufferSize);

    auto bufferRawUAV = mBufferRawUAVs.find({offset, size});
    if (bufferRawUAV != mBufferRawUAVs.end())
    {
        *uavOut = &bufferRawUAV->second;
        return angle::Result::Continue;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC bufferUAVDesc;

    // DXGI_FORMAT_R32_TYPELESS uses 4 bytes per element
    constexpr int kBytesToElement     = 4;
    bufferUAVDesc.Buffer.FirstElement = offset / kBytesToElement;
    bufferUAVDesc.Buffer.NumElements  = size / kBytesToElement;
    bufferUAVDesc.Buffer.Flags        = D3D11_BUFFER_UAV_FLAG_RAW;
    bufferUAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;  // Format must be DXGI_FORMAT_R32_TYPELESS,
                                                      // when creating Raw Unordered Access View
    bufferUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

    ANGLE_TRY(mRenderer->allocateResource(GetImplAs<Context11>(context), bufferUAVDesc,
                                          mBuffer.get(), &mBufferRawUAVs[{offset, size}]));
    *uavOut = &mBufferRawUAVs[{offset, size}];
    return angle::Result::Continue;
}

void Buffer11::NativeStorage::clearSRVs()
{
    mBufferResourceViews.clear();
}

void Buffer11::NativeStorage::clearUAVs()
{
    mBufferRawUAVs.clear();
}

Buffer11::StructuredBufferStorage::StructuredBufferStorage(Renderer11 *renderer, BufferUsage usage)
    : NativeStorage(renderer, usage), mStructuredBufferResourceView()
{}

Buffer11::StructuredBufferStorage::~StructuredBufferStorage()
{
    mStructuredBufferResourceView.reset();
}

angle::Result Buffer11::StructuredBufferStorage::resizeStructuredBuffer(
    const gl::Context *context,
    unsigned int size,
    unsigned int structureByteStride,
    BufferFeedback *feedback)
{
    if (size == 0)
    {
        mBuffer.reset();
        mBufferSize = 0;
        return angle::Result::Continue;
    }

    D3D11_BUFFER_DESC bufferDesc;
    bufferDesc.ByteWidth           = size;
    bufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = structureByteStride;
    bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;

    d3d11::Buffer newBuffer;
    ANGLE_TRY(
        mRenderer->allocateResource(SafeGetImplAs<Context11>(context), bufferDesc, &newBuffer));
    newBuffer.setInternalName("Buffer11::StructuredBufferStorage");

    // No longer need the old buffer
    mBuffer = std::move(newBuffer);

    mBufferSize = static_cast<size_t>(bufferDesc.ByteWidth);

    mStructuredBufferResourceView.reset();

    feedback->bufferStateChanged = true;

    return angle::Result::Continue;
}

angle::Result Buffer11::StructuredBufferStorage::getStructuredBufferRangeSRV(
    const gl::Context *context,
    unsigned int offset,
    unsigned int size,
    unsigned int structureByteStride,
    const d3d11::ShaderResourceView **srvOut)
{
    if (mStructuredBufferResourceView.valid())
    {
        *srvOut = &mStructuredBufferResourceView;
        return angle::Result::Continue;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC bufferSRVDesc = {};
    bufferSRVDesc.BufferEx.NumElements = structureByteStride == 0u ? 1 : size / structureByteStride;
    bufferSRVDesc.BufferEx.FirstElement = 0;
    bufferSRVDesc.BufferEx.Flags        = 0;
    bufferSRVDesc.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
    bufferSRVDesc.Format                = DXGI_FORMAT_UNKNOWN;

    ANGLE_TRY(mRenderer->allocateResource(GetImplAs<Context11>(context), bufferSRVDesc,
                                          mBuffer.get(), &mStructuredBufferResourceView));

    *srvOut = &mStructuredBufferResourceView;
    return angle::Result::Continue;
}

// Buffer11::PackStorage implementation

Buffer11::PackStorage::PackStorage(Renderer11 *renderer)
    : BufferStorage(renderer, BUFFER_USAGE_PIXEL_PACK), mStagingTexture(), mDataModified(false)
{}

Buffer11::PackStorage::~PackStorage() {}

angle::Result Buffer11::PackStorage::copyFromStorage(const gl::Context *context,
                                                     BufferStorage *source,
                                                     size_t sourceOffset,
                                                     size_t size,
                                                     size_t destOffset,
                                                     CopyResult *resultOut,
                                                     BufferFeedback *feedback)
{
    ANGLE_TRY(flushQueuedPackCommand(context));

    // For all use cases of pack buffers, we must copy through a readable buffer.
    ASSERT(source->isCPUAccessible(GL_MAP_READ_BIT));
    uint8_t *sourceData = nullptr;
    ANGLE_TRY(source->map(context, sourceOffset, size, GL_MAP_READ_BIT, &sourceData));
    ASSERT(destOffset + size <= mMemoryBuffer.size());
    memcpy(mMemoryBuffer.data() + destOffset, sourceData, size);
    source->unmap();
    *resultOut = CopyResult::NOT_RECREATED;
    return angle::Result::Continue;
}

angle::Result Buffer11::PackStorage::resize(const gl::Context *context,
                                            size_t size,
                                            bool preserveData,
                                            BufferFeedback *feedback)
{
    if (size != mBufferSize)
    {
        Context11 *context11 = GetImplAs<Context11>(context);
        ANGLE_CHECK_GL_ALLOC(context11, mMemoryBuffer.resize(size));
        mBufferSize = size;
    }

    return angle::Result::Continue;
}

angle::Result Buffer11::PackStorage::map(const gl::Context *context,
                                         size_t offset,
                                         size_t length,
                                         GLbitfield access,
                                         uint8_t **mapPointerOut)
{
    ASSERT(offset + length <= getSize());
    // TODO: fast path
    //  We might be able to optimize out one or more memcpy calls by detecting when
    //  and if D3D packs the staging texture memory identically to how we would fill
    //  the pack buffer according to the current pack state.

    ANGLE_TRY(flushQueuedPackCommand(context));

    mDataModified = (mDataModified || (access & GL_MAP_WRITE_BIT) != 0);

    *mapPointerOut = mMemoryBuffer.data() + offset;
    return angle::Result::Continue;
}

void Buffer11::PackStorage::unmap()
{
    // No-op
}

angle::Result Buffer11::PackStorage::packPixels(const gl::Context *context,
                                                const gl::FramebufferAttachment &readAttachment,
                                                const PackPixelsParams &params)
{
    ANGLE_TRY(flushQueuedPackCommand(context));

    RenderTarget11 *renderTarget = nullptr;
    ANGLE_TRY(readAttachment.getRenderTarget(context, 0, &renderTarget));

    gl::Rectangle clippedReadArea;
    bool reverseRowOrder     = false;
    const bool invertTexture = UsePresentPathFast(mRenderer, &readAttachment);
    if (!ClipRenderTargetReadArea(*renderTarget, params.area, invertTexture, params.reverseRowOrder,
                                  &clippedReadArea, &reverseRowOrder))
    {
        return angle::Result::Continue;
    }

    const TextureHelper11 *srcTexture = &renderTarget->getTexture();
    unsigned int srcSubresource       = renderTarget->getSubresourceIndex();

    TextureHelper11 resolvedTexture;
    if (srcTexture->getSampleCount() > 1)
    {
        ANGLE_TRY(mRenderer->resolveMultisampledTexture(context, renderTarget, false, false,
                                                        &resolvedTexture));
        srcTexture     = &resolvedTexture;
        srcSubresource = 0;
    }

    mQueuedPackCommand.reset(new PackPixelsParams(clippedReadArea, *params.destFormat,
                                                  params.outputPitch, reverseRowOrder,
                                                  params.packBuffer, params.offset));

    const gl::Extents stagingTextureSize(clippedReadArea.width, clippedReadArea.height, 1);
    if (!mStagingTexture.get() || mStagingTexture.getFormat() != srcTexture->getFormat() ||
        mStagingTexture.getExtents() != stagingTextureSize ||
        mStagingTexture.getTextureType() != srcTexture->getTextureType())
    {
        mStagingTexture.reset();
        ANGLE_TRY(mRenderer->createStagingTexture(context, srcTexture->getTextureType(),
                                                  srcTexture->getFormatSet(), stagingTextureSize,
                                                  StagingAccess::READ, &mStagingTexture));
    }

    ID3D11DeviceContext *immediateContext = mRenderer->getDeviceContext();
    D3D11_BOX srcBox;
    srcBox.left   = static_cast<UINT>(clippedReadArea.x);
    srcBox.right  = static_cast<UINT>(clippedReadArea.x + clippedReadArea.width);
    srcBox.top    = static_cast<UINT>(clippedReadArea.y);
    srcBox.bottom = static_cast<UINT>(clippedReadArea.y + clippedReadArea.height);

    // Select the correct layer from a 3D attachment
    srcBox.front = 0;
    if (srcTexture->is3D())
    {
        srcBox.front = static_cast<UINT>(readAttachment.layer());
    }
    srcBox.back = srcBox.front + 1;

    // Asynchronous copy
    immediateContext->CopySubresourceRegion(mStagingTexture.get(), 0, 0, 0, 0, srcTexture->get(),
                                            srcSubresource, &srcBox);

    return angle::Result::Continue;
}

angle::Result Buffer11::PackStorage::flushQueuedPackCommand(const gl::Context *context)
{
    ASSERT(mMemoryBuffer.size() > 0);

    if (mQueuedPackCommand)
    {
        ANGLE_TRY(mRenderer->packPixels(context, mStagingTexture, *mQueuedPackCommand,
                                        mMemoryBuffer.data()));
        mQueuedPackCommand.reset(nullptr);
    }

    return angle::Result::Continue;
}

// Buffer11::SystemMemoryStorage implementation

Buffer11::SystemMemoryStorage::SystemMemoryStorage(Renderer11 *renderer)
    : Buffer11::BufferStorage(renderer, BUFFER_USAGE_SYSTEM_MEMORY)
{}

angle::Result Buffer11::SystemMemoryStorage::copyFromStorage(const gl::Context *context,
                                                             BufferStorage *source,
                                                             size_t sourceOffset,
                                                             size_t size,
                                                             size_t destOffset,
                                                             CopyResult *resultOut,
                                                             BufferFeedback *feedback)
{
    ASSERT(source->isCPUAccessible(GL_MAP_READ_BIT));
    uint8_t *sourceData = nullptr;
    ANGLE_TRY(source->map(context, sourceOffset, size, GL_MAP_READ_BIT, &sourceData));
    ASSERT(destOffset + size <= mSystemCopy.size());
    memcpy(mSystemCopy.data() + destOffset, sourceData, size);
    source->unmap();
    *resultOut = CopyResult::RECREATED;
    return angle::Result::Continue;
}

angle::Result Buffer11::SystemMemoryStorage::resize(const gl::Context *context,
                                                    size_t size,
                                                    bool preserveData,
                                                    BufferFeedback *feedback)
{
    if (mSystemCopy.size() < size)
    {
        Context11 *context11 = GetImplAs<Context11>(context);
        ANGLE_CHECK_GL_ALLOC(context11, mSystemCopy.resize(size));
        mBufferSize = size;
    }

    return angle::Result::Continue;
}

angle::Result Buffer11::SystemMemoryStorage::map(const gl::Context *context,
                                                 size_t offset,
                                                 size_t length,
                                                 GLbitfield access,
                                                 uint8_t **mapPointerOut)
{
    ASSERT(!mSystemCopy.empty() && offset + length <= mSystemCopy.size());
    *mapPointerOut = mSystemCopy.data() + offset;
    return angle::Result::Continue;
}

void Buffer11::SystemMemoryStorage::unmap()
{
    // No-op
}
}  // namespace rx
