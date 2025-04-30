/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* interface for all rendering objects */

#ifndef nsIFrame_h___
#define nsIFrame_h___

#ifndef MOZILLA_INTERNAL_API
#error This header/class should only be used within Mozilla code. It should not be used by extensions.
#endif

#if (defined(XP_WIN) && !defined(HAVE_64BIT_BUILD)) || defined(ANDROID)
// Blink's magic depth limit from its HTML parser (513) plus as much as fits in
// the default run-time stack on armv7 Android on Dalvik when using display:
// block minus a bit just to be sure. The Dalvik default stack crashes at 588.
// ART can do a few frames more. Using the same number for 32-bit Windows for
// consistency. Over there, Blink's magic depth of 513 doesn't fit in the
// default stack of 1 MB, but this magic depth fits when the default is grown by
// mere 192 KB (tested in 64 KB increments).
//
// 32-bit Windows has a different limit compared to 64-bit desktop, because the
// default stack size affects all threads and consumes address space. Fixing
// that is bug 1257522.
//
// 32-bit Android on ARM already happens to have defaults that are close enough
// to what makes sense as a temporary measure on Windows, so adjusting the
// Android stack can be a follow-up. The stack on 64-bit ARM needs adjusting in
// any case before 64-bit ARM can become tier-1. See bug 1400811.
//
// Ideally, we'd get rid of this smaller limit and make 32-bit Windows and
// Android capable of working with the Linux/Mac/Win64 number below.
#  define MAX_REFLOW_DEPTH 585
#else
// Blink's magic depth limit from its HTML parser times two. Also just about
// fits within the system default runtime stack limit of 8 MB on 64-bit Mac and
// Linux with display: table-cell.
#  define MAX_REFLOW_DEPTH 1026
#endif

/* nsIFrame is in the process of being deCOMtaminated, i.e., this file is
   eventually going to be eliminated, and all callers will use nsFrame instead.
   At the moment we're midway through this process, so you will see inlined
   functions and member variables in this file.  -dwh */

#include <algorithm>
#include <stdio.h>

#include "FrameProperties.h"
#include "LayoutConstants.h"
#include "mozilla/AspectRatio.h"
#include "mozilla/Attributes.h"
#include "mozilla/Baseline.h"
#include "mozilla/EnumSet.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/RelativeTo.h"
#include "mozilla/Result.h"
#include "mozilla/SmallPointerArray.h"
#include "mozilla/ToString.h"
#include "mozilla/WritingModes.h"
#include "nsDirection.h"
#include "nsFrameList.h"
#include "nsFrameState.h"
#include "mozilla/ReflowInput.h"
#include "nsIContent.h"
#include "nsITheme.h"
#include "nsQueryFrame.h"
#include "mozilla/ComputedStyle.h"
#include "nsStyleStruct.h"
#include "Visibility.h"
#include "nsChangeHint.h"
#include "mozilla/EnumSet.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/CompositorHitTestInfo.h"
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsDisplayItemTypes.h"
#include "nsPresContext.h"
#include "nsTHashSet.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/AccTypes.h"
#endif

/**
 * New rules of reflow:
 * 1. you get a WillReflow() followed by a Reflow() followed by a DidReflow() in
 *    order (no separate pass over the tree)
 * 2. it's the parent frame's responsibility to size/position the child's view
 *    (not the child frame's responsibility as it is today) during reflow (and
 *    before sending the DidReflow() notification)
 * 3. positioning of child frames (and their views) is done on the way down the
 *    tree, and sizing of child frames (and their views) on the way back up
 * 4. if you move a frame (outside of the reflow process, or after reflowing
 *    it), then you must make sure that its view (or its child frame's views)
 *    are re-positioned as well. It's reasonable to not position the view until
 *    after all reflowing the entire line, for example, but the frame should
 *    still be positioned and sized (and the view sized) during the reflow
 *    (i.e., before sending the DidReflow() notification)
 * 5. the view system handles moving of widgets, i.e., it's not our problem
 */

class nsAtom;
class nsView;
class nsFrameSelection;
class nsIWidget;
class nsISelectionController;
class nsILineIterator;
class gfxSkipChars;
class gfxSkipCharsIterator;
class gfxContext;
class nsLineLink;
template <typename Link, bool>
class GenericLineListIterator;
using LineListIterator = GenericLineListIterator<nsLineLink, false>;
class nsAbsoluteContainingBlock;
class nsContainerFrame;
class nsPlaceholderFrame;
class nsStyleChangeList;
class nsViewManager;
class nsWindowSizes;

struct CharacterDataChangeInfo;

namespace mozilla {

enum class CaretAssociationHint;
enum class IsFocusableFlags : uint8_t;
enum class PeekOffsetOption : uint16_t;
enum class PseudoStyleType : uint8_t;
enum class TableSelectionMode : uint32_t;

class EffectSet;
class LazyLogModule;
class nsDisplayItem;
class nsDisplayList;
class nsDisplayListBuilder;
class nsDisplayListSet;
class PresShell;
class ScrollContainerFrame;
class ServoRestyleState;
class WidgetGUIEvent;
class WidgetMouseEvent;

struct PeekOffsetStruct;

namespace layers {
class Layer;
class LayerManager;
}  // namespace layers

namespace layout {
class ScrollAnchorContainer;
}  // namespace layout

}  // namespace mozilla

//----------------------------------------------------------------------

// 1 million CSS pixels less than our max app unit measure.
// For reflowing with an "infinite" available inline space per [css-sizing].
// (reflowing with an NS_UNCONSTRAINEDSIZE available inline size isn't allowed
//  and leads to assertions)
#define INFINITE_ISIZE_COORD nscoord(NS_MAXSIZE - (1000000 * 60))

//----------------------------------------------------------------------

namespace mozilla {

enum class LayoutFrameType : uint8_t {
#define FRAME_TYPE(ty_, ...) ty_,
#include "mozilla/FrameTypeList.h"
#undef FRAME_TYPE
};

}  // namespace mozilla

enum nsSelectionAmount {
  eSelectCharacter = 0,  // a single Unicode character;
                         // do not use this (prefer Cluster) unless you
                         // are really sure it's what you want
  eSelectCluster = 1,    // a grapheme cluster: this is usually the right
                         // choice for movement or selection by "character"
                         // as perceived by the user
  eSelectWord = 2,
  eSelectWordNoSpace = 3,  // select a "word" without selecting the following
                           // space, no matter what the default platform
                           // behavior is
  eSelectLine = 4,         // previous drawn line in flow.
  // NOTE that selection code depends on the ordering of the above values,
  // allowing simple <= tests to check categories of caret movement.
  // Don't rearrange without checking the usage in nsSelection.cpp!

  eSelectBeginLine = 5,
  eSelectEndLine = 6,
  eSelectNoAmount = 7,  // just bounce back current offset.
  eSelectParagraph = 8  // select a "paragraph"
};

//----------------------------------------------------------------------
// Reflow status returned by the Reflow() methods.
class nsReflowStatus final {
 public:
  nsReflowStatus()
      : mFloatClearType(mozilla::UsedClear::None),
        mInlineBreak(InlineBreak::None),
        mCompletion(Completion::FullyComplete),
        mNextInFlowNeedsReflow(false),
        mFirstLetterComplete(false) {}

  // Reset all the member variables.
  void Reset() {
    mFloatClearType = mozilla::UsedClear::None;
    mInlineBreak = InlineBreak::None;
    mCompletion = Completion::FullyComplete;
    mNextInFlowNeedsReflow = false;
    mFirstLetterComplete = false;
  }

  // Return true if all member variables have their default values.
  bool IsEmpty() const {
    return (IsFullyComplete() && !IsInlineBreak() && !mNextInFlowNeedsReflow &&
            !mFirstLetterComplete);
  }

  // There are three possible completion statuses, represented by
  // mCompletion.
  //
  // Incomplete means the frame does *not* map all its content, and the
  // parent frame should create a continuing frame.
  //
  // OverflowIncomplete means that the frame has an overflow that is not
  // complete, but its own box is complete. (This happens when the content
  // overflows a fixed-height box.) The reflower should place and size the
  // frame and continue its reflow, but it needs to create an overflow
  // container as a continuation for this frame. See "Overflow containers"
  // documentation in nsContainerFrame.h for more information.
  //
  // FullyComplete means the frame is neither Incomplete nor
  // OverflowIncomplete. This is the default state for a nsReflowStatus.
  //
  enum class Completion : uint8_t {
    // The order of the enum values is important, which represents the
    // precedence when merging.
    FullyComplete,
    OverflowIncomplete,
    Incomplete,
  };

  bool IsIncomplete() const { return mCompletion == Completion::Incomplete; }
  bool IsOverflowIncomplete() const {
    return mCompletion == Completion::OverflowIncomplete;
  }
  bool IsFullyComplete() const {
    return mCompletion == Completion::FullyComplete;
  }
  // Just for convenience; not a distinct state.
  bool IsComplete() const { return !IsIncomplete(); }

  void SetIncomplete() { mCompletion = Completion::Incomplete; }
  void SetOverflowIncomplete() { mCompletion = Completion::OverflowIncomplete; }

  // mNextInFlowNeedsReflow bit flag means that the next-in-flow is dirty,
  // and also needs to be reflowed. This status only makes sense for a frame
  // that is not complete, i.e. you wouldn't set mNextInFlowNeedsReflow when
  // IsComplete() is true.
  bool NextInFlowNeedsReflow() const { return mNextInFlowNeedsReflow; }
  void SetNextInFlowNeedsReflow() { mNextInFlowNeedsReflow = true; }

  // Merge the frame completion status bits from aStatus into this.
  void MergeCompletionStatusFrom(const nsReflowStatus& aStatus) {
    if (mCompletion < aStatus.mCompletion) {
      mCompletion = aStatus.mCompletion;
    }

    // These asserts ensure that the mCompletion merging works as we expect.
    // (Incomplete beats OverflowIncomplete, which beats FullyComplete.)
    static_assert(
        Completion::Incomplete > Completion::OverflowIncomplete &&
            Completion::OverflowIncomplete > Completion::FullyComplete,
        "mCompletion merging won't work without this!");

    mNextInFlowNeedsReflow |= aStatus.mNextInFlowNeedsReflow;
  }

  // There are three possible inline-break statuses, represented by
  // mInlineBreak.
  //
  // "None" means no break is requested.
  // "Before" means the break should occur before the frame.
  // "After" means the break should occur after the frame.
  // (Here, "the frame" is the frame whose reflow results are being reported by
  // this nsReflowStatus.)
  //
  enum class InlineBreak : uint8_t {
    None,
    Before,
    After,
  };

  bool IsInlineBreak() const { return mInlineBreak != InlineBreak::None; }
  bool IsInlineBreakBefore() const {
    return mInlineBreak == InlineBreak::Before;
  }
  bool IsInlineBreakAfter() const { return mInlineBreak == InlineBreak::After; }
  mozilla::UsedClear FloatClearType() const { return mFloatClearType; }

  // Set the inline line-break-before status, and reset other bit flags. Note
  // that other frame completion status isn't expected to matter after calling
  // this method.
  //
  // Here's one scenario where a child frame would report this status. Suppose
  // the child has "break-inside:avoid" in its style, and the child (and its
  // content) won't fit in the available block-size. This child would want to
  // report this status so that it gets pushed (in its entirety) to the next
  // column/page where it will hopefully fit.
  void SetInlineLineBreakBeforeAndReset() {
    Reset();
    mFloatClearType = mozilla::UsedClear::None;
    mInlineBreak = InlineBreak::Before;
  }

  // Set the inline line-break-after status. The clear type can be changed
  // via the optional aClearType param.
  void SetInlineLineBreakAfter(
      mozilla::UsedClear aClearType = mozilla::UsedClear::None) {
    mFloatClearType = aClearType;
    mInlineBreak = InlineBreak::After;
  }

  // mFirstLetterComplete bit flag means the break was induced by
  // completion of a first-letter.
  bool FirstLetterComplete() const { return mFirstLetterComplete; }
  void SetFirstLetterComplete() { mFirstLetterComplete = true; }

 private:
  mozilla::UsedClear mFloatClearType;
  InlineBreak mInlineBreak;
  Completion mCompletion;
  bool mNextInFlowNeedsReflow : 1;
  bool mFirstLetterComplete : 1;
};

// Convert nsReflowStatus to a human-readable string.
std::ostream& operator<<(std::ostream& aStream, const nsReflowStatus& aStatus);

namespace mozilla {

// Loosely: https://drafts.csswg.org/css-align-3/#shared-alignment-context
enum class AlignmentContext {
  Inline,
  Table,
  Flexbox,
  Grid,
};

/*
 * For replaced elements only. Gets the intrinsic dimensions of this element,
 * which can be specified on a per-axis basis.
 */
struct IntrinsicSize {
  Maybe<nscoord> width;
  Maybe<nscoord> height;

  IntrinsicSize() = default;

  IntrinsicSize(nscoord aWidth, nscoord aHeight)
      : width(Some(aWidth)), height(Some(aHeight)) {}

  explicit IntrinsicSize(const nsSize& aSize)
      : IntrinsicSize(aSize.Width(), aSize.Height()) {}

  Maybe<nsSize> ToSize() const {
    return width && height ? Some(nsSize(*width, *height)) : Nothing();
  }

  Maybe<nscoord>& ISize(WritingMode aWM) {
    return aWM.IsVertical() ? height : width;
  }
  const Maybe<nscoord>& ISize(WritingMode aWM) const {
    return aWM.IsVertical() ? height : width;
  }

  Maybe<nscoord>& BSize(WritingMode aWM) {
    return aWM.IsVertical() ? width : height;
  }
  const Maybe<nscoord>& BSize(WritingMode aWM) const {
    return aWM.IsVertical() ? width : height;
  }

  void Zoom(const StyleZoom& aZoom) {
    if (width) {
      *width = aZoom.ZoomCoord(*width);
    }
    if (height) {
      *height = aZoom.ZoomCoord(*height);
    }
  }

  bool operator==(const IntrinsicSize& rhs) const {
    return width == rhs.width && height == rhs.height;
  }
  bool operator!=(const IntrinsicSize& rhs) const { return !(*this == rhs); }
};

// Pseudo bidi embedding level indicating nonexistence.
constexpr mozilla::intl::BidiEmbeddingLevel kBidiLevelNone(0xff);

struct FrameBidiData {
  mozilla::intl::BidiEmbeddingLevel baseLevel;
  mozilla::intl::BidiEmbeddingLevel embeddingLevel;
  // The embedding level of virtual bidi formatting character before
  // this frame if any. kBidiLevelNone is used to indicate nonexistence
  // or unnecessity of such virtual character.
  mozilla::intl::BidiEmbeddingLevel precedingControl;
};

// A struct aggregates necessary data to compute the intrinsic sizes for a
// frame, typically the frame whose intrinsic size contribution is being
// requested. This struct is used as an input for GetMinISize(), GetPrefISize(),
// IntrinsicISize(), and others.
struct MOZ_STACK_CLASS IntrinsicSizeInput final {
  gfxContext* const mContext;

  // The content-box size of a frame's containing block (in the frame's own
  // writing mode), used as a percentage basis for percentage-based sizes on the
  // frame itself that contribute to its intrinsic size. For example, in grid
  // layout, a percentage value of min-height be can transferred through the
  // aspect-ratio to determine auto repeat columns specified in
  // grid-template-columns.
  //
  // Note: it is acceptable for mContainingBlockSize to be Nothing() as long as
  // the frame doesn't have percentage-based value for properties that need to
  // be resolved in order to compute its intrinsic size.
  Maybe<LogicalSize> mContainingBlockSize;

  // The content-box size of a frame (in the frame's own writing mode), served
  // as a percentage basis when computing the children's intrinsic
  // contributions. If the basis is indefinite in a given axis, use
  // NS_UNCONSTRAINEDSIZE for that component. If the value is Nothing, it is
  // semantically equivalent to NS_UNCONSTRAINEDSIZE in both axes.
  //
  // In most scenarios, this struct is used when computing the inline size
  // contribution, so the inline component of the percentage basis should be set
  // to NS_UNCONSTRAINEDSIZE.
  Maybe<LogicalSize> mPercentageBasisForChildren;

  bool HasSomePercentageBasisForChildren() const {
    return mPercentageBasisForChildren &&
           !mPercentageBasisForChildren->IsAllValues(NS_UNCONSTRAINEDSIZE);
  }

  IntrinsicSizeInput(gfxContext* aContext,
                     const Maybe<LogicalSize>& aContainingBlockSize,
                     const Maybe<LogicalSize>& aPercentageBasisForChildren)
      : mContext(aContext),
        mContainingBlockSize(aContainingBlockSize),
        mPercentageBasisForChildren(aPercentageBasisForChildren) {
    MOZ_ASSERT(mContext);
  }

  // Construct a new IntrinsicSizeInput for children by copying from
  // aParentInput.
  //
  // Note: since this constructor creates an IntrinsicSizeInput for the
  // children, it does not copy mContainingBlockSize from aParentInput.
  //
  // This constructor converts mPercentageBasisForChildren's writing mode, if it
  // exists. The original mPercentageBasis in aSource is expected to be in the
  // writing mode aFromWM, and it will be converted to the writing mode aToWM.
  IntrinsicSizeInput(const IntrinsicSizeInput& aParentInput,
                     mozilla::WritingMode aToWM, mozilla::WritingMode aFromWM)
      : IntrinsicSizeInput(
            aParentInput.mContext, Nothing(),
            aParentInput.mPercentageBasisForChildren.map([&](const auto& aPB) {
              return aPB.ConvertTo(aToWM, aFromWM);
            })) {}
};

}  // namespace mozilla

/// Generic destructor for frame properties. Calls delete.
template <typename T>
static void DeleteValue(T* aPropertyValue) {
  delete aPropertyValue;
}

/// Generic destructor for frame properties. Calls Release().
template <typename T>
static void ReleaseValue(T* aPropertyValue) {
  aPropertyValue->Release();
}

//----------------------------------------------------------------------

/**
 * nsIFrame logging constants. We redefine the nspr
 * PRLogModuleInfo.level field to be a bitfield.  Each bit controls a
 * specific type of logging. Each logging operation has associated
 * inline methods defined below.
 *
 * Due to the redefinition of the level field we cannot use MOZ_LOG directly
 * as that will cause assertions due to invalid log levels.
 */
#define NS_FRAME_TRACE_CALLS 0x1
#define NS_FRAME_TRACE_PUSH_PULL 0x2
#define NS_FRAME_TRACE_CHILD_REFLOW 0x4
#define NS_FRAME_TRACE_NEW_FRAMES 0x8

#define NS_FRAME_LOG_TEST(_lm, _bit) \
  (int(((mozilla::LogModule*)(_lm))->Level()) & (_bit))

#ifdef DEBUG
#  define NS_FRAME_LOG(_bit, _args)                           \
    PR_BEGIN_MACRO                                            \
    if (NS_FRAME_LOG_TEST(nsIFrame::sFrameLogModule, _bit)) { \
      printf_stderr _args;                                    \
    }                                                         \
    PR_END_MACRO
#else
#  define NS_FRAME_LOG(_bit, _args)
#endif

// XXX Need to rework this so that logging is free when it's off
#ifdef DEBUG
#  define NS_FRAME_TRACE_IN(_method) Trace(_method, true)

#  define NS_FRAME_TRACE_OUT(_method) Trace(_method, false)

#  define NS_FRAME_TRACE(_bit, _args)                         \
    PR_BEGIN_MACRO                                            \
    if (NS_FRAME_LOG_TEST(nsIFrame::sFrameLogModule, _bit)) { \
      TraceMsg _args;                                         \
    }                                                         \
    PR_END_MACRO

#  define NS_FRAME_TRACE_REFLOW_IN(_method) Trace(_method, true)

#  define NS_FRAME_TRACE_REFLOW_OUT(_method, _status) \
    Trace(_method, false, _status)

#else
#  define NS_FRAME_TRACE(_bits, _args)
#  define NS_FRAME_TRACE_IN(_method)
#  define NS_FRAME_TRACE_OUT(_method)
#  define NS_FRAME_TRACE_REFLOW_IN(_method)
#  define NS_FRAME_TRACE_REFLOW_OUT(_method, _status)
#endif

//----------------------------------------------------------------------

// Frame allocation boilerplate macros. Every subclass of nsFrame must
// either use NS_{DECL,IMPL}_FRAMEARENA_HELPERS pair for allocating
// memory correctly, or use NS_DECL_ABSTRACT_FRAME to declare a frame
// class abstract and stop it from being instantiated. If a frame class
// without its own operator new and GetFrameId gets instantiated, the
// per-frame recycler lists in nsPresArena will not work correctly,
// with potentially catastrophic consequences (not enough memory is
// allocated for a frame object).

#define NS_DECL_FRAMEARENA_HELPERS(class)                                      \
  NS_DECL_QUERYFRAME_TARGET(class)                                             \
  static constexpr nsIFrame::ClassID kClassID = nsIFrame::ClassID::class##_id; \
  void* operator new(size_t, mozilla::PresShell*) MOZ_MUST_OVERRIDE;           \
  nsQueryFrame::FrameIID GetFrameId() const override MOZ_MUST_OVERRIDE {       \
    return nsQueryFrame::class##_id;                                           \
  }

