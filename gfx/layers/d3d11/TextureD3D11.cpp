/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureD3D11.h"

#include "CompositorD3D11.h"
#include "DXVA2Manager.h"
#include "Effects.h"
#include "MainThreadUtils.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxWindowsPlatform.h"
#include "mozilla/DataMutex.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/FileHandleWrapper.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/D3D11ZeroCopyTextureImage.h"
#include "mozilla/layers/FenceD3D11.h"
#include "mozilla/layers/CompositeProcessD3D11FencesHolderMap.h"
#include "mozilla/layers/GpuProcessD3D11TextureMap.h"
#include "mozilla/layers/HelpersD3D11.h"
#include "mozilla/layers/VideoProcessorD3D11.h"
#include "mozilla/webrender/RenderD3D11TextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {

using namespace gfx;

namespace layers {

gfx::DeviceResetReason DXGIErrorToDeviceResetReason(HRESULT aError) {
  switch (aError) {
    case S_OK:
      return gfx::DeviceResetReason::OK;
    case DXGI_ERROR_DEVICE_REMOVED:
      return gfx::DeviceResetReason::REMOVED;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      return gfx::DeviceResetReason::DRIVER_ERROR;
    case DXGI_ERROR_DEVICE_HUNG:
      return gfx::DeviceResetReason::HUNG;
    case DXGI_ERROR_DEVICE_RESET:
      return gfx::DeviceResetReason::RESET;
    case DXGI_ERROR_INVALID_CALL:
      return gfx::DeviceResetReason::INVALID_CALL;
    default:
      gfxCriticalNote << "Device reset with D3D11Device unexpected reason: "
                      << gfx::hexa(aError);
      break;
  }
  return gfx::DeviceResetReason::UNKNOWN;
}

static const GUID sD3D11TextureUsage = {
    0xd89275b0,
    0x6c7d,
    0x4038,
    {0xb5, 0xfa, 0x4d, 0x87, 0x16, 0xd5, 0xcc, 0x4e}};

/* This class gets its lifetime tied to a D3D texture
 * and increments memory usage on construction and decrements
 * on destruction */
class TextureMemoryMeasurer final : public IUnknown {
 public:
  explicit TextureMemoryMeasurer(size_t aMemoryUsed) {
    mMemoryUsed = aMemoryUsed;
    gfxWindowsPlatform::sD3D11SharedTextures += mMemoryUsed;
    mRefCnt = 0;
  }
  STDMETHODIMP_(ULONG) AddRef() {
    mRefCnt++;
    return mRefCnt;
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
    IUnknown* punk = nullptr;
    if (riid == IID_IUnknown) {
      punk = this;
    }
    *ppvObject = punk;
    if (punk) {
      punk->AddRef();
      return S_OK;
    } else {
      return E_NOINTERFACE;
    }
  }

  STDMETHODIMP_(ULONG) Release() {
    int refCnt = --mRefCnt;
    if (refCnt == 0) {
      gfxWindowsPlatform::sD3D11SharedTextures -= mMemoryUsed;
      delete this;
    }
    return refCnt;
  }

 private:
  int mRefCnt;
  int mMemoryUsed;

  ~TextureMemoryMeasurer() = default;
};

static DXGI_FORMAT SurfaceFormatToDXGIFormat(gfx::SurfaceFormat aFormat) {
  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case SurfaceFormat::B8G8R8X8:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case SurfaceFormat::R8G8B8A8:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case SurfaceFormat::R8G8B8X8:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case SurfaceFormat::A8:
      return DXGI_FORMAT_R8_UNORM;
    case SurfaceFormat::A16:
      return DXGI_FORMAT_R16_UNORM;
    default:
      MOZ_ASSERT(false, "unsupported format");
      return DXGI_FORMAT_UNKNOWN;
  }
}

void ReportTextureMemoryUsage(ID3D11Texture2D* aTexture, size_t aBytes) {
  aTexture->SetPrivateDataInterface(sD3D11TextureUsage,
                                    new TextureMemoryMeasurer(aBytes));
}

static uint32_t GetRequiredTilesD3D11(uint32_t aSize, uint32_t aMaxSize) {
  uint32_t requiredTiles = aSize / aMaxSize;
  if (aSize % aMaxSize) {
    requiredTiles++;
  }
  return requiredTiles;
}

static IntRect GetTileRectD3D11(uint32_t aID, IntSize aSize,
                                uint32_t aMaxSize) {
  uint32_t horizontalTiles = GetRequiredTilesD3D11(aSize.width, aMaxSize);
  uint32_t verticalTiles = GetRequiredTilesD3D11(aSize.height, aMaxSize);

  uint32_t verticalTile = aID / horizontalTiles;
  uint32_t horizontalTile = aID % horizontalTiles;

  return IntRect(
      horizontalTile * aMaxSize, verticalTile * aMaxSize,
      horizontalTile < (horizontalTiles - 1) ? aMaxSize
                                             : aSize.width % aMaxSize,
      verticalTile < (verticalTiles - 1) ? aMaxSize : aSize.height % aMaxSize);
}

AutoTextureLock::AutoTextureLock(IDXGIKeyedMutex* aMutex, HRESULT& aResult,
                                 uint32_t aTimeout) {
  mMutex = aMutex;
  if (mMutex) {
    mResult = mMutex->AcquireSync(0, aTimeout);
    aResult = mResult;
  } else {
    aResult = E_INVALIDARG;
  }
}

AutoTextureLock::~AutoTextureLock() {
  if (mMutex && !FAILED(mResult) && mResult != WAIT_TIMEOUT &&
      mResult != WAIT_ABANDONED) {
    mMutex->ReleaseSync(0);
  }
}

ID3D11ShaderResourceView* TextureSourceD3D11::GetShaderResourceView() {
  MOZ_ASSERT(mTexture == GetD3D11Texture(),
             "You need to override GetShaderResourceView if you're overriding "
             "GetD3D11Texture!");

  if (!mSRV && mTexture) {
    RefPtr<ID3D11Device> device;
    mTexture->GetDevice(getter_AddRefs(device));

    // see comment in CompositingRenderTargetD3D11 constructor
    CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(D3D11_SRV_DIMENSION_TEXTURE2D,
                                             mFormatOverride);
    D3D11_SHADER_RESOURCE_VIEW_DESC* desc =
        mFormatOverride == DXGI_FORMAT_UNKNOWN ? nullptr : &srvDesc;

    HRESULT hr =
        device->CreateShaderResourceView(mTexture, desc, getter_AddRefs(mSRV));
    if (FAILED(hr)) {
      gfxCriticalNote << "[D3D11] TextureSourceD3D11:GetShaderResourceView "
                         "CreateSRV failure "
                      << gfx::hexa(hr);
      return nullptr;
    }
  }
  return mSRV;
}

DataTextureSourceD3D11::DataTextureSourceD3D11(ID3D11Device* aDevice,
                                               SurfaceFormat aFormat,
                                               TextureFlags aFlags)
    : mDevice(aDevice),
      mFormat(aFormat),
      mFlags(aFlags),
      mCurrentTile(0),
      mIsTiled(false),
      mIterating(false),
      mAllowTextureUploads(true) {}

DataTextureSourceD3D11::DataTextureSourceD3D11(ID3D11Device* aDevice,
                                               SurfaceFormat aFormat,
                                               ID3D11Texture2D* aTexture)
    : mDevice(aDevice),
      mFormat(aFormat),
      mFlags(TextureFlags::NO_FLAGS),
      mCurrentTile(0),
      mIsTiled(false),
      mIterating(false),
      mAllowTextureUploads(false) {
  mTexture = aTexture;
  D3D11_TEXTURE2D_DESC desc;
  aTexture->GetDesc(&desc);

  mSize = IntSize(desc.Width, desc.Height);
}

DataTextureSourceD3D11::DataTextureSourceD3D11(gfx::SurfaceFormat aFormat,
                                               TextureSourceProvider* aProvider,
                                               ID3D11Texture2D* aTexture)
    : DataTextureSourceD3D11(aProvider->GetD3D11Device(), aFormat, aTexture) {}

DataTextureSourceD3D11::DataTextureSourceD3D11(gfx::SurfaceFormat aFormat,
                                               TextureSourceProvider* aProvider,
                                               TextureFlags aFlags)
    : DataTextureSourceD3D11(aProvider->GetD3D11Device(), aFormat, aFlags) {}

DataTextureSourceD3D11::~DataTextureSourceD3D11() {}

enum class SerializeWithMoz2D : bool { No, Yes };

template <typename T>  // ID3D10Texture2D or ID3D11Texture2D
static bool LockD3DTexture(
    T* aTexture, SerializeWithMoz2D aSerialize = SerializeWithMoz2D::No) {
  MOZ_ASSERT(aTexture);
  RefPtr<IDXGIKeyedMutex> mutex;
  aTexture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mutex));
  // Textures created by the DXVA decoders don't have a mutex for
  // synchronization
  if (mutex) {
    HRESULT hr;
    if (aSerialize == SerializeWithMoz2D::Yes) {
      AutoSerializeWithMoz2D serializeWithMoz2D(BackendType::DIRECT2D1_1);
      hr = mutex->AcquireSync(0, 10000);
    } else {
      hr = mutex->AcquireSync(0, 10000);
    }
    if (hr == WAIT_TIMEOUT) {
      RefPtr<ID3D11Device> device;
      aTexture->GetDevice(getter_AddRefs(device));
      if (!device) {
        gfxCriticalNote << "GFX: D3D11 lock mutex timeout - no device returned";
      } else if (device->GetDeviceRemovedReason() != S_OK) {
        gfxCriticalNote << "GFX: D3D11 lock mutex timeout - device removed";
      } else {
        gfxDevCrash(LogReason::D3DLockTimeout)
            << "D3D lock mutex timeout - device not removed";
      }
    } else if (hr == WAIT_ABANDONED) {
      gfxCriticalNote << "GFX: D3D11 lock mutex abandoned";
    }

    if (FAILED(hr)) {
      NS_WARNING("Failed to lock the texture");
      return false;
    }
  }
  return true;
}

