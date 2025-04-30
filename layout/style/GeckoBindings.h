/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* FFI functions for Servo to call into Gecko */

#ifndef mozilla_GeckoBindings_h
#define mozilla_GeckoBindings_h

#include <stdint.h>

#include "mozilla/ServoTypes.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/css/DocumentMatchingFunction.h"
#include "mozilla/css/SheetLoadData.h"
#include "mozilla/dom/Document.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/PreferenceSheet.h"
#include "nsStyleStruct.h"
#include "COLRFonts.h"

class nsAtom;
class nsIURI;
class nsSimpleContentList;
struct nsFont;
class ServoComputedData;

namespace mozilla {
class ComputedStyle;
class SeenPtrs;
class ServoElementSnapshot;
class ServoElementSnapshotTable;
class StyleSheet;
enum class PseudoStyleType : uint8_t;
enum class PointerCapabilities : uint8_t;
enum class UpdateAnimationsTasks : uint8_t;
enum class StyleColorGamut : uint8_t;
struct Keyframe;
struct StyleStylesheetContents;

namespace css {
class LoaderReusableStyleSheets;
}
namespace dom {
enum class CompositeOperationOrAuto : uint8_t;
}  // namespace dom
}  // namespace mozilla

#ifdef NIGHTLY_BUILD
const bool GECKO_IS_NIGHTLY = true;
#else
const bool GECKO_IS_NIGHTLY = false;
#endif

#define NS_DECL_THREADSAFE_FFI_REFCOUNTING(class_, name_)  \
  void Gecko_AddRef##name_##ArbitraryThread(class_* aPtr); \
  void Gecko_Release##name_##ArbitraryThread(class_* aPtr);
#define NS_IMPL_THREADSAFE_FFI_REFCOUNTING(class_, name_)                      \
  static_assert(class_::HasThreadSafeRefCnt::value,                            \
                "NS_DECL_THREADSAFE_FFI_REFCOUNTING can only be used with "    \
                "classes that have thread-safe refcounting");                  \
  void Gecko_AddRef##name_##ArbitraryThread(class_* aPtr) { NS_ADDREF(aPtr); } \
  void Gecko_Release##name_##ArbitraryThread(class_* aPtr) { NS_RELEASE(aPtr); }

