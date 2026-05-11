/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// nsMenuPopupFrame
//

#ifndef nsMenuPopupFrame_h_
#define nsMenuPopupFrame_h_

#include "Units.h"
#include "mozilla/Attributes.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/Types.h"
#include "nsAtom.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsExpirationState.h"
#include "nsIDOMEventListener.h"
#include "nsIWidgetListener.h"
#include "nsXULPopupManager.h"

class nsIWidget;

namespace mozilla {
class PresShell;
enum class WindowShadow : uint8_t;
namespace dom {
class KeyboardEvent;
class XULButtonElement;
class XULPopupElement;
}  // namespace dom
namespace widget {
enum class PopupLevel : uint8_t;
}
}  // namespace mozilla

enum ConsumeOutsideClicksResult {
  ConsumeOutsideClicks_ParentOnly =
      0,                          // Only consume clicks on the parent anchor
  ConsumeOutsideClicks_True = 1,  // Always consume clicks
  ConsumeOutsideClicks_Never = 2  // Never consume clicks
};

// How a popup may be flipped. Flipping to the outside edge is like how a
// submenu would work. The entire popup is flipped to the opposite side of the
// anchor.
enum class FlipStyle {
  None = 0,
  Outside = 1,
  Inside = 2,
};

// Values for the flip attribute
enum class FlipType {
  Default = 0,
  None = 1,   // don't try to flip or translate to stay onscreen
  Both = 2,   // flip in both directions
  Slide = 3,  // allow the arrow to "slide" instead of resizing
};

enum class IsNativeMenu : bool { No, Yes };

enum class MenuPopupAnchorType : uint8_t {
  Node = 0,   // anchored to a node
  Point = 1,  // unanchored, and positioned at a screen point
  Rect = 2,   // anchored at a screen rectangle
};

// values are selected so that the direction can be flipped just by
// changing the sign
#define POPUPALIGNMENT_NONE 0
#define POPUPALIGNMENT_TOPLEFT 1
#define POPUPALIGNMENT_TOPRIGHT -1
#define POPUPALIGNMENT_BOTTOMLEFT 2
#define POPUPALIGNMENT_BOTTOMRIGHT -2

#define POPUPALIGNMENT_LEFTCENTER 16
#define POPUPALIGNMENT_RIGHTCENTER -16
#define POPUPALIGNMENT_TOPCENTER 17
#define POPUPALIGNMENT_BOTTOMCENTER 18

// The constants here are selected so that horizontally and vertically flipping
// can be easily handled using the two flip macros below.
#define POPUPPOSITION_UNKNOWN -1
#define POPUPPOSITION_BEFORESTART 0
#define POPUPPOSITION_BEFOREEND 1
#define POPUPPOSITION_AFTERSTART 2
#define POPUPPOSITION_AFTEREND 3
#define POPUPPOSITION_STARTBEFORE 4
#define POPUPPOSITION_ENDBEFORE 5
#define POPUPPOSITION_STARTAFTER 6
#define POPUPPOSITION_ENDAFTER 7
#define POPUPPOSITION_OVERLAP 8
#define POPUPPOSITION_AFTERPOINTER 9
#define POPUPPOSITION_SELECTION 10

#define POPUPPOSITION_HFLIP(v) (v ^ 1)
#define POPUPPOSITION_VFLIP(v) (v ^ 2)

nsIFrame* NS_NewMenuPopupFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle);

class nsMenuPopupFrame;

// this class is used for dispatching popupshown events asynchronously.
class nsXULPopupShownEvent final : public mozilla::Runnable,
                                   public nsIDOMEventListener {
 public:
  nsXULPopupShownEvent(nsIContent* aPopup, nsPresContext* aPresContext)
      : mozilla::Runnable("nsXULPopupShownEvent"),
        mPopup(aPopup),
        mPresContext(aPresContext) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIDOMEVENTLISTENER

  void CancelListener();

 protected:
  virtual ~nsXULPopupShownEvent() = default;

 private:
  const nsCOMPtr<nsIContent> mPopup;
  const RefPtr<nsPresContext> mPresContext;
};

