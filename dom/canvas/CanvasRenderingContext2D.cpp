/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasRenderingContext2D.h"

#include "mozilla/gfx/Helpers.h"
#include "nsCSSValue.h"
#include "nsXULElement.h"

#include "nsMathUtils.h"

#include "nsContentUtils.h"

#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/GeckoBindings.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FontFaceSetImpl.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/GeneratePlaceholderCanvasData.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/gfx/CanvasShutdownManager.h"
#include "nsPresContext.h"

#include "nsIInterfaceRequestorUtils.h"
#include "nsIFrame.h"
#include "nsError.h"

#include "nsCSSPseudoElements.h"
#include "nsComputedDOMStyle.h"

#include "nsPrintfCString.h"

#include "nsRFPService.h"
#include "nsReadableUtils.h"

#include "nsColor.h"
#include "nsGfxCIID.h"
#include "nsIDocShell.h"
#include "nsPIDOMWindow.h"
#include "nsDisplayList.h"
#include "nsFocusManager.h"

#include "nsTArray.h"

#include "ImageEncoder.h"
#include "ImageRegion.h"

#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxFont.h"
#include "gfxBlur.h"
#include "gfxTextRun.h"
#include "gfxUtils.h"

#include "nsFrameLoader.h"
#include "nsBidiPresUtils.h"
#include "LayerUserData.h"
#include "CanvasUtils.h"
#include "nsIMemoryReporter.h"
#include "nsStyleUtil.h"
#include "CanvasImageCache.h"

#include <algorithm>

#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Array.h"  // JS::GetArrayLength
#include "js/Conversions.h"
#include "js/experimental/TypedData.h"  // JS_NewUint8ClampedArray, JS_GetUint8ClampedArrayData
#include "js/HeapAPI.h"
#include "js/PropertyAndElement.h"  // JS_GetElement
#include "js/Warnings.h"            // JS::WarnASCII

#include "mozilla/Alignment.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/dom/CanvasGradient.h"
#include "mozilla/dom/CanvasPattern.h"
#include "mozilla/dom/DOMMatrix.h"
#include "mozilla/dom/ImageBitmap.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/FilterInstance.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/PatternHelpers.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/PersistentBufferProvider.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Preferences.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "nsCCUncollectableMarker.h"
#include "nsWrapperCacheInlines.h"
#include "mozilla/dom/CanvasRenderingContext2DBinding.h"
#include "mozilla/dom/CanvasPath.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/SVGImageElement.h"
#include "mozilla/dom/TextMetrics.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Logging.h"
#include "nsGlobalWindowInner.h"
#include "nsDeviceContext.h"
#include "nsFontMetrics.h"
#include "nsLayoutUtils.h"
#include "Units.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/layers/CanvasClient.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"
#include "WindowRenderer.h"
#include "GeckoBindings.h"

#undef free  // apparently defined by some windows header, clashing with a
             // free() method in SkTypes.h

#ifdef XP_WIN
#  include "gfxWindowsPlatform.h"
#endif

// windows.h (included by chromium code) defines this, in its infinite wisdom
#undef DrawText

using namespace mozilla;
using namespace mozilla::CanvasUtils;
using namespace mozilla::css;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::ipc;
using namespace mozilla::layers;

namespace mozilla::dom {

// Cap sigma to avoid overly large temp surfaces.
const Float SIGMA_MAX = 100;

const size_t MAX_STYLE_STACK_SIZE = 1024;

/* Memory reporter stuff */
static Atomic<int64_t> gCanvasAzureMemoryUsed(0);

// Adds Save() / Restore() calls to the scope.
class MOZ_RAII AutoSaveRestore {
 public:
  explicit AutoSaveRestore(CanvasRenderingContext2D* aCtx) : mCtx(aCtx) {
    mCtx->Save();
  }
  ~AutoSaveRestore() { mCtx->Restore(); }

 private:
  RefPtr<CanvasRenderingContext2D> mCtx;
};

// This is KIND_OTHER because it's not always clear where in memory the pixels
// of a canvas are stored.  Furthermore, this memory will be tracked by the
// underlying surface implementations.  See bug 655638 for details.
class Canvas2dPixelsReporter final : public nsIMemoryReporter {
  ~Canvas2dPixelsReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("canvas-2d-pixels", KIND_OTHER, UNITS_BYTES,
                       gCanvasAzureMemoryUsed,
                       "Memory used by 2D canvases. Each canvas requires "
                       "(width * height * 4) bytes.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(Canvas2dPixelsReporter, nsIMemoryReporter)

class CanvasConicGradient : public CanvasGradient {
 public:
  CanvasConicGradient(CanvasRenderingContext2D* aContext, Float aAngle,
                      const Point& aCenter)
      : CanvasGradient(aContext, Type::CONIC),
        mAngle(aAngle),
        mCenter(aCenter) {}

  const Float mAngle;
  const Point mCenter;
};

class CanvasRadialGradient : public CanvasGradient {
 public:
  CanvasRadialGradient(CanvasRenderingContext2D* aContext,
                       const Point& aBeginOrigin, Float aBeginRadius,
                       const Point& aEndOrigin, Float aEndRadius)
      : CanvasGradient(aContext, Type::RADIAL),
        mCenter1(aBeginOrigin),
        mCenter2(aEndOrigin),
        mRadius1(aBeginRadius),
        mRadius2(aEndRadius) {}

  Point mCenter1;
  Point mCenter2;
  Float mRadius1;
  Float mRadius2;
};

class CanvasLinearGradient : public CanvasGradient {
 public:
  CanvasLinearGradient(CanvasRenderingContext2D* aContext, const Point& aBegin,
                       const Point& aEnd)
      : CanvasGradient(aContext, Type::LINEAR), mBegin(aBegin), mEnd(aEnd) {}

 protected:
  friend struct CanvasBidiProcessor;
  friend class CanvasGeneralPattern;

  // Beginning of linear gradient.
  Point mBegin;
  // End of linear gradient.
  Point mEnd;
};

bool CanvasRenderingContext2D::PatternIsOpaque(
    CanvasRenderingContext2D::Style aStyle, bool* aIsColor) const {
  const ContextState& state = CurrentState();
  bool opaque = false;
  bool color = false;
  if (state.globalAlpha >= 1.0) {
    if (state.patternStyles[aStyle] && state.patternStyles[aStyle]->mSurface) {
      opaque = IsOpaque(state.patternStyles[aStyle]->mSurface->GetFormat());
    } else if (!state.gradientStyles[aStyle]) {
      // TODO: for gradient patterns we could check that all stops are opaque
      // colors.
      // it's a color pattern.
      opaque = sRGBColor::FromABGR(state.colorStyles[aStyle]).a >= 1.0;
      color = true;
    }
  }
  if (aIsColor) {
    *aIsColor = color;
  }
  return opaque;
}

// This class is named 'GeneralCanvasPattern' instead of just
// 'GeneralPattern' to keep Windows PGO builds from confusing the
// GeneralPattern class in gfxContext.cpp with this one.
class CanvasGeneralPattern {
 public:
  using Style = CanvasRenderingContext2D::Style;
  using ContextState = CanvasRenderingContext2D::ContextState;

  Pattern& ForStyle(CanvasRenderingContext2D* aCtx, Style aStyle,
                    DrawTarget* aRT) {
    // This should only be called once or the mPattern destructor will
    // not be executed.
    NS_ASSERTION(
        !mPattern.GetPattern(),
        "ForStyle() should only be called once on CanvasGeneralPattern!");

    const ContextState& state = aCtx->CurrentState();

    if (state.StyleIsColor(aStyle)) {
      mPattern.InitColorPattern(ToDeviceColor(state.colorStyles[aStyle]));
    } else if (state.gradientStyles[aStyle] &&
               state.gradientStyles[aStyle]->GetType() ==
                   CanvasGradient::Type::LINEAR) {
      auto gradient = static_cast<CanvasLinearGradient*>(
          state.gradientStyles[aStyle].get());

      mPattern.InitLinearGradientPattern(
          gradient->mBegin, gradient->mEnd,
          gradient->GetGradientStopsForTarget(aRT));
    } else if (state.gradientStyles[aStyle] &&
               state.gradientStyles[aStyle]->GetType() ==
                   CanvasGradient::Type::RADIAL) {
      auto gradient = static_cast<CanvasRadialGradient*>(
          state.gradientStyles[aStyle].get());

      mPattern.InitRadialGradientPattern(
          gradient->mCenter1, gradient->mCenter2, gradient->mRadius1,
          gradient->mRadius2, gradient->GetGradientStopsForTarget(aRT));
    } else if (state.gradientStyles[aStyle] &&
               state.gradientStyles[aStyle]->GetType() ==
                   CanvasGradient::Type::CONIC) {
      auto gradient =
          static_cast<CanvasConicGradient*>(state.gradientStyles[aStyle].get());

      mPattern.InitConicGradientPattern(
          gradient->mCenter, gradient->mAngle, 0, 1,
          gradient->GetGradientStopsForTarget(aRT));
    } else if (state.patternStyles[aStyle]) {
      aCtx->DoSecurityCheck(state.patternStyles[aStyle]->mPrincipal,
                            state.patternStyles[aStyle]->mForceWriteOnly,
                            state.patternStyles[aStyle]->mCORSUsed);

      ExtendMode mode;
      if (state.patternStyles[aStyle]->mRepeat ==
          CanvasPattern::RepeatMode::NOREPEAT) {
        mode = ExtendMode::CLAMP;
      } else {
        mode = ExtendMode::REPEAT;
      }

      SamplingFilter samplingFilter;
      if (state.imageSmoothingEnabled) {
        samplingFilter = SamplingFilter::GOOD;
      } else {
        samplingFilter = SamplingFilter::POINT;
      }

      mPattern.InitSurfacePattern(state.patternStyles[aStyle]->mSurface, mode,
                                  state.patternStyles[aStyle]->mTransform,
                                  samplingFilter);
    }

    return *mPattern.GetPattern();
  }

  GeneralPattern mPattern;
};

/* This is an RAII based class that can be used as a drawtarget for
 * operations that need to have a filter applied to their results.
 * All coordinates passed to the constructor are in device space.
 */
class AdjustedTargetForFilter {
 public:
  using ContextState = CanvasRenderingContext2D::ContextState;

  AdjustedTargetForFilter(CanvasRenderingContext2D* aCtx,
                          DrawTarget* aFinalTarget,
                          const gfx::IntPoint& aFilterSpaceToTargetOffset,
                          const gfx::IntRect& aPreFilterBounds,
                          const gfx::IntRect& aPostFilterBounds,
                          gfx::CompositionOp aCompositionOp)
      : mFinalTarget(aFinalTarget),
        mCtx(aCtx),
        mPostFilterBounds(aPostFilterBounds),
        mOffset(aFilterSpaceToTargetOffset),
        mCompositionOp(aCompositionOp) {
    nsIntRegion sourceGraphicNeededRegion;
    nsIntRegion fillPaintNeededRegion;
    nsIntRegion strokePaintNeededRegion;

    FilterSupport::ComputeSourceNeededRegions(
        aCtx->CurrentState().filter, mPostFilterBounds,
        sourceGraphicNeededRegion, fillPaintNeededRegion,
        strokePaintNeededRegion);

    mSourceGraphicRect = sourceGraphicNeededRegion.GetBounds();
    mFillPaintRect = fillPaintNeededRegion.GetBounds();
    mStrokePaintRect = strokePaintNeededRegion.GetBounds();

    mSourceGraphicRect = mSourceGraphicRect.Intersect(aPreFilterBounds);

    if (mSourceGraphicRect.IsEmpty()) {
      // The filter might not make any use of the source graphic. We need to
      // create a DrawTarget that we can return from DT() anyway, so we'll
      // just use a 1x1-sized one.
      mSourceGraphicRect.SizeTo(1, 1);
    }

    if (!mFinalTarget->CanCreateSimilarDrawTarget(mSourceGraphicRect.Size(),
                                                  SurfaceFormat::B8G8R8A8)) {
      mTarget = mFinalTarget;
      mCtx = nullptr;
      mFinalTarget = nullptr;
      return;
    }

    mTarget = mFinalTarget->CreateSimilarDrawTarget(mSourceGraphicRect.Size(),
                                                    SurfaceFormat::B8G8R8A8);

    if (mTarget) {
      // See bug 1524554.
      mTarget->ClearRect(gfx::Rect());
    }

    if (!mTarget || !mTarget->IsValid()) {
      // XXX - Deal with the situation where our temp size is too big to
      // fit in a texture (bug 1066622).
      mTarget = mFinalTarget;
      mCtx = nullptr;
      mFinalTarget = nullptr;
      return;
    }

    mTarget->SetTransform(mFinalTarget->GetTransform().PostTranslate(
        -mSourceGraphicRect.TopLeft() + mOffset));
  }

  // Return a SourceSurface that contains the FillPaint or StrokePaint source.
  already_AddRefed<SourceSurface> DoSourcePaint(
      gfx::IntRect& aRect, CanvasRenderingContext2D::Style aStyle) {
    if (aRect.IsEmpty()) {
      return nullptr;
    }

    RefPtr<DrawTarget> dt = mFinalTarget->CreateSimilarDrawTarget(
        aRect.Size(), SurfaceFormat::B8G8R8A8);

    if (dt) {
      // See bug 1524554.
      dt->ClearRect(gfx::Rect());
    }

    if (!dt || !dt->IsValid()) {
      aRect.SetEmpty();
      return nullptr;
    }

    Matrix transform =
        mFinalTarget->GetTransform().PostTranslate(-aRect.TopLeft() + mOffset);

    dt->SetTransform(transform);

    if (transform.Invert()) {
      gfx::Rect dtBounds(0, 0, aRect.width, aRect.height);
      gfx::Rect fillRect = transform.TransformBounds(dtBounds);
      dt->FillRect(fillRect, CanvasGeneralPattern().ForStyle(mCtx, aStyle, dt));
    }
    return dt->Snapshot();
  }

  ~AdjustedTargetForFilter() {
    if (!mCtx) {
      return;
    }

    RefPtr<SourceSurface> snapshot = mTarget->Snapshot();

    RefPtr<SourceSurface> fillPaint =
        DoSourcePaint(mFillPaintRect, CanvasRenderingContext2D::Style::FILL);
    RefPtr<SourceSurface> strokePaint = DoSourcePaint(
        mStrokePaintRect, CanvasRenderingContext2D::Style::STROKE);

    AutoRestoreTransform autoRestoreTransform(mFinalTarget);
    mFinalTarget->SetTransform(Matrix());

    MOZ_RELEASE_ASSERT(!mCtx->CurrentState().filter.mPrimitives.IsEmpty());
    gfx::FilterSupport::RenderFilterDescription(
        mFinalTarget, mCtx->CurrentState().filter, gfx::Rect(mPostFilterBounds),
        snapshot, mSourceGraphicRect, fillPaint, mFillPaintRect, strokePaint,
        mStrokePaintRect, mCtx->CurrentState().filterAdditionalImages,
        mPostFilterBounds.TopLeft() - mOffset,
        DrawOptions(1.0f, mCompositionOp));

    const gfx::FilterDescription& filter = mCtx->CurrentState().filter;
    MOZ_RELEASE_ASSERT(!filter.mPrimitives.IsEmpty());
    if (filter.mPrimitives.LastElement().IsTainted()) {
      if (mCtx->mCanvasElement) {
        mCtx->mCanvasElement->SetWriteOnly();
      } else if (mCtx->mOffscreenCanvas) {
        mCtx->mOffscreenCanvas->SetWriteOnly();
      }
    }
  }

  DrawTarget* DT() { return mTarget; }

 private:
  RefPtr<DrawTarget> mTarget;
  RefPtr<DrawTarget> mFinalTarget;
  CanvasRenderingContext2D* mCtx;
  gfx::IntRect mSourceGraphicRect;
  gfx::IntRect mFillPaintRect;
  gfx::IntRect mStrokePaintRect;
  gfx::IntRect mPostFilterBounds;
  gfx::IntPoint mOffset;
  gfx::CompositionOp mCompositionOp;
};

/* This is an RAII based class that can be used as a drawtarget for
 * operations that need to have a shadow applied to their results.
 * All coordinates passed to the constructor are in device space.
 */
class AdjustedTargetForShadow {
 public:
  using ContextState = CanvasRenderingContext2D::ContextState;

  AdjustedTargetForShadow(CanvasRenderingContext2D* aCtx,
                          DrawTarget* aFinalTarget, const gfx::Rect& aBounds,
                          gfx::CompositionOp aCompositionOp)
      : mFinalTarget(aFinalTarget), mCtx(aCtx), mCompositionOp(aCompositionOp) {
    const ContextState& state = mCtx->CurrentState();
    mSigma = state.ShadowBlurSigma();

    // We actually include the bounds of the shadow blur, this makes it
    // easier to execute the actual blur on hardware, and shouldn't affect
    // the amount of pixels that need to be touched.
    gfx::Rect bounds = aBounds;
    int32_t blurRadius = state.ShadowBlurRadius();
    bounds.Inflate(blurRadius);
    bounds.RoundOut();
    if (!bounds.ToIntRect(&mTempRect) ||
        !mFinalTarget->CanCreateSimilarDrawTarget(mTempRect.Size(),
                                                  SurfaceFormat::B8G8R8A8)) {
      mTarget = mFinalTarget;
      mCtx = nullptr;
      mFinalTarget = nullptr;
      return;
    }

    mTarget = mFinalTarget->CreateShadowDrawTarget(
        mTempRect.Size(), SurfaceFormat::B8G8R8A8, mSigma);

    if (mTarget) {
      // See bug 1524554.
      mTarget->ClearRect(gfx::Rect());
    }

    if (!mTarget || !mTarget->IsValid()) {
      // XXX - Deal with the situation where our temp size is too big to
      // fit in a texture (bug 1066622).
      mTarget = mFinalTarget;
      mCtx = nullptr;
      mFinalTarget = nullptr;
    } else {
      mTarget->SetTransform(
          mFinalTarget->GetTransform().PostTranslate(-mTempRect.TopLeft()));
    }
  }

  ~AdjustedTargetForShadow() {
    if (!mCtx) {
      return;
    }

    RefPtr<SourceSurface> snapshot = mTarget->Snapshot();

    mFinalTarget->DrawSurfaceWithShadow(
        snapshot, mTempRect.TopLeft(),
        ShadowOptions(ToDeviceColor(mCtx->CurrentState().shadowColor),
                      mCtx->CurrentState().shadowOffset, mSigma),
        mCompositionOp);
  }

  DrawTarget* DT() { return mTarget; }

  gfx::IntPoint OffsetToFinalDT() { return mTempRect.TopLeft(); }

 private:
  RefPtr<DrawTarget> mTarget;
  RefPtr<DrawTarget> mFinalTarget;
  CanvasRenderingContext2D* mCtx;
  Float mSigma;
  gfx::IntRect mTempRect;
  gfx::CompositionOp mCompositionOp;
};

/*
 * This is an RAII based class that can be used as a drawtarget for
 * operations that need a shadow or a filter drawn. It will automatically
 * provide a temporary target when needed, and if so blend it back with a
 * shadow, filter, or both.
 * If both a shadow and a filter are needed, the filter is applied first,
 * and the shadow is applied to the filtered results.
 *
 * aBounds specifies the bounds of the drawing operation that will be
 * drawn to the target, it is given in device space! If this is nullptr the
 * drawing operation will be assumed to cover the whole canvas.
 */
class AdjustedTarget {
 public:
  using ContextState = CanvasRenderingContext2D::ContextState;

  explicit AdjustedTarget(CanvasRenderingContext2D* aCtx,
                          const gfx::Rect* aBounds = nullptr,
                          bool aAllowOptimization = false)
      : mCtx(aCtx),
        mOptimizeShadow(false),
        mUsedOperation(aCtx->CurrentState().op) {
    // All rects in this function are in the device space of ctx->mTarget.

    // In order to keep our temporary surfaces as small as possible, we first
    // calculate what their maximum required bounds would need to be if we
    // were to fill the whole canvas. Everything outside those bounds we don't
    // need to render.
    gfx::Rect r(0, 0, aCtx->mWidth, aCtx->mHeight);
    gfx::Rect maxSourceNeededBoundsForShadow =
        MaxSourceNeededBoundsForShadow(r, aCtx);
    gfx::Rect maxSourceNeededBoundsForFilter =
        MaxSourceNeededBoundsForFilter(maxSourceNeededBoundsForShadow, aCtx);
    if (!aCtx->IsTargetValid()) {
      return;
    }

    gfx::Rect bounds = maxSourceNeededBoundsForFilter;
    if (aBounds) {
      bounds = bounds.Intersect(*aBounds);
    }
    gfx::Rect boundsAfterFilter = BoundsAfterFilter(bounds, aCtx);
    if (!aCtx->IsTargetValid() || !boundsAfterFilter.IsFinite()) {
      return;
    }

    gfx::IntPoint offsetToFinalDT;

    // First set up the shadow draw target, because the shadow goes outside.
    // It applies to the post-filter results, if both a filter and a shadow
    // are used.
    const bool applyFilter = aCtx->NeedToApplyFilter();
    if (aCtx->NeedToDrawShadow()) {
      if (aAllowOptimization && !applyFilter) {
        // If only drawing a shadow and no filter, then avoid buffering to an
        // intermediate target while drawing the shadow directly to the final
        // target. When doing so, we want to use the actual composition op
        // instead of OP_OVER.
        mTarget = aCtx->mTarget;
        if (mTarget && mTarget->IsValid()) {
          mOptimizeShadow = true;
          return;
        }
      }
      mShadowTarget = MakeUnique<AdjustedTargetForShadow>(
          aCtx, aCtx->mTarget, boundsAfterFilter, mUsedOperation);
      mTarget = mShadowTarget->DT();
      offsetToFinalDT = mShadowTarget->OffsetToFinalDT();

      // If we also have a filter, the filter needs to be drawn with OP_OVER
      // because shadow drawing already applies op on the result.
      mUsedOperation = CompositionOp::OP_OVER;
    }

    // Now set up the filter draw target.
    if (!aCtx->IsTargetValid()) {
      return;
    }
    if (applyFilter) {
      bounds.RoundOut();

      if (!mTarget) {
        mTarget = aCtx->mTarget;
      }
      gfx::IntRect intBounds;
      if (!bounds.ToIntRect(&intBounds)) {
        return;
      }
      mFilterTarget = MakeUnique<AdjustedTargetForFilter>(
          aCtx, mTarget, offsetToFinalDT, intBounds,
          gfx::RoundedToInt(boundsAfterFilter), mUsedOperation);
      mTarget = mFilterTarget->DT();
      mUsedOperation = CompositionOp::OP_OVER;
    }
    if (!mTarget) {
      mTarget = aCtx->mTarget;
    }
  }

  ~AdjustedTarget() {
    // The order in which the targets are finalized is important.
    // Filters are inside, any shadow applies to the post-filter results.
    mFilterTarget.reset();
    mShadowTarget.reset();
  }

  operator DrawTarget*() { return mTarget; }

  DrawTarget* operator->() MOZ_NO_ADDREF_RELEASE_ON_RETURN { return mTarget; }

  CompositionOp UsedOperation() const { return mUsedOperation; }

  bool UseOptimizeShadow() const { return mOptimizeShadow; }

  ShadowOptions ShadowParams() const {
    const ContextState& state = mCtx->CurrentState();
    return ShadowOptions(ToDeviceColor(state.shadowColor), state.shadowOffset,
                         state.ShadowBlurSigma());
  }

  void Fill(const Path* aPath, const Pattern& aPattern,
            const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      mTarget->DrawShadow(aPath, aPattern, ShadowParams(), aOptions);
    }
    mTarget->Fill(aPath, aPattern, aOptions);
  }

  void FillRect(const Rect& aRect, const Pattern& aPattern,
                const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      RefPtr<Path> path = MakePathForRect(*mTarget, aRect);
      mTarget->DrawShadow(path, aPattern, ShadowParams(), aOptions);
    }
    mTarget->FillRect(aRect, aPattern, aOptions);
  }

  void Stroke(const Path* aPath, const Pattern& aPattern,
              const StrokeOptions& aStrokeOptions,
              const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      mTarget->DrawShadow(aPath, aPattern, ShadowParams(), aOptions,
                          &aStrokeOptions);
    }
    mTarget->Stroke(aPath, aPattern, aStrokeOptions, aOptions);
  }

  void StrokeRect(const Rect& aRect, const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions,
                  const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      RefPtr<Path> path = MakePathForRect(*mTarget, aRect);
      mTarget->DrawShadow(path, aPattern, ShadowParams(), aOptions,
                          &aStrokeOptions);
    }
    mTarget->StrokeRect(aRect, aPattern, aStrokeOptions, aOptions);
  }

  void StrokeLine(const Point& aStart, const Point& aEnd,
                  const Pattern& aPattern, const StrokeOptions& aStrokeOptions,
                  const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      RefPtr<PathBuilder> builder = mTarget->CreatePathBuilder();
      builder->MoveTo(aStart);
      builder->LineTo(aEnd);
      RefPtr<Path> path = builder->Finish();
      mTarget->DrawShadow(path, aPattern, ShadowParams(), aOptions,
                          &aStrokeOptions);
    }
    mTarget->StrokeLine(aStart, aEnd, aPattern, aStrokeOptions, aOptions);
  }

  void DrawSurface(SourceSurface* aSurface, const Rect& aDest,
                   const Rect& aSource, const DrawSurfaceOptions& aSurfOptions,
                   const DrawOptions& aOptions) {
    if (mOptimizeShadow) {
      RefPtr<Path> path = MakePathForRect(*mTarget, aSource);
      ShadowOptions shadowParams(ShadowParams());
      SurfacePattern pattern(aSurface, ExtendMode::CLAMP, Matrix(),
                             shadowParams.BlurRadius() > 1
                                 ? SamplingFilter::POINT
                                 : aSurfOptions.mSamplingFilter);
      Matrix matrix = Matrix::Scaling(aDest.width / aSource.width,
                                      aDest.height / aSource.height);
      matrix.PreTranslate(-aSource.x, -aSource.y);
      matrix.PostTranslate(aDest.x, aDest.y);
      AutoRestoreTransform autoRestoreTransform(mTarget);
      mTarget->ConcatTransform(matrix);
      mTarget->DrawShadow(path, pattern, shadowParams, aOptions);
    }
    mTarget->DrawSurface(aSurface, aDest, aSource, aSurfOptions, aOptions);
  }

 private:
  gfx::Rect MaxSourceNeededBoundsForFilter(const gfx::Rect& aDestBounds,
                                           CanvasRenderingContext2D* aCtx) {
    const bool applyFilter = aCtx->NeedToApplyFilter();
    if (!aCtx->IsTargetValid()) {
      return aDestBounds;
    }
    if (!applyFilter) {
      return aDestBounds;
    }

    nsIntRegion sourceGraphicNeededRegion;
    nsIntRegion fillPaintNeededRegion;
    nsIntRegion strokePaintNeededRegion;

    FilterSupport::ComputeSourceNeededRegions(
        aCtx->CurrentState().filter, gfx::RoundedToInt(aDestBounds),
        sourceGraphicNeededRegion, fillPaintNeededRegion,
        strokePaintNeededRegion);

    return gfx::Rect(sourceGraphicNeededRegion.GetBounds());
  }

  gfx::Rect MaxSourceNeededBoundsForShadow(const gfx::Rect& aDestBounds,
                                           CanvasRenderingContext2D* aCtx) {
    if (!aCtx->NeedToDrawShadow()) {
      return aDestBounds;
    }

    const ContextState& state = aCtx->CurrentState();
    gfx::Rect sourceBounds = aDestBounds - state.shadowOffset;
    sourceBounds.Inflate(state.ShadowBlurRadius());

    // Union the shadow source with the original rect because we're going to
    // draw both.
    return sourceBounds.Union(aDestBounds);
  }

  gfx::Rect BoundsAfterFilter(const gfx::Rect& aBounds,
                              CanvasRenderingContext2D* aCtx) {
    const bool applyFilter = aCtx->NeedToApplyFilter();
    if (!aCtx->IsTargetValid()) {
      return aBounds;
    }
    if (!applyFilter) {
      return aBounds;
    }

    gfx::Rect bounds(aBounds);
    bounds.RoundOut();

    gfx::IntRect intBounds;
    if (!bounds.ToIntRect(&intBounds)) {
      return gfx::Rect();
    }

    nsIntRegion extents = gfx::FilterSupport::ComputePostFilterExtents(
        aCtx->CurrentState().filter, intBounds);
    return gfx::Rect(extents.GetBounds());
  }

  CanvasRenderingContext2D* mCtx;
  bool mOptimizeShadow;
  CompositionOp mUsedOperation;
  RefPtr<DrawTarget> mTarget;
  UniquePtr<AdjustedTargetForShadow> mShadowTarget;
  UniquePtr<AdjustedTargetForFilter> mFilterTarget;
};

void CanvasPattern::SetTransform(const DOMMatrix2DInit& aInit,
                                 ErrorResult& aError) {
  RefPtr<DOMMatrixReadOnly> matrix =
      DOMMatrixReadOnly::FromMatrix(GetParentObject(), aInit, aError);
  if (aError.Failed()) {
    return;
  }
  const auto* matrix2D = matrix->GetInternal2D();
  if (!matrix2D->IsFinite()) {
    return;
  }
  mTransform = Matrix(*matrix2D);
}

void CanvasGradient::AddColorStop(float aOffset, const nsACString& aColorstr,
                                  ErrorResult& aRv) {
  if (aOffset < 0.0 || aOffset > 1.0) {
    return aRv.ThrowIndexSizeError("Offset out of 0-1.0 range");
  }

  if (!mContext) {
    return aRv.ThrowSyntaxError("No canvas context");
  }

  auto color = mContext->ParseColor(
      aColorstr, CanvasRenderingContext2D::ResolveCurrentColor::No);
  if (!color) {
    return aRv.ThrowSyntaxError("Invalid color");
  }

  GradientStop newStop;

  newStop.offset = aOffset;
  newStop.color = ToDeviceColor(*color);

  mRawStops.AppendElement(newStop);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CanvasGradient, mContext)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CanvasPattern, mContext)

