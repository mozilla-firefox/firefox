/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureHost.h"

#include <limits>

#include "../opengl/CompositorOGL.h"
#include "CompositableHost.h"  // for CompositableHost
#include "gfxUtils.h"
#include "mozilla/RefPtr.h"  // for nsRefPtr
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/gfx/2D.h"  // for DataSourceSurface, Factory
#include "mozilla/gfx/CanvasManagerParent.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/Shmem.h"  // for Shmem
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/CompositableTransactionParent.h"  // for CompositableParentManager
#include "mozilla/layers/Compositor.h"                     // for Compositor
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/GPUVideoTextureHost.h"
#include "mozilla/layers/ISurfaceAllocator.h"  // for ISurfaceAllocator
#include "mozilla/layers/ImageBridgeParent.h"  // for ImageBridgeParent
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "mozilla/layers/PTextureParent.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureHostOGL.h"  // for TextureHostOGL
#include "mozilla/layers/VideoBridgeParent.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/webrender/RenderBufferTextureHost.h"
#include "mozilla/webrender/RenderExternalTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsAString.h"
#include "nsPrintfCString.h"  // for nsPrintfCString

#ifdef XP_MACOSX
#  include "../opengl/MacIOSurfaceTextureHostOGL.h"
#endif

#ifdef XP_WIN
#  include "../d3d11/CompositorD3D11.h"
#  include "mozilla/layers/TextureD3D11.h"
#  ifdef MOZ_WMF_MEDIA_ENGINE
#    include "mozilla/layers/DcompSurfaceImage.h"
#  endif
#endif

#if 0
#  define RECYCLE_LOG(...) printf_stderr(__VA_ARGS__)
#else
#  define RECYCLE_LOG(...) \
    do {                   \
    } while (0)
#endif

namespace mozilla {
namespace layers {

/**
 * TextureParent is the host-side IPDL glue between TextureClient and
 * TextureHost. It is an IPDL actor just like LayerParent, CompositableParent,
 * etc.
 */
class TextureParent final : public PTextureParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TextureParent, final)

  TextureParent(HostIPCAllocator* aAllocator,
                const dom::ContentParentId& aContentId, uint64_t aSerial,
                const wr::MaybeExternalImageId& aExternalImageId);

  bool Init(const SurfaceDescriptor& aSharedData,
            ReadLockDescriptor&& aReadLock, const LayersBackend& aLayersBackend,
            const TextureFlags& aFlags);

  void NotifyNotUsed(uint64_t aTransactionId);

  mozilla::ipc::IPCResult RecvRecycleTexture(
      const TextureFlags& aTextureFlags) final;

  mozilla::ipc::IPCResult RecvDestroy() final {
    (void)Send__delete__(this);
    return IPC_OK();
  }

  void ActorDestroy(ActorDestroyReason aWhy) override;

  TextureHost* GetTextureHost() { return mTextureHost; }

  void Destroy();

  const dom::ContentParentId& GetContentId() const { return mContentId; }

  uint64_t GetSerial() const { return mSerial; }

  HostIPCAllocator* mSurfaceAllocator;
  RefPtr<TextureHost> mTextureHost;
  dom::ContentParentId mContentId;
  // mSerial is unique in TextureClient's process.
  const uint64_t mSerial;
  wr::MaybeExternalImageId mExternalImageId;

 private:
  virtual ~TextureParent();
};

