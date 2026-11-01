/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* FFI functions for Servo to call into Gecko */

#include "mozilla/GeckoBindings.h"

#include "AnchorPositioningUtils.h"
#include "ChildIterator.h"
#include "ErrorReporter.h"
#include "PseudoStyleType.h"
#include "gfxFontFeatures.h"
#include "gfxMathTable.h"
#include "gfxTextRun.h"
#include "imgLoader.h"
#include "mozilla/AttributeStyles.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EffectSet.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/Hal.h"
#include "mozilla/Keyframe.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Mutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoElementSnapshot.h"
#include "mozilla/ServoTraversalStatistics.h"
#include "mozilla/ShadowParts.h"
#include "mozilla/SizeOfState.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/TimelineManager.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/dom/CSSMozDocumentRule.h"
#include "mozilla/dom/CSSTransition.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/HTMLTableCellElement.h"
#include "mozilla/dom/MediaList.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/WorkerCommon.h"
#include "nsAnimationManager.h"
#include "nsAttrValueInlines.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSProps.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsDOMTokenList.h"
#include "nsDeviceContext.h"
#include "nsFontMetrics.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsILoadContext.h"
#include "nsINode.h"
#include "nsIURI.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsString.h"
#include "nsStyleStruct.h"
#include "nsStyleUtil.h"
#include "nsTArray.h"
#include "nsTransitionManager.h"
#include "nsWindowSizes.h"

#if defined(MOZ_MEMORY)
#  include "mozmemory.h"
#endif

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;

ServoTraversalStatistics* ServoTraversalStatistics::sSingleton = nullptr;

static StaticAutoPtr<Mutex> sServoFFILock;
/*
 * Does this child count as significant for selector matching?
 *
 * See nsStyleUtil::IsSignificantChild for details.
 */
bool Gecko_IsSignificantChild(const nsINode* aNode,
                              bool aWhitespaceIsSignificant) {
  return nsStyleUtil::ThreadSafeIsSignificantChild(aNode->AsContent(),
                                                   aWhitespaceIsSignificant);
}

const nsINode* Gecko_GetLastChild(const nsINode* aNode) {
  return aNode->GetLastChild();
}

const nsINode* Gecko_GetFlattenedTreeParentNode(const nsINode* aNode) {
  return aNode->GetFlattenedTreeParentNodeForStyle();
}

void Gecko_GetAnonymousContentForElement(const Element* aElement,
                                         nsTArray<nsIContent*>* aArray) {
  MOZ_ASSERT(aElement->MayHaveAnonymousChildren());
  nsLayoutUtils::AppendGeneratedContentPseudos(aElement, *aArray);
  nsContentUtils::AppendNativeAnonymousChildren(
      aElement, *aArray, nsIContent::eSkipDocumentLevelNativeAnonymousContent);
}

void Gecko_DestroyAnonymousContentList(nsTArray<nsIContent*>* aAnonContent) {
  MOZ_ASSERT(aAnonContent);
  delete aAnonContent;
}

RustSpan<const nsINode* const> Gecko_GetAssignedNodes(const Element* aElement) {
  MOZ_ASSERT(HTMLSlotElement::FromNode(aElement));
  Span<const RefPtr<nsINode>> span =
      static_cast<const HTMLSlotElement*>(aElement)->AssignedNodes();
  return {reinterpret_cast<const nsINode* const*>(span.Elements()),
          span.Length()};
}

void Gecko_GetQueryContainerSize(const Element* aElement, nscoord* aOutWidth,
                                 nscoord* aOutHeight) {
  MOZ_ASSERT(aElement);
  const nsIFrame* frame = aElement->GetPrimaryFrame();
  if (!frame) {
    return;
  }
  const auto containAxes = frame->GetContainSizeAxes();
  if (!containAxes.IsAny()) {
    return;
  }
  nsSize size = frame->GetContentRectRelativeToSelf().Size();
  bool isVertical = frame->GetWritingMode().IsVertical();
  if (isVertical ? containAxes.mBContained : containAxes.mIContained) {
    *aOutWidth = size.width;
  }
  if (isVertical ? containAxes.mIContained : containAxes.mBContained) {
    *aOutHeight = size.height;
  }
}

void Gecko_ComputedStyle_Init(ComputedStyle* aStyle,
                              const ServoComputedData* aValues) {
  new (KnownNotNull, aStyle) ComputedStyle(ServoComputedDataForgotten(aValues));
}

ServoComputedData::ServoComputedData(const ServoComputedDataForgotten aValue) {
  memcpy((void*)this, aValue.mPtr, sizeof(*this));
}

MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(ServoStyleStructsMallocEnclosingSizeOf)

void ServoComputedData::AddSizeOfExcludingThis(nsWindowSizes& aSizes) const {
  // Note: GetStyleFoo() returns a pointer to an nsStyleFoo that sits within a
  // servo_arc::Arc, i.e. it is preceded by a word-sized refcount. So we need
  // to measure it with a function that can handle an interior pointer. We use
  // ServoStyleStructsEnclosingMallocSizeOf to clearly identify in DMD's
  // output the memory measured here.
#define MEASURE_STRUCT(name_)                                     \
  static_assert(alignof(nsStyle##name_) <= sizeof(size_t),        \
                "alignment will break AddSizeOfExcludingThis()"); \
  const void* p##name_ = Style##name_();                          \
  if (!aSizes.mState.HaveSeenPtr(p##name_)) {                     \
    aSizes.mStyleSizes.NS_STYLE_SIZES_FIELD(name_) +=             \
        ServoStyleStructsMallocEnclosingSizeOf(p##name_);         \
  }
  FOR_EACH_STYLE_STRUCT(MEASURE_STRUCT, MEASURE_STRUCT)
#undef MEASURE_STRUCT

  if (visited_style && !aSizes.mState.HaveSeenPtr(visited_style)) {
    visited_style->AddSizeOfIncludingThis(aSizes,
                                          &aSizes.mLayoutComputedValuesVisited);
  }

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - custom_properties
  // - writing_mode
  // - rules
  // - font_computation_data
}

void Gecko_ComputedStyle_Destroy(ComputedStyle* aStyle) {
  aStyle->~ComputedStyle();
}

void Gecko_ConstructStyleChildrenIterator(const Element* aElement,
                                          StyleChildrenIterator* aIterator) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(aIterator);
  new (aIterator) StyleChildrenIterator(aElement);
}

void Gecko_DestroyStyleChildrenIterator(StyleChildrenIterator* aIterator) {
  MOZ_ASSERT(aIterator);

  aIterator->~StyleChildrenIterator();
}

const nsINode* Gecko_GetNextStyleChild(StyleChildrenIterator* aIterator) {
  MOZ_ASSERT(aIterator);
  return aIterator->GetNextChild();
}

bool Gecko_VisitedStylesEnabled(const Document* aDoc) {
  MOZ_ASSERT(aDoc);
  MOZ_ASSERT(NS_IsMainThread());

  if (!StaticPrefs::layout_css_visited_links_enabled()) {
    return false;
  }

  if (aDoc->IsBeingUsedAsImage()) {
    return false;
  }

  nsILoadContext* loadContext = aDoc->GetLoadContext();
  if (loadContext && loadContext->UsePrivateBrowsing()) {
    return false;
  }

  return true;
}

ElementState::InternalType Gecko_ElementState(const Element* aElement) {
  return aElement->StyleState().GetInternalValue();
}

bool Gecko_IsRootElement(const Element* aElement) {
  return aElement->OwnerDoc()->GetRootElement() == aElement;
}

void Gecko_GetCachedLazyPseudoStyles(const ComputedStyle* aStyle,
                                     nsTArray<const ComputedStyle*>* aArray) {
  MOZ_ASSERT(aStyle);
  aStyle->GetCachedLazyPseudoStyles(*aArray);
}

void Gecko_NoteDirtyElement(const Element* aElement) {
  MOZ_ASSERT(NS_IsMainThread());
  const_cast<Element*>(aElement)->NoteDirtyForServo();
}

void Gecko_NoteDirtySubtreeForInvalidation(const Element* aElement) {
  MOZ_ASSERT(NS_IsMainThread());
  const_cast<Element*>(aElement)->NoteDirtySubtreeForServo();
}

void Gecko_NoteAnimationOnlyDirtyElement(const Element* aElement) {
  MOZ_ASSERT(NS_IsMainThread());
  const_cast<Element*>(aElement)->NoteAnimationOnlyDirtyForServo();
}

bool Gecko_AnimationNameMayBeReferencedFromStyle(
    const nsPresContext* aPresContext, nsAtom* aName) {
  MOZ_ASSERT(aPresContext);
  return aPresContext->AnimationManager()->AnimationMayBeReferenced(aName);
}

void Gecko_InvalidatePositionTry(const Element* aElement) {
  auto* f = aElement->GetPrimaryFrame();
  if (!f || !f->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    return;
  }
  f->RemoveProperty(nsIFrame::LastSuccessfulPositionFallback());
  f->PresShell()->MarkPositionedFrameForReflow(f);
}

void Gecko_NoteHighlightPseudoStyleInvalidated(const Document* aDoc) {
  if (auto* presContext = aDoc->GetPresContext()) {
    presContext->RestyleManager()->NoteHighlightPseudoStyleInvalidated();
  }
}

float Gecko_GetScrollbarInlineSize(const nsPresContext* aPc) {
  MOZ_ASSERT(aPc);
  auto overlay = aPc->UseOverlayScrollbars() ? nsITheme::Overlay::Yes
                                             : nsITheme::Overlay::No;
  LayoutDeviceIntCoord size =
      aPc->Theme()->GetScrollbarSize(aPc, StyleScrollbarWidth::Auto, overlay);
  return aPc->DevPixelsToFloatCSSPixels(size);
}

PseudoStyleType Gecko_GetImplementedPseudoType(const Element* aElement) {
  return aElement->GetPseudoElementType();
}

nsAtom* Gecko_GetImplementedPseudoIdentifier(const Element* aElement) {
  if (!aElement->HasName()) {
    return nullptr;
  }

  PseudoStyleType type = aElement->GetPseudoElementType();
  if (!PseudoStyle::IsNamedViewTransitionPseudoElement(type) &&
      type != PseudoStyleType::Picker) {
    return nullptr;
  }

  return aElement->GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
}

uint32_t Gecko_CalcStyleDifference(const ComputedStyle* aOldStyle,
                                   const ComputedStyle* aNewStyle,
                                   bool* aAnyStyleStructChanged,
                                   bool* aOnlyResetStructsChanged) {
  MOZ_ASSERT(aOldStyle);
  MOZ_ASSERT(aNewStyle);

  uint32_t equalStructs;
  nsChangeHint result =
      aOldStyle->CalcStyleDifference(*aNewStyle, &equalStructs);

  *aAnyStyleStructChanged =
      equalStructs != StyleStructConstants::kAllStructsMask;

  const auto kInheritedStructsMask =
      StyleStructConstants::kInheritedStructsMask;
  *aOnlyResetStructsChanged =
      (equalStructs & kInheritedStructsMask) == kInheritedStructsMask;

  return result;
}

nscoord Gecko_CalcLineHeight(const StyleLineHeight* aLh,
                             const nsPresContext* aPc, bool aVertical,
                             const nsStyleFont* aAgainstFont,
                             const mozilla::dom::Element* aElement) {
  // Normal line-height depends on font metrics.
  MutexAutoLock guard(*sServoFFILock);
  return ReflowInput::CalcLineHeight(*aLh, *aAgainstFont,
                                     const_cast<nsPresContext*>(aPc), aVertical,
                                     aElement, 1.0f);
}

float Gecko_CalcAutoDecorationInset(float aFontSize) {
  // Use an inset factor of 1/12.5, so we get 2px of inset (resulting in 4px
  // gap between adjacent lines) at font-size 25px.
  constexpr float kAutoInsetFactor = 1.0 / 12.5;

  // Use the em size multiplied by kAutoInsetFactor, with a minimum of one
  // CSS pixel to ensure that at least some separation occurs.
  return std::max(1.0f, aFontSize * kAutoInsetFactor);
}

const ServoElementSnapshot* Gecko_GetElementSnapshot(
    const ServoElementSnapshotTable* aTable, const Element* aElement) {
  MOZ_ASSERT(aTable);
  MOZ_ASSERT(aElement);

  return aTable->Get(const_cast<Element*>(aElement));
}

bool Gecko_HaveSeenPtr(SeenPtrs* aTable, const void* aPtr) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTable);
  // Empty Rust allocations are indicated by small values up to the alignment
  // of the relevant type. We shouldn't see anything like that here.
  MOZ_ASSERT(uintptr_t(aPtr) > 16);

  return aTable->HaveSeenPtr(aPtr);
}

