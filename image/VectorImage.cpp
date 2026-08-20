/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VectorImage.h"

#include "AutoRestoreSVGState.h"
#include "BlobSurfaceProvider.h"
#include "ISurfaceProvider.h"
#include "ImageRegion.h"
#include "LookupResult.h"
#include "Orientation.h"
#include "SVGDocumentWrapper.h"
#include "SVGDrawingCallback.h"
#include "SVGDrawingParameters.h"
#include "SurfaceCache.h"
#include "WindowRenderer.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "imgFrame.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGObserverUtils.h"  // for SVGRenderingObserver
#include "mozilla/SVGUtils.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/SVGDocument.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/image/Resolution.h"
#include "nsIDOMEventListener.h"
#include "nsIStreamListener.h"
#include "nsMimeTypes.h"
#include "nsPresContext.h"
#include "nsRect.h"
#include "nsString.h"
#include "nsStubDocumentObserver.h"
#include "nsWindowSizes.h"

namespace mozilla {

using namespace dom;
using namespace dom::SVGPreserveAspectRatio_Binding;
using namespace gfx;
using namespace layers;

namespace image {

// Helper-class: SVGRootRenderingObserver
class SVGRootRenderingObserver final : public SVGRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGRootRenderingObserver(SVGDocumentWrapper* aDocWrapper,
                           VectorImage* aVectorImage)
      : mDocWrapper(aDocWrapper),
        mVectorImage(aVectorImage),
        mHonoringInvalidations(true) {
    MOZ_ASSERT(mDocWrapper, "Need a non-null SVG document wrapper");
    MOZ_ASSERT(mVectorImage, "Need a non-null VectorImage");

    StartObserving();
    Element* elem = GetReferencedElementWithoutObserving();
    MOZ_ASSERT(elem, "no root SVG node for us to observe");

    SVGObserverUtils::AddRenderingObserver(elem, this);
    mInObserverSet = true;
  }

  void ResumeHonoringInvalidations() { mHonoringInvalidations = true; }

 protected:
  virtual ~SVGRootRenderingObserver() {
    // This needs to call our GetReferencedElementWithoutObserving override,
    // so must be called here rather than in our base class's dtor.
    StopObserving();
  }

  Element* GetReferencedElementWithoutObserving() const final {
    return mDocWrapper->GetSVGRootElement();
  }

  virtual void OnRenderingChange() override {
    Element* elem = GetReferencedElementWithoutObserving();
    MOZ_ASSERT(elem, "missing root SVG node");

    if (mHonoringInvalidations && !mDocWrapper->ShouldIgnoreInvalidation()) {
      nsIFrame* frame = elem->GetPrimaryFrame();
      if (!frame || frame->PresShell()->IsDestroying()) {
        // We're being destroyed. Bail out.
        return;
      }

      // Ignore further invalidations until we draw.
      mHonoringInvalidations = false;

      mVectorImage->InvalidateObserversOnNextRefreshDriverTick();
    }

    // Our caller might've removed us from rendering-observer list.
    // Add ourselves back!
    if (!mInObserverSet) {
      SVGObserverUtils::AddRenderingObserver(elem, this);
      mInObserverSet = true;
    }
  }

  // Private data
  const RefPtr<SVGDocumentWrapper> mDocWrapper;
  VectorImage* const mVectorImage;  // Raw pointer because it owns me.
  bool mHonoringInvalidations;
};

NS_IMPL_ISUPPORTS(SVGRootRenderingObserver, nsIMutationObserver)

class SVGParseCompleteListener final : public nsStubDocumentObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGParseCompleteListener(SVGDocument* aDocument, VectorImage* aImage)
      : mDocument(aDocument), mImage(aImage) {
    MOZ_ASSERT(mDocument, "Need an SVG document");
    MOZ_ASSERT(mImage, "Need an image");

    mDocument->AddObserver(this);
  }

 private:
  ~SVGParseCompleteListener() {
    if (mDocument) {
      // The document must have been destroyed before we got our event.
      // Otherwise this can't happen, since documents hold strong references to
      // their observers.
      Cancel();
    }
  }

 public:
  void EndLoad(Document* aDocument) override {
    MOZ_ASSERT(aDocument == mDocument, "Got EndLoad for wrong document?");

    // OnSVGDocumentParsed will release our owner's reference to us, so ensure
    // we stick around long enough to complete our work.
    RefPtr<SVGParseCompleteListener> kungFuDeathGrip(this);

    mImage->OnSVGDocumentParsed();
  }

  void Cancel() {
    MOZ_ASSERT(mDocument, "Duplicate call to Cancel");
    if (mDocument) {
      mDocument->RemoveObserver(this);
      mDocument = nullptr;
    }
  }

 private:
  RefPtr<SVGDocument> mDocument;
  VectorImage* const mImage;  // Raw pointer to owner.
};

NS_IMPL_ISUPPORTS(SVGParseCompleteListener, nsIDocumentObserver)

class SVGLoadEventListener final : public nsIDOMEventListener {
 public:
  NS_DECL_ISUPPORTS

  SVGLoadEventListener(Document* aDocument, VectorImage* aImage)
      : mDocument(aDocument), mImage(aImage) {
    MOZ_ASSERT(mDocument, "Need an SVG document");
    MOZ_ASSERT(mImage, "Need an image");

    mDocument->AddEventListener(u"MozSVGAsImageDocumentLoad"_ns, this, true,
                                false);
  }

 private:
  ~SVGLoadEventListener() {
    if (mDocument) {
      // The document must have been destroyed before we got our event.
      // Otherwise this can't happen, since documents hold strong references to
      // their observers.
      Cancel();
    }
  }

 public:
  NS_IMETHOD HandleEvent(Event* aEvent) override {
    MOZ_ASSERT(mDocument, "Need an SVG document. Received multiple events?");

    // OnSVGDocumentLoaded will release our owner's reference
    // to us, so ensure we stick around long enough to complete our work.
    RefPtr<SVGLoadEventListener> kungFuDeathGrip(this);

#ifdef DEBUG
    nsAutoString eventType;
    aEvent->GetType(eventType);
    MOZ_ASSERT(eventType.EqualsLiteral("MozSVGAsImageDocumentLoad"),
               "Received unexpected event");
#endif

    mImage->OnSVGDocumentLoaded();

    return NS_OK;
  }

  void Cancel() {
    MOZ_ASSERT(mDocument, "Duplicate call to Cancel");
    if (mDocument) {
      mDocument->RemoveEventListener(u"MozSVGAsImageDocumentLoad"_ns, this,
                                     true);
      mDocument = nullptr;
    }
  }

 private:
  nsCOMPtr<Document> mDocument;
  VectorImage* const mImage;  // Raw pointer to owner.
};

NS_IMPL_ISUPPORTS(SVGLoadEventListener, nsIDOMEventListener)

SVGDrawingCallback::SVGDrawingCallback(SVGDocumentWrapper* aSVGDocumentWrapper,
                                       const CSSSize& aViewportSize,
                                       const IntSize& aSize,
                                       uint32_t aImageFlags)
    : mSVGDocumentWrapper(aSVGDocumentWrapper),
      mViewportSize(aViewportSize),
      mSize(aSize),
      mImageFlags(aImageFlags) {}

SVGDrawingCallback::~SVGDrawingCallback() = default;

