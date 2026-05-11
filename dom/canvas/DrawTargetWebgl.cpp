/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetWebglInternal.h"
#include "FilterNodeWebgl.h"
#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "SharedSurface.h"
#include "SourceSurfaceWebgl.h"
#include "WebGL2Context.h"
#include "WebGLBuffer.h"
#include "WebGLChild.h"
#include "WebGLContext.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"
#include "WebGLVertexArray.h"
#include "gfxPlatform.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/AAStroke.h"
#include "mozilla/gfx/Blur.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/DrawTargetSkia.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/gfx/HelpersSkia.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/PathSkia.h"
#include "mozilla/gfx/Scale.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "nsContentUtils.h"
#include "nsIMemoryReporter.h"
#include "skia/include/core/SkPixmap.h"

#ifdef XP_MACOSX
#  include "mozilla/gfx/ScaledFontMac.h"
#endif

namespace mozilla::gfx {

static Atomic<size_t> gReportedTextureMemory;
static Atomic<size_t> gReportedHeapData;
static Atomic<size_t> gReportedContextCount;
static Atomic<size_t> gReportedTargetCount;

class AcceleratedCanvas2DMemoryReporter final : public nsIMemoryReporter {
  ~AcceleratedCanvas2DMemoryReporter() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  MOZ_DEFINE_MALLOC_SIZE_OF_ON_ALLOC(MallocSizeOfOnAlloc)
  MOZ_DEFINE_MALLOC_SIZE_OF_ON_FREE(MallocSizeOfOnFree)

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("ac2d-texture-memory", KIND_OTHER, UNITS_BYTES,
                       gReportedTextureMemory,
                       "GPU memory used by Accelerated Canvas2D textures.");
    MOZ_COLLECT_REPORT("explicit/ac2d/heap-resources", KIND_HEAP, UNITS_BYTES,
                       gReportedHeapData,
                       "Heap overhead for Accelerated Canvas2D resources.");
    MOZ_COLLECT_REPORT("ac2d-context-count", KIND_OTHER, UNITS_COUNT,
                       gReportedContextCount,
                       "Number of Accelerated Canvas2D contexts.");
    MOZ_COLLECT_REPORT("ac2d-target-count", KIND_OTHER, UNITS_COUNT,
                       gReportedTargetCount,
                       "Number of Accelerated Canvas2D targets.");
    return NS_OK;
  }

  static void Register() {
    static bool registered = false;
    if (!registered) {
      registered = true;
      RegisterStrongMemoryReporter(
          MakeAndAddRef<AcceleratedCanvas2DMemoryReporter>());
    }
  }
};

NS_IMPL_ISUPPORTS(AcceleratedCanvas2DMemoryReporter, nsIMemoryReporter)

BackingTexture::BackingTexture(const IntSize& aSize, SurfaceFormat aFormat,
                               const RefPtr<WebGLTexture>& aTexture)
    : mSize(aSize), mFormat(aFormat), mTexture(aTexture) {}

#ifdef XP_WIN
// Work around buggy ANGLE/D3D drivers that may copy blocks of pixels outside
// the row length. Extra space is reserved at the end of each row up to stride
// alignment. This does not affect standalone textures.
static const Etagere::AllocatorOptions kR8AllocatorOptions = {16, 1, 1, 0};
#endif

SharedTexture::SharedTexture(const IntSize& aSize, SurfaceFormat aFormat,
                             const RefPtr<WebGLTexture>& aTexture)
    : BackingTexture(aSize, aFormat, aTexture),
      mAtlasAllocator(
#ifdef XP_WIN
          aFormat == SurfaceFormat::A8
              ? Etagere::etagere_atlas_allocator_with_options(
                    aSize.width, aSize.height, &kR8AllocatorOptions)
              :
#endif
              Etagere::etagere_atlas_allocator_new(aSize.width, aSize.height)) {
}

SharedTexture::~SharedTexture() {
  if (mAtlasAllocator) {
    Etagere::etagere_atlas_allocator_delete(mAtlasAllocator);
    mAtlasAllocator = nullptr;
  }
}

SharedTextureHandle::SharedTextureHandle(Etagere::AllocationId aId,
                                         const IntRect& aBounds,
                                         SharedTexture* aTexture)
    : mAllocationId(aId), mBounds(aBounds), mTexture(aTexture) {}

already_AddRefed<SharedTextureHandle> SharedTexture::Allocate(
    const IntSize& aSize) {
  Etagere::Allocation alloc = {{0, 0, 0, 0}, Etagere::INVALID_ALLOCATION_ID};
  if (!mAtlasAllocator ||
      !Etagere::etagere_atlas_allocator_allocate(mAtlasAllocator, aSize.width,
                                                 aSize.height, &alloc) ||
      alloc.id == Etagere::INVALID_ALLOCATION_ID) {
    return nullptr;
  }
  RefPtr<SharedTextureHandle> handle = new SharedTextureHandle(
      alloc.id,
      IntRect(IntPoint(alloc.rectangle.min_x, alloc.rectangle.min_y), aSize),
      this);
  return handle.forget();
}

bool SharedTexture::Free(SharedTextureHandle& aHandle) {
  if (aHandle.mTexture != this) {
    return false;
  }
  if (aHandle.mAllocationId != Etagere::INVALID_ALLOCATION_ID) {
    if (mAtlasAllocator) {
      Etagere::etagere_atlas_allocator_deallocate(mAtlasAllocator,
                                                  aHandle.mAllocationId);
    }
    aHandle.mAllocationId = Etagere::INVALID_ALLOCATION_ID;
  }
  return true;
}

StandaloneTexture::StandaloneTexture(const IntSize& aSize,
                                     SurfaceFormat aFormat,
                                     const RefPtr<WebGLTexture>& aTexture)
    : BackingTexture(aSize, aFormat, aTexture) {}

DrawTargetWebgl::DrawTargetWebgl() = default;

inline void SharedContextWebgl::ClearLastTexture(bool aFullClear) {
  mLastTexture = nullptr;
  if (aFullClear) {
    mLastClipMask = nullptr;
  }
}

// Attempts to clear the snapshot state. If the snapshot is only referenced by
// this target, then it should simply be destroyed. If it is a WebGL surface in
// use by something else, then special cleanup such as reusing the texture or
// copy-on-write may be possible.
void DrawTargetWebgl::ClearSnapshot(bool aCopyOnWrite, bool aNeedHandle) {
  if (!mSnapshot) {
    return;
  }
  mSharedContext->ClearLastTexture();
  RefPtr<SourceSurfaceWebgl> snapshot = mSnapshot.forget();
  if (snapshot->hasOneRef()) {
    return;
  }
  if (aCopyOnWrite) {
    // WebGL snapshots must be notified that the framebuffer contents will be
    // changing so that it can copy the data.
    snapshot->DrawTargetWillChange(aNeedHandle);
  } else {
    // If not copying, then give the backing texture to the surface for reuse.
    snapshot->GiveTexture(
        mSharedContext->WrapSnapshot(GetSize(), GetFormat(), mTex.forget()));
  }
}

DrawTargetWebgl::~DrawTargetWebgl() {
  ClearSnapshot(false);
  if (mSharedContext) {
    // Force any Skia snapshots to copy the shmem before it deallocs.
    if (mSkia) {
      mSkia->DetachAllSnapshots();
    }
    mSharedContext->ClearLastTexture(true);
    if (mClipMask) {
      mSharedContext->RemoveUntrackedTextureMemory(mClipMask);
      mClipMask = nullptr;
    }
    mFramebuffer = nullptr;
    if (mTex) {
      mSharedContext->RemoveUntrackedTextureMemory(mTex);
      mTex = nullptr;
    }
    mSharedContext->mDrawTargetCount--;
    gReportedTargetCount--;
  }
}

SharedContextWebgl::SharedContextWebgl() = default;

SharedContextWebgl::~SharedContextWebgl() {
  // Detect context loss before deletion.
  if (mWebgl) {
    ExitTlsScope();
    mWebgl->ActiveTexture(0);
    gReportedContextCount--;
  }
  if (mWGRPathBuilder) {
    WGR::wgr_builder_release(mWGRPathBuilder);
    mWGRPathBuilder = nullptr;
  }
  if (mWGROutputBuffer) {
    RemoveHeapData(mWGROutputBuffer.get());
    mWGROutputBuffer = nullptr;
  }
  if (mPathVertexBuffer) {
    RemoveUntrackedTextureMemory(mPathVertexBuffer);
    mPathVertexBuffer = nullptr;
  }
  ClearZeroBuffer();
  ClearAllTextures();
  UnlinkSurfaceTextures(true);
  UnlinkGlyphCaches();
  ClearSnapshotPBOs();
}

gl::GLContext* SharedContextWebgl::GetGLContext() {
  return mWebgl ? mWebgl->GL() : nullptr;
}

void SharedContextWebgl::EnterTlsScope() {
  if (mTlsScope.isSome()) {
    return;
  }
  if (gl::GLContext* gl = GetGLContext()) {
    mTlsScope = Some(gl->mUseTLSIsCurrent);
    gl::GLContext::InvalidateCurrentContext();
    gl->mUseTLSIsCurrent = true;
  }
}

void SharedContextWebgl::ExitTlsScope() {
  if (mTlsScope.isNothing()) {
    return;
  }
  if (gl::GLContext* gl = GetGLContext()) {
    gl->mUseTLSIsCurrent = mTlsScope.value();
  }
  mTlsScope = Nothing();
}

// Remove any SourceSurface user data associated with this TextureHandle.
inline void SharedContextWebgl::UnlinkSurfaceTexture(
    const RefPtr<TextureHandle>& aHandle, bool aForce) {
  if (RefPtr<SourceSurface> surface = aHandle->GetSurface()) {
    // Ensure any WebGL snapshot textures get unlinked.
    if (surface->GetType() == SurfaceType::WEBGL) {
      static_cast<SourceSurfaceWebgl*>(surface.get())
          ->OnUnlinkTexture(this, aHandle, aForce);
    }
    surface->RemoveUserData(&mTextureHandleKey);
  }
}

// Unlinks TextureHandles from any SourceSurface user data.
void SharedContextWebgl::UnlinkSurfaceTextures(bool aForce) {
  for (RefPtr<TextureHandle> handle = mTextureHandles.getFirst(); handle;
       handle = handle->getNext()) {
    UnlinkSurfaceTexture(handle, aForce);
  }
}

// Unlinks GlyphCaches from any ScaledFont user data.
void SharedContextWebgl::UnlinkGlyphCaches() {
  GlyphCache* cache = mGlyphCaches.getFirst();
  while (cache) {
    ScaledFont* font = cache->GetFont();
    // Access the next cache before removing the user data, as it might destroy
    // the cache.
    cache = cache->getNext();
    font->RemoveUserData(&mGlyphCacheKey);
  }
}

void SharedContextWebgl::OnMemoryPressure() { mShouldClearCaches = true; }

void SharedContextWebgl::ClearCaches() {
  OnMemoryPressure();
  ClearCachesIfNecessary();
}

// Clear out the entire list of texture handles from any source.
void SharedContextWebgl::ClearAllTextures() {
  while (!mTextureHandles.isEmpty()) {
    PruneTextureHandle(mTextureHandles.popLast());
    --mNumTextureHandles;
  }
}

static inline size_t TextureMemoryUsage(WebGLTexture* aTexture) {
  return aTexture->MemoryUsage();
}

static inline size_t TextureMemoryUsage(WebGLBuffer* aBuffer) {
  return aBuffer->ByteLength();
}

inline void SharedContextWebgl::AddHeapData(const void* aBuf) {
  if (aBuf) {
    gReportedHeapData +=
        AcceleratedCanvas2DMemoryReporter::MallocSizeOfOnAlloc(aBuf);
  }
}

inline void SharedContextWebgl::RemoveHeapData(const void* aBuf) {
  if (aBuf) {
    gReportedHeapData -=
        AcceleratedCanvas2DMemoryReporter::MallocSizeOfOnFree(aBuf);
  }
}

inline void SharedContextWebgl::AddUntrackedTextureMemory(size_t aBytes) {
  gReportedTextureMemory += aBytes;
}

inline void SharedContextWebgl::RemoveUntrackedTextureMemory(size_t aBytes) {
  gReportedTextureMemory -= aBytes;
}

template <typename T>
inline void SharedContextWebgl::AddUntrackedTextureMemory(
    const RefPtr<T>& aObject, size_t aBytes) {
  size_t usedBytes = aBytes > 0 ? aBytes : TextureMemoryUsage(aObject);
  AddUntrackedTextureMemory(usedBytes);
  gReportedHeapData += aObject->SizeOfIncludingThis(
      AcceleratedCanvas2DMemoryReporter::MallocSizeOfOnAlloc);
}

template <typename T>
inline void SharedContextWebgl::RemoveUntrackedTextureMemory(
    const RefPtr<T>& aObject, size_t aBytes) {
  size_t usedBytes = aBytes > 0 ? aBytes : TextureMemoryUsage(aObject);
  RemoveUntrackedTextureMemory(usedBytes);
  gReportedHeapData -= aObject->SizeOfIncludingThis(
      AcceleratedCanvas2DMemoryReporter::MallocSizeOfOnFree);
}

inline void SharedContextWebgl::AddTextureMemory(BackingTexture* aTexture) {
  size_t usedBytes = aTexture->UsedBytes();
  mTotalTextureMemory += usedBytes;
  AddUntrackedTextureMemory(aTexture->GetWebGLTexture(), usedBytes);
}

inline void SharedContextWebgl::RemoveTextureMemory(BackingTexture* aTexture) {
  size_t usedBytes = aTexture->UsedBytes();
  mTotalTextureMemory -= usedBytes;
  RemoveUntrackedTextureMemory(aTexture->GetWebGLTexture(), usedBytes);
}

// Scan through the shared texture pages looking for any that are empty and
// delete them.
void SharedContextWebgl::ClearEmptyTextureMemory() {
  for (auto pos = mSharedTextures.begin(); pos != mSharedTextures.end();) {
    if (!(*pos)->HasAllocatedHandles()) {
      RefPtr<SharedTexture> shared = *pos;
      mEmptyTextureMemory -= shared->UsedBytes();
      RemoveTextureMemory(shared);
      pos = mSharedTextures.erase(pos);
    } else {
      ++pos;
    }
  }
}

void SharedContextWebgl::ClearZeroBuffer() {
  if (mZeroBuffer) {
    RemoveUntrackedTextureMemory(mZeroBuffer);
    mZeroBuffer = nullptr;
  }
}

// If there is a request to clear out the caches because of memory pressure,
// then first clear out all the texture handles in the texture cache. If there
// are still empty texture pages being kept around, then clear those too.
void SharedContextWebgl::ClearCachesIfNecessary() {
  if (!mShouldClearCaches.exchange(false)) {
    return;
  }
  ClearZeroBuffer();
  ClearAllTextures();
  if (mEmptyTextureMemory) {
    ClearEmptyTextureMemory();
  }
  ClearLastTexture();
  ClearSnapshotPBOs();
}

// Try to initialize a new WebGL context. Verifies that the requested size does
// not exceed the available texture limits and that shader creation succeeded.
bool DrawTargetWebgl::Init(const IntSize& size, const SurfaceFormat format,
                           const RefPtr<SharedContextWebgl>& aSharedContext) {
  switch (format) {
    case SurfaceFormat::B8G8R8A8:
    case SurfaceFormat::B8G8R8X8:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported format for DrawTargetWebgl.");
      return false;
  }

  mSize = size;
  mFormat = format;

  if (!aSharedContext || aSharedContext->IsContextLost() ||
      aSharedContext->mDrawTargetCount >=
          StaticPrefs::gfx_canvas_accelerated_max_draw_target_count()) {
    return false;
  }
  mSharedContext = aSharedContext;
  mSharedContext->mDrawTargetCount++;
  gReportedTargetCount++;

  if (size_t(std::max(size.width, size.height)) >
      mSharedContext->mMaxTextureSize) {
    return false;
  }

  if (!CreateFramebuffer()) {
    return false;
  }

  Maybe<size_t> byteSize = layers::ImageDataSerializer::ComputeRGBBufferSize(
      mSize, SurfaceFormat::B8G8R8A8);
  if (byteSize.isNothing()) {
    return false;
  }

  size_t shmemSize =
      mozilla::ipc::shared_memory::PageAlignedSize(byteSize.value());
  if (NS_WARN_IF(shmemSize > UINT32_MAX)) {
    MOZ_ASSERT_UNREACHABLE("Buffer too big?");
    return false;
  }

  auto handle = mozilla::ipc::shared_memory::Create(shmemSize);
  if (NS_WARN_IF(!handle)) {
    return false;
  }
  auto mapping = handle.Map();
  if (NS_WARN_IF(!mapping)) {
    return false;
  }

  mShmemHandle = std::move(handle).ToReadOnly();
  mShmem = std::move(mapping);

  mSkia = new DrawTargetSkia;
  auto stride = layers::ImageDataSerializer::ComputeRGBStride(
      SurfaceFormat::B8G8R8A8, size.width);
  if (NS_WARN_IF(stride.isNothing())) {
    return false;
  }

  if (!mSkia->Init(mShmem.DataAs<uint8_t>(), size, stride.value(),
                   SurfaceFormat::B8G8R8A8, true)) {
    return false;
  }

  // Allocate an unclipped copy of the DT pointing to its data.
  uint8_t* dtData = nullptr;
  IntSize dtSize;
  int32_t dtStride = 0;
  SurfaceFormat dtFormat = SurfaceFormat::UNKNOWN;
  if (!mSkia->LockBits(&dtData, &dtSize, &dtStride, &dtFormat)) {
    return false;
  }
  mSkiaNoClip = new DrawTargetSkia;
  if (!mSkiaNoClip->Init(dtData, dtSize, dtStride, dtFormat, true)) {
    mSkia->ReleaseBits(dtData);
    return false;
  }
  mSkia->ReleaseBits(dtData);

  SetPermitSubpixelAA(IsOpaque(format));
  return true;
}

// If a non-recoverable error occurred that would stop the canvas from initing.
static Atomic<bool> sContextInitError(false);

already_AddRefed<SharedContextWebgl> SharedContextWebgl::Create() {
  // If context initialization would fail, don't even try to create a context.
  if (sContextInitError) {
    return nullptr;
  }
  RefPtr<SharedContextWebgl> sharedContext = new SharedContextWebgl;
  if (!sharedContext->Initialize()) {
    return nullptr;
  }
  return sharedContext.forget();
}

bool SharedContextWebgl::Initialize() {
  AcceleratedCanvas2DMemoryReporter::Register();

  WebGLContextOptions options = {};
  options.alpha = true;
  options.depth = false;
  options.stencil = false;
  options.antialias = false;
  options.preserveDrawingBuffer = true;
  options.failIfMajorPerformanceCaveat = false;

  const bool resistFingerprinting = nsContentUtils::ShouldResistFingerprinting(
      "Fallback", RFPTarget::WebGLRenderCapability);
  const auto initDesc = webgl::InitContextDesc{
      .isWebgl2 = true,
      .resistFingerprinting = resistFingerprinting,
      .principalKey = 0,
      .size = {1, 1},
      .options = options,
  };

  webgl::InitContextResult initResult;
  mWebgl = WebGLContext::Create(nullptr, initDesc, &initResult);
  if (!mWebgl) {
    // There was a non-recoverable error when trying to create a host context.
    sContextInitError = true;
    mWebgl = nullptr;
    return false;
  }
  if (mWebgl->IsContextLost()) {
    mWebgl = nullptr;
    return false;
  }

  mMaxTextureSize = initResult.limits.maxTex2dSize;

  if (kIsMacOS) {
    mRasterizationTruncates = initResult.vendor == gl::GLVendor::ATI;
  }

  CachePrefs();

  if (!CreateShaders()) {
    // There was a non-recoverable error when trying to init shaders.
    sContextInitError = true;
    mWebgl = nullptr;
    return false;
  }

  mWGRPathBuilder = WGR::wgr_new_builder();

  gReportedContextCount++;

  return true;
}

inline void SharedContextWebgl::BlendFunc(GLenum aSrcFactor,
                                          GLenum aDstFactor) {
  mWebgl->BlendFuncSeparate({}, aSrcFactor, aDstFactor, aSrcFactor, aDstFactor);
}

void SharedContextWebgl::SetBlendState(CompositionOp aOp,
                                       const Maybe<DeviceColor>& aColor,
                                       uint8_t aStage) {
  if (aOp == mLastCompositionOp && mLastBlendColor == aColor &&
      mLastBlendStage == aStage) {
    return;
  }
  mLastCompositionOp = aOp;
  mLastBlendColor = aColor;
  mLastBlendStage = aStage;
  // AA is not supported for all composition ops, so switching blend modes may
  // cause a toggle in AA state. Certain ops such as OP_SOURCE require output
  // alpha that is blended separately from AA coverage. This would require two
  // stage blending which can incur a substantial performance penalty, so to
  // work around this currently we just disable AA for those ops.

  // Map the composition op to a WebGL blend mode, if possible.
  bool enabled = true;
  switch (aOp) {
    case CompositionOp::OP_OVER:
      if (aColor) {
        // If a color is supplied, then we blend subpixel text.
        mWebgl->BlendColor(aColor->b, aColor->g, aColor->r, 1.0f);
        BlendFunc(LOCAL_GL_CONSTANT_COLOR, LOCAL_GL_ONE_MINUS_SRC_COLOR);
      } else {
        BlendFunc(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA);
      }
      break;
    case CompositionOp::OP_DEST_OVER:
      BlendFunc(LOCAL_GL_ONE_MINUS_DST_ALPHA, LOCAL_GL_ONE);
      break;
    case CompositionOp::OP_ADD:
      BlendFunc(LOCAL_GL_ONE, LOCAL_GL_ONE);
      break;
    case CompositionOp::OP_DEST_OUT:
      BlendFunc(LOCAL_GL_ZERO, LOCAL_GL_ONE_MINUS_SRC_ALPHA);
      break;
    case CompositionOp::OP_ATOP:
      BlendFunc(LOCAL_GL_DST_ALPHA, LOCAL_GL_ONE_MINUS_SRC_ALPHA);
      break;
    case CompositionOp::OP_SOURCE:
      if (aColor) {
        // If a color is supplied, then we assume there is clipping or AA. This
        // requires that we still use an over blend func with the clip/AA alpha,
        // while filling the interior with the unaltered color. Normally this
        // would require dual source blending, but we can emulate it with only
        // a blend color.
        mWebgl->BlendColor(aColor->b, aColor->g, aColor->r, aColor->a);
        BlendFunc(LOCAL_GL_CONSTANT_COLOR, LOCAL_GL_ONE_MINUS_SRC_COLOR);
      } else {
        enabled = false;
      }
      break;
    case CompositionOp::OP_CLEAR:
      // Assume the source is an alpha mask for clearing. Be careful to blend in
      // the correct alpha if the target is opaque.
      mWebgl->BlendFuncSeparate(
          {}, LOCAL_GL_ZERO, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
          IsOpaque(mCurrentTarget->GetFormat()) ? LOCAL_GL_ONE : LOCAL_GL_ZERO,
          LOCAL_GL_ONE_MINUS_SRC_ALPHA);
      break;
    case CompositionOp::OP_MULTIPLY:
      switch (aStage) {
        // Single stage, assume dest is opaque alpha.
        case 0:
          BlendFunc(LOCAL_GL_DST_COLOR, LOCAL_GL_ONE_MINUS_SRC_ALPHA);
          break;
        // Multi-stage, decompose into [Cs*(1 - Ad)] + [Cd*(1 - As) + Cs*Cd]
        case 1:
          BlendFunc(LOCAL_GL_DST_COLOR, LOCAL_GL_ONE_MINUS_SRC_ALPHA);
          break;
        case 2:
          BlendFunc(LOCAL_GL_ONE_MINUS_DST_ALPHA, LOCAL_GL_ONE);
          break;
      }
      break;
    case CompositionOp::OP_SCREEN:
      BlendFunc(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_COLOR);
      break;
    case CompositionOp::OP_IN:  // unbounded
      BlendFunc(LOCAL_GL_DST_ALPHA, LOCAL_GL_ZERO);
      break;
    case CompositionOp::OP_OUT:  // unbounded
      BlendFunc(LOCAL_GL_ONE_MINUS_DST_ALPHA, LOCAL_GL_ZERO);
      break;
    case CompositionOp::OP_DEST_IN:  // unbounded
      BlendFunc(LOCAL_GL_ZERO, LOCAL_GL_SRC_ALPHA);
      break;
    case CompositionOp::OP_DEST_ATOP:  // unbounded
      BlendFunc(LOCAL_GL_ONE_MINUS_DST_ALPHA, LOCAL_GL_SRC_ALPHA);
      break;
    default:
      enabled = false;
      break;
  }

  mWebgl->SetEnabled(LOCAL_GL_BLEND, {}, enabled);
}

// Ensure the WebGL framebuffer is set to the current target.
bool SharedContextWebgl::SetTarget(DrawTargetWebgl* aDT,
                                   const RefPtr<TextureHandle>& aHandle,
                                   const IntSize& aViewportSize) {
  if (!mWebgl || mWebgl->IsContextLost()) {
    return false;
  }
  if (aDT != mCurrentTarget || mTargetHandle != aHandle) {
    mCurrentTarget = aDT;
    mTargetHandle = aHandle;
    IntRect bounds;
    if (aHandle) {
      if (!mTargetFramebuffer) {
        mTargetFramebuffer = mWebgl->CreateFramebuffer();
      }
      mWebgl->BindFramebuffer(LOCAL_GL_FRAMEBUFFER, mTargetFramebuffer);

      webgl::FbAttachInfo attachInfo;
      attachInfo.tex = aHandle->GetBackingTexture()->GetWebGLTexture();
      mWebgl->FramebufferAttach(LOCAL_GL_FRAMEBUFFER,
                                LOCAL_GL_COLOR_ATTACHMENT0, LOCAL_GL_TEXTURE_2D,
                                attachInfo);

      bounds = aHandle->GetBounds();
    } else if (aDT) {
      mWebgl->BindFramebuffer(LOCAL_GL_FRAMEBUFFER, aDT->mFramebuffer);
      bounds = aDT->GetRect();
    }
    mViewportSize = !aViewportSize.IsEmpty() ? Min(aViewportSize, bounds.Size())
                                             : bounds.Size();
    mWebgl->Viewport(bounds.x, bounds.y, mViewportSize.width,
                     mViewportSize.height);
  }
  return true;
}

// Replace the current clip rect with a new potentially-AA'd clip rect.
void SharedContextWebgl::SetClipRect(const Rect& aClipRect) {
  // Only invalidate the clip rect if it actually changes.
  if (!mClipAARect.IsEqualEdges(aClipRect)) {
    mClipAARect = aClipRect;
    // Store the integer-aligned bounds.
    mClipRect = RoundedOut(aClipRect);
  }
}

bool SharedContextWebgl::SetClipMask(const RefPtr<WebGLTexture>& aTex) {
  if (mLastClipMask != aTex) {
    if (!mWebgl) {
      return false;
    }
    mWebgl->ActiveTexture(1);
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, aTex);
    mWebgl->ActiveTexture(0);
    mLastClipMask = aTex;
  }
  return true;
}

bool SharedContextWebgl::SetNoClipMask() {
  if (mNoClipMask) {
    return SetClipMask(mNoClipMask);
  }
  if (!mWebgl) {
    return false;
  }
  mNoClipMask = mWebgl->CreateTexture();
  if (!mNoClipMask) {
    return false;
  }
  mWebgl->ActiveTexture(1);
  mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, mNoClipMask);
  static const auto solidMask =
      std::array<const uint8_t, 4>{0xFF, 0xFF, 0xFF, 0xFF};
  mWebgl->TexImage(0, LOCAL_GL_RGBA8, {0, 0, 0},
                   {LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE},
                   {LOCAL_GL_TEXTURE_2D,
                    {1, 1, 1},
                    gfxAlphaType::NonPremult,
                    Some(Span{solidMask})});
  InitTexParameters(mNoClipMask, false);
  mWebgl->ActiveTexture(0);
  mLastClipMask = mNoClipMask;
  return true;
}

inline bool DrawTargetWebgl::ClipStack::operator==(
    const DrawTargetWebgl::ClipStack& aOther) const {
  // Verify the transform and bounds match.
  if (!mTransform.FuzzyEquals(aOther.mTransform) ||
      !mRect.IsEqualInterior(aOther.mRect)) {
    return false;
  }
  // Verify the paths match.
  if (!mPath) {
    return !aOther.mPath;
  }
  if (!aOther.mPath ||
      mPath->GetBackendType() != aOther.mPath->GetBackendType()) {
    return false;
  }
  if (mPath->GetBackendType() != BackendType::SKIA) {
    return mPath == aOther.mPath;
  }
  return static_cast<const PathSkia*>(mPath.get())->GetPath() ==
         static_cast<const PathSkia*>(aOther.mPath.get())->GetPath();
}

// If the clip region can't be approximated by a simple clip rect, then we need
// to generate a clip mask that can represent the clip region per-pixel. We
// render to the Skia target temporarily, transparent outside the clip region,
// opaque inside, and upload this to a texture that can be used by the shaders.
bool DrawTargetWebgl::GenerateComplexClipMask() {
  if (!mClipChanged || (mClipMask && mCachedClipStack == mClipStack)) {
    mClipChanged = false;
    // If the clip mask was already generated, use the cached mask and bounds.
    mSharedContext->SetClipMask(mClipMask);
    mSharedContext->SetClipRect(mClipBounds);
    return true;
  }
  if (!mWebglValid) {
    // If the Skia target is currently being used, then we can't render the mask
    // in it.
    return false;
  }
  RefPtr<WebGLContext> webgl = mSharedContext->mWebgl;
  if (!webgl) {
    return false;
  }
  bool init = false;
  if (!mClipMask) {
    mClipMask = webgl->CreateTexture();
    if (!mClipMask) {
      return false;
    }
    init = true;
  }
  // Try to get the bounds of the clip to limit the size of the mask.
  if (Maybe<IntRect> clip = mSkia->GetDeviceClipRect(true)) {
    mClipBounds = *clip;
  } else {
    // If we can't get bounds, then just use the entire viewport.
    mClipBounds = GetRect();
  }
  mClipAARect = Rect(mClipBounds);
  // If initializing the clip mask, then allocate the entire texture to ensure
  // all pixels get filled with an empty mask regardless. Otherwise, restrict
  // uploading to only the clip region.
  RefPtr<DrawTargetSkia> dt = new DrawTargetSkia;
  if (!dt->Init(mClipBounds.Size(), SurfaceFormat::A8)) {
    if (init) {
      // Ensure that the clip mask is reinitialized on the next attempt.
      mClipMask = nullptr;
    }
    return false;
  }
  // Set the clip region and fill the entire inside of it
  // with opaque white.
  mCachedClipStack.clear();
  for (auto& clipStack : mClipStack) {
    // Record the current state of the clip stack for this mask.
    mCachedClipStack.push_back(clipStack);
    dt->SetTransform(
        Matrix(clipStack.mTransform).PostTranslate(-mClipBounds.TopLeft()));
    if (clipStack.mPath) {
      dt->PushClip(clipStack.mPath);
    } else {
      dt->PushClipRect(clipStack.mRect);
    }
  }
  dt->SetTransform(Matrix::Translation(-mClipBounds.TopLeft()));
  dt->FillRect(Rect(mClipBounds), ColorPattern(DeviceColor(1, 1, 1, 1)));
  // Bind the clip mask for uploading. This is done on texture unit 0 so that
  // we can work around an Windows Intel driver bug. If done on texture unit 1,
  // the driver doesn't notice that the texture contents was modified. Force a
  // re-latch by binding the texture on texture unit 1 only after modification.
  webgl->BindTexture(LOCAL_GL_TEXTURE_2D, mClipMask);
  if (init) {
    mSharedContext->InitTexParameters(mClipMask, false);
  }
  RefPtr<DataSourceSurface> data;
  if (RefPtr<SourceSurface> snapshot = dt->Snapshot()) {
    data = snapshot->GetDataSurface();
  }
  // Finally, upload the texture data and initialize texture storage if
  // necessary.
  if (init && mClipBounds.Size() != mSize) {
    mSharedContext->UploadSurface(nullptr, SurfaceFormat::A8, GetRect(),
                                  IntPoint(), true, true);
    init = false;
  }
  mSharedContext->UploadSurface(data, SurfaceFormat::A8,
                                IntRect(IntPoint(), mClipBounds.Size()),
                                mClipBounds.TopLeft(), init);
  mSharedContext->ClearLastTexture();
  // Bind the new clip mask to the clip sampler on texture unit 1.
  mSharedContext->SetClipMask(mClipMask);
  mSharedContext->SetClipRect(mClipBounds);
  // We uploaded a surface, just as if we missed the texture cache, so account
  // for that here.
  if (init) {
    mSharedContext->AddUntrackedTextureMemory(mClipMask);
  }
  mProfile.OnCacheMiss();
  return !!data;
}

Maybe<Rect> DrawTargetWebgl::ComputeSimpleClipRect() const {
  // Determine whether the clipping rectangle is simple enough to accelerate.
  // Check if there is a device space clip rectangle available from the Skia
  // target.
  if (Maybe<IntRect> clip = mSkia->GetDeviceClipRect(false)) {
    // If the clip is empty, leave the final integer clip rectangle empty to
    // trivially discard the draw request.
    // If the clip rect is larger than the viewport, just set it to the
    // viewport.
    if (!clip->IsEmpty() && clip->Contains(GetRect())) {
      clip = Some(GetRect());
    }
    return Some(Rect(*clip));
  }

  // There was no pixel-aligned clip rect available, so check the clip stack to
  // see if there is an AA'd axis-aligned rectangle clip.
  Rect rect(GetRect());
  for (auto& clipStack : mClipStack) {
    // If clip is a path or it has a non-axis-aligned transform, then it is
    // complex.
    if (clipStack.mPath ||
        !clipStack.mTransform.PreservesAxisAlignedRectangles()) {
      return Nothing();
    }
    // Transform the rect and intersect it with the current clip.
    rect =
        clipStack.mTransform.TransformBounds(clipStack.mRect).Intersect(rect);
  }
  return Some(rect);
}

bool DrawTargetWebgl::SetSimpleClipRect() {
  if (Maybe<Rect> rect = ComputeSimpleClipRect()) {
    mSharedContext->SetClipRect(*rect);
    mSharedContext->SetNoClipMask();
    return true;
  }
  return false;
}

// Installs the Skia clip rectangle, if applicable, onto the shared WebGL
// context as well as sets the WebGL framebuffer to the current target.
bool DrawTargetWebgl::PrepareContext(bool aClipped,
                                     const RefPtr<TextureHandle>& aHandle,
                                     const IntSize& aViewportSize) {
  if (!aClipped || aHandle) {
    // If no clipping requested, just set the clip rect to the viewport.
    mSharedContext->SetClipRect(
        aHandle
            ? IntRect(IntPoint(), !aViewportSize.IsEmpty()
                                      ? Min(aHandle->GetSize(), aViewportSize)
                                      : aHandle->GetSize())
            : GetRect());
    mSharedContext->SetNoClipMask();
    // Ensure the clip gets reset if clipping is later requested for the target.
    mRefreshClipState = true;
  } else if (mRefreshClipState || !mSharedContext->IsCurrentTarget(this)) {
    // Try to use a simple clip rect if possible. Otherwise, fall back to
    // generating a clip mask texture that can represent complex clip regions.
    if (!SetSimpleClipRect() && !GenerateComplexClipMask()) {
      return false;
    }
    mClipChanged = false;
    mRefreshClipState = false;
  }
  return mSharedContext->SetTarget(this, aHandle, aViewportSize);
}

