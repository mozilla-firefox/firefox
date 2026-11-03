//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// TextureGL.h: Defines the class interface for TextureGL.

#ifndef LIBANGLE_RENDERER_GL_TEXTUREGL_H_
#define LIBANGLE_RENDERER_GL_TEXTUREGL_H_

#include <optional>

#include "libANGLE/Texture.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/renderer/TextureImpl.h"

namespace rx
{

class BlitGL;
class FunctionsGL;
class StateManagerGL;

struct LUMAWorkaroundGL
{
    bool enabled;
    GLenum workaroundFormat;

    LUMAWorkaroundGL();
    LUMAWorkaroundGL(bool enabled, GLenum workaroundFormat);
};

// Structure containing information about format and workarounds for each mip level of the
// TextureGL.
struct LevelInfoGL
{
    // Format of the data used in this mip level.
    GLenum sourceFormat;

    // Internal format used for the native call to define this texture
    GLenum nativeInternalFormat;

    // If this mip level requires sampler-state re-writing so that only a red channel is exposed.
    // In GLES 2.0, depth textures are treated as luminance, so we check the
    // context's major version when applying the depth swizzle.
    bool depthStencilWorkaround;

    // Information about luminance alpha texture workarounds in the core profile.
    LUMAWorkaroundGL lumaWorkaround;

    // If this texture level hides the fact that it has an alpha channel by setting the sampler
    // parameters to always sample 1.0.
    bool emulatedAlphaChannel;

    LevelInfoGL();
    LevelInfoGL(GLenum sourceFormat,
                GLenum nativeInternalFormat,
                bool depthStencilWorkaround,
                const LUMAWorkaroundGL &lumaWorkaround,
                bool emulatedAlphaChannel);
};

class TextureGL : public TextureImpl
{
  public:
    TextureGL(const gl::TextureState &state, GLuint id);
    ~TextureGL() override;

    void onDestroy(const gl::Context *context) override;

    angle::Result setImage(const gl::Context *context,
                           const gl::ImageIndex &index,
                           GLenum internalFormat,
                           const gl::Extents &size,
                           GLenum format,
                           GLenum type,
                           const gl::PixelUnpackState &unpack,
                           gl::Buffer *unpackBuffer,
                           const uint8_t *pixels) override;
    angle::Result setSubImage(const gl::Context *context,
                              const gl::ImageIndex &index,
                              const gl::Box &area,
                              GLenum format,
                              GLenum type,
                              const gl::PixelUnpackState &unpack,
                              gl::Buffer *unpackBuffer,
                              const uint8_t *pixels) override;

    angle::Result setCompressedImage(const gl::Context *context,
                                     const gl::ImageIndex &index,
                                     GLenum internalFormat,
                                     const gl::Extents &size,
                                     const gl::PixelUnpackState &unpack,
                                     size_t imageSize,
                                     const uint8_t *pixels) override;
    angle::Result setCompressedSubImage(const gl::Context *context,
                                        const gl::ImageIndex &index,
                                        const gl::Box &area,
                                        GLenum format,
                                        const gl::PixelUnpackState &unpack,
                                        size_t imageSize,
                                        const uint8_t *pixels) override;

    angle::Result copyImage(const gl::Context *context,
                            const gl::ImageIndex &index,
                            const gl::Rectangle &sourceArea,
                            GLenum internalFormat,
                            gl::Framebuffer *source) override;
    angle::Result copySubImage(const gl::Context *context,
                               const gl::ImageIndex &index,
                               const gl::Offset &destOffset,
                               const gl::Rectangle &sourceArea,
                               gl::Framebuffer *source) override;

    angle::Result copyTexture(const gl::Context *context,
                              const gl::ImageIndex &index,
                              GLenum internalFormat,
                              GLenum type,
                              gl::LevelIndex sourceLevel,
                              bool unpackFlipY,
                              bool unpackPremultiplyAlpha,
                              bool unpackUnmultiplyAlpha,
                              const gl::Texture *source) override;
    angle::Result copySubTexture(const gl::Context *context,
                                 const gl::ImageIndex &index,
                                 const gl::Offset &destOffset,
                                 gl::LevelIndex sourceLevel,
                                 const gl::Box &sourceBox,
                                 bool unpackFlipY,
                                 bool unpackPremultiplyAlpha,
                                 bool unpackUnmultiplyAlpha,
                                 const gl::Texture *source) override;
    angle::Result copySubTextureHelper(const gl::Context *context,
                                       gl::TextureTarget target,
                                       size_t level,
                                       const gl::Offset &destOffset,
                                       GLint sourceLevel,
                                       const gl::Rectangle &sourceArea,
                                       const gl::InternalFormat &destFormat,
                                       bool unpackFlipY,
                                       bool unpackPremultiplyAlpha,
                                       bool unpackUnmultiplyAlpha,
                                       const gl::Texture *source);

