/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a presentation of a document, part 1 */

#include "nsPresContext.h"
#include "nsPresContextInlines.h"

#include "mozilla/ArrayUtils.h"
#if defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/AsyncEventDispatcher.h"
#endif
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Encoding.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"

#include "base/basictypes.h"
#include "nsCRT.h"
#include "nsCOMPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsDocShell.h"
#include "nsIConsoleService.h"
#include "nsIDocumentViewer.h"
#include "nsPIDOMWindow.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/MediaFeatureChange.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "nsIPrintSettings.h"
#include "nsLanguageAtomService.h"
#include "mozilla/LookAndFeel.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsHTMLDocument.h"
#include "nsIWeakReferenceUtils.h"
#include "nsThreadUtils.h"
#include "nsLayoutUtils.h"
#include "nsViewManager.h"
#include "mozilla/RestyleManager.h"
#include "gfxPlatform.h"
#include "nsFontFaceLoader.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EventListenerManager.h"
#include "prenv.h"
#include "nsTransitionManager.h"
#include "nsAnimationManager.h"
#include "CounterStyleManager.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/Element.h"
#include "nsIMessageManager.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/MediaQueryList.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/StaticPresData.h"
#include "nsRefreshDriver.h"
#include "LayerUserData.h"
#include "mozilla/dom/NotifyPaintEvent.h"
#include "nsFontCache.h"
#include "nsFrameLoader.h"
#include "nsContentUtils.h"
#include "nsPIWindowRoot.h"
#include "mozilla/Preferences.h"
#include "gfxTextRun.h"
#include "nsFontFaceUtils.h"
#include "COLRFonts.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/GlobalStyleSheetCache.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/StaticPrefs_bidi.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/StaticPrefs_zoom.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/glean/LayoutMetrics.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TimelineManager.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceMainThread.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "mozilla/dom/PerformancePaintTiming.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "MobileViewportManager.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/dom/InteractiveWidget.h"
#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessible.h"
#endif

// Needed for Start/Stop of Image Animation
#include "imgIContainer.h"
#include "nsIImageLoadingContent.h"

#include "nsBidiUtils.h"
#include "nsServiceManagerUtils.h"

#include "mozilla/dom/URL.h"
#include "mozilla/ServoCSSParser.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;

/**
 * Layer UserData for ContainerLayers that want to be notified
 * of local invalidations of them and their descendant layers.
 * Pass a callback to ComputeDifferences to have these called.
 */
class ContainerLayerPresContext : public LayerUserData {
 public:
  nsPresContext* mPresContext;
};

bool nsPresContext::IsDOMPaintEventPending() {
  if (!mTransactions.IsEmpty()) {
    return true;
  }

  nsRootPresContext* drpc = GetRootPresContext();
  if (drpc && drpc->mRefreshDriver->IsPaintPending()) {
    // Since we're promising that there will be a MozAfterPaint event fired, we
    // record an empty invalidation in case display list invalidation doesn't
    // invalidate anything further.
    NotifyInvalidation(drpc->mRefreshDriver->LastTransactionId().Next(),
                       nsRect());
    return true;
  }
  return false;
}

struct WeakRunnableMethod : Runnable {
  using Method = void (nsPresContext::*)();

  WeakRunnableMethod(const char* aName, nsPresContext* aPc, Method aMethod)
      : Runnable(aName), mPresContext(aPc), mMethod(aMethod) {}

  NS_IMETHOD Run() override {
    if (nsPresContext* pc = mPresContext.get()) {
      (pc->*mMethod)();
    }
    return NS_OK;
  }

 private:
  WeakPtr<nsPresContext> mPresContext;
  Method mMethod;
};

// When forcing a font-info-update reflow from style, we don't need to reframe,
// but we'll need to restyle to pick up updated font metrics. In order to avoid
// synchronously having to deal with multiple restyles, we use an early refresh
// driver runner, which should prevent flashing for users.
//
// We might do a bit of extra work if the page flushes layout between the
// restyle and when this happens, which is a bit unfortunate, but not worse than
// what we used to do...
//
// A better solution would be to be able to synchronously initialize font
// information from style worker threads, perhaps...
void nsPresContext::ForceReflowForFontInfoUpdateFromStyle() {
  if (mPendingFontInfoUpdateReflowFromStyle) {
    return;
  }

  mPendingFontInfoUpdateReflowFromStyle = true;
  nsCOMPtr<nsIRunnable> ev = new WeakRunnableMethod(
      "nsPresContext::DoForceReflowForFontInfoUpdateFromStyle", this,
      &nsPresContext::DoForceReflowForFontInfoUpdateFromStyle);
  RefreshDriver()->AddEarlyRunner(ev);
}

void nsPresContext::DoForceReflowForFontInfoUpdateFromStyle() {
  mPendingFontInfoUpdateReflowFromStyle = false;
  ForceReflowForFontInfoUpdate(false);
}

void nsPresContext::ForceReflowForFontInfoUpdate(bool aNeedsReframe) {
  // In the case of a static-clone document used for printing or print-preview,
  // this is undesirable because the nsPrintJob is holding weak refs to frames
  // that will get blown away unexpectedly by this reconstruction. So the
  // prescontext for a print/preview doc ignores the font-list update.
  //
  // This means the print document may still be using cached fonts that are no
  // longer present in the font list, but that should be safe given that all the
  // required font instances have already been created, so it won't be depending
  // on access to the font-list entries.
  //
  // XXX Actually, I think it's probably a bad idea to do *any* restyling of
  // print documents in response to pref changes. We could be in the middle
  // of printing the document, and reflowing all the frames might cause some
  // kind of unwanted mid-document discontinuity.
  if (IsPrintingOrPrintPreview()) {
    return;
  }

  // If there's a user font set, discard any src:local() faces it may have
  // loaded because their font entries may no longer be valid.
  if (auto* fonts = Document()->GetFonts()) {
    fonts->GetImpl()->ForgetLocalFaces();
  }

  FlushFontCache();

  if (!mPresShell) {
    // RebuildAllStyleData won't do anything without mPresShell.
    return;
  }

  nsChangeHint changeHint =
      aNeedsReframe ? nsChangeHint_ReconstructFrame : NS_STYLE_HINT_REFLOW;

  // We also need to trigger restyling for ex/ch units changes to take effect,
  // if needed.
  auto restyleHint = StyleSet()->UsesFontMetrics()
                         ? RestyleHint::RecascadeSubtree()
                         : RestyleHint{0};

  RebuildAllStyleData(changeHint, restyleHint);
}

static bool IsVisualCharset(NotNull<const Encoding*> aCharset) {
  return aCharset == ISO_8859_8_ENCODING;
}

nsPresContext::nsPresContext(dom::Document* aDocument, nsPresContextType aType)
    : mPresShell(nullptr),
      mDocument(aDocument),
      mMedium(aType == eContext_Galley ? nsGkAtoms::screen : nsGkAtoms::print),
      mTextZoom(1.0),
      mFullZoom(1.0),
      mLastFontInflationScreenSize(gfxSize(-1.0, -1.0)),
      mCurAppUnitsPerDevPixel(0),
      mDynamicToolbarMaxHeight(0),
      mDynamicToolbarHeight(0),
      mPageSize(-1, -1),
      mPageScale(0.0),
      mPPScale(1.0f),
      mViewportScrollOverrideElement(nullptr),
      mElementsRestyled(0),
      mFramesConstructed(0),
      mFramesReflowed(0),
      mAnimationTriggeredRestyles(0),
      mInterruptChecksToSkip(0),
      mNextFrameRateMultiplier(0),
      mMeasuredTicksSinceLoading(0),
      mViewportScrollStyles(StyleOverflow::Auto, StyleOverflow::Auto),
      // mImageAnimationMode is initialised below, in constructor body
      mImageAnimationModePref(imgIContainer::kNormalAnimMode),
      mType(aType),
      mInflationDisabledForShrinkWrap(false),
      mInteractionTimeEnabled(true),
      mHasPendingInterrupt(false),
      mHasEverBuiltInvisibleText(false),
      mPendingInterruptFromTest(false),
      mInterruptsEnabled(false),
      mDrawImageBackground(true),  // always draw the background
      mDrawColorBackground(true),
      // mNeverAnimate is initialised below, in constructor body
      mPaginated(aType != eContext_Galley),
      mCanPaginatedScroll(false),
      mDoScaledTwips(true),
      mIsRootPaginatedDocument(false),
      mPendingThemeChanged(false),
      mPendingThemeChangeKind(0),
      mPendingUIResolutionChanged(false),
      mPendingFontInfoUpdateReflowFromStyle(false),
      mIsGlyph(false),
      mCounterStylesDirty(true),
      mFontFeatureValuesDirty(true),
      mFontPaletteValuesDirty(true),
      mIsVisual(false),
      mInRDMPane(false),
      mHasWarnedAboutTooLargeDashedOrDottedRadius(false),
      mQuirkSheetAdded(false),
      mHadNonBlankPaint(false),
      mHadFirstContentfulPaint(false),
      mHadNonTickContentfulPaint(false),
      mHadContentfulPaintComposite(false),
      mNeedsToUpdateHiddenByContentVisibilityForAnimations(false),
      mUserInputEventsAllowed(false),
#ifdef DEBUG
      mInitialized(false),
#endif
      mOverriddenOrEmbedderColorScheme(dom::PrefersColorSchemeOverride::None),
      mForcedColors(StyleForcedColors::None) {
#ifdef DEBUG
  PodZero(&mLayoutPhaseCount);
#endif

  if (!IsDynamic()) {
    mImageAnimationMode = imgIContainer::kDontAnimMode;
    mNeverAnimate = true;
  } else {
    mImageAnimationMode = imgIContainer::kNormalAnimMode;
    mNeverAnimate = false;
  }
  NS_ASSERTION(mDocument, "Null document");

  // if text perf logging enabled, init stats struct
  if (MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_textperf), LogLevel::Warning)) {
    mTextPerf = MakeUnique<gfxTextPerfMetrics>();
  }

  if (StaticPrefs::gfx_missing_fonts_notify()) {
    mMissingFonts = MakeUnique<gfxMissingFontRecorder>();
  }

  if (StaticPrefs::layout_dynamic_toolbar_max_height() > 0 &&
      IsRootContentDocumentCrossProcess()) {
    // The pref for dynamic toolbar max height is only used in reftests so it's
    // fine to set here.
    mDynamicToolbarMaxHeight = StaticPrefs::layout_dynamic_toolbar_max_height();
  }

  UpdateFontVisibility();
  UpdateForcedColors(/* aNotify = */ false);
}

static const char* gExactCallbackPrefs[] = {
    "dom.meta-viewport.enabled",
    "image.animation_mode",
    "intl.accept_languages",
    "layout.css.devPixelsPerPx",
    "layout.css.dpi",
    "layout.css.letter-spacing.model",
    "layout.css.text-transform.uppercase-eszett.enabled",
    "privacy.trackingprotection.enabled",
    nullptr,
};

static const char* gPrefixCallbackPrefs[] = {
    "bidi.",
    "browser.viewport.",
    "font.",
    "gfx.font_rendering.",
    "layout.css.font-visibility.",
    nullptr,
};

void nsPresContext::Destroy() {
  if (mEventManager) {
    // unclear if these are needed, but can't hurt
    mEventManager->NotifyDestroyPresContext(this);
    mEventManager->SetPresContext(nullptr);
    mEventManager = nullptr;
  }

  if (mFontCache) {
    mFontCache->Destroy();
    mFontCache = nullptr;
  }

  // Unregister preference callbacks
  Preferences::UnregisterPrefixCallbacks(nsPresContext::PreferenceChanged,
                                         gPrefixCallbackPrefs, this);
  Preferences::UnregisterCallbacks(nsPresContext::PreferenceChanged,
                                   gExactCallbackPrefs, this);

  mRefreshDriver = nullptr;
  MOZ_ASSERT(mManagedPostRefreshObservers.IsEmpty());
}

nsPresContext::~nsPresContext() {
  MOZ_ASSERT(!mPresShell, "Presshell forgot to clear our mPresShell pointer");
  DetachPresShell();

  Destroy();
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsPresContext)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsPresContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(nsPresContext, LastRelease())

void nsPresContext::LastRelease() {
  if (mMissingFonts) {
    mMissingFonts->Clear();
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsPresContext)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsPresContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnimationEventDispatcher);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument);
  // NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mDeviceContext); // not xpcom
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEffectCompositor);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEventManager);
  // NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mLanguage); // an atom

  // NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTheme); // a service
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPrintSettings);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsPresContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAnimationEventDispatcher);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDeviceContext);  // worth bothering?
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEffectCompositor);
  // NS_RELEASE(tmp->mLanguage); // an atom
  // NS_IMPL_CYCLE_COLLECTION_UNLINK(mTheme); // a service
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPrintSettings);
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR

  tmp->Destroy();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

bool nsPresContext::IsChrome() const {
  return Document()->IsInChromeDocShell();
}

void nsPresContext::GetUserPreferences() {
  if (!GetPresShell()) {
    // No presshell means nothing to do here.  We'll do this when we
    // get a presshell.
    return;
  }

  Document()->SetMayNeedFontPrefsUpdate();

  // * image animation
  nsAutoCString animatePref;
  Preferences::GetCString("image.animation_mode", animatePref);
  if (animatePref.EqualsLiteral("normal")) {
    mImageAnimationModePref = imgIContainer::kNormalAnimMode;
  } else if (animatePref.EqualsLiteral("none")) {
    mImageAnimationModePref = imgIContainer::kDontAnimMode;
  } else if (animatePref.EqualsLiteral("once")) {
    mImageAnimationModePref = imgIContainer::kLoopOnceAnimMode;
  } else {  // dynamic change to invalid value should act like it does initially
    mImageAnimationModePref = imgIContainer::kNormalAnimMode;
  }

  uint32_t bidiOptions = GetBidi();

  SET_BIDI_OPTION_DIRECTION(bidiOptions, StaticPrefs::bidi_direction());
  SET_BIDI_OPTION_TEXTTYPE(bidiOptions, StaticPrefs::bidi_texttype());
  SET_BIDI_OPTION_NUMERAL(bidiOptions, StaticPrefs::bidi_numeral());

  // We don't need to force reflow: either we are initializing a new
  // prescontext or we are being called from PreferenceChanged()
  // which triggers a reflow anyway.
  SetBidi(bidiOptions);
}