#define NS_IMPL_FRAMEARENA_HELPERS(class)                              \
  void* class ::operator new(size_t sz, mozilla::PresShell * aShell) { \
    return aShell->AllocateFrame(nsQueryFrame::class##_id, sz);        \
  }

#define NS_DECL_ABSTRACT_FRAME(class)                                         \
  void* operator new(size_t, mozilla::PresShell*) MOZ_MUST_OVERRIDE = delete; \
  nsQueryFrame::FrameIID GetFrameId() const override MOZ_MUST_OVERRIDE = 0;

//----------------------------------------------------------------------

namespace mozilla {

// A simple class to group stuff that we need to keep around when tearing down
// a frame tree.
//
// Native anonymous content created by the frames need to get unbound _after_
// the frame has been destroyed, see bug 1400618.
//
// We destroy the anonymous content bottom-up (so, in reverse order), because
// it's a bit simpler, though we generally don't have that much nested anonymous
// content (except for scrollbars).
struct MOZ_RAII FrameDestroyContext {
  explicit FrameDestroyContext(PresShell* aPs) : mPresShell(aPs) {}

  void AddAnonymousContent(already_AddRefed<nsIContent>&& aContent) {
    if (RefPtr<nsIContent> content = aContent) {
      mAnonymousContent.AppendElement(std::move(content));
    }
  }

  ~FrameDestroyContext();

 private:
  PresShell* const mPresShell;
  AutoTArray<RefPtr<nsIContent>, 100> mAnonymousContent;
};

/**
 * Bit-flags specific to a given layout class id.
 */
enum class LayoutFrameClassFlags : uint16_t {
  None = 0,
  Leaf = 1 << 0,
  LeafDynamic = 1 << 1,
  MathML = 1 << 2,
  SVG = 1 << 3,
  SVGContainer = 1 << 4,
  BidiInlineContainer = 1 << 5,
  // The frame is for a replaced element, such as an image
  Replaced = 1 << 6,
  // A replaced element that has replaced-element sizing characteristics (i.e.,
  // like images or iframes), as opposed to inline-block sizing characteristics
  // (like form controls).
  ReplacedSizing = 1 << 7,
  // A frame that participates in inline reflow, i.e., one that requires
  // ReflowInput::mLineLayout.
  LineParticipant = 1 << 8,
  // Whether this frame is a table part (but not a table or table wrapper).
  TablePart = 1 << 9,
  CanContainOverflowContainers = 1 << 10,
  // Whether the frame supports CSS transforms.
  SupportsCSSTransforms = 1 << 11,
  // Whether this frame class supports 'contain: layout' and 'contain: paint'
  // (supporting one is equivalent to supporting the other).
  SupportsContainLayoutAndPaint = 1 << 12,
  // Whether this frame class supports the `aspect-ratio` property.
  SupportsAspectRatio = 1 << 13,
  // Whether this frame class is always a BFC.
  BlockFormattingContext = 1 << 14,
  // Whether we're a SVG rendering observer container.
  SVGRenderingObserverContainer = 1 << 15,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(LayoutFrameClassFlags)

}  // namespace mozilla

/**
 * A frame in the layout model. This interface is supported by all frame
 * objects.
 *
 * Frames can have multiple child lists: the default child list
 * (referred to as the <i>principal</i> child list, and additional named
 * child lists. There is an ordering of frames within a child list, but
 * there is no order defined between frames in different child lists of
 * the same parent frame.
 *
 * Frames are NOT reference counted. Use the Destroy() member function
 * to destroy a frame. The lifetime of the frame hierarchy is bounded by the
 * lifetime of the presentation shell which owns the frames.
 *
 * nsIFrame is a private Gecko interface. If you are not Gecko then you
 * should not use it. If you're not in layout, then you won't be able to
 * link to many of the functions defined here. Too bad.
 *
 * If you're not in layout but you must call functions in here, at least
 * restrict yourself to calling virtual methods, which won't hurt you as badly.
 */
class nsIFrame : public nsQueryFrame {
 public:
  using AlignmentContext = mozilla::AlignmentContext;
  using BaselineSharingGroup = mozilla::BaselineSharingGroup;
  using BaselineExportContext = mozilla::BaselineExportContext;
  template <typename T>
  using Maybe = mozilla::Maybe<T>;
  template <typename T, typename E>
  using Result = mozilla::Result<T, E>;
  using Nothing = mozilla::Nothing;
  using OnNonvisible = mozilla::OnNonvisible;
  using ReflowInput = mozilla::ReflowInput;
  using ReflowOutput = mozilla::ReflowOutput;
  using Visibility = mozilla::Visibility;
  using ContentRelevancy = mozilla::ContentRelevancy;

  using nsDisplayItem = mozilla::nsDisplayItem;
  using nsDisplayList = mozilla::nsDisplayList;
  using nsDisplayListSet = mozilla::nsDisplayListSet;
  using nsDisplayListBuilder = mozilla::nsDisplayListBuilder;

  typedef mozilla::ComputedStyle ComputedStyle;
  typedef mozilla::FrameProperties FrameProperties;
  typedef mozilla::layers::LayerManager LayerManager;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::Matrix Matrix;
  typedef mozilla::gfx::Matrix4x4 Matrix4x4;
  typedef mozilla::gfx::Matrix4x4Flagged Matrix4x4Flagged;
  typedef mozilla::Sides Sides;
  typedef mozilla::LogicalSides LogicalSides;
  typedef mozilla::SmallPointerArray<nsDisplayItem> DisplayItemArray;

  typedef nsQueryFrame::ClassID ClassID;

  using ClassFlags = mozilla::LayoutFrameClassFlags;

 protected:
  using ChildList = mozilla::FrameChildList;
  using ChildListID = mozilla::FrameChildListID;
  using ChildListIDs = mozilla::FrameChildListIDs;

 public:
  // nsQueryFrame
  NS_DECL_QUERYFRAME
  NS_DECL_QUERYFRAME_TARGET(nsIFrame)

  explicit nsIFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                    ClassID aID)
      : mContent(nullptr),
        mComputedStyle(aStyle),
        mPresContext(aPresContext),
        mParent(nullptr),
        mNextSibling(nullptr),
        mPrevSibling(nullptr),
        mState(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY),
        mWritingMode(aStyle),
        mClass(aID),
        mMayHaveRoundedCorners(false),
        mHasImageRequest(false),
        mHasFirstLetterChild(false),
        mParentIsWrapperAnonBox(false),
        mIsWrapperBoxNeedingRestyle(false),
        mReflowRequestedForCharDataChange(false),
        mForceDescendIntoIfVisible(false),
        mBuiltDisplayList(false),
        mFrameIsModified(false),
        mHasModifiedDescendants(false),
        mHasOverrideDirtyRegion(false),
        mMayHaveWillChangeBudget(false),
#ifdef DEBUG
        mWasVisitedByAutoFrameConstructionPageName(false),
#endif
        mIsPrimaryFrame(false),
        mMayHaveTransformAnimation(false),
        mMayHaveOpacityAnimation(false),
        mAllDescendantsAreInvisible(false),
        mHasBSizeChange(false),
        mHasPaddingChange(false),
        mInScrollAnchorChain(false),
        mHasColumnSpanSiblings(false),
        mDescendantMayDependOnItsStaticPosition(false) {
    MOZ_ASSERT(mComputedStyle);
    MOZ_ASSERT(mPresContext);
    mozilla::PodZero(&mOverflow);
    MOZ_COUNT_CTOR(nsIFrame);
  }
  explicit nsIFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, ClassID::nsIFrame_id) {}

  nsPresContext* PresContext() const { return mPresContext; }

  mozilla::PresShell* PresShell() const { return PresContext()->PresShell(); }

  virtual nsQueryFrame::FrameIID GetFrameId() const MOZ_MUST_OVERRIDE {
    return kFrameIID;
  }

  /**
   * Called to initialize the frame. This is called immediately after creating
   * the frame.
   *
   * If the frame is a continuing frame, then aPrevInFlow indicates the previous
   * frame (the frame that was split).
   *
   * @param   aContent the content object associated with the frame
   * @param   aParent the parent frame
   * @param   aPrevInFlow the prev-in-flow frame
   */
  virtual void Init(nsIContent* aContent, nsContainerFrame* aParent,
                    nsIFrame* aPrevInFlow);

  void* operator new(size_t, mozilla::PresShell*) MOZ_MUST_OVERRIDE;

  using DestroyContext = mozilla::FrameDestroyContext;

  /**
   * Flags for PeekOffsetCharacter, PeekOffsetNoAmount, PeekOffsetWord return
   * values.
   */
  enum FrameSearchResult {
    // Peek found a appropriate offset within frame.
    FOUND = 0x00,
    // try next frame for offset.
    CONTINUE = 0x1,
    // offset not found because the frame was empty of text.
    CONTINUE_EMPTY = 0x2 | CONTINUE,
    // offset not found because the frame didn't contain any text that could be
    // selected.
    CONTINUE_UNSELECTABLE = 0x4 | CONTINUE,
  };

  /**
   * Options for PeekOffsetCharacter().
   */
  struct MOZ_STACK_CLASS PeekOffsetCharacterOptions {
    // Whether to restrict result to valid cursor locations (between grapheme
    // clusters) - if this is included, maintains "normal" behavior, otherwise,
    // used for selection by "code unit" (instead of "character")
    bool mRespectClusters;
    // Whether to check user-select style value - if this is included, checks
    // if user-select is all, then, it may return CONTINUE_UNSELECTABLE.
    bool mIgnoreUserStyleAll;

    PeekOffsetCharacterOptions()
        : mRespectClusters(true), mIgnoreUserStyleAll(false) {}
  };

  virtual void Destroy(DestroyContext&);

 protected:
  /**
   * Return true if the frame is part of a Selection.
   * Helper method to implement the public IsSelected() API.
   */
  virtual bool IsFrameSelected() const;

  template <class Source>
  friend class do_QueryFrameHelper;  // to read mClass
  friend class nsBlockFrame;         // for GetCaretBaseline
  friend class nsContainerFrame;     // for ReparentFrameViewTo

  virtual ~nsIFrame();

  // Overridden to prevent the global delete from being called, since
  // the memory came out of an arena instead of the heap.
  //
  // Ideally this would be private and undefined, like the normal
  // operator new.  Unfortunately, the C++ standard requires an
  // overridden operator delete to be accessible to any subclass that
  // defines a virtual destructor, so we can only make it protected;
  // worse, some C++ compilers will synthesize calls to this function
  // from the "deleting destructors" that they emit in case of
  // delete-expressions, so it can't even be undefined.
  void operator delete(void* aPtr, size_t sz);

 private:
  // Left undefined; nsFrame objects are never allocated from the heap.
  void* operator new(size_t sz) noexcept(true);

  // Returns true if this frame has any kind of CSS animations.
  bool HasCSSAnimations();

  // Returns true if this frame has any kind of CSS transitions.
  bool HasCSSTransitions();

 public:
  /**
   * Get the content object associated with this frame. Does not add a
   * reference.
   */
  [[nodiscard]] nsIContent* GetContent() const { return mContent; }

  [[nodiscard]] bool ContentIsRootOfNativeAnonymousSubtree() const {
    return mContent && mContent->IsRootOfNativeAnonymousSubtree();
  }

  [[nodiscard]] inline bool ContentIsEditable() const;

  /**
   * @brief Get the closest native anonymous subtree root if the content is in a
   * native anonymous subtree.
   *
   * @return The root of native anonymous subtree which the content belongs to.
   * Otherwise, nullptr.
   */
  nsIContent* GetClosestNativeAnonymousSubtreeRoot() const {
    return mContent ? mContent->GetClosestNativeAnonymousSubtreeRoot()
                    : nullptr;
  }

  /**
   * Get the frame that should be the parent for the frames of child elements
   * May return nullptr during reflow
   */
  virtual nsContainerFrame* GetContentInsertionFrame() { return nullptr; }

  /**
   * Move any frames on our overflow list to the end of our principal list.
   * @return true if there were any overflow frames
   */
  virtual bool DrainSelfOverflowList() { return false; }

  /**
   * Get the frame that should be scrolled if the content associated with this
   * frame is targeted for scrolling. For a scroll container frame, this will
   * just return the frame itself. For frames like nsTextControlFrame that
   * contain a scroll container frame, will return that scroll container frame.
   */
  virtual mozilla::ScrollContainerFrame* GetScrollTargetFrame() const {
    return nullptr;
  }

  /**
   * Get the offsets of the frame. most will be 0,0
   *
   */
  virtual std::pair<int32_t, int32_t> GetOffsets() const;

  /**
   * Reset the offsets when splitting frames during Bidi reordering
   *
   */
  virtual void AdjustOffsetsForBidi(int32_t aStart, int32_t aEnd) {}

  /**
   * Get the style associated with this frame.
   */
  ComputedStyle* Style() const { return mComputedStyle; }

  void AssertNewStyleIsSane(ComputedStyle&)
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      ;
#else
  {
  }
#endif

  void SetComputedStyle(ComputedStyle* aStyle) {
    if (aStyle != mComputedStyle) {
      AssertNewStyleIsSane(*aStyle);
      RefPtr<ComputedStyle> oldComputedStyle = std::move(mComputedStyle);
      mComputedStyle = aStyle;
      DidSetComputedStyle(oldComputedStyle);
    }
  }

  /**
   * SetComputedStyleWithoutNotification is for changes to the style
   * context that should suppress style change processing, in other
   * words, those that aren't really changes.  This generally means only
   * changes that happen during frame construction.
   */
  void SetComputedStyleWithoutNotification(ComputedStyle* aStyle) {
    if (aStyle != mComputedStyle) {
      mComputedStyle = aStyle;
    }
  }

 protected:
  // Style post processing hook
  // Attention: the old style is the one we're forgetting,
  // and hence possibly completely bogus for GetStyle* purposes.
  // Use PeekStyleData instead.
  virtual void DidSetComputedStyle(ComputedStyle* aOldComputedStyle);

 public:
/**
 * Define typesafe getter functions for each style struct by
 * preprocessing the list of style structs.  These functions are the
 * preferred way to get style data.  The macro creates functions like:
 *   const nsStyleBorder* StyleBorder();
 *   const nsStyleColor* StyleColor();
 *
 * Callers outside of libxul should use nsIDOMWindow::GetComputedStyle()
 * instead of these accessors.
 *
 * Callers can use Style*WithOptionalParam if they're in a function that
 * accepts an *optional* pointer the style struct.
 */
#define STYLE_STRUCT(name_)                                          \
  const nsStyle##name_* Style##name_() const MOZ_NONNULL_RETURN {    \
    NS_ASSERTION(mComputedStyle, "No style found!");                 \
    return mComputedStyle->Style##name_();                           \
  }                                                                  \
  const nsStyle##name_* Style##name_##WithOptionalParam(             \
      const nsStyle##name_* aStyleStruct) const MOZ_NONNULL_RETURN { \
    if (aStyleStruct) {                                              \
      MOZ_ASSERT(aStyleStruct == Style##name_());                    \
      return aStyleStruct;                                           \
    }                                                                \
    return Style##name_();                                           \
  }
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

  /** Also forward GetVisitedDependentColor to the style */
  template <typename T, typename S>
  nscolor GetVisitedDependentColor(T S::* aField) {
    return mComputedStyle->GetVisitedDependentColor(aField);
  }

  /**
   * These methods are to access any additional ComputedStyles that
   * the frame may be holding.
   *
   * These are styles that are children of the frame's primary style and are NOT
   * used as styles for any child frames.
   *
   * These contexts also MUST NOT have any child styles whatsoever. If you need
   * to insert styles into the style tree, then you should create pseudo element
   * frames to own them.
   *
   * The indicies must be consecutive and implementations MUST return null if
   * asked for an index that is out of range.
   */
  virtual ComputedStyle* GetAdditionalComputedStyle(int32_t aIndex) const;

  virtual void SetAdditionalComputedStyle(int32_t aIndex,
                                          ComputedStyle* aComputedStyle);

  /**
   * @param aSelectionStatus nsISelectionController::getDisplaySelection.
   */
  already_AddRefed<ComputedStyle> ComputeSelectionStyle(
      int16_t aSelectionStatus) const;

  already_AddRefed<ComputedStyle> ComputeHighlightSelectionStyle(
      nsAtom* aHighlightName);

  already_AddRefed<ComputedStyle> ComputeTargetTextStyle() const;

  /**
   * Accessor functions for geometric parent.
   */
  nsContainerFrame* GetParent() const { return mParent; }

  bool CanBeDynamicReflowRoot() const;

  /**
   * Gets the parent of a frame, using the parent of the placeholder for
   * out-of-flow frames.
   */
  inline nsContainerFrame* GetInFlowParent() const;

  /**
   * Gets the primary frame of the closest flattened tree ancestor that has a
   * frame (flattened tree ancestors may not have frames in presence of display:
   * contents).
   */
  inline nsIFrame* GetClosestFlattenedTreeAncestorPrimaryFrame() const;

  /**
   * Return the placeholder for this frame (which must be out-of-flow).
   * @note this will only return non-null if |this| is the first-in-flow
   * although we don't assert that here for legacy reasons.
   */
  inline nsPlaceholderFrame* GetPlaceholderFrame() const {
    MOZ_ASSERT(HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));
    return GetProperty(PlaceholderFrameProperty());
  }

  /**
   * Set this frame's parent to aParent.
   */
  void SetParent(nsContainerFrame* aParent);

  /**
   * The frame's writing-mode, used for logical layout computations.
   * It's usually the 'writing-mode' computed value, but there are exceptions:
   *   * inner table frames copy the value from the table frame
   *     (@see nsTableRowGroupFrame::Init, nsTableRowFrame::Init etc)
   *   * the root element frame propagates its value to its ancestors.
   *     The value may obtain from the principal <body> element.
   *     (@see nsCSSFrameConstructor::ConstructDocElementFrame)
   *   * the internal anonymous frames of the root element copy their value
   *     from the parent.
   *     (@see nsIFrame::Init)
   *   * a scrolled frame propagates its value to its ancestor scroll frame
   *     (@see ScrollContainerFrame::ReloadChildFrames)
   */
  mozilla::WritingMode GetWritingMode() const { return mWritingMode; }

  /**
   * Construct a writing mode for line layout in this frame.  This is
   * the writing mode of this frame, except that if this frame is styled with
   * unicode-bidi:plaintext, we reset the direction to the resolved paragraph
   * level of the given subframe (typically the first frame on the line),
   * because the container frame could be split by hard line breaks into
   * multiple paragraphs with different base direction.
   * @param aSelfWM the WM of 'this'
   */
  mozilla::WritingMode WritingModeForLine(mozilla::WritingMode aSelfWM,
                                          nsIFrame* aSubFrame) const;

  /**
   * Bounding rect of the frame.
   *
   * For frames that are laid out according to CSS box model rules the values
   * are in app units, and the origin is relative to the upper-left of the
   * geometric parent.  The size includes the content area, borders, and
   * padding.
   *
   * Frames that are laid out according to SVG's coordinate space based rules
   * (frames with the NS_FRAME_SVG_LAYOUT bit set, which *excludes*
   * SVGOuterSVGFrame) are different.  Many frames of this type do not set or
   * use mRect, in which case the frame rect is undefined.  The exceptions are:
   *
   *   - SVGInnerSVGFrame
   *   - SVGGeometryFrame (used for <path>, <circle>, etc.)
   *   - SVGImageFrame
   *   - SVGForeignObjectFrame
   *
   * For these frames the frame rect contains the frame's element's userspace
   * bounds including fill, stroke and markers, but converted to app units
   * rather than being in user units (CSS px).  In the SVG code "userspace" is
   * defined to be the coordinate system for the attributes that define an
   * element's geometry (such as the 'cx' attribute for <circle>).  For more
   * precise details see these frames' implementations of the ReflowSVG method
   * where mRect is set.
   *
   * Note: moving or sizing the frame does not affect the view's size or
   * position.
   */
  nsRect GetRect() const { return mRect; }
  nsPoint GetPosition() const { return mRect.TopLeft(); }
  nsSize GetSize() const { return mRect.Size(); }
  nsRect GetRectRelativeToSelf() const {
    return nsRect(nsPoint(0, 0), mRect.Size());
  }

  /**
   * Like the frame's rect (see |GetRect|), which is the border rect,
   * other rectangles of the frame, in app units, relative to the parent.
   */
  nsRect GetPaddingRect() const;
  nsRect GetPaddingRectRelativeToSelf() const;
  nsRect GetContentRect() const;
  nsRect GetContentRectRelativeToSelf() const;
  nsRect GetMarginRect() const;
  nsRect GetMarginRectRelativeToSelf() const;

  /**
   * Dimensions and position in logical coordinates in the frame's writing mode
   *  or another writing mode
   */
  mozilla::LogicalRect GetLogicalRect(const nsSize& aContainerSize) const {
    return GetLogicalRect(GetWritingMode(), aContainerSize);
  }
  mozilla::LogicalPoint GetLogicalPosition(const nsSize& aContainerSize) const {
    return GetLogicalPosition(GetWritingMode(), aContainerSize);
  }
  mozilla::LogicalSize GetLogicalSize() const {
    return GetLogicalSize(GetWritingMode());
  }
  mozilla::LogicalRect GetLogicalRect(mozilla::WritingMode aWritingMode,
                                      const nsSize& aContainerSize) const {
    return mozilla::LogicalRect(aWritingMode, GetRect(), aContainerSize);
  }
  mozilla::LogicalPoint GetLogicalPosition(mozilla::WritingMode aWritingMode,
                                           const nsSize& aContainerSize) const {
    return GetLogicalRect(aWritingMode, aContainerSize).Origin(aWritingMode);
  }
  mozilla::LogicalSize GetLogicalSize(mozilla::WritingMode aWritingMode) const {
    return mozilla::LogicalSize(aWritingMode, GetSize());
  }
  nscoord IStart(const nsSize& aContainerSize) const {
    return IStart(GetWritingMode(), aContainerSize);
  }
  nscoord IStart(mozilla::WritingMode aWritingMode,
                 const nsSize& aContainerSize) const {
    return GetLogicalPosition(aWritingMode, aContainerSize).I(aWritingMode);
  }
  nscoord BStart(const nsSize& aContainerSize) const {
    return BStart(GetWritingMode(), aContainerSize);
  }
  nscoord BStart(mozilla::WritingMode aWritingMode,
                 const nsSize& aContainerSize) const {
    return GetLogicalPosition(aWritingMode, aContainerSize).B(aWritingMode);
  }
  nscoord ISize() const { return ISize(GetWritingMode()); }
  nscoord ISize(mozilla::WritingMode aWritingMode) const {
    return GetLogicalSize(aWritingMode).ISize(aWritingMode);
  }
  nscoord BSize() const { return BSize(GetWritingMode()); }
  nscoord BSize(mozilla::WritingMode aWritingMode) const {
    return GetLogicalSize(aWritingMode).BSize(aWritingMode);
  }
  mozilla::LogicalSize ContentSize() const {
    return ContentSize(GetWritingMode());
  }
  mozilla::LogicalSize ContentSize(mozilla::WritingMode aWritingMode) const {
    mozilla::WritingMode wm = GetWritingMode();
    const auto bp = GetLogicalUsedBorderAndPadding(wm)
                        .ApplySkipSides(GetLogicalSkipSides())
                        .ConvertTo(aWritingMode, wm);
    const auto size = GetLogicalSize(aWritingMode);
    return mozilla::LogicalSize(
        aWritingMode,
        std::max(0, size.ISize(aWritingMode) - bp.IStartEnd(aWritingMode)),
        std::max(0, size.BSize(aWritingMode) - bp.BStartEnd(aWritingMode)));
  }
  nscoord ContentISize(mozilla::WritingMode aWritingMode) const {
    return ContentSize(aWritingMode).ISize(aWritingMode);
  }
  nscoord ContentBSize(mozilla::WritingMode aWritingMode) const {
    return ContentSize(aWritingMode).BSize(aWritingMode);
  }

  /**
   * When we change the size of the frame's border-box rect, we may need to
   * reset the overflow rect if it was previously stored as deltas.
   * (If it is currently a "large" overflow and could be re-packed as deltas,
   * we don't bother as the cost of the allocation has already been paid.)
   * @param aRebuildDisplayItems If true, then adds this frame to the
   * list of modified frames for display list building if the rect has changed.
   * Only pass false if you're sure that the relevant display items will be
   * rebuilt already (possibly by an ancestor being in the modified list), or if
   * this is a temporary change.
   */
  void SetRect(const nsRect& aRect, bool aRebuildDisplayItems = true) {
    if (aRect == mRect) {
      return;
    }
    if (mOverflow.mType != OverflowStorageType::Large &&
        mOverflow.mType != OverflowStorageType::None) {
      mozilla::OverflowAreas overflow = GetOverflowAreas();
      mRect = aRect;
      SetOverflowAreas(overflow);
    } else {
      mRect = aRect;
    }
    if (aRebuildDisplayItems) {
      MarkNeedsDisplayItemRebuild();
    }
  }
  /**
   * Set this frame's rect from a logical rect in its own writing direction
   */
  void SetRect(const mozilla::LogicalRect& aRect,
               const nsSize& aContainerSize) {
    SetRect(GetWritingMode(), aRect, aContainerSize);
  }
  /**
   * Set this frame's rect from a logical rect in a different writing direction
   * (GetPhysicalRect will assert if the writing mode doesn't match)
   */
  void SetRect(mozilla::WritingMode aWritingMode,
               const mozilla::LogicalRect& aRect,
               const nsSize& aContainerSize) {
    SetRect(aRect.GetPhysicalRect(aWritingMode, aContainerSize));
  }

  /**
   * Set this frame's size from a logical size in its own writing direction.
   * This leaves the frame's logical position unchanged, which means its
   * physical position may change (for right-to-left modes).
   */
  void SetSize(const mozilla::LogicalSize& aSize) {
    SetSize(GetWritingMode(), aSize);
  }
  /*
   * Set this frame's size from a logical size in a different writing direction.
   * This leaves the frame's logical position in the given mode unchanged,
   * which means its physical position may change (for right-to-left modes).
   */
  void SetSize(mozilla::WritingMode aWritingMode,
               const mozilla::LogicalSize& aSize) {
    if (aWritingMode.IsPhysicalRTL()) {
      nscoord oldWidth = mRect.Width();
      SetSize(aSize.GetPhysicalSize(aWritingMode));
      mRect.x -= mRect.Width() - oldWidth;
    } else {
      SetSize(aSize.GetPhysicalSize(aWritingMode));
    }
  }

  /**
   * Set this frame's physical size. This leaves the frame's physical position
   * (topLeft) unchanged.
   * @param aRebuildDisplayItems If true, then adds this frame to the
   * list of modified frames for display list building if the size has changed.
   * Only pass false if you're sure that the relevant display items will be
   * rebuilt already (possibly by an ancestor being in the modified list), or if
   * this is a temporary change.
   */
  void SetSize(const nsSize& aSize, bool aRebuildDisplayItems = true) {
    SetRect(nsRect(mRect.TopLeft(), aSize), aRebuildDisplayItems);
  }

  void SetPosition(const nsPoint& aPt);
  void SetPosition(mozilla::WritingMode aWritingMode,
                   const mozilla::LogicalPoint& aPt,
                   const nsSize& aContainerSize) {
    // We subtract mRect.Size() from the container size to account for
    // the fact that logical origins in RTL coordinate systems are at
    // the top right of the frame instead of the top left.
    SetPosition(
        aPt.GetPhysicalPoint(aWritingMode, aContainerSize - mRect.Size()));
  }

  /**
   * Move the frame, accounting for relative positioning. Use this when
   * adjusting the frame's position by a known amount, to properly update its
   * saved normal position (see GetNormalPosition below).
   *
   * This must be used only when moving a frame *after*
   * ReflowInput::ApplyRelativePositioning is called.  When moving
   * a frame during the reflow process prior to calling
   * ReflowInput::ApplyRelativePositioning, the position should
   * simply be adjusted directly (e.g., using SetPosition()).
   */
  void MovePositionBy(const nsPoint& aTranslation);

  /**
   * As above, using a logical-point delta in a given writing mode.
   */
  void MovePositionBy(mozilla::WritingMode aWritingMode,
                      const mozilla::LogicalPoint& aTranslation) {
    // The LogicalPoint represents a vector rather than a point within a
    // rectangular coordinate space, so we use a null containerSize when
    // converting logical to physical.
    const nsSize nullContainerSize;
    MovePositionBy(
        aTranslation.GetPhysicalPoint(aWritingMode, nullContainerSize));
  }

  /**
   * Return frame's rect without relative positioning
   */
  nsRect GetNormalRect() const;
  mozilla::LogicalRect GetLogicalNormalRect(
      mozilla::WritingMode aWritingMode, const nsSize& aContainerSize) const {
    return mozilla::LogicalRect(aWritingMode, GetNormalRect(), aContainerSize);
  }

  /**
   * Returns frame's rect as required by the GetBoundingClientRect() DOM API.
   */
  nsRect GetBoundingClientRect();

  /**
   * Return frame's position without relative positioning.
   * If aHasProperty is provided, returns whether the normal position
   * was stored in a frame property.
   */
  inline nsPoint GetNormalPosition(bool* aHasProperty = nullptr) const;
  inline mozilla::LogicalPoint GetLogicalNormalPosition(
      mozilla::WritingMode aWritingMode, const nsSize& aContainerSize) const;

  virtual nsPoint GetPositionOfChildIgnoringScrolling(const nsIFrame* aChild) {
    return aChild->GetPosition();
  }

  nsPoint GetPositionIgnoringScrolling() const;

#define NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(prop, type, dtor)              \
  static const mozilla::FramePropertyDescriptor<type>* prop() {            \
    /* Use of constexpr caused startup crashes with MSVC2015u1 PGO. */     \
    static const auto descriptor =                                         \
        mozilla::FramePropertyDescriptor<type>::NewWithDestructor<dtor>(); \
    return &descriptor;                                                    \
  }

// Don't use this unless you really know what you're doing!
#define NS_DECLARE_FRAME_PROPERTY_WITH_FRAME_IN_DTOR(prop, type, dtor) \
  static const mozilla::FramePropertyDescriptor<type>* prop() {        \
    /* Use of constexpr caused startup crashes with MSVC2015u1 PGO. */ \
    static const auto descriptor = mozilla::FramePropertyDescriptor<   \
        type>::NewWithDestructorWithFrame<dtor>();                     \
    return &descriptor;                                                \
  }

#define NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(prop, type)              \
  static const mozilla::FramePropertyDescriptor<type>* prop() {         \
    /* Use of constexpr caused startup crashes with MSVC2015u1 PGO. */  \
    static const auto descriptor =                                      \
        mozilla::FramePropertyDescriptor<type>::NewWithoutDestructor(); \
    return &descriptor;                                                 \
  }

#define NS_DECLARE_FRAME_PROPERTY_DELETABLE(prop, type) \
  NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(prop, type, DeleteValue)

#define NS_DECLARE_FRAME_PROPERTY_RELEASABLE(prop, type) \
  NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(prop, type, ReleaseValue)

