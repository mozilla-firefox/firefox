//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// angleutils.h: Common ANGLE utilities.

#ifndef COMMON_ANGLEUTILS_H_
#define COMMON_ANGLEUTILS_H_

#include "common/platform.h"

#if defined(ANGLE_WITH_LSAN)
#    include <sanitizer/lsan_interface.h>
#endif  // defined(ANGLE_WITH_LSAN)

#if defined(ANGLE_WITH_MSAN)
#    include <sanitizer/msan_interface.h>
#endif  // defined(ANGLE_WITH_MSAN)

#include <array>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/unsafe_buffers.h"

namespace angle
{

#if defined(ANGLE_PLATFORM_WINDOWS)
using Microsoft::WRL::ComPtr;
#endif

// Forward declaration. Implementation in system_utils.h
using ThreadId = std::thread::id;

// A helper class to disallow copy and assignment operators
class NonCopyable
{
  protected:
    constexpr NonCopyable() = default;
    ~NonCopyable()          = default;

  private:
    NonCopyable(const NonCopyable &)    = delete;
    void operator=(const NonCopyable &) = delete;
};

extern const uintptr_t DirtyPointer;

// AMD_performance_monitor helpers.
constexpr char kPerfMonitorExtensionName[] = "GL_AMD_performance_monitor";

struct PerfMonitorCounterInfo
{
    PerfMonitorCounterInfo() = default;
    PerfMonitorCounterInfo(std::string_view name) : name(name) {}

    std::string name;
};
struct PerfMonitorCounter
{
    PerfMonitorCounter() = default;
    PerfMonitorCounter(uint64_t value) : value(value) {}

