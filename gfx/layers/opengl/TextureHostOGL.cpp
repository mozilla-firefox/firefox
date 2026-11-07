/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureHostOGL.h"

#include "GLContextEGL.h"  // for GLContext, etc
#include "GLLibraryEGL.h"  // for GLLibraryEGL
#include "GLReadTexImageHelper.h"
#include "GLUploadHelpers.h"
#include "GeckoProfiler.h"
#include "GfxTexturesReporter.h"  // for GfxTexturesReporter
#include "gfx2DGlue.h"            // for ContentForFormat, etc
#include "mozilla/ScopeExit.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"        // for DataSourceSurface
#include "mozilla/gfx/BaseSize.h"  // for BaseSize
#include "mozilla/gfx/Logging.h"   // for gfxCriticalError
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/webrender/RenderEGLImageTextureHost.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsRegion.h"  // for nsIntRegion

#ifdef XP_MACOSX
#  include "mozilla/layers/MacIOSurfaceTextureHostOGL.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#  include "mozilla/layers/AndroidImageConsumer.h"
#  include "mozilla/webrender/RenderAndroidHardwareBufferTextureHost.h"
#  include "mozilla/webrender/RenderAndroidImageReaderImageTextureHost.h"
#  include "mozilla/webrender/RenderAndroidSurfaceTextureHost.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "mozilla/layers/DMABUFTextureHostOGL.h"
#endif

using namespace mozilla::gl;
using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

class Compositor;

void ApplySamplingFilterToBoundTexture(gl::GLContext* aGL,
                                       gfx::SamplingFilter aSamplingFilter,
                                       GLuint aTarget) {
  GLenum filter =
      (aSamplingFilter == gfx::SamplingFilter::POINT ? LOCAL_GL_NEAREST
                                                     : LOCAL_GL_LINEAR);

  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MIN_FILTER, filter);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MAG_FILTER, filter);
}

already_AddRefed<TextureHost> CreateTextureHostOGL(
    const SurfaceDescriptor& aDesc, ISurfaceAllocator* aDeallocator,
    LayersBackend aBackend, TextureFlags aFlags) {
  RefPtr<TextureHost> result;
  switch (aDesc.type()) {
#ifdef MOZ_WIDGET_ANDROID
    case SurfaceDescriptor::TSurfaceTextureDescriptor: {
      if (gfx::gfxVars::ForceAImageReaderAndroid()) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        gfxCriticalError() << "ForceAImageReader is enabled";
        return nullptr;
      }
      const SurfaceTextureDescriptor& desc =
          aDesc.get_SurfaceTextureDescriptor();
      java::GeckoSurfaceTexture::LocalRef surfaceTexture =
          java::GeckoSurfaceTexture::Lookup(desc.handle());

      result = new SurfaceTextureHost(
          aFlags, surfaceTexture, desc.size(), desc.format(), desc.continuous(),
          desc.forceBT709ColorSpace(), desc.transformOverride());
      break;
    }
    case SurfaceDescriptor::TSurfaceDescriptorAndroidHardwareBuffer: {
      if (aDeallocator) {
        gfxCriticalError() << "SurfaceDescriptorAndroidHardwareBuffer is used "
                              "only for remote texture";
        return nullptr;
      }
      const SurfaceDescriptorAndroidHardwareBuffer& desc =
          aDesc.get_SurfaceDescriptorAndroidHardwareBuffer();
      result = AndroidHardwareBufferTextureHost::Create(aFlags, desc);
      break;
    }
    case SurfaceDescriptor::TAndroidImageReaderImageDescriptor: {
      const AndroidImageReaderImageDescriptor& desc =
          aDesc.get_AndroidImageReaderImageDescriptor();
      result = new AndroidImageReaderImageTextureHost(aFlags, desc);
      break;
    }
#endif

    case SurfaceDescriptor::TEGLImageDescriptor: {
      if (aDeallocator && !aDeallocator->IsSameProcess()) {
        gfxCriticalError()
            << "EGLImageDescriptor must only be used in same process";
        return nullptr;
      }
      const EGLImageDescriptor& desc = aDesc.get_EGLImageDescriptor();
      result = new EGLImageTextureHost(aFlags, (EGLImage)desc.image(),
                                       (EGLSync)desc.fence(), desc.size(),
                                       desc.hasAlpha());
      break;
    }

#ifdef MOZ_WIDGET_GTK
    case SurfaceDescriptor::TSurfaceDescriptorDMABuf: {
      const SurfaceDescriptorDMABuf& desc = aDesc.get_SurfaceDescriptorDMABuf();
      result = new DMABUFTextureHostOGL(aFlags, desc);
      if (!result->IsValid()) {
        gfxCriticalError() << "DMABuf surface import failed!";
        result = nullptr;
      }
      break;
    }
#endif

#ifdef XP_MACOSX
    case SurfaceDescriptor::TSurfaceDescriptorMacIOSurface: {
      const SurfaceDescriptorMacIOSurface& desc =
          aDesc.get_SurfaceDescriptorMacIOSurface();
      result = new MacIOSurfaceTextureHostOGL(aFlags, desc);
      break;
    }
#endif

    case SurfaceDescriptor::TSurfaceDescriptorSharedGLTexture: {
      if (aDeallocator && !aDeallocator->IsSameProcess()) {
        gfxCriticalError() << "SurfaceDescriptorSharedGLTexture must only be "
                              "used in same process";
        return nullptr;
      }
      const auto& desc = aDesc.get_SurfaceDescriptorSharedGLTexture();
      result =
          new GLTextureHost(aFlags, desc.texture(), desc.target(),
                            (GLsync)desc.fence(), desc.size(), desc.hasAlpha());
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("Unsupported SurfaceDescriptor type");
      break;
    }
  }
  return result.forget();
}

