/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a presentation of a document, part 1 */

#ifndef nsPresContext_h_
#define nsPresContext_h_

#include "FontVisibilityProvider.h"
#include "Units.h"
#include "gfxRect.h"
#include "gfxTypes.h"
#include "mozilla/AppUnits.h"
#include "mozilla/Attributes.h"
#include "mozilla/DepthOrderedFrameList.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/MediaEmulationData.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/NotNull.h"
#include "mozilla/PreferenceSheet.h"
#include "mozilla/PresShellForwards.h"
#include "mozilla/ScrollStyles.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/intl/Bidi.h"
#include "mozilla/widget/ThemeChangeKind.h"
#include "nsAtom.h"
#include "nsCOMPtr.h"
#include "nsChangeHint.h"
#include "nsColor.h"
#include "nsCompatibility.h"
#include "nsCoord.h"
#include "nsCycleCollectionParticipant.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsHashKeys.h"
#include "nsIWidgetListener.h"  // for nsSizeMode
#include "nsRect.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsTHashtable.h"
#include "nsThreadUtils.h"

class nsIPrintSettings;
class nsDocShell;
class nsIDocShell;
class nsITheme;
class nsITimer;
class nsIContent;
class nsIFrame;
class nsFrameManager;
class nsAtom;
class nsIRunnable;
class gfxFontFamily;
class gfxFontFeatureValueSet;
class gfxUserFontEntry;
class gfxUserFontSet;
class gfxTextPerfMetrics;
class nsCSSFontFeatureValuesRule;
class nsCSSFrameConstructor;
class nsFontCache;
class nsTransitionManager;
class nsAnimationManager;
class nsRefreshDriver;
class nsIWidget;
class nsDeviceContext;
class gfxMissingFontRecorder;

namespace mozilla {
class AnimationEventDispatcher;
class EffectCompositor;
class Encoding;
class EventStateManager;
class CounterStyleManager;
class ManagedPostRefreshObserver;
class PresShell;
class RestyleManager;
class ServoStyleSet;
class StaticPresData;
class TimelineManager;
struct MediaFeatureChange;
enum class MediaFeatureChangePropagation : uint8_t;
enum class ColorScheme : uint8_t;
enum class StyleForcedColors : uint8_t;
namespace layers {
class ContainerLayer;
class LayerManager;
}  // namespace layers
namespace dom {
class Document;
class Element;
class PerformanceMainThread;
enum class PrefersColorSchemeOverride : uint8_t;
}  // namespace dom
namespace gfx {
class FontPaletteValueSet;
}  // namespace gfx
}  // namespace mozilla

// IDs for the default variable and fixed fonts (not to be changed, see
// nsFont.h) To be used for Get/SetDefaultFont(). The other IDs in nsFont.h are
// also supported.
//
// kGenericFont_moz_variable
const uint8_t kPresContext_DefaultVariableFont_ID = 0x00;
// kGenericFont_moz_fixed
const uint8_t kPresContext_DefaultFixedFont_ID = 0x01;

#ifdef DEBUG
struct nsAutoLayoutPhase;

enum class nsLayoutPhase : uint8_t {
  Paint,
  DisplayListBuilding,  // sometimes a subset of the paint phase
  Reflow,
  FrameC,
  COUNT
};
#endif

class nsRootPresContext;

// An interface for presentation contexts. Presentation contexts are
// objects that provide an outer context for a presentation shell.

