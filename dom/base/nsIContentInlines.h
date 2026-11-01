/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIContentInlines_h
#define nsIContentInlines_h

#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "nsAtom.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsINode.h"

inline bool nsINode::HasScopedRegistry() const {
  if (!mozilla::StaticPrefs::dom_scoped_custom_element_registries_enabled()) {
    return false;
  }
  bool isScoped = false;
  if (IsElement()) {
    isScoped = AsElement()->GetCustomElementRegistryState() ==
               CustomElementRegistryState::Scoped;
  } else if (const auto* shadowRoot =
                 mozilla::dom::ShadowRoot::FromNode(this)) {
    isScoped = shadowRoot->GetCustomElementRegistryState() ==
               CustomElementRegistryState::Scoped;
  } else if (IsDocument()) {
    isScoped = AsDocument()->HasScopedCustomElementRegistry();
  }
  MOZ_ASSERT(
      isScoped == mozilla::dom::CustomElementRegistry::IsInScopedRegistryMap(
                      const_cast<nsINode&>(*this)),
      "scoped-registry check disagrees with the registry map");
  return isScoped;
}

inline bool nsIContent::IsInHTMLDocument() const {
  return OwnerDoc()->IsHTMLDocument();
}

inline bool nsIContent::IsInChromeDocument() const {
  return nsContentUtils::IsChromeDoc(OwnerDoc());
}

inline void nsIContent::SetPrimaryFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame || IsInUncomposedDoc() || IsInShadowTree(),
             "This will end badly!");
  MOZ_ASSERT(!aFrame || IsInComposedDoc(), "This will end badly!");

  MOZ_ASSERT(!aFrame || !mPrimaryFrame || aFrame == mPrimaryFrame,
             "Losing track of existing primary frame");

  if (aFrame) {
    MOZ_ASSERT(!aFrame->IsPlaceholderFrame());
    aFrame->SetIsPrimaryFrame(true);
  } else if (nsIFrame* currentPrimaryFrame = GetPrimaryFrame()) {
    currentPrimaryFrame->SetIsPrimaryFrame(false);
  }

  mPrimaryFrame = aFrame;
}

/**
 * Return the parent node of aNode in the specified tree. If aNode is the root
 * of a native anonymous subtree, this returns the parent as-is.
 *
 * When aType is eNormal, return the parent node in the flattened tree. If the
 * parent node of aNode is a shadow host, return its assigned <slot> or nullptr.
 * If the parent node is a <slot> in a shadow, return nullptr if the <slot> has
 * some assigned nodes or otherwise, the <slot> (i.e., when the node is a used
 * fallback content). If the parent node is a shadow root, return its host
 * element.
 *
 * When aType is eForStyle, return the almost same value as when aType is
 * eNormal. However, if aNode is the root of a native anonymous subtree and the
 * parent node in the DOM is the document element, return the document.
 *
 * When aType is eForSelection, return the parent node in the DOM even if aNode
 * is not a part of the flattened tree, e.g., it is a child node of a shadow
 * host but is not assigned to any <slot>, or it is a child node of a <slot>
 * which has some assigned nodes. The reason is, Selection API accepts any node
 * in the DOM as the container. On the other hand, UA shadow tree such as a
 * shadow for <details>, <video> or <audio> should not be treated as a shadow.
 * Therefore, if the parent node is a shadow root of a UA shadow DOM, return the
 * shadow root instead of the host.
 */
