/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageBitmap.h"

#include "imgLoader.h"
#include "imgTools.h"
#include "jsapi.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Mutex.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/CanvasUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLMediaElementBinding.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/OffscreenCanvas.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/SVGImageElement.h"
#include "mozilla/dom/StructuredCloneTags.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Scale.h"
#include "mozilla/gfx/Swizzle.h"
#include "nsGlobalWindowInner.h"
#include "nsIAsyncInputStream.h"
#include "nsISerialEventTarget.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsStreamUtils.h"

using namespace mozilla::gfx;
using namespace mozilla::layers;
using mozilla::dom::HTMLMediaElement_Binding::HAVE_METADATA;
using mozilla::dom::HTMLMediaElement_Binding::NETWORK_EMPTY;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageBitmap, mParent)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageBitmap)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageBitmap)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageBitmap)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

class ImageBitmapShutdownObserver;

static StaticMutex sShutdownMutex;
static ImageBitmapShutdownObserver* sShutdownObserver = nullptr;

class SendShutdownToWorkerThread : public MainThreadWorkerControlRunnable {
 public:
  explicit SendShutdownToWorkerThread(ImageBitmap* aImageBitmap)
      : MainThreadWorkerControlRunnable("SendShutdownToWorkerThread"),
        mImageBitmap(aImageBitmap) {
    MOZ_ASSERT(GetCurrentThreadWorkerPrivate());
    mTarget = GetCurrentThreadWorkerPrivate()->ControlEventTarget();
    MOZ_ASSERT(mTarget);
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    if (mImageBitmap) {
      mImageBitmap->OnShutdown();
      mImageBitmap = nullptr;
    }
    return true;
  }

  void DispatchToWorker() {
    MOZ_ASSERT(mTarget);
    (void)NS_WARN_IF(NS_FAILED(mTarget->Dispatch(this, NS_DISPATCH_NORMAL)));
    mTarget = nullptr;
  }

  nsCOMPtr<nsISerialEventTarget> mTarget;
  ImageBitmap* mImageBitmap;
};

/* This class observes shutdown notifications and sends that notification
 * to the worker thread if the image bitmap is on a worker thread.
 */
class ImageBitmapShutdownObserver final : public nsIObserver {
 public:
  void Init() {
    sShutdownMutex.AssertCurrentThreadOwns();
    if (NS_IsMainThread()) {
      RegisterObserver();
      return;
    }

    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    auto* mainThreadEventTarget = workerPrivate->MainThreadEventTarget();
    MOZ_ASSERT(mainThreadEventTarget);
    RefPtr<ImageBitmapShutdownObserver> self = this;
    nsCOMPtr<nsIRunnable> r =
        NS_NewRunnableFunction("ImageBitmapShutdownObserver::RegisterObserver",
                               [self]() { self->RegisterObserver(); });
    mainThreadEventTarget->Dispatch(r.forget());
  }

  void RegisterObserver() {
    MOZ_ASSERT(NS_IsMainThread());
    nsContentUtils::RegisterShutdownObserver(this);
  }

  already_AddRefed<SendShutdownToWorkerThread> Track(
      ImageBitmap* aImageBitmap) {
    sShutdownMutex.AssertCurrentThreadOwns();
    MOZ_ASSERT(!mBitmaps.Contains(aImageBitmap));

    RefPtr<SendShutdownToWorkerThread> runnable = nullptr;
    if (!NS_IsMainThread()) {
      runnable = new SendShutdownToWorkerThread(aImageBitmap);
    }

    RefPtr<SendShutdownToWorkerThread> retval = runnable;
    mBitmaps.Insert(aImageBitmap);
    return retval.forget();
  }

  void Untrack(ImageBitmap* aImageBitmap) {
    sShutdownMutex.AssertCurrentThreadOwns();
    MOZ_ASSERT(mBitmaps.Contains(aImageBitmap));

    mBitmaps.Remove(aImageBitmap);
  }

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER
 private:
  ~ImageBitmapShutdownObserver() = default;

  nsTHashSet<ImageBitmap*> mBitmaps;
};

NS_IMPL_ISUPPORTS(ImageBitmapShutdownObserver, nsIObserver)

NS_IMETHODIMP
ImageBitmapShutdownObserver::Observe(nsISupports* aSubject, const char* aTopic,
                                     const char16_t* aData) {
  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    StaticMutexAutoLock lock(sShutdownMutex);

    for (const auto& bitmap : mBitmaps) {
      const auto& runnable = bitmap->mShutdownRunnable;
      if (runnable) {
        runnable->DispatchToWorker();
      } else {
        bitmap->OnShutdown();
      }
    }

    nsContentUtils::UnregisterShutdownObserver(this);

    sShutdownObserver = nullptr;
  }

  return NS_OK;
}

/*
 * If either aRect.width or aRect.height are negative, then return a new IntRect
 * which represents the same rectangle as the aRect does but with positive width
 * and height.
 */
static IntRect FixUpNegativeDimension(const IntRect& aRect, ErrorResult& aRv) {
  gfx::IntRect rect = aRect;

  // fix up negative dimensions
  if (rect.width < 0) {
    CheckedInt32 checkedX = CheckedInt32(rect.x) + rect.width;

    if (!checkedX.isValid()) {
      aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
      return rect;
    }

    rect.x = checkedX.value();
    rect.width = -(rect.width);
  }

  if (rect.height < 0) {
    CheckedInt32 checkedY = CheckedInt32(rect.y) + rect.height;

    if (!checkedY.isValid()) {
      aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
      return rect;
    }

    rect.y = checkedY.value();
    rect.height = -(rect.height);
  }

  return rect;
}

/*
 * This helper function copies the data of the given DataSourceSurface,
 *  _aSurface_, in the given area, _aCropRect_, into a new DataSourceSurface.
 * This might return null if it can not create a new SourceSurface or it cannot
 * read data from the given _aSurface_.
 *
 * Warning: Even though the area of _aCropRect_ is just the same as the size of
 *          _aSurface_, this function still copy data into a new
 *          DataSourceSurface.
 */
static already_AddRefed<DataSourceSurface> CropAndCopyDataSourceSurface(
    DataSourceSurface* aSurface, const IntRect& aCropRect) {
  MOZ_ASSERT(aSurface);

  // Check the aCropRect
  ErrorResult error;
  const IntRect positiveCropRect = FixUpNegativeDimension(aCropRect, error);
  if (NS_WARN_IF(error.Failed())) {
    error.SuppressException();
    return nullptr;
  }

  // Calculate the size of the new SourceSurface.
  // We cannot keep using an opaque format like SurfaceFormat::B8G8R8X8 to
  // create new DataSourceSurface, since it does not handle opacity, however the
  // specification explicitly define that "If any of the pixels on this
  // rectangle are outside the area where the input bitmap was placed, then they
  // will be transparent black in output."
  // So, instead, we use the format with alpha in the same channel order.
  SurfaceFormat format = aSurface->GetFormat();
  switch (format) {
    case SurfaceFormat::B8G8R8X8:
      format = SurfaceFormat::B8G8R8A8;
      break;
    case SurfaceFormat::R8G8B8X8:
      format = SurfaceFormat::R8G8B8A8;
      break;
    case SurfaceFormat::X8R8G8B8:
      format = SurfaceFormat::A8R8G8B8;
      break;
    default:
      break;
  }
  const int bytesPerPixel = BytesPerPixel(format);
  const IntSize dstSize =
      IntSize(positiveCropRect.width, positiveCropRect.height);
  const uint32_t dstStride = dstSize.width * bytesPerPixel;

  // Create a new SourceSurface.
  RefPtr<DataSourceSurface> dstDataSurface =
      Factory::CreateDataSourceSurfaceWithStride(dstSize, format, dstStride,
                                                 true);

  if (NS_WARN_IF(!dstDataSurface)) {
    return nullptr;
  }

  // Only do copying and cropping when the positiveCropRect intersects with
  // the size of aSurface.
  const IntRect surfRect(IntPoint(0, 0), aSurface->GetSize());
  if (surfRect.Intersects(positiveCropRect)) {
    const IntRect surfPortion = surfRect.Intersect(positiveCropRect);
    const IntPoint dest(std::max(0, surfPortion.X() - positiveCropRect.X()),
                        std::max(0, surfPortion.Y() - positiveCropRect.Y()));

    // Copy the raw data into the newly created DataSourceSurface.
    DataSourceSurface::ScopedMap srcMap(aSurface, DataSourceSurface::READ);
    DataSourceSurface::ScopedMap dstMap(dstDataSurface,
                                        DataSourceSurface::WRITE);
    if (NS_WARN_IF(!srcMap.IsMapped()) || NS_WARN_IF(!dstMap.IsMapped())) {
      return nullptr;
    }

    uint8_t* srcBufferPtr = srcMap.GetData() +
                            surfPortion.y * srcMap.GetStride() +
                            surfPortion.x * bytesPerPixel;
    uint8_t* dstBufferPtr =
        dstMap.GetData() + dest.y * dstMap.GetStride() + dest.x * bytesPerPixel;
    CheckedInt<uint32_t> copiedBytesPerRaw =
        CheckedInt<uint32_t>(surfPortion.width) * bytesPerPixel;
    if (!copiedBytesPerRaw.isValid()) {
      return nullptr;
    }

    for (int i = 0; i < surfPortion.height; ++i) {
      memcpy(dstBufferPtr, srcBufferPtr, copiedBytesPerRaw.value());
      srcBufferPtr += srcMap.GetStride();
      dstBufferPtr += dstMap.GetStride();
    }
  }

  return dstDataSurface.forget();
}

/*
 * This helper function scales the data of the given DataSourceSurface,
 *  _aSurface_, in the given area, _aCropRect_, into a new DataSourceSurface.
 * This might return null if it can not create a new SourceSurface or it cannot
 * read data from the given _aSurface_.
 *
 */