template <typename T>
static bool HasKeyedMutex(T* aTexture) {
  MOZ_ASSERT(aTexture);
  RefPtr<IDXGIKeyedMutex> mutex;
  aTexture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mutex));
  return !!mutex;
}

template <typename T>  // ID3D10Texture2D or ID3D11Texture2D
static void UnlockD3DTexture(
    T* aTexture, SerializeWithMoz2D aSerialize = SerializeWithMoz2D::No) {
  MOZ_ASSERT(aTexture);
  RefPtr<IDXGIKeyedMutex> mutex;
  aTexture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mutex));
  if (mutex) {
    HRESULT hr;
    if (aSerialize == SerializeWithMoz2D::Yes) {
      AutoSerializeWithMoz2D serializeWithMoz2D(BackendType::DIRECT2D1_1);
      hr = mutex->ReleaseSync(0);
    } else {
      hr = mutex->ReleaseSync(0);
    }
    if (FAILED(hr)) {
      NS_WARNING("Failed to unlock the texture");
    }
  }
}

D3D11TextureData::D3D11TextureData(
    ID3D11Device* aDevice, ID3D11Texture2D* aTexture, uint32_t aArrayIndex,
    RefPtr<gfx::FileHandleWrapper> aSharedHandle, gfx::IntSize aSize,
    gfx::SurfaceFormat aFormat,
    const Maybe<CompositeProcessFencesHolderId> aFencesHolderId,
    const RefPtr<FenceD3D11> aWriteFence, TextureAllocationFlags aFlags)
    : mSize(aSize),
      mFormat(aFormat),
      mHasKeyedMutex(HasKeyedMutex(aTexture)),
      mFencesHolderId(aFencesHolderId),
      mWriteFence(aWriteFence),
      mNeedsClear(aFlags & ALLOC_CLEAR_BUFFER),
      mTexture(aTexture),
      mSharedHandle(std::move(aSharedHandle)),
      mArrayIndex(aArrayIndex),
      mAllocationFlags(aFlags) {
  MOZ_ASSERT(aTexture);
}

static void DestroyDrawTarget(RefPtr<DrawTarget>& aDT,
                              RefPtr<ID3D11Texture2D>& aTexture) {
  // An Azure DrawTarget needs to be locked when it gets nullptr'ed as this is
  // when it calls EndDraw. This EndDraw should not execute anything so it
  // shouldn't -really- need the lock but the debug layer chokes on this.
  LockD3DTexture(aTexture.get(), SerializeWithMoz2D::Yes);
  aDT = nullptr;

  // Do the serialization here, so we can hold it while destroying the texture.
  AutoSerializeWithMoz2D serializeWithMoz2D(BackendType::DIRECT2D1_1);
  UnlockD3DTexture(aTexture.get(), SerializeWithMoz2D::No);
  aTexture = nullptr;
}

D3D11TextureData::~D3D11TextureData() {
  if (mDrawTarget) {
    DestroyDrawTarget(mDrawTarget, mTexture);
  }

  if (mGpuProcessTextureId.isSome()) {
    auto* textureMap = GpuProcessD3D11TextureMap::Get();
    if (textureMap) {
      textureMap->Unregister(mGpuProcessTextureId.ref());
    } else {
      gfxCriticalNoteOnce << "GpuProcessD3D11TextureMap does not exist";
    }
  }
  if (mFencesHolderId.isSome()) {
    auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
    if (fencesHolderMap) {
      fencesHolderMap->Unregister(mFencesHolderId.ref());
      gfxCriticalNoteOnce
          << "CompositeProcessD3D11FencesHolderMap does not exist";
    }
  }
}

bool D3D11TextureData::Lock(OpenMode aMode) {
  if (mFencesHolderId.isSome()) {
    auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
    fencesHolderMap->WaitAllFencesAndForget(mFencesHolderId.ref(), mDevice);
  }

  if (mHasKeyedMutex &&
      !LockD3DTexture(mTexture.get(), SerializeWithMoz2D::Yes)) {
    return false;
  }

  if (NS_IsMainThread()) {
    if (!PrepareDrawTargetInLock(aMode)) {
      Unlock();
      return false;
    }
  }

  return true;
}

bool D3D11TextureData::PrepareDrawTargetInLock(OpenMode aMode) {
  // Make sure that successful write-lock means we will have a DrawTarget to
  // write into.
  if (!mDrawTarget && (aMode & OpenMode::OPEN_WRITE || mNeedsClear)) {
    mDrawTarget = BorrowDrawTarget();
    if (!mDrawTarget) {
      return false;
    }
  }

  // Reset transform
  mDrawTarget->SetTransform(Matrix());

  if (mNeedsClear) {
    mDrawTarget->ClearRect(Rect(0, 0, mSize.width, mSize.height));
    mNeedsClear = false;
  }

  return true;
}

void D3D11TextureData::Unlock() {
  if (mFencesHolderId.isSome()) {
    auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
    fencesHolderMap->SetWriteFence(mFencesHolderId.ref(), mWriteFence);
  }
  if (mHasKeyedMutex) {
    UnlockD3DTexture(mTexture.get(), SerializeWithMoz2D::Yes);
  }
}

void D3D11TextureData::FillInfo(TextureData::Info& aInfo) const {
  aInfo.size = mSize;
  aInfo.format = mFormat;
  aInfo.supportsMoz2D = true;
  aInfo.hasSynchronization = mHasKeyedMutex;
}

void D3D11TextureData::SyncWithObject(RefPtr<SyncObjectClient> aSyncObject) {
  if (!aSyncObject || mHasKeyedMutex) {
    // When we have per texture synchronization we sync using the keyed mutex.
    return;
  }

  MOZ_ASSERT(aSyncObject->GetSyncType() == SyncObjectClient::SyncType::D3D11);
  SyncObjectD3D11Client* sync =
      static_cast<SyncObjectD3D11Client*>(aSyncObject.get());
  sync->RegisterTexture(mTexture);
}

bool D3D11TextureData::SerializeSpecific(
    SurfaceDescriptorD3D10* const aOutDesc) {
  *aOutDesc = SurfaceDescriptorD3D10(
      mSharedHandle, mGpuProcessTextureId, mArrayIndex, mFormat, mSize,
      mColorSpace, mColorRange, mHasKeyedMutex, mFencesHolderId);
  return true;
}

bool D3D11TextureData::Serialize(SurfaceDescriptor& aOutDescriptor) {
  SurfaceDescriptorD3D10 desc;
  if (!SerializeSpecific(&desc)) return false;

  aOutDescriptor = std::move(desc);
  return true;
}

void D3D11TextureData::GetSubDescriptor(
    RemoteDecoderVideoSubDescriptor* const aOutDesc) {
  SurfaceDescriptorD3D10 ret;
  if (!SerializeSpecific(&ret)) return;

  *aOutDesc = std::move(ret);
}

/* static */
already_AddRefed<TextureClient> D3D11TextureData::CreateTextureClient(
    ID3D11Texture2D* aTexture, uint32_t aIndex, gfx::IntSize aSize,
    gfx::SurfaceFormat aFormat, gfx::ColorSpace2 aColorSpace,
    gfx::ColorRange aColorRange, KnowsCompositor* aKnowsCompositor,
    RefPtr<ZeroCopyUsageInfo> aUsageInfo,
    const RefPtr<FenceD3D11> aWriteFence) {
  MOZ_ASSERT(aTexture);

  RefPtr<ID3D11Device> device;
  aTexture->GetDevice(getter_AddRefs(device));

  Maybe<CompositeProcessFencesHolderId> fencesHolderId;
  if (aWriteFence) {
    auto* fencesHolderMap = layers::CompositeProcessD3D11FencesHolderMap::Get();
    fencesHolderId = Some(CompositeProcessFencesHolderId::GetNext());
    fencesHolderMap->Register(fencesHolderId.ref());
  }

  D3D11TextureData* data = new D3D11TextureData(
      device, aTexture, aIndex, nullptr, aSize, aFormat, fencesHolderId,
      aWriteFence, TextureAllocationFlags::ALLOC_MANUAL_SYNCHRONIZATION);
  data->mColorSpace = aColorSpace;
  data->SetColorRange(aColorRange);

  RefPtr<TextureClient> textureClient = MakeAndAddRef<TextureClient>(
      data, TextureFlags::NO_FLAGS, aKnowsCompositor->GetTextureForwarder());
  const auto textureId = GpuProcessD3D11TextureMap::GetNextTextureId();
  data->SetGpuProcessTextureId(textureId);

  // Register ID3D11Texture2D to GpuProcessD3D11TextureMap
  auto* textureMap = GpuProcessD3D11TextureMap::Get();
  if (textureMap) {
    textureMap->Register(textureId, aTexture, aIndex, aSize, aUsageInfo);
  } else {
    gfxCriticalNoteOnce << "GpuProcessD3D11TextureMap does not exist";
  }

  return textureClient.forget();
}