// Based loosely on SVGIntegrationUtils' PaintFrameCallback::operator()
bool SVGDrawingCallback::operator()(gfxContext* aContext,
                                    const gfxRect& aFillRect,
                                    const SamplingFilter aSamplingFilter,
                                    const gfxMatrix& aTransform) {
  MOZ_ASSERT(mSVGDocumentWrapper, "need an SVGDocumentWrapper");

  // Get (& sanity-check) the helper-doc's presShell
  RefPtr<PresShell> presShell = mSVGDocumentWrapper->GetPresShell();
  MOZ_ASSERT(presShell, "GetPresShell returned null for an SVG image?");

  Document* doc = presShell->GetDocument();
  nsIURI* uri = doc ? doc->GetDocumentURI() : nullptr;
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING(
      "SVG Image drawing", GRAPHICS,
      nsPrintfCString("%dx%d %s", mSize.width, mSize.height,
                      uri ? uri->GetSpecOrDefault().get() : "N/A"));

  gfxContextAutoSaveRestore contextRestorer(aContext);

  // Clip to aFillRect so that we don't paint outside.
  aContext->Clip(aFillRect);

  gfxMatrix matrix = aTransform;
  if (!matrix.Invert()) {
    return false;
  }
  aContext->SetMatrixDouble(
      aContext->CurrentMatrixDouble().PreMultiply(matrix).PreScale(
          double(mSize.width) / mViewportSize.width,
          double(mSize.height) / mViewportSize.height));

  nsRect svgRect(nsPoint(), CSSPixel::ToAppUnits(mViewportSize));

  RenderDocumentFlags renderDocFlags =
      RenderDocumentFlags::IgnoreViewportScrolling;
  if (!(mImageFlags & imgIContainer::FLAG_SYNC_DECODE)) {
    renderDocFlags |= RenderDocumentFlags::AsyncDecodeImages;
  }
  if (mImageFlags & imgIContainer::FLAG_HIGH_QUALITY_SCALING) {
    renderDocFlags |= RenderDocumentFlags::UseHighQualityScaling;
  }

  presShell->RenderDocument(svgRect, renderDocFlags,
                            NS_RGBA(0, 0, 0, 0),  // transparent
                            aContext);

  return true;
}

// Implement VectorImage's nsISupports-inherited methods
NS_IMPL_ISUPPORTS(VectorImage, imgIContainer, nsIStreamListener,
                  nsIRequestObserver)

//------------------------------------------------------------------------------
// Constructor / Destructor

VectorImage::VectorImage(nsIURI* aURI /* = nullptr */)
    : ImageResource(aURI),  // invoke superclass's constructor
      mLockCount(0),
      mIsInitialized(false),
      mDiscardable(false),
      mIsFullyLoaded(false),
      mHaveAnimations(false),
      mHasPendingInvalidation(false) {}

VectorImage::~VectorImage() {
  ReportDocumentUseCounters();
  CancelAllListeners();
  SurfaceCache::RemoveImage(ImageKey(this));
}

//------------------------------------------------------------------------------
// Methods inherited from Image.h

nsresult VectorImage::Init(const char* aMimeType, uint32_t aFlags) {
  // We don't support re-initialization
  if (mIsInitialized) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  MOZ_ASSERT(!mIsFullyLoaded && !mHaveAnimations && !mError,
             "Flags unexpectedly set before initialization");
  MOZ_ASSERT(!strcmp(aMimeType, IMAGE_SVG_XML), "Unexpected mimetype");

  mDiscardable = !!(aFlags & INIT_FLAG_DISCARDABLE);

  // Lock this image's surfaces in the SurfaceCache if we're not discardable.
  if (!mDiscardable) {
    mLockCount++;
    SurfaceCache::LockImage(ImageKey(this));
  }

  mIsInitialized = true;
  return NS_OK;
}

size_t VectorImage::SizeOfSourceWithComputedFallback(
    SizeOfState& aState) const {
  if (!mSVGDocumentWrapper) {
    return 0;  // No document, so no memory used for the document.
  }

  RefPtr<SVGDocument> doc = mSVGDocumentWrapper->GetDocument();
  if (!doc) {
    return 0;  // No document, so no memory used for the document.
  }

  nsWindowSizes windowSizes(aState);
  doc->DocAddSizeOfIncludingThis(windowSizes);

  if (windowSizes.getTotalSize() == 0) {
    // MallocSizeOf fails on this platform. Because we also use this method for
    // determining the size of cache entries, we need to return something
    // reasonable here. Unfortunately, there's no way to estimate the document's
    // size accurately, so we just use a constant value of 100KB, which will
    // generally underestimate the true size.
    return 100 * 1024;
  }

  return windowSizes.getTotalSize();
}

nsresult VectorImage::OnImageDataComplete(nsIRequest* aRequest,
                                          nsresult aStatus, bool aLastPart) {
  // Call our internal OnStopRequest method, which only talks to our embedded
  // SVG document. This won't have any effect on our ProgressTracker.
  nsresult finalStatus = OnStopRequest(aRequest, aStatus);

  // Give precedence to Necko failure codes.
  if (NS_FAILED(aStatus)) {
    finalStatus = aStatus;
  }

  Progress loadProgress = LoadCompleteProgress(aLastPart, mError, finalStatus);

  if (mIsFullyLoaded || mError) {
    // Our document is loaded, so we're ready to notify now.
    mProgressTracker->SyncNotifyProgress(loadProgress);
  } else {
    // Record our progress so far; we'll actually send the notifications in
    // OnSVGDocumentLoaded or OnSVGDocumentError.
    mLoadProgress = Some(loadProgress);
  }

  return finalStatus;
}

nsresult VectorImage::OnImageDataAvailable(nsIRequest* aRequest,
                                           nsIInputStream* aInStr,
                                           uint64_t aSourceOffset,
                                           uint32_t aCount) {
  return OnDataAvailable(aRequest, aInStr, aSourceOffset, aCount);
}

nsresult VectorImage::StartAnimation() {
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(ShouldAnimate(), "Should not animate!");

  mSVGDocumentWrapper->StartAnimation();
  return NS_OK;
}

nsresult VectorImage::StopAnimation() {
  nsresult rv = NS_OK;
  if (mError) {
    rv = NS_ERROR_FAILURE;
  } else {
    MOZ_ASSERT(mIsFullyLoaded && mHaveAnimations,
               "Should not have been animating!");

    mSVGDocumentWrapper->StopAnimation();
  }

  mAnimating = false;
  return rv;
}

bool VectorImage::ShouldAnimate() {
  return ImageResource::ShouldAnimate() && mIsFullyLoaded && mHaveAnimations;
}

// Helper for GetWidth/GetHeight:
// If the given LengthPercentage is just a Length, this function converts it to
// CSS pixels and clamps it to be nonnegative, and returns the result.
// Otherwise, this function returns Nothing().
static Maybe<int32_t> ClampedPxLengthOrNothing(
    const LengthPercentage& aLenPct) {
  if (!aLenPct.IsLength()) {
    return Nothing();
  }
  auto lenInPx = SVGUtils::ClampToInt(aLenPct.AsLength().ToCSSPixels());
  return Some(std::max(0, lenInPx));
}

//------------------------------------------------------------------------------
// imgIContainer methods

//******************************************************************************