template <nsINode::FlattenedParentType aType>
static inline nsINode* GetFlattenedTreeParentNode(const nsINode* aNode) {
  if (!aNode->IsContent()) {
    return nullptr;
  }

  nsINode* parent = aNode->GetParentNode();
  if (!parent || !parent->IsContent()) {
    return parent;
  }

  const nsIContent* content = aNode->AsContent();
  nsIContent* parentAsContent = parent->AsContent();

  if (aType == nsINode::eForStyle &&
      content->IsRootOfNativeAnonymousSubtree() &&
      parentAsContent == content->OwnerDoc()->GetRootElement()) {
    const bool docLevel =
        content->GetProperty(nsGkAtoms::docLevelNativeAnonymousContent);
    return docLevel ? content->OwnerDocAsNode() : parent;
  }

  if (content->IsRootOfNativeAnonymousSubtree()) {
    return parent;
  }

  // Use GetShadowRootForSelection for the selection case such that
  // if the content is slotted into a non-content shadow tree, use
  // the parent of content as the flattened tree parent (instead of
  // the slot element).
  const nsINode* shadowRootForParent =
      aType == nsINode::eForSelection
          ? parentAsContent->GetShadowRootForSelection()
          : parentAsContent->GetShadowRoot();

  if (shadowRootForParent) {
    // When aType is not nsINode::eForSelection, If it's not assigned to any
    // slot it's not part of the flat tree, and thus we return null.
    auto* assignedSlot = content->GetAssignedSlot();
    if (assignedSlot || aType != nsINode::eForSelection) {
      return assignedSlot;
    }

    MOZ_ASSERT(aType == nsINode::eForSelection);
    // When aType is nsINode::eForSelection, we use the parent of the
    // content even if it's not assigned to any slot because Selection API
    // accepts any node in the DOM as a container of a range boundary.
    return parent;
  }

  if (parentAsContent->IsInShadowTree()) {
    if (auto* slot = mozilla::dom::HTMLSlotElement::FromNode(parentAsContent)) {
      if constexpr (aType == nsINode::eForSelection) {
        // Even if the parent node is a <slot> in any shadow (for either content
        // or UA such as <details>, <video>, <audio>, we can return the <slot>
        // as parent for nsINode::eForSelection because any node in the DOM can
        // be a container of a range boundary. E.g., even if the <slot> has some
        // assigned nodes, the fallback content can be selected by the Selection
        // API. So, we should not treat the unused fallback content as
        // disconnected from the flattened tree.
        return slot;
      } else {
        // If the parent is a <slot> and its assigned nodes list is empty, aNode
        // is a fallback content which is active, otherwise we are not part of
        // the flattened tree.
        return slot->AssignedNodes().IsEmpty() ? slot : nullptr;
      }
    }

    if (auto* const shadowRoot =
            mozilla::dom::ShadowRoot::FromNode(parentAsContent)) {
      if constexpr (aType != nsINode::eForSelection) {
        return shadowRoot->GetHost();
      } else {
        // If the parent is a shadow root for UA widget, we shouldn't cross the
        // boundary with a selection range. Therefore, we should treat the UA
        // shadow root as the root.
        if (shadowRoot->IsUAWidget()) {
          return shadowRoot;
        }
        // Even if the shadow root is not for a UA widget, the shadow root may
        // be a UA shadow root. Currently, the only case is SVG <use>. It
        // creates a closed shadow to clone the referring elements. We want to
        // treat the shadow is flattened in the flat tree for selection.
        MOZ_ASSERT_IF(shadowRoot->GetHost() &&
                          !shadowRoot->GetHost()->CanAttachShadowDOM(),
                      shadowRoot->GetHost()->IsSVGElement(nsGkAtoms::use));
        return shadowRoot->GetHost();
      }
    }
  }

  // Common case.
  return parent;
}

inline nsINode* nsINode::GetFlattenedTreeParentNode() const {
  return ::GetFlattenedTreeParentNode<nsINode::eNormal>(this);
}

inline nsIContent* nsIContent::GetFlattenedTreeParent() const {
  nsINode* parent = GetFlattenedTreeParentNode();
  return (parent && parent->IsContent()) ? parent->AsContent() : nullptr;
}

inline bool nsIContent::IsEventAttributeName(nsAtom* aName) {
  const char16_t* name = aName->GetUTF16String();
  if (name[0] != 'o' || name[1] != 'n') {
    return false;
  }

  return IsEventAttributeNameInternal(aName);
}

inline nsINode* nsINode::GetFlattenedTreeParentNodeForStyle() const {
  return ::GetFlattenedTreeParentNode<nsINode::eForStyle>(this);
}

inline nsIContent* nsINode::GetFlattenedTreeParentForStyle() const {
  return nsIContent::FromNodeOrNull(GetFlattenedTreeParentNodeForStyle());
}

inline nsINode* nsINode::GetFlattenedTreeParentNodeForSelection() const {
  return ::GetFlattenedTreeParentNode<nsINode::eForSelection>(this);
}

