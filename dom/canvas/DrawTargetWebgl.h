/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWTARGETWEBGL_H
#define MOZILLA_GFX_DRAWTARGETWEBGL_H

#include <deque>
#include <memory>
#include <vector>

#include "GLTypes.h"
#include "mozilla/Array.h"
#include "mozilla/LinkedList.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PathSkia.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/layers/LayersTypes.h"

namespace WGR {
struct OutputVertex;
struct PathBuilder;
}  // namespace WGR

namespace mozilla {

class WebGLContext;
class WebGLBuffer;
class WebGLFramebuffer;
class WebGLProgram;
class WebGLRenderbuffer;
class WebGLTexture;
class WebGLUniformLocation;
class WebGLVertexArray;

namespace gl {
class GLContext;
class SharedSurface;
}  // namespace gl

namespace layers {
class RemoteTextureOwnerClient;
}  // namespace layers

namespace gfx {

class DataSourceSurface;
class DrawTargetSkia;
class DrawTargetWebgl;
class FilterNodeWebgl;
class PathSkia;
class SourceSurfaceSkia;
class SourceSurfaceWebgl;

class BackingTexture;
class TextureHandle;
class SharedTexture;
class SharedTextureHandle;
class StandaloneTexture;
class GlyphCache;
class PathCache;
class PathCacheEntry;
struct PathVertexRange;
enum class AAStrokeMode;

// SharedContextWebgl stores most of the actual WebGL state that may be used by
// any number of DrawTargetWebgl's that use it. Foremost, it holds the actual
// WebGL client context, programs, and buffers for mapping to WebGL.
// Secondarily, it holds shared caches for surfaces, glyphs, paths, and
// shadows so that each DrawTargetWebgl does not require its own cache. It is
// important that SetTarget is called to install the current DrawTargetWebgl
// before actually using the SharedContext, as the individual framebuffers
// and viewport are still maintained in DrawTargetWebgl itself.
class SharedContextWebgl : public mozilla::RefCounted<SharedContextWebgl>,
                           public mozilla::SupportsWeakPtr {
  friend class DrawTargetWebgl;
  friend class FilterNodeWebgl;
  friend class SourceSurfaceWebgl;
  friend class TextureHandle;
  friend class SharedTextureHandle;
  friend class StandaloneTexture;

 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(SharedContextWebgl)

  static already_AddRefed<SharedContextWebgl> Create();

  ~SharedContextWebgl();

  gl::GLContext* GetGLContext();

  void EnterTlsScope();
  void ExitTlsScope();

  bool IsContextLost() const;

  void OnMemoryPressure();

  void ClearCaches();

  std::shared_ptr<gl::SharedSurface> ExportSharedSurface(
      layers::TextureType aTextureType, SourceSurface* aSurface);

  already_AddRefed<SourceSurface> ImportSurfaceDescriptor(
      DrawTargetWebgl* aDT, const layers::SurfaceDescriptor& aDesc,
      const gfx::IntSize& aSize, SurfaceFormat aFormat);

 private:
  SharedContextWebgl();

  WeakPtr<DrawTargetWebgl> mCurrentTarget;
  RefPtr<TextureHandle> mTargetHandle;
  IntSize mViewportSize;
  // The current integer-aligned scissor rect.
  IntRect mClipRect;
  // The current fractional AA'd clip rect bounds.
  Rect mClipAARect;

  RefPtr<WebGLContext> mWebgl;

  // Avoid spurious state changes by caching last used state.
  RefPtr<WebGLProgram> mLastProgram;
  RefPtr<WebGLTexture> mLastTexture;
  RefPtr<WebGLTexture> mLastClipMask;

  // WebGL shader resources
  RefPtr<WebGLBuffer> mPathVertexBuffer;
  RefPtr<WebGLVertexArray> mPathVertexArray;
  // The current insertion offset into the GPU path buffer.
  uint32_t mPathVertexOffset = 0;
  // The maximum size of the GPU path buffer.
  uint32_t mPathVertexCapacity = 0;
  // The maximum supported type complexity of a GPU path.
  uint32_t mPathMaxComplexity = 0;
  // Whether to accelerate stroked paths with AAStroke.
  bool mPathAAStroke = true;
  // Whether to accelerate stroked paths with WGR.
  bool mPathWGRStroke = false;

  WGR::PathBuilder* mWGRPathBuilder = nullptr;
  // Temporary buffer for generating WGR output into.
  UniquePtr<WGR::OutputVertex[]> mWGROutputBuffer;

  RefPtr<WebGLProgram> mSolidProgram;
  Maybe<uint32_t> mSolidProgramViewport;
  Maybe<uint32_t> mSolidProgramAA;
  Maybe<uint32_t> mSolidProgramTransform;
  Maybe<uint32_t> mSolidProgramColor;
  Maybe<uint32_t> mSolidProgramClipMask;
  Maybe<uint32_t> mSolidProgramClipBounds;
  RefPtr<WebGLProgram> mImageProgram;
  Maybe<uint32_t> mImageProgramViewport;
  Maybe<uint32_t> mImageProgramAA;
  Maybe<uint32_t> mImageProgramTransform;
  Maybe<uint32_t> mImageProgramTexMatrix;
  Maybe<uint32_t> mImageProgramTexBounds;
  Maybe<uint32_t> mImageProgramColor;
  Maybe<uint32_t> mImageProgramSwizzle;
  Maybe<uint32_t> mImageProgramSampler;
  Maybe<uint32_t> mImageProgramClipMask;
  Maybe<uint32_t> mImageProgramClipBounds;
  RefPtr<WebGLProgram> mBlurProgram;
  Maybe<uint32_t> mBlurProgramViewport;
  Maybe<uint32_t> mBlurProgramTransform;
  Maybe<uint32_t> mBlurProgramTexMatrix;
  Maybe<uint32_t> mBlurProgramTexBounds;
  Maybe<uint32_t> mBlurProgramOffsetScale;
  Maybe<uint32_t> mBlurProgramSigma;
  Maybe<uint32_t> mBlurProgramColor;
  Maybe<uint32_t> mBlurProgramSwizzle;
  Maybe<uint32_t> mBlurProgramSampler;
  Maybe<uint32_t> mBlurProgramClipMask;
  Maybe<uint32_t> mBlurProgramClipBounds;
  RefPtr<WebGLProgram> mFilterProgram;
  Maybe<uint32_t> mFilterProgramViewport;
  Maybe<uint32_t> mFilterProgramTransform;
  Maybe<uint32_t> mFilterProgramTexMatrix;
  Maybe<uint32_t> mFilterProgramTexBounds;
  Maybe<uint32_t> mFilterProgramColorMatrix;
  Maybe<uint32_t> mFilterProgramColorOffset;
  Maybe<uint32_t> mFilterProgramSampler;
  Maybe<uint32_t> mFilterProgramClipMask;
  Maybe<uint32_t> mFilterProgramClipBounds;

  struct SolidProgramUniformState {
    Maybe<Array<float, 2>> mViewport;
    Maybe<Array<float, 1>> mAA;
    Maybe<Array<float, 6>> mTransform;
    Maybe<Array<float, 4>> mColor;
    Maybe<Array<float, 4>> mClipBounds;
  } mSolidProgramUniformState;

  struct ImageProgramUniformState {
    Maybe<Array<float, 2>> mViewport;
    Maybe<Array<float, 1>> mAA;
    Maybe<Array<float, 6>> mTransform;
    Maybe<Array<float, 6>> mTexMatrix;
    Maybe<Array<float, 4>> mTexBounds;
    Maybe<Array<float, 4>> mColor;
    Maybe<Array<float, 1>> mSwizzle;
    Maybe<Array<float, 4>> mClipBounds;
  } mImageProgramUniformState;

  struct BlurProgramUniformState {
    Maybe<Array<float, 2>> mViewport;
    Maybe<Array<float, 4>> mTransform;
    Maybe<Array<float, 4>> mTexMatrix;
    Maybe<Array<float, 4>> mTexBounds;
    Maybe<Array<float, 2>> mOffsetScale;
    Maybe<Array<float, 1>> mSigma;
    Maybe<Array<float, 4>> mColor;
    Maybe<Array<float, 1>> mSwizzle;
    Maybe<Array<float, 4>> mClipBounds;
  } mBlurProgramUniformState;

  struct FilterProgramUniformState {
    Maybe<Array<float, 2>> mViewport;
    Maybe<Array<float, 4>> mTransform;
    Maybe<Array<float, 4>> mTexMatrix;
    Maybe<Array<float, 4>> mTexBounds;
    Maybe<Array<float, 16>> mColorMatrix;
    Maybe<Array<float, 4>> mColorOffset;
    Maybe<Array<float, 4>> mClipBounds;
  } mFilterProgramUniformState;

  // Scratch framebuffer used to wrap textures for miscellaneous utility ops.
  RefPtr<WebGLFramebuffer> mScratchFramebuffer;
  // Scratch framebuffer used to wrap textures for sub-targets.
  RefPtr<WebGLFramebuffer> mTargetFramebuffer;
  // Scratch framebuffer used to wrap textures for export.
  RefPtr<WebGLFramebuffer> mExportFramebuffer;
  // Buffer filled with zero data for initializing textures.
  RefPtr<WebGLBuffer> mZeroBuffer;
  size_t mZeroSize = 0;
  // 1x1 texture with solid white mask for disabling clipping
  RefPtr<WebGLTexture> mNoClipMask;

  uint32_t mMaxTextureSize = 0;
  bool mRasterizationTruncates = false;

  // The current blending operation.
  CompositionOp mLastCompositionOp = CompositionOp::OP_SOURCE;
  // The constant blend color used for the blending operation.
  Maybe<DeviceColor> mLastBlendColor;
  // The blend stage of the current blending operation.
  uint8_t mLastBlendStage = 0;

  // The cached scissor state. Operations that rely on scissor state should
  // take care to enable or disable the cached scissor state as necessary.
  bool mScissorEnabled = false;
  IntRect mLastScissor = {-1, -1, -1, -1};

  // A most-recently-used list of allocated texture handles.
  LinkedList<RefPtr<TextureHandle>> mTextureHandles;
  size_t mNumTextureHandles = 0;
  // User data key linking a SourceSurface with its TextureHandle.
  UserDataKey mTextureHandleKey = {0};
  // User data key linking a ScaledFont with its GlyphCache.
  UserDataKey mGlyphCacheKey = {0};
  // List of all GlyphCaches currently allocated to fonts.
  LinkedList<GlyphCache> mGlyphCaches;
  // Cache of rasterized paths.
  UniquePtr<PathCache> mPathCache;
  // Collection of allocated shared texture pages that may be shared amongst
  // many handles.
  std::vector<RefPtr<SharedTexture>> mSharedTextures;
  // Collection of allocated standalone textures that have a single assigned
  // handle.
  std::vector<RefPtr<StandaloneTexture>> mStandaloneTextures;
  size_t mUsedTextureMemory = 0;
  size_t mTotalTextureMemory = 0;
  // The total reserved memory for empty texture pages that are kept around
  // for future allocations.
  size_t mEmptyTextureMemory = 0;
  // A memory pressure event may signal from another thread that caches should
  // be cleared if possible.
  Atomic<bool> mShouldClearCaches;
  // The total number of DrawTargetWebgls using this shared context.
  size_t mDrawTargetCount = 0;
  // Whether we are inside a scoped usage of TLS MakeCurrent, and what previous
  // value to restore it to when exiting the scope.
  Maybe<bool> mTlsScope;

  // Cached unit circle path
  RefPtr<Path> mUnitCirclePath;

  // The total bytes used by pending snapshot PBOs.
  size_t mUsedSnapshotPBOMemory = 0;
  // The owning surfaces initiating snapshot PBO readbacks.
  std::deque<ThreadSafeWeakPtr<SourceSurfaceWebgl>> mSnapshotPBOs;

  bool Initialize();
  bool CreateShaders();
  void ResetPathVertexBuffer();

  void BlendFunc(GLenum aSrcFactor, GLenum aDstFactor);
  void SetBlendState(CompositionOp aOp,
                     const Maybe<DeviceColor>& aColor = Nothing(),
                     uint8_t aStage = 0);
  uint8_t RequiresMultiStageBlend(const DrawOptions& aOptions,
                                  DrawTargetWebgl* aDT = nullptr);

  void SetClipRect(const Rect& aClipRect);
  void SetClipRect(const IntRect& aClipRect) { SetClipRect(Rect(aClipRect)); }
  bool SetClipMask(const RefPtr<WebGLTexture>& aTex);
  bool SetNoClipMask();
  bool HasClipMask() const {
    return mLastClipMask && mLastClipMask != mNoClipMask;
  }

  Maybe<uint32_t> GetUniformLocation(const RefPtr<WebGLProgram>& prog,
                                     const std::string& aName) const;

  template <class T, size_t N>
  void UniformData(GLenum aFuncElemType, const Maybe<uint32_t>& aLoc,
                   const Array<T, N>& aData);

  // Avoids redundant UniformData calls by caching the previously set value.
  template <class T, size_t N>
  void MaybeUniformData(GLenum aFuncElemType, const Maybe<uint32_t>& aLoc,
                        const Array<T, N>& aData, Maybe<Array<T, N>>& aCached);

  bool IsCurrentTarget(DrawTargetWebgl* aDT) const {
    return aDT == mCurrentTarget;
  }
  bool SetTarget(DrawTargetWebgl* aDT,
                 const RefPtr<TextureHandle>& aHandle = nullptr,
                 const IntSize& aViewportSize = IntSize());
  void RestoreCurrentTarget(const RefPtr<WebGLTexture>& aClipMask = nullptr);

  // Reset the current target.
  void ClearTarget() { mCurrentTarget = nullptr; }
  // Reset the last used texture to force binding next use.
  void ClearLastTexture(bool aFullClear = false);

  bool SupportsPattern(const Pattern& aPattern);

  void EnableScissor(const IntRect& aRect, bool aForce = false);
  void DisableScissor(bool aForce = false);

  void SetTexFilter(WebGLTexture* aTex, bool aFilter);
  void InitTexParameters(WebGLTexture* aTex, bool aFilter = true);

  bool ReadInto(uint8_t* aDstData, int32_t aDstStride, SurfaceFormat aFormat,
                const IntRect& aBounds, TextureHandle* aHandle = nullptr,
                const RefPtr<WebGLBuffer>& aBuffer = nullptr);
  already_AddRefed<DataSourceSurface> ReadSnapshot(
      TextureHandle* aHandle = nullptr, uint8_t* aData = nullptr,
      int32_t aStride = 0);
  already_AddRefed<TextureHandle> WrapSnapshot(const IntSize& aSize,
                                               SurfaceFormat aFormat,
                                               RefPtr<WebGLTexture> aTex);
  already_AddRefed<TextureHandle> CopySnapshot(
      const IntRect& aRect, TextureHandle* aHandle = nullptr);

  already_AddRefed<WebGLBuffer> ReadSnapshotIntoPBO(
      SourceSurfaceWebgl* aOwner, TextureHandle* aHandle = nullptr);
  already_AddRefed<DataSourceSurface> ReadSnapshotFromPBO(
      const RefPtr<WebGLBuffer>& aBuffer, SurfaceFormat aFormat,
      const IntSize& aSize, uint8_t* aData = nullptr, int32_t aStride = 0);
  void RemoveSnapshotPBO(SourceSurfaceWebgl* aOwner,
                         already_AddRefed<WebGLBuffer> aBuffer);
  void ClearSnapshotPBOs(size_t aMaxMemory = 0);

  already_AddRefed<WebGLTexture> GetCompatibleSnapshot(
      SourceSurface* aSurface, RefPtr<TextureHandle>* aHandle = nullptr,
      bool aCheckTarget = true) const;
  bool IsCompatibleSurface(SourceSurface* aSurface) const;

  bool UploadSurface(DataSourceSurface* aData, SurfaceFormat aFormat,
                     const IntRect& aSrcRect, const IntPoint& aDstOffset,
                     bool aInit, bool aZero = false,
                     const RefPtr<WebGLTexture>& aTex = nullptr);
  void UploadSurfaceToHandle(const RefPtr<DataSourceSurface>& aData,
                             const IntPoint& aSrcOffset,
                             const RefPtr<TextureHandle>& aHandle);
  void BindAndInitRenderTex(const RefPtr<WebGLTexture>& aTex,
                            SurfaceFormat aFormat, const IntSize& aSize);
  void InitRenderTex(BackingTexture* aBacking);
  void ClearRenderTex(BackingTexture* aBacking);
  void BindScratchFramebuffer(TextureHandle* aHandle, bool aInit,
                              const IntSize& aViewportSize = IntSize());
  already_AddRefed<TextureHandle> AllocateTextureHandle(
      DrawTargetWebgl* aDT, SurfaceFormat aFormat, const IntSize& aSize,
      bool aAllowShared = true, bool aRenderable = false,
      const WebGLTexture* aAvoid = nullptr);
  void DrawQuad();
  void DrawTriangles(const PathVertexRange& aRange);
  bool DrawRectAccel(const Rect& aRect, const Pattern& aPattern,
                     const DrawOptions& aOptions,
                     Maybe<DeviceColor> aMaskColor = Nothing(),
                     RefPtr<TextureHandle>* aHandle = nullptr,
                     bool aTransformed = true, bool aClipped = true,
                     bool aAccelOnly = false, bool aForceUpdate = false,
                     const StrokeOptions* aStrokeOptions = nullptr,
                     const PathVertexRange* aVertexRange = nullptr,
                     const Matrix* aRectXform = nullptr,
                     uint8_t aBlendStage = 0);

  already_AddRefed<WebGLTexture> GetFilterInputTexture(
      const RefPtr<SourceSurface>& aSurface, const IntRect& aSourceRect,
      RefPtr<TextureHandle>* aHandle, IntPoint& aOffset, SurfaceFormat& aFormat,
      IntRect& aBounds, IntSize& aBackingSize);
  bool FilterRect(const Rect& aDestRect, const Matrix5x4& aColorMatrix,
                  const RefPtr<SourceSurface>& aSurface,
                  const IntRect& aSourceRect, const DrawOptions& aOptions,
                  RefPtr<TextureHandle>* aHandle,
                  RefPtr<TextureHandle>* aTargetHandle = nullptr);
  bool BlurRectPass(const Rect& aDestRect, const Point& aSigma,
                    bool aHorizontal, const RefPtr<SourceSurface>& aSurface,
                    const IntRect& aSourceRect,
                    const DrawOptions& aOptions = DrawOptions(),
                    Maybe<DeviceColor> aMaskColor = Nothing(),
                    RefPtr<TextureHandle>* aHandle = nullptr,
                    RefPtr<TextureHandle>* aTargetHandle = nullptr,
                    bool aFilter = false);
  bool BlurRectAccel(const Rect& aDestRect, const Point& aSigma,
                     const RefPtr<SourceSurface>& aSurface,
                     const IntRect& aSourceRect,
                     const DrawOptions& aOptions = DrawOptions(),
                     Maybe<DeviceColor> aMaskColor = Nothing(),
                     RefPtr<TextureHandle>* aHandle = nullptr,
                     RefPtr<TextureHandle>* aTargetHandle = nullptr,
                     RefPtr<TextureHandle>* aResultHandle = nullptr,
                     bool aFilter = false);
  already_AddRefed<SourceSurface> DownscaleBlurInput(SourceSurface* aSurface,
                                                     const IntRect& aSourceRect,
                                                     int aIters = 1);

  already_AddRefed<TextureHandle> DrawStrokeMask(
      const PathVertexRange& aVertexRange, const IntSize& aSize);
  bool DrawWGRPath(const Path* aPath, const IntRect& aIntBounds,
                   const Rect& aQuantBounds, const Matrix& aPathXform,
                   RefPtr<PathCacheEntry>& aEntry, const DrawOptions& aOptions,
                   const StrokeOptions* aStrokeOptions,
                   AAStrokeMode aAAStrokeMode, const Pattern& aPattern,
                   const Maybe<DeviceColor>& aColor);
  bool DrawPathAccel(const Path* aPath, const Pattern& aPattern,
                     const DrawOptions& aOptions,
                     const StrokeOptions* aStrokeOptions = nullptr,
                     bool aAllowStrokeAlpha = false,
                     const ShadowOptions* aShadow = nullptr,
                     bool aCacheable = true,
                     const Matrix* aPathXform = nullptr);

  bool DrawCircleAccel(const Point& aCenter, float aRadius,
                       const Pattern& aPattern, const DrawOptions& aOptions,
                       const StrokeOptions* aStrokeOptions = nullptr);

  bool DrawGlyphsAccel(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                       const Pattern& aPattern, const DrawOptions& aOptions,
                       const StrokeOptions* aStrokeOptions,
                       bool aUseSubpixelAA);

  already_AddRefed<TextureHandle> ResolveFilterInputAccel(
      DrawTargetWebgl* aDT, const Path* aPath, const Pattern& aPattern,
      const IntRect& aSourceRect, const Matrix& aDestTransform,
      const DrawOptions& aOptions = DrawOptions(),
      const StrokeOptions* aStrokeOptions = nullptr,
      SurfaceFormat aFormat = SurfaceFormat::B8G8R8A8);

  void PruneTextureHandle(const RefPtr<TextureHandle>& aHandle);
  bool PruneTextureMemory(size_t aMargin = 0, bool aPruneUnused = true);

  bool RemoveSharedTexture(const RefPtr<SharedTexture>& aTexture);
  bool RemoveStandaloneTexture(const RefPtr<StandaloneTexture>& aTexture);

  void UnlinkSurfaceTextures(bool aForce = false);
  void UnlinkSurfaceTexture(const RefPtr<TextureHandle>& aHandle,
                            bool aForce = false);
  void UnlinkGlyphCaches();

  void AddHeapData(const void* aBuf);
  void RemoveHeapData(const void* aBuf);
  void AddUntrackedTextureMemory(size_t aBytes);
  void RemoveUntrackedTextureMemory(size_t aBytes);
  template <typename T>
  void AddUntrackedTextureMemory(const RefPtr<T>& aObject, size_t aBytes = 0);
  template <typename T>
  void RemoveUntrackedTextureMemory(const RefPtr<T>& aObject,
                                    size_t aBytes = 0);
  void AddTextureMemory(BackingTexture* aTexture);
  void RemoveTextureMemory(BackingTexture* aTexture);

  void ClearZeroBuffer();
  void ClearAllTextures();
  void ClearEmptyTextureMemory();
  void ClearCachesIfNecessary();

  void CachePrefs();
};

// DrawTargetWebgl implements a subset of the DrawTarget API suitable for use
// by CanvasRenderingContext2D. It maps these to a client WebGL context so that
// they can be accelerated where possible by WebGL. It manages both routing to
// appropriate shaders and texture allocation/caching for surfaces. For commands
// that are not feasible to accelerate with WebGL, it mirrors state to a backup
// DrawTargetSkia that can be used as a fallback software renderer. Multiple
// instances of DrawTargetWebgl within a process will actually share a single
// WebGL context so that data can be more easily interchanged between them and
// also to enable more reasonable limiting of resource usage.
class DrawTargetWebgl : public DrawTarget, public SupportsWeakPtr {
  friend class FilterNodeWebgl;
  friend class FilterNodeDeferInputWebgl;
  friend class SourceSurfaceWebgl;
  friend class SharedContextWebgl;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawTargetWebgl, override)