void nsPresContext::InvalidatePaintedLayers() {
  if (!mPresShell) {
    return;
  }
  if (nsIFrame* rootFrame = mPresShell->GetRootFrame()) {
    // FrameLayerBuilder caches invalidation-related values that depend on the
    // appunits-per-dev-pixel ratio, so ensure that all PaintedLayer drawing
    // is completely flushed.
    rootFrame->InvalidateFrameSubtree();
  }
}

void nsPresContext::AppUnitsPerDevPixelChanged() {
  int32_t oldAppUnitsPerDevPixel = mCurAppUnitsPerDevPixel;

  InvalidatePaintedLayers();

  FlushFontCache();

  mCurAppUnitsPerDevPixel = mDeviceContext->AppUnitsPerDevPixel();

#ifdef ACCESSIBILITY
  if (mCurAppUnitsPerDevPixel != oldAppUnitsPerDevPixel) {
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->NotifyOfDevPixelRatioChange(mPresShell,
                                              mCurAppUnitsPerDevPixel);
    }
  }
#endif

  // Recompute the size for vh units since it's changed by the dynamic toolbar
  // max height which is stored in screen coord.
  if (IsRootContentDocumentCrossProcess()) {
    AdjustSizeForViewportUnits();
  }

  // nsSubDocumentFrame uses a AppUnitsPerDevPixel difference between parent and
  // child document to determine if it needs to build a nsDisplayZoom item. So
  // if we that changes then we need to invalidate the subdoc frame so that
  // item gets created/removed.
  if (mPresShell) {
    if (nsIFrame* frame = mPresShell->GetRootFrame()) {
      frame = nsLayoutUtils::GetCrossDocParentFrameInProcess(frame);
      if (frame) {
        int32_t parentAPD = frame->PresContext()->AppUnitsPerDevPixel();
        if ((parentAPD == oldAppUnitsPerDevPixel) !=
            (parentAPD == mCurAppUnitsPerDevPixel)) {
          frame->InvalidateFrame();
        }
      }
    }
  }

  MediaFeatureValuesChanged(
      {RestyleHint::RecascadeSubtree(), NS_STYLE_HINT_REFLOW,
       MediaFeatureChangeReason::ResolutionChange},
      MediaFeatureChangePropagation::JustThisDocument);

  // We would also have to look at all of our child subdocuments but the
  // InvalidatePaintedLayers call above calls InvalidateFrameSubtree which
  // would invalidate all subdocument frames already.
}

// static
void nsPresContext::PreferenceChanged(const char* aPrefName, void* aSelf) {
  static_cast<nsPresContext*>(aSelf)->PreferenceChanged(aPrefName);
}

void nsPresContext::PreferenceChanged(const char* aPrefName) {
  if (!mPresShell) {
    return;
  }

  nsDependentCString prefName(aPrefName);
  if (prefName.EqualsLiteral("layout.css.dpi") ||
      prefName.EqualsLiteral("layout.css.devPixelsPerPx")) {
    int32_t oldAppUnitsPerDevPixel = mDeviceContext->AppUnitsPerDevPixel();
    // We need to assume the DPI changes, since `mDeviceContext` is shared with
    // other documents, and we'd need to save the return value of the first call
    // for all of them.
    Unused << mDeviceContext->CheckDPIChange();
    OwningNonNull<mozilla::PresShell> presShell(*mPresShell);
    // Re-fetch the view manager's window dimensions in case there's a
    // deferred resize which hasn't affected our mVisibleArea yet
    nscoord oldWidthAppUnits, oldHeightAppUnits;
    RefPtr<nsViewManager> vm = presShell->GetViewManager();
    if (!vm) {
      return;
    }
    vm->GetWindowDimensions(&oldWidthAppUnits, &oldHeightAppUnits);
    float oldWidthDevPixels = oldWidthAppUnits / oldAppUnitsPerDevPixel;
    float oldHeightDevPixels = oldHeightAppUnits / oldAppUnitsPerDevPixel;

    UIResolutionChangedInternal();

    nscoord width = NSToCoordRound(oldWidthDevPixels * AppUnitsPerDevPixel());
    nscoord height = NSToCoordRound(oldHeightDevPixels * AppUnitsPerDevPixel());
    vm->SetWindowDimensions(width, height);
    return;
  }

  if (StringBeginsWith(prefName, "browser.viewport."_ns) ||
      StringBeginsWith(prefName, "font.size.inflation."_ns) ||
      prefName.EqualsLiteral("dom.meta-viewport.enabled")) {
    mPresShell->MaybeReflowForInflationScreenSizeChange();
  }

  auto changeHint = nsChangeHint{0};
  auto restyleHint = RestyleHint{0};
  if (prefName.EqualsLiteral(GFX_MISSING_FONTS_NOTIFY_PREF)) {
    if (StaticPrefs::gfx_missing_fonts_notify()) {
      if (!mMissingFonts) {
        mMissingFonts = MakeUnique<gfxMissingFontRecorder>();
        // trigger reflow to detect missing fonts on the current page
        changeHint |= NS_STYLE_HINT_REFLOW;
      }
    } else {
      if (mMissingFonts) {
        mMissingFonts->Clear();
      }
      mMissingFonts = nullptr;
    }
  }

  if (StringBeginsWith(prefName, "font."_ns) ||
      // Changes to font family preferences don't change anything in the
      // computed style data, so the style system won't generate a reflow hint
      // for us.  We need to do that manually.
      prefName.EqualsLiteral("intl.accept_languages") ||
      // Changes to bidi prefs need to trigger a reflow (see bug 443629)
      StringBeginsWith(prefName, "bidi."_ns) ||
      // Changes to font_rendering prefs need to trigger a reflow
      StringBeginsWith(prefName, "gfx.font_rendering."_ns)) {
    changeHint |= NS_STYLE_HINT_REFLOW;
    if (StyleSet()->UsesFontMetrics()) {
      restyleHint |= RestyleHint::RecascadeSubtree();
    }
  }

  if (prefName.EqualsLiteral(
          "layout.css.text-transform.uppercase-eszett.enabled") ||
      prefName.EqualsLiteral("layout.css.letter-spacing.model")) {
    changeHint |= NS_STYLE_HINT_REFLOW;
  }

  // Same, this just frees a bunch of memory.
  StaticPresData::Get()->InvalidateFontPrefs();
  Document()->SetMayNeedFontPrefsUpdate();

  // Initialize our state from the user preferences.
  GetUserPreferences();

  FlushFontCache();
  if (UpdateFontVisibility()) {
    changeHint |= NS_STYLE_HINT_REFLOW;
  }

  // Preferences require rerunning selector matching because we rebuild
  // the pref style sheet for some preference changes.
  if (changeHint || restyleHint) {
    RebuildAllStyleData(changeHint, restyleHint);
  }

  InvalidatePaintedLayers();
}

nsresult nsPresContext::Init(nsDeviceContext* aDeviceContext) {
  NS_ASSERTION(!mInitialized, "attempt to reinit pres context");
  NS_ENSURE_ARG(aDeviceContext);

  mDeviceContext = aDeviceContext;

  // In certain rare cases (such as changing page mode), we tear down layout
  // state and re-initialize a new prescontext for a document. Given that we
  // hang style state off the DOM, we detect that re-initialization case and
  // lazily drop the servo data. We don't do this eagerly during layout teardown
  // because that would incur an extra whole-tree traversal that's unnecessary
  // most of the time.
  //
  // FIXME(emilio): I'm pretty sure this doesn't happen after bug 1414999.
  Element* root = mDocument->GetRootElement();
  if (root && root->HasServoData()) {
    RestyleManager::ClearServoDataFromSubtree(root);
  }

  if (mDeviceContext->SetFullZoom(mFullZoom)) {
    FlushFontCache();
  }
  mCurAppUnitsPerDevPixel = mDeviceContext->AppUnitsPerDevPixel();

  mEventManager = new mozilla::EventStateManager();

  mAnimationEventDispatcher = new mozilla::AnimationEventDispatcher(this);
  mEffectCompositor = new mozilla::EffectCompositor(this);
  mTransitionManager = MakeUnique<nsTransitionManager>(this);
  mAnimationManager = MakeUnique<nsAnimationManager>(this);
  mTimelineManager = MakeUnique<mozilla::TimelineManager>(this);

  if (mDocument->GetDisplayDocument()) {
    NS_ASSERTION(mDocument->GetDisplayDocument()->GetPresContext(),
                 "Why are we being initialized?");
    mRefreshDriver =
        mDocument->GetDisplayDocument()->GetPresContext()->RefreshDriver();
  } else {
    dom::Document* parent = mDocument->GetInProcessParentDocument();
    // Unfortunately, sometimes |parent| here has no presshell because
    // printing screws up things.  Assert that in other cases it does,
    // but whenever the shell is null just fall back on using our own
    // refresh driver.
    NS_ASSERTION(
        !parent || mDocument->IsStaticDocument() || parent->GetPresShell(),
        "How did we end up with a presshell if our parent doesn't "
        "have one?");
    if (parent && parent->GetPresContext()) {
      // XXX the document can change in AttachPresShell, does this work?
      dom::BrowsingContext* browsingContext = mDocument->GetBrowsingContext();
      if (browsingContext && !browsingContext->IsTop()) {
        Element* containingElement = mDocument->GetEmbedderElement();
        if (!containingElement->IsXULElement() ||
            !containingElement->HasAttr(nsGkAtoms::forceOwnRefreshDriver)) {
          mRefreshDriver = parent->GetPresContext()->RefreshDriver();
        }
      }
    }

    if (!mRefreshDriver) {
      mRefreshDriver = new nsRefreshDriver(this);
    }
  }

  // Register callbacks so we're notified when the preferences change
  Preferences::RegisterPrefixCallbacks(nsPresContext::PreferenceChanged,
                                       gPrefixCallbackPrefs, this);
  Preferences::RegisterCallbacks(nsPresContext::PreferenceChanged,
                                 gExactCallbackPrefs, this);

  nsresult rv = mEventManager->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  mEventManager->SetPresContext(this);

#if defined(MOZ_WIDGET_ANDROID)
  if (IsRootContentDocumentCrossProcess()) {
    if (BrowserChild* browserChild = BrowserChild::GetFrom(GetDocShell())) {
      if (MOZ_LIKELY(!Preferences::HasUserValue(
              "layout.dynamic-toolbar-max-height"))) {
        mDynamicToolbarMaxHeight = browserChild->GetDynamicToolbarMaxHeight();
        mDynamicToolbarHeight = mDynamicToolbarMaxHeight;
      }
    }
  }
#endif

#ifdef DEBUG
  mInitialized = true;
#endif

  return NS_OK;
}

void nsPresContext::UpdateForcedColors(bool aNotify) {
  auto old = mForcedColors;
  mForcedColors = [&] {
    if (Document()->IsBeingUsedAsImage()) {
      return StyleForcedColors::None;
    }

    // Handle BrowsingContext override.
    if (auto* bc = mDocument->GetBrowsingContext();
        bc &&
        bc->Top()->ForcedColorsOverride() == ForcedColorsOverride::Active) {
      return StyleForcedColors::Active;
    }

    const auto& prefs = PrefSheetPrefs();
    if (!prefs.mUseDocumentColors) {
      return StyleForcedColors::Active;
    }
    // On Windows, having a high contrast theme also means that the OS is
    // requesting the colors to be forced. This is mostly convenience for the
    // front-end, which wants to reuse the forced-colors styles for chrome in
    // this case as well, and it's a lot more convenient to use
    // `(forced-colors)` than `(forced-colors) or ((-moz-platform: windows) and
    // (prefers-contrast))`.
    //
    // TODO(emilio): We might want to factor in here the lwtheme attribute in
    // the root element and so on.
#ifdef XP_WIN
    if (prefs.mUseAccessibilityTheme && prefs.mIsChrome) {
      return StyleForcedColors::Requested;
    }
#endif
    return StyleForcedColors::None;
  }();
  if (aNotify && mForcedColors != old) {
    MediaFeatureValuesChanged(
        MediaFeatureChange::ForPreferredColorSchemeOrForcedColorsChange(),
        MediaFeatureChangePropagation::JustThisDocument);
  }
}

bool nsPresContext::ForcingColors() const {
  return mForcedColors == StyleForcedColors::Active;
}

bool nsPresContext::UpdateFontVisibility() {
  FontVisibility oldValue = mFontVisibility;

  /*
   * Expected behavior in order of precedence:
   *  1  Chrome Rules give User Level (3)
   *  2  RFP gives Highest Level (1 aka Base)
   *  3  An RFPTarget of Base gives Base Level (1)
   *  4  An RFPTarget of LangPack gives LangPack Level (2)
   *  5  The value of the Standard Font Visibility Pref
   *
   * If the ETP toggle is disabled (aka
   * ContentBlockingAllowList::Check is true), it will only override 3-5,
   * not rules 1 or 2.
   */

  // Rule 1: Allow all font access for privileged contexts, including
  // chrome and devtools contexts.
  if (Document()->ChromeRulesEnabled()) {
    mFontVisibility = FontVisibility::User;
    return mFontVisibility != oldValue;
  }

  // Is this a private browsing context?
  bool isPrivate = false;
  if (nsCOMPtr<nsILoadContext> loadContext = mDocument->GetLoadContext()) {
    isPrivate = loadContext->UsePrivateBrowsing();
  }

  int32_t level;
  // Rule 3
  if (mDocument->ShouldResistFingerprinting(
          RFPTarget::FontVisibilityBaseSystem)) {
    // Rule 2: Check RFP pref
    // This is inside Rule 3 in case this document is exempted from RFP.
    // But if it is not exempted, and RFP is enabled, we return immediately
    // to prevent the override below from occurring.
    if (nsRFPService::IsRFPPrefEnabled(isPrivate)) {
      mFontVisibility = FontVisibility::Base;
      return mFontVisibility != oldValue;
    }

    level = int32_t(FontVisibility::Base);
  }
  // Rule 4
  else if (mDocument->ShouldResistFingerprinting(
               RFPTarget::FontVisibilityLangPack)) {
    level = int32_t(FontVisibility::LangPack);
  }
  // Rule 5
  else {
    level = StaticPrefs::layout_css_font_visibility();
  }

  // Override Rules 3-5 Only: Determine if the user has exempted the
  // domain from tracking protections, if so, use the default value.
  if (level != StaticPrefs::layout_css_font_visibility() &&
      ContentBlockingAllowList::Check(mDocument->CookieJarSettings())) {
    level = StaticPrefs::layout_css_font_visibility();
  }

  // Clamp result to the valid range of levels.
  level = std::clamp(level, int32_t(FontVisibility::Base),
                     int32_t(FontVisibility::User));

  mFontVisibility = FontVisibility(level);
  return mFontVisibility != oldValue;
}