extern "C" {

NS_DECL_THREADSAFE_FFI_REFCOUNTING(nsIURI, nsIURI);

// Debugging stuff.
void Gecko_Element_DebugListAttributes(const mozilla::dom::Element*,
                                       nsCString*);

void Gecko_Snapshot_DebugListAttributes(const mozilla::ServoElementSnapshot*,
                                        nsCString*);

bool Gecko_IsSignificantChild(const nsINode*, bool whitespace_is_significant);

const nsINode* Gecko_GetLastChild(const nsINode*);
const nsINode* Gecko_GetFlattenedTreeParentNode(const nsINode*);
const mozilla::dom::Element* Gecko_GetBeforeOrAfterPseudo(
    const mozilla::dom::Element*, bool is_before);
const mozilla::dom::Element* Gecko_GetMarkerPseudo(
    const mozilla::dom::Element*);

nsTArray<nsIContent*>* Gecko_GetAnonymousContentForElement(
    const mozilla::dom::Element*);
void Gecko_DestroyAnonymousContentList(nsTArray<nsIContent*>* anon_content);

const nsTArray<RefPtr<nsINode>>* Gecko_GetAssignedNodes(
    const mozilla::dom::Element*);

void Gecko_GetQueryContainerSize(const mozilla::dom::Element*,
                                 nscoord* aOutWidth, nscoord* aOutHeight);

void Gecko_ComputedStyle_Init(mozilla::ComputedStyle* context,
                              const ServoComputedData* values,
                              mozilla::PseudoStyleType pseudo_type);

void Gecko_ComputedStyle_Destroy(mozilla::ComputedStyle* context);

// By default, Servo walks the DOM by traversing the siblings of the DOM-view
// first child. This generally works, but misses anonymous children, which we
// want to traverse during styling. To support these cases, we create an
// optional stack-allocated iterator in aIterator for nodes that need it.
void Gecko_ConstructStyleChildrenIterator(const mozilla::dom::Element*,
                                          mozilla::dom::StyleChildrenIterator*);

void Gecko_DestroyStyleChildrenIterator(mozilla::dom::StyleChildrenIterator*);

const nsINode* Gecko_GetNextStyleChild(mozilla::dom::StyleChildrenIterator*);

nsAtom* Gecko_Element_ImportedPart(const nsAttrValue*, nsAtom*);
nsAtom** Gecko_Element_ExportedParts(const nsAttrValue*, nsAtom*,
                                     size_t* aOutLength);

NS_DECL_THREADSAFE_FFI_REFCOUNTING(mozilla::css::SheetLoadDataHolder,
                                   SheetLoadDataHolder);

void Gecko_StyleSheet_FinishAsyncParse(
    mozilla::css::SheetLoadDataHolder* data,
    mozilla::StyleStrong<mozilla::StyleStylesheetContents> sheet_contents,
    mozilla::StyleUseCounters* use_counters);

mozilla::StyleSheet* Gecko_LoadStyleSheet(
    mozilla::css::Loader* loader, mozilla::StyleSheet* parent,
    mozilla::css::SheetLoadData* parent_load_data,
    mozilla::css::LoaderReusableStyleSheets* reusable_sheets,
    const mozilla::StyleCssUrl* url,
    mozilla::StyleStrong<mozilla::StyleLockedMediaList> media_list);

void Gecko_LoadStyleSheetAsync(
    mozilla::css::SheetLoadDataHolder* parent_data,
    const mozilla::StyleCssUrl* url,
    mozilla::StyleStrong<mozilla::StyleLockedMediaList>,
    mozilla::StyleStrong<mozilla::StyleLockedImportRule>);

// Selector Matching.
uint64_t Gecko_ElementState(const mozilla::dom::Element*);
bool Gecko_IsRootElement(const mozilla::dom::Element*);

bool Gecko_MatchLang(const mozilla::dom::Element*, nsAtom* override_lang,
                     bool has_override_lang, const char16_t* value);

nsAtom* Gecko_GetXMLLangValue(const mozilla::dom::Element*);

const mozilla::PreferenceSheet::Prefs* Gecko_GetPrefSheetPrefs(
    const mozilla::dom::Document*);

bool Gecko_IsTableBorderNonzero(const mozilla::dom::Element* element);
bool Gecko_IsSelectListBox(const mozilla::dom::Element* element);

// Attributes.
#define SERVO_DECLARE_ELEMENT_ATTR_MATCHING_FUNCTIONS(prefix_, implementor_) \
  nsAtom* prefix_##LangValue(implementor_ element);

bool Gecko_AttrEquals(const nsAttrValue*, const nsAtom*, bool aIgnoreCase);
bool Gecko_AttrDashEquals(const nsAttrValue*, const nsAtom*, bool aIgnoreCase);
bool Gecko_AttrIncludes(const nsAttrValue*, const nsAtom*, bool aIgnoreCase);
bool Gecko_AttrHasSubstring(const nsAttrValue*, const nsAtom*,
                            bool aIgnoreCase);
bool Gecko_AttrHasPrefix(const nsAttrValue*, const nsAtom*, bool aIgnoreCase);
bool Gecko_AttrHasSuffix(const nsAttrValue*, const nsAtom*, bool aIgnoreCase);

bool Gecko_AssertClassAttrValueIsSane(const nsAttrValue*);
const nsAttrValue* Gecko_GetSVGAnimatedClass(const mozilla::dom::Element*);

SERVO_DECLARE_ELEMENT_ATTR_MATCHING_FUNCTIONS(Gecko_,
                                              const mozilla::dom::Element*)

SERVO_DECLARE_ELEMENT_ATTR_MATCHING_FUNCTIONS(
    Gecko_Snapshot, const mozilla::ServoElementSnapshot*)

#undef SERVO_DECLARE_ELEMENT_ATTR_MATCHING_FUNCTIONS

// Style attributes.
const mozilla::StyleLockedDeclarationBlock* Gecko_GetStyleAttrDeclarationBlock(
    const mozilla::dom::Element* element);

void Gecko_UnsetDirtyStyleAttr(const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock* Gecko_GetViewTransitionDynamicRule(
    const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock*
Gecko_GetHTMLPresentationAttrDeclarationBlock(
    const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock*
Gecko_GetExtraContentStyleDeclarations(const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock*
Gecko_GetUnvisitedLinkAttrDeclarationBlock(
    const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock*
Gecko_GetVisitedLinkAttrDeclarationBlock(const mozilla::dom::Element* element);

const mozilla::StyleLockedDeclarationBlock*
Gecko_GetActiveLinkAttrDeclarationBlock(const mozilla::dom::Element* element);

// Visited handling.

// Returns whether visited styles are enabled for a given document.
bool Gecko_VisitedStylesEnabled(const mozilla::dom::Document*);

// Animations
bool Gecko_GetAnimationRule(
    const mozilla::dom::Element* aElementOrPseudo,
    mozilla::EffectCompositor::CascadeLevel aCascadeLevel,
    mozilla::StyleAnimationValueMap* aAnimationValues);

bool Gecko_StyleAnimationsEquals(
    const nsStyleAutoArray<mozilla::StyleAnimation>*,
    const nsStyleAutoArray<mozilla::StyleAnimation>*);

bool Gecko_StyleScrollTimelinesEquals(
    const nsStyleAutoArray<mozilla::StyleScrollTimeline>*,
    const nsStyleAutoArray<mozilla::StyleScrollTimeline>*);

bool Gecko_StyleViewTimelinesEquals(
    const nsStyleAutoArray<mozilla::StyleViewTimeline>*,
    const nsStyleAutoArray<mozilla::StyleViewTimeline>*);

void Gecko_UpdateAnimations(const mozilla::dom::Element* aElementOrPseudo,
                            const mozilla::ComputedStyle* aOldComputedValues,
                            const mozilla::ComputedStyle* aComputedValues,
                            mozilla::UpdateAnimationsTasks aTasks);

size_t Gecko_GetAnimationEffectCount(
    const mozilla::dom::Element* aElementOrPseudo);
bool Gecko_ElementHasAnimations(const mozilla::dom::Element* aElementOrPseudo);
bool Gecko_ElementHasCSSAnimations(
    const mozilla::dom::Element* aElementOrPseudo);
bool Gecko_ElementHasCSSTransitions(
    const mozilla::dom::Element* aElementOrPseudo);
bool Gecko_ElementHasWebAnimations(
    const mozilla::dom::Element* aElementOrPseudo);
size_t Gecko_ElementTransitions_Length(
    const mozilla::dom::Element* aElementOrPseudo);

nsCSSPropertyID Gecko_ElementTransitions_PropertyAt(
    const mozilla::dom::Element* aElementOrPseudo, size_t aIndex);

const mozilla::StyleAnimationValue* Gecko_ElementTransitions_EndValueAt(
    const mozilla::dom::Element* aElementOrPseudo, size_t aIndex);

double Gecko_GetProgressFromComputedTiming(const mozilla::ComputedTiming*);

double Gecko_GetPositionInSegment(const mozilla::AnimationPropertySegment*,
                                  double aProgress, bool aBeforeFlag);

// Get servo's AnimationValue for |aProperty| from the cached base style
// |aBaseStyles|.
// |aBaseStyles| is nsRefPtrHashtable<nsGenericHashKey<AnimatedPropertyID>,
// StyleAnimationValue>.
// We use RawServoAnimationValueTableBorrowed to avoid exposing
// nsRefPtrHashtable in FFI.
const mozilla::StyleAnimationValue* Gecko_AnimationGetBaseStyle(
    const RawServoAnimationValueTable* aBaseStyles,
    const mozilla::AnimatedPropertyID* aProperty);

void Gecko_StyleTransition_SetUnsupportedProperty(
    mozilla::StyleTransition* aTransition, nsAtom* aAtom);

// Atoms.
nsAtom* Gecko_Atomize(const char* aString, uint32_t aLength);
nsAtom* Gecko_Atomize16(const nsAString* aString);
void Gecko_AddRefAtom(nsAtom* aAtom);
void Gecko_ReleaseAtom(nsAtom* aAtom);

// will not run destructors on dst, give it uninitialized memory
// font_id is LookAndFeel::FontID
void Gecko_nsFont_InitSystem(nsFont* dst, mozilla::StyleSystemFont font_id,
                             const nsStyleFont* font,
                             const mozilla::dom::Document*);

void Gecko_nsFont_Destroy(nsFont* dst);

// The gfxFontFeatureValueSet returned from this function has zero reference.
gfxFontFeatureValueSet* Gecko_ConstructFontFeatureValueSet();

nsTArray<uint32_t>* Gecko_AppendFeatureValueHashEntry(
    gfxFontFeatureValueSet* value_set, nsAtom* family, uint32_t alternate,
    nsAtom* name);

// Font variant alternates
void Gecko_ClearAlternateValues(nsFont* font, size_t length);

void Gecko_AppendAlternateValues(nsFont* font, uint32_t alternate_name,
                                 nsAtom* atom);

void Gecko_CopyAlternateValuesFrom(nsFont* dest, const nsFont* src);

// The FontPaletteValueSet returned from this function has zero reference.
mozilla::gfx::FontPaletteValueSet* Gecko_ConstructFontPaletteValueSet();

mozilla::gfx::FontPaletteValueSet::PaletteValues*
Gecko_AppendPaletteValueHashEntry(
    mozilla::gfx::FontPaletteValueSet* aPaletteValueSet, nsAtom* aFamily,
    nsAtom* aName);

void Gecko_SetFontPaletteBase(
    mozilla::gfx::FontPaletteValueSet::PaletteValues* aValues,
    int32_t aBasePaletteIndex);

void Gecko_SetFontPaletteOverride(
    mozilla::gfx::FontPaletteValueSet::PaletteValues* aValues, int32_t aIndex,
    mozilla::StyleAbsoluteColor* aColor);

// Visibility style
void Gecko_SetImageOrientation(nsStyleVisibility* aVisibility,
                               uint8_t aOrientation, bool aFlip);

void Gecko_SetImageOrientationAsFromImage(nsStyleVisibility* aVisibility);

void Gecko_CopyImageOrientationFrom(nsStyleVisibility* aDst,
                                    const nsStyleVisibility* aSrc);

// list-style-image style.
void Gecko_SetListStyleImageNone(nsStyleList* style_struct);

void Gecko_SetListStyleImageImageValue(nsStyleList* style_struct,
                                       const mozilla::StyleComputedUrl* url);

void Gecko_CopyListStyleImageFrom(nsStyleList* dest, const nsStyleList* src);

// Dirtiness tracking.
void Gecko_NoteDirtyElement(const mozilla::dom::Element*);
void Gecko_NoteDirtySubtreeForInvalidation(const mozilla::dom::Element*);
void Gecko_NoteAnimationOnlyDirtyElement(const mozilla::dom::Element*);

bool Gecko_AnimationNameMayBeReferencedFromStyle(const nsPresContext*,
                                                 nsAtom* name);

float Gecko_GetScrollbarInlineSize(const nsPresContext*);

// Retrive pseudo type from an element.
mozilla::PseudoStyleType Gecko_GetImplementedPseudoType(
    const mozilla::dom::Element*);
// Retrive pseudo identifier from an element if any.
nsAtom* Gecko_GetImplementedPseudoIdentifier(const mozilla::dom::Element*);

// We'd like to return `nsChangeHint` here, but bindgen bitfield enums don't
// work as return values with the Linux 32-bit ABI at the moment because
// they wrap the value in a struct.
uint32_t Gecko_CalcStyleDifference(const mozilla::ComputedStyle* old_style,
                                   const mozilla::ComputedStyle* new_style,
                                   bool* any_style_struct_changed,
                                   bool* reset_only_changed);

nscoord Gecko_CalcLineHeight(const mozilla::StyleLineHeight*,
                             const nsPresContext*, bool aVertical,
                             const nsStyleFont* aAgainstFont,
                             const mozilla::dom::Element* aElement);

// Get an element snapshot for a given element from the table.
const mozilla::ServoElementSnapshot* Gecko_GetElementSnapshot(
    const mozilla::ServoElementSnapshotTable* table,
    const mozilla::dom::Element*);

// Have we seen this pointer before?
bool Gecko_HaveSeenPtr(mozilla::SeenPtrs* table, const void* ptr);

void Gecko_EnsureImageLayersLength(nsStyleImageLayers* layers, size_t len,
                                   nsStyleImageLayers::LayerType layer_type);

void Gecko_EnsureStyleAnimationArrayLength(void* array, size_t len);
void Gecko_EnsureStyleTransitionArrayLength(void* array, size_t len);
void Gecko_EnsureStyleScrollTimelineArrayLength(void* array, size_t len);
void Gecko_EnsureStyleViewTimelineArrayLength(void* array, size_t len);

// Searches from the beginning of |keyframes| for a Keyframe object with the
// specified offset and timing function. If none is found, a new Keyframe object
// with the specified |offset| and |timingFunction| will be prepended to
// |keyframes|.
//
// @param keyframes  An array of Keyframe objects, sorted by offset.
//                   The first Keyframe in the array, if any, MUST have an
//                   offset greater than or equal to |offset|.
// @param offset  The offset to search for, or, if no suitable Keyframe is
//                found, the offset to use for the created Keyframe.
//                Must be a floating point number in the range [0.0, 1.0].
// @param timingFunction  The timing function to match, or, if no suitable
//                        Keyframe is found, to set on the created Keyframe.
// @param composition  The composition to match, or, if no suitable Keyframe is
//                     found, to set on the created Keyframe.
//
// @returns  The matching or created Keyframe.
mozilla::Keyframe* Gecko_GetOrCreateKeyframeAtStart(
    nsTArray<mozilla::Keyframe>* keyframes, float offset,
    const mozilla::StyleComputedTimingFunction* timingFunction,
    const mozilla::dom::CompositeOperationOrAuto composition);

// As with Gecko_GetOrCreateKeyframeAtStart except that this method will search
// from the beginning of |keyframes| for a Keyframe with matching timing
// function, composition, and an offset of 0.0.
// Furthermore, if a matching Keyframe is not found, a new Keyframe will be
// inserted after the *last* Keyframe in |keyframes| with offset 0.0.
mozilla::Keyframe* Gecko_GetOrCreateInitialKeyframe(
    nsTArray<mozilla::Keyframe>* keyframes,
    const mozilla::StyleComputedTimingFunction* timingFunction,
    const mozilla::dom::CompositeOperationOrAuto composition);

// As with Gecko_GetOrCreateKeyframeAtStart except that this method will search
// from the *end* of |keyframes| for a Keyframe with matching timing function,
// composition, and an offset of 1.0. If a matching Keyframe is not found, a new
// Keyframe will be appended to the end of |keyframes|.
mozilla::Keyframe* Gecko_GetOrCreateFinalKeyframe(
    nsTArray<mozilla::Keyframe>* keyframes,
    const mozilla::StyleComputedTimingFunction* timingFunction,
    const mozilla::dom::CompositeOperationOrAuto composition);

void Gecko_ResetFilters(nsStyleEffects* effects, size_t new_len);

void Gecko_CopyFiltersFrom(nsStyleEffects* aSrc, nsStyleEffects* aDest);

void Gecko_nsStyleSVG_SetDashArrayLength(nsStyleSVG* svg, uint32_t len);

void Gecko_nsStyleSVG_CopyDashArray(nsStyleSVG* dst, const nsStyleSVG* src);

void Gecko_nsStyleSVG_SetContextPropertiesLength(nsStyleSVG* svg, uint32_t len);

void Gecko_nsStyleSVG_CopyContextProperties(nsStyleSVG* dst,
                                            const nsStyleSVG* src);

void Gecko_GetComputedURLSpec(const mozilla::StyleComputedUrl* url,
                              nsCString* spec);

// Return true if the given image MIME type is supported
bool Gecko_IsSupportedImageMimeType(const uint8_t* mime_type,
                                    const uint32_t len);

void Gecko_nsIURI_Debug(nsIURI*, nsCString* spec);

void Gecko_nsIReferrerInfo_Debug(nsIReferrerInfo* aReferrerInfo,
                                 nsCString* aOut);

NS_DECL_THREADSAFE_FFI_REFCOUNTING(mozilla::URLExtraData, URLExtraData);
NS_DECL_THREADSAFE_FFI_REFCOUNTING(nsIReferrerInfo, nsIReferrerInfo);

void Gecko_FillAllImageLayers(nsStyleImageLayers* layers, uint32_t max_len);

void Gecko_LoadData_Drop(mozilla::StyleLoadData*);

void Gecko_nsStyleFont_SetLang(nsStyleFont* font, nsAtom* atom);

void Gecko_nsStyleFont_CopyLangFrom(nsStyleFont* aFont,
                                    const nsStyleFont* aSource);

mozilla::Length Gecko_nsStyleFont_ComputeMinSize(const nsStyleFont*,
                                                 const mozilla::dom::Document*);

// Computes the default generic font for a language.
mozilla::StyleGenericFontFamily
Gecko_nsStyleFont_ComputeFallbackFontTypeForLanguage(
    const mozilla::dom::Document*, nsAtom* language);

mozilla::Length Gecko_GetBaseSize(const mozilla::dom::Document*,
                                  nsAtom* language,
                                  mozilla::StyleGenericFontFamily);

struct GeckoFontMetrics {
  mozilla::Length mXSize;
  mozilla::Length mChSize;     // negatives indicate not found.
  mozilla::Length mCapHeight;  // negatives indicate not found.
  mozilla::Length mIcWidth;    // negatives indicate not found.
  mozilla::Length mAscent;
  mozilla::Length mComputedEmSize;
  float mScriptPercentScaleDown;        // zero is invalid or means not found.
  float mScriptScriptPercentScaleDown;  // zero is invalid or means not found.
};

GeckoFontMetrics Gecko_GetFontMetrics(
    const nsPresContext*, bool is_vertical, const nsStyleFont* font,
    mozilla::Length font_size, mozilla::StyleQueryFontMetricsFlags flags);

mozilla::StyleSheet* Gecko_StyleSheet_Clone(const mozilla::StyleSheet* aSheet);

void Gecko_StyleSheet_AddRef(const mozilla::StyleSheet* aSheet);
void Gecko_StyleSheet_Release(const mozilla::StyleSheet* aSheet);

struct GeckoImplicitScopeRoot {
  const mozilla::dom::Element* mHost;
  const mozilla::dom::Element* mRoot;
  bool mConstructed;
};
GeckoImplicitScopeRoot Gecko_StyleSheet_ImplicitScopeRoot(
    const mozilla::StyleSheet* aSheet);

bool Gecko_IsDocumentBody(const mozilla::dom::Element* element);

bool Gecko_IsDarkColorScheme(const mozilla::dom::Document*,
                             const mozilla::StyleColorSchemeFlags*);
nscolor Gecko_ComputeSystemColor(mozilla::StyleSystemColor,
                                 const mozilla::dom::Document*,
                                 const mozilla::StyleColorSchemeFlags*);

// We use an int32_t here instead of a LookAndFeel::IntID/FloatID because
// forward-declaring a nested enum/struct is impossible.
int32_t Gecko_GetLookAndFeelInt(int32_t int_id);
float Gecko_GetLookAndFeelFloat(int32_t float_id);

void Gecko_AddPropertyToSet(nsCSSPropertyIDSet*, nsCSSPropertyID);

// Style-struct management.
#define STYLE_STRUCT(name)                                                   \
  void Gecko_Construct_Default_nsStyle##name(nsStyle##name* ptr,             \
                                             const mozilla::dom::Document*); \
  void Gecko_CopyConstruct_nsStyle##name(nsStyle##name* ptr,                 \
                                         const nsStyle##name* other);        \
  void Gecko_Destroy_nsStyle##name(nsStyle##name* ptr);
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

bool Gecko_DocumentRule_UseForPresentation(
    const mozilla::dom::Document*, const nsACString* aPattern,
    mozilla::css::DocumentMatchingFunction);

// Allocator hinting.
void Gecko_SetJemallocThreadLocalArena(bool enabled);

// Pseudo-element flags.
#define CSS_PSEUDO_ELEMENT(name_, value_, flags_) \
  const uint32_t SERVO_CSS_PSEUDO_ELEMENT_FLAGS_##name_ = flags_;
#include "nsCSSPseudoElementList.h"
#undef CSS_PSEUDO_ELEMENT

bool Gecko_ErrorReportingEnabled(const mozilla::StyleSheet* sheet,
                                 const mozilla::css::Loader* loader,
                                 uint64_t* aOutWindowId);

void Gecko_ReportUnexpectedCSSError(
    uint64_t windowId, nsIURI* uri, const char* message, const char* param,
    uint32_t paramLen, const char* prefix, const char* prefixParam,
    uint32_t prefixParamLen, const char* suffix, const char* selectors,
    uint32_t selectorsLen, uint32_t lineNumber, uint32_t colNumber);

// DOM APIs.
void Gecko_ContentList_AppendAll(nsSimpleContentList* aContentList,
                                 const mozilla::dom::Element** aElements,
                                 size_t aLength);

// FIXME(emilio): These two below should be a single function that takes a
// `const DocumentOrShadowRoot*`, but that doesn't make MSVC builds happy for a
// reason I haven't really dug into.
const nsTArray<mozilla::dom::Element*>* Gecko_Document_GetElementsWithId(
    const mozilla::dom::Document*, nsAtom* aId);

const nsTArray<mozilla::dom::Element*>* Gecko_ShadowRoot_GetElementsWithId(
    const mozilla::dom::ShadowRoot*, nsAtom* aId);

bool Gecko_EvalMozPrefFeature(nsAtom*,
                              const mozilla::StyleComputedMozPrefFeatureValue*);

// Check whether font format/tech is supported.
bool Gecko_IsFontFormatSupported(
    mozilla::StyleFontFaceSourceFormatKeyword aFormat);
bool Gecko_IsFontTechSupported(mozilla::StyleFontFaceSourceTechFlags aFlag);

bool Gecko_IsKnownIconFontFamily(const nsAtom* aFamilyName);

// Returns true if we're currently performing the servo traversal.
bool Gecko_IsInServoTraversal();

// Returns true if we're currently on the main thread.
bool Gecko_IsMainThread();

// Returns true if we're currently on a DOM worker thread.
bool Gecko_IsDOMWorkerThread();

// Returns the preferred number of style threads to use, or -1 for no
// preference.
int32_t Gecko_GetNumStyleThreads();

// Media feature helpers.
//
// Defined in nsMediaFeatures.cpp.
mozilla::StyleDisplayMode Gecko_MediaFeatures_GetDisplayMode(
    const mozilla::dom::Document*);

bool Gecko_MediaFeatures_UseOverlayScrollbars(const mozilla::dom::Document*);
int32_t Gecko_MediaFeatures_GetColorDepth(const mozilla::dom::Document*);
int32_t Gecko_MediaFeatures_GetMonochromeBitsPerPixel(
    const mozilla::dom::Document*);
mozilla::StyleColorGamut Gecko_MediaFeatures_ColorGamut(
    const mozilla::dom::Document*);

void Gecko_MediaFeatures_GetDeviceSize(const mozilla::dom::Document*,
                                       nscoord* width, nscoord* height);

float Gecko_MediaFeatures_GetResolution(const mozilla::dom::Document*);
bool Gecko_MediaFeatures_PrefersReducedMotion(const mozilla::dom::Document*);
bool Gecko_MediaFeatures_PrefersReducedTransparency(
    const mozilla::dom::Document*);
mozilla::StylePrefersContrast Gecko_MediaFeatures_PrefersContrast(
    const mozilla::dom::Document*);
mozilla::StylePrefersColorScheme Gecko_MediaFeatures_PrefersColorScheme(
    const mozilla::dom::Document*, bool aUseContent);
bool Gecko_MediaFeatures_InvertedColors(const mozilla::dom::Document*);
mozilla::StyleScripting Gecko_MediaFeatures_Scripting(
    const mozilla::dom::Document*);

mozilla::StyleDynamicRange Gecko_MediaFeatures_DynamicRange(
    const mozilla::dom::Document*);
mozilla::StyleDynamicRange Gecko_MediaFeatures_VideoDynamicRange(
    const mozilla::dom::Document*);

mozilla::PointerCapabilities Gecko_MediaFeatures_PrimaryPointerCapabilities(
    const mozilla::dom::Document*);

mozilla::PointerCapabilities Gecko_MediaFeatures_AllPointerCapabilities(
    const mozilla::dom::Document*);

float Gecko_MediaFeatures_GetDevicePixelRatio(const mozilla::dom::Document*);

bool Gecko_MediaFeatures_IsResourceDocument(const mozilla::dom::Document*);
bool Gecko_MediaFeatures_MatchesPlatform(mozilla::StylePlatform);
mozilla::StyleGtkThemeFamily Gecko_MediaFeatures_GtkThemeFamily();

void Gecko_GetSafeAreaInsets(const nsPresContext*, float*, float*, float*,
                             float*);

void Gecko_PrintfStderr(const nsCString*);

}  // extern "C"

#endif  // mozilla_GeckoBindings_h
