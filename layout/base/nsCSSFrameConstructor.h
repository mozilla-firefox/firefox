/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * construction of a frame tree that is nearly isomorphic to the content
 * tree and updating of that tree in response to dynamic changes
 */

#ifndef nsCSSFrameConstructor_h_
#define nsCSSFrameConstructor_h_

#include "mozilla/ArenaAllocator.h"
#include "mozilla/Attributes.h"
#include "mozilla/ContainStyleScopeManager.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/LinkedList.h"
#include "mozilla/ScrollStyles.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/ChildIterator.h"
#include "nsCOMPtr.h"
#include "nsFrameManager.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIFrame.h"
#include "nsILayoutHistoryState.h"

struct nsStyleDisplay;
struct nsGenConInitializer;

class nsBlockFrame;
class nsContainerFrame;
class nsCanvasFrame;
class nsFirstLetterFrame;
class nsFirstLineFrame;
class nsFrameConstructorState;
class nsPageContentFrame;
class nsPageSequenceFrame;

namespace mozilla {

class ComputedStyle;
class PresShell;
class PrintedSheetFrame;
class RestyleManager;
class ViewportFrame;

namespace dom {

class CharacterData;
class Text;
}  // namespace dom
}  // namespace mozilla

class nsCSSFrameConstructor final : public nsFrameManager {
 public:
  using ComputedStyle = mozilla::ComputedStyle;
  using PseudoStyleType = mozilla::PseudoStyleType;
  using PresShell = mozilla::PresShell;
  using Element = mozilla::dom::Element;
  using Text = mozilla::dom::Text;

  // FIXME(emilio): Is this really needed?
  friend class mozilla::RestyleManager;

  nsCSSFrameConstructor(mozilla::dom::Document* aDocument,
                        PresShell* aPresShell);
  ~nsCSSFrameConstructor() { MOZ_ASSERT(mFCItemsInUse == 0); }

  nsCSSFrameConstructor(const nsCSSFrameConstructor& aCopy) = delete;
  nsCSSFrameConstructor& operator=(const nsCSSFrameConstructor& aCopy) = delete;

  static void GetAlternateTextFor(const Element&, nsAString& aAltText);

  /**
   * Whether insertion should be done synchronously or asynchronously.
   *
   * Generally, insertion is synchronous if we're entering frame construction
   * from restyle processing, and async if we're removing stuff, or need to
   * reconstruct some ancestor.
   *
   * Note that constructing async from frame construction will post a restyle
   * event, but won't need another whole refresh driver tick to go in. Instead
   * change hint processing will keep going as long as there are changes in the
   * queue.
   */
  enum class InsertionKind {
    Sync,
    Async,
  };

  mozilla::RestyleManager* RestyleManager() const;

  mozilla::ViewportFrame* ConstructRootFrame();

 private:
  // aChild is the child being inserted for inserts, and the first
  // child being appended for appends. All the nodes in the range are
  // guaranteed to have the same flat tree parent.
  void ConstructLazily(nsIContent* aStartChild, nsIContent* aEndChild);

#ifdef DEBUG
  void CheckBitsForLazyFrameConstruction(nsIContent* aParent);
#else
  void CheckBitsForLazyFrameConstruction(nsIContent*) {}
#endif

  // Issues a single ContentInserted for each child in the range
  // [aStartChild, aEndChild).
  void IssueSingleInsertNofications(nsIContent* aStartChild,
                                    nsIContent* aEndChild, InsertionKind);

  /**
   * Data that represents an insertion point for some child content.
   */
  struct InsertionPoint {
    InsertionPoint() : mParentFrame(nullptr), mContainer(nullptr) {}

    InsertionPoint(nsContainerFrame* aParentFrame, nsIContent* aContainer)
        : mParentFrame(aParentFrame), mContainer(aContainer) {}

    /**
     * The parent frame to use if the inserted children needs to create
     * frame(s).  May be null, which signals that  we shouldn't try to
     * create frames for the inserted children; either because there are
     * no parent frame or because there are multiple insertion points and
     * we will call IssueSingleInsertNofications for each child instead.
     * mContainer should not be used when mParentFrame is null.
     */
    nsContainerFrame* mParentFrame;
    /**
     * The flattened tree parent for the inserted children.
     * It's undefined if mParentFrame is null.
     */
    nsIContent* mContainer;
  };

  /**
   * Checks if the children in the range [aStartChild, aEndChild) can be
   * inserted/appended to one insertion point together.
   *
   * If so, returns that insertion point. If not, returns with
   * InsertionPoint.mFrame == nullptr and issues single ContentInserted calls
   * for each child.
   *
   * aEndChild = nullptr indicates that we are dealing with an append.
   */
  InsertionPoint GetRangeInsertionPoint(nsIContent* aStartChild,
                                        nsIContent* aEndChild, InsertionKind);

  // Returns true if parent was recreated due to frameset child, false
  // otherwise.
  bool MaybeRecreateForFrameset(nsIFrame* aParentFrame, nsIContent* aStartChild,
                                nsIContent* aEndChild);

  /**
   * For each child in the aStartChild/aEndChild range, calls
   * NoteDirtyDescendantsForServo on their flattened tree parents.  This is
   * used when content is inserted into the document and we decide that
   * we can do lazy frame construction.  It handles children being rebound to
   * different insertion points by calling NoteDirtyDescendantsForServo on each
   * child's flattened tree parent.  Only used when we are styled by Servo.
   */
  void LazilyStyleNewChildRange(nsIContent* aStartChild, nsIContent* aEndChild);

  /**
   * For each child in the aStartChild/aEndChild range, calls StyleNewChildren
   * on their flattened tree parents.  This is used when content is inserted
   * into the document and we decide that we cannot do lazy frame construction.
   * It handles children being rebound to different insertion points by calling
   * StyleNewChildren on each child's flattened tree parent.  Only used when we
   * are styled by Servo.
   */
  void StyleNewChildRange(nsIContent* aStartChild, nsIContent* aEndChild);

 public:
  /**
   * Lazy frame construction is controlled by the InsertionKind parameter of
   * nsCSSFrameConstructor::ContentAppended/Inserted. It is true for all
   * inserts/appends as passed from the presshell, except for the insert of the
   * root element, which is always non-lazy.
   *
   * If we construct lazily, then we add NODE_NEEDS_FRAME bits to the newly
   * inserted/appended nodes and adds NODE_DESCENDANTS_NEED_FRAMES bits to the
   * container and up along the parent chain until it hits the root or another
   * node with that bit set. Then it posts a restyle event to ensure that a
   * flush happens to construct those frames.
   *
   * When the flush happens the RestyleManager walks the dirty nodes during
   * ProcessPostTraversal, and ends up calling Content{Appended,Inserted} with
   * InsertionKind::Sync in ProcessRestyledFrames.
   *
   * If a node is removed from the document then we don't bother unsetting any
   * of the lazy bits that might be set on it, its descendants, or any of its
   * ancestor nodes because that is a slow operation, the work might be wasted
   * if another node gets inserted in its place, and we can clear the bits
   * quicker by processing the content tree from top down the next time we
   * reconstruct frames. (We do clear the bits when BindToTree is called on any
   * nsIContent; so any nodes added to the document will not have any lazy bits
   * set.)
   */

  // If the insertion kind is Async then frame construction of the new children
  // can be done lazily.
  void ContentAppended(nsIContent* aFirstNewContent, InsertionKind);

  // If the insertion kind is Async then frame construction of the new child
  // can be done lazily.
  void ContentInserted(nsIContent* aChild, InsertionKind);

  // Like ContentInserted but handles inserting the children in the range
  // [aStartChild, aEndChild).  aStartChild must be non-null.  aEndChild may be
  // null to indicate the range includes all kids after aStartChild.
  //
  // If aInsertionKind is Async then frame construction of the new children can
  // be done lazily. It is only allowed to be Async when inserting a single
  // node.
  void ContentRangeInserted(nsIContent* aStartChild, nsIContent* aEndChild,
                            InsertionKind aInsertionKind);

  // The kind of removal we're dealing with.
  enum class RemovalKind : uint8_t {
    // The DOM node is getting removed from the document.
    Dom,
    // We're about to remove this frame, but we will insert it later.
    ForReconstruction,
    // We're about to remove this frame due to a style change but we know we
    // are not going to create a frame later.
    ForDisplayNoneChange,
  };

  /**
   * Recreate or destroy frames for aChild.
   *
   * Regardless of the removal kind, this method will in some cases try to
   * reconstruct frames on some ancestor of aChild.
   *
   * The return value indicates whether this "reconstruct an ancestor" action
   * took place.  If true is returned, that means that the frame subtree rooted
   * at some ancestor of aChild's frame was destroyed and will be reconstructed
   * async.
   */
  bool ContentWillBeRemoved(nsIContent* aChild, RemovalKind);

  void CharacterDataChanged(nsIContent* aContent,
                            const CharacterDataChangeInfo& aInfo);

  // If aContent is a text node that has been optimized away due to being
  // whitespace next to a block boundary (or for some other reason), ensure that
  // a frame for it is created the next time frames are flushed, if it can
  // possibly have a frame at all.
  //
  // Returns whether there are chances for the frame to be unsuppressed.
  bool EnsureFrameForTextNodeIsCreatedAfterFlush(
      mozilla::dom::CharacterData* aContent);

  // Should be called when a frame is going to be destroyed and
  // WillDestroyFrameTree hasn't been called yet.
  void NotifyDestroyingFrame(nsIFrame* aFrame);

  void RecalcQuotesAndCounters();

  // Called when any counter style is changed.
  void NotifyCounterStylesAreDirty();

  // Gets called when the presshell is destroying itself and also
  // when we tear down our frame tree to reconstruct it
  void WillDestroyFrameTree();

  /**
   * Destroy the frames for aContent.  Note that this may destroy frames
   * for an ancestor instead.
   *
   * Returns whether a reconstruct was posted for any ancestor.
   */
  bool DestroyFramesFor(nsIContent* aContent);

  // Request to create a continuing frame.  This method never returns null.
  nsIFrame* CreateContinuingFrame(nsIFrame* aFrame,
                                  nsContainerFrame* aParentFrame,
                                  bool aIsFluid = true);

  /**
   * Sets the page name when a page break is being generated due to a change
   * in page name.
   *
   * Should only be used during paginated reflow, to signal what page value
   * the next page content frame should have.
   *
   * It is an error to set this if a new page name has already been set, either
   * through SetNextPageContentFramePageName or
   * MaybeSetNextPageContentFramePageName.
   */
  void SetNextPageContentFramePageName(const nsAtom* aPageName) {
    MOZ_ASSERT(aPageName, "New page name should never be null");
    MOZ_ASSERT(!mNextPageContentFramePageName,
               "PageContentFrame page name was already set");
    mNextPageContentFramePageName = aPageName;
  }

  /**
   * If a new page name has not been set for the next page, sets the value
   * using the given frame.
   *
   * |aFrame| should be a frame to be placed on the new page.
   *
   * This function handles the work of resolving an atom for the frame, and
   * avoids doing this extra work when not necessary.
   *
   * This is used during block reflow when a page break has occurred but it was
   * not caused by a change in page name. It should only be used during
   * paginated reflow.
   */
  void MaybeSetNextPageContentFramePageName(const nsIFrame* aFrame);