void nsPresContext::ReportBlockedFontFamilyName(const nsCString& aFamily,
                                                FontVisibility aVisibility) {
  if (!mBlockedFonts.EnsureInserted(aFamily)) {
    return;
  }
  nsAutoString msg;
  msg.AppendPrintf(
      "Request for font \"%s\" blocked at visibility level %d (requires %d)\n",
      aFamily.get(), int(GetFontVisibility()), int(aVisibility));
  nsContentUtils::ReportToConsoleNonLocalized(msg, nsIScriptError::warningFlag,
                                              "Security"_ns, mDocument);
}

void nsPresContext::ReportBlockedFontFamily(const fontlist::Family& aFamily) {
  auto* fontList = gfxPlatformFontList::PlatformFontList()->SharedFontList();
  const nsCString& name = aFamily.DisplayName().AsString(fontList);
  ReportBlockedFontFamilyName(name, aFamily.Visibility());
}

void nsPresContext::ReportBlockedFontFamily(const gfxFontFamily& aFamily) {
  ReportBlockedFontFamilyName(aFamily.Name(), aFamily.Visibility());
}

void nsPresContext::InitFontCache() {
  if (!mFontCache) {
    mFontCache = new nsFontCache();
    mFontCache->Init(this);
  }
}

void nsPresContext::UpdateFontCacheUserFonts(gfxUserFontSet* aUserFontSet) {
  if (mFontCache) {
    mFontCache->UpdateUserFonts(aUserFontSet);
  }
}

already_AddRefed<nsFontMetrics> nsPresContext::GetMetricsFor(
    const nsFont& aFont, const nsFontMetrics::Params& aParams) {
  InitFontCache();
  return mFontCache->GetMetricsFor(aFont, aParams);
}

nsresult nsPresContext::FlushFontCache() {
  if (mFontCache) {
    mFontCache->Flush();
  }
  return NS_OK;
}

nsresult nsPresContext::FontMetricsDeleted(const nsFontMetrics* aFontMetrics) {
  if (mFontCache) {
    mFontCache->FontMetricsDeleted(aFontMetrics);
  }
  return NS_OK;
}

// Note: We don't hold a reference on the shell; it has a reference to
// us
void nsPresContext::AttachPresShell(mozilla::PresShell* aPresShell) {
  MOZ_ASSERT(!mPresShell);
  mPresShell = aPresShell;

  mRestyleManager = MakeUnique<mozilla::RestyleManager>(this);

  // Since CounterStyleManager is also the name of a method of
  // nsPresContext, it is necessary to prefix the class with the mozilla
  // namespace here.
  mCounterStyleManager = new mozilla::CounterStyleManager(this);

  dom::Document* doc = mPresShell->GetDocument();
  MOZ_ASSERT(doc);
  // Have to update PresContext's mDocument before calling any other methods.
  mDocument = doc;

  LookAndFeel::HandleGlobalThemeChange();

  // Initialize our state from the user preferences, now that we
  // have a presshell, and hence a document.
  GetUserPreferences();

  EnsureTheme();

  nsIURI* docURI = doc->GetDocumentURI();

  if (IsDynamic() && docURI) {
    if (!docURI->SchemeIs("chrome") && !docURI->SchemeIs("resource")) {
      mImageAnimationMode = mImageAnimationModePref;
    } else {
      mImageAnimationMode = imgIContainer::kNormalAnimMode;
    }
  }

  UpdateCharSet(doc->GetDocumentCharacterSet());
}

Maybe<ColorScheme> nsPresContext::GetOverriddenOrEmbedderColorScheme() const {
  if (Medium() == nsGkAtoms::print) {
    return Some(ColorScheme::Light);
  }

  switch (mOverriddenOrEmbedderColorScheme) {
    case dom::PrefersColorSchemeOverride::Dark:
      return Some(ColorScheme::Dark);
    case dom::PrefersColorSchemeOverride::Light:
      return Some(ColorScheme::Light);
    case dom::PrefersColorSchemeOverride::None:
      break;
  }

  return Nothing();
}

void nsPresContext::SetColorSchemeOverride(
    PrefersColorSchemeOverride aOverride) {
  auto oldScheme = mDocument->PreferredColorScheme();

  mOverriddenOrEmbedderColorScheme = aOverride;

  if (mDocument->PreferredColorScheme() != oldScheme) {
    MediaFeatureValuesChanged(
        MediaFeatureChange::ForPreferredColorSchemeOrForcedColorsChange(),
        MediaFeatureChangePropagation::JustThisDocument);
  }
}

void nsPresContext::RecomputeBrowsingContextDependentData() {
  MOZ_ASSERT(mDocument);
  dom::Document* doc = mDocument;
  // Resource documents inherit all this state from their display document.
  while (dom::Document* outer = doc->GetDisplayDocument()) {
    doc = outer;
  }
  auto* browsingContext = doc->GetBrowsingContext();
  if (!browsingContext) {
    // This can legitimately happen for e.g. SVG images. Those just get scaled
    // as a result of the zoom on the embedder document so it doesn't really
    // matter... Medium also doesn't affect those.
    return;
  }
  if (!IsPrintingOrPrintPreview()) {
    auto systemZoom = LookAndFeel::SystemZoomSettings();
    SetFullZoom(browsingContext->FullZoom() * systemZoom.mFullZoom);
    SetTextZoom(browsingContext->TextZoom() * systemZoom.mTextZoom);
    SetOverrideDPPX(browsingContext->OverrideDPPX());
  }

  auto* top = browsingContext->Top();
  SetColorSchemeOverride([&] {
    auto overriden = top->PrefersColorSchemeOverride();
    if (overriden != PrefersColorSchemeOverride::None) {
      return overriden;
    }
    if (!StaticPrefs::
            layout_css_iframe_embedder_prefers_color_scheme_content_enabled()) {
      return top->GetEmbedderColorSchemes().mPreferred;
    }
    return browsingContext->GetEmbedderColorSchemes().mPreferred;
  }());

  UpdateForcedColors();

  SetInRDMPane(top->GetInRDMPane());

  if (doc == mDocument) {
    // Medium doesn't apply to resource documents, etc.
    RefPtr<nsAtom> mediumToEmulate;
    if (MOZ_UNLIKELY(!top->GetMediumOverride().IsEmpty())) {
      nsAutoString lower;
      nsContentUtils::ASCIIToLower(top->GetMediumOverride(), lower);
      mediumToEmulate = NS_Atomize(lower);
    }
    EmulateMedium(mediumToEmulate);
  }

  mDocument->EnumerateExternalResources([](dom::Document& aSubResource) {
    if (nsPresContext* subResourcePc = aSubResource.GetPresContext()) {
      subResourcePc->RecomputeBrowsingContextDependentData();
    }
    return CallState::Continue;
  });
}

void nsPresContext::DetachPresShell() {
  // The counter style manager's destructor needs to deallocate with the
  // presshell arena. Disconnect it before nulling out the shell.
  //
  // XXXbholley: Given recent refactorings, it probably makes more sense to
  // just null our mPresShell at the bottom of this function. I'm leaving it
  // this way to preserve the old ordering, but I doubt anything would break.
  if (mCounterStyleManager) {
    mCounterStyleManager->Disconnect();
    mCounterStyleManager = nullptr;
  }

  mPresShell = nullptr;

  CancelManagedPostRefreshObservers();

  if (mAnimationEventDispatcher) {
    mAnimationEventDispatcher->Disconnect();
    mAnimationEventDispatcher = nullptr;
  }
  if (mEffectCompositor) {
    mEffectCompositor->Disconnect();
    mEffectCompositor = nullptr;
  }
  if (mTransitionManager) {
    mTransitionManager->Disconnect();
    mTransitionManager = nullptr;
  }
  if (mAnimationManager) {
    mAnimationManager->Disconnect();
    mAnimationManager = nullptr;
  }
  if (mTimelineManager) {
    mTimelineManager->Disconnect();
    mTimelineManager = nullptr;
  }
  if (mRestyleManager) {
    mRestyleManager->Disconnect();
    mRestyleManager = nullptr;
  }
  if (mRefreshDriver && mRefreshDriver->GetPresContext() == this) {
    mRefreshDriver->Disconnect();
    // Can't null out the refresh driver here.
  }
}

struct QueryContainerState {
  nsSize mSize;
  WritingMode mWm;
  StyleContainerType mType;

  nscoord GetInlineSize() const { return LogicalSize(mWm, mSize).ISize(mWm); }

  bool Changed(const QueryContainerState& aNewState) {
    if (mType != aNewState.mType) {
      return true;
    }
    switch (mType) {
      case StyleContainerType::Normal:
        break;
      case StyleContainerType::Size:
        return mSize != aNewState.mSize;
      case StyleContainerType::InlineSize:
        return GetInlineSize() != aNewState.GetInlineSize();
    }
    return false;
  }
};
NS_DECLARE_FRAME_PROPERTY_DELETABLE(ContainerState, QueryContainerState);

void nsPresContext::RegisterContainerQueryFrame(nsIFrame* aFrame) {
  mContainerQueryFrames.Add(aFrame);
}

void nsPresContext::UnregisterContainerQueryFrame(nsIFrame* aFrame) {
  mContainerQueryFrames.Remove(aFrame);
}

void nsPresContext::FinishedContainerQueryUpdate() {
  mUpdatedContainerQueryContents.Clear();
}

bool nsPresContext::UpdateContainerQueryStyles() {
  if (mContainerQueryFrames.IsEmpty()) {
    return false;
  }

  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Container Query Styles Update", LAYOUT);
  AUTO_PROFILER_MARKER_UNTYPED("UpdateContainerQueryStyles", LAYOUT, {});

  PresShell()->DoFlushLayout(/* aInterruptible = */ false);

  AutoTArray<nsIFrame*, 8> framesToUpdate;

  bool anyChanged = false;
  for (nsIFrame* frame : mContainerQueryFrames.IterFromShallowest()) {
    MOZ_ASSERT(frame->IsPrimaryFrame());

    auto type = frame->StyleDisplay()->mContainerType;
    MOZ_ASSERT(type != StyleContainerType::Normal,
               "Non-container frames shouldn't be in this set");

    const QueryContainerState newState{frame->GetSize(),
                                       frame->GetWritingMode(), type};
    QueryContainerState* oldState = frame->GetProperty(ContainerState());

    const bool changed = !oldState || oldState->Changed(newState);

    // Make sure to update the state regardless. It's cheap and it keeps tracks
    // of both axes correctly even if only one axis is contained.
    if (oldState) {
      *oldState = newState;
    } else {
      frame->SetProperty(ContainerState(), new QueryContainerState(newState));
    }

    if (!changed) {
      continue;
    }

    const bool updatingAncestor = [&] {
      for (nsIFrame* f : framesToUpdate) {
        if (nsLayoutUtils::IsProperAncestorFrame(f, frame)) {
          return true;
        }
      }
      return false;
    }();

    if (updatingAncestor) {
      // We're going to update an ancestor container of this frame already,
      // avoid updating this one too until all our ancestor containers are
      // updated.
      continue;
    }

    // To prevent unstable layout, only update once per-element per-flush.
    if (NS_WARN_IF(!mUpdatedContainerQueryContents.EnsureInserted(
            frame->GetContent()))) {
      continue;
    }

    framesToUpdate.AppendElement(frame);

    // TODO(emilio): More fine-grained invalidation rather than invalidating the
    // whole subtree, probably!
    RestyleManager()->PostRestyleEvent(frame->GetContent()->AsElement(),
                                       RestyleHint::RestyleSubtree(),
                                       nsChangeHint(0));
    anyChanged = true;
  }
  return anyChanged;
}

void nsPresContext::DocumentCharSetChanged(NotNull<const Encoding*> aCharSet) {
  UpdateCharSet(aCharSet);
  FlushFontCache();

  // If a document contains one or more <script> elements, frame construction
  // might happen earlier than the UpdateCharSet(), so we need to restyle
  // descendants to make their style data up-to-date.
  //
  // FIXME(emilio): Revisit whether this is true after bug 1438911.
  RebuildAllStyleData(NS_STYLE_HINT_REFLOW, RestyleHint::RecascadeSubtree());
}

void nsPresContext::UpdateCharSet(NotNull<const Encoding*> aCharSet) {
  switch (GET_BIDI_OPTION_TEXTTYPE(GetBidi())) {
    case IBMBIDI_TEXTTYPE_LOGICAL:
      SetVisualMode(false);
      break;

    case IBMBIDI_TEXTTYPE_VISUAL:
      SetVisualMode(true);
      break;

    case IBMBIDI_TEXTTYPE_CHARSET:
    default:
      SetVisualMode(IsVisualCharset(aCharSet));
  }
}

nsPresContext* nsPresContext::GetParentPresContext() const {
  mozilla::PresShell* presShell = GetPresShell();
  if (presShell) {
    nsViewManager* viewManager = presShell->GetViewManager();
    if (viewManager) {
      nsView* view = viewManager->GetRootView();
      if (view) {
        view = view->GetParent();  // anonymous inner view
        if (view) {
          view = view->GetParent();  // subdocumentframe's view
          if (view) {
            nsIFrame* f = view->GetFrame();
            if (f) {
              return f->PresContext();
            }
          }
        }
      }
    }
  }
  return nullptr;
}

nsPresContext* nsPresContext::GetInProcessRootContentDocumentPresContext() {
  if (IsChrome()) {
    return nullptr;
  }
  nsPresContext* pc = this;
  for (;;) {
    nsPresContext* parent = pc->GetParentPresContext();
    if (!parent || parent->IsChrome()) {
      return pc;
    }
    pc = parent;
  }
}

nsIWidget* nsPresContext::GetNearestWidget(nsPoint* aOffset) {
  NS_ENSURE_TRUE(mPresShell, nullptr);
  nsViewManager* vm = mPresShell->GetViewManager();
  NS_ENSURE_TRUE(vm, nullptr);
  nsView* rootView = vm->GetRootView();
  NS_ENSURE_TRUE(rootView, nullptr);
  return rootView->GetNearestWidget(aOffset);
}

nsIWidget* nsPresContext::GetRootWidget() const {
  NS_ENSURE_TRUE(mPresShell, nullptr);
  nsViewManager* vm = mPresShell->GetViewManager();
  if (!vm) {
    return nullptr;
  }
  return vm->GetRootWidget();
}

// We may want to replace this with something faster, maybe caching the root
// prescontext
nsRootPresContext* nsPresContext::GetRootPresContext() const {
  nsPresContext* pc = const_cast<nsPresContext*>(this);
  for (;;) {
    nsPresContext* parent = pc->GetParentPresContext();
    if (!parent) {
      break;
    }
    pc = parent;
  }
  return pc->IsRoot() ? static_cast<nsRootPresContext*>(pc) : nullptr;
}