    uint64_t value;
};
using PerfMonitorCountersInfo = std::vector<PerfMonitorCounterInfo>;
using PerfMonitorCounters     = std::vector<PerfMonitorCounter>;

struct PerfMonitorCounterGroupInfo
{
    std::string name;
    PerfMonitorCountersInfo counters;
};
struct PerfMonitorCounterGroup
{
    PerfMonitorCounters counters;
};
using PerfMonitorCounterGroupsInfo = std::vector<PerfMonitorCounterGroupInfo>;
using PerfMonitorCounterGroups     = std::vector<PerfMonitorCounterGroup>;

struct PerfMonitorTriplet
{
    uint32_t group;
    uint32_t counter;
    uint64_t value;
};

#define ANGLE_VK_PERF_COUNTERS_X(FN)               \
    FN(commandQueueSubmitCallsTotal)               \
    FN(vkQueueSubmitCallsTotal)                    \
    FN(commandQueueWaitSemaphoresTotal)            \
    FN(renderPasses)                               \
    FN(writeDescriptorSets)                        \
    FN(flushedOutsideRenderPassCommandBuffers)     \
    FN(swapchainCreate)                            \
    FN(swapchainResolveInSubpass)                  \
    FN(swapchainResolveOutsideSubpass)             \
    FN(resolveImageCommands)                       \
    FN(colorLoadOpClears)                          \
    FN(colorLoadOpLoads)                           \
    FN(colorLoadOpNones)                           \
    FN(colorStoreOpStores)                         \
    FN(colorStoreOpNones)                          \
    FN(colorClearAttachments)                      \
    FN(depthLoadOpClears)                          \
    FN(depthLoadOpLoads)                           \
    FN(depthLoadOpNones)                           \
    FN(depthStoreOpStores)                         \
    FN(depthStoreOpNones)                          \
    FN(depthClearAttachments)                      \
    FN(stencilLoadOpClears)                        \
    FN(stencilLoadOpLoads)                         \
    FN(stencilLoadOpNones)                         \
    FN(stencilStoreOpStores)                       \
    FN(stencilStoreOpNones)                        \
    FN(stencilClearAttachments)                    \
    FN(colorAttachmentUnresolves)                  \
    FN(depthAttachmentUnresolves)                  \
    FN(stencilAttachmentUnresolves)                \
    FN(colorAttachmentResolves)                    \
    FN(depthAttachmentResolves)                    \
    FN(stencilAttachmentResolves)                  \
    FN(readOnlyDepthStencilRenderPasses)           \
    FN(pipelineCreationCacheHits)                  \
    FN(pipelineCreationCacheMisses)                \
    FN(pipelineCreationTotalCacheHitsDurationNs)   \
    FN(pipelineCreationTotalCacheMissesDurationNs) \
    FN(monolithicPipelineCreation)                 \
    FN(descriptorSetAllocations)                   \
    FN(descriptorSetCacheTotalSize)                \
    FN(uniformsAndXfbDescriptorSetCacheHits)       \
    FN(uniformsAndXfbDescriptorSetCacheMisses)     \
    FN(uniformsAndXfbDescriptorSetCacheTotalSize)  \
    FN(textureDescriptorSetCacheHits)              \
    FN(textureDescriptorSetCacheMisses)            \
    FN(textureDescriptorSetCacheTotalSize)         \
    FN(uniformBuffersDescriptorSetCacheHits)       \
    FN(uniformBuffersDescriptorSetCacheMisses)     \
    FN(uniformBuffersDescriptorSetCacheTotalSize)  \
    FN(shaderResourcesDescriptorSetCacheHits)      \
    FN(shaderResourcesDescriptorSetCacheMisses)    \
    FN(shaderResourcesDescriptorSetCacheTotalSize) \
    FN(deviceMemoryImageAllocationFallbacks)       \
    FN(tileMemoryImages)                           \
    FN(fallbackFromTileMemory)                     \
    FN(mutableTexturesUploaded)                    \
    FN(fullImageClears)                            \
    FN(buffersGhosted)                             \
    FN(vertexArraySyncStateCalls)                  \
    FN(allocateNewBufferBlockCalls)                \
    FN(bufferSuballocationCalls)                   \
    FN(framebufferCacheSize)                       \
    FN(pendingSubmissionGarbageObjects)            \
    FN(graphicsDriverUniformsUpdated)

#define ANGLE_VK_API_PERF_COUNTER_GROUPS_X(FN) \
    FN(Command)                                \
    FN(Submit)                                 \
    FN(Surface)                                \
    FN(Wait)                                   \
    FN(Other)

#define ANGLE_VK_API_PERF_COUNTER_TYPES_X(FN) \
    FN(WallTimeNs)                            \
    FN(Samples)

#define ANGLE_VK_API_PERF_COUNTER_TYPES_WITH_PARAM_X(FN, PARAM) \
    FN(WallTimeNs, PARAM)                                       \
    FN(Samples, PARAM)

#define ANGLE_DECLARE_PERF_COUNTER(COUNTER) uint64_t COUNTER;

struct VulkanPerfCounters
{
    ANGLE_VK_PERF_COUNTERS_X(ANGLE_DECLARE_PERF_COUNTER)
};

#undef ANGLE_DECLARE_PERF_COUNTER

#define ANGLE_DECLARE_VK_API_PERF_COUNTER_ENUM(NAME) NAME,

enum class VulkanApiPerfCounterGroup
{
    ANGLE_VK_API_PERF_COUNTER_GROUPS_X(ANGLE_DECLARE_VK_API_PERF_COUNTER_ENUM)
    // EnumCount enables PackedEnums support.
    EnumCount
};

enum class VulkanApiPerfCounterType
{
    ANGLE_VK_API_PERF_COUNTER_TYPES_X(ANGLE_DECLARE_VK_API_PERF_COUNTER_ENUM)
    // EnumCount enables PackedEnums support.
    EnumCount
};

#undef ANGLE_DECLARE_VK_API_PERF_COUNTER_ENUM

std::string_view GetVulkanApiPerfCounterGroupName(VulkanApiPerfCounterGroup group);
std::string_view GetVulkanApiPerfCounterTypeName(VulkanApiPerfCounterType type);
std::string_view GetVulkanApiPerfCounterName(VulkanApiPerfCounterGroup group,
                                             VulkanApiPerfCounterType type);

namespace internal
{
template <typename Range>
class ReversedAdapter
{
  public:
    explicit constexpr ReversedAdapter(Range &&range) : mRange(std::forward<Range>(range)) {}
    constexpr ReversedAdapter(const ReversedAdapter &)  = default;
    constexpr ReversedAdapter(ReversedAdapter &&)       = default;
    ReversedAdapter &operator=(const ReversedAdapter &) = delete;
    ReversedAdapter &operator=(ReversedAdapter &&)      = delete;

