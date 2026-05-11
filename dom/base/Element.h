/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes; this provides an implementation
 * of DOM Core's Element, implements nsIContent, provides
 * utility methods for subclasses, and so forth.
 */

#ifndef mozilla_dom_Element_h_
#define mozilla_dom_Element_h_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "AttrArray.h"
#include "ErrorList.h"
#include "Units.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/CORSMode.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/FlushType.h"
#include "mozilla/Maybe.h"
#include "mozilla/PseudoStyleType.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/RustCell.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BorrowedAttrInfo.h"
#include "mozilla/dom/DOMString.h"
#include "mozilla/dom/DOMTokenListSupportedTokens.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/RustTypes.h"
#include "mozilla/dom/ShadowRootBindingFwd.h"
#include "nsAtom.h"
#include "nsAttrValue.h"
#include "nsAttrValueInlines.h"
#include "nsCaseTreatment.h"
#include "nsChangeHint.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsHashKeys.h"
#include "nsLiteralString.h"
#include "nsRect.h"
#include "nsTHashMap.h"
#include "nsTLiteralString.h"
#include "nscore.h"

class JSObject;
class mozAutoDocUpdate;
class nsAttrName;
class nsAttrValueOrString;
class nsDOMAttributeMap;
class nsDOMCSSAttributeDeclaration;
class nsDOMStringMap;
class nsDOMTokenList;
class nsFocusManager;
class nsGenericHTMLElement;
class nsGenericHTMLFormControlElementWithState;
class nsGlobalWindowInner;
class nsGlobalWindowOuter;
class nsImageLoadingContent;
class nsIAutoCompletePopup;
class nsIBrowser;
class nsIDOMXULButtonElement;
class nsIDOMXULContainerElement;
class nsIDOMXULContainerItemElement;
class nsIDOMXULControlElement;
class nsIDOMXULMenuListElement;
class nsIDOMXULMultiSelectControlElement;
class nsIDOMXULRadioGroupElement;
class nsIDOMXULRelatedElement;
class nsIDOMXULSelectControlElement;
class nsIDOMXULSelectControlItemElement;
class nsIFrame;
class nsIPrincipal;
class nsIScreen;
class nsIURI;
class nsObjectLoadingContent;
class nsPresContext;
class nsWindowSizes;
struct JSContext;
struct ServoNodeData;
template <class E>
class nsTArray;
template <class T>
class nsGetterAddRefs;

namespace mozilla {
class DeclarationBlock;
class MappedDeclarationsBuilder;
class EditorBase;
class ErrorResult;
class OOMReporter;
class ScrollContainerFrame;
class SMILAttr;
struct MutationClosureData;
class TextEditor;
namespace css {
struct URLValue;
}  // namespace css
namespace dom {
struct CheckVisibilityOptions;
struct CustomElementData;
struct SetHTMLUnsafeOptions;
struct SetHTMLOptions;
struct GetHTMLOptions;
struct GetAnimationsOptions;
struct ScrollIntoViewOptions;
struct ScrollToOptions;
struct FocusOptions;
struct ShadowRootInit;
struct ScrollOptions;
struct FullscreenOptions;
struct PointerLockOptions;
class Attr;
class BooleanOrScrollIntoViewOptions;
class ContentList;
class Document;
class HTMLFormElement;
class DOMIntersectionObserver;
class DOMMatrixReadOnly;
class Element;
class ElementOrCSSPseudoElement;
class PopoverData;
class Promise;
class Sanitizer;
class ShadowRoot;
class StylePropertyMapReadOnly;
class TrustedHTMLOrString;
class UnrestrictedDoubleOrKeyframeAnimationOptions;
template <typename T>
class Optional;
enum class CallerType : uint32_t;
enum class ReferrerPolicy : uint8_t;
enum class FetchPriority : uint8_t;
enum class PopoverAttributeState : uint8_t;
enum class ShadowRootMode : uint8_t;
}  // namespace dom
}  // namespace mozilla

using nsMapRuleToAttributesFunc = void (*)(mozilla::MappedDeclarationsBuilder&);

// Declared here because of include hell.
extern "C" bool Servo_Element_IsDisplayContents(const mozilla::dom::Element*);

already_AddRefed<mozilla::dom::ContentList> NS_GetContentList(
    nsINode* aRootNode, int32_t aMatchNameSpaceId, const nsAString& aTagname);

#define ELEMENT_FLAG_BIT(n_) \
  NODE_FLAG_BIT(NODE_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// Element-specific flags
enum : uint32_t {
  // Whether this node has dirty descendants for Servo's style system.
  ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO = ELEMENT_FLAG_BIT(0),
  // Whether this node has dirty descendants for animation-only restyle for
  // Servo's style system.
  ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO = ELEMENT_FLAG_BIT(1),

  // Whether the element has been snapshotted due to attribute or state changes
  // by the Servo restyle manager.
  ELEMENT_HAS_SNAPSHOT = ELEMENT_FLAG_BIT(2),

  // Whether the element has already handled its relevant snapshot.
  //
  // Used by the servo restyle process in order to accurately track whether the
  // style of an element is up-to-date, even during the same restyle process.
  ELEMENT_HANDLED_SNAPSHOT = ELEMENT_FLAG_BIT(3),

  // If this flag is set on an element, that means that it is a HTML datalist
  // element or has a HTML datalist element ancestor.
  ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR = ELEMENT_FLAG_BIT(4),

  // If this flag is set on an element, that means this element
  // has been considered by our LargestContentfulPaint algorithm and
  // it's not going to be considered again.
  ELEMENT_PROCESSED_BY_LCP_FOR_TEXT = ELEMENT_FLAG_BIT(5),

  // If this flag is set on an element, this means the HTML parser encountered
  // a duplicate attribute error:
  // https://html.spec.whatwg.org/multipage/parsing.html#parse-error-duplicate-attribute
  // This flag is used for detecting dangling markup attacks in the CSP
  // algorithm https://w3c.github.io/webappsec-csp/#is-element-nonceable.
  ELEMENT_PARSER_HAD_DUPLICATE_ATTR_ERROR = ELEMENT_FLAG_BIT(6),

  // If this flag is set, this element is in
  // Document::mContentIdentifiersForLCP.
  ELEMENT_IN_CONTENT_IDENTIFIER_FOR_LCP = ELEMENT_FLAG_BIT(7),

  // 2-bit field encoding the element's custom element registry state.
  // See CustomElementRegistryState for the possible values.
  ELEMENT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT = ELEMENT_FLAG_BIT(8),
  ELEMENT_CUSTOM_ELEMENT_REGISTRY_MASK =
      ELEMENT_FLAG_BIT(8) | ELEMENT_FLAG_BIT(9),

  // Whether this element has an associated EditContext
  // https://w3c.github.io/edit-context
  ELEMENT_HAS_EDIT_CONTEXT = ELEMENT_FLAG_BIT(10),

  // Remaining bits are for subclasses
  ELEMENT_TYPE_SPECIFIC_BITS_OFFSET = NODE_TYPE_SPECIFIC_BITS_OFFSET + 11
};

#undef ELEMENT_FLAG_BIT

// Make sure we have space for our bits
ASSERT_NODE_FLAGS_SPACE(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET);

// Encodes the custom element registry state for an element or shadow root.
//   Global:  uses the owner document's effective global registry (initial
//   state).
//   Null:    explicitly opted out of all registries.
//   Scoped:  uses a scoped registry stored in gScopedRegistryMap.
enum class CustomElementRegistryState : uint8_t {
  Global = 0,
  Null = 1,
  Scoped = 2,
};

namespace mozilla {
enum class PseudoStyleType : uint8_t;
struct PseudoStyleRequest;
class EventChainPostVisitor;
class EventChainPreVisitor;
class EventChainVisitor;
class EventListenerManager;
class EventStateManager;

enum class ContentEditableState {
  Inherit,
  False,
  True,
  PlainTextOnly,
};

namespace dom {

struct CustomElementDefinition;
class Animation;
class CustomElementRegistry;
class Link;
class DOMRect;
class DOMRectList;
class Flex;
class Grid;
class OwningTrustedHTMLOrNullIsEmptyString;
class TrustedHTML;
class TrustedHTMLOrNullIsEmptyString;
class TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString;

// IID for the dom::Element interface
#define NS_ELEMENT_IID \
  {0xc67ed254, 0xfd3b, 0x4b10, {0x96, 0xa2, 0xc5, 0x8b, 0x7b, 0x64, 0x97, 0xd1}}

#define REFLECT_NULLABLE_DOMSTRING_ATTR(method, attr)            \
  void Get##method(nsAString& aValue) const {                    \
    const nsAttrValue* val = mAttrs.GetAttr(nsGkAtoms::attr);    \
    if (!val) {                                                  \
      SetDOMStringToNull(aValue);                                \
      return;                                                    \
    }                                                            \
    val->ToString(aValue);                                       \
  }                                                              \
  void Set##method(const nsAString& aValue, ErrorResult& aRv) {  \
    SetOrRemoveNullableStringAttr(nsGkAtoms::attr, aValue, aRv); \
  }

#define REFLECT_NULLABLE_ELEMENT_ATTR(method, attr)              \
  Element* Get##method() const {                                 \
    return GetAttrAssociatedElementForBindings(nsGkAtoms::attr); \
  }                                                              \
                                                                 \
  void Set##method(Element* aElement) {                          \
    ExplicitlySetAttrElement(nsGkAtoms::attr, aElement);         \
  }

#define REFLECT_NULLABLE_ELEMENTS_ATTR(method, attr)                       \
  void Get##method(bool* aUseCachedValue,                                  \
                   Nullable<nsTArray<RefPtr<Element>>>& aElements) {       \
    GetAttrAssociatedElementsForBindings(nsGkAtoms::attr, aUseCachedValue, \
                                         aElements);                       \
  }                                                                        \
                                                                           \
  void Set##method(                                                        \
      const Nullable<Sequence<OwningNonNull<Element>>>& aElements) {       \
    ExplicitlySetAttrElements(nsGkAtoms::attr, aElements);                 \
  }

class Element : public FragmentOrElement {
 public:
#ifdef MOZILLA_INTERNAL_API
  explicit Element(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : FragmentOrElement(std::move(aNodeInfo)),
        mState(ElementState::READONLY | ElementState::DEFINED |
               ElementState::LTR) {
    MOZ_ASSERT(mNodeInfo->NodeType() == ELEMENT_NODE,
               "Bad NodeType in aNodeInfo");
    SetIsElement();
  }

  ~Element() {
    NS_ASSERTION(!HasServoData(), "expected ServoData to be cleared earlier");
    UnlinkCustomElementRegistry(this);
  }

#endif  // MOZILLA_INTERNAL_API

  NS_INLINE_DECL_STATIC_IID(NS_ELEMENT_IID)

  NS_DECL_ADDSIZEOFEXCLUDINGTHIS

  NS_IMPL_FROMNODE_HELPER(Element, IsElement())

  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;

  /**
   * Method to get the full state of this element. See dom/base/rust/lib.rs for
   * the possible bits that could be set here.
   */
  ElementState State() const { return mState; }

  /**
   * Returns the current disabled state of the element.
   */
  bool IsDisabled() const { return State().HasState(ElementState::DISABLED); }
  bool IsReadOnly() const { return State().HasState(ElementState::READONLY); }
  bool IsDisabledOrReadOnly() const {
    return State().HasAtLeastOneOfStates(ElementState::DISABLED |
                                         ElementState::READONLY);
  }

  /**
   * Return true if this element has contenteditable="plaintext-only".
   */
  [[nodiscard]] inline bool IsContentEditablePlainTextOnly() const;

  virtual int32_t TabIndexDefault() { return -1; }

  /**
   * Get tabIndex of this element. If not found, return TabIndexDefault.
   */
  int32_t TabIndex();

  /**
   * Get the parsed value of tabindex attribute.
   */
  Maybe<int32_t> GetTabIndexAttrValue();

  /**
   * Set tabIndex value to this element.
   */
  void SetTabIndex(int32_t aTabIndex, mozilla::ErrorResult& aError);

  /**
   * Sets the ShadowRoot binding for this element. The contents of the
   * binding is rendered in place of this node's children.
   *
   * @param aShadowRoot The ShadowRoot to be bound to this element.
   */
  void SetShadowRoot(ShadowRoot* aShadowRoot);

  void SetLastRememberedBSize(float aBSize);
  void SetLastRememberedISize(float aISize);
  void RemoveLastRememberedBSize();
  void RemoveLastRememberedISize();

  /**
   * Make focus on this element.
   */
  // TODO: Convert Focus() to MOZ_CAN_RUN_SCRIPT and get rid of the
  //       kungFuDeathGrip in it.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void Focus(const FocusOptions& aOptions,
                                                 const CallerType aCallerType,
                                                 ErrorResult& aError);

