//
// Copyright 2021 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// cl_types.h: Defines common types for the OpenCL support in ANGLE.
//

#ifndef LIBANGLE_CLTYPES_H_
#define LIBANGLE_CLTYPES_H_

#if defined(ANGLE_ENABLE_CL)
#    include "libANGLE/CLBitField.h"
#    include "libANGLE/CLCaps.h"
#    include "libANGLE/CLRefPointer.h"
#    include "libANGLE/Debug.h"
#    include "libANGLE/Error.h"
#    include "libANGLE/angletypes.h"

#    include "common/MemoryBuffer.h"
#    include "common/PackedCLEnums_autogen.h"
#    include "common/PackedEnums.h"
#    include "common/SimpleMutex.h"
#    include "common/SynchronizedValue.h"
#    include "common/WorkerThread.h"
#    include "common/angleutils.h"
#    include "common/hash_containers.h"
#    include "common/log_utils.h"
#    include "common/mathutil.h"
#    include "common/string_utils.h"
#    include "common/system_utils.h"
#    include "common/unsafe_buffers.h"

// Include frequently used standard headers
#    include <algorithm>
#    include <array>
#    include <functional>
#    include <list>
#    include <memory>
#    include <string>
#    include <unordered_map>
#    include <utility>
#    include <vector>

namespace cl
{

class Buffer;
class CommandQueue;
class Context;
class Device;
class Event;
class Image;
class Kernel;
class Memory;
class Object;
class Platform;
class Program;
class Sampler;

using BufferPtr       = RefPointer<Buffer>;
using CommandQueuePtr = RefPointer<CommandQueue>;
using ContextPtr      = RefPointer<Context>;
using DevicePtr       = RefPointer<Device>;
using EventPtr        = RefPointer<Event>;
using KernelPtr       = RefPointer<Kernel>;
using MemoryPtr       = RefPointer<Memory>;
using ConstMemoryPtr  = RefPointer<const Memory>;
using PlatformPtr     = RefPointer<Platform>;
using ProgramPtr      = RefPointer<Program>;
using SamplerPtr      = RefPointer<Sampler>;

using BufferPtrs      = std::vector<BufferPtr>;
using DevicePtrs      = std::vector<DevicePtr>;
using EventPtrs       = std::vector<EventPtr>;
using KernelPtrs      = std::vector<KernelPtr>;
using MemoryPtrs      = std::vector<MemoryPtr>;
using ConstMemoryPtrs = std::vector<ConstMemoryPtr>;
using PlatformPtrs    = std::vector<PlatformPtr>;
using ProgramPtrs     = std::vector<ProgramPtr>;
using SamplerPtrs     = std::vector<SamplerPtr>;

using WorkgroupSize    = std::array<uint32_t, 3>;
using GlobalWorkOffset = std::array<uint32_t, 3>;
using GlobalWorkSize   = std::array<uint32_t, 3>;
using WorkgroupCount   = std::array<uint32_t, 3>;

template <typename T>
using EventStatusMap = std::array<T, 3>;

using Extents = angle::Extents<size_t>;
constexpr Extents kExtentsZero(0, 0, 0);
using Offset = angle::Offset<size_t>;
constexpr Offset kOffsetZero(0, 0, 0);

using ChannelMapping = std::array<uint32_t, 4>;
union PixelColor
{
    uint8_t u8[4];
    int8_t s8[4];
    uint16_t u16[4];
    int16_t s16[4];
    uint32_t u32[4];
    int32_t s32[4];
    cl_half fp16[4];
    cl_float fp32[4];
};

class MemoryRegion
{
  public:
    MemoryRegion() = default;
    // A 1D region spans [0, 1) in y and z.
    MemoryRegion(const size_t offset, const size_t size) : mOffset(offset, 0, 0), mSize(size, 1, 1)
    {}
    MemoryRegion(const Offset &offset, const Extents &size) : mOffset(offset), mSize(size) {}

    bool overlaps(const MemoryRegion &other) const
    {
        // Per-axis half-open interval overlap: [off, off + size).
        return axisOverlaps(mOffset.x, mSize.width, other.mOffset.x, other.mSize.width) &&
               axisOverlaps(mOffset.y, mSize.height, other.mOffset.y, other.mSize.height) &&
               axisOverlaps(mOffset.z, mSize.depth, other.mOffset.z, other.mSize.depth);
    }