  // Copy over fixed frames from aParentFrame's prev-in-flow
  nsresult ReplicateFixedFrames(nsPageContentFrame* aParentFrame);

  /**
   * Get the insertion point for aChild.
   */
  InsertionPoint GetInsertionPoint(nsIContent* aChild);

  /**
   * Return the insertion frame of the primary frame of aContent, or its nearest
   * ancestor that isn't display:contents.
   */
  nsContainerFrame* GetContentInsertionFrameFor(nsIContent* aContent);

  // This returns the outermost frame for the root element.
  nsContainerFrame* GetRootElementFrame() { return mRootElementFrame; }
  // This returns the frame for the root element that does not
  // have a pseudo-element style
  nsIFrame* GetRootElementStyleFrame() { return mRootElementStyleFrame; }
  nsPageSequenceFrame* GetPageSequenceFrame() { return mPageSequenceFrame; }
  // Returns the outermost canvas frame. There's usually one per document, but
  // if but if we're in printing / paginated mode we might have multiple:  one
  // per page plus the background one.
  nsCanvasFrame* GetCanvasFrame() { return mCanvasFrame; }
  // Get the frame that is the parent of the root element's frame.
  nsCanvasFrame* GetDocElementContainingBlock() {
    return mDocElementContainingBlock;
  }

  void AddSizeOfIncludingThis(nsWindowSizes& aSizes) const;

#if defined(ACCESSIBILITY) || defined(MOZ_LAYOUT_DEBUGGER)
  // Exposed only for nsLayoutUtils::GetMarkerSpokenText and
  // nsLayoutDebuggingTools to use.
  mozilla::ContainStyleScopeManager& GetContainStyleScopeManager() {
    return mContainStyleScopeManager;
  }
#endif

 private:
  struct FrameConstructionItem;
  class FrameConstructionItemList;

  mozilla::PrintedSheetFrame* ConstructPrintedSheetFrame(
      PresShell* aPresShell, nsContainerFrame* aParentFrame,
      nsIFrame* aPrevSheetFrame);

  nsContainerFrame* ConstructPageFrame(PresShell* aPresShell,
                                       nsContainerFrame* aParentFrame,
                                       nsIFrame* aPrevPageFrame,
                                       nsCanvasFrame*& aCanvasFrame);

  enum class AllowCounters : bool { No, Yes };

  void InitAndRestoreFrame(const nsFrameConstructorState& aState,
                           nsIContent* aContent, nsContainerFrame* aParentFrame,
                           nsIFrame* aNewFrame,
                           AllowCounters = AllowCounters::Yes);

  already_AddRefed<ComputedStyle> ResolveComputedStyle(nsIContent* aContent);

  enum class ItemFlag : uint8_t {
    // Allow page-break before and after items to be created if the
    // style asks for them.
    AllowPageBreak,
    IsGeneratedContent,
    IsWithinSVGText,
    // The item allows items to be created for SVG <textPath> children.
    AllowTextPathChild,
    // The item is content created by an nsIAnonymousContentCreator frame.
    IsAnonymousContentCreatorContent,
    // The item will be the rendered legend of a <fieldset>.
    IsForRenderedLegend,
    // This will be an outside ::marker.
    IsForOutsideMarker,
  };

  using ItemFlags = mozilla::EnumSet<ItemFlag>;

  // Add the frame construction items for the given aContent and aParentFrame
  // to the list.  This might add more than one item in some rare cases.
  // If aSuppressWhiteSpaceOptimizations is true, optimizations that
  // may suppress the construction of white-space-only text frames
  // must be skipped for these items and items around them.
  void AddFrameConstructionItems(nsFrameConstructorState& aState,
                                 nsIContent* aContent,
                                 bool aSuppressWhiteSpaceOptimizations,
                                 const ComputedStyle& aParentStyle,
                                 const InsertionPoint& aInsertion,
                                 FrameConstructionItemList& aItems,
                                 ItemFlags = {});

  // Helper method for AddFrameConstructionItems etc.
  // Unsets the need-frame/restyle bits on aContent.
  // return true iff we should attempt to create frames for aContent.
  bool ShouldCreateItemsForChild(nsFrameConstructorState& aState,
                                 nsIContent* aContent,
                                 nsContainerFrame* aParentFrame);

  // Construct the frames for the document element.  This can return null if the
  // document element is display:none, or if it's an SVG element that's not
  // <svg>, etc.
  nsIFrame* ConstructDocElementFrame(Element* aDocElement);

  // Set up our mDocElementContainingBlock correctly for the given root
  // content.
  void SetUpDocElementContainingBlock(nsIContent* aDocElement);

  /**
   * CreateAttributeContent creates a single content/frame combination for an
   * |attr(foo)| generated content.
   *
   * @param aParentContent the parent content for the generated content (that
   * is, the originating element).
   * @param aParentFrame the parent frame for the generated frame
   * @param aAttrNamespace the namespace of the attribute in question
   * @param aAttrName the localname of the attribute
   * @param aComputedStyle the style to use
   * @param aGeneratedContent the array of generated content to append the
   *                          created content to.
   * @param [out] aNewContent the content node we create
   * @param [out] aNewFrame the new frame we create
   */
  void CreateAttributeContent(const Element& aParentContent,
                              nsIFrame* aParentFrame, int32_t aAttrNamespace,
                              nsAtom* aAttrName, ComputedStyle* aComputedStyle,
                              nsCOMArray<nsIContent>& aGeneratedContent,
                              nsIContent** aNewContent, nsIFrame** aNewFrame);

  /**
   * Create a text node containing the given string. If aText is non-null
   * then we also set aText to the returned node.
   */
  already_AddRefed<nsIContent> CreateGenConTextNode(
      nsFrameConstructorState& aState, const nsAString& aString,
      mozilla::UniquePtr<nsGenConInitializer> aInitializer);

  /**
   * Create a content node for the given generated content style.
   * The caller takes care of making it SetIsNativeAnonymousRoot, binding it
   * to the document, and creating frames for it.
   * @param aOriginatingElement is the node that has the before/after style.
   * @param aComputedStyle is the 'before' or 'after' pseudo-element style.
   * @param aContentIndex is the index of the content item to create.
   * @param aAddChild callback to be called for each generated content child.
   */
  void CreateGeneratedContent(
      nsFrameConstructorState& aState, Element& aOriginatingElement,
      ComputedStyle& aPseudoStyle, const mozilla::StyleContentItem& aItem,
      size_t aContentIndex,
      const mozilla::FunctionRef<void(nsIContent*)> aAddChild);

  /**
   * Create child content nodes for a ::marker from its 'list-style-*' values.
   */
  void CreateGeneratedContentFromListStyle(
      nsFrameConstructorState& aState, Element& aOriginatingElement,
      const ComputedStyle& aPseudoStyle,
      const mozilla::FunctionRef<void(nsIContent*)> aAddChild);
  /**
   * Create child content nodes for a ::marker from its 'list-style-type'.
   */
  void CreateGeneratedContentFromListStyleType(
      nsFrameConstructorState& aState, Element& aOriginatingElement,
      const ComputedStyle& aPseudoStyle,
      const mozilla::FunctionRef<void(nsIContent*)> aAddChild);

  // aParentFrame may be null; this method doesn't use it directly in any case.
  void CreateGeneratedContentItem(nsFrameConstructorState& aState,
                                  nsContainerFrame* aParentFrame,
                                  Element& aOriginatingElement, ComputedStyle&,
                                  PseudoStyleType aPseudoElement,
                                  FrameConstructionItemList& aItems,
                                  ItemFlags aExtraFlags = {});

  // This method is called by ContentAppended() and ContentRangeInserted() when
  // appending flowed frames to a parent's principal child list. It handles the
  // case where the parent is the trailing inline of an ib-split or is the last
  // continuation of a ::-moz-column-content in an nsColumnSetFrame.
  //
  // This method can change aFrameList: it can chop off the beginning and put it
  // in aParentFrame while either putting the remainder into an ib-split sibling
  // of aParentFrame or creating aParentFrame's column-span siblings for the
  // remainder.
  //
  // aPrevSibling must be the frame after which aFrameList is to be placed on
  // aParentFrame's principal child list. It may be null if aFrameList is being
  // added at the beginning of the child list.
  void AppendFramesToParent(nsFrameConstructorState& aState,
                            nsContainerFrame* aParentFrame,
                            nsFrameList& aFrameList, nsIFrame* aPrevSibling,
                            bool aIsRecursiveCall = false);

  // BEGIN TABLE SECTION
  /**
   * Construct a table wrapper frame. This is the FrameConstructionData
   * callback used for the job.
   */
  nsIFrame* ConstructTable(nsFrameConstructorState& aState,
                           FrameConstructionItem& aItem,
                           nsContainerFrame* aParentFrame,
                           const nsStyleDisplay* aDisplay,
                           nsFrameList& aFrameList);

  /**
   * FrameConstructionData callback for constructing table rows and row groups.
   */
  nsIFrame* ConstructTableRowOrRowGroup(nsFrameConstructorState& aState,
                                        FrameConstructionItem& aItem,
                                        nsContainerFrame* aParentFrame,
                                        const nsStyleDisplay* aStyleDisplay,
                                        nsFrameList& aFrameList);

  /**
   * FrameConstructionData callback used for constructing table columns.
   */
  nsIFrame* ConstructTableCol(nsFrameConstructorState& aState,
                              FrameConstructionItem& aItem,
                              nsContainerFrame* aParentFrame,
                              const nsStyleDisplay* aStyleDisplay,
                              nsFrameList& aFrameList);

  /**
   * FrameConstructionData callback used for constructing table cells.
   */
  nsIFrame* ConstructTableCell(nsFrameConstructorState& aState,
                               FrameConstructionItem& aItem,
                               nsContainerFrame* aParentFrame,
                               const nsStyleDisplay* aStyleDisplay,
                               nsFrameList& aFrameList);

 private:
  /* An enum of possible parent types for anonymous table or ruby object
     construction */
  enum ParentType {
    eTypeBlock = 0, /* This includes all non-table-related frames */
    eTypeRow,
    eTypeRowGroup,
    eTypeColGroup,
    eTypeTable,
    eTypeRuby,
    eTypeRubyBase,
    eTypeRubyBaseContainer,
    eTypeRubyText,
    eTypeRubyTextContainer,
    eParentTypeCount
  };

  /* 4 bits is enough to handle our ParentType values */
#define FCDATA_PARENT_TYPE_OFFSET 28
  /* Macro to get the desired parent type out of an mBits member of
     FrameConstructionData */
#define FCDATA_DESIRED_PARENT_TYPE(_bits) \
  ParentType((_bits) >> FCDATA_PARENT_TYPE_OFFSET)
  /* Macro to create FrameConstructionData bits out of a desired parent type */
#define FCDATA_DESIRED_PARENT_TYPE_TO_BITS(_type) \
  (((uint32_t)(_type)) << FCDATA_PARENT_TYPE_OFFSET)

  /* Get the parent type that aParentFrame has. */
  static ParentType GetParentType(nsIFrame* aParentFrame) {
    return GetParentType(aParentFrame->Type());
  }