    angle::Result setStorage(const gl::Context *context,
                             gl::TextureType type,
                             size_t levels,
                             GLenum internalFormat,
                             const gl::Extents &size) override;

    angle::Result setStorageMultisample(const gl::Context *context,
                                        gl::TextureType type,
                                        GLsizei samples,
                                        GLint internalformat,
                                        const gl::Extents &size,
                                        bool fixedSampleLocations) override;

    angle::Result setStorageExternalMemory(const gl::Context *context,
                                           gl::TextureType type,
                                           size_t levels,
                                           GLenum internalFormat,
                                           const gl::Extents &size,
                                           gl::MemoryObject *memoryObject,
                                           GLuint64 offset,
                                           GLbitfield createFlags,
                                           GLbitfield usageFlags,
                                           const void *imageCreateInfoPNext) override;

    angle::Result setImageExternal(const gl::Context *context,
                                   gl::TextureType type,
                                   egl::Stream *stream,
                                   const egl::Stream::GLTextureDescription &desc) override;

    angle::Result generateMipmap(const gl::Context *context) override;

    angle::Result clearImage(const gl::Context *context,
                             gl::LevelIndex level,
                             GLenum format,
                             GLenum type,
                             const uint8_t *data) override;
    angle::Result clearSubImage(const gl::Context *context,
                                gl::LevelIndex level,
                                const gl::Box &area,
                                GLenum format,
                                GLenum type,
                                const uint8_t *data) override;

    angle::Result bindTexImage(const gl::Context *context, egl::Surface *surface) override;
    angle::Result releaseTexImage(const gl::Context *context) override;

    angle::Result setEGLImageTarget(const gl::Context *context,
                                    gl::TextureType type,
                                    egl::Image *image) override;

    GLuint getTextureID() const { return mTextureID; }

    gl::TextureType getType() const;

    angle::Result syncState(const gl::Context *context,
                            const gl::Texture::DirtyBits &dirtyBits,
                            gl::Command source) override;
    bool hasAnyDirtyBit() const;

    angle::Result setBaseLevel(const gl::Context *context, GLuint baseLevel) override;
    angle::Result setMaxLevel(const gl::Context *context, GLuint maxLevel);

    angle::Result initializeContents(const gl::Context *context,
                                     GLenum binding,
                                     const gl::ImageIndex &imageIndex) override;

    GLint getRequiredExternalTextureImageUnits(const gl::Context *context) override;

    angle::Result setMinFilter(const gl::Context *context, GLenum filter);
    angle::Result setMagFilter(const gl::Context *context, GLenum filter);

    angle::Result setSwizzle(const gl::Context *context, GLint swizzle[4]);

    angle::Result setBuffer(const gl::Context *context, GLenum internalFormat) override;

    GLenum getNativeInternalFormat(const gl::ImageIndex &index) const;
    bool hasEmulatedAlphaChannel(const gl::ImageIndex &index) const;

    struct NativeLevelTarget
    {
        GLuint textureID         = 0;
        gl::TextureTarget target = gl::TextureTarget::InvalidEnum;
        GLint level              = 0;
    };
    NativeLevelTarget getNativeLevelTarget(gl::TextureTarget target,
                                           size_t level,
                                           bool invalidateCpuData = true) const;

  private:
    // For the copyImageSubData3DFailsOnTailMipLevels workaround: caches CPU texel data uploaded to
    // 1x1xD 3D tail mip levels so copyBetweenNativeLevels can re-upload via glTexSubImage3D instead
    // of calling glCopyImageSubDataEXT (which fails on PowerVR when level >= log2(max(w, h)) + 1).
    struct TailMip3DCpuData
    {
        TailMip3DCpuData() = default;

        void clearCpuData() const
        {
            data.clear();
            contiguousSlicesWritten = 0;
        }

        // Marked mutable because getNativeLevelTarget() is called on const TextureGL pointers
        // (e.g., when attaching a texture level to a framebuffer or binding an image unit) and
        // must invalidate any cached CPU data when the level may be modified by the GPU.
        mutable std::vector<uint8_t> data = {};
        // Tracks the number of consecutive Z slices starting from slice 0 (i.e. [0,
        // contiguousSlicesWritten)) that have valid texel data cached in `data`.
        // `copyBetweenNativeLevels` can only use `data` to re-upload the entire 1x1xD level via
        // glTexSubImage3D when all slices [0, size.depth) have been contiguously populated
        // (contiguousSlicesWritten == size.depth).
        mutable size_t contiguousSlicesWritten = 0;

