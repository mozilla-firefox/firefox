/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWTARGETWEBGL_INTERNAL_H
#define MOZILLA_GFX_DRAWTARGETWEBGL_INTERNAL_H

#include "DrawTargetWebgl.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/gfx/Etagere.h"
#include "mozilla/gfx/PathSkia.h"
#include "mozilla/gfx/WPFGpuRaster.h"

namespace mozilla::gfx {

// CacheEnty is a generic interface for various items that need to be cached to
// a texture.
class CacheEntry : public RefCounted<CacheEntry> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(CacheEntry)

  CacheEntry(const Matrix& aTransform, const IntRect& aBounds, HashNumber aHash)
      : mTransform(aTransform), mBounds(aBounds), mHash(aHash) {}
  virtual ~CacheEntry() = default;

  void Link(const RefPtr<TextureHandle>& aHandle);
  void Unlink();

  const RefPtr<TextureHandle>& GetHandle() const { return mHandle; }

  const Matrix& GetTransform() const { return mTransform; }
  const IntRect& GetBounds() const { return mBounds; }
  HashNumber GetHash() const { return mHash; }

  virtual bool IsValid() const { return true; }

 protected:
  virtual void RemoveFromList() = 0;

  // The handle of the rendered cache item.
  RefPtr<TextureHandle> mHandle;
  // The transform that was used to render the entry. This is necessary as
  // the geometry might only be correctly rendered in device space after
  // the transform is applied, so in general we can't cache untransformed
  // geometry.
  Matrix mTransform;
  // The device space bounds of the rendered geometry.
  IntRect mBounds;
  // A hash of the geometry that may be used for quickly rejecting entries.
  HashNumber mHash;
};

// CacheEntryImpl provides type-dependent boilerplate code for implementations
// of CacheEntry.
template <typename T>
class CacheEntryImpl : public CacheEntry, public LinkedListElement<RefPtr<T>> {
  typedef LinkedListElement<RefPtr<T>> ListType;

 public:
  CacheEntryImpl(const Matrix& aTransform, const IntRect& aBounds,
                 HashNumber aHash)
      : CacheEntry(aTransform, aBounds, aHash) {}

  void RemoveFromList() override {
    if (ListType::isInList()) {
      ListType::remove();
    }
  }
};

// CacheImpl manages a list of CacheEntry.
template <typename T, bool BIG>
class CacheImpl {
 protected:
  typedef LinkedList<RefPtr<T>> ListType;

  // Whether the cache should be small and space-efficient or prioritize speed.
  static constexpr size_t kNumChains = BIG ? 499 : 71;

 public:
  ~CacheImpl() {
    for (auto& chain : mChains) {
      while (RefPtr<T> entry = chain.popLast()) {
        entry->Unlink();
      }
    }
  }

 protected:
  ListType& GetChain(HashNumber aHash) { return mChains[aHash % kNumChains]; }

  void Insert(T* aEntry) { GetChain(aEntry->GetHash()).insertFront(aEntry); }

  ListType mChains[kNumChains];
};

// BackingTexture provides information about the shared or standalone texture
// that is backing a texture handle.
class BackingTexture {
 public:
  BackingTexture(const IntSize& aSize, SurfaceFormat aFormat,
                 const RefPtr<WebGLTexture>& aTexture);

  SurfaceFormat GetFormat() const { return mFormat; }
  IntSize GetSize() const { return mSize; }

  static inline size_t UsedBytes(SurfaceFormat aFormat, const IntSize& aSize) {
    return size_t(BytesPerPixel(aFormat)) * size_t(aSize.width) *
           size_t(aSize.height);
  }

  size_t UsedBytes() const { return UsedBytes(GetFormat(), GetSize()); }

  const RefPtr<WebGLTexture>& GetWebGLTexture() const { return mTexture; }

  bool IsInitialized() const { return mFlags & INITIALIZED; }
  void MarkInitialized() { mFlags |= INITIALIZED; }

  bool IsRenderable() const { return mFlags & RENDERABLE; }
  void MarkRenderable() { mFlags |= RENDERABLE; }

 protected:
  IntSize mSize;
  SurfaceFormat mFormat;
  RefPtr<WebGLTexture> mTexture;

 private:
  enum Flags : uint8_t {
    INITIALIZED = 1 << 0,
    RENDERABLE = 1 << 1,
  };

  uint8_t mFlags = 0;
};