// Whether clipping may be necessary for the operation. This tries to avoid
// generating a complex clip mask in case the current target is not active
// or not using WebGL. If there is only a simple clip mask and its bounds
// encompass the viewport, then no clipping is required.
bool DrawTargetWebgl::ShouldClip() {
  if (mSharedContext->IsCurrentTarget(this) && !mRefreshClipState) {
    return mSharedContext->HasClipMask() ||
           !mSharedContext->mClipAARect.Contains(Rect(GetRect()));
  }
  if (Maybe<Rect> rect = ComputeSimpleClipRect()) {
    return !rect->Contains(Rect(GetRect()));
  }
  return true;
}

void SharedContextWebgl::RestoreCurrentTarget(
    const RefPtr<WebGLTexture>& aClipMask) {
  if (!mCurrentTarget) {
    return;
  }
  mWebgl->BindFramebuffer(
      LOCAL_GL_FRAMEBUFFER,
      mTargetHandle ? mTargetFramebuffer : mCurrentTarget->mFramebuffer);
  IntPoint offset =
      mTargetHandle ? mTargetHandle->GetBounds().TopLeft() : IntPoint(0, 0);
  mWebgl->Viewport(offset.x, offset.y, mViewportSize.width,
                   mViewportSize.height);
  if (aClipMask) {
    SetClipMask(aClipMask);
  }
}

bool SharedContextWebgl::IsContextLost() const {
  return !mWebgl || mWebgl->IsContextLost();
}

// Signal to CanvasRenderingContext2D when the WebGL context is lost.
bool DrawTargetWebgl::IsValid() const {
  return mSharedContext && !mSharedContext->IsContextLost();
}

bool DrawTargetWebgl::CanCreate(const IntSize& aSize, SurfaceFormat aFormat) {
  if (!gfxVars::UseAcceleratedCanvas2D()) {
    return false;
  }

  if (!Factory::AllowedSurfaceSize(aSize)) {
    return false;
  }

  // The interpretation of the min-size and max-size follows from the old
  // SkiaGL prefs. First just ensure that the context is not unreasonably
  // small.
  static const int32_t kMinDimension = 16;
  if (std::min(aSize.width, aSize.height) < kMinDimension) {
    return false;
  }

  int32_t minSize = StaticPrefs::gfx_canvas_accelerated_min_size();
  if (aSize.width * aSize.height < minSize * minSize) {
    return false;
  }

  // Maximum pref allows 2 different options:
  //  <= 0 means unlimited size,
  //  > 0 means use value as an absolute threshold,
  int32_t maxSize = StaticPrefs::gfx_canvas_accelerated_max_size();
  if (maxSize > 0 && std::max(aSize.width, aSize.height) > maxSize) {
    return false;
  }

  return true;
}

already_AddRefed<DrawTargetWebgl> DrawTargetWebgl::Create(
    const IntSize& aSize, SurfaceFormat aFormat,
    const RefPtr<SharedContextWebgl>& aSharedContext) {
  // Validate the size and format.
  if (!CanCreate(aSize, aFormat)) {
    return nullptr;
  }

  RefPtr<DrawTargetWebgl> dt = new DrawTargetWebgl;
  if (!dt->Init(aSize, aFormat, aSharedContext) || !dt->IsValid()) {
    return nullptr;
  }

  return dt.forget();
}

void* DrawTargetWebgl::GetNativeSurface(NativeSurfaceType aType) {
  switch (aType) {
    case NativeSurfaceType::WEBGL_CONTEXT:
      // If the context is lost, then don't attempt to access it.
      if (mSharedContext->IsContextLost()) {
        return nullptr;
      }
      if (!mWebglValid) {
        FlushFromSkia();
      }
      return mSharedContext->mWebgl.get();
    default:
      return nullptr;
  }
}

// Wrap a WebGL texture holding a snapshot with a texture handle. Note that
// while the texture is still in use as the backing texture of a framebuffer,
// it's texture memory is not currently tracked with other texture handles.
// Once it is finally orphaned and used as a texture handle, it must be added
// to the resource usage totals.
already_AddRefed<TextureHandle> SharedContextWebgl::WrapSnapshot(
    const IntSize& aSize, SurfaceFormat aFormat, RefPtr<WebGLTexture> aTex) {
  // Ensure there is enough space for the texture.
  size_t usedBytes = BackingTexture::UsedBytes(aFormat, aSize);
  PruneTextureMemory(usedBytes, false);
  // Allocate a handle for the texture
  RefPtr<StandaloneTexture> handle =
      new StandaloneTexture(aSize, aFormat, aTex.forget());
  mStandaloneTextures.push_back(handle);
  mTextureHandles.insertFront(handle);
  AddTextureMemory(handle);
  mUsedTextureMemory += usedBytes;
  ++mNumTextureHandles;
  return handle.forget();
}

void SharedContextWebgl::SetTexFilter(WebGLTexture* aTex, bool aFilter) {
  mWebgl->TexParameter_base(
      LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER,
      FloatOrInt(aFilter ? LOCAL_GL_LINEAR : LOCAL_GL_NEAREST));
  mWebgl->TexParameter_base(
      LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER,
      FloatOrInt(aFilter ? LOCAL_GL_LINEAR : LOCAL_GL_NEAREST));
}

void SharedContextWebgl::InitTexParameters(WebGLTexture* aTex, bool aFilter) {
  mWebgl->TexParameter_base(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S,
                            FloatOrInt(LOCAL_GL_REPEAT));
  mWebgl->TexParameter_base(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T,
                            FloatOrInt(LOCAL_GL_REPEAT));
  SetTexFilter(aTex, aFilter);
}

// Copy the contents of the WebGL framebuffer into a WebGL texture.
already_AddRefed<TextureHandle> SharedContextWebgl::CopySnapshot(
    const IntRect& aRect, TextureHandle* aHandle) {
  if (!mWebgl || mWebgl->IsContextLost()) {
    return nullptr;
  }

  // If the target is going away, then we can just directly reuse the
  // framebuffer texture since it will never change.
  RefPtr<WebGLTexture> tex = mWebgl->CreateTexture();
  if (!tex) {
    return nullptr;
  }

  // If copying from a non-DT source, we have to bind a scratch framebuffer for
  // reading.
  if (aHandle) {
    BindScratchFramebuffer(aHandle, false);
  }

  // Create a texture to hold the copy
  BindAndInitRenderTex(tex, SurfaceFormat::B8G8R8A8, aRect.Size());
  // Copy the framebuffer into the texture
  mWebgl->CopyTexImage(LOCAL_GL_TEXTURE_2D, 0, 0, {0, 0, 0}, {aRect.x, aRect.y},
                       {uint32_t(aRect.width), uint32_t(aRect.height)});

  SurfaceFormat format =
      aHandle ? aHandle->GetFormat() : mCurrentTarget->GetFormat();
  already_AddRefed<TextureHandle> result =
      WrapSnapshot(aRect.Size(), format, tex.forget());

  // Restore the actual framebuffer after reading is done.
  if (aHandle) {
    RestoreCurrentTarget();
  }

  return result;
}

inline DrawTargetWebgl::AutoRestoreContext::AutoRestoreContext(
    DrawTargetWebgl* aTarget)
    : mTarget(aTarget),
      mClipAARect(aTarget->mSharedContext->mClipAARect),
      mLastClipMask(aTarget->mSharedContext->mLastClipMask) {}

inline DrawTargetWebgl::AutoRestoreContext::~AutoRestoreContext() {
  mTarget->mSharedContext->SetClipRect(mClipAARect);
  if (mLastClipMask) {
    mTarget->mSharedContext->SetClipMask(mLastClipMask);
  }
  mTarget->mRefreshClipState = true;
}

// Utility method to install the target before copying a snapshot.
already_AddRefed<TextureHandle> DrawTargetWebgl::CopySnapshot(
    const IntRect& aRect) {
  AutoRestoreContext restore(this);
  if (!PrepareContext(false)) {
    return nullptr;
  }
  return mSharedContext->CopySnapshot(aRect);
}

bool DrawTargetWebgl::HasDataSnapshot() const {
  return (mSkiaValid && !mSkiaLayer) || (mSnapshot && mSnapshot->HasReadData());
}

bool DrawTargetWebgl::PrepareSkia() {
  if (!mSkiaValid) {
    ReadIntoSkia();
  } else if (mSkiaLayer) {
    FlattenSkia();
  }
  return mSkiaValid;
}

bool DrawTargetWebgl::EnsureDataSnapshot() {
  // If there is already a data snapshot, there is nothing to do. If there is a
  // snapshot that has a pending PBO readback, then try to force the readback.
  // Otherwise, read back the WebGL framebuffer into the Skia DT.
  return HasDataSnapshot() || (mSnapshot && mSnapshot->ForceReadFromPBO()) ||
         PrepareSkia();
}

void DrawTargetWebgl::PrepareShmem() { PrepareSkia(); }

// Borrow a snapshot that may be used by another thread for composition. Only
// Skia snapshots are safe to pass around.
already_AddRefed<SourceSurface> DrawTargetWebgl::GetDataSnapshot() {
  PrepareSkia();
  return mSkia->Snapshot(mFormat);
}

already_AddRefed<SourceSurface> DrawTargetWebgl::Snapshot() {
  // If already using the Skia fallback, then just snapshot that.
  if (mSkiaValid) {
    return GetDataSnapshot();
  }

  // There's no valid Skia snapshot, so we need to get one from the WebGL
  // context.
  if (!mSnapshot) {
    // Create a copy-on-write reference to this target.
    mSnapshot = new SourceSurfaceWebgl(this);
  }
  return do_AddRef(mSnapshot);
}

// If we need to provide a snapshot for another DrawTargetWebgl that shares the
// same WebGL context, then it is safe to directly return a snapshot. Otherwise,
// we may be exporting to another thread and require a data snapshot.
already_AddRefed<SourceSurface> DrawTargetWebgl::GetOptimizedSnapshot(
    DrawTarget* aTarget) {
  if (aTarget && aTarget->GetBackendType() == BackendType::WEBGL &&
      static_cast<DrawTargetWebgl*>(aTarget)->mSharedContext ==
          mSharedContext) {
    return Snapshot();
  }
  return GetDataSnapshot();
}

// Read from the WebGL context into a buffer, either a memory buffer or a PBO.
// This handles both swizzling BGRA to RGBA and flipping the image.
bool SharedContextWebgl::ReadInto(uint8_t* aDstData, int32_t aDstStride,
                                  SurfaceFormat aFormat, const IntRect& aBounds,
                                  TextureHandle* aHandle,
                                  const RefPtr<WebGLBuffer>& aBuffer) {
  MOZ_ASSERT(aFormat == SurfaceFormat::B8G8R8A8 ||
             aFormat == SurfaceFormat::B8G8R8X8 ||
             aFormat == SurfaceFormat::A8);

  // If reading into a new texture, we have to bind it to a scratch framebuffer
  // for reading.
  if (aHandle) {
    BindScratchFramebuffer(aHandle, false);
  } else if (!aBuffer && mCurrentTarget && !mTargetHandle &&
             mCurrentTarget->mIsClear) {
    // If reading from a target that is still clear, then avoid the readback by
    // just clearing the data.
    SkPixmap(MakeSkiaImageInfo(aBounds.Size(), aFormat), aDstData, aDstStride)
        .erase(IsOpaque(aFormat) ? SK_ColorBLACK : SK_ColorTRANSPARENT);
    return true;
  }

  webgl::ReadPixelsDesc desc;
  desc.srcOffset = *ivec2::From(aBounds);
  desc.size = *uvec2::FromSize(aBounds);
  desc.packState.rowLength = aDstStride / BytesPerPixel(aFormat);
  if (aBuffer) {
    mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, aBuffer);
    mWebgl->ReadPixelsPbo(desc, 0);
    mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, 0);
  } else {
    Range<uint8_t> range = {aDstData, size_t(aDstStride) * aBounds.height};
    mWebgl->ReadPixelsInto(desc, range);
  }

  // Restore the actual framebuffer after reading is done.
  if (aHandle) {
    RestoreCurrentTarget();
  }

  return true;
}

already_AddRefed<DataSourceSurface> SharedContextWebgl::ReadSnapshot(
    TextureHandle* aHandle, uint8_t* aData, int32_t aStride) {
  // Allocate a data surface, map it, and read from the WebGL context into the
  // surface.
  SurfaceFormat format = SurfaceFormat::UNKNOWN;
  IntRect bounds;
  if (aHandle) {
    format = aHandle->GetFormat();
    bounds = aHandle->GetBounds();
  } else {
    if (!mCurrentTarget) {
      return nullptr;
    }
    format = mCurrentTarget->GetFormat();
    bounds = mCurrentTarget->GetRect();
  }
  RefPtr<DataSourceSurface> surface =
      aData ? Factory::CreateWrappingDataSourceSurface(aData, aStride,
                                                       bounds.Size(), format)
            : Factory::CreateDataSourceSurface(bounds.Size(), format);
  if (!surface) {
    return nullptr;
  }
  DataSourceSurface::ScopedMap dstMap(surface, DataSourceSurface::WRITE);
  if (!dstMap.IsMapped() || !ReadInto(dstMap.GetData(), dstMap.GetStride(),
                                      format, bounds, aHandle)) {
    return nullptr;
  }
  return surface.forget();
}

static inline Maybe<int32_t> GetPBOStride(int32_t aWidth,
                                          SurfaceFormat aFormat) {
  return GetAlignedStride<16>(aWidth, BytesPerPixel(aFormat));
}

already_AddRefed<WebGLBuffer> SharedContextWebgl::ReadSnapshotIntoPBO(
    SourceSurfaceWebgl* aOwner, TextureHandle* aHandle) {
  // Allocate a PBO, and read from the WebGL context into it.
  SurfaceFormat format = SurfaceFormat::UNKNOWN;
  IntRect bounds;
  if (aHandle) {
    format = aHandle->GetFormat();
    bounds = aHandle->GetBounds();
  } else {
    if (!mCurrentTarget) {
      return nullptr;
    }
    format = mCurrentTarget->GetFormat();
    bounds = mCurrentTarget->GetRect();
  }
  auto pboStride = GetPBOStride(bounds.width, format);
  if (pboStride.isNothing()) {
    return nullptr;
  }
  size_t bufSize =
      BufferSizeFromStrideAndHeight(pboStride.value(), bounds.height);
  if (!bufSize) {
    return nullptr;
  }

  // If the PBO is too large to fit within the memory limit by itself, then
  // don't try to use a PBO.
  size_t maxPBOMemory =
      StaticPrefs::gfx_canvas_accelerated_max_snapshot_pbo_memory();
  if (bufSize > maxPBOMemory) {
    return nullptr;
  }

  RefPtr<WebGLBuffer> pbo = mWebgl->CreateBuffer();
  if (!pbo) {
    return nullptr;
  }
  mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, pbo);
  mWebgl->UninitializedBufferData_SizeOnly(LOCAL_GL_PIXEL_PACK_BUFFER, bufSize,
                                           LOCAL_GL_STREAM_READ);
  mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, 0);
  if (!ReadInto(nullptr, pboStride.value(), format, bounds, aHandle, pbo)) {
    return nullptr;
  }

  // If there are existing snapshot PBOs, check if adding this PBO would exceed
  // the memory limit for snapshot PBOs. This happens after the new PBO was set
  // up and the readback initiated, in case purging an old PBO causes a stall
  // which can be used to cover the latency of the readback for the new PBO.
  ClearSnapshotPBOs(maxPBOMemory - std::min(bufSize, maxPBOMemory));

  mUsedSnapshotPBOMemory += bufSize;
  mSnapshotPBOs.emplace_back(aOwner);
  return pbo.forget();
}

already_AddRefed<DataSourceSurface> SharedContextWebgl::ReadSnapshotFromPBO(
    const RefPtr<WebGLBuffer>& aBuffer, SurfaceFormat aFormat,
    const IntSize& aSize, uint8_t* aData, int32_t aStride) {
  // For an existing PBO where a readback has been initiated previously, create
  // a new data surface and copy the PBO's data into the data surface.
  auto pboStride = GetPBOStride(aSize.width, aFormat);
  if (pboStride.isNothing()) {
    return nullptr;
  }
  size_t bufSize = BufferSizeFromStrideAndHeight(
      aData ? aStride : pboStride.value(), aSize.height);
  if (!bufSize) {
    return nullptr;
  }
  RefPtr<DataSourceSurface> surface =
      aData ? Factory::CreateWrappingDataSourceSurface(aData, aStride, aSize,
                                                       aFormat)
            : Factory::CreateDataSourceSurfaceWithStride(aSize, aFormat,
                                                         pboStride.value());
  if (!surface) {
    return nullptr;
  }
  DataSourceSurface::ScopedMap dstMap(surface, DataSourceSurface::WRITE);
  if (!dstMap.IsMapped()) {
    return nullptr;
  }
  mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, aBuffer);
  Range<uint8_t> range = {dstMap.GetData(), bufSize};
  bool success = mWebgl->AsWebGL2()->GetBufferSubData(
      LOCAL_GL_PIXEL_PACK_BUFFER, 0, range, aSize.height,
      BytesPerPixel(aFormat) * aSize.width, pboStride.value(),
      dstMap.GetStride());
  mWebgl->BindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, 0);
  if (success) {
    return surface.forget();
  }
  return nullptr;
}

void SharedContextWebgl::RemoveSnapshotPBO(
    SourceSurfaceWebgl* aOwner, already_AddRefed<WebGLBuffer> aBuffer) {
  RefPtr<WebGLBuffer> buffer(aBuffer);
  MOZ_ASSERT(aOwner && buffer);
  IntSize size = aOwner->GetSize();
  SurfaceFormat format = aOwner->GetFormat();
  size_t bufSize = 0;
  if (auto pboStride = GetPBOStride(size.width, format)) {
    bufSize = BufferSizeFromStrideAndHeight(pboStride.value(), size.height);
  }
  // If the queue is empty, no memory should be used. Otherwise, deduct the
  // usage from the queue.
  if (mSnapshotPBOs.empty()) {
    mUsedSnapshotPBOMemory = 0;
  } else if (bufSize) {
    mUsedSnapshotPBOMemory -= std::min(mUsedSnapshotPBOMemory, bufSize);
  }
}

void SharedContextWebgl::ClearSnapshotPBOs(size_t aMaxMemory) {
  // Force any pending readback PBOs to convert to actual data.
  while (!mSnapshotPBOs.empty() &&
         (!aMaxMemory || mUsedSnapshotPBOMemory > aMaxMemory)) {
    RefPtr<SourceSurfaceWebgl> snapshot(mSnapshotPBOs.front());
    mSnapshotPBOs.pop_front();
    if (snapshot) {
      snapshot->ForceReadFromPBO();
    }
  }
  if (mSnapshotPBOs.empty()) {
    mUsedSnapshotPBOMemory = 0;
  }
}

// Utility method to install the target before reading a snapshot.
bool DrawTargetWebgl::ReadInto(uint8_t* aDstData, int32_t aDstStride) {
  if (!PrepareContext(false)) {
    return false;
  }

  return mSharedContext->ReadInto(aDstData, aDstStride, GetFormat(), GetRect());
}

// Utility method to install the target before reading a snapshot.
already_AddRefed<DataSourceSurface> DrawTargetWebgl::ReadSnapshot(
    uint8_t* aData, int32_t aStride) {
  AutoRestoreContext restore(this);
  if (!PrepareContext(false)) {
    return nullptr;
  }
  mProfile.OnReadback();
  return mSharedContext->ReadSnapshot(nullptr, aData, aStride);
}

already_AddRefed<WebGLBuffer> DrawTargetWebgl::ReadSnapshotIntoPBO(
    SourceSurfaceWebgl* aOwner) {
  AutoRestoreContext restore(this);
  if (!PrepareContext(false)) {
    return nullptr;
  }
  mProfile.OnReadback();
  return mSharedContext->ReadSnapshotIntoPBO(aOwner);
}

already_AddRefed<SourceSurface> DrawTargetWebgl::GetBackingSurface() {
  return Snapshot();
}

void DrawTargetWebgl::DetachAllSnapshots() {
  mSkia->DetachAllSnapshots();
  ClearSnapshot();
}

// Prepare the framebuffer for accelerated drawing. Any cached snapshots will
// be invalidated if not detached and copied here. Ensure the WebGL
// framebuffer's contents are updated if still somehow stored in the Skia
// framebuffer.
bool DrawTargetWebgl::MarkChanged() {
  if (mSnapshot) {
    // Try to copy the target into a new texture if possible.
    ClearSnapshot(true, true);
  }
  if (!mWebglValid && !FlushFromSkia()) {
    return false;
  }
  mSkiaValid = false;
  mIsClear = false;
  return true;
}

void DrawTargetWebgl::MarkSkiaChanged(bool aOverwrite) {
  if (aOverwrite) {
    mSkiaValid = true;
    mSkiaLayer = false;
  } else if (!mSkiaValid) {
    if (ReadIntoSkia()) {
      // Signal that we've hit a complete software fallback.
      mProfile.OnFallback();
    }
  } else if (mSkiaLayer && !mLayerDepth) {
    FlattenSkia();
  }
  mWebglValid = false;
  mIsClear = false;
}

// Whether a given composition operator is associative and thus allows drawing
// into a separate layer that can be later composited back into the WebGL
// context.
static inline bool SupportsLayering(const DrawOptions& aOptions) {
  switch (aOptions.mCompositionOp) {
    case CompositionOp::OP_OVER:
      // Layering is only supported for the default source-over composition op.
      return true;
    default:
      return false;
  }
}

void DrawTargetWebgl::MarkSkiaChanged(const DrawOptions& aOptions) {
  if (SupportsLayering(aOptions)) {
    if (!mSkiaValid) {
      // If the Skia context needs initialization, clear it and enable layering.
      mSkiaValid = true;
      if (mWebglValid) {
        mProfile.OnLayer();
        mSkiaLayer = true;
        mSkiaLayerClear = mIsClear;
        mSkia->DetachAllSnapshots();
        if (mSkiaLayerClear) {
          // Avoid blending later by making sure the layer background is filled
          // with opaque alpha values if necessary.
          mSkiaNoClip->FillRect(Rect(mSkiaNoClip->GetRect()), GetClearPattern(),
                                DrawOptions(1.0f, CompositionOp::OP_SOURCE));
        } else {
          mSkiaNoClip->ClearRect(Rect(mSkiaNoClip->GetRect()));
        }
      }
    }
    // The WebGL context is no longer up-to-date.
    mWebglValid = false;
    mIsClear = false;
  } else {
    // For other composition ops, just overwrite the Skia data.
    MarkSkiaChanged();
  }
}

bool DrawTargetWebgl::LockBits(uint8_t** aData, IntSize* aSize,
                               int32_t* aStride, SurfaceFormat* aFormat,
                               IntPoint* aOrigin) {
  // Can only access pixels if there is valid, flattened Skia data.
  if (mSkiaValid && !mSkiaLayer) {
    MarkSkiaChanged();
    return mSkia->LockBits(aData, aSize, aStride, aFormat, aOrigin);
  }
  return false;
}

void DrawTargetWebgl::ReleaseBits(uint8_t* aData) {
  // Can only access pixels if there is valid, flattened Skia data.
  if (mSkiaValid && !mSkiaLayer) {
    mSkia->ReleaseBits(aData);
  }
}

// Format is x, y, alpha
static const float kRectVertexData[12] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                                          1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

// Orphans the contents of the path vertex buffer. The beginning of the buffer
// always contains data for a simple rectangle draw to avoid needing to switch
// buffers.
void SharedContextWebgl::ResetPathVertexBuffer() {
  if (!mPathVertexBuffer) {
    MOZ_ASSERT(false);
    return;
  }

  size_t oldCapacity = mPathVertexBuffer->ByteLength();
  RemoveUntrackedTextureMemory(oldCapacity);

  mWebgl->BindBuffer(LOCAL_GL_ARRAY_BUFFER, mPathVertexBuffer.get());
  mWebgl->UninitializedBufferData_SizeOnly(
      LOCAL_GL_ARRAY_BUFFER,
      std::max(size_t(mPathVertexCapacity), sizeof(kRectVertexData)),
      LOCAL_GL_DYNAMIC_DRAW);
  mWebgl->BufferSubData(LOCAL_GL_ARRAY_BUFFER, 0, sizeof(kRectVertexData),
                        (const uint8_t*)kRectVertexData);
  mPathVertexOffset = sizeof(kRectVertexData);

  size_t newCapacity = mPathVertexBuffer->ByteLength();
  AddUntrackedTextureMemory(newCapacity);

  if (mWGROutputBuffer &&
      (!mPathVertexCapacity || newCapacity != oldCapacity)) {
    RemoveHeapData(mWGROutputBuffer.get());
    mWGROutputBuffer = nullptr;
  }

  if (mPathVertexCapacity > 0 && !mWGROutputBuffer) {
    mWGROutputBuffer.reset(new (
        fallible) WGR::OutputVertex[newCapacity / sizeof(WGR::OutputVertex)]);
    AddHeapData(mWGROutputBuffer.get());
  }
}

// Sigma below which contribution of neighbor pixels is visually insignificant.
#define BLUR_ACCEL_SIGMA_MIN 0.27f
// Maximum blur sigma allowed by shadows and filters currently.
#define BLUR_ACCEL_SIGMA_MAX 100
#define BLUR_ACCEL_RADIUS(sigma) (int(ceil(1.5 * (sigma))) * 2)
#define BLUR_ACCEL_RADIUS_MAX (3 * BLUR_ACCEL_SIGMA_MAX)

// Threshold for when to downscale blur inputs.
#define BLUR_ACCEL_DOWNSCALE_SIGMA 20
#define BLUR_ACCEL_DOWNSCALE_SIZE 32
// How much to downscale blur inputs.
#define BLUR_ACCEL_DOWNSCALE_ITERS 2