    constexpr auto begin() { return std::rbegin(mRange); }
    constexpr auto begin() const { return std::rbegin(mRange); }
    constexpr auto cbegin() const { return std::crbegin(mRange); }

    constexpr auto end() { return std::rend(mRange); }
    constexpr auto end() const { return std::rend(mRange); }
    constexpr auto cend() const { return std::crend(mRange); }

  private:
    Range mRange;
};
}  // namespace internal

// Reversed returns a container adapter usable in a range-based "for" statement
// for iterating a reversible container in reverse order.
//
// TODO(http://crbug.com/330910097): Switch to std::views::reverse once ANGLE
// drops C++17 support and requires C++20.
template <typename Range>
constexpr internal::ReversedAdapter<Range> Reversed(Range &&range)
{
    return internal::ReversedAdapter<Range>(std::forward<Range>(range));
}
}  // namespace angle

template <typename T, size_t N>
constexpr inline size_t ArraySize(T (&)[N])
{
    return N;
}

template <typename T, size_t N>
constexpr inline size_t ArraySize(const std::array<T, N> &)
{
    return N;
}

template <typename T>
class WrappedArray final : angle::NonCopyable
{
  public:
    constexpr WrappedArray() = default;
    constexpr WrappedArray(const T *data, size_t size) : mArray(data), mSize(size) {}

    WrappedArray(WrappedArray &&other) : WrappedArray()
    {
        std::swap(mArray, other.mArray);
        std::swap(mSize, other.mSize);
    }

    template <size_t N>
    constexpr WrappedArray(const T (&data)[N]) : mArray(&data[0]), mSize(N)
    {}

    ~WrappedArray() {}

    constexpr const T *get() const { return mArray; }
    constexpr size_t size() const { return mSize; }

  private:
    const T *mArray = nullptr;
    size_t mSize    = 0;
};

template <typename T>
void SafeDelete(T *&resource)
{
    delete resource;
    resource = nullptr;
}

template <typename T>
void SafeDeleteContainer(T &resource)
{
    for (auto &element : resource)
    {
        SafeDelete(element);
    }
    resource.clear();
}

template <typename T>
void SafeDeleteArray(T *&resource)
{
    delete[] resource;
    resource = nullptr;
}

// Provide a less-than function for comparing structs
// Note: struct memory must be initialized to zero, because of packing gaps
template <typename T>
inline bool StructLessThan(const T &a, const T &b)
{
    // SAFETY: both `a` and `b` are the same compiler-deduced size.
    return ANGLE_UNSAFE_BUFFERS(memcmp(&a, &b, sizeof(T)) < 0);
}

// Provide a less-than function for comparing structs
// Note: struct memory must be initialized to zero, because of packing gaps
template <typename T>
inline bool StructEquals(const T &a, const T &b)
{
    // SAFETY: both `a` and `b` are the same compiler-deduced size.
    return ANGLE_UNSAFE_BUFFERS(memcmp(&a, &b, sizeof(T)) == 0);
}

template <typename T>
inline void StructZero(T *obj)
{
    // SAFETY: compiler-deduced size.
    ANGLE_UNSAFE_BUFFERS(memset(obj, 0, sizeof(T)));
}

template <typename T>
inline void StructCopy(T *dst, const T &src)
{
    // SAFETY: compiler-deduced size.
    ANGLE_UNSAFE_BUFFERS(memcpy(dst, &src, sizeof(T)));
}

template <typename T>
inline bool IsMaskFlagSet(T mask, T flag)
{
    // Handles multibit flags as well
    return (mask & flag) == flag;
}