static bool WrapWithWebRenderTextureHost(ISurfaceAllocator* aDeallocator,
                                         LayersBackend aBackend,
                                         TextureFlags aFlags) {
  if (!aDeallocator) {
    return false;
  }
  if ((aFlags & TextureFlags::SNAPSHOT) ||
      (!aDeallocator->UsesImageBridge() &&
       !aDeallocator->AsCompositorBridgeParentBase())) {
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
already_AddRefed<PTextureParent> TextureHost::CreateIPDLActor(
    HostIPCAllocator* aAllocator, const SurfaceDescriptor& aSharedData,
    ReadLockDescriptor&& aReadLock, LayersBackend aLayersBackend,
    TextureFlags aFlags, const dom::ContentParentId& aContentId,
    uint64_t aSerial, const wr::MaybeExternalImageId& aExternalImageId) {
  MOZ_ASSERT(!(aFlags & TextureFlags::DEALLOCATE_CLIENT));

  TextureFlags flags = aFlags & ~TextureFlags::DEALLOCATE_CLIENT;
  auto actor = MakeRefPtr<TextureParent>(aAllocator, aContentId, aSerial,
                                         aExternalImageId);
  if (!actor->Init(aSharedData, std::move(aReadLock), aLayersBackend, flags)) {
    return nullptr;
  }
  return actor.forget();
}

// static
TextureHost* TextureHost::AsTextureHost(PTextureParent* actor) {
  if (!actor) {
    return nullptr;
  }
  return static_cast<TextureParent*>(actor)->mTextureHost;
}

// static
uint64_t TextureHost::GetTextureSerial(PTextureParent* actor) {
  if (!actor) {
    return UINT64_MAX;
  }
  return static_cast<TextureParent*>(actor)->mSerial;
}

// static
dom::ContentParentId TextureHost::GetTextureContentId(PTextureParent* actor) {
  if (!actor) {
    return dom::ContentParentId();
  }
  return static_cast<TextureParent*>(actor)->mContentId;
}

PTextureParent* TextureHost::GetIPDLActor() { return mActor; }

void TextureHost::SetLastFwdTransactionId(uint64_t aTransactionId) {
  MOZ_ASSERT(mFwdTransactionId <= aTransactionId);
  mFwdTransactionId = aTransactionId;
}

already_AddRefed<TextureHost> CreateDummyBufferTextureHost(
    mozilla::layers::LayersBackend aBackend,
    mozilla::layers::TextureFlags aFlags) {
  // Ensure that the host will delete the memory.
  aFlags &= ~TextureFlags::DEALLOCATE_CLIENT;
  aFlags |= TextureFlags::DUMMY_TEXTURE;
  UniquePtr<TextureData> textureData(BufferTextureData::Create(
      gfx::IntSize(1, 1), gfx::SurfaceFormat::B8G8R8A8, gfx::ColorSpace2::SRGB,
      gfx::TransferFunction::SRGB, gfx::BackendType::SKIA, aBackend, aFlags,
      TextureAllocationFlags::ALLOC_DEFAULT, nullptr));
  SurfaceDescriptor surfDesc;
  textureData->Serialize(surfDesc);
  const SurfaceDescriptorBuffer& bufferDesc =
      surfDesc.get_SurfaceDescriptorBuffer();
  const MemoryOrShmem& data = bufferDesc.data();
  RefPtr host = MakeRefPtr<MemoryTextureHost>(
      reinterpret_cast<uint8_t*>(data.get_uintptr_t()), bufferDesc.desc(),
      aFlags);
  return host.forget();
}

already_AddRefed<TextureHost> TextureHost::Create(
    const SurfaceDescriptor& aDesc, ReadLockDescriptor&& aReadLock,
    HostIPCAllocator* aDeallocator, LayersBackend aBackend, TextureFlags aFlags,
    wr::MaybeExternalImageId& aExternalImageId) {
  RefPtr<TextureHost> result;

  switch (aDesc.type()) {
    case SurfaceDescriptor::TSurfaceDescriptorBuffer:
    case SurfaceDescriptor::TSurfaceDescriptorGPUVideo:
      result = CreateBackendIndependentTextureHost(aDesc, aDeallocator,
                                                   aBackend, aFlags);
      break;

    case SurfaceDescriptor::TEGLImageDescriptor:
    case SurfaceDescriptor::TSurfaceTextureDescriptor:
    case SurfaceDescriptor::TSurfaceDescriptorAndroidHardwareBuffer:
    case SurfaceDescriptor::TAndroidImageReaderImageDescriptor:
    case SurfaceDescriptor::TSurfaceDescriptorSharedGLTexture:
    case SurfaceDescriptor::TSurfaceDescriptorDMABuf:
      result = CreateTextureHostOGL(aDesc, aDeallocator, aBackend, aFlags);
      break;

    case SurfaceDescriptor::TSurfaceDescriptorMacIOSurface:
      result = CreateTextureHostOGL(aDesc, aDeallocator, aBackend, aFlags);
      break;

#ifdef XP_WIN
    case SurfaceDescriptor::TSurfaceDescriptorD3D10:
    case SurfaceDescriptor::TSurfaceDescriptorDXGIYCbCr:
      result = CreateTextureHostD3D11(aDesc, aDeallocator, aBackend, aFlags);
      break;
#  ifdef MOZ_WMF_MEDIA_ENGINE
    case SurfaceDescriptor::TSurfaceDescriptorDcompSurface:
      result =
          CreateTextureHostDcompSurface(aDesc, aDeallocator, aBackend, aFlags);
      break;
#  endif
#endif
    default:
      MOZ_CRASH("GFX: Unsupported Surface type host");
  }

  if (!result) {
    gfxCriticalNote << "TextureHost creation failure type=" << aDesc.type();
  }

  if (result && WrapWithWebRenderTextureHost(aDeallocator, aBackend, aFlags)) {
    MOZ_ASSERT(aExternalImageId.isSome());
    result = MakeRefPtr<WebRenderTextureHost>(aFlags, result,
                                              aExternalImageId.ref());
  }

  if (result) {
    result->DeserializeReadLock(std::move(aReadLock), aDeallocator);
  }

  return result.forget();
}

already_AddRefed<TextureHost> CreateBackendIndependentTextureHost(
    const SurfaceDescriptor& aDesc, ISurfaceAllocator* aDeallocator,
    LayersBackend aBackend, TextureFlags aFlags) {
  RefPtr<TextureHost> result;
  switch (aDesc.type()) {
    case SurfaceDescriptor::TSurfaceDescriptorBuffer: {
      const SurfaceDescriptorBuffer& bufferDesc =
          aDesc.get_SurfaceDescriptorBuffer();
      const MemoryOrShmem& data = bufferDesc.data();
      switch (data.type()) {
        case MemoryOrShmem::TShmem: {
          const ipc::Shmem& shmem = data.get_Shmem();
          if (!shmem.IsReadable()) {
            // We failed to map the shmem so we can't verify its size.
            // Attempting to construct a ShmemTextureHost with it will succeed,
            // but the resulting object will have a null shmem and can't ever be
            // locked or mapped -- it's not useful at all. We just return
            // nullptr instead.
            gfxCriticalError() << "Failed texture host with unmappable shmem.";
            return nullptr;
          }

          size_t bufSize = shmem.Size<char>();
          Maybe<size_t> reqSize;
          const BufferDescriptor& desc = bufferDesc.desc();
          switch (desc.type()) {
            case BufferDescriptor::TYCbCrDescriptor: {
              const YCbCrDescriptor& ycbcr = desc.get_YCbCrDescriptor();
              reqSize = ImageDataSerializer::ComputeYCbCrBufferSize(
                  ycbcr.display(), ycbcr.ySize(), ycbcr.yStride(),
                  ycbcr.cbCrSize(), ycbcr.cbCrStride(), ycbcr.yOffset(),
                  ycbcr.cbOffset(), ycbcr.crOffset(), ycbcr.colorDepth(),
                  ycbcr.chromaSubsampling());
              break;
            }
            case BufferDescriptor::TRGBDescriptor: {
              const RGBDescriptor& rgb = desc.get_RGBDescriptor();
              reqSize = ImageDataSerializer::ComputeRGBBufferSize(rgb.size(),
                                                                  rgb.format());
              break;
            }
            default:
              gfxCriticalError()
                  << "Bad buffer host descriptor " << (int)desc.type();
              MOZ_CRASH("GFX: Bad descriptor");
          }

          if (reqSize.isNothing() || bufSize < reqSize.value()) {
            NS_ERROR(
                "A client process gave a shmem too small to fit for its "
                "descriptor!");
            return nullptr;
          }

          result =
              MakeRefPtr<ShmemTextureHost>(shmem, desc, aDeallocator, aFlags);
          break;
        }
        case MemoryOrShmem::Tuintptr_t: {
          if (aDeallocator && !aDeallocator->IsSameProcess()) {
            NS_ERROR(
                "A client process is trying to peek at our address space using "
                "a MemoryTexture!");
            return nullptr;
          }

          result = MakeRefPtr<MemoryTextureHost>(
              reinterpret_cast<uint8_t*>(data.get_uintptr_t()),
              bufferDesc.desc(), aFlags);
          break;
        }
        default:
          gfxCriticalError()
              << "Failed texture host for backend " << (int)data.type();
          MOZ_CRASH("GFX: No texture host for backend");
      }
      break;
    }
    case SurfaceDescriptor::TSurfaceDescriptorGPUVideo: {
      MOZ_ASSERT(aDesc.get_SurfaceDescriptorGPUVideo().type() ==
                 SurfaceDescriptorGPUVideo::TSurfaceDescriptorRemoteDecoder);
      result = GPUVideoTextureHost::CreateFromDescriptor(
          aDeallocator->GetContentId(), aFlags,
          aDesc.get_SurfaceDescriptorGPUVideo());
      break;
    }
    default: {
      NS_WARNING("No backend independent TextureHost for this descriptor type");
    }
  }
  return result.forget();
}

TextureHost::TextureHost(TextureHostType aType, TextureFlags aFlags)
    : AtomicRefCountedWithFinalize("TextureHost"),
      mTextureHostType(aType),
      mActor(nullptr),
      mFlags(aFlags),
      mCompositableCount(0),
      mFwdTransactionId(0),
      mReadLocked(false) {}

TextureHost::~TextureHost() {
  MOZ_ASSERT(mExternalImageId.isNothing());

  if (mReadLocked) {
    // If we still have a ReadLock, unlock it. At this point we don't care about
    // the texture client being written into on the other side since it should
    // be destroyed by now. But we will hit assertions if we don't ReadUnlock
    // before destroying the lock itself.
    ReadUnlock();
  }
  if (mDestroyedCallback) {
    mDestroyedCallback();
  }
}

void TextureHost::Finalize() {
  MaybeDestroyRenderTexture();

  if (!(GetFlags() & TextureFlags::DEALLOCATE_CLIENT)) {
    DeallocateSharedData();
    DeallocateDeviceData();
  }
}

void TextureHost::UnbindTextureSource() {
  if (mReadLocked) {
    ReadUnlock();
  }
}

void TextureHost::RecycleTexture(TextureFlags aFlags) {
  MOZ_ASSERT(GetFlags() & TextureFlags::RECYCLE);
  MOZ_ASSERT(aFlags & TextureFlags::RECYCLE);
  mFlags = aFlags;
}

void TextureHost::PrepareForUse() {}

void TextureHost::NotifyNotUsed() {
  if (!mActor) {
    if ((mFlags & TextureFlags::REMOTE_TEXTURE) && AsSurfaceTextureHost()) {
      MOZ_ASSERT(mExternalImageId.isSome());
      wr::RenderThread::Get()->NotifyNotUsed(*mExternalImageId);
    }
    return;
  }

  // Do not need to call NotifyNotUsed() if TextureHost does not have
  // TextureFlags::RECYCLE flag nor TextureFlags::WAIT_HOST_USAGE_END flag.
  if (!(GetFlags() & TextureFlags::RECYCLE) &&
      !(GetFlags() & TextureFlags::WAIT_HOST_USAGE_END)) {
    return;
  }

  mActor->NotifyNotUsed(mFwdTransactionId);
}

void TextureHost::CallNotifyNotUsed() {
  if (!mActor) {
    return;
  }
  mActor->NotifyNotUsed(mFwdTransactionId);
}

void TextureHost::MaybeDestroyRenderTexture() {
  if (mExternalImageId.isNothing()) {
    // RenderTextureHost was not created
    return;
  }
  // When TextureHost created RenderTextureHost, delete it here.
  TextureHost::DestroyRenderTexture(mExternalImageId.ref());
  mExternalImageId = Nothing();
}

void TextureHost::DestroyRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  wr::RenderThread::Get()->UnregisterExternalImage(aExternalImageId);
}

void TextureHost::EnsureRenderTexture(
    const wr::MaybeExternalImageId& aExternalImageId) {
  if (aExternalImageId.isNothing()) {
    // TextureHost is wrapped by GPUVideoTextureHost.
    if (mExternalImageId.isSome()) {
      // RenderTextureHost was already created.
      return;
    }
    mExternalImageId =
        Some(AsyncImagePipelineManager::GetNextExternalImageId());
  } else {
    // TextureHost is wrapped by WebRenderTextureHost.
    if (aExternalImageId == mExternalImageId) {
      // The texture has already been created.
      return;
    }
    MOZ_ASSERT(mExternalImageId.isNothing());
    mExternalImageId = aExternalImageId;
  }
  CreateRenderTexture(mExternalImageId.ref());
}

TextureSource::TextureSource() : mCompositableCount(0) {}

TextureSource::~TextureSource() = default;
BufferTextureHost::BufferTextureHost(const BufferDescriptor& aDesc,
                                     TextureFlags aFlags)
    : TextureHost(TextureHostType::Buffer, aFlags), mLocked(false) {
  mDescriptor = aDesc;
  switch (mDescriptor.type()) {
    case BufferDescriptor::TYCbCrDescriptor: {
      const YCbCrDescriptor& ycbcr = mDescriptor.get_YCbCrDescriptor();
      MOZ_ASSERT(gfx::IntRect(gfx::IntPoint(), ycbcr.ySize())
                     .Contains(ycbcr.display()));
      mSize = ycbcr.display().Size();
      if (ycbcr.colorDepth() == gfx::ColorDepth::COLOR_8) {
        mFormat = gfx::SurfaceFormat::YUV420;
      } else if (ycbcr.colorDepth() == gfx::ColorDepth::COLOR_10) {
        mFormat = gfx::SurfaceFormat::YUV420P10;
      } else {
        // we should probably do better for the other depths
        // but UNKNOWN seems good enough.
        mFormat = gfx::SurfaceFormat::UNKNOWN;
      }
      break;
    }
    case BufferDescriptor::TRGBDescriptor: {
      const RGBDescriptor& rgb = mDescriptor.get_RGBDescriptor();
      mSize = rgb.size();
      mFormat = rgb.format();
      break;
    }
    default:
      gfxCriticalError() << "Bad buffer host descriptor "
                         << (int)mDescriptor.type();
      MOZ_CRASH("GFX: Bad descriptor");
  }

#ifdef XP_MACOSX
  const int kMinSize = 1024;
  const int kMaxSize = 4096;
  mUseExternalTextures =
      kMaxSize >= mSize.width && mSize.width >= kMinSize &&
      kMaxSize >= mSize.height && mSize.height >= kMinSize &&
      StaticPrefs::gfx_webrender_enable_client_storage_AtStartup() &&
      !gfx::gfxVars::UseWebRenderANGLE();
#else
  mUseExternalTextures = false;
#endif
}

BufferTextureHost::~BufferTextureHost() = default;

void BufferTextureHost::DeallocateDeviceData() {}

bool BufferTextureHost::IsYCbCr() const {
  return mDescriptor.type() == BufferDescriptor::TYCbCrDescriptor;
}

void BufferTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr<wr::RenderTextureHost> texture;

  if (UseExternalTextures()) {
    texture = MakeRefPtr<wr::RenderExternalTextureHost>(GetBuffer(),
                                                        GetBufferDescriptor());
  } else {
    texture = MakeRefPtr<wr::RenderBufferTextureHost>(GetBuffer(),
                                                      GetBufferDescriptor());
  }

  if (auto* shmemTextureHost = AsShmemTextureHost()) {
    shmemTextureHost->OnRenderTextureCreated(texture);
  }

  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t BufferTextureHost::NumSubTextures() {
  if (IsYCbCr()) {
    return 3;
  }

  return 1;
}

void BufferTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Use native textures if our backend requires it, or if our backend doesn't
  // forbid it and we want to use them.
  NativeTexturePolicy policy = BackendNativeTexturePolicy(
      aResources.GetCapabilities().mBackendType, GetSize());
  bool useNativeTexture =
      (policy == REQUIRE) || (policy != FORBID && UseExternalTextures());
  auto imageType = useNativeTexture ? wr::ExternalImageType::TextureHandle(
                                          wr::ImageBufferKind::TextureRect)
                                    : wr::ExternalImageType::Buffer();

  if (!IsYCbCr()) {
    if (aImageKeys.length() != 1) {
      MOZ_ASSERT_UNREACHABLE("unexpected keys lenght");
      return;
    }

    auto stride =
        ImageDataSerializer::ComputeRGBStride(GetFormat(), GetSize().width);
    if (NS_WARN_IF(stride.isNothing())) {
      return;
    }

    auto format = wr::SurfaceFormatToImageFormat(GetFormat());
    if (NS_WARN_IF(!format)) {
      return;
    }
    wr::ImageDescriptor descriptor(GetSize(), stride.value(), *format,
                                   wr::ToOpacityType(GetFormat()));
    (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                         /* aNormalizedUvs */ false);
  } else {
    if (aImageKeys.length() != 3) {
      MOZ_ASSERT_UNREACHABLE("unexpected keys lenght");
      return;
    }

    const layers::YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    gfx::IntSize ySize = desc.display().Size();
    gfx::IntSize cbcrSize = ImageDataSerializer::GetCroppedCbCrSize(desc);
    gfx::SurfaceFormat surfaceFormat =
        SurfaceFormatForColorDepth(desc.colorDepth());
    auto format = wr::SurfaceFormatToImageFormat(surfaceFormat);
    if (NS_WARN_IF(!format)) {
      return;
    }
    auto opacity = wr::ToOpacityType(surfaceFormat);
    wr::ImageDescriptor yDescriptor(ySize, desc.yStride(), *format, opacity);
    wr::ImageDescriptor cbcrDescriptor(cbcrSize, desc.cbCrStride(), *format,
                                       opacity);
    (aResources.*method)(aImageKeys[0], yDescriptor, aExtID, imageType, 0,
                         /* aNormalizedUvs */ false);
    (aResources.*method)(aImageKeys[1], cbcrDescriptor, aExtID, imageType, 1,
                         /* aNormalizedUvs */ false);
    (aResources.*method)(aImageKeys[2], cbcrDescriptor, aExtID, imageType, 2,
                         /* aNormalizedUvs */ false);
  }
}