D3D11TextureData* D3D11TextureData::Create(IntSize aSize, SurfaceFormat aFormat,
                                           TextureAllocationFlags aFlags,
                                           ID3D11Device* aDevice) {
  return Create(aSize, aFormat, nullptr, aFlags, aDevice);
}

D3D11TextureData* D3D11TextureData::Create(SourceSurface* aSurface,
                                           TextureAllocationFlags aFlags,
                                           ID3D11Device* aDevice) {
  return Create(aSurface->GetSize(), aSurface->GetFormat(), aSurface, aFlags,
                aDevice);
}

D3D11TextureData* D3D11TextureData::Create(IntSize aSize, SurfaceFormat aFormat,
                                           SourceSurface* aSurface,
                                           TextureAllocationFlags aFlags,
                                           ID3D11Device* aDevice) {
  if (aFormat == SurfaceFormat::A8) {
    // Currently we don't support A8 surfaces. Fallback.
    return nullptr;
  }

  // Just grab any device. We never use the immediate context, so the devices
  // are fine to use from any thread.
  RefPtr<ID3D11Device> device = aDevice;
  if (!device) {
    device = DeviceManagerDx::Get()->GetContentDevice();
    if (!device) {
      return nullptr;
    }
  }

  CD3D11_TEXTURE2D_DESC newDesc(
      DXGI_FORMAT_B8G8R8A8_UNORM, aSize.width, aSize.height, 1, 1,
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

  if (aFormat == SurfaceFormat::NV12) {
    newDesc.Format = DXGI_FORMAT_NV12;
  } else if (aFormat == SurfaceFormat::P010) {
    newDesc.Format = DXGI_FORMAT_P010;
  } else if (aFormat == SurfaceFormat::P016) {
    newDesc.Format = DXGI_FORMAT_P016;
  }

  newDesc.MiscFlags =
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
  bool useFence = false;
  bool useKeyedMutex = false;
  if (!NS_IsMainThread()) {
    // On the main thread we use the syncobject to handle synchronization.
    if (!(aFlags & ALLOC_MANUAL_SYNCHRONIZATION)) {
      auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
      useFence = fencesHolderMap && FenceD3D11::IsSupported(device);
      if (!useFence) {
        newDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                            D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        useKeyedMutex = true;
      }
    }
  }

  Maybe<CompositeProcessFencesHolderId> fencesHolderId;
  RefPtr<FenceD3D11> fence;
  if (useFence) {
    fence = FenceD3D11::Create(device);
    if (!fence) {
      return nullptr;
    }
    fencesHolderId = Some(CompositeProcessFencesHolderId::GetNext());
  }

  if (aSurface && useKeyedMutex &&
      !DeviceManagerDx::Get()->CanInitializeKeyedMutexTextures()) {
    return nullptr;
  }

  D3D11_SUBRESOURCE_DATA uploadData;
  D3D11_SUBRESOURCE_DATA* uploadDataPtr = nullptr;
  RefPtr<DataSourceSurface> srcSurf;

  if (aSurface) {
    srcSurf = aSurface->GetDataSurface();

    if (!srcSurf) {
      gfxCriticalError()
          << "Failed to GetDataSurface in D3D11TextureData::Create";
      return nullptr;
    }

    DataSourceSurface::MappedSurface sourceMap;
    if (!srcSurf->Map(DataSourceSurface::READ, &sourceMap)) {
      gfxCriticalError()
          << "Failed to map source surface for D3D11TextureData::Create";
      return nullptr;
    }

    uploadData.pSysMem = sourceMap.mData;
    uploadData.SysMemPitch = sourceMap.mStride;
    uploadData.SysMemSlicePitch = 0;  // unused

    uploadDataPtr = &uploadData;
  }

  // See bug 1397040
  RefPtr<ID3D10Multithread> mt;
  device->QueryInterface((ID3D10Multithread**)getter_AddRefs(mt));

  RefPtr<ID3D11Texture2D> texture11;

  {
    AutoSerializeWithMoz2D serializeWithMoz2D(BackendType::DIRECT2D1_1);
    D3D11MTAutoEnter lock(mt.forget());

    HRESULT hr = device->CreateTexture2D(&newDesc, uploadDataPtr,
                                         getter_AddRefs(texture11));

    if (FAILED(hr) || !texture11) {
      gfxCriticalNote << "[D3D11] 2 CreateTexture2D failure Size: " << aSize
                      << "texture11: " << texture11
                      << " Code: " << gfx::hexa(hr);
      return nullptr;
    }
  }

  if (srcSurf) {
    srcSurf->Unmap();
  }

  // If we created the texture with a keyed mutex, then we expect all operations
  // on it to be synchronized using it. If we did an initial upload using
  // aSurface then bizarely this isn't covered, so we insert a manual
  // lock/unlock pair to force this.
  if (aSurface && useKeyedMutex) {
    if (!LockD3DTexture(texture11.get(), SerializeWithMoz2D::Yes)) {
      return nullptr;
    }
    UnlockD3DTexture(texture11.get(), SerializeWithMoz2D::Yes);
  }

  RefPtr<IDXGIResource1> resource;
  texture11->QueryInterface((IDXGIResource1**)getter_AddRefs(resource));
  if (!resource) {
    gfxCriticalNoteOnce << "Failed to get IDXGIResource";
    return nullptr;
  }

  HANDLE sharedHandle;
  HRESULT hr = resource->GetSharedHandle(&sharedHandle);
  hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &sharedHandle);
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "GetSharedHandle failed: " << gfx::hexa(hr);
    return nullptr;
  }

  texture11->SetPrivateDataInterface(
      sD3D11TextureUsage,
      new TextureMemoryMeasurer(newDesc.Width * newDesc.Height * 4));

  RefPtr<gfx::FileHandleWrapper> handle =
      new gfx::FileHandleWrapper(UniqueFileHandle(sharedHandle));

  if (useFence) {
    auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
    fencesHolderMap->Register(fencesHolderId.ref());
  }

  D3D11TextureData* data =
      new D3D11TextureData(device, texture11, 0, handle, aSize, aFormat,
                           fencesHolderId, fence, aFlags);

  texture11->GetDevice(getter_AddRefs(device));
  if (XRE_IsGPUProcess() &&
      device == gfx::DeviceManagerDx::Get()->GetCompositorDevice()) {
    const auto textureId = GpuProcessD3D11TextureMap::GetNextTextureId();
    data->SetGpuProcessTextureId(textureId);
    // Register ID3D11Texture2D to GpuProcessD3D11TextureMap
    auto* textureMap = GpuProcessD3D11TextureMap::Get();
    if (textureMap) {
      textureMap->Register(textureId, texture11, 0, aSize, nullptr, handle);
    } else {
      gfxCriticalNoteOnce << "GpuProcessD3D11TextureMap does not exist";
    }
  }

  return data;
}

void D3D11TextureData::Deallocate(LayersIPCChannel* aAllocator) {
  mDrawTarget = nullptr;
  mTexture = nullptr;
}

TextureData* D3D11TextureData::CreateSimilar(
    LayersIPCChannel* aAllocator, LayersBackend aLayersBackend,
    TextureFlags aFlags, TextureAllocationFlags aAllocFlags) const {
  return D3D11TextureData::Create(mSize, mFormat, aAllocFlags);
}

TextureFlags D3D11TextureData::GetTextureFlags() const {
  // With WebRender, resource open happens asynchronously on RenderThread.
  // During opening the resource on host side, TextureClient needs to be alive.
  // With WAIT_HOST_USAGE_END, keep TextureClient alive during host side usage.
  return TextureFlags::WAIT_HOST_USAGE_END;
}

void D3D11TextureData::IncrementAndSignalWriteFence() {
  if (!mFencesHolderId.isNothing() || !mWriteFence) {
    return;
  }
  auto* fencesHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
  if (!fencesHolderMap) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  mWriteFence->IncrementAndSignal();
  fencesHolderMap->SetWriteFence(mFencesHolderId.ref(), mWriteFence);
}