std::string ArrayString(unsigned int i);

// Indices are stored in vectors with the outermost index in the back. In the output of the function
// the indices are reversed.
std::string ArrayIndexString(const std::vector<unsigned int> &indices);

inline std::string Str(int i)
{
    std::stringstream strstr;
    strstr << i;
    return strstr.str();
}

template <typename T>
std::string ToString(const T &value)
{
    std::ostringstream o;
    o << value;
    return o.str();
}

inline bool IsLittleEndian()
{
    constexpr uint32_t kEndiannessTest = 1;
    const bool isLittleEndian          = *reinterpret_cast<const uint8_t *>(&kEndiannessTest) == 1;
    return isLittleEndian;
}

// Helper macro that casts to a bitfield type then verifies no bits were dropped.
#define SetBitField(lhs, rhs)                                                         \
    do                                                                                \
    {                                                                                 \
        auto ANGLE_LOCAL_VAR = rhs;                                                   \
        lhs = static_cast<typename std::decay<decltype(lhs)>::type>(ANGLE_LOCAL_VAR); \
        ASSERT(static_cast<decltype(ANGLE_LOCAL_VAR)>(lhs) == ANGLE_LOCAL_VAR);       \
    } while (0)

// snprintf is not defined with MSVC prior to to msvc14
#if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#endif

// Standard 64-bit type enums (for internal use)
#define GL_INT64 0x140E           // Same as GL_INT64_ARB
#define GL_UNSIGNED_INT64 0x140F  // Same as GL_UNSIGNED_INT64_ARB

// Note: when adding internal formats, update IsAngleInternalFormat() so they aren't accidentally
// accessible by the application.
#define GL_A1RGB5_ANGLEX 0x6AC5
#define GL_BGRX8_ANGLEX 0x6ABA
#define GL_BGR565_ANGLEX 0x6ABB
#define GL_BGRA4_ANGLEX 0x6ABC
#define GL_BGR5_A1_ANGLEX 0x6ABD
#define GL_BGRA8_SRGB_ANGLEX 0x6AC0
#define GL_BGR10_A2_ANGLEX 0x6AF9
#define GL_BGRX8_SRGB_ANGLEX 0x6AFC
// fake format for GL_ANGLE_rgbx_internal_format
#define GL_RGBX8_SRGB_ANGLEX 0x6AFA

#define GL_R10X6G10X6B10X6A10X6_UNORM_ANGLEX 0x6AFD

// These are fake formats used to fit typeless D3D textures that can be bound to EGL pbuffers into
// the format system (for extension EGL_ANGLE_d3d_texture_client_buffer):
#define GL_RGBA8_TYPELESS_ANGLEX 0x6AC1
#define GL_RGBA8_TYPELESS_SRGB_ANGLEX 0x6AC2
#define GL_BGRA8_TYPELESS_ANGLEX 0x6AC3
#define GL_BGRA8_TYPELESS_SRGB_ANGLEX 0x6AC4

#define GL_R8_SSCALED_ANGLEX 0x6AC6
#define GL_RG8_SSCALED_ANGLEX 0x6AC7
#define GL_RGB8_SSCALED_ANGLEX 0x6AC8
#define GL_RGBA8_SSCALED_ANGLEX 0x6AC9
#define GL_R8_USCALED_ANGLEX 0x6ACA
#define GL_RG8_USCALED_ANGLEX 0x6ACB
#define GL_RGB8_USCALED_ANGLEX 0x6ACC
#define GL_RGBA8_USCALED_ANGLEX 0x6ACD

#define GL_R16_SSCALED_ANGLEX 0x6ACE
#define GL_RG16_SSCALED_ANGLEX 0x6ACF
#define GL_RGB16_SSCALED_ANGLEX 0x6AD0
#define GL_RGBA16_SSCALED_ANGLEX 0x6AD1
#define GL_R16_USCALED_ANGLEX 0x6AD2
#define GL_RG16_USCALED_ANGLEX 0x6AD3
#define GL_RGB16_USCALED_ANGLEX 0x6AD4
#define GL_RGBA16_USCALED_ANGLEX 0x6AD5

