/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CanvasRenderingContext2D_h
#define CanvasRenderingContext2D_h

#include <numbers>

#include "FilterDescription.h"
#include "gfx2DGlue.h"
#include "gfxFontConstants.h"
#include "gfxTextRun.h"
#include "gfxUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/Maybe.h"
#include "mozilla/MruCache.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SurfaceFromElementResult.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CanvasRenderingContext2DBinding.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/intl/Bidi.h"
#include "nsColor.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsIFrame.h"
#include "nsRFPService.h"

class gfxFontGroup;
class nsGlobalWindowInner;
class nsXULElement;

namespace mozilla {
class ErrorResult;
class ISVGFilterObserverList;
class PresShell;

namespace gl {
class SourceSurface;
}  // namespace gl

namespace layers {
class PersistentBufferProvider;
enum class LayersBackend : int8_t;
}  // namespace layers

namespace dom {
class
    HTMLImageElementOrSVGImageElementOrHTMLCanvasElementOrHTMLVideoElementOrOffscreenCanvasOrImageBitmapOrVideoFrame;
using CanvasImageSource =
    HTMLImageElementOrSVGImageElementOrHTMLCanvasElementOrHTMLVideoElementOrOffscreenCanvasOrImageBitmapOrVideoFrame;
class ImageBitmap;
class ImageData;
struct ImageDataSettings;
class UTF8StringOrCanvasGradientOrCanvasPattern;
class OwningUTF8StringOrCanvasGradientOrCanvasPattern;
class TextMetrics;
class CanvasGradient;
class CanvasPath;
class CanvasPattern;

extern const mozilla::gfx::Float SIGMA_MAX;

template <typename T>
class Optional;

struct CanvasBidiProcessor;
class CanvasDrawObserver;
class CanvasShutdownObserver;

class DOMMatrix;
class DOMMatrixReadOnly;
struct DOMMatrix2DInit;

/**
 ** CanvasRenderingContext2D
 **/
class CanvasRenderingContext2D : public nsICanvasRenderingContextInternal,
                                 public nsWrapperCache {
 protected:
  virtual ~CanvasRenderingContext2D();

 public:
  explicit CanvasRenderingContext2D(layers::LayersBackend aCompositorBackend);

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  HTMLCanvasElement* GetCanvas() const {
    if (!mCanvasElement || mCanvasElement->IsInNativeAnonymousSubtree()) {
      return nullptr;
    }

    // corresponds to changes to the old bindings made in bug 745025
    return mCanvasElement->GetOriginalCanvas();
  }

  void GetContextAttributes(CanvasRenderingContext2DSettings& aSettings) const;

  void GetDebugInfo(bool aEnsureTarget,
                    CanvasRenderingContext2DDebugInfo& aDebugInfo,
                    ErrorResult& aError);

  void OnMemoryPressure() override;
  void OnBeforePaintTransaction() override;
  void OnDidPaintTransaction() override;
  void OnWindowDestroy() override;
  layers::PersistentBufferProvider* GetBufferProvider() override;

  Maybe<layers::SurfaceDescriptor> GetFrontBuffer(
      WebGLFramebufferJS*, const bool webvr = false) override;

  already_AddRefed<layers::FwdTransactionTracker> UseCompositableForwarder(
      layers::CompositableForwarder* aForwarder) override;

  mozilla::gfx::Matrix GetCurrentTransform() const;
  bool HasAnyClips() const;

  void Save();
  void Restore();

  void Reset() {
    // reset the rendering context to its default state
    // Userland polyfill is `c2d.width = c2d.width;`
    SetDimensions(GetWidth(), GetHeight());
  }

  void Scale(double aX, double aY, mozilla::ErrorResult& aError);
  void Rotate(double aAngle, mozilla::ErrorResult& aError);
  void Translate(double aX, double aY, mozilla::ErrorResult& aError);
  void Transform(double aM11, double aM12, double aM21, double aM22, double aDx,
                 double aDy, mozilla::ErrorResult& aError);
  already_AddRefed<DOMMatrix> GetTransform(mozilla::ErrorResult& aError);
  void SetTransform(double aM11, double aM12, double aM21, double aM22,
                    double aDx, double aDy, mozilla::ErrorResult& aError);
  void SetTransform(const DOMMatrix2DInit& aInit, mozilla::ErrorResult& aError);
  void ResetTransform(mozilla::ErrorResult& aError);

  double GlobalAlpha() { return CurrentState().globalAlpha; }

  // Useful for silencing cast warnings
  static mozilla::gfx::Float ToFloat(double aValue) {
    return mozilla::gfx::Float(aValue);
  }

  void SetGlobalAlpha(double aGlobalAlpha) {
    if (aGlobalAlpha >= 0.0 && aGlobalAlpha <= 1.0) {
      CurrentState().globalAlpha = ToFloat(aGlobalAlpha);
    }
  }

  enum class ResolveCurrentColor : bool { No, Yes };
  Maybe<nscolor> ParseColor(const nsACString&,
                            ResolveCurrentColor = ResolveCurrentColor::Yes);

  void GetGlobalCompositeOperation(nsAString& aOp,
                                   mozilla::ErrorResult& aError);
  void SetGlobalCompositeOperation(const nsAString& aOp,
                                   mozilla::ErrorResult& aError);

  void GetStrokeStyle(OwningUTF8StringOrCanvasGradientOrCanvasPattern& aValue) {
    GetStyleAsUnion(aValue, Style::STROKE);
  }

  void SetStrokeStyle(const UTF8StringOrCanvasGradientOrCanvasPattern& aValue) {
    SetStyleFromUnion(aValue, Style::STROKE);
  }

  void GetFillStyle(OwningUTF8StringOrCanvasGradientOrCanvasPattern& aValue) {
    GetStyleAsUnion(aValue, Style::FILL);
  }

  void SetFillStyle(const UTF8StringOrCanvasGradientOrCanvasPattern& aValue) {
    SetStyleFromUnion(aValue, Style::FILL);
  }

  already_AddRefed<CanvasGradient> CreateLinearGradient(double aX0, double aY0,
                                                        double aX1, double aY1);
  already_AddRefed<CanvasGradient> CreateRadialGradient(double aX0, double aY0,
                                                        double aR0, double aX1,
                                                        double aY1, double aR1,
                                                        ErrorResult& aError);
  already_AddRefed<CanvasGradient> CreateConicGradient(double aAngle,
                                                       double aCx, double aCy);
  already_AddRefed<CanvasPattern> CreatePattern(
      const CanvasImageSource& aElement, const nsAString& aRepeat,
      ErrorResult& aError);

  double ShadowOffsetX() { return CurrentState().shadowOffset.x; }

  void SetShadowOffsetX(double aShadowOffsetX) {
    CurrentState().shadowOffset.x = ToFloat(aShadowOffsetX);
  }

  double ShadowOffsetY() { return CurrentState().shadowOffset.y; }

  void SetShadowOffsetY(double aShadowOffsetY) {
    CurrentState().shadowOffset.y = ToFloat(aShadowOffsetY);
  }

  double ShadowBlur() { return CurrentState().shadowBlur; }

  void SetShadowBlur(double aShadowBlur) {
    if (aShadowBlur >= 0.0) {
      CurrentState().shadowBlur = ToFloat(aShadowBlur);
    }
  }

  void GetShadowColor(nsACString& aShadowColor) {
    StyleColorToString(CurrentState().shadowColor, aShadowColor);
  }

  void GetFilter(nsACString& aFilter) { aFilter = CurrentState().filterString; }

  void SetShadowColor(const nsACString& aShadowColor);
  void SetFilter(const nsACString& aFilter, mozilla::ErrorResult& aError);
  void ClearRect(double aX, double aY, double aW, double aH);
  void FillRect(double aX, double aY, double aW, double aH);
  void StrokeRect(double aX, double aY, double aW, double aH);
  void BeginPath();
  void Fill(const CanvasWindingRule& aWinding);
  void Fill(const CanvasPath& aPath, const CanvasWindingRule& aWinding);
  void Stroke();
  void Stroke(const CanvasPath& aPath);
  void DrawFocusIfNeeded(mozilla::dom::Element& aElement, ErrorResult& aRv);
  bool DrawCustomFocusRing(mozilla::dom::Element& aElement);
  void Clip(const CanvasWindingRule& aWinding);
  void Clip(const CanvasPath& aPath, const CanvasWindingRule& aWinding);
  bool IsPointInPath(JSContext* aCx, double aX, double aY,
                     const CanvasWindingRule& aWinding,
                     nsIPrincipal& aSubjectPrincipal);
  bool IsPointInPath(JSContext* aCx, const CanvasPath& aPath, double aX,
                     double aY, const CanvasWindingRule& aWinding,
                     nsIPrincipal&);
  bool IsPointInStroke(JSContext* aCx, double aX, double aY,
                       nsIPrincipal& aSubjectPrincipal);
  bool IsPointInStroke(JSContext* aCx, const CanvasPath& aPath, double aX,
                       double aY, nsIPrincipal&);
  void FillText(const nsAString& aText, double aX, double aY,
                const Optional<double>& aMaxWidth,
                mozilla::ErrorResult& aError);
  void StrokeText(const nsAString& aText, double aX, double aY,
                  const Optional<double>& aMaxWidth,
                  mozilla::ErrorResult& aError);
  UniquePtr<TextMetrics> MeasureText(const nsAString& aRawText,
                                     mozilla::ErrorResult& aError);

  void DrawImage(const CanvasImageSource& aImage, double aDx, double aDy,
                 mozilla::ErrorResult& aError) {
    DrawImage(aImage, 0.0, 0.0, 0.0, 0.0, aDx, aDy, 0.0, 0.0, 0, aError);
  }

  void DrawImage(const CanvasImageSource& aImage, double aDx, double aDy,
                 double aDw, double aDh, mozilla::ErrorResult& aError) {
    DrawImage(aImage, 0.0, 0.0, 0.0, 0.0, aDx, aDy, aDw, aDh, 2, aError);
  }

  void DrawImage(const CanvasImageSource& aImage, double aSx, double aSy,
                 double aSw, double aSh, double aDx, double aDy, double aDw,
                 double aDh, mozilla::ErrorResult& aError) {
    DrawImage(aImage, aSx, aSy, aSw, aSh, aDx, aDy, aDw, aDh, 6, aError);
  }

  already_AddRefed<ImageData> CreateImageData(
      JSContext*, int32_t aSw, int32_t aSh, const ImageDataSettings& aSettings,
      ErrorResult&);
  already_AddRefed<ImageData> CreateImageData(JSContext*, ImageData&,
                                              ErrorResult&);
  already_AddRefed<ImageData> GetImageData(JSContext*, int32_t aSx, int32_t aSy,
                                           int32_t aSw, int32_t aSh,
                                           const ImageDataSettings& aSettings,
                                           nsIPrincipal& aSubjectPrincipal,
                                           ErrorResult&);
  void PutImageData(ImageData&, int32_t aDx, int32_t aDy, ErrorResult&);
  void PutImageData(ImageData&, int32_t aDx, int32_t aDy, int32_t aDirtyX,
                    int32_t aDirtyY, int32_t aDirtyWidth, int32_t aDirtyHeight,
                    ErrorResult&);

  double LineWidth() { return CurrentState().lineWidth; }

  void SetLineWidth(double aWidth) {
    if (aWidth > 0.0) {
      CurrentState().lineWidth = ToFloat(aWidth);
    }
  }

  CanvasLineCap LineCap() { return CurrentState().lineCap; }
  void SetLineCap(const CanvasLineCap& aLinecapStyle) {
    CurrentState().lineCap = aLinecapStyle;
  }

  CanvasLineJoin LineJoin() { return CurrentState().lineJoin; }
  void SetLineJoin(const CanvasLineJoin& aLinejoinStyle) {
    CurrentState().lineJoin = aLinejoinStyle;
  }

  double MiterLimit() { return CurrentState().miterLimit; }

  void SetMiterLimit(double aMiter) {
    if (aMiter > 0.0) {
      CurrentState().miterLimit = ToFloat(aMiter);
    }
  }

  void GetFont(nsACString& aFont) { aFont = GetFont(); }

  void SetFont(const nsACString& aFont, ErrorResult& aError);

  CanvasTextAlign TextAlign() { return CurrentState().textAlign; }
  void SetTextAlign(const CanvasTextAlign& aTextAlign) {
    CurrentState().textAlign = aTextAlign;
  }

  CanvasTextBaseline TextBaseline() { return CurrentState().textBaseline; }
  void SetTextBaseline(const CanvasTextBaseline& aTextBaseline) {
    CurrentState().textBaseline = aTextBaseline;
  }

  CanvasDirection Direction() { return CurrentState().textDirection; }
  void SetDirection(const CanvasDirection& aDirection) {
    CurrentState().textDirection = aDirection;
  }

  CanvasFontKerning FontKerning() { return CurrentState().fontKerning; }
  void SetFontKerning(const CanvasFontKerning& aFontKerning) {
    if (CurrentState().fontKerning != aFontKerning) {
      CurrentState().fontKerning = aFontKerning;
      CurrentState().fontGroup = nullptr;
    }
  }

  CanvasFontStretch FontStretch() { return CurrentState().fontWidth; }
  void SetFontStretch(const CanvasFontStretch& aFontWidth) {
    if (CurrentState().fontWidth != aFontWidth) {
      CurrentState().fontWidth = aFontWidth;
      CurrentState().fontGroup = nullptr;
    }
  }

  CanvasFontVariantCaps FontVariantCaps() {
    return CurrentState().fontVariantCaps;
  }
  void SetFontVariantCaps(const CanvasFontVariantCaps& aFontVariantCaps) {
    if (CurrentState().fontVariantCaps != aFontVariantCaps) {
      CurrentState().fontVariantCaps = aFontVariantCaps;
      CurrentState().fontGroup = nullptr;
    }
  }

  CanvasTextRendering TextRendering() { return CurrentState().textRendering; }
  void SetTextRendering(const CanvasTextRendering& aTextRendering) {
    CurrentState().textRendering = aTextRendering;
  }

  void GetLang(nsAString& aLang) { CurrentState().lang->ToString(aLang); }
  void SetLang(const nsAString& aLang) {
    if (!CurrentState().lang->Equals(aLang)) {
      CurrentState().lang = NS_Atomize(aLang);
      CurrentState().fontGroup = nullptr;
    }
  }

  void GetLetterSpacing(nsACString& aLetterSpacing);
  void SetLetterSpacing(const nsACString& aLetterSpacing);
  void GetWordSpacing(nsACString& aWordSpacing);
  void SetWordSpacing(const nsACString& aWordSpacing);

  void EnsureCapped() {
    if (mPathPruned) {
      mPathBuilder->LineTo(mPathBuilder->CurrentPoint());
      mPathPruned = false;
    }
  }

  void EnsureActivePath() {
    if (mPathPruned && !mPathBuilder->IsActive()) {
      mPathBuilder->MoveTo(mPathBuilder->CurrentPoint());
      mPathPruned = false;
    }
  }

  void ClosePath() {
    if (!EnsureWritablePath()) {
      return;
    }

    mPathBuilder->Close();
    mPathPruned = false;
  }

  void MoveTo(double aX, double aY) {
    if (!EnsureWritablePath()) {
      return;
    }

    mozilla::gfx::Point pos(ToFloat(aX), ToFloat(aY));
    if (!pos.IsFinite()) {
      return;
    }

    EnsureCapped();
    mPathBuilder->MoveTo(pos);
  }

  void LineTo(double aX, double aY) {
    if (!EnsureWritablePath()) {
      return;
    }

    LineTo(mozilla::gfx::Point(ToFloat(aX), ToFloat(aY)));
  }

  void QuadraticCurveTo(double aCpx, double aCpy, double aX, double aY) {
    if (!EnsureWritablePath()) {
      return;
    }

    mozilla::gfx::Point cp1(ToFloat(aCpx), ToFloat(aCpy));
    mozilla::gfx::Point cp2(ToFloat(aX), ToFloat(aY));
    if (!cp1.IsFinite() || !cp2.IsFinite()) {
      return;
    }

    if (cp1 == mPathBuilder->CurrentPoint() && cp1 == cp2) {
      mPathPruned = true;
      return;
    }
    EnsureActivePath();
    mPathBuilder->QuadraticBezierTo(cp1, cp2);
    mPathPruned = false;
  }

  void BezierCurveTo(double aCp1x, double aCp1y, double aCp2x, double aCp2y,
                     double aX, double aY) {
    if (!EnsureWritablePath()) {
      return;
    }

    BezierTo(mozilla::gfx::Point(ToFloat(aCp1x), ToFloat(aCp1y)),
             mozilla::gfx::Point(ToFloat(aCp2x), ToFloat(aCp2y)),
             mozilla::gfx::Point(ToFloat(aX), ToFloat(aY)));
  }

  void ArcTo(double aX1, double aY1, double aX2, double aY2, double aRadius,
             mozilla::ErrorResult& aError);
  void Rect(double aX, double aY, double aW, double aH);
  void RoundRect(
      double aX, double aY, double aW, double aH,
      const UnrestrictedDoubleOrDOMPointInitOrUnrestrictedDoubleOrDOMPointInitSequence&
          aRadii,
      ErrorResult& aError);
  void Arc(double aX, double aY, double aRadius, double aStartAngle,
           double aEndAngle, bool aAnticlockwise, mozilla::ErrorResult& aError);
  void Ellipse(double aX, double aY, double aRadiusX, double aRadiusY,
               double aRotation, double aStartAngle, double aEndAngle,
               bool aAnticlockwise, ErrorResult& aError);

  void GetFillRule(nsAString& aFillRule);
  void SetFillRule(const nsAString& aFillRule);

  void SetLineDash(const Sequence<double>& aSegments,
                   mozilla::ErrorResult& aRv);
  void GetLineDash(nsTArray<double>& aSegments) const;

  void SetLineDashOffset(double aOffset);
  double LineDashOffset() const;

  bool ImageSmoothingEnabled() { return CurrentState().imageSmoothingEnabled; }

  void SetImageSmoothingEnabled(bool aImageSmoothingEnabled) {
    if (aImageSmoothingEnabled != CurrentState().imageSmoothingEnabled) {
      CurrentState().imageSmoothingEnabled = aImageSmoothingEnabled;
    }
  }

  CanvasContextProperties ContextProperties() const {
    return mContextProperties;
  }

  void SetContextProperties(const CanvasContextProperties& aValue) {
    mContextProperties = aValue;
  }

  void DrawWindow(nsGlobalWindowInner& aWindow, double aX, double aY, double aW,
                  double aH, const nsACString& aBgColor, uint32_t aFlags,
                  nsIPrincipal& aSubjectPrincipal,
                  mozilla::ErrorResult& aError);

  // Eventually this should be deprecated. Keeping for now to keep the binding
  // functional.
  void Demote();

  nsresult Redraw();

  gfx::IntSize GetSize() const { return gfx::IntSize(mWidth, mHeight); }
  int32_t GetWidth() override { return GetSize().width; }
  int32_t GetHeight() override { return GetSize().height; }

  PredefinedColorSpace GetColorSpace() const { return mColorSpace; }

  // nsICanvasRenderingContextInternal
  /**
   * Gets the pres shell from either the canvas element or the doc shell
   */
  PresShell* GetPresShell() final;
  nsresult Initialize() override;
  NS_IMETHOD SetDimensions(int32_t aWidth, int32_t aHeight) override;
  NS_IMETHOD InitializeWithDrawTarget(
      nsIDocShell* aShell, NotNull<gfx::DrawTarget*> aTarget) override;

  NS_IMETHOD GetInputStream(
      const char* aMimeType, const nsAString& aEncoderOptions,
      mozilla::CanvasUtils::ImageExtraction aExtractionBehavior,
      const nsACString& aRandomizationKey, nsIInputStream** aStream) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetOptimizedSnapshot(
      mozilla::gfx::DrawTarget* aTarget, gfxAlphaType* aOutAlphaType) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* aOutAlphaType = nullptr) override {
    return GetOptimizedSnapshot(nullptr, aOutAlphaType);
  }