static gl::TextureImage::Flags FlagsToGLFlags(TextureFlags aFlags) {
  uint32_t result = TextureImage::NoFlags;

  if (aFlags & TextureFlags::USE_NEAREST_FILTER)
    result |= TextureImage::UseNearestFilter;
  if (aFlags & TextureFlags::ORIGIN_BOTTOM_LEFT)
    result |= TextureImage::OriginBottomLeft;
  if (aFlags & TextureFlags::DISALLOW_BIGIMAGE)
    result |= TextureImage::DisallowBigImage;

  return static_cast<gl::TextureImage::Flags>(result);
}

TextureImageTextureSourceOGL::TextureImageTextureSourceOGL(
    CompositorOGL* aCompositor, TextureFlags aFlags)
    : mGL(aCompositor->gl()),
      mCompositor(aCompositor),
      mFlags(aFlags),
      mIterating(false) {
  if (mCompositor) {
    mCompositor->RegisterTextureSource(this);
  }
}

TextureImageTextureSourceOGL::~TextureImageTextureSourceOGL() {
  DeallocateDeviceData();
}

void TextureImageTextureSourceOGL::DeallocateDeviceData() {
  mTexImage = nullptr;
  mGL = nullptr;
  if (mCompositor) {
    mCompositor->UnregisterTextureSource(this);
  }
  SetUpdateSerial(0);
}

bool TextureImageTextureSourceOGL::Update(gfx::DataSourceSurface* aSurface,
                                          nsIntRegion* aDestRegion,
                                          gfx::IntPoint* aSrcOffset,
                                          gfx::IntPoint* aDstOffset) {
  GLContext* gl = mGL;
  MOZ_ASSERT(gl);
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING(
        "trying to update TextureImageTextureSourceOGL without a GLContext");
    return false;
  }
  if (!aSurface) {
    gfxCriticalError() << "Invalid surface for OGL update";
    return false;
  }
  MOZ_ASSERT(aSurface);

  IntSize size = aSurface->GetSize();
  if (!mTexImage || (mTexImage->GetSize() != size && !aSrcOffset) ||
      mTexImage->GetContentType() !=
          gfx::ContentForFormat(aSurface->GetFormat())) {
    if (mFlags & TextureFlags::DISALLOW_BIGIMAGE) {
      GLint maxTextureSize;
      gl->fGetIntegerv(LOCAL_GL_MAX_TEXTURE_SIZE, &maxTextureSize);
      if (size.width > maxTextureSize || size.height > maxTextureSize) {
        NS_WARNING("Texture exceeds maximum texture size, refusing upload");
        return false;
      }
      // Explicitly use CreateBasicTextureImage instead of CreateTextureImage,
      // because CreateTextureImage might still choose to create a tiled
      // texture image.
      mTexImage = CreateBasicTextureImage(
          gl, size, gfx::ContentForFormat(aSurface->GetFormat()),
          LOCAL_GL_CLAMP_TO_EDGE, FlagsToGLFlags(mFlags));
    } else {
      // XXX - clarify which size we want to use. IncrementalContentHost will
      // require the size of the destination surface to be different from
      // the size of aSurface.
      // See bug 893300 (tracks the implementation of ContentHost for new
      // textures).
      mTexImage = CreateTextureImage(
          gl, size, gfx::ContentForFormat(aSurface->GetFormat()),
          LOCAL_GL_CLAMP_TO_EDGE, FlagsToGLFlags(mFlags),
          SurfaceFormatToImageFormat(aSurface->GetFormat()));
    }
    ClearCachedFilter();

    if (aDestRegion && !aSrcOffset &&
        !aDestRegion->IsEqual(gfx::IntRect(0, 0, size.width, size.height))) {
      // UpdateFromDataSource will ignore our specified aDestRegion since the
      // texture hasn't been allocated with glTexImage2D yet. Call Resize() to
      // force the allocation (full size, but no upload), and then we'll only
      // upload the pixels we care about below.
      mTexImage->Resize(size);
    }
  }

  return mTexImage->UpdateFromDataSource(aSurface, aDestRegion, aSrcOffset,
                                         aDstOffset);
}