void BufferTextureHost::PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                                         const wr::LayoutRect& aBounds,
                                         const wr::LayoutRect& aClip,
                                         wr::ImageRendering aFilter,
                                         const Range<wr::ImageKey>& aImageKeys,
                                         PushDisplayItemFlagSet aFlags) {
  // SWGL should always try to bypass shaders and composite directly.
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool useExternalSurface =
      aFlags.contains(PushDisplayItemFlag::SUPPORTS_EXTERNAL_BUFFER_TEXTURES);
  if (!IsYCbCr()) {
    if (aImageKeys.length() != 1) {
      MOZ_ASSERT_UNREACHABLE("unexpected key length");
      return;
    }
    aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                       !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                       wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                       preferCompositorSurface, useExternalSurface);
  } else {
    if (aImageKeys.length() != 3) {
      MOZ_ASSERT_UNREACHABLE("unexpected key length");
      return;
    }
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    aBuilder.PushYCbCrPlanarImage(
        aBounds, aClip, true, aImageKeys[0], aImageKeys[1], aImageKeys[2],
        wr::ToWrColorDepth(desc.colorDepth()),
        wr::ToWrYuvColorSpace(desc.yUVColorSpace()),
        wr::ToWrColorRange(desc.colorRange()), aFilter, preferCompositorSurface,
        useExternalSurface);
  }
}

