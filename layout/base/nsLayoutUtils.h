/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsLayoutUtils_h__
#define nsLayoutUtils_h__

#include <limits>
#include <algorithm>

#include "gfxPoint.h"
#include "LayoutConstants.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/ScrollableLayerGuid.h"
#include "mozilla/LayoutStructs.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RelativeTo.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPrefs_nglayout.h"
#include "mozilla/SurfaceFromElementResult.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/ToString.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WritingModes.h"
#include "nsBoundingMetrics.h"
#include "nsCSSPropertyIDSet.h"
#include "nsFrameList.h"
#include "nsPoint.h"
#include "nsThreadUtils.h"
#include "Units.h"
// If you're thinking of adding a new include here, please try hard to not.
// This header file gets included just about everywhere and adding headers here
// can dramatically increase avoidable build activity. Try instead:
// - using a forward declaration
// - putting the include in the .cpp file, if it is only needed by the body
// - putting your new functions in some other less-widely-used header

class gfxContext;
class gfxFontEntry;
class imgIContainer;
class nsFrameList;
class nsPresContext;
class nsIContent;
class nsIPrincipal;
class nsIWidget;
class nsAtom;
class nsRegion;
enum nsChangeHint : uint32_t;
class nsFontMetrics;
class nsFontFaceList;
class nsIImageLoadingContent;
class nsBlockFrame;
class nsContainerFrame;
class nsView;
class nsIFrame;
class nsPIDOMWindowOuter;
class imgIRequest;
struct nsStyleFont;

namespace mozilla {
class nsDisplayItem;
class nsDisplayList;
class nsDisplayListBuilder;
enum class nsDisplayListBuilderMode : uint8_t;
class RetainedDisplayListBuilder;
struct AspectRatio;
class ComputedStyle;
class DisplayPortUtils;
class PresShell;
enum class PseudoStyleType : uint8_t;
class EventListenerManager;
enum class LayoutFrameType : uint8_t;
struct IntrinsicSize;
class ReflowOutput;
class WritingMode;
class DisplayItemClip;
class EffectSet;
struct ActiveScrolledRoot;
class ScrollContainerFrame;
enum class ScrollOrigin : uint8_t;
enum class StyleImageOrientation : uint8_t;
enum class StyleSystemFont : uint8_t;
enum class StyleScrollbarWidth : uint8_t;
struct OverflowAreas;
namespace dom {
class CanvasRenderingContext2D;
class DOMRectList;
class Document;
class Element;
class Event;
class HTMLImageElement;
class HTMLCanvasElement;
class HTMLVideoElement;
class ImageBitmap;
class InspectorFontFace;
class OffscreenCanvas;
class Selection;
class VideoFrame;
}  // namespace dom
namespace gfx {
struct RectCornerRadii;
enum class ShapedTextFlags : uint16_t;
}  // namespace gfx
namespace image {
class ImageIntRegion;
struct Resolution;
}  // namespace image
namespace layers {
struct FrameMetrics;
struct ScrollMetadata;
class Image;
class StackingContextHelper;
class Layer;
class WebRenderLayerManager;
}  // namespace layers
namespace widget {
enum class TransparencyMode : uint8_t;
}
}  // namespace mozilla

// Flags to customize the behavior of nsLayoutUtils::DrawString.
enum class DrawStringFlags {
  Default = 0x0,
  ForceHorizontal = 0x1  // Forces the text to be drawn horizontally.
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(DrawStringFlags)

namespace mozilla {

class RectCallback {
 public:
  virtual void AddRect(const nsRect& aRect) = 0;
};

}  // namespace mozilla

/**
 * nsLayoutUtils is a namespace class used for various helper
 * functions that are useful in multiple places in layout.  The goal
 * is not to define multiple copies of the same static helper.
 */
class nsLayoutUtils {
  typedef mozilla::AspectRatio AspectRatio;
  typedef mozilla::ComputedStyle ComputedStyle;
  typedef mozilla::LengthPercentage LengthPercentage;
  typedef mozilla::LengthPercentageOrAuto LengthPercentageOrAuto;
  typedef mozilla::dom::DOMRectList DOMRectList;
  typedef mozilla::layers::StackingContextHelper StackingContextHelper;
  typedef mozilla::IntrinsicSize IntrinsicSize;
  typedef mozilla::RelativeTo RelativeTo;
  typedef mozilla::ScrollOrigin ScrollOrigin;
  typedef mozilla::ViewportType ViewportType;
  typedef mozilla::gfx::SourceSurface SourceSurface;
  typedef mozilla::gfx::sRGBColor sRGBColor;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::ExtendMode ExtendMode;
  typedef mozilla::gfx::SamplingFilter SamplingFilter;
  typedef mozilla::gfx::Float Float;
  typedef mozilla::gfx::Point Point;
  typedef mozilla::gfx::Rect Rect;
  typedef mozilla::gfx::RectDouble RectDouble;
  typedef mozilla::gfx::Size Size;
  typedef mozilla::gfx::Matrix4x4 Matrix4x4;
  typedef mozilla::gfx::Matrix4x4Flagged Matrix4x4Flagged;
  typedef mozilla::gfx::MatrixScales MatrixScales;
  typedef mozilla::gfx::MatrixScalesDouble MatrixScalesDouble;
  typedef mozilla::gfx::RectCornerRadii RectCornerRadii;
  typedef mozilla::gfx::StrokeOptions StrokeOptions;
  typedef mozilla::image::ImgDrawResult ImgDrawResult;

  using nsDisplayItem = mozilla::nsDisplayItem;
  using nsDisplayList = mozilla::nsDisplayList;
  using nsDisplayListBuilder = mozilla::nsDisplayListBuilder;
  using nsDisplayListBuilderMode = mozilla::nsDisplayListBuilderMode;
  using RetainedDisplayListBuilder = mozilla::RetainedDisplayListBuilder;

 public:
  typedef mozilla::layers::FrameMetrics FrameMetrics;
  typedef mozilla::layers::ScrollMetadata ScrollMetadata;
  typedef mozilla::layers::ScrollableLayerGuid::ViewID ViewID;
  typedef mozilla::CSSPoint CSSPoint;
  typedef mozilla::CSSSize CSSSize;
  typedef mozilla::CSSIntSize CSSIntSize;
  typedef mozilla::CSSRect CSSRect;
  typedef mozilla::ScreenMargin ScreenMargin;
  typedef mozilla::LayoutDeviceIntSize LayoutDeviceIntSize;
  typedef mozilla::LayoutDeviceRect LayoutDeviceRect;
  typedef mozilla::PresShell PresShell;
  typedef mozilla::StyleGeometryBox StyleGeometryBox;
  typedef mozilla::SVGImageContext SVGImageContext;
  typedef mozilla::LogicalSize LogicalSize;

  /**
   * Finds previously assigned ViewID for the given content element, if any.
   * Returns whether a ViewID was previously assigned.
   */
  static bool FindIDFor(const nsIContent* aContent, ViewID* aOutViewId);

  /**
   * Finds previously assigned or generates a unique ViewID for the given
   * content element.
   */
  static ViewID FindOrCreateIDFor(nsIContent* aContent);

  /**
   * Find content for given ID.
   */
  static nsIContent* FindContentFor(ViewID aId);

  /**
   * Find the scroll container frame for a given content element.
   */
  static mozilla::ScrollContainerFrame* FindScrollContainerFrameFor(
      nsIContent* aContent);

  /**
   * Find the scroll container frame for a given ID.
   */
  static mozilla::ScrollContainerFrame* FindScrollContainerFrameFor(ViewID aId);

  /**
   * Helper for FindScrollContainerFrameFor(), also used in DisplayPortUtils.
   * Most clients should use FindScrollContainerFrameFor().
   */
  static nsIFrame* GetScrollContainerFrameFromContent(nsIContent* aContent);

  /**
   * Find the ID for a given scroll container frame.
   */
  static ViewID FindIDForScrollContainerFrame(
      mozilla::ScrollContainerFrame* aScrollContainerFrame);

  /**
   * Notify the scroll frame with the given scroll id that its scroll offset
   * is being sent to APZ as part of a paint-skip transaction.
   *
   * Normally, this notification happens during painting, after calls to
   * ComputeScrollMetadata(). During paint-skipping that code is skipped,
   * but it's still important for the scroll frame to be notified for
   * correctness of relative scroll updates, so the code that sends the
   * empty paint-skip transaction needs to call this.
   */
  static void NotifyPaintSkipTransaction(ViewID aScrollId);

  /**
   * Use heuristics to figure out the child list that
   * aChildFrame is currently in.
   */
  static mozilla::FrameChildListID GetChildListNameFor(nsIFrame* aChildFrame);

  /**
   * Returns the ::before pseudo-element for aContent, if any.
   */
  static mozilla::dom::Element* GetBeforePseudo(const nsIContent* aContent);

  /**
   * Returns the frame corresponding to the ::before pseudo-element for
   * aContent, if any.
   */
  static nsIFrame* GetBeforeFrame(const nsIContent* aContent);

  /**
   * Returns the ::after pseudo-element for aContent, if any.
   */
  static mozilla::dom::Element* GetAfterPseudo(const nsIContent* aContent);

  /**
   * Returns the frame corresponding to the ::after pseudo-element for aContent,
   * if any.
   */
  static nsIFrame* GetAfterFrame(const nsIContent* aContent);

  /**
   * Returns the ::marker pseudo-element for aContent, if any.
   */
  static mozilla::dom::Element* GetMarkerPseudo(const nsIContent* aContent);

  /**
   * Returns the frame corresponding to the ::marker pseudo-element for
   * aContent, if any.
   */
  static nsIFrame* GetMarkerFrame(const nsIContent* aContent);

#ifdef ACCESSIBILITY
  /**
   * Set aText to the spoken text for the given ::marker content (aContent)
   * if it has a frame, or the empty string otherwise.
   */
  static void GetMarkerSpokenText(const nsIContent* aContent, nsAString& aText);
#endif

  /**
   * Given a frame, search up the frame tree until we find an
   * ancestor that (or the frame itself) is of type aFrameType, if any.
   *
   * @param aFrame the frame to start at
   * @param aFrameType the frame type to look for
   * @param aStopAt a frame to stop at after we checked it
   * @return a frame of the given type or nullptr if no
   *         such ancestor exists
   */
  static nsIFrame* GetClosestFrameOfType(nsIFrame* aFrame,
                                         mozilla::LayoutFrameType aFrameType,
                                         nsIFrame* aStopAt = nullptr);

  /**
   * Given a frame, search up the frame tree until we find an
   * ancestor that (or the frame itself) is a "Page" frame, if any.
   *
   * @param aFrame the frame to start at
   * @return a frame of type mozilla::LayoutFrameType::Page or nullptr if no
   *         such ancestor exists
   */
  static nsIFrame* GetPageFrame(nsIFrame* aFrame);

  /**
   * Given a frame which is the primary frame for an element,
   * return the frame that has the non-pseudoelement ComputedStyle for
   * the content.
   * This is aPrimaryFrame itself except for tableWrapper frames.
   *
   * Given a non-null input, this will return null if and only if its
   * argument is a table wrapper frame that is mid-destruction (and its
   * table frame has been destroyed).
   */
  static nsIFrame* GetStyleFrame(nsIFrame* aPrimaryFrame);
  static const nsIFrame* GetStyleFrame(const nsIFrame* aPrimaryFrame);

  /**
   * Given a content node,
   * return the frame that has the non-pseudoelement ComputedStyle for
   * the content.  May return null.
   * This is aContent->GetPrimaryFrame() except for tableWrapper frames.
   */
  static nsIFrame* GetStyleFrame(const nsIContent* aContent);

  /**
   * Returns the placeholder size for when the scrollbar is unthemed.
   */
  static mozilla::CSSIntCoord UnthemedScrollbarSize(
      mozilla::StyleScrollbarWidth);

  /**
   * The inverse of GetStyleFrame. Returns |aStyleFrame| unless it is an inner
   * table frame, in which case the table wrapper frame is returned.
   */
  static nsIFrame* GetPrimaryFrameFromStyleFrame(nsIFrame* aStyleFrame);
  static const nsIFrame* GetPrimaryFrameFromStyleFrame(
      const nsIFrame* aStyleFrame);

  /**
   * Similar to nsIFrame::IsPrimaryFrame except that this will return true
   * for the inner table frame rather than for its wrapper frame.
   */
  static bool IsPrimaryStyleFrame(const nsIFrame* aFrame);

  /**
   * CompareTreePosition determines whether aFrame1 comes before or
   * after aFrame2 in a preorder traversal of the frame tree, where out
   * of flow frames are treated as children of their placeholders. This is
   * basically the same ordering as DoCompareTreePosition(nsIContent*) except
   * that it handles anonymous content properly and there are subtleties with
   * continuations.
   *
   * @param aCommonAncestor either null, or a common ancestor of
   *                        aContent1 and aContent2.  Actually this is
   *                        only a hint; if it's not an ancestor of
   *                        aContent1 or aContent2, this function will
   *                        still work, but it will be slower than
   *                        normal.
   * @return < 0 if aContent1 is before aContent2
   *         > 0 if aContent1 is after aContent2,
   *         0 otherwise (meaning they're the same, or they're in
   *           different frame trees)
   */
  static int32_t CompareTreePosition(nsIFrame* aFrame1, nsIFrame* aFrame2,
                                     nsIFrame* aCommonAncestor = nullptr) {
    return DoCompareTreePosition(aFrame1, aFrame2, aCommonAncestor);
  }

  static int32_t CompareTreePosition(nsIFrame* aFrame1, nsIFrame* aFrame2,
                                     nsTArray<nsIFrame*>& aFrame2Ancestors,
                                     nsIFrame* aCommonAncestor = nullptr) {
    return DoCompareTreePosition(aFrame1, aFrame2, aFrame2Ancestors,
                                 aCommonAncestor);
  }

  static nsIFrame* FillAncestors(nsIFrame* aFrame, nsIFrame* aStopAtAncestor,
                                 nsTArray<nsIFrame*>* aAncestors);

  static int32_t DoCompareTreePosition(nsIFrame* aFrame1, nsIFrame* aFrame2,
                                       nsIFrame* aCommonAncestor);
  static int32_t DoCompareTreePosition(nsIFrame* aFrame1, nsIFrame* aFrame2,
                                       nsTArray<nsIFrame*>& aFrame2Ancestors,
                                       nsIFrame* aCommonAncestor);

  /**
   * LastContinuationWithChild gets the last continuation in aFrame's chain
   * that has a child, or the first continuation if the frame has no children.
   */
  static nsContainerFrame* LastContinuationWithChild(nsContainerFrame* aFrame);

  /**
   * GetLastSibling simply finds the last sibling of aFrame, or returns nullptr
   * if aFrame is null.
   */
  static nsIFrame* GetLastSibling(nsIFrame* aFrame);

  /**
   * FindSiblingViewFor locates the child of aParentView that aFrame's
   * view should be inserted 'above' (i.e., before in sibling view
   * order).  This is the first child view of aParentView whose
   * corresponding content is before aFrame's content (view siblings
   * are in reverse content order).
   */
  static nsView* FindSiblingViewFor(nsView* aParentView, nsIFrame* aFrame);

  /**
   * Get the parent of aFrame. If aFrame is the root frame for a document,
   * and the document has a parent document in the same view hierarchy, then
   * we try to return the subdocumentframe in the parent document.
   * @param aCrossDocOffset [in/out] if non-null, then as we cross documents
   * an extra offset may be required and it will be added to aCrossDocOffset.
   * Be careful dealing with this extra offset as it is in app units of the
   * parent document, which may have a different app units per dev pixel ratio
   * than the child document.
   * Note that, while this function crosses document boundaries, it (naturally)
   * cannot cross process boundaries.
   */
  static nsIFrame* GetCrossDocParentFrameInProcess(
      const nsIFrame* aFrame, nsPoint* aCrossDocOffset = nullptr);

  /**
   * Does the same thing as GetCrossDocParentFrameInProcess().
   * The purpose of having two functions is to more easily track which call
   * sites have been audited to consider out-of-process iframes (bug 1599913).
   * Once all call sites have been audited, this function can be removed.
   */
  static nsIFrame* GetCrossDocParentFrame(const nsIFrame* aFrame,
                                          nsPoint* aCrossDocOffset = nullptr);

  /**
   * IsProperAncestorFrame checks whether aAncestorFrame is an ancestor
   * of aFrame and not equal to aFrame.
   * @param aCommonAncestor nullptr, or a common ancestor of aFrame and
   * aAncestorFrame. If non-null, this can bound the search and speed up
   * the function
   */
  static bool IsProperAncestorFrame(const nsIFrame* aAncestorFrame,
                                    const nsIFrame* aFrame,
                                    const nsIFrame* aCommonAncestor = nullptr);

  /**
   * Like IsProperAncestorFrame, but looks across document boundaries.
   *
   * Just like IsAncestorFrameCrossDoc, except that it returns false when
   * aFrame == aAncestorFrame.
   * TODO: Once after we fixed bug 1715932, this function should be removed.
   */
  static bool IsProperAncestorFrameCrossDoc(
      const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
      const nsIFrame* aCommonAncestor = nullptr);