DXGIYCbCrTextureData* DXGIYCbCrTextureData::Create(
    ID3D11Texture2D* aTextureY, ID3D11Texture2D* aTextureCb,
    ID3D11Texture2D* aTextureCr, const gfx::IntSize& aSize,
    const gfx::IntSize& aSizeY, const gfx::IntSize& aSizeCbCr,
    const gfx::ColorDepth aColorDepth, const YUVColorSpace aYUVColorSpace,
    const gfx::ColorRange aColorRange) {
  if (!aTextureY || !aTextureCb || !aTextureCr) {
    return nullptr;
  }

  aTextureY->SetPrivateDataInterface(
      sD3D11TextureUsage,
      new TextureMemoryMeasurer(aSizeY.width * aSizeY.height));
  aTextureCb->SetPrivateDataInterface(
      sD3D11TextureUsage,
      new TextureMemoryMeasurer(aSizeCbCr.width * aSizeCbCr.height));
  aTextureCr->SetPrivateDataInterface(
      sD3D11TextureUsage,
      new TextureMemoryMeasurer(aSizeCbCr.width * aSizeCbCr.height));

  RefPtr<IDXGIResource1> resource;

  aTextureY->QueryInterface((IDXGIResource1**)getter_AddRefs(resource));

  HANDLE handleY;
  HRESULT hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &handleY);
  if (FAILED(hr)) {
    return nullptr;
  }
  const RefPtr<gfx::FileHandleWrapper> sharedHandleY =
      new gfx::FileHandleWrapper(UniqueFileHandle(handleY));

  aTextureCb->QueryInterface((IDXGIResource1**)getter_AddRefs(resource));

  HANDLE handleCb;
  hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &handleCb);
  if (FAILED(hr)) {
    return nullptr;
  }
  const RefPtr<gfx::FileHandleWrapper> sharedHandleCb =
      new gfx::FileHandleWrapper(UniqueFileHandle(handleCb));

  aTextureCr->QueryInterface((IDXGIResource1**)getter_AddRefs(resource));
  HANDLE handleCr;
  hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &handleCr);
  if (FAILED(hr)) {
    return nullptr;
  }
  const RefPtr<gfx::FileHandleWrapper> sharedHandleCr =
      new gfx::FileHandleWrapper(UniqueFileHandle(handleCr));

  auto* fenceHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
  if (!fenceHolderMap) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  RefPtr<ID3D11Device> device;
  aTextureY->GetDevice(getter_AddRefs(device));
  if (!device) {
    return nullptr;
  }

  RefPtr<FenceD3D11> fence = FenceD3D11::Create(device);
  if (!fence) {
    return nullptr;
  }

  auto fencesHolderId = CompositeProcessFencesHolderId::GetNext();
  fenceHolderMap->Register(fencesHolderId);

  RefPtr<ID3D11Texture2D> textures[3] = {aTextureY, aTextureCb, aTextureCr};
  RefPtr<gfx::FileHandleWrapper> handles[3] = {sharedHandleY, sharedHandleCb,
                                               sharedHandleCr};

  DXGIYCbCrTextureData* texture = new DXGIYCbCrTextureData(
      textures, handles, aSize, aSizeY, aSizeCbCr, aColorDepth, aYUVColorSpace,
      aColorRange, fencesHolderId, fence);
  return texture;
}

DXGIYCbCrTextureData::DXGIYCbCrTextureData(
    RefPtr<ID3D11Texture2D> (&aD3D11Textures)[3],
    RefPtr<gfx::FileHandleWrapper>(aHandles)[3], const gfx::IntSize& aSize,
    const gfx::IntSize& aSizeY, const gfx::IntSize& aSizeCbCr,
    const gfx::ColorDepth aColorDepth, const gfx::YUVColorSpace aYUVColorSpace,
    const gfx::ColorRange aColorRange,
    const CompositeProcessFencesHolderId aFencesHolderId,
    const RefPtr<FenceD3D11> aWriteFence)
    : mSize(aSize),
      mSizeY(aSizeY),
      mSizeCbCr(aSizeCbCr),
      mColorDepth(aColorDepth),
      mYUVColorSpace(aYUVColorSpace),
      mColorRange(aColorRange),
      mFencesHolderId(aFencesHolderId),
      mWriteFence(aWriteFence),
      mD3D11Textures{aD3D11Textures[0], aD3D11Textures[1], aD3D11Textures[2]},
      mHandles{aHandles[0], aHandles[1], aHandles[2]} {}

DXGIYCbCrTextureData::~DXGIYCbCrTextureData() {
  auto* fenceHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
  if (!fenceHolderMap) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  fenceHolderMap->Unregister(mFencesHolderId);
}

void DXGIYCbCrTextureData::FillInfo(TextureData::Info& aInfo) const {
  aInfo.size = mSize;
  aInfo.format = gfx::SurfaceFormat::YUV420;
  aInfo.supportsMoz2D = false;
  aInfo.hasSynchronization = false;
}

void DXGIYCbCrTextureData::SerializeSpecific(
    SurfaceDescriptorDXGIYCbCr* const aOutDesc) {
  *aOutDesc = SurfaceDescriptorDXGIYCbCr(
      mHandles[0], mHandles[1], mHandles[2], mSize, mSizeY, mSizeCbCr,
      mColorDepth, mYUVColorSpace, mColorRange, mFencesHolderId);
}

bool DXGIYCbCrTextureData::Serialize(SurfaceDescriptor& aOutDescriptor) {
  SurfaceDescriptorDXGIYCbCr desc;
  SerializeSpecific(&desc);

  aOutDescriptor = std::move(desc);
  return true;
}

void DXGIYCbCrTextureData::GetSubDescriptor(
    RemoteDecoderVideoSubDescriptor* const aOutDesc) {
  SurfaceDescriptorDXGIYCbCr desc;
  SerializeSpecific(&desc);

  *aOutDesc = std::move(desc);
}

void DXGIYCbCrTextureData::Deallocate(LayersIPCChannel*) {
  mD3D11Textures[0] = nullptr;
  mD3D11Textures[1] = nullptr;
  mD3D11Textures[2] = nullptr;
}

TextureFlags DXGIYCbCrTextureData::GetTextureFlags() const {
  // With WebRender, resource open happens asynchronously on RenderThread.
  // During opening the resource on host side, TextureClient needs to be alive.
  // With WAIT_HOST_USAGE_END, keep TextureClient alive during host side usage.
  return TextureFlags::WAIT_HOST_USAGE_END;
}

already_AddRefed<TextureHost> CreateTextureHostD3D11(
    const SurfaceDescriptor& aDesc, ISurfaceAllocator* aDeallocator,
    LayersBackend aBackend, TextureFlags aFlags) {
  RefPtr<TextureHost> result;
  switch (aDesc.type()) {
    case SurfaceDescriptor::TSurfaceDescriptorD3D10: {
      result =
          new DXGITextureHostD3D11(aFlags, aDesc.get_SurfaceDescriptorD3D10());
      break;
    }
    case SurfaceDescriptor::TSurfaceDescriptorDXGIYCbCr: {
      result = new DXGIYCbCrTextureHostD3D11(
          aFlags, aDesc.get_SurfaceDescriptorDXGIYCbCr());
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("Unsupported SurfaceDescriptor type");
    }
  }
  return result.forget();
}

already_AddRefed<DrawTarget> D3D11TextureData::BorrowDrawTarget() {
  MOZ_ASSERT(NS_IsMainThread() || NS_IsInCanvasThreadOrWorker());

  if (!mDrawTarget && mTexture) {
    // This may return a null DrawTarget
    mDrawTarget = Factory::CreateDrawTargetForD3D11Texture(mTexture, mFormat);
    if (!mDrawTarget) {
      gfxCriticalNote << "Could not borrow DrawTarget (D3D11) " << (int)mFormat;
    }
  }

  RefPtr<DrawTarget> result = mDrawTarget;
  return result.forget();
}

bool D3D11TextureData::UpdateFromSurface(gfx::SourceSurface* aSurface) {
  // Supporting texture updates after creation requires an ID3D11DeviceContext
  // and those aren't threadsafe. We'd need to either lock, or have a device for
  // whatever thread this runs on and we're trying to avoid extra devices (bug
  // 1284672).
  MOZ_ASSERT(false,
             "UpdateFromSurface not supported for D3D11! Use CreateFromSurface "
             "instead");
  return false;
}

static RefPtr<ID3D11Texture2D> OpenSharedD3D11Texture(
    ID3D11Device* const aDevice, const HANDLE handle) {
  MOZ_ASSERT(aDevice);

  RefPtr<ID3D11Device1> device1;
  aDevice->QueryInterface((ID3D11Device1**)getter_AddRefs(device1));
  if (!device1) {
    gfxCriticalNoteOnce << "Failed to get ID3D11Device1";
    return nullptr;
  }

  RefPtr<ID3D11Texture2D> tex;
  auto hr = device1->OpenSharedResource1(
      (HANDLE)handle, __uuidof(ID3D11Texture2D),
      (void**)(ID3D11Texture2D**)getter_AddRefs(tex));
  if (FAILED(hr)) {
    gfxCriticalNote << "Error code from OpenSharedResource1: " << gfx::hexa(hr);
    return nullptr;
  }

  return tex;
}

static RefPtr<ID3D11Texture2D> OpenSharedD3D11Texture(
    DXGITextureHostD3D11* aTextureHost, ID3D11Device* const aDevice) {
  MOZ_ASSERT(aDevice);
  MOZ_ASSERT(aTextureHost);

  const auto& handle = aTextureHost->mHandle;
  const auto& gpuProcessTextureId = aTextureHost->mGpuProcessTextureId;

  RefPtr<ID3D11Texture2D> texture;
  if (gpuProcessTextureId.isSome()) {
    auto* textureMap = GpuProcessD3D11TextureMap::Get();
    if (textureMap) {
      texture = textureMap->GetTexture(gpuProcessTextureId.ref());
    }
  } else if (handle) {
    texture = OpenSharedD3D11Texture(aDevice, handle->GetHandle());
  }

  if (!texture) {
    return nullptr;
  }
  return texture;
}

DXGITextureHostD3D11::DXGITextureHostD3D11(
    TextureFlags aFlags, const SurfaceDescriptorD3D10& aDescriptor)
    : TextureHost(TextureHostType::DXGI, aFlags),
      mHandle(aDescriptor.handle()),
      mGpuProcessTextureId(aDescriptor.gpuProcessTextureId()),
      mArrayIndex(aDescriptor.arrayIndex()),
      mSize(aDescriptor.size()),
      mFormat(aDescriptor.format()),
      mHasKeyedMutex(aDescriptor.hasKeyedMutex()),
      mFencesHolderId(aDescriptor.fencesHolderId()),
      mColorSpace(aDescriptor.colorSpace()),
      mColorRange(aDescriptor.colorRange()) {}