void TextureHost::DeserializeReadLock(ReadLockDescriptor&& aDesc,
                                      ISurfaceAllocator* aAllocator) {
  if (mReadLock) {
    return;
  }

  mReadLock = TextureReadLock::Deserialize(std::move(aDesc), aAllocator);
}

void TextureHost::SetReadLocked() {
  if (!mReadLock) {
    return;
  }
  // If mReadLocked is true it means we haven't read unlocked yet and the
  // content side should not have been able to write into this texture and read
  // lock again!
  MOZ_ASSERT(!mReadLocked);
  mReadLocked = true;
}

void TextureHost::ReadUnlock() {
  if (mReadLock && mReadLocked) {
    mReadLock->ReadUnlock();
    mReadLocked = false;
  }
}

bool TextureHost::NeedsYFlip() const {
  return bool(mFlags & TextureFlags::ORIGIN_BOTTOM_LEFT);
}

void BufferTextureHost::UnbindTextureSource() {
  // This texture is not used by any layer anymore.
  // If the texture has an intermediate buffer we don't care either because
  // texture uploads are also performed synchronously for BufferTextureHost.
  ReadUnlock();
}

gfx::SurfaceFormat BufferTextureHost::GetFormat() const { return mFormat; }

gfx::YUVColorSpace BufferTextureHost::GetYUVColorSpace() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.yUVColorSpace();
  }
  return gfx::YUVColorSpace::Identity;
}