  /**
   * Like IsProperAncestorFrame, but looks across document boundaries.
   *
   * Just like IsAncestorFrameCrossDoc, except that it returns false when
   * aFrame == aAncestorFrame.
   */
  static bool IsProperAncestorFrameCrossDocInProcess(
      const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
      const nsIFrame* aCommonAncestor = nullptr);

  /**
   * IsAncestorFrameCrossDoc checks whether aAncestorFrame is an ancestor
   * of aFrame or equal to aFrame, looking across document boundaries.
   * @param aCommonAncestor nullptr, or a common ancestor of aFrame and
   * aAncestorFrame. If non-null, this can bound the search and speed up
   * the function.
   *
   * Just like IsProperAncestorFrameCrossDoc, except that it returns true when
   * aFrame == aAncestorFrame.
   *
   * TODO: Bug 1700245, all call sites of this function will be eventually
   * replaced by IsAncestorFrameCrossDocInProcess.
   */
  static bool IsAncestorFrameCrossDoc(
      const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
      const nsIFrame* aCommonAncestor = nullptr);

  /**
   * IsAncestorFrameCrossDocInProcess checks whether aAncestorFrame is an
   * ancestor of aFrame or equal to aFrame, looking across document boundaries
   * in the same process.
   * @param aCommonAncestor nullptr, or a common ancestor of aFrame and
   * aAncestorFrame. If non-null, this can bound the search and speed up
   * the function.
   *
   * Just like IsProperAncestorFrameCrossDoc, except that it returns true when
   * aFrame == aAncestorFrame.
   *
   * NOTE: This function doesn't return true even if |aAncestorFrame| and
   * |aFrame| is in the same process but they are not directly connected, e.g.
   * both |aAncestorFrame| and |aFrame| in A domain documents, but there's
   * another an iframe document domain B, such as A1 -> B1 ->A2 document tree.
   */
  static bool IsAncestorFrameCrossDocInProcess(
      const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
      const nsIFrame* aCommonAncestor = nullptr);

  static mozilla::SideBits GetSideBitsForFixedPositionContent(
      const nsIFrame* aFixedPosFrame);

  /**
   * Get the scroll id for the root scrollframe of the presshell of the given
   * prescontext. Returns NULL_SCROLL_ID if it couldn't be found.
   */
  static ViewID ScrollIdForRootScrollFrame(nsPresContext* aPresContext);

  /**
   * GetScrollContainerFrameFor returns the scroll container frame for a
   * scrolled frame.
   */
  static mozilla::ScrollContainerFrame* GetScrollContainerFrameFor(
      const nsIFrame* aScrolledFrame);

  /**
   * GetNearestScrollableFrameForDirection locates the first ancestor of
   * aFrame (or aFrame itself) that is scrollable with overflow:scroll or
   * overflow:auto in the given direction and where either the scrollbar for
   * that direction is visible or the frame can be scrolled by some
   * positive amount in that direction.
   * The search extends across document boundaries.
   *
   * @param  aFrame the frame to start with
   * @param  aDirection Whether it's for horizontal or vertical scrolling.
   * @return the nearest scroll container frame or nullptr if not found
   */
  static mozilla::ScrollContainerFrame* GetNearestScrollableFrameForDirection(
      nsIFrame* aFrame, mozilla::layers::ScrollDirections aDirections);

  enum {
    /**
     * If the SCROLLABLE_SAME_DOC flag is set, then we only walk the frame tree
     * up to the root frame in the current document.
     */
    SCROLLABLE_SAME_DOC = 0x01,
    /**
     * If the SCROLLABLE_INCLUDE_HIDDEN flag is set then we allow
     * overflow:hidden scrollframes to be returned as scrollable frames.
     */
    SCROLLABLE_INCLUDE_HIDDEN = 0x02,
    /**
     * If the SCROLLABLE_ONLY_ASYNC_SCROLLABLE flag is set, then we only
     * want to match scrollable frames for which WantAsyncScroll() returns
     * true.
     */
    SCROLLABLE_ONLY_ASYNC_SCROLLABLE = 0x04,
    /**
     * If the SCROLLABLE_ALWAYS_MATCH_ROOT flag is set, then we will always
     * return the root scrollable frame for the root document (in the current
     * process) if we encounter it, whether or not it is async scrollable or
     * overflow: hidden.
     */
    SCROLLABLE_ALWAYS_MATCH_ROOT = 0x08,
    /**
     * If the SCROLLABLE_FIXEDPOS_FINDS_ROOT flag is set, then for fixed-pos
     * frames return the root scrollable frame for that document.
     */
    SCROLLABLE_FIXEDPOS_FINDS_ROOT = 0x10,
    /**
     * If the SCROLLABLE_STOP_AT_PAGE flag is set, then we stop searching
     * for scrollable ancestors when seeing a nsPageFrame.  This can be used
     * to avoid finding the viewport scroll frame in Print Preview (which
     * would be undesirable as a 'position:sticky' container for content).
     */
    SCROLLABLE_STOP_AT_PAGE = 0x20,
  };
  /**
   * GetNearestScrollContainerFrame locates the first ancestor of aFrame
   * (or aFrame itself) that is scrollable with overflow:scroll or
   * overflow:auto in some direction.
   *
   * @param  aFrame the frame to start with
   * @param  aFlags if SCROLLABLE_SAME_DOC is set, do not search across
   * document boundaries. If SCROLLABLE_INCLUDE_HIDDEN is set, include
   * frames scrollable with overflow:hidden.
   * @return the nearest scroll container frame or nullptr if not found
   */
  static mozilla::ScrollContainerFrame* GetNearestScrollContainerFrame(
      nsIFrame* aFrame, uint32_t aFlags = 0);

  /**
   * GetScrolledRect returns the range of allowable scroll offsets
   * for aScrolledFrame, assuming the scrollable overflow area is
   * aScrolledFrameOverflowArea and the scrollport size is aScrollPortSize.
   */
  static nsRect GetScrolledRect(nsIFrame* aScrolledFrame,
                                const nsRect& aScrolledFrameOverflowArea,
                                const nsSize& aScrollPortSize,
                                mozilla::StyleDirection);

  /**
   * HasPseudoStyle returns true if aContent (whose primary style
   * context is aComputedStyle) has the aPseudoElement pseudo-style
   * attached to it; returns false otherwise.
   *
   * @param aContent the content node we're looking at
   * @param aComputedStyle aContent's ComputedStyle
   * @param aPseudoElement the id of the pseudo style we care about
   * @param aPresContext the presentation context
   * @return whether aContent has aPseudoElement style attached to it
   */
  static bool HasPseudoStyle(nsIContent* aContent,
                             ComputedStyle* aComputedStyle,
                             mozilla::PseudoStyleType aPseudoElement,
                             nsPresContext* aPresContext);

  /**
   * If this frame is a placeholder for a float, then return the float,
   * otherwise return nullptr.  aPlaceholder must be a placeholder frame.
   */
  static nsIFrame* GetFloatFromPlaceholder(nsIFrame* aPlaceholder);

  // Combine aOrigClearType with aNewClearType, but limit the clear types
  // to UsedClear::Left, Right, Both.
  static mozilla::UsedClear CombineClearType(mozilla::UsedClear aOrigClearType,
                                             mozilla::UsedClear aNewClearType);

  /**
   * Get the coordinates of a given DOM mouse event, relative to a given
   * frame. Works only for DOM events generated by WidgetGUIEvents.
   * @param aDOMEvent the event
   * @param aFrame the frame to make coordinates relative to
   * @return the point, or (NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) if
   * for some reason the coordinates for the mouse are not known (e.g.,
   * the event is not a GUI event).
   */
  static nsPoint GetDOMEventCoordinatesRelativeTo(
      mozilla::dom::Event* aDOMEvent, nsIFrame* aFrame);

  /**
   * Get the coordinates of a given native mouse event, relative to a given
   * frame.
   * @param aEvent the event
   * @param aFrame the frame to make coordinates relative to
   * @return the point, or (NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) if
   * for some reason the coordinates for the mouse are not known (e.g.,
   * the event is not a GUI event).
   */
  static nsPoint GetEventCoordinatesRelativeTo(
      const mozilla::WidgetEvent* aEvent, RelativeTo aFrame);

  /**
   * Get the coordinates of a given point relative to an event and a
   * given frame.
   * @param aEvent the event
   * @param aPoint the point to get the coordinates relative to
   * @param aFrame the frame to make coordinates relative to
   * @return the point, or (NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) if
   * for some reason the coordinates for the mouse are not known (e.g.,
   * the event is not a GUI event).
   */
  static nsPoint GetEventCoordinatesRelativeTo(
      const mozilla::WidgetEvent* aEvent,
      const mozilla::LayoutDeviceIntPoint& aPoint, RelativeTo aFrame);

  /**
   * Get the coordinates of a given point relative to a widget and a
   * given frame.
   * @param aWidget the event src widget
   * @param aPoint the point to get the coordinates relative to
   * @param aFrame the frame to make coordinates relative to
   * @return the point, or (NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) if
   * for some reason the coordinates for the mouse are not known (e.g.,
   * the event is not a GUI event).
   */
  static nsPoint GetEventCoordinatesRelativeTo(
      nsIWidget* aWidget, const mozilla::LayoutDeviceIntPoint& aPoint,
      RelativeTo aFrame);

  /**
   * Get the popup frame of a given native mouse event.
   * @param aRootPresContext only check popups within aRootPresContext or a
   * descendant
   * @param aEvent  the event.
   * @return        Null, if there is no popup frame at the point, otherwise,
   *                returns top-most popup frame at the point.
   */
  static nsIFrame* GetPopupFrameForEventCoordinates(
      nsPresContext* aRootPresContext, const mozilla::WidgetEvent* aEvent);

  /**
   * Get the popup frame of a given point relative to a widget.
   * @param aRootPresContext only check popups within aRootPresContext or a
   * descendant
   * @param aEvent  the event.
   * @return        Null, if there is no popup frame at the point, otherwise,
   *                returns top-most popup frame at the point.
   */
  enum class GetPopupFrameForPointFlags : uint8_t {
    OnlyReturnFramesWithWidgets = 0x1,
  };
  static nsIFrame* GetPopupFrameForPoint(
      nsPresContext* aRootPresContext, nsIWidget* aWidget,
      const mozilla::LayoutDeviceIntPoint& aPoint,
      GetPopupFrameForPointFlags aFlags = GetPopupFrameForPointFlags(0));

  /**
   * Get container and offset if aEvent collapses Selection.
   * @param aPresShell      The PresShell handling aEvent.
   * @param aEvent          The event having coordinates where you want to
   *                        collapse Selection.
   * @param aContainer      Returns the container node at the point.
   *                        Set nullptr if you don't need this.
   * @param aOffset         Returns offset in the container node at the point.
   *                        Set nullptr if you don't need this.
   */
  MOZ_CAN_RUN_SCRIPT
  static void GetContainerAndOffsetAtEvent(PresShell* aPresShell,
                                           const mozilla::WidgetEvent* aEvent,
                                           nsIContent** aContainer,
                                           int32_t* aOffset);

  /**
   * Translate from widget coordinates to the view's coordinates
   * @param aPresContext the PresContext for the view
   * @param aWidget the widget
   * @param aPt the point relative to the widget
   * @param aView  view to which returned coordinates are relative
   * @return the point in the view's coordinates
   */
  static nsPoint TranslateWidgetToView(nsPresContext* aPresContext,
                                       nsIWidget* aWidget,
                                       const mozilla::LayoutDeviceIntPoint& aPt,
                                       nsView* aView);

  /**
   * Translate from view coordinates to the widget's coordinates.
   * @param aPresContext the PresContext for the view
   * @param aView the view
   * @param aPt the point relative to the view
   * @param aViewportType whether the point is in visual or layout coordinates
   * @param aWidget the widget to which returned coordinates are relative
   * @return the point in the view's coordinates
   */
  static mozilla::LayoutDeviceIntPoint TranslateViewToWidget(
      nsPresContext* aPresContext, nsView* aView, nsPoint aPt,
      ViewportType aViewportType, nsIWidget* aWidget);

  static mozilla::LayoutDeviceIntPoint WidgetToWidgetOffset(
      nsIWidget* aFromWidget, nsIWidget* aToWidget);

  enum class FrameForPointOption {
    /**
     * When set, paint suppression is ignored, so we'll return non-root page
     * elements even if paint suppression is stopping them from painting.
     */
    IgnorePaintSuppression = 1,
    /**
     * When set, clipping due to the root scroll frame (and any other viewport-
     * related clipping) is ignored.
     */
    IgnoreRootScrollFrame,
    /**
     * When set, return only content in the same document as aFrame.
     */
    IgnoreCrossDoc,
    /**
     * When set, return only content that is actually visible.
     */
    OnlyVisible,
  };

  struct FrameForPointOptions {
    using Bits = mozilla::EnumSet<FrameForPointOption>;

    Bits mBits;
    // If mBits contains OnlyVisible, what is the opacity threshold which we
    // consider "opaque enough" to clobber stuff underneath.
    float mVisibleThreshold;

    FrameForPointOptions(Bits aBits, float aVisibleThreshold)
        : mBits(aBits), mVisibleThreshold(aVisibleThreshold) {};

    MOZ_IMPLICIT FrameForPointOptions(Bits aBits)
        : FrameForPointOptions(aBits, 1.0f) {}

    FrameForPointOptions() : FrameForPointOptions(Bits()) {};
  };

  /**
   * Given aFrame, the root frame of a stacking context, find its descendant
   * frame under the point aPt that receives a mouse event at that location,
   * or nullptr if there is no such frame.
   * @param aPt the point, relative to the frame origin, in either visual
   *            or layout coordinates depending on aRelativeTo.mViewportType
   */
  static nsIFrame* GetFrameForPoint(RelativeTo aRelativeTo, nsPoint aPt,
                                    const FrameForPointOptions& = {});

  /**
   * Given aFrame, the root frame of a stacking context, find all descendant
   * frames under the area of a rectangle that receives a mouse event,
   * or nullptr if there is no such frame.
   * @param aRect the rect, relative to the frame origin, in either visual
   *              or layout coordinates depending on aRelativeTo.mViewportType
   * @param aOutFrames an array to add all the frames found
   */
  static nsresult GetFramesForArea(RelativeTo aRelativeTo, const nsRect& aRect,
                                   nsTArray<nsIFrame*>& aOutFrames,
                                   const FrameForPointOptions& = {});

  /**
   * Transform aRect relative to aFrame up to the coordinate system of
   * aAncestor. Computes the bounding-box of the true quadrilateral.
   * Pass non-null aPreservesAxisAlignedRectangles and it will be set to true if
   * we only need to use a 2d transform that PreservesAxisAlignedRectangles().
   * The corner positions of aRect are treated as meaningful even if aRect is
   * empty.
   *
   * |aMatrixCache| allows for optimizations in recomputing the same matrix over
   * and over. The argument can be one of the following values:
   *
   * nullptr (the default) - No optimization; the transform matrix is computed
   * on every call to this function.
   *
   * non-null pointer to an empty Maybe<Matrix4x4> - Upon return, the Maybe is
   * filled with the transform matrix that was computed. This can then be passed
   * in to subsequent calls with the same source and destination frames to avoid
   * recomputing the matrix.
   *
   * non-null pointer to a non-empty Matrix4x4 - The provided matrix will be
   * used as the transform matrix and applied to the rect.
   */
  static nsRect TransformFrameRectToAncestor(
      const nsIFrame* aFrame, const nsRect& aRect, const nsIFrame* aAncestor,
      bool* aPreservesAxisAlignedRectangles = nullptr,
      mozilla::Maybe<Matrix4x4Flagged>* aMatrixCache = nullptr,
      bool aStopAtStackingContextAndDisplayPortAndOOFFrame = false,
      nsIFrame** aOutAncestor = nullptr) {
    return TransformFrameRectToAncestor(
        aFrame, aRect, RelativeTo{aAncestor}, aPreservesAxisAlignedRectangles,
        aMatrixCache, aStopAtStackingContextAndDisplayPortAndOOFFrame,
        aOutAncestor);
  }
  static nsRect TransformFrameRectToAncestor(
      const nsIFrame* aFrame, const nsRect& aRect, RelativeTo aAncestor,
      bool* aPreservesAxisAlignedRectangles = nullptr,
      mozilla::Maybe<Matrix4x4Flagged>* aMatrixCache = nullptr,
      bool aStopAtStackingContextAndDisplayPortAndOOFFrame = false,
      nsIFrame** aOutAncestor = nullptr);

  /**
   * Gets the transform for aFrame relative to aAncestor. Pass null for
   * aAncestor to go up to the root frame. Including nsIFrame::IN_CSS_UNITS
   * flag in aFlags will return CSS pixels, by default it returns device
   * pixels.
   * More info can be found in nsIFrame::GetTransformMatrix.
   *
   * Some notes on the possible combinations of |aFrame.mViewportType| and
   * |aAncestor.mViewportType|:
   *
   * | aFrame.       | aAncestor.    | Notes
   * | mViewportType | mViewportType |
   * ==========================================================================
   * | Layout        | Layout        | Commonplace, when both source and target
   * |               |               | are inside zoom boundary.
   * |               |               |
   * |               |               | Could also happen in non-e10s setups
   * |               |               | when both source and target are outside
   * |               |               | the zoom boundary and the code is
   * |               |               | oblivious to the existence of a zoom
   * |               |               | boundary.
   * ==========================================================================
   * | Layout        | Visual        | Commonplace, used when hit testing visual
   * |               |               | coordinates (e.g. coming from user input
   * |               |               | events). We expected to encounter a
   * |               |               | zoomed content root during traversal and
   * |               |               | apply a layout-to-visual transform.
   * ==========================================================================
   * | Visual        | Layout        | Should never happen, will assert.
   * ==========================================================================
   * | Visual        | Visual        | In e10s setups, should only happen if
   * |               |               | aFrame and aAncestor are both the
   * |               |               | RCD viewport frame.
   * |               |               |
   * |               |               | In non-e10s setups, could happen with
   * |               |               | different frames if they are both
   * |               |               | outside the zoom boundary.
   * ==========================================================================
   */
  static Matrix4x4Flagged GetTransformToAncestor(
      RelativeTo aFrame, RelativeTo aAncestor, uint32_t aFlags = 0,
      nsIFrame** aOutAncestor = nullptr);