  /* Get the parent type for the given LayoutFrameType */
  static ParentType GetParentType(mozilla::LayoutFrameType aFrameType);

  static bool IsRubyParentType(ParentType aParentType) {
    return (aParentType == eTypeRuby || aParentType == eTypeRubyBase ||
            aParentType == eTypeRubyBaseContainer ||
            aParentType == eTypeRubyText ||
            aParentType == eTypeRubyTextContainer);
  }

  static bool IsTableParentType(ParentType aParentType) {
    return (aParentType == eTypeTable || aParentType == eTypeRow ||
            aParentType == eTypeRowGroup || aParentType == eTypeColGroup);
  }

  /* A constructor function that just creates an nsIFrame object.  The caller
     is responsible for initializing the object, adding it to frame lists,
     constructing frames for the children, etc.

     @param PresShell the presshell whose arena should be used to allocate
                      the frame.
     @param ComputedStyle the style to use for the frame. */
  using FrameCreationFunc = nsIFrame* (*)(PresShell*, ComputedStyle*);
  using ContainerFrameCreationFunc = nsContainerFrame* (*)(PresShell*,
                                                           ComputedStyle*);
  using BlockFrameCreationFunc = nsBlockFrame* (*)(PresShell*, ComputedStyle*);

  /* A function that can be used to get a FrameConstructionData.  Such
     a function is allowed to return null.

     @param nsIContent the node for which the frame is being constructed.
     @param ComputedStyle the style to be used for the frame.
  */
  struct FrameConstructionData;
  using FrameConstructionDataGetter =
      const FrameConstructionData* (*)(const Element&, ComputedStyle&);

  /* A constructor function that's used for complicated construction tasks.
     This is expected to create the new frame, initialize it, add whatever
     needs to be added to aFrameList (XXXbz is that really necessary?  Could
     caller add?  Might there be cases when the returned frame or its
     placeholder is not the thing that ends up in aFrameList?  If not, would
     it be safe to do the add into the frame construction state after
     processing kids?  Look into this as a followup!), process children as
     needed, etc.  It is NOT expected to deal with setting the frame on the
     content.

     @param aState the frame construction state to use.
     @param aItem the frame construction item to use
     @param aParentFrame the frame to set as the parent of the
                         newly-constructed frame.
     @param aStyleDisplay the display struct from aItem's mComputedStyle
     @param aFrameList the frame list to add the new frame (or its
                        placeholder) to.
     @return the frame that was constructed.  This frame is what the caller
             will set as the frame on the content.  Guaranteed non-null.
  */
  using FrameFullConstructor =
      nsIFrame* (nsCSSFrameConstructor::*)(nsFrameConstructorState & aState,
                                           FrameConstructionItem& aItem,
                                           nsContainerFrame* aParentFrame,
                                           const nsStyleDisplay* aStyleDisplay,
                                           nsFrameList& aFrameList);

  /* Bits that modify the way a FrameConstructionData is handled */

  /* If the FCDATA_SKIP_FRAMESET bit is set, then the frame created should not
     be set as the primary frame on the content node.  This should only be used
     in very rare cases when we create more than one frame for a given content
     node. */
#define FCDATA_SKIP_FRAMESET 0x1
  /* If the FCDATA_FUNC_IS_DATA_GETTER bit is set, then the mFunc of the
     FrameConstructionData is a getter function that can be used to get the
     actual FrameConstructionData to use. */
#define FCDATA_FUNC_IS_DATA_GETTER 0x2
  /* If the FCDATA_FUNC_IS_FULL_CTOR bit is set, then the FrameConstructionData
     has an mFullConstructor.  In this case, there is no relevant mData or
     mFunc */
#define FCDATA_FUNC_IS_FULL_CTOR 0x4
  /* If FCDATA_DISALLOW_OUT_OF_FLOW is set, do not allow the frame to
     float or be absolutely positioned.  This can also be used with
     FCDATA_FUNC_IS_FULL_CTOR to indicate what the full-constructor
     function will do. */
#define FCDATA_DISALLOW_OUT_OF_FLOW 0x8
  /* If FCDATA_FORCE_NULL_ABSPOS_CONTAINER is set, make sure to push a
     null absolute containing block before processing children for this
     frame.  If this is not set, the frame will be pushed as the
     absolute containing block as needed, based on its style */
#define FCDATA_FORCE_NULL_ABSPOS_CONTAINER 0x10
  /* If FCDATA_WRAP_KIDS_IN_BLOCKS is set, the inline kids of the frame
     will be wrapped in blocks.  This is only usable for MathML at the
     moment. */
#define FCDATA_WRAP_KIDS_IN_BLOCKS 0x20
  /* If FCDATA_SUPPRESS_FRAME is set, no frame should be created for the
     content.  If this bit is set, nothing else in the struct needs to be
     set. */
#define FCDATA_SUPPRESS_FRAME 0x40
  /* If FCDATA_MAY_NEED_SCROLLFRAME is set, the new frame should be wrapped in
     a scrollframe if its overflow type so requires. */
#define FCDATA_MAY_NEED_SCROLLFRAME 0x80
  /* If FCDATA_SKIP_ABSPOS_PUSH is set, don't push this frame as an
     absolute containing block, no matter what its style says. */
#define FCDATA_SKIP_ABSPOS_PUSH 0x200
  /* If FCDATA_DISALLOW_GENERATED_CONTENT is set, then don't allow generated
     content when processing kids of this frame.  This should not be used with
     FCDATA_FUNC_IS_FULL_CTOR */
#define FCDATA_DISALLOW_GENERATED_CONTENT 0x400
  /* If FCDATA_IS_TABLE_PART is set, then the frame is some sort of
     table-related thing and we should not attempt to fetch a table-cell parent
     for it if it's inside another table-related frame. */
#define FCDATA_IS_TABLE_PART 0x800
  /* If FCDATA_IS_INLINE is set, then the frame is a non-replaced CSS
     inline box. */
#define FCDATA_IS_INLINE 0x1000
  /* If FCDATA_IS_LINE_PARTICIPANT is set, the frame is something that will
     return true for IsLineParticipant() */
#define FCDATA_IS_LINE_PARTICIPANT 0x2000
  /* If FCDATA_IS_LINE_BREAK is set, the frame is something that will
     induce a line break boundary before and after itself. */
#define FCDATA_IS_LINE_BREAK 0x4000
  /* If FCDATA_ALLOW_BLOCK_STYLES is set, allow block styles when processing
     children of a block (i.e. allow ::first-letter/line).
     This should not be used with FCDATA_FUNC_IS_FULL_CTOR. */
#define FCDATA_ALLOW_BLOCK_STYLES 0x8000
  /* If FCDATA_USE_CHILD_ITEMS is set, then use the mChildItems in the relevant
     FrameConstructionItem instead of trying to process the content's children.
     This can be used with or without FCDATA_FUNC_IS_FULL_CTOR.
     The child items might still need table pseudo processing. */
#define FCDATA_USE_CHILD_ITEMS 0x10000
  // 0x200000 is free
  /* If FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS is set, then create a
     block formatting context wrapper around the kids of this frame
     using the FrameConstructionData's mPseudoAtom for its anonymous
     box type. */
#define FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS 0x40000
  /* If FCDATA_IS_SVG_TEXT is set, then this text frame is a descendant of
     an SVG text frame. */
#define FCDATA_IS_SVG_TEXT 0x80000
  /**
   * Whether the kids of this FrameConstructionData should be flagged as having
   * a wrapper anon box parent.  This should only be set if
   * FCDATA_USE_CHILD_ITEMS is set.
   */
#define FCDATA_IS_WRAPPER_ANON_BOX 0x400000

  /* Structure representing information about how a frame should be
     constructed.  */
  struct FrameConstructionData {
    // We have exactly one of three types of functions, so use a union for
    // better cache locality.
    union Func {
      FrameCreationFunc mCreationFunc;
      FrameConstructionDataGetter mDataGetter;
      FrameFullConstructor mFullConstructor;

      explicit constexpr Func(FrameCreationFunc aFunc) : mCreationFunc(aFunc) {}
      explicit constexpr Func(FrameConstructionDataGetter aDataGetter)
          : mDataGetter(aDataGetter) {}
      explicit constexpr Func(FrameFullConstructor aCtor)
          : mFullConstructor(aCtor) {}
    } mFunc;
    // Flag bits that can modify the way the construction happens
    const uint32_t mBits = 0;
    // For cases when FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS is set, the
    // anonymous box type to use for that wrapper.
    PseudoStyleType const mAnonBoxPseudo = PseudoStyleType::NotPseudo;

    constexpr FrameConstructionData() : FrameConstructionData(nullptr) {}

    MOZ_IMPLICIT constexpr FrameConstructionData(std::nullptr_t,
                                                 uint32_t aBits = 0)
        : mFunc(static_cast<FrameCreationFunc>(nullptr)), mBits(aBits) {}

    MOZ_IMPLICIT constexpr FrameConstructionData(
        FrameCreationFunc aCreationFunc, uint32_t aBits = 0)
        : mFunc(aCreationFunc), mBits(aBits) {}
    constexpr FrameConstructionData(FrameCreationFunc aCreationFunc,
                                    uint32_t aBits,
                                    PseudoStyleType aAnonBoxPseudo)
        : mFunc(aCreationFunc),
          mBits(aBits | FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS),
          mAnonBoxPseudo(aAnonBoxPseudo) {}
    MOZ_IMPLICIT constexpr FrameConstructionData(
        FrameConstructionDataGetter aDataGetter, uint32_t aBits = 0)
        : mFunc(aDataGetter),
          mBits(aBits | FCDATA_FUNC_IS_DATA_GETTER),
          mAnonBoxPseudo(PseudoStyleType::NotPseudo) {}
    MOZ_IMPLICIT constexpr FrameConstructionData(FrameFullConstructor aCtor,
                                                 uint32_t aBits = 0)
        : mFunc(aCtor),
          mBits(aBits | FCDATA_FUNC_IS_FULL_CTOR),
          mAnonBoxPseudo(PseudoStyleType::NotPseudo) {}
  };

  /* Structure representing a mapping of an atom to a FrameConstructionData.
     This can be used with non-static atoms, assuming that the nsAtom* is
     stored somewhere that this struct can point to (that is, a static
     nsAtom*) and that it's allocated before the struct is ever used. */
  struct FrameConstructionDataByTag {
    const nsStaticAtom* const mTag;
    const FrameConstructionData mData;
  };

  /* Structure representing a mapping of an integer to a
     FrameConstructionData. There are no magic integer values here. */
  struct FrameConstructionDataByInt {
    /* Could be used for display or whatever else */
    const int32_t mInt;
    const FrameConstructionData mData;
  };

  struct FrameConstructionDataByDisplay {
#ifdef DEBUG
    const mozilla::StyleDisplay mDisplay;
#endif
    const FrameConstructionData mData;
  };

  /* Structure that has a FrameConstructionData and style pseudo-type
     for a table pseudo-frame */
  struct PseudoParentData {
    const FrameConstructionData mFCData;
    mozilla::PseudoStyleType const mPseudoType;
  };
  /* Array of such structures that we use to properly construct table
     pseudo-frames as needed */
  static const PseudoParentData sPseudoParentData[eParentTypeCount];