  /**
   * Show blur and clear focus.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void Blur(mozilla::ErrorResult& aError);

  /**
   * The style state of this element. This is the real state of the element
   * with any style locks applied for pseudo-class inspecting.
   */
  ElementState StyleState() const {
    if (!HasLockedStyleStates()) {
      return mState;
    }
    return StyleStateFromLocks();
  }

  /**
   * StyleStateLocks is used to specify which event states should be locked,
   * and whether they should be locked to on or off.
   */
  struct StyleStateLocks {
    // mLocks tracks which event states should be locked.
    ElementState mLocks;
    // mValues tracks if the locked state should be on or off.
    ElementState mValues;
  };

  /**
   * The style state locks applied to this element.
   */
  StyleStateLocks LockedStyleStates() const;

  /**
   * Add a style state lock on this element.
   * aEnabled is the value to lock the given state bits to.
   */
  void LockStyleStates(ElementState aStates, bool aEnabled);

  /**
   * Remove a style state lock on this element.
   */
  void UnlockStyleStates(ElementState aStates);

  /**
   * Clear all style state locks on this element.
   */
  void ClearStyleStateLocks();

  /**
   * Accessors for the state of our dir attribute.
   */
  bool HasDirAuto() const {
    return State().HasState(ElementState::HAS_DIR_ATTR_LIKE_AUTO);
  }

  /**
   * Elements with dir="rtl" or dir="ltr".
   */
  bool HasFixedDir() const {
    return State().HasAtLeastOneOfStates(ElementState::HAS_DIR_ATTR_LTR |
                                         ElementState::HAS_DIR_ATTR_RTL);
  }

  /**
   * Get the inline style declaration, if any, for this element.
   */
  DeclarationBlock* GetInlineStyleDeclaration() const;

  /**
   * Get the mapped attributes, if any, for this element.
   */
  StyleLockedDeclarationBlock* GetMappedAttributeStyle() const {
    return mAttrs.GetMappedDeclarationBlock();
  }

  bool IsPendingMappedAttributeEvaluation() const {
    return mAttrs.IsPendingMappedAttributeEvaluation();
  }

  void SetMappedDeclarationBlock(already_AddRefed<StyleLockedDeclarationBlock>);

  /**
   * InlineStyleDeclarationWillChange is called before SetInlineStyleDeclaration
   * so that the element implementation can access the old style attribute
   * value.
   */
  virtual void InlineStyleDeclarationWillChange(MutationClosureData& aData);

  /**
   * Set the inline style declaration for this element.
   */
  virtual nsresult SetInlineStyleDeclaration(DeclarationBlock& aDeclaration,
                                             MutationClosureData& aData);

  /**
   * Get the SMIL override style declaration for this element. If the
   * rule hasn't been created, this method simply returns null.
   */
  DeclarationBlock* GetSMILOverrideStyleDeclaration();

  /**
   * Set the SMIL override style declaration for this element. This method will
   * notify the document's pres context, so that the style changes will be
   * noticed.
   */
  void SetSMILOverrideStyleDeclaration(DeclarationBlock&);

  /**
   * Returns a new SMILAttr that allows the caller to animate the given
   * attribute on this element.
   */
  virtual UniquePtr<SMILAttr> GetAnimatedAttr(int32_t aNamespaceID,
                                              nsAtom* aName);

  /**
   * Get the SMIL override style for this element. This is a style declaration
   * that is applied *after* the inline style, and it can be used e.g. to store
   * animated style values.
   *
   * Note: This method is analogous to the 'GetStyle' method in
   * nsGenericHTMLElement and nsStyledElement.
   */
  nsDOMCSSAttributeDeclaration* SMILOverrideStyle();

  /**
   * Returns if the element is labelable as per HTML specification.
   */
  virtual bool IsLabelable() const;

  /**
   * Returns if the element is interactive content as per HTML specification.
   */
  virtual bool IsInteractiveHTMLContent() const;

  /**
   * Is the attribute named aAttribute a mapped attribute?
   */
  NS_IMETHOD_(bool) IsAttributeMapped(const nsAtom* aAttribute) const;

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;
  using nsIContent::UnbindFromTree;

  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const;
  static void MapNoAttributesInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Get a hint that tells the style system what to do when
   * an attribute on this node changes, if something needs to happen
   * in response to the change *other* than the result of what is
   * mapped into style data via any type of style rule.
   */
  virtual nsChangeHint GetAttributeChangeHint(const nsAtom* aAttribute,
                                              AttrModType aModType) const;

  inline Directionality GetDirectionality() const {
    ElementState state = State();
    if (state.HasState(ElementState::RTL)) {
      return Directionality::Rtl;
    }
    if (state.HasState(ElementState::LTR)) {
      return Directionality::Ltr;
    }
    return Directionality::Unset;
  }

  inline void SetDirectionality(Directionality aDir, bool aNotify) {
    AutoStateChangeNotifier notifier(*this, aNotify);
    RemoveStatesSilently(ElementState::DIR_STATES);
    switch (aDir) {
      case Directionality::Rtl:
        AddStatesSilently(ElementState::RTL);
        break;
      case Directionality::Ltr:
        AddStatesSilently(ElementState::LTR);
        break;
      case Directionality::Unset:
      case Directionality::Auto:
        MOZ_ASSERT_UNREACHABLE("Setting unresolved directionality?");
        break;
    }
  }

  Directionality GetComputedDirectionality() const;

  static const uint32_t kAllServoDescendantBits =
      ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO |
      ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO |
      NODE_DESCENDANTS_NEED_FRAMES;

  /**
   * Notes that something in the given subtree of this element needs dirtying,
   * and that all the relevant dirty bits have already been propagated up to the
   * element.
   *
   * This is important because `NoteDirtyForServo` uses the dirty bits to reason
   * about the shape of the tree, so we can't just call into there.
   */
  void NoteDirtySubtreeForServo();

  void NoteDirtyForServo();
  void NoteAnimationOnlyDirtyForServo();
  void NoteDescendantsNeedFramesForServo();