 private:
  IntSize mSize;
  RefPtr<WebGLFramebuffer> mFramebuffer;
  RefPtr<WebGLTexture> mTex;
  RefPtr<WebGLTexture> mClipMask;
  // The integer-aligned, scissor-compatible conservative bounds of the clip.
  IntRect mClipBounds;
  // The fractional, AA'd bounds of the clip rect, if applicable.
  Rect mClipAARect;
  RefPtr<DrawTargetSkia> mSkia;
  // Skia DT pointing to the same pixel data, but without any applied clips.
  RefPtr<DrawTargetSkia> mSkiaNoClip;
  // A Shmem read-only handle to the same memory as mShmem, to be consumed by
  // TakeShmemHandle().
  mozilla::ipc::ReadOnlySharedMemoryHandle mShmemHandle;
  // The Shmem backing the Skia DT, if applicable.
  mozilla::ipc::SharedMemoryMapping mShmem;
  // The currently cached snapshot of the WebGL context
  RefPtr<SourceSurfaceWebgl> mSnapshot;
  // Whether the framebuffer is still in the initially clear state.
  bool mIsClear = true;
  // Whether or not the Skia target has valid contents and is being drawn to
  bool mSkiaValid = false;
  // Whether or not Skia layering over the WebGL context is enabled
  bool mSkiaLayer = false;
  // Whether the WebGL target was clear when the Skia layer was established.
  bool mSkiaLayerClear = false;
  // Whether or not the WebGL context has valid contents and is being drawn to
  bool mWebglValid = true;
  // Whether or not the clip state has changed since last used by
  // SharedContextWebgl.
  bool mClipChanged = true;
  // Whether or not the clip state needs to be refreshed. Sometimes the clip
  // state may be overwritten and require a refresh later, even though it has
  // not changed.
  bool mRefreshClipState = true;
  // The number of layers currently pushed.
  int32_t mLayerDepth = 0;