#define NS_DECLARE_FRAME_PROPERTY_WITH_DTOR_NEVER_CALLED(prop, type) \
  static void AssertOnDestroyingProperty##prop(type*) {              \
    MOZ_ASSERT_UNREACHABLE(                                          \
        "Frame property " #prop                                      \
        " should never be destroyed by the FrameProperties class");  \
  }                                                                  \
  NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(prop, type,                    \
                                      AssertOnDestroyingProperty##prop)

#define NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(prop, type) \
  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(prop, mozilla::SmallValueHolder<type>)

  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(IBSplitSibling, nsContainerFrame)
  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(IBSplitPrevSibling, nsContainerFrame)

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(NormalPositionProperty, nsPoint)
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(ComputedOffsetProperty, nsMargin)

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(OutlineInnerRectProperty, nsRect)
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(PreEffectsBBoxProperty, nsRect)
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(PreTransformOverflowAreasProperty,
                                      mozilla::OverflowAreas)

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(OverflowAreasProperty,
                                      mozilla::OverflowAreas)

  // The initial overflow area passed to FinishAndStoreOverflow. This is only
  // set on frames that Preserve3D() or HasPerspective() or IsTransformed(), and
  // when at least one of the overflow areas differs from the frame bound rect.
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(InitialOverflowProperty,
                                      mozilla::OverflowAreas)

#ifdef DEBUG
  // InitialOverflowPropertyDebug is added to the frame to indicate that either
  // the InitialOverflowProperty has been stored or the InitialOverflowProperty
  // has been suppressed due to being set to the default value (frame bounds)
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(DebugInitialOverflowPropertyApplied,
                                        bool)
#endif

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(UsedMarginProperty, nsMargin)
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(UsedPaddingProperty, nsMargin)

  // This tracks the start and end page value for a frame.
  //
  // https://www.w3.org/TR/css-page-3/#using-named-pages
  //
  // This is only tracked during paginated frame construction.
  // This is used to implement fragmentation based on CSS page names. During
  // frame construction, we insert page breaks when we begin a new page box and
  // the previous page box had a different name.
  struct PageValues {
    // A value of null indicates that the value is equal to what auto resolves
    // to for this frame.
    RefPtr<const nsAtom> mStartPageValue = nullptr;
    RefPtr<const nsAtom> mEndPageValue = nullptr;
  };
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(PageValuesProperty, PageValues)

  const nsAtom* GetStartPageValue() const {
    if (const PageValues* const values =
            FirstInFlow()->GetProperty(PageValuesProperty())) {
      return values->mStartPageValue;
    }
    return nullptr;
  }

  const nsAtom* GetEndPageValue() const {
    if (const PageValues* const values =
            FirstInFlow()->GetProperty(PageValuesProperty())) {
      return values->mEndPageValue;
    }
    return nullptr;
  }

  // Returns the page name based on style information for this frame, or null
  // if the value is auto.
  const nsAtom* GetStylePageName() const {
    const mozilla::StylePageName& pageName = StylePage()->mPage;
    if (pageName.IsPageName()) {
      return pageName.AsPageName().AsAtom();
    }
    MOZ_ASSERT(pageName.IsAuto(), "Impossible page name");
    return nullptr;
  }

  bool HasUnreflowedContainerQueryAncestor() const;

  // Return True if this frame has a forced break value before it.
  //
  // Note: this method only checks 'break-before' property on *this* frame, and
  // it doesn't handle forced break value propagation from its first child.
  // Callers should handle the propagation in reflow.
  bool ShouldBreakBefore(const ReflowInput::BreakType aBreakType) const;

  // Return True if this frame has a forced break value after it.
  //
  // Note: this method only checks 'break-after' property on *this* frame, and
  // it doesn't handle forced break value propagation from its last child.
  // Callers should handle the propagation in reflow.
  bool ShouldBreakAfter(const ReflowInput::BreakType aBreakType) const;

 private:
  bool ShouldBreakBetween(const nsStyleDisplay* aDisplay,
                          const mozilla::StyleBreakBetween aBreakBetween,
                          const ReflowInput::BreakType aBreakType) const;

  // The value that the CSS page-name "auto" keyword resolves to for children
  // of this frame.
  //
  // A missing value for this property indicates that the auto value is the
  // empty string, which is the default if no ancestors of a frame specify a
  // page name. This avoids ever storing this property if the document doesn't
  // use named pages.
  //
  // https://www.w3.org/TR/css-page-3/#using-named-pages
  //
  // Ideally this would be a const atom, but that isn't possible with the
  // Release() call. This isn't too bad, since it's hidden behind constness-
  // preserving getter/setter.
  NS_DECLARE_FRAME_PROPERTY_RELEASABLE(AutoPageValueProperty, nsAtom)

 public:
  // Get the value that the CSS page-name "auto" keyword resolves to for
  // children of this frame.
  // This is needed when propagating page-name values up the frame tree.
  const nsAtom* GetAutoPageValue() const {
    if (const nsAtom* const atom = GetProperty(AutoPageValueProperty())) {
      return atom;
    }
    return nsGkAtoms::_empty;
  }
  void SetAutoPageValue(const nsAtom* aAtom) {
    MOZ_ASSERT(aAtom, "Atom must not be null");
    nsAtom* const atom = const_cast<nsAtom*>(aAtom);
    if (atom != nsGkAtoms::_empty) {
      SetProperty(AutoPageValueProperty(), do_AddRef(atom).take());
    }
  }

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(LineBaselineOffset, nscoord)

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(InvalidationRect, nsRect)

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(RefusedAsyncAnimationProperty, bool)

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(FragStretchBSizeProperty, nscoord)

  // The block-axis margin-box size associated with eBClampMarginBoxMinSize.
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(BClampMarginBoxMinSizeProperty, nscoord)

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(IBaselinePadProperty, nscoord)
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(BBaselinePadProperty, nscoord)

  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(BidiDataProperty,
                                        mozilla::FrameBidiData)

  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(PlaceholderFrameProperty,
                                         nsPlaceholderFrame)

  NS_DECLARE_FRAME_PROPERTY_RELEASABLE(OffsetPathCache, mozilla::gfx::Path)

  mozilla::FrameBidiData GetBidiData() const {
    bool exists;
    mozilla::FrameBidiData bidiData = GetProperty(BidiDataProperty(), &exists);
    if (!exists) {
      bidiData.precedingControl = mozilla::kBidiLevelNone;
    }
    return bidiData;
  }

  mozilla::intl::BidiEmbeddingLevel GetBaseLevel() const {
    return GetBidiData().baseLevel;
  }

  mozilla::intl::BidiEmbeddingLevel GetEmbeddingLevel() const {
    return GetBidiData().embeddingLevel;
  }

  /**
   * Return the distance between the border edge of the frame and the
   * margin edge of the frame.  Like GetRect(), returns the dimensions
   * as of the most recent reflow.
   *
   * This doesn't include any margin collapsing that may have occurred.
   * It also doesn't consider GetSkipSides()/GetLogicalSkipSides(), so
   * may report nonzero values on sides that are actually skipped for
   * this fragment.
   *
   * It also treats 'auto' margins as zero, and treats any margins that
   * should have been turned into 'auto' because of overconstraint as
   * having their original values.
   */
  virtual nsMargin GetUsedMargin() const;
  virtual mozilla::LogicalMargin GetLogicalUsedMargin(
      mozilla::WritingMode aWritingMode) const {
    return mozilla::LogicalMargin(aWritingMode, GetUsedMargin());
  }

  /**
   * Return the distance between the border edge of the frame (which is
   * its rect) and the padding edge of the frame. Like GetRect(), returns
   * the dimensions as of the most recent reflow.
   *
   * This doesn't consider GetSkipSides()/GetLogicalSkipSides(), so
   * may report nonzero values on sides that are actually skipped for
   * this fragment.
   *
   * Note that this differs from StyleBorder()->GetComputedBorder() in
   * that this describes a region of the frame's box, and
   * StyleBorder()->GetComputedBorder() describes a border.  They differ
   * for tables (particularly border-collapse tables) and themed
   * elements.
   */
  virtual nsMargin GetUsedBorder() const;
  virtual mozilla::LogicalMargin GetLogicalUsedBorder(
      mozilla::WritingMode aWritingMode) const {
    return mozilla::LogicalMargin(aWritingMode, GetUsedBorder());
  }

  /**
   * Return the distance between the padding edge of the frame and the
   * content edge of the frame.  Like GetRect(), returns the dimensions
   * as of the most recent reflow.
   *
   * This doesn't consider GetSkipSides()/GetLogicalSkipSides(), so
   * may report nonzero values on sides that are actually skipped for
   * this fragment.
   */
  virtual nsMargin GetUsedPadding() const;
  virtual mozilla::LogicalMargin GetLogicalUsedPadding(
      mozilla::WritingMode aWritingMode) const {
    return mozilla::LogicalMargin(aWritingMode, GetUsedPadding());
  }

  nsMargin GetUsedBorderAndPadding() const {
    return GetUsedBorder() + GetUsedPadding();
  }
  mozilla::LogicalMargin GetLogicalUsedBorderAndPadding(
      mozilla::WritingMode aWritingMode) const {
    return mozilla::LogicalMargin(aWritingMode, GetUsedBorderAndPadding());
  }

  /**
   * The area to paint box-shadows around.  The default is the border rect.
   * (nsFieldSetFrame overrides this).
   */
  virtual nsRect VisualBorderRectRelativeToSelf() const {
    return nsRect(0, 0, mRect.Width(), mRect.Height());
  }

  /**
   * Get the size, in app units, of the border radii. It returns FALSE iff all
   * returned radii == 0 (so no border radii), TRUE otherwise.
   * For the aRadii indexes, use the enum HalfCorner constants in gfx/2d/Types.h
   * If a side is skipped via aSkipSides, its corners are forced to 0.
   *
   * All corner radii are then adjusted so they do not require more
   * space than aBorderArea, according to the algorithm in css3-background.
   *
   * aFrameSize is used as the basis for percentage widths and heights.
   * aBorderArea is used for the adjustment of radii that might be too
   * large.
   *
   * Return whether any radii are nonzero.
   */
  static bool ComputeBorderRadii(const mozilla::BorderRadius&,
                                 const nsSize& aFrameSize,
                                 const nsSize& aBorderArea, Sides aSkipSides,
                                 nscoord aRadii[8]);

  /*
   * Given a set of border radii for one box (e.g., border box), convert
   * it to the equivalent set of radii for another box (e.g., in to
   * padding box, out to outline box) by reducing radii or increasing
   * nonzero radii as appropriate.
   *
   * Indices into aRadii are the enum HalfCorner constants in gfx/2d/Types.h
   *
   * Note that insetting the radii is lossy, since it can turn nonzero radii
   * into zero, and re-adjusting does not inflate zero radii.
   *
   * Therefore, callers should always adjust directly from the original value
   * coming from style.
   */
  static void AdjustBorderRadii(nscoord aRadii[8], const nsMargin& aOffsets);

  /**
   * Fill in border radii for this frame.  Return whether any are nonzero.
   * Indices into aRadii are the enum HalfCorner constants in gfx/2d/Types.h
   * aSkipSides is a union of SideBits::eLeft/Right/Top/Bottom bits that says
   * which side(s) to skip.
   *
   * Note: GetMarginBoxBorderRadii() and GetShapeBoxBorderRadii() work only
   * on frames that establish block formatting contexts since they don't
   * participate in margin-collapsing.
   */
  virtual bool GetBorderRadii(const nsSize& aFrameSize,
                              const nsSize& aBorderArea, Sides aSkipSides,
                              nscoord aRadii[8]) const;
  bool GetBorderRadii(nscoord aRadii[8]) const;
  bool GetMarginBoxBorderRadii(nscoord aRadii[8]) const;
  bool GetPaddingBoxBorderRadii(nscoord aRadii[8]) const;
  bool GetContentBoxBorderRadii(nscoord aRadii[8]) const;
  bool GetBoxBorderRadii(nscoord aRadii[8], const nsMargin& aOffset) const;
  bool GetShapeBoxBorderRadii(nscoord aRadii[8]) const;

  /**
   * Returns one em unit, adjusted for font inflation if needed, in app units.
   */
  nscoord OneEmInAppUnits() const;

  /**
   * `GetNaturalBaselineBOffset`, but determines the baseline sharing group
   * through `GetDefaultBaselineSharingGroup` (If not specified), assuming line
   * layout context, and never fails, returning a synthesized baseline through
   * `SynthesizeFallbackBaseline`. Unlike `GetNaturalBaselineBOffset`, Result is
   * always relative to the block start of the frame.
   */
  nscoord GetLogicalBaseline(mozilla::WritingMode aWM) const;
  /**
   * Same as the above, but with baseline sharing group & export
   * context specified.
   */
  nscoord GetLogicalBaseline(mozilla::WritingMode aWM,
                             BaselineSharingGroup aBaselineGroup,
                             BaselineExportContext aExportContext) const;

  /**
   * Return true if the frame has a first(last) inline-axis baseline per
   * CSS Box Alignment.  If so, the returned baseline is the distance from
   * the relevant block-axis border-box edge (Start for
   * BaselineSharingGroup::First, end for BaselineSharingGroup::Last), where
   * a positive value points towards the content-box.
   * Some frames can export different baselines depending if it's in a line
   * layout context or any other context (e.g. Flex, grid).
   * https://drafts.csswg.org/css-align-3/#baseline-export
   * @note The returned value is only valid when reflow is not needed.
   * @note You should only call this on frames with a WM that's parallel to aWM.
   * @note We're approaching `nsLayoutUtils::Get(First|Last)LineBaseline` ==
   * `GetNaturalBaselineBOffset(aWM, (First|Last), Other)`. Grid relies on
   * baseline synthesis behaviour in `nsLayoutUtils` implementations (bug
   * 1609403), which blocks its removal.
   * @param aWM the writing-mode of the alignment context.
   * @return the baseline offset, if one exists
   */
  virtual Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext aExportContext) const {
    return Nothing{};
  }

  struct CaretBlockAxisMetrics {
    nscoord mOffset = 0;
    nscoord mExtent = 0;
  };
  /**
   * Get the offset (Block start of the caret) and extent of the caret in this
   * frame. The returned offset is corrected for vertical & line-inverted
   * writing modes.
   */
  CaretBlockAxisMetrics GetCaretBlockAxisMetrics(mozilla::WritingMode,
                                                 const nsFontMetrics&) const;
  // Gets the page-name value to be used for the page that contains this frame
  // during paginated reflow.
  // This only inspects the first in-flow child of this frame, and if that
  // is a container frame then its first in-flow child, until it reaches the
  // deepest child of the tree.
  // This will resolve auto values, including the case where no frame has a
  // page-name set in which case it will return the empty atom. It will never
  // return null.
  // This is intended to be used either on the root frame to find the first
  // page's page-name, or on a newly created continuation to find what the new
  // page's page-name will be.
  //
  // The auto page value can be set by the caller. This is useful when trying
  // to compute a page value in the middle of a frame tree. In that case the
  // auto value can be found from the AutoPageValue frame property of the
  // parent frame. A null auto value is interpreted as the empty-string atom.
  const nsAtom* ComputePageValue(const nsAtom* aAutoValue = nullptr) const
      MOZ_NONNULL_RETURN;

  ///////////////////////////////////////////////////////////////////////////////
  // The public visibility API.
  ///////////////////////////////////////////////////////////////////////////////

  /// @return true if we're tracking visibility for this frame.
  bool TrackingVisibility() const {
    return HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED);
  }

  /// @return the visibility state of this frame. See the Visibility enum
  /// for the possible return values and their meanings.
  Visibility GetVisibility() const;

  /// Update the visibility state of this frame synchronously.
  /// XXX(seth): Avoid using this method; we should be relying on the refresh
  /// driver for visibility updates. This method, which replaces
  /// nsLayoutUtils::UpdateApproximateFrameVisibility(), exists purely as a
  /// temporary measure to avoid changing behavior during the transition from
  /// the old image visibility code.
  void UpdateVisibilitySynchronously();

  // A frame property which stores the visibility state of this frame. Right
  // now that consists of an approximate visibility counter represented as a
  // uint32_t. When the visibility of this frame is not being tracked, this
  // property is absent.
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(VisibilityStateProperty, uint32_t);

 protected:
  /**
   * Get the position of the baseline on which the caret needs to be placed,
   * relative to the top of the frame.  This is mostly needed for frames
   * which return a baseline from GetBaseline which is not useful for
   * caret positioning.
   */
  virtual nscoord GetCaretBaseline() const {
    return GetLogicalBaseline(GetWritingMode());
  }

  // Gets a caret baseline suitable for the frame if the frame doesn't have one.
  //
  // @param aBSize the content box block size of the line container. Needed to
  // resolve line-height: -moz-block-height. NS_UNCONSTRAINEDSIZE is fine
  // otherwise.
  //
  // TODO(emilio): Now we support align-content on blocks it seems we could
  // get rid of line-height: -moz-block-height.
  nscoord GetFontMetricsDerivedCaretBaseline(
      nscoord aBSize = NS_UNCONSTRAINEDSIZE) const;

  /**
   * Subclasses can call this method to enable visibility tracking for this
   * frame.
   *
   * If visibility tracking was previously disabled, this will schedule an
   * update an asynchronous update of visibility.
   */
  void EnableVisibilityTracking();

  /**
   * Subclasses can call this method to disable visibility tracking for this
   * frame.
   *
   * Note that if visibility tracking was previously enabled, disabling
   * visibility tracking will cause a synchronous call to OnVisibilityChange().
   */
  void DisableVisibilityTracking();

  /**
   * Called when a frame transitions between visibility states (for example,
   * from nonvisible to visible, or from visible to nonvisible).
   *
   * @param aNewVisibility    The new visibility state.
   * @param aNonvisibleAction A requested action if the frame has become
   *                          nonvisible. If Nothing(), no action is
   *                          requested. If DISCARD_IMAGES is specified, the
   *                          frame is requested to ask any images it's
   *                          associated with to discard their surfaces if
   *                          possible.
   *
   * Subclasses which override this method should call their parent class's
   * implementation.
   */
  virtual void OnVisibilityChange(
      Visibility aNewVisibility,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());

  /**
   * Synthesize a baseline for this element. The returned baseline is the
   * distance from the relevant block-axis border-box edge (Start for
   * BaselineSharingGroup::First, end for BaselineSharingGroup::Last), where a
   * positive value points towards the content-box.
   * @note This always returns a synthesized baseline, even if the element may
   * have an actual baseline.
   */
  virtual nscoord SynthesizeFallbackBaseline(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup) const;

 public:
  /**
   * Get the suitable baseline sharing group for this element, assuming line
   * layout.
   */
  virtual BaselineSharingGroup GetDefaultBaselineSharingGroup() const {
    return BaselineSharingGroup::First;
  }

  ///////////////////////////////////////////////////////////////////////////////
  // Internal implementation for the approximate frame visibility API.
  ///////////////////////////////////////////////////////////////////////////////

  /**
   * We track the approximate visibility of frames using a counter; if it's
   * non-zero, then the frame is considered visible. Using a counter allows us
   * to account for situations where the frame may be visible in more than one
   * place (for example, via -moz-element), and it simplifies the
   * implementation of our approximate visibility tracking algorithms.
   *
   * @param aNonvisibleAction A requested action if the frame has become
   *                          nonvisible. If Nothing(), no action is
   *                          requested. If DISCARD_IMAGES is specified, the
   *                          frame is requested to ask any images it's
   *                          associated with to discard their surfaces if
   *                          possible.
   */
  void DecApproximateVisibleCount(
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());
  void IncApproximateVisibleCount();

  /**
   * Get the specified child list.
   *
   * @param   aListID identifies the requested child list.
   * @return  the child list.  If the requested list is unsupported by this
   *          frame type, an empty list will be returned.
   */
  virtual const nsFrameList& GetChildList(ChildListID aListID) const;
  const nsFrameList& PrincipalChildList() const {
    return GetChildList(mozilla::FrameChildListID::Principal);
  }

  /**
   * Sub-classes should override this methods if they want to append their own
   * child lists into aLists.
   */
  virtual void GetChildLists(nsTArray<ChildList>* aLists) const;

  /**
   * Returns the child lists for this frame.
   */
  AutoTArray<ChildList, 4> ChildLists() const {
    AutoTArray<ChildList, 4> childLists;
    GetChildLists(&childLists);
    return childLists;
  }

  /**
   * Returns the child lists for this frame, including ones belong to a child
   * document.
   */
  AutoTArray<ChildList, 4> CrossDocChildLists();

  /**
   * Child frames are linked together in a doubly-linked list
   */
  nsIFrame* GetNextSibling() const { return mNextSibling; }
  void SetNextSibling(nsIFrame* aNextSibling) {
    NS_ASSERTION(this != aNextSibling,
                 "Creating a circular frame list, this is very bad.");
    if (mNextSibling && mNextSibling->GetPrevSibling() == this) {
      mNextSibling->mPrevSibling = nullptr;
    }
    mNextSibling = aNextSibling;
    if (mNextSibling) {
      mNextSibling->mPrevSibling = this;
    }
  }

  nsIFrame* GetPrevSibling() const { return mPrevSibling; }

  /**
   * Builds the display lists for the content represented by this frame
   * and its descendants. The background+borders of this element must
   * be added first, before any other content.
   *
   * This should only be called by methods in nsFrame. Instead of calling this
   * directly, call either BuildDisplayListForStackingContext or
   * BuildDisplayListForChild.
   *
   * See nsDisplayList.h for more information about display lists.
   */
  virtual void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                const nsDisplayListSet& aLists) {}
  /**
   * Displays the caret onto the given display list builder. The caret is
   * painted on top of the rest of the display list items.
   */
  void DisplayCaret(nsDisplayListBuilder* aBuilder, nsDisplayList* aList);

  /**
   * Get the preferred caret color at the offset.
   *
   * @param aOffset is offset of the content.
   */
  virtual nscolor GetCaretColorAt(int32_t aOffset);

  bool IsThemed(nsITheme::Transparency* aTransparencyState = nullptr) const {
    return IsThemed(StyleDisplay(), aTransparencyState);
  }
  bool IsThemed(const nsStyleDisplay* aDisp,
                nsITheme::Transparency* aTransparencyState = nullptr) const {
    if (!aDisp->HasAppearance()) {
      return false;
    }
    nsIFrame* mutable_this = const_cast<nsIFrame*>(this);
    nsPresContext* pc = PresContext();
    nsITheme* theme = pc->Theme();
    if (!theme->ThemeSupportsWidget(pc, mutable_this,
                                    aDisp->EffectiveAppearance())) {
      return false;
    }
    if (aTransparencyState) {
      *aTransparencyState = theme->GetWidgetTransparency(
          mutable_this, aDisp->EffectiveAppearance());
    }
    return true;
  }

  /**
   * Builds a display list for the content represented by this frame,
   * treating this frame as the root of a stacking context.
   * Optionally sets aCreatedContainerItem to true if we created a
   * single container display item for the stacking context, and no
   * other wrapping items are needed.
   */
  void BuildDisplayListForStackingContext(
      nsDisplayListBuilder* aBuilder, nsDisplayList* aList,
      bool* aCreatedContainerItem = nullptr);

  enum class DisplayChildFlag {
    ForcePseudoStackingContext,
    ForceStackingContext,
    Inline,
  };
  using DisplayChildFlags = mozilla::EnumSet<DisplayChildFlag>;

  /**
   * Adjusts aDirtyRect for the child's offset, checks that the dirty rect
   * actually intersects the child (or its descendants), calls BuildDisplayList
   * on the child if necessary, and puts things in the right lists if the child
   * is positioned.
   *
   * @param aFlags a set of of DisplayChildFlag values that are applicable for
   * this operation.
   */
  void BuildDisplayListForChild(nsDisplayListBuilder* aBuilder,
                                nsIFrame* aChild,
                                const nsDisplayListSet& aLists,
                                DisplayChildFlags aFlags = {});

  void BuildDisplayListForSimpleChild(nsDisplayListBuilder* aBuilder,
                                      nsIFrame* aChild,
                                      const nsDisplayListSet& aLists);

  /**
   * Helper for BuildDisplayListForChild, to implement this special-case for
   * grid (and flex) items from the spec:
   *   The painting order of grid items is exactly the same as inline blocks,
   *   except that [...], and 'z-index' values other than 'auto' create a
   *   stacking context even if 'position' is 'static' (behaving exactly as if
   *   'position' were 'relative'). https://drafts.csswg.org/css-grid/#z-order
   *
   * Flex items also have the same special-case described in
   * https://drafts.csswg.org/css-flexbox/#painting
   */
  static DisplayChildFlags DisplayFlagsForFlexOrGridItem() {
    return DisplayChildFlags{DisplayChildFlag::ForcePseudoStackingContext};
  }

  bool RefusedAsyncAnimation() const {
    return GetProperty(RefusedAsyncAnimationProperty());
  }

  /**
   * Returns true if this frame is transformed (e.g. has CSS, SVG, or custom
   * transforms) or if its parent is an SVG frame that has children-only
   * transforms (e.g.  an SVG viewBox attribute) or if its transform-style is
   * preserve-3d or the frame has transform animations.
   */
  bool IsTransformed() const;

  /**
   * Same as IsTransformed, except that it doesn't take SVG transforms
   * into account.
   */
  bool IsCSSTransformed() const;

  /**
   * True if this frame has any animation of transform in effect.
   */
  bool HasAnimationOfTransform() const;

  /**
   * True if this frame has any animation of opacity in effect.
   *
   * EffectSet is just an optimization.
   */
  bool HasAnimationOfOpacity(mozilla::EffectSet* = nullptr) const;

  /**
   * Returns true if the frame is translucent or the frame has opacity
   * animations for the purposes of creating a stacking context.
   *
   * @param aStyleDisplay:  This function needs style display struct.
   *
   * @param aStyleEffects:  This function needs style effects struct.
   *
   * @param aEffectSet: This function may need to look up EffectSet property.
   *   If a caller already have one, pass it in can save property look up
   *   time; otherwise, just leave it as nullptr.
   */
  bool HasOpacity(const nsStyleDisplay* aStyleDisplay,
                  const nsStyleEffects* aStyleEffects,
                  mozilla::EffectSet* aEffectSet = nullptr) const {
    return HasOpacityInternal(1.0f, aStyleDisplay, aStyleEffects, aEffectSet);
  }
  /**
   * Returns true if the frame is translucent for display purposes.
   *
   * @param aStyleDisplay:  This function needs style display struct.
   *
   * @param aStyleEffects:  This function needs style effects struct.
   *
   * @param aEffectSet: This function may need to look up EffectSet property.
   *   If a caller already have one, pass it in can save property look up
   *   time; otherwise, just leave it as nullptr.
   */
  bool HasVisualOpacity(const nsStyleDisplay* aStyleDisplay,
                        const nsStyleEffects* aStyleEffects,
                        mozilla::EffectSet* aEffectSet = nullptr) const {
    // Treat an opacity value of 0.99 and above as opaque.  This is an
    // optimization aimed at Web content which use opacity:0.99 as a hint for
    // creating a stacking context only.
    return HasOpacityInternal(0.99f, aStyleDisplay, aStyleEffects, aEffectSet);
  }

  /**
   * Returns a matrix (in pixels) for the current frame. The matrix should be
   * relative to the current frame's coordinate space.
   *
   * @param aFrame The frame to compute the transform for.
   * @param aAppUnitsPerPixel The number of app units per graphics unit.
   */
  using ComputeTransformFunction = Matrix4x4 (*)(const nsIFrame*,
                                                 float aAppUnitsPerPixel);
  /** Returns the transform getter of this frame, if any. */
  virtual ComputeTransformFunction GetTransformGetter() const {
    return nullptr;
  }

  /**
   * Returns true if this frame's parent is an SVG frame that has children-only
   * transforms (e.g. an SVG viewBox attribute).
   * If aFromParentTransforms is non-null, then aFromParentTransforms will be
   * set to these transforms.
   */
  bool GetParentSVGTransforms(Matrix* aFromParentTransforms = nullptr) const {
    if (!HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
      return false;
    }
    return DoGetParentSVGTransforms(aFromParentTransforms);
  }
  virtual bool DoGetParentSVGTransforms(Matrix* = nullptr) const;

  /**
   * Returns whether this frame will attempt to extend the 3d transforms of its
   * children. This requires transform-style: preserve-3d, as well as no
   * clipping or svg effects.
   *
   * @param aStyleDisplay:  If the caller has this->StyleDisplay(), providing
   *   it here will improve performance.
   *
   * @param aStyleEffects:  If the caller has this->StyleEffects(), providing
   *   it here will improve performance.
   *
   * @param aEffectSetForOpacity: This function may need to look up the
   *   EffectSet for opacity animations on this frame.
   *   If the caller already has looked up this EffectSet, it may pass it in to
   *   save an extra property lookup.
   */
  bool Extend3DContext(
      const nsStyleDisplay* aStyleDisplay, const nsStyleEffects* aStyleEffects,
      mozilla::EffectSet* aEffectSetForOpacity = nullptr) const;
  bool Extend3DContext(
      mozilla::EffectSet* aEffectSetForOpacity = nullptr) const {
    return Extend3DContext(StyleDisplay(), StyleEffects(),
                           aEffectSetForOpacity);
  }

  /**
   * Returns whether this frame has a parent that Extend3DContext() and has
   * its own transform (or hidden backface) to be combined with the parent's
   * transform.
   */
  bool Combines3DTransformWithAncestors() const;

  /**
   * Returns whether this frame has a hidden backface and has a parent that
   * Extend3DContext(). This is useful because in some cases the hidden
   * backface can safely be ignored if it could not be visible anyway.
   *
   */
  bool In3DContextAndBackfaceIsHidden() const;

  bool IsPreserve3DLeaf(const nsStyleDisplay* aStyleDisplay,
                        mozilla::EffectSet* aEffectSet = nullptr) const {
    return Combines3DTransformWithAncestors() &&
           !Extend3DContext(aStyleDisplay, StyleEffects(), aEffectSet);
  }
  bool IsPreserve3DLeaf(mozilla::EffectSet* aEffectSet = nullptr) const {
    return IsPreserve3DLeaf(StyleDisplay(), aEffectSet);
  }

  bool HasPerspective() const;

  bool ChildrenHavePerspective(const nsStyleDisplay* aStyleDisplay) const;
  bool ChildrenHavePerspective() const {
    return ChildrenHavePerspective(StyleDisplay());
  }

  /**
   * Includes the overflow area of all descendants that participate in the
   * current 3d context into aOverflowAreas.
   */
  void ComputePreserve3DChildrenOverflow(
      mozilla::OverflowAreas& aOverflowAreas);

  void RecomputePerspectiveChildrenOverflow(const nsIFrame* aStartFrame);

  /**
   * Returns whether z-index applies to this frame.
   */
  bool ZIndexApplies() const;

  /**
   * Returns the computed z-index for this frame, returning Nothing() for
   * z-index: auto, and for frames that don't support z-index.
   */
  Maybe<int32_t> ZIndex() const;

  /**
   * Returns whether this frame is the anchor of some ancestor scroll frame. As
   * this frame is moved, the scroll frame will apply adjustments to keep this
   * scroll frame in the same relative position.
   *
   * aOutContainer will optionally be set to the scroll anchor container for
   * this frame if this frame is an anchor.
   */
  bool IsScrollAnchor(
      mozilla::layout::ScrollAnchorContainer** aOutContainer = nullptr);

  /**
   * Returns whether this frame is the anchor of some ancestor scroll frame, or
   * has a descendant which is the scroll anchor.
   */
  bool IsInScrollAnchorChain() const;
  void SetInScrollAnchorChain(bool aInChain);

  /**
   * Returns the number of ancestors between this and the root of our frame tree
   */
  uint32_t GetDepthInFrameTree() const;

  /**
   * Event handling of GUI events.
   *
   * @param aEvent event structure describing the type of event and rge widget
   * where the event originated. The |point| member of this is in the coordinate
   * system of the view returned by GetOffsetFromView.
   *
   * @param aEventStatus a return value indicating whether the event was
   * handled and whether default processing should be done
   *
   * XXX From a frame's perspective it's unclear what the effect of the event
   * status is. Does it cause the event to continue propagating through the
   * frame hierarchy or is it just returned to the widgets?
   *
   * @see     WidgetGUIEvent
   * @see     nsEventStatus
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual nsresult HandleEvent(nsPresContext* aPresContext,
                               mozilla::WidgetGUIEvent* aEvent,
                               nsEventStatus* aEventStatus);

  /**
   * Search for selectable content at point and attempt to select
   * based on the start and end selection behaviours.
   *
   * @param aPresContext Presentation context
   * @param aPoint Point at which selection will occur. Coordinates
   * should be relative to this frame.
   * @param aBeginAmountType, aEndAmountType Selection behavior, see
   * nsIFrame for definitions.
   * @param aSelectFlags Selection flags defined in nsIFrame.h.
   * @return success or failure at finding suitable content to select.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  SelectByTypeAtPoint(nsPresContext* aPresContext, const nsPoint& aPoint,
                      nsSelectionAmount aBeginAmountType,
                      nsSelectionAmount aEndAmountType, uint32_t aSelectFlags);

  MOZ_CAN_RUN_SCRIPT nsresult PeekBackwardAndForwardForSelection(
      nsSelectionAmount aAmountBack, nsSelectionAmount aAmountForward,
      int32_t aStartPos, bool aJumpLines, uint32_t aSelectFlags);

  enum { SELECT_ACCUMULATE = 0x01 };

 protected:
  // Fire DOM event. If no aContent argument use frame's mContent.
  void FireDOMEvent(const nsAString& aDOMEventName,
                    nsIContent* aContent = nullptr);

  // Selection Methods

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD
  HandlePress(nsPresContext* aPresContext, mozilla::WidgetGUIEvent* aEvent,
              nsEventStatus* aEventStatus);

  /**
   * MoveCaretToEventPoint() moves caret at the point of aMouseEvent.
   *
   * @param aPresContext        Must not be nullptr.
   * @param aMouseEvent         Must not be nullptr, the message must be
   *                            eMouseDown and its button must be primary or
   *                            middle button.
   * @param aEventStatus        [out] Must not be nullptr.  This method ignores
   *                            its initial value, but callees may refer it.
   */
  MOZ_CAN_RUN_SCRIPT nsresult MoveCaretToEventPoint(
      nsPresContext* aPresContext, mozilla::WidgetMouseEvent* aMouseEvent,
      nsEventStatus* aEventStatus);

  /**
   * Check whether aSecondaryButtonMouseEvent should or should not cause moving
   * caret at event point.  This is designed only for the secondary mouse button
   * event (i.e., right button event in general).
   *
   * @param aFrameSelection         The nsFrameSelection which should handle the
   *                                caret move with.
   * @param aSecondaryButtonEvent   Must be the button value is
   *                                MouseButton::eSecondary.
   * @param aContentAtEventPoint    The content node at the event point.
   * @param aOffsetAtEventPoint     The offset in aContentAtEventPoint where
   *                                aSecondaryButtonEvent clicked.
   */
  [[nodiscard]] bool MovingCaretToEventPointAllowedIfSecondaryButtonEvent(
      const nsFrameSelection& aFrameSelection,
      mozilla::WidgetMouseEvent& aSecondaryButtonEvent,
      const nsIContent& aContentAtEventPoint,
      int32_t aOffsetAtEventPoint) const;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD HandleMultiplePress(
      nsPresContext* aPresContext, mozilla::WidgetGUIEvent* aEvent,
      nsEventStatus* aEventStatus, bool aControlHeld);

  /**
   * @param aPresContext must be non-nullptr.
   * @param aEvent must be non-nullptr.
   * @param aEventStatus must be non-nullptr.
   */
  MOZ_CAN_RUN_SCRIPT
  NS_IMETHOD HandleDrag(nsPresContext* aPresContext,
                        mozilla::WidgetGUIEvent* aEvent,
                        nsEventStatus* aEventStatus);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD
  HandleRelease(nsPresContext* aPresContext, mozilla::WidgetGUIEvent* aEvent,
                nsEventStatus* aEventStatus);

  // Test if we are selecting a table object:
  //  Most table/cell selection requires that Ctrl (Cmd on Mac) key is down
  //   during a mouse click or drag. Exception is using Shift+click when
  //   already in "table/cell selection mode" to extend a block selection
  //  Get the parent content node and offset of the frame
  //   of the enclosing cell or table (if not inside a cell)
  //  aTarget tells us what table element to select (currently only cell and
  //  table supported) (enums for this are defined in nsIFrame.h)
  nsresult GetDataForTableSelection(const nsFrameSelection* aFrameSelection,
                                    mozilla::PresShell* aPresShell,
                                    mozilla::WidgetMouseEvent* aMouseEvent,
                                    nsIContent** aParentContent,
                                    int32_t* aContentOffset,
                                    mozilla::TableSelectionMode* aTarget);

  /**
   * @return see nsISelectionController.idl's `getDisplaySelection`.
   */
  int16_t DetermineDisplaySelection();

 public:
  virtual nsIContent* GetContentForEvent(const mozilla::WidgetEvent*) const;

  // This structure keeps track of the content node and offsets associated with
  // a point; there is a primary and a secondary offset associated with any
  // point.  The primary and secondary offsets differ when the point is over a
  // non-text object.  The primary offset is the expected position of the
  // cursor calculated from a point; the secondary offset, when it is different,
  // indicates that the point is in the boundaries of some selectable object.
  // Note that the primary offset can be after the secondary offset; for places
  // that need the beginning and end of the object, the StartOffset and
  // EndOffset helpers can be used.
  struct MOZ_STACK_CLASS ContentOffsets {
    ContentOffsets() = default;
    bool IsNull() { return !content; }
    // Helpers for places that need the ends of the offsets and expect them in
    // numerical order, as opposed to wanting the primary and secondary offsets
    int32_t StartOffset() { return std::min(offset, secondaryOffset); }
    int32_t EndOffset() { return std::max(offset, secondaryOffset); }

    nsCOMPtr<nsIContent> content;
    int32_t offset = 0;
    int32_t secondaryOffset = 0;
    // This value indicates whether the associated content is before or after
    // the offset; the most visible use is to allow the caret to know which line
    // to display on.
    mozilla::CaretAssociationHint associate{0};  // Before
  };
  enum {
    IGNORE_SELECTION_STYLE = 1 << 0,
    // Treat visibility:hidden frames as non-selectable
    SKIP_HIDDEN = 1 << 1,
    // Do not return content in native anonymous subtree (if the frame is in a
    // native anonymous subtree, the method may return content in same subtree).
    IGNORE_NATIVE_ANONYMOUS_SUBTREE = 1 << 2,
  };
  /**
   * This function calculates the content offsets for selection relative to
   * a point.  Note that this should generally only be callled on the event
   * frame associated with an event because this function does not account
   * for frame lists other than the primary one.
   * @param aPoint point relative to this frame
   */
  ContentOffsets GetContentOffsetsFromPoint(const nsPoint& aPoint,
                                            uint32_t aFlags = 0);

  virtual ContentOffsets GetContentOffsetsFromPointExternal(
      const nsPoint& aPoint, uint32_t aFlags = 0) {
    return GetContentOffsetsFromPoint(aPoint, aFlags);
  }

  // Helper for GetContentAndOffsetsFromPoint; calculation of content offsets
  // in this function assumes there is no child frame that can be targeted.
  virtual ContentOffsets CalcContentOffsetsFromFramePoint(
      const nsPoint& aPoint);

  /**
   * Ensure that `this` gets notifed when `aImage`s underlying image request
   * loads or animates.
   *
   * This in practice is only needed for the canvas frame and table cell
   * backgrounds, which are the only cases that should paint a background that
   * isn't its own. The canvas paints the background from the root element or
   * body, and the table cell paints the background for its row.
   *
   * For regular frames, this is done in DidSetComputedStyle.
   *
   * NOTE: It's unclear if we even actually _need_ this for the second case, as
   * invalidating the row should invalidate all the cells. For the canvas case
   * this is definitely needed as it paints the background from somewhere "down"
   * in the frame tree.
   *
   * Returns whether the image was in fact associated with the frame.
   */
  [[nodiscard]] bool AssociateImage(const mozilla::StyleImage&);

  /**
   * This needs to be called if the above caller returned true, once the above
   * caller doesn't care about getting notified anymore.
   */
  void DisassociateImage(const mozilla::StyleImage&);

  mozilla::StyleImageRendering UsedImageRendering() const;
  mozilla::StyleTouchAction UsedTouchAction() const;

  enum class AllowCustomCursorImage {
    No,
    Yes,
  };

  /**
   * This structure holds information about a cursor. AllowCustomCursorImage
   * is `No`, then no cursor image should be loaded from the style specified on
   * `mStyle`, or the frame's style.
   *
   * The `mStyle` member is used for `<area>` elements.
   */
  struct MOZ_STACK_CLASS Cursor {
    mozilla::StyleCursorKind mCursor = mozilla::StyleCursorKind::Auto;
    AllowCustomCursorImage mAllowCustomCursor = AllowCustomCursorImage::Yes;
    RefPtr<mozilla::ComputedStyle> mStyle;
  };

  /**
   * Get the cursor for a given frame.
   */
  virtual Cursor GetCursor(const nsPoint&);

  /**
   * Get a point (in the frame's coordinate space) given an offset into
   * the content. This point should be on the baseline of text with
   * the correct horizontal offset
   */
  virtual nsresult GetPointFromOffset(int32_t inOffset, nsPoint* outPoint);

  /**
   * Get a list of character rects in a given range.
   * This is similar version of GetPointFromOffset.
   */
  virtual nsresult GetCharacterRectsInRange(int32_t aInOffset, int32_t aLength,
                                            nsTArray<nsRect>& aRects);

  /**
   * Get the child frame of this frame which contains the given
   * content offset. outChildFrame may be this frame, or nullptr on return.
   * outContentOffset returns the content offset relative to the start
   * of the returned node. You can also pass a hint which tells the method
   * to stick to the end of the first found frame or the beginning of the
   * next in case the offset falls on a boundary.
   */
  virtual nsresult GetChildFrameContainingOffset(
      int32_t inContentOffset,
      bool inHint,  // false stick left
      int32_t* outFrameContentOffset, nsIFrame** outChildFrame);

  /**
   * Get the current frame-state value for this frame. aResult is
   * filled in with the state bits.
   */
  nsFrameState GetStateBits() const { return mState; }

  /**
   * Update the current frame-state value for this frame.
   */
  void AddStateBits(nsFrameState aBits) { mState |= aBits; }
  void RemoveStateBits(nsFrameState aBits) { mState &= ~aBits; }
  void AddOrRemoveStateBits(nsFrameState aBits, bool aVal) {
    aVal ? AddStateBits(aBits) : RemoveStateBits(aBits);
  }

  /**
   * Checks if the current frame-state includes all of the listed bits
   */
  bool HasAllStateBits(nsFrameState aBits) const {
    return (mState & aBits) == aBits;
  }

  /**
   * Checks if the current frame-state includes any of the listed bits
   */
  bool HasAnyStateBits(nsFrameState aBits) const { return mState & aBits; }

 private:
  /**
   * Called when this frame becomes primary for mContent.
   */
  void InitPrimaryFrame();
  /**
   * Called when the primary frame style changes.
   *
   * Kind of like DidSetComputedStyle, but the first computed style is set
   * before SetPrimaryFrame, so we need this tweak.
   */
  void HandlePrimaryFrameStyleChange(ComputedStyle* aOldStyle);

 public:
  /**
   * Return true if this frame is the primary frame for mContent.
   */
  bool IsPrimaryFrame() const { return mIsPrimaryFrame; }

  void SetIsPrimaryFrame(bool aIsPrimary) {
    mIsPrimaryFrame = aIsPrimary;
    if (aIsPrimary) {
      InitPrimaryFrame();
    }
  }

  bool ShouldPropagateRepaintsToRoot() const;

  /**
   * @return true if this frame is used as a fieldset's rendered legend.
   */
  bool IsRenderedLegend() const;

  /**
   * This call is invoked on the primary frame for a character data content
   * node, when it is changed in the content tree.
   */
  virtual nsresult CharacterDataChanged(const CharacterDataChangeInfo&);

  /**
   * This call is invoked when the value of a content objects's attribute
   * is changed.
   * The first frame that maps that content is asked to deal
   * with the change by doing whatever is appropriate.
   *
   * @param aNameSpaceID the namespace of the attribute
   * @param aAttribute the atom name of the attribute
   * @param aModType Whether or not the attribute was added, changed, or
   * removed. The constants are defined in MutationEvent.webidl.
   */
  virtual nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                                    int32_t aModType);

  /**
   * When the element states of mContent change, this method is invoked on the
   * primary frame of that element.
   *
   * @param aStates the changed states
   */
  virtual void ElementStateChanged(mozilla::dom::ElementState aStates);

  /**
   * Continuation member functions
   */
  virtual nsIFrame* GetPrevContinuation() const;
  virtual void SetPrevContinuation(nsIFrame*);
  virtual nsIFrame* GetNextContinuation() const;
  virtual void SetNextContinuation(nsIFrame*);
  virtual nsIFrame* FirstContinuation() const {
    return const_cast<nsIFrame*>(this);
  }
  virtual nsIFrame* LastContinuation() const {
    return const_cast<nsIFrame*>(this);
  }

  /**
   * GetTailContinuation gets the last non-overflow-container continuation
   * in the continuation chain, i.e. where the next sibling element
   * should attach).
   */
  nsIFrame* GetTailContinuation();

  /**
   * Flow member functions
   */
  virtual nsIFrame* GetPrevInFlow() const;
  virtual void SetPrevInFlow(nsIFrame*);

  virtual nsIFrame* GetNextInFlow() const;
  virtual void SetNextInFlow(nsIFrame*);

  /**
   * Return the first frame in our current flow.
   */
  virtual nsIFrame* FirstInFlow() const { return const_cast<nsIFrame*>(this); }

  /**
   * Return the last frame in our current flow.
   */
  virtual nsIFrame* LastInFlow() const { return const_cast<nsIFrame*>(this); }

  /**
   * Useful for line participants. Find the line container frame for this line.
   */
  nsIFrame* FindLineContainer() const;

  /**
   * Mark any stored intrinsic inline size information as dirty (requiring
   * re-calculation).  Note that this should generally not be called
   * directly; PresShell::FrameNeedsReflow() will call it instead.
   */
  virtual void MarkIntrinsicISizesDirty();

  /**
   * Make this frame and all descendants dirty (if not already).
   * Exceptions: TableColGroupFrame children.
   */
  void MarkSubtreeDirty();

  /**
   * Get the min-content intrinsic inline size of the frame.  This must be
   * less than or equal to the max-content intrinsic inline size.
   *
   * This is *not* affected by the CSS 'min-width', 'width', and
   * 'max-width' properties on this frame, but it is affected by the
   * values of those properties on this frame's descendants.  (It may be
   * called during computation of the values of those properties, so it
   * cannot depend on any values in the nsStylePosition for this frame.)
   *
   * The value returned should **NOT** include the space required for
   * padding and border.
   *
   * Note that many frames will cache the result of this function call
   * unless MarkIntrinsicISizesDirty is called.
   *
   * It is not acceptable for a frame to mark itself dirty when this
   * method is called.
   *
   * This method must not return a negative value.
   */
  nscoord GetMinISize(const mozilla::IntrinsicSizeInput& aInput) {
    return IntrinsicISize(aInput, mozilla::IntrinsicISizeType::MinISize);
  }

  /**
   * Get the max-content intrinsic inline size of the frame.  This must be
   * greater than or equal to the min-content intrinsic inline size.
   *
   * Otherwise, all the comments for |GetMinISize| above apply.
   */
  nscoord GetPrefISize(const mozilla::IntrinsicSizeInput& aInput) {
    return IntrinsicISize(aInput, mozilla::IntrinsicISizeType::PrefISize);
  }

  /**
   * A helper to implement GetMinISize() and GetPrefISize(). A derived class
   * should override this method to return its intrinsic size.
   *
   * All the comments for GetMinISize() and GetPrefISize() apply.
   */
  virtual nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                                 mozilla::IntrinsicISizeType aType) {
    return 0;
  }

  /**
   * |InlineIntrinsicISize| represents the intrinsic inline size information
   * in inline layout.  Code that determines the intrinsic inline size of a
   * region of inline layout accumulates the result into this structure.
   * This pattern is needed because we need to maintain state
   * information about whitespace (for both collapsing and trimming).
   */
  struct InlineIntrinsicISizeData {
    // The line. This may be null if the inlines are not associated with
    // a block or if we just don't know the line.
    const LineListIterator* mLine = nullptr;

    // The line container. Private, to ensure we always use SetLineContainer
    // to update it.
    //
    // Note that nsContainerFrame::DoInlineIntrinsicISize will clear the
    // |mLine| and |mLineContainer| fields when following a next-in-flow link,
    // so we must not assume these can always be dereferenced.
   private:
    nsIFrame* mLineContainer = nullptr;

    // Setter and getter for the lineContainer field:
   public:
    void SetLineContainer(nsIFrame* aLineContainer) {
      mLineContainer = aLineContainer;
    }
    nsIFrame* LineContainer() const { return mLineContainer; }

    // The max-content intrinsic inline size for all previous lines.
    nscoord mPrevLines = 0;

    // The max-content intrinsic inline size for the current line.  At a line
    // break (mandatory for max-content inline size; allowed for min-content
    // inline size), the caller should call |Break()|.
    nscoord mCurrentLine = 0;

    // This contains the inline size of the trimmable whitespace at the end of
    // |mCurrentLine|; it is zero if there is no such whitespace.
    nscoord mTrailingWhitespace = 0;

    // True if initial collapsable whitespace should be skipped.  This
    // should be true at the beginning of a block, after hard breaks
    // and when the last text ended with whitespace.
    bool mSkipWhitespace = true;

    // Floats encountered in the lines.
    class FloatInfo final {
     public:
      FloatInfo(const nsIFrame* aFrame, nscoord aISize)
          : mFrame(aFrame), mISize(aISize) {}
      const nsIFrame* Frame() const { return mFrame; }
      nscoord ISize() const { return mISize; }

     private:
      const nsIFrame* mFrame;
      nscoord mISize;
    };

    nsTArray<FloatInfo> mFloats;
  };

  struct InlineMinISizeData : public InlineIntrinsicISizeData {
    // The default implementation for nsIFrame::AddInlineMinISize.
    void DefaultAddInlineMinISize(nsIFrame* aFrame, nscoord aISize,
                                  bool aAllowBreak = true);

    // We need to distinguish forced and optional breaks for cases where the
    // current line total is negative. When it is, we need to ignore optional
    // breaks to prevent min-content inline size from ending up bigger than
    // max-content inline size.
    void ForceBreak();

    // If the break here is actually taken, aHyphenWidth must be added to the
    // width of the current line.
    void OptionallyBreak(nscoord aHyphenWidth = 0);

    // Whether we're currently at the start of the line.  If we are, we
    // can't break (for example, between the text-indent and the first
    // word).
    bool mAtStartOfLine = true;
  };

  struct InlinePrefISizeData : public InlineIntrinsicISizeData {
    /**
     * Finish the current line and start a new line.
     *
     * @param aClearType controls whether isize of floats are considered
     * and what floats are kept for the next line:
     *  * |None| skips handling floats, which means no floats are
     *    removed, and isizes of floats are not considered either.
     *  * |Both| takes floats into consideration when computing isize
     *    of the current line, and removes all floats after that.
     *  * |Left| and |Right| do the same as |Both| except that they only
     *    remove floats on the given side, and any floats on the other
     *    side that are prior to a float on the given side that has a
     *    'clear' property that clears them.
     */
    void ForceBreak(mozilla::UsedClear aClearType = mozilla::UsedClear::Both);

    // The default implementation for nsIFrame::AddInlinePrefISize.
    void DefaultAddInlinePrefISize(nscoord aISize);

    // True if the current line contains nothing other than placeholders.
    bool mLineIsEmpty = true;
  };

  /**
   * Add the min-content intrinsic inline size of a frame in a way suitable for
   * use in inline layout to an |InlineMinISizeData| object that
   * represents the intrinsic inline size information of all the previous
   * frames in the inline layout region.
   *
   * All *allowed* breakpoints within the frame determine what counts as
   * a line for the |InlineMinISizeData|.  This means that
   * |aData->mTrailingWhitespace| will always be zero (unlike for
   * AddInlinePrefISize).
   *
   * All the comments for |GetMinISize| apply, except that this function
   * is responsible for adding padding, border, and margin and for
   * considering the effects of 'inline-size', 'min-inline-size', and
   * 'max-inline-size'.
   *
   * This may be called on any frame.  Frames that do not participate in
   * line breaking can inherit the default implementation on nsIFrame,
   * which calls |GetMinISize|.
   */
  virtual void AddInlineMinISize(const mozilla::IntrinsicSizeInput& aInput,
                                 InlineMinISizeData* aData);

  /**
   * Add the max-content intrinsic inline size of a frame in a way suitable for
   * use in inline layout to an |InlinePrefISizeData| object that
   * represents the intrinsic inline size information of all the previous
   * frames in the inline layout region.
   *
   * All the comments for |AddInlineMinISize| and |GetPrefISize| apply,
   * except that this fills in an |InlinePrefISizeData| structure
   * based on using all *mandatory* breakpoints within the frame.
   */
  virtual void AddInlinePrefISize(const mozilla::IntrinsicSizeInput& aInput,
                                  InlinePrefISizeData* aData);

  /**
   * Intrinsic size of a frame in a single axis.
   *
   * This can represent either isize or bsize.
   */
  struct IntrinsicSizeOffsetData {
    nscoord padding = 0;
    nscoord border = 0;
    nscoord margin = 0;
    nscoord BorderPadding() const { return border + padding; };
    nscoord MarginBorderPadding() const { return margin + border + padding; }
  };

  /**
   * Return the isize components of padding, border, and margin
   * that contribute to the intrinsic width that applies to the parent.
   * @param aPercentageBasis the percentage basis to use for padding/margin -
   *   i.e. the Containing Block's inline-size
   */
  virtual IntrinsicSizeOffsetData IntrinsicISizeOffsets(
      nscoord aPercentageBasis = NS_UNCONSTRAINEDSIZE);

  /**
   * Return the bsize components of padding, border, and margin
   * that contribute to the intrinsic width that applies to the parent.
   * @param aPercentageBasis the percentage basis to use for padding/margin -
   *   i.e. the Containing Block's inline-size
   */
  IntrinsicSizeOffsetData IntrinsicBSizeOffsets(
      nscoord aPercentageBasis = NS_UNCONSTRAINEDSIZE);

  virtual mozilla::IntrinsicSize GetIntrinsicSize();

  /**
   * Get the preferred aspect ratio of this frame, or a default-constructed
   * AspectRatio if it has none.
   *
   * https://drafts.csswg.org/css-sizing-4/#preferred-aspect-ratio
   */
  mozilla::AspectRatio GetAspectRatio() const;

  /**
   * Get the intrinsic aspect ratio of this frame, or a default-constructed
   * AspectRatio if it has no intrinsic ratio.
   *
   * The intrinsic ratio is the ratio of the width/height of a box with an
   * intrinsic size or the intrinsic aspect ratio of a scalable vector image
   * without an intrinsic size. A frame class implementing a replaced element
   * should override this method if it has a intrinsic ratio.
   */
  virtual mozilla::AspectRatio GetIntrinsicRatio() const;

  /**
   * Compute the size that a frame will occupy.  Called while
   * constructing the ReflowInput to be used to Reflow the frame,
   * in order to fill its mComputedWidth and mComputedHeight member
   * variables.
   *
   * Note that the reason that border and padding need to be passed
   * separately is so that the 'box-sizing' property can be handled.
   * Thus aMargin includes absolute positioning offsets as well.
   *
   * @param aWM  The writing mode to use for the returned size (need not match
   *             this frame's writing mode). This is also the writing mode of
   *             the passed-in LogicalSize parameters.
   * @param aCBSize  The size of the element's containing block.  (Well,
   *                 the BSize() component isn't really.)
   * @param aAvailableISize  The available inline-size for 'auto' inline-size.
   *                         This is usually the same as aCBSize.ISize(),
   *                         but differs in cases such as block
   *                         formatting context roots next to floats, or
   *                         in some cases of float reflow in quirks
   *                         mode.
   * @param aMargin  The sum of the inline / block margins ***AND***
   *                 absolute positioning offsets (inset-block and
   *                 inset-inline) of the frame, including actual values
   *                 resulting from percentages and from the
   *                 "hypothetical box" for absolute positioning, but
   *                 not including actual values resulting from 'auto'
   *                 margins or ignored 'auto' values in absolute
   *                 positioning.
   * @param aBorderPadding  The sum of the frame's inline / block border-widths
   *                        and padding (including actual values resulting from
   *                        percentage padding values).
   * @param aSizeOverride Optional override values for size properties, which
   *                      this function will use internally instead of the
   *                      actual property values.
   * @param aFlags   Flags to further customize behavior (definitions in
   *                 LayoutConstants.h).
   *
   * The return value includes the computed LogicalSize and AspectRatioUsage
   * which indicates whether the inline/block size is affected by aspect-ratio
   * or not. The BSize() of the returned LogicalSize may be
   * NS_UNCONSTRAINEDSIZE, but the ISize() must not be. We need AspectRatioUsage
   * during reflow because the final size may be affected by the content size
   * after applying aspect-ratio.
   * https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
   *
   */
  enum class AspectRatioUsage : uint8_t {
    None,
    ToComputeISize,
    ToComputeBSize,
  };
  struct SizeComputationResult {
    mozilla::LogicalSize mLogicalSize;
    AspectRatioUsage mAspectRatioUsage = AspectRatioUsage::None;
  };
  virtual SizeComputationResult ComputeSize(
      gfxContext* aRenderingContext, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags);

  static nscoord ComputeBSizeValueAsPercentageBasis(
      const mozilla::StyleSize& aStyleBSize,
      const mozilla::StyleSize& aStyleMinBSize,
      const mozilla::StyleMaxSize& aStyleMaxBSize, nscoord aCBBSize,
      nscoord aContentEdgeToBoxSizingBSize);

 protected:
  /**
   * A helper, used by |nsIFrame::ComputeSize| (for frames that need to
   * override only this part of ComputeSize), that computes the size
   * that should be returned when inline-size, block-size, and
   * [min|max]-[inline-size|block-size] are all 'auto' or equivalent.
   *
   * In general, frames that can accept any computed inline-size/block-size
   * should override only ComputeAutoSize, and frames that cannot do so need to
   * override ComputeSize to enforce their inline-size/block-size invariants.
   *
   * Implementations may optimize by returning a garbage inline-size if
   * StylePosition()->ISize() is not 'auto' (or inline-size override in
   * aSizeOverrides is not 'auto' if provided), and likewise for BSize(), since
   * in such cases the result is guaranteed to be unused.
   *
   * Most of the frame are not expected to check the aSizeOverrides parameter
   * apart from checking the inline size override for 'auto' if they want to
   * optimize and return garbage inline-size.
   */
  virtual mozilla::LogicalSize ComputeAutoSize(
      gfxContext* aRenderingContext, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags);

  /**
   * A helper used by |nsIFrame::ComputeAutoSize|, computing auto sizes of
   * frames that are absolutely positioned. Any class that overrides
   * `ComputeAutoSize` may use this function to maintain the standard absolute
   * size computation specified in [1].
   *
   * [1]: https://drafts.csswg.org/css-position-3/#abspos-auto-size
   */
  mozilla::LogicalSize ComputeAbsolutePosAutoSize(
      gfxContext* aRenderingContext, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      const mozilla::ComputeSizeFlags& aFlags);

  /**
   * Precondition helper function to determine if
   * |nsIFrame::ComputeAbsolutePosAutoSize| can be called on this frame.
   */
  bool IsAbsolutelyPositionedWithDefiniteContainingBlock() const;

  /**
   * Utility function for ComputeAutoSize implementations.  Return
   * max(GetMinISize(), min(aISizeInCB, GetPrefISize()))
   */
  nscoord ShrinkISizeToFit(const mozilla::IntrinsicSizeInput& aInput,
                           nscoord aISizeInCB,
                           mozilla::ComputeSizeFlags aFlags);

  /**
   * A helper for derived classes to implement min-content & max-content
   * intrinsic inline size in terms of AddInlineMinISize() and
   * AddInlinePrefISize().
   */
  nscoord IntrinsicISizeFromInline(const mozilla::IntrinsicSizeInput& aInput,
                                   mozilla::IntrinsicISizeType aType);

 public:
  /**
   * Compute a tight bounding rectangle for the frame. This is a rectangle
   * that encloses the pixels that are actually drawn. We're allowed to be
   * conservative and currently we don't try very hard. The rectangle is
   * in appunits and relative to the origin of this frame.
   *
   * This probably only needs to include frame bounds, glyph bounds, and
   * text decorations, but today it sometimes includes other things that
   * contribute to ink overflow.
   *
   * @param aDrawTarget a draw target that can be used if we need
   * to do measurement
   */
  virtual nsRect ComputeTightBounds(DrawTarget* aDrawTarget) const;

  /**
   * This function is similar to GetPrefISize and ComputeTightBounds: it
   * computes the left and right coordinates of a preferred tight bounding
   * rectangle for the frame. This is a rectangle that would enclose the pixels
   * that are drawn if we lay out the element without taking any optional line
   * breaks. The rectangle is in appunits and relative to the origin of this
   * frame. Currently, this function is only implemented for nsBlockFrame and
   * nsTextFrame and is used to determine intrinsic widths of MathML token
   * elements.

   * @param aContext a rendering context that can be used if we need
   * to do measurement
   * @param aX      computed left coordinate of the tight bounding rectangle
   * @param aXMost  computed intrinsic width of the tight bounding rectangle
   *
   */
  virtual nsresult GetPrefWidthTightBounds(gfxContext* aContext, nscoord* aX,
                                           nscoord* aXMost);

  /**
   * The frame is given an available size and asked for its desired
   * size.  This is the frame's opportunity to reflow its children.
   *
   * If the frame has the NS_FRAME_IS_DIRTY bit set then it is
   * responsible for completely reflowing itself and all of its
   * descendants.
   *
   * Otherwise, if the frame has the NS_FRAME_HAS_DIRTY_CHILDREN bit
   * set, then it is responsible for reflowing at least those
   * children that have NS_FRAME_HAS_DIRTY_CHILDREN or NS_FRAME_IS_DIRTY
   * set.
   *
   * If a difference in available size from the previous reflow causes
   * the frame's size to change, it should reflow descendants as needed.
   *
   * Calculates the size of this frame after reflowing (calling Reflow on, and
   * updating the size and position of) its children, as necessary.  The
   * calculated size is returned to the caller via the ReflowOutput
   * outparam.  (The caller is responsible for setting the actual size and
   * position of this frame.)
   *
   * A frame's children must _all_ be reflowed if the frame is dirty (the
   * NS_FRAME_IS_DIRTY bit is set on it).  Otherwise, individual children
   * must be reflowed if they are dirty or have the NS_FRAME_HAS_DIRTY_CHILDREN
   * bit set on them.  Otherwise, whether children need to be reflowed depends
   * on the frame's type (it's up to individual Reflow methods), and on what
   * has changed.  For example, a change in the width of the frame may require
   * all of its children to be reflowed (even those without dirty bits set on
   * them), whereas a change in its height might not.
   * (ReflowInput::ShouldReflowAllKids may be helpful in deciding whether
   * to reflow all the children, but for some frame types it might result in
   * over-reflow.)
   *
   * Note: if it's only the overflow rect(s) of a frame that need to be
   * updated, then UpdateOverflow should be called instead of Reflow.
   *
   * @param aReflowOutput <i>out</i> parameter where you should return the
   *          desired size and ascent/descent info. You should include any
   *          space you want for border/padding in the desired size you return.
   *
   *          It's okay to return a desired size that exceeds the avail
   *          size if that's the smallest you can be, i.e. it's your
   *          minimum size.
   *
   *          For an incremental reflow you are responsible for invalidating
   *          any area within your frame that needs repainting (including
   *          borders). If your new desired size is different than your current
   *          size, then your parent frame is responsible for making sure that
   *          the difference between the two rects is repainted
   *
   * @param aReflowInput information about your reflow including the reason
   *          for the reflow and the available space in which to lay out. Each
   *          dimension of the available space can either be constrained or
   *          unconstrained (a value of NS_UNCONSTRAINEDSIZE).
   *
   *          Note that the available space can be negative. In this case you
   *          still must return an accurate desired size. If you're a container
   *          you must <b>always</b> reflow at least one frame regardless of the
   *          available space
   *
   * @param aStatus a return value indicating whether the frame is complete
   *          and whether the next-in-flow is dirty and needs to be reflowed
   */
  virtual void Reflow(nsPresContext* aPresContext, ReflowOutput& aReflowOutput,
                      const ReflowInput& aReflowInput, nsReflowStatus& aStatus);

  // Option flags for ReflowChild(), FinishReflowChild(), and
  // SyncFrameViewAfterReflow().
  enum class ReflowChildFlags : uint32_t {
    Default = 0,

    // Don't position the frame's view. Set this if you don't want to
    // automatically sync the frame and view.
    NoMoveView = 1 << 0,

    // Don't move the frame. Also implies NoMoveView.
    NoMoveFrame = (1 << 1) | NoMoveView,

    // Don't size the frame's view.
    NoSizeView = 1 << 2,

    // Only applies to ReflowChild; if true, don't delete the next-in-flow, even
    // if the reflow is fully complete.
    NoDeleteNextInFlowChild = 1 << 3,

    // Only applies to FinishReflowChild.  Tell it to call
    // ApplyRelativePositioning.
    ApplyRelativePositioning = 1 << 4,
  };

  /**
   * Post-reflow hook. After a frame is reflowed this method will be called
   * informing the frame that this reflow process is complete, and telling the
   * frame the status returned by the Reflow member function.
   *
   * This call may be invoked many times, while NS_FRAME_IN_REFLOW is set,
   * before it is finally called once with a NS_FRAME_REFLOW_COMPLETE value.
   * When called with a NS_FRAME_REFLOW_COMPLETE value the NS_FRAME_IN_REFLOW
   * bit in the frame state will be cleared.
   *
   * XXX This doesn't make sense. If the frame is reflowed but not complete,
   * then the status should have IsIncomplete() equal to true.
   * XXX Don't we want the semantics to dictate that we only call this once for
   * a given reflow?
   */
  virtual void DidReflow(nsPresContext* aPresContext,
                         const ReflowInput* aReflowInput);

  void FinishReflowWithAbsoluteFrames(nsPresContext* aPresContext,
                                      ReflowOutput& aDesiredSize,
                                      const ReflowInput& aReflowInput,
                                      nsReflowStatus& aStatus,
                                      bool aConstrainBSize = true);

  /**
   * Updates the overflow areas of the frame. This can be called if an
   * overflow area of the frame's children has changed without reflowing.
   * @return true if either of the overflow areas for this frame have changed.
   */
  bool UpdateOverflow();

  /**
   * Computes any overflow area created by the frame itself (outside of the
   * frame bounds) and includes it into aOverflowAreas.
   *
   * Returns false if updating overflow isn't supported for this frame.
   * If the frame requires a reflow instead, then it is responsible
   * for scheduling one.
   */
  virtual bool ComputeCustomOverflow(mozilla::OverflowAreas& aOverflowAreas);

  /**
   * Computes any overflow area created by children of this frame and
   * includes it into aOverflowAreas. If aAsIfScrolled is true, then it behaves
   * as if we were the scrolled content frame.
   */
  virtual void UnionChildOverflow(mozilla::OverflowAreas& aOverflowAreas,
                                  bool aAsIfScrolled = false);

  // Returns the applicable overflow-clip-margin values.
  nsSize OverflowClipMargin(mozilla::PhysicalAxes aClipAxes) const;

  // Returns the axes on which this frame should apply overflow clipping.
  mozilla::PhysicalAxes ShouldApplyOverflowClipping(
      const nsStyleDisplay* aDisp) const;

  // Returns whether this frame is a block that was supposed to be a
  // scrollframe, but that was suppressed for print.
  bool IsSuppressedScrollableBlockForPrint() const;

  /**
   * Helper method used by block reflow to identify runs of text so
   * that proper word-breaking can be done.
   *
   * @return
   *    true if we can continue a "text run" through the frame. A
   *    text run is text that should be treated contiguously for line
   *    and word breaking.
   */
  virtual bool CanContinueTextRun() const;

  /**
   * Computes an approximation of the rendered text of the frame and its
   * continuations. Returns nothing for non-text frames.
   * The appended text will often not contain all the whitespace from source,
   * depending on CSS white-space processing.
   * if aEndOffset goes past end, use the text up to the string's end.
   * Call this on the primary frame for a text node.
   * aStartOffset and aEndOffset can be content offsets or offsets in the
   * rendered text, depending on aOffsetType.
   * Returns a string, as well as offsets identifying the start of the text
   * within the rendered text for the whole node, and within the text content
   * of the node.
   */
  struct RenderedText {
    nsAutoString mString;
    uint32_t mOffsetWithinNodeRenderedText;
    int32_t mOffsetWithinNodeText;
    RenderedText()
        : mOffsetWithinNodeRenderedText(0), mOffsetWithinNodeText(0) {}
  };
  enum class TextOffsetType {
    // Passed-in start and end offsets are within the content text.
    OffsetsInContentText,
    // Passed-in start and end offsets are within the rendered text.
    OffsetsInRenderedText,
  };
  enum class TrailingWhitespace {
    Trim,
    // Spaces preceding a caret at the end of a line should not be trimmed
    DontTrim,
  };
  virtual RenderedText GetRenderedText(
      uint32_t aStartOffset = 0, uint32_t aEndOffset = UINT32_MAX,
      TextOffsetType aOffsetType = TextOffsetType::OffsetsInContentText,
      TrailingWhitespace aTrimTrailingWhitespace = TrailingWhitespace::Trim) {
    return RenderedText();
  }

  /**
   * Returns true if the frame contains any non-collapsed characters.
   * This method is only available for text frames, and it will return false
   * for all other frame types.
   */
  virtual bool HasAnyNoncollapsedCharacters() { return false; }

  /**
   * Returns true if events of the given type targeted at this frame
   * should only be dispatched to the system group.
   */
  virtual bool OnlySystemGroupDispatch(mozilla::EventMessage aMessage) const {
    return false;
  }

  //
  // Accessor functions to an associated view object:
  //
  bool HasView() const { return !!(mState & NS_FRAME_HAS_VIEW); }

  template <typename SizeOrMaxSize>
  static inline bool IsIntrinsicKeyword(const SizeOrMaxSize& aSize) {
    // All keywords other than auto/none/-moz-available depend on intrinsic
    // sizes.
    return aSize.IsMaxContent() || aSize.IsMinContent() ||
           aSize.IsFitContent() || aSize.IsFitContentFunction();
  }

  // Returns true iff this frame's computed block-size property is one of the
  // intrinsic-sizing keywords.
  bool HasIntrinsicKeywordForBSize() const {
    const auto bSize =
        StylePosition()->BSize(GetWritingMode(), StyleDisplay()->mPosition);
    return IsIntrinsicKeyword(*bSize);
  }

 protected:
  virtual nsView* GetViewInternal() const {
    MOZ_ASSERT_UNREACHABLE("method should have been overridden by subclass");
    return nullptr;
  }
  virtual void SetViewInternal(nsView* aView) {
    MOZ_ASSERT_UNREACHABLE("method should have been overridden by subclass");
  }

 public:
  nsView* GetView() const {
    if (MOZ_LIKELY(!HasView())) {
      return nullptr;
    }
    nsView* view = GetViewInternal();
    MOZ_ASSERT(view, "GetViewInternal() should agree with HasView()");
    return view;
  }
  void SetView(nsView* aView);

  /**
   * Find the closest view (on |this| or an ancestor).
   * If aOffset is non-null, it will be set to the offset of |this|
   * from the returned view.
   */
  nsView* GetClosestView(nsPoint* aOffset = nullptr) const;

  /**
   * Sets the view's attributes from the frame style.
   * Call this for nsChangeHint_SyncFrameView style changes or when the view
   * has just been created.
   * @param aView the frame's view or use GetView() if nullptr is given
   */
  void SyncFrameViewProperties(nsView* aView = nullptr);

  /**
   * Get the offset between the coordinate systems of |this| and aOther.
   * Adding the return value to a point in the coordinate system of |this|
   * will transform the point to the coordinate system of aOther.
   *
   * aOther must be non-null.
   *
   * This function is fastest when aOther is an ancestor of |this|.
   *
   * This function _DOES NOT_ work across document boundaries.
   * Use this function only when |this| and aOther are in the same document.
   *
   * NOTE: this actually returns the offset from aOther to |this|, but
   * that offset is added to transform _coordinates_ from |this| to
   * aOther.
   */
  nsPoint GetOffsetTo(const nsIFrame* aOther) const;

  /**
   * Just like GetOffsetTo, but treats all scrollframes as scrolled to
   * their origin.
   */
  nsPoint GetOffsetToIgnoringScrolling(const nsIFrame* aOther) const;

  /**
   * Get the offset between the coordinate systems of |this| and aOther
   * expressed in appunits per dev pixel of |this|' document. Adding the return
   * value to a point that is relative to the origin of |this| will make the
   * point relative to the origin of aOther but in the appunits per dev pixel
   * ratio of |this|.
   *
   * aOther must be non-null.
   *
   * This function is fastest when aOther is an ancestor of |this|.
   *
   * This function works across document boundaries.
   *
   * Because this function may cross document boundaries that have different
   * app units per dev pixel ratios it needs to be used very carefully.
   *
   * NOTE: this actually returns the offset from aOther to |this|, but
   * that offset is added to transform _coordinates_ from |this| to
   * aOther.
   */
  nsPoint GetOffsetToCrossDoc(const nsIFrame* aOther) const;

  /**
   * Like GetOffsetToCrossDoc, but the caller can specify which appunits
   * to return the result in.
   */
  nsPoint GetOffsetToCrossDoc(const nsIFrame* aOther, const int32_t aAPD) const;

  /**
   * Get the rect of the frame relative to the top-left corner of the
   * screen in CSS pixels.
   * @return the CSS pixel rect of the frame relative to the top-left
   *         corner of the screen.
   */
  mozilla::CSSIntRect GetScreenRect() const;

  /**
   * Get the screen rect of the frame in app units.
   * @return the app unit rect of the frame in screen coordinates.
   */
  nsRect GetScreenRectInAppUnits() const;

  /**
   * Returns the offset from this frame to the closest geometric parent that
   * has a view. Also returns the containing view or null in case of error
   */
  void GetOffsetFromView(nsPoint& aOffset, nsView** aView) const;

  /**
   * Returns the nearest widget containing this frame. If this frame has a
   * view and the view has a widget, then this frame's widget is
   * returned, otherwise this frame's geometric parent is checked
   * recursively upwards.
   */
  nsIWidget* GetNearestWidget() const;

  /**
   * Whether the frame is a subgrid right now.
   */
  bool IsSubgrid() const;

  /**
   * Same as GetNearestWidget() above but uses an outparam to return the offset
   * of this frame to the returned widget expressed in appunits of |this| (the
   * widget might be in a different document with a different zoom).
   */
  nsIWidget* GetNearestWidget(nsPoint& aOffset) const;

  /**
   * Whether the content for this frame is disabled, used for event handling.
   */
  bool IsContentDisabled() const;

  enum class IncludeContentVisibility {
    Auto,
    Hidden,
  };

  constexpr static mozilla::EnumSet<IncludeContentVisibility>
  IncludeAllContentVisibility() {
    return {IncludeContentVisibility::Auto, IncludeContentVisibility::Hidden};
  }

  /**
   * Returns true if this frame's `content-visibility: auto` element is
   * considered relevant content.
   */
  bool IsContentRelevant() const;

  /**
   * Whether this frame hides its contents via the `content-visibility`
   * property.
   * @param aInclude specifies what kind of `content-visibility` to include.
   */
  bool HidesContent(const mozilla::EnumSet<IncludeContentVisibility>& =
                        IncludeAllContentVisibility()) const;

  /**
   * Whether this frame hides its contents via the `content-visibility`
   * property, while doing layout. This might return false when `HidesContent()`
   * returns true in the case that hidden content is being forced to lay out
   * by position or size queries from script.
   */
  bool HidesContentForLayout() const;

  /**
   * returns the closest ancestor with `content-visibility` property.
   * @param aInclude specifies what kind of `content-visibility` to include.
   */
  nsIFrame* GetClosestContentVisibilityAncestor(
      const mozilla::EnumSet<IncludeContentVisibility>& =
          IncludeAllContentVisibility()) const;

  /**
   * Returns true if this frame is entirely hidden due the `content-visibility`
   * property on an ancestor.
   * @param aInclude specifies what kind of `content-visibility` to include.
   */
  bool IsHiddenByContentVisibilityOnAnyAncestor(
      const mozilla::EnumSet<IncludeContentVisibility>& =
          IncludeAllContentVisibility()) const;

  /**
   * @brief Returns true if the frame is hidden=until-found or in a closed
   *        <details> element.
   *
   * The frame is considered hidden=until-found, if all parent frames are either
   * visible or hidden=until-found. If a hidden=until-found element is inside a
   * content-visibility:hidden element (or vice versa), this returns false.
   *
   * Similarly, if the frame is inside a closed details element, and it is not
   * hidden, this also returns true.
   */
  bool IsHiddenUntilFoundOrClosedDetails() const;

  /**
   * Returns true is this frame is hidden by its first unskipped in flow
   * ancestor due to `content-visibility`.
   */
  bool IsHiddenByContentVisibilityOfInFlowParentForLayout() const;

  /**
   * Returns true if this frame has a SelectionType::eNormal type selection in
   * somewhere in its subtree of frames. This is used to determine content
   * relevancy for `content-visibility: auto`.
   */
  bool HasSelectionInSubtree();

  /**
   * Update the whether or not this frame is considered relevant content for the
   * purposes of `content-visibility: auto` according to the rules specified in
   * https://drafts.csswg.org/css-contain-2/#relevant-to-the-user.
   * Returns true if the over-all relevancy changed.
   */
  bool UpdateIsRelevantContent(const ContentRelevancy& aRelevancyToUpdate);

  /**
   * Get the "type" of the frame.
   *
   * @see mozilla::LayoutFrameType
   */
  mozilla::LayoutFrameType Type() const {
    MOZ_ASSERT(uint8_t(mClass) < std::size(sLayoutFrameTypes));
    return sLayoutFrameTypes[uint8_t(mClass)];
  }

  /** Return this frame's class id */
  ClassID GetClassID() const { return mClass; }

  /**
   * Get the type flags of the frame.
   *
   * @see mozilla::LayoutFrameType
   */
  ClassFlags GetClassFlags() const {
    MOZ_ASSERT(uint8_t(mClass) < std::size(sLayoutFrameClassFlags));
    return sLayoutFrameClassFlags[uint8_t(mClass)];
  }

  bool HasAnyClassFlag(ClassFlags aFlag) const {
    return bool(GetClassFlags() & aFlag);
  }

  /**
   * Is this a leaf frame?  Frames that want the frame constructor to be able to
   * construct kids for them should return false, all others should return true.
   *
   * Note that returning true here does not mean that the frame _can't_ have
   * kids. It could still have kids created via nsIAnonymousContentCreator.
   *
   * Returning true indicates that "normal" (non-anonymous, CSS generated
   * content, etc) children should not be constructed.
   */
  bool IsLeaf() const {
    auto bits = GetClassFlags();
    if (MOZ_UNLIKELY(bits & ClassFlags::LeafDynamic)) {
      return IsLeafDynamic();
    }
    return bool(bits & ClassFlags::Leaf);
  }
  virtual bool IsLeafDynamic() const { return false; }