  virtual void SetOpaqueValueFromOpaqueAttr(bool aOpaqueAttrValue) override;
  bool GetIsOpaque() override { return mOpaque; }
  void ResetBitmap(bool aFreeBuffer);
  void ResetBitmap() override { ResetBitmap(true); }

  bool UpdateWebRenderCanvasData(nsDisplayListBuilder* aBuilder,
                                 WebRenderCanvasData* aCanvasData) override;

  bool InitializeCanvasRenderer(nsDisplayListBuilder* aBuilder,
                                CanvasRenderer* aRenderer) override;
  void MarkContextClean() override;
  void MarkContextCleanForFrameCapture() override {
    mFrameCaptureState = FrameCaptureState::CLEAN;
  }
  Watchable<FrameCaptureState>* GetFrameCaptureState() override {
    return &mFrameCaptureState;
  }
  // this rect is in canvas device space
  void Redraw(const mozilla::gfx::Rect& aR);
  NS_IMETHOD Redraw(const gfxRect& aR) override {
    Redraw(ToRect(aR));
    return NS_OK;
  }
  NS_IMETHOD SetContextOptions(
      const CanvasRenderingContextOptions& aOptions) override;

  /**
   * An abstract base class to be implemented by callers wanting to be notified
   * that a refresh has occurred. Callers must ensure an observer is removed
   * before it is destroyed.
   */
  virtual void DidRefresh() override;