const StyleLockedDeclarationBlock* Gecko_GetStyleAttrDeclarationBlock(
    const Element* aElement) {
  return aElement->GetInlineStyleDeclaration();
}

const StyleLockedDeclarationBlock* Gecko_GetMappedAttributeDeclarations(
    const Element* aElement) {
  return aElement->GetMappedAttributeStyle();
}

const StyleLockedDeclarationBlock* Gecko_GetViewTransitionDynamicRule(
    const Element* aElement) {
  const auto* vt = aElement->OwnerDoc()->GetActiveViewTransition();
  if (!vt) {
    return nullptr;
  }
  return vt->GetDynamicRuleFor(*aElement);
}

void Gecko_GetSubmitString(const Document* aDocument, nsAString* aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                          "Submit", aDocument, *aResult);
}

const StyleLockedDeclarationBlock* Gecko_GetExtraContentStyleDeclarations(
    const Element* aElement) {
  if (const auto* cell = HTMLTableCellElement::FromNode(aElement)) {
    return cell->GetMappedAttributesInheritedFromTable();
  }
  if (const auto* img = HTMLImageElement::FromNode(aElement)) {
    return img->GetMappedAttributesFromSource();
  }
  return nullptr;
}

const StyleLockedDeclarationBlock* Gecko_GetUnvisitedLinkAttrDeclarationBlock(
    const Element* aElement) {
  AttributeStyles* attrStyles = aElement->OwnerDoc()->GetAttributeStyles();
  if (!attrStyles) {
    return nullptr;
  }

  return attrStyles->GetServoUnvisitedLinkDecl();
}

StyleSheet* Gecko_StyleSheet_Clone(const StyleSheet* aSheet) {
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->GetParentSheet(), "Should only be used for @import");
  // NOTE(emilio): We don't pass either the parent pointer of the stylesheet,
  // nor fix up the child list (yet). This is fixed up in the StylesheetInner
  // constructor.
  RefPtr<StyleSheet> newSheet = aSheet->Clone(nullptr, nullptr);
  return static_cast<StyleSheet*>(newSheet.forget().take());
}

void Gecko_StyleSheet_AddRef(const StyleSheet* aSheet) {
  MOZ_ASSERT(NS_IsMainThread());
  const_cast<StyleSheet*>(aSheet)->AddRef();
}

void Gecko_StyleSheet_Release(const StyleSheet* aSheet) {
  MOZ_ASSERT(NS_IsMainThread());
  const_cast<StyleSheet*>(aSheet)->Release();
}

GeckoImplicitScopeRoot Gecko_StyleSheet_ImplicitScopeRoot(
    const mozilla::StyleSheet* aSheet) {
  if (aSheet->IsConstructed()) {
    return GeckoImplicitScopeRoot{
        .mHost = nullptr, .mRoot = nullptr, .mConstructed = true};
  }
  // https://drafts.csswg.org/css-cascade-6/#scope-limits
  // "If no <scope-start> is specified, the scoping root is the parent element
  // of the owner node of the stylesheet where the @scope rule is defined."
  const auto* node = aSheet->GetOwnerNodeOfOutermostSheet();
  if (!node) {
    return GeckoImplicitScopeRoot{
        .mHost = nullptr, .mRoot = nullptr, .mConstructed = false};
  }
  const auto* host = node->GetContainingShadowHost();

  if (auto* aElement = node->GetParentElement()) {
    return GeckoImplicitScopeRoot{
        .mHost = host, .mRoot = aElement, .mConstructed = false};
  }
  // "[...] If no such element exists, then the scoping root is the root of the
  // containing node tree." This really should only happen for stylesheets
  // defined at the edge of the shadow root.
  return GeckoImplicitScopeRoot{
      .mHost = host, .mRoot = host, .mConstructed = false};
}

const StyleLockedDeclarationBlock* Gecko_GetVisitedLinkAttrDeclarationBlock(
    const Element* aElement) {
  AttributeStyles* attrStyles = aElement->OwnerDoc()->GetAttributeStyles();
  if (!attrStyles) {
    return nullptr;
  }
  return attrStyles->GetServoVisitedLinkDecl();
}

const StyleLockedDeclarationBlock* Gecko_GetActiveLinkAttrDeclarationBlock(
    const Element* aElement) {
  AttributeStyles* attrStyles = aElement->OwnerDoc()->GetAttributeStyles();
  if (!attrStyles) {
    return nullptr;
  }
  return attrStyles->GetServoActiveLinkDecl();
}

bool Gecko_GetAnimationRule(const Element* aElement,
                            EffectCompositor::CascadeLevel aCascadeLevel,
                            StyleAnimationValueMap* aAnimationValues) {
  MOZ_ASSERT(aElement);

  Document* doc = aElement->GetComposedDoc();
  if (!doc) {
    return false;
  }
  nsPresContext* presContext = doc->GetPresContext();
  if (!presContext) {
    return false;
  }

  const auto [element, pseudoRequest] =
      AnimationUtils::GetElementPseudoPair(aElement);
  return presContext->EffectCompositor()->GetServoAnimationRule(
      element, pseudoRequest, aCascadeLevel, aAnimationValues);
}

bool Gecko_StyleAnimationsEquals(const nsStyleAutoArray<StyleAnimation>* aA,
                                 const nsStyleAutoArray<StyleAnimation>* aB) {
  return *aA == *aB;
}

bool Gecko_StyleScrollTimelinesEquals(
    const nsStyleAutoArray<StyleScrollTimeline>* aA,
    const nsStyleAutoArray<StyleScrollTimeline>* aB) {
  return *aA == *aB;
}

bool Gecko_StyleViewTimelinesEquals(
    const nsStyleAutoArray<StyleViewTimeline>* aA,
    const nsStyleAutoArray<StyleViewTimeline>* aB) {
  return *aA == *aB;
}

void Gecko_UpdateAnimations(const Element* aElement,
                            const ComputedStyle* aOldComputedData,
                            const ComputedStyle* aComputedData,
                            UpdateAnimationsTasks aTasks) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aElement);

  if (!aElement->IsInComposedDoc()) {
    return;
  }

  nsPresContext* presContext = nsContentUtils::GetContextForContent(aElement);
  if (!presContext || !presContext->IsDynamic()) {
    return;
  }

  nsAutoAnimationMutationBatch mb(aElement->OwnerDoc());

  const auto [element, pseudoRequest] =
      AnimationUtils::GetElementPseudoPair(aElement);

  // Handle timeline scopes first, because our own scroll/view-linked animations
  // may be affected.
  if (aTasks & UpdateAnimationsTasks::TimelineScopes) {
    presContext->TimelineManager()->UpdateTimelineScopes(
        const_cast<Element*>(element), aComputedData);
    // We could try to limit the impact here, at least for changes involving not
    // `all`. However, defer any such optimization until after bug 2024012.
    presContext->AnimationManager()->UpdateAllNamedTimelineAnimations();
  }

  // Handle scroll/view timelines first because CSS animations may refer to the
  // timeline defined by itself.
  if (aTasks & UpdateAnimationsTasks::ScrollTimelines) {
    const auto affected = presContext->TimelineManager()->UpdateTimelines(
        const_cast<Element*>(element), pseudoRequest, aComputedData,
        TimelineManager::ProgressTimelineType::Scroll);
    presContext->AnimationManager()->UpdateNamedTimelineAnimations(affected);
  }

  if (aTasks & UpdateAnimationsTasks::ViewTimelines) {
    const auto affected = presContext->TimelineManager()->UpdateTimelines(
        const_cast<Element*>(element), pseudoRequest, aComputedData,
        TimelineManager::ProgressTimelineType::View);
    presContext->AnimationManager()->UpdateNamedTimelineAnimations(affected);
  }

  if (aTasks & UpdateAnimationsTasks::CSSAnimations) {
    presContext->AnimationManager()->UpdateAnimations(
        const_cast<Element*>(element), pseudoRequest, aComputedData);
  }

  // aComputedData might be nullptr if the target element is now in a
  // display:none subtree. We still call Gecko_UpdateAnimations in this case
  // because we need to stop CSS animations in the display:none subtree.
  // However, we don't need to update transitions since they are stopped by
  // RestyleManager::AnimationsWithDestroyedFrame so we just return early
  // here.
  if (!aComputedData) {
    return;
  }

  if (aTasks & UpdateAnimationsTasks::CSSTransitions) {
    MOZ_ASSERT(aOldComputedData);
    presContext->TransitionManager()->UpdateTransitions(
        const_cast<Element*>(element), pseudoRequest, *aOldComputedData,
        *aComputedData);
  }

  if (aTasks & UpdateAnimationsTasks::EffectProperties) {
    presContext->EffectCompositor()->UpdateEffectProperties(
        aComputedData, const_cast<Element*>(element), pseudoRequest);
  }

  if (aTasks & UpdateAnimationsTasks::CascadeResults) {
    EffectSet* effectSet = EffectSet::Get(element, pseudoRequest);
    // CSS animations/transitions might have been destroyed as part of the above
    // steps so before updating cascade results, we check if there are still any
    // animations to update.
    if (effectSet) {
      // We call UpdateCascadeResults directly (intead of
      // MaybeUpdateCascadeResults) since we know for sure that the cascade has
      // changed, but we were unable to call MarkCascadeUpdated when we noticed
      // it since we avoid mutating state as part of the Servo parallel
      // traversal.
      presContext->EffectCompositor()->UpdateCascadeResults(
          *effectSet, const_cast<Element*>(element), pseudoRequest);
    }
  }

  if (aTasks & UpdateAnimationsTasks::DisplayChangedFromNone) {
    presContext->EffectCompositor()->RequestRestyle(
        const_cast<Element*>(element), pseudoRequest,
        EffectCompositor::RestyleType::Standard,
        EffectCompositor::CascadeLevel::Animations);
  }
}