void TextureImageTextureSourceOGL::EnsureBuffer(const IntSize& aSize,
                                                gfxContentType aContentType) {
  if (!mTexImage || mTexImage->GetSize() != aSize ||
      mTexImage->GetContentType() != aContentType) {
    mTexImage =
        CreateTextureImage(mGL, aSize, aContentType, LOCAL_GL_CLAMP_TO_EDGE,
                           FlagsToGLFlags(mFlags));
  }
  mTexImage->Resize(aSize);
}

gfx::IntSize TextureImageTextureSourceOGL::GetSize() const {
  if (mTexImage) {
    if (mIterating) {
      return mTexImage->GetTileRect().Size();
    }
    return mTexImage->GetSize();
  }
  NS_WARNING("Trying to query the size of an empty TextureSource.");
  return gfx::IntSize(0, 0);
}

gfx::SurfaceFormat TextureImageTextureSourceOGL::GetFormat() const {
  if (mTexImage) {
    return mTexImage->GetTextureFormat();
  }
  NS_WARNING("Trying to query the format of an empty TextureSource.");
  return gfx::SurfaceFormat::UNKNOWN;
}

gfx::IntRect TextureImageTextureSourceOGL::GetTileRect() {
  return mTexImage->GetTileRect();
}

void TextureImageTextureSourceOGL::BindTexture(
    GLenum aTextureUnit, gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mTexImage,
             "Trying to bind a TextureSource that does not have an underlying "
             "GL texture.");
  mTexImage->BindTexture(aTextureUnit);
  SetSamplingFilter(mGL, aSamplingFilter);
}

////////////////////////////////////////////////////////////////////////
// GLTextureSource

GLTextureSource::GLTextureSource(TextureSourceProvider* aProvider,
                                 GLuint aTextureHandle, GLenum aTarget,
                                 gfx::IntSize aSize, gfx::SurfaceFormat aFormat)
    : GLTextureSource(aProvider->GetGLContext(), aTextureHandle, aTarget, aSize,
                      aFormat) {}

GLTextureSource::GLTextureSource(GLContext* aGL, GLuint aTextureHandle,
                                 GLenum aTarget, gfx::IntSize aSize,
                                 gfx::SurfaceFormat aFormat)
    : mGL(aGL),
      mTextureHandle(aTextureHandle),
      mTextureTarget(aTarget),
      mSize(aSize),
      mFormat(aFormat) {
  MOZ_COUNT_CTOR(GLTextureSource);
}

GLTextureSource::~GLTextureSource() {
  MOZ_COUNT_DTOR(GLTextureSource);
  DeleteTextureHandle();
}

void GLTextureSource::DeallocateDeviceData() { DeleteTextureHandle(); }

void GLTextureSource::DeleteTextureHandle() {
  GLContext* gl = this->gl();
  if (mTextureHandle != 0 && gl && gl->MakeCurrent()) {
    gl->fDeleteTextures(1, &mTextureHandle);
  }
  mTextureHandle = 0;
}

void GLTextureSource::BindTexture(GLenum aTextureUnit,
                                  gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mTextureHandle != 0);
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    return;
  }
  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, mTextureHandle);
  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool GLTextureSource::IsValid() const { return !!gl() && mTextureHandle != 0; }

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// SurfaceTextureHost

#ifdef MOZ_WIDGET_ANDROID

SurfaceTextureSource::SurfaceTextureSource(
    TextureSourceProvider* aProvider,
    mozilla::java::GeckoSurfaceTexture::Ref& aSurfTex,
    gfx::SurfaceFormat aFormat, GLenum aTarget, GLenum aWrapMode,
    gfx::IntSize aSize, Maybe<gfx::Matrix4x4> aTransformOverride)
    : mGL(aProvider->GetGLContext()),
      mSurfTex(aSurfTex),
      mFormat(aFormat),
      mTextureTarget(aTarget),
      mWrapMode(aWrapMode),
      mSize(aSize),
      mTransformOverride(aTransformOverride) {}