  bool HasDirtyDescendantsForServo() const {
    return HasFlag(ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  void SetHasDirtyDescendantsForServo() {
    SetFlags(ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  void UnsetHasDirtyDescendantsForServo() {
    UnsetFlags(ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  bool HasAnimationOnlyDirtyDescendantsForServo() const {
    return HasFlag(ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  void SetHasAnimationOnlyDirtyDescendantsForServo() {
    SetFlags(ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  void UnsetHasAnimationOnlyDirtyDescendantsForServo() {
    UnsetFlags(ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO);
  }

  bool HasServoData() const { return !!mServoData.Get(); }

  void ClearServoData() { ClearServoData(GetComposedDoc()); }
  void ClearServoData(Document* aDocument);

  PopoverData* GetPopoverData() const {
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mPopoverData.get() : nullptr;
  }

  PopoverData& EnsurePopoverData() {
    if (auto* popoverData = GetPopoverData()) {
      return *popoverData;
    }
    return CreatePopoverData();
  }

  bool IsPopoverOpenedInMode(PopoverAttributeState aMode) const;
  bool IsPopoverOpen() const;

  void SetAssociatedPopover(nsGenericHTMLElement& aPopover);
  nsGenericHTMLElement* GetAssociatedPopover() const;

  /**
   * https://html.spec.whatwg.org/multipage/popover.html#topmost-popover-ancestor
   */
  Element* GetTopmostPopoverAncestor(PopoverAttributeState aMode,
                                     const Element* aInvoker,
                                     bool isPopover) const;

  ElementAnimationData* GetAnimationData() const {
    if (!MayHaveAnimations()) {
      return nullptr;
    }
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mAnimations.get() : nullptr;
  }

  ElementAnimationData& EnsureAnimationData() {
    if (auto* anim = GetAnimationData()) {
      return *anim;
    }
    return CreateAnimationData();
  }

 private:
  ElementAnimationData& CreateAnimationData();
  PopoverData& CreatePopoverData();

 public:
  void ClearPopoverData();

  /**
   * Gets the custom element data used by web components custom element.
   * Custom element data is created at the first attempt to enqueue a callback.
   *
   * @return The custom element data or null if none.
   */
  CustomElementData* GetCustomElementData() const {
    if (!HasCustomElementData()) {
      return nullptr;
    }

    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mCustomElementData.get() : nullptr;
  }

  /**
   * Sets the custom element data, ownership of the
   * callback data is taken by this element.
   *
   * @param aData The custom element data.
   */
  void SetCustomElementData(UniquePtr<CustomElementData> aData);

  void ClearCustomElementData();

  nsTArray<RefPtr<nsAtom>>& EnsureCustomStates();

  /**
   * Gets the custom element definition used by web components custom element.
   *
   * @return The custom element definition or null if element is not a custom
   *         element or custom element is not defined yet.
   */
  CustomElementDefinition* GetCustomElementDefinition() const;

  /**
   * Sets the custom element definition, called when custom element is created
   * or upgraded.
   *
   * @param aDefinition The custom element definition.
   */
  virtual void SetCustomElementDefinition(CustomElementDefinition* aDefinition);

  const AttrArray& GetAttrs() const { return mAttrs; }

  void SetDefined(bool aSet) { SetStates(ElementState::DEFINED, aSet); }

  // AccessibilityRole
  REFLECT_NULLABLE_DOMSTRING_ATTR(Role, role)

  // AriaAttributes
  REFLECT_NULLABLE_ELEMENT_ATTR(AriaActiveDescendantElement,
                                aria_activedescendant)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaAtomic, aria_atomic)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaAutoComplete, aria_autocomplete)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaBrailleLabel, aria_braillelabel)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaBrailleRoleDescription,
                                  aria_brailleroledescription)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaBusy, aria_busy)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaChecked, aria_checked)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaColCount, aria_colcount)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaColIndex, aria_colindex)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaColIndexText, aria_colindextext)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaColSpan, aria_colspan)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaControlsElements, aria_controls)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaCurrent, aria_current)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaDescribedByElements, aria_describedby)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaDescription, aria_description)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaDetailsElements, aria_details)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaDisabled, aria_disabled)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaErrorMessageElements, aria_errormessage)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaExpanded, aria_expanded)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaFlowToElements, aria_flowto)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaHasPopup, aria_haspopup)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaHidden, aria_hidden)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaInvalid, aria_invalid)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaKeyShortcuts, aria_keyshortcuts)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaLabel, aria_label)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaLabelledByElements, aria_labelledby)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaLevel, aria_level)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaLive, aria_live)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaModal, aria_modal)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaMultiLine, aria_multiline)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaMultiSelectable, aria_multiselectable)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaOrientation, aria_orientation)
  REFLECT_NULLABLE_ELEMENTS_ATTR(AriaOwnsElements, aria_owns)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaPlaceholder, aria_placeholder)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaPosInSet, aria_posinset)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaPressed, aria_pressed)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaReadOnly, aria_readonly)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRelevant, aria_relevant)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRequired, aria_required)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRoleDescription, aria_roledescription)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRowCount, aria_rowcount)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRowIndex, aria_rowindex)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRowIndexText, aria_rowindextext)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaRowSpan, aria_rowspan)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaSelected, aria_selected)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaSetSize, aria_setsize)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaSort, aria_sort)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaValueMax, aria_valuemax)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaValueMin, aria_valuemin)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaValueNow, aria_valuenow)
  REFLECT_NULLABLE_DOMSTRING_ATTR(AriaValueText, aria_valuetext)

 protected:
  already_AddRefed<ShadowRoot> AttachShadowInternal(ShadowRootMode,
                                                    ErrorResult& aError);

 public:
  MOZ_CAN_RUN_SCRIPT
  ScrollContainerFrame* GetScrollContainerFrame(
      nsIFrame** aFrame = nullptr, FlushType aFlushType = FlushType::Layout);

 private:
  // Style state computed from element's state and style locks.
  ElementState StyleStateFromLocks() const;

  void NotifyStateChange(ElementState aStates);
  void NotifyStyleStateChange(ElementState aStates);

 public:
  struct AutoStateChangeNotifier {
    AutoStateChangeNotifier(Element& aElement, bool aNotify)
        : mElement(aElement), mOldState(aElement.State()), mNotify(aNotify) {}
    ~AutoStateChangeNotifier() {
      if (!mNotify) {
        return;
      }
      ElementState newState = mElement.State();
      if (mOldState != newState) {
        mElement.NotifyStateChange(mOldState ^ newState);
      }
    }

   private:
    Element& mElement;
    const ElementState mOldState;
    const bool mNotify;
  };

  // Method to add state bits.  This should be called from subclass constructors
  // to set up our event state correctly at construction time, and other places
  // where we don't want to notify a state change, or there's an
  // AutoStateChangeNotifier on the stack.
  void AddStatesSilently(ElementState aStates) { mState |= aStates; }
  // Method to remove state bits.  This should be called from subclass
  // constructors to set up our event state correctly at construction time and
  // other places where we don't want to notify a state change.
  void RemoveStatesSilently(ElementState aStates) { mState &= ~aStates; }
  // Methods to add state bits, potentially notifying. These will handle setting
  // up script blockers when they notify, so no need to do it in the callers
  // unless desired. States passed here must only be those in
  // EXTERNALLY_MANAGED_STATES.
  void AddStates(ElementState aStates, bool aNotify = true) {
    ElementState old = mState;
    AddStatesSilently(aStates);
    if (aNotify && old != mState) {
      NotifyStateChange(old ^ mState);
    }
  }
  void RemoveStates(ElementState aStates, bool aNotify = true) {
    ElementState old = mState;
    RemoveStatesSilently(aStates);
    if (aNotify && old != mState) {
      NotifyStateChange(old ^ mState);
    }
  }
  void SetStates(ElementState aStates, bool aSet, bool aNotify = true) {
    if (aSet) {
      AddStates(aStates, aNotify);
    } else {
      RemoveStates(aStates, aNotify);
    }
  }
  void ToggleStates(ElementState aStates, bool aNotify) {
    mState ^= aStates;
    if (aNotify) {
      NotifyStateChange(aStates);
    }
  }

  void UpdateEditableState(bool aNotify) override;
  // Makes sure that the READONLY/READWRITE flags are in sync.
  void UpdateReadOnlyState(bool aNotify);
  // Form controls and non-form controls should have different :read-only /
  // :read-write behavior. This is what effectively controls it.
  virtual bool IsReadOnlyInternal() const;

  /**
   * Normalizes an attribute name and returns it as a nodeinfo if an attribute
   * with that name exists. This method is intended for character case
   * conversion if the content object is case insensitive (e.g. HTML). Returns
   * the nodeinfo of the attribute with the specified name if one exists or
   * null otherwise.
   *
   * @param aStr the unparsed attribute string
   * @return the node info. May be nullptr.
   */
  already_AddRefed<mozilla::dom::NodeInfo> GetExistingAttrNameFromQName(
      const nsAString& aStr) const;

  /**
   * Helper for SetAttr/SetParsedAttr. This method will return true if aNotify
   * is true, this is a custom element, the attribute is currently set, and the
   * new value that is about to be set is different to the current value. As a
   * perf optimization the new and old values will not actually be compared if
   * we aren't notifying (in which case it's cheap to just return false and let
   * the caller go ahead and set the value).
   * @param aOldValue [out] Set to the old value of the attribute, but only if
   *   there are event listeners. If set, the type of aOldValue will be either
   *   nsAttrValue::eString or nsAttrValue::eAtom.
   * @param aModType [out] Set to AttrModType::Modification or to
   *   AttrModType::Addition, but only if this helper returns true
   * @param aOldValueSet [out] Indicates whether an old attribute value has been
   *   stored in aOldValue. The bool will be set to true if a value was stored.
   */
  bool MaybeCheckSameAttrVal(int32_t aNamespaceID, const nsAtom* aName,
                             const nsAtom* aPrefix,
                             const nsAttrValueOrString& aValue, bool aNotify,
                             nsAttrValue& aOldValue, AttrModType* aModType,
                             bool* aOldValueSet);

  /**
   * Notifies mutation observers if aNotify is true, there are mutation
   * observers, and the attribute value is changing.
   *
   * @param aNamespaceID The namespace of the attribute
   * @param aName The local name of the attribute
   * @param aPrefix The prefix of the attribute
   * @param aValue The value that the attribute is being changed to
   * @param aNotify If true, mutation listeners will be notified if they exist
   *   and the attribute value is changing
   * @param aOldValue [out] Set to the old value of the attribute, but only if
   *   there are event listeners. If set, the type of aOldValue will be either
   *   nsAttrValue::eString or nsAttrValue::eAtom.
   * @param aModType [out] Set to AttrModType::Modification or to
   *   AttrModType::Addition, but only if this helper returns true
   * @param aOldValueSet [out] Indicates whether an old attribute value has been
   *   stored in aOldValue. The bool will be set to true if a value was stored.
   */
  bool OnlyNotifySameValueSet(int32_t aNamespaceID, nsAtom* aName,
                              nsAtom* aPrefix,
                              const nsAttrValueOrString& aValue, bool aNotify,
                              nsAttrValue& aOldValue, AttrModType* aModType,
                              bool* aOldValueSet);

  /**
   * Sets the class attribute.
   * Assumes that we are not notifying and that the attribute hasn't been
   * set previously.
   */
  nsresult SetClassAttrFromParser(nsAtom* aValue);

  // aParsedValue receives the old value of the attribute. That's useful if
  // either the input or output value of aParsedValue is StoresOwnData.
  nsresult SetParsedAttr(int32_t aNameSpaceID, nsAtom* aName, nsAtom* aPrefix,
                         nsAttrValue& aParsedValue, bool aNotify);
  /**
   * Get the current value of the attribute. This returns a form that is
   * suitable for passing back into SetAttr.
   *
   * @param aNameSpaceID the namespace of the attr (defaults to
                         kNameSpaceID_None in the overload that omits this arg)
   * @param aName the name of the attr
   * @param aResult the value (may legitimately be the empty string) [OUT]
   * @returns true if the attribute was set (even when set to empty string)
   *          false when not set.
   * GetAttr is not inlined on purpose, to keep down codesize from all the
   * inlined nsAttrValue bits for C++ callers.
   */
  bool GetAttr(int32_t aNameSpaceID, const nsAtom* aName,
               nsAString& aResult) const;
  bool GetAttr(const nsAtom* aName, nsAString& aResult) const;

  /**
   * Determine if an attribute has been set (empty string or otherwise).
   *
   * @param aNameSpaceId the namespace id of the attribute (defaults to
                         kNameSpaceID_None in the overload that omits this arg)
   * @param aAttr the attribute name
   * @return whether an attribute exists
   */
  inline bool HasAttr(int32_t aNameSpaceID, const nsAtom* aName) const {
    return mAttrs.HasAttr(aNameSpaceID, aName);
  }

  bool HasAttr(const nsAtom* aAttr) const { return mAttrs.HasAttr(aAttr); }

  /**
   * Determine if an attribute has been set to a non-empty string value. If the
   * attribute is not set at all, this will return false.
   *
   * @param aNameSpaceId the namespace id of the attribute (defaults to
   *                     kNameSpaceID_None in the overload that omits this arg)
   * @param aAttr the attribute name
   */
  inline bool HasNonEmptyAttr(int32_t aNameSpaceID, const nsAtom* aName) const;

  bool HasNonEmptyAttr(const nsAtom* aAttr) const {
    return HasNonEmptyAttr(kNameSpaceID_None, aAttr);
  }

  /**
   * Test whether this Element's given attribute has the given value.  If the
   * attribute is not set at all, this will return false.
   *
   * @param aNameSpaceID The namespace ID of the attribute.  Must not
   *                     be kNameSpaceID_Unknown.
   * @param aName The name atom of the attribute.  Must not be null.
   * @param aValue The value to compare to.
   * @param aCaseSensitive Whether to do a case-sensitive compare on the value.
   */
  inline bool AttrValueIs(int32_t aNameSpaceID, const nsAtom* aName,
                          const nsAString& aValue,
                          nsCaseTreatment aCaseSensitive) const;

  /**
   * Test whether this Element's given attribute has the given value.  If the
   * attribute is not set at all, this will return false.
   *
   * @param aNameSpaceID The namespace ID of the attribute.  Must not
   *                     be kNameSpaceID_Unknown.
   * @param aName The name atom of the attribute.  Must not be null.
   * @param aValue The value to compare to.  Must not be null.
   * @param aCaseSensitive Whether to do a case-sensitive compare on the value.
   */
  bool AttrValueIs(int32_t aNameSpaceID, const nsAtom* aName,
                   const nsAtom* aValue, nsCaseTreatment aCaseSensitive) const;

  /**
   * Check whether this Element's given attribute has one of a given list of
   * values. If there is a match, we return the index in the list of the first
   * matching value. If there was no attribute at all, then we return
   * ATTR_MISSING. If there was an attribute but it didn't match, we return
   * ATTR_VALUE_NO_MATCH. A non-negative result always indicates a match.
   *
   * @param aNameSpaceID The namespace ID of the attribute.  Must not
   *                     be kNameSpaceID_Unknown.
   * @param aName The name atom of the attribute.  Must not be null.
   * @param aValues a nullptr-terminated array of pointers to atom values to
   * test against.
   * @param aCaseSensitive Whether to do a case-sensitive compare on the values.
   * @return ATTR_MISSING, ATTR_VALUE_NO_MATCH or the non-negative index
   * indicating the first value of aValues that matched
   */
  using AttrValuesArray = AttrArray::AttrValuesArray;
  int32_t FindAttrValueIn(int32_t aNameSpaceID, const nsAtom* aName,
                          AttrArray::AttrValuesArray* aValues,
                          nsCaseTreatment aCaseSensitive) const;

  /**
   * Set attribute values. All attribute values are assumed to have a
   * canonical string representation that can be used for these
   * methods. The SetAttr method is assumed to perform a translation
   * of the canonical form into the underlying content specific
   * form.
   *
   * @param aNameSpaceID the namespace of the attribute
   * @param aName the name of the attribute
   * @param aValue the value to set
   * @param aNotify specifies how whether or not the document should be
   *        notified of the attribute change.
   */
  nsresult SetAttr(int32_t aNameSpaceID, nsAtom* aName, const nsAString& aValue,
                   bool aNotify) {
    return SetAttr(aNameSpaceID, aName, nullptr, aValue, aNotify);
  }
  nsresult SetAttr(int32_t aNameSpaceID, nsAtom* aName, nsAtom* aPrefix,
                   const nsAString& aValue, bool aNotify) {
    return SetAttr(aNameSpaceID, aName, aPrefix, aValue, nullptr, aNotify);
  }
  nsresult SetAttr(int32_t aNameSpaceID, nsAtom* aName, const nsAString& aValue,
                   nsIPrincipal* aTriggeringPrincipal, bool aNotify) {
    return SetAttr(aNameSpaceID, aName, nullptr, aValue, aTriggeringPrincipal,
                   aNotify);
  }

  /**
   * Set attribute values. All attribute values are assumed to have a
   * canonical String representation that can be used for these
   * methods. The SetAttr method is assumed to perform a translation
   * of the canonical form into the underlying content specific
   * form.
   *
   * @param aNameSpaceID the namespace of the attribute
   * @param aName the name of the attribute
   * @param aPrefix the prefix of the attribute
   * @param aValue the value to set
   * @param aMaybeScriptedPrincipal the principal of the scripted caller
   * responsible for setting the attribute, or null if no scripted caller can be
   *        determined. A null value here does not guarantee that there is no
   *        scripted caller, but a non-null value does guarantee that a scripted
   *        caller with the given principal is directly responsible for the
   *        attribute change.
   * @param aNotify specifies how whether or not the document should be
   *        notified of the attribute change.
   */
  nsresult SetAttr(int32_t aNameSpaceID, nsAtom* aName, nsAtom* aPrefix,
                   const nsAString& aValue,
                   nsIPrincipal* aMaybeScriptedPrincipal, bool aNotify);

  nsresult SetAttr(int32_t aNameSpaceID, nsAtom* aName, nsAtom* aPrefix,
                   nsAtom* aValue, nsIPrincipal* aMaybeScriptedPrincipal,
                   bool aNotify);

  /**
   * Remove an attribute so that it is no longer explicitly specified.
   *
   * @param aNameSpaceID the namespace id of the attribute
   * @param aName the name of the attribute to unset
   * @param aNotify specifies whether or not the document should be
   * notified of the attribute change
   */
  nsresult UnsetAttr(int32_t aNameSpaceID, nsAtom* aName, bool aNotify);

  /**
   * Swap an attribute value. This is a public wrapper that ensures bloom
   * filter updates are performed correctly. For kNameSpaceID_None attributes,
   * this will automatically update the bloom filter and propagate changes to
   * parent elements.
   *
   * @param aLocalName the local name of the attribute
   * @param aValue the value to swap (will contain old value on return)
   * @param aHadValue set to true if attribute existed, false otherwise
   */
  nsresult SetAndSwapAttr(nsAtom* aLocalName, nsAttrValue& aValue,
                          bool* aHadValue);

  /**
   * Swap an attribute value. This is a public wrapper that ensures bloom
   * filter updates are performed correctly. For kNameSpaceID_None attributes,
   * this will automatically update the bloom filter and propagate changes to
   * parent elements.
   *
   * @param aName the node info of the attribute
   * @param aValue the value to swap (will contain old value on return)
   * @param aHadValue set to true if attribute existed, false otherwise
   */
  nsresult SetAndSwapAttr(mozilla::dom::NodeInfo* aName, nsAttrValue& aValue,
                          bool* aHadValue);

  /**
   * Get the namespace / name / prefix of a given attribute.
   *
   * @param   aIndex the index of the attribute name
   * @returns The name at the given index.
   * @note    The document returned by NodeInfo()->GetDocument() (if one is
   *          present) is *not* necessarily the owner document of the element.
   * @note    The pointer returned by this function is only valid until the
   *          next call of either GetAttrNameAt or SetAttr on the element.
   * @note    This will crash if the index is invalid.
   */
  const nsAttrName* GetAttrNameAt(uint32_t aIndex) const {
    return mAttrs.GetSafeAttrNameAt(aIndex);
  }

  /**
   * Same as above, but does not do out-of-bounds checks!
   */
  const nsAttrName* GetUnsafeAttrNameAt(uint32_t aIndex) const {
    return mAttrs.AttrNameAt(aIndex);
  }

  /**
   * A fallible overload of GetAttrNameAt.
   */
  [[nodiscard]] bool GetAttrNameAt(uint32_t aIndex,
                                   const nsAttrName** aResult) const {
    return mAttrs.GetSafeAttrNameAt(aIndex, aResult);
  }

  /**
   * Gets the attribute info (name and value) for this element at a given index.
   */
  BorrowedAttrInfo GetAttrInfoAt(uint32_t aIndex) const {
    if (aIndex >= mAttrs.AttrCount()) {
      return BorrowedAttrInfo(nullptr, nullptr);
    }

    return mAttrs.AttrInfoAt(aIndex);
  }

  /**
   * Get the number of all specified attributes.
   *
   * @return the number of attributes
   */
  uint32_t GetAttrCount() const { return mAttrs.AttrCount(); }

  /**
   * Get the class list of this element (this corresponds to the value of the
   * class attribute).  This may be null if there are no classes, but that's not
   * guaranteed (e.g. we could have class="").
   */
  const nsAttrValue* GetClasses() const {
    if (!MayHaveClass()) {
      return nullptr;
    }

    if (IsSVGElement()) {
      if (const nsAttrValue* value = GetSVGAnimatedClass()) {
        return value;
      }
    }

    return GetParsedAttr(nsGkAtoms::_class);
  }