  // this rect is in mTarget's current user space
  void RedrawUser(const gfxRect& aR);

  // nsISupports interface + CC
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_WRAPPERCACHE_CLASS(
      CanvasRenderingContext2D)

  enum class CanvasMultiGetterType : uint8_t {
    STRING = 0,
    PATTERN = 1,
    GRADIENT = 2
  };

  enum class Style : uint8_t { STROKE = 0, FILL, MAX };

  void LineTo(const mozilla::gfx::Point& aPoint) {
    mFeatureUsage |= CanvasFeatureUsage::LineTo;

    if (!aPoint.IsFinite()) {
      return;
    }
    if (mPathBuilder->CurrentPoint() == aPoint) {
      mPathPruned = true;
      return;
    }
    EnsureActivePath();
    mPathBuilder->LineTo(aPoint);
    mPathPruned = false;
  }

  void BezierTo(const mozilla::gfx::Point& aCP1,
                const mozilla::gfx::Point& aCP2,
                const mozilla::gfx::Point& aCP3) {
    if (!aCP1.IsFinite() || !aCP2.IsFinite() || !aCP3.IsFinite()) {
      return;
    }

    if (aCP1 == mPathBuilder->CurrentPoint() && aCP1 == aCP2 && aCP1 == aCP3) {
      mPathPruned = true;
      return;
    }
    EnsureActivePath();
    mPathBuilder->BezierTo(aCP1, aCP2, aCP3);
    mPathPruned = false;
  }

