/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsINode_h_
#define nsINode_h_

#include <fmt/format.h>

#include <iosfwd>

#include "js/TypeDecls.h"  // for Handle, Value, JSObject, JSContext
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/DOMString.h"
#include "mozilla/dom/EventTarget.h"  // for base class
#include "mozilla/dom/NodeInfo.h"     // member (in nsCOMPtr)
#include "nsCOMPtr.h"                 // for member, local
#include "nsGkAtoms.h"                // for nsGkAtoms::baseURIProperty
#include "nsIMutationObserver.h"
#include "nsIWeakReference.h"
#include "nsNodeInfoManager.h"  // for use in NodePrincipal()
#include "nsPropertyTable.h"    // for typedefs

// Including 'windows.h' will #define GetClassInfo to something else.
#ifdef XP_WIN
#  ifdef GetClassInfo
#    undef GetClassInfo
#  endif
#endif

class AttrArray;
class nsAttrChildContentList;
template <typename T>
class nsCOMArray;
class nsDOMAttributeMap;
class nsFrameSelection;
class nsGenericHTMLElement;
class nsIAnimationObserver;
class nsIContent;
class nsIContentSecurityPolicy;
class nsIFrame;
class nsIGlobalObject;
class nsIFormControl;
class nsMultiMutationObserver;
class nsINode;
class nsIPolicyContainer;
class nsIPrincipal;
class nsIURI;
class nsNodeSupportsWeakRefTearoff;
class nsDOMMutationObserver;
class nsWindowSizes;

namespace mozilla {
class EventListenerManager;
struct StyleSelectorList;
template <typename T>
class Maybe;
template <typename T>
class LinkedList;
class ErrorResult;
class PresShell;
class TextEditor;
class WidgetEvent;
namespace dom {
class Range;
class NodeList;
class HTMLCollection;

/**
 * @return true if aChar is what the WHATWG defines as a 'ascii whitespace'.
 * https://infra.spec.whatwg.org/#ascii-whitespace
 */
inline bool IsSpaceCharacter(char16_t aChar) {
  return aChar == ' ' || aChar == '\t' || aChar == '\n' || aChar == '\r' ||
         aChar == '\f';
}
inline bool IsSpaceCharacter(char aChar) {
  return aChar == ' ' || aChar == '\t' || aChar == '\n' || aChar == '\r' ||
         aChar == '\f';
}
class AbstractRange;
class AccessibleNode;
template <typename T>
class AncestorsOfTypeIterator;
struct BoxQuadOptions;
struct ConvertCoordinateOptions;
class CustomElementRegistry;
class DocGroup;
class Document;
class DocumentFragment;
class DocumentOrShadowRoot;
class DOMPoint;
class DOMQuad;
class DOMRectReadOnly;
class Element;
class EventHandlerNonNull;
template <typename T>
class FlatTreeAncestorsOfTypeIterator;
class HTMLDialogElement;
class HTMLSlotElement;
template <typename T>
class InclusiveAncestorsOfTypeIterator;
template <typename T>
class InclusiveFlatTreeAncestorsOfTypeIterator;
class LinkStyle;
class MutationObservers;
template <typename T>
class Optional;
class OwningNodeOrString;
class SelectionNodeCache;
template <typename>
class Sequence;
class ShadowRoot;
class SVGUseElement;
class Text;
class TextOrElementOrDocument;
struct DOMPointInit;
struct GetRootNodeOptions;
enum class AllowRangeCrossShadowBoundary : bool;  // defined in AbstractRange.h
enum class CallerType : uint32_t;
struct AriaNotificationOptions;
}  // namespace dom
}  // namespace mozilla

#define NODE_FLAG_BIT(n_) \
  (nsWrapperCache::FlagsType(1U) << (WRAPPER_CACHE_FLAGS_BITS_USED + (n_)))

enum : uint32_t {
  // This bit will be set if the node has a listener manager.
  NODE_HAS_LISTENERMANAGER = NODE_FLAG_BIT(0),

  // Whether this node has had any properties set on it
  NODE_HAS_PROPERTIES = NODE_FLAG_BIT(1),

  // Whether the node has some ancestor, possibly itself, that is native
  // anonymous.  This includes ancestors crossing XBL scopes, in cases when an
  // XBL binding is attached to an element which has a native anonymous
  // ancestor.  This flag is set-once: once a node has it, it must not be
  // removed.
  // NOTE: Should only be used on nsIContent nodes
  NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE = NODE_FLAG_BIT(2),

  // Whether this node is the root of a native anonymous (from the perspective
  // of its parent) subtree.  This flag is set-once: once a node has it, it
  // must not be removed.
  // NOTE: Should only be used on nsIContent nodes
  NODE_IS_NATIVE_ANONYMOUS_ROOT = NODE_FLAG_BIT(3),

  NODE_IS_EDITABLE = NODE_FLAG_BIT(4),

  // Whether the node participates in a shadow tree.
  NODE_IS_IN_SHADOW_TREE = NODE_FLAG_BIT(5),

  // This node needs to go through frame construction to get a frame (or
  // undisplayed entry).
  NODE_NEEDS_FRAME = NODE_FLAG_BIT(6),

  // At least one descendant in the flattened tree has NODE_NEEDS_FRAME set.
  // This should be set on every node on the flattened tree path between the
  // node(s) with NODE_NEEDS_FRAME and the root content.
  NODE_DESCENDANTS_NEED_FRAMES = NODE_FLAG_BIT(7),

  // Set if the node has the accesskey attribute set.
  NODE_HAS_ACCESSKEY = NODE_FLAG_BIT(8),

  NODE_HAS_BEEN_IN_UA_WIDGET = NODE_FLAG_BIT(9),

  // Set if the node has a nonce value and a header delivered CSP.
  NODE_HAS_NONCE_AND_HEADER_CSP = NODE_FLAG_BIT(10),

  NODE_KEEPS_DOMARENA = NODE_FLAG_BIT(11),

  NODE_MAY_HAVE_ELEMENT_CHILDREN = NODE_FLAG_BIT(12),

  NODE_HAS_SCHEDULED_SELECTION_CHANGE_EVENT = NODE_FLAG_BIT(13),

  // Remaining bits are node type specific.
  NODE_TYPE_SPECIFIC_BITS_OFFSET = 14
};

// Flags for selectors that persist to the DOM node.
enum class NodeSelectorFlags : uint32_t {
  // Node has an :empty or :-moz-only-whitespace selector
  HasEmptySelector = 1 << 0,

  /// A child of the node has a selector such that any insertion,
  /// removal, or appending of children requires restyling the parent, if the
  /// parent is an element. If the parent is the shadow root, the child's
  /// siblings are restyled.
  HasSlowSelector = 1 << 1,

  /// A child of the node has a :first-child, :-moz-first-node,
  /// :only-child, :last-child or :-moz-last-node selector.
  HasEdgeChildSelector = 1 << 2,

  /// A child of the node has a selector such that any insertion or
  /// removal of children requires restyling later siblings of that
  /// element.  Additionally (in this manner it is stronger than
  /// NODE_HAS_SLOW_SELECTOR), if a child's style changes due to any
  /// other content tree changes (e.g., the child changes to or from
  /// matching :empty due to a grandchild insertion or removal), the
  /// child's later siblings must also be restyled.
  HasSlowSelectorLaterSiblings = 1 << 3,

  /// HasSlowSelector* was set by the presence of :nth (But not of).
  HasSlowSelectorNth = 1 << 4,

  /// A child of this node might be matched by :nth-child(.. of <selector>) or
  /// :nth-last-child(.. of <selector>). If a DOM mutation may have caused the
  /// selector to either match or no longer match that child, the child's
  /// siblings are restyled.
  HasSlowSelectorNthOf = 1 << 5,

  /// All instances of :nth flags.
  HasSlowSelectorNthAll = HasSlowSelectorNthOf | HasSlowSelectorNth,

  /// A child of this node may be using sibling-index() or sibling-count(),
  /// and must be recascaded if other children are added or removed.
  MayHaveTreeCountingFunction = 1 << 6,

  /// Set of selector flags that may trigger a restyle on DOM append, with
  /// restyle on siblings or a single parent (And perhaps their subtrees).
  AllSimpleRestyleFlagsForAppend =
      HasEmptySelector | HasSlowSelector | HasEdgeChildSelector |
      HasSlowSelectorNthAll | MayHaveTreeCountingFunction,

  /// Set of selector flags that may trigger a restyle as a result of any
  /// DOM mutation.
  AllSimpleRestyleFlags =
      AllSimpleRestyleFlagsForAppend | HasSlowSelectorLaterSiblings,

  // This node was evaluated as an anchor for a relative selector.
  RelativeSelectorAnchor = 1 << 7,

  // This node was evaluated as an anchor for a relative selector, and that
  // relative selector was not the subject of the overall selector.
  RelativeSelectorAnchorNonSubject = 1 << 8,

  // This node's sibling(s) performed a relative selector search to this node.
  RelativeSelectorSearchDirectionSibling = 1 << 9,

  // This node's ancestor(s) performed a relative selector search to this node.
  RelativeSelectorSearchDirectionAncestor = 1 << 10,

  // This node's sibling(s) and ancestor(s), and/or this node's ancestor's
  // sibling(s) performed a relative selector search to this node.
  RelativeSelectorSearchDirectionAncestorSibling =
      RelativeSelectorSearchDirectionSibling |
      RelativeSelectorSearchDirectionAncestor,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(NodeSelectorFlags);

enum class BatchRemovalOrder {
  FrontToBack,
  BackToFront,
};

struct BatchRemovalState {
  // Whether we're the fist kid getting removed in the batch. Note that that's
  // different to whether we're the first _child_, if we're removing
  // back-to-front.
  bool mIsFirst = true;
};

// Make sure we have space for our bits
#define ASSERT_NODE_FLAGS_SPACE(n)                         \
  static_assert(WRAPPER_CACHE_FLAGS_BITS_USED + (n) <=     \
                    sizeof(nsWrapperCache::FlagsType) * 8, \
                "Not enough space for our bits")
ASSERT_NODE_FLAGS_SPACE(NODE_TYPE_SPECIFIC_BITS_OFFSET);

/**
 * Class used to detect unexpected mutations. To use the class create an
 * nsMutationGuard on the stack before unexpected mutations could occur.
 * You can then at any time call Mutated to check if any unexpected mutations
 * have occurred.
 *
 * NOTE: There is SelectionChangeGuard which manages the generation of selection
 * ranges. If you need to fix a bug of this class, you need to touch
 * SelectionChangeGuard too.
 */
class nsMutationGuard {
 public:
  nsMutationGuard() { mStartingGeneration = sGeneration; }

  /**
   * Returns true if any unexpected mutations have occurred. You can pass in
   * an 8-bit ignore count to ignore a number of expected mutations.
   *
   * We don't need to care about overflow because subtraction of uint64_t's is
   * finding the difference between two elements of the group Z < 2^64.  Once
   * we know the difference between two elements we only need to check that is
   * less than the given number of mutations to know less than that many
   * mutations occurred.  Assuming constant 1ns mutations it would take 584
   * years for sGeneration to fully wrap around so we can ignore a guard living
   * through a full wrap around.
   */
  bool Mutated(uint8_t aIgnoreCount) const {
    return (sGeneration - mStartingGeneration) > aIgnoreCount;
  }

  // This function should be called whenever a mutation that we want to keep
  // track of happen. For now this is only done when children are added or
  // removed, but we might do it for attribute changes too in the future.
  static void DidMutate() { sGeneration++; }

 private:
  // This is the value sGeneration had when the guard was constructed.
  uint64_t mStartingGeneration;

  // This value is incremented on every mutation, for the life of the process.
  static uint64_t sGeneration;
};

/**
 * A class that implements nsIWeakReference
 */
class nsNodeWeakReference final : public nsIWeakReference {
 public:
  explicit nsNodeWeakReference(nsINode* aNode);

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIWeakReference
  NS_DECL_NSIWEAKREFERENCE

  void NoticeNodeDestruction() { mObject = nullptr; }

 private:
  ~nsNodeWeakReference();
};

enum class TreeKind : uint8_t {
  // The simplest DOM, won't get ShadowRoot from the parent.
  // FYI: This is a good TreeKind for the cases which won't cross Shadow DOM
  // boundaries. E.g., for the editor.
  DOM,
  // Treat attached ShadowRoot as a child before its first child. However,
  // non-content shadow is ignored because they must have <slot>s to use the
  // direct child of the host, but in this mode, <slot> is treated as not having
  // assigned nodes.
  // FYI: This is a good TreeKind for comparing the range boundaries of
  // TreeKind::DOM.
  // NOTE: When comparing 2 points with nsContentUtils::ComparePoints or its
  // relatives in the shadow including DOM, the points may be treated as in the
  // simple DOM tree because a point is in a shadow host is a valid point for
  // RangeBoundaryBase.  Therefore, they can compare points across shadow DOM
  // boundaries but they also may compare children of a shadow host which are
  // replaced with the shadow root in the shadow including DOM.
  ShadowIncludingDOM,
  // Handle the flattened tree which assigned nodes of <slot> are treated as
  // children of the <slot>.
  // FYI: This is a good TreeKind for the cases which need to work with the
  // visual order and/or any visible nodes. E.g., for the layout module.
  Flat,
  // Handle the flattened tree for Selection. Selection API accepts any nodes in
  // the DOM as a container of a range boundary. I.e., even if a node is not
  // part of the flattened tree. On the other hand, shadow for UA shadow DOM for
  // elements such as <details>, <video> and <audio> should not be treated as a
  // shadow. Therefore, their content in the DOM are treated as-is and the nodes
  // only in the UA shadow DOM are treated as disconnected shadow's content from
  // the parent point of view. I.e., DOM APIs should not cross the shadow DOM
  // boundary from the parent. On the other hand, they may be treated as
  // connected from the child point of view. E.g., users can select the default
  // <summary> text of a <details>.
  // FYI: This is a good TreeKind for DOM Selection that may cross the shadow
  // DOM boundaries.
  FlatForSelection,
};

template <TreeKind aKind>
[[nodiscard]] constexpr static inline bool ShouldIgnoreNonContentShadow() {
  return aKind != TreeKind::Flat;
}

template <TreeKind aKind>
[[nodiscard]] constexpr static inline bool ShouldHandleAssignedNodesOnSlot() {
  return aKind == TreeKind::Flat || aKind == TreeKind::FlatForSelection;
}

inline auto format_as(const TreeKind& aTreeKind) {
  constexpr static const char* sNames[] = {
      "DOM",
      "ShadowIncludingDOM",
      "Flat",
      "FlatForSelection",
  };
  return std::string(sNames[static_cast<uint8_t>(aTreeKind)]);
}

inline std::ostream& operator<<(std::ostream& aStream, TreeKind aTreeKind) {
  return aStream << format_as(aTreeKind);
}

// This should be used for any nsINode sub-class that has fields of its own
// that it needs to measure; any sub-class that doesn't use it will inherit
// AddSizeOfExcludingThis from its super-class. AddSizeOfIncludingThis() need
// not be defined, it is inherited from nsINode.
#define NS_DECL_ADDSIZEOFEXCLUDINGTHIS                       \
  virtual void AddSizeOfExcludingThis(nsWindowSizes& aSizes, \
                                      size_t* aNodeSize) const override;

// IID for the nsINode interface
// Must be kept in sync with xpcom/rust/xpcom/src/interfaces/nonidl.rs
#define NS_INODE_IID \
  {0x70ba4547, 0x7699, 0x44fc, {0xb3, 0x20, 0x52, 0xdb, 0xe3, 0xd1, 0xf9, 0x0a}}

/**
 * An internal interface that abstracts some DOMNode-related parts that both
 * nsIContent and Document share.  An instance of this interface has a list
 * of nsIContent children and provides access to them.
 */
class nsINode : public mozilla::dom::EventTarget {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  void AssertInvariantsOnNodeInfoChange();
#endif
 public:
  using BoxQuadOptions = mozilla::dom::BoxQuadOptions;
  using ConvertCoordinateOptions = mozilla::dom::ConvertCoordinateOptions;
  using DocGroup = mozilla::dom::DocGroup;
  using Document = mozilla::dom::Document;
  using DOMPoint = mozilla::dom::DOMPoint;
  using DOMPointInit = mozilla::dom::DOMPointInit;
  using DOMQuad = mozilla::dom::DOMQuad;
  using DOMRectReadOnly = mozilla::dom::DOMRectReadOnly;
  using OwningNodeOrString = mozilla::dom::OwningNodeOrString;
  using TextOrElementOrDocument = mozilla::dom::TextOrElementOrDocument;
  using CallerType = mozilla::dom::CallerType;
  using ErrorResult = mozilla::ErrorResult;