void SurfaceTextureSource::BindTexture(GLenum aTextureUnit,
                                       gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mSurfTex);
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING("Trying to bind a texture without a GLContext");
    return;
  }

  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, mSurfTex->GetTexName());

  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool SurfaceTextureSource::IsValid() const { return !!gl(); }

gfx::Matrix4x4 SurfaceTextureSource::GetTextureTransform() {
  MOZ_ASSERT(mSurfTex);

  gfx::Matrix4x4 ret;

  // GetTransformMatrix() returns the transform set by the producer side of the
  // SurfaceTexture that must be applied to texture coordinates when
  // sampling. In some cases we may have set an override value, such as for
  // MediaCodec output on devices where we know the value is incorrect.
  if (mTransformOverride) {
    ret = *mTransformOverride;
  } else {
    const auto& surf = java::sdk::SurfaceTexture::LocalRef(
        java::sdk::SurfaceTexture::Ref::From(mSurfTex));
    AndroidSurfaceTexture::GetTransformMatrix(surf, &ret);
  }

  return ret;
}

void SurfaceTextureSource::DeallocateDeviceData() { mSurfTex = nullptr; }

////////////////////////////////////////////////////////////////////////

SurfaceTextureHost::SurfaceTextureHost(
    TextureFlags aFlags, mozilla::java::GeckoSurfaceTexture::Ref& aSurfTex,
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat, bool aContinuousUpdate,
    bool aForceBT709ColorSpace, Maybe<Matrix4x4> aTransformOverride)
    : TextureHost(TextureHostType::AndroidSurfaceTexture, aFlags),
      mSurfTex(aSurfTex),
      mSize(aSize),
      mFormat(aFormat),
      mContinuousUpdate(aContinuousUpdate),
      mForceBT709ColorSpace(aForceBT709ColorSpace),
      mTransformOverride(aTransformOverride) {
  if (!mSurfTex) {
    return;
  }

  // Continuous update makes no sense with single buffer mode
  MOZ_ASSERT(!mSurfTex->IsSingleBuffer() || !mContinuousUpdate);

  mSurfTex->IncrementUse();
}

SurfaceTextureHost::~SurfaceTextureHost() {
  if (mSurfTex) {
    mSurfTex->DecrementUse();
    mSurfTex = nullptr;
  }
}

gl::GLContext* SurfaceTextureHost::gl() const { return nullptr; }

gfx::SurfaceFormat SurfaceTextureHost::GetFormat() const { return mFormat; }

void SurfaceTextureHost::DeallocateDeviceData() {
  if (mTextureSource) {
    mTextureSource->DeallocateDeviceData();
  }

  if (mSurfTex) {
    mSurfTex->DecrementUse();
    mSurfTex = nullptr;
  }
}

void SurfaceTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  bool isRemoteTexture = !!(mFlags & TextureFlags::REMOTE_TEXTURE);
  RefPtr texture = MakeRefPtr<wr::RenderAndroidSurfaceTextureHost>(
      mSurfTex, mSize, mFormat, mContinuousUpdate, mTransformOverride,
      isRemoteTexture);
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t SurfaceTextureHost::NumSubTextures() { return mSurfTex ? 1 : 0; }

void SurfaceTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(
          aResources.GetCapabilities().mBackendType, GetSize());
  auto imageType = wr::ExternalImageType::TextureHandle(
      wr::ImageBufferKind::TextureExternal);
  if (policy == TextureHost::NativeTexturePolicy::REQUIRE) {
    imageType =
        wr::ExternalImageType::TextureHandle(wr::ImageBufferKind::TextureRect);
  } else if (mForceBT709ColorSpace) {
    imageType = wr::ExternalImageType::TextureHandle(
        wr::ImageBufferKind::TextureExternalBT709);
  }

  // Hardware webrender directly renders from the SurfaceTexture therefore we
  // must provide it the (transformed) normalized UVs. For software webrender we
  // first read from the SurfaceTexture in to a CPU buffer, which we sample from
  // using unnormalized UVs. The readback code handles the texture transform.
  // See RenderAndroidSurfaceTextureHost::Lock() and
  // RenderAndroidSurfaceTextureHost::ReadTexImage(), respectively.
  const bool normalizedUvs =
      aResources.GetCapabilities().mBackendType == WebRenderBackend::HARDWARE;

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return;
      }

      // XXX Add RGBA handling. Temporary hack to avoid crash
      // With BGRA format setting, rendering works without problem.
      wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                     GetFormat() == gfx::SurfaceFormat::R8G8B8A8
                                         ? wr::OpacityType::HasAlphaChannel
                                         : wr::OpacityType::Opaque);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           normalizedUvs);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