  /**
   * Gets the scale factors of the transform for aFrame relative to the root
   * frame if this transform can be drawn 2D, or the identity scale factors
   * otherwise.
   */
  static MatrixScales GetTransformToAncestorScale(const nsIFrame* aFrame);

  /**
   * Gets the scale factors of the transform for aFrame relative to the root
   * frame if this transform is 2D, or the identity scale factors otherwise.
   * If some frame on the path from aFrame to the display root frame may have an
   * animated scale, returns the identity scale factors.
   */
  static MatrixScales GetTransformToAncestorScaleExcludingAnimated(
      nsIFrame* aFrame);

  /**
   * Gets a scale that includes CSS transforms in this process as well as the
   * transform to ancestor scale passed down from our direct ancestor process
   * (which includes any enclosing CSS transforms and resolution). Note: this
   * does not include any resolution in the current process (this is on purpose
   * because that is what the transform to ancestor field on FrameMetrics needs,
   * see its definition for explanation as to why). This is the transform to
   * ancestor scale to set on FrameMetrics.
   */
  static mozilla::ParentLayerToScreenScale2D
  GetTransformToAncestorScaleCrossProcessForFrameMetrics(
      const nsIFrame* aFrame);

  /**
   * Find the nearest common ancestor frame for aFrame1 and aFrame2. The
   * ancestor frame could be cross-doc.
   */
  static const nsIFrame* FindNearestCommonAncestorFrame(
      const nsIFrame* aFrame1, const nsIFrame* aFrame2);

  /**
   * Find the nearest common ancestor frame for aFrame1 and aFrame2, assuming
   * that they are within the same block.
   *
   * Returns null if they are not within the same block.
   */
  static const nsIFrame* FindNearestCommonAncestorFrameWithinBlock(
      const nsTextFrame* aFrame1, const nsTextFrame* aFrame2);

  /**
   * Whether author-specified borders / backgrounds disable theming for a given
   * appearance value.
   */
  static bool AuthorSpecifiedBorderBackgroundDisablesTheming(
      mozilla::StyleAppearance);

  /**
   * Transforms a list of CSSPoints from aFromFrame to aToFrame, taking into
   * account all relevant transformations on the frames up to (but excluding)
   * their nearest common ancestor.
   * If we encounter a transform that we need to invert but which is
   * non-invertible, we return NONINVERTIBLE_TRANSFORM. If the frames have
   * no common ancestor, we return NO_COMMON_ANCESTOR.
   * If this returns TRANSFORM_SUCCEEDED, the points in aPoints are transformed
   * in-place, otherwise they are untouched.
   */
  enum TransformResult {
    TRANSFORM_SUCCEEDED,
    NO_COMMON_ANCESTOR,
    NONINVERTIBLE_TRANSFORM
  };
  static TransformResult TransformPoints(RelativeTo aFromFrame,
                                         RelativeTo aToFrame,
                                         uint32_t aPointCount,
                                         CSSPoint* aPoints);

  /**
   * Same as above function, but transform points in app units and
   * handle 1 point per call.
   */
  static TransformResult TransformPoint(RelativeTo aFromFrame,
                                        RelativeTo aToFrame, nsPoint& aPoint);

  /**
   * Transforms a rect from aFromFrame to aToFrame. In app units.
   * Returns the bounds of the actual rect if the transform requires rotation
   * or anything complex like that.
   */
  static TransformResult TransformRect(const nsIFrame* aFromFrame,
                                       const nsIFrame* aToFrame, nsRect& aRect);

  /**
   * Converts app units to pixels (with optional snapping) and appends as a
   * translation to aTransform.
   */
  static void PostTranslate(Matrix4x4& aTransform, const nsPoint& aOrigin,
                            float aAppUnitsPerPixel, bool aRounded);

  /*
   * Whether the frame should snap to grid. This will end up being passed
   * as the aRounded parameter in PostTranslate above. SVG frames should
   * not have their translation rounded.
   */
  static bool ShouldSnapToGrid(const nsIFrame* aFrame);

  /**
   * Get the border-box of aElement's primary frame, transformed it to be
   * relative to aFrame.
   */
  static nsRect GetRectRelativeToFrame(mozilla::dom::Element* aElement,
                                       nsIFrame* aFrame);

  /**
   * Returns true if aRect with border inflation of size aInflateSize contains
   * aPoint.
   */
  static bool ContainsPoint(const nsRect& aRect, const nsPoint& aPoint,
                            nscoord aInflateSize);

  /**
   * Clamp aRect relative to aFrame to the scroll frames boundary searching from
   * aFrame.
   */
  static nsRect ClampRectToScrollFrames(nsIFrame* aFrame, const nsRect& aRect);

  /**
   * Given a point in the global coordinate space, returns that point expressed
   * in the coordinate system of aFrame.  This effectively inverts all
   * transforms between this point and the root frame.
   *
   * @param aFromType Specifies whether |aPoint| is in layout or visual
   * coordinates.
   * @param aFrame The frame that acts as the coordinate space container.
   * @param aPoint The point, in global layout or visual coordinates (as per
   * |aFromType|, to get in the frame-local space.
   * @return aPoint, expressed in aFrame's canonical coordinate space.
   */
  static nsPoint TransformRootPointToFrame(ViewportType aFromType,
                                           RelativeTo aFrame,
                                           const nsPoint& aPoint) {
    return TransformAncestorPointToFrame(aFrame, aPoint,
                                         RelativeTo{nullptr, aFromType});
  }

  /**
   * Transform aPoint relative to aAncestor down to the coordinate system of
   * aFrame.
   */
  static nsPoint TransformAncestorPointToFrame(RelativeTo aFrame,
                                               const nsPoint& aPoint,
                                               RelativeTo aAncestor);

  static nsPoint TransformFramePointToRoot(ViewportType aToType,
                                           RelativeTo aFromFrame,
                                           const nsPoint& aPoint);

  /**
   * Helper function that, given a rectangle and a matrix, returns the smallest
   * rectangle containing the image of the source rectangle.
   *
   * @param aBounds The rectangle to transform.
   * @param aMatrix The matrix to transform it with.
   * @param aFactor The number of app units per graphics unit.
   * @return The smallest rect that contains the image of aBounds.
   */
  static nsRect MatrixTransformRect(const nsRect& aBounds,
                                    const Matrix4x4& aMatrix, float aFactor);
  static nsRect MatrixTransformRect(const nsRect& aBounds,
                                    const Matrix4x4Flagged& aMatrix,
                                    float aFactor);

  /**
   * Helper function that, given a point and a matrix, returns the image
   * of that point under the matrix transform.
   *
   * @param aPoint The point to transform.
   * @param aMatrix The matrix to transform it with.
   * @param aFactor The number of app units per graphics unit.
   * @return The image of the point under the transform.
   */
  static nsPoint MatrixTransformPoint(const nsPoint& aPoint,
                                      const Matrix4x4& aMatrix, float aFactor);

  /**
   * Given a graphics rectangle in graphics space, return a rectangle in
   * app space that contains the graphics rectangle, rounding out as necessary.
   *
   * @param aRect The graphics rect to round outward.
   * @param aFactor The number of app units per graphics unit.
   * @return The smallest rectangle in app space that contains aRect.
   */
  template <typename T>
  static nsRect RoundGfxRectToAppRect(const T& aRect, const float aFactor);

  /**
   * Returns a subrectangle of aContainedRect that is entirely inside the
   * rounded rect. Complex cases are handled conservatively by returning a
   * smaller rect than necessary.
   */
  static nsRegion RoundedRectIntersectRect(const nsRect& aRoundedRect,
                                           const nscoord aRadii[8],
                                           const nsRect& aContainedRect);
  static nsIntRegion RoundedRectIntersectIntRect(
      const nsIntRect& aRoundedRect, const RectCornerRadii& aCornerRadii,
      const nsIntRect& aContainedRect);

  /**
   * Return whether any part of aTestRect is inside of the rounded
   * rectangle formed by aBounds and aRadii (which are indexed by the
   * enum HalfCorner constants in gfx/2d/Types.h). This is precise.
   */
  static bool RoundedRectIntersectsRect(const nsRect& aRoundedRect,
                                        const nscoord aRadii[8],
                                        const nsRect& aTestRect);

  enum class PaintFrameFlags : uint32_t {
    InTransform = 0x01,
    SyncDecodeImages = 0x02,
    WidgetLayers = 0x04,
    IgnoreSuppression = 0x08,
    DocumentRelative = 0x10,
    HideCaret = 0x20,
    ToWindow = 0x40,
    ExistingTransaction = 0x80,
    ForWebRender = 0x100,
    UseHighQualityScaling = 0x200,
    ResetViewportScrolling = 0x400,
    CompositeOffscreen = 0x800,
  };

  /**
   * Given aFrame, the root frame of a stacking context, paint it and its
   * descendants to aRenderingContext.
   * @param aRenderingContext a rendering context translated so that (0,0)
   * is the origin of aFrame; for best results, (0,0) should transform
   * to pixel-aligned coordinates. This can be null, in which case
   * aFrame must be a "display root" (root frame for a root document,
   * or the root of a popup) with an associated widget and we draw using
   * the layer manager for the frame's widget.
   * @param aDirtyRegion the region that must be painted, in the coordinates
   * of aFrame.
   * @param aBackstop paint the dirty area with this color before drawing
   * the actual content; pass NS_RGBA(0,0,0,0) to draw no background.
   * @param aBuilderMode Passed through to the display-list builder.
   * @param aFlags if PAINT_IN_TRANSFORM is set, then we assume
   * this is inside a transform or SVG foreignObject. If
   * PAINT_SYNC_DECODE_IMAGES is set, we force synchronous decode on all
   * images. If PAINT_WIDGET_LAYERS is set, aFrame must be a display root,
   * and we will use the frame's widget's layer manager to paint
   * even if aRenderingContext is non-null. This is useful if you want
   * to force rendering to use the widget's layer manager for testing
   * or speed. PAINT_WIDGET_LAYERS must be set if aRenderingContext is null.
   * If PAINT_DOCUMENT_RELATIVE is used, the visible region is interpreted
   * as being relative to the document (normally it's relative to the CSS
   * viewport) and the document is painted as if no scrolling has occured.
   * Only considered if PresShell::IgnoringViewportScrolling is true.
   * If ResetViewportScrolling is used, then the root scroll frame's scroll
   * position is set to 0 during painting, so that position:fixed elements
   * are drawn in their initial position.
   * PAINT_TO_WINDOW sets painting to window to true on the display list
   * builder even if we can't tell that we are painting to the window.
   * If PAINT_EXISTING_TRANSACTION is set, then BeginTransaction() has already
   * been called on aFrame's widget's layer manager and should not be
   * called again.
   * If PAINT_COMPRESSED is set, the FrameLayerBuilder should be set to
   * compressed mode to avoid short cut optimizations.
   *
   * So there are three possible behaviours:
   * 1) PAINT_WIDGET_LAYERS is set and aRenderingContext is null; we paint
   * by calling BeginTransaction on the widget's layer manager.
   * 2) PAINT_WIDGET_LAYERS is set and aRenderingContext is non-null; we
   * paint by calling BeginTransactionWithTarget on the widget's layer
   * manager.
   * 3) PAINT_WIDGET_LAYERS is not set and aRenderingContext is non-null;
   * we paint by construct a BasicLayerManager and calling
   * BeginTransactionWithTarget on it. This is desirable if we're doing
   * something like drawWindow in a mode where what gets rendered doesn't
   * necessarily correspond to what's visible in the window; we don't
   * want to mess up the widget's layer tree.
   */
  static void PaintFrame(gfxContext* aRenderingContext, nsIFrame* aFrame,
                         const nsRegion& aDirtyRegion, nscolor aBackstop,
                         nsDisplayListBuilderMode aBuilderMode,
                         PaintFrameFlags aFlags = PaintFrameFlags(0));

  /**
   * Uses a binary search for find where the cursor falls in the line of text
   * It also keeps track of the part of the string that has already been
   * measured so it doesn't have to keep measuring the same text over and over.
   *
   * @param "aBaseWidth" contains the width in twips of the portion
   * of the text that has already been measured, and aBaseInx contains
   * the index of the text that has already been measured.
   *
   * @param aTextWidth returns (in twips) the length of the text that falls
   * before the cursor aIndex contains the index of the text where the cursor
   * falls.
   */
  static bool BinarySearchForPosition(DrawTarget* aDrawTarget,
                                      nsFontMetrics& aFontMetrics,
                                      const char16_t* aText, int32_t aBaseWidth,
                                      int32_t aBaseInx, int32_t aStartInx,
                                      int32_t aEndInx, int32_t aCursorPos,
                                      int32_t& aIndex, int32_t& aTextWidth);

  class BoxCallback {
   public:
    BoxCallback() = default;
    virtual void AddBox(nsIFrame* aFrame) = 0;
    bool mIncludeCaptionBoxForTable = true;
    // Whether we are in a continuation or ib-split-sibling of the target we're
    // measuring. This is useful because if we know we're in the target subtree
    // and measuring against it we can avoid finding the common ancestor.
    bool mInTargetContinuation = false;
  };
  /**
   * Collect all CSS boxes associated with aFrame and its
   * continuations, "drilling down" through table wrapper frames and
   * some anonymous blocks since they're not real CSS boxes.
   * If aFrame is null, no boxes are returned.
   * SVG frames return a single box, themselves.
   */
  static void GetAllInFlowBoxes(nsIFrame* aFrame, BoxCallback* aCallback);

  /**
   * Like GetAllInFlowBoxes, but doesn't include continuations.
   */
  static void AddBoxesForFrame(nsIFrame* aFrame, BoxCallback* aCallback);

  /**
   * Find the first frame descendant of aFrame (including aFrame) which is
   * not an anonymous frame that getBoxQuads/getClientRects should ignore.
   */
  static nsIFrame* GetFirstNonAnonymousFrame(nsIFrame* aFrame);

  struct RectAccumulator : public mozilla::RectCallback {
    nsRect mResultRect;
    nsRect mFirstRect;
    bool mSeenFirstRect;

    RectAccumulator();

    virtual void AddRect(const nsRect& aRect) override;
  };

  struct RectListBuilder : public mozilla::RectCallback {
    DOMRectList* mRectList;

    explicit RectListBuilder(DOMRectList* aList);
    virtual void AddRect(const nsRect& aRect) override;
  };

  static nsIFrame* GetContainingBlockForClientRect(nsIFrame* aFrame);

  /**
   * Collect all CSS boxes (content, padding, border, or margin) associated
   * with aFrame and its continuations, "drilling down" through table wrapper
   * frames and some anonymous blocks since they're not real CSS boxes.
   *
   * The boxes are positioned relative to aRelativeTo (taking scrolling
   * into account) and passed to the callback in frame-tree order.
   * If aFrame is null, no boxes are returned.
   *
   * For SVG frames, returns one rectangle, the bounding box.
   *
   * If aFlags includes 'AccountForTransforms', then when converting the boxes
   * into aRelativeTo coordinates, transforms (including CSS and SVG transforms)
   * are taken into account.
   *
   * If aFlags includes one of 'UseContentBox', 'UsePaddingBox', 'UseMarginBox',
   * or 'UseMarginBoxWithAutoResolvedAsZero', the corresponding type of box is
   * used. Otherwise (by default), the border box is used. Note that these "Box"
   * flags are meant to be mutually exclusive, though we don't enforce that. If
   * multiple "Box" flags are used, we'll gracefully just use the first one in
   * the order of the enum.
   */
  enum class GetAllInFlowRectsFlag : uint8_t {
    AccountForTransforms,
    UseContentBox,
    UsePaddingBox,
    UseMarginBox,
    // Similar to UseMarginBox, but the 'auto' margins are resolved as zero.
    UseMarginBoxWithAutoResolvedAsZero,
  };
  using GetAllInFlowRectsFlags = mozilla::EnumSet<GetAllInFlowRectsFlag>;
  static void GetAllInFlowRects(nsIFrame* aFrame, const nsIFrame* aRelativeTo,
                                mozilla::RectCallback* aCallback,
                                GetAllInFlowRectsFlags aFlags = {});

  static void GetAllInFlowRectsAndTexts(
      nsIFrame* aFrame, const nsIFrame* aRelativeTo,
      mozilla::RectCallback* aCallback,
      mozilla::dom::Sequence<nsString>* aTextList,
      GetAllInFlowRectsFlags aFlags = {});