  RefPtr<TextureHandle> mSnapshotTexture;

  // Store a log of clips currently pushed so that they can be used to init
  // the clip state of temporary DTs.
  struct ClipStack {
    Matrix mTransform;
    Rect mRect;
    RefPtr<const Path> mPath;

    bool operator==(const ClipStack& aOther) const;
  };

  std::vector<ClipStack> mClipStack;

  // The previous state of the clip stack when a mask was generated.
  std::vector<ClipStack> mCachedClipStack;

  // UsageProfile stores per-frame counters for significant profiling events
  // that assist in determining whether acceleration should still be used for
  // a Canvas2D user.
  struct UsageProfile {
    uint32_t mFailedFrames = 0;
    uint32_t mFrameCount = 0;
    uint32_t mCacheMisses = 0;
    uint32_t mCacheHits = 0;
    uint32_t mUncachedDraws = 0;
    uint32_t mLayers = 0;
    uint32_t mReadbacks = 0;
    uint32_t mFallbacks = 0;

    void BeginFrame();
    void EndFrame();
    bool RequiresRefresh() const;

    void OnCacheMiss() { ++mCacheMisses; }
    void OnCacheHit() { ++mCacheHits; }
    void OnUncachedDraw() { ++mUncachedDraws; }
    void OnLayer() { ++mLayers; }
    void OnReadback() { ++mReadbacks; }
    void OnFallback() { ++mFallbacks; }
  };