void SurfaceTextureHost::PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                                          const wr::LayoutRect& aBounds,
                                          const wr::LayoutRect& aClip,
                                          wr::ImageRendering aFilter,
                                          const Range<wr::ImageKey>& aImageKeys,
                                          PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length");
        return;
      }
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface, supportsExternalCompositing);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

bool SurfaceTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

SurfaceDescriptor SurfaceTextureHost::GetSurfaceDescriptor() {
  if (!mSurfTex) {
    return SurfaceDescriptor();
  }
  return SurfaceTextureDescriptor(mSurfTex->GetHandle(), mSize, mFormat,
                                  mContinuousUpdate, mForceBT709ColorSpace,
                                  mTransformOverride);
}

////////////////////////////////////////////////////////////////////////
// AndroidHardwareBufferTextureSource

AndroidHardwareBufferTextureSource::AndroidHardwareBufferTextureSource(
    TextureSourceProvider* aProvider,
    AndroidHardwareBuffer* aAndroidHardwareBuffer, gfx::SurfaceFormat aFormat,
    GLenum aTarget, GLenum aWrapMode, gfx::IntSize aSize)
    : mGL(aProvider->GetGLContext()),
      mAndroidHardwareBuffer(aAndroidHardwareBuffer),
      mFormat(aFormat),
      mTextureTarget(aTarget),
      mWrapMode(aWrapMode),
      mSize(aSize),
      mEGLImage(EGL_NO_IMAGE),
      mTextureHandle(0) {}

AndroidHardwareBufferTextureSource::~AndroidHardwareBufferTextureSource() {
  DeleteTextureHandle();
  DestroyEGLImage();
}

bool AndroidHardwareBufferTextureSource::EnsureEGLImage() {
  if (!mAndroidHardwareBuffer) {
    return false;
  }

  auto fenceFd = mAndroidHardwareBuffer->GetAndResetAcquireFence();
  if (fenceFd) {
    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;

    // Like Chromium's AndroidImageBacking::BeginRead() and
    // ImageReaderGLOwner::ScopedCurrentImageRef::GetReadyFence(), give EGL a
    // duplicate FD while retaining the original fence. Restore it on failure
    // so a subsequent bind or readback still waits for the producer.
    auto restoreAcquireFence = MakeScopeExit(
        [&] { mAndroidHardwareBuffer->SetAcquireFence(std::move(fenceFd)); });
    UniqueFileHandle importFd = DuplicateFileHandle(fenceFd);
    if (!importFd) {
      gfxCriticalNoteOnce
          << "AHB compositor: failed to duplicate acquire fence FD";
      DeleteTextureHandle();
      DestroyEGLImage();
      return false;
    }

    // EGL takes ownership of the duplicate, including on creation failure.
    const EGLint attribs[] = {LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                              importFd.release(), LOCAL_EGL_NONE};
    EGLSync sync =
        egl->fCreateSyncKHR(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
    if (!sync) {
      const EGLint eglError = egl->mLib->fGetError();
      gfxCriticalNoteOnce
          << "AHB compositor: failed to import acquire fence, EGL error: "
          << eglError;
      DeleteTextureHandle();
      DestroyEGLImage();
      return false;
    }
    bool waitSucceeded;
    if (egl->IsExtensionSupported(gl::EGLExtension::KHR_wait_sync)) {
      // Queue a GPU-side wait without blocking the CPU.
      waitSucceeded = egl->fWaitSync(sync, 0) == LOCAL_EGL_TRUE;
    } else {
      // Without server-side wait support, block the CPU until the fence
      // signals.
      waitSucceeded = egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER) ==
                      LOCAL_EGL_CONDITION_SATISFIED;
    }
    egl->fDestroySync(sync);
    if (!waitSucceeded) {
      gfxCriticalNoteOnce << "AHB compositor: acquire fence wait failed";
      DeleteTextureHandle();
      DestroyEGLImage();
      return false;
    }

    // A successful wait no longer needs the original FD for retry.
    restoreAcquireFence.release();
  }

  if (mTextureHandle) {
    return true;
  }

  if (!mEGLImage) {
    // XXX add crop handling for video
    // Should only happen the first time.
    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;

    const EGLint attrs[] = {
        LOCAL_EGL_IMAGE_PRESERVED,
        LOCAL_EGL_TRUE,
        LOCAL_EGL_NONE,
        LOCAL_EGL_NONE,
    };

    EGLClientBuffer clientBuffer = egl->mLib->fGetNativeClientBufferANDROID(
        mAndroidHardwareBuffer->GetNativeBuffer());
    mEGLImage = egl->fCreateImage(
        EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
  }
  MOZ_ASSERT(mEGLImage);

  mGL->fGenTextures(1, &mTextureHandle);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, mTextureHandle);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, mEGLImage);

  return true;
}