NS_IMETHODIMP
VectorImage::GetWidth(int32_t* aWidth) {
  if (mError || !mIsFullyLoaded) {
    // XXXdholbert Technically we should leave outparam untouched when we
    // fail. But since many callers don't check for failure, we set it to 0 on
    // failure, for sane/predictable results.
    *aWidth = 0;
    return NS_ERROR_FAILURE;
  }

  SVGSVGElement* rootElem = mSVGDocumentWrapper->GetSVGRootElement();
  if (MOZ_UNLIKELY(!rootElem)) {
    // Unlikely to reach this code; we should have a root SVG elem, since we
    // finished loading without errors. But we can sometimes get here during
    // shutdown (as part of gathering a memory report) if the internal SVG
    // document has already been torn down by a shutdown listener.
    *aWidth = 0;
    return NS_ERROR_FAILURE;
  }

  auto widthFromSVG = ClampedPxLengthOrNothing(rootElem->GetIntrinsicWidth());
  if (!widthFromSVG) {
    *aWidth = 0;
    return NS_ERROR_FAILURE;
  }

  *aWidth = *widthFromSVG;
  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetHeight(int32_t* aHeight) {
  if (mError || !mIsFullyLoaded) {
    // XXXdholbert Technically we should leave outparam untouched when we
    // fail. But since many callers don't check for failure, we set it to 0 on
    // failure, for sane/predictable results.
    *aHeight = 0;
    return NS_ERROR_FAILURE;
  }

  SVGSVGElement* rootElem = mSVGDocumentWrapper->GetSVGRootElement();
  if (MOZ_UNLIKELY(!rootElem)) {
    // Unlikely to reach this code; we should have a root SVG elem, since we
    // finished loading without errors. But we can sometimes get here during
    // shutdown (as part of gathering a memory report) if the internal SVG
    // document has already been torn down by a shutdown listener.
    *aHeight = 0;
    return NS_ERROR_FAILURE;
  }
  auto heightFromSVG = ClampedPxLengthOrNothing(rootElem->GetIntrinsicHeight());
  if (!heightFromSVG) {
    *aHeight = 0;
    return NS_ERROR_FAILURE;
  }

  *aHeight = *heightFromSVG;
  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetIntrinsicSize(ImageIntrinsicSize* aIntrinsicSize) {
  if (mError || !mIsFullyLoaded) {
    return NS_ERROR_FAILURE;
  }
  SVGSVGElement* rootElem = mSVGDocumentWrapper->GetSVGRootElement();
  if (MOZ_UNLIKELY(!rootElem)) {
    return NS_ERROR_FAILURE;
  }

  aIntrinsicSize->mWidth =
      ClampedPxLengthOrNothing(rootElem->GetIntrinsicWidth());
  aIntrinsicSize->mHeight =
      ClampedPxLengthOrNothing(rootElem->GetIntrinsicHeight());

  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetIntrinsicSizeInAppUnits(nsSize* aSize) {
  if (mError || !mIsFullyLoaded) {
    return NS_ERROR_FAILURE;
  }

  nsIFrame* rootFrame = mSVGDocumentWrapper->GetRootLayoutFrame();
  if (!rootFrame) {
    return NS_ERROR_FAILURE;
  }

  *aSize = nsSize(-1, -1);
  IntrinsicSize rfSize = rootFrame->GetIntrinsicSize();
  if (rfSize.width) {
    aSize->width = *rfSize.width;
  }
  if (rfSize.height) {
    aSize->height = *rfSize.height;
  }
  return NS_OK;
}

//******************************************************************************
AspectRatio VectorImage::GetIntrinsicRatio() {
  if (mError || !mIsFullyLoaded) {
    return {};
  }
  nsIFrame* rootFrame = mSVGDocumentWrapper->GetRootLayoutFrame();
  if (!rootFrame) {
    return {};
  }
  return rootFrame->GetIntrinsicRatio();
}

nsresult VectorImage::GetHotspotX(int32_t* aX) {
  return Image::GetHotspotX(aX);
}

nsresult VectorImage::GetHotspotY(int32_t* aY) {
  return Image::GetHotspotY(aY);
}

nsIntSize VectorImage::OptimalImageSizeForDest(const gfxSize& aDest,
                                               uint32_t aWhichFrame,
                                               SamplingFilter aSamplingFilter,
                                               uint32_t aFlags) {
  MOZ_ASSERT(aDest.width >= 0 || ceil(aDest.width) <= INT32_MAX ||
                 aDest.height >= 0 || ceil(aDest.height) <= INT32_MAX,
             "Unexpected destination size");

  // We can rescale SVGs freely, so just return the provided destination size.
  return nsIntSize::Ceil(aDest.width, aDest.height);
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetType(uint16_t* aType) {
  NS_ENSURE_ARG_POINTER(aType);

  *aType = imgIContainer::TYPE_VECTOR;
  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetAnimated(bool* aAnimated) {
  if (mError || !mIsFullyLoaded) {
    return NS_ERROR_FAILURE;
  }

  *aAnimated = mSVGDocumentWrapper->IsAnimated();
  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::GetProviderId(uint32_t* aId) {
  NS_ENSURE_ARG_POINTER(aId);

  *aId = ImageResource::GetImageProviderId();
  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
VectorImage::GetFrame(uint32_t aWhichFrame, uint32_t aFlags) {
  if (mError) {
    return nullptr;
  }

  // Look up height & width
  // ----------------------
  SVGSVGElement* svgElem = mSVGDocumentWrapper->GetSVGRootElement();
  MOZ_ASSERT(svgElem,
             "Should have a root SVG elem, since we finished "
             "loading without errors");
  LengthPercentage width = svgElem->GetIntrinsicWidth();
  LengthPercentage height = svgElem->GetIntrinsicHeight();
  if (!width.IsLength() || !height.IsLength()) {
    // The SVG is lacking a definite size for its width or height, so we do not
    // know how big of a surface to generate. Hence, we just bail.
    NS_WARNING(
        "VectorImage::GetFrame called on image without an intrinsic width or "
        "height");
    return nullptr;
  }

  nsIntSize imageIntSize(SVGUtils::ClampToInt(width.AsLength().ToCSSPixels()),
                         SVGUtils::ClampToInt(height.AsLength().ToCSSPixels()));

  return GetFrameAtSize(imageIntSize, aWhichFrame, aFlags);
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
VectorImage::GetFrameAtSize(const IntSize& aSize, uint32_t aWhichFrame,
                            uint32_t aFlags) {
  MOZ_ASSERT(aWhichFrame <= FRAME_MAX_VALUE);

  AutoProfilerImagePaintMarker PROFILER_RAII(this);
#ifdef DEBUG
  NotifyDrawingObservers();
#endif

  if (aSize.IsEmpty() || aWhichFrame > FRAME_MAX_VALUE || mError ||
      !mIsFullyLoaded) {
    return nullptr;
  }

  uint32_t whichFrame = mHaveAnimations ? aWhichFrame : FRAME_FIRST;

  auto [sourceSurface, decodeSize] =
      LookupCachedSurface(aSize, SVGImageContext(), aFlags);
  if (sourceSurface) {
    return sourceSurface.forget();
  }

  if (mSVGDocumentWrapper->IsDrawing()) {
    NS_WARNING("Refusing to make re-entrant call to VectorImage::Draw");
    return nullptr;
  }

  float animTime = (whichFrame == FRAME_FIRST)
                       ? 0.0f
                       : mSVGDocumentWrapper->GetCurrentTimeAsFloat();

  // By using a null gfxContext, we ensure that we will always attempt to
  // create a surface, even if we aren't capable of caching it (e.g. due to our
  // flags, having an animation, etc). Otherwise CreateSurface will assume that
  // the caller is capable of drawing directly to its own draw target if we
  // cannot cache.
  SVGImageContext svgContext;
  SVGDrawingParameters params(
      nullptr, decodeSize, aSize, ImageRegion::Create(decodeSize),
      SamplingFilter::POINT, svgContext, animTime, aFlags, 1.0);

  bool didCache;  // Was the surface put into the cache?

  AutoRestoreSVGState autoRestore(params, mSVGDocumentWrapper,
                                  /* aContextPaint */ false);

  RefPtr<gfxDrawable> svgDrawable = CreateSVGDrawable(params);
  RefPtr<SourceSurface> surface = CreateSurface(params, svgDrawable, didCache);
  if (!surface) {
    MOZ_ASSERT(!didCache);
    return nullptr;
  }

  SendFrameComplete(didCache, params.flags);
  return surface.forget();
}

NS_IMETHODIMP_(bool)
VectorImage::WillDrawOpaqueNow() {
  return false;  // In general, SVG content is not opaque.
}

bool VectorImage::HasDecodedPixels() {
  MOZ_ASSERT_UNREACHABLE("calling VectorImage::HasDecodedPixels");
  return mIsFullyLoaded;
}

NS_IMETHODIMP_(bool)
VectorImage::IsImageContainerAvailable(WindowRenderer* aRenderer,
                                       uint32_t aFlags) {
  if (mError || !mIsFullyLoaded ||
      aRenderer->GetBackendType() != LayersBackend::LAYERS_WR) {
    return false;
  }

  if (mHaveAnimations && !StaticPrefs::image_svg_blob_image()) {
    // We don't support rasterizing animation SVGs. We can put them in a blob
    // recording however instead of using fallback.
    return false;
  }

  return true;
}

//******************************************************************************
NS_IMETHODIMP_(ImgDrawResult)
VectorImage::GetImageProvider(WindowRenderer* aRenderer,
                              const gfx::IntSize& aSize,
                              const SVGImageContext& aSVGContext,
                              const Maybe<ImageIntRegion>& aRegion,
                              uint32_t aFlags,
                              WebRenderImageProvider** aProvider) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRenderer);
  MOZ_ASSERT(!(aFlags & FLAG_BYPASS_SURFACE_CACHE), "Unsupported flags");

  // We don't need to check if the size is too big since we only support
  // WebRender backends.
  if (aSize.IsEmpty()) {
    return ImgDrawResult::BAD_ARGS;
  }

  if (mError) {
    return ImgDrawResult::BAD_IMAGE;
  }

  if (!mIsFullyLoaded) {
    return ImgDrawResult::NOT_READY;
  }

  if (mHaveAnimations && !(aFlags & FLAG_RECORD_BLOB)) {
    // We don't support rasterizing animation SVGs. We can put them in a blob
    // recording however instead of using fallback.
    return ImgDrawResult::NOT_SUPPORTED;
  }

  AutoProfilerImagePaintMarker PROFILER_RAII(this);
#ifdef DEBUG
  NotifyDrawingObservers();
#endif

  // Only blob recordings support a region to restrict drawing.
  const bool blobRecording = aFlags & FLAG_RECORD_BLOB;
  MOZ_ASSERT_IF(!blobRecording, aRegion.isNothing());

  LookupResult result(MatchType::NOT_FOUND);
  auto playbackType =
      mHaveAnimations ? PlaybackType::eAnimated : PlaybackType::eStatic;
  auto surfaceFlags = ToSurfaceFlags(aFlags);

  SVGImageContext newSVGContext = aSVGContext;
  bool contextPaint = MaybeRestrictSVGContext(newSVGContext, aFlags);

  SurfaceKey surfaceKey = VectorSurfaceKey(aSize, aRegion, newSVGContext,
                                           surfaceFlags, playbackType);
  if ((aFlags & FLAG_SYNC_DECODE) || !(aFlags & FLAG_HIGH_QUALITY_SCALING)) {
    result = SurfaceCache::Lookup(ImageKey(this), surfaceKey,
                                  /* aMarkUsed = */ true);
  } else {
    result = SurfaceCache::LookupBestMatch(ImageKey(this), surfaceKey,
                                           /* aMarkUsed = */ true);
  }

  // Unless we get a best match (exact or factor of 2 limited), then we want to
  // generate a new recording/rerasterize, even if we have a substitute.
  if (result && (result.Type() == MatchType::EXACT ||
                 result.Type() == MatchType::SUBSTITUTE_BECAUSE_BEST)) {
    result.Surface().TakeProvider(aProvider);
    return ImgDrawResult::SUCCESS;
  }

  // Ensure we store the surface with the correct key if we switched to factor
  // of 2 sizing or we otherwise got clamped.
  IntSize rasterSize(aSize);
  if (!result.SuggestedSize().IsEmpty()) {
    rasterSize = result.SuggestedSize();
    surfaceKey = surfaceKey.CloneWithSize(rasterSize);
  }

  // We're about to rerasterize, which may mean that some of the previous
  // surfaces we've rasterized aren't useful anymore. We can allow them to
  // expire from the cache by unlocking them here, and then sending out an
  // invalidation. If this image is locked, any surfaces that are still useful
  // will become locked again when Draw touches them, and the remainder will
  // eventually expire.
  bool mayCache = SurfaceCache::CanHold(rasterSize);
  if (mayCache) {
    SurfaceCache::UnlockEntries(ImageKey(this));
  }

  // Blob recorded vector images just create a provider responsible for
  // generating blob keys and recording bindings. The recording won't happen
  // until the caller requests the key explicitly.
  RefPtr<ISurfaceProvider> provider;
  if (blobRecording) {
    provider = MakeRefPtr<BlobSurfaceProvider>(ImageKey(this), surfaceKey,
                                               mSVGDocumentWrapper, aFlags);
  } else {
    if (mSVGDocumentWrapper->IsDrawing()) {
      NS_WARNING("Refusing to make re-entrant call to VectorImage::Draw");
      return ImgDrawResult::TEMPORARY_ERROR;
    }

    if (!SurfaceCache::IsLegalSize(rasterSize) ||
        !Factory::AllowedSurfaceSize(rasterSize)) {
      // If either of these is true then the InitWithDrawable call below will
      // fail, so fail early and use this opportunity to return NOT_SUPPORTED
      // instead of TEMPORARY_ERROR as we do for any InitWithDrawable failure.
      // This means that we will use fallback which has a path that will draw
      // directly into the gfxContext without having to allocate a surface. It
      // means we will have to use fallback and re-rasterize for everytime we
      // have to draw this image, but it's better than not drawing anything at
      // all.
      return ImgDrawResult::NOT_SUPPORTED;
    }

    // We aren't using blobs, so we need to rasterize.
    float animTime =
        mHaveAnimations ? mSVGDocumentWrapper->GetCurrentTimeAsFloat() : 0.0f;

    // By using a null gfxContext, we ensure that we will always attempt to
    // create a surface, even if we aren't capable of caching it (e.g. due to
    // our flags, having an animation, etc). Otherwise CreateSurface will assume
    // that the caller is capable of drawing directly to its own draw target if
    // we cannot cache.
    SVGDrawingParameters params(
        nullptr, rasterSize, aSize, ImageRegion::Create(rasterSize),
        SamplingFilter::POINT, newSVGContext, animTime, aFlags, 1.0);

    RefPtr<gfxDrawable> svgDrawable = CreateSVGDrawable(params);
    AutoRestoreSVGState autoRestore(params, mSVGDocumentWrapper, contextPaint);

    mSVGDocumentWrapper->UpdateViewportBounds(params.viewportSize);
    mSVGDocumentWrapper->FlushImageTransformInvalidation();

    // Given we have no context, the default backend is fine.
    BackendType backend =
        gfxPlatform::GetPlatform()->GetDefaultContentBackend();

    // Try to create an imgFrame, initializing the surface it contains by
    // drawing our gfxDrawable into it. (We use FILTER_NEAREST since we never
    // scale here.)
    auto frame = MakeNotNull<RefPtr<imgFrame>>();
    nsresult rv = frame->InitWithDrawable(
        svgDrawable, params.size, SurfaceFormat::OS_RGBA, SamplingFilter::POINT,
        params.flags, backend);

    // If we couldn't create the frame, it was probably because it would end
    // up way too big. Generally it also wouldn't fit in the cache, but the
    // prefs could be set such that the cache isn't the limiting factor.
    if (NS_FAILED(rv)) {
      return ImgDrawResult::TEMPORARY_ERROR;
    }

    provider =
        MakeRefPtr<SimpleSurfaceProvider>(ImageKey(this), surfaceKey, frame);
  }

  if (mayCache) {
    // Attempt to cache the frame.
    if (SurfaceCache::Insert(WrapNotNull(provider)) == InsertOutcome::SUCCESS) {
      if (rasterSize != aSize) {
        // We created a new surface that wasn't the size we requested, which
        // means we entered factor-of-2 mode. We should purge any surfaces we
        // no longer need rather than waiting for the cache to expire them.
        SurfaceCache::PruneImage(ImageKey(this));
      }

      SendFrameComplete(/* aDidCache */ true, aFlags);
    }
  }

  MOZ_ASSERT(provider);
  provider.forget(aProvider);
  return ImgDrawResult::SUCCESS;
}

//******************************************************************************
NS_IMETHODIMP_(ImgDrawResult)
VectorImage::Draw(gfxContext* aContext, const nsIntSize& aSize,
                  const ImageRegion& aRegion, uint32_t aWhichFrame,
                  SamplingFilter aSamplingFilter,
                  const SVGImageContext& aSVGContext, uint32_t aFlags,
                  float aOpacity) {
  if (aWhichFrame > FRAME_MAX_VALUE) {
    return ImgDrawResult::BAD_ARGS;
  }

  if (!aContext) {
    return ImgDrawResult::BAD_ARGS;
  }

  if (mError) {
    return ImgDrawResult::BAD_IMAGE;
  }

  if (!mIsFullyLoaded) {
    return ImgDrawResult::NOT_READY;
  }

  if (mAnimationConsumers == 0 && mHaveAnimations) {
    SendOnUnlockedDraw(aFlags);
  }

  // We should bypass the cache when:
  // - We are using a DrawTargetRecording because we prefer the drawing commands
  //   in general to the rasterized surface. This allows blob images to avoid
  //   rasterized SVGs with WebRender.
  if (aContext->GetDrawTarget()->GetBackendType() == BackendType::RECORDING) {
    aFlags |= FLAG_BYPASS_SURFACE_CACHE;
  }

  MOZ_ASSERT(!(aFlags & FLAG_FORCE_PRESERVEASPECTRATIO_NONE) ||
                 aSVGContext.GetViewportSize(),
             "Viewport size is required when using "
             "FLAG_FORCE_PRESERVEASPECTRATIO_NONE");

  uint32_t whichFrame = mHaveAnimations ? aWhichFrame : FRAME_FIRST;

  float animTime = (whichFrame == FRAME_FIRST)
                       ? 0.0f
                       : mSVGDocumentWrapper->GetCurrentTimeAsFloat();

  SVGImageContext newSVGContext = aSVGContext;
  bool contextPaint = MaybeRestrictSVGContext(newSVGContext, aFlags);

  SVGDrawingParameters params(aContext, aSize, aSize, aRegion, aSamplingFilter,
                              newSVGContext, animTime, aFlags, aOpacity);

  // If we have an prerasterized version of this image that matches the
  // drawing parameters, use that.
  RefPtr<SourceSurface> sourceSurface;
  std::tie(sourceSurface, params.size) =
      LookupCachedSurface(aSize, params.svgContext, aFlags);
  if (sourceSurface) {
    auto drawable = MakeRefPtr<gfxSurfaceDrawable>(sourceSurface, params.size);
    Show(drawable, params);
    return ImgDrawResult::SUCCESS;
  }

  // else, we need to paint the image:

  if (mSVGDocumentWrapper->IsDrawing()) {
    NS_WARNING("Refusing to make re-entrant call to VectorImage::Draw");
    return ImgDrawResult::TEMPORARY_ERROR;
  }

  AutoRestoreSVGState autoRestore(params, mSVGDocumentWrapper, contextPaint);

  bool didCache;  // Was the surface put into the cache?
  RefPtr<gfxDrawable> svgDrawable = CreateSVGDrawable(params);
  sourceSurface = CreateSurface(params, svgDrawable, didCache);
  if (!sourceSurface) {
    MOZ_ASSERT(!didCache);
    Show(svgDrawable, params);
    return ImgDrawResult::SUCCESS;
  }

  auto drawable = MakeRefPtr<gfxSurfaceDrawable>(sourceSurface, params.size);
  Show(drawable, params);
  SendFrameComplete(didCache, params.flags);
  return ImgDrawResult::SUCCESS;
}

NS_IMETHODIMP
VectorImage::StartDecoding(uint32_t aFlags, uint32_t aWhichFrame) {
  // Nothing to do for SVG images
  return NS_OK;
}

bool VectorImage::StartDecodingWithResult(uint32_t aFlags,
                                          uint32_t aWhichFrame) {
  // SVG images are ready to draw when they are loaded
  return mIsFullyLoaded;
}

imgIContainer::DecodeResult VectorImage::RequestDecodeWithResult(
    uint32_t aFlags, uint32_t aWhichFrame) {
  // SVG images are ready to draw when they are loaded and don't have an error.

  if (mError) {
    return imgIContainer::DECODE_REQUEST_FAILED;
  }

  if (!mIsFullyLoaded) {
    return imgIContainer::DECODE_REQUESTED;
  }

  return imgIContainer::DECODE_SURFACE_AVAILABLE;
}

NS_IMETHODIMP
VectorImage::RequestDecodeForSize(const nsIntSize& aSize, uint32_t aFlags,
                                  uint32_t aWhichFrame) {
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  // Nothing to do for SVG images, though in theory we could rasterize to the
  // provided size ahead of time if we supported off-main-thread SVG
  // rasterization...
  return NS_OK;
}

//******************************************************************************

NS_IMETHODIMP
VectorImage::LockImage() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mError) {
    return NS_ERROR_FAILURE;
  }

  mLockCount++;

  if (mLockCount == 1) {
    // Lock this image's surfaces in the SurfaceCache.
    SurfaceCache::LockImage(ImageKey(this));
  }

  return NS_OK;
}

//******************************************************************************

NS_IMETHODIMP
VectorImage::UnlockImage() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mError) {
    return NS_ERROR_FAILURE;
  }

  if (mLockCount == 0) {
    MOZ_ASSERT_UNREACHABLE("Calling UnlockImage with a zero lock count");
    return NS_ERROR_ABORT;
  }

  mLockCount--;

  if (mLockCount == 0) {
    // Unlock this image's surfaces in the SurfaceCache.
    SurfaceCache::UnlockImage(ImageKey(this));
  }

  return NS_OK;
}

//******************************************************************************

NS_IMETHODIMP
VectorImage::RequestDiscard() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mDiscardable && mLockCount == 0) {
    SurfaceCache::RemoveImage(ImageKey(this));
    mProgressTracker->OnDiscard();
  }

  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP_(void)
VectorImage::RequestRefresh(const TimeStamp& aTime) {
  if (HadRecentRefresh(aTime)) {
    return;
  }

  Document* doc = mSVGDocumentWrapper->GetDocument();
  if (!doc) {
    // We are racing between shutdown and a refresh.
    return;
  }

  EvaluateAnimation();

  mSVGDocumentWrapper->TickRefreshDriver();

  if (mHasPendingInvalidation) {
    SendInvalidationNotifications();
  }
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::ResetAnimation() {
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  if (!mIsFullyLoaded || !mHaveAnimations) {
    return NS_OK;  // There are no animations to be reset.
  }

  mSVGDocumentWrapper->ResetAnimation();

  return NS_OK;
}

NS_IMETHODIMP_(float)
VectorImage::GetFrameIndex(uint32_t aWhichFrame) {
  MOZ_ASSERT(aWhichFrame <= FRAME_MAX_VALUE, "Invalid argument");
  return aWhichFrame == FRAME_FIRST
             ? 0.0f
             : mSVGDocumentWrapper->GetCurrentTimeAsFloat();
}

NS_IMETHODIMP_(Orientation)
VectorImage::GetOrientation() { return Orientation(); }

NS_IMETHODIMP_(Resolution)
VectorImage::GetResolution() { return {}; }

//******************************************************************************
int32_t VectorImage::GetFirstFrameDelay() {
  if (mError) {
    return -1;
  }

  if (!mSVGDocumentWrapper->IsAnimated()) {
    return -1;
  }

  // We don't really have a frame delay, so just pretend that we constantly
  // need updates.
  return 0;
}

NS_IMETHODIMP_(void)
VectorImage::SetAnimationStartTime(const TimeStamp& aTime) {
  // We don't care about animation start time.
}

NS_IMETHODIMP_(IntRect)
VectorImage::GetImageSpaceInvalidationRect(const IntRect& aRect) {
  return aRect;
}

already_AddRefed<imgIContainer> VectorImage::Unwrap() {
  nsCOMPtr<imgIContainer> self(this);
  return self.forget();
}

void VectorImage::PropagateUseCounters(Document* aReferencingDocument) {
  if (Document* doc = mSVGDocumentWrapper->GetDocument()) {
    doc->PropagateImageUseCounters(aReferencingDocument);
  }
}

void VectorImage::MediaFeatureValuesChangedAllDocuments(
    const MediaFeatureChange& aChange) {
  if (!mSVGDocumentWrapper) {
    return;
  }

  // Don't bother if the document hasn't loaded yet.
  if (!mIsFullyLoaded) {
    return;
  }

  if (Document* doc = mSVGDocumentWrapper->GetDocument()) {
    if (RefPtr<nsPresContext> presContext = doc->GetPresContext()) {
      presContext->MediaFeatureValuesChanged(
          aChange, MediaFeatureChangePropagation::All);
      // Media feature value changes don't happen in the middle of layout,
      // so we don't need to call InvalidateObserversOnNextRefreshDriverTick
      // to invalidate asynchronously.
      if (presContext->FlushPendingMediaFeatureValuesChanged()) {
        // NOTE(emilio): SendInvalidationNotifications flushes layout via
        // VectorImage::CreateSurface -> FlushImageTransformInvalidation.
        SendInvalidationNotifications();
      }
    }
  }
}

//******************************************************************************
nsresult VectorImage::GetNativeSizes(nsTArray<IntSize>& aNativeSizes) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

//******************************************************************************
size_t VectorImage::GetNativeSizesLength() { return 0; }

bool VectorImage::MaybeRestrictSVGContext(SVGImageContext& aSVGContext,
                                          uint32_t aFlags) {
  bool overridePAR = (aFlags & FLAG_FORCE_PRESERVEASPECTRATIO_NONE);

  bool haveContextPaint = aSVGContext.GetContextPaint();
  bool blockContextPaint =
      haveContextPaint && !SVGContextPaint::IsAllowedForImageFromURI(mURI);

  if (overridePAR || blockContextPaint) {
    if (overridePAR) {
      // The SVGImageContext must take account of the preserveAspectRatio
      // override:
      MOZ_ASSERT(!aSVGContext.GetPreserveAspectRatio(),
                 "FLAG_FORCE_PRESERVEASPECTRATIO_NONE is not expected if a "
                 "preserveAspectRatio override is supplied");
      Maybe<SVGPreserveAspectRatio> aspectRatio = Some(SVGPreserveAspectRatio(
          SVG_PRESERVEASPECTRATIO_NONE, SVG_MEETORSLICE_UNKNOWN));
      aSVGContext.SetPreserveAspectRatio(aspectRatio);
    }

    if (blockContextPaint) {
      // The SVGImageContext must not include context paint if the image is
      // not allowed to use it:
      aSVGContext.ClearContextPaint();
    }
  }

  return haveContextPaint && !blockContextPaint;
}

already_AddRefed<gfxDrawable> VectorImage::CreateSVGDrawable(
    const SVGDrawingParameters& aParams) {
  auto cb = MakeRefPtr<SVGDrawingCallback>(
      mSVGDocumentWrapper, aParams.viewportSize, aParams.size, aParams.flags);

  auto svgDrawable = MakeRefPtr<gfxCallbackDrawable>(cb, aParams.size);
  return svgDrawable.forget();
}

std::tuple<RefPtr<SourceSurface>, IntSize> VectorImage::LookupCachedSurface(
    const IntSize& aSize, const SVGImageContext& aSVGContext, uint32_t aFlags) {
  // We can't use cached surfaces if we:
  // - Explicitly disallow it via FLAG_BYPASS_SURFACE_CACHE
  // - Want a blob recording which aren't supported by the cache.
  // - Have animations which aren't supported by the cache.
  if (aFlags & (FLAG_BYPASS_SURFACE_CACHE | FLAG_RECORD_BLOB) ||
      mHaveAnimations) {
    return std::make_tuple(RefPtr<SourceSurface>(), aSize);
  }

  LookupResult result(MatchType::NOT_FOUND);
  SurfaceKey surfaceKey = VectorSurfaceKey(aSize, aSVGContext);
  if ((aFlags & FLAG_SYNC_DECODE) || !(aFlags & FLAG_HIGH_QUALITY_SCALING)) {
    result = SurfaceCache::Lookup(ImageKey(this), surfaceKey,
                                  /* aMarkUsed = */ true);
  } else {
    result = SurfaceCache::LookupBestMatch(ImageKey(this), surfaceKey,
                                           /* aMarkUsed = */ true);
  }

  IntSize rasterSize =
      result.SuggestedSize().IsEmpty() ? aSize : result.SuggestedSize();
  MOZ_ASSERT(result.Type() != MatchType::SUBSTITUTE_BECAUSE_PENDING);
  if (!result || result.Type() == MatchType::SUBSTITUTE_BECAUSE_NOT_FOUND) {
    // No matching surface, or the OS freed the volatile buffer.
    return std::make_tuple(RefPtr<SourceSurface>(), rasterSize);
  }

  RefPtr<SourceSurface> sourceSurface = result.Surface()->GetSourceSurface();
  if (!sourceSurface) {
    // Something went wrong. (Probably a GPU driver crash or device reset.)
    // Attempt to recover.
    RecoverFromLossOfSurfaces();
    return std::make_tuple(RefPtr<SourceSurface>(), rasterSize);
  }

  return std::make_tuple(std::move(sourceSurface), rasterSize);
}

already_AddRefed<SourceSurface> VectorImage::CreateSurface(
    const SVGDrawingParameters& aParams, gfxDrawable* aSVGDrawable,
    bool& aWillCache) {
  MOZ_ASSERT(mSVGDocumentWrapper->IsDrawing());
  MOZ_ASSERT(!(aParams.flags & FLAG_RECORD_BLOB));

  mSVGDocumentWrapper->UpdateViewportBounds(aParams.viewportSize);
  mSVGDocumentWrapper->FlushImageTransformInvalidation();

  // Determine whether or not we should put the surface to be created into
  // the cache. If we fail, we need to reset this to false to let the caller
  // know nothing was put in the cache.
  aWillCache = !(aParams.flags & FLAG_BYPASS_SURFACE_CACHE) &&
               // Refuse to cache animated images:
               // XXX(seth): We may remove this restriction in bug 922893.
               !mHaveAnimations &&
               // The image is too big to fit in the cache:
               SurfaceCache::CanHold(aParams.size);

  // If we weren't given a context, then we know we just want the rasterized
  // surface. We will create the frame below but only insert it into the cache
  // if we actually need to.
  if (!aWillCache && aParams.context) {
    return nullptr;
  }

  // We're about to rerasterize, which may mean that some of the previous
  // surfaces we've rasterized aren't useful anymore. We can allow them to
  // expire from the cache by unlocking them here, and then sending out an
  // invalidation. If this image is locked, any surfaces that are still useful
  // will become locked again when Draw touches them, and the remainder will
  // eventually expire.
  if (aWillCache) {
    SurfaceCache::UnlockEntries(ImageKey(this));
  }

  // If there is no context, the default backend is fine.
  BackendType backend =
      aParams.context ? aParams.context->GetDrawTarget()->GetBackendType()
                      : gfxPlatform::GetPlatform()->GetDefaultContentBackend();

  // Try to create an imgFrame, initializing the surface it contains by drawing
  // our gfxDrawable into it. (We use FILTER_NEAREST since we never scale here.)
  auto frame = MakeNotNull<RefPtr<imgFrame>>();
  nsresult rv = frame->InitWithDrawable(
      aSVGDrawable, aParams.size, SurfaceFormat::OS_RGBA, SamplingFilter::POINT,
      aParams.flags, backend);

  // If we couldn't create the frame, it was probably because it would end
  // up way too big. Generally it also wouldn't fit in the cache, but the prefs
  // could be set such that the cache isn't the limiting factor.
  if (NS_FAILED(rv)) {
    aWillCache = false;
    return nullptr;
  }

  // Take a strong reference to the frame's surface and make sure it hasn't
  // already been purged by the operating system.
  RefPtr<SourceSurface> surface = frame->GetSourceSurface();
  if (!surface) {
    aWillCache = false;
    return nullptr;
  }

  // We created the frame, but only because we had no context to draw to
  // directly. All the caller wants is the surface in this case.
  if (!aWillCache) {
    return surface.forget();
  }

  // Attempt to cache the frame.
  SurfaceKey surfaceKey = VectorSurfaceKey(aParams.size, aParams.svgContext);
  NotNull<RefPtr<ISurfaceProvider>> provider =
      MakeNotNull<SimpleSurfaceProvider*>(ImageKey(this), surfaceKey, frame);

  if (SurfaceCache::Insert(provider) == InsertOutcome::SUCCESS) {
    if (aParams.size != aParams.drawSize) {
      // We created a new surface that wasn't the size we requested, which means
      // we entered factor-of-2 mode. We should purge any surfaces we no longer
      // need rather than waiting for the cache to expire them.
      SurfaceCache::PruneImage(ImageKey(this));
    }
  } else {
    aWillCache = false;
  }

  return surface.forget();
}

void VectorImage::SendFrameComplete(bool aDidCache, uint32_t aFlags) {
  // If the cache was not updated, we have nothing to do.
  if (!aDidCache) {
    return;
  }

  // Send out an invalidation so that surfaces that are still in use get
  // re-locked. See the discussion of the UnlockSurfaces call above.
  if (!(aFlags & FLAG_ASYNC_NOTIFY)) {
    mProgressTracker->SyncNotifyProgress(FLAG_FRAME_COMPLETE,
                                         GetMaxSizedIntRect());
  } else {
    NotNull<RefPtr<VectorImage>> image = WrapNotNull(this);
    NS_DispatchToMainThread(CreateRenderBlockingRunnable(NS_NewRunnableFunction(
        "ProgressTracker::SyncNotifyProgress", [=]() -> void {
          RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
          if (tracker) {
            tracker->SyncNotifyProgress(FLAG_FRAME_COMPLETE,
                                        GetMaxSizedIntRect());
          }
        })));
  }
}

void VectorImage::Show(gfxDrawable* aDrawable,
                       const SVGDrawingParameters& aParams) {
  // The surface size may differ from the size at which we wish to draw. As
  // such, we may need to adjust the context/region to take this into account.
  gfxContextMatrixAutoSaveRestore saveMatrix(aParams.context);
  ImageRegion region(aParams.region);
  if (aParams.drawSize != aParams.size) {
    gfx::MatrixScales scale(
        double(aParams.drawSize.width) / aParams.size.width,
        double(aParams.drawSize.height) / aParams.size.height);
    aParams.context->Multiply(gfx::Matrix::Scaling(scale));
    region.Scale(1.0 / scale.xScale, 1.0 / scale.yScale);
  }

  MOZ_ASSERT(aDrawable, "Should have a gfxDrawable by now");
  gfxUtils::DrawPixelSnapped(aParams.context, aDrawable,
                             SizeDouble(aParams.size), region,
                             SurfaceFormat::OS_RGBA, aParams.samplingFilter,
                             aParams.flags, aParams.opacity);

  AutoProfilerImagePaintMarker PROFILER_RAII(this);
#ifdef DEBUG
  NotifyDrawingObservers();
#endif

  MOZ_ASSERT(mRenderingObserver, "Should have a rendering observer by now");
  mRenderingObserver->ResumeHonoringInvalidations();
}

void VectorImage::RecoverFromLossOfSurfaces() {
  NS_WARNING("An imgFrame became invalid. Attempting to recover...");

  // Discard all existing frames, since they're probably all now invalid.
  SurfaceCache::RemoveImage(ImageKey(this));
}

void VectorImage::OnSurfaceDiscarded(const SurfaceKey& aSurfaceKey) {
  MOZ_ASSERT(mProgressTracker);

  NS_DispatchToMainThread(NewRunnableMethod("ProgressTracker::OnDiscard",
                                            mProgressTracker,
                                            &ProgressTracker::OnDiscard));
}

//------------------------------------------------------------------------------
// nsIRequestObserver methods

//******************************************************************************
NS_IMETHODIMP
VectorImage::OnStartRequest(nsIRequest* aRequest) {
  MOZ_ASSERT(!mSVGDocumentWrapper,
             "Repeated call to OnStartRequest -- can this happen?");

  mSVGDocumentWrapper = new SVGDocumentWrapper();
  RefPtr<SVGDocumentWrapper> wrapper = mSVGDocumentWrapper;
  nsresult rv = wrapper->OnStartRequest(aRequest);
  if (NS_FAILED(rv)) {
    mSVGDocumentWrapper = nullptr;
    mError = true;
    return rv;
  }

  // Create a listener to wait until the SVG document is fully loaded, which
  // will signal that this image is ready to render. Certain error conditions
  // will prevent us from ever getting this notification, so we also create a
  // listener that waits for parsing to complete and cancels the
  // SVGLoadEventListener if needed. The listeners are automatically attached
  // to the document by their constructors.
  SVGDocument* document = mSVGDocumentWrapper->GetDocument();
  mLoadEventListener = new SVGLoadEventListener(document, this);
  mParseCompleteListener = new SVGParseCompleteListener(document, this);

  // Displayed documents will call InitUseCounters under SetScriptGlobalObject,
  // but SVG image documents never get a script global object, so we initialize
  // use counters here, right after the document has been created.
  document->InitUseCounters();

  return NS_OK;
}

//******************************************************************************
NS_IMETHODIMP
VectorImage::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<SVGDocumentWrapper> wrapper = mSVGDocumentWrapper;
  return wrapper->OnStopRequest(aRequest, aStatus);
}

void VectorImage::OnSVGDocumentParsed() {
  MOZ_ASSERT(mParseCompleteListener, "Should have the parse complete listener");
  MOZ_ASSERT(mLoadEventListener, "Should have the load event listener");

  if (!mSVGDocumentWrapper->GetSVGRootElement()) {
    // This is an invalid SVG document. It may have failed to parse, or it may
    // be missing the <svg> root element, or the <svg> root element may not
    // declare the correct namespace. In any of these cases, we'll never be
    // notified that the SVG finished loading, so we need to treat this as an
    // error.
    OnSVGDocumentError();
  }
}

void VectorImage::CancelAllListeners() {
  if (mParseCompleteListener) {
    mParseCompleteListener->Cancel();
    mParseCompleteListener = nullptr;
  }
  if (mLoadEventListener) {
    mLoadEventListener->Cancel();
    mLoadEventListener = nullptr;
  }
}

void VectorImage::SendInvalidationNotifications() {
  // Animated images don't send out invalidation notifications as soon as
  // they're generated. Instead, InvalidateObserversOnNextRefreshDriverTick
  // records that there are pending invalidations and then returns immediately.
  // The notifications are actually sent from RequestRefresh(). We send these
  // notifications there to ensure that there is actually a document observing
  // us. Otherwise, the notifications are just wasted effort.
  //
  // Non-animated images post an event to call this method from
  // InvalidateObserversOnNextRefreshDriverTick, because RequestRefresh is never
  // called for them. Ordinarily this isn't needed, since we send out
  // invalidation notifications in OnSVGDocumentLoaded, but in rare cases the
  // SVG document may not be 100% ready to render at that time. In those cases
  // we would miss the subsequent invalidations if we didn't send out the
  // notifications indirectly in |InvalidateObservers...|.

  mHasPendingInvalidation = false;

  if (SurfaceCache::InvalidateImage(ImageKey(this))) {
    // If we had any surface providers in the cache, make sure we handle future
    // invalidations.
    MOZ_ASSERT(mRenderingObserver, "Should have a rendering observer by now");
    mRenderingObserver->ResumeHonoringInvalidations();
  }

  if (mProgressTracker) {
    mProgressTracker->SyncNotifyProgress(FLAG_FRAME_COMPLETE,
                                         GetMaxSizedIntRect());
  }
}

void VectorImage::OnSVGDocumentLoaded() {
  MOZ_ASSERT(mSVGDocumentWrapper->GetSVGRootElement(),
             "Should have parsed successfully");
  MOZ_ASSERT(!mIsFullyLoaded && !mHaveAnimations,
             "These flags shouldn't get set until OnSVGDocumentLoaded. "
             "Duplicate calls to OnSVGDocumentLoaded?");

  CancelAllListeners();

  // XXX Flushing is wasteful if embedding frame hasn't had initial reflow.
  mSVGDocumentWrapper->FlushLayout();

  // This is the earliest point that we can get accurate use counter data
  // for a valid SVG document.  Without the FlushLayout call, we would miss
  // any CSS property usage that comes from SVG presentation attributes.
  mSVGDocumentWrapper->GetDocument()->ReportDocumentUseCounters();

  mIsFullyLoaded = true;
  mHaveAnimations = mSVGDocumentWrapper->IsAnimated();

  // Start listening to our image for rendering updates.
  mRenderingObserver = new SVGRootRenderingObserver(mSVGDocumentWrapper, this);

  // ProgressTracker::SyncNotifyProgress may release us, so ensure we
  // stick around long enough to complete our work.
  RefPtr<VectorImage> kungFuDeathGrip(this);

  // Tell *our* observers that we're done loading.
  if (mProgressTracker) {
    Progress progress = FLAG_SIZE_AVAILABLE | FLAG_HAS_TRANSPARENCY |
                        FLAG_FRAME_COMPLETE | FLAG_DECODE_COMPLETE;

    if (mHaveAnimations) {
      progress |= FLAG_IS_ANIMATED;
    }

    // Merge in any saved progress from OnImageDataComplete.
    if (mLoadProgress) {
      progress |= *mLoadProgress;
      mLoadProgress = Nothing();
    }

    mProgressTracker->SyncNotifyProgress(progress, GetMaxSizedIntRect());
  }

  EvaluateAnimation();
}

void VectorImage::OnSVGDocumentError() {
  CancelAllListeners();

  mError = true;

  // We won't enter OnSVGDocumentLoaded, so report use counters now for this
  // invalid document.
  ReportDocumentUseCounters();

  // ProgressTracker::SyncNotifyProgress may release us, so ensure we
  // stick around long enough to complete our work.
  RefPtr<VectorImage> kungFuDeathGrip(this);

  if (mProgressTracker) {
    // Notify observers about the error and unblock page load.
    Progress progress = FLAG_HAS_ERROR;

    // Merge in any saved progress from OnImageDataComplete.
    if (mLoadProgress) {
      progress |= *mLoadProgress;
      mLoadProgress = Nothing();
    }

    mProgressTracker->SyncNotifyProgress(progress);
  }
}

//------------------------------------------------------------------------------
// nsIStreamListener method

//******************************************************************************
NS_IMETHODIMP
VectorImage::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInStr,
                             uint64_t aSourceOffset, uint32_t aCount) {
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<SVGDocumentWrapper> wrapper = mSVGDocumentWrapper;
  return wrapper->OnDataAvailable(aRequest, aInStr, aSourceOffset, aCount);
}