gfx::TransferFunction BufferTextureHost::GetTransferFunction() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.transferFunction();
  }
  return gfx::TransferFunction::BT709;
}

gfx::ColorDepth BufferTextureHost::GetColorDepth() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.colorDepth();
  }
  return gfx::ColorDepth::COLOR_8;
}

gfx::ColorRange BufferTextureHost::GetColorRange() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.colorRange();
  }
  return TextureHost::GetColorRange();
}

gfx::ChromaSubsampling BufferTextureHost::GetChromaSubsampling() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.chromaSubsampling();
  }
  return gfx::ChromaSubsampling::FULL;
}

uint8_t* BufferTextureHost::GetYChannel() {
  if (mFormat == gfx::SurfaceFormat::YUV420) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetYChannel(GetBuffer(), desc);
  }
  return nullptr;
}

uint8_t* BufferTextureHost::GetCbChannel() {
  if (mFormat == gfx::SurfaceFormat::YUV420) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetCbChannel(GetBuffer(), desc);
  }
  return nullptr;
}

uint8_t* BufferTextureHost::GetCrChannel() {
  if (mFormat == gfx::SurfaceFormat::YUV420) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetCrChannel(GetBuffer(), desc);
  }
  return nullptr;
}

uint16_t* BufferTextureHost::GetYChannel16() {
  if (mFormat == gfx::SurfaceFormat::YUV420P10) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetYChannel(GetBuffer16(), desc);
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
  return nullptr;
}