bool nsPresContext::UserInputEventsAllowed() {
  MOZ_ASSERT(IsRoot());
  if (mUserInputEventsAllowed) {
    return true;
  }

  // Special document
  if (Document()->IsEverInitialDocument()) {
    return true;
  }

  if (mRefreshDriver->IsThrottled()) {
    MOZ_ASSERT(!mPresShell->IsVisible());
    // This implies that the BC is not visibile and users can't
    // interact with it, so we are okay with handling user inputs here.
    return true;
  }

  if (mMeasuredTicksSinceLoading <
      StaticPrefs::dom_input_events_security_minNumTicks()) {
    return false;
  }

  if (!StaticPrefs::dom_input_events_security_minTimeElapsedInMS()) {
    return true;
  }

  dom::Document* doc = Document();

  MOZ_ASSERT_IF(StaticPrefs::dom_input_events_security_minNumTicks(),
                doc->GetReadyStateEnum() >= Document::READYSTATE_LOADING);

  TimeStamp loadingOrRestoredFromBFCacheTime =
      doc->GetLoadingOrRestoredFromBFCacheTimeStamp();
  MOZ_ASSERT(!loadingOrRestoredFromBFCacheTime.IsNull());

  TimeDuration elapsed = TimeStamp::Now() - loadingOrRestoredFromBFCacheTime;
  if (elapsed.ToMilliseconds() >=
      StaticPrefs::dom_input_events_security_minTimeElapsedInMS()) {
    mUserInputEventsAllowed = true;
    return true;
  }

  return false;
}

void nsPresContext::MaybeIncreaseMeasuredTicksSinceLoading() {
  MOZ_ASSERT(IsRoot());
  if (mMeasuredTicksSinceLoading >=
      StaticPrefs::dom_input_events_security_minNumTicks()) {
    return;
  }

  // We consider READYSTATE_LOADING is the point when the page
  // becomes interactive
  if (Document()->GetReadyStateEnum() >= Document::READYSTATE_LOADING ||
      Document()->IsInitialDocument()) {
    ++mMeasuredTicksSinceLoading;
  }

  if (mMeasuredTicksSinceLoading <
      StaticPrefs::dom_input_events_security_minNumTicks()) {
    // Here we are forcing refresh driver to run because we can't always
    // guarantee refresh driver will run enough times to meet the minNumTicks
    // requirement. i.e. about:blank.
    if (!RefreshDriver()->HasPendingTick()) {
      RefreshDriver()->InitializeTimer();
    }
  }
}

bool nsPresContext::NeedsMoreTicksForUserInput() const {
  MOZ_ASSERT(IsRoot());
  return mMeasuredTicksSinceLoading <
         StaticPrefs::dom_input_events_security_minNumTicks();
}

// Helper function for setting Anim Mode on image
static void SetImgAnimModeOnImgReq(imgIRequest* aImgReq, uint16_t aMode) {
  if (aImgReq) {
    nsCOMPtr<imgIContainer> imgCon;
    aImgReq->GetImage(getter_AddRefs(imgCon));
    if (imgCon) {
      imgCon->SetAnimationMode(aMode);
    }
  }
}

// IMPORTANT: Assumption is that all images for a Presentation
// have the same Animation Mode (pavlov said this was OK)
//
// Walks content and set the animation mode
// this is a way to turn on/off image animations
void nsPresContext::SetImgAnimations(nsIContent* aParent, uint16_t aMode) {
  nsCOMPtr<nsIImageLoadingContent> imgContent(do_QueryInterface(aParent));
  if (imgContent) {
    nsCOMPtr<imgIRequest> imgReq;
    imgContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                           getter_AddRefs(imgReq));
    SetImgAnimModeOnImgReq(imgReq, aMode);
  }

  for (nsIContent* childContent = aParent->GetFirstChild(); childContent;
       childContent = childContent->GetNextSibling()) {
    SetImgAnimations(childContent, aMode);
  }
}

void nsPresContext::SetSMILAnimations(dom::Document* aDoc, uint16_t aNewMode,
                                      uint16_t aOldMode) {
  if (aDoc->HasAnimationController()) {
    SMILAnimationController* controller = aDoc->GetAnimationController();
    switch (aNewMode) {
      case imgIContainer::kNormalAnimMode:
      case imgIContainer::kLoopOnceAnimMode:
        if (aOldMode == imgIContainer::kDontAnimMode) {
          controller->Resume(SMILTimeContainer::PAUSE_USERPREF);
        }
        break;

      case imgIContainer::kDontAnimMode:
        if (aOldMode != imgIContainer::kDontAnimMode) {
          controller->Pause(SMILTimeContainer::PAUSE_USERPREF);
        }
        break;
    }
  }
}

void nsPresContext::SetImageAnimationMode(uint16_t aMode) {
  NS_ASSERTION(aMode == imgIContainer::kNormalAnimMode ||
                   aMode == imgIContainer::kDontAnimMode ||
                   aMode == imgIContainer::kLoopOnceAnimMode,
               "Wrong Animation Mode is being set!");

  // Image animation mode cannot be changed when rendering to a printer.
  if (!IsDynamic()) {
    return;
  }

  // Now walk the content tree and set the animation mode
  // on all the images.
  if (mPresShell) {
    dom::Document* doc = mPresShell->GetDocument();
    if (doc) {
      doc->StyleImageLoader()->SetAnimationMode(aMode);

      Element* rootElement = doc->GetRootElement();
      if (rootElement) {
        SetImgAnimations(rootElement, aMode);
      }
      SetSMILAnimations(doc, aMode, mImageAnimationMode);
    }
  }

  mImageAnimationMode = aMode;
}

void nsPresContext::SetTextZoom(float aTextZoom) {
  float newZoom = aTextZoom;
  float minZoom = StaticPrefs::zoom_minPercent() / 100.0f;
  float maxZoom = StaticPrefs::zoom_maxPercent() / 100.0f;

  if (newZoom < minZoom) {
    newZoom = minZoom;
  } else if (newZoom > maxZoom) {
    newZoom = maxZoom;
  }

  if (newZoom == mTextZoom) {
    return;
  }

  mTextZoom = newZoom;

  // Media queries could have changed, since we changed the meaning
  // of 'em' units in them.
  MediaFeatureValuesChanged(
      {RestyleHint::RecascadeSubtree(), NS_STYLE_HINT_REFLOW,
       MediaFeatureChangeReason::ZoomChange},
      MediaFeatureChangePropagation::JustThisDocument);
}

void nsPresContext::SetInRDMPane(bool aInRDMPane) {
  if (mInRDMPane == aInRDMPane) {
    return;
  }
  mInRDMPane = aInRDMPane;
  RecomputeTheme();
  if (mPresShell) {
    nsContentUtils::AddScriptRunner(NewRunnableMethod<bool>(
        "PresShell::MaybeRecreateMobileViewportManager", mPresShell,
        &PresShell::MaybeRecreateMobileViewportManager, true));
  }
}

float nsPresContext::GetDeviceFullZoom() {
  return mDeviceContext->GetFullZoom();
}

void nsPresContext::SetFullZoom(float aZoom) {
  if (!mPresShell) {
    return;
  }

  // Sanitize aZoom to check for bogus values. (Outer iframes with small or
  // huge css 'zoom' can end up stacking to propagate an extremely small/huge
  // full-zoom value to inner iframes, possibly reaching 0 or infinity. We
  // handle that edge case by just falling back to 1.0f here, so we can render
  // something, and particularly so we don't do something invalid like trying
  // to allocate a zero-sized or infinite-sized surface.)
  if (MOZ_UNLIKELY(!std::isfinite(aZoom) || aZoom <= 0.0f)) {
    aZoom = 1.0f;
  }

  if (mFullZoom == aZoom) {
    return;
  }

  // Re-fetch the view manager's window dimensions in case there's a deferred
  // resize which hasn't affected our mVisibleArea yet
  nscoord oldWidthAppUnits, oldHeightAppUnits;
  mPresShell->GetViewManager()->GetWindowDimensions(&oldWidthAppUnits,
                                                    &oldHeightAppUnits);
  float oldWidthDevPixels = oldWidthAppUnits / float(mCurAppUnitsPerDevPixel);
  float oldHeightDevPixels = oldHeightAppUnits / float(mCurAppUnitsPerDevPixel);
  mDeviceContext->SetFullZoom(aZoom);

  mFullZoom = aZoom;

  AppUnitsPerDevPixelChanged();

  mPresShell->GetViewManager()->SetWindowDimensions(
      NSToCoordRound(oldWidthDevPixels * AppUnitsPerDevPixel()),
      NSToCoordRound(oldHeightDevPixels * AppUnitsPerDevPixel()));
}

void nsPresContext::SetOverrideDPPX(float aDPPX) {
  // SetOverrideDPPX is called during navigations, including history
  // traversals.  In that case, it's typically called with our current value,
  // and we don't need to actually do anything.
  if (aDPPX == GetOverrideDPPX()) {
    return;
  }

  mMediaEmulationData.mDPPX = aDPPX;
  MediaFeatureValuesChanged({MediaFeatureChangeReason::ResolutionChange},
                            MediaFeatureChangePropagation::JustThisDocument);
}

void nsPresContext::UpdateTopInnerSizeForRFP() {
  if (!mDocument->ShouldResistFingerprinting(RFPTarget::WindowOuterSize) ||
      !mDocument->GetBrowsingContext() ||
      !mDocument->GetBrowsingContext()->IsTop()) {
    return;
  }

  CSSSize size = CSSPixel::FromAppUnits(GetVisibleArea().Size());

  switch (StaticPrefs::dom_innerSize_rounding()) {
    case 1:
      size.width = std::roundf(size.width);
      size.height = std::roundf(size.height);
      break;
    case 2:
      size.width = std::truncf(size.width);
      size.height = std::truncf(size.height);
      break;
    default:
      break;
  }

  Unused << mDocument->GetBrowsingContext()->SetTopInnerSizeForRFP(
      CSSIntSize{(int)size.width, (int)size.height});
}

gfxSize nsPresContext::ScreenSizeInchesForFontInflation(bool* aChanged) {
  if (aChanged) {
    *aChanged = false;
  }

  nsDeviceContext* dx = DeviceContext();
  float unitsPerInch = dx->AppUnitsPerPhysicalInch();
  nsRect clientRect = dx->GetClientRect();
  gfxSize deviceSizeInches(float(clientRect.width) / unitsPerInch,
                           float(clientRect.height) / unitsPerInch);

  if (mLastFontInflationScreenSize == gfxSize(-1.0, -1.0)) {
    mLastFontInflationScreenSize = deviceSizeInches;
  }

  if (deviceSizeInches != mLastFontInflationScreenSize && aChanged) {
    *aChanged = true;
    mLastFontInflationScreenSize = deviceSizeInches;
  }

  return deviceSizeInches;
}

static bool CheckOverflow(const ComputedStyle* aComputedStyle,
                          ScrollStyles* aStyles) {
  // If they're not styled yet, we'll get around to it when constructing frames
  // for the element.
  if (!aComputedStyle) {
    return false;
  }
  const nsStyleDisplay* display = aComputedStyle->StyleDisplay();

  // If they will generate no box, just don't.
  if (display->mDisplay == StyleDisplay::None ||
      display->mDisplay == StyleDisplay::Contents) {
    return false;
  }

  // NOTE(emilio): This check needs to match the one in
  // Document::IsPotentiallyScrollable.
  if (display->OverflowIsVisibleInBothAxis()) {
    return false;
  }

  *aStyles =
      ScrollStyles(*display, ScrollStyles::MapOverflowToValidScrollStyle);
  return true;
}

// https://drafts.csswg.org/css-overflow/#overflow-propagation
//
// NOTE(emilio): We may need to use out-of-date styles for this, since this is
// called from nsCSSFrameConstructor::ContentRemoved. We could refactor this a
// bit to avoid doing that, and also fix correctness issues (we don't invalidate
// properly when we insert a body element and there is a previous one, for
// example).
static Element* GetPropagatedScrollStylesForViewport(
    nsPresContext* aPresContext, const Element* aRemovedChild,
    ScrollStyles* aStyles) {
  Document* document = aPresContext->Document();
  Element* docElement = document->GetRootElement();
  // docElement might be null if we're doing this after removing it.
  if (!docElement || docElement == aRemovedChild) {
    return nullptr;
  }

  // Check the style on the document root element
  const auto* rootStyle = Servo_Element_GetMaybeOutOfDateStyle(docElement);
  if (CheckOverflow(rootStyle, aStyles)) {
    // tell caller we stole the overflow style from the root element
    return docElement;
  }

  if (rootStyle && rootStyle->StyleDisplay()->IsContainAny()) {
    return nullptr;
  }

  // Don't look in the BODY for non-HTML documents or HTML documents
  // with non-HTML roots.
  // XXX this should be earlier; we shouldn't even look at the document root
  // for non-HTML documents. Fix this once we support explicit CSS styling
  // of the viewport
  if (!document->IsHTMLOrXHTML() ||
      !docElement->IsHTMLElement(nsGkAtoms::html)) {
    return nullptr;
  }

  Element* bodyElement = document->GetBodyElement(aRemovedChild);
  if (!bodyElement) {
    return nullptr;
  }

  const auto* bodyStyle = Servo_Element_GetMaybeOutOfDateStyle(bodyElement);
  if (bodyStyle && bodyStyle->StyleDisplay()->IsContainAny()) {
    return nullptr;
  }

  if (CheckOverflow(bodyStyle, aStyles)) {
    // tell caller we stole the overflow style from the body element
    return bodyElement;
  }

  return nullptr;
}

Element* nsPresContext::UpdateViewportScrollStylesOverride(
    const Element* aRemovedChild) {
  ScrollStyles oldViewportScrollStyles = mViewportScrollStyles;

  // Start off with our default styles, and then update them as needed.
  mViewportScrollStyles =
      ScrollStyles(StyleOverflow::Auto, StyleOverflow::Auto);
  mViewportScrollOverrideElement = nullptr;
  // Don't propagate the scrollbar state in printing or print preview.
  if (!IsPaginated()) {
    mViewportScrollOverrideElement = GetPropagatedScrollStylesForViewport(
        this, aRemovedChild, &mViewportScrollStyles);
  }

  dom::Document* document = Document();
  if (Element* fsElement = document->GetUnretargetedFullscreenElement()) {
    // If the document is in fullscreen, but the fullscreen element is
    // not the root element, we should explicitly suppress the scrollbar
    // here. Note that, we still need to return the original element
    // the styles are from, so that the state of those elements is not
    // affected across fullscreen change.
    if (fsElement != document->GetRootElement() &&
        fsElement != mViewportScrollOverrideElement) {
      mViewportScrollStyles =
          ScrollStyles(StyleOverflow::Hidden, StyleOverflow::Hidden);
    }
  }

  if (mViewportScrollStyles != oldViewportScrollStyles) {
    if (mPresShell) {
      if (nsIFrame* frame = mPresShell->GetRootFrame()) {
        frame->SchedulePaint();
      }
    }
  }

  return mViewportScrollOverrideElement;
}