  /**
   * Computes the union of all rects returned by GetAllInFlowRects. If
   * the union is empty, returns the first rect.
   *
   * See GetAllInFlowRects() documentation for the meaning of aRelativeTo and
   * aFlags.
   */
  static nsRect GetAllInFlowRectsUnion(nsIFrame* aFrame,
                                       const nsIFrame* aRelativeTo,
                                       GetAllInFlowRectsFlags aFlags = {});

  enum { EXCLUDE_BLUR_SHADOWS = 0x01 };
  /**
   * Takes a text-shadow array from the style properties of a given nsIFrame and
   * computes the union of those shadows along with the given initial rect.
   * If there are no shadows, the initial rect is returned.
   */
  static nsRect GetTextShadowRectsUnion(const nsRect& aTextAndDecorationsRect,
                                        nsIFrame* aFrame, uint32_t aFlags = 0);

  /**
   * Computes the destination rect that a given replaced element should render
   * into, based on its CSS 'object-fit' and 'object-position' properties.
   *
   * @param aConstraintRect The constraint rect that we have at our disposal,
   *                        which would e.g. be exactly filled by the image
   *                        if we had "object-fit: fill".
   * @param aIntrinsicSize The replaced content's intrinsic size, as reported
   *                       by nsIFrame::GetIntrinsicSize().
   * @param aIntrinsicRatio The replaced content's intrinsic ratio, as reported
   *                        by nsIFrame::GetIntrinsicRatio().
   * @param aStylePos The nsStylePosition struct that contains the 'object-fit'
   *                  and 'object-position' values that we should rely on.
   *                  (This should usually be the nsStylePosition for the
   *                  replaced element in question, but not always. For
   *                  example, a <video>'s poster-image has a dedicated
   *                  anonymous element & child-frame, but we should still use
   *                  the <video>'s 'object-fit' and 'object-position' values.)
   * @param aAnchorPoint [out] A point that should be pixel-aligned by functions
   *                           like nsLayoutUtils::DrawImage. See documentation
   *                           in nsCSSRendering.h for ComputeObjectAnchorPoint.
   * @return The nsRect into which we should render the replaced content (using
   *         the same coordinate space as the passed-in aConstraintRect).
   */
  static nsRect ComputeObjectDestRect(const nsRect& aConstraintRect,
                                      const IntrinsicSize& aIntrinsicSize,
                                      const AspectRatio& aIntrinsicRatio,
                                      const nsStylePosition* aStylePos,
                                      nsPoint* aAnchorPoint = nullptr);

  /**
   * Get the font metrics corresponding to the frame's style data.
   * @param aFrame the frame
   * @param aSizeInflation number to multiply font size by
   */
  static already_AddRefed<nsFontMetrics> GetFontMetricsForFrame(
      const nsIFrame* aFrame, float aSizeInflation);

  static already_AddRefed<nsFontMetrics> GetInflatedFontMetricsForFrame(
      const nsIFrame* aFrame) {
    return GetFontMetricsForFrame(aFrame, FontSizeInflationFor(aFrame));
  }

  /**
   * Get the font metrics corresponding to the given style data.
   * @param aComputedStyle the style data
   * @param aSizeInflation number to multiply font size by
   */
  static already_AddRefed<nsFontMetrics> GetFontMetricsForComputedStyle(
      const ComputedStyle* aComputedStyle, nsPresContext* aPresContext,
      float aSizeInflation = 1.0f,
      uint8_t aVariantWidth = NS_FONT_VARIANT_WIDTH_NORMAL);

  /**
   * Get the font metrics of emphasis marks corresponding to the given
   * style data. The result is same as GetFontMetricsForComputedStyle
   * except that the font size is scaled down to 50%.
   * @param aComputedStyle the style data
   * @param aInflation number to multiple font size by
   */
  static already_AddRefed<nsFontMetrics> GetFontMetricsOfEmphasisMarks(
      ComputedStyle* aComputedStyle, nsPresContext* aPresContext,
      float aInflation) {
    return GetFontMetricsForComputedStyle(aComputedStyle, aPresContext,
                                          aInflation * 0.5f);
  }

  /**
   * Find the immediate child of aParent whose frame subtree contains
   * aDescendantFrame. Returns null if aDescendantFrame is not a descendant
   * of aParent.
   */
  static nsIFrame* FindChildContainingDescendant(nsIFrame* aParent,
                                                 nsIFrame* aDescendantFrame);

  /**
   * Find the nearest ancestor that's a block
   */
  static nsBlockFrame* FindNearestBlockAncestor(nsIFrame* aFrame);

  /*
   * Whether the frame is an nsBlockFrame which is not a wrapper block.
   */
  static bool IsNonWrapperBlock(nsIFrame* aFrame);

  /**
   * If aFrame is an out of flow frame, return its placeholder, otherwise
   * return its parent.
   */
  static nsIFrame* GetParentOrPlaceholderFor(const nsIFrame* aFrame);

  /**
   * If aFrame is an out of flow frame, return its placeholder, otherwise
   * return its (possibly cross-doc) parent.
   */
  static nsIFrame* GetParentOrPlaceholderForCrossDoc(const nsIFrame* aFrame);

  /**
   * Returns the frame that would act as the parent of aFrame when
   * descending through the frame tree in display list building.
   * Usually the same as GetParentOrPlaceholderForCrossDoc, except
   * that pushed floats are treated as children of their containing
   * block.
   */
  static nsIFrame* GetDisplayListParent(nsIFrame* aFrame);

  /**
   * Get a frame's previous continuation, or, if it doesn't have one, its
   * previous block-in-inline-split sibling.
   */
  static nsIFrame* GetPrevContinuationOrIBSplitSibling(const nsIFrame* aFrame);

  /**
   * Get a frame's next continuation, or, if it doesn't have one, its
   * block-in-inline-split sibling.
   */
  static nsIFrame* GetNextContinuationOrIBSplitSibling(const nsIFrame* aFrame);

  /**
   * Get the first frame in the continuation-plus-ib-split-sibling chain
   * containing aFrame.
   */
  static nsIFrame* FirstContinuationOrIBSplitSibling(const nsIFrame* aFrame);

  /**
   * Get the last frame in the continuation-plus-ib-split-sibling chain
   * containing aFrame.
   */
  static nsIFrame* LastContinuationOrIBSplitSibling(const nsIFrame* aFrame);

  /**
   * Is FirstContinuationOrIBSplitSibling(aFrame) going to return
   * aFrame?
   */
  static bool IsFirstContinuationOrIBSplitSibling(const nsIFrame* aFrame);

  /**
   * Check whether aFrame is a part of the scrollbar or scrollcorner of
   * the root content.
   * @param aFrame the checking frame.
   * @return true if the frame is a part of the scrollbar or scrollcorner of
   *         the root content.
   */
  static bool IsViewportScrollbarFrame(nsIFrame* aFrame);

  /**
   * Use only for paddings / widths / heights, since it clamps negative calc()
   * to 0.
   */
  template <typename LengthPercentageLike>
  static mozilla::Maybe<nscoord> GetAbsoluteSize(
      const LengthPercentageLike& aSize) {
    if (!aSize.ConvertsToLength()) {
      return mozilla::Nothing();
    }
    return mozilla::Some(std::max(0, aSize.ToLength()));
  }

  /**
   * Get the contribution of aFrame to its containing block's intrinsic
   * size for the given physical axis.  This considers the child's intrinsic
   * width, its 'width', 'min-width', and 'max-width' properties (or 'height'
   * variations if that's what matches aAxis) and its padding, border and margin
   * in the corresponding dimension.
   * @param aPercentageBasis an optional percentage basis (in aFrame's WM).
   *   If the basis is indefinite in a given axis, pass a size with
   *   NS_UNCONSTRAINEDSIZE in that component.
   *   If you pass Nothing() a percentage basis will be calculated from aFrame's
   *   ancestors' computed size in the relevant axis, if needed.
   * @param aMarginBoxMinSizeClamp make the result fit within this margin-box
   * size by reducing the *content size* (flooring at zero).  This is used for:
   * https://drafts.csswg.org/css-grid/#min-size-auto
   * @param aSizeOverrides optional override values for size properties, which
   * this function will use internally instead of the actual property values.
   */
  enum {
    IGNORE_PADDING = 0x01,
    BAIL_IF_REFLOW_NEEDED = 0x02,  // returns NS_INTRINSIC_ISIZE_UNKNOWN if so
    MIN_INTRINSIC_ISIZE = 0x04,  // use min-width/height instead of width/height
  };
  static nscoord IntrinsicForAxis(
      mozilla::PhysicalAxis aAxis, gfxContext* aRenderingContext,
      nsIFrame* aFrame, mozilla::IntrinsicISizeType aType,
      const mozilla::Maybe<LogicalSize>& aPercentageBasis = mozilla::Nothing(),
      uint32_t aFlags = 0, nscoord aMarginBoxMinSizeClamp = NS_MAXSIZE,
      const mozilla::StyleSizeOverrides& aSizeOverrides = {});
  /**
   * Calls IntrinsicForAxis with aFrame's parent's inline physical axis.
   */
  static nscoord IntrinsicForContainer(
      gfxContext* aRenderingContext, nsIFrame* aFrame,
      mozilla::IntrinsicISizeType aType,
      const mozilla::Maybe<LogicalSize>& aPercentageBasis = mozilla::Nothing(),
      uint32_t aFlags = 0,
      const mozilla::StyleSizeOverrides& aSizeOverrides = {});

  /**
   * Get the definite size contribution of aFrame for the given physical axis.
   * This considers the child's 'min-width' property (or 'min-height' if the
   * given axis is vertical), and its padding, border, and margin in the
   * corresponding dimension.  If the 'min-' property is 'auto' (and 'overflow'
   * is 'visible') and the corresponding 'width'/'height' is definite it returns
   * the "specified size" for:
   * https://drafts.csswg.org/css-grid/#min-size-auto
   * Note that the "transferred size" is not handled here; use IntrinsicForAxis.
   * Note that any percentage in 'width'/'height' makes it count as indefinite.
   * If the 'min-' property is 'auto' and 'overflow' is not 'visible', then it
   * calculates the result as if the 'min-' computed value is zero.
   * Otherwise, return NS_UNCONSTRAINEDSIZE.
   *
   * @param aPercentageBasis the percentage basis (in aFrame's WM).
   *   Pass NS_UNCONSTRAINEDSIZE if the basis is indefinite in either/both axes.
   * @note this behavior is specific to Grid/Flexbox (currently) so aFrame
   * should be a grid/flex item.
   */
  static nscoord MinSizeContributionForAxis(mozilla::PhysicalAxis aAxis,
                                            gfxContext* aRC, nsIFrame* aFrame,
                                            mozilla::IntrinsicISizeType aType,
                                            const LogicalSize& aPercentageBasis,
                                            uint32_t aFlags = 0);

  /*
   * Convert LengthPercentage to nscoord when percentages depend on the
   * containing block size.
   * @param aPercentBasis The width or height of the containing block
   * (whichever the client wants to use for resolving percentages).
   */
  static nscoord ComputeCBDependentValue(nscoord aPercentBasis,
                                         const LengthPercentage& aCoord) {
    NS_ASSERTION(aPercentBasis != NS_UNCONSTRAINEDSIZE || !aCoord.HasPercent(),
                 "Have unconstrained percentage basis when percentage "
                 "resolution needed; this should only result from very "
                 "large sizes, not attempts at intrinsic size calculation");
    return aCoord.Resolve(aPercentBasis);
  }
  static nscoord ComputeCBDependentValue(nscoord aPercentBasis,
                                         const LengthPercentageOrAuto& aCoord) {
    if (aCoord.IsAuto()) {
      return 0;
    }
    return ComputeCBDependentValue(aPercentBasis, aCoord.AsLengthPercentage());
  }

  static nscoord ComputeCBDependentValue(nscoord aPercentBasis,
                                         const AnchorResolvedInset& aInset) {
    if (aInset->IsAuto()) {
      // Callers are assumed to have handled other cases already.
      return 0;
    }
    NS_ASSERTION(aPercentBasis != NS_UNCONSTRAINEDSIZE || !aInset->HasPercent(),
                 "Have unconstrained percentage basis when percentage "
                 "resolution needed; this should only result from very "
                 "large sizes, not attempts at intrinsic size calculation");
    return aInset->AsLengthPercentage().Resolve(aPercentBasis);
  }

  static nscoord ComputeCBDependentValue(nscoord aPercentBasis,
                                         const AnchorResolvedMargin& aMargin) {
    if (!aMargin->IsLengthPercentage()) {
      MOZ_ASSERT(aMargin->IsAuto(), "Didn't resolve anchor functions first?");
      return 0;
    }
    return ComputeCBDependentValue(aPercentBasis,
                                   aMargin->AsLengthPercentage());
  }

  static nscoord ComputeBSizeValue(nscoord aContainingBlockBSize,
                                   nscoord aContentEdgeToBoxSizingBoxEdge,
                                   const LengthPercentage& aCoord) {
    MOZ_ASSERT(aContainingBlockBSize != nscoord_MAX || !aCoord.HasPercent(),
               "caller must deal with %% of unconstrained block-size");

    nscoord result = aCoord.Resolve(aContainingBlockBSize);
    // Clamp calc(), and the subtraction for box-sizing.
    return std::max(0, result - aContentEdgeToBoxSizingBoxEdge);
  }

  // Wrapper for ComputeBSizeValue that also handles 'stretch':
  template <typename SizeOrMaxSize>
  static nscoord ComputeBSizeValueHandlingStretch(
      nscoord aContainingBlockBSize, nscoord aMargin, nscoord aBorderPadding,
      nscoord aContentEdgeToBoxSizingBoxEdge, const SizeOrMaxSize& aSize) {
    if (aSize.BehavesLikeStretchOnBlockAxis()) {
      // Note: we don't need to worry about accounting for "box-sizing" when
      // resolving 'stretch' here. This function unconditionally returns a
      // content-box size, and the content-box size of a stretched element is
      // the same regardless of whether whether the author is conceptually
      // asking us to stretch the content box vs. the border-box.
      return ComputeStretchContentBoxBSize(aContainingBlockBSize, aMargin,
                                           aBorderPadding);
    }
    return ComputeBSizeValue(aContainingBlockBSize,
                             aContentEdgeToBoxSizingBoxEdge,
                             aSize.AsLengthPercentage());
  }

  /**
   * Returns the size that an element's box should take on, in order for its
   * margin-box to exactly reach a particular larger size (e.g. to fill its
   * containing block in a particular axis). The box in question can be either
   * the content-box or the border-box, determined by the aBoxSizing param.
   *
   * This function can be used to resolve the "stretch" size for the child box,
   * for example: https://drafts.csswg.org/css-sizing-4/#stretch-fit-sizing
   *
   * The returned value is floored at 0.
   *
   * There's a version for ISize and BSize; the only difference is that the
   * BSize version has an assertion to be sure that we're not inadvertently
   * doing arithmetic with the NS_UNCONSTRAINEDSIZE sentinel value in that
   * axis. (This sentinel has special meaning as a block-axis size but not as
   * an inline-axis size; hence, the assertion only makes sense for block-axis
   * sizes.)
   *
   * TODO(dholbert): Maybe do minor refactors to use this where we resolve
   * 'stretch' alignment in various places, if that feels useful?
   *
   * @param aSizeToFill
   *   The size that the child's margin-box should fill, in the axis in
   *   question -- e.g. the containing block size.  Assumed to be a constrained
   *   size; this function doesn't have any special treatment to handle the
   *   case where this is unconstrained.
   *
   * @param aMargin
   *   The sum of the child box's margins in the axis in question (using zero
   *   for any margins that should be ignored in computing the 'stretch' size;
   *   see bug 1932993 for one special case where this should happen).
   *
   * @param aBorderPadding
   *   The sum of the child box's border and padding in the axis in question.
   *
   * @param aBoxSizing
   *   The StyleBoxSizing enum that represents the box that the caller wants to
   *   resolve a size for. NOTE: it may or may not be appropriate to actually
   *   pass the true specified 'box-sizing' value for this param; it depends on
   *   what box the caller is trying to actually resolve. In many cases, we
   *   internally work with variables that unconditionally represent a
   *   content-box size, regardless of the 'box-sizing' value; and for those
   *   cases, it would be appropriate to unconditionally pass
   *   StyleBoxSizing::Content to this function, or to just use the
   *   convenience-wrapper that has "ContentBox" in the function name.
   */
  static inline nscoord ComputeStretchBSize(
      nscoord aSizeToFill, nscoord aMargin, nscoord aBorderPadding,
      mozilla::StyleBoxSizing aBoxSizing) {
    NS_ASSERTION(aSizeToFill != NS_UNCONSTRAINEDSIZE,
                 "We don't handle situations with unconstrained "
                 "aSizeToFill; caller should handle that!");
    nscoord stretchSize = aSizeToFill - aMargin;
    if (aBoxSizing == mozilla::StyleBoxSizing::Content) {
      stretchSize -= aBorderPadding;
    }
    return std::max(0, stretchSize);
  }
  // Convenience wrapper that assumes we're resolving the content-box size:
  static inline nscoord ComputeStretchContentBoxBSize(nscoord aSizeToFill,
                                                      nscoord aMargin,
                                                      nscoord aBorderPadding) {
    return ComputeStretchBSize(aSizeToFill, aMargin, aBorderPadding,
                               mozilla::StyleBoxSizing::Content);
  }
  // Similar to the above convenience-wrapper, but now for inline-axis.
  // TODO(dholbert): would it be useful to add a box-sizing-aware version of
  // this API for the inline axis too, like we've got for the block axis?
  static inline nscoord ComputeStretchContentBoxISize(nscoord aSizeToFill,
                                                      nscoord aMargin,
                                                      nscoord aBorderPadding) {
    return std::max(0, aSizeToFill - aMargin - aBorderPadding);
  }