NS_IMPL_CYCLE_COLLECTING_ADDREF(CanvasRenderingContext2D)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CanvasRenderingContext2D)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(CanvasRenderingContext2D)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CanvasRenderingContext2D)
  // Make sure we remove ourselves from the list of demotable contexts (raw
  // pointers), since we're logically destructed at this point.
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCanvasElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOffscreenCanvas)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocShell)
  for (uint32_t i = 0; i < tmp->mStyleStack.Length(); i++) {
    ImplCycleCollectionUnlink(tmp->mStyleStack[i].patternStyles[Style::STROKE]);
    ImplCycleCollectionUnlink(tmp->mStyleStack[i].patternStyles[Style::FILL]);
    ImplCycleCollectionUnlink(
        tmp->mStyleStack[i].gradientStyles[Style::STROKE]);
    ImplCycleCollectionUnlink(tmp->mStyleStack[i].gradientStyles[Style::FILL]);
    if (auto* autoSVGFiltersObserver =
            tmp->mStyleStack[i].autoSVGFiltersObserver.get()) {
      /*
       * XXXjwatt: I don't think this is doing anything useful.  All we do under
       * this function is clear a raw C-style (i.e. not strong) pointer.  That's
       * clearly not helping in breaking any cycles.  The fact that we MOZ_CRASH
       * in OnRenderingChange if that pointer is null indicates that this isn't
       * even doing anything useful in terms of preventing further invalidation
       * from any observed filters.
       */
      autoSVGFiltersObserver->Detach();
    }
    ImplCycleCollectionUnlink(tmp->mStyleStack[i].autoSVGFiltersObserver);
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(CanvasRenderingContext2D)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCanvasElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOffscreenCanvas)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocShell)
  for (uint32_t i = 0; i < tmp->mStyleStack.Length(); i++) {
    ImplCycleCollectionTraverse(
        cb, tmp->mStyleStack[i].patternStyles[Style::STROKE],
        "Stroke CanvasPattern");
    ImplCycleCollectionTraverse(cb,
                                tmp->mStyleStack[i].patternStyles[Style::FILL],
                                "Fill CanvasPattern");
    ImplCycleCollectionTraverse(
        cb, tmp->mStyleStack[i].gradientStyles[Style::STROKE],
        "Stroke CanvasGradient");
    ImplCycleCollectionTraverse(cb,
                                tmp->mStyleStack[i].gradientStyles[Style::FILL],
                                "Fill CanvasGradient");
    ImplCycleCollectionTraverse(cb, tmp->mStyleStack[i].autoSVGFiltersObserver,
                                "RAII SVG Filters Observer");
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(CanvasRenderingContext2D)
  if (nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper()) {
    dom::Element* canvasElement = tmp->mCanvasElement;
    if (canvasElement) {
      if (canvasElement->IsPurple()) {
        canvasElement->RemovePurple();
      }
      dom::Element::MarkNodeChildren(canvasElement);
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(CanvasRenderingContext2D)
  return nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(CanvasRenderingContext2D)
  return nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CanvasRenderingContext2D)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsICanvasRenderingContextInternal)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

CanvasRenderingContext2D::ContextState::ContextState() = default;

CanvasRenderingContext2D::ContextState::ContextState(const ContextState& aOther)
    : fontGroup(aOther.fontGroup),
      fontLanguage(aOther.fontLanguage),
      fontFont(aOther.fontFont),
      gradientStyles(aOther.gradientStyles),
      patternStyles(aOther.patternStyles),
      colorStyles(aOther.colorStyles),
      font(aOther.font),
      textAlign(aOther.textAlign),
      textBaseline(aOther.textBaseline),
      textDirection(aOther.textDirection),
      fontKerning(aOther.fontKerning),
      fontStretch(aOther.fontStretch),
      fontVariantCaps(aOther.fontVariantCaps),
      textRendering(aOther.textRendering),
      letterSpacing(aOther.letterSpacing),
      wordSpacing(aOther.wordSpacing),
      fontLineHeight(aOther.fontLineHeight),
      letterSpacingStr(aOther.letterSpacingStr),
      wordSpacingStr(aOther.wordSpacingStr),
      shadowColor(aOther.shadowColor),
      transform(aOther.transform),
      shadowOffset(aOther.shadowOffset),
      lineWidth(aOther.lineWidth),
      miterLimit(aOther.miterLimit),
      globalAlpha(aOther.globalAlpha),
      shadowBlur(aOther.shadowBlur),
      dash(aOther.dash.Clone()),
      dashOffset(aOther.dashOffset),
      op(aOther.op),
      fillRule(aOther.fillRule),
      lineCap(aOther.lineCap),
      lineJoin(aOther.lineJoin),
      filterString(aOther.filterString),
      filterChain(aOther.filterChain),
      autoSVGFiltersObserver(aOther.autoSVGFiltersObserver),
      filter(aOther.filter),
      filterAdditionalImages(aOther.filterAdditionalImages.Clone()),
      filterSourceGraphicTainted(aOther.filterSourceGraphicTainted),
      imageSmoothingEnabled(aOther.imageSmoothingEnabled),
      fontExplicitLanguage(aOther.fontExplicitLanguage) {}

CanvasRenderingContext2D::ContextState::~ContextState() = default;

void CanvasRenderingContext2D::ContextState::SetColorStyle(Style aWhichStyle,
                                                           nscolor aColor) {
  colorStyles[aWhichStyle] = aColor;
  gradientStyles[aWhichStyle] = nullptr;
  patternStyles[aWhichStyle] = nullptr;
}

void CanvasRenderingContext2D::ContextState::SetPatternStyle(
    Style aWhichStyle, CanvasPattern* aPat) {
  gradientStyles[aWhichStyle] = nullptr;
  patternStyles[aWhichStyle] = aPat;
}

void CanvasRenderingContext2D::ContextState::SetGradientStyle(
    Style aWhichStyle, CanvasGradient* aGrad) {
  gradientStyles[aWhichStyle] = aGrad;
  patternStyles[aWhichStyle] = nullptr;
}

/**
 ** CanvasRenderingContext2D impl
 **/

// Initialize our static variables.
MOZ_THREAD_LOCAL(uintptr_t) CanvasRenderingContext2D::sNumLivingContexts;
MOZ_THREAD_LOCAL(DrawTarget*) CanvasRenderingContext2D::sErrorTarget;

// Helpers to map Canvas2D WebIDL enum values to gfx constants for rendering.
static JoinStyle CanvasToGfx(CanvasLineJoin aJoin) {
  switch (aJoin) {
    case CanvasLineJoin::Round:
      return JoinStyle::ROUND;
    case CanvasLineJoin::Bevel:
      return JoinStyle::BEVEL;
    case CanvasLineJoin::Miter:
      return JoinStyle::MITER_OR_BEVEL;
    default:
      MOZ_CRASH("unknown lineJoin!");
  }
}

static CapStyle CanvasToGfx(CanvasLineCap aCap) {
  switch (aCap) {
    case CanvasLineCap::Butt:
      return CapStyle::BUTT;
    case CanvasLineCap::Round:
      return CapStyle::ROUND;
    case CanvasLineCap::Square:
      return CapStyle::SQUARE;
    default:
      MOZ_CRASH("unknown lineCap!");
  }
}

static uint8_t CanvasToGfx(CanvasFontKerning aKerning) {
  switch (aKerning) {
    case CanvasFontKerning::Auto:
      return NS_FONT_KERNING_AUTO;
    case CanvasFontKerning::Normal:
      return NS_FONT_KERNING_NORMAL;
    case CanvasFontKerning::None:
      return NS_FONT_KERNING_NONE;
    default:
      MOZ_CRASH("unknown kerning!");
  }
}

CanvasRenderingContext2D::CanvasRenderingContext2D(
    layers::LayersBackend aCompositorBackend)
    :  // these are the default values from the Canvas spec
      mWidth(0),
      mHeight(0),
      mZero(false),
      mOpaqueAttrValue(false),
      mContextAttributesHasAlpha(true),
      mOpaque(false),
      mResetLayer(true),
      mIPC(false),
      mHasPendingStableStateCallback(false),
      mIsEntireFrameInvalid(false),
      mPredictManyRedrawCalls(false),
      mFrameCaptureState(FrameCaptureState::CLEAN,
                         "CanvasRenderingContext2D::mFrameCaptureState"),
      mInvalidateCount(0),
      mWriteOnly(false) {
  sNumLivingContexts.infallibleInit();
  sErrorTarget.infallibleInit();
  sNumLivingContexts.set(sNumLivingContexts.get() + 1);
}

CanvasRenderingContext2D::~CanvasRenderingContext2D() {
  CanvasImageCache::NotifyCanvasDestroyed(this);
  RemovePostRefreshObserver();
  RemoveShutdownObserver();
  ResetBitmap();

  sNumLivingContexts.set(sNumLivingContexts.get() - 1);
  if (sNumLivingContexts.get() == 0 && sErrorTarget.get()) {
    RefPtr<DrawTarget> target = dont_AddRef(sErrorTarget.get());
    sErrorTarget.set(nullptr);
  }
}

nsresult CanvasRenderingContext2D::Initialize() {
  if (NS_WARN_IF(!AddShutdownObserver())) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

JSObject* CanvasRenderingContext2D::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return CanvasRenderingContext2D_Binding::Wrap(aCx, this, aGivenProto);
}

void CanvasRenderingContext2D::GetContextAttributes(
    CanvasRenderingContext2DSettings& aSettings) const {
  aSettings = CanvasRenderingContext2DSettings();

  aSettings.mAlpha = mContextAttributesHasAlpha;
  aSettings.mWillReadFrequently = mWillReadFrequently;
  aSettings.mForceSoftwareRendering = mForceSoftwareRendering;

  // We don't support the 'desynchronized' and 'colorSpace' attributes, so
  // those just keep their default values.
}

void CanvasRenderingContext2D::GetDebugInfo(
    bool aEnsureTarget, CanvasRenderingContext2DDebugInfo& aDebugInfo,
    ErrorResult& aError) {
  if (aEnsureTarget && !EnsureTarget(aError)) {
    return;
  }

  if (!mBufferProvider) {
    aError.ThrowInvalidStateError("No buffer provider available");
    return;
  }

  if (!mTarget) {
    aError.ThrowInvalidStateError("No target available");
    return;
  }

  aDebugInfo.mIsAccelerated = mBufferProvider->IsAccelerated();
  aDebugInfo.mIsShared = mBufferProvider->IsShared();
  aDebugInfo.mBackendType = static_cast<int8_t>(mTarget->GetBackendType());
  aDebugInfo.mDrawTargetType = static_cast<int8_t>(mTarget->GetType());
}

CanvasRenderingContext2D::ColorStyleCacheEntry
CanvasRenderingContext2D::ParseColorSlow(const nsACString& aString) {
  ColorStyleCacheEntry result{nsCString(aString)};
  Document* document = mCanvasElement ? mCanvasElement->OwnerDoc() : nullptr;
  css::Loader* loader = document ? document->CSSLoader() : nullptr;

  PresShell* presShell = GetPresShell();
  ServoStyleSet* set = presShell ? presShell->StyleSet() : nullptr;
  bool wasCurrentColor = false;
  nscolor color;
  if (ServoCSSParser::ComputeColor(set, NS_RGB(0, 0, 0), aString, &color,
                                   &wasCurrentColor, loader)) {
    result.mWasCurrentColor = wasCurrentColor;
    result.mColor.emplace(color);
  }

  return result;
}

Maybe<nscolor> CanvasRenderingContext2D::ParseColor(
    const nsACString& aString, ResolveCurrentColor aResolveCurrentColor) {
  auto entry = mColorStyleCache.Lookup(aString);
  if (!entry) {
    entry.Set(ParseColorSlow(aString));
  }

  const auto& data = entry.Data();
  if (data.mWasCurrentColor && mCanvasElement &&
      aResolveCurrentColor == ResolveCurrentColor::Yes) {
    // If it was currentColor, get the value of the color property, flushing
    // style if necessary.
    RefPtr<const ComputedStyle> canvasStyle =
        nsComputedDOMStyle::GetComputedStyle(mCanvasElement);
    if (canvasStyle) {
      return Some(canvasStyle->StyleText()->mColor.ToColor());
    }
  }
  return data.mColor;
}

void CanvasRenderingContext2D::ResetBitmap(bool aFreeBuffer) {
  if (mCanvasElement) {
    mCanvasElement->InvalidateCanvas();
  }

  // only do this for non-docshell created contexts,
  // since those are the ones that we created a surface for
  if (mTarget && IsTargetValid() && !mDocShell) {
    gCanvasAzureMemoryUsed -= mWidth * mHeight * 4;
  }

  bool forceReset = true;
  ReturnTarget(forceReset);
  mTarget = nullptr;
  if (aFreeBuffer) {
    mBufferProvider = nullptr;
  } else if (mBufferProvider) {
    // Try to keep the buffer around. However, we still need to clear the
    // contents as if it was recreated before next use.
    mBufferNeedsClear = true;
  }

  // Since the target changes the backing texture will change, and this will
  // no longer be valid.
  mIsEntireFrameInvalid = false;
  mPredictManyRedrawCalls = false;
  mFrameCaptureState = FrameCaptureState::CLEAN;
}

void CanvasRenderingContext2D::OnShutdown() {
  RefPtr<PersistentBufferProvider> provider = mBufferProvider;

  ResetBitmap();

  if (provider) {
    provider->OnShutdown();
  }

  if (mOffscreenCanvas) {
    mOffscreenCanvas->Destroy();
  }

  mHasShutdown = true;
}

bool CanvasRenderingContext2D::AddShutdownObserver() {
  auto* const canvasManager = CanvasShutdownManager::Get();
  if (NS_WARN_IF(!canvasManager)) {
    mHasShutdown = true;
    return false;
  }

  canvasManager->AddShutdownObserver(this);
  return true;
}

void CanvasRenderingContext2D::RemoveShutdownObserver() {
  auto* const canvasManager = CanvasShutdownManager::MaybeGet();
  if (!canvasManager) {
    return;
  }

  canvasManager->RemoveShutdownObserver(this);
}

void CanvasRenderingContext2D::OnRemoteCanvasLost() {
  // We only lose context / data if we are using remote canvas, which is only
  // for accelerated targets.
  if (!mBufferProvider || !mBufferProvider->IsAccelerated() || mIsContextLost) {
    return;
  }

  // 2. Set context's context lost to true.
  mIsContextLost = mAllowContextRestore = true;

  // 3. Reset the rendering context to its default state given context.
  ClearTarget();

  // We dispatch because it isn't safe to call into the script event handlers,
  // and we don't want to mutate our state in CanvasShutdownManager.
  NS_DispatchToCurrentThread(NS_NewCancelableRunnableFunction(
      "CanvasRenderingContext2D::OnRemoteCanvasLost", [self = RefPtr{this}] {
        // 4. Let shouldRestore be the result of firing an event named
        // contextlost at canvas, with the cancelable attribute initialized to
        // true.
        self->mAllowContextRestore = self->DispatchEvent(
            u"contextlost"_ns, CanBubble::eNo, Cancelable::eYes);
        gfxCriticalNote << gfx::hexa(self.get())
                        << " accel canvas lost, can restore: "
                        << self->mAllowContextRestore;
      }));
}

void CanvasRenderingContext2D::OnRemoteCanvasRestored() {
  // We never lost our context if it was not a remote canvas, nor can we restore
  // if we have already shutdown.
  if (mHasShutdown || !mIsContextLost || !mAllowContextRestore) {
    return;
  }

  // We dispatch because it isn't safe to call into the script event handlers,
  // and we don't want to mutate our state in CanvasShutdownManager.
  NS_DispatchToCurrentThread(NS_NewCancelableRunnableFunction(
      "CanvasRenderingContext2D::OnRemoteCanvasRestored",
      [self = RefPtr{this}] {
        // 5. If shouldRestore is false, then abort these steps.
        if (!self->mHasShutdown && self->mIsContextLost &&
            self->mAllowContextRestore) {
          // 7. Set context's context lost to false.
          self->mIsContextLost = false;

          // 6. Attempt to restore context by creating a backing storage using
          // context's attributes and associating them with context. If this
          // fails, then abort these steps.
          if (!self->EnsureTarget()) {
            gfxCriticalNote << gfx::hexa(self.get())
                            << " accel canvas failed to restore";
            self->mIsContextLost = true;
            return;
          }

          // 8. Fire an event named contextrestored at canvas.
          self->DispatchEvent(u"contextrestored"_ns, CanBubble::eNo,
                              Cancelable::eNo);
          gfxCriticalNote << gfx::hexa(self.get()) << " accel canvas restored";
        }
      }));
}

void CanvasRenderingContext2D::SetStyleFromString(const nsACString& aStr,
                                                  Style aWhichStyle) {
  MOZ_ASSERT(!aStr.IsVoid());

  Maybe<nscolor> color = ParseColor(aStr);
  if (!color) {
    return;
  }

  CurrentState().SetColorStyle(aWhichStyle, *color);
}

void CanvasRenderingContext2D::GetStyleAsUnion(
    OwningUTF8StringOrCanvasGradientOrCanvasPattern& aValue,
    Style aWhichStyle) {
  const ContextState& state = CurrentState();
  if (state.patternStyles[aWhichStyle]) {
    aValue.SetAsCanvasPattern() = state.patternStyles[aWhichStyle];
  } else if (state.gradientStyles[aWhichStyle]) {
    aValue.SetAsCanvasGradient() = state.gradientStyles[aWhichStyle];
  } else {
    StyleColorToString(state.colorStyles[aWhichStyle],
                       aValue.SetAsUTF8String());
  }
}

// static
void CanvasRenderingContext2D::StyleColorToString(const nscolor& aColor,
                                                  nsACString& aStr) {
  aStr.Truncate();
  // We can't reuse the normal CSS color stringification code,
  // because the spec calls for a different algorithm for canvas.
  if (NS_GET_A(aColor) == 255) {
    aStr.AppendPrintf("#%02x%02x%02x", NS_GET_R(aColor), NS_GET_G(aColor),
                      NS_GET_B(aColor));
  } else {
    aStr.AppendPrintf("rgba(%d, %d, %d, ", NS_GET_R(aColor), NS_GET_G(aColor),
                      NS_GET_B(aColor));
    aStr.AppendFloat(nsStyleUtil::ColorComponentToFloat(NS_GET_A(aColor)));
    aStr.Append(')');
  }
}

nsresult CanvasRenderingContext2D::Redraw() {
  mFrameCaptureState = FrameCaptureState::DIRTY;

  if (mIsEntireFrameInvalid) {
    return NS_OK;
  }

  mIsEntireFrameInvalid = true;

  if (mCanvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(mCanvasElement);
    mCanvasElement->InvalidateCanvasContent(nullptr);
  } else if (mOffscreenCanvas) {
    mOffscreenCanvas->QueueCommitToCompositor();
  } else {
    NS_ASSERTION(mDocShell, "Redraw with no canvas element or docshell!");
  }

  return NS_OK;
}

void CanvasRenderingContext2D::Redraw(const gfx::Rect& aR) {
  mFrameCaptureState = FrameCaptureState::DIRTY;

  ++mInvalidateCount;

  if (mIsEntireFrameInvalid) {
    return;
  }

  if (mPredictManyRedrawCalls || mInvalidateCount > kCanvasMaxInvalidateCount) {
    Redraw();
    return;
  }

  if (mCanvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(mCanvasElement);
    mCanvasElement->InvalidateCanvasContent(&aR);
  } else if (mOffscreenCanvas) {
    mOffscreenCanvas->QueueCommitToCompositor();
  } else {
    NS_ASSERTION(mDocShell, "Redraw with no canvas element or docshell!");
  }
}

void CanvasRenderingContext2D::DidRefresh() {}

void CanvasRenderingContext2D::RedrawUser(const gfxRect& aR) {
  mFrameCaptureState = FrameCaptureState::DIRTY;

  if (mIsEntireFrameInvalid) {
    ++mInvalidateCount;
    return;
  }

  gfx::Rect newr = mTarget->GetTransform().TransformBounds(ToRect(aR));
  Redraw(newr);
}

bool CanvasRenderingContext2D::CopyBufferProvider(
    PersistentBufferProvider& aOld, DrawTarget& aTarget, IntRect aCopyRect) {
  // Borrowing the snapshot must be done after ReturnTarget.
  RefPtr<SourceSurface> snapshot = aOld.BorrowSnapshot();

  if (!snapshot) {
    return false;
  }

  aTarget.CopySurface(snapshot, aCopyRect, IntPoint());

  aOld.ReturnSnapshot(snapshot.forget());
  return true;
}

void CanvasRenderingContext2D::Demote() {}

void CanvasRenderingContext2D::ScheduleStableStateCallback() {
  if (mHasPendingStableStateCallback) {
    return;
  }
  mHasPendingStableStateCallback = true;

  nsContentUtils::RunInStableState(
      NewRunnableMethod("dom::CanvasRenderingContext2D::OnStableState", this,
                        &CanvasRenderingContext2D::OnStableState));
}

void CanvasRenderingContext2D::OnStableState() {
  if (!mHasPendingStableStateCallback) {
    return;
  }

  ReturnTarget();

  mHasPendingStableStateCallback = false;
}

void CanvasRenderingContext2D::RestoreClipsAndTransformToTarget() {
  // Restore clips and transform.
  mTarget->SetTransform(Matrix());

  if (mTarget->GetBackendType() == gfx::BackendType::CAIRO) {
    // Cairo doesn't play well with huge clips. When given a very big clip it
    // will try to allocate big mask surface without taking the target
    // size into account which can cause OOM. See bug 1034593.
    // This limits the clip extents to the size of the canvas.
    // A fix in Cairo would probably be preferable, but requires somewhat
    // invasive changes.
    mTarget->PushClipRect(gfx::Rect(0, 0, mWidth, mHeight));
  }

  for (auto& style : mStyleStack) {
    for (auto& clipOrTransform : style.clipsAndTransforms) {
      if (clipOrTransform.IsClip()) {
        if (clipOrTransform.clip->GetBackendType() != mPathType) {
          // We have possibly changed backends, so we need to convert the clips
          // in case they are no longer compatible with mTarget.
          RefPtr<PathBuilder> pathBuilder = mTarget->CreatePathBuilder();
          clipOrTransform.clip->StreamToSink(pathBuilder);
          clipOrTransform.clip = pathBuilder->Finish();
        }
        mTarget->PushClip(clipOrTransform.clip);
      } else {
        mTarget->SetTransform(clipOrTransform.transform);
      }
    }
  }
}

bool CanvasRenderingContext2D::BorrowTarget(const IntRect& aPersistedRect,
                                            bool aNeedsClear) {
  // We are attempting to request a DrawTarget from the current
  // PersistentBufferProvider. However, if the provider needs to be refreshed,
  // or if it is accelerated and the application has requested that we disallow
  // acceleration, then we skip trying to use this provider so that it will be
  // recreated by EnsureTarget later.
  if (!mBufferProvider || mBufferProvider->RequiresRefresh() ||
      (mBufferProvider->IsAccelerated() && UseSoftwareRendering())) {
    return false;
  }
  mTarget = mBufferProvider->BorrowDrawTarget(aPersistedRect);
  if (!mTarget || !mTarget->IsValid()) {
    if (mTarget) {
      mBufferProvider->ReturnDrawTarget(mTarget.forget());
    }
    return false;
  }
  if (!mBufferProvider->PreservesDrawingState() || mBufferNeedsClear ||
      mTargetNeedsClipsAndTransforms) {
    if (mBufferProvider->PreservesDrawingState()) {
      // If the buffer provider preserves the clip and transform state, then
      // we must ensure it is cleared before reusing the target.
      if (!mTarget->RemoveAllClips()) {
        mBufferProvider->ReturnDrawTarget(mTarget.forget());
        return false;
      }
      mTarget->SetTransform(Matrix());
    }

    // If the canvas was reset, then we need to clear the target in case its
    // contents was somehow preserved. We only need to clear the target if
    // the operation doesn't fill the entire canvas.
    if (mBufferNeedsClear && aNeedsClear) {
      mTarget->ClearRect(gfx::Rect(mTarget->GetRect()));
    }

    RestoreClipsAndTransformToTarget();

    mBufferNeedsClear = false;
    mTargetNeedsClipsAndTransforms = false;
  }
  return true;
}

bool CanvasRenderingContext2D::HasAnyClips() const {
  for (const auto& style : mStyleStack) {
    for (const auto& clipOrTransform : style.clipsAndTransforms) {
      if (clipOrTransform.IsClip()) {
        return true;
      }
    }
  }
  return false;
}

bool CanvasRenderingContext2D::HasErrorState(ErrorResult& aError,
                                             bool aInitProvider) {
  // If there is no buffer provider, then attempt to initialize it to flush any
  // error state. It also forces any backend resources (such as WebGL contexts)
  // in the buffer provider to initialize as soon as possible, so that it may
  // speed up initial page loads.
  // It is normally beneficial to delay initialization of just the target until
  // we encounter a drawing operation, since this may allow us to elide copying
  // the old contents of the target to the new target if the drawing operation
  // would overwrite the entire framebuffer contents.
  // However, if there is no old target to copy from, there is no benefit to
  // delaying it. It may incidentally delay creation of the buffer provider,
  // which is an expensive operation that benefits from being scheduled as soon
  // as possible. Thus, we only want to delay initialization of the target when
  // a buffer provider already exists.
  if (aInitProvider && !mBufferProvider && !EnsureTarget(aError)) {
    return true;
  }

  if (AlreadyShutDown()) {
    gfxCriticalNoteOnce << "Attempt to render into a Canvas2d after shutdown.";
    SetErrorState();
    aError.ThrowInvalidStateError(
        "Cannot use canvas after shutdown initiated.");
    return true;
  }

  // The spec doesn't say what to do in this case, but Chrome silently fails
  // without throwing an error. We should at least throw if the canvas is
  // permanently disabled.
  if (NS_WARN_IF(mIsContextLost)) {
    if (!mAllowContextRestore) {
      aError.ThrowInvalidStateError(
          "Cannot use canvas as context is lost forever.");
    }
    return true;
  }

  if (mTarget && mTarget == sErrorTarget.get()) {
    aError.ThrowInvalidStateError("Canvas is already in error state.");
    return true;
  }

  return false;
}

bool CanvasRenderingContext2D::EnsureTarget(ErrorResult& aError,
                                            const gfx::Rect* aCoveredRect,
                                            bool aWillClear,
                                            bool aSkipTransform) {
  if (HasErrorState(aError, false)) {
    return false;
  }

  // If there is an existing target and no error state, just reuse it.
  if (mTarget) {
    return true;
  }

  // Check that the dimensions are sane
  if (mWidth > StaticPrefs::gfx_canvas_max_size() ||
      mHeight > StaticPrefs::gfx_canvas_max_size()) {
    SetErrorState();
    aError.ThrowInvalidStateError("Canvas exceeds max size.");
    return false;
  }

  if (mWidth < 0 || mHeight < 0) {
    SetErrorState();
    aError.ThrowInvalidStateError("Canvas has invalid size.");
    return false;
  }

  // If the next drawing command covers the entire canvas, we can skip copying
  // from the previous frame and/or clearing the canvas.
  // If a clip is active we don't know for sure that the next drawing command
  // will really cover the entire canvas.
  gfx::Rect canvasRect(0, 0, mWidth, mHeight);
  bool canDiscardContent =
      aCoveredRect &&
      (aSkipTransform ? *aCoveredRect
                      : GetCurrentTransform().TransformBounds(*aCoveredRect))
          .Contains(canvasRect) &&
      !HasAnyClips();

  ScheduleStableStateCallback();

  IntRect persistedRect = canDiscardContent || mBufferNeedsClear
                              ? IntRect()
                              : IntRect(0, 0, mWidth, mHeight);

  // Attempt to reuse the existing buffer provider.
  if (BorrowTarget(persistedRect, !canDiscardContent)) {
    return true;
  }

  RefPtr<DrawTarget> newTarget;
  RefPtr<PersistentBufferProvider> newProvider;

  if (!TryAcceleratedTarget(newTarget, newProvider) &&
      !TrySharedTarget(newTarget, newProvider) &&
      !TryBasicTarget(newTarget, newProvider, aError)) {
    gfxCriticalError(
        CriticalLog::DefaultOptions(Factory::ReasonableSurfaceSize(GetSize())))
        << "Failed borrow shared and basic targets.";

    SetErrorState();
    return false;
  }

  MOZ_ASSERT(newTarget);
  MOZ_ASSERT(newProvider);

  bool needsClear =
      !canDiscardContent || (mBufferProvider && mBufferNeedsClear);
  if (newTarget->GetBackendType() == gfx::BackendType::SKIA &&
      (needsClear || !aWillClear)) {
    // Skia expects the unused X channel to contains 0xFF even for opaque
    // operations so we can't skip clearing in that case, even if we are going
    // to cover the entire canvas in the next drawing operation.
    newTarget->ClearRect(canvasRect);
    needsClear = false;
  }

  // Try to copy data from the previous buffer provider if there is one.
  if (!canDiscardContent && mBufferProvider && !mBufferNeedsClear &&
      CopyBufferProvider(*mBufferProvider, *newTarget, persistedRect)) {
    needsClear = false;
  }

  if (needsClear) {
    newTarget->ClearRect(canvasRect);
  }

  // Ensure any Path state is compatible with the type of DrawTarget used. This
  // may require making a copy with the correct type if they (rarely) mismatch.
  mPathType = newTarget->GetPathType();
  MOZ_ASSERT(mPathType != BackendType::NONE);
  if (mPathBuilder && mPathBuilder->GetBackendType() != mPathType) {
    RefPtr<Path> path = mPathBuilder->Finish();
    mPathBuilder = newTarget->CreatePathBuilder(path->GetFillRule());
    path->StreamToSink(mPathBuilder);
  }
  if (mPath && mPath->GetBackendType() != mPathType) {
    RefPtr<PathBuilder> builder =
        newTarget->CreatePathBuilder(mPath->GetFillRule());
    mPath->StreamToSink(builder);
    mPath = builder->Finish();
  }

  mTarget = std::move(newTarget);
  mBufferProvider = std::move(newProvider);
  mBufferNeedsClear = false;
  mTargetNeedsClipsAndTransforms = false;

  RegisterAllocation();
  AddZoneWaitingForGC();

  RestoreClipsAndTransformToTarget();

  // Force a full layer transaction since we didn't have a layer before
  // and now we might need one.
  if (mCanvasElement) {
    mCanvasElement->InvalidateCanvas();
  }
  // EnsureTarget hasn't drawn anything. Preserve mFrameCaptureState.
  FrameCaptureState captureState = mFrameCaptureState;
  // Calling Redraw() tells our invalidation machinery that the entire
  // canvas is already invalid, which can speed up future drawing.
  Redraw();
  mFrameCaptureState = captureState;

  return true;
}

void CanvasRenderingContext2D::SetInitialState() {
  // Set up the initial canvas defaults
  mPathBuilder = nullptr;
  mPath = nullptr;
  mPathPruned = false;
  mPathTransform = Matrix();
  mPathTransformDirty = false;
  mPathType =
      (mTarget ? mTarget : gfxPlatform::ThreadLocalScreenReferenceDrawTarget())
          ->GetPathType();
  MOZ_ASSERT(mPathType != BackendType::NONE);

  mStyleStack.Clear();
  ContextState* state = mStyleStack.AppendElement();
  state->globalAlpha = 1.0;

  state->colorStyles[Style::FILL] = NS_RGB(0, 0, 0);
  state->colorStyles[Style::STROKE] = NS_RGB(0, 0, 0);
  state->shadowColor = NS_RGBA(0, 0, 0, 0);
}

void CanvasRenderingContext2D::SetErrorState() {
  EnsureErrorTarget();

  if (mTarget && mTarget != sErrorTarget.get()) {
    gCanvasAzureMemoryUsed -= mWidth * mHeight * 4;
  }

  mTarget = sErrorTarget.get();
  mBufferProvider = nullptr;

  // clear transforms, clips, etc.
  SetInitialState();
}

void CanvasRenderingContext2D::RegisterAllocation() {
  // XXX - It would make more sense to track the allocation in
  // PeristentBufferProvider, rather than here.
  static bool registered = false;
  // FIXME: Disable the reporter for now, see bug 1241865
  if (!registered && false) {
    registered = true;
    RegisterStrongMemoryReporter(new Canvas2dPixelsReporter());
  }
}

void CanvasRenderingContext2D::AddZoneWaitingForGC() {
  JSObject* wrapper = GetWrapperPreserveColor();
  if (wrapper) {
    CycleCollectedJSRuntime::Get()->AddZoneWaitingForGC(
        JS::GetObjectZone(wrapper));
  }
}

static WindowRenderer* WindowRendererFromCanvasElement(
    nsINode* aCanvasElement) {
  if (!aCanvasElement) {
    return nullptr;
  }

  return nsContentUtils::WindowRendererForDocument(aCanvasElement->OwnerDoc());
}

bool CanvasRenderingContext2D::TryAcceleratedTarget(
    RefPtr<gfx::DrawTarget>& aOutDT,
    RefPtr<layers::PersistentBufferProvider>& aOutProvider) {
  if (!XRE_IsContentProcess()) {
    // Only allow accelerated contexts to be created in a content process to
    // ensure it is remoted appropriately and run on the correct parent or
    // GPU process threads.
    return false;
  }
  if (mBufferProvider && mBufferProvider->IsAccelerated() &&
      mBufferProvider->RequiresRefresh()) {
    // If there is already a provider and we got here, then the provider needs
    // to be refreshed and we should avoid using acceleration in the future.
    mAllowAcceleration = false;
  }
  // Don't try creating an accelerate DrawTarget if either acceleration failed
  // previously or if the application expects acceleration to be slow.
  if (!mAllowAcceleration || UseSoftwareRendering()) {
    return false;
  }

  if (mCanvasElement) {
    MOZ_ASSERT(NS_IsMainThread());

    WindowRenderer* renderer = WindowRendererFromCanvasElement(mCanvasElement);
    if (NS_WARN_IF(!renderer)) {
      return false;
    }

    aOutProvider = PersistentBufferProviderAccelerated::Create(
        GetSize(), GetSurfaceFormat(), renderer->AsKnowsCompositor());
  } else if (mOffscreenCanvas &&
             StaticPrefs::gfx_canvas_remote_allow_offscreen()) {
    RefPtr<ImageBridgeChild> imageBridge = ImageBridgeChild::GetSingleton();
    if (NS_WARN_IF(!imageBridge)) {
      return false;
    }

    aOutProvider = PersistentBufferProviderAccelerated::Create(
        GetSize(), GetSurfaceFormat(), imageBridge);
  }

  if (!aOutProvider) {
    return false;
  }
  aOutDT = aOutProvider->BorrowDrawTarget(IntRect());
  MOZ_ASSERT(aOutDT);
  return !!aOutDT;
}

bool CanvasRenderingContext2D::TrySharedTarget(
    RefPtr<gfx::DrawTarget>& aOutDT,
    RefPtr<layers::PersistentBufferProvider>& aOutProvider) {
  aOutDT = nullptr;
  aOutProvider = nullptr;

  if (mBufferProvider && mBufferProvider->IsShared()) {
    // we are already using a shared buffer provider, we are allocating a new
    // one because the current one failed so let's just fall back to the basic
    // provider.
    return false;
  }

  if (mCanvasElement) {
    MOZ_ASSERT(NS_IsMainThread());

    WindowRenderer* renderer = WindowRendererFromCanvasElement(mCanvasElement);
    if (NS_WARN_IF(!renderer)) {
      return false;
    }

    aOutProvider = renderer->CreatePersistentBufferProvider(
        GetSize(), GetSurfaceFormat(),
        !mAllowAcceleration || UseSoftwareRendering());
  } else if (mOffscreenCanvas) {
    if (!StaticPrefs::gfx_offscreencanvas_shared_provider()) {
      return false;
    }

    RefPtr<layers::ImageBridgeChild> imageBridge =
        layers::ImageBridgeChild::GetSingleton();
    if (NS_WARN_IF(!imageBridge)) {
      return false;
    }

    aOutProvider = PersistentBufferProviderShared::Create(
        GetSize(), GetSurfaceFormat(), imageBridge,
        !mAllowAcceleration || UseSoftwareRendering(),
        mOffscreenCanvas->GetWindowID());
  }

  if (!aOutProvider) {
    return false;
  }

  // We can pass an empty persisted rect since we just created the buffer
  // provider (nothing to restore).
  aOutDT = aOutProvider->BorrowDrawTarget(IntRect());
  MOZ_ASSERT(aOutDT);

  return !!aOutDT;
}

bool CanvasRenderingContext2D::TryBasicTarget(
    RefPtr<gfx::DrawTarget>& aOutDT,
    RefPtr<layers::PersistentBufferProvider>& aOutProvider,
    ErrorResult& aError) {
  aOutDT = gfxPlatform::GetPlatform()->CreateOffscreenCanvasDrawTarget(
      GetSize(), GetSurfaceFormat(), UseSoftwareRendering());
  if (!aOutDT) {
    aError.ThrowInvalidStateError("Canvas could not create basic draw target.");
    return false;
  }

  // See Bug 1524554 - this forces DT initialization.
  aOutDT->ClearRect(gfx::Rect());

  if (!aOutDT->IsValid()) {
    aOutDT = nullptr;
    aError.ThrowInvalidStateError("Canvas could not init basic draw target.");
    return false;
  }

  aOutProvider = new PersistentBufferProviderBasic(aOutDT);
  return true;
}

PersistentBufferProvider* CanvasRenderingContext2D::GetBufferProvider() {
  if (mBufferProvider && mBufferNeedsClear) {
    // Force the buffer to clear before it is used.
    EnsureTarget();
  }
  return mBufferProvider;
}

Maybe<SurfaceDescriptor> CanvasRenderingContext2D::GetFrontBuffer(
    WebGLFramebufferJS*, const bool webvr) {
  if (auto* provider = GetBufferProvider()) {
    return provider->GetFrontBuffer();
  }
  return Nothing();
}

already_AddRefed<layers::FwdTransactionTracker>
CanvasRenderingContext2D::UseCompositableForwarder(
    layers::CompositableForwarder* aForwarder) {
  if (mBufferProvider) {
    return mBufferProvider->UseCompositableForwarder(aForwarder);
  }
  return nullptr;
}

PresShell* CanvasRenderingContext2D::GetPresShell() {
  if (mCanvasElement) {
    return mCanvasElement->OwnerDoc()->GetPresShell();
  }
  if (mDocShell) {
    return mDocShell->GetPresShell();
  }
  return nullptr;
}

NS_IMETHODIMP
CanvasRenderingContext2D::SetDimensions(int32_t aWidth, int32_t aHeight) {
  // Zero sized surfaces can cause problems.
  mZero = false;
  if (aHeight == 0) {
    aHeight = 1;
    mZero = true;
  }
  if (aWidth == 0) {
    aWidth = 1;
    mZero = true;
  }

  ClearTarget(aWidth, aHeight);

  return NS_OK;
}

void CanvasRenderingContext2D::AddAssociatedMemory() {
  JSObject* wrapper = GetWrapperMaybeDead();
  if (wrapper) {
    JS::AddAssociatedMemory(wrapper, BindingJSObjectMallocBytes(this),
                            JS::MemoryUse::DOMBinding);
  }
}

void CanvasRenderingContext2D::RemoveAssociatedMemory() {
  JSObject* wrapper = GetWrapperMaybeDead();
  if (wrapper) {
    JS::RemoveAssociatedMemory(wrapper, BindingJSObjectMallocBytes(this),
                               JS::MemoryUse::DOMBinding);
  }
}

void CanvasRenderingContext2D::ClearTarget(int32_t aWidth, int32_t aHeight) {
  // Only free the buffer provider if the size no longer matches.
  bool freeBuffer = aWidth != mWidth || aHeight != mHeight;
  ResetBitmap(freeBuffer);

  mResetLayer = true;

  SetInitialState();

  // Update dimensions only if new (strictly positive) values were passed.
  if (aWidth > 0 && aHeight > 0) {
    // Update the memory size associated with the wrapper object when we change
    // the dimensions. Note that we need to keep updating dying wrappers before
    // they are finalized so that the memory accounting balances out.
    RemoveAssociatedMemory();
    mWidth = aWidth;
    mHeight = aHeight;
    AddAssociatedMemory();
  }

  if (mOffscreenCanvas) {
    OffscreenCanvasDisplayData data;
    data.mSize = {mWidth, mHeight};
    data.mIsOpaque = mOpaque;
    data.mIsAlphaPremult = true;
    data.mDoPaintCallbacks = true;
    mOffscreenCanvas->UpdateDisplayData(data);
  }

  if (!mCanvasElement || !mCanvasElement->IsInComposedDoc()) {
    return;
  }

  // For vertical writing-mode, unless text-orientation is sideways,
  // we'll modify the initial value of textBaseline to 'middle'.
  RefPtr<const ComputedStyle> canvasStyle =
      nsComputedDOMStyle::GetComputedStyle(mCanvasElement);
  if (canvasStyle) {
    WritingMode wm(canvasStyle);
    if (wm.IsVertical() && !wm.IsSideways()) {
      CurrentState().textBaseline = CanvasTextBaseline::Middle;
    }
  }
}

void CanvasRenderingContext2D::ReturnTarget(bool aForceReset) {
  if (mTarget && mBufferProvider && mTarget != sErrorTarget.get()) {
    CurrentState().transform = mTarget->GetTransform();
    if (aForceReset || !mBufferProvider->PreservesDrawingState()) {
      for (const auto& style : mStyleStack) {
        for (const auto& clipOrTransform : style.clipsAndTransforms) {
          if (clipOrTransform.IsClip()) {
            mTarget->PopClip();
          }
        }
      }

      if (mTarget->GetBackendType() == gfx::BackendType::CAIRO) {
        // With the cairo backend we pushed an extra clip rect which we have to
        // balance out here. See the comment in
        // RestoreClipsAndTransformToTarget.
        mTarget->PopClip();
      }

      mTarget->SetTransform(Matrix());
    }

    mBufferProvider->ReturnDrawTarget(mTarget.forget());
  }
}

NS_IMETHODIMP
CanvasRenderingContext2D::InitializeWithDrawTarget(
    nsIDocShell* aShell, NotNull<gfx::DrawTarget*> aTarget) {
  if (NS_WARN_IF(!AddShutdownObserver())) {
    return NS_ERROR_FAILURE;
  }

  RemovePostRefreshObserver();
  mDocShell = aShell;
  AddPostRefreshObserverIfNecessary();

  IntSize size = aTarget->GetSize();
  SetDimensions(size.width, size.height);

  mTarget = aTarget;
  mBufferProvider = new PersistentBufferProviderBasic(aTarget);

  RestoreClipsAndTransformToTarget();

  return NS_OK;
}

void CanvasRenderingContext2D::SetOpaqueValueFromOpaqueAttr(
    bool aOpaqueAttrValue) {
  if (aOpaqueAttrValue != mOpaqueAttrValue) {
    mOpaqueAttrValue = aOpaqueAttrValue;
    UpdateIsOpaque();
  }
}

void CanvasRenderingContext2D::UpdateIsOpaque() {
  mOpaque = !mContextAttributesHasAlpha || mOpaqueAttrValue;
  ClearTarget();
}

NS_IMETHODIMP
CanvasRenderingContext2D::SetContextOptions(JSContext* aCx,
                                            JS::Handle<JS::Value> aOptions,
                                            ErrorResult& aRvForDictionaryInit) {
  if (aOptions.isNullOrUndefined()) {
    return NS_OK;
  }

  // This shouldn't be called before drawing starts, so there should be no
  // drawtarget yet
  MOZ_ASSERT(!mTarget);

  CanvasRenderingContext2DSettings attributes;
  if (!attributes.Init(aCx, aOptions)) {
    aRvForDictionaryInit.Throw(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  mWillReadFrequently = attributes.mWillReadFrequently;
  mForceSoftwareRendering = attributes.mForceSoftwareRendering;

  mContextAttributesHasAlpha = attributes.mAlpha;
  UpdateIsOpaque();

  return NS_OK;
}

UniquePtr<uint8_t[]> CanvasRenderingContext2D::GetImageBuffer(
    int32_t* out_format, gfx::IntSize* out_imageSize) {
  UniquePtr<uint8_t[]> ret;

  *out_format = 0;
  *out_imageSize = {};

  if (!GetBufferProvider() && !EnsureTarget()) {
    return nullptr;
  }

  RefPtr<SourceSurface> snapshot = mBufferProvider->BorrowSnapshot();
  if (snapshot) {
    RefPtr<DataSourceSurface> data = snapshot->GetDataSurface();
    if (data && data->GetSize() == GetSize()) {
      *out_format = imgIEncoder::INPUT_FORMAT_HOSTARGB;
      *out_imageSize = data->GetSize();
      ret = SurfaceToPackedBGRA(data);
    }
  }

  mBufferProvider->ReturnSnapshot(snapshot.forget());

  if (ret && ShouldResistFingerprinting(RFPTarget::CanvasRandomization)) {
    nsRFPService::RandomizePixels(
        GetCookieJarSettings(), ret.get(), out_imageSize->width,
        out_imageSize->height, out_imageSize->width * out_imageSize->height * 4,
        SurfaceFormat::A8R8G8B8_UINT32);
  }

  return ret;
}

NS_IMETHODIMP
CanvasRenderingContext2D::GetInputStream(const char* aMimeType,
                                         const nsAString& aEncoderOptions,
                                         nsIInputStream** aStream) {
  nsCString enccid("@mozilla.org/image/encoder;2?type=");
  enccid += aMimeType;
  nsCOMPtr<imgIEncoder> encoder = do_CreateInstance(enccid.get());
  if (!encoder) {
    return NS_ERROR_FAILURE;
  }

  int32_t format = 0;
  gfx::IntSize imageSize = {};
  UniquePtr<uint8_t[]> imageBuffer = GetImageBuffer(&format, &imageSize);
  if (!imageBuffer) {
    return NS_ERROR_FAILURE;
  }

  return ImageEncoder::GetInputStream(imageSize.width, imageSize.height,
                                      imageBuffer.get(), format, encoder,
                                      aEncoderOptions, aStream);
}

already_AddRefed<mozilla::gfx::SourceSurface>
CanvasRenderingContext2D::GetOptimizedSnapshot(DrawTarget* aTarget,
                                               gfxAlphaType* aOutAlphaType) {
  if (aOutAlphaType) {
    *aOutAlphaType = (mOpaque ? gfxAlphaType::Opaque : gfxAlphaType::Premult);
  }

  // For GetSurfaceSnapshot we always call EnsureTarget even if mBufferProvider
  // already exists, otherwise we get performance issues. See bug 1567054.
  if (!EnsureTarget()) {
    MOZ_ASSERT(
        mTarget == sErrorTarget.get() || mIsContextLost,
        "On EnsureTarget failure mTarget should be set to sErrorTarget.");
    // In rare circumstances we may have failed to create an error target.
    return mTarget ? mTarget->Snapshot() : nullptr;
  }

  // The concept of BorrowSnapshot seems a bit broken here, but the original
  // code in GetSurfaceSnapshot just returned a snapshot from mTarget, which
  // amounts to breaking the concept implicitly.
  RefPtr<SourceSurface> snapshot = mBufferProvider->BorrowSnapshot(aTarget);
  RefPtr<SourceSurface> retSurface = snapshot;
  mBufferProvider->ReturnSnapshot(snapshot.forget());
  return retSurface.forget();
}

SurfaceFormat CanvasRenderingContext2D::GetSurfaceFormat() const {
  return mOpaque ? SurfaceFormat::B8G8R8X8 : SurfaceFormat::B8G8R8A8;
}

//
// state
//

Matrix CanvasRenderingContext2D::GetCurrentTransform() const {
  if (IsTargetValid()) {
    return mTarget->GetTransform();
  }
  for (auto style = mStyleStack.crbegin(); style != mStyleStack.crend();
       ++style) {
    const auto& clipsAndTransforms = style->clipsAndTransforms;
    auto clipOrTransform = clipsAndTransforms.end();
    while (clipOrTransform != clipsAndTransforms.begin()) {
      --clipOrTransform;
      if (!clipOrTransform->IsClip()) {
        return clipOrTransform->transform;
      }
    }
  }
  return Matrix();
}

void CanvasRenderingContext2D::Save() {
  if (MOZ_UNLIKELY(HasErrorState() || mStyleStack.IsEmpty())) {
    SetErrorState();
    return;
  }
  mStyleStack[mStyleStack.Length() - 1].transform = GetCurrentTransform();
  mStyleStack.SetCapacity(mStyleStack.Length() + 1);
  mStyleStack.AppendElement(CurrentState());

  if (mStyleStack.Length() > MAX_STYLE_STACK_SIZE) {
    // This is not fast, but is better than OOMing and shouldn't be hit by
    // reasonable code.
    mStyleStack.RemoveElementAt(0);
  }
}

void CanvasRenderingContext2D::Restore() {
  if (MOZ_UNLIKELY(mStyleStack.Length() < 2 || HasErrorState())) {
    return;
  }

  if (IsTargetValid()) {
    for (const auto& clipOrTransform : CurrentState().clipsAndTransforms) {
      if (clipOrTransform.IsClip()) {
        mTarget->PopClip();
      }
    }
    mTarget->SetTransform(PreviousState().transform);
  } else {
    mTargetNeedsClipsAndTransforms = true;
  }

  mStyleStack.RemoveLastElement();

  mPathTransformDirty = true;
}

//
// transformations
//

void CanvasRenderingContext2D::Scale(double aX, double aY,
                                     ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  Matrix newMatrix = GetCurrentTransform();
  newMatrix.PreScale(aX, aY);
  SetTransformInternal(newMatrix);
}

void CanvasRenderingContext2D::Rotate(double aAngle, ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  Matrix newMatrix = Matrix::Rotation(aAngle) * GetCurrentTransform();
  SetTransformInternal(newMatrix);
}

void CanvasRenderingContext2D::Translate(double aX, double aY,
                                         ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  Matrix newMatrix = GetCurrentTransform();
  newMatrix.PreTranslate(aX, aY);
  SetTransformInternal(newMatrix);
}

void CanvasRenderingContext2D::Transform(double aM11, double aM12, double aM21,
                                         double aM22, double aDx, double aDy,
                                         ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  Matrix newMatrix(aM11, aM12, aM21, aM22, aDx, aDy);
  newMatrix *= GetCurrentTransform();
  SetTransformInternal(newMatrix);
}

already_AddRefed<DOMMatrix> CanvasRenderingContext2D::GetTransform(
    ErrorResult& aError) {
  // If we are silently failing, then we still need to return a transform while
  // we are in the process of recovering.
  RefPtr<DOMMatrix> matrix =
      new DOMMatrix(GetParentObject(), GetCurrentTransform());
  return matrix.forget();
}

void CanvasRenderingContext2D::SetTransform(double aM11, double aM12,
                                            double aM21, double aM22,
                                            double aDx, double aDy,
                                            ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  Matrix newMatrix(aM11, aM12, aM21, aM22, aDx, aDy);
  SetTransformInternal(newMatrix);
}

void CanvasRenderingContext2D::SetTransform(const DOMMatrix2DInit& aInit,
                                            ErrorResult& aError) {
  if (HasErrorState(aError)) {
    return;
  }
  RefPtr<DOMMatrixReadOnly> matrix =
      DOMMatrixReadOnly::FromMatrix(GetParentObject(), aInit, aError);
  if (!aError.Failed()) {
    Matrix newMatrix = Matrix(*(matrix->GetInternal2D()));
    SetTransformInternal(newMatrix);
  }
}

void CanvasRenderingContext2D::SetTransformInternal(const Matrix& aTransform) {
  if (!aTransform.IsFinite()) {
    return;
  }

  // Save the transform in the clip stack to be able to replay clips properly.
  auto& clipsAndTransforms = CurrentState().clipsAndTransforms;
  if (clipsAndTransforms.IsEmpty() ||
      clipsAndTransforms.LastElement().IsClip()) {
    clipsAndTransforms.AppendElement(ClipState(aTransform));
  } else {
    // If the last item is a transform we can replace it instead of appending
    // a new item.
    clipsAndTransforms.LastElement().transform = aTransform;
  }

  if (IsTargetValid()) {
    mTarget->SetTransform(aTransform);
  } else {
    mTargetNeedsClipsAndTransforms = true;
  }
  mPathTransformDirty = true;
}

void CanvasRenderingContext2D::ResetTransform(ErrorResult& aError) {
  SetTransform(1.0, 0.0, 0.0, 1.0, 0.0, 0.0, aError);
}

//
// colors
//

void CanvasRenderingContext2D::SetStyleFromUnion(
    const UTF8StringOrCanvasGradientOrCanvasPattern& aValue,
    Style aWhichStyle) {
  if (aValue.IsUTF8String()) {
    SetStyleFromString(aValue.GetAsUTF8String(), aWhichStyle);
    return;
  }

  if (aValue.IsCanvasGradient()) {
    SetStyleFromGradient(aValue.GetAsCanvasGradient(), aWhichStyle);
    return;
  }

  if (aValue.IsCanvasPattern()) {
    CanvasPattern& pattern = aValue.GetAsCanvasPattern();
    SetStyleFromPattern(pattern, aWhichStyle);
    if (pattern.mForceWriteOnly) {
      SetWriteOnly();
    }
    return;
  }

  MOZ_ASSERT_UNREACHABLE("Invalid union value");
}

void CanvasRenderingContext2D::SetFillRule(const nsAString& aString) {
  FillRule rule;

  if (aString.EqualsLiteral("evenodd"))
    rule = FillRule::FILL_EVEN_ODD;
  else if (aString.EqualsLiteral("nonzero"))
    rule = FillRule::FILL_WINDING;
  else
    return;

  CurrentState().fillRule = rule;
}

void CanvasRenderingContext2D::GetFillRule(nsAString& aString) {
  switch (CurrentState().fillRule) {
    case FillRule::FILL_WINDING:
      aString.AssignLiteral("nonzero");
      break;
    case FillRule::FILL_EVEN_ODD:
      aString.AssignLiteral("evenodd");
      break;
  }
}
//
// gradients and patterns
//
already_AddRefed<CanvasGradient> CanvasRenderingContext2D::CreateLinearGradient(
    double aX0, double aY0, double aX1, double aY1) {
  RefPtr<CanvasGradient> grad =
      new CanvasLinearGradient(this, Point(aX0, aY0), Point(aX1, aY1));

  return grad.forget();
}

already_AddRefed<CanvasGradient> CanvasRenderingContext2D::CreateRadialGradient(
    double aX0, double aY0, double aR0, double aX1, double aY1, double aR1,
    ErrorResult& aError) {
  if (aR0 < 0.0 || aR1 < 0.0) {
    aError.ThrowIndexSizeError("Negative radius");
    return nullptr;
  }

  RefPtr<CanvasGradient> grad = new CanvasRadialGradient(
      this, Point(aX0, aY0), aR0, Point(aX1, aY1), aR1);

  return grad.forget();
}

already_AddRefed<CanvasGradient> CanvasRenderingContext2D::CreateConicGradient(
    double aAngle, double aCx, double aCy) {
  double adjustedStartAngle = aAngle + M_PI / 2.0;
  return MakeAndAddRef<CanvasConicGradient>(this, adjustedStartAngle,
                                            Point(aCx, aCy));
}

already_AddRefed<CanvasPattern> CanvasRenderingContext2D::CreatePattern(
    const CanvasImageSource& aSource, const nsAString& aRepeat,
    ErrorResult& aError) {
  CanvasPattern::RepeatMode repeatMode = CanvasPattern::RepeatMode::NOREPEAT;

  if (aRepeat.IsEmpty() || aRepeat.EqualsLiteral("repeat")) {
    repeatMode = CanvasPattern::RepeatMode::REPEAT;
  } else if (aRepeat.EqualsLiteral("repeat-x")) {
    repeatMode = CanvasPattern::RepeatMode::REPEATX;
  } else if (aRepeat.EqualsLiteral("repeat-y")) {
    repeatMode = CanvasPattern::RepeatMode::REPEATY;
  } else if (aRepeat.EqualsLiteral("no-repeat")) {
    repeatMode = CanvasPattern::RepeatMode::NOREPEAT;
  } else {
    aError.ThrowSyntaxError("Invalid pattern keyword");
    return nullptr;
  }

  Element* element = nullptr;
  OffscreenCanvas* offscreenCanvas = nullptr;
  VideoFrame* videoFrame = nullptr;

  if (aSource.IsHTMLCanvasElement()) {
    HTMLCanvasElement* canvas = &aSource.GetAsHTMLCanvasElement();
    element = canvas;

    CSSIntSize size = canvas->GetSize();
    if (size.width == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has width 0");
      return nullptr;
    }

    if (size.height == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has height 0");
      return nullptr;
    }

    // Special case for Canvas, which could be an Azure canvas!
    nsICanvasRenderingContextInternal* srcCanvas = canvas->GetCurrentContext();
    if (srcCanvas) {
      // This might not be an Azure canvas!
      RefPtr<SourceSurface> srcSurf = srcCanvas->GetSurfaceSnapshot();
      if (!srcSurf) {
        aError.ThrowInvalidStateError(
            "CanvasRenderingContext2D.createPattern() failed to snapshot source"
            "canvas.");
        return nullptr;
      }

      RefPtr<CanvasPattern> pat =
          new CanvasPattern(this, srcSurf, repeatMode, element->NodePrincipal(),
                            canvas->IsWriteOnly(), false);

      return pat.forget();
    }
  } else if (aSource.IsHTMLImageElement()) {
    HTMLImageElement* img = &aSource.GetAsHTMLImageElement();
    element = img;
  } else if (aSource.IsSVGImageElement()) {
    SVGImageElement* img = &aSource.GetAsSVGImageElement();
    element = img;
  } else if (aSource.IsHTMLVideoElement()) {
    auto& video = aSource.GetAsHTMLVideoElement();
    video.LogVisibility(
        mozilla::dom::HTMLVideoElement::CallerAPI::CREATE_PATTERN);
    element = &video;
  } else if (aSource.IsOffscreenCanvas()) {
    offscreenCanvas = &aSource.GetAsOffscreenCanvas();

    CSSIntSize size = offscreenCanvas->GetWidthHeight();
    if (size.width == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has width 0");
      return nullptr;
    }

    if (size.height == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has height 0");
      return nullptr;
    }

    nsICanvasRenderingContextInternal* srcCanvas =
        offscreenCanvas->GetContext();
    if (srcCanvas) {
      RefPtr<SourceSurface> srcSurf = srcCanvas->GetSurfaceSnapshot();
      if (!srcSurf) {
        aError.ThrowInvalidStateError(
            "Passed-in canvas failed to create snapshot");
        return nullptr;
      }

      RefPtr<CanvasPattern> pat = new CanvasPattern(
          this, srcSurf, repeatMode, srcCanvas->PrincipalOrNull(),
          offscreenCanvas->IsWriteOnly(), false);

      return pat.forget();
    }
  } else if (aSource.IsVideoFrame()) {
    videoFrame = &aSource.GetAsVideoFrame();

    if (videoFrame->CodedWidth() == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has width 0");
      return nullptr;
    }

    if (videoFrame->CodedHeight() == 0) {
      aError.ThrowInvalidStateError("Passed-in canvas has height 0");
      return nullptr;
    }
  } else {
    // Special case for ImageBitmap
    ImageBitmap& imgBitmap = aSource.GetAsImageBitmap();
    if (!EnsureTarget(aError)) {
      return nullptr;
    }

    MOZ_ASSERT(IsTargetValid());

    RefPtr<SourceSurface> srcSurf = imgBitmap.PrepareForDrawTarget(mTarget);
    if (!srcSurf) {
      aError.ThrowInvalidStateError(
          "Passed-in ImageBitmap has been transferred");
      return nullptr;
    }

    // An ImageBitmap never taints others so we set principalForSecurityCheck to
    // nullptr and set CORSUsed to true for passing the security check in
    // CanvasUtils::DoDrawImageSecurityCheck().
    RefPtr<CanvasPattern> pat = new CanvasPattern(
        this, srcSurf, repeatMode, nullptr, imgBitmap.IsWriteOnly(), true);

    return pat.forget();
  }

  if (!EnsureTarget(aError)) {
    return nullptr;
  }

  MOZ_ASSERT(IsTargetValid());

  // The canvas spec says that createPattern should use the first frame
  // of animated images
  auto flags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE |
               nsLayoutUtils::SFE_EXACT_SIZE_SURFACE;
  SurfaceFromElementResult res;
  if (offscreenCanvas) {
    res = nsLayoutUtils::SurfaceFromOffscreenCanvas(offscreenCanvas, flags,
                                                    mTarget);
  } else if (videoFrame) {
    res = nsLayoutUtils::SurfaceFromVideoFrame(videoFrame, flags, mTarget);
  } else {
    res = nsLayoutUtils::SurfaceFromElement(element, flags, mTarget);
  }

  // Per spec, we should throw here for the HTMLImageElement and SVGImageElement
  // cases if the image request state is "broken".  In terms of the infromation
  // in "res", the "broken" state corresponds to not having a size and not being
  // still-loading (so there is no size forthcoming).
  if (aSource.IsHTMLImageElement() || aSource.IsSVGImageElement()) {
    if (!res.mIsStillLoading && !res.mHasSize) {
      aError.ThrowInvalidStateError(
          "Passed-in image's current request's state is \"broken\"");
      return nullptr;
    }

    if (res.mSize.width == 0 || res.mSize.height == 0) {
      return nullptr;
    }

    // Is the "fully decodable" check already done in SurfaceFromElement?  It's
    // not clear how to do it from here, exactly.
  }

  RefPtr<SourceSurface> surface = res.GetSourceSurface();
  if (!surface) {
    return nullptr;
  }

  RefPtr<CanvasPattern> pat =
      new CanvasPattern(this, surface, repeatMode, res.mPrincipal,
                        res.mIsWriteOnly, res.mCORSUsed);
  return pat.forget();
}

//
// shadows
//
void CanvasRenderingContext2D::SetShadowColor(const nsACString& aShadowColor) {
  Maybe<nscolor> color = ParseColor(aShadowColor);
  if (!color) {
    return;
  }

  CurrentState().shadowColor = *color;
}

//
// filters
//

static already_AddRefed<StyleLockedDeclarationBlock> CreateDeclarationForServo(
    nsCSSPropertyID aProperty, const nsACString& aPropertyValue,
    Document* aDocument) {
  ServoCSSParser::ParsingEnvironment env{aDocument->DefaultStyleAttrURLData(),
                                         aDocument->GetCompatibilityMode(),
                                         aDocument->CSSLoader()};
  RefPtr<StyleLockedDeclarationBlock> servoDeclarations =
      ServoCSSParser::ParseProperty(aProperty, aPropertyValue, env,
                                    StyleParsingMode::DEFAULT);

  if (!servoDeclarations) {
    // We got a syntax error.  The spec says this value must be ignored.
    return nullptr;
  }

  if (aProperty == eCSSProperty_font) {
    Servo_DeclarationBlock_SanitizeForCanvas(servoDeclarations);
  }

  return servoDeclarations.forget();
}

static already_AddRefed<StyleLockedDeclarationBlock>
CreateFontDeclarationForServo(const nsACString& aFont, Document* aDocument) {
  return CreateDeclarationForServo(eCSSProperty_font, aFont, aDocument);
}

static already_AddRefed<const ComputedStyle> GetFontStyleForServo(
    Element* aElement, const nsACString& aFont, PresShell* aPresShell,
    nsACString& aOutUsedFont, ErrorResult& aError) {
  RefPtr<StyleLockedDeclarationBlock> declarations =
      CreateFontDeclarationForServo(aFont, aPresShell->GetDocument());
  if (!declarations) {
    // We got a syntax error.  The spec says this value must be ignored.
    return nullptr;
  }

  // In addition to unparseable values, the spec says we need to reject
  // 'inherit' and 'initial'. The easiest way to check for this is to look
  // at font-size-adjust, which the font shorthand resets to 'none'.
  if (Servo_DeclarationBlock_HasCSSWideKeyword(declarations,
                                               eCSSProperty_font_size_adjust)) {
    return nullptr;
  }

  ServoStyleSet* styleSet = aPresShell->StyleSet();

  // Have to get a parent ComputedStyle for inherit-like relative values (2em,
  // bolder, etc.)
  RefPtr<const ComputedStyle> parentStyle;
  if (aElement) {
    parentStyle = nsComputedDOMStyle::GetComputedStyle(aElement);
    if (NS_WARN_IF(aPresShell->IsDestroying())) {
      // The flush might've killed the shell.
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
  }
  if (!parentStyle) {
    RefPtr<StyleLockedDeclarationBlock> declarations =
        CreateFontDeclarationForServo("10px sans-serif"_ns,
                                      aPresShell->GetDocument());
    MOZ_ASSERT(declarations);

    parentStyle =
        aPresShell->StyleSet()->ResolveForDeclarations(nullptr, declarations);
  }

  MOZ_RELEASE_ASSERT(parentStyle, "Should have a valid parent style");

  MOZ_ASSERT(!aPresShell->IsDestroying(),
             "We should have returned an error above if the presshell is "
             "being destroyed.");

  RefPtr<const ComputedStyle> sc =
      styleSet->ResolveForDeclarations(parentStyle, declarations);

  // https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-font
  // The font-size component must be converted to CSS px for reserialization,
  // so we update the declarations with the value from the computed style.
  if (!sc->StyleFont()->mFont.family.is_system_font) {
    float px = sc->StyleFont()->mFont.size.ToCSSPixels();
    Servo_DeclarationBlock_SetLengthValue(declarations, eCSSProperty_font_size,
                                          px, eCSSUnit_Pixel);
  }

  // The font getter is required to be reserialized based on what we
  // parsed (including having line-height removed).
  // If we failed to reserialize, ignore this attempt to set the value.
  Servo_SerializeFontValueForCanvas(declarations, &aOutUsedFont);
  if (aOutUsedFont.IsEmpty()) {
    return nullptr;
  }

  return sc.forget();
}

static already_AddRefed<StyleLockedDeclarationBlock>
CreateFilterDeclarationForServo(const nsACString& aFilter,
                                Document* aDocument) {
  return CreateDeclarationForServo(eCSSProperty_filter, aFilter, aDocument);
}

static already_AddRefed<const ComputedStyle> ResolveFilterStyleForServo(
    const nsACString& aFilterString, const ComputedStyle* aParentStyle,
    PresShell* aPresShell, ErrorResult& aError) {
  RefPtr<StyleLockedDeclarationBlock> declarations =
      CreateFilterDeclarationForServo(aFilterString, aPresShell->GetDocument());
  if (!declarations) {
    // Refuse to accept the filter, but do not throw an error.
    return nullptr;
  }

  // In addition to unparseable values, the spec says we need to reject
  // 'inherit' and 'initial'.
  if (Servo_DeclarationBlock_HasCSSWideKeyword(declarations,
                                               eCSSProperty_filter)) {
    return nullptr;
  }

  ServoStyleSet* styleSet = aPresShell->StyleSet();
  RefPtr<const ComputedStyle> computedValues =
      styleSet->ResolveForDeclarations(aParentStyle, declarations);

  return computedValues.forget();
}

bool CanvasRenderingContext2D::ParseFilter(
    const nsACString& aString, StyleOwnedSlice<StyleFilter>& aFilterChain,
    ErrorResult& aError) {
  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    nsIGlobalObject* global = GetParentObject();
    FontFaceSet* fontFaceSet = global ? global->GetFonts() : nullptr;
    FontFaceSetImpl* fontFaceSetImpl =
        fontFaceSet ? fontFaceSet->GetImpl() : nullptr;
    RefPtr<URLExtraData> urlExtraData =
        fontFaceSetImpl ? fontFaceSetImpl->GetURLExtraData() : nullptr;

    if (NS_WARN_IF(!urlExtraData)) {
      // Provided we have a FontFaceSetImpl object, this should only happen on
      // worker threads, where we failed to initialize the worker before it was
      // shutdown.
      aError.ThrowInvalidStateError("Missing URLExtraData");
      return false;
    }

    if (NS_WARN_IF(!Servo_ParseFilters(&aString, /* aIgnoreUrls */ true,
                                       urlExtraData, &aFilterChain))) {
      return false;
    }

    return true;
  }

  nsAutoCString usedFont;  // unused

  RefPtr<const ComputedStyle> parentStyle = GetFontStyleForServo(
      mCanvasElement, GetFont(), presShell, usedFont, aError);
  if (!parentStyle) {
    return false;
  }

  RefPtr<const ComputedStyle> style =
      ResolveFilterStyleForServo(aString, parentStyle, presShell, aError);
  if (!style) {
    return false;
  }

  aFilterChain = style->StyleEffects()->mFilters;
  return true;
}

void CanvasRenderingContext2D::SetFilter(const nsACString& aFilter,
                                         ErrorResult& aError) {
  StyleOwnedSlice<StyleFilter> filterChain;
  if (ParseFilter(aFilter, filterChain, aError)) {
    CurrentState().filterString = aFilter;
    CurrentState().filterChain = std::move(filterChain);
    if (mCanvasElement) {
      CurrentState().autoSVGFiltersObserver =
          SVGObserverUtils::ObserveFiltersForCanvasContext(
              this, mCanvasElement, CurrentState().filterChain.AsSpan());
    }
    UpdateFilter(/* aFlushIfNeeded = */ true);
  }
}

static already_AddRefed<const ComputedStyle> ResolveStyleForServo(
    nsCSSPropertyID aProperty, const nsACString& aString,
    const ComputedStyle* aParentStyle, PresShell* aPresShell,
    ErrorResult& aError) {
  RefPtr<StyleLockedDeclarationBlock> declarations =
      CreateDeclarationForServo(aProperty, aString, aPresShell->GetDocument());
  if (!declarations) {
    return nullptr;
  }

  // In addition to unparseable values, reject 'inherit' and 'initial'.
  if (Servo_DeclarationBlock_HasCSSWideKeyword(declarations, aProperty)) {
    return nullptr;
  }

  ServoStyleSet* styleSet = aPresShell->StyleSet();
  return styleSet->ResolveForDeclarations(aParentStyle, declarations);
}

already_AddRefed<const ComputedStyle>
CanvasRenderingContext2D::ResolveStyleForProperty(nsCSSPropertyID aProperty,
                                                  const nsACString& aValue) {
  RefPtr<PresShell> presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return nullptr;
  }

  nsAutoCString usedFont;
  IgnoredErrorResult err;
  RefPtr<const ComputedStyle> parentStyle =
      GetFontStyleForServo(mCanvasElement, GetFont(), presShell, usedFont, err);
  if (!parentStyle) {
    return nullptr;
  }

  return ResolveStyleForServo(aProperty, aValue, parentStyle, presShell, err);
}

void CanvasRenderingContext2D::GetLetterSpacing(nsACString& aLetterSpacing) {
  if (CurrentState().letterSpacingStr.IsEmpty()) {
    aLetterSpacing.AssignLiteral("0px");
  } else {
    aLetterSpacing = CurrentState().letterSpacingStr;
  }
}

void CanvasRenderingContext2D::SetLetterSpacing(
    const nsACString& aLetterSpacing) {
  ParseSpacing(aLetterSpacing, &CurrentState().letterSpacing,
               CurrentState().letterSpacingStr);
}

void CanvasRenderingContext2D::GetWordSpacing(nsACString& aWordSpacing) {
  if (CurrentState().wordSpacingStr.IsEmpty()) {
    aWordSpacing.AssignLiteral("0px");
  } else {
    aWordSpacing = CurrentState().wordSpacingStr;
  }
}

void CanvasRenderingContext2D::SetWordSpacing(const nsACString& aWordSpacing) {
  ParseSpacing(aWordSpacing, &CurrentState().wordSpacing,
               CurrentState().wordSpacingStr);
}

static GeckoFontMetrics GetFontMetricsFromCanvas(void* aContext) {
  auto* ctx = static_cast<CanvasRenderingContext2D*>(aContext);
  auto* fontGroup = ctx->GetCurrentFontStyle();
  if (!fontGroup) {
    // Shouldn't happen, but just in case... return plausible values for a
    // 10px font (canvas default size).
    return {Length::FromPixels(5.0),
            Length::FromPixels(5.0),
            Length::FromPixels(8.0),
            Length::FromPixels(10.0),
            Length::FromPixels(8.0),
            Length::FromPixels(10.0),
            0.0f,
            0.0f};
  }
  auto metrics = fontGroup->GetMetricsForCSSUnits(
      nsFontMetrics::eHorizontal, StyleQueryFontMetricsFlags::NEEDS_CH |
                                      StyleQueryFontMetricsFlags::NEEDS_IC);
  return {Length::FromPixels(metrics.xHeight),
          Length::FromPixels(metrics.zeroWidth),
          Length::FromPixels(metrics.capHeight),
          Length::FromPixels(metrics.ideographicWidth),
          Length::FromPixels(metrics.maxAscent),
          Length::FromPixels(fontGroup->GetStyle()->size),
          0.0f,
          0.0f};
}

void CanvasRenderingContext2D::ParseSpacing(const nsACString& aSpacing,
                                            float* aValue,
                                            nsACString& aNormalized) {
  // Normalize whitespace in the string before trying to parse it, as we want
  // to store it in normalized form, and this allows a simple check against the
  // 'normal' keyword, which is not accepted.
  nsAutoCString normalized(aSpacing);
  normalized.CompressWhitespace(true, true);
  ToLowerCase(normalized);
  if (normalized.EqualsLiteral("normal")) {
    return;
  }
  float value;
  if (!Servo_ParseLengthWithoutStyleContext(&normalized, &value,
                                            GetFontMetricsFromCanvas, this)) {
    if (!GetPresShell()) {
      return;
    }
    // This will parse aSpacing as a <length-percentage>...
    RefPtr<const ComputedStyle> style =
        ResolveStyleForProperty(eCSSProperty_letter_spacing, aSpacing);
    if (!style) {
      return;
    }
    // ...but only <length> is allowed according to the canvas spec.
    if (!style->StyleText()->mLetterSpacing.IsLength()) {
      return;
    }
    value = style->StyleText()->mLetterSpacing.AsLength().ToCSSPixels();
  }
  aNormalized = normalized;
  *aValue = value;
}

class CanvasUserSpaceMetrics final : public UserSpaceMetricsWithSize {
 public:
  CanvasUserSpaceMetrics(const gfx::IntSize& aSize, const nsFont& aFont,
                         const StyleLineHeight& aLineHeight,
                         RefPtr<nsAtom> aFontLanguage,
                         bool aFontExplicitLanguage,
                         const ComputedStyle* aCanvasStyle,
                         nsPresContext* aPresContext)
      : mSize(aSize),
        mFont(aFont),
        mLineHeight(aLineHeight),
        mFontLanguage(std::move(aFontLanguage)),
        mFontExplicitLanguage(aFontExplicitLanguage),
        mCanvasStyle(aCanvasStyle),
        mPresContext(aPresContext) {}

  float GetZoom() const override {
    return mCanvasStyle ? mCanvasStyle->EffectiveZoom().ToFloat() : 1.0f;
  }

  float GetRootZoom() const override {
    return UserSpaceMetrics::GetZoom(
        mPresContext->Document()->GetRootElement());
  }

  float GetEmLength(Type aType) const override {
    switch (aType) {
      case Type::This:
        return mFont.size.ToCSSPixels();
      case Type::Root:
        return SVGContentUtils::GetFontSize(
            mPresContext->Document()->GetRootElement());
      default:
        MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
        return 1.0f;
    }
  }
  gfx::Size GetSize() const override { return Size(mSize); }

  CSSSize GetCSSViewportSize() const override {
    return GetCSSViewportSizeFromContext(mPresContext);
  }

  float GetLineHeight(Type aType) const override {
    // This is used if a filter is added through `url()`, and if the SVG
    // filter being referred to is using line-height units.
    switch (aType) {
      case Type::This: {
        const auto wm = GetWritingModeForType(aType);
        const auto lh = ReflowInput::CalcLineHeightForCanvas(
            mLineHeight, mFont, mFontLanguage, mFontExplicitLanguage,
            mPresContext, wm);
        return nsPresContext::AppUnitsToFloatCSSPixels(lh);
      }
      case Type::Root: {
        return SVGContentUtils::GetLineHeight(
            mPresContext->Document()->GetRootElement());
      }
    }
    MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
    return 1.0f;
  }

 private:
  GeckoFontMetrics GetFontMetricsForType(Type aType) const override {
    switch (aType) {
      case Type::This: {
        if (!mCanvasStyle) {
          return DefaultFontMetrics();
        }
        return Gecko_GetFontMetrics(
            mPresContext, WritingMode(mCanvasStyle).IsVertical(),
            mCanvasStyle->StyleFont(), mCanvasStyle->StyleFont()->mFont.size,
            StyleQueryFontMetricsFlags::USE_USER_FONT_SET |
                StyleQueryFontMetricsFlags::NEEDS_CH |
                StyleQueryFontMetricsFlags::NEEDS_IC);
      }
      case Type::Root:
        return GetFontMetrics(mPresContext->Document()->GetRootElement());
      default:
        MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
        return DefaultFontMetrics();
    }
  }

  WritingMode GetWritingModeForType(Type aType) const override {
    switch (aType) {
      case Type::This:
        return mCanvasStyle ? WritingMode(mCanvasStyle) : WritingMode();
      case Type::Root:
        return GetWritingMode(mPresContext->Document()->GetRootElement());
      default:
        MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
        return WritingMode();
    }
  }

  gfx::IntSize mSize;
  const nsFont& mFont;
  StyleLineHeight mLineHeight;
  RefPtr<nsAtom> mFontLanguage;
  bool mFontExplicitLanguage;
  RefPtr<const ComputedStyle> mCanvasStyle;
  nsPresContext* mPresContext;
};

// The filter might reference an SVG filter that is declared inside this
// document. Flush frames so that we'll have a SVGFilterFrame to work
// with.
static bool FiltersNeedFrameFlush(Span<const StyleFilter> aFilters) {
  for (const auto& filter : aFilters) {
    if (filter.IsUrl()) {
      return true;
    }
  }
  return false;
}

void CanvasRenderingContext2D::UpdateFilter(bool aFlushIfNeeded) {
  const bool writeOnly = IsWriteOnly() ||
                         (mCanvasElement && mCanvasElement->IsWriteOnly()) ||
                         (mOffscreenCanvas && mOffscreenCanvas->IsWriteOnly());

  RefPtr<PresShell> presShell = GetPresShell();
  if (!mOffscreenCanvas && (!presShell || presShell->IsDestroying())) {
    // Ensure we set an empty filter and update the state to
    // reflect the current "taint" status of the canvas
    CurrentState().filter = FilterDescription();
    CurrentState().filterSourceGraphicTainted = writeOnly;
    return;
  }

  // The PresContext is only used with URL filters and we don't allow those to
  // be used on worker threads.
  nsPresContext* presContext = nullptr;
  if (presShell) {
    if (aFlushIfNeeded &&
        FiltersNeedFrameFlush(CurrentState().filterChain.AsSpan())) {
      presShell->FlushPendingNotifications(FlushType::Frames);
    }

    if (MOZ_UNLIKELY(presShell->IsDestroying())) {
      return;
    }

    presContext = presShell->GetPresContext();
  }
  RefPtr<const ComputedStyle> canvasStyle;
  if (mCanvasElement) {
    canvasStyle = nsComputedDOMStyle::GetComputedStyleNoFlush(mCanvasElement);
  }

  MOZ_RELEASE_ASSERT(!mStyleStack.IsEmpty());

  CurrentState().filter = FilterInstance::GetFilterDescription(
      mCanvasElement, CurrentState().filterChain.AsSpan(),
      CurrentState().autoSVGFiltersObserver, writeOnly,
      CanvasUserSpaceMetrics(
          GetSize(), CurrentState().fontFont, CurrentState().fontLineHeight,
          CurrentState().fontLanguage, CurrentState().fontExplicitLanguage,
          canvasStyle, presContext),
      gfxRect(0, 0, mWidth, mHeight), CurrentState().filterAdditionalImages);
  CurrentState().filterSourceGraphicTainted = writeOnly;
}

//
// rects
//

static bool ValidateRect(double& aX, double& aY, double& aWidth,
                         double& aHeight, bool aIsZeroSizeValid) {
  if (!aIsZeroSizeValid && (aWidth == 0.0 || aHeight == 0.0)) {
    return false;
  }

  // bug 1018527
  // The values of canvas API input are in double precision, but Moz2D APIs are
  // using float precision. Bypass canvas API calls when the input is out of
  // float precision to avoid precision problem
  if (!std::isfinite((float)aX) || !std::isfinite((float)aY) ||
      !std::isfinite((float)aWidth) || !std::isfinite((float)aHeight)) {
    return false;
  }

  // bug 1074733
  // The canvas spec does not forbid rects with negative w or h, so given
  // corners (x, y), (x+w, y), (x+w, y+h), and (x, y+h) we must generate
  // the appropriate rect by flipping negative dimensions. This prevents
  // draw targets from receiving "empty" rects later on.
  if (aWidth < 0) {
    aWidth = -aWidth;
    aX -= aWidth;
  }
  if (aHeight < 0) {
    aHeight = -aHeight;
    aY -= aHeight;
  }
  return true;
}

void CanvasRenderingContext2D::ClearRect(double aX, double aY, double aW,
                                         double aH) {
  // Do not allow zeros - it's a no-op at that point per spec.
  if (!ValidateRect(aX, aY, aW, aH, false)) {
    return;
  }

  gfx::Rect clearRect(aX, aY, aW, aH);

  EnsureTarget(&clearRect, true);
  if (!IsTargetValid()) {
    return;
  }

  mTarget->ClearRect(clearRect);

  RedrawUser(gfxRect(aX, aY, aW, aH));
}

void CanvasRenderingContext2D::FillRect(double aX, double aY, double aW,
                                        double aH) {
  mFeatureUsage |= CanvasFeatureUsage::FillRect;

  if (!ValidateRect(aX, aY, aW, aH, true)) {
    return;
  }

  const ContextState* state = &CurrentState();
  if (state->patternStyles[Style::FILL]) {
    auto& style = state->patternStyles[Style::FILL];
    CanvasPattern::RepeatMode repeat = style->mRepeat;
    // In the FillRect case repeat modes are easy to deal with.
    bool limitx = repeat == CanvasPattern::RepeatMode::NOREPEAT ||
                  repeat == CanvasPattern::RepeatMode::REPEATY;
    bool limity = repeat == CanvasPattern::RepeatMode::NOREPEAT ||
                  repeat == CanvasPattern::RepeatMode::REPEATX;
    if ((limitx || limity) && style->mTransform.IsRectilinear()) {
      // For rectilinear transforms, we can just get the transformed pattern
      // bounds and intersect them with the fill rectangle bounds.
      // TODO: If the transform is not rectilinear, then we would need a fully
      // general clip path to represent the X and Y clip planes bounding the
      // pattern. For such cases, it would be more efficient to rely on Skia's
      // Decal tiling mode rather than trying to generate a path. Until then,
      // just punt to relying on the default Clamp mode.
      gfx::Rect patternBounds(style->mSurface->GetRect());
      patternBounds = style->mTransform.TransformBounds(patternBounds);
      if (style->mTransform.HasNonAxisAlignedTransform()) {
        // If there is an rotation (90 or 270 degrees), the X axis of the
        // pattern projects onto the Y axis of the geometry, and vice versa.
        std::swap(limitx, limity);
      }
      // We always need to execute painting for non-over operators, even if
      // we end up with w/h = 0. The default Rect::Intersect can cause both
      // dimensions to become empty if either dimension individually fails
      // to overlap, which is unsuitable. Instead, we need to independently
      // limit the supplied rectangle on each dimension as required.
      if (limitx) {
        double x2 = aX + aW;
        aX = std::max(aX, double(patternBounds.x));
        aW = std::max(std::min(x2, double(patternBounds.XMost())) - aX, 0.0);
      }
      if (limity) {
        double y2 = aY + aH;
        aY = std::max(aY, double(patternBounds.y));
        aH = std::max(std::min(y2, double(patternBounds.YMost())) - aY, 0.0);
      }
    }
  }
  state = nullptr;

  bool isColor;
  bool discardContent = PatternIsOpaque(Style::FILL, &isColor) &&
                        (CurrentState().op == CompositionOp::OP_OVER ||
                         CurrentState().op == CompositionOp::OP_SOURCE);
  const gfx::Rect fillRect(aX, aY, aW, aH);
  EnsureTarget(discardContent ? &fillRect : nullptr, discardContent && isColor);
  if (!IsTargetValid()) {
    return;
  }

  gfx::Rect bounds;
  const bool needBounds = NeedToCalculateBounds();
  if (!IsTargetValid()) {
    return;
  }
  if (needBounds) {
    bounds = mTarget->GetTransform().TransformBounds(fillRect);
  }

  AntialiasMode antialiasMode = CurrentState().imageSmoothingEnabled
                                    ? AntialiasMode::DEFAULT
                                    : AntialiasMode::NONE;

  AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
  CompositionOp op = target.UsedOperation();
  if (!target) {
    return;
  }
  target.FillRect(gfx::Rect(aX, aY, aW, aH),
                  CanvasGeneralPattern().ForStyle(this, Style::FILL, mTarget),
                  DrawOptions(CurrentState().globalAlpha, op, antialiasMode));

  RedrawUser(gfxRect(aX, aY, aW, aH));
}

void CanvasRenderingContext2D::StrokeRect(double aX, double aY, double aW,
                                          double aH) {
  if (!aW && !aH) {
    return;
  }

  if (!ValidateRect(aX, aY, aW, aH, true)) {
    return;
  }

  EnsureTarget();
  if (!IsTargetValid()) {
    return;
  }

  const bool needBounds = NeedToCalculateBounds();
  if (!IsTargetValid()) {
    return;
  }

  gfx::Rect bounds;
  if (needBounds) {
    const ContextState& state = CurrentState();
    bounds = gfx::Rect(aX - state.lineWidth / 2.0f, aY - state.lineWidth / 2.0f,
                       aW + state.lineWidth, aH + state.lineWidth);
    bounds = mTarget->GetTransform().TransformBounds(bounds);
  }

  if (!IsTargetValid()) {
    return;
  }

  if (!aH) {
    CapStyle cap = CapStyle::BUTT;
    if (CurrentState().lineJoin == CanvasLineJoin::Round) {
      cap = CapStyle::ROUND;
    }
    AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
    auto op = target.UsedOperation();
    if (!target) {
      return;
    }

    const ContextState& state = CurrentState();
    target.StrokeLine(
        Point(aX, aY), Point(aX + aW, aY),
        CanvasGeneralPattern().ForStyle(this, Style::STROKE, mTarget),
        StrokeOptions(state.lineWidth, CanvasToGfx(state.lineJoin), cap,
                      state.miterLimit, state.dash.Length(),
                      state.dash.Elements(), state.dashOffset),
        DrawOptions(state.globalAlpha, op));
    return;
  }

  if (!aW) {
    CapStyle cap = CapStyle::BUTT;
    if (CurrentState().lineJoin == CanvasLineJoin::Round) {
      cap = CapStyle::ROUND;
    }
    AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
    auto op = target.UsedOperation();
    if (!target) {
      return;
    }

    const ContextState& state = CurrentState();
    target.StrokeLine(
        Point(aX, aY), Point(aX, aY + aH),
        CanvasGeneralPattern().ForStyle(this, Style::STROKE, mTarget),
        StrokeOptions(state.lineWidth, CanvasToGfx(state.lineJoin), cap,
                      state.miterLimit, state.dash.Length(),
                      state.dash.Elements(), state.dashOffset),
        DrawOptions(state.globalAlpha, op));
    return;
  }

  AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
  auto op = target.UsedOperation();
  if (!target) {
    return;
  }

  const ContextState& state = CurrentState();
  target.StrokeRect(
      gfx::Rect(aX, aY, aW, aH),
      CanvasGeneralPattern().ForStyle(this, Style::STROKE, mTarget),
      StrokeOptions(state.lineWidth, CanvasToGfx(state.lineJoin),
                    CanvasToGfx(state.lineCap), state.miterLimit,
                    state.dash.Length(), state.dash.Elements(),
                    state.dashOffset),
      DrawOptions(state.globalAlpha, op));

  Redraw();
}

//
// path bits
//

void CanvasRenderingContext2D::BeginPath() {
  mPath = nullptr;
  mPathBuilder = nullptr;
  mPathPruned = false;
}

void CanvasRenderingContext2D::FillImpl(const gfx::Path& aPath) {
  MOZ_ASSERT(IsTargetValid());
  if (aPath.IsEmpty()) {
    return;
  }

  const bool needBounds = NeedToCalculateBounds();
  gfx::Rect bounds;
  if (needBounds) {
    bounds = aPath.GetBounds(mTarget->GetTransform());
  }

  AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
  if (!target) {
    return;
  }

  auto op = target.UsedOperation();
  if (!IsTargetValid() || !target) {
    return;
  }
  target.Fill(&aPath,
              CanvasGeneralPattern().ForStyle(this, Style::FILL, mTarget),
              DrawOptions(CurrentState().globalAlpha, op));
  Redraw();
}

void CanvasRenderingContext2D::Fill(const CanvasWindingRule& aWinding) {
  EnsureTargetAndUserSpacePath(aWinding);
  if (!IsTargetValid()) {
    return;
  }

  if (mPath) {
    FillImpl(*mPath);
  }
}

void CanvasRenderingContext2D::Fill(const CanvasPath& aPath,
                                    const CanvasWindingRule& aWinding) {
  EnsureTarget();
  if (!IsTargetValid()) {
    return;
  }

  RefPtr<gfx::Path> gfxpath = aPath.GetPath(aWinding, mTarget);
  if (gfxpath) {
    FillImpl(*gfxpath);
  }
}

void CanvasRenderingContext2D::StrokeImpl(const gfx::Path& aPath) {
  MOZ_ASSERT(IsTargetValid());
  if (aPath.IsEmpty()) {
    return;
  }

  const ContextState* state = &CurrentState();
  StrokeOptions strokeOptions(state->lineWidth, CanvasToGfx(state->lineJoin),
                              CanvasToGfx(state->lineCap), state->miterLimit,
                              state->dash.Length(), state->dash.Elements(),
                              state->dashOffset);
  state = nullptr;

  const bool needBounds = NeedToCalculateBounds();
  if (!IsTargetValid()) {
    return;
  }
  gfx::Rect bounds;
  if (needBounds) {
    bounds = aPath.GetStrokedBounds(strokeOptions, mTarget->GetTransform());
  }

  AdjustedTarget target(this, bounds.IsEmpty() ? nullptr : &bounds, true);
  if (!target) {
    return;
  }

  auto op = target.UsedOperation();
  if (!IsTargetValid() || !target) {
    return;
  }
  target.Stroke(&aPath,
                CanvasGeneralPattern().ForStyle(this, Style::STROKE, mTarget),
                strokeOptions, DrawOptions(CurrentState().globalAlpha, op));
  Redraw();
}

void CanvasRenderingContext2D::Stroke() {
  mFeatureUsage |= CanvasFeatureUsage::Stroke;

  EnsureTargetAndUserSpacePath();
  if (!IsTargetValid()) {
    return;
  }

  if (mPath) {
    StrokeImpl(*mPath);
  }
}

void CanvasRenderingContext2D::Stroke(const CanvasPath& aPath) {
  EnsureTarget();
  if (!IsTargetValid()) {
    return;
  }
  RefPtr<gfx::Path> gfxpath =
      aPath.GetPath(CanvasWindingRule::Nonzero, mTarget);
  if (gfxpath) {
    StrokeImpl(*gfxpath);
  }
}

void CanvasRenderingContext2D::DrawFocusIfNeeded(
    mozilla::dom::Element& aElement, ErrorResult& aRv) {
  EnsureTargetAndUserSpacePath();
  if (!mPath) {
    return;
  }

  if (DrawCustomFocusRing(aElement)) {
    AutoSaveRestore asr(this);

    // set state to conforming focus state
    ContextState* state = &CurrentState();
    state->globalAlpha = 1.0;
    state->shadowBlur = 0;
    state->shadowOffset.x = 0;
    state->shadowOffset.y = 0;
    state->op = mozilla::gfx::CompositionOp::OP_OVER;

    state->lineCap = CanvasLineCap::Butt;
    state->lineJoin = CanvasLineJoin::Miter;
    state->lineWidth = 1;
    state->dash.Clear();

    // color and style of the rings is the same as for image maps
    // set the background focus color
    state->SetColorStyle(Style::STROKE, NS_RGBA(255, 255, 255, 255));
    state = nullptr;

    // draw the focus ring
    Stroke();
    if (!mPath) {
      return;
    }

    // set dashing for foreground
    nsTArray<mozilla::gfx::Float>& dash = CurrentState().dash;
    for (uint32_t i = 0; i < 2; ++i) {
      if (!dash.AppendElement(1, fallible)) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
    }

    // set the foreground focus color
    CurrentState().SetColorStyle(Style::STROKE, NS_RGBA(0, 0, 0, 255));
    // draw the focus ring
    Stroke();
    if (!mPath) {
      return;
    }
  }
}

bool CanvasRenderingContext2D::DrawCustomFocusRing(Element& aElement) {
  if (!aElement.State().HasState(ElementState::FOCUSRING)) {
    return false;
  }

  HTMLCanvasElement* canvas = GetCanvas();
  if (!canvas || !aElement.IsInclusiveDescendantOf(canvas)) {
    return false;
  }

  EnsureTargetAndUserSpacePath();
  return true;
}

void CanvasRenderingContext2D::Clip(const CanvasWindingRule& aWinding) {
  EnsureUserSpacePath(aWinding);

  if (!mPath) {
    return;
  }

  if (IsTargetValid()) {
    mTarget->PushClip(mPath);
  } else {
    mTargetNeedsClipsAndTransforms = true;
  }
  CurrentState().clipsAndTransforms.AppendElement(ClipState(mPath));
}

void CanvasRenderingContext2D::Clip(const CanvasPath& aPath,
                                    const CanvasWindingRule& aWinding) {
  if (!mBufferProvider) {
    EnsureTarget();
    if (!IsTargetValid()) {
      return;
    }
  }

  RefPtr<gfx::Path> gfxpath = aPath.GetPath(aWinding, mPathType);

  if (!gfxpath) {
    return;
  }

  if (IsTargetValid()) {
    mTarget->PushClip(gfxpath);
  } else {
    mTargetNeedsClipsAndTransforms = true;
  }
  CurrentState().clipsAndTransforms.AppendElement(ClipState(gfxpath));
}

void CanvasRenderingContext2D::ArcTo(double aX1, double aY1, double aX2,
                                     double aY2, double aRadius,
                                     ErrorResult& aError) {
  if (aRadius < 0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }

  if (!EnsureWritablePath()) {
    return;
  }

  // Current point in user space!
  Point p0 = mPathBuilder->CurrentPoint();

  Point p1(aX1, aY1);
  Point p2(aX2, aY2);

  if (!p1.IsFinite() || !p2.IsFinite() || !std::isfinite(aRadius)) {
    return;
  }

  // Execute these calculations in double precision to avoid cumulative
  // rounding errors.
  double dir, a2, b2, c2, cosx, sinx, d, anx, any, bnx, bny, x3, y3, x4, y4, cx,
      cy, angle0, angle1;
  bool anticlockwise;

  if (p0 == p1 || p1 == p2 || aRadius == 0) {
    LineTo(p1);
    return;
  }

  // Check for colinearity
  dir = (p2.x.value - p1.x.value) * (p0.y.value - p1.y.value) +
        (p2.y.value - p1.y.value) * (p1.x.value - p0.x.value);
  if (dir == 0) {
    LineTo(p1);
    return;
  }

  // XXX - Math for this code was already available from the non-azure code
  // and would be well tested. Perhaps converting to bezier directly might
  // be more efficient longer run.
  a2 = (p0.x - aX1) * (p0.x - aX1) + (p0.y - aY1) * (p0.y - aY1);
  b2 = (aX1 - aX2) * (aX1 - aX2) + (aY1 - aY2) * (aY1 - aY2);
  c2 = (p0.x - aX2) * (p0.x - aX2) + (p0.y - aY2) * (p0.y - aY2);
  cosx = (a2 + b2 - c2) / (2 * sqrt(a2 * b2));

  sinx = sqrt(1 - cosx * cosx);
  d = aRadius / ((1 - cosx) / sinx);

  anx = (aX1 - p0.x) / sqrt(a2);
  any = (aY1 - p0.y) / sqrt(a2);
  bnx = (aX1 - aX2) / sqrt(b2);
  bny = (aY1 - aY2) / sqrt(b2);
  x3 = aX1 - anx * d;
  y3 = aY1 - any * d;
  x4 = aX1 - bnx * d;
  y4 = aY1 - bny * d;
  anticlockwise = (dir < 0);
  cx = x3 + any * aRadius * (anticlockwise ? 1 : -1);
  cy = y3 - anx * aRadius * (anticlockwise ? 1 : -1);
  angle0 = atan2((y3 - cy), (x3 - cx));
  angle1 = atan2((y4 - cy), (x4 - cx));

  LineTo(x3, y3);

  Arc(cx, cy, aRadius, angle0, angle1, anticlockwise, aError);
}

void CanvasRenderingContext2D::Arc(double aX, double aY, double aR,
                                   double aStartAngle, double aEndAngle,
                                   bool aAnticlockwise, ErrorResult& aError) {
  if (aR < 0.0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }
  if (aStartAngle == aEndAngle) {
    LineTo(aX + aR * cos(aStartAngle), aY + aR * sin(aStartAngle));
    return;
  }

  if (!EnsureWritablePath()) {
    return;
  }

  EnsureActivePath();

  mPathBuilder->Arc(Point(aX, aY), aR, aStartAngle, aEndAngle, aAnticlockwise);
  mPathPruned = false;
}

void CanvasRenderingContext2D::Rect(double aX, double aY, double aW,
                                    double aH) {
  if (!EnsureWritablePath()) {
    return;
  }

  if (!std::isfinite(aX) || !std::isfinite(aY) || !std::isfinite(aW) ||
      !std::isfinite(aH)) {
    return;
  }

  EnsureCapped();
  mPathBuilder->MoveTo(Point(aX, aY));
  if (aW == 0 && aH == 0) {
    return;
  }
  mPathBuilder->LineTo(Point(aX + aW, aY));
  mPathBuilder->LineTo(Point(aX + aW, aY + aH));
  mPathBuilder->LineTo(Point(aX, aY + aH));
  mPathBuilder->Close();
}

// https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-roundrect
static void RoundRectImpl(
    PathBuilder* aPathBuilder, const Maybe<Matrix>& aTransform, double aX,
    double aY, double aW, double aH,
    const UnrestrictedDoubleOrDOMPointInitOrUnrestrictedDoubleOrDOMPointInitSequence&
        aRadii,
    ErrorResult& aError) {
  // Step 1. If any of x, y, w, or h are infinite or NaN, then return.
  if (!std::isfinite(aX) || !std::isfinite(aY) || !std::isfinite(aW) ||
      !std::isfinite(aH)) {
    return;
  }

  nsTArray<OwningUnrestrictedDoubleOrDOMPointInit> radii;
  // Step 2. If radii is an unrestricted double or DOMPointInit, then set radii
  // to « radii ».
  if (aRadii.IsUnrestrictedDouble()) {
    radii.AppendElement()->SetAsUnrestrictedDouble() =
        aRadii.GetAsUnrestrictedDouble();
  } else if (aRadii.IsDOMPointInit()) {
    radii.AppendElement()->SetAsDOMPointInit() = aRadii.GetAsDOMPointInit();
  } else {
    radii = aRadii.GetAsUnrestrictedDoubleOrDOMPointInitSequence();
    // Step 3. If radii is not a list of size one, two, three, or
    // four, then throw a RangeError.
    if (radii.Length() < 1 || radii.Length() > 4) {
      aError.ThrowRangeError("Can have between 1 and 4 radii");
      return;
    }
  }

  // Step 4. Let normalizedRadii be an empty list.
  AutoTArray<Size, 4> normalizedRadii;

  // Step 5. For each radius of radii:
  for (const auto& radius : radii) {
    // Step 5.1. If radius is a DOMPointInit:
    if (radius.IsDOMPointInit()) {
      const DOMPointInit& point = radius.GetAsDOMPointInit();
      // Step 5.1.1. If radius["x"] or radius["y"] is infinite or NaN, then
      // return.
      if (!std::isfinite(point.mX) || !std::isfinite(point.mY)) {
        return;
      }

      // Step 5.1.2. If radius["x"] or radius["y"] is negative, then
      // throw a RangeError.
      if (point.mX < 0 || point.mY < 0) {
        aError.ThrowRangeError("Radius can not be negative");
        return;
      }

      // Step 5.1.3. Otherwise, append radius to
      // normalizedRadii.
      normalizedRadii.AppendElement(
          Size(gfx::Float(point.mX), gfx::Float(point.mY)));
      continue;
    }

    // Step 5.2. If radius is a unrestricted double:
    double r = radius.GetAsUnrestrictedDouble();
    // Step 5.2.1. If radius is infinite or NaN, then return.
    if (!std::isfinite(r)) {
      return;
    }

    // Step 5.2.2. If radius is negative, then throw a RangeError.
    if (r < 0) {
      aError.ThrowRangeError("Radius can not be negative");
      return;
    }

    // Step 5.2.3. Otherwise append «[ "x" → radius, "y" → radius ]» to
    // normalizedRadii.
    normalizedRadii.AppendElement(Size(gfx::Float(r), gfx::Float(r)));
  }

  // Step 6. Let upperLeft, upperRight, lowerRight, and lowerLeft be null.
  Size upperLeft, upperRight, lowerRight, lowerLeft;

  if (normalizedRadii.Length() == 4) {
    // Step 7. If normalizedRadii's size is 4, then set upperLeft to
    // normalizedRadii[0], set upperRight to normalizedRadii[1], set lowerRight
    // to normalizedRadii[2], and set lowerLeft to normalizedRadii[3].
    upperLeft = normalizedRadii[0];
    upperRight = normalizedRadii[1];
    lowerRight = normalizedRadii[2];
    lowerLeft = normalizedRadii[3];
  } else if (normalizedRadii.Length() == 3) {
    // Step 8. If normalizedRadii's size is 3, then set upperLeft to
    // normalizedRadii[0], set upperRight and lowerLeft to normalizedRadii[1],
    // and set lowerRight to normalizedRadii[2].
    upperLeft = normalizedRadii[0];
    upperRight = normalizedRadii[1];
    lowerRight = normalizedRadii[2];
    lowerLeft = normalizedRadii[1];
  } else if (normalizedRadii.Length() == 2) {
    // Step 9. If normalizedRadii's size is 2, then set upperLeft and lowerRight
    // to normalizedRadii[0] and set upperRight and lowerLeft to
    // normalizedRadii[1].
    upperLeft = normalizedRadii[0];
    upperRight = normalizedRadii[1];
    lowerRight = normalizedRadii[0];
    lowerLeft = normalizedRadii[1];
  } else {
    // Step 10. If normalizedRadii's size is 1, then set upperLeft, upperRight,
    // lowerRight, and lowerLeft to normalizedRadii[0].
    MOZ_ASSERT(normalizedRadii.Length() == 1);
    upperLeft = normalizedRadii[0];
    upperRight = normalizedRadii[0];
    lowerRight = normalizedRadii[0];
    lowerLeft = normalizedRadii[0];
  }

  // This is not as specified but copied from Chrome.
  // XXX Maybe if we implemented Step 12 (the path algorithm) per
  // spec this wouldn't be needed?
  Float x(aX), y(aY), w(aW), h(aH);
  bool clockwise = true;
  if (w < 0) {
    // Horizontal flip
    clockwise = false;
    x += w;
    w = -w;
    std::swap(upperLeft, upperRight);
    std::swap(lowerLeft, lowerRight);
  }

  if (h < 0) {
    // Vertical flip
    clockwise = !clockwise;
    y += h;
    h = -h;
    std::swap(upperLeft, lowerLeft);
    std::swap(upperRight, lowerRight);
  }

  // Step 11. Corner curves must not overlap. Scale all radii to prevent this:
  // Step 11.1. Let top be upperLeft["x"] + upperRight["x"].
  Float top = upperLeft.width + upperRight.width;
  // Step 11.2. Let right be upperRight["y"] + lowerRight["y"].
  Float right = upperRight.height + lowerRight.height;
  // Step 11.3. Let bottom be lowerRight["x"] + lowerLeft["x"].
  Float bottom = lowerRight.width + lowerLeft.width;
  // Step 11.4. Let left be upperLeft["y"] + lowerLeft["y"].
  Float left = upperLeft.height + lowerLeft.height;
  // Step 11.5. Let scale be the minimum value of the ratios w / top, h / right,
  // w / bottom, h / left.
  Float scale = std::min({w / top, h / right, w / bottom, h / left});
  // Step 11.6. If scale is less than 1, then set the x and y members of
  // upperLeft, upperRight, lowerLeft, and lowerRight to their current values
  // multiplied by scale.
  if (scale < 1.0f) {
    upperLeft = upperLeft * scale;
    upperRight = upperRight * scale;
    lowerLeft = lowerLeft * scale;
    lowerRight = lowerRight * scale;
  }

  // Step 12. Create a new subpath:
  // Step 13. Mark the subpath as closed.
  // Note: Implemented by AppendRoundedRectToPath, which is shared with CSS
  // borders etc.
  gfx::Rect rect{x, y, w, h};
  RectCornerRadii cornerRadii(upperLeft, upperRight, lowerRight, lowerLeft);
  AppendRoundedRectToPath(aPathBuilder, rect, cornerRadii, clockwise,
                          aTransform);

  // Step 14. Create a new subpath with the point (x, y) as the only point in
  // the subpath.
  // XXX We don't seem to be doing this for ::Rect either?
}

void CanvasRenderingContext2D::RoundRect(
    double aX, double aY, double aW, double aH,
    const UnrestrictedDoubleOrDOMPointInitOrUnrestrictedDoubleOrDOMPointInitSequence&
        aRadii,
    ErrorResult& aError) {
  if (!EnsureWritablePath()) {
    return;
  }

  PathBuilder* builder = mPathBuilder;
  Maybe<Matrix> transform = Nothing();

  EnsureCapped();
  RoundRectImpl(builder, transform, aX, aY, aW, aH, aRadii, aError);
}

void CanvasRenderingContext2D::Ellipse(double aX, double aY, double aRadiusX,
                                       double aRadiusY, double aRotation,
                                       double aStartAngle, double aEndAngle,
                                       bool aAnticlockwise,
                                       ErrorResult& aError) {
  if (aRadiusX < 0.0 || aRadiusY < 0.0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }

  if (!EnsureWritablePath()) {
    return;
  }

  ArcToBezier(this, Point(aX, aY), Size(aRadiusX, aRadiusY), aStartAngle,
              aEndAngle, aAnticlockwise, aRotation);
  mPathPruned = false;
}

void CanvasRenderingContext2D::FlushPathTransform() {
  if (!mPathTransformDirty) {
    return;
  }
  Matrix newTransform = GetCurrentTransform();
  if (mPath || mPathBuilder) {
    Matrix inverse = newTransform;
    if (!inverse.ExactlyEquals(mPathTransform) && inverse.Invert()) {
      TransformCurrentPath(mPathTransform * inverse);
    }
  }
  mPathTransform = newTransform;
  mPathTransformDirty = false;
}

bool CanvasRenderingContext2D::EnsureWritablePath() {
  if (!mBufferProvider) {
    EnsureTarget();
    // NOTE: IsTargetValid() may be false here (mTarget == sErrorTarget) but we
    // go ahead and create a path anyway since callers depend on that.
    if (!mTarget) {
      return false;
    }
  }

  FillRule fillRule = CurrentState().fillRule;

  if (mPathTransformDirty) {
    FlushPathTransform();
  }

  if (mPathBuilder) {
    return true;
  }

  if (!mPath) {
    if (mBufferProvider) {
      mPathBuilder = Factory::CreatePathBuilder(mPathType, fillRule);
    } else {
      mPathBuilder = mTarget->CreatePathBuilder(fillRule);
    }
  } else {
    mPathBuilder = Path::ToBuilder(mPath.forget(), fillRule);
  }
  return true;
}

bool CanvasRenderingContext2D::EnsureBufferProvider() {
  if (mBufferProvider) {
    return true;
  }
  EnsureTarget();
  return IsTargetValid();
}

void CanvasRenderingContext2D::EnsureUserSpacePath(
    const CanvasWindingRule& aWinding) {
  FillRule fillRule = CurrentState().fillRule;
  if (aWinding == CanvasWindingRule::Evenodd) {
    fillRule = FillRule::FILL_EVEN_ODD;
  }

  if (!EnsureBufferProvider()) {
    return;
  }

  if (mPathTransformDirty) {
    FlushPathTransform();
  }

  if (!mPath && !mPathBuilder) {
    mPathBuilder = Factory::CreatePathBuilder(mPathType, fillRule);
  }

  if (mPathBuilder) {
    EnsureCapped();
    mPath = mPathBuilder->Finish();
    mPathBuilder = nullptr;
  }

  if (mPath && mPath->GetFillRule() != fillRule) {
    Path::SetFillRule(mPath, fillRule);
  }

  NS_ASSERTION(mPath, "mPath should exist");
}

void CanvasRenderingContext2D::TransformCurrentPath(const Matrix& aTransform) {
  if (mPathBuilder) {
    mPathBuilder = Path::ToBuilder(mPathBuilder->Finish(), aTransform);
  } else if (mPath) {
    mPathBuilder = Path::ToBuilder(mPath.forget(), aTransform);
  }
}

//
// text
//

void CanvasRenderingContext2D::SetFont(const nsACString& aFont,
                                       ErrorResult& aError) {
  mFeatureUsage |= CanvasFeatureUsage::SetFont;

  SetFontInternal(aFont, aError);
  if (aError.Failed()) {
    return;
  }

  // Setting the font attribute magically resets fontVariantCaps and
  // fontStretch to normal.
  // (spec unclear, cf. https://github.com/whatwg/html/issues/8103)
  SetFontVariantCaps(CanvasFontVariantCaps::Normal);
  SetFontStretch(CanvasFontStretch::Normal);

  // If letterSpacing or wordSpacing is present, recompute to account for
  // changes to font-relative dimensions.
  UpdateSpacing();
}

static float QuantizeFontSize(float aSize) {
  // Based on the Veltkamp-Dekker float-splitting algorithm, see e.g.
  // https://indico.cern.ch/event/313684/contributions/1687773/attachments/600513/826490/FPArith-Part2.pdf
  // A 32-bit float has 24 bits of precision (23 stored, plus an implicit 1 bit
  // at the start of the mantissa).
  constexpr int bitsToDrop = 17;  // leaving 7 bits of precision
  constexpr int scale = 1 << bitsToDrop;
  float d = aSize * (scale + 1);
  float t = d - aSize;
  return d - t;
}

bool CanvasRenderingContext2D::SetFontInternal(const nsACString& aFont,
                                               ErrorResult& aError) {
  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    return SetFontInternalDisconnected(aFont, aError);
  }

  nsPresContext* c = presShell->GetPresContext();
  FontStyleCacheKey key{aFont, c->RestyleManager()->GetRestyleGeneration()};
  auto entry = mFontStyleCache.Lookup(key);
  if (!entry) {
    FontStyleData newData;
    newData.mKey = key;
    newData.mStyle = GetFontStyleForServo(mCanvasElement, aFont, presShell,
                                          newData.mUsedFont, aError);
    entry.Set(newData);
  }

  const auto& data = entry.Data();
  if (!data.mStyle) {
    return false;
  }

  const nsStyleFont* fontStyle = data.mStyle->StyleFont();

  // Purposely ignore the font size that respects the user's minimum
  // font preference (fontStyle->mFont.size) in favor of the computed
  // size (fontStyle->mSize).  See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=698652.
  // FIXME: Nobody initializes mAllowZoom for servo?
  // MOZ_ASSERT(!fontStyle->mAllowZoom,
  //           "expected text zoom to be disabled on this nsStyleFont");
  nsFont resizedFont(fontStyle->mFont);
  // Create a font group working in units of CSS pixels instead of the usual
  // device pixels, to avoid being affected by page zoom. nsFontMetrics will
  // convert nsFont size in app units to device pixels for the font group, so
  // here we first apply to the size the equivalent of a conversion from device
  // pixels to CSS pixels, to adjust for the difference in expectations from
  // other nsFontMetrics clients.
  resizedFont.size =
      fontStyle->mSize.ScaledBy(1.0f / c->CSSToDevPixelScale().scale);

  // Quantize font size to avoid filling caches with thousands of fonts that
  // differ by imperceptibly-tiny size deltas.
  resizedFont.size = StyleCSSPixelLength::FromPixels(
      QuantizeFontSize(resizedFont.size.ToCSSPixels()));

  resizedFont.kerning = CanvasToGfx(CurrentState().fontKerning);

  // fontStretch handling: if fontStretch is not 'normal', apply it;
  // if it is normal, then use whatever the shorthand set.
  // XXX(jfkthame) The interaction between the shorthand and the separate attr
  // here is not clearly spec'd, and we may want to reconsider it (or revise
  // the available values); see https://github.com/whatwg/html/issues/8103.
  switch (CurrentState().fontStretch) {
    case CanvasFontStretch::Normal:
      // Leave whatever the shorthand set.
      break;
    case CanvasFontStretch::Ultra_condensed:
      resizedFont.stretch = StyleFontStretch::ULTRA_CONDENSED;
      break;
    case CanvasFontStretch::Extra_condensed:
      resizedFont.stretch = StyleFontStretch::EXTRA_CONDENSED;
      break;
    case CanvasFontStretch::Condensed:
      resizedFont.stretch = StyleFontStretch::CONDENSED;
      break;
    case CanvasFontStretch::Semi_condensed:
      resizedFont.stretch = StyleFontStretch::SEMI_CONDENSED;
      break;
    case CanvasFontStretch::Semi_expanded:
      resizedFont.stretch = StyleFontStretch::SEMI_EXPANDED;
      break;
    case CanvasFontStretch::Expanded:
      resizedFont.stretch = StyleFontStretch::EXPANDED;
      break;
    case CanvasFontStretch::Extra_expanded:
      resizedFont.stretch = StyleFontStretch::EXTRA_EXPANDED;
      break;
    case CanvasFontStretch::Ultra_expanded:
      resizedFont.stretch = StyleFontStretch::ULTRA_EXPANDED;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unknown stretch value");
      break;
  }

  // fontVariantCaps handling: if fontVariantCaps is not 'normal', apply it;
  // if it is, then use the smallCaps boolean from the shorthand.
  // XXX(jfkthame) The interaction between the shorthand and the separate attr
  // here is not clearly spec'd, and we may want to reconsider it (or revise
  // the available values); see https://github.com/whatwg/html/issues/8103.
  switch (CurrentState().fontVariantCaps) {
    case CanvasFontVariantCaps::Normal:
      // Leave whatever the shorthand set.
      break;
    case CanvasFontVariantCaps::Small_caps:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_SMALLCAPS;
      break;
    case CanvasFontVariantCaps::All_small_caps:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_ALLSMALL;
      break;
    case CanvasFontVariantCaps::Petite_caps:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_PETITECAPS;
      break;
    case CanvasFontVariantCaps::All_petite_caps:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_ALLPETITE;
      break;
    case CanvasFontVariantCaps::Unicase:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_UNICASE;
      break;
    case CanvasFontVariantCaps::Titling_caps:
      resizedFont.variantCaps = NS_FONT_VARIANT_CAPS_TITLING;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unknown caps value");
      break;
  }

  c->Document()->FlushUserFontSet();

  nsFontMetrics::Params params;
  params.language = fontStyle->mLanguage;
  params.explicitLanguage = fontStyle->mExplicitLanguage;
  params.userFontSet = c->GetUserFontSet();
  params.textPerf = c->GetTextPerfMetrics();
#ifdef XP_WIN
  params.allowForceGDIClassic = false;
#endif
  RefPtr<nsFontMetrics> metrics = c->GetMetricsFor(resizedFont, params);

  gfxFontGroup* newFontGroup = metrics->GetThebesFontGroup();
  CurrentState().fontGroup = newFontGroup;
  NS_ASSERTION(CurrentState().fontGroup, "Could not get font group");
  CurrentState().font = data.mUsedFont;
  CurrentState().fontFont = fontStyle->mFont;
  CurrentState().fontFont.size = fontStyle->mSize;
  CurrentState().fontLanguage = fontStyle->mLanguage;
  CurrentState().fontExplicitLanguage = fontStyle->mExplicitLanguage;
  CurrentState().fontLineHeight = data.mStyle->StyleFont()->mLineHeight;

  return true;
}

static nsAutoCString FamilyListToString(
    const StyleFontFamilyList& aFamilyList) {
  return StringJoin(", "_ns, aFamilyList.list.AsSpan(),
                    [](nsACString& dst, const StyleSingleFontFamily& name) {
                      name.AppendToString(dst);
                    });
}

static void SerializeFontForCanvas(const StyleFontFamilyList& aList,
                                   const gfxFontStyle& aStyle,
                                   nsACString& aUsedFont) {
  // Re-serialize the font shorthand as required by the canvas spec.
  aUsedFont.Truncate();

  if (!aStyle.style.IsNormal()) {
    aStyle.style.ToString(aUsedFont);
    aUsedFont.Append(" ");
  }

  // font-weight is serialized as a number
  if (!aStyle.weight.IsNormal()) {
    aUsedFont.AppendFloat(aStyle.weight.ToFloat());
    aUsedFont.Append(" ");
  }

  // font-stretch is serialized using CSS Fonts 3 keywords, not percentages.
  if (!aStyle.stretch.IsNormal() &&
      Servo_FontStretch_SerializeKeyword(&aStyle.stretch, &aUsedFont)) {
    aUsedFont.Append(" ");
  }

  if (aStyle.variantCaps == NS_FONT_VARIANT_CAPS_SMALLCAPS) {
    aUsedFont.Append("small-caps ");
  }

  // Serialize the computed (not specified) size, and the family name(s).
  aUsedFont.AppendFloat(aStyle.size);
  aUsedFont.Append("px ");
  aUsedFont.Append(FamilyListToString(aList));
}

bool CanvasRenderingContext2D::SetFontInternalDisconnected(
    const nsACString& aFont, ErrorResult& aError) {
  FontFaceSet* fontFaceSet = nullptr;
  if (mCanvasElement) {
    fontFaceSet = mCanvasElement->OwnerDoc()->Fonts();
  } else {
    nsIGlobalObject* global = GetParentObject();
    fontFaceSet = global ? global->GetFonts() : nullptr;
  }

  FontFaceSetImpl* fontFaceSetImpl =
      fontFaceSet ? fontFaceSet->GetImpl() : nullptr;
  RefPtr<URLExtraData> urlExtraData =
      fontFaceSetImpl ? fontFaceSetImpl->GetURLExtraData() : nullptr;

  if (NS_WARN_IF(!urlExtraData)) {
    // Provided we have a FontFaceSetImpl object, this should only happen on
    // worker threads, where we failed to initialize the worker before it was
    // shutdown.
    aError.ThrowInvalidStateError("Missing URLExtraData");
    return false;
  }

  if (fontFaceSetImpl) {
    fontFaceSetImpl->FlushUserFontSet();
  }

  // In the OffscreenCanvas case we don't have the context necessary to call
  // GetFontStyleForServo(), as we do in the main-thread canvas context, so
  // instead we borrow ParseFontShorthandForMatching to parse the attribute.
  StyleFontFamilyList list;
  gfxFontStyle fontStyle;
  float size = 0.0f;
  bool smallCaps = false;
  if (!ServoCSSParser::ParseFontShorthandForMatching(
          aFont, urlExtraData, list, fontStyle.style, fontStyle.stretch,
          fontStyle.weight, &size, &smallCaps)) {
    return false;
  }

  fontStyle.size = QuantizeFontSize(size);
#ifdef XP_WIN
  fontStyle.allowForceGDIClassic = false;
#endif

  switch (CurrentState().fontStretch) {
    case CanvasFontStretch::Normal:
      // Leave whatever the shorthand set.
      break;
    case CanvasFontStretch::Ultra_condensed:
      fontStyle.stretch = StyleFontStretch::ULTRA_CONDENSED;
      break;
    case CanvasFontStretch::Extra_condensed:
      fontStyle.stretch = StyleFontStretch::EXTRA_CONDENSED;
      break;
    case CanvasFontStretch::Condensed:
      fontStyle.stretch = StyleFontStretch::CONDENSED;
      break;
    case CanvasFontStretch::Semi_condensed:
      fontStyle.stretch = StyleFontStretch::SEMI_CONDENSED;
      break;
    case CanvasFontStretch::Semi_expanded:
      fontStyle.stretch = StyleFontStretch::SEMI_EXPANDED;
      break;
    case CanvasFontStretch::Expanded:
      fontStyle.stretch = StyleFontStretch::EXPANDED;
      break;
    case CanvasFontStretch::Extra_expanded:
      fontStyle.stretch = StyleFontStretch::EXTRA_EXPANDED;
      break;
    case CanvasFontStretch::Ultra_expanded:
      fontStyle.stretch = StyleFontStretch::ULTRA_EXPANDED;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unknown stretch value");
      break;
  }

  // fontVariantCaps handling: if fontVariantCaps is not 'normal', apply it;
  // if it is, then use the smallCaps boolean from the shorthand.
  // XXX(jfkthame) The interaction between the shorthand and the separate attr
  // here is not clearly spec'd, and we may want to reconsider it (or revise
  // the available values); see https://github.com/whatwg/html/issues/8103.
  switch (CurrentState().fontVariantCaps) {
    case CanvasFontVariantCaps::Normal:
      fontStyle.variantCaps = smallCaps ? NS_FONT_VARIANT_CAPS_SMALLCAPS
                                        : NS_FONT_VARIANT_CAPS_NORMAL;
      break;
    case CanvasFontVariantCaps::Small_caps:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_SMALLCAPS;
      break;
    case CanvasFontVariantCaps::All_small_caps:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_ALLSMALL;
      break;
    case CanvasFontVariantCaps::Petite_caps:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_PETITECAPS;
      break;
    case CanvasFontVariantCaps::All_petite_caps:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_ALLPETITE;
      break;
    case CanvasFontVariantCaps::Unicase:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_UNICASE;
      break;
    case CanvasFontVariantCaps::Titling_caps:
      fontStyle.variantCaps = NS_FONT_VARIANT_CAPS_TITLING;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unknown caps value");
      break;
  }
  // If variantCaps is set, we need to disable a gfxFont fast-path.
  fontStyle.noFallbackVariantFeatures =
      (fontStyle.variantCaps == NS_FONT_VARIANT_CAPS_NORMAL);

  // Set the kerning feature, if required by the fontKerning attribute.
  gfxFontFeature setting{TRUETYPE_TAG('k', 'e', 'r', 'n'), 0};
  switch (CurrentState().fontKerning) {
    case CanvasFontKerning::None:
      setting.mValue = 0;
      fontStyle.featureSettings.AppendElement(setting);
      break;
    case CanvasFontKerning::Normal:
      setting.mValue = 1;
      fontStyle.featureSettings.AppendElement(setting);
      break;
    default:
      // auto case implies use user agent default
      break;
  }

  // If we have a canvas element, get its lang (if known).
  RefPtr<nsAtom> language;
  bool explicitLanguage = false;
  if (mCanvasElement) {
    language = mCanvasElement->FragmentOrElement::GetLang();
    if (language) {
      explicitLanguage = true;
    } else {
      language = mCanvasElement->OwnerDoc()->GetLanguageForStyle();
    }
  } else {
    // Pass the OS default language, to behave similarly to HTML or canvas-
    // element content with no language tag.
    language = nsLanguageAtomService::GetService()->GetLocaleLanguage();
  }

  // TODO: Cache fontGroups in the Worker (use an nsFontCache?)
  gfxFontGroup* fontGroup =
      new gfxFontGroup(nullptr,           // aPresContext
                       list,              // aFontFamilyList
                       &fontStyle,        // aStyle
                       language,          // aLanguage
                       explicitLanguage,  // aExplicitLanguage
                       nullptr,           // aTextPerf
                       fontFaceSetImpl,   // aUserFontSet
                       1.0,               // aDevToCssSize
                       StyleFontVariantEmoji::Normal);
  CurrentState().fontGroup = fontGroup;
  SerializeFontForCanvas(list, fontStyle, CurrentState().font);
  CurrentState().fontFont = nsFont(StyleFontFamily{list, false, false},
                                   StyleCSSPixelLength::FromPixels(size));
  CurrentState().fontFont.variantCaps = fontStyle.variantCaps;
  CurrentState().fontLanguage = nullptr;
  CurrentState().fontExplicitLanguage = false;
  // We don't have any computed style, assume normal height.
  CurrentState().fontLineHeight = StyleLineHeight::Normal();
  return true;
}

void CanvasRenderingContext2D::UpdateSpacing() {
  auto state = CurrentState();
  if (!state.letterSpacingStr.IsEmpty()) {
    SetLetterSpacing(state.letterSpacingStr);
  }
  if (!state.wordSpacingStr.IsEmpty()) {
    SetWordSpacing(state.wordSpacingStr);
  }
}

/*
 * Helper function that replaces the whitespace characters in a string
 * with U+0020 SPACE. The whitespace characters are defined as U+0020 SPACE,
 * U+0009 CHARACTER TABULATION (tab), U+000A LINE FEED (LF), U+000B LINE
 * TABULATION, U+000C FORM FEED (FF), and U+000D CARRIAGE RETURN (CR).
 * We also replace characters with Bidi type Segment Separator or Block
 * Separator.
 * @param str The string whose whitespace characters to replace.
 */
static inline void TextReplaceWhitespaceCharacters(nsAutoString& aStr) {
  aStr.ReplaceChar(u"\x09\x0A\x0B\x0C\x0D\x1C\x1D\x1E\x1F\x85\x2029",
                   char16_t(' '));
}

void CanvasRenderingContext2D::FillText(const nsAString& aText, double aX,
                                        double aY,
                                        const Optional<double>& aMaxWidth,
                                        ErrorResult& aError) {
  // We try to match the most commonly observed strings used by canvas
  // fingerprinting scripts. We do a prefix match, because that means having to
  // match fewer bytes and sometimes the strings is followed by a few random
  // characters.
  // - Cwm fjordbank gly
  //   Used by FingerprintJS
  //   (https://github.com/fingerprintjs/fingerprintjs/blob/4c4b2c8455e701b8341b2b766d1939cf5de4b615/src/sources/canvas.ts#L119)
  //   and others
  // - Hel$&?6%){mZ+#@
  // - <@nv45. F1n63r,Pr1n71n6!
  // Usually there are at most a handful (usually ~1/2) fillText calls by
  // fingerprinters
  if (mFillTextCalls <= 5) {
    if (StringBeginsWith(aText, u"Cwm fjord"_ns) ||
        StringBeginsWith(aText, u"Hel$&?6%"_ns) ||
        StringBeginsWith(aText, u"<@nv45. "_ns)) {
      mFeatureUsage |= CanvasFeatureUsage::KnownFingerprintText;
    }
    mFillTextCalls++;
  }

  DebugOnly<UniquePtr<TextMetrics>> metrics = DrawOrMeasureText(
      aText, aX, aY, aMaxWidth, TextDrawOperation::FILL, aError);
  MOZ_ASSERT(
      !metrics.inspect());  // drawing operation never returns TextMetrics
}

void CanvasRenderingContext2D::StrokeText(const nsAString& aText, double aX,
                                          double aY,
                                          const Optional<double>& aMaxWidth,
                                          ErrorResult& aError) {
  DebugOnly<UniquePtr<TextMetrics>> metrics = DrawOrMeasureText(
      aText, aX, aY, aMaxWidth, TextDrawOperation::STROKE, aError);
  MOZ_ASSERT(
      !metrics.inspect());  // drawing operation never returns TextMetrics
}

UniquePtr<TextMetrics> CanvasRenderingContext2D::MeasureText(
    const nsAString& aRawText, ErrorResult& aError) {
  Optional<double> maxWidth;
  return DrawOrMeasureText(aRawText, 0, 0, maxWidth, TextDrawOperation::MEASURE,
                           aError);
}

/**
 * Used for nsBidiPresUtils::ProcessText
 */
struct MOZ_STACK_CLASS CanvasBidiProcessor final
    : public nsBidiPresUtils::BidiProcessor {
  using Style = CanvasRenderingContext2D::Style;

  explicit CanvasBidiProcessor(mozilla::gfx::PaletteCache& aPaletteCache)
      : mPaletteCache(aPaletteCache) {
    if (StaticPrefs::gfx_missing_fonts_notify()) {
      mMissingFonts = MakeUnique<gfxMissingFontRecorder>();
    }
  }

  ~CanvasBidiProcessor() {
    // notify front-end code if we encountered missing glyphs in any script
    if (mMissingFonts) {
      mMissingFonts->Flush();
    }
  }

  class PropertyProvider : public gfxTextRun::PropertyProvider {
   public:
    explicit PropertyProvider(const CanvasBidiProcessor& aProcessor)
        : mProcessor(aProcessor) {}

    void GetSpacing(gfxTextRun::Range aRange,
                    gfxFont::Spacing* aSpacing) const {
      for (auto i = aRange.start; i < aRange.end; ++i) {
        auto* charGlyphs = mProcessor.mTextRun->GetCharacterGlyphs();
        if (i == mProcessor.mTextRun->GetLength() - 1 ||
            (charGlyphs[i + 1].IsClusterStart() &&
             charGlyphs[i + 1].IsLigatureGroupStart())) {
          // Currently we add all the letterspacing to the right of the glyph,
          // which is similar to Chrome's behavior, though the LTR vs RTL
          // asymmetry seems unfortunate.
          if (mProcessor.mTextRun->IsRightToLeft()) {
            aSpacing->mAfter = 0;
            aSpacing->mBefore = mProcessor.mLetterSpacing;
          } else {
            aSpacing->mBefore = 0;
            aSpacing->mAfter = mProcessor.mLetterSpacing;
          }
        } else {
          aSpacing->mBefore = 0;
          aSpacing->mAfter = 0;
        }
        if (charGlyphs[i].CharIsSpace()) {
          if (mProcessor.mTextRun->IsRightToLeft()) {
            aSpacing->mBefore += mProcessor.mWordSpacing;
          } else {
            aSpacing->mAfter += mProcessor.mWordSpacing;
          }
        }
        aSpacing++;
      }
    }

    mozilla::StyleHyphens GetHyphensOption() const {
      return mozilla::StyleHyphens::None;
    }

    // Methods only used when hyphenation is active, not relevant to canvas2d:
    void GetHyphenationBreaks(gfxTextRun::Range aRange,
                              gfxTextRun::HyphenType* aBreakBefore) const {
      MOZ_ASSERT_UNREACHABLE("no hyphenation in canvas2d text!");
    }
    gfxFloat GetHyphenWidth() const {
      MOZ_ASSERT_UNREACHABLE("no hyphenation in canvas2d text!");
      return 0.0;
    }
    already_AddRefed<DrawTarget> GetDrawTarget() const {
      MOZ_ASSERT_UNREACHABLE("no hyphenation in canvas2d text!");
      return nullptr;
    }
    uint32_t GetAppUnitsPerDevUnit() const {
      MOZ_ASSERT_UNREACHABLE("no hyphenation in canvas2d text!");
      return 60;
    }
    gfx::ShapedTextFlags GetShapedTextFlags() const {
      MOZ_ASSERT_UNREACHABLE("no hyphenation in canvas2d text!");
      return gfx::ShapedTextFlags();
    }

   private:
    const CanvasBidiProcessor& mProcessor;
  };

  using ContextState = CanvasRenderingContext2D::ContextState;

  void SetText(const char16_t* aText, int32_t aLength,
               intl::BidiDirection aDirection) override {
    if (mIgnoreSetText) {
      // We've been told to ignore SetText because the processor is only ever
      // handling a single, fixed string.
      MOZ_ASSERT(mTextRun && mTextRun->GetLength() == uint32_t(aLength));
      return;
    }
    mSetTextCount++;
    auto* pfl = gfxPlatformFontList::PlatformFontList();
    pfl->Lock();
    mFontgrp->UpdateUserFonts();  // ensure user font generation is current
    // adjust flags for current direction run
    gfx::ShapedTextFlags flags = mTextRunFlags;
    if (aDirection == intl::BidiDirection::RTL) {
      flags |= gfx::ShapedTextFlags::TEXT_IS_RTL;
    } else {
      flags &= ~gfx::ShapedTextFlags::TEXT_IS_RTL;
    }
    mTextRun = mFontgrp->MakeTextRun(
        aText, aLength, mDrawTarget, mAppUnitsPerDevPixel, flags,
        nsTextFrameUtils::Flags::DontSkipDrawingForPendingUserFonts,
        mMissingFonts.get());
    pfl->Unlock();
  }

  nscoord GetWidth() override {
    PropertyProvider provider(*this);
    gfxTextRun::Metrics textRunMetrics = mTextRun->MeasureText(
        mDoMeasureBoundingBox ? gfxFont::TIGHT_INK_EXTENTS
                              : gfxFont::LOOSE_INK_EXTENTS,
        mDrawTarget, &provider);

    // this only measures the height; the total width is gotten from the
    // the return value of ProcessText.
    if (mDoMeasureBoundingBox) {
      textRunMetrics.mBoundingBox.Scale(1.0 / mAppUnitsPerDevPixel);
      mBoundingBox = mBoundingBox.Union(textRunMetrics.mBoundingBox);
    }

    return NSToCoordRound(textRunMetrics.mAdvanceWidth);
  }

  already_AddRefed<gfxPattern> GetGradientFor(Style aStyle) {
    RefPtr<gfxPattern> pattern;
    CanvasGradient* gradient = mCtx->CurrentState().gradientStyles[aStyle];
    CanvasGradient::Type type = gradient->GetType();

    switch (type) {
      case CanvasGradient::Type::CONIC: {
        auto conic = static_cast<CanvasConicGradient*>(gradient);
        pattern = new gfxPattern(conic->mCenter.x, conic->mCenter.y,
                                 conic->mAngle, 0, 1);
        break;
      }
      case CanvasGradient::Type::RADIAL: {
        auto radial = static_cast<CanvasRadialGradient*>(gradient);
        pattern = new gfxPattern(radial->mCenter1.x, radial->mCenter1.y,
                                 radial->mRadius1, radial->mCenter2.x,
                                 radial->mCenter2.y, radial->mRadius2);
        break;
      }
      case CanvasGradient::Type::LINEAR: {
        auto linear = static_cast<CanvasLinearGradient*>(gradient);
        pattern = new gfxPattern(linear->mBegin.x, linear->mBegin.y,
                                 linear->mEnd.x, linear->mEnd.y);
        break;
      }
      default:
        MOZ_ASSERT(false, "Should be linear, radial or conic gradient.");
        return nullptr;
    }

    for (auto stop : gradient->mRawStops) {
      pattern->AddColorStop(stop.offset, stop.color);
    }

    return pattern.forget();
  }

  gfx::ExtendMode CvtCanvasRepeatToGfxRepeat(
      CanvasPattern::RepeatMode aRepeatMode) {
    switch (aRepeatMode) {
      case CanvasPattern::RepeatMode::REPEAT:
        return gfx::ExtendMode::REPEAT;
      case CanvasPattern::RepeatMode::REPEATX:
        return gfx::ExtendMode::REPEAT_X;
      case CanvasPattern::RepeatMode::REPEATY:
        return gfx::ExtendMode::REPEAT_Y;
      case CanvasPattern::RepeatMode::NOREPEAT:
        return gfx::ExtendMode::CLAMP;
      default:
        return gfx::ExtendMode::CLAMP;
    }
  }

  already_AddRefed<gfxPattern> GetPatternFor(Style aStyle) {
    const CanvasPattern* pat = mCtx->CurrentState().patternStyles[aStyle];
    RefPtr<gfxPattern> pattern = new gfxPattern(pat->mSurface, pat->mTransform);
    pattern->SetExtend(CvtCanvasRepeatToGfxRepeat(pat->mRepeat));
    return pattern.forget();
  }

  void DrawText(nscoord aXOffset) override {
    gfx::Point point = mPt;
    bool rtl = mTextRun->IsRightToLeft();
    bool verticalRun = mTextRun->IsVertical();
    RefPtr<gfxPattern> pattern;

    float& inlineCoord = verticalRun ? point.y.value : point.x.value;
    inlineCoord += aXOffset;

    PropertyProvider provider(*this);

    // offset is given in terms of left side of string
    if (rtl) {
      // Bug 581092 - don't use rounded pixel width to advance to
      // right-hand end of run, because this will cause different
      // glyph positioning for LTR vs RTL drawing of the same
      // glyph string on OS X and DWrite where textrun widths may
      // involve fractional pixels.
      gfxTextRun::Metrics textRunMetrics = mTextRun->MeasureText(
          mDoMeasureBoundingBox ? gfxFont::TIGHT_INK_EXTENTS
                                : gfxFont::LOOSE_INK_EXTENTS,
          mDrawTarget, &provider);
      inlineCoord += textRunMetrics.mAdvanceWidth;
      // old code was:
      //   point.x += width * mAppUnitsPerDevPixel;
      // TODO: restore this if/when we move to fractional coords
      // throughout the text layout process
    }

    mCtx->EnsureTarget();
    if (!mCtx->IsTargetValid()) {
      return;
    }

    // Defer the tasks to gfxTextRun which will handle color/svg-in-ot fonts
    // appropriately.
    StrokeOptions strokeOpts;
    DrawOptions drawOpts;
    Style style = (mOp == CanvasRenderingContext2D::TextDrawOperation::FILL)
                      ? Style::FILL
                      : Style::STROKE;
    const ContextState& state = mCtx->CurrentState();

    gfx::Rect bounds;
    if (mCtx->NeedToCalculateBounds()) {
      bounds = ToRect(mBoundingBox);
      bounds.MoveBy(mPt / mAppUnitsPerDevPixel);
      if (style == Style::STROKE) {
        bounds.Inflate(state.lineWidth / 2.0);
      }
      bounds = mDrawTarget->GetTransform().TransformBounds(bounds);
    }

    AdjustedTarget target(mCtx, bounds.IsEmpty() ? nullptr : &bounds, false);
    if (!target) {
      return;
    }

    gfxContext thebes(target, /* aPreserveTransform */ true);
    gfxTextRun::DrawParams params(&thebes, mPaletteCache);

    params.allowGDI = false;

    if (state.StyleIsColor(style)) {  // Color
      nscolor fontColor = state.colorStyles[style];
      if (style == Style::FILL) {
        params.context->SetColor(sRGBColor::FromABGR(fontColor));
      } else {
        params.textStrokeColor = fontColor;
      }
    } else {
      if (state.gradientStyles[style]) {  // Gradient
        pattern = GetGradientFor(style);
      } else if (state.patternStyles[style]) {  // Pattern
        pattern = GetPatternFor(style);
      } else {
        MOZ_ASSERT(false, "Should never reach here.");
        return;
      }
      MOZ_ASSERT(pattern, "No valid pattern.");

      if (style == Style::FILL) {
        params.context->SetPattern(pattern);
      } else {
        params.textStrokePattern = pattern;
      }
    }

    drawOpts.mAlpha = state.globalAlpha;
    drawOpts.mCompositionOp = target.UsedOperation();
    if (!mCtx->IsTargetValid()) {
      return;
    }

    params.drawOpts = &drawOpts;
    params.provider = &provider;

    if (style == Style::STROKE) {
      strokeOpts.mLineWidth = state.lineWidth;
      strokeOpts.mLineJoin = CanvasToGfx(state.lineJoin);
      strokeOpts.mLineCap = CanvasToGfx(state.lineCap);
      strokeOpts.mMiterLimit = state.miterLimit;
      strokeOpts.mDashLength = state.dash.Length();
      strokeOpts.mDashPattern =
          (strokeOpts.mDashLength > 0) ? state.dash.Elements() : 0;
      strokeOpts.mDashOffset = state.dashOffset;

      params.drawMode = DrawMode::GLYPH_STROKE;
      params.strokeOpts = &strokeOpts;
    }

    mTextRun->Draw(gfxTextRun::Range(mTextRun.get()), point, params);
  }

  // current text run
  RefPtr<gfxTextRun> mTextRun;

  // pointer to a screen reference context used to measure text and such
  RefPtr<DrawTarget> mDrawTarget;

  // Pointer to the draw target we should fill our text to
  CanvasRenderingContext2D* mCtx = nullptr;

  // position of the left side of the string, alphabetic baseline
  gfx::Point mPt;

  // current font
  gfxFontGroup* mFontgrp = nullptr;

  // palette cache for COLR font rendering
  mozilla::gfx::PaletteCache& mPaletteCache;

  // spacing adjustments to be applied
  gfx::Float mLetterSpacing = 0.0f;
  gfx::Float mWordSpacing = 0.0f;

  // to record any unsupported characters found in the text,
  // and notify front-end if it is interested
  UniquePtr<gfxMissingFontRecorder> mMissingFonts;

  // dev pixel conversion factor
  int32_t mAppUnitsPerDevPixel = 0;

  // operation (fill or stroke)
  CanvasRenderingContext2D::TextDrawOperation mOp =
      CanvasRenderingContext2D::TextDrawOperation::FILL;

  // union of bounding boxes of all runs, needed for shadows
  gfxRect mBoundingBox;

  // flags to use when creating textrun, based on CSS style
  gfx::ShapedTextFlags mTextRunFlags = gfx::ShapedTextFlags();

  // Count of how many times SetText has been called on this processor.
  uint32_t mSetTextCount = 0;

  // true iff the bounding box should be measured
  bool mDoMeasureBoundingBox = false;

  // true if future SetText calls should be ignored
  bool mIgnoreSetText = false;
};

UniquePtr<TextMetrics> CanvasRenderingContext2D::DrawOrMeasureText(
    const nsAString& aText, float aX, float aY,
    const Optional<double>& aMaxWidth, TextDrawOperation aOp,
    ErrorResult& aError) {
  RefPtr<gfxFontGroup> currentFontStyle = GetCurrentFontStyle();
  if (NS_WARN_IF(!currentFontStyle)) {
    aError = NS_ERROR_FAILURE;
    return nullptr;
  }

  RefPtr<PresShell> presShell = GetPresShell();
  RefPtr<Document> document = presShell ? presShell->GetDocument() : nullptr;

  // replace all the whitespace characters with U+0020 SPACE
  nsAutoString textToDraw(aText);
  TextReplaceWhitespaceCharacters(textToDraw);

  // According to spec, the API should return an empty array if maxWidth was
  // provided but is less than or equal to zero or equal to NaN.
  if (aMaxWidth.WasPassed() &&
      (aMaxWidth.Value() <= 0 || std::isnan(aMaxWidth.Value()))) {
    textToDraw.Truncate();
  }

  RefPtr<const ComputedStyle> canvasStyle;
  if (mCanvasElement) {
    canvasStyle = nsComputedDOMStyle::GetComputedStyle(mCanvasElement);
  }

  // Get text direction, either from the property or inherited from context.
  const ContextState& state = CurrentState();
  bool isRTL;
  switch (state.textDirection) {
    case CanvasDirection::Ltr:
      isRTL = false;
      break;
    case CanvasDirection::Rtl:
      isRTL = true;
      break;
    case CanvasDirection::Inherit:
      if (canvasStyle) {
        isRTL =
            canvasStyle->StyleVisibility()->mDirection == StyleDirection::Rtl;
      } else if (document) {
        isRTL = GET_BIDI_OPTION_DIRECTION(document->GetBidiOptions()) ==
                IBMBIDI_TEXTDIRECTION_RTL;
      } else {
        isRTL = false;
      }
      break;
    default:
      MOZ_CRASH("unknown direction!");
  }

  // This is only needed to know if we can know the drawing bounding box easily.
  const bool doCalculateBounds = NeedToCalculateBounds();
  if (presShell && presShell->IsDestroying()) {
    aError = NS_ERROR_FAILURE;
    return nullptr;
  }

  nsPresContext* presContext =
      presShell ? presShell->GetPresContext() : nullptr;

  if (presContext) {
    // ensure user font set is up to date
    presContext->Document()->FlushUserFontSet();
    currentFontStyle->SetUserFontSet(presContext->GetUserFontSet());
  }

  if (currentFontStyle->GetStyle()->size == 0.0F) {
    aError = NS_OK;
    if (aOp == TextDrawOperation::MEASURE) {
      return MakeUnique<TextMetrics>(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0, 0.0);
    }
    return nullptr;
  }

  if (!std::isfinite(aX) || !std::isfinite(aY)) {
    aError = NS_OK;
    // This may not be correct - what should TextMetrics contain in the case of
    // infinite width or height?
    if (aOp == TextDrawOperation::MEASURE) {
      return MakeUnique<TextMetrics>(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0, 0.0);
    }
    return nullptr;
  }

  CanvasBidiProcessor processor(mPaletteCache);

  // If we don't have a ComputedStyle, we can't set up vertical-text flags
  // (for now, at least; perhaps we need new Canvas API to control this).
  processor.mTextRunFlags =
      canvasStyle ? nsLayoutUtils::GetTextRunFlagsForStyle(
                        canvasStyle, presContext, canvasStyle->StyleFont(),
                        canvasStyle->StyleText(), 0)
                  : gfx::ShapedTextFlags();

  switch (state.textRendering) {
    case CanvasTextRendering::Auto:
      if (state.fontFont.size.ToCSSPixels() <
          StaticPrefs::browser_display_auto_quality_min_font_size()) {
        processor.mTextRunFlags |= gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      } else {
        processor.mTextRunFlags &= ~gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      }
      break;
    case CanvasTextRendering::OptimizeSpeed:
      processor.mTextRunFlags |= gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      break;
    case CanvasTextRendering::OptimizeLegibility:
    case CanvasTextRendering::GeometricPrecision:
      processor.mTextRunFlags &= ~gfx::ShapedTextFlags::TEXT_OPTIMIZE_SPEED;
      break;
    default:
      MOZ_CRASH("unknown textRendering!");
  }

  GetAppUnitsValues(&processor.mAppUnitsPerDevPixel, nullptr);
  processor.mPt = gfx::Point(aX, aY);
  processor.mDrawTarget = gfxPlatform::ThreadLocalScreenReferenceDrawTarget();

  // If we don't have a target then we don't have a transform. A target won't
  // be needed in the case where we're measuring the text size. This allows
  // to avoid creating a target if it's only being used to measure text sizes.
  processor.mDrawTarget->SetTransform(GetCurrentTransform());
  processor.mCtx = this;
  processor.mOp = aOp;
  processor.mBoundingBox = gfxRect(0, 0, 0, 0);
  processor.mDoMeasureBoundingBox = doCalculateBounds ||
                                    !mIsEntireFrameInvalid ||
                                    aOp == TextDrawOperation::MEASURE;
  processor.mFontgrp = currentFontStyle;

  if (state.letterSpacing != 0.0 || state.wordSpacing != 0.0) {
    processor.mLetterSpacing =
        state.letterSpacing * processor.mAppUnitsPerDevPixel;
    processor.mWordSpacing = state.wordSpacing * processor.mAppUnitsPerDevPixel;
    processor.mTextRunFlags |= gfx::ShapedTextFlags::TEXT_ENABLE_SPACING;
    if (state.letterSpacing != 0.0) {
      processor.mTextRunFlags |=
          gfx::ShapedTextFlags::TEXT_DISABLE_OPTIONAL_LIGATURES;
    }
  }

  nscoord totalWidthCoord;

  processor.mFontgrp
      ->UpdateUserFonts();  // ensure user font generation is current
  RefPtr<gfxFont> font = processor.mFontgrp->GetFirstValidFont();
  const gfxFont::Metrics& fontMetrics =
      font->GetMetrics(nsFontMetrics::eHorizontal);

  // calls bidi algo twice since it needs the full text width and the
  // bounding boxes before rendering anything
  aError = nsBidiPresUtils::ProcessText(
      textToDraw.get(), textToDraw.Length(),
      isRTL ? intl::BidiEmbeddingLevel::RTL() : intl::BidiEmbeddingLevel::LTR(),
      presContext, processor, nsBidiPresUtils::MODE_MEASURE, nullptr, 0,
      &totalWidthCoord, mBidiEngine);
  if (aError.Failed()) {
    return nullptr;
  }

  // If ProcessText only called SetText once, we're dealing with a single run,
  // and so we don't need to repeat SetText and textRun construction at drawing
  // time below; we can just re-use the existing textRun.
  if (processor.mSetTextCount == 1) {
    processor.mIgnoreSetText = true;
  }

  float totalWidth = float(totalWidthCoord) / processor.mAppUnitsPerDevPixel;

  // offset pt.x based on text align
  gfxFloat anchorX;

  if (state.textAlign == CanvasTextAlign::Center) {
    anchorX = .5;
  } else if (state.textAlign == CanvasTextAlign::Left ||
             (!isRTL && state.textAlign == CanvasTextAlign::Start) ||
             (isRTL && state.textAlign == CanvasTextAlign::End)) {
    anchorX = 0;
  } else {
    anchorX = 1;
  }

  float offsetX = anchorX * totalWidth;
  processor.mPt.x -= offsetX;

  gfx::ShapedTextFlags runOrientation =
      (processor.mTextRunFlags & gfx::ShapedTextFlags::TEXT_ORIENT_MASK);
  nsFontMetrics::FontOrientation fontOrientation =
      (runOrientation == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_MIXED ||
       runOrientation == gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT)
          ? nsFontMetrics::eVertical
          : nsFontMetrics::eHorizontal;

  // offset pt.y (or pt.x, for vertical text) based on text baseline
  gfxFloat baselineAnchor;

  switch (state.textBaseline) {
    case CanvasTextBaseline::Hanging:
      baselineAnchor = font->GetBaselines(fontOrientation).mHanging;
      break;
    case CanvasTextBaseline::Top:
      baselineAnchor = fontMetrics.emAscent;
      break;
    case CanvasTextBaseline::Middle:
      baselineAnchor = (fontMetrics.emAscent - fontMetrics.emDescent) * .5f;
      break;
    case CanvasTextBaseline::Alphabetic:
      baselineAnchor = font->GetBaselines(fontOrientation).mAlphabetic;
      break;
    case CanvasTextBaseline::Ideographic:
      baselineAnchor = font->GetBaselines(fontOrientation).mIdeographic;
      break;
    case CanvasTextBaseline::Bottom:
      baselineAnchor = -fontMetrics.emDescent;
      break;
    default:
      MOZ_CRASH("GFX: unexpected TextBaseline");
  }

  // We can't query the textRun directly, as it may not have been created yet;
  // so instead we check the flags that will be used to initialize it.
  if (runOrientation != gfx::ShapedTextFlags::TEXT_ORIENT_HORIZONTAL) {
    if (fontOrientation == nsFontMetrics::eVertical) {
      // Adjust to account for mTextRun being shaped using center baseline
      // rather than alphabetic.
      baselineAnchor -= (fontMetrics.emAscent - fontMetrics.emDescent) * .5f;
    }
    processor.mPt.x -= baselineAnchor;
  } else {
    processor.mPt.y += baselineAnchor;
  }

  // if only measuring, don't need to do any more work
  if (aOp == TextDrawOperation::MEASURE) {
    aError = NS_OK;
    // Note that actualBoundingBoxLeft measures the distance in the leftward
    // direction, so its sign is reversed from our usual physical coordinates.
    double actualBoundingBoxLeft = offsetX - processor.mBoundingBox.X();
    double actualBoundingBoxRight = processor.mBoundingBox.XMost() - offsetX;
    double actualBoundingBoxAscent =
        -processor.mBoundingBox.Y() - baselineAnchor;
    double actualBoundingBoxDescent =
        processor.mBoundingBox.YMost() + baselineAnchor;
    auto baselines = font->GetBaselines(fontOrientation);
    return MakeUnique<TextMetrics>(
        totalWidth, actualBoundingBoxLeft, actualBoundingBoxRight,
        fontMetrics.maxAscent - baselineAnchor,   // fontBBAscent
        fontMetrics.maxDescent + baselineAnchor,  // fontBBDescent
        actualBoundingBoxAscent, actualBoundingBoxDescent,
        fontMetrics.emAscent - baselineAnchor,   // emHeightAscent
        fontMetrics.emDescent + baselineAnchor,  // emHeightDescent
        baselines.mHanging - baselineAnchor,
        baselines.mAlphabetic - baselineAnchor,
        baselines.mIdeographic - baselineAnchor);
  }

  // If we did not actually calculate bounds, set up a simple bounding box
  // based on the text position and advance.
  if (!doCalculateBounds) {
    processor.mBoundingBox.width = totalWidth;
    processor.mBoundingBox.MoveBy(gfxPoint(processor.mPt.x, processor.mPt.y));
  }

  processor.mPt.x *= processor.mAppUnitsPerDevPixel;
  processor.mPt.y *= processor.mAppUnitsPerDevPixel;

  if (!EnsureTarget(aError)) {
    return nullptr;
  }

  MOZ_ASSERT(IsTargetValid());

  Matrix oldTransform = mTarget->GetTransform();
  bool restoreTransform = false;
  // if text is over aMaxWidth, then scale the text horizontally such that its
  // width is precisely aMaxWidth
  if (aMaxWidth.WasPassed() && aMaxWidth.Value() > 0 &&
      totalWidth > aMaxWidth.Value()) {
    Matrix newTransform = oldTransform;

    // Translate so that the anchor point is at 0,0, then scale and then
    // translate back.
    newTransform.PreTranslate(aX, 0);
    newTransform.PreScale(aMaxWidth.Value() / totalWidth, 1);
    newTransform.PreTranslate(-aX, 0);
    /* we do this to avoid an ICE in the android compiler */
    Matrix androidCompilerBug = newTransform;
    mTarget->SetTransform(androidCompilerBug);
    restoreTransform = true;
  }

  // save the previous bounding box
  gfxRect boundingBox = processor.mBoundingBox;

  // don't ever need to measure the bounding box twice
  processor.mDoMeasureBoundingBox = false;

  aError = nsBidiPresUtils::ProcessText(
      textToDraw.get(), textToDraw.Length(),
      isRTL ? intl::BidiEmbeddingLevel::RTL() : intl::BidiEmbeddingLevel::LTR(),
      presContext, processor, nsBidiPresUtils::MODE_DRAW, nullptr, 0, nullptr,
      mBidiEngine);

  if (aError.Failed()) {
    return nullptr;
  }

  if (restoreTransform) {
    mTarget->SetTransform(oldTransform);
  }

  if (aOp == CanvasRenderingContext2D::TextDrawOperation::FILL &&
      !doCalculateBounds) {
    RedrawUser(boundingBox);
  } else {
    Redraw();
  }

  aError = NS_OK;
  return nullptr;
}

gfxFontGroup* CanvasRenderingContext2D::GetCurrentFontStyle() {
  // Use lazy (re)initialization for the fontGroup since it's rather expensive.

  RefPtr<PresShell> presShell = GetPresShell();
  nsPresContext* presContext =
      presShell ? presShell->GetPresContext() : nullptr;

  // If we have a cached fontGroup, check that it is valid for the current
  // prescontext; if not, we need to discard and re-create it.
  RefPtr<gfxFontGroup>& fontGroup = CurrentState().fontGroup;
  if (fontGroup) {
    if (fontGroup->GetPresContext() != presContext) {
      fontGroup = nullptr;
    }
  }

  if (!fontGroup) {
    ErrorResult err;
    constexpr auto kDefaultFontStyle = "10px sans-serif"_ns;
    const float kDefaultFontSize = 10.0;
    // If the font has already been set, we're re-creating the fontGroup
    // and should re-use the existing font attribute; if not, we initialize
    // it to the canvas default.
    const nsCString& currentFont = CurrentState().font;
    bool fontUpdated = SetFontInternal(
        currentFont.IsEmpty() ? kDefaultFontStyle : currentFont, err);
    if (err.Failed() || !fontUpdated) {
      err.SuppressException();
      // XXX Should we get a default lang from the prescontext or something?
      nsAtom* language = nsGkAtoms::x_western;
      bool explicitLanguage = false;
      gfxFontStyle style;
      style.size = kDefaultFontSize;
      int32_t perDevPixel, perCSSPixel;
      GetAppUnitsValues(&perDevPixel, &perCSSPixel);
      gfxFloat devToCssSize = gfxFloat(perDevPixel) / gfxFloat(perCSSPixel);
      const auto* sans =
          Servo_FontFamily_Generic(StyleGenericFontFamily::SansSerif);
      fontGroup = new gfxFontGroup(
          presContext, sans->families, &style, language, explicitLanguage,
          presContext ? presContext->GetTextPerfMetrics() : nullptr, nullptr,
          devToCssSize, StyleFontVariantEmoji::Normal);
      if (fontGroup) {
        CurrentState().font = kDefaultFontStyle;
      } else {
        NS_ERROR("Default canvas font is invalid");
      }
    }
  }

  return fontGroup;
}

//
// line dash styles
//

void CanvasRenderingContext2D::SetLineDash(const Sequence<double>& aSegments,
                                           ErrorResult& aRv) {
  nsTArray<mozilla::gfx::Float> dash;

  for (uint32_t x = 0; x < aSegments.Length(); x++) {
    if (aSegments[x] < 0.0) {
      // Pattern elements must be finite "numbers" >= 0, with "finite"
      // taken care of by WebIDL
      return;
    }

    if (!dash.AppendElement(aSegments[x], fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }
  if (aSegments.Length() %
      2) {  // If the number of elements is odd, concatenate again
    for (uint32_t x = 0; x < aSegments.Length(); x++) {
      if (!dash.AppendElement(aSegments[x], fallible)) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
    }
  }

  CurrentState().dash = std::move(dash);
}

void CanvasRenderingContext2D::GetLineDash(nsTArray<double>& aSegments) const {
  const nsTArray<mozilla::gfx::Float>& dash = CurrentState().dash;
  aSegments.Clear();

  for (uint32_t x = 0; x < dash.Length(); x++) {
    aSegments.AppendElement(dash[x]);
  }
}

void CanvasRenderingContext2D::SetLineDashOffset(double aOffset) {
  CurrentState().dashOffset = aOffset;
}

double CanvasRenderingContext2D::LineDashOffset() const {
  return CurrentState().dashOffset;
}

bool CanvasRenderingContext2D::IsPointInPath(JSContext* aCx, double aX,
                                             double aY,
                                             const CanvasWindingRule& aWinding,
                                             nsIPrincipal& aSubjectPrincipal) {
  if (!FloatValidate(aX, aY)) {
    return false;
  }

  // Check for site-specific permission and return false if no permission.
  if (mCanvasElement) {
    nsCOMPtr<Document> ownerDoc = mCanvasElement->OwnerDoc();
    if (!CanvasUtils::IsImageExtractionAllowed(ownerDoc, aCx,
                                               aSubjectPrincipal)) {
      return false;
    }
  } else if (mOffscreenCanvas && mOffscreenCanvas->ShouldResistFingerprinting(
                                     RFPTarget::CanvasImageExtractionPrompt)) {
    return false;
  }

  EnsureUserSpacePath(aWinding);
  if (!mPath) {
    return false;
  }

  return mPath->ContainsPoint(Point(aX, aY), GetCurrentTransform());
}

bool CanvasRenderingContext2D::IsPointInPath(JSContext* aCx,
                                             const CanvasPath& aPath, double aX,
                                             double aY,
                                             const CanvasWindingRule& aWinding,
                                             nsIPrincipal& aSubjectPrincipal) {
  if (!FloatValidate(aX, aY)) {
    return false;
  }

  if (!EnsureBufferProvider()) {
    return false;
  }

  RefPtr<gfx::Path> tempPath = aPath.GetPath(aWinding, mPathType);

  return tempPath->ContainsPoint(Point(aX, aY), GetCurrentTransform());
}

bool CanvasRenderingContext2D::IsPointInStroke(
    JSContext* aCx, double aX, double aY, nsIPrincipal& aSubjectPrincipal) {
  if (!FloatValidate(aX, aY)) {
    return false;
  }

  // Check for site-specific permission and return false if no permission.
  if (mCanvasElement) {
    nsCOMPtr<Document> ownerDoc = mCanvasElement->OwnerDoc();
    if (!CanvasUtils::IsImageExtractionAllowed(ownerDoc, aCx,
                                               aSubjectPrincipal)) {
      return false;
    }
  } else if (mOffscreenCanvas && mOffscreenCanvas->ShouldResistFingerprinting(
                                     RFPTarget::CanvasImageExtractionPrompt)) {
    return false;
  }

  EnsureUserSpacePath();
  if (!mPath) {
    return false;
  }

  const ContextState& state = CurrentState();

  StrokeOptions strokeOptions(state.lineWidth, CanvasToGfx(state.lineJoin),
                              CanvasToGfx(state.lineCap), state.miterLimit,
                              state.dash.Length(), state.dash.Elements(),
                              state.dashOffset);

  return mPath->StrokeContainsPoint(strokeOptions, Point(aX, aY),
                                    GetCurrentTransform());
}

bool CanvasRenderingContext2D::IsPointInStroke(
    JSContext* aCx, const CanvasPath& aPath, double aX, double aY,
    nsIPrincipal& aSubjectPrincipal) {
  if (!FloatValidate(aX, aY)) {
    return false;
  }

  if (!EnsureBufferProvider()) {
    return false;
  }

  RefPtr<gfx::Path> tempPath =
      aPath.GetPath(CanvasWindingRule::Nonzero, mPathType);

  const ContextState& state = CurrentState();

  StrokeOptions strokeOptions(state.lineWidth, CanvasToGfx(state.lineJoin),
                              CanvasToGfx(state.lineCap), state.miterLimit,
                              state.dash.Length(), state.dash.Elements(),
                              state.dashOffset);

  return tempPath->StrokeContainsPoint(strokeOptions, Point(aX, aY),
                                       GetCurrentTransform());
}

// Returns a surface that contains only the part needed to draw aSourceRect.
// On entry, aSourceRect is relative to aSurface, and on return aSourceRect is
// relative to the returned surface.
static already_AddRefed<SourceSurface> ExtractSubrect(SourceSurface* aSurface,
                                                      gfx::Rect* aSourceRect,
                                                      DrawTarget* aTargetDT) {
  gfx::Rect roundedOutSourceRect = *aSourceRect;
  roundedOutSourceRect.RoundOut();
  gfx::IntRect roundedOutSourceRectInt;
  if (!roundedOutSourceRect.ToIntRect(&roundedOutSourceRectInt)) {
    RefPtr<SourceSurface> surface(aSurface);
    return surface.forget();
  }

  // Try to extract an optimized sub-surface.
  if (RefPtr<SourceSurface> surface =
          aSurface->ExtractSubrect(roundedOutSourceRectInt)) {
    *aSourceRect -= roundedOutSourceRect.TopLeft();
    return surface.forget();
  }

  RefPtr<DrawTarget> subrectDT = aTargetDT->CreateSimilarDrawTarget(
      roundedOutSourceRectInt.Size(), SurfaceFormat::B8G8R8A8);

  if (subrectDT) {
    // See bug 1524554.
    subrectDT->ClearRect(gfx::Rect());
  }

  if (!subrectDT || !subrectDT->IsValid()) {
    RefPtr<SourceSurface> surface(aSurface);
    return surface.forget();
  }

  *aSourceRect -= roundedOutSourceRect.TopLeft();

  subrectDT->CopySurface(aSurface, roundedOutSourceRectInt, IntPoint());
  return subrectDT->Snapshot();
}

//
// image
//

static void ClipImageDimension(double& aSourceCoord, double& aSourceSize,
                               double& aClipOriginCoord, double& aClipSize,
                               double& aDestCoord, double& aDestSize) {
  double scale = aDestSize / aSourceSize;
  double relativeCoord = aSourceCoord - aClipOriginCoord;
  if (relativeCoord < 0.0) {
    double destEnd = aDestCoord + aDestSize;
    aDestCoord -= relativeCoord * scale;
    aDestSize = destEnd - aDestCoord;
    aSourceSize += relativeCoord;
    aSourceCoord = aClipOriginCoord;
    relativeCoord = 0.0;
  }
  double delta = aClipSize - (relativeCoord + aSourceSize);
  if (delta < 0.0) {
    aDestSize += delta * scale;
    aSourceSize = aClipSize - relativeCoord;
  }
}

// Acts like nsLayoutUtils::SurfaceFromElement, but it'll attempt
// to pull a SourceSurface from our cache. This allows us to avoid
// reoptimizing surfaces if content and canvas backends are different.
SurfaceFromElementResult CanvasRenderingContext2D::CachedSurfaceFromElement(
    Element* aElement) {
  SurfaceFromElementResult res;
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(aElement);
  if (!imageLoader) {
    return res;
  }

  nsCOMPtr<imgIRequest> imgRequest;
  imageLoader->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                          getter_AddRefs(imgRequest));
  if (!imgRequest) {
    return res;
  }

  uint32_t status = 0;
  if (NS_FAILED(imgRequest->GetImageStatus(&status)) ||
      !(status & imgIRequest::STATUS_LOAD_COMPLETE)) {
    return res;
  }

  nsCOMPtr<nsIPrincipal> principal;
  if (NS_FAILED(imgRequest->GetImagePrincipal(getter_AddRefs(principal))) ||
      !principal) {
    return res;
  }

  if (NS_FAILED(imgRequest->GetHadCrossOriginRedirects(
          &res.mHadCrossOriginRedirects))) {
    return res;
  }

  res.mSourceSurface = CanvasImageCache::LookupAllCanvas(aElement, mTarget);
  if (!res.mSourceSurface) {
    return res;
  }

  res.mCORSUsed = nsLayoutUtils::ImageRequestUsesCORS(imgRequest);
  res.mSize = res.mIntrinsicSize = res.mSourceSurface->GetSize();
  res.mPrincipal = std::move(principal);
  res.mImageRequest = std::move(imgRequest);
  res.mIsWriteOnly = CheckWriteOnlySecurity(res.mCORSUsed, res.mPrincipal,
                                            res.mHadCrossOriginRedirects);

  return res;
}

static void SwapScaleWidthHeightForRotation(gfx::Rect& aRect,
                                            VideoRotation aDegrees) {
  if (aDegrees == VideoRotation::kDegree_90 ||
      aDegrees == VideoRotation::kDegree_270) {
    std::swap(aRect.width, aRect.height);
  }
}

static Matrix ComputeRotationMatrix(gfxFloat aRotatedWidth,
                                    gfxFloat aRotatedHeight,
                                    VideoRotation aDegrees) {
  Point shiftVideoCenterToOrigin(-aRotatedWidth / 2.0, -aRotatedHeight / 2.0);
  if (aDegrees == VideoRotation::kDegree_90 ||
      aDegrees == VideoRotation::kDegree_270) {
    std::swap(shiftVideoCenterToOrigin.x, shiftVideoCenterToOrigin.y);
  }
  auto angle = static_cast<double>(aDegrees) / 180.0 * M_PI;
  Matrix rotation = Matrix::Rotation(static_cast<gfx::Float>(angle));
  Point shiftLeftTopToOrigin(aRotatedWidth / 2.0, aRotatedHeight / 2.0);
  return rotation.PreTranslate(shiftVideoCenterToOrigin)
      .PostTranslate(shiftLeftTopToOrigin);
}

static Maybe<layers::SurfaceDescriptor>
MaybeGetSurfaceDescriptorForRemoteCanvas(
    const SurfaceFromElementResult& aResult) {
  if (!StaticPrefs::gfx_canvas_remote_use_draw_image_fast_path()) {
    return Nothing();
  }

  if (!aResult.mLayersImage) {
    return Nothing();
  }

  Maybe<layers::SurfaceDescriptor> sd;
  sd = aResult.mLayersImage->GetDesc();
  if (sd.isNothing() ||
      sd.ref().type() !=
          layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo) {
    return Nothing();
  }

  auto& sdv = sd.ref().get_SurfaceDescriptorGPUVideo();
  const auto& sdvType = sdv.type();
  if (sdvType ==
      layers::SurfaceDescriptorGPUVideo::TSurfaceDescriptorRemoteDecoder) {
    auto& sdrd = sdv.get_SurfaceDescriptorRemoteDecoder();
    auto& subdesc = sdrd.subdesc();
    const auto& subdescType = subdesc.type();
    if (subdescType == layers::RemoteDecoderVideoSubDescriptor::Tnull_t) {
      return sd;
    }
    if (subdescType == layers::RemoteDecoderVideoSubDescriptor::
                           TSurfaceDescriptorMacIOSurface) {
      return sd;
    }
    if (subdescType ==
            layers::RemoteDecoderVideoSubDescriptor::TSurfaceDescriptorD3D10 &&
        StaticPrefs::gfx_canvas_remote_use_draw_image_fast_path_d3d()) {
      auto& descD3D10 = subdesc.get_SurfaceDescriptorD3D10();
      // Clear FileHandleWrapper, since FileHandleWrapper::mHandle could not be
      // cross process delivered by using Shmem. Cross-process delivery of
      // FileHandleWrapper::mHandle is not possible simply by using shmen. When
      // it is tried, parent side process just causes crash during destroying
      // FileHandleWrapper.
      descD3D10.handle() = nullptr;
      return sd;
    }
  }

  return Nothing();
}

// drawImage(in HTMLImageElement image, in float dx, in float dy);
//   -- render image from 0,0 at dx,dy top-left coords
// drawImage(in HTMLImageElement image, in float dx, in float dy, in float dw,
//           in float dh);
//   -- render image from 0,0 at dx,dy top-left coords clipping it to dw,dh
// drawImage(in HTMLImageElement image, in float sx, in float sy, in float sw,
//           in float sh, in float dx, in float dy, in float dw, in float dh);
//   -- render the region defined by (sx,sy,sw,wh) in image-local space into the
//      region (dx,dy,dw,dh) on the canvas

// If only dx and dy are passed in then optional_argc should be 0. If only
// dx, dy, dw and dh are passed in then optional_argc should be 2. The only
// other valid value for optional_argc is 6 if sx, sy, sw, sh, dx, dy, dw and dh
// are all passed in.

void CanvasRenderingContext2D::DrawImage(const CanvasImageSource& aImage,
                                         double aSx, double aSy, double aSw,
                                         double aSh, double aDx, double aDy,
                                         double aDw, double aDh,
                                         uint8_t aOptional_argc,
                                         ErrorResult& aError) {
  MOZ_ASSERT(aOptional_argc == 0 || aOptional_argc == 2 || aOptional_argc == 6);

  if (!ValidateRect(aDx, aDy, aDw, aDh, true)) {
    return;
  }
  if (aOptional_argc == 6) {
    if (!ValidateRect(aSx, aSy, aSw, aSh, true)) {
      return;
    }
  }

  RefPtr<SourceSurface> srcSurf;
  gfx::IntSize imgSize;
  gfx::IntSize intrinsicImgSize;
  Maybe<IntRect> cropRect;
  Element* element = nullptr;
  OffscreenCanvas* offscreenCanvas = nullptr;
  VideoFrame* videoFrame = nullptr;

  if (!EnsureTarget(aError)) {
    return;
  }

  MOZ_ASSERT(IsTargetValid());

  if (aImage.IsHTMLCanvasElement()) {
    HTMLCanvasElement* canvas = &aImage.GetAsHTMLCanvasElement();
    element = canvas;
    CSSIntSize size = canvas->GetSize();
    if (size.width == 0 || size.height == 0) {
      return aError.ThrowInvalidStateError("Passed-in canvas is empty");
    }

    if (canvas->IsWriteOnly()) {
      SetWriteOnly();
    }
  } else if (aImage.IsOffscreenCanvas()) {
    offscreenCanvas = &aImage.GetAsOffscreenCanvas();
    CSSIntSize size = offscreenCanvas->GetWidthHeight();
    if (size.IsEmpty()) {
      return aError.ThrowInvalidStateError("Passed-in canvas is empty");
    }

    srcSurf = offscreenCanvas->GetSurfaceSnapshot();
    if (srcSurf) {
      imgSize = intrinsicImgSize = srcSurf->GetSize();
    }

    if (offscreenCanvas->IsWriteOnly()) {
      SetWriteOnly();
    }
  } else if (aImage.IsImageBitmap()) {
    ImageBitmap& imageBitmap = aImage.GetAsImageBitmap();
    srcSurf = imageBitmap.PrepareForDrawTarget(mTarget);

    if (!srcSurf) {
      if (imageBitmap.IsClosed()) {
        aError.ThrowInvalidStateError("Passed-in ImageBitmap is closed");
      }
      return;
    }

    if (imageBitmap.IsWriteOnly()) {
      SetWriteOnly();
    }

    imgSize = intrinsicImgSize =
        gfx::IntSize(imageBitmap.Width(), imageBitmap.Height());
  } else if (aImage.IsVideoFrame()) {
    videoFrame = &aImage.GetAsVideoFrame();
  } else {
    if (aImage.IsHTMLImageElement()) {
      HTMLImageElement* img = &aImage.GetAsHTMLImageElement();
      element = img;
    } else if (aImage.IsSVGImageElement()) {
      SVGImageElement* img = &aImage.GetAsSVGImageElement();
      element = img;
    } else {
      HTMLVideoElement* video = &aImage.GetAsHTMLVideoElement();
      video->LogVisibility(
          mozilla::dom::HTMLVideoElement::CallerAPI::DRAW_IMAGE);
      element = video;
    }

    srcSurf = CanvasImageCache::LookupCanvas(element, this, mTarget, &imgSize,
                                             &intrinsicImgSize, &cropRect);
  }

  DirectDrawInfo drawInfo;
  Maybe<layers::SurfaceDescriptor> surfaceDescriptor;
  SurfaceFromElementResult res;

  if (!srcSurf) {
    // The canvas spec says that drawImage should draw the first frame
    // of animated images. We also don't want to rasterize vector images.
    uint32_t sfeFlags = nsLayoutUtils::SFE_WANT_FIRST_FRAME_IF_IMAGE |
                        nsLayoutUtils::SFE_NO_RASTERIZING_VECTORS |
                        nsLayoutUtils::SFE_ALLOW_UNCROPPED_UNSCALED;

    if (offscreenCanvas) {
      res = nsLayoutUtils::SurfaceFromOffscreenCanvas(offscreenCanvas, sfeFlags,
                                                      mTarget);
    } else if (videoFrame) {
      res = nsLayoutUtils::SurfaceFromVideoFrame(videoFrame, sfeFlags, mTarget);
    } else {
      res = CanvasRenderingContext2D::CachedSurfaceFromElement(element);
      if (!res.mSourceSurface) {
        HTMLVideoElement* video = HTMLVideoElement::FromNodeOrNull(element);
        if (video && mBufferProvider->IsAccelerated() &&
            mTarget->IsRecording() &&
            !(!NeedToApplyFilter() && NeedToDrawShadow())) {
          res = nsLayoutUtils::SurfaceFromElement(
              video, sfeFlags, mTarget, /* aOptimizeSourceSurface */ false);
          surfaceDescriptor = MaybeGetSurfaceDescriptorForRemoteCanvas(res);
          if (surfaceDescriptor.isNothing() && res.mLayersImage) {
            if ((res.mSourceSurface = res.mLayersImage->GetAsSourceSurface())) {
              RefPtr<SourceSurface> opt =
                  mTarget->OptimizeSourceSurface(res.mSourceSurface);
              if (opt) {
                res.mSourceSurface = opt;
              }
            }
          }
        } else {
          res = nsLayoutUtils::SurfaceFromElement(element, sfeFlags, mTarget);
        }
      }
    }

    if (surfaceDescriptor.isNothing()) {
      srcSurf = res.GetSourceSurface();
    }

    if (!srcSurf && surfaceDescriptor.isNothing() &&
        !res.mDrawInfo.mImgContainer) {
      // https://html.spec.whatwg.org/#check-the-usability-of-the-image-argument:
      //
      // Only throw if the request is broken and the element is an
      // HTMLImageElement / SVGImageElement. Note that even for those the spec
      // says to silently do nothing in the following cases:
      //   - The element is still loading.
      //   - The image is bad, but it's not in the broken state (i.e., we could
      //     decode the headers and get the size).
      if (!res.mIsStillLoading && !res.mHasSize &&
          (aImage.IsHTMLImageElement() || aImage.IsSVGImageElement())) {
        aError.ThrowInvalidStateError("Passed-in image is \"broken\"");
      } else if (videoFrame) {
        aError.ThrowInvalidStateError("Passed-in video frame is \"broken\"");
      }
      return;
    }

    imgSize = res.mSize;
    intrinsicImgSize = res.mIntrinsicSize;
    cropRect = res.mCropRect;
    DoSecurityCheck(res.mPrincipal, res.mIsWriteOnly, res.mCORSUsed);

    if (srcSurf) {
      if (res.mImageRequest) {
        CanvasImageCache::NotifyDrawImage(element, this, mTarget, srcSurf,
                                          imgSize, intrinsicImgSize, cropRect);
      }
    } else {
      drawInfo = res.mDrawInfo;
    }
  }

  double clipOriginX, clipOriginY, clipWidth, clipHeight;
  if (cropRect) {
    clipOriginX = cropRect.ref().X();
    clipOriginY = cropRect.ref().Y();
    clipWidth = cropRect.ref().Width();
    clipHeight = cropRect.ref().Height();
  } else {
    clipOriginX = clipOriginY = 0.0;
    clipWidth = imgSize.width;
    clipHeight = imgSize.height;
  }

  // Any provided coordinates are in the display space, or the same as the
  // intrinsic size. In order to get to the surface coordinate space, we may
  // need to adjust for scaling and/or cropping. If no source coordinates are
  // provided, then we can just directly use the actual surface size.
  if (aOptional_argc == 0) {
    aSx = clipOriginX;
    aSy = clipOriginY;
    aSw = clipWidth;
    aSh = clipHeight;
    aDw = (double)intrinsicImgSize.width;
    aDh = (double)intrinsicImgSize.height;
  } else if (aOptional_argc == 2) {
    aSx = clipOriginX;
    aSy = clipOriginY;
    aSw = clipWidth;
    aSh = clipHeight;
  } else if (cropRect || intrinsicImgSize != imgSize) {
    // We need to first scale between the cropped size and the intrinsic size,
    // and then adjust for the offset from the crop rect.
    double scaleXToCrop = clipWidth / intrinsicImgSize.width;
    double scaleYToCrop = clipHeight / intrinsicImgSize.height;
    aSx = aSx * scaleXToCrop + clipOriginX;
    aSy = aSy * scaleYToCrop + clipOriginY;
    aSw = aSw * scaleXToCrop;
    aSh = aSh * scaleYToCrop;
  }

  if (aSw == 0.0 || aSh == 0.0) {
    return;
  }

  ClipImageDimension(aSx, aSw, clipOriginX, clipWidth, aDx, aDw);
  ClipImageDimension(aSy, aSh, clipOriginY, clipHeight, aDy, aDh);

  if (aSw <= 0.0 || aSh <= 0.0 || aDw <= 0.0 || aDh <= 0.0) {
    // source and/or destination are fully clipped, so nothing is painted
    return;
  }

  // Per spec, the smoothing setting applies only to scaling up a bitmap image.
  // When down-scaling the user agent is free to choose whether or not to smooth
  // the image. Nearest sampling when down-scaling is rarely desirable and
  // smoothing when down-scaling matches chromium's behavior.
  // If any dimension is up-scaled, we consider the image as being up-scaled.
  auto scale = mTarget->GetTransform().ScaleFactors();
  bool isDownScale =
      aDw * Abs(scale.xScale) < aSw && aDh * Abs(scale.yScale) < aSh;

  SamplingFilter samplingFilter;
  AntialiasMode antialiasMode;

  if (CurrentState().imageSmoothingEnabled || isDownScale) {
    samplingFilter = gfx::SamplingFilter::LINEAR;
    antialiasMode = AntialiasMode::DEFAULT;
  } else {
    samplingFilter = gfx::SamplingFilter::POINT;
    antialiasMode = AntialiasMode::NONE;
  }

  const bool needBounds = NeedToCalculateBounds();
  if (!IsTargetValid()) {
    return;
  }
  gfx::Rect bounds;
  if (needBounds) {
    bounds = gfx::Rect(aDx, aDy, aDw, aDh);
    bounds = mTarget->GetTransform().TransformBounds(bounds);
  }

  if (!IsTargetValid()) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (srcSurf || surfaceDescriptor.isSome()) {
    gfx::Rect sourceRect(aSx, aSy, aSw, aSh);
    if (srcSurf && ((element && element == mCanvasElement) ||
                    (offscreenCanvas && offscreenCanvas == mOffscreenCanvas))) {
      // srcSurf is a snapshot of mTarget. If we draw to mTarget now, we'll
      // trigger a COW copy of the whole canvas into srcSurf. That's a huge
      // waste if sourceRect doesn't cover the whole canvas.
      // We avoid copying the whole canvas by manually copying just the part
      // that we need.
      srcSurf = ExtractSubrect(srcSurf, &sourceRect, mTarget);
      // The SFE result may inadvertently keep the snapshot alive, forcing a
      // copy when MarkChanged is called. Clear out possibly the last reference
      // to the original snapshot to avoid this.
      res.mSourceSurface = nullptr;
    }

    AdjustedTarget tempTarget(this, bounds.IsEmpty() ? nullptr : &bounds, true);
    if (!tempTarget) {
      gfxWarning() << "Invalid adjusted target in Canvas2D "
                   << gfx::hexa((DrawTarget*)mTarget) << ", "
                   << NeedToDrawShadow() << NeedToApplyFilter();
      return;
    }

    auto op = tempTarget.UsedOperation();
    if (!IsTargetValid() || !tempTarget) {
      return;
    }

    VideoRotation rotationDeg = VideoRotation::kDegree_0;
    if (HTMLVideoElement* video = HTMLVideoElement::FromNodeOrNull(element)) {
      rotationDeg = video->RotationDegrees();
    }

    gfx::Rect destRect(aDx, aDy, aDw, aDh);

    Matrix currentTransform = tempTarget->GetTransform();
    if (rotationDeg != VideoRotation::kDegree_0) {
      tempTarget->ConcatTransform(
          ComputeRotationMatrix(aDw, aDh, rotationDeg).PostTranslate(aDx, aDy));

      SwapScaleWidthHeightForRotation(destRect, rotationDeg);
      // When rotation exists, aDx, aDy is handled by transform, Since aDest.x
      // aDest.y handling of DrawSurface() does not care about the rotation.
      destRect.x = 0;
      destRect.y = 0;
    }

    if (srcSurf) {
      MOZ_ASSERT(surfaceDescriptor.isNothing());

      tempTarget.DrawSurface(
          srcSurf, destRect, sourceRect,
          DrawSurfaceOptions(samplingFilter, SamplingBounds::UNBOUNDED),
          DrawOptions(CurrentState().globalAlpha, op, antialiasMode));
    } else if (surfaceDescriptor.isSome()) {
      MOZ_ASSERT(!tempTarget.UseOptimizeShadow());
      MOZ_ASSERT(res.mLayersImage);

      mTarget->DrawSurfaceDescriptor(
          surfaceDescriptor.ref(), res.mLayersImage, destRect, sourceRect,
          DrawSurfaceOptions(samplingFilter, SamplingBounds::UNBOUNDED),
          DrawOptions(CurrentState().globalAlpha, op, antialiasMode));
    } else {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }

    if (rotationDeg != VideoRotation::kDegree_0) {
      tempTarget->SetTransform(currentTransform);
    }

  } else {
    DrawDirectlyToCanvas(drawInfo, &bounds, gfx::Rect(aDx, aDy, aDw, aDh),
                         gfx::Rect(aSx, aSy, aSw, aSh), imgSize);
  }

  RedrawUser(gfxRect(aDx, aDy, aDw, aDh));
}

void CanvasRenderingContext2D::DrawDirectlyToCanvas(
    const DirectDrawInfo& aImage, gfx::Rect* aBounds, gfx::Rect aDest,
    gfx::Rect aSrc, gfx::IntSize aImgSize) {
  MOZ_ASSERT(aSrc.width > 0 && aSrc.height > 0,
             "Need positive source width and height");

  AdjustedTarget tempTarget(this, aBounds->IsEmpty() ? nullptr : aBounds);
  if (!tempTarget || !tempTarget->IsValid()) {
    return;
  }

  // Get any existing transforms on the context, including transformations used
  // for context shadow.
  Matrix matrix = tempTarget->GetTransform();
  gfxMatrix contextMatrix = ThebesMatrix(matrix);
  MatrixScalesDouble contextScale = contextMatrix.ScaleFactors();

  // Scale the dest rect to include the context scale.
  aDest.Scale((float)contextScale.xScale, (float)contextScale.yScale);

  // Scale the image size to the dest rect, and adjust the source rect to match.
  MatrixScalesDouble scale(aDest.width / aSrc.width,
                           aDest.height / aSrc.height);
  IntSize scaledImageSize =
      IntSize::Ceil(static_cast<float>(scale.xScale * aImgSize.width),
                    static_cast<float>(scale.yScale * aImgSize.height));
  aSrc.Scale(static_cast<float>(scale.xScale),
             static_cast<float>(scale.yScale));

  // We're wrapping tempTarget's (our) DrawTarget here, so we need to restore
  // the matrix even though this is a temp gfxContext.
  AutoRestoreTransform autoRestoreTransform(mTarget);

  gfxContext context(tempTarget);
  context.SetMatrixDouble(
      contextMatrix
          .PreScale(1.0 / contextScale.xScale, 1.0 / contextScale.yScale)
          .PreTranslate(aDest.x - aSrc.x, aDest.y - aSrc.y));

  context.SetOp(tempTarget.UsedOperation());

  // FLAG_CLAMP is added for increased performance, since we never tile here.
  uint32_t modifiedFlags = aImage.mDrawingFlags | imgIContainer::FLAG_CLAMP;

  // XXX hmm is scaledImageSize really in CSS pixels?
  CSSIntSize sz(scaledImageSize.width, scaledImageSize.height);
  SVGImageContext svgContext(Some(sz));

  if (mContextProperties != CanvasContextProperties::None &&
      aImage.mImgContainer->GetType() == imgIContainer::TYPE_VECTOR) {
    SVGEmbeddingContextPaint* contextPaint =
        svgContext.GetOrCreateContextPaint();
    const ContextState& state = CurrentState();

    if (mContextProperties != CanvasContextProperties::Fill &&
        state.StyleIsColor(Style::STROKE)) {
      contextPaint->SetStroke(state.colorStyles[Style::STROKE]);
    }

    if (mContextProperties != CanvasContextProperties::Stroke &&
        state.StyleIsColor(Style::FILL)) {
      contextPaint->SetFill(state.colorStyles[Style::FILL]);
    }
  }

  auto result = aImage.mImgContainer->Draw(
      &context, scaledImageSize,
      ImageRegion::Create(gfxRect(aSrc.x, aSrc.y, aSrc.width, aSrc.height)),
      aImage.mWhichFrame, SamplingFilter::GOOD, svgContext, modifiedFlags,
      CurrentState().globalAlpha);

  if (result != ImgDrawResult::SUCCESS) {
    NS_WARNING("imgIContainer::Draw failed");
  }
}

void CanvasRenderingContext2D::SetGlobalCompositeOperation(
    const nsAString& aOp, ErrorResult& aError) {
  CompositionOp comp_op;

#define CANVAS_OP_TO_GFX_OP(cvsop, op2d) \
  if (aOp.EqualsLiteral(cvsop)) comp_op = CompositionOp::OP_##op2d;

  CANVAS_OP_TO_GFX_OP("clear", CLEAR)
  else CANVAS_OP_TO_GFX_OP("copy", SOURCE)
  else CANVAS_OP_TO_GFX_OP("source-atop", ATOP)
  else CANVAS_OP_TO_GFX_OP("source-in", IN)
  else CANVAS_OP_TO_GFX_OP("source-out", OUT)
  else CANVAS_OP_TO_GFX_OP("source-over", OVER)
  else CANVAS_OP_TO_GFX_OP("destination-in", DEST_IN)
  else CANVAS_OP_TO_GFX_OP("destination-out", DEST_OUT)
  else CANVAS_OP_TO_GFX_OP("destination-over", DEST_OVER)
  else CANVAS_OP_TO_GFX_OP("destination-atop", DEST_ATOP)
  else CANVAS_OP_TO_GFX_OP("lighter", ADD)
  else CANVAS_OP_TO_GFX_OP("xor", XOR)
  else CANVAS_OP_TO_GFX_OP("multiply", MULTIPLY)
  else CANVAS_OP_TO_GFX_OP("screen", SCREEN)
  else CANVAS_OP_TO_GFX_OP("overlay", OVERLAY)
  else CANVAS_OP_TO_GFX_OP("darken", DARKEN)
  else CANVAS_OP_TO_GFX_OP("lighten", LIGHTEN)
  else CANVAS_OP_TO_GFX_OP("color-dodge", COLOR_DODGE)
  else CANVAS_OP_TO_GFX_OP("color-burn", COLOR_BURN)
  else CANVAS_OP_TO_GFX_OP("hard-light", HARD_LIGHT)
  else CANVAS_OP_TO_GFX_OP("soft-light", SOFT_LIGHT)
  else CANVAS_OP_TO_GFX_OP("difference", DIFFERENCE)
  else CANVAS_OP_TO_GFX_OP("exclusion", EXCLUSION)
  else CANVAS_OP_TO_GFX_OP("hue", HUE)
  else CANVAS_OP_TO_GFX_OP("saturation", SATURATION)
  else CANVAS_OP_TO_GFX_OP("color", COLOR)
  else CANVAS_OP_TO_GFX_OP("luminosity", LUMINOSITY)
  // XXX ERRMSG we need to report an error to developers here! (bug 329026)
  else return;

#undef CANVAS_OP_TO_GFX_OP
  CurrentState().op = comp_op;
}

void CanvasRenderingContext2D::GetGlobalCompositeOperation(
    nsAString& aOp, ErrorResult& aError) {
  CompositionOp comp_op = CurrentState().op;

#define CANVAS_OP_TO_GFX_OP(cvsop, op2d) \
  if (comp_op == CompositionOp::OP_##op2d) aOp.AssignLiteral(cvsop);

  CANVAS_OP_TO_GFX_OP("clear", CLEAR)
  else CANVAS_OP_TO_GFX_OP("copy", SOURCE)
  else CANVAS_OP_TO_GFX_OP("destination-atop", DEST_ATOP)
  else CANVAS_OP_TO_GFX_OP("destination-in", DEST_IN)
  else CANVAS_OP_TO_GFX_OP("destination-out", DEST_OUT)
  else CANVAS_OP_TO_GFX_OP("destination-over", DEST_OVER)
  else CANVAS_OP_TO_GFX_OP("lighter", ADD)
  else CANVAS_OP_TO_GFX_OP("source-atop", ATOP)
  else CANVAS_OP_TO_GFX_OP("source-in", IN)
  else CANVAS_OP_TO_GFX_OP("source-out", OUT)
  else CANVAS_OP_TO_GFX_OP("source-over", OVER)
  else CANVAS_OP_TO_GFX_OP("xor", XOR)
  else CANVAS_OP_TO_GFX_OP("multiply", MULTIPLY)
  else CANVAS_OP_TO_GFX_OP("screen", SCREEN)
  else CANVAS_OP_TO_GFX_OP("overlay", OVERLAY)
  else CANVAS_OP_TO_GFX_OP("darken", DARKEN)
  else CANVAS_OP_TO_GFX_OP("lighten", LIGHTEN)
  else CANVAS_OP_TO_GFX_OP("color-dodge", COLOR_DODGE)
  else CANVAS_OP_TO_GFX_OP("color-burn", COLOR_BURN)
  else CANVAS_OP_TO_GFX_OP("hard-light", HARD_LIGHT)
  else CANVAS_OP_TO_GFX_OP("soft-light", SOFT_LIGHT)
  else CANVAS_OP_TO_GFX_OP("difference", DIFFERENCE)
  else CANVAS_OP_TO_GFX_OP("exclusion", EXCLUSION)
  else CANVAS_OP_TO_GFX_OP("hue", HUE)
  else CANVAS_OP_TO_GFX_OP("saturation", SATURATION)
  else CANVAS_OP_TO_GFX_OP("color", COLOR)
  else CANVAS_OP_TO_GFX_OP("luminosity", LUMINOSITY)
  else {
    aError.Throw(NS_ERROR_FAILURE);
  }

#undef CANVAS_OP_TO_GFX_OP
}

void CanvasRenderingContext2D::DrawWindow(nsGlobalWindowInner& aWindow,
                                          double aX, double aY, double aW,
                                          double aH, const nsACString& aBgColor,
                                          uint32_t aFlags,
                                          nsIPrincipal& aSubjectPrincipal,
                                          ErrorResult& aError) {
  if (int32_t(aW) == 0 || int32_t(aH) == 0) {
    return;
  }

  // protect against too-large surfaces that will cause allocation
  // or overflow issues
  if (!Factory::CheckSurfaceSize(IntSize(int32_t(aW), int32_t(aH)), 0xffff)) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  Document* doc = aWindow.GetExtantDoc();
  if (doc && aSubjectPrincipal.GetIsAddonOrExpandedAddonPrincipal()) {
    doc->WarnOnceAbout(
        DeprecatedOperations::eDrawWindowCanvasRenderingContext2D);
  }

  // Flush layout updates
  if (!(aFlags & CanvasRenderingContext2D_Binding::DRAWWINDOW_DO_NOT_FLUSH)) {
    nsContentUtils::FlushLayoutForTree(aWindow.GetOuterWindow());
  }

  CompositionOp op = CurrentState().op;
  bool discardContent =
      GlobalAlpha() == 1.0f &&
      (op == CompositionOp::OP_OVER || op == CompositionOp::OP_SOURCE);
  const gfx::Rect drawRect(aX, aY, aW, aH);
  if (!EnsureTarget(aError, discardContent ? &drawRect : nullptr)) {
    return;
  }

  MOZ_ASSERT(IsTargetValid());

  RefPtr<nsPresContext> presContext;
  nsIDocShell* docshell = aWindow.GetDocShell();
  if (docshell) {
    presContext = docshell->GetPresContext();
  }
  if (!presContext) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  Maybe<nscolor> backgroundColor = ParseColor(aBgColor);
  if (!backgroundColor) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  nsRect r(nsPresContext::CSSPixelsToAppUnits((float)aX),
           nsPresContext::CSSPixelsToAppUnits((float)aY),
           nsPresContext::CSSPixelsToAppUnits((float)aW),
           nsPresContext::CSSPixelsToAppUnits((float)aH));
  RenderDocumentFlags renderDocFlags =
      (RenderDocumentFlags::IgnoreViewportScrolling |
       RenderDocumentFlags::DocumentRelative);
  if (aFlags & CanvasRenderingContext2D_Binding::DRAWWINDOW_DRAW_CARET) {
    renderDocFlags |= RenderDocumentFlags::DrawCaret;
  }
  if (aFlags & CanvasRenderingContext2D_Binding::DRAWWINDOW_DRAW_VIEW) {
    renderDocFlags &= ~(RenderDocumentFlags::IgnoreViewportScrolling |
                        RenderDocumentFlags::DocumentRelative);
  }
  if (aFlags & CanvasRenderingContext2D_Binding::DRAWWINDOW_USE_WIDGET_LAYERS) {
    renderDocFlags |= RenderDocumentFlags::UseWidgetLayers;
  }
  if (aFlags &
      CanvasRenderingContext2D_Binding::DRAWWINDOW_ASYNC_DECODE_IMAGES) {
    renderDocFlags |= RenderDocumentFlags::AsyncDecodeImages;
  }
  if (aFlags & CanvasRenderingContext2D_Binding::DRAWWINDOW_DO_NOT_FLUSH) {
    renderDocFlags |= RenderDocumentFlags::DrawWindowNotFlushing;
  }

  // gfxContext-over-Azure may modify the DrawTarget's transform, so
  // save and restore it
  Matrix matrix = mTarget->GetTransform();
  double sw = matrix._11 * aW;
  double sh = matrix._22 * aH;
  if (!sw || !sh) {
    return;
  }

  Maybe<gfxContext> thebes;
  RefPtr<DrawTarget> drawDT;
  // Rendering directly is faster and can be done if mTarget supports Azure
  // and does not need alpha blending.
  // Since the pre-transaction callback calls ReturnTarget, we can't have a
  // gfxContext wrapped around it when using a shared buffer provider because
  // the DrawTarget's shared buffer may be unmapped in ReturnTarget.
  op = CompositionOp::OP_ADD;
  if (gfxPlatform::GetPlatform()->SupportsAzureContentForDrawTarget(mTarget) &&
      GlobalAlpha() == 1.0f) {
    op = CurrentState().op;
    if (!IsTargetValid()) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }
  }
  if (op == CompositionOp::OP_OVER &&
      (!mBufferProvider || !mBufferProvider->IsShared())) {
    thebes.emplace(mTarget);
    thebes.ref().SetMatrix(matrix);
  } else {
    IntSize dtSize = IntSize::Ceil(sw, sh);
    if (!Factory::AllowedSurfaceSize(dtSize)) {
      // attempt to limit the DT to what will actually cover the target
      Size limitSize(mTarget->GetSize());
      limitSize.Scale(matrix._11, matrix._22);
      dtSize = Min(dtSize, IntSize::Ceil(limitSize));
      // if the DT is still too big, then error
      if (!Factory::AllowedSurfaceSize(dtSize)) {
        aError.Throw(NS_ERROR_FAILURE);
        return;
      }
    }
    drawDT = gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
        dtSize, SurfaceFormat::B8G8R8A8);
    if (!drawDT || !drawDT->IsValid()) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    thebes.emplace(drawDT);
    thebes.ref().SetMatrix(Matrix::Scaling(matrix._11, matrix._22));
  }
  MOZ_ASSERT(thebes.isSome());

  RefPtr<PresShell> presShell = presContext->PresShell();

  Unused << presShell->RenderDocument(r, renderDocFlags, *backgroundColor,
                                      &thebes.ref());
  // If this canvas was contained in the drawn window, the pre-transaction
  // callback may have returned its DT. If so, we must reacquire it here.
  if (!EnsureTarget(aError, discardContent ? &drawRect : nullptr)) {
    return;
  }

  MOZ_ASSERT(IsTargetValid());

  if (drawDT) {
    RefPtr<SourceSurface> snapshot = drawDT->Snapshot();
    if (NS_WARN_IF(!snapshot)) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    op = CurrentState().op;
    if (!IsTargetValid()) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }
    gfx::Rect destRect(0, 0, aW, aH);
    gfx::Rect sourceRect(0, 0, sw, sh);
    mTarget->DrawSurface(snapshot, destRect, sourceRect,
                         DrawSurfaceOptions(gfx::SamplingFilter::POINT),
                         DrawOptions(GlobalAlpha(), op, AntialiasMode::NONE));
  } else {
    mTarget->SetTransform(matrix);
  }

  // note that x and y are coordinates in the document that
  // we're drawing; x and y are drawn to 0,0 in current user
  // space.
  RedrawUser(gfxRect(0, 0, aW, aH));
}