bool nsPresContext::ElementWouldPropagateScrollStyles(const Element& aElement) {
  if (aElement.GetParent() && !aElement.IsHTMLElement(nsGkAtoms::body)) {
    // We certainly won't be propagating from this element.
    return false;
  }

  // Go ahead and just call GetPropagatedScrollStylesForViewport, but update
  // a dummy ScrollStyles we don't care about.  It'll do a bit of extra work,
  // but saves us having to have more complicated code or more code duplication;
  // in practice we will make this call quite rarely, because we checked for all
  // the common cases above.
  ScrollStyles dummy(StyleOverflow::Auto, StyleOverflow::Auto);
  return GetPropagatedScrollStylesForViewport(this, nullptr, &dummy) ==
         &aElement;
}

nsISupports* nsPresContext::GetContainerWeak() const {
  return mDocument->GetDocShell();
}

ColorScheme nsPresContext::DefaultBackgroundColorScheme() const {
  dom::Document* doc = Document();
  // Use a dark background for top-level about:blank that is inaccessible to
  // content JS.
  if (doc->IsLikelyContentInaccessibleTopLevelAboutBlank()) {
    return doc->PreferredColorScheme(Document::IgnoreRFP::Yes);
  }
  // Prefer the root color-scheme (since generally the default canvas
  // background comes from the root element's background-color), and fall back
  // to the default color-scheme if not available.
  if (auto* frame = FrameConstructor()->GetRootElementStyleFrame()) {
    return LookAndFeel::ColorSchemeForFrame(frame);
  }
  return doc->DefaultColorScheme();
}

nscolor nsPresContext::DefaultBackgroundColor() const {
  if (!GetBackgroundColorDraw()) {
    return NS_RGB(255, 255, 255);
  }
  return PrefSheetPrefs()
      .ColorsFor(DefaultBackgroundColorScheme())
      .mDefaultBackground;
}

nsDocShell* nsPresContext::GetDocShell() const {
  return nsDocShell::Cast(mDocument->GetDocShell());
}

bool nsPresContext::BidiEnabled() const { return Document()->GetBidiEnabled(); }

void nsPresContext::SetBidiEnabled() const { Document()->SetBidiEnabled(); }

void nsPresContext::SetBidi(uint32_t aSource) {
  // Don't do all this stuff unless the options have changed.
  if (aSource == GetBidi()) {
    return;
  }

  Document()->SetBidiOptions(aSource);
  if (IBMBIDI_TEXTDIRECTION_RTL == GET_BIDI_OPTION_DIRECTION(aSource) ||
      IBMBIDI_NUMERAL_HINDI == GET_BIDI_OPTION_NUMERAL(aSource)) {
    SetBidiEnabled();
  }
  if (IBMBIDI_TEXTTYPE_VISUAL == GET_BIDI_OPTION_TEXTTYPE(aSource)) {
    SetVisualMode(true);
  } else if (IBMBIDI_TEXTTYPE_LOGICAL == GET_BIDI_OPTION_TEXTTYPE(aSource)) {
    SetVisualMode(false);
  } else {
    SetVisualMode(IsVisualCharset(Document()->GetDocumentCharacterSet()));
  }
}

uint32_t nsPresContext::GetBidi() const { return Document()->GetBidiOptions(); }

void nsPresContext::RecordInteractionTime(InteractionType aType,
                                          const TimeStamp& aTimeStamp) {
  if (!mInteractionTimeEnabled || aTimeStamp.IsNull()) {
    return;
  }

  // Array of references to the member variable of each time stamp
  // for the different interaction types, keyed by InteractionType.
  TimeStamp nsPresContext::* interactionTimes[] = {
      &nsPresContext::mFirstClickTime, &nsPresContext::mFirstKeyTime,
      &nsPresContext::mFirstMouseMoveTime, &nsPresContext::mFirstScrollTime};

  TimeStamp& interactionTime =
      this->*(interactionTimes[static_cast<uint32_t>(aType)]);
  if (!interactionTime.IsNull()) {
    // We have already recorded an interaction time.
    return;
  }

  // Record the interaction time if it occurs after the first paint
  // of the top level content document.
  nsPresContext* inProcessRootPresContext =
      GetInProcessRootContentDocumentPresContext();

  if (!inProcessRootPresContext ||
      !inProcessRootPresContext->IsRootContentDocumentCrossProcess()) {
    // There is no top content pres context, or we are in a cross process
    // document so we don't care about the interaction time. Record a value
    // anyways to avoid trying to find the top content pres context in future
    // interactions.
    interactionTime = TimeStamp::Now();
    return;
  }

  if (inProcessRootPresContext->mFirstNonBlankPaintTime.IsNull() ||
      inProcessRootPresContext->mFirstNonBlankPaintTime > aTimeStamp) {
    // Top content pres context has not had a non-blank paint yet
    // or the event timestamp is before the first non-blank paint,
    // so don't record interaction time.
    return;
  }

  // Check if we are recording the first of any of the interaction types.
  bool isFirstInteraction = true;
  for (TimeStamp nsPresContext::* memberPtr : interactionTimes) {
    TimeStamp& timeStamp = this->*(memberPtr);
    if (!timeStamp.IsNull()) {
      isFirstInteraction = false;
      break;
    }
  }

  interactionTime = TimeStamp::Now();
  // Only the top level content pres context reports first interaction
  // time to telemetry (if it hasn't already done so).
  if (this == inProcessRootPresContext) {
    if (Telemetry::CanRecordExtended() && isFirstInteraction) {
      glean::layout::time_to_first_interaction.AccumulateRawDuration(
          interactionTime - mFirstNonBlankPaintTime);
    }
  } else {
    inProcessRootPresContext->RecordInteractionTime(aType, aTimeStamp);
  }
}

nsITheme* nsPresContext::Theme() const {
  MOZ_ASSERT(mTheme);
  return mTheme;
}

void nsPresContext::EnsureTheme() {
  MOZ_ASSERT(!mTheme);
  if (Document()->ShouldAvoidNativeTheme()) {
    if (mInRDMPane) {
      mTheme = do_GetRDMThemeDoNotUseDirectly();
    } else {
      mTheme = do_GetBasicNativeThemeDoNotUseDirectly();
    }
  } else {
    mTheme = do_GetNativeThemeDoNotUseDirectly();
  }
  MOZ_RELEASE_ASSERT(mTheme);
}

void nsPresContext::RecomputeTheme() {
  if (!mTheme) {
    return;
  }
  nsCOMPtr<nsITheme> oldTheme = std::move(mTheme);
  EnsureTheme();
  if (oldTheme == mTheme) {
    return;
  }
  // Theme affects layout information (as it affects whether we create
  // scrollbar buttons for example) and also style (affects the
  // scrollbar-inline-size env var).
  RebuildAllStyleData(nsChangeHint_ReconstructFrame,
                      RestyleHint::RecascadeSubtree());
  // This is a bit of a lie, but this affects the overlay-scrollbars
  // media query and it's the code-path that gets taken for regular system
  // metrics changes via ThemeChanged().
  MediaFeatureValuesChanged({MediaFeatureChangeReason::SystemMetricsChange},
                            MediaFeatureChangePropagation::JustThisDocument);
}

bool nsPresContext::UseOverlayScrollbars() const {
  return LookAndFeel::GetInt(LookAndFeel::IntID::UseOverlayScrollbars) ||
         mInRDMPane;
}

void nsPresContext::ThemeChanged(widget::ThemeChangeKind aKind) {
  PROFILER_MARKER_UNTYPED("ThemeChanged", LAYOUT, MarkerStack::Capture());

  mPendingThemeChangeKind |= unsigned(aKind);

  if (!mPendingThemeChanged) {
    nsCOMPtr<nsIRunnable> ev =
        new WeakRunnableMethod("nsPresContext::ThemeChangedInternal", this,
                               &nsPresContext::ThemeChangedInternal);
    RefreshDriver()->AddEarlyRunner(ev);
    mPendingThemeChanged = true;
  }
  MOZ_ASSERT(LookAndFeel::HasPendingGlobalThemeChange());
}

void nsPresContext::ThemeChangedInternal() {
  MOZ_ASSERT(mPendingThemeChanged);

  mPendingThemeChanged = false;

  const auto kind = widget::ThemeChangeKind(mPendingThemeChangeKind);
  mPendingThemeChangeKind = 0;

  LookAndFeel::HandleGlobalThemeChange();

  // Full zoom might have changed as a result of the text scale factor.
  // Forced colors might also have changed.
  RecomputeBrowsingContextDependentData();

  // Changes to system metrics and other look and feel values can change media
  // queries on them.
  //
  // Changes in theme can change system colors (whose changes are properly
  // reflected in computed style data), system fonts (whose changes are
  // some reflected (like sizes and such) and some not), and -moz-appearance
  // (whose changes are not), so we need to recascade for the first, and reflow
  // for the rest.
  auto restyleHint = (kind & widget::ThemeChangeKind::Style)
                         ? RestyleHint::RecascadeSubtree()
                         : RestyleHint{0};
  auto changeHint = (kind & widget::ThemeChangeKind::Layout)
                        ? NS_STYLE_HINT_REFLOW
                        : nsChangeHint(0);
  MediaFeatureValuesChanged(
      {restyleHint, changeHint, MediaFeatureChangeReason::SystemMetricsChange},
      MediaFeatureChangePropagation::All);

  if (Document()->IsInChromeDocShell()) {
    if (RefPtr<nsPIDOMWindowInner> win = Document()->GetInnerWindow()) {
      nsContentUtils::DispatchEventOnlyToChrome(
          Document(), nsGlobalWindowInner::Cast(win), u"nativethemechange"_ns,
          CanBubble::eYes, Cancelable::eYes, nullptr);
    }
  }
}

void nsPresContext::UIResolutionChanged() {
  if (!mPendingUIResolutionChanged) {
    nsCOMPtr<nsIRunnable> ev =
        NewRunnableMethod("nsPresContext::UIResolutionChangedInternal", this,
                          &nsPresContext::UIResolutionChangedInternal);
    nsresult rv = Document()->Dispatch(ev.forget());
    if (NS_SUCCEEDED(rv)) {
      mPendingUIResolutionChanged = true;
    }
  }
}

void nsPresContext::UIResolutionChangedSync() {
  if (!mPendingUIResolutionChanged) {
    mPendingUIResolutionChanged = true;
    UIResolutionChangedInternal();
  }
}

static void NotifyTabUIResolutionChanged(nsIRemoteTab* aTab, void* aArg) {
  aTab->NotifyResolutionChanged();
}

static void NotifyChildrenUIResolutionChanged(nsPIDOMWindowOuter* aWindow) {
  nsCOMPtr<Document> doc = aWindow->GetExtantDoc();
  RefPtr<nsPIWindowRoot> topLevelWin = nsContentUtils::GetWindowRoot(doc);
  if (!topLevelWin) {
    return;
  }
  topLevelWin->EnumerateBrowsers(NotifyTabUIResolutionChanged, nullptr);
}

void nsPresContext::UIResolutionChangedInternal() {
  mPendingUIResolutionChanged = false;

  mDeviceContext->CheckDPIChange();
  if (mCurAppUnitsPerDevPixel != mDeviceContext->AppUnitsPerDevPixel()) {
    AppUnitsPerDevPixelChanged();
  }

  if (mPresShell) {
    mPresShell->RefreshZoomConstraintsForScreenSizeChange();
    if (RefPtr<MobileViewportManager> mvm =
            mPresShell->GetMobileViewportManager()) {
      mvm->UpdateSizesBeforeReflow();
    }
  }

  // Recursively notify all remote leaf descendants of the change.
  if (nsPIDOMWindowOuter* window = mDocument->GetWindow()) {
    NotifyChildrenUIResolutionChanged(window);
  }

  mDocument->EnumerateSubDocuments([](dom::Document& aSubDoc) {
    if (nsPresContext* pc = aSubDoc.GetPresContext()) {
      pc->UIResolutionChangedInternal();
    }
    return CallState::Continue;
  });
}

void nsPresContext::EmulateMedium(nsAtom* aMediaType) {
  MOZ_ASSERT(!aMediaType || aMediaType->IsAsciiLowercase());

  RefPtr<const nsAtom> oldMedium = Medium();
  auto oldScheme = mDocument->PreferredColorScheme();

  mMediaEmulationData.mMedium = aMediaType;

  if (Medium() == oldMedium) {
    return;
  }

  MediaFeatureChange change(MediaFeatureChangeReason::MediumChange);
  if (oldScheme != mDocument->PreferredColorScheme()) {
    change |= MediaFeatureChange::ForPreferredColorSchemeOrForcedColorsChange();
  }
  MediaFeatureValuesChanged(change,
                            MediaFeatureChangePropagation::JustThisDocument);
}

void nsPresContext::ContentLanguageChanged() {
  PostRebuildAllStyleDataEvent(nsChangeHint(0),
                               RestyleHint::RecascadeSubtree());
}

void nsPresContext::RegisterManagedPostRefreshObserver(
    ManagedPostRefreshObserver* aObserver) {
  if (MOZ_UNLIKELY(!mPresShell)) {
    // If we're detached from our pres shell already, refuse to keep observer
    // around, as that'd create a cycle.
    RefPtr<ManagedPostRefreshObserver> obs = aObserver;
    obs->Cancel();
    return;
  }

  RefreshDriver()->AddPostRefreshObserver(
      static_cast<nsAPostRefreshObserver*>(aObserver));
  mManagedPostRefreshObservers.AppendElement(aObserver);
}

void nsPresContext::UnregisterManagedPostRefreshObserver(
    ManagedPostRefreshObserver* aObserver) {
  RefreshDriver()->RemovePostRefreshObserver(
      static_cast<nsAPostRefreshObserver*>(aObserver));
  DebugOnly<bool> removed =
      mManagedPostRefreshObservers.RemoveElement(aObserver);
  MOZ_ASSERT(removed,
             "ManagedPostRefreshObserver should be owned by PresContext");
}