  const FrameConstructionData* FindDataForContent(nsIContent&, ComputedStyle&,
                                                  nsIFrame* aParentFrame,
                                                  ItemFlags aFlags);

  // aParentFrame might be null.  If it is, that means it was an inline frame.
  static const FrameConstructionData* FindTextData(const Text&,
                                                   nsIFrame* aParentFrame);
  const FrameConstructionData* FindElementData(const Element&, ComputedStyle&,
                                               nsIFrame* aParentFrame,
                                               ItemFlags aFlags);
  const FrameConstructionData* FindElementTagData(const Element&,
                                                  ComputedStyle&,
                                                  nsIFrame* aParentFrame,
                                                  ItemFlags aFlags);

  /* A function that takes an integer, content, style, and array of
     FrameConstructionDataByInts and finds the appropriate frame construction
     data to use and returns it.  This can return null if none of the integers
     match or if the matching integer has a FrameConstructionDataGetter that
     returns null. */
  static const FrameConstructionData* FindDataByInt(
      int32_t aInt, const Element&, ComputedStyle&,
      const FrameConstructionDataByInt* aDataPtr, uint32_t aDataLength);

  /**
   * A function that takes a tag, content, style, and array of
   * FrameConstructionDataByTags and finds the appropriate frame construction
   * data to use and returns it.
   *
   * This can return null if none of the tags match or if the matching tag has a
   * FrameConstructionDataGetter that returns null. In the case that the tags
   * actually match, aTagFound will be true, even if the return value is null.
   */
  static const FrameConstructionData* FindDataByTag(
      const Element& aElement, ComputedStyle& aComputedStyle,
      const FrameConstructionDataByTag* aDataPtr, uint32_t aDataLength);

  /* A class representing a list of FrameConstructionItems.  Instances of this
     class are only created as AutoFrameConstructionItemList, or as a member
     of FrameConstructionItem. */
  class FrameConstructionItemList {
   public:
    void Reset(nsCSSFrameConstructor* aFCtor) {
      Destroy(aFCtor);
      this->~FrameConstructionItemList();
      new (this) FrameConstructionItemList();
    }

    void SetLineBoundaryAtStart(bool aBoundary) {
      mLineBoundaryAtStart = aBoundary;
    }
    void SetLineBoundaryAtEnd(bool aBoundary) {
      mLineBoundaryAtEnd = aBoundary;
    }
    void SetParentHasNoShadowDOM(bool aValue) {
      mParentHasNoShadowDOM = aValue;
    }
    bool HasLineBoundaryAtStart() { return mLineBoundaryAtStart; }
    bool HasLineBoundaryAtEnd() { return mLineBoundaryAtEnd; }
    bool ParentHasNoShadowDOM() { return mParentHasNoShadowDOM; }
    bool IsEmpty() const { return mItems.isEmpty(); }
    bool AreAllItemsInline() const { return mInlineCount == mItemCount; }
    bool AreAllItemsBlock() const { return mBlockCount == mItemCount; }
    bool AllWantParentType(ParentType aDesiredParentType) const {
      return mDesiredParentCounts[aDesiredParentType] == mItemCount;
    }

    // aSuppressWhiteSpaceOptimizations is true if optimizations that
    // skip constructing whitespace frames for this item or items
    // around it cannot be performed.
    // Also, the return value is always non-null, thanks to infallible 'new'.
    FrameConstructionItem* AppendItem(
        nsCSSFrameConstructor* aFCtor, const FrameConstructionData* aFCData,
        nsIContent* aContent, already_AddRefed<ComputedStyle> aComputedStyle,
        bool aSuppressWhiteSpaceOptimizations) {
      FrameConstructionItem* item = new (aFCtor)
          FrameConstructionItem(aFCData, aContent, std::move(aComputedStyle),
                                aSuppressWhiteSpaceOptimizations);
      mItems.insertBack(item);
      ++mItemCount;
      ++mDesiredParentCounts[item->DesiredParentType()];
      return item;
    }

    // Arguments are the same as AppendItem().
    FrameConstructionItem* PrependItem(
        nsCSSFrameConstructor* aFCtor, const FrameConstructionData* aFCData,
        nsIContent* aContent, already_AddRefed<ComputedStyle> aComputedStyle,
        bool aSuppressWhiteSpaceOptimizations) {
      FrameConstructionItem* item = new (aFCtor)
          FrameConstructionItem(aFCData, aContent, std::move(aComputedStyle),
                                aSuppressWhiteSpaceOptimizations);
      mItems.insertFront(item);
      ++mItemCount;
      ++mDesiredParentCounts[item->DesiredParentType()];
      return item;
    }

    void InlineItemAdded() { ++mInlineCount; }
    void BlockItemAdded() { ++mBlockCount; }

    // Not allocated from the heap!
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
#ifdef _MSC_VER /* Visual Studio */
   private:
    void operator delete(void*) { MOZ_CRASH("FrameConstructionItemList::del"); }

   public:
#else
    void operator delete(void*) = delete;
#endif
    void operator delete[](void*) = delete;

    class Iterator {
     public:
      explicit Iterator(FrameConstructionItemList& aList)
          : mCurrent(aList.mItems.getFirst()), mList(aList) {}
      Iterator(const Iterator& aOther) = default;

      bool operator==(const Iterator& aOther) const {
        MOZ_ASSERT(&mList == &aOther.mList, "Iterators for different lists?");
        return mCurrent == aOther.mCurrent;
      }

      Iterator& operator=(const Iterator& aOther) {
        MOZ_ASSERT(&mList == &aOther.mList, "Iterators for different lists?");
        mCurrent = aOther.mCurrent;
        return *this;
      }

      FrameConstructionItemList* List() { return &mList; }

      FrameConstructionItem& item() {
        MOZ_ASSERT(!IsDone(), "Should have checked IsDone()!");
        return *mCurrent;
      }

      const FrameConstructionItem& item() const {
        MOZ_ASSERT(!IsDone(), "Should have checked IsDone()!");
        return *mCurrent;
      }

      bool IsDone() const { return mCurrent == nullptr; }
      bool AtStart() const { return mCurrent == mList.mItems.getFirst(); }
      void Next() {
        NS_ASSERTION(!IsDone(), "Should have checked IsDone()!");
        mCurrent = mCurrent->getNext();
      }
      void Prev() {
        NS_ASSERTION(!AtStart(), "Should have checked AtStart()!");
        mCurrent = mCurrent ? mCurrent->getPrevious() : mList.mItems.getLast();
      }
      void SetToEnd() { mCurrent = nullptr; }

      // Skip over all items that want the given parent type. Return whether
      // the iterator is done after doing that.  The iterator must not be done
      // when this is called.
      inline bool SkipItemsWantingParentType(ParentType aParentType);

      // Skip over all items that want a parent type different from the given
      // one.  Return whether the iterator is done after doing that.  The
      // iterator must not be done when this is called.
      inline bool SkipItemsNotWantingParentType(ParentType aParentType);

      // Skip over non-replaced inline frames and positioned frames.
      // Return whether the iterator is done after doing that.
      // The iterator must not be done when this is called.
      inline bool SkipItemsThatNeedAnonFlexOrGridItem(
          const nsFrameConstructorState& aState, bool aIsWebkitBox);

      // Skip to the first frame that is a non-replaced inline or is
      // positioned.  Return whether the iterator is done after doing that.
      // The iterator must not be done when this is called.
      inline bool SkipItemsThatDontNeedAnonFlexOrGridItem(
          const nsFrameConstructorState& aState, bool aIsWebkitBox);

      // Skip over all items that do not want a ruby parent.  Return whether
      // the iterator is done after doing that.  The iterator must not be done
      // when this is called.
      inline bool SkipItemsNotWantingRubyParent();

      // Skip over whitespace.  Return whether the iterator is done after doing
      // that.  The iterator must not be done, and must be pointing to a
      // whitespace item when this is called.
      inline bool SkipWhitespace(nsFrameConstructorState& aState);

      // Remove the item pointed to by this iterator from its current list and
      // Append it to aTargetList.  This iterator is advanced to point to the
      // next item in its list.  aIter must not be done.  aTargetList must not
      // be the list this iterator is iterating over..
      void AppendItemToList(FrameConstructionItemList& aTargetList);

      // As above, but moves all items starting with this iterator until we
      // get to aEnd; the item pointed to by aEnd is not stolen.  This method
      // might have optimizations over just looping and doing StealItem for
      // some special cases.  After this method returns, this iterator will
      // point to the item aEnd points to now; aEnd is not modified.
      // aTargetList must not be the list this iterator is iterating over.
      void AppendItemsToList(nsCSSFrameConstructor* aFCtor,
                             const Iterator& aEnd,
                             FrameConstructionItemList& aTargetList);

      // Insert aItem in this iterator's list right before the item pointed to
      // by this iterator.  After the insertion, this iterator will continue to
      // point to the item it now points to (the one just after the
      // newly-inserted item).  This iterator is allowed to be done; in that
      // case this call just appends the given item to the list.
      void InsertItem(FrameConstructionItem* aItem);

      // Delete the items between this iterator and aEnd, including the item
      // this iterator currently points to but not including the item pointed
      // to by aEnd.  When this returns, this iterator will point to the same
      // item as aEnd.  This iterator must not equal aEnd when this method is
      // called.
      void DeleteItemsTo(nsCSSFrameConstructor* aFCtor, const Iterator& aEnd);

     private:
      FrameConstructionItem* mCurrent;
      FrameConstructionItemList& mList;
    };

   protected:
    FrameConstructionItemList()
        : mInlineCount(0),
          mBlockCount(0),
          mItemCount(0),
          mLineBoundaryAtStart(false),
          mLineBoundaryAtEnd(false),
          mParentHasNoShadowDOM(false) {
      MOZ_COUNT_CTOR(FrameConstructionItemList);
      memset(mDesiredParentCounts, 0, sizeof(mDesiredParentCounts));
    }

    void Destroy(nsCSSFrameConstructor* aFCtor) {
      while (FrameConstructionItem* item = mItems.popFirst()) {
        item->Delete(aFCtor);
      }
    }

    // Prevent stack instances (except as AutoFrameConstructionItemList).
    friend struct FrameConstructionItem;
    ~FrameConstructionItemList() {
      MOZ_COUNT_DTOR(FrameConstructionItemList);
      MOZ_ASSERT(mItems.isEmpty(), "leaking");
    }

   private:
    // Placement new is used by Reset().
    void* operator new(size_t, void* aPtr) { return aPtr; }

    struct UndisplayedItem {
      UndisplayedItem(nsIContent* aContent, ComputedStyle* aComputedStyle)
          : mContent(aContent), mComputedStyle(aComputedStyle) {}

      nsIContent* const mContent;
      RefPtr<ComputedStyle> mComputedStyle;
    };

    // Adjust our various counts for aItem being added or removed.  aDelta
    // should be either +1 or -1 depending on which is happening.
    void AdjustCountsForItem(FrameConstructionItem* aItem, int32_t aDelta);