  /**
   * The "extremum length" values (see ExtremumLength) that return true from
   * 'BehavesLikeInitialValueOnBlockAxis()' were originally aimed at
   * inline-size (or width, as it was before logicalization). For now, we return
   * true for those here, so that we don't call ComputeBSizeValue with value
   * types that it doesn't understand. (See bug 1113216.)
   */
  template <typename SizeOrMaxSize>
  static bool IsAutoBSize(const SizeOrMaxSize& aCoord, nscoord aCBBSize) {
    // Note: percentages and 'stretch' both behave like 'auto' in the block
    // axis *if and only if* they're resolved against an unconstrained
    // block-size (on their containing block). That's what the second half of
    // this condition is handling.
    return aCoord.BehavesLikeInitialValueOnBlockAxis() ||
           (aCBBSize == nscoord_MAX &&
            (aCoord.HasPercent() || aCoord.BehavesLikeStretchOnBlockAxis()));
  }

  static bool IsPaddingZero(const LengthPercentage& aLength) {
    // clamp negative calc() to 0
    return aLength.Resolve(nscoord_MAX) <= 0 && aLength.Resolve(0) <= 0;
  }

  static void MarkDescendantsDirty(nsIFrame* aSubtreeRoot);

  static void MarkIntrinsicISizesDirtyIfDependentOnBSize(nsIFrame* aFrame);

  /*
   * Calculate the used values for 'width' and 'height' when width
   * and height are 'auto'. The tentWidth and tentHeight arguments should be
   * the result of applying the rules for computing intrinsic sizes and ratios.
   * as specified by CSS 2.1 sections 10.3.2 and 10.6.2
   */
  static nsSize ComputeAutoSizeWithIntrinsicDimensions(
      nscoord minWidth, nscoord minHeight, nscoord maxWidth, nscoord maxHeight,
      nscoord tentWidth, nscoord tentHeight);

  // Get a suitable foreground color for painting aColor for aFrame.
  static nscolor DarkenColorIfNeeded(nsIFrame* aFrame, nscolor aColor);

  // Get a suitable text foreground color for painting aField for aFrame.
  // Type of aFrame is made a template parameter because nsIFrame is not
  // a complete type in the header. Type-safety is not harmed given that
  // DarkenColorIfNeeded requires an nsIFrame pointer.
  template <typename Frame, typename T, typename S>
  static nscolor GetTextColor(Frame* aFrame, T S::* aField) {
    nscolor color = aFrame->GetVisitedDependentColor(aField);
    return DarkenColorIfNeeded(aFrame, color);
  }

  // Get a baseline y position in app units that is snapped to device pixels.
  static gfxFloat GetSnappedBaselineY(nsIFrame* aFrame, gfxContext* aContext,
                                      nscoord aY, nscoord aAscent);
  // Ditto for an x position (for vertical text). Note that for vertical-rl
  // writing mode, the ascent value should be negated by the caller.
  static gfxFloat GetSnappedBaselineX(nsIFrame* aFrame, gfxContext* aContext,
                                      nscoord aX, nscoord aAscent);

  static nscoord AppUnitWidthOfString(char16_t aC, nsFontMetrics& aFontMetrics,
                                      DrawTarget* aDrawTarget) {
    return AppUnitWidthOfString(&aC, 1, aFontMetrics, aDrawTarget);
  }
  static nscoord AppUnitWidthOfString(mozilla::Span<const char16_t> aString,
                                      nsFontMetrics& aFontMetrics,
                                      DrawTarget* aDrawTarget) {
    return nsLayoutUtils::AppUnitWidthOfString(
        aString.Elements(), aString.Length(), aFontMetrics, aDrawTarget);
  }
  static nscoord AppUnitWidthOfString(const char16_t* aString, uint32_t aLength,
                                      nsFontMetrics& aFontMetrics,
                                      DrawTarget* aDrawTarget);
  static nscoord AppUnitWidthOfStringBidi(const nsString& aString,
                                          const nsIFrame* aFrame,
                                          nsFontMetrics& aFontMetrics,
                                          gfxContext& aContext) {
    return nsLayoutUtils::AppUnitWidthOfStringBidi(
        aString.get(), aString.Length(), aFrame, aFontMetrics, aContext);
  }
  static nscoord AppUnitWidthOfStringBidi(const char16_t* aString,
                                          uint32_t aLength,
                                          const nsIFrame* aFrame,
                                          nsFontMetrics& aFontMetrics,
                                          gfxContext& aContext);

  static bool StringWidthIsGreaterThan(const nsString& aString,
                                       nsFontMetrics& aFontMetrics,
                                       DrawTarget* aDrawTarget, nscoord aWidth);

  static nsBoundingMetrics AppUnitBoundsOfString(const char16_t* aString,
                                                 uint32_t aLength,
                                                 nsFontMetrics& aFontMetrics,
                                                 DrawTarget* aDrawTarget);

  static void DrawString(const nsIFrame* aFrame, nsFontMetrics& aFontMetrics,
                         gfxContext* aContext, const char16_t* aString,
                         int32_t aLength, nsPoint aPoint,
                         ComputedStyle* aComputedStyle = nullptr,
                         DrawStringFlags aFlags = DrawStringFlags::Default);

  static nsPoint GetBackgroundFirstTilePos(const nsPoint& aDest,
                                           const nsPoint& aFill,
                                           const nsSize& aRepeatSize);

  /**
   * Supports only LTR or RTL. Bidi (mixed direction) is not supported.
   */
  static void DrawUniDirString(const char16_t* aString, uint32_t aLength,
                               const nsPoint& aPoint,
                               nsFontMetrics& aFontMetrics,
                               gfxContext& aContext);

  /**
   * Helper function for drawing text-shadow. The callback's job
   * is to draw whatever needs to be blurred onto the given context.
   */
  typedef void (*TextShadowCallback)(gfxContext* aCtx, nsPoint aShadowOffset,
                                     const nscolor& aShadowColor, void* aData);

  static void PaintTextShadow(const nsIFrame* aFrame, gfxContext* aContext,
                              const nsRect& aTextRect, const nsRect& aDirtyRect,
                              const nscolor& aForegroundColor,
                              TextShadowCallback aCallback,
                              void* aCallbackData);

  /**
   * Gets the baseline to vertically center text from a font within a
   * line of specified height.
   * aIsInverted: true if the text is inverted relative to the block
   * direction, so that the block-dir "ascent" corresponds to font
   * descent. (Applies to sideways text in vertical-lr mode.)
   *
   * Returns the baseline position relative to the top of the line.
   */
  static nscoord GetCenteredFontBaseline(nsFontMetrics* aFontMetrics,
                                         nscoord aLineHeight, bool aIsInverted);

  /**
   * Derive a baseline of |aFrame| (measured from its top border edge)
   * from its first in-flow line box (not descending into anything with
   * 'overflow' not 'visible', potentially including aFrame itself).
   *
   * Returns true if a baseline was found (and fills in aResult).
   * Otherwise returns false.
   */
  static bool GetFirstLineBaseline(mozilla::WritingMode aWritingMode,
                                   const nsIFrame* aFrame, nscoord* aResult);

  /**
   * Just like GetFirstLineBaseline, except also returns the top and
   * bottom of the line with the baseline.
   *
   * Returns true if a line was found (and fills in aResult).
   * Otherwise returns false.
   */
  struct LinePosition {
    nscoord mBStart{nscoord_MAX};
    nscoord mBaseline{nscoord_MAX};
    nscoord mBEnd{nscoord_MAX};

    LinePosition operator+(nscoord aOffset) const {
      LinePosition result;
      result.mBStart = mBStart + aOffset;
      result.mBaseline = mBaseline + aOffset;
      result.mBEnd = mBEnd + aOffset;
      return result;
    }
  };
  static bool GetFirstLinePosition(mozilla::WritingMode aWritingMode,
                                   const nsIFrame* aFrame,
                                   LinePosition* aResult);

  /**
   * Derive a baseline of |aFrame| (measured from its top border edge)
   * from its last in-flow line box (not descending into anything with
   * 'overflow' not 'visible', potentially including aFrame itself).
   *
   * Returns true if a baseline was found (and fills in aResult).
   * Otherwise returns false.
   */
  static bool GetLastLineBaseline(mozilla::WritingMode aWritingMode,
                                  const nsIFrame* aFrame, nscoord* aResult);

  /**
   * Returns a block-dir coordinate relative to this frame's origin that
   * represents the logical block-end of the frame or its visible content,
   * whichever is further from the origin.
   * Relative positioning is ignored and margins and glyph bounds are not
   * considered.
   * This value will be >= mRect.BSize() and <= overflowRect.BEnd() unless
   * relative positioning is applied.
   */
  static nscoord CalculateContentBEnd(mozilla::WritingMode aWritingMode,
                                      nsIFrame* aFrame);

  /**
   * Gets the closest frame (the frame passed in or one of its parents) that
   * qualifies as a "layer"; used in DOM0 methods that depends upon that
   * definition. This is the nearest frame that is either positioned or scrolled
   * (the child of a scroll frame).
   */
  static nsIFrame* GetClosestLayer(nsIFrame* aFrame);

  /**
   * Gets the graphics sampling filter for the frame
   */
  static SamplingFilter GetSamplingFilterForFrame(nsIFrame* aFrame);

  static inline void InitDashPattern(StrokeOptions& aStrokeOptions,
                                     mozilla::StyleBorderStyle aBorderStyle) {
    if (aBorderStyle == mozilla::StyleBorderStyle::Dotted) {
      static Float dot[] = {1.f, 1.f};
      aStrokeOptions.mDashLength = std::size(dot);
      aStrokeOptions.mDashPattern = dot;
    } else if (aBorderStyle == mozilla::StyleBorderStyle::Dashed) {
      static Float dash[] = {5.f, 5.f};
      aStrokeOptions.mDashLength = std::size(dash);
      aStrokeOptions.mDashPattern = dash;
    } else {
      aStrokeOptions.mDashLength = 0;
      aStrokeOptions.mDashPattern = nullptr;
    }
  }

  /**
   * Convert an nsRect to a gfxRect.
   */
  static gfxRect RectToGfxRect(const nsRect& aRect,
                               int32_t aAppUnitsPerDevPixel);

  static gfxPoint PointToGfxPoint(const nsPoint& aPoint,
                                  int32_t aAppUnitsPerPixel) {
    return gfxPoint(gfxFloat(aPoint.x) / aAppUnitsPerPixel,
                    gfxFloat(aPoint.y) / aAppUnitsPerPixel);
  }

  /* N.B. The only difference between variants of the Draw*Image
   * functions below is the type of the aImage argument.
   */

  /**
   * Draw a background image.  The image's dimensions are as specified in aDest;
   * the image itself is not consulted to determine a size.
   * See https://wiki.mozilla.org/Gecko:Image_Snapping_and_Rendering
   *
   * @param aContext
   *   The context to draw to, already set up with an appropriate scale and
   *   transform for drawing in app units.
   * @param aForFrame
   *   The nsIFrame that we're drawing this image for.
   * @param aImage
   *   The image.
   * @param aDest
   *  The position and scaled area where one copy of the image should be drawn.
   *  This area represents the image itself in its correct position as defined
   *  with the background-position css property.
   * @param aFill
   *  The area to be filled with copies of the image.
   * @param aRepeatSize
   *  The distance between the positions of two subsequent repeats of the image.
   *  Sizes larger than aDest.Size() create gaps between the images.
   * @param aAnchor
   *  A point in aFill which we will ensure is pixel-aligned in the output.
   * @param aDirty
   *   Pixels outside this area may be skipped.
   * @param aImageFlags
   *   Image flags of the imgIContainer::FLAG_* variety.
   * @param aExtendMode
   *   How to extend the image over the dest rect.
   */
  static ImgDrawResult DrawBackgroundImage(
      gfxContext& aContext, nsIFrame* aForFrame, nsPresContext* aPresContext,
      imgIContainer* aImage, SamplingFilter aSamplingFilter,
      const nsRect& aDest, const nsRect& aFill, const nsSize& aRepeatSize,
      const nsPoint& aAnchor, const nsRect& aDirty, uint32_t aImageFlags,
      ExtendMode aExtendMode, float aOpacity);

  /**
   * Draw an image.
   * See https://wiki.mozilla.org/Gecko:Image_Snapping_and_Rendering
   *   @param aRenderingContext Where to draw the image, set up with an
   *                            appropriate scale and transform for drawing in
   *                            app units.
   *   @param aComputedStyle    The ComputedStyle of the nsIFrame (or
   *                            pseudo-element) for which this image is being
   *                            drawn.
   *   @param aImage            The image.
   *   @param aDest             Where one copy of the image should mapped to.
   *   @param aFill             The area to be filled with copies of the
   *                            image.
   *   @param aAnchor           A point in aFill which we will ensure is
   *                            pixel-aligned in the output.
   *   @param aDirty            Pixels outside this area may be skipped.
   *   @param aImageFlags       Image flags of the imgIContainer::FLAG_* variety
   */
  static ImgDrawResult DrawImage(gfxContext& aContext,
                                 ComputedStyle* aComputedStyle,
                                 nsPresContext* aPresContext,
                                 imgIContainer* aImage,
                                 const SamplingFilter aSamplingFilter,
                                 const nsRect& aDest, const nsRect& aFill,
                                 const nsPoint& aAnchor, const nsRect& aDirty,
                                 uint32_t aImageFlags, float aOpacity = 1.0);

  /**
   * Draw a whole image without scaling or tiling.
   *
   *   @param aRenderingContext Where to draw the image, set up with an
   *                            appropriate scale and transform for drawing in
   *                            app units.
   *   @param aImage            The image.
   *   @param aDest             The top-left where the image should be drawn.
   *   @param aDirty            If non-null, then pixels outside this area may
   *                            be skipped.
   *   @param aSVGContext       Optionally provides an SVGImageContext.
   *                            Callers should pass an SVGImageContext with at
   *                            least the viewport size set if aImage may be of
   *                            type imgIContainer::TYPE_VECTOR, or pass
   *                            Nothing() if it is of type
   *                            imgIContainer::TYPE_RASTER (to save cycles
   *                            constructing an SVGImageContext, since this
   *                            argument will be ignored for raster images).
   *   @param aImageFlags       Image flags of the imgIContainer::FLAG_* variety
   *   @param aSourceArea       If non-null, this area is extracted from
   *                            the image and drawn at aDest. It's
   *                            in appunits. For best results it should
   *                            be aligned with image pixels.
   */
  static ImgDrawResult DrawSingleUnscaledImage(
      gfxContext& aContext, nsPresContext* aPresContext, imgIContainer* aImage,
      const SamplingFilter aSamplingFilter, const nsPoint& aDest,
      const nsRect* aDirty, const mozilla::SVGImageContext& aSVGContext,
      uint32_t aImageFlags, const nsRect* aSourceArea = nullptr);

  /**
   * Draw a whole image without tiling.
   *
   *   @param aRenderingContext Where to draw the image, set up with an
   *                            appropriate scale and transform for drawing in
   *                            app units.
   *   @param aImage            The image.
   *   @param aDest             The area that the image should fill.
   *   @param aDirty            Pixels outside this area may be skipped.
   *   @param aSVGContext       Optionally provides an SVGImageContext.
   *                            Callers should pass an SVGImageContext with at
   *                            least the viewport size set if aImage may be of
   *                            type imgIContainer::TYPE_VECTOR, or pass
   *                            Nothing() if it is of type
   *                            imgIContainer::TYPE_RASTER (to save cycles
   *                            constructing an SVGImageContext, since this
   *                            argument will be ignored for raster images).
   *   @param aImageFlags       Image flags of the imgIContainer::FLAG_*
   *                            variety.
   *   @param aAnchor           If non-null, a point which we will ensure
   *                            is pixel-aligned in the output.
   */
  static ImgDrawResult DrawSingleImage(
      gfxContext& aContext, nsPresContext* aPresContext, imgIContainer* aImage,
      SamplingFilter aSamplingFilter, const nsRect& aDest, const nsRect& aDirty,
      const mozilla::SVGImageContext& aSVGContext, uint32_t aImageFlags,
      const nsPoint* aAnchorPoint = nullptr);

  /**
   * Given an imgIContainer, this method attempts to obtain an intrinsic
   * px-valued height & width for it.  If the imgIContainer has a non-pixel
   * value for either height or width, this method tries to generate a pixel
   * value for that dimension using the intrinsic ratio (if available).  The
   * intrinsic ratio will be assigned to aIntrinsicRatio; if there's no
   * intrinsic ratio then (0, 0) will be assigned.
   *
   * This method will always set aGotWidth and aGotHeight to indicate whether
   * we were able to successfully obtain (or compute) a value for each
   * dimension.
   *
   * NOTE: This method is similar to ComputeSizeWithIntrinsicDimensions.  The
   * difference is that this one is simpler and is suited to places where we
   * have less information about the frame tree.
   *
   * @param aResolution The resolution specified by the author for the image, or
   *                    its intrinsic resolution.
   *
   *                    This will affect the intrinsic size size of the image
   *                    (so e.g., if resolution is 2, and the image is 100x100,
   *                    the intrinsic size of the image will be 50x50).
   */
  static void ComputeSizeForDrawing(imgIContainer* aImage,
                                    const mozilla::image::Resolution&,
                                    CSSIntSize& aImageSize,
                                    AspectRatio& aIntrinsicRatio,
                                    bool& aGotWidth, bool& aGotHeight);