// TextureHandle is an abstract base class for supplying textures to drawing
// commands that may be backed by different resource types (such as a shared
// or standalone texture). It may be further linked to use-specific metadata
// such as for shadow drawing or for cached entries in the glyph cache.
class TextureHandle : public RefCounted<TextureHandle>,
                      public SupportsWeakPtr,
                      public LinkedListElement<RefPtr<TextureHandle>> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(TextureHandle)

  enum Type { SHARED, STANDALONE };

  virtual Type GetType() const = 0;
  virtual IntRect GetBounds() const = 0;
  IntSize GetSize() const { return GetBounds().Size(); }
  virtual SurfaceFormat GetFormat() const = 0;

  virtual BackingTexture* GetBackingTexture() = 0;

  size_t UsedBytes() const {
    return BackingTexture::UsedBytes(GetFormat(), GetSize());
  }

  virtual void UpdateSize(const IntSize& aSize) {}

  virtual void Cleanup(SharedContextWebgl& aContext) {}

  virtual ~TextureHandle() {}

  bool IsValid() const { return mValid; }
  void Invalidate() { mValid = false; }

  void ClearSurface() { mSurface = nullptr; }
  void SetSurface(const RefPtr<SourceSurface>& aSurface) {
    mSurface = aSurface;
  }
  already_AddRefed<SourceSurface> GetSurface() const {
    RefPtr<SourceSurface> surface(mSurface);
    return surface.forget();
  }

  float GetSigma() const { return mSigma; }
  void SetSigma(float aSigma) { mSigma = aSigma; }
  bool IsShadow() const { return mSigma >= 0.0f; }

  void SetSamplingOffset(const IntPoint& aSamplingOffset) {
    mSamplingOffset = aSamplingOffset;
  }
  const IntPoint& GetSamplingOffset() const { return mSamplingOffset; }
  IntRect GetSamplingRect() const {
    return IntRect(GetSamplingOffset(), GetSize());
  }

  const RefPtr<CacheEntry>& GetCacheEntry() const { return mCacheEntry; }
  void SetCacheEntry(const RefPtr<CacheEntry>& aEntry) { mCacheEntry = aEntry; }

  // Note as used if there is corresponding surface or cache entry.
  bool IsUsed() const {
    return !mSurface.IsDead() || (mCacheEntry && mCacheEntry->IsValid());
  }

 private:
  bool mValid = true;
  // If applicable, weak pointer to the SourceSurface that is linked to this
  // TextureHandle.
  ThreadSafeWeakPtr<SourceSurface> mSurface;
  // If this TextureHandle stores a cached shadow, then we need to remember the
  // blur sigma used to produce the shadow.
  float mSigma = -1.0f;
  // If the originating surface requested a sampling rect, then we need to know
  // the offset of the subrect within the surface for texture coordinates.
  IntPoint mSamplingOffset;
  // If applicable, the CacheEntry that is linked to this TextureHandle.
  RefPtr<CacheEntry> mCacheEntry;
};

class SharedTextureHandle;

// SharedTexture is a large slab texture that is subdivided (by using a
// TexturePacker) to hold many small SharedTextureHandles. This avoids needing
// to allocate many WebGL textures for every single small Canvas 2D texture.
class SharedTexture : public RefCounted<SharedTexture>, public BackingTexture {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(SharedTexture)

  SharedTexture(const IntSize& aSize, SurfaceFormat aFormat,
                const RefPtr<WebGLTexture>& aTexture,
                const dom::ContentParentId& aContentId);
  ~SharedTexture();

  already_AddRefed<SharedTextureHandle> Allocate(const IntSize& aSize);
  bool Free(SharedTextureHandle& aHandle);

  bool HasAllocatedHandles() const {
    return mAtlasAllocator && Etagere::etagere_atlas_allocator_allocated_space(
                                  mAtlasAllocator) > 0;
  }

  const dom::ContentParentId& GetContentId() const { return mContentId; }

 private:
  Etagere::AtlasAllocator* mAtlasAllocator = nullptr;
  dom::ContentParentId mContentId;
};