    mozilla::LinkedList<FrameConstructionItem> mItems;
    uint32_t mInlineCount;
    uint32_t mBlockCount;
    uint32_t mItemCount;
    uint32_t mDesiredParentCounts[eParentTypeCount];
    // True if there is guaranteed to be a line boundary before the
    // frames created by these items
    bool mLineBoundaryAtStart;
    // True if there is guaranteed to be a line boundary after the
    // frames created by these items
    bool mLineBoundaryAtEnd;
    // True if the parent is guaranteed to have no shadow tree.
    bool mParentHasNoShadowDOM;
  };

  /* A struct representing a list of FrameConstructionItems on the stack. */
  struct MOZ_RAII AutoFrameConstructionItemList final
      : public FrameConstructionItemList {
    template <typename... Args>
    explicit AutoFrameConstructionItemList(nsCSSFrameConstructor* aFCtor,
                                           Args&&... args)
        : FrameConstructionItemList(std::forward<Args>(args)...),
          mFCtor(aFCtor) {
      MOZ_ASSERT(mFCtor);
    }
    ~AutoFrameConstructionItemList() { Destroy(mFCtor); }

   private:
    nsCSSFrameConstructor* const mFCtor;
  };

  typedef FrameConstructionItemList::Iterator FCItemIterator;

  /* A struct representing an item for which frames might need to be
   * constructed.  This contains all the information needed to construct the
   * frame other than the parent frame and whatever would be stored in the
   * frame constructor state.  You probably want to use
   * AutoFrameConstructionItem instead of this struct. */
  struct FrameConstructionItem final
      : public mozilla::LinkedListElement<FrameConstructionItem> {
    FrameConstructionItem(const FrameConstructionData* aFCData,
                          nsIContent* aContent,
                          already_AddRefed<ComputedStyle> aComputedStyle,
                          bool aSuppressWhiteSpaceOptimizations)
        : mFCData(aFCData),
          mContent(aContent),
          mComputedStyle(std::move(aComputedStyle)),
          mSuppressWhiteSpaceOptimizations(aSuppressWhiteSpaceOptimizations),
          mIsText(false),
          mIsGeneratedContent(false),
          mIsAllInline(false),
          mIsBlock(false),
          mIsRenderedLegend(false) {
      MOZ_COUNT_CTOR(FrameConstructionItem);
    }

    void* operator new(size_t, nsCSSFrameConstructor* aFCtor) {
      return aFCtor->AllocateFCItem();
    }

    // Not allocated from the general heap - instead, use the new/Delete APIs
    // that take a nsCSSFrameConstructor* (which manages our arena allocation).
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
#ifdef _MSC_VER /* Visual Studio */
   private:
    void operator delete(void*) { MOZ_CRASH("FrameConstructionItem::delete"); }

   public:
#else
    void operator delete(void*) = delete;
#endif
    void operator delete[](void*) = delete;
    FrameConstructionItem(const FrameConstructionItem& aOther) = delete;

    void Delete(nsCSSFrameConstructor* aFCtor) {
      mChildItems.Destroy(aFCtor);
      if (mIsGeneratedContent) {
        mContent->UnbindFromTree();
        NS_RELEASE(mContent);
      }
      this->~FrameConstructionItem();
      aFCtor->FreeFCItem(this);
    }

    ParentType DesiredParentType() {
      return FCDATA_DESIRED_PARENT_TYPE(mFCData->mBits);
    }

    // Indicates whether (when in a flex or grid container) this item needs
    // to be wrapped in an anonymous block.  (Note that we implement
    // -webkit-box/-webkit-inline-box using our standard flexbox frame class,
    // but we use different rules for what gets wrapped. The aIsWebkitBox
    // parameter here tells us whether to use those different rules.)
    bool NeedsAnonFlexOrGridItem(const nsFrameConstructorState& aState,
                                 bool aIsWebkitBox);

    // Don't call this unless the frametree really depends on the answer!
    // Especially so for generated content, where we don't want to reframe
    // things.
    bool IsWhitespace(nsFrameConstructorState& aState) const;

    bool IsLineBoundary() const {
      return mIsBlock || (mFCData->mBits & FCDATA_IS_LINE_BREAK);
    }

    // Child frame construction items.
    FrameConstructionItemList mChildItems;

    // The FrameConstructionData to use.
    const FrameConstructionData* mFCData;
    // The nsIContent node to use when initializing the new frame.
    nsIContent* mContent;
    // The style to use for creating the new frame.
    RefPtr<ComputedStyle> mComputedStyle;
    // Whether optimizations to skip constructing textframes around
    // this content need to be suppressed.
    bool mSuppressWhiteSpaceOptimizations : 1;
    // Whether this is a text content item.
    bool mIsText : 1;
    // Whether this is a generated content container.
    // If it is, mContent is a strong pointer.
    bool mIsGeneratedContent : 1;
    // Whether construction from this item will create only frames that are
    // IsInlineOutside() in the principal child list.  This is not precise, but
    // conservative: if true the frames will really be inline, whereas if false
    // they might still all be inline.
    bool mIsAllInline : 1;
    // Whether construction from this item will create only frames that are
    // IsBlockOutside() in the principal child list.  This is not precise, but
    // conservative: if true the frames will really be blocks, whereas if false
    // they might still be blocks (and in particular, out-of-flows that didn't
    // find a containing block).
    bool mIsBlock : 1;
    // Whether this item is the rendered legend of a <fieldset>
    bool mIsRenderedLegend : 1;

   private:
    // Not allocated from the stack!
    ~FrameConstructionItem() {
      MOZ_COUNT_DTOR(FrameConstructionItem);
      MOZ_ASSERT(mChildItems.IsEmpty(), "leaking");
    }
  };

  /**
   * Convenience struct to assist in managing a temporary FrameConstructionItem
   * using a local variable. Castable to FrameConstructionItem so that it can
   * be passed transparently to functions that expect that type.
   * (This struct exists because FrameConstructionItem is arena-allocated, and
   * it's nice to abstract away its allocation/deallocation.)
   */
  struct MOZ_RAII AutoFrameConstructionItem final {
    template <typename... Args>
    explicit AutoFrameConstructionItem(nsCSSFrameConstructor* aFCtor,
                                       Args&&... args)
        : mFCtor(aFCtor),
          mItem(new (aFCtor)
                    FrameConstructionItem(std::forward<Args>(args)...)) {
      MOZ_ASSERT(mFCtor);
    }
    ~AutoFrameConstructionItem() { mItem->Delete(mFCtor); }
    operator FrameConstructionItem&() { return *mItem; }

   private:
    nsCSSFrameConstructor* const mFCtor;
    FrameConstructionItem* const mItem;
  };

  /**
   * Updates the nsFrameConstructorState auto page-name value, and restores the
   * previous value on destruction.
   * See https://drafts.csswg.org/css-page-3/#using-named-pages
   *
   * To track this, this will automatically add PageValuesProperty to
   * the frame.
   *
   * Note that this does not add PageValuesProperty to the frame when not in a
   * paginated context.
   */
  class MOZ_RAII AutoFrameConstructionPageName final {
    nsFrameConstructorState& mState;
    const nsAtom* mNameToRestore;

   public:
    AutoFrameConstructionPageName(const AutoFrameConstructionPageName&) =
        delete;
    AutoFrameConstructionPageName(AutoFrameConstructionPageName&&) = delete;
    AutoFrameConstructionPageName(nsFrameConstructorState& aState,
                                  nsIFrame* const aFrame);
    ~AutoFrameConstructionPageName();
  };

  /**
   * Function to create the anonymous flex or grid items that we need.
   * If aParentFrame is not a nsFlexContainerFrame or nsGridContainerFrame then
   * this method is a NOP.
   * @param aItems the child frame construction items before pseudo creation
   * @param aParentFrame the parent frame
   */
  void CreateNeededAnonFlexOrGridItems(nsFrameConstructorState& aState,
                                       FrameConstructionItemList& aItems,
                                       nsIFrame* aParentFrame);

  enum RubyWhitespaceType {
    eRubyNotWhitespace,
    eRubyInterLevelWhitespace,
    // Includes inter-base and inter-annotation whitespace
    eRubyInterLeafWhitespace,
    eRubyInterSegmentWhitespace
  };

  /**
   * Function to compute the whitespace type according to the display
   * values of the previous and the next elements.
   */
  static inline RubyWhitespaceType ComputeRubyWhitespaceType(
      mozilla::StyleDisplay aPrevDisplay, mozilla::StyleDisplay aNextDisplay);

  /**
   * Function to interpret the type of whitespace between
   * |aStartIter| and |aEndIter|.
   */
  static inline RubyWhitespaceType InterpretRubyWhitespace(
      nsFrameConstructorState& aState, const FCItemIterator& aStartIter,
      const FCItemIterator& aEndIter);

  /**
   * Function to wrap consecutive misparented inline content into
   * a ruby base box or a ruby text box.
   */
  void WrapItemsInPseudoRubyLeafBox(FCItemIterator& aIter,
                                    ComputedStyle* aParentStyle,
                                    nsIContent* aParentContent);

  /**
   * Function to wrap consecutive misparented items
   * into a ruby level container.
   */
  inline void WrapItemsInPseudoRubyLevelContainer(
      nsFrameConstructorState& aState, FCItemIterator& aIter,
      ComputedStyle* aParentStyle, nsIContent* aParentContent);

  /**
   * Function to trim leading and trailing whitespaces.
   */
  inline void TrimLeadingAndTrailingWhitespaces(
      nsFrameConstructorState& aState, FrameConstructionItemList& aItems);

  /**
   * Function to create internal ruby boxes.
   */
  inline void CreateNeededPseudoInternalRubyBoxes(
      nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
      nsIFrame* aParentFrame);

  /**
   * Function to create the pseudo intermediate containers we need.
   * @param aItems the child frame construction items before pseudo creation
   * @param aParentFrame the parent frame we're creating pseudos for
   */
  inline void CreateNeededPseudoContainers(nsFrameConstructorState& aState,
                                           FrameConstructionItemList& aItems,
                                           nsIFrame* aParentFrame);

  /**
   * Function to wrap consecutive items into a pseudo parent.
   */
  inline void WrapItemsInPseudoParent(nsIContent* aParentContent,
                                      ComputedStyle* aParentStyle,
                                      ParentType aWrapperType,
                                      FCItemIterator& aIter,
                                      const FCItemIterator& aEndIter);

  /**
   * Function to create the pseudo siblings we need.
   */
  inline void CreateNeededPseudoSiblings(nsFrameConstructorState& aState,
                                         FrameConstructionItemList& aItems,
                                         nsIFrame* aParentFrame);

  // END TABLE SECTION

 protected:
  static nsIFrame* CreatePlaceholderFrameFor(PresShell* aPresShell,
                                             nsIContent* aContent,
                                             nsIFrame* aFrame,
                                             nsContainerFrame* aParentFrame,
                                             nsIFrame* aPrevInFlow,
                                             nsFrameState aTypeBit);

 private:
  // ConstructFieldSetFrame puts the new frame in aFrameList and
  // handles the kids of the fieldset
  nsIFrame* ConstructFieldSetFrame(nsFrameConstructorState& aState,
                                   FrameConstructionItem& aItem,
                                   nsContainerFrame* aParentFrame,
                                   const nsStyleDisplay* aStyleDisplay,
                                   nsFrameList& aFrameList);