  /**
   * Given an imgIContainer, this method attempts to obtain an intrinsic
   * px-valued height & width for it. If the imgIContainer has a non-pixel
   * value for either height or width, this method tries to generate a pixel
   * value for that dimension using the intrinsic ratio (if available). If,
   * after trying all these methods, no value is available for one or both
   * dimensions, the corresponding dimension of aFallbackSize is used instead.
   */
  static CSSIntSize ComputeSizeForDrawingWithFallback(
      imgIContainer* aImage, const mozilla::image::Resolution&,
      const nsSize& aFallbackSize);

  /**
   * Given the image container, frame, and dest rect, determine the best fitting
   * size to decode the image at, and calculate any necessary SVG parameters.
   */
  static mozilla::gfx::IntSize ComputeImageContainerDrawingParameters(
      imgIContainer* aImage, nsIFrame* aForFrame,
      const LayoutDeviceRect& aDestRect, const LayoutDeviceRect& aFillRect,
      const StackingContextHelper& aSc, uint32_t aFlags,
      mozilla::SVGImageContext& aSVGContext,
      mozilla::Maybe<mozilla::image::ImageIntRegion>& aRegion);

  /**
   * Given a source area of an image (in appunits) and a destination area
   * that we want to map that source area too, computes the area that
   * would be covered by the whole image. This is useful for passing to
   * the aDest parameter of DrawImage, when we want to draw a subimage
   * of an overall image.
   */
  static nsRect GetWholeImageDestination(const nsSize& aWholeImageSize,
                                         const nsRect& aImageSourceArea,
                                         const nsRect& aDestArea);

  /**
   * Given an image container and an orientation, returns an image container
   * that contains the same image, reoriented appropriately. May return the
   * original image container if no changes are needed.
   *
   * @param aContainer   The image container to apply the orientation to.
   * @param aOrientation The desired orientation.
   */
  static already_AddRefed<imgIContainer> OrientImage(
      imgIContainer* aContainer,
      const mozilla::StyleImageOrientation& aOrientation);

  /**
   * Given an image request, determine if the request uses CORS.
   */
  static bool ImageRequestUsesCORS(imgIRequest* aRequest);

  /**
   * Determine if any corner radius is of nonzero size
   *   @param aCorners the |BorderRadius| object to check
   *   @return true unless all the coordinates are 0%, 0 or null.
   *
   * A corner radius with one dimension zero and one nonzero is
   * treated as a nonzero-radius corner, even though it will end up
   * being rendered like a zero-radius corner.  This is because such
   * corners are not expected to appear outside of test cases, and it's
   * simpler to implement the test this way.
   */
  static bool HasNonZeroCorner(const mozilla::BorderRadius& aCorners);

  /**
   * Determine if there is any corner radius on corners adjacent to the
   * given side.
   */
  static bool HasNonZeroCornerOnSide(const mozilla::BorderRadius& aCorners,
                                     mozilla::Side aSide);

  /**
   * Determine if a widget is likely to require transparency or translucency.
   *   @param aBackgroundFrame The frame that the background is set on. For
   *                           <window>s, this will be the canvas frame.
   *   @param aCSSRootFrame    The frame that holds CSS properties affecting
   *                           the widget's transparency. For menupopups,
   *                           aBackgroundFrame and aCSSRootFrame will be the
   *                           same.
   *   @return a value suitable for passing to SetWindowTranslucency.
   */
  using TransparencyMode = mozilla::widget::TransparencyMode;
  static TransparencyMode GetFrameTransparency(nsIFrame* aBackgroundFrame,
                                               nsIFrame* aCSSRootFrame);

  /**
   * A frame is a popup if it has its own floating window. Menus, panels
   * and combobox dropdowns are popups.
   */
  static bool IsPopup(const nsIFrame* aFrame);

  /**
   * Find the nearest "display root". This is the nearest enclosing
   * popup frame or the root prescontext's root frame.
   */
  static nsIFrame* GetDisplayRootFrame(nsIFrame* aFrame);
  static const nsIFrame* GetDisplayRootFrame(const nsIFrame* aFrame);

  /**
   * Get the reference frame that would be used when constructing a
   * display item for this frame.  Rather than using their own frame
   * as a reference frame.)
   *
   * This duplicates some of the logic of GetDisplayRootFrame above and
   * of nsDisplayListBuilder::FindReferenceFrameFor.
   *
   * If you have an nsDisplayListBuilder, you should get the reference
   * frame from it instead of calling this.
   */
  static nsIFrame* GetReferenceFrame(nsIFrame* aFrame);

  /**
   * Get textrun construction flags determined by a given style; in particular
   * some combination of:
   * -- TEXT_DISABLE_OPTIONAL_LIGATURES if letter-spacing is in use
   * -- TEXT_OPTIMIZE_SPEED if the text-rendering CSS property and font size
   * and prefs indicate we should be optimizing for speed over quality
   */
  static mozilla::gfx::ShapedTextFlags GetTextRunFlagsForStyle(
      const ComputedStyle*, nsPresContext*, const nsStyleFont*,
      const nsStyleText*, nscoord aLetterSpacing);

  /**
   * Get orientation flags for textrun construction.
   */
  static mozilla::gfx::ShapedTextFlags GetTextRunOrientFlagsForStyle(
      const ComputedStyle*);

  /**
   * Takes two rectangles whose origins must be the same, and computes
   * the difference between their union and their intersection as two
   * rectangles. (This difference is a superset of the difference
   * between the two rectangles.)
   */
  static void GetRectDifferenceStrips(const nsRect& aR1, const nsRect& aR2,
                                      nsRect* aHStrip, nsRect* aVStrip);

  /**
   * Get a device context that can be used to get up-to-date device
   * dimensions for the given window. For some reason, this is more
   * complicated than it ought to be in multi-monitor situations.
   */
  static nsDeviceContext* GetDeviceContextForScreenInfo(
      nsPIDOMWindowOuter* aWindow);

  /**
   * Some frames with 'position: fixed' (nsStyleDisplay::mPosition ==
   * StylePositionProperty::Fixed) are not really fixed positioned, since
   * they're inside a transformed element or other element that establishes a
   * fixed-pos containing block).  This function says whether such an element is
   * a real fixed-pos element.
   */
  static bool IsReallyFixedPos(const nsIFrame* aFrame);

  /**
   * This function says whether `aFrame` would really be a fixed positioned
   * frame if the frame was created with StylePositionProperty::Fixed.
   *
   * It is effectively the same as IsReallyFixedPos, but without asserting the
   * position value. Use it only when you know what you're doing, like when
   * tearing down the frame tree (where styles may have changed due to
   * ::first-line reparenting and rule changes at the same time).
   */
  static bool MayBeReallyFixedPos(const nsIFrame* aFrame);

  /**
   * Returns true if |aFrame| is inside position:fixed subtree.
   */
  static bool IsInPositionFixedSubtree(const nsIFrame* aFrame);

  /**
   * Obtain a SourceSurface from the given DOM element, if possible.
   * This obtains the most natural surface from the element; that
   * is, the one that can be obtained with the fewest conversions.
   *
   * The flags below can modify the behaviour of this function.  The
   * result is returned as a SurfaceFromElementResult struct, also
   * defined below.
   *
   * Currently, this will do:
   *  - HTML Canvas elements: will return the underlying canvas surface
   *  - HTML Video elements: will return the current video frame
   *  - Image elements: will return the image
   *
   * The above results are modified by the below flags (copying,
   * forcing image surface, etc.).
   */

  enum {
    /* Whether to extract the first frame (as opposed to the
       current frame) in the case that the element is an image. */
    SFE_WANT_FIRST_FRAME_IF_IMAGE = 1 << 0,
    /* Whether we should skip colorspace/gamma conversion */
    SFE_NO_COLORSPACE_CONVERSION = 1 << 1,
    /* Caller handles SFER::mAlphaType = NonPremult */
    SFE_ALLOW_NON_PREMULT = 1 << 2,
    /* Whether we should skip getting a surface for vector images and
       return a DirectDrawInfo containing an imgIContainer instead. */
    SFE_NO_RASTERIZING_VECTORS = 1 << 3,
    /* If image type is vector, the return surface size will same as
       element size, not image's intrinsic size. */
    SFE_USE_ELEMENT_SIZE_IF_VECTOR = 1 << 4,
    /* Ensure that the returned surface has a size that matches the
     * SurfaceFromElementResult::mSize. This is mostly a convenience thing so
     * that callers who want this don't have to deal with it themselves.
     * The surface might be different for, e.g., a EXIF-scaled raster image, if
     * we don't rescale during decode. */
    SFE_EXACT_SIZE_SURFACE = 1 << 6,
    /* Use orientation from image */
    SFE_ORIENTATION_FROM_IMAGE = 1 << 7,
    /* Caller handles SFER::mCropRect.isSome() */
    SFE_ALLOW_UNCROPPED_UNSCALED = 1 << 8,
  };

  // This function can be called on any thread.
  static mozilla::SurfaceFromElementResult SurfaceFromOffscreenCanvas(
      mozilla::dom::OffscreenCanvas* aOffscreenCanvas, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  static mozilla::SurfaceFromElementResult SurfaceFromOffscreenCanvas(
      mozilla::dom::OffscreenCanvas* aOffscreenCanvas,
      uint32_t aSurfaceFlags = 0) {
    RefPtr<DrawTarget> target = nullptr;
    return SurfaceFromOffscreenCanvas(aOffscreenCanvas, aSurfaceFlags, target);
  }
  // This function can be called on any thread.
  static mozilla::SurfaceFromElementResult SurfaceFromVideoFrame(
      mozilla::dom::VideoFrame* aVideoFrame, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  static mozilla::SurfaceFromElementResult SurfaceFromVideoFrame(
      mozilla::dom::VideoFrame* aVideoFrame, uint32_t aSurfaceFlags = 0) {
    RefPtr<DrawTarget> target = nullptr;
    return SurfaceFromVideoFrame(aVideoFrame, aSurfaceFlags, target);
  }
  // This function can be called on any thread.
  static mozilla::SurfaceFromElementResult SurfaceFromImageBitmap(
      mozilla::dom::ImageBitmap* aImageBitmap, uint32_t aSurfaceFlags);

  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::Element* aElement,
      const mozilla::Maybe<int32_t>& aResizeWidth,
      const mozilla::Maybe<int32_t>& aResizeHeight, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::Element* aElement, uint32_t aSurfaceFlags = 0) {
    RefPtr<DrawTarget> target = nullptr;
    return SurfaceFromElement(aElement, mozilla::Nothing(), mozilla::Nothing(),
                              aSurfaceFlags, target);
  }
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::Element* aElement, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget) {
    return SurfaceFromElement(aElement, mozilla::Nothing(), mozilla::Nothing(),
                              aSurfaceFlags, aTarget);
  }
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::Element* aElement,
      const mozilla::Maybe<int32_t>& aResizeWidth,
      const mozilla::Maybe<int32_t>& aResizeHeight,
      uint32_t aSurfaceFlags = 0) {
    RefPtr<DrawTarget> target = nullptr;
    return SurfaceFromElement(aElement, aResizeWidth, aResizeHeight,
                              aSurfaceFlags, target);
  }