static already_AddRefed<DataSourceSurface> ScaleDataSourceSurface(
    DataSourceSurface* aSurface, const ImageBitmapOptions& aOptions) {
  if (NS_WARN_IF(!aSurface)) {
    return nullptr;
  }

  const SurfaceFormat format = aSurface->GetFormat();
  const int bytesPerPixel = BytesPerPixel(format);

  const IntSize srcSize = aSurface->GetSize();
  Maybe<int32_t> resizeWidth;
  Maybe<int32_t> resizeHeight;
  if (aOptions.mResizeWidth.WasPassed()) {
    CheckedInt<int32_t> checked(aOptions.mResizeWidth.Value());
    if (!checked.isValid()) {
      return nullptr;
    }
    resizeWidth.emplace(checked.value());
  }
  if (aOptions.mResizeHeight.WasPassed()) {
    CheckedInt<int32_t> checked(aOptions.mResizeHeight.Value());
    if (!checked.isValid()) {
      return nullptr;
    }
    resizeHeight.emplace(checked.value());
  }
  Maybe<IntSize> maybeDstSize =
      nsLayoutUtils::ComputeResizedSize(srcSize, resizeWidth, resizeHeight);
  if (!maybeDstSize) {
    return nullptr;
  }
  const IntSize dstSize = *maybeDstSize;
  const int32_t dstStride = dstSize.width * bytesPerPixel;

  // Create a new SourceSurface.
  RefPtr<DataSourceSurface> dstDataSurface =
      Factory::CreateDataSourceSurfaceWithStride(dstSize, format, dstStride,
                                                 true);

  if (NS_WARN_IF(!dstDataSurface)) {
    return nullptr;
  }

  // Copy the raw data into the newly created DataSourceSurface.
  DataSourceSurface::ScopedMap srcMap(aSurface, DataSourceSurface::READ);
  DataSourceSurface::ScopedMap dstMap(dstDataSurface, DataSourceSurface::WRITE);
  if (NS_WARN_IF(!srcMap.IsMapped()) || NS_WARN_IF(!dstMap.IsMapped())) {
    return nullptr;
  }

  uint8_t* srcBufferPtr = srcMap.GetData();
  uint8_t* dstBufferPtr = dstMap.GetData();

  SamplingFilter filter = SamplingFilter::LINEAR;
  switch (aOptions.mResizeQuality) {
    case ResizeQuality::Pixelated:
      filter = SamplingFilter::POINT;
      break;
    case ResizeQuality::Medium:
    case ResizeQuality::High:
      filter = SamplingFilter::GOOD;
      break;
    case ResizeQuality::Low:
    default:
      break;
  }

  bool res =
      Scale(srcBufferPtr, srcSize.width, srcSize.height, srcMap.GetStride(),
            dstBufferPtr, dstSize.width, dstSize.height, dstMap.GetStride(),
            aSurface->GetFormat(), filter);
  if (!res) {
    return nullptr;
  }

  return dstDataSurface.forget();
}

static DataSourceSurface* FlipYDataSourceSurface(DataSourceSurface* aSurface) {
  MOZ_ASSERT(aSurface);

  // Invert in y direction.
  DataSourceSurface::ScopedMap srcMap(aSurface, DataSourceSurface::READ_WRITE);
  if (NS_WARN_IF(!srcMap.IsMapped())) {
    return nullptr;
  }

  const int bpp = BytesPerPixel(aSurface->GetFormat());
  const IntSize srcSize = aSurface->GetSize();
  uint8_t* srcBufferPtr = srcMap.GetData();
  const uint32_t stride = srcMap.GetStride();

  CheckedInt<uint32_t> copiedBytesPerRaw = CheckedInt<uint32_t>(stride);
  if (!copiedBytesPerRaw.isValid()) {
    return nullptr;
  }

  for (int i = 0; i < srcSize.height / 2; ++i) {
    std::swap_ranges(srcBufferPtr + stride * i,
                     srcBufferPtr + stride * i + srcSize.width * bpp,
                     srcBufferPtr + stride * (srcSize.height - 1 - i));
  }

  return aSurface;
}

static DataSourceSurface* AlphaPremultiplyDataSourceSurface(
    DataSourceSurface* aSurface, const bool forward = true) {
  MOZ_ASSERT(aSurface);

  DataSourceSurface::MappedSurface surfaceMap;

  if (aSurface->Map(DataSourceSurface::MapType::READ_WRITE, &surfaceMap)) {
    if (forward) {
      PremultiplyData(surfaceMap.mData, surfaceMap.mStride,
                      aSurface->GetFormat(), surfaceMap.mData,
                      surfaceMap.mStride, aSurface->GetFormat(),
                      aSurface->GetSize());
    } else {
      UnpremultiplyData(surfaceMap.mData, surfaceMap.mStride,
                        aSurface->GetFormat(), surfaceMap.mData,
                        surfaceMap.mStride, aSurface->GetFormat(),
                        aSurface->GetSize());
    }

    aSurface->Unmap();
  } else {
    return nullptr;
  }

  return aSurface;
}

/*
 * Encapsulate the given _aSurface_ into a layers::SourceSurfaceImage.
 */
static already_AddRefed<layers::Image> CreateImageFromSurface(
    SourceSurface* aSurface) {
  MOZ_ASSERT(aSurface);
  RefPtr<layers::SourceSurfaceImage> image =
      new layers::SourceSurfaceImage(aSurface->GetSize(), aSurface);
  return image.forget();
}

/*
 * CreateImageFromRawData(), CreateSurfaceFromRawData() and
 * CreateImageFromRawDataInMainThreadSyncTask are helpers for
 * create-from-ImageData case
 */
static already_AddRefed<SourceSurface> CreateSurfaceFromRawData(
    const gfx::IntSize& aSize, uint32_t aStride, gfx::SurfaceFormat aFormat,
    uint8_t* aBuffer, uint32_t aBufferLength, const Maybe<IntRect>& aCropRect,
    const ImageBitmapOptions& aOptions) {
  MOZ_ASSERT(!aSize.IsEmpty());
  MOZ_ASSERT(aBuffer);

  // Wrap the source buffer into a SourceSurface.
  RefPtr<DataSourceSurface> dataSurface =
      Factory::CreateWrappingDataSourceSurface(aBuffer, aStride, aSize,
                                               aFormat);

  if (NS_WARN_IF(!dataSurface)) {
    return nullptr;
  }

  // The temporary cropRect variable is equal to the size of source buffer if we
  // do not need to crop, or it equals to the given cropping size.
  const IntRect cropRect =
      aCropRect.valueOr(IntRect(0, 0, aSize.width, aSize.height));

  // Copy the source buffer in the _cropRect_ area into a new SourceSurface.
  RefPtr<DataSourceSurface> result =
      CropAndCopyDataSourceSurface(dataSurface, cropRect);
  if (NS_WARN_IF(!result)) {
    return nullptr;
  }

  if (aOptions.mImageOrientation == ImageOrientation::FlipY) {
    result = FlipYDataSourceSurface(result);

    if (NS_WARN_IF(!result)) {
      return nullptr;
    }
  }

  if (aOptions.mPremultiplyAlpha == PremultiplyAlpha::Premultiply) {
    result = AlphaPremultiplyDataSourceSurface(result);

    if (NS_WARN_IF(!result)) {
      return nullptr;
    }
  }

  if (aOptions.mResizeWidth.WasPassed() || aOptions.mResizeHeight.WasPassed()) {
    dataSurface = result->GetDataSurface();
    result = ScaleDataSourceSurface(dataSurface, aOptions);
    if (NS_WARN_IF(!result)) {
      return nullptr;
    }
  }

  return result.forget();
}

static already_AddRefed<layers::Image> CreateImageFromRawData(
    const gfx::IntSize& aSize, uint32_t aStride, gfx::SurfaceFormat aFormat,
    uint8_t* aBuffer, uint32_t aBufferLength, const Maybe<IntRect>& aCropRect,
    const ImageBitmapOptions& aOptions) {
  MOZ_ASSERT(NS_IsMainThread());

  // Copy and crop the source buffer into a SourceSurface.
  RefPtr<SourceSurface> rgbaSurface = CreateSurfaceFromRawData(
      aSize, aStride, aFormat, aBuffer, aBufferLength, aCropRect, aOptions);

  if (NS_WARN_IF(!rgbaSurface)) {
    return nullptr;
  }

  // Convert RGBA to BGRA
  RefPtr<DataSourceSurface> rgbaDataSurface = rgbaSurface->GetDataSurface();
  DataSourceSurface::ScopedMap rgbaMap(rgbaDataSurface,
                                       DataSourceSurface::READ);
  if (NS_WARN_IF(!rgbaMap.IsMapped())) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> bgraDataSurface =
      Factory::CreateDataSourceSurfaceWithStride(rgbaDataSurface->GetSize(),
                                                 SurfaceFormat::B8G8R8A8,
                                                 rgbaMap.GetStride());
  if (NS_WARN_IF(!bgraDataSurface)) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap bgraMap(bgraDataSurface,
                                       DataSourceSurface::WRITE);
  if (NS_WARN_IF(!bgraMap.IsMapped())) {
    return nullptr;
  }

  SwizzleData(rgbaMap.GetData(), rgbaMap.GetStride(), SurfaceFormat::R8G8B8A8,
              bgraMap.GetData(), bgraMap.GetStride(), SurfaceFormat::B8G8R8A8,
              bgraDataSurface->GetSize());

  // Create an Image from the BGRA SourceSurface.
  return CreateImageFromSurface(bgraDataSurface);
}

/*
 * This is a synchronous task.
 * This class is used to create a layers::SourceSurfaceImage from raw data in
 * the main thread. While creating an ImageBitmap from an ImageData, we need to
 * create a SouceSurface from the ImageData's raw data and then set the
 * SourceSurface into a layers::SourceSurfaceImage. However, the
 * layers::SourceSurfaceImage asserts the setting operation in the main thread,
 * so if we are going to create an ImageBitmap from an ImageData off the main
 * thread, we post an event to the main thread to create a
 * layers::SourceSurfaceImage from an ImageData's raw data.
 */
class CreateImageFromRawDataInMainThreadSyncTask final
    : public WorkerMainThreadRunnable {
 public:
  CreateImageFromRawDataInMainThreadSyncTask(
      uint8_t* aBuffer, uint32_t aBufferLength, uint32_t aStride,
      gfx::SurfaceFormat aFormat, const gfx::IntSize& aSize,
      const Maybe<IntRect>& aCropRect, layers::Image** aImage,
      const ImageBitmapOptions& aOptions)
      : WorkerMainThreadRunnable(
            GetCurrentThreadWorkerPrivate(),
            "ImageBitmap :: Create Image from Raw Data"_ns),
        mImage(aImage),
        mBuffer(aBuffer),
        mBufferLength(aBufferLength),
        mStride(aStride),
        mFormat(aFormat),
        mSize(aSize),
        mCropRect(aCropRect),
        mOptions(aOptions) {
    MOZ_ASSERT(!(*aImage),
               "Don't pass an existing Image into "
               "CreateImageFromRawDataInMainThreadSyncTask.");
  }

  bool MainThreadRun() override {
    RefPtr<layers::Image> image = CreateImageFromRawData(
        mSize, mStride, mFormat, mBuffer, mBufferLength, mCropRect, mOptions);

    if (NS_WARN_IF(!image)) {
      return false;
    }

    image.forget(mImage);

    return true;
  }

 private:
  layers::Image** mImage;
  uint8_t* mBuffer;
  uint32_t mBufferLength;
  uint32_t mStride;
  gfx::SurfaceFormat mFormat;
  gfx::IntSize mSize;
  const Maybe<IntRect>& mCropRect;
  const ImageBitmapOptions mOptions;
};

/*
 * A wrapper to the nsLayoutUtils::SurfaceFromElement() function followed by the
 * security checking.
 */