void AndroidHardwareBufferTextureSource::DeleteTextureHandle() {
  if (!mTextureHandle) {
    return;
  }
  MOZ_ASSERT(mGL);
  mGL->fDeleteTextures(1, &mTextureHandle);
  mTextureHandle = 0;
}

void AndroidHardwareBufferTextureSource::DestroyEGLImage() {
  if (!mEGLImage) {
    return;
  }
  MOZ_ASSERT(mGL);
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  egl->fDestroyImage(mEGLImage);
  mEGLImage = EGL_NO_IMAGE;
}

void AndroidHardwareBufferTextureSource::BindTexture(
    GLenum aTextureUnit, gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mAndroidHardwareBuffer);
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING("Trying to bind a texture without a GLContext");
    return;
  }

  if (!EnsureEGLImage()) {
    return;
  }

  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, mTextureHandle);

  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool AndroidHardwareBufferTextureSource::IsValid() const { return !!gl(); }

void AndroidHardwareBufferTextureSource::DeallocateDeviceData() {
  DestroyEGLImage();
  DeleteTextureHandle();
  mAndroidHardwareBuffer = nullptr;
}

////////////////////////////////////////////////////////////////////////
// AndroidHardwareBufferTextureHost

/* static */
already_AddRefed<AndroidHardwareBufferTextureHost>
AndroidHardwareBufferTextureHost::Create(
    TextureFlags aFlags, const SurfaceDescriptorAndroidHardwareBuffer& aDesc) {
  RefPtr<AndroidHardwareBuffer> buffer =
      AndroidHardwareBufferManager::Get()->GetBuffer(aDesc.bufferId());
  if (!buffer) {
    return nullptr;
  }
  RefPtr host = MakeRefPtr<AndroidHardwareBufferTextureHost>(aFlags, buffer);
  return host.forget();
}

AndroidHardwareBufferTextureHost::AndroidHardwareBufferTextureHost(
    TextureFlags aFlags, AndroidHardwareBuffer* aAndroidHardwareBuffer)
    : TextureHost(TextureHostType::AndroidHardwareBuffer, aFlags),
      mAndroidHardwareBuffer(aAndroidHardwareBuffer) {
  MOZ_ASSERT(mAndroidHardwareBuffer);
}

AndroidHardwareBufferTextureHost::~AndroidHardwareBufferTextureHost() {}

gl::GLContext* AndroidHardwareBufferTextureHost::gl() const { return nullptr; }

void AndroidHardwareBufferTextureHost::NotifyNotUsed() {
  TextureHost::NotifyNotUsed();
}

gfx::SurfaceFormat AndroidHardwareBufferTextureHost::GetFormat() const {
  if (mAndroidHardwareBuffer) {
    return mAndroidHardwareBuffer->mFormat;
  }
  return gfx::SurfaceFormat::UNKNOWN;
}

gfx::IntSize AndroidHardwareBufferTextureHost::GetSize() const {
  if (mAndroidHardwareBuffer) {
    return mAndroidHardwareBuffer->mSize;
  }
  return gfx::IntSize();
}

void AndroidHardwareBufferTextureHost::DeallocateDeviceData() {
  mAndroidHardwareBuffer = nullptr;
}

void AndroidHardwareBufferTextureHost::SetReadFence(Fence* aReadFence) {
  MOZ_ASSERT(aReadFence);
  MOZ_ASSERT(aReadFence->AsFenceFileHandle());
  MOZ_ASSERT(mAndroidHardwareBuffer);

  if (!aReadFence || !aReadFence->AsFenceFileHandle() ||
      !mAndroidHardwareBuffer) {
    return;
  }

  UniqueFileHandle handle =
      aReadFence->AsFenceFileHandle()->DuplicateFileHandle();
  mAndroidHardwareBuffer->SetReleaseFence(std::move(handle));
}

void AndroidHardwareBufferTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr<wr::RenderTextureHost> texture =
      new wr::RenderAndroidHardwareBufferTextureHost(mAndroidHardwareBuffer);
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t AndroidHardwareBufferTextureHost::NumSubTextures() {
  return mAndroidHardwareBuffer ? 1 : 0;
}

void AndroidHardwareBufferTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(
          aResources.GetCapabilities().mBackendType, GetSize());
  auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                       ? wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureRect)
                       : wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureExternal);

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return;
      }

      // XXX Add RGBA handling. Temporary hack to avoid crash
      // With BGRA format setting, rendering works without problem.
      wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                     GetFormat() == gfx::SurfaceFormat::R8G8B8A8
                                         ? wr::OpacityType::HasAlphaChannel
                                         : wr::OpacityType::Opaque);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

void AndroidHardwareBufferTextureHost::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length");
        return;
      }
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface, supportsExternalCompositing);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

bool AndroidHardwareBufferTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

////////////////////////////////////////////////////////////////////////
// AndroidImageReaderImageTextureSource

AndroidImageReaderImageTextureSource::AndroidImageReaderImageTextureSource(
    TextureSourceProvider* aProvider,
    AndroidImageConsumer* aAndroidImageConsumer, gfx::SurfaceFormat aFormat,
    GLenum aTarget, GLenum aWrapMode, gfx::IntSize aSize)
    : mGL(aProvider->GetGLContext()),
      mAndroidImageConsumer(aAndroidImageConsumer),
      mFormat(aFormat),
      mTextureTarget(aTarget),
      mWrapMode(aWrapMode),
      mSize(aSize) {}

AndroidImageReaderImageTextureSource::~AndroidImageReaderImageTextureSource() {}

void AndroidImageReaderImageTextureSource::BindTexture(
    GLenum aTextureUnit, gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mAndroidImageConsumer);

  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING("Trying to bind a texture without a GLContext");
    return;
  }

  const auto handle = mAndroidImageConsumer->GetGlTextureHandle();

  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, handle);

  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool AndroidImageReaderImageTextureSource::IsValid() const { return !!gl(); }

void AndroidImageReaderImageTextureSource::DeallocateDeviceData() {
  mAndroidImageConsumer = nullptr;
}

////////////////////////////////////////////////////////////////////////

AndroidImageReaderImageTextureHost::AndroidImageReaderImageTextureHost(
    TextureFlags aFlags, const AndroidImageReaderImageDescriptor& aDescriptor)
    : TextureHost(TextureHostType::AndroidSurfaceTexture, aFlags),
      mDescriptor(aDescriptor),
      mImageReaderId(aDescriptor.imageReaderId()),
      mFrameId(aDescriptor.frameId()),
      mSize(aDescriptor.size()),
      mFormat(aDescriptor.format()) {}

AndroidImageReaderImageTextureHost::~AndroidImageReaderImageTextureHost() {}

gfx::SurfaceFormat AndroidImageReaderImageTextureHost::GetFormat() const {
  return mFormat;
}

void AndroidImageReaderImageTextureHost::DeallocateDeviceData() {}

void AndroidImageReaderImageTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr<wr::RenderTextureHost> texture =
      new wr::RenderAndroidImageReaderImageTextureHost(mImageReaderId, mFrameId,
                                                       mSize, mFormat);
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t AndroidImageReaderImageTextureHost::NumSubTextures() { return 1; }

void AndroidImageReaderImageTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(
          aResources.GetCapabilities().mBackendType, GetSize());
  auto imageType = wr::ExternalImageType::TextureHandle(
      wr::ImageBufferKind::TextureExternal);
  if (policy == TextureHost::NativeTexturePolicy::REQUIRE) {
    imageType =
        wr::ExternalImageType::TextureHandle(wr::ImageBufferKind::TextureRect);
  }

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        return;
      }

      // XXX Add RGBA handling. Temporary hack to avoid crash
      // With BGRA format setting, rendering works without problem.
      wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                     GetFormat() == gfx::SurfaceFormat::R8G8B8A8
                                         ? wr::OpacityType::HasAlphaChannel
                                         : wr::OpacityType::Opaque);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

void AndroidImageReaderImageTextureHost::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      if (aImageKeys.length() != 1) {
        MOZ_ASSERT_UNREACHABLE("unexpected key length");
        return;
      }
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface, supportsExternalCompositing);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

bool AndroidImageReaderImageTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

SurfaceDescriptor AndroidImageReaderImageTextureHost::GetSurfaceDescriptor() {
  return mDescriptor;
}

#endif  // MOZ_WIDGET_ANDROID

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// EGLImage