#ifdef MOZ_DOM_LIST
  virtual void List(FILE* out = stdout, int32_t aIndent = 0) const override {
    List(out, aIndent, ""_ns);
  }
  virtual void DumpContent(FILE* out, int32_t aIndent,
                           bool aDumpAll) const override;
  void List(FILE* out, int32_t aIndent, const nsCString& aPrefix) const;
  void ListAttributes(FILE* out) const;
#endif

  /**
   * Append to aOutDescription a string describing the element and its
   * attributes.
   */
  enum class DescriptionKind { IdOnly, IdAndClass, AllAttributes };
  void Describe(nsAString& aOutDescription,
                DescriptionKind aKind = DescriptionKind::AllAttributes) const;

  /*
   * Attribute Mapping Helpers
   */
  struct MappedAttributeEntry {
    const nsStaticAtom* const attribute;
  };

  /**
   * A common method where you can just pass in a list of maps to check
   * for attribute dependence. Most implementations of
   * IsAttributeMapped should use this function as a default
   * handler.
   */
  template <size_t N>
  static bool FindAttributeDependence(
      const nsAtom* aAttribute, const MappedAttributeEntry* const (&aMaps)[N]) {
    return FindAttributeDependence(aAttribute, aMaps, N);
  }

  // https://html.spec.whatwg.org/#attr-button-command
  enum class Command : uint8_t {
    Invalid,
    Custom,
    TogglePopover,
    ShowPopover,
    HidePopover,
    ShowModal,
    RequestClose,
    Toggle,
    Close,
    Open,
  };

  virtual bool IsValidCommandAction(Command aCommand) const { return false; }

  /**
   * Elements can provide their own default behaviours for "Invoke" (see
   * command/commandfor attributes).
   * If the action is not recognised, they can choose to ignore it and `return
   * false`. If an action is recognised then they should `return true` to
   * indicate to sub-classes that this has been handled and no further steps
   * should be run.
   */
  MOZ_CAN_RUN_SCRIPT virtual bool HandleCommandInternal(Element* aSource,
                                                        Command aCommand,
                                                        ErrorResult& aRv) {
    return false;
  }

 private:
  void DescribeAttribute(uint32_t index, nsAString& aOutDescription) const;

  static bool FindAttributeDependence(const nsAtom* aAttribute,
                                      const MappedAttributeEntry* const aMaps[],
                                      uint32_t aMapCount);

  bool HasSharedRoot(const Element* aElement) const;

  Element* GetElementByIdInDocOrSubtree(nsAtom* aID) const;

 protected:
  inline bool GetAttr(const nsAtom* aName, DOMString& aResult) const {
    MOZ_ASSERT(aResult.IsEmpty(), "Should have empty string coming in");
    const nsAttrValue* val = mAttrs.GetAttr(aName);
    if (!val) {
      return false;  // DOMString comes pre-emptied.
    }
    val->ToString(aResult);
    return true;
  }

  inline bool GetAttr(int32_t aNameSpaceID, const nsAtom* aName,
                      DOMString& aResult) const {
    MOZ_ASSERT(aResult.IsEmpty(), "Should have empty string coming in");
    const nsAttrValue* val = mAttrs.GetAttr(aName, aNameSpaceID);
    if (!val) {
      return false;  // DOMString comes pre-emptied.
    }
    val->ToString(aResult);
    return true;
  }

 public:
  bool HasAttrs() const { return mAttrs.HasAttrs(); }

  inline bool GetAttr(const nsAString& aName, DOMString& aResult) const {
    MOZ_ASSERT(aResult.IsEmpty(), "Should have empty string coming in");
    const nsAttrValue* val = mAttrs.GetAttr(aName);
    if (val) {
      val->ToString(aResult);
      return true;
    }
    // else DOMString comes pre-emptied.
    return false;
  }

  // Note, this does not notify about the removal.
  void ClearAttributes() { mAttrs.Clear(); }

  void GetTagName(nsAString& aTagName) const { aTagName = NodeName(); }
  void GetId(nsAString& aId) const { GetAttr(nsGkAtoms::id, aId); }
  void GetId(DOMString& aId) const { GetAttr(nsGkAtoms::id, aId); }
  void SetId(const nsAString& aId) {
    SetAttr(kNameSpaceID_None, nsGkAtoms::id, aId, true);
  }
  void GetClassName(nsAString& aClassName) {
    GetAttr(nsGkAtoms::_class, aClassName);
  }
  void GetClassName(DOMString& aClassName) {
    GetAttr(nsGkAtoms::_class, aClassName);
  }
  void SetClassName(const nsAString& aClassName) {
    SetAttr(kNameSpaceID_None, nsGkAtoms::_class, aClassName, true);
  }

  nsDOMTokenList* ClassList();
  nsDOMTokenList* Part();

  nsDOMAttributeMap* Attributes();

  void GetAttributeNames(nsTArray<nsString>& aResult);

  void GetAttribute(const nsAString& aName, nsAString& aReturn) {
    DOMString str;
    GetAttribute(aName, str);
    aReturn.Assign(std::move(str));
  }

  void GetAttribute(const nsAString& aName, DOMString& aReturn);
  void GetAttributeNS(const nsAString& aNamespaceURI,
                      const nsAString& aLocalName, nsAString& aReturn);
  bool ToggleAttribute(const nsAString& aName, const Optional<bool>& aForce,
                       nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError);
  void SetAttribute(const nsAString& aName, const nsAString& aValue,
                    nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError);
  void SetAttributeNS(const nsAString& aNamespaceURI,
                      const nsAString& aLocalName, const nsAString& aValue,
                      nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError);
  void SetAttribute(const nsAString& aName, const nsAString& aValue,
                    ErrorResult& aError) {
    SetAttribute(aName, aValue, nullptr, aError);
  }

  MOZ_CAN_RUN_SCRIPT void SetAttribute(
      const nsAString& aName,
      const TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString& aValue,
      nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT void SetAttributeNS(
      const nsAString& aNamespaceURI, const nsAString& aLocalName,
      const TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString& aValue,
      nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT void SetAttribute(
      const nsAString& aName,
      const TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString& aValue,
      ErrorResult& aError) {
    SetAttribute(aName, aValue, nullptr, aError);
  }

  /**
   * This method creates a principal that subsumes this element's NodePrincipal
   * and which has flags set for elevated permissions that devtools needs to
   * operate on this element. The principal returned by this method is used by
   * various devtools methods to permit otherwise blocked operations, without
   * changing any other restrictions the NodePrincipal might have.
   */
  already_AddRefed<nsIPrincipal> CreateDevtoolsPrincipal();
  void SetAttributeDevtools(const nsAString& aName, const nsAString& aValue,
                            ErrorResult& aError);
  void SetAttributeDevtoolsNS(const nsAString& aNamespaceURI,
                              const nsAString& aLocalName,
                              const nsAString& aValue, ErrorResult& aError);

  void RemoveAttribute(const nsAString& aName, ErrorResult& aError);
  void RemoveAttributeNS(const nsAString& aNamespaceURI,
                         const nsAString& aLocalName, ErrorResult& aError);
  bool HasAttribute(const nsAString& aName) const {
    return InternalGetAttrNameFromQName(aName) != nullptr;
  }
  bool HasAttributeNS(const nsAString& aNamespaceURI,
                      const nsAString& aLocalName) const;
  bool HasAttributes() const { return HasAttrs(); }
  Element* Closest(const nsACString& aSelector, ErrorResult& aResult);
  bool Matches(const nsACString& aSelector, ErrorResult& aError);
  already_AddRefed<HTMLCollection> GetElementsByTagName(
      const nsAString& aQualifiedName);
  already_AddRefed<HTMLCollection> GetElementsByTagNameNS(
      const nsAString& aNamespaceURI, const nsAString& aLocalName,
      ErrorResult& aError);
  already_AddRefed<HTMLCollection> GetElementsByClassName(
      const nsAString& aClassNames);

  /**
   * Returns attribute-associated element for the given attribute name. See
   * https://whatpr.org/html/10995/common-microsyntaxes.html#get-the-attr-associated-element
   */
  Element* GetAttrAssociatedElementInternal(nsAtom* aAttr,
                                            bool aForBindings = false) const;
  /**
   * The getter for the IDL attribute which reflects the given attribute. See
   * https://whatpr.org/html/10995/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes:reflected-idl-attribute-31
   */
  Element* GetAttrAssociatedElementForBindings(nsAtom* aAttr) const;

  /**
   * Returns attribute associated elements for the given attribute name. See
   * https://whatpr.org/html/10995/common-microsyntaxes.html#attr-associated-elements
   */
  Maybe<nsTArray<RefPtr<Element>>> GetAttrAssociatedElementsInternal(
      nsAtom* aAttr, bool aForBindings = false);
  /**
   * The getter for the IDL attribute which reflects the given attribute. See
   * https://whatpr.org/html/10995/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes:reflected-idl-attribute-33
   */
  void GetAttrAssociatedElementsForBindings(
      nsAtom* aAttr, bool* aUseCachedValue,
      Nullable<nsTArray<RefPtr<Element>>>& aElements);

  typedef bool (*AttrTargetObserver)(Element* aOldElement, Element* aNewElement,
                                     Element* thisElement);
  /**
   * Add an attr-associated element observer for a given attribute. The observer
   * will fire whenever the element associated with |aAttr| for this element
   * changes. This can occur in multiple scenarios:
   * - The attribute value or explicitly set attr-element changes;
   * - An element with an ID matching the attribute value is added or removed
   *   from the document or shadow root containing the element with the
   *   attribute;
   * - The explicitly set attr-element is added or removed from the document or
   *   shadow root containing the element with the attribute;
   * - The reference target of the element directly referred to by the attribute
   *   changes.
   * @return the current attr-associated element for |aAttr| for this element,
   * if any.
   */
  Element* AddAttrAssociatedElementObserver(nsAtom* aAttr,
                                            AttrTargetObserver aObserver);
  void RemoveAttrAssociatedElementObserver(nsAtom* aAttr,
                                           AttrTargetObserver aObserver);
  bool AttrAssociatedElementUpdated(nsAtom* aAttr);

 protected:
  void IDREFAttributeValueChanged(nsAtom* aAttr, const nsAttrValue* aValue);

 private:
  FragmentOrElement::nsExtendedDOMSlots::AttrElementObserverData*
  GetAttrElementObserverData(nsAtom* aAttr);
  void DeleteAttrAssociatedElementObserverData(nsAtom* aAttr);
  void AddDocOrShadowObserversForAttrAssociatedElement(
      DocumentOrShadowRoot& aContainingDocOrShadow, nsAtom* aAttr);
  void RemoveDocOrShadowObserversForAttrAssociatedElement(
      DocumentOrShadowRoot& aContainingDocOrShadow, nsAtom* aAttr);
  void BindAttrAssociatedElementObservers(
      DocumentOrShadowRoot& aContainingDocOrShadow);
  void UnbindAttrAssociatedElementObservers(
      DocumentOrShadowRoot& aContainingDocOrShadow);

 public:
  /**
   * Sets an attribute element for the given attribute.
   * https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#explicitly-set-attr-element
   */
  void ExplicitlySetAttrElement(nsAtom* aAttr, Element* aElement);
  void ExplicitlySetAttrElements(
      nsAtom* aAttr,
      const Nullable<Sequence<OwningNonNull<Element>>>& aElements);

  void ClearExplicitlySetAttrElement(nsAtom*);
  void ClearExplicitlySetAttrElements(nsAtom*);

  /**
   * Gets the attribute element for the given attribute.
   * https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#explicitly-set-attr-element
   * Unlike GetAttrAssociatedElement, this returns the target even if it isn't
   * a descendant of any of this element's shadow-including ancestors. It also
   * doesn't attempt to retrieve an element using a string id set in the content
   * attribute.
   */
  Element* GetExplicitlySetAttrElement(nsAtom* aAttr) const;

  /**
   * Gets the attribute elements for the given attribute. Unlike
   * GetAttrAssociatedElements, this returns an uncached array of explicitly set
   * elements without checking if they are a descendant of any of this element's
   * shadow-including ancestors. It also does not attempt to retrieve elements
   * using the ids set in the content attribute.
   */
  Maybe<nsTArray<RefPtr<dom::Element>>> GetExplicitlySetAttrElements(
      nsAtom* aAttr) const;

  /**
   * Callback called when an element's resolved reference target changes.
   * @param aData The callback data which was stored using
   * AddReferenceTargetChangeObserver.
   * @return true to keep the callback in the callback set, false to remove
   * it.
   */
  typedef bool (*ReferenceTargetChangeObserver)(void* aData);

  /**
   * Listen for changes to the given element's resolved reference target. This
   * could happen in a number of ways:
   * - The given element's shadow root's referenceTarget property changes, or is
   *   added or removed;
   * - The element referred to by the referenceTarget property changes (e.g.
   *   because an element with that ID is added to the shadow root);
   * - Recursively: that is, when the resolved reference target of the element
   *   referred to by the referenceTarget property changes for one of the above
   *   two reasons (i.e. the referenceTarget property refers to an element foo,
   *   which also has a shadow root, and foo's resolved reference target
   *   changes).
   * @param aElement an element on which to listen for resolved
   * reference target changes. The element must have this document or shadow
   * root as its root (i.e. aElement.GetUncomposedDocOrConnectedShadowRoot() ==
   * this).
   * @param aObserver The callback to fire when the resolved reference target
   * changes.
   * @param aData Data to pass to the callback.
   */
  void AddReferenceTargetChangeObserver(ReferenceTargetChangeObserver aObserver,
                                        void* aData);
  void RemoveReferenceTargetChangeObserver(
      ReferenceTargetChangeObserver aObserver, void* aData);
  /**
   * Called when aElement's resolved reference target changes.
   * @param aElement the element whose reference target has changed
   */
  void NotifyReferenceTargetChanged();

  PseudoStyleType GetPseudoElementType() const {
    nsresult rv = NS_OK;
    auto raw = GetProperty(nsGkAtoms::pseudoProperty, &rv);
    if (rv == NS_PROPTABLE_PROP_NOT_THERE) {
      return PseudoStyleType::NotPseudo;
    }
    return PseudoStyleType(reinterpret_cast<uintptr_t>(raw));
  }

  void SetPseudoElementType(PseudoStyleType aPseudo) {
    static_assert(sizeof(PseudoStyleType) <= sizeof(uintptr_t),
                  "Need to be able to store this in a void*");
    MOZ_ASSERT(PseudoStyle::IsPseudoElement(aPseudo));
    SetProperty(nsGkAtoms::pseudoProperty, reinterpret_cast<void*>(aPseudo),
                /* aDtor = */ nullptr, /* aTransfer = */ true);
  }

  /**
   * Return an array of all elements in the subtree rooted at this
   * element that have grid container frames. This does not include
   * pseudo-elements.
   */
  void GetElementsWithGrid(nsTArray<RefPtr<Element>>& aElements);

  /**
   * Provide a direct way to determine if this Element has visible
   * scrollbars. Flushes layout.
   */
  MOZ_CAN_RUN_SCRIPT bool HasVisibleScrollbars();

  /**
   * Get an editor which handles user inputs when this element has focus.
   * If this is a text control, return a TextEditor if it's already created and
   * it's not in the design mode.
   * If this is editable, return HTMLEditor which should've already been
   * created.
   * Otherwise, return nullptr.
   */
  EditorBase* GetExtantEditor() const;

 private:
  /**
   * Implement the algorithm specified at
   * https://dom.spec.whatwg.org/#insert-adjacent for both
   * |insertAdjacentElement()| and |insertAdjacentText()| APIs.
   */
  nsINode* InsertAdjacent(const nsAString& aWhere, nsINode* aNode,
                          ErrorResult& aError);

 public:
  Element* InsertAdjacentElement(const nsAString& aWhere, Element& aElement,
                                 ErrorResult& aError);

  void InsertAdjacentText(const nsAString& aWhere, const nsAString& aData,
                          ErrorResult& aError);

  void SetPointerCapture(int32_t aPointerId, ErrorResult& aError);
  void ReleasePointerCapture(int32_t aPointerId, ErrorResult& aError);
  bool HasPointerCapture(long aPointerId);
  void SetCapture(bool aRetargetToElement);

  void SetCaptureAlways(bool aRetargetToElement);

  void ReleaseCapture();

  already_AddRefed<Promise> RequestFullscreen(const FullscreenOptions&,
                                              CallerType, ErrorResult&);
  already_AddRefed<Promise> RequestPointerLock(
      const PointerLockOptions& aOptions, CallerType aCallerType,
      ErrorResult& aRv);
  Attr* GetAttributeNode(const nsAString& aName);
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Attr> SetAttributeNode(
      Attr& aNewAttr, nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);
  already_AddRefed<Attr> RemoveAttributeNode(Attr& aOldAttr,
                                             ErrorResult& aError);
  Attr* GetAttributeNodeNS(const nsAString& aNamespaceURI,
                           const nsAString& aLocalName);
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Attr> SetAttributeNodeNS(
      Attr& aNewAttr, nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT already_AddRefed<DOMRectList> GetClientRects();
  MOZ_CAN_RUN_SCRIPT already_AddRefed<DOMRect> GetBoundingClientRect();

  enum class Loading : uint8_t {
    Eager,
    Lazy,
  };

  Loading LoadingState() const;
  void GetLoading(nsAString& aValue) const;
  bool ParseLoadingAttribute(const nsAString& aValue, nsAttrValue& aResult);

 protected:
  // Returns whether the element started lazy-loading.
  // If this function returns true, it is the caller's responsibility to call
  // LazyLoadingElementBindToTree/UnbindFromTree and/or StopLazyLoading().
  [[nodiscard]] bool MaybeStartLazyLoading();
  void StopLazyLoading();
  void LazyLoadingElementBindToTree(BindContext&);
  void LazyLoadingElementUnbindFromTree(UnbindContext&);

 public:
  // https://html.spec.whatwg.org/#potentially-render-blocking
  virtual bool IsPotentiallyRenderBlocking() { return false; }
  bool BlockingContainsRender() const;

  // Shadow DOM v1
  enum class ShadowRootDeclarative : bool { No, Yes };

  // https://dom.spec.whatwg.org/#dom-element-attachshadow
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  already_AddRefed<ShadowRoot> AttachShadow(const ShadowRootInit&,
                                            ErrorResult&);
  bool CanAttachShadowDOM() const;
  virtual void GetSlotNameFor(const ShadowRoot&, const nsIContent& aContent,
                              nsAString& aName) const {
    if (const Element* element = Element::FromNode(aContent)) {
      element->GetAttr(nsGkAtoms::slot, aName);
    }
  }
  virtual void OnChildBeforeSlotted(ShadowRoot&, nsIContent&) {}
  virtual void OnChildUnslotted(ShadowRoot&, nsIContent&) {}

  enum class DelegatesFocus : bool { No, Yes };
  enum class ShadowRootClonable : bool { No, Yes };
  enum class ShadowRootSerializable : bool { No, Yes };
  enum class CustomSlotDispatch : bool { No, Yes };

  // https://dom.spec.whatwg.org/#concept-attach-a-shadow-root
  already_AddRefed<ShadowRoot> AttachShadowWithoutNameChecks(
      const ShadowRootInit&, bool aNotify = true,
      CustomSlotDispatch = CustomSlotDispatch::No);

  // Attach UA Shadow Root if it is not attached.
  enum class NotifyUAWidget : bool { No, Yes };
  void AttachAndSetUAShadowRoot(NotifyUAWidget = NotifyUAWidget::Yes,
                                DelegatesFocus = DelegatesFocus::No,
                                CustomSlotDispatch = CustomSlotDispatch::No,
                                bool aNotify = true);

  // Dispatch an event to UAWidgetsChild, triggering construction
  // or onchange callback on the existing widget.
  void NotifyUAWidgetSetupOrChange();

  enum class UnattachShadowRoot : bool { No, Yes };
  // Dispatch an event to UAWidgetsChild, triggering UA Widget destruction.
  // and optionally remove the shadow root.
  void TeardownUAShadowRoot(NotifyUAWidget = NotifyUAWidget::Yes,
                            UnattachShadowRoot = UnattachShadowRoot::Yes);

  void UnattachShadow();

  void SetSlot(const nsAString& aName, ErrorResult& aError);
  void GetSlot(nsAString& aName);

  ShadowRoot* GetShadowRootForBindings() const;
  ShadowRoot* GetOpenOrClosedShadowRoot(nsIPrincipal& aSubject) const;
  ShadowRoot* GetShadowRoot() const {
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mShadowRoot.get() : nullptr;
  }

  Element* ResolveReferenceTarget() const;
  Element* RetargetReferenceTargetForBindings(Element* aElement) const;

  CustomElementRegistryState GetCustomElementRegistryState() const {
    return static_cast<CustomElementRegistryState>(
        (GetFlags() & ELEMENT_CUSTOM_ELEMENT_REGISTRY_MASK) /
        ELEMENT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT);
  }

  void SetCustomElementRegistryState(CustomElementRegistryState aState) {
    UnsetFlags(ELEMENT_CUSTOM_ELEMENT_REGISTRY_MASK);
    SetFlags(static_cast<uint32_t>(aState) *
             ELEMENT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT);
  }

  // Returns true if the element has an explicit registry set.
  // That registry might be null.
  bool HasCustomElementRegistry() const {
    return GetCustomElementRegistryState() !=
           CustomElementRegistryState::Global;
  }

  // https://dom.spec.whatwg.org/#element-custom-element-registry
  CustomElementRegistry* GetCustomElementRegistry();
  void SetCustomElementRegistry(CustomElementRegistry* aCustomElementRegistry);
  static void TraverseCustomElementRegistry(
      Element* aElement, nsCycleCollectionTraversalCallback& aCb);
  static void UnlinkCustomElementRegistry(Element* aElement);

  Maybe<float> GetLastRememberedBSize() const {
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mLastRememberedBSize : Nothing();
  }
  Maybe<float> GetLastRememberedISize() const {
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mLastRememberedISize : Nothing();
  }
  bool HasLastRememberedBSize() const {
    return GetLastRememberedBSize().isSome();
  }
  bool HasLastRememberedISize() const {
    return GetLastRememberedISize().isSome();
  }

  Maybe<ContentRelevancy> GetContentRelevancy() const {
    const auto* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mContentRelevancy : Nothing();
  }
  void SetContentRelevancy(ContentRelevancy relevancy) {
    ExtendedDOMSlots()->mContentRelevancy = Some(relevancy);
  }

  Maybe<bool> GetVisibleForContentVisibility() const {
    const auto* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mVisibleForContentVisibility : Nothing();
  }
  void SetVisibleForContentVisibility(bool visible) {
    ExtendedDOMSlots()->mVisibleForContentVisibility = Some(visible);
  }

  void ClearContentRelevancy() {
    if (auto* slots = GetExistingExtendedDOMSlots()) {
      slots->mContentRelevancy.reset();
      slots->mVisibleForContentVisibility.reset();
      slots->mTemporarilyVisibleForScrolledIntoViewDescendant = false;
    }
  }

  bool TemporarilyVisibleForScrolledIntoViewDescendant() const {
    const auto* slots = GetExistingExtendedDOMSlots();
    return slots && slots->mTemporarilyVisibleForScrolledIntoViewDescendant;
  }

  void SetTemporarilyVisibleForScrolledIntoViewDescendant(bool aVisible) {
    ExtendedDOMSlots()->mTemporarilyVisibleForScrolledIntoViewDescendant =
        aVisible;
  }

  // https://drafts.csswg.org/cssom-view-1/#dom-element-checkvisibility
  MOZ_CAN_RUN_SCRIPT bool CheckVisibility(const CheckVisibilityOptions&);

 private:
  // DO NOT USE THIS FUNCTION directly in C++. This function is supposed to be
  // called from JS. Use PresShell::ScrollContentIntoView instead.
  MOZ_CAN_RUN_SCRIPT void ScrollIntoView(const ScrollIntoViewOptions& aOptions);

 public:
  MOZ_CAN_RUN_SCRIPT
  // DO NOT USE THIS FUNCTION directly in C++. This function is supposed to be
  // called from JS. Use PresShell::ScrollContentIntoView instead.
  void ScrollIntoView(const BooleanOrScrollIntoViewOptions& aObject);
  MOZ_CAN_RUN_SCRIPT void ScrollTo(double aXScroll, double aYScroll);
  MOZ_CAN_RUN_SCRIPT void ScrollTo(const ScrollToOptions& aOptions);
  MOZ_CAN_RUN_SCRIPT void ScrollBy(double aXScrollDif, double aYScrollDif);
  MOZ_CAN_RUN_SCRIPT void ScrollBy(const ScrollToOptions& aOptions);
  MOZ_CAN_RUN_SCRIPT double ScrollTop();
  MOZ_CAN_RUN_SCRIPT void SetScrollTop(double aScrollTop);
  MOZ_CAN_RUN_SCRIPT double ScrollLeft();
  MOZ_CAN_RUN_SCRIPT void SetScrollLeft(double aScrollLeft);
  MOZ_CAN_RUN_SCRIPT int32_t ScrollWidth();
  MOZ_CAN_RUN_SCRIPT int32_t ScrollHeight();
  MOZ_CAN_RUN_SCRIPT void MozScrollSnap();
  MOZ_CAN_RUN_SCRIPT int32_t ClientTop() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().y).Rounded();
  }
  MOZ_CAN_RUN_SCRIPT int32_t ClientLeft() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().x).Rounded();
  }
  MOZ_CAN_RUN_SCRIPT int32_t ClientWidth() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().Width()).Rounded();
  }
  MOZ_CAN_RUN_SCRIPT int32_t ClientHeight() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().Height()).Rounded();
  }

  MOZ_CAN_RUN_SCRIPT int32_t ScreenX();
  MOZ_CAN_RUN_SCRIPT int32_t ScreenY();
  MOZ_CAN_RUN_SCRIPT already_AddRefed<nsIScreen> GetScreen();

  MOZ_CAN_RUN_SCRIPT double ScrollTopMin();
  MOZ_CAN_RUN_SCRIPT double ScrollTopMax();
  MOZ_CAN_RUN_SCRIPT double ScrollLeftMin();
  MOZ_CAN_RUN_SCRIPT double ScrollLeftMax();

  MOZ_CAN_RUN_SCRIPT double ClientHeightDouble() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().Height());
  }

  MOZ_CAN_RUN_SCRIPT double ClientWidthDouble() {
    return CSSPixel::FromAppUnits(GetClientAreaRect().Width());
  }

  MOZ_CAN_RUN_SCRIPT double CurrentCSSZoom();

  Element* GetOffsetParent() {
    CSSIntRect rcFrame;
    return GetOffsetRect(rcFrame);
  }
  int32_t OffsetTop() {
    CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);
    return rcFrame.y;
  }
  int32_t OffsetLeft() {
    CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);
    return rcFrame.x;
  }
  int32_t OffsetWidth() {
    CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);
    return rcFrame.Width();
  }
  int32_t OffsetHeight() {
    CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);
    return rcFrame.Height();
  }
  /**
   * Get the frame's offset information for offsetTop/Left/Width/Height.
   * Returns the parent the offset is relative to.
   * @note This method flushes pending notifications (FlushType::Layout).
   * @param aRect the offset information [OUT]
   */
  Element* GetOffsetRect(CSSIntRect& aRect);

  // This function will return the block size of first line box, no matter if
  // the box is 'block' or 'inline'. The return unit is pixel. If the element
  // can't get a primary frame, we will return be zero.
  double FirstLineBoxBSize() const;

  already_AddRefed<Flex> GetAsFlexContainer();
  void GetGridFragments(nsTArray<RefPtr<Grid>>& aResult);

  bool HasGridFragments();

  already_AddRefed<DOMMatrixReadOnly> GetTransformToAncestor(
      Element& aAncestor);
  already_AddRefed<DOMMatrixReadOnly> GetTransformToParent();
  already_AddRefed<DOMMatrixReadOnly> GetTransformToViewport();

  already_AddRefed<Animation> Animate(
      JSContext* aContext, JS::Handle<JSObject*> aKeyframes,
      const UnrestrictedDoubleOrKeyframeAnimationOptions& aOptions,
      ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void GetAnimations(const GetAnimationsOptions& aOptions,
                     nsTArray<RefPtr<Animation>>& aAnimations,
                     ErrorResult& aError);

  void GetAnimationsWithoutFlush(const GetAnimationsOptions& aOptions,
                                 nsTArray<RefPtr<Animation>>& aAnimations,
                                 ErrorResult& aError);

  void CloneAnimationsFrom(const Element& aOther);

  virtual void GetInnerHTML(nsAString& aInnerHTML, OOMReporter& aError);

  // https://html.spec.whatwg.org/#dom-parsing-and-serialization:dom-element-innerhtml
  // @param aInnerHTML will always be of type `NullIsEmptyString`.
  void GetInnerHTML(OwningTrustedHTMLOrNullIsEmptyString& aInnerHTML,
                    OOMReporter& aError);

  // https://html.spec.whatwg.org/#dom-parsing-and-serialization:dom-element-innerhtml
  //
  // May only run script if aInnerHTML is a string. If this behavior changes,
  // callees might need adjusting.
  MOZ_CAN_RUN_SCRIPT void SetInnerHTML(
      const TrustedHTMLOrNullIsEmptyString& aInnerHTML,
      nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  // Call this method only with trusted, i.e. non-attacker-controlled, strings.
  virtual void SetInnerHTMLTrusted(const nsAString& aInnerHTML,
                                   nsIPrincipal* aSubjectPrincipal,
                                   ErrorResult& aError);

  // @param aOuterHTML will always be of type `NullIsEmptyString`.
  void GetOuterHTML(OwningTrustedHTMLOrNullIsEmptyString& aOuterHTML);

  MOZ_CAN_RUN_SCRIPT void SetOuterHTML(
      const TrustedHTMLOrNullIsEmptyString& aOuterHTML,
      nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT void InsertAdjacentHTML(
      const nsAString& aPosition,
      const TrustedHTMLOrString& aTrustedHTMLOrString,
      nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  virtual void SetHTML(const nsAString& aInnerHTML,
                       const SetHTMLOptions& aOptions, ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  virtual void SetHTMLUnsafe(const TrustedHTMLOrString& aHTML,
                             const SetHTMLUnsafeOptions& aOptions,
                             nsIPrincipal* aSubjectPrincipal,
                             ErrorResult& aError);

  void GetHTML(const GetHTMLOptions& aOptions, nsAString& aResult);

  StylePropertyMapReadOnly* ComputedStyleMap();

  //----------------------------------------

  /**
   * Add a script event listener with the given event handler name
   * (like onclick) and with the value as JS
   * @param aEventName the event listener name
   * @param aValue the JS to attach
   * @param aDefer indicates if deferred execution is allowed
   */
  void SetEventHandler(nsAtom* aEventName, const nsAString& aValue,
                       bool aDefer = true);

  /**
   * Do whatever needs to be done when the mouse leaves a link
   */
  nsresult LeaveLink(nsPresContext* aPresContext);

  static bool ShouldBlur(nsIContent* aContent);

  /**
   * Method to create and dispatch a left-click event loosely based on
   * aSourceEvent. If aFullDispatch is true, the event will be dispatched
   * through the full dispatching of the presshell of the aPresContext; if it's
   * false the event will be dispatched only as a DOM event.
   * If aPresContext is nullptr, this does nothing.
   *
   * @param aFlags      Extra flags for the dispatching event.  The true flags
   *                    will be respected.
   */
  MOZ_CAN_RUN_SCRIPT
  static nsresult DispatchClickEvent(nsPresContext* aPresContext,
                                     WidgetInputEvent* aSourceEvent,
                                     nsIContent* aTarget, bool aFullDispatch,
                                     const EventFlags* aFlags,
                                     nsEventStatus* aStatus);

  /**
   * Method to dispatch aEvent to aTarget. If aFullDispatch is true, the event
   * will be dispatched through the full dispatching of the presshell of the
   * aPresContext; if it's false the event will be dispatched only as a DOM
   * event.
   * If aPresContext is nullptr, this does nothing.
   */
  using nsIContent::DispatchEvent;
  MOZ_CAN_RUN_SCRIPT
  static nsresult DispatchEvent(nsPresContext* aPresContext,
                                WidgetEvent* aEvent, nsIContent* aTarget,
                                bool aFullDispatch, nsEventStatus* aStatus);

  bool IsDisplayContents() const {
    return HasServoData() && Servo_Element_IsDisplayContents(this);
  }

  /*
   * https://html.spec.whatwg.org/#being-rendered
   *
   * With a gotcha for display contents:
   *   https://github.com/whatwg/html/issues/1837
   */
  bool IsRendered() const { return GetPrimaryFrame() || IsDisplayContents(); }

  const nsAttrValue* GetParsedAttr(const nsAtom* aAttr) const {
    return mAttrs.GetAttr(aAttr);
  }

  const nsAttrValue* GetParsedAttr(const nsAtom* aAttr,
                                   int32_t aNameSpaceID) const {
    return mAttrs.GetAttr(aAttr, aNameSpaceID);
  }

  /**
   * Returns the attribute map, if there is one.
   *
   * @return existing attribute map or nullptr.
   */
  nsDOMAttributeMap* GetAttributeMap() {
    nsDOMSlots* slots = GetExistingDOMSlots();

    return slots ? slots->mAttributeMap.get() : nullptr;
  }

  void RecompileScriptEventListeners();

  /**
   * Get the attr info for the given namespace ID and attribute name.  The
   * namespace ID must not be kNameSpaceID_Unknown and the name must not be
   * null.  Note that this can only return info on attributes that actually
   * live on this element (and is only virtual to handle XUL prototypes).  That
   * is, this should only be called from methods that only care about attrs
   * that effectively live in mAttrs.
   */
  BorrowedAttrInfo GetAttrInfo(int32_t aNamespaceID,
                               const nsAtom* aName) const {
    NS_ASSERTION(aName, "must have attribute name");
    NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
                 "must have a real namespace ID!");

    int32_t index = mAttrs.IndexOfAttr(aName, aNamespaceID);
    if (index < 0) {
      return BorrowedAttrInfo(nullptr, nullptr);
    }

    return mAttrs.AttrInfoAt(index);
  }

  /**
   * Parse a string into an nsAttrValue for a CORS attribute.  This
   * never fails.  The resulting value is an enumerated value whose
   * GetEnumValue() returns one of the above constants.
   */
  static void ParseCORSValue(const nsAString& aValue, nsAttrValue& aResult);

  /**
   * Return the CORS mode for a given string
   */
  static CORSMode StringToCORSMode(const nsAString& aValue);

  /**
   * Return the CORS mode for a given nsAttrValue (which may be null,
   * but if not should have been parsed via ParseCORSValue).
   */
  static CORSMode AttrValueToCORSMode(const nsAttrValue* aValue);

  nsINode* GetScopeChainParent() const override;

  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  /**
   * Locate a TextEditor rooted at this content node, if there is one.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::TextEditor* GetTextEditorInternal();

  /**
   * Detach EditContext from this element.
   */
  void ClearEditContext();

  /**
   * Gets value of boolean attribute. Only works for attributes in null
   * namespace.
   *
   * @param aAttr    name of attribute.
   * @param aValue   Boolean value of attribute.
   */
  bool GetBoolAttr(nsAtom* aAttr) const { return HasAttr(aAttr); }

  /**
   * Sets value of boolean attribute by removing attribute or setting it to
   * the empty string. Only works for attributes in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aValue   Boolean value of attribute.
   */
  nsresult SetBoolAttr(nsAtom* aAttr, bool aValue);

  /**
   * Gets the enum value string of an attribute and using a default value if
   * the attribute is missing or the string is an invalid enum value.
   *
   * @param aType     the name of the attribute.
   * @param aDefault  the default value if the attribute is missing or invalid.
   * @param aResult   string corresponding to the value [out].
   */
  void GetEnumAttr(nsAtom* aAttr, const char* aDefault,
                   nsAString& aResult) const;

  /**
   * Gets the enum value string of an attribute and using the default missing
   * value if the attribute is missing or the default invalid value if the
   * string is an invalid enum value.
   *
   * @param aType            the name of the attribute.
   * @param aDefaultMissing  the default value if the attribute is missing.  If
                             null and the attribute is missing, aResult will be
                             set to the null DOMString; this only matters for
                             cases in which we're reflecting a nullable string.
   * @param aDefaultInvalid  the default value if the attribute is invalid.
   * @param aResult          string corresponding to the value [out].
   */
  void GetEnumAttr(nsAtom* aAttr, const char* aDefaultMissing,
                   const char* aDefaultInvalid, nsAString& aResult) const;

  /**
   * Unset an attribute.
   */
  void UnsetAttr(nsAtom* aAttr, ErrorResult& aError) {
    aError = UnsetAttr(kNameSpaceID_None, aAttr, true);
  }

  /**
   * Set an attribute in the simplest way possible.
   */
  void SetAttr(nsAtom* aAttr, const nsAString& aValue, ErrorResult& aError) {
    aError = SetAttr(kNameSpaceID_None, aAttr, aValue, true);
  }

  void SetAttr(nsAtom* aAttr, const nsAString& aValue,
               nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError) {
    aError =
        SetAttr(kNameSpaceID_None, aAttr, aValue, aTriggeringPrincipal, true);
  }

  /**
   * Preallocate space in this element's attribute array for the given
   * total number of attributes.
   */
  void TryReserveAttributeCount(uint32_t aAttributeCount);

  void SetParserHadDuplicateAttributeError() {
    SetFlags(ELEMENT_PARSER_HAD_DUPLICATE_ATTR_ERROR);
  }

  /**
   * Set a content attribute via a reflecting nullable string IDL
   * attribute (e.g. a CORS attribute).  If DOMStringIsNull(aValue),
   * this will actually remove the content attribute.
   */
  void SetOrRemoveNullableStringAttr(nsAtom* aName, const nsAString& aValue,
                                     ErrorResult& aError);

  /**
   * Retrieve the ratio of font-size-inflated text font size to computed font
   * size for this element. This will query the element for its primary frame,
   * and then use this to get font size inflation information about the frame.
   *
   * @returns The font size inflation ratio (inflated font size to uninflated
   *          font size) for the primary frame of this element. Returns 1.0
   *          by default if font size inflation is not enabled. Returns -1
   *          if the element does not have a primary frame.
   *
   * @note The font size inflation ratio that is returned is actually the
   *       font size inflation data for the element's _primary frame_, not the
   *       element itself, but for most purposes, this should be sufficient.
   */
  float FontSizeInflation();

  void GetImplementedPseudoElement(nsAString&) const;

  /**
   * Get the pseudo element for this pseudo request (i.e. PseudoStyleType and
   * its function parameter, if any).
   */
  Element* GetPseudoElement(const PseudoStyleRequest&) const;

  ReferrerPolicy GetReferrerPolicyAsEnum() const;
  ReferrerPolicy ReferrerPolicyFromAttr(const nsAttrValue* aValue) const;

  /*
   * Helpers for .dataset.  This is implemented on Element, though only some
   * sorts of elements expose it to JS as a .dataset property
   */
  // Getter, to be called from bindings.
  already_AddRefed<nsDOMStringMap> Dataset();
  // Callback for destructor of dataset to ensure to null out our weak pointer
  // to it.
  void ClearDataset();

  // A number of methods to cast to various XUL interfaces. They return a
  // pointer only if the element implements that interface.
  already_AddRefed<nsIDOMXULButtonElement> AsXULButton();
  already_AddRefed<nsIDOMXULContainerElement> AsXULContainer();
  already_AddRefed<nsIDOMXULContainerItemElement> AsXULContainerItem();
  already_AddRefed<nsIDOMXULControlElement> AsXULControl();
  already_AddRefed<nsIDOMXULMenuListElement> AsXULMenuList();
  already_AddRefed<nsIDOMXULMultiSelectControlElement>
  AsXULMultiSelectControl();
  already_AddRefed<nsIDOMXULRadioGroupElement> AsXULRadioGroup();
  already_AddRefed<nsIDOMXULRelatedElement> AsXULRelated();
  already_AddRefed<nsIDOMXULSelectControlElement> AsXULSelectControl();
  already_AddRefed<nsIDOMXULSelectControlItemElement> AsXULSelectControlItem();
  already_AddRefed<nsIBrowser> AsBrowser();
  already_AddRefed<nsIAutoCompletePopup> AsAutoCompletePopup();

  /**
   * Get the presentation context for this content node.
   * @return the presentation context
   */
  enum PresContextFor { eForComposedDoc, eForUncomposedDoc };
  nsPresContext* GetPresContext(PresContextFor aFor) const;

  /**
   * The method focuses (or activates) element that accesskey is bound to. It is
   * called when accesskey is activated.
   *
   * @param aKeyCausesActivation - if true then element should be activated
   * @param aIsTrustedEvent - if true then event that is cause of accesskey
   *                          execution is trusted.
   * @return an error if the element isn't able to handle the accesskey (caller
   *         would look for the next element to handle it).
   *         a boolean indicates whether the focus moves to the element after
   *         the element handles the accesskey.
   */
  MOZ_CAN_RUN_SCRIPT
  virtual Result<bool, nsresult> PerformAccesskey(bool aKeyCausesActivation,
                                                  bool aIsTrustedEvent) {
    return Err(NS_ERROR_NOT_IMPLEMENTED);
  }

 protected:
  // Supported rel values for <form> and anchors.
  static const DOMTokenListSupportedToken sAnchorAndFormRelValues[];

  /*
   * Named-bools for use with SetAttrAndNotify to make call sites easier to
   * read.
   */
  static const bool kNotifyDocumentObservers = true;
  static const bool kDontNotifyDocumentObservers = false;
  static const bool kCallAfterSetAttr = true;
  static const bool kDontCallAfterSetAttr = false;

  /*
   * The supported values of blocking attribute for use with nsDOMTokenList.
   */
  static const DOMTokenListSupportedToken sSupportedBlockingValues[];

  /**
   * Common implementation for SetAttr overloads. Takes a callback to perform
   * the type-specific parsing and value setting.
   */
  template <typename ParseFunc>
  nsresult SetAttrInternal(int32_t aNamespaceID, nsAtom* aName, nsAtom* aPrefix,
                           const nsAttrValueOrString& aValueForComparison,
                           nsIPrincipal* aSubjectPrincipal, bool aNotify,
                           ParseFunc&& aParseFn);

  /**
   * Set attribute and (if needed) notify documentobservers.  This will send the
   * AttributeChanged notification. Callers of this method are responsible for
   * calling AttributeWillChange, since that needs to happen before the new attr
   * value has been set, and in particular before it has been parsed.
   *
   * For the boolean parameters, consider using the named bools above to aid
   * code readability.
   *
   * @param aNamespaceID  namespace of attribute
   * @param aAttribute    local-name of attribute
   * @param aPrefix       aPrefix of attribute
   * @param aOldValue     The old value of the attribute to use as a fallback
   *                      in the cases where the actual old value (i.e.
   *                      its current value) is !StoresOwnData() --- in which
   *                      case the current value is probably already useless.
   *                      If the current value is StoresOwnData() (or absent),
   *                      aOldValue will not be used. aOldValue will only be set
   *                      in certain circumstances (there are mutation
   *                      listeners, element is a custom element, attribute was
   *                      not previously unset). Otherwise it will be null.
   * @param aParsedValue  parsed new value of attribute. Replaced by the
   *                      old value of the attribute. This old value is only
   *                      useful if either it or the new value is StoresOwnData.
   * @param aSubjectPrincipal
   *                      the principal of the scripted caller responsible for
   *                      setting the attribute, or null if no scripted caller
   *                      can be determined. A null value here does not
   *                      guarantee that there is no scripted caller, but a
   *                      non-null value does guarantee that a scripted caller
   *                      with the given principal is directly responsible for
   *                      the attribute change.
   * @param aModType      AttrModType::Modification or AttrModType::Addition.
   * @param aNotify       should we notify document-observers?
   * @param aCallAfterSetAttr should we call AfterSetAttr?
   * @param aComposedDocument The current composed document of the element.
   * @param aGuard        For making sure that this is called with a
   *                      mozAutoDocUpdate instance, this is here.  Specify
   *                      an instance of it which you created for the call.
   */
  nsresult SetAttrAndNotify(int32_t aNamespaceID, nsAtom* aName,
                            nsAtom* aPrefix, const nsAttrValue* aOldValue,
                            nsAttrValue& aParsedValue,
                            nsIPrincipal* aSubjectPrincipal,
                            AttrModType aModType, bool aNotify,
                            bool aCallAfterSetAttr, Document* aComposedDocument,
                            const mozAutoDocUpdate& aGuard);

  /**
   * Convert an attribute string value to attribute type based on the type of
   * attribute.  Called by SetAttr().  Note that at the moment we only do this
   * for attributes in the null namespace (kNameSpaceID_None).
   *
   * @param aNamespaceID the namespace of the attribute to convert
   * @param aAttribute the attribute to convert
   * @param aValue the string value to convert
   * @param aMaybeScriptedPrincipal the principal of the script setting the
   *        attribute, if one can be determined, or null otherwise. As in
   *        AfterSetAttr, a null value does not guarantee that the attribute was
   *        not set by a scripted caller, but a non-null value guarantees that
   *        the attribute was set by a scripted caller with the given principal.
   * @param aResult the nsAttrValue [OUT]
   * @return true if the parsing was successful, false otherwise
   */
  virtual bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                              const nsAString& aValue,
                              nsIPrincipal* aMaybeScriptedPrincipal,
                              nsAttrValue& aResult);

  /**
   * Hook that is called by Element::SetAttr to allow subclasses to
   * deal with attribute sets.  This will only be called after we verify that
   * we're actually doing an attr set and will be called before
   * AttributeWillChange and before ParseAttribute and hence before we've set
   * the new value.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the value it's being set to represented as either a string or
   *        a parsed nsAttrValue. Alternatively, if the attr is being removed it
   *        will be null.
   * @param aNotify Whether we plan to notify document observers.
   */
  virtual void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                             const nsAttrValue* aValue, bool aNotify);

  /**
   * Hook that is called by Element::SetAttr to allow subclasses to
   * deal with attribute sets.  This will only be called after we have called
   * SetAndSwapAttr (that is, after we have actually set the attr).  It will
   * always be called under a scriptblocker.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the value it's being set to.  If null, the attr is being
   *        removed.
   * @param aOldValue the value that the attribute had previously. If null,
   *        the attr was not previously set. This argument may not have the
   *        correct value for SVG elements, or other cases in which the
   *        attribute value doesn't store its own data
   * @param aMaybeScriptedPrincipal the principal of the scripted caller
   *        responsible for setting the attribute, or null if no scripted caller
   *        can be determined, or the attribute is being unset. A null value
   *        here does not guarantee that there is no scripted caller, but a
   *        non-null value does guarantee that a scripted caller with the given
   *        principal is directly responsible for the attribute change.
   * @param aNotify Whether we plan to notify document observers.
   */
  virtual void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                            const nsAttrValue* aValue,
                            const nsAttrValue* aOldValue,
                            nsIPrincipal* aMaybeScriptedPrincipal,
                            bool aNotify);

  /**
   * This function shall be called just before the id attribute changes. It will
   * be called after BeforeSetAttr. If the attribute being changed is not the id
   * attribute, this function does nothing. Otherwise, it will remove the old id
   * from the document's id cache.
   *
   * This must happen after BeforeSetAttr (rather than during) because the
   * the subclasses' calls to BeforeSetAttr may notify on state changes. If they
   * incorrectly determine whether the element had an id, the element may not be
   * restyled properly.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the new id value. Will be null if the id is being unset.
   */
  void PreIdMaybeChange(int32_t aNamespaceID, nsAtom* aName,
                        const nsAttrValue* aValue);

  /**
   * This function shall be called just after the id attribute changes. It will
   * be called before AfterSetAttr. If the attribute being changed is not the id
   * attribute, this function does nothing. Otherwise, it will add the new id to
   * the document's id cache and properly set the ElementHasID flag.
   *
   * This must happen before AfterSetAttr (rather than during) because the
   * the subclasses' calls to AfterSetAttr may notify on state changes. If they
   * incorrectly determine whether the element now has an id, the element may
   * not be restyled properly.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the new id value. Will be null if the id is being unset.
   */
  void PostIdMaybeChange(int32_t aNamespaceID, nsAtom* aName,
                         const nsAttrValue* aValue);

  /**
   * Usually, setting an attribute to the value that it already has results in
   * no action. However, in some cases, setting an attribute to its current
   * value should have the effect of, for example, forcing a reload of
   * network data. To address that, this function will be called in this
   * situation to allow the handling of such a case.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the value it's being set to represented as either a string or
   *        a parsed nsAttrValue.
   * @param aNotify Whether we plan to notify document observers.
   */
  virtual void OnAttrSetButNotChanged(int32_t aNamespaceID, nsAtom* aName,
                                      const nsAttrValueOrString& aValue,
                                      bool aNotify);

  /**
   * Hook to allow subclasses to produce a different EventListenerManager if
   * needed for attachment of attribute-defined handlers
   */
  virtual EventListenerManager* GetEventListenerManagerForAttr(
      nsAtom* aAttrName, bool* aDefer);

  /**
   * Internal hook for converting an attribute name-string to nsAttrName in
   * case there is such existing attribute. aNameToUse can be passed to get
   * name which was used for looking for the attribute (lowercase in HTML).
   */
  const nsAttrName* InternalGetAttrNameFromQName(
      const nsAString& aStr, nsAutoString* aNameToUse = nullptr) const;

  virtual Element* GetNameSpaceElement() override { return this; }

  Attr* GetAttributeNodeNSInternal(const nsAString& aNamespaceURI,
                                   const nsAString& aLocalName);

  inline void RegisterActivityObserver();
  inline void UnregisterActivityObserver();

  /**
   * Add/remove this element to the documents id cache
   */
  void AddToIdTable(nsAtom* aId);
  void RemoveFromIdTable();

  /**
   * Functions to carry out event default actions for links of all types
   * (HTML links, XLinks, SVG "XLinks", etc.)
   */

  /**
   * Check that we meet the conditions to handle a link event
   * and that we are actually on a link.
   *
   * @param aVisitor event visitor
   * @return true if we can handle the link event, false otherwise
   */
  bool CheckHandleEventForLinksPrecondition(EventChainVisitor& aVisitor) const;

  /**
   * Handle status bar updates before they can be cancelled.
   */
  void GetEventTargetParentForLinks(EventChainPreVisitor& aVisitor);

  void DispatchChromeOnlyLinkClickEvent(EventChainPostVisitor& aVisitor);

  /**
   * Handle default actions for link event if the event isn't consumed yet.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEventForLinks(EventChainPostVisitor& aVisitor);

  mozilla::dom::FetchPriority GetFetchPriority() const;

  static void ParseFetchPriority(const nsAString& aValue, nsAttrValue& aResult);

 public:
  /**
   * Check if this element is a link. This matches the CSS definition of the
   * :any-link pseudo-class.
   */
  bool IsLink() const {
    return mState.HasAtLeastOneOfStates(ElementState::VISITED |
                                        ElementState::UNVISITED);
  }

  /**
   * Get a pointer to the full href URI (fully resolved and canonicalized, since
   * it's an nsIURI object) for link elements.
   *
   * @return A pointer to the URI or null if the element is not a link, or it
   *         has no HREF attribute, or the HREF attribute is an invalid URI.
   */
  virtual already_AddRefed<nsIURI> GetHrefURI() const { return nullptr; }

  // Step 2. of
  // <https://html.spec.whatwg.org/multipage/semantics.html#get-an-element's-target>
  //
  // Sanitize targets that look like they contain dangling markup.
  static void SanitizeLinkOrFormTarget(nsAString& aTarget);

  /**
   * <https://html.spec.whatwg.org/multipage/semantics.html#get-an-element's-target>
   * (Excluding <form>)
   *
   * Get the target of this link element. Consumers should established that
   * this element is a link (probably using IsLink) before calling this
   * function (or else why call it?). This method neuters probably markup
   * injection attempts.
   *
   * Note: for HTML this gets the value of the 'target' attribute; for XLink
   * this gets the value of the xlink:_moz_target attribute, or failing that,
   * the value of xlink:show, converted to a suitably equivalent named target
   * (e.g. _blank).
   */
  void GetLinkTarget(nsAString& aTarget);

  virtual void GetLinkTargetImpl(nsAString& aTarget);

  virtual bool Translate() const;

  MOZ_CAN_RUN_SCRIPT
  void FireBeforematchEvent(ErrorResult& aRv);

  void PropagateBloomFilterToParents();
  void UpdateSubtreeBloomFilterForClass(const nsAttrValue* aClassValue);
  void UpdateSubtreeBloomFilterForAttribute(nsAtom* aAttribute);
  uint64_t GetSubtreeBloomFilter() const {
    return mAttrs.GetSubtreeBloomFilter();
  }
#ifdef DEBUG
  void VerifySubtreeBloomFilter() const;
#endif

 protected:
  enum class ReparseAttributes { No, Yes };
  /**
   * Copy attributes and state to another element
   * @param aDest the object to copy to
   */
  nsresult CopyInnerTo(Element* aDest,
                       ReparseAttributes = ReparseAttributes::Yes);

  /**
   * Some event handler content attributes have a different name (e.g. different
   * case) from the actual event name.  This function takes an event handler
   * content attribute name and returns the corresponding event name, to be used
   * for adding the actual event listener.
   */
  virtual nsAtom* GetEventNameForAttr(nsAtom* aAttr);

  /**
   * Register/unregister this element to accesskey map if it supports accesskey.
   */
  virtual void RegUnRegAccessKey(bool aDoReg);

  // Prevent people from doing pointless checks/casts on Element instances.
  void IsElement() = delete;
  void AsElement() = delete;

 private:
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  void AssertInvariantsOnNodeInfoChange();
#endif

  /**
   * Slow path for GetClasses, this should only be called for SVG elements.
   */
  const nsAttrValue* GetSVGAnimatedClass() const;

  /**
   * Get this element's client area rect in app units.
   * @return the frame's client area
   */
  MOZ_CAN_RUN_SCRIPT nsRect GetClientAreaRect();

  /** Gets the scroll size as for the scroll{Width,Height} APIs */
  MOZ_CAN_RUN_SCRIPT nsSize GetScrollSize();
  /** Gets the scroll position as for the scroll{Top,Left} APIs */
  MOZ_CAN_RUN_SCRIPT nsPoint GetScrollOrigin();
  /** Gets the scroll range as for the scroll{Top,Left}{Min,Max} APIs */
  MOZ_CAN_RUN_SCRIPT nsRect GetScrollRange();

  /**
   * GetCustomInterface is somewhat like a GetInterface, but it is expected
   * that the implementation is provided by a custom element or via the
   * the XBL implements keyword. To use this, create a public method that
   * wraps a call to GetCustomInterface.
   */
  template <class T>
  void GetCustomInterface(nsGetterAddRefs<T> aResult);

  // Data members
  ElementState mState;
  // Per-node data managed by Servo.
  //
  // There should not be data on nodes that are not in the flattened tree, or
  // descendants of display: none elements.
  mozilla::RustCell<ServoNodeData*> mServoData;

 protected:
  // Array containing all attributes for this element
  AttrArray mAttrs;
};