  nsIFrame* ConstructListBoxSelectFrame(nsFrameConstructorState& aState,
                                        FrameConstructionItem& aItem,
                                        nsContainerFrame* aParentFrame,
                                        const nsStyleDisplay* aStyleDisplay,
                                        nsFrameList& aFrameList);

  nsIFrame* ConstructTextControl(nsFrameConstructorState& aState,
                                 FrameConstructionItem& aItem,
                                 nsContainerFrame* aParentFrame,
                                 const nsStyleDisplay* aStyleDisplay,
                                 nsFrameList& aFrameList);

  // Creates a block frame wrapping an anonymous ruby frame.
  nsIFrame* ConstructBlockRubyFrame(nsFrameConstructorState& aState,
                                    FrameConstructionItem& aItem,
                                    nsContainerFrame* aParentFrame,
                                    const nsStyleDisplay* aStyleDisplay,
                                    nsFrameList& aFrameList);

  void ConstructTextFrame(const FrameConstructionData* aData,
                          nsFrameConstructorState& aState, nsIContent* aContent,
                          nsContainerFrame* aParentFrame,
                          ComputedStyle* aComputedStyle,
                          nsFrameList& aFrameList);

  // If aPossibleTextContent is a text node and doesn't have a frame, append a
  // frame construction item for it to aItems.
  void AddTextItemIfNeeded(nsFrameConstructorState& aState,
                           const ComputedStyle& aParentStyle,
                           const InsertionPoint& aInsertion,
                           nsIContent* aPossibleTextContent,
                           FrameConstructionItemList& aItems);

  // If aContent is a text node and doesn't have a frame, try to create a frame
  // for it.
  void ReframeTextIfNeeded(nsIContent* aContent);

  void AppendPageBreakItem(nsIContent* aContent,
                           FrameConstructionItemList& aItems);

  // Function to find FrameConstructionData for aElement.  Will return
  // null if aElement is not HTML.
  // aParentFrame might be null.  If it is, that means it was an
  // inline frame.
  static const FrameConstructionData* FindHTMLData(const Element&,
                                                   nsIFrame* aParentFrame,
                                                   ComputedStyle&);
  // HTML data-finding helper functions
  static const FrameConstructionData* FindSelectData(const Element&,
                                                     ComputedStyle&);
  static const FrameConstructionData* FindImgData(const Element&,
                                                  ComputedStyle&);
  static const FrameConstructionData* FindHTMLButtonData(const Element&,
                                                         ComputedStyle&);
  static const FrameConstructionData* FindGeneratedImageData(const Element&,
                                                             ComputedStyle&);
  static const FrameConstructionData* FindImgControlData(const Element&,
                                                         ComputedStyle&);
  static const FrameConstructionData* FindSearchControlData(const Element&,
                                                            ComputedStyle&);
  static const FrameConstructionData* FindInputData(const Element&,
                                                    ComputedStyle&);
  static const FrameConstructionData* FindObjectData(const Element&,
                                                     ComputedStyle&);
  static const FrameConstructionData* FindCanvasData(const Element&,
                                                     ComputedStyle&);
  // <details> always creates a block per spec *if* the about:config pref
  // 'layout.details.force-block-layout' is set to 'true'.  This is a legacy
  // restriction (based on old spec-text) and we're planning to remove it.
  static const FrameConstructionData* FindDetailsData(const Element&,
                                                      ComputedStyle&);

  /* Construct a frame from the given FrameConstructionItem.  This function
     will handle adding the frame to frame lists, processing children, setting
     the frame as the primary frame for the item's content, and so forth.

     @param aItem the FrameConstructionItem to use.
     @param aState the frame construction state to use.
     @param aParentFrame the frame to set as the parent of the
                         newly-constructed frame.
     @param aFrameList the frame list to add the new frame (or its
                        placeholder) to.
  */
  void ConstructFrameFromItemInternal(FrameConstructionItem& aItem,
                                      nsFrameConstructorState& aState,
                                      nsContainerFrame* aParentFrame,
                                      nsFrameList& aFrameList);

  // The guts of AddFrameConstructionItems
  // aParentFrame might be null.  If it is, that means it was an
  // inline frame.
  void AddFrameConstructionItemsInternal(nsFrameConstructorState& aState,
                                         nsIContent* aContent,
                                         nsContainerFrame* aParentFrame,
                                         bool aSuppressWhiteSpaceOptimizations,
                                         ComputedStyle*, ItemFlags,
                                         FrameConstructionItemList& aItems);

  /**
   * Construct frames for the given item list and parent frame, and put the
   * resulting frames in aFrameList.
   */
  void ConstructFramesFromItemList(nsFrameConstructorState& aState,
                                   FrameConstructionItemList& aItems,
                                   nsContainerFrame* aParentFrame,
                                   bool aParentIsWrapperAnonBox,
                                   nsFrameList& aFrameList);
  void ConstructFramesFromItem(nsFrameConstructorState& aState,
                               FCItemIterator& aItem,
                               nsContainerFrame* aParentFrame,
                               nsFrameList& aFrameList);
  static bool AtLineBoundary(FCItemIterator& aIter);

  nsresult GetAnonymousContent(
      nsIContent* aParent, nsIFrame* aParentFrame,
      nsTArray<nsIAnonymousContentCreator::ContentInfo>& aAnonContent);

  // MathML Mod - RBS
  /**
   * Takes the frames in aBlockList and wraps them in a new anonymous block
   * frame whose content is aContent and whose parent will be aParentFrame.
   * The anonymous block is added to aNewList and aBlockList is cleared.
   */
  void FlushAccumulatedBlock(nsFrameConstructorState& aState,
                             nsIContent* aContent,
                             nsContainerFrame* aParentFrame,
                             nsFrameList& aBlockList, nsFrameList& aNewList);

  // Function to find FrameConstructionData for an element.  Will return
  // null if the element is not MathML.
  static const FrameConstructionData* FindMathMLData(const Element&,
                                                     ComputedStyle&);

  // Function to find FrameConstructionData for an element.  Will return
  // null if the element is not XUL.
  static const FrameConstructionData* FindXULTagData(const Element&,
                                                     ComputedStyle&);
  // XUL data-finding helper functions and structures
  static const FrameConstructionData* FindXULLabelOrDescriptionData(
      const Element&, ComputedStyle&);

  /**
   * Constructs an outer frame, an anonymous child that wraps its real
   * children, and its descendant frames.  This is used by both
   * ConstructOuterSVG and ConstructMarker, which both want an anonymous block
   * child for their children to go in to.
   */
  nsContainerFrame* ConstructSVGFrameWithAnonymousChild(
      nsFrameConstructorState& aState, FrameConstructionItem& aItem,
      nsContainerFrame* aParentFrame, nsFrameList& aFrameList,
      ContainerFrameCreationFunc aConstructor,
      ContainerFrameCreationFunc aInnerConstructor,
      PseudoStyleType aInnerPseudo, bool aCandidateRootFrame);

  /**
   * Construct an SVGOuterSVGFrame.
   */
  nsIFrame* ConstructOuterSVG(nsFrameConstructorState& aState,
                              FrameConstructionItem& aItem,
                              nsContainerFrame* aParentFrame,
                              const nsStyleDisplay* aDisplay,
                              nsFrameList& aFrameList);

  /**
   * Construct an SVGMarkerFrame.
   */
  nsIFrame* ConstructMarker(nsFrameConstructorState& aState,
                            FrameConstructionItem& aItem,
                            nsContainerFrame* aParentFrame,
                            const nsStyleDisplay* aDisplay,
                            nsFrameList& aFrameList);

  static const FrameConstructionData* FindSVGData(const Element&,
                                                  nsIFrame* aParentFrame,
                                                  bool aIsWithinSVGText,
                                                  bool aAllowsTextPathChild,
                                                  ComputedStyle&);

  // Not static because it does PropagateScrollToViewport.  If this
  // changes, make this static.
  const FrameConstructionData* FindDisplayData(const nsStyleDisplay&,
                                               const Element&);

  /**
   * Construct a scrollable block frame
   */
  nsIFrame* ConstructScrollableBlock(nsFrameConstructorState& aState,
                                     FrameConstructionItem& aItem,
                                     nsContainerFrame* aParentFrame,
                                     const nsStyleDisplay* aDisplay,
                                     nsFrameList& aFrameList);

  /**
   * Construct a non-scrollable block frame
   */
  nsIFrame* ConstructNonScrollableBlock(nsFrameConstructorState& aState,
                                        FrameConstructionItem& aItem,
                                        nsContainerFrame* aParentFrame,
                                        const nsStyleDisplay* aDisplay,
                                        nsFrameList& aFrameList);

  // Construct a scrollable block with an already created subclass of
  // ScrollContainerFrame, or nullptr for a plain ScrollContainerFrame.
  void ConstructScrollableBlockWithScrollContainer(
      nsFrameConstructorState& aState, FrameConstructionItem& aItem,
      nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
      nsFrameList& aFrameList, nsContainerFrame*&);

  /**
   * This adds FrameConstructionItem objects to aItemsToConstruct for the
   * anonymous content returned by an nsIAnonymousContentCreator::
   * CreateAnonymousContent implementation.
   * This includes an AutoFrameConstructionPageName argument as it is always
   * the caller's responsibility to handle page-name tracking before calling
   * this function.
   */
  void AddFCItemsForAnonymousContent(
      nsFrameConstructorState& aState, nsContainerFrame* aFrame,
      const nsTArray<nsIAnonymousContentCreator::ContentInfo>& aAnonymousItems,
      FrameConstructionItemList& aItemsToConstruct,
      const AutoFrameConstructionPageName& aUnusedPageNameTracker);

  /**
   * Construct the frames for the children of aContent.  "children" is defined
   * as "whatever FlattenedChildIterator returns for aContent".  This means
   * we're basically operating on children in the "flattened tree":
   *
   *   https://drafts.csswg.org/css-scoping/#flat-tree
   *
   * This method will also handle constructing ::before, ::after,
   * ::first-letter, and ::first-line frames, as needed and if allowed.
   *
   * If the parent is a float containing block, this method will handle pushing
   * it as the float containing block in aState (so there's no need for callers
   * to push it themselves).
   *
   * @param aState the frame construction state
   * @param aContent the content node whose children need frames
   * @param aComputedStyle the style for aContent
   * @param aParentFrame the frame to use as the parent frame for the new
   * in-flow kids. Note that this must be its own content insertion frame, but
   *        need not be be the primary frame for aContent.  This frame will be
   *        pushed as the float containing block, as needed.  aFrame is also
   *        used to find the parent style for the kids' style
   *        (not necessary aFrame's style).
   * @param aCanHaveGeneratedContent Whether to allow :before and
   *        :after styles on the parent.
   * @param aFrameList the list in which we should place the in-flow children
   * @param aAllowBlockStyles Whether to allow first-letter and first-line
   *        styles on the parent.
   * @param aPossiblyLeafFrame if non-null, this should be used for the isLeaf
   *        test and the anonymous content creation.  If null, aFrame will be
   *        used.
   */
  void ProcessChildren(nsFrameConstructorState& aState, nsIContent* aContent,
                       ComputedStyle* aComputedStyle,
                       nsContainerFrame* aParentFrame,
                       const bool aCanHaveGeneratedContent,
                       nsFrameList& aFrameList, const bool aAllowBlockStyles,
                       nsIFrame* aPossiblyLeafFrame = nullptr);