already_AddRefed<gfx::DataSourceSurface> DXGITextureHostD3D11::GetAsSurface(
    gfx::DataSourceSurface* aSurface) {
  RefPtr<ID3D11Device> d3d11Device =
      DeviceManagerDx::Get()->GetCompositorDevice();
  if (!d3d11Device) {
    return nullptr;
  }

  RefPtr<ID3D11Texture2D> d3dTexture =
      OpenSharedD3D11Texture(this, d3d11Device);
  if (!d3dTexture) {
    return nullptr;
  }

  bool isLocked = LockD3DTexture(d3dTexture.get());
  if (!isLocked) {
    return nullptr;
  }

  const auto onExit =
      mozilla::MakeScopeExit([&]() { UnlockD3DTexture(d3dTexture.get()); });

  bool isRGB = [&]() {
    switch (mFormat) {
      case gfx::SurfaceFormat::R8G8B8X8:
      case gfx::SurfaceFormat::R8G8B8A8:
      case gfx::SurfaceFormat::B8G8R8A8:
      case gfx::SurfaceFormat::B8G8R8X8:
        return true;
      default:
        break;
    }
    return false;
  }();

  if (!isRGB) {
    return nullptr;
  }

  D3D11_TEXTURE2D_DESC textureDesc = {0};
  d3dTexture->GetDesc(&textureDesc);

  RefPtr<ID3D11DeviceContext> context;
  d3d11Device->GetImmediateContext(getter_AddRefs(context));

  textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  textureDesc.Usage = D3D11_USAGE_STAGING;
  textureDesc.BindFlags = 0;
  textureDesc.MiscFlags = 0;
  textureDesc.MipLevels = 1;
  RefPtr<ID3D11Texture2D> cpuTexture;
  HRESULT hr = d3d11Device->CreateTexture2D(&textureDesc, nullptr,
                                            getter_AddRefs(cpuTexture));
  if (FAILED(hr)) {
    return nullptr;
  }

  context->CopyResource(cpuTexture, d3dTexture);

  D3D11_MAPPED_SUBRESOURCE mappedSubresource;
  hr = context->Map(cpuTexture, 0, D3D11_MAP_READ, 0, &mappedSubresource);
  if (FAILED(hr)) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> surf = gfx::CreateDataSourceSurfaceFromData(
      IntSize(textureDesc.Width, textureDesc.Height), GetFormat(),
      (uint8_t*)mappedSubresource.pData, mappedSubresource.RowPitch);
  context->Unmap(cpuTexture, 0);
  return surf.forget();
}

already_AddRefed<gfx::DataSourceSurface>
DXGITextureHostD3D11::GetAsSurfaceWithDevice(
    ID3D11Device* const aDevice,
    DataMutex<RefPtr<VideoProcessorD3D11>>& aVideoProcessorD3D11) {
  if (!aDevice) {
    return nullptr;
  }

  RefPtr<ID3D11Texture2D> d3dTexture = OpenSharedD3D11Texture(this, aDevice);
  if (!d3dTexture) {
    return nullptr;
  }

  RefPtr<ID3D11Device> device;
  d3dTexture->GetDevice(getter_AddRefs(device));
  if (!device) {
    gfxCriticalNoteOnce << "Failed to get D3D11 device from source texture";
    return nullptr;
  }

  if (mFencesHolderId.isSome()) {
    auto* fencesHolderMap = layers::CompositeProcessD3D11FencesHolderMap::Get();
    MOZ_ASSERT(fencesHolderMap);
    if (!fencesHolderMap) {
      return nullptr;
    }
    fencesHolderMap->WaitWriteFence(mFencesHolderId.ref(), device);
  } else {
    bool isLocked = LockD3DTexture(d3dTexture.get());
    if (!isLocked) {
      return nullptr;
    }
  }

  const auto onExit = mozilla::MakeScopeExit([&]() {
    if (mFencesHolderId.isSome()) {
      return;
    }
    UnlockD3DTexture(d3dTexture.get());
  });

  bool isRGB = [&]() {
    switch (mFormat) {
      case gfx::SurfaceFormat::R8G8B8X8:
      case gfx::SurfaceFormat::R8G8B8A8:
      case gfx::SurfaceFormat::B8G8R8A8:
      case gfx::SurfaceFormat::B8G8R8X8:
        return true;
      default:
        break;
    }
    return false;
  }();

  if (isRGB) {
    RefPtr<gfx::DrawTarget> dt =
        gfx::Factory::CreateDrawTargetForD3D11Texture(d3dTexture, mFormat);
    if (!dt) {
      return nullptr;
    }
    RefPtr<gfx::SourceSurface> surface = dt->Snapshot();
    if (!surface) {
      return nullptr;
    }
    RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
    if (!dataSurface) {
      return nullptr;
    }
    return dataSurface.forget();
  }

  if (mFormat != gfx::SurfaceFormat::NV12 &&
      mFormat != gfx::SurfaceFormat::P010 &&
      mFormat != gfx::SurfaceFormat::P016) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  RefPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(getter_AddRefs(context));

  CD3D11_TEXTURE2D_DESC desc;
  d3dTexture->GetDesc(&desc);

  desc = CD3D11_TEXTURE2D_DESC(
      DXGI_FORMAT_B8G8R8A8_UNORM, desc.Width, desc.Height, 1, 1,
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
  desc.MiscFlags =
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

  RefPtr<ID3D11Texture2D> copiedTexture;
  HRESULT hr =
      device->CreateTexture2D(&desc, nullptr, getter_AddRefs(copiedTexture));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "Failed to create copiedTexture: " << gfx::hexa(hr);
    return nullptr;
  }

  {
    auto lock = aVideoProcessorD3D11.Lock();
    auto& videoProcessor = lock.ref();
    if (videoProcessor && (videoProcessor->mDevice != device)) {
      videoProcessor = nullptr;
    }

    if (!videoProcessor) {
      videoProcessor = VideoProcessorD3D11::Create(device);
      if (!videoProcessor) {
        gfxCriticalNoteOnce << "Failed to create VideoProcessorD3D11";
        return nullptr;
      }
    }

    hr = videoProcessor->Init(mSize);
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to init VideoProcessorD3D11"
                          << gfx::hexa(hr);
      return nullptr;
    }

    VideoProcessorD3D11::InputTextureInfo info(mColorSpace, mColorRange,
                                               mArrayIndex, d3dTexture);
    if (!videoProcessor->CallVideoProcessorBlt(info, copiedTexture)) {
      gfxCriticalNoteOnce << "CallVideoProcessorBlt failed";
      return nullptr;
    }
  }

  {
    // Wait VideoProcessorBlt gpu task complete.
    RefPtr<ID3D11Query> query;
    CD3D11_QUERY_DESC desc(D3D11_QUERY_EVENT);
    hr = device->CreateQuery(&desc, getter_AddRefs(query));
    if (FAILED(hr) || !query) {
      gfxWarning() << "Could not create D3D11_QUERY_EVENT: " << gfx::hexa(hr);
      return nullptr;
    }

    context->End(query);

    BOOL result;
    bool ret = WaitForFrameGPUQuery(device, context, query, &result);
    if (!ret) {
      gfxCriticalNoteOnce << "WaitForFrameGPUQuery() failed";
    }
  }

  RefPtr<IDXGIResource1> resource;
  copiedTexture->QueryInterface((IDXGIResource1**)getter_AddRefs(resource));
  if (!resource) {
    gfxCriticalNoteOnce << "Failed to get IDXGIResource";
    return nullptr;
  }

  HANDLE sharedHandle;
  hr = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &sharedHandle);
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "GetSharedHandle failed: " << gfx::hexa(hr);
    return nullptr;
  }

  RefPtr<gfx::FileHandleWrapper> handle =
      new gfx::FileHandleWrapper(UniqueFileHandle(sharedHandle));

  d3dTexture = OpenSharedD3D11Texture(aDevice, handle->GetHandle());
  if (!d3dTexture) {
    gfxCriticalNoteOnce << "Failed to open copied texture handle";
    return nullptr;
  }

  RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateDrawTargetForD3D11Texture(
      d3dTexture, gfx::SurfaceFormat::B8G8R8A8);
  if (!dt) {
    gfxCriticalNote << "Failed to create DrawTarget (D3D11)";
    return nullptr;
  }
  RefPtr<gfx::SourceSurface> surface = dt->Snapshot();
  if (!surface) {
    return nullptr;
  }
  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    return nullptr;
  }

  return dataSurface.forget();
}

void DXGITextureHostD3D11::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr<wr::RenderDXGITextureHost> texture = new wr::RenderDXGITextureHost(
      mHandle, mGpuProcessTextureId, mArrayIndex, mFormat, mColorSpace,
      mColorRange, mSize, mHasKeyedMutex, mFencesHolderId);
  if (mFlags & TextureFlags::SOFTWARE_DECODED_VIDEO) {
    texture->SetIsSoftwareDecodedVideo();
  }
  if (mFlags & TextureFlags::DRM_SOURCE) {
    texture->SetIsFromDRMSource(/* aIsFromDRMSource */ true);
  }
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t DXGITextureHostD3D11::NumSubTextures() {
  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      return 1;
    }
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016: {
      return 2;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected format");
      return 1;
    }
  }
}