uint16_t* BufferTextureHost::GetCbChannel16() {
  if (mFormat == gfx::SurfaceFormat::YUV420P10) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetCbChannel(GetBuffer16(), desc);
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
  return nullptr;
}

uint16_t* BufferTextureHost::GetCrChannel16() {
  if (mFormat == gfx::SurfaceFormat::YUV420P10) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return ImageDataSerializer::GetCrChannel(GetBuffer16(), desc);
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
  return nullptr;
}

int32_t BufferTextureHost::GetYStride() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.yStride();
  }
  return 0;
}

int32_t BufferTextureHost::GetCbCrStride() const {
  if (IsYCbCr()) {
    const YCbCrDescriptor& desc = mDescriptor.get_YCbCrDescriptor();
    return desc.cbCrStride();
  }
  return 0;
}

already_AddRefed<gfx::DataSourceSurface> BufferTextureHost::GetAsSurface(
    gfx::DataSourceSurface* aSurface) {
  RefPtr<gfx::DataSourceSurface> result;
  if (mFormat == gfx::SurfaceFormat::UNKNOWN) {
    NS_WARNING("BufferTextureHost: unsupported format!");
    return nullptr;
  }
  if (!GetBuffer()) {
    return nullptr;
  }
  if (IsYCbCr()) {
    result = ImageDataSerializer::DataSourceSurfaceFromYCbCrDescriptor(
        GetBuffer(), mDescriptor.get_YCbCrDescriptor(), aSurface);
    if (NS_WARN_IF(!result)) {
      return nullptr;
    }
  } else {
    auto stride =
        ImageDataSerializer::GetRGBStride(mDescriptor.get_RGBDescriptor());
    if (stride.isNothing()) {
      return nullptr;
    }

    struct Closure {
      RefPtr<nsISerialEventTarget> mEventTarget;
      RefPtr<Runnable> mRunnable;
    };

    RefPtr<nsISerialEventTarget> eventTarget = CompositorThread();
    RefPtr<Runnable> runnable =
        NS_NewRunnableFunction("BufferTextureHost::GetAsSurface::Runnable",
                               [self = RefPtr{this}]() {});

    Closure* closure = new Closure{eventTarget.forget(), runnable.forget()};

    auto destroyedCallback = [](void* aClosure) mutable {
      auto* closure = static_cast<Closure*>(aClosure);
      closure->mEventTarget->Dispatch(closure->mRunnable.forget());
      delete closure;
    };

    result = gfx::Factory::CreateWrappingDataSourceSurface(
        GetBuffer(), stride.value(), mSize, mFormat, destroyedCallback,
        closure);
  }
  return result.forget();
}

