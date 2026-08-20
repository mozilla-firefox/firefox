/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_shadowroot_h_
#define mozilla_dom_shadowroot_h_

#include "mozilla/BindgenUniquePtr.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentOrShadowRoot.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsStubMutationObserver.h"
#include "nsTHashtable.h"

class nsAtom;
class nsIContent;
class nsIPrincipal;

enum class CustomElementRegistryState : uint8_t;

namespace mozilla {

struct StyleAuthorStyles;
struct StyleRuleChange;

class EventChainPreVisitor;
class ServoStyleRuleMap;

enum class StyleRuleChangeKind : uint32_t;
enum class BuiltInStyleSheet : uint8_t;

namespace css {
class Rule;
}

namespace dom {

class CSSImportRule;
class CustomElementRegistry;
class Element;
class HTMLInputElement;
class OwningTrustedHTMLOrNullIsEmptyString;
class TrustedHTMLOrString;
class TrustedHTMLOrNullIsEmptyString;

#define SHADOW_ROOT_FLAG_BIT(n_) \
  NODE_FLAG_BIT(NODE_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// ShadowRoot-specific flags
enum : uint32_t {
  // Mode: open (0) or closed (1)
  SHADOW_ROOT_MODE_CLOSED = SHADOW_ROOT_FLAG_BIT(0),

  // Whether focus is delegated
  SHADOW_ROOT_DELEGATES_FOCUS = SHADOW_ROOT_FLAG_BIT(1),

  // Slot assignment: named (0) or manual (1)
  SHADOW_ROOT_SLOT_ASSIGNMENT_MANUAL = SHADOW_ROOT_FLAG_BIT(2),

  // https://dom.spec.whatwg.org/#shadowroot-declarative
  SHADOW_ROOT_IS_DECLARATIVE = SHADOW_ROOT_FLAG_BIT(3),

  // https://dom.spec.whatwg.org/#shadowroot-clonable
  SHADOW_ROOT_IS_CLONABLE = SHADOW_ROOT_FLAG_BIT(4),

  // https://dom.spec.whatwg.org/#shadowroot-serializable
  SHADOW_ROOT_IS_SERIALIZABLE = SHADOW_ROOT_FLAG_BIT(5),

  // https://dom.spec.whatwg.org/#shadowroot-available-to-element-internals
  SHADOW_ROOT_IS_AVAILABLE_TO_ELEMENT_INTERNALS = SHADOW_ROOT_FLAG_BIT(6),

  // Whether the host element overrides slot dispatch (GetSlotNameFor,
  // OnChildBeforeSlotted, OnChildUnslotted). When unset, the common-case
  // default logic is inlined to avoid virtual calls.
  SHADOW_ROOT_HAS_CUSTOM_SLOT_DISPATCH = SHADOW_ROOT_FLAG_BIT(7),

  // 2-bit field encoding the shadow root's custom element registry state.
  // See CustomElementRegistryState for the possible values.
  SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT = SHADOW_ROOT_FLAG_BIT(8),
  SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_MASK =
      SHADOW_ROOT_FLAG_BIT(8) | SHADOW_ROOT_FLAG_BIT(9),

  // https://dom.spec.whatwg.org/#shadowroot-keep-custom-element-registry-null
  // Only meaningful when the registry state is Null: distinguishes a shadow
  // root that must retain a null registry across adoption from one whose null
  // registry should be re-pointed at the new document's global registry.
  SHADOWROOT_KEEP_CUSTOM_ELEMENT_REGISTRY_NULL = SHADOW_ROOT_FLAG_BIT(10),

  // Remaining bits are unused
  SHADOW_ROOT_FLAGS_BITS_USED = 11
};

#undef SHADOW_ROOT_FLAG_BIT

// Make sure we have space for our bits
ASSERT_NODE_FLAGS_SPACE(NODE_TYPE_SPECIFIC_BITS_OFFSET +
                        SHADOW_ROOT_FLAGS_BITS_USED);

class ShadowRoot final : public DocumentFragment, public DocumentOrShadowRoot {
  friend class DocumentOrShadowRoot;

  using Declarative = Element::ShadowRootDeclarative;
  using IsClonable = Element::ShadowRootClonable;
  using IsSerializable = Element::ShadowRootSerializable;

  using CustomSlotDispatch = Element::CustomSlotDispatch;

 public:
  NS_IMPL_FROMNODE_HELPER(ShadowRoot, IsShadowRoot());

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ShadowRoot, DocumentFragment)
  NS_DECL_ISUPPORTS_INHERITED

  // Part of https://dom.spec.whatwg.org/#concept-attach-a-shadow-root step 5
  ShadowRoot(Element* aElement, ShadowRootMode aMode,
             Element::DelegatesFocus aDelegatesFocus,
             SlotAssignmentMode aSlotAssignment, IsClonable aClonable,
             IsSerializable aIsSerializable, Declarative aDeclarative,
             CustomSlotDispatch aCustomSlotDispatch,
             const Maybe<RefPtr<CustomElementRegistry>> aRegistry,
             already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  void AddSizeOfExcludingThis(nsWindowSizes&, size_t* aNodeSize) const final;

  // Try to reassign an element or text to a slot.
  void MaybeReassignContent(nsIContent& aElementOrText);
  // Called when an element is inserted as a direct child of our host. Tries to
  // slot the child in one of our slots.
  void MaybeSlotHostChild(nsIContent&);
  // Called when a direct child of our host is removed. Tries to un-slot the
  // child from the currently-assigned slot, if any.
  void MaybeUnslotHostChild(nsIContent&);

  // Shadow DOM v1
  Element* Host() const {
    MOZ_ASSERT(GetHost(),
               "ShadowRoot always has a host, how did we create "
               "this ShadowRoot?");
    return GetHost();
  }

  ShadowRootMode Mode() const {
    return HasFlag(SHADOW_ROOT_MODE_CLOSED) ? ShadowRootMode::Closed
                                            : ShadowRootMode::Open;
  }
  bool DelegatesFocus() const { return HasFlag(SHADOW_ROOT_DELEGATES_FOCUS); }
  bool HasCustomSlotDispatch() const {
    return HasFlag(SHADOW_ROOT_HAS_CUSTOM_SLOT_DISPATCH);
  }
  SlotAssignmentMode SlotAssignment() const {
    return HasFlag(SHADOW_ROOT_SLOT_ASSIGNMENT_MANUAL)
               ? SlotAssignmentMode::Manual
               : SlotAssignmentMode::Named;
  }
  bool Clonable() const { return HasFlag(SHADOW_ROOT_IS_CLONABLE); }
  bool IsClosed() const { return HasFlag(SHADOW_ROOT_MODE_CLOSED); }
  bool Serializable() const { return HasFlag(SHADOW_ROOT_IS_SERIALIZABLE); }

  void RemoveSheetFromStyles(StyleSheet&);
  void RuleAdded(StyleSheet&, css::Rule&);
  void RuleRemoved(StyleSheet&, css::Rule&);
  void RuleChanged(StyleSheet&, css::Rule*, const StyleRuleChange&);
  void ImportRuleLoaded(StyleSheet&);
  void SheetCloned(StyleSheet&);
  void StyleSheetApplicableStateChanged(StyleSheet&);

  // Adds a built-in author style-sheet to the shadow tree.
  void AppendBuiltInStyleSheet(BuiltInStyleSheet);

  /**
   * Clones internal state, for example stylesheets, of aOther to 'this'.
   */
  void CloneInternalDataFrom(ShadowRoot* aOther);
  void InsertSheetAt(size_t aIndex, StyleSheet&);

  // Calls UnbindFromTree for each of our kids, and also flags us as no longer
  // being connected.
  void Unbind();

  // Only intended for UA widgets / special shadow roots, or for handling
  // failure cases when adopting (see BlastSubtreeToPieces).
  //
  // Forgets our shadow host and unbinds all our kids.
  void Unattach();

  // Calls BindToTree on each of our kids, and also maybe flags us as being
  // connected.
  nsresult Bind();

  /**
   * Explicitly invalidates the style and layout of the flattened-tree subtree
   * rooted at the element.
   *
   * You need to use this whenever the flat tree is going to be shuffled in a
   * way that layout doesn't understand via the usual ContentInserted /
   * ContentAppended / ContentRemoved notifications. For example, if removing an
   * element will cause a change in the flat tree such that other element will
   * start showing up (like fallback content), this method needs to be called on
   * an ancestor of that element.
   *
   * It is important that this runs _before_ actually shuffling the flat tree
   * around, so that layout knows the actual tree that it needs to invalidate.
   */
  static void InvalidateStyleAndLayoutOnSubtree(Element*);

 private:
  void InsertSheetIntoAuthorData(size_t aIndex, StyleSheet&,
                                 const nsTArray<RefPtr<StyleSheet>>&);

  void AppendStyleSheet(StyleSheet& aSheet) {
    InsertSheetAt(SheetCount(), aSheet);
  }

  /**
   * Represents the insertion point in a slot for a given node.
   */
  struct SlotInsertionPoint {
    HTMLSlotElement* mSlot = nullptr;
    Maybe<uint32_t> mIndex;

    SlotInsertionPoint() = default;
    SlotInsertionPoint(HTMLSlotElement* aSlot, const Maybe<uint32_t>& aIndex)
        : mSlot(aSlot), mIndex(aIndex) {}
  };

  /**
   * Return the assignment corresponding to the content node at this particular
   * point in time.
   *
   * It's the caller's responsibility to actually call InsertAssignedNode /
   * AppendAssignedNode in the slot as needed.
   */
  SlotInsertionPoint SlotInsertionPointFor(nsIContent&);

  /**
   * Returns the effective slot name for a given slottable. In most cases, this
   * is just the value of the slot attribute, if any, or the empty string, but
   * this also deals with the <details> shadow tree specially.
   */
  void GetSlotNameFor(const nsIContent&, nsAString&) const;

 public:
  void AddSlot(HTMLSlotElement* aSlot);
  void RemoveSlot(HTMLSlotElement* aSlot);
  bool HasSlots() const { return !mSlotMap.IsEmpty(); };
  HTMLSlotElement* GetFirstNamedSlot(const nsAString& aName) const {
    SlotArray* list = mSlotMap.Get(aName);
    return list ? list->ElementAt(0) : nullptr;
  }

  void PartAdded(const Element&);
  void PartRemoved(const Element&);

  IMPL_EVENT_HANDLER(slotchange);

  const nsTArray<const Element*>& Parts() const { return mParts; }

  const StyleAuthorStyles* GetServoStyles() const { return mServoStyles.get(); }

  StyleAuthorStyles* GetServoStyles() { return mServoStyles.get(); }

  mozilla::ServoStyleRuleMap& ServoStyleRuleMap();

  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) final;

  void NodeInfoChanged(Document* aOldDoc) override;

  void AddToIdTable(Element* aElement, nsAtom* aId);
  void RemoveFromIdTable(Element* aElement, nsAtom* aId);

  // WebIDL methods.
  using mozilla::dom::DocumentOrShadowRoot::GetElementById;

  Element* GetActiveElement();

  /**
   * These methods allow UA Widget to insert DOM elements into the Shadow ROM
   * without putting their DOM reflectors to content scope first.
   * The inserted DOM will have their reflectors in the UA Widget scope.
   */
  nsINode* ImportNodeAndAppendChildAt(nsINode& aParentNode, nsINode& aNode,
                                      bool aDeep, mozilla::ErrorResult& rv);

  nsINode* CreateElementAndAppendChildAt(nsINode& aParentNode,
                                         const nsAString& aTagName,
                                         mozilla::ErrorResult& rv);

  bool IsUAWidget() const { return HasBeenInUAWidget(); }

  void SetIsUAWidget() {
    MOZ_ASSERT(!HasChildren());
    SetIsNativeAnonymousRoot();
    SetFlags(NODE_HAS_BEEN_IN_UA_WIDGET);
  }

  bool IsAvailableToElementInternals() const {
    return HasFlag(SHADOW_ROOT_IS_AVAILABLE_TO_ELEMENT_INTERNALS);
  }

  void SetAvailableToElementInternals() {
    SetFlags(SHADOW_ROOT_IS_AVAILABLE_TO_ELEMENT_INTERNALS);
  }

  CustomElementRegistryState GetCustomElementRegistryState() const {
    return static_cast<CustomElementRegistryState>(
        (GetFlags() & SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_MASK) /
        SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT);
  }
  void SetCustomElementRegistryState(CustomElementRegistryState aState) {
    UnsetFlags(SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_MASK);
    SetFlags(static_cast<uint32_t>(aState) *
             SHADOWROOT_CUSTOM_ELEMENT_REGISTRY_LOW_BIT);
  }

  bool HasCustomElementRegistry() const {
    return GetCustomElementRegistryState() !=
           CustomElementRegistryState::Global;
  }

  void SetCustomElementRegistry(CustomElementRegistry* aRegistry);
  // Sets the shadow root's custom element registry to null.
  // https://dom.spec.whatwg.org/#concept-attach-a-shadow-root step 12
  void SetNullCustomElementRegistry() {
    SetCustomElementRegistryState(CustomElementRegistryState::Null);
  }
  // Sets keep custom element registry null to true. Only meaningful once the
  // registry is null.
  // https://dom.spec.whatwg.org/#shadowroot-keep-custom-element-registry-null
  void SetKeepCustomElementRegistryNull() {
    MOZ_ASSERT(IsDeclarative(),
               "KeepCustomElementRegistryNull should only be set on "
               "Declarative roots!");
    MOZ_ASSERT(StaticPrefs::dom_scoped_custom_element_registries_enabled());
    MOZ_ASSERT(
        GetCustomElementRegistryState() == CustomElementRegistryState::Null,
        "keep custom element registry null is only meaningful when the "
        "registry is null");
    SetFlags(SHADOWROOT_KEEP_CUSTOM_ELEMENT_REGISTRY_NULL);
  }
  bool KeepCustomElementRegistryNull() const {
    return HasFlag(SHADOWROOT_KEEP_CUSTOM_ELEMENT_REGISTRY_NULL);
  }
  // https://dom.spec.whatwg.org/#shadowroot-custom-element-registry
  CustomElementRegistry* GetCustomElementRegistry() const;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;

  bool IsDeclarative() const { return HasFlag(SHADOW_ROOT_IS_DECLARATIVE); }
  void SetIsDeclarative(Declarative aIsDeclarative) {
    SetIsDeclarative(aIsDeclarative == Declarative::Yes);
  }
  void SetIsDeclarative(bool aIsDeclarative) {
    if (aIsDeclarative) {
      SetFlags(SHADOW_ROOT_IS_DECLARATIVE);
    } else {
      UnsetFlags(SHADOW_ROOT_IS_DECLARATIVE);
    }
  }

  void SetHTML(const nsAString& aInnerHTML, const SetHTMLOptions& aOptions,
               ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void SetHTMLUnsafe(const TrustedHTMLOrString& aHTML,
                     const SetHTMLUnsafeOptions& aOptions,
                     nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  // @param aInnerHTML will always be of type `NullIsEmptyString`.
  void GetInnerHTML(OwningTrustedHTMLOrNullIsEmptyString& aInnerHTML);

  MOZ_CAN_RUN_SCRIPT void SetInnerHTML(
      const TrustedHTMLOrNullIsEmptyString& aInnerHTML,
      nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);

  void GetHTML(const GetHTMLOptions& aOptions, nsAString& aResult);

  bool HasReferenceTarget() const { return mReferenceTarget; }
  void GetReferenceTarget(nsAString& aResult) const {
    if (!mReferenceTarget) {
      aResult.SetIsVoid(true);
      return;
    }
    mReferenceTarget->ToString(aResult);
  }
  nsAtom* ReferenceTarget() const { return mReferenceTarget; }
  void SetReferenceTarget(const nsAString& aValue) {
    if (aValue.IsVoid()) {
      return SetReferenceTarget(nullptr);
    }
    SetReferenceTarget(NS_Atomize(aValue));
  }
  void SetReferenceTarget(RefPtr<nsAtom> aTarget);
  Element* GetReferenceTargetElement() const {
    return mReferenceTarget ? GetElementById(mReferenceTarget) : nullptr;
  }

 protected:
  // FIXME(emilio): This will need to become more fine-grained.
  void ApplicableRulesChanged();

  virtual ~ShadowRoot();

  // Make sure that the first field is pointer-aligned so it doesn't get packed
  // in the base class' padding, since otherwise rust-bindgen can't generate
  // correct bindings for it, see
  // https://github.com/rust-lang/rust-bindgen/issues/380

  // The computed data from the style sheets.
  BindgenUniquePtr<StyleAuthorStyles> mServoStyles;
  UniquePtr<mozilla::ServoStyleRuleMap> mStyleRuleMap;

  using SlotArray = TreeOrderedArray<HTMLSlotElement*>;
  // Map from name of slot to an array of all slots in the shadow DOM with with
  // the given name. The slots are stored as a weak pointer because the elements
  // are in the shadow tree and should be kept alive by its parent.
  nsClassHashtable<nsStringHashKey, SlotArray> mSlotMap;

  // Unordered array of all elements that have a part attribute in this shadow
  // tree.
  nsTArray<const Element*> mParts;

  RefPtr<nsAtom> mReferenceTarget;

  static bool ReferenceTargetIDTargetChanged(Element* aOldElement,
                                             Element* aNewElement, void* aData);
  static bool RecursiveReferenceTargetChanged(void* aData);

  void NotifyReferenceTargetChangedObservers();

  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_shadowroot_h_