// Attempts to create all shaders and resources to be used for drawing commands.
// Returns whether or not this succeeded.
bool SharedContextWebgl::CreateShaders() {
  if (!mPathVertexArray) {
    mPathVertexArray = mWebgl->CreateVertexArray();
  }
  if (!mPathVertexBuffer) {
    mPathVertexBuffer = mWebgl->CreateBuffer();
    AddUntrackedTextureMemory(mPathVertexBuffer);
    mWebgl->BindVertexArray(mPathVertexArray.get());
    ResetPathVertexBuffer();
    mWebgl->EnableVertexAttribArray(0);

    webgl::VertAttribPointerDesc attribDesc;
    attribDesc.channels = 3;
    attribDesc.type = LOCAL_GL_FLOAT;
    attribDesc.normalized = false;
    mWebgl->VertexAttribPointer(0, attribDesc);
  }
  if (!mSolidProgram) {
    // AA is computed by using the basis vectors of the transform to determine
    // both the scale and orientation. The scale is then used to extrude the
    // rectangle outward by 1 screen-space pixel to account for the AA region.
    // The distance to the rectangle edges is passed to the fragment shader in
    // an interpolant, biased by 0.5 so it represents the desired coverage. The
    // minimum coverage is then chosen by the fragment shader to use as an AA
    // coverage value to modulate the color.
    auto vsSource =
        "attribute vec3 a_vertex;\n"
        "uniform vec2 u_transform[3];\n"
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_clipbounds;\n"
        "uniform float u_aa;\n"
        "varying vec2 v_cliptc;\n"
        "varying vec4 v_clipdist;\n"
        "varying vec4 v_dist;\n"
        "varying float v_alpha;\n"
        "void main() {\n"
        "   vec2 scale = vec2(dot(u_transform[0], u_transform[0]),\n"
        "                     dot(u_transform[1], u_transform[1]));\n"
        "   vec2 invScale = u_aa * inversesqrt(scale + 1.0e-6);\n"
        "   scale *= invScale;\n"
        "   vec2 extrude = a_vertex.xy +\n"
        "                  invScale * (2.0 * a_vertex.xy - 1.0);\n"
        "   vec2 vertex = u_transform[0] * extrude.x +\n"
        "                 u_transform[1] * extrude.y +\n"
        "                 u_transform[2];\n"
        "   gl_Position = vec4(vertex * 2.0 / u_viewport - 1.0, 0.0, 1.0);\n"
        "   v_cliptc = vertex / u_viewport;\n"
        "   v_clipdist = vec4(vertex - u_clipbounds.xy,\n"
        "                     u_clipbounds.zw - vertex);\n"
        "   float noAA = 1.0 - u_aa;\n"
        "   v_dist = vec4(extrude, 1.0 - extrude) * scale.xyxy + 0.5 + noAA;\n"
        "   v_alpha = min(a_vertex.z,\n"
        "                 min(scale.x, 1.0) * min(scale.y, 1.0) + noAA);\n"
        "}\n";
    auto fsSource =
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "uniform sampler2D u_clipmask;\n"
        "varying highp vec2 v_cliptc;\n"
        "varying vec4 v_clipdist;\n"
        "varying vec4 v_dist;\n"
        "varying float v_alpha;\n"
        "void main() {\n"
        "   float clip = texture2D(u_clipmask, v_cliptc).r;\n"
        "   vec4 dist = min(v_dist, v_clipdist);\n"
        "   dist.xy = min(dist.xy, dist.zw);\n"
        "   float aa = clamp(min(dist.x, dist.y), 0.0, v_alpha);\n"
        "   gl_FragColor = clip * aa * u_color;\n"
        "}\n";
    RefPtr<WebGLShader> vsId = mWebgl->CreateShader(LOCAL_GL_VERTEX_SHADER);
    mWebgl->ShaderSource(*vsId, vsSource);
    mWebgl->CompileShader(*vsId);
    if (!mWebgl->GetCompileResult(*vsId).success) {
      return false;
    }
    RefPtr<WebGLShader> fsId = mWebgl->CreateShader(LOCAL_GL_FRAGMENT_SHADER);
    mWebgl->ShaderSource(*fsId, fsSource);
    mWebgl->CompileShader(*fsId);
    if (!mWebgl->GetCompileResult(*fsId).success) {
      return false;
    }
    mSolidProgram = mWebgl->CreateProgram();
    mWebgl->AttachShader(*mSolidProgram, *vsId);
    mWebgl->AttachShader(*mSolidProgram, *fsId);
    mWebgl->BindAttribLocation(*mSolidProgram, 0, "a_vertex");
    mWebgl->LinkProgram(*mSolidProgram);
    if (!mWebgl->GetLinkResult(*mSolidProgram).success) {
      return false;
    }
    mSolidProgramViewport = GetUniformLocation(mSolidProgram, "u_viewport");
    mSolidProgramAA = GetUniformLocation(mSolidProgram, "u_aa");
    mSolidProgramTransform = GetUniformLocation(mSolidProgram, "u_transform");
    mSolidProgramColor = GetUniformLocation(mSolidProgram, "u_color");
    mSolidProgramClipMask = GetUniformLocation(mSolidProgram, "u_clipmask");
    mSolidProgramClipBounds = GetUniformLocation(mSolidProgram, "u_clipbounds");
    if (!mSolidProgramViewport || !mSolidProgramAA || !mSolidProgramTransform ||
        !mSolidProgramColor || !mSolidProgramClipMask ||
        !mSolidProgramClipBounds) {
      return false;
    }
    mWebgl->UseProgram(mSolidProgram);
    UniformData(LOCAL_GL_INT, mSolidProgramClipMask, Array<int32_t, 1>{1});
  }

  if (!mImageProgram) {
    auto vsSource =
        "attribute vec3 a_vertex;\n"
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_clipbounds;\n"
        "uniform float u_aa;\n"
        "uniform vec2 u_transform[3];\n"
        "uniform vec2 u_texmatrix[3];\n"
        "varying vec2 v_cliptc;\n"
        "varying vec2 v_texcoord;\n"
        "varying vec4 v_clipdist;\n"
        "varying vec4 v_dist;\n"
        "varying float v_alpha;\n"
        "void main() {\n"
        "   vec2 scale = vec2(dot(u_transform[0], u_transform[0]),\n"
        "                     dot(u_transform[1], u_transform[1]));\n"
        "   vec2 invScale = u_aa * inversesqrt(scale + 1.0e-6);\n"
        "   scale *= invScale;\n"
        "   vec2 extrude = a_vertex.xy +\n"
        "                  invScale * (2.0 * a_vertex.xy - 1.0);\n"
        "   vec2 vertex = u_transform[0] * extrude.x +\n"
        "                 u_transform[1] * extrude.y +\n"
        "                 u_transform[2];\n"
        "   gl_Position = vec4(vertex * 2.0 / u_viewport - 1.0, 0.0, 1.0);\n"
        "   v_cliptc = vertex / u_viewport;\n"
        "   v_clipdist = vec4(vertex - u_clipbounds.xy,\n"
        "                     u_clipbounds.zw - vertex);\n"
        "   v_texcoord = u_texmatrix[0] * extrude.x +\n"
        "                u_texmatrix[1] * extrude.y +\n"
        "                u_texmatrix[2];\n"
        "   float noAA = 1.0 - u_aa;\n"
        "   v_dist = vec4(extrude, 1.0 - extrude) * scale.xyxy + 0.5 + noAA;\n"
        "   v_alpha = min(a_vertex.z,\n"
        "                 min(scale.x, 1.0) * min(scale.y, 1.0) + noAA);\n"
        "}\n";
    auto fsSource =
        "precision mediump float;\n"
        "uniform vec4 u_texbounds;\n"
        "uniform vec4 u_color;\n"
        "uniform float u_swizzle;\n"
        "uniform sampler2D u_sampler;\n"
        "uniform sampler2D u_clipmask;\n"
        "varying highp vec2 v_cliptc;\n"
        "varying highp vec2 v_texcoord;\n"
        "varying vec4 v_clipdist;\n"
        "varying vec4 v_dist;\n"
        "varying float v_alpha;\n"
        "void main() {\n"
        "   highp vec2 tc = clamp(v_texcoord, u_texbounds.xy,\n"
        "                         u_texbounds.zw);\n"
        "   vec4 image = texture2D(u_sampler, tc);\n"
        "   float clip = texture2D(u_clipmask, v_cliptc).r;\n"
        "   vec4 dist = min(v_dist, v_clipdist);\n"
        "   dist.xy = min(dist.xy, dist.zw);\n"
        "   float aa = clamp(min(dist.x, dist.y), 0.0, v_alpha);\n"
        "   gl_FragColor = clip * aa * u_color *\n"
        "                  mix(image, image.rrrr, u_swizzle);\n"
        "}\n";
    RefPtr<WebGLShader> vsId = mWebgl->CreateShader(LOCAL_GL_VERTEX_SHADER);
    mWebgl->ShaderSource(*vsId, vsSource);
    mWebgl->CompileShader(*vsId);
    if (!mWebgl->GetCompileResult(*vsId).success) {
      return false;
    }
    RefPtr<WebGLShader> fsId = mWebgl->CreateShader(LOCAL_GL_FRAGMENT_SHADER);
    mWebgl->ShaderSource(*fsId, fsSource);
    mWebgl->CompileShader(*fsId);
    if (!mWebgl->GetCompileResult(*fsId).success) {
      return false;
    }
    mImageProgram = mWebgl->CreateProgram();
    mWebgl->AttachShader(*mImageProgram, *vsId);
    mWebgl->AttachShader(*mImageProgram, *fsId);
    mWebgl->BindAttribLocation(*mImageProgram, 0, "a_vertex");
    mWebgl->LinkProgram(*mImageProgram);
    if (!mWebgl->GetLinkResult(*mImageProgram).success) {
      return false;
    }
    mImageProgramViewport = GetUniformLocation(mImageProgram, "u_viewport");
    mImageProgramAA = GetUniformLocation(mImageProgram, "u_aa");
    mImageProgramTransform = GetUniformLocation(mImageProgram, "u_transform");
    mImageProgramTexMatrix = GetUniformLocation(mImageProgram, "u_texmatrix");
    mImageProgramTexBounds = GetUniformLocation(mImageProgram, "u_texbounds");
    mImageProgramSwizzle = GetUniformLocation(mImageProgram, "u_swizzle");
    mImageProgramColor = GetUniformLocation(mImageProgram, "u_color");
    mImageProgramSampler = GetUniformLocation(mImageProgram, "u_sampler");
    mImageProgramClipMask = GetUniformLocation(mImageProgram, "u_clipmask");
    mImageProgramClipBounds = GetUniformLocation(mImageProgram, "u_clipbounds");
    if (!mImageProgramViewport || !mImageProgramAA || !mImageProgramTransform ||
        !mImageProgramTexMatrix || !mImageProgramTexBounds ||
        !mImageProgramSwizzle || !mImageProgramColor || !mImageProgramSampler ||
        !mImageProgramClipMask || !mImageProgramClipBounds) {
      return false;
    }
    mWebgl->UseProgram(mImageProgram);
    UniformData(LOCAL_GL_INT, mImageProgramSampler, Array<int32_t, 1>{0});
    UniformData(LOCAL_GL_INT, mImageProgramClipMask, Array<int32_t, 1>{1});
  }
  if (!mBlurProgram) {
    auto vsSource =
        "#version 300 es\n"
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_clipbounds;\n"
        "uniform vec4 u_transform;\n"
        "uniform vec4 u_texmatrix;\n"
        "uniform vec4 u_texbounds;\n"
        "uniform vec2 u_offsetscale;\n"
        "uniform float u_sigma;\n"
        "in vec3 a_vertex;\n"
        "out vec2 v_cliptc;\n"
        "out vec2 v_texcoord;\n"
        "out vec4 v_texbounds;\n"
        "out vec4 v_clipdist;\n"
        "flat out vec2 v_gauss_coeffs;\n"
        "flat out ivec2 v_support;\n"
        "void calculate_gauss_coeffs(float sigma) {\n"
        "  v_gauss_coeffs = vec2(1.0 / (sqrt(2.0 * 3.14159265) * sigma),\n"
        "                        exp(-0.5 / (sigma * sigma)));\n"
        "  vec3 gauss_coeff = vec3(v_gauss_coeffs,\n"
        "                           v_gauss_coeffs.y * v_gauss_coeffs.y);\n"
        "  float gauss_coeff_total = gauss_coeff.x;\n"
        "  for (int i = 1; i <= v_support.x; i += 2) {\n"
        "    gauss_coeff.xy *= gauss_coeff.yz;\n"
        "    float gauss_coeff_subtotal = gauss_coeff.x;\n"
        "    gauss_coeff.xy *= gauss_coeff.yz;\n"
        "    gauss_coeff_subtotal += gauss_coeff.x;\n"
        "    gauss_coeff_total += 2.0 * gauss_coeff_subtotal;\n"
        "  }\n"
        "  v_gauss_coeffs.x /= gauss_coeff_total;\n"
        "}\n"
        "void main() {\n"
        "  vec2 vertex = u_transform.xy * a_vertex.xy + u_transform.zw;\n"
        "  gl_Position = vec4(vertex * 2.0 / u_viewport - 1.0, 0.0, 1.0);\n"
        "  v_cliptc = vertex / u_viewport;\n"
        "  v_clipdist = vec4(vertex - u_clipbounds.xy,\n"
        "                    u_clipbounds.zw - vertex);\n"
        "  v_texcoord = u_texmatrix.xy * a_vertex.xy + u_texmatrix.zw;\n"
        "  vec4 texbounds = vec4(v_texcoord - u_texbounds.xy,\n"
        "                        u_texbounds.zw - v_texcoord);\n"
        "  v_texbounds = u_offsetscale.x != 0.0 ?\n"
        "     vec4(texbounds.xz / u_offsetscale.x, texbounds.yw) :\n"
        "     vec4(texbounds.yw / u_offsetscale.y, texbounds.xz);\n"
        "  v_support.x = " MOZ_STRINGIFY(BLUR_ACCEL_RADIUS(u_sigma)) ";\n"
        "  calculate_gauss_coeffs(u_sigma);\n"
        "}\n";
    auto fsSource =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "uniform float u_swizzle;\n"
        "uniform highp vec4 u_texbounds;\n"
        "uniform highp vec2 u_offsetscale;\n"
        "uniform sampler2D u_sampler;\n"
        "uniform sampler2D u_clipmask;\n"
        "in highp vec2 v_cliptc;\n"
        "in highp vec2 v_texcoord;\n"
        "in highp vec4 v_texbounds;\n"
        "in vec4 v_clipdist;\n"
        "flat in vec2 v_gauss_coeffs;\n"
        "flat in ivec2 v_support;\n"
        "out vec4 out_FragColor;\n"
        "void main() {\n"
        "  vec3 gauss_coeff = vec3(v_gauss_coeffs,\n"
        "                          v_gauss_coeffs.y * v_gauss_coeffs.y);\n"
        "  bvec4 inside = greaterThanEqual(v_texbounds, vec4(0.0));\n"
        "  vec4 avg_color = texture(u_sampler, v_texcoord) *\n"
        "                   (all(inside.xy) ? gauss_coeff.x : 0.0);\n"
        "  int support = min(v_support.x,\n"
        "                    " MOZ_STRINGIFY(BLUR_ACCEL_RADIUS_MAX) ");\n"
        "  for (int i = 1; i <= support; i += 2) {\n"
        "    gauss_coeff.xy *= gauss_coeff.yz;\n"
        "    float gauss_coeff_subtotal = gauss_coeff.x;\n"
        "    gauss_coeff.xy *= gauss_coeff.yz;\n"
        "    gauss_coeff_subtotal += gauss_coeff.x;\n"
        "    float gauss_ratio = gauss_coeff.x / gauss_coeff_subtotal;\n"
        "    vec4 curbounds = v_texbounds.xyxy + vec4(-1.0, 1.0, 1.0, -1.0) * float(i);\n"
        "    bvec4 inside0 = greaterThanEqual(curbounds.xyxy, vec4(0.0, 0.0, 1.0, -1.0));\n"
        "    bvec4 inside1 = greaterThanEqual(curbounds.zwzw, vec4(0.0, 0.0, -1.0, 1.0));\n"
        "    vec2 weights0 =\n"
        "      (all(inside0.xy) ? vec2(1.0, gauss_ratio) : vec2(gauss_ratio, 1.0)) -\n"
        "        (all(inside0.zw) ? 0.0 : gauss_ratio);\n"
        "    vec2 weights1 =\n"
        "      (all(inside1.xy) ? vec2(1.0, gauss_ratio) : vec2(gauss_ratio, 1.0)) -\n"
        "        (all(inside1.zw) ? 0.0 : gauss_ratio);\n"
        "    vec2 tc0 = v_texcoord - u_offsetscale * (float(i) + weights0.y);\n"
        "    vec2 tc1 = v_texcoord + u_offsetscale * (float(i) + weights1.y);\n"
        "    avg_color += gauss_coeff_subtotal * (\n"
        "      texture(u_sampler, tc0) * weights0.x +\n"
        "      texture(u_sampler, tc1) * weights1.x);\n"
        "  }\n"
        "  float clip = texture(u_clipmask, v_cliptc).r;\n"
        "  vec2 dist = min(v_clipdist.xy, v_clipdist.zw);\n"
        "  float aa = clamp(min(dist.x, dist.y), 0.0, 1.0);\n"
        "  out_FragColor = clip * aa * u_color * float(all(inside.zw)) *\n"
        "                  mix(avg_color, avg_color.rrrr, u_swizzle);\n"
        "}\n";
    RefPtr<WebGLShader> vsId = mWebgl->CreateShader(LOCAL_GL_VERTEX_SHADER);
    mWebgl->ShaderSource(*vsId, vsSource);
    mWebgl->CompileShader(*vsId);
    if (!mWebgl->GetCompileResult(*vsId).success) {
      return false;
    }
    RefPtr<WebGLShader> fsId = mWebgl->CreateShader(LOCAL_GL_FRAGMENT_SHADER);
    mWebgl->ShaderSource(*fsId, fsSource);
    mWebgl->CompileShader(*fsId);
    if (!mWebgl->GetCompileResult(*fsId).success) {
      return false;
    }
    mBlurProgram = mWebgl->CreateProgram();
    mWebgl->AttachShader(*mBlurProgram, *vsId);
    mWebgl->AttachShader(*mBlurProgram, *fsId);
    mWebgl->BindAttribLocation(*mBlurProgram, 0, "a_vertex");
    mWebgl->LinkProgram(*mBlurProgram);
    if (!mWebgl->GetLinkResult(*mBlurProgram).success) {
      return false;
    }
    mBlurProgramViewport = GetUniformLocation(mBlurProgram, "u_viewport");
    mBlurProgramTransform = GetUniformLocation(mBlurProgram, "u_transform");
    mBlurProgramTexMatrix = GetUniformLocation(mBlurProgram, "u_texmatrix");
    mBlurProgramTexBounds = GetUniformLocation(mBlurProgram, "u_texbounds");
    mBlurProgramOffsetScale = GetUniformLocation(mBlurProgram, "u_offsetscale");
    mBlurProgramSigma = GetUniformLocation(mBlurProgram, "u_sigma");
    mBlurProgramColor = GetUniformLocation(mBlurProgram, "u_color");
    mBlurProgramSwizzle = GetUniformLocation(mBlurProgram, "u_swizzle");
    mBlurProgramSampler = GetUniformLocation(mBlurProgram, "u_sampler");
    mBlurProgramClipMask = GetUniformLocation(mBlurProgram, "u_clipmask");
    mBlurProgramClipBounds = GetUniformLocation(mBlurProgram, "u_clipbounds");
    if (!mBlurProgramViewport || !mBlurProgramTransform ||
        !mBlurProgramTexMatrix || !mBlurProgramTexBounds ||
        !mBlurProgramOffsetScale || !mBlurProgramSigma || !mBlurProgramColor ||
        !mBlurProgramSwizzle || !mBlurProgramSampler || !mBlurProgramClipMask ||
        !mBlurProgramClipBounds) {
      return false;
    }
    mWebgl->UseProgram(mBlurProgram);
    UniformData(LOCAL_GL_INT, mBlurProgramSampler, Array<int32_t, 1>{0});
    UniformData(LOCAL_GL_INT, mBlurProgramClipMask, Array<int32_t, 1>{1});
  }
  if (!mFilterProgram) {
    auto vsSource =
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_clipbounds;\n"
        "uniform vec4 u_transform;\n"
        "uniform vec4 u_texmatrix;\n"
        "attribute vec3 a_vertex;\n"
        "varying vec2 v_cliptc;\n"
        "varying vec2 v_texcoord;\n"
        "varying vec4 v_clipdist;\n"
        "void main() {\n"
        "  vec2 vertex = u_transform.xy * a_vertex.xy + u_transform.zw;\n"
        "  gl_Position = vec4(vertex * 2.0 / u_viewport - 1.0, 0.0, 1.0);\n"
        "  v_cliptc = vertex / u_viewport;\n"
        "  v_clipdist = vec4(vertex - u_clipbounds.xy,\n"
        "                    u_clipbounds.zw - vertex);\n"
        "  v_texcoord = u_texmatrix.xy * a_vertex.xy + u_texmatrix.zw;\n"
        "}\n";
    auto fsSource =
        "precision mediump float;\n"
        "uniform vec4 u_texbounds;\n"
        "uniform mat4 u_colormatrix;\n"
        "uniform vec4 u_coloroffset;\n"
        "uniform sampler2D u_sampler;\n"
        "uniform sampler2D u_clipmask;\n"
        "varying highp vec2 v_cliptc;\n"
        "varying highp vec2 v_texcoord;\n"
        "varying vec4 v_clipdist;\n"
        "bool check_bounds(vec2 tc) {\n"
        "  return all(greaterThanEqual(\n"
        "             vec4(tc, u_texbounds.zw), vec4(u_texbounds.xy, tc)));\n"
        "}\n"
        "void main() {\n"
        "  vec4 color = check_bounds(v_texcoord) ?\n"
        "      texture2D(u_sampler, v_texcoord) : vec4(0.0);\n"
        "  if (color.a != 0.0) color.rgb /= color.a;\n"
        "  color = clamp(u_colormatrix * color + u_coloroffset, 0.0, 1.0);\n"
        "  color.rgb *= color.a;\n"
        "  float clip = texture2D(u_clipmask, v_cliptc).r;\n"
        "  vec2 dist = min(v_clipdist.xy, v_clipdist.zw);\n"
        "  float aa = clamp(min(dist.x, dist.y), 0.0, 1.0);\n"
        "  gl_FragColor = clip * aa * color;\n"
        "}\n";
    RefPtr<WebGLShader> vsId = mWebgl->CreateShader(LOCAL_GL_VERTEX_SHADER);
    mWebgl->ShaderSource(*vsId, vsSource);
    mWebgl->CompileShader(*vsId);
    if (!mWebgl->GetCompileResult(*vsId).success) {
      return false;
    }
    RefPtr<WebGLShader> fsId = mWebgl->CreateShader(LOCAL_GL_FRAGMENT_SHADER);
    mWebgl->ShaderSource(*fsId, fsSource);
    mWebgl->CompileShader(*fsId);
    if (!mWebgl->GetCompileResult(*fsId).success) {
      return false;
    }
    mFilterProgram = mWebgl->CreateProgram();
    mWebgl->AttachShader(*mFilterProgram, *vsId);
    mWebgl->AttachShader(*mFilterProgram, *fsId);
    mWebgl->BindAttribLocation(*mFilterProgram, 0, "a_vertex");
    mWebgl->LinkProgram(*mFilterProgram);
    if (!mWebgl->GetLinkResult(*mFilterProgram).success) {
      return false;
    }
    mFilterProgramViewport = GetUniformLocation(mFilterProgram, "u_viewport");
    mFilterProgramTransform = GetUniformLocation(mFilterProgram, "u_transform");
    mFilterProgramTexMatrix = GetUniformLocation(mFilterProgram, "u_texmatrix");
    mFilterProgramTexBounds = GetUniformLocation(mFilterProgram, "u_texbounds");
    mFilterProgramColorMatrix =
        GetUniformLocation(mFilterProgram, "u_colormatrix");
    mFilterProgramColorOffset =
        GetUniformLocation(mFilterProgram, "u_coloroffset");
    mFilterProgramSampler = GetUniformLocation(mFilterProgram, "u_sampler");
    mFilterProgramClipMask = GetUniformLocation(mFilterProgram, "u_clipmask");
    mFilterProgramClipBounds =
        GetUniformLocation(mFilterProgram, "u_clipbounds");
    if (!mFilterProgramViewport || !mFilterProgramTransform ||
        !mFilterProgramTexMatrix || !mFilterProgramTexBounds ||
        !mFilterProgramColorMatrix || !mFilterProgramColorOffset ||
        !mFilterProgramSampler || !mFilterProgramClipMask ||
        !mFilterProgramClipBounds) {
      return false;
    }
    mWebgl->UseProgram(mFilterProgram);
    UniformData(LOCAL_GL_INT, mFilterProgramSampler, Array<int32_t, 1>{0});
    UniformData(LOCAL_GL_INT, mFilterProgramClipMask, Array<int32_t, 1>{1});
  }
  return true;
}

void SharedContextWebgl::EnableScissor(const IntRect& aRect, bool aForce) {
  // Only update scissor state if it actually changes.
  IntRect rect = !aForce && mTargetHandle
                     ? aRect + mTargetHandle->GetBounds().TopLeft()
                     : aRect;
  if (!mLastScissor.IsEqualEdges(rect)) {
    mLastScissor = rect;
    mWebgl->Scissor(rect.x, rect.y, rect.width, rect.height);
  }
  if (!mScissorEnabled) {
    mScissorEnabled = true;
    mWebgl->SetEnabled(LOCAL_GL_SCISSOR_TEST, {}, true);
  }
}

void SharedContextWebgl::DisableScissor(bool aForce) {
  if (!aForce && mTargetHandle) {
    EnableScissor(IntRect(IntPoint(), mViewportSize));
    return;
  }
  if (mScissorEnabled) {
    mScissorEnabled = false;
    mWebgl->SetEnabled(LOCAL_GL_SCISSOR_TEST, {}, false);
  }
}

inline ColorPattern DrawTargetWebgl::GetClearPattern() const {
  return ColorPattern(
      DeviceColor(0.0f, 0.0f, 0.0f, IsOpaque(mFormat) ? 1.0f : 0.0f));
}

template <typename R>
inline RectDouble DrawTargetWebgl::TransformDouble(const R& aRect) const {
  return MatrixDouble(mTransform).TransformBounds(WidenToDouble(aRect));
}

// Check if the transformed rect clips to the viewport.
inline Maybe<Rect> DrawTargetWebgl::RectClippedToViewport(
    const RectDouble& aRect) const {
  if (!mTransform.PreservesAxisAlignedRectangles()) {
    return Nothing();
  }

  return Some(NarrowToFloat(aRect.SafeIntersect(RectDouble(GetRect()))));
}

// Ensure that the rect, after transform, is within reasonable precision limits
// such that when transformed and clipped in the shader it will not round bits
// from the mantissa in a way that will diverge in a noticeable way from path
// geometry calculated by the path fallback.
template <typename R>
static inline bool RectInsidePrecisionLimits(const R& aRect) {
  return R(-(1 << 20), -(1 << 20), 2 << 20, 2 << 20).Contains(aRect);
}

void DrawTargetWebgl::ClearRect(const Rect& aRect) {
  if (mIsClear) {
    // No need to clear anything if the entire framebuffer is already clear.
    return;
  }

  RectDouble xformRect = TransformDouble(aRect);
  bool containsViewport = false;
  if (Maybe<Rect> clipped = RectClippedToViewport(xformRect)) {
    // If the rect clips to viewport, just clear the clipped rect
    // to avoid transform issues.
    containsViewport = clipped->Size() == Size(GetSize());
    DrawRect(*clipped, GetClearPattern(),
             DrawOptions(1.0f, CompositionOp::OP_CLEAR), Nothing(), nullptr,
             false);
  } else if (RectInsidePrecisionLimits(xformRect)) {
    // If the rect transform won't stress precision, then just use it.
    DrawRect(aRect, GetClearPattern(),
             DrawOptions(1.0f, CompositionOp::OP_CLEAR));
  } else {
    // Otherwise, using the transform in the shader may lead to inaccuracies, so
    // just fall back.
    MarkSkiaChanged();
    mSkia->ClearRect(aRect);
  }

  // If the clear rectangle encompasses the entire viewport and is not clipped,
  // then mark the target as entirely clear.
  if (containsViewport && !ShouldClip()) {
    mIsClear = true;
  }
}

static inline DeviceColor PremultiplyColor(const DeviceColor& aColor,
                                           float aAlpha = 1.0f) {
  float a = aColor.a * aAlpha;
  return DeviceColor(aColor.r * a, aColor.g * a, aColor.b * a, a);
}

// Attempts to create the framebuffer used for drawing and also any relevant
// non-shared resources. Returns whether or not this succeeded.
bool DrawTargetWebgl::CreateFramebuffer() {
  RefPtr<WebGLContext> webgl = mSharedContext->mWebgl;
  if (!mFramebuffer) {
    mFramebuffer = webgl->CreateFramebuffer();
  }
  if (!mTex) {
    mTex = webgl->CreateTexture();
    mSharedContext->BindAndInitRenderTex(mTex, SurfaceFormat::B8G8R8A8, mSize);
    webgl->BindFramebuffer(LOCAL_GL_FRAMEBUFFER, mFramebuffer);
    webgl::FbAttachInfo attachInfo;
    attachInfo.tex = mTex;
    webgl->FramebufferAttach(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                             LOCAL_GL_TEXTURE_2D, attachInfo);
    webgl->Viewport(0, 0, mSize.width, mSize.height);
    mSharedContext->DisableScissor();
    DeviceColor color = PremultiplyColor(GetClearPattern().mColor);
    webgl->ClearColor(color.b, color.g, color.r, color.a);
    webgl->Clear(LOCAL_GL_COLOR_BUFFER_BIT);
    mSharedContext->ClearTarget();
    mSharedContext->AddUntrackedTextureMemory(mTex);
  }
  return true;
}

void DrawTargetWebgl::CopySurface(SourceSurface* aSurface,
                                  const IntRect& aSourceRect,
                                  const IntPoint& aDestination) {
  // Intersect the source and destination rectangles with the viewport bounds.
  IntRect destRect =
      IntRect(aDestination, aSourceRect.Size()).SafeIntersect(GetRect());
  IntRect srcRect = destRect - aDestination + aSourceRect.TopLeft();
  if (srcRect.IsEmpty()) {
    return;
  }

  if (mSkiaValid) {
    if (mSkiaLayer) {
      if (destRect.Contains(GetRect())) {
        // If the the destination would override the entire layer, discard the
        // layer.
        mSkiaLayer = false;
      } else if (!IsOpaque(aSurface->GetFormat())) {
        // If the surface is not opaque, copying it into the layer results in
        // unintended blending rather than a copy to the destination.
        FlattenSkia();
      }
    } else {
      // If there is no layer, copying is safe.
      MarkSkiaChanged();
    }
    mSkia->CopySurface(aSurface, srcRect, destRect.TopLeft());
    return;
  }

  IntRect samplingRect;
  if (!mSharedContext->IsCompatibleSurface(aSurface)) {
    // If this data surface completely overwrites the framebuffer, then just
    // copy it to the Skia target.
    if (destRect.Contains(GetRect())) {
      MarkSkiaChanged(true);
      mSkia->DetachAllSnapshots();
      mSkiaNoClip->CopySurface(aSurface, srcRect, destRect.TopLeft());
      return;
    }

    // CopySurface usually only samples a surface once, so don't cache the
    // entire surface as it is unlikely to be reused. Limit it to the used
    // source rectangle instead.
    IntRect surfaceRect = aSurface->GetRect();
    if (!srcRect.IsEqualEdges(surfaceRect)) {
      samplingRect = srcRect.SafeIntersect(surfaceRect) - surfaceRect.TopLeft();
    }
  }

  Matrix matrix = Matrix::Translation(destRect.TopLeft() - srcRect.TopLeft());
  SurfacePattern pattern(aSurface, ExtendMode::CLAMP, matrix,
                         SamplingFilter::POINT, samplingRect);
  DrawRect(Rect(destRect), pattern, DrawOptions(1.0f, CompositionOp::OP_SOURCE),
           Nothing(), nullptr, false, false);
}

void DrawTargetWebgl::PushClip(const Path* aPath) {
  if (aPath && aPath->GetBackendType() == BackendType::SKIA) {
    // Detect if the path is really just a rect to simplify caching.
    if (Maybe<Rect> rect = aPath->AsRect()) {
      PushClipRect(*rect);
      return;
    }
  }

  mClipChanged = true;
  mRefreshClipState = true;
  mSkia->PushClip(aPath);

  mClipStack.push_back({GetTransform(), Rect(), aPath});
}

void DrawTargetWebgl::PushClipRect(const Rect& aRect) {
  mClipChanged = true;
  mRefreshClipState = true;
  mSkia->PushClipRect(aRect);

  mClipStack.push_back({GetTransform(), aRect, nullptr});
}

void DrawTargetWebgl::PushDeviceSpaceClipRects(const IntRect* aRects,
                                               uint32_t aCount) {
  mClipChanged = true;
  mRefreshClipState = true;
  mSkia->PushDeviceSpaceClipRects(aRects, aCount);

  for (uint32_t i = 0; i < aCount; i++) {
    mClipStack.push_back({Matrix(), Rect(aRects[i]), nullptr});
  }
}

void DrawTargetWebgl::PopClip() {
  if (mClipStack.empty()) {
    return;
  }

  mClipChanged = true;
  mRefreshClipState = true;
  mSkia->PopClip();

  mClipStack.pop_back();
}

bool DrawTargetWebgl::RemoveAllClips() {
  if (mClipStack.empty()) {
    return true;
  }
  if (!mSkia->RemoveAllClips()) {
    return false;
  }
  mClipChanged = true;
  mRefreshClipState = true;
  mClipStack.clear();
  return true;
}

bool DrawTargetWebgl::CopyToFallback(DrawTarget* aDT) {
  aDT->RemoveAllClips();
  for (auto& clipStack : mClipStack) {
    aDT->SetTransform(clipStack.mTransform);
    if (clipStack.mPath) {
      aDT->PushClip(clipStack.mPath);
    } else {
      aDT->PushClipRect(clipStack.mRect);
    }
  }
  aDT->SetTransform(GetTransform());

  // An existing data snapshot is required for fallback, as we have to avoid
  // trying to touch the WebGL context, which is assumed to be invalid and not
  // suitable for readback.
  if (HasDataSnapshot()) {
    if (RefPtr<SourceSurface> snapshot = Snapshot()) {
      aDT->CopySurface(snapshot, snapshot->GetRect(), gfx::IntPoint(0, 0));
      return true;
    }
  }
  return false;
}

enum class SupportsDrawOptionsStatus { No, UnboundedBlend, Yes };

// Whether a given composition operator can be mapped to a WebGL blend mode.
static inline SupportsDrawOptionsStatus SupportsDrawOptions(
    const DrawOptions& aOptions) {
  switch (aOptions.mCompositionOp) {
    case CompositionOp::OP_OVER:
    case CompositionOp::OP_DEST_OVER:
    case CompositionOp::OP_ADD:
    case CompositionOp::OP_DEST_OUT:
    case CompositionOp::OP_ATOP:
    case CompositionOp::OP_SOURCE:
    case CompositionOp::OP_CLEAR:
    case CompositionOp::OP_MULTIPLY:
    case CompositionOp::OP_SCREEN:
      return SupportsDrawOptionsStatus::Yes;
    case CompositionOp::OP_IN:
    case CompositionOp::OP_OUT:
    case CompositionOp::OP_DEST_IN:
    case CompositionOp::OP_DEST_ATOP:
      return SupportsDrawOptionsStatus::UnboundedBlend;
    default:
      return SupportsDrawOptionsStatus::No;
  }
}

bool DrawTargetWebgl::SupportsDrawOptions(const DrawOptions& aOptions,
                                          const Rect& aRect) {
  switch (mozilla::gfx::SupportsDrawOptions(aOptions)) {
    case SupportsDrawOptionsStatus::Yes:
      return true;
    case SupportsDrawOptionsStatus::UnboundedBlend:
      if (aRect.IsEmpty()) {
        return false;
      }
      if (Maybe<IntRect> clip = mSkia->GetDeviceClipRect(false)) {
        if (!clip->IsEmpty() && clip->Contains(GetRect())) {
          clip = Some(GetRect());
        }
        Rect clipF(*clip);
        if (aRect.Contains(clipF) || aRect.WithinEpsilonOf(clipF, 1e-3f)) {
          return true;
        }
        return false;
      }
      return false;
    default:
      return false;
  }
}

// Whether the composition operator requires multiple blend stages to
// approximate with WebGL blend modes.
inline uint8_t SharedContextWebgl::RequiresMultiStageBlend(
    const DrawOptions& aOptions, DrawTargetWebgl* aDT) {
  switch (aOptions.mCompositionOp) {
    case CompositionOp::OP_MULTIPLY:
      return !IsOpaque(aDT ? aDT->GetFormat()
                           : (mTargetHandle ? mTargetHandle->GetFormat()
                                            : mCurrentTarget->GetFormat()))
                 ? 2
                 : 0;
    default:
      return 0;
  }
}

static inline bool SupportsExtendMode(const SurfacePattern& aPattern) {
  switch (aPattern.mExtendMode) {
    case ExtendMode::CLAMP:
      return true;
    case ExtendMode::REPEAT:
    case ExtendMode::REPEAT_X:
    case ExtendMode::REPEAT_Y:
      if ((!aPattern.mSurface ||
           aPattern.mSurface->GetUnderlyingType() == SurfaceType::WEBGL) &&
          !aPattern.mSamplingRect.IsEmpty()) {
        return false;
      }
      return true;
    default:
      return false;
  }
}

// Whether a pattern can be mapped to an available WebGL shader.
bool SharedContextWebgl::SupportsPattern(const Pattern& aPattern) {
  switch (aPattern.GetType()) {
    case PatternType::COLOR:
      return true;
    case PatternType::SURFACE: {
      auto surfacePattern = static_cast<const SurfacePattern&>(aPattern);
      if (!SupportsExtendMode(surfacePattern)) {
        return false;
      }
      if (surfacePattern.mSurface) {
        // If the surface is already uploaded to a texture, then just use it.
        if (IsCompatibleSurface(surfacePattern.mSurface)) {
          return true;
        }

        IntSize size = surfacePattern.mSurface->GetSize();
        // The maximum size a surface can be before triggering a fallback to
        // software. Bound the maximum surface size by the actual texture size
        // limit.
        int32_t maxSize = int32_t(
            std::min(StaticPrefs::gfx_canvas_accelerated_max_surface_size(),
                     mMaxTextureSize));
        // Check if either of the surface dimensions or the sampling rect,
        // if supplied, exceed the maximum.
        if (std::max(size.width, size.height) > maxSize &&
            (surfacePattern.mSamplingRect.IsEmpty() ||
             std::max(surfacePattern.mSamplingRect.width,
                      surfacePattern.mSamplingRect.height) > maxSize)) {
          return false;
        }
      }
      return true;
    }
    default:
      // Patterns other than colors and surfaces are currently not accelerated.
      return false;
  }
}

bool DrawTargetWebgl::DrawRect(const Rect& aRect, const Pattern& aPattern,
                               const DrawOptions& aOptions,
                               Maybe<DeviceColor> aMaskColor,
                               RefPtr<TextureHandle>* aHandle,
                               bool aTransformed, bool aClipped,
                               bool aAccelOnly, bool aForceUpdate,
                               const StrokeOptions* aStrokeOptions) {
  // If there is nothing to draw, then don't draw...
  if (aRect.IsEmpty() || mSkia->IsClipEmpty()) {
    return true;
  }

  // If we're already drawing directly to the WebGL context, then we want to
  // continue to do so. However, if we're drawing into a Skia layer over the
  // WebGL context, then we need to be careful to avoid repeatedly clearing
  // and flushing the layer if we hit a drawing request that can be accelerated
  // in between layered drawing requests, as clearing and flushing the layer
  // can be significantly expensive when repeated. So when a Skia layer is
  // active, if it is possible to continue drawing into the layer, then don't
  // accelerate the drawing request.
  if (mWebglValid || (mSkiaLayer && !mLayerDepth &&
                      (aAccelOnly || !SupportsLayering(aOptions)))) {
    // If we get here, either the WebGL context is being directly drawn to
    // or we are going to flush the Skia layer to it before doing so. The shared
    // context still needs to be claimed and prepared for drawing. If this
    // fails, we just fall back to drawing with Skia below.
    if (SupportsDrawOptions(aOptions, aRect) && PrepareContext(aClipped)) {
      // The shared context is claimed and the framebuffer is now valid, so try
      // accelerated drawing.
      return mSharedContext->DrawRectAccel(
          aRect, aPattern, aOptions, aMaskColor, aHandle, aTransformed,
          aClipped, aAccelOnly, aForceUpdate, aStrokeOptions);
    }
  }

  // Either there is no valid WebGL target to draw into, or we failed to prepare
  // it for drawing. The only thing we can do at this point is fall back to
  // drawing with Skia. If the request explicitly requires accelerated drawing,
  // then draw nothing before returning failure.
  if (!aAccelOnly) {
    DrawRectFallback(aRect, aPattern, aOptions, aMaskColor, aTransformed,
                     aClipped, aStrokeOptions);
  }
  return false;
}

void DrawTargetWebgl::DrawRectFallback(const Rect& aRect,
                                       const Pattern& aPattern,
                                       const DrawOptions& aOptions,
                                       Maybe<DeviceColor> aMaskColor,
                                       bool aTransformed, bool aClipped,
                                       const StrokeOptions* aStrokeOptions) {
  // Invalidate the WebGL target and prepare the Skia target for drawing.
  MarkSkiaChanged(aOptions);

  if (aTransformed) {
    // If transforms are requested, then just translate back to FillRect.
    if (aMaskColor) {
      mSkia->Mask(ColorPattern(*aMaskColor), aPattern, aOptions);
    } else if (aStrokeOptions) {
      mSkia->StrokeRect(aRect, aPattern, *aStrokeOptions, aOptions);
    } else {
      mSkia->FillRect(aRect, aPattern, aOptions);
    }
  } else if (aClipped) {
    // If no transform was requested but clipping is still required, then
    // temporarily reset the transform before translating to FillRect.
    mSkia->SetTransform(Matrix());
    if (aMaskColor) {
      auto surfacePattern = static_cast<const SurfacePattern&>(aPattern);
      if (surfacePattern.mSamplingRect.IsEmpty()) {
        mSkia->MaskSurface(ColorPattern(*aMaskColor), surfacePattern.mSurface,
                           aRect.TopLeft(), aOptions);
      } else {
        mSkia->Mask(ColorPattern(*aMaskColor), aPattern, aOptions);
      }
    } else if (aStrokeOptions) {
      mSkia->StrokeRect(aRect, aPattern, *aStrokeOptions, aOptions);
    } else {
      mSkia->FillRect(aRect, aPattern, aOptions);
    }
    mSkia->SetTransform(mTransform);
  } else if (aPattern.GetType() == PatternType::SURFACE) {
    // No transform nor clipping was requested, so it is essentially just a
    // copy.
    auto surfacePattern = static_cast<const SurfacePattern&>(aPattern);
    IntRect destRect = RoundedOut(aRect);
    IntRect srcRect =
        destRect - IntPoint::Round(surfacePattern.mMatrix.GetTranslation());
    mSkia->CopySurface(surfacePattern.mSurface, srcRect, destRect.TopLeft());
  } else {
    MOZ_ASSERT(false);
  }
}

inline already_AddRefed<WebGLTexture> SharedContextWebgl::GetCompatibleSnapshot(
    SourceSurface* aSurface, RefPtr<TextureHandle>* aHandle,
    bool aCheckTarget) const {
  if (aSurface->GetUnderlyingType() == SurfaceType::WEBGL) {
    RefPtr<SourceSurfaceWebgl> webglSurf =
        aSurface->GetUnderlyingSurface().downcast<SourceSurfaceWebgl>();
    if (this == webglSurf->mSharedContext) {
      // If there is a snapshot copy in a texture handle, use that.
      if (webglSurf->mHandle) {
        if (aHandle) {
          *aHandle = webglSurf->mHandle;
        }
        return do_AddRef(
            webglSurf->mHandle->GetBackingTexture()->GetWebGLTexture());
      }
      if (RefPtr<DrawTargetWebgl> webglDT = webglSurf->GetTarget()) {
        // If there is a copy-on-write reference to a target, use its backing
        // texture directly. This is only safe if the targets don't match, but
        // MarkChanged should ensure that any snapshots were copied into a
        // texture handle before we ever get here.
        if (!aCheckTarget || !IsCurrentTarget(webglDT)) {
          return do_AddRef(webglDT->mTex);
        }
      }
    }
  }
  return nullptr;
}

inline bool SharedContextWebgl::IsCompatibleSurface(
    SourceSurface* aSurface) const {
  return bool(RefPtr<WebGLTexture>(GetCompatibleSnapshot(aSurface)));
}