  UsageProfile mProfile;

  RefPtr<SharedContextWebgl> mSharedContext;
  dom::ContentParentId mContentId;

 public:
  DrawTargetWebgl();
  ~DrawTargetWebgl();

  static bool CanCreate(const IntSize& aSize, SurfaceFormat aFormat);
  static already_AddRefed<DrawTargetWebgl> Create(
      const IntSize& aSize, SurfaceFormat aFormat,
      const RefPtr<SharedContextWebgl>& aSharedContext,
      const dom::ContentParentId& aContentId);

  bool Init(const IntSize& aSize, SurfaceFormat aFormat,
            const RefPtr<SharedContextWebgl>& aSharedContext,
            const dom::ContentParentId& aContentId);

  bool IsValid() const override;

  DrawTargetType GetType() const override {
    return DrawTargetType::HARDWARE_RASTER;
  }
  BackendType GetBackendType() const override { return BackendType::WEBGL; }
  BackendType GetPathType() const override { return BackendType::SKIA; }
  IntSize GetSize() const override { return mSize; }
  const RefPtr<SharedContextWebgl>& GetSharedContext() const {
    return mSharedContext;
  }
  const dom::ContentParentId& GetContentId() const { return mContentId; }

  bool HasDataSnapshot() const;
  bool EnsureDataSnapshot();
  void PrepareShmem();
  already_AddRefed<SourceSurface> GetDataSnapshot();
  already_AddRefed<SourceSurface> Snapshot() override;
  already_AddRefed<SourceSurface> GetOptimizedSnapshot(DrawTarget* aTarget);
  already_AddRefed<SourceSurface> GetBackingSurface() override;
  void DetachAllSnapshots() override;