        GLenum format   = GL_NONE;
        GLenum type     = GL_NONE;
        bool compressed = false;
    };

    struct VirtualLevelInfo
    {
        GLuint texID = 0;
        gl::Extents size;
        GLenum internalFormat = GL_NONE;

        TailMip3DCpuData tailMip3DCpuData;
    };

    // Tracks the per-level metadata/companion textures and the contiguous mipmap chain currently
    // materialized in the primary native texture mTextureID when the virtualizeBaseLevelAndMipchain
    // workaround is active for this texture.
    //
    // Under this workaround, individual glTexImage* calls initially allocate standalone 1-level
    // native textures in virtualLevels[idx].texID. When the texture is synchronized for use
    // (in syncVirtualMipchain) or allocated via glTexStorage* / eglBindTexImage / EGLImage,
    // mTextureID is allocated with immutable storage (glTexStorage*) holding the contiguous range
    // of mipmap-compatible virtual levels [baseLevel, baseLevel + levels), where virtual level
    // (baseLevel + k) resides at native level k of mTextureID.
    struct VirtualMipChain
    {
        explicit VirtualMipChain(size_t maxLevelInfoCount) : virtualLevels(maxLevelInfoCount) {}

        // The virtual level stored at native level 0 of mTextureID. Set to 0 for glTexStorage*,
        // eglBindTexImage, and EGLImage targets, or to the texture's effective
        // GL_TEXTURE_BASE_LEVEL when syncVirtualMipchain materializes the mipchain.
        size_t baseLevel = 0;

        // The number of contiguous, mip-compatible virtual levels currently allocated in
        // mTextureID (or 0 if mTextureID has not yet been allocated by syncVirtualMipchain and all
        // defined levels currently reside in per-level companion textures). This count is based on
        // how many consecutive defined levels starting at baseLevel have matching internal formats
        // and valid halved mip dimensions down to 1x1x1; GL_TEXTURE_MAX_LEVEL is not used to
        // truncate this allocation, and is instead translated to native (maxLevel - baseLevel) on
        // mTextureID so MAX_LEVEL changes do not force reallocation.
        size_t levels = 0;

        // The dimensions of native level 0 in mTextureID (virtual level baseLevel).
        gl::Extents baseSize = {};

        // The native sized internal format of mTextureID.
        GLenum internalFormat = GL_NONE;

        // True if mTextureID is backed by an EGLImage target.
        bool isEGLImageTarget = false;

        // True if mTextureID must be regenerated with a new texture ID on the next mipchain
        // allocation (e.g., after being bound as an EGLImage source or target).
        bool forceReallocation = false;

        // Per-(target, level) dimensions, format, and optional standalone 1-level companion
        // texture ID (when texID != 0, the level resides in texID at native level 0 rather than in
        // mTextureID).
        std::vector<VirtualLevelInfo> virtualLevels;
    };

    bool isVirtualMipchainType() const;
    angle::Result allocateTexStorage(const gl::Context *context,
                                     GLuint texID,
                                     gl::TextureType type,
                                     size_t levels,
                                     GLenum internalFormat,
                                     const gl::Extents &size);
    angle::Result copyBetweenNativeLevels(const gl::Context *context,
                                          GLuint srcTexID,
                                          gl::TextureTarget srcTarget,
                                          GLint srcLevel,
                                          GLuint dstTexID,
                                          gl::TextureTarget dstTarget,
                                          GLint dstLevel,
                                          const VirtualLevelInfo &dstVLevel);
    angle::Result evacuateMipchainToLevelTextures(const gl::Context *context);
    angle::Result allocateOrUpdateVirtualLevel(const gl::Context *context,
                                               gl::TextureTarget target,
                                               size_t level,
                                               GLenum internalFormat,
                                               const gl::Extents &size);
    angle::Result syncVirtualMipchain(const gl::Context *context);
    void clearVirtualLevels(const gl::Context *context);

    // For the copyImageSubData3DFailsOnTailMipLevels workaround.
    angle::Result updateVirtualLevel3D1x1CpuData(const gl::Context *context,
                                                 size_t level,
                                                 const gl::Box &area,
                                                 GLenum format,
                                                 GLenum type,
                                                 GLenum nativeFormat,
                                                 GLenum nativeType,
                                                 const gl::PixelUnpackState &unpack,
                                                 bool hasUnpackBuffer,
                                                 const uint8_t *pixels);