inline nsIContent* nsINode::GetFlattenedTreeFirstChild() const {
  return mozilla::dom::FlattenedChildIterator::GetFirstChild(this);
}

inline nsIContent* nsINode::GetFlattenedTreeFirstChildForSelection() const {
  return mozilla::dom::FlattenedChildIteratorForSelection::GetFirstChild(this);
}

inline nsIContent* nsINode::GetFlattenedTreeLastChild() const {
  return mozilla::dom::FlattenedChildIterator::GetLastChild(this);
}

inline nsIContent* nsINode::GetFlattenedTreeLastChildForSelection() const {
  return mozilla::dom::FlattenedChildIteratorForSelection::GetLastChild(this);
}

inline uint32_t nsINode::GetFlatTreeChildCount() const {
  if (!IsContainerNode()) {
    return 0;
  }
  MOZ_ASSERT(!IsCharacterData());
  return mozilla::dom::FlattenedChildIterator::GetLength(this);
}

inline uint32_t nsINode::GetFlatTreeForSelectionChildCount() const {
  if (!IsContainerNode()) {
    return 0;
  }
  MOZ_ASSERT(!IsCharacterData());
  return mozilla::dom::FlattenedChildIteratorForSelection::GetLength(this);
}

inline mozilla::Maybe<uint32_t> nsINode::ComputeFlatTreeIndexOf(
    const nsINode* aPossibleChild) const {
  return mozilla::dom::FlattenedChildIterator::GetIndexOf(this, aPossibleChild);
}

inline mozilla::Maybe<uint32_t> nsINode::ComputeFlatTreeForSelectionIndexOf(
    const nsINode* aPossibleChild) const {
  return mozilla::dom::FlattenedChildIteratorForSelection::GetIndexOf(
      this, aPossibleChild);
}

inline nsIContent* nsINode::GetChildAtInFlatTree(uint32_t aIndex) const {
  return mozilla::dom::FlattenedChildIterator::GetChildAt(this, aIndex);
}

inline nsIContent* nsINode::GetChildAtInFlatTreeForSelection(
    uint32_t aIndex) const {
  return mozilla::dom::FlattenedChildIteratorForSelection::GetChildAt(this,
                                                                      aIndex);
}

inline mozilla::dom::ShadowRoot* nsINode::GetContainingShadowForSelection()
    const {
  if (!IsInShadowTree()) {
    return nullptr;
  }
  mozilla::dom::ShadowRoot* const shadowRoot =
      AsContent()->GetContainingShadow();
  return shadowRoot && !shadowRoot->IsUAWidget() ? shadowRoot : nullptr;
}

inline mozilla::dom::ShadowRoot* nsINode::GetClosestShadowRootInFlattenedTree()
    const {
  for (nsINode* node = const_cast<nsINode*>(this); node && node->IsContent();
       node = node->GetParentNode()) {
    // If this node is an inclusive descendant of a shadow root, return it.
    if (auto* const shadowRoot = mozilla::dom::ShadowRoot::FromNode(node)) {
      return shadowRoot;
    }
    // If this node is an inclusive descendant of an assigned node, return the
    // containing shadow of the <slot>.
    if (auto* const slot = node->AsContent()->GetAssignedSlot()) {
      return slot->GetContainingShadow();
    }
  }
  return nullptr;
}

inline mozilla::dom::ShadowRoot*
nsINode::GetClosestShadowRootInFlattenedTreeForSelection() const {
  for (nsINode* node = const_cast<nsINode*>(this); node && node->IsContent();
       node = node->GetParentNode()) {
    // If this node is an inclusive descendant of a shadow root, return it.
    if (auto* const shadowRoot = mozilla::dom::ShadowRoot::FromNode(node)) {
      if (!shadowRoot->IsUAWidget()) {
        return shadowRoot;
      }
    }
    // If this node is an inclusive descendant of an assigned node, return the
    // containing shadow of the <slot>.
    if (auto* const slot = node->AsContent()->GetAssignedSlotForSelection()) {
      return slot->GetContainingShadow();
    }
  }
  return nullptr;
}

inline bool nsINode::NodeOrAncestorHasDirAuto() const {
  return AncestorHasDirAuto() || (IsElement() && AsElement()->HasDirAuto());
}