ShmemTextureHost::ShmemTextureHost(const ipc::Shmem& aShmem,
                                   const BufferDescriptor& aDesc,
                                   ISurfaceAllocator* aDeallocator,
                                   TextureFlags aFlags)
    : BufferTextureHost(aDesc, aFlags), mDeallocator(aDeallocator) {
  MOZ_ASSERT(!(mFlags & TextureFlags::DEALLOCATE_CLIENT));

  if (aShmem.IsReadable()) {
    UniquePtr<mozilla::ipc::Shmem> shmem = MakeUnique<ipc::Shmem>(aShmem);
    mShmemDeallocRunnable =
        MakeRefPtr<ShmemDeallocRunnable>(mDeallocator, std::move(shmem));
  } else {
    // This can happen if we failed to map the shmem on this process, perhaps
    // because it was big and we didn't have enough contiguous address space
    // available, even though we did on the child process.
    // As a result this texture will be in an invalid state and Lock will
    // always fail.

    gfxCriticalNote << "Failed to create a valid ShmemTextureHost";
  }

  MOZ_COUNT_CTOR(ShmemTextureHost);
}

ShmemTextureHost::~ShmemTextureHost() {
  DeallocateDeviceData();
  MOZ_COUNT_DTOR(ShmemTextureHost);
}

void ShmemTextureHost::DeallocateSharedData() {}

void ShmemTextureHost::ForgetSharedData() {}

void ShmemTextureHost::OnShutdown() { mShmemDeallocRunnable = nullptr; }

ShmemTextureHost::ShmemDeallocRunnable::ShmemDeallocRunnable(
    ISurfaceAllocator* aDeallocator, UniquePtr<mozilla::ipc::Shmem>&& aShmem)
    : Runnable("ShmemDeallocRunnable"),
      mDeallocator(aDeallocator),
      mShmem(std::move(aShmem)) {}

nsresult ShmemTextureHost::ShmemDeallocRunnable::Run() {
  if (!mDeallocator || !mShmem) {
    return NS_OK;
  }
  mDeallocator->AsShmemAllocator()->DeallocShmem(*mShmem);
  mShmem = nullptr;
  return NS_OK;
}

ShmemTextureHost::ShmemDeallocRunnable::~ShmemDeallocRunnable() {
  if (!mDeallocator || !mShmem) {
    return;
  }
  mDeallocator->AsShmemAllocator()->DeallocShmem(*mShmem);
}

void ShmemTextureHost::OnRenderTextureCreated(
    wr::RenderTextureHost* aRenderTexture) {
  MOZ_ASSERT(aRenderTexture);

  if (!mShmemDeallocRunnable || !mShmemDeallocRunnable->GetShmem()) {
    return;
  }

  RefPtr<nsISerialEventTarget> eventTarget = GetCurrentSerialEventTarget();
  RefPtr<ShmemDeallocRunnable> runnable = mShmemDeallocRunnable;

  auto destroyedCallback = [eventTarget = std::move(eventTarget),
                            runnable = std::move(runnable)]() mutable {
    eventTarget->Dispatch(runnable.forget());
  };

  aRenderTexture->SetDestroyedCallback(destroyedCallback);
}

uint8_t* ShmemTextureHost::GetBuffer() const {
  if (mShmemDeallocRunnable && mShmemDeallocRunnable->GetShmem()) {
    return mShmemDeallocRunnable->GetShmem()->get<uint8_t>();
  }
  return nullptr;
}