// SharedTextureHandle is an allocated region within a large SharedTexture page
// that owns it.
class SharedTextureHandle : public TextureHandle {
  friend class SharedTexture;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SharedTextureHandle, override)

  SharedTextureHandle(Etagere::AllocationId aId, const IntRect& aBounds,
                      SharedTexture* aTexture);

  Type GetType() const override { return Type::SHARED; }

  IntRect GetBounds() const override { return mBounds; }

  SurfaceFormat GetFormat() const override { return mTexture->GetFormat(); }

  BackingTexture* GetBackingTexture() override { return mTexture.get(); }

  void Cleanup(SharedContextWebgl& aContext) override;

  const RefPtr<SharedTexture>& GetOwner() const { return mTexture; }

 private:
  Etagere::AllocationId mAllocationId = Etagere::INVALID_ALLOCATION_ID;
  IntRect mBounds;
  RefPtr<SharedTexture> mTexture;
};

// StandaloneTexture is a texture that can not be effectively shared within
// a SharedTexture page, such that it is better to assign it its own WebGL
// texture.
class StandaloneTexture : public TextureHandle, public BackingTexture {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(StandaloneTexture, override)

  StandaloneTexture(const IntSize& aSize, SurfaceFormat aFormat,
                    const RefPtr<WebGLTexture>& aTexture);

  Type GetType() const override { return Type::STANDALONE; }

  IntRect GetBounds() const override {
    return IntRect(IntPoint(0, 0), BackingTexture::GetSize());
  }

  SurfaceFormat GetFormat() const override {
    return BackingTexture::GetFormat();
  }

  using BackingTexture::UsedBytes;

  BackingTexture* GetBackingTexture() override { return this; }

  void UpdateSize(const IntSize& aSize) override { mSize = aSize; }

  void Cleanup(SharedContextWebgl& aContext) override;
};

// GlyphCacheEntry stores rendering metadata for a rendered text run, as well
// the handle to the texture it was rendered into, so that it can be located
// for reuse under similar rendering circumstances.
class GlyphCacheEntry : public CacheEntryImpl<GlyphCacheEntry> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GlyphCacheEntry, override)

  GlyphCacheEntry(const GlyphBuffer& aBuffer, const DeviceColor& aColor,
                  const Matrix& aTransform, const IntPoint& aQuantizeScale,
                  const IntRect& aBounds, const IntRect& aFullBounds,
                  HashNumber aHash,
                  StoredStrokeOptions* aStrokeOptions = nullptr);
  ~GlyphCacheEntry();

  const GlyphBuffer& GetGlyphBuffer() const { return mBuffer; }

  bool MatchesGlyphs(const GlyphBuffer& aBuffer, const DeviceColor& aColor,
                     const Matrix& aTransform, const IntPoint& aQuantizeOffset,
                     const IntPoint& aBoundsOffset, const IntRect& aClipRect,
                     HashNumber aHash, const StrokeOptions* aStrokeOptions);

  static HashNumber HashGlyphs(const GlyphBuffer& aBuffer,
                               const Matrix& aTransform,
                               const IntPoint& aQuantizeScale);

 private:
  // The glyph keys used to render the text run.
  GlyphBuffer mBuffer = {nullptr, 0};
  // The color of the text run.
  DeviceColor mColor;
  // The full bounds of the text run without any clipping applied.
  IntRect mFullBounds;
  // Stroke options for the text run.
  UniquePtr<StoredStrokeOptions> mStrokeOptions;
};

// GlyphCache maintains a list of GlyphCacheEntry's representing previously
// rendered text runs. The cache is searched to see if a given incoming text
// run has already been rendered to a texture, and if so, just reuses it.
// Otherwise, the text run will be rendered to a new texture handle and
// inserted into a new GlyphCacheEntry to represent it.
class GlyphCache : public LinkedListElement<GlyphCache>,
                   public CacheImpl<GlyphCacheEntry, false> {
 public:
  explicit GlyphCache(ScaledFont* aFont);

  ScaledFont* GetFont() const { return mFont; }

  already_AddRefed<GlyphCacheEntry> FindEntry(const GlyphBuffer& aBuffer,
                                              const DeviceColor& aColor,
                                              const Matrix& aTransform,
                                              const IntPoint& aQuantizeScale,
                                              const IntRect& aClipRect,
                                              HashNumber aHash,
                                              const StrokeOptions* aOptions);

  already_AddRefed<GlyphCacheEntry> InsertEntry(
      const GlyphBuffer& aBuffer, const DeviceColor& aColor,
      const Matrix& aTransform, const IntPoint& aQuantizeScale,
      const IntRect& aBounds, const IntRect& aFullBounds, HashNumber aHash,
      const StrokeOptions* aOptions);

  bool IsWhitespace(const GlyphBuffer& aBuffer) const;
  void SetLastWhitespace(const GlyphBuffer& aBuffer);

 private:
  // Weak pointer to the owning font
  ScaledFont* mFont;
  // The last whitespace queried from this cache
  Maybe<uint32_t> mLastWhitespace;
};