template <class ElementType>
static already_AddRefed<SourceSurface> GetSurfaceFromElement(
    nsIGlobalObject* aGlobal, ElementType& aElement, bool* aWriteOnly,
    const ImageBitmapOptions& aOptions, gfxAlphaType* aAlphaType,
    ErrorResult& aRv) {
  uint32_t flags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE |
                   nsLayoutUtils::SFE_ORIENTATION_FROM_IMAGE |
                   nsLayoutUtils::SFE_EXACT_SIZE_SURFACE;

  // by default surfaces have premultiplied alpha
  // attempt to get non premultiplied if required
  if (aOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
    flags |= nsLayoutUtils::SFE_ALLOW_NON_PREMULT;
  }

  if (aOptions.mColorSpaceConversion == ColorSpaceConversion::None &&
      aElement.IsHTMLElement(nsGkAtoms::img)) {
    flags |= nsLayoutUtils::SFE_NO_COLORSPACE_CONVERSION;
  }

  Maybe<int32_t> resizeWidth, resizeHeight;
  if (aOptions.mResizeWidth.WasPassed()) {
    if (!CheckedInt32(aOptions.mResizeWidth.Value()).isValid()) {
      aRv.ThrowInvalidStateError("resizeWidth is too large");
      return nullptr;
    }
    resizeWidth.emplace(aOptions.mResizeWidth.Value());
  }
  if (aOptions.mResizeHeight.WasPassed()) {
    if (!CheckedInt32(aOptions.mResizeHeight.Value()).isValid()) {
      aRv.ThrowInvalidStateError("resizeHeight is too large");
      return nullptr;
    }
    resizeHeight.emplace(aOptions.mResizeHeight.Value());
  }
  SurfaceFromElementResult res = nsLayoutUtils::SurfaceFromElement(
      &aElement, resizeWidth, resizeHeight, flags);

  RefPtr<SourceSurface> surface = res.GetSourceSurface();
  if (NS_WARN_IF(!surface)) {
    aRv.ThrowInvalidStateError("The source image could not be decoded");
    return nullptr;
  }

  *aWriteOnly = res.mIsWriteOnly;
  *aAlphaType = res.mAlphaType;

  return surface.forget();
}

ImageBitmap::ImageBitmap(nsIGlobalObject* aGlobal, layers::Image* aData,
                         bool aAllocatedImageData, bool aWriteOnly,
                         gfxAlphaType aAlphaType)
    : mParent(aGlobal),
      mData(aData),
      mSurface(nullptr),
      mPictureRect(aData->GetPictureRect()),
      mAlphaType(aAlphaType),
      mAllocatedImageData(aAllocatedImageData),
      mWriteOnly(aWriteOnly) {
  MOZ_ASSERT(aData, "aData is null in ImageBitmap constructor.");

  StaticMutexAutoLock lock(sShutdownMutex);
  if (!sShutdownObserver &&
      !AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
    sShutdownObserver = new ImageBitmapShutdownObserver();
    sShutdownObserver->Init();
  }
  if (sShutdownObserver) {
    mShutdownRunnable = sShutdownObserver->Track(this);
  }
}

ImageBitmap::~ImageBitmap() {
  StaticMutexAutoLock lock(sShutdownMutex);
  if (mShutdownRunnable) {
    mShutdownRunnable->mImageBitmap = nullptr;
  }
  mShutdownRunnable = nullptr;
  if (sShutdownObserver) {
    sShutdownObserver->Untrack(this);
  }
}

JSObject* ImageBitmap::WrapObject(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return ImageBitmap_Binding::Wrap(aCx, this, aGivenProto);
}

void ImageBitmap::Close() {
  RemoveAssociatedMemory();
  mData = nullptr;
  mSurface = nullptr;
  mPictureRect.SetEmpty();
}

void ImageBitmap::OnShutdown() { Close(); }

void ImageBitmap::SetPictureRect(const IntRect& aRect, ErrorResult& aRv) {
  mPictureRect = FixUpNegativeDimension(aRect, aRv);
  mSurface = nullptr;
}

SurfaceFromElementResult ImageBitmap::SurfaceFrom(uint32_t aSurfaceFlags) {
  SurfaceFromElementResult sfer;

  if (!mData) {
    return sfer;
  }

  // An ImageBitmap, not being a DOM element, only has `origin-clean`
  // (via our `IsWriteOnly`), and does not participate in CORS.
  // Right now we mark this by setting mCORSUsed to true.
  sfer.mCORSUsed = true;
  sfer.mIsWriteOnly = mWriteOnly;

  if (mParent) {
    sfer.mPrincipal = mParent->PrincipalOrNull();
  }

  IntSize imageSize(mData->GetSize());
  IntRect imageRect(IntPoint(0, 0), imageSize);
  bool hasCropRect = !mPictureRect.IsEqualEdges(imageRect);

  bool wantExactSize =
      bool(aSurfaceFlags & nsLayoutUtils::SFE_EXACT_SIZE_SURFACE);
  bool allowNonPremult =
      bool(aSurfaceFlags & nsLayoutUtils::SFE_ALLOW_NON_PREMULT);
  bool allowUncropped =
      bool(aSurfaceFlags & nsLayoutUtils::SFE_ALLOW_UNCROPPED_UNSCALED);
  enum class Premultiplication { NotNeeded, Required, Skipped };
  Premultiplication premultiplication = Premultiplication::NotNeeded;
  if (mAlphaType == gfxAlphaType::NonPremult) {
    premultiplication = allowNonPremult ? Premultiplication::Skipped
                                        : Premultiplication::Required;
  }
  bool requiresCrop = !allowUncropped && hasCropRect;
  if (wantExactSize || requiresCrop ||
      premultiplication == Premultiplication::Required) {
    if (premultiplication == Premultiplication::Skipped) {
      RefPtr<SourceSurface> surface = mData->GetAsSourceSurface();
      RefPtr<DataSourceSurface> dataSurface =
          surface ? surface->GetDataSurface() : nullptr;
      if (dataSurface && hasCropRect) {
        dataSurface = CropAndCopyDataSourceSurface(dataSurface, mPictureRect);
      }
      sfer.mSourceSurface = std::move(dataSurface);
    } else {
      RefPtr<DrawTarget> dt = Factory::CreateDrawTarget(
          BackendType::SKIA, IntSize(1, 1), SurfaceFormat::B8G8R8A8);
      sfer.mSourceSurface = PrepareForDrawTarget(dt);
    }

    if (!sfer.mSourceSurface) {
      return sfer;
    }

    // mSurface caches the premultiplied result of PrepareForDrawTarget().
    MOZ_ASSERT(premultiplication == Premultiplication::Skipped || mSurface);

    sfer.mSize = sfer.mIntrinsicSize = sfer.mSourceSurface->GetSize();
    sfer.mHasSize = true;
    if (premultiplication == Premultiplication::Skipped) {
      sfer.mAlphaType = gfxAlphaType::NonPremult;
    } else {
      sfer.mAlphaType = IsOpaque(sfer.mSourceSurface->GetFormat())
                            ? gfxAlphaType::Opaque
                            : gfxAlphaType::Premult;
    }
    return sfer;
  }

  if (hasCropRect) {
    IntRect imagePortion = imageRect.Intersect(mPictureRect);

    // the crop lies entirely outside the surface area, nothing to draw
    if (imagePortion.IsEmpty()) {
      return sfer;
    }

    sfer.mCropRect = Some(imagePortion);
    sfer.mIntrinsicSize = imagePortion.Size();
  } else {
    sfer.mIntrinsicSize = imageSize;
  }

  sfer.mSize = imageSize;
  sfer.mHasSize = true;
  sfer.mAlphaType = mAlphaType;
  sfer.mLayersImage = mData;
  return sfer;
}

/*
 * The functionality of PrepareForDrawTarget method:
 * (1) Get a SourceSurface from the mData (which is a layers::Image).
 * (2) Convert the SourceSurface to format B8G8R8A8 if the original format is
 *     R8G8B8, B8G8R8, HSV or Lab.
 *     Note: if the original format is A8 or Depth, then return null directly.
 * (3) Do cropping if the size of SourceSurface does not equal to the
 *     mPictureRect.
 * (4) Pre-multiply alpha if needed.
 */