bool SharedContextWebgl::UploadSurface(DataSourceSurface* aData,
                                       SurfaceFormat aFormat,
                                       const IntRect& aSrcRect,
                                       const IntPoint& aDstOffset, bool aInit,
                                       bool aZero,
                                       const RefPtr<WebGLTexture>& aTex) {
  webgl::TexUnpackBlobDesc texDesc = {LOCAL_GL_TEXTURE_2D};
  IntRect srcRect(aSrcRect);
  IntPoint dstOffset(aDstOffset);
  if (srcRect.IsEmpty()) {
    return true;
  }
  Maybe<DataSourceSurface::ScopedMap> map;
  if (aData) {
    // If the source rect could not possibly overlap the surface, then it is
    // effectively empty with nothing to upload.
    srcRect = srcRect.SafeIntersect(IntRect(IntPoint(0, 0), aData->GetSize()));
    if (srcRect.IsEmpty()) {
      return true;
    }
    // If there is a non-empty rect remaining, then ensure the dest offset
    // reflects the change in source rect.
    dstOffset += srcRect.TopLeft() - aSrcRect.TopLeft();

    // Ensure source data matches the expected format size.
    int32_t bpp = BytesPerPixel(aFormat);
    if (bpp != BytesPerPixel(aData->GetFormat())) {
      return false;
    }

    // The surface needs to be uploaded to its backing texture either to
    // initialize or update the texture handle contents. Map the data
    // contents of the surface so it can be read.
    map.emplace(aData, DataSourceSurface::READ);
    if (!map->IsMapped()) {
      return false;
    }
    int32_t stride = map->GetStride();
    // Get the data pointer range considering the sampling rect offset and
    // size.
    Span<const uint8_t> range(
        map->GetData() + srcRect.y * size_t(stride) + srcRect.x * bpp,
        std::max(srcRect.height - 1, 0) * size_t(stride) + srcRect.width * bpp);
    texDesc.cpuData = Some(range);
    // If the stride happens to be 4 byte aligned, assume that is the
    // desired alignment regardless of format (even A8). Otherwise, we
    // default to byte alignment.
    texDesc.unpacking.alignmentInTypeElems = stride % 4 ? 1 : 4;
    texDesc.unpacking.rowLength = stride / bpp;
  } else if (aZero) {
    // Create a PBO filled with zero data to initialize the texture data and
    // avoid slow initialization inside WebGL.
    if (srcRect.TopLeft() != IntPoint(0, 0)) {
      MOZ_ASSERT_UNREACHABLE("Invalid origin for texture initialization.");
      return false;
    }
    auto stride = GetAlignedStride<4>(srcRect.width, BytesPerPixel(aFormat));
    if (stride.isNothing()) {
      MOZ_ASSERT_UNREACHABLE("Invalid stride for texture initialization.");
      return false;
    }
    CheckedInt<size_t> size =
        CheckedInt<size_t>(stride.value()) * srcRect.height;
    if (!size.isValid()) {
      MOZ_ASSERT_UNREACHABLE(
          "Invalid stride * srcRect.height for texture initialization.");
      return false;
    }
    if (!mZeroBuffer || size.value() > mZeroSize) {
      ClearZeroBuffer();
      mZeroBuffer = mWebgl->CreateBuffer();
      mZeroSize = size.value();
      mWebgl->BindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, mZeroBuffer);
      // WebGL will zero initialize the empty buffer, so we don't send zero data
      // explicitly.
      mWebgl->BufferData(LOCAL_GL_PIXEL_UNPACK_BUFFER, mZeroSize, nullptr,
                         LOCAL_GL_STATIC_DRAW);
      AddUntrackedTextureMemory(mZeroBuffer);
    } else {
      mWebgl->BindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, mZeroBuffer);
    }
    texDesc.pboOffset = Some(0);
  }
  texDesc.size = uvec3(uint32_t(srcRect.width), uint32_t(srcRect.height), 1);
  // Upload as RGBA8 to avoid swizzling during upload. Surfaces provide
  // data as BGRA, but we manually swizzle that in the shader. An A8
  // surface will be stored as an R8 texture that will also be swizzled
  // in the shader.
  GLenum intFormat =
      aFormat == SurfaceFormat::A8 ? LOCAL_GL_R8 : LOCAL_GL_RGBA8;
  GLenum extFormat =
      aFormat == SurfaceFormat::A8 ? LOCAL_GL_RED : LOCAL_GL_RGBA;
  webgl::PackingInfo texPI = {extFormat, LOCAL_GL_UNSIGNED_BYTE};
  // Do the (partial) upload for the shared or standalone texture.
  if (aTex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, aTex);
  }
  mWebgl->TexImage(0, aInit ? intFormat : 0,
                   {uint32_t(dstOffset.x), uint32_t(dstOffset.y), 0}, texPI,
                   texDesc);
  if (aTex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, mLastTexture);
  }
  if (!aData && aZero) {
    mWebgl->BindBuffer(LOCAL_GL_PIXEL_UNPACK_BUFFER, 0);
  }
  return true;
}

void SharedContextWebgl::UploadSurfaceToHandle(
    const RefPtr<DataSourceSurface>& aData, const IntPoint& aSrcOffset,
    const RefPtr<TextureHandle>& aHandle) {
  BackingTexture* backing = aHandle->GetBackingTexture();
  RefPtr<WebGLTexture> tex = backing->GetWebGLTexture();
  if (mLastTexture != tex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, tex);
    mLastTexture = tex;
  }
  if (!backing->IsInitialized()) {
    backing->MarkInitialized();
    InitTexParameters(tex);
    if (aHandle->GetSize() != backing->GetSize()) {
      UploadSurface(nullptr, backing->GetFormat(),
                    IntRect(IntPoint(), backing->GetSize()), IntPoint(), true,
                    true);
    }
  }
  UploadSurface(
      aData, aHandle->GetFormat(), IntRect(aSrcOffset, aHandle->GetSize()),
      aHandle->GetBounds().TopLeft(), aHandle->GetSize() == backing->GetSize());
  // Signal that we had to upload new data to the texture cache.
  mCurrentTarget->mProfile.OnCacheMiss();
}

void SharedContextWebgl::BindAndInitRenderTex(const RefPtr<WebGLTexture>& aTex,
                                              SurfaceFormat aFormat,
                                              const IntSize& aSize) {
  mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, aTex);
  mWebgl->TexStorage(
      LOCAL_GL_TEXTURE_2D, 1,
      aFormat == SurfaceFormat::A8 ? LOCAL_GL_R8 : LOCAL_GL_RGBA8,
      {uint32_t(aSize.width), uint32_t(aSize.height), 1});
  InitTexParameters(aTex);
  ClearLastTexture();
}

void SharedContextWebgl::InitRenderTex(BackingTexture* aBacking) {
  // Initialize the backing texture if necessary.
  if (!aBacking->IsInitialized()) {
    // If the backing texture is uninitialized, it needs its sampling parameters
    // set for later use.
    BindAndInitRenderTex(aBacking->GetWebGLTexture(), aBacking->GetFormat(),
                         aBacking->GetSize());
  }
}

void SharedContextWebgl::ClearRenderTex(BackingTexture* aBacking) {
  if (!aBacking->IsInitialized()) {
    aBacking->MarkInitialized();
    // WebGL implicitly clears the backing texture the first time it is used.
  } else {
    // Ensure the mask background is clear.
    mWebgl->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    mWebgl->Clear(LOCAL_GL_COLOR_BUFFER_BIT);
  }
}

void SharedContextWebgl::BindScratchFramebuffer(TextureHandle* aHandle,
                                                bool aInit,
                                                const IntSize& aViewportSize) {
  BackingTexture* backing = aHandle->GetBackingTexture();
  if (aInit) {
    InitRenderTex(backing);
  }

  // Set up a scratch framebuffer to render to the appropriate sub-texture of
  // the backing texture.
  if (!mScratchFramebuffer) {
    mScratchFramebuffer = mWebgl->CreateFramebuffer();
  }
  mWebgl->BindFramebuffer(LOCAL_GL_FRAMEBUFFER, mScratchFramebuffer);
  webgl::FbAttachInfo attachInfo;
  attachInfo.tex = backing->GetWebGLTexture();
  mWebgl->FramebufferAttach(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                            LOCAL_GL_TEXTURE_2D, attachInfo);
  IntRect bounds = aHandle->GetBounds();
  if (!aViewportSize.IsEmpty()) {
    bounds.SizeTo(Min(bounds.Size(), aViewportSize));
  }
  mWebgl->Viewport(bounds.x, bounds.y, bounds.width, bounds.height);

  if (aInit) {
    EnableScissor(bounds, true);
    ClearRenderTex(backing);
  }
}

// Allocate a new texture handle backed by either a standalone texture or as a
// sub-texture of a larger shared texture.
already_AddRefed<TextureHandle> SharedContextWebgl::AllocateTextureHandle(
    SurfaceFormat aFormat, const IntSize& aSize, bool aAllowShared,
    bool aRenderable, const WebGLTexture* aAvoid) {
  RefPtr<TextureHandle> handle;
  // Calculate the bytes that would be used by this texture handle, and prune
  // enough other textures to ensure we have that much usable texture space
  // available to allocate.
  size_t usedBytes = BackingTexture::UsedBytes(aFormat, aSize);
  PruneTextureMemory(usedBytes, false);
  // The requested page size for shared textures.
  int32_t pageSize = int32_t(std::min(
      StaticPrefs::gfx_canvas_accelerated_shared_page_size(), mMaxTextureSize));
  if (aAllowShared && std::max(aSize.width, aSize.height) <= pageSize / 2) {
    // Ensure that the surface is no bigger than a quadrant of a shared texture
    // page. If so, try to allocate it to a shared texture. Look for any
    // existing shared texture page with a matching format and allocate
    // from that if possible.
    for (auto& shared : mSharedTextures) {
      if (shared->GetFormat() == aFormat &&
          shared->IsRenderable() == aRenderable &&
          shared->GetWebGLTexture() != aAvoid) {
        bool wasEmpty = !shared->HasAllocatedHandles();
        handle = shared->Allocate(aSize);
        if (handle) {
          if (wasEmpty) {
            // If the page was previously empty, then deduct it from the
            // empty memory reserves.
            mEmptyTextureMemory -= shared->UsedBytes();
          }
          break;
        }
      }
    }
    // If we couldn't find an existing shared texture page with matching
    // format, then allocate a new page to put the request in.
    if (!handle) {
      if (RefPtr<WebGLTexture> tex = mWebgl->CreateTexture()) {
        RefPtr<SharedTexture> shared =
            new SharedTexture(IntSize(pageSize, pageSize), aFormat, tex);
        if (aRenderable) {
          shared->MarkRenderable();
        }
        mSharedTextures.push_back(shared);
        AddTextureMemory(shared);
        handle = shared->Allocate(aSize);
      }
    }
  } else {
    // The surface wouldn't fit in a shared texture page, so we need to
    // allocate a standalone texture for it instead.
    if (RefPtr<WebGLTexture> tex = mWebgl->CreateTexture()) {
      RefPtr<StandaloneTexture> standalone =
          new StandaloneTexture(aSize, aFormat, tex);
      if (aRenderable) {
        standalone->MarkRenderable();
      }
      mStandaloneTextures.push_back(standalone);
      AddTextureMemory(standalone);
      handle = standalone;
    }
  }

  if (!handle) {
    return nullptr;
  }

  // Insert the new texture handle into the front of the MRU list and
  // update used space for it.
  mTextureHandles.insertFront(handle);
  ++mNumTextureHandles;
  mUsedTextureMemory += handle->UsedBytes();

  return handle.forget();
}

static inline SamplingFilter GetSamplingFilter(const Pattern& aPattern) {
  return aPattern.GetType() == PatternType::SURFACE
             ? static_cast<const SurfacePattern&>(aPattern).mSamplingFilter
             : SamplingFilter::GOOD;
}

static inline bool UseNearestFilter(const Pattern& aPattern) {
  return GetSamplingFilter(aPattern) == SamplingFilter::POINT;
}

// Determine if the rectangle is still axis-aligned and pixel-aligned.
static inline Maybe<IntRect> IsAlignedRect(bool aTransformed,
                                           const Matrix& aCurrentTransform,
                                           const Rect& aRect) {
  if (!aTransformed || aCurrentTransform.HasOnlyIntegerTranslation()) {
    auto intRect = RoundedToInt(aRect);
    if (aRect.WithinEpsilonOf(Rect(intRect), 1.0e-3f)) {
      if (aTransformed) {
        intRect += RoundedToInt(aCurrentTransform.GetTranslation());
      }
      return Some(intRect);
    }
  }
  return Nothing();
}

Maybe<uint32_t> SharedContextWebgl::GetUniformLocation(
    const RefPtr<WebGLProgram>& aProg, const std::string& aName) const {
  if (!aProg || !aProg->LinkInfo()) {
    return Nothing();
  }

  for (const auto& activeUniform : aProg->LinkInfo()->active.activeUniforms) {
    if (activeUniform.block_index != -1) continue;

    auto locName = activeUniform.name;
    const auto indexed = webgl::ParseIndexed(locName);
    if (indexed) {
      locName = indexed->name;
    }

    const auto baseLength = locName.size();
    for (const auto& pair : activeUniform.locByIndex) {
      if (indexed) {
        locName.erase(baseLength);  // Erase previous "[N]".
        locName += '[';
        locName += std::to_string(pair.first);
        locName += ']';
      }
      if (locName == aName || locName == aName + "[0]") {
        return Some(pair.second);
      }
    }
  }

  return Nothing();
}

template <class T>
struct IsUniformDataValT : std::false_type {};
template <>
struct IsUniformDataValT<webgl::UniformDataVal> : std::true_type {};
template <>
struct IsUniformDataValT<float> : std::true_type {};
template <>
struct IsUniformDataValT<int32_t> : std::true_type {};
template <>
struct IsUniformDataValT<uint32_t> : std::true_type {};

template <typename T, typename = std::enable_if_t<IsUniformDataValT<T>::value>>
inline Range<const webgl::UniformDataVal> AsUniformDataVal(
    const Range<const T>& data) {
  return {data.begin().template ReinterpretCast<const webgl::UniformDataVal>(),
          data.end().template ReinterpretCast<const webgl::UniformDataVal>()};
}

template <class T, size_t N>
inline void SharedContextWebgl::UniformData(GLenum aFuncElemType,
                                            const Maybe<uint32_t>& aLoc,
                                            const Array<T, N>& aData) {
  // We currently always pass false for transpose. If in the future we need
  // support for transpose then caching needs to take that in to account.
  mWebgl->UniformData(*aLoc, false,
                      AsUniformDataVal(Range<const T>(Span<const T>(aData))));
}

template <class T, size_t N>
void SharedContextWebgl::MaybeUniformData(GLenum aFuncElemType,
                                          const Maybe<uint32_t>& aLoc,
                                          const Array<T, N>& aData,
                                          Maybe<Array<T, N>>& aCached) {
  if (aCached.isNothing() || !(*aCached == aData)) {
    aCached = Some(aData);
    UniformData(aFuncElemType, aLoc, aData);
  }
}

inline void SharedContextWebgl::DrawQuad() {
  mWebgl->DrawArraysInstanced(LOCAL_GL_TRIANGLE_FAN, 0, 4, 1);
}

void SharedContextWebgl::DrawTriangles(const PathVertexRange& aRange) {
  mWebgl->DrawArraysInstanced(LOCAL_GL_TRIANGLES, GLint(aRange.mOffset),
                              GLsizei(aRange.mLength), 1);
}

// Common rectangle and pattern drawing function shared by many DrawTarget
// commands. If aMaskColor is specified, the provided surface pattern will be
// treated as a mask. If aHandle is specified, then the surface pattern's
// texture will be cached in the supplied handle, as opposed to using the
// surface's user data. If aTransformed or aClipped are false, then transforms
// and/or clipping will be disabled. If aAccelOnly is specified, then this
// function will return before it would have otherwise drawn without
// acceleration. If aForceUpdate is specified, then the provided texture handle
// will be respecified with the provided surface.
bool SharedContextWebgl::DrawRectAccel(
    const Rect& aRect, const Pattern& aPattern, const DrawOptions& aOptions,
    Maybe<DeviceColor> aMaskColor, RefPtr<TextureHandle>* aHandle,
    bool aTransformed, bool aClipped, bool aAccelOnly, bool aForceUpdate,
    const StrokeOptions* aStrokeOptions, const PathVertexRange* aVertexRange,
    const Matrix* aRectXform, uint8_t aBlendStage) {
  // If the rect or clip rect is empty, then there is nothing to draw.
  if (aRect.IsEmpty() || mClipRect.IsEmpty()) {
    return true;
  }

  // Check if the drawing options and the pattern support acceleration. Also
  // ensure the framebuffer is prepared for drawing. If not, fall back to using
  // the Skia target. When we need to forcefully update a texture, we must be
  // careful to override any pattern limits, as the caller ensures the pattern
  // is otherwise a supported type.
  if (SupportsDrawOptions(aOptions) == SupportsDrawOptionsStatus::No ||
      (!aForceUpdate && !SupportsPattern(aPattern)) || aStrokeOptions ||
      (!mTargetHandle && !mCurrentTarget->MarkChanged())) {
    // If only accelerated drawing was requested, bail out without software
    // drawing fallback.
    if (!aAccelOnly) {
      MOZ_ASSERT(!aVertexRange);
      mCurrentTarget->DrawRectFallback(aRect, aPattern, aOptions, aMaskColor,
                                       aTransformed, aClipped, aStrokeOptions);
    }
    return false;
  }

  if (!aBlendStage) {
    if (uint8_t numStages = RequiresMultiStageBlend(aOptions)) {
      for (uint8_t stage = 1; stage <= numStages; ++stage) {
        if (!DrawRectAccel(aRect, aPattern, aOptions, aMaskColor, aHandle,
                           aTransformed, aClipped, aAccelOnly, aForceUpdate,
                           aStrokeOptions, aVertexRange, aRectXform, stage)) {
          return false;
        }
      }
      return true;
    }
  }

  const Matrix& currentTransform = mCurrentTarget->GetTransform();
  // rectXform only applies to the rect, but should not apply to the pattern,
  // as it might inadvertently alter the pattern.
  Matrix rectXform = currentTransform;
  if (aRectXform) {
    rectXform.PreMultiply(*aRectXform);
  }

  if (aOptions.mCompositionOp == CompositionOp::OP_SOURCE && aClipped &&
      (aVertexRange ||
       !(aTransformed
             ? rectXform.PreservesAxisAlignedRectangles() &&
                   rectXform.TransformBounds(aRect).Contains(mClipAARect)
             : aRect.IsEqualEdges(Rect(mClipRect)) ||
                   aRect.Contains(mClipAARect)) ||
       (aPattern.GetType() == PatternType::SURFACE &&
        (HasClipMask() || !IsAlignedRect(false, Matrix(), mClipAARect))))) {
    // Clear outside the mask region for masks that are not bounded by clip.
    return DrawRectAccel(Rect(mClipRect), ColorPattern(DeviceColor(0, 0, 0, 0)),
                         DrawOptions(1.0f, CompositionOp::OP_SOURCE,
                                     aOptions.mAntialiasMode),
                         Nothing(), nullptr, false, aClipped, aAccelOnly) &&
           DrawRectAccel(aRect, aPattern,
                         DrawOptions(aOptions.mAlpha, CompositionOp::OP_ADD,
                                     aOptions.mAntialiasMode),
                         aMaskColor, aHandle, aTransformed, aClipped,
                         aAccelOnly, aForceUpdate, aStrokeOptions, aVertexRange,
                         aRectXform);
  }
  if (aOptions.mCompositionOp == CompositionOp::OP_CLEAR &&
      aPattern.GetType() == PatternType::SURFACE && !aMaskColor) {
    // If the surface being drawn with clear is not a mask, then its contents
    // needs to be ignored. Just use a color pattern instead.
    return DrawRectAccel(aRect, ColorPattern(DeviceColor(1, 1, 1, 1)), aOptions,
                         Nothing(), aHandle, aTransformed, aClipped, aAccelOnly,
                         aForceUpdate, aStrokeOptions, aVertexRange,
                         aRectXform);
  }

  // Set up the scissor test to reflect the clipping rectangle, if supplied.
  if (!mClipRect.Contains(IntRect(IntPoint(), mViewportSize))) {
    EnableScissor(mClipRect);
  } else {
    DisableScissor();
  }

  bool success = false;

  // Now try to actually draw the pattern...
  switch (aPattern.GetType()) {
    case PatternType::COLOR: {
      if (!aVertexRange) {
        // Only an uncached draw if not using the vertex cache.
        mCurrentTarget->mProfile.OnUncachedDraw();
      }
      DeviceColor color = PremultiplyColor(
          static_cast<const ColorPattern&>(aPattern).mColor, aOptions.mAlpha);
      if (((color.a == 1.0f &&
            aOptions.mCompositionOp == CompositionOp::OP_OVER) ||
           aOptions.mCompositionOp == CompositionOp::OP_SOURCE ||
           aOptions.mCompositionOp == CompositionOp::OP_CLEAR) &&
          !aStrokeOptions && !aVertexRange && !HasClipMask() &&
          mClipAARect.IsEqualEdges(Rect(mClipRect))) {
        // Certain color patterns can be mapped to scissored clears. The
        // composition op must effectively overwrite the destination, and the
        // transform must map to an axis-aligned integer rectangle.
        if (Maybe<IntRect> intRect =
                IsAlignedRect(aTransformed, rectXform, aRect)) {
          // Only use a clear if the area is larger than a quarter or the
          // viewport.
          if (intRect->Area() >=
              (mViewportSize.width / 2) * (mViewportSize.height / 2)) {
            if (!intRect->Contains(mClipRect)) {
              EnableScissor(intRect->Intersect(mClipRect));
            }
            if (aOptions.mCompositionOp == CompositionOp::OP_CLEAR) {
              color =
                  PremultiplyColor(mCurrentTarget->GetClearPattern().mColor);
            }
            mWebgl->ClearColor(color.b, color.g, color.r, color.a);
            mWebgl->Clear(LOCAL_GL_COLOR_BUFFER_BIT);
            success = true;
            break;
          }
        }
      }
      // Map the composition op to a WebGL blend mode, if possible.
      Maybe<DeviceColor> blendColor;
      if (aOptions.mCompositionOp == CompositionOp::OP_SOURCE ||
          aOptions.mCompositionOp == CompositionOp::OP_CLEAR) {
        // The source operator can support clipping and AA by emulating it with
        // the over op. Supply the color with blend state, and set the shader
        // color to white, to avoid needing dual-source blending.
        blendColor = Some(color);
        // Both source and clear operators should output a mask from the shader.
        color = DeviceColor(1, 1, 1, 1);
      }
      SetBlendState(aOptions.mCompositionOp, blendColor, aBlendStage);
      // Since it couldn't be mapped to a scissored clear, we need to use the
      // solid color shader with supplied transform.
      if (mLastProgram != mSolidProgram) {
        mWebgl->UseProgram(mSolidProgram);
        mLastProgram = mSolidProgram;
      }
      Array<float, 2> viewportData = {float(mViewportSize.width),
                                      float(mViewportSize.height)};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mSolidProgramViewport, viewportData,
                       mSolidProgramUniformState.mViewport);

      // Generated paths provide their own AA as vertex alpha.
      Array<float, 1> aaData = {aVertexRange ? 0.0f : 1.0f};
      MaybeUniformData(LOCAL_GL_FLOAT, mSolidProgramAA, aaData,
                       mSolidProgramUniformState.mAA);

      // Offset the clip AA bounds by 0.5 to ensure AA falls to 0 at pixel
      // boundary.
      Array<float, 4> clipData = {mClipAARect.x - 0.5f, mClipAARect.y - 0.5f,
                                  mClipAARect.XMost() + 0.5f,
                                  mClipAARect.YMost() + 0.5f};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mSolidProgramClipBounds, clipData,
                       mSolidProgramUniformState.mClipBounds);

      Array<float, 4> colorData = {color.b, color.g, color.r, color.a};
      Matrix xform(aRect.width, 0.0f, 0.0f, aRect.height, aRect.x, aRect.y);
      if (aTransformed) {
        xform *= rectXform;
      }
      Array<float, 6> xformData = {xform._11, xform._12, xform._21,
                                   xform._22, xform._31, xform._32};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mSolidProgramTransform, xformData,
                       mSolidProgramUniformState.mTransform);

      MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mSolidProgramColor, colorData,
                       mSolidProgramUniformState.mColor);

      // Finally draw the colored rectangle.
      if (aVertexRange) {
        // If there's a vertex range, then we need to draw triangles within from
        // generated from a path stored in the path vertex buffer.
        DrawTriangles(*aVertexRange);
      } else {
        // Otherwise we're drawing a simple filled rectangle.
        DrawQuad();
      }
      success = true;
      break;
    }
    case PatternType::SURFACE: {
      auto surfacePattern = static_cast<const SurfacePattern&>(aPattern);
      // If a texture handle was supplied, or if the surface already has an
      // assigned texture handle stashed in its used data, try to use it.
      RefPtr<SourceSurface> underlyingSurface =
          surfacePattern.mSurface
              ? surfacePattern.mSurface->GetUnderlyingSurface()
              : nullptr;
      RefPtr<TextureHandle> handle =
          aHandle
              ? aHandle->get()
              : (underlyingSurface
                     ? static_cast<TextureHandle*>(
                           underlyingSurface->GetUserData(&mTextureHandleKey))
                     : nullptr);
      IntSize texSize;
      IntPoint offset;
      SurfaceFormat format;
      // Check if the found handle is still valid and if its sampling rect
      // matches the requested sampling rect.
      if (handle && handle->IsValid() &&
          (surfacePattern.mSamplingRect.IsEmpty() ||
           handle->GetSamplingRect().IsEqualEdges(
               surfacePattern.mSamplingRect)) &&
          (surfacePattern.mExtendMode == ExtendMode::CLAMP ||
           handle->GetType() == TextureHandle::STANDALONE)) {
        texSize = handle->GetSize();
        format = handle->GetFormat();
        offset = handle->GetSamplingOffset();
      } else {
        // Otherwise, there is no handle that can be used yet, so extract
        // information from the surface pattern.
        handle = nullptr;
        if (!underlyingSurface) {
          // If there was no actual surface supplied, then we tried to draw
          // using a texture handle, but the texture handle wasn't valid.
          break;
        }
        texSize = underlyingSurface->GetSize();
        format = underlyingSurface->GetFormat();
        if (!surfacePattern.mSamplingRect.IsEmpty()) {
          texSize = surfacePattern.mSamplingRect.Size();
          offset = surfacePattern.mSamplingRect.TopLeft();
        }
      }

      // We need to be able to transform from local space into texture space.
      Matrix invMatrix = surfacePattern.mMatrix;
      // Determine if the requested surface itself is offset from the underlying
      // surface.
      if (surfacePattern.mSurface) {
        invMatrix.PreTranslate(surfacePattern.mSurface->GetRect().TopLeft());
      }
      // If drawing a pre-transformed vertex range, then we need to ensure the
      // user-space pattern is still transformed to screen-space.
      if (aVertexRange && !aTransformed) {
        invMatrix *= currentTransform;
      }
      if (!invMatrix.Invert()) {
        break;
      }
      if (aRectXform) {
        // If there is aRectXform, it must be applied to the source rectangle to
        // generate the proper input coordinates for the inverse pattern matrix.
        invMatrix.PreMultiply(*aRectXform);
      }

      RefPtr<WebGLTexture> tex;
      IntRect bounds;
      IntSize backingSize;
      if (handle) {
        if (aForceUpdate) {
          RefPtr<DataSourceSurface> data = underlyingSurface->GetDataSurface();
          if (!data) {
            break;
          }
          UploadSurfaceToHandle(data, offset, handle);
          // The size of the texture may change if we update contents.
          mUsedTextureMemory -= handle->UsedBytes();
          handle->UpdateSize(texSize);
          mUsedTextureMemory += handle->UsedBytes();
          handle->SetSamplingOffset(surfacePattern.mSamplingRect.TopLeft());
        } else {
          // Count reusing a snapshot texture (no readback) as a cache hit.
          mCurrentTarget->mProfile.OnCacheHit();
        }
        // If using an existing handle, move it to the front of the MRU list.
        handle->remove();
        mTextureHandles.insertFront(handle);
      } else if ((tex = GetCompatibleSnapshot(underlyingSurface, &handle))) {
        backingSize = underlyingSurface->GetSize();
        bounds = IntRect(offset, texSize);
        // Count reusing a snapshot texture (no readback) as a cache hit.
        mCurrentTarget->mProfile.OnCacheHit();
        if (aHandle) {
          *aHandle = handle;
        }
      } else {
        // If we get here, we need a data surface for a texture upload.
        RefPtr<DataSourceSurface> data = underlyingSurface->GetDataSurface();
        if (!data) {
          break;
        }
        // There is no existing handle. Try to allocate a new one. If the
        // surface size may change via a forced update, then don't allocate
        // from a shared texture page.
        handle = AllocateTextureHandle(
            format, texSize,
            !aForceUpdate && surfacePattern.mExtendMode == ExtendMode::CLAMP);
        if (!handle) {
          MOZ_ASSERT(false);
          break;
        }
        UploadSurfaceToHandle(data, offset, handle);
        // Link the handle to the surface's user data.
        handle->SetSamplingOffset(surfacePattern.mSamplingRect.TopLeft());
        if (aHandle) {
          *aHandle = handle;
        } else {
          handle->SetSurface(underlyingSurface);
          underlyingSurface->AddUserData(&mTextureHandleKey, handle.get(),
                                         nullptr);
        }
      }
      if (handle) {
        BackingTexture* backing = handle->GetBackingTexture();
        if (!tex) {
          tex = backing->GetWebGLTexture();
        }
        bounds = bounds.IsEmpty() ? handle->GetBounds()
                                  : handle->GetBounds().SafeIntersect(
                                        bounds + handle->GetBounds().TopLeft());
        backingSize = backing->GetSize();
      }

      // Map the composition op to a WebGL blend mode, if possible. If there is
      // a mask color and a texture with multiple channels, assume subpixel
      // blending. If we encounter the source op here, then assume the surface
      // is opaque (non-opaque is handled above) and emulate it with over.
      SetBlendState(aOptions.mCompositionOp,
                    format != SurfaceFormat::A8 ? aMaskColor : Nothing(),
                    aBlendStage);
      // Switch to the image shader and set up relevant transforms.
      if (mLastProgram != mImageProgram) {
        mWebgl->UseProgram(mImageProgram);
        mLastProgram = mImageProgram;
      }

      Array<float, 2> viewportData = {float(mViewportSize.width),
                                      float(mViewportSize.height)};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mImageProgramViewport, viewportData,
                       mImageProgramUniformState.mViewport);

      // AA is not supported for OP_SOURCE. Generated paths provide their own
      // AA as vertex alpha.
      Array<float, 1> aaData = {
          mLastCompositionOp == CompositionOp::OP_SOURCE || aVertexRange
              ? 0.0f
              : 1.0f};
      MaybeUniformData(LOCAL_GL_FLOAT, mImageProgramAA, aaData,
                       mImageProgramUniformState.mAA);

      // Offset the clip AA bounds by 0.5 to ensure AA falls to 0 at pixel
      // boundary.
      Array<float, 4> clipData = {mClipAARect.x - 0.5f, mClipAARect.y - 0.5f,
                                  mClipAARect.XMost() + 0.5f,
                                  mClipAARect.YMost() + 0.5f};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mImageProgramClipBounds, clipData,
                       mImageProgramUniformState.mClipBounds);

      DeviceColor color =
          mLastCompositionOp == CompositionOp::OP_CLEAR
              ? DeviceColor(1, 1, 1, 1)
              : PremultiplyColor(
                    aMaskColor && format != SurfaceFormat::A8
                        ? DeviceColor::Mask(1.0f, aMaskColor->a)
                        : aMaskColor.valueOr(DeviceColor(1, 1, 1, 1)),
                    aOptions.mAlpha);
      Array<float, 4> colorData = {color.b, color.g, color.r, color.a};
      Array<float, 1> swizzleData = {format == SurfaceFormat::A8 ? 1.0f : 0.0f};
      Matrix xform(aRect.width, 0.0f, 0.0f, aRect.height, aRect.x, aRect.y);
      if (aTransformed) {
        xform *= rectXform;
      }
      Array<float, 6> xformData = {xform._11, xform._12, xform._21,
                                   xform._22, xform._31, xform._32};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mImageProgramTransform, xformData,
                       mImageProgramUniformState.mTransform);

      MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mImageProgramColor, colorData,
                       mImageProgramUniformState.mColor);

      MaybeUniformData(LOCAL_GL_FLOAT, mImageProgramSwizzle, swizzleData,
                       mImageProgramUniformState.mSwizzle);

      // Start binding the WebGL state for the texture.
      if (mLastTexture != tex) {
        mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, tex);
        mLastTexture = tex;
      }

      // Set up the texture coordinate matrix to map from the input rectangle to
      // the backing texture subrect.
      Size backingSizeF(backingSize);
      Matrix uvMatrix(aRect.width, 0.0f, 0.0f, aRect.height, aRect.x, aRect.y);
      uvMatrix *= invMatrix;
      uvMatrix *= Matrix(1.0f / backingSizeF.width, 0.0f, 0.0f,
                         1.0f / backingSizeF.height,
                         float(bounds.x - offset.x) / backingSizeF.width,
                         float(bounds.y - offset.y) / backingSizeF.height);
      Array<float, 6> uvData = {uvMatrix._11, uvMatrix._12, uvMatrix._21,
                                uvMatrix._22, uvMatrix._31, uvMatrix._32};
      MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mImageProgramTexMatrix, uvData,
                       mImageProgramUniformState.mTexMatrix);

      // Clamp sampling to within the bounds of the backing texture subrect.
      Array<float, 4> texBounds = {
          (bounds.x + 0.5f) / backingSizeF.width,
          (bounds.y + 0.5f) / backingSizeF.height,
          (bounds.XMost() - 0.5f) / backingSizeF.width,
          (bounds.YMost() - 0.5f) / backingSizeF.height,
      };
      switch (surfacePattern.mExtendMode) {
        case ExtendMode::REPEAT:
          texBounds[0] = -1e16f;
          texBounds[1] = -1e16f;
          texBounds[2] = 1e16f;
          texBounds[3] = 1e16f;
          break;
        case ExtendMode::REPEAT_X:
          texBounds[0] = -1e16f;
          texBounds[2] = 1e16f;
          break;
        case ExtendMode::REPEAT_Y:
          texBounds[1] = -1e16f;
          texBounds[3] = 1e16f;
          break;
        default:
          break;
      }
      MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mImageProgramTexBounds, texBounds,
                       mImageProgramUniformState.mTexBounds);

      // Ensure we use nearest filtering when no antialiasing is requested.
      if (UseNearestFilter(surfacePattern)) {
        SetTexFilter(tex, false);
      }

      // Finally draw the image rectangle.
      if (aVertexRange) {
        // If there's a vertex range, then we need to draw triangles within from
        // generated from a path stored in the path vertex buffer.
        DrawTriangles(*aVertexRange);
      } else {
        // Otherwise we're drawing a simple filled rectangle.
        DrawQuad();
      }

      // Restore the default linear filter if overridden.
      if (UseNearestFilter(surfacePattern)) {
        SetTexFilter(tex, true);
      }

      success = true;
      break;
    }
    default:
      gfxWarning() << "Unknown DrawTargetWebgl::DrawRect pattern type: "
                   << (int)aPattern.GetType();
      break;
  }

  return success;
}