    // For the readback3DDepthStencilPbosDuringUpload workaround.
    bool shouldReadback3DPboDuringUpload(const gl::Context *context,
                                         const gl::InternalFormat &internalFormatInfo,
                                         GLenum format,
                                         GLenum type,
                                         const gl::PixelUnpackState &unpack) const;
    angle::Result setSubImage3DFromUnpackBufferViaCpu(const gl::Context *context,
                                                      const NativeLevelTarget &nlt,
                                                      size_t level,
                                                      const gl::Box &area,
                                                      GLenum format,
                                                      GLenum type,
                                                      GLenum nativeFormat,
                                                      GLenum nativeType,
                                                      const gl::PixelUnpackState &unpack,
                                                      const uint8_t *pixels);

    angle::Result recreateTexture(const gl::Context *context);

    angle::Result recreateTextureOnTexImage3DDepthIncreaseWorkaround(const gl::Context *context,
                                                                     gl::TextureTarget target,
                                                                     size_t level,
                                                                     const gl::Extents &size);

    angle::Result setImageHelper(const gl::Context *context,
                                 gl::TextureTarget target,
                                 size_t level,
                                 GLenum internalFormat,
                                 const gl::Extents &size,
                                 GLenum format,
                                 GLenum type,
                                 const uint8_t *pixels);
    // This changes the current pixel unpack state that will have to be reapplied.
    angle::Result reserveTexImageToBeFilled(const gl::Context *context,
                                            gl::TextureTarget target,
                                            size_t level,
                                            GLenum internalFormat,
                                            const gl::Extents &size,
                                            GLenum format,
                                            GLenum type);
    angle::Result setSubImageRowByRowWorkaround(const gl::Context *context,
                                                gl::TextureTarget target,
                                                size_t level,
                                                const gl::Box &area,
                                                GLenum format,
                                                GLenum type,
                                                const gl::PixelUnpackState &unpack,
                                                const gl::Buffer *unpackBuffer,
                                                size_t maxBytesUploadedPerChunk,
                                                const uint8_t *pixels);

    angle::Result setSubImagePaddingWorkaround(const gl::Context *context,
                                               gl::TextureTarget target,
                                               size_t level,
                                               const gl::Box &area,
                                               GLenum format,
                                               GLenum type,
                                               const gl::PixelUnpackState &unpack,
                                               const gl::Buffer *unpackBuffer,
                                               const uint8_t *pixels);
    angle::Result allocateMipmapLevelsForGeneration(const gl::Context *context);

    angle::Result syncTextureStateSwizzle(const gl::Context *context,
                                          const FunctionsGL *functions,
                                          GLenum name,
                                          GLenum value,
                                          GLenum *currentlyAppliedValue);

    void setLevelInfo(const gl::Context *context,
                      gl::TextureTarget target,
                      size_t level,
                      size_t levelCount,
                      const LevelInfoGL &levelInfo);
    void setLevelInfo(const gl::Context *context,
                      gl::TextureType type,
                      size_t level,
                      size_t levelCount,
                      const LevelInfoGL &levelInfo);
    const LevelInfoGL &getLevelInfo(gl::TextureTarget target, size_t level) const;
    const LevelInfoGL &getBaseLevelInfo() const;

    angle::Result initializeContentsImpl(const gl::Context *context,
                                         GLenum binding,
                                         const gl::ImageIndex &imageIndex);

    angle::Result handleCopyImageSelfCopyRedefine(const gl::Context *context,
                                                  GLenum internalFormat,
                                                  GLenum initTexFormat,
                                                  GLenum initTexType,
                                                  const gl::Rectangle &sourceArea,
                                                  bool outside,
                                                  const gl::ImageIndex &destIndex,
                                                  gl::Framebuffer *source);

    std::vector<LevelInfoGL> mLevelInfo;

    // Present once this texture's levels are managed by the virtualizeBaseLevelAndMipchain
    // workaround; std::nullopt when the workaround is inactive or the texture was just reset.
    std::optional<VirtualMipChain> mVirtualMipChain;

    gl::Texture::DirtyBits mLocalDirtyBits;

    // All dirty bits ever sychronized by this texture OR'd together. Used to know what state needs
    // to be resynced if the texture is ever recreated without needing extension checks or state
    // comparisons.
    gl::Texture::DirtyBits mAllModifiedDirtyBits;

    gl::SwizzleState mAppliedSwizzle;
    gl::SamplerState mAppliedSampler;
    GLuint mAppliedBaseLevel;
    GLuint mAppliedMaxLevel;
    GLenum mAppliedDepthStencilTextureMode;

    GLuint mTextureID;
};
}  // namespace rx

#endif  // LIBANGLE_RENDERER_GL_TEXTUREGL_H_
