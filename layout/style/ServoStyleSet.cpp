/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleSet.h"

#include "COLRFonts.h"
#include "PseudoStyleType.h"
#include "gfxUserFontSet.h"
#include "mozilla/AttributeStyles.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Keyframe.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/ServoStyleRuleMap.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/ServoTypes.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/CSSAppearanceBaseRule.h"
#include "mozilla/dom/CSSBinding.h"
#include "mozilla/dom/CSSContainerRule.h"
#include "mozilla/dom/CSSCounterStyleRule.h"
#include "mozilla/dom/CSSCustomMediaRule.h"
#include "mozilla/dom/CSSFontFaceRule.h"
#include "mozilla/dom/CSSFontFeatureValuesRule.h"
#include "mozilla/dom/CSSFontPaletteValuesRule.h"
#include "mozilla/dom/CSSImportRule.h"
#include "mozilla/dom/CSSKeyframeRule.h"
#include "mozilla/dom/CSSKeyframesRule.h"
#include "mozilla/dom/CSSLayerBlockRule.h"
#include "mozilla/dom/CSSLayerStatementRule.h"
#include "mozilla/dom/CSSMarginRule.h"
#include "mozilla/dom/CSSMediaRule.h"
#include "mozilla/dom/CSSMozDocumentRule.h"
#include "mozilla/dom/CSSNamespaceRule.h"
#include "mozilla/dom/CSSNestedDeclarations.h"
#include "mozilla/dom/CSSPageRule.h"
#include "mozilla/dom/CSSPositionTryRule.h"
#include "mozilla/dom/CSSPropertyRule.h"
#include "mozilla/dom/CSSScopeRule.h"
#include "mozilla/dom/CSSStartingStyleRule.h"
#include "mozilla/dom/CSSStyleRule.h"
#include "mozilla/dom/CSSSupportsRule.h"
#include "mozilla/dom/CSSViewTransitionRule.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/ViewTransition.h"
#include "nsCSSFrameConstructor.h"
#include "nsDeviceContext.h"
#include "nsIAnonymousContentCreator.h"
#include "nsLayoutUtils.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "nsWindowSizes.h"

namespace mozilla {

using namespace dom;

#ifdef DEBUG
bool ServoStyleSet::IsCurrentThreadInServoTraversal() {
  return sInServoTraversal && (NS_IsMainThread() || Servo_IsWorkerThread());
}
#endif

// The definition of kOrigins relies on this.
static_assert(static_cast<uint8_t>(StyleOrigin::UserAgent) ==
              static_cast<uint8_t>(OriginFlags::UserAgent));
static_assert(static_cast<uint8_t>(StyleOrigin::User) ==
              static_cast<uint8_t>(OriginFlags::User));
static_assert(static_cast<uint8_t>(StyleOrigin::Author) ==
              static_cast<uint8_t>(OriginFlags::Author));

constexpr const StyleOrigin ServoStyleSet::kOrigins[];

ServoStyleSet* sInServoTraversal = nullptr;

// On construction, sets sInServoTraversal to the given ServoStyleSet.
// On destruction, clears sInServoTraversal and calls RunPostTraversalTasks.
class MOZ_RAII AutoSetInServoTraversal {
 public:
  explicit AutoSetInServoTraversal(ServoStyleSet* aSet) : mSet(aSet) {
    MOZ_ASSERT(!sInServoTraversal);
    MOZ_ASSERT(aSet);
    sInServoTraversal = aSet;
  }

  ~AutoSetInServoTraversal() {
    MOZ_ASSERT(sInServoTraversal);
    sInServoTraversal = nullptr;
    mSet->RunPostTraversalTasks();
  }

 private:
  ServoStyleSet* mSet;
};

// Sets up for one or more calls to Servo_TraverseSubtree.
class MOZ_RAII AutoPrepareTraversal : public AutoSetInServoTraversal {
 public:
  explicit AutoPrepareTraversal(ServoStyleSet* aSet)
      : AutoSetInServoTraversal(aSet) {
    MOZ_ASSERT(!aSet->StylistNeedsUpdate());
  }
};

ServoStyleSet::ServoStyleSet(Document& aDocument) : mDocument(&aDocument) {
  PodArrayZero(mCachedAnonymousContentStyleIndexes);
  mRawData.reset(Servo_StyleSet_Init(&aDocument));
}

ServoStyleSet::~ServoStyleSet() {
  MOZ_ASSERT(!IsInServoTraversal());
  EnumerateStyleSheets([&](StyleSheet& aSheet) { aSheet.DropStyleSet(this); });
}

nsPresContext* ServoStyleSet::GetPresContext() {
  return mDocument->GetPresContext();
}

template <typename Functor>
static void EnumerateShadowRoots(const Document& aDoc, const Functor& aCb) {
  const Document::ShadowRootSet& shadowRoots = aDoc.ComposedShadowRoots();
  for (ShadowRoot* root : shadowRoots) {
    MOZ_ASSERT(root);
    MOZ_DIAGNOSTIC_ASSERT(root->IsInComposedDoc());
    aCb(*root);
  }
}

void ServoStyleSet::ShellDetachedFromDocument() {
  ClearNonInheritingComputedStyles();
  mCachedAnonymousContentStyles.Clear();
  PodArrayZero(mCachedAnonymousContentStyleIndexes);
  mStyleRuleMap = nullptr;

  // Remove all our stylesheets...
  for (auto origin : kOrigins) {
    for (size_t count = SheetCount(origin); count--;) {
      RemoveStyleSheet(*SheetAt(origin, count));
    }
  }

  // And remove all the CascadeDatas from memory.
  UpdateStylistIfNeeded();

  // Also GC the ruletree if it got big now that the DOM no longer has
  // references to styles around anymore.
  MaybeGCRuleTree();
}

void ServoStyleSet::RecordShadowStyleChange(ShadowRoot& aShadowRoot) {
  // TODO(emilio): We could keep track of the actual shadow roots that need
  // their styles recomputed.
  SetStylistShadowDOMStyleSheetsDirty();

  // FIXME(emilio): This should be done using stylesheet invalidation instead.
  if (nsPresContext* pc = GetPresContext()) {
    pc->RestyleManager()->PostRestyleEvent(
        aShadowRoot.Host(), RestyleHint::RestyleSubtree(), nsChangeHint(0));
  }
}

void ServoStyleSet::InvalidateStyleForDocumentStateChanges(
    DocumentState aStatesChanged) {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(!aStatesChanged.IsEmpty());

  nsPresContext* pc = GetPresContext();
  if (!pc) {
    return;
  }

  Element* root = mDocument->GetRootElement();
  if (!root) {
    return;
  }

  // TODO(emilio): It may be nicer to just invalidate stuff in a given subtree
  // for Shadow DOM. Consider just enumerating shadow roots instead and run
  // invalidation individually, passing mRawData for the UA / User sheets.
  AutoTArray<const StyleAuthorStyles*, 20> nonDocumentStyles;

  EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
    if (auto* authorStyles = aShadowRoot.GetServoStyles()) {
      nonDocumentStyles.AppendElement(authorStyles);
    }
  });

  Servo_InvalidateStyleForDocStateChanges(root, mRawData.get(),
                                          &nonDocumentStyles,
                                          aStatesChanged.GetInternalValue());
}

static const MediaFeatureChangeReason kMediaFeaturesAffectingDefaultStyle =
    // Zoom changes change the meaning of em units.
    MediaFeatureChangeReason::ZoomChange |
    // A resolution change changes the app-units-per-dev-pixels ratio, which
    // some structs (Border, Outline, Column) store for clamping. We should
    // arguably not do that, maybe doing it on layout directly, to try to avoid
    // relying on the pres context (bug 1418159).
    MediaFeatureChangeReason::ResolutionChange;

RestyleHint ServoStyleSet::MediumFeaturesChanged(
    MediaFeatureChangeReason aReason) {
  AutoTArray<StyleAuthorStyles*, 20> nonDocumentStyles;

  EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
    if (auto* authorStyles = aShadowRoot.GetServoStyles()) {
      nonDocumentStyles.AppendElement(authorStyles);
    }
  });

  const bool mayAffectDefaultStyle =
      bool(aReason & kMediaFeaturesAffectingDefaultStyle);
  const MediumFeaturesChangedResult result =
      Servo_StyleSet_MediumFeaturesChanged(mRawData.get(), &nonDocumentStyles,
                                           mayAffectDefaultStyle);

  const bool viewportChanged =
      bool(aReason & MediaFeatureChangeReason::ViewportChange);
  if (viewportChanged) {
    InvalidateForViewportUnits(OnlyDynamic::No);
  }

  const bool rulesChanged =
      result.mAffectsDocumentRules || result.mAffectsNonDocumentRules;

  if (result.mAffectsDocumentRules) {
    SetStylistStyleSheetsDirty();
  }

  if (result.mAffectsNonDocumentRules) {
    SetStylistShadowDOMStyleSheetsDirty();
  }

  if (rulesChanged) {
    // TODO(emilio): This could be more granular.
    return RestyleHint::RestyleSubtree();
  }

  return RestyleHint{0};
}