  virtual UniquePtr<uint8_t[]> GetImageBuffer(
      mozilla::CanvasUtils::ImageExtraction aExtractionBehavior,
      int32_t* out_format, gfx::IntSize* out_imageSize) override;

  void OnShutdown() override;

  bool IsContextLost() const { return mIsContextLost; }
  void OnRemoteCanvasLost() override;
  void OnRemoteCanvasRestored() override;
  void OnRemoteCanvasReset(
      const nsTArray<layers::RemoteTextureOwnerId>& aOwnerIds) override;

  /**
   * Update CurrentState().filter with the filter description for
   * CurrentState().filterChain.
   * Flushes the PresShell if aFlushIsNeeded is true, so the world can change
   * if you call this function.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void UpdateFilter(bool aFlushIfNeeded);

  CanvasFeatureUsage FeatureUsage() const { return mFeatureUsage; }

 protected:
  /**
   * Helper to parse a value for the letterSpacing or wordSpacing attribute.
   * If the string can be parsed, returns Some(value) and sets aNormalized to
   * the normalized form of the specified string. If it cannot be parsed as a
   * spacing value, returns Nothing, and aNormalized is untouched.
   * Note that ParseSpacing may flush style (to resolve font-relative units).
   */
  mozilla::Maybe<float> ParseSpacing(const nsACString& aSpacing,
                                     nsACString& aNormalized);

  already_AddRefed<const ComputedStyle> ResolveStyleForProperty(
      NonCustomCSSPropertyId aProperty, const nsACString& aValue);

  nsresult GetImageDataArray(JSContext* aCx, int32_t aX, int32_t aY,
                             uint32_t aWidth, uint32_t aHeight,
                             PredefinedColorSpace aColorSpace,
                             nsIPrincipal& aSubjectPrincipal,
                             JSObject** aRetval);

  void PutImageData_explicit(int32_t aX, int32_t aY, ImageData&,
                             bool aHasDirtyRect, int32_t aDirtyX,
                             int32_t aDirtyY, int32_t aDirtyWidth,
                             int32_t aDirtyHeight,
                             PredefinedColorSpace aColorSpace, ErrorResult&);

  bool CopyBufferProvider(layers::PersistentBufferProvider& aOld,
                          gfx::DrawTarget& aTarget, gfx::IntRect aCopyRect);

  /**
   * Internal method to complete initialisation, expects mTarget to have been
   * set
   */
  nsresult Initialize(int32_t aWidth, int32_t aHeight);

  nsresult InitializeWithTarget(mozilla::gfx::DrawTarget* aSurface,
                                int32_t aWidth, int32_t aHeight);

  /**
   * The number of living nsCanvasRenderingContexts.  When this goes down to
   * 0, we free the premultiply and unpremultiply tables, if they exist.
   */
  static MOZ_THREAD_LOCAL(uintptr_t) sNumLivingContexts;

  static MOZ_THREAD_LOCAL(mozilla::gfx::DrawTarget*) sErrorTarget;

  void SetTransformInternal(const mozilla::gfx::Matrix& aTransform);

  // Some helpers.  Doesn't modify a color on failure.
  void SetStyleFromUnion(
      const UTF8StringOrCanvasGradientOrCanvasPattern& aValue,
      Style aWhichStyle);
  void SetStyleFromString(const nsACString& aStr, Style aWhichStyle);

  void SetStyleFromGradient(CanvasGradient& aGradient, Style aWhichStyle) {
    CurrentState().SetGradientStyle(aWhichStyle, &aGradient);
  }