void DXGITextureHostD3D11::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  if (!gfx::gfxVars::UseWebRenderANGLE()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called without ANGLE");
    return;
  }

  MOZ_ASSERT(mHandle || mGpuProcessTextureId.isSome());
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;
  switch (mFormat) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      MOZ_ASSERT(aImageKeys.length() == 1);

      wr::ImageDescriptor descriptor(mSize, GetFormat());
      // Prefer TextureExternal unless the backend requires TextureRect.
      TextureHost::NativeTexturePolicy policy =
          TextureHost::BackendNativeTexturePolicy(aResources.GetBackendType(),
                                                  mSize);
      auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                           ? wr::ExternalImageType::TextureHandle(
                                 wr::ImageBufferKind::TextureRect)
                           : wr::ExternalImageType::TextureHandle(
                                 wr::ImageBufferKind::TextureExternal);
      (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016:
    case gfx::SurfaceFormat::NV12: {
      MOZ_ASSERT(aImageKeys.length() == 2);
      MOZ_ASSERT(mSize.width % 2 == 0);
      MOZ_ASSERT(mSize.height % 2 == 0);

      wr::ImageDescriptor descriptor0(mSize, mFormat == gfx::SurfaceFormat::NV12
                                                 ? gfx::SurfaceFormat::A8
                                                 : gfx::SurfaceFormat::A16);
      wr::ImageDescriptor descriptor1(mSize / 2,
                                      mFormat == gfx::SurfaceFormat::NV12
                                          ? gfx::SurfaceFormat::R8G8
                                          : gfx::SurfaceFormat::R16G16);
      // Prefer TextureExternal unless the backend requires TextureRect.
      TextureHost::NativeTexturePolicy policy =
          TextureHost::BackendNativeTexturePolicy(aResources.GetBackendType(),
                                                  mSize);
      auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                           ? wr::ExternalImageType::TextureHandle(
                                 wr::ImageBufferKind::TextureRect)
                           : wr::ExternalImageType::TextureHandle(
                                 wr::ImageBufferKind::TextureExternal);
      (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                           /* aNormalizedUvs */ false);
      (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                           /* aNormalizedUvs */ false);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

void DXGITextureHostD3D11::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  if (!gfx::gfxVars::UseWebRenderANGLE()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called without ANGLE");
    return;
  }

  bool preferExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());
  if (aFlags.contains(PushDisplayItemFlag::EXTERNAL_COMPOSITING_DISABLED)) {
    MOZ_ASSERT(aBuilder.GetBackendType() != WebRenderBackend::SOFTWARE);
    preferExternalCompositing = false;
  }

  switch (GetFormat()) {
    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8: {
      MOZ_ASSERT(aImageKeys.length() == 1);
      aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                         !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                         wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                         preferCompositorSurface, preferExternalCompositing);
      break;
    }
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016:
    case gfx::SurfaceFormat::NV12: {
      // DXGI_FORMAT_P010 stores its 10 bit value in the most significant bits
      // of each 16 bit word with the unused lower bits cleared to zero so that
      // it may be handled as if it was DXGI_FORMAT_P016. This is approximately
      // perceptually correct. However, due to rounding error, the precise
      // quantized value after sampling may be off by 1.
      MOZ_ASSERT(aImageKeys.length() == 2);
      aBuilder.PushNV12Image(
          aBounds, aClip, true, aImageKeys[0], aImageKeys[1],
          GetFormat() == gfx::SurfaceFormat::NV12 ? wr::ColorDepth::Color8
                                                  : wr::ColorDepth::Color16,
          wr::ToWrYuvColorSpace(ToYUVColorSpace(mColorSpace)),
          wr::ToWrColorRange(mColorRange), aFilter, preferCompositorSurface,
          preferExternalCompositing);
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
  }
}

bool DXGITextureHostD3D11::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  if (aBackend == WebRenderBackend::SOFTWARE) {
    return true;
  }
  // XXX Add P010 and P016 support.
  if (GetFormat() == gfx::SurfaceFormat::NV12) {
    if ((mFlags & TextureFlags::SOFTWARE_DECODED_VIDEO) &&
        (gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin())) {
      return true;
    }
    if (!(mFlags & TextureFlags::SOFTWARE_DECODED_VIDEO) &&
        (gfx::gfxVars::UseWebRenderDCompVideoHwOverlayWin())) {
      return true;
    }
  }
  return false;
}

DXGIYCbCrTextureHostD3D11::DXGIYCbCrTextureHostD3D11(
    TextureFlags aFlags, const SurfaceDescriptorDXGIYCbCr& aDescriptor)
    : TextureHost(TextureHostType::DXGIYCbCr, aFlags),
      mSize(aDescriptor.size()),
      mSizeY(aDescriptor.sizeY()),
      mSizeCbCr(aDescriptor.sizeCbCr()),
      mColorDepth(aDescriptor.colorDepth()),
      mYUVColorSpace(aDescriptor.yUVColorSpace()),
      mColorRange(aDescriptor.colorRange()),
      mFencesHolderId(aDescriptor.fencesHolderId()) {
  mHandles[0] = aDescriptor.handleY();
  mHandles[1] = aDescriptor.handleCb();
  mHandles[2] = aDescriptor.handleCr();
}

void DXGIYCbCrTextureHostD3D11::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr<wr::RenderTextureHost> texture = new wr::RenderDXGIYCbCrTextureHost(
      mHandles, mYUVColorSpace, mColorDepth, mColorRange, mSizeY, mSizeCbCr,
      mFencesHolderId);

  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

uint32_t DXGIYCbCrTextureHostD3D11::NumSubTextures() {
  // ycbcr use 3 sub textures.
  return 3;
}

void DXGIYCbCrTextureHostD3D11::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  if (!gfx::gfxVars::UseWebRenderANGLE()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called without ANGLE");
    return;
  }

  MOZ_ASSERT(mHandles[0] && mHandles[1] && mHandles[2]);
  MOZ_ASSERT(aImageKeys.length() == 3);
  // Assume the chroma planes are rounded up if the luma plane is odd sized.
  MOZ_ASSERT((mSizeCbCr.width == mSizeY.width ||
              mSizeCbCr.width == (mSizeY.width + 1) >> 1) &&
             (mSizeCbCr.height == mSizeY.height ||
              mSizeCbCr.height == (mSizeY.height + 1) >> 1));

  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  // Use a size that is the maximum of the Y and CbCr sizes.
  IntSize textureSize = std::max(mSizeY, mSizeCbCr);
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(aResources.GetBackendType(),
                                              textureSize);
  auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                       ? wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureRect)
                       : wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureExternal);

  // y
  wr::ImageDescriptor descriptor0(mSizeY, gfx::SurfaceFormat::A8);
  // cb and cr
  wr::ImageDescriptor descriptor1(mSizeCbCr, gfx::SurfaceFormat::A8);
  (aResources.*method)(aImageKeys[0], descriptor0, aExtID, imageType, 0,
                       /* aNormalizedUvs */ false);
  (aResources.*method)(aImageKeys[1], descriptor1, aExtID, imageType, 1,
                       /* aNormalizedUvs */ false);
  (aResources.*method)(aImageKeys[2], descriptor1, aExtID, imageType, 2,
                       /* aNormalizedUvs */ false);
}

void DXGIYCbCrTextureHostD3D11::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  if (!gfx::gfxVars::UseWebRenderANGLE()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called without ANGLE");
    return;
  }

  MOZ_ASSERT(aImageKeys.length() == 3);

  aBuilder.PushYCbCrPlanarImage(
      aBounds, aClip, true, aImageKeys[0], aImageKeys[1], aImageKeys[2],
      wr::ToWrColorDepth(mColorDepth), wr::ToWrYuvColorSpace(mYUVColorSpace),
      wr::ToWrColorRange(mColorRange), aFilter,
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE),
      SupportsExternalCompositing(aBuilder.GetBackendType()));
}

bool DXGIYCbCrTextureHostD3D11::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

void DXGIYCbCrTextureHostD3D11::NotifyNotUsed() {
  if (!mReadFence) {
    return;
  }

  auto* fenceHolderMap = CompositeProcessD3D11FencesHolderMap::Get();
  if (!fenceHolderMap) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  fenceHolderMap->SetReadFence(mFencesHolderId, mReadFence);
  mReadFence = nullptr;
}

void DXGIYCbCrTextureHostD3D11::SetReadFence(RefPtr<FenceD3D11> aReadFence) {
  MOZ_ASSERT(aReadFence);

  if (!aReadFence) {
    return;
  }

  mReadFence = aReadFence;
}