class nsPresContext : public nsISupports,
                      public mozilla::SupportsWeakPtr,
                      public FontVisibilityProvider {
 public:
  using Encoding = mozilla::Encoding;
  template <typename T>
  using NotNull = mozilla::NotNull<T>;
  template <typename T>
  using Maybe = mozilla::Maybe<T>;
  using MediaEmulationData = mozilla::MediaEmulationData;

  typedef mozilla::ScrollStyles ScrollStyles;
  using TransactionId = mozilla::layers::TransactionId;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(nsPresContext)

  FONT_VISIBILITY_PROVIDER_IMPL

  enum nsPresContextType : uint8_t {
    eContext_Galley,        // unpaginated screen presentation
    eContext_PrintPreview,  // paginated screen presentation
    eContext_Print,         // paginated printer presentation
    eContext_PageLayout     // paginated & editable.
  };

  nsPresContext(mozilla::dom::Document* aDocument, nsPresContextType aType);

  /**
   * Initialize the presentation context from a particular device.
   */
  void Init(nsDeviceContext*);

  /**
   * Initialize the font cache if it hasn't been initialized yet.
   * (Needed for stylo)
   */
  void InitFontCache();

  void UpdateFontCacheUserFonts(gfxUserFontSet* aUserFontSet);

  /**
   * Get the nsFontMetrics that describe the properties of
   * an nsFont.
   * @param aFont font description to obtain metrics for
   */
  already_AddRefed<nsFontMetrics> GetMetricsFor(
      const nsFont& aFont, const nsFontMetrics::Params& aParams);

  /**
   * Notification when a font metrics instance created for this context is
   * about to be deleted
   */
  nsresult FontMetricsDeleted(const nsFontMetrics* aFontMetrics);

  /**
   * Attempt to free up resources by flushing out any fonts no longer
   * referenced by anything other than the font cache itself.
   * @return error status
   */
  nsresult FlushFontCache();

  /**
   * Set and detach presentation shell that this context is bound to.
   * A presentation context may only be bound to a single shell.
   */
  void AttachPresShell(mozilla::PresShell* aPresShell);
  void DetachPresShell();

  nsPresContextType Type() const { return mType; }

  /**
   * Get the PresentationShell that this context is bound to.
   */
  mozilla::PresShell* PresShell() const {
    NS_ASSERTION(mPresShell, "Null pres shell");
    return mPresShell;
  }

  mozilla::PresShell* GetPresShell() const { return mPresShell; }

  void DocumentCharSetChanged(NotNull<const Encoding*> aCharSet);

  mozilla::dom::PerformanceMainThread* GetPerformanceMainThread() const;
  /**
   * Returns the parent prescontext for this one. Returns null if this is a
   * root.
   */
  nsPresContext* GetParentPresContext() const;

  /**
   * Returns the prescontext of the root content document in the same process
   * that contains this presentation, or null if there isn't one.
   */
  nsPresContext* GetInProcessRootContentDocumentPresContext();

  /**
   * Returns the nearest widget for the root frame or view of this.
   */
  nsIWidget* GetNearestWidget() const;

  /**
   * Returns the root widget for this.
   */
  nsIWidget* GetRootWidget() const;

  /**
   * Returns the widget which may have native focus and handles text input
   * like keyboard input, IME, etc.
   */
  nsIWidget* GetTextInputHandlingWidget() const {
    // Currently, root widget for each PresContext handles text input.
    return GetRootWidget();
  }

  /**
   * Return the presentation context for the root of the view manager
   * hierarchy that contains this presentation context, or nullptr if it can't
   * be found (e.g. it's detached).
   */
  nsRootPresContext* GetRootPresContext() const;

  virtual bool IsRoot() const { return false; }

  mozilla::dom::Document* Document() const {
#ifdef DEBUG
    ValidatePresShellAndDocumentReleation();
#endif  // #ifdef DEBUG
    return mDocument;
  }

  inline mozilla::ServoStyleSet* StyleSet() const;

  bool HasPendingMediaQueryUpdates() const {
    return !!mPendingMediaFeatureValuesChange;
  }

  inline nsCSSFrameConstructor* FrameConstructor() const;

  mozilla::AnimationEventDispatcher* AnimationEventDispatcher() {
    return mAnimationEventDispatcher;
  }

  mozilla::EffectCompositor* EffectCompositor() { return mEffectCompositor; }
  nsTransitionManager* TransitionManager() { return mTransitionManager.get(); }
  nsAnimationManager* AnimationManager() { return mAnimationManager.get(); }
  const nsAnimationManager* AnimationManager() const {
    return mAnimationManager.get();
  }
  mozilla::TimelineManager* TimelineManager() { return mTimelineManager.get(); }

  nsRefreshDriver* RefreshDriver() { return mRefreshDriver; }

  mozilla::RestyleManager* RestyleManager() {
    MOZ_ASSERT(mRestyleManager);
    return mRestyleManager.get();
  }

  mozilla::CounterStyleManager* CounterStyleManager() const {
    return mCounterStyleManager;
  }

  /**
   * Rebuilds all style data by throwing out the old rule tree and
   * building a new one, and additionally applying a change hint (which must not
   * contain nsChangeHint_ReconstructFrame) to the root frame.
   *
   * For the restyle hint argument, see RestyleManager::RebuildAllStyleData.
   * Also rebuild the user font set and counter style manager.
   *
   * FIXME(emilio): The name of this is an utter lie. We should probably call
   * this PostGlobalStyleChange or something, as it doesn't really rebuild
   * anything unless you tell it to via the change hint / restyle hint
   * machinery.
   */
  void RebuildAllStyleData(nsChangeHint, const mozilla::RestyleHint&);
  /**
   * Just like RebuildAllStyleData, except (1) asynchronous and (2) it
   * doesn't rebuild the user font set / counter-style manager / etc.
   */
  void PostRebuildAllStyleDataEvent(nsChangeHint, const mozilla::RestyleHint&);

  void ContentLanguageChanged();

  /** Returns whether any media query changed. */
  bool FlushPendingMediaFeatureValuesChanged();

  /**
   * Schedule a media feature change for this document, and potentially for
   * other subdocuments and images (depending on the arguments).
   */
  void MediaFeatureValuesChanged(const mozilla::MediaFeatureChange&,
                                 mozilla::MediaFeatureChangePropagation);

  /**
   * Updates the size mode on all remote children and recursively notifies this
   * document and all subdocuments (including remote children) that a media
   * feature value has changed.
   */
  void SizeModeChanged(nsSizeMode aSizeMode);

  /**
   * Access compatibility mode for this context.  This is the same as
   * our document's compatibility mode.
   */
  nsCompatibility CompatibilityMode() const;

  /**
   * Access the image animation mode for this context
   */
  uint16_t ImageAnimationMode() const { return mImageAnimationMode; }
  void SetImageAnimationMode(uint16_t aMode);

  /**
   * Get medium of presentation
   */
  const nsAtom* Medium() const {
    MOZ_ASSERT(mMedium);
    return mMediaEmulationData.mMedium ? mMediaEmulationData.mMedium.get()
                                       : mMedium;
  }

  /*
   * Render the document as if being viewed on a device with the specified
   * media type.
   *
   * If passed null, it stops emulating.
   */
  void EmulateMedium(nsAtom* aMediaType);

  const mozilla::PreferenceSheet::Prefs& PrefSheetPrefs() const {
    return mozilla::PreferenceSheet::PrefsFor(*mDocument);
  }

  mozilla::StyleForcedColors ForcedColors() const { return mForcedColors; }
  bool ForcingColors() const;

  mozilla::ColorScheme DefaultBackgroundColorScheme() const;
  nscolor DefaultBackgroundColor() const;

  nsISupports* GetContainerWeak() const;

  nsDocShell* GetDocShell() const;

  /**
   * Get the visible area associated with this presentation context.
   * This is the size of the visible area that is used for
   * presenting the document. The returned value is in the standard
   * nscoord units (as scaled by the device context).
   */
  nsRect GetVisibleArea() const { return mVisibleArea; }

  /**
   * Set the currently visible area. The units for r are standard
   * nscoord units (as scaled by the device context).
   */
  void SetVisibleArea(const nsRect& aRect);

  /**
   * Set the initial visible area. This should be called only from
   * nsDocumentViewer when initializing this pres context visible area with
   * the document viewer bounds.
   */
  void SetInitialVisibleArea(const nsRect& aRect);

  nsSize GetSizeForViewportUnits() const { return mSizeForViewportUnits; }

  /**
   * Set the maximum height of the dynamic toolbar in nscoord units.
   */
  MOZ_CAN_RUN_SCRIPT
  void SetDynamicToolbarMaxHeight(mozilla::ScreenIntCoord aHeight);

  /**
   * Returns true if we are using the dynamic toolbar.
   */
  bool HasDynamicToolbar() const { return GetDynamicToolbarMaxHeight() > 0; }

  /*
   * |aOffset| must be offset from the bottom edge of the ICB and it's negative.
   */
  void UpdateDynamicToolbarOffset(mozilla::ScreenIntCoord aOffset);

  mozilla::ScreenIntCoord GetDynamicToolbarMaxHeight() const {
    MOZ_ASSERT_IF(mDynamicToolbarMaxHeight > 0,
                  IsRootContentDocumentCrossProcess());
    return mDynamicToolbarMaxHeight;
  }

  nscoord GetDynamicToolbarMaxHeightInAppUnits() const;

  mozilla::ScreenIntCoord GetDynamicToolbarHeight() const {
    MOZ_ASSERT_IF(mDynamicToolbarHeight > 0,
                  IsRootContentDocumentCrossProcess());
    return mDynamicToolbarHeight;
  }

  void UpdateKeyboardHeight(mozilla::ScreenIntCoord aHeight);

  mozilla::ScreenIntCoord GetKeyboardHeight() const;

  /**
   * Returns true if the software keyboard is visible and the document is
   * `interactive-widget=overlays-content` mode.
   */
  bool IsKeyboardVisibleOnOverlaysContent() const;

  /**
   * Returns the maximum height of the dynamic toolbar if the toolbar state is
   * `DynamicToolbarState::Collapsed`, otherwise returns zero.
   */
  nscoord GetBimodalDynamicToolbarHeightInAppUnits() const;

  /**
   * Returns the maximum height of the dynamic toolbar if the toolbar state is
   * `DynamicToolbarState::Collapsed`, otherwise returns zero.
   * The maximum height is based on the fixed viewport scale for elements with
   * position:fixed.
   */
  nscoord GetBimodalDynamicToolbarHeightForFixedPosInAppUnits() const;

  /**
   * Returns the state of the dynamic toolbar.
   */
  mozilla::DynamicToolbarState GetDynamicToolbarState() const;

  /**
   * Return true if this presentation context is a paginated
   * context.
   */
  bool IsPaginated() const { return mPaginated; }

  /**
   * Sets whether the presentation context can scroll for a paginated
   * context.
   */
  void SetPaginatedScrolling(bool aResult);

  /**
   * Return true if this presentation context can scroll for paginated
   * context.
   */
  bool HasPaginatedScrolling() const { return mCanPaginatedScroll; }

  uint32_t LastScrollGeneration() { return mLastScrollGeneration; }
  void UpdateLastScrollGeneration() { mLastScrollGeneration += 1; }
  bool HasBeenScrolledSince(const uint32_t& mPreviousScrollGeneration) const {
    return mPreviousScrollGeneration < mLastScrollGeneration;
  }

  /**
   * Get/set the size of a page
   */
  const nsSize& GetPageSize() const { return mPageSize; }
  const nsMargin& GetDefaultPageMargin() const { return mDefaultPageMargin; }
  void SetPageSize(nsSize aSize) { mPageSize = aSize; }

  /**
   * Get/set whether this document should be treated as having real pages
   * XXX This raises the obvious question of why a document that isn't a page
   *     is paginated; there isn't a good reason except history
   */
  bool IsRootPaginatedDocument() { return mIsRootPaginatedDocument; }
  void SetIsRootPaginatedDocument(bool aIsRootPaginatedDocument) {
    mIsRootPaginatedDocument = aIsRootPaginatedDocument;
  }

  /**
   * Get/set the print scaling level; used by nsPageFrame to scale up
   * pages.  Set safe to call before reflow, get guaranteed to be set
   * properly after reflow.
   */

  float GetPageScale() { return mPageScale; }
  void SetPageScale(float aScale) { mPageScale = aScale; }

  /**
   * Get/set the scaling factor to use when rendering the pages for print
   * preview. Only safe to get after print preview set up; safe to set anytime.
   * This is a scaling factor for the display of the print preview.  It
   * does not affect layout.  It only affects the size of the onscreen pages
   * in print preview.
   *
   * The getter should only be used by the page sequence frame, which is the
   * frame responsible for applying the scaling. Other callers should use
   * nsPageSequenceFrame::GetPrintPreviewScale() if needed, instead of this API.
   *
   * XXX Temporary: see http://wiki.mozilla.org/Gecko:PrintPreview
   */
  float GetPrintPreviewScaleForSequenceFrameOrScrollbars() const {
    return mPPScale;
  }
  void SetPrintPreviewScale(float aScale) { mPPScale = aScale; }

  nsDeviceContext* DeviceContext() const { return mDeviceContext; }
  mozilla::EventStateManager* EventStateManager() { return mEventManager; }

  bool UserInputEventsAllowed();

  void MaybeIncreaseMeasuredTicksSinceLoading();

  bool NeedsMoreTicksForUserInput() const;

  void ResetUserInputEventsAllowed() {
    MOZ_ASSERT(IsRoot());
    mMeasuredTicksSinceLoading = 0;
    mUserInputEventsAllowed = false;
  }

  // Get the text zoom factor in use.
  float TextZoom() const { return mTextZoom; }

  /**
   * Notify the pres context that the safe area insets have changed.
   */
  void SetSafeAreaInsets(const mozilla::LayoutDeviceIntMargin& aInsets);

  const mozilla::LayoutDeviceIntMargin& GetSafeAreaInsets() const {
    return mSafeAreaInsets;
  }

  void RegisterManagedPostRefreshObserver(mozilla::ManagedPostRefreshObserver*);
  void UnregisterManagedPostRefreshObserver(
      mozilla::ManagedPostRefreshObserver*);

 protected:
  void CancelManagedPostRefreshObservers();

#ifdef DEBUG
  void ValidatePresShellAndDocumentReleation() const;
#endif  // #ifdef DEBUG

  void SetTextZoom(float aZoom);
  void SetFullZoom(float aZoom);
  void SetOverrideDPPX(float);
  void SetInRDMPane(bool aInRDMPane);
  void UpdateInnerSizeSpoofedForRFP();
  void UpdateForcedColors(bool aNotify = true);

 public:
  float GetFullZoom() { return mFullZoom; }
  /**
   * Device full zoom differs from full zoom because it gets the zoom from
   * the device context, which may be using a different zoom due to rounding
   * of app units to device pixels.
   */
  float GetDeviceFullZoom();

  float GetOverrideDPPX() const { return mMediaEmulationData.mDPPX; }

  // Gets the forced color-scheme if any via either our embedder, or DevTools
  // emulation, or printing.
  //
  // NOTE(emilio): This might be called from an stylo thread.
  mozilla::dom::PrefersColorSchemeOverride GetOverriddenOrEmbedderColorScheme()
      const {
    return mOverriddenOrEmbedderColorScheme;
  }

  /**
   * Recomputes the data dependent on the browsing context, like zoom and text
   * zoom.
   */
  void RecomputeBrowsingContextDependentData();

  /**
   * Sets the effective color scheme override, and invalidate stuff as needed.
   */
  void SetColorSchemeOverride(mozilla::dom::PrefersColorSchemeOverride);

  /**
   * Sets the effective link parameters overrides, and invalidate stuff as
   * needed.
   */
  void SetLinkParametersOverride(
      const mozilla::StyleLinkParameters& aLinkParameters);

  /**
   * Content-area scrollbar insets forwarded from the <browser> embedder, per
   * physical side, in app units.
   */
  const nsMargin& EmbedderScrollbarInset() const {
    return mEmbedderScrollbarInset;
  }
  void SetEmbedderScrollbarInset(const nsMargin& aInset);

  /**
   * Return the device's screen size in inches, for font size
   * inflation.
   *
   * If |aChanged| is non-null, then aChanged is filled in with whether
   * the screen size value has changed since either:
   *  a. the last time the function was called with non-null aChanged, or
   *  b. the first time the function was called.
   */
  gfxSize ScreenSizeInchesForFontInflation(bool* aChanged = nullptr);

  int32_t AppUnitsPerDevPixel() const { return mCurAppUnitsPerDevPixel; }

  static nscoord CSSPixelsToAppUnits(int32_t aPixels) {
    return NSToCoordRoundWithClamp(float(aPixels) *
                                   float(mozilla::AppUnitsPerCSSPixel()));
  }

  static nscoord CSSPixelsToAppUnits(float aPixels) {
    return NSToCoordRoundWithClamp(aPixels *
                                   float(mozilla::AppUnitsPerCSSPixel()));
  }

  static int32_t AppUnitsToIntCSSPixels(nscoord aAppUnits) {
    return NSAppUnitsToIntPixels(aAppUnits,
                                 float(mozilla::AppUnitsPerCSSPixel()));
  }

  static float AppUnitsToFloatCSSPixels(nscoord aAppUnits) {
    return NSAppUnitsToFloatPixels(aAppUnits,
                                   float(mozilla::AppUnitsPerCSSPixel()));
  }

  static double AppUnitsToDoubleCSSPixels(nscoord aAppUnits) {
    return NSAppUnitsToDoublePixels(aAppUnits,
                                    double(mozilla::AppUnitsPerCSSPixel()));
  }

  nscoord DevPixelsToAppUnits(int32_t aPixels) const {
    return NSIntPixelsToAppUnits(aPixels, AppUnitsPerDevPixel());
  }

  int32_t AppUnitsToDevPixels(nscoord aAppUnits) const {
    return NSAppUnitsToIntPixels(aAppUnits, float(AppUnitsPerDevPixel()));
  }

  float AppUnitsToFloatDevPixels(nscoord aAppUnits) const {
    return aAppUnits / float(AppUnitsPerDevPixel());
  }

  int32_t CSSPixelsToDevPixels(int32_t aPixels) const {
    return AppUnitsToDevPixels(CSSPixelsToAppUnits(aPixels));
  }

  float CSSPixelsToDevPixels(float aPixels) const {
    return NSAppUnitsToFloatPixels(CSSPixelsToAppUnits(aPixels),
                                   float(AppUnitsPerDevPixel()));
  }

  int32_t DevPixelsToIntCSSPixels(int32_t aPixels) const {
    return AppUnitsToIntCSSPixels(DevPixelsToAppUnits(aPixels));
  }

  static nscoord RoundDownAppUnitsToCSSPixel(nscoord aAppUnits) {
    return mozilla::RoundDownToMultiple(aAppUnits,
                                        mozilla::AppUnitsPerCSSPixel());
  }
  static nscoord RoundUpAppUnitsToCSSPixel(nscoord aAppUnits) {
    return mozilla::RoundUpToMultiple(aAppUnits,
                                      mozilla::AppUnitsPerCSSPixel());
  }
  static nscoord RoundAppUnitsToCSSPixel(nscoord aAppUnits) {
    return mozilla::RoundToMultiple(aAppUnits, mozilla::AppUnitsPerCSSPixel());
  }

  nscoord RoundDownAppUnitsToDevPixel(nscoord aAppUnits) const {
    return mozilla::RoundDownToMultiple(aAppUnits, AppUnitsPerDevPixel());
  }
  nscoord RoundUpAppUnitsToDevPixel(nscoord aAppUnits) const {
    return mozilla::RoundUpToMultiple(aAppUnits, AppUnitsPerDevPixel());
  }
  nscoord RoundAppUnitsToDevPixel(nscoord aAppUnits) const {
    return mozilla::RoundToMultiple(aAppUnits, AppUnitsPerDevPixel());
  }

  mozilla::CSSIntPoint DevPixelsToIntCSSPixels(
      const mozilla::LayoutDeviceIntPoint& aPoint) {
    return mozilla::CSSIntPoint(
        AppUnitsToIntCSSPixels(DevPixelsToAppUnits(aPoint.x)),
        AppUnitsToIntCSSPixels(DevPixelsToAppUnits(aPoint.y)));
  }

  float DevPixelsToFloatCSSPixels(int32_t aPixels) const {
    return AppUnitsToFloatCSSPixels(DevPixelsToAppUnits(aPixels));
  }

  mozilla::CSSToLayoutDeviceScale CSSToDevPixelScale() const {
    return mozilla::CSSToLayoutDeviceScale(
        float(mozilla::AppUnitsPerCSSPixel()) / float(AppUnitsPerDevPixel()));
  }

  // If there is a remainder, it is rounded to nearest app units.
  nscoord GfxUnitsToAppUnits(gfxFloat aGfxUnits) const;

  gfxFloat AppUnitsToGfxUnits(nscoord aAppUnits) const;

  gfxRect AppUnitsToGfxUnits(const nsRect& aAppRect) const {
    return gfxRect(AppUnitsToGfxUnits(aAppRect.x),
                   AppUnitsToGfxUnits(aAppRect.y),
                   AppUnitsToGfxUnits(aAppRect.Width()),
                   AppUnitsToGfxUnits(aAppRect.Height()));
  }

  static nscoord CSSTwipsToAppUnits(float aTwips) {
    return NSToCoordRoundWithClamp(mozilla::AppUnitsPerCSSInch() *
                                   NS_TWIPS_TO_INCHES(aTwips));
  }

  // Margin-specific version, since they often need TwipsToAppUnits
  static nsMargin CSSTwipsToAppUnits(const nsIntMargin& marginInTwips) {
    return nsMargin(CSSTwipsToAppUnits(float(marginInTwips.top)),
                    CSSTwipsToAppUnits(float(marginInTwips.right)),
                    CSSTwipsToAppUnits(float(marginInTwips.bottom)),
                    CSSTwipsToAppUnits(float(marginInTwips.left)));
  }

  static nscoord CSSPointsToAppUnits(float aPoints) {
    return NSToCoordRound(aPoints * mozilla::AppUnitsPerCSSInch() /
                          POINTS_PER_INCH_FLOAT);
  }

  nscoord PhysicalMillimetersToAppUnits(float aMM) const;

  nscoord RoundAppUnitsToNearestDevPixels(nscoord aAppUnits) const {
    return DevPixelsToAppUnits(AppUnitsToDevPixels(aAppUnits));
  }

  /**
   * This checks the root element and the HTML BODY, if any, for an "overflow"
   * property that should be applied to the viewport. If one is found then we
   * return the element that we took the overflow from (which should then be
   * treated as "overflow: visible"), and we store the overflow style here.
   * If the document is in fullscreen, and the fullscreen element is not the
   * root, the scrollbar of viewport will be suppressed.
   * @param aRemovedChild the element we're about to remove from the DOM, which
   *                      we can't make the new override element.
   * @return if scroll was propagated from some content node, the content node
   *         it was propagated from.
   */
  mozilla::dom::Element* UpdateViewportScrollStylesOverride(
      const mozilla::dom::Element* aRemovedChild = nullptr);

  /**
   * Returns the cached result from the last call to
   * UpdateViewportScrollStylesOverride() -- i.e. return the node
   * whose scrollbar styles we have propagated to the viewport (or nullptr if
   * there is no such node).
   */
  mozilla::dom::Element* GetViewportScrollStylesOverrideElement() const {
    return mViewportScrollOverrideElement;
  }

  const ScrollStyles& GetViewportScrollStylesOverride() const {
    return mViewportScrollStyles;
  }

  /**
   * Check whether the given element would propagate its scrollbar styles to the
   * viewport in non-paginated mode.
   */
  bool ElementWouldPropagateScrollStyles(const mozilla::dom::Element&);

  /**
   * Methods for controlling the background drawing.
   */
  bool GetBackgroundImageDraw() const { return mDrawImageBackground; }
  bool GetBackgroundColorDraw() const { return mDrawColorBackground; }

  /**
   *  Check if bidi enabled (set depending on the presence of RTL
   *  characters or when default directionality is RTL).
   *  If enabled, we should apply the Unicode Bidi Algorithm
   *
   *  @lina 07/12/2000
   */
  bool BidiEnabled() const;

  /**
   *  Set bidi enabled. This means we should apply the Unicode Bidi Algorithm
   *
   *  @lina 07/12/2000
   */
  void SetBidiEnabled() const;

  /**
   *  Set visual or implicit mode into the pres context.
   *
   *  Visual directionality is a presentation method that displays text
   *  as if it were a uni-directional, according to the primary display
   *  direction only.
   *
   *  Implicit directionality is a presentation method in which the
   *  direction is determined by the Bidi algorithm according to the
   *  category of the characters and the category of the adjacent
   *  characters, and according to their primary direction.
   *
   *  @lina 05/02/2000
   */
  void SetVisualMode(bool aIsVisual) { mIsVisual = aIsVisual; }

  /**
   *  Check whether the content should be treated as visual.
   *
   *  @lina 05/02/2000
   */
  bool IsVisualMode() const { return mIsVisual; }

  enum class InteractionType : uint32_t {
    ClickInteraction,
    KeyInteraction,
    MouseMoveInteraction,
    ScrollInteraction
  };

  void RecordInteractionTime(InteractionType aType,
                             const mozilla::TimeStamp& aTimeStamp);

  void DisableInteractionTimeRecording() { mInteractionTimeEnabled = false; }

  // Mohamed

  /**
   * Set the Bidi options for the presentation context
   */
  void SetBidi(uint32_t aBidiOptions);

  /**
   * Get the Bidi options for the presentation context
   * Not inline so consumers of nsPresContext are not forced to
   * include Document.
   */
  uint32_t GetBidi() const;

  nsITheme* Theme() const MOZ_NONNULL_RETURN;

  void RecomputeTheme();

  bool UseOverlayScrollbars() const;

  /*
   * Notify the pres context that the theme has changed.  An internal switch
   * means it's one of our Mozilla themes that changed (e.g., Modern to
   * Classic). Otherwise, the OS is telling us that the native theme for the
   * platform has changed.
   */
  void ThemeChanged(mozilla::widget::ThemeChangeKind);

  /*
   * Notify the pres context that the resolution of the user interface has
   * changed. This happens if a window is moved between HiDPI and non-HiDPI
   * displays, so that the ratio of points to device pixels changes.
   * The notification happens asynchronously.
   */
  void UIResolutionChanged();

  /*
   * Like UIResolutionChanged() but invalidates values immediately.
   */
  void UIResolutionChangedSync();

  /** Printing methods below should only be used for Medium() == print **/
  void SetPrintSettings(nsIPrintSettings* aPrintSettings);

  nsIPrintSettings* GetPrintSettings() { return mPrintSettings; }

  /* Helper function that ensures that this prescontext is shown in its
     docshell if it's the most recent prescontext for the docshell.  Returns
     whether the prescontext is now being shown.
  */
  bool EnsureVisible();

#ifdef MOZ_REFLOW_PERF
  void CountReflows(const char* aName, nsIFrame* aFrame);
#endif

  void ConstructedFrame() { ++mFramesConstructed; }
  void ReflowedFrame() { ++mFramesReflowed; }
  void TriggeredAnimationRestyle() { ++mAnimationTriggeredRestyles; }

  uint64_t FramesConstructedCount() const { return mFramesConstructed; }
  uint64_t FramesReflowedCount() const { return mFramesReflowed; }
  uint64_t AnimationTriggeredRestylesCount() const {
    return mAnimationTriggeredRestyles;
  }

  static nscoord GetBorderWidthForKeyword(unsigned int aBorderWidthKeyword) {
    // This table maps border-width enums 'thin', 'medium', 'thick'
    // to actual nscoord values.
    static const nscoord kBorderWidths[] = {
        CSSPixelsToAppUnits(1), CSSPixelsToAppUnits(3), CSSPixelsToAppUnits(5)};
    MOZ_ASSERT(size_t(aBorderWidthKeyword) < std::size(kBorderWidths));

    return kBorderWidths[aBorderWidthKeyword];
  }

  gfxTextPerfMetrics* GetTextPerfMetrics() { return mTextPerf.get(); }

  bool IsDynamic() const {
    return mType == eContext_PageLayout || mType == eContext_Galley;
  }
  bool IsScreen() const {
    return mMedium == nsGkAtoms::screen || mType == eContext_PageLayout ||
           mType == eContext_PrintPreview;
  }
  bool IsPrintingOrPrintPreview() const {
    return mType == eContext_Print || mType == eContext_PrintPreview;
  }

  bool IsPrintPreview() const { return mType == eContext_PrintPreview; }

  gfxUserFontSet* GetUserFontSet();

  gfxMissingFontRecorder* MissingFontRecorder() { return mMissingFonts.get(); }

  void NotifyMissingFonts();

  void FlushCounterStyles();
  void MarkCounterStylesDirty();

  void FlushFontFeatureValues();
  void MarkFontFeatureValuesDirty() { mFontFeatureValuesDirty = true; }

  void FlushFontPaletteValues();
  void MarkFontPaletteValuesDirty() { mFontPaletteValuesDirty = true; }

  // Ensure that it is safe to hand out CSS rules outside the layout
  // engine by ensuring that all CSS style sheets have unique inners
  // and, if necessary, synchronously rebuilding all style data.
  void EnsureSafeToHandOutCSSRules();

  // Mark an area as invalidated, associated with a given transaction id
  // (allocated by nsRefreshDriver::GetTransactionId). Invalidated regions will
  // be dispatched to MozAfterPaint events when NotifyDidPaintForSubtree is
  // called for the transaction id (or any higher id).
  void NotifyInvalidation(TransactionId aTransactionId, const nsRect& aRect);
  void NotifyDidPaintForSubtree(
      TransactionId aTransactionId = TransactionId{0},
      const mozilla::TimeStamp& aTimeStamp = mozilla::TimeStamp());
  void NotifyRevokingDidPaint(TransactionId aTransactionId);

  // Tells the parent to stop painting the previous document of this browser.
  // Deferred for an inbound view transition, see Document::Reveal.
  void SendReadyToReplaceRetainedPaint();
  void DeferReadyToReplaceRetainedPaint() {
    mReadyToReplaceRetainedPaintDeferred = true;
  }
  bool IsReadyToReplaceRetainedPaintDeferred() const {
    return mReadyToReplaceRetainedPaintDeferred;
  }
  void SendReadyToReplaceRetainedPaintAfterNextComposite();
  void FlushDeferredReadyToReplaceRetainedPaint();
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FireDOMPaintEvent(
      nsTArray<nsRect>* aList, TransactionId aTransactionId,
      mozilla::TimeStamp aTimeStamp = mozilla::TimeStamp());

  bool IsDOMPaintEventPending();

  /**
   * Returns the RestyleManager's restyle generation counter.
   */
  uint64_t GetRestyleGeneration() const;
  uint64_t GetUndisplayedRestyleGeneration() const;

  /**
   * Notify the prescontext that the presshell is about to reflow a reflow root.
   * The single argument indicates whether this reflow should be interruptible.
   * If aInterruptible is false then CheckForInterrupt and HasPendingInterrupt
   * will always return false. If aInterruptible is true then CheckForInterrupt
   * will return true when a pending event is detected.  This is for use by the
   * presshell only.  Reflow code wanting to prevent interrupts should use
   * InterruptPreventer.
   */
  void ReflowStarted(bool aInterruptible);

  /**
   * A class that can be used to temporarily disable reflow interruption.
   */
  class InterruptPreventer;
  friend class InterruptPreventer;
  class MOZ_STACK_CLASS InterruptPreventer {
   public:
    explicit InterruptPreventer(nsPresContext* aCtx)
        : mCtx(aCtx),
          mInterruptsEnabled(aCtx->mInterruptsEnabled),
          mHasPendingInterrupt(aCtx->mHasPendingInterrupt) {
      mCtx->mInterruptsEnabled = false;
      mCtx->mHasPendingInterrupt = false;
    }
    ~InterruptPreventer() {
      mCtx->mInterruptsEnabled = mInterruptsEnabled;
      mCtx->mHasPendingInterrupt = mHasPendingInterrupt;
    }

   private:
    nsPresContext* mCtx;
    bool mInterruptsEnabled;
    bool mHasPendingInterrupt;
  };

  /**
   * Check for interrupts. This may return true if a pending event is
   * detected. Once it has returned true, it will keep returning true
   * until ReflowStarted is called. In all cases where this returns true,
   * the passed-in frame (which should be the frame whose reflow will be
   * interrupted if true is returned) will be passed to
   * PresShell::FrameNeedsToContinueReflow.
   */
  bool CheckForInterrupt(nsIFrame* aFrame);
  /**
   * Returns true if CheckForInterrupt has returned true since the last
   * ReflowStarted call. Cannot itself trigger an interrupt check.
   */
  bool HasPendingInterrupt() const { return mHasPendingInterrupt; }
  /**
   * Sets a flag that will trip a reflow interrupt. This only bypasses the
   * interrupt timeout and the pending event check; other checks such as whether
   * interrupts are enabled and the interrupt check skipping still take effect.
   */
  void SetPendingInterruptFromTest() { mPendingInterruptFromTest = true; }

  /**
   * If we have a presshell, and if the given content's current
   * document is the same as our presshell's document, return the
   * content's primary frame, or the image frame if the content is an
   * <area>.  Otherwise, return null.  Only use this if you care about
   * which presshell the frame is in.
   */
  nsIFrame* GetPrimaryOrAreaImageFrameFor(nsIContent* aContent);

  virtual size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  virtual size_t SizeOfIncludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  /**
   * We are a root content document in process if: we are not a resource doc, we
   * are not chrome, and we either have no parent in the current process or our
   * parent is chrome.
   */
  bool IsRootContentDocumentInProcess() const;

  /**
   * We are a root content document cross process if: we are not a resource doc,
   * we are not chrome, and we either have no parent in any process or our
   * parent is chrome.
   */
  bool IsRootContentDocumentCrossProcess() const;

  bool HadNonBlankPaint() const { return mHadNonBlankPaint; }
  bool HadFirstContentfulPaint() const { return mHadFirstContentfulPaint; }
  bool HasStoppedGeneratingLCP() const;
  void NotifyNonBlankPaint();
  void NotifyContentfulPaint();
  void NotifyPaintStatusReset();

  bool HasEverBuiltInvisibleText() const { return mHasEverBuiltInvisibleText; }
  void SetBuiltInvisibleText() { mHasEverBuiltInvisibleText = true; }

  bool HasWarnedAboutTooLargeDashedOrDottedRadius() const {
    return mHasWarnedAboutTooLargeDashedOrDottedRadius;
  }

  void SetHasWarnedAboutTooLargeDashedOrDottedRadius() {
    mHasWarnedAboutTooLargeDashedOrDottedRadius = true;
  }

  void RegisterContainerQueryFrame(nsIFrame* aFrame);
  void UnregisterContainerQueryFrame(nsIFrame* aFrame);
  bool HasContainerQueryFrames() const {
    return !mContainerQueryFrames.IsEmpty();
  }

  void FinishedContainerQueryUpdate();

  void UpdateContainerQueryStylesAndAnchorPosLayout();

  // Call GetBidiEngine to get a bidi-resolution engine from the prescontext.
  // This may be an previously-cached object or a newly-constructed one.
  // The caller then has sole ownership of the returned object.
  mozilla::UniquePtr<mozilla::intl::Bidi> GetBidiEngine();

  // Call ReleaseBidiEngine when finished with a bidi-resolution engine. This
  // allows the prescontext to keep it around for subsequent re-use.
  void ReleaseBidiEngine(
      mozilla::UniquePtr<mozilla::intl::Bidi>&& aBidiEngine) {
    mBidiEngine = std::move(aBidiEngine);
  }

  gfxFontFeatureValueSet* GetFontFeatureValuesLookup() const {
    return mFontFeatureValuesLookup;
  }

  mozilla::gfx::FontPaletteValueSet* GetFontPaletteValueSet() const {
    return mFontPaletteValueSet;
  }

  bool NeedsToUpdateHiddenByContentVisibilityForAnimations() const {
    return mNeedsToUpdateHiddenByContentVisibilityForAnimations;
  }
  void SetNeedsToUpdateHiddenByContentVisibilityForAnimations() {
    mNeedsToUpdateHiddenByContentVisibilityForAnimations = true;
  }
  void UpdateHiddenByContentVisibilityForAnimationsIfNeeded() {
    if (mNeedsToUpdateHiddenByContentVisibilityForAnimations) {
      DoUpdateHiddenByContentVisibilityForAnimations();
    }
  }

 protected:
  void DoUpdateHiddenByContentVisibilityForAnimations();
  friend class nsRunnableMethod<nsPresContext>;
  MOZ_CAN_RUN_SCRIPT void ThemeChangedInternal();
  void RefreshSystemMetrics();

  // Update device context's resolution from the widget
  void UIResolutionChangedInternal();

  void SetImgAnimations(nsIContent* aParent, uint16_t aMode);
  void SetSMILAnimations(mozilla::dom::Document* aDoc, uint16_t aNewMode,
                         uint16_t aOldMode);

  static void PreferenceChanged(const char* aPrefName, void* aSelf);
  void PreferenceChanged(const char* aPrefName);

  void GetUserPreferences();

  void UpdateCharSet(NotNull<const Encoding*> aCharSet);

  void DoForceReflowForFontInfoUpdateFromStyle();

  void UpdateAnimationsPlayBackRateMultiplier(double aMultiplier);

 public:
  // Used by the PresShell to force a reflow when some aspect of font info
  // has been updated, potentially affecting font selection and layout.
  void ForceReflowForFontInfoUpdate(bool aNeedsReframe);
  void ForceReflowForFontInfoUpdateFromStyle();

  void InvalidatePaintedLayers();

  uint32_t GetNextFrameRateMultiplier() const {
    return mNextFrameRateMultiplier;
  }

  void DidUseFrameRateMultiplier() {
    // This heuristic is used to reduce frame rate between fcp and the end of
    // the page load.
    if (mNextFrameRateMultiplier < 8) {
      ++mNextFrameRateMultiplier;
    }
  }

  mozilla::TimeStamp GetMarkPaintTimingStart() const {
    return mMarkPaintTimingStart;
  }

  // Are we using normalized ruby-positioning metrics? This will fetch and
  // cache the pref if not already initialized.
  bool NormalizeRubyMetrics();

  // Get the scaling factor to apply to em-normalized font ascent/descent. This
  // should only be used if NormalizeRubyMetrics() has returned true, otherwise
  // its return value may be meaningless.
  float RubyPositioningFactor() const {
    MOZ_ASSERT(mRubyPositioningFactor > 0.0f);
    return mRubyPositioningFactor;
  }

  double AnimationsPlayBackRateMultiplier() const {
    return mAnimationsPlayBackRateMultiplier;
  }

 protected:
  // May be called multiple times (unlink, destructor)
  void Destroy();

  void AppUnitsPerDevPixelChanged();

  bool HavePendingInputEvent();

  // Creates a one-shot timer with the given aCallback & aDelay.
  // Returns a refcounted pointer to the timer (or nullptr on failure).
  already_AddRefed<nsITimer> CreateTimer(nsTimerCallbackFunc aCallback,
                                         const nsACString& aName,
                                         uint32_t aDelay);

  struct TransactionInvalidations {
    TransactionId mTransactionId;
    nsTArray<nsRect> mInvalidations;
    bool mIsWaitingForPreviousTransaction = false;
  };
  TransactionInvalidations* GetInvalidations(TransactionId aTransactionId);

  // This should be called only when we update mVisibleArea or
  // mDynamicToolbarMaxHeight or `app units per device pixels` changes.
  void AdjustSizeForViewportUnits();

  // Call in response to prefs changes that might affect what fonts should be
  // visibile to CSS. Returns whether the current visibility value actually
  // changed (in which case content should be reflowed).
  bool UpdateFontVisibility();

  // IMPORTANT: The ownership implicit in the following member variables
  // has been explicitly checked.  If you add any members to this class,
  // please make the ownership explicit (pinkerton, scc).

  // the PresShell owns a strong reference to the nsPresContext, and is
  // responsible for nulling this pointer before it is destroyed
  mozilla::PresShell* MOZ_NON_OWNING_REF mPresShell;  // [WEAK]
  RefPtr<mozilla::dom::Document> mDocument;
  RefPtr<nsDeviceContext> mDeviceContext;  // [STRONG] could be weak, but
                                           // better safe than sorry.
                                           // Cannot reintroduce cycles
                                           // since there is no dependency
                                           // from gfx back to layout.
  RefPtr<nsFontCache> mFontCache;
  RefPtr<mozilla::EventStateManager> mEventManager;
  RefPtr<nsRefreshDriver> mRefreshDriver;
  RefPtr<mozilla::AnimationEventDispatcher> mAnimationEventDispatcher;
  RefPtr<mozilla::EffectCompositor> mEffectCompositor;
  mozilla::UniquePtr<nsTransitionManager> mTransitionManager;
  mozilla::UniquePtr<nsAnimationManager> mAnimationManager;
  mozilla::UniquePtr<mozilla::TimelineManager> mTimelineManager;
  mozilla::UniquePtr<mozilla::RestyleManager> mRestyleManager;
  RefPtr<mozilla::CounterStyleManager> mCounterStyleManager;
  const nsStaticAtom* mMedium;
  RefPtr<gfxFontFeatureValueSet> mFontFeatureValuesLookup;
  RefPtr<mozilla::gfx::FontPaletteValueSet> mFontPaletteValueSet;

  // TODO(emilio): Maybe lazily create and put under a UniquePtr if this grows a
  // lot?
  MediaEmulationData mMediaEmulationData;

  float mTextZoom;  // Text zoom, defaults to 1.0
  float mFullZoom;  // Page zoom, defaults to 1.0

  gfxSize mLastFontInflationScreenSize;

  int32_t mCurAppUnitsPerDevPixel;
  int32_t mAutoQualityMinFontSizePixelsPref;

  float mRubyPositioningFactor = -1.0f;  // negative indicates uninitialized

  nsCOMPtr<nsITheme> mTheme;
  nsCOMPtr<nsIPrintSettings> mPrintSettings;

  mozilla::UniquePtr<mozilla::intl::Bidi> mBidiEngine;

  AutoTArray<TransactionInvalidations, 4> mTransactions;

  // text performance metrics
  mozilla::UniquePtr<gfxTextPerfMetrics> mTextPerf;

  mozilla::UniquePtr<gfxMissingFontRecorder> mMissingFonts;

  nsRect mVisibleArea;
  // This value is used to resolve viewport units.
  // On mobile this size is including the dynamic toolbar maximum height below.
  // On desktops this size is pretty much the same as |mVisibleArea|.
  nsSize mSizeForViewportUnits;
  // The maximum height of the dynamic toolbar on mobile.
  mozilla::ScreenIntCoord mDynamicToolbarMaxHeight;
  mozilla::ScreenIntCoord mDynamicToolbarHeight;
  // Safe area insets support
  mozilla::LayoutDeviceIntMargin mSafeAreaInsets;
  nsSize mPageSize;

  // The computed page margins from the print settings.
  //
  // This margin will be used for each page in the current print operation, by
  // default (i.e. unless overridden by @page rules).
  //
  // FIXME(emilio): Maybe we could let a global @page rule do that, though it's
  // sketchy at best, see https://github.com/w3c/csswg-drafts/issues/5437 for
  // discussion.
  nsMargin mDefaultPageMargin;
  float mPageScale;
  float mPPScale;

  // This is a non-owning pointer. May be null. If non-null, it's guaranteed to
  // be pointing to an element that's still alive, because we'll reset it in
  // UpdateViewportScrollStylesOverride() as part of the cleanup code when
  // this element is removed from the document. (For <body> and the root
  // element, this call happens in nsCSSFrameConstructor::ContentRemoved(). For
  // fullscreen elements, it happens in the fullscreen-specific cleanup invoked
  // by Element::UnbindFromTree().)
  mozilla::dom::Element* MOZ_NON_OWNING_REF mViewportScrollOverrideElement;

  // Counters for tests and tools that want to detect frame construction
  // or reflow.
  uint64_t mElementsRestyled;
  uint64_t mFramesConstructed;
  uint64_t mFramesReflowed;
  uint64_t mAnimationTriggeredRestyles;

  mozilla::TimeStamp mReflowStartTime;

  // Defined in https://w3c.github.io/paint-timing/#mark-paint-timing step 2.
  mozilla::TimeStamp mMarkPaintTimingStart;

  Maybe<TransactionId> mFirstContentfulPaintTransactionId;

  // ReadyToReplaceRetainedPaint is sent when this transaction, or a later one,
  // has been composited. The refresh driver can have older transactions in
  // flight.
  Maybe<TransactionId> mReadyToReplaceRetainedPaintTransactionId;

  mozilla::UniquePtr<mozilla::MediaFeatureChange>
      mPendingMediaFeatureValuesChange;

  // Time of various first interaction types, used to report time from
  // first paint of the top level content pres shell to first interaction.
  mozilla::TimeStamp mFirstNonBlankPaintTime;
  mozilla::TimeStamp mFirstClickTime;
  mozilla::TimeStamp mFirstKeyTime;
  mozilla::TimeStamp mFirstMouseMoveTime;
  mozilla::TimeStamp mFirstScrollTime;

  // last time we did a full style flush
  mozilla::TimeStamp mLastStyleUpdateForAllAnimations;

  // incremented each time the root scroller scrolls, helpful to determine if
  // it has scrolled between the start and end of some deferred work (such as a
  // navigation intercept).
  uint32_t mLastScrollGeneration;

  uint32_t mInterruptChecksToSkip;

  // During page load we use slower frame rate.
  uint32_t mNextFrameRateMultiplier;

  uint32_t mMeasuredTicksSinceLoading;

  nsTArray<RefPtr<mozilla::ManagedPostRefreshObserver>>
      mManagedPostRefreshObservers;

  // If we block the use of a font-family that is explicitly requested,
  // due to font visibility settings, we log a message to the web console;
  // this hash-set keeps track of names we've logged for this context, so
  // that we can avoid repeatedly reporting the same font.
  nsTHashSet<nsCString> mBlockedFonts;

  // The set of container query boxes currently in the document, sorted by
  // depth.
  mozilla::DepthOrderedFrameList mContainerQueryFrames;
  // The set of container query elements currently in the document that have
  // been updated so far. This is necessary to avoid reentering on container
  // query style changes which cause us to do frame reconstruction.
  nsTHashSet<nsIContent*> mUpdatedContainerQueryContents;

  // The cache of BrowsingContext.animationsPlayBackRateMultiplier.
  // We need to cache it since the multiplier needs to be queried off the
  // main-thread, unfortunately Document::GetBrowsingContext can not be used off
  // the main-thread.
  double mAnimationsPlayBackRateMultiplier = 1.0;

  ScrollStyles mViewportScrollStyles;

  uint16_t mImageAnimationMode;
  uint16_t mImageAnimationModePref;

  nsPresContextType mType;

 public:
  // The following are public member variables so that we can use them
  // with mozilla::AutoToggle or mozilla::AutoRestore.

  // Should we disable font size inflation because we're inside of
  // shrink-wrapping calculations on an inflation container?
  bool mInflationDisabledForShrinkWrap;

 protected:
  static constexpr size_t kThemeChangeKindBits = 2;
  static_assert(unsigned(mozilla::widget::ThemeChangeKind::AllBits) <=
                    (1u << kThemeChangeKindBits) - 1,
                "theme change kind doesn't fit");

  unsigned mInteractionTimeEnabled : 1;
  unsigned mHasPendingInterrupt : 1;
  unsigned mHasEverBuiltInvisibleText : 1;
  unsigned mPendingInterruptFromTest : 1;
  unsigned mInterruptsEnabled : 1;
  unsigned mDrawImageBackground : 1;
  unsigned mDrawColorBackground : 1;
  unsigned mNeverAnimate : 1;
  unsigned mPaginated : 1;
  unsigned mCanPaginatedScroll : 1;
  unsigned mDoScaledTwips : 1;
  unsigned mIsRootPaginatedDocument : 1;
  unsigned mPendingThemeChanged : 1;
  // widget::ThemeChangeKind
  unsigned mPendingThemeChangeKind : kThemeChangeKindBits;
  unsigned mPendingUIResolutionChanged : 1;
  unsigned mPendingFontInfoUpdateReflowFromStyle : 1;

  // Are we currently drawing an SVG glyph?
  unsigned mIsGlyph : 1;

  // Is the current mCounterStyleManager valid?
  unsigned mCounterStylesDirty : 1;

  // Is the current mFontFeatureValuesLookup valid?
  unsigned mFontFeatureValuesDirty : 1;

  // Is the current mFontFeatureValueSet valid?
  unsigned mFontPaletteValuesDirty : 1;

  unsigned mIsVisual : 1;

  // Are we in the RDM pane?
  unsigned mInRDMPane : 1;

  unsigned mHasWarnedAboutTooLargeDashedOrDottedRadius : 1;

  // Have we added quirk.css to the style set?
  unsigned mQuirkSheetAdded : 1;

  // Has NotifyNonBlankPaint been called on this PresContext?
  unsigned mHadNonBlankPaint : 1;
  // Has NotifyContentfulPaint been called on this PresContext?
  unsigned mHadFirstContentfulPaint : 1;
  // True when a contentful paint has happened and this paint doesn't
  // come from the regular tick process. Usually this means a
  // contentful paint was triggered manually.
  unsigned mHadNonTickContentfulPaint : 1;

  // Has NotifyDidPaintForSubtree been called for a contentful paint?
  unsigned mHadContentfulPaintComposite : 1;
  // Whether ReadyToReplaceRetainedPaint waits for an inbound view transition.
  unsigned mReadyToReplaceRetainedPaintDeferred : 1;

  // Whether we might need to update c-v state for animations.
  unsigned mNeedsToUpdateHiddenByContentVisibilityForAnimations : 1;

  unsigned mUserInputEventsAllowed : 1;

#ifdef DEBUG
  unsigned mInitialized : 1;
#endif

  // FIXME(emilio): These would be better packed on top of the bitfields, but
  // that breaks bindgen in win32.
  FontVisibility mFontVisibility = FontVisibility::Unknown;
  mozilla::dom::PrefersColorSchemeOverride mOverriddenOrEmbedderColorScheme;
  mozilla::StyleForcedColors mForcedColors;
  mozilla::StyleLinkParameters mLinkParameters;
  nsMargin mEmbedderScrollbarInset;

 protected:
  virtual ~nsPresContext();

  void LastRelease();

  void EnsureTheme();

#ifdef DEBUG
 private:
  friend struct nsAutoLayoutPhase;
  mozilla::EnumeratedArray<nsLayoutPhase, uint32_t,
                           size_t(nsLayoutPhase::COUNT)>
      mLayoutPhaseCount;

 public:
  uint32_t LayoutPhaseCount(nsLayoutPhase aPhase) {
    return mLayoutPhaseCount[aPhase];
  }
#endif
};

