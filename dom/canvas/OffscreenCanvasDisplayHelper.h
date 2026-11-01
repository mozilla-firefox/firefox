/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
#define MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_

#include "GLContextTypes.h"
#include "ImageContainer.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CanvasRenderingContextHelper.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsISupportsImpl.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {
class HTMLCanvasElement;
class OffscreenCanvas;
class ThreadSafeWorkerRef;
class WorkerPrivate;

struct OffscreenCanvasDisplayData final {
  mozilla::gfx::IntSize mSize = {0, 0};
  bool mDoPaintCallbacks = false;
  bool mIsOpaque = true;
  bool mIsAlphaPremult = true;
  mozilla::gl::OriginPos mOriginPos = gl::OriginPos::TopLeft;
};

class OffscreenCanvasDisplayHelper final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OffscreenCanvasDisplayHelper)

 public:
  explicit OffscreenCanvasDisplayHelper(HTMLCanvasElement* aCanvasElement,
                                        uint32_t aWidth, uint32_t aHeight);

  CanvasContextType GetContextType() const;

  RefPtr<layers::ImageContainer> GetImageContainer() const;

  bool MayUpdateContext(WorkerPrivate* aWorker, ErrorResult& aRv);
  void UpdateContext(OffscreenCanvas* aOffscreenCanvas,
                     RefPtr<ThreadSafeWorkerRef>&& aWorkerRef,
                     CanvasContextType aType,
                     const Maybe<mozilla::ipc::ActorId>& aChildId);

  void FlushForDisplay();

  // While presentation is disabled, the frames the canvas draws are kept in
  // the context's own buffers instead of being handed to the compositor. The
  // latest one is presented when presentation is enabled again.
  void SetPresentationEnabled(bool aEnabled);

  bool CommitFrameToCompositor(nsICanvasRenderingContextInternal* aContext,
                               const Maybe<OffscreenCanvasDisplayData>& aData);

  void DestroyCanvas();
  void DestroyElement();

  bool IsWriteOnly() const {
    MutexAutoLock lock(mMutex);
    return mIsWriteOnly;
  }

  bool HasWorkerRef() const {
    MutexAutoLock lock(mMutex);
    return !!mWorkerRef;
  }

  void SetWriteOnly(nsIPrincipal* aExpandedReader = nullptr);
  bool CallerCanRead(nsIPrincipal& aPrincipal) const;

  bool CanElementCaptureStream() const;
  bool UsingElementCaptureStream() const;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot();
  already_AddRefed<mozilla::layers::Image> GetAsImage();
  UniquePtr<uint8_t[]> GetImageBuffer(
      CanvasUtils::ImageExtraction aExtractionBehavior, int32_t* aOutFormat,
      gfx::IntSize* aOutImageSize);
  void MaybeRandomizePixels(CanvasUtils::ImageExtraction aExtractionBehavior,
                            uint8_t* aData, gfx::IntSize aSize);

 private:
  ~OffscreenCanvasDisplayHelper();
  void MaybeQueueInvalidateElement() MOZ_REQUIRES(mMutex);
  already_AddRefed<OffscreenCanvas> GetCanvasForWorker(WorkerPrivate* aWorker);
  void InvalidateElement();

  already_AddRefed<gfx::SourceSurface> TransformSurface(
      gfx::SourceSurface* aSurface, bool aHasAlpha, bool aIsAlphaPremult,
      gl::OriginPos aOriginPos) const;

  mutable Mutex mMutex;
  HTMLCanvasElement* MOZ_NON_OWNING_REF mCanvasElement MOZ_GUARDED_BY(mMutex);
  OffscreenCanvas* MOZ_NON_OWNING_REF mOffscreenCanvas MOZ_GUARDED_BY(mMutex) =
      nullptr;
  RefPtr<layers::ImageContainer> mImageContainer MOZ_GUARDED_BY(mMutex);
  RefPtr<gfx::SourceSurface> mFrontBufferSurface MOZ_GUARDED_BY(mMutex);
  RefPtr<ThreadSafeWorkerRef> mWorkerRef MOZ_GUARDED_BY(mMutex);

  OffscreenCanvasDisplayData mData MOZ_GUARDED_BY(mMutex);
  CanvasContextType mType MOZ_GUARDED_BY(mMutex) = CanvasContextType::NoContext;
  Maybe<uint32_t> mContextManagerId MOZ_GUARDED_BY(mMutex);
  Maybe<mozilla::ipc::ActorId> mContextChildId MOZ_GUARDED_BY(mMutex);
  const mozilla::layers::ImageContainer::ProducerID mImageProducerID;
  mozilla::layers::ImageContainer::FrameID mLastFrameID MOZ_GUARDED_BY(mMutex) =
      0;
  bool mPendingInvalidate MOZ_GUARDED_BY(mMutex) = false;
  bool mPresentationEnabled MOZ_GUARDED_BY(mMutex) = true;
  bool mPendingPresent MOZ_GUARDED_BY(mMutex) = false;
  bool mIsWriteOnly MOZ_GUARDED_BY(mMutex) = false;
  RefPtr<nsIPrincipal> mExpandedReader MOZ_GUARDED_BY(mMutex);
  bool mCreatedOnMainThread MOZ_GUARDED_BY(mMutex) = false;
  bool mCreatedOnWorker MOZ_GUARDED_BY(mMutex) = false;
};

}  // namespace mozilla::dom

#endif  // MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