struct QuantizedPath {
  explicit QuantizedPath(const WGR::Path& aPath);
  // Ensure the path can only be moved, but not copied.
  QuantizedPath(QuantizedPath&&) noexcept;
  QuantizedPath(const QuantizedPath&) = delete;
  ~QuantizedPath();

  bool operator==(const QuantizedPath&) const;

  WGR::Path mPath;
};

struct PathVertexRange {
  uint32_t mOffset;
  uint32_t mLength;

  PathVertexRange() : mOffset(0), mLength(0) {}
  PathVertexRange(uint32_t aOffset, uint32_t aLength)
      : mOffset(aOffset), mLength(aLength) {}

  bool IsValid() const { return mLength > 0; }
};

enum class AAStrokeMode {
  Unsupported,
  Geometry,
  Mask,
};

// PathCacheEntry stores a rasterized version of a supplied path with a given
// pattern.
class PathCacheEntry : public CacheEntryImpl<PathCacheEntry> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(PathCacheEntry, override)

  PathCacheEntry(QuantizedPath&& aPath, Pattern* aPattern,
                 StoredStrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
                 const Matrix& aTransform, const IntRect& aBounds,
                 const Point& aOrigin, HashNumber aHash, float aSigma = -1.0f);

  bool MatchesPath(const QuantizedPath& aPath, const Pattern* aPattern,
                   const StrokeOptions* aStrokeOptions,
                   AAStrokeMode aStrokeMode, const Matrix& aTransform,
                   const IntRect& aBounds, const Point& aOrigin,
                   HashNumber aHash, float aSigma);

  static HashNumber HashPath(const QuantizedPath& aPath,
                             const Pattern* aPattern, const Matrix& aTransform,
                             const IntRect& aBounds, const Point& aOrigin);

  const QuantizedPath& GetPath() const { return mPath; }

  const Point& GetOrigin() const { return mOrigin; }

  // Valid if either a mask (no pattern) or there is valid pattern.
  bool IsValid() const override { return !mPattern || mPattern->IsValid(); }

  const PathVertexRange& GetVertexRange() const { return mVertexRange; }
  void SetVertexRange(const PathVertexRange& aRange) { mVertexRange = aRange; }

  const WeakPtr<TextureHandle>& GetSecondaryHandle() const {
    return mSecondaryHandle;
  }
  void SetSecondaryHandle(WeakPtr<TextureHandle> aHandle) {
    mSecondaryHandle = std::move(aHandle);
  }

 private:
  // The actual path geometry supplied
  QuantizedPath mPath;
  // The transformed origin of the path
  Point mOrigin;
  // The pattern used to rasterize the path, if not a mask
  UniquePtr<Pattern> mPattern;
  // The StrokeOptions used for stroked paths, if applicable
  UniquePtr<StoredStrokeOptions> mStrokeOptions;
  // The AAStroke mode used for rendering a stroked path.
  AAStrokeMode mAAStrokeMode = AAStrokeMode::Unsupported;
  // The shadow blur sigma
  float mSigma;
  // If the path has cached geometry in the vertex buffer.
  PathVertexRange mVertexRange;
  // Secondary texture handle that is not linked and only weakly referenced.
  WeakPtr<TextureHandle> mSecondaryHandle;
};

class PathCache : public CacheImpl<PathCacheEntry, true> {
 public:
  PathCache() = default;

  already_AddRefed<PathCacheEntry> FindOrInsertEntry(
      QuantizedPath aPath, const Pattern* aPattern,
      const StrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
      const Matrix& aTransform, const IntRect& aBounds, const Point& aOrigin,
      float aSigma = -1.0f);

  already_AddRefed<PathCacheEntry> FindEntry(
      const QuantizedPath& aPath, const Pattern* aPattern,
      const StrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
      const Matrix& aTransform, const IntRect& aBounds, const Point& aOrigin,
      float aSigma = -1.0f, bool aHasSecondaryHandle = false);

  void ClearVertexRanges();
};

}  // namespace mozilla::gfx

#endif  // MOZILLA_GFX_DRAWTARGETWEBGL_INTERNAL_H