  void SetStyleFromPattern(CanvasPattern& aPattern, Style aWhichStyle) {
    CurrentState().SetPatternStyle(aWhichStyle, &aPattern);
  }

  void GetStyleAsUnion(OwningUTF8StringOrCanvasGradientOrCanvasPattern& aValue,
                       Style aWhichStyle);

  static void StyleColorToString(const nscolor& aColor, nsACString& aStr);

  // Returns whether a filter was successfully parsed.
  bool ParseFilter(const nsACString& aString,
                   StyleOwnedSlice<StyleFilter>& aFilterChain,
                   ErrorResult& aError);

  // Returns whether the font was successfully updated.
  bool SetFontInternal(const nsACString& aFont, mozilla::ErrorResult& aError);

  // Can we skip parsing and resolving the font, because it is the same as last
  // time and no other relevant attributes have changed?
  bool FontIsUnchanged(const nsACString& aFont, gfxUserFontSet* aFontSet);

  // Helper for SetFontInternal in the case where we have no PresShell.
  bool SetFontInternalDisconnected(const nsACString& aFont,
                                   mozilla::ErrorResult& aError);

  // Update the resolved values for letterSpacing and wordSpacing, if present,
  // following a potential change to font-relative dimensions.
  void UpdateSpacing();

  // Clears the target and updates mOpaque based on mOpaqueAttrValue and
  // mContextAttributesHasAlpha.
  void UpdateIsOpaque();

  // Shared implementation for Stroke() and Stroke(CanvasPath) methods.
  void StrokeImpl(const mozilla::gfx::Path& aPath);

  // Shared implementation for Fill() methods.
  void FillImpl(const mozilla::gfx::Path& aPath);

  /**
   * Creates the error target, if it doesn't exist
   */
  static void EnsureErrorTarget();

  /* This function ensures there is a writable pathbuilder available
   */
  bool EnsureWritablePath();

  // Ensures there is a valid BufferProvider for creating a PathBuilder.
  bool EnsureBufferProvider();

  // Ensures a path in UserSpace is available.
  void EnsureUserSpacePath(
      const CanvasWindingRule& aWinding = CanvasWindingRule::Nonzero);

  // Ensures both target and a path in UserSpace is available.
  void EnsureTargetAndUserSpacePath(
      const CanvasWindingRule& aWinding = CanvasWindingRule::Nonzero) {
    EnsureTarget();
    EnsureUserSpacePath(aWinding);
  }

  /**
   * Needs to be called before updating the transform. This makes a call to
   * EnsureTarget() so you don't have to.
   */
  void TransformCurrentPath(const mozilla::gfx::Matrix& aTransform);

  /**
   * Check if the target is in an error state. Functions that may need to
   * access the transform or clip state with or without a target should call
   * this first to verify that it's okay to access this state, as error targets
   * should ignore it instead.
   */
  bool HasErrorState(ErrorResult& aError, bool aInitProvider = true);
  bool HasErrorState() {
    IgnoredErrorResult error;
    return HasErrorState(error);
  }

  /**
   * Create the backing surfacing, if it doesn't exist. If there is an error
   * in creating the target then it will put sErrorTarget in place. If there
   * is in turn an error in creating the sErrorTarget then they would both
   * be null so IsTargetValid() would still return null.
   *
   * Returns true on success.
   */
  bool EnsureTarget(ErrorResult& aError,
                    const gfx::Rect* aCoveredRect = nullptr,
                    bool aWillClear = false, bool aSkipTransform = false,
                    bool aPreferAcceleration = false);

  bool EnsureTarget(const gfx::Rect* aCoveredRect = nullptr,
                    bool aWillClear = false, bool aSkipTransform = false) {
    IgnoredErrorResult error;
    return EnsureTarget(error, aCoveredRect, aWillClear, aSkipTransform);
  }

  // Attempt to borrow a new target from an existing buffer provider.
  bool BorrowTarget(const gfx::IntRect& aPersistedRect, bool aNeedsClear);

  void RestoreClipsAndTransformToTarget();

  // aPreferAcceleration allows an accelerated target below the usual minimum
  // size for acceleration.
  bool TryAcceleratedTarget(
      RefPtr<gfx::DrawTarget>& aOutDT,
      RefPtr<layers::PersistentBufferProvider>& aOutProvider,
      bool aPreferAcceleration);

  bool HasAcceleratedBufferProvider() const;

  // Before the first drawing operation after a reset, returns whether the next
  // EnsureTarget should create an accelerated target regardless of the canvas
  // size, because aImage is an accelerated 2D canvas. Drawing it into a
  // software target would require a synchronous readback.
  bool MaybeAccelerateForDrawImage(const CanvasImageSource& aImage);

  bool TrySharedTarget(RefPtr<gfx::DrawTarget>& aOutDT,
                       RefPtr<layers::PersistentBufferProvider>& aOutProvider);

  bool TryBasicTarget(RefPtr<gfx::DrawTarget>& aOutDT,
                      RefPtr<layers::PersistentBufferProvider>& aOutProvider,
                      ErrorResult& aError);

  void RegisterAllocation();

  void SetInitialState();

  void SetErrorState();

  /**
   * This method is run at the end of the event-loop spin where
   * ScheduleStableStateCallback was called.
   *
   * We use it to unlock resources that need to be locked while drawing.
   */
  void OnStableState();

  /**
   * Cf. OnStableState.
   */
  void ScheduleStableStateCallback();

  /**
   * Disposes an old target and prepares to lazily create a new target.
   *
   * Parameters are the new dimensions to be used, or if either is negative,
   * existing dimensions will be left unchanged.
   */
  void ClearTarget(int32_t aWidth = -1, int32_t aHeight = -1);

  /*
   * Returns the target to the buffer provider. i.e. this will queue a frame for
   * rendering.
   */
  void ReturnTarget(bool aForceReset = false);

  /**
   * Check if the target is valid after calling EnsureTarget.
   */
  bool IsTargetValid() const {
    return !!mTarget && mTarget != sErrorTarget.get() && !mIsContextLost;
  }

  /**
   * Returns the surface format this canvas should be allocated using. Takes
   * into account mOpaque, platform requirements, etc.
   */
  mozilla::gfx::SurfaceFormat GetSurfaceFormat() const;

  /**
   * Returns true if we know for sure that the pattern for a given style is
   * opaque. Usefull to know if we can discard the content below in certain
   * situations. Optionally checks if the pattern is a color pattern.
   */
  bool PatternIsOpaque(Style aStyle, bool* aIsColor = nullptr) const;

  SurfaceFromElementResult CachedSurfaceFromElement(Element* aElement);

