/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasDisplayHelper.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/TextureWrapperImage.h"
#include "mozilla/SVGObserverUtils.h"
#include "nsICanvasRenderingContextInternal.h"

namespace mozilla::dom {

OffscreenCanvasDisplayHelper::OffscreenCanvasDisplayHelper(
    HTMLCanvasElement* aCanvasElement, uint32_t aWidth, uint32_t aHeight)
    : mMutex("mozilla::dom::OffscreenCanvasDisplayHelper"),
      mCanvasElement(aCanvasElement),
      mImageProducerID(layers::ImageContainer::AllocateProducerID()) {
  mData.mSize.width = aWidth;
  mData.mSize.height = aHeight;
}

OffscreenCanvasDisplayHelper::~OffscreenCanvasDisplayHelper() {
  MutexAutoLock lock(mMutex);
  NS_ReleaseOnMainThread("OffscreenCanvas::mExpandedReader",
                         mExpandedReader.forget());
}

void OffscreenCanvasDisplayHelper::DestroyElement() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  mCanvasElement = nullptr;
}

void OffscreenCanvasDisplayHelper::DestroyCanvas() {
  MutexAutoLock lock(mMutex);
  mOffscreenCanvas = nullptr;
  mWorkerRef = nullptr;
}

void OffscreenCanvasDisplayHelper::SetWriteOnly(nsIPrincipal* aExpandedReader) {
  MutexAutoLock lock(mMutex);
  NS_ReleaseOnMainThread("OffscreenCanvasDisplayHelper::mExpandedReader",
                         mExpandedReader.forget());
  mExpandedReader = aExpandedReader;
  mIsWriteOnly = true;
}

bool OffscreenCanvasDisplayHelper::CallerCanRead(
    nsIPrincipal* aPrincipal) const {
  MutexAutoLock lock(mMutex);
  if (!mIsWriteOnly) {
    return true;
  }

  if (!aPrincipal) {
    return false;
  }

  // If mExpandedReader is set, this canvas was tainted only by
  // mExpandedReader's resources. So allow reading if the subject
  // principal subsumes mExpandedReader.
  if (mExpandedReader && aPrincipal->Subsumes(mExpandedReader)) {
    return true;
  }

  return nsContentUtils::PrincipalHasPermission(*aPrincipal,
                                                nsGkAtoms::all_urlsPermission);
}

CanvasContextType OffscreenCanvasDisplayHelper::GetContextType() const {
  MutexAutoLock lock(mMutex);
  return mType;
}

RefPtr<layers::ImageContainer> OffscreenCanvasDisplayHelper::GetImageContainer()
    const {
  MutexAutoLock lock(mMutex);
  return mImageContainer;
}

bool OffscreenCanvasDisplayHelper::MayUpdateContext(WorkerPrivate* aWorker,
                                                    ErrorResult& aRv) {
  MutexAutoLock lock(mMutex);
  // Allow context update on main thread to be more fault tolerant, whereas
  // update on a worker needs to more strict in case of transfer.
  if ((mCreatedOnMainThread && aWorker) || mCreatedOnWorker) {
    aRv.ThrowInvalidStateError(
        "Cannot update created context to different thread");
    return false;
  }
  if (!mOffscreenCanvas) {
    return true;
  }
  if (aWorker) {
    if (mWorkerRef && mWorkerRef->Private() == aWorker) {
      return true;
    }
  } else if (!mWorkerRef) {
    return true;
  }
  aRv.ThrowInvalidStateError(
      "Cannot update created context to different thread");
  return false;
}

void OffscreenCanvasDisplayHelper::UpdateContext(
    OffscreenCanvas* aOffscreenCanvas, RefPtr<ThreadSafeWorkerRef>&& aWorkerRef,
    CanvasContextType aType, const Maybe<int32_t>& aChildId) {
  RefPtr<layers::ImageContainer> imageContainer =
      MakeRefPtr<layers::ImageContainer>(layers::ImageContainer::ASYNCHRONOUS);

  MutexAutoLock lock(mMutex);

  mOffscreenCanvas = aOffscreenCanvas;
  mWorkerRef = std::move(aWorkerRef);
  mType = aType;
  mContextChildId = aChildId;
  mImageContainer = std::move(imageContainer);

  if (mOffscreenCanvas) {
    if (mWorkerRef) {
      mCreatedOnWorker = true;
    } else {
      mCreatedOnMainThread = true;
    }
  }

  if (aChildId) {
    mContextManagerId = Some(gfx::CanvasManagerChild::Get()->Id());
  } else {
    mContextManagerId.reset();
  }

  MaybeQueueInvalidateElement();
}