void nsPresContext::CancelManagedPostRefreshObservers() {
  auto observers = std::move(mManagedPostRefreshObservers);
  nsRefreshDriver* driver = RefreshDriver();
  for (const auto& observer : observers) {
    observer->Cancel();
    driver->RemovePostRefreshObserver(
        static_cast<nsAPostRefreshObserver*>(observer));
  }
}

void nsPresContext::RebuildAllStyleData(nsChangeHint aExtraHint,
                                        const RestyleHint& aRestyleHint) {
  if (!mPresShell) {
    // We must have been torn down. Nothing to do here.
    return;
  }

  // TODO(emilio): It's unclear to me why would these three calls below be
  // needed. In particular, RebuildAllStyleData doesn't rebuild rules or
  // specified style information and such (note the comment in
  // RestyleManager::RebuildAllStyleData re. the funny semantics), so I
  // don't know why should we rebuild the user font set / counter styles /
  // etc...
  mDocument->MarkUserFontSetDirty();
  MarkCounterStylesDirty();
  MarkFontFeatureValuesDirty();
  MarkFontPaletteValuesDirty();
  PostRebuildAllStyleDataEvent(aExtraHint, aRestyleHint);
}

void nsPresContext::PostRebuildAllStyleDataEvent(
    nsChangeHint aExtraHint, const RestyleHint& aRestyleHint) {
  if (!mPresShell) {
    // We must have been torn down. Nothing to do here.
    return;
  }
  RestyleManager()->RebuildAllStyleData(aExtraHint, aRestyleHint);
}

void nsPresContext::MediaFeatureValuesChanged(
    const MediaFeatureChange& aChange,
    MediaFeatureChangePropagation aPropagation) {
  if (mPresShell) {
    mPresShell->EnsureStyleFlush();
  }

  if (!mDocument->MediaQueryLists().isEmpty()) {
    RefreshDriver()->ScheduleMediaQueryListenerUpdate();
  }

  if (!mPendingMediaFeatureValuesChange) {
    mPendingMediaFeatureValuesChange = MakeUnique<MediaFeatureChange>(aChange);
  } else {
    *mPendingMediaFeatureValuesChange |= aChange;
  }

  if (aPropagation & MediaFeatureChangePropagation::Images) {
    // Propagate the media feature value change down to any SVG images the
    // document is using.
    mDocument->ImageTracker()->MediaFeatureValuesChangedAllDocuments(aChange);
  }

  if (aPropagation & MediaFeatureChangePropagation::SubDocuments) {
    // And then into any subdocuments.
    mDocument->EnumerateSubDocuments(
        [&aChange, aPropagation](dom::Document& aSubDoc) {
          if (nsPresContext* pc = aSubDoc.GetPresContext()) {
            pc->MediaFeatureValuesChanged(aChange, aPropagation);
          }
          return CallState::Continue;
        });
  }

  // We notify the media feature values changed for the responsive content of
  // HTMLImageElements synchronously, so their image sources are always
  // up-to-date when running the image load tasks in the microtasks.
  mDocument->NotifyMediaFeatureValuesChanged();
}

bool nsPresContext::FlushPendingMediaFeatureValuesChanged() {
  if (!mPendingMediaFeatureValuesChange) {
    return false;
  }

  MediaFeatureChange change = *mPendingMediaFeatureValuesChange;
  mPendingMediaFeatureValuesChange.reset();

  // MediumFeaturesChanged updates the applied rules, so it always gets called.
  if (mPresShell) {
    change.mRestyleHint |=
        mPresShell->StyleSet()->MediumFeaturesChanged(change.mReason);
  }

  const bool changedStyle = change.mRestyleHint || change.mChangeHint;
  if (changedStyle) {
    RebuildAllStyleData(change.mChangeHint, change.mRestyleHint);
  }

  for (MediaQueryList* mql : mDocument->MediaQueryLists()) {
    mql->MediaFeatureValuesChanged();
  }

  return changedStyle;
}

void nsPresContext::SizeModeChanged(nsSizeMode aSizeMode) {
  if (nsPIDOMWindowOuter* window = mDocument->GetWindow()) {
    nsContentUtils::CallOnAllRemoteChildren(
        window, [&aSizeMode](BrowserParent* aBrowserParent) -> CallState {
          aBrowserParent->SizeModeChanged(aSizeMode);
          return CallState::Continue;
        });
  }
  MediaFeatureValuesChanged({MediaFeatureChangeReason::SizeModeChange},
                            MediaFeatureChangePropagation::SubDocuments);
}

nsCompatibility nsPresContext::CompatibilityMode() const {
  return Document()->GetCompatibilityMode();
}

void nsPresContext::SetPaginatedScrolling(bool aPaginated) {
  if (mType == eContext_PrintPreview || mType == eContext_PageLayout) {
    mCanPaginatedScroll = aPaginated;
  }
}

void nsPresContext::SetPrintSettings(nsIPrintSettings* aPrintSettings) {
  if (mMedium != nsGkAtoms::print) {
    return;
  }

  mPrintSettings = aPrintSettings;
  mDefaultPageMargin = nsMargin();
  if (!mPrintSettings) {
    return;
  }

  // Set the presentation context to the value in the print settings.
  mDrawColorBackground = mPrintSettings->GetPrintBGColors();
  mDrawImageBackground = mPrintSettings->GetPrintBGImages();

  nsIntMargin marginTwips = mPrintSettings->GetMarginInTwips();
  if (!mPrintSettings->GetIgnoreUnwriteableMargins()) {
    nsIntMargin unwriteableTwips =
        mPrintSettings->GetUnwriteableMarginInTwips();
    NS_ASSERTION(unwriteableTwips.top >= 0 && unwriteableTwips.right >= 0 &&
                     unwriteableTwips.bottom >= 0 && unwriteableTwips.left >= 0,
                 "Unwriteable twips should be non-negative");
    marginTwips.EnsureAtLeast(unwriteableTwips);
  }
  mDefaultPageMargin = nsPresContext::CSSTwipsToAppUnits(marginTwips);
}

bool nsPresContext::EnsureVisible() {
  BrowsingContext* browsingContext =
      mDocument ? mDocument->GetBrowsingContext() : nullptr;
  if (!browsingContext || browsingContext->IsInBFCache()) {
    return false;
  }

  nsCOMPtr<nsIDocShell> docShell(GetDocShell());
  if (!docShell) {
    return false;
  }
  nsCOMPtr<nsIDocumentViewer> viewer;
  docShell->GetDocViewer(getter_AddRefs(viewer));
  // Make sure this is the content viewer we belong with
  if (!viewer || viewer->GetPresContext() != this) {
    return false;
  }
  // OK, this is us.  We want to call Show() on the content viewer.
  nsresult result = viewer->Show();
  return NS_SUCCEEDED(result);
}

#ifdef MOZ_REFLOW_PERF
void nsPresContext::CountReflows(const char* aName, nsIFrame* aFrame) {
  if (mPresShell) {
    mPresShell->CountReflows(aName, aFrame);
  }
}
#endif

gfxUserFontSet* nsPresContext::GetUserFontSet() {
  return mDocument->GetUserFontSet();
}

void nsPresContext::UserFontSetUpdated(gfxUserFontEntry* aUpdatedFont) {
  if (!mPresShell) {
    return;
  }

  // Note: this method is called without a font when rules in the userfont set
  // are updated.
  //
  // We can avoid a full restyle if font-metric-dependent units are not in use,
  // since we know there's no style resolution that would depend on this font
  // and trigger its load.
  //
  // TODO(emilio): We could be more granular if we knew which families have
  // potentially changed.
  if (!aUpdatedFont) {
    auto hint = StyleSet()->UsesFontMetrics() ? RestyleHint::RecascadeSubtree()
                                              : RestyleHint{0};
    PostRebuildAllStyleDataEvent(NS_STYLE_HINT_REFLOW, hint);
    return;
  }

  // Iterate over the frame tree looking for frames associated with the
  // downloadable font family in question. If a frame's nsStyleFont has
  // the name, check the font group associated with the metrics to see if
  // it contains that specific font (i.e. the one chosen within the family
  // given the weight, width, and slant from the nsStyleFont). If it does,
  // mark that frame dirty and skip inspecting its descendants.
  if (nsIFrame* root = mPresShell->GetRootFrame()) {
    nsFontFaceUtils::MarkDirtyForFontChange(root, aUpdatedFont);
  }
}

class CounterStyleCleaner final : public nsAPostRefreshObserver {
 public:
  CounterStyleCleaner(nsRefreshDriver* aRefreshDriver,
                      CounterStyleManager* aCounterStyleManager)
      : mRefreshDriver(aRefreshDriver),
        mCounterStyleManager(aCounterStyleManager) {}
  virtual ~CounterStyleCleaner() = default;

  void DidRefresh() final {
    mRefreshDriver->RemovePostRefreshObserver(this);
    mCounterStyleManager->CleanRetiredStyles();
    delete this;
  }

 private:
  RefPtr<nsRefreshDriver> mRefreshDriver;
  RefPtr<CounterStyleManager> mCounterStyleManager;
};

void nsPresContext::FlushCounterStyles() {
  if (!mPresShell) {
    return;  // we've been torn down
  }
  if (mCounterStyleManager->IsInitial()) {
    // Still in its initial state, no need to clean.
    return;
  }

  if (mCounterStylesDirty) {
    bool changed = mCounterStyleManager->NotifyRuleChanged();
    if (changed) {
      PresShell()->NotifyCounterStylesAreDirty();
      PostRebuildAllStyleDataEvent(NS_STYLE_HINT_REFLOW, RestyleHint{0});
      RefreshDriver()->AddPostRefreshObserver(
          new CounterStyleCleaner(RefreshDriver(), mCounterStyleManager));
    }
    mCounterStylesDirty = false;
  }
}

void nsPresContext::MarkCounterStylesDirty() {
  if (mCounterStyleManager->IsInitial()) {
    // Still in its initial state, no need to touch anything.
    return;
  }

  mCounterStylesDirty = true;
}

void nsPresContext::NotifyMissingFonts() {
  if (mMissingFonts) {
    mMissingFonts->Flush();
  }
}

void nsPresContext::EnsureSafeToHandOutCSSRules() {
  if (!mPresShell->StyleSet()->EnsureUniqueInnerOnCSSSheets()) {
    // Nothing to do.
    return;
  }

  RebuildAllStyleData(nsChangeHint(0), RestyleHint::RestyleSubtree());
}

void nsPresContext::FireDOMPaintEvent(
    nsTArray<nsRect>* aList, TransactionId aTransactionId,
    mozilla::TimeStamp aTimeStamp /* = mozilla::TimeStamp() */) {
  nsPIDOMWindowInner* ourWindow = mDocument->GetInnerWindow();
  if (!ourWindow) {
    return;
  }

  nsCOMPtr<EventTarget> dispatchTarget = do_QueryInterface(ourWindow);
  nsCOMPtr<EventTarget> eventTarget = dispatchTarget;
  if (!IsChrome() && !StaticPrefs::dom_send_after_paint_to_content()) {
    // Don't tell the window about this event, it should not know that
    // something happened in a subdocument. Tell only the chrome event handler.
    // (Events sent to the window get propagated to the chrome event handler
    // automatically.)
    dispatchTarget = ourWindow->GetParentTarget();
    if (!dispatchTarget) {
      return;
    }
  }

  if (aTimeStamp.IsNull()) {
    aTimeStamp = mozilla::TimeStamp::Now();
  }
  DOMHighResTimeStamp timeStamp = 0;
  if (ourWindow) {
    mozilla::dom::Performance* perf = ourWindow->GetPerformance();
    if (perf) {
      timeStamp = perf->GetDOMTiming()->TimeStampToDOMHighRes(aTimeStamp);
    }
  }

  // Events sent to the window get propagated to the chrome event handler
  // automatically.
  //
  // This will empty our list in case dispatching the event causes more damage
  // (hopefully it won't, or we're likely to get an infinite loop! At least
  // it won't be blocking app execution though).
  RefPtr<NotifyPaintEvent> event =
      NS_NewDOMNotifyPaintEvent(eventTarget, this, nullptr, eAfterPaint, aList,
                                uint64_t(aTransactionId), timeStamp);

  // Even if we're not telling the window about the event (so eventTarget is
  // the chrome event handler, not the window), the window is still
  // logically the event target.
  event->SetTarget(eventTarget);
  event->SetTrusted(true);
  EventDispatcher::DispatchDOMEvent(dispatchTarget, nullptr,
                                    static_cast<Event*>(event), this, nullptr);
}

nsPresContext::TransactionInvalidations* nsPresContext::GetInvalidations(
    TransactionId aTransactionId) {
  for (TransactionInvalidations& t : mTransactions) {
    if (t.mTransactionId == aTransactionId) {
      return &t;
    }
  }
  return nullptr;
}

void nsPresContext::NotifyInvalidation(TransactionId aTransactionId,
                                       const nsRect& aRect) {
  MOZ_ASSERT(GetContainerWeak(), "Invalidation in detached pres context");

  for (nsPresContext* pc = this; pc; pc = pc->GetParentPresContext()) {
    TransactionInvalidations* transaction =
        pc->GetInvalidations(aTransactionId);
    if (transaction) {
      break;
    }
    transaction = pc->mTransactions.AppendElement();
    transaction->mTransactionId = aTransactionId;
  }

  TransactionInvalidations* transaction = GetInvalidations(aTransactionId);
  MOZ_ASSERT(transaction);
  transaction->mInvalidations.AppendElement(aRect);
}

class DelayedFireDOMPaintEvent : public Runnable {
 public:
  DelayedFireDOMPaintEvent(
      nsPresContext* aPresContext, nsTArray<nsRect>&& aList,
      TransactionId aTransactionId,
      const mozilla::TimeStamp& aTimeStamp = mozilla::TimeStamp())
      : mozilla::Runnable("DelayedFireDOMPaintEvent"),
        mPresContext(aPresContext),
        mTransactionId(aTransactionId),
        mTimeStamp(aTimeStamp),
        mList(std::move(aList)) {
    MOZ_ASSERT(mPresContext->GetContainerWeak(),
               "DOMPaintEvent requested for a detached pres context");
  }
  NS_IMETHOD Run() override {
    // The pres context might have been detached during the delay -
    // that's fine, just don't fire the event.
    if (mPresContext->GetContainerWeak()) {
      mPresContext->FireDOMPaintEvent(&mList, mTransactionId, mTimeStamp);
    }
    return NS_OK;
  }