  void DrawImage(const CanvasImageSource& aImgElt, double aSx, double aSy,
                 double aSw, double aSh, double aDx, double aDy, double aDw,
                 double aDh, uint8_t aOptional_argc,
                 mozilla::ErrorResult& aError);

  void DrawDirectlyToCanvas(const DirectDrawInfo& aImage,
                            mozilla::gfx::Rect* aBounds,
                            mozilla::gfx::Rect aDest, mozilla::gfx::Rect aSrc,
                            gfx::IntSize aImgSize);

  nsCString& GetFont() {
    // will initialize the value if not set, else does nothing
    GetCurrentFontStyle();

    return CurrentState().resolvedFont;
  }

  bool UseSoftwareRendering() const;

  // Member vars
  int32_t mWidth, mHeight;

  // Determines the context colorspace
  PredefinedColorSpace mColorSpace;

  // This is true when the canvas is valid, but of zero size, this requires
  // specific behavior on some operations.
  bool mZero;

  // The two ways to set the opaqueness of the canvas.
  // mOpaqueAttrValue: Whether the <canvas> element has the moz-opaque attribute
  // set. Can change during the lifetime of the context. Non-standard, should
  // hopefully go away soon.
  // mContextAttributesHasAlpha: The standard way of setting canvas opaqueness.
  // Set at context initialization time and never changes.
  bool mOpaqueAttrValue;
  bool mContextAttributesHasAlpha;

  // Determines the context's opaqueness. Is computed from mOpaqueAttrValue and
  // mContextAttributesHasAlpha in UpdateIsOpaque().
  bool mOpaque;

  // This is true when the next time our layer is retrieved we need to
  // recreate it (i.e. our backing surface changed)
  bool mResetLayer;
  // This is needed for drawing in drawAsyncXULElement
  bool mIPC;

  bool mHasPendingStableStateCallback;

  // If mCanvasElement is not provided, then a docshell is
  nsCOMPtr<nsIDocShell> mDocShell;

  // This is created lazily so it is necessary to call EnsureTarget before
  // accessing it. In the event of an error it will be equal to
  // sErrorTarget.
  RefPtr<mozilla::gfx::DrawTarget> mTarget;

  RefPtr<mozilla::layers::PersistentBufferProvider> mBufferProvider;
  // Whether the target's buffer must be cleared before drawing.
  bool mBufferNeedsClear = false;
  // Whether the current state contains clips or transforms not set on target.
  bool mTargetNeedsClipsAndTransforms = false;

  // Whether we should try to create an accelerated buffer provider.
  bool mAllowAcceleration = true;
  // Whether the application expects to use operations that perform poorly with
  // acceleration.
  bool mWillReadFrequently = false;
  // Whether to force software rendering
  bool mForceSoftwareRendering = false;
  // Whether or not we have already shutdown.
  bool mHasShutdown = false;
  // Whether or not remote canvas is currently unavailable.
  bool mIsContextLost = false;
  // Whether or not we can restore the context after restoration.
  bool mAllowContextRestore = true;
  // Which context properties apply to an SVG when calling drawImage.
  CanvasContextProperties mContextProperties = CanvasContextProperties::None;

  bool AlreadyShutDown() const { return mHasShutdown; }

  /**
   * Flag to avoid duplicate calls to InvalidateFrame. Set to true whenever
   * Redraw is called, reset to false when Render is called.
   */
  bool mIsEntireFrameInvalid;
  /**
   * When this is set, the first call to Redraw(gfxRect) should set
   * mIsEntireFrameInvalid since we expect it will be followed by
   * many more Redraw calls.
   */
  bool mPredictManyRedrawCalls;

  /**
   * Flag to avoid unnecessary surface copies to FrameCaptureListeners in the
   * case when the canvas is not currently being drawn into and not rendered
   * but canvas capturing is still ongoing.
   */
  Watchable<FrameCaptureState> mFrameCaptureState;

  /**
   * We also have a device space pathbuilder. The reason for this is as
   * follows, when a path is being built, but the transform changes, we
   * can no longer keep a single path in userspace, considering there's
   * several 'user spaces' now. We therefore transform the current path
   * into device space, and add all operations to this path in device
   * space.
   *
   * When then finally executing a render, the Azure drawing API expects
   * the path to be in userspace. We could then set an identity transform
   * on the DrawTarget and do all drawing in device space. This is
   * undesirable because it requires transforming patterns, gradients,
   * clips, etc. into device space and it would not work for stroking.
   * What we do instead is convert the path back to user space when it is
   * drawn, and draw it with the current transform. This makes all drawing
   * occur correctly.
   *
   * There's never both a device space path builder and a user space path
   * builder present at the same time. There is also never a path and a
   * path builder present at the same time. When writing proceeds on an
   * existing path the Path is cleared and a new builder is created.
   *
   * mPath is always in user-space.
   */
  RefPtr<mozilla::gfx::Path> mPath;
  RefPtr<mozilla::gfx::PathBuilder> mPathBuilder;
  RefPtr<mozilla::gfx::PathBuilder> mRecycledPathBuilder;
  mozilla::gfx::BackendType mPathType = mozilla::gfx::BackendType::NONE;
  bool mPathPruned = false;
  mozilla::gfx::Matrix mPathTransform;
  bool mPathTransformDirty = false;

  void FlushPathTransform();
  already_AddRefed<mozilla::gfx::PathBuilder> CreateOrRecyclePathBuilder(
      mozilla::gfx::FillRule aFillRule);

  /**
   * Number of times we've invalidated before calling redraw
   */
  uint32_t mInvalidateCount;
  static const uint32_t kCanvasMaxInvalidateCount = 100;

  mozilla::intl::Bidi mBidiEngine;

  /**
   * Returns true if a shadow should be drawn along with a
   * drawing operation.
   */
  bool NeedToDrawShadow() {
    const ContextState& state = CurrentState();

    // The spec says we should not draw shadows if the operator is OVER.
    // If it's over and the alpha value is zero, nothing needs to be drawn.
    return NS_GET_A(state.shadowColor) != 0 &&
           (state.shadowBlur != 0.f || state.shadowOffset.x != 0.f ||
            state.shadowOffset.y != 0.f);
  }

  /**
   * Returns true if the result of a drawing operation should be
   * drawn with a filter.
   */
  bool NeedToApplyFilter() {
    // Avoid trying to update filter state if no filter chain has ever been set.
    if (CurrentState().filterChain.IsEmpty()) {
      return false;
    }
    return EnsureUpdatedFilter().mPrimitives.Length() > 0;
  }