already_AddRefed<OffscreenCanvas>
OffscreenCanvasDisplayHelper::GetCanvasForWorker(WorkerPrivate* aWorker) {
  MutexAutoLock lock(mMutex);
  // A runnable queued to a worker somehow got assigned to another thread.
  if (NS_WARN_IF(!mWorkerRef) || NS_WARN_IF(mWorkerRef->Private() != aWorker)) {
    return nullptr;
  }
  return do_AddRef(mOffscreenCanvas);
}

void OffscreenCanvasDisplayHelper::FlushForDisplay() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);

  // Without an OffscreenCanvas object bound to us, we either have not drawn
  // using the canvas at all, or we have already destroyed it.
  if (!mOffscreenCanvas) {
    return;
  }

  // We assign/destroy the worker ref at the same time as the OffscreenCanvas
  // ref, so we can only have the canvas without a worker ref if it exists on
  // the main thread.
  if (!mWorkerRef) {
    // We queue to ensure that we have the same asynchronous update behaviour
    // for a main thread and a worker based OffscreenCanvas.
    mOffscreenCanvas->QueueCommitToCompositor();
    return;
  }

  class FlushWorkerRunnable final : public WorkerRunnable {
   public:
    FlushWorkerRunnable(WorkerPrivate* aWorkerPrivate,
                        OffscreenCanvasDisplayHelper* aDisplayHelper)
        : WorkerRunnable(aWorkerPrivate), mDisplayHelper(aDisplayHelper) {}

    bool WorkerRun(JSContext*, WorkerPrivate* aWorkerPrivate) override {
      // The OffscreenCanvas can only be freed on the worker thread, so we
      // cannot be racing with an OffscreenCanvas::DestroyCanvas call and its
      // destructor. We just need to make sure we don't call into
      // OffscreenCanvas while holding the lock since it calls back into the
      // OffscreenCanvasDisplayHelper.
      RefPtr<OffscreenCanvas> canvas =
          mDisplayHelper->GetCanvasForWorker(aWorkerPrivate);

      if (canvas) {
        canvas->CommitFrameToCompositor();
      }
      return true;
    }

   private:
    RefPtr<OffscreenCanvasDisplayHelper> mDisplayHelper;
  };

  // Otherwise we are calling from the main thread during painting to a canvas
  // on a worker thread.
  auto task = MakeRefPtr<FlushWorkerRunnable>(mWorkerRef->Private(), this);
  task->Dispatch();
}