class nsMenuPopupFrame final : public nsBlockFrame, public nsIWidgetListener {
  using PopupLevel = mozilla::widget::PopupLevel;
  using PopupType = mozilla::widget::PopupType;

 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsMenuPopupFrame)

  explicit nsMenuPopupFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);
  ~nsMenuPopupFrame();

  // as popups are opened asynchronously, the popup pending state is used to
  // prevent multiple requests from attempting to open the same popup twice
  nsPopupState PopupState() const { return mPopupState; }
  void SetPopupState(nsPopupState);

  /*
   * When this popup is open, should clicks outside of it be consumed?
   * Return true if the popup should rollup on an outside click,
   * but consume that click so it can't be used for anything else.
   * Return false to allow clicks outside the popup to activate content
   * even when the popup is open.
   * ---------------------------------------------------------------------
   *
   * Should clicks outside of a popup be eaten?
   *
   *       Menus     Autocomplete     Comboboxes
   * Mac     Eat           No              Eat
   * Win     No            No              Eat
   * Unix    Eat           No              Eat
   *
   */
  ConsumeOutsideClicksResult ConsumeOutsideClicks();

  mozilla::dom::XULPopupElement& PopupElement() const;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  nsIWidget* GetWidget() const;
  already_AddRefed<nsIWidget> ComputeParentWidget() const;

  enum class WidgetStyle : uint8_t {
    ColorScheme,
    InputRegion,
    Opacity,
    Shadow,
    Transform,
    MicaBackdrop,
  };
  using WidgetStyleFlags = mozilla::EnumSet<WidgetStyle>;
  static constexpr WidgetStyleFlags AllWidgetStyleFlags() {
    return {WidgetStyle::ColorScheme, WidgetStyle::InputRegion,
            WidgetStyle::Opacity,     WidgetStyle::Shadow,
            WidgetStyle::Transform,   WidgetStyle::MicaBackdrop};
  }
  void PropagateStyleToWidget(WidgetStyleFlags = AllWidgetStyleFlags()) const;

  // Overridden methods
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            AttrModType aModType) override;

  // nsIWidgetListener
  mozilla::PresShell* GetPresShell() override { return PresShell(); }
  nsMenuPopupFrame* GetAsMenuPopupFrame() override { return this; }
  void WindowMoved(nsIWidget*, const mozilla::LayoutDeviceIntPoint&,
                   ByMoveToRect) override;
  void WindowResized(nsIWidget*, const mozilla::LayoutDeviceIntSize&) override;
  bool RequestWindowClose(nsIWidget*) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsEventStatus HandleEvent(mozilla::WidgetGUIEvent* aEvent) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void PaintWindow(nsIWidget* aWidget) override;
  void DidCompositeWindow(mozilla::layers::TransactionId aTransactionId,
                          const mozilla::TimeStamp& aCompositeStart,
                          const mozilla::TimeStamp& aCompositeEnd) override;
  bool ShouldNotBeVisible() override { return !IsOpen(); }
  using nsIFrame::HandleEvent;  // Needed to silence warning.

  // FIXME: This shouldn't run script (this can end up calling HidePopup).
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Destroy(DestroyContext&) override;

  bool HasRemoteContent() const;

  // Whether this is a drag popup to show drag feedback.
  bool IsDragPopup() const;

  // Whether we should have a widget even when we're not shown.
  bool ShouldHaveWidgetWhenHidden() const;

  // Whether we should expand the menu to take the size of the parent menulist.
  bool ShouldExpandToInflowParentOrAnchor() const;

  // Returns true if the popup is a panel with the noautohide attribute set to
  // true. These panels do not roll up automatically.
  bool IsNoAutoHide() const;

  PopupLevel GetPopupLevel() const { return GetPopupLevel(IsNoAutoHide()); }

  // Ensure that a widget has already been created for this view, and create
  // one if it hasn't. If aForceRecreate is true, destroys any existing widget
  // and creates a new one, regardless of whether one has already been created.
  // Otherwise does so only if needed.
  void PrepareWidget(bool aForceRecreate = false);

  MOZ_CAN_RUN_SCRIPT void EnsureActiveMenuListItemIsVisible();

  void CreateWidget();
  void DestroyWidget();
  mozilla::WindowShadow GetShadowStyle() const;

  void DidSetComputedStyle(ComputedStyle* aOldStyle) override;

  // layout, position and display the popup as needed
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void LayoutPopup(nsPresContext*, ReflowOutput&, const ReflowInput&,
                   nsReflowStatus&);

  // Set the position of the popup relative to the anchor content, anchored at a
  // rectangle, or at a specific point if a screen position is set. The popup
  // will be adjusted so that it is on screen. If aIsMove is true, then the
  // popup is being moved, and should not be flipped.
  void SetPopupPosition(bool aIsMove);

  // Called when the Enter key is pressed while the popup is open. This will
  // just pass the call down to the current menu, if any.
  // Also, calling Enter will reset the current incremental search string,
  // calculated in FindMenuWithShortcut.
  MOZ_CAN_RUN_SCRIPT void HandleEnterKeyPress(mozilla::WidgetEvent&);

  // Locate and return the menu frame that should be activated for the supplied
  // key event. If aDoAction is set to true by this method, then the menu's
  // action should be carried out, as if the user had pressed the Enter key. If
  // aDoAction is false, the menu should just be highlighted.
  // This method also handles incremental searching in menus so the user can
  // type the first few letters of an item/s name to select it.
  mozilla::dom::XULButtonElement* FindMenuWithShortcut(
      mozilla::dom::KeyboardEvent& aKeyEvent, bool& aDoAction);

  mozilla::dom::XULButtonElement* GetCurrentMenuItem() const;
  nsIFrame* GetCurrentMenuItemFrame() const;

  PopupType GetPopupType() const { return mPopupType; }
  bool IsContextMenu() const { return mIsContextMenu; }

  bool IsOpen() const {
    return mPopupState == ePopupOpening || mPopupState == ePopupVisible ||
           mPopupState == ePopupShown;
  }
  bool IsVisible() const {
    return mPopupState == ePopupVisible || mPopupState == ePopupShown;
  }
  bool IsVisibleOrShowing() const {
    return IsOpen() || mPopupState == ePopupPositioning ||
           mPopupState == ePopupShowing;
  }
  bool IsNativeMenu() const { return mIsNativeMenu; }
  bool CanSkipLayout() const;
  bool IsMouseTransparent() const;

  // Return true if the popup is for a menulist.
  bool IsMenuList() const;

  bool IsDragSource() const { return mIsDragSource; }
  void SetIsDragSource(bool aIsDragSource) { mIsDragSource = aIsDragSource; }

  bool PendingWidgetMoveResize() const { return mPendingWidgetMoveResize; }
  void ClearPendingWidgetMoveResize() { mPendingWidgetMoveResize = false; }
  void SchedulePendingWidgetMoveResize();

  static nsIContent* GetTriggerContent(nsMenuPopupFrame* aMenuPopupFrame);
  void ClearTriggerContent() { mTriggerContent = nullptr; }
  void ClearTriggerContentIncludingDocument();

  // returns true if the popup is in a content shell, or false for a popup in
  // a chrome shell
  bool IsInContentShell() const { return mInContentShell; }

  // the Initialize methods are used to set the anchor position for
  // each way of opening a popup.
  void InitializePopup(nsIContent* aAnchorContent, nsIContent* aTriggerContent,
                       const nsAString& aPosition, int32_t aXPos, int32_t aYPos,
                       MenuPopupAnchorType aAnchorType,
                       bool aAttributesOverride,
                       enum IsNativeMenu aIsNativeMenu);

  void InitializePopupAtRect(nsIContent* aTriggerContent,
                             const nsAString& aPosition, const nsIntRect& aRect,
                             bool aAttributesOverride,
                             enum IsNativeMenu aIsNativeMenu);

  /**
   * @param aIsContextMenu if true, then the popup is
   * positioned at a slight offset from aXPos/aYPos to ensure the
   * (presumed) mouse position is not over the menu.
   */
  void InitializePopupAtScreen(nsIContent* aTriggerContent, int32_t aXPos,
                               int32_t aYPos, bool aIsContextMenu,
                               enum IsNativeMenu aIsNativeMenu);

  // indicate that the popup should be opened
  void ShowPopup(bool aIsContextMenu);
  // indicate that the popup should be hidden. The new state should either be
  // ePopupClosed or ePopupInvisible.
  MOZ_CAN_RUN_SCRIPT void HidePopup(bool aDeselectMenu, nsPopupState aNewState,
                                    bool aFromFrameDestruction = false);

  void ClearIncrementalString() { mIncrementalString.Truncate(); }
  static bool IsWithinIncrementalTime(mozilla::TimeStamp time) {
    return !sLastKeyTime.IsNull() &&
           ((time - sLastKeyTime).ToMilliseconds() <=
            mozilla::StaticPrefs::ui_menu_incremental_search_timeout());
  }