  /**
   * These two functions are used when we start frame creation from a non-root
   * element. They should recreate the same state that we would have
   * arrived at if we had built frames from the root frame to aFrame.
   * Therefore, any calls to PushFloatContainingBlock and
   * PushAbsoluteContainingBlock during frame construction should get
   * corresponding logic in these functions.
   */
 public:
  enum ContainingBlockType { ABS_POS, FIXED_POS };
  nsContainerFrame* GetAbsoluteContainingBlock(nsIFrame* aFrame,
                                               ContainingBlockType aType);
  nsContainerFrame* GetFloatContainingBlock(nsIFrame* aFrame);

 private:
  // Build a scroll container frame, and wrap the scrolled frame. The
  // hierarchy will look like this:
  //
  //       ScrollContainerFrame
  //                 ^
  //                 |
  //               Frame (scrolled frame you passed in as aScrolledFrame)
  //
  // @param aContent the content node of the child to wrap.
  //
  // @param aContentStyle the style that has already been resolved for the
  // content being passed in.
  //
  // @param aScrolledFrame The frame of the content to wrap. This should not be
  // initialized (i.e. Init() should not yet have been called). This method will
  // initialize it with a scrolled pseudo and no nsIContent. The content will be
  // attached to the scroll container frame that this function returns.
  //
  // @param aParentFrame The geometric parent to attach the scroll container
  // frame to.
  //
  // @param aNewFrame [in/out] If this is not nullptr, we will just use it as
  // the scroll container frame, rather than creating a new scroll container
  // frame. Otherwise (i.e. if it's nullptr), we'll create a new scroll
  // container frame, and return it by reference via this param.
  void BuildScrollContainerFrame(nsFrameConstructorState& aState,
                                 nsIContent* aContent,
                                 ComputedStyle* aContentStyle,
                                 nsIFrame* aScrolledFrame,
                                 nsContainerFrame* aParentFrame,
                                 nsContainerFrame*& aNewFrame);

  // Builds the initial scroll container frame.
  already_AddRefed<ComputedStyle> BeginBuildingScrollContainerFrame(
      nsFrameConstructorState& aState, nsIContent* aContent,
      ComputedStyle* aContentStyle, nsContainerFrame* aParentFrame,
      bool aIsRoot, nsContainerFrame*& aNewFrame);

  // Completes the building of the scroll container frame.
  // Creates a view for the scrolledframe and makes it the child of the
  // scroll container frame.
  void FinishBuildingScrollContainerFrame(
      nsContainerFrame* aScrollContainerFrame, nsIFrame* aScrolledFrame);

  void InitializeListboxSelect(nsFrameConstructorState& aState,
                               nsContainerFrame* aScrollFrame,
                               nsContainerFrame* aScrolledFrame,
                               nsIContent* aContent,
                               nsContainerFrame* aParentFrame,
                               ComputedStyle* aComputedStyle,
                               nsFrameList& aFrameList);

  /**
   * Recreate frames for aContent.
   */
  void RecreateFramesForContent(nsIContent* aContent, InsertionKind);

  /**
   *  Handles change of rowspan and colspan attributes on table cells.
   */
  void UpdateTableCellSpans(nsIContent* aContent);

  // If removal of aFrame from the frame tree requires reconstruction of some
  // containing block (either of aFrame or of its parent) due to {ib} splits or
  // table pseudo-frames, recreate the relevant frame subtree.  The return value
  // indicates whether this happened.  aFrame must be the result of a
  // GetPrimaryFrame() call on a content node (which means its parent is also
  // not null).
  bool MaybeRecreateContainerForFrameRemoval(nsIFrame* aFrame);

  nsIFrame* CreateContinuingOuterTableFrame(nsIFrame* aFrame,
                                            nsContainerFrame* aParentFrame,
                                            nsIContent* aContent,
                                            ComputedStyle* aComputedStyle);

  nsIFrame* CreateContinuingTableFrame(nsIFrame* aFrame,
                                       nsContainerFrame* aParentFrame,
                                       nsIContent* aContent,
                                       ComputedStyle* aComputedStyle);

  //----------------------------------------

  // Methods support creating block frames and their children

  already_AddRefed<ComputedStyle> GetFirstLetterStyle(
      nsIContent* aContent, ComputedStyle* aComputedStyle);

  already_AddRefed<ComputedStyle> GetFirstLineStyle(
      nsIContent* aContent, ComputedStyle* aComputedStyle);

  bool ShouldHaveFirstLetterStyle(nsIContent* aContent,
                                  ComputedStyle* aComputedStyle);

  // Check whether a given block has first-letter style.  Make sure to
  // only pass in blocks!  And don't pass in null either.
  bool HasFirstLetterStyle(nsIFrame* aBlockFrame);

  bool ShouldHaveFirstLineStyle(nsIContent* aContent,
                                ComputedStyle* aComputedStyle);

  void ShouldHaveSpecialBlockStyle(nsIContent* aContent,
                                   ComputedStyle* aComputedStyle,
                                   bool* aHaveFirstLetterStyle,
                                   bool* aHaveFirstLineStyle);

  // |aContentParentFrame| should be null if it's really the same as
  // |aParentFrame|.
  // @param aFrameList where we want to put the block in case it's in-flow.
  // @param aNewFrame an in/out parameter. On input it is the block to be
  // constructed. On output it is reset to the outermost
  // frame constructed (e.g. if we need to wrap the block in an
  // nsColumnSetFrame.
  // @param aParentFrame is the desired parent for the (possibly wrapped)
  // block
  // @param aContentParent is the parent the block would have if it
  // were in-flow
  // @param aPositionedFrameForAbsPosContainer if non-null, then the new
  // block should be an abs-pos container and aPositionedFrameForAbsPosContainer
  // is the frame whose style is making this block an abs-pos container.
  void ConstructBlock(nsFrameConstructorState& aState, nsIContent* aContent,
                      nsContainerFrame* aParentFrame,
                      nsContainerFrame* aContentParentFrame,
                      ComputedStyle* aComputedStyle,
                      nsContainerFrame** aNewFrame, nsFrameList& aFrameList,
                      nsIFrame* aPositionedFrameForAbsPosContainer);

  // Build the initial column hierarchy around aColumnContent. This function
  // should be called before constructing aColumnContent's children.
  //
  // Before calling FinishBuildingColumns(), we need to create column-span
  // siblings for aColumnContent's children. Caller can use helpers
  // MayNeedToCreateColumnSpanSiblings() and CreateColumnSpanSiblings() to
  // check whether column-span siblings might need to be created and to do
  // the actual work of creating them if they're needed.
  //
  // @param aColumnContent the block that we're wrapping in a ColumnSet. On
  //        entry to this function it has aComputedStyle as its style. After
  //        this function returns, aColumnContent has a ::-moz-column-content
  //        anonymous box style.
  // @param aParentFrame the parent frame we want to use for the
  //        ColumnSetWrapperFrame (which would have been the parent of
  //        aColumnContent if we were not creating a column hierarchy).
  // @param aContent is the content of the aColumnContent.
  // @return the outermost ColumnSetWrapperFrame.
  nsBlockFrame* BeginBuildingColumns(nsFrameConstructorState& aState,
                                     nsIContent* aContent,
                                     nsContainerFrame* aParentFrame,
                                     nsContainerFrame* aColumnContent,
                                     ComputedStyle* aComputedStyle);

  // Complete building the column hierarchy by first wrapping each
  // non-column-span child in aChildList in a ColumnSetFrame (skipping
  // column-span children), and reparenting them to have aColumnSetWrapper
  // as their parent.
  //
  // @param aColumnSetWrapper is the frame returned by
  //        BeginBuildingColumns(), and is the grandparent of aColumnContent.
  // @param aColumnContent is the block frame passed into
  //        BeginBuildingColumns()
  // @param aColumnContentSiblings contains the aColumnContent's siblings, which
  //        are the column spanners and aColumnContent's continuations returned
  //        by CreateColumnSpanSiblings(). It'll become empty after this call.
  void FinishBuildingColumns(nsFrameConstructorState& aState,
                             nsContainerFrame* aColumnSetWrapper,
                             nsContainerFrame* aColumnContent,
                             nsFrameList& aColumnContentSiblings);

  // Return whether aBlockFrame's children in aChildList, which might
  // contain column-span, may need to be wrapped in
  // ::moz-column-span-wrapper and promoted as aBlockFrame's siblings.
  //
  // @param aBlockFrame is the parent of the frames in aChildList.
  //
  // Note: This a check without actually looking into each frame in the
  // child list, so it may return false positive.
  bool MayNeedToCreateColumnSpanSiblings(nsContainerFrame* aBlockFrame,
                                         const nsFrameList& aChildList);

  // Wrap consecutive runs of column-span kids and runs of non-column-span
  // kids in blocks for aInitialBlock's children.
  //
  // @param aInitialBlock is the parent of those frames in aChildList.
  // @param aChildList must begin with a column-span kid. It becomes empty
  //        after this call.
  // @param aPositionedFrame if non-null, it's the frame whose style is making
  //        aInitialBlock an abs-pos container.
  //
  // Return those wrapping blocks in nsFrameList.
  nsFrameList CreateColumnSpanSiblings(nsFrameConstructorState& aState,
                                       nsContainerFrame* aInitialBlock,
                                       nsFrameList& aChildList,
                                       nsIFrame* aPositionedFrame);

  // Reconstruct the multi-column containing block of aParentFrame when we want
  // to insert aFrameList into aParentFrame immediately after aPrevSibling but
  // cannot fix the frame tree because aFrameList contains some column-spans.
  //
  // Note: This method is intended to be called as a helper in ContentAppended()
  // and ContentRangeInserted(). It assumes aState was set up locally and wasn't
  // used to construct any ancestors of aParentFrame in aFrameList.
  //
  // @param aParentFrame the to-be parent frame for aFrameList.
  // @param aFrameList the frames to be inserted. It will be cleared if we need
  //        reconstruction.
  // @param aPrevSibling the position where the frames in aFrameList are going
  //        to be inserted. Nullptr means aFrameList is being inserted at
  //        the beginning.
  // @return true if the multi-column containing block of aParentFrame is
  //         reconstructed; false otherwise.
  bool MaybeRecreateForColumnSpan(nsFrameConstructorState& aState,
                                  nsContainerFrame* aParentFrame,
                                  nsFrameList& aFrameList,
                                  nsIFrame* aPrevSibling);

  nsIFrame* ConstructInline(nsFrameConstructorState& aState,
                            FrameConstructionItem& aItem,
                            nsContainerFrame* aParentFrame,
                            const nsStyleDisplay* aDisplay,
                            nsFrameList& aFrameList);