inline bool Element::HasNonEmptyAttr(int32_t aNameSpaceID,
                                     const nsAtom* aName) const {
  MOZ_ASSERT(aNameSpaceID > kNameSpaceID_Unknown, "Must have namespace");
  MOZ_ASSERT(aName, "Must have attribute name");

  const nsAttrValue* val = mAttrs.GetAttr(aName, aNameSpaceID);
  return val && !val->IsEmptyString();
}

inline bool Element::AttrValueIs(int32_t aNameSpaceID, const nsAtom* aName,
                                 const nsAString& aValue,
                                 nsCaseTreatment aCaseSensitive) const {
  return mAttrs.AttrValueIs(aNameSpaceID, aName, aValue, aCaseSensitive);
}

inline bool Element::AttrValueIs(int32_t aNameSpaceID, const nsAtom* aName,
                                 const nsAtom* aValue,
                                 nsCaseTreatment aCaseSensitive) const {
  return mAttrs.AttrValueIs(aNameSpaceID, aName, aValue, aCaseSensitive);
}

}  // namespace dom
}  // namespace mozilla

NON_VIRTUAL_ADDREF_RELEASE(mozilla::dom::Element)

inline mozilla::dom::Element* nsINode::AsElement() {
  MOZ_ASSERT(IsElement());
  return static_cast<mozilla::dom::Element*>(this);
}