size_t Gecko_GetAnimationEffectCount(const Element* aElementOrPseudo) {
  const auto [element, pseudo] =
      AnimationUtils::GetElementPseudoPair(aElementOrPseudo);

  EffectSet* effectSet = EffectSet::Get(element, pseudo);
  return effectSet ? effectSet->Count() : 0;
}

bool Gecko_ElementHasAnimations(const Element* aElement) {
  const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(aElement);
  return !!EffectSet::Get(element, pseudo);
}

bool Gecko_ElementHasCSSAnimations(const Element* aElement) {
  const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(aElement);
  auto* collection =
      nsAnimationManager::CSSAnimationCollection::Get(element, pseudo);
  return collection && !collection->mAnimations.IsEmpty();
}

bool Gecko_ElementHasCSSTransitions(const Element* aElement) {
  const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(aElement);
  auto* collection =
      nsTransitionManager::CSSTransitionCollection::Get(element, pseudo);
  return collection && !collection->mAnimations.IsEmpty();
}

size_t Gecko_ElementTransitions_Length(const Element* aElement) {
  const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(aElement);
  auto* collection =
      nsTransitionManager::CSSTransitionCollection::Get(element, pseudo);
  return collection ? collection->mAnimations.Length() : 0;
}

static CSSTransition* GetCurrentTransitionAt(const Element* aElement,
                                             size_t aIndex) {
  const auto [element, pseudo] = AnimationUtils::GetElementPseudoPair(aElement);
  auto* collection =
      nsTransitionManager::CSSTransitionCollection ::Get(element, pseudo);
  if (!collection) {
    return nullptr;
  }
  return collection->mAnimations.SafeElementAt(aIndex);
}

NonCustomCSSPropertyId Gecko_ElementTransitions_PropertyAt(
    const Element* aElement, size_t aIndex) {
  CSSTransition* transition = GetCurrentTransitionAt(aElement, aIndex);
  return transition ? transition->TransitionProperty().mId
                    : NonCustomCSSPropertyId::eCSSProperty_UNKNOWN;
}

const StyleAnimationValue* Gecko_ElementTransitions_EndValueAt(
    const Element* aElement, size_t aIndex) {
  CSSTransition* transition = GetCurrentTransitionAt(aElement, aIndex);
  return transition ? transition->ToValue().mServo.get() : nullptr;
}

double Gecko_GetProgressFromComputedTiming(const ComputedTiming* aTiming) {
  return aTiming->mProgress.Value();
}

double Gecko_GetPositionInSegment(const AnimationPropertySegment* aSegment,
                                  double aProgress, bool aBeforeFlag) {
  MOZ_ASSERT(aSegment->mFromKey < aSegment->mToKey,
             "The segment from key should be less than to key");

  double positionInSegment = (aProgress - aSegment->mFromKey) /
                             // To avoid floating precision inaccuracies, make
                             // sure we calculate both the numerator and
                             // denominator using double precision.
                             (double(aSegment->mToKey) - aSegment->mFromKey);

  return StyleComputedTimingFunction::GetPortion(
      aSegment->mTimingFunction, positionInSegment, aBeforeFlag);
}

const StyleAnimationValue* Gecko_AnimationGetBaseStyle(
    const RawServoAnimationValueTable* aBaseStyles,
    const mozilla::CSSPropertyId* aProperty) {
  const auto* base = reinterpret_cast<const nsRefPtrHashtable<
      nsGenericHashKey<CSSPropertyId>, StyleAnimationValue>*>(aBaseStyles);
  return base->GetWeak(*aProperty);
}

void Gecko_FillAllImageLayers(nsStyleImageLayers* aLayers, uint32_t aMaxLen) {
  aLayers->FillAllLayers(aMaxLen);
}

bool Gecko_IsDocumentBody(const Element* aElement) {
  Document* doc = aElement->GetUncomposedDoc();
  return doc && doc->GetBodyElement() == aElement;
}

bool Gecko_IsDarkColorScheme(const Document* aDoc,
                             const StyleColorSchemeFlags* aStyle) {
  return LookAndFeel::ColorSchemeForStyle(*aDoc, *aStyle) == ColorScheme::Dark;
}

nscolor Gecko_ComputeSystemColor(StyleSystemColor aColor, const Document* aDoc,
                                 const StyleColorSchemeFlags* aStyle) {
  auto colorScheme = LookAndFeel::ColorSchemeForStyle(*aDoc, *aStyle);
  const auto& prefs = PreferenceSheet::PrefsFor(*aDoc);
  if (prefs.mMustUseLightSystemColors) {
    colorScheme = ColorScheme::Light;
  }
  const auto& colors = prefs.ColorsFor(colorScheme);
  switch (aColor) {
    case StyleSystemColor::Canvastext:
      return colors.mDefault;
    case StyleSystemColor::Canvas:
      return colors.mDefaultBackground;
    case StyleSystemColor::Linktext:
      return colors.mLink;
    case StyleSystemColor::Activetext:
      return colors.mActiveLink;
    case StyleSystemColor::Visitedtext:
      return colors.mVisitedLink;
    default:
      break;
  }

  auto useStandins = LookAndFeel::ShouldUseStandins(*aDoc, aColor);
  return LookAndFeel::Color(aColor, colorScheme, useStandins);
}

int32_t Gecko_GetLookAndFeelInt(int32_t aId) {
  auto intId = static_cast<LookAndFeel::IntID>(aId);
  return LookAndFeel::GetInt(intId);
}

float Gecko_GetLookAndFeelFloat(int32_t aId) {
  auto id = static_cast<LookAndFeel::FloatID>(aId);
  return LookAndFeel::GetFloat(id);
}

bool Gecko_MatchLang(const Element* aElement, nsAtom* aOverrideLang,
                     bool aHasOverrideLang, const char16_t* aValue) {
  MOZ_ASSERT(!(aOverrideLang && !aHasOverrideLang),
             "aHasOverrideLang should only be set when aOverrideLang is null");
  MOZ_ASSERT(aValue, "null lang parameter");
  if (!aValue || !*aValue) {
    return false;
  }

  // We have to determine the language of the current element.  Since
  // this is currently no property and since the language is inherited
  // from the parent we have to be prepared to look at all parent
  // nodes.  The language itself is encoded in the LANG attribute.
  if (auto* language = aHasOverrideLang ? aOverrideLang : aElement->GetLang()) {
    return nsStyleUtil::LangTagCompare(nsAtomCString(language),
                                       NS_ConvertUTF16toUTF8(aValue));
  }

  // Try to get the language from the HTTP header or if this
  // is missing as well from the preferences.
  // The content language can be a comma-separated list of
  // language codes.
  // FIXME: We're not really consistent in our treatment of comma-separated
  // content-language values.
  if (nsAtom* language = aElement->OwnerDoc()->GetContentLanguage()) {
    const NS_ConvertUTF16toUTF8 langString(aValue);
    nsAtomCString docLang(language);
    docLang.StripWhitespace();
    for (auto const& lang : docLang.Split(',')) {
      if (nsStyleUtil::LangTagCompare(lang, langString)) {
        return true;
      }
    }
  }
  return false;
}

bool Gecko_MatchViewTransitionClass(
    const mozilla::dom::Element* aElement,
    const nsTArray<StyleAtom>* aPtNameAndClassSelector) {
  MOZ_ASSERT(aElement && aPtNameAndClassSelector);

  const Document* doc = aElement->OwnerDoc();
  MOZ_ASSERT(doc);
  const ViewTransition* vt = doc->GetActiveViewTransition();
  if (!vt) {
    // We can get here while lazily resolving the style of a named view
    // transition pseudo-element that doesn't exist.
    return false;
  }
  nsAtom* name = Gecko_GetImplementedPseudoIdentifier(aElement);
  if (!name) {
    return false;
  }
  return vt->MatchClassList(name, *aPtNameAndClassSelector);
}

static bool IsValidViewTransitionType(nsAtom* aName) {
  nsDependentAtomString str(aName);
  return !StringBeginsWith(str, u"-ua-"_ns,
                           nsASCIICaseInsensitiveStringComparator) &&
         !str.LowerCaseEqualsASCII("none");
}

bool Gecko_HasActiveViewTransitionTypes(
    const mozilla::dom::Document* aDoc,
    const nsTArray<StyleCustomIdent>* aNames) {
  MOZ_ASSERT(aDoc);
  MOZ_ASSERT(aNames);
  const ViewTransition* vt = aDoc->GetActiveViewTransition();
  if (!vt) {
    return false;
  }
  const auto& typeList = vt->GetTypeList();
  if (typeList.IsEmpty()) {
    return false;
  }
  for (const auto& name : *aNames) {
    if (typeList.Contains(name.AsAtom())) {
      // NOTE(emilio): This IsValidViewTransitionType() check is not in the spec
      // and is rather weird, but matches other browsers for now, see:
      // https://github.com/w3c/csswg-drafts/issues/13141
      if (IsValidViewTransitionType(name.AsAtom())) {
        return true;
      }
    }
  }
  return false;
}