bool DataTextureSourceD3D11::Update(DataSourceSurface* aSurface,
                                    nsIntRegion* aDestRegion,
                                    IntPoint* aSrcOffset,
                                    IntPoint* aDstOffset) {
  // Incremental update with a source offset is only used on Mac so it is not
  // clear that we ever will need to support it for D3D.
  MOZ_ASSERT(!aSrcOffset);
  MOZ_RELEASE_ASSERT(!aDstOffset);
  MOZ_ASSERT(aSurface);

  MOZ_ASSERT(mAllowTextureUploads);
  if (!mAllowTextureUploads) {
    return false;
  }

  HRESULT hr;

  if (!mDevice) {
    return false;
  }

  uint32_t bpp = BytesPerPixel(aSurface->GetFormat());
  DXGI_FORMAT dxgiFormat = SurfaceFormatToDXGIFormat(aSurface->GetFormat());

  mSize = aSurface->GetSize();
  mFormat = aSurface->GetFormat();

  CD3D11_TEXTURE2D_DESC desc(dxgiFormat, mSize.width, mSize.height, 1, 1);

  int32_t maxSize = GetMaxTextureSizeFromDevice(mDevice);
  if ((mSize.width <= maxSize && mSize.height <= maxSize) ||
      (mFlags & TextureFlags::DISALLOW_BIGIMAGE)) {
    if (mTexture) {
      D3D11_TEXTURE2D_DESC currentDesc;
      mTexture->GetDesc(&currentDesc);

      // Make sure there's no size mismatch, if there is, recreate.
      if (static_cast<int32_t>(currentDesc.Width) != mSize.width ||
          static_cast<int32_t>(currentDesc.Height) != mSize.height ||
          currentDesc.Format != dxgiFormat) {
        mTexture = nullptr;
        // Make sure we upload the whole surface.
        aDestRegion = nullptr;
      }
    }

    nsIntRegion* regionToUpdate = aDestRegion;
    if (!mTexture) {
      hr = mDevice->CreateTexture2D(&desc, nullptr, getter_AddRefs(mTexture));
      mIsTiled = false;
      if (FAILED(hr) || !mTexture) {
        Reset();
        return false;
      }

      if (mFlags & TextureFlags::COMPONENT_ALPHA) {
        regionToUpdate = nullptr;
      }
    }

    DataSourceSurface::MappedSurface map;
    if (!aSurface->Map(DataSourceSurface::MapType::READ, &map)) {
      gfxCriticalError() << "Failed to map surface.";
      Reset();
      return false;
    }

    RefPtr<ID3D11DeviceContext> context;
    mDevice->GetImmediateContext(getter_AddRefs(context));

    if (regionToUpdate) {
      for (auto iter = regionToUpdate->RectIter(); !iter.Done(); iter.Next()) {
        const IntRect& rect = iter.Get();
        D3D11_BOX box;
        box.front = 0;
        box.back = 1;
        box.left = rect.X();
        box.top = rect.Y();
        box.right = rect.XMost();
        box.bottom = rect.YMost();

        void* data = map.mData + map.mStride * rect.Y() +
                     BytesPerPixel(aSurface->GetFormat()) * rect.X();

        context->UpdateSubresource(mTexture, 0, &box, data, map.mStride,
                                   map.mStride * rect.Height());
      }
    } else {
      context->UpdateSubresource(mTexture, 0, nullptr, map.mData, map.mStride,
                                 map.mStride * mSize.height);
    }

    aSurface->Unmap();
  } else {
    mIsTiled = true;
    uint32_t tileCount = GetRequiredTilesD3D11(mSize.width, maxSize) *
                         GetRequiredTilesD3D11(mSize.height, maxSize);

    mTileTextures.resize(tileCount);
    mTileSRVs.resize(tileCount);
    mTexture = nullptr;

    DataSourceSurface::ScopedMap map(aSurface, DataSourceSurface::READ);
    if (!map.IsMapped()) {
      gfxCriticalError() << "Failed to map surface.";
      Reset();
      return false;
    }

    for (uint32_t i = 0; i < tileCount; i++) {
      IntRect tileRect = GetTileRect(i);

      desc.Width = tileRect.Width();
      desc.Height = tileRect.Height();
      desc.Usage = D3D11_USAGE_IMMUTABLE;

      D3D11_SUBRESOURCE_DATA initData;
      initData.pSysMem =
          map.GetData() + tileRect.Y() * map.GetStride() + tileRect.X() * bpp;
      initData.SysMemPitch = map.GetStride();

      hr = mDevice->CreateTexture2D(&desc, &initData,
                                    getter_AddRefs(mTileTextures[i]));
      if (FAILED(hr) || !mTileTextures[i]) {
        Reset();
        return false;
      }
    }
  }
  return true;
}

ID3D11Texture2D* DataTextureSourceD3D11::GetD3D11Texture() const {
  return mIterating ? mTileTextures[mCurrentTile] : mTexture;
}

RefPtr<TextureSource> DataTextureSourceD3D11::ExtractCurrentTile() {
  MOZ_ASSERT(mIterating);
  return new DataTextureSourceD3D11(mDevice, mFormat,
                                    mTileTextures[mCurrentTile]);
}

ID3D11ShaderResourceView* DataTextureSourceD3D11::GetShaderResourceView() {
  if (mIterating) {
    if (!mTileSRVs[mCurrentTile]) {
      if (!mTileTextures[mCurrentTile]) {
        return nullptr;
      }

      RefPtr<ID3D11Device> device;
      mTileTextures[mCurrentTile]->GetDevice(getter_AddRefs(device));
      HRESULT hr = device->CreateShaderResourceView(
          mTileTextures[mCurrentTile], nullptr,
          getter_AddRefs(mTileSRVs[mCurrentTile]));
      if (FAILED(hr)) {
        gfxCriticalNote
            << "[D3D11] DataTextureSourceD3D11:GetShaderResourceView CreateSRV "
               "failure "
            << gfx::hexa(hr);
        return nullptr;
      }
    }
    return mTileSRVs[mCurrentTile];
  }

  return TextureSourceD3D11::GetShaderResourceView();
}

void DataTextureSourceD3D11::Reset() {
  mTexture = nullptr;
  mTileSRVs.resize(0);
  mTileTextures.resize(0);
  mIsTiled = false;
  mSize.width = 0;
  mSize.height = 0;
}

IntRect DataTextureSourceD3D11::GetTileRect(uint32_t aIndex) const {
  return GetTileRectD3D11(aIndex, mSize, GetMaxTextureSizeFromDevice(mDevice));
}

IntRect DataTextureSourceD3D11::GetTileRect() {
  IntRect rect = GetTileRect(mCurrentTile);
  return IntRect(rect.X(), rect.Y(), rect.Width(), rect.Height());
}

CompositingRenderTargetD3D11::CompositingRenderTargetD3D11(
    ID3D11Texture2D* aTexture, const gfx::IntPoint& aOrigin,
    DXGI_FORMAT aFormatOverride)
    : CompositingRenderTarget(aOrigin) {
  MOZ_ASSERT(aTexture);

  mTexture = aTexture;

  RefPtr<ID3D11Device> device;
  mTexture->GetDevice(getter_AddRefs(device));

  mFormatOverride = aFormatOverride;

  // If we happen to have a typeless underlying DXGI surface, we need to be
  // explicit about the format here. (Such a surface could come from an external
  // source, such as the Oculus compositor)
  CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(D3D11_RTV_DIMENSION_TEXTURE2D,
                                         mFormatOverride);
  D3D11_RENDER_TARGET_VIEW_DESC* desc =
      aFormatOverride == DXGI_FORMAT_UNKNOWN ? nullptr : &rtvDesc;

  HRESULT hr =
      device->CreateRenderTargetView(mTexture, desc, getter_AddRefs(mRTView));

  if (FAILED(hr)) {
    LOGD3D11("Failed to create RenderTargetView.");
  }
}

void CompositingRenderTargetD3D11::BindRenderTarget(
    ID3D11DeviceContext* aContext) {
  if (mClearOnBind) {
    FLOAT clear[] = {0, 0, 0, 0};
    aContext->ClearRenderTargetView(mRTView, clear);
    mClearOnBind = false;
  }
  ID3D11RenderTargetView* view = mRTView;
  aContext->OMSetRenderTargets(1, &view, nullptr);
}

IntSize CompositingRenderTargetD3D11::GetSize() const {
  return TextureSourceD3D11::GetSize();
}

static inline bool ShouldDevCrashOnSyncInitFailure() {
  // Compositor shutdown does not wait for video decoding to finish, so it is
  // possible for the compositor to destroy the SyncObject before video has a
  // chance to initialize it.
  if (!NS_IsMainThread()) {
    return false;
  }

  // Note: CompositorIsInGPUProcess is a main-thread-only function.
  return !CompositorBridgeChild::CompositorIsInGPUProcess() &&
         !DeviceManagerDx::Get()->HasDeviceReset();
}

SyncObjectD3D11Host::SyncObjectD3D11Host(ID3D11Device* aDevice)
    : mSyncHandle(nullptr), mDevice(aDevice) {
  MOZ_ASSERT(aDevice);
}

bool SyncObjectD3D11Host::Init() {
  CD3D11_TEXTURE2D_DESC desc(
      DXGI_FORMAT_B8G8R8A8_UNORM, 1, 1, 1, 1,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                   D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  RefPtr<ID3D11Texture2D> texture;
  HRESULT hr =
      mDevice->CreateTexture2D(&desc, nullptr, getter_AddRefs(texture));
  if (FAILED(hr) || !texture) {
    gfxWarning() << "Could not create a sync texture: " << gfx::hexa(hr);
    return false;
  }

  hr = texture->QueryInterface((IDXGIResource1**)getter_AddRefs(mSyncTexture));
  if (FAILED(hr) || !mSyncTexture) {
    gfxWarning() << "Could not QI sync texture: " << gfx::hexa(hr);
    return false;
  }

  hr = mSyncTexture->QueryInterface(
      (IDXGIKeyedMutex**)getter_AddRefs(mKeyedMutex));
  if (FAILED(hr) || !mKeyedMutex) {
    gfxWarning() << "Could not QI keyed-mutex: " << gfx::hexa(hr);
    return false;
  }

  HANDLE sharedHandle;
  hr = mSyncTexture->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &sharedHandle);
  if (FAILED(hr)) {
    gfxWarning() << "Could not get sync texture shared handle: "
                 << gfx::hexa(hr);
    return false;
  }
  mSyncHandle = new gfx::FileHandleWrapper(UniqueFileHandle(sharedHandle));

  return true;
}