// --------------------------
// Invalidation helper method

void VectorImage::InvalidateObserversOnNextRefreshDriverTick() {
  if (mHasPendingInvalidation) {
    return;
  }

  mHasPendingInvalidation = true;

  // Animated images can wait for the refresh tick.
  if (mHaveAnimations) {
    return;
  }

  // Non-animated images won't get the refresh tick, so we should just send an
  // invalidation outside the current execution context. We need to defer
  // because the layout tree is in the middle of invalidation, and the tree
  // state needs to be consistent. Specifically only some of the frames have
  // had the NS_FRAME_DESCENDANT_NEEDS_PAINT and/or NS_FRAME_NEEDS_PAINT bits
  // set by InvalidateFrameInternal in layout/generic/nsFrame.cpp. These bits
  // get cleared when we repaint the SVG into a surface by
  // nsIFrame::ClearInvalidationStateBits in nsDisplayList::PaintRoot.
  nsCOMPtr<nsIEventTarget> eventTarget = do_GetMainThread();

  RefPtr<VectorImage> self(this);
  nsCOMPtr<nsIRunnable> ev(NS_NewRunnableFunction(
      "VectorImage::SendInvalidationNotifications",
      [=]() -> void { self->SendInvalidationNotifications(); }));
  eventTarget->Dispatch(CreateRenderBlockingRunnable(ev.forget()),
                        NS_DISPATCH_NORMAL);
}

void VectorImage::ReportDocumentUseCounters() {
  if (!mSVGDocumentWrapper) {
    return;
  }

  if (Document* doc = mSVGDocumentWrapper->GetDocument()) {
    doc->ReportDocumentUseCounters();
  }
}

}  // namespace image
}  // namespace mozilla