  void BeginFrame(bool aInvalidContents = false);
  void EndFrame();
  bool RequiresRefresh() const { return mProfile.RequiresRefresh(); }

  bool LockBits(uint8_t** aData, IntSize* aSize, int32_t* aStride,
                SurfaceFormat* aFormat, IntPoint* aOrigin = nullptr) override;
  void ReleaseBits(uint8_t* aData) override;

  void Flush() override {}
  void DrawSurface(
      SourceSurface* aSurface, const Rect& aDest, const Rect& aSource,
      const DrawSurfaceOptions& aSurfOptions = DrawSurfaceOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;
  void DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                  const Point& aDestPoint,
                  const DrawOptions& aOptions = DrawOptions()) override;
  void DrawSurfaceWithShadow(SourceSurface* aSurface, const Point& aDest,
                             const ShadowOptions& aShadow,
                             CompositionOp aOperator) override;
  void DrawShadow(const Path* aPath, const Pattern& aPattern,
                  const ShadowOptions& aShadow, const DrawOptions& aOptions,
                  const StrokeOptions* aStrokeOptions = nullptr) override;

  void ClearRect(const Rect& aRect) override;
  void CopySurface(SourceSurface* aSurface, const IntRect& aSourceRect,
                   const IntPoint& aDestination) override;
  void FillRect(const Rect& aRect, const Pattern& aPattern,
                const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeRect(const Rect& aRect, const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;
  bool StrokeLineAccel(const Point& aStart, const Point& aEnd,
                       const Pattern& aPattern,
                       const StrokeOptions& aStrokeOptions,
                       const DrawOptions& aOptions, bool aClosed = false);
  void StrokeLine(const Point& aStart, const Point& aEnd,
                  const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;
  void Stroke(const Path* aPath, const Pattern& aPattern,
              const StrokeOptions& aStrokeOptions = StrokeOptions(),
              const DrawOptions& aOptions = DrawOptions()) override;
  void Fill(const Path* aPath, const Pattern& aPattern,
            const DrawOptions& aOptions = DrawOptions()) override;
  void FillCircle(const Point& aOrigin, float aRadius, const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeCircle(const Point& aOrigin, float aRadius,
                    const Pattern& aPattern,
                    const StrokeOptions& aStrokeOptions = StrokeOptions(),
                    const DrawOptions& aOptions = DrawOptions()) override;

  void SetPermitSubpixelAA(bool aPermitSubpixelAA) override;
  void FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                  const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                    const Pattern& aPattern,
                    const StrokeOptions& aStrokeOptions = StrokeOptions(),
                    const DrawOptions& aOptions = DrawOptions()) override;
  void Mask(const Pattern& aSource, const Pattern& aMask,
            const DrawOptions& aOptions = DrawOptions()) override;
  void MaskSurface(const Pattern& aSource, SourceSurface* aMask, Point aOffset,
                   const DrawOptions& aOptions = DrawOptions()) override;
  bool Draw3DTransformedSurface(SourceSurface* aSurface,
                                const Matrix4x4& aMatrix) override;
  void PushClip(const Path* aPath) override;
  void PushClipRect(const Rect& aRect) override;
  void PushDeviceSpaceClipRects(const IntRect* aRects,
                                uint32_t aCount) override;
  void PopClip() override;
  bool RemoveAllClips() override;
  bool CopyToFallback(DrawTarget* aDT);
  void PushLayer(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                 const Matrix& aMaskTransform,
                 const IntRect& aBounds = IntRect(),
                 bool aCopyBackground = false) override;
  void PushLayerWithBlend(
      bool aOpaque, Float aOpacity, SourceSurface* aMask,
      const Matrix& aMaskTransform, const IntRect& aBounds = IntRect(),
      bool aCopyBackground = false,
      CompositionOp aCompositionOp = CompositionOp::OP_OVER) override;
  void PopLayer() override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromData(
      unsigned char* aData, const IntSize& aSize, int32_t aStride,
      SurfaceFormat aFormat) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurface(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurfaceForUnknownAlpha(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromNativeSurface(
      const NativeSurface& aSurface) const override;
  already_AddRefed<DrawTarget> CreateSimilarDrawTarget(
      const IntSize& aSize, SurfaceFormat aFormat) const override;
  bool CanCreateSimilarDrawTarget(const IntSize& aSize,
                                  SurfaceFormat aFormat) const override;
  RefPtr<DrawTarget> CreateClippedDrawTarget(const Rect& aBounds,
                                             SurfaceFormat aFormat) override;

  already_AddRefed<PathBuilder> CreatePathBuilder(
      FillRule aFillRule = FillRule::FILL_WINDING) const override;
  already_AddRefed<GradientStops> CreateGradientStops(
      GradientStop* aStops, uint32_t aNumStops,
      ExtendMode aExtendMode = ExtendMode::CLAMP) const override;
  already_AddRefed<FilterNode> CreateFilter(FilterType aType) override;
  already_AddRefed<FilterNode> DeferFilterInput(
      const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
      const IntPoint& aDestOffset, const DrawOptions& aOptions = DrawOptions(),
      const StrokeOptions* aStrokeOptions = nullptr) override;

  already_AddRefed<SourceSurfaceWebgl> ResolveFilterInputAccel(
      const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
      const Matrix& aDestTransform, const DrawOptions& aOptions = DrawOptions(),
      const StrokeOptions* aStrokeOptions = nullptr,
      SurfaceFormat aFormat = SurfaceFormat::B8G8R8A8);

  bool FilterSurface(const Matrix5x4& aColorMatrix, SourceSurface* aSurface,
                     const IntRect& aSourceRect, const Point& aDest,
                     const DrawOptions& aOptions = DrawOptions());
  bool BlurSurface(float aSigma, SourceSurface* aSurface,
                   const IntRect& aSourceRect, const Point& aDest,
                   const DrawOptions& aOptions = DrawOptions(),
                   const DeviceColor& aColor = DeviceColor(1, 1, 1, 1));

  void SetTransform(const Matrix& aTransform) override;
  void* GetNativeSurface(NativeSurfaceType aType) override;

  bool CopyToSwapChain(
      layers::TextureType aTextureType, layers::RemoteTextureId aId,
      layers::RemoteTextureOwnerId aOwnerId,
      layers::RemoteTextureOwnerClient* aOwnerClient = nullptr);

  already_AddRefed<SourceSurface> ImportSurfaceDescriptor(
      const layers::SurfaceDescriptor& aDesc, const gfx::IntSize& aSize,
      SurfaceFormat aFormat) override;

  void OnMemoryPressure() { mSharedContext->OnMemoryPressure(); }

  operator std::string() const {
    std::stringstream stream;
    stream << "DrawTargetWebgl(" << this << ")";
    return stream.str();
  }

  mozilla::ipc::ReadOnlySharedMemoryHandle TakeShmemHandle() {
    return std::move(mShmemHandle);
  }

 private:
  bool SupportsPattern(const Pattern& aPattern) {
    return mSharedContext->SupportsPattern(aPattern);
  }

  bool SupportsDrawOptions(const DrawOptions& aOptions,
                           const Rect& aRect = Rect());

  Maybe<Rect> ComputeSimpleClipRect() const;
  bool SetSimpleClipRect();
  bool GenerateComplexClipMask();
  bool PrepareContext(bool aClipped = true,
                      const RefPtr<TextureHandle>& aHandle = nullptr,
                      const IntSize& aViewportSize = IntSize());
  bool ShouldClip();

  void DrawRectFallback(const Rect& aRect, const Pattern& aPattern,
                        const DrawOptions& aOptions,
                        Maybe<DeviceColor> aMaskColor = Nothing(),
                        bool aTransform = true, bool aClipped = true,
                        const StrokeOptions* aStrokeOptions = nullptr);
  bool DrawRect(const Rect& aRect, const Pattern& aPattern,
                const DrawOptions& aOptions,
                Maybe<DeviceColor> aMaskColor = Nothing(),
                RefPtr<TextureHandle>* aHandle = nullptr,
                bool aTransformed = true, bool aClipped = true,
                bool aAccelOnly = false, bool aForceUpdate = false,
                const StrokeOptions* aStrokeOptions = nullptr);
  Maybe<SurfacePattern> LinearGradientToSurface(const RectDouble& aBounds,
                                                const Pattern& aPattern);

  ColorPattern GetClearPattern() const;

  template <typename R>
  RectDouble TransformDouble(const R& aRect) const;

  Maybe<Rect> RectClippedToViewport(const RectDouble& aRect) const;

  bool ShouldAccelPath(const DrawOptions& aOptions,
                       const StrokeOptions* aStrokeOptions,
                       const Rect& aRect = Rect());
  void DrawPath(const Path* aPath, const Pattern& aPattern,
                const DrawOptions& aOptions,
                const StrokeOptions* aStrokeOptions = nullptr,
                bool aAllowStrokeAlpha = false);
  void DrawCircle(const Point& aOrigin, float aRadius, const Pattern& aPattern,
                  const DrawOptions& aOptions,
                  const StrokeOptions* aStrokeOptions = nullptr);

  bool MarkChanged();

  bool ReadIntoSkia();
  void FlattenSkia();
  bool PrepareSkia();
  bool FlushFromSkia();

  void MarkSkiaChanged(bool aOverwrite = false);
  void MarkSkiaChanged(const DrawOptions& aOptions);

  bool ShouldUseSubpixelAA(ScaledFont* aFont, const DrawOptions& aOptions);

  bool ReadInto(uint8_t* aDstData, int32_t aDstStride);
  already_AddRefed<DataSourceSurface> ReadSnapshot(uint8_t* aData = nullptr,
                                                   int32_t aStride = 0);
  already_AddRefed<WebGLBuffer> ReadSnapshotIntoPBO(SourceSurfaceWebgl* aOwner);
  already_AddRefed<TextureHandle> CopySnapshot(const IntRect& aRect);
  already_AddRefed<TextureHandle> CopySnapshot() {
    return CopySnapshot(GetRect());
  }

  void ClearSnapshot(bool aCopyOnWrite = true, bool aNeedHandle = false);

  bool CreateFramebuffer();

  void DrawFilterFallback(FilterNode* aNode, const Rect& aSourceRect,
                          const Point& aDestPoint,
                          const DrawOptions& aOptions = DrawOptions());

  // PrepareContext may sometimes be used recursively. When this occurs, ensure
  // that clip state is restored after the context is used.
  struct AutoRestoreContext {
    DrawTargetWebgl* mTarget;
    Rect mClipAARect;
    RefPtr<WebGLTexture> mLastClipMask;

    explicit AutoRestoreContext(DrawTargetWebgl* aTarget);

    ~AutoRestoreContext();
  };
};

}  // namespace gfx
}  // namespace mozilla

#endif  // MOZILLA_GFX_DRAWTARGETWEBGL_H