// Get an appropriate input texture for a given surface within the source rect.
already_AddRefed<WebGLTexture> SharedContextWebgl::GetFilterInputTexture(
    const RefPtr<SourceSurface>& aSurface, const IntRect& aSourceRect,
    RefPtr<TextureHandle>* aHandle, IntPoint& aOffset, SurfaceFormat& aFormat,
    IntRect& aBounds, IntSize& aBackingSize) {
  // If a texture handle was supplied, or if the surface already has an
  // assigned texture handle stashed in its used data, try to use it.
  RefPtr<SourceSurface> underlyingSurface =
      aSurface ? aSurface->GetUnderlyingSurface() : nullptr;
  RefPtr<TextureHandle> handle =
      aHandle ? aHandle->get()
              : (underlyingSurface
                     ? static_cast<TextureHandle*>(
                           underlyingSurface->GetUserData(&mTextureHandleKey))
                     : nullptr);
  IntSize texSize;
  IntPoint offset;
  SurfaceFormat format;
  // Check if the found handle is still valid.
  if (handle && handle->IsValid() &&
      (aSourceRect.IsEmpty() ||
       handle->GetSamplingRect().IsEqualEdges(aSourceRect))) {
    texSize = handle->GetSize();
    format = handle->GetFormat();
    offset = handle->GetSamplingOffset();
  } else {
    // Otherwise, there is no handle that can be used yet, so extract
    // information from the surface pattern.
    handle = nullptr;
    if (!underlyingSurface) {
      // If there was no actual surface supplied, then we tried to draw
      // using a texture handle, but the texture handle wasn't valid.
      return nullptr;
    }
    texSize = underlyingSurface->GetSize();
    format = underlyingSurface->GetFormat();
    if (!aSourceRect.IsEmpty()) {
      texSize = aSourceRect.Size();
      offset = aSourceRect.TopLeft();
    }
  }

  RefPtr<WebGLTexture> tex;
  IntRect bounds;
  IntSize backingSize;
  if (handle) {
    // If using an existing handle, move it to the front of the MRU list.
    handle->remove();
    mTextureHandles.insertFront(handle);
    // Count reusing a snapshot texture (no readback) as a cache hit.
    mCurrentTarget->mProfile.OnCacheHit();
  } else if ((tex = GetCompatibleSnapshot(underlyingSurface, &handle))) {
    backingSize = underlyingSurface->GetSize();
    bounds = IntRect(offset, texSize);
    // Count reusing a snapshot texture (no readback) as a cache hit.
    mCurrentTarget->mProfile.OnCacheHit();
  } else {
    // If we get here, we need a data surface for a texture upload.
    RefPtr<DataSourceSurface> data = underlyingSurface->GetDataSurface();
    if (!data) {
      return nullptr;
    }
    // There is no existing handle. Try to allocate a new one. If the
    // surface size may change via a forced update, then don't allocate
    // from a shared texture page.
    handle = AllocateTextureHandle(format, texSize);
    if (!handle) {
      MOZ_ASSERT(false);
      return nullptr;
    }
    UploadSurfaceToHandle(data, offset, handle);
    // Link the handle to the surface's user data.
    handle->SetSamplingOffset(aSourceRect.TopLeft());
    if (aHandle) {
      *aHandle = handle;
    } else {
      handle->SetSurface(underlyingSurface);
      underlyingSurface->AddUserData(&mTextureHandleKey, handle.get(), nullptr);
    }
  }

  if (handle) {
    BackingTexture* backing = handle->GetBackingTexture();
    if (!tex) {
      tex = backing->GetWebGLTexture();
    }
    bounds = bounds.IsEmpty() ? handle->GetBounds()
                              : handle->GetBounds().SafeIntersect(
                                    bounds + handle->GetBounds().TopLeft());
    backingSize = backing->GetSize();
  }

  aOffset = offset;
  aFormat = format;
  aBounds = bounds;
  aBackingSize = backingSize;
  return tex.forget();
}

// Implements any filter that can be computed with a 5x4 color matrix.
bool SharedContextWebgl::FilterRect(const Rect& aDestRect,
                                    const Matrix5x4& aColorMatrix,
                                    const RefPtr<SourceSurface>& aSurface,
                                    const IntRect& aSourceRect,
                                    const DrawOptions& aOptions,
                                    RefPtr<TextureHandle>* aHandle,
                                    RefPtr<TextureHandle>* aTargetHandle) {
  if (!aTargetHandle && !mCurrentTarget->MarkChanged()) {
    return false;
  }

  IntPoint offset;
  SurfaceFormat format;
  IntRect bounds;
  IntSize backingSize;
  RefPtr<WebGLTexture> tex = GetFilterInputTexture(
      aSurface, aSourceRect, aHandle, offset, format, bounds, backingSize);
  if (!tex) {
    return false;
  }

  IntSize viewportSize = mViewportSize;
  bool needTarget = !!aTargetHandle;
  if (needTarget) {
    IntSize targetSize = IntSize::Ceil(aDestRect.Size());
    viewportSize = targetSize;
    // Blur filters need to render to a color target, whereas shadows will only
    // sample alpha.
    // If sourcing from a texture handle as input, be careful not to render to
    // a handle with the same exact backing texture, which is not allowed in
    // WebGL.
    RefPtr<TextureHandle> targetHandle =
        AllocateTextureHandle(format, targetSize, true, true, tex);
    if (!targetHandle) {
      MOZ_ASSERT(false);
      return false;
    }

    *aTargetHandle = targetHandle;

    BindScratchFramebuffer(targetHandle, true, targetSize);

    SetBlendState(CompositionOp::OP_OVER);
  } else {
    // Set up the scissor test to reflect the clipping rectangle, if supplied.
    if (!mClipRect.Contains(IntRect(IntPoint(), mViewportSize))) {
      EnableScissor(mClipRect);
    } else {
      DisableScissor();
    }

    // Map the composition op to a WebGL blend mode, if possible.
    SetBlendState(aOptions.mCompositionOp);
  }

  // Switch to the filter shader and set up relevant transforms.
  if (mLastProgram != mFilterProgram) {
    mWebgl->UseProgram(mFilterProgram);
    mLastProgram = mFilterProgram;
  }

  Array<float, 2> viewportData = {float(viewportSize.width),
                                  float(viewportSize.height)};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mFilterProgramViewport, viewportData,
                   mFilterProgramUniformState.mViewport);

  Rect xformRect;
  if (needTarget) {
    // If rendering to a temporary target for an intermediate pass, then fill
    // the entire framebuffer.
    xformRect = Rect(IntRect(IntPoint(), viewportSize));
  } else {
    // If doing a final composite, then render to the requested rectangle.
    xformRect = aDestRect;
  }
  Array<float, 4> xformData = {xformRect.width, xformRect.height, xformRect.x,
                               xformRect.y};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mFilterProgramTransform, xformData,
                   mFilterProgramUniformState.mTransform);

  Rect clipRect;
  if (needTarget) {
    // Disable any AA clipping.
    clipRect = xformRect;
  } else {
    clipRect = mClipAARect;
  }
  // Offset the clip AA bounds by 0.5 to ensure AA falls to 0 at pixel
  // boundary.
  clipRect.Inflate(0.5f);
  Array<float, 4> clipData = {clipRect.x, clipRect.y, clipRect.XMost(),
                              clipRect.YMost()};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mFilterProgramClipBounds, clipData,
                   mFilterProgramUniformState.mClipBounds);

  Array<float, 16> colorMatData = {
      aColorMatrix._11, aColorMatrix._12, aColorMatrix._13, aColorMatrix._14,
      aColorMatrix._21, aColorMatrix._22, aColorMatrix._23, aColorMatrix._24,
      aColorMatrix._31, aColorMatrix._32, aColorMatrix._33, aColorMatrix._34,
      aColorMatrix._41, aColorMatrix._42, aColorMatrix._43, aColorMatrix._44};
  MaybeUniformData(LOCAL_GL_FLOAT_MAT4, mFilterProgramColorMatrix, colorMatData,
                   mFilterProgramUniformState.mColorMatrix);
  Array<float, 4> colorOffData = {aColorMatrix._51, aColorMatrix._52,
                                  aColorMatrix._53, aColorMatrix._54};
  MaybeUniformData(LOCAL_GL_FLOAT_MAT4, mFilterProgramColorOffset, colorOffData,
                   mFilterProgramUniformState.mColorOffset);

  // Start binding the WebGL state for the texture.
  if (mLastTexture != tex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, tex);
    mLastTexture = tex;
  }

  // Set up the texture coordinate matrix to map from the input rectangle to
  // the backing texture subrect.
  Size backingSizeF(backingSize);
  Rect uvXform((bounds.x - offset.x) / backingSizeF.width,
               (bounds.y - offset.y) / backingSizeF.height,
               xformRect.width / backingSizeF.width,
               xformRect.height / backingSizeF.height);
  Array<float, 4> uvData = {uvXform.width, uvXform.height, uvXform.x,
                            uvXform.y};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mFilterProgramTexMatrix, uvData,
                   mFilterProgramUniformState.mTexMatrix);

  // Bounds for inclusion testing. These are not offset by half a pixel because
  // they are not used for clamping, but rather denote pixel thresholds.
  Array<float, 4> texBounds = {
      bounds.x / backingSizeF.width,
      bounds.y / backingSizeF.height,
      bounds.XMost() / backingSizeF.width,
      bounds.YMost() / backingSizeF.height,
  };
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mFilterProgramTexBounds, texBounds,
                   mFilterProgramUniformState.mTexBounds);

  RefPtr<WebGLTexture> prevClipMask;
  if (needTarget) {
    // Ensure the current clip mask is ignored.
    prevClipMask = mLastClipMask;
    SetNoClipMask();
  }

  DrawQuad();

  if (needTarget) {
    // Restore the previous framebuffer state.
    RestoreCurrentTarget(prevClipMask);
  }

  return true;
}

// Filters a surface and draws the result at the specified offset.
bool DrawTargetWebgl::FilterSurface(const Matrix5x4& aColorMatrix,
                                    SourceSurface* aSurface,
                                    const IntRect& aSourceRect,
                                    const Point& aDest,
                                    const DrawOptions& aOptions) {
  IntRect sourceRect =
      aSourceRect.IsEmpty() ? aSurface->GetRect() : aSourceRect;
  if (ShouldAccelPath(aOptions, nullptr,
                      Rect(aDest, Size(sourceRect.Size())))) {
    if (mTransform.IsTranslation() &&
        !mSharedContext->RequiresMultiStageBlend(aOptions, this)) {
      // If the transform doesn't require resampling and the blend op is simple,
      // then draw the filter directly to the canvas.
      return mSharedContext->FilterRect(
          Rect(aDest + mTransform.GetTranslation(), Size(sourceRect.Size())),
          aColorMatrix, aSurface, sourceRect, aOptions, nullptr, nullptr);
    }
    // There is a complex transform or blend op, so the filter must be drawn to
    // an intermediate texture first before resampling.
    RefPtr<TextureHandle> resultHandle;
    if (mSharedContext->FilterRect(Rect(Point(0, 0), Size(sourceRect.Size())),
                                   aColorMatrix, aSurface, sourceRect,
                                   DrawOptions(), nullptr, &resultHandle) &&
        resultHandle) {
      SurfacePattern filterPattern(nullptr, ExtendMode::CLAMP,
                                   Matrix::Translation(aDest));
      return mSharedContext->DrawRectAccel(
          Rect(aDest, Size(resultHandle->GetSize())), filterPattern, aOptions,
          Nothing(), &resultHandle, true, true, true);
    }
  }
  return false;
}

// Provides a single pass of a separable blur.
bool SharedContextWebgl::BlurRectPass(
    const Rect& aDestRect, const Point& aSigma, bool aHorizontal,
    const RefPtr<SourceSurface>& aSurface, const IntRect& aSourceRect,
    const DrawOptions& aOptions, Maybe<DeviceColor> aMaskColor,
    RefPtr<TextureHandle>* aHandle, RefPtr<TextureHandle>* aTargetHandle,
    bool aFilter) {
  IntPoint offset;
  SurfaceFormat format;
  IntRect bounds;
  IntSize backingSize;
  RefPtr<WebGLTexture> tex = GetFilterInputTexture(
      aSurface, aSourceRect, aHandle, offset, format, bounds, backingSize);
  if (!tex) {
    return false;
  }

  IntSize viewportSize = mViewportSize;
  IntSize blurRadius(BLUR_ACCEL_RADIUS(aSigma.x), BLUR_ACCEL_RADIUS(aSigma.y));
  bool needTarget = !!aTargetHandle;
  if (needTarget) {
    // For the initial horizontal pass, and also for the second pass of filters,
    // we need to render to a temporary framebuffer that has been inflated to
    // accommodate blurred pixels in the margins.
    IntSize targetSize(
        int(ceil(aDestRect.width)) + blurRadius.width * 2,
        aHorizontal ? bounds.height
                    : int(ceil(aDestRect.height)) + blurRadius.height * 2);
    viewportSize = targetSize;
    // Blur filters need to render to a color target, whereas shadows will only
    // sample alpha.
    // If sourcing from a texture handle as input, be careful not to render to
    // a handle with the same exact backing texture, which is not allowed in
    // WebGL.
    RefPtr<TextureHandle> targetHandle = AllocateTextureHandle(
        aFilter ? format : SurfaceFormat::A8, targetSize, true, true, tex);
    if (!targetHandle) {
      MOZ_ASSERT(false);
      return false;
    }

    *aTargetHandle = targetHandle;

    BindScratchFramebuffer(targetHandle, true, targetSize);

    SetBlendState(CompositionOp::OP_OVER);
  } else {
    // Set up the scissor test to reflect the clipping rectangle, if supplied.
    if (!mClipRect.Contains(IntRect(IntPoint(), mViewportSize))) {
      EnableScissor(mClipRect);
    } else {
      DisableScissor();
    }

    // Map the composition op to a WebGL blend mode, if possible.
    SetBlendState(aOptions.mCompositionOp);
  }

  // Switch to the blur shader and set up relevant transforms.
  if (mLastProgram != mBlurProgram) {
    mWebgl->UseProgram(mBlurProgram);
    mLastProgram = mBlurProgram;
  }

  Array<float, 2> viewportData = {float(viewportSize.width),
                                  float(viewportSize.height)};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mBlurProgramViewport, viewportData,
                   mBlurProgramUniformState.mViewport);

  Rect xformRect;
  if (needTarget) {
    // If rendering to a temporary target for an intermediate pass, then fill
    // the entire framebuffer.
    xformRect = Rect(IntRect(IntPoint(), viewportSize));
  } else {
    // If doing a final composite, then render to the requested rectangle,
    // inflated for the blurred margin pixels.
    xformRect = aDestRect;
    xformRect.Inflate(Size(blurRadius));
  }
  Array<float, 4> xformData = {xformRect.width, xformRect.height, xformRect.x,
                               xformRect.y};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mBlurProgramTransform, xformData,
                   mBlurProgramUniformState.mTransform);

  Rect clipRect;
  if (needTarget) {
    // Disable any AA clipping.
    clipRect = xformRect;
  } else {
    clipRect = mClipAARect;
  }
  // Offset the clip AA bounds by 0.5 to ensure AA falls to 0 at pixel
  // boundary.
  clipRect.Inflate(0.5f);
  Array<float, 4> clipData = {clipRect.x, clipRect.y, clipRect.XMost(),
                              clipRect.YMost()};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mBlurProgramClipBounds, clipData,
                   mBlurProgramUniformState.mClipBounds);

  DeviceColor color =
      needTarget ? DeviceColor(1, 1, 1, 1)
                 : PremultiplyColor(aMaskColor.valueOr(DeviceColor(1, 1, 1, 1)),
                                    aOptions.mAlpha);
  Array<float, 4> colorData = {color.b, color.g, color.r, color.a};
  Array<float, 1> swizzleData = {format == SurfaceFormat::A8 ? 1.0f : 0.0f};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mBlurProgramColor, colorData,
                   mBlurProgramUniformState.mColor);
  MaybeUniformData(LOCAL_GL_FLOAT, mBlurProgramSwizzle, swizzleData,
                   mBlurProgramUniformState.mSwizzle);

  // Start binding the WebGL state for the texture.
  if (mLastTexture != tex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, tex);
    mLastTexture = tex;
  }

  // Set up the texture coordinate matrix to map from the input rectangle to
  // the backing texture subrect.
  Size backingSizeF(backingSize);
  Rect uvXform((bounds.x - offset.x - (xformRect.width - bounds.width) / 2) /
                   backingSizeF.width,
               (bounds.y - offset.y - (xformRect.height - bounds.height) / 2) /
                   backingSizeF.height,
               xformRect.width / backingSizeF.width,
               xformRect.height / backingSizeF.height);
  Array<float, 4> uvData = {uvXform.width, uvXform.height, uvXform.x,
                            uvXform.y};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mBlurProgramTexMatrix, uvData,
                   mBlurProgramUniformState.mTexMatrix);

  // Bounds for inclusion testing. These are not offset by half a pixel because
  // they are not used for clamping, but rather denote pixel thresholds.
  Array<float, 4> texBounds = {
      bounds.x / backingSizeF.width,
      bounds.y / backingSizeF.height,
      bounds.XMost() / backingSizeF.width,
      bounds.YMost() / backingSizeF.height,
  };
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mBlurProgramTexBounds, texBounds,
                   mBlurProgramUniformState.mTexBounds);

  Array<float, 1> sigmaData = {aHorizontal ? aSigma.x : aSigma.y};
  MaybeUniformData(LOCAL_GL_FLOAT, mBlurProgramSigma, sigmaData,
                   mBlurProgramUniformState.mSigma);

  Array<float, 2> offsetScale =
      aHorizontal ? Array<float, 2>{1.0f / backingSizeF.width, 0.0f}
                  : Array<float, 2>{0.0f, 1.0f / backingSizeF.height};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mBlurProgramOffsetScale, offsetScale,
                   mBlurProgramUniformState.mOffsetScale);

  RefPtr<WebGLTexture> prevClipMask;
  if (needTarget) {
    // Ensure the current clip mask is ignored.
    prevClipMask = mLastClipMask;
    SetNoClipMask();
  }

  DrawQuad();

  if (needTarget) {
    // Restore the previous framebuffer state.
    RestoreCurrentTarget(prevClipMask);
  }

  return true;
}

// Utility function to schedule multiple blur passes of a separable blur.
bool SharedContextWebgl::BlurRectAccel(
    const Rect& aDestRect, const Point& aSigma,
    const RefPtr<SourceSurface>& aSurface, const IntRect& aSourceRect,
    const DrawOptions& aOptions, Maybe<DeviceColor> aMaskColor,
    RefPtr<TextureHandle>* aHandle, RefPtr<TextureHandle>* aTargetHandle,
    RefPtr<TextureHandle>* aResultHandle, bool aFilter) {
  if (!aResultHandle && !mCurrentTarget->MarkChanged()) {
    return false;
  }
  RefPtr<TextureHandle> targetHandle =
      aTargetHandle ? aTargetHandle->get() : nullptr;
  if (targetHandle && targetHandle->IsValid() &&
      BlurRectPass(aDestRect, aSigma, false, nullptr, IntRect(), aOptions,
                   aMaskColor, &targetHandle, aResultHandle, aFilter)) {
    return true;
  }

  RefPtr<TextureHandle> handle = aHandle ? aHandle->get() : nullptr;
  if (BlurRectPass(aDestRect, aSigma, true, aSurface, aSourceRect,
                   DrawOptions(), Nothing(), &handle, &targetHandle, aFilter) &&
      targetHandle &&
      BlurRectPass(aDestRect, aSigma, false, nullptr, IntRect(), aOptions,
                   aMaskColor, &targetHandle, aResultHandle, aFilter)) {
    if (aHandle) {
      *aHandle = handle.forget();
    }
    if (aTargetHandle) {
      *aTargetHandle = targetHandle.forget();
    }
    return true;
  }
  return false;
}

// Halves the dimensions of a blur input texture for sufficiently large blurs
// where scaling won't significantly impact resultant blur quality.
already_AddRefed<SourceSurface> SharedContextWebgl::DownscaleBlurInput(
    SourceSurface* aSurface, const IntRect& aSourceRect, int aIters) {
  if (std::max(aSourceRect.width, aSourceRect.height) <= 1) {
    return nullptr;
  }
  RefPtr<TextureHandle> fullHandle;
  // First check if the source surface is actually a texture.
  if (RefPtr<WebGLTexture> fullTex =
          GetCompatibleSnapshot(aSurface, &fullHandle)) {
    IntRect sourceRect = aSourceRect;
    for (int i = 0; i < aIters; ++i) {
      IntSize halfSize = (sourceRect.Size() + IntSize(1, 1)) / 2;
      // Allocate a half-size texture for the downscale target.
      RefPtr<TextureHandle> halfHandle = AllocateTextureHandle(
          aSurface->GetFormat(), halfSize, true, true, fullTex);
      if (!halfHandle) {
        break;
      }

      // Set up the read framebuffer for the full-size texture.
      if (!mScratchFramebuffer) {
        mScratchFramebuffer = mWebgl->CreateFramebuffer();
      }
      mWebgl->BindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, mScratchFramebuffer);
      webgl::FbAttachInfo readInfo;
      readInfo.tex = fullTex;
      mWebgl->FramebufferAttach(LOCAL_GL_READ_FRAMEBUFFER,
                                LOCAL_GL_COLOR_ATTACHMENT0, LOCAL_GL_TEXTURE_2D,
                                readInfo);

      // Set up the draw framebuffer for the half-size texture.
      if (!mTargetFramebuffer) {
        mTargetFramebuffer = mWebgl->CreateFramebuffer();
      }
      BackingTexture* halfBacking = halfHandle->GetBackingTexture();
      InitRenderTex(halfBacking);

      mWebgl->BindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, mTargetFramebuffer);
      webgl::FbAttachInfo drawInfo;
      drawInfo.tex = halfBacking->GetWebGLTexture();
      mWebgl->FramebufferAttach(LOCAL_GL_DRAW_FRAMEBUFFER,
                                LOCAL_GL_COLOR_ATTACHMENT0, LOCAL_GL_TEXTURE_2D,
                                drawInfo);

      IntRect halfBounds = halfHandle->GetBounds();
      EnableScissor(halfBounds, true);
      if (!halfBacking->IsInitialized()) {
        halfBacking->MarkInitialized();
        // WebGL implicitly clears the backing texture the first time it is
        // used.
      } else if (i == 0 && !aSurface->GetRect().Contains(sourceRect)) {
        // Only clear if the blit does not completely fill the framebuffer.
        ClearRenderTex(halfBacking);
      }

      // Do a linear-scaled blit from the full-size to the half-size texture.
      IntRect fullBounds = sourceRect;
      if (fullHandle) {
        fullBounds += fullHandle->GetBounds().TopLeft();
      }
      mWebgl->AsWebGL2()->BlitFramebuffer(
          fullBounds.x, fullBounds.y, fullBounds.XMost(), fullBounds.YMost(),
          halfBounds.x, halfBounds.y, halfBounds.XMost(), halfBounds.YMost(),
          LOCAL_GL_COLOR_BUFFER_BIT, LOCAL_GL_LINEAR);

      fullHandle = halfHandle;
      fullTex = halfBacking->GetWebGLTexture();
      sourceRect = IntRect(IntPoint(), halfBounds.Size());
    }
    RestoreCurrentTarget();
    if (fullHandle) {
      if (sourceRect.IsEqualEdges(aSourceRect)) {
        return nullptr;
      }
      // Wrap the half-size texture with a surface.
      RefPtr<SourceSurfaceWebgl> surface = new SourceSurfaceWebgl(this);
      surface->SetHandle(fullHandle);
      return surface.forget();
    }
  }

  // If the full-size source surface is not actually a texture, downscale it
  // with a software filter as it will still improve upload performance while
  // reducing the final blur cost.
  IntRect sourceRect = aSourceRect;
  RefPtr<SourceSurface> fullSurface = aSurface;
  for (int i = 0; i < aIters; ++i) {
    IntSize halfSize = (sourceRect.Size() + IntSize(1, 1)) / 2;
    RefPtr<DrawTarget> halfDT = Factory::CreateDrawTarget(
        BackendType::SKIA, halfSize, aSurface->GetFormat());
    if (!halfDT) {
      break;
    }
    halfDT->DrawSurface(
        fullSurface, Rect(halfDT->GetRect()), Rect(sourceRect),
        DrawSurfaceOptions(SamplingFilter::LINEAR),
        DrawOptions(1.0f, aSurface->GetFormat() == SurfaceFormat::A8
                              ? CompositionOp::OP_OVER
                              : CompositionOp::OP_SOURCE));
    RefPtr<SourceSurface> halfSurface = halfDT->Snapshot();
    if (!halfSurface) {
      break;
    }
    fullSurface = halfSurface;
    sourceRect = halfSurface->GetRect();
  }
  if (sourceRect.IsEqualEdges(aSourceRect)) {
    return nullptr;
  }
  return fullSurface.forget();
}

// Blurs a surface and draws the result at the specified offset.
bool DrawTargetWebgl::BlurSurface(float aSigma, SourceSurface* aSurface,
                                  const IntRect& aSourceRect,
                                  const Point& aDest,
                                  const DrawOptions& aOptions,
                                  const DeviceColor& aColor) {
  IntRect sourceRect =
      aSourceRect.IsEmpty() ? aSurface->GetRect() : aSourceRect;
  if (aSigma >= 0.0f && aSigma <= BLUR_ACCEL_SIGMA_MAX &&
      ShouldAccelPath(aOptions, nullptr,
                      Rect(aDest, Size(sourceRect.Size())))) {
    Maybe<DeviceColor> maskColor =
        aSurface->GetFormat() == SurfaceFormat::A8 ? Some(aColor) : Nothing();
    if (aSigma < BLUR_ACCEL_SIGMA_MIN) {
      SurfacePattern maskPattern(aSurface, ExtendMode::CLAMP,
                                 Matrix::Translation(aDest));
      if (!sourceRect.IsEqualEdges(aSurface->GetRect())) {
        maskPattern.mSamplingRect = sourceRect;
      }
      return DrawRect(Rect(aDest, Size(sourceRect.Size())), maskPattern,
                      aOptions, maskColor);
    }
    // For large blurs, attempt to downscale the input texture so that
    // the blur radius can also be reduced to help performance.
    if (aSigma >= BLUR_ACCEL_DOWNSCALE_SIGMA &&
        std::max(sourceRect.width, sourceRect.height) >=
            BLUR_ACCEL_DOWNSCALE_SIZE) {
      if (RefPtr<SourceSurface> scaleSurf = mSharedContext->DownscaleBlurInput(
              aSurface, sourceRect, BLUR_ACCEL_DOWNSCALE_ITERS)) {
        // Approximate the large blur with a smaller blur that scales the sigma
        // proportionally to the surface size.
        IntSize scaleSize = scaleSurf->GetSize();
        Point scale(float(sourceRect.width) / float(scaleSize.width),
                    float(sourceRect.height) / float(scaleSize.height));
        RefPtr<TextureHandle> resultHandle;
        if (mSharedContext->BlurRectAccel(
                Rect(scaleSurf->GetRect()),
                Point(aSigma / scale.x, aSigma / scale.y), scaleSurf,
                scaleSurf->GetRect(), DrawOptions(), Nothing(), nullptr,
                nullptr, &resultHandle, true) &&
            resultHandle) {
          IntSize blurMargin = (resultHandle->GetSize() - scaleSize) / 2;
          Point blurOrigin = aDest - Point(blurMargin.width * scale.x,
                                           blurMargin.height * scale.y);
          SurfacePattern blurPattern(
              nullptr, ExtendMode::CLAMP,
              Matrix::Scaling(scale.x, scale.y).PostTranslate(blurOrigin));
          return mSharedContext->DrawRectAccel(
              Rect(blurOrigin,
                   Size(resultHandle->GetSize()) * Size(scale.x, scale.y)),
              blurPattern,
              DrawOptions(aOptions.mAlpha, aOptions.mCompositionOp,
                          AntialiasMode::DEFAULT),
              maskColor, &resultHandle, true, true, true);
        }
      }
    }
    if (mTransform.IsTranslation() &&
        !mSharedContext->RequiresMultiStageBlend(aOptions, this)) {
      return mSharedContext->BlurRectAccel(
          Rect(aDest + mTransform.GetTranslation(), Size(sourceRect.Size())),
          Point(aSigma, aSigma), aSurface, sourceRect, aOptions, maskColor,
          nullptr, nullptr, nullptr, true);
    }
    RefPtr<TextureHandle> resultHandle;
    if (mSharedContext->BlurRectAccel(
            Rect(Point(0, 0), Size(sourceRect.Size())), Point(aSigma, aSigma),
            aSurface, sourceRect, DrawOptions(), Nothing(), nullptr, nullptr,
            &resultHandle, true) &&
        resultHandle) {
      IntSize blurMargin = (resultHandle->GetSize() - sourceRect.Size()) / 2;
      Point blurOrigin = aDest - Point(blurMargin.width, blurMargin.height);
      SurfacePattern blurPattern(nullptr, ExtendMode::CLAMP,
                                 Matrix::Translation(blurOrigin));
      return mSharedContext->DrawRectAccel(
          Rect(blurOrigin, Size(resultHandle->GetSize())), blurPattern,
          aOptions, maskColor, &resultHandle, true, true, true);
    }
  }
  return false;
}

static inline int RoundToFactor(int aDim, int aFactor) {
  // If the size is either greater than the factor or not power-of-two, round it
  // up to the round factor.
  int mask = aFactor - 1;
  return aDim > 1 && (aDim > mask || (aDim & (aDim - 1)))
             ? (aDim + mask) & ~mask
             : aDim;
}

already_AddRefed<TextureHandle> SharedContextWebgl::ResolveFilterInputAccel(
    DrawTargetWebgl* aDT, const Path* aPath, const Pattern& aPattern,
    const IntRect& aSourceRect, const Matrix& aDestTransform,
    const DrawOptions& aOptions, const StrokeOptions* aStrokeOptions,
    SurfaceFormat aFormat) {
  if (SupportsDrawOptions(aOptions) != SupportsDrawOptionsStatus::Yes) {
    return nullptr;
  }
  if (IsContextLost()) {
    return nullptr;
  }
  // Round size to account for potential mipping from blur filters.
  int roundFactor = 2 << BLUR_ACCEL_DOWNSCALE_ITERS;
  IntSize roundSize =
      std::max(aSourceRect.width, aSourceRect.height) >=
              BLUR_ACCEL_DOWNSCALE_SIZE
          ? IntSize(RoundToFactor(aSourceRect.width, roundFactor),
                    RoundToFactor(aSourceRect.height, roundFactor))
          : aSourceRect.Size();
  RefPtr<TextureHandle> handle =
      AllocateTextureHandle(aFormat, roundSize, true, true);
  if (!handle) {
    return nullptr;
  }

  BackingTexture* targetBacking = handle->GetBackingTexture();
  InitRenderTex(targetBacking);
  if (!aDT->PrepareContext(false, handle)) {
    return nullptr;
  }
  DisableScissor();
  ClearRenderTex(targetBacking);

  AutoRestoreTransform restore(aDT);
  aDT->SetTransform(
      Matrix(aDestTransform).PostTranslate(-aSourceRect.TopLeft()));

  const SkPath& skiaPath = static_cast<const PathSkia*>(aPath)->GetPath();
  SkRect skiaRect = SkRect::MakeEmpty();
  // Draw the path as a simple rectangle with a supported pattern when
  // possible.
  if (!aStrokeOptions && skiaPath.isRect(&skiaRect)) {
    RectDouble rect = SkRectToRectDouble(skiaRect);
    RectDouble xformRect = aDT->TransformDouble(rect);
    if (aPattern.GetType() == PatternType::COLOR) {
      if (Maybe<Rect> clipped = aDT->RectClippedToViewport(xformRect)) {
        // If the pattern is transform-invariant and the rect clips to
        // the viewport, just clip drawing to the viewport to avoid
        // transform issues.
        if (DrawRectAccel(*clipped, aPattern, aOptions, Nothing(), nullptr,
                          false, false, true)) {
          return handle.forget();
        }
        return nullptr;
      }
    }
    if (RectInsidePrecisionLimits(xformRect)) {
      if (SupportsPattern(aPattern)) {
        if (DrawRectAccel(NarrowToFloat(rect), aPattern, aOptions, Nothing(),
                          nullptr, true, true, true)) {
          return handle.forget();
        }
        return nullptr;
      }
      if (aPattern.GetType() == PatternType::LINEAR_GRADIENT) {
        if (Maybe<SurfacePattern> surface =
                aDT->LinearGradientToSurface(xformRect, aPattern)) {
          if (DrawRectAccel(NarrowToFloat(rect), *surface, aOptions, Nothing(),
                            nullptr, true, true, true)) {
            return handle.forget();
          }
          return nullptr;
        }
      }
    }
  }
  if (DrawPathAccel(aPath, aPattern, aOptions, aStrokeOptions)) {
    return handle.forget();
  }
  return nullptr;
}

already_AddRefed<SourceSurfaceWebgl> DrawTargetWebgl::ResolveFilterInputAccel(
    const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
    const Matrix& aDestTransform, const DrawOptions& aOptions,
    const StrokeOptions* aStrokeOptions, SurfaceFormat aFormat) {
  if (RefPtr<TextureHandle> handle = mSharedContext->ResolveFilterInputAccel(
          this, aPath, aPattern, aSourceRect, aDestTransform, aOptions,
          aStrokeOptions, aFormat)) {
    RefPtr<SourceSurfaceWebgl> surface = new SourceSurfaceWebgl(mSharedContext);
    surface->SetHandle(handle);
    return surface.forget();
  }
  return nullptr;
}

bool SharedContextWebgl::RemoveSharedTexture(
    const RefPtr<SharedTexture>& aTexture) {
  auto pos =
      std::find(mSharedTextures.begin(), mSharedTextures.end(), aTexture);
  if (pos == mSharedTextures.end()) {
    return false;
  }
  // Keep around a reserve of empty pages to avoid initialization costs from
  // allocating shared pages. If still below the limit of reserved pages, then
  // just add it to the reserve. Otherwise, erase the empty texture page.
  size_t maxBytes = StaticPrefs::gfx_canvas_accelerated_reserve_empty_cache()
                    << 20;
  size_t totalEmpty = mEmptyTextureMemory + aTexture->UsedBytes();
  if (totalEmpty <= maxBytes) {
    mEmptyTextureMemory = totalEmpty;
  } else {
    RemoveTextureMemory(aTexture);
    mSharedTextures.erase(pos);
    ClearLastTexture();
  }
  return true;
}

void SharedTextureHandle::Cleanup(SharedContextWebgl& aContext) {
  mTexture->Free(*this);

  // Check if the shared handle's owning page has no more allocated handles
  // after we freed it. If so, remove the empty shared texture page also.
  if (!mTexture->HasAllocatedHandles()) {
    aContext.RemoveSharedTexture(mTexture);
  }
}

bool SharedContextWebgl::RemoveStandaloneTexture(
    const RefPtr<StandaloneTexture>& aTexture) {
  auto pos = std::find(mStandaloneTextures.begin(), mStandaloneTextures.end(),
                       aTexture);
  if (pos == mStandaloneTextures.end()) {
    return false;
  }
  RemoveTextureMemory(aTexture);
  mStandaloneTextures.erase(pos);
  ClearLastTexture();
  return true;
}

void StandaloneTexture::Cleanup(SharedContextWebgl& aContext) {
  aContext.RemoveStandaloneTexture(this);
}

// Prune a given texture handle and release its associated resources.
void SharedContextWebgl::PruneTextureHandle(
    const RefPtr<TextureHandle>& aHandle) {
  // Invalidate the handle so nothing will subsequently use its contents.
  aHandle->Invalidate();
  // If the handle has an associated SourceSurface, unlink it.
  UnlinkSurfaceTexture(aHandle);
  // If the handle has an associated CacheEntry, unlink it.
  if (RefPtr<CacheEntry> entry = aHandle->GetCacheEntry()) {
    entry->Unlink();
  }
  // Deduct the used space from the total.
  mUsedTextureMemory -= aHandle->UsedBytes();
  // Ensure any allocated shared or standalone texture regions get freed.
  aHandle->Cleanup(*this);
}

// Prune any texture memory above the limit (or margin below the limit) or any
// least-recently-used handles that are no longer associated with any usable
// surface.
bool SharedContextWebgl::PruneTextureMemory(size_t aMargin, bool aPruneUnused) {
  // The maximum amount of texture memory that may be used by textures.
  size_t maxBytes = StaticPrefs::gfx_canvas_accelerated_cache_size() << 20;
  maxBytes -= std::min(maxBytes, aMargin);
  size_t maxItems = StaticPrefs::gfx_canvas_accelerated_cache_items();
  size_t oldItems = mNumTextureHandles;
  while (!mTextureHandles.isEmpty() &&
         (mUsedTextureMemory > maxBytes || mNumTextureHandles > maxItems ||
          (aPruneUnused && !mTextureHandles.getLast()->IsUsed()))) {
    PruneTextureHandle(mTextureHandles.popLast());
    --mNumTextureHandles;
  }
  return mNumTextureHandles < oldItems;
}