    const Offset getOffset() const { return mOffset; }
    Extents getExtents() const { return mSize; }

  private:
    bool axisOverlaps(size_t aOff, size_t aSize, size_t bOff, size_t bSize) const;

    Offset mOffset;
    Extents mSize;
};

inline bool MemoryRegion::axisOverlaps(size_t aOff, size_t aSize, size_t bOff, size_t bSize) const
{
    return aOff < bOff + bSize && bOff < aOff + aSize;
}

// MappedRegionInfo captures the full state of a mapped region request from the user
// - region, mapped pointer, and if its mapped for write
template <typename T>
struct MappedRegionInfo
{
    const T key;
    const MemoryRegion region;
    const bool isWrite;
};

// A tracker to manage read and write dependencies at region level. The read and write regions are
// tracked on a map to let multiple uses.
//  - In case of mapping, the regions are tracked per mapped pointer
template <typename T>
class MemoryAccessTracker
{
  public:
    MemoryAccessTracker()  = default;
    ~MemoryAccessTracker() = default;

    bool hasReadConflict(const T key) const { return mReaders.find(key) != mReaders.end(); }
    bool hasWriteConflict(const T key) const { return mWriters.find(key) != mWriters.end(); }
    bool hasConflict(const T key) const { return hasReadConflict(key) || hasWriteConflict(key); }

    bool hasReadConflict(const MemoryRegion &region) const
    {
        return MapHasOverlap(mReaders, region);
    }
    bool hasWriteConflict(const MemoryRegion &region) const
    {
        return MapHasOverlap(mWriters, region);
    }
    bool hasConflict(const MemoryRegion &region) const
    {
        return hasReadConflict(region) || hasWriteConflict(region);
    }

    void addReader(const T key, const MemoryRegion &region)
    {
        ASSERT(!hasReadConflict(region));
        mReaders[key].push_back(region);
    }
    void addWriter(const T key, const MemoryRegion &region)
    {
        ASSERT(!hasWriteConflict(region));
        mWriters[key].push_back(region);
    }

    bool hasKey(const T key) const
    {
        return mReaders.find(key) != mReaders.end() || mWriters.find(key) != mWriters.end();
    }

    // Pops one tracked region for the given key and reports whether it came
    // from the write map. Note that this removes the region from the tracker, for query only needs
    // use `hasKey`. The region is popped from the back to keep it O(1), the user for this currently
    // is map/unmap operations and the spec doesn't require any specific ordering.
    MappedRegionInfo<T> popRegion(const T key)
    {
        // Spec doesn't enforce any specific order, so we pull out from writer first as that's more
        // restricting on future maps
        MemoryRegion region;
        if (popOne(key, mWriters, &region))
        {
            return MappedRegionInfo<T>{key, region, /*isWrite=*/true};
        }
        const bool popped = popOne(key, mReaders, &region);

        // addRegion and popRegion must be one-for-one
        ASSERT(popped);
        return MappedRegionInfo<T>{key, region, /*isWrite=*/false};
    }

  private:
    using MemoryRegionMap = std::unordered_map<T, std::vector<MemoryRegion>>;

    static bool MapHasOverlap(const MemoryRegionMap &map, const MemoryRegion &region)
    {
        for (const auto &[key, regions] : map)
        {
            for (const MemoryRegion &tracked : regions)
            {
                if (tracked.overlaps(region))
                {
                    return true;
                }
            }
        }
        return false;
    }

    static void MergeMap(MemoryRegionMap &dst, const MemoryRegionMap &src)
    {
        for (const auto &entry : src)
        {
            auto &dstRegions = dst[entry.first];
            dstRegions.insert(dstRegions.end(), entry.second.begin(), entry.second.end());
        }
    }

    bool popOne(const T key, MemoryRegionMap &m, MemoryRegion *outRegion)
    {
        auto it = m.find(key);
        if (it == m.end())
        {
            return false;
        }
        ASSERT(!it->second.empty());
        *outRegion = std::move(it->second.back());
        it->second.pop_back();
        if (it->second.empty())
        {
            m.erase(it);
        }
        return true;
    }