  /**
   * Calls UpdateFilter if the filter description hasn't been built for the
   * current filter chain yet, or if the canvas's WriteOnly state has changed
   * between the last call to UpdateFilter and now.
   */
  const gfx::FilterDescription& EnsureUpdatedFilter() {
    bool isWriteOnly = IsWriteOnly() ||
                       (mCanvasElement && mCanvasElement->IsWriteOnly()) ||
                       (mOffscreenCanvas && mOffscreenCanvas->IsWriteOnly());
    if (!CurrentState().filterDescriptionUpToDate ||
        CurrentState().filterSourceGraphicTainted != isWriteOnly) {
      // Do not flush here: this runs inside drawing operations that hold raw
      // references to mPath/state, and a flush can run script that resets the
      // context, leading to UAF. Flush already happened at SetFilter() time.
      UpdateFilter(/* aFlushIfNeeded = */ false);
    }
    MOZ_ASSERT(CurrentState().filterSourceGraphicTainted == isWriteOnly);
    return CurrentState().filter;
  }

  bool NeedToCalculateBounds() {
    return NeedToDrawShadow() || NeedToApplyFilter();
  }

  // text

  // Resolve the `lang` property if it is `inherit` or empty, returning true
  // if the resolved value has changed.
  bool ResolveFontLang();

 public:
  gfxFontGroup* GetCurrentFontStyle();

 protected:
  enum class TextDrawOperation : uint8_t { FILL, STROKE, MEASURE };

  /**
   * Implementation of the fillText, strokeText, and measure functions with
   * the operation abstracted to a flag.
   * Returns a TextMetrics object _only_ if the operation is measure;
   * drawing operations (fill or stroke) always return nullptr.
   */
  UniquePtr<TextMetrics> DrawOrMeasureText(const nsAString& aText, float aX,
                                           float aY,
                                           const Optional<double>& aMaxWidth,
                                           TextDrawOperation aOp,
                                           ErrorResult& aError);

  // A clip or a transform, recorded and restored in order.
  struct ClipState {
    explicit ClipState(mozilla::gfx::Path* aClip) : clip(aClip) {}

    explicit ClipState(const mozilla::gfx::Matrix& aTransform)
        : transform(aTransform) {}

    bool IsClip() const { return !!clip; }

    RefPtr<mozilla::gfx::Path> clip;
    mozilla::gfx::Matrix transform;
  };

  // state stack handling
  class ContextState {
   public:
    ContextState() = default;
    ContextState(const ContextState& aOther);

    ~ContextState();

    void SetColorStyle(Style aWhichStyle, nscolor aColor);
    void SetPatternStyle(Style aWhichStyle, CanvasPattern* aPat);
    void SetGradientStyle(Style aWhichStyle, CanvasGradient* aGrad);

    /**
     * returns true iff the given style is a solid color.
     */
    bool StyleIsColor(Style aWhichStyle) const {
      return !(patternStyles[aWhichStyle] || gradientStyles[aWhichStyle]);
    }

    int32_t ShadowBlurRadius() const {
      static const gfxFloat GAUSSIAN_SCALE_FACTOR =
          (3 * sqrt(2 * std::numbers::pi) / 4) * 1.5;
      return (int32_t)floor(ShadowBlurSigma() * GAUSSIAN_SCALE_FACTOR + 0.5);
    }

    mozilla::gfx::Float ShadowBlurSigma() const {
      return std::min(SIGMA_MAX, shadowBlur / 2.0f);
    }

    ElementOrArray<ClipState> clipsAndTransforms;

    RefPtr<gfxFontGroup> fontGroup;
    nsFont fontFont;
    RefPtr<const ComputedStyle>
        fontComputedStyle;  // NOTE: Only non-null if connected.

    EnumeratedArray<Style, RefPtr<CanvasGradient>, size_t(Style::MAX)>
        gradientStyles;
    EnumeratedArray<Style, RefPtr<CanvasPattern>, size_t(Style::MAX)>
        patternStyles;
    EnumeratedArray<Style, nscolor, size_t(Style::MAX)> colorStyles;

    nsCString specifiedFont;  // `font` as specified by the caller
    nsCString resolvedFont;   // parsed & re-serialized value of `font`
    CanvasTextAlign textAlign = CanvasTextAlign::Start;
    CanvasTextBaseline textBaseline = CanvasTextBaseline::Alphabetic;
    CanvasDirection textDirection = CanvasDirection::Inherit;
    CanvasFontKerning fontKerning = CanvasFontKerning::Auto;
    CanvasFontStretch fontWidth = CanvasFontStretch::Normal;
    CanvasFontVariantCaps fontVariantCaps = CanvasFontVariantCaps::Normal;
    CanvasTextRendering textRendering = CanvasTextRendering::Auto;

    gfx::Float letterSpacing = 0.0f;
    gfx::Float wordSpacing = 0.0f;
    nsCString letterSpacingStr;
    nsCString wordSpacingStr;
    RefPtr<nsAtom> lang = nsGkAtoms::inherit;
    RefPtr<nsAtom> resolvedFontLang;

    nscolor shadowColor = 0;

    mozilla::gfx::Matrix transform;
    mozilla::gfx::Point shadowOffset;
    mozilla::gfx::Float lineWidth = 1.0f;
    mozilla::gfx::Float miterLimit = 10.0f;
    mozilla::gfx::Float globalAlpha = 1.0f;
    mozilla::gfx::Float shadowBlur = 0.0f;

    nsTArray<mozilla::gfx::Float> dash;
    mozilla::gfx::Float dashOffset = 0.0f;

    mozilla::gfx::CompositionOp op = mozilla::gfx::CompositionOp::OP_OVER;
    mozilla::gfx::FillRule fillRule = mozilla::gfx::FillRule::FILL_WINDING;
    CanvasLineCap lineCap = CanvasLineCap::Butt;
    CanvasLineJoin lineJoin = CanvasLineJoin::Miter;

    nsCString filterString{"none"};
    StyleOwnedSlice<StyleFilter> filterChain;
    // RAII object that we obtain when we start to observer SVG filter elements
    // for rendering changes.  When released we stop observing the SVG elements.
    nsCOMPtr<ISVGFilterObserverList> autoSVGFiltersObserver;
    mozilla::gfx::FilterDescription filter;
    nsTArray<RefPtr<mozilla::gfx::SourceSurface>> filterAdditionalImages;

    // This keeps track of whether the canvas was "tainted" or not when
    // we last used a filter. This is a security measure, whereby the
    // canvas is flipped to write-only if a cross-origin image is drawn to it.
    // This is to stop bad actors from reading back data they shouldn't have
    // access to.
    //
    // This also limits what filters we can apply to the context; in particular
    // feDisplacementMap is restricted.
    //
    // We keep track of this to ensure that if this gets out of sync with the
    // tainted state of the canvas itself, we update our filters accordingly.
    bool filterSourceGraphicTainted = false;
    // Whether |filter| has been built for |filterChain| by UpdateFilter().
    bool filterDescriptionUpToDate = false;
    bool imageSmoothingEnabled = true;