// Attempt to convert a linear gradient to a 1D ramp texture.
Maybe<SurfacePattern> DrawTargetWebgl::LinearGradientToSurface(
    const RectDouble& aBounds, const Pattern& aPattern) {
  MOZ_ASSERT(aPattern.GetType() == PatternType::LINEAR_GRADIENT);
  const auto& gradient = static_cast<const LinearGradientPattern&>(aPattern);
  // The gradient points must be transformed by the gradient's matrix.
  Point gradBegin = gradient.mMatrix.TransformPoint(gradient.mBegin);
  Point gradEnd = gradient.mMatrix.TransformPoint(gradient.mEnd);
  // Get the gradient points in user-space.
  Point begin = mTransform.TransformPoint(gradBegin);
  Point end = mTransform.TransformPoint(gradEnd);
  // Find the normalized direction of the gradient and its length.
  Point dir = end - begin;
  float len = dir.Length();
  dir = dir / len;
  // Restrict the rendered bounds to fall within the canvas.
  Rect visBounds = NarrowToFloat(aBounds.SafeIntersect(RectDouble(GetRect())));
  // Calculate the distances along the gradient direction of the bounds.
  float dist0 = (visBounds.TopLeft() - begin).DotProduct(dir);
  float distX = visBounds.width * dir.x;
  float distY = visBounds.height * dir.y;
  float minDist = floorf(
      std::max(dist0 + std::min(distX, 0.0f) + std::min(distY, 0.0f), 0.0f));
  float maxDist = ceilf(
      std::min(dist0 + std::max(distX, 0.0f) + std::max(distY, 0.0f), len));
  // Calculate the approximate size of the ramp texture, and see if it would be
  // sufficiently smaller than just rendering the primitive.
  float subLen = maxDist - minDist;
  if (subLen > 0 && subLen < 0.5f * visBounds.Area()) {
    // Create a 1D texture to contain the gradient ramp. Reserve two extra
    // texels at the beginning and end of the ramp to account for clamping.
    RefPtr<DrawTargetSkia> dt = new DrawTargetSkia;
    if (dt->Init(IntSize(int32_t(subLen + 2), 1), SurfaceFormat::B8G8R8A8)) {
      // Fill the section of the gradient ramp that is actually used.
      dt->FillRect(Rect(dt->GetRect()),
                   LinearGradientPattern(Point(1 - minDist, 0.0f),
                                         Point(len + 1 - minDist, 0.0f),
                                         gradient.mStops));
      if (RefPtr<SourceSurface> snapshot = dt->Snapshot()) {
        // Calculate a matrix that will map the gradient ramp texture onto the
        // actual direction of the gradient.
        Point gradDir = (gradEnd - gradBegin) / len;
        Point tangent = Point(-gradDir.y, gradDir.x) / gradDir.Length();
        SurfacePattern surfacePattern(
            snapshot, ExtendMode::CLAMP,
            Matrix(gradDir.x, gradDir.y, tangent.x, tangent.y, gradBegin.x,
                   gradBegin.y)
                .PreTranslate(minDist - 1, 0));
        if (SupportsPattern(surfacePattern)) {
          return Some(surfacePattern);
        }
      }
    }
  }
  return Nothing();
}

void DrawTargetWebgl::FillRect(const Rect& aRect, const Pattern& aPattern,
                               const DrawOptions& aOptions) {
  RectDouble xformRect = TransformDouble(aRect);
  if (aPattern.GetType() == PatternType::COLOR) {
    if (Maybe<Rect> clipped = RectClippedToViewport(xformRect)) {
      // If the pattern is transform-invariant and the rect clips to the
      // viewport, just clip drawing to the viewport to avoid transform
      // issues.
      DrawRect(*clipped, aPattern, aOptions, Nothing(), nullptr, false);
      return;
    }
  }
  if (RectInsidePrecisionLimits(xformRect)) {
    if (SupportsPattern(aPattern)) {
      DrawRect(aRect, aPattern, aOptions);
      return;
    }
    if (aPattern.GetType() == PatternType::LINEAR_GRADIENT) {
      if (Maybe<SurfacePattern> surface =
              LinearGradientToSurface(xformRect, aPattern)) {
        if (DrawRect(aRect, *surface, aOptions, Nothing(), nullptr, true, true,
                     true)) {
          return;
        }
      }
    }
  }

  if (!mWebglValid) {
    MarkSkiaChanged(aOptions);
    mSkia->FillRect(aRect, aPattern, aOptions);
  } else {
    // If the pattern is unsupported, then transform the rect to a path so it
    // can be cached.
    SkPath skiaPath;
    skiaPath.addRect(RectToSkRect(aRect));
    RefPtr<PathSkia> path = new PathSkia(skiaPath, FillRule::FILL_WINDING);
    DrawPath(path, aPattern, aOptions);
  }
}

void CacheEntry::Link(const RefPtr<TextureHandle>& aHandle) {
  mHandle = aHandle;
  mHandle->SetCacheEntry(this);
}

// When the CacheEntry becomes unused, it marks the corresponding
// TextureHandle as unused and unlinks it from the CacheEntry. The
// entry is removed from its containing Cache, if applicable.
void CacheEntry::Unlink() {
  // The entry may not have a valid handle if rasterization failed.
  if (mHandle) {
    mHandle->SetCacheEntry(nullptr);
    mHandle = nullptr;
  }

  RemoveFromList();
}

// Hashes a path and pattern to a single hash value that can be used for quick
// comparisons. This currently avoids to expensive hashing of internal path
// and pattern data for speed, relying instead on later exact comparisons for
// disambiguation.
HashNumber PathCacheEntry::HashPath(const QuantizedPath& aPath,
                                    const Pattern* aPattern,
                                    const Matrix& aTransform,
                                    const IntRect& aBounds,
                                    const Point& aOrigin) {
  HashNumber hash = 0;
  hash = AddToHash(hash, aPath.mPath.num_types);
  hash = AddToHash(hash, aPath.mPath.num_points);
  if (aPath.mPath.num_points > 0) {
    hash = AddToHash(hash, aPath.mPath.points[0].x);
    hash = AddToHash(hash, aPath.mPath.points[0].y);
    if (aPath.mPath.num_points > 1) {
      hash = AddToHash(hash, aPath.mPath.points[1].x);
      hash = AddToHash(hash, aPath.mPath.points[1].y);
    }
  }
  // Quantize the relative offset of the path to its bounds.
  IntPoint offset = RoundedToInt((aOrigin - Point(aBounds.TopLeft())) * 16.0f);
  hash = AddToHash(hash, offset.x);
  hash = AddToHash(hash, offset.y);
  hash = AddToHash(hash, aBounds.width);
  hash = AddToHash(hash, aBounds.height);
  if (aPattern) {
    hash = AddToHash(hash, (int)aPattern->GetType());
  }
  return hash;
}

// When caching rendered geometry, we need to ensure the scale and orientation
// is approximately the same. The offset will be considered separately.
static inline bool HasMatchingScale(const Matrix& aTransform1,
                                    const Matrix& aTransform2) {
  return FuzzyEqual(aTransform1._11, aTransform2._11) &&
         FuzzyEqual(aTransform1._22, aTransform2._22) &&
         FuzzyEqual(aTransform1._12, aTransform2._12) &&
         FuzzyEqual(aTransform1._21, aTransform2._21);
}

static const float kIgnoreSigma = 1e6f;

// Determines if an existing path cache entry matches an incoming path and
// pattern.
inline bool PathCacheEntry::MatchesPath(
    const QuantizedPath& aPath, const Pattern* aPattern,
    const StrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
    const Matrix& aTransform, const IntRect& aBounds, const Point& aOrigin,
    HashNumber aHash, float aSigma) {
  return aHash == mHash && HasMatchingScale(aTransform, mTransform) &&
         // Ensure the clipped relative bounds fit inside those of the entry
         aBounds.x - aOrigin.x >= mBounds.x - mOrigin.x &&
         (aBounds.x - aOrigin.x) + aBounds.width <=
             (mBounds.x - mOrigin.x) + mBounds.width &&
         aBounds.y - aOrigin.y >= mBounds.y - mOrigin.y &&
         (aBounds.y - aOrigin.y) + aBounds.height <=
             (mBounds.y - mOrigin.y) + mBounds.height &&
         aPath == mPath &&
         (!aPattern ? !mPattern : mPattern && *aPattern == *mPattern) &&
         (!aStrokeOptions
              ? !mStrokeOptions
              : mStrokeOptions && *aStrokeOptions == *mStrokeOptions &&
                    mAAStrokeMode == aStrokeMode) &&
         (aSigma == kIgnoreSigma || aSigma == mSigma);
}

PathCacheEntry::PathCacheEntry(QuantizedPath&& aPath, Pattern* aPattern,
                               StoredStrokeOptions* aStrokeOptions,
                               AAStrokeMode aStrokeMode,
                               const Matrix& aTransform, const IntRect& aBounds,
                               const Point& aOrigin, HashNumber aHash,
                               float aSigma)
    : CacheEntryImpl<PathCacheEntry>(aTransform, aBounds, aHash),
      mPath(std::move(aPath)),
      mOrigin(aOrigin),
      mPattern(aPattern),
      mStrokeOptions(aStrokeOptions),
      mAAStrokeMode(aStrokeMode),
      mSigma(aSigma) {}

// Attempt to find a matching entry in the path cache. If one isn't found,
// a new entry will be created. The caller should check whether the contained
// texture handle is valid to determine if it will need to render the text run
// or just reuse the cached texture.
already_AddRefed<PathCacheEntry> PathCache::FindOrInsertEntry(
    QuantizedPath aPath, const Pattern* aPattern,
    const StrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
    const Matrix& aTransform, const IntRect& aBounds, const Point& aOrigin,
    float aSigma) {
  HashNumber hash =
      PathCacheEntry::HashPath(aPath, aPattern, aTransform, aBounds, aOrigin);
  for (const RefPtr<PathCacheEntry>& entry : GetChain(hash)) {
    if (entry->MatchesPath(aPath, aPattern, aStrokeOptions, aStrokeMode,
                           aTransform, aBounds, aOrigin, hash, aSigma)) {
      return do_AddRef(entry);
    }
  }
  Pattern* pattern = nullptr;
  if (aPattern) {
    pattern = aPattern->CloneWeak();
    if (!pattern) {
      return nullptr;
    }
  }
  StoredStrokeOptions* strokeOptions = nullptr;
  if (aStrokeOptions) {
    strokeOptions = aStrokeOptions->Clone();
    if (!strokeOptions) {
      return nullptr;
    }
  }
  RefPtr<PathCacheEntry> entry =
      new PathCacheEntry(std::move(aPath), pattern, strokeOptions, aStrokeMode,
                         aTransform, aBounds, aOrigin, hash, aSigma);
  Insert(entry);
  return entry.forget();
}

// Attempt to find a matching entry in the path cache. If one isn't found,
// just return failure and don't actually create an entry.
already_AddRefed<PathCacheEntry> PathCache::FindEntry(
    const QuantizedPath& aPath, const Pattern* aPattern,
    const StrokeOptions* aStrokeOptions, AAStrokeMode aStrokeMode,
    const Matrix& aTransform, const IntRect& aBounds, const Point& aOrigin,
    float aSigma, bool aHasSecondaryHandle) {
  HashNumber hash =
      PathCacheEntry::HashPath(aPath, aPattern, aTransform, aBounds, aOrigin);
  for (const RefPtr<PathCacheEntry>& entry : GetChain(hash)) {
    if (entry->MatchesPath(aPath, aPattern, aStrokeOptions, aStrokeMode,
                           aTransform, aBounds, aOrigin, hash, aSigma) &&
        (!aHasSecondaryHandle || (entry->GetSecondaryHandle() &&
                                  entry->GetSecondaryHandle()->IsValid()))) {
      return do_AddRef(entry);
    }
  }
  return nullptr;
}

void DrawTargetWebgl::Fill(const Path* aPath, const Pattern& aPattern,
                           const DrawOptions& aOptions) {
  if (!aPath || aPath->GetBackendType() != BackendType::SKIA) {
    return;
  }

  const SkPath& skiaPath = static_cast<const PathSkia*>(aPath)->GetPath();
  SkRect skiaRect = SkRect::MakeEmpty();
  // Draw the path as a simple rectangle with a supported pattern when possible.
  if (skiaPath.isRect(&skiaRect)) {
    RectDouble rect = SkRectToRectDouble(skiaRect);
    RectDouble xformRect = TransformDouble(rect);
    if (aPattern.GetType() == PatternType::COLOR) {
      if (Maybe<Rect> clipped = RectClippedToViewport(xformRect)) {
        // If the pattern is transform-invariant and the rect clips to the
        // viewport, just clip drawing to the viewport to avoid transform
        // issues.
        DrawRect(*clipped, aPattern, aOptions, Nothing(), nullptr, false);
        return;
      }
    }

    if (RectInsidePrecisionLimits(xformRect)) {
      if (SupportsPattern(aPattern)) {
        DrawRect(NarrowToFloat(rect), aPattern, aOptions);
        return;
      }
      if (aPattern.GetType() == PatternType::LINEAR_GRADIENT) {
        if (Maybe<SurfacePattern> surface =
                LinearGradientToSurface(xformRect, aPattern)) {
          if (DrawRect(NarrowToFloat(rect), *surface, aOptions, Nothing(),
                       nullptr, true, true, true)) {
            return;
          }
        }
      }
    }
  }

  DrawPath(aPath, aPattern, aOptions);
}

void DrawTargetWebgl::FillCircle(const Point& aOrigin, float aRadius,
                                 const Pattern& aPattern,
                                 const DrawOptions& aOptions) {
  DrawCircle(aOrigin, aRadius, aPattern, aOptions);
}

QuantizedPath::QuantizedPath(const WGR::Path& aPath) : mPath(aPath) {}

QuantizedPath::QuantizedPath(QuantizedPath&& aPath) noexcept
    : mPath(aPath.mPath) {
  aPath.mPath.points = nullptr;
  aPath.mPath.num_points = 0;
  aPath.mPath.types = nullptr;
  aPath.mPath.num_types = 0;
}

QuantizedPath::~QuantizedPath() {
  if (mPath.points || mPath.types) {
    WGR::wgr_path_release(mPath);
  }
}

bool QuantizedPath::operator==(const QuantizedPath& aOther) const {
  return mPath.num_types == aOther.mPath.num_types &&
         mPath.num_points == aOther.mPath.num_points &&
         mPath.fill_mode == aOther.mPath.fill_mode &&
         !memcmp(mPath.types, aOther.mPath.types,
                 mPath.num_types * sizeof(uint8_t)) &&
         !memcmp(mPath.points, aOther.mPath.points,
                 mPath.num_points * sizeof(WGR::Point));
}

// Generate a quantized path from the Skia path using WGR. The supplied
// transform will be applied to the path. The path is stored relative to its
// bounds origin to support translation later.
static Maybe<QuantizedPath> GenerateQuantizedPath(
    WGR::PathBuilder* aPathBuilder, const SkPath& aPath, const Rect& aBounds,
    const Matrix& aTransform) {
  if (!aPathBuilder) {
    return Nothing();
  }

  WGR::wgr_builder_reset(aPathBuilder);
  WGR::wgr_builder_set_fill_mode(aPathBuilder,
                                 aPath.getFillType() == SkPathFillType::kWinding
                                     ? WGR::FillMode::Winding
                                     : WGR::FillMode::EvenOdd);

  SkPath::RawIter iter(aPath);
  SkPoint params[4];
  SkPath::Verb currentVerb;

  // printf_stderr("bounds: (%d, %d) %d x %d\n", aBounds.x, aBounds.y,
  // aBounds.width, aBounds.height);
  Matrix transform = aTransform;
  transform.PostTranslate(-aBounds.TopLeft());
  while ((currentVerb = iter.next(params)) != SkPath::kDone_Verb) {
    switch (currentVerb) {
      case SkPath::kMove_Verb: {
        Point p0 = transform.TransformPoint(SkPointToPoint(params[0]));
        WGR::wgr_builder_move_to(aPathBuilder, p0.x, p0.y);
        break;
      }
      case SkPath::kLine_Verb: {
        Point p1 = transform.TransformPoint(SkPointToPoint(params[1]));
        WGR::wgr_builder_line_to(aPathBuilder, p1.x, p1.y);
        break;
      }
      case SkPath::kCubic_Verb: {
        Point p1 = transform.TransformPoint(SkPointToPoint(params[1]));
        Point p2 = transform.TransformPoint(SkPointToPoint(params[2]));
        Point p3 = transform.TransformPoint(SkPointToPoint(params[3]));
        // printf_stderr("cubic (%f, %f), (%f, %f), (%f, %f)\n", p1.x, p1.y,
        // p2.x, p2.y, p3.x, p3.y);
        WGR::wgr_builder_curve_to(aPathBuilder, p1.x, p1.y, p2.x, p2.y, p3.x,
                                  p3.y);
        break;
      }
      case SkPath::kQuad_Verb: {
        Point p1 = transform.TransformPoint(SkPointToPoint(params[1]));
        Point p2 = transform.TransformPoint(SkPointToPoint(params[2]));
        // printf_stderr("quad (%f, %f), (%f, %f)\n", p1.x, p1.y, p2.x, p2.y);
        WGR::wgr_builder_quad_to(aPathBuilder, p1.x, p1.y, p2.x, p2.y);
        break;
      }
      case SkPath::kConic_Verb: {
        Point p0 = transform.TransformPoint(SkPointToPoint(params[0]));
        Point p1 = transform.TransformPoint(SkPointToPoint(params[1]));
        Point p2 = transform.TransformPoint(SkPointToPoint(params[2]));
        float w = iter.conicWeight();
        std::vector<Point> quads;
        int numQuads = ConvertConicToQuads(p0, p1, p2, w, quads);
        for (int i = 0; i < numQuads; i++) {
          Point q1 = quads[2 * i + 1];
          Point q2 = quads[2 * i + 2];
          // printf_stderr("conic quad (%f, %f), (%f, %f)\n", q1.x, q1.y, q2.x,
          // q2.y);
          WGR::wgr_builder_quad_to(aPathBuilder, q1.x, q1.y, q2.x, q2.y);
        }
        break;
      }
      case SkPath::kClose_Verb:
        // printf_stderr("close\n");
        WGR::wgr_builder_close(aPathBuilder);
        break;
      default:
        MOZ_ASSERT(false);
        // Unexpected verb found in path!
        return Nothing();
    }
  }

  WGR::Path p = WGR::wgr_builder_get_path(aPathBuilder);
  if (!p.num_points || !p.num_types) {
    WGR::wgr_path_release(p);
    return Nothing();
  }
  return Some(QuantizedPath(p));
}

// Get the output vertex buffer using WGR from an input quantized path.
static Maybe<WGR::VertexBuffer> GeneratePathVertexBuffer(
    const QuantizedPath& aPath, const IntRect& aClipRect,
    bool aRasterizationTruncates, WGR::OutputVertex* aBuffer,
    size_t aBufferCapacity) {
  WGR::VertexBuffer vb = WGR::wgr_path_rasterize_to_tri_list(
      &aPath.mPath, aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height,
      true, false, aRasterizationTruncates, aBuffer, aBufferCapacity);
  if (!vb.len || (aBuffer && vb.len > aBufferCapacity)) {
    WGR::wgr_vertex_buffer_release(vb);
    return Nothing();
  }
  return Some(vb);
}

static inline AAStroke::LineJoin ToAAStrokeLineJoin(JoinStyle aJoin) {
  switch (aJoin) {
    case JoinStyle::BEVEL:
      return AAStroke::LineJoin::Bevel;
    case JoinStyle::ROUND:
      return AAStroke::LineJoin::Round;
    case JoinStyle::MITER:
    case JoinStyle::MITER_OR_BEVEL:
      return AAStroke::LineJoin::Miter;
  }
  return AAStroke::LineJoin::Miter;
}

static inline AAStroke::LineCap ToAAStrokeLineCap(CapStyle aCap) {
  switch (aCap) {
    case CapStyle::BUTT:
      return AAStroke::LineCap::Butt;
    case CapStyle::ROUND:
      return AAStroke::LineCap::Round;
    case CapStyle::SQUARE:
      return AAStroke::LineCap::Square;
  }
  return AAStroke::LineCap::Butt;
}

static inline Point WGRPointToPoint(const WGR::Point& aPoint) {
  // WGR points are 28.4 fixed-point where (0.0, 0.0) is assumed to be a pixel
  // center, as opposed to (0.5, 0.5) in canvas device space. WGR thus shifts
  // each point by (-0.5, -0.5). To undo this, transform from fixed-point back
  // to floating-point, and reverse the pixel shift by adding back (0.5, 0.5).
  return Point(IntPoint(aPoint.x, aPoint.y)) * (1.0f / 16.0f) +
         Point(0.5f, 0.5f);
}

// Generates a vertex buffer for a stroked path using aa-stroke.
static Maybe<AAStroke::VertexBuffer> GenerateStrokeVertexBuffer(
    const QuantizedPath& aPath, const StrokeOptions* aStrokeOptions,
    float aScale, WGR::OutputVertex* aBuffer, size_t aBufferCapacity) {
  AAStroke::StrokeStyle style = {aStrokeOptions->mLineWidth * aScale,
                                 ToAAStrokeLineCap(aStrokeOptions->mLineCap),
                                 ToAAStrokeLineJoin(aStrokeOptions->mLineJoin),
                                 aStrokeOptions->mMiterLimit};
  if (style.width <= 0.0f || !std::isfinite(style.width) ||
      style.miter_limit <= 0.0f || !std::isfinite(style.miter_limit)) {
    return Nothing();
  }
  AAStroke::Stroker* s = AAStroke::aa_stroke_new(
      &style, (AAStroke::OutputVertex*)aBuffer, aBufferCapacity);
  bool valid = true;
  size_t curPoint = 0;
  for (size_t curType = 0; valid && curType < aPath.mPath.num_types;) {
    // Verify that we are at the start of a sub-path.
    if ((aPath.mPath.types[curType] & WGR::PathPointTypePathTypeMask) !=
        WGR::PathPointTypeStart) {
      valid = false;
      break;
    }
    // Find where the next sub-path starts so we can locate the end.
    size_t endType = curType + 1;
    for (; endType < aPath.mPath.num_types; endType++) {
      if ((aPath.mPath.types[endType] & WGR::PathPointTypePathTypeMask) ==
          WGR::PathPointTypeStart) {
        break;
      }
    }
    // Check if the path is closed. This is a flag modifying the last type.
    bool closed =
        (aPath.mPath.types[endType - 1] & WGR::PathPointTypeCloseSubpath) != 0;
    for (; curType < endType; curType++) {
      // If this is the last type and the sub-path is not closed, determine if
      // this segment should be capped.
      bool end = curType + 1 == endType && !closed;
      switch (aPath.mPath.types[curType] & WGR::PathPointTypePathTypeMask) {
        case WGR::PathPointTypeStart: {
          if (curPoint + 1 > aPath.mPath.num_points) {
            valid = false;
            break;
          }
          Point p1 = WGRPointToPoint(aPath.mPath.points[curPoint]);
          AAStroke::aa_stroke_move_to(s, p1.x, p1.y, closed);
          if (end) {
            AAStroke::aa_stroke_line_to(s, p1.x, p1.y, true);
          }
          curPoint++;
          break;
        }
        case WGR::PathPointTypeLine: {
          if (curPoint + 1 > aPath.mPath.num_points) {
            valid = false;
            break;
          }
          Point p1 = WGRPointToPoint(aPath.mPath.points[curPoint]);
          AAStroke::aa_stroke_line_to(s, p1.x, p1.y, end);
          curPoint++;
          break;
        }
        case WGR::PathPointTypeBezier: {
          if (curPoint + 3 > aPath.mPath.num_points) {
            valid = false;
            break;
          }
          Point p1 = WGRPointToPoint(aPath.mPath.points[curPoint]);
          Point p2 = WGRPointToPoint(aPath.mPath.points[curPoint + 1]);
          Point p3 = WGRPointToPoint(aPath.mPath.points[curPoint + 2]);
          AAStroke::aa_stroke_curve_to(s, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y,
                                       end);
          curPoint += 3;
          break;
        }
        default:
          MOZ_ASSERT(false, "Unknown WGR path point type");
          valid = false;
          break;
      }
    }
    // Close the sub-path if necessary.
    if (valid && closed) {
      AAStroke::aa_stroke_close(s);
    }
  }
  Maybe<AAStroke::VertexBuffer> result;
  if (valid) {
    AAStroke::VertexBuffer vb = AAStroke::aa_stroke_finish(s);
    if (!vb.len || (aBuffer && vb.len > aBufferCapacity)) {
      AAStroke::aa_stroke_vertex_buffer_release(vb);
    } else {
      result = Some(vb);
    }
  }
  AAStroke::aa_stroke_release(s);
  return result;
}

// Search the path cache for any entries stored in the path vertex buffer and
// remove them.
void PathCache::ClearVertexRanges() {
  for (auto& chain : mChains) {
    PathCacheEntry* entry = chain.getFirst();
    while (entry) {
      PathCacheEntry* next = entry->getNext();
      if (entry->GetVertexRange().IsValid()) {
        entry->Unlink();
      }
      entry = next;
    }
  }
}

inline bool DrawTargetWebgl::ShouldAccelPath(
    const DrawOptions& aOptions, const StrokeOptions* aStrokeOptions,
    const Rect& aRect) {
  return mWebglValid && SupportsDrawOptions(aOptions, aRect) &&
         PrepareContext();
}

// For now, we only directly support stroking solid color patterns to limit
// artifacts from blending of overlapping geometry generated by AAStroke. Other
// types of patterns may be partially supported by rendering to a temporary
// mask.
static inline AAStrokeMode SupportsAAStroke(const Pattern& aPattern,
                                            const DrawOptions& aOptions,
                                            const StrokeOptions& aStrokeOptions,
                                            bool aAllowStrokeAlpha) {
  if (aStrokeOptions.mDashPattern) {
    return AAStrokeMode::Unsupported;
  }
  switch (aOptions.mCompositionOp) {
    case CompositionOp::OP_SOURCE:
      return AAStrokeMode::Geometry;
    case CompositionOp::OP_OVER:
      if (aPattern.GetType() == PatternType::COLOR) {
        return static_cast<const ColorPattern&>(aPattern).mColor.a *
                               aOptions.mAlpha <
                           1.0f &&
                       !aAllowStrokeAlpha
                   ? AAStrokeMode::Mask
                   : AAStrokeMode::Geometry;
      }
      return AAStrokeMode::Unsupported;
    default:
      return AAStrokeMode::Unsupported;
  }
}

// Render an AA-Stroke'd vertex range into an R8 mask texture for subsequent
// drawing.
already_AddRefed<TextureHandle> SharedContextWebgl::DrawStrokeMask(
    const PathVertexRange& aVertexRange, const IntSize& aSize) {
  // Allocate a new texture handle to store the rendered mask.
  RefPtr<TextureHandle> handle =
      AllocateTextureHandle(SurfaceFormat::A8, aSize, true, true);
  if (!handle) {
    return nullptr;
  }

  IntRect texBounds = handle->GetBounds();
  BindScratchFramebuffer(handle, true);

  // Reset any blending when drawing the mask.
  SetBlendState(CompositionOp::OP_OVER);

  // Set up the solid color shader to draw a simple opaque mask.
  if (mLastProgram != mSolidProgram) {
    mWebgl->UseProgram(mSolidProgram);
    mLastProgram = mSolidProgram;
  }
  Array<float, 2> viewportData = {float(texBounds.width),
                                  float(texBounds.height)};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mSolidProgramViewport, viewportData,
                   mSolidProgramUniformState.mViewport);
  Array<float, 1> aaData = {0.0f};
  MaybeUniformData(LOCAL_GL_FLOAT, mSolidProgramAA, aaData,
                   mSolidProgramUniformState.mAA);
  Array<float, 4> clipData = {-0.5f, -0.5f, float(texBounds.width) + 0.5f,
                              float(texBounds.height) + 0.5f};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mSolidProgramClipBounds, clipData,
                   mSolidProgramUniformState.mClipBounds);
  Array<float, 4> colorData = {1.0f, 1.0f, 1.0f, 1.0f};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC4, mSolidProgramColor, colorData,
                   mSolidProgramUniformState.mColor);
  Array<float, 6> xformData = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
  MaybeUniformData(LOCAL_GL_FLOAT_VEC2, mSolidProgramTransform, xformData,
                   mSolidProgramUniformState.mTransform);

  // Ensure the current clip mask is ignored.
  RefPtr<WebGLTexture> prevClipMask = mLastClipMask;
  SetNoClipMask();

  // Draw the mask using the supplied path vertex range.
  DrawTriangles(aVertexRange);

  // Restore the previous framebuffer state.
  RestoreCurrentTarget(prevClipMask);

  return handle.forget();
}

// Attempts to draw a path using WGR (or AAStroke), when possible.
bool SharedContextWebgl::DrawWGRPath(
    const Path* aPath, const IntRect& aIntBounds, const Rect& aQuantBounds,
    const Matrix& aPathXform, RefPtr<PathCacheEntry>& aEntry,
    const DrawOptions& aOptions, const StrokeOptions* aStrokeOptions,
    AAStrokeMode aAAStrokeMode, const Pattern& aPattern,
    const Maybe<DeviceColor>& aColor) {
  const PathSkia* pathSkia = static_cast<const PathSkia*>(aPath);
  const Matrix& currentTransform = mCurrentTarget->GetTransform();
  if (aEntry->GetVertexRange().IsValid()) {
    // If there is a valid cached vertex data in the path vertex buffer, then
    // just draw that. We must draw at integer pixel boundaries (using
    // intBounds instead of quantBounds) due to WGR's reliance on pixel center
    // location.
    mCurrentTarget->mProfile.OnCacheHit();
    return DrawRectAccel(Rect(aIntBounds.TopLeft(), Size(1, 1)), aPattern,
                         aOptions, Nothing(), nullptr, false, true, true, false,
                         nullptr, &aEntry->GetVertexRange());
  }

  // printf_stderr("Generating... verbs %d, points %d\n",
  //     int(pathSkia->GetPath().countVerbs()),
  //     int(pathSkia->GetPath().countPoints()));
  WGR::OutputVertex* outputBuffer = nullptr;
  size_t outputBufferCapacity = 0;
  if (mWGROutputBuffer) {
    outputBuffer = mWGROutputBuffer.get();
    outputBufferCapacity = mPathVertexCapacity / sizeof(WGR::OutputVertex);
  }
  Maybe<WGR::VertexBuffer> wgrVB;
  Maybe<AAStroke::VertexBuffer> strokeVB;
  if (!aStrokeOptions) {
    if (aPath == mUnitCirclePath) {
      auto scaleFactors = aPathXform.ScaleFactors();
      if (scaleFactors.AreScalesSame()) {
        Point center = aPathXform.GetTranslation() - aQuantBounds.TopLeft();
        float radius = scaleFactors.xScale;
        AAStroke::VertexBuffer vb = AAStroke::aa_stroke_filled_circle(
            center.x, center.y, radius, (AAStroke::OutputVertex*)outputBuffer,
            outputBufferCapacity);
        if (!vb.len || (outputBuffer && vb.len > outputBufferCapacity)) {
          AAStroke::aa_stroke_vertex_buffer_release(vb);
        } else {
          strokeVB = Some(vb);
        }
      }
    }
    if (!strokeVB) {
      wgrVB = GeneratePathVertexBuffer(
          aEntry->GetPath(), IntRect(-aIntBounds.TopLeft(), mViewportSize),
          mRasterizationTruncates, outputBuffer, outputBufferCapacity);
    }
  } else {
    if (aAAStrokeMode != AAStrokeMode::Unsupported) {
      auto scaleFactors = currentTransform.ScaleFactors();
      if (scaleFactors.AreScalesSame()) {
        strokeVB = GenerateStrokeVertexBuffer(aEntry->GetPath(), aStrokeOptions,
                                              scaleFactors.xScale, outputBuffer,
                                              outputBufferCapacity);
      }
    }
    if (!strokeVB && mPathWGRStroke) {
      //  If stroking, then generate a path to fill the stroked region. This
      //  path will need to be quantized again because it differs from the
      //  path used for the cache entry, but this allows us to avoid
      //  generating a fill path on a cache hit.
      Maybe<Rect> cullRect;
      Matrix invTransform = currentTransform;
      if (invTransform.Invert()) {
        // Transform the stroking clip rect from device space to local
        // space.
        Rect invRect = invTransform.TransformBounds(Rect(mClipRect));
        invRect.RoundOut();
        cullRect = Some(invRect);
      }
      SkPath fillPath;
      if (pathSkia->GetFillPath(*aStrokeOptions, aPathXform, fillPath,
                                cullRect)) {
        // printf_stderr("    stroke fill... verbs %d, points %d\n",
        //     int(fillPath.countVerbs()),
        //     int(fillPath.countPoints()));
        if (Maybe<QuantizedPath> qp = GenerateQuantizedPath(
                mWGRPathBuilder, fillPath, aQuantBounds, aPathXform)) {
          wgrVB = GeneratePathVertexBuffer(
              *qp, IntRect(-aIntBounds.TopLeft(), mViewportSize),
              mRasterizationTruncates, outputBuffer, outputBufferCapacity);
        }
      }
    }
  }
  if (!wgrVB && !strokeVB) {
    // Failed to generate any vertex data.
    return false;
  }
  const uint8_t* vbData =
      wgrVB ? (const uint8_t*)wgrVB->data : (const uint8_t*)strokeVB->data;
  if (outputBuffer && !vbData) {
    vbData = (const uint8_t*)outputBuffer;
  }
  size_t vbLen = wgrVB ? wgrVB->len : strokeVB->len;
  uint32_t vertexBytes =
      uint32_t(std::min(vbLen * sizeof(WGR::OutputVertex), size_t(UINT32_MAX)));
  // printf_stderr("  ... %d verts, %d bytes\n", int(vbLen),
  //     int(vertexBytes));
  if (vertexBytes > mPathVertexCapacity - mPathVertexOffset &&
      vertexBytes <= mPathVertexCapacity - sizeof(kRectVertexData)) {
    // If the vertex data is too large to fit in the remaining path vertex
    // buffer, then orphan the contents of the vertex buffer to make room
    // for it.
    if (mPathCache) {
      mPathCache->ClearVertexRanges();
    }
    ResetPathVertexBuffer();
  }
  if (vertexBytes > mPathVertexCapacity - mPathVertexOffset) {
    // There is insufficient space in the path buffer to fit vertex data.
    if (wgrVB) {
      WGR::wgr_vertex_buffer_release(wgrVB.ref());
    } else {
      AAStroke::aa_stroke_vertex_buffer_release(strokeVB.ref());
    }
    return false;
  }
  // If there is actually room to fit the vertex data in the vertex buffer
  // after orphaning as necessary, then upload the data to the next
  // available offset in the buffer.
  PathVertexRange vertexRange(
      uint32_t(mPathVertexOffset / sizeof(WGR::OutputVertex)), uint32_t(vbLen));
  // printf_stderr("      ... offset %d\n", mPathVertexOffset);
  // Normal glBufferSubData interleaved with draw calls causes performance
  // issues on Mali, so use our special unsynchronized version. This is
  // safe as we never update regions referenced by pending draw calls.
  mWebgl->BufferSubData(LOCAL_GL_ARRAY_BUFFER, mPathVertexOffset, vertexBytes,
                        vbData,
                        /* unsynchronized */ true);
  mPathVertexOffset += vertexBytes;
  if (wgrVB) {
    WGR::wgr_vertex_buffer_release(wgrVB.ref());
  } else {
    AAStroke::aa_stroke_vertex_buffer_release(strokeVB.ref());
  }
  if (strokeVB && aAAStrokeMode == AAStrokeMode::Mask) {
    // Attempt to generate a stroke mask for path.
    if (RefPtr<TextureHandle> handle =
            DrawStrokeMask(vertexRange, aIntBounds.Size())) {
      // Finally, draw the rendered stroke mask.
      if (aEntry) {
        aEntry->Link(handle);
      }
      mCurrentTarget->mProfile.OnCacheMiss();
      SurfacePattern maskPattern(nullptr, ExtendMode::CLAMP,
                                 Matrix::Translation(aQuantBounds.TopLeft()),
                                 SamplingFilter::GOOD);
      return DrawRectAccel(aQuantBounds, maskPattern, aOptions, aColor, &handle,
                           false, true, true);
    }
  } else {
    // Remember the vertex range in the cache entry so that it can be
    // reused later.
    if (aEntry) {
      aEntry->SetVertexRange(vertexRange);
    }

    // Finally, draw the uploaded vertex data.
    mCurrentTarget->mProfile.OnCacheMiss();
    return DrawRectAccel(Rect(aIntBounds.TopLeft(), Size(1, 1)), aPattern,
                         aOptions, Nothing(), nullptr, false, true, true, false,
                         nullptr, &vertexRange);
  }
  // If we failed to draw the vertex data for some reason, then fall back
  // to the texture rasterization path.
  return false;
}