#ifdef DEBUG_FRAME_DUMP
  virtual nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"MenuPopup"_ns, aResult);
  }
#endif

  MOZ_CAN_RUN_SCRIPT void ChangeByPage(bool aIsUp);

  // Move the popup to the screen coordinate |aPos| in CSS pixels.
  // If aUpdateAttrs is true, and the popup already has left or top attributes,
  // then those attributes are updated to the new location.
  // The frame may be destroyed by this method.
  void MoveTo(const mozilla::CSSPoint& aPos, bool aUpdateAttrs,
              bool aByMoveToRect = false);

  void MoveToAnchor(nsIContent* aAnchorContent, const nsAString& aPosition,
                    int32_t aXPos, int32_t aYPos, bool aAttributesOverride);

  mozilla::ScrollContainerFrame* GetScrollContainerFrame() const;

  void SetOverrideConstraintRect(const mozilla::CSSIntRect& aRect) {
    mOverrideConstraintRect = mozilla::CSSIntRect::ToAppUnits(aRect);
  }

  bool IsConstrainedByLayout() const { return mConstrainedByLayout; }

  struct Rects {
    // For anchored popups, the anchor rectangle. For non-anchored popups, the
    // size will be 0.
    nsRect mAnchorRect;
    // mAnchorRect before accounting for flipping / resizing / intersecting with
    // the screen. This is needed for Wayland, which flips / resizes at the
    // widget level.
    nsRect mUntransformedAnchorRect;
    // The final used rect we want to occupy.
    nsRect mUsedRect;
    // The alignment offset for sliding the panel, see
    // nsMenuPopupFrame::mAlignmentOffset.
    nscoord mAlignmentOffset = 0;
    bool mHFlip = false;
    bool mVFlip = false;
    bool mConstrainedByLayout = false;
    nsPoint mViewPoint;
  };

  // For a popup that should appear anchored at the given rect, gets the anchor
  // and constraint rects for that popup.
  // This will be the available area of the screen the popup should be displayed
  // on. Content popups, however, will also be constrained by the content area.
  //
  // For non-toplevel popups (which will always be panels), we will also
  // constrain them to the available screen rect, ie they will not fall
  // underneath the taskbar, dock or other fixed OS elements.
  Rects GetRects(const nsSize& aPrefSize) const;
  Maybe<nsRect> GetConstraintRect(const nsRect& aAnchorRect,
                                  const nsRect& aRootScreenRect,
                                  PopupLevel) const;
  void PerformMove(const Rects&);

  // Return true if the popup is positioned relative to an anchor.
  bool IsAnchored() const { return mAnchorType != MenuPopupAnchorType::Point; }

  // Return the anchor if there is one.
  nsIContent* GetAnchor() const { return mAnchorContent; }
  void ClearAnchorContent() { mAnchorContent = nullptr; }

  // Return the screen coordinates in CSS pixels of the popup,
  // or (-1, -1, 0, 0) if anchored.
  mozilla::CSSIntRect GetScreenAnchorRect() const {
    return mozilla::CSSRect::FromAppUnitsRounded(mScreenRect);
  }

  mozilla::LayoutDeviceIntRect CalcWidgetBounds() const;

  // Return the alignment of the popup
  int8_t GetAlignmentPosition() const;

  // Return the offset applied to the alignment of the popup
  nscoord GetAlignmentOffset() const { return mAlignmentOffset; }

  // Clear the mPopupShownDispatcher, remove the listener and return true if
  // mPopupShownDispatcher was non-null.
  bool ClearPopupShownDispatcher() {
    if (mPopupShownDispatcher) {
      mPopupShownDispatcher->CancelListener();
      mPopupShownDispatcher = nullptr;
      return true;
    }

    return false;
  }

  void ShowWithPositionedEvent() { mPopupState = ePopupPositioning; }

  // Checks for the anchor to change and either moves or hides the popup
  // accordingly. The original position of the anchor should be supplied as
  // the argument. If the popup needs to be hidden, HidePopup will be called by
  // CheckForAnchorChange. If the popup needs to be moved, aRect will be updated
  // with the new rectangle.
  void CheckForAnchorChange(nsRect& aRect);

  void WillDispatchPopupPositioned() { mPendingPositionedEvent = false; }

 protected:
  // returns the popup's level.
  PopupLevel GetPopupLevel(bool aIsNoAutoHide) const;

  void InitPositionFromAnchorAlign(const nsAString& aAnchor,
                                   const nsAString& aAlign);

  // return the position where the popup should be, when it should be
  // anchored at anchorRect. aHFlip and aVFlip will be set if the popup may be
  // flipped in that direction if there is not enough space available.
  nsPoint AdjustPositionForAnchorAlign(nsRect& aAnchorRect,
                                       const nsSize& aPrefSize,
                                       FlipStyle& aHFlip,
                                       FlipStyle& aVFlip) const;

  // For popups that are going to align to their selected item, get the frame of
  // the selected item.
  nsIFrame* GetSelectedItemForAlignment() const;

  // check if the popup will fit into the available space and resize it. This
  // method handles only one axis at a time so is called twice, once for
  // horizontal and once for vertical. All arguments are specified for this
  // one axis. All coordinates are in app units relative to the screen.
  //   aScreenPoint - the point where the popup should appear
  //   aSize - the size of the popup
  //   aScreenBegin - the left or top edge of the screen
  //   aScreenEnd - the right or bottom edge of the screen
  //   aAnchorBegin - the left or top edge of the anchor rectangle
  //   aAnchorEnd - the right or bottom edge of the anchor rectangle
  //   aMarginBegin - the left or top margin of the popup
  //   aMarginEnd - the right or bottom margin of the popup
  //   aFlip - how to flip or resize the popup when there isn't space
  //   aFlipSide - pointer to where current flip mode is stored
  nscoord FlipOrResize(nscoord& aScreenPoint, nscoord aSize,
                       nscoord aScreenBegin, nscoord aScreenEnd,
                       nscoord aAnchorBegin, nscoord aAnchorEnd,
                       nscoord aMarginBegin, nscoord aMarginEnd,
                       FlipStyle aFlip, bool aIsOnEnd, bool* aFlipSide) const;

  // check if the popup can fit into the available space by "sliding" (i.e.,
  // by having the anchor arrow slide along one axis and only resizing if that
  // can't provide the requested size). Only one axis can be slid - the other
  // axis is "flipped" as normal. This method can handle either axis, but is
  // only called for the sliding axis. All coordinates are in app units
  // relative to the screen.
  //   aScreenPoint - the point where the popup should appear
  //   aSize - the size of the popup
  //   aScreenBegin - the left or top edge of the screen
  //   aScreenEnd - the right or bottom edge of the screen
  //   aOffset - the amount by which the arrow must be slid such that it is
  //             still aligned with the anchor.
  // Result is the new size of the popup, which will typically be the same
  // as aSize, unless aSize is greater than the screen width/height.
  nscoord SlideOrResize(nscoord& aScreenPoint, nscoord aSize,
                        nscoord aScreenBegin, nscoord aScreenEnd,
                        nscoord* aOffset) const;

  // Given an anchor frame, compute the anchor rectangle relative to the screen,
  // using the popup frame's app units, and taking into account transforms.
  nsRect ComputeAnchorRect(nsPresContext* aRootPresContext,
                           nsIFrame* aAnchorFrame) const;

  // Move the popup to the position specified in its |left| and |top|
  // attributes.
  void MoveToAttributePosition();

  nsIFrame* GetAnchorFrame() const;

 public:
  /**
   * Return whether the popup direction should be RTL.
   * If the popup has an anchor, its direction is the anchor direction.
   * Otherwise, its the general direction of the UI.
   *
   * Return whether the popup direction should be RTL.
   */
  bool IsDirectionRTL() const;

  // Returns true if the popup should try to remain at the same relative
  // location as the anchor while it is open. If the anchor becomes hidden
  // either directly or indirectly because a parent popup or other element
  // is no longer visible, or a parent deck page is changed, the popup hides
  // as well. The second variation also sets the anchor rectangle, relative to
  // the popup frame.
  bool ShouldFollowAnchor() const;

  bool ShouldFollowAnchor(nsRect& aRect);

  // Returns parent menu widget for submenus that are in the same
  // frame hierarchy, it's needed for Linux/Wayland which demands
  // strict popup windows hierarchy.
  nsIWidget* GetParentMenuWidget();

  // Returns the effective margin for this popup. This is the CSS margin plus
  // the context-menu shift, if needed.
  nsMargin GetMargin() const;

  // These are used by Wayland backend.
  const nsRect& GetUntransformedAnchorRect() const {
    return mUntransformedAnchorRect;
  }
  int8_t GetUntransformedPopupAlignment() const {
    return mUntransformedPopupAlignment;
  }
  int8_t GetUntransformedPopupAnchor() const {
    return mUntransformedPopupAnchor;
  }

  int8_t GetPopupAlignment() const { return mPopupAlignment; }
  int8_t GetPopupAnchor() const { return mPopupAnchor; }
  FlipType GetFlipType() const { return mFlip; }

  uint64_t GetAPZFocusSequenceNumber() const { return mAPZFocusSequenceNumber; }

  void UpdateAPZFocusSequenceNumber(uint64_t aNewNumber) {
    mAPZFocusSequenceNumber = aNewNumber;
  }

  void DestroyWidgetIfNeeded();
  nsExpirationState* GetExpirationState() { return &mExpirationState; }

 protected:
  nsString mIncrementalString;  // for incremental typing navigation

  // the content that the popup is anchored to, if any, which may be in a
  // different document than the popup.
  nsCOMPtr<nsIContent> mAnchorContent;

  // the content that triggered the popup, typically the node where the mouse
  // was clicked. It will be cleared when the popup is hidden.
  nsCOMPtr<nsIContent> mTriggerContent;

  RefPtr<nsIWidget> mWidget;

  RefPtr<nsXULPopupShownEvent> mPopupShownDispatcher;

  // The popup's screen rectangle in app units.
  nsRect mUsedScreenRect;

  // A popup's preferred size may be different than its actual size stored in
  // mRect in the case where the popup was resized because it was too large
  // for the screen. The preferred size mPrefSize holds the full size the popup
  // would be before resizing. Computations are performed using this size.
  nsSize mPrefSize{-1, -1};

  // A point with extra offsets to apply in the horizontal and vertical axes. We
  // don't use an nsMargin because the values would be the same for the same
  // axis.
  nsPoint mExtraMargin;

  nsRect mScreenRect;
  // Used for store rectangle which the popup is going to be anchored to, we
  // need that for Wayland. It's important that this rect is unflipped, and
  // without margins applied, as GTK is what takes care of determining how to
  // flip etc. on Wayland.
  nsRect mUntransformedAnchorRect;

  // If the panel prefers to "slide" rather than resize, then the arrow gets
  // positioned at this offset (along either the x or y axis, depending on
  // mPosition)
  nscoord mAlignmentOffset = 0;

  // The focus sequence number of the last processed input event
  uint64_t mAPZFocusSequenceNumber = 0;

  PopupType mPopupType = PopupType::Panel;  // type of popup
  nsPopupState mPopupState = ePopupClosed;  // open state of the popup

  // popup alignment relative to the anchor node
  // The untransformed variants are needed for Wayland
  int8_t mUntransformedPopupAlignment = POPUPALIGNMENT_NONE;
  int8_t mUntransformedPopupAnchor = POPUPALIGNMENT_NONE;
  int8_t mPopupAlignment = POPUPALIGNMENT_NONE;
  int8_t mPopupAnchor = POPUPALIGNMENT_NONE;
  int8_t mPosition = POPUPPOSITION_UNKNOWN;

  FlipType mFlip = FlipType::Default;  // Whether to flip

  // Whether we were moved by the move-to-rect Wayland callback. In that case,
  // we stop updating the anchor so that we can end up with a stable position.
  bool mPositionedByMoveToRect = false;
  // true if the open state changed since the last layout.
  bool mIsOpenChanged = false;
  // true for context menus and their submenus.
  bool mIsContextMenu = false;
  // true for the topmost context menu.
  bool mIsTopLevelContextMenu = false;
  // true if the popup is in a content shell.
  bool mInContentShell = true;

  // The flip modes that were used when the popup was opened
  bool mHFlip = false;
  bool mVFlip = false;
  // Whether layout has constrained this popup in some way.
  bool mConstrainedByLayout = false;

  // Whether the most recent initialization of this menupopup is native.
  bool mIsNativeMenu = false;

  // Whether we have a pending `popuppositioned` event.
  bool mPendingPositionedEvent = false;

  // Whether this popup is source of D&D operation. We can't close such
  // popup on Wayland as it cancel whole D&D operation.
  bool mIsDragSource = false;

  // Whether there's a pending move-resize of our widget.
  bool mPendingWidgetMoveResize = false;

  // When POPUPPOSITION_SELECTION is used, this indicates the vertical offset
  // that the original selected item was. This needs to be used in case the
  // popup gets changed so that we can keep the popup at the same vertical
  // offset.
  // TODO(emilio): try to make this not mutable.
  mutable nscoord mPositionedOffset = 0;

  // How the popup is anchored.
  MenuPopupAnchorType mAnchorType = MenuPopupAnchorType::Node;

  nsRect mOverrideConstraintRect;

  nsExpirationState mExpirationState;
  static mozilla::TimeStamp sLastKeyTime;
};  // class nsMenuPopupFrame

#endif