already_AddRefed<SourceSurface> ImageBitmap::PrepareForDrawTarget(
    gfx::DrawTarget* aTarget) {
  MOZ_ASSERT(aTarget);

  if (!mData) {
    return nullptr;
  }

  // We may have a cached surface optimized for the last DrawTarget. If it was
  // created via DrawTargetRecording, it may not be suitable for use with the
  // current DrawTarget, as we can only do readbacks via the
  // PersistentBufferProvider for the canvas, and not for individual
  // SourceSurfaceRecording objects. In such situations, the only thing we can
  // do is clear our cache and extract a new SourceSurface from mData.
  if (mSurface &&
      mSurface->GetUnderlyingType() == gfx::SurfaceType::RECORDING &&
      !aTarget->IsRecording()) {
    RefPtr<gfx::DataSourceSurface> dataSurface = mSurface->GetDataSurface();
    if (!dataSurface) {
      mSurface = nullptr;
    }
  }

  // If we have a surface, then it is already cropped and premultiplied.
  if (mSurface) {
    return do_AddRef(mSurface);
  }

  RefPtr<gfx::SourceSurface> surface = mData->GetAsSourceSurface();
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }

  IntRect surfRect(0, 0, surface->GetSize().width, surface->GetSize().height);
  SurfaceFormat format = surface->GetFormat();
  bool isOpaque = IsOpaque(format);
  bool mustPremultiply = mAlphaType == gfxAlphaType::NonPremult && !isOpaque;
  bool hasCopied = false;

  // Check if we still need to crop our surface
  if (!mPictureRect.IsEqualEdges(surfRect)) {
    IntRect surfPortion = surfRect.Intersect(mPictureRect);

    IntPoint dest(std::max(0, surfPortion.X() - mPictureRect.X()),
                  std::max(0, surfPortion.Y() - mPictureRect.Y()));

    // We must initialize this target with mPictureRect.Size() because the
    // specification states that if the cropping area is given, then return an
    // ImageBitmap with the size equals to the cropping area. Ensure that the
    // format matches the surface, even though the DT type is similar to the
    // destination, i.e. blending an alpha surface to an opaque DT. However,
    // any pixels outside the surface portion must be filled with transparent
    // black, even if the surface is opaque, so force to an alpha format in
    // that case.
    if (!surfPortion.IsEqualEdges(mPictureRect) && isOpaque) {
      format = SurfaceFormat::B8G8R8A8;
    }

    // If we need to pre-multiply the alpha, then we need to be able to
    // read/write the pixel data, and as such, we want to avoid creating a
    // SourceSurfaceRecording for the same reasons earlier in this method.
    RefPtr<DrawTarget> cropped;
    if (mustPremultiply && aTarget->IsRecording()) {
      cropped = Factory::CreateDrawTarget(BackendType::SKIA,
                                          mPictureRect.Size(), format);
    } else {
      if (aTarget->CanCreateSimilarDrawTarget(mPictureRect.Size(), format)) {
        cropped = aTarget->CreateSimilarDrawTarget(mPictureRect.Size(), format);
      }
    }

    if (NS_WARN_IF(!cropped)) {
      return nullptr;
    }

    // A crop that lies entirely outside the surface area leaves the target
    // transparent black.
    if (!surfPortion.IsEmpty()) {
      cropped->CopySurface(surface, surfPortion, dest);
    }
    surface = cropped->GetBackingSurface();
    hasCopied = true;
    if (NS_WARN_IF(!surface)) {
      return nullptr;
    }
  }

  // Pre-multiply alpha here.
  // Ignore this step if the source surface does not have alpha channel; this
  // kind of source surfaces might come form layers::PlanarYCbCrImage. If the
  // crop rect imputed transparency, and the original surface was opaque, we
  // can skip doing the pre-multiply here as the only transparent pixels are
  // already transparent black.
  if (mustPremultiply) {
    MOZ_ASSERT(surface->GetFormat() == SurfaceFormat::R8G8B8A8 ||
               surface->GetFormat() == SurfaceFormat::B8G8R8A8 ||
               surface->GetFormat() == SurfaceFormat::A8R8G8B8);

    RefPtr<DataSourceSurface> srcSurface = surface->GetDataSurface();
    if (NS_WARN_IF(!srcSurface)) {
      return nullptr;
    }

    if (hasCopied) {
      // If we are using our own local copy, then we can safely premultiply in
      // place without an additional allocation.
      DataSourceSurface::ScopedMap map(srcSurface,
                                       DataSourceSurface::READ_WRITE);
      if (!map.IsMapped()) {
        gfxCriticalError() << "Failed to map surface for premultiplying alpha.";
        return nullptr;
      }

      PremultiplyData(map.GetData(), map.GetStride(), srcSurface->GetFormat(),
                      map.GetData(), map.GetStride(), srcSurface->GetFormat(),
                      surface->GetSize());
    } else {
      RefPtr<DataSourceSurface> dstSurface = Factory::CreateDataSourceSurface(
          srcSurface->GetSize(), srcSurface->GetFormat());
      if (NS_WARN_IF(!dstSurface)) {
        return nullptr;
      }

      DataSourceSurface::ScopedMap srcMap(srcSurface, DataSourceSurface::READ);
      if (!srcMap.IsMapped()) {
        gfxCriticalError()
            << "Failed to map source surface for premultiplying alpha.";
        return nullptr;
      }

      DataSourceSurface::ScopedMap dstMap(dstSurface, DataSourceSurface::WRITE);
      if (!dstMap.IsMapped()) {
        gfxCriticalError()
            << "Failed to map destination surface for premultiplying alpha.";
        return nullptr;
      }

      PremultiplyData(srcMap.GetData(), srcMap.GetStride(),
                      srcSurface->GetFormat(), dstMap.GetData(),
                      dstMap.GetStride(), dstSurface->GetFormat(),
                      dstSurface->GetSize());

      surface = std::move(dstSurface);
    }
  }

  // Replace our surface with one optimized for the target we're about to draw
  // to, under the assumption it'll likely be drawn again to that target.
  // This call should be a no-op for already-optimized surfaces
  mSurface = aTarget->OptimizeSourceSurface(surface);
  if (!mSurface) {
    mSurface = std::move(surface);
  }
  return do_AddRef(mSurface);
}

already_AddRefed<layers::Image> ImageBitmap::TransferAsImage() {
  RefPtr<layers::Image> image = mData;
  Close();
  return image.forget();
}