#define CLASS_FLAG_METHOD(name_, flag_) \
  bool name_() const { return HasAnyClassFlag(ClassFlags::flag_); }
#define CLASS_FLAG_METHOD0(name_) CLASS_FLAG_METHOD(name_, name_)

  CLASS_FLAG_METHOD(IsMathMLFrame, MathML);
  CLASS_FLAG_METHOD(IsSVGFrame, SVG);
  CLASS_FLAG_METHOD(IsSVGContainerFrame, SVGContainer);
  CLASS_FLAG_METHOD(IsBidiInlineContainer, BidiInlineContainer);
  CLASS_FLAG_METHOD(IsLineParticipant, LineParticipant);
  CLASS_FLAG_METHOD(IsReplaced, Replaced);
  CLASS_FLAG_METHOD(HasReplacedSizing, ReplacedSizing);
  CLASS_FLAG_METHOD(IsTablePart, TablePart);
  CLASS_FLAG_METHOD0(CanContainOverflowContainers)
  CLASS_FLAG_METHOD0(SupportsCSSTransforms);
  CLASS_FLAG_METHOD0(SupportsContainLayoutAndPaint)
  CLASS_FLAG_METHOD0(SupportsAspectRatio)
  CLASS_FLAG_METHOD(IsSVGRenderingObserverContainer,
                    SVGRenderingObserverContainer);