const PreferenceSheet::Prefs* Gecko_GetPrefSheetPrefs(const Document* aDoc) {
  return &PreferenceSheet::PrefsFor(*aDoc);
}

bool Gecko_IsTableBorderNonzero(const Element* aElement) {
  if (!aElement->IsHTMLElement(nsGkAtoms::table)) {
    return false;
  }
  const nsAttrValue* val = aElement->GetParsedAttr(nsGkAtoms::border);
  return val &&
         (val->Type() != nsAttrValue::eInteger || val->GetIntegerValue() != 0);
}

bool Gecko_IsSelectListBox(const Element* aElement) {
  const auto* select = HTMLSelectElement::FromNode(aElement);
  return select && !select->IsCombobox();
}

bool Gecko_LookupAttrValue(const Element* aElement, nsAtom& aNamespace,
                           nsAtom& aName, nsAString& aResult) {
  int32_t attrNameSpace = kNameSpaceID_None;
  if (!aNamespace.IsEmpty()) {
    attrNameSpace = nsNameSpaceManager::GetInstance()->GetNameSpaceID(
        &aNamespace, nsContentUtils::IsChromeDoc(aElement->OwnerDoc()));
  }
  // All attribute names on HTML elements in HTML docs match
  // ASCII-case-insensitively. See note in:
  // https://html.spec.whatwg.org/multipage/dom.html#custom-data-attribute
  if (!aName.IsAsciiLowercase() && aElement->OwnerDoc()->IsHTMLDocument() &&
      aElement->IsHTMLElement()) {
    RefPtr<nsAtom> lowercaseName(&aName);
    ToLowerCaseASCII(lowercaseName);
    return aElement->GetAttr(attrNameSpace, lowercaseName, aResult);
  }
  return aElement->GetAttr(attrNameSpace, &aName, aResult);
}

template <typename Implementor>
static nsAtom* LangValue(Implementor* aElement) {
  // TODO(emilio): Deduplicate a bit with nsIContent::GetLang().
  const nsAttrValue* attr =
      aElement->GetParsedAttr(nsGkAtoms::lang, kNameSpaceID_XML);
  if (!attr && aElement->SupportsLangAttr()) {
    attr = aElement->GetParsedAttr(nsGkAtoms::lang);
  }

  if (!attr) {
    return nullptr;
  }

  MOZ_ASSERT(attr->Type() == nsAttrValue::eAtom);
  RefPtr<nsAtom> atom = attr->GetAtomValue();
  return atom.forget().take();
}