  RefPtr<nsPresContext> mPresContext;
  TransactionId mTransactionId;
  const mozilla::TimeStamp mTimeStamp;
  nsTArray<nsRect> mList;
};

void nsPresContext::NotifyRevokingDidPaint(TransactionId aTransactionId) {
  if ((IsRoot() || !PresShell()->IsVisible()) && mTransactions.IsEmpty()) {
    return;
  }

  TransactionInvalidations* transaction = nullptr;
  for (auto& t : mTransactions) {
    if (t.mTransactionId == aTransactionId) {
      transaction = &t;
      break;
    }
  }
  // If there are no transaction invalidations (which imply callers waiting
  // on the event) for this revoked id, then we don't need to fire a
  // MozAfterPaint.
  if (!transaction) {
    return;
  }

  // If there are queued transactions with an earlier id, we can't send
  // our event now since it will arrive out of order. Set the waiting for
  // previous transaction flag to true, and we'll send the event when
  // the others are completed.
  // If this is the only transaction, then we can send it immediately.
  if (mTransactions.Length() == 1) {
    nsCOMPtr<nsIRunnable> ev = new DelayedFireDOMPaintEvent(
        this, std::move(transaction->mInvalidations),
        transaction->mTransactionId, mozilla::TimeStamp());
    nsContentUtils::AddScriptRunner(ev);
    mTransactions.RemoveElementAt(0);
  } else {
    transaction->mIsWaitingForPreviousTransaction = true;
  }

  mDocument->EnumerateSubDocuments([&aTransactionId](dom::Document& aSubDoc) {
    if (nsPresContext* pc = aSubDoc.GetPresContext()) {
      pc->NotifyRevokingDidPaint(aTransactionId);
    }
    return CallState::Continue;
  });
}

void nsPresContext::NotifyDidPaintForSubtree(
    TransactionId aTransactionId, const mozilla::TimeStamp& aTimeStamp) {
  if (mFirstContentfulPaintTransactionId && !mHadContentfulPaintComposite) {
    if (aTransactionId >= *mFirstContentfulPaintTransactionId) {
      mHadContentfulPaintComposite = true;
      RefPtr<nsDOMNavigationTiming> timing = mDocument->GetNavigationTiming();
      if (timing && !IsPrintingOrPrintPreview()) {
        timing->NotifyContentfulCompositeForRootContentDocument(aTimeStamp);
      }
    }
  }

  if (IsRoot() && mTransactions.IsEmpty()) {
    return;
  }

  if (!PresShell()->IsVisible() && mTransactions.IsEmpty()) {
    return;
  }

  // Non-root prescontexts fire MozAfterPaint to all their descendants
  // unconditionally, even if no invalidations have been collected. This is
  // because we don't want to eat the cost of collecting invalidations for
  // every subdocument (which would require putting every subdocument in its
  // own layer).

  bool sent = false;
  uint32_t i = 0;
  while (i < mTransactions.Length()) {
    if (mTransactions[i].mTransactionId <= aTransactionId) {
      if (!mTransactions[i].mInvalidations.IsEmpty()) {
        nsCOMPtr<nsIRunnable> ev = new DelayedFireDOMPaintEvent(
            this, std::move(mTransactions[i].mInvalidations),
            mTransactions[i].mTransactionId, aTimeStamp);
        NS_DispatchToCurrentThreadQueue(ev.forget(),
                                        EventQueuePriority::MediumHigh);
        sent = true;
      }
      mTransactions.RemoveElementAt(i);
    } else {
      // If there are transaction which is waiting for this transaction,
      // we should fire a MozAfterPaint immediately.
      if (sent && mTransactions[i].mIsWaitingForPreviousTransaction) {
        nsCOMPtr<nsIRunnable> ev = new DelayedFireDOMPaintEvent(
            this, std::move(mTransactions[i].mInvalidations),
            mTransactions[i].mTransactionId, aTimeStamp);
        NS_DispatchToCurrentThreadQueue(ev.forget(),
                                        EventQueuePriority::MediumHigh);
        sent = true;
        mTransactions.RemoveElementAt(i);
        continue;
      }
      i++;
    }
  }

  if (!sent) {
    nsTArray<nsRect> dummy;
    nsCOMPtr<nsIRunnable> ev = new DelayedFireDOMPaintEvent(
        this, std::move(dummy), aTransactionId, aTimeStamp);
    NS_DispatchToCurrentThreadQueue(ev.forget(),
                                    EventQueuePriority::MediumHigh);
  }

  mDocument->EnumerateSubDocuments(
      [&aTransactionId, &aTimeStamp](dom::Document& aSubDoc) {
        if (nsPresContext* pc = aSubDoc.GetPresContext()) {
          pc->NotifyDidPaintForSubtree(aTransactionId, aTimeStamp);
        }
        return CallState::Continue;
      });
}

already_AddRefed<nsITimer> nsPresContext::CreateTimer(
    nsTimerCallbackFunc aCallback, const char* aName, uint32_t aDelay) {
  nsCOMPtr<nsITimer> timer;
  NS_NewTimerWithFuncCallback(getter_AddRefs(timer), aCallback, this, aDelay,
                              nsITimer::TYPE_ONE_SHOT, aName,
                              GetMainThreadSerialEventTarget());
  return timer.forget();
}

static bool sGotInterruptEnv = false;
enum InterruptMode { ModeRandom, ModeCounter, ModeEvent };
// Controlled by the GECKO_REFLOW_INTERRUPT_MODE env var; allowed values are
// "random" (except on Windows) or "counter".  If neither is used, the mode is
// ModeEvent.
static InterruptMode sInterruptMode = ModeEvent;
#ifndef XP_WIN
// Used for the "random" mode.  Controlled by the GECKO_REFLOW_INTERRUPT_SEED
// env var.
static uint32_t sInterruptSeed = 1;
#endif
// Used for the "counter" mode.  This is the number of unskipped interrupt
// checks that have to happen before we interrupt.  Controlled by the
// GECKO_REFLOW_INTERRUPT_FREQUENCY env var.
static uint32_t sInterruptMaxCounter = 10;
// Used for the "counter" mode.  This counts up to sInterruptMaxCounter and is
// then reset to 0.
static uint32_t sInterruptCounter;
// Number of interrupt checks to skip before really trying to interrupt.
// Controlled by the GECKO_REFLOW_INTERRUPT_CHECKS_TO_SKIP env var.
static uint32_t sInterruptChecksToSkip = 200;
// Number of milliseconds that a reflow should be allowed to run for before we
// actually allow interruption.  Controlled by the
// GECKO_REFLOW_MIN_NOINTERRUPT_DURATION env var.  Can't be initialized here,
// because TimeDuration/TimeStamp is not safe to use in static constructors..
static TimeDuration sInterruptTimeout;

static void GetInterruptEnv() {
  char* ev = PR_GetEnv("GECKO_REFLOW_INTERRUPT_MODE");
  if (ev) {
#ifndef XP_WIN
    if (nsCRT::strcasecmp(ev, "random") == 0) {
      ev = PR_GetEnv("GECKO_REFLOW_INTERRUPT_SEED");
      if (ev) {
        sInterruptSeed = atoi(ev);
      }
      srandom(sInterruptSeed);
      sInterruptMode = ModeRandom;
    } else
#endif
        if (PL_strcasecmp(ev, "counter") == 0) {
      ev = PR_GetEnv("GECKO_REFLOW_INTERRUPT_FREQUENCY");
      if (ev) {
        sInterruptMaxCounter = atoi(ev);
      }
      sInterruptCounter = 0;
      sInterruptMode = ModeCounter;
    }
  }
  ev = PR_GetEnv("GECKO_REFLOW_INTERRUPT_CHECKS_TO_SKIP");
  if (ev) {
    sInterruptChecksToSkip = atoi(ev);
  }

  ev = PR_GetEnv("GECKO_REFLOW_MIN_NOINTERRUPT_DURATION");
  int duration_ms = ev ? atoi(ev) : 100;
  sInterruptTimeout = TimeDuration::FromMilliseconds(duration_ms);
}

bool nsPresContext::HavePendingInputEvent() {
  switch (sInterruptMode) {
#ifndef XP_WIN
    case ModeRandom:
      return (random() & 1);
#endif
    case ModeCounter:
      if (sInterruptCounter < sInterruptMaxCounter) {
        ++sInterruptCounter;
        return false;
      }
      sInterruptCounter = 0;
      return true;
    default:
    case ModeEvent: {
      nsIFrame* f = PresShell()->GetRootFrame();
      if (f) {
        nsIWidget* w = f->GetNearestWidget();
        if (w) {
          return w->HasPendingInputEvent();
        }
      }
      return false;
    }
  }
}

void nsPresContext::ReflowStarted(bool aInterruptible) {
#ifdef NOISY_INTERRUPTIBLE_REFLOW
  if (!aInterruptible) {
    printf("STARTING NONINTERRUPTIBLE REFLOW\n");
  }
#endif
  // We don't support interrupting in paginated contexts, since page
  // sequences only handle initial reflow
  mInterruptsEnabled = aInterruptible && !IsPaginated() &&
                       StaticPrefs::layout_interruptible_reflow_enabled();

  // Don't set mHasPendingInterrupt based on HavePendingInputEvent() here.  If
  // we ever change that, then we need to update the code in
  // PresShell::DoReflow to only add the just-reflown root to dirty roots if
  // it's actually dirty.  Otherwise we can end up adding a root that has no
  // interruptible descendants, just because we detected an interrupt at reflow
  // start.
  mHasPendingInterrupt = false;

  mInterruptChecksToSkip = sInterruptChecksToSkip;

  if (mInterruptsEnabled) {
    mReflowStartTime = TimeStamp::Now();
  }
}

bool nsPresContext::CheckForInterrupt(nsIFrame* aFrame) {
  if (mHasPendingInterrupt) {
    mPresShell->FrameNeedsToContinueReflow(aFrame);
    return true;
  }

  if (!sGotInterruptEnv) {
    sGotInterruptEnv = true;
    GetInterruptEnv();
  }

  if (!mInterruptsEnabled) {
    return false;
  }

  if (mInterruptChecksToSkip > 0) {
    --mInterruptChecksToSkip;
    return false;
  }
  mInterruptChecksToSkip = sInterruptChecksToSkip;

  // Don't interrupt if it's been less than sInterruptTimeout since we started
  // the reflow.
  mHasPendingInterrupt =
      TimeStamp::Now() - mReflowStartTime > sInterruptTimeout &&
      HavePendingInputEvent() && !IsChrome();

  if (mPendingInterruptFromTest) {
    mPendingInterruptFromTest = false;
    mHasPendingInterrupt = true;
  }

  if (mHasPendingInterrupt) {
#ifdef NOISY_INTERRUPTIBLE_REFLOW
    printf("*** DETECTED pending interrupt (time=%lld)\n", PR_Now());
#endif /* NOISY_INTERRUPTIBLE_REFLOW */
    mPresShell->FrameNeedsToContinueReflow(aFrame);
  }
  return mHasPendingInterrupt;
}

nsIFrame* nsPresContext::GetPrimaryFrameFor(nsIContent* aContent) {
  MOZ_ASSERT(aContent, "Don't do that");
  if (GetPresShell() &&
      GetPresShell()->GetDocument() == aContent->GetComposedDoc()) {
    return aContent->GetPrimaryFrame();
  }
  return nullptr;
}

size_t nsPresContext::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  // Measurement may be added later if DMD finds it is worthwhile.
  return 0;
}

bool nsPresContext::IsRootContentDocumentInProcess() const {
  if (mDocument->IsResourceDoc()) {
    return false;
  }
  if (IsChrome()) {
    return false;
  }
  // We may not have a root frame, so use views.
  nsView* view = PresShell()->GetViewManager()->GetRootView();
  if (!view) {
    return false;
  }
  view = view->GetParent();  // anonymous inner view
  if (!view) {
    return true;
  }
  view = view->GetParent();  // subdocumentframe's view
  if (!view) {
    return true;
  }

  nsIFrame* f = view->GetFrame();
  return (f && f->PresContext()->IsChrome());
}

bool nsPresContext::IsRootContentDocumentCrossProcess() const {
  if (mDocument->IsResourceDoc()) {
    return false;
  }

  if (BrowsingContext* browsingContext = mDocument->GetBrowsingContext()) {
    if (browsingContext->GetEmbeddedInContentDocument()) {
      return false;
    }
  }
  return mDocument->IsTopLevelContentDocument();
}

void nsPresContext::NotifyNonBlankPaint() {
  MOZ_ASSERT(!mHadNonBlankPaint);
  mHadNonBlankPaint = true;
  if (IsRootContentDocumentCrossProcess()) {
    RefPtr<nsDOMNavigationTiming> timing = mDocument->GetNavigationTiming();
    if (timing && !IsPrintingOrPrintPreview()) {
      timing->NotifyNonBlankPaintForRootContentDocument();
    }

    mFirstNonBlankPaintTime = TimeStamp::Now();
  }
  if (IsChrome() && IsRoot()) {
    if (nsCOMPtr<nsIWidget> rootWidget = GetRootWidget()) {
      rootWidget->DidGetNonBlankPaint();
    }
  }
}

bool nsPresContext::HasStoppedGeneratingLCP() const {
  if (auto* perf = GetPerformanceMainThread()) {
    return perf->HasDispatchedInputEvent() || perf->HasDispatchedScrollEvent();
  }

  return true;
}