UniquePtr<ImageBitmapCloneData> ImageBitmap::ToCloneData() const {
  if (!mData) {
    // A closed image cannot be cloned.
    return nullptr;
  }

  RefPtr<SourceSurface> surface = mData->GetAsSourceSurface();
  if (!surface) {
    // It might just not be possible to get/map the surface. (e.g. from another
    // process)
    return nullptr;
  }

  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (NS_WARN_IF(!dataSurface)) {
    // This can reasonably fail in many cases (e.g. canvas state doesn't allow
    // reading back the snapshot).
    return nullptr;
  }

  auto result = std::make_unique<ImageBitmapCloneData>();
  result->mPictureRect = mPictureRect;
  result->mAlphaType = mAlphaType;
  result->mSurface = std::move(dataSurface);
  result->mWriteOnly = mWriteOnly;
  return result;
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateFromSourceSurface(
    nsIGlobalObject* aGlobal, gfx::SourceSurface* aSource, ErrorResult& aRv) {
  RefPtr<layers::Image> data = CreateImageFromSurface(aSource);
  RefPtr<ImageBitmap> ret =
      new ImageBitmap(aGlobal, data, true, false /* writeOnly */);
  return ret.forget();
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateFromCloneData(
    nsIGlobalObject* aGlobal, ImageBitmapCloneData* aData) {
  RefPtr<layers::Image> data = CreateImageFromSurface(aData->mSurface);

  RefPtr<ImageBitmap> ret = new ImageBitmap(
      aGlobal, data, true, aData->mWriteOnly, aData->mAlphaType);

  ErrorResult rv;
  ret->SetPictureRect(aData->mPictureRect, rv);
  return ret.forget();
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateFromOffscreenCanvas(
    nsIGlobalObject* aGlobal, OffscreenCanvas& aOffscreenCanvas,
    ErrorResult& aRv) {
  // Check write-only mode.
  bool writeOnly = aOffscreenCanvas.IsWriteOnly();
  uint32_t flags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE |
                   nsLayoutUtils::SFE_EXACT_SIZE_SURFACE;

  SurfaceFromElementResult res =
      nsLayoutUtils::SurfaceFromOffscreenCanvas(&aOffscreenCanvas, flags);

  RefPtr<SourceSurface> surface = res.GetSourceSurface();

  if (NS_WARN_IF(!surface)) {
    aRv.ThrowUnknownError("Failed to create ImageBitmap from OffscreenCanvas");
    return nullptr;
  }

  RefPtr<layers::Image> data = CreateImageFromSurface(surface);

  RefPtr<ImageBitmap> ret = new ImageBitmap(aGlobal, data, true, writeOnly);

  return ret.forget();
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateImageBitmapInternal(
    nsIGlobalObject* aGlobal, gfx::SourceSurface* aSurface,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    const bool aWriteOnly, const bool aAllocatedImageData, const bool aMustCopy,
    const gfxAlphaType aAlphaType, ErrorResult& aRv) {
  bool needToReportMemoryAllocation = aAllocatedImageData;
  const IntSize srcSize = aSurface->GetSize();
  IntRect cropRect =
      aCropRect.valueOr(IntRect(0, 0, srcSize.width, srcSize.height));

  RefPtr<SourceSurface> surface = aSurface;
  RefPtr<DataSourceSurface> dataSurface;

  // handle alpha premultiplication if surface not of correct type

  gfxAlphaType alphaType = aAlphaType;
  bool requiresPremultiply = false;
  bool requiresUnpremultiply = false;

  if (!IsOpaque(surface->GetFormat())) {
    if (aAlphaType == gfxAlphaType::Premult &&
        aOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
      requiresUnpremultiply = true;
      alphaType = gfxAlphaType::NonPremult;
    } else if (aAlphaType == gfxAlphaType::NonPremult &&
               aOptions.mPremultiplyAlpha == PremultiplyAlpha::Premultiply) {
      requiresPremultiply = true;
      alphaType = gfxAlphaType::Premult;
    }
  }

  /*
   * if we don't own the data and need to modify the buffer.
   * or
   * we need to crop and flip, where crop must come first.
   * or
   * the caller demands a copy (WebGL contexts).
   */
  bool willModify = aOptions.mImageOrientation == ImageOrientation::FlipY ||
                    requiresPremultiply || requiresUnpremultiply;
  if ((willModify && !aAllocatedImageData) ||
      ((aOptions.mImageOrientation == ImageOrientation::FlipY ||
        aOptions.mResizeWidth.WasPassed() ||
        aOptions.mResizeHeight.WasPassed()) &&
       aCropRect.isSome()) ||
      aMustCopy) {
    dataSurface = surface->GetDataSurface();

    dataSurface = CropAndCopyDataSourceSurface(dataSurface, cropRect);
    if (NS_WARN_IF(!dataSurface)) {
      aRv.ThrowInvalidStateError("Failed to crop source image");
      return nullptr;
    }

    surface = dataSurface;
    cropRect.SetRect(0, 0, dataSurface->GetSize().width,
                     dataSurface->GetSize().height);
    needToReportMemoryAllocation = true;
  }

  // flip image in Y direction
  if (aOptions.mImageOrientation == ImageOrientation::FlipY) {
    if (!dataSurface) {
      dataSurface = surface->GetDataSurface();
    }

    surface = FlipYDataSourceSurface(dataSurface);
    if (NS_WARN_IF(!surface)) {
      return nullptr;
    }
  }

  if (requiresPremultiply) {
    if (!dataSurface) {
      dataSurface = surface->GetDataSurface();
    }

    surface = AlphaPremultiplyDataSourceSurface(dataSurface, true);
    if (NS_WARN_IF(!surface)) {
      return nullptr;
    }
  }

  // resize if required
  if (aOptions.mResizeWidth.WasPassed() || aOptions.mResizeHeight.WasPassed()) {
    if (!dataSurface) {
      dataSurface = surface->GetDataSurface();
    };

    surface = ScaleDataSourceSurface(dataSurface, aOptions);
    if (NS_WARN_IF(!surface)) {
      aRv.ThrowInvalidStateError("Failed to create resized image");
      return nullptr;
    }

    needToReportMemoryAllocation = true;
    cropRect.SetRect(0, 0, surface->GetSize().width, surface->GetSize().height);
  }

  if (requiresUnpremultiply) {
    if (!dataSurface) {
      dataSurface = surface->GetDataSurface();
    }

    surface = AlphaPremultiplyDataSourceSurface(dataSurface, false);
    if (NS_WARN_IF(!surface)) {
      return nullptr;
    }
  }

  // Create an Image from the SourceSurface.
  RefPtr<layers::Image> data = CreateImageFromSurface(surface);
  RefPtr<ImageBitmap> ret = new ImageBitmap(
      aGlobal, data, needToReportMemoryAllocation, aWriteOnly, alphaType);

  // Set the picture rectangle.
  ret->SetPictureRect(cropRect, aRv);

  return ret.forget();
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, HTMLImageElement& aImageEl,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  // Check if the image element is completely available or not.
  if (!aImageEl.Complete()) {
    aRv.ThrowInvalidStateError("Passed-in image has not finished loading");
    return nullptr;
  }

  bool writeOnly = true;
  gfxAlphaType alphaType = gfxAlphaType::NonPremult;

  // Get the SourceSurface out from the image element and then do security
  // checking.
  RefPtr<SourceSurface> surface = GetSurfaceFromElement(
      aGlobal, aImageEl, &writeOnly, aOptions, &alphaType, aRv);

  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  bool needToReportMemoryAllocation = false;
  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   false, alphaType, aRv);
}
/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, SVGImageElement& aImageEl,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  bool writeOnly = true;
  gfxAlphaType alphaType = gfxAlphaType::NonPremult;

  // Get the SourceSurface out from the image element and then do security
  // checking.
  RefPtr<SourceSurface> surface = GetSurfaceFromElement(
      aGlobal, aImageEl, &writeOnly, aOptions, &alphaType, aRv);

  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  bool needToReportMemoryAllocation = false;

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   false, alphaType, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, HTMLVideoElement& aVideoEl,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  aVideoEl.LogVisibility(
      mozilla::dom::HTMLVideoElement::CallerAPI::CREATE_IMAGEBITMAP);

  // Check network state.
  if (aVideoEl.NetworkState() == NETWORK_EMPTY) {
    aRv.ThrowInvalidStateError("Passed-in video has not loaded any data");
    return nullptr;
  }

  // Check ready state.
  // Cannot be HTMLMediaElement::HAVE_NOTHING or
  // HTMLMediaElement::HAVE_METADATA.
  if (aVideoEl.ReadyState() <= HAVE_METADATA) {
    aRv.ThrowInvalidStateError("Passed-in video does not have enough data");
    return nullptr;
  }

  // Check security.
  nsCOMPtr<nsIPrincipal> principal = aVideoEl.GetCurrentVideoPrincipal();
  bool hadCrossOriginRedirects = aVideoEl.HadCrossOriginRedirects();
  bool CORSUsed = aVideoEl.GetCORSMode() != CORS_NONE;
  bool writeOnly = CanvasUtils::CheckWriteOnlySecurity(CORSUsed, principal,
                                                       hadCrossOriginRedirects);

  // Create ImageBitmap.
  RefPtr<layers::Image> data = aVideoEl.GetCurrentImage();
  if (!data) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  RefPtr<SourceSurface> surface = data->GetAsSourceSurface();
  if (!surface) {
    // preserve original behavior in case of unavailble surface
    RefPtr<ImageBitmap> ret = new ImageBitmap(aGlobal, data, false, writeOnly);
    return ret.forget();
  }

  bool needToReportMemoryAllocation = false;

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   false, gfxAlphaType::Premult, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, HTMLCanvasElement& aCanvasEl,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  if (aCanvasEl.Width() == 0) {
    aRv.ThrowInvalidStateError("Passed-in canvas has width 0");
    return nullptr;
  }

  if (aCanvasEl.Height() == 0) {
    aRv.ThrowInvalidStateError("Passed-in canvas has height 0");
    return nullptr;
  }

  bool writeOnly = true;
  gfxAlphaType alphaType = gfxAlphaType::NonPremult;

  RefPtr<SourceSurface> surface = GetSurfaceFromElement(
      aGlobal, aCanvasEl, &writeOnly, aOptions, &alphaType, aRv);

  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (!writeOnly) {
    writeOnly = aCanvasEl.IsWriteOnly();
  }

  // If the HTMLCanvasElement's rendering context is WebGL/WebGPU,
  // then the snapshot we got from the HTMLCanvasElement is
  // a DataSourceSurface which is a copy of the rendering context.
  // We handle cropping in this case.
  bool needToReportMemoryAllocation = false;
  bool mustCopy = false;

  if ((aCanvasEl.GetCurrentContextType() == CanvasContextType::WebGL1 ||
       aCanvasEl.GetCurrentContextType() == CanvasContextType::WebGL2 ||
       aCanvasEl.GetCurrentContextType() == CanvasContextType::WebGPU) &&
      aCropRect.isSome()) {
    mustCopy = true;
  }

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   mustCopy, alphaType, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, OffscreenCanvas& aOffscreenCanvas,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  if (aOffscreenCanvas.Width() == 0) {
    aRv.ThrowInvalidStateError("Passed-in canvas has width 0");
    return nullptr;
  }

  if (aOffscreenCanvas.Height() == 0) {
    aRv.ThrowInvalidStateError("Passed-in canvas has height 0");
    return nullptr;
  }

  uint32_t flags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE |
                   nsLayoutUtils::SFE_EXACT_SIZE_SURFACE;

  // by default surfaces have premultiplied alpha
  // attempt to get non premultiplied if required
  if (aOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
    flags |= nsLayoutUtils::SFE_ALLOW_NON_PREMULT;
  }

  SurfaceFromElementResult res =
      nsLayoutUtils::SurfaceFromOffscreenCanvas(&aOffscreenCanvas, flags);

  RefPtr<SourceSurface> surface = res.GetSourceSurface();
  if (NS_WARN_IF(!surface)) {
    aRv.ThrowInvalidStateError("Passed-in canvas failed to create snapshot");
    return nullptr;
  }

  gfxAlphaType alphaType = res.mAlphaType;
  bool writeOnly = res.mIsWriteOnly;

  // If the OffscreenCanvas's rendering context is WebGL/WebGPU, then the
  // snapshot we got from the OffscreenCanvas is a DataSourceSurface which
  // is a copy of the rendering context. We handle cropping in this case.
  bool needToReportMemoryAllocation = false;
  bool mustCopy =
      aCropRect.isSome() &&
      (aOffscreenCanvas.GetContextType() == CanvasContextType::WebGL1 ||
       aOffscreenCanvas.GetContextType() == CanvasContextType::WebGL2 ||
       aOffscreenCanvas.GetContextType() == CanvasContextType::WebGPU);

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   mustCopy, alphaType, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, ImageData& aImageData,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  // Copy data into SourceSurface.
  RootedSpiderMonkeyInterface<Uint8ClampedArray> array(RootingCx());
  if (!array.Init(aImageData.GetDataObject())) {
    aRv.ThrowInvalidStateError(
        "Failed to extract Uint8ClampedArray from ImageData (security check "
        "failed?)");
    return nullptr;
  }
  const SurfaceFormat FORMAT = SurfaceFormat::R8G8B8A8;
  // ImageData's underlying data is not alpha-premultiplied.
  auto alphaType = (aOptions.mPremultiplyAlpha == PremultiplyAlpha::Premultiply)
                       ? gfxAlphaType::Premult
                       : gfxAlphaType::NonPremult;

  const uint32_t BYTES_PER_PIXEL = BytesPerPixel(FORMAT);
  const uint32_t imageWidth = aImageData.Width();
  const uint32_t imageHeight = aImageData.Height();
  const uint32_t imageStride = imageWidth * BYTES_PER_PIXEL;
  const gfx::IntSize imageSize(imageWidth, imageHeight);

  // Check the ImageData is neutered or not.
  if (imageWidth == 0 || imageHeight == 0) {
    aRv.ThrowInvalidStateError("Passed-in image is empty");
    return nullptr;
  }

  return array.ProcessFixedData(
      [&](const Span<const uint8_t>& aData) -> already_AddRefed<ImageBitmap> {
        const uint32_t dataLength = aData.Length();
        if ((imageWidth * imageHeight * BYTES_PER_PIXEL) != dataLength) {
          aRv.ThrowInvalidStateError("Data size / image format mismatch");
          return nullptr;
        }

        // Create and Crop the raw data into a layers::Image
        RefPtr<layers::Image> data;

        uint8_t* fixedData = const_cast<uint8_t*>(aData.Elements());

        if (NS_IsMainThread()) {
          data =
              CreateImageFromRawData(imageSize, imageStride, FORMAT, fixedData,
                                     dataLength, aCropRect, aOptions);
        } else {
          RefPtr<CreateImageFromRawDataInMainThreadSyncTask> task =
              new CreateImageFromRawDataInMainThreadSyncTask(
                  fixedData, dataLength, imageStride, FORMAT, imageSize,
                  aCropRect, getter_AddRefs(data), aOptions);
          task->Dispatch(GetCurrentThreadWorkerPrivate(), Canceling, aRv);
        }

        if (NS_WARN_IF(!data)) {
          aRv.ThrowInvalidStateError("Failed to create internal image");
          return nullptr;
        }

        // Create an ImageBitmap.
        RefPtr<ImageBitmap> ret = new ImageBitmap(
            aGlobal, data, true, false /* write-only */, alphaType);

        // The cropping information has been handled in the
        // CreateImageFromRawData() function.

        return ret.forget();
      });
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, CanvasRenderingContext2D& aCanvasCtx,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(aGlobal);
  nsGlobalWindowInner* window = nsGlobalWindowInner::Cast(win);
  if (NS_WARN_IF(!window) || !window->GetExtantDoc()) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  window->GetExtantDoc()->WarnOnceAndReportAbout(
      DeprecatedOperations::eCreateImageBitmapCanvasRenderingContext2D);

  // Check write-only mode.
  bool writeOnly =
      aCanvasCtx.GetCanvas()->IsWriteOnly() || aCanvasCtx.IsWriteOnly();

  RefPtr<SourceSurface> surface = aCanvasCtx.GetSurfaceSnapshot();

  if (NS_WARN_IF(!surface)) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  const IntSize surfaceSize = surface->GetSize();
  if (surfaceSize.width == 0 || surfaceSize.height == 0) {
    aRv.ThrowInvalidStateError("Passed-in canvas has width 0 or height 0");
    return nullptr;
  }

  bool needToReportMemoryAllocation = false;

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   false, gfxAlphaType::Premult, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, ImageBitmap& aImageBitmap,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  if (!aImageBitmap.mData) {
    aRv.ThrowInvalidStateError(
        "Passed-in ImageBitmap has been closed or transferred");
    return nullptr;
  }

  IntRect cropRect;
  RefPtr<SourceSurface> surface;
  RefPtr<DataSourceSurface> dataSurface;
  gfxAlphaType alphaType;

  bool needToReportMemoryAllocation = false;

  if (aImageBitmap.mSurface &&
      (dataSurface = aImageBitmap.mSurface->GetDataSurface())) {
    // the source imageBitmap already has a cropped surface, and we can get a
    // DataSourceSurface from it, so just use it directly
    surface = aImageBitmap.mSurface;
    cropRect = aCropRect.valueOr(IntRect(IntPoint(0, 0), surface->GetSize()));
    alphaType = IsOpaque(surface->GetFormat()) ? gfxAlphaType::Opaque
                                               : gfxAlphaType::Premult;
  } else {
    RefPtr<layers::Image> data = aImageBitmap.mData;
    surface = data->GetAsSourceSurface();
    if (NS_WARN_IF(!surface)) {
      aRv.Throw(NS_ERROR_NOT_AVAILABLE);
      return nullptr;
    }

    cropRect = aImageBitmap.mPictureRect;
    alphaType = aImageBitmap.mAlphaType;
    if (aCropRect.isSome()) {
      // get new crop rect relative to original uncropped surface
      IntRect newCropRect = aCropRect.ref();
      newCropRect = FixUpNegativeDimension(newCropRect, aRv);

      newCropRect.MoveBy(cropRect.X(), cropRect.Y());

      if (cropRect.Contains(newCropRect)) {
        // new crop region within existing surface
        // safe to just crop this with new rect
        cropRect = newCropRect;
      } else {
        // crop includes area outside original cropped region
        // create new surface cropped by original bitmap crop rect
        RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();

        surface = CropAndCopyDataSourceSurface(dataSurface, cropRect);
        if (NS_WARN_IF(!surface)) {
          aRv.Throw(NS_ERROR_NOT_AVAILABLE);
          return nullptr;
        }
        needToReportMemoryAllocation = true;
        cropRect = aCropRect.ref();
      }
    }
  }

  return CreateImageBitmapInternal(
      aGlobal, surface, Some(cropRect), aOptions, aImageBitmap.mWriteOnly,
      needToReportMemoryAllocation, false, alphaType, aRv);
}

/* static */
already_AddRefed<ImageBitmap> ImageBitmap::CreateInternal(
    nsIGlobalObject* aGlobal, VideoFrame& aVideoFrame,
    const Maybe<IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  if (aVideoFrame.CodedWidth() == 0) {
    aRv.ThrowInvalidStateError("Passed-in video frame has width 0");
    return nullptr;
  }

  if (aVideoFrame.CodedHeight() == 0) {
    aRv.ThrowInvalidStateError("Passed-in video frame has height 0");
    return nullptr;
  }

  uint32_t flags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE;

  // by default surfaces have premultiplied alpha
  // attempt to get non premultiplied if required
  if (aOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
    flags |= nsLayoutUtils::SFE_ALLOW_NON_PREMULT;
  }

  SurfaceFromElementResult res =
      nsLayoutUtils::SurfaceFromVideoFrame(&aVideoFrame, flags);

  RefPtr<SourceSurface> surface = res.GetSourceSurface();
  if (NS_WARN_IF(!surface)) {
    aRv.ThrowInvalidStateError("Passed-in video frame has no surface data");
    return nullptr;
  }

  gfxAlphaType alphaType = res.mAlphaType;
  bool writeOnly = res.mIsWriteOnly;
  bool needToReportMemoryAllocation = false;
  bool mustCopy = false;

  return CreateImageBitmapInternal(aGlobal, surface, aCropRect, aOptions,
                                   writeOnly, needToReportMemoryAllocation,
                                   mustCopy, alphaType, aRv);
}

class FulfillImageBitmapPromise {
 protected:
  FulfillImageBitmapPromise(Promise* aPromise, ImageBitmap* aImageBitmap)
      : mPromise(aPromise), mImageBitmap(aImageBitmap) {
    MOZ_ASSERT(aPromise);
  }

  void DoFulfillImageBitmapPromise() { mPromise->MaybeResolve(mImageBitmap); }

 private:
  RefPtr<Promise> mPromise;
  RefPtr<ImageBitmap> mImageBitmap;
};

class FulfillImageBitmapPromiseTask final : public Runnable,
                                            public FulfillImageBitmapPromise {
 public:
  FulfillImageBitmapPromiseTask(Promise* aPromise, ImageBitmap* aImageBitmap)
      : Runnable("dom::FulfillImageBitmapPromiseTask"),
        FulfillImageBitmapPromise(aPromise, aImageBitmap) {}

  NS_IMETHOD Run() override {
    DoFulfillImageBitmapPromise();
    return NS_OK;
  }
};

class FulfillImageBitmapPromiseWorkerTask final
    : public WorkerSameThreadRunnable,
      public FulfillImageBitmapPromise {
 public:
  FulfillImageBitmapPromiseWorkerTask(Promise* aPromise,
                                      ImageBitmap* aImageBitmap)
      : WorkerSameThreadRunnable("FulfillImageBitmapPromiseWorkerTask"),
        FulfillImageBitmapPromise(aPromise, aImageBitmap) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    DoFulfillImageBitmapPromise();
    return true;
  }
};

static void AsyncFulfillImageBitmapPromise(Promise* aPromise,
                                           ImageBitmap* aImageBitmap) {
  if (NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> task =
        new FulfillImageBitmapPromiseTask(aPromise, aImageBitmap);
    NS_DispatchToCurrentThread(task);  // Actually, to the main-thread.
  } else {
    RefPtr<FulfillImageBitmapPromiseWorkerTask> task =
        new FulfillImageBitmapPromiseWorkerTask(aPromise, aImageBitmap);
    task->Dispatch(GetCurrentThreadWorkerPrivate());  // Actually, to the
                                                      // current worker-thread.
  }
}

class CreateImageBitmapFromBlobRunnable;

class CreateImageBitmapFromBlob final : public DiscardableRunnable,
                                        public imgIContainerCallback,
                                        public nsIInputStreamCallback {
  friend class CreateImageBitmapFromBlobRunnable;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_IMGICONTAINERCALLBACK
  NS_DECL_NSIINPUTSTREAMCALLBACK

  static already_AddRefed<CreateImageBitmapFromBlob> Create(
      Promise* aPromise, nsIGlobalObject* aGlobal, Blob& aBlob,
      const Maybe<IntRect>& aCropRect, nsIEventTarget* aMainThreadEventTarget,
      const ImageBitmapOptions& aOptions);

  NS_IMETHOD Run() override {
    MOZ_ASSERT(IsCurrentThread());

    nsresult rv = StartMimeTypeAndDecodeAndCropBlob();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MimeTypeAndDecodeAndCropBlobCompletedMainThread(nullptr, rv);
    }

    return NS_OK;
  }

  // Called by the WorkerRef.
  void WorkerShuttingDown();

 private:
  CreateImageBitmapFromBlob(Promise* aPromise, nsIGlobalObject* aGlobal,
                            already_AddRefed<nsIInputStream> aInputStream,
                            const Maybe<IntRect>& aCropRect,
                            nsIEventTarget* aMainThreadEventTarget,
                            const ImageBitmapOptions& aOptions)
      : DiscardableRunnable("dom::CreateImageBitmapFromBlob"),

        mMutex("dom::CreateImageBitmapFromBlob::mMutex"),
        mPromise(aPromise),
        mGlobalObject(aGlobal),
        mInputStream(std::move(aInputStream)),
        mCropRect(aCropRect),
        mMainThreadEventTarget(aMainThreadEventTarget),
        mOptions(aOptions),
        mThread(PR_GetCurrentThread()) {}

  virtual ~CreateImageBitmapFromBlob() = default;

  bool IsCurrentThread() const { return mThread == PR_GetCurrentThread(); }

  // Called on the owning thread.
  nsresult StartMimeTypeAndDecodeAndCropBlob();

  // Will be called when the decoding + cropping is completed on the
  // main-thread. This could the not the owning thread!
  void MimeTypeAndDecodeAndCropBlobCompletedMainThread(layers::Image* aImage,
                                                       nsresult aStatus);

  // Will be called when the decoding + cropping is completed on the owning
  // thread.
  void MimeTypeAndDecodeAndCropBlobCompletedOwningThread(layers::Image* aImage,
                                                         nsresult aStatus);

  // This is called on the main-thread only.
  nsresult MimeTypeAndDecodeAndCropBlob();

  // This is called on the main-thread only.
  nsresult DecodeAndCropBlob(const nsACString& aMimeType);

  // This is called on the main-thread only.
  nsresult GetMimeTypeSync(nsACString& aMimeType);

  // This is called on the main-thread only.
  nsresult GetMimeTypeAsync();

  Mutex mMutex MOZ_UNANNOTATED;

  // The access to this object is protected by mutex but is always nullified on
  // the owning thread.
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;

  // Touched only on the owning thread.
  RefPtr<Promise> mPromise;

  // Touched only on the owning thread.
  nsCOMPtr<nsIGlobalObject> mGlobalObject;

  nsCOMPtr<nsIInputStream> mInputStream;
  Maybe<IntRect> mCropRect;
  nsCOMPtr<nsIEventTarget> mMainThreadEventTarget;
  const ImageBitmapOptions mOptions;
  void* mThread;
};