#undef CLASS_FLAG_METHOD
#undef CLASS_FLAG_METHOD0

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunknown-pragmas"
#  pragma clang diagnostic ignored "-Wtautological-unsigned-zero-compare"
#endif

#define FRAME_TYPE(name_, first_class_, last_class_)                 \
  bool Is##name_##Frame() const {                                    \
    return uint8_t(mClass) >= uint8_t(ClassID::first_class_##_id) && \
           uint8_t(mClass) <= uint8_t(ClassID::last_class_##_id);    \
  }
#include "mozilla/FrameTypeList.h"
#undef FRAME_TYPE

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif
#ifdef __clang__
#  pragma clang diagnostic pop
#endif

  /**
   * Returns a transformation matrix that converts points in this frame's
   * coordinate space to points in some ancestor frame's coordinate space.
   * The frame decides which ancestor it will use as a reference point.
   * If this frame has no ancestor, aOutAncestor will be set to null.
   *
   * @param aViewportType specifies whether the starting point is layout
   *   or visual coordinates
   * @param aStopAtAncestor don't look further than aStopAtAncestor. If null,
   *   all ancestors (including across documents) will be traversed.
   * @param aOutAncestor [out] The ancestor frame the frame has chosen. If this
   *   frame has no ancestor, *aOutAncestor will be set to null. If this frame
   *   IsTransformed(), then *aOutAncestor will be the parent frame.
   * @return A Matrix4x4 that converts points in the coordinate space
   *   RelativeTo{this, aViewportType} into points in aOutAncestor's
   *   coordinate space.
   */
  enum {
    IN_CSS_UNITS = 1 << 0,
    STOP_AT_STACKING_CONTEXT_AND_DISPLAY_PORT = 1 << 1
  };
  Matrix4x4Flagged GetTransformMatrix(mozilla::ViewportType aViewportType,
                                      mozilla::RelativeTo aStopAtAncestor,
                                      nsIFrame** aOutAncestor,
                                      uint32_t aFlags = 0) const;

  /**
   * Return true if this frame's preferred size property or max size property
   * contains a percentage value that should be resolved against zero when
   * calculating its min-content contribution in the corresponding axis.
   *
   * This is a special case for webcompat required by CSS Sizing 3 §5.2.1c
   * https://drafts.csswg.org/css-sizing-3/#replaced-percentage-min-contribution,
   * and applies only to some replaced elements and form control elements. See
   * CSS Sizing 3 §5.2.2 for the list of elements this rule applies to.
   * https://drafts.csswg.org/css-sizing-3/#min-content-zero
   *
   * Bug 1463700: some callers may not match the spec by resolving the entire
   * preferred size property or max size property against zero.
   */
  bool IsPercentageResolvedAgainstZero(
      const mozilla::StyleSize& aStyleSize,
      const mozilla::StyleMaxSize& aStyleMaxSize) const;

  // Type of preferred size/min size/max size.
  enum class SizeProperty { Size, MinSize, MaxSize };
  /**
   * This is simliar to the above method but accepts LengthPercentage. Return
   * true if the frame's preferred size property or max size property contains
   * a percentage value that should be resolved against zero. For min size, it
   * always returns true.
   */
  bool IsPercentageResolvedAgainstZero(const mozilla::LengthPercentage& aSize,
                                       SizeProperty aProperty) const;

  /**
   * Returns true if the frame is a block wrapper.
   */
  bool IsBlockWrapper() const;

  /**
   * Returns true if the frame is an instance of nsBlockFrame or one of its
   * subclasses.
   */
  bool IsBlockFrameOrSubclass() const;

  /**
   * Returns true if the frame is an instance of nsImageFrame or one of its
   * subclasses.
   */
  bool IsImageFrameOrSubclass() const;

  /**
   * Returns true if the frame is an instance of ScrollContainerFrame or one of
   * its subclasses.
   */
  bool IsScrollContainerOrSubclass() const;

  /**
   * Get this frame's CSS containing block.
   *
   * The algorithm is defined in
   * http://www.w3.org/TR/CSS2/visudet.html#containing-block-details.
   *
   * NOTE: This is guaranteed to return a non-null pointer when invoked on any
   * frame other than the root frame.
   *
   * Requires SKIP_SCROLLED_FRAME to get behaviour matching the spec, otherwise
   * it can return anonymous inner scrolled frames. Bug 1204044 is filed for
   * investigating whether any of the callers actually require the default
   * behaviour.
   */
  enum {
    // If the containing block is an anonymous scrolled frame, then skip over
    // this and return the outer scroll frame.
    SKIP_SCROLLED_FRAME = 0x01
  };
  nsIFrame* GetContainingBlock(uint32_t aFlags,
                               const nsStyleDisplay* aStyleDisplay) const;
  nsIFrame* GetContainingBlock(uint32_t aFlags = 0) const {
    return GetContainingBlock(aFlags, StyleDisplay());
  }

  /**
   * If this frame can be a block container, i.e. whether it can serve as a
   * containing block for its descendants. See also GetNearestBlockContainer()
   * and GetContainingBlock().
   */
  bool IsBlockContainer() const;

  /**
   * Is this frame a containing block for floating elements?
   * Note that very few frames are, so default to false.
   */
  virtual bool IsFloatContainingBlock() const { return false; }

  /**
   * Marks all display items created by this frame as needing a repaint,
   * and calls SchedulePaint() if requested and one is not already pending.
   *
   * This includes all display items created by this frame, including
   * container types.
   *
   * @param aDisplayItemKey If specified, only issues an invalidate
   * if this frame painted a display item of that type during the
   * previous paint. SVG rendering observers are always notified.
   * @param aRebuildDisplayItems If true, then adds this frame to the
   * list of modified frames for display list building. Only pass false
   * if you're sure that the relevant display items will be rebuilt
   * already (possibly by an ancestor being in the modified list).
   */
  virtual void InvalidateFrame(uint32_t aDisplayItemKey = 0,
                               bool aRebuildDisplayItems = true);

  /**
   * Same as InvalidateFrame(), but only mark a fixed rect as needing
   * repainting.
   *
   * @param aRect The rect to invalidate, relative to the TopLeft of the
   * frame's border box.
   * @param aDisplayItemKey If specified, only issues an invalidate
   * if this frame painted a display item of that type during the
   * previous paint. SVG rendering observers are always notified.
   * @param aRebuildDisplayItems If true, then adds this frame to the
   * list of modified frames for display list building. Only pass false
   * if you're sure that the relevant display items will be rebuilt
   * already (possibly by an ancestor being in the modified list).
   */
  virtual void InvalidateFrameWithRect(const nsRect& aRect,
                                       uint32_t aDisplayItemKey = 0,
                                       bool aRebuildDisplayItems = true);

  /**
   * Calls InvalidateFrame() on all frames descendant frames (including
   * this one).
   *
   * This function doesn't walk through placeholder frames to invalidate
   * the out-of-flow frames.
   *
   * @param aRebuildDisplayItems If true, then adds this frame to the
   * list of modified frames for display list building. Only pass false
   * if you're sure that the relevant display items will be rebuilt
   * already (possibly by an ancestor being in the modified list).
   */
  void InvalidateFrameSubtree(bool aRebuildDisplayItems = true);

  /**
   * Called when a frame is about to be removed and needs to be invalidated.
   * Normally does nothing since DLBI handles removed frames.
   */
  virtual void InvalidateFrameForRemoval() {}

  /**
   * Checks if a frame has had InvalidateFrame() called on it since the
   * last paint.
   *
   * If true, then the invalid rect is returned in aRect, with an
   * empty rect meaning all pixels drawn by this frame should be
   * invalidated.
   * If false, aRect is left unchanged.
   */
  bool IsInvalid(nsRect& aRect);

  /**
   * Check if any frame within the frame subtree (including this frame)
   * returns true for IsInvalid().
   */
  bool HasInvalidFrameInSubtree() {
    return HasAnyStateBits(NS_FRAME_NEEDS_PAINT |
                           NS_FRAME_DESCENDANT_NEEDS_PAINT);
  }

  /**
   * Removes the invalid state from the current frame and all
   * descendant frames.
   */
  void ClearInvalidationStateBits();

  /**
   * Ensures that the refresh driver is running, and schedules a view
   * manager flush on the next tick.
   *
   * The view manager flush will update the layer tree, repaint any
   * invalid areas in the layer tree and schedule a layer tree
   * composite operation to display the layer tree.
   *
   * In general it is not necessary for frames to call this when they change.
   * For example, changes that result in a reflow will have this called for
   * them by PresContext::DoReflow when the reflow begins. Style changes that
   * do not trigger a reflow should have this called for them by
   * DoApplyRenderingChangeToTree.
   *
   * @param aType PAINT_COMPOSITE_ONLY : No changes have been made
   * that require a layer tree update, so only schedule a layer
   * tree composite.
   */
  enum PaintType { PAINT_DEFAULT = 0, PAINT_COMPOSITE_ONLY };
  void SchedulePaint(PaintType aType = PAINT_DEFAULT,
                     bool aFrameChanged = true);

  // Similar to SchedulePaint() but without calling
  // InvalidateRenderingObservers() for SVG.
  void SchedulePaintWithoutInvalidatingObservers(
      PaintType aType = PAINT_DEFAULT);

  /**
   * Checks if the layer tree includes a dedicated layer for this
   * frame/display item key pair, and invalidates at least aDamageRect
   * area within that layer.
   *
   * If no layer is found, calls InvalidateFrame() instead.
   *
   * @param aDamageRect Area of the layer to invalidate.
   * @param aFrameDamageRect If no layer is found, the area of the frame to
   *                         invalidate. If null, the entire frame will be
   *                         invalidated.
   * @param aDisplayItemKey Display item type.
   * @param aFlags UPDATE_IS_ASYNC : Will skip the invalidation
   * if the found layer is being composited by a remote
   * compositor.
   * @return Layer, if found, nullptr otherwise.
   */
  enum { UPDATE_IS_ASYNC = 1 << 0 };
  void InvalidateLayer(DisplayItemType aDisplayItemKey,
                       const nsIntRect* aDamageRect = nullptr,
                       const nsRect* aFrameDamageRect = nullptr,
                       uint32_t aFlags = 0);

  void MarkNeedsDisplayItemRebuild();

  /**
   * Returns a rect that encompasses everything that might be painted by
   * this frame.  This includes this frame, all its descendant frames, this
   * frame's outline, and descendant frames' outline, but does not include
   * areas clipped out by the CSS "overflow" and "clip" properties.
   *
   * HasOverflowAreas() (below) will return true when this overflow
   * rect has been explicitly set, even if it matches mRect.
   * XXX Note: because of a space optimization using the formula above,
   * during reflow this function does not give accurate data if
   * FinishAndStoreOverflow has been called but mRect hasn't yet been
   * updated yet.  FIXME: This actually isn't true, but it should be.
   *
   * The ink overflow rect should NEVER be used for things that
   * affect layout.  The scrollable overflow rect is permitted to affect
   * layout.
   *
   * @return the rect relative to this frame's origin, but after
   * CSS transforms have been applied (i.e. not really this frame's coordinate
   * system, and may not contain the frame's border-box, e.g. if there
   * is a CSS transform scaling it down)
   */
  nsRect InkOverflowRect() const {
    return GetOverflowRect(mozilla::OverflowType::Ink);
  }

  /**
   * Returns a rect that encompasses the area of this frame that the
   * user should be able to scroll to reach.  This is similar to
   * InkOverflowRect, but does not include outline or shadows, and
   * may in the future include more margins than ink overflow does.
   * It does not include areas clipped out by the CSS "overflow" and
   * "clip" properties.
   *
   * HasOverflowAreas() (below) will return true when this overflow
   * rect has been explicitly set, even if it matches mRect.
   * XXX Note: because of a space optimization using the formula above,
   * during reflow this function does not give accurate data if
   * FinishAndStoreOverflow has been called but mRect hasn't yet been
   * updated yet.
   *
   * @return the rect relative to this frame's origin, but after
   * CSS transforms have been applied (i.e. not really this frame's coordinate
   * system, and may not contain the frame's border-box, e.g. if there
   * is a CSS transform scaling it down)
   */
  nsRect ScrollableOverflowRect() const {
    return GetOverflowRect(mozilla::OverflowType::Scrollable);
  }

  mozilla::OverflowAreas GetOverflowAreas() const;

  /**
   * Same as GetOverflowAreas, except in this frame's coordinate
   * system (before transforms are applied).
   *
   * @return the overflow areas relative to this frame, before any CSS
   * transforms have been applied, i.e. in this frame's coordinate system
   */
  mozilla::OverflowAreas GetOverflowAreasRelativeToSelf() const;

  /**
   * Same as GetOverflowAreas, except relative to the parent frame.
   *
   * @return the overflow area relative to the parent frame, in the parent
   * frame's coordinate system
   */
  mozilla::OverflowAreas GetOverflowAreasRelativeToParent() const;

  /**
   * Same as GetOverflowAreasRelativeToParent(), except that it also unions in
   * the normal position overflow area if this frame is relatively or sticky
   * positioned.
   *
   * @return the overflow area relative to the parent frame, in the parent
   * frame's coordinate system
   */
  mozilla::OverflowAreas GetActualAndNormalOverflowAreasRelativeToParent()
      const;

  /**
   * Same as ScrollableOverflowRect, except relative to the parent
   * frame.
   *
   * @return the rect relative to the parent frame, in the parent frame's
   * coordinate system
   */
  nsRect ScrollableOverflowRectRelativeToParent() const;

  /**
   * Same as ScrollableOverflowRect, except in this frame's coordinate
   * system (before transforms are applied).
   *
   * @return the rect relative to this frame, before any CSS transforms have
   * been applied, i.e. in this frame's coordinate system
   */
  nsRect ScrollableOverflowRectRelativeToSelf() const;

  /**
   * Like InkOverflowRect, except in this frame's
   * coordinate system (before transforms are applied).
   *
   * @return the rect relative to this frame, before any CSS transforms have
   * been applied, i.e. in this frame's coordinate system
   */
  nsRect InkOverflowRectRelativeToSelf() const;

  /**
   * Same as InkOverflowRect, except relative to the parent
   * frame.
   *
   * @return the rect relative to the parent frame, in the parent frame's
   * coordinate system
   */
  nsRect InkOverflowRectRelativeToParent() const;

  /**
   * Returns this frame's ink overflow rect as it would be before taking
   * account of SVG effects or transforms. The rect returned is relative to
   * this frame.
   */
  nsRect PreEffectsInkOverflowRect() const;

  /**
   * Store the overflow area in the frame's mOverflow.mInkOverflowDeltas
   * fields or as a frame property in OverflowAreasProperty() so that it can
   * be retrieved later without reflowing the frame. Returns true if either of
   * the overflow areas changed.
   */
  bool FinishAndStoreOverflow(mozilla::OverflowAreas& aOverflowAreas,
                              nsSize aNewSize, nsSize* aOldSize = nullptr,
                              const nsStyleDisplay* aStyleDisplay = nullptr);

  bool FinishAndStoreOverflow(ReflowOutput* aMetrics,
                              const nsStyleDisplay* aStyleDisplay = nullptr) {
    return FinishAndStoreOverflow(aMetrics->mOverflowAreas,
                                  nsSize(aMetrics->Width(), aMetrics->Height()),
                                  nullptr, aStyleDisplay);
  }

  /**
   * Returns whether the frame has an overflow rect that is different from
   * its border-box.
   */
  bool HasOverflowAreas() const {
    return mOverflow.mType != OverflowStorageType::None;
  }

  /**
   * Removes any stored overflow rects (visual and scrollable) from the frame.
   * Returns true if the overflow changed.
   */
  bool ClearOverflowRects();

  /**
   * Determine whether borders, padding, margins etc should NOT be applied
   * on certain sides of the frame.
   * @see mozilla::Sides in gfx/2d/BaseMargin.h
   * @see mozilla::LogicalSides in layout/generic/WritingModes.h
   *
   * @note (See also bug 743402, comment 11) GetSkipSides() checks to see
   *       if this frame has a previous or next continuation to determine
   *       if a side should be skipped.
   *       So this only works after the entire frame tree has been reflowed.
   *       During reflow, if this frame can be split in the block axis, you
   *       should use nsSplittableFrame::PreReflowBlockLevelLogicalSkipSides().
   */
  Sides GetSkipSides() const;
  virtual LogicalSides GetLogicalSkipSides() const {
    return LogicalSides(mWritingMode);
  }

  /**
   * @returns true if this frame is selected.
   */
  bool IsSelected() const {
    return (GetContent() && GetContent()->IsMaybeSelected()) ? IsFrameSelected()
                                                             : false;
  }

  /**
   * Shouldn't be called if this is a `nsTextFrame`. Call the
   * `nsTextFrame::SelectionStateChanged` overload instead.
   */
  void SelectionStateChanged() {
    MOZ_ASSERT(!IsTextFrame());
    InvalidateFrameSubtree();  // TODO: should this deal with continuations?
  }

  /**
   * Called to discover where this frame, or a parent frame has user-select
   * style applied, which affects that way that it is selected.
   *
   * @param aSelectStyle out param. Returns the type of selection style found
   * (using values defined in nsStyleConsts.h).
   *
   * @return Whether the frame can be selected (i.e. is not affected by
   * user-select: none)
   */
  bool IsSelectable(mozilla::StyleUserSelect* aSelectStyle) const;

  /**
   * Returns whether this frame should have the content-block-size of a line,
   * even if empty.
   */
  bool ShouldHaveLineIfEmpty() const;

  /**
   * Called to retrieve the SelectionController associated with the frame.
   *
   * @param aSelCon will contain the selection controller associated with
   * the frame.
   */
  nsresult GetSelectionController(nsPresContext* aPresContext,
                                  nsISelectionController** aSelCon);

  /**
   * Call to get nsFrameSelection for this frame.
   */
  already_AddRefed<nsFrameSelection> GetFrameSelection();

  /**
   * GetConstFrameSelection returns an object which methods are safe to use for
   * example in nsIFrame code.
   */
  const nsFrameSelection* GetConstFrameSelection() const;

  /**
   * called to find the previous/next character, word, or line. Returns the
   * actual nsIFrame and the frame offset. THIS DOES NOT CHANGE SELECTION STATE.
   * Uses frame's begin selection state to start. If no selection on this frame
   * will return NS_ERROR_FAILURE.
   *
   * @param aPos is defined in nsFrameSelection
   */
  virtual nsresult PeekOffset(mozilla::PeekOffsetStruct* aPos);

 private:
  nsresult PeekOffsetForCharacter(mozilla::PeekOffsetStruct* aPos,
                                  int32_t aOffset);
  nsresult PeekOffsetForWord(mozilla::PeekOffsetStruct* aPos, int32_t aOffset);
  nsresult PeekOffsetForLine(mozilla::PeekOffsetStruct* aPos);
  nsresult PeekOffsetForLineEdge(mozilla::PeekOffsetStruct* aPos);

  /**
   * Search for the first paragraph boundary before or after the given position
   * @param  aPos See description in nsFrameSelection.h. The following fields
   *              are used by this method:
   *              Input: mDirection
   *              Output: mResultContent, mContentOffset
   */
  nsresult PeekOffsetForParagraph(mozilla::PeekOffsetStruct* aPos);

 public:
  // given a frame five me the first/last leaf available
  // XXX Robert O'Callahan wants to move these elsewhere
  // FIXME: Only GetLastLeaf() never returns a leaf frame in native anonymous
  // subtrees under aFrame.  However, GetFirstLeaf() may return a leaf frame
  // in a native anonymous subtree.
  static void GetLastLeaf(nsIFrame** aFrame);
  static void GetFirstLeaf(nsIFrame** aFrame);

  struct SelectablePeekReport {
    /** the previous/next selectable leaf frame */
    nsIFrame* mFrame = nullptr;
    /**
     * 0 indicates that we arrived at the beginning of the output frame; -1
     * indicates that we arrived at its end.
     */
    int32_t mOffset = 0;
    /** whether the input frame and the returned frame are on different lines */
    bool mJumpedLine = false;
    /** whether we met a hard break between the input and the returned frame */
    bool mJumpedHardBreak = false;
    /** whether we met a child placeholder frame */
    bool mFoundPlaceholder = false;
    /** whether we jumped over a non-selectable frame during the search */
    bool mMovedOverNonSelectableText = false;
    /** whether we met selectable text frame that isn't editable during the
     *  search */
    bool mHasSelectableFrame = false;
    /** whether we ignored a br frame */
    bool mIgnoredBrFrame = false;

    FrameSearchResult PeekOffsetNoAmount(bool aForward) {
      return mFrame->PeekOffsetNoAmount(aForward, &mOffset);
    }
    FrameSearchResult PeekOffsetCharacter(bool aForward,
                                          PeekOffsetCharacterOptions aOptions) {
      return mFrame->PeekOffsetCharacter(aForward, &mOffset, aOptions);
    };

    /** Transfers frame and offset info for PeekOffset() result */
    void TransferTo(mozilla::PeekOffsetStruct& aPos) const;
    bool Failed() { return !mFrame; }

    explicit SelectablePeekReport(nsIFrame* aFrame = nullptr,
                                  int32_t aOffset = 0)
        : mFrame(aFrame), mOffset(aOffset) {}
    MOZ_IMPLICIT SelectablePeekReport(
        const mozilla::GenericErrorResult<nsresult>&& aErr);
  };

  /**
   * Called to find the previous/next non-anonymous selectable leaf frame.
   *
   * @param aDirection the direction to move in (eDirPrevious or eDirNext)
   * @param aOptions the other options which is same as
   * PeekOffsetStruct::mOptions.
   * @param aAncestorLimiter if set, this refers only the frames for its
   * descendants.
   * FIXME: Due to the include hell, we cannot use the alias, PeekOffsetOptions
   * is not available in this header file.
   */
  SelectablePeekReport GetFrameFromDirection(
      nsDirection aDirection,
      const mozilla::EnumSet<mozilla::PeekOffsetOption>& aOptions,
      const mozilla::dom::Element* aAncestorLimiter);
  SelectablePeekReport GetFrameFromDirection(
      const mozilla::PeekOffsetStruct& aPos);

  /**
   * Return:
   * (1) the containing block frame for a line; i.e. the frame which
   * supports a line iterator, or null if none can be found; and
   * (2) the frame to use to get a line number, which will be direct child of
   * the returned containing block.
   * @param aLockScroll true to avoid breaking outside scrollframes.
   */
  std::pair<nsIFrame*, nsIFrame*> GetContainingBlockForLine(
      bool aLockScroll) const;

 private:
  Result<bool, nsresult> IsVisuallyAtLineEdge(nsILineIterator* aLineIterator,
                                              int32_t aLine,
                                              nsDirection aDirection);
  Result<bool, nsresult> IsLogicallyAtLineEdge(nsILineIterator* aLineIterator,
                                               int32_t aLine,
                                               nsDirection aDirection);

 public:
  /**
   * Called to tell a frame that one of its child frames is dirty (i.e.,
   * has the NS_FRAME_IS_DIRTY *or* NS_FRAME_HAS_DIRTY_CHILDREN bit
   * set).  This should always set the NS_FRAME_HAS_DIRTY_CHILDREN on
   * the frame, and may do other work.
   */
  virtual void ChildIsDirty(nsIFrame* aChild);

  /**
   * Called to retrieve this frame's accessible.
   * If this frame implements Accessibility return a valid accessible
   * If not return NS_ERROR_NOT_IMPLEMENTED.
   * Note: LocalAccessible must be refcountable. Do not implement directly on
   * your frame Use a mediatior of some kind.
   */
#ifdef ACCESSIBILITY
  virtual mozilla::a11y::AccType AccessibleType();
#endif

  /**
   * Get the frame whose style should be the parent of this frame's style (i.e.,
   * provide the parent style).
   *
   * This frame must either be an ancestor of this frame or a child.  If
   * this returns a child frame, then the child frame must be sure to
   * return a grandparent or higher!  Furthermore, if a child frame is
   * returned it must have the same GetContent() as this frame.
   *
   * @param aProviderFrame (out) the frame associated with the returned value
   *     or nullptr if the style is for display:contents content.
   * @return The style that should be the parent of this frame's style. Null is
   *         permitted, and means that this frame's style should be the root of
   *         the style tree.
   */
  virtual ComputedStyle* GetParentComputedStyle(
      nsIFrame** aProviderFrame) const {
    return DoGetParentComputedStyle(aProviderFrame);
  }

  /**
   * Do the work for getting the parent ComputedStyle frame so that
   * other frame's |GetParentComputedStyle| methods can call this
   * method on *another* frame.  (This function handles out-of-flow
   * frames by using the frame manager's placeholder map and it also
   * handles block-within-inline and generated content wrappers.)
   *
   * @param aProviderFrame (out) the frame associated with the returned value
   *   or null if the ComputedStyle is for display:contents content.
   * @return The ComputedStyle that should be the parent of this frame's
   *   ComputedStyle.  Null is permitted, and means that this frame's
   *   ComputedStyle should be the root of the ComputedStyle tree.
   */
  ComputedStyle* DoGetParentComputedStyle(nsIFrame** aProviderFrame) const;

  /**
   * Adjust the given parent frame to the right ComputedStyle parent frame for
   * the child, given the pseudo-type of the prospective child.  This handles
   * things like walking out of table pseudos and so forth.
   *
   * @param aProspectiveParent what GetParent() on the child returns.
   *                           Must not be null.
   * @param aChildPseudo the child's pseudo type, if any.
   */
  static nsIFrame* CorrectStyleParentFrame(
      nsIFrame* aProspectiveParent, mozilla::PseudoStyleType aChildPseudo);

  /**
   * Called by RestyleManager to update the style of anonymous boxes
   * directly associated with this frame.
   *
   * The passed-in ServoRestyleState can be used to create new ComputedStyles as
   * needed, as well as posting changes to the change list.
   *
   * It's guaranteed to already have a change in it for this frame and this
   * frame's content.
   *
   * This function will be called after this frame's style has already been
   * updated.  This function will only be called on frames which have the
   * NS_FRAME_OWNS_ANON_BOXES bit set.
   */
  void UpdateStyleOfOwnedAnonBoxes(mozilla::ServoRestyleState& aRestyleState) {
    if (HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES)) {
      DoUpdateStyleOfOwnedAnonBoxes(aRestyleState);
    }
  }

  mozilla::ContainSizeAxes GetContainSizeAxes() const {
    return StyleDisplay()->GetContainSizeAxes(*this);
  }

  // Common steps to all replaced elements given an unconstrained intrinsic
  // size.
  mozilla::IntrinsicSize FinishIntrinsicSize(
      const mozilla::ContainSizeAxes& aAxes,
      const mozilla::IntrinsicSize& aUncontainedSize) const {
    auto result = aAxes.ContainIntrinsicSize(aUncontainedSize, *this);
    result.Zoom(Style()->EffectiveZoom());
    return result;
  }

  Maybe<nscoord> ContainIntrinsicBSize(nscoord aNoneValue = 0) const {
    return GetContainSizeAxes().ContainIntrinsicBSize(*this, aNoneValue);
  }

  Maybe<nscoord> ContainIntrinsicISize(nscoord aNoneValue = 0) const {
    return GetContainSizeAxes().ContainIntrinsicISize(*this, aNoneValue);
  }

 protected:
  // This does the actual work of UpdateStyleOfOwnedAnonBoxes.  It calls
  // AppendDirectlyOwnedAnonBoxes to find all of the anonymous boxes
  // owned by this frame, and then updates styles on each of them.
  void DoUpdateStyleOfOwnedAnonBoxes(mozilla::ServoRestyleState& aRestyleState);

  // A helper for DoUpdateStyleOfOwnedAnonBoxes for the specific case
  // of the owned anon box being a child of this frame.
  void UpdateStyleOfChildAnonBox(nsIFrame* aChildFrame,
                                 mozilla::ServoRestyleState& aRestyleState);

  // Allow ServoRestyleState to call UpdateStyleOfChildAnonBox.
  friend class mozilla::ServoRestyleState;

 public:
  // A helper both for UpdateStyleOfChildAnonBox, and to update frame-backed
  // pseudo-elements in RestyleManager.
  //
  // This gets a ComputedStyle that will be the new style for `aChildFrame`, and
  // takes care of updating it, calling CalcStyleDifference, and adding to the
  // change list as appropriate.
  //
  // If aContinuationComputedStyle is not Nothing, it should be used for
  // continuations instead of aNewComputedStyle.  In either case, changehints
  // are only computed based on aNewComputedStyle.
  //
  // Returns the generated change hint for the frame.
  static nsChangeHint UpdateStyleOfOwnedChildFrame(
      nsIFrame* aChildFrame, ComputedStyle* aNewComputedStyle,
      mozilla::ServoRestyleState& aRestyleState,
      const Maybe<ComputedStyle*>& aContinuationComputedStyle = Nothing());

  struct OwnedAnonBox {
    typedef void (*UpdateStyleFn)(nsIFrame* aOwningFrame, nsIFrame* aAnonBox,
                                  mozilla::ServoRestyleState& aRestyleState);

    explicit OwnedAnonBox(nsIFrame* aAnonBoxFrame,
                          UpdateStyleFn aUpdateStyleFn = nullptr)
        : mAnonBoxFrame(aAnonBoxFrame), mUpdateStyleFn(aUpdateStyleFn) {}

    nsIFrame* mAnonBoxFrame;
    UpdateStyleFn mUpdateStyleFn;
  };

  /**
   * Appends information about all of the anonymous boxes owned by this frame,
   * including other anonymous boxes owned by those which this frame owns
   * directly.
   */
  void AppendOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) {
    if (HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES)) {
      if (IsInlineFrame()) {
        // See comment in nsIFrame::DoUpdateStyleOfOwnedAnonBoxes for why
        // we skip nsInlineFrames.
        return;
      }
      DoAppendOwnedAnonBoxes(aResult);
    }
  }

 protected:
  // This does the actual work of AppendOwnedAnonBoxes.
  void DoAppendOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult);

 public:
  /**
   * Hook subclasses can override to return their owned anonymous boxes.
   *
   * This function only appends anonymous boxes that are directly owned by
   * this frame, i.e. direct children or (for certain frames) a wrapper
   * parent, unlike AppendOwnedAnonBoxes, which will append all anonymous
   * boxes transitively owned by this frame.
   */
  virtual void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult);

  /**
   * Determines whether a frame is visible for painting;
   * taking into account whether it is painting a selection or printing.
   */
  bool IsVisibleForPainting() const;
  /**
   * Determines whether a frame is visible for painting or collapsed;
   * taking into account whether it is painting a selection or printing,
   */
  bool IsVisibleOrCollapsedForPainting() const;

  /**
   * Determines if this frame is a stacking context.
   */
  bool IsStackingContext(const nsStyleDisplay*, const nsStyleEffects*);
  bool IsStackingContext();

  // Whether we should paint backgrounds or not.
  struct ShouldPaintBackground {
    bool mColor = false;
    bool mImage = false;
  };
  ShouldPaintBackground ComputeShouldPaintBackground() const;

  /**
   * Determine whether the frame is logically empty, which is roughly
   * whether the layout would be the same whether or not the frame is
   * present.  Placeholder frames should return true.  Block frames
   * should be considered empty whenever margins collapse through them,
   * even though those margins are relevant.  Text frames containing
   * only whitespace that does not contribute to the height of the line
   * should return true.
   */
  virtual bool IsEmpty();
  /**
   * Return the same as IsEmpty(). This may only be called after the frame
   * has been reflowed and before any further style or content changes.
   */
  virtual bool CachedIsEmpty();
  /**
   * Determine whether the frame is logically empty, assuming that all
   * its children are empty.
   */
  virtual bool IsSelfEmpty();

  /**
   * IsGeneratedContentFrame returns whether a frame corresponds to
   * generated content
   *
   * @return whether the frame correspods to generated content
   */
  bool IsGeneratedContentFrame() const {
    return HasAnyStateBits(NS_FRAME_GENERATED_CONTENT);
  }

  /**
   * IsPseudoFrame returns whether a frame is a pseudo frame (eg an
   * anonymous table-row frame created for a CSS table-cell without an
   * enclosing table-row.
   *
   * @param aParentContent the content node corresponding to the parent frame
   * @return whether the frame is a pseudo frame
   */
  bool IsPseudoFrame(const nsIContent* aParentContent) {
    return mContent == aParentContent;
  }

  /**
   * Support for reading and writing properties on the frame.
   * These call through to the frame's FrameProperties object, if it
   * exists, but avoid creating it if no property is ever set.
   */
  template <typename T>
  FrameProperties::PropertyType<T> GetProperty(
      FrameProperties::Descriptor<T> aProperty,
      bool* aFoundResult = nullptr) const {
    return mProperties.Get(aProperty, aFoundResult);
  }

  template <typename T>
  bool HasProperty(FrameProperties::Descriptor<T> aProperty) const {
    return mProperties.Has(aProperty);
  }

  /**
   * Add a property, or update an existing property for the given descriptor.
   *
   * Note: This function asserts if updating an existing nsFrameList property.
   */
  template <typename T>
  void SetProperty(FrameProperties::Descriptor<T> aProperty,
                   FrameProperties::PropertyType<T> aValue) {
    if constexpr (std::is_same_v<T, nsFrameList>) {
      MOZ_ASSERT(aValue, "Shouldn't set nullptr to a nsFrameList property!");
      MOZ_ASSERT(!HasProperty(aProperty),
                 "Shouldn't update an existing nsFrameList property!");
    }
    mProperties.Set(aProperty, aValue, this);
  }

  // Unconditionally add a property; use ONLY if the descriptor is known
  // to NOT already be present.
  template <typename T>
  void AddProperty(FrameProperties::Descriptor<T> aProperty,
                   FrameProperties::PropertyType<T> aValue) {
    mProperties.Add(aProperty, aValue);
  }

  /**
   * Remove a property and return its value without destroying it. May return
   * nullptr.
   *
   * Note: The caller is responsible for handling the life cycle of the returned
   * value.
   */
  template <typename T>
  [[nodiscard]] FrameProperties::PropertyType<T> TakeProperty(
      FrameProperties::Descriptor<T> aProperty, bool* aFoundResult = nullptr) {
    return mProperties.Take(aProperty, aFoundResult);
  }

  template <typename T>
  void RemoveProperty(FrameProperties::Descriptor<T> aProperty) {
    mProperties.Remove(aProperty, this);
  }

  void RemoveAllProperties() { mProperties.RemoveAll(this); }

  // nsIFrames themselves are in the nsPresArena, and so are not measured here.
  // Instead, this measures heap-allocated things hanging off the nsIFrame, and
  // likewise for its descendants.
  virtual void AddSizeOfExcludingThisForTree(nsWindowSizes& aWindowSizes) const;

  /**
   * Return true if and only if this frame obeys visibility:hidden.
   * if it does not, then nsContainerFrame will hide its view even though
   * this means children can't be made visible again.
   */
  virtual bool SupportsVisibilityHidden() { return true; }

  /**
   * Returns the clip rect set via the 'clip' property, if the 'clip' property
   * applies to this frame; otherwise returns Nothing(). The 'clip' property
   * applies to HTML frames if they are absolutely positioned. The 'clip'
   * property applies to SVG frames regardless of the value of the 'position'
   * property.
   *
   * The coordinates of the returned rectangle are relative to this frame's
   * origin.
   */
  Maybe<nsRect> GetClipPropClipRect(const nsStyleDisplay* aDisp,
                                    const nsStyleEffects* aEffects,
                                    const nsSize& aSize) const;

  /** Whether this frame is a stacking context for view transitions purposes */
  bool ForcesStackingContextForViewTransition() const;

  /**
   * Check if this frame is focusable and in the current tab order.
   * Tabbable is indicated by a nonnegative tabindex & is a subset of focusable.
   * For example, only the selected radio button in a group is in the
   * tab order, unless the radio group has no selection in which case
   * all of the visible, non-disabled radio buttons in the group are
   * in the tab order. On the other hand, all of the visible, non-disabled
   * radio buttons are always focusable via clicking or script.
   * Also, depending on the pref accessibility.tabfocus some widgets may be
   * focusable but removed from the tab order. This is the default on
   * Mac OS X, where fewer items are focusable.
   * @return whether the frame is focusable via mouse, kbd or script.
   */
  [[nodiscard]] Focusable IsFocusable(
      mozilla::IsFocusableFlags = mozilla::IsFocusableFlags(0));

 protected:
  // Helper for IsFocusable.
  bool IsFocusableDueToScrollFrame();

  /**
   * Returns true if this box clips its children, e.g., if this box is an
   * scrollbox or has overflow: clip in both axes.
   */
  bool DoesClipChildrenInBothAxes() const;

  /**
   * NOTE: aStatus is assumed to be already-initialized. The reflow statuses of
   * any reflowed absolute children will be merged into aStatus; aside from
   * that, this method won't modify aStatus.
   */
  void ReflowAbsoluteFrames(nsPresContext* aPresContext,
                            ReflowOutput& aDesiredSize,
                            const ReflowInput& aReflowInput,
                            nsReflowStatus& aStatus,
                            bool aConstrainBSize = true);

 private:
  nscoord ComputeISizeValueFromAspectRatio(
      mozilla::WritingMode aWM, const mozilla::LogicalSize& aCBSize,
      const mozilla::LogicalSize& aContentEdgeToBoxSizing,
      const mozilla::LengthPercentage& aBSize,
      const mozilla::AspectRatio& aAspectRatio) const;

 public:
  /**
   * @return true if this text frame ends with a newline character.  It
   * should return false if this is not a text frame.
   */
  virtual bool HasSignificantTerminalNewline() const;

  struct CaretPosition {
    CaretPosition();
    ~CaretPosition();

    nsCOMPtr<nsIContent> mResultContent;
    int32_t mContentOffset;
  };

  /**
   * gets the first or last possible caret position within the frame
   *
   * @param  [in] aStart
   *         true  for getting the first possible caret position
   *         false for getting the last possible caret position
   * @return The caret position in a CaretPosition.
   *         the returned value is a 'best effort' in case errors
   *         are encountered rummaging through the frame.
   */
  CaretPosition GetExtremeCaretPosition(bool aStart);

  /**
   * Query whether this frame supports getting a line iterator.
   * @return true if a line iterator is supported.
   */
  virtual bool CanProvideLineIterator() const { return false; }

  /**
   * Get a line iterator for this frame, if supported.
   *
   * @return nullptr if no line iterator is supported.
   * @note dispose the line iterator using nsILineIterator::DisposeLineIterator
   */
  virtual nsILineIterator* GetLineIterator() { return nullptr; }

  /**
   * If this frame is a next-in-flow, and its prev-in-flow has something on its
   * overflow list, pull those frames into the child list of this one.
   */
  virtual void PullOverflowsFromPrevInFlow() {}

  /**
   * Accessors for the absolute containing block.
   */
  bool IsAbsoluteContainer() const {
    return !!(mState & NS_FRAME_HAS_ABSPOS_CHILDREN);
  }
  bool HasAbsolutelyPositionedChildren() const;
  nsAbsoluteContainingBlock* GetAbsoluteContainingBlock() const;
  void MarkAsAbsoluteContainingBlock();
  void MarkAsNotAbsoluteContainingBlock();
  // Child frame types override this function to select their own child list
  // name
  virtual mozilla::FrameChildListID GetAbsoluteListID() const {
    return mozilla::FrameChildListID::Absolute;
  }

  // Checks if we (or any of our descendants) have NS_FRAME_PAINTED_THEBES set,
  // and clears this bit if so.
  bool CheckAndClearPaintedState();

  // Checks if we (or any of our descendents) have mBuiltDisplayList set, and
  // clears this bit if so.
  bool CheckAndClearDisplayListState();

  // CSS visibility just doesn't cut it because it doesn't inherit through
  // documents. Also if this frame is in a hidden card of a deck then it isn't
  // visible either and that isn't expressed using CSS visibility. Also if it
  // is in a hidden view (there are a few cases left and they are hopefully
  // going away soon).
  // If the VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY flag is passed then we
  // ignore the chrome/content boundary, otherwise we stop looking when we
  // reach it.
  enum { VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY = 0x01 };
  bool IsVisibleConsideringAncestors(uint32_t aFlags = 0) const;

  struct FrameWithDistance {
    nsIFrame* mFrame;
    nscoord mXDistance;
    nscoord mYDistance;
  };

  /**
   * Finds a frame that is closer to a specified point than a current
   * distance.  Distance is measured as for text selection -- a closer x
   * distance beats a closer y distance.
   *
   * Normally, this function will only check the distance between this
   * frame's rectangle and the specified point.  SVGTextFrame overrides
   * this so that it can manage all of its descendant frames and take
   * into account any SVG text layout.
   *
   * If aPoint is closer to this frame's rectangle than aCurrentBestFrame
   * indicates, then aCurrentBestFrame is updated with the distance between
   * aPoint and this frame's rectangle, and with a pointer to this frame.
   * If aPoint is not closer, then aCurrentBestFrame is left unchanged.
   *
   * @param aPoint The point to check for its distance to this frame.
   * @param aCurrentBestFrame Pointer to a struct that will be updated with
   *   a pointer to this frame and its distance to aPoint, if this frame
   *   is indeed closer than the current distance in aCurrentBestFrame.
   */
  virtual void FindCloserFrameForSelection(
      const nsPoint& aPoint, FrameWithDistance* aCurrentBestFrame);

  /**
   * Is this a flex item? (i.e. a non-abs-pos child of a flex container)
   */
  inline bool IsFlexItem() const;
  /**
   * Is this a grid item? (i.e. a non-abs-pos child of a grid container)
   */
  inline bool IsGridItem() const;
  /**
   * Is this a flex or grid item? (i.e. a non-abs-pos child of a flex/grid
   * container)
   */
  inline bool IsFlexOrGridItem() const;
  inline bool IsFlexOrGridContainer() const;

  /**
   * Return true if this frame has masonry layout in aAxis.
   * @note only valid to call on nsGridContainerFrames
   */
  inline bool IsMasonry(mozilla::LogicalAxis aAxis) const;

  /**
   * @return true if this frame is used as a table caption.
   */
  inline bool IsTableCaption() const;

  inline bool IsBlockOutside() const;
  inline bool IsInlineOutside() const;
  inline mozilla::StyleDisplay GetDisplay() const;
  inline bool IsFloating() const;
  inline bool IsAbsPosContainingBlock() const;
  inline bool IsFixedPosContainingBlock() const;
  inline bool IsRelativelyOrStickyPositioned() const;

  // Note: In general, you'd want to call IsRelativelyOrStickyPositioned()
  // unless you want to deal with "position:relative" and "position:sticky"
  // differently.
  inline bool IsRelativelyPositioned() const;
  inline bool IsStickyPositioned() const;

  inline bool IsAbsolutelyPositioned(
      const nsStyleDisplay* aStyleDisplay = nullptr) const;
  inline bool IsTrueOverflowContainer() const;

  // Does this frame have "column-span: all" style.
  //
  // Note this only checks computed style, but not testing whether the
  // containing block formatting context was established by a multicol. Callers
  // need to use IsColumnSpanInMulticolSubtree() to check whether multi-column
  // effects apply or not.
  inline bool IsColumnSpan() const;

  // Like IsColumnSpan(), but this also checks whether the frame has a
  // multi-column ancestor or not.
  inline bool IsColumnSpanInMulticolSubtree() const;

  /**
   * Returns the vertical-align value to be used for layout, if it is one
   * of the enumerated values.  If this is an SVG text frame, it returns a value
   * that corresponds to the value of dominant-baseline.  If the
   * vertical-align property has length or percentage value, this returns
   * Nothing().
   */
  Maybe<mozilla::StyleVerticalAlignKeyword> VerticalAlignEnum() const;

  /**
   * Adds the NS_FRAME_IN_POPUP state bit to aFrame, and
   * all descendant frames (including cross-doc ones).
   */
  static void AddInPopupStateBitToDescendants(nsIFrame* aFrame);
  /**
   * Removes the NS_FRAME_IN_POPUP state bit from aFrame and
   * all descendant frames (including cross-doc ones), unless
   * the frame is a popup itself.
   */
  static void RemoveInPopupStateBitFromDescendants(nsIFrame* aFrame);

  /**
   * Return true if aFrame is in an {ib} split and is NOT one of the
   * continuations of the first inline in it.
   */
  bool FrameIsNonFirstInIBSplit() const {
    return HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT) &&
           FirstContinuation()->GetProperty(nsIFrame::IBSplitPrevSibling());
  }

  /**
   * Return true if aFrame is in an {ib} split and is NOT one of the
   * continuations of the last inline in it.
   */
  bool FrameIsNonLastInIBSplit() const {
    return HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT) &&
           FirstContinuation()->GetProperty(nsIFrame::IBSplitSibling());
  }

  /**
   * Return whether this is a frame whose width is used when computing
   * the font size inflation of its descendants.
   */
  bool IsContainerForFontSizeInflation() const {
    return HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER);
  }

  /**
   * Return whether this frame or any of its children is dirty.
   */
  bool IsSubtreeDirty() const {
    return HasAnyStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN);
  }

  /**
   * Returns true if the frame is an SVGTextFrame or one of its descendants.
   */
  bool IsInSVGTextSubtree() const {
    return HasAnyStateBits(NS_FRAME_IS_SVG_TEXT);
  }

  /**
   * Return whether this frame keeps track of overflow areas. (Frames for
   * non-display SVG elements -- e.g. <clipPath> -- do not maintain overflow
   * areas, because they're never painted.)
   */
  bool FrameMaintainsOverflow() const {
    return !HasAllStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY) &&
           !(IsSVGOuterSVGFrame() && HasAnyStateBits(NS_FRAME_IS_NONDISPLAY));
  }

  /*
   * @param aStyleDisplay:  If the caller has this->StyleDisplay(), providing
   *   it here will improve performance.
   */
  bool BackfaceIsHidden(const nsStyleDisplay* aStyleDisplay) const {
    MOZ_ASSERT(aStyleDisplay == StyleDisplay());
    return aStyleDisplay->BackfaceIsHidden();
  }
  bool BackfaceIsHidden() const { return StyleDisplay()->BackfaceIsHidden(); }

  /**
   * Returns true if the frame is scrolled out of view.
   */
  bool IsScrolledOutOfView() const;

  /**
   * Computes a 2D matrix from the -moz-window-transform property on aFrame.
   * Values that don't result in a 2D matrix will be ignored and an identity
   * matrix will be returned instead.
   */
  Matrix ComputeWidgetTransform() const;

  /**
   * @return true iff this frame has one or more associated image requests.
   * @see mozilla::css::ImageLoader.
   */
  bool HasImageRequest() const { return mHasImageRequest; }

  /**
   * Update this frame's image request state.
   */
  void SetHasImageRequest(bool aHasRequest) { mHasImageRequest = aHasRequest; }

  /**
   * Whether this frame has a first-letter child.  If it does, the frame is
   * actually an nsContainerFrame and the first-letter frame can be gotten by
   * walking up to the nearest ancestor blockframe and getting its first
   * continuation's nsContainerFrame::FirstLetterProperty() property.  This will
   * only return true for the first continuation of the first-letter's parent.
   */
  bool HasFirstLetterChild() const { return mHasFirstLetterChild; }

  /**
   * Whether this frame's parent is a wrapper anonymous box.  See documentation
   * for mParentIsWrapperAnonBox.
   */
  bool ParentIsWrapperAnonBox() const { return mParentIsWrapperAnonBox; }
  void SetParentIsWrapperAnonBox() { mParentIsWrapperAnonBox = true; }

  /**
   * Whether this is a wrapper anonymous box needing a restyle.
   */
  bool IsWrapperAnonBoxNeedingRestyle() const {
    return mIsWrapperBoxNeedingRestyle;
  }
  void SetIsWrapperAnonBoxNeedingRestyle(bool aNeedsRestyle) {
    mIsWrapperBoxNeedingRestyle = aNeedsRestyle;
  }

  bool MayHaveTransformAnimation() const { return mMayHaveTransformAnimation; }
  void SetMayHaveTransformAnimation() {
    AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
    mMayHaveTransformAnimation = true;
  }
  bool MayHaveOpacityAnimation() const { return mMayHaveOpacityAnimation; }
  void SetMayHaveOpacityAnimation() { mMayHaveOpacityAnimation = true; }

  // Returns true if this frame is visible or may have visible descendants.
  // Note: This function is accurate only on primary frames, because
  // mAllDescendantsAreInvisible is not updated on continuations.
  bool IsVisibleOrMayHaveVisibleDescendants() const {
    return !mAllDescendantsAreInvisible || StyleVisibility()->IsVisible();
  }
  // Update mAllDescendantsAreInvisible flag for this frame and ancestors.
  void UpdateVisibleDescendantsState();

  /**
   * If this returns true, the frame it's called on should get the
   * NS_FRAME_HAS_DIRTY_CHILDREN bit set on it by the caller; either directly
   * if it's already in reflow, or via calling FrameNeedsReflow() to schedule a
   * reflow.
   */
  virtual bool RenumberFrameAndDescendants(int32_t* aOrdinal, int32_t aDepth,
                                           int32_t aIncrement,
                                           bool aForCounting) {
    return false;
  }

  enum class ExtremumLength {
    MinContent,
    MaxContent,
    MozAvailable,
    Stretch,
    FitContent,
    FitContentFunction,
  };

  template <typename SizeOrMaxSize>
  static Maybe<ExtremumLength> ToExtremumLength(const SizeOrMaxSize& aSize) {
    switch (aSize.tag) {
      case SizeOrMaxSize::Tag::MinContent:
        return mozilla::Some(ExtremumLength::MinContent);
      case SizeOrMaxSize::Tag::MaxContent:
        return mozilla::Some(ExtremumLength::MaxContent);
      case SizeOrMaxSize::Tag::MozAvailable:
        return mozilla::Some(ExtremumLength::MozAvailable);
      case SizeOrMaxSize::Tag::WebkitFillAvailable:
      case SizeOrMaxSize::Tag::Stretch:
        return mozilla::Some(ExtremumLength::Stretch);
      case SizeOrMaxSize::Tag::FitContent:
        return mozilla::Some(ExtremumLength::FitContent);
      case SizeOrMaxSize::Tag::FitContentFunction:
        return mozilla::Some(ExtremumLength::FitContentFunction);
      default:
        return mozilla::Nothing();
    }
  }

  /**
   * Helper function - computes the content-box inline size for aSize, which is
   * a more complex version to resolve a StyleExtremumLength.
   *
   * @param aAvailableISizeOverride If this has a value, it is used as the
   * available inline-size instead of aCBSize.ISize(aWM) when resolving
   * fit-content.
   */
  struct ISizeComputationResult {
    nscoord mISize = 0;
    AspectRatioUsage mAspectRatioUsage = AspectRatioUsage::None;
  };
  ISizeComputationResult ComputeISizeValue(
      gfxContext* aRenderingContext, const mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize,
      const mozilla::LogicalSize& aContentEdgeToBoxSizing,
      nscoord aBoxSizingToMarginEdge, ExtremumLength aSize,
      Maybe<nscoord> aAvailableISizeOverride,
      const mozilla::StyleSize& aStyleBSize,
      const mozilla::AspectRatio& aAspectRatio,
      mozilla::ComputeSizeFlags aFlags);

  /**
   * Helper function - computes the content-box inline size for aSize, which is
   * a simpler version to resolve a LengthPercentage.
   */
  nscoord ComputeISizeValue(const mozilla::WritingMode aWM,
                            const mozilla::LogicalSize& aCBSize,
                            const mozilla::LogicalSize& aContentEdgeToBoxSizing,
                            const mozilla::LengthPercentage& aSize) const;

  /**
   * Compute content-box inline size for aSize.
   *
   * This method doesn't handle 'auto' when aSize is of type StyleSize,
   * nor does it handle 'none' when aSize is of type StyleMaxSize.
   *
   * @param aStyleBSize the style block size of the frame, used to compute
   * intrinsic inline size with aAspectRatio.
   *
   * @param aAspectRatio the preferred aspect-ratio of the frame.
   */
  template <typename SizeOrMaxSize>
  ISizeComputationResult ComputeISizeValue(
      gfxContext* aRenderingContext, const mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize,
      const mozilla::LogicalSize& aContentEdgeToBoxSizing,
      nscoord aBoxSizingToMarginEdge, const SizeOrMaxSize& aSize,
      const mozilla::StyleSize& aStyleBSize,
      const mozilla::AspectRatio& aAspectRatio,
      mozilla::ComputeSizeFlags aFlags = {}) {
    if (aSize.IsLengthPercentage()) {
      return {ComputeISizeValue(aWM, aCBSize, aContentEdgeToBoxSizing,
                                aSize.AsLengthPercentage())};
    }
    auto length = ToExtremumLength(aSize);
    MOZ_ASSERT(length, "This doesn't handle none / auto");
    Maybe<nscoord> availbleISizeOverride;
    if (aSize.IsFitContentFunction()) {
      availbleISizeOverride.emplace(
          aSize.AsFitContentFunction().Resolve(aCBSize.ISize(aWM)));
    }
    return ComputeISizeValue(
        aRenderingContext, aWM, aCBSize, aContentEdgeToBoxSizing,
        aBoxSizingToMarginEdge, length.valueOr(ExtremumLength::MinContent),
        availbleISizeOverride, aStyleBSize, aAspectRatio, aFlags);
  }

  DisplayItemArray& DisplayItems() { return mDisplayItems; }
  const DisplayItemArray& DisplayItems() const { return mDisplayItems; }

  void AddDisplayItem(nsDisplayItem* aItem);
  bool RemoveDisplayItem(nsDisplayItem* aItem);
  void RemoveDisplayItemDataForDeletion();
  bool HasDisplayItems();
  bool HasDisplayItem(nsDisplayItem* aItem);
  bool HasDisplayItem(uint32_t aKey);

  static void PrintDisplayList(nsDisplayListBuilder* aBuilder,
                               const nsDisplayList& aList,
                               bool aDumpHtml = false);
  static void PrintDisplayList(nsDisplayListBuilder* aBuilder,
                               const nsDisplayList& aList,
                               std::stringstream& aStream,
                               bool aDumpHtml = false);
  static void PrintDisplayItem(nsDisplayListBuilder* aBuilder,
                               nsDisplayItem* aItem, std::stringstream& aStream,
                               uint32_t aIndent = 0, bool aDumpSublist = false,
                               bool aDumpHtml = false);