#define GL_R32_SSCALED_ANGLEX 0x6AD6
#define GL_RG32_SSCALED_ANGLEX 0x6AD7
#define GL_RGB32_SSCALED_ANGLEX 0x6AD8
#define GL_RGBA32_SSCALED_ANGLEX 0x6AD9
#define GL_R32_USCALED_ANGLEX 0x6ADA
#define GL_RG32_USCALED_ANGLEX 0x6ADB
#define GL_RGB32_USCALED_ANGLEX 0x6ADC
#define GL_RGBA32_USCALED_ANGLEX 0x6ADD

#define GL_R32_SNORM_ANGLEX 0x6ADE
#define GL_RG32_SNORM_ANGLEX 0x6ADF
#define GL_RGB32_SNORM_ANGLEX 0x6AE0
#define GL_RGBA32_SNORM_ANGLEX 0x6AE1
#define GL_R32_UNORM_ANGLEX 0x6AE2
#define GL_RG32_UNORM_ANGLEX 0x6AE3
#define GL_RGB32_UNORM_ANGLEX 0x6AE4
#define GL_RGBA32_UNORM_ANGLEX 0x6AE5

#define GL_R32_FIXED_ANGLEX 0x6AE6
#define GL_RG32_FIXED_ANGLEX 0x6AE7
#define GL_RGB32_FIXED_ANGLEX 0x6AE8
#define GL_RGBA32_FIXED_ANGLEX 0x6AE9

#define GL_RGB10_A2_SINT_ANGLEX 0x6AEA
#define GL_RGB10_A2_SNORM_ANGLEX 0x6AEB
#define GL_RGB10_A2_SSCALED_ANGLEX 0x6AEC
#define GL_RGB10_A2_USCALED_ANGLEX 0x6AED

// These are fake formats for OES_vertex_type_10_10_10_2
#define GL_A2_RGB10_UNORM_ANGLEX 0x6AEF
#define GL_A2_RGB10_SNORM_ANGLEX 0x6AF0
#define GL_A2_RGB10_USCALED_ANGLEX 0x6AF1
#define GL_A2_RGB10_SSCALED_ANGLEX 0x6AF2
#define GL_X2_RGB10_UINT_ANGLEX 0x6AF3
#define GL_X2_RGB10_SINT_ANGLEX 0x6AF4
#define GL_X2_RGB10_USCALED_ANGLEX 0x6AF5
#define GL_X2_RGB10_SSCALED_ANGLEX 0x6AF6
#define GL_X2_RGB10_UNORM_ANGLEX 0x6AF7
#define GL_X2_RGB10_SNORM_ANGLEX 0x6AF8

#define ANGLE_CHECK_GL_ALLOC(context, result) \
    ANGLE_CHECK(context, result, "Failed to allocate host memory.", GL_OUT_OF_MEMORY)

#define ANGLE_CHECK_GL_MATH(context, result) \
    ANGLE_CHECK(context, result, "Integer overflow.", GL_INVALID_OPERATION)

#define ANGLE_GL_UNREACHABLE(context) \
    UNREACHABLE();                    \
    ANGLE_CHECK(context, false, "Unreachable code.", GL_INVALID_OPERATION)

#if defined(ANGLE_WITH_LSAN)
#    define ANGLE_SCOPED_DISABLE_LSAN() __lsan::ScopedDisabler lsanDisabler
#else
#    define ANGLE_SCOPED_DISABLE_LSAN()
#endif

#if defined(ANGLE_WITH_MSAN)
class MsanScopedDisableInterceptorChecks final : angle::NonCopyable
{
  public:
    MsanScopedDisableInterceptorChecks() { __msan_scoped_disable_interceptor_checks(); }
    ~MsanScopedDisableInterceptorChecks() { __msan_scoped_enable_interceptor_checks(); }
};
#    define ANGLE_SCOPED_DISABLE_MSAN() \
        MsanScopedDisableInterceptorChecks msanScopedDisableInterceptorChecks