uint16_t* ShmemTextureHost::GetBuffer16() const {
  if (mShmemDeallocRunnable && mShmemDeallocRunnable->GetShmem()) {
    return mShmemDeallocRunnable->GetShmem()->get<uint16_t>();
  }
  return nullptr;
}

size_t ShmemTextureHost::GetBufferSize() const {
  if (mShmemDeallocRunnable && mShmemDeallocRunnable->GetShmem()) {
    return mShmemDeallocRunnable->GetShmem()->Size<uint8_t>();
  }
  return 0;
}

MemoryTextureHost::MemoryTextureHost(uint8_t* aBuffer,
                                     const BufferDescriptor& aDesc,
                                     TextureFlags aFlags)
    : BufferTextureHost(aDesc, aFlags), mBuffer(aBuffer) {
  MOZ_COUNT_CTOR(MemoryTextureHost);
}

MemoryTextureHost::~MemoryTextureHost() {
  MOZ_ASSERT(!mBuffer || (mFlags & TextureFlags::DEALLOCATE_CLIENT),
             "Leaking our buffer");
  DeallocateDeviceData();
  MOZ_COUNT_DTOR(MemoryTextureHost);
}

void MemoryTextureHost::DeallocateSharedData() {
  if (mBuffer) {
    GfxMemoryImageReporter::WillFree(mBuffer);
  }
  delete[] mBuffer;
  mBuffer = nullptr;
}

void MemoryTextureHost::ForgetSharedData() { mBuffer = nullptr; }

uint8_t* MemoryTextureHost::GetBuffer() const { return mBuffer; }
uint16_t* MemoryTextureHost::GetBuffer16() const {
  return reinterpret_cast<uint16_t*>(mBuffer);
}

size_t MemoryTextureHost::GetBufferSize() const {
  // MemoryTextureHost just trusts that the buffer size is large enough to read
  // anything we need to. That's because MemoryTextureHost has to trust the
  // buffer pointer anyway, so the security model here is just that
  // MemoryTexture's are restricted to same-process clients.
  return std::numeric_limits<size_t>::max();
}

TextureParent::TextureParent(HostIPCAllocator* aSurfaceAllocator,
                             const dom::ContentParentId& aContentId,
                             uint64_t aSerial,
                             const wr::MaybeExternalImageId& aExternalImageId)
    : mSurfaceAllocator(aSurfaceAllocator),
      mContentId(aContentId),
      mSerial(aSerial),
      mExternalImageId(aExternalImageId) {
  MOZ_COUNT_CTOR(TextureParent);
}

TextureParent::~TextureParent() { MOZ_COUNT_DTOR(TextureParent); }

void TextureParent::NotifyNotUsed(uint64_t aTransactionId) {
  if (!mTextureHost) {
    return;
  }
  mSurfaceAllocator->NotifyNotUsed(this, aTransactionId);
}

bool TextureParent::Init(const SurfaceDescriptor& aSharedData,
                         ReadLockDescriptor&& aReadLock,
                         const LayersBackend& aBackend,
                         const TextureFlags& aFlags) {
  mTextureHost =
      TextureHost::Create(aSharedData, std::move(aReadLock), mSurfaceAllocator,
                          aBackend, aFlags, mExternalImageId);
  if (mTextureHost) {
    mTextureHost->mActor = this;
  }

  return !!mTextureHost;
}

void TextureParent::Destroy() {
  if (!mTextureHost) {
    return;
  }

  if (mTextureHost->mReadLocked) {
    // ReadUnlock here to make sure the ReadLock's shmem does not outlive the
    // protocol that created it.
    mTextureHost->ReadUnlock();
  }

  if (mTextureHost->GetFlags() & TextureFlags::DEALLOCATE_CLIENT) {
    mTextureHost->ForgetSharedData();
  }

  mTextureHost->mActor = nullptr;
  mTextureHost = nullptr;
}

void TextureHost::ReceivedDestroy(PTextureParent* aActor) {
  static_cast<TextureParent*>(aActor)->RecvDestroy();
}

mozilla::ipc::IPCResult TextureParent::RecvRecycleTexture(
    const TextureFlags& aTextureFlags) {
  if (!mTextureHost) {
    return IPC_OK();
  }
  mTextureHost->RecycleTexture(aTextureFlags);
  return IPC_OK();
}

void TextureParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (VideoBridgeParent* manager = ActorDynCast<VideoBridgeParent>(Manager())) {
    manager->RemoveTexture(mSerial);
  }
  Destroy();
}

////////////////////////////////////////////////////////////////////////////////

}  // namespace layers
}  // namespace mozilla