  // XXXbz Maybe we should codegen a class holding these constants and
  // inherit from it...
  static const uint16_t ELEMENT_NODE = 1;
  static const uint16_t ATTRIBUTE_NODE = 2;
  static const uint16_t TEXT_NODE = 3;
  static const uint16_t CDATA_SECTION_NODE = 4;
  static const uint16_t ENTITY_REFERENCE_NODE = 5;
  static const uint16_t ENTITY_NODE = 6;
  static const uint16_t PROCESSING_INSTRUCTION_NODE = 7;
  static const uint16_t COMMENT_NODE = 8;
  static const uint16_t DOCUMENT_NODE = 9;
  static const uint16_t DOCUMENT_TYPE_NODE = 10;
  static const uint16_t DOCUMENT_FRAGMENT_NODE = 11;
  static const uint16_t NOTATION_NODE = 12;
  static const uint16_t MAX_NODE_TYPE = NOTATION_NODE;

  void* operator new(size_t aSize, nsNodeInfoManager* aManager);
  void* operator new(size_t aSize) = delete;
  void operator delete(void* aPtr);

  template <class T>
  using Sequence = mozilla::dom::Sequence<T>;

  NS_INLINE_DECL_STATIC_IID(NS_INODE_IID)

  // The |aNodeSize| outparam on this function is where the actual node size
  // value is put. It gets added to the appropriate value within |aSizes| by
  // AddSizeOfNodeTree().
  //
  // Among the sub-classes that inherit (directly or indirectly) from nsINode,
  // measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - nsGenericHTMLElement:  mForm, mFieldSet
  // - nsGenericHTMLFrameElement: mFrameLoader (bug 672539)
  // - HTMLBodyElement:       mContentStyleRule
  // - HTMLDataListElement:   mOptions
  // - HTMLFieldSetElement:   mElements, mDependentElements, mFirstLegend
  // - HTMLFormElement:       many!
  // - HTMLFrameSetElement:   mRowSpecs, mColSpecs
  // - HTMLInputElement:      mInputData, mFiles, mFileList, mStaticDocfileList
  // - nsHTMLMapElement:      mAreas
  // - HTMLMediaElement:      many!
  // - nsHTMLOutputElement:   mDefaultValue, mTokenList
  // - nsHTMLRowElement:      mCells
  // - nsHTMLSelectElement:   mOptions, mRestoreState
  // - nsHTMLTableElement:    mTBodies, mRows, mTableInheritedAttributes
  // - nsHTMLTableSectionElement: mRows
  // - nsHTMLTextAreaElement: mControllers, mState
  //
  // The following members don't need to be measured:
  // - nsIContent: mPrimaryFrame, because it's non-owning and measured elsewhere
  //
  virtual void AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                      size_t* aNodeSize) const;

  // SizeOfIncludingThis doesn't need to be overridden by sub-classes because
  // sub-classes of nsINode are guaranteed to be laid out in memory in such a
  // way that |this| points to the start of the allocated object, even in
  // methods of nsINode's sub-classes, so aSizes.mState.mMallocSizeOf(this) is
  // always safe to call no matter which object it was invoked on.
  void AddSizeOfIncludingThis(nsWindowSizes& aSizes, size_t* aNodeSize) const;

  friend class nsNodeWeakReference;
  friend class nsNodeSupportsWeakRefTearoff;
  friend class AttrArray;

#ifdef MOZILLA_INTERNAL_API
  explicit nsINode(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);
#endif

  virtual ~nsINode();

  bool IsContainerNode() const {
    return IsElement() || IsDocument() || IsDocumentFragment();
  }

  /**
   * Returns true if the node is a HTMLTemplate element.
   */
  bool IsTemplateElement() const { return IsHTMLElement(nsGkAtoms::_template); }

  bool IsSlotable() const { return IsElement() || IsText(); }

  /**
   * Returns true if this is a document node.
   */
  bool IsDocument() const {
    // One less pointer-chase than checking NodeType().
    return !GetParentNode() && IsInUncomposedDoc();
  }

  /**
   * Return this node as a document. Asserts IsDocument().
   *
   * This is defined inline in Document.h.
   */
  inline Document* AsDocument();
  inline const Document* AsDocument() const;

  /**
   * Returns true if this is a document fragment node.
   */
  bool IsDocumentFragment() const {
    return NodeType() == DOCUMENT_FRAGMENT_NODE;
  }

  virtual bool IsHTMLFormControlElement() const { return false; }

  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-descendant
   *
   * @param aNode must not be nullptr.
   */
  bool IsInclusiveDescendantOf(const nsINode* aNode) const;

  /**
   * https://dom.spec.whatwg.org/#concept-shadow-including-descendant
   *
   * @param aNode must not be nullptr.
   */
  bool IsShadowIncludingDescendantOf(const nsINode* aNode) const;

  /**
   * https://dom.spec.whatwg.org/#concept-shadow-including-inclusive-descendant
   *
   * @param aNode must not be nullptr.
   */
  bool IsShadowIncludingInclusiveDescendantOf(const nsINode* aNode) const;

  /**
   * Returns true if the given node is this node or one of its descendants
   * in the "flat tree."
   *
   * @param aNode must not be nullptr.
   */
  bool IsInclusiveFlatTreeDescendantOf(const nsINode* aNode) const;