    MemoryRegionMap mReaders;
    MemoryRegionMap mWriters;
};

struct KernelArg
{
    bool isSet;
    cl_uint index;
    size_t size;
    const void *valuePtr;
};

struct BufferRect
{
    BufferRect(const Offset &offset,
               const Extents &size,
               const size_t row_pitch,
               const size_t slice_pitch,
               const size_t element_size = 1)
        : mOrigin(offset),
          mSize(size),
          mRowPitch(row_pitch == 0 ? element_size * size.width : row_pitch),
          mSlicePitch(slice_pitch == 0 ? mRowPitch * size.height : slice_pitch),
          mElementSize(element_size)
    {}
    bool valid() const
    {
        return mSize.width != 0 && mSize.height != 0 && mSize.depth != 0 &&
               mRowPitch >= mSize.width * mElementSize && mSlicePitch >= mRowPitch * mSize.height &&
               mElementSize > 0;
    }
    bool operator==(const BufferRect &other) const
    {
        return (mOrigin == other.mOrigin && mSize == other.mSize && mRowPitch == other.mRowPitch &&
                mSlicePitch == other.mSlicePitch && mElementSize == other.mElementSize);
    }
    bool operator!=(const BufferRect &other) const { return !(*this == other); }

    size_t getRowOffset(size_t slice, size_t row) const
    {
        return ((mRowPitch * (mOrigin.y + row)) + (mOrigin.x * mElementSize)) +  // row offset
               (mSlicePitch * (mOrigin.z + slice));                              // slice offset
    }
    // Calculates the offset of the starting position of the rectangle
    size_t getBufferOffset() const { return getRowOffset(0, 0); }

    size_t getRowPitch() const { return mRowPitch; }
    size_t getSlicePitch() const { return mSlicePitch; }

    // Given the offset, row pitch, slice pitch, this returns the size of the buffer in which this
    // rect sits.
    //
    //            row_pitch
    // +------------------------------+
    // |   origin                     |
    // |      +---------------+       |
    // |      |               |height |
    // |      |               |       |
    // |      |   width       |       |
    // +------+---------------+-------+
    //   - size of the buffer is
    //      - initial offset to start of rect
    //      - + size of the rectangle
    //      - - (extra regions for the last slice and row)
    size_t getRectSize() const
    {
        // Treat:
        //  S: mSlicePitch, R: mRowPitch, O: Offset(xyz_dim), D: mSize.depth, H: mSize.height,
        //  W: mSize.width, E: mElementSize
        //
        //  strideHeightPadding == (S / R) - H
        //  strideRowPadding    == R - (W * E)
        //
        // getRectSize() =
        //    getBufferOffset()         // initial offset to the start of rect
        //  + (S * D)                   // size of the rectangle
        //  - (strideHeightPadding * R) // extraneous region for the last slice
        //  - strideRowPadding          // extraneous region for the last row
        //
        // Where:
        //  getBufferOffset()   == getRowOffset(0,0) == S(Oz) + R(Oy) + (Ox * E)
        //  getRowOffset(s, r)  == S(Oz + s) + R(Oy + r) + (Ox * E)
        //
        // Simplifies to:
        //  getRectSize() = S(Oz) + R(Oy) + (Ox * E) + (S * D) - R(S/R - H) - R - (W * E)
        //  getRectSize() = S(Oz + (D - 1)) + R(Oy + (H - 1)) + (Ox * E) + (W * E)
        //  ...
        //  getRectSize() = getRowOffset((D - 1), (H - 1)) + (W * E)

        // The end of the rect is the beginning of the last row + the length of the row.
        // This logic excludes paddings for the last row / slice.
        return getRowOffset(mSize.depth - 1, mSize.height - 1) + mSize.width * mElementSize;
    }
    const Extents &getExtents() const { return mSize; }
    const Offset &getOrigin() const { return mOrigin; }
    Offset mOrigin;
    Extents mSize;
    size_t mRowPitch;
    size_t mSlicePitch;
    size_t mElementSize;
};

struct ImageDescriptor
{
    MemObjectType type;
    size_t width;
    size_t height;
    size_t depth;
    size_t arraySize;
    size_t rowPitch;
    size_t slicePitch;
    cl_uint numMipLevels;
    cl_uint numSamples;