#else
#    define ANGLE_SCOPED_DISABLE_MSAN()
#endif

// The ANGLE_NO_SANITIZE_MEMORY macro suppresses MemorySanitizer checks for
// use-of-uninitialized-data. It can be used to decorate functions with known
// false positives.
#ifdef __clang__
#    define ANGLE_NO_SANITIZE_MEMORY __attribute__((no_sanitize_memory))
#else
#    define ANGLE_NO_SANITIZE_MEMORY
#endif

// Similar to the above, but for thread sanitization.
#ifdef __clang__
#    define ANGLE_NO_SANITIZE_THREAD __attribute__((no_sanitize_thread))
#else
#    define ANGLE_NO_SANITIZE_THREAD
#endif

// Similar to the above, but for cfi-icall.
#ifdef __clang__
#    define ANGLE_NO_SANITIZE_CFI_ICALL __attribute__((no_sanitize("cfi-icall")))
#else
#    define ANGLE_NO_SANITIZE_CFI_ICALL
#endif

// The below inlining code lifted from V8.
#if defined(__clang__) || (defined(__GNUC__) && defined(__has_attribute))
#    define ANGLE_HAS_ATTRIBUTE_ALWAYS_INLINE (__has_attribute(always_inline))
#    define ANGLE_HAS___FORCEINLINE 0
#elif defined(_MSC_VER)
#    define ANGLE_HAS_ATTRIBUTE_ALWAYS_INLINE 0
#    define ANGLE_HAS___FORCEINLINE 1
#else
#    define ANGLE_HAS_ATTRIBUTE_ALWAYS_INLINE 0
#    define ANGLE_HAS___FORCEINLINE 0
#endif

#if defined(NDEBUG) && ANGLE_HAS_ATTRIBUTE_ALWAYS_INLINE
#    define ANGLE_INLINE inline __attribute__((always_inline))
#elif defined(NDEBUG) && ANGLE_HAS___FORCEINLINE
#    define ANGLE_INLINE __forceinline
#else
#    define ANGLE_INLINE inline
#endif

#if defined(__clang__) || (defined(__GNUC__) && defined(__has_attribute))
#    if __has_attribute(noinline)
#        define ANGLE_NOINLINE __attribute__((noinline))
#    else
#        define ANGLE_NOINLINE
#    endif
#elif defined(_MSC_VER)
#    define ANGLE_NOINLINE __declspec(noinline)
#else
#    define ANGLE_NOINLINE
#endif

// Annotates a type as being suitable for passing in registers despite having a
// non-trivial copy or move constructor or destructor. This requires the type
// not be concerned about its address remaining constant, be safely usable after
// copying its memory, and have a destructor that may be safely omitted on
// moved-from instances; an example is `std::unique_ptr`. Unnecessary if the
// copy/move constructor(s) and destructor are unconditionally trivial; likely
// ineffective if the type is too large to be passed in one or two registers
// with the target ABI. However, annotating a type this way will also cause
// `IS_TRIVIALLY_RELOCATABLE()` to return true for that type, and so may be
// desirable even for large types, if they are placed in containers that
// optimize based on that check.
//
// NOTE: Use with caution; this has subtle effects on constructor/destructor
// ordering. When used with types passed or returned by value, values may be
// constructed in the source stack frame, passed in a register, and then used
// and destroyed in the target stack frame.
//
// See also:
//   https://clang.llvm.org/docs/AttributeReference.html#trivial-abi
//   https://libcxx.llvm.org/docs/DesignDocs/UniquePtrTrivialAbi.html
//
// Usage:
// ```
//   // Instances of type `S` will be eligible to be passed in registers despite
//   // `S`'s nontrivial destructor.
//   struct ANGLE_TRIVIAL_ABI S { ~S(); }
// ```
#if defined(__has_cpp_attribute)
#    if __has_cpp_attribute(clang::trivial_abi)
#        define ANGLE_TRIVIAL_ABI [[clang::trivial_abi]]
#    else
#        define ANGLE_TRIVIAL_ABI
#    endif
#else
#    define ANGLE_TRIVIAL_ABI
#endif