#ifdef MOZ_DUMP_PAINTING
  static void PrintDisplayListSet(nsDisplayListBuilder* aBuilder,
                                  const nsDisplayListSet& aSet,
                                  std::stringstream& aStream,
                                  bool aDumpHtml = false);
#endif

  /**
   * Adds display items for standard CSS background if necessary.
   * Does not check IsVisibleForPainting.
   * @return whether a themed background item was created.
   */
  bool DisplayBackgroundUnconditional(nsDisplayListBuilder* aBuilder,
                                      const nsDisplayListSet& aLists);
  /**
   * Adds display items for standard CSS borders, background and outline for
   * for this frame, as necessary. Checks IsVisibleForPainting and won't
   * display anything if the frame is not visible.
   */
  void DisplayBorderBackgroundOutline(nsDisplayListBuilder* aBuilder,
                                      const nsDisplayListSet& aLists);
  /**
   * Add a display item for the CSS outline. Does not check visibility.
   */
  void DisplayOutlineUnconditional(nsDisplayListBuilder* aBuilder,
                                   const nsDisplayListSet& aLists);
  /**
   * Add a display item for the CSS outline, after calling
   * IsVisibleForPainting to confirm we are visible.
   */
  void DisplayOutline(nsDisplayListBuilder* aBuilder,
                      const nsDisplayListSet& aLists);

  /**
   * Add a display item for CSS inset box shadows. Does not check visibility.
   */
  void DisplayInsetBoxShadowUnconditional(nsDisplayListBuilder* aBuilder,
                                          nsDisplayList* aList);

  /**
   * Add a display item for CSS inset box shadow, after calling
   * IsVisibleForPainting to confirm we are visible.
   */
  void DisplayInsetBoxShadow(nsDisplayListBuilder* aBuilder,
                             nsDisplayList* aList);

  /**
   * Add a display item for CSS outset box shadows. Does not check visibility.
   */
  void DisplayOutsetBoxShadowUnconditional(nsDisplayListBuilder* aBuilder,
                                           nsDisplayList* aList);

  /**
   * Add a display item for CSS outset box shadow, after calling
   * IsVisibleForPainting to confirm we are visible.
   */
  void DisplayOutsetBoxShadow(nsDisplayListBuilder* aBuilder,
                              nsDisplayList* aList);

  bool ForceDescendIntoIfVisible() const { return mForceDescendIntoIfVisible; }
  void SetForceDescendIntoIfVisible(bool aForce) {
    mForceDescendIntoIfVisible = aForce;
  }

  bool BuiltDisplayList() const { return mBuiltDisplayList; }
  void SetBuiltDisplayList(const bool aBuilt) { mBuiltDisplayList = aBuilt; }

  bool IsFrameModified() const { return mFrameIsModified; }
  void SetFrameIsModified(const bool aFrameIsModified) {
    mFrameIsModified = aFrameIsModified;
  }

  bool HasModifiedDescendants() const { return mHasModifiedDescendants; }
  void SetHasModifiedDescendants(const bool aHasModifiedDescendants) {
    mHasModifiedDescendants = aHasModifiedDescendants;
  }

  bool HasOverrideDirtyRegion() const { return mHasOverrideDirtyRegion; }
  void SetHasOverrideDirtyRegion(const bool aHasDirtyRegion) {
    mHasOverrideDirtyRegion = aHasDirtyRegion;
  }

  bool MayHaveWillChangeBudget() const { return mMayHaveWillChangeBudget; }
  void SetMayHaveWillChangeBudget(const bool aHasBudget) {
    mMayHaveWillChangeBudget = aHasBudget;
  }

  bool HasBSizeChange() const { return mHasBSizeChange; }
  void SetHasBSizeChange(const bool aHasBSizeChange) {
    mHasBSizeChange = aHasBSizeChange;
  }

  bool HasPaddingChange() const { return mHasPaddingChange; }
  void SetHasPaddingChange(const bool aHasPaddingChange) {
    mHasPaddingChange = aHasPaddingChange;
  }

  bool HasColumnSpanSiblings() const { return mHasColumnSpanSiblings; }
  void SetHasColumnSpanSiblings(bool aHasColumnSpanSiblings) {
    mHasColumnSpanSiblings = aHasColumnSpanSiblings;
  }

  bool DescendantMayDependOnItsStaticPosition() const {
    return mDescendantMayDependOnItsStaticPosition;
  }
  void SetDescendantMayDependOnItsStaticPosition(bool aValue) {
    mDescendantMayDependOnItsStaticPosition = aValue;
  }

  /**
   * Returns the hit test area of the frame.
   */
  nsRect GetCompositorHitTestArea(nsDisplayListBuilder* aBuilder);

  /**
   * Returns the set of flags indicating the properties of the frame that the
   * compositor might care about for hit-testing purposes. Note that this
   * function must be called during Gecko display list construction time (i.e
   * while the frame tree is being traversed) because that is when the display
   * list builder has the necessary state set up correctly.
   */
  mozilla::gfx::CompositorHitTestInfo GetCompositorHitTestInfo(
      nsDisplayListBuilder* aBuilder);

  /**
   * Copies aWM to mWritingMode on 'this' and all its ancestors.
   */
  inline void PropagateWritingModeToSelfAndAncestors(mozilla::WritingMode aWM);

  /**
   * Observes or unobserves the element with an internal ResizeObserver,
   * depending on whether it needs to update its last remembered size.
   * Also removes a previously stored last remembered size if the element
   * can no longer have it.
   * @see {@link https://drafts.csswg.org/css-sizing-4/#last-remembered}
   */
  void HandleLastRememberedSize();

 protected:
  /**
   * Reparent this frame's view if it has one.
   */
  void ReparentFrameViewTo(nsViewManager* aViewManager, nsView* aNewParentView);

  // Members
  nsRect mRect;
  nsCOMPtr<nsIContent> mContent;
  RefPtr<ComputedStyle> mComputedStyle;

 private:
  nsPresContext* const mPresContext;
  nsContainerFrame* mParent;
  nsIFrame* mNextSibling;  // doubly-linked list of frames
  nsIFrame* mPrevSibling;  // Do not touch outside SetNextSibling!

  DisplayItemArray mDisplayItems;

  void MarkAbsoluteFramesForDisplayList(nsDisplayListBuilder* aBuilder);

 protected:
  void MarkInReflow() {
#ifdef DEBUG_dbaron_off
    // bug 81268
    NS_ASSERTION(!(mState & NS_FRAME_IN_REFLOW), "frame is already in reflow");
#endif
    AddStateBits(NS_FRAME_IN_REFLOW);
  }

 private:
  nsFrameState mState;

 protected:
  /**
   * List of properties attached to the frame.
   */
  FrameProperties mProperties;

  // When there is no scrollable overflow area, and the ink overflow area only
  // slightly larger than mRect, the ink overflow area may be stored a set of
  // four 1-byte deltas from the edges of mRect rather than allocating a whole
  // separate rectangle property. If all four deltas are zero, this means that
  // no overflow area has actually been set (this is the initial state of
  // newly-created frames).
  //
  // Note that these are unsigned values, all measured "outwards" from the edges
  // of mRect, so mLeft and mTop are reversed from our normal coordinate system.
  struct InkOverflowDeltas {
    // The maximum delta value we can store in any of the four edges.
    static constexpr uint8_t kMax = 0xfe;

    uint8_t mLeft;
    uint8_t mTop;
    uint8_t mRight;
    uint8_t mBottom;
    bool operator==(const InkOverflowDeltas& aOther) const {
      return mLeft == aOther.mLeft && mTop == aOther.mTop &&
             mRight == aOther.mRight && mBottom == aOther.mBottom;
    }
    bool operator!=(const InkOverflowDeltas& aOther) const {
      return !(*this == aOther);
    }
  };
  enum class OverflowStorageType : uint32_t {
    // No overflow area; code relies on this being an all-zero value.
    None = 0x00000000u,

    // Ink overflow is too large to stored in InkOverflowDeltas.
    Large = 0x000000ffu,
  };
  // If mOverflow.mType is OverflowStorageType::Large, then the delta values are
  // not meaningful and the overflow area is stored in OverflowAreasProperty()
  // instead.
  union {
    OverflowStorageType mType;
    InkOverflowDeltas mInkOverflowDeltas;
  } mOverflow;

  /** @see GetWritingMode() */
  mozilla::WritingMode mWritingMode;

  /** The ClassID of the concrete class of this instance. */
  const ClassID mClass;  // 1 byte

  bool mMayHaveRoundedCorners : 1;

  /**
   * True iff this frame has one or more associated image requests.
   * @see mozilla::css::ImageLoader.
   */
  bool mHasImageRequest : 1;

  /**
   * True if this frame has a continuation that has a first-letter frame, or its
   * placeholder, as a child.  In that case this frame has a blockframe ancestor
   * that has the first-letter frame hanging off it in the
   * nsContainerFrame::FirstLetterProperty() property.
   */
  bool mHasFirstLetterChild : 1;

  /**
   * True if this frame's parent is a wrapper anonymous box (e.g. a table
   * anonymous box as specified at
   * <https://www.w3.org/TR/CSS21/tables.html#anonymous-boxes>).
   *
   * We could compute this information directly when we need it, but it wouldn't
   * be all that cheap, and since this information is immutable for the lifetime
   * of the frame we might as well cache it.
   *
   * Note that our parent may itself have mParentIsWrapperAnonBox set to true.
   */
  bool mParentIsWrapperAnonBox : 1;

  /**
   * True if this is a wrapper anonymous box needing a restyle.  This is used to
   * track, during stylo post-traversal, whether we've already recomputed the
   * style of this anonymous box, if we end up seeing it twice.
   */
  bool mIsWrapperBoxNeedingRestyle : 1;

  /**
   * This bit is used in nsTextFrame::CharacterDataChanged() as an optimization
   * to skip redundant reflow-requests when the character data changes multiple
   * times between reflows. If this flag is set, then it implies that the
   * NS_FRAME_IS_DIRTY state bit is also set (and that intrinsic sizes have
   * been marked as dirty on our ancestor chain).
   *
   * XXXdholbert This bit is *only* used on nsTextFrame, but it lives here on
   * nsIFrame simply because this is where we've got unused state bits
   * available in a gap. If bits become more scarce, we should perhaps consider
   * expanding the range of frame-specific state bits in nsFrameStateBits.h and
   * moving this to be one of those (e.g. by swapping one of the adjacent
   * general-purpose bits to take the place of this bool:1 here, so we can grow
   * that range of frame-specific bits by 1).
   */
  bool mReflowRequestedForCharDataChange : 1;

  /**
   * This bit is used during BuildDisplayList to mark frames that need to
   * have display items rebuilt. We will descend into them if they are
   * currently visible, even if they don't intersect the dirty area.
   */
  bool mForceDescendIntoIfVisible : 1;

  /**
   * True if we have built display items for this frame since
   * the last call to CheckAndClearDisplayListState, false
   * otherwise. Used for the reftest harness to verify minimal
   * display list building.
   */
  bool mBuiltDisplayList : 1;

  /**
   * True if the frame has been marked modified by
   * |MarkNeedsDisplayItemRebuild()|, usually due to a style change or reflow.
   */
  bool mFrameIsModified : 1;

  /**
   * True if the frame has modified descendants. Set before display list
   * preprocessing and only used during partial display list builds.
   */
  bool mHasModifiedDescendants : 1;

  /**
   * Used by merging based retained display lists to restrict the dirty area
   * during partial display list builds.
   */
  bool mHasOverrideDirtyRegion : 1;

  /**
   * True if frame has will-change, and currently has display
   * items consuming some of the will-change budget.
   */
  bool mMayHaveWillChangeBudget : 1;