bool OffscreenCanvasDisplayHelper::CommitFrameToCompositor(
    nsICanvasRenderingContextInternal* aContext,
    layers::TextureType aTextureType,
    const Maybe<OffscreenCanvasDisplayData>& aData) {
  MutexAutoLock lock(mMutex);

  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;
  layers::TextureFlags flags = layers::TextureFlags::IMMUTABLE;

  if (!mCanvasElement) {
    // Our weak reference to the canvas element has been cleared, so we cannot
    // present directly anymore.
    return false;
  }

  if (aData) {
    mData = aData.ref();
    MaybeQueueInvalidateElement();
  }

  if (mData.mOwnerId.isSome()) {
    // No need to update the ImageContainer as the presentation itself is
    // handled in the compositor process.
    return true;
  }

  if (!mImageContainer) {
    return false;
  }

  if (mData.mIsOpaque) {
    flags |= layers::TextureFlags::IS_OPAQUE;
    format = gfx::SurfaceFormat::B8G8R8X8;
  } else if (!mData.mIsAlphaPremult) {
    flags |= layers::TextureFlags::NON_PREMULTIPLIED;
  }

  switch (mData.mOriginPos) {
    case gl::OriginPos::BottomLeft:
      flags |= layers::TextureFlags::ORIGIN_BOTTOM_LEFT;
      break;
    case gl::OriginPos::TopLeft:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled origin position!");
      break;
  }

  auto imageBridge = layers::ImageBridgeChild::GetSingleton();
  if (!imageBridge) {
    return false;
  }

  bool paintCallbacks = mData.mDoPaintCallbacks;
  RefPtr<layers::Image> image;
  RefPtr<gfx::SourceSurface> surface;
  Maybe<layers::SurfaceDescriptor> desc;

  {
    MutexAutoUnlock unlock(mMutex);
    if (paintCallbacks) {
      aContext->OnBeforePaintTransaction();
    }

    desc = aContext->PresentFrontBuffer(nullptr, aTextureType);
    if (!desc) {
      surface =
          aContext->GetFrontBufferSnapshot(/* requireAlphaPremult */ false);
      if (surface && surface->GetType() == gfx::SurfaceType::WEBGL) {
        // Ensure we can map in the surface. If we get a SourceSurfaceWebgl
        // surface, then it may not be backed by raw pixels yet. We need to map
        // it on the owning thread rather than the ImageBridge thread.
        gfx::DataSourceSurface::ScopedMap map(
            static_cast<gfx::DataSourceSurface*>(surface.get()),
            gfx::DataSourceSurface::READ);
        if (!map.IsMapped()) {
          surface = nullptr;
        }
      }
    }

    if (paintCallbacks) {
      aContext->OnDidPaintTransaction();
    }
  }

  if (desc) {
    if (desc->type() ==
        layers::SurfaceDescriptor::TSurfaceDescriptorRemoteTexture) {
      const auto& textureDesc = desc->get_SurfaceDescriptorRemoteTexture();
      imageBridge->UpdateCompositable(mImageContainer, textureDesc.textureId(),
                                      textureDesc.ownerId(), mData.mSize,
                                      flags);
    } else {
      RefPtr<layers::TextureClient> texture =
          layers::SharedSurfaceTextureData::CreateTextureClient(
              *desc, format, mData.mSize, flags, imageBridge);
      if (texture) {
        image = new layers::TextureWrapperImage(
            texture, gfx::IntRect(gfx::IntPoint(0, 0), mData.mSize));
      }
    }
  } else if (surface) {
    auto surfaceImage = MakeRefPtr<layers::SourceSurfaceImage>(surface);
    surfaceImage->SetTextureFlags(flags);
    image = surfaceImage;
  }

  if (image) {
    AutoTArray<layers::ImageContainer::NonOwningImage, 1> imageList;
    imageList.AppendElement(layers::ImageContainer::NonOwningImage(
        image, TimeStamp(), mLastFrameID++, mImageProducerID));
    mImageContainer->SetCurrentImages(imageList);
  } else if (!desc ||
             desc->type() !=
                 layers::SurfaceDescriptor::TSurfaceDescriptorRemoteTexture) {
    mImageContainer->ClearAllImages();
  }

  // We save any current surface because we might need it in GetSnapshot. If we
  // are on a worker thread and not WebGL, then this will be the only way we can
  // access the pixel data on the main thread.
  mFrontBufferSurface = surface;
  return true;
}

void OffscreenCanvasDisplayHelper::MaybeQueueInvalidateElement() {
  if (!mPendingInvalidate) {
    mPendingInvalidate = true;
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "OffscreenCanvasDisplayHelper::InvalidateElement",
        [self = RefPtr{this}] { self->InvalidateElement(); }));
  }
}

void OffscreenCanvasDisplayHelper::InvalidateElement() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<HTMLCanvasElement> canvasElement;
  gfx::IntSize size;

  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mPendingInvalidate);
    mPendingInvalidate = false;
    canvasElement = mCanvasElement;
    size = mData.mSize;
  }

  if (canvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(canvasElement);
    canvasElement->InvalidateCanvasPlaceholder(size.width, size.height);
    canvasElement->InvalidateCanvasContent(nullptr);
  }
}

bool OffscreenCanvasDisplayHelper::TransformSurface(
    const gfx::DataSourceSurface::ScopedMap& aSrcMap,
    const gfx::DataSourceSurface::ScopedMap& aDstMap,
    gfx::SurfaceFormat aFormat, const gfx::IntSize& aSize, bool aNeedsPremult,
    gl::OriginPos aOriginPos) const {
  if (!aSrcMap.IsMapped() || !aDstMap.IsMapped()) {
    return false;
  }

  switch (aOriginPos) {
    case gl::OriginPos::BottomLeft:
      if (aNeedsPremult) {
        return gfx::PremultiplyYFlipData(aSrcMap.GetData(), aSrcMap.GetStride(),
                                         aFormat, aDstMap.GetData(),
                                         aDstMap.GetStride(), aFormat, aSize);
      }
      return gfx::SwizzleYFlipData(aSrcMap.GetData(), aSrcMap.GetStride(),
                                   aFormat, aDstMap.GetData(),
                                   aDstMap.GetStride(), aFormat, aSize);
    case gl::OriginPos::TopLeft:
      if (aNeedsPremult) {
        return gfx::PremultiplyData(aSrcMap.GetData(), aSrcMap.GetStride(),
                                    aFormat, aDstMap.GetData(),
                                    aDstMap.GetStride(), aFormat, aSize);
      }
      return gfx::SwizzleData(aSrcMap.GetData(), aSrcMap.GetStride(), aFormat,
                              aDstMap.GetData(), aDstMap.GetStride(), aFormat,
                              aSize);
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled origin position!");
      break;
  }

  return false;
}