inline bool nsINode::IsEditingHost() const {
  if (!IsEditable() || !IsInComposedDoc() || IsInDesignMode() ||
      IsInNativeAnonymousSubtree()) {
    return false;
  }
  nsIContent* const parent = GetParent();
  return !parent ||  // The root element (IsInComposedDoc() is checked above)
         !parent->IsEditable();  // or an editable node in a non-editable one
}

inline bool nsINode::IsInDesignMode() const {
  if (!OwnerDoc()->HasFlag(NODE_IS_EDITABLE)) {
    return false;
  }

  // NOTE(emilio): If you change this to be the composed doc you also need to
  // change NotifyEditableStateChange() in Document.cpp.
  // NOTE(masayuki): Perhaps, we should keep this behavior because of
  // web-compat.
  if (IsInUncomposedDoc()) {
    return true;
  }

  // FYI: In design mode, form controls don't work as usual.  For example,
  //      <input type=text> isn't focusable but can be deleted and replaced
  //      with typed text. <select> is also not focusable but always selected
  //      all to be deleted or replaced.  On the other hand, newer controls
  //      don't behave as the traditional controls.  For example, data/time
  //      picker can be opened and change the value from the picker.  And also
  //      the buttons of <video controls> work as usual.  On the other hand,
  //      their UI (i.e., nodes in their shadow tree) are not editable.
  //      Therefore, we need special handling for nodes in anonymous subtree
  //      unless we fix <https://bugzilla.mozilla.org/show_bug.cgi?id=1734512>.

  // If the shadow host is not in design mode, this can never be in design
  // mode.  Otherwise, the content is never editable by design mode of
  // composed document. If we're in a native anonymous subtree, we should
  // consider it with the host.
  if (IsInNativeAnonymousSubtree()) {
    nsIContent* host = GetClosestNativeAnonymousSubtreeRootParentOrHost();
    MOZ_DIAGNOSTIC_ASSERT(host != this);
    return host && host->IsInDesignMode();
  }

  // Otherwise, i.e., when it's in a shadow tree which is not created by us,
  // the node is not editable by design mode (but it's possible that it may be
  // editable if this node is in `contenteditable` element in the shadow tree).
  return false;
}

inline void nsIContent::HandleInsertionToOrRemovalFromSlot() {
  using mozilla::dom::HTMLSlotElement;

  MOZ_ASSERT(GetParentElement());
  if (!IsInShadowTree() || IsRootOfNativeAnonymousSubtree()) {
    return;
  }
  HTMLSlotElement* slot = HTMLSlotElement::FromNode(mParent);
  if (!slot) {
    return;
  }
  // If parent's root is a shadow root, and parent is a slot whose
  // assigned nodes is the empty list, then run signal a slot change for
  // parent.
  if (slot->AssignedNodes().IsEmpty()) {
    slot->EnqueueSlotChangeEvent();
  }
}

inline void nsIContent::HandleShadowDOMRelatedInsertionSteps(bool aHadParent) {
  using mozilla::dom::Element;
  using mozilla::dom::ShadowRoot;

  if (!aHadParent) {
    if (Element* parentElement = Element::FromNode(mParent)) {
      if (ShadowRoot* shadow = parentElement->GetShadowRoot()) {
        shadow->MaybeSlotHostChild(*this);
      }
      HandleInsertionToOrRemovalFromSlot();
    }
  }
}

inline void nsIContent::HandleShadowDOMRelatedRemovalSteps(bool aNullParent) {
  using mozilla::dom::Element;
  using mozilla::dom::ShadowRoot;

  if (aNullParent) {
    // FIXME(emilio, bug 1577141): FromNodeOrNull rather than just FromNode
    // because frame destruction likes to call UnbindFromTree at very odd times
    // (with already disconnected anonymous content subtrees).
    if (Element* parentElement = Element::FromNodeOrNull(mParent)) {
      if (ShadowRoot* shadow = parentElement->GetShadowRoot()) {
        shadow->MaybeUnslotHostChild(*this);
      }
      HandleInsertionToOrRemovalFromSlot();
    }
  }
}

#endif  // nsIContentInlines_h