//
// device pixel getting/setting
//

already_AddRefed<ImageData> CanvasRenderingContext2D::GetImageData(
    JSContext* aCx, int32_t aSx, int32_t aSy, int32_t aSw, int32_t aSh,
    nsIPrincipal& aSubjectPrincipal, ErrorResult& aError) {
  if (!mCanvasElement && !mDocShell && !mOffscreenCanvas) {
    NS_ERROR("No canvas element and no docshell in GetImageData!!!");
    aError.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  // Check only if we have a canvas element; if we were created with a docshell,
  // then it's special internal use.
  if (IsWriteOnly() ||
      (mCanvasElement && !mCanvasElement->CallerCanRead(aSubjectPrincipal)) ||
      (mOffscreenCanvas &&
       !mOffscreenCanvas->CallerCanRead(aSubjectPrincipal))) {
    // XXX ERRMSG we need to report an error to developers here! (bug 329026)
    aError.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  if (!aSw || !aSh) {
    aError.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }

  // Handle negative width and height by flipping the rectangle over in the
  // relevant direction.
  uint32_t w, h;
  if (aSw < 0) {
    w = uint32_t(-aSw);
    aSx -= w;
  } else {
    w = aSw;
  }
  if (aSh < 0) {
    h = uint32_t(-aSh);
    aSy -= h;
  } else {
    h = aSh;
  }

  if (w == 0) {
    w = 1;
  }
  if (h == 0) {
    h = 1;
  }

  JS::Rooted<JSObject*> array(aCx);
  aError = GetImageDataArray(aCx, aSx, aSy, w, h, aSubjectPrincipal,
                             array.address());
  if (aError.Failed()) {
    return nullptr;
  }
  MOZ_ASSERT(array);
  return do_AddRef(new ImageData(GetParentObject(), w, h, array));
}

static IntRect ClipImageDataTransfer(IntRect& aSrc, const IntPoint& aDestOffset,
                                     const IntSize& aDestBounds) {
  IntRect dest = aSrc;
  dest.SafeMoveBy(aDestOffset);
  dest = IntRect(IntPoint(0, 0), aDestBounds).SafeIntersect(dest);

  aSrc = aSrc.SafeIntersect(dest - aDestOffset);
  return aSrc + aDestOffset;
}

nsresult CanvasRenderingContext2D::GetImageDataArray(
    JSContext* aCx, int32_t aX, int32_t aY, uint32_t aWidth, uint32_t aHeight,
    nsIPrincipal& aSubjectPrincipal, JSObject** aRetval) {
  MOZ_ASSERT(aWidth && aHeight);

  // Restrict the typed array length to INT32_MAX because that's all we support.
  CheckedInt<uint32_t> len = CheckedInt<uint32_t>(aWidth) * aHeight * 4;
  if (!len.isValid() || len.value() > INT32_MAX) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  CheckedInt<int32_t> rightMost = CheckedInt<int32_t>(aX) + aWidth;
  CheckedInt<int32_t> bottomMost = CheckedInt<int32_t>(aY) + aHeight;

  if (!rightMost.isValid() || !bottomMost.isValid()) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  JS::Rooted<JSObject*> darray(aCx, JS_NewUint8ClampedArray(aCx, len.value()));
  if (!darray) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (mZero) {
    *aRetval = darray;
    return NS_OK;
  }

  IntRect dstWriteRect(0, 0, aWidth, aHeight);
  IntRect srcReadRect = ClipImageDataTransfer(dstWriteRect, IntPoint(aX, aY),
                                              IntSize(mWidth, mHeight));
  if (srcReadRect.IsEmpty()) {
    *aRetval = darray;
    return NS_OK;
  }

  if (!GetBufferProvider() && !EnsureTarget()) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<SourceSurface> snapshot = mBufferProvider->BorrowSnapshot();
  if (!snapshot) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  RefPtr<DataSourceSurface> readback = snapshot->GetDataSurface();
  mBufferProvider->ReturnSnapshot(snapshot.forget());

  // Check for site-specific permission.
  CanvasUtils::ImageExtraction permission =
      CanvasUtils::ImageExtraction::Unrestricted;
  if (mCanvasElement) {
    permission = CanvasUtils::ImageExtractionResult(mCanvasElement, aCx,
                                                    aSubjectPrincipal);
  } else if (mOffscreenCanvas) {
    permission = CanvasUtils::ImageExtractionResult(mOffscreenCanvas, aCx,
                                                    aSubjectPrincipal);
  }

  // Clone the data source surface if canvas randomization is enabled. We need
  // to do this because we don't want to alter the actual image buffer.
  // Otherwise, we will provide inconsistent image data with multiple calls.
  //
  // Note that we don't need to clone if we will use the place holder because
  // the place holder doesn't use actual image data.
  if (permission == CanvasUtils::ImageExtraction::Randomize) {
    if (readback) {
      readback = CreateDataSourceSurfaceByCloning(readback);
    }
  }

  DataSourceSurface::MappedSurface rawData;
  if (!readback || !readback->Map(DataSourceSurface::READ, &rawData)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  do {
    uint8_t* randomData;
    if (permission == CanvasUtils::ImageExtraction::Placeholder) {
      // Since we cannot call any GC-able functions (like requesting the RNG
      // service) after we call JS_GetUint8ClampedArrayData, we will
      // pre-generate the randomness required for GeneratePlaceholderCanvasData.
      randomData = TryToGenerateRandomDataForPlaceholderCanvasData();
    } else if (permission == CanvasUtils::ImageExtraction::Randomize) {
      // Apply the random noises if canvan randomization is enabled. We don't
      // need to calculate random noises if we are going to use the place
      // holder.

      const IntSize size = readback->GetSize();
      nsRFPService::RandomizePixels(
          GetCookieJarSettings(), rawData.mData, size.width, size.height,
          size.height * size.width * 4, SurfaceFormat::A8R8G8B8_UINT32);
    }

    JS::AutoCheckCannotGC nogc;
    bool isShared;
    uint8_t* data = JS_GetUint8ClampedArrayData(darray, &isShared, nogc);
    MOZ_ASSERT(!isShared);  // Should not happen, data was created above

    if (permission == CanvasUtils::ImageExtraction::Placeholder) {
      FillPlaceholderCanvas(randomData, len.value(), data);
      break;
    }

    uint32_t srcStride = rawData.mStride;
    uint8_t* src =
        rawData.mData + srcReadRect.y * srcStride + srcReadRect.x * 4;

    uint8_t* dst = data + dstWriteRect.y * (aWidth * 4) + dstWriteRect.x * 4;

    if (mOpaque) {
      SwizzleData(src, srcStride, SurfaceFormat::X8R8G8B8_UINT32, dst,
                  aWidth * 4, SurfaceFormat::R8G8B8A8, dstWriteRect.Size());
    } else {
      UnpremultiplyData(src, srcStride, SurfaceFormat::A8R8G8B8_UINT32, dst,
                        aWidth * 4, SurfaceFormat::R8G8B8A8,
                        dstWriteRect.Size());
    }
  } while (false);

  readback->Unmap();
  *aRetval = darray;
  return NS_OK;
}

void CanvasRenderingContext2D::EnsureErrorTarget() {
  if (sErrorTarget.get()) {
    return;
  }

  RefPtr<DrawTarget> errorTarget =
      gfxPlatform::GetPlatform()->CreateOffscreenCanvasDrawTarget(
          IntSize(1, 1), SurfaceFormat::B8G8R8A8);
  MOZ_ASSERT(errorTarget, "Failed to allocate the error target!");

  sErrorTarget.set(errorTarget.forget().take());
}

void CanvasRenderingContext2D::FillRuleChanged() {
  if (mPath) {
    mPathBuilder = Path::ToBuilder(mPath.forget(), CurrentState().fillRule);
  }
}

void CanvasRenderingContext2D::PutImageData(ImageData& aImageData, int32_t aDx,
                                            int32_t aDy, ErrorResult& aRv) {
  RootedSpiderMonkeyInterface<Uint8ClampedArray> arr(RootingCx());
  PutImageData_explicit(aDx, aDy, aImageData, false, 0, 0, 0, 0, aRv);
}

void CanvasRenderingContext2D::PutImageData(ImageData& aImageData, int32_t aDx,
                                            int32_t aDy, int32_t aDirtyX,
                                            int32_t aDirtyY,
                                            int32_t aDirtyWidth,
                                            int32_t aDirtyHeight,
                                            ErrorResult& aRv) {
  PutImageData_explicit(aDx, aDy, aImageData, true, aDirtyX, aDirtyY,
                        aDirtyWidth, aDirtyHeight, aRv);
}

void CanvasRenderingContext2D::PutImageData_explicit(
    int32_t aX, int32_t aY, ImageData& aImageData, bool aHasDirtyRect,
    int32_t aDirtyX, int32_t aDirtyY, int32_t aDirtyWidth, int32_t aDirtyHeight,
    ErrorResult& aRv) {
  RootedSpiderMonkeyInterface<Uint8ClampedArray> arr(RootingCx());
  if (!arr.Init(aImageData.GetDataObject())) {
    return aRv.ThrowInvalidStateError(
        "Failed to extract Uint8ClampedArray from ImageData (security check "
        "failed?)");
  }

  const uint32_t width = aImageData.Width();
  const uint32_t height = aImageData.Height();
  if (width == 0 || height == 0) {
    return aRv.ThrowInvalidStateError("Passed-in image is empty");
  }

  IntRect dirtyRect;
  IntRect imageDataRect(0, 0, width, height);

  if (aHasDirtyRect) {
    // fix up negative dimensions
    if (aDirtyWidth < 0) {
      if (aDirtyWidth == INT_MIN) {
        return aRv.ThrowInvalidStateError("Dirty width is invalid");
      }

      CheckedInt32 checkedDirtyX = CheckedInt32(aDirtyX) + aDirtyWidth;

      if (!checkedDirtyX.isValid()) {
        return aRv.ThrowInvalidStateError("Dirty width is invalid");
      }

      aDirtyX = checkedDirtyX.value();
      aDirtyWidth = -aDirtyWidth;
    }

    if (aDirtyHeight < 0) {
      if (aDirtyHeight == INT_MIN) {
        return aRv.ThrowInvalidStateError("Dirty height is invalid");
      }

      CheckedInt32 checkedDirtyY = CheckedInt32(aDirtyY) + aDirtyHeight;

      if (!checkedDirtyY.isValid()) {
        return aRv.ThrowInvalidStateError("Dirty height is invalid");
      }

      aDirtyY = checkedDirtyY.value();
      aDirtyHeight = -aDirtyHeight;
    }

    // bound the dirty rect within the imageData rectangle
    dirtyRect = imageDataRect.Intersect(
        IntRect(aDirtyX, aDirtyY, aDirtyWidth, aDirtyHeight));

    if (dirtyRect.Width() <= 0 || dirtyRect.Height() <= 0) {
      return;
    }
  } else {
    dirtyRect = imageDataRect;
  }

  IntRect srcRect = dirtyRect;
  dirtyRect = ClipImageDataTransfer(srcRect, IntPoint(aX, aY),
                                    IntSize(mWidth, mHeight));
  if (dirtyRect.IsEmpty()) {
    return;
  }

  RefPtr<DataSourceSurface> sourceSurface;
  uint8_t* lockedBits = nullptr;

  // The canvas spec says that the current path, transformation matrix,
  // shadow attributes, global alpha, the clipping region, and global
  // composition operator must not affect the getImageData() and
  // putImageData() methods.
  const gfx::Rect putRect(dirtyRect);
  if (!EnsureTarget(aRv, &putRect, true, true)) {
    return;
  }

  MOZ_ASSERT(IsTargetValid());

  DataSourceSurface::MappedSurface map;
  uint8_t* dstData;
  IntSize dstSize;
  int32_t dstStride;
  SurfaceFormat dstFormat;
  if (mTarget->LockBits(&lockedBits, &dstSize, &dstStride, &dstFormat)) {
    dstData = lockedBits + dirtyRect.y * dstStride + dirtyRect.x * 4;
  } else {
    sourceSurface = Factory::CreateDataSourceSurface(
        dirtyRect.Size(), SurfaceFormat::B8G8R8A8, false);

    // In certain scenarios, requesting larger than 8k image fails.  Bug
    // 803568 covers the details of how to run into it, but the full
    // detailed investigation hasn't been done to determine the
    // underlying cause.  We will just handle the failure to allocate
    // the surface to avoid a crash.
    if (!sourceSurface) {
      return aRv.Throw(NS_ERROR_FAILURE);
    }
    if (!sourceSurface->Map(DataSourceSurface::READ_WRITE, &map)) {
      return aRv.Throw(NS_ERROR_FAILURE);
    }

    dstData = map.mData;
    if (!dstData) {
      return aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    }
    dstStride = map.mStride;
    dstFormat = sourceSurface->GetFormat();
  }

  arr.ProcessData(
      [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&& nogc) {
        // Verify that the length hasn't changed.
        if (aData.Length() != width * height * 4) {
          // FIXME Should this call ReleaseBits/Unmap?
          return aRv.ThrowInvalidStateError("Invalid width or height");
        }

        uint8_t* srcData =
            aData.Elements() + srcRect.y * (width * 4) + srcRect.x * 4;

        PremultiplyData(srcData, width * 4, SurfaceFormat::R8G8B8A8, dstData,
                        dstStride,
                        mOpaque ? SurfaceFormat::X8R8G8B8_UINT32
                                : SurfaceFormat::A8R8G8B8_UINT32,
                        dirtyRect.Size());
      });

  if (aRv.Failed()) {
    return;
  }

  if (lockedBits) {
    mTarget->ReleaseBits(lockedBits);
  } else if (sourceSurface) {
    sourceSurface->Unmap();
    mTarget->CopySurface(sourceSurface, dirtyRect - dirtyRect.TopLeft(),
                         dirtyRect.TopLeft());
  }

  Redraw(
      gfx::Rect(dirtyRect.x, dirtyRect.y, dirtyRect.width, dirtyRect.height));
}

static already_AddRefed<ImageData> CreateImageData(
    JSContext* aCx, CanvasRenderingContext2D* aContext, uint32_t aW,
    uint32_t aH, ErrorResult& aError) {
  if (aW == 0) aW = 1;
  if (aH == 0) aH = 1;

  // Restrict the typed array length to INT32_MAX because that's all we support
  // in dom::TypedArray::ComputeState.
  CheckedInt<uint32_t> len = CheckedInt<uint32_t>(aW) * aH * 4;
  if (!len.isValid() || len.value() > INT32_MAX) {
    aError.ThrowIndexSizeError("Invalid width or height");
    return nullptr;
  }

  // Create the fast typed array; it's initialized to 0 by default.
  JS::Rooted<JSObject*> darray(
      aCx, Uint8ClampedArray::Create(aCx, aContext, len.value(), aError));
  if (aError.Failed()) {
    return nullptr;
  }

  return do_AddRef(new ImageData(aContext->GetParentObject(), aW, aH, darray));
}

already_AddRefed<ImageData> CanvasRenderingContext2D::CreateImageData(
    JSContext* aCx, int32_t aSw, int32_t aSh, ErrorResult& aError) {
  if (!aSw || !aSh) {
    aError.ThrowIndexSizeError("Invalid width or height");
    return nullptr;
  }

  uint32_t w = Abs(aSw);
  uint32_t h = Abs(aSh);
  return dom::CreateImageData(aCx, this, w, h, aError);
}

already_AddRefed<ImageData> CanvasRenderingContext2D::CreateImageData(
    JSContext* aCx, ImageData& aImagedata, ErrorResult& aError) {
  return dom::CreateImageData(aCx, this, aImagedata.Width(),
                              aImagedata.Height(), aError);
}

void CanvasRenderingContext2D::OnMemoryPressure() {
  if (mBufferProvider) {
    mBufferProvider->OnMemoryPressure();
  }
}

void CanvasRenderingContext2D::OnBeforePaintTransaction() {
  if (!mTarget) return;
  OnStableState();
}

void CanvasRenderingContext2D::OnDidPaintTransaction() { MarkContextClean(); }

bool CanvasRenderingContext2D::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  if (mOpaque) {
    // If we're opaque then make sure we have a surface so we paint black
    // instead of transparent.
    EnsureTarget();
  }

  // Don't call EnsureTarget() ... if there isn't already a surface, then
  // we have nothing to paint and there is no need to create a surface just
  // to paint nothing. Also, EnsureTarget() can cause creation of a persistent
  // layer manager which must NOT happen during a paint.
  if (!mBufferProvider && !IsTargetValid()) {
    // No DidTransactionCallback will be received, so mark the context clean
    // now so future invalidations will be dispatched.
    MarkContextClean();
    // Clear CanvasRenderer of WebRenderCanvasData
    aCanvasData->ClearCanvasRenderer();
    return false;
  }

  auto renderer = aCanvasData->GetCanvasRenderer();

  if (!mResetLayer && renderer) {
    CanvasRendererData data;
    data.mContext = this;
    data.mSize = GetSize();

    if (renderer->IsDataValid(data)) {
      return true;
    }
  }

  renderer = aCanvasData->CreateCanvasRenderer();
  if (!InitializeCanvasRenderer(aBuilder, renderer)) {
    // Clear CanvasRenderer of WebRenderCanvasData
    aCanvasData->ClearCanvasRenderer();
    return false;
  }

  MOZ_ASSERT(renderer);
  mResetLayer = false;
  return true;
}

bool CanvasRenderingContext2D::InitializeCanvasRenderer(
    nsDisplayListBuilder* aBuilder, CanvasRenderer* aRenderer) {
  CanvasRendererData data;
  data.mContext = this;
  data.mSize = GetSize();
  data.mIsOpaque = mOpaque;
  data.mDoPaintCallbacks = true;

  if (!mBufferProvider) {
    // Force the creation of a buffer provider.
    EnsureTarget();
    ReturnTarget();
    if (!mBufferProvider) {
      MarkContextClean();
      return false;
    }
  }

  aRenderer->Initialize(data);
  aRenderer->SetDirty();
  return true;
}

void CanvasRenderingContext2D::MarkContextClean() {
  if (mInvalidateCount > 0) {
    mPredictManyRedrawCalls = mInvalidateCount > kCanvasMaxInvalidateCount;
  }
  mIsEntireFrameInvalid = false;
  mInvalidateCount = 0;
}

void CanvasRenderingContext2D::GetAppUnitsValues(int32_t* aPerDevPixel,
                                                 int32_t* aPerCSSPixel) {
  // If we don't have a canvas element, we just return something generic.
  if (aPerDevPixel) {
    *aPerDevPixel = 60;
  }
  if (aPerCSSPixel) {
    *aPerCSSPixel = 60;
  }
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }
  nsPresContext* presContext = presShell->GetPresContext();
  if (!presContext) {
    return;
  }
  if (aPerDevPixel) {
    *aPerDevPixel = presContext->AppUnitsPerDevPixel();
  }
  if (aPerCSSPixel) {
    *aPerCSSPixel = AppUnitsPerCSSPixel();
  }
}

void CanvasRenderingContext2D::SetWriteOnly() {
  mWriteOnly = true;
  if (mCanvasElement) {
    mCanvasElement->SetWriteOnly();
  } else if (mOffscreenCanvas) {
    mOffscreenCanvas->SetWriteOnly();
  }
}

bool CanvasRenderingContext2D::UseSoftwareRendering() const {
  return mWillReadFrequently || mForceSoftwareRendering;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CanvasPath, mParent)

CanvasPath::CanvasPath(nsISupports* aParent) : mParent(aParent) {
  mPathBuilder =
      gfxPlatform::ThreadLocalScreenReferenceDrawTarget()->CreatePathBuilder();
}

CanvasPath::CanvasPath(nsISupports* aParent,
                       already_AddRefed<PathBuilder> aPathBuilder)
    : mParent(aParent), mPathBuilder(aPathBuilder) {
  if (!mPathBuilder) {
    mPathBuilder = gfxPlatform::ThreadLocalScreenReferenceDrawTarget()
                       ->CreatePathBuilder();
  }
}

JSObject* CanvasPath::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return Path2D_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<CanvasPath> CanvasPath::Constructor(
    const GlobalObject& aGlobal) {
  RefPtr<CanvasPath> path = new CanvasPath(aGlobal.GetAsSupports());
  return path.forget();
}

already_AddRefed<CanvasPath> CanvasPath::Constructor(
    const GlobalObject& aGlobal, CanvasPath& aCanvasPath) {
  RefPtr<gfx::DrawTarget> drawTarget =
      gfxPlatform::ThreadLocalScreenReferenceDrawTarget();
  RefPtr<gfx::Path> tempPath =
      aCanvasPath.GetPath(CanvasWindingRule::Nonzero, drawTarget.get());

  RefPtr<CanvasPath> path =
      new CanvasPath(aGlobal.GetAsSupports(), tempPath->CopyToBuilder());
  return path.forget();
}

already_AddRefed<CanvasPath> CanvasPath::Constructor(
    const GlobalObject& aGlobal, const nsACString& aPathString) {
  RefPtr<gfx::Path> tempPath = SVGContentUtils::GetPath(aPathString);
  if (!tempPath) {
    return Constructor(aGlobal);
  }

  RefPtr<CanvasPath> path =
      new CanvasPath(aGlobal.GetAsSupports(), tempPath->CopyToBuilder());
  return path.forget();
}

void CanvasPath::ClosePath() {
  EnsurePathBuilder();

  mPathBuilder->Close();
  mPruned = false;
}

inline void CanvasPath::EnsureCapped() const {
  // If there were zero-length segments emitted that were pruned, we need to
  // emit a LineTo to ensure that caps are generated for the segment.
  if (mPruned) {
    mPathBuilder->LineTo(mPathBuilder->CurrentPoint());
    mPruned = false;
  }
}

inline void CanvasPath::EnsureActive() const {
  // If the path is not active, then adding an op to the path may cause the path
  // to add the first point of the op as the initial point instead of the actual
  // current point.
  if (mPruned && !mPathBuilder->IsActive()) {
    mPathBuilder->MoveTo(mPathBuilder->CurrentPoint());
    mPruned = false;
  }
}

void CanvasPath::MoveTo(double aX, double aY) {
  EnsurePathBuilder();

  Point pos(ToFloat(aX), ToFloat(aY));
  if (!pos.IsFinite()) {
    return;
  }

  EnsureCapped();
  mPathBuilder->MoveTo(pos);
}

void CanvasPath::LineTo(double aX, double aY) {
  LineTo(Point(ToFloat(aX), ToFloat(aY)));
}

void CanvasPath::QuadraticCurveTo(double aCpx, double aCpy, double aX,
                                  double aY) {
  EnsurePathBuilder();

  Point cp1(ToFloat(aCpx), ToFloat(aCpy));
  Point cp2(ToFloat(aX), ToFloat(aY));
  if (!cp1.IsFinite() || !cp2.IsFinite()) {
    return;
  }
  if (cp1 == mPathBuilder->CurrentPoint() && cp1 == cp2) {
    mPruned = true;
    return;
  }

  EnsureActive();

  mPathBuilder->QuadraticBezierTo(cp1, cp2);
  mPruned = false;
}

void CanvasPath::BezierCurveTo(double aCp1x, double aCp1y, double aCp2x,
                               double aCp2y, double aX, double aY) {
  BezierTo(gfx::Point(ToFloat(aCp1x), ToFloat(aCp1y)),
           gfx::Point(ToFloat(aCp2x), ToFloat(aCp2y)),
           gfx::Point(ToFloat(aX), ToFloat(aY)));
}

void CanvasPath::ArcTo(double aX1, double aY1, double aX2, double aY2,
                       double aRadius, ErrorResult& aError) {
  if (aRadius < 0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }

  EnsurePathBuilder();

  // Current point in user space!
  Point p0 = mPathBuilder->CurrentPoint();
  Point p1(aX1, aY1);
  Point p2(aX2, aY2);

  if (!p1.IsFinite() || !p2.IsFinite() || !std::isfinite(aRadius)) {
    return;
  }

  // Execute these calculations in double precision to avoid cumulative
  // rounding errors.
  double dir, a2, b2, c2, cosx, sinx, d, anx, any, bnx, bny, x3, y3, x4, y4, cx,
      cy, angle0, angle1;
  bool anticlockwise;

  if (p0 == p1 || p1 == p2 || aRadius == 0) {
    LineTo(p1);
    return;
  }

  // Check for colinearity
  dir = (p2.x.value - p1.x.value) * (p0.y.value - p1.y.value) +
        (p2.y.value - p1.y.value) * (p1.x.value - p0.x.value);
  if (dir == 0) {
    LineTo(p1);
    return;
  }

  // XXX - Math for this code was already available from the non-azure code
  // and would be well tested. Perhaps converting to bezier directly might
  // be more efficient longer run.
  a2 = (p0.x - aX1) * (p0.x - aX1) + (p0.y - aY1) * (p0.y - aY1);
  b2 = (aX1 - aX2) * (aX1 - aX2) + (aY1 - aY2) * (aY1 - aY2);
  c2 = (p0.x - aX2) * (p0.x - aX2) + (p0.y - aY2) * (p0.y - aY2);
  cosx = (a2 + b2 - c2) / (2 * sqrt(a2 * b2));

  sinx = sqrt(1 - cosx * cosx);
  d = aRadius / ((1 - cosx) / sinx);

  anx = (aX1 - p0.x) / sqrt(a2);
  any = (aY1 - p0.y) / sqrt(a2);
  bnx = (aX1 - aX2) / sqrt(b2);
  bny = (aY1 - aY2) / sqrt(b2);
  x3 = aX1 - anx * d;
  y3 = aY1 - any * d;
  x4 = aX1 - bnx * d;
  y4 = aY1 - bny * d;
  anticlockwise = (dir < 0);
  cx = x3 + any * aRadius * (anticlockwise ? 1 : -1);
  cy = y3 - anx * aRadius * (anticlockwise ? 1 : -1);
  angle0 = atan2((y3 - cy), (x3 - cx));
  angle1 = atan2((y4 - cy), (x4 - cx));

  LineTo(x3, y3);

  Arc(cx, cy, aRadius, angle0, angle1, anticlockwise, aError);
}

void CanvasPath::Rect(double aX, double aY, double aW, double aH) {
  EnsurePathBuilder();

  if (!std::isfinite(aX) || !std::isfinite(aY) || !std::isfinite(aW) ||
      !std::isfinite(aH)) {
    return;
  }

  MoveTo(aX, aY);
  if (aW == 0 && aH == 0) {
    return;
  }
  LineTo(aX + aW, aY);
  LineTo(aX + aW, aY + aH);
  LineTo(aX, aY + aH);
  ClosePath();
}

void CanvasPath::RoundRect(
    double aX, double aY, double aW, double aH,
    const UnrestrictedDoubleOrDOMPointInitOrUnrestrictedDoubleOrDOMPointInitSequence&
        aRadii,
    ErrorResult& aError) {
  EnsurePathBuilder();

  EnsureCapped();
  RoundRectImpl(mPathBuilder, Nothing(), aX, aY, aW, aH, aRadii, aError);
}

void CanvasPath::Arc(double aX, double aY, double aRadius, double aStartAngle,
                     double aEndAngle, bool aAnticlockwise,
                     ErrorResult& aError) {
  if (aRadius < 0.0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }
  if (aStartAngle == aEndAngle) {
    LineTo(aX + aRadius * cos(aStartAngle), aY + aRadius * sin(aStartAngle));
    return;
  }

  EnsurePathBuilder();

  EnsureActive();

  mPathBuilder->Arc(Point(aX, aY), aRadius, aStartAngle, aEndAngle,
                    aAnticlockwise);
  mPruned = false;
}

void CanvasPath::Ellipse(double x, double y, double radiusX, double radiusY,
                         double rotation, double startAngle, double endAngle,
                         bool anticlockwise, ErrorResult& aError) {
  if (radiusX < 0.0 || radiusY < 0.0) {
    return aError.ThrowIndexSizeError("Negative radius");
  }

  EnsurePathBuilder();

  ArcToBezier(this, Point(x, y), Size(radiusX, radiusY), startAngle, endAngle,
              anticlockwise, rotation);
  mPruned = false;
}

void CanvasPath::LineTo(const gfx::Point& aPoint) {
  EnsurePathBuilder();

  if (!aPoint.IsFinite()) {
    return;
  }
  if (aPoint == mPathBuilder->CurrentPoint()) {
    mPruned = true;
    return;
  }

  EnsureActive();

  mPathBuilder->LineTo(aPoint);
  mPruned = false;
}

void CanvasPath::BezierTo(const gfx::Point& aCP1, const gfx::Point& aCP2,
                          const gfx::Point& aCP3) {
  EnsurePathBuilder();

  if (!aCP1.IsFinite() || !aCP2.IsFinite() || !aCP3.IsFinite()) {
    return;
  }
  if (aCP1 == mPathBuilder->CurrentPoint() && aCP1 == aCP2 && aCP1 == aCP3) {
    mPruned = true;
    return;
  }

  EnsureActive();

  mPathBuilder->BezierTo(aCP1, aCP2, aCP3);
  mPruned = false;
}

void CanvasPath::AddPath(CanvasPath& aCanvasPath, const DOMMatrix2DInit& aInit,
                         ErrorResult& aError) {
  RefPtr<gfx::DrawTarget> drawTarget =
      gfxPlatform::ThreadLocalScreenReferenceDrawTarget();
  RefPtr<gfx::Path> tempPath =
      aCanvasPath.GetPath(CanvasWindingRule::Nonzero, drawTarget.get());

  Matrix transform(DOMMatrixReadOnly::ToValidatedMatrixDouble(aInit, aError));
  if (aError.Failed()) {
    return;
  }

  if (!transform.IsFinite()) {
    return;
  }

  if (!transform.IsIdentity()) {
    Path::TransformAndSetFillRule(tempPath, transform, FillRule::FILL_WINDING);
  }

  EnsurePathBuilder();  // in case a path is added to itself
  EnsureCapped();
  tempPath->StreamToSink(mPathBuilder);
}

already_AddRefed<gfx::Path> CanvasPath::GetPath(
    const CanvasWindingRule& aWinding, BackendType aBackendType) const {
  FillRule fillRule = FillRule::FILL_WINDING;
  if (aWinding == CanvasWindingRule::Evenodd) {
    fillRule = FillRule::FILL_EVEN_ODD;
  }

  if (mPath && (mPath->GetBackendType() == aBackendType) &&
      (mPath->GetFillRule() == fillRule)) {
    RefPtr<gfx::Path> path(mPath);
    return path.forget();
  }

  if (!mPath) {
    // if there is no path, there must be a pathbuilder
    MOZ_ASSERT(mPathBuilder);
    EnsureCapped();
    mPath = mPathBuilder->Finish();
    if (!mPath) {
      RefPtr<gfx::Path> path(mPath);
      return path.forget();
    }

    mPathBuilder = nullptr;
  }

  // retarget our backend if we're used with a different backend
  if (mPath->GetBackendType() != aBackendType) {
    RefPtr<PathBuilder> tmpPathBuilder =
        Factory::CreatePathBuilder(aBackendType, fillRule);
    mPath->StreamToSink(tmpPathBuilder);
    mPath = tmpPathBuilder->Finish();
  } else if (mPath->GetFillRule() != fillRule) {
    Path::SetFillRule(mPath, fillRule);
  }

  RefPtr<gfx::Path> path(mPath);
  return path.forget();
}

void CanvasPath::EnsurePathBuilder() const {
  if (mPathBuilder) {
    return;
  }

  // if there is not pathbuilder, there must be a path
  MOZ_ASSERT(mPath);
  mPathBuilder = Path::ToBuilder(mPath.forget());
}

size_t BindingJSObjectMallocBytes(CanvasRenderingContext2D* aContext) {
  IntSize size = aContext->GetSize();

  // TODO: Bug 1552137: No memory will be allocated if either dimension is
  // greater than gfxPrefs::gfx_canvas_max_size(). We should check this here
  // too.

  CheckedInt<uint32_t> bytes =
      CheckedInt<uint32_t>(size.width) * size.height * 4;
  if (!bytes.isValid()) {
    return 0;
  }

  return bytes.value();
}

}  // namespace mozilla::dom