NS_IMPL_ISUPPORTS_INHERITED(CreateImageBitmapFromBlob, DiscardableRunnable,
                            imgIContainerCallback, nsIInputStreamCallback)

class CreateImageBitmapFromBlobRunnable final : public WorkerThreadRunnable {
 public:
  explicit CreateImageBitmapFromBlobRunnable(CreateImageBitmapFromBlob* aTask,
                                             layers::Image* aImage,
                                             nsresult aStatus)
      : WorkerThreadRunnable("CreateImageBitmapFromBlobRunnable"),
        mTask(aTask),
        mImage(aImage),
        mStatus(aStatus) {}

  // Override Predispatch/PostDispatch to remove the noise of assertion for
  // nested Worker.
  virtual bool PreDispatch(WorkerPrivate*) override { return true; }
  virtual void PostDispatch(WorkerPrivate*, bool) override {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    mTask->MimeTypeAndDecodeAndCropBlobCompletedOwningThread(mImage, mStatus);
    return true;
  }

 private:
  RefPtr<CreateImageBitmapFromBlob> mTask;
  RefPtr<layers::Image> mImage;
  nsresult mStatus;
};

static void AsyncCreateImageBitmapFromBlob(Promise* aPromise,
                                           nsIGlobalObject* aGlobal,
                                           Blob& aBlob,
                                           const Maybe<IntRect>& aCropRect,
                                           const ImageBitmapOptions& aOptions) {
  // Let's identify the main-thread event target.
  nsCOMPtr<nsIEventTarget> mainThreadEventTarget;
  if (NS_IsMainThread()) {
    mainThreadEventTarget = aGlobal->SerialEventTarget();
  } else {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    mainThreadEventTarget = workerPrivate->MainThreadEventTarget();
  }

  RefPtr<CreateImageBitmapFromBlob> task = CreateImageBitmapFromBlob::Create(
      aPromise, aGlobal, aBlob, aCropRect, mainThreadEventTarget, aOptions);
  if (NS_WARN_IF(!task)) {
    aPromise->MaybeRejectWithInvalidStateError(
        "Failed to create blob decoding task");
    return;
  }

  NS_DispatchToCurrentThread(task);
}

