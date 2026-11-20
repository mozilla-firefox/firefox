//
// Copyright 2018 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// vk_helpers:
//   Helper utility classes that manage Vulkan resources.

#include "libANGLE/renderer/vulkan/vk_helpers.h"

#include <array>

#include "common/unsafe_buffers.h"

#include "common/aligned_memory.h"
#include "common/utilities.h"
#include "common/vulkan/vk_headers.h"
#include "image_util/loadimage.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/renderer/driver_utils.h"
#include "libANGLE/renderer/renderer_utils.h"
#include "libANGLE/renderer/vulkan/BufferVk.h"
#include "libANGLE/renderer/vulkan/ContextVk.h"
#include "libANGLE/renderer/vulkan/DisplayVk.h"
#include "libANGLE/renderer/vulkan/FramebufferVk.h"
#include "libANGLE/renderer/vulkan/RenderTargetVk.h"
#include "libANGLE/renderer/vulkan/android/vk_android_utils.h"
#include "libANGLE/renderer/vulkan/vk_ref_counted_event.h"
#include "libANGLE/renderer/vulkan/vk_renderer.h"
#include "libANGLE/renderer/vulkan/vk_utils.h"

namespace rx
{
namespace vk
{
namespace
{
// During descriptorSet cache eviction, we keep it in the cache only if it is recently used. If it
// has not been used in the past kDescriptorSetCacheRetireAge frames, it will be evicted.
constexpr uint32_t kDescriptorSetCacheRetireAge = 10;

// ANGLE_robust_resource_initialization requires color textures to be initialized to zero.
constexpr VkClearColorValue kRobustInitColorValue = {{0, 0, 0, 0}};
// When emulating a texture, we want the emulated channels to be 0, with alpha 1.
constexpr VkClearColorValue kEmulatedInitColorValue = {{0, 0, 0, 1.0f}};
// ANGLE_robust_resource_initialization requires depth to be initialized to 1 and stencil to 0.
// We are fine with these values for emulated depth/stencil textures too.
constexpr VkClearDepthStencilValue kRobustInitDepthStencilValue = {1.0f, 0};

constexpr VkImageAspectFlags kDepthStencilAspects =
    VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

// A flag that is used to allow to fetch raw GPU pointer to a buffer and can be used when
// supportsBufferDeviceAddress is enabled
constexpr VkMemoryAllocateFlagsInfo kMemoryAllocateDeviceAddressFlags = {
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    0};

EventStage GetImageAccessEventStage(Renderer *renderer, ImageAccess imageAccess)
{
    const ImageMemoryBarrierData &barrierData = renderer->getImageMemoryBarrierData(imageAccess);
    return barrierData.eventStage;
}

bool HasBothDepthAndStencilAspects(VkImageAspectFlags aspectFlags)
{
    return IsMaskFlagSet(aspectFlags, kDepthStencilAspects);
}

uint8_t GetContentDefinedLayerRangeBits(gl::OwnerLayer layerStart,
                                        uint32_t layerCount,
                                        uint32_t maxLayerCount)
{
    uint8_t layerRangeBits = layerCount >= maxLayerCount ? static_cast<uint8_t>(~0u)
                                                         : angle::BitMask<uint8_t>(layerCount);
    layerRangeBits <<= layerStart.get();

    return layerRangeBits;
}

uint32_t GetImageLayerCountForView(const ImageHelper &image)
{
    // Depth > 1 means this is a 3D texture and depth is our layer count
    return image.getExtents().depth > 1 ? image.getExtents().depth : image.getLayerCount();
}

bool IsClearValueEqual(VkImageAspectFlags aspect, const VkClearValue &a, const VkClearValue &b)
{
    if (aspect & VK_IMAGE_ASPECT_COLOR_BIT)
    {
        return ANGLE_UNSAFE_TODO(memcmp(&a.color, &b.color, sizeof(VkClearColorValue))) == 0;
    }

    bool depthEqual   = true;
    bool stencilEqual = true;
    if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
    {
        depthEqual = (a.depthStencil.depth == b.depthStencil.depth);
    }
    if (aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        stencilEqual = (a.depthStencil.stencil == b.depthStencil.stencil);
    }
    return depthEqual && stencilEqual;
}

void ReleaseImageViews(ImageViewVector *imageViewVector, GarbageObjects *garbage)
{
    for (ImageView &imageView : *imageViewVector)
    {
        if (imageView.valid())
        {
            garbage->emplace_back(GetGarbage(&imageView));
        }
    }
    imageViewVector->clear();
}

void DestroyImageViews(ImageViewVector *imageViewVector, VkDevice device)
{
    for (ImageView &imageView : *imageViewVector)
    {
        imageView.destroy(device);
    }
    imageViewVector->clear();
}

void ReleaseLayerLevelImageViews(LayerLevelImageViewVector *imageViewVector,
                                 GarbageObjects *garbage)
{
    for (ImageViewVector &layerViews : *imageViewVector)
    {
        for (ImageView &imageView : layerViews)
        {
            if (imageView.valid())
            {
                garbage->emplace_back(GetGarbage(&imageView));
            }
        }
    }
    imageViewVector->clear();
}

void DestroyLayerLevelImageViews(LayerLevelImageViewVector *imageViewVector, VkDevice device)
{
    for (ImageViewVector &layerViews : *imageViewVector)
    {
        for (ImageView &imageView : layerViews)
        {
            imageView.destroy(device);
        }
    }
    imageViewVector->clear();
}

void ReleaseSubresourceImageViews(SubresourceImageViewMap *imageViews, GarbageObjects *garbage)
{
    for (auto &iter : *imageViews)
    {
        std::unique_ptr<ImageView> &imageView = iter.second;
        if (imageView->valid())
        {
            garbage->emplace_back(GetGarbage(imageView.get()));
        }
    }
    imageViews->clear();
}

void DestroySubresourceImageViews(SubresourceImageViewMap *imageViews, VkDevice device)
{
    for (auto &iter : *imageViews)
    {
        std::unique_ptr<ImageView> &imageView = iter.second;
        imageView->destroy(device);
    }
    imageViews->clear();
}

ImageView *GetLevelImageView(ImageViewVector *imageViews, LevelIndex levelVk, uint32_t levelCount)
{
    // Lazily allocate the storage for image views. We allocate the full level count because we
    // don't want to trigger any std::vector reallocations. Reallocations could invalidate our
    // view pointers.
    if (imageViews->empty())
    {
        imageViews->resize(levelCount);
    }
    ASSERT(imageViews->size() > levelVk.get());

    return &(*imageViews)[levelVk.get()];
}

ImageView *GetLevelLayerImageView(LayerLevelImageViewVector *imageViews,
                                  LevelIndex levelVk,
                                  LayerIndex layer,
                                  uint32_t levelCount,
                                  uint32_t layerCount)
{
    // Lazily allocate the storage for image views. We allocate the full layer count because we
    // don't want to trigger any std::vector reallocations. Reallocations could invalidate our
    // view pointers.
    if (imageViews->empty())
    {
        imageViews->resize(layerCount);
    }
    ASSERT(imageViews->size() > layer.get());

    return GetLevelImageView(&(*imageViews)[layer.get()], levelVk, levelCount);
}

// Special rules apply to VkBufferImageCopy with depth/stencil. The components are tightly packed
// into a depth or stencil section of the destination buffer. See the spec:
// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkBufferImageCopy.html
const angle::Format &GetDepthStencilImageToBufferFormat(const angle::Format &imageFormat,
                                                        VkImageAspectFlagBits copyAspect)
{
    if (copyAspect == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        ASSERT(imageFormat.id == angle::FormatID::D24_UNORM_S8_UINT ||
               imageFormat.id == angle::FormatID::D32_FLOAT_S8X24_UINT ||
               imageFormat.id == angle::FormatID::S8_UINT);
        return angle::Format::Get(angle::FormatID::S8_UINT);
    }

    ASSERT(copyAspect == VK_IMAGE_ASPECT_DEPTH_BIT);

    switch (imageFormat.id)
    {
        case angle::FormatID::D16_UNORM:
            return imageFormat;
        case angle::FormatID::D24_UNORM_X8_UINT:
            return imageFormat;
        case angle::FormatID::D24_UNORM_S8_UINT:
            return angle::Format::Get(angle::FormatID::D24_UNORM_X8_UINT);
        case angle::FormatID::D32_FLOAT:
            return imageFormat;
        case angle::FormatID::D32_FLOAT_S8X24_UINT:
            return angle::Format::Get(angle::FormatID::D32_FLOAT);
        default:
            UNREACHABLE();
            return imageFormat;
    }
}

VkClearValue GetRobustResourceClearValue(const angle::Format &intendedFormat,
                                         const angle::Format &actualFormat)
{
    VkClearValue clearValue = {};
    if (intendedFormat.hasDepthOrStencilBits())
    {
        clearValue.depthStencil = kRobustInitDepthStencilValue;
    }
    else
    {
        clearValue.color = HasEmulatedImageChannels(intendedFormat, actualFormat)
                               ? kEmulatedInitColorValue
                               : kRobustInitColorValue;
    }
    return clearValue;
}

bool IsShaderReadOnlyAccess(ImageAccess imageAccess)
{
    switch (imageAccess)
    {
        case ImageAccess::ExternalShadersReadOnly:
        case ImageAccess::VertexShaderReadOnly:
        case ImageAccess::PreFragmentShadersReadOnly:
        case ImageAccess::FragmentShaderReadOnly:
        case ImageAccess::ComputeShaderReadOnly:
        case ImageAccess::AllGraphicsShadersReadOnly:
        // We also use read-only depth/stencil for texture sample from depth texture. See
        // GetImageReadAccess() for details.
        case ImageAccess::DepthReadStencilRead:
        case ImageAccess::DepthReadStencilReadFragmentShaderRead:
        case ImageAccess::DepthReadStencilReadAllShadersRead:
            return true;
        default:
            return false;
    }
}

bool IsAnySubresourceContentDefined(const gl::TexLevelArray<angle::BitSet8<8>> &contentDefined)
{
    for (const angle::BitSet8<8> &levelContentDefined : contentDefined)
    {
        if (levelContentDefined.any())
        {
            return true;
        }
    }
    return false;
}

void ExtendRenderPassInvalidateArea(const gl::Rectangle &invalidateArea, gl::Rectangle *out)
{
    if (out->empty())
    {
        *out = invalidateArea;
    }
    else
    {
        gl::ExtendRectangle(*out, invalidateArea, out);
    }
}

bool CanCopyWithTransferForCopyImage(Renderer *renderer,
                                     ImageHelper *srcImage,
                                     ImageHelper *dstImage)
{
    // Neither source nor destination formats can be emulated for copy image through transfer,
    // unless they are emulated with the same format!
    bool isFormatCompatible =
        (!srcImage->hasEmulatedImageFormat() && !dstImage->hasEmulatedImageFormat()) ||
        srcImage->getActualFormatID() == dstImage->getActualFormatID();

    // If neither formats are emulated, GL validation ensures that pixelBytes is the same for both.
    ASSERT(!isFormatCompatible ||
           srcImage->getActualFormat().pixelBytes == dstImage->getActualFormat().pixelBytes);

    return isFormatCompatible &&
           CanCopyWithTransfer(renderer, srcImage->getUsage(), dstImage->getActualFormatID(),
                               dstImage->getTilingMode());
}

void ReleaseBufferListToRenderer(Context *context, BufferHelperQueue *buffers)
{
    for (std::unique_ptr<BufferHelper> &toFree : *buffers)
    {
        toFree->release(context);
    }
    buffers->clear();
}

void DestroyBufferList(Renderer *renderer, BufferHelperQueue *buffers)
{
    for (std::unique_ptr<BufferHelper> &toDestroy : *buffers)
    {
        toDestroy->destroy(renderer);
    }
    buffers->clear();
}

// Helper functions used below
char GetLoadOpShorthand(RenderPassLoadOp loadOp)
{
    switch (loadOp)
    {
        case RenderPassLoadOp::Clear:
            return 'C';
        case RenderPassLoadOp::Load:
            return 'L';
        case RenderPassLoadOp::None:
            return 'N';
        default:
            return 'D';
    }
}

char GetStoreOpShorthand(RenderPassStoreOp storeOp)
{
    switch (storeOp)
    {
        case RenderPassStoreOp::Store:
            return 'S';
        case RenderPassStoreOp::None:
            return 'N';
        default:
            return 'D';
    }
}

bool IsClear(UpdateSource updateSource)
{
    return updateSource == UpdateSource::Clear ||
           updateSource == UpdateSource::ClearEmulatedChannelsOnly ||
           updateSource == UpdateSource::ClearAfterInvalidate;
}

bool IsClearOfAllChannels(UpdateSource updateSource)
{
    return updateSource == UpdateSource::Clear ||
           updateSource == UpdateSource::ClearAfterInvalidate;
}

template <typename Offset, typename Extent>
gl::Box MakeUpdateBoundingBox(const Offset &offset,
                              const Extent &extents,
                              gl::OwnerLayer layerIndex,
                              uint32_t layerCount)
{
    gl::Box updateBoundingBox = {offset, extents};
    // 3D-image updates use the layer index/count for z/depth.
    updateBoundingBox.z     = layerIndex.get();
    updateBoundingBox.depth = layerCount;
    return updateBoundingBox;
}

angle::Result InitDynamicDescriptorPool(ErrorContext *context,
                                        const DescriptorSetLayoutDesc &descriptorSetLayoutDesc,
                                        const DescriptorSetLayout &descriptorSetLayout,
                                        uint32_t descriptorCountMultiplier,
                                        DynamicDescriptorPool *poolToInit)
{
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes;
    DescriptorSetLayoutBindingVector bindingVector;
    descriptorSetLayoutDesc.unpackBindings(&bindingVector);
    descriptorPoolSizes.reserve(bindingVector.size());

    for (const VkDescriptorSetLayoutBinding &binding : bindingVector)
    {
        if (binding.descriptorCount > 0)
        {
            VkDescriptorPoolSize poolSize = {};
            poolSize.type                 = binding.descriptorType;
            poolSize.descriptorCount      = binding.descriptorCount * descriptorCountMultiplier;
            descriptorPoolSizes.emplace_back(poolSize);
        }
    }

    if (!descriptorPoolSizes.empty())
    {
        ANGLE_TRY(poolToInit->init(context, descriptorPoolSizes.data(), descriptorPoolSizes.size(),
                                   descriptorSetLayout));
    }

    return angle::Result::Continue;
}

bool IsAnyLayout(VkImageLayout needle, const VkImageLayout *haystack, uint32_t haystackCount)
{
    const VkImageLayout *haystackEnd = ANGLE_UNSAFE_TODO(haystack + haystackCount);
    return std::find(haystack, haystackEnd, needle) != haystackEnd;
}

gl::TexLevelMask AggregateSkipLevelsAnyFaceSkipped(
    const gl::CubeFaceArray<gl::TexLevelMask> &skipLevels)
{
    gl::TexLevelMask skipLevelsAnyFace = skipLevels[0];
    for (size_t face = 1; face < gl::kCubeFaceCount; ++face)
    {
        skipLevelsAnyFace |= skipLevels[face];
    }
    return skipLevelsAnyFace;
}

gl::TexLevelMask AggregateSkipLevelsAllFacesSkipped(
    const gl::CubeFaceArray<gl::TexLevelMask> &skipLevels,
    gl::TextureType textureType)
{
    gl::TexLevelMask skipLevelsAllFaces = skipLevels[0];
    if (textureType == gl::TextureType::CubeMap)
    {
        for (size_t face = 1; face < gl::kCubeFaceCount; ++face)
        {
            skipLevelsAllFaces &= skipLevels[face];
        }
    }
    return skipLevelsAllFaces;
}

// Get layer mask for a particular image level.
ImageLayerWriteMask GetImageLayerWriteMask(gl::OwnerLayer layerStart, uint32_t layerCount)
{
    ImageLayerWriteMask layerMask = angle::BitMask<uint64_t>(layerCount);
    uint32_t rotateShift          = layerStart.get() % kMaxParallelLayerWrites;
    layerMask = (layerMask << rotateShift) | (layerMask >> (kMaxParallelLayerWrites - rotateShift));
    return layerMask;
}

ImageSubresourceRange MakeImageSubresourceReadRange(gl::OwnerLevel level,
                                                    uint32_t levelCount,
                                                    gl::OwnerLayer layer,
                                                    LayerMode layerMode,
                                                    ImageViewColorspace readColorspace,
                                                    ImageViewColorspace writeColorspace)
{
    ImageSubresourceRange range;

    SetBitField(range.level, level.get());
    SetBitField(range.levelCount, levelCount);
    SetBitField(range.layer, layer.get());
    SetBitField(range.layerMode, layerMode);
    SetBitField(range.readColorspace, readColorspace == ImageViewColorspace::SRGB ? 1 : 0);
    SetBitField(range.writeColorspace, writeColorspace == ImageViewColorspace::SRGB ? 1 : 0);

    return range;
}

ImageSubresourceRange MakeImageSubresourceDrawRange(gl::OwnerLevel level,
                                                    gl::OwnerLayer layer,
                                                    LayerMode layerMode,
                                                    ImageViewColorspace readColorspace,
                                                    ImageViewColorspace writeColorspace)
{
    ImageSubresourceRange range;

    SetBitField(range.level, level.get());
    SetBitField(range.levelCount, 1);
    SetBitField(range.layer, layer.get());
    SetBitField(range.layerMode, layerMode);
    SetBitField(range.readColorspace, readColorspace == ImageViewColorspace::SRGB ? 1 : 0);
    SetBitField(range.writeColorspace, writeColorspace == ImageViewColorspace::SRGB ? 1 : 0);

    return range;
}

// Obtain VkClearColorValue from input byte data and actual format.
void GetVkClearColorValueFromBytes(uint8_t *actualData,
                                   const angle::Format &actualFormat,
                                   VkClearValue *clearValueOut)
{
    ASSERT(actualData != nullptr && !actualFormat.hasDepthOrStencilBits());

    *clearValueOut               = {};
    VkClearColorValue colorValue = {{}};
    actualFormat.pixelReadFunction(actualData, reinterpret_cast<uint8_t *>(&colorValue));
    clearValueOut->color = colorValue;
}

// Obtain VkClearDepthStencilValue from input byte data and intended format.
void GetVkClearDepthStencilValueFromBytes(uint8_t *intendedData,
                                          const angle::Format &intendedFormat,
                                          VkClearValue *clearValueOut)
{
    ASSERT(intendedData != nullptr && intendedFormat.hasDepthOrStencilBits());

    *clearValueOut     = {};
    uint32_t dsData[4] = {0};
    double depthValue  = 0;

    intendedFormat.pixelReadFunction(intendedData, reinterpret_cast<uint8_t *>(dsData));
    ANGLE_UNSAFE_TODO(memcpy(&depthValue, &dsData[0], sizeof(double)));
    clearValueOut->depthStencil.depth   = static_cast<float>(depthValue);
    clearValueOut->depthStencil.stencil = dsData[2];
}

VkPipelineStageFlags ConvertShaderBitSetToVkPipelineStageFlags(
    const gl::ShaderBitSet &writeShaderStages)
{
    VkPipelineStageFlags pipelineStageFlags = 0;
    for (gl::ShaderType shaderType : writeShaderStages)
    {
        const PipelineStage stage = GetPipelineStage(shaderType);
        pipelineStageFlags |= GetBufferMemoryBarrierData(stage).pipelineStageFlags;
    }
    return pipelineStageFlags;
}

// Temporarily updating an image's chromaFilter and restore it at the end.
class [[nodiscard]] ScopedOverrideYCbCrFilter final
{
  public:
    ScopedOverrideYCbCrFilter(Renderer *renderer, ImageHelper *img, const VkFilter &filter)
        : mRenderer(renderer),
          mImage(img),
          mOriginalFilter(img->getYcbcrConversionDesc().getChromaFilter())
    {
        img->updateChromaFilter(renderer, filter);
    }

    ~ScopedOverrideYCbCrFilter()
    {
        mImage->updateChromaFilter(mRenderer, mOriginalFilter);
        mRenderer = nullptr;
        mImage    = nullptr;
    }

  private:
    Renderer *mRenderer;
    ImageHelper *mImage;
    VkFilter mOriginalFilter;
};

// image usage flags that allowed for VK_QCOM_tile_memory_heap
constexpr VkImageUsageFlags kQCOMTileMemoryAllowedImageUsageBits =
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

// Returns true if it is compatible with VK_QCOM_tile_memory_heap.
// If compatible and |transferSrcSupported| is non-null, also probes whether
// TRANSFER_SRC_BIT can be combined with tile memory for this format, and sets
// *transferSrcSupported accordingly.
bool IsTileMemoryCompatible(const vk::Renderer *renderer,
                            const VkImageCreateInfo &createInfo,
                            bool *transferSrcSupported)
{
    // First check general conditions specified in
    // https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_QCOM_tile_memory_heap.adoc
    bool compatible = createInfo.imageType == VK_IMAGE_TYPE_2D &&
                      createInfo.tiling == VK_IMAGE_TILING_OPTIMAL && createInfo.flags == 0 &&
                      createInfo.mipLevels == 1 && createInfo.arrayLayers == 1 &&
                      createInfo.samples == VK_SAMPLE_COUNT_1_BIT &&
                      (createInfo.usage & ~kQCOMTileMemoryAllowedImageUsageBits) == 0;

    // If the driver actually supports VK_QCOM_tile_memory_heap, we must also check the format with
    // vulkan driver. Otherwise we just skip format check for simulation.
    if (renderer->getFeatures().supportsTileMemoryHeap.enabled && compatible)
    {
        VkImageUsageFlags usage = createInfo.usage | VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM;
        compatible              = vk::ImageHelper::FormatSupportsUsage(
            renderer, createInfo.format, createInfo.imageType, createInfo.tiling, usage,
            createInfo.flags, nullptr, nullptr,
            vk::ImageHelper::FormatSupportCheck::OnlyQuerySuccess);

        if (compatible && transferSrcSupported != nullptr)
        {
            // Probe whether TRANSFER_SRC_BIT is also supported alongside tile memory.
            // If so, vkCmdCopyImage can be used during fallback instead of a shader-based blit.
            VkImageUsageFlags transferSrcUsage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            *transferSrcSupported              = vk::ImageHelper::FormatSupportsUsage(
                renderer, createInfo.format, createInfo.imageType, createInfo.tiling,
                transferSrcUsage, createInfo.flags, nullptr, nullptr,
                vk::ImageHelper::FormatSupportCheck::OnlyQuerySuccess);
        }
    }

    return compatible;
}

const VkImageFormatListCreateInfoKHR *GetImageFormatListCreateInfo(const void *createInfoPNext)
{
    const VkBaseInStructure *pNextChain =
        reinterpret_cast<const VkBaseInStructure *>(createInfoPNext);
    while (pNextChain != nullptr &&
           pNextChain->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR)
    {
        pNextChain = pNextChain->pNext;
    }

    return reinterpret_cast<const VkImageFormatListCreateInfoKHR *>(pNextChain);
}

void DeriveImageViewFormatsFromExternalCreateInfo(const void *externalCreateInfo,
                                                  vk::ImageHelper::ImageFormats *imageFormats)
{
    const VkImageFormatListCreateInfoKHR *imageFormatListCreateInfo =
        GetImageFormatListCreateInfo(externalCreateInfo);

    if (imageFormatListCreateInfo != nullptr)
    {
        // Clear out any stale entries
        ASSERT(imageFormats);
        imageFormats->clear();

        for (uint32_t i = 0; i < imageFormatListCreateInfo->viewFormatCount; i++)
        {
            imageFormats->push_back(
                *(ANGLE_UNSAFE_TODO(imageFormatListCreateInfo->pViewFormats + i)));
        }
    }
}
}  // anonymous namespace

// This is an arbitrary max. We can change this later if necessary.
uint32_t DynamicDescriptorPool::mMaxSetsPerPool           = 16;
uint32_t DynamicDescriptorPool::mMaxSetsPerPoolMultiplier = 2;

bool FormatHasNecessaryFeature(Renderer *renderer,
                               angle::FormatID formatID,
                               VkImageTiling tilingMode,
                               VkFormatFeatureFlags featureBits)
{
    return (tilingMode == VK_IMAGE_TILING_OPTIMAL)
               ? renderer->hasImageFormatFeatureBits(formatID, featureBits)
               : renderer->hasLinearImageFormatFeatureBits(formatID, featureBits);
}

bool CanCopyWithTransfer(Renderer *renderer,
                         VkImageUsageFlags srcUsage,
                         angle::FormatID dstFormatID,
                         VkImageTiling dstTilingMode)
{
    // Checks that the formats in the copy transfer have the appropriate transfer bits
    bool srcFormatHasNecessaryFeature = (srcUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    bool dstFormatHasNecessaryFeature = FormatHasNecessaryFeature(
        renderer, dstFormatID, dstTilingMode, VK_FORMAT_FEATURE_TRANSFER_DST_BIT);

    return srcFormatHasNecessaryFeature && dstFormatHasNecessaryFeature;
}

// Context implementation
Context::Context(Renderer *renderer)
    : ErrorContext(renderer), mShareGroupRefCountedEventsGarbageRecycler(nullptr)
{}

Context::~Context()
{
    ASSERT(mForeignImagesInUse.empty());
}

void Context::onForeignImageUse(ImageHelper *image)
{
    // The image might be used multiple times in the same frame, |mForeignImagesInUse| is a "set"
    // so the image is tracked only once.
    mForeignImagesInUse.insert(image);
}

void Context::finalizeForeignImage(ImageHelper *image)
{
    // The image must have been marked as in use, otherwise finalize is called while the initial use
    // was missed.
    ASSERT(mForeignImagesInUse.find(image) != mForeignImagesInUse.end());
    // The image must not already be finalized.
    ASSERT(
        std::find_if(mImagesToTransitionToForeign.begin(), mImagesToTransitionToForeign.end(),
                     [image = image->getImage().getHandle()](const VkImageMemoryBarrier &barrier) {
                         return barrier.image == image;
                     }) == mImagesToTransitionToForeign.end());

    mImagesToTransitionToForeign.push_back(image->releaseToForeign(mRenderer));
    mForeignImagesInUse.erase(image);
}

void Context::finalizeAllForeignImages()
{
    mImagesToTransitionToForeign.reserve(mImagesToTransitionToForeign.size() +
                                         mForeignImagesInUse.size());
    while (!mForeignImagesInUse.empty())
    {
        finalizeForeignImage(*mForeignImagesInUse.begin());
    }
}

// PackedClearValuesArray implementation
PackedClearValuesArray::PackedClearValuesArray() : mValues{} {}
PackedClearValuesArray::~PackedClearValuesArray() = default;

PackedClearValuesArray::PackedClearValuesArray(const PackedClearValuesArray &other) = default;
PackedClearValuesArray &PackedClearValuesArray::operator=(const PackedClearValuesArray &rhs) =
    default;

void PackedClearValuesArray::storeColor(PackedAttachmentIndex index, const VkClearValue &clearValue)
{
    mValues[index.get()] = clearValue;
}

void PackedClearValuesArray::storeDepthStencil(PackedAttachmentIndex index,
                                               const VkClearValue &clearValue)
{
    mValues[index.get()] = clearValue;
}

// RenderPassAttachment implementation
RenderPassAttachment::RenderPassAttachment()
{
    reset();
}

void RenderPassAttachment::init(ImageHelper *image,
                                gl::OwnerLevel levelIndex,
                                gl::OwnerLayer layerIndex,
                                uint32_t layerCount,
                                VkImageAspectFlagBits aspect)
{
    ASSERT(mImage == nullptr);

    mImage              = image;
    mLevelIndex         = levelIndex;
    mLayerIndex         = layerIndex;
    mLayerCount         = layerCount;
    mAspect             = aspect;
}

void RenderPassAttachment::reset()
{
    mImage = nullptr;

    mAccess = ResourceAccess::Unused;

    mInvalidatedCmdCount = kInfiniteCmdCount;
    mDisabledCmdCount    = kInfiniteCmdCount;
    mClearedCmdCount     = kInfiniteCmdCount;
    mClearValue          = {};
    mInvalidateArea      = gl::Rectangle();
}

void RenderPassAttachment::onAccess(ResourceAccess access, uint32_t currentCmdCount)
{
    // Update the access for optimizing this render pass's loadOp
    UpdateAccess(&mAccess, access);

    if (HasResourceWriteAccess(access))
    {
        mClearedCmdCount = kInfiniteCmdCount;
    }

    // Update the invalidate state for optimizing this render pass's storeOp
    if (onAccessImpl(access, currentCmdCount))
    {
        // The attachment is no longer invalid, so restore its content.
        restoreContent();
    }
}

void RenderPassAttachment::invalidate(const gl::Rectangle &invalidateArea,
                                      bool isAttachmentEnabled,
                                      uint32_t currentCmdCount)
{
    // Keep track of the command count in the render pass at the time of invalidation.  If there are
    // more commands in the future, invalidate must be undone.
    mInvalidatedCmdCount = currentCmdCount;

    // Also track the command count if the attachment is currently disabled.
    mDisabledCmdCount = isAttachmentEnabled ? kInfiniteCmdCount : currentCmdCount;

    // Set/extend the invalidate area.
    ExtendRenderPassInvalidateArea(invalidateArea, &mInvalidateArea);
}

void RenderPassAttachment::onRenderAreaGrowth(ContextVk *contextVk,
                                              const gl::Rectangle &newRenderArea)
{
    // Remove invalidate if it's no longer applicable.
    if (mInvalidateArea.empty() || mInvalidateArea.encloses(newRenderArea))
    {
        return;
    }

    ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_LOW,
                          "InvalidateSubFramebuffer discarded due to increased scissor region");

    mInvalidateArea      = gl::Rectangle();
    mInvalidatedCmdCount = kInfiniteCmdCount;
}

void RenderPassAttachment::finalizeLoadStore(ErrorContext *context,
                                             uint32_t currentCmdCount,
                                             bool hasUnresolveAttachment,
                                             bool hasResolveAttachment,
                                             RenderPassLoadOp *loadOp,
                                             RenderPassStoreOp *storeOp,
                                             bool *isInvalidatedOut)
{
    // If the attachment is invalidated, skip the store op.  If we are not loading or clearing the
    // attachment and the attachment has not been used, auto-invalidate it.
    const bool notLoaded = *loadOp == RenderPassLoadOp::DontCare && !hasUnresolveAttachment;
    if (isInvalidated(currentCmdCount) || (notLoaded && !HasResourceWriteAccess(mAccess)))
    {
        *storeOp          = RenderPassStoreOp::DontCare;
        *isInvalidatedOut = true;
    }
    else if (hasWriteAfterInvalidate(currentCmdCount))
    {
        // The attachment was invalidated, but is now valid.  Let the image know the contents are
        // now defined so a future render pass would use loadOp=LOAD.
        restoreContent();
    }

    // For read only depth stencil, we can use StoreOpNone if available.  DontCare is still
    // preferred, so do this after handling DontCare.
    const bool supportsLoadStoreOpNone =
        context->getFeatures().supportsRenderPassLoadStoreOpNone.enabled;
    const bool supportsStoreOpNone =
        supportsLoadStoreOpNone || context->getFeatures().supportsRenderPassStoreOpNone.enabled;
    if (mAccess == ResourceAccess::ReadOnly && supportsStoreOpNone)
    {
        if (*storeOp == RenderPassStoreOp::Store && *loadOp != RenderPassLoadOp::Clear)
        {
            *storeOp = RenderPassStoreOp::None;
        }
    }

    if (mAccess == ResourceAccess::Unused)
    {
        if (*storeOp != RenderPassStoreOp::DontCare)
        {
            switch (*loadOp)
            {
                case RenderPassLoadOp::Clear:
                    // Cannot optimize away the ops if the attachment is cleared (even if not used
                    // afterwards)
                    break;
                case RenderPassLoadOp::Load:
                    // Make sure the attachment is neither loaded nor stored (as it's neither used
                    // nor invalidated), if possible.
                    if (supportsLoadStoreOpNone)
                    {
                        *loadOp = RenderPassLoadOp::None;
                    }
                    if (supportsStoreOpNone)
                    {
                        *storeOp = RenderPassStoreOp::None;
                    }
                    break;
                case RenderPassLoadOp::DontCare:
                    // loadOp=DontCare should be covered by storeOp=DontCare below.
                    break;
                case RenderPassLoadOp::None:
                    break;
                default:
                    UNREACHABLE();
                    break;
            }
        }
    }

    if (mAccess == ResourceAccess::Unused || (mAccess == ResourceAccess::ReadOnly && notLoaded))
    {
        // If we are loading or clearing the attachment, but the attachment has not been used,
        // and the data has also not been stored back into attachment, then just skip the
        // load/clear op. If loadOp/storeOp=None is supported, prefer that to reduce the amount
        // of synchronization; DontCare is a write operation, while None is not.
        //
        // Don't optimize away a Load or Clear if there is a resolve attachment. Although the
        // storeOp=DontCare the image content needs to be resolved into the resolve attachment.
        const bool attachmentNeedsToBeResolved =
            hasResolveAttachment &&
            (*loadOp == RenderPassLoadOp::Load || *loadOp == RenderPassLoadOp::Clear);
        if (*storeOp == RenderPassStoreOp::DontCare && !attachmentNeedsToBeResolved)
        {
            if (supportsLoadStoreOpNone && !isInvalidated(currentCmdCount))
            {
                *loadOp  = RenderPassLoadOp::None;
                *storeOp = RenderPassStoreOp::None;
            }
            else
            {
                *loadOp = RenderPassLoadOp::DontCare;
            }
        }
    }
}

void RenderPassAttachment::restoreContent()
{
    // Note that the image may have been deleted since the render pass has started.
    if (mImage)
    {
        ASSERT(mImage->valid());
        if (mAspect == VK_IMAGE_ASPECT_STENCIL_BIT)
        {
            mImage->restoreSubresourceStencilContent(mLevelIndex, mLayerIndex, mLayerCount);
        }
        else
        {
            mImage->restoreSubresourceContent(mLevelIndex, mLayerIndex, mLayerCount);
        }
        mInvalidateArea = gl::Rectangle();
    }
}

bool RenderPassAttachment::hasWriteAfterInvalidate(uint32_t currentCmdCount) const
{
    return (mInvalidatedCmdCount != kInfiniteCmdCount &&
            std::min(mDisabledCmdCount, currentCmdCount) != mInvalidatedCmdCount);
}

bool RenderPassAttachment::isInvalidated(uint32_t currentCmdCount) const
{
    return mInvalidatedCmdCount != kInfiniteCmdCount &&
           std::min(mDisabledCmdCount, currentCmdCount) == mInvalidatedCmdCount;
}

bool RenderPassAttachment::isAttachmentCleared(uint32_t currentCmdCount) const
{
    return mClearedCmdCount != kInfiniteCmdCount &&
           std::min(mDisabledCmdCount, currentCmdCount) == mClearedCmdCount;
}

bool RenderPassAttachment::isClearRedundant(uint32_t currentCmdCount,
                                            const VkClearValue &clearValue) const
{
    return isAttachmentCleared(currentCmdCount) &&
           IsClearValueEqual(mAspect, mClearValue, clearValue);
}

bool RenderPassAttachment::onAccessImpl(ResourceAccess access, uint32_t currentCmdCount)
{
    if (mInvalidatedCmdCount == kInfiniteCmdCount)
    {
        // If never invalidated or no longer invalidated, return early.
        return false;
    }
    if (HasResourceWriteAccess(access))
    {
        // Drawing to this attachment is being enabled.  Assume that drawing will immediately occur
        // after this attachment is enabled, and that means that the attachment will no longer be
        // invalidated.
        mInvalidatedCmdCount = kInfiniteCmdCount;
        mDisabledCmdCount    = kInfiniteCmdCount;
        // Return true to indicate that the store op should remain STORE and that
        // mVkImageContentDefined should be set to true;
        return true;
    }
    // Drawing to this attachment is being disabled.
    if (hasWriteAfterInvalidate(currentCmdCount))
    {
        // The attachment was previously drawn while enabled, and so is no longer invalidated.
        mInvalidatedCmdCount = kInfiniteCmdCount;
        mDisabledCmdCount    = kInfiniteCmdCount;
        // Return true to indicate that the store op should remain STORE and that
        // mVkImageContentDefined should be set to true;
        return true;
    }

    // Use the latest CmdCount at the start of being disabled.  At the end of the render pass,
    // cmdCountDisabled is <= the actual command count, and so it's compared with
    // cmdCountInvalidated.  If the same, the attachment is still invalidated.
    mDisabledCmdCount = currentCmdCount;
    return false;
}

// CommandBufferHelperCommon implementation.
CommandBufferHelperCommon::CommandBufferHelperCommon()
    : mCommandPool(nullptr), mHasShaderStorageOutput(false), mHasGLMemoryBarrierIssued(false)
{}

CommandBufferHelperCommon::~CommandBufferHelperCommon() {}

void CommandBufferHelperCommon::initializeImpl() {}

void CommandBufferHelperCommon::resetImpl(ErrorContext *context)
{
    ASSERT(!mAcquireNextImageSemaphore.valid());
    mCommandAllocator.resetAllocator();
    ASSERT(!mIsAnyHostVisibleBufferWritten);

    ASSERT(mRefCountedEvents.empty());
    ASSERT(mRefCountedEventCollector.empty());

    ASSERT(mPipelineBarriers.isEmpty());
    ASSERT(mEventBarriers.isEmpty());
}

template <class DerivedT>
angle::Result CommandBufferHelperCommon::attachCommandPoolImpl(ErrorContext *context,
                                                               SecondaryCommandPool *commandPool)
{
    if constexpr (!DerivedT::ExecutesInline())
    {
        DerivedT *derived = static_cast<DerivedT *>(this);
        ASSERT(commandPool != nullptr);
        ASSERT(mCommandPool == nullptr);
        ASSERT(!derived->getCommandBuffer().valid());

        mCommandPool = commandPool;

        ANGLE_TRY(derived->initializeCommandBuffer(context));
    }
    return angle::Result::Continue;
}

template <class DerivedT, bool kIsRenderPassBuffer>
angle::Result CommandBufferHelperCommon::detachCommandPoolImpl(
    ErrorContext *context,
    SecondaryCommandPool **commandPoolOut)
{
    if constexpr (!DerivedT::ExecutesInline())
    {
        DerivedT *derived = static_cast<DerivedT *>(this);
        ASSERT(mCommandPool != nullptr);
        ASSERT(derived->getCommandBuffer().valid());

        if constexpr (!kIsRenderPassBuffer)
        {
            ASSERT(!derived->getCommandBuffer().empty());
            ANGLE_TRY(derived->endCommandBuffer(context));
        }

        *commandPoolOut = mCommandPool;
        mCommandPool    = nullptr;
    }
    ASSERT(mCommandPool == nullptr);
    return angle::Result::Continue;
}

template <class DerivedT>
void CommandBufferHelperCommon::releaseCommandPoolImpl()
{
    if constexpr (!DerivedT::ExecutesInline())
    {
        DerivedT *derived = static_cast<DerivedT *>(this);
        ASSERT(mCommandPool != nullptr);

        if (derived->getCommandBuffer().valid())
        {
            ASSERT(derived->getCommandBuffer().empty());
            mCommandPool->collect(&derived->getCommandBuffer());
        }

        mCommandPool = nullptr;
    }
    ASSERT(mCommandPool == nullptr);
}

template <class DerivedT>
ANGLE_INLINE void CommandBufferHelperCommon::assertCanBeRecycledImpl()
{
    DerivedT *derived = static_cast<DerivedT *>(this);
    ASSERT(mCommandPool == nullptr);
    // Vulkan secondary command buffers must be invalid (collected).
    ASSERT(DerivedT::ExecutesInline() || !derived->getCommandBuffer().valid());
    // ANGLEs Custom secondary command buffers must be empty (reset).
    ASSERT(!DerivedT::ExecutesInline() || derived->getCommandBuffer().empty());
}

void CommandBufferHelperCommon::bufferWrite(Context *context,
                                            VkAccessFlags writeAccessType,
                                            PipelineStage writeStage,
                                            BufferHelper *buffer)
{
    VkPipelineStageFlags writePipelineStageFlags =
        GetBufferMemoryBarrierData(writeStage).pipelineStageFlags;
    bufferWriteImpl(context, writeAccessType, writePipelineStageFlags, writeStage, buffer);
}

void CommandBufferHelperCommon::bufferWrite(Context *context,
                                            VkAccessFlags writeAccessType,
                                            const gl::ShaderBitSet &writeShaderStages,
                                            BufferHelper *buffer)
{
    VkPipelineStageFlags writePipelineStageFlags =
        ConvertShaderBitSetToVkPipelineStageFlags(writeShaderStages);
    PipelineStage firstWriteStage = GetPipelineStage(writeShaderStages.first());
    bufferWriteImpl(context, writeAccessType, writePipelineStageFlags, firstWriteStage, buffer);
}

void CommandBufferHelperCommon::bufferRead(Context *context,
                                           VkAccessFlags readAccessType,
                                           PipelineStage readStage,
                                           BufferHelper *buffer)
{
    VkPipelineStageFlags readPipelineStageFlags =
        GetBufferMemoryBarrierData(readStage).pipelineStageFlags;
    bufferReadImpl(context, readAccessType, readPipelineStageFlags, readStage, buffer);
}

void CommandBufferHelperCommon::bufferRead(Context *context,
                                           VkAccessFlags readAccessType,
                                           const gl::ShaderBitSet &readShaderStages,
                                           BufferHelper *buffer)
{
    for (const gl::ShaderType shaderType : readShaderStages)
    {
        PipelineStage readStage = GetPipelineStage(shaderType);
        VkPipelineStageFlags readPipelineStageFlags =
            GetBufferMemoryBarrierData(readStage).pipelineStageFlags;
        bufferReadImpl(context, readAccessType, readPipelineStageFlags, readStage, buffer);
    }
}

void CommandBufferHelperCommon::bufferWriteImpl(Context *context,
                                                VkAccessFlags writeAccessType,
                                                VkPipelineStageFlags writePipelineStageFlags,
                                                PipelineStage writeStage,
                                                BufferHelper *buffer)
{
    buffer->recordWriteBarrier(context, writeAccessType, writePipelineStageFlags, writeStage,
                               mQueueSerial, &mPipelineBarriers, &mEventBarriers,
                               &mRefCountedEventCollector);

    // Make sure host-visible buffer writes result in a barrier inserted at the end of the frame to
    // make the results visible to the host.  The buffer may be mapped by the application in the
    // future.
    if (buffer->isHostVisible())
    {
        mIsAnyHostVisibleBufferWritten = true;
    }

    buffer->recordWriteEvent(context, writeAccessType, writePipelineStageFlags, mQueueSerial,
                             writeStage, &mRefCountedEvents);
}

void CommandBufferHelperCommon::bufferReadImpl(Context *context,
                                               VkAccessFlags readAccessType,
                                               VkPipelineStageFlags readPipelineStageFlags,
                                               PipelineStage readStage,
                                               BufferHelper *buffer)
{
    buffer->recordReadBarrier(context, readAccessType, readPipelineStageFlags, readStage,
                              &mPipelineBarriers, &mEventBarriers, &mRefCountedEventCollector);
    ASSERT(!usesBufferForWrite(*buffer));

    buffer->recordReadEvent(context, readAccessType, readPipelineStageFlags, readStage,
                            mQueueSerial, GetBufferMemoryBarrierData(readStage).eventStage,
                            &mRefCountedEvents);
}

void CommandBufferHelperCommon::imageReadImpl(Context *context,
                                              VkImageAspectFlags aspectFlags,
                                              ImageAccess imageAccess,
                                              BarrierType barrierType,
                                              ImageHelper *image)
{
    if (image->isReadBarrierNecessary(context->getRenderer(), imageAccess))
    {
        updateImageLayoutAndBarrier(context, image, aspectFlags, imageAccess, barrierType);
    }
}

void CommandBufferHelperCommon::imageWriteImpl(Context *context,
                                               gl::OwnerLevel level,
                                               gl::OwnerLayer layerStart,
                                               uint32_t layerCount,
                                               VkImageAspectFlags aspectFlags,
                                               ImageAccess imageAccess,
                                               BarrierType barrierType,
                                               ImageHelper *image)
{
    const LevelIndex levelVk = image->toVkLevel(level);
    image->onWrite(levelVk, 1, layerStart, layerCount, aspectFlags);
    if (image->isWriteBarrierNecessary(imageAccess, levelVk, 1, layerStart, layerCount))
    {
        updateImageLayoutAndBarrier(context, image, aspectFlags, imageAccess, barrierType);
    }
}

void CommandBufferHelperCommon::updateImageLayoutAndBarrier(Context *context,
                                                            ImageHelper *image,
                                                            VkImageAspectFlags aspectFlags,
                                                            ImageAccess imageAccess,
                                                            BarrierType barrierType)
{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    image->updateLayoutAndBarrier(context, aspectFlags, imageAccess, barrierType, mQueueSerial,
                                  &mPipelineBarriers, &mEventBarriers, &mRefCountedEventCollector,
                                  &semaphore);
    // If image has an ANI semaphore, move it to command buffer so that we can wait for it in
    // next submission.
    if (semaphore != VK_NULL_HANDLE)
    {
        ASSERT(!mAcquireNextImageSemaphore.valid());
        mAcquireNextImageSemaphore.setHandle(semaphore);
    }
}

void CommandBufferHelperCommon::retainImageWithEvent(Context *context, ImageHelper *image)
{
    image->setQueueSerial(mQueueSerial);
    image->updatePipelineStageAccessHistory(context->getRenderer());

    if (context->getFeatures().useVkEventForImageBarrier.enabled)
    {
        image->setCurrentRefCountedEvent(context, &mRefCountedEvents);
    }
}

template <typename CommandBufferT>
void CommandBufferHelperCommon::flushSetEventsImpl(Context *context, CommandBufferT *commandBuffer)
{
    if (mRefCountedEvents.empty())
    {
        return;
    }

    // Add VkCmdSetEvent here to track the completion of this renderPass.
    mRefCountedEvents.flushSetEvents(context->getRenderer(), commandBuffer);
    // We no longer need event, so garbage collect it.
    mRefCountedEvents.releaseToEventCollector(&mRefCountedEventCollector);
}

template void CommandBufferHelperCommon::flushSetEventsImpl<priv::SecondaryCommandBuffer>(
    Context *context,
    priv::SecondaryCommandBuffer *commandBuffer);
template void CommandBufferHelperCommon::flushSetEventsImpl<VulkanSecondaryCommandBuffer>(
    Context *context,
    VulkanSecondaryCommandBuffer *commandBuffer);

void CommandBufferHelperCommon::executeBarriers(Renderer *renderer,
                                                CommandsState *commandsState,
                                                PrimaryCommandBuffer *primaryCommands)
{
    // Add ANI semaphore to the command submission.
    if (mAcquireNextImageSemaphore.valid())
    {
        commandsState->addWaitSemaphore(mAcquireNextImageSemaphore.release(),
                                        kSwapchainAcquireImageWaitStageFlags);
    }

    mPipelineBarriers.execute(renderer, primaryCommands);
    mEventBarriers.execute(renderer, primaryCommands);
}

void CommandBufferHelperCommon::addCommandDiagnosticsCommon(std::ostringstream *out)
{
    mPipelineBarriers.addDiagnosticsString(*out);
    mEventBarriers.addDiagnosticsString(*out);
}

// OutsideRenderPassCommandBufferHelper implementation.
OutsideRenderPassCommandBufferHelper::OutsideRenderPassCommandBufferHelper() {}

OutsideRenderPassCommandBufferHelper::~OutsideRenderPassCommandBufferHelper() {}

angle::Result OutsideRenderPassCommandBufferHelper::initialize(ErrorContext *context)
{
    initializeImpl();
    return initializeCommandBuffer(context);
}
angle::Result OutsideRenderPassCommandBufferHelper::initializeCommandBuffer(ErrorContext *context)
{
    // Skip initialization in the Pool-detached state.
    if (!ExecutesInline() && mCommandPool == nullptr)
    {
        return angle::Result::Continue;
    }
    return mCommandBuffer.initialize(context, mCommandPool, false,
                                     mCommandAllocator.getAllocator());
}

angle::Result OutsideRenderPassCommandBufferHelper::reset(
    ErrorContext *context,
    SecondaryCommandBufferCollector *commandBufferCollector)
{
    resetImpl(context);

    // Collect/Reset the command buffer
    commandBufferCollector->collectCommandBuffer(std::move(mCommandBuffer));
    mIsCommandBufferEnded = false;

    // Invalidate the queue serial here. We will get a new queue serial after commands flush.
    mQueueSerial = QueueSerial();

    return initializeCommandBuffer(context);
}

void OutsideRenderPassCommandBufferHelper::abandon(
    ErrorContext *context,
    SecondaryCommandBufferCollector *commandBufferCollector)
{
    // reset call assumes we have flushed out barriers. But this may not be the case when error
    // happened.
    mPipelineBarriers.reset();
    mEventBarriers.reset();

    (void)reset(context, commandBufferCollector);
}

void OutsideRenderPassCommandBufferHelper::imageRead(Context *context,
                                                     VkImageAspectFlags aspectFlags,
                                                     ImageAccess imageAccess,
                                                     ImageHelper *image)
{
    if (image->getResourceUse() >= mQueueSerial)
    {
        // If image is already used by renderPass, it may already set the event to renderPass's
        // event. In this case we already lost the previous event to wait for, thus use pipeline
        // barrier instead of event
        imageReadImpl(context, aspectFlags, imageAccess, BarrierType::Pipeline, image);
    }
    else
    {
        imageReadImpl(context, aspectFlags, imageAccess, BarrierType::Event, image);
        // Usually an image can only used by a RenderPassCommands or OutsideRenderPassCommands
        // because the layout will be different, except with image sampled from compute shader. In
        // this case, the renderPassCommands' read will override the outsideRenderPassCommands'
        retainImageWithEvent(context, image);
    }
}

void OutsideRenderPassCommandBufferHelper::imageWrite(Context *context,
                                                      gl::OwnerLevel level,
                                                      gl::OwnerLayer layerStart,
                                                      uint32_t layerCount,
                                                      VkImageAspectFlags aspectFlags,
                                                      ImageAccess imageAccess,
                                                      ImageHelper *image)
{
    imageWriteImpl(context, level, layerStart, layerCount, aspectFlags, imageAccess,
                   BarrierType::Event, image);
    retainImageWithEvent(context, image);
}

void OutsideRenderPassCommandBufferHelper::retainImage(Renderer *renderer, ImageHelper *image)
{
    // We want explicit control on when VkEvent is used for outsideRPCommands to minimize the
    // overhead, so do not setEvent here.
    image->setQueueSerial(mQueueSerial);
    image->updatePipelineStageAccessHistory(renderer);
}

void OutsideRenderPassCommandBufferHelper::trackImageWithEvent(Context *context, ImageHelper *image)
{
    image->setCurrentRefCountedEvent(context, &mRefCountedEvents);
    flushSetEventsImpl(context, &mCommandBuffer);
}

void OutsideRenderPassCommandBufferHelper::collectRefCountedEventsGarbage(
    RefCountedEventsGarbageRecycler *garbageRecycler)
{
    ASSERT(garbageRecycler != nullptr);
    if (!mRefCountedEventCollector.empty())
    {
        garbageRecycler->collectGarbage(mQueueSerial, std::move(mRefCountedEventCollector));
    }
}

angle::Result OutsideRenderPassCommandBufferHelper::flushToPrimary(
    Context *context,
    CommandsState *commandsState,
    PrimaryCommandBuffer *primaryCommands)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "OutsideRenderPassCommandBufferHelper::flushToPrimary");
    ASSERT(!empty());

    Renderer *renderer = context->getRenderer();

    // Commands that are added to primary before beginRenderPass command
    executeBarriers(renderer, commandsState, primaryCommands);

    ANGLE_TRY(endCommandBuffer(context));
    ASSERT(mIsCommandBufferEnded);
    mCommandBuffer.executeCommands(primaryCommands);

    // Call VkCmdSetEvent to track the completion of this renderPass.
    flushSetEventsImpl(context, primaryCommands);

    // Proactively reset all released events before ending command buffer.
    context->getRenderer()->getRefCountedEventRecycler()->resetEvents(context, mQueueSerial,
                                                                      primaryCommands);

    return angle::Result::Continue;
}

angle::Result OutsideRenderPassCommandBufferHelper::endCommandBuffer(ErrorContext *context)
{
    ASSERT(ExecutesInline() || mCommandPool != nullptr);
    ASSERT(mCommandBuffer.valid());
    ASSERT(!mIsCommandBufferEnded);

    ANGLE_TRY(mCommandBuffer.end(context));
    mIsCommandBufferEnded = true;

    return angle::Result::Continue;
}

angle::Result OutsideRenderPassCommandBufferHelper::attachCommandPool(
    ErrorContext *context,
    SecondaryCommandPool *commandPool)
{
    return attachCommandPoolImpl<OutsideRenderPassCommandBufferHelper>(context, commandPool);
}

angle::Result OutsideRenderPassCommandBufferHelper::detachCommandPool(
    ErrorContext *context,
    SecondaryCommandPool **commandPoolOut)
{
    return detachCommandPoolImpl<OutsideRenderPassCommandBufferHelper, false>(context,
                                                                              commandPoolOut);
}

void OutsideRenderPassCommandBufferHelper::releaseCommandPool()
{
    releaseCommandPoolImpl<OutsideRenderPassCommandBufferHelper>();
}

ANGLE_INLINE void OutsideRenderPassCommandBufferHelper::assertCanBeRecycled()
{
    assertCanBeRecycledImpl<OutsideRenderPassCommandBufferHelper>();
}

std::string OutsideRenderPassCommandBufferHelper::getCommandDiagnostics()
{
    std::ostringstream out;
    addCommandDiagnosticsCommon(&out);

    out << mCommandBuffer.dumpCommands("\\l");

    return out.str();
}

// RenderPassFramebuffer implementation.
void RenderPassFramebuffer::reset()
{
    mInitialFramebuffer.release();
    mImageViews.clear();
    mIsImageless = false;
    mIsDefault   = false;
}

void RenderPassFramebuffer::addResolveAttachment(size_t viewIndex, VkImageView view)
{
    // The initial framebuffer is no longer usable.
    mInitialFramebuffer.release();

    if (viewIndex >= mImageViews.size())
    {
        mImageViews.resize(viewIndex + 1, VK_NULL_HANDLE);
    }

    ASSERT(mImageViews[viewIndex] == VK_NULL_HANDLE);
    mImageViews[viewIndex] = view;
}

angle::Result RenderPassFramebuffer::packResolveViewsAndCreateFramebuffer(
    ErrorContext *context,
    const RenderPass &renderPass,
    Framebuffer *framebufferOut)
{
    // This is only called if the initial framebuffer was not usable.  Since this is called when
    // the render pass is finalized, the render pass that is passed in is the final one (not a
    // compatible one) and the framebuffer that is created is not imageless.
    ASSERT(!mInitialFramebuffer.valid());

    PackViews(&mImageViews);
    mIsImageless = false;

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.flags                   = 0;
    framebufferInfo.renderPass              = renderPass.getHandle();
    framebufferInfo.attachmentCount         = static_cast<uint32_t>(mImageViews.size());
    framebufferInfo.pAttachments            = mImageViews.data();
    framebufferInfo.width                   = mWidth;
    framebufferInfo.height                  = mHeight;
    framebufferInfo.layers                  = mLayers;

    ANGLE_VK_TRY(context, framebufferOut->init(context->getDevice(), framebufferInfo));
    return angle::Result::Continue;
}

void RenderPassFramebuffer::packResolveViewsForRenderPassBegin(
    VkRenderPassAttachmentBeginInfo *beginInfoOut)
{
    // Called when using the initial framebuffer which is imageless
    ASSERT(mInitialFramebuffer.valid());
    ASSERT(mIsImageless);

    PackViews(&mImageViews);

    *beginInfoOut                 = {};
    beginInfoOut->sType           = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO_KHR;
    beginInfoOut->attachmentCount = static_cast<uint32_t>(mImageViews.size());
    beginInfoOut->pAttachments    = mImageViews.data();
}

// static
void RenderPassFramebuffer::PackViews(FramebufferAttachmentsVector<VkImageView> *views)
{
    PackedAttachmentIndex packIndex = kAttachmentIndexZero;
    for (size_t viewIndex = 0; viewIndex < views->size(); ++viewIndex)
    {
        if ((*views)[viewIndex] != VK_NULL_HANDLE)
        {
            (*views)[packIndex.get()] = (*views)[viewIndex];
            ++packIndex;
        }
    }

    views->resize(packIndex.get());
}

// RenderPassCommandBufferHelper implementation.
RenderPassCommandBufferHelper::RenderPassCommandBufferHelper()
    : mCurrentSubpassCommandBufferIndex(0),
      mClearValues{},
      mRenderPassStarted(false),
      mTransformFeedbackCounterBuffers{},
      mTransformFeedbackCounterBufferOffsets{},
      mValidTransformFeedbackBufferCount(0),
      mRebindTransformFeedbackBuffers(false),
      mIsTransformFeedbackActiveUnpaused(false),
      mPreviousSubpassesCmdCount(0),
      mDepthStencilAttachmentIndex(kAttachmentIndexInvalid),
      mColorAttachmentsCount(0),
      mImageOptimizeForPresent(nullptr),
      mImageOptimizeForPresentOriginalLayout(ImageAccess::Undefined),
      mPipelineLayout(nullptr)
{}

RenderPassCommandBufferHelper::~RenderPassCommandBufferHelper() {}

angle::Result RenderPassCommandBufferHelper::initialize(ErrorContext *context)
{
    mGraphicsDriverUniforms = std::make_unique<GraphicsDriverUniforms>(context->getRenderer());
    initializeImpl();
    return initializeCommandBuffer(context);
}
angle::Result RenderPassCommandBufferHelper::initializeCommandBuffer(ErrorContext *context)
{
    // Skip initialization in the Pool-detached state.
    if (!ExecutesInline() && mCommandPool == nullptr)
    {
        return angle::Result::Continue;
    }
    return getCommandBuffer().initialize(context, mCommandPool, true,
                                         mCommandAllocator.getAllocator());
}

angle::Result RenderPassCommandBufferHelper::reset(
    ErrorContext *context,
    SecondaryCommandBufferCollector *commandBufferCollector)
{
    resetImpl(context);

    for (PackedAttachmentIndex index = kAttachmentIndexZero; index < mColorAttachmentsCount;
         ++index)
    {
        mColorAttachments[index].reset();
        mColorResolveAttachments[index].reset();
    }

    mDepthAttachment.reset();
    mDepthResolveAttachment.reset();
    mStencilAttachment.reset();
    mStencilResolveAttachment.reset();

    mFragmentShadingRateAtachment.reset();

    mRenderPassStarted                     = false;
    mValidTransformFeedbackBufferCount     = 0;
    mRebindTransformFeedbackBuffers        = false;
    mHasShaderStorageOutput                = false;
    mHasGLMemoryBarrierIssued              = false;
    mPreviousSubpassesCmdCount             = 0;
    mColorAttachmentsCount                 = PackedAttachmentCount(0);
    mDepthStencilAttachmentIndex           = kAttachmentIndexInvalid;
    mImageOptimizeForPresent               = nullptr;
    mImageOptimizeForPresentOriginalLayout = ImageAccess::Undefined;
    mPipelineLayout                        = nullptr;

    // Collect/Reset the command buffers
    for (uint32_t subpass = 0; subpass < getSubpassCommandBufferCount(); ++subpass)
    {
        commandBufferCollector->collectCommandBuffer(std::move(mCommandBuffers[subpass]));
    }

    mCurrentSubpassCommandBufferIndex = 0;

    // Reset the image views used for imageless framebuffer (if any)
    mFramebuffer.reset();

    // Invalidate the queue serial here. We will get a new queue serial when we begin renderpass.
    mQueueSerial = QueueSerial();

    return initializeCommandBuffer(context);
}

void RenderPassCommandBufferHelper::abandon(ErrorContext *context,
                                            SecondaryCommandBufferCollector *commandBufferCollector)
{
    // reset call assumes we have flushed out barriers. But this may not be the case when error
    // happened.
    mPipelineBarriers.reset();
    mEventBarriers.reset();

    (void)reset(context, commandBufferCollector);
}

void RenderPassCommandBufferHelper::imageRead(ContextVk *contextVk,
                                              VkImageAspectFlags aspectFlags,
                                              ImageAccess imageAccess,
                                              ImageHelper *image)
{
    imageReadImpl(contextVk, aspectFlags, imageAccess, BarrierType::Event, image);
    // As noted in the header we don't support multiple read layouts for Images.
    // We allow duplicate uses in the RP to accommodate for normal GL sampler usage.
    retainImageWithEvent(contextVk, image);
}

void RenderPassCommandBufferHelper::imageWrite(ContextVk *contextVk,
                                               gl::OwnerLevel level,
                                               gl::OwnerLayer layerStart,
                                               uint32_t layerCount,
                                               VkImageAspectFlags aspectFlags,
                                               ImageAccess imageAccess,
                                               ImageHelper *image)
{
    imageWriteImpl(contextVk, level, layerStart, layerCount, aspectFlags, imageAccess,
                   BarrierType::Event, image);
    retainImageWithEvent(contextVk, image);
}

void RenderPassCommandBufferHelper::colorImagesDraw(gl::OwnerLevel level,
                                                    gl::OwnerLayer layerStart,
                                                    uint32_t layerCount,
                                                    ImageHelper *image,
                                                    ImageHelper *resolveImage,
                                                    PackedAttachmentIndex packedAttachmentIndex)
{
    ASSERT(packedAttachmentIndex < mColorAttachmentsCount);

    image->onRenderPassAttach(mQueueSerial);

    mColorAttachments[packedAttachmentIndex].init(image, level, layerStart, layerCount,
                                                  VK_IMAGE_ASPECT_COLOR_BIT);
    image->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);

    if (resolveImage)
    {
        resolveImage->onRenderPassAttach(mQueueSerial);
        mColorResolveAttachments[packedAttachmentIndex].init(resolveImage, level, layerStart,
                                                             layerCount, VK_IMAGE_ASPECT_COLOR_BIT);
        resolveImage->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);
    }
}

void RenderPassCommandBufferHelper::depthStencilImagesDraw(gl::OwnerLevel level,
                                                           gl::OwnerLayer layerStart,
                                                           uint32_t layerCount,
                                                           ImageHelper *image,
                                                           ImageHelper *resolveImage)
{
    ASSERT(!usesImage(*image));
    ASSERT(!resolveImage || !usesImage(*resolveImage));

    // Because depthStencil buffer's read/write property can change while we build renderpass, we
    // defer the image layout changes until endRenderPass time or when images going away so that we
    // only insert layout change barrier once.
    image->onRenderPassAttach(mQueueSerial);

    mDepthAttachment.init(image, level, layerStart, layerCount, VK_IMAGE_ASPECT_DEPTH_BIT);
    mStencilAttachment.init(image, level, layerStart, layerCount, VK_IMAGE_ASPECT_STENCIL_BIT);
    image->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);

    if (resolveImage)
    {
        // Note that the resolve depth/stencil image has the same level/layer index as the
        // depth/stencil image as currently it can only ever come from
        // multisampled-render-to-texture renderbuffers.
        resolveImage->onRenderPassAttach(mQueueSerial);

        mDepthResolveAttachment.init(resolveImage, level, layerStart, layerCount,
                                     VK_IMAGE_ASPECT_DEPTH_BIT);
        mStencilResolveAttachment.init(resolveImage, level, layerStart, layerCount,
                                       VK_IMAGE_ASPECT_STENCIL_BIT);
        resolveImage->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);
    }
}

void RenderPassCommandBufferHelper::fragmentShadingRateImageRead(ImageHelper *image)
{
    ASSERT(image && image->valid());
    ASSERT(!usesImage(*image));

    image->onRenderPassAttach(mQueueSerial);

    // Initialize RenderPassAttachment for fragment shading rate attachment.
    mFragmentShadingRateAtachment.init(image, gl::OwnerLevel(0), gl::OwnerLayer(0), 1,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
    image->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);
}

void RenderPassCommandBufferHelper::onColorAccess(PackedAttachmentIndex packedAttachmentIndex,
                                                  ResourceAccess access)
{
    ASSERT(packedAttachmentIndex < mColorAttachmentsCount);
    mColorAttachments[packedAttachmentIndex].onAccess(access, getRenderPassWriteCommandCount());
}

void RenderPassCommandBufferHelper::onDepthAccess(ResourceAccess access)
{
    mDepthAttachment.onAccess(access, getRenderPassWriteCommandCount());
}

void RenderPassCommandBufferHelper::onStencilAccess(ResourceAccess access)
{
    mStencilAttachment.onAccess(access, getRenderPassWriteCommandCount());
}

void RenderPassCommandBufferHelper::updateDepthReadOnlyMode(RenderPassUsageFlags dsUsageFlags)
{
    ASSERT(mRenderPassStarted);
    updateStartedRenderPassWithDepthStencilMode(&mDepthResolveAttachment, hasDepthWriteOrClear(),
                                                dsUsageFlags,
                                                RenderPassUsage::DepthReadOnlyAttachment);
}

void RenderPassCommandBufferHelper::updateStencilReadOnlyMode(RenderPassUsageFlags dsUsageFlags)
{
    ASSERT(mRenderPassStarted);
    updateStartedRenderPassWithDepthStencilMode(&mStencilResolveAttachment,
                                                hasStencilWriteOrClear(), dsUsageFlags,
                                                RenderPassUsage::StencilReadOnlyAttachment);
}

void RenderPassCommandBufferHelper::updateDepthStencilReadOnlyMode(
    RenderPassUsageFlags dsUsageFlags,
    VkImageAspectFlags dsAspectFlags)
{
    ASSERT(mRenderPassStarted);
    if ((dsAspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
    {
        updateDepthReadOnlyMode(dsUsageFlags);
    }
    if ((dsAspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
    {
        updateStencilReadOnlyMode(dsUsageFlags);
    }
}

void RenderPassCommandBufferHelper::updateStartedRenderPassWithDepthStencilMode(
    RenderPassAttachment *resolveAttachment,
    bool renderPassHasWriteOrClear,
    RenderPassUsageFlags dsUsageFlags,
    RenderPassUsage readOnlyAttachmentUsage)
{
    ASSERT(mRenderPassStarted);
    ASSERT(mDepthAttachment.getImage() == mStencilAttachment.getImage());
    ASSERT(mDepthResolveAttachment.getImage() == mStencilResolveAttachment.getImage());

    // Determine read-only mode for depth or stencil
    const bool readOnlyMode =
        mDepthStencilAttachmentIndex != kAttachmentIndexInvalid &&
        resolveAttachment->getImage() == nullptr &&
        (dsUsageFlags.test(readOnlyAttachmentUsage) || !renderPassHasWriteOrClear);

    // If readOnlyMode is false, we are switching out of read only mode due to depth/stencil write.
    // We must not be in the read only feedback loop mode because the logic in
    // DIRTY_BIT_READ_ONLY_DEPTH_FEEDBACK_LOOP_MODE should ensure we end the previous renderpass and
    // a new renderpass will start with feedback loop disabled.
    ASSERT(readOnlyMode || !dsUsageFlags.test(readOnlyAttachmentUsage));

    ImageHelper *depthStencilImage = mDepthAttachment.getImage();
    if (depthStencilImage)
    {
        RenderPassUsageFlags &renderPassUsageFlags =
            depthStencilImage->getRenderPassUsage().flags(this);
        renderPassUsageFlags.set(readOnlyAttachmentUsage, readOnlyMode);
    }
    // The depth/stencil resolve image is never in read-only mode
}

void RenderPassCommandBufferHelper::finalizeColorImageLayout(
    Context *context,
    ImageHelper *image,
    PackedAttachmentIndex packedAttachmentIndex,
    bool isResolveImage)
{
    ASSERT(packedAttachmentIndex < mColorAttachmentsCount);
    ASSERT(image != nullptr);

    // Do layout change.
    ImageAccess imageAccess;
    const RenderPassUsageFlags renderPassUsageFlags = image->getRenderPassUsage().getFlags(this);
    if (renderPassUsageFlags[RenderPassUsage::RenderTargetAttachment] &&
        renderPassUsageFlags[RenderPassUsage::ColorTextureSampler])
    {
        // Used by current renderPass as both attachment and sampler. Texture code should already
        // picked layout and inserted barrier
        imageAccess = image->getCurrentImageAccess();
        ASSERT(imageAccess == ImageAccess::ColorWriteFragmentShaderFeedback ||
               imageAccess == ImageAccess::ColorWriteAllShadersFeedback);
    }
    else
    {
        // When color is unresolved, use a layout that includes fragment shader reads.  This is done
        // for all color resolve attachments even if they are not all unresolved for simplicity.  In
        // particular, the GL color index is not available (only the packed index) at this point,
        // but that is needed to query whether the attachment is unresolved or not.
        const bool hasUnresolve =
            isResolveImage && mRenderPassDesc.getColorUnresolveAttachmentMask().any();
        imageAccess = hasUnresolve ? ImageAccess::MSRTTEmulationColorUnresolveAndResolve
                                   : ImageAccess::ColorWrite;
        if (context->getFeatures().preferDynamicRendering.enabled &&
            mRenderPassDesc.hasColorFramebufferFetch())
        {
            // Note MSRTT emulation is not implemented with dynamic rendering.
            ASSERT(imageAccess == ImageAccess::ColorWrite);
            imageAccess = ImageAccess::ColorWriteAndInput;
        }
        else if (image->getCurrentImageAccess() == ImageAccess::SharedPresent)
        {
            // Once you transition to ImageAccess::SharedPresent, you never transition out of it.
            ASSERT(imageAccess == ImageAccess::ColorWrite);
            imageAccess = ImageAccess::SharedPresent;
        }

        updateImageLayoutAndBarrier(context, image, VK_IMAGE_ASPECT_COLOR_BIT, imageAccess,
                                    BarrierType::Event);
    }

    if (!isResolveImage)
    {
        mAttachmentOps.setLayouts(packedAttachmentIndex, imageAccess, imageAccess);
    }
    else
    {
        SetBitField(mAttachmentOps[packedAttachmentIndex].finalResolveLayout, imageAccess);
    }

    // Dynamic rendering does not have implicit layout transitions at render pass boundaries.  This
    // optimization is instead done by recording the necessary transition after the render pass
    // directly on the primary command buffer.
    if (mImageOptimizeForPresent == image)
    {
        ASSERT(isDefault());
        ASSERT(context->getFeatures().supportsPresentation.enabled);
        ASSERT(packedAttachmentIndex == kAttachmentIndexZero);
        // Shared present mode must not change layout
        ASSERT(imageAccess != ImageAccess::SharedPresent);

        // Use finalLayout instead of extra barrier for layout change to present.  For dynamic
        // rendering, this is not possible and is done when the render pass is flushed.  However,
        // because this function is expected to finalize the image layout, we still have to pretend
        // the image is in the present layout already.
        mImageOptimizeForPresentOriginalLayout = mImageOptimizeForPresent->getCurrentImageAccess();
        mImageOptimizeForPresent->setCurrentImageAccess(context->getRenderer(),
                                                        ImageAccess::Present);

        if (!context->getFeatures().preferDynamicRendering.enabled)
        {
            if (isResolveImage)
            {
                SetBitField(mAttachmentOps[packedAttachmentIndex].finalResolveLayout,
                            mImageOptimizeForPresent->getCurrentImageAccess());
            }
            else
            {
                SetBitField(mAttachmentOps[packedAttachmentIndex].finalLayout,
                            mImageOptimizeForPresent->getCurrentImageAccess());
            }
            mImageOptimizeForPresent               = nullptr;
            mImageOptimizeForPresentOriginalLayout = ImageAccess::Undefined;
        }
    }

    if (isResolveImage)
    {
        // Note: the color image will have its flags reset after load/store ops are determined.
        image->getRenderPassUsage().reset(this);
    }
}

void RenderPassCommandBufferHelper::finalizeColorImageLoadStore(
    Context *context,
    PackedAttachmentIndex packedAttachmentIndex)
{
    PackedAttachmentOpsDesc &ops = mAttachmentOps[packedAttachmentIndex];
    RenderPassLoadOp loadOp      = static_cast<RenderPassLoadOp>(ops.loadOp);
    RenderPassStoreOp storeOp    = static_cast<RenderPassStoreOp>(ops.storeOp);

    // This has to be called after layout been finalized
    ASSERT(ops.initialLayout != static_cast<uint16_t>(ImageAccess::Undefined));

    uint32_t currentCmdCount = getRenderPassWriteCommandCount();
    bool isInvalidated       = false;

    RenderPassAttachment &colorAttachment = mColorAttachments[packedAttachmentIndex];
    colorAttachment.finalizeLoadStore(
        context, currentCmdCount, mRenderPassDesc.getColorUnresolveAttachmentMask().any(),
        mRenderPassDesc.getColorResolveAttachmentMask().any(), &loadOp, &storeOp, &isInvalidated);

    if (isInvalidated)
    {
        ops.isInvalidated = true;
    }

    if (!ops.isInvalidated)
    {
        mColorResolveAttachments[packedAttachmentIndex].restoreContent();
    }

    // If the image is being written to, mark its contents defined.
    // This has to be done after storeOp has been finalized.
    if (storeOp == RenderPassStoreOp::Store)
    {
        colorAttachment.restoreContent();
    }

    SetBitField(ops.loadOp, loadOp);
    SetBitField(ops.storeOp, storeOp);
}

void RenderPassCommandBufferHelper::finalizeDepthStencilImageLayout(Context *context)
{
    ASSERT(mDepthAttachment.getImage() != nullptr);
    ASSERT(mDepthAttachment.getImage() == mStencilAttachment.getImage());

    ImageHelper *depthStencilImage = mDepthAttachment.getImage();

    // Do depth stencil layout change.
    ImageAccess imageAccess;
    bool barrierRequired;
    const RenderPassUsageFlags renderPassUsageFlags =
        depthStencilImage->getRenderPassUsage().getFlags(this);

    const bool isDepthAttachmentAndSampler =
        renderPassUsageFlags[RenderPassUsage::RenderTargetAttachment] &&
        renderPassUsageFlags[RenderPassUsage::DepthTextureSampler];
    const bool isStencilAttachmentAndSampler =
        renderPassUsageFlags[RenderPassUsage::RenderTargetAttachment] &&
        renderPassUsageFlags[RenderPassUsage::StencilTextureSampler];
    const bool isReadOnlyDepth   = renderPassUsageFlags[RenderPassUsage::DepthReadOnlyAttachment];
    const bool isReadOnlyStencil = renderPassUsageFlags[RenderPassUsage::StencilReadOnlyAttachment];
    BarrierType barrierType      = BarrierType::Event;

    if (isDepthAttachmentAndSampler || isStencilAttachmentAndSampler)
    {
        // texture code already picked layout and inserted barrier
        imageAccess = depthStencilImage->getCurrentImageAccess();

        if ((isDepthAttachmentAndSampler && !isReadOnlyDepth) ||
            (isStencilAttachmentAndSampler && !isReadOnlyStencil))
        {
            ASSERT(imageAccess == ImageAccess::DepthStencilFragmentShaderFeedback ||
                   imageAccess == ImageAccess::DepthStencilAllShadersFeedback);
            barrierRequired = true;
        }
        else
        {
            ASSERT(imageAccess == ImageAccess::DepthWriteStencilReadFragmentShaderStencilRead ||
                   imageAccess == ImageAccess::DepthWriteStencilReadAllShadersStencilRead ||
                   imageAccess == ImageAccess::DepthReadStencilWriteFragmentShaderDepthRead ||
                   imageAccess == ImageAccess::DepthReadStencilWriteAllShadersDepthRead ||
                   imageAccess == ImageAccess::DepthReadStencilReadFragmentShaderRead ||
                   imageAccess == ImageAccess::DepthReadStencilReadAllShadersRead);
            barrierRequired =
                depthStencilImage->isReadBarrierNecessary(context->getRenderer(), imageAccess);
        }
    }
    else
    {
        if (mRenderPassDesc.hasDepthStencilFramebufferFetch())
        {
            imageAccess = ImageAccess::DepthStencilWriteAndInput;
        }
        else if (isReadOnlyDepth)
        {
            imageAccess = isReadOnlyStencil ? ImageAccess::DepthReadStencilRead
                                            : ImageAccess::DepthReadStencilWrite;
        }
        else
        {
            imageAccess = isReadOnlyStencil ? ImageAccess::DepthWriteStencilRead
                                            : ImageAccess::DepthWriteStencilWrite;
        }

        barrierRequired =
            !isReadOnlyDepth || !isReadOnlyStencil ||
            depthStencilImage->isReadBarrierNecessary(context->getRenderer(), imageAccess);
    }

    mAttachmentOps.setLayouts(mDepthStencilAttachmentIndex, imageAccess, imageAccess);

    if (barrierRequired)
    {
        const angle::Format &format = depthStencilImage->getActualFormat();
        ASSERT(format.hasDepthOrStencilBits());
        VkImageAspectFlags aspectFlags = GetDepthStencilAspectFlags(format);
        updateImageLayoutAndBarrier(context, depthStencilImage, aspectFlags, imageAccess,
                                    barrierType);
    }
}

void RenderPassCommandBufferHelper::finalizeDepthStencilResolveImageLayout(Context *context)
{
    ASSERT(mDepthResolveAttachment.getImage() != nullptr);
    ASSERT(mDepthResolveAttachment.getImage() == mStencilResolveAttachment.getImage());

    ImageHelper *depthStencilResolveImage = mDepthResolveAttachment.getImage();

    // When depth/stencil is unresolved, use a layout that includes fragment shader reads.
    ImageAccess imageAccess     = mRenderPassDesc.hasDepthStencilUnresolveAttachment()
                                      ? ImageAccess::MSRTTEmulationDepthStencilUnresolveAndResolve
                                      : ImageAccess::DepthStencilResolve;
    const angle::Format &format = depthStencilResolveImage->getActualFormat();
    ASSERT(format.hasDepthOrStencilBits());
    VkImageAspectFlags aspectFlags = GetDepthStencilAspectFlags(format);

    updateImageLayoutAndBarrier(context, depthStencilResolveImage, aspectFlags, imageAccess,
                                BarrierType::Event);

    // The resolve image can never be read-only.
    ASSERT(!depthStencilResolveImage->getRenderPassUsage().getFlags(
        this)[RenderPassUsage::DepthReadOnlyAttachment]);
    ASSERT(!depthStencilResolveImage->getRenderPassUsage().getFlags(
        this)[RenderPassUsage::StencilReadOnlyAttachment]);
    ASSERT(mDepthStencilAttachmentIndex != kAttachmentIndexInvalid);
    const PackedAttachmentOpsDesc &dsOps = mAttachmentOps[mDepthStencilAttachmentIndex];

    // If the image is being written to, mark its contents defined.
    if (!dsOps.isInvalidated && mRenderPassDesc.hasDepthResolveAttachment())
    {
        mDepthResolveAttachment.restoreContent();
    }
    if (!dsOps.isStencilInvalidated && mRenderPassDesc.hasStencilResolveAttachment())
    {
        mStencilResolveAttachment.restoreContent();
    }

    depthStencilResolveImage->getRenderPassUsage().reset(this);
}

void RenderPassCommandBufferHelper::finalizeFragmentShadingRateImageLayout(Context *context)
{
    ImageHelper *image      = mFragmentShadingRateAtachment.getImage();
    ImageAccess imageAccess = ImageAccess::FragmentShadingRateAttachmentReadOnly;
    ASSERT(image != nullptr);
    ASSERT(image->valid());
    if (image->isReadBarrierNecessary(context->getRenderer(), imageAccess))
    {
        updateImageLayoutAndBarrier(context, image, VK_IMAGE_ASPECT_COLOR_BIT, imageAccess,
                                    BarrierType::Event);
    }
    image->getRenderPassUsage().reset(this);
}

bool RenderPassCommandBufferHelper::finalizeImageLayout(Context *context, const ImageHelper *image)
{
    const RenderPassUsageFlags renderPassUsageFlags = image->getRenderPassUsage().getFlags(this);
    if (!renderPassUsageFlags[RenderPassUsage::RenderTargetAttachment])
    {
        return false;
    }

    for (PackedAttachmentIndex index = kAttachmentIndexZero; index < mColorAttachmentsCount;
         ++index)
    {
        if (mColorAttachments[index].hasImage(image))
        {
            finalizeColorImageLayoutAndLoadStore(context, index);
            mColorAttachments[index].reset();
        }
        else if (mColorResolveAttachments[index].hasImage(image))
        {
            finalizeColorImageLayout(context, mColorResolveAttachments[index].getImage(), index,
                                     true);
            mColorResolveAttachments[index].reset();
        }
    }

    if (mDepthAttachment.hasImage(image))
    {
        finalizeDepthStencilImageLayoutAndLoadStore(context);
        mDepthAttachment.reset();
        mStencilAttachment.reset();
    }

    if (mDepthResolveAttachment.hasImage(image))
    {
        finalizeDepthStencilResolveImageLayout(context);
        mDepthResolveAttachment.reset();
        mStencilResolveAttachment.reset();
    }

    if (mFragmentShadingRateAtachment.hasImage(image))
    {
        finalizeFragmentShadingRateImageLayout(context);
        mFragmentShadingRateAtachment.reset();
    }

    ASSERT(!image->getRenderPassUsage().getFlags(this)[RenderPassUsage::RenderTargetAttachment]);
    return true;
}

void RenderPassCommandBufferHelper::finalizeDepthStencilLoadStore(Context *context)
{
    ASSERT(mDepthStencilAttachmentIndex != kAttachmentIndexInvalid);

    PackedAttachmentOpsDesc &dsOps   = mAttachmentOps[mDepthStencilAttachmentIndex];
    RenderPassLoadOp depthLoadOp     = static_cast<RenderPassLoadOp>(dsOps.loadOp);
    RenderPassStoreOp depthStoreOp   = static_cast<RenderPassStoreOp>(dsOps.storeOp);
    RenderPassLoadOp stencilLoadOp   = static_cast<RenderPassLoadOp>(dsOps.stencilLoadOp);
    RenderPassStoreOp stencilStoreOp = static_cast<RenderPassStoreOp>(dsOps.stencilStoreOp);
    const RenderPassUsageFlags renderPassUsageFlags =
        mDepthAttachment.getImage()->getRenderPassUsage().getFlags(this);

    // This has to be called after layout been finalized
    ASSERT(dsOps.initialLayout != static_cast<uint16_t>(ImageAccess::Undefined));
    // Ensure we don't write to a read-only attachment. (ReadOnly -> !Write)
    ASSERT(!renderPassUsageFlags[RenderPassUsage::DepthReadOnlyAttachment] ||
           !mDepthAttachment.hasWriteAccess());
    ASSERT(!renderPassUsageFlags[RenderPassUsage::StencilReadOnlyAttachment] ||
           !mStencilAttachment.hasWriteAccess());

    uint32_t currentCmdCount         = getRenderPassWriteCommandCount();
    bool isDepthInvalidated          = false;
    bool isStencilInvalidated        = false;
    bool hasDepthResolveAttachment   = mRenderPassDesc.hasDepthResolveAttachment();
    bool hasStencilResolveAttachment = mRenderPassDesc.hasStencilResolveAttachment();

    mDepthAttachment.finalizeLoadStore(
        context, currentCmdCount, mRenderPassDesc.hasDepthUnresolveAttachment(),
        hasDepthResolveAttachment, &depthLoadOp, &depthStoreOp, &isDepthInvalidated);
    mStencilAttachment.finalizeLoadStore(
        context, currentCmdCount, mRenderPassDesc.hasStencilUnresolveAttachment(),
        hasStencilResolveAttachment, &stencilLoadOp, &stencilStoreOp, &isStencilInvalidated);

    const bool disableMixedDepthStencilLoadOpNoneAndLoad =
        context->getFeatures().disallowMixedDepthStencilLoadOpNoneAndLoad.enabled;

    if (disableMixedDepthStencilLoadOpNoneAndLoad)
    {
        if (depthLoadOp == RenderPassLoadOp::None && stencilLoadOp != RenderPassLoadOp::None)
        {
            depthLoadOp = RenderPassLoadOp::Load;
        }
        if (depthLoadOp != RenderPassLoadOp::None && stencilLoadOp == RenderPassLoadOp::None)
        {
            stencilLoadOp = RenderPassLoadOp::Load;
        }
    }

    if (isDepthInvalidated)
    {
        dsOps.isInvalidated = true;
    }
    if (isStencilInvalidated)
    {
        dsOps.isStencilInvalidated = true;
    }

    // If any aspect is missing, set the corresponding ops to don't care.
    const uint32_t depthStencilIndexGL =
        static_cast<uint32_t>(mRenderPassDesc.depthStencilAttachmentIndex());
    const angle::FormatID attachmentFormatID = mRenderPassDesc[depthStencilIndexGL];
    ASSERT(attachmentFormatID != angle::FormatID::NONE);
    const angle::Format &angleFormat = angle::Format::Get(attachmentFormatID);

    if (angleFormat.depthBits == 0)
    {
        depthLoadOp  = RenderPassLoadOp::DontCare;
        depthStoreOp = RenderPassStoreOp::DontCare;
    }
    if (angleFormat.stencilBits == 0)
    {
        stencilLoadOp  = RenderPassLoadOp::DontCare;
        stencilStoreOp = RenderPassStoreOp::DontCare;
    }

    // If the image is being written to, mark its contents defined.
    // This has to be done after storeOp has been finalized.
    ASSERT(mDepthAttachment.getImage() == mStencilAttachment.getImage());
    if (!renderPassUsageFlags[RenderPassUsage::DepthReadOnlyAttachment])
    {
        if (depthStoreOp == RenderPassStoreOp::Store)
        {
            mDepthAttachment.restoreContent();
        }
    }
    if (!renderPassUsageFlags[RenderPassUsage::StencilReadOnlyAttachment])
    {
        if (stencilStoreOp == RenderPassStoreOp::Store)
        {
            mStencilAttachment.restoreContent();
        }
    }

    SetBitField(dsOps.loadOp, depthLoadOp);
    SetBitField(dsOps.storeOp, depthStoreOp);
    SetBitField(dsOps.stencilLoadOp, stencilLoadOp);
    SetBitField(dsOps.stencilStoreOp, stencilStoreOp);
}

void RenderPassCommandBufferHelper::finalizeColorImageLayoutAndLoadStore(
    Context *context,
    PackedAttachmentIndex packedAttachmentIndex)
{
    finalizeColorImageLayout(context, mColorAttachments[packedAttachmentIndex].getImage(),
                             packedAttachmentIndex, false);
    finalizeColorImageLoadStore(context, packedAttachmentIndex);

    mColorAttachments[packedAttachmentIndex].getImage()->getRenderPassUsage().reset(this);
}

void RenderPassCommandBufferHelper::finalizeDepthStencilImageLayoutAndLoadStore(Context *context)
{
    finalizeDepthStencilImageLayout(context);
    finalizeDepthStencilLoadStore(context);

    ASSERT(mDepthAttachment.getImage() == mStencilAttachment.getImage());
    mDepthAttachment.getImage()->getRenderPassUsage().reset(this);
}

void RenderPassCommandBufferHelper::collectRefCountedEventsGarbage(
    Renderer *renderer,
    RefCountedEventsGarbageRecycler *garbageRecycler)
{
    // For render pass the VkCmdSetEvent works differently from OutsideRenderPassCommands.
    // VkCmdEndRenderPass are called in the primary command buffer, and VkCmdSetEvents has to be
    // issued after VkCmdEndRenderPass. This means VkCmdSetEvent has to be delayed. Because of this,
    // here we simply make a copy of the VkEvent from RefCountedEvent and then add the
    // RefCountedEvent to the garbage collector. No VkCmdSetEvent call is issued here (they will be
    // issued at flushToPrimary time).
    mVkEventArray.init(renderer, mRefCountedEvents);
    mRefCountedEvents.releaseToEventCollector(&mRefCountedEventCollector);

    if (!mRefCountedEventCollector.empty())
    {
        garbageRecycler->collectGarbage(mQueueSerial, std::move(mRefCountedEventCollector));
    }
}

void RenderPassCommandBufferHelper::updatePerfCountersForDynamicRenderingInstance(
    ErrorContext *context,
    angle::VulkanPerfCounters *countersOut)
{
    mRenderPassDesc.updatePerfCounters(context, mFramebuffer.getUnpackedImageViews(),
                                       mAttachmentOps, countersOut);
}

void RenderPassCommandBufferHelper::addCurrentDriverUniforms(
    const vk::PipelineLayout *pipelineLayout,
    const GraphicsDriverUniforms &graphicsDriverUniforms)
{
    mPipelineLayout = pipelineLayout;
    if (pipelineLayout != nullptr)
    {
        mGraphicsDriverUniforms->copyGraphicsDriverUniformsData(graphicsDriverUniforms);
    }
}

void RenderPassCommandBufferHelper::dirtyCurrentDriverUniforms()
{
    mGraphicsDriverUniforms->setAllDirtyBits();
}

angle::Result RenderPassCommandBufferHelper::beginRenderPass(
    ContextVk *contextVk,
    RenderPassFramebuffer &&framebuffer,
    const gl::Rectangle &renderArea,
    const RenderPassDesc &renderPassDesc,
    const AttachmentOpsArray &renderPassAttachmentOps,
    const PackedAttachmentCount colorAttachmentCount,
    const PackedAttachmentIndex depthStencilAttachmentIndex,
    const PackedClearValuesArray &clearValues,
    const QueueSerial &queueSerial,
    RenderPassCommandBuffer **commandBufferOut)
{
    ASSERT(!mRenderPassStarted);

    mRenderPassDesc              = renderPassDesc;
    mAttachmentOps               = renderPassAttachmentOps;
    mDepthStencilAttachmentIndex = depthStencilAttachmentIndex;
    mColorAttachmentsCount       = colorAttachmentCount;
    mFramebuffer                 = std::move(framebuffer);
    mRenderArea                  = renderArea;
    mClearValues                 = clearValues;
    mQueueSerial                 = queueSerial;
    *commandBufferOut            = &getCommandBuffer();

    mRenderPassStarted = true;

    for (PackedAttachmentIndex index(0); index < colorAttachmentCount; ++index)
    {
        RenderPassLoadOp loadOp =
            static_cast<RenderPassLoadOp>(renderPassAttachmentOps[index].loadOp);
        if (loadOp == RenderPassLoadOp::Clear)
        {
            mColorAttachments[index].setCleared(0, clearValues[index]);
        }
    }

    if (depthStencilAttachmentIndex != kAttachmentIndexInvalid)
    {
        RenderPassLoadOp depthLoadOp = static_cast<RenderPassLoadOp>(
            renderPassAttachmentOps[depthStencilAttachmentIndex].loadOp);
        if (depthLoadOp == RenderPassLoadOp::Clear)
        {
            mDepthAttachment.setCleared(0, clearValues[depthStencilAttachmentIndex]);
        }

        RenderPassLoadOp stencilLoadOp = static_cast<RenderPassLoadOp>(
            renderPassAttachmentOps[depthStencilAttachmentIndex].stencilLoadOp);
        if (stencilLoadOp == RenderPassLoadOp::Clear)
        {
            mStencilAttachment.setCleared(0, clearValues[depthStencilAttachmentIndex]);
        }
    }

    return beginRenderPassCommandBuffer(contextVk);
}

angle::Result RenderPassCommandBufferHelper::beginRenderPassCommandBuffer(ContextVk *contextVk)
{
    VkCommandBufferInheritanceInfo inheritanceInfo;
    VkCommandBufferInheritanceRenderingInfo renderingInfo;
    gl::DrawBuffersArray<VkFormat> colorFormatStorage;

    ANGLE_TRY(RenderPassCommandBuffer::InitializeRenderPassInheritanceInfo(
        contextVk, mFramebuffer.getFramebuffer(), mRenderPassDesc, &inheritanceInfo, &renderingInfo,
        &colorFormatStorage));
    inheritanceInfo.subpass = mCurrentSubpassCommandBufferIndex;

    return getCommandBuffer().begin(contextVk, inheritanceInfo);
}

angle::Result RenderPassCommandBufferHelper::endRenderPass(ContextVk *contextVk)
{
    ANGLE_TRY(endRenderPassCommandBuffer(contextVk));

    for (PackedAttachmentIndex index = kAttachmentIndexZero; index < mColorAttachmentsCount;
         ++index)
    {
        if (mColorAttachments[index].getImage() != nullptr)
        {
            finalizeColorImageLayoutAndLoadStore(contextVk, index);
        }
        if (mColorResolveAttachments[index].getImage() != nullptr)
        {
            finalizeColorImageLayout(contextVk, mColorResolveAttachments[index].getImage(), index,
                                     true);
        }
    }

    if (mFragmentShadingRateAtachment.getImage() != nullptr)
    {
        finalizeFragmentShadingRateImageLayout(contextVk);
    }

    if (mDepthStencilAttachmentIndex != kAttachmentIndexInvalid)
    {
        // Do depth stencil layout change and load store optimization.
        ASSERT(mDepthAttachment.getImage() == mStencilAttachment.getImage());
        ASSERT(mDepthResolveAttachment.getImage() == mStencilResolveAttachment.getImage());
        if (mDepthAttachment.getImage() != nullptr)
        {
            finalizeDepthStencilImageLayoutAndLoadStore(contextVk);
        }
        if (mDepthResolveAttachment.getImage() != nullptr)
        {
            finalizeDepthStencilResolveImageLayout(contextVk);
        }
    }

    return angle::Result::Continue;
}

angle::Result RenderPassCommandBufferHelper::endRenderPassCommandBuffer(ContextVk *contextVk)
{
    return getCommandBuffer().end(contextVk);
}

angle::Result RenderPassCommandBufferHelper::nextSubpass(ContextVk *contextVk,
                                                         RenderPassCommandBuffer **commandBufferOut)
{
    ASSERT(!contextVk->getFeatures().preferDynamicRendering.enabled);

    if (ExecutesInline())
    {
        // When using ANGLE secondary command buffers, the commands are inline and are executed on
        // the primary command buffer.  This means that vkCmdNextSubpass can be intermixed with the
        // rest of the commands, and there is no need to split command buffers.
        //
        // Note also that the command buffer handle doesn't change in this case.
        getCommandBuffer().nextSubpass(VK_SUBPASS_CONTENTS_INLINE);
        return angle::Result::Continue;
    }

    // When using Vulkan secondary command buffers, each subpass's contents must be recorded in a
    // separate command buffer that is vkCmdExecuteCommands'ed in the primary command buffer.
    // vkCmdNextSubpass calls must also be issued in the primary command buffer.
    //
    // To support this, a list of command buffers are kept, one for each subpass.  When moving to
    // the next subpass, the previous command buffer is ended and a new one is initialized and
    // begun.

    // Accumulate command count for tracking purposes.
    mPreviousSubpassesCmdCount = getRenderPassWriteCommandCount();

    ANGLE_TRY(endRenderPassCommandBuffer(contextVk));
    markClosed();

    ++mCurrentSubpassCommandBufferIndex;
    ASSERT(getSubpassCommandBufferCount() <= kMaxSubpassCount);

    ANGLE_TRY(initializeCommandBuffer(contextVk));
    ANGLE_TRY(beginRenderPassCommandBuffer(contextVk));
    markOpen();

    // Return the new command buffer handle
    *commandBufferOut = &getCommandBuffer();
    return angle::Result::Continue;
}

void RenderPassCommandBufferHelper::beginTransformFeedback(size_t validBufferCount,
                                                           const VkBuffer *counterBuffers,
                                                           const VkDeviceSize *counterBufferOffsets,
                                                           bool rebindBuffers)
{
    mValidTransformFeedbackBufferCount = static_cast<uint32_t>(validBufferCount);
    mRebindTransformFeedbackBuffers    = rebindBuffers;

    for (size_t index = 0; index < validBufferCount; index++)
    {
        mTransformFeedbackCounterBuffers[index] = ANGLE_UNSAFE_TODO(counterBuffers[index]);
        mTransformFeedbackCounterBufferOffsets[index] =
            ANGLE_UNSAFE_TODO(counterBufferOffsets[index]);
    }
}

void RenderPassCommandBufferHelper::endTransformFeedback()
{
    pauseTransformFeedback();
    mValidTransformFeedbackBufferCount = 0;
}

void RenderPassCommandBufferHelper::invalidateRenderPassColorAttachment(
    const gl::State &state,
    size_t colorIndexGL,
    PackedAttachmentIndex attachmentIndex,
    const gl::Rectangle &invalidateArea)
{
    // Color write is enabled if:
    //
    // - Draw buffer is enabled (this is implicit, as invalidate only affects enabled draw buffers)
    // - Color output is not entirely masked
    // - Rasterizer-discard is not enabled
    const gl::BlendStateExt &blendStateExt = state.getBlendStateExt();
    const bool isColorWriteEnabled =
        blendStateExt.getColorMaskIndexed(colorIndexGL) != 0 && !state.isRasterizerDiscardEnabled();
    mColorAttachments[attachmentIndex].invalidate(invalidateArea, isColorWriteEnabled,
                                                  getRenderPassWriteCommandCount());
}

void RenderPassCommandBufferHelper::invalidateRenderPassDepthAttachment(
    const gl::DepthStencilState &dsState,
    const gl::Rectangle &invalidateArea)
{
    const bool isDepthWriteEnabled = dsState.depthTest && dsState.depthMask;
    mDepthAttachment.invalidate(invalidateArea, isDepthWriteEnabled,
                                getRenderPassWriteCommandCount());
}

void RenderPassCommandBufferHelper::invalidateRenderPassStencilAttachment(
    const gl::DepthStencilState &dsState,
    GLuint framebufferStencilSize,
    const gl::Rectangle &invalidateArea)
{
    const bool isStencilWriteEnabled =
        dsState.stencilTest && (!dsState.isStencilNoOp(framebufferStencilSize) ||
                                !dsState.isStencilBackNoOp(framebufferStencilSize));
    mStencilAttachment.invalidate(invalidateArea, isStencilWriteEnabled,
                                  getRenderPassWriteCommandCount());
}

angle::Result RenderPassCommandBufferHelper::flushToPrimary(Context *context,
                                                            CommandsState *commandsState,
                                                            PrimaryCommandBuffer *primaryCommands,
                                                            const RenderPass &renderPass,
                                                            VkFramebuffer framebufferOverride)
{
    Renderer *renderer = context->getRenderer();
    // |framebufferOverride| must only be provided if the initial framebuffer the render pass was
    // started with is not usable (due to the addition of resolve attachments after the fact).
    ASSERT(framebufferOverride == VK_NULL_HANDLE ||
           mFramebuffer.needsNewFramebufferWithResolveAttachments());
    // When a new framebuffer had to be created because of addition of resolve attachments, it's
    // never imageless.
    ASSERT(!(framebufferOverride != VK_NULL_HANDLE && mFramebuffer.isImageless()));

    ANGLE_TRACE_EVENT0("gpu.angle", "RenderPassCommandBufferHelper::flushToPrimary");
    ASSERT(mRenderPassStarted);

    // Commands that are added to primary before beginRenderPass command
    executeBarriers(renderer, commandsState, primaryCommands);

    constexpr VkSubpassContents kSubpassContents =
        ExecutesInline() ? VK_SUBPASS_CONTENTS_INLINE
                         : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;

    if (!renderPass.valid())
    {
        mRenderPassDesc.beginRendering(context, primaryCommands, mRenderArea, kSubpassContents,
                                       mFramebuffer.getUnpackedImageViews(), mAttachmentOps,
                                       mClearValues, mFramebuffer.getLayers());
    }
    else
    {
        // With imageless framebuffers, the attachments should be also added to beginInfo.
        VkRenderPassAttachmentBeginInfo attachmentBeginInfo = {};
        if (mFramebuffer.isImageless())
        {
            mFramebuffer.packResolveViewsForRenderPassBegin(&attachmentBeginInfo);

            // If nullColorAttachmentWithExternalFormatResolve is true, there will be no color
            // attachment even though mRenderPassDesc indicates so.
            ASSERT((mRenderPassDesc.hasYUVResolveAttachment() &&
                    renderer->nullColorAttachmentWithExternalFormatResolve()) ||
                   attachmentBeginInfo.attachmentCount == mRenderPassDesc.attachmentCount());
        }

        mRenderPassDesc.beginRenderPass(
            context, primaryCommands, renderPass,
            framebufferOverride ? framebufferOverride : mFramebuffer.getFramebuffer().getHandle(),
            mRenderArea, kSubpassContents, mClearValues,
            mFramebuffer.isImageless() ? &attachmentBeginInfo : nullptr);
    }

    if (mPipelineLayout != nullptr)
    {
        // This will issue pushConstants only if it is dirty, which by default it is not.
        mGraphicsDriverUniforms->pushConstants(renderer, *mPipelineLayout, primaryCommands);
        mPipelineLayout = nullptr;
    }

    // Run commands inside the RenderPass.
    for (uint32_t subpass = 0; subpass < getSubpassCommandBufferCount(); ++subpass)
    {
        if (subpass > 0)
        {
            ASSERT(!context->getFeatures().preferDynamicRendering.enabled);
            primaryCommands->nextSubpass(kSubpassContents);
        }
        mCommandBuffers[subpass].executeCommands(primaryCommands);
    }

    if (!renderPass.valid())
    {
        primaryCommands->endRendering();

        if (mImageOptimizeForPresent != nullptr)
        {
            // finalizeColorImageLayout forces layout to Present.  If this is not the case, that
            // code was not run (so mImageOptimizeForPresentOriginalLayout is invalid).
            ASSERT(mImageOptimizeForPresent->getCurrentImageAccess() == ImageAccess::Present);

            // Restore the original layout of the image and do the real transition after the render
            // pass ends.
            mImageOptimizeForPresent->setCurrentImageAccess(renderer,
                                                            mImageOptimizeForPresentOriginalLayout);
            mImageOptimizeForPresent->recordWriteBarrierOneOff(renderer, ImageAccess::Present,
                                                               primaryCommands, nullptr);
            mImageOptimizeForPresent               = nullptr;
            mImageOptimizeForPresentOriginalLayout = ImageAccess::Undefined;
        }
    }
    else
    {
        primaryCommands->endRenderPass();
    }

    // Now issue VkCmdSetEvents to primary command buffer
    ASSERT(mRefCountedEvents.empty());
    mVkEventArray.flushSetEvents(primaryCommands);

    return angle::Result::Continue;
}

void RenderPassCommandBufferHelper::addColorResolveAttachment(size_t colorIndexGL,
                                                              ImageHelper *image,
                                                              VkImageView view,
                                                              gl::OwnerLevel level,
                                                              gl::OwnerLayer layerStart,
                                                              uint32_t layerCount)
{
    mFramebuffer.addColorResolveAttachment(colorIndexGL, view);
    mRenderPassDesc.packColorResolveAttachment(colorIndexGL);

    PackedAttachmentIndex packedAttachmentIndex =
        mRenderPassDesc.getPackedColorAttachmentIndex(colorIndexGL);
    ASSERT(mColorResolveAttachments[packedAttachmentIndex].getImage() == nullptr);

    image->onRenderPassAttach(mQueueSerial);
    mColorResolveAttachments[packedAttachmentIndex].init(image, level, layerStart, layerCount,
                                                         VK_IMAGE_ASPECT_COLOR_BIT);
    image->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);
}

void RenderPassCommandBufferHelper::addDepthStencilResolveAttachment(ImageHelper *image,
                                                                     VkImageView view,
                                                                     VkImageAspectFlags aspects,
                                                                     gl::OwnerLevel level,
                                                                     gl::OwnerLayer layerStart,
                                                                     uint32_t layerCount)
{
    mFramebuffer.addDepthStencilResolveAttachment(view);
    if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
    {
        mRenderPassDesc.packDepthResolveAttachment();
    }
    if ((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
    {
        mRenderPassDesc.packStencilResolveAttachment();
    }

    image->onRenderPassAttach(mQueueSerial);
    mDepthResolveAttachment.init(image, level, layerStart, layerCount, VK_IMAGE_ASPECT_DEPTH_BIT);
    mStencilResolveAttachment.init(image, level, layerStart, layerCount,
                                   VK_IMAGE_ASPECT_STENCIL_BIT);
    image->getRenderPassUsage().flags(this).set(RenderPassUsage::RenderTargetAttachment);
}

void RenderPassCommandBufferHelper::resumeTransformFeedback()
{
    ASSERT(isTransformFeedbackStarted());

    uint32_t numCounterBuffers =
        mRebindTransformFeedbackBuffers ? 0 : mValidTransformFeedbackBufferCount;

    mRebindTransformFeedbackBuffers    = false;
    mIsTransformFeedbackActiveUnpaused = true;

    getCommandBuffer().beginTransformFeedback(0, numCounterBuffers,
                                              mTransformFeedbackCounterBuffers.data(),
                                              mTransformFeedbackCounterBufferOffsets.data());
}

void RenderPassCommandBufferHelper::pauseTransformFeedback()
{
    ASSERT(isTransformFeedbackStarted() && isTransformFeedbackActiveUnpaused());
    mIsTransformFeedbackActiveUnpaused = false;
    getCommandBuffer().endTransformFeedback(0, mValidTransformFeedbackBufferCount,
                                            mTransformFeedbackCounterBuffers.data(),
                                            mTransformFeedbackCounterBufferOffsets.data());
}

void RenderPassCommandBufferHelper::updateRenderPassColorClear(PackedAttachmentIndex colorIndexVk,
                                                               const VkClearValue &clearValue)
{
    mAttachmentOps.setClearOp(colorIndexVk);
    mClearValues.storeColor(colorIndexVk, clearValue);
}

void RenderPassCommandBufferHelper::updateRenderPassDepthStencilClear(
    VkImageAspectFlags aspectFlags,
    const VkClearValue &clearValue)
{
    // Don't overwrite prior clear values for individual aspects.
    VkClearValue combinedClearValue = mClearValues[mDepthStencilAttachmentIndex];

    if ((aspectFlags & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
    {
        mAttachmentOps.setClearOp(mDepthStencilAttachmentIndex);
        combinedClearValue.depthStencil.depth = clearValue.depthStencil.depth;
    }

    if ((aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
    {
        mAttachmentOps.setClearStencilOp(mDepthStencilAttachmentIndex);
        combinedClearValue.depthStencil.stencil = clearValue.depthStencil.stencil;
    }

    // Bypass special D/S handling. This clear values array stores values packed.
    mClearValues.storeDepthStencil(mDepthStencilAttachmentIndex, combinedClearValue);
}

void RenderPassCommandBufferHelper::growRenderArea(ContextVk *contextVk,
                                                   const gl::Rectangle &newRenderArea)
{
    // The render area is grown such that it covers both the previous and the new render areas.
    gl::GetEnclosingRectangle(mRenderArea, newRenderArea, &mRenderArea);

    // Remove invalidates that are no longer applicable.
    mDepthAttachment.onRenderAreaGrowth(contextVk, mRenderArea);
    mStencilAttachment.onRenderAreaGrowth(contextVk, mRenderArea);
}

angle::Result RenderPassCommandBufferHelper::attachCommandPool(ErrorContext *context,
                                                               SecondaryCommandPool *commandPool)
{
    ASSERT(!mRenderPassStarted);
    ASSERT(getSubpassCommandBufferCount() == 1);
    return attachCommandPoolImpl<RenderPassCommandBufferHelper>(context, commandPool);
}

void RenderPassCommandBufferHelper::detachCommandPool(SecondaryCommandPool **commandPoolOut)
{
    ASSERT(mRenderPassStarted);
    angle::Result result =
        detachCommandPoolImpl<RenderPassCommandBufferHelper, true>(nullptr, commandPoolOut);
    ASSERT(result == angle::Result::Continue);
}

void RenderPassCommandBufferHelper::releaseCommandPool()
{
    ASSERT(!mRenderPassStarted);
    ASSERT(getSubpassCommandBufferCount() == 1);
    releaseCommandPoolImpl<RenderPassCommandBufferHelper>();
}

ANGLE_INLINE void RenderPassCommandBufferHelper::assertCanBeRecycled()
{
    ASSERT(!mRenderPassStarted);
    ASSERT(getSubpassCommandBufferCount() == 1);
    assertCanBeRecycledImpl<RenderPassCommandBufferHelper>();
}

std::string RenderPassCommandBufferHelper::getCommandDiagnostics()
{
    std::ostringstream out;
    addCommandDiagnosticsCommon(&out);

    size_t attachmentCount             = mRenderPassDesc.clearableAttachmentCount();
    size_t depthStencilAttachmentCount = mRenderPassDesc.hasDepthStencilAttachment() ? 1 : 0;
    size_t colorAttachmentCount        = attachmentCount - depthStencilAttachmentCount;

    PackedAttachmentIndex attachmentIndexVk(0);
    std::string loadOps, storeOps;

    if (colorAttachmentCount > 0)
    {
        loadOps += " Color: ";
        storeOps += " Color: ";

        for (size_t i = 0; i < colorAttachmentCount; ++i)
        {
            loadOps += GetLoadOpShorthand(
                static_cast<RenderPassLoadOp>(mAttachmentOps[attachmentIndexVk].loadOp));
            storeOps += GetStoreOpShorthand(
                static_cast<RenderPassStoreOp>(mAttachmentOps[attachmentIndexVk].storeOp));
            ++attachmentIndexVk;
        }
    }

    if (depthStencilAttachmentCount > 0)
    {
        ASSERT(depthStencilAttachmentCount == 1);

        loadOps += " Depth/Stencil: ";
        storeOps += " Depth/Stencil: ";

        loadOps += GetLoadOpShorthand(
            static_cast<RenderPassLoadOp>(mAttachmentOps[attachmentIndexVk].loadOp));
        loadOps += GetLoadOpShorthand(
            static_cast<RenderPassLoadOp>(mAttachmentOps[attachmentIndexVk].stencilLoadOp));

        storeOps += GetStoreOpShorthand(
            static_cast<RenderPassStoreOp>(mAttachmentOps[attachmentIndexVk].storeOp));
        storeOps += GetStoreOpShorthand(
            static_cast<RenderPassStoreOp>(mAttachmentOps[attachmentIndexVk].stencilStoreOp));
    }

    if (attachmentCount > 0)
    {
        out << "LoadOp:  " << loadOps << "\\l";
        out << "StoreOp: " << storeOps << "\\l";
    }

    for (uint32_t subpass = 0; subpass < getSubpassCommandBufferCount(); ++subpass)
    {
        if (subpass > 0)
        {
            out << "Next Subpass" << "\\l";
        }
        out << mCommandBuffers[subpass].dumpCommands("\\l");
    }

    return out.str();
}

// CommandBufferRecycler implementation.
template <typename CommandBufferHelperT>
void CommandBufferRecycler<CommandBufferHelperT>::onDestroy()
{
    std::unique_lock<angle::SimpleMutex> lock(mMutex);
    for (CommandBufferHelperT *commandBufferHelper : mCommandBufferHelperFreeList)
    {
        SafeDelete(commandBufferHelper);
    }
    mCommandBufferHelperFreeList.clear();
}

template void CommandBufferRecycler<OutsideRenderPassCommandBufferHelper>::onDestroy();
template void CommandBufferRecycler<RenderPassCommandBufferHelper>::onDestroy();

template <typename CommandBufferHelperT>
angle::Result CommandBufferRecycler<CommandBufferHelperT>::getCommandBufferHelper(
    ErrorContext *context,
    SecondaryCommandPool *commandPool,
    CommandBufferHelperT **commandBufferHelperOut)
{
    std::unique_lock<angle::SimpleMutex> lock(mMutex);
    if (mCommandBufferHelperFreeList.empty())
    {
        CommandBufferHelperT *commandBuffer = new CommandBufferHelperT();
        *commandBufferHelperOut             = commandBuffer;
        ANGLE_TRY(commandBuffer->initialize(context));
    }
    else
    {
        CommandBufferHelperT *commandBuffer = mCommandBufferHelperFreeList.back();
        mCommandBufferHelperFreeList.pop_back();
        *commandBufferHelperOut = commandBuffer;
    }

    ANGLE_TRY((*commandBufferHelperOut)->attachCommandPool(context, commandPool));

    return angle::Result::Continue;
}

template angle::Result
CommandBufferRecycler<OutsideRenderPassCommandBufferHelper>::getCommandBufferHelper(
    ErrorContext *,
    SecondaryCommandPool *,
    OutsideRenderPassCommandBufferHelper **);
template angle::Result CommandBufferRecycler<RenderPassCommandBufferHelper>::getCommandBufferHelper(
    ErrorContext *,
    SecondaryCommandPool *,
    RenderPassCommandBufferHelper **);

template <typename CommandBufferHelperT>
void CommandBufferRecycler<CommandBufferHelperT>::recycleCommandBufferHelper(
    CommandBufferHelperT **commandBuffer)
{
    (*commandBuffer)->assertCanBeRecycled();
    (*commandBuffer)->markOpen();

    {
        std::unique_lock<angle::SimpleMutex> lock(mMutex);
        mCommandBufferHelperFreeList.push_back(*commandBuffer);
    }

    *commandBuffer = nullptr;
}

template void
CommandBufferRecycler<OutsideRenderPassCommandBufferHelper>::recycleCommandBufferHelper(
    OutsideRenderPassCommandBufferHelper **);
template void CommandBufferRecycler<RenderPassCommandBufferHelper>::recycleCommandBufferHelper(
    RenderPassCommandBufferHelper **);

// SecondaryCommandBufferCollector implementation.
void SecondaryCommandBufferCollector::collectCommandBuffer(
    priv::SecondaryCommandBuffer &&commandBuffer)
{
    commandBuffer.reset();
}

void SecondaryCommandBufferCollector::collectCommandBuffer(
    VulkanSecondaryCommandBuffer &&commandBuffer)
{
    ASSERT(commandBuffer.valid());
    mCollectedCommandBuffers.emplace_back(std::move(commandBuffer));
}

void SecondaryCommandBufferCollector::releaseCommandBuffers()
{
    // Note: we currently free the command buffers individually, but we could potentially reset the
    // entire command pool.  https://issuetracker.google.com/issues/166793850
    for (VulkanSecondaryCommandBuffer &commandBuffer : mCollectedCommandBuffers)
    {
        commandBuffer.destroy();
    }
    mCollectedCommandBuffers.clear();
}

// DynamicBuffer implementation.
DynamicBuffer::DynamicBuffer()
    : mUsage(0),
      mHostVisible(false),
      mInitialSize(0),
      mNextAllocationOffset(0),
      mSize(0),
      mSizeInRecentHistory(0),
      mAlignment(0),
      mMemoryPropertyFlags(0)
{}

DynamicBuffer::DynamicBuffer(DynamicBuffer &&other)
    : mUsage(other.mUsage),
      mHostVisible(other.mHostVisible),
      mInitialSize(other.mInitialSize),
      mBuffer(std::move(other.mBuffer)),
      mNextAllocationOffset(other.mNextAllocationOffset),
      mSize(other.mSize),
      mSizeInRecentHistory(other.mSizeInRecentHistory),
      mAlignment(other.mAlignment),
      mMemoryPropertyFlags(other.mMemoryPropertyFlags),
      mInFlightBuffers(std::move(other.mInFlightBuffers)),
      mBufferFreeList(std::move(other.mBufferFreeList))
{}

void DynamicBuffer::init(Renderer *renderer,
                         VkBufferUsageFlags usage,
                         size_t alignment,
                         size_t initialSize,
                         bool hostVisible)
{
    mUsage       = usage;
    mHostVisible = hostVisible;
    mMemoryPropertyFlags =
        (hostVisible) ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (hostVisible && renderer->getFeatures().preferHostCachedForNonStaticBufferUsage.enabled)
    {
        mMemoryPropertyFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    // Check that we haven't overridden the initial size of the buffer in setMinimumSizeForTesting.
    if (mInitialSize == 0)
    {
        mInitialSize         = initialSize;
        mSize                = 0;
        mSizeInRecentHistory = initialSize;
    }

    // Workaround for the mock ICD not supporting allocations greater than 0x1000.
    // Could be removed if https://github.com/KhronosGroup/Vulkan-Tools/issues/84 is fixed.
    if (renderer->isMockICDEnabled())
    {
        mSize = std::min<size_t>(mSize, 0x1000);
    }

    requireAlignment(renderer, alignment);
}

DynamicBuffer::~DynamicBuffer()
{
    ASSERT(mBuffer == nullptr);
    ASSERT(mInFlightBuffers.empty());
    ASSERT(mBufferFreeList.empty());
}

angle::Result DynamicBuffer::allocateNewBuffer(ErrorContext *context)
{
    // Allocate the buffer
    ASSERT(!mBuffer);
    RendererScoped<BufferHelper> buffer(context->getRenderer());

    VkBufferCreateInfo createInfo    = {};
    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.flags                 = 0;
    createInfo.size                  = mSize;
    createInfo.usage                 = mUsage;
    createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices   = nullptr;

    ANGLE_TRY(buffer.get().init(context, createInfo, mMemoryPropertyFlags));

    mBuffer = std::make_unique<BufferHelper>(buffer.release());
    return angle::Result::Continue;
}

bool DynamicBuffer::allocateFromCurrentBuffer(size_t sizeInBytes, BufferHelper **bufferHelperOut)
{
    if (mBuffer == nullptr)
    {
        return false;
    }

    mNextAllocationOffset =
        roundUp<uint32_t>(mNextAllocationOffset, static_cast<uint32_t>(mAlignment));

    ASSERT(bufferHelperOut);
    size_t sizeToAllocate                                      = roundUp(sizeInBytes, mAlignment);
    angle::base::CheckedNumeric<size_t> checkedNextWriteOffset = mNextAllocationOffset;
    checkedNextWriteOffset += sizeToAllocate;

    if (!checkedNextWriteOffset.IsValid() || checkedNextWriteOffset.ValueOrDie() > mSize)
    {
        return false;
    }

    ASSERT(mBuffer != nullptr);
    ASSERT(mHostVisible);
    ASSERT(mBuffer->getMappedMemory());
    mBuffer->setSuballocationOffsetAndSize(mNextAllocationOffset, sizeToAllocate);
    *bufferHelperOut = mBuffer.get();

    mNextAllocationOffset += static_cast<uint32_t>(sizeToAllocate);
    return true;
}

angle::Result DynamicBuffer::allocate(Context *context,
                                      size_t sizeInBytes,
                                      BufferHelper **bufferHelperOut,
                                      bool *newBufferAllocatedOut)
{
    ASSERT(sizeInBytes != 0);
    bool newBuffer = !allocateFromCurrentBuffer(sizeInBytes, bufferHelperOut);
    if (newBufferAllocatedOut)
    {
        *newBufferAllocatedOut = newBuffer;
    }

    if (!newBuffer)
    {
        return angle::Result::Continue;
    }

    size_t sizeToAllocate = roundUp(sizeInBytes, mAlignment);

    if (mBuffer)
    {
        // Make sure the buffer is not released externally.
        ASSERT(mBuffer->valid());
        mInFlightBuffers.push_back(std::move(mBuffer));
        ASSERT(!mBuffer);
    }

    Renderer *renderer = context->getRenderer();

    const size_t minRequiredBlockSize = std::max(mInitialSize, sizeToAllocate);

    // The average required buffer size in recent history is used to determine whether the currently
    // used buffer size needs to be reduced (when it goes below 1/8 of the current buffer size).
    constexpr uint32_t kDecayCoeffPercent = 20;
    static_assert(kDecayCoeffPercent >= 0 && kDecayCoeffPercent <= 100);
    mSizeInRecentHistory = (mSizeInRecentHistory * kDecayCoeffPercent +
                            minRequiredBlockSize * (100 - kDecayCoeffPercent) + 50) /
                           100;

    if (sizeToAllocate > mSize || mSizeInRecentHistory < mSize / 8)
    {
        mSize = minRequiredBlockSize;
        // Clear the free list since the free buffers are now either too small or too big.
        ReleaseBufferListToRenderer(context, &mBufferFreeList);
    }

    // The front of the free list should be the oldest. Thus if it is in use the rest of the
    // free list should be in use as well.
    if (mBufferFreeList.empty() ||
        !renderer->hasResourceUseFinished(mBufferFreeList.front()->getResourceUse()))
    {
        ANGLE_TRY(allocateNewBuffer(context));
    }
    else
    {
        mBuffer = std::move(mBufferFreeList.front());
        mBufferFreeList.pop_front();
    }

    ASSERT(mBuffer->getBlockMemorySize() == mSize);

    mNextAllocationOffset = 0;

    ASSERT(mBuffer != nullptr);
    mBuffer->setSuballocationOffsetAndSize(mNextAllocationOffset, sizeToAllocate);
    *bufferHelperOut = mBuffer.get();

    mNextAllocationOffset += static_cast<uint32_t>(sizeToAllocate);
    return angle::Result::Continue;
}

void DynamicBuffer::release(Context *context)
{
    reset();

    ReleaseBufferListToRenderer(context, &mInFlightBuffers);
    ReleaseBufferListToRenderer(context, &mBufferFreeList);

    if (mBuffer)
    {
        mBuffer->release(context);
        mBuffer.reset(nullptr);
    }
}

void DynamicBuffer::updateQueueSerialAndReleaseInFlightBuffers(ContextVk *contextVk,
                                                               const QueueSerial &queueSerial)
{
    for (std::unique_ptr<BufferHelper> &bufferHelper : mInFlightBuffers)
    {
        // This function is used only for internal buffers, and they are all read-only.
        // It's possible this may change in the future, but there isn't a good way to detect that,
        // unfortunately.
        bufferHelper->setQueueSerial(queueSerial);

        // We only keep free buffers that have the same size. Note that bufferHelper's size is
        // suballocation's size. We need to use the whole block memory size here.
        if (bufferHelper->getBlockMemorySize() != mSize)
        {
            bufferHelper->release(contextVk);
        }
        else
        {
            mBufferFreeList.push_back(std::move(bufferHelper));
        }
    }
    mInFlightBuffers.clear();
}

void DynamicBuffer::destroy(Renderer *renderer)
{
    reset();

    DestroyBufferList(renderer, &mInFlightBuffers);
    DestroyBufferList(renderer, &mBufferFreeList);

    if (mBuffer)
    {
        mBuffer->unmap(renderer);
        mBuffer->destroy(renderer);
        mBuffer.reset(nullptr);
    }
}

void DynamicBuffer::requireAlignment(Renderer *renderer, size_t alignment)
{
    ASSERT(alignment > 0);

    size_t prevAlignment = mAlignment;

    // If alignment was never set, initialize it with the atom size limit.
    if (prevAlignment == 0)
    {
        prevAlignment =
            static_cast<size_t>(renderer->getPhysicalDeviceProperties().limits.nonCoherentAtomSize);
        ASSERT(gl::isPow2(prevAlignment));
    }

    // We need lcm(prevAlignment, alignment).  Usually, one divides the other so std::max() could be
    // used instead.  Only known case where this assumption breaks is for 3-component types with
    // 16- or 32-bit channels, so that's special-cased to avoid a full-fledged lcm implementation.

    if (gl::isPow2(prevAlignment * alignment))
    {
        ASSERT(alignment % prevAlignment == 0 || prevAlignment % alignment == 0);

        alignment = std::max(prevAlignment, alignment);
    }
    else
    {
        ASSERT(prevAlignment % 3 != 0 || gl::isPow2(prevAlignment / 3));
        ASSERT(alignment % 3 != 0 || gl::isPow2(alignment / 3));

        prevAlignment = prevAlignment % 3 == 0 ? prevAlignment / 3 : prevAlignment;
        alignment     = alignment % 3 == 0 ? alignment / 3 : alignment;

        alignment = std::max(prevAlignment, alignment) * 3;
    }

    // If alignment has changed, make sure the next allocation is done at an aligned offset.
    if (alignment != mAlignment)
    {
        mNextAllocationOffset = roundUp(mNextAllocationOffset, static_cast<uint32_t>(alignment));
    }

    mAlignment = alignment;
}

void DynamicBuffer::setMinimumSizeForTesting(size_t minSize)
{
    // This will really only have an effect next time we call allocate.
    mInitialSize = minSize;

    // Forces a new allocation on the next allocate.
    mSize                = 0;
    mSizeInRecentHistory = 0;
}

void DynamicBuffer::reset()
{
    mSize                 = 0;
    mSizeInRecentHistory  = 0;
    mNextAllocationOffset = 0;
}

// BufferPool implementation.
BufferPool::BufferPool()
    : mVirtualBlockCreateFlags(vma::VirtualBlockCreateFlagBits::GENERAL),
      mUsage(0),
      mHostVisible(false),
      mSize(0),
      mInitialSize(0),
      mPreferredSize(0),
      mMemoryTypeIndex(0),
      mTotalMemorySize(0),
      mNumberOfNewBuffersNeededSinceLastPrune(0)
{}

BufferPool::BufferPool(BufferPool &&other)
    : mVirtualBlockCreateFlags(other.mVirtualBlockCreateFlags),
      mUsage(other.mUsage),
      mHostVisible(other.mHostVisible),
      mSize(other.mSize),
      mInitialSize(other.mInitialSize),
      mPreferredSize(other.mPreferredSize),
      mMemoryTypeIndex(other.mMemoryTypeIndex)
{}

void BufferPool::initWithFlags(Renderer *renderer,
                               vma::VirtualBlockCreateFlags flags,
                               VkBufferUsageFlags usage,
                               uint32_t memoryTypeIndex,
                               VkMemoryPropertyFlags memoryPropertyFlags)
{
    mVirtualBlockCreateFlags = flags;
    mUsage                   = usage;
    mMemoryTypeIndex         = memoryTypeIndex;
    mInitialSize             = renderer->getPreferredInitialBufferBlockSize(memoryTypeIndex);
    mPreferredSize           = renderer->getPreferredLargeBufferBlockSize(memoryTypeIndex);
    mSize                    = mInitialSize;
    mHostVisible             = ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
    mBufferBlocks.reserve(32);
}

BufferPool::~BufferPool()
{
    ASSERT(mBufferBlocks.empty());
    ASSERT(mEmptyBufferBlocks.empty());
}

void BufferPool::pruneEmptyBuffers(Renderer *renderer)
{
    // Walk through mBuffers and move empty buffers to mEmptyBuffer and remove null
    // pointers for allocation performance.
    bool needsCompact          = false;
    size_t nonEmptyBufferCount = 0;
    for (std::unique_ptr<BufferBlock> &block : mBufferBlocks)
    {
        if (block->isEmpty())
        {
            // We will always free empty buffers that has smaller size. Or if the empty buffer has
            // been found empty for long enough time, or we accumulated too many empty buffers, we
            // also free it.
            if (block->getMemorySize() < mSize)
            {
                mTotalMemorySize -= block->getMemorySize();
                block->destroy(renderer);
                block.reset();
            }
            else
            {
                mEmptyBufferBlocks.push_back(std::move(block));
            }
            needsCompact = true;
        }
        else
        {
            if (needsCompact)
            {
                mBufferBlocks[nonEmptyBufferCount] = std::move(block);
            }
            nonEmptyBufferCount++;
        }
    }

    if (needsCompact)
    {
        mBufferBlocks.resize(nonEmptyBufferCount);
    }

    // Decide how many empty buffers to keep around and trim down the excessive empty buffers. We
    // keep track of how many buffers are needed since last prune. Assume we are in stable state,
    // which means we may still need that many empty buffers in next prune cycle. To reduce chance
    // to call into vulkan driver to allocate new buffers, we try to keep that many empty buffers
    // around, subject to the maximum cap. If we overestimate, next cycle they used fewer buffers,
    // we will trim excessive empty buffers at next prune call. Or if we underestimate, we will end
    // up have to call into vulkan driver allocate new buffers, but next cycle we should correct
    // ourselves to keep enough number of empty buffers around.
    size_t buffersToKeep = std::min(mNumberOfNewBuffersNeededSinceLastPrune,
                                    static_cast<size_t>(kMaxTotalEmptyBufferBytes / mSize));
    while (mEmptyBufferBlocks.size() > buffersToKeep)
    {
        std::unique_ptr<BufferBlock> &block = mEmptyBufferBlocks.back();
        mTotalMemorySize -= block->getMemorySize();
        block->destroy(renderer);
        mEmptyBufferBlocks.pop_back();
    }
    mNumberOfNewBuffersNeededSinceLastPrune = 0;
}

VkResult BufferPool::allocateNewBuffer(ErrorContext *context, VkDeviceSize sizeInBytes)
{
    Renderer *renderer         = context->getRenderer();
    const Allocator &allocator = renderer->getAllocator();

    VkDeviceSize heapSize =
        renderer->getMemoryProperties().getHeapSizeForMemoryType(mMemoryTypeIndex);

    // First ensure we are not exceeding the heapSize to avoid the validation error.
    VK_RESULT_CHECK(sizeInBytes <= heapSize, VK_ERROR_OUT_OF_DEVICE_MEMORY);

    // Double the size until the requirement is met. This also helps reduce fragmentation. Since
    // this is global pool, we have less worry about memory waste.
    // If this is the first buffer block allocation from an empty pool, the initial size is used.
    // However, the preferred size will be used for future blocks in the same pool.
    ASSERT(mInitialSize <= mPreferredSize);
    VkDeviceSize newSize = mBufferBlocks.empty() ? mInitialSize : mPreferredSize;
    while (newSize < sizeInBytes)
    {
        newSize <<= 1;
    }
    mSize = std::min(newSize, heapSize);

    // Allocate buffer
    VkBufferCreateInfo createInfo    = {};
    createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.flags                 = 0;
    createInfo.size                  = mSize;
    createInfo.usage                 = mUsage;
    createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices   = nullptr;

    VkMemoryPropertyFlags memoryPropertyFlags;
    allocator.getMemoryTypeProperties(mMemoryTypeIndex, &memoryPropertyFlags);

    DeviceScoped<Buffer> buffer(renderer->getDevice());
    VK_RESULT_TRY(buffer.get().init(context->getDevice(), createInfo));

    DeviceScoped<DeviceMemory> deviceMemory(renderer->getDevice());
    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    VkDeviceSize sizeOut;
    uint32_t memoryTypeIndex;

    const void *extraAllocationInfoPtr = context->getFeatures().supportsBufferDeviceAddress.enabled
                                             ? &kMemoryAllocateDeviceAddressFlags
                                             : nullptr;

    VK_RESULT_TRY(AllocateBufferMemory(
        context, MemoryAllocationType::Buffer, memoryPropertyFlags, &memoryPropertyFlagsOut,
        extraAllocationInfoPtr, &buffer.get(), &memoryTypeIndex, &deviceMemory.get(), &sizeOut));
    ASSERT(sizeOut >= mSize);

    // Allocate bufferBlock
    std::unique_ptr<BufferBlock> block = std::make_unique<BufferBlock>();
    VK_RESULT_TRY(block->init(context, buffer.get(), memoryTypeIndex, mVirtualBlockCreateFlags,
                              deviceMemory.get(), memoryPropertyFlagsOut, mSize));

    if (mHostVisible)
    {
        VK_RESULT_TRY(block->map(context->getDevice()));
    }

    mTotalMemorySize += block->getMemorySize();
    // Append the bufferBlock into the pool
    mBufferBlocks.push_back(std::move(block));
    context->getPerfCounters().allocateNewBufferBlockCalls++;

    return VK_SUCCESS;
}

VkResult BufferPool::allocateBuffer(ErrorContext *context,
                                    VkDeviceSize sizeInBytes,
                                    VkDeviceSize alignment,
                                    BufferSuballocation *suballocation)
{
    ASSERT(alignment);
    VmaVirtualAllocation allocation;
    VkDeviceSize offset;
    VkDeviceSize alignedSize = roundUp(sizeInBytes, alignment);

    if (alignedSize >= kMaxBufferSizeForSuballocation)
    {
        VkDeviceSize heapSize =
            context->getRenderer()->getMemoryProperties().getHeapSizeForMemoryType(
                mMemoryTypeIndex);
        // First ensure we are not exceeding the heapSize to avoid the validation error.
        VK_RESULT_CHECK(sizeInBytes <= heapSize, VK_ERROR_OUT_OF_DEVICE_MEMORY);

        // Allocate buffer
        VkBufferCreateInfo createInfo    = {};
        createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.flags                 = 0;
        createInfo.size                  = alignedSize;
        createInfo.usage                 = mUsage;
        createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices   = nullptr;

        VkMemoryPropertyFlags memoryPropertyFlags;
        const Allocator &allocator = context->getRenderer()->getAllocator();
        allocator.getMemoryTypeProperties(mMemoryTypeIndex, &memoryPropertyFlags);

        DeviceScoped<Buffer> buffer(context->getDevice());
        VK_RESULT_TRY(buffer.get().init(context->getDevice(), createInfo));

        DeviceScoped<DeviceMemory> deviceMemory(context->getDevice());
        VkMemoryPropertyFlags memoryPropertyFlagsOut;
        VkDeviceSize sizeOut;
        uint32_t memoryTypeIndex;

        const void *extraAllocationInfoPtr =
            context->getFeatures().supportsBufferDeviceAddress.enabled
                ? &kMemoryAllocateDeviceAddressFlags
                : nullptr;

        VK_RESULT_TRY(AllocateBufferMemory(context, MemoryAllocationType::Buffer,
                                           memoryPropertyFlags, &memoryPropertyFlagsOut,
                                           extraAllocationInfoPtr, &buffer.get(), &memoryTypeIndex,
                                           &deviceMemory.get(), &sizeOut));
        ASSERT(sizeOut >= alignedSize);

        suballocation->initWithEntireBuffer(context, buffer.get(), MemoryAllocationType::Buffer,
                                            memoryTypeIndex, deviceMemory.get(),
                                            memoryPropertyFlagsOut, alignedSize, sizeOut);
        if (mHostVisible)
        {
            VK_RESULT_TRY(suballocation->map(context));
        }
        return VK_SUCCESS;
    }

    // We always allocate from reverse order so that older buffers have a chance to be empty. The
    // assumption is that to allocate from new buffers first may have a better chance to leave the
    // older buffers completely empty and we may able to free it.
    for (auto iter = mBufferBlocks.rbegin(); iter != mBufferBlocks.rend();)
    {
        std::unique_ptr<BufferBlock> &block = *iter;
        if (block->isEmpty() && block->getMemorySize() < mSize)
        {
            // Don't try to allocate from an empty buffer that has smaller size. It will get
            // released when pruneEmptyBuffers get called later on.
            ++iter;
            continue;
        }

        if (block->allocate(alignedSize, alignment, &allocation, &offset) == VK_SUCCESS)
        {
            suballocation->init(block.get(), allocation, offset, alignedSize);
            return VK_SUCCESS;
        }
        ++iter;
    }

    // Try to allocate from empty buffers
    while (!mEmptyBufferBlocks.empty())
    {
        std::unique_ptr<BufferBlock> &block = mEmptyBufferBlocks.back();
        if (block->getMemorySize() < mSize || block->getMemorySize() < alignedSize)
        {
            mTotalMemorySize -= block->getMemorySize();
            block->destroy(context->getRenderer());
            mEmptyBufferBlocks.pop_back();
        }
        else
        {
            VK_RESULT_TRY(block->allocate(alignedSize, alignment, &allocation, &offset));
            suballocation->init(block.get(), allocation, offset, alignedSize);
            mBufferBlocks.push_back(std::move(block));
            mEmptyBufferBlocks.pop_back();
            mNumberOfNewBuffersNeededSinceLastPrune++;
            return VK_SUCCESS;
        }
    }

    // Failed to allocate from empty buffer. Now try to allocate a new buffer.
    VK_RESULT_TRY(allocateNewBuffer(context, alignedSize));

    // Sub-allocate from the bufferBlock.
    std::unique_ptr<BufferBlock> &block = mBufferBlocks.back();
    VK_RESULT_CHECK(block->allocate(alignedSize, alignment, &allocation, &offset) == VK_SUCCESS,
                    VK_ERROR_OUT_OF_DEVICE_MEMORY);
    suballocation->init(block.get(), allocation, offset, alignedSize);
    mNumberOfNewBuffersNeededSinceLastPrune++;

    return VK_SUCCESS;
}

void BufferPool::destroy(Renderer *renderer, bool orphanNonEmptyBufferBlock)
{
    for (std::unique_ptr<BufferBlock> &block : mBufferBlocks)
    {
        if (block->isEmpty())
        {
            block->destroy(renderer);
        }
        else
        {
            // When orphan is not allowed, all BufferBlocks must be empty.
            // If Device is lost due to errors then add non-empty BufferBlocks
            // to the orphan list to be freed towards end.
            ASSERT(renderer->isDeviceLost() || orphanNonEmptyBufferBlock);
            renderer->addBufferBlockToOrphanList(block.release());
        }
    }
    mBufferBlocks.clear();

    for (std::unique_ptr<BufferBlock> &block : mEmptyBufferBlocks)
    {
        block->destroy(renderer);
    }
    mEmptyBufferBlocks.clear();
}

VkDeviceSize BufferPool::getTotalEmptyMemorySize() const
{
    VkDeviceSize totalMemorySize = 0;
    for (const std::unique_ptr<BufferBlock> &block : mEmptyBufferBlocks)
    {
        totalMemorySize += block->getMemorySize();
    }
    return totalMemorySize;
}

void BufferPool::addStats(std::ostringstream *out) const
{
    VkDeviceSize totalUnusedBytes = 0;
    VkDeviceSize totalMemorySize  = 0;
    for (size_t i = 0; i < mBufferBlocks.size(); i++)
    {
        const std::unique_ptr<BufferBlock> &block = mBufferBlocks[i];
        vma::StatInfo statInfo;
        block->calculateStats(&statInfo);
        ASSERT(statInfo.basicInfo.blockCount == 1);
        INFO() << "[" << i << "]={" << " allocationCount:" << statInfo.basicInfo.allocationCount
               << " blockBytes:" << statInfo.basicInfo.blockBytes
               << " allocationBytes:" << statInfo.basicInfo.allocationBytes
               << " unusedRangeCount:" << statInfo.unusedRangeCount
               << " allocationSizeMin:" << statInfo.allocationSizeMin
               << " allocationSizeMax:" << statInfo.allocationSizeMax
               << " unusedRangeSizeMin:" << statInfo.unusedRangeSizeMin
               << " unusedRangeSizeMax:" << statInfo.unusedRangeSizeMax << " }";
        VkDeviceSize unusedBytes =
            statInfo.basicInfo.blockBytes - statInfo.basicInfo.allocationBytes;
        totalUnusedBytes += unusedBytes;
        totalMemorySize += block->getMemorySize();
    }
    *out << "mBufferBlocks.size():" << mBufferBlocks.size()
         << " totalUnusedBytes:" << totalUnusedBytes / 1024
         << "KB / totalMemorySize:" << totalMemorySize / 1024 << "KB";
    *out << " emptyBuffers [memorySize:" << getTotalEmptyMemorySize() / 1024 << "KB "
         << " count:" << mEmptyBufferBlocks.size()
         << " needed: " << mNumberOfNewBuffersNeededSinceLastPrune << "]";
}

// DescriptorSetHelper implementation.
void DescriptorSetHelper::destroy(VkDevice device)
{
    if (valid())
    {
        // Since the pool is created without VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, we
        // don't call vkFreeDescriptorSets. We always add to garbage list so that it can be
        // recycled. Since we dont actually know if it is GPU completed, we always just add to the
        // pending garbage list assuming the worst case.
        DescriptorPoolPointer pool(device, mPool);
        DescriptorSetPointer garbage(device, std::move(*this));
        pool->addPendingGarbage(std::move(garbage));
        ASSERT(!valid());
    }
}

// DescriptorPoolHelper implementation.
DescriptorPoolHelper::DescriptorPoolHelper()
    : mMaxDescriptorSets(0), mValidDescriptorSets(0), mFreeDescriptorSets(0)
{}

DescriptorPoolHelper::~DescriptorPoolHelper()
{
    ASSERT(mPendingGarbageList.empty());
    ASSERT(mFinishedGarbageList.empty());
}

angle::Result DescriptorPoolHelper::init(ErrorContext *context,
                                         const std::vector<VkDescriptorPoolSize> &poolSizesIn,
                                         uint32_t maxSets)
{
    Renderer *renderer = context->getRenderer();

    ASSERT(mPendingGarbageList.empty());
    ASSERT(mFinishedGarbageList.empty());

    if (mDescriptorPool.valid())
    {
        mDescriptorPool.destroy(renderer->getDevice());
    }

    // Make a copy of the pool sizes, so we can grow them to satisfy the specified maxSets.
    std::vector<VkDescriptorPoolSize> poolSizes = poolSizesIn;

    for (VkDescriptorPoolSize &poolSize : poolSizes)
    {
        poolSize.descriptorCount *= maxSets;
    }

    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.flags                      = 0;
    descriptorPoolInfo.maxSets                    = maxSets;
    descriptorPoolInfo.poolSizeCount              = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes                 = poolSizes.data();

    mMaxDescriptorSets   = maxSets;
    mFreeDescriptorSets  = maxSets;
    mValidDescriptorSets = 0;

    ANGLE_VK_TRY(context, mDescriptorPool.init(renderer->getDevice(), descriptorPoolInfo));

    mRenderer = renderer;

    return angle::Result::Continue;
}

void DescriptorPoolHelper::destroy(VkDevice device)
{
    ASSERT(mValidDescriptorSets == 0);
    ASSERT(mPendingGarbageList.empty());
    ASSERT(mFinishedGarbageList.empty());
    mDescriptorPool.destroy(device);
}

bool DescriptorPoolHelper::allocateVkDescriptorSet(ErrorContext *context,
                                                   const DescriptorSetLayout &descriptorSetLayout,
                                                   VkDescriptorSet *descriptorSetOut)
{
    if (mFreeDescriptorSets > 0)
    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool              = mDescriptorPool.getHandle();
        allocInfo.descriptorSetCount          = 1;
        allocInfo.pSetLayouts                 = descriptorSetLayout.ptr();

        VkResult result = mDescriptorPool.allocateDescriptorSets(context->getDevice(), allocInfo,
                                                                 descriptorSetOut);
        ++context->getPerfCounters().descriptorSetAllocations;
        // If fail, it means our own accounting has a bug.
        ASSERT(result == VK_SUCCESS);
        mFreeDescriptorSets--;
        mValidDescriptorSets++;
        return true;
    }

    return false;
}

void DescriptorPoolHelper::cleanupPendingGarbage()
{
    while (!mPendingGarbageList.empty())
    {
        DescriptorSetPointer &garbage = mPendingGarbageList.front();
        if (!mRenderer->hasResourceUseFinished(garbage->getResourceUse()))
        {
            break;
        }
        mFinishedGarbageList.push_back(std::move(garbage));
        mPendingGarbageList.pop_front();
    }
}

void DescriptorPoolHelper::forceFinishPendingGarbage()
{
    while (!mPendingGarbageList.empty())
    {
        mFinishedGarbageList.push_back(std::move(mPendingGarbageList.front()));
        mPendingGarbageList.pop_front();
    }
}

bool DescriptorPoolHelper::recycleFromGarbage(Renderer *renderer,
                                              DescriptorSetPointer *descriptorSetOut)
{
    if (mFinishedGarbageList.empty())
    {
        cleanupPendingGarbage();
    }

    if (!mFinishedGarbageList.empty())
    {
        DescriptorSetPointer &garbage = mFinishedGarbageList.front();
        *descriptorSetOut             = std::move(garbage);
        mFinishedGarbageList.pop_front();
        mValidDescriptorSets++;
        return true;
    }

    return false;
}

bool DescriptorPoolHelper::allocateDescriptorSet(ErrorContext *context,
                                                 const DescriptorSetLayout &descriptorSetLayout,
                                                 const DescriptorPoolPointer &pool,
                                                 DescriptorSetPointer *descriptorSetOut)
{
    ASSERT(pool.get() == this);
    VkDescriptorSet descriptorSet;
    if (allocateVkDescriptorSet(context, descriptorSetLayout, &descriptorSet))
    {
        DescriptorSetHelper helper = DescriptorSetHelper(descriptorSet, pool);
        *descriptorSetOut          = DescriptorSetPointer(context->getDevice(), std::move(helper));
        return true;
    }
    return false;
}

void DescriptorPoolHelper::destroyGarbage()
{
    ASSERT(mPendingGarbageList.empty());

    while (!mFinishedGarbageList.empty())
    {
        DescriptorSetPointer &garbage = mFinishedGarbageList.front();
        ASSERT(garbage.unique());
        ASSERT(garbage->valid());
        // Because we do not use VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT when pool is
        // created, We can't free each individual descriptor set before destroying the pool, we
        // simply clear the descriptorSet and the mPool weak pointer so that
        // DescriptorSetHelper::destroy will not find it the garbage being valid  and try to add to
        // garbage list again.
        garbage->mDescriptorSet = VK_NULL_HANDLE;
        garbage->mPool.reset();
        ASSERT(!garbage->valid());
        mFinishedGarbageList.pop_front();
    }
}

// DynamicDescriptorPool implementation.
DynamicDescriptorPool::DynamicDescriptorPool() : mCachedDescriptorSetLayout(VK_NULL_HANDLE)
{
    mDescriptorPools.reserve(32);
}

DynamicDescriptorPool::~DynamicDescriptorPool()
{
    ASSERT(mLRUList.empty());
    ASSERT(mDescriptorSetCache.empty());
    ASSERT(mDescriptorPools.empty());
}

DynamicDescriptorPool::DynamicDescriptorPool(DynamicDescriptorPool &&other)
    : DynamicDescriptorPool()
{
    *this = std::move(other);
}

DynamicDescriptorPool &DynamicDescriptorPool::operator=(DynamicDescriptorPool &&other)
{
    std::swap(mDescriptorPools, other.mDescriptorPools);
    std::swap(mPoolSizes, other.mPoolSizes);
    std::swap(mCachedDescriptorSetLayout, other.mCachedDescriptorSetLayout);
    std::swap(mLRUList, other.mLRUList);
    std::swap(mDescriptorSetCache, other.mDescriptorSetCache);
    return *this;
}

angle::Result DynamicDescriptorPool::init(ErrorContext *context,
                                          const VkDescriptorPoolSize *setSizes,
                                          size_t setSizeCount,
                                          const DescriptorSetLayout &descriptorSetLayout)
{
    ASSERT(setSizes);
    ASSERT(setSizeCount);
    ASSERT(mDescriptorPools.empty());
    ASSERT(mCachedDescriptorSetLayout == VK_NULL_HANDLE);
    mPoolSizes.reserve(setSizeCount);
    mPoolSizes.assign(setSizes, ANGLE_UNSAFE_TODO(setSizes + setSizeCount));
    mCachedDescriptorSetLayout = descriptorSetLayout.getHandle();

    DescriptorPoolPointer newPool = DescriptorPoolPointer::MakeShared(context->getDevice());
    ANGLE_TRY(newPool->init(context, mPoolSizes, mMaxSetsPerPool));

    mDescriptorPools.emplace_back(std::move(newPool));

    return angle::Result::Continue;
}

void DynamicDescriptorPool::destroy(VkDevice device)
{
    // Destroy cache
    mDescriptorSetCache.clear();

    // Destroy LRU list and SharedDescriptorSetCacheKey.
    for (auto it = mLRUList.begin(); it != mLRUList.end();)
    {
        (it->sharedCacheKey)->destroy(device);
        it = mLRUList.erase(it);
    }
    ASSERT(mLRUList.empty());

    for (DescriptorPoolPointer &pool : mDescriptorPools)
    {
        // Usually all pending garbage should have been finished when DynamicDescriptorPool is
        // destroyed. But when context runs into error and submit code path early out, we could have
        // unfinished garbage in the pending list. So force all pending garbage to be cleared rather
        // than being left in place until the pool itself is deleted.
        pool->forceFinishPendingGarbage();
        pool->destroyGarbage();
        ASSERT(pool.unique());
    }
    mDescriptorPools.clear();

    mCachedDescriptorSetLayout = VK_NULL_HANDLE;
}

bool DynamicDescriptorPool::allocateFromExistingPool(ErrorContext *context,
                                                     const DescriptorSetLayout &descriptorSetLayout,
                                                     DescriptorSetPointer *descriptorSetOut)
{
    for (DescriptorPoolPointer &pool : mDescriptorPools)
    {
        if (!pool || !pool->valid())
        {
            continue;
        }
        if (pool->allocateDescriptorSet(context, descriptorSetLayout, pool, descriptorSetOut))
        {
            return true;
        }
    }
    return false;
}

bool DynamicDescriptorPool::recycleFromGarbage(Renderer *renderer,
                                               DescriptorSetPointer *descriptorSetOut)
{
    for (DescriptorPoolPointer &pool : mDescriptorPools)
    {
        if (pool->recycleFromGarbage(renderer, descriptorSetOut))
        {
            return true;
        }
    }
    return false;
}

bool DynamicDescriptorPool::evictStaleDescriptorSets(Renderer *renderer,
                                                     uint32_t oldestFrameToKeep,
                                                     uint32_t currentFrame)
{
    ASSERT(oldestFrameToKeep < currentFrame);
    size_t descriptorSetEvicted = 0;
    // Walk LRU list backwards from oldest to most recent, evict anything that earlier than
    // oldestFrameIDToKeep.
    auto it = mLRUList.rbegin();
    while (it != mLRUList.rend())
    {
        DescriptorSetPointer &descriptorSet = it->descriptorSet;
        if (descriptorSet.unique())
        {
            // Stop if it is recently being used.
            if (descriptorSet->getLastUsedFrame() > oldestFrameToKeep)
            {
                break;
            }
            // Stop if GPU is still busy
            if (!renderer->hasResourceUseFinished(descriptorSet->getResourceUse()))
            {
                break;
            }
            // Evict it from the cache and remove it from LRU list.
            bool removed = mDescriptorSetCache.eraseDescriptorSet(it->sharedCacheKey->getDesc());
            ASSERT(removed);
            // Invalidate the sharedCacheKey so that they could be reused.
            it->sharedCacheKey->destroy(renderer->getDevice());
            ASSERT(!it->sharedCacheKey->valid());

            // Note that erase it from LRU list will "destroy" descriptorSet. Since we
            // never actually destroy descriptorSet, it will just add to the garbage list. Here we
            // want more explicit control to add it to the front of list (because we know it is
            // already GPU completed) instead to the end of the list, so we do it explicitly.
            DescriptorPoolWeakPointer pool = descriptorSet->getPool();
            pool->addFinishedGarbage(std::move(descriptorSet));
            descriptorSetEvicted++;

            // This should destroy descriptorSet, which is already invalid;
            it = decltype(it)(mLRUList.erase(std::next(it).base()));
            mCacheStats.decrementSize();
        }
        else
        {
            // It means it is still bound to one of the programs. Move it to the front of the LRU
            // list to avoid repeatedly hitting it for every eviction.
            mLRUList.splice(mLRUList.begin(), mLRUList, std::next(it).base());
            ++it;
            // Update to currentFrame to maintain LRU order
            descriptorSet->updateLastUsedFrame(currentFrame);
        }
    }

    if (descriptorSetEvicted > 0)
    {
        // If there is any pool that is completely empty, destroy it first so that we can allocate
        // from partial pool.
        checkAndDestroyUnusedPool(renderer);
        return true;
    }

    return false;
}

angle::Result DynamicDescriptorPool::allocateDescriptorSet(
    ErrorContext *context,
    const DescriptorSetLayout &descriptorSetLayout,
    DescriptorSetPointer *descriptorSetOut)
{
    ASSERT(!mDescriptorPools.empty());
    ASSERT(descriptorSetLayout.getHandle() == mCachedDescriptorSetLayout);

    if (allocateFromExistingPool(context, descriptorSetLayout, descriptorSetOut))
    {
        return angle::Result::Continue;
    }

    if (recycleFromGarbage(context->getRenderer(), descriptorSetOut))
    {
        return angle::Result::Continue;
    }

    // Last, try to allocate a new pool (and/or evict an existing pool)
    ANGLE_TRY(allocateNewPool(context));
    bool success = allocateFromExistingPool(context, descriptorSetLayout, descriptorSetOut);
    // Allocate from a new pool must succeed.
    ASSERT(success);

    return angle::Result::Continue;
}

angle::Result DynamicDescriptorPool::getOrAllocateDescriptorSet(
    Context *context,
    uint32_t currentFrame,
    const DescriptorSetDesc &desc,
    const DescriptorSetLayout &descriptorSetLayout,
    DescriptorSetPointer *descriptorSetOut,
    SharedDescriptorSetCacheKey *newSharedCacheKeyOut)
{
    Renderer *renderer = context->getRenderer();
    ASSERT(context->getFeatures().descriptorSetCache.enabled);
    bool success;

    // If desc matches the most recently resolved entry, reuse it directly without hashing
    // desc or probing mDescriptorSetCache.
    if (!mLRUList.empty() && mLRUList.front().sharedCacheKey->getDesc() == desc)
    {
        *descriptorSetOut = mLRUList.front().descriptorSet;
        ASSERT(!(*newSharedCacheKeyOut));
        mCacheStats.hit();
        return angle::Result::Continue;
    }

    // Scan the descriptorSet cache.
    DescriptorSetLRUListIterator listIterator;
    if (mDescriptorSetCache.getDescriptorSet(desc, &listIterator))
    {
        *descriptorSetOut = listIterator->descriptorSet;
        (*newSharedCacheKeyOut).reset();
        // Move it to the front of the LRU list.
        mLRUList.splice(mLRUList.begin(), mLRUList, listIterator);
        mCacheStats.hit();
        return angle::Result::Continue;
    }

    // Try to allocate from the existing pool (or recycle from garbage list)
    success = allocateFromExistingPool(context, descriptorSetLayout, descriptorSetOut);

    // Try to recycle from the garbage list.
    if (!success)
    {
        success = recycleFromGarbage(context->getRenderer(), descriptorSetOut);
    }

    // Try to evict oldest descriptorSets that has not being used in last
    // kDescriptorSetCacheRetireAge.
    if (!success && currentFrame > kDescriptorSetCacheRetireAge)
    {
        uint32_t oldestFrameToKeep = currentFrame - kDescriptorSetCacheRetireAge;
        if (evictStaleDescriptorSets(renderer, oldestFrameToKeep, currentFrame))
        {
            success = recycleFromGarbage(renderer, descriptorSetOut);
        }
    }

    // Last, try to allocate a new pool
    if (!success)
    {
        ANGLE_TRY(allocateNewPool(context));
        success = allocateFromExistingPool(context, descriptorSetLayout, descriptorSetOut);
        // Allocate from a new pool must succeed.
        ASSERT(success);
    }

    ASSERT(descriptorSetOut->unique());
    ASSERT((*descriptorSetOut)->valid());

    // Let pool know there is a shared cache key created and destroys the shared cache key
    // when it destroys the pool.
    SharedDescriptorSetCacheKey sharedCacheKey = CreateSharedDescriptorSetCacheKey(desc, this);

    // Add to the front of the LRU list and add list iterator to the cache
    mLRUList.push_front({sharedCacheKey, *descriptorSetOut});
    mDescriptorSetCache.insertDescriptorSet(desc, mLRUList.begin());
    mCacheStats.missAndIncrementSize();

    *newSharedCacheKeyOut = sharedCacheKey;
    return angle::Result::Continue;
}

angle::Result DynamicDescriptorPool::allocateNewPool(ErrorContext *context)
{
    static constexpr size_t kMaxPools = 99999;
    ANGLE_VK_CHECK(context, mDescriptorPools.size() < kMaxPools, VK_ERROR_TOO_MANY_OBJECTS);
    // This pool is getting hot, so grow its max size to try and prevent allocating another pool in
    // the future.
    if (mMaxSetsPerPool < kMaxSetsPerPoolMax)
    {
        mMaxSetsPerPool *= mMaxSetsPerPoolMultiplier;
    }
    DescriptorPoolPointer newPool = DescriptorPoolPointer::MakeShared(context->getDevice());
    ANGLE_TRY(newPool->init(context, mPoolSizes, mMaxSetsPerPool));
    mDescriptorPools.emplace_back(std::move(newPool));

    return angle::Result::Continue;
}

void DynamicDescriptorPool::releaseCachedDescriptorSet(Renderer *renderer,
                                                       const DescriptorSetDesc &desc)
{
    ASSERT(renderer->getFeatures().descriptorSetCache.enabled);
    DescriptorSetLRUListIterator listIter;
    // Remove from the cache hash map. Note that we can't delete it until refcount goes to 0
    if (mDescriptorSetCache.eraseDescriptorSet(desc, &listIter))
    {
        DescriptorSetPointer descriptorSet = std::move(listIter->descriptorSet);
        mCacheStats.decrementSize();
        mLRUList.erase(listIter);

        if (descriptorSet.unique())
        {
            DescriptorPoolWeakPointer pool = descriptorSet->getPool();
            pool->addPendingGarbage(std::move(descriptorSet));
        }
    }
}

void DynamicDescriptorPool::destroyCachedDescriptorSet(Renderer *renderer,
                                                       const DescriptorSetDesc &desc)
{
    ASSERT(renderer->getFeatures().descriptorSetCache.enabled);
    DescriptorSetLRUListIterator listIter;
    // Remove from the cache hash map. Note that we can't delete it until refcount goes to 0
    if (mDescriptorSetCache.eraseDescriptorSet(desc, &listIter))
    {
        DescriptorSetPointer descriptorSet = std::move(listIter->descriptorSet);
        mCacheStats.decrementSize();
        mLRUList.erase(listIter);

        if (descriptorSet.unique())
        {
            DescriptorPoolWeakPointer pool = descriptorSet->getPool();
            pool->addFinishedGarbage(std::move(descriptorSet));
            if (pool->canDestroy())
            {
                destroyUnusedPool(renderer, pool);
            }
        }
    }
}

void DynamicDescriptorPool::destroyUnusedPool(Renderer *renderer,
                                              const DescriptorPoolWeakPointer &pool)
{
    ASSERT(renderer->getFeatures().descriptorSetCache.enabled);
    ASSERT(pool->canDestroy());

    // We always keep at least one pool around.
    if (mDescriptorPools.size() < 2)
    {
        return;
    }

    // Erase it from the array
    for (auto it = mDescriptorPools.begin(); it != mDescriptorPools.end(); ++it)
    {
        if (pool.owner_equal(*it))
        {
            ASSERT(pool->valid());
            pool->destroyGarbage();
            ASSERT((*it).unique());
            it = mDescriptorPools.erase(it);
            return;
        }
    }
}

void DynamicDescriptorPool::checkAndDestroyUnusedPool(Renderer *renderer)
{
    ASSERT(renderer->getFeatures().descriptorSetCache.enabled);
    for (const DescriptorPoolPointer &pool : mDescriptorPools)
    {
        pool->cleanupPendingGarbage();
    }

    // We always keep at least one pool around.
    if (mDescriptorPools.size() < 2)
    {
        return;
    }

    // Erase it from the array
    for (auto it = mDescriptorPools.begin(); it != mDescriptorPools.end();)
    {
        if ((*it)->canDestroy())
        {
            (*it)->destroyGarbage();
            ASSERT((*it).unique());
            it = mDescriptorPools.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// For ASSERT only
bool DynamicDescriptorPool::hasCachedDescriptorSet(const DescriptorSetDesc &desc) const
{
    DescriptorSetLRUListIterator listIterator;
    return mDescriptorSetCache.getDescriptorSet(desc, &listIterator);
}

// For testing only!
uint32_t DynamicDescriptorPool::GetMaxSetsPerPoolForTesting()
{
    return mMaxSetsPerPool;
}

// For testing only!
void DynamicDescriptorPool::SetMaxSetsPerPoolForTesting(uint32_t maxSetsPerPool)
{
    mMaxSetsPerPool = maxSetsPerPool;
}

// For testing only!
uint32_t DynamicDescriptorPool::GetMaxSetsPerPoolMultiplierForTesting()
{
    return mMaxSetsPerPoolMultiplier;
}

// For testing only!
void DynamicDescriptorPool::SetMaxSetsPerPoolMultiplierForTesting(uint32_t maxSetsPerPoolMultiplier)
{
    mMaxSetsPerPoolMultiplier = maxSetsPerPoolMultiplier;
}

// DynamicallyGrowingPool implementation
template <typename Pool>
DynamicallyGrowingPool<Pool>::DynamicallyGrowingPool()
    : mPoolSize(0), mCurrentPool(0), mCurrentFreeEntry(0)
{
    mPools.reserve(64);
}

template <typename Pool>
DynamicallyGrowingPool<Pool>::~DynamicallyGrowingPool() = default;

template <typename Pool>
angle::Result DynamicallyGrowingPool<Pool>::initEntryPool(ErrorContext *contextVk,
                                                          uint32_t poolSize)
{
    ASSERT(mPools.empty());
    mPoolSize         = poolSize;
    mCurrentFreeEntry = poolSize;
    return angle::Result::Continue;
}

template <typename Pool>
void DynamicallyGrowingPool<Pool>::destroyEntryPool(VkDevice device)
{
    for (PoolResource &resource : mPools)
    {
        destroyPoolImpl(device, resource.pool);
    }
    mPools.clear();
}

template <typename Pool>
bool DynamicallyGrowingPool<Pool>::findFreeEntryPool(ContextVk *contextVk)
{
    Renderer *renderer = contextVk->getRenderer();
    for (size_t poolIndex = 0; poolIndex < mPools.size(); ++poolIndex)
    {
        PoolResource &pool = mPools[poolIndex];
        if (pool.freedCount == mPoolSize && renderer->hasResourceUseFinished(pool.getResourceUse()))
        {
            mCurrentPool      = poolIndex;
            mCurrentFreeEntry = 0;

            pool.freedCount = 0;

            return true;
        }
    }

    return false;
}

template <typename Pool>
angle::Result DynamicallyGrowingPool<Pool>::allocateNewEntryPool(ContextVk *contextVk, Pool &&pool)
{
    mPools.emplace_back(std::move(pool), 0);

    mCurrentPool      = mPools.size() - 1;
    mCurrentFreeEntry = 0;

    return angle::Result::Continue;
}

template <typename Pool>
void DynamicallyGrowingPool<Pool>::onEntryFreed(ContextVk *contextVk,
                                                size_t poolIndex,
                                                const ResourceUse &use)
{
    ASSERT(poolIndex < mPools.size() && mPools[poolIndex].freedCount < mPoolSize);
    if (!contextVk->getRenderer()->hasResourceUseFinished(use))
    {
        mPools[poolIndex].mergeResourceUse(use);
    }
    ++mPools[poolIndex].freedCount;
}

template <typename Pool>
angle::Result DynamicallyGrowingPool<Pool>::allocatePoolEntries(ContextVk *contextVk,
                                                                uint32_t entryCount,
                                                                uint32_t *poolIndex,
                                                                uint32_t *currentEntryOut)
{
    if (mCurrentFreeEntry + entryCount > mPoolSize)
    {
        if (!findFreeEntryPool(contextVk))
        {
            Pool newPool;
            ANGLE_TRY(allocatePoolImpl(contextVk, newPool, mPoolSize));
            ANGLE_TRY(allocateNewEntryPool(contextVk, std::move(newPool)));
        }
    }

    *poolIndex       = static_cast<uint32_t>(mCurrentPool);
    *currentEntryOut = mCurrentFreeEntry;

    mCurrentFreeEntry += entryCount;

    return angle::Result::Continue;
}

template <typename Pool>
DynamicallyGrowingPool<Pool>::PoolResource::PoolResource(Pool &&poolIn, uint32_t freedCountIn)
    : pool(std::move(poolIn)), freedCount(freedCountIn)
{}

template <typename Pool>
DynamicallyGrowingPool<Pool>::PoolResource::PoolResource(PoolResource &&other)
    : Resource(std::move(other)), pool(std::move(other.pool)), freedCount(other.freedCount)
{}

// DynamicQueryPool implementation
DynamicQueryPool::DynamicQueryPool() = default;

DynamicQueryPool::~DynamicQueryPool() = default;

angle::Result DynamicQueryPool::init(ContextVk *contextVk, VkQueryType type, uint32_t poolSize)
{
    // SecondaryCommandBuffer's ResetQueryPoolParams would like the query index to fit in 24 bits.
    ASSERT(poolSize < (1 << 24));

    ANGLE_TRY(initEntryPool(contextVk, poolSize));
    mQueryType = type;
    return angle::Result::Continue;
}

void DynamicQueryPool::destroy(VkDevice device)
{
    destroyEntryPool(device);
}

void DynamicQueryPool::destroyPoolImpl(VkDevice device, QueryPool &poolToDestroy)
{
    poolToDestroy.destroy(device);
}

angle::Result DynamicQueryPool::allocateQuery(ContextVk *contextVk,
                                              QueryHelper *queryOut,
                                              uint32_t queryCount)
{
    ASSERT(!queryOut->valid());

    uint32_t currentPool = 0;
    uint32_t queryIndex  = 0;
    ANGLE_TRY(allocatePoolEntries(contextVk, queryCount, &currentPool, &queryIndex));

    queryOut->init(this, currentPool, queryIndex, queryCount);

    return angle::Result::Continue;
}

angle::Result DynamicQueryPool::allocatePoolImpl(ContextVk *contextVk,
                                                 QueryPool &poolToAllocate,
                                                 uint32_t entriesToAllocate)
{
    VkQueryPoolCreateInfo queryPoolInfo = {};
    queryPoolInfo.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.flags                 = 0;
    queryPoolInfo.queryType             = this->mQueryType;
    queryPoolInfo.queryCount            = entriesToAllocate;
    queryPoolInfo.pipelineStatistics    = 0;

    if (this->mQueryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        queryPoolInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
    }

    ANGLE_VK_TRY(contextVk, poolToAllocate.init(contextVk->getDevice(), queryPoolInfo));
    return angle::Result::Continue;
}

void DynamicQueryPool::freeQuery(ContextVk *contextVk, QueryHelper *query)
{
    if (query->valid())
    {
        size_t poolIndex = query->mQueryPoolIndex;
        ASSERT(getQueryPool(poolIndex).valid());

        onEntryFreed(contextVk, poolIndex, query->getResourceUse());

        query->deinit();
    }
}

// QueryResult implementation
void QueryResult::setResults(uint64_t *results, uint32_t queryCount)
{
    ASSERT(mResults[0] == 0 && mResults[1] == 0);

    // Accumulate the query results.  For multiview, where multiple query indices are used to return
    // the results, it's undefined how the results are distributed between indices, but the sum is
    // guaranteed to be the desired result.
    for (uint32_t query = 0; query < queryCount; ++query)
    {
        for (uint32_t perQueryIndex = 0; perQueryIndex < mIntsPerResult; ++perQueryIndex)
        {
            mResults[perQueryIndex] +=
                ANGLE_UNSAFE_TODO(results[query * mIntsPerResult + perQueryIndex]);
        }
    }
}

// QueryHelper implementation
QueryHelper::QueryHelper()
    : mDynamicQueryPool(nullptr),
      mQueryPoolIndex(0),
      mQuery(0),
      mQueryCount(0),
      mStatus(QueryStatus::Inactive)
{}

QueryHelper::~QueryHelper() {}

// Move constructor
QueryHelper::QueryHelper(QueryHelper &&rhs)
    : Resource(std::move(rhs)),
      mDynamicQueryPool(rhs.mDynamicQueryPool),
      mQueryPoolIndex(rhs.mQueryPoolIndex),
      mQuery(rhs.mQuery),
      mQueryCount(rhs.mQueryCount),
      mStatus(rhs.mStatus)
{
    rhs.mDynamicQueryPool = nullptr;
    rhs.mQueryPoolIndex   = 0;
    rhs.mQuery            = 0;
    rhs.mQueryCount       = 0;
    rhs.mStatus           = QueryStatus::Inactive;
}

QueryHelper &QueryHelper::operator=(QueryHelper &&rhs)
{
    Resource::operator=(std::move(rhs));
    std::swap(mDynamicQueryPool, rhs.mDynamicQueryPool);
    std::swap(mQueryPoolIndex, rhs.mQueryPoolIndex);
    std::swap(mQuery, rhs.mQuery);
    std::swap(mQueryCount, rhs.mQueryCount);
    std::swap(mStatus, rhs.mStatus);
    return *this;
}

void QueryHelper::init(const DynamicQueryPool *dynamicQueryPool,
                       const size_t queryPoolIndex,
                       uint32_t query,
                       uint32_t queryCount)
{
    mDynamicQueryPool = dynamicQueryPool;
    mQueryPoolIndex   = queryPoolIndex;
    mQuery            = query;
    mQueryCount       = queryCount;

    ASSERT(mQueryCount <= gl::IMPLEMENTATION_ANGLE_MULTIVIEW_MAX_VIEWS);
}

void QueryHelper::deinit()
{
    mDynamicQueryPool = nullptr;
    mQueryPoolIndex   = 0;
    mQuery            = 0;
    mQueryCount       = 0;
    mUse.reset();
    mStatus = QueryStatus::Inactive;
}

template <typename CommandBufferT>
void QueryHelper::beginQueryImpl(ContextVk *contextVk,
                                 OutsideRenderPassCommandBuffer *resetCommandBuffer,
                                 CommandBufferT *commandBuffer)
{
    ASSERT(mStatus != QueryStatus::Active);
    const QueryPool &queryPool = getQueryPool();
    resetQueryPoolImpl(contextVk, queryPool, resetCommandBuffer);
    commandBuffer->beginQuery(queryPool, mQuery, 0);
    mStatus = QueryStatus::Active;
}

template <typename CommandBufferT>
void QueryHelper::endQueryImpl(ContextVk *contextVk, CommandBufferT *commandBuffer)
{
    ASSERT(mStatus != QueryStatus::Ended);
    commandBuffer->endQuery(getQueryPool(), mQuery);
    mStatus = QueryStatus::Ended;
}

angle::Result QueryHelper::beginQuery(ContextVk *contextVk)
{
    if (contextVk->hasStartedRenderPass())
    {
        ANGLE_TRY(contextVk->flushCommandsAndEndRenderPass(
            RenderPassClosureReason::BeginNonRenderPassQuery));
    }

    OutsideRenderPassCommandBuffer *commandBuffer;
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer({}, &commandBuffer));

    ANGLE_TRY(contextVk->handleGraphicsEventLog(rx::GraphicsEventCmdBuf::InOutsideCmdBufQueryCmd));

    beginQueryImpl(contextVk, commandBuffer, commandBuffer);

    return angle::Result::Continue;
}

angle::Result QueryHelper::endQuery(ContextVk *contextVk)
{
    if (contextVk->hasStartedRenderPass())
    {
        ANGLE_TRY(contextVk->flushCommandsAndEndRenderPass(
            RenderPassClosureReason::EndNonRenderPassQuery));
    }

    CommandResources resources;
    OutsideRenderPassCommandBuffer *commandBuffer;
    resources.onQueryAccess(this);
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

    ANGLE_TRY(contextVk->handleGraphicsEventLog(rx::GraphicsEventCmdBuf::InOutsideCmdBufQueryCmd));

    endQueryImpl(contextVk, commandBuffer);

    return angle::Result::Continue;
}

template <typename CommandBufferT>
void QueryHelper::resetQueryPoolImpl(ContextVk *contextVk,
                                     const QueryPool &queryPool,
                                     CommandBufferT *commandBuffer)
{
    Renderer *renderer = contextVk->getRenderer();
    if (renderer->getFeatures().supportsHostQueryReset.enabled)
    {
        VK_CALL(vkResetQueryPoolEXT, contextVk->getDevice(), queryPool.getHandle(), mQuery,
                mQueryCount);
    }
    else
    {
        commandBuffer->resetQueryPool(queryPool, mQuery, mQueryCount);
    }
}

angle::Result QueryHelper::beginRenderPassQuery(ContextVk *contextVk)
{
    OutsideRenderPassCommandBuffer *outsideRenderPassCommandBuffer;
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer({}, &outsideRenderPassCommandBuffer));

    RenderPassCommandBuffer *renderPassCommandBuffer =
        &contextVk->getStartedRenderPassCommands().getCommandBuffer();

    beginQueryImpl(contextVk, outsideRenderPassCommandBuffer, renderPassCommandBuffer);

    return angle::Result::Continue;
}

void QueryHelper::endRenderPassQuery(ContextVk *contextVk)
{
    if (mStatus == QueryStatus::Active)
    {
        endQueryImpl(contextVk, &contextVk->getStartedRenderPassCommands().getCommandBuffer());
        contextVk->getStartedRenderPassCommands().retainResource(this);
    }
}

angle::Result QueryHelper::flushAndWriteTimestamp(ContextVk *contextVk)
{
    if (contextVk->hasStartedRenderPass())
    {
        ANGLE_TRY(
            contextVk->flushCommandsAndEndRenderPass(RenderPassClosureReason::TimestampQuery));
    }

    CommandResources resources;
    OutsideRenderPassCommandBuffer *commandBuffer;
    resources.onQueryAccess(this);
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));
    writeTimestamp(contextVk, commandBuffer);
    return angle::Result::Continue;
}

void QueryHelper::writeTimestampToPrimary(ContextVk *contextVk, PrimaryCommandBuffer *primary)
{
    // Note that commands may not be flushed at this point.

    const QueryPool &queryPool = getQueryPool();
    resetQueryPoolImpl(contextVk, queryPool, primary);
    primary->writeTimestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPool, mQuery);
}

void QueryHelper::writeTimestamp(ContextVk *contextVk,
                                 OutsideRenderPassCommandBuffer *commandBuffer)
{
    const QueryPool &queryPool = getQueryPool();
    resetQueryPoolImpl(contextVk, queryPool, commandBuffer);
    commandBuffer->writeTimestamp(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPool, mQuery);
}

bool QueryHelper::hasSubmittedCommands() const
{
    return mUse.valid();
}

angle::Result QueryHelper::getUint64ResultNonBlocking(ContextVk *contextVk,
                                                      QueryResult *resultOut,
                                                      bool *availableOut)
{
    ASSERT(valid());
    VkResult result;

    // Ensure that we only wait if we have inserted a query in command buffer. Otherwise you will
    // wait forever and trigger GPU timeout.
    if (hasSubmittedCommands())
    {
        constexpr VkQueryResultFlags kFlags = VK_QUERY_RESULT_64_BIT;
        result                              = getResultImpl(contextVk, kFlags, resultOut);
    }
    else
    {
        result     = VK_SUCCESS;
        *resultOut = 0;
    }

    if (result == VK_NOT_READY)
    {
        *availableOut = false;
        return angle::Result::Continue;
    }
    else
    {
        ANGLE_VK_TRY(contextVk, result);
        *availableOut = true;
    }
    return angle::Result::Continue;
}

angle::Result QueryHelper::getUint64Result(ContextVk *contextVk, QueryResult *resultOut)
{
    ASSERT(valid());
    if (hasSubmittedCommands())
    {
        constexpr VkQueryResultFlags kFlags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT;
        ANGLE_VK_TRY(contextVk, getResultImpl(contextVk, kFlags, resultOut));
    }
    else
    {
        *resultOut = 0;
    }
    return angle::Result::Continue;
}

VkResult QueryHelper::getResultImpl(ContextVk *contextVk,
                                    const VkQueryResultFlags flags,
                                    QueryResult *resultOut)
{
    std::array<uint64_t, 2 * gl::IMPLEMENTATION_ANGLE_MULTIVIEW_MAX_VIEWS> results;

    VkDevice device = contextVk->getDevice();
    VkResult result = getQueryPool().getResults(device, mQuery, mQueryCount, sizeof(results),
                                                results.data(), resultOut->getDataSize(), flags);

    if (result == VK_SUCCESS)
    {
        resultOut->setResults(results.data(), mQueryCount);
    }

    return result;
}

// SemaphoreHelper implementation
SemaphoreHelper::SemaphoreHelper() : mSemaphorePoolIndex(0), mSemaphore(0) {}

SemaphoreHelper::~SemaphoreHelper() {}

SemaphoreHelper::SemaphoreHelper(SemaphoreHelper &&other)
    : mSemaphorePoolIndex(other.mSemaphorePoolIndex), mSemaphore(other.mSemaphore)
{
    other.mSemaphore = nullptr;
}

SemaphoreHelper &SemaphoreHelper::operator=(SemaphoreHelper &&other)
{
    std::swap(mSemaphorePoolIndex, other.mSemaphorePoolIndex);
    std::swap(mSemaphore, other.mSemaphore);
    return *this;
}

void SemaphoreHelper::init(const size_t semaphorePoolIndex, const Semaphore *semaphore)
{
    mSemaphorePoolIndex = semaphorePoolIndex;
    mSemaphore          = semaphore;
}

void SemaphoreHelper::deinit()
{
    mSemaphorePoolIndex = 0;
    mSemaphore          = nullptr;
}

// PipelineBarrier implementation.
void PipelineBarrier::addDiagnosticsString(std::ostringstream &out) const
{
    if (mMemoryBarrierSrcAccess != 0 || mMemoryBarrierDstAccess != 0)
    {
        out << "Src: 0x" << std::hex << mMemoryBarrierSrcAccess << " &rarr; Dst: 0x" << std::hex
            << mMemoryBarrierDstAccess << std::endl;
    }
}

// PipelineBarrierArray implementation.
void PipelineBarrierArray::execute(Renderer *renderer, PrimaryCommandBuffer *primary)
{
    // make a local copy for faster access
    PipelineStagesMask mask = mBarrierMask;
    if (mask.none())
    {
        return;
    }

    if (renderer->getFeatures().preferAggregateBarrierCalls.enabled)
    {
        PipelineStagesMask::Iterator iter = mask.begin();
        PipelineBarrier &barrier          = mBarriers[*iter];
        for (++iter; iter != mask.end(); ++iter)
        {
            barrier.merge(&mBarriers[*iter]);
        }
        barrier.execute(primary);
    }
    else
    {
        for (PipelineStage pipelineStage : mask)
        {
            PipelineBarrier &barrier = mBarriers[pipelineStage];
            barrier.execute(primary);
        }
    }
    mBarrierMask.reset();
}

void PipelineBarrierArray::addDiagnosticsString(std::ostringstream &out) const
{
    std::ostringstream barrierStream;
    barrierStream << "Memory Barrier: ";
    bool haveBarrierLog = false;
    for (PipelineStage pipelineStage : mBarrierMask)
    {
        const PipelineBarrier &barrier = mBarriers[pipelineStage];
        if (!barrier.isEmpty())
        {
            haveBarrierLog = true;
            barrier.addDiagnosticsString(barrierStream);
        }
    }
    if (haveBarrierLog)
    {
        out << barrierStream.str() << "\\l";
    }
}

// BufferHelper implementation.
BufferHelper::BufferHelper()
    : mCurrentWriteAccess(0),
      mCurrentReadAccess(0),
      mCurrentWriteStages(0),
      mCurrentReadStages(0),
      mSerial(),
      mClientBuffer(nullptr),
      mIsReleasedToExternal(false)
{}

BufferHelper::~BufferHelper()
{
    // We must have released external buffer properly
    ASSERT(mClientBuffer == nullptr);
}

BufferHelper::BufferHelper(BufferHelper &&other)
{
    *this = std::move(other);
}

BufferHelper &BufferHelper::operator=(BufferHelper &&other)
{
    ReadWriteResource::operator=(std::move(other));

    mSuballocation      = std::move(other.mSuballocation);
    mBufferWithUserSize = std::move(other.mBufferWithUserSize);

    mCurrentDeviceQueueIndex = other.mCurrentDeviceQueueIndex;
    mIsReleasedToExternal    = other.mIsReleasedToExternal;
    mCurrentWriteAccess      = other.mCurrentWriteAccess;
    mCurrentReadAccess       = other.mCurrentReadAccess;
    mCurrentWriteStages      = other.mCurrentWriteStages;
    mCurrentReadStages       = other.mCurrentReadStages;
    if (other.mCurrentWriteEvent.valid())
    {
        mCurrentWriteEvent = std::move(other.mCurrentWriteEvent);
    }
    if (!other.mCurrentReadEvents.empty())
    {
        mCurrentReadEvents = std::move(other.mCurrentReadEvents);
    }
    mXFBOrComputeWriteHeuristicBits = std::move(other.mXFBOrComputeWriteHeuristicBits);
    mSerial                         = other.mSerial;

    // BufferHelper is usually std::move()'ed when the GL buffer backing is internally recreated as
    // an optimization, or for other internal buffer allocation reasons.  It should never happen for
    // buffers that are backed by external memory.
    ASSERT(other.mClientBuffer == nullptr);
    mClientBuffer = nullptr;

    return *this;
}

angle::Result BufferHelper::init(ErrorContext *context,
                                 const VkBufferCreateInfo &requestedCreateInfo,
                                 VkMemoryPropertyFlags memoryPropertyFlags)
{
    Renderer *renderer         = context->getRenderer();
    const Allocator &allocator = renderer->getAllocator();

    initializeBarrierTracker(context);

    VkBufferCreateInfo modifiedCreateInfo;
    const VkBufferCreateInfo *createInfo = &requestedCreateInfo;

    modifiedCreateInfo      = requestedCreateInfo;
    modifiedCreateInfo.size = renderer->padVertexAttribBufferSizeIfNeeded(requestedCreateInfo.size);
    createInfo              = &modifiedCreateInfo;

    VkMemoryPropertyFlags requiredFlags =
        (memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    VkMemoryPropertyFlags preferredFlags =
        (memoryPropertyFlags & (~VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

    bool persistentlyMapped = renderer->getFeatures().persistentlyMappedBuffers.enabled;

    // Check that the allocation is not too large.
    uint32_t memoryTypeIndex = kInvalidMemoryTypeIndex;
    ANGLE_VK_TRY(context, allocator.findMemoryTypeIndexForBufferInfo(
                              *createInfo, requiredFlags, preferredFlags, persistentlyMapped,
                              &memoryTypeIndex));

    VkDeviceSize heapSize =
        renderer->getMemoryProperties().getHeapSizeForMemoryType(memoryTypeIndex);

    ANGLE_VK_CHECK(context, createInfo->size <= heapSize, VK_ERROR_OUT_OF_DEVICE_MEMORY);

    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    allocator.getMemoryTypeProperties(memoryTypeIndex, &memoryPropertyFlagsOut);
    // Allocate buffer object
    DeviceScoped<Buffer> buffer(renderer->getDevice());
    ANGLE_VK_TRY(context, buffer.get().init(context->getDevice(), *createInfo));

    DeviceScoped<DeviceMemory> deviceMemory(renderer->getDevice());
    VkDeviceSize sizeOut;
    uint32_t bufferMemoryTypeIndex;

    const void *extraAllocationInfoPtr = context->getFeatures().supportsBufferDeviceAddress.enabled
                                             ? &kMemoryAllocateDeviceAddressFlags
                                             : nullptr;

    ANGLE_VK_TRY(context, AllocateBufferMemory(
                              context, MemoryAllocationType::Buffer, memoryPropertyFlagsOut,
                              &memoryPropertyFlagsOut, extraAllocationInfoPtr, &buffer.get(),
                              &bufferMemoryTypeIndex, &deviceMemory.get(), &sizeOut));

    ASSERT(sizeOut >= createInfo->size);

    mSuballocation.initWithEntireBuffer(context, buffer.get(), MemoryAllocationType::Buffer,
                                        bufferMemoryTypeIndex, deviceMemory.get(),
                                        memoryPropertyFlagsOut, requestedCreateInfo.size, sizeOut);
    if (isHostVisible())
    {
        uint8_t *ptrOut;
        ANGLE_TRY(map(context, &ptrOut));
    }

    if (renderer->getFeatures().allocateNonZeroMemory.enabled)
    {
        ANGLE_TRY(initializeNonZeroMemory(context, createInfo->usage, sizeOut));
    }

    return angle::Result::Continue;
}

angle::Result BufferHelper::initExternal(ErrorContext *context,
                                         VkMemoryPropertyFlags memoryProperties,
                                         const VkBufferCreateInfo &requestedCreateInfo,
                                         GLeglClientBufferEXT clientBuffer)
{
    ASSERT(IsAndroid());

    Renderer *renderer = context->getRenderer();

    initializeBarrierTracker(context);

    VkBufferCreateInfo modifiedCreateInfo             = requestedCreateInfo;
    VkExternalMemoryBufferCreateInfo externCreateInfo = {};
    externCreateInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externCreateInfo.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    externCreateInfo.pNext   = nullptr;
    modifiedCreateInfo.pNext = &externCreateInfo;

    DeviceScoped<Buffer> buffer(renderer->getDevice());
    ANGLE_VK_TRY(context, buffer.get().init(renderer->getDevice(), modifiedCreateInfo));

    DeviceScoped<DeviceMemory> deviceMemory(renderer->getDevice());
    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    VkDeviceSize allocatedSize = 0;
    uint32_t memoryTypeIndex;
    ANGLE_TRY(InitAndroidExternalMemory(context, clientBuffer, memoryProperties, &buffer.get(),
                                        &memoryPropertyFlagsOut, &memoryTypeIndex,
                                        &deviceMemory.get(), &allocatedSize));
    mClientBuffer = clientBuffer;

    mSuballocation.initWithEntireBuffer(context, buffer.get(), MemoryAllocationType::BufferExternal,
                                        memoryTypeIndex, deviceMemory.get(), memoryPropertyFlagsOut,
                                        requestedCreateInfo.size, allocatedSize);
    if (isHostVisible())
    {
        uint8_t *ptrOut;
        ANGLE_TRY(map(context, &ptrOut));
    }
    return angle::Result::Continue;
}

angle::Result BufferHelper::initAndAcquireFromExternalMemory(
    Context *context,
    VkMemoryPropertyFlags memoryProperties,
    const VkBufferCreateInfo &requestedCreateInfo,
    const VkExternalMemoryHandleTypeFlagBits externalMemoryHandleType,
    const int32_t sharedBufferFD)
{
    Renderer *renderer = context->getRenderer();
    ASSERT(renderer);

    initializeBarrierTracker(context);

    VkBufferCreateInfo modifiedCreateInfo             = requestedCreateInfo;
    VkExternalMemoryBufferCreateInfo externCreateInfo = {};
    externCreateInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externCreateInfo.handleTypes = externalMemoryHandleType;
    externCreateInfo.pNext       = nullptr;
    modifiedCreateInfo.pNext     = &externCreateInfo;

    DeviceScoped<Buffer> buffer(renderer->getDevice());
    ANGLE_VK_TRY(context, buffer.get().init(renderer->getDevice(), modifiedCreateInfo));

    DeviceScoped<DeviceMemory> deviceMemory(renderer->getDevice());
    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    VkDeviceSize allocatedSize = 0;
    uint32_t memoryTypeIndex;
    ANGLE_TRY(InitExternalSharedFDMemory(context, externalMemoryHandleType, sharedBufferFD,
                                         memoryProperties, &buffer.get(), &memoryPropertyFlagsOut,
                                         &memoryTypeIndex, &deviceMemory.get(), &allocatedSize));

    mSuballocation.initWithEntireBuffer(context, buffer.get(), MemoryAllocationType::BufferExternal,
                                        memoryTypeIndex, deviceMemory.get(), memoryPropertyFlagsOut,
                                        requestedCreateInfo.size, allocatedSize);
    if (isHostVisible())
    {
        uint8_t *ptrOut;
        ANGLE_TRY(map(context, &ptrOut));
    }
    return angle::Result::Continue;
}

angle::Result BufferHelper::initHostExternal(ErrorContext *context,
                                             VkMemoryPropertyFlags memoryProperties,
                                             const VkBufferCreateInfo &requestedCreateInfo,
                                             void *hostPtr)
{
    Renderer *renderer = context->getRenderer();

    initializeBarrierTracker(context);

    VkBufferCreateInfo modifiedCreateInfo             = requestedCreateInfo;
    VkExternalMemoryBufferCreateInfo externCreateInfo = {};
    externCreateInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    externCreateInfo.pNext       = nullptr;
    modifiedCreateInfo.pNext     = &externCreateInfo;

    DeviceScoped<Buffer> buffer(renderer->getDevice());
    ANGLE_VK_TRY(context, buffer.get().init(renderer->getDevice(), modifiedCreateInfo));

    DeviceScoped<DeviceMemory> deviceMemory(renderer->getDevice());
    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    VkDeviceSize allocatedSize = 0;
    uint32_t memoryTypeIndex   = UINT32_MAX;

    ANGLE_TRY(InitExternalHostMemory(context, hostPtr, memoryProperties, &buffer.get(),
                                     &memoryPropertyFlagsOut, &memoryTypeIndex, &deviceMemory.get(),
                                     &allocatedSize));

    mSuballocation.initWithEntireBuffer(context, buffer.get(), MemoryAllocationType::BufferExternal,
                                        memoryTypeIndex, deviceMemory.get(), memoryPropertyFlagsOut,
                                        requestedCreateInfo.size, allocatedSize);

    return angle::Result::Continue;
}

VkResult BufferHelper::initSuballocation(Context *context,
                                         uint32_t memoryTypeIndex,
                                         size_t size,
                                         size_t alignment,
                                         BufferUsageType usageType,
                                         BufferPool *pool)
{
    ASSERT(pool != nullptr);
    ASSERT(size != 0);
    Renderer *renderer = context->getRenderer();

    // We should reset these in case the BufferHelper object has been released and called
    // initSuballocation again.
    initializeBarrierTracker(context);

    size = static_cast<size_t>(renderer->padVertexAttribBufferSizeIfNeeded(size));
    VK_RESULT_TRY(pool->allocateBuffer(context, size, alignment, &mSuballocation));

    context->getPerfCounters().bufferSuballocationCalls++;

    return VK_SUCCESS;
}

void BufferHelper::initializeBarrierTracker(ErrorContext *context)
{
    Renderer *renderer       = context->getRenderer();
    mCurrentDeviceQueueIndex = context->getDeviceQueueIndex();
    mIsReleasedToExternal    = false;
    mCurrentWriteEvent.release(renderer);
    mCurrentReadEvents.release(renderer);
    mSerial             = renderer->getResourceSerialFactory().generateBufferSerial();
    mCurrentWriteAccess = 0;
    mCurrentReadAccess  = 0;
    mCurrentWriteStages = 0;
    mCurrentReadStages  = 0;
}

angle::Result BufferHelper::initializeRobustMemory(ErrorContext *context,
                                                   VkBufferUsageFlags usage,
                                                   VkDeviceSize size)
{
    constexpr int kInitZeroValue = 0;
    return initializeMemoryWithValueImpl(context, usage, size, kInitZeroValue);
}

angle::Result BufferHelper::initializeNonZeroMemory(ErrorContext *context,
                                                    VkBufferUsageFlags usage,
                                                    VkDeviceSize size)
{
    constexpr int kInitNonZeroValue = 55;
    return initializeMemoryWithValueImpl(context, usage, size, kInitNonZeroValue);
}

angle::Result BufferHelper::initializeMemoryWithValueImpl(ErrorContext *context,
                                                          VkBufferUsageFlags usage,
                                                          VkDeviceSize size,
                                                          const int value)
{
    Renderer *renderer = context->getRenderer();

    // This memory can't be mapped, so the buffer must be marked as a transfer destination so we
    // can use a staging resource to initialize it to a non-zero value. If the memory is
    // mappable we do the initialization in AllocateBufferMemory.
    if (!isHostVisible() && (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0)
    {
        ASSERT((usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0);
        StagingBuffer stagingBuffer;
        ANGLE_TRY(stagingBuffer.init(context, size, StagingUsage::Both, value));

        // Queue a DMA copy.
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset    = 0;
        copyRegion.dstOffset    = getOffset();
        copyRegion.size         = size;

        ScopedPrimaryCommandBuffer scopedCommandBuffer(renderer->getDevice());
        ANGLE_TRY(renderer->getCommandBufferOneOff(context, ProtectionType::Unprotected,
                                                   &scopedCommandBuffer));
        PrimaryCommandBuffer &commandBuffer = scopedCommandBuffer.get();

        commandBuffer.copyBuffer(stagingBuffer.getBuffer(), getBuffer(), 1, &copyRegion);

        renderer->insertSubmitDebugMarkerInCommandBuffer(commandBuffer,
                                                         QueueSubmitReason::InitializeMemory);
        ANGLE_VK_TRY(context, commandBuffer.end());

        QueueSerial queueSerial;
        ANGLE_TRY(renderer->queueSubmitOneOff(
            context, std::move(scopedCommandBuffer), ProtectionType::Unprotected,
            egl::ContextPriority::Medium, VK_NULL_HANDLE, 0, &queueSerial));

        stagingBuffer.collectGarbage(renderer, queueSerial);
        // Update both ResourceUse objects, since mReadOnlyUse tracks when the buffer can be
        // destroyed, and mReadWriteUse tracks when the write has completed.
        setWriteQueueSerial(queueSerial);
    }
    else if (isHostVisible())
    {
        // Can map the memory to initialize non-zero memory for sanitization.
        uint8_t *mapPointer = mSuballocation.getMappedMemory();
        ANGLE_UNSAFE_TODO(memset(mapPointer, value, static_cast<size_t>(size)));
        if (!isCoherent())
        {
            mSuballocation.flush(renderer);
        }
    }

    return angle::Result::Continue;
}

const Buffer &BufferHelper::getBufferForVertexArray(ContextVk *contextVk,
                                                    VkDeviceSize actualDataSize,
                                                    VkDeviceSize *offsetOut)
{
    ASSERT(!contextVk->getFeatures().supportsBindVertexBuffers2.enabled);
    return getBufferForVertexArrayImpl(contextVk, actualDataSize, offsetOut);
}

const Buffer &BufferHelper::getIndexBufferForVertexArray(ContextVk *contextVk,
                                                         VkDeviceSize actualDataSize,
                                                         VkDeviceSize *offsetOut)
{
    ASSERT(!contextVk->getFeatures().supportsMaintenance5.enabled);
    return getBufferForVertexArrayImpl(contextVk, actualDataSize, offsetOut);
}

const Buffer &BufferHelper::getBufferForVertexArrayImpl(ContextVk *contextVk,
                                                        VkDeviceSize actualDataSize,
                                                        VkDeviceSize *offsetOut)
{
    ASSERT(mSuballocation.valid());
    ASSERT(actualDataSize <= mSuballocation.getSize());

    if (!contextVk->hasRobustAccess() || !mSuballocation.isSuballocated() ||
        actualDataSize == mSuballocation.getSize())
    {
        *offsetOut = mSuballocation.getOffset();
        return mSuballocation.getBuffer();
    }

    if (!mBufferWithUserSize.valid())
    {
        // Allocate buffer that is backed by sub-range of the memory for vertex array usage. This is
        // only needed when robust resource init is enabled so that vulkan driver will know the
        // exact size of the vertex buffer it is supposedly to use and prevent out of bound access.
        VkBufferCreateInfo createInfo    = {};
        createInfo.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.flags                 = 0;
        createInfo.size                  = actualDataSize;
        createInfo.usage                 = kVertexBufferUsageFlags | kIndexBufferUsageFlags;
        createInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices   = nullptr;
        mBufferWithUserSize.init(contextVk->getDevice(), createInfo);

        VkMemoryRequirements memoryRequirements;
        mBufferWithUserSize.getMemoryRequirements(contextVk->getDevice(), &memoryRequirements);
        ASSERT(contextVk->getRenderer()->isMockICDEnabled() ||
               mSuballocation.getSize() >= memoryRequirements.size);
        ASSERT(!contextVk->getRenderer()->isMockICDEnabled() ||
               mSuballocation.getOffset() % memoryRequirements.alignment == 0);

        mBufferWithUserSize.bindMemory(contextVk->getDevice(), mSuballocation.getDeviceMemory(),
                                       mSuballocation.getOffset());
    }
    *offsetOut = 0;
    return mBufferWithUserSize;
}

bool BufferHelper::onBufferUserSizeChange(Renderer *renderer)
{
    // Buffer's user size and allocation size may be different due to alignment requirement. In
    // normal usage we just use the actual allocation size and it is good enough. But when
    // robustResourceInit is enabled, mBufferWithUserSize is created to mjatch the exact user
    // size. Thus when user size changes, we must clear and recreate this mBufferWithUserSize.
    if (mBufferWithUserSize.valid())
    {
        BufferSuballocation unusedSuballocation;
        renderer->collectSuballocationGarbage(mUse, std::move(unusedSuballocation),
                                              std::move(mBufferWithUserSize));
        mSerial = renderer->getResourceSerialFactory().generateBufferSerial();
        return true;
    }
    return false;
}

void BufferHelper::destroy(Renderer *renderer)
{
    mCurrentWriteEvent.release(renderer);
    mCurrentReadEvents.release(renderer);
    ASSERT(mDescriptorSetCacheManager.allValidEntriesAreCached(nullptr));
    mDescriptorSetCacheManager.destroyKeys(renderer);
    unmap(renderer);
    mBufferWithUserSize.destroy(renderer->getDevice());
    mSuballocation.destroy(renderer);
    if (mClientBuffer != nullptr)
    {
        ReleaseAndroidExternalMemory(renderer, mClientBuffer);
        mClientBuffer = nullptr;
    }
}

void BufferHelper::release(Renderer *renderer)
{
    mCurrentWriteEvent.release(renderer);
    mCurrentReadEvents.release(renderer);
    releaseImpl(renderer);
}

void BufferHelper::release(Context *context)
{
    mCurrentWriteEvent.release(context);
    mCurrentReadEvents.release(context);
    releaseImpl(context->getRenderer());
}

void BufferHelper::releaseImpl(Renderer *renderer)
{
    ASSERT(mDescriptorSetCacheManager.empty());
    unmap(renderer);

    if (mSuballocation.valid())
    {
        renderer->collectSuballocationGarbage(mUse, std::move(mSuballocation),
                                              std::move(mBufferWithUserSize));
    }
    mUse.reset();
    mWriteUse.reset();
    ASSERT(!mBufferWithUserSize.valid());

    if (mClientBuffer != nullptr)
    {
        ReleaseAndroidExternalMemory(renderer, mClientBuffer);
        mClientBuffer = nullptr;
    }
}

void BufferHelper::releaseBufferAndDescriptorSetCache(ContextVk *contextVk)
{
    Renderer *renderer = contextVk->getRenderer();

    ASSERT(mDescriptorSetCacheManager.allValidEntriesAreCached(contextVk));
    if (renderer->hasResourceUseFinished(getResourceUse()))
    {
        mDescriptorSetCacheManager.destroyKeys(renderer);
    }
    else
    {
        mDescriptorSetCacheManager.releaseKeys(renderer);
    }

    release(contextVk);
}

angle::Result BufferHelper::map(ErrorContext *context, uint8_t **ptrOut)
{
    if (!mSuballocation.isMapped())
    {
        ANGLE_VK_TRY(context, mSuballocation.map(context));
    }
    *ptrOut = mSuballocation.getMappedMemory();
    return angle::Result::Continue;
}

angle::Result BufferHelper::mapWithOffset(ErrorContext *context, uint8_t **ptrOut, size_t offset)
{
    uint8_t *mapBufPointer;
    ANGLE_TRY(map(context, &mapBufPointer));
    *ptrOut = ANGLE_UNSAFE_TODO(mapBufPointer + offset);
    return angle::Result::Continue;
}

angle::Result BufferHelper::flush(Renderer *renderer, VkDeviceSize offset, VkDeviceSize size)
{
    mSuballocation.flush(renderer);
    return angle::Result::Continue;
}
angle::Result BufferHelper::flush(Renderer *renderer)
{
    return flush(renderer, 0, getSize());
}

angle::Result BufferHelper::invalidate(Renderer *renderer, VkDeviceSize offset, VkDeviceSize size)
{
    mSuballocation.invalidate(renderer);
    return angle::Result::Continue;
}
angle::Result BufferHelper::invalidate(Renderer *renderer)
{
    return invalidate(renderer, 0, getSize());
}

void BufferHelper::changeQueueFamily(uint32_t srcQueueFamilyIndex,
                                     uint32_t dstQueueFamilyIndex,
                                     OutsideRenderPassCommandBuffer *commandBuffer)
{
    VkBufferMemoryBarrier bufferMemoryBarrier = {};
    bufferMemoryBarrier.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferMemoryBarrier.srcAccessMask         = 0;
    bufferMemoryBarrier.dstAccessMask         = 0;
    bufferMemoryBarrier.srcQueueFamilyIndex   = srcQueueFamilyIndex;
    bufferMemoryBarrier.dstQueueFamilyIndex   = dstQueueFamilyIndex;
    bufferMemoryBarrier.buffer                = getBuffer().getHandle();
    bufferMemoryBarrier.offset                = getOffset();
    bufferMemoryBarrier.size                  = getSize();

    commandBuffer->bufferBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, &bufferMemoryBarrier);
}

void BufferHelper::acquireFromExternal(DeviceQueueIndex externalQueueFamilyIndex,
                                       DeviceQueueIndex newDeviceQueueIndex,
                                       OutsideRenderPassCommandBuffer *commandBuffer)
{
    changeQueueFamily(externalQueueFamilyIndex.familyIndex(), newDeviceQueueIndex.familyIndex(),
                      commandBuffer);
    mCurrentDeviceQueueIndex = newDeviceQueueIndex;
    mIsReleasedToExternal    = false;
}

void BufferHelper::releaseToExternal(DeviceQueueIndex externalQueueIndex,
                                     OutsideRenderPassCommandBuffer *commandBuffer)
{
    if (mCurrentDeviceQueueIndex.familyIndex() != externalQueueIndex.familyIndex())
    {
        changeQueueFamily(mCurrentDeviceQueueIndex.familyIndex(), externalQueueIndex.familyIndex(),
                          commandBuffer);
        mCurrentDeviceQueueIndex = kInvalidDeviceQueueIndex;
    }
    mIsReleasedToExternal = true;
}

void BufferHelper::recordReadBarrier(Context *context,
                                     VkAccessFlags readAccessType,
                                     VkPipelineStageFlags readPipelineStageFlags,
                                     PipelineStage stageIndex,
                                     PipelineBarrierArray *pipelineBarriers,
                                     EventBarrierArray *eventBarriers,
                                     RefCountedEventCollector *eventCollector)
{
    // If the type of read already tracked by mCurrentReadEvents, it means we must already inserted
    // the barrier when mCurrentReadEvents is set. No new barrier is needed.
    EventStage eventStage = GetBufferMemoryBarrierData(stageIndex).eventStage;
    if (mCurrentReadEvents.hasEventAndAccess(eventStage, readAccessType))
    {
        ASSERT((context->getRenderer()->getPipelineStageMask(eventStage) &
                readPipelineStageFlags) == readPipelineStageFlags);
        ASSERT((mCurrentReadEvents.getAccessFlags(eventStage) & readAccessType) == readAccessType);
        return;
    }

    // If the type of read already tracked by mCurrentReadAccess, it means we must already inserted
    // the barrier when mCurrentReadAccess is set. No new barrier is needed.
    if ((mCurrentReadAccess & readAccessType) == readAccessType &&
        (mCurrentReadStages & readPipelineStageFlags) == readPipelineStageFlags)
    {
        return;
    }

    // Barrier against prior write VkEvent.
    if (mCurrentWriteEvent.valid())
    {
        eventBarriers->addEventMemoryBarrier(context->getRenderer(), mCurrentWriteEvent.getEvent(),
                                             mCurrentWriteEvent.getAccessFlags(),
                                             readPipelineStageFlags, readAccessType);
        eventCollector->emplace_back(mCurrentWriteEvent.getEvent());
    }

    // Barrier against prior access that not tracked by VkEvent using pipelineBarrier.
    if (mCurrentWriteAccess != 0)
    {
        pipelineBarriers->mergeMemoryBarrier(stageIndex, mCurrentWriteStages,
                                             readPipelineStageFlags, mCurrentWriteAccess,
                                             readAccessType);
    }
}

void BufferHelper::recordReadEvent(Context *context,
                                   VkAccessFlags readAccessType,
                                   VkPipelineStageFlags readPipelineStageFlags,
                                   PipelineStage readStage,
                                   const QueueSerial &queueSerial,
                                   EventStage eventStage,
                                   RefCountedEventArray *refCountedEventArray)
{
    bool useVkEvent = false;
    if (context->getFeatures().useVkEventForBufferBarrier.enabled &&
        eventStage != EventStage::InvalidEnum)
    {
        // VkCmdSetEvent can remove the unnecessary GPU pipeline bubble that comes from false
        // dependency between fragment and vertex/transfer/compute stages. But it also comes with
        // higher overhead. In order to strike the balance, right now we only track it with VkEvent
        // if it ever written by transform feedback.
        useVkEvent = mXFBOrComputeWriteHeuristicBits.any();
    }

    if (useVkEvent && refCountedEventArray->initEventAtStage(context, eventStage))
    {
        // Replace the mCurrentReadEvents so that it tracks the current read so that we can
        // waitEvent later.
        mCurrentReadEvents.replaceEventAtStage(
            context, eventStage, refCountedEventArray->getEvent(eventStage), readAccessType);
    }
    else
    {
        // Accumulate new read usage to be used in pipelineBarrier.
        mCurrentReadAccess |= readAccessType;
        mCurrentReadStages |= readPipelineStageFlags;
    }

    if (getResourceUse() >= queueSerial)
    {
        // We should not run into situation that RP is writing to it while we are reading it here
        ASSERT(!(getWriteResourceUse() >= queueSerial));
        // A buffer could have read accessed by both renderPassCommands and
        // outsideRenderPassCommands and there is no need to endRP or flush. In this case, the
        // renderPassCommands' read will override the outsideRenderPassCommands' read, since its
        // queueSerial must be greater than outsideRP.
    }
    else
    {
        setQueueSerial(queueSerial);
    }
}

void BufferHelper::recordWriteBarrier(Context *context,
                                      VkAccessFlags writeAccessType,
                                      VkPipelineStageFlags writeStage,
                                      PipelineStage stageIndex,
                                      const QueueSerial &queueSerial,
                                      PipelineBarrierArray *pipelineBarriers,
                                      EventBarrierArray *eventBarriers,
                                      RefCountedEventCollector *eventCollector)
{
    Renderer *renderer = context->getRenderer();

    updatePipelineStageWriteHistory(context, stageIndex);

    // Barrier against prior read VkEvents.
    if (!mCurrentReadEvents.empty())
    {
        // If we already have a event in the same command buffer, fall back to pipeline. Otherwise
        // you may run into wait an event that has not been set. This may be can be removed once we
        // fix https://issuetracker.google.com/392968868
        if (usedByCommandBuffer(queueSerial))
        {
            for (EventStage eventStage : mCurrentReadEvents.getBitMask())
            {
                mCurrentReadStages |= renderer->getPipelineStageMask(eventStage);
                mCurrentReadAccess |= mCurrentReadEvents.getAccessFlags(eventStage);
            }
        }
        else
        {
            for (EventStage eventStage : mCurrentReadEvents.getBitMask())
            {
                const RefCountedEvent &waitEvent = mCurrentReadEvents.getEvent(eventStage);
                const VkAccessFlags srcAccess    = mCurrentReadEvents.getAccessFlags(eventStage);
                eventBarriers->addEventMemoryBarrier(renderer, waitEvent, srcAccess, writeStage,
                                                     writeAccessType);
            }
        }
        // Garbage collect the event, which tracks GPU completion automatically.
        mCurrentReadEvents.releaseToEventCollector(eventCollector);
    }

    // Barrier against prior write VkEvent.
    if (mCurrentWriteEvent.valid())
    {
        const VkPipelineStageFlags srcStageFlags =
            renderer->getPipelineStageMask(mCurrentWriteEvent.getEventStage());

        // If we already have a write event in the same command buffer, fall back to pipeline
        // barrier. Using VkEvent to track multiple writes either requires tracking multiple write
        // events or has to replace existing event with another event that tracks more pipeline
        // stage bits. Both are a bit complex. Without evidence showing we are hitting performance
        // issue in real world situation, this will just use pipeline barriers to track extra stages
        // that not captured by mCurrentWriteEvent.
        if (writtenByCommandBuffer(queueSerial))
        {
            mCurrentWriteStages |= srcStageFlags;
            mCurrentWriteAccess |= mCurrentWriteEvent.getAccessFlags();
        }
        else
        {
            eventBarriers->addEventMemoryBarrier(
                context->getRenderer(), mCurrentWriteEvent.getEvent(),
                mCurrentWriteEvent.getAccessFlags(), writeStage, writeAccessType);
        }
        // Garbage collect the event, which tracks GPU completion automatically.
        mCurrentWriteEvent.releaseToEventCollector(eventCollector);
    }

    // We don't need to check mCurrentReadStages here since if it is not zero,
    // mCurrentReadAccess must not be zero as well. stage is finer grain than accessType.
    ASSERT((!mCurrentReadStages && !mCurrentReadAccess) ||
           (mCurrentReadStages && mCurrentReadAccess));

    // Barrier against prior access that not tracked by VkEvent using pipelineBarrier.
    if (mCurrentReadAccess != 0 || mCurrentWriteAccess != 0)
    {
        // If there are more pipeline stage bits not captured by eventBarrier, use pipelineBarrier.
        VkPipelineStageFlags srcStageMask = mCurrentWriteStages | mCurrentReadStages;
        if (srcStageMask)
        {
            pipelineBarriers->mergeMemoryBarrier(stageIndex, srcStageMask, writeStage,
                                                 mCurrentWriteAccess, writeAccessType);
        }

        mCurrentReadStages  = 0;
        mCurrentReadAccess  = 0;
        mCurrentWriteStages = 0;
        mCurrentWriteAccess = 0;
    }
}

void BufferHelper::recordWriteEvent(Context *context,
                                    VkAccessFlags writeAccessType,
                                    VkPipelineStageFlags writePipelineStageFlags,
                                    const QueueSerial &writeQueueSerial,
                                    PipelineStage writeStage,
                                    RefCountedEventArray *refCountedEventArray)
{
    EventStage eventStage = GetBufferMemoryBarrierData(writeStage).eventStage;
    bool useVkEvent       = false;

    if (context->getFeatures().useVkEventForBufferBarrier.enabled &&
        eventStage != EventStage::InvalidEnum)
    {
        ASSERT(mCurrentReadEvents.empty());

        // VkCmdSetEvent can remove the unnecessary GPU pipeline bubble that comes from false
        // dependency between fragment and vertex/transfer/compute stages. But it also comes with
        // higher overhead. In order to strike the balance, right now we only track it with VkEvent
        // if it ever written by transform feedback.
        useVkEvent = mXFBOrComputeWriteHeuristicBits.any();

        // We only track one write event. In case of multiple writes like write from different
        // shader stages in the same render pass, only the first write is tracked by event,
        // additional writes will still be tracked by pipelineBarriers.
        if (mCurrentWriteEvent.valid())
        {
            useVkEvent = false;
        }
    }

    if (useVkEvent && refCountedEventArray->initEventAtStage(context, eventStage))
    {
        // Copy the event to mCurrentEvent so that we can wait for it in future. This will add extra
        // refcount to the underlying VkEvent.
        mCurrentWriteEvent.setEventAndAccessFlags(refCountedEventArray->getEvent(eventStage),
                                                  writeAccessType);
    }
    else
    {
        // Reset usages on the new write to be used by pipelineBarrier later.
        mCurrentWriteAccess = writeAccessType;
        mCurrentWriteStages = writePipelineStageFlags;
    }

    setWriteQueueSerial(writeQueueSerial);
}

void BufferHelper::fillWithColor(const angle::Color<uint8_t> &color,
                                 const gl::InternalFormat &internalFormat)
{
    uint32_t count =
        static_cast<uint32_t>(getSize()) / static_cast<uint32_t>(internalFormat.pixelBytes);
    void *buffer = static_cast<void *>(getMappedMemory());

    switch (internalFormat.internalFormat)
    {
        case GL_RGB565:
        {
            uint16_t pixelColor =
                ((color.blue & 0xF8) << 11) | ((color.green & 0xFC) << 5) | (color.red & 0xF8);
            uint16_t *pixelPtr = static_cast<uint16_t *>(buffer);
            std::fill_n<uint16_t *, uint32_t, uint16_t>(pixelPtr, count, pixelColor);
        }
        break;
        case GL_RGBA8:
        {
            uint32_t pixelColor =
                (color.alpha << 24) | (color.blue << 16) | (color.green << 8) | (color.red);
            uint32_t *pixelPtr = static_cast<uint32_t *>(buffer);
            std::fill_n<uint32_t *, uint32_t, uint32_t>(pixelPtr, count, pixelColor);
        }
        break;
        case GL_BGR565_ANGLEX:
        {
            uint16_t pixelColor =
                ((color.red & 0xF8) << 11) | ((color.green & 0xFC) << 5) | (color.blue & 0xF8);
            uint16_t *pixelPtr = static_cast<uint16_t *>(buffer);
            std::fill_n<uint16_t *, uint32_t, uint16_t>(pixelPtr, count, pixelColor);
        }
        break;
        case GL_BGRA8_EXT:
        {
            uint32_t pixelColor =
                (color.alpha << 24) | (color.red << 16) | (color.green << 8) | (color.blue);
            uint32_t *pixelPtr = static_cast<uint32_t *>(buffer);
            std::fill_n<uint32_t *, uint32_t, uint32_t>(pixelPtr, count, pixelColor);
        }
        break;
        default:
            UNREACHABLE();  // Unsupported format
    }
}

void BufferHelper::fillWithPattern(const void *pattern,
                                   size_t patternSize,
                                   size_t offset,
                                   size_t size)
{
    ASSERT(offset + size <= getSize());
    ASSERT((size % patternSize) == 0);
    ASSERT((offset % patternSize) == 0);

    uint8_t *buffer = ANGLE_UNSAFE_TODO(getMappedMemory() + offset);
    ANGLE_UNSAFE_TODO(std::memcpy(buffer, pattern, patternSize));
    size_t remaining = size - patternSize;
    while (remaining > patternSize)
    {
        ANGLE_UNSAFE_TODO(std::memcpy(buffer + patternSize, buffer, patternSize));
        remaining -= patternSize;
        patternSize *= 2;
    }
    ANGLE_UNSAFE_TODO(std::memcpy(buffer + patternSize, buffer, remaining));
    return;
}

VkDeviceAddress BufferHelper::getDeviceAddress(Context *context)
{
    ASSERT(context->getFeatures().supportsBufferDeviceAddress.enabled);
    ASSERT(valid());

    VkBufferDeviceAddressInfo info{};
    info.buffer     = getBuffer().getHandle();
    info.sType      = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.pNext      = NULL;
    VkDevice device = context->getDevice();

    return VK_CALL(vkGetBufferDeviceAddressKHR, device, &info);
}

// Used for ImageHelper non-zero memory allocation when useVmaForImageSuballocation is disabled.
angle::Result InitMappableDeviceMemory(ErrorContext *context,
                                       DeviceMemory *deviceMemory,
                                       VkDeviceSize size,
                                       int value,
                                       VkMemoryPropertyFlags memoryPropertyFlags)
{
    ASSERT(!context->getFeatures().useVmaForImageSuballocation.enabled);
    VkDevice device = context->getDevice();

    uint8_t *mapPointer;
    ANGLE_VK_TRY(context, deviceMemory->map(device, 0, VK_WHOLE_SIZE, 0, &mapPointer));
    ANGLE_UNSAFE_TODO(memset(mapPointer, value, static_cast<size_t>(size)));

    // if the memory type is not host coherent, we perform an explicit flush.
    if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
        VkMappedMemoryRange mappedRange = {};
        mappedRange.sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory              = deviceMemory->getHandle();
        mappedRange.size                = VK_WHOLE_SIZE;
        ANGLE_VK_TRY(context, VK_CALL(vkFlushMappedMemoryRanges, device, 1, &mappedRange));
    }

    deviceMemory->unmap(device);

    return angle::Result::Continue;
}

// ImageHelper implementation.
ImageHelper::ImageHelper()
{
    resetCachedProperties();
    // Reserve reasonable amount of space to avoid storage reallocation.
    mSubresourceUpdates.reserve(12);
}

ImageHelper::~ImageHelper()
{
    ASSERT(!valid());
    ASSERT(!mAcquireNextImageSemaphore.valid());
}

void ImageHelper::resetCachedProperties()
{
    mImageType                     = VK_IMAGE_TYPE_2D;
    mTilingMode                    = VK_IMAGE_TILING_OPTIMAL;
    mCreateFlags                   = kVkImageCreateFlagsNone;
    mRequestedUsage                = 0;
    mExtents                       = {};
    mRotatedAspectRatio            = false;
    mIntendedFormatID              = angle::FormatID::NONE;
    mActualFormatID                = angle::FormatID::NONE;
    mSamples                       = 1;
    mImageSerial                   = kInvalidImageSerial;
    mCurrentAccess                 = ImageAccess::Undefined;
    mCurrentDeviceQueueIndex       = kInvalidDeviceQueueIndex;
    mIsReleasedToExternal          = false;
    mIsForeignImage                = false;
    mLastNonShaderReadOnlyAccess   = ImageAccess::Undefined;
    mCurrentShaderReadStageMask    = 0;
    mFirstAllocatedLevel           = gl::OwnerLevel(0);
    mLayerCount                    = 0;
    mLevelCount                    = 0;
    mTotalStagedBufferUpdateSize   = 0;
    mAllocationSize                = 0;
    mMemoryAllocationType          = MemoryAllocationType::InvalidEnum;
    mMemoryTypeIndex               = kInvalidMemoryTypeIndex;
    mTileMemoryCompatible          = false;
    mUseTileMemory                 = false;
    mTileMemorySupportsTransferSrc = false;
    mViewFormats.clear();
    mYcbcrConversionDesc.reset();
    mCurrentSingleClearValue.reset();
    mRenderPassUsageFlags.clear();
    mVkImageCreateInfo = {};

    setEntireContentUndefined();
}

ANGLE_INLINE void ImageHelper::setEntireContentDefined()
{
    for (LevelContentDefinedMask &levelContentDefined : mVkImageContentDefined)
    {
        levelContentDefined.set();
    }
    for (LevelContentDefinedMask &levelContentDefined : mVkImageStencilContentDefined)
    {
        levelContentDefined.set();
    }
}

ANGLE_INLINE void ImageHelper::setEntireContentUndefined()
{
    for (LevelContentDefinedMask &levelContentDefined : mVkImageContentDefined)
    {
        levelContentDefined.reset();
    }
    for (LevelContentDefinedMask &levelContentDefined : mVkImageStencilContentDefined)
    {
        levelContentDefined.reset();
    }

    // Note: this function is typically called during init/release, but also when importing an image
    // from Vulkan, so unlike invalidateSubresourceContentImpl, it doesn't attempt to make sure
    // emulated formats have a clear staged.
}

ANGLE_INLINE void ImageHelper::setContentDefined(LevelIndex levelStart,
                                                 uint32_t levelCount,
                                                 LayerIndex layerStart,
                                                 uint32_t layerCount,
                                                 VkImageAspectFlags aspectFlags)
{
    // Mark the range as defined.  Layers above 8 are discarded, and are always assumed to have
    // defined contents.
    if (layerStart.get() >= kMaxContentDefinedLayerCount)
    {
        return;
    }

    uint8_t layerRangeBits =
        GetContentDefinedLayerRangeBits(layerStart, layerCount, kMaxContentDefinedLayerCount);

    for (uint32_t levelOffset = 0; levelOffset < levelCount; ++levelOffset)
    {
        LevelIndex level = levelStart + levelOffset;

        if ((aspectFlags & ~VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
        {
            setLevelContentDefined(level, layerRangeBits);
        }
        if ((aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
        {
            setLevelStencilContentDefined(level, layerRangeBits);
        }
    }
}

ANGLE_INLINE void ImageHelper::setLevelContentDefined(LevelIndex level,
                                                      const uint8_t layerRangeBits)
{
    mVkImageContentDefined[level.get()] |= layerRangeBits;
}

ANGLE_INLINE void ImageHelper::clearLevelContentDefined(LevelIndex level,
                                                        const uint8_t layerRangeBits)
{
    mVkImageContentDefined[level.get()] &= ~layerRangeBits;
}

ANGLE_INLINE void ImageHelper::setLevelStencilContentDefined(LevelIndex level,
                                                             const uint8_t layerRangeBits)
{
    mVkImageStencilContentDefined[level.get()] |= layerRangeBits;
}

ANGLE_INLINE void ImageHelper::clearLevelStencilContentDefined(LevelIndex level,
                                                               const uint8_t layerRangeBits)
{
    mVkImageStencilContentDefined[level.get()] &= ~layerRangeBits;
}

ANGLE_INLINE const ImageHelper::LevelContentDefinedMask &ImageHelper::getLevelContentDefined(
    LevelIndex level) const
{
    return mVkImageContentDefined[level.get()];
}

ANGLE_INLINE const ImageHelper::LevelContentDefinedMask &ImageHelper::getLevelStencilContentDefined(
    LevelIndex level) const
{
    return mVkImageStencilContentDefined[level.get()];
}

YcbcrConversionDesc ImageHelper::deriveConversionDesc(ErrorContext *context,
                                                      angle::FormatID actualFormatID,
                                                      angle::FormatID intendedFormatID)
{
    YcbcrConversionDesc conversionDesc{};
    const angle::Format &actualFormat = angle::Format::Get(actualFormatID);

    if (actualFormat.isYUV)
    {
        // Build a suitable conversionDesc; the image is not external but may be YUV
        // if app is using ANGLE's YUV internalformat extensions.
        Renderer *renderer = context->getRenderer();

        // The Vulkan spec states: The potential format features of the sampler YCBCR conversion
        // must support VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT or
        // VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT
        constexpr VkFormatFeatureFlags kChromaSubSampleFeatureBits =
            VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
            VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

        VkFormatFeatureFlags supportedFeatureBits =
            renderer->getImageFormatFeatureBits(actualFormatID, kChromaSubSampleFeatureBits);

        VkChromaLocation supportedLocation =
            (supportedFeatureBits & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT) != 0
                ? VK_CHROMA_LOCATION_COSITED_EVEN
                : VK_CHROMA_LOCATION_MIDPOINT;
        vk::YcbcrLinearFilterSupport linearFilterSupported =
            (supportedFeatureBits &
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) != 0
                ? vk::YcbcrLinearFilterSupport::Supported
                : vk::YcbcrLinearFilterSupport::Unsupported;

        VkSamplerYcbcrModelConversion conversionModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
        VkSamplerYcbcrRange colorRange                = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        VkFilter chromaFilter                         = kDefaultYCbCrChromaFilter;
        VkComponentMapping components                 = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        };

        conversionDesc.update(renderer, 0, conversionModel, colorRange, supportedLocation,
                              supportedLocation, chromaFilter, components, intendedFormatID,
                              linearFilterSupported);
    }

    return conversionDesc;
}

angle::Result ImageHelper::init(ErrorContext *context,
                                gl::TextureType textureType,
                                const VkExtent3D &extents,
                                const Format &format,
                                GLint samples,
                                VkImageUsageFlags usage,
                                gl::OwnerLevel firstLevel,
                                uint32_t mipLevels,
                                uint32_t layerCount,
                                bool isRobustResourceInitEnabled,
                                bool hasProtectedContent,
                                TileMemory tileMemoryPreference)
{
    return initExternal(context, textureType, extents, format.getIntendedFormatID(),
                        format.getActualRenderableImageFormatID(), samples, usage,
                        kVkImageCreateFlagsNone, ImageAccess::Undefined, nullptr, firstLevel,
                        mipLevels, layerCount, isRobustResourceInitEnabled, hasProtectedContent,
                        tileMemoryPreference,
                        deriveConversionDesc(context, format.getActualRenderableImageFormatID(),
                                             format.getIntendedFormatID()),
                        nullptr, ImageFormatReinterpretability::ColorspaceOverrides);
}

angle::Result ImageHelper::copyToBufferOneOff(ErrorContext *context,
                                              BufferHelper *stagingBuffer,
                                              VkBufferImageCopy copyRegion)
{
    Renderer *renderer = context->getRenderer();
    ScopedPrimaryCommandBuffer scopedCommandBuffer(renderer->getDevice());
    ANGLE_TRY(renderer->getCommandBufferOneOff(context, ProtectionType::Unprotected,
                                               &scopedCommandBuffer));
    PrimaryCommandBuffer &commandBuffer = scopedCommandBuffer.get();

    VkSemaphore acquireNextImageSemaphore;
    recordBarrierOneOffImpl(renderer, getAspectFlags(), ImageAccess::TransferDst,
                            renderer->getQueueFamilyIndex(), &commandBuffer,
                            &acquireNextImageSemaphore);
    commandBuffer.copyBufferToImage(stagingBuffer->getBuffer().getHandle(), getImage(),
                                    getCurrentLayout(renderer), 1, &copyRegion);

    renderer->insertSubmitDebugMarkerInCommandBuffer(commandBuffer,
                                                     QueueSubmitReason::CopyBufferToImageOneOff);
    ANGLE_VK_TRY(context, commandBuffer.end());

    QueueSerial submitQueueSerial;
    ANGLE_TRY(renderer->queueSubmitOneOff(
        context, std::move(scopedCommandBuffer), ProtectionType::Unprotected,
        egl::ContextPriority::Medium, acquireNextImageSemaphore,
        kSwapchainAcquireImageWaitStageFlags, &submitQueueSerial));

    return renderer->finishQueueSerial(context, submitQueueSerial);
}

angle::Result ImageHelper::initAncillarySwapchain(ErrorContext *context,
                                                  gl::TextureType textureType,
                                                  const VkExtent3D &extents,
                                                  bool rotatedAspectRatio,
                                                  angle::FormatID intendedFormatID,
                                                  angle::FormatID actualFormatID,
                                                  GLint samples,
                                                  VkImageUsageFlags usage,
                                                  gl::OwnerLevel firstLevel,
                                                  uint32_t mipLevels,
                                                  uint32_t layerCount,
                                                  bool isRobustResourceInitEnabled,
                                                  bool hasProtectedContent)
{
    ANGLE_TRY(initExternal(context, textureType, extents, intendedFormatID, actualFormatID, samples,
                           usage, kVkImageCreateFlagsNone, ImageAccess::Undefined, nullptr,
                           firstLevel, mipLevels, layerCount, isRobustResourceInitEnabled,
                           hasProtectedContent, TileMemory::Prohibited, YcbcrConversionDesc{},
                           nullptr, ImageFormatReinterpretability::ColorspaceOverrides));
    if (rotatedAspectRatio)
    {
        std::swap(mExtents.width, mExtents.height);
    }
    mRotatedAspectRatio = rotatedAspectRatio;
    return angle::Result::Continue;
}

angle::Result ImageHelper::initExternal(ErrorContext *context,
                                        gl::TextureType textureType,
                                        const VkExtent3D &extents,
                                        angle::FormatID intendedFormatID,
                                        angle::FormatID actualFormatID,
                                        GLint samples,
                                        VkImageUsageFlags usage,
                                        VkImageCreateFlags additionalCreateFlags,
                                        ImageAccess initialAccess,
                                        const void *externalImageCreateInfo,
                                        gl::OwnerLevel firstLevel,
                                        uint32_t mipLevels,
                                        uint32_t layerCount,
                                        bool isRobustResourceInitEnabled,
                                        bool hasProtectedContent,
                                        TileMemory tileMemoryPreference,
                                        YcbcrConversionDesc conversionDesc,
                                        const void *compressionControl,
                                        ImageFormatReinterpretability formatReinterpretability)
{
    ASSERT(!valid());
    ASSERT(!IsAnySubresourceContentDefined(mVkImageContentDefined));
    ASSERT(!IsAnySubresourceContentDefined(mVkImageStencilContentDefined));

    Renderer *renderer = context->getRenderer();

    mImageType           = gl_vk::GetImageType(textureType);
    mExtents             = extents;
    mRotatedAspectRatio  = false;
    mIntendedFormatID    = intendedFormatID;
    mActualFormatID      = actualFormatID;
    mSamples             = std::max(samples, 1);
    mImageSerial         = renderer->getResourceSerialFactory().generateImageSerial();
    mFirstAllocatedLevel = firstLevel;
    mLevelCount          = mipLevels;
    mLayerCount          = layerCount;
    mCreateFlags =
        vk::GetMinimalImageCreateFlags(renderer, textureType, usage) | additionalCreateFlags;
    mRequestedUsage = usage;

    // Validate that mLayerCount is compatible with the texture type
    ASSERT(textureType != gl::TextureType::_3D || mLayerCount == 1);
    ASSERT(textureType != gl::TextureType::_2DArray || mExtents.depth == 1);
    ASSERT(textureType != gl::TextureType::External || mLayerCount == 1);
    ASSERT(textureType != gl::TextureType::Rectangle || mLayerCount == 1);
    ASSERT(textureType != gl::TextureType::CubeMap || mLayerCount == gl::kCubeFaceCount);
    ASSERT(textureType != gl::TextureType::CubeMapArray || mLayerCount % gl::kCubeFaceCount == 0);

    // It is invalid to pass in both compression control and external create info
    ASSERT(compressionControl == nullptr || externalImageCreateInfo == nullptr);

    // If externalImageCreateInfo is provided, use that directly.  Otherwise derive the necessary
    // pNext chain.
    VkFormat actualVkFormat          = GetVkFormatFromFormatID(renderer, actualFormatID);
    ImageFormats imageFormats        = {actualVkFormat};
    const void *imageCreateInfoPNext = nullptr;
    VkImageFormatListCreateInfoKHR imageFormatListInfoStorage;
    if (externalImageCreateInfo == nullptr)
    {
        imageCreateInfoPNext = compressionControl;
        ASSERT(GetImageFormatListCreateInfo(imageCreateInfoPNext) == nullptr);
        imageCreateInfoPNext = DeriveCreateInfoPNext(
            context, intendedFormatID, actualFormatID, imageCreateInfoPNext,
            &imageFormatListInfoStorage, &imageFormats, formatReinterpretability, &mCreateFlags);
    }
    else
    {
        imageCreateInfoPNext = externalImageCreateInfo;
        // Derive image formats from external create info
        DeriveImageViewFormatsFromExternalCreateInfo(externalImageCreateInfo, &imageFormats);

        // Derive the tiling for external images.
        deriveExternalImageTiling(externalImageCreateInfo);
    }

    // Cache image view formats
    ASSERT(imageFormats.size() >= 1);
    mViewFormats = imageFormats;

    const angle::Format &actualFormat = angle::Format::Get(actualFormatID);
    ANGLE_TRACE_EVENT_INSTANT(
        "gpu.angle.texture_metrics", "ImageHelper::initExternal", "intended_format",
        angle::Format::Get(intendedFormatID).glInternalFormat, "actual_format",
        actualFormat.glInternalFormat, "width", extents.width, "height", extents.height);
    mYcbcrConversionDesc = conversionDesc;
    if (actualFormat.isYUV)
    {
        ASSERT(mYcbcrConversionDesc.valid());

        // The Vulkan spec states: If the pNext chain includes a VkExternalFormatANDROID structure
        // whose externalFormat member is not 0, flags must not include
        // VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
        if (!IsYUVExternalFormat(actualFormatID))
        {
            // The Vulkan spec states: If sampler is used and the VkFormat of the image is a
            // multi-planar format, the image must have been created with
            // VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
            mCreateFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }
    }

    if (hasProtectedContent)
    {
        mCreateFlags |= VK_IMAGE_CREATE_PROTECTED_BIT;
    }

    VkSampleCountFlagBits sampleCountFlagBits =
        gl_vk::GetSamples(mSamples, context->getFeatures().limitSampleCountTo2.enabled);

    VkImageCreateInfo imageInfo     = {};
    imageInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext                 = imageCreateInfoPNext;
    imageInfo.flags                 = mCreateFlags;
    imageInfo.imageType             = mImageType;
    imageInfo.format                = actualVkFormat;
    imageInfo.extent                = mExtents;
    imageInfo.mipLevels             = mLevelCount;
    imageInfo.arrayLayers           = mLayerCount;
    imageInfo.samples               = sampleCountFlagBits;
    imageInfo.tiling                = mTilingMode;
    imageInfo.usage                 = mRequestedUsage;
    imageInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices   = nullptr;
    imageInfo.initialLayout         = renderer->getVkImageLayout(initialAccess);

    // DepthStencil format that has emulated channels needs re-stage emulated channel clear after
    // reallocate, for now just disable the tile memory for that.
    if (tileMemoryPreference == TileMemory::Preferred &&
        (renderer->getFeatures().simulateTileMemoryForTesting.enabled ||
         renderer->getFeatures().supportsTileMemoryHeap.enabled) &&
        !HasEmulatedImageChannels(angle::Format::Get(mIntendedFormatID),
                                  angle::Format::Get(mActualFormatID)) &&
        !renderer->getFeatures().allocateNonZeroMemory.enabled)
    {
        ASSERT(initialAccess == ImageAccess::Undefined);
        ASSERT(angle::Format::Get(actualFormatID).hasDepthOrStencilBits());
        ASSERT(imageCreateInfoPNext == nullptr);

        // Remove transfer bits when determining tile memory compatible or not, since tile memory
        // does not support transfers by default.
        imageInfo.usage &= ~kImageUsageTransferBits;
        mTileMemoryCompatible =
            IsTileMemoryCompatible(renderer, imageInfo, &mTileMemorySupportsTransferSrc);
        if (mTileMemoryCompatible)
        {
            if (renderer->getFeatures().supportsTileMemoryHeap.enabled)
            {
                imageInfo.usage |= VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM;
                if (mTileMemorySupportsTransferSrc)
                {
                    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                }
            }
            else if (renderer->getFeatures().simulateTileMemoryTransferSrcForTesting.enabled)
            {
                // Use SwiftShader to test VK_IMAGE_USAGE_TRANSFER_SRC_BIT capability
                ASSERT(renderer->getFeatures().simulateTileMemoryForTesting.enabled);
                mTileMemorySupportsTransferSrc = true;
                imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            mUseTileMemory = true;
        }
        else
        {
            // Restore usage bits
            imageInfo.usage = mRequestedUsage;
        }
    }

    mCurrentAccess               = initialAccess;
    mCurrentDeviceQueueIndex     = kInvalidDeviceQueueIndex;
    mIsReleasedToExternal        = false;
    mIsForeignImage              = false;
    mLastNonShaderReadOnlyAccess = ImageAccess::Undefined;
    mCurrentShaderReadStageMask  = 0;

    ANGLE_VK_TRY(context, mImage.init(context->getDevice(), imageInfo));

    mVkImageCreateInfo               = imageInfo;
    mVkImageCreateInfo.pNext         = nullptr;
    mVkImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    stageClearIfEmulatedFormat(isRobustResourceInitEnabled, externalImageCreateInfo != nullptr);

    // Consider the contents defined for any image that has the PREINITIALIZED layout, or is
    // imported from external.
    if (initialAccess != ImageAccess::Undefined || externalImageCreateInfo != nullptr)
    {
        setEntireContentDefined();
    }

    return angle::Result::Continue;
}

// static
const void *ImageHelper::DeriveCreateInfoPNext(
    ErrorContext *context,
    angle::FormatID intendedFormatID,
    angle::FormatID actualFormatID,
    const void *pNext,
    VkImageFormatListCreateInfoKHR *imageFormatListInfoStorage,
    ImageFormats *imageFormats,
    ImageFormatReinterpretability formatReinterpretability,
    VkImageCreateFlags *createFlagsOut)
{
    // Early-return if format reinterpretability was "None"
    if (formatReinterpretability == ImageFormatReinterpretability::None)
    {
        return pNext;
    }

    // For full format reinterpretability just update the VkImage create flags and return
    if (formatReinterpretability == ImageFormatReinterpretability::Full)
    {
        *createFlagsOut |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        return pNext;
    }

    ASSERT(formatReinterpretability == ImageFormatReinterpretability::ColorspaceOverrides);

    // With the introduction of sRGB related GLES extensions any sample/render target could be
    // respecified causing it to be interpreted in a different colorspace.
    // Allow linear and sRGB variants if the _intended_ format requires it.  sRGB override should be
    // ignored even if the fallback format supports it.
    //
    // For most formats, whether sRGB override is possible is covered by whether
    // GL_EXT_texture_format_sRGB_override is supported.  However, R8 and RG8 may not support
    // override, and they are controlled by GL_EXT_texture_sRGB_R8 and GL_EXT_texture_sRGB_RG8
    // respectively.
    Renderer *renderer                 = context->getRenderer();
    const bool isR8WithoutSrgbSupport  = intendedFormatID == angle::FormatID::R8_UNORM &&
                                         !renderer->getNativeExtensions().textureSRGBR8EXT;
    const bool isRG8WithoutSrgbSupport = intendedFormatID == angle::FormatID::R8G8_UNORM &&
                                         !renderer->getNativeExtensions().textureSRGBRG8EXT;
    if (IsOverridableLinearOrSRGBFormat(intendedFormatID) && !isR8WithoutSrgbSupport &&
        !isRG8WithoutSrgbSupport)
    {
        const angle::Format &actualFormat = angle::Format::Get(actualFormatID);
        angle::FormatID additionalFormatID =
            actualFormat.isSRGB ? ConvertToLinear(actualFormatID) : ConvertToSRGB(actualFormatID);

        // Add the VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT to VkImage create flag
        *createFlagsOut |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

        ASSERT(imageFormats);
        imageFormats->clear();

        // VkImageView can be created with either the linear or sRGB variant of the format
        imageFormats->push_back(vk::GetVkFormatFromFormatID(renderer, actualFormatID));
        imageFormats->push_back(vk::GetVkFormatFromFormatID(renderer, additionalFormatID));
        // VUID-VkImageFormatListCreateInfo-viewFormatCount-09540
        //    If viewFormatCount is not 0, each element of pViewFormats
        //    must not be VK_FORMAT_UNDEFINED
        ASSERT(imageFormats->at(0) != VK_FORMAT_UNDEFINED);
        ASSERT(imageFormats->at(1) != VK_FORMAT_UNDEFINED);

        imageFormatListInfoStorage->sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
        imageFormatListInfoStorage->pNext = pNext;
        imageFormatListInfoStorage->viewFormatCount = kImageColorspaceOverrideFormatCount;
        imageFormatListInfoStorage->pViewFormats    = imageFormats->data();

        pNext = imageFormatListInfoStorage;
    }

    return pNext;
}

// static
bool ImageHelper::FormatSupportsUsage(const Renderer *renderer,
                                      VkFormat format,
                                      VkImageType imageType,
                                      VkImageTiling tilingMode,
                                      VkImageUsageFlags usageFlags,
                                      VkImageCreateFlags createFlags,
                                      void *formatInfoPNext,
                                      void *propertiesPNext,
                                      const FormatSupportCheck formatSupportCheck)
{
    VkPhysicalDeviceImageFormatInfo2 imageFormatInfo = {};
    imageFormatInfo.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    imageFormatInfo.pNext  = formatInfoPNext;
    imageFormatInfo.format = format;
    imageFormatInfo.type   = imageType;
    imageFormatInfo.tiling = tilingMode;
    imageFormatInfo.usage  = usageFlags;
    imageFormatInfo.flags  = createFlags;

    VkImageFormatProperties2 imageFormatProperties2 = {};
    imageFormatProperties2.sType                    = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    imageFormatProperties2.pNext                    = propertiesPNext;

    VkResult result =
        VK_CALL(vkGetPhysicalDeviceImageFormatProperties2, renderer->getPhysicalDevice(),
                &imageFormatInfo, &imageFormatProperties2);

    if (formatSupportCheck == FormatSupportCheck::RequireMultisampling)
    {
        // Some drivers return success but sampleCounts == 1 which means no MSRTT
        return result == VK_SUCCESS &&
               imageFormatProperties2.imageFormatProperties.sampleCounts > 1;
    }
    return result == VK_SUCCESS;
}

void ImageHelper::deriveExternalImageTiling(const void *createInfoChain)
{
    const VkBaseInStructure *chain = reinterpret_cast<const VkBaseInStructure *>(createInfoChain);
    while (chain != nullptr)
    {
        if (chain->sType == VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT ||
            chain->sType == VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT)
        {
            mTilingMode = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
            return;
        }

        chain = reinterpret_cast<const VkBaseInStructure *>(chain->pNext);
    }
}

void ImageHelper::releaseImage(Renderer *renderer)
{
    if (mImage.valid())
    {
        GarbageObjects garbageObjects;
        garbageObjects.reserve(2);
        garbageObjects.emplace_back(GarbageObject::Get(&mImage));

        //  mDeviceMemory and mVmaAllocation should not be valid at the same time.
        ASSERT(!mDeviceMemory.valid() || !mVmaAllocation.valid());
        if (mDeviceMemory.valid())
        {
            renderer->onMemoryDealloc(mMemoryAllocationType, mAllocationSize, mMemoryTypeIndex,
                                      mDeviceMemory.getHandle());
            garbageObjects.emplace_back(GarbageObject::Get(&mDeviceMemory));
        }
        if (mVmaAllocation.valid())
        {
            renderer->onMemoryDealloc(mMemoryAllocationType, mAllocationSize, mMemoryTypeIndex,
                                      mVmaAllocation.getHandle());
            garbageObjects.emplace_back(GarbageObject::Get(&mVmaAllocation));
        }
        renderer->collectGarbage(mUse, std::move(garbageObjects));
    }
    else
    {
        ASSERT(!mDeviceMemory.valid());
        ASSERT(!mVmaAllocation.valid());
    }

    mCurrentEvent.release(renderer);
    mLastNonShaderReadOnlyEvent.release(renderer);
    mViewFormats.clear();
    mUse.reset();
    mImageSerial          = kInvalidImageSerial;
    mMemoryAllocationType = MemoryAllocationType::InvalidEnum;
    mCurrentSingleClearValue.reset();
    setEntireContentUndefined();
}

void ImageHelper::releaseImage(ContextVk *contextVk)
{
    contextVk->addToPendingImageGarbage(mUse, mAllocationSize);
    releaseImage(contextVk->getRenderer());
}

void ImageHelper::releaseStagedUpdates(Renderer *renderer)
{
    assertSubresourceUpdateRefCountsConsistent();

    // Remove updates that never made it to the texture.
    for (SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        while (!levelUpdates.empty())
        {
            levelUpdates.front().release(renderer);
            levelUpdates.pop_front();
        }
    }

    assertSubresourceUpdateRefCountsConsistent();

    mSubresourceUpdates.clear();
    mTotalStagedBufferUpdateSize = 0;
    mCurrentSingleClearValue.reset();
}

void ImageHelper::resetImageWeakReference()
{
    mImage.reset();
    mImageSerial        = kInvalidImageSerial;
    mRotatedAspectRatio = false;
    // Caller must ensure ANI semaphores are properly waited or released.
    ASSERT(!mAcquireNextImageSemaphore.valid());
}

angle::Result ImageHelper::initializeNonZeroMemory(ErrorContext *context,
                                                   bool hasProtectedContent,
                                                   VkMemoryPropertyFlags flags,
                                                   VkDeviceSize size)
{
    // If available, memory mapping should be used.
    Renderer *renderer = context->getRenderer();

    if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
    {
        // Wipe memory to an invalid value when the 'allocateNonZeroMemory' feature is enabled. The
        // invalid values ensures our testing doesn't assume zero-initialized memory.
        constexpr int kNonZeroInitValue = 0x3F;
        if (renderer->getFeatures().useVmaForImageSuballocation.enabled)
        {
            ANGLE_VK_TRY(context,
                         renderer->getImageMemorySuballocator().mapMemoryAndInitWithNonZeroValue(
                             renderer, &mVmaAllocation, size, kNonZeroInitValue, flags));
        }
        else
        {
            ANGLE_TRY(vk::InitMappableDeviceMemory(context, &mDeviceMemory, size, kNonZeroInitValue,
                                                   flags));
        }

        return angle::Result::Continue;
    }

    // If mapping the memory is unavailable, a staging resource is used.
    const angle::Format &angleFormat = getActualFormat();
    bool isCompressedFormat          = angleFormat.isBlock;

    if (angleFormat.isYUV)
    {
        // VUID-vkCmdClearColorImage-image-01545
        // vkCmdClearColorImage(): format must not be one of the formats requiring sampler YCBCR
        // conversion for VK_IMAGE_ASPECT_COLOR_BIT image views
        return angle::Result::Continue;
    }

    ASSERT(canTransferTo());

    // Since we are going to do a one off out of order submission, there shouldn't any pending
    // setEvent.
    ASSERT(!mCurrentEvent.valid());

    ScopedPrimaryCommandBuffer scopedCommandBuffer(renderer->getDevice());
    auto protectionType = ConvertProtectionBoolToType(hasProtectedContent);
    ANGLE_TRY(renderer->getCommandBufferOneOff(context, protectionType, &scopedCommandBuffer));
    PrimaryCommandBuffer &commandBuffer = scopedCommandBuffer.get();

    // Queue a DMA copy.
    VkSemaphore acquireNextImageSemaphore;
    recordBarrierOneOffImpl(renderer, getAspectFlags(), ImageAccess::TransferDst,
                            context->getDeviceQueueIndex(), &commandBuffer,
                            &acquireNextImageSemaphore);
    // SwapChain image should not come here
    ASSERT(acquireNextImageSemaphore == VK_NULL_HANDLE);

    StagingBuffer stagingBuffer;

    if (isCompressedFormat)
    {
        // If format is compressed, set its contents through buffer copies.

        // The staging buffer memory is non-zero-initialized in 'init'.
        // Pick an arbitrary value to initialize non-zero memory for sanitization.
        // Note that 0x3F3F3F3F as float is about 0.75.
        constexpr int kInitNonZeroValue = 0x3F;
        ANGLE_TRY(stagingBuffer.init(context, size, StagingUsage::Write, kInitNonZeroValue));

        for (LevelIndex level(0); level < LevelIndex(mLevelCount); ++level)
        {
            VkBufferImageCopy copyRegion = {};

            gl_vk::GetExtent(getLevelExtents(level), &copyRegion.imageExtent);
            copyRegion.imageSubresource.aspectMask = getAspectFlags();
            copyRegion.imageSubresource.mipLevel   = level.get();
            copyRegion.imageSubresource.layerCount = mLayerCount;

            // If image has depth and stencil, copy to each individually per Vulkan spec.
            bool hasBothDepthAndStencil = isCombinedDepthStencilFormat();
            if (hasBothDepthAndStencil)
            {
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            commandBuffer.copyBufferToImage(stagingBuffer.getBuffer().getHandle(), mImage,
                                            getCurrentLayout(renderer), 1, &copyRegion);

            if (hasBothDepthAndStencil)
            {
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

                commandBuffer.copyBufferToImage(stagingBuffer.getBuffer().getHandle(), mImage,
                                                getCurrentLayout(renderer), 1, &copyRegion);
            }
        }
    }
    else
    {
        // Otherwise issue clear commands.
        VkImageSubresourceRange subresource = {};
        subresource.aspectMask              = getAspectFlags();
        subresource.baseMipLevel            = 0;
        subresource.levelCount              = mLevelCount;
        subresource.baseArrayLayer          = 0;
        subresource.layerCount              = mLayerCount;

        // Arbitrary value to initialize the memory with.  Note: the given uint value, reinterpreted
        // as float is about 0.7.
        constexpr uint32_t kInitValue   = 0x3F345678;
        constexpr float kInitValueFloat = 0.12345f;

        if ((subresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0)
        {
            VkClearColorValue clearValue;
            clearValue.uint32[0] = kInitValue;
            clearValue.uint32[1] = kInitValue;
            clearValue.uint32[2] = kInitValue;
            clearValue.uint32[3] = kInitValue;

            commandBuffer.clearColorImage(mImage, getCurrentLayout(renderer), clearValue, 1,
                                          &subresource);
        }
        else
        {
            VkClearDepthStencilValue clearValue;
            clearValue.depth   = kInitValueFloat;
            clearValue.stencil = kInitValue;

            commandBuffer.clearDepthStencilImage(mImage, getCurrentLayout(renderer), clearValue, 1,
                                                 &subresource);
        }
    }

    renderer->insertSubmitDebugMarkerInCommandBuffer(commandBuffer,
                                                     QueueSubmitReason::InitializeMemory);
    ANGLE_VK_TRY(context, commandBuffer.end());

    QueueSerial queueSerial;
    ANGLE_TRY(renderer->queueSubmitOneOff(context, std::move(scopedCommandBuffer), protectionType,
                                          egl::ContextPriority::Medium, VK_NULL_HANDLE, 0,
                                          &queueSerial));

    if (isCompressedFormat)
    {
        stagingBuffer.collectGarbage(renderer, queueSerial);
    }
    setQueueSerial(queueSerial);
    ASSERT(!mIsForeignImage);

    return angle::Result::Continue;
}

VkResult ImageHelper::initMemory(ErrorContext *context,
                                 VkMemoryPropertyFlags flags,
                                 VkMemoryPropertyFlags excludedFlags,
                                 VkMemoryRequirements *memoryRequirements,
                                 MemoryAllocationType allocationType,
                                 VkMemoryPropertyFlags *flagsOut,
                                 VkDeviceSize *sizeOut)
{
    mMemoryAllocationType = allocationType;

    // To allocate memory here, if possible, we use the image memory suballocator which uses VMA.
    ASSERT(excludedFlags < VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM);
    Renderer *renderer = context->getRenderer();
    bool allocateDedicatedMemory =
        renderer->getImageMemorySuballocator().needsDedicatedMemory(memoryRequirements->size);

    // First try allocate tile memory if requested
    ASSERT(!mDeviceMemory.valid());
    if (mUseTileMemory)
    {
        if (renderer->getFeatures().supportsTileMemoryHeap.enabled)
        {
            AllocateImageMemoryFromTileHeap(context, mMemoryAllocationType, flags, flagsOut,
                                            &mImage, &mMemoryTypeIndex, &mDeviceMemory,
                                            &mAllocationSize);
        }
        else
        {
            ASSERT(renderer->getFeatures().simulateTileMemoryForTesting.enabled);
            // For testing purpose, we cap the tile memory allocation size to 64M. If exceed, we
            // force allocation to fail so that the allocation failure path also get testing
            // coverage.
            bool simulateAllocationFailure = memoryRequirements->size > 64ull * 1024ull * 1024ull;
            if (simulateAllocationFailure)
            {
                ASSERT(!mDeviceMemory.valid());
                WARN() << "Simulating tile memory allocation failed for image "
                       << mImage.getHandle();
            }
            else
            {
                AllocateImageMemory(context, mMemoryAllocationType, flags, flagsOut, nullptr,
                                    &mImage, &mMemoryTypeIndex, &mDeviceMemory, &mAllocationSize);
            }
        }

        if (mDeviceMemory.valid())
        {
            context->getPerfCounters().tileMemoryImages++;
        }
        else
        {
            INFO() << "tile memory allocation failed for image " << mImage.getHandle();
            // Tile-memory allocation failed. The tile-memory image was created without transfer
            // usage. So recreate the VkImage with the originally requested usage before falling
            // back to regular memory below.
            VkImageCreateInfo imageCreateInfo = mVkImageCreateInfo;
            imageCreateInfo.usage             = mRequestedUsage;
            Image newImage;
            VK_RESULT_TRY(newImage.init(context->getDevice(), imageCreateInfo));

            mImage.destroy(renderer->getDevice());
            mImage             = std::move(newImage);
            mVkImageCreateInfo = imageCreateInfo;
            mImageSerial       = renderer->getResourceSerialFactory().generateImageSerial();
            mUseTileMemory = false;

            // memory requirements may have changed
            mImage.getMemoryRequirements(renderer->getDevice(), memoryRequirements);
            allocateDedicatedMemory = renderer->getImageMemorySuballocator().needsDedicatedMemory(
                memoryRequirements->size);
        }
    }

    if (!mDeviceMemory.valid() && renderer->getFeatures().useVmaForImageSuballocation.enabled)
    {
        // While it may be preferable to allocate the image on the device, it should also be
        // possible to allocate on other memory types if the device is out of memory.
        VkMemoryPropertyFlags requiredFlags  = flags & (~excludedFlags);
        VkMemoryPropertyFlags preferredFlags = flags;
        VK_RESULT_TRY(renderer->getImageMemorySuballocator().allocateAndBindMemory(
            context, &mImage, &mVkImageCreateInfo, requiredFlags, preferredFlags,
            memoryRequirements, allocateDedicatedMemory, mMemoryAllocationType, &mVmaAllocation,
            flagsOut, &mMemoryTypeIndex, &mAllocationSize));
    }
    else if (!mDeviceMemory.valid())
    {
        const void *extraAllocationInfoPtr =
            context->getFeatures().supportsBufferDeviceAddress.enabled
                ? &kMemoryAllocateDeviceAddressFlags
                : nullptr;

        VK_RESULT_TRY(AllocateImageMemory(context, mMemoryAllocationType, flags, flagsOut,
                                          extraAllocationInfoPtr, &mImage, &mMemoryTypeIndex,
                                          &mDeviceMemory, &mAllocationSize));
    }

    mCurrentDeviceQueueIndex = context->getDeviceQueueIndex();
    mIsReleasedToExternal    = false;
    mIsForeignImage          = false;
    *sizeOut                 = mAllocationSize;

    return VK_SUCCESS;
}

angle::Result ImageHelper::initMemoryAndNonZeroFillIfNeeded(ErrorContext *context,
                                                            bool hasProtectedContent,
                                                            VkMemoryPropertyFlags flags,
                                                            MemoryAllocationType allocationType)
{
    Renderer *renderer = context->getRenderer();
    VkMemoryPropertyFlags outputFlags;
    VkDeviceSize outputSize;

    if (hasProtectedContent)
    {
        flags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    // Get memory requirements for the allocation.
    VkMemoryRequirements memoryRequirements;
    mImage.getMemoryRequirements(renderer->getDevice(), &memoryRequirements);

    ANGLE_VK_TRY(context, initMemory(context, flags, 0, &memoryRequirements, allocationType,
                                     &outputFlags, &outputSize));

    // Memory can only be non-zero initialized if the TRANSFER_DST usage is set.  This is normally
    // the case, but not with |initImplicitMultisampledRenderToTexture| which creates a
    // lazy-allocated transient image.
    if (renderer->getFeatures().allocateNonZeroMemory.enabled &&
        (mVkImageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0)
    {
        ANGLE_TRY(initializeNonZeroMemory(context, hasProtectedContent, outputFlags, outputSize));
    }
    return angle::Result::Continue;
}

angle::Result ImageHelper::initExternalMemory(ErrorContext *context,
                                              const MemoryProperties &memoryProperties,
                                              const VkMemoryRequirements &memoryRequirements,
                                              uint32_t extraAllocationInfoCount,
                                              const void **extraAllocationInfo,
                                              DeviceQueueIndex currentDeviceQueueIndex,
                                              VkMemoryPropertyFlags flags)
{
    // Vulkan allows up to 4 memory planes.
    constexpr size_t kMaxMemoryPlanes                                                        = 4;
    static constexpr std::array<VkImageAspectFlagBits, kMaxMemoryPlanes> kMemoryPlaneAspects = {
        VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT,
    };
    ASSERT(extraAllocationInfoCount <= kMaxMemoryPlanes);

    VkBindImagePlaneMemoryInfoKHR bindImagePlaneMemoryInfo = {};
    bindImagePlaneMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;

    const VkBindImagePlaneMemoryInfoKHR *bindImagePlaneMemoryInfoPtr =
        extraAllocationInfoCount == 1 ? nullptr : &bindImagePlaneMemoryInfo;

    mAllocationSize       = memoryRequirements.size;
    mMemoryAllocationType = MemoryAllocationType::ImageExternal;

    for (uint32_t memoryPlane = 0; memoryPlane < extraAllocationInfoCount; ++memoryPlane)
    {
        bindImagePlaneMemoryInfo.planeAspect = kMemoryPlaneAspects[memoryPlane];

        VkMemoryRequirements planeMemoryRequirements = memoryRequirements;
        if (extraAllocationInfoCount > 1)
        {
            VkImagePlaneMemoryRequirementsInfo planeMemoryRequirementsInfo = {};
            planeMemoryRequirementsInfo.sType =
                VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
            planeMemoryRequirementsInfo.planeAspect = kMemoryPlaneAspects[memoryPlane];

            VkImageMemoryRequirementsInfo2 imageMemoryRequirementsInfo = {};
            imageMemoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
            imageMemoryRequirementsInfo.pNext = &planeMemoryRequirementsInfo;
            imageMemoryRequirementsInfo.image = mImage.getHandle();

            VkMemoryRequirements2 planeMemoryRequirements2 = {};
            planeMemoryRequirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
            mImage.getMemoryRequirements2(context->getDevice(), imageMemoryRequirementsInfo,
                                          &planeMemoryRequirements2);
            planeMemoryRequirements = planeMemoryRequirements2.memoryRequirements;
        }

        ANGLE_VK_TRY(context,
                     AllocateImageMemoryWithRequirements(
                         context, mMemoryAllocationType, flags, planeMemoryRequirements,
                         ANGLE_UNSAFE_TODO(extraAllocationInfo[memoryPlane]),
                         bindImagePlaneMemoryInfoPtr, &mImage, &mMemoryTypeIndex, &mDeviceMemory));
    }
    mCurrentDeviceQueueIndex = currentDeviceQueueIndex;
    mIsReleasedToExternal    = false;
    mIsForeignImage          = currentDeviceQueueIndex == kForeignDeviceQueueIndex;

    return angle::Result::Continue;
}

angle::Result ImageHelper::fallbackFromTileMemory(ContextVk *contextVk)
{
    ASSERT(mUseTileMemory);
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::fallbackFromTileMemory");
    Renderer *renderer                  = contextVk->getRenderer();
    UtilsVk &utilsVk                    = contextVk->getUtils();
    MemoryAllocationType allocationType = mMemoryAllocationType;

    ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_LOW,
                          "The Vulkan driver has to copy from tile memory to regular memory. "
                          "Consider calling glInvalidateFramebuffer");

    contextVk->getShareGroup()->imageWillFallbackFromTileMemory(this);

    // Move the necessary information from this ImageHelper to prevImage
    std::unique_ptr<ImageHelper> prevImage = std::make_unique<ImageHelper>();
    // Move storage from this object to prevImage
    prevImage->copyStateAndMoveStorageFrom(this);

    // Recreate VkImage with original usage bits
    mImageSerial             = renderer->getResourceSerialFactory().generateImageSerial();
    mVkImageCreateInfo.usage = mRequestedUsage;
    ANGLE_VK_TRY(contextVk, mImage.init(contextVk->getDevice(), mVkImageCreateInfo));
    contextVk->getPerfCounters().tileMemoryImages--;
    contextVk->getPerfCounters().fallbackFromTileMemory++;

    // reallocate device memory.
    VkMemoryRequirements memoryRequirements;
    mImage.getMemoryRequirements(renderer->getDevice(), &memoryRequirements);

    VkMemoryPropertyFlags memoryPropertyFlagsOut;
    VkDeviceSize deviceMemorySizeOut;
    ANGLE_VK_TRY(contextVk, initMemory(contextVk, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0x0,
                                       &memoryRequirements, allocationType, &memoryPropertyFlagsOut,
                                       &deviceMemorySizeOut));

    // Copy data from the previous image.
    if (prevImage->isVkImageContentDefined())
    {
        const angle::Format &actualFormat = prevImage->getActualFormat();
        VkImageAspectFlags aspectFlags    = 0;
        if (actualFormat.depthBits > 0 &&
            IsAnySubresourceContentDefined(prevImage->mVkImageContentDefined))
        {
            aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if (actualFormat.stencilBits > 0 &&
            IsAnySubresourceContentDefined(prevImage->mVkImageStencilContentDefined))
        {
            aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        ASSERT(aspectFlags != 0);

        if (prevImage->mTileMemorySupportsTransferSrc)
        {
            // If image has depth and stencil, copy to each individually per Vulkan spec
            // VUID-VkImageMemoryBarrier-image-03320.
            if (isCombinedDepthStencilFormat())
            {
                aspectFlags = kDepthStencilAspects;
            }
            // Use vkCmdCopyImage for a bit-exact copy instead of sampling tile memory through a
            // shader.
            CommandResources resources;
            resources.onImageTransferRead(aspectFlags, prevImage.get());
            resources.onImageTransferWrite(gl::OwnerLevel(0), 1, gl::OwnerLayer(0), 1, aspectFlags,
                                           this);

            OutsideRenderPassCommandBuffer *commandBuffer;
            ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

            VkImageCopy region                   = {};
            region.srcSubresource.aspectMask     = aspectFlags;
            region.srcSubresource.mipLevel       = 0;
            region.srcSubresource.baseArrayLayer = 0;
            region.srcSubresource.layerCount     = 1;
            region.dstSubresource                = region.srcSubresource;
            region.extent.width                  = mExtents.width;
            region.extent.height                 = mExtents.height;
            region.extent.depth                  = 1;

            commandBuffer->copyImage(prevImage->getImage(), prevImage->getCurrentLayout(renderer),
                                     getImage(), getCurrentLayout(renderer), 1, &region);

            // Seal mOutsideRenderPassCommands so the copy lands in the same vkQueueSubmit as
            // the tile memory render pass (mirrors what copyImageFromTileMemory does below).
            ANGLE_TRY(contextVk->flushCommandsAndEndRenderPassWithoutSubmit(
                RenderPassClosureReason::TileMemorySimulatedClear));

            // Restore the added extra content defined bits due to extra aspectFlags for copyImage.
            mVkImageContentDefined[0]        = prevImage->mVkImageContentDefined[0];
            mVkImageStencilContentDefined[0] = prevImage->mVkImageStencilContentDefined[0];
        }
        else
        {
            ANGLE_TRY(
                utilsVk.copyImageFromTileMemory(contextVk, aspectFlags, this, prevImage.get()));
        }

        // If RenderPassLoadStoreOpNone is not supported, load/store will be used and it will result
        // in both aspect data being valid. It is less optimal, but most driver supports it already.
        if (contextVk->getFeatures().supportsRenderPassLoadStoreOpNone.enabled)
        {
            ASSERT(IsAnySubresourceContentDefined(mVkImageContentDefined) ==
                   IsAnySubresourceContentDefined(prevImage->mVkImageContentDefined));
            ASSERT(IsAnySubresourceContentDefined(mVkImageStencilContentDefined) ==
                   IsAnySubresourceContentDefined(prevImage->mVkImageStencilContentDefined));
        }
    }

    prevImage->releaseImage(renderer);
    prevImage.release();

    // Notify RenderBufferVk/SurfaceVk so that they can free all cached objects like ImageViews and
    // VkFramebuffers.
    onStateChange(angle::SubjectMessage::VkImageChanged);

    return angle::Result::Continue;
}

void ImageHelper::getImageSubresourceLayout(Renderer *renderer,
                                            VkSubresourceLayout2 *subresourceLayout)
{
    ASSERT(renderer->getFeatures().supportsImageCompressionControl.enabled);
    ASSERT(subresourceLayout != nullptr);

    VkImageSubresource2 imageSubresource         = {};
    imageSubresource.sType                       = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT;
    imageSubresource.imageSubresource.aspectMask = getAspectFlags();

    VK_CALL(vkGetImageSubresourceLayout2EXT, renderer->getDevice(), mImage.getHandle(),
            &imageSubresource, subresourceLayout);
}

angle::Result ImageHelper::initLayerImageView(ContextVk *contextVk,
                                              gl::TextureType textureType,
                                              VkImageAspectFlags aspectMask,
                                              const gl::SwizzleState &swizzleMap,
                                              ImageView *imageViewOut,
                                              LevelIndex baseMipLevelVk,
                                              uint32_t levelCount,
                                              LayerIndex baseArrayLayer,
                                              uint32_t layerCount) const
{
    return initLayerImageViewImpl(
        contextVk, textureType, aspectMask, swizzleMap, imageViewOut, baseMipLevelVk, levelCount,
        baseArrayLayer, layerCount,
        GetVkFormatFromFormatID(contextVk->getRenderer(), mActualFormatID),
        kDefaultImageViewUsageFlags, gl::YuvSamplingMode::Default, GL_NONE);
}

angle::Result ImageHelper::initLayerImageViewWithUsage(ContextVk *contextVk,
                                                       gl::TextureType textureType,
                                                       VkImageAspectFlags aspectMask,
                                                       const gl::SwizzleState &swizzleMap,
                                                       ImageView *imageViewOut,
                                                       LevelIndex baseMipLevelVk,
                                                       uint32_t levelCount,
                                                       LayerIndex baseArrayLayer,
                                                       uint32_t layerCount,
                                                       VkImageUsageFlags imageUsageFlags,
                                                       GLenum astcDecodePrecision) const
{
    return initLayerImageViewImpl(
        contextVk, textureType, aspectMask, swizzleMap, imageViewOut, baseMipLevelVk, levelCount,
        baseArrayLayer, layerCount,
        GetVkFormatFromFormatID(contextVk->getRenderer(), mActualFormatID), imageUsageFlags,
        gl::YuvSamplingMode::Default, astcDecodePrecision);
}

angle::Result ImageHelper::initLayerImageViewWithYuvModeOverride(
    ContextVk *contextVk,
    gl::TextureType textureType,
    VkImageAspectFlags aspectMask,
    const gl::SwizzleState &swizzleMap,
    ImageView *imageViewOut,
    LevelIndex baseMipLevelVk,
    uint32_t levelCount,
    LayerIndex baseArrayLayer,
    uint32_t layerCount,
    gl::YuvSamplingMode yuvSamplingMode,
    VkImageUsageFlags imageUsageFlags,
    GLenum astcDecodePrecision) const
{
    return initLayerImageViewImpl(
        contextVk, textureType, aspectMask, swizzleMap, imageViewOut, baseMipLevelVk, levelCount,
        baseArrayLayer, layerCount,
        GetVkFormatFromFormatID(contextVk->getRenderer(), mActualFormatID), imageUsageFlags,
        yuvSamplingMode, astcDecodePrecision);
}

angle::Result ImageHelper::initLayerImageViewImpl(ContextVk *contextVk,
                                                  gl::TextureType textureType,
                                                  VkImageAspectFlags aspectMask,
                                                  const gl::SwizzleState &swizzleMap,
                                                  ImageView *imageViewOut,
                                                  LevelIndex baseMipLevelVk,
                                                  uint32_t levelCount,
                                                  LayerIndex baseArrayLayer,
                                                  uint32_t layerCount,
                                                  VkFormat imageFormat,
                                                  VkImageUsageFlags usageFlags,
                                                  gl::YuvSamplingMode yuvSamplingMode,
                                                  GLenum astcDecodePrecision) const
{
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.flags                 = 0;
    viewInfo.image                 = mImage.getHandle();
    viewInfo.viewType              = gl_vk::GetImageViewType(textureType);
    viewInfo.format                = imageFormat;

    if (swizzleMap.swizzleRequired() && !mYcbcrConversionDesc.valid())
    {
        viewInfo.components.r = gl_vk::GetSwizzle(swizzleMap.swizzleRed);
        viewInfo.components.g = gl_vk::GetSwizzle(swizzleMap.swizzleGreen);
        viewInfo.components.b = gl_vk::GetSwizzle(swizzleMap.swizzleBlue);
        viewInfo.components.a = gl_vk::GetSwizzle(swizzleMap.swizzleAlpha);
    }
    else
    {
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    }
    viewInfo.subresourceRange.aspectMask     = aspectMask;
    viewInfo.subresourceRange.baseMipLevel   = baseMipLevelVk.get();
    viewInfo.subresourceRange.levelCount     = levelCount;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer.get();
    viewInfo.subresourceRange.layerCount     = layerCount;

    VkImageViewUsageCreateInfo imageViewUsageCreateInfo = {};
    if (usageFlags)
    {
        imageViewUsageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        imageViewUsageCreateInfo.usage = usageFlags;

        viewInfo.pNext = &imageViewUsageCreateInfo;
    }

    const angle::Format &angleFormat               = getActualFormat();
    GLenum glFormat                                = angleFormat.glInternalFormat;
    VkImageViewASTCDecodeModeEXT astcDecodeModeEXT = {};
    if (astcDecodePrecision != GL_NONE &&
        (gl::IsASTC2DFormat(glFormat) || gl::IsASTC3DFormat(glFormat)))
    {
        astcDecodeModeEXT.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT;
        astcDecodeModeEXT.pNext      = nullptr;
        astcDecodeModeEXT.decodeMode = gl_vk::GetAstcDecodeMode(astcDecodePrecision);

        AddToPNextChain(&viewInfo, &astcDecodeModeEXT);
    }

    VkSamplerYcbcrConversionInfo yuvConversionInfo = {};

    auto conversionDesc =
        yuvSamplingMode == gl::YuvSamplingMode::Y2Y ? getY2YConversionDesc() : mYcbcrConversionDesc;

    if (conversionDesc.valid())
    {
        ASSERT((contextVk->getFeatures().supportsYUVSamplerConversion.enabled));
        yuvConversionInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        yuvConversionInfo.pNext = nullptr;
        ANGLE_TRY(contextVk->getYuvConversionCache().getSamplerYcbcrConversion(
            contextVk, conversionDesc, &yuvConversionInfo.conversion));
        AddToPNextChain(&viewInfo, &yuvConversionInfo);

        // VUID-VkImageViewCreateInfo-image-02399
        // If image has an external format, format must be VK_FORMAT_UNDEFINED
        if (conversionDesc.getExternalFormat() != 0)
        {
            viewInfo.format = VK_FORMAT_UNDEFINED;
        }
    }
    ANGLE_VK_TRY(contextVk, imageViewOut->init(contextVk->getDevice(), viewInfo));
    return angle::Result::Continue;
}

angle::Result ImageHelper::initReinterpretedLayerImageView(ContextVk *contextVk,
                                                           gl::TextureType textureType,
                                                           VkImageAspectFlags aspectMask,
                                                           const gl::SwizzleState &swizzleMap,
                                                           ImageView *imageViewOut,
                                                           LevelIndex baseMipLevelVk,
                                                           uint32_t levelCount,
                                                           LayerIndex baseArrayLayer,
                                                           uint32_t layerCount,
                                                           VkImageUsageFlags imageUsageFlags,
                                                           angle::FormatID imageViewFormat,
                                                           GLenum astcDecodePrecision) const
{
    VkImageUsageFlags usageFlags =
        imageUsageFlags & GetMaximalImageUsageFlags(contextVk->getRenderer(), imageViewFormat);

    return initLayerImageViewImpl(
        contextVk, textureType, aspectMask, swizzleMap, imageViewOut, baseMipLevelVk, levelCount,
        baseArrayLayer, layerCount,
        vk::GetVkFormatFromFormatID(contextVk->getRenderer(), imageViewFormat), usageFlags,
        gl::YuvSamplingMode::Default, astcDecodePrecision);
}

void ImageHelper::destroy(Renderer *renderer)
{
    VkDevice device = renderer->getDevice();

    // mDeviceMemory and mVmaAllocation should not be valid at the same time.
    ASSERT(!mDeviceMemory.valid() || !mVmaAllocation.valid());
    if (mDeviceMemory.valid())
    {
        renderer->onMemoryDealloc(mMemoryAllocationType, mAllocationSize, mMemoryTypeIndex,
                                  mDeviceMemory.getHandle());
    }
    if (mVmaAllocation.valid())
    {
        renderer->onMemoryDealloc(mMemoryAllocationType, mAllocationSize, mMemoryTypeIndex,
                                  mVmaAllocation.getHandle());
    }

    mCurrentEvent.release(renderer);
    mLastNonShaderReadOnlyEvent.release(renderer);
    mImage.destroy(device);
    mDeviceMemory.destroy(device);
    mVmaAllocation.destroy(renderer->getAllocator());
    mCurrentAccess        = ImageAccess::Undefined;
    mImageType            = VK_IMAGE_TYPE_2D;
    mLayerCount           = 0;
    mLevelCount           = 0;
    mMemoryAllocationType = MemoryAllocationType::InvalidEnum;

    setEntireContentUndefined();
}

void ImageHelper::init2DWeakReference(ErrorContext *context,
                                      VkImage handle,
                                      const gl::Extents &glExtents,
                                      bool rotatedAspectRatio,
                                      angle::FormatID intendedFormatID,
                                      angle::FormatID actualFormatID,
                                      VkImageCreateFlags createFlags,
                                      VkImageUsageFlags usage,
                                      GLint samples,
                                      bool isRobustResourceInitEnabled,
                                      const ImageFormats &imageFormats)
{
    ASSERT(!valid());
    ASSERT(!IsAnySubresourceContentDefined(mVkImageContentDefined));
    ASSERT(!IsAnySubresourceContentDefined(mVkImageStencilContentDefined));
    vk::Renderer *renderer = context->getRenderer();

    gl_vk::GetExtent(glExtents, &mExtents);
    mRotatedAspectRatio      = rotatedAspectRatio;
    mIntendedFormatID        = intendedFormatID;
    mActualFormatID          = actualFormatID;
    mCreateFlags             = createFlags;
    mRequestedUsage          = usage;
    mSamples                 = std::max(samples, 1);
    mImageSerial             = renderer->getResourceSerialFactory().generateImageSerial();
    mCurrentDeviceQueueIndex = context->getDeviceQueueIndex();
    mIsReleasedToExternal    = false;
    mIsForeignImage          = false;
    mCurrentAccess           = ImageAccess::Undefined;
    mLayerCount              = 1;
    mLevelCount              = 1;

    // The view formats and usage flags are used for imageless framebuffers.
    mViewFormats = imageFormats;

    mImage.setHandle(handle);
    // Even though we did not use mVkImageCreateInfo to create mImage, mVkImageCreateInfo.usage  is
    // being used for actual usage of mImage, so we always keep it updated.
    mVkImageCreateInfo.usage = mRequestedUsage;

    stageClearIfEmulatedFormat(isRobustResourceInitEnabled, false);
}

angle::Result ImageHelper::init2DStaging(ErrorContext *context,
                                         bool hasProtectedContent,
                                         const gl::Extents &glExtents,
                                         angle::FormatID intendedFormatID,
                                         angle::FormatID actualFormatID,
                                         VkImageUsageFlags usage,
                                         uint32_t layerCount)
{
    gl_vk::GetExtent(glExtents, &mExtents);

    return initStaging(context, hasProtectedContent, VK_IMAGE_TYPE_2D, mExtents, intendedFormatID,
                       actualFormatID, 1, usage, 1, layerCount);
}

angle::Result ImageHelper::initStaging(ErrorContext *context,
                                       bool hasProtectedContent,
                                       VkImageType imageType,
                                       const VkExtent3D &extents,
                                       angle::FormatID intendedFormatID,
                                       angle::FormatID actualFormatID,
                                       GLint samples,
                                       VkImageUsageFlags usage,
                                       uint32_t mipLevels,
                                       uint32_t layerCount)
{
    ASSERT(!valid());
    ASSERT(!IsAnySubresourceContentDefined(mVkImageContentDefined));
    ASSERT(!IsAnySubresourceContentDefined(mVkImageStencilContentDefined));
    vk::Renderer *renderer = context->getRenderer();

    mImageType          = imageType;
    mExtents            = extents;
    mRotatedAspectRatio = false;
    mIntendedFormatID   = intendedFormatID;
    mActualFormatID     = actualFormatID;
    mSamples            = std::max(samples, 1);
    mImageSerial        = renderer->getResourceSerialFactory().generateImageSerial();
    mLayerCount         = layerCount;
    mLevelCount         = mipLevels;
    mRequestedUsage     = usage;

    // Validate that mLayerCount is compatible with the image type
    ASSERT(imageType != VK_IMAGE_TYPE_3D || mLayerCount == 1);
    ASSERT(imageType != VK_IMAGE_TYPE_2D || mExtents.depth == 1);

    mCurrentAccess = ImageAccess::Undefined;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags             = hasProtectedContent ? VK_IMAGE_CREATE_PROTECTED_BIT : 0;
    imageInfo.imageType         = mImageType;
    imageInfo.format            = GetVkFormatFromFormatID(renderer, actualFormatID);
    imageInfo.extent            = mExtents;
    imageInfo.mipLevels         = mLevelCount;
    imageInfo.arrayLayers       = mLayerCount;
    imageInfo.samples =
        gl_vk::GetSamples(mSamples, context->getFeatures().limitSampleCountTo2.enabled);
    imageInfo.tiling                = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage                 = usage;
    imageInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices   = nullptr;
    imageInfo.initialLayout         = getCurrentLayout(renderer);

    ANGLE_VK_TRY(context, mImage.init(context->getDevice(), imageInfo));

    mVkImageCreateInfo               = imageInfo;
    mVkImageCreateInfo.pNext         = nullptr;
    mVkImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Allocate and bind device-local memory.
    VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (hasProtectedContent)
    {
        memoryPropertyFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
    }

    ANGLE_TRY(initMemoryAndNonZeroFillIfNeeded(context, hasProtectedContent, memoryPropertyFlags,
                                               vk::MemoryAllocationType::StagingImage));
    return angle::Result::Continue;
}

angle::Result ImageHelper::initImplicitMultisampledRenderToTexture(
    ErrorContext *context,
    bool hasProtectedContent,
    GLint samples,
    const ImageHelper &resolveImage,
    const VkExtent3D &multisampleImageExtents,
    bool isRobustResourceInitEnabled)
{
    ASSERT(!valid());
    ASSERT(samples > 1);
    ASSERT(!IsAnySubresourceContentDefined(mVkImageContentDefined));
    ASSERT(!IsAnySubresourceContentDefined(mVkImageStencilContentDefined));

    // The image is used as either color or depth/stencil attachment.  Additionally, its memory is
    // lazily allocated as the contents are discarded at the end of the renderpass and with tiling
    // GPUs no actual backing memory is required.
    //
    // Note that the Vulkan image is created with or without VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
    // based on whether the memory that will be used to create the image would have
    // VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT.  TRANSIENT is provided if there is any memory that
    // supports LAZILY_ALLOCATED.  However, based on actual image requirements, such a memory may
    // not be suitable for the image.  We don't support such a case, which will result in the
    // |initMemory| call below failing.
    const bool hasLazilyAllocatedMemory =
        context->getRenderer()->getMemoryProperties().hasLazilyAllocatedMemory();

    const VkImageUsageFlags kLazyFlags =
        hasLazilyAllocatedMemory ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0;
    constexpr VkImageUsageFlags kColorFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    // Request input attachment flag iff supportsShaderFramebufferFetchDepthStencil is enabled.
    const VkImageUsageFlags depthStencilFlags =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        ((context->getFeatures().supportsShaderFramebufferFetchDepthStencil.enabled)
             ? VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
             : 0);

    const VkImageUsageFlags kMultisampledUsageFlags =
        kLazyFlags |
        (resolveImage.getAspectFlags() == VK_IMAGE_ASPECT_COLOR_BIT ? kColorFlags
                                                                    : depthStencilFlags);
    const VkImageCreateFlags kMultisampledCreateFlags =
        hasProtectedContent ? VK_IMAGE_CREATE_PROTECTED_BIT : 0;

    // Multisampled images have only 1 level
    constexpr uint32_t kLevelCount = 1;

    // The resolve image is either 2D or a cubemap.  For the implicit MSAA image, use a 2D array
    // image instead of a cubemap since multisampling together with
    // VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT is not allowed in Vulkan.
    ASSERT(resolveImage.getLayerCount() == 1 || resolveImage.getLayerCount() == gl::kCubeFaceCount);
    const gl::TextureType textureType =
        resolveImage.getLayerCount() == 1 ? gl::TextureType::_2D : gl::TextureType::_2DArray;
    ANGLE_TRY(initExternal(context, textureType, multisampleImageExtents,
                           resolveImage.getIntendedFormatID(), resolveImage.getActualFormatID(),
                           samples, kMultisampledUsageFlags, kMultisampledCreateFlags,
                           ImageAccess::Undefined, nullptr, resolveImage.getFirstAllocatedLevel(),
                           kLevelCount, resolveImage.getLayerCount(), isRobustResourceInitEnabled,
                           hasProtectedContent, TileMemory::Prohibited, YcbcrConversionDesc{},
                           nullptr, ImageFormatReinterpretability::ColorspaceOverrides));

    // Remove the emulated format clear from the multisampled image if any.  There is one already
    // staged on the resolve image if needed.
    removeStagedUpdates(context, getFirstAllocatedLevel(), getLastAllocatedLevel());

    const VkMemoryPropertyFlags kMultisampledMemoryFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        (hasLazilyAllocatedMemory ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : 0) |
        (hasProtectedContent ? VK_MEMORY_PROPERTY_PROTECTED_BIT : 0);

    // If this ever fails, it can be retried without the LAZILY_ALLOCATED flag (which will probably
    // still fail), but ideally that means GL_EXT_multisampled_render_to_texture should not be
    // advertised on this platform in the first place.
    ANGLE_TRY(initMemoryAndNonZeroFillIfNeeded(
        context, hasProtectedContent, kMultisampledMemoryFlags,
        vk::MemoryAllocationType::ImplicitMultisampledRenderToTextureImage));
    return angle::Result::Continue;
}

angle::Result ImageHelper::initRgbDrawImageForYuvResolve(ErrorContext *context,
                                                         const ImageHelper &resolveImage,
                                                         bool isRobustResourceInitEnabled)
{
    // Find the RGB format corresponding to the YUV format
    const vk::ExternalYuvFormatInfo &externalFormatInfo =
        context->getRenderer()->getExternalFormatTable()->getExternalFormatInfo(
            resolveImage.getActualFormatID());
    const angle::FormatID formatID =
        vk::GetFormatIDFromVkFormat(externalFormatInfo.colorAttachmentFormat);

    // Create RGB draw image
    const VkImageUsageFlags usageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    const bool hasProtectedContent =
        (resolveImage.getCreateFlags() & VK_IMAGE_CREATE_PROTECTED_BIT) != 0;
    const VkImageCreateFlags createFlags = hasProtectedContent ? VK_IMAGE_CREATE_PROTECTED_BIT : 0;

    ANGLE_TRY(initExternal(context, gl::TextureType::_2D, resolveImage.getExtents(), formatID,
                           formatID, 1, usageFlags, createFlags, ImageAccess::Undefined, nullptr,
                           resolveImage.getFirstAllocatedLevel(), resolveImage.getLevelCount(),
                           resolveImage.getLayerCount(), isRobustResourceInitEnabled,
                           hasProtectedContent, TileMemory::Prohibited, YcbcrConversionDesc{},
                           nullptr, ImageFormatReinterpretability::None));

    ASSERT(!hasEmulatedImageChannels());

    const VkMemoryPropertyFlags yuvMemoryFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        (hasProtectedContent ? VK_MEMORY_PROPERTY_PROTECTED_BIT : 0);

    ANGLE_TRY(initMemoryAndNonZeroFillIfNeeded(context, hasProtectedContent, yuvMemoryFlags,
                                               vk::MemoryAllocationType::ImplicitYuvTargetImage));

    return angle::Result::Continue;
}

VkImageAspectFlags ImageHelper::getIntendedAspectFlags() const
{
    return GetFormatAspectFlags(angle::Format::Get(mIntendedFormatID));
}

VkImageAspectFlags ImageHelper::getAspectFlags() const
{
    return GetFormatAspectFlags(angle::Format::Get(mActualFormatID));
}

bool ImageHelper::isCombinedDepthStencilFormat() const
{
    return (getAspectFlags() & kDepthStencilAspects) == kDepthStencilAspects;
}

void ImageHelper::setCurrentImageAccess(Renderer *renderer, ImageAccess newAccess)
{
    // Once you transition to ImageAccess::SharedPresent, you never transition out of it.
    if (mCurrentAccess == ImageAccess::SharedPresent)
    {
        return;
    }

    mLastNonShaderReadOnlyAccess =
        !IsShaderReadOnlyAccess(mCurrentAccess) ? mCurrentAccess : mLastNonShaderReadOnlyAccess;
    // Force the use of BarrierType::Pipeline in the next barrierImpl call
    mLastNonShaderReadOnlyEvent.release(renderer);
    mCurrentShaderReadStageMask = IsShaderReadOnlyAccess(newAccess)
                                      ? renderer->getImageMemoryBarrierData(newAccess).dstStageMask
                                      : 0;
    mCurrentAccess              = newAccess;
}

VkImageLayout ImageHelper::getCurrentLayout(Renderer *renderer) const
{
    return renderer->getVkImageLayout(mCurrentAccess);
}

gl::Extents ImageHelper::getLevelExtents(LevelIndex levelVk) const
{
    // Level 0 should be the size of the extents, after that every time you increase a level
    // you shrink the extents by half.
    uint32_t width  = std::max(mExtents.width >> levelVk.get(), 1u);
    uint32_t height = std::max(mExtents.height >> levelVk.get(), 1u);
    uint32_t depth  = std::max(mExtents.depth >> levelVk.get(), 1u);

    return gl::Extents(width, height, depth);
}

gl::Extents ImageHelper::getLevelExtents2D(LevelIndex levelVk) const
{
    gl::Extents extents = getLevelExtents(levelVk);
    extents.depth       = 1;
    return extents;
}

const VkExtent3D ImageHelper::getRotatedExtents() const
{
    VkExtent3D extents = mExtents;
    if (mRotatedAspectRatio)
    {
        std::swap(extents.width, extents.height);
    }
    return extents;
}

gl::Extents ImageHelper::getRotatedLevelExtents2D(LevelIndex levelVk) const
{
    gl::Extents extents = getLevelExtents2D(levelVk);
    if (mRotatedAspectRatio)
    {
        std::swap(extents.width, extents.height);
    }
    return extents;
}

bool ImageHelper::isDepthOrStencil() const
{
    return getActualFormat().hasDepthOrStencilBits();
}

bool ImageHelper::isReadBarrierNecessary(Renderer *renderer, ImageAccess newAccess) const
{
    // If transitioning to a different layout, we need always need a barrier.
    if (mCurrentAccess != newAccess)
    {
        return true;
    }

    // RAR (read-after-read) is not a hazard and doesn't require a barrier.
    //
    // RAW (read-after-write) hazards always require a memory barrier.  This can only happen if the
    // layout (same as new layout) is writable which in turn is only possible if the image is
    // simultaneously bound for shader write (i.e. the layout is GENERAL or SHARED_PRESENT).
    const ImageMemoryBarrierData &layoutData = renderer->getImageMemoryBarrierData(mCurrentAccess);
    return HasResourceWriteAccess(layoutData.type);
}

bool ImageHelper::isReadSubresourceBarrierNecessary(ImageAccess newAccess,
                                                    LevelIndex levelStart,
                                                    uint32_t levelCount,
                                                    gl::OwnerLayer layerStart,
                                                    uint32_t layerCount) const
{
    // In case an image has both read and write permissions, the written subresources since the last
    // barrier should be checked to avoid RAW and WAR hazards. However, if a layout change is
    // necessary regardless, there is no need to check the written subresources.
    if (mCurrentAccess != newAccess)
    {
        return true;
    }

    // Updates are tracked by layer, which are always [0, 1) for 3D images.
    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        layerStart = gl::OwnerLayer(0);
        layerCount = 1;
    }

    ImageLayerWriteMask layerMask = GetImageLayerWriteMask(layerStart, layerCount);
    for (uint32_t levelOffset = 0; levelOffset < levelCount; levelOffset++)
    {
        if (areLevelSubresourcesWrittenWithinMaskRange(levelStart + levelOffset, layerMask))
        {
            return true;
        }
    }

    return false;
}

bool ImageHelper::isWriteBarrierNecessary(ImageAccess newAccess,
                                          LevelIndex levelStart,
                                          uint32_t levelCount,
                                          gl::OwnerLayer layerStart,
                                          uint32_t layerCount) const
{
    // If transitioning to a different layout, we need always need a barrier.
    if (mCurrentAccess != newAccess)
    {
        return true;
    }

    if (layerCount >= kMaxParallelLayerWrites)
    {
        return true;
    }

    // Updates are tracked by layer, which are always [0, 1) for 3D images.
    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        layerStart = gl::OwnerLayer(0);
        layerCount = 1;
    }

    // If we are writing to the same parts of the image (level/layer), we need a barrier. Otherwise,
    // it can be done in parallel.
    ImageLayerWriteMask layerMask = GetImageLayerWriteMask(layerStart, layerCount);
    for (uint32_t levelOffset = 0; levelOffset < levelCount; levelOffset++)
    {
        if (areLevelSubresourcesWrittenWithinMaskRange(levelStart + levelOffset, layerMask))
        {
            return true;
        }
    }

    return false;
}

void ImageHelper::changeLayoutAndQueue(Context *context,
                                       VkImageAspectFlags aspectMask,
                                       ImageAccess newAccess,
                                       DeviceQueueIndex newDeviceQueueIndex,
                                       OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(!mIsForeignImage);

    ASSERT(isQueueFamilyChangeNeccesary(newDeviceQueueIndex));
    VkSemaphore acquireNextImageSemaphore;
    // recordBarrierImpl should detect there is queue switch and fall back to pipelineBarrier
    // properly.
    recordBarrierImpl(context, aspectMask, newAccess, newDeviceQueueIndex, nullptr, commandBuffer,
                      &acquireNextImageSemaphore);
    // SwapChain image should not get here.
    ASSERT(acquireNextImageSemaphore == VK_NULL_HANDLE);
}

void ImageHelper::acquireFromExternal(Context *context,
                                      DeviceQueueIndex externalQueueIndex,
                                      DeviceQueueIndex newDeviceQueueIndex,
                                      ImageAccess currentAccess,
                                      OutsideRenderPassCommandBuffer *commandBuffer)
{
    // The image must be newly allocated or have been released to the external
    // queue. If this is not the case, it's an application bug, so ASSERT might
    // eventually need to change to a warning.
    ASSERT(mCurrentAccess == ImageAccess::ExternalPreInitialized ||
           mCurrentDeviceQueueIndex.familyIndex() == externalQueueIndex.familyIndex());

    mCurrentAccess           = currentAccess;
    mCurrentDeviceQueueIndex = externalQueueIndex;
    mIsReleasedToExternal    = false;

    // Only change the layout and queue if the layout is anything by Undefined.  If it is undefined,
    // leave it to transition out as the image is used later.
    if (currentAccess != ImageAccess::Undefined)
    {
        changeLayoutAndQueue(context, getAspectFlags(), mCurrentAccess, newDeviceQueueIndex,
                             commandBuffer);
    }

    // It is unknown how the external has modified the image, so assume every subresource has
    // defined content.  That is unless the layout is Undefined.
    if (currentAccess == ImageAccess::Undefined)
    {
        setEntireContentUndefined();
    }
    else
    {
        setEntireContentDefined();
    }
}

void ImageHelper::releaseToExternal(Context *context,
                                    DeviceQueueIndex externalQueueIndex,
                                    ImageAccess expectedAccess,
                                    OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(!mIsReleasedToExternal);

    // A layout change is unnecessary if the image that was previously acquired was never used by
    // GL!
    if (mCurrentDeviceQueueIndex.familyIndex() != externalQueueIndex.familyIndex() ||
        mCurrentAccess != expectedAccess)
    {
        changeLayoutAndQueue(context, getAspectFlags(), expectedAccess, externalQueueIndex,
                             commandBuffer);
    }

    mIsReleasedToExternal = true;
}

VkImageMemoryBarrier ImageHelper::releaseToForeign(Renderer *renderer)
{
    ASSERT(mIsForeignImage);

    const ImageMemoryBarrierData barrierData = renderer->getImageMemoryBarrierData(mCurrentAccess);

    VkImageMemoryBarrier barrier        = {};
    barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask               = barrierData.srcAccessMask;
    barrier.dstAccessMask               = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout                   = barrierData.layout;
    barrier.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex         = renderer->getQueueFamilyIndex();
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_FOREIGN_EXT;
    barrier.image                       = mImage.getHandle();
    barrier.subresourceRange.aspectMask = getAspectFlags();
    barrier.subresourceRange.levelCount = mLevelCount;
    barrier.subresourceRange.layerCount = mLayerCount;

    mCurrentAccess               = ImageAccess::ForeignAccess;
    mCurrentDeviceQueueIndex     = kForeignDeviceQueueIndex;
    mLastNonShaderReadOnlyAccess = ImageAccess::Undefined;
    mCurrentShaderReadStageMask  = 0;

    return barrier;
}

LevelIndex ImageHelper::toVkLevel(gl::OwnerLevel levelIndexGL) const
{
    return gl_vk::GetLevelIndex(levelIndexGL, mFirstAllocatedLevel);
}

gl::OwnerLevel ImageHelper::toGLLevel(LevelIndex levelIndexVk) const
{
    return vk_gl::GetLevelIndex(levelIndexVk, mFirstAllocatedLevel);
}

ANGLE_INLINE void ImageHelper::initImageMemoryBarrierStruct(
    Renderer *renderer,
    VkImageAspectFlags aspectMask,
    ImageAccess newAccess,
    uint32_t newQueueFamilyIndex,
    VkImageMemoryBarrier *imageMemoryBarrier) const
{
    ASSERT(mCurrentDeviceQueueIndex.familyIndex() != QueueFamily::kInvalidIndex);
    ASSERT(newQueueFamilyIndex != QueueFamily::kInvalidIndex);

    const ImageMemoryBarrierData &transitionFrom =
        renderer->getImageMemoryBarrierData(mCurrentAccess);
    const ImageMemoryBarrierData &transitionTo = renderer->getImageMemoryBarrierData(newAccess);

    imageMemoryBarrier->sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier->srcAccessMask       = transitionFrom.srcAccessMask;
    imageMemoryBarrier->dstAccessMask       = transitionTo.dstAccessMask;
    imageMemoryBarrier->oldLayout           = renderer->getVkImageLayout(mCurrentAccess);
    imageMemoryBarrier->newLayout           = renderer->getVkImageLayout(newAccess);
    imageMemoryBarrier->srcQueueFamilyIndex = mCurrentDeviceQueueIndex.familyIndex();
    imageMemoryBarrier->dstQueueFamilyIndex = newQueueFamilyIndex;
    imageMemoryBarrier->image               = mImage.getHandle();

    // Transition the whole resource.
    imageMemoryBarrier->subresourceRange.aspectMask     = aspectMask;
    imageMemoryBarrier->subresourceRange.baseMipLevel   = 0;
    imageMemoryBarrier->subresourceRange.levelCount     = mLevelCount;
    imageMemoryBarrier->subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier->subresourceRange.layerCount     = mLayerCount;
}

// Generalized to accept both "primary" and "secondary" command buffers.
template <typename CommandBufferT>
void ImageHelper::barrierImpl(Renderer *renderer,
                              VkImageAspectFlags aspectMask,
                              ImageAccess newAccess,
                              DeviceQueueIndex newDeviceQueueIndex,
                              RefCountedEventCollector *eventCollector,
                              CommandBufferT *commandBuffer,
                              VkSemaphore *acquireNextImageSemaphoreOut)
{
    // Release the ANI semaphore to caller to add to the command submission.
    ASSERT(acquireNextImageSemaphoreOut != nullptr || !mAcquireNextImageSemaphore.valid());
    if (acquireNextImageSemaphoreOut != nullptr)
    {
        *acquireNextImageSemaphoreOut = mAcquireNextImageSemaphore.release();
    }

    if (mCurrentAccess == ImageAccess::SharedPresent)
    {
        const ImageMemoryBarrierData &transition =
            renderer->getImageMemoryBarrierData(mCurrentAccess);
        VkMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask   = transition.srcAccessMask;
        memoryBarrier.dstAccessMask   = transition.dstAccessMask;

        commandBuffer->memoryBarrier(transition.srcStageMask, transition.dstStageMask,
                                     memoryBarrier);
        return;
    }

    // Make sure we never transition out of SharedPresent
    ASSERT(mCurrentAccess != ImageAccess::SharedPresent || newAccess == ImageAccess::SharedPresent);

    const ImageMemoryBarrierData &transitionFrom =
        renderer->getImageMemoryBarrierData(mCurrentAccess);
    const ImageMemoryBarrierData &transitionTo = renderer->getImageMemoryBarrierData(newAccess);

    VkImageMemoryBarrier imageMemoryBarrier = {};
    initImageMemoryBarrierStruct(renderer, aspectMask, newAccess, newDeviceQueueIndex.familyIndex(),
                                 &imageMemoryBarrier);

    VkPipelineStageFlags dstStageMask = transitionTo.dstStageMask;

    // Fallback to pipelineBarrier if there is no event tracking image.
    // VkCmdWaitEvent requires the srcQueueFamilyIndex and dstQueueFamilyIndex members of any
    // element of pBufferMemoryBarriers or pImageMemoryBarriers must be equal
    // (VUID-vkCmdWaitEvents-srcQueueFamilyIndex-02803).
    BarrierType barrierType =
        mCurrentEvent.valid() && mCurrentDeviceQueueIndex == newDeviceQueueIndex
            ? BarrierType::Event
            : BarrierType::Pipeline;

    if (barrierType == BarrierType::Event)
    {
        // If there is an event, we use the waitEvent to do layout change. Once we have waited, the
        // event gets garbage collected (which is GPU completion tracked) to avoid waited again in
        // future. We always use DstStageMask since that is what setEvent used and
        // VUID-vkCmdWaitEvents-srcStageMask-01158 requires they must match.
        VkPipelineStageFlags srcStageMask =
            renderer->getPipelineStageMask(mCurrentEvent.getEventStage());
        commandBuffer->imageWaitEvent(mCurrentEvent.getEvent().getHandle(), srcStageMask,
                                      dstStageMask, imageMemoryBarrier);
        eventCollector->emplace_back(std::move(mCurrentEvent));
    }
    else
    {
        // There might be other shaderRead operations there other than the current layout.
        VkPipelineStageFlags srcStageMask = transitionFrom.srcStageMask;
        if (mCurrentShaderReadStageMask)
        {
            srcStageMask |= mCurrentShaderReadStageMask;
            mCurrentShaderReadStageMask  = 0;
            mLastNonShaderReadOnlyAccess = ImageAccess::Undefined;
        }
        commandBuffer->imageBarrier(srcStageMask, dstStageMask, imageMemoryBarrier);
    }

    mCurrentAccess           = newAccess;
    mCurrentDeviceQueueIndex = newDeviceQueueIndex;
    resetSubresourcesWrittenSinceBarrier();
}

template <typename CommandBufferT>
void ImageHelper::recordBarrierImpl(Context *context,
                                    VkImageAspectFlags aspectMask,
                                    ImageAccess newAccess,
                                    DeviceQueueIndex newDeviceQueueIndex,
                                    RefCountedEventCollector *eventCollector,
                                    CommandBufferT *commandBuffer,
                                    VkSemaphore *acquireNextImageSemaphoreOut)
{
    Renderer *renderer = context->getRenderer();
    // mCurrentEvent must be invalid if useVkEventForImageBarrieris disabled.
    ASSERT(renderer->getFeatures().useVkEventForImageBarrier.enabled || !mCurrentEvent.valid());

    if (mCurrentAccess == ImageAccess::SharedPresent)
    {
        // For now we always use pipelineBarrier for singlebuffer mode. We could use event here in
        // future.
        mCurrentEvent.release(context);
    }

    // The image has transitioned out of the FOREIGN queue.  Remember it so it can be transitioned
    // back on submission.
    if (mCurrentDeviceQueueIndex == kForeignDeviceQueueIndex)
    {
        context->onForeignImageUse(this);
    }

    barrierImpl(renderer, aspectMask, newAccess, newDeviceQueueIndex, eventCollector, commandBuffer,
                acquireNextImageSemaphoreOut);

    // We must release the event so that new event will be created and added. If we did not add new
    // event, because mCurrentEvent have been released, next barrier will automatically fallback to
    // pipelineBarrier. Otherwise if we keep mCurrentEvent here we may accidentally end up waiting
    // for an old event which creates sync hazard.
    mCurrentEvent.release(context);
}

void ImageHelper::recordBarrierOneOffImpl(Renderer *renderer,
                                          VkImageAspectFlags aspectMask,
                                          ImageAccess newAccess,
                                          DeviceQueueIndex newDeviceQueueIndex,
                                          PrimaryCommandBuffer *commandBuffer,
                                          VkSemaphore *acquireNextImageSemaphoreOut)
{
    // Release the event here to force pipelineBarrier.
    mCurrentEvent.release(renderer);
    ASSERT(mCurrentDeviceQueueIndex != kForeignDeviceQueueIndex);

    barrierImpl(renderer, aspectMask, newAccess, newDeviceQueueIndex, nullptr, commandBuffer,
                acquireNextImageSemaphoreOut);
}

void ImageHelper::setSubresourcesWrittenSinceBarrier(LevelIndex levelStart,
                                                     uint32_t levelCount,
                                                     gl::OwnerLayer layerStart,
                                                     uint32_t layerCount)
{
    // Updates are tracked by layer, which are always [0, 1) for 3D images.
    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        layerStart = gl::OwnerLayer(0);
        layerCount = 1;
    }

    for (uint32_t levelOffset = 0; levelOffset < levelCount; levelOffset++)
    {
        LevelIndex level = levelStart + levelOffset;
        if (layerCount >= kMaxParallelLayerWrites)
        {
            mSubresourcesWrittenSinceBarrier[level.get()].set();
        }
        else
        {
            ImageLayerWriteMask layerMask = GetImageLayerWriteMask(layerStart, layerCount);
            mSubresourcesWrittenSinceBarrier[level.get()] |= layerMask;
        }
    }
}

void ImageHelper::resetSubresourcesWrittenSinceBarrier()
{
    for (auto &layerWriteMask : mSubresourcesWrittenSinceBarrier)
    {
        layerWriteMask.reset();
    }
}

void ImageHelper::recordWriteBarrier(Context *context,
                                     VkImageAspectFlags aspectMask,
                                     ImageAccess newAccess,
                                     gl::OwnerLevel levelStart,
                                     uint32_t levelCount,
                                     gl::OwnerLayer layerStart,
                                     uint32_t layerCount,
                                     OutsideRenderPassCommandBufferHelper *commands)
{
    if (isWriteBarrierNecessary(newAccess, toVkLevel(levelStart), levelCount, layerStart,
                                layerCount))
    {
        ASSERT(!mCurrentEvent.valid() || !commands->hasSetEventPendingFlush(mCurrentEvent));
        VkSemaphore acquireNextImageSemaphore;
        recordBarrierImpl(context, aspectMask, newAccess, context->getDeviceQueueIndex(),
                          commands->getRefCountedEventCollector(), &commands->getCommandBuffer(),
                          &acquireNextImageSemaphore);

        if (acquireNextImageSemaphore != VK_NULL_HANDLE)
        {
            commands->setAcquireNextImageSemaphore(acquireNextImageSemaphore);
        }
    }

    setSubresourcesWrittenSinceBarrier(toVkLevel(levelStart), levelCount, layerStart, layerCount);
}

void ImageHelper::recordReadSubresourceBarrier(Context *context,
                                               VkImageAspectFlags aspectMask,
                                               ImageAccess newAccess,
                                               gl::OwnerLevel levelStart,
                                               uint32_t levelCount,
                                               gl::OwnerLayer layerStart,
                                               uint32_t layerCount,
                                               OutsideRenderPassCommandBufferHelper *commands)
{
    const LevelIndex levelStartVk = toVkLevel(levelStart);

    // This barrier is used for an image with both read/write permissions, including during mipmap
    // generation and self-copy.
    if (isReadSubresourceBarrierNecessary(newAccess, levelStartVk, levelCount, layerStart,
                                          layerCount))
    {
        ASSERT(!mCurrentEvent.valid() || !commands->hasSetEventPendingFlush(mCurrentEvent));
        VkSemaphore acquireNextImageSemaphore;
        recordBarrierImpl(context, aspectMask, newAccess, context->getDeviceQueueIndex(),
                          commands->getRefCountedEventCollector(), &commands->getCommandBuffer(),
                          &acquireNextImageSemaphore);

        if (acquireNextImageSemaphore != VK_NULL_HANDLE)
        {
            commands->setAcquireNextImageSemaphore(acquireNextImageSemaphore);
        }
    }

    // Levels/layers being read from are also registered to avoid RAW and WAR hazards.
    setSubresourcesWrittenSinceBarrier(levelStartVk, levelCount, layerStart, layerCount);
}

void ImageHelper::recordReadBarrier(Context *context,
                                    VkImageAspectFlags aspectMask,
                                    ImageAccess newAccess,
                                    OutsideRenderPassCommandBufferHelper *commands)
{
    if (!isReadBarrierNecessary(context->getRenderer(), newAccess))
    {
        return;
    }

    ASSERT(!mCurrentEvent.valid() || !commands->hasSetEventPendingFlush(mCurrentEvent));
    VkSemaphore acquireNextImageSemaphore;
    recordBarrierImpl(context, aspectMask, newAccess, context->getDeviceQueueIndex(),
                      commands->getRefCountedEventCollector(), &commands->getCommandBuffer(),
                      &acquireNextImageSemaphore);

    if (acquireNextImageSemaphore != VK_NULL_HANDLE)
    {
        commands->setAcquireNextImageSemaphore(acquireNextImageSemaphore);
    }
}

void ImageHelper::updateLayoutAndBarrier(Context *context,
                                         VkImageAspectFlags aspectMask,
                                         ImageAccess newAccess,
                                         BarrierType barrierType,
                                         const QueueSerial &queueSerial,
                                         PipelineBarrierArray *pipelineBarriers,
                                         EventBarrierArray *eventBarriers,
                                         RefCountedEventCollector *eventCollector,
                                         VkSemaphore *semaphoreOut)
{
    Renderer *renderer = context->getRenderer();
    ASSERT(queueSerial.valid());
    ASSERT(!mBarrierQueueSerial.valid() ||
           mBarrierQueueSerial.getIndex() != queueSerial.getIndex() ||
           mBarrierQueueSerial.getSerial() <= queueSerial.getSerial());
    ASSERT(renderer->getImageMemoryBarrierData(newAccess).barrierIndex !=
           PipelineStage::InvalidEnum);
    // mCurrentEvent must be invalid if useVkEventForImageBarrieris disabled.
    ASSERT(renderer->getFeatures().useVkEventForImageBarrier.enabled || !mCurrentEvent.valid());

    const bool hasQueueChange = mCurrentDeviceQueueIndex != context->getDeviceQueueIndex();
    if (hasQueueChange)
    {
        // Fallback to pipelineBarrier if the VkQueue has changed.
        barrierType = BarrierType::Pipeline;
        if (mCurrentDeviceQueueIndex == kForeignDeviceQueueIndex)
        {
            context->onForeignImageUse(this);
        }
    }
    else if (!mCurrentEvent.valid())
    {
        // Fallback to pipelineBarrier if there is no event tracking image.
        barrierType = BarrierType::Pipeline;
    }

    // Once you transition to ImageAccess::SharedPresent, you never transition out of it.
    if (mCurrentAccess == ImageAccess::SharedPresent)
    {
        newAccess = ImageAccess::SharedPresent;
    }

    if (newAccess == mCurrentAccess && !hasQueueChange)
    {
        if (mBarrierQueueSerial == queueSerial)
        {
            ASSERT(!mAcquireNextImageSemaphore.valid());
            // If there is no layout change and the previous layout change happened in the same
            // render pass, then early out do nothing. This can happen when the same image is
            // attached to the multiple attachments of the framebuffer.
            return;
        }

        const ImageMemoryBarrierData &layoutData =
            renderer->getImageMemoryBarrierData(mCurrentAccess);
        // RAR is not a hazard and doesn't require a barrier, especially as the image layout hasn't
        // changed.  The following asserts that such a barrier is not attempted.
        ASSERT(HasResourceWriteAccess(layoutData.type));

        // No layout change, only memory barrier is required
        if (barrierType == BarrierType::Event)
        {
            eventBarriers->addEventMemoryBarrier(renderer, mCurrentEvent, layoutData.dstAccessMask,
                                                 layoutData.dstStageMask, layoutData.dstAccessMask);
            // Garbage collect the event, which tracks GPU completion automatically.
            eventCollector->emplace_back(std::move(mCurrentEvent));
        }
        else
        {
            pipelineBarriers->mergeMemoryBarrier(layoutData.barrierIndex, layoutData.dstStageMask,
                                                 layoutData.dstStageMask, layoutData.srcAccessMask,
                                                 layoutData.dstAccessMask);

            // Release it. No need to garbage collect since we did not use the event here. ALl
            // previous use of event should garbage tracked already.
            mCurrentEvent.release(context);
        }
        mBarrierQueueSerial = queueSerial;
    }
    else
    {
        const ImageMemoryBarrierData &transitionFrom =
            renderer->getImageMemoryBarrierData(mCurrentAccess);
        const ImageMemoryBarrierData &transitionTo = renderer->getImageMemoryBarrierData(newAccess);
        VkPipelineStageFlags srcStageMask          = transitionFrom.srcStageMask;
        VkPipelineStageFlags dstStageMask          = transitionTo.dstStageMask;
        const bool isNewAccessShaderReadOnly       = IsShaderReadOnlyAccess(newAccess);

        if (transitionFrom.layout == transitionTo.layout && isNewAccessShaderReadOnly &&
            mBarrierQueueSerial == queueSerial && !hasQueueChange)
        {
            // If we are switching between different shader stage reads of the same render pass,
            // then there is no actual layout change or access type change. We only need a barrier
            // if we are making a read that is from a new stage. Also note that we do barrier
            // against previous non-shaderRead layout. We do not barrier between one shaderRead and
            // another shaderRead.
            bool isNewReadStage = (mCurrentShaderReadStageMask & dstStageMask) != dstStageMask;
            if (!isNewReadStage)
            {
                ASSERT(!mAcquireNextImageSemaphore.valid());
                return;
            }

            ASSERT(!mLastNonShaderReadOnlyEvent.valid() ||
                   mLastNonShaderReadOnlyEvent.getEventStage() ==
                       GetImageAccessEventStage(renderer, mLastNonShaderReadOnlyAccess));
            if (!mLastNonShaderReadOnlyEvent.valid())
            {
                barrierType = BarrierType::Pipeline;
            }

            if (barrierType == BarrierType::Event)
            {
                // If we already inserted a barrier in the same renderPass, we have to add
                // the new stage mask to the existing VkCmdWaitEvent call, otherwise VVL will
                // complain.
                eventBarriers->addAdditionalStageAccess(mLastNonShaderReadOnlyEvent, dstStageMask,
                                                        transitionTo.dstAccessMask);
                eventCollector->emplace_back(mLastNonShaderReadOnlyEvent);
            }
            else
            {
                const ImageMemoryBarrierData &layoutData =
                    renderer->getImageMemoryBarrierData(mLastNonShaderReadOnlyAccess);
                pipelineBarriers->mergeMemoryBarrier(
                    transitionTo.barrierIndex, layoutData.srcStageMask, dstStageMask,
                    layoutData.srcAccessMask, transitionTo.dstAccessMask);
            }

            mBarrierQueueSerial = queueSerial;
            // Accumulate new read stage.
            mCurrentShaderReadStageMask |= dstStageMask;

            // Since we used pipelineBarrier, release the event now to avoid wait for the
            // event again.
            if (mCurrentEvent.valid())
            {
                eventCollector->emplace_back(std::move(mCurrentEvent));
            }
        }
        else
        {
            VkImageMemoryBarrier imageMemoryBarrier = {};
            initImageMemoryBarrierStruct(renderer, aspectMask, newAccess,
                                         context->getDeviceQueueIndex().familyIndex(),
                                         &imageMemoryBarrier);

            if (transitionFrom.layout == transitionTo.layout && isNewAccessShaderReadOnly)
            {
                // If we are transiting within shaderReadOnly layout, i.e. reading from different
                // shader stages, VkEvent can't handle this right now. In order for VkEvent to
                // handle this properly we have to wait for the previous shaderReadOnly layout
                // transition event and add a new memoryBarrier. But we may have lost that event
                // already if it has been used in a new render pass (because we have to update the
                // event even if there is no barrier needed). To workaround this issue we fall back
                // to pipelineBarrier for now.
                barrierType = BarrierType::Pipeline;
            }
            else if (mBarrierQueueSerial == queueSerial)
            {
                // If we already inserted a barrier in this render pass, force to use
                // pipelineBarrier. Otherwise we will end up inserting a VkCmdWaitEvent that has not
                // been set (See https://issuetracker.google.com/333419317 for example).
                barrierType = BarrierType::Pipeline;
            }

            // if we transition from shaderReadOnly, we must add in stashed shader stage masks since
            // there might be outstanding shader reads from stages other than current layout. We do
            // not insert barrier between one shaderRead to another shaderRead
            if (mCurrentShaderReadStageMask)
            {
                if ((mCurrentShaderReadStageMask & srcStageMask) != mCurrentShaderReadStageMask)
                {
                    // mCurrentShaderReadStageMask has more bits than srcStageMask. This means it
                    // has been used by more than one shader stage in the same render pass. These
                    // two usages are tracked by two different ImageAccess, even though underlying
                    // VkImageLayout is the same. This means two different RefCountedEvents since
                    // each RefCountedEvent is associated with one ImageAccess. When we transit out
                    // of this layout, we must wait for all reads to finish. But Right now
                    // ImageHelper only keep track of the last read. To workaround this problem we
                    // use pipelineBarrier in this case.
                    barrierType = BarrierType::Pipeline;
                    srcStageMask |= mCurrentShaderReadStageMask;
                }
                mCurrentShaderReadStageMask  = 0;
                mLastNonShaderReadOnlyAccess = ImageAccess::Undefined;
                if (mLastNonShaderReadOnlyEvent.valid())
                {
                    mLastNonShaderReadOnlyEvent.release(context);
                }
            }

            // If we are transition into shaderRead layout, remember the last non-shaderRead layout
            // here.
            const bool isCurrentAccessShaderReadOnly = IsShaderReadOnlyAccess(mCurrentAccess);
            if (isNewAccessShaderReadOnly)
            {
                if (!isCurrentAccessShaderReadOnly)
                {
                    mLastNonShaderReadOnlyEvent.release(context);
                    mLastNonShaderReadOnlyAccess = mCurrentAccess;
                    mCurrentShaderReadStageMask  = dstStageMask;
                }
                else
                {
                    mCurrentShaderReadStageMask |= dstStageMask;
                }
            }

            if (barrierType == BarrierType::Event)
            {
                eventBarriers->addEventImageBarrier(renderer, mCurrentEvent, dstStageMask,
                                                    imageMemoryBarrier);
                if (isNewAccessShaderReadOnly && !isCurrentAccessShaderReadOnly)
                {
                    mLastNonShaderReadOnlyEvent = mCurrentEvent;
                }
                eventCollector->emplace_back(std::move(mCurrentEvent));
            }
            else
            {
                pipelineBarriers->mergeImageBarrier(transitionTo.barrierIndex, srcStageMask,
                                                    dstStageMask, imageMemoryBarrier);
                mCurrentEvent.release(context);
            }

            mBarrierQueueSerial = queueSerial;
        }
        mCurrentAccess = newAccess;
    }

    mCurrentDeviceQueueIndex = context->getDeviceQueueIndex();

    *semaphoreOut = mAcquireNextImageSemaphore.release();
    // We must release the event so that new event will be created and added. If we did not add new
    // event, because mCurrentEvent have been released, next barrier will automatically fallback to
    // pipelineBarrier. Otherwise if we keep mCurrentEvent here we may accidentally end up waiting
    // for an old event which creates sync hazard.
    ASSERT(!mCurrentEvent.valid());

    ASSERT(!IsShaderReadOnlyAccess(mLastNonShaderReadOnlyAccess));
}

void ImageHelper::setCurrentRefCountedEvent(Context *context,
                                            RefCountedEventArray *refCountedEventArray)
{
    ASSERT(context->getFeatures().useVkEventForImageBarrier.enabled);

    // If there is already an event, release it first.
    mCurrentEvent.release(context);

    // VkCmdSetEvent can remove the unnecessary GPU pipeline bubble that comes from false dependency
    // between fragment and vertex/transfer/compute stages. But it also comes with higher overhead.
    // In order to strike the balance, we exclude the images that are only used by one group of
    // pipeline stages in the past N references, where N is the heuristic window that we keep track
    // of. Use of VkEvent will not be beneficial if it is only accessed by one group of stages since
    // execution within the group is expected to be non-overlap.
    if (mPipelineStageAccessHeuristic == kPipelineStageAccessFragmentOnly ||
        mPipelineStageAccessHeuristic == kPipelineStageAccessPreFragmentOnly ||
        mPipelineStageAccessHeuristic == kPipelineStageAccessComputeOnly)
    {
        return;
    }

    // Create the event if we have not yet so. Otherwise just use the already created event. This
    // means all images used in the same render pass that has the same layout will be tracked by the
    // same event.
    EventStage eventStage = GetImageAccessEventStage(context->getRenderer(), mCurrentAccess);
    if (!refCountedEventArray->getEvent(eventStage).valid() &&
        !refCountedEventArray->initEventAtStage(context, eventStage))
    {
        // If VkEvent creation fail, we fallback to pipelineBarrier
        return;
    }

    // Copy the event to mCurrentEvent so that we can wait for it in future. This will add extra
    // refcount to the underlying VkEvent.
    mCurrentEvent = refCountedEventArray->getEvent(eventStage);
}

void ImageHelper::updatePipelineStageAccessHistory(Renderer *renderer)
{
    const ImageMemoryBarrierData &barrierData = renderer->getImageMemoryBarrierData(mCurrentAccess);
    mPipelineStageAccessHeuristic.onAccess(barrierData.pipelineStageGroup);
}

bool ImageHelper::areStagedUpdatesClearOnly()
{
    for (SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (SubresourceUpdate &update : levelUpdates)
        {
            if (!IsClear(update.updateSource))
            {
                return false;
            }
        }
    }
    return true;
}

void ImageHelper::clearColor(Renderer *renderer,
                             const VkClearColorValue &color,
                             LevelIndex baseMipLevelVk,
                             uint32_t levelCount,
                             LayerIndex baseArrayLayer,
                             uint32_t layerCount,
                             OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(valid());

    ASSERT(mCurrentAccess == ImageAccess::TransferDst ||
           mCurrentAccess == ImageAccess::SharedPresent);

    VkImageSubresourceRange range = {};
    range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel            = baseMipLevelVk.get();
    range.levelCount              = levelCount;
    range.baseArrayLayer          = baseArrayLayer.get();
    range.layerCount              = layerCount;

    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        ASSERT(baseArrayLayer == LayerIndex(0));
        ASSERT(layerCount == static_cast<uint32_t>(getLevelExtents(baseMipLevelVk).depth));
        range.layerCount = 1;
    }

    commandBuffer->clearColorImage(mImage, getCurrentLayout(renderer), color, 1, &range);

    onWrite(baseMipLevelVk, 1, baseArrayLayer, layerCount, VK_IMAGE_ASPECT_COLOR_BIT);
}

void ImageHelper::clearDepthStencil(Renderer *renderer,
                                    VkImageAspectFlags clearAspectFlags,
                                    const VkClearDepthStencilValue &depthStencil,
                                    LevelIndex baseMipLevelVk,
                                    uint32_t levelCount,
                                    LayerIndex baseArrayLayer,
                                    uint32_t layerCount,
                                    OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(valid());

    ASSERT(mCurrentAccess == ImageAccess::TransferDst);

    VkImageSubresourceRange range = {};
    range.aspectMask              = clearAspectFlags;
    range.baseMipLevel            = baseMipLevelVk.get();
    range.levelCount              = levelCount;
    range.baseArrayLayer          = baseArrayLayer.get();
    range.layerCount              = layerCount;

    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        ASSERT(baseArrayLayer == LayerIndex(0));
        ASSERT(layerCount == static_cast<uint32_t>(getLevelExtents(baseMipLevelVk).depth));
        range.layerCount = 1;
    }

    commandBuffer->clearDepthStencilImage(mImage, getCurrentLayout(renderer), depthStencil, 1,
                                          &range);

    onWrite(baseMipLevelVk, 1, baseArrayLayer, layerCount, clearAspectFlags);
}

void ImageHelper::clear(Renderer *renderer,
                        VkImageAspectFlags aspectFlags,
                        const VkClearValue &value,
                        LevelIndex mipLevel,
                        LayerIndex baseArrayLayer,
                        uint32_t layerCount,
                        OutsideRenderPassCommandBuffer *commandBuffer)
{
    const angle::Format &angleFormat = getActualFormat();
    bool isDepthStencil              = angleFormat.hasDepthOrStencilBits();

    if (isDepthStencil)
    {
        clearDepthStencil(renderer, aspectFlags, value.depthStencil, mipLevel, 1, baseArrayLayer,
                          layerCount, commandBuffer);
    }
    else
    {
        ASSERT(!angleFormat.isBlock);

        clearColor(renderer, value.color, mipLevel, 1, baseArrayLayer, layerCount, commandBuffer);
    }
}

angle::Result ImageHelper::clearPartial(ContextVk *contextVk,
                                        VkImageAspectFlags aspectFlags,
                                        const VkClearValue &value,
                                        LevelIndex mipLevel,
                                        LayerIndex baseArrayLayer,
                                        uint32_t layerCount,
                                        const gl::Rectangle &clearArea,
                                        OutsideRenderPassCommandBufferHelper **commandBuffer)
{
    // clearTexture() uses LOAD_OP_CLEAR in a render pass to clear the texture. If
    // the texture has the depth dimension or multiple layers, the clear will be
    // performed layer by layer.
    UtilsVk::ClearTextureParameters params = {};
    params.aspectFlags                     = aspectFlags;
    params.level                           = mipLevel;
    params.clearArea                       = clearArea;
    params.clearValue                      = value;

    for (gl::OwnerLayer layerIndex = baseArrayLayer; layerIndex < baseArrayLayer + layerCount;
         ++layerIndex)
    {
        params.layer = layerIndex;
        // Note: clearTexture() starts a render pass that automatically marks the
        // contents of the subresource as defined.  onWrite is
        // unnecessary.
        ANGLE_TRY(contextVk->getUtils().clearTexture(contextVk, this, params));
    }

    // Queue serial index becomes invalid after starting render pass for the op
    // above. Therefore, the outside command buffer should be re-acquired.
    return contextVk->getOutsideRenderPassCommandBufferHelper({}, commandBuffer);
}

angle::Result ImageHelper::clearEmulatedChannels(ContextVk *contextVk,
                                                 VkColorComponentFlags colorMaskFlags,
                                                 const VkClearValue &value,
                                                 LevelIndex mipLevel,
                                                 LayerIndex baseArrayLayer,
                                                 uint32_t layerCount)
{
    const gl::Extents levelExtents = getLevelExtents(mipLevel);

    if (levelExtents.depth > 1)
    {
        // Currently not implemented for 3D textures
        UNIMPLEMENTED();
        return angle::Result::Continue;
    }

    UtilsVk::ClearImageParameters params = {};
    params.clearArea                     = {0, 0, levelExtents.width, levelExtents.height};
    params.dstMip                        = mipLevel;
    params.colorMaskFlags                = colorMaskFlags;
    params.colorClearValue               = value.color;

    for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
    {
        params.dstLayer = baseArrayLayer + layerIndex;

        ANGLE_TRY(contextVk->getUtils().clearImage(contextVk, this, params));
    }

    return angle::Result::Continue;
}

// static
void ImageHelper::Copy(Renderer *renderer,
                       ImageHelper *srcImage,
                       ImageHelper *dstImage,
                       const gl::Offset &srcOffset,
                       const gl::Offset &dstOffset,
                       const gl::Extents &copySize,
                       const VkImageSubresourceLayers &srcSubresource,
                       const VkImageSubresourceLayers &dstSubresource,
                       OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(commandBuffer->valid() && srcImage->valid() && dstImage->valid());

    ASSERT(srcImage->getCurrentImageAccess() == ImageAccess::TransferSrc);
    ASSERT(dstImage->getCurrentImageAccess() == ImageAccess::TransferDst);

    VkImageCopy region    = {};
    region.srcSubresource = srcSubresource;
    region.srcOffset.x    = srcOffset.x;
    region.srcOffset.y    = srcOffset.y;
    region.srcOffset.z    = srcOffset.z;
    region.dstSubresource = dstSubresource;
    region.dstOffset.x    = dstOffset.x;
    region.dstOffset.y    = dstOffset.y;
    region.dstOffset.z    = dstOffset.z;
    region.extent.width   = copySize.width;
    region.extent.height  = copySize.height;
    region.extent.depth   = copySize.depth;

    commandBuffer->copyImage(srcImage->getImage(), srcImage->getCurrentLayout(renderer),
                             dstImage->getImage(), dstImage->getCurrentLayout(renderer), 1,
                             &region);
}

// static
angle::Result ImageHelper::CopyImageSubData(const gl::Context *context,
                                            ImageHelper *srcImage,
                                            gl::OwnerLevel srcLevelGL,
                                            GLint srcX,
                                            GLint srcY,
                                            gl::OwnerLayer srcZ,
                                            ImageHelper *dstImage,
                                            gl::OwnerLevel dstLevelGL,
                                            GLint dstX,
                                            GLint dstY,
                                            gl::OwnerLayer dstZ,
                                            GLsizei srcWidth,
                                            GLsizei srcHeight,
                                            GLsizei srcDepth)
{
    ContextVk *contextVk = GetImpl(context);
    Renderer *renderer   = contextVk->getRenderer();

    if (CanCopyWithTransferForCopyImage(renderer, srcImage, dstImage))
    {
        bool isSrc3D                         = srcImage->getType() == VK_IMAGE_TYPE_3D;
        bool isDst3D                         = dstImage->getType() == VK_IMAGE_TYPE_3D;
        const VkImageAspectFlags aspectFlags = srcImage->getAspectFlags();
        const gl::OwnerLayer srcBaseLayer    = isSrc3D ? gl::OwnerLayer(0) : srcZ;
        const gl::OwnerLayer dstBaseLayer    = isDst3D ? gl::OwnerLayer(0) : dstZ;

        ASSERT(srcImage->getAspectFlags() == dstImage->getAspectFlags());

        VkImageCopy region = {};

        region.srcSubresource.aspectMask     = aspectFlags;
        region.srcSubresource.mipLevel       = srcImage->toVkLevel(srcLevelGL).get();
        region.srcSubresource.baseArrayLayer = srcBaseLayer.get();
        region.srcSubresource.layerCount     = isSrc3D ? 1 : srcDepth;

        region.dstSubresource.aspectMask     = aspectFlags;
        region.dstSubresource.mipLevel       = dstImage->toVkLevel(dstLevelGL).get();
        region.dstSubresource.baseArrayLayer = dstBaseLayer.get();
        region.dstSubresource.layerCount     = isDst3D ? 1 : srcDepth;

        region.srcOffset.x   = srcX;
        region.srcOffset.y   = srcY;
        region.srcOffset.z   = isSrc3D ? srcZ.get() : 0;
        region.dstOffset.x   = dstX;
        region.dstOffset.y   = dstY;
        region.dstOffset.z   = isDst3D ? dstZ.get() : 0;
        region.extent.width  = srcWidth;
        region.extent.height = srcHeight;
        region.extent.depth  = (isSrc3D || isDst3D) ? srcDepth : 1;

        CommandResources resources;
        if (srcImage == dstImage)
        {
            resources.onImageSelfCopy(srcLevelGL, 1, srcBaseLayer, region.srcSubresource.layerCount,
                                      dstLevelGL, 1, dstBaseLayer, region.dstSubresource.layerCount,
                                      aspectFlags, srcImage);
        }
        else
        {
            resources.onImageTransferRead(aspectFlags, srcImage);
            resources.onImageTransferWrite(dstLevelGL, 1, dstBaseLayer,
                                           region.dstSubresource.layerCount, aspectFlags, dstImage);
        }

        OutsideRenderPassCommandBuffer *commandBuffer;
        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

        ASSERT(srcImage->valid() && dstImage->valid());
        ASSERT(srcImage->getCurrentImageAccess() == ImageAccess::TransferSrc ||
               srcImage->getCurrentImageAccess() == ImageAccess::TransferSrcDst);
        ASSERT(dstImage->getCurrentImageAccess() == ImageAccess::TransferDst ||
               dstImage->getCurrentImageAccess() == ImageAccess::TransferSrcDst);

        // Some drivers copy wrong data when the formats differ and the region covers more than
        // one layer; issue one copy per layer instead.  No barriers are needed between them.
        const bool splitPerLayer =
            renderer->getFeatures().splitCrossFormatLayeredImageCopy.enabled &&
            srcImage->getActualFormatID() != dstImage->getActualFormatID() &&
            !srcImage->getActualFormat().isBlock && !dstImage->getActualFormat().isBlock &&
            srcDepth > 1 && !(isSrc3D && isDst3D);

        if (!splitPerLayer)
        {
            commandBuffer->copyImage(srcImage->getImage(), srcImage->getCurrentLayout(renderer),
                                     dstImage->getImage(), dstImage->getCurrentLayout(renderer), 1,
                                     &region);
        }
        else
        {
            VkImageCopy layerRegion               = region;
            layerRegion.srcSubresource.layerCount = 1;
            layerRegion.dstSubresource.layerCount = 1;
            layerRegion.extent.depth              = 1;

            for (uint32_t layer = 0; layer < static_cast<uint32_t>(srcDepth); ++layer)
            {
                // copyImage() records a single region, so issue one call per layer.
                commandBuffer->copyImage(srcImage->getImage(), srcImage->getCurrentLayout(renderer),
                                         dstImage->getImage(), dstImage->getCurrentLayout(renderer),
                                         1, &layerRegion);

                if (isSrc3D)
                {
                    ++layerRegion.srcOffset.z;
                }
                else
                {
                    ++layerRegion.srcSubresource.baseArrayLayer;
                }

                if (isDst3D)
                {
                    ++layerRegion.dstOffset.z;
                }
                else
                {
                    ++layerRegion.dstSubresource.baseArrayLayer;
                }
            }
        }
    }
    else if (!srcImage->getIntendedFormat().isBlock && !dstImage->getIntendedFormat().isBlock)
    {
        // The source and destination image formats may be using a fallback in the case of RGB
        // images.  A compute shader is used in such a case to perform the copy.
        UtilsVk &utilsVk = contextVk->getUtils();

        UtilsVk::CopyImageBitsParameters params = {};
        params.srcOffset[0]   = srcX;
        params.srcOffset[1]   = srcY;
        params.srcOffset[2]   = srcZ.get();
        params.srcLevel       = srcLevelGL;
        params.dstOffset[0]   = dstX;
        params.dstOffset[1]   = dstY;
        params.dstOffset[2]   = dstZ.get();
        params.dstLevel       = dstLevelGL;
        params.copyExtents[0] = srcWidth;
        params.copyExtents[1] = srcHeight;
        params.copyExtents[2] = srcDepth;

        ANGLE_TRY(utilsVk.copyImageBits(contextVk, dstImage, srcImage, params));
    }
    else
    {
        // No support for emulated compressed formats.
        UNIMPLEMENTED();
        ANGLE_VK_CHECK(contextVk, false, VK_ERROR_FEATURE_NOT_PRESENT);
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::generateMipmapsWithBlit(ContextVk *contextVk,
                                                   LevelIndex baseLevel,
                                                   LevelIndex maxLevel)
{
    Renderer *renderer = contextVk->getRenderer();

    CommandResources resources;
    gl::OwnerLevel baseLevelGL = toGLLevel(baseLevel);
    resources.onImageTransferWrite(baseLevelGL + 1,
                                   std::min(mLevelCount - 1, maxLevel.get()) - baseLevel.get(),
                                   gl::OwnerLayer(0), mLayerCount, VK_IMAGE_ASPECT_COLOR_BIT, this);

    OutsideRenderPassCommandBuffer *commandBuffer;
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

    // We are able to use blitImage since the image format we are using supports it.
    int32_t mipWidth  = mExtents.width;
    int32_t mipHeight = mExtents.height;
    int32_t mipDepth  = mExtents.depth;

    // Manually manage the image memory barrier because it uses a lot more parameters than our
    // usual one.
    VkImageMemoryBarrier barrier            = {};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image                           = mImage.getHandle();
    barrier.oldLayout                       = getCurrentLayout(renderer);
    barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = mLayerCount;
    barrier.subresourceRange.levelCount     = 1;

    // The barriers can be simplified if the image layout does not need to change.
    const bool isGeneralLayout    = getCurrentLayout(renderer) == VK_IMAGE_LAYOUT_GENERAL;
    VkMemoryBarrier globalBarrier = {};
    globalBarrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    globalBarrier.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
    globalBarrier.dstAccessMask   = VK_ACCESS_TRANSFER_READ_BIT;

    const VkImageLayout blitSrcLayout =
        isGeneralLayout ? VK_IMAGE_LAYOUT_GENERAL : barrier.newLayout;
    const VkImageLayout blitDstLayout = barrier.oldLayout;

    const VkFilter filter =
        gl_vk::GetFilter(CalculateGenerateMipmapFilter(contextVk, getActualFormatID()));

    for (LevelIndex mipLevel(1); mipLevel <= LevelIndex(mLevelCount); ++mipLevel)
    {
        int32_t nextMipWidth  = std::max<int32_t>(1, mipWidth >> 1);
        int32_t nextMipHeight = std::max<int32_t>(1, mipHeight >> 1);
        int32_t nextMipDepth  = std::max<int32_t>(1, mipDepth >> 1);

        if (mipLevel > baseLevel && mipLevel <= maxLevel)
        {
            if (isGeneralLayout)
            {
                commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             VK_PIPELINE_STAGE_TRANSFER_BIT, globalBarrier);
            }
            else
            {
                barrier.subresourceRange.baseMipLevel = mipLevel.get() - 1;

                commandBuffer->imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT, barrier);
            }

            // We can do it for all layers at once.
            VkImageBlit blit                   = {};
            blit.srcOffsets[0]                 = {0, 0, 0};
            blit.srcOffsets[1]                 = {mipWidth, mipHeight, mipDepth};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = mipLevel.get() - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = mLayerCount;
            blit.dstOffsets[0]                 = {0, 0, 0};
            blit.dstOffsets[1]                 = {nextMipWidth, nextMipHeight, nextMipDepth};
            blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel       = mipLevel.get();
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount     = mLayerCount;

            commandBuffer->blitImage(mImage, blitSrcLayout, mImage, blitDstLayout, 1, &blit,
                                     filter);
        }
        mipWidth  = nextMipWidth;
        mipHeight = nextMipHeight;
        mipDepth  = nextMipDepth;
    }

    // If the GENERAL layout is not used, the layout of the mips need to be unified because we
    // really only track one layout for the whole image.
    if (!isGeneralLayout)
    {
        // Transition all mip level to the same layout so we can declare our whole image layout to
        // one ImageAccess. FragmentShaderReadOnly is picked here since this is the most reasonable
        // usage after glGenerateMipmap call.
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        if (baseLevel.get() > 0)
        {
            // [0:baseLevel-1] from TRANSFER_DST to SHADER_READ
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount   = baseLevel.get();
            commandBuffer->imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, barrier);
        }
        // [maxLevel:mLevelCount-1] from TRANSFER_DST to SHADER_READ
        ASSERT(mLevelCount > maxLevel.get());
        barrier.subresourceRange.baseMipLevel = maxLevel.get();
        barrier.subresourceRange.levelCount   = mLevelCount - maxLevel.get();
        commandBuffer->imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, barrier);
        // [baseLevel:maxLevel-1] from TRANSFER_SRC to SHADER_READ
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.subresourceRange.baseMipLevel = baseLevel.get();
        barrier.subresourceRange.levelCount   = maxLevel.get() - baseLevel.get();
        commandBuffer->imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, barrier);

        // This is just changing the internal state of the image helper so that the next call
        // to changeLayout will use this layout as the "oldLayout" argument.
        // mLastNonShaderReadOnlyAccess is used to ensure previous write are made visible to reads,
        // since the only write here is transfer, hence mLastNonShaderReadOnlyAccess is set to
        // ImageAccess::TransferDst.
        setCurrentImageAccess(renderer, ImageAccess::FragmentShaderReadOnly);
    }
    else
    {
        // onImageTransferWrite already declares that the image is being written to.  Set the access
        // to TransferSrcDst for correct future synchronization.
        setCurrentImageAccess(renderer, ImageAccess::TransferSrcDst);
    }

    contextVk->trackImageWithOutsideRenderPassEvent(this);

    return angle::Result::Continue;
}

void ImageHelper::copy(Renderer *renderer,
                       ImageHelper *dst,
                       const VkImageCopy &region,
                       OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(mCurrentAccess == ImageAccess::TransferSrc ||
           mCurrentAccess == ImageAccess::SharedPresent);
    ASSERT(dst->getCurrentImageAccess() == ImageAccess::TransferDst ||
           dst->getCurrentImageAccess() == ImageAccess::SharedPresent);
    // Source image must be up-to-date.
    ASSERT(verifyNoStagedUpdates());
    // Dest image might have clears staged due to robustness or format emulation, which can be
    // dropped.  Note that this function is only used with swapchain images, so there is no need to
    // be specific about which subresource updates to drop.
    ASSERT(dst->getLevelCount() == 1 && dst->getLayerCount() == 1);
    ASSERT(dst->verifyNoStagedUpdates() || dst->areStagedUpdatesClearOnly());
    dst->mSubresourceUpdates.clear();
    commandBuffer->copyImage(getImage(), getCurrentLayout(renderer), dst->getImage(),
                             dst->getCurrentLayout(renderer), 1, &region);
}

void ImageHelper::resolve(Renderer *renderer,
                          ImageHelper *dst,
                          const VkImageResolve &region,
                          OutsideRenderPassCommandBuffer *commandBuffer)
{
    ASSERT(mCurrentAccess == ImageAccess::TransferSrc ||
           mCurrentAccess == ImageAccess::SharedPresent);
    ASSERT(dst->getCurrentImageAccess() == ImageAccess::TransferDst ||
           dst->getCurrentImageAccess() == ImageAccess::SharedPresent);
    commandBuffer->resolveImage(getImage(), getCurrentLayout(renderer), dst->getImage(),
                                dst->getCurrentLayout(renderer), 1, &region);
}

void ImageHelper::removeSingleSubresourceStagedUpdates(ContextVk *contextVk,
                                                       gl::OwnerLevel levelIndexGL,
                                                       gl::OwnerLayer layerIndex,
                                                       uint32_t layerCount)
{
    // Find any staged updates for this index and remove them from the pending list.
    SubresourceUpdates *levelUpdates = getLevelUpdates(levelIndexGL);
    if (levelUpdates == nullptr)
    {
        return;
    }

    for (size_t index = 0; index < levelUpdates->size();)
    {
        auto update = levelUpdates->begin() + index;
        if (matchesLayerRange(*update, levelIndexGL, layerIndex, layerCount))
        {
            // Update total staging buffer size
            mTotalStagedBufferUpdateSize -= update->updateSource == UpdateSource::Buffer
                                                ? update->data.buffer.bufferHelper->getSize()
                                                : 0;
            update->release(contextVk->getRenderer());
            levelUpdates->erase(update);
        }
        else
        {
            // The layer range should either match the update, or not intersect with it.  If this
            // assertion fails, the update should be pertially removed, but is retained which is
            // incorrect.
            ASSERT(!intersectsLayerRange(*update, levelIndexGL, layerIndex, layerCount));
            index++;
        }
    }
}


void ImageHelper::removeStagedUpdates(ErrorContext *context,
                                      gl::OwnerLevel levelGLStart,
                                      gl::OwnerLevel levelGLEnd)
{
    assertSubresourceUpdateRefCountsConsistent();

    // Remove all updates to levels [start, end].
    for (gl::OwnerLevel level = levelGLStart; level <= levelGLEnd; ++level)
    {
        SubresourceUpdates *levelUpdates = getLevelUpdates(level);
        if (levelUpdates == nullptr)
        {
            ASSERT(static_cast<size_t>(level.get()) >= mSubresourceUpdates.size());
            return;
        }

        for (SubresourceUpdate &update : *levelUpdates)
        {
            // Update total staging buffer size
            mTotalStagedBufferUpdateSize -= update.updateSource == UpdateSource::Buffer
                                                ? update.data.buffer.bufferHelper->getSize()
                                                : 0;
            update.release(context->getRenderer());
        }

        levelUpdates->clear();
    }

    assertSubresourceUpdateRefCountsConsistent();
}

void ImageHelper::redefineLevels(ErrorContext *context,
                                 gl::OwnerLevel levelGLStart,
                                 gl::OwnerLevel levelGLEnd)
{
    removeStagedUpdates(context, levelGLStart, levelGLEnd);
    if (valid())
    {
        for (gl::OwnerLevel level = levelGLStart; level <= levelGLEnd; ++level)
        {
            if (level >= getFirstAllocatedLevel() && level <= getLastAllocatedLevel())
            {
                invalidateEntireLevelContent(context, level);
                if ((getAspectFlags() & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
                {
                    invalidateEntireLevelStencilContent(context, level);
                }
            }
        }
    }
}

void ImageHelper::redefineSingleSubresource(ContextVk *contextVk,
                                            gl::OwnerLevel levelIndexGL,
                                            gl::OwnerLayer layerIndex,
                                            uint32_t layerCount)
{
    removeSingleSubresourceStagedUpdates(contextVk, levelIndexGL, layerIndex, layerCount);

    if (valid())
    {
        if (levelIndexGL >= getFirstAllocatedLevel() && levelIndexGL <= getLastAllocatedLevel())
        {
            invalidateSubresourceContent(contextVk, levelIndexGL, layerIndex, layerCount, nullptr);
            if ((getAspectFlags() & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
            {
                invalidateSubresourceStencilContent(contextVk, levelIndexGL, layerIndex, layerCount,
                                                    nullptr);
            }
        }
    }
}

angle::Result ImageHelper::stageSubresourceUpdate(ContextVk *contextVk,
                                                  const gl::OwnerImageIndex &index,
                                                  const gl::Extents &glExtents,
                                                  const gl::Offset &offset,
                                                  const gl::InternalFormat &formatInfo,
                                                  GLenum type,
                                                  const uint8_t *pixels,
                                                  const Format &vkFormat,
                                                  ImageFormatSupport formatSupport,
                                                  const GLuint inputRowPitch,
                                                  const GLuint inputDepthPitch,
                                                  const GLuint inputSkipBytes,
                                                  ApplyImageUpdate applyUpdate,
                                                  bool *updateAppliedImmediatelyOut)
{
    *updateAppliedImmediatelyOut = false;

    const angle::Format &storageFormat = vkFormat.getActualImageFormat(formatSupport);
    angle::FormatID storageFormatID    = storageFormat.id;

    size_t outputRowPitch;
    size_t outputDepthPitch;
    size_t stencilAllocationSize = 0;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    size_t allocationSize;

    LoadImageFunctionInfo loadFunctionInfo = vkFormat.getTextureLoadFunction(formatSupport, type);
    LoadImageFunction stencilLoadFunction  = nullptr;

    // In case the flag for BGR565 usage is enabled, we should make sure that the actual format ID
    // for the image is also BGR565. Otherwise, the loading functions for RGB565 should be used for
    // correct data conversions.
    if (mActualFormatID == angle::FormatID::R5G6B5_UNORM &&
        storageFormatID == angle::FormatID::B5G6R5_UNORM)
    {
        ASSERT(mIntendedFormatID == angle::FormatID::R5G6B5_UNORM);
        loadFunctionInfo = Format::GetRGB565TextureLoadFunction(contextVk->getRenderer())(type);
        storageFormatID  = angle::FormatID::R5G6B5_UNORM;
    }

    bool useComputeTransCoding = false;
    if (storageFormat.isBlock)
    {
        const gl::InternalFormat &storageFormatInfo = vkFormat.getInternalFormatInfo(type);
        GLuint rowPitch;
        GLuint depthPitch;
        GLuint totalSize;

        ANGLE_VK_CHECK_MATH(contextVk, storageFormatInfo.computeCompressedImageRowPitch(
                                           glExtents.width, &rowPitch));
        ANGLE_VK_CHECK_MATH(contextVk, storageFormatInfo.computeCompressedImageDepthPitch(
                                           glExtents.height, rowPitch, &depthPitch));

        ANGLE_VK_CHECK_MATH(contextVk,
                            storageFormatInfo.computeCompressedImageSize(glExtents, &totalSize));

        outputRowPitch   = rowPitch;
        outputDepthPitch = depthPitch;
        allocationSize   = totalSize;

        ANGLE_VK_CHECK_MATH(
            contextVk, storageFormatInfo.computeBufferRowLength(glExtents.width, &bufferRowLength));
        ANGLE_VK_CHECK_MATH(contextVk, storageFormatInfo.computeBufferImageHeight(
                                           glExtents.height, &bufferImageHeight));

        if (contextVk->getFeatures().supportsComputeTranscodeEtcToBc.enabled &&
            IsETCFormat(vkFormat.getIntendedFormatID()) && IsBCFormat(storageFormatID))
        {
            useComputeTransCoding =
                shouldUseComputeForTransCoding(toVkLevel(index.getLevelIndex()));
            if (!useComputeTransCoding)
            {
                loadFunctionInfo = GetEtcToBcTransCodingFunc(vkFormat.getIntendedFormatID());
            }
        }
    }
    else
    {
        ASSERT(storageFormat.pixelBytes != 0);
        const bool stencilOnly = formatInfo.sizedInternalFormat == GL_STENCIL_INDEX8;

        if (!stencilOnly && storageFormatID == angle::FormatID::D24_UNORM_S8_UINT)
        {
            switch (type)
            {
                case GL_UNSIGNED_INT_24_8:
                    stencilLoadFunction = angle::LoadX24S8ToS8;
                    break;
                case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
                    stencilLoadFunction = angle::LoadX32S8ToS8;
                    break;
            }
        }
        if (!stencilOnly && storageFormatID == angle::FormatID::D32_FLOAT_S8X24_UINT)
        {
            // If depth is D32FLOAT_S8, we must pack D32F tightly (no stencil) for CopyBufferToImage
            outputRowPitch = sizeof(float) * glExtents.width;

            // The generic load functions don't handle tightly packing D32FS8 to D32F & S8 so call
            // special case load functions.
            switch (type)
            {
                case GL_UNSIGNED_INT:
                    loadFunctionInfo.loadFunction = angle::LoadD32ToD32F;
                    stencilLoadFunction           = nullptr;
                    break;
                case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
                    loadFunctionInfo.loadFunction = angle::LoadD32FS8X24ToD32F;
                    stencilLoadFunction           = angle::LoadX32S8ToS8;
                    break;
                case GL_UNSIGNED_INT_24_8:
                    loadFunctionInfo.loadFunction = angle::LoadD24S8ToD32F;
                    stencilLoadFunction           = angle::LoadX24S8ToS8;
                    break;
                default:
                    UNREACHABLE();
            }
        }
        else if (!stencilOnly)
        {
            outputRowPitch = static_cast<size_t>(glExtents.width) * storageFormat.pixelBytes;
        }
        else
        {
            // Some Vulkan implementations do not support S8_UINT, so stencil-only data is
            // uploaded using one of combined depth-stencil formats there. Since the uploaded
            // stencil data must be tightly packed, the actual storage format should be ignored
            // with regards to its load function and output row pitch.
            loadFunctionInfo.loadFunction = angle::LoadToNative<GLubyte, 1>;
            outputRowPitch                = glExtents.width;
        }
        outputDepthPitch = outputRowPitch * glExtents.height;

        bufferRowLength   = glExtents.width;
        bufferImageHeight = glExtents.height;

        allocationSize = outputDepthPitch * glExtents.depth;

        // Note: because the LoadImageFunctionInfo functions are limited to copying a single
        // component, we have to special case packed depth/stencil use and send the stencil as a
        // separate chunk.
        if (storageFormat.hasDepthAndStencilBits() && formatInfo.depthBits > 0 &&
            formatInfo.stencilBits > 0)
        {
            // Note: Stencil is always one byte
            stencilAllocationSize = static_cast<size_t>(glExtents.width) *
                                    static_cast<size_t>(glExtents.height) *
                                    static_cast<size_t>(glExtents.depth);
            allocationSize += stencilAllocationSize;
        }
    }

    const uint8_t *source = ANGLE_UNSAFE_TODO(pixels + static_cast<ptrdiff_t>(inputSkipBytes));

    // If possible, copy the buffer to the image directly on the host, to avoid having to use a temp
    // image (and do a double copy).
    if (applyUpdate != ApplyImageUpdate::Defer && !loadFunctionInfo.requiresConversion &&
        inputRowPitch == outputRowPitch && inputDepthPitch == outputDepthPitch)
    {
        bool copied = false;
        ANGLE_TRY(updateSubresourceOnHost(contextVk, applyUpdate, index, glExtents, offset, source,
                                          bufferRowLength, bufferImageHeight, &copied));
        if (copied)
        {
            *updateAppliedImmediatelyOut = true;
            return angle::Result::Continue;
        }
    }

    std::unique_ptr<RefCounted<BufferHelper>> stagingBuffer =
        std::make_unique<RefCounted<BufferHelper>>();
    BufferHelper *currentBuffer = &stagingBuffer->get();

    uint8_t *stagingPointer;
    VkDeviceSize stagingOffset;
    ANGLE_TRY(contextVk->initBufferForImageCopy(currentBuffer, allocationSize,
                                                MemoryCoherency::CachedNonCoherent, storageFormatID,
                                                &stagingOffset, &stagingPointer));

    loadFunctionInfo.loadFunction(
        contextVk->getImageLoadContext(), glExtents.width, glExtents.height, glExtents.depth,
        source, inputRowPitch, inputDepthPitch, stagingPointer, outputRowPitch, outputDepthPitch);

    // YUV formats need special handling.
    if (storageFormat.isYUV)
    {
        gl::YuvFormatInfo yuvInfo(formatInfo.internalFormat, glExtents);

        static constexpr std::array<VkImageAspectFlagBits, 3> kPlaneAspectFlags = {
            VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT};

        // We only support mip level 0 and layerCount of 1 for YUV formats.
        ASSERT(index.getLevelIndex().get() == 0);
        ASSERT(index.getLayerCount() == 1);

        int hSub = 1, vSub = 1;
        gl::GetSubSampleFactor(formatInfo.internalFormat, &hSub, &vSub);

        for (uint32_t plane = 0; plane < yuvInfo.planeCount; plane++)
        {
            gl::Offset planeOffset = offset;
            if (plane > 0)
            {
                planeOffset.x = offset.x / hSub;
                planeOffset.y = offset.y / vSub;
            }

            VkBufferImageCopy copy           = {};
            copy.bufferOffset                = stagingOffset + yuvInfo.planeOffset[plane];
            copy.bufferRowLength             = 0;
            copy.bufferImageHeight           = 0;
            copy.imageSubresource.mipLevel   = 0;
            copy.imageSubresource.layerCount = 1;
            gl_vk::GetOffset(planeOffset, &copy.imageOffset);
            gl_vk::GetExtent(yuvInfo.planeExtent[plane], &copy.imageExtent);
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.aspectMask     = kPlaneAspectFlags[plane];
            appendSubresourceUpdate(
                gl::OwnerLevel(0),
                SubresourceUpdate(stagingBuffer.get(), currentBuffer, copy, storageFormatID));
        }

        stagingBuffer.release();
        return angle::Result::Continue;
    }

    VkBufferImageCopy copy         = {};
    VkImageAspectFlags aspectFlags = GetFormatAspectFlags(storageFormat);

    copy.bufferOffset      = stagingOffset;
    copy.bufferRowLength   = bufferRowLength;
    copy.bufferImageHeight = bufferImageHeight;

    gl::OwnerLevel updateLevelGL     = index.getLevelIndex();
    copy.imageSubresource.mipLevel   = updateLevelGL.get();
    copy.imageSubresource.baseArrayLayer =
        index.hasLayer() ? index.getLayerIndex().get() : offset.z;
    copy.imageSubresource.layerCount = index.getLayerCount();

    gl_vk::GetOffset(offset, &copy.imageOffset);
    gl_vk::GetExtent(glExtents, &copy.imageExtent);

    if (index.getType() == gl::TextureType::_3D)
    {
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount     = 1;
    }
    else
    {
        copy.imageOffset.z     = 0;
        copy.imageExtent.depth = 1;
    }

    if (stencilAllocationSize > 0)
    {
        // Note: Stencil is always one byte
        ASSERT((aspectFlags & VK_IMAGE_ASPECT_STENCIL_BIT) != 0);

        // Skip over depth data.
        ANGLE_UNSAFE_TODO(stagingPointer += outputDepthPitch * glExtents.depth);
        stagingOffset += outputDepthPitch * glExtents.depth;

        // recompute pitch for stencil data
        outputRowPitch   = glExtents.width;
        outputDepthPitch = outputRowPitch * glExtents.height;

        ASSERT(stencilLoadFunction != nullptr);
        stencilLoadFunction(contextVk->getImageLoadContext(), glExtents.width, glExtents.height,
                            glExtents.depth, source, inputRowPitch, inputDepthPitch, stagingPointer,
                            outputRowPitch, outputDepthPitch);

        VkBufferImageCopy stencilCopy = {};

        stencilCopy.bufferOffset                    = stagingOffset;
        stencilCopy.bufferRowLength                 = bufferRowLength;
        stencilCopy.bufferImageHeight               = bufferImageHeight;
        stencilCopy.imageSubresource.mipLevel       = copy.imageSubresource.mipLevel;
        stencilCopy.imageSubresource.baseArrayLayer = copy.imageSubresource.baseArrayLayer;
        stencilCopy.imageSubresource.layerCount     = copy.imageSubresource.layerCount;
        stencilCopy.imageOffset                     = copy.imageOffset;
        stencilCopy.imageExtent                     = copy.imageExtent;
        stencilCopy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_STENCIL_BIT;
        appendSubresourceUpdate(updateLevelGL, SubresourceUpdate(stagingBuffer.get(), currentBuffer,
                                                                 stencilCopy, storageFormatID));

        aspectFlags &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    if (HasBothDepthAndStencilAspects(aspectFlags))
    {
        // We still have both depth and stencil aspect bits set. That means we have a destination
        // buffer that is packed depth stencil and that the application is only loading one aspect.
        // Figure out which aspect the user is touching and remove the unused aspect bit.
        if (formatInfo.stencilBits > 0)
        {
            aspectFlags &= ~VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else
        {
            aspectFlags &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    if (aspectFlags)
    {
        copy.imageSubresource.aspectMask = aspectFlags;
        appendSubresourceUpdate(
            updateLevelGL, SubresourceUpdate(stagingBuffer.get(), currentBuffer, copy,
                                             useComputeTransCoding ? vkFormat.getIntendedFormatID()
                                                                   : storageFormatID));
        pruneSupersededUpdatesForLevel(contextVk, updateLevelGL, PruneReason::MemoryOptimization);
    }

    stagingBuffer.release();
    return angle::Result::Continue;
}

angle::Result ImageHelper::updateSubresourceOnHost(ContextVk *contextVk,
                                                   ApplyImageUpdate applyUpdate,
                                                   const gl::OwnerImageIndex &index,
                                                   const gl::Extents &glExtents,
                                                   const gl::Offset &offset,
                                                   const uint8_t *source,
                                                   const GLuint memoryRowLength,
                                                   const GLuint memoryImageHeight,
                                                   bool *copiedOut)
{
    // If the image is not set up for host copy, it can't be done.
    if (!valid() || (mVkImageCreateInfo.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) == 0)
    {
        return angle::Result::Continue;
    }

    Renderer *renderer = contextVk->getRenderer();
    const VkPhysicalDeviceHostImageCopyPropertiesEXT &hostImageCopyProperties =
        renderer->getPhysicalDeviceHostImageCopyProperties();

    // The image should be unused by the GPU.
    if (!renderer->hasResourceUseFinished(getResourceUse()))
    {
        ANGLE_TRY(renderer->checkCompletedCommandsAndCleanup(contextVk));
        if (!renderer->hasResourceUseFinished(getResourceUse()))
        {
            return angle::Result::Continue;
        }
    }

    // If there are staged updates that will be completely overwritten, try to drop them.  In
    // particular, this commonly happens where a clear is automatically staged for robustness or
    // other reasons, which would now be superseded by the data upload.
    const gl::OwnerLevel updateLevelGL = index.getLevelIndex();
    const gl::OwnerLayer layerIndex = index.hasLayer() ? index.getLayerIndex() : gl::OwnerLayer(0);
    const uint32_t layerCount = index.getLayerCount();
    const bool isArray                  = gl::IsArrayTextureType(index.getType());
    const bool is3D                     = mImageType == VK_IMAGE_TYPE_3D;
    const gl::OwnerLayer baseArrayLayer = isArray || is3D ? gl::OwnerLayer(offset.z) : layerIndex;
    const gl::Box updateBoundingBox =
        MakeUpdateBoundingBox(offset, glExtents, baseArrayLayer, layerCount);
    pruneSupersededUpdatesForLevelImpl(contextVk, updateLevelGL, updateBoundingBox,
                                       PruneReason::MinimizeWorkBeforeFlush);

    // If there are still pending updates to this subresource, cannot overwrite it.
    if (hasStagedUpdatesForSubresource(updateLevelGL, baseArrayLayer, layerCount))
    {
        return angle::Result::Continue;
    }

    // The image should be in a layout this is copiable.  If UNDEFINED, it can be transitioned to a
    // layout that is copyable.
    const VkImageAspectFlags aspectMask = getAspectFlags();
    if (mCurrentAccess == ImageAccess::Undefined)
    {
        VkHostImageLayoutTransitionInfoEXT transition = {};
        transition.sType     = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;
        transition.image     = mImage.getHandle();
        transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // The GENERAL layout is always guaranteed to be in
        // VkPhysicalDeviceHostImageCopyPropertiesEXT::pCopyDstLayouts
        transition.newLayout                   = renderer->getVkImageLayout(ImageAccess::HostCopy);
        transition.subresourceRange.aspectMask = aspectMask;
        transition.subresourceRange.baseMipLevel   = 0;
        transition.subresourceRange.levelCount     = mLevelCount;
        transition.subresourceRange.baseArrayLayer = 0;
        transition.subresourceRange.layerCount     = mLayerCount;

        ANGLE_VK_TRY(contextVk,
                     VK_CALL(vkTransitionImageLayoutEXT, renderer->getDevice(), 1, &transition));
        mCurrentAccess = ImageAccess::HostCopy;
    }
    else if (mCurrentAccess == ImageAccess::HostCopy)
    {
        // If last access to the image was a host-copy, continue to copy to it on the host; this
        // frequently happens because uploads to mips or layers can be done by separate calls.
    }
    else
    {
        // If host-copy is disallowed post-initialization, or if the layout is not supported for
        // host-copy, fall back to the buffer-copy path.
        const bool canHostCopyAfterGpuUse =
            renderer->getFeatures().allowHostImageCopyAfterInitialUpload.enabled &&
            IsAnyLayout(getCurrentLayout(renderer), hostImageCopyProperties.pCopyDstLayouts,
                        hostImageCopyProperties.copyDstLayoutCount);
        if (!canHostCopyAfterGpuUse)
        {
            return angle::Result::Continue;
        }
    }

    const LevelIndex updateLevelVk = toVkLevel(updateLevelGL);
    onWrite(updateLevelVk, 1, baseArrayLayer, layerCount, aspectMask);
    *copiedOut = true;

    // Perform the copy without holding the lock.  This is important for applications that perform
    // the copy on a separate thread, and doing all the work while holding the lock effectively
    // destroys all parallelism.  Note that the texture may not be used by the other thread without
    // appropriate synchronization (such as through glFenceSync), and because the copy is happening
    // in this call (just without holding the lock), the sync function won't be called until the
    // copy is done.
    auto doCopy = [contextVk, image = mImage.getHandle(), source, memoryRowLength,
                   memoryImageHeight, aspectMask, levelVk = updateLevelVk, is3D, baseArrayLayer,
                   layerCount, offset, glExtents,
                   layout = getCurrentLayout(renderer)](void *resultOut) {
        ANGLE_TRACE_EVENT0("gpu.angle", "Upload image data on host");
        ANGLE_UNUSED_VARIABLE(resultOut);

        VkMemoryToImageCopyEXT copyRegion          = {};
        copyRegion.sType                           = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;
        copyRegion.pHostPointer                    = source;
        copyRegion.memoryRowLength                 = memoryRowLength;
        copyRegion.memoryImageHeight               = memoryImageHeight;
        copyRegion.imageSubresource.aspectMask     = aspectMask;
        copyRegion.imageSubresource.mipLevel       = levelVk.get();
        copyRegion.imageSubresource.baseArrayLayer = baseArrayLayer.get();
        copyRegion.imageSubresource.layerCount     = layerCount;
        gl_vk::GetOffset(offset, &copyRegion.imageOffset);
        gl_vk::GetExtent(glExtents, &copyRegion.imageExtent);

        if (is3D)
        {
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount     = 1;
        }
        else
        {
            copyRegion.imageOffset.z     = 0;
            copyRegion.imageExtent.depth = 1;
        }

        VkCopyMemoryToImageInfoEXT copyInfo = {};
        copyInfo.sType                      = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;
        copyInfo.dstImage                   = image;
        copyInfo.dstImageLayout             = layout;
        copyInfo.regionCount                = 1;
        copyInfo.pRegions                   = &copyRegion;

        VkResult result = VK_CALL(vkCopyMemoryToImageEXT, contextVk->getDevice(), &copyInfo);
        if (result != VK_SUCCESS)
        {
            contextVk->handleError(result, __FILE__, ANGLE_FUNCTION, __LINE__);
        }
    };

    switch (applyUpdate)
    {
        // If possible, perform the copy in an unlocked tail call.  Then the other threads of the
        // application are free to draw.
        case ApplyImageUpdate::ImmediatelyInUnlockedTailCall:
            egl::Display::GetCurrentThreadUnlockedTailCall()->add(doCopy);
            break;

        // In some cases, the copy cannot be delayed.  For example because the contents are
        // immediately needed (such as when the generate mipmap hint is set), or because unlocked
        // tail calls are not allowed (this is the case with incomplete textures which are lazily
        // created at draw, but unlocked tail calls are avoided on draw calls due to overhead).
        case ApplyImageUpdate::Immediately:
            doCopy(nullptr);
            break;

        default:
            UNREACHABLE();
            doCopy(nullptr);
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::createReformattedStagedBufferUpdate(
    ContextVk *contextVk,
    const angle::Format &srcFormat,
    const angle::Format &dstFormat,
    gl::TextureType dstTextureType,
    const SubresourceUpdate &sourceUpdate,
    SubresourceUpdate *reformattedUpdateOut)
{
    ASSERT(sourceUpdate.updateSource == UpdateSource::Buffer);
    ASSERT(sourceUpdate.data.buffer.formatID == srcFormat.id);
    ASSERT(reformattedUpdateOut != nullptr);

    const gl::InternalFormat &dstFormatInfo =
        gl::GetSizedInternalFormatInfo(dstFormat.glInternalFormat);

    ASSERT(srcFormat.pixelReadFunction != nullptr);
    ASSERT(dstFormat.pixelWriteFunction != nullptr);

    const VkBufferImageCopy &copy    = sourceUpdate.data.buffer.copyRegion;
    const uint32_t depthOrLayerCount = dstTextureType == gl::TextureType::_3D
                                           ? copy.imageExtent.depth
                                           : copy.imageSubresource.layerCount;

    // Source and destination data are tightly packed.
    const size_t srcDataRowPitch =
        static_cast<size_t>(copy.imageExtent.width) * srcFormat.pixelBytes;
    const size_t dstDataRowPitch =
        static_cast<size_t>(copy.imageExtent.width) * dstFormat.pixelBytes;
    const size_t srcDataDepthPitch = srcDataRowPitch * copy.imageExtent.height;
    const size_t dstDataDepthPitch = dstDataRowPitch * copy.imageExtent.height;
    const size_t dstBufferSize     = dstDataDepthPitch * depthOrLayerCount;

    vk::BufferHelper *srcBuffer = sourceUpdate.data.buffer.bufferHelper;
    ASSERT(srcBuffer->isMapped());
    // bufferOffset is relative to the mapped buffer block.
    uint8_t *srcData = ANGLE_UNSAFE_TODO(srcBuffer->getBlockMemory() + copy.bufferOffset);

    std::unique_ptr<RefCounted<BufferHelper>> stagingBuffer =
        std::make_unique<RefCounted<BufferHelper>>();
    BufferHelper *dstBuffer = &stagingBuffer->get();

    uint8_t *dstData;
    VkDeviceSize dstBufferOffset;
    ANGLE_TRY(contextVk->initBufferForImageCopy(dstBuffer, dstBufferSize,
                                                MemoryCoherency::CachedNonCoherent, dstFormat.id,
                                                &dstBufferOffset, &dstData));

    rx::PixelReadFunction pixelReadFunction   = srcFormat.pixelReadFunction;
    rx::PixelWriteFunction pixelWriteFunction = dstFormat.pixelWriteFunction;

    CopyImageCHROMIUM(srcData, srcDataRowPitch, srcFormat.pixelBytes, srcDataDepthPitch,
                      pixelReadFunction, dstData, dstDataRowPitch, dstFormat.pixelBytes,
                      dstDataDepthPitch, pixelWriteFunction, dstFormatInfo.format,
                      dstFormatInfo.componentType, copy.imageExtent.width, copy.imageExtent.height,
                      depthOrLayerCount, false, false, false);

    VkBufferImageCopy reformattedCopy = copy;
    reformattedCopy.bufferOffset      = dstBufferOffset;
    SubresourceUpdate reformattedUpdate(stagingBuffer.release(), dstBuffer, reformattedCopy,
                                        dstFormat.id);
    *reformattedUpdateOut = std::move(reformattedUpdate);

    return angle::Result::Continue;
}

angle::Result ImageHelper::reformatStagedBufferUpdates(ContextVk *contextVk,
                                                       const angle::Format &srcFormat,
                                                       const angle::Format &dstFormat,
                                                       gl::TextureType dstTextureType)
{
    for (SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (SubresourceUpdate &update : levelUpdates)
        {
            if (update.updateSource != UpdateSource::Buffer ||
                update.data.buffer.formatID != srcFormat.id)
            {
                continue;
            }

            SubresourceUpdate reformattedUpdate;
            ANGLE_TRY(createReformattedStagedBufferUpdate(
                contextVk, srcFormat, dstFormat, dstTextureType, update, &reformattedUpdate));

            const VkDeviceSize sourceSize = update.data.buffer.bufferHelper->getSize();
            const VkDeviceSize reformattedSize =
                reformattedUpdate.data.buffer.bufferHelper->getSize();
            update.release(contextVk->getRenderer());
            update = std::move(reformattedUpdate);
            mTotalStagedBufferUpdateSize -= sourceSize;
            mTotalStagedBufferUpdateSize += reformattedSize;
        }
    }

    return angle::Result::Continue;
}

ImageHelper::ImageUpdateReadback::ImageUpdateReadback(Renderer *rendererIn,
                                                      SubresourceUpdate *updateIn)
    : renderer(rendererIn),
      buffer(std::make_unique<RefCounted<BufferHelper>>()),
      update(updateIn),
      srcData(nullptr)
{}

ImageHelper::ImageUpdateReadback::ImageUpdateReadback(ImageUpdateReadback &&other) noexcept
    : renderer(other.renderer),
      buffer(std::move(other.buffer)),
      update(other.update),
      srcData(other.srcData)
{}

ImageHelper::ImageUpdateReadback::~ImageUpdateReadback()
{
    if (buffer)
    {
        buffer->get().release(renderer);
    }
}

angle::Result ImageHelper::reformatStagedImageUpdateBatch(
    ContextVk *contextVk,
    const angle::Format &srcFormat,
    const angle::Format &dstFormat,
    gl::TextureType dstTextureType,
    std::vector<ImageUpdateReadback> *readbacks)
{
    if (readbacks->empty())
    {
        return angle::Result::Continue;
    }

    Renderer *renderer = contextVk->getRenderer();

    // Updates retain their source images until the batch completes.
    ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_HIGH,
                          "GPU stall due to staged texture format conversion");
    ANGLE_TRY(contextVk->finishImpl(QueueSubmitReason::TextureReformatToRenderable));

    for (ImageUpdateReadback &readback : *readbacks)
    {
        BufferHelper *buffer = &readback.buffer->get();
        ANGLE_TRY(buffer->invalidate(renderer));

        SubresourceUpdate &update    = *readback.update;
        const VkImageCopy copy       = update.data.image.copyRegion;
        VkBufferImageCopy bufferCopy = {};
        const uintptr_t blockAddress = reinterpret_cast<uintptr_t>(buffer->getBlockMemory());
        const uintptr_t dataAddress  = reinterpret_cast<uintptr_t>(readback.srcData);
        ASSERT(blockAddress != 0 && dataAddress >= blockAddress);
        const uintptr_t readbackOffset = dataAddress - blockAddress;
        ASSERT(readbackOffset <= buffer->getBlockMemorySize());
        bufferCopy.bufferOffset             = static_cast<VkDeviceSize>(readbackOffset);
        bufferCopy.imageSubresource         = copy.dstSubresource;
        bufferCopy.imageOffset              = copy.dstOffset;
        bufferCopy.imageExtent              = copy.extent;
        RefCounted<BufferHelper> *bufferRef = readback.buffer.release();
        SubresourceUpdate sourceUpdate(bufferRef, buffer, bufferCopy, srcFormat.id);
        SubresourceUpdate reformattedUpdate;
        angle::Result result = createReformattedStagedBufferUpdate(
            contextVk, srcFormat, dstFormat, dstTextureType, sourceUpdate, &reformattedUpdate);
        sourceUpdate.release(renderer);
        ANGLE_TRY(result);

        const VkDeviceSize reformattedSize = reformattedUpdate.data.buffer.bufferHelper->getSize();
        update.release(renderer);
        update = std::move(reformattedUpdate);
        mTotalStagedBufferUpdateSize += reformattedSize;
    }

    readbacks->clear();
    return angle::Result::Continue;
}

angle::Result ImageHelper::reformatStagedImageUpdates(ContextVk *contextVk,
                                                      const angle::Format &srcFormat,
                                                      const angle::Format &dstFormat,
                                                      gl::TextureType dstTextureType)
{
    std::vector<ImageUpdateReadback> readbacks;
    Renderer *renderer                 = contextVk->getRenderer();
    VkDeviceSize readbackBatchSize     = 0;
    VkDeviceSize readbackBatchCapacity = 0;

    // Bound readback batches by staging allocation capacity and mip level.
    for (SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (SubresourceUpdate &update : levelUpdates)
        {
            if (update.updateSource != UpdateSource::Image ||
                update.data.image.formatID != srcFormat.id)
            {
                continue;
            }

            const VkImageCopy &copy = update.data.image.copyRegion;
            ASSERT(copy.srcSubresource.layerCount == copy.dstSubresource.layerCount);
            const gl::Box sourceBox(copy.srcOffset.x, copy.srcOffset.y, copy.srcOffset.z,
                                    static_cast<int>(copy.extent.width),
                                    static_cast<int>(copy.extent.height),
                                    static_cast<int>(copy.extent.depth));
            readbacks.emplace_back(renderer, &update);
            ImageUpdateReadback &readback = readbacks.back();
            ASSERT(update.refCounted.image != nullptr);
            ImageHelper *srcImage = &update.refCounted.image->get();
            ASSERT(srcImage->valid());
            ASSERT(srcImage->getActualFormatID() == srcFormat.id);
            ANGLE_TRY(srcImage->copyImageDataToBuffer(
                contextVk, srcImage->toGLLevel(LevelIndex(copy.srcSubresource.mipLevel)),
                copy.srcSubresource.layerCount, gl::OwnerLayer(copy.srcSubresource.baseArrayLayer),
                sourceBox, &readback.buffer->get(), &readback.srcData));

            const BufferHelper &buffer = readback.buffer->get();
            readbackBatchCapacity = std::max(readbackBatchCapacity, buffer.getBlockMemorySize());
            if (readbackBatchCapacity == 0 || readbackBatchSize >= readbackBatchCapacity ||
                buffer.getSize() >= readbackBatchCapacity - readbackBatchSize)
            {
                ANGLE_TRY(reformatStagedImageUpdateBatch(contextVk, srcFormat, dstFormat,
                                                         dstTextureType, &readbacks));
                readbackBatchSize     = 0;
                readbackBatchCapacity = 0;
            }
            else
            {
                readbackBatchSize += buffer.getSize();
            }
        }

        ANGLE_TRY(reformatStagedImageUpdateBatch(contextVk, srcFormat, dstFormat, dstTextureType,
                                                 &readbacks));
        readbackBatchSize     = 0;
        readbackBatchCapacity = 0;
    }

    assertSubresourceUpdateRefCountsConsistent();
    return angle::Result::Continue;
}

angle::Result ImageHelper::reformatStagedUpdates(ContextVk *contextVk,
                                                 angle::FormatID srcFormatID,
                                                 angle::FormatID dstFormatID,
                                                 gl::TextureType dstTextureType)
{
    const angle::Format &srcFormat = angle::Format::Get(srcFormatID);
    const angle::Format &dstFormat = angle::Format::Get(dstFormatID);

    bool hasBufferUpdatesToReformat = false;
    bool hasImageUpdatesToReformat  = false;
    for (const SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (const SubresourceUpdate &update : levelUpdates)
        {
            hasBufferUpdatesToReformat |= update.updateSource == UpdateSource::Buffer &&
                                          update.data.buffer.formatID == srcFormatID;
            hasImageUpdatesToReformat |= update.updateSource == UpdateSource::Image &&
                                         update.data.image.formatID == srcFormatID;
        }
    }
    if (!hasBufferUpdatesToReformat && !hasImageUpdatesToReformat)
    {
        return angle::Result::Continue;
    }

    ASSERT(srcFormat.pixelReadFunction != nullptr);
    ASSERT(dstFormat.pixelWriteFunction != nullptr);

    if (hasBufferUpdatesToReformat)
    {
        ANGLE_TRY(reformatStagedBufferUpdates(contextVk, srcFormat, dstFormat, dstTextureType));
    }
    if (hasImageUpdatesToReformat)
    {
        ANGLE_TRY(reformatStagedImageUpdates(contextVk, srcFormat, dstFormat, dstTextureType));
    }
    return angle::Result::Continue;
}

angle::Result ImageHelper::calculateBufferInfo(ContextVk *contextVk,
                                               const gl::Extents &glExtents,
                                               const gl::InternalFormat &formatInfo,
                                               const gl::PixelUnpackState &unpack,
                                               GLenum type,
                                               bool is3D,
                                               GLuint *inputRowPitch,
                                               GLuint *inputDepthPitch,
                                               GLuint *inputSkipBytes)
{
    // YUV formats need special handling.
    if (gl::IsYuvFormat(formatInfo.internalFormat))
    {
        gl::YuvFormatInfo yuvInfo(formatInfo.internalFormat, glExtents);

        // row pitch = Y plane row pitch
        *inputRowPitch = yuvInfo.planePitch[0];
        // depth pitch = Y plane size + chroma plane size
        *inputDepthPitch = yuvInfo.planeSize[0] + yuvInfo.planeSize[1] + yuvInfo.planeSize[2];
        *inputSkipBytes  = 0;

        return angle::Result::Continue;
    }

    ANGLE_VK_CHECK_MATH(contextVk, formatInfo.computeRowDepthSkipBytes(
                                       type, glExtents.width, glExtents.height, unpack, is3D,
                                       inputRowPitch, inputDepthPitch, inputSkipBytes));

    return angle::Result::Continue;
}

void ImageHelper::onRenderPassAttach(const QueueSerial &queueSerial)
{
    setQueueSerial(queueSerial);
    // updatePipelineStageAccessHistory uses mCurrentAccess which we dont know yet (deferred until
    // endRenderPass time). So update it directly since we know attachment will be accessed by
    // fragment and attachment stages.
    mPipelineStageAccessHeuristic.onAccess(PipelineStageGroup::FragmentOnly);
}

void ImageHelper::onWrite(LevelIndex levelStart,
                          uint32_t levelCount,
                          gl::OwnerLayer layerStart,
                          uint32_t layerCount,
                          VkImageAspectFlags aspectFlags)
{
    ASSERT((levelStart + levelCount).get() <= mLevelCount);
    // As a special case, the caller might get kMaxContentDefinedLayerCount as layer and a layer
    // count of 0.
    const bool isInDepthRange =
        mImageType == VK_IMAGE_TYPE_3D &&
        (layerStart + layerCount).get() <= std::max(mExtents.depth >> levelStart.get(), 1u);
    const bool isInLayerRange =
        mImageType != VK_IMAGE_TYPE_3D && (layerStart + layerCount).get() <= mLayerCount;
    ASSERT((layerStart == gl::OwnerLayer(kMaxContentDefinedLayerCount) && layerCount == 0) ||
           isInDepthRange || isInLayerRange);

    mCurrentSingleClearValue.reset();

    // Mark contents of the given subresource as defined.
    setContentDefined(levelStart, levelCount, layerStart, layerCount, aspectFlags);

    setSubresourcesWrittenSinceBarrier(levelStart, levelCount, layerStart, layerCount);
}

bool ImageHelper::hasSubresourceDefinedContent(gl::OwnerLevel level,
                                               gl::OwnerLayer layerIndex,
                                               uint32_t layerCount) const
{
    if (layerIndex.get() >= kMaxContentDefinedLayerCount)
    {
        return true;
    }

    uint8_t layerRangeBits =
        GetContentDefinedLayerRangeBits(layerIndex, layerCount, kMaxContentDefinedLayerCount);
    return (getLevelContentDefined(toVkLevel(level)) & LevelContentDefinedMask(layerRangeBits))
        .any();
}

bool ImageHelper::hasSubresourceDefinedStencilContent(gl::OwnerLevel level,
                                                      gl::OwnerLayer layerIndex,
                                                      uint32_t layerCount) const
{
    if (layerIndex.get() >= kMaxContentDefinedLayerCount)
    {
        return true;
    }

    uint8_t layerRangeBits =
        GetContentDefinedLayerRangeBits(layerIndex, layerCount, kMaxContentDefinedLayerCount);
    return (getLevelStencilContentDefined(toVkLevel(level)) &
            LevelContentDefinedMask(layerRangeBits))
        .any();
}

void ImageHelper::invalidateEntireLevelContent(vk::ErrorContext *context, gl::OwnerLevel level)
{
    invalidateSubresourceContentImpl(
        context, level, gl::OwnerLayer(0),
        mImageType == VK_IMAGE_TYPE_3D ? mExtents.depth : mLayerCount,
        static_cast<VkImageAspectFlagBits>(getIntendedAspectFlags() & ~VK_IMAGE_ASPECT_STENCIL_BIT),
        nullptr, nullptr);
}

void ImageHelper::invalidateSubresourceContent(ContextVk *contextVk,
                                               gl::OwnerLevel level,
                                               gl::OwnerLayer layerIndex,
                                               uint32_t layerCount,
                                               bool *preferToKeepContentsDefinedOut)
{
    const VkImageAspectFlagBits aspect =
        static_cast<VkImageAspectFlagBits>(getAspectFlags() & ~VK_IMAGE_ASPECT_STENCIL_BIT);
    bool layerLimitReached = false;
    invalidateSubresourceContentImpl(contextVk, level, layerIndex, layerCount, aspect,
                                     preferToKeepContentsDefinedOut, &layerLimitReached);
    if (layerLimitReached)
    {
        const char *aspectName = (aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? "depth" : "color");
        ANGLE_UNSAFE_TODO(ANGLE_VK_PERF_WARNING(
            contextVk, GL_DEBUG_SEVERITY_LOW,
            "glInvalidateFramebuffer (%s) ineffective on attachments with layer >= 8", aspectName));
    }
}

void ImageHelper::invalidateEntireLevelStencilContent(vk::ErrorContext *context,
                                                      gl::OwnerLevel level)
{
    if (getIntendedFormat().stencilBits > 0)
    {
        invalidateSubresourceContentImpl(
            context, level, gl::OwnerLayer(0),
            mImageType == VK_IMAGE_TYPE_3D ? mExtents.depth : mLayerCount,
            VK_IMAGE_ASPECT_STENCIL_BIT, nullptr, nullptr);
    }
}

void ImageHelper::invalidateSubresourceStencilContent(ContextVk *contextVk,
                                                      gl::OwnerLevel level,
                                                      gl::OwnerLayer layerIndex,
                                                      uint32_t layerCount,
                                                      bool *preferToKeepContentsDefinedOut)
{
    bool layerLimitReached = false;
    invalidateSubresourceContentImpl(contextVk, level, layerIndex, layerCount,
                                     VK_IMAGE_ASPECT_STENCIL_BIT, preferToKeepContentsDefinedOut,
                                     &layerLimitReached);
    if (layerLimitReached)
    {
        ANGLE_VK_PERF_WARNING(
            contextVk, GL_DEBUG_SEVERITY_LOW,
            "glInvalidateFramebuffer (stencil) ineffective on attachments with layer >= 8");
    }
}

void ImageHelper::invalidateSubresourceContentImpl(vk::ErrorContext *context,
                                                   gl::OwnerLevel level,
                                                   gl::OwnerLayer layerIndex,
                                                   uint32_t layerCount,
                                                   VkImageAspectFlagBits aspect,
                                                   bool *preferToKeepContentsDefinedOut,
                                                   bool *layerLimitReachedOut)
{
    // If the aspect being invalidated doesn't exist, skip invalidation altogether.
    if ((getAspectFlags() & aspect) == 0)
    {
        if (preferToKeepContentsDefinedOut)
        {
            // Let the caller know that this invalidate request was ignored.
            *preferToKeepContentsDefinedOut = true;
        }
        return;
    }

    // If the color format is emulated and has extra channels, those channels need to stay cleared.
    // On some devices, it's cheaper to skip invalidating the framebuffer attachment, while on
    // others it's cheaper to invalidate but then re-clear the image.
    //
    // For depth/stencil formats, each channel is separately invalidated, so the invalidate is
    // simply skipped for the emulated channel on all devices.
    const bool hasEmulatedChannels = hasEmulatedImageChannels();
    bool skip                      = false;
    switch (aspect)
    {
        case VK_IMAGE_ASPECT_DEPTH_BIT:
            skip = hasEmulatedDepthChannel();
            break;
        case VK_IMAGE_ASPECT_STENCIL_BIT:
            skip = hasEmulatedStencilChannel();
            break;
        case VK_IMAGE_ASPECT_COLOR_BIT:
            skip = hasEmulatedChannels &&
                   context->getFeatures().preferSkippingInvalidateForEmulatedFormats.enabled;
            break;
        default:
            UNREACHABLE();
            skip = true;
    }

    if (preferToKeepContentsDefinedOut)
    {
        *preferToKeepContentsDefinedOut = skip;
    }
    if (skip)
    {
        return;
    }

    if (layerIndex.get() >= kMaxContentDefinedLayerCount)
    {
        ASSERT(layerLimitReachedOut != nullptr);
        *layerLimitReachedOut = true;
        return;
    }

    uint8_t layerRangeBits =
        GetContentDefinedLayerRangeBits(layerIndex, layerCount, kMaxContentDefinedLayerCount);
    LevelIndex levelVk = toVkLevel(level);
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        clearLevelStencilContentDefined(levelVk, layerRangeBits);
    }
    else
    {
        clearLevelContentDefined(levelVk, layerRangeBits);
    }

    // The VkImage data has been invalidated, the previous storeOp maybe turned into DontCare, which
    // means there is no grantee that it still contains mCurrentSingleClearValue data. THis is also
    // consistent with mVkImageContentDefined since we have just cleared the bits.
    mCurrentSingleClearValue.reset();

    // If there are emulated channels, stage a clear to make sure those channels continue to contain
    // valid values.
    if (hasEmulatedChannels && aspect == VK_IMAGE_ASPECT_COLOR_BIT)
    {
        VkClearValue clearValue;
        clearValue.color = kEmulatedInitColorValue;

        prependSubresourceUpdate(
            level, SubresourceUpdate(aspect, clearValue, level, layerIndex, layerCount));
        mSubresourceUpdates[level.get()].front().updateSource = UpdateSource::ClearAfterInvalidate;
    }
}

void ImageHelper::restoreSubresourceContent(gl::OwnerLevel level,
                                            gl::OwnerLayer layerIndex,
                                            uint32_t layerCount)
{
    restoreSubresourceContentImpl(
        level, layerIndex, layerCount,
        static_cast<VkImageAspectFlagBits>(getAspectFlags() & ~VK_IMAGE_ASPECT_STENCIL_BIT));
}

void ImageHelper::restoreSubresourceStencilContent(gl::OwnerLevel level,
                                                   gl::OwnerLayer layerIndex,
                                                   uint32_t layerCount)
{
    restoreSubresourceContentImpl(level, layerIndex, layerCount, VK_IMAGE_ASPECT_STENCIL_BIT);
}

void ImageHelper::restoreSubresourceContentImpl(gl::OwnerLevel level,
                                                gl::OwnerLayer layerIndex,
                                                uint32_t layerCount,
                                                VkImageAspectFlagBits aspect)
{
    if (layerIndex.get() >= kMaxContentDefinedLayerCount)
    {
        return;
    }

    // This function is called on attachments during a render pass when it's determined that
    // they should no longer be considered invalidated.  For an attachment with emulated
    // format that has extra channels, invalidateSubresourceContentImpl may have proactively
    // inserted a clear so that the extra channels continue to have defined values.
    // |FramebufferVk::invalidateImpl| closes the render pass right away however in that
    // case, so it should be impossible for the contents of such formats to need to be
    // restored.
    const bool hasClearAfterInvalidateUpdate =
        getLevelUpdates(level) != nullptr && getLevelUpdates(level)->size() != 0 &&
        getLevelUpdates(level)->at(0).updateSource == UpdateSource::ClearAfterInvalidate;
    ASSERT(!hasEmulatedImageChannels() || !hasClearAfterInvalidateUpdate);

    // Additionally, as the resource has been rewritten to in the render pass, its no longer cleared
    // to the cached value.
    mCurrentSingleClearValue.reset();

    uint8_t layerRangeBits =
        GetContentDefinedLayerRangeBits(layerIndex, layerCount, kMaxContentDefinedLayerCount);
    LevelIndex levelVk = toVkLevel(level);

    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        setLevelStencilContentDefined(levelVk, layerRangeBits);
    }
    else
    {
        setLevelContentDefined(levelVk, layerRangeBits);
    }
}

// Returns true if we have valid data in any non-emulated channel.
bool ImageHelper::isVkImageContentDefined() const
{
    if (isDepthOrStencil())
    {
        // The emulated channel will always have valid content since we stage clear for emulated
        // channel. When we query if VkImage has defined content or not, we only care about user
        // content. So ignore the emulated channel when checking content defined.
        return (getIntendedFormat().depthBits > 0 &&
                IsAnySubresourceContentDefined(mVkImageContentDefined)) ||
               (getIntendedFormat().stencilBits > 0 &&
                IsAnySubresourceContentDefined(mVkImageStencilContentDefined));
    }
    else
    {
        return IsAnySubresourceContentDefined(mVkImageContentDefined);
    }
}

angle::Result ImageHelper::stagePartialClear(ContextVk *contextVk,
                                             const gl::Box &clearArea,
                                             const ClearTextureMode clearMode,
                                             const gl::OwnerImageIndex &index,
                                             GLenum type,
                                             const gl::InternalFormat &formatInfo,
                                             const Format &vkFormat,
                                             ImageFormatSupport formatSupport,
                                             const uint8_t *data)
{
    // If the input data pointer is null, the texture is filled with zeros.
    const angle::Format &intendedFormat = vkFormat.getIntendedFormat();
    const angle::Format &actualFormat   = vkFormat.getActualImageFormat(formatSupport);
    auto intendedPixelSize              = static_cast<uint32_t>(intendedFormat.pixelBytes);
    auto actualPixelSize                = static_cast<uint32_t>(actualFormat.pixelBytes);

    uint8_t intendedData[16] = {0};
    if (data != nullptr)
    {
        ANGLE_UNSAFE_TODO(memcpy(intendedData, data, intendedPixelSize));
    }

    // The appropriate loading function is used to take the original value as a single pixel and
    // convert it into the format actually used for this image.
    std::vector<uint8_t> actualData(actualPixelSize, 0);
    LoadImageFunctionInfo loadFunctionInfo = vkFormat.getTextureLoadFunction(formatSupport, type);

    bool stencilOnly = formatInfo.sizedInternalFormat == GL_STENCIL_INDEX8;
    if (stencilOnly)
    {
        // Some Vulkan implementations do not support S8_UINT, so stencil-only data is
        // uploaded using one of combined depth-stencil formats there. Since the uploaded
        // stencil data must be tightly packed, the actual storage format should be ignored
        // with regards to its load function and output row pitch.
        loadFunctionInfo.loadFunction = angle::LoadToNative<GLubyte, 1>;
    }

    loadFunctionInfo.loadFunction(contextVk->getImageLoadContext(), 1, 1, 1, intendedData, 1, 1,
                                  actualData.data(), 1, 1);

    // VkClearValue is used for renderable images.
    VkClearValue clearValue = {};
    if (formatInfo.isDepthOrStencil())
    {
        GetVkClearDepthStencilValueFromBytes(intendedData, intendedFormat, &clearValue);
    }
    else
    {
        GetVkClearColorValueFromBytes(actualData.data(), actualFormat, &clearValue);
    }

    // Stage a ClearPartial update.
    VkImageAspectFlags aspectFlags = 0;
    if (!formatInfo.isDepthOrStencil())
    {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    else
    {
        aspectFlags |= formatInfo.depthBits > 0 ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
        aspectFlags |= formatInfo.stencilBits > 0 ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    }

    const gl::OwnerLevel levelIndexGL = index.getLevelIndex();
    const bool is3D                   = index.getType() == gl::TextureType::_3D;
    ASSERT(!is3D || index.getLayerIndex().get() == static_cast<uint32_t>(clearArea.z));
    ASSERT(!is3D || index.getLayerCount() == static_cast<uint32_t>(clearArea.depth));

    const gl::OwnerLayer layerIndex = index.getLayerIndex();
    const uint32_t layerCount       = index.getLayerCount();

    if (clearMode == ClearTextureMode::FullClear)
    {
        appendSubresourceUpdate(
            levelIndexGL,
            SubresourceUpdate(aspectFlags, clearValue, levelIndexGL, layerIndex, layerCount));
    }
    else
    {
        appendSubresourceUpdate(
            levelIndexGL,
            SubresourceUpdate(
                aspectFlags, clearValue, levelIndexGL, layerIndex, layerCount,
                gl::Rectangle(clearArea.x, clearArea.y, clearArea.width, clearArea.height)));
    }
    return angle::Result::Continue;
}

angle::Result ImageHelper::stageSubresourceUpdateAndGetData(ContextVk *contextVk,
                                                            size_t allocationSize,
                                                            const gl::OwnerImageIndex &imageIndex,
                                                            const gl::Extents &glExtents,
                                                            const gl::Offset &offset,
                                                            uint8_t **dstData,
                                                            angle::FormatID formatID)
{
    std::unique_ptr<RefCounted<BufferHelper>> stagingBuffer =
        std::make_unique<RefCounted<BufferHelper>>();
    BufferHelper *currentBuffer = &stagingBuffer->get();

    VkDeviceSize stagingOffset;
    ANGLE_TRY(contextVk->initBufferForImageCopy(currentBuffer, allocationSize,
                                                MemoryCoherency::CachedNonCoherent, formatID,
                                                &stagingOffset, dstData));

    const gl::OwnerLevel updateLevelGL = imageIndex.getLevelIndex();

    VkBufferImageCopy copy               = {};
    copy.bufferOffset                    = stagingOffset;
    copy.bufferRowLength                 = glExtents.width;
    copy.bufferImageHeight               = glExtents.height;
    copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel       = updateLevelGL.get();
    copy.imageSubresource.baseArrayLayer =
        imageIndex.hasLayer() ? imageIndex.getLayerIndex().get() : 0;
    copy.imageSubresource.layerCount     = imageIndex.getLayerCount();

    // Note: Only support color now
    ASSERT((mActualFormatID == angle::FormatID::NONE) ||
           (getAspectFlags() == VK_IMAGE_ASPECT_COLOR_BIT));

    gl_vk::GetOffset(offset, &copy.imageOffset);
    gl_vk::GetExtent(glExtents, &copy.imageExtent);

    appendSubresourceUpdate(
        updateLevelGL, SubresourceUpdate(stagingBuffer.release(), currentBuffer, copy, formatID));
    return angle::Result::Continue;
}

angle::Result ImageHelper::stageSubresourceUpdateFromFramebuffer(
    const gl::Context *context,
    const gl::OwnerImageIndex &index,
    const gl::Rectangle &sourceArea,
    const gl::Offset &dstOffset,
    const gl::Extents &dstExtent,
    const gl::InternalFormat &formatInfo,
    ImageFormatSupport formatSupport,
    FramebufferVk *framebufferVk)
{
    ContextVk *contextVk = GetImpl(context);

    // If the extents and offset is outside the source image, we need to clip.
    gl::Rectangle clippedRectangle;
    const gl::Extents readExtents = framebufferVk->getReadImageExtents();
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, readExtents.width, readExtents.height),
                       &clippedRectangle))
    {
        // Empty source area, nothing to do.
        return angle::Result::Continue;
    }

    bool isViewportFlipEnabled = contextVk->isViewportFlipEnabledForDrawFBO();
    if (isViewportFlipEnabled)
    {
        clippedRectangle.y = readExtents.height - clippedRectangle.y - clippedRectangle.height;
    }

    // 1- obtain a buffer handle to copy to
    Renderer *renderer = contextVk->getRenderer();

    const Format &vkFormat             = renderer->getFormat(formatInfo.sizedInternalFormat);
    const angle::Format &storageFormat = vkFormat.getActualImageFormat(formatSupport);
    LoadImageFunctionInfo loadFunction =
        vkFormat.getTextureLoadFunction(formatSupport, formatInfo.type);

    size_t outputRowPitch   = storageFormat.pixelBytes * clippedRectangle.width;
    size_t outputDepthPitch = outputRowPitch * clippedRectangle.height;

    std::unique_ptr<RefCounted<BufferHelper>> stagingBuffer =
        std::make_unique<RefCounted<BufferHelper>>();
    BufferHelper *currentBuffer = &stagingBuffer->get();

    uint8_t *stagingPointer;
    VkDeviceSize stagingOffset;

    // The destination is only one layer deep.
    size_t allocationSize = outputDepthPitch;
    ANGLE_TRY(contextVk->initBufferForImageCopy(currentBuffer, allocationSize,
                                                MemoryCoherency::CachedNonCoherent,
                                                storageFormat.id, &stagingOffset, &stagingPointer));

    const angle::Format &copyFormat =
        GetFormatFromFormatType(formatInfo.internalFormat, formatInfo.type);
    PackPixelsParams params(clippedRectangle, copyFormat, static_cast<GLuint>(outputRowPitch),
                            isViewportFlipEnabled, nullptr, 0);

    RenderTargetVk *readRenderTarget = framebufferVk->getColorReadRenderTarget();

    // 2- copy the source image region to the pixel buffer using a cpu readback
    if (loadFunction.requiresConversion)
    {
        // When a conversion is required, we need to use the loadFunction to read from a temporary
        // buffer instead so its an even slower path.
        const size_t bufferSize           = static_cast<size_t>(clippedRectangle.width) *
                                            static_cast<size_t>(clippedRectangle.height) *
                                            storageFormat.pixelBytes;
        angle::MemoryBuffer *memoryBuffer = nullptr;
        ANGLE_VK_CHECK_ALLOC(contextVk, context->getScratchBuffer(bufferSize, &memoryBuffer));

        // Read into the scratch buffer
        ANGLE_TRY(framebufferVk->readPixelsImpl(contextVk, clippedRectangle, params,
                                                VK_IMAGE_ASPECT_COLOR_BIT, readRenderTarget,
                                                memoryBuffer->data()));

        // Load from scratch buffer to our pixel buffer
        loadFunction.loadFunction(contextVk->getImageLoadContext(), clippedRectangle.width,
                                  clippedRectangle.height, 1, memoryBuffer->data(), outputRowPitch,
                                  0, stagingPointer, outputRowPitch, 0);
    }
    else
    {
        // We read directly from the framebuffer into our pixel buffer.
        ANGLE_TRY(framebufferVk->readPixelsImpl(contextVk, clippedRectangle, params,
                                                VK_IMAGE_ASPECT_COLOR_BIT, readRenderTarget,
                                                stagingPointer));
    }

    const gl::OwnerLevel updateLevelGL = index.getLevelIndex();

    // If the image is not an array type, the base layer index and layer count should be 0 and 1
    // respectively.
    uint32_t layerIndex = index.hasLayer() ? index.getLayerIndex().get() : 0;
    uint32_t layerCount = index.getLayerCount();
    if (index.getType() == gl::TextureType::_3D)
    {
        ASSERT(static_cast<uint32_t>(dstOffset.z) == layerIndex);
        layerIndex = 0;
        layerCount = 1;
    }

    // 3- enqueue the destination image subresource update
    VkBufferImageCopy copyToImage               = {};
    copyToImage.bufferOffset                    = static_cast<VkDeviceSize>(stagingOffset);
    copyToImage.bufferRowLength                 = 0;  // Tightly packed data can be specified as 0.
    copyToImage.bufferImageHeight               = clippedRectangle.height;
    copyToImage.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copyToImage.imageSubresource.mipLevel       = updateLevelGL.get();
    copyToImage.imageSubresource.baseArrayLayer = layerIndex;
    copyToImage.imageSubresource.layerCount     = layerCount;
    gl_vk::GetOffset(dstOffset, &copyToImage.imageOffset);
    gl_vk::GetExtent(dstExtent, &copyToImage.imageExtent);

    // 3- enqueue the destination image subresource update
    appendSubresourceUpdate(updateLevelGL, SubresourceUpdate(stagingBuffer.release(), currentBuffer,
                                                             copyToImage, storageFormat.id));

    return angle::Result::Continue;
}

void ImageHelper::stageSubresourceUpdateFromImage(RefCounted<ImageHelper> *image,
                                                  const gl::OwnerImageIndex &index,
                                                  LevelIndex srcMipLevel,
                                                  LayerIndex srcLayerIndex,
                                                  const gl::Offset &destOffset,
                                                  const gl::Extents &glExtents,
                                                  const VkImageType srcImageType,
                                                  const VkImageType dstImageType)
{
    const gl::OwnerLevel updateLevelGL  = index.getLevelIndex();
    VkImageAspectFlags imageAspectFlags = vk::GetFormatAspectFlags(image->get().getActualFormat());

    VkImageCopy copyToImage               = {};
    copyToImage.srcSubresource.aspectMask = imageAspectFlags;
    copyToImage.srcSubresource.mipLevel   = srcMipLevel.get();
    copyToImage.srcSubresource.baseArrayLayer = srcLayerIndex.get();
    copyToImage.srcSubresource.layerCount = index.getLayerCount();
    copyToImage.dstSubresource.aspectMask = imageAspectFlags;
    copyToImage.dstSubresource.mipLevel   = updateLevelGL.get();
    copyToImage.dstSubresource.layerCount     = index.getLayerCount();

    // These values must be set explicitly to follow the Vulkan spec:
    // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkImageCopy.html
    // If either of the calling command's srcImage or dstImage parameters are of VkImageType
    // VK_IMAGE_TYPE_3D, the baseArrayLayer and layerCount members of the corresponding
    // subresource must be 0 and 1, respectively.
    if (srcImageType == VK_IMAGE_TYPE_3D)
    {
        ASSERT(srcLayerIndex == LayerIndex(0));
        copyToImage.srcSubresource.layerCount = 1;
    }
    if (dstImageType == VK_IMAGE_TYPE_3D)
    {
        copyToImage.dstSubresource.layerCount = 1;
        // Preserve the assumption that destOffset.z == "dstSubresource.baseArrayLayer"
        ASSERT(static_cast<uint32_t>(destOffset.z) ==
               (index.hasLayer() ? index.getLayerIndex().get() : 0));
    }
    else
    {
        copyToImage.dstSubresource.baseArrayLayer =
            index.hasLayer() ? index.getLayerIndex().get() : 0;
    }

    gl_vk::GetOffset(destOffset, &copyToImage.dstOffset);
    gl_vk::GetExtent(glExtents, &copyToImage.extent);

    appendSubresourceUpdate(
        updateLevelGL, SubresourceUpdate(image, copyToImage, image->get().getActualFormatID()));
}

gl::OwnerImageIndex ImageHelper::getImageIndexForLevel(gl::OwnerLevel level)
{
    if (mLayerCount > 1)
    {
        // We don't need to distinguish 2D array and cube.
        return gl::OwnerImageIndex::Make2DArrayRange(level, gl::OwnerLayer(0), mLayerCount);
    }
    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        // The depth slices of the 3D image are treated as layers for this purpose.
        return gl::OwnerImageIndex::MakeFromType(gl::TextureType::_3D, level, gl::OwnerLayer(0),
                                                 getLevelExtents(toVkLevel(level)).depth);
    }

    ASSERT(mExtents.depth == 1);
    ASSERT(mLayerCount == 1);
    return gl::OwnerImageIndex::Make2D(level);
}

void ImageHelper::stageSubresourceUpdatesFromAllImageLevels(RefCounted<ImageHelper> *image,
                                                            gl::OwnerLevel baseLevel)
{
    // The given image must match this image in dimensions, but not necessarily levels
    ASSERT(image->get().getExtents().width == getExtents().width);
    ASSERT(image->get().getExtents().height == getExtents().height);
    ASSERT(image->get().getExtents().depth == getExtents().depth);
    ASSERT(image->get().getLayerCount() == getLayerCount());

    for (LevelIndex levelVk(0); levelVk < LevelIndex(image->get().getLevelCount()); ++levelVk)
    {
        const gl::OwnerLevel levelGL    = vk_gl::GetLevelIndex(levelVk, baseLevel);
        const gl::OwnerImageIndex index = getImageIndexForLevel(levelGL);

        stageSubresourceUpdateFromImage(image, index, levelVk, LayerIndex(0), gl::kOffsetZero,
                                        image->get().getLevelExtents(levelVk),
                                        image->get().getType(), image->get().getType());
    }
}

void ImageHelper::stageClear(const gl::OwnerImageIndex &index,
                             VkImageAspectFlags aspectFlags,
                             const VkClearValue &clearValue)
{
    const gl::OwnerLevel updateLevelGL = index.getLevelIndex();
    appendSubresourceUpdate(updateLevelGL, SubresourceUpdate(aspectFlags, clearValue, index));
}

void ImageHelper::stageRobustResourceClear(const gl::OwnerImageIndex &index,
                                           const VkImageAspectFlags aspectFlags)
{
    ASSERT(mActualFormatID != angle::FormatID::NONE);
    VkClearValue clearValue = GetRobustResourceClearValue(getIntendedFormat(), getActualFormat());

    const gl::OwnerLevel updateLevelGL = index.getLevelIndex();
    appendSubresourceUpdate(updateLevelGL, SubresourceUpdate(aspectFlags, clearValue, index));
}

angle::Result ImageHelper::stageResourceClearWithFormat(ContextVk *contextVk,
                                                        const gl::OwnerImageIndex &index,
                                                        const gl::Extents &glExtents,
                                                        const angle::Format &intendedFormat,
                                                        const angle::Format &imageFormat,
                                                        const VkClearValue &clearValue)
{
    // A prior clear or update (such as a previous robust clear or a ClearAfterInvalidate on an
    // emulated format) may already be staged on this subresource if the texture level was
    // redefined or re-initialized across operations like mipmap generation. Drop any prior
    // staged updates so that this newly staged clear becomes the sole pending update.
    removeSingleSubresourceStagedUpdates(contextVk, index.getLevelIndex(), index.getLayerIndex(),
                                         index.getLayerCount());

    // Otherwise robust clears must only be staged if we do not have any prior data for this
    // subresource.
    ASSERT(!hasStagedUpdatesForSubresource(index.getLevelIndex(), index.getLayerIndex(),
                                           index.getLayerCount()));

    const VkImageAspectFlags aspectFlags = GetFormatAspectFlags(imageFormat);

    const gl::OwnerLevel updateLevelGL = index.getLevelIndex();

    if (imageFormat.isBlock)
    {
        // This only supports doing an initial clear to 0, not clearing to a specific encoded RGBA
        // value
        ASSERT((clearValue.color.int32[0] == 0) && (clearValue.color.int32[1] == 0) &&
               (clearValue.color.int32[2] == 0) && (clearValue.color.int32[3] == 0));

        const gl::InternalFormat &formatInfo =
            gl::GetSizedInternalFormatInfo(imageFormat.glInternalFormat);

        // For the array compressed textures (e.g., 2D array), the depth is set to 1. This should be
        // taken into account when calculating the required buffer size for the copy.
        gl::Extents glExtentForSizeComputation = glExtents;
        if (gl::IsArrayTextureType(index.getType()))
        {
            ASSERT(glExtentForSizeComputation.depth == 1);
            glExtentForSizeComputation.depth = index.getLayerCount();
        }

        GLuint totalSize;
        ANGLE_VK_CHECK_MATH(contextVk, formatInfo.computeCompressedImageSize(
                                           glExtentForSizeComputation, &totalSize));

        std::unique_ptr<RefCounted<BufferHelper>> stagingBuffer =
            std::make_unique<RefCounted<BufferHelper>>();
        BufferHelper *currentBuffer = &stagingBuffer->get();

        uint8_t *stagingPointer;
        VkDeviceSize stagingOffset;
        ANGLE_TRY(contextVk->initBufferForImageCopy(
            currentBuffer, totalSize, MemoryCoherency::CachedNonCoherent, imageFormat.id,
            &stagingOffset, &stagingPointer));
        ANGLE_UNSAFE_TODO(memset(stagingPointer, 0, totalSize));

        VkBufferImageCopy copyRegion               = {};
        copyRegion.bufferOffset                    = stagingOffset;
        copyRegion.imageExtent.width               = glExtents.width;
        copyRegion.imageExtent.height              = glExtents.height;
        copyRegion.imageExtent.depth               = glExtents.depth;
        copyRegion.imageSubresource.mipLevel       = updateLevelGL.get();
        copyRegion.imageSubresource.aspectMask     = aspectFlags;
        copyRegion.imageSubresource.baseArrayLayer =
            index.hasLayer() ? index.getLayerIndex().get() : 0;
        copyRegion.imageSubresource.layerCount     = index.getLayerCount();

        // The update structure owns the staging buffer.
        appendSubresourceUpdate(
            updateLevelGL,
            SubresourceUpdate(stagingBuffer.release(), currentBuffer, copyRegion, imageFormat.id));
    }
    else
    {
        appendSubresourceUpdate(updateLevelGL, SubresourceUpdate(aspectFlags, clearValue, index));
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::stageRobustResourceClearWithFormat(ContextVk *contextVk,
                                                              const gl::OwnerImageIndex &index,
                                                              const gl::Extents &glExtents,
                                                              const angle::Format &intendedFormat,
                                                              const angle::Format &imageFormat)
{
    VkClearValue clearValue          = GetRobustResourceClearValue(intendedFormat, imageFormat);
    gl::OwnerImageIndex fullResourceIndex = index;
    gl::Extents fullResourceExtents  = glExtents;

    if (gl::IsArrayTextureType(index.getType()))
    {
        // For 2Darray textures gl::Extents::depth is the layer count.
        fullResourceIndex =
            gl::OwnerImageIndex::MakeFromType(index.getType(), index.getLevelIndex(),
                                              gl::OwnerImageIndex::kEntireLayer, glExtents.depth);
        // Vulkan requires depth of 1 for 2Darray textures.
        fullResourceExtents.depth = 1;
    }

    return stageResourceClearWithFormat(contextVk, fullResourceIndex, fullResourceExtents,
                                        intendedFormat, imageFormat, clearValue);
}

void ImageHelper::stageClearIfEmulatedFormat(bool isRobustResourceInitEnabled, bool isExternalImage)
{
    // Skip staging extra clears if robust resource init is enabled.
    if (!hasEmulatedImageChannels() || isRobustResourceInitEnabled)
    {
        return;
    }

    VkClearValue clearValue = {};
    if (getIntendedFormat().hasDepthOrStencilBits())
    {
        clearValue.depthStencil = kRobustInitDepthStencilValue;
    }
    else
    {
        clearValue.color = kEmulatedInitColorValue;
    }

    const VkImageAspectFlags aspectFlags = getAspectFlags();

    // If the image has an emulated channel and robust resource init is not enabled, always clear
    // it. These channels will be masked out in future writes, and shouldn't contain uninitialized
    // values.
    //
    // For external images, we cannot clear the image entirely, as it may contain data in the
    // non-emulated channels.  For depth/stencil images, clear is already per aspect, but for color
    // images we would need to take a special path where we only clear the emulated channels.

    // Block images are not cleared, since no emulated channels are present if decoded.
    if (isExternalImage && getIntendedFormat().isBlock)
    {
        return;
    }

    const bool clearOnlyEmulatedChannels =
        isExternalImage && !getIntendedFormat().hasDepthOrStencilBits();
    const VkColorComponentFlags colorMaskFlags =
        clearOnlyEmulatedChannels ? getEmulatedChannelsMask() : 0;

    for (LevelIndex level(0); level < LevelIndex(mLevelCount); ++level)
    {
        const gl::OwnerLevel updateLevelGL = toGLLevel(level);
        const gl::OwnerImageIndex index    = getImageIndexForLevel(updateLevelGL);

        if (clearOnlyEmulatedChannels)
        {
            prependSubresourceUpdate(updateLevelGL,
                                     SubresourceUpdate(colorMaskFlags, clearValue.color, index));
        }
        else
        {
            prependSubresourceUpdate(updateLevelGL,
                                     SubresourceUpdate(aspectFlags, clearValue, index));
        }
    }
}

bool ImageHelper::verifyNoStagedUpdates() const
{
    for (const SubresourceUpdates &updates : mSubresourceUpdates)
    {
        if (!updates.empty())
        {
            return false;
        }
    }
    return true;
}

bool ImageHelper::verifyEmulatedClearsAreBeforeOtherUpdates(const SubresourceUpdates &updates)
{
    bool isIteratingEmulatedClears = true;

    for (const SubresourceUpdate &update : updates)
    {
        // If anything other than ClearEmulatedChannelsOnly is visited, there cannot be any
        // ClearEmulatedChannelsOnly updates after that.
        if (update.updateSource != UpdateSource::ClearEmulatedChannelsOnly)
        {
            isIteratingEmulatedClears = false;
        }
        else if (!isIteratingEmulatedClears)
        {
            // If ClearEmulatedChannelsOnly is visited after another update, that's an error.
            return false;
        }
    }

    // Additionally, verify that emulated clear is not applied multiple times.
    if (updates.size() >= 2 && updates[1].updateSource == UpdateSource::ClearEmulatedChannelsOnly)
    {
        return false;
    }

    return true;
}

void ImageHelper::copyStateAndMoveStorageFrom(ImageHelper *other)
{
    // move these vulkan objects
    mImage         = std::move(other->mImage);
    mDeviceMemory  = std::move(other->mDeviceMemory);
    mVmaAllocation = std::move(other->mVmaAllocation);
    if (other->mCurrentEvent.valid())
    {
        mCurrentEvent = std::move(other->mCurrentEvent);
    }
    if (other->mLastNonShaderReadOnlyEvent.valid())
    {
        mLastNonShaderReadOnlyEvent = std::move(other->mLastNonShaderReadOnlyEvent);
    }

    // Copy over state
    mUse = other->mUse;

    mVkImageCreateInfo  = other->mVkImageCreateInfo;
    mImageType          = other->mImageType;
    mTilingMode         = other->mTilingMode;
    mCreateFlags        = other->mCreateFlags;
    mRequestedUsage     = other->mRequestedUsage;
    mExtents            = other->mExtents;
    mRotatedAspectRatio = other->mRotatedAspectRatio;
    mIntendedFormatID   = other->mIntendedFormatID;
    mActualFormatID     = other->mActualFormatID;
    mSamples            = other->mSamples;
    mImageSerial        = other->mImageSerial;

    mCurrentAccess                = other->mCurrentAccess;
    mCurrentDeviceQueueIndex      = other->mCurrentDeviceQueueIndex;
    mLastNonShaderReadOnlyAccess  = other->mLastNonShaderReadOnlyAccess;
    mCurrentShaderReadStageMask   = other->mCurrentShaderReadStageMask;
    mRenderPassUsageFlags         = other->mRenderPassUsageFlags;
    mBarrierQueueSerial           = other->mBarrierQueueSerial;
    mPipelineStageAccessHeuristic = other->mPipelineStageAccessHeuristic;

    mIsReleasedToExternal = other->mIsReleasedToExternal;
    mIsForeignImage       = other->mIsForeignImage;
    mYcbcrConversionDesc  = other->mYcbcrConversionDesc;

    mFirstAllocatedLevel          = other->mFirstAllocatedLevel;
    mLayerCount                   = other->mLayerCount;
    mLevelCount                   = other->mLevelCount;
    mVkImageContentDefined        = other->mVkImageContentDefined;
    mVkImageStencilContentDefined = other->mVkImageStencilContentDefined;

    mAllocationSize                = other->mAllocationSize;
    mMemoryAllocationType          = other->mMemoryAllocationType;
    mMemoryTypeIndex               = other->mMemoryTypeIndex;
    mTileMemoryCompatible          = other->mTileMemoryCompatible;
    mUseTileMemory                 = other->mUseTileMemory;
    mTileMemorySupportsTransferSrc = other->mTileMemorySupportsTransferSrc;

    mSubresourcesWrittenSinceBarrier = other->mSubresourcesWrittenSinceBarrier;

    // Reset information for other (invalid) image.
    other->mCurrentAccess               = ImageAccess::Undefined;
    other->mCurrentDeviceQueueIndex     = kInvalidDeviceQueueIndex;
    other->mIsReleasedToExternal        = false;
    other->mIsForeignImage              = false;
    other->mLastNonShaderReadOnlyAccess = ImageAccess::Undefined;
    other->mCurrentShaderReadStageMask  = 0;
    other->mImageSerial                 = kInvalidImageSerial;
    other->mMemoryAllocationType        = MemoryAllocationType::InvalidEnum;
    other->setEntireContentUndefined();
    other->mTileMemoryCompatible          = false;
    other->mUseTileMemory                 = false;
    other->mTileMemorySupportsTransferSrc = false;
}

void ImageHelper::stageSelfAsSubresourceUpdates(
    ContextVk *contextVk,
    uint32_t levelCount,
    gl::TextureType textureType,
    const gl::CubeFaceArray<gl::TexLevelMask> &skipLevels)
{
    // Nothing to do if every level must be skipped
    const gl::TexLevelMask levelsMask(angle::BitMask<uint32_t>(levelCount)
                                      << mFirstAllocatedLevel.get());
    const gl::TexLevelMask skipLevelsAllFaces =
        AggregateSkipLevelsAllFacesSkipped(skipLevels, textureType);

    if ((~skipLevelsAllFaces & levelsMask).none())
    {
        return;
    }

    // Because we are cloning this object to another object, we must finalize the layout if it is
    // being used by current renderpass as attachment. Otherwise we are copying the incorrect layout
    // since it is determined at endRenderPass time.
    contextVk->getShareGroup()->finalizeImageLayoutInAllSharedContexts(this);

    std::unique_ptr<RefCounted<ImageHelper>> prevImage =
        std::make_unique<RefCounted<ImageHelper>>();

    // Move storage from this object to prevImage
    prevImage->get().copyStateAndMoveStorageFrom(this);

    // Stage updates from the previous image.
    for (LevelIndex levelVk(0); levelVk < LevelIndex(levelCount); ++levelVk)
    {
        gl::OwnerLevel levelGL = toGLLevel(levelVk);
        if (textureType == gl::TextureType::CubeMap)
        {
            for (uint32_t faceIndex = 0; faceIndex < gl::kCubeFaceCount; ++faceIndex)
            {
                const gl::OwnerLayer face(faceIndex);
                if (!skipLevels[face.get()][levelGL.get()])
                {
                    const gl::OwnerImageIndex index =
                        gl::OwnerImageIndex::Make2DArrayRange(levelGL, face, 1);

                    stageSubresourceUpdateFromImage(prevImage.get(), index, levelVk, face,
                                                    gl::kOffsetZero, getLevelExtents(levelVk),
                                                    mImageType, mImageType);
                }
            }
        }
        else if (!skipLevelsAllFaces.test(levelGL.get()))
        {
            const gl::OwnerImageIndex index = getImageIndexForLevel(levelGL);

            stageSubresourceUpdateFromImage(prevImage.get(), index, levelVk, gl::OwnerLayer(0),
                                            gl::kOffsetZero, getLevelExtents(levelVk), mImageType,
                                            mImageType);
        }
    }

    ASSERT(levelCount > 0);
    prevImage.release();
}

angle::Result ImageHelper::flushSingleSubresourceStagedUpdates(ContextVk *contextVk,
                                                               gl::OwnerLevel levelGL,
                                                               gl::OwnerLayer layer,
                                                               uint32_t layerCount,
                                                               ClearValuesArray *deferredClears,
                                                               uint32_t deferredClearIndex)
{
    SubresourceUpdates *levelUpdates = getLevelUpdates(levelGL);
    if (levelUpdates == nullptr || levelUpdates->empty())
    {
        return angle::Result::Continue;
    }

    // Handle deferred clears. Search the updates list for a matching clear index.
    if (deferredClears)
    {
        Optional<size_t> foundClear;

        for (size_t updateIndex = 0; updateIndex < levelUpdates->size(); ++updateIndex)
        {
            SubresourceUpdate &update = (*levelUpdates)[updateIndex];

            if (intersectsLayerRange(update, levelGL, layer, layerCount))
            {
                // On any data update or the clear does not match exact layer range, we'll need to
                // do a full upload.
                const bool isClear = IsClearOfAllChannels(update.updateSource);
                if (isClear && matchesLayerRange(update, levelGL, layer, layerCount))
                {
                    foundClear = updateIndex;
                }
                else
                {
                    foundClear.reset();
                    break;
                }
            }
        }

        // If we have a valid index we defer the clear using the clear reference.
        if (foundClear.valid())
        {
            size_t foundIndex         = foundClear.value();
            const ClearUpdate &update = (*levelUpdates)[foundIndex].data.clear;

            // Note that this set command handles combined or separate depth/stencil clears.
            deferredClears->store(deferredClearIndex, update.aspectFlags, update.value);

            // We process the updates again to erase any clears for this level.
            removeSingleSubresourceStagedUpdates(contextVk, levelGL, layer, layerCount);
            return angle::Result::Continue;
        }

        // Otherwise we proceed with a normal update.
    }

    return flushStagedUpdates(contextVk, levelGL, levelGL + 1, layer, layer + layerCount, {});
}

angle::Result ImageHelper::flushStagedClearEmulatedChannelsUpdates(ContextVk *contextVk,
                                                                   gl::OwnerLevel levelGLStart,
                                                                   gl::OwnerLevel levelGLLimit,
                                                                   bool *otherUpdatesToFlushOut)
{
    *otherUpdatesToFlushOut = false;
    for (gl::OwnerLevel updateMipLevelGL = levelGLStart; updateMipLevelGL < levelGLLimit;
         ++updateMipLevelGL)
    {
        // It is expected that the checked mip levels in this loop do not surpass the size of
        // mSubresourceUpdates.
        SubresourceUpdates *levelUpdates = getLevelUpdates(updateMipLevelGL);
        ASSERT(levelUpdates != nullptr);

        // The levels with no updates should be skipped.
        if (levelUpdates->empty())
        {
            continue;
        }

        // Since ClearEmulatedChannelsOnly is expected in the beginning and there cannot be more
        // than one such update type, we can process the first update and move on if there is
        // another update type in the list.
        ASSERT(verifyEmulatedClearsAreBeforeOtherUpdates(*levelUpdates));
        SubresourceUpdate *update = &(*levelUpdates).front();

        if (update->updateSource != UpdateSource::ClearEmulatedChannelsOnly)
        {
            *otherUpdatesToFlushOut = true;
            continue;
        }

        // If found, ClearEmulatedChannelsOnly should be flushed before the others and removed from
        // the update list.
        ASSERT(update->updateSource == UpdateSource::ClearEmulatedChannelsOnly);
        gl::OwnerLayer updateBaseLayer;
        uint32_t updateLayerCount;
        getDestSubresource(*update, updateMipLevelGL, &updateBaseLayer, &updateLayerCount);

        const LevelIndex updateMipLevelVk = toVkLevel(updateMipLevelGL);
        update->data.clear.levelIndex     = updateMipLevelGL.get();
        ANGLE_TRY(clearEmulatedChannels(contextVk, update->data.clear.colorMaskFlags,
                                        update->data.clear.value, updateMipLevelVk, updateBaseLayer,
                                        updateLayerCount));
        // Do not call onWrite. Even though some channels of the image are cleared, don't consider
        // the contents defined. Also, since clearing emulated channels is a one-time thing that's
        // superseded by Clears, |mCurrentSingleClearValue| is irrelevant and can't have a value.
        ASSERT(!mCurrentSingleClearValue.valid());

        levelUpdates->pop_front();
        if (!levelUpdates->empty())
        {
            ASSERT(levelUpdates->begin()->updateSource != UpdateSource::ClearEmulatedChannelsOnly);
            *otherUpdatesToFlushOut = true;
        }
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::flushStagedUpdatesImpl(ContextVk *contextVk,
                                                  gl::OwnerLevel levelGLStart,
                                                  gl::OwnerLevel levelGLEnd,
                                                  gl::OwnerLayer layerStart,
                                                  gl::OwnerLayer layerEnd,
                                                  const gl::TexLevelMask &skipLevels)
{
    Renderer *renderer = contextVk->getRenderer();

    const angle::FormatID &actualformat   = getActualFormatID();
    const angle::FormatID &intendedFormat = getIntendedFormatID();

    const VkImageAspectFlags aspectFlags = GetFormatAspectFlags(getActualFormat());

    // Start in TransferDst.  Don't yet mark any subresource as having defined contents; that is
    // done with fine granularity as updates are applied.  This is achieved by specifying a layer
    // that is outside the tracking range. Under some circumstances, ComputeWrite is also required.
    // This need not be applied if the only updates are ClearEmulatedChannels.
    CommandResources transferAccess;
    OutsideRenderPassCommandBufferHelper *commandBuffer = nullptr;
    bool transCoding = renderer->getFeatures().supportsComputeTranscodeEtcToBc.enabled &&
                       IsETCFormat(intendedFormat) && IsBCFormat(actualformat);

    if (transCoding)
    {
        transferAccess.onImageTransferDstAndComputeWrite(
            levelGLStart, 1, gl::OwnerLayer(kMaxContentDefinedLayerCount), 0, aspectFlags, this);
    }
    else if (mUseTileMemory)
    {
        ASSERT(areStagedUpdatesClearOnly());
    }
    else
    {
        transferAccess.onImageTransferWrite(
            levelGLStart, 1, gl::OwnerLayer(kMaxContentDefinedLayerCount), 0, aspectFlags, this);
    }
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBufferHelper(transferAccess, &commandBuffer));

    // Flush the staged updates in each mip level.
    for (gl::OwnerLevel updateMipLevelGL = levelGLStart; updateMipLevelGL < levelGLEnd;
         ++updateMipLevelGL)
    {
        // If updates to this level are specifically asked to be skipped, skip
        // them. This can happen when recreating an image that has been partially incompatibly
        // redefined, in which case only updates to the levels that haven't been redefined
        // should be flushed.
        if (skipLevels.test(updateMipLevelGL.get()))
        {
            continue;
        }

        // It is expected that the checked mip levels in this loop do not surpass the size of
        // mSubresourceUpdates.
        SubresourceUpdates *levelUpdates = getLevelUpdates(updateMipLevelGL);
        SubresourceUpdates updatesToKeep;
        ASSERT(levelUpdates != nullptr);

        // Because updates may have overlapping layer ranges, we must first figure out the actual
        // layer ranges that will be flushed. The updatesToKeep list must compare against this
        // adjusted layer range. Otherwise you may end up keeping the update even though it is
        // overlapped with the update that gets flushed, and then content gets overwritten when
        // updatesToKeep gets flushed out.
        gl::OwnerLayer adjustedLayerStart = layerStart, adjustedLayerEnd = layerEnd;
        if (levelUpdates->size() > 1)
        {
            adjustLayerRange(*levelUpdates, updateMipLevelGL, &adjustedLayerStart,
                             &adjustedLayerEnd);
        }

        for (SubresourceUpdate &update : *levelUpdates)
        {
            ASSERT(IsClearOfAllChannels(update.updateSource) ||
                   (update.updateSource == UpdateSource::ClearPartial) ||
                   (update.updateSource == UpdateSource::Buffer &&
                    update.data.buffer.bufferHelper != nullptr) ||
                   (update.updateSource == UpdateSource::Image &&
                    update.refCounted.image != nullptr && update.refCounted.image->get().valid()));
            if (update.updateSource == UpdateSource::Image)
            {
                update.refCounted.image->assertIsReferenced();
            }

            gl::OwnerLayer updateBaseLayer;
            uint32_t updateLayerCount;
            getDestSubresource(update, updateMipLevelGL, &updateBaseLayer, &updateLayerCount);

            // If the update layers don't intersect the requested layers, skip the update.
            const bool areUpdateLayersOutsideRange =
                updateBaseLayer + updateLayerCount <= adjustedLayerStart ||
                updateBaseLayer >= adjustedLayerEnd;
            if (areUpdateLayersOutsideRange)
            {
                updatesToKeep.emplace_back(std::move(update));
                continue;
            }

            const LevelIndex updateMipLevelVk = toVkLevel(updateMipLevelGL);

            // It seems we haven't fully support glCopyImageSubData
            // when compressed format emulated by uncompressed format.
            // make assumption that there is no data source come from image.
            ASSERT(!transCoding || (transCoding && update.updateSource == UpdateSource::Buffer));
            // The updates were holding gl::OwnerLevel values so that they would not need
            // modification when the base level of the texture changes.  Now that the update is
            // about to take effect, we need to change miplevel to LevelIndex.
            switch (update.updateSource)
            {
                case UpdateSource::Clear:
                case UpdateSource::ClearAfterInvalidate:
                {
                    update.data.clear.levelIndex = updateMipLevelVk.get();
                    break;
                }
                case UpdateSource::ClearPartial:
                {
                    update.data.clearPartial.levelIndex = updateMipLevelVk.get();
                    break;
                }
                case UpdateSource::Buffer:
                {
                    if (!transCoding && !isDataFormatMatchForCopy(update.data.buffer.formatID))
                    {
                        // TODO: http://anglebug.com/42264884, we should handle this in higher level
                        // code. If we have incompatible updates, skip but keep it.
                        updatesToKeep.emplace_back(std::move(update));
                        continue;
                    }
                    update.data.buffer.copyRegion.imageSubresource.mipLevel =
                        updateMipLevelVk.get();
                    break;
                }
                case UpdateSource::Image:
                {
                    if (!isDataFormatMatchForCopy(update.data.image.formatID))
                    {
                        // If we have incompatible updates, skip but keep it.
                        updatesToKeep.emplace_back(std::move(update));
                        continue;
                    }
                    update.data.image.copyRegion.dstSubresource.mipLevel = updateMipLevelVk.get();
                    break;
                }
                default:
                {
                    UNREACHABLE();
                    break;
                }
            }

            // When a barrier is necessary when uploading updates to a level, we could instead move
            // to the next level and continue uploads in parallel.  Once all levels need a barrier,
            // a single barrier can be issued and we could continue with the rest of the updates
            // from the first level. In case of multiple layer updates within the same level, a
            // barrier might be needed if there are multiple updates in the same parts of the image.
            ImageAccess barrierAccess =
                transCoding ? ImageAccess::TransferDstAndComputeWrite : ImageAccess::TransferDst;
            if (updateLayerCount >= kMaxParallelLayerWrites)
            {
                // If there are more subresources than bits we can track, always insert a barrier.
                recordWriteBarrier(contextVk, aspectFlags, barrierAccess, updateMipLevelGL, 1,
                                   updateBaseLayer, updateLayerCount, commandBuffer);
                mSubresourcesWrittenSinceBarrier[updateMipLevelVk.get()].set();
            }
            else
            {
                ImageLayerWriteMask subresourceHash =
                    GetImageLayerWriteMask(updateBaseLayer, updateLayerCount);

                if (areLevelSubresourcesWrittenWithinMaskRange(updateMipLevelVk, subresourceHash))
                {
                    // If there's overlap in subresource upload, issue a barrier.
                    recordWriteBarrier(contextVk, aspectFlags, barrierAccess, updateMipLevelGL, 1,
                                       updateBaseLayer, updateLayerCount, commandBuffer);
                    mSubresourcesWrittenSinceBarrier[updateMipLevelVk.get()].reset();
                }
                mSubresourcesWrittenSinceBarrier[updateMipLevelVk.get()] |= subresourceHash;
            }

            // Add the necessary commands to the outside command buffer.
            switch (update.updateSource)
            {
                case UpdateSource::Clear:
                case UpdateSource::ClearAfterInvalidate:
                {
                    if (canTransferTo())
                    {
                        // If clearing only a slice of a 3D image, vkCmdClear*Image cannot be used.
                        const bool isSliceOf3D =
                            mImageType == VK_IMAGE_TYPE_3D &&
                            (updateBaseLayer.get() != 0 ||
                             (updateBaseLayer + updateLayerCount).get() !=
                                 static_cast<uint32_t>(getLevelExtents(updateMipLevelVk).depth));
                        if (isSliceOf3D)
                        {
                            const gl::Extents clearExtents = getLevelExtents(updateMipLevelVk);
                            const gl::Rectangle clearArea(0, 0, clearExtents.width,
                                                          clearExtents.height);
                            ASSERT(updateBaseLayer < clearExtents.depth);
                            ASSERT((updateBaseLayer + updateLayerCount).get() <=
                                   static_cast<uint32_t>(clearExtents.depth));

                            ANGLE_TRY(clearPartial(contextVk, update.data.clear.aspectFlags,
                                                   update.data.clear.value, updateMipLevelVk,
                                                   updateBaseLayer, updateLayerCount, clearArea,
                                                   &commandBuffer));
                        }
                        else
                        {
                            clear(renderer, update.data.clear.aspectFlags, update.data.clear.value,
                                  updateMipLevelVk, updateBaseLayer, updateLayerCount,
                                  &commandBuffer->getCommandBuffer());
                        }
                    }
                    else
                    {
                        ASSERT(mUseTileMemory);
                        ASSERT(mImageType != VK_IMAGE_TYPE_3D);

                        ANGLE_TRY(clearPartial(contextVk, getAspectFlags(), update.data.clear.value,
                                               updateMipLevelVk, updateBaseLayer, updateLayerCount,
                                               gl::Rectangle(0, 0, mExtents.width, mExtents.height),
                                               &commandBuffer));
                    }
                    contextVk->getPerfCounters().fullImageClears++;
                    // Remember the latest operation is a clear call.  Note that the tracked level
                    // is the GL level.
                    mCurrentSingleClearValue                    = update.data.clear;
                    mCurrentSingleClearValue.value().levelIndex = updateMipLevelGL.get();
                    break;
                }
                case UpdateSource::ClearPartial:
                {
                    const ClearPartialUpdate &clearPartialUpdate = update.data.clearPartial;
                    const gl::Rectangle clearArea =
                        gl::Rectangle(clearPartialUpdate.offset, clearPartialUpdate.extent);

                    ANGLE_TRY(clearPartial(contextVk, clearPartialUpdate.aspectFlags,
                                           clearPartialUpdate.clearValue, updateMipLevelVk,
                                           updateBaseLayer, updateLayerCount, clearArea,
                                           &commandBuffer));

                    break;
                }
                case UpdateSource::Buffer:
                {
                    BufferUpdate &bufferUpdate = update.data.buffer;

                    BufferHelper *currentBuffer = bufferUpdate.bufferHelper;
                    ASSERT(currentBuffer && currentBuffer->valid());
                    ANGLE_TRY(currentBuffer->flush(renderer));

                    CommandResources bufferAccess;
                    VkBufferImageCopy *copyRegion = &update.data.buffer.copyRegion;

                    if (transCoding && update.data.buffer.formatID != actualformat)
                    {
                        bufferAccess.onBufferComputeShaderRead(currentBuffer);
                        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBufferHelper(
                            bufferAccess, &commandBuffer));
                        ANGLE_TRY(contextVk->getUtils().transCodeEtcToBc(contextVk, currentBuffer,
                                                                         this, copyRegion));
                    }
                    else
                    {
                        bufferAccess.onBufferTransferRead(currentBuffer);
                        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBufferHelper(
                            bufferAccess, &commandBuffer));
                        commandBuffer->getCommandBuffer().copyBufferToImage(
                            currentBuffer->getBuffer().getHandle(), mImage,
                            getCurrentLayout(renderer), 1, copyRegion);
                    }
                    bool commandBufferWasFlushed = false;
                    ANGLE_TRY(contextVk->onCopyUpdate(currentBuffer->getSize(),
                                                      &commandBufferWasFlushed));
                    onWrite(updateMipLevelVk, 1, updateBaseLayer, updateLayerCount,
                            copyRegion->imageSubresource.aspectMask);

                    // Update total staging buffer size.
                    mTotalStagedBufferUpdateSize -= bufferUpdate.bufferHelper->getSize();

                    if (commandBufferWasFlushed)
                    {
                        ANGLE_TRY(
                            contextVk->getOutsideRenderPassCommandBufferHelper({}, &commandBuffer));
                    }
                    break;
                }
                case UpdateSource::Image:
                {
                    CommandResources imageAccess;
                    imageAccess.onImageTransferRead(aspectFlags, &update.refCounted.image->get());
                    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBufferHelper(imageAccess,
                                                                                 &commandBuffer));

                    VkImageCopy *copyRegion = &update.data.image.copyRegion;
                    commandBuffer->getCommandBuffer().copyImage(
                        update.refCounted.image->get().getImage(),
                        update.refCounted.image->get().getCurrentLayout(renderer), mImage,
                        getCurrentLayout(renderer), 1, copyRegion);
                    onWrite(updateMipLevelVk, 1, updateBaseLayer, updateLayerCount,
                            copyRegion->dstSubresource.aspectMask);
                    break;
                }
                default:
                {
                    UNREACHABLE();
                    break;
                }
            }

            update.release(renderer);
        }

        // Only remove the updates that were actually applied to the image.
        *levelUpdates = std::move(updatesToKeep);
    }

    // After applying the updates, the image serial should match the current queue serial of the
    // outside command buffer.
    if (mUse.getSerials()[commandBuffer->getQueueSerial().getIndex()] !=
        commandBuffer->getQueueSerial().getSerial())
    {
        // There has been a submission after the retainImage() call. Update the queue serial again.
        setQueueSerial(commandBuffer->getQueueSerial());
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::flushStagedUpdates(ContextVk *contextVk,
                                              gl::OwnerLevel levelGLStart,
                                              gl::OwnerLevel levelGLEnd,
                                              gl::OwnerLayer layerStart,
                                              gl::OwnerLayer layerEnd,
                                              const gl::CubeFaceArray<gl::TexLevelMask> &skipLevels)
{
    Renderer *renderer = contextVk->getRenderer();

    if (!hasStagedUpdatesInLevels(levelGLStart, levelGLEnd))
    {
        return angle::Result::Continue;
    }

    const gl::TexLevelMask skipLevelsAnyFace = AggregateSkipLevelsAnyFaceSkipped(skipLevels);
    removeSupersededUpdates(contextVk, skipLevelsAnyFace);

    // If a clear is requested and we know it was previously cleared with the same value, we drop
    // the clear.
    if (mCurrentSingleClearValue.valid())
    {
        const gl::OwnerLevel clearLevel(mCurrentSingleClearValue.value().levelIndex);
        if (clearLevel >= levelGLStart && clearLevel < levelGLEnd)
        {
            SubresourceUpdates *levelUpdates = getLevelUpdates(clearLevel);
            if (levelUpdates && levelUpdates->size() == 1)
            {
                SubresourceUpdate &update = (*levelUpdates)[0];
                if (IsClearOfAllChannels(update.updateSource) &&
                    mCurrentSingleClearValue.value() == update.data.clear)
                {
                    ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_LOW,
                                          "Repeated Clear on framebuffer attachment dropped");
                    update.release(renderer);
                    levelUpdates->clear();
                }
            }
        }
    }

    assertSubresourceUpdateRefCountsConsistent();

    // Process the clear emulated channels from the updates first. They are expected to be at the
    // beginning of the level updates.
    bool otherUpdatesToFlushOut = false;
    clipLevelToUpdateListUpperLimit(&levelGLEnd);
    ANGLE_TRY(flushStagedClearEmulatedChannelsUpdates(contextVk, levelGLStart, levelGLEnd,
                                                      &otherUpdatesToFlushOut));

    // If updates remain after processing ClearEmulatedChannelsOnly updates, we should acquire the
    // outside command buffer and apply the necessary barriers. Otherwise, this function can return
    // early, skipping the next loop.
    if (otherUpdatesToFlushOut)
    {
        ANGLE_TRY(flushStagedUpdatesImpl(contextVk, levelGLStart, levelGLEnd, layerStart, layerEnd,
                                         skipLevelsAnyFace));
    }

    // Compact mSubresourceUpdates, then check if there are any updates left.
    size_t compactSize;
    for (compactSize = mSubresourceUpdates.size(); compactSize > 0; --compactSize)
    {
        if (!mSubresourceUpdates[compactSize - 1].empty())
        {
            break;
        }
    }
    mSubresourceUpdates.resize(compactSize);

    assertSubresourceUpdateRefCountsConsistent();

    // If no updates left, release the staging buffers to save memory.
    if (mSubresourceUpdates.empty())
    {
        ASSERT(mTotalStagedBufferUpdateSize == 0);
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::flushAllStagedUpdates(ContextVk *contextVk)
{
    const gl::OwnerLayer firstLayer(0);
    return flushStagedUpdates(contextVk, mFirstAllocatedLevel, mFirstAllocatedLevel + mLevelCount,
                              firstLayer, firstLayer + mLayerCount, {});
}

bool ImageHelper::hasStagedUpdatesForSubresource(gl::OwnerLevel levelGL,
                                                 gl::OwnerLayer layer,
                                                 uint32_t layerCount) const
{
    // Check to see if any updates are staged for the given level and layer

    const SubresourceUpdates *levelUpdates = getLevelUpdates(levelGL);
    if (levelUpdates == nullptr || levelUpdates->empty())
    {
        return false;
    }

    for (const SubresourceUpdate &update : *levelUpdates)
    {
        gl::OwnerLayer updateBaseLayer;
        uint32_t updateLayerCount;
        getDestSubresource(update, levelGL, &updateBaseLayer, &updateLayerCount);

        const gl::OwnerLayer updateLayerEnd = updateBaseLayer + updateLayerCount;
        const gl::OwnerLayer layerEnd       = layer + layerCount;

        if ((layer >= updateBaseLayer && layer < updateLayerEnd) ||
            (layerEnd > updateBaseLayer && layerEnd <= updateLayerEnd))
        {
            // The layers intersect with the update range
            return true;
        }
    }

    return false;
}

bool ImageHelper::removeStagedClearUpdatesAndReturnColor(gl::OwnerLevel levelGL,
                                                         const VkClearColorValue **color)
{
    SubresourceUpdates *levelUpdates = getLevelUpdates(levelGL);
    if (levelUpdates == nullptr || levelUpdates->empty())
    {
        return false;
    }

    bool result = false;

    for (size_t index = 0; index < levelUpdates->size();)
    {
        auto update = levelUpdates->begin() + index;
        if (IsClearOfAllChannels(update->updateSource))
        {
            if (color != nullptr)
            {
                *color = &update->data.clear.value.color;
            }
            levelUpdates->erase(update);
            result = true;
        }
    }

    return result;
}

void ImageHelper::adjustLayerRange(const SubresourceUpdates &levelUpdates,
                                   const gl::OwnerLevel levelIndex,
                                   gl::OwnerLayer *layerStart,
                                   gl::OwnerLayer *layerEnd)
{
    for (const SubresourceUpdate &update : levelUpdates)
    {
        gl::OwnerLayer updateBaseLayer;
        uint32_t updateLayerCount;
        getDestSubresource(update, levelIndex, &updateBaseLayer, &updateLayerCount);
        const gl::OwnerLayer updateLayerEnd = updateBaseLayer + updateLayerCount;

        // In some cases, the update has the bigger layer range than the request. If the update
        // layers intersect the requested layers, then expand the layer range to the maximum from
        // the update and from the request.
        const bool areUpdateLayersWithinRange =
            updateBaseLayer < *layerEnd && updateLayerEnd > *layerStart;
        if (areUpdateLayersWithinRange)
        {
            *layerStart = std::min(*layerStart, updateBaseLayer);
            *layerEnd   = std::max(*layerEnd, updateLayerEnd);
        }
    }
}

gl::OwnerLevel ImageHelper::getLastAllocatedLevel() const
{
    return mFirstAllocatedLevel + (mLevelCount - 1);
}

bool ImageHelper::hasStagedUpdatesInAllocatedLevels() const
{
    return hasStagedUpdatesInLevels(mFirstAllocatedLevel, getLastAllocatedLevel() + 1);
}

bool ImageHelper::hasStagedUpdatesInLevels(gl::OwnerLevel levelStart, gl::OwnerLevel levelEnd) const
{
    for (gl::OwnerLevel level = levelStart; level < levelEnd; ++level)
    {
        const SubresourceUpdates *levelUpdates = getLevelUpdates(level);
        if (levelUpdates == nullptr)
        {
            ASSERT(static_cast<size_t>(level.get()) >= mSubresourceUpdates.size());
            return false;
        }

        if (!levelUpdates->empty())
        {
            return true;
        }
    }
    return false;
}

bool ImageHelper::hasBufferSourcedStagedUpdatesInAllLevels() const
{
    for (gl::OwnerLevel level = mFirstAllocatedLevel; level <= getLastAllocatedLevel(); ++level)
    {
        const SubresourceUpdates *levelUpdates = getLevelUpdates(level);
        if (levelUpdates == nullptr || levelUpdates->empty())
        {
            return false;
        }

        bool hasUpdateSourceWithBufferOrPartialClear = false;
        for (const SubresourceUpdate &update : *levelUpdates)
        {
            if (update.updateSource == UpdateSource::Buffer ||
                update.updateSource == UpdateSource::ClearPartial)
            {
                hasUpdateSourceWithBufferOrPartialClear = true;
                break;
            }
        }
        if (!hasUpdateSourceWithBufferOrPartialClear)
        {
            return false;
        }
    }
    return true;
}

void ImageHelper::assertSubresourceUpdateBufferRefConsistentImpl(
    RefCounted<BufferHelper> *buffer) const
{
    if (buffer == nullptr)
    {
        return;
    }

    uint32_t refs = 0;

    for (const SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (const SubresourceUpdate &update : levelUpdates)
        {
            if (update.updateSource == UpdateSource::Buffer && update.refCounted.buffer == buffer)
            {
                ++refs;
            }
        }
    }

    buffer->assertIsRefCountAsExpected(refs);
}

void ImageHelper::assertSubresourceUpdateImageRefConsistentImpl(
    RefCounted<ImageHelper> *image) const
{
    if (image == nullptr)
    {
        return;
    }

    uint32_t refs = 0;

    for (const SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (const SubresourceUpdate &update : levelUpdates)
        {
            if (update.updateSource == UpdateSource::Image && update.refCounted.image == image)
            {
                ++refs;
            }
        }
    }

    image->assertIsRefCountAsExpected(refs);
}

void ImageHelper::assertSubresourceUpdateRefCountsConsistentImpl() const
{
    for (const SubresourceUpdates &levelUpdates : mSubresourceUpdates)
    {
        for (const SubresourceUpdate &update : levelUpdates)
        {
            if (update.updateSource == UpdateSource::Image)
            {
                assertSubresourceUpdateImageRefConsistentImpl(update.refCounted.image);
            }
            else if (update.updateSource == UpdateSource::Buffer)
            {
                assertSubresourceUpdateBufferRefConsistentImpl(update.refCounted.buffer);
            }
        }
    }
}

void ImageHelper::pruneSupersededUpdatesForLevel(ContextVk *contextVk,
                                                 const gl::OwnerLevel level,
                                                 const PruneReason reason)
{
    constexpr VkDeviceSize kSubresourceUpdateSizeBeforePruning = 16 * 1024 * 1024;  // 16 MB
    constexpr int kUpdateCountThreshold                        = 1024;
    SubresourceUpdates *levelUpdates                           = getLevelUpdates(level);

    // If we are below pruning threshold, nothing to do.
    const int updateCount      = static_cast<int>(levelUpdates->size());
    const bool withinThreshold = updateCount < kUpdateCountThreshold &&
                                 mTotalStagedBufferUpdateSize < kSubresourceUpdateSizeBeforePruning;
    if (updateCount == 1 || (reason == PruneReason::MemoryOptimization && withinThreshold))
    {
        return;
    }

    pruneSupersededUpdatesForLevelImpl(contextVk, level, {}, reason);
}

void ImageHelper::pruneSupersededUpdatesForLevelImpl(ContextVk *contextVk,
                                                     const gl::OwnerLevel level,
                                                     const gl::Box &upcomingUpdateBoundingBox,
                                                     const PruneReason reason)
{
    SubresourceUpdates *levelUpdates = getLevelUpdates(level);
    if (levelUpdates == nullptr || levelUpdates->size() == 0)
    {
        return;
    }

    // ClearEmulatedChannelsOnly updates can only be in the beginning of the list of updates.
    // They don't entirely clear the image, so they cannot supersede any update.
    ASSERT(verifyEmulatedClearsAreBeforeOtherUpdates(*levelUpdates));

    // Start from the most recent update and define a boundingBox that covers the region to be
    // updated. Walk through all earlier updates and if its update region is contained within the
    // boundingBox, mark it as superseded, otherwise reset the boundingBox and continue.
    //
    // Color, depth and stencil are the only types supported for now. The boundingBox for color and
    // depth types is at index 0 and index 1 has the boundingBox for stencil type.
    //
    // |upcomingUpdateBoundingBox| is non-empty in case an update is about to happen but is not
    // staged.  In that case, that unstaged update can also supersede the staged updates.
    VkDeviceSize supersededUpdateSize  = 0;
    std::array<gl::Box, 2> boundingBox = {upcomingUpdateBoundingBox, upcomingUpdateBoundingBox};

    auto canDropUpdate = [this, contextVk, level, reason, &supersededUpdateSize,
                          &boundingBox](SubresourceUpdate &update) {
        if (IsClear(update.updateSource) && reason == PruneReason::MemoryOptimization)
        {
            return false;
        }
        VkDeviceSize updateSize       = 0;
        VkImageAspectFlags aspectMask = update.getDestAspectFlags();

        const bool isColor =
            (aspectMask & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_PLANE_0_BIT |
                           VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT)) != 0;
        const bool isDepth   = (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        const bool isStencil = (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
        ASSERT(isColor || isDepth || isStencil);

        gl::OwnerLayer layerIndex;
        uint32_t layerCount = 0;
        getDestSubresource(update, level, &layerIndex, &layerCount);

        gl::Box currentUpdateBox(gl::kOffsetZero, gl::Extents());
        if (update.updateSource == UpdateSource::Buffer)
        {
            currentUpdateBox = MakeUpdateBoundingBox(update.data.buffer.copyRegion.imageOffset,
                                                     update.data.buffer.copyRegion.imageExtent,
                                                     layerIndex, layerCount);
            updateSize       = update.data.buffer.bufferHelper->getSize();
        }
        else if (update.updateSource == UpdateSource::Image)
        {
            currentUpdateBox =
                MakeUpdateBoundingBox(update.data.image.copyRegion.dstOffset,
                                      update.data.image.copyRegion.extent, layerIndex, layerCount);
        }
        else if (update.updateSource == UpdateSource::ClearPartial)
        {
            const ClearPartialUpdate &clearPartial = update.data.clearPartial;
            const VkOffset3D offset = {clearPartial.offset.x, clearPartial.offset.y, 0};
            const VkExtent3D extent = {clearPartial.extent.width, clearPartial.extent.height, 1};
            currentUpdateBox        = MakeUpdateBoundingBox(offset, extent, layerIndex, layerCount);
        }
        else
        {
            ASSERT(IsClear(update.updateSource));
            currentUpdateBox = MakeUpdateBoundingBox(
                gl::kOffsetZero, getLevelExtents(toVkLevel(level)), layerIndex, layerCount);
        }

        // Check if current update region is superseded by the accumulated update region
        const bool aspect0Superseded =
            !(isColor || isDepth) || boundingBox[0].contains(currentUpdateBox);
        const bool aspect1Superseded = !isStencil || boundingBox[1].contains(currentUpdateBox);
        if (aspect0Superseded && aspect1Superseded)
        {
            // Warn that the app did something useless.  In case of ClearEmulatedChannelsOnly, a
            // clear is staged by ANGLE not the app, so no need to warn in that case.
            if (update.updateSource != UpdateSource::ClearEmulatedChannelsOnly)
            {
                ANGLE_VK_PERF_WARNING(
                    contextVk, GL_DEBUG_SEVERITY_LOW,
                    "Dropped texture update that is superseded by a more recent one");
            }

            // Release the superseded update
            update.release(contextVk->getRenderer());

            // Update pruning size
            supersededUpdateSize += updateSize;

            return true;
        }
        else
        {
            const int aspectIndexStart = (isColor || isDepth) ? 0 : 1;
            const int aspectIndexEnd   = isStencil ? 1 : 0;
            for (int aspectIndex = aspectIndexStart; aspectIndex <= aspectIndexEnd; ++aspectIndex)
            {
                // Extend boundingBox to best accommodate current update's box.
                boundingBox[aspectIndex].extend(currentUpdateBox);
                // If the volume of the current update box is larger than the extended boundingBox
                // use that as the new boundingBox instead.
                if (currentUpdateBox.volume() > boundingBox[aspectIndex].volume())
                {
                    boundingBox[aspectIndex] = currentUpdateBox;
                }
            }
            return false;
        }
    };

    levelUpdates->erase(
        levelUpdates->begin(),
        std::remove_if(levelUpdates->rbegin(), levelUpdates->rend(), canDropUpdate).base());

    // Update total staging buffer size
    mTotalStagedBufferUpdateSize -= supersededUpdateSize;

    assertSubresourceUpdateRefCountsConsistent();
}

void ImageHelper::removeSupersededUpdates(ContextVk *contextVk, const gl::TexLevelMask skipLevels)
{
    assertSubresourceUpdateRefCountsConsistent();

    for (LevelIndex levelVk(0); levelVk < LevelIndex(mLevelCount); ++levelVk)
    {
        gl::OwnerLevel levelGL                       = toGLLevel(levelVk);
        SubresourceUpdates *levelUpdates             = getLevelUpdates(levelGL);
        if (levelUpdates == nullptr || levelUpdates->size() == 0 || skipLevels.test(levelGL.get()))
        {
            // There are no valid updates to process, continue.
            continue;
        }

        pruneSupersededUpdatesForLevel(contextVk, levelGL, PruneReason::MinimizeWorkBeforeFlush);
    }
}

angle::Result ImageHelper::copyImageDataToBuffer(ContextVk *contextVk,
                                                 gl::OwnerLevel sourceLevelGL,
                                                 uint32_t layerCount,
                                                 gl::OwnerLayer baseLayer,
                                                 const gl::Box &sourceArea,
                                                 BufferHelper *dstBuffer,
                                                 uint8_t **outDataPtr)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::copyImageDataToBuffer");
    Renderer *renderer               = contextVk->getRenderer();
    const angle::Format &imageFormat = getActualFormat();

    // As noted in the OpenGL ES 3.2 specs, table 8.13, CopyTexImage cannot
    // be used for depth textures. There is no way for the image or buffer
    // used in this function to be of some combined depth and stencil format.
    ASSERT(getAspectFlags() == VK_IMAGE_ASPECT_COLOR_BIT);

    const size_t pixelBytes = imageFormat.pixelBytes;
    const size_t bufferSize = static_cast<size_t>(sourceArea.width) * sourceArea.height *
                              sourceArea.depth * pixelBytes * layerCount;

    const VkImageAspectFlags aspectFlags = getAspectFlags();

    // Allocate staging buffer prefer coherent
    ASSERT(dstBuffer != nullptr && !dstBuffer->valid());
    VkDeviceSize dstOffset;
    ANGLE_TRY(contextVk->initBufferForImageCopy(dstBuffer, bufferSize,
                                                MemoryCoherency::CachedPreferCoherent,
                                                imageFormat.id, &dstOffset, outDataPtr));
    ANGLE_TRY(dstBuffer->flush(renderer));

    VkBuffer bufferHandle = dstBuffer->getBuffer().getHandle();

    LevelIndex sourceLevelVk = toVkLevel(sourceLevelGL);

    VkBufferImageCopy regions = {};
    uint32_t regionCount      = 1;
    // Default to non-combined DS case
    regions.bufferOffset                    = dstOffset;
    regions.bufferRowLength                 = 0;
    regions.bufferImageHeight               = 0;
    regions.imageExtent.width               = sourceArea.width;
    regions.imageExtent.height              = sourceArea.height;
    regions.imageExtent.depth               = sourceArea.depth;
    regions.imageOffset.x                   = sourceArea.x;
    regions.imageOffset.y                   = sourceArea.y;
    regions.imageOffset.z                   = sourceArea.z;
    regions.imageSubresource.aspectMask     = aspectFlags;
    regions.imageSubresource.baseArrayLayer = baseLayer.get();
    regions.imageSubresource.layerCount     = layerCount;
    regions.imageSubresource.mipLevel       = sourceLevelVk.get();

    CommandResources resources;
    resources.onBufferTransferWrite(dstBuffer);
    resources.onImageTransferRead(aspectFlags, this);

    OutsideRenderPassCommandBuffer *commandBuffer;
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

    commandBuffer->copyImageToBuffer(mImage, getCurrentLayout(renderer), bufferHandle, regionCount,
                                     &regions);

    return angle::Result::Continue;
}

angle::Result ImageHelper::copySurfaceImageToBuffer(ErrorContext *context,
                                                    gl::OwnerLevel sourceLevelGL,
                                                    uint32_t layerCount,
                                                    gl::OwnerLayer baseLayer,
                                                    const gl::Box &sourceArea,
                                                    vk::BufferHelper *bufferHelper)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::copySurfaceImageToBuffer");

    Renderer *renderer = context->getRenderer();

    VkBufferImageCopy region               = {};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageExtent.width               = sourceArea.width;
    region.imageExtent.height              = sourceArea.height;
    region.imageExtent.depth               = sourceArea.depth;
    region.imageOffset.x                   = sourceArea.x;
    region.imageOffset.y                   = sourceArea.y;
    region.imageOffset.z                   = sourceArea.z;
    region.imageSubresource.aspectMask     = getAspectFlags();
    region.imageSubresource.baseArrayLayer = baseLayer.get();
    region.imageSubresource.layerCount     = layerCount;
    region.imageSubresource.mipLevel       = toVkLevel(sourceLevelGL).get();

    ScopedPrimaryCommandBuffer scopedCommandBuffer(renderer->getDevice());
    ANGLE_TRY(renderer->getCommandBufferOneOff(context, ProtectionType::Unprotected,
                                               &scopedCommandBuffer));
    PrimaryCommandBuffer &primaryCommandBuffer = scopedCommandBuffer.get();

    VkSemaphore acquireNextImageSemaphore;
    recordBarrierOneOffImpl(renderer, getAspectFlags(), ImageAccess::TransferSrc,
                            context->getDeviceQueueIndex(), &primaryCommandBuffer,
                            &acquireNextImageSemaphore);
    primaryCommandBuffer.copyImageToBuffer(mImage, getCurrentLayout(renderer),
                                           bufferHelper->getBuffer().getHandle(), 1, &region);

    renderer->insertSubmitDebugMarkerInCommandBuffer(primaryCommandBuffer,
                                                     QueueSubmitReason::CopySurfaceImageToBuffer);
    ANGLE_VK_TRY(context, primaryCommandBuffer.end());

    QueueSerial submitQueueSerial;
    ANGLE_TRY(renderer->queueSubmitOneOff(
        context, std::move(scopedCommandBuffer), ProtectionType::Unprotected,
        egl::ContextPriority::Medium, acquireNextImageSemaphore,
        kSwapchainAcquireImageWaitStageFlags, &submitQueueSerial));

    return renderer->finishQueueSerial(context, submitQueueSerial);
}

angle::Result ImageHelper::copyBufferToSurfaceImage(ErrorContext *context,
                                                    gl::OwnerLevel sourceLevelGL,
                                                    uint32_t layerCount,
                                                    gl::OwnerLayer baseLayer,
                                                    const gl::Box &sourceArea,
                                                    vk::BufferHelper *bufferHelper)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::copyBufferToSurfaceImage");

    Renderer *renderer = context->getRenderer();

    VkBufferImageCopy region               = {};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageExtent.width               = sourceArea.width;
    region.imageExtent.height              = sourceArea.height;
    region.imageExtent.depth               = sourceArea.depth;
    region.imageOffset.x                   = sourceArea.x;
    region.imageOffset.y                   = sourceArea.y;
    region.imageOffset.z                   = sourceArea.z;
    region.imageSubresource.aspectMask     = getAspectFlags();
    region.imageSubresource.baseArrayLayer = baseLayer.get();
    region.imageSubresource.layerCount     = layerCount;
    region.imageSubresource.mipLevel       = toVkLevel(sourceLevelGL).get();

    ScopedPrimaryCommandBuffer scopedCommandBuffer(renderer->getDevice());
    ANGLE_TRY(renderer->getCommandBufferOneOff(context, ProtectionType::Unprotected,
                                               &scopedCommandBuffer));
    PrimaryCommandBuffer &commandBuffer = scopedCommandBuffer.get();

    VkSemaphore acquireNextImageSemaphore;
    recordBarrierOneOffImpl(renderer, getAspectFlags(), ImageAccess::TransferDst,
                            context->getDeviceQueueIndex(), &commandBuffer,
                            &acquireNextImageSemaphore);
    commandBuffer.copyBufferToImage(bufferHelper->getBuffer().getHandle(), mImage,
                                    getCurrentLayout(renderer), 1, &region);

    renderer->insertSubmitDebugMarkerInCommandBuffer(commandBuffer,
                                                     QueueSubmitReason::CopyBufferToSurfaceImage);
    ANGLE_VK_TRY(context, commandBuffer.end());

    QueueSerial submitQueueSerial;
    ANGLE_TRY(renderer->queueSubmitOneOff(
        context, std::move(scopedCommandBuffer), ProtectionType::Unprotected,
        egl::ContextPriority::Medium, acquireNextImageSemaphore,
        kSwapchainAcquireImageWaitStageFlags, &submitQueueSerial));

    return renderer->finishQueueSerial(context, submitQueueSerial);
}

// static
angle::Result ImageHelper::GetReadPixelsParams(ContextVk *contextVk,
                                               const gl::PixelPackState &packState,
                                               gl::Buffer *packBuffer,
                                               GLenum format,
                                               GLenum type,
                                               const gl::Rectangle &area,
                                               const gl::Rectangle &clippedArea,
                                               PackPixelsParams *paramsOut,
                                               GLuint *skipBytesOut)
{
    const gl::InternalFormat &sizedFormatInfo = gl::GetInternalFormatInfo(format, type);

    GLuint outputPitch = 0;
    ANGLE_VK_CHECK_MATH(contextVk, sizedFormatInfo.computeRowSkipBytes(type, area.width, packState,
                                                                       &outputPitch, skipBytesOut));

    ANGLE_TRY(GetPackPixelsParams(sizedFormatInfo, outputPitch, packState, packBuffer, area,
                                  clippedArea, paramsOut, skipBytesOut));
    return angle::Result::Continue;
}

angle::Result ImageHelper::readPixelsForGetImage(ContextVk *contextVk,
                                                 const gl::PixelPackState &packState,
                                                 gl::Buffer *packBuffer,
                                                 gl::OwnerLevel levelGL,
                                                 gl::OwnerLayer layer,
                                                 uint32_t layerCount,
                                                 GLenum format,
                                                 GLenum type,
                                                 void *pixels)
{
    const angle::Format &angleFormat = GetFormatFromFormatType(format, type);

    VkImageAspectFlagBits aspectFlags = {};
    if (angleFormat.redBits > 0 || angleFormat.blueBits > 0 || angleFormat.greenBits > 0 ||
        angleFormat.alphaBits > 0 || angleFormat.luminanceBits > 0)
    {
        aspectFlags = static_cast<VkImageAspectFlagBits>(aspectFlags | VK_IMAGE_ASPECT_COLOR_BIT);
    }
    else
    {
        if (angleFormat.depthBits > 0)
        {
            aspectFlags =
                static_cast<VkImageAspectFlagBits>(aspectFlags | VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        if (angleFormat.stencilBits > 0)
        {
            aspectFlags =
                static_cast<VkImageAspectFlagBits>(aspectFlags | VK_IMAGE_ASPECT_STENCIL_BIT);
        }
    }

    ASSERT(aspectFlags != 0);

    PackPixelsParams params;
    GLuint outputSkipBytes = 0;

    const LevelIndex levelVk     = toVkLevel(levelGL);
    const gl::Extents mipExtents = getLevelExtents(levelVk);
    gl::Rectangle area(0, 0, mipExtents.width, mipExtents.height);

    ANGLE_TRY(GetReadPixelsParams(contextVk, packState, packBuffer, format, type, area, area,
                                  &params, &outputSkipBytes));

    if (mExtents.depth > 1 || layerCount > 1)
    {
        const gl::OwnerLayer lastLayer = layer + layerCount;

        for (gl::OwnerLayer mipLayer = layer; mipLayer < lastLayer; ++mipLayer)
        {
            ANGLE_UNSAFE_TODO(
                ANGLE_TRY(readPixels(contextVk, area, params, aspectFlags, levelGL, mipLayer,
                                     static_cast<uint8_t *>(pixels) + outputSkipBytes)));

            outputSkipBytes += mipExtents.width * mipExtents.height *
                               gl::GetInternalFormatInfo(format, type).pixelBytes;
        }
    }
    else
    {
        ANGLE_UNSAFE_TODO(ANGLE_TRY(readPixels(contextVk, area, params, aspectFlags, levelGL, layer,
                                               static_cast<uint8_t *>(pixels) + outputSkipBytes)));
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::readPixelsForCompressedGetImage(ContextVk *contextVk,
                                                           const gl::PixelPackState &packState,
                                                           gl::Buffer *packBuffer,
                                                           gl::OwnerLevel levelGL,
                                                           gl::OwnerLayer layer,
                                                           uint32_t layerCount,
                                                           void *pixels)
{
    PackPixelsParams params;
    GLuint outputSkipBytes = 0;

    const LevelIndex levelVk = toVkLevel(levelGL);
    gl::Extents mipExtents   = getLevelExtents(levelVk);
    gl::Rectangle area(0, 0, mipExtents.width, mipExtents.height);

    VkImageAspectFlagBits aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

    const angle::Format *readFormat = &getActualFormat();

    // TODO(anglebug.com/42264702): Implement encoding for emuluated compression formats
    ANGLE_VK_CHECK(contextVk, readFormat->isBlock, VK_ERROR_FORMAT_NOT_SUPPORTED);

    if (mExtents.depth > 1 || layerCount > 1)
    {
        const gl::OwnerLayer lastLayer = layer + layerCount;

        const vk::Format &vkFormat = contextVk->getRenderer()->getFormat(readFormat->id);
        const gl::InternalFormat &storageFormatInfo =
            vkFormat.getInternalFormatInfo(readFormat->componentType);

        // Calculate size for one layer
        mipExtents.depth = 1;
        GLuint layerSize;
        ANGLE_VK_CHECK_MATH(contextVk,
                            storageFormatInfo.computeCompressedImageSize(mipExtents, &layerSize));

        for (gl::OwnerLayer mipLayer = layer; mipLayer < lastLayer; ++mipLayer)
        {
            ANGLE_UNSAFE_TODO(
                ANGLE_TRY(readPixels(contextVk, area, params, aspectFlags, levelGL, mipLayer,
                                     static_cast<uint8_t *>(pixels) + outputSkipBytes)));
            outputSkipBytes += layerSize;
        }
    }
    else
    {
        ANGLE_UNSAFE_TODO(ANGLE_TRY(readPixels(contextVk, area, params, aspectFlags, levelGL, layer,
                                               static_cast<uint8_t *>(pixels) + outputSkipBytes)));
    }

    return angle::Result::Continue;
}

angle::Result ImageHelper::readPixelsWithCompute(ContextVk *contextVk,
                                                 ImageHelper *src,
                                                 const PackPixelsParams &packPixelsParams,
                                                 const VkOffset3D &srcOffset,
                                                 const VkExtent3D &srcExtent,
                                                 ptrdiff_t pixelsOffset,
                                                 const VkImageSubresourceLayers &srcSubresource)
{
    ASSERT(srcOffset.z == 0 || srcSubresource.baseArrayLayer == 0);

    UtilsVk::CopyImageToBufferParameters params = {};
    params.srcOffset[0]                         = srcOffset.x;
    params.srcOffset[1]                         = srcOffset.y;
    params.srcLayer = LayerIndex(std::max<uint32_t>(srcOffset.z, srcSubresource.baseArrayLayer));
    params.srcMip          = LevelIndex(srcSubresource.mipLevel);
    params.size[0]         = srcExtent.width;
    params.size[1]         = srcExtent.height;
    params.outputOffset    = packPixelsParams.offset + pixelsOffset;
    params.outputPitch     = packPixelsParams.outputPitch;
    params.reverseRowOrder = packPixelsParams.reverseRowOrder;
    params.outputFormat    = packPixelsParams.destFormat;

    BufferHelper &packBuffer = GetImpl(packPixelsParams.packBuffer)->getBuffer();

    return contextVk->getUtils().copyImageToBuffer(contextVk, &packBuffer, src, params);
}

bool ImageHelper::canCopyWithTransformForReadPixels(const PackPixelsParams &packPixelsParams,
                                                    const VkExtent3D &srcExtent,
                                                    const angle::Format *readFormat,
                                                    ptrdiff_t pixelsOffset)
{
    ASSERT(mActualFormatID != angle::FormatID::NONE && mIntendedFormatID != angle::FormatID::NONE);

    // Only allow copies to PBOs with identical format.
    const bool isSameFormatCopy = *readFormat == *packPixelsParams.destFormat;

    // Disallow any transformation.
    const bool needsTransformation =
        packPixelsParams.rotation != SurfaceRotation::Identity || packPixelsParams.reverseRowOrder;

    // Disallow copies when the output pitch cannot be correctly specified in Vulkan.
    const bool isPitchMultipleOfTexelSize =
        packPixelsParams.outputPitch % readFormat->pixelBytes == 0;

    // Disallow copies when PBO offset violates Vulkan bufferOffset alignment requirements.
    const BufferHelper &packBuffer = GetImpl(packPixelsParams.packBuffer)->getBuffer();
    const VkDeviceSize offset = packBuffer.getOffset() + packPixelsParams.offset + pixelsOffset;
    const bool isOffsetMultipleOfTexelSize = offset % readFormat->pixelBytes == 0;

    // Disallow copies when PBO row length is smaller than the source area width.
    const bool isRowLengthEnough =
        packPixelsParams.outputPitch >= srcExtent.width * readFormat->pixelBytes;

    // Don't allow copies from emulated formats for simplicity.
    return !hasEmulatedImageFormat() && isSameFormatCopy && !needsTransformation &&
           isPitchMultipleOfTexelSize && isOffsetMultipleOfTexelSize && isRowLengthEnough &&
           canTransferFrom();
}

bool ImageHelper::canCopyWithComputeForReadPixels(const PackPixelsParams &packPixelsParams,
                                                  const VkExtent3D &srcExtent,
                                                  const angle::Format *readFormat,
                                                  ptrdiff_t pixelsOffset)
{
    ASSERT(mActualFormatID != angle::FormatID::NONE && mIntendedFormatID != angle::FormatID::NONE);
    const angle::Format *writeFormat = packPixelsParams.destFormat;

    // For now, only float formats are supported with 4-byte 4-channel normalized pixels for output.
    const bool isFloat =
        !readFormat->isSint() && !readFormat->isUint() && !readFormat->hasDepthOrStencilBits();
    const bool isFourByteOutput   = writeFormat->pixelBytes == 4 && writeFormat->channelCount == 4;
    const bool isNormalizedOutput = writeFormat->isUnorm() || writeFormat->isSnorm();

    // Disallow rotation.
    const bool needsTransformation = packPixelsParams.rotation != SurfaceRotation::Identity;

    // Disallow copies when the output pitch cannot be correctly specified in Vulkan.
    const bool isPitchMultipleOfTexelSize =
        packPixelsParams.outputPitch % readFormat->pixelBytes == 0;

    // Disallow copies when the output offset is not aligned to uint32_t
    const bool isOffsetMultipleOfUint =
        (packPixelsParams.offset + pixelsOffset) % readFormat->pixelBytes == 0;

    // Disallow copies when PBO row length is smaller than the source area width.
    const bool isRowLengthEnough =
        packPixelsParams.outputPitch >= srcExtent.width * readFormat->pixelBytes;

    return isFloat && isFourByteOutput && isNormalizedOutput && !needsTransformation &&
           isPitchMultipleOfTexelSize && isOffsetMultipleOfUint && isRowLengthEnough;
}

angle::Result ImageHelper::readPixels(ContextVk *contextVk,
                                      const gl::Rectangle &area,
                                      const PackPixelsParams &packPixelsParams,
                                      VkImageAspectFlagBits copyAspectFlags,
                                      gl::OwnerLevel levelGL,
                                      gl::OwnerLayer layer,
                                      void *pixels)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::readPixels");

    const angle::Format &readFormat = getActualFormat();

    if (readFormat.depthBits == 0)
    {
        copyAspectFlags =
            static_cast<VkImageAspectFlagBits>(copyAspectFlags & ~VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    if (readFormat.stencilBits == 0)
    {
        copyAspectFlags =
            static_cast<VkImageAspectFlagBits>(copyAspectFlags & ~VK_IMAGE_ASPECT_STENCIL_BIT);
    }

    if (copyAspectFlags == IMAGE_ASPECT_DEPTH_STENCIL)
    {
        const angle::Format &depthFormat =
            GetDepthStencilImageToBufferFormat(readFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        const angle::Format &stencilFormat =
            GetDepthStencilImageToBufferFormat(readFormat, VK_IMAGE_ASPECT_STENCIL_BIT);

        int depthOffset   = 0;
        int stencilOffset = 0;
        switch (readFormat.id)
        {
            case angle::FormatID::D24_UNORM_S8_UINT:
                depthOffset   = 1;
                stencilOffset = 0;
                break;

            case angle::FormatID::D32_FLOAT_S8X24_UINT:
                depthOffset   = 0;
                stencilOffset = 4;
                break;

            default:
                UNREACHABLE();
        }

        ASSERT(depthOffset > 0 || stencilOffset > 0);
        ASSERT(depthOffset + depthFormat.depthBits / 8 <= readFormat.pixelBytes);
        ASSERT(stencilOffset + stencilFormat.stencilBits / 8 <= readFormat.pixelBytes);
        const size_t areaWidth  = static_cast<size_t>(area.width);
        const size_t areaHeight = static_cast<size_t>(area.height);

        // Read the depth values, tightly-packed
        angle::MemoryBuffer depthBuffer;
        const size_t outputDepthPitch = areaWidth * depthFormat.pixelBytes;
        const size_t depthBufferSize  = outputDepthPitch * areaHeight;
        ANGLE_VK_CHECK_ALLOC(contextVk, depthBuffer.resize(depthBufferSize));
        ANGLE_TRY(readPixelsImpl(
            contextVk, area,
            PackPixelsParams(area, depthFormat, static_cast<GLuint>(outputDepthPitch), false,
                             nullptr, 0),
            VK_IMAGE_ASPECT_DEPTH_BIT, levelGL, layer, depthBuffer.data()));

        // Read the stencil values, tightly-packed
        angle::MemoryBuffer stencilBuffer;
        const size_t outputStencilPitch = areaWidth * stencilFormat.pixelBytes;
        const size_t stencilBufferSize  = outputStencilPitch * areaHeight;
        ANGLE_VK_CHECK_ALLOC(contextVk, stencilBuffer.resize(stencilBufferSize));
        ANGLE_TRY(readPixelsImpl(
            contextVk, area,
            PackPixelsParams(area, stencilFormat, static_cast<GLuint>(outputStencilPitch), false,
                             nullptr, 0),
            VK_IMAGE_ASPECT_STENCIL_BIT, levelGL, layer, stencilBuffer.data()));

        // Interleave them together
        angle::MemoryBuffer readPixelBuffer;
        const size_t readPixelArea       = areaWidth * areaHeight;
        const size_t readPixelBufferSize = readPixelArea * readFormat.pixelBytes;
        ANGLE_VK_CHECK_ALLOC(contextVk, readPixelBuffer.resize(readPixelBufferSize));
        readPixelBuffer.fill(0);
        for (size_t i = 0; i < readPixelArea; i++)
        {
            uint8_t *readPixel =
                ANGLE_UNSAFE_TODO(readPixelBuffer.data() + i * readFormat.pixelBytes);
            ANGLE_UNSAFE_TODO({
                memcpy(readPixel + depthOffset, depthBuffer.data() + i * depthFormat.pixelBytes,
                       depthFormat.depthBits / 8);
                memcpy(readPixel + stencilOffset,
                       stencilBuffer.data() + i * stencilFormat.pixelBytes,
                       stencilFormat.stencilBits / 8);
            })
        }

        // Pack the interleaved depth and stencil into user-provided
        // destination, per user's pack pixels params

        // The compressed format path in packReadPixelBuffer isn't applicable
        // to our case, let's make extra sure we won't hit it
        ASSERT(!readFormat.isBlock);
        return packReadPixelBuffer(contextVk, area, packPixelsParams, readFormat, readFormat,
                                   readPixelBuffer.data(), levelGL, pixels);
    }

    return readPixelsImpl(contextVk, area, packPixelsParams, copyAspectFlags, levelGL, layer,
                          pixels);
}

angle::Result ImageHelper::readPixelsImpl(ContextVk *contextVk,
                                          const gl::Rectangle &area,
                                          const PackPixelsParams &packPixelsParams,
                                          VkImageAspectFlagBits copyAspectFlags,
                                          gl::OwnerLevel levelGL,
                                          gl::OwnerLayer layer,
                                          void *pixels)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::readPixelsImpl");

    Renderer *renderer = contextVk->getRenderer();

    bool isExternalFormat = getExternalFormat() != 0;

    // Vulkan spec says that mYcbcrConversionDesc is always set for samplable external-format
    // images
    ASSERT(!isExternalFormat || mYcbcrConversionDesc.valid());

    // If the source image is multisampled, we need to resolve it into a temporary image before
    // performing a readback.
    bool isMultisampled = mSamples > 1;
    RendererScoped<ImageHelper> resolvedImage(contextVk->getRenderer());

    ImageHelper *src = this;

    ASSERT(!hasStagedUpdatesForSubresource(levelGL, layer, 1));

    if (isMultisampled)
    {
        ANGLE_TRY(resolvedImage.get().init2DStaging(
            contextVk, contextVk->getState().hasProtectedContent(),
            gl::Extents(area.width, area.height, 1), mIntendedFormatID, mActualFormatID,
            vk::kImageUsageTransferBits | VK_IMAGE_USAGE_SAMPLED_BIT, 1));
    }
    else if (isExternalFormat)
    {
        ANGLE_TRY(resolvedImage.get().init2DStaging(
            contextVk, contextVk->getState().hasProtectedContent(),
            gl::Extents(area.width, area.height, 1), angle::FormatID::R8G8B8A8_UNORM,
            angle::FormatID::R8G8B8A8_UNORM,
            vk::kImageUsageTransferBits | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            1));
    }

    VkImageAspectFlags layoutChangeAspectFlags = src->getAspectFlags();

    const angle::Format *rgbaFormat = &angle::Format::Get(angle::FormatID::R8G8B8A8_UNORM);
    const angle::Format *readFormat = isExternalFormat ? rgbaFormat : &getActualFormat();
    const vk::Format &vkFormat      = contextVk->getRenderer()->getFormat(readFormat->id);
    const gl::InternalFormat &storageFormatInfo =
        vkFormat.getInternalFormatInfo(readFormat->componentType);

    if (copyAspectFlags != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        readFormat = &GetDepthStencilImageToBufferFormat(*readFormat, copyAspectFlags);
    }

    VkOffset3D srcOffset = {area.x, area.y, 0};

    VkImageSubresourceLayers srcSubresource = {};
    srcSubresource.aspectMask               = copyAspectFlags;
    srcSubresource.mipLevel                 = toVkLevel(levelGL).get();
    srcSubresource.baseArrayLayer           = layer.get();
    srcSubresource.layerCount               = 1;

    VkExtent3D srcExtent = {static_cast<uint32_t>(area.width), static_cast<uint32_t>(area.height),
                            1};

    if (mImageType == VK_IMAGE_TYPE_3D)
    {
        // For 3D texture we need special handling
        srcOffset.z                   = layer.get();
        srcSubresource.baseArrayLayer = 0;
    }

    if (isExternalFormat)
    {
        // Make sure the render pass is closed, per UtilsVk::copyImage's requirements.
        ANGLE_TRY(
            contextVk->flushCommandsAndEndRenderPass(RenderPassClosureReason::PrepareForImageCopy));

        CommandResources resources;
        OutsideRenderPassCommandBuffer *commandBuffer;
        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

        // Create some temp views because copyImage works in terms of them
        gl::TextureType textureType = Get2DTextureType(1, resolvedImage.get().getSamples());

        ScopedOverrideYCbCrFilter scopedOverrideYCbCrFilter(renderer, src, VK_FILTER_NEAREST);

        // Surely we have a view of this already!
        vk::ImageView srcView;
        ANGLE_TRY(src->initLayerImageView(contextVk, textureType, VK_IMAGE_ASPECT_COLOR_BIT,
                                          gl::SwizzleState(), &srcView, LevelIndex(0), 1,
                                          LayerIndex(0), mLayerCount));
        vk::ImageView stagingView;
        ANGLE_TRY(resolvedImage.get().initLayerImageView(
            contextVk, textureType, VK_IMAGE_ASPECT_COLOR_BIT, gl::SwizzleState(), &stagingView,
            LevelIndex(0), 1, LayerIndex(0), mLayerCount));

        UtilsVk::CopyImageParameters params = {};
        params.srcLayer                     = vk::LayerIndex(0);
        params.dstMip                       = gl::OwnerLevel(0);
        params.dstLayer                     = gl::OwnerLayer(0);
        params.srcOffset[0]                 = srcOffset.x;
        params.srcOffset[1]                 = srcOffset.y;
        params.srcExtents[0]                = srcExtent.width;
        params.srcExtents[1]                = srcExtent.height;
        params.srcHeight                    = srcExtent.height;
        ANGLE_TRY(contextVk->getUtils().copyImage(contextVk, &resolvedImage.get(), &stagingView,
                                                  src, &srcView, params));

        CommandResources readAccess;
        readAccess.onImageTransferRead(layoutChangeAspectFlags, &resolvedImage.get());
        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(readAccess, &commandBuffer));

        // Make the resolved image the target of buffer copy
        src                           = &resolvedImage.get();
        srcOffset                     = {0, 0, 0};
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount     = 1;
        srcSubresource.mipLevel       = 0;

        // Mark our temp views as garbage immediately
        contextVk->addGarbage(&srcView);
        contextVk->addGarbage(&stagingView);
    }

    if (isMultisampled)
    {
        CommandResources resources;
        resources.onImageTransferRead(layoutChangeAspectFlags, this);
        resources.onImageTransferWrite(gl::OwnerLevel(0), 1, gl::OwnerLayer(0), 1,
                                       layoutChangeAspectFlags, &resolvedImage.get());

        OutsideRenderPassCommandBuffer *commandBuffer;
        ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(resources, &commandBuffer));

        // Note: resolve only works on color images (not depth/stencil).
        ASSERT(copyAspectFlags == VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageResolve resolveRegion                = {};
        resolveRegion.srcSubresource                = srcSubresource;
        resolveRegion.srcOffset                     = srcOffset;
        resolveRegion.dstSubresource.aspectMask     = copyAspectFlags;
        resolveRegion.dstSubresource.mipLevel       = 0;
        resolveRegion.dstSubresource.baseArrayLayer = 0;
        resolveRegion.dstSubresource.layerCount     = 1;
        resolveRegion.dstOffset                     = {};
        resolveRegion.extent                        = srcExtent;

        resolve(renderer, &resolvedImage.get(), resolveRegion, commandBuffer);

        // Make the resolved image the target of buffer copy.
        src                           = &resolvedImage.get();
        srcOffset                     = {0, 0, 0};
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount     = 1;
        srcSubresource.mipLevel       = 0;
    }

    if (!src->canTransferFrom())
    {
        ASSERT(src->useTileMemory());
        if (src->useTileMemory())
        {
            ANGLE_TRY(src->fallbackFromTileMemory(contextVk));
            ASSERT(src->canTransferFrom());
        }
    }

    // If PBO and if possible, copy directly on the GPU.
    if (packPixelsParams.packBuffer)
    {
        ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::readPixelsImpl - PBO");

        const ptrdiff_t pixelsOffset = reinterpret_cast<ptrdiff_t>(pixels);
        if (canCopyWithTransformForReadPixels(packPixelsParams, srcExtent, readFormat,
                                              pixelsOffset))
        {
            BufferHelper &packBuffer      = GetImpl(packPixelsParams.packBuffer)->getBuffer();
            VkDeviceSize packBufferOffset = packBuffer.getOffset();

            CommandResources copyAccess;
            copyAccess.onBufferTransferWrite(&packBuffer);
            copyAccess.onImageTransferRead(layoutChangeAspectFlags, src);

            OutsideRenderPassCommandBuffer *copyCommandBuffer;
            ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(copyAccess, &copyCommandBuffer));

            ASSERT(packPixelsParams.outputPitch % readFormat->pixelBytes == 0);

            VkBufferImageCopy region = {};
            region.bufferImageHeight = srcExtent.height;
            region.bufferOffset      = packBufferOffset + packPixelsParams.offset + pixelsOffset;
            region.bufferRowLength   = packPixelsParams.outputPitch / readFormat->pixelBytes;
            region.imageExtent       = srcExtent;
            region.imageOffset       = srcOffset;
            region.imageSubresource  = srcSubresource;

            copyCommandBuffer->copyImageToBuffer(src->getImage(), src->getCurrentLayout(renderer),
                                                 packBuffer.getBuffer().getHandle(), 1, &region);
            return angle::Result::Continue;
        }
        if (canCopyWithComputeForReadPixels(packPixelsParams, srcExtent, readFormat, pixelsOffset))
        {
            ANGLE_TRY(readPixelsWithCompute(contextVk, src, packPixelsParams, srcOffset, srcExtent,
                                            pixelsOffset, srcSubresource));
            return angle::Result::Continue;
        }
    }

    ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::readPixelsImpl - CPU Readback");

    RendererScoped<vk::BufferHelper> readBuffer(renderer);
    vk::BufferHelper *stagingBuffer = &readBuffer.get();

    uint8_t *readPixelBuffer   = nullptr;
    VkDeviceSize stagingOffset = 0;
    size_t allocationSize =
        static_cast<size_t>(area.width) * static_cast<size_t>(area.height) * readFormat->pixelBytes;

    ANGLE_TRY(contextVk->initBufferForImageCopy(stagingBuffer, allocationSize,
                                                MemoryCoherency::CachedPreferCoherent,
                                                readFormat->id, &stagingOffset, &readPixelBuffer));
    ANGLE_TRY(stagingBuffer->flush(renderer));
    VkBuffer bufferHandle = stagingBuffer->getBuffer().getHandle();

    VkBufferImageCopy region = {};
    region.bufferImageHeight = srcExtent.height;
    region.bufferOffset      = stagingOffset;
    region.bufferRowLength   = srcExtent.width;
    region.imageExtent       = srcExtent;
    region.imageOffset       = srcOffset;
    region.imageSubresource  = srcSubresource;

    // For compressed textures, vkCmdCopyImageToBuffer requires
    // a region that is a multiple of the block size.
    if (readFormat->isBlock)
    {
        region.bufferRowLength =
            roundUp(region.bufferRowLength, storageFormatInfo.compressedBlockWidth);
        region.bufferImageHeight =
            roundUp(region.bufferImageHeight, storageFormatInfo.compressedBlockHeight);
    }

    CommandResources readbackAccess;
    readbackAccess.onBufferTransferWrite(stagingBuffer);
    readbackAccess.onImageTransferRead(layoutChangeAspectFlags, src);

    OutsideRenderPassCommandBuffer *readbackCommandBuffer;
    ANGLE_TRY(contextVk->getOutsideRenderPassCommandBuffer(readbackAccess, &readbackCommandBuffer));

    readbackCommandBuffer->copyImageToBuffer(src->getImage(), src->getCurrentLayout(renderer),
                                             bufferHandle, 1, &region);

    ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_HIGH, "GPU stall due to ReadPixels");

    // Triggers a full finish.
    ANGLE_TRY(contextVk->finishImpl(QueueSubmitReason::GLReadPixels));
    // invalidate must be called after wait for finish.
    ANGLE_TRY(stagingBuffer->invalidate(renderer));

    return packReadPixelBuffer(contextVk, area, packPixelsParams, getActualFormat(), *readFormat,
                               readPixelBuffer, levelGL, pixels);
}

angle::Result ImageHelper::packReadPixelBuffer(ContextVk *contextVk,
                                               const gl::Rectangle &area,
                                               const PackPixelsParams &packPixelsParams,
                                               const angle::Format &readFormat,
                                               const angle::Format &aspectFormat,
                                               const uint8_t *readPixelBuffer,
                                               gl::OwnerLevel levelGL,
                                               void *pixels)
{
    const vk::Format &vkFormat = contextVk->getRenderer()->getFormat(readFormat.id);
    const gl::InternalFormat &storageFormatInfo =
        vkFormat.getInternalFormatInfo(readFormat.componentType);

    if (readFormat.isBlock)
    {
        ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::packReadPixelBuffer - Block");

        ASSERT(readFormat == aspectFormat);

        const LevelIndex levelVk = toVkLevel(levelGL);
        gl::Extents levelExtents = getLevelExtents(levelVk);

        // Calculate size of one layer
        levelExtents.depth = 1;
        GLuint layerSize;
        ANGLE_VK_CHECK_MATH(contextVk,
                            storageFormatInfo.computeCompressedImageSize(levelExtents, &layerSize));
        ANGLE_UNSAFE_TODO(memcpy(pixels, readPixelBuffer, layerSize));
    }
    else if (packPixelsParams.packBuffer)
    {
        ANGLE_TRACE_EVENT0("gpu.angle", "ImageHelper::packReadPixelBuffer - PBO");

        // Must map the PBO in order to read its contents (and then unmap it later)
        BufferVk *packBufferVk = GetImpl(packPixelsParams.packBuffer);
        void *mapPtr           = nullptr;
        BufferFeedback feedback;
        ANGLE_TRY(packBufferVk->mapImpl(contextVk, GL_MAP_WRITE_BIT, &mapPtr, &feedback));
        ASSERT(!feedback.hasFeedback());
        uint8_t *dst =
            ANGLE_UNSAFE_TODO(static_cast<uint8_t *>(mapPtr) + reinterpret_cast<ptrdiff_t>(pixels));
        PackPixels(packPixelsParams, aspectFormat, area.width * aspectFormat.pixelBytes,
                   readPixelBuffer, dst);
        ANGLE_TRY(packBufferVk->unmapImpl(contextVk, &feedback));
        ASSERT(!feedback.hasFeedback());
    }
    else
    {
        PackPixels(packPixelsParams, aspectFormat, area.width * aspectFormat.pixelBytes,
                   readPixelBuffer, static_cast<uint8_t *>(pixels));
    }

    return angle::Result::Continue;
}

// ImageHelper::SubresourceUpdate implementation
ImageHelper::SubresourceUpdate::SubresourceUpdate() : updateSource(UpdateSource::Buffer)
{
    data.buffer.bufferHelper = nullptr;
    refCounted.buffer        = nullptr;
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(const VkImageAspectFlags aspectFlags,
                                                  const VkClearValue &clearValue,
                                                  const gl::OwnerLevel levelIndex,
                                                  const gl::OwnerLayer layerIndex,
                                                  const uint32_t layerCount,
                                                  const gl::Rectangle &clearArea)
    : updateSource(UpdateSource::ClearPartial)
{
    refCounted.image              = nullptr;
    data.clearPartial.aspectFlags = aspectFlags;
    data.clearPartial.levelIndex  = levelIndex.get();
    data.clearPartial.layerIndex  = layerIndex.get();
    data.clearPartial.layerCount  = layerCount;
    data.clearPartial.offset      = {clearArea.x, clearArea.y};
    data.clearPartial.extent      = {static_cast<uint32_t>(clearArea.width),
                                     static_cast<uint32_t>(clearArea.height)};
    data.clearPartial.clearValue  = clearValue;
}

ImageHelper::SubresourceUpdate::~SubresourceUpdate() {}

ImageHelper::SubresourceUpdate::SubresourceUpdate(RefCounted<BufferHelper> *bufferIn,
                                                  BufferHelper *bufferHelperIn,
                                                  const VkBufferImageCopy &copyRegionIn,
                                                  angle::FormatID formatID)
    : updateSource(UpdateSource::Buffer)
{
    refCounted.buffer = bufferIn;
    if (refCounted.buffer != nullptr)
    {
        refCounted.buffer->addRef();
    }
    data.buffer.bufferHelper = bufferHelperIn;
    data.buffer.copyRegion   = copyRegionIn;
    data.buffer.formatID     = formatID;
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(RefCounted<ImageHelper> *imageIn,
                                                  const VkImageCopy &copyRegionIn,
                                                  angle::FormatID formatID)
    : updateSource(UpdateSource::Image)
{
    refCounted.image = imageIn;
    refCounted.image->addRef();
    data.image.copyRegion = copyRegionIn;
    data.image.formatID   = formatID;
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(VkImageAspectFlags aspectFlags,
                                                  const VkClearValue &clearValue,
                                                  const gl::OwnerImageIndex &imageIndex)
    : SubresourceUpdate(
          aspectFlags,
          clearValue,
          imageIndex.getLevelIndex(),
          imageIndex.hasLayer() ? imageIndex.getLayerIndex() : gl::OwnerLayer(0),
          imageIndex.hasLayer() ? imageIndex.getLayerCount() : VK_REMAINING_ARRAY_LAYERS)
{}

ImageHelper::SubresourceUpdate::SubresourceUpdate(VkImageAspectFlags aspectFlags,
                                                  const VkClearValue &clearValue,
                                                  gl::OwnerLevel level,
                                                  gl::OwnerLayer layerIndex,
                                                  uint32_t layerCount)
    : updateSource(UpdateSource::Clear)
{
    refCounted.image          = nullptr;
    data.clear.aspectFlags    = aspectFlags;
    data.clear.value          = clearValue;
    data.clear.levelIndex     = level.get();
    data.clear.layerIndex     = layerIndex.get();
    data.clear.layerCount     = layerCount;
    data.clear.colorMaskFlags = 0;
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(VkColorComponentFlags colorMaskFlags,
                                                  const VkClearColorValue &clearValue,
                                                  const gl::OwnerImageIndex &imageIndex)
    : updateSource(UpdateSource::ClearEmulatedChannelsOnly)
{
    refCounted.image       = nullptr;
    data.clear.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    data.clear.value.color = clearValue;
    data.clear.levelIndex  = imageIndex.getLevelIndex().get();
    data.clear.layerIndex  = imageIndex.hasLayer() ? imageIndex.getLayerIndex().get() : 0;
    data.clear.layerCount =
        imageIndex.hasLayer() ? imageIndex.getLayerCount() : VK_REMAINING_ARRAY_LAYERS;
    data.clear.colorMaskFlags = colorMaskFlags;
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(const SubresourceUpdate &other)
    : updateSource(other.updateSource)
{
    switch (updateSource)
    {
        case UpdateSource::Clear:
        case UpdateSource::ClearEmulatedChannelsOnly:
        case UpdateSource::ClearAfterInvalidate:
            data.clear        = other.data.clear;
            refCounted.buffer = nullptr;
            break;
        case UpdateSource::ClearPartial:
            data.clearPartial = other.data.clearPartial;
            break;
        case UpdateSource::Buffer:
            data.buffer       = other.data.buffer;
            refCounted.buffer = other.refCounted.buffer;
            break;
        case UpdateSource::Image:
            data.image       = other.data.image;
            refCounted.image = other.refCounted.image;
            break;
        default:
            UNREACHABLE();
    }
}

ImageHelper::SubresourceUpdate::SubresourceUpdate(SubresourceUpdate &&other)
    : updateSource(other.updateSource)
{
    switch (updateSource)
    {
        case UpdateSource::Clear:
        case UpdateSource::ClearEmulatedChannelsOnly:
        case UpdateSource::ClearAfterInvalidate:
            data.clear        = other.data.clear;
            refCounted.buffer = nullptr;
            break;
        case UpdateSource::ClearPartial:
            data.clearPartial = other.data.clearPartial;
            break;
        case UpdateSource::Buffer:
            data.buffer             = other.data.buffer;
            refCounted.buffer       = other.refCounted.buffer;
            other.refCounted.buffer = nullptr;
            break;
        case UpdateSource::Image:
            data.image             = other.data.image;
            refCounted.image       = other.refCounted.image;
            other.refCounted.image = nullptr;
            break;
        default:
            UNREACHABLE();
    }
}

ImageHelper::SubresourceUpdate &ImageHelper::SubresourceUpdate::operator=(SubresourceUpdate &&other)
{
    // Given that the update is a union of three structs, we can't use std::swap on the fields.  For
    // example, |this| may be an Image update and |other| may be a Buffer update.
    // The following could work:
    //
    // SubresourceUpdate oldThis;
    // Set oldThis to this->field based on updateSource
    // Set this->otherField to other.otherField based on other.updateSource
    // Set other.field to oldThis->field based on updateSource
    // std::Swap(updateSource, other.updateSource);
    //
    // It's much simpler to just swap the memory instead.

    SubresourceUpdate oldThis;
    ANGLE_UNSAFE_TODO({
        memcpy(&oldThis, this, sizeof(*this));
        memcpy(this, &other, sizeof(*this));
        memcpy(&other, &oldThis, sizeof(*this));
    })

    return *this;
}

void ImageHelper::SubresourceUpdate::release(Renderer *renderer)
{
    if (updateSource == UpdateSource::Image)
    {
        if (refCounted.image->getAndReleaseRef() == 1)
        {
            // Staging images won't be used in render pass attachments.
            refCounted.image->get().releaseImage(renderer);
            refCounted.image->get().releaseStagedUpdates(renderer);
            SafeDelete(refCounted.image);
        }

        refCounted.image = nullptr;
    }
    else if (updateSource == UpdateSource::Buffer && refCounted.buffer != nullptr)
    {
        if (refCounted.buffer->getAndReleaseRef() == 1)
        {
            refCounted.buffer->get().release(renderer);
            SafeDelete(refCounted.buffer);
        }

        refCounted.buffer = nullptr;
    }
}

bool ImageHelper::matchesLayerRange(const SubresourceUpdate &update,
                                    const gl::OwnerLevel levelIndex,
                                    gl::OwnerLayer layerIndex,
                                    uint32_t layerCount) const
{
    ASSERT(layerCount != VK_REMAINING_ARRAY_LAYERS);
    gl::OwnerLayer updateBaseLayer;
    uint32_t updateLayerCount;
    getDestSubresource(update, levelIndex, &updateBaseLayer, &updateLayerCount);

    return updateBaseLayer == layerIndex && updateLayerCount == layerCount;
}

bool ImageHelper::intersectsLayerRange(const SubresourceUpdate &update,
                                       const gl::OwnerLevel levelIndex,
                                       gl::OwnerLayer layerIndex,
                                       uint32_t layerCount) const
{
    gl::OwnerLayer updateBaseLayer;
    uint32_t updateLayerCount;
    getDestSubresource(update, levelIndex, &updateBaseLayer, &updateLayerCount);
    const gl::OwnerLayer updateLayerEnd = updateBaseLayer + updateLayerCount;

    return updateBaseLayer < (layerIndex + layerCount) && updateLayerEnd > layerIndex;
}

void ImageHelper::getDestSubresource(const SubresourceUpdate &update,
                                     const gl::OwnerLevel levelIndex,
                                     gl::OwnerLayer *baseLayerOut,
                                     uint32_t *layerCountOut) const
{
    const bool is3D = mImageType == VK_IMAGE_TYPE_3D;
    if (IsClear(update.updateSource))
    {
        *baseLayerOut  = gl::OwnerLayer(update.data.clear.layerIndex);
        *layerCountOut = update.data.clear.layerCount;

        if (*layerCountOut == static_cast<uint32_t>(gl::ImageIndex::kEntireLevel))
        {
            *layerCountOut = is3D ? getLevelExtents(toVkLevel(levelIndex)).depth : mLayerCount;
        }
    }
    else if (update.updateSource == UpdateSource::ClearPartial)
    {
        *baseLayerOut  = gl::OwnerLayer(update.data.clearPartial.layerIndex);
        *layerCountOut = update.data.clearPartial.layerCount;

        if (*layerCountOut == static_cast<uint32_t>(gl::ImageIndex::kEntireLevel))
        {
            *layerCountOut = is3D ? getLevelExtents(toVkLevel(levelIndex)).depth : mLayerCount;
        }
    }
    else if (update.updateSource == UpdateSource::Buffer)
    {
        const VkImageSubresourceLayers &dstSubresource =
            update.data.buffer.copyRegion.imageSubresource;
        const VkOffset3D &dstOffset = update.data.buffer.copyRegion.imageOffset;
        const VkExtent3D &dstExtent = update.data.buffer.copyRegion.imageExtent;
        *baseLayerOut  = gl::OwnerLayer(is3D ? dstOffset.z : dstSubresource.baseArrayLayer);
        *layerCountOut = is3D ? dstExtent.depth : dstSubresource.layerCount;

        ASSERT(*layerCountOut != static_cast<uint32_t>(gl::ImageIndex::kEntireLevel));
    }
    else
    {
        const VkImageSubresourceLayers &dstSubresource =
            update.data.image.copyRegion.dstSubresource;
        const VkOffset3D &dstOffset = update.data.image.copyRegion.dstOffset;
        const VkExtent3D &dstExtent = update.data.image.copyRegion.extent;
        *baseLayerOut  = gl::OwnerLayer(is3D ? dstOffset.z : dstSubresource.baseArrayLayer);
        *layerCountOut = is3D ? dstExtent.depth : dstSubresource.layerCount;

        ASSERT(*layerCountOut != static_cast<uint32_t>(gl::ImageIndex::kEntireLevel));
    }
}

VkImageAspectFlags ImageHelper::SubresourceUpdate::getDestAspectFlags() const
{
    if (IsClear(updateSource))
    {
        return data.clear.aspectFlags;
    }
    else if (updateSource == UpdateSource::ClearPartial)
    {
        return data.clearPartial.aspectFlags;
    }
    else if (updateSource == UpdateSource::Buffer)
    {
        return data.buffer.copyRegion.imageSubresource.aspectMask;
    }
    else
    {
        ASSERT(updateSource == UpdateSource::Image);
        return data.image.copyRegion.dstSubresource.aspectMask;
    }
}

size_t ImageHelper::getLevelUpdateCount(gl::OwnerLevel level) const
{
    return static_cast<size_t>(level.get()) < mSubresourceUpdates.size()
               ? mSubresourceUpdates[level.get()].size()
               : 0;
}

void ImageHelper::clipLevelToUpdateListUpperLimit(gl::OwnerLevel *level) const
{
    const gl::OwnerLevel levelLimit(static_cast<uint32_t>(mSubresourceUpdates.size()));
    *level = std::min(*level, levelLimit);
}

ImageHelper::SubresourceUpdates *ImageHelper::getLevelUpdates(gl::OwnerLevel level)
{
    return static_cast<size_t>(level.get()) < mSubresourceUpdates.size()
               ? &mSubresourceUpdates[level.get()]
               : nullptr;
}

const ImageHelper::SubresourceUpdates *ImageHelper::getLevelUpdates(gl::OwnerLevel level) const
{
    return static_cast<size_t>(level.get()) < mSubresourceUpdates.size()
               ? &mSubresourceUpdates[level.get()]
               : nullptr;
}

void ImageHelper::appendSubresourceUpdate(gl::OwnerLevel level, SubresourceUpdate &&update)
{
    if (mSubresourceUpdates.size() <= static_cast<size_t>(level.get()))
    {
        mSubresourceUpdates.resize(level.get() + 1);
    }
    // Update total staging buffer size
    mTotalStagedBufferUpdateSize += update.updateSource == UpdateSource::Buffer
                                        ? update.data.buffer.bufferHelper->getSize()
                                        : 0;
    mSubresourceUpdates[level.get()].emplace_back(std::move(update));
    onStateChange(angle::SubjectMessage::SubjectChanged);
}

void ImageHelper::prependSubresourceUpdate(gl::OwnerLevel level, SubresourceUpdate &&update)
{
    if (mSubresourceUpdates.size() <= static_cast<size_t>(level.get()))
    {
        mSubresourceUpdates.resize(level.get() + 1);
    }

    // Update total staging buffer size
    mTotalStagedBufferUpdateSize += update.updateSource == UpdateSource::Buffer
                                        ? update.data.buffer.bufferHelper->getSize()
                                        : 0;
    mSubresourceUpdates[level.get()].emplace_front(std::move(update));
    onStateChange(angle::SubjectMessage::SubjectChanged);
}

bool ImageHelper::hasEmulatedImageChannels() const
{
    const angle::Format &angleFmt   = getIntendedFormat();
    const angle::Format &textureFmt = getActualFormat();

    // Block formats may be decoded and emulated with a non-block format.
    if (angleFmt.isBlock)
    {
        return !textureFmt.isBlock;
    }

    // The red channel is never emulated.
    ASSERT((angleFmt.redBits != 0 || angleFmt.luminanceBits != 0 || angleFmt.alphaBits != 0) ==
           (textureFmt.redBits != 0));

    return (angleFmt.alphaBits == 0 && textureFmt.alphaBits > 0) ||
           (angleFmt.blueBits == 0 && textureFmt.blueBits > 0) ||
           (angleFmt.greenBits == 0 && textureFmt.greenBits > 0) ||
           (angleFmt.depthBits == 0 && textureFmt.depthBits > 0) ||
           (angleFmt.stencilBits == 0 && textureFmt.stencilBits > 0);
}

bool ImageHelper::hasEmulatedDepthChannel() const
{
    return getIntendedFormat().depthBits == 0 && getActualFormat().depthBits > 0;
}

bool ImageHelper::hasEmulatedStencilChannel() const
{
    return getIntendedFormat().stencilBits == 0 && getActualFormat().stencilBits > 0;
}

bool ImageHelper::hasInefficientlyEmulatedImageFormat() const
{
    if (hasEmulatedImageFormat())
    {
        // ETC2 compression is compatible with ETC1
        return !(mIntendedFormatID == angle::FormatID::ETC1_R8G8B8_UNORM_BLOCK &&
                 mActualFormatID == angle::FormatID::ETC2_R8G8B8_UNORM_BLOCK);
    }
    return false;
}

VkColorComponentFlags ImageHelper::getEmulatedChannelsMask() const
{
    const angle::Format &angleFmt   = getIntendedFormat();
    const angle::Format &textureFmt = getActualFormat();

    ASSERT(!angleFmt.hasDepthOrStencilBits());

    VkColorComponentFlags emulatedChannelsMask = 0;

    if (angleFmt.alphaBits == 0 && textureFmt.alphaBits > 0)
    {
        emulatedChannelsMask |= VK_COLOR_COMPONENT_A_BIT;
    }
    if (angleFmt.blueBits == 0 && textureFmt.blueBits > 0)
    {
        emulatedChannelsMask |= VK_COLOR_COMPONENT_B_BIT;
    }
    if (angleFmt.greenBits == 0 && textureFmt.greenBits > 0)
    {
        emulatedChannelsMask |= VK_COLOR_COMPONENT_G_BIT;
    }

    // The red channel is never emulated.
    ASSERT((angleFmt.redBits != 0 || angleFmt.luminanceBits != 0 || angleFmt.alphaBits != 0) ==
           (textureFmt.redBits != 0));

    return emulatedChannelsMask;
}

LayerMode GetLayerMode(const vk::ImageHelper &image, uint32_t layerCount)
{
    const uint32_t imageLayerCount = GetImageLayerCountForView(image);
    const bool allLayers           = layerCount == imageLayerCount;

    ASSERT(allLayers || (layerCount > 0 && layerCount <= gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS));
    return allLayers ? LayerMode::All : static_cast<LayerMode>(layerCount);
}

ComputePipelineOptions GetComputePipelineOptions(vk::PipelineRobustness robustness,
                                                 vk::PipelineProtectedAccess protectedAccess)
{
    vk::ComputePipelineOptions pipelineOptions = {};

    if (robustness == vk::PipelineRobustness::Robust)
    {
        pipelineOptions.robustness = 1;
    }
    if (protectedAccess == vk::PipelineProtectedAccess::Protected)
    {
        pipelineOptions.protectedAccess = 1;
    }

    return pipelineOptions;
}

// ImageViewHelper implementation.
ImageViewHelper::ImageViewHelper()
    : mCurrentBaseMaxLevelHash(0),
      mReadColorspace(ImageViewColorspace::Invalid),
      mWriteColorspace(ImageViewColorspace::Invalid)
{}

ImageViewHelper::ImageViewHelper(ImageViewHelper &&other)
{
    std::swap(mCurrentBaseMaxLevelHash, other.mCurrentBaseMaxLevelHash);
    std::swap(mReadColorspace, other.mReadColorspace);
    std::swap(mWriteColorspace, other.mWriteColorspace);
    std::swap(mColorspaceState, other.mColorspaceState);

    std::swap(mPerLevelRangeLinearReadImageViews, other.mPerLevelRangeLinearReadImageViews);
    std::swap(mPerLevelRangeSRGBReadImageViews, other.mPerLevelRangeSRGBReadImageViews);
    std::swap(mPerLevelRangeStencilReadImageViews, other.mPerLevelRangeStencilReadImageViews);
    std::swap(mPerLevelRangeSamplerExternal2DY2YEXTImageViews,
              other.mPerLevelRangeSamplerExternal2DY2YEXTImageViews);

    std::swap(mLinearCopyImageView, other.mLinearCopyImageView);
    std::swap(mSRGBCopyImageView, other.mSRGBCopyImageView);

    std::swap(mLayerLevelDrawImageViews, other.mLayerLevelDrawImageViews);
    std::swap(mLayerLevelDrawImageViewsLinear, other.mLayerLevelDrawImageViewsLinear);
    std::swap(mSubresourceDrawImageViews, other.mSubresourceDrawImageViews);

    std::swap(mLayerLevelDepthOnlyImageViews, other.mLayerLevelDepthOnlyImageViews);
    std::swap(mLayerLevelStencilOnlyImageViews, other.mLayerLevelStencilOnlyImageViews);
    std::swap(mSubresourceDepthOnlyImageViews, other.mSubresourceDepthOnlyImageViews);
    std::swap(mSubresourceStencilOnlyImageViews, other.mSubresourceStencilOnlyImageViews);

    std::swap(mLevelStorageImageViews, other.mLevelStorageImageViews);
    std::swap(mLayerLevelStorageImageViews, other.mLayerLevelStorageImageViews);
    std::swap(mFragmentShadingRateImageView, other.mFragmentShadingRateImageView);
    std::swap(mImageViewSerial, other.mImageViewSerial);
}

ImageViewHelper::~ImageViewHelper() {}

void ImageViewHelper::init(Renderer *renderer)
{
    if (!mImageViewSerial.valid())
    {
        mImageViewSerial = renderer->getResourceSerialFactory().generateImageOrBufferViewSerial();
    }
}

void ImageViewHelper::release(Renderer *renderer, const ResourceUse &use)
{
    mCurrentBaseMaxLevelHash = 0;
    mReadColorspace          = ImageViewColorspace::Invalid;
    mWriteColorspace         = ImageViewColorspace::Invalid;
    // Clear shared flag
    mColorspaceState.reset();

    GarbageObjects garbage;
    // Reserve reasonable amount of storage
    garbage.reserve(4);

    // Release the read views
    ReleaseImageViews(&mPerLevelRangeLinearReadImageViews, &garbage);
    ReleaseImageViews(&mPerLevelRangeSRGBReadImageViews, &garbage);
    ReleaseImageViews(&mPerLevelRangeStencilReadImageViews, &garbage);
    ReleaseImageViews(&mPerLevelRangeSamplerExternal2DY2YEXTImageViews, &garbage);

    // Release the copy views
    if (mLinearCopyImageView.valid())
    {
        garbage.emplace_back(GetGarbage(&mLinearCopyImageView));
    }
    if (mSRGBCopyImageView.valid())
    {
        garbage.emplace_back(GetGarbage(&mSRGBCopyImageView));
    }

    // Release the draw views
    ReleaseLayerLevelImageViews(&mLayerLevelDrawImageViews, &garbage);
    ReleaseLayerLevelImageViews(&mLayerLevelDrawImageViewsLinear, &garbage);
    ReleaseSubresourceImageViews(&mSubresourceDrawImageViews, &garbage);

    // Release the depth-xor-stencil input views
    ReleaseLayerLevelImageViews(&mLayerLevelDepthOnlyImageViews, &garbage);
    ReleaseLayerLevelImageViews(&mLayerLevelStencilOnlyImageViews, &garbage);
    ReleaseSubresourceImageViews(&mSubresourceDepthOnlyImageViews, &garbage);
    ReleaseSubresourceImageViews(&mSubresourceStencilOnlyImageViews, &garbage);

    // Release the storage views
    for (auto &iter : mLevelStorageImageViews)
    {
        std::unique_ptr<ImageViewVector> &imageViewVector = iter.second;
        ReleaseImageViews(imageViewVector.get(), &garbage);
        imageViewVector.reset();
    }
    mLevelStorageImageViews.clear();
    for (auto &iter : mLayerLevelStorageImageViews)
    {
        std::unique_ptr<LayerLevelImageViewVector> &layerLevelImageViewVector = iter.second;
        ReleaseLayerLevelImageViews(layerLevelImageViewVector.get(), &garbage);
        layerLevelImageViewVector.reset();
    }
    mLayerLevelStorageImageViews.clear();

    // Release fragment shading rate view
    if (mFragmentShadingRateImageView.valid())
    {
        garbage.emplace_back(GetGarbage(&mFragmentShadingRateImageView));
    }

    if (!garbage.empty())
    {
        renderer->collectGarbage(use, std::move(garbage));
    }

    // Update image view serial.
    mImageViewSerial = renderer->getResourceSerialFactory().generateImageOrBufferViewSerial();
}

bool ImageViewHelper::isImageViewGarbageEmpty() const
{
    return mPerLevelRangeLinearReadImageViews.empty() && !mLinearCopyImageView.valid() &&
           mPerLevelRangeSRGBReadImageViews.empty() && !mSRGBCopyImageView.valid() &&
           mPerLevelRangeStencilReadImageViews.empty() &&
           mPerLevelRangeSamplerExternal2DY2YEXTImageViews.empty() &&
           mLayerLevelDrawImageViews.empty() && mLayerLevelDrawImageViewsLinear.empty() &&
           mSubresourceDrawImageViews.empty() && mLayerLevelDepthOnlyImageViews.empty() &&
           mLayerLevelStencilOnlyImageViews.empty() && mSubresourceDepthOnlyImageViews.empty() &&
           mSubresourceStencilOnlyImageViews.empty() && mLayerLevelStorageImageViews.empty() &&
           !mFragmentShadingRateImageView.valid();
}

void ImageViewHelper::destroy(VkDevice device)
{
    mCurrentBaseMaxLevelHash = 0;
    mReadColorspace          = ImageViewColorspace::Invalid;
    mWriteColorspace         = ImageViewColorspace::Invalid;
    mColorspaceState.reset();

    // Release the read views
    DestroyImageViews(&mPerLevelRangeLinearReadImageViews, device);
    DestroyImageViews(&mPerLevelRangeSRGBReadImageViews, device);
    DestroyImageViews(&mPerLevelRangeStencilReadImageViews, device);
    DestroyImageViews(&mPerLevelRangeSamplerExternal2DY2YEXTImageViews, device);

    // Release the copy views
    mLinearCopyImageView.destroy(device);
    mSRGBCopyImageView.destroy(device);

    // Release the draw views
    DestroyLayerLevelImageViews(&mLayerLevelDrawImageViews, device);
    DestroyLayerLevelImageViews(&mLayerLevelDrawImageViewsLinear, device);
    DestroySubresourceImageViews(&mSubresourceDrawImageViews, device);

    // Release the depth-xor-stencil input views
    DestroyLayerLevelImageViews(&mLayerLevelDepthOnlyImageViews, device);
    DestroyLayerLevelImageViews(&mLayerLevelStencilOnlyImageViews, device);
    DestroySubresourceImageViews(&mSubresourceDepthOnlyImageViews, device);
    DestroySubresourceImageViews(&mSubresourceStencilOnlyImageViews, device);

    // Release the storage views
    for (auto &iter : mLevelStorageImageViews)
    {
        std::unique_ptr<ImageViewVector> &imageViewVector = iter.second;
        DestroyImageViews(imageViewVector.get(), device);
        imageViewVector.reset();
    }
    mLevelStorageImageViews.clear();
    for (auto &iter : mLayerLevelStorageImageViews)
    {
        std::unique_ptr<LayerLevelImageViewVector> &layerLevelImageViewVector = iter.second;
        DestroyLayerLevelImageViews(layerLevelImageViewVector.get(), device);
        layerLevelImageViewVector.reset();
    }
    mLayerLevelStorageImageViews.clear();

    // Destroy fragment shading rate view
    mFragmentShadingRateImageView.destroy(device);

    mImageViewSerial = kInvalidImageOrBufferViewSerial;
}

angle::Result ImageViewHelper::initReadViews(ContextVk *contextVk,
                                             gl::TextureType viewType,
                                             const ImageHelper &image,
                                             const gl::SwizzleState &formatSwizzle,
                                             const gl::SwizzleState &readSwizzle,
                                             LevelIndex baseLevel,
                                             uint32_t levelCount,
                                             LayerIndex baseLayer,
                                             uint32_t layerCount,
                                             bool requiresSRGBViews,
                                             VkImageUsageFlags imageUsageFlags,
                                             GLenum astcDecodePrecision)
{
    ASSERT(levelCount > 0);

    const uint32_t maxLevel = levelCount - 1;
    ASSERT(maxLevel < 16);
    ASSERT(baseLevel.get() < 16);
    mCurrentBaseMaxLevelHash = static_cast<uint8_t>(baseLevel.get() << 4 | maxLevel);
    updateColorspace(image.getActualFormat());

    if (mCurrentBaseMaxLevelHash >= mPerLevelRangeLinearReadImageViews.size())
    {
        const uint32_t maxViewCount = mCurrentBaseMaxLevelHash + 1;

        mPerLevelRangeLinearReadImageViews.resize(maxViewCount);
        mPerLevelRangeSRGBReadImageViews.resize(maxViewCount);
        mPerLevelRangeStencilReadImageViews.resize(maxViewCount);
        mPerLevelRangeSamplerExternal2DY2YEXTImageViews.resize(maxViewCount);
    }

    // Determine if we already have ImageViews for the new max level
    if (getReadImageView().valid())
    {
        return angle::Result::Continue;
    }

    // Since we don't have a readImageView, we must create ImageViews for the new max level
    if (requiresSRGBViews)
    {
        // Initialize image views for both linear and srgb colorspaces
        ANGLE_TRY(initLinearAndSrgbReadViewsImpl(contextVk, viewType, image, formatSwizzle,
                                                 readSwizzle, baseLevel, levelCount, baseLayer,
                                                 layerCount, imageUsageFlags, astcDecodePrecision));
    }
    else
    {
        // Initialize image view for image's format's colorspace
        ANGLE_TRY(initReadViewsImpl(contextVk, viewType, image, formatSwizzle, readSwizzle,
                                    baseLevel, levelCount, baseLayer, layerCount, imageUsageFlags,
                                    astcDecodePrecision));
    }

    return angle::Result::Continue;
}

angle::Result ImageViewHelper::initReadViewsImpl(ContextVk *contextVk,
                                                 gl::TextureType viewType,
                                                 const ImageHelper &image,
                                                 const gl::SwizzleState &formatSwizzle,
                                                 const gl::SwizzleState &readSwizzle,
                                                 LevelIndex baseLevel,
                                                 uint32_t levelCount,
                                                 LayerIndex baseLayer,
                                                 uint32_t layerCount,
                                                 VkImageUsageFlags imageUsageFlags,
                                                 GLenum astcDecodePrecision)
{
    ASSERT(mImageViewSerial.valid());
    ASSERT(mReadColorspace != ImageViewColorspace::Invalid);

    const VkImageAspectFlags aspectFlags = GetFormatAspectFlags(image.getIntendedFormat());

    if (HasBothDepthAndStencilAspects(aspectFlags))
    {
        ANGLE_TRY(image.initLayerImageViewWithUsage(
            contextVk, viewType, VK_IMAGE_ASPECT_DEPTH_BIT, readSwizzle, &getReadImageView(),
            baseLevel, levelCount, baseLayer, layerCount, imageUsageFlags, astcDecodePrecision));
        ANGLE_TRY(image.initLayerImageViewWithUsage(
            contextVk, viewType, VK_IMAGE_ASPECT_STENCIL_BIT, readSwizzle,
            &mPerLevelRangeStencilReadImageViews[mCurrentBaseMaxLevelHash], baseLevel, levelCount,
            baseLayer, layerCount, imageUsageFlags, astcDecodePrecision));
    }
    else
    {
        ANGLE_TRY(image.initLayerImageViewWithUsage(
            contextVk, viewType, aspectFlags, readSwizzle, &getReadImageView(), baseLevel,
            levelCount, baseLayer, layerCount, imageUsageFlags, astcDecodePrecision));

        if (image.hasImmutableSampler())
        {
            ANGLE_TRY(image.initLayerImageViewWithYuvModeOverride(
                contextVk, viewType, aspectFlags, readSwizzle,
                &getSamplerExternal2DY2YEXTImageView(), baseLevel, levelCount, baseLayer,
                layerCount, gl::YuvSamplingMode::Y2Y, imageUsageFlags, astcDecodePrecision));
        }
    }

    const gl::TextureType fetchType =
        image.getType() == VK_IMAGE_TYPE_3D
            ? gl::TextureType::_3D
            : Get2DTextureType(image.getLayerCount(), image.getSamples());

    if (!image.getActualFormat().isBlock && !getCopyImageView().valid())
    {
        ANGLE_TRY(image.initLayerImageViewWithUsage(
            contextVk, fetchType, aspectFlags, formatSwizzle, &getCopyImageView(), LevelIndex(0),
            image.getLevelCount(), LayerIndex(0), image.getLayerCount(), imageUsageFlags,
            astcDecodePrecision));
    }
    return angle::Result::Continue;
}

angle::Result ImageViewHelper::initLinearAndSrgbReadViewsImpl(ContextVk *contextVk,
                                                              gl::TextureType viewType,
                                                              const ImageHelper &image,
                                                              const gl::SwizzleState &formatSwizzle,
                                                              const gl::SwizzleState &readSwizzle,
                                                              LevelIndex baseLevel,
                                                              uint32_t levelCount,
                                                              LayerIndex baseLayer,
                                                              uint32_t layerCount,
                                                              VkImageUsageFlags imageUsageFlags,
                                                              GLenum astcDecodePrecision)
{
    ASSERT(mReadColorspace != ImageViewColorspace::Invalid);

    // When we select the linear/srgb counterpart formats, we must first make sure they're
    // actually supported by the ICD. If they are not supported by the ICD, then we treat that as if
    // there is no counterpart format.
    const bool imageFormatIsSrgb      = image.getActualFormat().isSRGB;
    const angle::FormatID imageFormat = image.getActualFormatID();
    angle::FormatID srgbFormat = imageFormatIsSrgb ? imageFormat : ConvertToSRGB(imageFormat);
    if (srgbFormat != angle::FormatID::NONE &&
        !HasNonRenderableTextureFormatSupport(contextVk->getRenderer(), srgbFormat))
    {
        srgbFormat = angle::FormatID::NONE;
    }

    angle::FormatID linearFormat = !imageFormatIsSrgb ? imageFormat : ConvertToLinear(imageFormat);
    ASSERT(linearFormat != angle::FormatID::NONE);

    const VkImageAspectFlags aspectFlags = GetFormatAspectFlags(image.getIntendedFormat());

    if (HasBothDepthAndStencilAspects(aspectFlags))
    {
        ANGLE_TRY(image.initReinterpretedLayerImageView(
            contextVk, viewType, VK_IMAGE_ASPECT_DEPTH_BIT, readSwizzle,
            &mPerLevelRangeLinearReadImageViews[mCurrentBaseMaxLevelHash], baseLevel, levelCount,
            baseLayer, layerCount, imageUsageFlags, linearFormat, astcDecodePrecision));

        ANGLE_TRY(image.initReinterpretedLayerImageView(
            contextVk, viewType, VK_IMAGE_ASPECT_STENCIL_BIT, readSwizzle,
            &mPerLevelRangeStencilReadImageViews[mCurrentBaseMaxLevelHash], baseLevel, levelCount,
            baseLayer, layerCount, imageUsageFlags, linearFormat, astcDecodePrecision));
    }
    else
    {
        if (!mPerLevelRangeLinearReadImageViews[mCurrentBaseMaxLevelHash].valid())
        {
            ANGLE_TRY(image.initReinterpretedLayerImageView(
                contextVk, viewType, aspectFlags, readSwizzle,
                &mPerLevelRangeLinearReadImageViews[mCurrentBaseMaxLevelHash], baseLevel,
                levelCount, baseLayer, layerCount, imageUsageFlags, linearFormat,
                astcDecodePrecision));
        }

        if (srgbFormat != angle::FormatID::NONE &&
            !mPerLevelRangeSRGBReadImageViews[mCurrentBaseMaxLevelHash].valid())
        {
            ANGLE_TRY(image.initReinterpretedLayerImageView(
                contextVk, viewType, aspectFlags, readSwizzle,
                &mPerLevelRangeSRGBReadImageViews[mCurrentBaseMaxLevelHash], baseLevel, levelCount,
                baseLayer, layerCount, imageUsageFlags, srgbFormat, astcDecodePrecision));
        }

        if (image.hasImmutableSampler())
        {
            ANGLE_TRY(image.initLayerImageViewWithYuvModeOverride(
                contextVk, viewType, aspectFlags, readSwizzle,
                &getSamplerExternal2DY2YEXTImageView(), baseLevel, levelCount, baseLayer,
                layerCount, gl::YuvSamplingMode::Y2Y, imageUsageFlags, astcDecodePrecision));
        }
    }

    const gl::TextureType fetchType =
        image.getType() == VK_IMAGE_TYPE_3D
            ? gl::TextureType::_3D
            : Get2DTextureType(image.getLayerCount(), image.getSamples());

    if (!image.getActualFormat().isBlock)
    {
        if (!mLinearCopyImageView.valid())
        {
            ANGLE_TRY(image.initReinterpretedLayerImageView(
                contextVk, fetchType, aspectFlags, formatSwizzle, &mLinearCopyImageView,
                LevelIndex(0), image.getLevelCount(), LayerIndex(0), image.getLayerCount(),
                imageUsageFlags, linearFormat, astcDecodePrecision));
        }
        if (srgbFormat != angle::FormatID::NONE && !mSRGBCopyImageView.valid())
        {
            ANGLE_TRY(image.initReinterpretedLayerImageView(
                contextVk, fetchType, aspectFlags, formatSwizzle, &mSRGBCopyImageView,
                LevelIndex(0), image.getLevelCount(), LayerIndex(0), image.getLayerCount(),
                imageUsageFlags, srgbFormat, astcDecodePrecision));
        }
    }

    return angle::Result::Continue;
}

angle::Result ImageViewHelper::getLevelStorageImageView(ContextVk *contextVk,
                                                        gl::TextureType viewType,
                                                        const ImageHelper &image,
                                                        LevelIndex levelVk,
                                                        LayerIndex layer,
                                                        VkImageUsageFlags imageUsageFlags,
                                                        angle::FormatID formatID,
                                                        const ImageView **imageViewOut)
{
    ASSERT(mImageViewSerial.valid());

    if (!mLevelStorageImageViews.contains(formatID))
    {
        mLevelStorageImageViews[formatID] = std::make_unique<ImageViewVector>();
    }
    ASSERT(mLevelStorageImageViews.contains(formatID));

    ImageViewVector *levelStorageImageViews = mLevelStorageImageViews[formatID].get();
    ASSERT(levelStorageImageViews);

    ImageView *imageView =
        GetLevelImageView(levelStorageImageViews, levelVk, image.getLevelCount());

    *imageViewOut = imageView;
    if (imageView->valid())
    {
        return angle::Result::Continue;
    }

    // Create the view.  Note that storage images are not affected by swizzle parameters.
    return image.initReinterpretedLayerImageView(
        contextVk, viewType, image.getAspectFlags(), gl::SwizzleState(), imageView, levelVk, 1,
        layer, image.getLayerCount(), imageUsageFlags, formatID, GL_NONE);
}

angle::Result ImageViewHelper::getLevelLayerStorageImageView(ContextVk *contextVk,
                                                             const ImageHelper &image,
                                                             LevelIndex levelVk,
                                                             LayerIndex layer,
                                                             VkImageUsageFlags imageUsageFlags,
                                                             angle::FormatID formatID,
                                                             const ImageView **imageViewOut)
{
    ASSERT(image.valid());
    ASSERT(mImageViewSerial.valid());
    ASSERT(!image.getActualFormat().isBlock);

    if (!mLayerLevelStorageImageViews.contains(formatID))
    {
        mLayerLevelStorageImageViews[formatID] = std::make_unique<LayerLevelImageViewVector>();
    }
    ASSERT(mLayerLevelStorageImageViews.contains(formatID));

    LayerLevelImageViewVector *layerLevelStorageImageViews =
        mLayerLevelStorageImageViews[formatID].get();
    ASSERT(layerLevelStorageImageViews);

    ImageView *imageView =
        GetLevelLayerImageView(layerLevelStorageImageViews, levelVk, layer, image.getLevelCount(),
                               GetImageLayerCountForView(image));
    *imageViewOut = imageView;

    if (imageView->valid())
    {
        return angle::Result::Continue;
    }

    // Create the view.  Note that storage images are not affected by swizzle parameters.
    gl::TextureType viewType = Get2DTextureType(1, image.getSamples());
    return image.initReinterpretedLayerImageView(contextVk, viewType, image.getAspectFlags(),
                                                 gl::SwizzleState(), imageView, levelVk, 1, layer,
                                                 1, imageUsageFlags, formatID, GL_NONE);
}

angle::Result ImageViewHelper::getLevelLayerDrawImageViewImpl(ContextVk *contextVk,
                                                              const ImageHelper &image,
                                                              LevelIndex levelVk,
                                                              LayerIndex layer,
                                                              uint32_t layerCount,
                                                              ImageView *imageViewOut)
{
    ASSERT(imageViewOut != nullptr);

    // If we are initializing an imageview for use with EXT_srgb_write_control, we need to override
    // the format to its linear counterpart. Formats that cannot be reinterpreted are exempt from
    // this requirement.
    angle::FormatID actualFormat = image.getActualFormatID();
    angle::FormatID linearFormat = ConvertToLinear(actualFormat);
    angle::FormatID sRGBFormat   = ConvertToSRGB(actualFormat);
    if (mWriteColorspace == ImageViewColorspace::Linear && linearFormat != angle::FormatID::NONE)
    {
        actualFormat = linearFormat;
    }
    else if (mWriteColorspace == ImageViewColorspace::SRGB && sRGBFormat != angle::FormatID::NONE)
    {
        actualFormat = sRGBFormat;
    }

    // Note that these views are specifically made to be used as framebuffer attachments, and
    // therefore don't have swizzle.
    return image.initReinterpretedLayerImageView(
        contextVk, Get2DTextureType(layerCount, image.getSamples()), image.getAspectFlags(),
        gl::SwizzleState(), imageViewOut, levelVk, 1, layer, layerCount,
        vk::ImageHelper::kDefaultImageViewUsageFlags, actualFormat, GL_NONE);
}

angle::Result ImageViewHelper::getLevelDrawImageView(ContextVk *contextVk,
                                                     const ImageHelper &image,
                                                     LevelIndex levelVk,
                                                     LayerIndex layer,
                                                     uint32_t layerCount,
                                                     const ImageView **imageViewOut)
{
    ASSERT(image.valid());
    ASSERT(mImageViewSerial.valid());
    ASSERT(!image.getActualFormat().isBlock);

    if (mWriteColorspace == ImageViewColorspace::Invalid)
    {
        updateColorspace(image.getActualFormat());
    }
    ASSERT(mWriteColorspace != ImageViewColorspace::Invalid);

    ImageSubresourceRange range = MakeImageSubresourceDrawRange(image.toGLLevel(levelVk), layer,
                                                                GetLayerMode(image, layerCount),
                                                                mReadColorspace, mWriteColorspace);

    std::unique_ptr<ImageView> &view = mSubresourceDrawImageViews[range];
    if (view)
    {
        *imageViewOut = view.get();
        return angle::Result::Continue;
    }

    view          = std::make_unique<ImageView>();
    *imageViewOut = view.get();

    return getLevelLayerDrawImageViewImpl(contextVk, image, levelVk, layer, layerCount, view.get());
}

angle::Result ImageViewHelper::getLevelLayerDrawImageView(ContextVk *contextVk,
                                                          const ImageHelper &image,
                                                          LevelIndex levelVk,
                                                          LayerIndex layer,
                                                          const ImageView **imageViewOut)
{
    ASSERT(image.valid());
    ASSERT(mImageViewSerial.valid());
    ASSERT(!image.getActualFormat().isBlock);

    if (mWriteColorspace == ImageViewColorspace::Invalid)
    {
        updateColorspace(image.getActualFormat());
    }
    ASSERT(mWriteColorspace != ImageViewColorspace::Invalid);

    LayerLevelImageViewVector &imageViews = (mWriteColorspace == ImageViewColorspace::Linear)
                                                ? mLayerLevelDrawImageViewsLinear
                                                : mLayerLevelDrawImageViews;

    // Lazily allocate the storage for image views
    ImageView *imageView = GetLevelLayerImageView(
        &imageViews, levelVk, layer, image.getLevelCount(), GetImageLayerCountForView(image));
    *imageViewOut = imageView;

    if (imageView->valid())
    {
        return angle::Result::Continue;
    }

    return getLevelLayerDrawImageViewImpl(contextVk, image, levelVk, layer, 1, imageView);
}

angle::Result ImageViewHelper::getLevelDepthOrStencilImageView(ContextVk *contextVk,
                                                               const ImageHelper &image,
                                                               LevelIndex levelVk,
                                                               LayerIndex layer,
                                                               uint32_t layerCount,
                                                               VkImageAspectFlagBits aspect,
                                                               const ImageView **imageViewOut)
{
    ASSERT(image.valid());
    ASSERT(mImageViewSerial.valid());
    ASSERT((image.getAspectFlags() & aspect) != 0);

    ImageSubresourceRange range = MakeImageSubresourceDrawRange(
        image.toGLLevel(levelVk), layer, GetLayerMode(image, layerCount),
        ImageViewColorspace::Linear, ImageViewColorspace::Linear);

    SubresourceImageViewMap &imageViews = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                                              ? mSubresourceDepthOnlyImageViews
                                              : mSubresourceStencilOnlyImageViews;

    std::unique_ptr<ImageView> &view = imageViews[range];
    if (view)
    {
        *imageViewOut = view.get();
        return angle::Result::Continue;
    }

    view          = std::make_unique<ImageView>();
    *imageViewOut = view.get();

    return getLevelLayerDepthOrStencilImageViewImpl(contextVk, image, levelVk, layer, layerCount,
                                                    aspect, view.get());
}

angle::Result ImageViewHelper::getLevelLayerDepthOrStencilImageView(ContextVk *contextVk,
                                                                    const ImageHelper &image,
                                                                    LevelIndex levelVk,
                                                                    LayerIndex layer,
                                                                    VkImageAspectFlagBits aspect,
                                                                    const ImageView **imageViewOut)
{
    ASSERT(image.valid());
    ASSERT(mImageViewSerial.valid());
    ASSERT((image.getAspectFlags() & aspect) != 0);

    LayerLevelImageViewVector &imageViews = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                                                ? mLayerLevelDepthOnlyImageViews
                                                : mLayerLevelStencilOnlyImageViews;

    // Lazily allocate the storage for image views
    ImageView *imageView = GetLevelLayerImageView(
        &imageViews, levelVk, layer, image.getLevelCount(), GetImageLayerCountForView(image));
    *imageViewOut = imageView;

    if (imageView->valid())
    {
        return angle::Result::Continue;
    }

    return getLevelLayerDepthOrStencilImageViewImpl(contextVk, image, levelVk, layer, 1, aspect,
                                                    imageView);
}

angle::Result ImageViewHelper::getLevelLayerDepthOrStencilImageViewImpl(
    ContextVk *contextVk,
    const ImageHelper &image,
    LevelIndex levelVk,
    LayerIndex layer,
    uint32_t layerCount,
    VkImageAspectFlagBits aspect,
    ImageView *imageViewOut)
{
    // Note that these views are specifically made to be used as input attachments, and
    // therefore don't have swizzle.
    return image.initReinterpretedLayerImageView(
        contextVk, Get2DTextureType(layerCount, image.getSamples()), aspect, gl::SwizzleState(),
        imageViewOut, levelVk, 1, layer, layerCount, vk::ImageHelper::kDefaultImageViewUsageFlags,
        image.getActualFormatID(), GL_NONE);
}

angle::Result ImageViewHelper::initFragmentShadingRateView(ContextVk *contextVk, ImageHelper *image)
{
    ASSERT(image->valid());
    ASSERT(mImageViewSerial.valid());

    // Determine if we already have ImageView
    if (mFragmentShadingRateImageView.valid())
    {
        return angle::Result::Continue;
    }

    // Fragment shading rate image view always have -
    // - gl::TextureType    == gl::TextureType::_2D
    // - VkImageAspectFlags == VK_IMAGE_ASPECT_COLOR_BIT
    // - gl::SwizzleState   == gl::SwizzleState()
    // - baseMipLevelVk     == LevelIndex(0)
    // - levelCount         == 1
    // - baseArrayLayer     == LayerIndex(0)
    // - layerCount         == 1
    return image->initLayerImageViewWithUsage(contextVk, gl::TextureType::_2D,
                                              VK_IMAGE_ASPECT_COLOR_BIT, gl::SwizzleState(),
                                              &mFragmentShadingRateImageView, LevelIndex(0), 1,
                                              LayerIndex(0), 1, image->getUsage(), GL_NONE);
}

angle::FormatID ImageViewHelper::getColorspaceOverrideFormatImpl(ImageViewColorspace colorspace,
                                                                 angle::FormatID format) const
{
    ASSERT(colorspace != ImageViewColorspace::Invalid);

    angle::FormatID colorspaceOverrideFormat = format;
    angle::FormatID linearFormat             = ConvertToLinear(format);
    angle::FormatID sRGBFormat               = ConvertToSRGB(format);
    if (colorspace == ImageViewColorspace::Linear && linearFormat != angle::FormatID::NONE)
    {
        colorspaceOverrideFormat = linearFormat;
    }
    else if (colorspace == ImageViewColorspace::SRGB && sRGBFormat != angle::FormatID::NONE)
    {
        colorspaceOverrideFormat = sRGBFormat;
    }

    return colorspaceOverrideFormat;
}

void ImageViewHelper::updateColorspace(const angle::Format &imageFormat) const
{
    ImageViewColorspace imageViewColorspace = ImageViewColorspace::Invalid;
    mReadColorspace                         = ImageViewColorspace::Invalid;
    mWriteColorspace                        = ImageViewColorspace::Invalid;

    // Initialize colorspace based on image's format's colorspace
    imageViewColorspace =
        imageFormat.isSRGB ? ImageViewColorspace::SRGB : ImageViewColorspace::Linear;

    // Process EGL image colorspace override state
    if (!imageFormat.isSRGB && mColorspaceState.eglImageColorspace == egl::ImageColorspace::SRGB)
    {
        imageViewColorspace = ImageViewColorspace::SRGB;
    }
    else if (imageFormat.isSRGB &&
             mColorspaceState.eglImageColorspace == egl::ImageColorspace::Linear)
    {
        imageViewColorspace = ImageViewColorspace::Linear;
    }
    ASSERT(imageViewColorspace != ImageViewColorspace::Invalid);

    mReadColorspace  = imageViewColorspace;
    mWriteColorspace = imageViewColorspace;

    // Process srgb decode and srgb override state
    if (mReadColorspace == ImageViewColorspace::Linear)
    {
        if (mColorspaceState.srgbOverride == gl::SrgbOverride::SRGB &&
            rx::ConvertToSRGB(imageFormat.id) != angle::FormatID::NONE &&
            mColorspaceState.srgbDecode != gl::SrgbDecode::Skip)
        {
            mReadColorspace = ImageViewColorspace::SRGB;
        }
    }
    else
    {
        ASSERT(mReadColorspace == ImageViewColorspace::SRGB);

        if (mColorspaceState.srgbDecode == gl::SrgbDecode::Skip &&
            !mColorspaceState.hasStaticTexelFetchAccess)
        {
            mReadColorspace = ImageViewColorspace::Linear;
        }
    }

    // Process srgb write control state
    if (mWriteColorspace == ImageViewColorspace::SRGB &&
        mColorspaceState.srgbWriteControl == gl::SrgbWriteControlMode::Linear)
    {
        mWriteColorspace = ImageViewColorspace::Linear;
    }

    ASSERT(mReadColorspace != ImageViewColorspace::Invalid);
    ASSERT(mWriteColorspace != ImageViewColorspace::Invalid);
}

ImageOrBufferViewSubresourceSerial ImageViewHelper::getSubresourceSerial(gl::OwnerLevel levelGL,
                                                                         uint32_t levelCount,
                                                                         gl::OwnerLayer layer,
                                                                         LayerMode layerMode) const
{
    return getSubresourceSerialForColorspace(levelGL, levelCount, layer, layerMode,
                                             mReadColorspace);
}

ImageOrBufferViewSubresourceSerial ImageViewHelper::getSubresourceSerialForColorspace(
    gl::OwnerLevel levelGL,
    uint32_t levelCount,
    gl::OwnerLayer layer,
    LayerMode layerMode,
    ImageViewColorspace readColorspace) const
{
    ASSERT(mImageViewSerial.valid());

    ImageOrBufferViewSubresourceSerial serial = {};
    serial.viewSerial                         = mImageViewSerial;
    serial.subresource = MakeImageSubresourceReadRange(levelGL, levelCount, layer, layerMode,
                                                       readColorspace, mWriteColorspace);
    return serial;
}

ImageSubresourceRange ImageViewHelper::getSubresourceDrawRange(gl::OwnerLevel level,
                                                               gl::OwnerLayer layer,
                                                               LayerMode layerMode) const
{
    return MakeImageSubresourceDrawRange(level, layer, layerMode, mReadColorspace,
                                         mWriteColorspace);
}

// BufferViewHelper implementation.
BufferViewHelper::BufferViewHelper() : mInitialized(false), mOffset(0), mSize(0) {}

BufferViewHelper::BufferViewHelper(BufferViewHelper &&other) : Resource(std::move(other))
{
    std::swap(mInitialized, other.mInitialized);
    std::swap(mOffset, other.mOffset);
    std::swap(mSize, other.mSize);
    std::swap(mViews, other.mViews);
    std::swap(mViewSerial, other.mViewSerial);
}

BufferViewHelper::~BufferViewHelper() {}

void BufferViewHelper::init(Renderer *renderer, VkDeviceSize offset, VkDeviceSize size)
{
    ASSERT(mViews.empty());

    mOffset = offset;
    mSize   = size;

    if (!mViewSerial.valid())
    {
        mViewSerial = renderer->getResourceSerialFactory().generateImageOrBufferViewSerial();
    }

    mInitialized = true;
}

void BufferViewHelper::release(Renderer *renderer)
{
    if (!mInitialized)
    {
        return;
    }

    GarbageObjects garbage;

    for (auto &formatAndView : mViews)
    {
        BufferView &view = formatAndView.second;
        ASSERT(view.valid());

        garbage.emplace_back(GetGarbage(&view));
    }

    if (!garbage.empty())
    {
        renderer->collectGarbage(mUse, std::move(garbage));
        // Update image view serial.
        mViewSerial = renderer->getResourceSerialFactory().generateImageOrBufferViewSerial();
    }

    mUse.reset();
    mViews.clear();
    mOffset      = 0;
    mSize        = 0;
    mInitialized = false;
}

void BufferViewHelper::release(ContextVk *contextVk)
{
    if (!mInitialized)
    {
        return;
    }

    contextVk->flushDescriptorSetUpdates();
    return release(contextVk->getRenderer());
}

void BufferViewHelper::destroy(VkDevice device)
{
    for (auto &formatAndView : mViews)
    {
        BufferView &view = formatAndView.second;
        view.destroy(device);
    }

    mViews.clear();

    mOffset = 0;
    mSize   = 0;

    mViewSerial = kInvalidImageOrBufferViewSerial;
}

angle::Result BufferViewHelper::getView(ErrorContext *context,
                                        const BufferHelper &buffer,
                                        VkDeviceSize bufferOffset,
                                        const Format &format,
                                        const BufferView **viewOut,
                                        VkFormat *viewVkFormatOut)
{
    ASSERT(format.valid());

    vk::Renderer *renderer = context->getRenderer();
    VkFormat viewVkFormat  = format.getActualBufferVkFormat(renderer);
    if (viewVkFormatOut != nullptr)
    {
        *viewVkFormatOut = viewVkFormat;
    }

    auto iter = mViews.find(viewVkFormat);
    if (iter != mViews.end())
    {
        *viewOut = &iter->second;
        return angle::Result::Continue;
    }

    // If the size is not a multiple of pixelBytes, remove the extra bytes.  The last element cannot
    // be read anyway, and this is a requirement of Vulkan (for size to be a multiple of format
    // texel block size).
    const angle::Format &bufferFormat = format.getActualBufferFormat();
    const GLuint pixelBytes           = bufferFormat.pixelBytes;
    VkDeviceSize size                 = mSize - mSize % pixelBytes;

    VkBufferViewCreateInfo viewCreateInfo = {};
    viewCreateInfo.sType                  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewCreateInfo.buffer                 = buffer.getBuffer().getHandle();
    viewCreateInfo.format                 = viewVkFormat;
    viewCreateInfo.offset                 = mOffset + bufferOffset;
    viewCreateInfo.range                  = size;

    VkBufferUsageFlags2CreateInfoKHR usageFlagsCreateInfo = {};
    if (renderer->getFeatures().supportsMaintenance5.enabled)
    {
        // The usage for bufferview should only contain the uniform texel and storage texel bits.
        usageFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR;
        constexpr VkFormatFeatureFlags kViewUsageFormatFeatureMask =
            VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT | VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
        VkBufferUsageFlags2KHR viewUsage = 0;

        VkFormatFeatureFlags bufferFormatFeatureBits =
            renderer->getBufferFormatFeatureBits(bufferFormat.id, kViewUsageFormatFeatureMask);
        if ((bufferFormatFeatureBits & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) != 0)
        {
            viewUsage |= VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR;
        }
        if ((bufferFormatFeatureBits & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT) != 0)
        {
            viewUsage |= VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT_KHR;
        }

        ASSERT(viewUsage != 0);
        usageFlagsCreateInfo.usage = viewUsage;
        AddToPNextChain(&viewCreateInfo, &usageFlagsCreateInfo);
    }

    BufferView view;
    ANGLE_VK_TRY(context, view.init(context->getDevice(), viewCreateInfo));

    // Cache the view
    auto insertIter = mViews.insert({viewVkFormat, std::move(view)});
    *viewOut        = &insertIter.first->second;
    ASSERT(insertIter.second);

    return angle::Result::Continue;
}

ImageOrBufferViewSubresourceSerial BufferViewHelper::getSerial() const
{
    ASSERT(mViewSerial.valid());

    ImageOrBufferViewSubresourceSerial serial = {};
    serial.viewSerial                         = mViewSerial;
    return serial;
}

// ShaderProgramHelper implementation.
ShaderProgramHelper::ShaderProgramHelper()  = default;
ShaderProgramHelper::~ShaderProgramHelper() = default;

bool ShaderProgramHelper::valid(const gl::ShaderType shaderType) const
{
    return mShaders[shaderType];
}

void ShaderProgramHelper::destroy(Renderer *renderer)
{
    for (ShaderModulePtr &shader : mShaders)
    {
        shader.reset();
    }
}

void ShaderProgramHelper::release(ContextVk *contextVk)
{
    for (ShaderModulePtr &shader : mShaders)
    {
        shader.reset();
    }
}

void ShaderProgramHelper::setShader(gl::ShaderType shaderType, const ShaderModulePtr &shader)
{
    // The shaders must be set once and are not expected to change.
    ASSERT(!mShaders[shaderType]);
    ASSERT(shader && shader->valid());
    mShaders[shaderType] = shader;
}

void ShaderProgramHelper::createMonolithicPipelineCreationTask(vk::ErrorContext *context,
                                                               PipelineCacheAccess *pipelineCache,
                                                               const GraphicsPipelineDesc &desc,
                                                               const PipelineLayout &pipelineLayout,
                                                               PipelineHelper *pipeline) const
{
    std::shared_ptr<CreateMonolithicPipelineTask> monolithicPipelineCreationTask =
        std::make_shared<CreateMonolithicPipelineTask>(context->getRenderer(), *pipelineCache,
                                                       pipelineLayout, mShaders, desc);

    pipeline->setMonolithicPipelineCreationTask(std::move(monolithicPipelineCreationTask));
}

angle::Result ShaderProgramHelper::getOrCreateComputePipeline(
    vk::ErrorContext *context,
    ComputePipelineCache *computePipelines,
    PipelineCacheAccess *pipelineCache,
    const PipelineLayout &pipelineLayout,
    ComputePipelineOptions pipelineOptions,
    PipelineSource source,
    PipelineHelper **pipelineOut,
    const char *shaderName,
    VkSpecializationInfo *specializationInfo) const
{
    return computePipelines->getOrCreatePipeline(context, pipelineCache, pipelineLayout,
                                                 pipelineOptions, source, pipelineOut, shaderName,
                                                 specializationInfo, mShaders);
}

// ActiveHandleCounter implementation.
ActiveHandleCounter::ActiveHandleCounter() : mActiveCounts{}, mAllocatedCounts{} {}

ActiveHandleCounter::~ActiveHandleCounter() = default;

// CommandResources implementation.
CommandResources::CommandResources()  = default;
CommandResources::~CommandResources() = default;

void CommandResources::onBufferRead(VkAccessFlags readAccessType,
                                    PipelineStage readStage,
                                    BufferHelper *buffer)
{
    ASSERT(!buffer->isReleasedToExternal());
    mReadBuffers.emplace_back(buffer, readAccessType, readStage);
}

void CommandResources::onBufferWrite(VkAccessFlags writeAccessType,
                                     PipelineStage writeStage,
                                     BufferHelper *buffer)
{
    ASSERT(!buffer->isReleasedToExternal());
    mWriteBuffers.emplace_back(buffer, writeAccessType, writeStage);
}

void CommandResources::onImageRead(VkImageAspectFlags aspectFlags,
                                   ImageAccess imageAccess,
                                   ImageHelper *image)
{
    ASSERT(!image->isReleasedToExternal());
    ASSERT(image->getImageSerial().valid());
    mReadImages.emplace_back(image, aspectFlags, imageAccess);
}

void CommandResources::onImageWrite(gl::OwnerLevel levelStart,
                                    uint32_t levelCount,
                                    gl::OwnerLayer layerStart,
                                    uint32_t layerCount,
                                    VkImageAspectFlags aspectFlags,
                                    ImageAccess imageAccess,
                                    ImageHelper *image)
{
    ASSERT(!image->isReleasedToExternal());
    ASSERT(image->getImageSerial().valid());
    mWriteImages.emplace_back(CommandResourceImage{image, aspectFlags, imageAccess}, levelStart,
                              levelCount, layerStart, layerCount);
}

void CommandResources::onImageReadSubresources(gl::OwnerLevel levelStart,
                                               uint32_t levelCount,
                                               gl::OwnerLayer layerStart,
                                               uint32_t layerCount,
                                               VkImageAspectFlags aspectFlags,
                                               ImageAccess imageAccess,
                                               ImageHelper *image)
{
    ASSERT(!image->isReleasedToExternal());
    ASSERT(image->getImageSerial().valid());
    mReadImageSubresources.emplace_back(CommandResourceImage{image, aspectFlags, imageAccess},
                                        levelStart, levelCount, layerStart, layerCount);
}

void CommandResources::onBufferExternalAcquireRelease(BufferHelper *buffer)
{
    mExternalAcquireReleaseBuffers.emplace_back(
        CommandResourceBufferExternalAcquireRelease{buffer});
}

void CommandResources::onResourceAccess(Resource *resource)
{
    mGenericResources.emplace_back(CommandResourceGeneric{resource});
}

// DescriptorMetaCache implementation.
MetaDescriptorPool::MetaDescriptorPool() = default;

MetaDescriptorPool::~MetaDescriptorPool()
{
    ASSERT(mPayload.empty());
}

void MetaDescriptorPool::destroy(Renderer *renderer)
{
    for (auto &iter : mPayload)
    {
        DynamicDescriptorPoolPointer &pool = iter.second;
        ASSERT(pool.unique());
    }
    mPayload.clear();
}

angle::Result MetaDescriptorPool::bindCachedDescriptorPool(
    ErrorContext *context,
    const DescriptorSetLayoutDesc &descriptorSetLayoutDesc,
    uint32_t descriptorCountMultiplier,
    DescriptorSetLayoutCache *descriptorSetLayoutCache,
    DynamicDescriptorPoolPointer *dynamicDescriptorPoolOut)
{
    if (descriptorSetLayoutDesc.empty())
    {
        // No need for descriptorSet pool.
        return angle::Result::Continue;
    }

    auto cacheIter = mPayload.find(descriptorSetLayoutDesc);
    if (cacheIter != mPayload.end())
    {
        *dynamicDescriptorPoolOut = cacheIter->second;
        return angle::Result::Continue;
    }

    DescriptorSetLayoutPtr descriptorSetLayout;
    ANGLE_TRY(descriptorSetLayoutCache->getDescriptorSetLayout(context, descriptorSetLayoutDesc,
                                                               &descriptorSetLayout));

    DynamicDescriptorPool newDescriptorPool;
    ANGLE_TRY(InitDynamicDescriptorPool(context, descriptorSetLayoutDesc, *descriptorSetLayout,
                                        descriptorCountMultiplier, &newDescriptorPool));

    ASSERT(newDescriptorPool.valid());
    DynamicDescriptorPoolPointer newDynamicDescriptorPoolPtr(context->getDevice(),
                                                             std::move(newDescriptorPool));
    mPayload.emplace(descriptorSetLayoutDesc, newDynamicDescriptorPoolPtr);
    *dynamicDescriptorPoolOut = std::move(newDynamicDescriptorPoolPtr);

    return angle::Result::Continue;
}

static_assert(static_cast<uint32_t>(PresentMode::ImmediateKHR) == VK_PRESENT_MODE_IMMEDIATE_KHR,
              "PresentMode must be updated");
static_assert(static_cast<uint32_t>(PresentMode::MailboxKHR) == VK_PRESENT_MODE_MAILBOX_KHR,
              "PresentMode must be updated");
static_assert(static_cast<uint32_t>(PresentMode::FifoKHR) == VK_PRESENT_MODE_FIFO_KHR,
              "PresentMode must be updated");
static_assert(static_cast<uint32_t>(PresentMode::FifoRelaxedKHR) ==
                  VK_PRESENT_MODE_FIFO_RELAXED_KHR,
              "PresentMode must be updated");
static_assert(static_cast<uint32_t>(PresentMode::SharedDemandRefreshKHR) ==
                  VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,
              "PresentMode must be updated");
static_assert(static_cast<uint32_t>(PresentMode::SharedContinuousRefreshKHR) ==
                  VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR,
              "PresentMode must be updated");

VkPresentModeKHR ConvertPresentModeToVkPresentMode(PresentMode presentMode)
{
    return static_cast<VkPresentModeKHR>(presentMode);
}

PresentMode ConvertVkPresentModeToPresentMode(VkPresentModeKHR vkPresentMode)
{
    return static_cast<PresentMode>(vkPresentMode);
}

}  // namespace vk
}  // namespace rx