    // Whether resolvedFontLang was an explicitly-specified lang or inferred.
    bool explicitLang = false;
  };

  AutoTArray<ContextState, 3> mStyleStack;

  inline ContextState& CurrentState() { return mStyleStack.LastElement(); }

  inline const ContextState& CurrentState() const {
    return mStyleStack.LastElement();
  }

  inline const ContextState& PreviousState() const {
    return mStyleStack[mStyleStack.Length() - 2];
  }

  struct FontStyleCacheKey {
    FontStyleCacheKey() = default;
    FontStyleCacheKey(const nsACString& aFont, nsAtom* aLang,
                      uint64_t aGeneration)
        : mFont(aFont), mLang(aLang), mGeneration(aGeneration) {}
    nsCString mFont;
    RefPtr<nsAtom> mLang;
    uint64_t mGeneration = 0;
  };

  struct FontStyleData {
    FontStyleCacheKey mKey;
    nsCString mUsedFont;
    RefPtr<const ComputedStyle> mStyle;
  };

  class FontStyleCache
      : public MruCache<FontStyleCacheKey, FontStyleData, FontStyleCache> {
   public:
    // A cached failure has a null mStyle, but every live entry has a lang.
    static bool IsEmpty(const FontStyleData& aVal) { return !aVal.mKey.mLang; }
    static HashNumber Hash(const FontStyleCacheKey& aKey) {
      HashNumber hash = HashString(aKey.mFont);
      hash = AddToHash(hash, aKey.mLang->hash());
      return AddToHash(hash, aKey.mGeneration);
    }
    static bool Match(const FontStyleCacheKey& aKey,
                      const FontStyleData& aVal) {
      return aVal.mKey.mGeneration == aKey.mGeneration &&
             aVal.mKey.mLang == aKey.mLang && aVal.mKey.mFont == aKey.mFont;
    }
  };

  mozilla::UniquePtr<FontStyleCache> mFontStyleCache;

  struct FontGroupCacheKey {
    FontGroupCacheKey() = default;
    FontGroupCacheKey(const nsACString& aStr, nsAtom* aLang,
                      CanvasFontStretch aStretch, CanvasFontVariantCaps aCaps,
                      CanvasFontKerning aKerning, uint64_t aGen)
        : mSpecifiedFont(aStr),
          mLang(aLang),
          mStretch(aStretch),
          mCaps(aCaps),
          mKerning(aKerning),
          mGeneration(aGen) {}

    nsCString mSpecifiedFont;
    RefPtr<nsAtom> mLang;
    CanvasFontStretch mStretch;
    CanvasFontVariantCaps mCaps;
    CanvasFontKerning mKerning;
    uint64_t mGeneration = 0;
  };

  struct FontGroupCacheData {
    FontGroupCacheData() = default;
    FontGroupCacheData(const FontGroupCacheKey& aKey,
                       RefPtr<gfxFontGroup> aFontGroup,
                       const nsACString& aResolvedFont, const nsFont& aFont)
        : mKey(aKey),
          mFontGroup(aFontGroup),
          mResolvedFont(aResolvedFont),
          mFont(aFont) {}

    FontGroupCacheKey mKey;
    RefPtr<gfxFontGroup> mFontGroup;
    nsCString mResolvedFont;
    nsFont mFont;
  };

  class FontGroupCache
      : public MruCache<FontGroupCacheKey, FontGroupCacheData, FontGroupCache> {
   public:
    static bool IsEmpty(const FontGroupCacheData& aVal) {
      return !aVal.mFontGroup;
    }
    static HashNumber Hash(const FontGroupCacheKey& aKey) {
      HashNumber hash = HashString(aKey.mSpecifiedFont);
      hash = AddToHash(hash, aKey.mLang->hash());
      hash = AddToHash(hash, aKey.mStretch);
      hash = AddToHash(hash, aKey.mCaps);
      hash = AddToHash(hash, aKey.mKerning);
      hash = AddToHash(hash, aKey.mGeneration);
      return hash;
    }
    static bool Match(const FontGroupCacheKey& aKey,
                      const FontGroupCacheData& aVal) {
      return aVal.mKey.mGeneration == aKey.mGeneration &&
             aVal.mKey.mSpecifiedFont == aKey.mSpecifiedFont &&
             aVal.mKey.mLang == aKey.mLang &&
             aVal.mKey.mStretch == aKey.mStretch &&
             aVal.mKey.mCaps == aKey.mCaps &&
             aVal.mKey.mKerning == aKey.mKerning;
    }
  };

  mozilla::UniquePtr<FontGroupCache> mFontGroupCache;

  const ComputedStyle* GetCurrentFontComputedStyle() {
    GetCurrentFontStyle();
    return CurrentState().fontComputedStyle;
  }

  struct ColorStyleCacheEntry {
    nsCString mKey;
    Maybe<nscolor> mColor;
    bool mWasCurrentColor = false;
  };
  class ColorStyleCache
      : public MruCache<nsACString, ColorStyleCacheEntry, ColorStyleCache> {
   public:
    static bool IsEmpty(const ColorStyleCacheEntry& aVal) {
      return aVal.mKey.IsEmpty();
    }
    static HashNumber Hash(const nsACString& aKey) { return HashString(aKey); }
    static bool Match(const nsACString& aKey,
                      const ColorStyleCacheEntry& aVal) {
      return aVal.mKey == aKey;
    }
  };
  ColorStyleCache mColorStyleCache;

  ColorStyleCacheEntry ParseColorSlow(const nsACString&);

  friend class CanvasGeneralPattern;
  friend class AdjustedTarget;
  friend class AdjustedTargetForShadow;
  friend class AdjustedTargetForFilter;

  // other helpers
  void GetAppUnitsValues(int32_t* aPerDevPixel, int32_t* aPerCSSPixel);

  friend struct CanvasBidiProcessor;
  friend class CanvasDrawObserver;
  friend class ImageBitmap;

  void SetWriteOnly();

  bool IsWriteOnly() const { return mWriteOnly; }

  bool mWriteOnly;

  uint8_t mFillTextCalls = 0;
  // Flags used by the fingerprinting detection heuristic
  CanvasFeatureUsage mFeatureUsage = CanvasFeatureUsage::None;

  virtual void AddZoneWaitingForGC();
  virtual void AddAssociatedMemory();
  virtual void RemoveAssociatedMemory();
};

size_t BindingJSObjectMallocBytes(CanvasRenderingContext2D* aContext);

}  // namespace dom
}  // namespace mozilla

#endif /* CanvasRenderingContext2D_h */