inline const mozilla::dom::Element* nsINode::AsElement() const {
  MOZ_ASSERT(IsElement());
  return static_cast<const mozilla::dom::Element*>(this);
}

inline mozilla::dom::Element* nsINode::GetParentElement() const {
  return mozilla::dom::Element::FromNodeOrNull(mParent);
}

inline mozilla::dom::Element* nsINode::GetPreviousElementSibling() const {
  auto* parent = GetParentNode();
  if (!parent || !parent->HasFlag(NODE_MAY_HAVE_ELEMENT_CHILDREN)) {
    return nullptr;
  }
  nsIContent* previousSibling = GetPreviousSibling();
  while (previousSibling) {
    if (previousSibling->IsElement()) {
      return previousSibling->AsElement();
    }
    previousSibling = previousSibling->GetPreviousSibling();
  }

  return nullptr;
}

inline mozilla::dom::ShadowRoot* nsINode::GetShadowRoot() const {
  return IsElement() ? AsElement()->GetShadowRoot() : nullptr;
}

inline mozilla::dom::Element* nsINode::GetAsElementOrParentElement() const {
  return IsElement() ? const_cast<mozilla::dom::Element*>(AsElement())
                     : GetParentElement();
}

inline mozilla::dom::Element* nsINode::GetNextElementSibling() const {
  auto* parent = GetParentNode();
  if (!parent || !parent->HasFlag(NODE_MAY_HAVE_ELEMENT_CHILDREN)) {
    return nullptr;
  }
  nsIContent* nextSibling = GetNextSibling();
  while (nextSibling) {
    if (nextSibling->IsElement()) {
      return nextSibling->AsElement();
    }
    nextSibling = nextSibling->GetNextSibling();
  }

  return nullptr;
}