/* static */
already_AddRefed<Promise> ImageBitmap::Create(
    nsIGlobalObject* aGlobal, const ImageBitmapSource& aSrc,
    const Maybe<gfx::IntRect>& aCropRect, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  MOZ_ASSERT(aGlobal);

  RefPtr<Promise> promise = Promise::Create(aGlobal, aRv);

  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (aCropRect.isSome()) {
    if (aCropRect->Width() == 0) {
      aRv.ThrowRangeError(
          "The crop rect width passed to createImageBitmap must be nonzero");
      return promise.forget();
    }

    if (aCropRect->Height() == 0) {
      aRv.ThrowRangeError(
          "The crop rect height passed to createImageBitmap must be nonzero");
      return promise.forget();
    }
  }

  if (aOptions.mResizeWidth.WasPassed() && aOptions.mResizeWidth.Value() == 0) {
    aRv.ThrowInvalidStateError(
        "The resizeWidth passed to createImageBitmap must be nonzero");
    return promise.forget();
  }

  if (aOptions.mResizeHeight.WasPassed() &&
      aOptions.mResizeHeight.Value() == 0) {
    aRv.ThrowInvalidStateError(
        "The resizeHeight passed to createImageBitmap must be nonzero");
    return promise.forget();
  }

  RefPtr<ImageBitmap> imageBitmap;

  if (aSrc.IsHTMLImageElement()) {
    MOZ_ASSERT(
        NS_IsMainThread(),
        "Creating ImageBitmap from HTMLImageElement off the main thread.");
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsHTMLImageElement(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsSVGImageElement()) {
    MOZ_ASSERT(
        NS_IsMainThread(),
        "Creating ImageBitmap from SVGImageElement off the main thread.");
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsSVGImageElement(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsHTMLVideoElement()) {
    MOZ_ASSERT(
        NS_IsMainThread(),
        "Creating ImageBitmap from HTMLVideoElement off the main thread.");
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsHTMLVideoElement(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsHTMLCanvasElement()) {
    MOZ_ASSERT(
        NS_IsMainThread(),
        "Creating ImageBitmap from HTMLCanvasElement off the main thread.");
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsHTMLCanvasElement(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsOffscreenCanvas()) {
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsOffscreenCanvas(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsImageData()) {
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsImageData(), aCropRect,
                                 aOptions, aRv);
  } else if (aSrc.IsCanvasRenderingContext2D()) {
    MOZ_ASSERT(NS_IsMainThread(),
               "Creating ImageBitmap from CanvasRenderingContext2D off the "
               "main thread.");
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsCanvasRenderingContext2D(),
                                 aCropRect, aOptions, aRv);
  } else if (aSrc.IsImageBitmap()) {
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsImageBitmap(), aCropRect,
                                 aOptions, aRv);
  } else if (aSrc.IsBlob()) {
    AsyncCreateImageBitmapFromBlob(promise, aGlobal, aSrc.GetAsBlob(),
                                   aCropRect, aOptions);
    return promise.forget();
  } else if (aSrc.IsVideoFrame()) {
    imageBitmap = CreateInternal(aGlobal, aSrc.GetAsVideoFrame(), aCropRect,
                                 aOptions, aRv);
  } else {
    MOZ_CRASH("Unsupported type!");
    return nullptr;
  }

  if (!aRv.Failed()) {
    AsyncFulfillImageBitmapPromise(promise, imageBitmap);
  }

  return promise.forget();
}

/*static*/
JSObject* ImageBitmap::ReadStructuredClone(
    JSContext* aCx, JSStructuredCloneReader* aReader, nsIGlobalObject* aParent,
    const nsTArray<RefPtr<DataSourceSurface>>& aClonedSurfaces,
    uint32_t aIndex) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aReader);
  // aParent might be null.

  uint32_t picRectX_;
  uint32_t picRectY_;
  uint32_t picRectWidth_;
  uint32_t picRectHeight_;
  uint32_t alphaType_;
  uint32_t writeOnly;

  if (!JS_ReadUint32Pair(aReader, &picRectX_, &picRectY_) ||
      !JS_ReadUint32Pair(aReader, &picRectWidth_, &picRectHeight_) ||
      !JS_ReadUint32Pair(aReader, &alphaType_, &writeOnly)) {
    return nullptr;
  }

  int32_t picRectX = BitwiseCast<int32_t>(picRectX_);
  int32_t picRectY = BitwiseCast<int32_t>(picRectY_);
  int32_t picRectWidth = BitwiseCast<int32_t>(picRectWidth_);
  int32_t picRectHeight = BitwiseCast<int32_t>(picRectHeight_);
  const auto alphaType = BitwiseCast<gfxAlphaType>(alphaType_);

  // Create a new ImageBitmap.
  MOZ_ASSERT(!aClonedSurfaces.IsEmpty());
  MOZ_ASSERT(aIndex < aClonedSurfaces.Length());

  // RefPtr<ImageBitmap> needs to go out of scope before toObjectOrNull() is
  // called because the static analysis thinks dereferencing XPCOM objects
  // can GC (because in some cases it can!), and a return statement with a
  // JSObject* type means that JSObject* is on the stack as a raw pointer
  // while destructors are running.
  JS::Rooted<JS::Value> value(aCx);
  {
#ifdef FUZZING
    if (aIndex >= aClonedSurfaces.Length()) {
      return nullptr;
    }
#endif
    RefPtr<layers::Image> img = CreateImageFromSurface(aClonedSurfaces[aIndex]);
    RefPtr<ImageBitmap> imageBitmap =
        new ImageBitmap(aParent, img, true, !!writeOnly, alphaType);

    ErrorResult error;
    imageBitmap->SetPictureRect(
        IntRect(picRectX, picRectY, picRectWidth, picRectHeight), error);
    if (NS_WARN_IF(error.Failed())) {
      error.SuppressException();
      return nullptr;
    }

    if (!GetOrCreateDOMReflector(aCx, imageBitmap, &value)) {
      return nullptr;
    }
  }

  return &(value.toObject());
}

/*static*/
void ImageBitmap::WriteStructuredClone(
    JSStructuredCloneWriter* aWriter,
    nsTArray<RefPtr<DataSourceSurface>>& aClonedSurfaces,
    ImageBitmap* aImageBitmap, ErrorResult& aRv) {
  MOZ_ASSERT(aWriter);
  MOZ_ASSERT(aImageBitmap);

  if (aImageBitmap->IsWriteOnly()) {
    return aRv.ThrowDataCloneError("Cannot clone ImageBitmap, is write-only");
  }

  if (!aImageBitmap->mData) {
    // A closed image cannot be cloned.
    return aRv.ThrowDataCloneError("Cannot clone ImageBitmap, is closed");
  }

  const uint32_t picRectX = BitwiseCast<uint32_t>(aImageBitmap->mPictureRect.x);
  const uint32_t picRectY = BitwiseCast<uint32_t>(aImageBitmap->mPictureRect.y);
  const uint32_t picRectWidth =
      BitwiseCast<uint32_t>(aImageBitmap->mPictureRect.width);
  const uint32_t picRectHeight =
      BitwiseCast<uint32_t>(aImageBitmap->mPictureRect.height);
  const uint32_t alphaType = BitwiseCast<uint32_t>(aImageBitmap->mAlphaType);

  // Indexing the cloned surfaces and send the index to the receiver.
  uint32_t index = aClonedSurfaces.Length();

  if (NS_WARN_IF(!JS_WriteUint32Pair(aWriter, SCTAG_DOM_IMAGEBITMAP, index)) ||
      NS_WARN_IF(!JS_WriteUint32Pair(aWriter, picRectX, picRectY)) ||
      NS_WARN_IF(!JS_WriteUint32Pair(aWriter, picRectWidth, picRectHeight)) ||
      NS_WARN_IF(
          !JS_WriteUint32Pair(aWriter, alphaType, aImageBitmap->mWriteOnly))) {
    return aRv.ThrowDataCloneError(
        "Cannot clone ImageBitmap, failed to write params");
  }

  RefPtr<SourceSurface> surface = aImageBitmap->mData->GetAsSourceSurface();
  if (NS_WARN_IF(!surface)) {
    return aRv.ThrowDataCloneError("Cannot clone ImageBitmap, no surface");
  }

  RefPtr<DataSourceSurface> snapshot = surface->GetDataSurface();
  if (NS_WARN_IF(!snapshot)) {
    return aRv.ThrowDataCloneError("Cannot clone ImageBitmap, no data surface");
  }

  RefPtr<DataSourceSurface> dstDataSurface;
  {
    // DataSourceSurfaceD2D1::GetStride() will call EnsureMapped implicitly and
    // won't Unmap after exiting function. So instead calling GetStride()
    // directly, using ScopedMap to get stride.
    DataSourceSurface::ScopedMap map(snapshot, DataSourceSurface::READ);
    if (NS_WARN_IF(!map.IsMapped())) {
      return aRv.ThrowDataCloneError(
          "Cannot clone ImageBitmap, cannot map surface");
    }

    dstDataSurface = Factory::CreateDataSourceSurfaceWithStride(
        snapshot->GetSize(), snapshot->GetFormat(), map.GetStride(), true);
  }
  if (NS_WARN_IF(!dstDataSurface)) {
    return aRv.ThrowDataCloneError("Cannot clone ImageBitmap, out of memory");
  }
  Factory::CopyDataSourceSurface(snapshot, dstDataSurface);
  aClonedSurfaces.AppendElement(dstDataSurface);
}

size_t ImageBitmap::GetAllocatedSize() const {
  if (!mAllocatedImageData || !mData) {
    return 0;
  }

  // Calculate how many bytes are used.
  switch (mData->GetFormat()) {
    case ImageFormat::PLANAR_YCBCR:
      return mData->AsPlanarYCbCrImage()->GetDataSize();
    case ImageFormat::NV_IMAGE:
      return mData->AsNVImage()->GetBufferSize();
    default:
      break;
  }

  IntSize size = mData->GetSize();
  CheckedInt<uint32_t> bytes =
      CheckedInt<uint32_t>(size.width) * size.height * 4;
  return bytes.isValid() ? bytes.value() : 0;
}

size_t BindingJSObjectMallocBytes(ImageBitmap* aBitmap) {
  return aBitmap->GetAllocatedSize();
}

void ImageBitmap::RemoveAssociatedMemory() {
  if (!mAllocatedImageData) {
    return;
  }
  if (JSObject* wrapper = GetWrapperMaybeDead()) {
    if (size_t bytes = BindingJSObjectMallocBytes(this)) {
      JS::RemoveAssociatedMemory(wrapper, bytes, JS::MemoryUse::DOMBinding);
    }
  }
  mAllocatedImageData = false;
}