#ifdef DEBUG
 public:
  /**
   * True if this frame has already been been visited by
   * nsCSSFrameConstructor::AutoFrameConstructionPageName.
   *
   * This is used to assert that we have visited each frame only once, and is
   * not useful otherwise.
   */
  bool mWasVisitedByAutoFrameConstructionPageName : 1;
#endif

 private:
  /**
   * True if this is the primary frame for mContent.
   */
  bool mIsPrimaryFrame : 1;

  bool mMayHaveTransformAnimation : 1;
  bool mMayHaveOpacityAnimation : 1;

  /**
   * True if we are certain that all descendants are not visible.
   *
   * This flag is conservative in that it might sometimes be false even if, in
   * fact, all descendants are invisible.
   * For example; an element is visibility:visible and has a visibility:hidden
   * child. This flag is stil false in such case.
   */
  bool mAllDescendantsAreInvisible : 1;

  bool mHasBSizeChange : 1;

  /**
   * True if the frame seems to be in the process of being reflowed with a
   * different amount of inline-axis padding as compared to its most recent
   * reflow. This flag's purpose is to detect cases where the frame's
   * inline-axis content-box-size has changed, without any style change or any
   * change to the border-box size, so that we can mark/invalidate things
   * appropriately in ReflowInput::InitResizeFlags().
   *
   * This flag is set in SizeComputationResult::InitOffsets() and cleared in
   * nsIFrame::DidReflow().
   */
  bool mHasPaddingChange : 1;

  /**
   * True if we are or contain the scroll anchor for a scrollable frame.
   */
  bool mInScrollAnchorChain : 1;

  /**
   * Suppose a frame was split into multiple parts to separate parts containing
   * column-spans from parts not containing column-spans. This bit is set on all
   * continuations *not* containing column-spans except for the those after the
   * last column-span/non-column-span boundary (i.e., the bit really means it
   * has a *later* sibling across a split). Note that the last part is always
   * created to containing no columns-spans even if it has no children. See
   * nsCSSFrameConstructor::CreateColumnSpanSiblings() for the implementation.
   *
   * If the frame having this bit set is removed, we need to reframe the
   * multi-column container.
   */
  bool mHasColumnSpanSiblings : 1;

  /**
   * True if we may have any descendant whose positioning may depend on its
   * static position (and thus which we need to recompute the position for if we
   * move).
   */
  bool mDescendantMayDependOnItsStaticPosition : 1;

 protected:
  // Helpers
  /**
   * Can we stop inside this frame when we're skipping non-rendered whitespace?
   *
   * @param aForward [in] Are we moving forward (or backward) in content order.
   *
   * @param aOffset [in/out] At what offset into the frame to start looking.
   * at offset was reached (whether or not we found a place to stop).
   *
   * @return
   *   * STOP: An appropriate offset was found within this frame,
   *     and is given by aOffset.
   *   * CONTINUE: Not found within this frame, need to try the next frame.
   *     See enum FrameSearchResult for more details.
   */
  virtual FrameSearchResult PeekOffsetNoAmount(bool aForward, int32_t* aOffset);

  /**
   * Search the frame for the next character
   *
   * @param aForward [in] Are we moving forward (or backward) in content order.
   *
   * @param aOffset [in/out] At what offset into the frame to start looking.
   * on output - what offset was reached (whether or not we found a place to
   * stop).
   *
   * @param aOptions [in] Options, see the comment in PeekOffsetCharacterOptions
   * for the detail.
   *
   * @return
   *   * STOP: An appropriate offset was found within this frame, and is given
   *     by aOffset.
   *   * CONTINUE: Not found within this frame, need to try the next frame. See
   *     enum FrameSearchResult for more details.
   */
  virtual FrameSearchResult PeekOffsetCharacter(
      bool aForward, int32_t* aOffset,
      PeekOffsetCharacterOptions aOptions = PeekOffsetCharacterOptions());
  static_assert(sizeof(PeekOffsetCharacterOptions) <= sizeof(intptr_t),
                "aOptions should be changed to const reference");

  struct PeekWordState {
    // true when we're still at the start of the search, i.e., we can't return
    // this point as a valid offset!
    bool mAtStart;
    // true when we've encountered at least one character of the type before the
    // boundary we're looking for:
    // 1. If we're moving forward and eating whitepace, looking for a word
    //    beginning (i.e. a boundary between whitespace and non-whitespace),
    //    then mSawBeforeType==true means "we already saw some whitespace".
    // 2. Otherwise, looking for a word beginning (i.e. a boundary between
    //    non-whitespace and whitespace), then mSawBeforeType==true means "we
    //    already saw some non-whitespace".
    bool mSawBeforeType;
    // true when we've encountered at least one non-newline character
    bool mSawInlineCharacter;
    // true when the last character encountered was punctuation
    bool mLastCharWasPunctuation;
    // true when the last character encountered was whitespace
    bool mLastCharWasWhitespace;
    // true when we've seen non-punctuation since the last whitespace
    bool mSeenNonPunctuationSinceWhitespace;
    // text that's *before* the current frame when aForward is true, *after*
    // the current frame when aForward is false. Only includes the text
    // on the current line.
    nsAutoString mContext;

    PeekWordState()
        : mAtStart(true),
          mSawBeforeType(false),
          mSawInlineCharacter(false),
          mLastCharWasPunctuation(false),
          mLastCharWasWhitespace(false),
          mSeenNonPunctuationSinceWhitespace(false) {}
    void SetSawBeforeType() { mSawBeforeType = true; }
    void SetSawInlineCharacter() { mSawInlineCharacter = true; }
    void Update(bool aAfterPunctuation, bool aAfterWhitespace) {
      mLastCharWasPunctuation = aAfterPunctuation;
      mLastCharWasWhitespace = aAfterWhitespace;
      if (aAfterWhitespace) {
        mSeenNonPunctuationSinceWhitespace = false;
      } else if (!aAfterPunctuation) {
        mSeenNonPunctuationSinceWhitespace = true;
      }
      mAtStart = false;
    }
  };

  /**
   * Search the frame for the next word boundary
   * @param  aForward [in] Are we moving forward (or backward) in content order.
   * @param  aWordSelectEatSpace [in] true: look for non-whitespace following
   *         whitespace (in the direction of movement).
   *         false: look for whitespace following non-whitespace (in the
   *         direction  of movement).
   * @param  aIsKeyboardSelect [in] Was the action initiated by a keyboard
   * operation? If true, punctuation immediately following a word is considered
   * part of that word. Otherwise, a sequence of punctuation is always
   * considered as a word on its own.
   * @param  aOffset [in/out] At what offset into the frame to start looking.
   *         on output - what offset was reached (whether or not we found a
   * place to stop).
   * @param  aState [in/out] the state that is carried from frame to frame
   */
  virtual FrameSearchResult PeekOffsetWord(
      bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
      int32_t* aOffset, PeekWordState* aState, bool aTrimSpaces);

 protected:
  /**
   * Check whether we should break at a boundary between punctuation and
   * non-punctuation. Only call it at a punctuation boundary
   * (i.e. exactly one of the previous and next characters are punctuation).
   * @param aForward true if we're moving forward in content order
   * @param aPunctAfter true if the next character is punctuation
   * @param aWhitespaceAfter true if the next character is whitespace
   */
  static bool BreakWordBetweenPunctuation(const PeekWordState* aState,
                                          bool aForward, bool aPunctAfter,
                                          bool aWhitespaceAfter,
                                          bool aIsKeyboardSelect);

 private:
  nsRect GetOverflowRect(mozilla::OverflowType aType) const;

  // Get a pointer to the overflow areas property attached to the frame.
  mozilla::OverflowAreas* GetOverflowAreasProperty() const {
    MOZ_ASSERT(mOverflow.mType == OverflowStorageType::Large);
    mozilla::OverflowAreas* overflow = GetProperty(OverflowAreasProperty());
    MOZ_ASSERT(overflow);
    return overflow;
  }

  nsRect InkOverflowFromDeltas() const {
    MOZ_ASSERT(mOverflow.mType != OverflowStorageType::Large,
               "should not be called when overflow is in a property");
    // Calculate the rect using deltas from the frame's border rect.
    // Note that the mOverflow.mInkOverflowDeltas fields are unsigned, but we
    // will often need to return negative values for the left and top, so take
    // care to cast away the unsigned-ness.
    return nsRect(-(int32_t)mOverflow.mInkOverflowDeltas.mLeft,
                  -(int32_t)mOverflow.mInkOverflowDeltas.mTop,
                  mRect.Width() + mOverflow.mInkOverflowDeltas.mRight +
                      mOverflow.mInkOverflowDeltas.mLeft,
                  mRect.Height() + mOverflow.mInkOverflowDeltas.mBottom +
                      mOverflow.mInkOverflowDeltas.mTop);
  }

  /**
   * Set the OverflowArea rect, storing it as deltas or a separate rect
   * depending on its size in relation to the primary frame rect.
   *
   * @return true if any overflow changed.
   */
  bool SetOverflowAreas(const mozilla::OverflowAreas& aOverflowAreas);

  bool HasOpacityInternal(float aThreshold, const nsStyleDisplay* aStyleDisplay,
                          const nsStyleEffects* aStyleEffects,
                          mozilla::EffectSet* aEffectSet = nullptr) const;

  static constexpr size_t kFrameClassCount =