    ImageDescriptor(MemObjectType type_,
                    size_t width_,
                    size_t height_,
                    size_t depth_,
                    size_t arraySize_,
                    size_t rowPitch_,
                    size_t slicePitch_,
                    cl_uint numMipLevels_,
                    cl_uint numSamples_)
        : type(type_),
          width(width_),
          height(height_),
          depth(depth_),
          arraySize(arraySize_),
          rowPitch(rowPitch_),
          slicePitch(slicePitch_),
          numMipLevels(numMipLevels_),
          numSamples(numSamples_)
    {
        if (type == MemObjectType::Image1D || type == MemObjectType::Image1D_Array ||
            type == MemObjectType::Image1D_Buffer)
        {
            depth  = 1;
            height = 1;
        }
        if (type == MemObjectType::Image2D || type == MemObjectType::Image2D_Array)
        {
            depth = 1;
        }
        if (!(type == MemObjectType::Image1D_Array || type == MemObjectType::Image2D_Array))
        {
            arraySize = 1;
        }
    }

    bool operator==(const ImageDescriptor &other) const
    {
        return (type == other.type && width == other.width && height == other.height &&
                depth == other.depth && arraySize == other.arraySize &&
                rowPitch == other.rowPitch && slicePitch == other.slicePitch &&
                numMipLevels == other.numMipLevels && numSamples == other.numSamples);
    }
    bool operator!=(const ImageDescriptor &other) const { return !(*this == other); }
};

struct NDRange
{
    NDRange(cl_uint workDimensionsIn,
            const size_t *globalWorkOffsetIn,
            const size_t *globalWorkSizeIn,
            const size_t *localWorkSizeIn)
        : workDimensions(workDimensionsIn),
          globalWorkOffset({0, 0, 0}),
          globalWorkSize({1, 1, 1}),
          localWorkSize({1, 1, 1}),
          nullLocalWorkSize(localWorkSizeIn == nullptr)
    {
        for (cl_uint dim = 0; dim < workDimensionsIn; dim++)
        {
            if (globalWorkOffsetIn != nullptr)
            {
                ASSERT(!ANGLE_UNSAFE_TODO(
                    (static_cast<uint32_t>((globalWorkOffsetIn[dim] + globalWorkSizeIn[dim])) <
                     globalWorkOffsetIn[dim])));
                globalWorkOffset[dim] =
                    static_cast<uint32_t>(ANGLE_UNSAFE_TODO(globalWorkOffsetIn[dim]));
            }
            if (globalWorkSizeIn != nullptr)
            {
                ASSERT(ANGLE_UNSAFE_TODO(globalWorkSizeIn[dim]) <= UINT32_MAX);
                globalWorkSize[dim] =
                    static_cast<uint32_t>(ANGLE_UNSAFE_TODO(globalWorkSizeIn[dim]));
            }
            else
            {
                // For versions >= 2.1, global work size can be a nullptr, in which case set dim to
                // zero. Validation checks ensure that we are here only for >= 2.1 versions.
                globalWorkSize[dim] = 0;
            }
            if (localWorkSizeIn != nullptr)
            {
                ASSERT(ANGLE_UNSAFE_TODO(localWorkSizeIn[dim]) <= UINT32_MAX);
                localWorkSize[dim] = static_cast<uint32_t>(ANGLE_UNSAFE_TODO(localWorkSizeIn[dim]));
            }
        }
    }

    cl::WorkgroupCount getWorkgroupCount() const
    {
        ASSERT(localWorkSize[0] > 0 && localWorkSize[1] > 0 && localWorkSize[2] > 0);
        return cl::WorkgroupCount{rx::UnsignedCeilDivide(globalWorkSize[0], localWorkSize[0]),
                                  rx::UnsignedCeilDivide(globalWorkSize[1], localWorkSize[1]),
                                  rx::UnsignedCeilDivide(globalWorkSize[2], localWorkSize[2])};
    }

    bool isUniform() const
    {
        for (cl_uint dim = 0; dim < workDimensions; dim++)
        {
            if (globalWorkSize[dim] % localWorkSize[dim] != 0)
            {
                return false;
            }
        }
        return true;
    }