bool Gecko_AttrEquals(const nsAttrValue* aValue, const nsAtom* aStr,
                      bool aIgnoreCase) {
  return aValue->Equals(aStr, aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

bool Gecko_AttrDashEquals(const nsAttrValue* aValue, const nsAtom* aStr,
                          bool aIgnoreCase) {
  nsAutoString str;
  aValue->ToString(str);
  return nsStyleUtil::DashMatchCompare(
      str, nsDependentAtomString(aStr),
      aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

bool Gecko_AttrIncludes(const nsAttrValue* aValue, const nsAtom* aStr,
                        bool aIgnoreCase) {
  if (aStr == nsGkAtoms::_empty) {
    return false;
  }
  nsAutoString str;
  aValue->ToString(str);
  return nsStyleUtil::ValueIncludes(str, nsDependentAtomString(aStr),
                                    aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

bool Gecko_AttrHasSubstring(const nsAttrValue* aValue, const nsAtom* aStr,
                            bool aIgnoreCase) {
  return aStr != nsGkAtoms::_empty &&
         aValue->HasSubstring(nsDependentAtomString(aStr),
                              aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

bool Gecko_AttrHasPrefix(const nsAttrValue* aValue, const nsAtom* aStr,
                         bool aIgnoreCase) {
  return aStr != nsGkAtoms::_empty &&
         aValue->HasPrefix(nsDependentAtomString(aStr),
                           aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

bool Gecko_AttrHasSuffix(const nsAttrValue* aValue, const nsAtom* aStr,
                         bool aIgnoreCase) {
  return aStr != nsGkAtoms::_empty &&
         aValue->HasSuffix(nsDependentAtomString(aStr),
                           aIgnoreCase ? eIgnoreCase : eCaseMatters);
}

#define SERVO_IMPL_ELEMENT_ATTR_MATCHING_FUNCTIONS(prefix_, implementor_) \
  nsAtom* prefix_##LangValue(implementor_ aElement) {                     \
    return LangValue(aElement);                                           \
  }

SERVO_IMPL_ELEMENT_ATTR_MATCHING_FUNCTIONS(Gecko_, const Element*)
SERVO_IMPL_ELEMENT_ATTR_MATCHING_FUNCTIONS(Gecko_Snapshot,
                                           const ServoElementSnapshot*)

#undef SERVO_IMPL_ELEMENT_ATTR_MATCHING_FUNCTIONS

nsAtom* Gecko_Atomize(const char* aString, uint32_t aLength) {
  nsDependentCSubstring str(aString, aLength);
  if (NS_IsMainThread()) {
    return NS_AtomizeMainThread(str).take();
  }
  return NS_Atomize(str).take();
}

nsAtom* Gecko_Atomize16(const nsAString* aString) {
  return NS_Atomize(*aString).take();
}

void Gecko_AddRefAtom(nsAtom* aAtom) { NS_ADDREF(aAtom); }

void Gecko_ReleaseAtom(nsAtom* aAtom) { NS_RELEASE(aAtom); }

void Gecko_nsFont_InitSystem(nsFont* aDest, StyleSystemFont aFontId,
                             const nsStyleFont* aFont,
                             const Document* aDocument) {
  const nsFont& defaultVariableFont =
      aDocument->GetFontPrefsForLang(aFont->GetLangAtom())
          ->mDefaultVariableFont;

  // We have passed uninitialized memory to this function,
  // initialize it. We can't simply return an nsFont because then
  // we need to know its size beforehand. Servo cannot initialize nsFont
  // itself, so this will do.
  new (aDest) nsFont(defaultVariableFont);

  nsLayoutUtils::ComputeSystemFont(aDest, aFontId, defaultVariableFont,
                                   aDocument);
}

void Gecko_nsFont_Destroy(nsFont* aDest) { aDest->~nsFont(); }

StyleGenericFontFamily Gecko_nsStyleFont_ComputeFallbackFontTypeForLanguage(
    const Document* aDoc, nsAtom* aLanguage) {
  return aDoc->GetFontPrefsForLang(aLanguage)->GetDefaultGeneric();
}

Length Gecko_GetBaseSize(const Document* aDoc, nsAtom* aLang,
                         StyleGenericFontFamily aGeneric) {
  return aDoc->GetFontPrefsForLang(aLang)->GetDefaultFont(aGeneric)->size;
}

gfxFontFeatureValueSet* Gecko_ConstructFontFeatureValueSet() {
  return new gfxFontFeatureValueSet();
}

nsTArray<uint32_t>* Gecko_AppendFeatureValueHashEntry(
    gfxFontFeatureValueSet* aFontFeatureValues, nsAtom* aFamily,
    uint32_t aAlternate, nsAtom* aName) {
  MOZ_ASSERT(NS_IsMainThread());
  return aFontFeatureValues->AppendFeatureValueHashEntry(nsAtomCString(aFamily),
                                                         aName, aAlternate);
}

gfx::FontPaletteValueSet* Gecko_ConstructFontPaletteValueSet() {
  return new gfx::FontPaletteValueSet();
}

gfx::FontPaletteValueSet::PaletteValues* Gecko_AppendPaletteValueHashEntry(
    gfx::FontPaletteValueSet* aPaletteValueSet, nsAtom* aFamily,
    nsAtom* aName) {
  MOZ_ASSERT(NS_IsMainThread());
  return aPaletteValueSet->Insert(aName, nsAtomCString(aFamily));
}

void Gecko_SetFontPaletteBase(gfx::FontPaletteValueSet::PaletteValues* aValues,
                              int32_t aBasePaletteIndex) {
  aValues->mBasePalette = aBasePaletteIndex;
}

void Gecko_SetFontPaletteOverride(
    gfx::FontPaletteValueSet::PaletteValues* aValues, int32_t aIndex,
    StyleAbsoluteColor* aColor) {
  if (aIndex < 0) {
    return;
  }
  aValues->mOverrides.AppendElement(gfx::FontPaletteValueSet::OverrideColor{
      uint32_t(aIndex), aColor->ToColor()});
}

void Gecko_EnsureImageLayersLength(nsStyleImageLayers* aLayers, size_t aLen,
                                   nsStyleImageLayers::LayerType aLayerType) {
  size_t oldLength = aLayers->mLayers.Length();

  aLayers->mLayers.EnsureLengthAtLeast(aLen);

  for (size_t i = oldLength; i < aLen; ++i) {
    aLayers->mLayers[i].Initialize(aLayerType);
  }
}

template <typename StyleType>
static void EnsureStyleAutoArrayLength(StyleType* aArray, size_t aLen) {
  aArray->EnsureLengthAtLeast(aLen);
}

void Gecko_EnsureStyleAnimationArrayLength(void* aArray, size_t aLen) {
  auto* base = static_cast<nsStyleAutoArray<StyleAnimation>*>(aArray);
  EnsureStyleAutoArrayLength(base, aLen);
}

void Gecko_EnsureStyleTransitionArrayLength(void* aArray, size_t aLen) {
  auto* base = reinterpret_cast<nsStyleAutoArray<StyleTransition>*>(aArray);
  EnsureStyleAutoArrayLength(base, aLen);
}

void Gecko_EnsureStyleScrollTimelineArrayLength(void* aArray, size_t aLen) {
  auto* base = static_cast<nsStyleAutoArray<StyleScrollTimeline>*>(aArray);
  EnsureStyleAutoArrayLength(base, aLen);
}

void Gecko_EnsureStyleViewTimelineArrayLength(void* aArray, size_t aLen) {
  auto* base = static_cast<nsStyleAutoArray<StyleViewTimeline>*>(aArray);
  EnsureStyleAutoArrayLength(base, aLen);
}

// Note: There is a specialized version, FindOrInsertInitialOrFinalKeyframe(),
// in CSSAnimation.cpp, and it finds the matched keyframes based on the computed
// offset. However, this function is to find or create the matched keyframe
// based on the specified <keyframe-selector>, for grouping them.
// For more details, see the step 2.2 in
// https://drafts.csswg.org/css-animations-2/#keyframe-processing
static std::pair<Keyframe*, size_t> GetOrCreateKeyframe(
    nsTArray<Keyframe>* aKeyframes, StyleTimelineRangeName aRangeName,
    float aOffset, const StyleComputedTimingFunction* aTimingFunction,
    const CompositeOperationOrAuto aComposition) {
  MOZ_ASSERT(aKeyframes, "The keyframe array should be valid");
  MOZ_ASSERT(aTimingFunction, "The timing function should be valid");
  MOZ_ASSERT(aRangeName != StyleTimelineRangeName::None ||
                 (aRangeName == StyleTimelineRangeName::None && aOffset >= 0. &&
                  aOffset <= 1.),
             "The percentage offset should be in the range of [0.0, 1.0]");

  const auto& offset = Keyframe::OffsetType{aRangeName, (double)aOffset};
  size_t keyframeIndex;
  // We search the keyframes in the reversed order because the newest added
  // keyframe is the most possible one we should match (since we sorted the
  // keyframes with <percentage> offsets already).
  if (nsAnimationManager::FindMatchingKeyframe(Reversed(*aKeyframes), offset,
                                               *aTimingFunction, aComposition,
                                               keyframeIndex)) {
    return {&(*aKeyframes)[aKeyframes->Length() - 1 - keyframeIndex],
            aKeyframes->Length() - 1 - keyframeIndex};
  }
  keyframeIndex = aKeyframes->Length() - 1;

  Keyframe* keyframe = aKeyframes->AppendElement();
  keyframe->mOffset.emplace(offset);
  if (!aTimingFunction->IsLinearKeyword()) {
    keyframe->mTimingFunction.emplace(*aTimingFunction);
  }
  keyframe->mComposite = aComposition;
  // Return the length of aKeyframes to represent the new Keyframe is inserted.
  return {keyframe, aKeyframes->Length()};
}

Keyframe* Gecko_GetOrCreateKeyframeForPercentageOffset(
    nsTArray<Keyframe>* aKeyframes, float aOffset,
    const StyleComputedTimingFunction* aTimingFunction,
    const CompositeOperationOrAuto aComposition) {
  MOZ_ASSERT(aKeyframes->IsEmpty() ||
                 aKeyframes->LastElement().mOffset->mPercentage >= aOffset,
             "The percentage offset should be less than or equal to the last "
             "keyframe's offset if there are exisiting keyframes");
  return GetOrCreateKeyframe(aKeyframes, StyleTimelineRangeName::None, aOffset,
                             aTimingFunction, aComposition)
      .first;
}

Keyframe* Gecko_GetOrCreateKeyframeForTimelineRangeOffset(
    nsTArray<Keyframe>* aKeyframes, const StyleTimelineRangeName aRangeName,
    float aOffset, const StyleComputedTimingFunction* aTimingFunction,
    const CompositeOperationOrAuto aComposition, size_t* aMatchedIdx) {
  MOZ_ASSERT(aRangeName != StyleTimelineRangeName::Normal,
             "normal shouldn't be used");
  auto [keyframe, idx] = GetOrCreateKeyframe(aKeyframes, aRangeName, aOffset,
                                             aTimingFunction, aComposition);
  *aMatchedIdx = idx;
  return keyframe;
}

void Gecko_GetComputedURLSpec(const StyleComputedUrl* aURL, nsCString* aOut) {
  MOZ_ASSERT(aURL);
  MOZ_ASSERT(aOut);
  if (aURL->IsLocalRef()) {
    aOut->Assign(aURL->SpecifiedSerialization());
    return;
  }

  if (nsIURI* uri = aURL->GetURI()) {
    nsresult rv = uri->GetSpec(*aOut);
    if (NS_SUCCEEDED(rv)) {
      return;
    }
  }

  // Empty URL computes to empty, per spec:
  if (aURL->SpecifiedSerialization().IsEmpty()) {
    aOut->Truncate();
  } else {
    aOut->AssignLiteral("about:invalid");
  }
}

bool Gecko_IsSupportedImageMimeType(const uint8_t* aMimeType,
                                    const uint32_t aLen) {
  nsDependentCSubstring mime(reinterpret_cast<const char*>(aMimeType), aLen);
  return imgLoader::SupportImageWithMimeType(
      mime, AcceptedMimeTypes::IMAGES_AND_DOCUMENTS);
}

void Gecko_nsIURI_Debug(nsIURI* aURI, nsCString* aOut) {
  // TODO(emilio): Do we have more useful stuff to put here, maybe?
  if (aURI) {
    *aOut = aURI->GetSpecOrDefault();
  }
}

// XXX Implemented by hand because even though it's thread-safe, only the
// subclasses have the HasThreadSafeRefCnt bits.
void Gecko_AddRefnsIURIArbitraryThread(nsIURI* aPtr) { NS_ADDREF(aPtr); }
void Gecko_ReleasensIURIArbitraryThread(nsIURI* aPtr) { NS_RELEASE(aPtr); }

void Gecko_nsIReferrerInfo_Debug(nsIReferrerInfo* aReferrerInfo,
                                 nsCString* aOut) {
  if (aReferrerInfo) {
    if (nsCOMPtr<nsIURI> referrer = aReferrerInfo->GetComputedReferrer()) {
      *aOut = referrer->GetSpecOrDefault();
    }
  }
}

template <typename ElementLike>
void DebugListAttributes(const ElementLike& aElement, nsCString& aOut) {
  const uint32_t kMaxAttributeLength = 40;

  uint32_t i = 0;
  while (BorrowedAttrInfo info = aElement.GetAttrInfoAt(i++)) {
    aOut.AppendLiteral(" ");
    if (nsAtom* prefix = info.mName->GetPrefix()) {
      aOut.Append(NS_ConvertUTF16toUTF8(nsDependentAtomString(prefix)));
      aOut.AppendLiteral(":");
    }
    aOut.Append(
        NS_ConvertUTF16toUTF8(nsDependentAtomString(info.mName->LocalName())));
    if (!info.mValue) {
      continue;
    }
    aOut.AppendLiteral("=\"");
    nsAutoString value;
    info.mValue->ToString(value);
    if (value.Length() > kMaxAttributeLength) {
      value.Truncate(kMaxAttributeLength - 3);
      value.AppendLiteral("...");
    }
    aOut.Append(NS_ConvertUTF16toUTF8(value));
    aOut.AppendLiteral("\"");
  }
}

void Gecko_Element_DebugListAttributes(const Element* aElement,
                                       nsCString* aOut) {
  DebugListAttributes(*aElement, *aOut);
}

void Gecko_Snapshot_DebugListAttributes(const ServoElementSnapshot* aSnapshot,
                                        nsCString* aOut) {
  DebugListAttributes(*aSnapshot, *aOut);
}

NS_IMPL_THREADSAFE_FFI_REFCOUNTING(URLExtraData, URLExtraData);

bool Gecko_IsURIInList(const URLExtraData* aData, const nsACString* aList) {
  return nsContentUtils::IsURIInList(aData->BaseURI(),
                                     PromiseFlatCString(*aList));
}

Length Gecko_nsStyleFont_ComputeMinSize(const nsStyleFont* aFont,
                                        const Document* aDocument) {
  // Don't change font-size:0, since that would un-hide hidden text.
  if (aFont->mSize.IsZero()) {
    return {0};
  }
  // Don't change it for docs where we don't enable the min-font-size.
  if (!aFont->MinFontSizeEnabled()) {
    return {0};
  }
  Length minFontSize =
      aDocument->GetFontPrefsForLang(aFont->GetLangAtom())->mMinimumFontSize;
  if (minFontSize.ToCSSPixels() <= 0.0f) {
    return {0};
  }
  minFontSize.ScaleBy(aFont->mMinFontSizeRatio._0);
  return minFontSize;
}

static StaticRefPtr<UACacheReporter> gUACacheReporter;

namespace mozilla {

void InitializeServo() {
  URLExtraData::Init();
  Servo_Initialize(URLExtraData::Dummy(), URLExtraData::DummyChrome());

  gUACacheReporter = new UACacheReporter();
  RegisterWeakMemoryReporter(gUACacheReporter);

  sServoFFILock = new Mutex("Servo::FFILock");
}

void ShutdownServo() {
  MOZ_ASSERT(sServoFFILock);

  UnregisterWeakMemoryReporter(gUACacheReporter);
  gUACacheReporter = nullptr;

  sServoFFILock = nullptr;
  Servo_Shutdown();

  URLExtraData::Shutdown();
}

#ifdef DEBUG
void AssertIsMainThreadOrServoFFILocked() {
  if (!NS_IsMainThread()) {
    MOZ_ASSERT(sServoFFILock);
    sServoFFILock->AssertCurrentThreadOwns();
  }
}
#endif

}  // namespace mozilla

GeckoFontMetrics Gecko_GetFontMetrics(const nsPresContext* aPresContext,
                                      bool aIsVertical,
                                      const nsStyleFont* aFont,
                                      Length aFontSize,
                                      StyleQueryFontMetricsFlags flags) {
  MutexAutoLock guard(*sServoFFILock);

  // Getting font metrics can require some main thread only work to be
  // done, such as work that needs to touch non-threadsafe refcounted
  // objects (like the DOM FontFace/FontFaceSet objects), network loads, etc.
  //
  // To handle this work, font code checks whether we are in a Servo traversal
  // and if so, appends PostTraversalTasks to the current ServoStyleSet
  // to be performed immediately after the traversal is finished.  This
  // works well for starting downloadable font loads, since we don't have
  // those fonts available to get metrics for anyway.  Platform fonts and
  // ArrayBuffer-backed FontFace objects are handled synchronously.

  nsPresContext* presContext = const_cast<nsPresContext*>(aPresContext);
  RefPtr<nsFontMetrics> fm = nsLayoutUtils::GetMetricsFor(
      presContext, aIsVertical, aFont, aFontSize,
      bool(flags & StyleQueryFontMetricsFlags::USE_USER_FONT_SET));
  auto* fontGroup = fm->GetThebesFontGroup();
  auto metrics = fontGroup->GetMetricsForCSSUnits(fm->Orientation(), flags);

  float scriptPercentScaleDown = 0;
  float scriptScriptPercentScaleDown = 0;
  if (flags & StyleQueryFontMetricsFlags::NEEDS_MATH_SCALES) {
    RefPtr<gfxFont> font = fontGroup->GetFirstValidFont();
    if (font->TryGetMathTable()) {
      scriptPercentScaleDown = static_cast<float>(
          font->MathTable()->Constant(gfxMathTable::ScriptPercentScaleDown));
      scriptScriptPercentScaleDown =
          static_cast<float>(font->MathTable()->Constant(
              gfxMathTable::ScriptScriptPercentScaleDown));
    }
  }

  int32_t d2a = aPresContext->AppUnitsPerDevPixel();
  auto ToLength = [](nscoord aLen) {
    return Length::FromPixels(CSSPixel::FromAppUnits(aLen));
  };
  return {ToLength(NS_round(metrics.xHeight * d2a)),
          ToLength(NS_round(metrics.zeroWidth * d2a)),
          ToLength(NS_round(metrics.capHeight * d2a)),
          ToLength(NS_round(metrics.ideographicWidth * d2a)),
          ToLength(NS_round(metrics.maxAscent * d2a)),
          ToLength(NS_round(fontGroup->GetStyle()->size * d2a)),
          scriptPercentScaleDown,
          scriptScriptPercentScaleDown};
}

NS_IMPL_THREADSAFE_FFI_REFCOUNTING(SheetLoadDataHolder, SheetLoadDataHolder);

void Gecko_StyleSheet_FinishAsyncParse(
    SheetLoadDataHolder* aData,
    StyleStrong<StyleStylesheetContents> aSheetContents) {
  RefPtr<SheetLoadDataHolder> loadData = aData;
  RefPtr<StyleStylesheetContents> sheetContents = aSheetContents.Consume();
  NS_DispatchToMainThreadQueue(
      NS_NewRunnableFunction(__func__,
                             [d = std::move(loadData),
                              contents = std::move(sheetContents)]() mutable {
                               MOZ_ASSERT(NS_IsMainThread());
                               SheetLoadData* data = d->get();
                               data->mSheet->FinishAsyncParse(
                                   contents.forget());
                             }),
      EventQueuePriority::RenderBlocking);
}

static already_AddRefed<StyleSheet> LoadImportSheet(
    Loader* aLoader, StyleSheet* aParent, SheetLoadData* aParentLoadData,
    LoaderReusableStyleSheets* aReusableSheets, const StyleCssUrl& aURL,
    already_AddRefed<StyleLockedMediaList> aMediaList) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aLoader, "Should've catched this before");
  MOZ_ASSERT(aParent, "Only used for @import, so parent should exist!");

  auto media = MakeRefPtr<MediaList>(std::move(aMediaList));
  nsCOMPtr<nsIURI> uri = aURL.GetURI();
  nsresult rv = uri ? NS_OK : NS_ERROR_FAILURE;

  size_t previousSheetCount = aParent->ChildSheets().Length();
  if (NS_SUCCEEDED(rv)) {
    // TODO(emilio): We should probably make LoadChildSheet return the
    // stylesheet rather than the return code.
    rv = aLoader->LoadChildSheet(*aParent, aParentLoadData, uri, media,
                                 aReusableSheets);
  }

  if (NS_FAILED(rv) || previousSheetCount == aParent->ChildSheets().Length()) {
    // Servo and Gecko have different ideas of what a valid URL is, so we might
    // get in here with a URL string that NS_NewURI can't handle.  We may also
    // reach here via an import cycle.  For the import cycle case, we need some
    // sheet object per spec, even if its empty.  DevTools uses the URI to
    // realize it has hit an import cycle, so we mark it complete to make the
    // sheet readable from JS.
    RefPtr<StyleSheet> emptySheet =
        aParent->CreateEmptyChildSheet(media.forget());
    // Make a dummy URI if we don't have one because some methods assume
    // non-null URIs.
    if (!uri) {
      NS_NewURI(getter_AddRefs(uri), "about:invalid"_ns);
    }
    nsCOMPtr<nsIReferrerInfo> referrerInfo =
        ReferrerInfo::CreateForExternalCSSResources(emptySheet, uri);
    emptySheet->SetURIs(uri, uri, referrerInfo, aURL.ExtraData().Principal());
    emptySheet->SetComplete();
    aParent->AppendStyleSheet(*emptySheet);
    return emptySheet.forget();
  }

  RefPtr<StyleSheet> sheet = aParent->ChildSheets().LastElement();
  return sheet.forget();
}

StyleSheet* Gecko_LoadStyleSheet(Loader* aLoader, StyleSheet* aParent,
                                 SheetLoadData* aParentLoadData,
                                 LoaderReusableStyleSheets* aReusableSheets,
                                 const StyleCssUrl* aUrl,
                                 StyleStrong<StyleLockedMediaList> aMediaList) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aUrl);

  return LoadImportSheet(aLoader, aParent, aParentLoadData, aReusableSheets,
                         *aUrl, aMediaList.Consume())
      .take();
}

void Gecko_LoadStyleSheetAsync(SheetLoadDataHolder* aParentData,
                               const StyleCssUrl* aUrl,
                               StyleStrong<StyleLockedMediaList> aMediaList,
                               StyleStrong<StyleLockedImportRule> aImportRule) {
  MOZ_ASSERT(aUrl);
  RefPtr<SheetLoadDataHolder> loadData = aParentData;
  RefPtr<StyleLockedMediaList> mediaList = aMediaList.Consume();
  RefPtr<StyleLockedImportRule> importRule = aImportRule.Consume();
  NS_DispatchToMainThreadQueue(
      NS_NewRunnableFunction(
          __func__,
          [data = std::move(loadData), url = StyleCssUrl(*aUrl),
           media = std::move(mediaList),
           import = std::move(importRule)]() mutable {
            MOZ_ASSERT(NS_IsMainThread());
            SheetLoadData* d = data->get();
            RefPtr<StyleSheet> sheet = LoadImportSheet(
                d->mLoader, d->mSheet, d, nullptr, url, media.forget());
            Servo_ImportRule_SetSheet(import, sheet);
          }),
      EventQueuePriority::RenderBlocking);
}

void Gecko_AddPropertyToSet(nsCSSPropertyIDSet* aPropertySet,
                            NonCustomCSSPropertyId aProperty) {
  aPropertySet->AddProperty(aProperty);
}

bool Gecko_DocumentRule_UseForPresentation(
    const Document* aDocument, const nsACString* aPattern,
    DocumentMatchingFunction aMatchingFunction) {
  MOZ_ASSERT(NS_IsMainThread());

  nsIURI* docURI = aDocument->GetDocumentURI();
  nsAutoCString docURISpec;
  if (docURI) {
    // If GetSpec fails (due to OOM) just skip these URI-specific CSS rules.
    nsresult rv = docURI->GetSpec(docURISpec);
    NS_ENSURE_SUCCESS(rv, false);
  }

  return CSSMozDocumentRule::Match(aDocument, docURI, docURISpec, *aPattern,
                                   aMatchingFunction);
}

void Gecko_SetJemallocThreadLocalArena(bool enabled) {
#if defined(MOZ_MEMORY)
  jemalloc_thread_local_arena(enabled);
#endif
}

template <typename T>
void Construct(T* aPtr, const Document* aDoc) {
  if constexpr (std::is_constructible_v<T, const Document&>) {
    MOZ_ASSERT(aDoc);
    new (KnownNotNull, aPtr) T(*aDoc);
  } else {
    MOZ_ASSERT(!aDoc);
    new (KnownNotNull, aPtr) T();
    // These instance are intentionally global, and we don't want leakcheckers
    // to report them.
    aPtr->MarkLeaked();
  }
}

#define GENERATE_GECKO_FUNCTIONS(name)                                 \
  void Gecko_Construct_Default_nsStyle##name(nsStyle##name* ptr,       \
                                             const Document* doc) {    \
    Construct(ptr, doc);                                               \
  }                                                                    \
  void Gecko_CopyConstruct_nsStyle##name(nsStyle##name* ptr,           \
                                         const nsStyle##name* other) { \
    new (ptr) nsStyle##name(*other);                                   \
  }                                                                    \
  void Gecko_Destroy_nsStyle##name(nsStyle##name* ptr) {               \
    ptr->~nsStyle##name();                                             \
  }

FOR_EACH_STYLE_STRUCT(GENERATE_GECKO_FUNCTIONS, GENERATE_GECKO_FUNCTIONS)

#undef GENERATE_GECKO_FUNCTIONS

bool Gecko_ErrorReportingEnabled(const StyleSheet* aSheet,
                                 const Loader* aLoader,
                                 uint64_t* aOutWindowId) {
  if (!ErrorReporter::ShouldReportErrors(aSheet, aLoader)) {
    return false;
  }
  *aOutWindowId = ErrorReporter::FindInnerWindowId(aSheet, aLoader);
  return true;
}

void Gecko_ReportUnexpectedCSSError(const uint64_t aWindowId, nsIURI* aURI,
                                    const char* message, const char* param,
                                    uint32_t paramLen, const char* prefix,
                                    const char* prefixParam,
                                    uint32_t prefixParamLen, const char* suffix,
                                    const char* selectors,
                                    uint32_t selectorsLen, uint32_t lineNumber,
                                    uint32_t colNumber) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  ErrorReporter reporter(aWindowId);

  if (prefix) {
    if (prefixParam) {
      nsDependentCSubstring paramValue(prefixParam, prefixParamLen);
      AutoTArray<nsString, 1> wideParam;
      CopyUTF8toUTF16(paramValue, *wideParam.AppendElement());
      reporter.ReportUnexpectedUnescaped(prefix, wideParam);
    } else {
      reporter.ReportUnexpected(prefix);
    }
  }

  if (param) {
    nsDependentCSubstring paramValue(param, paramLen);
    AutoTArray<nsString, 1> wideParam;
    CopyUTF8toUTF16(paramValue, *wideParam.AppendElement());
    reporter.ReportUnexpectedUnescaped(message, wideParam);
  } else {
    reporter.ReportUnexpected(message);
  }

  if (suffix) {
    reporter.ReportUnexpected(suffix);
  }
  nsDependentCSubstring selectorsValue(selectors, selectorsLen);
  reporter.OutputError(selectorsValue, lineNumber + 1, colNumber, aURI);
}

void Gecko_ContentList_AppendAll(SimpleContentList* aList,
                                 const Element** aElements, size_t aLength) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aElements);
  MOZ_ASSERT(aLength);
  MOZ_ASSERT(aList);

  aList->SetCapacity(aLength);

  for (size_t i = 0; i < aLength; ++i) {
    aList->AppendElement(const_cast<Element*>(aElements[i]));
  }
}