#define FRAME_ID(...) 1 +
#define ABSTRACT_FRAME_ID(...)
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
      0;

  // Maps mClass to LayoutFrameType.
  static const mozilla::LayoutFrameType sLayoutFrameTypes[kFrameClassCount];
  // Maps mClass to LayoutFrameTypeFlags.
  static const ClassFlags sLayoutFrameClassFlags[kFrameClassCount];

#ifdef DEBUG_FRAME_DUMP
 public:
  static void IndentBy(FILE* out, int32_t aIndent) {
    while (--aIndent >= 0) {
      fputs("  ", out);
    }
  }
  void ListTag(FILE* out) const { fputs(ListTag().get(), out); }
  nsAutoCString ListTag(bool aListOnlyDeterministic = false) const;

  enum class ListFlag {
    TraverseSubdocumentFrames,
    DisplayInCSSPixels,
    OnlyListDeterministicInfo
  };
  using ListFlags = mozilla::EnumSet<ListFlag>;

  template <typename T>
  static std::string ConvertToString(const T& aValue, ListFlags aFlags) {
    // This method can convert all physical types in app units to CSS pixels.
    return aFlags.contains(ListFlag::DisplayInCSSPixels)
               ? mozilla::ToString(mozilla::CSSPixel::FromAppUnits(aValue))
               : mozilla::ToString(aValue);
  }
  static std::string ConvertToString(const mozilla::LogicalRect& aRect,
                                     const mozilla::WritingMode aWM,
                                     ListFlags aFlags);
  static std::string ConvertToString(const mozilla::LogicalSize& aSize,
                                     const mozilla::WritingMode aWM,
                                     ListFlags aFlags);

  template <typename T>
  static void ListPtr(nsACString& aTo, const ListFlags& aFlags, const T* aPtr,
                      const char* aPrefix = "=") {
    ListPtr(aTo, aFlags.contains(ListFlag::OnlyListDeterministicInfo), aPtr,
            aPrefix);
  }

  template <typename T>
  static void ListPtr(nsACString& aTo, bool aSkip, const T* aPtr,
                      const char* aPrefix = "=") {
    if (aSkip) {
      return;
    }
    aTo += nsPrintfCString("%s%p", aPrefix, static_cast<const void*>(aPtr));
  }

  void ListGeneric(nsACString& aTo, const char* aPrefix = "",
                   ListFlags aFlags = ListFlags()) const;
  virtual void List(FILE* out = stderr, const char* aPrefix = "",
                    ListFlags aFlags = ListFlags()) const;

  void ListTextRuns(FILE* out = stderr) const;
  virtual void ListTextRuns(FILE* out, nsTHashSet<const void*>& aSeen) const;

  virtual void ListWithMatchedRules(FILE* out = stderr,
                                    const char* aPrefix = "") const;
  void ListMatchedRules(FILE* out, const char* aPrefix) const;

  /**
   * Dump the frame tree beginning from the root frame.
   */
  void DumpFrameTree() const;
  void DumpFrameTree(bool aListOnlyDeterministic) const;
  void DumpFrameTreeInCSSPixels() const;
  void DumpFrameTreeInCSSPixels(bool aListOnlyDeterministic) const;

  /**
   * Dump the frame tree beginning from ourselves.
   */
  void DumpFrameTreeLimited() const;
  void DumpFrameTreeLimitedInCSSPixels() const;

  /**
   * Get a printable from of the name of the frame type.
   * XXX This should be eliminated and we use GetType() instead...
   */
  virtual nsresult GetFrameName(nsAString& aResult) const;
  nsresult MakeFrameName(const nsAString& aType, nsAString& aResult) const;
  // Helper function to return the index in parent of the frame's content
  // object. Returns Nothing on error or if the frame doesn't have a content
  // object
  static mozilla::Maybe<uint32_t> ContentIndexInContainer(
      const nsIFrame* aFrame);
#endif

#ifdef DEBUG
  /**
   * Tracing method that writes a method enter/exit routine to the
   * nspr log using the nsIFrame log module. The tracing is only
   * done when the NS_FRAME_TRACE_CALLS bit is set in the log module's
   * level field.
   */
  void Trace(const char* aMethod, bool aEnter);
  void Trace(const char* aMethod, bool aEnter, const nsReflowStatus& aStatus);
  void TraceMsg(const char* aFormatString, ...) MOZ_FORMAT_PRINTF(2, 3);

  // Helper function that verifies that each frame in the list has the
  // NS_FRAME_IS_DIRTY bit set
  static void VerifyDirtyBitSet(const nsFrameList& aFrameList);

  static mozilla::LazyLogModule sFrameLogModule;
#endif
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsIFrame::ReflowChildFlags)

//----------------------------------------------------------------------

/**
 * AutoWeakFrame can be used to keep a reference to a nsIFrame in a safe way.
 * Whenever an nsIFrame object is deleted, the AutoWeakFrames pointing
 * to it will be cleared.  AutoWeakFrame is for variables on the stack or
 * in static storage only, there is also a WeakFrame below for heap uses.
 *
 * Create AutoWeakFrame object when it is sure that nsIFrame object
 * is alive and after some operations which may destroy the nsIFrame
 * (for example any DOM modifications) use IsAlive() or GetFrame() methods to
 * check whether it is safe to continue to use the nsIFrame object.
 *
 * @note The usage of this class should be kept to a minimum.
 */
class WeakFrame;
class MOZ_NONHEAP_CLASS AutoWeakFrame {
 public:
  explicit constexpr AutoWeakFrame() : mPrev(nullptr), mFrame(nullptr) {}

  AutoWeakFrame(const AutoWeakFrame& aOther) : mPrev(nullptr), mFrame(nullptr) {
    Init(aOther.GetFrame());
  }

  MOZ_IMPLICIT AutoWeakFrame(const WeakFrame& aOther);

  MOZ_IMPLICIT AutoWeakFrame(nsIFrame* aFrame)
      : mPrev(nullptr), mFrame(nullptr) {
    Init(aFrame);
  }

  AutoWeakFrame& operator=(AutoWeakFrame& aOther) {
    Init(aOther.GetFrame());
    return *this;
  }

  AutoWeakFrame& operator=(nsIFrame* aFrame) {
    Init(aFrame);
    return *this;
  }

  nsIFrame* operator->() { return mFrame; }

  operator nsIFrame*() { return mFrame; }

  void Clear(mozilla::PresShell* aPresShell);

  bool IsAlive() const { return !!mFrame; }

  nsIFrame* GetFrame() const { return mFrame; }

  AutoWeakFrame* GetPreviousWeakFrame() { return mPrev; }

  void SetPreviousWeakFrame(AutoWeakFrame* aPrev) { mPrev = aPrev; }

  ~AutoWeakFrame();

 private:
  // Not available for the heap!
  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;
  void operator delete(void*) = delete;
  void operator delete[](void*) = delete;

  void Init(nsIFrame* aFrame);

  AutoWeakFrame* mPrev;
  nsIFrame* mFrame;
};

// Use nsIFrame's fast-path to avoid QueryFrame:
inline do_QueryFrameHelper<nsIFrame> do_QueryFrame(AutoWeakFrame& s) {
  return do_QueryFrameHelper<nsIFrame>(s.GetFrame());
}

/**
 * @see AutoWeakFrame
 */
class MOZ_HEAP_CLASS WeakFrame {
 public:
  WeakFrame() : mFrame(nullptr) {}

  WeakFrame(const WeakFrame& aOther) : mFrame(nullptr) {
    Init(aOther.GetFrame());
  }

  MOZ_IMPLICIT WeakFrame(const AutoWeakFrame& aOther) : mFrame(nullptr) {
    Init(aOther.GetFrame());
  }

  MOZ_IMPLICIT WeakFrame(nsIFrame* aFrame) : mFrame(nullptr) { Init(aFrame); }

  ~WeakFrame() {
    Clear(mFrame ? mFrame->PresContext()->GetPresShell() : nullptr);
  }

  WeakFrame& operator=(WeakFrame& aOther) {
    Init(aOther.GetFrame());
    return *this;
  }

  WeakFrame& operator=(nsIFrame* aFrame) {
    Init(aFrame);
    return *this;
  }

  nsIFrame* operator->() { return mFrame; }
  operator nsIFrame*() { return mFrame; }

  bool operator==(nsIFrame* const aOther) const { return mFrame == aOther; }

  void Clear(mozilla::PresShell* aPresShell);

  bool IsAlive() const { return !!mFrame; }
  nsIFrame* GetFrame() const { return mFrame; }

 private:
  void Init(nsIFrame* aFrame);

  nsIFrame* mFrame;
};

// Use nsIFrame's fast-path to avoid QueryFrame:
inline do_QueryFrameHelper<nsIFrame> do_QueryFrame(WeakFrame& s) {
  return do_QueryFrameHelper<nsIFrame>(s.GetFrame());
}

inline bool nsFrameList::ContinueRemoveFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame->GetPrevSibling() || !aFrame->GetNextSibling(),
             "Forgot to call StartRemoveFrame?");
  if (aFrame == mLastChild) {
    MOZ_ASSERT(!aFrame->GetNextSibling(), "broken frame list");
    nsIFrame* prevSibling = aFrame->GetPrevSibling();
    if (!prevSibling) {
      MOZ_ASSERT(aFrame == mFirstChild, "broken frame list");
      mFirstChild = mLastChild = nullptr;
      return true;
    }
    MOZ_ASSERT(prevSibling->GetNextSibling() == aFrame, "Broken frame linkage");
    prevSibling->SetNextSibling(nullptr);
    mLastChild = prevSibling;
    return true;
  }
  if (aFrame == mFirstChild) {
    MOZ_ASSERT(!aFrame->GetPrevSibling(), "broken frame list");
    mFirstChild = aFrame->GetNextSibling();
    aFrame->SetNextSibling(nullptr);
    MOZ_ASSERT(mFirstChild, "broken frame list");
    return true;
  }
  return false;
}

inline bool nsFrameList::StartRemoveFrame(nsIFrame* aFrame) {
  if (aFrame->GetPrevSibling() && aFrame->GetNextSibling()) {
    UnhookFrameFromSiblings(aFrame);
    return true;
  }
  return ContinueRemoveFrame(aFrame);
}

// Operators of nsFrameList::Iterator
// ---------------------------------------------------

inline nsIFrame* nsFrameList::ForwardFrameTraversal::Next(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->GetNextSibling();
}
inline nsIFrame* nsFrameList::ForwardFrameTraversal::Prev(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->GetPrevSibling();
}

inline nsIFrame* nsFrameList::BackwardFrameTraversal::Next(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->GetPrevSibling();
}
inline nsIFrame* nsFrameList::BackwardFrameTraversal::Prev(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->GetNextSibling();
}

#endif /* nsIFrame_h___ */