bool SharedContextWebgl::DrawPathAccel(
    const Path* aPath, const Pattern& aPattern, const DrawOptions& aOptions,
    const StrokeOptions* aStrokeOptions, bool aAllowStrokeAlpha,
    const ShadowOptions* aShadow, bool aCacheable, const Matrix* aPathXform) {
  // Get the transformed bounds for the path and conservatively check if the
  // bounds overlap the canvas.
  const PathSkia* pathSkia = static_cast<const PathSkia*>(aPath);
  const Matrix& currentTransform = mCurrentTarget->GetTransform();
  Matrix pathXform = currentTransform;
  // If there is a path-specific transform that shouldn't be applied to the
  // pattern, then generate a matrix that should only be used with the Skia
  // path.
  if (aPathXform) {
    pathXform.PreMultiply(*aPathXform);
  }
  Rect bounds = pathSkia->GetFastBounds(pathXform, aStrokeOptions);
  // If the path is empty, then there is nothing to draw.
  if (bounds.IsEmpty()) {
    return true;
  }
  // Avoid integer conversion errors with abnormally large paths.
  if (!RectInsidePrecisionLimits(bounds)) {
    return false;
  }
  IntRect viewport(IntPoint(), mViewportSize);
  bool accelShadow = false;
  if (aShadow) {
    // Inflate the bounds to account for the blur radius.
    bounds += aShadow->mOffset;
    if (aShadow->mSigma > 0.0f && aShadow->mSigma <= BLUR_ACCEL_SIGMA_MAX &&
        !RequiresMultiStageBlend(aOptions)) {
      // Allow the input texture to be reused regardless of sigma since it
      // doesn't actually differ.
      viewport.Inflate(2 * BLUR_ACCEL_RADIUS_MAX);
      accelShadow = true;
    } else {
      // Software blurs require inflated inputs dependent on blur radius.
      int32_t blurRadius = aShadow->BlurRadius();
      bounds.Inflate(blurRadius);
      viewport.Inflate(blurRadius);
    }
  }
  Point realOrigin = bounds.TopLeft();
  if (aCacheable) {
    // Quantize the path origin to increase the reuse of cache entries.
    bounds.Scale(4.0f);
    bounds.Round();
    bounds.Scale(0.25f);
  }
  Point quantizedOrigin = bounds.TopLeft();
  // If the path doesn't intersect the viewport, then there is nothing to draw.
  IntRect intBounds = RoundedOut(bounds).Intersect(viewport);
  if (intBounds.IsEmpty()) {
    return true;
  }
  // Nudge the bounds to account for the quantization rounding.
  Rect quantBounds = Rect(intBounds) + (realOrigin - quantizedOrigin);
  // If the pattern is a solid color, then this will be used along with a path
  // mask to render the path, as opposed to baking the pattern into the cached
  // path texture.
  Maybe<DeviceColor> color =
      aOptions.mCompositionOp == CompositionOp::OP_CLEAR
          ? Some(DeviceColor(1, 1, 1, 1))
          : (aPattern.GetType() == PatternType::COLOR
                 ? Some(static_cast<const ColorPattern&>(aPattern).mColor)
                 : Nothing());
  AAStrokeMode aaStrokeMode =
      aStrokeOptions && mPathAAStroke
          ? SupportsAAStroke(aPattern, aOptions, *aStrokeOptions,
                             aAllowStrokeAlpha)
          : AAStrokeMode::Unsupported;
  // Look for an existing path cache entry, if possible, or otherwise create
  // one. If the draw request is not cacheable, then don't create an entry.
  RefPtr<PathCacheEntry> entry;
  RefPtr<TextureHandle> handle;
  if (aCacheable) {
    if (!mPathCache) {
      mPathCache = MakeUnique<PathCache>();
    }
    // Use a quantized, relative (to its bounds origin) version of the path as
    // a cache key to help limit cache bloat.
    Maybe<QuantizedPath> qp = GenerateQuantizedPath(
        mWGRPathBuilder, pathSkia->GetPath(), quantBounds, pathXform);
    if (!qp) {
      return false;
    }
    entry = mPathCache->FindOrInsertEntry(
        std::move(*qp), color ? nullptr : &aPattern, aStrokeOptions,
        aaStrokeMode, currentTransform, intBounds, quantizedOrigin,
        aShadow ? aShadow->mSigma : -1.0f);
    if (!entry) {
      return false;
    }
    handle = entry->GetHandle();
  }

  // If there is a shadow, it needs to draw with the shadow color rather than
  // the path color.
  Maybe<DeviceColor> shadowColor = color;
  if (aShadow && aOptions.mCompositionOp != CompositionOp::OP_CLEAR) {
    shadowColor = Some(aShadow->mColor);
    if (color) {
      shadowColor->a *= color->a;
    }
  }
  SamplingFilter filter =
      aShadow ? SamplingFilter::GOOD : GetSamplingFilter(aPattern);
  if (handle && handle->IsValid()) {
    if (accelShadow) {
      return BlurRectAccel(quantBounds, Point(aShadow->mSigma, aShadow->mSigma),
                           nullptr, IntRect(), aOptions, shadowColor, nullptr,
                           &handle);
    }

    // If the entry has a valid texture handle still, use it. However, the
    // entry texture is assumed to be located relative to its previous bounds.
    // We need to offset the pattern by the difference between its new unclipped
    // origin and its previous previous unclipped origin. Then when we finally
    // draw a rectangle at the expected new bounds, it will overlap the portion
    // of the old entry texture we actually need to sample from.
    Point offset =
        (realOrigin - entry->GetOrigin()) + entry->GetBounds().TopLeft();
    SurfacePattern pathPattern(nullptr, ExtendMode::CLAMP,
                               Matrix::Translation(offset), filter);
    return DrawRectAccel(quantBounds, pathPattern, aOptions, shadowColor,
                         &handle, false, true, true);
  }

  if (mPathVertexCapacity > 0 && !handle && entry && !aShadow &&
      aOptions.mAntialiasMode != AntialiasMode::NONE &&
      entry->GetPath().mPath.num_types <= mPathMaxComplexity) {
    if (aPattern.GetType() == PatternType::LINEAR_GRADIENT) {
      if (Maybe<SurfacePattern> gradient =
              mCurrentTarget->LinearGradientToSurface(WidenToDouble(bounds),
                                                      aPattern)) {
        if (DrawWGRPath(aPath, intBounds, quantBounds, pathXform, entry,
                        aOptions, aStrokeOptions, aaStrokeMode, gradient.ref(),
                        color)) {
          return true;
        }
      }
    } else if (SupportsPattern(aPattern) &&
               DrawWGRPath(aPath, intBounds, quantBounds, pathXform, entry,
                           aOptions, aStrokeOptions, aaStrokeMode, aPattern,
                           color)) {
      return true;
    }
  }

  // If a stroke path covers too much screen area, it is likely that most is
  // empty space in the interior. This usually imposes too high a cost versus
  // just rasterizing without acceleration. Note that AA-Stroke generally
  // produces more acceptable amounts of geometry for larger paths, so we do
  // this heuristic after we attempt AA-Stroke.
  if (aStrokeOptions &&
      intBounds.width * intBounds.height >
          (mViewportSize.width / 2) * (mViewportSize.height / 2)) {
    // Avoid filling cache with empty entries.
    if (entry) {
      entry->Unlink();
    }
    return false;
  }

  // If there is a similar shadow entry with a different blur radius that still
  // has a valid input texture cached. The blurred texture can be generated from
  // the previously cached input texture without incurring an upload cost.
  if (accelShadow && entry) {
    if (RefPtr<PathCacheEntry> similarEntry = mPathCache->FindEntry(
            entry->GetPath(), color ? nullptr : &aPattern, aStrokeOptions,
            aaStrokeMode, currentTransform, intBounds, quantizedOrigin,
            kIgnoreSigma, true)) {
      if (RefPtr<TextureHandle> inputHandle =
              similarEntry->GetSecondaryHandle().get()) {
        if (inputHandle->IsValid() &&
            BlurRectAccel(quantBounds, Point(aShadow->mSigma, aShadow->mSigma),
                          nullptr, IntRect(), aOptions, shadowColor,
                          &inputHandle, &handle)) {
          if (entry) {
            entry->Link(handle);
            entry->SetSecondaryHandle(WeakPtr(inputHandle));
          }
          return true;
        }
      }
    }
  }

  // If there isn't a valid texture handle, then we need to rasterize the
  // path in a software canvas and upload this to a texture. Solid color
  // patterns will be rendered as a path mask that can then be modulated
  // with any color. Other pattern types have to rasterize the pattern
  // directly into the cached texture.
  handle = nullptr;
  RefPtr<DrawTargetSkia> pathDT = new DrawTargetSkia;
  if (pathDT->Init(intBounds.Size(), color || aShadow
                                         ? SurfaceFormat::A8
                                         : SurfaceFormat::B8G8R8A8)) {
    Point offset = -quantBounds.TopLeft();
    if (aShadow) {
      // Ensure the the shadow is drawn at the requested offset
      offset += aShadow->mOffset;
    }
    DrawOptions drawOptions(1.0f, CompositionOp::OP_OVER,
                            aOptions.mAntialiasMode);
    static const ColorPattern maskPattern(DeviceColor(1.0f, 1.0f, 1.0f, 1.0f));
    const Pattern& cachePattern = color ? maskPattern : aPattern;
    // If the source pattern is a DrawTargetWebgl snapshot, we may shift
    // targets when drawing the path, so back up the old target.
    DrawTargetWebgl* oldTarget = mCurrentTarget;
    RefPtr<TextureHandle> oldHandle = mTargetHandle;
    IntSize oldViewport = mViewportSize;
    {
      RefPtr<const Path> path;
      if (!aPathXform || (color && !aStrokeOptions)) {
        // If the pattern is transform invariant or there is no pathXform, then
        // it is safe to use the path directly. Solid colors are transform
        // invariant, except when there are stroke options such as line width or
        // dashes that should not be scaled by pathXform.
        path = aPath;
        pathDT->SetTransform(pathXform * Matrix::Translation(offset));
      } else {
        // If there is a pathXform, then pre-apply that to the path to avoid
        // altering the pattern.
        RefPtr<PathBuilder> builder =
            aPath->TransformedCopyToBuilder(*aPathXform);
        path = builder->Finish();
        pathDT->SetTransform(currentTransform * Matrix::Translation(offset));
      }
      if (aStrokeOptions) {
        pathDT->Stroke(path, cachePattern, *aStrokeOptions, drawOptions);
      } else {
        pathDT->Fill(path, cachePattern, drawOptions);
      }
    }
    if (aShadow && aShadow->mSigma > 0.0f) {
      if (accelShadow) {
        RefPtr<SourceSurface> pathSurface = pathDT->Snapshot();
        // If the target changed, try to restore it.
        if ((mCurrentTarget == oldTarget && mTargetHandle == oldHandle &&
             mViewportSize == oldViewport) ||
            oldTarget->PrepareContext(!oldHandle, oldHandle, oldViewport)) {
          RefPtr<TextureHandle> inputHandle;
          // Generate the accelerated shadow from the software surface.
          if (BlurRectAccel(quantBounds,
                            Point(aShadow->mSigma, aShadow->mSigma),
                            pathSurface, IntRect(), aOptions, shadowColor,
                            &inputHandle, &handle)) {
            if (entry) {
              entry->Link(handle);
              entry->SetSecondaryHandle(WeakPtr(inputHandle));
            }
          } else if (entry) {
            entry->Unlink();
          }
          return true;
        }
        return false;
      }
      // Blur the shadow if required.
      GaussianBlur blur(Point(aShadow->mSigma, aShadow->mSigma));
      pathDT->Blur(blur);
    }
    RefPtr<SourceSurface> pathSurface = pathDT->Snapshot();
    // If the target changed, try to restore it.
    if (pathSurface &&
        ((mCurrentTarget == oldTarget && mTargetHandle == oldHandle &&
          mViewportSize == oldViewport) ||
         oldTarget->PrepareContext(!oldHandle, oldHandle, oldViewport))) {
      SurfacePattern pathPattern(pathSurface, ExtendMode::CLAMP,
                                 Matrix::Translation(quantBounds.TopLeft()),
                                 filter);
      // Try and upload the rasterized path to a texture. If there is a
      // valid texture handle after this, then link it to the entry.
      // Otherwise, we might have to fall back to software drawing the
      // path, so unlink it from the entry.
      if (DrawRectAccel(quantBounds, pathPattern, aOptions, shadowColor,
                        &handle, false, true) &&
          handle) {
        if (entry) {
          entry->Link(handle);
        }
      } else if (entry) {
        entry->Unlink();
      }
      return true;
    }
  }

  // Avoid filling cache with empty entries.
  if (entry) {
    entry->Unlink();
  }
  return false;
}

void DrawTargetWebgl::DrawPath(const Path* aPath, const Pattern& aPattern,
                               const DrawOptions& aOptions,
                               const StrokeOptions* aStrokeOptions,
                               bool aAllowStrokeAlpha) {
  // If there is a WebGL context, then try to cache the path to avoid slow
  // fallbacks.
  if (ShouldAccelPath(aOptions, aStrokeOptions) &&
      mSharedContext->DrawPathAccel(aPath, aPattern, aOptions, aStrokeOptions,
                                    aAllowStrokeAlpha)) {
    return;
  }

  // There was no path cache entry available to use, so fall back to drawing the
  // path with Skia.
  MarkSkiaChanged(aOptions);
  if (aStrokeOptions) {
    mSkia->Stroke(aPath, aPattern, *aStrokeOptions, aOptions);
  } else {
    mSkia->Fill(aPath, aPattern, aOptions);
  }
}

// DrawCircleAccel is a more specialized version of DrawPathAccel that attempts
// to cache a unit circle.
bool SharedContextWebgl::DrawCircleAccel(const Point& aCenter, float aRadius,
                                         const Pattern& aPattern,
                                         const DrawOptions& aOptions,
                                         const StrokeOptions* aStrokeOptions) {
  // Cache a unit circle and transform it to avoid creating a path repeatedly.
  if (!mUnitCirclePath) {
    mUnitCirclePath = MakePathForCircle(*mCurrentTarget, Point(0, 0), 1);
  }
  // Scale and translate the circle to the desired shape.
  Matrix circleXform(aRadius, 0, 0, aRadius, aCenter.x, aCenter.y);
  return DrawPathAccel(mUnitCirclePath, aPattern, aOptions, aStrokeOptions,
                       true, nullptr, true, &circleXform);
}

void DrawTargetWebgl::DrawCircle(const Point& aOrigin, float aRadius,
                                 const Pattern& aPattern,
                                 const DrawOptions& aOptions,
                                 const StrokeOptions* aStrokeOptions) {
  if (ShouldAccelPath(aOptions, aStrokeOptions) &&
      mSharedContext->DrawCircleAccel(aOrigin, aRadius, aPattern, aOptions,
                                      aStrokeOptions)) {
    return;
  }

  MarkSkiaChanged(aOptions);
  if (aStrokeOptions) {
    mSkia->StrokeCircle(aOrigin, aRadius, aPattern, *aStrokeOptions, aOptions);
  } else {
    mSkia->FillCircle(aOrigin, aRadius, aPattern, aOptions);
  }
}

void DrawTargetWebgl::DrawSurface(SourceSurface* aSurface, const Rect& aDest,
                                  const Rect& aSource,
                                  const DrawSurfaceOptions& aSurfOptions,
                                  const DrawOptions& aOptions) {
  Matrix matrix = Matrix::Scaling(aDest.width / aSource.width,
                                  aDest.height / aSource.height);
  matrix.PreTranslate(-aSource.TopLeft());
  matrix.PostTranslate(aDest.TopLeft());

  // Ensure the source rect is clipped to the surface bounds.
  Rect src = aSource.Intersect(Rect(aSurface->GetRect()));
  // Ensure the destination rect does not sample outside the surface bounds.
  Rect dest = matrix.TransformBounds(src).Intersect(aDest);
  SurfacePattern pattern(aSurface, ExtendMode::CLAMP, matrix,
                         aSurfOptions.mSamplingFilter);
  DrawRect(dest, pattern, aOptions);
}

void DrawTargetWebgl::Mask(const Pattern& aSource, const Pattern& aMask,
                           const DrawOptions& aOptions) {
  if (!SupportsDrawOptions(aOptions) ||
      aMask.GetType() != PatternType::SURFACE ||
      aSource.GetType() != PatternType::COLOR) {
    MarkSkiaChanged(aOptions);
    mSkia->Mask(aSource, aMask, aOptions);
    return;
  }
  auto sourceColor = static_cast<const ColorPattern&>(aSource).mColor;
  auto maskPattern = static_cast<const SurfacePattern&>(aMask);
  if (!maskPattern.mSurface) {
    return;
  }

  IntRect samplingRect = !maskPattern.mSamplingRect.IsEmpty()
                             ? maskPattern.mSamplingRect
                             : maskPattern.mSurface->GetRect();
  DrawRect(maskPattern.mMatrix.TransformBounds(Rect(samplingRect)), maskPattern,
           aOptions, Some(sourceColor));
}

void DrawTargetWebgl::MaskSurface(const Pattern& aSource, SourceSurface* aMask,
                                  Point aOffset, const DrawOptions& aOptions) {
  if (!SupportsDrawOptions(aOptions) ||
      aSource.GetType() != PatternType::COLOR) {
    MarkSkiaChanged(aOptions);
    mSkia->MaskSurface(aSource, aMask, aOffset, aOptions);
  } else {
    auto sourceColor = static_cast<const ColorPattern&>(aSource).mColor;
    SurfacePattern pattern(
        aMask, ExtendMode::CLAMP,
        Matrix::Translation(aOffset + aMask->GetRect().TopLeft()));
    DrawRect(Rect(aOffset, Size(aMask->GetSize())), pattern, aOptions,
             Some(sourceColor));
  }
}

// Extract the surface's alpha values into an A8 surface.
static already_AddRefed<DataSourceSurface> ExtractAlpha(SourceSurface* aSurface,
                                                        bool aAllowSubpixelAA) {
  RefPtr<DataSourceSurface> surfaceData = aSurface->GetDataSurface();
  if (!surfaceData) {
    return nullptr;
  }
  DataSourceSurface::ScopedMap srcMap(surfaceData, DataSourceSurface::READ);
  if (!srcMap.IsMapped()) {
    return nullptr;
  }
  IntSize size = surfaceData->GetSize();
  RefPtr<DataSourceSurface> alpha =
      Factory::CreateDataSourceSurface(size, SurfaceFormat::A8, false);
  if (!alpha) {
    return nullptr;
  }
  DataSourceSurface::ScopedMap dstMap(alpha, DataSourceSurface::WRITE);
  if (!dstMap.IsMapped()) {
    return nullptr;
  }
  // For subpixel masks, ignore the alpha and instead sample one of the color
  // channels as if they were alpha.
  SwizzleData(
      srcMap.GetData(), srcMap.GetStride(),
      aAllowSubpixelAA ? SurfaceFormat::A8R8G8B8 : surfaceData->GetFormat(),
      dstMap.GetData(), dstMap.GetStride(), SurfaceFormat::A8, size);
  return alpha.forget();
}

void DrawTargetWebgl::DrawShadow(const Path* aPath, const Pattern& aPattern,
                                 const ShadowOptions& aShadow,
                                 const DrawOptions& aOptions,
                                 const StrokeOptions* aStrokeOptions) {
  if (!aPath || aPath->GetBackendType() != BackendType::SKIA) {
    return;
  }

  // If there is a WebGL context, then try to cache the path to avoid slow
  // fallbacks.
  if (ShouldAccelPath(aOptions, aStrokeOptions) &&
      mSharedContext->DrawPathAccel(aPath, aPattern, aOptions, aStrokeOptions,
                                    false, &aShadow)) {
    return;
  }

  // There was no path cache entry available to use, so fall back to drawing the
  // path with Skia.
  MarkSkiaChanged(aOptions);
  mSkia->DrawShadow(aPath, aPattern, aShadow, aOptions, aStrokeOptions);
}

void DrawTargetWebgl::DrawSurfaceWithShadow(SourceSurface* aSurface,
                                            const Point& aDest,
                                            const ShadowOptions& aShadow,
                                            CompositionOp aOperator) {
  DrawOptions options(1.0f, aOperator);
  if (ShouldAccelPath(options, nullptr)) {
    SurfacePattern pattern(aSurface, ExtendMode::CLAMP,
                           Matrix::Translation(aDest));
    SkPath skiaPath;
    skiaPath.addRect(RectToSkRect(Rect(aSurface->GetRect()) + aDest));
    RefPtr<PathSkia> path = new PathSkia(skiaPath, FillRule::FILL_WINDING);
    AutoRestoreTransform restore(this);
    SetTransform(Matrix());
    if (mSharedContext->DrawPathAccel(path, pattern, options, nullptr, false,
                                      &aShadow, false)) {
      DrawRect(Rect(aSurface->GetRect()) + aDest, pattern, options);
      return;
    }
  }

  MarkSkiaChanged(options);
  mSkia->DrawSurfaceWithShadow(aSurface, aDest, aShadow, aOperator);
}

already_AddRefed<PathBuilder> DrawTargetWebgl::CreatePathBuilder(
    FillRule aFillRule) const {
  return mSkia->CreatePathBuilder(aFillRule);
}

void DrawTargetWebgl::SetTransform(const Matrix& aTransform) {
  DrawTarget::SetTransform(aTransform);
  mSkia->SetTransform(aTransform);
}

void DrawTargetWebgl::StrokeRect(const Rect& aRect, const Pattern& aPattern,
                                 const StrokeOptions& aStrokeOptions,
                                 const DrawOptions& aOptions) {
  if (!mWebglValid) {
    MarkSkiaChanged(aOptions);
    mSkia->StrokeRect(aRect, aPattern, aStrokeOptions, aOptions);
  } else {
    // If the stroke options are unsupported, then transform the rect to a path
    // so it can be cached.
    SkPath skiaPath;
    skiaPath.addRect(RectToSkRect(aRect));
    RefPtr<PathSkia> path = new PathSkia(skiaPath, FillRule::FILL_WINDING);
    DrawPath(path, aPattern, aOptions, &aStrokeOptions, true);
  }
}

static inline bool IsThinLine(const Matrix& aTransform,
                              const StrokeOptions& aStrokeOptions) {
  auto scale = aTransform.ScaleFactors();
  return std::max(scale.xScale, scale.yScale) * aStrokeOptions.mLineWidth <= 1;
}

bool DrawTargetWebgl::StrokeLineAccel(const Point& aStart, const Point& aEnd,
                                      const Pattern& aPattern,
                                      const StrokeOptions& aStrokeOptions,
                                      const DrawOptions& aOptions,
                                      bool aClosed) {
  // Approximating a wide line as a rectangle works only with certain cap styles
  // in the general case (butt or square). However, if the line width is
  // sufficiently thin, we can either ignore the round cap (or treat it like
  // square for zero-length lines) without causing objectionable artifacts.
  // Lines may sometimes be used in closed paths that immediately reverse back,
  // in which case we need to use mLineJoin instead of mLineCap to determine the
  // actual cap used.
  CapStyle capStyle =
      aClosed ? (aStrokeOptions.mLineJoin == JoinStyle::ROUND ? CapStyle::ROUND
                                                              : CapStyle::BUTT)
              : aStrokeOptions.mLineCap;
  if (mWebglValid && SupportsPattern(aPattern) &&
      (capStyle != CapStyle::ROUND ||
       IsThinLine(GetTransform(), aStrokeOptions)) &&
      aStrokeOptions.mDashPattern == nullptr && aStrokeOptions.mLineWidth > 0) {
    // Treat the line as a rectangle whose center-line is the supplied line and
    // for which the height is the supplied line width. Generate a matrix that
    // maps the X axis to the orientation of the line and the Y axis to the
    // normal vector to the line. This only works if the line caps are squared,
    // as rounded rectangles are currently not supported for round line caps.
    Point start = aStart;
    Point dirX = aEnd - aStart;
    Point dirY;
    float dirLen = dirX.Length();
    float scale = aStrokeOptions.mLineWidth;
    if (dirLen == 0.0f) {
      // If the line is zero-length, then only a cap is rendered.
      switch (capStyle) {
        case CapStyle::BUTT:
          // The cap doesn't extend beyond the line so nothing is drawn.
          return true;
        case CapStyle::ROUND:
        case CapStyle::SQUARE:
          // Draw a unit square centered at the single point.
          dirX = Point(scale, 0.0f);
          dirY = Point(0.0f, scale);
          // Offset the start by half a unit.
          start.x -= 0.5f * scale;
          break;
      }
    } else {
      // Make the scale map to a single unit length.
      scale /= dirLen;
      dirY = Point(-dirX.y, dirX.x) * scale;
      if (capStyle == CapStyle::SQUARE) {
        // Offset the start by half a unit.
        start -= (dirX * scale) * 0.5f;
        // Ensure the extent also accounts for the start and end cap.
        dirX += dirX * scale;
      }
    }
    Matrix lineXform(dirX.x, dirX.y, dirY.x, dirY.y, start.x - 0.5f * dirY.x,
                     start.y - 0.5f * dirY.y);
    if (PrepareContext() &&
        mSharedContext->DrawRectAccel(Rect(0, 0, 1, 1), aPattern, aOptions,
                                      Nothing(), nullptr, true, true, true,
                                      false, nullptr, nullptr, &lineXform)) {
      return true;
    }
  }
  return false;
}

void DrawTargetWebgl::StrokeLine(const Point& aStart, const Point& aEnd,
                                 const Pattern& aPattern,
                                 const StrokeOptions& aStrokeOptions,
                                 const DrawOptions& aOptions) {
  if (!mWebglValid) {
    MarkSkiaChanged(aOptions);
    mSkia->StrokeLine(aStart, aEnd, aPattern, aStrokeOptions, aOptions);
  } else if (!StrokeLineAccel(aStart, aEnd, aPattern, aStrokeOptions,
                              aOptions)) {
    // If the stroke options are unsupported, then transform the line to a path
    // so it can be cached.
    SkPath skiaPath;
    skiaPath.moveTo(PointToSkPoint(aStart));
    skiaPath.lineTo(PointToSkPoint(aEnd));
    RefPtr<PathSkia> path = new PathSkia(skiaPath, FillRule::FILL_WINDING);
    DrawPath(path, aPattern, aOptions, &aStrokeOptions, true);
  }
}

void DrawTargetWebgl::Stroke(const Path* aPath, const Pattern& aPattern,
                             const StrokeOptions& aStrokeOptions,
                             const DrawOptions& aOptions) {
  if (!aPath || aPath->GetBackendType() != BackendType::SKIA) {
    return;
  }
  const auto& skiaPath = static_cast<const PathSkia*>(aPath)->GetPath();
  if (!mWebglValid) {
    MarkSkiaChanged(aOptions);
    mSkia->Stroke(aPath, aPattern, aStrokeOptions, aOptions);
    return;
  }

  // Avoid using Skia's isLine here because some paths erroneously include a
  // closePath at the end, causing isLine to not detect the line. In that case
  // we just draw a line in reverse right over the original line.
  int numVerbs = skiaPath.countVerbs();
  bool allowStrokeAlpha = false;
  if (numVerbs >= 2 && numVerbs <= 3) {
    uint8_t verbs[3];
    skiaPath.getVerbs({verbs, numVerbs});
    if (verbs[0] == SkPath::kMove_Verb && verbs[1] == SkPath::kLine_Verb &&
        (numVerbs < 3 || verbs[2] == SkPath::kClose_Verb)) {
      bool closed = numVerbs >= 3;
      Point start = SkPointToPoint(skiaPath.getPoint(0));
      Point end = SkPointToPoint(skiaPath.getPoint(1));
      if (StrokeLineAccel(start, end, aPattern, aStrokeOptions, aOptions,
                          closed)) {
        if (closed) {
          StrokeLineAccel(end, start, aPattern, aStrokeOptions, aOptions, true);
        }
        return;
      }
      // If accelerated line drawing failed, just treat it as a path.
      allowStrokeAlpha = true;
    }
  }

  DrawPath(aPath, aPattern, aOptions, &aStrokeOptions, allowStrokeAlpha);
}

void DrawTargetWebgl::StrokeCircle(const Point& aOrigin, float aRadius,
                                   const Pattern& aPattern,
                                   const StrokeOptions& aStrokeOptions,
                                   const DrawOptions& aOptions) {
  DrawCircle(aOrigin, aRadius, aPattern, aOptions, &aStrokeOptions);
}

bool DrawTargetWebgl::ShouldUseSubpixelAA(ScaledFont* aFont,
                                          const DrawOptions& aOptions) {
  AntialiasMode aaMode = aFont->GetDefaultAAMode();
  if (aOptions.mAntialiasMode != AntialiasMode::DEFAULT) {
    aaMode = aOptions.mAntialiasMode;
  }
  return GetPermitSubpixelAA() &&
         (aaMode == AntialiasMode::DEFAULT ||
          aaMode == AntialiasMode::SUBPIXEL) &&
         aOptions.mCompositionOp == CompositionOp::OP_OVER;
}

void DrawTargetWebgl::StrokeGlyphs(ScaledFont* aFont,
                                   const GlyphBuffer& aBuffer,
                                   const Pattern& aPattern,
                                   const StrokeOptions& aStrokeOptions,
                                   const DrawOptions& aOptions) {
  if (!aFont || !aBuffer.mNumGlyphs) {
    return;
  }

  bool useSubpixelAA = ShouldUseSubpixelAA(aFont, aOptions);

  if (mWebglValid && SupportsDrawOptions(aOptions) &&
      aPattern.GetType() == PatternType::COLOR && PrepareContext() &&
      mSharedContext->DrawGlyphsAccel(aFont, aBuffer, aPattern, aOptions,
                                      &aStrokeOptions, useSubpixelAA)) {
    return;
  }

  if (useSubpixelAA) {
    // Subpixel AA does not support layering because the subpixel masks can't
    // blend with the over op.
    MarkSkiaChanged();
  } else {
    MarkSkiaChanged(aOptions);
  }
  mSkia->StrokeGlyphs(aFont, aBuffer, aPattern, aStrokeOptions, aOptions);
}

// Depending on whether we enable subpixel position for a given font, Skia may
// round transformed coordinates differently on each axis. By default, text is
// subpixel quantized horizontally and snapped to a whole integer vertical
// baseline. Axis-flip transforms instead snap to horizontal boundaries while
// subpixel quantizing along the vertical. For other types of transforms, Skia
// just applies subpixel quantization to both axes.
// We must duplicate the amount of quantization Skia applies carefully as a
// boundary value such as 0.49 may round to 0.5 with subpixel quantization,
// but if Skia actually snapped it to a whole integer instead, it would round
// down to 0. If a subsequent glyph with offset 0.51 came in, we might
// mistakenly round it down to 0.5, whereas Skia would round it up to 1. Thus
// we would alias 0.49 and 0.51 to the same cache entry, while Skia would
// actually snap the offset to 0 or 1, depending, resulting in mismatched
// hinting.
static inline IntPoint QuantizeScale(ScaledFont* aFont,
                                     const Matrix& aTransform) {
  if (!aFont->UseSubpixelPosition()) {
    return {1, 1};
  }
  if (aTransform._12 == 0) {
    // Glyphs are rendered subpixel horizontally, so snap vertically.
    return {4, 1};
  }
  if (aTransform._11 == 0) {
    // Glyphs are rendered subpixel vertically, so snap horizontally.
    return {1, 4};
  }
  // The transform isn't aligned, so don't snap.
  return {4, 4};
}

// Skia only supports subpixel positioning to the nearest 1/4 fraction. It
// would be wasteful to attempt to cache text runs with positioning that is
// anymore precise than this. To prevent this cache bloat, we quantize the
// transformed glyph positions to the nearest 1/4. The scaling factor for
// the quantization is baked into the transform, so that if subpixel rounding
// is used on a given axis, then the axis will be multiplied by 4 before
// rounding. Since the quantized position is not used for rasterization, the
// transform is safe to modify as such.
static inline IntPoint QuantizePosition(const Matrix& aTransform,
                                        const IntPoint& aOffset,
                                        const Point& aPosition) {
  return RoundedToInt(aTransform.TransformPoint(aPosition)) - aOffset;
}

// Get a quantized starting offset for the glyph buffer. We want this offset
// to encapsulate the transform and buffer offset while still preserving the
// relative subpixel positions of the glyphs this offset is subtracted from.
static inline IntPoint QuantizeOffset(const Matrix& aTransform,
                                      const IntPoint& aQuantizeScale,
                                      const GlyphBuffer& aBuffer) {
  IntPoint offset =
      RoundedToInt(aTransform.TransformPoint(aBuffer.mGlyphs[0].mPosition));
  offset.x.value &= ~(aQuantizeScale.x.value - 1);
  offset.y.value &= ~(aQuantizeScale.y.value - 1);
  return offset;
}

// Hashes a glyph buffer to a single hash value that can be used for quick
// comparisons. Each glyph position is transformed and quantized before
// hashing.
HashNumber GlyphCacheEntry::HashGlyphs(const GlyphBuffer& aBuffer,
                                       const Matrix& aTransform,
                                       const IntPoint& aQuantizeScale) {
  HashNumber hash = 0;
  IntPoint offset = QuantizeOffset(aTransform, aQuantizeScale, aBuffer);
  for (size_t i = 0; i < aBuffer.mNumGlyphs; i++) {
    const Glyph& glyph = aBuffer.mGlyphs[i];
    hash = AddToHash(hash, glyph.mIndex);
    IntPoint pos = QuantizePosition(aTransform, offset, glyph.mPosition);
    hash = AddToHash(hash, pos.x);
    hash = AddToHash(hash, pos.y);
  }
  return hash;
}

// Determines if an existing glyph cache entry matches an incoming text run.
inline bool GlyphCacheEntry::MatchesGlyphs(
    const GlyphBuffer& aBuffer, const DeviceColor& aColor,
    const Matrix& aTransform, const IntPoint& aQuantizeOffset,
    const IntPoint& aBoundsOffset, const IntRect& aClipRect, HashNumber aHash,
    const StrokeOptions* aStrokeOptions) {
  // First check if the hash matches to quickly reject the text run before any
  // more expensive checking. If it matches, then check if the color and
  // transform are the same.
  if (aHash != mHash || aBuffer.mNumGlyphs != mBuffer.mNumGlyphs ||
      aColor != mColor || !HasMatchingScale(aTransform, mTransform)) {
    return false;
  }
  // Finally check if all glyphs and their quantized positions match.
  for (size_t i = 0; i < aBuffer.mNumGlyphs; i++) {
    const Glyph& dst = mBuffer.mGlyphs[i];
    const Glyph& src = aBuffer.mGlyphs[i];
    if (dst.mIndex != src.mIndex ||
        dst.mPosition != Point(QuantizePosition(aTransform, aQuantizeOffset,
                                                src.mPosition))) {
      return false;
    }
  }
  // Check that stroke options actually match.
  if (aStrokeOptions) {
    // If stroking, verify that the entry is also stroked with the same options.
    if (!(mStrokeOptions && *aStrokeOptions == *mStrokeOptions)) {
      return false;
    }
  } else if (mStrokeOptions) {
    // If not stroking, check if the entry is stroked. If so, don't match.
    return false;
  }
  // Verify that the full bounds, once translated and clipped, are equal to the
  // clipped bounds.
  return (mFullBounds + aBoundsOffset)
      .Intersect(aClipRect)
      .IsEqualEdges(GetBounds() + aBoundsOffset);
}