RustSpan<const Element* const> Gecko_Document_GetElementsWithId(
    const Document* aDoc, nsAtom* aId) {
  MOZ_ASSERT(aDoc);
  MOZ_ASSERT(aId);
  auto span = aDoc->GetAllElementsForId(aId);
  return {span.Elements(), span.Length()};
}

RustSpan<const Element* const> Gecko_ShadowRoot_GetElementsWithId(
    const ShadowRoot* aShadowRoot, nsAtom* aId) {
  MOZ_ASSERT(aShadowRoot);
  MOZ_ASSERT(aId);
  auto span = aShadowRoot->GetAllElementsForId(aId);
  return {span.Elements(), span.Length()};
}

static StyleComputedMozPrefFeatureValue GetPrefValue(const nsCString& aPref) {
  using Value = StyleComputedMozPrefFeatureValue;
  switch (Preferences::GetType(aPref.get())) {
    case nsIPrefBranch::PREF_STRING: {
      nsAutoString value;
      Preferences::GetString(aPref.get(), value);
      return Value::String(StyleAtomString{NS_Atomize(value)});
    }
    case nsIPrefBranch::PREF_INT:
      return Value::Integer(Preferences::GetInt(aPref.get(), 0));
    case nsIPrefBranch::PREF_BOOL: {
      auto value = Preferences::GetBool(aPref.get(), false)
                       ? StyleBoolValue::True
                       : StyleBoolValue::False;
      return Value::Boolean(value);
    }
    case nsIPrefBranch::PREF_INVALID:
    default:
      break;
  }

  return StyleComputedMozPrefFeatureValue::None();
}