  // There are a bunch of callers of SurfaceFromElement.  Just mark it as
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      nsIImageLoadingContent* aElement,
      const mozilla::Maybe<int32_t>& aResizeWidth,
      const mozilla::Maybe<int32_t>& aResizeHeight, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  // Need an HTMLImageElement overload, because otherwise the
  // nsIImageLoadingContent and mozilla::dom::Element overloads are ambiguous
  // for HTMLImageElement.
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::HTMLImageElement* aElement, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::HTMLCanvasElement* aElement, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget);
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::HTMLCanvasElement* aElement, uint32_t aSurfaceFlags) {
    RefPtr<DrawTarget> target = nullptr;
    return SurfaceFromElement(aElement, aSurfaceFlags, target);
  }
  static mozilla::SurfaceFromElementResult SurfaceFromElement(
      mozilla::dom::HTMLVideoElement* aElement, uint32_t aSurfaceFlags,
      RefPtr<DrawTarget>& aTarget, bool aOptimizeSourceSurface = true);

  /**
   * When the document is editable by contenteditable attribute of its root
   * content or body content.
   *
   * Be aware, this returns nullptr if it's in designMode.
   *
   * For example:
   *
   *  <html contenteditable="true"><body></body></html>
   *    returns the <html>.
   *
   *  <html><body contenteditable="true"></body></html>
   *  <body contenteditable="true"></body>
   *    With these cases, this returns the <body>.
   *    NOTE: The latter case isn't created normally, however, it can be
   *          created by script with XHTML.
   *
   *  <body><p contenteditable="true"></p></body>
   *    returns nullptr because <body> isn't editable.
   */
  static mozilla::dom::Element* GetEditableRootContentByContentEditable(
      mozilla::dom::Document* aDocument);

  static void AddExtraBackgroundItems(nsDisplayListBuilder* aBuilder,
                                      nsDisplayList* aList, nsIFrame* aFrame,
                                      const nsRect& aCanvasArea,
                                      const nsRegion& aVisibleRegion,
                                      nscolor aBackstop);

  /**
   * Returns true if the passed in prescontext needs the dark grey background
   * that goes behind the page of a print preview presentation.
   */
  static bool NeedsPrintPreviewBackground(nsPresContext* aPresContext);

  /**
   * Types used by the helpers for InspectorUtils.getUsedFontFaces.
   * The API returns an array (UsedFontFaceList) that owns the
   * InspectorFontFace instances, but during range traversal we also
   * want to maintain a mapping from gfxFontEntry to InspectorFontFace
   * records, so use a temporary hashtable for that.
   */
  typedef nsTArray<mozilla::UniquePtr<mozilla::dom::InspectorFontFace>>
      UsedFontFaceList;
  typedef nsTHashMap<nsPtrHashKey<gfxFontEntry>,
                     mozilla::dom::InspectorFontFace*>
      UsedFontFaceTable;

  /**
   * Adds all font faces used in the frame tree starting from aFrame
   * to the list aFontFaceList.
   * aMaxRanges: maximum number of text ranges to record for each face.
   */
  static nsresult GetFontFacesForFrames(nsIFrame* aFrame,
                                        UsedFontFaceList& aResult,
                                        UsedFontFaceTable& aFontFaces,
                                        uint32_t aMaxRanges,
                                        bool aSkipCollapsedWhitespace);

  /**
   * Adds all font faces used within the specified range of text in aFrame,
   * and optionally its continuations, to the list in aFontFaceList.
   * Pass 0 and INT32_MAX for aStartOffset and aEndOffset to specify the
   * entire text is to be considered.
   * aMaxRanges: maximum number of text ranges to record for each face.
   */
  static void GetFontFacesForText(nsIFrame* aFrame, int32_t aStartOffset,
                                  int32_t aEndOffset, bool aFollowContinuations,
                                  UsedFontFaceList& aResult,
                                  UsedFontFaceTable& aFontFaces,
                                  uint32_t aMaxRanges,
                                  bool aSkipCollapsedWhitespace);

  /**
   * Walks the frame tree starting at aFrame looking for textRuns.
   * If |clear| is true, just clears the TEXT_RUN_MEMORY_ACCOUNTED flag
   * on each textRun found (and |aMallocSizeOf| is not used).
   * If |clear| is false, adds the storage used for each textRun to the
   * total, and sets the TEXT_RUN_MEMORY_ACCOUNTED flag to avoid double-
   * accounting. (Runs with this flag already set will be skipped.)
   * Expected usage pattern is therefore to call twice:
   *    (void)SizeOfTextRunsForFrames(rootFrame, nullptr, true);
   *    total = SizeOfTextRunsForFrames(rootFrame, mallocSizeOf, false);
   */
  static size_t SizeOfTextRunsForFrames(nsIFrame* aFrame,
                                        mozilla::MallocSizeOf aMallocSizeOf,
                                        bool clear);

  /**
   * Returns true if |aFrame| has an animation of a property in |aPropertySet|
   * regardless of whether any property in the set is overridden by an
   * !important rule.
   */
  static bool HasAnimationOfPropertySet(const nsIFrame* aFrame,
                                        const nsCSSPropertyIDSet& aPropertySet);

  /**
   * A variant of the above HasAnimationOfPropertySet that takes an optional
   * EffectSet parameter as an optimization to save redundant lookups of the
   * EffectSet.
   */
  static bool HasAnimationOfPropertySet(const nsIFrame* aFrame,
                                        const nsCSSPropertyIDSet& aPropertySet,
                                        mozilla::EffectSet* aEffectSet);

  /**
   * A variant of the above HasAnimationOfPropertySet. This is especially for
   * tranform-like properties with motion-path.
   * For transform-like properties with motion-path, we need to check if
   * offset-path has effect. If we don't have any animation on offset-path and
   * offset-path is none, there is no effective motion-path, and so we don't
   * care other offset-* properties. In this case, this function only checks the
   * rest of transform-like properties (i.e. transform/translate/rotate/scale).
   */
  static bool HasAnimationOfTransformAndMotionPath(const nsIFrame* aFrame);

  /**
   * Returns true if |aFrame| has an animation of |aProperty| which is
   * not overridden by !important rules.
   */
  static bool HasEffectiveAnimation(const nsIFrame* aFrame,
                                    nsCSSPropertyID aProperty);

  /**
   * Returns true if |aFrame| has an animation where at least one of the
   * properties in |aPropertySet| is not overridden by !important rules.
   *
   * If |aPropertySet| includes transform-like properties (transform, rotate,
   * etc.) however, this will return false if any of the transform-like
   * properties is overriden by an !important rule since these properties should
   * be combined on the compositor.
   */
  static bool HasEffectiveAnimation(const nsIFrame* aFrame,
                                    const nsCSSPropertyIDSet& aPropertySet);

  /**
   * Returns all effective animated CSS properties on |aStyleFrame| and its
   * corresponding primary frame (for content that makes this distinction,
   * notable display:table content) that can be animated on the compositor.
   *
   * Properties that can be animated on the compositor but which are overridden
   * by !important rules are not returned.
   *
   * Unlike HasEffectiveAnimation, however, this does not check the set of
   * transform-like properties to ensure that if any such properties are
   * overridden by !important rules, the other transform-like properties are
   * not run on the compositor (see bug 1534884).
   */
  static nsCSSPropertyIDSet GetAnimationPropertiesForCompositor(
      const nsIFrame* aStyleFrame);

  /**
   * Checks if off-main-thread animations are enabled.
   */
  static bool AreAsyncAnimationsEnabled();

  /**
   * Checks if retained display lists are enabled.
   */
  static bool AreRetainedDisplayListsEnabled();

  static bool DisplayRootHasRetainedDisplayListBuilder(nsIFrame* aFrame);

  static RetainedDisplayListBuilder* GetRetainedDisplayListBuilder(
      nsIFrame* aFrame);

  /**
   * Find a suitable scale for a element (aFrame's content) over the course of
   * any animations and transitions of the CSS transform property on the element
   * that run on the compositor thread. It will check the maximum and minimum
   * scale during the animations and transitions and return a suitable value for
   * performance and quality. Will return scale(1,1) if there are no such
   * animations. Always returns a positive value.
   * @param aVisibleSize is the size of the area we want to paint
   * @param aDisplaySize is the size of the display area of the pres context
   */
  static MatrixScales ComputeSuitableScaleForAnimation(
      const nsIFrame* aFrame, const nsSize& aVisibleSize,
      const nsSize& aDisplaySize);

  /**
   * Unions the overflow areas of the children of aFrame with aOverflowAreas.
   * aSkipChildLists specifies any child lists that should be skipped.
   * FrameChildListID::Popup is always skipped.
   */
  static void UnionChildOverflow(
      nsIFrame* aFrame, mozilla::OverflowAreas& aOverflowAreas,
      mozilla::FrameChildListIDs aSkipChildLists = {});

  /**
   * Return the font size inflation *ratio* for a given frame.  This is
   * the factor by which font sizes should be inflated; it is never
   * smaller than 1.
   */
  static float FontSizeInflationFor(const nsIFrame* aFrame);

  /**
   * Perform the first half of the computation of FontSizeInflationFor
   * (see above).
   * This includes determining whether inflation should be performed
   * within this container and returning 0 if it should not be.
   *
   * The result is guaranteed not to vary between line participants
   * (inlines, text frames) within a line.
   *
   * The result should not be used directly since font sizes slightly
   * above the minimum should always be adjusted as done by
   * FontSizeInflationInner.
   */
  static nscoord InflationMinFontSizeFor(const nsIFrame* aFrame);

  /**
   * Perform the second half of the computation done by
   * FontSizeInflationFor (see above).
   *
   * aMinFontSize must be the result of one of the
   *   InflationMinFontSizeFor methods above.
   */
  static float FontSizeInflationInner(const nsIFrame* aFrame,
                                      nscoord aMinFontSize);

  static bool FontSizeInflationEnabled(nsPresContext* aPresContext);

  /**
   * Returns true if the nglayout.debug.invalidation pref is set to true.
   */
  static bool InvalidationDebuggingIsEnabled() {
    return mozilla::StaticPrefs::nglayout_debug_invalidation() ||
           getenv("MOZ_DUMP_INVALIDATION") != 0;
  }

  static void Initialize();
  static void Shutdown();

  /**
   * Register an imgIRequest object with a refresh driver.
   *
   * @param aPresContext The nsPresContext whose refresh driver we want to
   *        register with.
   * @param aRequest A pointer to the imgIRequest object which the client wants
   *        to register with the refresh driver.
   * @param aRequestRegistered A pointer to a boolean value which indicates
   *        whether the given image request is registered. If
   *        *aRequestRegistered is true, then this request will not be
   *        registered again. If the request is registered by this function,
   *        then *aRequestRegistered will be set to true upon the completion of
   *        this function.
   *
   */
  static void RegisterImageRequest(nsPresContext* aPresContext,
                                   imgIRequest* aRequest,
                                   bool* aRequestRegistered);

  /**
   * Register an imgIRequest object with a refresh driver, but only if the
   * request is for an image that is animated.
   *
   * @param aPresContext The nsPresContext whose refresh driver we want to
   *        register with.
   * @param aRequest A pointer to the imgIRequest object which the client wants
   *        to register with the refresh driver.
   * @param aRequestRegistered A pointer to a boolean value which indicates
   *        whether the given image request is registered. If
   *        *aRequestRegistered is true, then this request will not be
   *        registered again. If the request is registered by this function,
   *        then *aRequestRegistered will be set to true upon the completion of
   *        this function.
   *
   */
  static void RegisterImageRequestIfAnimated(nsPresContext* aPresContext,
                                             imgIRequest* aRequest,
                                             bool* aRequestRegistered);

  /**
   * Deregister an imgIRequest object from a refresh driver.
   *
   * @param aPresContext The nsPresContext whose refresh driver we want to
   *        deregister from.
   * @param aRequest A pointer to the imgIRequest object with which the client
   *        previously registered and now wants to deregister from the refresh
   *        driver.
   * @param aRequestRegistered A pointer to a boolean value which indicates
   *        whether the given image request is registered. If
   *        *aRequestRegistered is false, then this request will not be
   *        deregistered. If the request is deregistered by this function,
   *        then *aRequestRegistered will be set to false upon the completion of
   *        this function.
   */
  static void DeregisterImageRequest(nsPresContext* aPresContext,
                                     imgIRequest* aRequest,
                                     bool* aRequestRegistered);

  /**
   * Shim to nsCSSFrameConstructor::PostRestyleEvent. Exists so that we
   * can avoid including nsCSSFrameConstructor.h and all its dependencies
   * in content files.
   */
  static void PostRestyleEvent(mozilla::dom::Element*, mozilla::RestyleHint,
                               nsChangeHint aMinChangeHint);

  /**
   * Updates a pair of x and y distances if a given point is closer to a given
   * rectangle than the original distance values.  If aPoint is closer to
   * aRect than aClosestXDistance and aClosestYDistance indicate, then those
   * two variables are updated with the distance between aPoint and aRect,
   * and true is returned.  If aPoint is not closer, then aClosestXDistance
   * and aClosestYDistance are left unchanged, and false is returned.
   *
   * Distances are measured in the two dimensions separately; a closer x
   * distance beats a closer y distance.
   */
  template <typename PointType, typename RectType, typename CoordType>
  static bool PointIsCloserToRect(PointType aPoint, const RectType& aRect,
                                  CoordType& aClosestXDistance,
                                  CoordType& aClosestYDistance);
  /**
   * Computes the box shadow rect for the frame, or returns an empty rect if
   * there are no shadows.
   *
   * @param aFrame Frame to compute shadows for.
   * @param aFrameSize Size of aFrame (in case it hasn't been set yet).
   */
  static nsRect GetBoxShadowRectForFrame(nsIFrame* aFrame,
                                         const nsSize& aFrameSize);

#ifdef DEBUG
  /**
   * Assert that there are no duplicate continuations of the same frame
   * within aFrameList.  Optimize the tests by assuming that all frames
   * in aFrameList have parent aContainer.
   */
  static void AssertNoDuplicateContinuations(nsIFrame* aContainer,
                                             const nsFrameList& aFrameList);

  /**
   * Assert that the frame tree rooted at |aSubtreeRoot| is empty, i.e.,
   * that it contains no first-in-flows.
   */
  static void AssertTreeOnlyEmptyNextInFlows(nsIFrame* aSubtreeRoot);