MOZ_DEFINE_MALLOC_SIZE_OF(ServoStyleSetMallocSizeOf)
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(ServoStyleSetMallocEnclosingSizeOf)

void ServoStyleSet::AddSizeOfIncludingThis(nsWindowSizes& aSizes) const {
  MallocSizeOf mallocSizeOf = aSizes.mState.mMallocSizeOf;

  aSizes.mLayoutStyleSetsOther += mallocSizeOf(this);

  if (mRawData) {
    aSizes.mLayoutStyleSetsOther += mallocSizeOf(mRawData.get());
    ServoStyleSetSizes sizes;
    // Measure mRawData. We use ServoStyleSetMallocSizeOf rather than
    // aMallocSizeOf to distinguish in DMD's output the memory measured within
    // Servo code.
    Servo_StyleSet_AddSizeOfExcludingThis(ServoStyleSetMallocSizeOf,
                                          ServoStyleSetMallocEnclosingSizeOf,
                                          &sizes, mRawData.get());

    // The StyleSet does not contain precomputed pseudos; they are in the UA
    // cache.
    MOZ_RELEASE_ASSERT(sizes.mPrecomputedPseudos == 0);

    aSizes.mLayoutStyleSetsStylistRuleTree += sizes.mRuleTree;
    aSizes.mLayoutStyleSetsStylistElementAndPseudosMaps +=
        sizes.mElementAndPseudosMaps;
    aSizes.mLayoutStyleSetsStylistInvalidationMap += sizes.mInvalidationMap;
    aSizes.mLayoutStyleSetsStylistRevalidationSelectors +=
        sizes.mRevalidationSelectors;
    aSizes.mLayoutStyleSetsStylistOther += sizes.mOther;
  }

  if (mStyleRuleMap) {
    aSizes.mLayoutStyleSetsOther +=
        mStyleRuleMap->SizeOfIncludingThis(aSizes.mState.mMallocSizeOf);
  }

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mSheets
  // - mNonInheritingComputedStyles
  //
  // The following members are not measured:
  // - mDocument, because it a non-owning pointer
}

void ServoStyleSet::SetAuthorStyleDisabled(bool aStyleDisabled) {
  if (mAuthorStyleDisabled == aStyleDisabled) {
    return;
  }

  mAuthorStyleDisabled = aStyleDisabled;
  if (Element* root = mDocument->GetRootElement()) {
    if (nsPresContext* pc = GetPresContext()) {
      pc->RestyleManager()->PostRestyleEvent(
          root, RestyleHint::RestyleSubtree(), nsChangeHint(0));
    }
  }
  Servo_StyleSet_SetAuthorStyleDisabled(mRawData.get(), mAuthorStyleDisabled);
  // XXX Workaround for bug 1437785.
  SetStylistStyleSheetsDirty();
}

const ServoElementSnapshotTable& ServoStyleSet::Snapshots() {
  MOZ_ASSERT(GetPresContext(), "Styling a document without a shell?");
  return GetPresContext()->RestyleManager()->Snapshots();
}

void ServoStyleSet::PreTraverseSync() {
  // Get the Document's root element to ensure that the cache is valid before
  // calling into the (potentially-parallel) Servo traversal, where a cache hit
  // is necessary to avoid a data race when updating the cache.
  (void)mDocument->GetRootElement();

  // FIXME(emilio): These two shouldn't be needed in theory, the call to the
  // same function in PresShell should do the work, but as it turns out we
  // ProcessPendingRestyles() twice, and runnables from frames just constructed
  // can end up doing editing stuff, which adds stylesheets etc...
  mDocument->FlushUserFontSet();
  UpdateStylistIfNeeded();

  mDocument->ResolveScheduledPresAttrs();

  if (gfxUserFontSet* userFontSet = mDocument->GetUserFontSet()) {
    nsPresContext* presContext = GetPresContext();
    MOZ_ASSERT(presContext,
               "For now, we don't call into here without a pres context");

    // Ensure that the @font-face data is not stale
    uint64_t generation = userFontSet->GetGeneration();
    if (generation != mUserFontSetUpdateGeneration) {
      mDocument->GetFonts()->CacheFontLoadability();
      presContext->UpdateFontCacheUserFonts(userFontSet);
      mUserFontSetUpdateGeneration = generation;
    }
  }

  MOZ_ASSERT(!StylistNeedsUpdate());
}

void ServoStyleSet::PreTraverse(ServoTraversalFlags aFlags, Element* aRoot) {
  PreTraverseSync();

  // Process animation stuff that we should avoid doing during the parallel
  // traversal.
  SMILAnimationController* smilController =
      mDocument->HasAnimationController() ? mDocument->GetAnimationController()
                                          : nullptr;

  MOZ_ASSERT(GetPresContext());
  if (aRoot) {
    GetPresContext()->EffectCompositor()->PreTraverseInSubtree(aFlags, aRoot);
    if (smilController) {
      smilController->PreTraverseInSubtree(aRoot);
    }
  } else {
    GetPresContext()->EffectCompositor()->PreTraverse(aFlags);
    if (smilController) {
      smilController->PreTraverse();
    }
  }
}