/**
 * Macros to implement Clone(). _elementName is the class for which to implement
 * Clone.
 */
#define NS_IMPL_ELEMENT_CLONE(_elementName, ...)                    \
  nsresult _elementName::Clone(mozilla::dom::NodeInfo* aNodeInfo,   \
                               nsINode** aResult) const {           \
    *aResult = nullptr;                                             \
    RefPtr<_elementName> it = new (aNodeInfo->NodeInfoManager())    \
        _elementName(do_AddRef(aNodeInfo), ##__VA_ARGS__);          \
    nsresult rv = const_cast<_elementName*>(this)->CopyInnerTo(it); \
    if (NS_SUCCEEDED(rv)) {                                         \
      it.forget(aResult);                                           \
    }                                                               \
    return rv;                                                      \
  }

#define NS_IMPL_ELEMENT_CLONE_WITH_INIT(_elementName, ...)           \
  nsresult _elementName::Clone(mozilla::dom::NodeInfo* aNodeInfo,    \
                               nsINode** aResult) const {            \
    *aResult = nullptr;                                              \
    RefPtr<_elementName> it = new (aNodeInfo->NodeInfoManager())     \
        _elementName(do_AddRef(aNodeInfo), ##__VA_ARGS__);           \
    nsresult rv = it->Init();                                        \
    nsresult rv2 = const_cast<_elementName*>(this)->CopyInnerTo(it); \
    if (NS_FAILED(rv2)) {                                            \
      rv = rv2;                                                      \
    }                                                                \
    if (NS_SUCCEEDED(rv)) {                                          \
      it.forget(aResult);                                            \
    }                                                                \
    return rv;                                                       \
  }

#define NS_IMPL_ELEMENT_CLONE_WITH_INIT_AND_PARSER(_elementName) \
  NS_IMPL_ELEMENT_CLONE_WITH_INIT(_elementName, NOT_FROM_PARSER)

#endif  // mozilla_dom_Element_h_