#endif

  /**
   * Helper method to transform |aBounds| from aFrame to aAncestorFrame,
   * and combine it with |aPreciseTargetDest| if it is axis-aligned, or
   * combine it with |aImpreciseTargetDest| if not. The transformed rect is
   * clipped to |aClip|; if |aClip| has rounded corners, that also causes
   * the imprecise target to be used.
   */
  static void TransformToAncestorAndCombineRegions(
      const nsRegion& aRegion, nsIFrame* aFrame, const nsIFrame* aAncestorFrame,
      nsRegion* aPreciseTargetDest, nsRegion* aImpreciseTargetDest,
      mozilla::Maybe<Matrix4x4Flagged>* aMatrixCache,
      const mozilla::DisplayItemClip* aClip);

  /**
   * Populate aOutSize with the size of the content viewer corresponding
   * to the given prescontext. Return true if the size was set, false
   * otherwise.
   */
  enum class SubtractDynamicToolbar { No, Yes };
  static bool GetDocumentViewerSize(
      const nsPresContext* aPresContext, LayoutDeviceIntSize& aOutSize,
      SubtractDynamicToolbar = SubtractDynamicToolbar::Yes);

  /**
   * Whether to include the dynamic toolbar area automatically (depending
   * whether the root container is scrollable or not) or forcibly in below
   * UpdateCompositionBoundsForRCDRSF and CalculateCompositionSizeForFrame
   * functions.
   */
  enum class IncludeDynamicToolbar { Auto, Force };

 private:
  static bool UpdateCompositionBoundsForRCDRSF(
      mozilla::ParentLayerRect& aCompBounds, const nsPresContext* aPresContext,
      IncludeDynamicToolbar aIncludeDynamicToolbar =
          IncludeDynamicToolbar::Auto);

 public:
  /**
   * Calculate the compostion size for a frame. See FrameMetrics.h for
   * defintion of composition size (or bounds).
   * Note that for the root content document's root scroll frame (RCD-RSF),
   * the returned size does not change as the document's resolution changes,
   * but for all other frames it does. This means that callers that pass in
   * a frame that may or may not be the RCD-RSF (which is most callers),
   * are likely to need special-case handling of the RCD-RSF.
   */
  static nsSize CalculateCompositionSizeForFrame(
      nsIFrame* aFrame, bool aSubtractScrollbars = true,
      const nsSize* aOverrideScrollPortSize = nullptr,
      IncludeDynamicToolbar aIncludeDynamicToolbar =
          IncludeDynamicToolbar::Auto);

  /**
   * Calculate a size suitable for bounding the size of the composition bounds
   * of scroll frames in the current process. This should be at most the
   * composition size of the cross-process RCD-RSF, but it may be a tighter
   * bounding size.
   * @param aFrame A frame in the (in-process) root content document (or a
   *          descendant of it).
   * @param aIsRootContentDocRootScrollFrame Whether aFrame is the root
   *          scroll frame of the *cross-process* root content document.
   *          In this case we just use aFrame's own composition size.
   * @param aMetrics A partially populated FrameMetrics for aFrame. Must have at
   *          least mCompositionBounds, mCumulativeResolution, and
   *          mDevPixelsPerCSSPixel set.
   */
  static CSSSize CalculateBoundingCompositionSize(
      const nsIFrame* aFrame, bool aIsRootContentDocRootScrollFrame,
      const FrameMetrics& aMetrics);

  /**
   * Calculate the scrollable rect for a frame. See FrameMetrics.h for
   * definition of scrollable rect. aScrollContainerFrame is the scroll
   * container frame to calculate the scrollable rect for. If it's null then we
   * calculate the scrollable rect as the rect of the root frame.
   */
  static nsRect CalculateScrollableRectForFrame(
      const mozilla::ScrollContainerFrame* aScrollContainerFrame,
      const nsIFrame* aRootFrame);

  /**
   * Calculate the expanded scrollable rect for a frame. See FrameMetrics.h for
   * defintion of expanded scrollable rect.
   */
  static nsRect CalculateExpandedScrollableRect(nsIFrame* aFrame);

  /**
   * Returns true if the widget owning the given frame uses asynchronous
   * scrolling.
   */
  static bool UsesAsyncScrolling(nsIFrame* aFrame);

  /**
   * Returns true if the widget owning the given frame has builtin APZ support
   * enabled.
   */
  static bool AsyncPanZoomEnabled(const nsIFrame* aFrame);

  /**
   * Returns true if aDocument should be allowed to use resolution
   * zooming.
   */
  static bool AllowZoomingForDocument(const mozilla::dom::Document* aDocument);

  /**
   * Returns true if we need to disable async scrolling for this particular
   * element. Note that this does a partial disabling - the displayport still
   * exists but uses a very small margin, and the compositor doesn't apply the
   * async transform. However, the content may still be layerized.
   */
  static bool ShouldDisableApzForElement(nsIContent* aContent);

  /**
   * Log a key/value pair as "additional data" (not associated with a paint)
   * for APZ testing.
   * While the data is not associated with a paint, the APZTestData object
   * is still owned by {Client,WebRender}LayerManager, so we need to be passed
   * something from which we can derive the layer manager.
   * This function takes a display list builder as the object to derive the
   * layer manager from, to facilitate logging test data during display list
   * building, but other overloads that take other objects could be added if
   * desired.
   */
  static void LogAdditionalTestData(nsDisplayListBuilder* aBuilder,
                                    const std::string& aKey,
                                    const std::string& aValue);

  /**
   * Log a key/value pair for APZ testing during a paint.
   * @param aManager   The data will be written to the APZTestData associated
   *                   with this layer manager.
   * @param aScrollId Identifies the scroll frame to which the data pertains.
   * @param aKey The key under which to log the data.
   * @param aValue The value of the data to be logged.
   */
  static void LogTestDataForPaint(
      mozilla::layers::WebRenderLayerManager* aManager, ViewID aScrollId,
      const std::string& aKey, const std::string& aValue) {
    DoLogTestDataForPaint(aManager, aScrollId, aKey, aValue);
  }

  /**
   * A convenience overload of LogTestDataForPaint() that accepts any type
   * as the value, and passes it through mozilla::ToString() to obtain a string
   * value. The type passed must support streaming to an std::ostream.
   */
  template <typename Value>
  static void LogTestDataForPaint(
      mozilla::layers::WebRenderLayerManager* aManager, ViewID aScrollId,
      const std::string& aKey, const Value& aValue) {
    DoLogTestDataForPaint(aManager, aScrollId, aKey, mozilla::ToString(aValue));
  }

  /**
   * Calculate a basic FrameMetrics with enough fields set to perform some
   * layout calculations. The fields set are dev-to-css ratio, pres shell
   * resolution, cumulative resolution, zoom, composition size, root
   * composition size, scroll offset and scrollable rect.
   *
   * Note that for the RCD-RSF, the scroll offset returned is the layout
   * viewport offset; if you need the visual viewport offset, that needs to
   * be queried independently via PresShell::GetVisualViewportOffset().
   *
   * By contrast, ComputeScrollMetadata() computes all the fields, but requires
   * extra inputs and can only be called during frame layer building.
   */
  static FrameMetrics CalculateBasicFrameMetrics(
      mozilla::ScrollContainerFrame* aScrollContainerFrame);

  static mozilla::ScrollContainerFrame* GetAsyncScrollableAncestorFrame(
      nsIFrame* aTarget);

  static void SetBSizeFromFontMetrics(
      const nsIFrame* aFrame, mozilla::ReflowOutput& aMetrics,
      const mozilla::LogicalMargin& aFramePadding, mozilla::WritingMode aLineWM,
      mozilla::WritingMode aFrameWM);

  static bool HasDocumentLevelListenersForApzAwareEvents(PresShell* aPresShell);

  /**
   * Returns true if the given scroll origin is "higher priority" than APZ.
   * In general any content programmatic scrolls (e.g. scrollTo calls) are
   * higher priority, and take precedence over APZ scrolling. This function
   * returns true for those, and returns false for other origins like APZ
   * itself, or scroll position updates from the history restore code.
   */
  static bool CanScrollOriginClobberApz(ScrollOrigin aScrollOrigin);

  static ScrollMetadata ComputeScrollMetadata(
      const nsIFrame* aForFrame, const nsIFrame* aScrollFrame,
      nsIContent* aContent, const nsIFrame* aItemFrame,
      const nsPoint& aOffsetToReferenceFrame,
      mozilla::layers::WebRenderLayerManager* aLayerManager,
      ViewID aScrollParentId, const nsSize& aScrollPortSize, bool aIsRoot);

  /**
   * Returns the metadata to put onto the root layer of a layer tree, if one is
   * needed. The last argument is a callback function to check if the caller
   * already has a metadata for a given scroll id.
   */
  static mozilla::Maybe<ScrollMetadata> GetRootMetadata(
      nsDisplayListBuilder* aBuilder,
      mozilla::layers::WebRenderLayerManager* aLayerManager,
      const std::function<bool(ViewID& aScrollId)>& aCallback);

  /**
   * If the given scroll frame needs an area excluded from its composition
   * bounds due to scrollbars, return that area, otherwise return an empty
   * margin.
   * There is no need to exclude scrollbars in the following cases:
   *   - If the scroll frame is not the RCD-RSF; in that case, the composition
   *     bounds is calculated based on the scroll port which already excludes
   *     the scrollbar area.
   *   - If the scrollbars are overlay, since then they are drawn on top of the
   *     scrollable content.
   */
  static nsMargin ScrollbarAreaToExcludeFromCompositionBoundsFor(
      const nsIFrame* aScrollFrame);

  static bool ShouldUseNoFramesSheet(mozilla::dom::Document*);

  /**
   * Get the text content inside the frame. This methods traverse the
   * frame tree and collect the content from text frames. Note that this
   * method is similiar to nsContentUtils::GetNodeTextContent, but it at
   * least differs from that method in the following things:
   * 1. it skips text content inside nodes like style, script, textarea
   *    which don't generate an in-tree text frame for the text;
   * 2. it skips elements with display property set to none;
   * 3. it skips out-of-flow elements;
   * 4. it includes content inside pseudo elements;
   * 5. it may include part of text content of a node if a text frame
   *    inside is split to different continuations.
   */
  static void GetFrameTextContent(nsIFrame* aFrame, nsAString& aResult);

  /**
   * Same as GetFrameTextContent but appends the result rather than sets it.
   */
  static void AppendFrameTextContent(nsIFrame* aFrame, nsAString& aResult);

  /**
   * Takes a selection, and returns selection's bounding rect which is relative
   * to its root frame.
   *
   * @param aSel      Selection to check
   */
  static nsRect GetSelectionBoundingRect(const mozilla::dom::Selection* aSel);

  /**
   * Calculate the bounding rect of |aContent|, relative to the origin
   * of the scrolled content of |aRootScrollFrame|.
   * Where the element is contained inside a scrollable subframe, the
   * bounding rect is clipped to the bounds of the subframe.
   * If non-null aOutNearestScrollClip will be filled in with the rect of the
   * nearest scroll frame (excluding aRootScrollFrame) that is an ancestor of
   * the frame of aContent, if such exists, in the same coords are the returned
   * rect. This rect is used to clip the result.
   */
  static CSSRect GetBoundingContentRect(
      const nsIContent* aContent,
      const mozilla::ScrollContainerFrame* aRootScrollContainerFrame,
      mozilla::Maybe<CSSRect>* aOutNearestScrollClip = nullptr);

  /**
   * Similar to GetBoundingContentRect for nsIFrame.
   */
  static CSSRect GetBoundingFrameRect(
      nsIFrame* aFrame,
      const mozilla::ScrollContainerFrame* aRootScrollContainerFrame,
      mozilla::Maybe<CSSRect>* aOutNearestScrollClip = nullptr);

  /**
   * Returns the first ancestor who is a float containing block.
   */
  static nsBlockFrame* GetFloatContainingBlock(nsIFrame* aFrame);

  /**
   * Walks up the frame tree from |aForFrame| up to |aTopFrame|, or to the
   * root of the frame tree if |aTopFrame| is nullptr, and returns true if
   * a transformed frame is encountered.
   */
  static bool IsTransformed(nsIFrame* aForFrame, nsIFrame* aTopFrame = nullptr);

  /**
   * Walk up from aFrame to the cross-doc root, accumulating all the APZ
   * callback transforms on the content elements encountered along the way.
   * Return the accumulated value.
   * XXX: Note that this does not take into account CSS transforms, nor
   * differences in structure between the frame tree and the layer tree (which
   * is probably what we *want* to be computing).
   */
  static CSSPoint GetCumulativeApzCallbackTransform(nsIFrame* aFrame);

  /**
   * Compute a rect to pre-render in cases where we want to render more of
   * something than what is visible (usually to support async transformation).
   * @param aFrame the target frame to be pre-rendered
   * @param aDirtyRect the area that's visible in the coordinate system of
   *        |aFrame|.
   * @param aOverflow the total size of the thing we're rendering in the
   *        coordinate system of |aFrame|.
   * @param aPrerenderSize how large of an area we're willing to render in the
   *        coordinate system of the root frame.
   * @return A rectangle that includes |aDirtyRect|, is clamped to |aOverflow|,
   *         and is no larger than |aPrerenderSize| (unless |aPrerenderSize| is
   *         smaller than |aDirtyRect|, in which case the returned rect will
   *         still include |aDirtyRect| and thus be larger than
   *         |aPrerenderSize|).
   */
  static nsRect ComputePartialPrerenderArea(nsIFrame* aFrame,
                                            const nsRect& aDirtyRect,
                                            const nsRect& aOverflow,
                                            const nsSize& aPrerenderSize);

  /*
   * Checks whether a node is an invisible break.
   * If not, returns the first frame on the next line if such a next line
   * exists.
   *
   * @return
   *   true if the node is an invisible break. aNextLineFrame is returned null
   *   in this case.
   *
   *   false if the node causes a visible break or if the node is no break.
   *
   * @param aNextLineFrame
   *   assigned to first frame on the next line if such a next line exists, null
   *   otherwise.
   */
  static bool IsInvisibleBreak(nsINode* aNode,
                               nsIFrame** aNextLineFrame = nullptr);

  static nsRect ComputeSVGOriginBox(mozilla::dom::SVGViewportElement*);

  // Compute the geometry box for SVG layout. The caller should map the CSS box
  // into the proper SVG box.
  // |aMayHaveCyclicDependency| is used for stroke-box to avoid the cyclic
  // dependency if any of its descendants uses non-scaling-stroke.
  enum class MayHaveNonScalingStrokeCyclicDependency : bool { No, Yes };
  static nsRect ComputeSVGReferenceRect(
      nsIFrame*, StyleGeometryBox,
      MayHaveNonScalingStrokeCyclicDependency =
          MayHaveNonScalingStrokeCyclicDependency::No);

  // Compute the geometry box for CSS layout. The caller should map the SVG box
  // into the proper CSS box.
  static nsRect ComputeHTMLReferenceRect(const nsIFrame*, StyleGeometryBox);

  static nsRect ComputeClipPathGeometryBox(
      nsIFrame*, const mozilla::StyleShapeGeometryBox&);

  static nsPoint ComputeOffsetToUserSpace(nsDisplayListBuilder* aBuilder,
                                          nsIFrame* aFrame);

  // Callers are responsible to ensure the user-font-set is up-to-date if
  // aUseUserFontSet is true.
  static already_AddRefed<nsFontMetrics> GetMetricsFor(
      nsPresContext* aPresContext, bool aIsVertical,
      const nsStyleFont* aStyleFont, mozilla::Length aFontSize,
      bool aUseUserFontSet);

  static void ComputeSystemFont(nsFont* aSystemFont,
                                mozilla::StyleSystemFont aFontID,
                                const nsFont& aDefaultVariableFont,
                                const mozilla::dom::Document* aDocument);

  static uint32_t ParseFontLanguageOverride(const nsAString& aLangTag);

  /**
   * Returns true if there are any preferences or overrides that indicate a
   * need to handle <meta name="viewport"> tags.
   */
  static bool ShouldHandleMetaViewport(const mozilla::dom::Document*);

  /**
   * Resolve a CSS <length-percentage> value to a definite size.
   */
  template <bool clampNegativeResultToZero>
  static nscoord ResolveToLength(const LengthPercentage& aLengthPercentage,
                                 nscoord aPercentageBasis) {
    nscoord value = (aPercentageBasis == NS_UNCONSTRAINEDSIZE)
                        ? aLengthPercentage.Resolve(0)
                        : aLengthPercentage.Resolve(aPercentageBasis);
    return clampNegativeResultToZero ? std::max(0, value) : value;
  }

  /**
   * Resolve a column-gap/row-gap to a definite size.
   * @note This method resolves 'normal' to zero.
   *   Callers who want different behavior should handle 'normal' on their own.
   */
  static nscoord ResolveGapToLength(
      const mozilla::NonNegativeLengthPercentageOrNormal& aGap,
      nscoord aPercentageBasis) {
    if (aGap.IsNormal()) {
      return nscoord(0);
    }
    return ResolveToLength<true>(aGap.AsLengthPercentage(), aPercentageBasis);
  }

  /**
   * Get the computed style from which the scrollbar style should be
   * used for the given scrollbar part frame.
   */
  static ComputedStyle* StyleForScrollbar(const nsIFrame* aScrollbarPart);

  /**
   * Returns true if |aFrame| is scrolled out of view by a scrollable element in
   * a cross-process ancestor document.
   * Note this function only works for frames in out-of-process iframes.
   **/
  static bool FrameRectIsScrolledOutOfViewInCrossProcess(
      const nsIFrame* aFrame, const nsRect& aFrameRect);

  /**
   * Similar to above FrameIsScrolledOutViewInCrossProcess but returns true even
   * if |aFrame| is not fully scrolled out of view and its visible area width or
   * height is smaller than |aMargin|.
   **/
  static bool FrameIsMostlyScrolledOutOfViewInCrossProcess(
      const nsIFrame* aFrame, nscoord aMargin);

  /**
   * Expand the height of |aSize| to the size of `vh` units.
   *
   * With dynamic toolbar(s) the height for `vh` units is greater than the
   * ICB height, we need to expand it in some places.
   **/
  static nsSize ExpandHeightForViewportUnits(nsPresContext* aPresContext,
                                             const nsSize& aSize);

  static CSSSize ExpandHeightForDynamicToolbar(
      const nsPresContext* aPresContext, const CSSSize& aSize);
  static nsSize ExpandHeightForDynamicToolbar(const nsPresContext* aPresContext,
                                              const nsSize& aSize);

  /**
   * Returns the nsIFrame which clips overflow regions of the given |aFrame|.
   * Note CSS clip or clip-path isn't accounted for.
   **/
  static nsIFrame* GetNearestOverflowClipFrame(nsIFrame* aFrame);

  /*
   * Returns true if the user's preferences allow for smooth scrolling.
   */
  static bool IsSmoothScrollingEnabled();

  /*
   * Recompute the default value of general.smoothScroll based on
   * the system settings for prefers-reduced-motion.
   *
   * Note: Must only be called from the main thread.
   */
  static void RecomputeSmoothScrollDefault();

 private:
  /**
   * Helper function for LogTestDataForPaint().
   */
  static void DoLogTestDataForPaint(
      mozilla::layers::WebRenderLayerManager* aManager, ViewID aScrollId,
      const std::string& aKey, const std::string& aValue);

  static bool IsAPZTestLoggingEnabled();

  static void ConstrainToCoordValues(gfxFloat& aStart, gfxFloat& aSize);
  static void ConstrainToCoordValues(float& aStart, float& aSize);
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsLayoutUtils::PaintFrameFlags)
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsLayoutUtils::GetPopupFrameForPointFlags)

template <typename PointType, typename RectType, typename CoordType>
/* static */ bool nsLayoutUtils::PointIsCloserToRect(
    PointType aPoint, const RectType& aRect, CoordType& aClosestXDistance,
    CoordType& aClosestYDistance) {
  CoordType fromLeft = aPoint.x - aRect.x;
  CoordType fromRight = aPoint.x - aRect.XMost();

  CoordType xDistance;
  if (fromLeft >= 0 && fromRight <= 0) {
    xDistance = 0;
  } else {
    xDistance = std::min(abs(fromLeft), abs(fromRight));
  }

  if (xDistance <= aClosestXDistance) {
    if (xDistance < aClosestXDistance) {
      aClosestYDistance = std::numeric_limits<CoordType>::max();
    }

    CoordType fromTop = aPoint.y - aRect.y;
    CoordType fromBottom = aPoint.y - aRect.YMost();

    CoordType yDistance;
    if (fromTop >= 0 && fromBottom <= 0) {
      yDistance = 0;
    } else {
      yDistance = std::min(abs(fromTop), abs(fromBottom));
    }

    if (yDistance < aClosestYDistance) {
      aClosestXDistance = xDistance;
      aClosestYDistance = yDistance;
      return true;
    }
  }

  return false;
}

template <typename T>
nsRect nsLayoutUtils::RoundGfxRectToAppRect(const T& aRect,
                                            const float aFactor) {
  // Get a new Rect whose units are app units by scaling by the specified
  // factor.
  T scaledRect = aRect;
  scaledRect.ScaleRoundOut(aFactor);

  // We now need to constrain our results to the max and min values for coords.
  ConstrainToCoordValues(scaledRect.x, scaledRect.width);
  ConstrainToCoordValues(scaledRect.y, scaledRect.height);

  if (!aRect.Width()) {
    scaledRect.SetWidth(0);
  }

  if (!aRect.Height()) {
    scaledRect.SetHeight(0);
  }

  // Now typecast everything back.  This is guaranteed to be safe.
  return nsRect(nscoord(scaledRect.X()), nscoord(scaledRect.Y()),
                nscoord(scaledRect.Width()), nscoord(scaledRect.Height()));
}

namespace mozilla {

/**
 * Converts an nsPoint in app units to a Moz2D Point in pixels (whether those
 * are device pixels or CSS px depends on what the caller chooses to pass as
 * aAppUnitsPerPixel).
 */
inline gfx::Point NSPointToPoint(const nsPoint& aPoint,
                                 int32_t aAppUnitsPerPixel) {
  return gfx::Point(gfx::Float(aPoint.x) / aAppUnitsPerPixel,
                    gfx::Float(aPoint.y) / aAppUnitsPerPixel);
}

/**
 * Converts an nsRect in app units to a Moz2D Rect in pixels (whether those
 * are device pixels or CSS px depends on what the caller chooses to pass as
 * aAppUnitsPerPixel).
 */
gfx::Rect NSRectToRect(const nsRect& aRect, double aAppUnitsPerPixel);

/**
 * Converts an nsRect in app units to a Moz2D Rect in pixels (whether those
 * are device pixels or CSS px depends on what the caller chooses to pass as
 * aAppUnitsPerPixel).
 *
 * The passed DrawTarget is used to additionally snap the returned Rect to
 * device pixels, if appropriate (as decided and carried out by Moz2D's
 * MaybeSnapToDevicePixels helper, which this function calls to do any
 * snapping).
 */
gfx::Rect NSRectToSnappedRect(const nsRect& aRect, double aAppUnitsPerPixel,
                              const gfx::DrawTarget& aSnapDT);

/**
 * Converts, where possible, an nsRect in app units to a Moz2D Rect in pixels
 * (whether those are device pixels or CSS px depends on what the caller
 *  chooses to pass as aAppUnitsPerPixel).
 *
 * If snapping results in a rectangle with zero width or height, the affected
 * coordinates are left unsnapped
 */
gfx::Rect NSRectToNonEmptySnappedRect(const nsRect& aRect,
                                      double aAppUnitsPerPixel,
                                      const gfx::DrawTarget& aSnapDT);

void StrokeLineWithSnapping(
    const nsPoint& aP1, const nsPoint& aP2, int32_t aAppUnitsPerDevPixel,
    gfx::DrawTarget& aDrawTarget, const gfx::Pattern& aPattern,
    const gfx::StrokeOptions& aStrokeOptions = gfx::StrokeOptions(),
    const gfx::DrawOptions& aDrawOptions = gfx::DrawOptions());

namespace layout {

/**
 * An RAII class which will, for the duration of its lifetime,
 * **if** the frame given is a container for font size inflation,
 * set the current inflation container on the pres context to null
 * (and then, in its destructor, restore the old value).
 */
class AutoMaybeDisableFontInflation {
 public:
  explicit AutoMaybeDisableFontInflation(nsIFrame* aFrame);

  ~AutoMaybeDisableFontInflation();

 private:
  nsPresContext* mPresContext;
  bool mOldValue;
};

}  // namespace layout
}  // namespace mozilla

class nsSetAttrRunnable : public mozilla::Runnable {
 public:
  nsSetAttrRunnable(mozilla::dom::Element* aElement, nsAtom* aAttrName,
                    const nsAString& aValue);
  nsSetAttrRunnable(mozilla::dom::Element* aElement, nsAtom* aAttrName,
                    int32_t aValue);

  NS_DECL_NSIRUNNABLE

  RefPtr<mozilla::dom::Element> mElement;
  RefPtr<nsAtom> mAttrName;
  nsAutoString mValue;
};

class nsUnsetAttrRunnable : public mozilla::Runnable {
 public:
  nsUnsetAttrRunnable(mozilla::dom::Element* aElement, nsAtom* aAttrName);

  NS_DECL_NSIRUNNABLE

  RefPtr<mozilla::dom::Element> mElement;
  RefPtr<nsAtom> mAttrName;
};

// This class allows you to easily set any pointer variable and ensure it's
// set to nullptr when leaving its scope.
template <typename T>
class MOZ_RAII SetAndNullOnExit {
 public:
  SetAndNullOnExit(T*& aVariable, T* aValue) {
    aVariable = aValue;
    mVariable = &aVariable;
  }
  ~SetAndNullOnExit() { *mVariable = nullptr; }

 private:
  T** mVariable;
};

#endif  // nsLayoutUtils_h__