class nsRootPresContext final : public nsPresContext {
 public:
  nsRootPresContext(mozilla::dom::Document* aDocument, nsPresContextType aType);
  virtual bool IsRoot() const override { return true; }

  /**
   * Add a runnable that will get called before the next paint. They will get
   * run eventually even if painting doesn't happen. They might run well before
   * painting happens.
   */
  void AddWillPaintObserver(nsIRunnable* aRunnable);

  /**
   * Run all runnables that need to get called before the next paint.
   */
  void FlushWillPaintObservers();

  virtual size_t SizeOfExcludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const override;

 protected:
  class RunWillPaintObservers : public mozilla::Runnable {
   public:
    explicit RunWillPaintObservers(nsRootPresContext* aPresContext)
        : Runnable("nsPresContextType::RunWillPaintObservers"),
          mPresContext(aPresContext) {}
    void Revoke() { mPresContext = nullptr; }
    NS_IMETHOD Run() override {
      if (mPresContext) {
        mPresContext->FlushWillPaintObservers();
      }
      return NS_OK;
    }
    // The lifetime of this reference is handled by an nsRevocableEventPtr
    nsRootPresContext* MOZ_NON_OWNING_REF mPresContext;
  };

  friend class nsPresContext;

  nsTArray<nsCOMPtr<nsIRunnable>> mWillPaintObservers;
  nsRevocableEventPtr<RunWillPaintObservers> mWillPaintFallbackEvent;
};

#ifdef MOZ_REFLOW_PERF

#  define DO_GLOBAL_REFLOW_COUNT(_name) \
    aPresContext->CountReflows((_name), (nsIFrame*)this);
#else
#  define DO_GLOBAL_REFLOW_COUNT(_name)
#endif  // MOZ_REFLOW_PERF

#endif /* nsPresContext_h_ */