void nsPresContext::NotifyContentfulPaint() {
  if (mHadFirstContentfulPaint && HasStoppedGeneratingLCP()) {
    return;
  }
  nsRootPresContext* rootPresContext = GetRootPresContext();
  if (!rootPresContext) {
    return;
  }
  if (!mHadNonTickContentfulPaint) {
#ifdef MOZ_WIDGET_ANDROID
    (new AsyncEventDispatcher(mDocument, u"MozFirstContentfulPaint"_ns,
                              CanBubble::eYes, ChromeOnlyDispatch::eYes))
        ->PostDOMEvent();
#endif
  }
  if (!rootPresContext->RefreshDriver()->IsInRefresh()) {
    if (!mHadNonTickContentfulPaint) {
      rootPresContext->RefreshDriver()
          ->AddForceNotifyContentfulPaintPresContext(this);
      mHadNonTickContentfulPaint = true;
    }
    return;
  }

  if (!mHadFirstContentfulPaint) {
    mHadFirstContentfulPaint = true;
    mFirstContentfulPaintTransactionId =
        Some(rootPresContext->mRefreshDriver->LastTransactionId().Next());
  }

  if (auto* perf = GetPerformanceMainThread()) {
    mMarkPaintTimingStart = TimeStamp::Now();
    MOZ_ASSERT(rootPresContext->RefreshDriver()->IsInRefresh(),
               "We should only notify contentful paint during refresh "
               "driver ticks");
    if (!perf->HadFCPTimingEntry()) {
      TimeStamp nowTime = rootPresContext->RefreshDriver()->MostRecentRefresh();
      MOZ_ASSERT(!nowTime.IsNull(),
                 "Most recent refresh timestamp should exist since we are in "
                 "a refresh driver tick");
      auto paintTiming = MakeRefPtr<PerformancePaintTiming>(
          perf, u"first-contentful-paint"_ns, nowTime);
      perf->SetFCPTimingEntry(paintTiming);

      if (profiler_thread_is_being_profiled_for_markers()) {
        RefPtr<nsDOMNavigationTiming> timing = mDocument->GetNavigationTiming();
        if (timing) {
          TimeStamp navigationStart = timing->GetNavigationStartTimeStamp();
          TimeDuration elapsed = nowTime - navigationStart;
          nsIURI* docURI = Document()->GetDocumentURI();
          nsPrintfCString marker(
              "Contentful paint after %dms for URL %s",
              int(elapsed.ToMilliseconds()),
              nsContentUtils::TruncatedURLForDisplay(docURI).get());
          PROFILER_MARKER_TEXT(
              "FirstContentfulPaint", DOM,
              MarkerOptions(
                  MarkerTiming::Interval(navigationStart, nowTime),
                  MarkerInnerWindowId(mDocument->GetInnerWindow()->WindowID())),
              marker);
        }
      }
    }

    perf->ProcessElementTiming();
  }
}

void nsPresContext::NotifyPaintStatusReset() {
  mHadNonBlankPaint = false;
  mHadFirstContentfulPaint = false;
#if defined(MOZ_WIDGET_ANDROID)
  (new AsyncEventDispatcher(mDocument, u"MozPaintStatusReset"_ns,
                            CanBubble::eYes, ChromeOnlyDispatch::eYes))
      ->PostDOMEvent();
#endif
  mHadNonTickContentfulPaint = false;
}

nscoord nsPresContext::GfxUnitsToAppUnits(gfxFloat aGfxUnits) const {
  return mDeviceContext->GfxUnitsToAppUnits(aGfxUnits);
}

gfxFloat nsPresContext::AppUnitsToGfxUnits(nscoord aAppUnits) const {
  return mDeviceContext->AppUnitsToGfxUnits(aAppUnits);
}

nscoord nsPresContext::PhysicalMillimetersToAppUnits(float aMM) const {
  float inches = aMM / MM_PER_INCH_FLOAT;
  return NSToCoordFloorClamped(
      inches * float(DeviceContext()->AppUnitsPerPhysicalInch()));
}

uint64_t nsPresContext::GetRestyleGeneration() const {
  if (!mRestyleManager) {
    return 0;
  }
  return mRestyleManager->GetRestyleGeneration();
}

uint64_t nsPresContext::GetUndisplayedRestyleGeneration() const {
  if (!mRestyleManager) {
    return 0;
  }
  return mRestyleManager->GetUndisplayedRestyleGeneration();
}

mozilla::intl::Bidi& nsPresContext::BidiEngine() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mBidiEngine) {
    mBidiEngine = MakeUnique<mozilla::intl::Bidi>();
  }
  return *mBidiEngine;
}

void nsPresContext::FlushFontFeatureValues() {
  if (!mPresShell) {
    return;  // we've been torn down
  }

  if (!mFontFeatureValuesDirty) {
    return;
  }

  ServoStyleSet* styleSet = mPresShell->StyleSet();
  mFontFeatureValuesLookup = styleSet->BuildFontFeatureValueSet();
  mFontFeatureValuesDirty = false;
}

void nsPresContext::FlushFontPaletteValues() {
  if (!mPresShell) {
    return;  // we've been torn down
  }

  if (!mFontPaletteValuesDirty) {
    return;
  }

  ServoStyleSet* styleSet = mPresShell->StyleSet();
  mFontPaletteValueSet = styleSet->BuildFontPaletteValueSet();
  mFontPaletteValuesDirty = false;

  if (mFontPaletteCache) {
    mFontPaletteCache->SetPaletteValueSet(mFontPaletteValueSet);
  }

  // Even if we're not reflowing anything, a change to the palette means we
  // need to repaint in order to show the new colors.
  InvalidatePaintedLayers();
}

gfx::PaletteCache& nsPresContext::FontPaletteCache() {
  if (!mFontPaletteCache) {
    mFontPaletteCache = MakeUnique<gfx::PaletteCache>(mFontPaletteValueSet);
  }
  return *mFontPaletteCache.get();
}

void nsPresContext::SetVisibleArea(const nsRect& aRect) {
  if (!aRect.IsEqualEdges(mVisibleArea)) {
    mVisibleArea = aRect;
    mSizeForViewportUnits = mVisibleArea.Size();
    AdjustSizeForViewportUnits();
    // Visible area does not affect media queries when paginated.
    if (!IsRootPaginatedDocument()) {
      MediaFeatureValuesChanged(
          {mozilla::MediaFeatureChangeReason::ViewportChange},
          MediaFeatureChangePropagation::JustThisDocument);
    }

    UpdateTopInnerSizeForRFP();
  }
}

void nsPresContext::SetInitialVisibleArea(const nsRect& aRect) {
  MOZ_ASSERT(mPresShell);

  if (RefPtr<MobileViewportManager> mvm =
          mPresShell->GetMobileViewportManager()) {
    // On mobile environments the top level content document viewport size is
    // depending on meta viewport tags in the document so we use it rather than
    // using the given document viewer size directly.
    SetVisibleArea(mvm->InitialVisibleArea());
    return;
  }
  SetVisibleArea(aRect);
}

void nsPresContext::SetDynamicToolbarMaxHeight(ScreenIntCoord aHeight) {
  MOZ_ASSERT(IsRootContentDocumentCrossProcess());

  if (mDynamicToolbarMaxHeight == aHeight) {
    return;
  }
  mDynamicToolbarMaxHeight = aHeight;
  mDynamicToolbarHeight = aHeight;

  AdjustSizeForViewportUnits();

  if (RefPtr<mozilla::PresShell> presShell = mPresShell) {
    // Changing the max height of the dynamic toolbar changes the ICB size, we
    // need to kick a reflow with the current window dimensions since the max
    // height change doesn't change the window dimensions but
    // PresShell::ResizeReflow ends up subtracting the new dynamic toolbar
    // height from the window dimensions and kick a reflow with the proper ICB
    // size.
    presShell->ForceResizeReflowWithCurrentDimensions();
  }
}

void nsPresContext::AdjustSizeForViewportUnits() {
  if (!HasDynamicToolbar()) {
    return;
  }

  if (MOZ_UNLIKELY(mVisibleArea.height == NS_UNCONSTRAINEDSIZE)) {
    // Ignore `NS_UNCONSTRAINEDSIZE` since it's a temporary state during a
    // reflow. We will end up calling this function again with a proper size in
    // the same reflow.
    return;
  }

  nscoord toolbarMaxHeight = GetDynamicToolbarMaxHeightInAppUnits();
  if (MOZ_UNLIKELY(mVisibleArea.height + toolbarMaxHeight > nscoord_MAX)) {
    MOZ_ASSERT_UNREACHABLE("The dynamic toolbar max height is probably wrong");
    return;
  }

  mSizeForViewportUnits.height = mVisibleArea.height + toolbarMaxHeight;
}

void nsPresContext::UpdateDynamicToolbarOffset(ScreenIntCoord aOffset) {
  MOZ_ASSERT(IsRootContentDocumentCrossProcess());
  if (!mPresShell) {
    return;
  }

  if (!HasDynamicToolbar()) {
    return;
  }

  MOZ_ASSERT(-mDynamicToolbarMaxHeight <= aOffset && aOffset <= 0);
  if (mDynamicToolbarHeight == mDynamicToolbarMaxHeight + aOffset) {
    return;
  }

  dom::InteractiveWidget interactiveWidget = mDocument->InteractiveWidget();
  if (interactiveWidget == InteractiveWidget::OverlaysContent &&
      GetKeyboardHeight() > 0) {
    // On overlays-content mode, the toolbar offset change should NOT affect
    // the visual viewport while the software keyboard is being shown since
    // the toolbar will be positioned somewhere in the middle of the visual
    // viewport.
    return;
  }

  // Forcibly flush position:fixed elements and position:sticky elements stuck
  // to the root scroll container in the case where the dynamic toolbar is
  // going to be completely hidden or starts to be visible so that %-based style
  // values will be recomputed with the visual viewport size which is including
  // the area covered by the dynamic toolbar, it also ensures that each
  // position:fixed or position:sticky element is painted at the correct
  // position on the main-thread.
  if (mDynamicToolbarHeight == 0 || aOffset == -mDynamicToolbarMaxHeight) {
    mPresShell->MarkFixedFramesForReflow(IntrinsicDirty::None);
    mPresShell->MarkStickyFramesForReflow();
    mPresShell->ScheduleResizeEventIfNeeded(
        PresShell::ResizeEventKind::Regular);
  }

  mDynamicToolbarHeight = mDynamicToolbarMaxHeight + aOffset;

  if (RefPtr<MobileViewportManager> mvm =
          mPresShell->GetMobileViewportManager()) {
    mvm->UpdateVisualViewportSizeByDynamicToolbar(-aOffset);
  }

  mPresShell->StyleSet()->InvalidateForViewportUnits(
      ServoStyleSet::OnlyDynamic::Yes);
}

void nsPresContext::UpdateKeyboardHeight(ScreenIntCoord aHeight) {
  MOZ_ASSERT(IsRootContentDocumentCrossProcess());
  if (!mPresShell) {
    return;
  }

  if (RefPtr<MobileViewportManager> mvm =
          mPresShell->GetMobileViewportManager()) {
    mvm->UpdateKeyboardHeight(aHeight);
  }
}

ScreenIntCoord nsPresContext::GetKeyboardHeight() const {
  MobileViewportManager* mvm = mPresShell->GetMobileViewportManager();
  return mvm ? mvm->GetKeyboardHeight() : ScreenIntCoord(0);
}

bool nsPresContext::IsKeyboardHiddenOrResizesContentMode() const {
  return GetKeyboardHeight() == 0 ||
         mDocument->InteractiveWidget() == InteractiveWidget::ResizesContent;
}

DynamicToolbarState nsPresContext::GetDynamicToolbarState() const {
  if (!HasDynamicToolbar()) {
    return DynamicToolbarState::None;
  }
  if (mDynamicToolbarMaxHeight == mDynamicToolbarHeight) {
    return DynamicToolbarState::Expanded;
  }
  if (mDynamicToolbarHeight == 0) {
    return DynamicToolbarState::Collapsed;
  }
  return DynamicToolbarState::InTransition;
}

static CSSToScreenScale GetMVMScale(PresShell* aPS) {
  if (aPS) {
    if (RefPtr mvm = aPS->GetMobileViewportManager()) {
      return mvm->GetZoom();
    }
  }
  return CSSToScreenScale(1.0f);
}

nscoord nsPresContext::GetDynamicToolbarMaxHeightInAppUnits() const {
  if (mDynamicToolbarMaxHeight == 0) {
    return 0;
  }
  return CSSPixel::ToAppUnits(
      ScreenCoord(GetDynamicToolbarMaxHeight()) /
      // For viewport units the dynamic toolbar height needs to be the height
      // as if this document is scaled by 1.0 whatever the current zoom scale
      // is.
      (CSSToDevPixelScale() * LayoutDeviceToScreenScale(1.0)));
}

nscoord nsPresContext::GetBimodalDynamicToolbarHeightInAppUnits() const {
  if (mDynamicToolbarMaxHeight == 0) {
    return 0;
  }
  return GetDynamicToolbarState() == DynamicToolbarState::Collapsed
             ? CSSPixel::ToAppUnits(ScreenCoord(GetDynamicToolbarMaxHeight()) /
                                    GetMVMScale(mPresShell))
             : 0;
}

void nsPresContext::SetSafeAreaInsets(
    const LayoutDeviceIntMargin& aSafeAreaInsets) {
  if (mSafeAreaInsets == aSafeAreaInsets) {
    return;
  }
  mSafeAreaInsets = aSafeAreaInsets;

  PostRebuildAllStyleDataEvent(nsChangeHint(0),
                               RestyleHint::RecascadeSubtree());
}

PerformanceMainThread* nsPresContext::GetPerformanceMainThread() const {
  if (nsPIDOMWindowInner* innerWindow = mDocument->GetInnerWindow()) {
    if (auto* perf = static_cast<PerformanceMainThread*>(
            innerWindow->GetPerformance())) {
      return perf;
    }
  }
  return nullptr;
}

void nsPresContext::DoUpdateHiddenByContentVisibilityForAnimations() {
  MOZ_ASSERT(NeedsToUpdateHiddenByContentVisibilityForAnimations());
  mNeedsToUpdateHiddenByContentVisibilityForAnimations = false;
  mDocument->UpdateHiddenByContentVisibilityForAnimations();
  TimelineManager()->UpdateHiddenByContentVisibilityForAnimations();
}

#ifdef DEBUG

void nsPresContext::ValidatePresShellAndDocumentReleation() const {
  NS_ASSERTION(!mPresShell || !mPresShell->GetDocument() ||
                   mPresShell->GetDocument() == mDocument,
               "nsPresContext doesn't have the same document as nsPresShell!");
}

#endif  // #ifdef DEBUG

nsRootPresContext::nsRootPresContext(dom::Document* aDocument,
                                     nsPresContextType aType)
    : nsPresContext(aDocument, aType) {}

void nsRootPresContext::AddWillPaintObserver(nsIRunnable* aRunnable) {
  if (!mWillPaintFallbackEvent.IsPending()) {
    mWillPaintFallbackEvent = new RunWillPaintObservers(this);
    Document()->Dispatch(do_AddRef(mWillPaintFallbackEvent));
  }
  mWillPaintObservers.AppendElement(aRunnable);
}

/**
 * Run all runnables that need to get called before the next paint.
 */
void nsRootPresContext::FlushWillPaintObservers() {
  mWillPaintFallbackEvent = nullptr;
  nsTArray<nsCOMPtr<nsIRunnable>> observers = std::move(mWillPaintObservers);
  for (uint32_t i = 0; i < observers.Length(); ++i) {
    observers[i]->Run();
  }
}

size_t nsRootPresContext::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return nsPresContext::SizeOfExcludingThis(aMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mWillPaintObservers
  // - mWillPaintFallbackEvent
}