  /**
   * Create any additional {ib} siblings needed to contain aChildList and put
   * them in aSiblings.
   *
   * @param aState the frame constructor state
   * @param aInitialInline is an already-existing inline frame that will be
   *                       part of this {ib} split and come before everything
   *                       in aSiblings.
   * @param aIsPositioned true if aInitialInline is positioned.
   * @param aChildList is a child list starting with a block; this method
   *                    assumes that the inline has already taken all the
   *                    children it wants.  When the method returns aChildList
   *                    will be empty.
   * @param aSiblings the nsFrameList to put the newly-created siblings into.
   *
   * This method is responsible for making any SetFrameIsIBSplit calls that are
   * needed.
   */
  void CreateIBSiblings(nsFrameConstructorState& aState,
                        nsContainerFrame* aInitialInline, bool aIsPositioned,
                        nsFrameList& aChildList, nsFrameList& aSiblings);

  /**
   * For an inline aParentItem, construct its list of child
   * FrameConstructionItems and set its mIsAllInline flag appropriately.
   */
  void BuildInlineChildItems(nsFrameConstructorState& aState,
                             FrameConstructionItem& aParentItem,
                             bool aItemIsWithinSVGText,
                             bool aItemAllowsTextPathChild);

  // Determine whether we need to wipe out aFrame (the insertion parent) and
  // rebuild the entire subtree when we insert or append new content under
  // aFrame.
  //
  // This is similar to WipeContainingBlock(), but is called
  // before constructing any frame construction items. Any container frames
  // which need reframing regardless of the content inserted or appended can add
  // a check in this method.
  //
  // @return true if we reconstructed the insertion parent frame; false
  // otherwise
  bool WipeInsertionParent(nsContainerFrame* aFrame);

  // Determine whether we need to wipe out what we just did and start over
  // because we're doing something like adding block kids to an inline frame
  // (and therefore need an {ib} split).  aPrevSibling must be correct, even in
  // aIsAppend cases.  Passing aIsAppend false even when an append is happening
  // is ok in terms of correctness, but can lead to unnecessary reframing.
  //
  // @return true if we reconstructed the containing block, false otherwise.
  bool WipeContainingBlock(nsFrameConstructorState& aState,
                           nsIFrame* aContainingBlock, nsIFrame* aFrame,
                           FrameConstructionItemList& aItems, bool aIsAppend,
                           nsIFrame* aPrevSibling);

  void ReframeContainingBlock(nsIFrame* aFrame);

  //----------------------------------------

  // Methods support :first-letter style

  nsFirstLetterFrame* CreateFloatingLetterFrame(
      nsFrameConstructorState& aState, mozilla::dom::Text* aTextContent,
      nsIFrame* aTextFrame, nsContainerFrame* aParentFrame,
      ComputedStyle* aParentStyle, ComputedStyle* aComputedStyle,
      nsFrameList& aResult);

  void CreateLetterFrame(nsContainerFrame* aBlockFrame,
                         nsContainerFrame* aBlockContinuation,
                         mozilla::dom::Text* aTextContent,
                         nsContainerFrame* aParentFrame, nsFrameList& aResult);

  void WrapFramesInFirstLetterFrame(nsContainerFrame* aBlockFrame,
                                    nsFrameList& aBlockFrames);

  /**
   * Looks in the block aBlockFrame for a text frame that contains the
   * first-letter of the block and creates the necessary first-letter frames
   * and returns them in aLetterFrames.
   *
   * @param aBlockFrame the (first-continuation of) the block we are creating a
   *                    first-letter frame for
   * @param aBlockContinuation the current continuation of the block that we
   *                           are looking in for a textframe with suitable
   *                           contents for first-letter
   * @param aParentFrame the current frame whose children we are looking at for
   *                     a suitable first-letter textframe
   * @param aParentFrameList the first child of aParentFrame
   * @param aModifiedParent returns the parent of the textframe that contains
   *                        the first-letter
   * @param aTextFrame returns the textframe that had the first-letter
   * @param aPrevFrame returns the previous sibling of aTextFrame
   * @param aLetterFrames returns the frames that were created
   */
  void WrapFramesInFirstLetterFrame(
      nsContainerFrame* aBlockFrame, nsContainerFrame* aBlockContinuation,
      nsContainerFrame* aParentFrame, nsIFrame* aParentFrameList,
      nsContainerFrame** aModifiedParent, nsIFrame** aTextFrame,
      nsIFrame** aPrevFrame, nsFrameList& aLetterFrames, bool* aStopLooking);

  void RecoverLetterFrames(nsContainerFrame* aBlockFrame);

  void RemoveLetterFrames(PresShell* aPresShell, nsContainerFrame* aBlockFrame);

  // Recursive helper for RemoveLetterFrames
  void RemoveFirstLetterFrames(PresShell* aPresShell, nsContainerFrame* aFrame,
                               nsContainerFrame* aBlockFrame,
                               bool* aStopLooking);

  // Special remove method for those pesky floating first-letter frames
  void RemoveFloatingFirstLetterFrames(PresShell* aPresShell,
                                       nsIFrame* aBlockFrame);

  // Capture state for the frame tree rooted at the frame associated with the
  // content object, aContent
  void CaptureStateForFramesOf(nsIContent* aContent,
                               nsILayoutHistoryState* aHistoryState);

  //----------------------------------------

  // Methods support :first-line style

  // This method chops the initial inline-outside frames out of aFrameList.
  // If aLineFrame is non-null, it appends them to that frame.  Otherwise, it
  // creates a new line frame, sets the inline frames as its initial child
  // list, and inserts that line frame at the front of what's left of
  // aFrameList.  In both cases, the kids are reparented to the line frame.
  // After this call, aFrameList holds the frames that need to become kids of
  // the block (possibly including line frames).
  void WrapFramesInFirstLineFrame(nsFrameConstructorState& aState,
                                  nsIContent* aBlockContent,
                                  nsContainerFrame* aBlockFrame,
                                  nsFirstLineFrame* aLineFrame,
                                  nsFrameList& aFrameList);

  // Handle the case when a block with first-line style is appended to (by
  // possibly calling WrapFramesInFirstLineFrame as needed).
  void AppendFirstLineFrames(nsFrameConstructorState& aState,
                             nsIContent* aContent,
                             nsContainerFrame* aBlockFrame,
                             nsFrameList& aFrameList);

  /**
   * When aFrameList is being inserted into aParentFrame, and aParentFrame has
   * pseudo-element-affected styles, it's possible that we're inserting under a
   * ::first-line frame.  In that case, with servo's style system, the styles we
   * resolved for aFrameList are wrong (they don't take ::first-line into
   * account), and we should fix them up, which is what this method does.
   *
   * This method does not mutate aFrameList.
   */
  void CheckForFirstLineInsertion(nsIFrame* aParentFrame,
                                  nsFrameList& aFrameList);

  // The direction in which we should look for siblings.
  enum class SiblingDirection {
    Forward,
    Backward,
  };

  /**
   * Find the frame for the content immediately next to the one aIter points to,
   * in the direction SiblingDirection indicates, following continuations if
   * necessary.
   *
   * aIter is passed by const reference on purpose, so as not to modify the
   * caller's iterator.
   *
   * @param aIter should be positioned such that aIter.GetPreviousChild()
   *          is the first content to search for frames
   */
  template <SiblingDirection>
  nsIFrame* FindSibling(const mozilla::dom::FlattenedChildIterator& aIter);

  // Helper for the implementation of FindSibling.
  //
  // Beware that this function does mutate the iterator.
  template <SiblingDirection>
  nsIFrame* FindSiblingInternal(mozilla::dom::FlattenedChildIterator&);

  // An alias of FindSibling<SiblingDirection::Forward>.
  nsIFrame* FindNextSibling(const mozilla::dom::FlattenedChildIterator& aIter);
  // An alias of FindSibling<SiblingDirection::Backward>.
  nsIFrame* FindPreviousSibling(
      const mozilla::dom::FlattenedChildIterator& aIter);

  // Given a potential first-continuation sibling frame, return the
  // appropriate continuation the new frame should be inserted next to.
  nsIFrame* AdjustSiblingFrame(nsIFrame* aSibling, SiblingDirection);

  // Find the right previous sibling for an insertion.  This also updates the
  // parent frame to point to the correct continuation of the parent frame to
  // use, and returns whether this insertion is to be treated as an append.
  // aChild is the child being inserted.
  // It is the callers' responsibility to check whether a range insert is safe
  // with regards to fieldsets / tables.
  nsIFrame* GetInsertionPrevSibling(InsertionPoint* aInsertion,  // inout
                                    nsIContent* aChild, bool* aIsAppend);

  void QuotesDirty();
  void CountersDirty();

  void ConstructAnonymousContentForRoot(nsFrameConstructorState& aState,
                                        nsContainerFrame* aCanvasFrame,
                                        nsIContent* aDocElement, nsFrameList&);

 public:
  friend class nsFrameConstructorState;

 private:
  // For allocating FrameConstructionItems from the mFCItemPool arena.
  friend struct FrameConstructionItem;
  void* AllocateFCItem();
  void FreeFCItem(FrameConstructionItem*);

  mozilla::dom::Document* mDocument;  // Weak ref

  // See the comment at the start of ConstructRootFrame for more details
  // about the following frames.

  // This is just the outermost frame for the root element.
  nsContainerFrame* mRootElementFrame = nullptr;
  // This is the frame for the root element that has no pseudo-element style.
  nsIFrame* mRootElementStyleFrame = nullptr;
  // This is the containing block that contains the root element ---
  // the real "initial containing block" according to CSS 2.1.
  nsCanvasFrame* mDocElementContainingBlock = nullptr;
  // This is usually mDocElementContainingBlock, except when printing, where it
  // is the canvas frame that is under all the printed pages.
  nsCanvasFrame* mCanvasFrame = nullptr;
  nsPageSequenceFrame* mPageSequenceFrame = nullptr;

  // FrameConstructionItem arena + list of freed items available for re-use.
  mozilla::ArenaAllocator<4096, 8> mFCItemPool;

  // This indicates what page name to use for the next nsPageContentFrame.
  // Set when CSS named pages cause a breakpoint.
  // This does not apply to the first page content frame, which has its name
  // set by nsPageContentFrame::EnsurePageName() during first reflow.
  RefPtr<const nsAtom> mNextPageContentFramePageName;

  struct FreeFCItemLink {
    FreeFCItemLink* mNext;
  };
  FreeFCItemLink* mFirstFreeFCItem;
  size_t mFCItemsInUse;

  mozilla::ContainStyleScopeManager mContainStyleScopeManager;

  // Current ProcessChildren depth.
  uint16_t mCurrentDepth;
  bool mQuotesDirty : 1;
  bool mCountersDirty : 1;
  bool mAlwaysCreateFramesForIgnorableWhitespace : 1;
  bool mRemovingContent : 1;

  // The layout state from our history entry (to restore scroll positions and
  // such from history), or a new one if there was none (so we can store scroll
  // positions and such during reframe).
  //
  // FIXME(bug 1397239): This can leak some state sometimes for the lifetime of
  // the frame constructor, which is not great.
  nsCOMPtr<nsILayoutHistoryState> mFrameTreeState;
};

#endif /* nsCSSFrameConstructor_h_ */