bool Gecko_EvalMozPrefFeature(nsAtom* aPref,
                              const StyleComputedMozPrefFeatureValue* aValue) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aValue);
  using Value = StyleComputedMozPrefFeatureValue;
  using PrefMap = nsTHashMap<RefPtr<nsAtom>, Value>;
  // This map leaks until shutdown, but that's fine, all the values are
  // controlled by us so it's not expected to be big.
  static StaticAutoPtr<PrefMap> sRegisteredPrefs;
  if (!sRegisteredPrefs) {
    if (PastShutdownPhase(ShutdownPhase::XPCOMShutdownFinal)) {
      // Styling doesn't really matter much at this point, don't bother.
      return false;
    }
    sRegisteredPrefs = new PrefMap();
    ClearOnShutdown(&sRegisteredPrefs);
  }

  const auto& value = sRegisteredPrefs->LookupOrInsertWith(aPref, [&] {
    nsAutoAtomCString prefName(aPref);
    Preferences::RegisterCallback(
        [](const char* aPrefName, void*) {
          nsDependentCString name(aPrefName);
          if (sRegisteredPrefs) {
            RefPtr<nsAtom> nameAtom = NS_Atomize(name);
            sRegisteredPrefs->InsertOrUpdate(nameAtom, GetPrefValue(name));
          }
          LookAndFeel::NotifyChangedAllWindows(
              widget::ThemeChangeKind::MediaQueriesOnly);
        },
        prefName);
    return GetPrefValue(prefName);
  });
  if (aValue->IsNone()) {
    // For a non-specified query, we return true if the pref is not false, zero,
    // empty or invalid
    switch (value.tag) {
      case Value::Tag::None:
        return false;
      case Value::Tag::Boolean:
        return value.AsBoolean() == StyleBoolValue::True;
      case Value::Tag::Integer:
        return value.AsInteger() != 0;
      case Value::Tag::String:
        return !value.AsString().AsAtom()->IsEmpty();
    }
    return false;
  }
  return value == *aValue;
}

bool Gecko_IsFontFormatSupported(StyleFontFaceSourceFormatKeyword aFormat) {
  return gfxPlatform::GetPlatform()->IsFontFormatSupported(
      aFormat, StyleFontFaceSourceTechFlags::Empty());
}

bool Gecko_IsFontTechSupported(StyleFontFaceSourceTechFlags aFlag) {
  return gfxPlatform::GetPlatform()->IsFontFormatSupported(
      StyleFontFaceSourceFormatKeyword::None, aFlag);
}

bool Gecko_IsKnownIconFontFamily(const nsAtom* aFamilyName) {
  return gfxPlatform::GetPlatform()->IsKnownIconFontFamily(aFamilyName);
}

bool Gecko_IsInServoTraversal() { return ServoStyleSet::IsInServoTraversal(); }

bool Gecko_IsMainThread() { return NS_IsMainThread(); }

bool Gecko_IsDOMWorkerThread() { return !!GetCurrentThreadWorkerPrivate(); }

int32_t Gecko_GetNumStyleThreads() {
  if (const auto& cpuInfo = hal::GetHeterogeneousCpuInfo()) {
    size_t numBigCpus = cpuInfo->mBigCpus.Count();
    // If CPUs are homogeneous we do not need to override stylo's
    // default number of threads.
    if (numBigCpus != cpuInfo->mTotalNumCpus) {
      // From testing on a variety of devices it appears using only
      // the number of big cores gives best performance when there are
      // 2 or more big cores. If there are fewer than 2 big cores then
      // additionally using the medium cores performs better.
      if (numBigCpus >= 2) {
        return static_cast<int32_t>(numBigCpus);
      }
      return static_cast<int32_t>(numBigCpus + cpuInfo->mMediumCpus.Count());
    }
  }

  return -1;
}

const nsAttrValue* Gecko_GetSVGAnimatedClass(const Element* aElement) {
  MOZ_ASSERT(aElement->IsSVGElement());
  return static_cast<const SVGElement*>(aElement)->GetAnimatedClassName();
}

bool Gecko_AssertClassAttrValueIsSane(const nsAttrValue* aValue) {
  MOZ_ASSERT(aValue->Type() == nsAttrValue::eAtom ||
             aValue->Type() == nsAttrValue::eString ||
             aValue->Type() == nsAttrValue::eAtomArray);
  MOZ_ASSERT_IF(
      aValue->Type() == nsAttrValue::eString,
      nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(
          aValue->GetStringValue())
          .IsEmpty());
  return true;
}

void Gecko_GetSafeAreaInsets(const nsPresContext* aPresContext, float* aTop,
                             float* aRight, float* aBottom, float* aLeft) {
  MOZ_ASSERT(aPresContext);
  const CSSMargin insets =
      LayoutDeviceMargin(aPresContext->GetSafeAreaInsets()) /
      aPresContext->CSSToDevPixelScale();
  *aTop = insets.top;
  *aRight = insets.right;
  *aBottom = insets.bottom;
  *aLeft = insets.left;
}

void Gecko_PrintfStderr(const nsCString* aStr) {
  printf_stderr("%s", aStr->get());
}

nsAtom* Gecko_Element_ImportedPart(const nsAttrValue* aValue,
                                   nsAtom* aPartName) {
  if (aValue->Type() != nsAttrValue::eShadowParts) {
    return nullptr;
  }
  return aValue->GetShadowPartsValue().GetReverse(aPartName);
}

nsAtom** Gecko_Element_ExportedParts(const nsAttrValue* aValue,
                                     nsAtom* aPartName, size_t* aOutLength) {
  if (aValue->Type() != nsAttrValue::eShadowParts) {
    return nullptr;
  }
  auto* parts = aValue->GetShadowPartsValue().Get(aPartName);
  if (!parts) {
    return nullptr;
  }
  *aOutLength = parts->Length();
  static_assert(sizeof(RefPtr<nsAtom>) == sizeof(nsAtom*));
  static_assert(alignof(RefPtr<nsAtom>) == alignof(nsAtom*));
  return reinterpret_cast<nsAtom**>(parts->Elements());
}

uint64_t Gecko_Element_GetSubtreeBloomFilter(const Element* aElement) {
  return aElement->GetSubtreeBloomFilter();
}

uint64_t Gecko_Element_GetRandomSeed(const Element* aElement) {
  return aElement->GetRandomSeed();
}

void Gecko_Document_GetRandomHasherKey(const Document* aDocument, uint64_t* aK0,
                                       uint64_t* aK1) {
  const Array<uint64_t, 2>& key = aDocument->GetRandomHasherKey();
  *aK0 = key[0];
  *aK1 = key[1];
}

bool StyleSingleFontFamily::IsNamedFamily(const nsAString& aFamilyName) const {
  if (!IsFamilyName()) {
    return false;
  }
  nsDependentAtomString name(AsFamilyName().name.AsAtom());
  return name.Equals(aFamilyName, nsCaseInsensitiveStringComparator);
}

StyleSingleFontFamily StyleSingleFontFamily::Parse(
    const nsACString& aFamilyOrGenericName) {
  // should only be passed a single font - not entirely correct, a family
  // *could* have a comma in it but in practice never does so
  // for debug purposes this is fine
  NS_ASSERTION(aFamilyOrGenericName.FindChar(',') == -1,
               "Convert method should only be passed a single family name");

  auto genericType = Servo_GenericFontFamily_Parse(&aFamilyOrGenericName);
  if (genericType != StyleGenericFontFamily::None) {
    return Generic(genericType);
  }
  return FamilyName({StyleAtom(NS_Atomize(aFamilyOrGenericName)),
                     StyleFontFamilyNameSyntax::Identifiers});
}

void StyleSingleFontFamily::AppendToString(nsACString& aName,
                                           bool aQuote) const {
  if (IsFamilyName()) {
    const auto& name = AsFamilyName();
    if (!aQuote) {
      aName.Append(nsAutoAtomCString(name.name.AsAtom()));
      return;
    }
    Servo_FamilyName_Serialize(&name, &aName);
    return;
  }

  switch (AsGeneric()) {
    case StyleGenericFontFamily::None:
    case StyleGenericFontFamily::MozEmoji:
      MOZ_FALLTHROUGH_ASSERT("Should never appear in a font-family name!");
    case StyleGenericFontFamily::Serif:
      return aName.AppendLiteral("serif");
    case StyleGenericFontFamily::SansSerif:
      return aName.AppendLiteral("sans-serif");
    case StyleGenericFontFamily::Monospace:
      return aName.AppendLiteral("monospace");
    case StyleGenericFontFamily::Cursive:
      return aName.AppendLiteral("cursive");
    case StyleGenericFontFamily::Fantasy:
      return aName.AppendLiteral("fantasy");
    case StyleGenericFontFamily::Math:
      return aName.AppendLiteral("math");
    case StyleGenericFontFamily::SystemUi:
      return aName.AppendLiteral("system-ui");
  }
  MOZ_ASSERT_UNREACHABLE("Unknown generic font-family!");
  return aName.AppendLiteral("serif");
}