// Annotates a class as a pointer-like type, so that the compiler can diagnose references left
// dangling by a temporary owner.
//
// https://clang.llvm.org/docs/AttributeReference.html#pointer
#if defined(__has_cpp_attribute)
#    if __has_cpp_attribute(gsl::Pointer)
#        define ANGLE_GSL_POINTER [[gsl::Pointer]]
#    else
#        define ANGLE_GSL_POINTER
#    endif
#else
#    define ANGLE_GSL_POINTER
#endif

#if defined(__clang__) || (defined(__GNUC__) && defined(__has_attribute))
#    if __has_attribute(format)
#        define ANGLE_FORMAT_PRINTF(fmt, args) __attribute__((format(__printf__, fmt, args)))
#    else
#        define ANGLE_FORMAT_PRINTF(fmt, args)
#    endif
#else
#    define ANGLE_FORMAT_PRINTF(fmt, args)
#endif

#if defined(__clang__) || (defined(__GNUC__) && defined(__has_attribute))
#    define ANGLE_HAS_ATTRIBUTE_CONSTRUCTOR (__has_attribute(constructor))
#    define ANGLE_HAS_ATTRIBUTE_DESTRUCTOR (__has_attribute(destructor))
#else
#    define ANGLE_HAS_ATTRIBUTE_CONSTRUCTOR 0
#    define ANGLE_HAS_ATTRIBUTE_DESTRUCTOR 0
#endif

#if ANGLE_HAS_ATTRIBUTE_CONSTRUCTOR
#    define ANGLE_CONSTRUCTOR __attribute__((constructor))
#endif

#if ANGLE_HAS_ATTRIBUTE_DESTRUCTOR
#    define ANGLE_DESTRUCTOR __attribute__((destructor))
#endif

ANGLE_FORMAT_PRINTF(1, 0)
size_t FormatStringIntoVector(const char *fmt, va_list vararg, std::vector<char> &buffer);

// Format messes up the # inside the macro.
// clang-format off
#ifndef ANGLE_STRINGIFY
#    define ANGLE_STRINGIFY(x) #x
#endif
// clang-format on

#ifndef ANGLE_MACRO_STRINGIFY
#    define ANGLE_MACRO_STRINGIFY(x) ANGLE_STRINGIFY(x)
#endif

// The ANGLE_MAYBE_UNUSED_PRIVATE_FIELD can be used to hint 'unused private field'
// instead of 'maybe_unused' attribute for the compatibility with GCC because
// GCC doesn't have '-Wno-unused-private-field' whereas Clang has.
#if defined(__clang__) || defined(_MSC_VER)
#    define ANGLE_MAYBE_UNUSED_PRIVATE_FIELD [[maybe_unused]]
#else
#    define ANGLE_MAYBE_UNUSED_PRIVATE_FIELD
#endif

#if __has_cpp_attribute(clang::require_constant_initialization)
#    define ANGLE_REQUIRE_CONSTANT_INIT [[clang::require_constant_initialization]]
#else
#    define ANGLE_REQUIRE_CONSTANT_INIT
#endif  // __has_cpp_attribute(require_constant_initialization)

// Compiler configs.
inline bool IsASan()
{
#if defined(ANGLE_WITH_ASAN)
    return true;
#else
    return false;
#endif  // defined(ANGLE_WITH_ASAN)
}

inline bool IsMSan()
{
#if defined(ANGLE_WITH_MSAN)
    return true;
#else
    return false;
#endif  // defined(ANGLE_WITH_MSAN)
}

inline bool IsTSan()
{
#if defined(ANGLE_WITH_TSAN)
    return true;
#else
    return false;
#endif  // defined(ANGLE_WITH_TSAN)
}

inline bool IsUBSan()
{
#if defined(ANGLE_WITH_UBSAN)
    return true;
#else
    return false;
#endif  // defined(ANGLE_WITH_UBSAN)
}
#endif  // COMMON_ANGLEUTILS_H_