SyncHandle SyncObjectD3D11Host::GetSyncHandle() { return mSyncHandle; }

bool SyncObjectD3D11Host::Synchronize(bool aFallible) {
  HRESULT hr;
  AutoTextureLock lock(mKeyedMutex, hr, 10000);

  if (hr == WAIT_TIMEOUT) {
    hr = mDevice->GetDeviceRemovedReason();
    if (hr != S_OK) {
      // Since the timeout is related to the driver-removed. Return false for
      // error handling.
      gfxCriticalNote << "GFX: D3D11 timeout with device-removed:"
                      << gfx::hexa(hr);
    } else if (aFallible) {
      gfxCriticalNote << "GFX: D3D11 timeout on the D3D11 sync lock.";
    } else {
      // There is no driver-removed event. Crash with this timeout.
      MOZ_CRASH("GFX: D3D11 normal status timeout");
    }

    return false;
  }
  if (hr == WAIT_ABANDONED) {
    gfxCriticalNote << "GFX: AL_D3D11 abandoned sync";
  }

  return true;
}

SyncObjectD3D11Client::SyncObjectD3D11Client(SyncHandle aSyncHandle,
                                             ID3D11Device* aDevice)
    : mSyncLock("SyncObjectD3D11"), mSyncHandle(aSyncHandle), mDevice(aDevice) {
  MOZ_ASSERT(aDevice);
}

SyncObjectD3D11Client::SyncObjectD3D11Client(SyncHandle aSyncHandle)
    : mSyncLock("SyncObjectD3D11"), mSyncHandle(aSyncHandle) {}

bool SyncObjectD3D11Client::Init(ID3D11Device* aDevice, bool aFallible) {
  if (mKeyedMutex) {
    return true;
  }

  if (!mSyncHandle) {
    return false;
  }

  RefPtr<ID3D11Device1> device1;
  aDevice->QueryInterface((ID3D11Device1**)getter_AddRefs(device1));
  if (!device1) {
    gfxCriticalNoteOnce << "Failed to get ID3D11Device1";
    return 0;
  }

  HRESULT hr = device1->OpenSharedResource1(
      mSyncHandle->GetHandle(), __uuidof(ID3D11Texture2D),
      (void**)(ID3D11Texture2D**)getter_AddRefs(mSyncTexture));
  if (FAILED(hr) || !mSyncTexture) {
    gfxCriticalNote << "Failed to OpenSharedResource1 for SyncObjectD3D11: "
                    << hexa(hr);
    if (!aFallible && ShouldDevCrashOnSyncInitFailure()) {
      gfxDevCrash(LogReason::D3D11FinalizeFrame)
          << "Without device reset: " << hexa(hr);
    }
    return false;
  }

  hr = mSyncTexture->QueryInterface(__uuidof(IDXGIKeyedMutex),
                                    getter_AddRefs(mKeyedMutex));
  if (FAILED(hr) || !mKeyedMutex) {
    // Leave both the critical error and MOZ_CRASH for now; the critical error
    // lets us "save" the hr value.  We will probably eventually replace this
    // with gfxDevCrash.
    if (!aFallible) {
      gfxCriticalError() << "Failed to get KeyedMutex (2): " << hexa(hr);
      MOZ_CRASH("GFX: Cannot get D3D11 KeyedMutex");
    } else {
      gfxCriticalNote << "Failed to get KeyedMutex (3): " << hexa(hr);
    }
    return false;
  }

  return true;
}

void SyncObjectD3D11Client::RegisterTexture(ID3D11Texture2D* aTexture) {
  mSyncedTextures.push_back(aTexture);
}

bool SyncObjectD3D11Client::IsSyncObjectValid() {
  MOZ_ASSERT(mDevice);
  return true;
}

// We have only 1 sync object. As a thing that somehow works,
// we copy each of the textures that need to be synced with the compositor
// into our sync object and only use a lock for this sync object.
// This way, we don't have to sync every texture we send to the compositor.
// We only have to do this once per transaction.
bool SyncObjectD3D11Client::Synchronize(bool aFallible) {
  MOZ_ASSERT(mDevice);
  // Since this can be called from either the Paint or Main thread.
  // We don't want this to race since we initialize the sync texture here
  // too.
  MutexAutoLock syncLock(mSyncLock);

  if (!mSyncedTextures.size()) {
    return true;
  }
  if (!Init(mDevice, aFallible)) {
    return false;
  }

  return SynchronizeInternal(mDevice, aFallible);
}

bool SyncObjectD3D11Client::SynchronizeInternal(ID3D11Device* aDevice,
                                                bool aFallible) {
  mSyncLock.AssertCurrentThreadOwns();

  HRESULT hr;
  AutoTextureLock lock(mKeyedMutex, hr, 20000);

  if (hr == WAIT_TIMEOUT) {
    if (DeviceManagerDx::Get()->HasDeviceReset()) {
      gfxWarning() << "AcquireSync timed out because of device reset.";
      return false;
    }
    if (aFallible) {
      gfxWarning() << "Timeout on the D3D11 sync lock.";
    } else {
      gfxDevCrash(LogReason::D3D11SyncLock)
          << "Timeout on the D3D11 sync lock.";
    }
    return false;
  }

  D3D11_BOX box;
  box.front = box.top = box.left = 0;
  box.back = box.bottom = box.right = 1;

  RefPtr<ID3D11DeviceContext> ctx;
  aDevice->GetImmediateContext(getter_AddRefs(ctx));

  for (auto iter = mSyncedTextures.begin(); iter != mSyncedTextures.end();
       iter++) {
    ctx->CopySubresourceRegion(mSyncTexture, 0, 0, 0, 0, *iter, 0, &box);
  }

  mSyncedTextures.clear();

  return true;
}

uint32_t GetMaxTextureSizeFromDevice(ID3D11Device* aDevice) {
  return GetMaxTextureSizeForFeatureLevel(aDevice->GetFeatureLevel());
}

AutoLockD3D11Texture::AutoLockD3D11Texture(ID3D11Texture2D* aTexture) {
  aTexture->QueryInterface((IDXGIKeyedMutex**)getter_AddRefs(mMutex));
  if (!mMutex) {
    return;
  }
  HRESULT hr = mMutex->AcquireSync(0, 10000);
  if (hr == WAIT_TIMEOUT) {
    MOZ_CRASH("GFX: IMFYCbCrImage timeout");
  }

  if (FAILED(hr)) {
    NS_WARNING("Failed to lock the texture");
  }
}

AutoLockD3D11Texture::~AutoLockD3D11Texture() {
  if (!mMutex) {
    return;
  }
  HRESULT hr = mMutex->ReleaseSync(0);
  if (FAILED(hr)) {
    NS_WARNING("Failed to unlock the texture");
  }
}

SyncObjectD3D11ClientContentDevice::SyncObjectD3D11ClientContentDevice(
    SyncHandle aSyncHandle)
    : SyncObjectD3D11Client(aSyncHandle) {}

bool SyncObjectD3D11ClientContentDevice::Synchronize(bool aFallible) {
  // Since this can be called from either the Paint or Main thread.
  // We don't want this to race since we initialize the sync texture here
  // too.
  MutexAutoLock syncLock(mSyncLock);

  MOZ_ASSERT(mContentDevice);

  if (!mSyncedTextures.size()) {
    return true;
  }

  if (!Init(mContentDevice, aFallible)) {
    return false;
  }

  RefPtr<ID3D11Device> dev;
  mSyncTexture->GetDevice(getter_AddRefs(dev));

  if (dev == DeviceManagerDx::Get()->GetContentDevice()) {
    if (DeviceManagerDx::Get()->HasDeviceReset()) {
      return false;
    }
  }

  if (dev != mContentDevice) {
    gfxWarning() << "Attempt to sync texture from invalid device.";
    return false;
  }

  return SyncObjectD3D11Client::SynchronizeInternal(dev, aFallible);
}

bool SyncObjectD3D11ClientContentDevice::IsSyncObjectValid() {
  RefPtr<ID3D11Device> dev;
  // There is a case that devices are not initialized yet with WebRender.
  if (gfxPlatform::GetPlatform()->DevicesInitialized()) {
    dev = DeviceManagerDx::Get()->GetContentDevice();
  }

  // Update mDevice if the ContentDevice initialization is detected.
  if (!mContentDevice && dev && NS_IsMainThread()) {
    mContentDevice = dev;
  }

  if (!dev || (NS_IsMainThread() && dev != mContentDevice)) {
    return false;
  }
  return true;
}

void SyncObjectD3D11ClientContentDevice::EnsureInitialized() {
  if (mContentDevice) {
    return;
  }

  if (XRE_IsGPUProcess() || !gfxPlatform::GetPlatform()->DevicesInitialized()) {
    return;
  }

  mContentDevice = DeviceManagerDx::Get()->GetContentDevice();
}

}  // namespace layers
}  // namespace mozilla