StyleFontFamilyList StyleFontFamilyList::WithNames(
    nsTArray<StyleSingleFontFamily>&& aNames) {
  StyleFontFamilyList list;
  Servo_FontFamilyList_WithNames(&aNames, &list);
  return list;
}

StyleFontFamilyList StyleFontFamilyList::WithOneUnquotedFamily(
    const nsACString& aName) {
  AutoTArray<StyleSingleFontFamily, 1> names;
  names.AppendElement(StyleSingleFontFamily::FamilyName(
      {StyleAtom(NS_Atomize(aName)), StyleFontFamilyNameSyntax::Identifiers}));
  return WithNames(std::move(names));
}

static bool AnchorSideUsesCBWM(
    const StyleAnchorSideKeyword& aAnchorSideKeyword) {
  switch (aAnchorSideKeyword) {
    case StyleAnchorSideKeyword::SelfStart:
    case StyleAnchorSideKeyword::SelfEnd:
      return false;
    case StyleAnchorSideKeyword::Inside:
    case StyleAnchorSideKeyword::Outside:
    case StyleAnchorSideKeyword::Start:
    case StyleAnchorSideKeyword::End:
    case StyleAnchorSideKeyword::Center:
      return true;
    // Return value shouldn't matter for these physical keywords.
    case StyleAnchorSideKeyword::Left:
    case StyleAnchorSideKeyword::Right:
    case StyleAnchorSideKeyword::Top:
    case StyleAnchorSideKeyword::Bottom:
      return true;
  }
  return false;
}

bool Gecko_GetAnchorPosOffset(const AnchorPosOffsetResolutionParams* aParams,
                              const nsAtom* aAnchorName,
                              const StyleCascadeLevel* aTreeScope,
                              StylePhysicalSide aPropSide,
                              StyleAnchorSideKeyword aAnchorSideKeyword,
                              float aPercentage, Length* aOut) {
  if (!aParams || !aParams->mBaseParams.mFrame) {
    return false;
  }
  const auto* positioned = aParams->mBaseParams.mFrame;
  const auto* containingBlock = positioned->GetParent();
  auto* cache = aParams->mBaseParams.mCache;
  const auto info = AnchorPositioningUtils::ResolveAnchorPosRect(
      positioned, containingBlock, {aAnchorName, *aTreeScope},
      !aParams->mCBSize, cache);
  if (!info) {
    return false;
  }
  if (cache) {
    // Cache is set during reflow, which is really the only time we want to
    // actively modify scroll compensation state & side.
    if (info->mCompensatesForScroll) {
      const auto axis = [aPropSide]() {
        switch (aPropSide) {
          case StylePhysicalSide::Left:
          case StylePhysicalSide::Right:
            return PhysicalAxis::Horizontal;
          case StylePhysicalSide::Top:
          case StylePhysicalSide::Bottom:
            break;
          default:
            MOZ_ASSERT_UNREACHABLE("Unhandled side?");
        }
        return PhysicalAxis::Vertical;
      }();
      cache->mReferenceData->AdjustCompensatingForScroll(axis);
      // Non scroll-compensated anchor will not have any impact on the
      // containing block due to scrolling. See documentation for
      // `mScrollCompensatedSides`.
      cache->mReferenceData->mScrollCompensatedSides |=
          SideToSideBit(ToSide(aPropSide));
    }
  }
  // Compute the offset here in C++, where translating between physical/logical
  // coordinates is easier.

  const auto usesCBWM = AnchorSideUsesCBWM(aAnchorSideKeyword);
  const auto cbwm = containingBlock->GetWritingMode();
  const auto wm =
      usesCBWM ? cbwm : aParams->mBaseParams.mFrame->GetWritingMode();
  const auto [rect, logicalCBSize] = [&] {
    // We need `AnchorPosReferenceData` to compute the anchor offset against
    // the adjusted CB, so make the best attempt to retrieve it.
    // TODO(dshin, bug 2005207): We really need to unify containing block
    // lookups and clean up cache lookups here.
    const auto* referenceData =
        cache ? cache->mReferenceData
              : positioned->GetProperty(nsIFrame::AnchorPosReferences());
    if (!referenceData) {
      return std::make_pair(
          info->mRect, aParams->mCBSize ? aParams->mCBSize->ConvertTo(wm, cbwm)
                                        : containingBlock->PaddingSize(wm));
    }
    // Offset happens from padding rect.
    const auto offset = referenceData->mAdjustedContainingBlock.TopLeft() -
                        referenceData->mOriginalContainingBlockRect.TopLeft();
    return std::make_pair(
        info->mRect - offset,
        aParams->mCBSize
            ? aParams->mCBSize->ConvertTo(wm, cbwm)
            : LogicalSize{cbwm, referenceData->mAdjustedContainingBlock.Size()}
                  .ConvertTo(wm, cbwm));
  }();
  const LogicalRect logicalAnchorRect{wm, rect,
                                      logicalCBSize.GetPhysicalSize(wm)};
  const auto logicalPropSide = wm.LogicalSideForPhysicalSide(ToSide(aPropSide));
  const auto propAxis = GetAxis(logicalPropSide);
  const auto propEdge = GetEdge(logicalPropSide);

  const auto anchorEdge = [&]() {
    switch (aAnchorSideKeyword) {
      case StyleAnchorSideKeyword::Left:
        return GetEdge(wm.LogicalSideForPhysicalSide(eSideLeft));
      case StyleAnchorSideKeyword::Right:
        return GetEdge(wm.LogicalSideForPhysicalSide(eSideRight));
      case StyleAnchorSideKeyword::Top:
        return GetEdge(wm.LogicalSideForPhysicalSide(eSideTop));
      case StyleAnchorSideKeyword::Bottom:
        return GetEdge(wm.LogicalSideForPhysicalSide(eSideBottom));
      case StyleAnchorSideKeyword::Inside:
        return propEdge;
      case StyleAnchorSideKeyword::Outside:
        return GetOppositeEdge(propEdge);
      case StyleAnchorSideKeyword::Start:
      case StyleAnchorSideKeyword::SelfStart:
      case StyleAnchorSideKeyword::Center:
        return LogicalEdge::Start;
      case StyleAnchorSideKeyword::End:
      case StyleAnchorSideKeyword::SelfEnd:
        return LogicalEdge::End;
    }
    return LogicalEdge::Start;
  }();

  nscoord result = [&]() {
    // Offset to the desired anchor edge, from the containing block's start
    // edge.
    const auto anchorOffsetFromStartEdge =
        anchorEdge == LogicalEdge::Start ? logicalAnchorRect.Start(propAxis, wm)
                                         : logicalAnchorRect.End(propAxis, wm);
    if (propEdge == LogicalEdge::Start) {
      return anchorOffsetFromStartEdge;
    }
    // Need the offset from the end edge of the containing block.
    const auto anchorOffsetFromEndEdge =
        logicalCBSize.Size(propAxis, wm) - anchorOffsetFromStartEdge;
    return anchorOffsetFromEndEdge;
  }();

  // Apply the percentage value, with the percentage basis as the anchor
  // element's size in the relevant axis.
  if (aPercentage != 0.f) {
    const nscoord anchorSize = LogicalSize{wm, rect.Size()}.Size(propAxis, wm);
    result += (propEdge == LogicalEdge::End ? -1 : 1) *
              ((aPercentage != 1.f)
                   ? NSToCoordRoundWithClamp(aPercentage *
                                             static_cast<float>(anchorSize))
                   : anchorSize);
  }
  *aOut = Length::FromPixels(CSSPixel::FromAppUnits(result));
  return true;
}

bool Gecko_GetAnchorPosSize(const AnchorPosResolutionParams* aParams,
                            const nsAtom* aAnchorName,
                            const mozilla::StyleCascadeLevel* aTreeScope,
                            StylePhysicalAxis aPropAxis,
                            StyleAnchorSizeKeyword aAnchorSizeKeyword,
                            Length* aOut) {
  if (!aParams || !aParams->mFrame) {
    return false;
  }
  const auto* positioned = aParams->mFrame;
  const auto* containingBlock = positioned->GetParent();
  const auto size = AnchorPositioningUtils::ResolveAnchorPosSize(
      positioned, containingBlock, {aAnchorName, *aTreeScope}, aParams->mCache);
  if (!size) {
    return false;
  }
  const auto l = [&]() {
    switch (aAnchorSizeKeyword) {
      case StyleAnchorSizeKeyword::None:
        switch (aPropAxis) {
          case StylePhysicalAxis::Horizontal:
            return size->Width();
          case StylePhysicalAxis::Vertical:
            return size->Height();
        }
        MOZ_ASSERT_UNREACHABLE("Unexpected physical axis.");
        return size->Width();
      case StyleAnchorSizeKeyword::Width:
        return size->Width();
      case StyleAnchorSizeKeyword::Height:
        return size->Height();
      case StyleAnchorSizeKeyword::Inline: {
        const auto wm = containingBlock->GetWritingMode();
        return LogicalSize{wm, *size}.ISize(wm);
      }
      case StyleAnchorSizeKeyword::Block: {
        const auto wm = containingBlock->GetWritingMode();
        return LogicalSize{wm, *size}.BSize(wm);
      }
      case StyleAnchorSizeKeyword::SelfInline: {
        const auto wm = positioned->GetWritingMode();
        return LogicalSize{wm, *size}.ISize(wm);
      }
      case StyleAnchorSizeKeyword::SelfBlock: {
        const auto wm = positioned->GetWritingMode();
        return LogicalSize{wm, *size}.BSize(wm);
      }
    }
    MOZ_ASSERT_UNREACHABLE("Unhandled anchor size keyword.");
    return size->Width();
  }();
  *aOut = Length::FromPixels(CSSPixel::FromAppUnits(l));
  return true;
}