GlyphCacheEntry::GlyphCacheEntry(const GlyphBuffer& aBuffer,
                                 const DeviceColor& aColor,
                                 const Matrix& aTransform,
                                 const IntPoint& aQuantizeScale,
                                 const IntRect& aBounds,
                                 const IntRect& aFullBounds, HashNumber aHash,
                                 StoredStrokeOptions* aStrokeOptions)
    : CacheEntryImpl<GlyphCacheEntry>(aTransform, aBounds, aHash),
      mColor(aColor),
      mFullBounds(aFullBounds),
      mStrokeOptions(aStrokeOptions) {
  // Store a copy of the glyph buffer with positions already quantized for fast
  // comparison later.
  Glyph* glyphs = new Glyph[aBuffer.mNumGlyphs];
  IntPoint offset = QuantizeOffset(aTransform, aQuantizeScale, aBuffer);
  // Make the bounds relative to the offset so we can add a new offset later.
  IntPoint boundsOffset(offset.x / aQuantizeScale.x,
                        offset.y / aQuantizeScale.y);
  mBounds -= boundsOffset;
  mFullBounds -= boundsOffset;
  for (size_t i = 0; i < aBuffer.mNumGlyphs; i++) {
    Glyph& dst = glyphs[i];
    const Glyph& src = aBuffer.mGlyphs[i];
    dst.mIndex = src.mIndex;
    dst.mPosition = Point(QuantizePosition(aTransform, offset, src.mPosition));
  }
  mBuffer.mGlyphs = glyphs;
  mBuffer.mNumGlyphs = aBuffer.mNumGlyphs;
}

GlyphCacheEntry::~GlyphCacheEntry() { delete[] mBuffer.mGlyphs; }

// Attempt to find a matching entry in the glyph cache. The caller should check
// whether the contained texture handle is valid to determine if it will need to
// render the text run or just reuse the cached texture.
already_AddRefed<GlyphCacheEntry> GlyphCache::FindEntry(
    const GlyphBuffer& aBuffer, const DeviceColor& aColor,
    const Matrix& aTransform, const IntPoint& aQuantizeScale,
    const IntRect& aClipRect, HashNumber aHash,
    const StrokeOptions* aStrokeOptions) {
  IntPoint offset = QuantizeOffset(aTransform, aQuantizeScale, aBuffer);
  IntPoint boundsOffset(offset.x / aQuantizeScale.x,
                        offset.y / aQuantizeScale.y);
  for (const RefPtr<GlyphCacheEntry>& entry : GetChain(aHash)) {
    if (entry->MatchesGlyphs(aBuffer, aColor, aTransform, offset, boundsOffset,
                             aClipRect, aHash, aStrokeOptions)) {
      return do_AddRef(entry);
    }
  }
  return nullptr;
}

// Insert a new entry in the glyph cache.
already_AddRefed<GlyphCacheEntry> GlyphCache::InsertEntry(
    const GlyphBuffer& aBuffer, const DeviceColor& aColor,
    const Matrix& aTransform, const IntPoint& aQuantizeScale,
    const IntRect& aBounds, const IntRect& aFullBounds, HashNumber aHash,
    const StrokeOptions* aStrokeOptions) {
  StoredStrokeOptions* strokeOptions = nullptr;
  if (aStrokeOptions) {
    strokeOptions = aStrokeOptions->Clone();
    if (!strokeOptions) {
      return nullptr;
    }
  }
  RefPtr<GlyphCacheEntry> entry =
      new GlyphCacheEntry(aBuffer, aColor, aTransform, aQuantizeScale, aBounds,
                          aFullBounds, aHash, strokeOptions);
  Insert(entry);
  return entry.forget();
}

GlyphCache::GlyphCache(ScaledFont* aFont) : mFont(aFont) {}

static void ReleaseGlyphCache(void* aPtr) {
  delete static_cast<GlyphCache*>(aPtr);
}

// Whether all glyphs in the buffer match the last whitespace glyph queried.
bool GlyphCache::IsWhitespace(const GlyphBuffer& aBuffer) const {
  if (!mLastWhitespace) {
    return false;
  }
  uint32_t whitespace = *mLastWhitespace;
  for (size_t i = 0; i < aBuffer.mNumGlyphs; ++i) {
    if (aBuffer.mGlyphs[i].mIndex != whitespace) {
      return false;
    }
  }
  return true;
}

// Remember the last whitespace glyph seen.
void GlyphCache::SetLastWhitespace(const GlyphBuffer& aBuffer) {
  mLastWhitespace = Some(aBuffer.mGlyphs[0].mIndex);
}

void DrawTargetWebgl::SetPermitSubpixelAA(bool aPermitSubpixelAA) {
  DrawTarget::SetPermitSubpixelAA(aPermitSubpixelAA);
  mSkia->SetPermitSubpixelAA(aPermitSubpixelAA);
}

// Check for any color glyphs contained within a rasterized BGRA8 text result.
static bool CheckForColorGlyphs(const RefPtr<SourceSurface>& aSurface) {
  if (aSurface->GetFormat() != SurfaceFormat::B8G8R8A8) {
    return false;
  }
  RefPtr<DataSourceSurface> dataSurf = aSurface->GetDataSurface();
  if (!dataSurf) {
    return true;
  }
  DataSourceSurface::ScopedMap map(dataSurf, DataSourceSurface::READ);
  if (!map.IsMapped()) {
    return true;
  }
  IntSize size = dataSurf->GetSize();
  const uint8_t* data = map.GetData();
  int32_t stride = map.GetStride();
  for (int y = 0; y < size.height; y++) {
    const uint32_t* x = (const uint32_t*)data;
    const uint32_t* end = x + size.width;
    for (; x < end; x++) {
      // Verify if all components are the same as for premultiplied grayscale.
      uint32_t color = *x;
      uint32_t gray = color & 0xFF;
      gray |= gray << 8;
      gray |= gray << 16;
      if (color != gray) return true;
    }
    data += stride;
  }
  return false;
}

// Quantize the preblend color used to key the cache, as only the high bits are
// used to determine the amount of preblending. This avoids excessive cache use.
// This roughly matches the quantization used in WebRender and Skia.
static DeviceColor QuantizePreblendColor(const DeviceColor& aColor,
                                         bool aUseSubpixelAA) {
  int32_t r = int32_t(aColor.r * 255.0f + 0.5f);
  int32_t g = int32_t(aColor.g * 255.0f + 0.5f);
  int32_t b = int32_t(aColor.b * 255.0f + 0.5f);
  // Skia only uses the high 3 bits of each color component to cache preblend
  // ramp tables.
  constexpr int32_t lumBits = 3;
  constexpr int32_t floorMask = ((1 << lumBits) - 1) << (8 - lumBits);
  if (!aUseSubpixelAA) {
    // If not using subpixel AA, then quantize only the luminance, stored in the
    // G channel.
    g = (r * 54 + g * 183 + b * 19) >> 8;
    g &= floorMask;
    r = g;
    b = g;
  } else {
    r &= floorMask;
    g &= floorMask;
    b &= floorMask;
  }
  return DeviceColor{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
}

// Draws glyphs to the WebGL target by trying to generate a cached texture for
// the text run that can be subsequently reused to quickly render the text run
// without using any software surfaces.
bool SharedContextWebgl::DrawGlyphsAccel(ScaledFont* aFont,
                                         const GlyphBuffer& aBuffer,
                                         const Pattern& aPattern,
                                         const DrawOptions& aOptions,
                                         const StrokeOptions* aStrokeOptions,
                                         bool aUseSubpixelAA) {
  // Look for an existing glyph cache on the font. If not there, create it.
  GlyphCache* cache =
      static_cast<GlyphCache*>(aFont->GetUserData(&mGlyphCacheKey));
  if (!cache) {
    cache = new GlyphCache(aFont);
    aFont->AddUserData(&mGlyphCacheKey, cache, ReleaseGlyphCache);
    mGlyphCaches.insertFront(cache);
  }

  // Check if the buffer contains non-renderable whitespace characters before
  // any other expensive checks.
  if (cache->IsWhitespace(aBuffer)) {
    return true;
  }

  // Whether the font may use bitmaps. If so, we need to render the glyphs with
  // color as grayscale bitmaps will use the color while color emoji will not,
  // with no easy way to know ahead of time. We currently have to check the
  // rasterized result to see if there are any color glyphs. To render subpixel
  // masks, we need to know that the rasterized result actually represents a
  // subpixel mask rather than try to interpret it as a normal RGBA result such
  // as for color emoji.
  bool useBitmaps = !aStrokeOptions && aFont->MayUseBitmaps() &&
                    aOptions.mCompositionOp != CompositionOp::OP_CLEAR;
  // Hash the incoming text run and looking for a matching entry.
  DeviceColor color = aOptions.mCompositionOp == CompositionOp::OP_CLEAR
                          ? DeviceColor(1, 1, 1, 1)
                          : static_cast<const ColorPattern&>(aPattern).mColor;
#if defined(XP_MACOSX)
  // macOS uses gamma-aware blending with font smooth from subpixel AA.
  // If font smoothing is requested, even if there is no subpixel AA, gamma-
  // aware blending might be used and differing amounts of dilation might be
  // applied.
  bool usePreblend = aUseSubpixelAA ||
                     (aFont->GetType() == FontType::MAC &&
                      static_cast<ScaledFontMac*>(aFont)->UseFontSmoothing());
#elif defined(XP_WIN)
  // Windows uses gamma-aware blending via ClearType with grayscale and subpixel
  // AA.
  bool usePreblend =
      aUseSubpixelAA || aOptions.mAntialiasMode != AntialiasMode::NONE;
#elif defined(MOZ_WIDGET_GTK) || defined(MOZ_WIDGET_ANDROID)
  // FreeType preblend is conditional on gamma pref being enabled.
  bool usePreblend =
      (StaticPrefs::gfx_font_rendering_freetype_gamma() >= 0 ||
       StaticPrefs::gfx_font_rendering_freetype_enhanced_contrast() > 0) &&
      (aUseSubpixelAA || aOptions.mAntialiasMode != AntialiasMode::NONE);
#else
  // Other platforms (uikit) do not use preblend.
  bool usePreblend = false;
#endif

  // If the font has bitmaps, use the color directly. Otherwise, the texture
  // holds a grayscale mask, so encode the key's subpixel state in the color.
  const Matrix& currentTransform = mCurrentTarget->GetTransform();
  IntPoint quantizeScale = QuantizeScale(aFont, currentTransform);
  Matrix quantizeTransform = currentTransform;
  quantizeTransform.PostScale(quantizeScale.x, quantizeScale.y);
  HashNumber hash =
      GlyphCacheEntry::HashGlyphs(aBuffer, quantizeTransform, quantizeScale);
  DeviceColor colorOrMask =
      useBitmaps ? color
                 : (usePreblend ? QuantizePreblendColor(color, aUseSubpixelAA)
                                : DeviceColor::Mask(aUseSubpixelAA ? 1 : 0, 1));
  IntRect clipRect(IntPoint(), mViewportSize);
  RefPtr<GlyphCacheEntry> entry =
      cache->FindEntry(aBuffer, colorOrMask, quantizeTransform, quantizeScale,
                       clipRect, hash, aStrokeOptions);
  if (!entry) {
    // For small text runs, bounds computations can be expensive relative to the
    // cost of looking up a cache result. Avoid doing local bounds computations
    // until actually inserting the entry into the cache.
    Maybe<Rect> bounds = mCurrentTarget->mSkia->GetGlyphLocalBounds(
        aFont, aBuffer, aPattern, aStrokeOptions, aOptions);
    if (!bounds) {
      // Assume the buffer is full of whitespace characters that should be
      // remembered for subsequent lookups.
      cache->SetLastWhitespace(aBuffer);
      return true;
    }
    // Transform the local bounds into device space so that we know how big
    // the cached texture will be.
    Rect xformBounds = currentTransform.TransformBounds(*bounds);
    // Check if the transform flattens out the bounds before rounding.
    if (xformBounds.IsEmpty()) {
      return true;
    }
    IntRect fullBounds = RoundedOut(xformBounds);
    IntRect clipBounds = fullBounds.Intersect(clipRect);
    // Check if the bounds are completely clipped out.
    if (clipBounds.IsEmpty()) {
      return true;
    }
    entry = cache->InsertEntry(aBuffer, colorOrMask, quantizeTransform,
                               quantizeScale, clipBounds, fullBounds, hash,
                               aStrokeOptions);
    if (!entry) {
      return false;
    }
  }

  // The bounds of the entry may have a different transform offset from the
  // bounds of the currently drawn text run. The entry bounds are relative to
  // the entry's quantized offset already, so just move the bounds to the new
  // offset.
  IntRect intBounds = entry->GetBounds();
  IntPoint newOffset =
      QuantizeOffset(quantizeTransform, quantizeScale, aBuffer);
  intBounds +=
      IntPoint(newOffset.x / quantizeScale.x, newOffset.y / quantizeScale.y);
  // Ensure there is a clear border around the text. This must be applied only
  // after clipping so that we always have some border texels for filtering.
  intBounds.Inflate(2);

  RefPtr<TextureHandle> handle = entry->GetHandle();
  if (handle && handle->IsValid()) {
    // If there is an entry with a valid cached texture handle, then try
    // to draw with that. If that for some reason failed, then fall back
    // to using the Skia target as that means we were preventing from
    // drawing to the WebGL context based on something other than the
    // texture.
    SurfacePattern pattern(nullptr, ExtendMode::CLAMP,
                           Matrix::Translation(intBounds.TopLeft()));
    if (DrawRectAccel(Rect(intBounds), pattern, aOptions,
                      useBitmaps ? Nothing() : Some(color), &handle, false,
                      true, true)) {
      return true;
    }
  } else {
    handle = nullptr;

    // If we get here, either there wasn't a cached texture handle or it
    // wasn't valid. Render the text run into a temporary target.
    RefPtr<DrawTargetSkia> textDT = new DrawTargetSkia;
    if (textDT->Init(intBounds.Size(),
                     useBitmaps || usePreblend || aUseSubpixelAA
                         ? SurfaceFormat::B8G8R8A8
                         : SurfaceFormat::A8)) {
      textDT->SetTransform(currentTransform *
                           Matrix::Translation(-intBounds.TopLeft()));
      textDT->SetPermitSubpixelAA(aUseSubpixelAA);
      DrawOptions drawOptions(1.0f, CompositionOp::OP_OVER,
                              aOptions.mAntialiasMode);
      // If bitmaps might be used, then we have to supply the color, as color
      // emoji may ignore it while grayscale bitmaps may use it, with no way to
      // know ahead of time. If we are using preblending in some form, then the
      // output also will depend on the supplied color. Otherwise, assume the
      // output will be a mask and just render it white to determine intensity.
      if (!useBitmaps && usePreblend) {
        textDT->DrawGlyphMask(aFont, aBuffer, color, aStrokeOptions,
                              drawOptions);
      } else {
        ColorPattern colorPattern(useBitmaps ? color : DeviceColor(1, 1, 1, 1));
        if (aStrokeOptions) {
          textDT->StrokeGlyphs(aFont, aBuffer, colorPattern, *aStrokeOptions,
                               drawOptions);
        } else {
          textDT->FillGlyphs(aFont, aBuffer, colorPattern, drawOptions);
        }
      }
      RefPtr<SourceSurface> textSurface = textDT->Snapshot();
      if (textSurface) {
        // If we don't expect the text surface to contain color glyphs
        // such as from subpixel AA, then do one final check to see if
        // any ended up in the result. If not, extract the alpha values
        // from the surface so we can render it as a mask.
        if (textSurface->GetFormat() != SurfaceFormat::A8 &&
            !CheckForColorGlyphs(textSurface)) {
          textSurface = ExtractAlpha(textSurface, !useBitmaps);
          if (!textSurface) {
            // Failed extracting alpha for the text surface...
            return false;
          }
        }
        // Attempt to upload the rendered text surface into a texture
        // handle and draw it.
        SurfacePattern pattern(textSurface, ExtendMode::CLAMP,
                               Matrix::Translation(intBounds.TopLeft()));
        if (DrawRectAccel(Rect(intBounds), pattern, aOptions,
                          useBitmaps ? Nothing() : Some(color), &handle, false,
                          true) &&
            handle) {
          // If drawing succeeded, then the text surface was uploaded to
          // a texture handle. Assign it to the glyph cache entry.
          entry->Link(handle);
        } else {
          // If drawing failed, remove the entry from the cache.
          entry->Unlink();
        }
        return true;
      }
    }
  }

  // Avoid filling cache with empty entries.
  entry->Unlink();
  return false;
}

void DrawTargetWebgl::FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                                 const Pattern& aPattern,
                                 const DrawOptions& aOptions) {
  if (!aFont || !aBuffer.mNumGlyphs) {
    return;
  }

  bool useSubpixelAA = ShouldUseSubpixelAA(aFont, aOptions);

  if (mWebglValid && SupportsDrawOptions(aOptions) &&
      aPattern.GetType() == PatternType::COLOR && PrepareContext() &&
      mSharedContext->DrawGlyphsAccel(aFont, aBuffer, aPattern, aOptions,
                                      nullptr, useSubpixelAA)) {
    return;
  }

  // If not able to cache the text run to a texture, then just fall back to
  // drawing with the Skia target.
  if (useSubpixelAA) {
    // Subpixel AA does not support layering because the subpixel masks can't
    // blend with the over op.
    MarkSkiaChanged();
  } else {
    MarkSkiaChanged(aOptions);
  }
  mSkia->FillGlyphs(aFont, aBuffer, aPattern, aOptions);
}

// Attempts to read the contents of the WebGL context into the Skia target.
bool DrawTargetWebgl::ReadIntoSkia() {
  if (mSkiaValid) {
    return false;
  }
  bool didReadback = false;
  if (mWebglValid) {
    uint8_t* data = nullptr;
    IntSize size;
    int32_t stride;
    SurfaceFormat format;
    if (mIsClear) {
      // If the WebGL target is still clear, then just clear the Skia target.
      mSkia->DetachAllSnapshots();
      mSkiaNoClip->FillRect(Rect(mSkiaNoClip->GetRect()), GetClearPattern(),
                            DrawOptions(1.0f, CompositionOp::OP_SOURCE));
    } else {
      // If there's no existing snapshot and we can successfully map the Skia
      // target for reading, then try to read into that.
      if (!mSnapshot && mSkia->LockBits(&data, &size, &stride, &format)) {
        (void)ReadInto(data, stride);
        mSkia->ReleaseBits(data);
      } else if (RefPtr<SourceSurface> snapshot = Snapshot()) {
        // Otherwise, fall back to getting a snapshot from WebGL if available
        // and then copying that to Skia.
        mSkia->CopySurface(snapshot, GetRect(), IntPoint(0, 0));
      }
      didReadback = true;
    }
  }
  mSkiaValid = true;
  // The Skia data is flat after reading, so disable any layering.
  mSkiaLayer = false;
  return didReadback;
}

// Reads data from the WebGL context and blends it with the current Skia layer.
void DrawTargetWebgl::FlattenSkia() {
  if (!mSkiaValid || !mSkiaLayer) {
    return;
  }
  mSkiaLayer = false;
  if (mSkiaLayerClear) {
    // If the WebGL target is clear, then there is nothing to blend.
    return;
  }
  if (RefPtr<DataSourceSurface> base = ReadSnapshot()) {
    mSkia->DetachAllSnapshots();
    mSkiaNoClip->DrawSurface(base, Rect(GetRect()), Rect(GetRect()),
                             DrawSurfaceOptions(SamplingFilter::POINT),
                             DrawOptions(1.f, CompositionOp::OP_DEST_OVER));
  }
}

// Attempts to draw the contents of the Skia target into the WebGL context.
bool DrawTargetWebgl::FlushFromSkia() {
  // If the WebGL context has been lost, then mark it as invalid and fail.
  if (mSharedContext->IsContextLost()) {
    mWebglValid = false;
    return false;
  }
  // The WebGL target is already valid, so there is nothing to do.
  if (mWebglValid) {
    return true;
  }
  // Ensure that DrawRect doesn't recursively call into FlushFromSkia. If
  // the Skia target isn't valid, then it doesn't matter what is in the the
  // WebGL target either, so only try to blend if there is a valid Skia target.
  mWebglValid = true;
  if (mSkiaValid) {
    AutoRestoreContext restore(this);

    // If the Skia target is clear, then there is no need to use a snapshot.
    // Directly clear the WebGL target instead.
    if (mIsClear) {
      if (!DrawRect(Rect(GetRect()), GetClearPattern(),
                    DrawOptions(1.0f, CompositionOp::OP_SOURCE), Nothing(),
                    nullptr, false, false, true)) {
        mWebglValid = false;
        return false;
      }
      return true;
    }

    RefPtr<SourceSurface> skiaSnapshot = mSkia->Snapshot();
    if (!skiaSnapshot) {
      // There's a valid Skia target to draw to, but for some reason there is
      // no available snapshot, so just keep using the Skia target.
      mWebglValid = false;
      return false;
    }

    // If there is no layer, then just upload it directly.
    if (!mSkiaLayer) {
      if (PrepareContext(false) && MarkChanged()) {
        if (RefPtr<DataSourceSurface> data = skiaSnapshot->GetDataSurface()) {
          mSharedContext->UploadSurface(data, mFormat, GetRect(), IntPoint(),
                                        false, false, mTex);
          return true;
        }
      }
      // Failed to upload the Skia snapshot.
      mWebglValid = false;
      return false;
    }

    SurfacePattern pattern(skiaSnapshot, ExtendMode::CLAMP);
    // If there is a layer, blend the snapshot with the WebGL context.
    if (!DrawRect(Rect(GetRect()), pattern,
                  DrawOptions(1.0f, CompositionOp::OP_OVER), Nothing(),
                  &mSnapshotTexture, false, false, true, true)) {
      // If accelerated drawing failed for some reason, then leave the Skia
      // target unchanged.
      mWebglValid = false;
      return false;
    }
  }
  return true;
}

void DrawTargetWebgl::UsageProfile::BeginFrame() {
  // Reset the usage profile counters for the new frame.
  mFallbacks = 0;
  mLayers = 0;
  mCacheMisses = 0;
  mCacheHits = 0;
  mUncachedDraws = 0;
  mReadbacks = 0;
}

void DrawTargetWebgl::UsageProfile::EndFrame() {
  bool failed = false;
  // If we hit a complete fallback to software rendering, or if cache misses
  // were more than cutoff ratio of all requests, then we consider the frame as
  // having failed performance profiling.
  float cacheRatio =
      StaticPrefs::gfx_canvas_accelerated_profile_cache_miss_ratio();
  if (mFallbacks > 0 ||
      float(mCacheMisses + mReadbacks + mLayers) >
          cacheRatio * float(mCacheMisses + mCacheHits + mUncachedDraws +
                             mReadbacks + mLayers)) {
    failed = true;
  }
  if (failed) {
    ++mFailedFrames;
  }
  ++mFrameCount;
}

bool DrawTargetWebgl::UsageProfile::RequiresRefresh() const {
  // If we've rendered at least the required number of frames for a profile and
  // more than the cutoff ratio of frames did not meet performance criteria,
  // then we should stop using an accelerated canvas.
  uint32_t profileFrames = StaticPrefs::gfx_canvas_accelerated_profile_frames();
  if (!profileFrames || mFrameCount < profileFrames) {
    return false;
  }
  float failRatio =
      StaticPrefs::gfx_canvas_accelerated_profile_fallback_ratio();
  return mFailedFrames > failRatio * mFrameCount;
}

void SharedContextWebgl::CachePrefs() {
  uint32_t capacity = StaticPrefs::gfx_canvas_accelerated_gpu_path_size() << 20;
  if (capacity != mPathVertexCapacity) {
    mPathVertexCapacity = capacity;
    if (mPathCache) {
      mPathCache->ClearVertexRanges();
    }
    if (mPathVertexBuffer) {
      ResetPathVertexBuffer();
    }
  }

  mPathMaxComplexity =
      StaticPrefs::gfx_canvas_accelerated_gpu_path_complexity();

  mPathAAStroke = StaticPrefs::gfx_canvas_accelerated_aa_stroke_enabled();
  mPathWGRStroke = StaticPrefs::gfx_canvas_accelerated_stroke_to_fill_path();
}

// For use within CanvasRenderingContext2D, called on BorrowDrawTarget.
void DrawTargetWebgl::BeginFrame(bool aInvalidContents) {
  // Force resetting of target bindings in case state is somehow inconsistent.
  mSharedContext->ClearTarget();
  // If still rendering into the Skia target, switch back to the WebGL
  // context.
  if (!mWebglValid) {
    if (aInvalidContents) {
      // If nothing needs to persist, just mark the WebGL context valid.
      mWebglValid = true;
      // Even if the Skia framebuffer is marked clear, since the WebGL
      // context is not valid, its contents may be out-of-date and not
      // necessarily clear.
      mIsClear = false;
    } else {
      FlushFromSkia();
    }
  }
  // Check if we need to clear out any cached because of memory pressure.
  mSharedContext->ClearCachesIfNecessary();
  // Cache any prefs for the frame.
  mSharedContext->CachePrefs();
  mProfile.BeginFrame();
}

// For use within CanvasRenderingContext2D, called on ReturnDrawTarget.
void DrawTargetWebgl::EndFrame() {
  if (StaticPrefs::gfx_canvas_accelerated_debug()) {
    // Draw a green rectangle in the upper right corner to indicate
    // acceleration.
    IntRect corner = IntRect(mSize.width - 16, 0, 16, 16).Intersect(GetRect());
    DrawRect(Rect(corner), ColorPattern(DeviceColor(0.0f, 1.0f, 0.0f, 1.0f)),
             DrawOptions(), Nothing(), nullptr, false, false);
  }
  mProfile.EndFrame();
  // Ensure we're not somehow using more than the allowed texture memory.
  mSharedContext->PruneTextureMemory();
  // Signal that we're done rendering the frame in case no present occurs.
  mSharedContext->mWebgl->EndOfFrame();
  // Check if we need to clear out any cached because of memory pressure.
  mSharedContext->ClearCachesIfNecessary();
}

bool DrawTargetWebgl::CopyToSwapChain(
    layers::TextureType aTextureType, layers::RemoteTextureId aId,
    layers::RemoteTextureOwnerId aOwnerId,
    layers::RemoteTextureOwnerClient* aOwnerClient) {
  if (!mWebglValid && !FlushFromSkia()) {
    return false;
  }

  // Copy and swizzle the WebGL framebuffer to the swap chain front buffer.
  webgl::SwapChainOptions options;
  options.bgra = true;
  // Allow async present to be toggled on for accelerated Canvas2D
  // independent of WebGL via pref.
  options.forceAsyncPresent =
      StaticPrefs::gfx_canvas_accelerated_async_present();
  options.remoteTextureId = aId;
  options.remoteTextureOwnerId = aOwnerId;
  bool success = mSharedContext->mWebgl->CopyToSwapChain(
      mFramebuffer, aTextureType, options, aOwnerClient);
  // Force framebuffer binding reset in case WebGLContext::CopyToSwapChain
  // unpredictably modifies it.
  mSharedContext->RestoreCurrentTarget();
  return success;
}

std::shared_ptr<gl::SharedSurface> SharedContextWebgl::ExportSharedSurface(
    layers::TextureType aTextureType, SourceSurface* aSurface) {
  RefPtr<WebGLTexture> tex = GetCompatibleSnapshot(aSurface, nullptr, false);
  if (!tex) {
    return nullptr;
  }
  if (!mExportFramebuffer) {
    mExportFramebuffer = mWebgl->CreateFramebuffer();
  }
  mWebgl->BindFramebuffer(LOCAL_GL_FRAMEBUFFER, mExportFramebuffer);
  webgl::FbAttachInfo attachInfo;
  attachInfo.tex = tex;
  mWebgl->FramebufferAttach(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                            LOCAL_GL_TEXTURE_2D, attachInfo);
  // Copy and swizzle the WebGL framebuffer to the swap chain front buffer.
  webgl::SwapChainOptions options;
  options.bgra = true;
  std::shared_ptr<gl::SharedSurface> sharedSurface;
  if (mWebgl->CopyToSwapChain(mExportFramebuffer, aTextureType, options)) {
    if (gl::SwapChain* swapChain =
            mWebgl->GetSwapChain(mExportFramebuffer, false)) {
      sharedSurface = swapChain->FrontBuffer();
    }
  }
  RestoreCurrentTarget();
  return sharedSurface;
}

already_AddRefed<SourceSurface> SharedContextWebgl::ImportSurfaceDescriptor(
    const layers::SurfaceDescriptor& aDesc, const IntSize& aSize,
    SurfaceFormat aFormat) {
  if (IsContextLost()) {
    return nullptr;
  }

  RefPtr<TextureHandle> handle =
      AllocateTextureHandle(aFormat, aSize, true, true);
  if (!handle) {
    return nullptr;
  }
  BackingTexture* backing = handle->GetBackingTexture();
  RefPtr<WebGLTexture> tex = backing->GetWebGLTexture();
  if (mLastTexture != tex) {
    mWebgl->BindTexture(LOCAL_GL_TEXTURE_2D, tex);
    mLastTexture = tex;
  }
  if (!backing->IsInitialized()) {
    backing->MarkInitialized();
    InitTexParameters(tex);
    if (handle->GetSize() != backing->GetSize()) {
      UploadSurface(nullptr, backing->GetFormat(),
                    IntRect(IntPoint(), backing->GetSize()), IntPoint(), true,
                    true);
    }
  }

  IntRect bounds = handle->GetBounds();
  webgl::TexUnpackBlobDesc texDesc = {
      LOCAL_GL_TEXTURE_2D, {uint32_t(aSize.width), uint32_t(aSize.height), 1}};
  texDesc.sd = Some(aDesc);
  texDesc.structuredSrcSize = uvec2::FromSize(aSize);
  GLenum intFormat =
      aFormat == SurfaceFormat::A8 ? LOCAL_GL_R8 : LOCAL_GL_RGBA8;
  GLenum extFormat =
      aFormat == SurfaceFormat::A8 ? LOCAL_GL_RED : LOCAL_GL_RGBA;
  webgl::PackingInfo texPI = {extFormat, LOCAL_GL_UNSIGNED_BYTE};
  mWebgl->TexImage(0, handle->GetSize() == backing->GetSize() ? intFormat : 0,
                   {uint32_t(bounds.x), uint32_t(bounds.y), 0}, texPI, texDesc);

  RefPtr<SourceSurfaceWebgl> surface = new SourceSurfaceWebgl(this);
  surface->SetHandle(handle);
  return surface.forget();
}

already_AddRefed<SourceSurface> DrawTargetWebgl::ImportSurfaceDescriptor(
    const layers::SurfaceDescriptor& aDesc, const IntSize& aSize,
    SurfaceFormat aFormat) {
  return mSharedContext->ImportSurfaceDescriptor(aDesc, aSize, aFormat);
}

already_AddRefed<DrawTarget> DrawTargetWebgl::CreateSimilarDrawTarget(
    const IntSize& aSize, SurfaceFormat aFormat) const {
  return mSkia->CreateSimilarDrawTarget(aSize, aFormat);
}

bool DrawTargetWebgl::CanCreateSimilarDrawTarget(const IntSize& aSize,
                                                 SurfaceFormat aFormat) const {
  return mSkia->CanCreateSimilarDrawTarget(aSize, aFormat);
}

RefPtr<DrawTarget> DrawTargetWebgl::CreateClippedDrawTarget(
    const Rect& aBounds, SurfaceFormat aFormat) {
  return mSkia->CreateClippedDrawTarget(aBounds, aFormat);
}

already_AddRefed<SourceSurface> DrawTargetWebgl::CreateSourceSurfaceFromData(
    unsigned char* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat) const {
  return mSkia->CreateSourceSurfaceFromData(aData, aSize, aStride, aFormat);
}

already_AddRefed<SourceSurface>
DrawTargetWebgl::CreateSourceSurfaceFromNativeSurface(
    const NativeSurface& aSurface) const {
  return mSkia->CreateSourceSurfaceFromNativeSurface(aSurface);
}

already_AddRefed<SourceSurface> DrawTargetWebgl::OptimizeSourceSurface(
    SourceSurface* aSurface) const {
  if (aSurface->GetUnderlyingType() == SurfaceType::WEBGL) {
    return do_AddRef(aSurface);
  }
  return mSkia->OptimizeSourceSurface(aSurface);
}

already_AddRefed<SourceSurface>
DrawTargetWebgl::OptimizeSourceSurfaceForUnknownAlpha(
    SourceSurface* aSurface) const {
  return mSkia->OptimizeSourceSurfaceForUnknownAlpha(aSurface);
}

already_AddRefed<GradientStops> DrawTargetWebgl::CreateGradientStops(
    GradientStop* aStops, uint32_t aNumStops, ExtendMode aExtendMode) const {
  return mSkia->CreateGradientStops(aStops, aNumStops, aExtendMode);
}

already_AddRefed<FilterNode> DrawTargetWebgl::CreateFilter(FilterType aType) {
  return FilterNodeWebgl::Create(aType);
}

already_AddRefed<FilterNode> DrawTargetWebgl::DeferFilterInput(
    const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
    const IntPoint& aDestOffset, const DrawOptions& aOptions,
    const StrokeOptions* aStrokeOptions) {
  RefPtr<FilterNode> filter = new FilterNodeDeferInputWebgl(
      do_AddRef((Path*)aPath), aPattern, aSourceRect,
      GetTransform().PostTranslate(aDestOffset), aOptions, aStrokeOptions);
  return filter.forget();
}

void DrawTargetWebgl::DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                                 const Point& aDestPoint,
                                 const DrawOptions& aOptions) {
  if (!aNode || aNode->GetBackendType() != FILTER_BACKEND_WEBGL) {
    return;
  }
  auto* webglFilter = static_cast<FilterNodeWebgl*>(aNode);
  webglFilter->Draw(this, aSourceRect, aDestPoint, aOptions);
}

void DrawTargetWebgl::DrawFilterFallback(FilterNode* aNode,
                                         const Rect& aSourceRect,
                                         const Point& aDestPoint,
                                         const DrawOptions& aOptions) {
  MarkSkiaChanged(aOptions);
  mSkia->DrawFilter(aNode, aSourceRect, aDestPoint, aOptions);
}

bool DrawTargetWebgl::Draw3DTransformedSurface(SourceSurface* aSurface,
                                               const Matrix4x4& aMatrix) {
  MarkSkiaChanged();
  return mSkia->Draw3DTransformedSurface(aSurface, aMatrix);
}

void DrawTargetWebgl::PushLayer(bool aOpaque, Float aOpacity,
                                SourceSurface* aMask,
                                const Matrix& aMaskTransform,
                                const IntRect& aBounds, bool aCopyBackground) {
  PushLayerWithBlend(aOpaque, aOpacity, aMask, aMaskTransform, aBounds,
                     aCopyBackground, CompositionOp::OP_OVER);
}

void DrawTargetWebgl::PushLayerWithBlend(bool aOpaque, Float aOpacity,
                                         SourceSurface* aMask,
                                         const Matrix& aMaskTransform,
                                         const IntRect& aBounds,
                                         bool aCopyBackground,
                                         CompositionOp aCompositionOp) {
  MarkSkiaChanged(DrawOptions(aOpacity, aCompositionOp));
  mSkia->PushLayerWithBlend(aOpaque, aOpacity, aMask, aMaskTransform, aBounds,
                            aCopyBackground, aCompositionOp);
  ++mLayerDepth;
  SetPermitSubpixelAA(mSkia->GetPermitSubpixelAA());
}

void DrawTargetWebgl::PopLayer() {
  MOZ_ASSERT(mSkiaValid);
  MOZ_ASSERT(mLayerDepth > 0);
  --mLayerDepth;
  mSkia->PopLayer();
  SetPermitSubpixelAA(mSkia->GetPermitSubpixelAA());
}

}  // namespace mozilla::gfx