  /**
   * Return this node as a document fragment. Asserts IsDocumentFragment().
   *
   * This is defined inline in DocumentFragment.h.
   */
  inline mozilla::dom::DocumentFragment* AsDocumentFragment();
  inline const mozilla::dom::DocumentFragment* AsDocumentFragment() const;

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*> aGivenProto) final;

  /**
   * Hook for constructing JS::ubi::Concrete specializations for memory
   * reporting. Specializations are defined in NodeUbiReporting.h.
   */
  virtual void ConstructUbiNode(void* storage) = 0;

  /**
   * returns true if we are in priviliged code or
   * layout.css.getBoxQuads.enabled == true.
   */
  static bool HasBoxQuadsSupport(JSContext* aCx, JSObject* /* unused */);

 protected:
  /**
   * WrapNode is called from WrapObject to actually wrap this node, WrapObject
   * does some additional checks and fix-up that's common to all nodes. WrapNode
   * should just call the DOM binding's Wrap function.
   *
   * aGivenProto is the prototype to use (or null if the default one should be
   * used) and should just be passed directly on to the DOM binding's Wrap
   * function.
   */
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) = 0;

 public:
  // Implemented in Document.h
  mozilla::dom::ParentObject GetParentObject() const;

  /**
   * Returns the first child of a node or the first child of
   * a template element's content if the provided node is a
   * template element.
   */
  nsIContent* GetFirstChildOfTemplateOrNode();

  /**
   * Return the scope chain parent for this node, for use in things
   * like event handler compilation.  Returning null means to use the
   * global object as the scope chain parent.
   */
  virtual nsINode* GetScopeChainParent() const;

  MOZ_CAN_RUN_SCRIPT mozilla::dom::Element* GetParentFlexElement();

  /**
   * Returns the nearest inclusive open popover for a given node, see
   * https://html.spec.whatwg.org/multipage/popover.html#nearest-inclusive-open-popover
   */
  mozilla::dom::Element* GetNearestInclusiveOpenPopover() const;

  /**
   * https://html.spec.whatwg.org/multipage/popover.html#nearest-inclusive-target-popover-for-invoker
   */
  mozilla::dom::Element* GetNearestInclusiveTargetPopoverForInvoker() const;

  nsGenericHTMLElement* GetEffectiveCommandForElement() const;

  /**
   * https://html.spec.whatwg.org/multipage/popover.html#popover-target-element
   */
  nsGenericHTMLElement* GetEffectivePopoverTargetElement() const;

  /**
   * https://html.spec.whatwg.org/multipage/popover.html#topmost-clicked-popover
   */
  mozilla::dom::Element* GetTopmostClickedPopover() const;

  /**
   * https://html.spec.whatwg.org/multipage/interactive-elements.html#nearest-clicked-dialog
   */
  mozilla::dom::HTMLDialogElement* NearestClickedDialog(mozilla::WidgetEvent*);

  bool IsNode() const final { return true; }

  NS_IMPL_FROMEVENTTARGET_HELPER(nsINode, IsNode())

  /**
   * Return whether the node is an Element node. Faster than using `NodeType()`.
   */
  bool IsElement() const { return GetBoolFlag(NodeIsElement); }

  virtual bool IsTextControlElement() const { return false; }
  virtual bool IsSelectedContentElement() const { return false; }
  virtual bool IsGenericHTMLFormControlElementWithState() const {
    return false;
  }

  /**
   * Return true if the concrete class inherits ScriptElement.
   */
  [[nodiscard]] bool IsScriptElement() const {
    return IsHTMLElement(nsGkAtoms::script) || IsSVGElement(nsGkAtoms::script);
  }

  // Returns non-null if this element subclasses `LinkStyle`.
  virtual const mozilla::dom::LinkStyle* AsLinkStyle() const { return nullptr; }
  mozilla::dom::LinkStyle* AsLinkStyle() {
    return const_cast<mozilla::dom::LinkStyle*>(
        static_cast<const nsINode*>(this)->AsLinkStyle());
  }

  /**
   * Return this node as an Element.  Should only be used for nodes
   * for which IsElement() is true.  This is defined inline in Element.h.
   */
  inline mozilla::dom::Element* AsElement();
  inline const mozilla::dom::Element* AsElement() const;

  /**
   * Return whether the node is an nsStyledElement instance or not.
   */
  virtual bool IsStyledElement() const { return false; }

  /**
   * Return this node as nsIContent.  Should only be used for nodes for which
   * IsContent() is true.
   *
   * The assertion in nsIContent's constructor makes this safe.
   */
  nsIContent* AsContent() {
    MOZ_ASSERT(IsContent());
    return reinterpret_cast<nsIContent*>(this);
  }
  const nsIContent* AsContent() const {
    MOZ_ASSERT(IsContent());
    return reinterpret_cast<const nsIContent*>(this);
  }

  /*
   * Return whether the node is a Text node (which might be an actual
   * textnode, or might be a CDATA section).
   */
  bool IsText() const {
    uint32_t nodeType = NodeType();
    return nodeType == TEXT_NODE || nodeType == CDATA_SECTION_NODE;
  }

  /**
   * Return this node as Text if it is one, otherwise null.  This is defined
   * inline in Text.h.
   */
  inline mozilla::dom::Text* GetAsText();
  inline const mozilla::dom::Text* GetAsText() const;

  /**
   * Return this node as Text.  Asserts IsText().  This is defined inline in
   * Text.h.
   */
  inline mozilla::dom::Text* AsText();
  inline const mozilla::dom::Text* AsText() const;

  /**
   * Return this node if the instance type inherits nsIFormControl, or an
   * nsIFormControl instance which ia associated with this node.  Otherwise,
   * returns nullptr.
   */
  [[nodiscard]] virtual nsIFormControl* GetAsFormControl() { return nullptr; }
  [[nodiscard]] virtual const nsIFormControl* GetAsFormControl() const {
    return nullptr;
  }

  /**
   * Return this node as HTMLSlotElement if this an HTMLSlotElement which has
   * some assigned nodes.
   */
  [[nodiscard]] mozilla::dom::HTMLSlotElement* GetAsHTMLSlotElementIfFilled();

  /**
   * Return this node as HTMLSlotElement if this an HTMLSlotElement which has
   * some assigned nodes.
   */
  [[nodiscard]] const mozilla::dom::HTMLSlotElement*
  GetAsHTMLSlotElementIfFilled() const;

  /**
   * Return this node as HTMLSlotElement if this an HTMLSlotElement which has
   * some assigned nodes.  However, if its containing shadow root is anonymous
   * one, this returns nullptr.
   */
  [[nodiscard]] mozilla::dom::HTMLSlotElement*
  GetAsHTMLSlotElementIfFilledForSelection();

  /**
   * Return this node as HTMLSlotElement if this an HTMLSlotElement which  has
   * some assigned nodes.  However, if its containing shadow root is anonymous
   * one, this returns nullptr.
   */
  [[nodiscard]] const mozilla::dom::HTMLSlotElement*
  GetAsHTMLSlotElementIfFilledForSelection() const;

  template <TreeKind aKind>
  [[nodiscard]] mozilla::dom::HTMLSlotElement* GetAsHTMLSlotElementIfFilled() {
    if constexpr (aKind == TreeKind::DOM ||
                  aKind == TreeKind::ShadowIncludingDOM) {
      return nullptr;
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetAsHTMLSlotElementIfFilled();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetAsHTMLSlotElementIfFilledForSelection();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  template <TreeKind aKind>
  [[nodiscard]] const mozilla::dom::HTMLSlotElement*
  GetAsHTMLSlotElementIfFilled() const {
    return const_cast<nsINode*>(this)->GetAsHTMLSlotElementIfFilled();
  }

  /*
   * Return whether the node is a ProcessingInstruction node.
   */
  bool IsProcessingInstruction() const {
    return NodeType() == PROCESSING_INSTRUCTION_NODE;
  }

  /*
   * Return whether the node is a CharacterData node (text, cdata,
   * comment, processing instruction)
   */
  bool IsCharacterData() const {
    uint32_t nodeType = NodeType();
    return nodeType == TEXT_NODE || nodeType == CDATA_SECTION_NODE ||
           nodeType == PROCESSING_INSTRUCTION_NODE || nodeType == COMMENT_NODE;
  }

  /**
   * Return whether the node is a Comment node.
   */
  bool IsComment() const { return NodeType() == COMMENT_NODE; }

  /**
   * Return whether the node is an Attr node.
   */
  bool IsAttr() const { return NodeType() == ATTRIBUTE_NODE; }

  /**
   * Return if this node has any children.
   */
  [[nodiscard]] bool HasChildren() const { return !!mFirstChild; }

  template <TreeKind aKind>
  [[nodiscard]] bool HasChildren() const {
    return !!GetChildCount<aKind>();
  }

  /**
   * Get the number of children
   * @return the number of children
   */
  [[nodiscard]] uint32_t GetChildCount() const { return mChildCount; }

  /** Get the number of flat tree children */
  [[nodiscard]] uint32_t GetFlatTreeChildCount() const;

  /** Get the number of flat tree children for selection */
  [[nodiscard]] uint32_t GetFlatTreeForSelectionChildCount() const;

  template <TreeKind aKind>
  [[nodiscard]] uint32_t GetChildCount() const {
    static_assert(
        aKind != TreeKind::ShadowIncludingDOM,
        "It's unclear what this should return if this is a shadow host so that "
        "this does not support TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return GetChildCount();
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetFlatTreeChildCount();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetFlatTreeForSelectionChildCount();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * NOTE: this function is going to be removed soon (hopefully!) Don't use it
   * in new code.
   *
   * Get a child by index
   * @param aIndex the index of the child to get
   * @return the child, or null if index out of bounds
   */
  [[nodiscard]] nsIContent* GetChildAt_Deprecated(uint32_t aIndex) const;

  /**
   * Return a child of the shadow root if there is, an assigned node if this is
   * a non-empty <slot> or a child of this node.
   */
  [[nodiscard]] nsIContent* GetChildAtInFlatTree(uint32_t aIndex) const;

  /**
   * Similar to GetChildAtInFlatTree().  However, this does not treat a
   * non-content shadow tree's host element like <detail>, <video> or SVG <use>
   * as a shadow host.  I.e., if the element has a non-content shadow, this
   * returns a child of this node instead.  Similary, this is a <slot> element
   * of a non-content shadow like in <details>, this does not return an assigned
   * node to the <slot>.
   */
  [[nodiscard]] nsIContent* GetChildAtInFlatTreeForSelection(
      uint32_t aIndex) const;

  template <TreeKind aKind>
  [[nodiscard]] nsIContent* GetChildAt_Deprecated(uint32_t aIndex) const {
    static_assert(
        aKind != TreeKind::ShadowIncludingDOM,
        "It's unclear what this should return if this is a shadow host so that "
        "this does not support TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return GetChildAt_Deprecated(aIndex);
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetChildAtInFlatTree(aIndex);
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetChildAtInFlatTreeForSelection(aIndex);
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * Get the index of a child within this content.
   *
   * @param aPossibleChild the child to get the index of.
   * @return the index of the child, or Nothing if not a child. Be aware that
   *         anonymous children (e.g. a <div> child of an <input> element) will
   *         result in Nothing.
   *
   * If the return value is Some, then calling GetChildAt_Deprecated() with
   * that value will return aPossibleChild.
   */
  [[nodiscard]] mozilla::Maybe<uint32_t> ComputeIndexOf(
      const nsINode* aPossibleChild) const;

  /**
   * Return true if ComputeIndexOf() may cache the computed index for further
   * calls.
   */
  [[nodiscard]] bool MaybeCachesComputedIndex() const;

  /**
   * Get the index of a child within this content's flat tree children.
   *
   * @param aPossibleChild the child to get the index of.
   * @return the index of the child, or Nothing if not a child. Be aware that
   *         anonymous children (e.g. a <div> child of an <input> element) will
   *         result in Nothing.
   */
  [[nodiscard]] mozilla::Maybe<uint32_t> ComputeFlatTreeIndexOf(
      const nsINode* aPossibleChild) const;

  /**
   * Get the index of a child within this content's flat tree children for
   * selection.
   *
   * @param aPossibleChild the child to get the index of.
   * @return the index of the child, or Nothing if not a child.  Note that if
   *         the child is in a shadow tree but it should be ignored for
   *         selection, this returns the simple DOM index of aPossibleChild.
   *         Be aware that anonymous children (e.g. a <div> child of an <input>
   *         element) will result in Nothing.
   */
  [[nodiscard]] mozilla::Maybe<uint32_t> ComputeFlatTreeForSelectionIndexOf(
      const nsINode* aPossibleChild) const;

  template <TreeKind aKind>
  [[nodiscard]] mozilla::Maybe<uint32_t> ComputeIndexOf(
      const nsINode* aPossibleChild) const {
    static_assert(
        aKind != TreeKind::ShadowIncludingDOM,
        "It's unclear what this should return if this is a shadow host and "
        "aPossibleChild is either a child of the ShadowRoot or a child of the "
        "host so that this does not support TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return ComputeIndexOf(aPossibleChild);
    } else if constexpr (aKind == TreeKind::Flat) {
      return ComputeFlatTreeIndexOf(aPossibleChild);
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return ComputeFlatTreeForSelectionIndexOf(aPossibleChild);
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * Get the index of this within parent node (ComputeIndexInParentNode) or
   * parent content (nsIContent) node (ComputeIndexInParentContent).
   *
   * @return the index of this node in the parent, or Nothing there is no
   *         parent (content) node or the parent does not have this node anymore
   *         (e.g., being removed from the parent). Be aware that anonymous
   *         children (e.g. a <div> child of an <input> element) will result in
   *         Nothing.
   *
   * If the return value is Some, then calling GetChildAt_Deprecated() with
   * that value will return this.
   */
  mozilla::Maybe<uint32_t> ComputeIndexInParentNode() const;
  mozilla::Maybe<uint32_t> ComputeIndexInParentContent() const;

  /**
   * Get the index of a child within this content.
   *
   * @param aPossibleChild the child to get the index of.
   * @return the index of the child, or -1 if not a child. Be aware that
   *         anonymous children (e.g. a <div> child of an <input> element) will
   *         result in -1.
   *
   * If the return value is not -1, then calling GetChildAt_Deprecated() with
   * that value will return aPossibleChild.
   */
  int32_t ComputeIndexOf_Deprecated(const nsINode* aPossibleChild) const;

  /**
   * Returns the "node document" of this node.
   *
   * https://dom.spec.whatwg.org/#concept-node-document
   *
   * Note that in the case that this node is a document node this method
   * will return |this|.  That is different to the Node.ownerDocument DOM
   * attribute (implemented by nsINode::GetOwnerDocument) which is specified to
   * be null in that case:
   *
   * https://dom.spec.whatwg.org/#dom-node-ownerdocument
   *
   * For all other cases OwnerDoc and GetOwnerDocument behave identically.
   */
  Document* OwnerDoc() const MOZ_NONNULL_RETURN {
    return mNodeInfo->GetDocument();
  }

  // Returns our owning NodeInfo manager.
  nsNodeInfoManager* NodeInfoManager() const {
    return mNodeInfo->NodeInfoManager();
  }

  /**
   * Return the "owner document" of this node as an nsINode*.  Implemented
   * in Document.h.
   */
  inline nsINode* OwnerDocAsNode() const MOZ_NONNULL_RETURN;

  /**
   * Returns true if the content has an ancestor that is a document.
   *
   * @return whether this content is in a document tree
   */
  bool IsInUncomposedDoc() const { return GetBoolFlag(IsInDocument); }

  /**
   * Get the document that this content is currently in, if any. This will be
   * null if the content has no ancestor that is a document.
   *
   * @return the current document
   */

  Document* GetUncomposedDoc() const {
    return IsInUncomposedDoc() ? OwnerDoc() : nullptr;
  }

  /**
   * Returns true if we're connected, and thus GetComposedDoc() would return a
   * non-null value.
   */
  bool IsInComposedDoc() const { return GetBoolFlag(IsConnected); }

  /**
   * This method returns the owner document if the node is connected to it
   * (as defined in the DOM spec), otherwise it returns null.
   * In other words, returns non-null even in the case the node is in
   * Shadow DOM, if there is a possibly shadow boundary crossing path from
   * the node to its owner document.
   */
  Document* GetComposedDoc() const {
    return IsInComposedDoc() ? OwnerDoc() : nullptr;
  }

  /**
   * Returns OwnerDoc() if the node is in uncomposed document and ShadowRoot if
   * the node is in Shadow DOM.
   */
  mozilla::dom::DocumentOrShadowRoot* GetContainingDocumentOrShadowRoot() const;

  /**
   * Returns OwnerDoc() if the node is in uncomposed document and ShadowRoot if
   * the node is in Shadow DOM and is in composed document.
   */
  mozilla::dom::DocumentOrShadowRoot* GetUncomposedDocOrConnectedShadowRoot()
      const;

  /**
   * To be called when reference count of the node drops to zero.
   */
  void LastRelease();

  /**
   * The values returned by this function are the ones defined for
   * Node.nodeType
   */
  uint16_t NodeType() const { return mNodeInfo->NodeType(); }
  const nsString& NodeName() const { return mNodeInfo->NodeName(); }
  const nsString& LocalName() const { return mNodeInfo->LocalName(); }

  /**
   * Get the NodeInfo for this element
   * @return the nodes node info
   */
  inline mozilla::dom::NodeInfo* NodeInfo() const { return mNodeInfo; }

  // Per spec step 5.1.3.9 of
  // https://dom.spec.whatwg.org/#concept-create-element
  // Set the namespace prefix on a freshly-created, disconnected node.
  void SetNamespacePrefix(nsAtom* aPrefix);

  /**
   * Called when we have been adopted, and the information of the
   * node has been changed.
   *
   * The new document can be reached via OwnerDoc().
   *
   * If you override this method,
   * please call up to the parent NodeInfoChanged.
   *
   * If you change this, change also the similar method in Link.
   */
  virtual void NodeInfoChanged(Document* aOldDoc) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    AssertInvariantsOnNodeInfoChange();
#endif
  }

  inline bool IsInNamespace(int32_t aNamespace) const {
    return mNodeInfo->NamespaceID() == aNamespace;
  }

  /**
   * Returns the DocGroup of the "node document" of this node.
   */
  DocGroup* GetDocGroup() const;

  /**
   * Print a debugger friendly descriptor of this element. This will describe
   * the position of this element in the document.
   *
   * @param aOutput [output] The result value.
   * @param aRoot [optional] The root node which should be printed at last.
   */
  void GetDebugDescription(nsACString& aOutput,
                           const nsINode* aRoot = nullptr) const;

  /**
   * Return nsCString instance which is converted from the result of
   * GetDebugDescription(). This is useful to dump the node path in an specific
   * element like contenteditable or something and print it with fmt::format()
   * or MOZ_LOG_FMT().
   *
   * @param aRoot The root node which should be printed at last. Can be null.
   */
  nsCString FormatAs(const nsINode* aRoot) const;

  friend std::ostream& operator<<(std::ostream&, const nsINode&);
  friend nsCString format_as(const nsINode& aNode) {
    return aNode.FormatAs(nullptr);
  }

 protected:
  // These 2 methods are useful for the recursive templates IsHTMLElement,
  // IsSVGElement, etc.
  inline bool IsNodeInternal() const { return false; }

  template <typename First, typename... Args>
  inline bool IsNodeInternal(First aFirst, Args... aArgs) const {
    return mNodeInfo->Equals(aFirst) || IsNodeInternal(aArgs...);
  }

 public:
  inline bool IsHTMLElement() const {
    return IsElement() && IsInNamespace(kNameSpaceID_XHTML);
  }

  inline bool IsHTMLElement(const nsAtom* aTag) const {
    return IsElement() && mNodeInfo->Equals(aTag, kNameSpaceID_XHTML);
  }

  template <typename First, typename... Args>
  inline bool IsAnyOfHTMLElements(First aFirst, Args... aArgs) const {
    return IsHTMLElement() && IsNodeInternal(aFirst, aArgs...);
  }

  inline bool IsSVGElement() const {
    return IsElement() && IsInNamespace(kNameSpaceID_SVG);
  }

  inline bool IsSVGElement(const nsAtom* aTag) const {
    return IsElement() && mNodeInfo->Equals(aTag, kNameSpaceID_SVG);
  }

  template <typename First, typename... Args>
  inline bool IsAnyOfSVGElements(First aFirst, Args... aArgs) const {
    return IsSVGElement() && IsNodeInternal(aFirst, aArgs...);
  }

  virtual bool IsSVGAnimationElement() const { return false; }
  virtual bool IsSVGComponentTransferFunctionElement() const { return false; }
  virtual bool IsSVGFilterPrimitiveElement() const { return false; }
  virtual bool IsSVGFilterPrimitiveChildElement() const { return false; }
  virtual bool IsSVGGeometryElement() const { return false; }
  virtual bool IsSVGGraphicsElement() const { return false; }

  inline bool IsXULElement() const {
    return IsElement() && IsInNamespace(kNameSpaceID_XUL);
  }

  inline bool IsXULElement(const nsAtom* aTag) const {
    return IsElement() && mNodeInfo->Equals(aTag, kNameSpaceID_XUL);
  }

  template <typename First, typename... Args>
  inline bool IsAnyOfXULElements(First aFirst, Args... aArgs) const {
    return IsXULElement() && IsNodeInternal(aFirst, aArgs...);
  }

  inline bool IsMathMLElement() const {
    return IsElement() && IsInNamespace(kNameSpaceID_MathML);
  }

  inline bool IsMathMLElement(const nsAtom* aTag) const {
    return IsElement() && mNodeInfo->Equals(aTag, kNameSpaceID_MathML);
  }

  template <typename First, typename... Args>
  inline bool IsAnyOfMathMLElements(First aFirst, Args... aArgs) const {
    return IsMathMLElement() && IsNodeInternal(aFirst, aArgs...);
  }

  bool IsShadowRoot() const {
    const bool isShadowRoot = IsInShadowTree() && !GetParentNode();
    MOZ_ASSERT_IF(isShadowRoot, IsDocumentFragment());
    return isShadowRoot;
  }

  bool IsHTMLHeadingElement() const {
    return IsAnyOfHTMLElements(nsGkAtoms::h1, nsGkAtoms::h2, nsGkAtoms::h3,
                               nsGkAtoms::h4, nsGkAtoms::h5, nsGkAtoms::h6);
  }

  /**
   * Check whether the conditional processing attributes other than
   * systemLanguage "return true" if they apply to and are specified
   * on the given SVG element. Returns true if this element should be
   * rendered, false if it should not.
   */
  virtual bool PassesConditionalProcessingTests() const { return true; }

  /**
   * Insert a content node before another or at the end.
   * This method handles calling BindToTree on the child appropriately.
   *
   * @param aKid the content to insert
   * @param aBeforeThis an existing node. Use nullptr if you want to
   *        add aKid at the end.
   * @param aNotify whether to notify the document (current document for
   *        nsIContent, and |this| for Document) that the insert has occurred
   * @param aRv The error, if any.
   *        Throw NS_ERROR_DOM_HIERARCHY_REQUEST_ERR if one attempts to have
   *        more than one element node as a child of a document.  Doing this
   *        will also assert -- you shouldn't be doing it!  Check with
   *        Document::GetRootElement() first if you're not sure.  Apart from
   *        this one constraint, this doesn't do any checking on whether aKid is
   *        a valid child of |this|.
   *        Throw NS_ERROR_OUT_OF_MEMORY in some cases (from BindToTree).
   * @param aOldParent In case the method is called as part of moveBefore,
   *        the argument tells which node used to be the parent of aKid.
   * @param aMutationEffectOnScript Indicate how this change after
   *        trustworthiness of parent script.
   */
  virtual void InsertChildBefore(
      nsIContent* aKid, nsIContent* aBeforeThis, bool aNotify,
      mozilla::ErrorResult& aRv, nsINode* aOldParent = nullptr,
      MutationEffectOnScript aMutationEffectOnScript =
          MutationEffectOnScript::DropTrustWorthiness);

  /**
   * Append a content node to the end of the child list.  This method handles
   * calling BindToTree on the child appropriately.
   *
   * @param aKid the content to append
   * @param aNotify whether to notify the document (current document for
   *        nsIContent, and |this| for Document) that the append has occurred
   * @param aRv The error, if any.
   *        Throw NS_ERROR_DOM_HIERARCHY_REQUEST_ERR if one attempts to have
   *        more than one element node as a child of a document.  Doing this
   *        will also assert -- you shouldn't be doing it!  Check with
   *        Document::GetRootElement() first if you're not sure.  Apart from
   *        this one constraint, this doesn't do any checking on whether aKid is
   *        a valid child of |this|.
   *        Throw NS_ERROR_OUT_OF_MEMORY in some cases (from BindToTree).
   * @param aMutationEffectOnScript Indicate how this change after
   *        trustworthiness of parent script.
   */
  void AppendChildTo(nsIContent* aKid, bool aNotify, mozilla::ErrorResult& aRv,
                     MutationEffectOnScript aMutationEffectOnScript =
                         MutationEffectOnScript::DropTrustWorthiness) {
    InsertChildBefore(aKid, nullptr, aNotify, aRv, nullptr,
                      aMutationEffectOnScript);
  }

  template <BatchRemovalOrder aOrder = BatchRemovalOrder::FrontToBack>
  void RemoveAllChildren(bool aNotify) {
    BatchRemovalState state{};
    while (HasChildren()) {
      nsIContent* nodeToRemove = aOrder == BatchRemovalOrder::FrontToBack
                                     ? GetFirstChild()
                                     : GetLastChild();
      RemoveChildNode(nodeToRemove, aNotify, &state, nullptr,
                      MutationEffectOnScript::KeepTrustWorthiness);
      state.mIsFirst = false;
    }
  }

  /**
   * Remove a child from this node.  This method handles calling UnbindFromTree
   * on the child appropriately.
   *
   * @param aKid the content to remove
   * @param aNotify whether to notify the document (current document for
   *        nsIContent, and |this| for Document) that the remove has occurred
   * @param BatchRemovalState The current state of our batch removal.
   * @param aNewParent In case the method is called as part of moveBefore,
   *        the argument tells which node will be aKid's new parent.
   * @param aMutationEffectOnScript Indicate how this change after
   *        trustworthiness of parent script.
   */
  virtual void RemoveChildNode(nsIContent* aKid, bool aNotify,
                               const BatchRemovalState* = nullptr,
                               nsINode* aNewParent = nullptr,
                               MutationEffectOnScript aMutationEffectOnScript =
                                   MutationEffectOnScript::DropTrustWorthiness);

  /**
   * Get a property associated with this node.
   *
   * @param aPropertyName  name of property to get.
   * @param aStatus        out parameter for storing resulting status.
   *                       Set to NS_PROPTABLE_PROP_NOT_THERE if the property
   *                       is not set.
   * @return               the property. Null if the property is not set
   *                       (though a null return value does not imply the
   *                       property was not set, i.e. it can be set to null).
   */
  void* GetProperty(const nsAtom* aPropertyName,
                    nsresult* aStatus = nullptr) const;

  /**
   * Set a property to be associated with this node. This will overwrite an
   * existing value if one exists. The existing value is destroyed using the
   * destructor function given when that value was set.
   *
   * @param aPropertyName  name of property to set.
   * @param aValue         new value of property.
   * @param aDtor          destructor function to be used when this property
   *                       is destroyed.
   * @param aTransfer      if true the property will not be deleted when the
   *                       ownerDocument of the node changes, if false it
   *                       will be deleted.
   *
   * @return NS_PROPTABLE_PROP_OVERWRITTEN (success value) if the property
   *                                       was already set
   * @throws NS_ERROR_OUT_OF_MEMORY if that occurs
   */
  nsresult SetProperty(nsAtom* aPropertyName, void* aValue,
                       NSPropertyDtorFunc aDtor = nullptr,
                       bool aTransfer = false);

  /**
   * A generic destructor for property values allocated with new.
   */
  template <class T>
  static void DeleteProperty(void*, nsAtom*, void* aPropertyValue, void*) {
    delete static_cast<T*>(aPropertyValue);
  }

  /**
   * Removes a property associated with this node. The value is destroyed using
   * the destruction function given when that value was set.
   *
   * @param aPropertyName  name of property to destroy.
   */
  void RemoveProperty(const nsAtom* aPropertyName);

  /**
   * Take a property associated with this node. The value will not be destroyed
   * but rather returned. It is the caller's responsibility to destroy the value
   * after that point.
   *
   * @param aPropertyName  name of property to unset.
   * @param aStatus        out parameter for storing resulting status.
   *                       Set to NS_PROPTABLE_PROP_NOT_THERE if the property
   *                       is not set.
   * @return               the property. Null if the property is not set
   *                       (though a null return value does not imply the
   *                       property was not set, i.e. it can be set to null).
   */
  void* TakeProperty(const nsAtom* aPropertyName, nsresult* aStatus = nullptr);

  bool HasProperties() const { return HasFlag(NODE_HAS_PROPERTIES); }

  /**
   * Return the principal of this node.  This is guaranteed to never be a null
   * pointer.
   */
  nsIPrincipal* NodePrincipal() const {
    return mNodeInfo->NodeInfoManager()->DocumentPrincipal();
  }

  /**
   * Return the Policy Container of this node's document, if any.
   */
  nsIPolicyContainer* GetPolicyContainer() const;

  /**
   * Get the parent nsIContent for this node.
   * @return the parent, or null if no parent or the parent is not an nsIContent
   */
  nsIContent* GetParent() const {
    if (GetBoolFlag(ParentIsContent)) [[likely]] {
      return mParent->AsContent();
    }
    return nullptr;
  }

  /**
   * Get the parent nsINode for this node. This can be either an nsIContent, a
   * Document or an Attr.
   * @return the parent node
   */
  [[nodiscard]] nsINode* GetParentNode() const { return mParent; }

 private:
  nsIContent* DoGetShadowHost() const;

 public:
  [[nodiscard]] nsINode* GetParentOrShadowHostNode() const {
    if (mParent) [[likely]] {
      return mParent;
    }
    // We could put this in nsIContentInlines.h or such to avoid this
    // reinterpret_cast, but it doesn't seem worth it.
    return IsInShadowTree() ? reinterpret_cast<nsINode*>(DoGetShadowHost())
                            : nullptr;
  }

  enum FlattenedParentType { eNormal, eForStyle, eForSelection };

  /**
   * Returns the node that is the parent of this node in the flattened
   * tree. This differs from the normal parent if the node is filtered
   * into an insertion point, or if the node is a direct child of a
   * shadow root.
   *
   * Be aware, this returns the host element if this node is a child of a
   * shadow root. This behavior is different from GetParentOrShadowHostNode().
   *
   * @return the flattened tree parent
   */
  [[nodiscard]] inline nsINode* GetFlattenedTreeParentNode() const;

  [[nodiscard]] nsINode* GetFlattenedTreeParentNodeNonInline() const;

  /**
   * Like GetFlattenedTreeParentNode, but returns the document for any native
   * anonymous content that was generated for ancestor frames of the document
   * element's primary frame, such as scrollbar elements created by the root
   * scroll frame.
   */
  inline nsINode* GetFlattenedTreeParentNodeForStyle() const;
  inline nsIContent* GetFlattenedTreeParentForStyle() const;

  /**
   * Similar to GetFlattenedTreeParentNode, it does two things differently
   *   1. For contents that are not in the flattened tree, use its
   *   parent rather than nullptr because any nodes in the DOM is a valid
   *   container node of Selection API.
   *   2. For contents that are slotted into a non-content shadow tree, use its
   *   parent rather than the slot element.
   *   3. For contents that is a shadow root of a non-content shadow tree,
   *   return nullptr, i.e., the shadow is treated as "disconnected".
   *
   * Be aware, this returns the host element if this node is a child of a
   * shadow root. This behavior is different from
   * GetFlattenedTreeParentOrShadowHostNodeForSelection().
   */
  [[nodiscard]] inline nsINode* GetFlattenedTreeParentNodeForSelection() const;

  /**
   * Return a parent node of this if this is connected to a node. However, if
   * this is a child of a `ShadowRoot` and TreeKind is not `DOM`, return the
   * host. I.e., this skips `ShadowRoot`. Additionally, if this is assigned to
   * a <slot> and TreeKind is FlatForSelection or Flat, return the <slot>.
   *
   * NOTE: This returns ShadowRoot if and only if TreeKind::DOM.
   */
  template <TreeKind aKind>
  [[nodiscard]] nsINode* GetParentNode() const {
    static_assert(aKind != TreeKind::ShadowIncludingDOM,
                  "It's unclear what this should return if this is a child of "
                  "a ShadowRoot so that this does not support "
                  "TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return GetParentNode();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetFlattenedTreeParentNodeForSelection();
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetFlattenedTreeParentNode();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  inline mozilla::dom::Element* GetFlattenedTreeParentElement() const;
  inline mozilla::dom::Element* GetFlattenedTreeParentElementForStyle() const;

  /**
   * Get the parent nsINode for this node if it is an Element.
   *
   * Defined inline in Element.h
   *
   * @return the parent node
   */
  inline mozilla::dom::Element* GetParentElement() const;

  /**
   * Get the parent Element of this node, traversing over a ShadowRoot
   * to its host if necessary.
   */
  mozilla::dom::Element* GetParentElementCrossingShadowRoot() const;

  /**
   * Get closest element node for the node.  Meaning that if the node is an
   * element node, returns itself.  Otherwise, returns parent element or null.
   * Note that if the node is a non-element node and it's root of a shadow DOM,
   * this returns nullptr.  If you want to get inclusive flattened tree ancestor
   * in this case, use GetInclusiveFlattenedTreeAncestorElement().
   */
  inline mozilla::dom::Element* GetAsElementOrParentElement() const;

  /**
   * Get inclusive ancestor element in the flattened tree.
   */
  inline mozilla::dom::Element* GetInclusiveFlattenedTreeAncestorElement()
      const;

  /**
   * Get the root of the subtree this node belongs to.  This never returns
   * null.  It may return 'this' (e.g. for document nodes, and nodes that
   * are the roots of disconnected subtrees).
   */
  nsINode* SubtreeRoot() const {
#ifdef DEBUG
    AssertSubtreeRootIsInSync();
#endif
    return mSubtreeRoot;
  }

  /*
   * Get context object's shadow-including root if options's composed is true,
   * and context object's root otherwise.
   */
  nsINode* GetRootNode(const mozilla::dom::GetRootNodeOptions& aOptions);

  mozilla::EventListenerManager* GetExistingListenerManager() const override;
  mozilla::EventListenerManager* GetOrCreateListenerManager() override;

  mozilla::Maybe<mozilla::dom::EventCallbackDebuggerNotificationType>
  GetDebuggerNotificationType() const override;

  bool ComputeDefaultWantsUntrusted(mozilla::ErrorResult& aRv) final;

  bool IsApzAware() const override;

  nsIGlobalObject* GetRelevantGlobal() const override;
  // The global of our owner document, as opposed to our global, which might be
  // different in the case of adoption.
  nsIGlobalObject* GetDocumentGlobal() const;
  mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder>
  GetDocumentGlobalForBindings();

  using mozilla::dom::EventTarget::DispatchEvent;
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool DispatchEvent(
      mozilla::dom::Event& aEvent, mozilla::dom::CallerType aCallerType,
      mozilla::ErrorResult& aRv) override;

  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEvent(mozilla::EventChainPostVisitor& aVisitor) override;

  /**
   * Adds a mutation observer to be notified when this node, or any of its
   * descendants, are modified. The node will hold a weak reference to the
   * observer, which means that it is the responsibility of the observer to
   * remove itself in case it dies before the node.  If an observer is added
   * while observers are being notified, it may also be notified.  In general,
   * adding observers while inside a notification is not a good idea.  An
   * observer that is already observing the node must not be added without
   * being removed first.
   *
   * For mutation observers that implement nsIAnimationObserver, use
   * AddAnimationObserver instead.
   */
  void AddMutationObserver(nsIMutationObserver* aMutationObserver) {
    nsSlots* s = Slots();
    if (aMutationObserver) {
      NS_ASSERTION(!s->mMutationObservers.contains(aMutationObserver),
                   "Observer already in the list");

      s->mMutationObservers.pushBack(aMutationObserver);
      ForgetObserverChain();
    }
  }

  void AddMutationObserver(nsMultiMutationObserver* aMultiMutationObserver);

  /**
   * Same as above, but only adds the observer if its not observing
   * the node already.
   *
   * For mutation observers that implement nsIAnimationObserver, use
   * AddAnimationObserverUnlessExists instead.
   */
  void AddMutationObserverUnlessExists(nsIMutationObserver* aMutationObserver) {
    nsSlots* s = Slots();
    if (aMutationObserver &&
        !s->mMutationObservers.contains(aMutationObserver)) {
      s->mMutationObservers.pushBack(aMutationObserver);
      ForgetObserverChain();
    }
  }

  void AddMutationObserverUnlessExists(
      nsMultiMutationObserver* aMultiMutationObserver);
  /**
   * Same as AddMutationObserver, but for nsIAnimationObservers.  This
   * additionally records on the document that animation observers have
   * been registered, which is used to determine whether notifications
   * must be fired when animations are added, removed or changed.
   */
  void AddAnimationObserver(nsIAnimationObserver* aAnimationObserver);

  /**
   * Same as above, but only adds the observer if its not observing
   * the node already.
   */
  void AddAnimationObserverUnlessExists(
      nsIAnimationObserver* aAnimationObserver);

  /**
   * Removes a mutation observer.
   */
  void RemoveMutationObserver(nsIMutationObserver* aMutationObserver) {
    // We do not need to invalidate the observer chain cache here. The cache
    // points to the lowest ancestor that had an observer. If an observer is
    // removed, the cached skip-to node might become overly conservative, but
    // it will never skip over new observers, so correctness is preserved.
    if (nsSlots* s = GetExistingSlots()) {
      s->mMutationObservers.remove(aMutationObserver);
    }
  }

  /**
   * Memoizes one ForEachAncestorObserver walk's start node and the lowest
   * ancestor holding an observer, so repeated mutations against the same node
   * skip the stretch between them. Only picks where a walk starts, never what
   * it returns, so a stale skip costs iteration and memory accesses rather than
   * correctness.
   */
  static bool IsObserverChainStart(const nsINode* aNode) {
    return aNode == sObserverChainStart;
  }
  static nsINode* ObserverChainSkipTo(const nsINode* aNode) {
    return aNode == sObserverChainStart ? sObserverChainSkipTo : nullptr;
  }
  static void NoteObserverChain(const nsINode* aStart, nsINode* aSkipTo) {
    sObserverChainStart = aStart;
    sObserverChainSkipTo = aSkipTo;
  }
  static void ForgetObserverChain() {
    sObserverChainStart = nullptr;
    sObserverChainSkipTo = nullptr;
  }
  static void ForgetObserverChainIfCached(const nsINode* aNode) {
    if (aNode == sObserverChainStart || aNode == sObserverChainSkipTo) {
      ForgetObserverChain();
    }
  }

  void RemoveMutationObserver(nsMultiMutationObserver* aMultiMutationObserver);

  mozilla::SafeDoublyLinkedList<nsIMutationObserver>* GetMutationObservers();

  /**
   * Helper methods to access ancestor node(s) of type T.
   * The implementations of the methods are in mozilla/dom/AncestorIterator.h.
   */
  template <typename T>
  inline mozilla::dom::AncestorsOfTypeIterator<T> AncestorsOfType() const;

  template <typename T>
  inline mozilla::dom::InclusiveAncestorsOfTypeIterator<T>
  InclusiveAncestorsOfType() const;

  template <typename T>
  inline mozilla::dom::FlatTreeAncestorsOfTypeIterator<T>
  FlatTreeAncestorsOfType() const;

  template <typename T>
  inline mozilla::dom::InclusiveFlatTreeAncestorsOfTypeIterator<T>
  InclusiveFlatTreeAncestorsOfType() const;

  template <typename T>
  T* FirstAncestorOfType() const;

 private:
#ifdef DEBUG
  void AssertSubtreeRootIsInSync() const;
#endif
  /**
   * Walks aNode, its attributes and, if aDeep is true, its descendant nodes.
   * If aClone is true the nodes will be cloned. If aNewNodeInfoManager is
   * not null, it is used to create new nodeinfos for the nodes.
   *
   * @param aNode Node to adopt/clone.
   * @param aClone If true the node will be cloned and the cloned node will
   *               be returned.
   * @param aDeep If true the function will be called recursively on
   *              descendants of the node
   * @param aNewNodeInfoManager The nodeinfo manager to use to create new
   *                            nodeinfos for aNode and its attributes and
   *                            descendants. May be null if the nodeinfos
   *                            shouldn't be changed.
   * @param aNewScope The destination global. A node's wrapper is preserved
   *                  unless it already lives in it. Unused when cloning.
   * @param aParent If aClone is true the cloned node will be appended to
   *                aParent's children. May be null. If not null then aNode
   *                must be an nsIContent.
   * @param aError The error, if any.
   * @param aFallbackRegistry A custom element registry to fallback to if
   *                          aNode's registry is null.
   *
   * @return If aClone is true then the cloned node will be returned,
   *          unless an error occurred.  In error conditions, null
   *          will be returned.
   */
  static already_AddRefed<nsINode> CloneAndAdopt(
      nsINode* aNode, bool aClone, bool aDeep,
      nsNodeInfoManager* aNewNodeInfoManager, nsIGlobalObject* aNewScope,
      nsINode* aParent, mozilla::ErrorResult& aError,
      mozilla::dom::CustomElementRegistry* aFallbackRegistry = nullptr);

 public:
  /**
   * Walks the node, its attributes and descendant nodes. If aNewNodeInfoManager
   * is not null, it is used to create new nodeinfos for the nodes.
   *
   * @param aNewNodeInfoManager The nodeinfo manager to use to create new
   *                            nodeinfos for the node and its attributes and
   *                            descendants. May be null if the nodeinfos
   *                            shouldn't be changed.
   * @param aError The error, if any.
   */
  void Adopt(nsNodeInfoManager* aNewNodeInfoManager,
             mozilla::ErrorResult& aError);

  /**
   * Clones the node, its attributes and, if aDeep is true, its descendant nodes
   * If aNewNodeInfoManager is not null, it is used to create new nodeinfos for
   * the clones.
   *
   * @param aDeep If true the function will be called recursively on
   *              descendants of the node
   * @param aNewNodeInfoManager The nodeinfo manager to use to create new
   *                            nodeinfos for the node and its attributes and
   *                            descendants. May be null if the nodeinfos
   *                            shouldn't be changed.
   * @param aError The error, if any.
   * @param aFallbackRegistry A custom element registry to fallback to if
   *                          aNode's registry is null.
   *
   * @return The newly created node.  Null in error conditions.
   */
  already_AddRefed<nsINode> Clone(
      bool aDeep, nsNodeInfoManager* aNewNodeInfoManager,
      mozilla::ErrorResult& aError,
      mozilla::dom::CustomElementRegistry* aFallbackRegistry = nullptr);

  /**
   * Clones this node. This needs to be overriden by all node classes. aNodeInfo
   * should be identical to this node's nodeInfo, except for the document which
   * may be different. When cloning an element, all attributes of the element
   * will be cloned. The children of the node will not be cloned.
   *
   * @param aNodeInfo the nodeinfo to use for the clone
   * @param aResult the clone
   */
  virtual nsresult Clone(mozilla::dom::NodeInfo*, nsINode** aResult) const = 0;

  // A callback that gets called when we are forcefully unbound from a node (due
  // to the node going away). You shouldn't take a strong ref to the node from
  // the callback.
  using UnbindCallback = void (*)(nsISupports*, nsINode*);
  // We should keep alive these objects.
  struct BoundObject {
    nsCOMPtr<nsISupports> mObject;
    UnbindCallback mDtor = nullptr;

    BoundObject(nsISupports* aObject, UnbindCallback aDtor)
        : mObject(aObject), mDtor(aDtor) {}

    bool operator==(nsISupports* aOther) const {
      return mObject.get() == aOther;
    }
  };

  // This class can be extended by subclasses that wish to store more
  // information in the slots.
  class nsSlots {
   public:
    nsSlots();

    // If needed we could remove the vtable pointer this dtor causes by
    // putting a DestroySlots function on nsINode
    virtual ~nsSlots();

    virtual void Traverse(nsCycleCollectionTraversalCallback&);
    virtual void Unlink(nsINode&);

    void ClearWeakReference();

    /**
     * A list of mutation observers
     */
    mozilla::SafeDoublyLinkedList<nsIMutationObserver> mMutationObservers;

    /**
     * The node's event listener manager.  Nodes without slots keep it in
     * nsINode::mSlotsOrListenerManager, documents in
     * Document::mListenerManager.  Handled by nsINode::Traverse and
     * DropNodeListenerManager, which cover the inline case too.
     */
    RefPtr<mozilla::EventListenerManager> mListenerManager;

    /**
     * An object implementing NodeList for this content (childNodes)
     * @see NodeList
     * @see nsGenericHTMLElement::GetChildNodes
     */
    RefPtr<nsAttrChildContentList> mChildNodes;

    /**
     * Weak reference to this node.  This is cleared by the destructor of
     * nsNodeWeakReference, and by ClearWeakReference when the node is
     * unlinked or destroyed.
     */
    nsNodeWeakReference* MOZ_NON_OWNING_REF mWeakReference;

    /** A list of objects that we should keep alive. See Bind/UnbindObject. */
    nsTArray<BoundObject> mBoundObjects;

    /**
     * A set of ranges which are in the selection and which have this node as
     * their endpoints' closest common inclusive ancestor
     * (https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor).  This is
     * a UniquePtr instead of just a LinkedList, because that prevents us from
     * pushing DOMSlots up to the next allocation bucket size, at the cost of
     * some complexity.
     */
    mozilla::UniquePtr<mozilla::LinkedList<mozilla::dom::AbstractRange>>
        mClosestCommonInclusiveAncestorRanges;
  };

  /**
   * Helper to allocate slot memory from the appropriate arena,
   * or from the heap if no arena is available.
   * Always returns allocated memory.
   */
  void* AllocateSlots(size_t aSize);

  /**
   * Functions for managing flags and slots
   */
#ifdef DEBUG
  nsSlots* DebugGetSlots() { return Slots(); }
#endif

  void SetFlags(FlagsType aFlagsToSet) {
    NS_ASSERTION(
        !(aFlagsToSet &
          (NODE_IS_NATIVE_ANONYMOUS_ROOT | NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE |
           NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME |
           NODE_HAS_BEEN_IN_UA_WIDGET)) ||
            IsContent(),
        "Flag only permitted on nsIContent nodes");
    nsWrapperCache::SetFlags(aFlagsToSet);
  }

  void UnsetFlags(FlagsType aFlagsToUnset) {
    NS_ASSERTION(!(aFlagsToUnset & (NODE_HAS_BEEN_IN_UA_WIDGET |
                                    NODE_IS_NATIVE_ANONYMOUS_ROOT)),
                 "Trying to unset write-only flags");
    nsWrapperCache::UnsetFlags(aFlagsToUnset);
  }

  void SetEditableFlag(bool aEditable) {
    if (aEditable) {
      SetFlags(NODE_IS_EDITABLE);
    } else {
      UnsetFlags(NODE_IS_EDITABLE);
    }
  }

  inline bool IsEditable() const { return HasFlag(NODE_IS_EDITABLE); }

  /**
   * Check if this node is an editing host. For avoiding confusion, this always
   * returns false if the node is in the design mode document.
   */
  inline bool IsEditingHost() const;

  /**
   * Check if this node is in design mode or not.  When this returns true and:
   * - if this is a Document node, it's the design mode root.
   * - if this is a content node, it's connected, it's not in a shadow tree
   *   (except shadow tree for UI widget and native anonymous subtree) and its
   *   uncomposed document is in design mode.
   * Note that returning true does NOT mean the node or its children is
   * editable.  E.g., when this node is in a shadow tree of a UA widget and its
   * host is in design mode.
   */
  inline bool IsInDesignMode() const;

  /**
   * Returns true if |this| or any of its ancestors is native anonymous.
   */
  bool IsInNativeAnonymousSubtree() const {
    return HasFlag(NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE);
  }

  /**
   * If |this| or any ancestor is native anonymous, return the root of the
   * native anonymous subtree. Note that in case of nested native anonymous
   * content, this returns the innermost root, not the outermost.
   */
  nsIContent* GetClosestNativeAnonymousSubtreeRoot() const {
    if (!IsInNativeAnonymousSubtree()) {
      MOZ_ASSERT(!HasBeenInUAWidget(), "UA widget implies anonymous");
      return nullptr;
    }
    MOZ_ASSERT(IsContent(), "How did non-content end up in NAC?");
    if (HasBeenInUAWidget()) {
      // reinterpret_cast because in this header we don't know ShadowRoot is an
      // nsIContent. ShadowRoot constructor asserts this is correct.
      return reinterpret_cast<nsIContent*>(GetContainingShadow());
    }
    for (const nsINode* node = this; node; node = node->GetParentNode()) {
      if (node->IsRootOfNativeAnonymousSubtree()) {
        return const_cast<nsINode*>(node)->AsContent();
      }
    }
    // FIXME(emilio): This should not happen, usually, but editor removes nodes
    // in native anonymous subtrees, and we don't clean nodes from the current
    // event content stack from ContentRemoved, so it can actually happen, see
    // bug 1510208.
    NS_WARNING("GetClosestNativeAnonymousSubtreeRoot on disconnected NAC!");
    return nullptr;
  }

  /**
   * If |this| or any ancestor is native anonymous, return the parent of the
   * native anonymous subtree. Note that in case of nested native anonymous
   * content, this returns the parent or host of the innermost root, not the
   * outermost.
   */
  nsIContent* GetClosestNativeAnonymousSubtreeRootParentOrHost() const {
    // We could put this in nsIContentInlines.h or such to avoid this
    // reinterpret_cast, but it doesn't seem worth it.
    const auto* root = reinterpret_cast<const nsINode*>(
        GetClosestNativeAnonymousSubtreeRoot());
    if (!root) {
      return nullptr;
    }
    if (nsIContent* parent = root->GetParent()) {
      return parent;
    }
    if (root->IsInShadowTree()) [[unlikely]] {
      return root->DoGetShadowHost();
    }
    return nullptr;
  }

  /**
   * Gets the root of the node tree for this content if it is in a shadow tree.
   *
   * NOTE: Return the shadow root if this is in any kind of shadow tree.
   * Therefore, the result may be non-content shadow root.
   * NOTE: This does not return a shadow root if this is an inclusive descendant
   * of a shadow host element in the DOM and the shadow host child is slotted to
   * a <slot> (and the shadow host is not a part of a shadow).
   */
  mozilla::dom::ShadowRoot* GetContainingShadow() const {
    return IsInShadowTree()
               ? reinterpret_cast<mozilla::dom::ShadowRoot*>(mSubtreeRoot)
               : nullptr;
  }

  /**
   * Gets the root of the node tree for this content if it is in a shadow tree
   * for selection.
   */
  [[nodiscard]] mozilla::dom::ShadowRoot* GetContainingShadowForSelection()
      const;

  template <TreeKind aKind>
  [[nodiscard]] mozilla::dom::ShadowRoot* GetContainingShadow() const {
    if constexpr (aKind == TreeKind::Flat) {
      return GetContainingShadow();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetContainingShadowForSelection();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * Gets the shadow host if this content is in a shadow tree. That is, the host
   * of |GetContainingShadow|, if its not null.
   *
   * @return The shadow host, if this is in shadow tree, or null.
   */
  mozilla::dom::Element* GetContainingShadowHost() const;

  /**
   * Gets the closest shadow root if:
   * - this node is connected to a shadow root
   * - an inclusive ancestor of this node is slotted to a <slot>
   */
  mozilla::dom::ShadowRoot* GetClosestShadowRootInFlattenedTree() const;

  /**
   * Gets the closest shadow root for selection if:
   * - this node is connected to a shadow root
   * - an inclusive ancestor of this node is slotted to a <slot>
   */
  mozilla::dom::ShadowRoot* GetClosestShadowRootInFlattenedTreeForSelection()
      const;

  template <TreeKind aKind>
  [[nodiscard]] mozilla::dom::ShadowRoot* GetClosestShadowRoot() const {
    if constexpr (aKind == TreeKind::Flat) {
      return GetClosestShadowRootInFlattenedTree();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetClosestShadowRootInFlattenedTreeForSelection();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  bool IsInSVGUseShadowTree() const {
    return !!GetContainingSVGUseShadowHost();
  }

  mozilla::dom::SVGUseElement* GetContainingSVGUseShadowHost() const {
    if (!IsInShadowTree()) {
      return nullptr;
    }
    return DoGetContainingSVGUseShadowHost();
  }

  // Whether this node has ever been part of a UA widget shadow tree.
  bool HasBeenInUAWidget() const { return HasFlag(NODE_HAS_BEEN_IN_UA_WIDGET); }

  // True for native anonymous content and for content in UA widgets.
  // Only nsIContent can fulfill this condition.
  bool ChromeOnlyAccess() const { return IsInNativeAnonymousSubtree(); }

  // Whether we're chrome-only for event targeting. UA widgets can use regular
  // shadow DOM retargeting for these.
  bool ChromeOnlyAccessForEvents() const {
    return ChromeOnlyAccess() && !HasBeenInUAWidget();
  }

  bool IsInShadowTree() const { return HasFlag(NODE_IS_IN_SHADOW_TREE); }

  /**
   * Get whether this node is C++-generated anonymous content
   * @see nsIAnonymousContentCreator
   * @return whether this content is anonymous
   */
  bool IsRootOfNativeAnonymousSubtree() const {
    NS_ASSERTION(
        !HasFlag(NODE_IS_NATIVE_ANONYMOUS_ROOT) || IsInNativeAnonymousSubtree(),
        "Some flags seem to be missing!");
    return HasFlag(NODE_IS_NATIVE_ANONYMOUS_ROOT);
  }

  // Whether this node is the root of a ChromeOnlyAccess DOM subtree.
  bool IsRootOfChromeAccessOnlySubtree() const {
    return IsRootOfNativeAnonymousSubtree();
  }

  /** Whether this is the container of a ::before pseudo-element. */
  bool IsGeneratedContentContainerForBefore() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentbefore;
  }

  /** Whether this is the container of an ::after pseudo-element. */
  bool IsGeneratedContentContainerForAfter() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentafter;
  }

  /** Whether this is the container of a ::marker pseudo-element. */
  bool IsGeneratedContentContainerForMarker() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentmarker;
  }

  /** Whether this is the container of a ::backdrop pseudo-element. */
  bool IsGeneratedContentContainerForBackdrop() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentbackdrop;
  }

  /** Whether this is the container of a ::checkmark pseudo-element. */
  bool IsGeneratedContentContainerForCheckmark() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentcheckmark;
  }

  /** Whether this is the container of a ::picker-icon pseudo-element. */
  bool IsGeneratedContentContainerForPickerIcon() const {
    return IsRootOfNativeAnonymousSubtree() &&
           mNodeInfo->NameAtom() == nsGkAtoms::mozgeneratedcontentpickericon;
  }

  /**
   * Returns true if |this| node is the closest common inclusive ancestor
   * (https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor) of the
   * start/end nodes of a Range in a Selection or a descendant of such a common
   * ancestor. This node is definitely not selected when |false| is returned,
   * but it may or may not be selected when |true| is returned.
   */
  bool IsMaybeSelected() const {
    return IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection() ||
           IsClosestCommonInclusiveAncestorForRangeInSelection();
  }

  /**
   * Return true if any part of (this, aStartOffset) .. (this, aEndOffset)
   * overlaps any Range in
   * GetClosestCommonInclusiveAncestorForRangeInSelection ranges (i.e.
   * where this is a descendant of a range's common inclusive ancestor node).
   * If a Range starts in (this, aEndOffset) or if it ends in
   * (this, aStartOffset) then it is non-overlapping and the result is false
   * for that Range.  Collapsed ranges always counts as non-overlapping.
   *
   * @param aStartOffset has to be less or equal to aEndOffset.
   * @param aCache A cache which contains all fully selected nodes for each
   *               selection. If present, this provides a fast path to check if
   *               a node is fully selected.
   */
  bool IsSelected(uint32_t aStartOffset, uint32_t aEndOffset,
                  mozilla::dom::SelectionNodeCache* aCache = nullptr) const;

#ifdef DEBUG
  void AssertIsRootElementSlow(bool) const;
#endif

  /** Returns whether we're the root element of our document. */
  bool IsRootElement() const {
    // This should be faster than pointer-chasing in the common cases, plus it
    // is also correct mid-unbind.
    const bool isRoot = !GetParent() && IsInUncomposedDoc() && IsElement();
#ifdef DEBUG
    AssertIsRootElementSlow(isRoot);
#endif
    return isRoot;
  }

  /**
   * Get the root element of the text editor associated with this node or the
   * root element of the text editor of the ancestor 'TextControlElement' if
   * this is in its native anonymous subtree.  I.e., this returns anonymous
   * `<div>` element of a `TextEditor`. Note that this can be used only for
   * getting root content of `<input>` or `<textarea>`.  I.e., this method
   * doesn't support HTML editors.
   */
  mozilla::dom::Element* GetAnonymousRootElementOfTextEditor();

  enum class IgnoreOwnIndependentSelection : bool { No, Yes };
  using AllowCrossShadowBoundary = mozilla::dom::AllowRangeCrossShadowBoundary;

  /**
   * Get the selection root for this node.
   * Note that if this node is not in an editor, the result comes from the
   * nsFrameSelection that is related to aPresShell, so the result might not be
   * the ancestor of this node.
   * Be aware that if this node and the computed selection limiter are not in
   * same subtree, this returns the root content of the closest subtree.
   *
   * @param aIgnoreOwnIndependentSelection
   *                    If "Yes", return selection root for selecting this node.
   *                    If "No", return independent selection root which is
   *                    in a native anonymous subtree hosted by this node.
   *                    For example, when this is a text control element,
   *                    return the document's selection root if "No" or return
   *                    the native anonymous <div> if "Yes".
   * @param aAllowCrossShadowBoundary
   *                    If "Yes", the result may be shadow including DOM
   *                    ancestor element.
   * @return Return the selection root element.
   *         - If this is in an editing host, return the editing host even if
   *           this is not editable.
   */
  nsIContent* GetSelectionRootContent(
      mozilla::PresShell* aPresShell,
      IgnoreOwnIndependentSelection aIgnoreOwnIndependentSelection,
      AllowCrossShadowBoundary aAllowCrossShadowBoundary);

  [[nodiscard]] nsFrameSelection* GetFrameSelection() const;

  bool HasScheduledSelectionChangeEvent() {
    return HasFlag(NODE_HAS_SCHEDULED_SELECTION_CHANGE_EVENT);
  }

  void SetHasScheduledSelectionChangeEvent() {
    SetFlags(NODE_HAS_SCHEDULED_SELECTION_CHANGE_EVENT);
  }

  void ClearHasScheduledSelectionChangeEvent() {
    UnsetFlags(NODE_HAS_SCHEDULED_SELECTION_CHANGE_EVENT);
  }

  mozilla::dom::NodeList* ChildNodes();

  /**
   * Return the first child of this.
   */
  [[nodiscard]] nsIContent* GetFirstChild() const { return mFirstChild; }

  /**
   * Return the last child of this.
   */
  [[nodiscard]] nsIContent* GetLastChild() const;

  /**
   * Return the first child in the flattened tree.
   * - If this is a <slot> whose assigned nodes list is not empty, this returns
   * the first item in the list.
   * - If this is a <slot> whose assigned nodes list is empty, this returns
   * the first child.
   * - If this is a shadow host element, this returns the first child of the
   * attached `ShadowRoot`.
   */
  [[nodiscard]] nsIContent* GetFlattenedTreeFirstChild() const;

  /**
   * Return the last child in the flattened tree.
   * - If this is a <slot> whose assigned nodes list is not empty, this returns
   * the last item in the list.
   * - If this is a <slot> whose assigned nodes list is empty, this returns
   * the last child.
   * - If this is a shadow host element, this returns the last child of the
   * attached `ShadowRoot`.
   */
  [[nodiscard]] nsIContent* GetFlattenedTreeLastChild() const;

  /**
   * Return the first child in the flattened tree for selection, i.e., this
   * does not handle assigned slot for non-content shadow root.
   * - If this is a <slot> whose assigned nodes list is not empty, this returns
   * the first item in the list.
   * - If this is a <slot> whose assigned nodes list is empty, this returns
   * the first child.
   * - If this is a shadow host element, this returns the first child of the
   * attached `ShadowRoot`.
   */
  [[nodiscard]] nsIContent* GetFlattenedTreeFirstChildForSelection() const;

  /**
   * Return the last child in the flattened tree for selection, i.e., this
   * does not handle assigned slot for non-content shadow root.
   * - If this is a <slot> whose assigned nodes list is not empty, this returns
   * the last item in the list.
   * - If this is a <slot> whose assigned nodes list is empty, this returns
   * the last child.
   * - If this is a shadow host element, this returns the last child of the
   * attached `ShadowRoot`.
   */
  [[nodiscard]] nsIContent* GetFlattenedTreeLastChildForSelection() const;

  template <TreeKind aKind>
  [[nodiscard]] nsIContent* GetFirstChild() const {
    static_assert(
        aKind != TreeKind::ShadowIncludingDOM,
        "It's unclear what this should return if this is a shadow host so that "
        "this does not support TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return GetFirstChild();
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetFlattenedTreeFirstChild();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetFlattenedTreeFirstChildForSelection();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  template <TreeKind aKind>
  [[nodiscard]] nsIContent* GetLastChild() const {
    static_assert(
        aKind != TreeKind::ShadowIncludingDOM,
        "It's unclear what this should return if this is a shadow host so that "
        "this does not support TreeKind::ShadowIncludingDOM");
    if constexpr (aKind == TreeKind::DOM) {
      return GetLastChild();
    } else if constexpr (aKind == TreeKind::Flat) {
      return GetFlattenedTreeLastChild();
    } else if constexpr (aKind == TreeKind::FlatForSelection) {
      return GetFlattenedTreeLastChildForSelection();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * Implementation is in Document.h, because it needs to cast from
   * Document* to nsINode*.
   */
  Document* GetOwnerDocument() const;

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Normalize();

  /**
   * Get the base URI for any relative URIs within this piece of
   * content. Generally, this is the document's base URI, but certain
   * content carries a local base for backward compatibility.
   *
   * @return the base URI.  May return null.
   */
  virtual nsIURI* GetBaseURI(bool aTryUseXHRDocBaseURI = false) const = 0;
  nsIURI* GetBaseURIObject() const;

  /**
   * Return true if the node may be apz aware. There are two cases. One is that
   * the node is apz aware (such as HTMLInputElement with number type). The
   * other is that the node has apz aware listeners. This is a non-virtual
   * function which calls IsNodeApzAwareInternal only when the MayBeApzAware is
   * set. We check the details in IsNodeApzAwareInternal which may be overriden
   * by child classes
   */
  bool IsNodeApzAware() const {
    return NodeMayBeApzAware() ? IsNodeApzAwareInternal() : false;
  }

  /**
   * Override this function and set the flag MayBeApzAware in case the node has
   * to let APZC be aware of it. It's used when the node may handle the apz
   * aware events and may do preventDefault to stop APZC to do default actions.
   *
   * For example, instead of scrolling page by APZ, we handle mouse wheel event
   * in HTMLInputElement with number type as increasing / decreasing its value.
   */
  virtual bool IsNodeApzAwareInternal() const;

  void GetTextContent(nsAString& aTextContent, mozilla::OOMReporter& aError) {
    GetTextContentInternal(aTextContent, aError);
  }
  MOZ_CAN_RUN_SCRIPT virtual void SetTextContent(
      const nsAString& aTextContent, nsIPrincipal* aSubjectPrincipal,
      mozilla::ErrorResult& aError) {
    SetTextContentInternal(aTextContent, aSubjectPrincipal, aError);
  }
  void SetTextContent(const nsAString& aTextContent,
                      mozilla::ErrorResult& aError) {
    SetTextContentInternal(aTextContent, nullptr, aError);
  }

  mozilla::dom::Element* QuerySelector(const nsACString& aSelector,
                                       mozilla::ErrorResult& aResult);
  already_AddRefed<mozilla::dom::NodeList> QuerySelectorAll(
      const nsACString& aSelector, mozilla::ErrorResult& aResult);

 protected:
  // Document and ShadowRoot override this with its own (faster) version.
  // This should really only be called for elements and document fragments.
  mozilla::dom::Element* GetElementById(const nsAString& aId);

  void AppendChildToChildList(nsIContent* aKid);
  void InsertChildToChildList(nsIContent* aKid, nsIContent* aNextSibling);
  void DisconnectChild(nsIContent* aKid);

 public:
  void LookupPrefix(const nsAString& aNamespace, nsAString& aResult);
  bool IsDefaultNamespace(const nsAString& aNamespaceURI) {
    nsAutoString defaultNamespace;
    LookupNamespaceURI(u""_ns, defaultNamespace);
    return aNamespaceURI.Equals(defaultNamespace);
  }
  void LookupNamespaceURI(const nsAString& aNamespacePrefix,
                          nsAString& aNamespaceURI);

  /**
   * Return the next sibling of this in the child list.
   */
  nsIContent* GetNextSibling() const { return mNextSibling; }

  /**
   * Return the previous sibling of this in the child list.
   * FYI: This is a bit slower than GetNextSibling() so that you should use
   * GetNextSibling() if you want to walk all children in a node.
   */
  nsIContent* GetPreviousSibling() const;

  /**
   * Return the next/previous sibling in the flattened tree (for selection).
   *
   * These methods need to compute the index of this if this is assigned to a
   * <slot>. Therefore, they may work slow. Therefore, these shorthands are
   * deleted to realize the cost. You should use FlattenedChildIterator instead.
   * For example:
   *
   * FlattenedChildIterator iter(
   *   FlattenedChildIterator::GetParentNodeOf(node));
   * iter.Seek(node);
   * auto* nextSibling = iter.GetNextChild();
   *
   * or if you want only the next sibling, you can do:
   *
   * auto* nextSibling = FlattenedChildIterator::GetNextChild(child);
   */
  nsIContent* GetFlattenedTreeNextSibling() const = delete;
  nsIContent* GetFlattenedTreePreviousSibling() const = delete;
  nsIContent* GetFlattenedTreeNextSiblingForSelection() const = delete;
  nsIContent* GetFlattenedTreePreviousSiblingForSelection() const = delete;

  /**
   * Return true if the node is being removed from the parent, it means that
   * the node still knows the container which it's disconnected from, but the
   * node has already been removed from the child node chain of the container.
   * I.e., Return true between a call of DisconnectChild of the parent and
   * a call of UnbindFromTree of the node.
   */
  bool IsBeingRemoved() const {
    return mParent && !mNextSibling && !mPreviousOrLastSibling;
  }

  /**
   * Get the next node in the pre-order tree traversal of the DOM.  If
   * aRoot is non-null, then it must be an ancestor of |this|
   * (possibly equal to |this|) and only nodes that are descendants of
   * aRoot, not including aRoot itself, will be returned.  Returns
   * null if there are no more nodes to traverse.
   */
  nsIContent* GetNextNode(const nsINode* aRoot = nullptr) const {
    return GetNextNodeImpl(aRoot, false);
  }

  /**
   * Get the next node in the pre-order tree traversal of the DOM but ignoring
   * the children of this node.  If aRoot is non-null, then it must be an
   * ancestor of |this| (possibly equal to |this|) and only nodes that are
   * descendants of aRoot, not including aRoot itself, will be returned.
   * Returns null if there are no more nodes to traverse.
   */
  nsIContent* GetNextNonChildNode(const nsINode* aRoot = nullptr) const {
    return GetNextNodeImpl(aRoot, true);
  }

  /**
   * Returns true if 'this' is either document or element or
   * document fragment and aOther is a descendant in the same
   * anonymous tree.
   */
  bool Contains(const nsINode* aOther) const;

  bool UnoptimizableCCNode() const;

  /**
   * Return true if the DevTools is observing the mutations in the owner
   * document.
   */
  [[nodiscard]] bool MaybeNeedsToNotifyDevToolsOfNodeRemovalsInOwnerDoc() const;

  /**
   * Return true when the DevTools should be notified of the removal of this
   * node.
   */
  [[nodiscard]] bool DevToolsShouldBeNotifiedOfThisRemoval() const;

  /**
   * Notify DevTools of the removals of all children of this node.
   * TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void NotifyDevToolsOfRemovalsOfChildren();

  void QueueDevtoolsAnonymousEvent(bool aIsRemove);

 private:
  mozilla::dom::SVGUseElement* DoGetContainingSVGUseShadowHost() const;

  nsIContent* GetNextNodeImpl(const nsINode* aRoot,
                              const bool aSkipChildren) const {
#ifdef DEBUG
    if (aRoot) {
      // TODO: perhaps nsINode::IsInclusiveDescendantOf could be used instead.
      const nsINode* cur = this;
      for (; cur; cur = cur->GetParentNode())
        if (cur == aRoot) break;
      NS_ASSERTION(cur, "aRoot not an ancestor of |this|?");
    }
#endif
    if (!aSkipChildren) {
      nsIContent* kid = GetFirstChild();
      if (kid) {
        return kid;
      }
    }
    if (this == aRoot) {
      return nullptr;
    }
    const nsINode* cur = this;
    while (true) {
      nsIContent* next = cur->GetNextSibling();
      if (next) {
        return next;
      }
      nsINode* parent = cur->GetParentNode();
      if (parent == aRoot) {
        return nullptr;
      }
      cur = parent;
    }
    MOZ_ASSERT_UNREACHABLE("How did we get here?");
  }

 public:
  /**
   * Get the previous nsIContent in the pre-order tree traversal of the DOM.  If
   * aRoot is non-null, then it must be an ancestor of |this|
   * (possibly equal to |this|) and only nsIContents that are descendants of
   * aRoot, including aRoot itself, will be returned.  Returns
   * null if there are no more nsIContents to traverse.
   */
  nsIContent* GetPrevNode(const nsINode* aRoot = nullptr) const {
#ifdef DEBUG
    if (aRoot) {
      // TODO: perhaps nsINode::IsInclusiveDescendantOf could be used instead.
      const nsINode* cur = this;
      for (; cur; cur = cur->GetParentNode())
        if (cur == aRoot) break;
      NS_ASSERTION(cur, "aRoot not an ancestor of |this|?");
    }
#endif

    if (this == aRoot) {
      return nullptr;
    }
    nsIContent* cur = this->GetParent();
    nsIContent* iter = this->GetPreviousSibling();
    while (iter) {
      cur = iter;
      iter = reinterpret_cast<nsINode*>(iter)->GetLastChild();
    }
    return cur;
  }

  /**
   * Boolean flags
   */
 private:
  enum BooleanFlag {
    // Set if we're being used from -moz-element or observed via a mask,
    // clipPath, filter or use element.
    NodeHasDirectRenderingObservers,
    // Set if our parent chain (including this node itself) terminates
    // in a document
    IsInDocument,
    // Set if we're part of the composed doc.
    // https://dom.spec.whatwg.org/#connected
    IsConnected,
    // Set if mParent is an nsIContent
    ParentIsContent,
    // Set if this node is an Element
    NodeIsElement,
    // Set if the element has a non-empty id attribute. This can in rare
    // cases lie for nsXMLElement, such as when the node has been moved between
    // documents with different id mappings.
    ElementHasID,
    // Set if the element might have a class.
    ElementMayHaveClass,
    // Set if the element might have inline style.
    ElementMayHaveStyle,
    // Set if the element has a name attribute set.
    ElementHasName,
    // Set if the element has a part attribute set.
    ElementHasPart,
    // Set if the element might have a contenteditable attribute set.
    ElementMayHaveContentEditableAttr,
    // Set if the element has a contenteditable attribute whose value makes the
    // element editable.
    ElementHasContentEditableAttrTrueOrPlainTextOnly,
    // Set if the node is the closest common inclusive ancestor of the start/end
    // nodes of a Range that is in a Selection.
    NodeIsClosestCommonInclusiveAncestorForRangeInSelection,
    // Set if the node is a descendant of a node with the above bit set.
    NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection,
    // Set if CanSkipInCC check has been done for this subtree root.
    NodeIsCCMarkedRoot,
    // Maybe set if this node is in black subtree.
    NodeIsCCBlackTree,
    // Maybe set if the node is a root of a subtree
    // which needs to be kept in the purple buffer.
    NodeIsPurpleRoot,
    // Set if the element has some style states locked
    ElementHasLockedStyleStates,
    // Set if element has pointer locked
    ElementHasPointerLock,
    // Set if the node may have DOMMutationObserver attached to it.
    NodeMayHaveDOMMutationObserver,
    // Set if node is Content
    NodeIsContent,
    // Set if the node has animations or transitions
    ElementHasAnimations,
    // Set if node has a dir attribute with a valid value (ltr, rtl, or auto).
    // Note that we cannot compute this from the dir attribute event state
    // flags, because we can't use those to distinguish
    // <bdi dir="some-invalid-value"> and <bdi dir="auto">.
    NodeHasValidDirAttribute,
    // Set if a node in the node's parent chain has dir=auto and nothing
    // inbetween nor the node itself establishes its own direction.
    NodeAncestorHasDirAuto,
    // Set if the node or an ancestor is assigned to a dir=auto slot and
    // nothing between nor the node itself establishes its own direction.
    // Except for when the node assigned to the dir=auto slot establishes
    // its own direction, then the flag is still set.
    NodeAffectsDirAutoSlot,
    // Set if the node is handling a click.
    NodeHandlingClick,
    // Set if the element has a parser insertion mode other than "in body",
    // per the HTML5 "Parse state" section.
    ElementHasWeirdParserInsertionMode,
    // Parser sets this flag if it has notified about the node.
    ParserHasNotified,
    // Sets if the node is apz aware or we have apz aware listeners.
    MayBeApzAware,
    // Set if the element might have any kind of anonymous content children,
    // which would not be found through the element's children list.
    ElementMayHaveAnonymousChildren,
    // Set if element has CustomElementData.
    ElementHasCustomElementData,
    // Set if the element was created already-translated from prototype cache
    // and its l10n attributes haven't been changed.
    ElementTranslatedFromPrototype,
    // Guard value
    BooleanFlagCount
  };

  void SetBoolFlag(BooleanFlag name, bool value) {
    static_assert(BooleanFlagCount <= 8 * sizeof(mBoolFlags),
                  "Too many boolean flags");
    mBoolFlags = (mBoolFlags & ~(1U << name)) | (value << name);
  }

  void SetBoolFlag(BooleanFlag name) {
    static_assert(BooleanFlagCount <= 8 * sizeof(mBoolFlags),
                  "Too many boolean flags");
    mBoolFlags |= (1U << name);
  }

  void ClearBoolFlag(BooleanFlag name) {
    static_assert(BooleanFlagCount <= 8 * sizeof(mBoolFlags),
                  "Too many boolean flags");
    mBoolFlags &= ~(1U << name);
  }

  bool GetBoolFlag(BooleanFlag name) const {
    static_assert(BooleanFlagCount <= 8 * sizeof(mBoolFlags),
                  "Too many boolean flags");
    return mBoolFlags & (1U << name);
  }

 public:
  bool HasDirectRenderingObservers() const {
    return GetBoolFlag(NodeHasDirectRenderingObservers);
  }
  void SetHasDirectRenderingObservers(bool aValue) {
    SetBoolFlag(NodeHasDirectRenderingObservers, aValue);
  }
  bool IsContent() const { return GetBoolFlag(NodeIsContent); }
  bool HasID() const { return GetBoolFlag(ElementHasID); }
  bool MayHaveClass() const { return GetBoolFlag(ElementMayHaveClass); }
  void SetMayHaveClass() { SetBoolFlag(ElementMayHaveClass); }
  bool MayHaveStyle() const { return GetBoolFlag(ElementMayHaveStyle); }
  bool HasName() const { return GetBoolFlag(ElementHasName); }
  bool HasPartAttribute() const { return GetBoolFlag(ElementHasPart); }
  bool MayHaveContentEditableAttr() const {
    return GetBoolFlag(ElementMayHaveContentEditableAttr);
  }
  /**
   * HasContentEditableAttrTrueOrPlainTextOnly() should not be called between
   * nsGenericHTMLElement::BeforeSetAttr and nsGenericHTMLElement::AfterSetAttr
   * because this is set and cleared by nsGenericHTMLElement::AfterSetAttr.
   */
  bool HasContentEditableAttrTrueOrPlainTextOnly() const {
    return GetBoolFlag(ElementHasContentEditableAttrTrueOrPlainTextOnly);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  bool IsClosestCommonInclusiveAncestorForRangeInSelection() const {
    return GetBoolFlag(NodeIsClosestCommonInclusiveAncestorForRangeInSelection);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void SetClosestCommonInclusiveAncestorForRangeInSelection() {
    SetBoolFlag(NodeIsClosestCommonInclusiveAncestorForRangeInSelection);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void ClearClosestCommonInclusiveAncestorForRangeInSelection() {
    ClearBoolFlag(NodeIsClosestCommonInclusiveAncestorForRangeInSelection);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  bool IsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection() const {
    return GetBoolFlag(
        NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void SetDescendantOfClosestCommonInclusiveAncestorForRangeInSelection() {
    SetBoolFlag(
        NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection);
  }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void ClearDescendantOfClosestCommonInclusiveAncestorForRangeInSelection() {
    ClearBoolFlag(
        NodeIsDescendantOfClosestCommonInclusiveAncestorForRangeInSelection);
  }

  void SetCCMarkedRoot(bool aValue) { SetBoolFlag(NodeIsCCMarkedRoot, aValue); }
  bool CCMarkedRoot() const { return GetBoolFlag(NodeIsCCMarkedRoot); }
  void SetInCCBlackTree(bool aValue) { SetBoolFlag(NodeIsCCBlackTree, aValue); }
  bool InCCBlackTree() const { return GetBoolFlag(NodeIsCCBlackTree); }
  void SetIsPurpleRoot(bool aValue) { SetBoolFlag(NodeIsPurpleRoot, aValue); }
  bool IsPurpleRoot() const { return GetBoolFlag(NodeIsPurpleRoot); }
  bool MayHaveDOMMutationObserver() {
    return GetBoolFlag(NodeMayHaveDOMMutationObserver);
  }
  void SetMayHaveDOMMutationObserver() {
    SetBoolFlag(NodeMayHaveDOMMutationObserver, true);
  }
  bool HasListenerManager() { return HasFlag(NODE_HAS_LISTENERMANAGER); }
  bool HasPointerLock() const { return GetBoolFlag(ElementHasPointerLock); }
  void SetPointerLock() { SetBoolFlag(ElementHasPointerLock); }
  void ClearPointerLock() { ClearBoolFlag(ElementHasPointerLock); }
  bool MayHaveAnimations() const { return GetBoolFlag(ElementHasAnimations); }
  void SetMayHaveAnimations() { SetBoolFlag(ElementHasAnimations); }
  void ClearMayHaveAnimations() { ClearBoolFlag(ElementHasAnimations); }
  void SetHasValidDir() { SetBoolFlag(NodeHasValidDirAttribute); }
  void ClearHasValidDir() { ClearBoolFlag(NodeHasValidDirAttribute); }
  bool HasValidDir() const { return GetBoolFlag(NodeHasValidDirAttribute); }
  void SetAncestorHasDirAuto() { SetBoolFlag(NodeAncestorHasDirAuto); }
  void ClearAncestorHasDirAuto() { ClearBoolFlag(NodeAncestorHasDirAuto); }
  bool AncestorHasDirAuto() const {
    return GetBoolFlag(NodeAncestorHasDirAuto);
  }
  void SetAffectsDirAutoSlot() { SetBoolFlag(NodeAffectsDirAutoSlot); }
  void ClearAffectsDirAutoSlot() { ClearBoolFlag(NodeAffectsDirAutoSlot); }

  // Set if the node or an ancestor is assigned to a dir=auto slot.
  bool AffectsDirAutoSlot() const {
    return GetBoolFlag(NodeAffectsDirAutoSlot);
  }

  // Implemented in nsIContentInlines.h.
  inline bool NodeOrAncestorHasDirAuto() const;

  void SetParserHasNotified() { SetBoolFlag(ParserHasNotified); };
  bool HasParserNotified() { return GetBoolFlag(ParserHasNotified); }

  void SetMayBeApzAware() { SetBoolFlag(MayBeApzAware); }
  bool NodeMayBeApzAware() const { return GetBoolFlag(MayBeApzAware); }

  void SetMayHaveAnonymousChildren() {
    SetBoolFlag(ElementMayHaveAnonymousChildren);
  }
  bool MayHaveAnonymousChildren() const {
    return GetBoolFlag(ElementMayHaveAnonymousChildren);
  }

  void SetHasCustomElementData() { SetBoolFlag(ElementHasCustomElementData); }
  bool HasCustomElementData() const {
    return GetBoolFlag(ElementHasCustomElementData);
  }
  void ClearHasCustomElementData() {
    ClearBoolFlag(ElementHasCustomElementData);
  }

  // Whether looking up a custom element registry for this node would yield a
  // scoped (non-global) registry.
  inline bool HasScopedRegistry() const;

  void SetElementTranslatedFromPrototype() {
    SetBoolFlag(ElementTranslatedFromPrototype);
  }
  bool IsElementTranslatedFromPrototype() {
    return GetBoolFlag(ElementTranslatedFromPrototype);
  }
  void ClearElementTranslatedFromPrototype() {
    ClearBoolFlag(ElementTranslatedFromPrototype);
  }

  [[nodiscard]] inline mozilla::dom::ShadowRoot* GetShadowRoot() const;

  // Return the shadow root of the node if it is a shadow host and
  // it meets the requirements for being a shadow host of a selection.
  // For example, <details>, <video> and <use> elements are not valid
  // shadow host for selection.
  [[nodiscard]] mozilla::dom::ShadowRoot* GetShadowRootForSelection() const;

  template <TreeKind aKind>
  [[nodiscard]] mozilla::dom::ShadowRoot* GetShadowRoot() const {
    if constexpr (aKind == TreeKind::DOM) {
      return nullptr;
    } else if constexpr (aKind == TreeKind::ShadowIncludingDOM ||
                         aKind == TreeKind::FlatForSelection) {
      MOZ_ASSERT(ShouldIgnoreNonContentShadow<aKind>());
      return GetShadowRootForSelection();
    } else if constexpr (aKind == TreeKind::Flat) {
      MOZ_ASSERT(!ShouldIgnoreNonContentShadow<aKind>());
      return GetShadowRoot();
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Handle the new TreeKind value");
    }
  }

  /**
   * If this node is a non-flattened node in the flattened tree, i.e., if this
   * node is an inclusive descendant of:
   * - a child of <slot> which has some assigned nodes
   * - a child of shadow host element and not assigned to a <slot>
   * then, return the shadow host element or the <slot> which exclude an
   * inclusive ancestor of this node from the flattened tree. Otherwise, i.e.,
   * this node is a part of the flattened tree, return nullptr.
   */
  template <TreeKind aKind,
            typename = std::enable_if_t<aKind == TreeKind::Flat ||
                                        aKind == TreeKind::FlatForSelection>>
  [[nodiscard]] mozilla::dom::Element*
  GetClosestFlatTreeAncestorElementForNonFlatTreeNode() const;

  /**
   * If this node is a non-flattened node in the flattened tree, i.e., if this
   * node is an inclusive descendant of:
   * - a child of <slot> which has some assigned nodes
   * - a child of shadow host element and not assigned to a <slot>
   * then, return the most distant <slot> or shadow host element which exclude
   * an inclusive ancestor of this node from the flattened tree. Otherwise,
   * i.e., this node is a part of the flattened tree, return nullptr.
   */
  template <TreeKind aKind,
            typename = std::enable_if_t<aKind == TreeKind::Flat ||
                                        aKind == TreeKind::FlatForSelection>>
  [[nodiscard]] mozilla::dom::Element*
  GetFlatTreeAncestorElementForNonFlatTreeNode() const;

 protected:
  void SetParentIsContent(bool aValue) { SetBoolFlag(ParentIsContent, aValue); }
  void SetIsInDocument() { SetBoolFlag(IsInDocument); }
  void ClearInDocument() { ClearBoolFlag(IsInDocument); }
  void SetIsConnected(bool aConnected) { SetBoolFlag(IsConnected, aConnected); }
  void SetNodeIsContent() { SetBoolFlag(NodeIsContent); }
  void SetIsElement() { SetBoolFlag(NodeIsElement); }
  void SetHasID() { SetBoolFlag(ElementHasID); }
  void ClearHasID() { ClearBoolFlag(ElementHasID); }
  void SetMayHaveStyle() { SetBoolFlag(ElementMayHaveStyle); }
  void SetHasName() { SetBoolFlag(ElementHasName); }
  void ClearHasName() { ClearBoolFlag(ElementHasName); }
  void SetHasPartAttribute(bool aPart) { SetBoolFlag(ElementHasPart, aPart); }
  void SetMayHaveContentEditableAttr() {
    SetBoolFlag(ElementMayHaveContentEditableAttr);
  }
  void ClearMayHaveContentEditableAttr() {
    ClearBoolFlag(ElementMayHaveContentEditableAttr);
  }
  void SetHasContentEditableAttrTrueOrPlainTextOnly() {
    SetBoolFlag(ElementHasContentEditableAttrTrueOrPlainTextOnly);
  }
  void ClearHasContentEditableAttrTrueOrPlainTextOnly() {
    ClearBoolFlag(ElementHasContentEditableAttrTrueOrPlainTextOnly);
  }
  void SetHasLockedStyleStates() { SetBoolFlag(ElementHasLockedStyleStates); }
  void ClearHasLockedStyleStates() {
    ClearBoolFlag(ElementHasLockedStyleStates);
  }
  bool HasLockedStyleStates() const {
    return GetBoolFlag(ElementHasLockedStyleStates);
  }
  void SetHasWeirdParserInsertionMode() {
    SetBoolFlag(ElementHasWeirdParserInsertionMode);
  }
  bool HasWeirdParserInsertionMode() const {
    return GetBoolFlag(ElementHasWeirdParserInsertionMode);
  }
  bool HandlingClick() const { return GetBoolFlag(NodeHandlingClick); }
  void SetHandlingClick() { SetBoolFlag(NodeHandlingClick); }
  void ClearHandlingClick() { ClearBoolFlag(NodeHandlingClick); }

  void SetSubtreeRootPointer(nsINode* aSubtreeRoot) {
    MOZ_ASSERT(aSubtreeRoot, "aSubtreeRoot can never be null!");
    mSubtreeRoot = aSubtreeRoot;
  }

 public:
  // Makes nsINode object keep aObject alive. If a callback is provided, it's
  // called before deleting the node.
  void BindObject(nsISupports* aObject, UnbindCallback = nullptr);
  // After calling UnbindObject nsINode, object doesn't keep aObject alive
  // anymore.
  void UnbindObject(nsISupports* aObject);

  void GenerateXPath(nsAString& aResult);

  already_AddRefed<mozilla::dom::AccessibleNode> GetAccessibleNode();

  /**
   * Returns the length of this node, as specified at
   * <http://dvcs.w3.org/hg/domcore/raw-file/tip/Overview.html#concept-node-length>
   */
  uint32_t Length() const;

  void GetNodeName(mozilla::dom::DOMString& aNodeName) {
    const nsString& nodeName = NodeName();
    aNodeName.SetKnownLiveString(nodeName);
  }
  [[nodiscard]] nsresult GetBaseURI(nsAString& aBaseURI) const;
  // Return the base URI for the document.
  // The returned value may differ if the document is loaded via XHR, and
  // when accessed from chrome privileged script and
  // from content privileged script for compatibility.
  void GetBaseURIFromJS(nsAString& aBaseURI, CallerType aCallerType,
                        ErrorResult& aRv) const;
  bool HasChildNodes() const { return HasChildren(); }

  uint16_t CompareDocumentPosition(const nsINode& aOther) const;
  void GetNodeValue(nsAString& aNodeValue) { GetNodeValueInternal(aNodeValue); }
  MOZ_CAN_RUN_SCRIPT virtual void SetNodeValue(const nsAString& aNodeValue,
                                               mozilla::ErrorResult& aError) {
    SetNodeValueInternal(aNodeValue, aError);
  }
  virtual void GetNodeValueInternal(nsAString& aNodeValue);
  virtual void SetNodeValueInternal(
      const nsAString& aNodeValue, mozilla::ErrorResult& aError,
      MutationEffectOnScript aMutationEffectOnScript =
          MutationEffectOnScript::DropTrustWorthiness) {
    // The DOM spec says that when nodeValue is defined to be null "setting it
    // has no effect", so we don't throw an exception.
  }
  void EnsurePreInsertionValidity(nsINode& aNewChild, nsINode* aRefChild,
                                  mozilla::ErrorResult& aError);
  nsINode* InsertBefore(nsINode& aNode, nsINode* aChild,
                        mozilla::ErrorResult& aError) {
    return InsertBeforeInternal(
        aNode, aChild, MutationEffectOnScript::DropTrustWorthiness, aError);
  }
  nsINode* InsertBeforeInternal(nsINode& aNode, nsINode* aChild,
                                MutationEffectOnScript aMutationEffectOnScript,
                                mozilla::ErrorResult& aError) {
    return ReplaceOrInsertBefore(false, &aNode, aChild, aMutationEffectOnScript,
                                 aError);
  }

  /**
   * See <https://dom.spec.whatwg.org/#dom-node-appendchild>.
   */
  nsINode* AppendChild(nsINode& aNode, mozilla::ErrorResult& aError) {
    return AppendChildInternal(
        aNode, MutationEffectOnScript::DropTrustWorthiness, aError);
  }
  nsINode* AppendChildInternal(nsINode& aNode,
                               MutationEffectOnScript aMutationEffectOnScript,
                               mozilla::ErrorResult& aError) {
    return InsertBeforeInternal(aNode, nullptr, aMutationEffectOnScript,
                                aError);
  }

  nsINode* ReplaceChild(nsINode& aNode, nsINode& aChild,
                        mozilla::ErrorResult& aError) {
    return ReplaceChildInternal(
        aNode, aChild, MutationEffectOnScript::DropTrustWorthiness, aError);
  }
  nsINode* ReplaceChildInternal(nsINode& aNode, nsINode& aChild,
                                MutationEffectOnScript aMutationEffectOnScript,
                                mozilla::ErrorResult& aError) {
    return ReplaceOrInsertBefore(true, &aNode, &aChild, aMutationEffectOnScript,
                                 aError);
  }

  nsINode* RemoveChild(nsINode& aChild, mozilla::ErrorResult& aError) {
    return RemoveChildInternal(
        aChild, MutationEffectOnScript::DropTrustWorthiness, aError);
  }
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsINode* RemoveChildInternal(
      nsINode& aChild, MutationEffectOnScript aMutationEffectOnScript,
      mozilla::ErrorResult& aError);
  already_AddRefed<nsINode> CloneNode(bool aDeep, mozilla::ErrorResult& aError);
  bool IsSameNode(nsINode* aNode);
  bool IsEqualNode(nsINode* aNode);
  void GetNamespaceURI(nsAString& aNamespaceURI) const {
    mNodeInfo->GetNamespaceURI(aNamespaceURI);
  }
#ifdef MOZILLA_INTERNAL_API
  void GetPrefix(nsAString& aPrefix) { mNodeInfo->GetPrefix(aPrefix); }
#endif
  void GetLocalName(mozilla::dom::DOMString& aLocalName) const {
    const nsString& localName = LocalName();
    aLocalName.SetKnownLiveString(localName);
  }

  nsDOMAttributeMap* GetAttributes();

  // ChildNode methods
  inline mozilla::dom::Element* GetPreviousElementSibling() const;
  inline mozilla::dom::Element* GetNextElementSibling() const;

  MOZ_CAN_RUN_SCRIPT void Before(const Sequence<OwningNodeOrString>& aNodes,
                                 ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void After(const Sequence<OwningNodeOrString>& aNodes,
                                ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void ReplaceWith(
      const Sequence<OwningNodeOrString>& aNodes, ErrorResult& aRv);
  /**
   * Remove this node from its parent, if any.
   */
  void Remove();

  // ParentNode methods
  mozilla::dom::Element* GetFirstElementChild() const;
  mozilla::dom::Element* GetLastElementChild() const;

  already_AddRefed<mozilla::dom::HTMLCollection> GetElementsByAttribute(
      const nsAString& aAttribute, const nsAString& aValue);
  already_AddRefed<mozilla::dom::HTMLCollection> GetElementsByAttributeNS(
      const nsAString& aNamespaceURI, const nsAString& aAttribute,
      const nsAString& aValue, ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void Prepend(const Sequence<OwningNodeOrString>& aNodes,
                                  ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void Append(const Sequence<OwningNodeOrString>& aNodes,
                                 ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void ReplaceChildren(
      const Sequence<OwningNodeOrString>& aNodes, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void ReplaceChildren(
      nsINode* aNode, ErrorResult& aRv,
      MutationEffectOnScript aMutationEffectOnScript =
          MutationEffectOnScript::DropTrustWorthiness);

  MOZ_CAN_RUN_SCRIPT void MoveBefore(nsINode& aNode, nsINode* aChild,
                                     ErrorResult& aRv);

  void GetBoxQuads(const BoxQuadOptions& aOptions,
                   nsTArray<RefPtr<DOMQuad>>& aResult, CallerType aCallerType,
                   ErrorResult& aRv);

  void GetBoxQuadsFromWindowOrigin(const BoxQuadOptions& aOptions,
                                   nsTArray<RefPtr<DOMQuad>>& aResult,
                                   ErrorResult& aRv);

  already_AddRefed<DOMQuad> ConvertQuadFromNode(
      DOMQuad& aQuad, const TextOrElementOrDocument& aFrom,
      const ConvertCoordinateOptions& aOptions, CallerType aCallerType,
      ErrorResult& aRv);
  already_AddRefed<DOMQuad> ConvertRectFromNode(
      DOMRectReadOnly& aRect, const TextOrElementOrDocument& aFrom,
      const ConvertCoordinateOptions& aOptions, CallerType aCallerType,
      ErrorResult& aRv);
  already_AddRefed<DOMPoint> ConvertPointFromNode(
      const DOMPointInit& aPoint, const TextOrElementOrDocument& aFrom,
      const ConvertCoordinateOptions& aOptions, CallerType aCallerType,
      ErrorResult& aRv);

  /**
   * See nsSlots::mClosestCommonInclusiveAncestorRanges.
   */
  const mozilla::LinkedList<mozilla::dom::AbstractRange>*
  GetExistingClosestCommonInclusiveAncestorRanges() const {
    if (!HasSlots()) {
      return nullptr;
    }
    return GetExistingSlots()->mClosestCommonInclusiveAncestorRanges.get();
  }

  /**
   * See nsSlots::mClosestCommonInclusiveAncestorRanges.
   */
  mozilla::LinkedList<mozilla::dom::AbstractRange>*
  GetExistingClosestCommonInclusiveAncestorRanges() {
    if (!HasSlots()) {
      return nullptr;
    }
    return GetExistingSlots()->mClosestCommonInclusiveAncestorRanges.get();
  }

  /**
   * See nsSlots::mClosestCommonInclusiveAncestorRanges.
   */
  mozilla::UniquePtr<mozilla::LinkedList<mozilla::dom::AbstractRange>>&
  GetClosestCommonInclusiveAncestorRangesPtr() {
    return Slots()->mClosestCommonInclusiveAncestorRanges;
  }

  nsIWeakReference* GetExistingWeakReference() {
    return HasSlots() ? GetExistingSlots()->mWeakReference : nullptr;
  }

  void QueueAncestorRevealingAlgorithm();

  MOZ_CAN_RUN_SCRIPT void AncestorRevealingAlgorithm(ErrorResult& aRv);

 protected:
  // Override this function to create a custom slots class.
  // Must not return null.
  virtual nsINode::nsSlots* CreateSlots();

  bool HasSlots() const {
    return !(mSlotsOrListenerManager & kListenerManagerBit);
  }

  nsSlots* GetExistingSlots() const {
    return HasSlots() ? reinterpret_cast<nsSlots*>(mSlotsOrListenerManager)
                      : nullptr;
  }

  nsSlots* Slots() {
    if (!HasSlots()) {
      SetSlots(CreateSlots());
    }
    return GetExistingSlots();
  }

  // Takes ownership of aSlots, moving any inline listener manager into it.
  // There must be no slots yet.
  void SetSlots(nsSlots* aSlots);

  /**
   * Invalidate cached child array inside mChildNodes
   * of type nsParentNodeChildContentList.
   */
  void InvalidateChildNodes();

  virtual void GetTextContentInternal(nsAString& aTextContent,
                                      mozilla::OOMReporter& aError);
  virtual void SetTextContentInternal(
      const nsAString& aTextContent, nsIPrincipal* aSubjectPrincipal,
      mozilla::ErrorResult& aError,
      MutationEffectOnScript aMutationEffectOnScript =
          MutationEffectOnScript::DropTrustWorthiness) {}

  void EnsurePreInsertionValidity1(mozilla::ErrorResult& aError);
  void EnsurePreInsertionValidity2(bool aReplace, nsINode& aNewChild,
                                   nsINode* aRefChild,
                                   mozilla::ErrorResult& aError);
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsINode* ReplaceOrInsertBefore(
      bool aReplace, nsINode* aNewChild, nsINode* aRefChild,
      MutationEffectOnScript aMutationEffectOnScript,
      mozilla::ErrorResult& aError);

  /**
   * Returns the Element that should be used for resolving namespaces
   * on this node (ie the ownerElement for attributes, the documentElement for
   * documents, the node itself for elements and for other nodes the parentNode
   * if it is an element).
   */
  virtual mozilla::dom::Element* GetNameSpaceElement() = 0;

  /**
   * Parse the given selector string into a servo SelectorList.
   *
   * Never returns null if aRv is not failing.
   *
   * Note that the selector list returned here is owned by the owner doc's
   * selector cache.
   */
  const mozilla::StyleSelectorList* ParseSelectorList(
      const nsACString& aSelectorString, mozilla::ErrorResult&);

 public:
  /* Event stuff that documents and elements share.

     Note that we include DOCUMENT_ONLY_EVENT events here so that we
     can forward all the document stuff to this implementation.
  */
#define EVENT(name_, id_, type_, struct_)                         \
  mozilla::dom::EventHandlerNonNull* GetOn##name_() {             \
    return GetEventHandler(nsGkAtoms::on##name_);                 \
  }                                                               \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler) { \
    SetEventHandler(nsGkAtoms::on##name_, handler);               \
  }
#define TOUCH_EVENT EVENT
#define DOCUMENT_ONLY_EVENT EVENT
#include "mozilla/EventNameList.inc"
#undef DOCUMENT_ONLY_EVENT
#undef TOUCH_EVENT
#undef EVENT

  NodeSelectorFlags GetSelectorFlags() const {
    return static_cast<NodeSelectorFlags>(mSelectorFlags.Get());
  }

  void AriaNotify(const nsAString& aAnnouncement,
                  const mozilla::dom::AriaNotificationOptions& aOptions);

 protected:
  static bool Traverse(nsINode* tmp, nsCycleCollectionTraversalCallback& cb);
  static void Unlink(nsINode* tmp);

  RefPtr<mozilla::dom::NodeInfo> mNodeInfo;

  // mParent is an owning ref most of the time, except for the case of document
  // nodes, so it cannot be represented by nsCOMPtr, so mark is as
  // MOZ_OWNING_REF.
  nsINode* MOZ_OWNING_REF mParent;

 private:
#ifndef BOOL_FLAGS_ON_WRAPPER_CACHE
  // Boolean flags.
  uint32_t mBoolFlags;
#endif

  mozilla::RustCell<uint32_t> mSelectorFlags{0};

  uint32_t mChildCount;

 protected:
  // mNextSibling and mFirstChild are strong references while
  // mPreviousOrLastSibling is a weak ref. |mFirstChild->mPreviousOrLastSibling|
  // points to the last child node.
  nsCOMPtr<nsIContent> mFirstChild;
  nsCOMPtr<nsIContent> mNextSibling;
  nsIContent* MOZ_NON_OWNING_REF mPreviousOrLastSibling;

  // Pointer to the root of our subtree.
  // This reference is non-owning and safe, since it either points to the
  // object itself, or to an ancestor (which keeps us alive).
  nsINode* MOZ_NON_OWNING_REF mSubtreeRoot;

  // Pointer to our primary frame.  Might be null.
  nsIFrame* mPrimaryFrame = nullptr;

 private:
  // The bit marks the listener manager, so that the very hot GetExistingSlots
  // needs no masking.
  static constexpr uintptr_t kListenerManagerBit = 1;

  // The inline listener manager, null if there is none.  Requires no slots.
  mozilla::EventListenerManager* GetInlineListenerManager() const {
    MOZ_ASSERT(!HasSlots());
    return reinterpret_cast<mozilla::EventListenerManager*>(
        mSlotsOrListenerManager & ~kListenerManagerBit);
  }

  // The manager the node itself stores, ignoring Document::mListenerManager.
  mozilla::EventListenerManager* GetNodeListenerManager() const;

  // Disconnects the manager the node stores, if any, and clears
  // NODE_HAS_LISTENERMANAGER.
  void DropNodeListenerManager();

  // Tagged union: with kListenerManagerBit set, a manually refcounted
  // EventListenerManager*, which is null for most nodes; otherwise a non-null
  // nsSlots*, which then owns the manager.  See SetSlots.
  uintptr_t mSlotsOrListenerManager = kListenerManagerBit;

  // See ObserverChainSkipTo.
  static const nsINode* sObserverChainStart;
  static nsINode* sObserverChainSkipTo;
};

NON_VIRTUAL_ADDREF_RELEASE(nsINode)

template <>
struct fmt::formatter<nsINode> : ostream_formatter {};

inline nsINode* mozilla::dom::EventTarget::GetAsNode() {
  return IsNode() ? AsNode() : nullptr;
}

inline const nsINode* mozilla::dom::EventTarget::GetAsNode() const {
  return const_cast<mozilla::dom::EventTarget*>(this)->GetAsNode();
}

inline nsINode* mozilla::dom::EventTarget::AsNode() {
  MOZ_DIAGNOSTIC_ASSERT(IsNode());
  return static_cast<nsINode*>(this);
}

inline const nsINode* mozilla::dom::EventTarget::AsNode() const {
  MOZ_DIAGNOSTIC_ASSERT(IsNode());
  return static_cast<const nsINode*>(this);
}

// Useful inline function for getting a node given an nsIContent and a Document.
// Returns the first argument cast to nsINode if it is non-null, otherwise
// returns the second (which may be null).  We use type variables instead of
// nsIContent* and Document* because the actual types must be
// known for the cast to work.
template <class C, class D>
inline nsINode* NODE_FROM(C& aContent, D& aDocument) {
  if (aContent) return static_cast<nsINode*>(aContent);
  return static_cast<nsINode*>(aDocument);
}

inline nsISupports* ToSupports(nsINode* aPointer) { return aPointer; }

// Some checks are faster to do on nsIContent or Element than on
// nsINode, so spit out FromNode versions taking those types too.
#define NS_IMPL_FROMNODE_GENERIC(_class, _check, _const)                 \
  template <typename T>                                                  \
  static auto FromNode(_const T& aNode)                                  \
      -> decltype(static_cast<_const _class*>(&aNode)) {                 \
    return aNode._check ? static_cast<_const _class*>(&aNode) : nullptr; \
  }                                                                      \
  template <typename T>                                                  \
  static _const _class* FromNode(_const T* aNode) {                      \
    return FromNode(*aNode);                                             \
  }                                                                      \
  template <typename T>                                                  \
  static _const _class* FromNodeOrNull(_const T* aNode) {                \
    return aNode ? FromNode(*aNode) : nullptr;                           \
  }                                                                      \
  template <typename T>                                                  \
  static auto FromEventTarget(_const T& aEventTarget)                    \
      -> decltype(static_cast<_const _class*>(&aEventTarget)) {          \
    return aEventTarget.IsNode() && aEventTarget.AsNode()->_check        \
               ? static_cast<_const _class*>(&aEventTarget)              \
               : nullptr;                                                \
  }                                                                      \
  template <typename T>                                                  \
  static _const _class* FromEventTarget(_const T* aEventTarget) {        \
    return FromEventTarget(*aEventTarget);                               \
  }                                                                      \
  template <typename T>                                                  \
  static _const _class* FromEventTargetOrNull(_const T* aEventTarget) {  \
    return aEventTarget ? FromEventTarget(*aEventTarget) : nullptr;      \
  }

#define NS_IMPL_FROMNODE_HELPER(_class, _check)                                \
  NS_IMPL_FROMNODE_GENERIC(_class, _check, )                                   \
  NS_IMPL_FROMNODE_GENERIC(_class, _check, const)                              \
                                                                               \
  template <typename T>                                                        \
  static _class* FromNode(T&& aNode) {                                         \
    /* We need the double-cast in case aNode is a smartptr.  Those */          \
    /* can cast to superclasses of the type they're templated on, */           \
    /* but not directly to subclasses.  */                                     \
    return aNode->_check ? static_cast<_class*>(static_cast<nsINode*>(aNode))  \
                         : nullptr;                                            \
  }                                                                            \
  template <typename T>                                                        \
  static _class* FromNodeOrNull(T&& aNode) {                                   \
    return aNode ? FromNode(aNode) : nullptr;                                  \
  }                                                                            \
  template <typename T>                                                        \
  static _class* FromEventTarget(T&& aEventTarget) {                           \
    /* We need the double-cast in case aEventTarget is a smartptr.  Those */   \
    /* can cast to superclasses of the type they're templated on, */           \
    /* but not directly to subclasses.  */                                     \
    return aEventTarget->IsNode() && aEventTarget->AsNode()->_check            \
               ? static_cast<_class*>(static_cast<EventTarget*>(aEventTarget)) \
               : nullptr;                                                      \
  }                                                                            \
  template <typename T>                                                        \
  static _class* FromEventTargetOrNull(T&& aEventTarget) {                     \
    return aEventTarget ? FromEventTarget(aEventTarget) : nullptr;             \
  }

#define NS_IMPL_FROMNODE(_class, _nsid) \
  NS_IMPL_FROMNODE_HELPER(_class, IsInNamespace(_nsid))

#define NS_IMPL_FROMNODE_WITH_TAG(_class, _nsid, _tag) \
  NS_IMPL_FROMNODE_HELPER(_class, NodeInfo()->Equals(nsGkAtoms::_tag, _nsid))

#define NS_IMPL_FROMNODE_HTML_WITH_TAG(_class, _tag) \
  NS_IMPL_FROMNODE_WITH_TAG(_class, kNameSpaceID_XHTML, _tag)

#endif /* nsINode_h_ */