static inline already_AddRefed<ComputedStyle>
ResolveStyleForTextOrFirstLetterContinuation(
    const StylePerDocumentStyleData* aRawData, ComputedStyle& aParent,
    PseudoStyleType aType) {
  MOZ_ASSERT(aType == PseudoStyleType::MozText ||
             aType == PseudoStyleType::MozFirstLetterContinuation);
  auto inheritTarget = aType == PseudoStyleType::MozText
                           ? InheritTarget::Text
                           : InheritTarget::FirstLetterContinuation;

  RefPtr<ComputedStyle> style = aParent.GetCachedInheritingAnonBoxStyle(aType);
  if (!style) {
    style =
        Servo_ComputedValues_Inherit(aRawData, aType, &aParent, inheritTarget)
            .Consume();
    MOZ_ASSERT(style);
    aParent.SetCachedInheritedAnonBoxStyle(style);
  }

  return style.forget();
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolveStyleForText(
    nsIContent* aTextNode, ComputedStyle* aParentStyle) {
  MOZ_ASSERT(aTextNode && aTextNode->IsText());
  MOZ_ASSERT(aTextNode->GetParent());
  MOZ_ASSERT(aParentStyle);

  return ResolveStyleForTextOrFirstLetterContinuation(
      mRawData.get(), *aParentStyle, PseudoStyleType::MozText);
}

already_AddRefed<ComputedStyle>
ServoStyleSet::ResolveStyleForFirstLetterContinuation(
    ComputedStyle* aParentStyle) {
  MOZ_ASSERT(aParentStyle);

  return ResolveStyleForTextOrFirstLetterContinuation(
      mRawData.get(), *aParentStyle,
      PseudoStyleType::MozFirstLetterContinuation);
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolveStyleForPlaceholder() {
  RefPtr<ComputedStyle>& cache =
      mNonInheritingComputedStyles[NonInheritingAnonBox::MozOofPlaceholder];
  if (cache) {
    RefPtr<ComputedStyle> retval = cache;
    return retval.forget();
  }

  RefPtr<ComputedStyle> computedValues =
      Servo_ComputedValues_Inherit(mRawData.get(),
                                   PseudoStyleType::MozOofPlaceholder, nullptr,
                                   InheritTarget::PlaceholderFrame)
          .Consume();
  MOZ_ASSERT(computedValues);

  cache = computedValues;
  return computedValues.forget();
}

static inline bool LazyPseudoIsCacheable(PseudoStyleType aType,
                                         const Element& aOriginatingElement,
                                         ComputedStyle* aParentStyle) {
  return aParentStyle && !PseudoStyle::IsEagerlyCascadedInServo(aType) &&
         aOriginatingElement.HasServoData() &&
         !Servo_Element_IsPrimaryStyleReusedViaRuleNode(&aOriginatingElement);
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolvePseudoElementStyle(
    const Element& aOriginatingElement, PseudoStyleType aType,
    nsAtom* aFunctionalPseudoParameter, ComputedStyle* aParentStyle,
    IsProbe aIsProbe) {
  // Runs from frame construction, this should have clean styles already, except
  // with non-lazy FC...
  UpdateStylistIfNeeded();
  MOZ_ASSERT(PseudoStyle::IsPseudoElement(aType));

  const bool cacheable =
      LazyPseudoIsCacheable(aType, aOriginatingElement, aParentStyle);
  RefPtr<ComputedStyle> style = cacheable
                                    ? aParentStyle->GetCachedLazyPseudoStyle(
                                          {aType, aFunctionalPseudoParameter})
                                    : nullptr;

  const bool isProbe = aIsProbe == IsProbe::Yes;

  if (!style) {
    // FIXME(emilio): Why passing null for probing as the parent style?
    //
    // There are callers which do pass the wrong parent style and it would
    // assert (like ComputeSelectionStyle()). That's messy!
    style = Servo_ResolvePseudoStyle(
                &aOriginatingElement, aType, aFunctionalPseudoParameter,
                isProbe, isProbe ? nullptr : aParentStyle, mRawData.get())
                .Consume();
    if (!style) {
      MOZ_ASSERT(isProbe);
      if (cacheable) {
        aParentStyle->SetCachedLazyPseudoStyle(nullptr, aType,
                                               aFunctionalPseudoParameter);
      }
      return nullptr;
    }
    if (cacheable) {
      const bool shouldCache = [&] {
        if (style->HasAttrReferences()) {
          // We can't cache styles referencing attr() functions here, because we
          // don't make the relevant attribute part of the cache key.
          return false;
        }
        if (style->UsesViewportUnits()) {
          // Don't cache styles with viewport units if the parent style differs
          // from the element's primary frame style. This can happen with
          // ::first-line, where text frames have a combined style. Cached lazy
          // pseudos on such combined styles aren't findable during viewport
          // invalidation, so we must recompute them each time to ensure correct
          // values. Note: Container units that fall back to viewport size
          // already set the USES_VIEWPORT_UNITS flag, so we only need to check
          // that.
          if (const auto* primaryFrame =
                  aOriginatingElement.GetPrimaryFrame()) {
            if (primaryFrame->Style() != aParentStyle) {
              return false;
            }
          }
        }
        return true;
      }();
      if (shouldCache) {
        aParentStyle->SetCachedLazyPseudoStyle(style, aType,
                                               aFunctionalPseudoParameter);
      }
    }
  }

  MOZ_ASSERT(style);

  if (isProbe && !GeneratedContentPseudoExists(*aParentStyle, *style)) {
    return nullptr;
  }

  return style.forget();
}

already_AddRefed<ComputedStyle>
ServoStyleSet::ResolveInheritingAnonymousBoxStyle(PseudoStyleType aType,
                                                  ComputedStyle* aParentStyle) {
  MOZ_ASSERT(PseudoStyle::IsInheritingAnonBox(aType));
  MOZ_ASSERT_IF(aParentStyle, !StylistNeedsUpdate());

  UpdateStylistIfNeeded();

  RefPtr<ComputedStyle> style = nullptr;

  if (aParentStyle) {
    style = aParentStyle->GetCachedInheritingAnonBoxStyle(aType);
  }

  if (!style) {
    style = Servo_ComputedValues_GetForAnonymousBox(aParentStyle, aType,
                                                    mRawData.get())
                .Consume();
    MOZ_ASSERT(style);
    if (aParentStyle) {
      aParentStyle->SetCachedInheritedAnonBoxStyle(style);
    }
  }

  return style.forget();
}

already_AddRefed<ComputedStyle>
ServoStyleSet::ResolveNonInheritingAnonymousBoxStyle(PseudoStyleType aType) {
  MOZ_ASSERT(PseudoStyle::IsNonInheritingAnonBox(aType));

  auto type = static_cast<NonInheritingAnonBox>(aType);
  RefPtr<ComputedStyle>& cache = mNonInheritingComputedStyles[type];
  if (cache) {
    RefPtr<ComputedStyle> retval = cache;
    return retval.forget();
  }

  UpdateStylistIfNeeded();
  RefPtr<ComputedStyle> computedValues =
      Servo_ComputedValues_GetForAnonymousBox(nullptr, aType, mRawData.get())
          .Consume();
  MOZ_ASSERT(computedValues);

  cache = computedValues;
  return computedValues.forget();
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolvePageContentStyle(
    const nsAtom* aPageName, const StylePagePseudoClassFlags& aPseudo) {
  // The empty atom is used to indicate no specified page name, and is not
  // usable as a page-rule selector. Changing this to null is a slight
  // optimization to avoid the Servo code from doing an unnecessary hashtable
  // lookup, and still use the style cache in this case.
  if (aPageName == nsGkAtoms::_empty) {
    aPageName = nullptr;
  }
  // Inherit from the root element, see
  // https://drafts.csswg.org/css-page/#page-properties
  RefPtr<const ComputedStyle> parentStyle;
  if (Element* root = mDocument->GetRootElement()) {
    parentStyle = Servo_Element_GetMaybeOutOfDateStyle(root);
  }

  // Only use the cache when we are doing a lookup for page styles without a
  // page-name or any pseudo classes, and the root element is already styled.
  const bool useCache = !aPageName && !aPseudo && parentStyle;
  if (useCache && mDefaultPageStyleCache) {
    return do_AddRef(mDefaultPageStyleCache);
  }

  UpdateStylistIfNeeded();

  RefPtr<ComputedStyle> computedValues =
      Servo_ComputedValues_GetForPageContent(mRawData.get(), parentStyle,
                                             aPageName, aPseudo)
          .Consume();
  MOZ_ASSERT(computedValues);

  if (useCache) {
    mDefaultPageStyleCache = computedValues;
  }
  return computedValues.forget();
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolveXULTreePseudoStyle(
    dom::Element* aParentElement, PseudoStyleType aType,
    ComputedStyle* aParentStyle, const AtomArray& aInputWord) {
  MOZ_ASSERT(aParentStyle);
  NS_ASSERTION(!StylistNeedsUpdate(),
               "Stylesheets modified when resolving XUL tree pseudo");

  return Servo_ComputedValues_ResolveXULTreePseudoStyle(
             aParentElement, aType, aParentStyle, &aInputWord, mRawData.get())
      .Consume();
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolvePositionTry(
    StyleCascadeLevel aScope, dom::Element& aElement,
    const ComputedStyle& aStyle,
    const StylePositionTryFallbacksItem& aFallback) {
  return Servo_ComputedValues_GetForPositionTry(mRawData.get(), &aStyle, aScope,
                                                &aElement, &aFallback)
      .Consume();
}

// manage the set of style sheets in the style set
void ServoStyleSet::AppendStyleSheet(StyleSheet& aSheet) {
  MOZ_ASSERT(aSheet.IsApplicable());
  MOZ_ASSERT(aSheet.RawContents(),
             "Raw sheet should be in place before insertion.");

  aSheet.AddStyleSet(this);

  // Maintain a mirrored list of sheets on the servo side.
  // Servo will remove aSheet from its original position as part of the call
  // to Servo_StyleSet_AppendStyleSheet.
  Servo_StyleSet_AppendStyleSheet(mRawData.get(), &aSheet);
  SetStylistStyleSheetsDirty();

  if (mStyleRuleMap) {
    mStyleRuleMap->SheetAdded(aSheet);
  }
}

void ServoStyleSet::RemoveStyleSheet(StyleSheet& aSheet) {
  aSheet.DropStyleSet(this);

  // Maintain a mirrored list of sheets on the servo side.
  Servo_StyleSet_RemoveStyleSheet(mRawData.get(), &aSheet);
  SetStylistStyleSheetsDirty();

  if (mStyleRuleMap) {
    mStyleRuleMap->SheetRemoved(aSheet);
  }
}

void ServoStyleSet::InsertStyleSheetBefore(StyleSheet& aNewSheet,
                                           StyleSheet& aReferenceSheet) {
  MOZ_ASSERT(aNewSheet.IsApplicable());
  MOZ_ASSERT(aReferenceSheet.IsApplicable());
  MOZ_ASSERT(&aNewSheet != &aReferenceSheet,
             "Can't place sheet before itself.");
  MOZ_ASSERT(aNewSheet.GetOrigin() == aReferenceSheet.GetOrigin(),
             "Sheets should be in the same origin");
  MOZ_ASSERT(aNewSheet.RawContents(),
             "Raw sheet should be in place before insertion.");
  MOZ_ASSERT(aReferenceSheet.RawContents(),
             "Reference sheet should have a raw sheet.");

  // Servo will remove aNewSheet from its original position as part of the
  // call to Servo_StyleSet_InsertStyleSheetBefore.
  aNewSheet.AddStyleSet(this);

  // Maintain a mirrored list of sheets on the servo side.
  Servo_StyleSet_InsertStyleSheetBefore(mRawData.get(), &aNewSheet,
                                        &aReferenceSheet);
  SetStylistStyleSheetsDirty();

  if (mStyleRuleMap) {
    mStyleRuleMap->SheetAdded(aNewSheet);
  }
}

size_t ServoStyleSet::SheetCount(Origin aOrigin) const {
  return Servo_StyleSet_GetSheetCount(mRawData.get(), aOrigin);
}

StyleSheet* ServoStyleSet::SheetAt(Origin aOrigin, size_t aIndex) const {
  return const_cast<StyleSheet*>(
      Servo_StyleSet_GetSheetAt(mRawData.get(), aOrigin, aIndex));
}

ServoStyleSet::PageSizeAndOrientation
ServoStyleSet::GetDefaultPageSizeAndOrientation() {
  PageSizeAndOrientation retval;
  const RefPtr<ComputedStyle> style =
      ResolvePageContentStyle(nullptr, StylePagePseudoClassFlags::NONE);
  const StylePageSize& pageSize = style->StylePage()->mSize;

  if (pageSize.IsSize()) {
    const nscoord w = pageSize.AsSize().width.ToAppUnits();
    const nscoord h = pageSize.AsSize().height.ToAppUnits();
    // Ignoring sizes that include a zero width or height.
    // These are also ignored in nsPageFrame::ComputePageSize()
    // when calculating the scaling for a page size.
    // In bug 1807985, we might add similar handling for @page margin/size
    // combinations that produce a zero-sized page-content box.
    if (w > 0 && h > 0) {
      retval.size.emplace(w, h);
      if (w > h) {
        retval.orientation.emplace(StylePageSizeOrientation::Landscape);
      } else if (w < h) {
        retval.orientation.emplace(StylePageSizeOrientation::Portrait);
      }
    }
  } else if (pageSize.IsOrientation()) {
    retval.orientation.emplace(pageSize.AsOrientation());
  } else {
    MOZ_ASSERT(pageSize.IsAuto(), "Impossible page size");
  }
  return retval;
}

void ServoStyleSet::AppendAllNonDocumentAuthorSheets(
    nsTArray<StyleSheet*>& aArray) const {
  EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
    for (auto index : IntegerRange(aShadowRoot.SheetCount())) {
      aArray.AppendElement(aShadowRoot.SheetAt(index));
    }
    aArray.AppendElements(aShadowRoot.AdoptedStyleSheets());
  });
}

void ServoStyleSet::AddDocStyleSheet(StyleSheet& aSheet) {
  MOZ_ASSERT(aSheet.IsApplicable());
  MOZ_ASSERT(aSheet.RawContents(),
             "Raw sheet should be in place by this point.");

  size_t index = mDocument->FindDocStyleSheetInsertionPoint(aSheet);
  aSheet.AddStyleSet(this);

  if (index < SheetCount(Origin::Author)) {
    // This case is insert before.
    StyleSheet* beforeSheet = SheetAt(Origin::Author, index);
    Servo_StyleSet_InsertStyleSheetBefore(mRawData.get(), &aSheet, beforeSheet);
  } else {
    Servo_StyleSet_AppendStyleSheet(mRawData.get(), &aSheet);
  }
  SetStylistStyleSheetsDirty();

  if (mStyleRuleMap) {
    mStyleRuleMap->SheetAdded(aSheet);
  }
}

bool ServoStyleSet::GeneratedContentPseudoExists(
    const ComputedStyle& aParentStyle, const ComputedStyle& aPseudoStyle) {
  auto type = aPseudoStyle.GetPseudoType();
  MOZ_ASSERT(type != PseudoStyleType::NotPseudo);

  if (type == PseudoStyleType::Marker) {
    // ::marker only exist for list items (for now).
    if (!aParentStyle.StyleDisplay()->IsListItem()) {
      return false;
    }
    const auto& content = aPseudoStyle.StyleContent()->mContent;
    // ::marker does not exist if 'content' is 'none' (this trumps
    // any 'list-style-type' or 'list-style-image' values).
    if (content.IsNone()) {
      return false;
    }
    // ::marker only exist if we have 'content' or at least one of
    // 'list-style-type' or 'list-style-image'.
    if (aPseudoStyle.StyleList()->mListStyleType.IsNone() &&
        aPseudoStyle.StyleList()->mListStyleImage.IsNone() &&
        content.IsNormal()) {
      return false;
    }
  }
  // For ::before, ::after, and ::checkmark pseudo-elements, no 'content'
  // items is equivalent to not having the pseudo-element at all.
  if (type == PseudoStyleType::Before || type == PseudoStyleType::After ||
      type == PseudoStyleType::Checkmark) {
    if (!aPseudoStyle.StyleContent()->mContent.IsItems()) {
      return false;
    }
    MOZ_ASSERT(!aPseudoStyle.StyleContent()->NonAltContentItems().IsEmpty(),
               "IsItems() implies we have at least one item");
  }
  if (type == PseudoStyleType::Before || type == PseudoStyleType::After ||
      type == PseudoStyleType::Marker || type == PseudoStyleType::Backdrop ||
      type == PseudoStyleType::Checkmark ||
      type == PseudoStyleType::PickerIcon) {
    // display:none is equivalent to not having a pseudo at all.
    if (aPseudoStyle.StyleDisplay()->mDisplay == StyleDisplay::None) {
      return false;
    }
  }
  return true;
}

bool ServoStyleSet::StyleDocument(ServoTraversalFlags aFlags) {
  AUTO_PROFILER_LABEL_CATEGORY_PAIR_RELEVANT_FOR_JS(LAYOUT_StyleComputation);
  MOZ_ASSERT(GetPresContext(), "Styling a document without a shell?");

  if (!mDocument->GetServoRestyleRoot()) {
    return false;
  }

  PreTraverse(aFlags);
  const SnapshotTable& snapshots = Snapshots();

  // Restyle the document from the root element and each of the document level
  // NAC subtree roots.
  bool postTraversalRequired = false;

  if (ShouldTraverseInParallel()) {
    aFlags |= ServoTraversalFlags::ParallelTraversal;
  }

  // Do the first traversal.
  DocumentStyleRootIterator iter(mDocument->GetServoRestyleRoot());
  while (Element* root = iter.GetNextStyleRoot()) {
    MOZ_ASSERT(MayTraverseFrom(root));

    Element* parent = root->GetFlattenedTreeParentElementForStyle();
    MOZ_ASSERT_IF(parent,
                  !parent->HasAnyOfFlags(Element::kAllServoDescendantBits));

    if (MOZ_UNLIKELY(!root->HasServoData()) && !parent) {
      StyleNewSubtree(root);
      postTraversalRequired = true;
      continue;
    }

    AutoPrepareTraversal guard(this);

    postTraversalRequired |=
        Servo_TraverseSubtree(root, mRawData.get(), &snapshots, aFlags) ||
        root->HasAnyOfFlags(Element::kAllServoDescendantBits |
                            NODE_NEEDS_FRAME);

    uint32_t existingBits = mDocument->GetServoRestyleRootDirtyBits();
    Element* newRoot = nullptr;
    while (parent && parent->HasDirtyDescendantsForServo()) {
      MOZ_ASSERT(root == mDocument->GetServoRestyleRoot(),
                 "Restyle root shouldn't have magically changed");
      // If any style invalidation was triggered in our siblings, then we may
      // need to post-traverse them, even if the root wasn't restyled after
      // all.
      // We need to propagate the existing bits to the ancestor.
      parent->SetFlags(existingBits);
      newRoot = parent;
      parent = parent->GetFlattenedTreeParentElementForStyle();
    }

    if (newRoot) {
      mDocument->SetServoRestyleRoot(
          newRoot, existingBits | ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
      postTraversalRequired = true;
    }
  }

  // If there are still animation restyles needed, trigger a second traversal to
  // update CSS animations or transitions' styles.
  //
  // Note that we need to check the style root again, because doing another
  // PreTraverse on the EffectCompositor might alter the style root. But we
  // don't need to worry about NAC, since document-level NAC shouldn't have
  // animations.
  //
  // We don't need to do this for SMIL since SMIL only updates its animation
  // values once at the begin of a tick. As a result, even if the previous
  // traversal caused, for example, the font-size to change, the SMIL style
  // won't be updated until the next tick anyway.
  if (GetPresContext()->EffectCompositor()->PreTraverse(aFlags)) {
    DocumentStyleRootIterator iter(mDocument->GetServoRestyleRoot());
    while (Element* root = iter.GetNextStyleRoot()) {
      AutoPrepareTraversal guard(this);
      postTraversalRequired |=
          Servo_TraverseSubtree(root, mRawData.get(), &snapshots, aFlags) ||
          root->HasAnyOfFlags(Element::kAllServoDescendantBits |
                              NODE_NEEDS_FRAME);
    }
  }

  return postTraversalRequired;
}

void ServoStyleSet::StyleNewSubtree(Element* aRoot) {
  MOZ_ASSERT(GetPresContext());
  MOZ_ASSERT(!aRoot->HasServoData());
  MOZ_ASSERT(aRoot->GetFlattenedTreeParentNodeForStyle(),
             "Not in the flat tree? Fishy!");
  PreTraverseSync();
  AutoPrepareTraversal guard(this);

  // Do the traversal. The snapshots will not be used.
  const SnapshotTable& snapshots = Snapshots();
  auto flags = ServoTraversalFlags::Empty;
  if (ShouldTraverseInParallel()) {
    flags |= ServoTraversalFlags::ParallelTraversal;
  }

  DebugOnly<bool> postTraversalRequired =
      Servo_TraverseSubtree(aRoot, mRawData.get(), &snapshots, flags);
  MOZ_ASSERT(!postTraversalRequired);

  // Annoyingly, the newly-styled content may have animations that need
  // starting, which requires traversing them again. Mark the elements
  // that need animation processing, then do a forgetful traversal to
  // update the styles and clear the animation bits.
  if (GetPresContext()->EffectCompositor()->PreTraverseInSubtree(flags,
                                                                 aRoot)) {
    postTraversalRequired =
        Servo_TraverseSubtree(aRoot, mRawData.get(), &snapshots,
                              ServoTraversalFlags::AnimationOnly |
                                  ServoTraversalFlags::FinalAnimationTraversal);
    MOZ_ASSERT(!postTraversalRequired);
  }
}

void ServoStyleSet::MarkOriginsDirty(OriginFlags aChangedOrigins) {
  SetStylistStyleSheetsDirty();
  Servo_StyleSet_NoteStyleSheetsChanged(mRawData.get(), aChangedOrigins);
}

void ServoStyleSet::SetStylistStyleSheetsDirty() {
  mStylistState |= StylistState::StyleSheetsDirty;

  // We need to invalidate cached style in getComputedStyle for undisplayed
  // elements, since we don't know if any of the style sheet change that we do
  // would affect undisplayed elements.
  //
  // We don't allow to call getComputedStyle in elements without a pres shell
  // yet, so it is fine if there's no pres context here.
  if (nsPresContext* presContext = GetPresContext()) {
    presContext->RestyleManager()->IncrementUndisplayedRestyleGeneration();
  }
}

void ServoStyleSet::SetStylistShadowDOMStyleSheetsDirty() {
  mStylistState |= StylistState::ShadowDOMStyleSheetsDirty;
  if (nsPresContext* presContext = GetPresContext()) {
    presContext->RestyleManager()->IncrementUndisplayedRestyleGeneration();
  }
}

static OriginFlags ToOriginFlags(StyleOrigin aOrigin) {
  switch (aOrigin) {
    case StyleOrigin::UserAgent:
      return OriginFlags::UserAgent;
    case StyleOrigin::User:
      return OriginFlags::User;
    default:
      MOZ_FALLTHROUGH_ASSERT("Unknown origin?");
    case StyleOrigin::Author:
      return OriginFlags::Author;
  }
}

void ServoStyleSet::ImportRuleLoaded(StyleSheet& aSheet) {
  if (mStyleRuleMap) {
    mStyleRuleMap->SheetAdded(aSheet);
  }

  // TODO: Should probably consider ancestor sheets too.
  if (!aSheet.IsApplicable()) {
    return;
  }

  // TODO(emilio): Could handle it better given we know it is an insertion, and
  // use the style invalidation machinery stuff that we do for regular sheet
  // insertions.
  MarkOriginsDirty(ToOriginFlags(aSheet.GetOrigin()));
}

void ServoStyleSet::RuleAdded(StyleSheet& aSheet, css::Rule& aRule) {
  if (mStyleRuleMap) {
    mStyleRuleMap->RuleAdded(aSheet, aRule);
  }

  if (!aSheet.IsApplicable() || aRule.IsIncompleteImportRule()) {
    return;
  }

  RuleChangedInternal(aSheet, aRule, StyleRuleChangeKind::Insertion);
}

void ServoStyleSet::RuleRemoved(StyleSheet& aSheet, css::Rule& aRule) {
  if (mStyleRuleMap) {
    mStyleRuleMap->RuleRemoved(aSheet, aRule);
  }

  if (!aSheet.IsApplicable()) {
    return;
  }

  RuleChangedInternal(aSheet, aRule, StyleRuleChangeKind::Removal);
}

static Maybe<StyleCssRuleRef> ToRuleRef(css::Rule& aRule) {
  switch (aRule.Type()) {
#define CASE_FOR(constant_, type_)                          \
  case StyleCssRuleType::constant_:                         \
    return Some(StyleCssRuleRef::constant_(                 \
        static_cast<dom::CSS##type_##Rule&>(aRule).Raw())); \
    break;
    CASE_FOR(CounterStyle, CounterStyle)
    CASE_FOR(Style, Style)
    CASE_FOR(Import, Import)
    CASE_FOR(Media, Media)
    CASE_FOR(Keyframes, Keyframes)
    CASE_FOR(Margin, Margin)
    CASE_FOR(CustomMedia, CustomMedia)
    CASE_FOR(FontFeatureValues, FontFeatureValues)
    CASE_FOR(FontPaletteValues, FontPaletteValues)
    CASE_FOR(FontFace, FontFace)
    CASE_FOR(Page, Page)
    CASE_FOR(Property, Property)
    CASE_FOR(Document, MozDocument)
    CASE_FOR(Supports, Supports)
    CASE_FOR(LayerBlock, LayerBlock)
    CASE_FOR(LayerStatement, LayerStatement)
    CASE_FOR(Container, Container)
    CASE_FOR(Scope, Scope)
    CASE_FOR(StartingStyle, StartingStyle)
    CASE_FOR(AppearanceBase, AppearanceBase)
    CASE_FOR(PositionTry, PositionTry)
    CASE_FOR(NestedDeclarations, NestedDeclarations)
    CASE_FOR(Namespace, Namespace)
    CASE_FOR(ViewTransition, ViewTransition)
#undef CASE_FOR
    case StyleCssRuleType::Keyframe:
      // No equivalent.
      break;
  }
  return Nothing{};
}

void ServoStyleSet::RuleChangedInternal(StyleSheet& aSheet, css::Rule& aRule,
                                        const StyleRuleChange& aChange) {
  MOZ_ASSERT(aSheet.IsApplicable());
  SetStylistStyleSheetsDirty();

  nsTArray<StyleCssRuleRef> ancestors;

  auto* parent = aRule.GetParentRule();
  while (parent) {
    if (const auto ref = ToRuleRef(*parent)) {
      ancestors.AppendElement(*ref);
    }
    parent = parent->GetParentRule();
  }
#define CASE_FOR(constant_, type_)                                        \
  case StyleCssRuleType::constant_:                                       \
    return Servo_StyleSet_##constant_##RuleChanged(                       \
        mRawData.get(), static_cast<dom::CSS##type_##Rule&>(aRule).Raw(), \
        &aSheet, aChange.mKind, &ancestors);
  switch (aRule.Type()) {
    CASE_FOR(CounterStyle, CounterStyle)
    CASE_FOR(Style, Style)
    CASE_FOR(Import, Import)
    CASE_FOR(CustomMedia, CustomMedia)
    CASE_FOR(Media, Media)
    CASE_FOR(Keyframes, Keyframes)
    CASE_FOR(Margin, Margin)
    CASE_FOR(FontFeatureValues, FontFeatureValues)
    CASE_FOR(FontPaletteValues, FontPaletteValues)
    CASE_FOR(FontFace, FontFace)
    CASE_FOR(Page, Page)
    CASE_FOR(Property, Property)
    CASE_FOR(Document, MozDocument)
    CASE_FOR(Supports, Supports)
    CASE_FOR(LayerBlock, LayerBlock)
    CASE_FOR(LayerStatement, LayerStatement)
    CASE_FOR(Container, Container)
    CASE_FOR(Scope, Scope)
    CASE_FOR(StartingStyle, StartingStyle)
    CASE_FOR(AppearanceBase, AppearanceBase)
    CASE_FOR(PositionTry, PositionTry)
    CASE_FOR(NestedDeclarations, NestedDeclarations)
    CASE_FOR(ViewTransition, ViewTransition)
    // @namespace can only be inserted / removed when there are only other
    // @namespace and @import rules, and can't be mutated.
    case StyleCssRuleType::Namespace:
      break;
    case StyleCssRuleType::Keyframe:
      // FIXME: We should probably just forward to the parent @keyframes rule? I
      // think that'd do the right thing, but meanwhile...
      return MarkOriginsDirty(ToOriginFlags(aSheet.GetOrigin()));
  }

#undef CASE_FOR
}

void ServoStyleSet::RuleChanged(StyleSheet& aSheet, css::Rule* aRule,
                                const StyleRuleChange& aChange) {
  if (!aSheet.IsApplicable()) {
    return;
  }

  if (!aRule) {
    MOZ_ASSERT(!aChange.mOldBlock);
    MOZ_ASSERT(!aChange.mNewBlock);
    // FIXME: This is done for StyleSheet.media attribute changes and such
    MarkOriginsDirty(ToOriginFlags(aSheet.GetOrigin()));
  } else {
    if (mStyleRuleMap && aChange.mOldBlock != aChange.mNewBlock) {
      mStyleRuleMap->RuleDeclarationsChanged(*aRule, aChange.mOldBlock,
                                             aChange.mNewBlock);
    }
    RuleChangedInternal(aSheet, *aRule, aChange);
  }
}

void ServoStyleSet::SheetCloned(StyleSheet& aSheet) {
  mNeedsRestyleAfterEnsureUniqueInner = true;
  if (mStyleRuleMap) {
    mStyleRuleMap->SheetCloned(aSheet);
  }
}

#ifdef DEBUG
void ServoStyleSet::AssertTreeIsClean() {
  DocumentStyleRootIterator iter(mDocument);
  while (Element* root = iter.GetNextStyleRoot()) {
    Servo_AssertTreeIsClean(root);
  }
}
#endif

bool ServoStyleSet::GetKeyframesForName(
    const Element& aElement, const ComputedStyle& aStyle, nsAtom* aName,
    const StyleComputedTimingFunction& aTimingFunction,
    nsTArray<Keyframe>& aKeyframes) {
  MOZ_ASSERT(!StylistNeedsUpdate());
  if (Servo_StyleSet_GetKeyframesForName(mRawData.get(), &aElement, &aStyle,
                                         aName, &aTimingFunction,
                                         &aKeyframes)) {
    return true;
  }
  if (StringBeginsWith(nsDependentAtomString(aName),
                       ViewTransition::kGroupAnimPrefix)) {
    if (auto* vt = mDocument->GetActiveViewTransition()) {
      if (vt->GetGroupKeyframes(aName, aTimingFunction, aKeyframes)) {
        return true;
      }
    }
  }
  return false;
}

nsTArray<ComputedKeyframeValues> ServoStyleSet::GetComputedKeyframeValuesFor(
    const nsTArray<Keyframe>& aKeyframes, Element* aElement,
    const PseudoStyleRequest& aPseudo, const ComputedStyle* aStyle) {
  nsTArray<ComputedKeyframeValues> result(aKeyframes.Length());

  // Construct each nsTArray<PropertyStyleAnimationValuePair> here.
  result.AppendElements(aKeyframes.Length());

  // FIXME: Bug 1922095. For view transition pseudo-element, we should just
  // use the pseudo-element here.
  Servo_GetComputedKeyframeValues(&aKeyframes, aElement, aPseudo.mType, aStyle,
                                  mRawData.get(), &result);
  return result;
}

void ServoStyleSet::GetAnimationValues(
    StyleLockedDeclarationBlock* aDeclarations, Element* aElement,
    const ComputedStyle* aComputedStyle,
    nsTArray<RefPtr<StyleAnimationValue>>& aAnimationValues) {
  // Servo_GetAnimationValues below won't handle ignoring existing element
  // data for bfcached documents. (See comment in ResolveStyleLazily
  // about these bfcache issues.)
  Servo_GetAnimationValues(aDeclarations, aElement, aComputedStyle,
                           mRawData.get(), &aAnimationValues);
}

already_AddRefed<ComputedStyle> ServoStyleSet::GetBaseContextForElement(
    Element* aElement, const ComputedStyle* aStyle) {
  return Servo_StyleSet_GetBaseComputedValuesForElement(
             mRawData.get(), aElement, aStyle, &Snapshots())
      .Consume();
}

already_AddRefed<StyleAnimationValue> ServoStyleSet::ComputeAnimationValue(
    Element* aElement, StyleLockedDeclarationBlock* aDeclarations,
    const ComputedStyle* aStyle) {
  return Servo_AnimationValue_Compute(aElement, aDeclarations, aStyle,
                                      mRawData.get())
      .Consume();
}

bool ServoStyleSet::UsesFontMetrics() const {
  return Servo_StyleSet_UsesFontMetrics(mRawData.get());
}

bool ServoStyleSet::UsesRootFontMetrics() const {
  return Servo_StyleSet_UsesRootFontMetrics(mRawData.get());
}

bool ServoStyleSet::EnsureUniqueInnerOnCSSSheets() {
  using SheetOwner = Variant<ServoStyleSet*, ShadowRoot*>;

  AutoTArray<std::pair<StyleSheet*, SheetOwner>, 32> queue;
  EnumerateStyleSheets([&](StyleSheet& aSheet) {
    queue.AppendElement(std::make_pair(&aSheet, SheetOwner{this}));
  });

  EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
    for (auto index : IntegerRange(aShadowRoot.SheetCount())) {
      queue.AppendElement(
          std::make_pair(aShadowRoot.SheetAt(index), SheetOwner{&aShadowRoot}));
    }
    for (const auto& adopted : aShadowRoot.AdoptedStyleSheets()) {
      queue.AppendElement(
          std::make_pair(adopted.get(), SheetOwner{&aShadowRoot}));
    }
  });

  while (!queue.IsEmpty()) {
    auto [sheet, owner] = queue.PopLastElement();

    if (sheet->HasForcedUniqueInner()) {
      // We already processed this sheet and its children.
      // Normally we don't hit this but adopted stylesheets can have dupes so we
      // can save some work here.
      continue;
    }

    // Only call EnsureUniqueInner for complete sheets. If we do call it on
    // incomplete sheets, we'll cause problems when the sheet is actually
    // loaded. We don't care about incomplete sheets here anyway, because this
    // method is only invoked by nsPresContext::EnsureSafeToHandOutCSSRules.
    // The CSSRule objects we are handing out won't contain any rules derived
    // from incomplete sheets (because they aren't yet applied in styling).
    if (sheet->IsComplete()) {
      sheet->EnsureUniqueInner();
    }

    // Enqueue all the sheet's children.
    for (StyleSheet* child : sheet->ChildSheets()) {
      queue.AppendElement(std::make_pair(child, owner));
    }
  }

  if (mNeedsRestyleAfterEnsureUniqueInner) {
    // TODO(emilio): We could make this faster if needed tracking the specific
    // origins and sheets that have been cloned. But the only caller of this
    // doesn't seem to really care about perf.
    MarkOriginsDirty(OriginFlags::All);
    ForceDirtyAllShadowStyles();
  }
  bool res = mNeedsRestyleAfterEnsureUniqueInner;
  mNeedsRestyleAfterEnsureUniqueInner = false;
  return res;
}

void ServoStyleSet::ClearCachedStyleData() {
  ClearNonInheritingComputedStyles();
  Servo_StyleSet_RebuildCachedData(mRawData.get());
  mCachedAnonymousContentStyles.Clear();
  PodArrayZero(mCachedAnonymousContentStyleIndexes);
}

void ServoStyleSet::ForceDirtyAllShadowStyles() {
  bool anyShadow = false;
  EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
    if (auto* authorStyles = aShadowRoot.GetServoStyles()) {
      anyShadow = true;
      Servo_AuthorStyles_ForceDirty(authorStyles);
    }
  });
  if (anyShadow) {
    SetStylistShadowDOMStyleSheetsDirty();
  }
}

void ServoStyleSet::CompatibilityModeChanged() {
  Servo_StyleSet_CompatModeChanged(mRawData.get());
  SetStylistStyleSheetsDirty();
  ForceDirtyAllShadowStyles();
}

void ServoStyleSet::ClearNonInheritingComputedStyles() {
  for (RefPtr<ComputedStyle>& ptr : mNonInheritingComputedStyles) {
    ptr = nullptr;
  }
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolveStyleLazily(
    const Element& aElement, const PseudoStyleRequest& aPseudoRequest,
    StyleRuleInclusion aRuleInclusion) {
  PreTraverseSync();
  MOZ_ASSERT(!StylistNeedsUpdate());

  AutoSetInServoTraversal guard(this);

  /**
   * NB: This is needed because we process animations and transitions on the
   * pseudo-elements themselves, not on the parent's EagerPseudoStyles.
   *
   * That means that that style doesn't account for animations, and we can't do
   * that easily from the traversal without doing wasted work.
   *
   * As such, we just lie here a bit, which is the entrypoint of
   * getComputedStyle, the only API where this can be observed, to look at the
   * style of the pseudo-element if it exists instead.
   */
  const Element* elementForStyleResolution = &aElement;
  PseudoStyleType pseudoTypeForStyleResolution = aPseudoRequest.mType;
  if (auto* pseudo = aElement.GetPseudoElement(aPseudoRequest)) {
    elementForStyleResolution = pseudo;
    pseudoTypeForStyleResolution = PseudoStyleType::NotPseudo;
  }

  nsPresContext* pc = GetPresContext();
  MOZ_ASSERT(pc, "For now, no style resolution without a pres context");
  auto* restyleManager = pc->RestyleManager();
  const bool canUseCache = aRuleInclusion == StyleRuleInclusion::All &&
                           aElement.OwnerDoc() == mDocument &&
                           pc->PresShell()->DidInitialize();
  return Servo_ResolveStyleLazily(
             elementForStyleResolution, pseudoTypeForStyleResolution,
             aPseudoRequest.mIdentifier.get(), aRuleInclusion,
             &restyleManager->Snapshots(),
             restyleManager->GetUndisplayedRestyleGeneration(), canUseCache,
             mRawData.get())
      .Consume();
}

void ServoStyleSet::AppendFontFaceRules(
    nsTArray<nsFontFaceRuleContainer>& aArray) {
  // TODO(emilio): Can we make this so this asserts instead?
  UpdateStylistIfNeeded();
  Servo_StyleSet_GetFontFaceRules(mRawData.get(), &aArray);
}

already_AddRefed<StyleViewTransitionRule>
ServoStyleSet::GetLastViewTransitionRule() {
  UpdateStylistIfNeeded();
  return Servo_StyleSet_GetLastViewTransitionRule(mRawData.get()).Consume();
}

const StyleLockedCounterStyleRule* ServoStyleSet::CounterStyleRuleForName(
    nsAtom* aName) {
  MOZ_ASSERT(!StylistNeedsUpdate());
  return Servo_StyleSet_GetCounterStyleRule(mRawData.get(), aName);
}

already_AddRefed<gfxFontFeatureValueSet>
ServoStyleSet::BuildFontFeatureValueSet() {
  MOZ_ASSERT(!StylistNeedsUpdate());
  RefPtr<gfxFontFeatureValueSet> set =
      Servo_StyleSet_BuildFontFeatureValueSet(mRawData.get());
  return set.forget();
}

already_AddRefed<gfx::FontPaletteValueSet>
ServoStyleSet::BuildFontPaletteValueSet() {
  MOZ_ASSERT(!StylistNeedsUpdate());
  RefPtr<gfx::FontPaletteValueSet> set =
      Servo_StyleSet_BuildFontPaletteValueSet(mRawData.get());
  return set.forget();
}

already_AddRefed<ComputedStyle> ServoStyleSet::ResolveForDeclarations(
    const ComputedStyle* aParentOrNull,
    const StyleLockedDeclarationBlock* aDeclarations) {
  // No need to update the stylist, we're only cascading aDeclarations.
  return Servo_StyleSet_ResolveForDeclarations(mRawData.get(), aParentOrNull,
                                               aDeclarations)
      .Consume();
}

void ServoStyleSet::UpdateStylist() {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Update stylesheet information", LAYOUT);
  MOZ_ASSERT(StylistNeedsUpdate());

  AutoTArray<StyleAuthorStyles*, 20> nonDocumentStyles;
  Element* root = mDocument->GetRootElement();
  const ServoElementSnapshotTable* snapshots = nullptr;
  if (nsPresContext* pc = GetPresContext()) {
    snapshots = &pc->RestyleManager()->Snapshots();
  }

  if (MOZ_UNLIKELY(mStylistState & StylistState::ShadowDOMStyleSheetsDirty)) {
    EnumerateShadowRoots(*mDocument, [&](ShadowRoot& aShadowRoot) {
      if (auto* authorStyles = aShadowRoot.GetServoStyles()) {
        nonDocumentStyles.AppendElement(authorStyles);
      }
    });
  }
  Servo_StyleSet_FlushStyleSheets(mRawData.get(), root, snapshots,
                                  &nonDocumentStyles);
  mStylistState = StylistState::NotDirty;
  mDocument->StylistUpdated();
}

void ServoStyleSet::MaybeGCRuleTree() {
  MOZ_ASSERT(NS_IsMainThread());
  Servo_MaybeGCRuleTree(mRawData.get());
}

/* static */
bool ServoStyleSet::MayTraverseFrom(const Element* aElement) {
  MOZ_ASSERT(aElement->IsInComposedDoc());
  nsINode* parent = aElement->GetFlattenedTreeParentNodeForStyle();
  if (!parent) {
    return false;
  }

  if (!parent->IsElement()) {
    MOZ_ASSERT(parent->IsDocument());
    return true;
  }

  if (!parent->AsElement()->HasServoData()) {
    return false;
  }

  return !Servo_Element_IsDisplayNone(parent->AsElement());
}

bool ServoStyleSet::ShouldTraverseInParallel() const {
  MOZ_ASSERT(mDocument->GetPresShell(), "Styling a document without a shell?");
  if (!mDocument->GetPresShell()->IsActive()) {
    return false;
  }
  if (profiler_feature_active(ProfilerFeature::SequentialStyle)) {
    return false;
  }
  return true;
}

void ServoStyleSet::RunPostTraversalTasks() {
  MOZ_ASSERT(!IsInServoTraversal());

  if (mPostTraversalTasks.IsEmpty()) {
    return;
  }

  nsTArray<PostTraversalTask> tasks = std::move(mPostTraversalTasks);

  for (auto& task : tasks) {
    task.Run();
  }
}

ServoStyleRuleMap* ServoStyleSet::StyleRuleMap() {
  if (!mStyleRuleMap) {
    mStyleRuleMap = std::make_unique<ServoStyleRuleMap>();
  }
  mStyleRuleMap->EnsureTable(*this);
  return mStyleRuleMap.get();
}

bool ServoStyleSet::MightHaveAttributeDependency(const Element& aElement,
                                                 nsAtom* aAttribute) const {
  return Servo_StyleSet_MightHaveAttributeDependency(mRawData.get(), &aElement,
                                                     aAttribute);
}

StyleContainerAttributeDependencyKind
ServoStyleSet::MightHaveAttributeDependencyInContainer(
    const Element& aElement, nsAtom* aAttribute) const {
  return Servo_StyleSet_MightHaveAttributeDependencyInContainer(
      mRawData.get(), &aElement, aAttribute);
}

bool ServoStyleSet::MightHaveNthOfIDDependency(const Element& aElement,
                                               nsAtom* aOldID,
                                               nsAtom* aNewID) const {
  return Servo_StyleSet_MightHaveNthOfIDDependency(mRawData.get(), &aElement,
                                                   aOldID, aNewID);
}

bool ServoStyleSet::MightHaveNthOfClassDependency(const Element& aElement) {
  return Servo_StyleSet_MightHaveNthOfClassDependency(mRawData.get(), &aElement,
                                                      &Snapshots());
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorIDDependency(
    const Element& aElement, nsAtom* aOldID, nsAtom* aNewID,
    const ServoElementSnapshotTable& aSnapshots) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorIDDependency(
      mRawData.get(), &aElement, aOldID, aNewID, &aSnapshots);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorClassDependency(
    const Element& aElement, const ServoElementSnapshotTable& aSnapshots) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorClassDependency(
      mRawData.get(), &aElement, &aSnapshots);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorCustomStateDependency(
    const Element& aElement, nsAtom* state,
    const ServoElementSnapshotTable& aSnapshots) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorCustomStateDependency(
      mRawData.get(), &aElement, state, &aSnapshots);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorAttributeDependency(
    const Element& aElement, nsAtom* aAttribute,
    const ServoElementSnapshotTable& aSnapshots) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorAttributeDependency(
      mRawData.get(), &aElement, aAttribute, &aSnapshots);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorStateDependency(
    const Element& aElement, ElementState aState,
    const ServoElementSnapshotTable& aSnapshots) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorStateDependency(
      mRawData.get(), &aElement, aState.GetInternalValue(), &aSnapshots);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorForEmptyDependency(
    const Element& aElement) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorEmptyDependency(mRawData.get(),
                                                                &aElement);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorForNthEdgeDependency(
    const Element& aElement,
    StyleRelativeSelectorNthEdgeInvalidateFor aInvalidateFor) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorNthEdgeDependency(
      mRawData.get(), &aElement, aInvalidateFor);
}

void ServoStyleSet::MaybeInvalidateRelativeSelectorForNthDependencyFromSibling(
    const Element* aFromSibling, bool aForceRestyleSiblings) {
  if (!aFromSibling) {
    return;
  }
  Servo_StyleSet_MaybeInvalidateRelativeSelectorNthDependencyFromSibling(
      mRawData.get(), aFromSibling, aForceRestyleSiblings);
}

void ServoStyleSet::MaybeInvalidateForElementInsertion(
    const Element& aElement) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorForInsertion(mRawData.get(),
                                                             &aElement);
}

void ServoStyleSet::MaybeInvalidateForElementAppend(
    const nsIContent& aFirstContent) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorForAppend(mRawData.get(),
                                                          &aFirstContent);
}

void ServoStyleSet::MaybeInvalidateForElementRemove(const Element& aElement) {
  Servo_StyleSet_MaybeInvalidateRelativeSelectorForRemoval(mRawData.get(),
                                                           &aElement);
}

bool ServoStyleSet::MightHaveNthOfAttributeDependency(
    const Element& aElement, nsAtom* aAttribute) const {
  return Servo_StyleSet_MightHaveNthOfAttributeDependency(
      mRawData.get(), &aElement, aAttribute);
}

bool ServoStyleSet::HasStateDependency(const Element& aElement,
                                       dom::ElementState aState) const {
  return Servo_StyleSet_HasStateDependency(mRawData.get(), &aElement,
                                           aState.GetInternalValue());
}

bool ServoStyleSet::HasNthOfStateDependency(const Element& aElement,
                                            dom::ElementState aState) const {
  return Servo_StyleSet_HasNthOfStateDependency(mRawData.get(), &aElement,
                                                aState.GetInternalValue());
}

bool ServoStyleSet::HasNthOfCustomStateDependency(const Element& aElement,
                                                  nsAtom* aState) const {
  return Servo_StyleSet_HasNthOfCustomStateDependency(mRawData.get(), &aElement,
                                                      aState);
}

void ServoStyleSet::RestyleSiblingsForNthOf(const Element& aElement,
                                            uint32_t aFlags) const {
  Servo_StyleSet_RestyleSiblingsForNthOf(&aElement, aFlags);
}

bool ServoStyleSet::HasDocumentStateDependency(
    dom::DocumentState aState) const {
  return Servo_StyleSet_HasDocumentStateDependency(mRawData.get(),
                                                   aState.GetInternalValue());
}

already_AddRefed<ComputedStyle> ServoStyleSet::ReparentComputedStyle(
    ComputedStyle* aComputedStyle, ComputedStyle* aNewParent,
    ComputedStyle* aNewLayoutParent, Element* aElement) {
  return Servo_ReparentStyle(aComputedStyle, aNewParent, aNewLayoutParent,
                             aElement, mRawData.get())
      .Consume();
}

void ServoStyleSet::InvalidateForViewportUnits(OnlyDynamic aOnlyDynamic) {
  dom::Element* root = mDocument->GetRootElement();
  if (!root) {
    return;
  }

  Servo_InvalidateForViewportUnits(mRawData.get(), root,
                                   aOnlyDynamic == OnlyDynamic::Yes);
}

void ServoStyleSet::RegisterProperty(const PropertyDefinition& aDefinition,
                                     ErrorResult& aRv) {
  using Result = StyleRegisterCustomPropertyResult;
  auto result = Servo_RegisterCustomProperty(
      RawData(), mDocument->DefaultStyleAttrURLData(), &aDefinition.mName,
      &aDefinition.mSyntax, aDefinition.mInherits,
      aDefinition.mInitialValue.WasPassed() ? &aDefinition.mInitialValue.Value()
                                            : nullptr);
  switch (result) {
    case Result::SuccessfullyRegistered:
      if (Element* root = mDocument->GetRootElement()) {
        if (nsPresContext* pc = GetPresContext()) {
          pc->RestyleManager()->PostRestyleEvent(
              root, RestyleHint::RecascadeSubtree(), nsChangeHint(0));
        }
      }
      mDocument->PostCustomPropertyRegistered(aDefinition);
      break;
    case Result::InvalidName:
      return aRv.ThrowSyntaxError("Invalid name");
    case Result::InvalidSyntax:
      return aRv.ThrowSyntaxError("Invalid syntax descriptor");
    case Result::InvalidInitialValue:
      return aRv.ThrowSyntaxError("Invalid initial value syntax");
    case Result::NoInitialValue:
      return aRv.ThrowSyntaxError(
          "Initial value is required when syntax is not universal");
    case Result::InitialValueNotComputationallyIndependent:
      return aRv.ThrowSyntaxError(
          "Initial value is required when syntax is not universal");
    case Result::AlreadyRegistered:
      return aRv.ThrowInvalidModificationError("Property already registered");
  }
}

NS_IMPL_ISUPPORTS(UACacheReporter, nsIMemoryReporter)

MOZ_DEFINE_MALLOC_SIZE_OF(ServoUACacheMallocSizeOf)
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(ServoUACacheMallocEnclosingSizeOf)

NS_IMETHODIMP
UACacheReporter::CollectReports(nsIHandleReportCallback* aHandleReport,
                                nsISupports* aData, bool aAnonymize) {
  ServoStyleSetSizes sizes;
  Servo_UACache_AddSizeOf(ServoUACacheMallocSizeOf,
                          ServoUACacheMallocEnclosingSizeOf, &sizes);

#define REPORT(_path, _amount, _desc)                                     \
  do {                                                                    \
    size_t __amount = _amount; /* evaluate _amount only once */           \
    if (__amount > 0) {                                                   \
      MOZ_COLLECT_REPORT(_path, KIND_HEAP, UNITS_BYTES, __amount, _desc); \
    }                                                                     \
  } while (0)

  // The UA cache does not contain the rule tree; that's in the StyleSet.
  MOZ_RELEASE_ASSERT(sizes.mRuleTree == 0);

  REPORT("explicit/layout/servo-ua-cache/precomputed-pseudos",
         sizes.mPrecomputedPseudos,
         "Memory used by precomputed pseudo-element declarations within the "
         "UA cache.");

  REPORT("explicit/layout/servo-ua-cache/element-and-pseudos-maps",
         sizes.mElementAndPseudosMaps,
         "Memory used by element and pseudos maps within the UA cache.");

  REPORT("explicit/layout/servo-ua-cache/invalidation-map",
         sizes.mInvalidationMap,
         "Memory used by invalidation maps within the UA cache.");

  REPORT("explicit/layout/servo-ua-cache/revalidation-selectors",
         sizes.mRevalidationSelectors,
         "Memory used by selectors for cache revalidation within the UA "
         "cache.");

  REPORT("explicit/layout/servo-ua-cache/other", sizes.mOther,
         "Memory used by other data within the UA cache");

  return NS_OK;
}

}  // namespace mozilla