EGLImageTextureSource::EGLImageTextureSource(TextureSourceProvider* aProvider,
                                             EGLImage aImage,
                                             gfx::SurfaceFormat aFormat,
                                             GLenum aTarget, GLenum aWrapMode,
                                             gfx::IntSize aSize)
    : mGL(aProvider->GetGLContext()),
      mCompositor(aProvider->AsCompositorOGL()),
      mImage(aImage),
      mFormat(aFormat),
      mTextureTarget(aTarget),
      mWrapMode(aWrapMode),
      mSize(aSize) {
  MOZ_ASSERT(mTextureTarget == LOCAL_GL_TEXTURE_2D ||
             mTextureTarget == LOCAL_GL_TEXTURE_EXTERNAL);
}

void EGLImageTextureSource::BindTexture(GLenum aTextureUnit,
                                        gfx::SamplingFilter aSamplingFilter) {
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING("Trying to bind a texture without a GLContext");
    return;
  }

#ifdef DEBUG
  const bool supportsEglImage = [&]() {
    const auto& gle = GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    return egl->HasKHRImageBase() &&
           egl->IsExtensionSupported(EGLExtension::KHR_gl_texture_2D_image) &&
           gl->IsExtensionSupported(GLContext::OES_EGL_image);
  }();
  MOZ_ASSERT(supportsEglImage, "EGLImage not supported or disabled in runtime");
#endif

  GLuint tex = mCompositor->GetTemporaryTexture(mTextureTarget, aTextureUnit);

  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, tex);

  gl->fEGLImageTargetTexture2D(mTextureTarget, mImage);

  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool EGLImageTextureSource::IsValid() const { return !!gl(); }

gfx::Matrix4x4 EGLImageTextureSource::GetTextureTransform() {
  gfx::Matrix4x4 ret;
  return ret;
}

////////////////////////////////////////////////////////////////////////

EGLImageTextureHost::EGLImageTextureHost(TextureFlags aFlags, EGLImage aImage,
                                         EGLSync aSync, gfx::IntSize aSize,
                                         bool hasAlpha)
    : TextureHost(TextureHostType::EGLImage, aFlags),
      mImage(aImage),
      mSync(aSync),
      mSize(aSize),
      mHasAlpha(hasAlpha) {}

EGLImageTextureHost::~EGLImageTextureHost() = default;

gl::GLContext* EGLImageTextureHost::gl() const { return nullptr; }

gfx::SurfaceFormat EGLImageTextureHost::GetFormat() const {
  return mHasAlpha ? gfx::SurfaceFormat::R8G8B8A8
                   : gfx::SurfaceFormat::R8G8B8X8;
}

void EGLImageTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr texture = MakeRefPtr<wr::RenderEGLImageTextureHost>(
      mImage, mSync, mSize, GetFormat());
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

void EGLImageTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(
          aResources.GetCapabilities().mBackendType, GetSize());
  auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                       ? wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureRect)
                       : wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureExternal);

  gfx::SurfaceFormat format = GetFormat();

  if (aImageKeys.length() != 1) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  // XXX Add RGBA handling. Temporary hack to avoid crash
  // With BGRA format setting, rendering works without problem.
  wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                 format == gfx::SurfaceFormat::R8G8B8A8
                                     ? wr::OpacityType::HasAlphaChannel
                                     : wr::OpacityType::Opaque);
  (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                       /* aNormalizedUvs */ false);
}

void EGLImageTextureHost::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());

  if (aImageKeys.length() != 1) {
    MOZ_ASSERT_UNREACHABLE("unexpected key length");
    return;
  }
  aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                     !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                     wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                     preferCompositorSurface, supportsExternalCompositing);
}

bool EGLImageTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

//

GLTextureHost::GLTextureHost(TextureFlags aFlags, GLuint aTextureHandle,
                             GLenum aTarget, GLsync aSync, gfx::IntSize aSize,
                             bool aHasAlpha)
    : TextureHost(TextureHostType::GLTexture, aFlags),
      mTexture(aTextureHandle),
      mTarget(aTarget),
      mSync(aSync),
      mSize(aSize),
      mHasAlpha(aHasAlpha) {}

GLTextureHost::~GLTextureHost() = default;

gl::GLContext* GLTextureHost::gl() const { return nullptr; }

gfx::SurfaceFormat GLTextureHost::GetFormat() const {
  MOZ_ASSERT(mTextureSource);
  return mTextureSource ? mTextureSource->GetFormat()
                        : gfx::SurfaceFormat::UNKNOWN;
}

}  // namespace layers
}  // namespace mozilla