    std::vector<NDRange> createUniformRegions(
        const std::array<uint32_t, 3> maxComputeWorkGroupCount) const
    {
        // Work-group sizes could be non-uniform in multiple dimensions, potentially producing
        // work-groups of up to 4 different sizes in a 2D range and 8 different sizes in a 3D range.
        constexpr size_t kMaxNonUniformWorkGroupShapes = 8u;

        std::vector<NDRange> regions;
        regions.reserve(kMaxNonUniformWorkGroupShapes);
        regions.push_back(*this);
        regions.front().globalWorkOffset = {0};
        for (uint32_t regionPos = 0; regionPos < regions.size(); ++regionPos)
        {
            for (uint32_t dim = 0; dim < workDimensions; dim++)
            {
                NDRange &region = regions.at(regionPos);
                uint32_t remainder =
                    ANGLE_UNSAFE_TODO(region.globalWorkSize[dim] % region.localWorkSize[dim]);
                if (remainder != 0)
                {
                    // Split the range along this dimension. The original range's global work size
                    // (e.g. 19) is clipped to a multiple of the local work size (e.g. 8). A new
                    // range is added for the remainder (in this example 3) where the global and
                    // local work sizes are identical to the remainder (i.e. it's also a uniform
                    // range).
                    NDRange newRegion(region);
                    newRegion.globalWorkSize[dim] = newRegion.localWorkSize[dim] = remainder;
                    ANGLE_UNSAFE_TODO(region.globalWorkSize[dim] = newRegion.globalWorkOffset[dim] =
                                          (region.globalWorkSize[dim] - remainder));
                    regions.push_back(newRegion);
                }
            }
        }
        ASSERT(regions.size() <= kMaxNonUniformWorkGroupShapes);

        // Break into uniform regions that fit into given maxComputeWorkGroupCount (if needed)
        std::vector<NDRange> regionsWithinDeviceLimits;
        regionsWithinDeviceLimits.reserve(regions.size());
        for (const auto &region : regions)
        {
            regionsWithinDeviceLimits.push_back(region);
            for (uint32_t regionPos = 0; regionPos < regionsWithinDeviceLimits.size(); ++regionPos)
            {
                for (uint32_t dim = 0; dim < workDimensions; dim++)
                {
                    NDRange &currentRegion = regionsWithinDeviceLimits.at(regionPos);

                    uint32_t maxGwsForRegion = gl::clampCast<uint32_t, uint64_t>(
                        static_cast<uint64_t>(maxComputeWorkGroupCount[dim]) *
                        static_cast<uint64_t>(currentRegion.localWorkSize[dim]));

                    if (currentRegion.globalWorkSize[dim] > maxGwsForRegion)
                    {
                        uint32_t remainderGws = currentRegion.globalWorkSize[dim] - maxGwsForRegion;
                        if (remainderGws > 0)
                        {
                            NDRange remainderRegion             = currentRegion;
                            remainderRegion.globalWorkSize[dim] = remainderGws;
                            remainderRegion.globalWorkOffset[dim] =
                                currentRegion.globalWorkOffset[dim] +
                                (currentRegion.globalWorkSize[dim] - remainderGws);
                            currentRegion.globalWorkSize[dim] = maxGwsForRegion;
                            regionsWithinDeviceLimits.push_back(remainderRegion);
                            continue;
                        }
                    }
                }
            }
        }
        return regionsWithinDeviceLimits;
    }

    cl_uint workDimensions;
    GlobalWorkOffset globalWorkOffset;
    GlobalWorkSize globalWorkSize;
    WorkgroupSize localWorkSize;
    bool nullLocalWorkSize{false};
};

// name-value pair for cl_properties lists
struct NameValueProperty
{
    cl_properties name;
    cl_properties value;
};

// this Defer class provides the user with a closure that executes on its destruction
template <typename F>
class Defer : public angle::Closure
{
  public:
    Defer(F &&func) : mFunc(std::forward<F>(func)) {}
    ~Defer() override { operator()(); }
    void operator()() override { mFunc(); }

  private:
    F mFunc;
};

constexpr cl_ulong kMaxAllocSentinel = 0;

}  // namespace cl

#endif  // ANGLE_ENABLE_CL

#endif  // LIBANGLE_CLTYPES_H_