already_AddRefed<gfx::SourceSurface>
OffscreenCanvasDisplayHelper::GetSurfaceSnapshot() {
  MOZ_ASSERT(NS_IsMainThread());

  Maybe<layers::SurfaceDescriptor> desc;

  bool hasAlpha;
  bool isAlphaPremult;
  gl::OriginPos originPos;
  Maybe<uint32_t> managerId;
  Maybe<int32_t> childId;
  HTMLCanvasElement* canvasElement;
  RefPtr<gfx::SourceSurface> surface;
  Maybe<layers::RemoteTextureOwnerId> ownerId;

  {
    MutexAutoLock lock(mMutex);
    hasAlpha = !mData.mIsOpaque;
    isAlphaPremult = mData.mIsAlphaPremult;
    originPos = mData.mOriginPos;
    ownerId = mData.mOwnerId;
    managerId = mContextManagerId;
    childId = mContextChildId;
    canvasElement = mCanvasElement;
    surface = mFrontBufferSurface;
  }

  if (surface) {
    // We already have a copy of the front buffer in our process.

    if (originPos == gl::OriginPos::TopLeft && (!hasAlpha || isAlphaPremult)) {
      // If we don't need to y-flip, and it is either opaque or premultiplied,
      // we can just return the same surface.
      return surface.forget();
    }

    // Otherwise we need to copy and apply the necessary transformations.
    RefPtr<gfx::DataSourceSurface> srcSurface = surface->GetDataSurface();
    if (!srcSurface) {
      return nullptr;
    }

    const auto size = srcSurface->GetSize();
    const auto format = srcSurface->GetFormat();

    RefPtr<gfx::DataSourceSurface> dstSurface =
        gfx::Factory::CreateDataSourceSurface(size, format, /* aZero */ false);
    if (!dstSurface) {
      return nullptr;
    }

    gfx::DataSourceSurface::ScopedMap srcMap(srcSurface,
                                             gfx::DataSourceSurface::READ);
    gfx::DataSourceSurface::ScopedMap dstMap(dstSurface,
                                             gfx::DataSourceSurface::WRITE);
    if (!TransformSurface(srcMap, dstMap, format, size,
                          hasAlpha && !isAlphaPremult, originPos)) {
      return nullptr;
    }

    return dstSurface.forget();
  }

#ifdef MOZ_WIDGET_ANDROID
  // On Android, we cannot both display a GL context and read back the pixels.
  if (canvasElement) {
    return nullptr;
  }
#endif

  if (managerId && childId) {
    // We don't have a usable surface, and the context lives in the compositor
    // process.
    return gfx::CanvasManagerChild::Get()->GetSnapshot(
        managerId.value(), childId.value(), ownerId,
        hasAlpha ? gfx::SurfaceFormat::R8G8B8A8 : gfx::SurfaceFormat::R8G8B8X8,
        hasAlpha && !isAlphaPremult, originPos == gl::OriginPos::BottomLeft);
  }

  // If we don't have any protocol IDs, or an existing surface, it is possible
  // it is a main thread OffscreenCanvas instance. If so, then the element's
  // OffscreenCanvas is not neutered and has access to the context. We can use
  // that to get the snapshot directly.
  if (!canvasElement) {
    return nullptr;
  }

  const auto* offscreenCanvas = canvasElement->GetOffscreenCanvas();
  nsICanvasRenderingContextInternal* context = offscreenCanvas->GetContext();
  if (!context) {
    return nullptr;
  }

  surface = context->GetFrontBufferSnapshot(/* requireAlphaPremult */ false);
  if (!surface) {
    return nullptr;
  }

  if (originPos == gl::OriginPos::TopLeft && (!hasAlpha || isAlphaPremult)) {
    // If we don't need to y-flip, and it is either opaque or premultiplied,
    // we can just return the same surface.
    return surface.forget();
  }

  // Otherwise we need to apply the necessary transformations in place.
  RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::READ_WRITE);
  if (!TransformSurface(map, map, dataSurface->GetFormat(),
                        dataSurface->GetSize(), hasAlpha && !isAlphaPremult,
                        originPos)) {
    return nullptr;
  }

  return surface.forget();
}

already_AddRefed<layers::Image> OffscreenCanvasDisplayHelper::GetAsImage() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<gfx::SourceSurface> surface = GetSurfaceSnapshot();
  if (!surface) {
    return nullptr;
  }
  return MakeAndAddRef<layers::SourceSurfaceImage>(surface);
}

}  // namespace mozilla::dom