/* static */
already_AddRefed<CreateImageBitmapFromBlob> CreateImageBitmapFromBlob::Create(
    Promise* aPromise, nsIGlobalObject* aGlobal, Blob& aBlob,
    const Maybe<IntRect>& aCropRect, nsIEventTarget* aMainThreadEventTarget,
    const ImageBitmapOptions& aOptions) {
  // Get the internal stream of the blob.
  nsCOMPtr<nsIInputStream> stream;
  ErrorResult error;
  aBlob.Impl()->CreateInputStream(getter_AddRefs(stream), error);
  if (NS_WARN_IF(error.Failed())) {
    return nullptr;
  }

  if (!NS_InputStreamIsBuffered(stream)) {
    nsCOMPtr<nsIInputStream> bufferedStream;
    nsresult rv = NS_NewBufferedInputStream(getter_AddRefs(bufferedStream),
                                            stream.forget(), 4096);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    stream = bufferedStream;
  }

  RefPtr<CreateImageBitmapFromBlob> task = new CreateImageBitmapFromBlob(
      aPromise, aGlobal, stream.forget(), aCropRect, aMainThreadEventTarget,
      aOptions);

  // Nothing to do for the main-thread.
  if (NS_IsMainThread()) {
    return task.forget();
  }

  // Let's use a WorkerRef to keep the worker alive if this is not the
  // main-thread.
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  RefPtr<StrongWorkerRef> workerRef =
      StrongWorkerRef::Create(workerPrivate, "CreateImageBitmapFromBlob",
                              [task]() { task->WorkerShuttingDown(); });
  if (NS_WARN_IF(!workerRef)) {
    return nullptr;
  }

  task->mWorkerRef = new ThreadSafeWorkerRef(workerRef);
  return task.forget();
}

nsresult CreateImageBitmapFromBlob::StartMimeTypeAndDecodeAndCropBlob() {
  MOZ_ASSERT(IsCurrentThread());

  // Workers.
  if (!NS_IsMainThread()) {
    RefPtr<CreateImageBitmapFromBlob> self = this;
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        "CreateImageBitmapFromBlob::MimeTypeAndDecodeAndCropBlob", [self]() {
          nsresult rv = self->MimeTypeAndDecodeAndCropBlob();
          if (NS_WARN_IF(NS_FAILED(rv))) {
            self->MimeTypeAndDecodeAndCropBlobCompletedMainThread(nullptr, rv);
          }
        });

    return mMainThreadEventTarget->Dispatch(r.forget());
  }

  // Main-thread.
  return MimeTypeAndDecodeAndCropBlob();
}

nsresult CreateImageBitmapFromBlob::MimeTypeAndDecodeAndCropBlob() {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString mimeType;
  nsresult rv = GetMimeTypeSync(mimeType);
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    return GetMimeTypeAsync();
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return DecodeAndCropBlob(mimeType);
}

nsresult CreateImageBitmapFromBlob::DecodeAndCropBlob(
    const nsACString& aMimeType) {
  // Get the Component object.
  nsCOMPtr<imgITools> imgtool = do_GetService(NS_IMGTOOLS_CID);
  if (NS_WARN_IF(!imgtool)) {
    return NS_ERROR_FAILURE;
  }

  // Decode image.
  nsresult rv = imgtool->DecodeImageAsync(mInputStream, aMimeType, this,
                                          mMainThreadEventTarget);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

static nsresult sniff_cb(nsIInputStream* aInputStream, void* aClosure,
                         const char* aFromRawSegment, uint32_t aToOffset,
                         uint32_t aCount, uint32_t* aWriteCount) {
  nsACString* mimeType = static_cast<nsACString*>(aClosure);
  MOZ_ASSERT(mimeType);

  if (aCount > 0) {
    imgLoader::GetMimeTypeFromContent(aFromRawSegment, aCount, *mimeType);
  }

  *aWriteCount = 0;

  // We don't want to consume data from the stream.
  return NS_ERROR_FAILURE;
}

nsresult CreateImageBitmapFromBlob::GetMimeTypeSync(nsACString& aMimeType) {
  uint32_t dummy;
  return mInputStream->ReadSegments(sniff_cb, &aMimeType, 128, &dummy);
}

nsresult CreateImageBitmapFromBlob::GetMimeTypeAsync() {
  nsCOMPtr<nsIAsyncInputStream> asyncInputStream =
      do_QueryInterface(mInputStream);
  if (NS_WARN_IF(!asyncInputStream)) {
    // If the stream is not async, why are we here?
    return NS_ERROR_FAILURE;
  }

  return asyncInputStream->AsyncWait(this, 0, 128, mMainThreadEventTarget);
}

NS_IMETHODIMP
CreateImageBitmapFromBlob::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  // The stream should have data now. Let's start from scratch again.
  nsresult rv = MimeTypeAndDecodeAndCropBlob();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MimeTypeAndDecodeAndCropBlobCompletedMainThread(nullptr, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
CreateImageBitmapFromBlob::OnImageReady(imgIContainer* aImgContainer,
                                        nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());

  if (NS_FAILED(aStatus)) {
    MimeTypeAndDecodeAndCropBlobCompletedMainThread(nullptr, aStatus);
    return NS_OK;
  }

  MOZ_ASSERT(aImgContainer);

  // Get the surface out.
  uint32_t frameFlags =
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY;
  uint32_t whichFrame = imgIContainer::FRAME_FIRST;

  if (mOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
    frameFlags |= imgIContainer::FLAG_DECODE_NO_PREMULTIPLY_ALPHA;
  }

  if (mOptions.mColorSpaceConversion == ColorSpaceConversion::None) {
    frameFlags |= imgIContainer::FLAG_DECODE_NO_COLORSPACE_CONVERSION;
  }

  RefPtr<SourceSurface> surface =
      aImgContainer->GetFrame(whichFrame, frameFlags);

  if (NS_WARN_IF(!surface)) {
    MimeTypeAndDecodeAndCropBlobCompletedMainThread(
        nullptr, NS_ERROR_DOM_INVALID_STATE_ERR);
    return NS_OK;
  }

  // Crop the source surface if needed.
  RefPtr<SourceSurface> croppedSurface = surface;
  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();

  // force a copy into unprotected memory as a side effect of
  // CropAndCopyDataSourceSurface
  bool copyRequired = mCropRect.isSome() ||
                      mOptions.mImageOrientation == ImageOrientation::FlipY;

  if (copyRequired) {
    // The blob is just decoded into a RasterImage and not optimized yet, so the
    // _surface_ we get is a DataSourceSurface which wraps the RasterImage's
    // raw buffer.
    //
    // The _surface_ might already be optimized so that its type is not
    // SurfaceType::DATA. However, we could keep using the generic cropping and
    // copying since the decoded buffer is only used in this ImageBitmap so we
    // should crop it to save memory usage.
    //
    // TODO: Bug1189632 is going to refactor this create-from-blob part to
    //       decode the blob off the main thread. Re-check if we should do
    //       cropping at this moment again there.

    IntRect cropRect =
        mCropRect.isSome() ? mCropRect.ref() : dataSurface->GetRect();

    croppedSurface = CropAndCopyDataSourceSurface(dataSurface, cropRect);
    if (NS_WARN_IF(!croppedSurface)) {
      MimeTypeAndDecodeAndCropBlobCompletedMainThread(
          nullptr, NS_ERROR_DOM_INVALID_STATE_ERR);
      return NS_OK;
    }

    dataSurface = croppedSurface->GetDataSurface();

    if (mCropRect.isSome()) {
      mCropRect->SetRect(0, 0, dataSurface->GetSize().width,
                         dataSurface->GetSize().height);
    }
  }

  if (mOptions.mImageOrientation == ImageOrientation::FlipY) {
    croppedSurface = FlipYDataSourceSurface(dataSurface);
  }

  if (mOptions.mResizeWidth.WasPassed() || mOptions.mResizeHeight.WasPassed()) {
    dataSurface = croppedSurface->GetDataSurface();
    croppedSurface = ScaleDataSourceSurface(dataSurface, mOptions);
    if (NS_WARN_IF(!croppedSurface)) {
      MimeTypeAndDecodeAndCropBlobCompletedMainThread(
          nullptr, NS_ERROR_DOM_INVALID_STATE_ERR);
      return NS_OK;
    }
    if (mCropRect.isSome()) {
      mCropRect->SetRect(0, 0, croppedSurface->GetSize().width,
                         croppedSurface->GetSize().height);
    }
  }

  if (NS_WARN_IF(!croppedSurface)) {
    MimeTypeAndDecodeAndCropBlobCompletedMainThread(
        nullptr, NS_ERROR_DOM_INVALID_STATE_ERR);
    return NS_OK;
  }

  // Create an Image from the source surface.
  RefPtr<layers::Image> image = CreateImageFromSurface(croppedSurface);

  if (NS_WARN_IF(!image)) {
    MimeTypeAndDecodeAndCropBlobCompletedMainThread(
        nullptr, NS_ERROR_DOM_INVALID_STATE_ERR);
    return NS_OK;
  }

  MimeTypeAndDecodeAndCropBlobCompletedMainThread(image, NS_OK);
  return NS_OK;
}

void CreateImageBitmapFromBlob::MimeTypeAndDecodeAndCropBlobCompletedMainThread(
    layers::Image* aImage, nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!IsCurrentThread()) {
    MutexAutoLock lock(mMutex);

    if (!mWorkerRef) {
      // The worker is already gone.
      return;
    }

    RefPtr<CreateImageBitmapFromBlobRunnable> r =
        new CreateImageBitmapFromBlobRunnable(this, aImage, aStatus);
    r->Dispatch(mWorkerRef->Private());
    return;
  }

  MimeTypeAndDecodeAndCropBlobCompletedOwningThread(aImage, aStatus);
}

void CreateImageBitmapFromBlob::
    MimeTypeAndDecodeAndCropBlobCompletedOwningThread(layers::Image* aImage,
                                                      nsresult aStatus) {
  MOZ_ASSERT(IsCurrentThread());

  if (!mPromise) {
    // The worker is going to be released soon. No needs to continue.
    return;
  }

  // Let's release what has to be released on the owning thread.
  auto raii = MakeScopeExit([&] {
    // Doing this we also release the worker.
    mWorkerRef = nullptr;

    mPromise = nullptr;
    mGlobalObject = nullptr;
  });

  if (NS_WARN_IF(NS_FAILED(aStatus))) {
    mPromise->MaybeRejectWithInvalidStateError(
        "The image could not be decoded");
    return;
  }

  gfxAlphaType alphaType = gfxAlphaType::Premult;

  if (mOptions.mPremultiplyAlpha == PremultiplyAlpha::None) {
    alphaType = gfxAlphaType::NonPremult;
  }

  // Create ImageBitmap object.
  RefPtr<ImageBitmap> imageBitmap = new ImageBitmap(
      mGlobalObject, aImage, true, false /* write-only */, alphaType);

  if (mCropRect.isSome()) {
    ErrorResult rv;
    imageBitmap->SetPictureRect(mCropRect.ref(), rv);

    if (rv.Failed()) {
      mPromise->MaybeReject(std::move(rv));
      return;
    }
  }

  mPromise->MaybeResolve(imageBitmap);
}

void CreateImageBitmapFromBlob::WorkerShuttingDown() {
  MOZ_ASSERT(IsCurrentThread());

  MutexAutoLock lock(mMutex);

  // Let's release all the non-thread-safe objects now.
  mWorkerRef = nullptr;
  mPromise = nullptr;
  mGlobalObject = nullptr;
}

}  // namespace mozilla::dom
