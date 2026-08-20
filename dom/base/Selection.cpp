/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of mozilla::dom::Selection
 */

#include "Selection.h"

#include <algorithm>

#include "ErrorList.h"
#include "LayoutConstants.h"
#include "mozilla/AccessibleCaretEventHub.h"
#include "mozilla/Assertions.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoCopyListener.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/SelectionMovementUtils.h"
#include "mozilla/StackWalk.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/ToString.h"
#include "mozilla/Try.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/SelectionBinding.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/TreeIterator.h"
#include "mozilla/intl/Bidi.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsBidiPresUtils.h"
#include "nsCCUncollectableMarker.h"
#include "nsCOMPtr.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsCopySupport.h"
#include "nsDebug.h"
#include "nsDeviceContext.h"
#include "nsDirection.h"
#include "nsError.h"
#include "nsFmtString.h"
#include "nsFocusManager.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIDocumentEncoder.h"
#include "nsIFrameInlines.h"
#include "nsINamed.h"
#include "nsISelectionController.h"  //for the enums
#include "nsISelectionListener.h"
#include "nsITableCellLayout.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"
#include "nsRange.h"
#include "nsRefreshDriver.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableWrapperFrame.h"
#include "nsTextFrame.h"
#include "nsThreadUtils.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

namespace mozilla {
// "Selection" logs only the calls of AddRangesForSelectableNodes and
// NotifySelectionListeners in debug level.
static LazyLogModule sSelectionLog("Selection");
// "SelectionAPI" logs all API calls (both internal ones and exposed to script
// ones) of normal selection which may change selection ranges.
// 3. Info: Calls of APIs
// 4. Debug: Call stacks with 7 ancestor callers of APIs
// 5. Verbose: Complete call stacks of APIs.
LazyLogModule sSelectionAPILog("SelectionAPI");

std::string format_as(SelectionType aType) {
  constexpr const char* sNames[] = {
      "eInvalid",
      "eNone",
      "eNormal",
      "eSpellCheck",
      "eIMERawClause",
      "eIMESelectedRawClause",
      "eIMEConvertedClause",
      "eIMESelectedClause",
      "eAccessibility",
      "eFind",
      "eURLSecondary",
      "eURLStrikeout",
      "eTargetText",
      "eHighlight",
  };
  static_assert(static_cast<std::underlying_type_t<SelectionType>>(
                    SelectionType::eInvalid) == -1);
  static_assert(static_cast<std::underlying_type_t<SelectionType>>(
                    SelectionType::eHighlight) -
                    static_cast<std::underlying_type_t<SelectionType>>(
                        SelectionType::eInvalid) ==
                std::size(sNames) - 1);
  const size_t index =
      static_cast<std::underlying_type_t<SelectionType>>(aType) + 1;
  return index >= std::size(sNames) ? "<invalid value>" : sNames[index];
}

std::ostream& operator<<(std::ostream& aStream, SelectionType aType) {
  return aStream << format_as(aType);
}

MOZ_ALWAYS_INLINE bool NeedsToLogSelectionAPI(dom::Selection& aSelection) {
  return aSelection.Type() == SelectionType::eNormal &&
         MOZ_LOG_TEST(sSelectionAPILog, LogLevel::Info);
}

void LogStackForSelectionAPI() {
  if (!MOZ_LOG_TEST(sSelectionAPILog, LogLevel::Debug)) {
    return;
  }
  static nsAutoCString* sBufPtr = nullptr;
  MOZ_ASSERT(!sBufPtr);
  nsAutoCString buf;
  sBufPtr = &buf;
  auto writer = [](const char* aBuf) { sBufPtr->Append(aBuf); };
  const LogLevel logLevel = MOZ_LOG_TEST(sSelectionAPILog, LogLevel::Verbose)
                                ? LogLevel::Verbose
                                : LogLevel::Debug;
  MozWalkTheStackWithWriter(writer, CallerPC(),
                            logLevel == LogLevel::Verbose
                                ? 0u /* all */
                                : 8u /* 8 inclusive ancestors */);
  MOZ_LOG_FMT(sSelectionAPILog, logLevel, "\n{}", buf.get());
  sBufPtr = nullptr;
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName) {
  MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info, "{} Selection::{}()",
              static_cast<const void*>(aSelection), aFuncName);
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName,
                            const nsINode* aNode) {
  MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info, "{} Selection::{}({}={})",
              static_cast<const void*>(aSelection), aFuncName, aArgName,
              RefPtr{aNode});
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName,
                            const dom::AbstractRange& aRange) {
  MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info, "{} Selection::{}({}={})",
              static_cast<const void*>(aSelection), aFuncName, aArgName,
              ToString(aRange));
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName1,
                            const nsINode* aNode, const char* aArgName2,
                            uint32_t aOffset) {
  MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
              "{} Selection::{}({}={}, {}={})",
              static_cast<const void*>(aSelection), aFuncName, aArgName1,
              RefPtr{aNode}, aArgName2, aOffset);
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName,
                            const RawRangeBoundary& aBoundary) {
  MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info, "{} Selection::{}({}={})",
              static_cast<const void*>(aSelection), aFuncName, aArgName,
              aBoundary);
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName1,
                            const nsAString& aStr1, const char* aArgName2,
                            const nsAString& aStr2, const char* aArgName3,
                            const nsAString& aStr3) {
  MOZ_LOG_FMT(
      sSelectionAPILog, LogLevel::Info, "{} Selection::{}({}={}, {}={}, {}={})",
      static_cast<const void*>(aSelection), aFuncName, aArgName1,
      NS_ConvertUTF16toUTF8(aStr1), aArgName2, NS_ConvertUTF16toUTF8(aStr2),
      aArgName3, NS_ConvertUTF16toUTF8(aStr3));
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aNodeArgName1,
                            const nsINode& aNode1, const char* aOffsetArgName1,
                            uint32_t aOffset1, const char* aNodeArgName2,
                            const nsINode& aNode2, const char* aOffsetArgName2,
                            uint32_t aOffset2) {
  if (&aNode1 == &aNode2 && aOffset1 == aOffset2) {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
                "{} Selection::{}({}={}={}, {}={}={})",
                static_cast<const void*>(aSelection), aFuncName, aNodeArgName1,
                aNodeArgName2, aNode1, aOffsetArgName1, aOffsetArgName2,
                aOffset1);
  } else {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
                "{} Selection::{}({}={}, {}={}, {}={}, {}={})",
                static_cast<const void*>(aSelection), aFuncName, aNodeArgName1,
                aNode1, aOffsetArgName1, aOffset1, aNodeArgName2, aNode2,
                aOffsetArgName2, aOffset2);
  }
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aNodeArgName1,
                            const nsINode& aNode1, const char* aOffsetArgName1,
                            uint32_t aOffset1, const char* aNodeArgName2,
                            const nsINode& aNode2, const char* aOffsetArgName2,
                            uint32_t aOffset2, const char* aDirArgName,
                            nsDirection aDirection, const char* aReasonArgName,
                            int16_t aReason) {
  if (&aNode1 == &aNode2 && aOffset1 == aOffset2) {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
                "{} Selection::{}({}={}={}, {}={}={}, {}={}, {}={})",
                static_cast<const void*>(aSelection), aFuncName, aNodeArgName1,
                aNodeArgName2, aNode1, aOffsetArgName1, aOffsetArgName2,
                aOffset1, aDirArgName, aDirection, aReasonArgName, aReason);
  } else {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
                "{} Selection::{}({}={}, {}={}, {}={}, {}={}, {}={}, {}={})",
                static_cast<const void*>(aSelection), aFuncName, aNodeArgName1,
                aNode1, aOffsetArgName1, aOffset1, aNodeArgName2, aNode2,
                aOffsetArgName2, aOffset2, aDirArgName, aDirection,
                aReasonArgName, aReason);
  }
}

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName1,
                            const RawRangeBoundary& aBoundary1,
                            const char* aArgName2,
                            const RawRangeBoundary& aBoundary2) {
  if (aBoundary1 == aBoundary2) {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info, "{} Selection::{}({}={}={})",
                static_cast<const void*>(aSelection), aFuncName, aArgName1,
                aArgName2, aBoundary1);
  } else {
    MOZ_LOG_FMT(sSelectionAPILog, LogLevel::Info,
                "{} Selection::{}({}={}, {}={})",
                static_cast<const void*>(aSelection), aFuncName, aArgName1,
                aBoundary1, aArgName2, aBoundary2);
  }
}
}  // namespace mozilla

using namespace mozilla;
using namespace mozilla::dom;

// #define DEBUG_TABLE 1

#ifdef PRINT_RANGE
static void printRange(nsRange* aDomRange);
#  define DEBUG_OUT_RANGE(x) printRange(x)
#else
#  define DEBUG_OUT_RANGE(x)
#endif  // PRINT_RANGE

uint64_t SelectionChangeGuard::sGeneration = 0;

static constexpr nsLiteralCString kNoDocumentTypeNodeError =
    "DocumentType nodes are not supported"_ns;
static constexpr nsLiteralCString kNoRangeExistsError =
    "No selection range exists"_ns;
static constexpr nsLiteralCString kIndexSizeError =
    "The offset is out of range."_ns;

namespace mozilla {

/******************************************************************************
 * Utility methods defined in nsISelectionListener.idl
 ******************************************************************************/

nsCString SelectionChangeReasonsToCString(int16_t aReasons) {
  nsCString reasons;
  if (!aReasons) {
    reasons.AssignLiteral("NO_REASON");
    return reasons;
  }
  auto EnsureSeparator = [](nsCString& aString) -> void {
    if (!aString.IsEmpty()) {
      aString.AppendLiteral(" | ");
    }
  };
  struct ReasonData {
    int16_t mReason;
    const char* mReasonStr;

    ReasonData(int16_t aReason, const char* aReasonStr)
        : mReason(aReason), mReasonStr(aReasonStr) {}
  };
  for (const ReasonData& reason :
       {ReasonData(nsISelectionListener::DRAG_REASON, "DRAG_REASON"),
        ReasonData(nsISelectionListener::MOUSEDOWN_REASON, "MOUSEDOWN_REASON"),
        ReasonData(nsISelectionListener::MOUSEUP_REASON, "MOUSEUP_REASON"),
        ReasonData(nsISelectionListener::KEYPRESS_REASON, "KEYPRESS_REASON"),
        ReasonData(nsISelectionListener::SELECTALL_REASON, "SELECTALL_REASON"),
        ReasonData(nsISelectionListener::COLLAPSETOSTART_REASON,
                   "COLLAPSETOSTART_REASON"),
        ReasonData(nsISelectionListener::COLLAPSETOEND_REASON,
                   "COLLAPSETOEND_REASON"),
        ReasonData(nsISelectionListener::IME_REASON, "IME_REASON"),
        ReasonData(nsISelectionListener::JS_REASON, "JS_REASON")}) {
    if (aReasons & reason.mReason) {
      EnsureSeparator(reasons);
      reasons.Append(reason.mReasonStr);
    }
  }
  return reasons;
}

}  // namespace mozilla

SelectionNodeCache::SelectionNodeCache(PresShell& aOwningPresShell)
    : mOwningPresShell(aOwningPresShell) {
  MOZ_ASSERT(!mOwningPresShell.mSelectionNodeCache);
  mOwningPresShell.mSelectionNodeCache = this;
}

SelectionNodeCache::~SelectionNodeCache() {
  mOwningPresShell.mSelectionNodeCache = nullptr;
}

bool SelectionNodeCache::MaybeCollectNodesAndCheckIfFullySelectedInAnyOf(
    const nsINode* aNode, const nsTArray<Selection*>& aSelections) {
  for (const auto* sel : aSelections) {
    if (MaybeCollectNodesAndCheckIfFullySelected(aNode, sel)) {
      return true;
    }
  }
  return false;
}

const nsTHashSet<const nsINode*>& SelectionNodeCache::MaybeCollect(
    const Selection* aSelection) {
  MOZ_ASSERT(aSelection);
  return mSelectedNodes.LookupOrInsertWith(aSelection, [sel = RefPtr(
                                                            aSelection)] {
    nsTHashSet<const nsINode*> fullySelectedNodes;
    for (size_t rangeIndex = 0; rangeIndex < sel->RangeCount(); ++rangeIndex) {
      AbstractRange* range = sel->GetAbstractRangeAt(rangeIndex);
      MOZ_ASSERT(range);
      if (range->AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()) {
        continue;
      }
      if (range->IsStaticRange() && !range->AsStaticRange()->IsValid()) {
        continue;
      }
      const RangeBoundary& startRef = range->MayCrossShadowBoundaryStartRef();
      const RangeBoundary& endRef = range->MayCrossShadowBoundaryEndRef();

      const nsINode* startContainer =
          startRef.IsStartOfContainer() ? nullptr : startRef.GetContainer();
      const nsINode* endContainer =
          endRef.IsEndOfContainer() ? nullptr : endRef.GetContainer();

      auto AddNodeIfFullySelected = [&](const nsINode* aNode) {
        if (!aNode) {
          return;
        }
        // Only collect start and end container if they are fully
        // selected (they are null in that case).
        if (aNode == startContainer || aNode == endContainer) {
          return;
        }
        fullySelectedNodes.Insert(aNode);
      };

      ContentSubtreeIterator subtreeIter;
      nsresult rv = subtreeIter.InitWithAllowCrossShadowBoundary(range);
      if (NS_FAILED(rv)) {
        continue;
      }

      for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
        MOZ_DIAGNOSTIC_ASSERT(subtreeIter.GetCurrentNode());
        if (subtreeIter.GetCurrentNode()->IsContent()) {
          TreeIterator<FlattenedChildIteratorForSelection> iter(
              *(subtreeIter.GetCurrentNode()->AsContent()));
          for (; iter.GetCurrent(); iter.GetNext()) {
            AddNodeIfFullySelected(iter.GetCurrent());
          }
        }
      }
    }
    return fullySelectedNodes;
  });
}

// #define DEBUG_SELECTION // uncomment for printf describing every collapse and
//  extend. #define DEBUG_NAVIGATION

// #define DEBUG_TABLE_SELECTION 1

struct CachedOffsetForFrame {
  CachedOffsetForFrame()
      : mCachedFrameOffset(0, 0)  // nsPoint ctor
        ,
        mLastCaretFrame(nullptr),
        mLastContentOffset(0),
        mCanCacheFrameOffset(false) {}

  nsPoint mCachedFrameOffset;  // cached frame offset
  nsIFrame* mLastCaretFrame;   // store the frame the caret was last drawn in.
  int32_t mLastContentOffset;  // store last content offset
  bool mCanCacheFrameOffset;   // cached frame offset is valid?
};

class AutoScroller final : public nsITimerCallback, public nsINamed {
 public:
  NS_DECL_ISUPPORTS

  explicit AutoScroller(nsFrameSelection* aFrameSelection)
      : mFrameSelection(aFrameSelection),
        mPresContext(nullptr),
        mPoint(0, 0),
        mDelayInMs(30),
        mFurtherScrollingAllowed(FurtherScrollingAllowed::kYes) {
    MOZ_ASSERT(mFrameSelection);
  }

  MOZ_CAN_RUN_SCRIPT nsresult DoAutoScroll(nsIFrame* aFrame, nsPoint aPoint);

 private:
  // aPoint is relative to aPresContext's root frame
  nsresult ScheduleNextDoAutoScroll(nsPresContext* aPresContext,
                                    nsPoint& aPoint) {
    if (NS_WARN_IF(mFurtherScrollingAllowed == FurtherScrollingAllowed::kNo)) {
      return NS_ERROR_FAILURE;
    }

    mPoint = aPoint;

    // Store the presentation context. The timer will be
    // stopped by the selection if the prescontext is destroyed.
    mPresContext = aPresContext;

    mContent = PresShell::GetCapturingContent();

    if (!mTimer) {
      mTimer = NS_NewTimer(GetMainThreadSerialEventTarget());
      if (!mTimer) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    return mTimer->InitWithCallback(this, mDelayInMs, nsITimer::TYPE_ONE_SHOT);
  }

 public:
  enum class FurtherScrollingAllowed { kYes, kNo };

  void Stop(const FurtherScrollingAllowed aFurtherScrollingAllowed) {
    MOZ_ASSERT((aFurtherScrollingAllowed == FurtherScrollingAllowed::kNo) ||
               (mFurtherScrollingAllowed == FurtherScrollingAllowed::kYes));

    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }

    mContent = nullptr;
    mFurtherScrollingAllowed = aFurtherScrollingAllowed;
  }

  void SetDelay(uint32_t aDelayInMs) { mDelayInMs = aDelayInMs; }

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Notify(nsITimer* timer) override {
    if (mPresContext) {
      AutoWeakFrame frame =
          mContent ? mPresContext->GetPrimaryFrameFor(mContent) : nullptr;
      if (!frame) {
        return NS_OK;
      }
      mContent = nullptr;

      nsPoint pt = mPoint - frame->GetOffsetTo(
                                mPresContext->PresShell()->GetRootFrame());
      RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
      frameSelection->HandleDrag(frame, pt);
      if (!frame.IsAlive()) {
        return NS_OK;
      }

      NS_ASSERTION(frame->PresContext() == mPresContext, "document mismatch?");
      DoAutoScroll(frame, pt);
    }
    return NS_OK;
  }

  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("AutoScroller");
    return NS_OK;
  }

 protected:
  virtual ~AutoScroller() {
    if (mTimer) {
      mTimer->Cancel();
    }
  }

 private:
  nsFrameSelection* const mFrameSelection;
  nsPresContext* mPresContext;
  // relative to mPresContext's root frame
  nsPoint mPoint;
  nsCOMPtr<nsITimer> mTimer;
  nsCOMPtr<nsIContent> mContent;
  uint32_t mDelayInMs;
  FurtherScrollingAllowed mFurtherScrollingAllowed;
};

NS_IMPL_ISUPPORTS(AutoScroller, nsITimerCallback, nsINamed)

#ifdef PRINT_RANGE
void printRange(nsRange* aDomRange) {
  if (!aDomRange) {
    printf("NULL Range\n");
  }
  nsINode* startNode = aDomRange->GetStartContainer();
  nsINode* endNode = aDomRange->GetEndContainer();
  int32_t startOffset = aDomRange->StartOffset();
  int32_t endOffset = aDomRange->EndOffset();

  printf("range: 0x%lx\t start: 0x%lx %ld, \t end: 0x%lx,%ld\n",
         (unsigned long)aDomRange, (unsigned long)startNode, (long)startOffset,
         (unsigned long)endNode, (long)endOffset);
}
#endif /* PRINT_RANGE */

void Selection::Stringify(nsAString& aResult, CallerType aCallerType,
                          FlushFrames aFlushFrames) {
  if (aFlushFrames == FlushFrames::Yes) {
    // We need FlushType::Frames here to make sure frames have been created for
    // the selected content.  Use mFrameSelection->GetPresShell() which returns
    // null if the Selection has been disconnected (the shell is Destroyed).
    RefPtr<PresShell> presShell =
        mFrameSelection ? mFrameSelection->GetPresShell() : nullptr;
    if (!presShell) {
      aResult.Truncate();
      return;
    }
    presShell->FlushPendingNotifications(FlushType::Frames);
  }

  IgnoredErrorResult rv;
  uint32_t flags = nsIDocumentEncoder::SkipInvisibleContent;
  if (StaticPrefs::dom_selection_mimic_chrome_tostring_enabled() &&
      Type() == SelectionType::eNormal &&
      aCallerType == CallerType::NonSystem) {
    if (mFrameSelection &&
        !mFrameSelection->GetIndependentSelectionRootElement()) {
      // NonSystem and non-independent selection
      flags |= nsIDocumentEncoder::MimicChromeToStringBehaviour;
    }
  }

  ToStringWithFormat(u"text/plain"_ns, flags, 0, aResult, rv);
  if (rv.Failed()) {
    aResult.Truncate();
  }
}

void Selection::ToStringWithFormat(const nsAString& aFormatType,
                                   uint32_t aFlags, int32_t aWrapCol,
                                   nsAString& aReturn, ErrorResult& aRv) {
  nsCOMPtr<nsIDocumentEncoder> encoder =
      do_createDocumentEncoder(NS_ConvertUTF16toUTF8(aFormatType).get());
  if (!encoder) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  Document* doc = presShell->GetDocument();

  // Flags should always include OutputSelectionOnly if we're coming from here:
  aFlags |= nsIDocumentEncoder::OutputSelectionOnly;
  nsAutoString readstring;
  readstring.Assign(aFormatType);
  nsresult rv = encoder->Init(doc, readstring, aFlags);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }

  Selection* selectionToEncode = this;

  if (aFlags & nsIDocumentEncoder::MimicChromeToStringBehaviour) {
    if (const nsFrameSelection* sel =
            presShell->GetLastSelectionForToString()) {
      MOZ_ASSERT(StaticPrefs::dom_selection_mimic_chrome_tostring_enabled());
      selectionToEncode = &sel->NormalSelection();
    }
  }

  encoder->SetSelection(selectionToEncode);
  if (aWrapCol != 0) encoder->SetWrapColumn(aWrapCol);

  rv = encoder->EncodeToString(aReturn);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

nsresult Selection::SetInterlinePosition(InterlinePosition aInterlinePosition) {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);
  MOZ_ASSERT(aInterlinePosition != InterlinePosition::Undefined);

  if (!mFrameSelection) {
    return NS_ERROR_NOT_INITIALIZED;  // Can't do selection
  }

  mFrameSelection->SetHint(aInterlinePosition ==
                                   InterlinePosition::StartOfNextLine
                               ? CaretAssociationHint::After
                               : CaretAssociationHint::Before);
  return NS_OK;
}

Selection::InterlinePosition Selection::GetInterlinePosition() const {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (!mFrameSelection) {
    return InterlinePosition::Undefined;
  }
  return mFrameSelection->GetHint() == CaretAssociationHint::After
             ? InterlinePosition::StartOfNextLine
             : InterlinePosition::EndOfLine;
}

void Selection::SetInterlinePositionJS(bool aHintRight, ErrorResult& aRv) {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  aRv = SetInterlinePosition(aHintRight ? InterlinePosition::StartOfNextLine
                                        : InterlinePosition::EndOfLine);
}

bool Selection::GetInterlinePositionJS(ErrorResult& aRv) const {
  const InterlinePosition interlinePosition = GetInterlinePosition();
  if (interlinePosition == InterlinePosition::Undefined) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);  // Can't do selection
    return false;
  }
  return interlinePosition == InterlinePosition::StartOfNextLine;
}

static bool IsEditorNode(const nsINode* aNode) {
  if (!aNode) {
    return false;
  }

  if (aNode->IsEditable()) {
    return true;
  }

  auto* element = Element::FromNode(aNode);
  return element && element->State().HasState(ElementState::READWRITE);
}

bool Selection::IsEditorSelection() const {
  return IsEditorNode(GetFocusNode());
}

Nullable<int16_t> Selection::GetCaretBidiLevel(
    mozilla::ErrorResult& aRv) const {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return Nullable<int16_t>();
  }
  mozilla::intl::BidiEmbeddingLevel caretBidiLevel =
      static_cast<mozilla::intl::BidiEmbeddingLevel>(
          mFrameSelection->GetCaretBidiLevel());
  return (caretBidiLevel & BIDI_LEVEL_UNDEFINED)
             ? Nullable<int16_t>()
             : Nullable<int16_t>(caretBidiLevel);
}

void Selection::SetCaretBidiLevel(const Nullable<int16_t>& aCaretBidiLevel,
                                  mozilla::ErrorResult& aRv) {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return;
  }
  if (aCaretBidiLevel.IsNull()) {
    mFrameSelection->UndefineCaretBidiLevel();
  } else {
    mFrameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(
        mozilla::intl::BidiEmbeddingLevel(aCaretBidiLevel.Value()));
  }
}

/**
 * Test whether the supplied range points to a single table element.
 * Result is one of the TableSelectionMode constants. "None" means
 * a table element isn't selected.
 */
// TODO: Figure out TableSelectionMode::Column and TableSelectionMode::AllCells
static nsresult GetTableSelectionMode(const nsRange& aRange,
                                      TableSelectionMode* aTableSelectionType) {
  if (!aTableSelectionType) {
    return NS_ERROR_NULL_POINTER;
  }

  *aTableSelectionType = TableSelectionMode::None;

  nsINode* startNode = aRange.GetStartContainer();
  if (!startNode) {
    return NS_ERROR_FAILURE;
  }

  nsINode* endNode = aRange.GetEndContainer();
  if (!endNode) {
    return NS_ERROR_FAILURE;
  }

  // Not a single selected node
  if (startNode != endNode) {
    return NS_OK;
  }

  nsIContent* child = aRange.GetChildAtStartOffset();

  // Not a single selected node
  if (!child || child->GetNextSibling() != aRange.GetChildAtEndOffset()) {
    return NS_OK;
  }

  if (!startNode->IsHTMLElement()) {
    // Implies a check for being an element; if we ever make this work
    // for non-HTML, need to keep checking for elements.
    return NS_OK;
  }

  if (startNode->IsHTMLElement(nsGkAtoms::tr)) {
    *aTableSelectionType = TableSelectionMode::Cell;
  } else  // check to see if we are selecting a table or row (column and all
          // cells not done yet)
  {
    if (child->IsHTMLElement(nsGkAtoms::table)) {
      *aTableSelectionType = TableSelectionMode::Table;
    } else if (child->IsHTMLElement(nsGkAtoms::tr)) {
      *aTableSelectionType = TableSelectionMode::Row;
    }
  }

  return NS_OK;
}

nsresult Selection::MaybeAddTableCellRange(nsRange& aRange,
                                           Maybe<size_t>* aOutIndex) {
  if (!aOutIndex) {
    return NS_ERROR_NULL_POINTER;
  }

  MOZ_ASSERT(aOutIndex->isNothing());

  if (!mFrameSelection) {
    return NS_OK;
  }

  // Get if we are adding a cell selection and the row, col of cell if we are
  TableSelectionMode tableMode;
  nsresult result = GetTableSelectionMode(aRange, &tableMode);
  if (NS_FAILED(result)) return result;

  // If not adding a cell range, we are done here
  if (tableMode != TableSelectionMode::Cell) {
    mFrameSelection->mTableSelection.mMode = tableMode;
    // Don't fail if range isn't a selected cell, aDidAddRange tells caller if
    // we didn't proceed
    return NS_OK;
  }

  // Set frame selection mode only if not already set to a table mode
  // so we don't lose the select row and column flags (not detected by
  // getTableCellLocation)
  if (mFrameSelection->mTableSelection.mMode == TableSelectionMode::None) {
    mFrameSelection->mTableSelection.mMode = tableMode;
  }

  return AddRangesForSelectableNodes(&aRange, aOutIndex,
                                     DispatchSelectstartEvent::Maybe);
}

Selection::Selection(SelectionType aSelectionType,
                     nsFrameSelection* aFrameSelection)
    : mFrameSelection(aFrameSelection),
      mCachedOffsetForFrame(nullptr),
      mDirection(eDirNext),
      mSelectionType(aSelectionType),
      mCustomColors(nullptr),
      mSelectionChangeBlockerCount(0),
      mUserInitiated(false),
      mCalledByJS(false),
      mNotifyAutoCopy(false) {}

Selection::~Selection() { Disconnect(); }

void Selection::Disconnect() {
  RemoveAnchorFocusRange();

  mStyledRanges.UnregisterSelection();

  if (mAutoScroller) {
    mAutoScroller->Stop(AutoScroller::FurtherScrollingAllowed::kNo);
    mAutoScroller = nullptr;
  }

  mScrollEvent.Revoke();

  if (mCachedOffsetForFrame) {
    delete mCachedOffsetForFrame;
    mCachedOffsetForFrame = nullptr;
  }
}

Document* Selection::GetParentObject() const {
  PresShell* presShell = GetPresShell();
  return presShell ? presShell->GetDocument() : nullptr;
}

DocGroup* Selection::GetDocGroup() const {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  Document* doc = presShell->GetDocument();
  return doc ? doc->GetDocGroup() : nullptr;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Selection)

MOZ_CAN_RUN_SCRIPT_BOUNDARY
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Selection)
  // Unlink the selection listeners *before* we do RemoveAllRangesInternal since
  // we don't want to notify the listeners during JS GC (they could be
  // in JS!).
  tmp->mNotifyAutoCopy = false;
  if (tmp->mAccessibleCaretEventHub) {
    tmp->StopNotifyingAccessibleCaretEventHub();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSelectionChangeEventDispatcher)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSelectionListeners)
  MOZ_KnownLive(tmp)->RemoveAllRangesInternal(IgnoreErrors(), IsUnlinking::Yes);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrameSelection)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHighlightData.mHighlight)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Selection)
  {
    uint32_t i, count = tmp->mStyledRanges.mInvalidStaticRanges.Length();
    for (i = 0; i < count; ++i) {
      NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
          mStyledRanges.mInvalidStaticRanges[i].mRange);
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStyledRanges.mRanges)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnchorFocusRange)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrameSelection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHighlightData.mHighlight)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSelectionChangeEventDispatcher)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSelectionListeners)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

// QueryInterface implementation for Selection
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Selection)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Selection)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(Selection, Disconnect())

const RangeBoundary& Selection::AnchorRef(
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) const {
  if (!mAnchorFocusRange) {
    static RangeBoundary sEmpty;
    return sEmpty;
  }

  if (GetDirection() == eDirNext) {
    return aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
               ? mAnchorFocusRange->MayCrossShadowBoundaryStartRef()
               : mAnchorFocusRange->StartRef();
  }

  return aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
             ? mAnchorFocusRange->MayCrossShadowBoundaryEndRef()
             : mAnchorFocusRange->EndRef();
}

const RangeBoundary& Selection::FocusRef(
    AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary) const {
  if (!mAnchorFocusRange) {
    static RangeBoundary sEmpty;
    return sEmpty;
  }

  if (GetDirection() == eDirNext) {
    return aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
               ? mAnchorFocusRange->MayCrossShadowBoundaryEndRef()
               : mAnchorFocusRange->EndRef();
  }
  return aAllowCrossShadowBoundary == AllowRangeCrossShadowBoundary::Yes
             ? mAnchorFocusRange->MayCrossShadowBoundaryStartRef()
             : mAnchorFocusRange->StartRef();
}

void Selection::SetAnchorFocusRange(size_t aIndex) {
  if (aIndex >= mStyledRanges.Length()) {
    return;
  }
  // Highlight selections may contain static ranges.
  MOZ_ASSERT(mSelectionType != SelectionType::eHighlight);
  AbstractRange* anchorFocusRange = mStyledRanges.GetAbstractRangeAt(aIndex);
  mAnchorFocusRange = anchorFocusRange->AsDynamicRange();
}

template <typename PT, typename RT>
static int32_t CompareToRangeStart(
    const RangeBoundaryBase<PT, RT>& aCompareBoundary, RangeBoundaryFor aFor,
    const AbstractRange& aRange, nsContentUtils::NodeIndexCache* aCache) {
  MOZ_ASSERT(aCompareBoundary.IsSet());
  const RangeBoundary& startRef = aRange.MayCrossShadowBoundaryStartRef();
  MOZ_ASSERT(startRef.IsSet());
  // If the nodes that we're comparing are not in the same document, assume
  // that aCompareNode will fall at the end of the ranges.
  if (aCompareBoundary.GetComposedDoc() != startRef.GetComposedDoc() ||
      !startRef.IsSetAndInComposedDoc()) {
    NS_WARNING(
        "`CompareToRangeStart` couldn't compare nodes, pretending some order.");
    return 1;
  }
  const auto rangeBoundaryFor =
      aRange.AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()
          ? RangeBoundaryFor::Collapsed
          : RangeBoundaryFor::Start;
  const Maybe<int32_t> order =
      nsContentUtils::ComparePoints<TreeKind::FlatForSelection>(
          aCompareBoundary.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(aFor),
          startRef.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
              rangeBoundaryFor),
          aCache);
  NS_WARNING_ASSERTION(
      order.isSome(),
      fmt::format(
          "\naCompareBoundary={}\n"
          "  .AsRangeBoundaryInFlatTreeOrNonFlattenedNode({})={}\n"
          "startRef={}\n"
          "  .AsRangeBoundaryInFlatTreeOrNonFlattenedNode({})={}\n",
          aCompareBoundary, aFor,
          aCompareBoundary.AsConstRaw()
              .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(aFor),
          startRef, rangeBoundaryFor,
          startRef.AsConstRaw().AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
              rangeBoundaryFor))
          .c_str());
  return order.valueOr(1);
}

template <typename PT, typename RT>
static int32_t CompareToRangeStart(
    const RangeBoundaryBase<PT, RT>& aCompareBoundary, RangeBoundaryFor aFor,
    const AbstractRange& aRange) {
  return CompareToRangeStart(aCompareBoundary, aFor, aRange, nullptr);
}

template <typename PT, typename RT>
static int32_t CompareToRangeEnd(
    const RangeBoundaryBase<PT, RT>& aCompareBoundary, RangeBoundaryFor aFor,
    const AbstractRange& aRange) {
  MOZ_ASSERT(aCompareBoundary.IsSet());
  MOZ_ASSERT(aRange.IsPositioned());
  const RangeBoundary& endRef = aRange.MayCrossShadowBoundaryEndRef();
  // If the nodes that we're comparing are not in the same document or in the
  // same subtree, assume that aCompareNode will fall at the end of the ranges.
  if (aCompareBoundary.GetComposedDoc() != endRef.GetComposedDoc() ||
      !endRef.IsSetAndInComposedDoc()) {
    NS_WARNING(
        "`CompareToRangeEnd` couldn't compare nodes, pretending some order.");
    return 1;
  }
  const auto rangeBoundaryFor =
      aRange.AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()
          ? RangeBoundaryFor::Collapsed
          : RangeBoundaryFor::End;
  const Maybe<int32_t> order =
      nsContentUtils::ComparePoints<TreeKind::FlatForSelection>(
          aCompareBoundary.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(aFor),
          endRef.AsRangeBoundaryInFlatTreeOrNonFlattenedNode(rangeBoundaryFor));
  NS_WARNING_ASSERTION(
      order.isSome(),
      fmt::format(
          "\naCompareBoundary={}\n"
          "  .AsRangeBoundaryInFlatTreeOrNonFlattenedNode({})={}\n"
          "endRef={}\n"
          "  .AsRangeBoundaryInFlatTreeOrNonFlattenedNode({})={}\n",
          aCompareBoundary, aFor,
          aCompareBoundary.AsConstRaw()
              .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(aFor),
          endRef, rangeBoundaryFor,
          endRef.AsConstRaw().AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
              rangeBoundaryFor))
          .c_str());
  return order.valueOr(1);
}

// Helper to extract AbstractRange* from array elements.
// `FindInsertionPoint` is called with arrayish types that either hold a
// `RefPtr<AbstractRange>` or a `StyledRange` (which holds a
// `RefPtr<AbstractRange>`). The specializations below extract the
// `AbstractRange*` from either type, to avoid needing to overload
// `FindInsertionPoint` itself.
template <typename T>
static const AbstractRange* ExtractRange(const T& aElement) = delete;

template <>
const AbstractRange* ExtractRange<const StyledRange>(
    const StyledRange& aElement) {
  return aElement.mRange;
}

template <>
const AbstractRange* ExtractRange<RefPtr<AbstractRange>>(
    const RefPtr<AbstractRange>& aElement) {
  return aElement.get();
}

// static
template <typename PT, typename RT, typename ArrayType>
size_t Selection::StyledRanges::FindInsertionPoint(
    const ArrayType& aElementArray, const RangeBoundaryBase<PT, RT>& aBoundary,
    RangeBoundaryFor aFor,
    int32_t (*aComparator)(const RangeBoundaryBase<PT, RT>&, RangeBoundaryFor,
                           const AbstractRange&)) {
  using ElementType = std::remove_reference_t<decltype(aElementArray[0])>;

  int32_t beginSearch = 0;
  int32_t endSearch = aElementArray.Length();  // one beyond what to check

  if (endSearch) {
    int32_t center = endSearch - 1;  // Check last index, then binary search
    do {
      const AbstractRange* range =
          ExtractRange<ElementType>(aElementArray[center]);

      int32_t cmp{aComparator(aBoundary, aFor, *range)};

      if (cmp < 0) {  // point < cur
        endSearch = center;
      } else if (cmp > 0) {  // point > cur
        beginSearch = center + 1;
      } else {  // found match, done
        beginSearch = center;
        break;
      }
      center = (endSearch - beginSearch) / 2 + beginSearch;
    } while (endSearch - beginSearch > 0);
  }

  return AssertedCast<size_t>(beginSearch);
}

// Selection::SubtractRange
//
//    A helper function that subtracts aSubtract from aRange, and adds
//    1 or 2 StyledRange objects representing the remaining non-overlapping
//    difference to aOutput. It is assumed that the caller has checked that
//    aRange and aSubtract do indeed overlap

// static
nsresult Selection::StyledRanges::SubtractRange(
    StyledRange& aRange, nsRange& aSubtract, nsTArray<StyledRange>* aOutput) {
  AbstractRange* range = aRange.mRange;
  if (NS_WARN_IF(!range->IsPositioned())) {
    return NS_ERROR_UNEXPECTED;
  }

  if (range->GetStartContainer()->SubtreeRoot() !=
      aSubtract.GetStartContainer()->SubtreeRoot()) {
    // These are ranges for different shadow trees, we can't subtract them in
    // any sensible way.
    aOutput->InsertElementAt(0, aRange);
    return NS_OK;
  }

  // First we want to compare to the range start
  int32_t cmp =
      CompareToRangeStart(range->StartRef(),
                          range->Collapsed() ? RangeBoundaryFor::Collapsed
                                             : RangeBoundaryFor::Start,
                          aSubtract);

  // Also, make a comparison to the range end
  int32_t cmp2 = CompareToRangeEnd(
      range->EndRef(),
      range->Collapsed() ? RangeBoundaryFor::Collapsed : RangeBoundaryFor::End,
      aSubtract);

  // If the existing range left overlaps the new range (aSubtract) then
  // cmp < 0, and cmp2 < 0
  // If it right overlaps the new range then cmp > 0 and cmp2 > 0
  // If it fully contains the new range, then cmp < 0 and cmp2 > 0

  if (cmp2 > 0) {
    // We need to add a new StyledRange to the output, running from
    // the end of aSubtract to the end of range
    ErrorResult error;
    RefPtr<nsRange> postOverlap =
        nsRange::Create(aSubtract.EndRef(), range->EndRef(), error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    MOZ_ASSERT(postOverlap);
    if (!postOverlap->Collapsed()) {
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier.
      aOutput->InsertElementAt(0, StyledRange(postOverlap));
      (*aOutput)[0].mTextRangeStyle = aRange.mTextRangeStyle;
    }
  }

  if (cmp < 0) {
    // We need to add a new StyledRange to the output, running from
    // the start of the range to the start of aSubtract
    ErrorResult error;
    RefPtr<nsRange> preOverlap =
        nsRange::Create(range->StartRef(), aSubtract.StartRef(), error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    MOZ_ASSERT(preOverlap);
    if (!preOverlap->Collapsed()) {
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier.
      aOutput->InsertElementAt(0, StyledRange(preOverlap));
      (*aOutput)[0].mTextRangeStyle = aRange.mTextRangeStyle;
    }
  }

  return NS_OK;
}

static void UserSelectRangesToAdd(nsRange* aItem,
                                  nsTArray<RefPtr<nsRange>>& aRangesToAdd) {
  // We cannot directly call IsEditorSelection() because we may be in an
  // inconsistent state during Collapse() (we're cleared already but we haven't
  // got a new focus node yet).
  if (!StaticPrefs::dom_selection_exclude_non_selectable_nodes() ||
      (IsEditorNode(aItem->GetStartContainer()) &&
       IsEditorNode(aItem->GetEndContainer()))) {
    // Don't mess with the selection ranges for editing, editor doesn't really
    // deal well with multi-range selections.
    aRangesToAdd.AppendElement(aItem);
  } else {
    aItem->ExcludeNonSelectableNodes(&aRangesToAdd);
  }
}

static nsINode* DetermineSelectstartEventTarget(const nsRange& aRange) {
  nsINode* target = aRange.GetStartContainer();
  if (target && target->IsInNativeAnonymousSubtree()) {
    // This is a selection under a text control, selectstart target depends on
    // the pref.
    target = StaticPrefs::dom_select_events_textcontrols_selectstart_enabled()
                 ? target->GetClosestNativeAnonymousSubtreeRootParentOrHost()
                 : nullptr;
  }
  return target;
}

/**
 * @return true, iff the default action should be executed.
 */
static bool MaybeDispatchSelectstartEvent(const nsRange& aRange,
                                          Document* aDocument) {
  nsCOMPtr<nsINode> selectstartEventTarget =
      DetermineSelectstartEventTarget(aRange);

  bool executeDefaultAction = true;

  if (selectstartEventTarget) {
    nsContentUtils::DispatchTrustedEvent(
        aDocument, selectstartEventTarget, u"selectstart"_ns, CanBubble::eYes,
        Cancelable::eYes, &executeDefaultAction);
  }

  return executeDefaultAction;
}

// static
bool Selection::IsUserSelectionCollapsed(
    const nsRange& aRange, nsTArray<RefPtr<nsRange>>& aTempRangesToAdd) {
  MOZ_ASSERT(aTempRangesToAdd.IsEmpty());

  RefPtr<nsRange> scratchRange = aRange.CloneRange();
  UserSelectRangesToAdd(scratchRange, aTempRangesToAdd);
  const bool userSelectionCollapsed =
      (aTempRangesToAdd.Length() == 0) ||
      ((aTempRangesToAdd.Length() == 1) && aTempRangesToAdd[0]->Collapsed());

  aTempRangesToAdd.ClearAndRetainStorage();

  return userSelectionCollapsed;
}

nsresult Selection::AddRangesForUserSelectableNodes(
    nsRange* aRange, Maybe<size_t>* aOutIndex,
    const DispatchSelectstartEvent aDispatchSelectstartEvent) {
  MOZ_ASSERT(mUserInitiated);
  MOZ_ASSERT(aOutIndex);
  MOZ_ASSERT(aOutIndex->isNothing());

  if (!aRange) {
    return NS_ERROR_NULL_POINTER;
  }

  if (!aRange->IsPositioned()) {
    return NS_ERROR_UNEXPECTED;
  }

  AutoTArray<RefPtr<nsRange>, 4> rangesToAdd;
  if (mStyledRanges.Length()) {
    aOutIndex->emplace(mStyledRanges.Length() - 1);
  }

  Document* doc = GetDocument();

  if (aDispatchSelectstartEvent == DispatchSelectstartEvent::Maybe &&
      mSelectionType == SelectionType::eNormal && IsCollapsed() &&
      !IsBlockingSelectionChangeEvents()) {
    // We consider a selection to be starting if we are currently collapsed,
    // and the selection is becoming uncollapsed, and this is caused by a
    // user initiated event.

    // First, we generate the ranges to add with a scratch range, which is a
    // clone of the original range passed in. We do this seperately, because
    // the selectstart event could have caused the world to change, and
    // required ranges to be re-generated

    const bool userSelectionCollapsed =
        IsUserSelectionCollapsed(*aRange, rangesToAdd);
    MOZ_ASSERT(userSelectionCollapsed || nsContentUtils::IsSafeToRunScript());
    if (!userSelectionCollapsed && nsContentUtils::IsSafeToRunScript()) {
      // The spec currently doesn't say that we should dispatch this event
      // on text controls, so for now we only support doing that under a
      // pref, disabled by default.
      // See https://github.com/w3c/selection-api/issues/53.
      const bool executeDefaultAction =
          MaybeDispatchSelectstartEvent(*aRange, doc);

      if (!executeDefaultAction) {
        return NS_OK;
      }

      // As we potentially dispatched an event to the DOM, something could have
      // changed under our feet. Re-generate the rangesToAdd array, and
      // ensure that the range we are about to add is still valid.
      if (!aRange->IsPositioned()) {
        return NS_ERROR_UNEXPECTED;
      }
    }
  }

  // Generate the ranges to add
  UserSelectRangesToAdd(aRange, rangesToAdd);
  size_t newAnchorFocusIndex =
      GetDirection() == eDirPrevious ? 0 : rangesToAdd.Length() - 1;
  for (size_t i = 0; i < rangesToAdd.Length(); ++i) {
    Maybe<size_t> index;
    // `MOZ_KnownLive` needed because of broken static analysis
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1622253#c1).
    nsresult rv = mStyledRanges.MaybeAddRangeAndTruncateOverlaps(
        MOZ_KnownLive(rangesToAdd[i]), &index);
    NS_ENSURE_SUCCESS(rv, rv);
    if (i == newAnchorFocusIndex) {
      *aOutIndex = index;
      rangesToAdd[i]->SetIsGenerated(false);
    } else {
      rangesToAdd[i]->SetIsGenerated(true);
    }
  }
  return NS_OK;
}

nsresult Selection::AddRangesForSelectableNodes(
    nsRange* aRange, Maybe<size_t>* aOutIndex,
    const DispatchSelectstartEvent aDispatchSelectstartEvent) {
  MOZ_ASSERT(aOutIndex);
  MOZ_ASSERT(aOutIndex->isNothing());

  if (!aRange) {
    return NS_ERROR_NULL_POINTER;
  }

  if (!aRange->IsPositioned()) {
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_LOG_FMT(
      sSelectionLog, LogLevel::Debug,
      "{}: selection={}, type={}, range=({}, StartOffset={}, EndOffset={})",
      __func__, static_cast<void*>(this), GetType(), static_cast<void*>(aRange),
      aRange->StartOffset(), aRange->EndOffset());

  if (mUserInitiated) {
    return AddRangesForUserSelectableNodes(aRange, aOutIndex,
                                           aDispatchSelectstartEvent);
  }

  return mStyledRanges.MaybeAddRangeAndTruncateOverlaps(aRange, aOutIndex);
}

nsresult Selection::StyledRanges::AddRangeAndIgnoreOverlaps(
    AbstractRange* aRange) {
  MOZ_ASSERT(aRange);
  MOZ_ASSERT(aRange->IsPositioned());
  MOZ_ASSERT(mSelection.mSelectionType == SelectionType::eHighlight);
  if (aRange->IsStaticRange() && !aRange->AsStaticRange()->IsValid()) {
    mInvalidStaticRanges.AppendElement(StyledRange(aRange));
    return NS_OK;
  }

  // a common case is that we have no ranges yet
  if (mRanges.Length() == 0) {
    if (NS_WARN_IF(
            NS_FAILED(aRange->RegisterSelection(MOZ_KnownLive(mSelection))))) {
      return NS_ERROR_FAILURE;
    }
    mRanges.AppendElement(StyledRange(aRange));
#ifdef ACCESSIBILITY
    a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(),
                                                  *aRange);
#endif
    return NS_OK;
  }

  Maybe<size_t> maybeStartIndex, maybeEndIndex;
  nsresult rv =
      GetIndicesForInterval(aRange->GetStartContainer(), aRange->StartOffset(),
                            aRange->GetEndContainer(), aRange->EndOffset(),
                            false, maybeStartIndex, maybeEndIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  size_t startIndex(0);
  if (maybeEndIndex.isNothing()) {
    // All ranges start after the given range. We can insert our range at
    // position 0.
    startIndex = 0;
  } else if (maybeStartIndex.isNothing()) {
    // All ranges end before the given range. We can insert our range at
    // the end of the array.
    startIndex = mRanges.Length();
  } else {
    startIndex = *maybeStartIndex;
  }

  if (NS_WARN_IF(
          NS_FAILED(aRange->RegisterSelection(MOZ_KnownLive(mSelection))))) {
    return NS_ERROR_FAILURE;
  }
  mRanges.InsertElementAt(startIndex, StyledRange(aRange));
#ifdef ACCESSIBILITY
  a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(), *aRange);
#endif
  return NS_OK;
}

nsresult Selection::StyledRanges::MaybeAddRangeAndTruncateOverlaps(
    nsRange* aRange, Maybe<size_t>* aOutIndex) {
  MOZ_ASSERT(aRange);
  MOZ_ASSERT(aRange->IsPositioned());
  MOZ_ASSERT(aOutIndex);
  MOZ_ASSERT(aOutIndex->isNothing());

  // a common case is that we have no ranges yet
  if (mRanges.Length() == 0) {
    if (NS_WARN_IF(
            NS_FAILED(aRange->RegisterSelection(MOZ_KnownLive(mSelection))))) {
      return NS_ERROR_FAILURE;
    }
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mRanges.AppendElement(StyledRange(aRange));
#ifdef ACCESSIBILITY
    a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(),
                                                  *aRange);
#endif

    aOutIndex->emplace(0u);
    return NS_OK;
  }

  Maybe<size_t> maybeStartIndex, maybeEndIndex;
  nsresult rv =
      GetIndicesForInterval(aRange->GetStartContainer(), aRange->StartOffset(),
                            aRange->GetEndContainer(), aRange->EndOffset(),
                            false, maybeStartIndex, maybeEndIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  size_t startIndex, endIndex;
  if (maybeEndIndex.isNothing()) {
    // All ranges start after the given range. We can insert our range at
    // position 0, knowing there are no overlaps (handled below)
    startIndex = endIndex = 0;
  } else if (maybeStartIndex.isNothing()) {
    // All ranges end before the given range. We can insert our range at
    // the end of the array, knowing there are no overlaps (handled below)
    startIndex = endIndex = mRanges.Length();
  } else {
    startIndex = *maybeStartIndex;
    endIndex = *maybeEndIndex;
  }

  // If the range is already contained in mRanges, silently
  // succeed
  const bool sameRange = HasEqualRangeBoundariesAt(*aRange, startIndex);
  if (sameRange) {
    aOutIndex->emplace(startIndex);
    return NS_OK;
  }

  // Beyond this point, we will expand the selection to cover aRange.
  // Accessibility doesn't need to know about ranges split due to overlaps. It
  // just needs a range that covers any text leaf that is impacted by the
  // change.
#ifdef ACCESSIBILITY
  a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(), *aRange);
#endif

  if (startIndex == endIndex) {
    if (NS_WARN_IF(
            NS_FAILED(aRange->RegisterSelection(MOZ_KnownLive(mSelection))))) {
      return NS_ERROR_FAILURE;
    }
    // The new range doesn't overlap any existing ranges
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mRanges.InsertElementAt(startIndex, StyledRange(aRange));
    aOutIndex->emplace(startIndex);
    return NS_OK;
  }

  // We now know that at least 1 existing range overlaps with the range that
  // we are trying to add. In fact, the only ranges of interest are those at
  // the two end points, startIndex and endIndex - 1 (which may point to the
  // same range) as these may partially overlap the new range. Any ranges
  // between these indices are fully overlapped by the new range, and so can be
  // removed.
  AutoTArray<StyledRange, 2> overlaps;
  overlaps.AppendElement(GetStyledRangeAt(startIndex));
  if (endIndex - 1 != startIndex) {
    overlaps.AppendElement(GetStyledRangeAt(endIndex - 1));
  }

  // Remove all the overlapping ranges
  for (size_t i = startIndex; i < endIndex; ++i) {
    GetAbstractRangeAt(i)->UnregisterSelection(mSelection);
  }
  mRanges.RemoveElementsAt(startIndex, endIndex - startIndex);

  AutoTArray<StyledRange, 3> temp;
  for (const size_t i : Reversed(IntegerRange(overlaps.Length()))) {
    nsresult rv = SubtractRange(overlaps[i], *aRange, &temp);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Insert the new element into our "leftovers" array
  // `aRange` is positioned, so it has to have a start container.
  size_t insertionPoint =
      FindInsertionPoint(temp, aRange->StartRef(),
                         aRange->Collapsed() ? RangeBoundaryFor::Collapsed
                                             : RangeBoundaryFor::Start,
                         CompareToRangeStart);

  temp.InsertElementAt(insertionPoint, StyledRange(aRange));

  for (uint32_t i = 0; i < temp.Length(); ++i) {
    if (temp[i].mRange->IsDynamicRange()) {
      if (NS_WARN_IF(
              NS_FAILED(MOZ_KnownLive(temp[i].mRange->AsDynamicRange())
                            ->RegisterSelection(MOZ_KnownLive(mSelection))))) {
        return NS_ERROR_FAILURE;
      }
      // `MOZ_KnownLive` is required because of
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1622253.
    }
  }

  // Merge the leftovers back in to mRanges
  mRanges.InsertElementsAt(startIndex, temp);

  aOutIndex->emplace(startIndex + insertionPoint);
  return NS_OK;
}

nsresult Selection::StyledRanges::RemoveRangeAndUnregisterSelection(
    AbstractRange& aRange) {
  // Find the range's index & remove it. We could use FindInsertionPoint to
  // get O(log n) time, but that requires many expensive DOM comparisons.
  // For even several thousand items, this is probably faster because the
  // comparisons are so fast.
  const bool rangeExists = mRanges.RemoveElement(&aRange);
  if (!rangeExists) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }

  aRange.UnregisterSelection(mSelection);
#ifdef ACCESSIBILITY
  a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(), aRange);
#endif

  return NS_OK;
}
nsresult Selection::RemoveCollapsedRanges() {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  return mStyledRanges.RemoveCollapsedRanges();
}

nsresult Selection::StyledRanges::RemoveCollapsedRanges() {
  uint32_t i = 0;
  while (i < mRanges.Length()) {
    RefPtr<AbstractRange> range = GetAbstractRangeAt(i);
    // If nsRange::mCrossShadowBoundaryRange exists, it means
    // there's a cross boundary selection, so obviously
    // we shouldn't remove this range.
    const bool collapsed =
        range->Collapsed() && !range->MayCrossShadowBoundary();
    // Cross boundary range should always be uncollapsed.
    MOZ_ASSERT_IF(
        range->MayCrossShadowBoundary(),
        !range->AsDynamicRange()->CrossShadowBoundaryRangeCollapsed());

    if (collapsed) {
      nsresult rv = RemoveRangeAndUnregisterSelection(*range);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      ++i;
    }
  }
  return NS_OK;
}

void Selection::Clear(nsPresContext* aPresContext, IsUnlinking aIsUnlinking) {
  RemoveAnchorFocusRange();

  mStyledRanges.UnregisterSelection();
  for (uint32_t i = 0; i < mStyledRanges.Length(); ++i) {
    SelectFrames(aPresContext, *mStyledRanges.GetAbstractRangeAt(i), false);
  }
  mStyledRanges.Clear();

  // Reset direction so for more dependable table selection range handling
  SetDirection(eDirNext);

  // If this was an ATTENTION selection, change it back to normal now
  if (mFrameSelection && mFrameSelection->GetDisplaySelection() ==
                             nsISelectionController::SELECTION_ATTENTION) {
    mFrameSelection->SetDisplaySelection(nsISelectionController::SELECTION_ON);
  }
}

bool Selection::StyledRanges::HasEqualRangeBoundariesAt(
    const AbstractRange& aRange, size_t aRangeIndex) const {
  if (aRangeIndex < mRanges.Length()) {
    const AbstractRange* range = GetAbstractRangeAt(aRangeIndex);
    return range->HasEqualBoundaries(aRange);
  }
  return false;
}

void Selection::GetRangesForInterval(nsINode& aBeginNode, uint32_t aBeginOffset,
                                     nsINode& aEndNode, uint32_t aEndOffset,
                                     bool aAllowAdjacent,
                                     nsTArray<RefPtr<nsRange>>& aReturn,
                                     mozilla::ErrorResult& aRv) {
  AutoTArray<nsRange*, 2> results;
  nsresult rv =
      GetDynamicRangesForIntervalArray(&aBeginNode, aBeginOffset, &aEndNode,
                                       aEndOffset, aAllowAdjacent, &results);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }

  aReturn.SetLength(results.Length());
  for (size_t i = 0; i < results.Length(); ++i) {
    aReturn[i] = results[i];  // AddRefs
  }
}

nsresult Selection::GetAbstractRangesForIntervalArray(
    nsINode* aBeginNode, uint32_t aBeginOffset, nsINode* aEndNode,
    uint32_t aEndOffset, bool aAllowAdjacent,
    nsTArray<AbstractRange*>* aRanges) {
  if (NS_WARN_IF(!aBeginNode)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (NS_WARN_IF(!aEndNode)) {
    return NS_ERROR_UNEXPECTED;
  }

  aRanges->Clear();
  Maybe<size_t> maybeStartIndex, maybeEndIndex;
  nsresult res = mStyledRanges.GetIndicesForInterval(
      aBeginNode, aBeginOffset, aEndNode, aEndOffset, aAllowAdjacent,
      maybeStartIndex, maybeEndIndex);
  NS_ENSURE_SUCCESS(res, res);

  if (maybeStartIndex.isNothing() || maybeEndIndex.isNothing()) {
    return NS_OK;
  }

  for (const size_t i : IntegerRange(*maybeStartIndex, *maybeEndIndex)) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    aRanges->AppendElement(mStyledRanges.GetAbstractRangeAt(i));
  }

  return NS_OK;
}

nsresult Selection::GetDynamicRangesForIntervalArray(
    nsINode* aBeginNode, uint32_t aBeginOffset, nsINode* aEndNode,
    uint32_t aEndOffset, bool aAllowAdjacent, nsTArray<nsRange*>* aRanges) {
  MOZ_ASSERT(mSelectionType != SelectionType::eHighlight);
  AutoTArray<AbstractRange*, 2> abstractRanges;
  nsresult rv = GetAbstractRangesForIntervalArray(
      aBeginNode, aBeginOffset, aEndNode, aEndOffset, aAllowAdjacent,
      &abstractRanges);
  NS_ENSURE_SUCCESS(rv, rv);
  aRanges->Clear();
  aRanges->SetCapacity(abstractRanges.Length());
  for (auto* abstractRange : abstractRanges) {
    aRanges->AppendElement(abstractRange->AsDynamicRange());
  }
  return NS_OK;
}

nsresult Selection::StyledRanges::ReorderRangesIfNecessary() {
  const Document* doc = mSelection.GetDocument();
  if (!doc) {
    // XXX Should we return error? But it'd cause new exception in JS in the
    // edge case.
    return NS_OK;
  }
  if (mRanges.Length() < 2 && mInvalidStaticRanges.IsEmpty()) {
    // There is nothing to be reordered.
    return NS_OK;
  }
  const int32_t currentDocumentGeneration = doc->GetGeneration();
  const bool domMutationHasHappened =
      currentDocumentGeneration != mDocumentGeneration;
  if (domMutationHasHappened) {
    // After a DOM mutation, invalid static ranges might have become valid and
    // valid static ranges might have become invalid.
    nsTArray<StyledRange> invalidStaticRanges;
    for (size_t i = 0; i < Length();) {
      AbstractRange* range = GetAbstractRangeAt(i);
      if (range->IsStaticRange() && !range->AsStaticRange()->IsValid()) {
        invalidStaticRanges.AppendElement(mRanges.ExtractElementAt(i));
      } else {
        ++i;
      }
    }
    for (auto iter = mInvalidStaticRanges.cbegin();
         iter != mInvalidStaticRanges.cend();) {
      MOZ_ASSERT(iter->mRange->IsStaticRange());
      if (iter->mRange->AsStaticRange()->IsValid()) {
        mRanges.AppendElement(*iter);
        if (!iter->mRange->IsInSelection(mSelection)) {
          if (NS_WARN_IF(
                  NS_FAILED(iter->mRange->RegisterSelection(mSelection)))) {
            return NS_ERROR_FAILURE;
          }
        }
        iter = mInvalidStaticRanges.RemoveElementAt(iter);
      } else {
        ++iter;
      }
    }
    mInvalidStaticRanges.AppendElements(std::move(invalidStaticRanges));
  }
  if (domMutationHasHappened || mRangesMightHaveChanged) {
    // This is hot code. Proceed with caution.
    // This path uses a cache that keep the last 100 node/index combinations
    // in a stack-allocated array to save up on expensive calls to
    // nsINode::ComputeIndexOf() (which happen in
    // nsContentUtils::ComparePoints()).
    // The second expensive call here is the sort() below, which should be
    // avoided if possible. Sorting can be avoided if the ranges are still in
    // order. Checking the order is cheap compared to sorting (also, it fills up
    // the cache, which is reused by the sort call).
    nsContentUtils::NodeIndexCache cache;
    bool rangeOrderHasChanged = false;
    RawRangeBoundary previousStartRef{TreeKind::FlatForSelection};
    for (const auto& range : mRanges.Ranges()) {
      if (!previousStartRef.IsSet()) {
        previousStartRef =
            range->StartRef()
                .AsRaw()
                .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                    range->Collapsed() ? RangeBoundaryFor::Collapsed
                                       : RangeBoundaryFor::Start);
        continue;
      }
      // Calling ComparePoints here saves one call of
      // AbstractRange::StartOffset() per iteration (which is surprisingly
      // expensive).
      const RawRangeBoundary startRef =
          range->MayCrossShadowBoundaryStartRef()
              .AsRaw()
              .AsRangeBoundaryInFlatTreeOrNonFlattenedNode(
                  range->Collapsed() ? RangeBoundaryFor::Collapsed
                                     : RangeBoundaryFor::Start);
      const Maybe<int32_t> compareResult =
          nsContentUtils::ComparePoints<TreeKind::FlatForSelection>(
              startRef, previousStartRef, &cache);
      // If the nodes are in different subtrees, the Maybe is empty.
      // Since CompareToRangeStart pretends ranges to be ordered, this aligns
      // to that behavior.
      if (compareResult.valueOr(1) != 1) {
        rangeOrderHasChanged = true;
        break;
      }
      previousStartRef = startRef;
    }
    if (rangeOrderHasChanged) {
      const auto compare = [&cache](const auto& a, const auto& b) {
        return CompareToRangeStart(a->StartRef(),
                                   a->Collapsed() ? RangeBoundaryFor::Collapsed
                                                  : RangeBoundaryFor::Start,
                                   *b, &cache);
      };
      mRanges.Sort(compare);
    }
    mDocumentGeneration = currentDocumentGeneration;
    mRangesMightHaveChanged = false;
  }
  return NS_OK;
}

nsresult Selection::StyledRanges::GetIndicesForInterval(
    const nsINode* aBeginNode, uint32_t aBeginOffset, const nsINode* aEndNode,
    uint32_t aEndOffset, bool aAllowAdjacent, Maybe<size_t>& aStartIndex,
    Maybe<size_t>& aEndIndex) {
  MOZ_ASSERT(aStartIndex.isNothing());
  MOZ_ASSERT(aEndIndex.isNothing());

  if (NS_WARN_IF(!aBeginNode)) {
    return NS_ERROR_INVALID_POINTER;
  }

  if (NS_WARN_IF(!aEndNode)) {
    return NS_ERROR_INVALID_POINTER;
  }

  if (NS_WARN_IF(NS_FAILED(ReorderRangesIfNecessary()))) {
    return NS_ERROR_FAILURE;
  }

  if (mRanges.Length() == 0) {
    return NS_OK;
  }

  const bool intervalIsCollapsed =
      aBeginNode == aEndNode && aBeginOffset == aEndOffset;

  // Ranges that end before the given interval and begin after the given
  // interval can be discarded
  size_t endsBeforeIndex = FindInsertionPoint(
      mRanges.Ranges(),
      ConstRawRangeBoundary(aEndNode, aEndOffset, RangeBoundarySetBy::Offset),
      intervalIsCollapsed ? RangeBoundaryFor::Collapsed : RangeBoundaryFor::End,
      &CompareToRangeStart);

  if (endsBeforeIndex == 0) {
    const AbstractRange* endRange = GetAbstractRangeAt(endsBeforeIndex);

    // If the interval is strictly before the range at index 0, we can optimize
    // by returning now - all ranges start after the given interval
    if (!endRange->StartRef().Equals(aEndNode, aEndOffset)) {
      return NS_OK;
    }

    // We now know that the start point of mRanges[0].mRange
    // equals the end of the interval. Thus, when aAllowadjacent is true, the
    // caller is always interested in this range. However, when excluding
    // adjacencies, we must remember to include the range when both it and the
    // given interval are collapsed to the same point
    if (!aAllowAdjacent && !(endRange->Collapsed() && intervalIsCollapsed))
      return NS_OK;
  }
  aEndIndex.emplace(endsBeforeIndex);

  size_t beginsAfterIndex =
      FindInsertionPoint(mRanges.Ranges(),
                         ConstRawRangeBoundary(aBeginNode, aBeginOffset,
                                               RangeBoundarySetBy::Offset),
                         intervalIsCollapsed ? RangeBoundaryFor::Collapsed
                                             : RangeBoundaryFor::Start,
                         &CompareToRangeEnd);

  if (beginsAfterIndex == mRanges.Length()) {
    return NS_OK;  // optimization: all ranges are strictly before us
  }

  if (aAllowAdjacent) {
    // At this point, one of the following holds:
    //   endsBeforeIndex == mRanges.Length(),
    //   endsBeforeIndex points to a range whose start point does not equal the
    //     given interval's start point
    //   endsBeforeIndex points to a range whose start point equals the given
    //     interval's start point
    // In the final case, there can be two such ranges, a collapsed range, and
    // an adjacent range (they will appear in mRanges in that
    // order). For this final case, we need to increment endsBeforeIndex, until
    // one of the first two possibilities hold
    while (endsBeforeIndex < mRanges.Length()) {
      const AbstractRange* endRange = GetAbstractRangeAt(endsBeforeIndex);
      if (!endRange->StartRef().Equals(aEndNode, aEndOffset)) {
        break;
      }
      endsBeforeIndex++;
    }

    // Likewise, one of the following holds:
    //   beginsAfterIndex == 0,
    //   beginsAfterIndex points to a range whose end point does not equal
    //     the given interval's end point
    //   beginsOnOrAfter points to a range whose end point equals the given
    //     interval's end point
    // In the final case, there can be two such ranges, an adjacent range, and
    // a collapsed range (they will appear in mRanges in that
    // order). For this final case, we only need to take action if both those
    // ranges exist, and we are pointing to the collapsed range - we need to
    // point to the adjacent range
    const AbstractRange* beginRange = GetAbstractRangeAt(beginsAfterIndex);
    if (beginsAfterIndex > 0 && beginRange->Collapsed() &&
        beginRange->EndRef().Equals(aBeginNode, aBeginOffset)) {
      beginRange = GetAbstractRangeAt(beginsAfterIndex - 1);
      if (beginRange->EndRef().Equals(aBeginNode, aBeginOffset)) {
        beginsAfterIndex--;
      }
    }
  } else {
    // See above for the possibilities at this point. The only case where we
    // need to take action is when the range at beginsAfterIndex ends on
    // the given interval's start point, but that range isn't collapsed (a
    // collapsed range should be included in the returned results).
    const AbstractRange* beginRange = GetAbstractRangeAt(beginsAfterIndex);
    if (beginRange->MayCrossShadowBoundaryEndRef().Equals(aBeginNode,
                                                          aBeginOffset) &&
        !beginRange->Collapsed()) {
      beginsAfterIndex++;
    }

    // Again, see above for the meaning of endsBeforeIndex at this point.
    // In particular, endsBeforeIndex may point to a collaped range which
    // represents the point at the end of the interval - this range should be
    // included
    if (endsBeforeIndex < mRanges.Length()) {
      const AbstractRange* endRange = GetAbstractRangeAt(endsBeforeIndex);
      if (endRange->MayCrossShadowBoundaryStartRef().Equals(aEndNode,
                                                            aEndOffset) &&
          endRange->Collapsed()) {
        endsBeforeIndex++;
      }
    }
  }

  NS_ASSERTION(beginsAfterIndex <= endsBeforeIndex, "Is mRanges not ordered?");
  NS_ENSURE_STATE(beginsAfterIndex <= endsBeforeIndex);

  aStartIndex.emplace(beginsAfterIndex);
  aEndIndex = Some(endsBeforeIndex);
  return NS_OK;
}

nsIFrame* Selection::GetPrimaryFrameForAnchorNode() const {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  nsCOMPtr<nsIContent> content = do_QueryInterface(GetAnchorNode());
  if (content && mFrameSelection) {
    return SelectionMovementUtils::GetFrameForNodeOffset(
        content, AnchorOffset(), mFrameSelection->GetHint());
  }
  return nullptr;
}

PrimaryFrameData Selection::GetPrimaryFrameForCaretAtFocusNode(
    bool aVisual) const {
  nsIContent* content = nsIContent::FromNodeOrNull(GetFocusNode());
  if (!content || !mFrameSelection || !mFrameSelection->GetPresShell()) {
    return {};
  }

  MOZ_ASSERT(mFrameSelection->GetPresShell()->GetDocument() ==
             content->GetComposedDoc());

  CaretAssociationHint hint = mFrameSelection->GetHint();
  intl::BidiEmbeddingLevel caretBidiLevel =
      mFrameSelection->GetCaretBidiLevel();
  return SelectionMovementUtils::GetPrimaryFrameForCaret(
      content, FocusOffset(), aVisual, hint, caretBidiLevel);
}

void Selection::SelectFramesOf(nsIContent* aContent, bool aSelected) const {
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame) {
    return;
  }
  // The frame could be an SVG text frame, in which case we don't treat it
  // as a text frame.
  if (frame->IsTextFrame()) {
    nsTextFrame* textFrame = static_cast<nsTextFrame*>(frame);
    textFrame->SelectionStateChanged(
        0, textFrame->CharacterDataBuffer().GetLength(), aSelected,
        mSelectionType);
  } else {
    frame->SelectionStateChanged();
  }
}

void Selection::SelectFramesInAllRanges(nsPresContext* aPresContext) {
  // this method is currently only called in a user-initiated context.
  // therefore it is safe to assume that we are not in a Highlight selection
  // and we only have to deal with nsRanges (no StaticRanges).
  MOZ_ASSERT(mSelectionType != SelectionType::eHighlight);
  for (size_t i = 0; i < mStyledRanges.Length(); ++i) {
    nsRange* range = mStyledRanges.GetAbstractRangeAt(i)->AsDynamicRange();
    MOZ_ASSERT(range->IsInAnySelection());
    SelectFrames(aPresContext, *range, range->IsInAnySelection());
  }
}

/**
 * The idea of this helper method is to select or deselect "top to bottom",
 * traversing through the frames
 */
nsresult Selection::SelectFrames(nsPresContext* aPresContext,
                                 AbstractRange& aRange, bool aSelect) const {
  if (!mFrameSelection || !aPresContext || !aPresContext->GetPresShell()) {
    // nothing to do
    return NS_OK;
  }

  MOZ_DIAGNOSTIC_ASSERT_IF(!aRange.IsPositioned(),
                           !aRange.MayCrossShadowBoundary());

  MOZ_DIAGNOSTIC_ASSERT(aRange.IsPositioned());

  const Document* const document = GetDocument();
  if (MOZ_UNLIKELY(!document ||
                   aRange.GetComposedDocOfContainers() != document)) {
    return NS_OK;  // Do nothing if the range is now in different document.
  }

  if (aRange.IsStaticRange() && !aRange.AsStaticRange()->IsValid()) {
    // TODO jjaschke: Actions necessary to unselect invalid static ranges?
    return NS_OK;
  }

  if (mFrameSelection->IsInTableSelectionMode()) {
    const nsIContent* const commonAncestorContent =
        nsIContent::FromNodeOrNull(aRange.GetClosestCommonInclusiveAncestor(
            StaticPrefs::dom_select_events_textcontrols_selectstart_enabled()
                ? AllowRangeCrossShadowBoundary::Yes
                : AllowRangeCrossShadowBoundary::No));
    nsIFrame* const frame = commonAncestorContent
                                ? commonAncestorContent->GetPrimaryFrame()
                                : aPresContext->PresShell()->GetRootFrame();
    if (frame) {
      if (frame->IsTextFrame()) {
        MOZ_ASSERT(commonAncestorContent ==
                   aRange.GetMayCrossShadowBoundaryStartContainer());
        MOZ_ASSERT(commonAncestorContent ==
                   aRange.GetMayCrossShadowBoundaryEndContainer());
        static_cast<nsTextFrame*>(frame)->SelectionStateChanged(
            aRange.MayCrossShadowBoundaryStartOffset(),
            aRange.MayCrossShadowBoundaryEndOffset(), aSelect, mSelectionType);
      } else {
        frame->SelectionStateChanged();
      }
    }

    return NS_OK;
  }

  // Loop through the content iterator for each content node; for each text
  // node, call SetSelected on it:
  nsIContent* const startContent = nsIContent::FromNodeOrNull(
      aRange.GetMayCrossShadowBoundaryStartContainer());
  if (MOZ_UNLIKELY(!startContent)) {
    // Don't warn, bug 1055722
    // XXX The range can start from a document node and such range can be
    //     added to Selection with JS.  Therefore, even in such cases,
    //     shouldn't we handle selection in the range?
    return NS_ERROR_UNEXPECTED;
  }
  MOZ_DIAGNOSTIC_ASSERT(startContent->IsInComposedDoc());

  // We must call first one explicitly
  nsINode* const endNode = aRange.GetMayCrossShadowBoundaryEndContainer();
  if (NS_WARN_IF(!endNode)) {
    // We null-checked start node above, therefore, end node should also be
    // non-null here.
    return NS_ERROR_UNEXPECTED;
  }
  const bool isFirstContentTextNode = startContent->IsText();
  if (isFirstContentTextNode) {
    if (nsIFrame* const frame = startContent->GetPrimaryFrame()) {
      // The frame could be an SVG text frame, in which case we don't treat it
      // as a text frame.
      if (frame->IsTextFrame()) {
        const uint32_t startOffset = aRange.MayCrossShadowBoundaryStartOffset();
        const uint32_t endOffset =
            endNode == startContent ? aRange.MayCrossShadowBoundaryEndOffset()
                                    : startContent->Length();
        static_cast<nsTextFrame*>(frame)->SelectionStateChanged(
            startOffset, endOffset, aSelect, mSelectionType);
      } else {
        frame->SelectionStateChanged();
      }
    }
  }

  // If the range is in a node and the node is a leaf node, we don't need to
  // walk the subtree.
  if ((aRange.Collapsed() && !aRange.MayCrossShadowBoundary()) ||
      (startContent == endNode && !startContent->HasChildren())) {
    if (!isFirstContentTextNode) {
      SelectFramesOf(startContent, aSelect);
    }
    return NS_OK;
  }

  ContentSubtreeIterator subtreeIter;
  nsresult rv = subtreeIter.InitWithAllowCrossShadowBoundary(&aRange);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (isFirstContentTextNode && !subtreeIter.IsDone() &&
      subtreeIter.GetCurrentNode() == startContent) {
    subtreeIter.Next();  // first content has already been handled.
  }
  for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
    MOZ_DIAGNOSTIC_ASSERT(subtreeIter.GetCurrentNode());
    if (nsIContent* const content =
            nsIContent::FromNodeOrNull(subtreeIter.GetCurrentNode())) {
      SelectFramesOfFlattenedTreeOfContent(content, aSelect);
    }
  }

  // We must now do the last one if it is not the same as the first
  if (endNode == startContent || !endNode->IsText()) {
    return NS_OK;
  }

  if (nsIFrame* const frame = endNode->AsText()->GetPrimaryFrame()) {
    // The frame could be an SVG text frame, in which case we'll ignore it.
    if (frame->IsTextFrame()) {
      static_cast<nsTextFrame*>(frame)->SelectionStateChanged(
          0, aRange.MayCrossShadowBoundaryEndOffset(), aSelect, mSelectionType);
    }
  }
  return NS_OK;
}

void Selection::SelectFramesOfFlattenedTreeOfContent(nsIContent* aContent,
                                                     bool aSelected) const {
  MOZ_ASSERT(aContent);
  TreeIterator<FlattenedChildIteratorForSelection> iter(*aContent);
  for (; iter.GetCurrent(); iter.GetNext()) {
    SelectFramesOf(iter.GetCurrent(), aSelected);
  }
}

// Selection::LookUpSelection
//
//    This function is called when a node wants to know where the selection is
//    over itself.
//
//    Usually, this is called when we already know there is a selection over
//    the node in question, and we only need to find the boundaries of it on
//    that node. This is when slowCheck is false--a strict test is not needed.
//    Other times, the caller has no idea, and wants us to test everything,
//    so we are supposed to determine whether there is a selection over the
//    node at all.
//
//    A previous version of this code used this flag to do less work when
//    inclusion was already known (slowCheck=false). However, our tree
//    structure allows us to quickly determine ranges overlapping the node,
//    so we just ignore the slowCheck flag and do the full test every time.
//
//    PERFORMANCE: a common case is that we are doing a fast check with exactly
//    one range in the selection. In this case, this function is slower than
//    brute force because of the overhead of checking the tree. We can optimize
//    this case to make it faster by doing the same thing the previous version
//    of this function did in the case of 1 range. This would also mean that
//    the aSlowCheck flag would have meaning again.

UniquePtr<SelectionDetails> Selection::LookUpSelection(
    nsIContent* aContent, uint32_t aContentOffset, uint32_t aContentLength,
    UniquePtr<SelectionDetails> aDetailsHead, SelectionType aSelectionType) {
  if (!aContent) {
    return aDetailsHead;
  }

  // it is common to have no ranges, to optimize that
  if (mStyledRanges.Length() == 0) {
    return aDetailsHead;
  }

  nsTArray<AbstractRange*> overlappingRanges;
  SelectionNodeCache* cache =
      GetPresShell() ? GetPresShell()->GetSelectionNodeCache() : nullptr;
  if (cache && RangeCount() == 1) {
    const bool isFullySelected =
        cache->MaybeCollectNodesAndCheckIfFullySelected(aContent, this);
    if (isFullySelected) {
      auto newHead = MakeUnique<SelectionDetails>();

      newHead->mNext = std::move(aDetailsHead);
      newHead->mStart = AssertedCast<int32_t>(0);
      newHead->mEnd = AssertedCast<int32_t>(aContentLength);
      newHead->mSelectionType = aSelectionType;
      newHead->mHighlightData = mHighlightData;
      if (const TextRangeStyle* style =
              mStyledRanges.GetNonDefaultTextRangeStyle(
                  GetAbstractRangeAt(0))) {
        newHead->mTextRangeStyle = *style;
      }
      auto detailsHead = std::move(newHead);

      return detailsHead;
    }
  }

  nsresult rv = GetAbstractRangesForIntervalArray(
      aContent, aContentOffset, aContent, aContentOffset + aContentLength,
      false, &overlappingRanges);
  if (NS_FAILED(rv)) {
    return aDetailsHead;
  }

  if (overlappingRanges.Length() == 0) {
    return aDetailsHead;
  }

  UniquePtr<SelectionDetails> detailsHead = std::move(aDetailsHead);

  for (size_t i = 0; i < overlappingRanges.Length(); i++) {
    AbstractRange* range = overlappingRanges[i];
    if (range->IsStaticRange() && !range->AsStaticRange()->IsValid()) {
      continue;
    }

    nsINode* startNode = range->GetMayCrossShadowBoundaryStartContainer();
    nsINode* endNode = range->GetMayCrossShadowBoundaryEndContainer();
    uint32_t startOffset = range->MayCrossShadowBoundaryStartOffset();
    uint32_t endOffset = range->MayCrossShadowBoundaryEndOffset();

    Maybe<uint32_t> start, end;
    if (startNode == aContent && endNode == aContent) {
      if (startOffset < (aContentOffset + aContentLength) &&
          endOffset > aContentOffset) {
        // this range is totally inside the requested content range
        start.emplace(
            startOffset >= aContentOffset ? startOffset - aContentOffset : 0u);
        end.emplace(std::min(aContentLength, endOffset - aContentOffset));
      }
      // otherwise, range is inside the requested node, but does not intersect
      // the requested content range, so ignore it
    } else if (startNode == aContent) {
      if (startOffset < (aContentOffset + aContentLength)) {
        // the beginning of the range is inside the requested node, but the
        // end is outside, select everything from there to the end
        start.emplace(
            startOffset >= aContentOffset ? startOffset - aContentOffset : 0u);
        end.emplace(aContentLength);
      }
    } else if (endNode == aContent) {
      if (endOffset > aContentOffset) {
        // the end of the range is inside the requested node, but the beginning
        // is outside, select everything from the beginning to there
        start.emplace(0u);
        end.emplace(std::min(aContentLength, endOffset - aContentOffset));
      }
    } else {
      // this range does not begin or end in the requested node, but since
      // GetRangesForInterval returned this range, we know it overlaps.
      // Therefore, this node is enclosed in the range, and we select all
      // of it.
      start.emplace(0u);
      end.emplace(aContentLength);
    }
    if (start.isNothing()) {
      continue;  // the ranges do not overlap the input range
    }

    auto newHead = MakeUnique<SelectionDetails>();

    newHead->mNext = std::move(detailsHead);
    newHead->mStart = AssertedCast<int32_t>(*start);
    newHead->mEnd = AssertedCast<int32_t>(*end);
    newHead->mSelectionType = aSelectionType;
    newHead->mHighlightData = mHighlightData;
    if (const TextRangeStyle* style =
            mStyledRanges.GetNonDefaultTextRangeStyle(range)) {
      newHead->mTextRangeStyle = *style;
    }
    detailsHead = std::move(newHead);
  }
  return detailsHead;
}

NS_IMETHODIMP
Selection::Repaint(nsPresContext* aPresContext) {
  int32_t arrCount = (int32_t)mStyledRanges.Length();

  if (arrCount < 1) return NS_OK;

  int32_t i;

  for (i = 0; i < arrCount; i++) {
    MOZ_ASSERT(mStyledRanges.GetAbstractRangeAt(i));
    nsresult rv =
        SelectFrames(aPresContext, *mStyledRanges.GetAbstractRangeAt(i), true);

    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

void Selection::SetCanCacheFrameOffset(bool aCanCacheFrameOffset) {
  if (!mCachedOffsetForFrame) {
    mCachedOffsetForFrame = new CachedOffsetForFrame;
  }

  mCachedOffsetForFrame->mCanCacheFrameOffset = aCanCacheFrameOffset;

  // clean up cached frame when turn off cache
  // fix bug 207936
  if (!aCanCacheFrameOffset) {
    mCachedOffsetForFrame->mLastCaretFrame = nullptr;
  }
}

nsresult Selection::GetCachedFrameOffset(nsIFrame* aFrame, int32_t inOffset,
                                         nsPoint& aPoint) {
  if (!mCachedOffsetForFrame) {
    mCachedOffsetForFrame = new CachedOffsetForFrame;
  }

  nsresult rv = NS_OK;
  if (mCachedOffsetForFrame->mCanCacheFrameOffset &&
      mCachedOffsetForFrame->mLastCaretFrame &&
      (aFrame == mCachedOffsetForFrame->mLastCaretFrame) &&
      (inOffset == mCachedOffsetForFrame->mLastContentOffset)) {
    // get cached frame offset
    aPoint = mCachedOffsetForFrame->mCachedFrameOffset;
  } else {
    // Recalculate frame offset and cache it. Don't cache a frame offset if
    // GetPointFromOffset fails, though.
    rv = aFrame->GetPointFromOffset(inOffset, &aPoint);
    if (NS_SUCCEEDED(rv) && mCachedOffsetForFrame->mCanCacheFrameOffset) {
      mCachedOffsetForFrame->mCachedFrameOffset = aPoint;
      mCachedOffsetForFrame->mLastCaretFrame = aFrame;
      mCachedOffsetForFrame->mLastContentOffset = inOffset;
    }
  }

  return rv;
}

Element* Selection::GetAncestorLimiter() const {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (mFrameSelection) {
    return mFrameSelection->GetAncestorLimiter();
  }
  return nullptr;
}

void Selection::SetAncestorLimiter(Element* aLimiter) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aLimiter", aLimiter);
    LogStackForSelectionAPI();
  }

  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (mFrameSelection) {
    RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
    frameSelection->SetAncestorLimiter(aLimiter);
  }
}

void Selection::StyledRanges::UnregisterSelection(IsUnlinking aIsUnlinking) {
  for (const auto& range : Ranges()) {
    range->UnregisterSelection(mSelection, aIsUnlinking);
  }
}

void Selection::StyledRanges::Clear() {
#ifdef ACCESSIBILITY
  for (auto& range : Ranges()) {
    if (!a11y::SelectionManager::SelectionRangeChanged(mSelection.GetType(),
                                                       *range)) {
      break;
    }
  }
#endif
  mRanges.Clear();
  mInvalidStaticRanges.Clear();
}

const TextRangeStyle* Selection::StyledRanges::GetNonDefaultTextRangeStyle(
    const AbstractRange* aRange) {
  return mRanges.GetTextRangeStyleIfNotDefault(aRange);
}

size_t Selection::StyledRanges::Length() const { return mRanges.Length(); }

nsresult Selection::SetTextRangeStyle(nsRange* aRange,
                                      const TextRangeStyle& aTextRangeStyle) {
  NS_ENSURE_ARG_POINTER(aRange);
  MOZ_ASSERT(
      mStyledRanges.Ranges().IndexOf(aRange) != Span<AbstractRange>::npos,
      "Range is not part of this Selection?");
  mStyledRanges.mRanges.SetTextRangeStyle(aRange, aTextRangeStyle);
  return NS_OK;
}

nsresult Selection::StartAutoScrollTimer(nsIFrame* aFrame,
                                         const nsPoint& aPoint,
                                         uint32_t aDelayInMs) {
  MOZ_ASSERT(aFrame, "Need a frame");
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (!mFrameSelection) {
    return NS_OK;  // nothing to do
  }

  if (!mAutoScroller) {
    mAutoScroller = new AutoScroller(mFrameSelection);
  }

  mAutoScroller->SetDelay(aDelayInMs);

  RefPtr<AutoScroller> autoScroller{mAutoScroller};
  return autoScroller->DoAutoScroll(aFrame, aPoint);
}

nsresult Selection::StopAutoScrollTimer() {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (mAutoScroller) {
    mAutoScroller->Stop(AutoScroller::FurtherScrollingAllowed::kYes);
  }

  return NS_OK;
}

nsresult AutoScroller::DoAutoScroll(nsIFrame* aFrame, nsPoint aPoint) {
  MOZ_ASSERT(aFrame, "Need a frame");

  Stop(FurtherScrollingAllowed::kYes);

  nsPresContext* presContext = aFrame->PresContext();
  RefPtr<PresShell> presShell = presContext->PresShell();
  nsRootPresContext* rootPC = presContext->GetRootPresContext();
  if (!rootPC) {
    return NS_OK;
  }
  nsIFrame* rootmostFrame = rootPC->PresShell()->GetRootFrame();
  AutoWeakFrame weakRootFrame(rootmostFrame);
  AutoWeakFrame weakFrame(aFrame);
  // Get the point relative to the root most frame because the scroll we are
  // about to do will change the coordinates of aFrame.
  nsPoint globalPoint = aPoint + aFrame->GetOffsetToCrossDoc(rootmostFrame);

  bool done = false;
  bool didScroll;
  while (true) {
    didScroll = presShell->ScrollFrameIntoView(
        aFrame, Some(nsRect(aPoint, nsSize())), AxisScrollParams(),
        AxisScrollParams(), ScrollFlags::None);
    if (!weakFrame || !weakRootFrame) {
      return NS_OK;
    }
    if (!didScroll && !done) {
      // If aPoint is at the very edge of the root, then try to scroll anyway,
      // once.
      nsRect rootRect = rootmostFrame->GetRect();
      nscoord onePx = AppUnitsPerCSSPixel();
      nscoord scrollAmount = 10 * onePx;
      if (std::abs(rootRect.x - globalPoint.x) <= onePx) {
        aPoint.x -= scrollAmount;
      } else if (std::abs(rootRect.XMost() - globalPoint.x) <= onePx) {
        aPoint.x += scrollAmount;
      } else if (std::abs(rootRect.y - globalPoint.y) <= onePx) {
        aPoint.y -= scrollAmount;
      } else if (std::abs(rootRect.YMost() - globalPoint.y) <= onePx) {
        aPoint.y += scrollAmount;
      } else {
        break;
      }
      done = true;
      continue;
    }
    break;
  }

  // Start the AutoScroll timer if necessary.
  // `ScrollFrameRectIntoView` above may have run script and this may have
  // forbidden to continue scrolling.
  if (didScroll && mFurtherScrollingAllowed == FurtherScrollingAllowed::kYes) {
    nsPoint presContextPoint =
        globalPoint -
        presShell->GetRootFrame()->GetOffsetToCrossDoc(rootmostFrame);
    ScheduleNextDoAutoScroll(presContext, presContextPoint);
  }

  return NS_OK;
}

void Selection::RemoveAllRanges(ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  RemoveAllRangesInternal(aRv);
}

already_AddRefed<StaticRange> Selection::GetComposedRange(
    const AbstractRange* aRange,
    const Sequence<OwningNonNull<ShadowRoot>>& aShadowRoots) const {
  // If aIsEndNode is true, this method does the Step 5.1 and 5.2
  // in https://www.w3.org/TR/selection-api/#dom-selection-getcomposedranges,
  // otherwise it does the Step 3.1 and 3.2.
  auto reScope = [&aShadowRoots](nsINode*& aNode, uint32_t& aOffset,
                                 bool aIsEndNode) {
    MOZ_ASSERT(aNode);
    while (aNode) {
      const ShadowRoot* shadowRootOfNode = aNode->GetContainingShadow();
      if (!shadowRootOfNode) {
        return;
      }

      for (const OwningNonNull<ShadowRoot>& shadowRoot : aShadowRoots) {
        if (shadowRoot->IsShadowIncludingInclusiveDescendantOf(
                shadowRootOfNode)) {
          return;
        }
      }

      const nsIContent* host = aNode->GetContainingShadowHost();
      const Maybe<uint32_t> maybeIndex = host->ComputeIndexInParentContent();
      MOZ_ASSERT(maybeIndex.isSome(), "not parent or anonymous child?");
      if (MOZ_UNLIKELY(maybeIndex.isNothing())) {
        // Unlikely to happen, but still set aNode to nullptr to avoid
        // leaking information about the shadow tree.
        aNode = nullptr;
        return;
      }
      aOffset = maybeIndex.value();
      if (aIsEndNode) {
        aOffset += 1;
      }
      aNode = host->GetParentNode();
    }
  };

  nsINode* startNode = aRange->GetMayCrossShadowBoundaryStartContainer();
  uint32_t startOffset = aRange->MayCrossShadowBoundaryStartOffset();
  nsINode* endNode = aRange->GetMayCrossShadowBoundaryEndContainer();
  uint32_t endOffset = aRange->MayCrossShadowBoundaryEndOffset();

  reScope(startNode, startOffset, false /* aIsEndNode */);
  reScope(endNode, endOffset, true /* aIsEndNode */);

  RefPtr<StaticRange> composedRange = StaticRange::Create(
      startNode, startOffset, endNode, endOffset, IgnoreErrors());
  return composedRange.forget();
}

void Selection::GetComposedRanges(
    const ShadowRootOrGetComposedRangesOptions&
        aShadowRootOrGetComposedRangesOptions,
    const Sequence<OwningNonNull<ShadowRoot>>& aShadowRoots,
    nsTArray<RefPtr<StaticRange>>& aComposedRanges) {
  aComposedRanges.SetCapacity(mStyledRanges.Length());

  auto GetComposedRangesForAllRanges =
      [this, &aComposedRanges](
          const Sequence<OwningNonNull<ShadowRoot>>& aShadowRoots) {
        for (const auto& range : this->mStyledRanges.Ranges()) {
          aComposedRanges.AppendElement(GetComposedRange(range, aShadowRoots));
        }
      };

  if (aShadowRootOrGetComposedRangesOptions.IsGetComposedRangesOptions()) {
    // { shadowRoots: ... }
    auto& options =
        aShadowRootOrGetComposedRangesOptions.GetAsGetComposedRangesOptions();
    return GetComposedRangesForAllRanges(options.mShadowRoots);
  }

  Sequence<OwningNonNull<ShadowRoot>> shadowRoots(aShadowRoots);

  if (aShadowRootOrGetComposedRangesOptions.IsShadowRoot()) {
    // Single shadow root provide
    //
    // The order in shadowRoots doesn't matter, as we just loop
    // through them eventually.
    if (!shadowRoots.AppendElement(
            aShadowRootOrGetComposedRangesOptions.GetAsShadowRoot(),
            fallible)) {
      // OOM
      return;
    }
  }

  // single + variadic
  return GetComposedRangesForAllRanges(shadowRoots);
}

void Selection::RemoveAllRangesInternal(ErrorResult& aRv,
                                        IsUnlinking aIsUnlinking) {
  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return;
  }

  RefPtr<nsPresContext> presContext = GetPresContext();
  Clear(presContext, aIsUnlinking);

  // Turn off signal for table selection
  RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
  frameSelection->ClearTableCellSelection();

  RefPtr<Selection> kungFuDeathGrip{this};
  // Be aware, this instance may be destroyed after this call.
  NotifySelectionListeners();
}

void Selection::AddRangeJS(nsRange& aRange, ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aRange", aRange);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  RefPtr<Document> document(GetDocument());
  AddRangeAndSelectFramesAndNotifyListenersInternal(aRange, document, aRv);
  if (StaticPrefs::dom_selection_mimic_chrome_tostring_enabled() &&
      !aRv.Failed()) {
    if (auto* presShell = GetPresShell()) {
      presShell->UpdateLastSelectionForToString(mFrameSelection);
    }
  }
}

void Selection::AddRangeAndSelectFramesAndNotifyListeners(nsRange& aRange,
                                                          ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aRange", aRange);
    LogStackForSelectionAPI();
  }

  RefPtr<Document> document(GetDocument());
  return AddRangeAndSelectFramesAndNotifyListenersInternal(aRange, document,
                                                           aRv);
}

void Selection::AddRangeAndSelectFramesAndNotifyListenersInternal(
    nsRange& aRange, Document* aDocument, ErrorResult& aRv) {
  RefPtr<nsRange> range = &aRange;
  if (aRange.IsInAnySelection()) {
    if (aRange.IsInSelection(*this)) {
      // If we already have the range, we don't need to handle this except
      // setting the interline position.
      if (mSelectionType == SelectionType::eNormal) {
        SetInterlinePosition(InterlinePosition::StartOfNextLine);
      }
      return;
    }
    if (mSelectionType != SelectionType::eNormal &&
        mSelectionType != SelectionType::eHighlight) {
      range = aRange.CloneRange();
    }
  }

  nsINode* rangeRoot = range->GetRoot();
  if (aDocument != rangeRoot &&
      (!rangeRoot || aDocument != rangeRoot->GetComposedDoc())) {
    // http://w3c.github.io/selection-api/#dom-selection-addrange
    // "...  if the root of the range's boundary points are the document
    // associated with context object. Otherwise, this method must do nothing."
    return;
  }

  // MaybeAddTableCellRange might flush frame and `NotifySelectionListeners`
  // below might destruct `this`.
  RefPtr<Selection> kungFuDeathGrip(this);

  // This inserts a table cell range in proper document order
  // and returns NS_OK if range doesn't contain just one table cell
  Maybe<size_t> maybeRangeIndex;
  nsresult result = MaybeAddTableCellRange(*range, &maybeRangeIndex);
  if (NS_FAILED(result)) {
    aRv.Throw(result);
    return;
  }

  if (maybeRangeIndex.isNothing()) {
    result = AddRangesForSelectableNodes(range, &maybeRangeIndex,
                                         DispatchSelectstartEvent::Maybe);
    if (NS_FAILED(result)) {
      aRv.Throw(result);
      return;
    }
    if (maybeRangeIndex.isNothing()) {
      return;
    }
  }

  MOZ_ASSERT(*maybeRangeIndex < mStyledRanges.Length());

  SetAnchorFocusRange(*maybeRangeIndex);

  // Make sure the caret appears on the next line, if at a newline
  if (mSelectionType == SelectionType::eNormal) {
    SetInterlinePosition(InterlinePosition::StartOfNextLine);
  }

  if (!mFrameSelection) {
    return;  // nothing to do
  }

  RefPtr<nsPresContext> presContext = GetPresContext();
  SelectFrames(presContext, *range, true);

  // Be aware, this instance may be destroyed after this call.
  NotifySelectionListeners();
  // Range order is guaranteed after adding a range.
  // Therefore, this flag can be reset to avoid
  // another unnecessary and costly reordering.
  mStyledRanges.mRangesMightHaveChanged = false;
}

void Selection::AddHighlightRangeAndSelectFramesAndNotifyListeners(
    AbstractRange& aRange) {
  MOZ_ASSERT(mSelectionType == SelectionType::eHighlight);
  nsresult rv = mStyledRanges.AddRangeAndIgnoreOverlaps(&aRange);
  if (NS_FAILED(rv)) {
    return;
  }

  if (!mFrameSelection) {
    return;  // nothing to do
  }

  RefPtr<nsPresContext> presContext = GetPresContext();
  SelectFrames(presContext, aRange, true);

  // Be aware, this instance may be destroyed after this call.
  RefPtr<Selection> kungFuDeathGrip(this);
  NotifySelectionListeners();
  // Range order is guaranteed after adding a range.
  // Therefore, this flag can be reset to avoid
  // another unnecessary and costly reordering.
  mStyledRanges.mRangesMightHaveChanged = false;
}

// Selection::RemoveRangeAndUnselectFramesAndNotifyListeners
//
//    Removes the given range from the selection. The tricky part is updating
//    the flags on the frames that indicate whether they have a selection or
//    not. There could be several selection ranges on the frame, and clearing
//    the bit would cause the selection to not be drawn, even when there is
//    another range on the frame (bug 346185).
//
//    We therefore find any ranges that intersect the same nodes as the range
//    being removed, and cause them to set the selected bits back on their
//    selected frames after we've cleared the bit from ours.

void Selection::RemoveRangeAndUnselectFramesAndNotifyListeners(
    AbstractRange& aRange, ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aRange", aRange);
    LogStackForSelectionAPI();
  }

  nsresult rv = mStyledRanges.RemoveRangeAndUnregisterSelection(aRange);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }

  nsINode* beginNode = aRange.GetStartContainer();
  nsINode* endNode = aRange.GetEndContainer();

  if (!beginNode || !endNode) {
    // Detached range; nothing else to do here.
    return;
  }

  // find out the length of the end node, so we can select all of it
  uint32_t beginOffset, endOffset;
  if (endNode->IsText()) {
    // Get the length of the text. We can't just use the offset because
    // another range could be touching this text node but not intersect our
    // range.
    beginOffset = 0;
    endOffset = endNode->AsText()->TextLength();
  } else {
    // For non-text nodes, the given offsets should be sufficient.
    beginOffset = aRange.StartOffset();
    endOffset = aRange.EndOffset();
  }

  // clear the selected bit from the removed range's frames
  RefPtr<nsPresContext> presContext = GetPresContext();
  SelectFrames(presContext, aRange, false);

  // add back the selected bit for each range touching our nodes
  nsTArray<AbstractRange*> affectedRanges;
  rv = GetAbstractRangesForIntervalArray(beginNode, beginOffset, endNode,
                                         endOffset, true, &affectedRanges);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }
  for (uint32_t i = 0; i < affectedRanges.Length(); i++) {
    MOZ_ASSERT(affectedRanges[i]);
    SelectFrames(presContext, *affectedRanges[i], true);
  }

  if (&aRange == mAnchorFocusRange) {
    const size_t rangeCount = mStyledRanges.Length();
    if (rangeCount) {
      SetAnchorFocusRange(rangeCount - 1);
    } else {
      RemoveAnchorFocusRange();
    }

    // When the selection is user-created it makes sense to scroll the range
    // into view. The spell-check selection, however, is created and destroyed
    // in the background. We don't want to scroll in this case or the view
    // might appear to be moving randomly (bug 337871).
    if (mSelectionType != SelectionType::eSpellCheck && rangeCount) {
      ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION);
    }
  }

  if (!mFrameSelection) return;  // nothing to do

  RefPtr<Selection> kungFuDeathGrip{this};
  // Be aware, this instance may be destroyed after this call.
  NotifySelectionListeners();
}

// static
bool Selection::IsValidNodeAndOffsetForBoundary(const nsINode& aContainer,
                                                uint32_t aOffset,
                                                ErrorResult& aRv) {
  if (MOZ_UNLIKELY(aContainer.NodeType() == nsINode::DOCUMENT_TYPE_NODE)) {
    aRv.ThrowInvalidNodeTypeError(kNoDocumentTypeNodeError);
    return false;
  }
  if (MOZ_UNLIKELY(aOffset > aContainer.Length())) {
    aRv.ThrowIndexSizeError(kIndexSizeError);
    return false;
  }
  return true;
}

/*
 * Collapse sets the whole selection to be one point.
 */
void Selection::CollapseJS(nsINode* aContainer, uint32_t aOffset,
                           ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aContainer", aContainer, "aOffset",
                    aOffset);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  if (!aContainer) {
    RemoveAllRangesInternal(aRv);
    return;
  }
  if (MOZ_UNLIKELY(
          !IsValidNodeAndOffsetForBoundary(*aContainer, aOffset, aRv))) {
    return;
  }
  CollapseInternal(InLimiter::eNo, RawRangeBoundary(aContainer, aOffset), aRv);
}

void Selection::CollapseInLimiter(const RawRangeBoundary& aPoint,
                                  ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aPoint", aPoint);
    LogStackForSelectionAPI();
  }
  if (!aPoint.IsSetAndValid()) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }
  CollapseInternal(InLimiter::eYes, aPoint, aRv);
}

void Selection::CollapseInternal(InLimiter aInLimiter,
                                 const RawRangeBoundary& aPoint,
                                 ErrorResult& aRv) {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);  // Can't do selection
    return;
  }

  if (!HasSameRootOrSameComposedDoc(*aPoint.GetContainer())) {
    // Return with no error
    return;
  }

  RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
  frameSelection->InvalidateDesiredCaretPos();
  if (aInLimiter == InLimiter::eYes &&
      !frameSelection->NodeIsInLimiters(aPoint.GetContainer())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  nsresult result;

  RefPtr<nsPresContext> presContext = GetPresContext();
  if (!presContext ||
      presContext->Document() != aPoint.GetContainer()->OwnerDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<nsRange> range = nsRange::Create(aPoint.GetContainer());
  result = range->CollapseTo(aPoint);
  if (NS_FAILED(result)) {
    aRv.Throw(result);
    return;
  }

  // Register before Clear() drops the current ranges: if they share the new
  // range's closest common inclusive ancestor, its range list never becomes
  // empty, so neither UnmarkDescendants() nor the following MarkDescendants()
  // walks its flattened subtree.
  result = range->RegisterSelection(*this);
  if (NS_FAILED(result)) {
    aRv.Throw(result);
    return;
  }

  // Delete all of the current ranges
  Clear(presContext);

  // Turn off signal for table selection
  frameSelection->ClearTableCellSelection();

  // Hack to display the caret on the right line (bug 1237236).
  frameSelection->SetHint(ComputeCaretAssociationHint(
      frameSelection->GetHint(), frameSelection->GetCaretBidiLevel(), aPoint));

#ifdef DEBUG_SELECTION
  nsCOMPtr<nsIContent> content = do_QueryInterface(aPoint.GetContainer());
  nsCOMPtr<Document> doc = do_QueryInterface(aPoint.GetContainer());
  printf("Sel. Collapse to %p %s %d\n", container.get(),
         content ? nsAtomCString(content->NodeInfo()->NameAtom()).get()
                 : (doc ? "DOCUMENT" : "???"),
         aPoint.Offset());
#endif

  Maybe<size_t> maybeRangeIndex;
  result = AddRangesForSelectableNodes(range, &maybeRangeIndex,
                                       DispatchSelectstartEvent::Maybe);
  if (NS_FAILED(result)) {
    range->UnregisterSelection(*this);
    aRv.Throw(result);
    return;
  }
  if (maybeRangeIndex.isNothing()) {
    // Clear() left no ranges, so range would have taken index 0 had it been
    // added.  AddRangesForUserSelectableNodes() adds nothing when no part of it
    // is selectable, which would leave the registration above backed by no
    // range of this selection.
    range->UnregisterSelection(*this);
  }
  SetAnchorFocusRange(0);
  SelectFrames(presContext, *range, true);

  RefPtr<Selection> kungFuDeathGrip{this};
  // Be aware, this instance may be destroyed after this call.
  NotifySelectionListeners();
}

/*
 * Sets the whole selection to be one point
 * at the start of the current selection
 */
void Selection::CollapseToStartJS(ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  CollapseToStart(aRv);
}

void Selection::CollapseToStart(ErrorResult& aRv) {
  if (!mCalledByJS && NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  if (RangeCount() == 0) {
    aRv.ThrowInvalidStateError(kNoRangeExistsError);
    return;
  }

  // Get the first range
  const RefPtr<AbstractRange> firstRange = mStyledRanges.GetAbstractRangeAt(0);
  if (!firstRange) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  CollapseToStartOf(*firstRange, aRv);
}

void Selection::CollapseToStartOf(const AbstractRange& aRange,
                                  ErrorResult& aRv) {
  if (mFrameSelection) {
    mFrameSelection->AddChangeReasons(
        nsISelectionListener::COLLAPSETOSTART_REASON);
  }
  nsINode* container = aRange.GetStartContainer();
  if (!container) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  const uint32_t offset = aRange.StartOffset();
  if (MOZ_UNLIKELY(!IsValidNodeAndOffsetForBoundary(*container, offset, aRv))) {
    return;
  }
  CollapseInternal(InLimiter::eNo, RawRangeBoundary(container, offset), aRv);
}

/*
 * Sets the whole selection to be one point
 * at the end of the current selection
 */
void Selection::CollapseToEndJS(ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  CollapseToEnd(aRv);
}

void Selection::CollapseToEnd(ErrorResult& aRv) {
  if (!mCalledByJS && NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  uint32_t cnt = RangeCount();
  if (cnt == 0) {
    aRv.ThrowInvalidStateError(kNoRangeExistsError);
    return;
  }

  // Get the last range
  const AbstractRange* lastRange = mStyledRanges.GetAbstractRangeAt(cnt - 1);
  if (!lastRange) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (mFrameSelection) {
    mFrameSelection->AddChangeReasons(
        nsISelectionListener::COLLAPSETOEND_REASON);
  }
  nsINode* container = lastRange->GetEndContainer();
  if (!container) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  const uint32_t offset = lastRange->EndOffset();
  if (MOZ_UNLIKELY(!IsValidNodeAndOffsetForBoundary(*container, offset, aRv))) {
    return;
  }
  CollapseInternal(InLimiter::eNo, RawRangeBoundary(container, offset), aRv);
}

void Selection::GetType(nsAString& aOutType) const {
  if (!RangeCount()) {
    aOutType.AssignLiteral("None");
  } else if (IsCollapsed()) {
    aOutType.AssignLiteral("Caret");
  } else {
    aOutType.AssignLiteral("Range");
  }
}

nsRange* Selection::GetRangeAt(uint32_t aIndex, ErrorResult& aRv) {
  nsRange* range = GetRangeAt(aIndex);
  if (!range) {
    aRv.ThrowIndexSizeError(nsPrintfCString("%u is out of range", aIndex));
    return nullptr;
  }

  return range;
}

AbstractRange* Selection::GetAbstractRangeAt(uint32_t aIndex) const {
  return aIndex < mStyledRanges.Length()
             ? mStyledRanges.GetAbstractRangeAt(aIndex)
             : nullptr;
}

// https://www.w3.org/TR/selection-api/#dom-selection-direction
void Selection::GetDirection(nsAString& aDirection) const {
  if (mStyledRanges.mRanges.IsEmpty() ||
      (mFrameSelection && (mFrameSelection->IsDoubleClickSelection() ||
                           mFrameSelection->IsTripleClickSelection()))) {
    // Empty range and double/triple clicks result a directionless selection.
    aDirection.AssignLiteral("none");
  } else if (mDirection == nsDirection::eDirNext) {
    // This is the default direction. It could be that the direction
    // is really "forward", or the direction is "none" if the selection
    // is collapsed.
    if (AreNormalAndCrossShadowBoundaryRangesCollapsed()) {
      aDirection.AssignLiteral("none");
      return;
    }
    aDirection.AssignLiteral("forward");
  } else {
    MOZ_ASSERT(!AreNormalAndCrossShadowBoundaryRangesCollapsed());
    aDirection.AssignLiteral("backward");
  }
}

nsRange* Selection::GetRangeAt(uint32_t aIndex) const {
  // This method per IDL spec returns a dynamic range.
  // Therefore, it must be ensured that it is only called
  // for a selection which contains dynamic ranges exclusively.
  // Highlight Selections are allowed to contain StaticRanges,
  // therefore this method must not be called.
  MOZ_ASSERT(mSelectionType != SelectionType::eHighlight);
  AbstractRange* abstractRange = GetAbstractRangeAt(aIndex);
  if (!abstractRange) {
    return nullptr;
  }
  return abstractRange->AsDynamicRange();
}

nsresult Selection::SetAnchorFocusToRange(nsRange* aRange) {
  NS_ENSURE_STATE(mAnchorFocusRange);

  const DispatchSelectstartEvent dispatchSelectstartEvent =
      IsCollapsed() ? DispatchSelectstartEvent::Maybe
                    : DispatchSelectstartEvent::No;

  nsresult rv =
      mStyledRanges.RemoveRangeAndUnregisterSelection(*mAnchorFocusRange);
  if (NS_FAILED(rv)) {
    return rv;
  }

  Maybe<size_t> maybeOutIndex;
  rv = AddRangesForSelectableNodes(aRange, &maybeOutIndex,
                                   dispatchSelectstartEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (maybeOutIndex.isSome()) {
    SetAnchorFocusRange(*maybeOutIndex);
  } else {
    RemoveAnchorFocusRange();
  }

  return NS_OK;
}

void Selection::ReplaceAnchorFocusRange(nsRange* aRange) {
  NS_ENSURE_TRUE_VOID(mAnchorFocusRange);
  RefPtr<nsPresContext> presContext = GetPresContext();
  if (presContext) {
    SelectFrames(presContext, *mAnchorFocusRange, false);
    SetAnchorFocusToRange(aRange);
    SelectFrames(presContext, *mAnchorFocusRange, true);
  }
}

void Selection::AdjustAnchorFocusForMultiRange(nsDirection aDirection) {
  if (aDirection == mDirection) {
    return;
  }
  SetDirection(aDirection);

  if (RangeCount() <= 1) {
    return;
  }

  nsRange* firstRange = GetRangeAt(0);
  nsRange* lastRange = GetRangeAt(RangeCount() - 1);

  if (mDirection == eDirPrevious) {
    firstRange->SetIsGenerated(false);
    lastRange->SetIsGenerated(true);
    SetAnchorFocusRange(0);
  } else {  // aDir == eDirNext
    firstRange->SetIsGenerated(true);
    lastRange->SetIsGenerated(false);
    SetAnchorFocusRange(RangeCount() - 1);
  }
}

/*
 * Extend extends the selection away from the anchor.
 * We don't need to know the direction, because we always change the focus.
 */
void Selection::ExtendJS(nsINode& aContainer, uint32_t aOffset,
                         ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aContainer", &aContainer, "aOffset",
                    aOffset);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  ExtendInternal(aContainer, aOffset, aRv);
}

nsresult Selection::Extend(nsINode* aContainer, uint32_t aOffset) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aContainer", aContainer, "aOffset",
                    aOffset);
    LogStackForSelectionAPI();
  }

  if (!aContainer) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult result;
  ExtendInternal(*aContainer, aOffset, result);
  return result.StealNSResult();
}

void Selection::ExtendInternal(nsINode& aContainer, uint32_t aOffset,
                               ErrorResult& aRv) {
  /*
    Notes which might come in handy for extend:

    We can tell the direction of the selection by asking for the anchors
    selection if the begin is less than the end then we know the selection is to
    the "right", else it is a backwards selection. Notation: a = anchor, 1 = old
    cursor, 2 = new cursor.

      if (a <= 1 && 1 <=2)    a,1,2  or (a1,2)
      if (a < 2 && 1 > 2)     a,2,1
      if (1 < a && a <2)      1,a,2
      if (a > 2 && 2 >1)      1,2,a
      if (2 < a && a <1)      2,a,1
      if (a > 1 && 1 >2)      2,1,a
    then execute
    a  1  2 select from 1 to 2
    a  2  1 deselect from 2 to 1
    1  a  2 deselect from 1 to a select from a to 2
    1  2  a deselect from 1 to 2
    2  1  a = continue selection from 2 to 1
  */

  // First, find the range containing the old focus point:
  if (!mAnchorFocusRange) {
    aRv.ThrowInvalidStateError(kNoRangeExistsError);
    return;
  }

  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);  // Can't do selection
    return;
  }

  if (!HasSameRootOrSameComposedDoc(aContainer)) {
    // Return with no error
    return;
  }

  if (!mFrameSelection->NodeIsInLimiters(&aContainer)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (aContainer.GetFrameSelection() != mFrameSelection) {
    NS_ASSERTION(
        false,
        fmt::format("mFrameSelection is {} which is expected as "
                    "aContainer.GetFrameSelection() ({})",
                    mFrameSelection, RefPtr{aContainer.GetFrameSelection()})
            .c_str());
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<nsPresContext> presContext = GetPresContext();
  if (!presContext || presContext->Document() != aContainer.OwnerDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (MOZ_UNLIKELY(
          !IsValidNodeAndOffsetForBoundary(aContainer, aOffset, aRv))) {
    return;
  }

  DebugOnly<nsDirection> oldDirection = GetDirection();
  const RawRangeBoundary newFocusRefInTreeKindDOM(
      &aContainer, aOffset, RangeBoundarySetBy::Offset, TreeKind::DOM);

  RefPtr<nsRange> range = mAnchorFocusRange->CloneRange();

  const RawRangeBoundary startRefInTreeKindDOM =
      range->MayCrossShadowBoundaryStartRef()
          .AsRaw()
          .AsRangeBoundaryInDOMTree();
  const RawRangeBoundary endRefInTreeKindDOM =
      range->MayCrossShadowBoundaryEndRef().AsRaw().AsRangeBoundaryInDOMTree();
  const RawRangeBoundary& anchorRefInTreeKindDOM =
      GetDirection() == nsDirection::eDirNext ? startRefInTreeKindDOM
                                              : endRefInTreeKindDOM;
  const RawRangeBoundary& focusRefInTreeKindDOM =
      GetDirection() == nsDirection::eDirNext ? endRefInTreeKindDOM
                                              : startRefInTreeKindDOM;

  // Selection API handles only the DOM points in the non-flattened tree.
  // Therefore, even if the cross-shadow-selection is enabled, we should compare
  // the DOM points within the shadow including DOM because some nodes may be
  // slotted to another place and that may change the order.
  const Maybe<int32_t> anchorOldFocusOrder =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          anchorRefInTreeKindDOM, focusRefInTreeKindDOM);
  const Maybe<int32_t> oldFocusNewFocusOrder =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          focusRefInTreeKindDOM, newFocusRefInTreeKindDOM);
  const Maybe<int32_t> anchorNewFocusOrder =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          anchorRefInTreeKindDOM, newFocusRefInTreeKindDOM);

  // If the points are disconnected, or if the range would cross a shadow
  // boundary and this is being called from JS, the range will be collapsed
  // below, resulting in a range that selects nothing.
  if (!anchorOldFocusOrder || !oldFocusNewFocusOrder || !anchorNewFocusOrder ||
      // https://w3c.github.io/selection-api/#dom-selection-extend
      // 5. If node's root is not the same as the this's range's root, set the
      //    start newRange's start and end to newFocus.
      (mCalledByJS && mAnchorFocusRange->GetRoot() !=
                          RangeUtils::ComputeRootNode(&aContainer))) {
    // Repaint the current range with the selection removed.
    SelectFrames(presContext, *range, false);

    nsresult rv = range->CollapseTo(&aContainer, aOffset);
    if (NS_FAILED(rv)) {
      aRv.Throw(rv);
      return;
    }

    rv = SetAnchorFocusToRange(range);
    if (NS_FAILED(rv)) {
      aRv.Throw(rv);
      return;
    }
  } else {
    // (the collapsed range < new focus) ||
    // (anchor <= focus && focus < new focus)
    // I.e., extending the end boundary forward. (a1,2  a,1,2)
    if ((*anchorOldFocusOrder == 0 && *anchorNewFocusOrder < 0) ||
        (*anchorOldFocusOrder <= 0 && *oldFocusNewFocusOrder < 0)) {
      // Select from the anchor which is not after focus to the new focus.
      range->SetEnd(newFocusRefInTreeKindDOM, aRv,
                    AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      SetDirection(eDirNext);
      const RefPtr<nsRange> diffRange =
          nsRange::Create(focusRefInTreeKindDOM, newFocusRefInTreeKindDOM, aRv,
                          AllowRangeCrossShadowBoundary::Yes);
      if (NS_WARN_IF(aRv.Failed())) {
        return;
      }
      SelectFrames(presContext, *diffRange, true);
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
    }
    // anchor == focus && new focus < the collapsed range
    // I.e., extending the collapsed range backward (2, a1)
    else if (*anchorOldFocusOrder == 0 && *anchorNewFocusOrder > 0) {
      SetDirection(eDirPrevious);
      range->SetStart(newFocusRefInTreeKindDOM, aRv,
                      AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      SelectFrames(presContext, *range, true);
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
    }
    // anchor <= new focus && new focus <= old focus
    // I.e., shrink the end boundary into the range.
    // (a,2,1 or a2,1 or a,21 or a21)
    else if (*anchorNewFocusOrder <= 0 && *oldFocusNewFocusOrder >= 0) {
      // deselect from 2 to 1
      const RefPtr<nsRange> diffRange =
          nsRange::Create(newFocusRefInTreeKindDOM, focusRefInTreeKindDOM, aRv,
                          AllowRangeCrossShadowBoundary::Yes);
      if (NS_WARN_IF(aRv.Failed())) {
        return;
      }

      range->SetEnd(newFocusRefInTreeKindDOM, aRv,
                    AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
      SelectFrames(presContext, *diffRange, false);
      MOZ_ASSERT(
          diffRange->MayCrossShadowBoundaryStartRef()
                  .AsRangeBoundaryInDOMTree() ==
              range->MayCrossShadowBoundaryEndRef().AsRangeBoundaryInDOMTree(),
          "Do we need to deselect the frames in this range??");
    }
    // focus <= anchor && anchor <= new focus
    // I.e., the range is reversed or collapsed and the new focus is after the
    // end boundary. So, collapse the range and extend the range forward.
    // (1,a,2 or 1a,2 or 1,a2 or 1a2)
    else if (*anchorOldFocusOrder >= 0 && *anchorNewFocusOrder <= 0) {
      // Collapse to end if the range was not collapsed
      RefPtr<nsRange> oldNonCollapsedRange;
      if (*anchorOldFocusOrder) {
        oldNonCollapsedRange = range->CloneRange();
        range->Collapse(false);
      }
      SetDirection(eDirNext);
      // Then, extend the range forward.
      range->SetEnd(newFocusRefInTreeKindDOM, aRv,
                    AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
      // If the range was not collapsed, we need to deselect frames.
      if (oldNonCollapsedRange) {
        // deselect from 1 to a
        SelectFrames(presContext, *oldNonCollapsedRange, false);
      }
      // select from a to 2
      SelectFrames(presContext, *range, true);
    }
    // focus <= new focus && new focus <= anchor
    // I.e., the range is reversed and the new focus is in the range. Shrink
    // the start boundary forward.
    // (1,2,a or 12,a or 1,2a or 12a)
    else if (*oldFocusNewFocusOrder <= 0 && *anchorNewFocusOrder >= 0) {
      RefPtr<nsRange> diffRange;
      if (focusRefInTreeKindDOM != newFocusRefInTreeKindDOM) {
        // deselect from 1 to 2
        diffRange =
            nsRange::Create(focusRefInTreeKindDOM, newFocusRefInTreeKindDOM,
                            aRv, AllowRangeCrossShadowBoundary::Yes);
        if (NS_WARN_IF(aRv.Failed())) {
          return;
        }

        SetDirection(eDirPrevious);
        range->SetStart(newFocusRefInTreeKindDOM, aRv,
                        AllowRangeCrossShadowBoundary::Yes);
        if (aRv.Failed()) {
          return;
        }
      }

      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }

      if (diffRange) {
        SelectFrames(presContext, *diffRange, false);
      }
      MOZ_ASSERT(!diffRange || range->MayCrossShadowBoundaryStartRef()
                                       .AsRangeBoundaryInDOMTree() ==
                                   diffRange->MayCrossShadowBoundaryEndRef()
                                       .AsRangeBoundaryInDOMTree(),
                 "Do we need to deselect the frames in this range??");
    }
    // new focus <= anchor && anchor <= focus
    // I.e., the new focus is before the range. Collapse the range to start and
    // extend the range backward. (2,a,1 or 2a,1 or 2,a1 or 2a1)
    else if (*anchorNewFocusOrder >= 0 && *anchorOldFocusOrder <= 0) {
      // Collapse the range to start.
      RefPtr<nsRange> oldNonCollapsedRange;
      if (*anchorOldFocusOrder) {
        oldNonCollapsedRange = range->CloneRange();
        range->Collapse(true);
      }
      SetDirection(eDirPrevious);
      // Then, extend the range backward.
      range->SetStart(newFocusRefInTreeKindDOM, aRv,
                      AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
      // If the range was not collapsed, we need to deselect frames.
      if (oldNonCollapsedRange) {
        // deselect from a to 1
        SelectFrames(presContext, *oldNonCollapsedRange, false);
      }
      // select from 2 to a
      SelectFrames(presContext, *range, true);
    }
    // new focus <= focus && focus <= anchor
    // I.e., the range is reversed and extend the range backward.
    // (2,1,a or 21,a or 2,1a or 21a)
    else if (*oldFocusNewFocusOrder >= 0 && *anchorOldFocusOrder >= 0) {
      // select from 2 to 1
      range->SetStart(newFocusRefInTreeKindDOM, aRv,
                      AllowRangeCrossShadowBoundary::Yes);
      if (MOZ_UNLIKELY(aRv.Failed())) {
        return;
      }
      SetDirection(eDirPrevious);
      const RefPtr<nsRange> diffRange =
          nsRange::Create(newFocusRefInTreeKindDOM, focusRefInTreeKindDOM, aRv,
                          AllowRangeCrossShadowBoundary::Yes);
      if (NS_WARN_IF(aRv.Failed())) {
        return;
      }

      SelectFrames(presContext, *diffRange, true);
      nsresult rv = SetAnchorFocusToRange(range);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
    }
  }

  if (mStyledRanges.Length() > 1) {
    SelectFramesInAllRanges(presContext);
  }

  DEBUG_OUT_RANGE(range);
#ifdef DEBUG_SELECTION
  if (GetDirection() != oldDirection) {
    printf("    direction changed to %s\n",
           GetDirection() == eDirNext ? "eDirNext" : "eDirPrevious");
  }
  nsCOMPtr<nsIContent> content = do_QueryInterface(&aContainer);
  printf("Sel. Extend to %p %s %d\n", content.get(),
         nsAtomCString(content->NodeInfo()->NameAtom()).get(), aOffset);
#endif

  RefPtr<Selection> kungFuDeathGrip{this};
  // Be aware, this instance may be destroyed after this call.
  NotifySelectionListeners();
}

void Selection::SelectAllChildrenJS(nsINode& aNode, ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aNode", &aNode);
    LogStackForSelectionAPI();
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  SelectAllChildren(aNode, aRv);
  if (StaticPrefs::dom_selection_mimic_chrome_tostring_enabled() &&
      !aRv.Failed()) {
    if (auto* presShell = GetPresShell()) {
      presShell->UpdateLastSelectionForToString(mFrameSelection);
    }
  }
}

void Selection::SelectAllChildren(nsINode& aNode, ErrorResult& aRv) {
  if (!mCalledByJS && NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aNode", &aNode);
    LogStackForSelectionAPI();
  }

  if (aNode.NodeType() == nsINode::DOCUMENT_TYPE_NODE) {
    aRv.ThrowInvalidNodeTypeError(kNoDocumentTypeNodeError);
    return;
  }

  if (!HasSameRootOrSameComposedDoc(aNode)) {
    // Return with no error
    return;
  }

  if (mFrameSelection) {
    mFrameSelection->AddChangeReasons(nsISelectionListener::SELECTALL_REASON);
  }

  // Chrome moves focus when aNode is outside of active editing host.
  // So, we don't need to respect the limiter with this method.
  const RawRangeBoundary startOfNode = RawRangeBoundary::StartOfParent(aNode);
  SetStartAndEndInternal(InLimiter::eNo, startOfNode,
                         aNode.IsContainerNode()
                             ? RawRangeBoundary::EndOfParent(aNode)
                             : startOfNode,
                         eDirNext, aRv);
}

bool Selection::ContainsNode(nsINode& aNode, bool aAllowPartial,
                             ErrorResult& aRv) {
  nsresult rv;
  if (mStyledRanges.Length() == 0) {
    return false;
  }

  // If aNode is a non-flattened node in the flattened tree for selection, i.e.,
  // an inclusive descendant is an unassigned child of a shadow host or a
  // fallback content node of a <slot> in a shadow.
  if (aNode.GetClosestFlatTreeAncestorElementForNonFlatTreeNode<
          TreeKind::FlatForSelection>()) {
    return false;
  }

  // XXXbz this duplicates the GetNodeLength code in nsRange.cpp
  uint32_t nodeLength;
  auto* nodeAsCharData = CharacterData::FromNode(aNode);
  if (nodeAsCharData) {
    nodeLength = nodeAsCharData->TextLength();
  } else {
    nodeLength = aNode.GetChildCount();
  }

  nsTArray<AbstractRange*> overlappingRanges;
  rv = GetAbstractRangesForIntervalArray(&aNode, 0, &aNode, nodeLength, false,
                                         &overlappingRanges);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return false;
  }
  if (overlappingRanges.Length() == 0) return false;  // no ranges overlap

  // if the caller said partial intersections are OK, we're done
  if (aAllowPartial) {
    return true;
  }

  // text nodes always count as inside
  if (nodeAsCharData) {
    return true;
  }

  // The caller wants to know if the node is entirely within the given range,
  // so we have to check all intersecting ranges.
  for (uint32_t i = 0; i < overlappingRanges.Length(); i++) {
    bool nodeStartsBeforeRange, nodeEndsAfterRange;
    if (NS_SUCCEEDED(RangeUtils::CompareNodeToRange(
            &aNode, overlappingRanges[i], &nodeStartsBeforeRange,
            &nodeEndsAfterRange))) {
      if (!nodeStartsBeforeRange && !nodeEndsAfterRange) {
        return true;
      }
    }
  }
  return false;
}

class PointInRectChecker : public mozilla::RectCallback {
 public:
  explicit PointInRectChecker(const nsPoint& aPoint)
      : mPoint(aPoint), mMatchFound(false) {}

  void AddRect(const nsRect& aRect) override {
    mMatchFound = mMatchFound || aRect.Contains(mPoint);
  }

  bool MatchFound() { return mMatchFound; }

 private:
  nsPoint mPoint;
  bool mMatchFound;
};

bool Selection::ContainsPoint(const nsPoint& aPoint) {
  if (IsCollapsed()) {
    return false;
  }
  PointInRectChecker checker(aPoint);
  const uint32_t rangeCount = RangeCount();
  for (const uint32_t i : IntegerRange(rangeCount)) {
    MOZ_ASSERT(RangeCount() == rangeCount);
    nsRange* range = GetRangeAt(i);
    MOZ_ASSERT(range);
    nsRange::CollectClientRectsAndText(
        &checker, nullptr, range, range->GetStartContainer(),
        range->StartOffset(), range->GetEndContainer(), range->EndOffset(),
        true, false);
    if (checker.MatchFound()) {
      return true;
    }
  }
  return false;
}

void Selection::MaybeNotifyAccessibleCaretEventHub(PresShell* aPresShell) {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  if (!mAccessibleCaretEventHub && aPresShell) {
    mAccessibleCaretEventHub = aPresShell->GetAccessibleCaretEventHub();
  }
}

void Selection::StopNotifyingAccessibleCaretEventHub() {
  MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

  mAccessibleCaretEventHub = nullptr;
}

nsPresContext* Selection::GetPresContext() const {
  PresShell* presShell = GetPresShell();
  return presShell ? presShell->GetPresContext() : nullptr;
}

PresShell* Selection::GetPresShell() const {
  if (!mFrameSelection) {
    return nullptr;  // nothing to do
  }
  return mFrameSelection->GetPresShell();
}

Document* Selection::GetDocument() const {
  PresShell* presShell = GetPresShell();
  return presShell ? presShell->GetDocument() : nullptr;
}

nsIFrame* Selection::GetSelectionAnchorGeometry(SelectionRegion aRegion,
                                                nsRect* aRect) {
  if (!mFrameSelection) return nullptr;  // nothing to do

  NS_ENSURE_TRUE(aRect, nullptr);

  aRect->SetRect(0, 0, 0, 0);

  switch (aRegion) {
    case nsISelectionController::SELECTION_ANCHOR_REGION:
    case nsISelectionController::SELECTION_FOCUS_REGION:
      return GetSelectionEndPointGeometry(aRegion, aRect);
    case nsISelectionController::SELECTION_WHOLE_SELECTION:
      break;
    default:
      return nullptr;
  }

  NS_ASSERTION(aRegion == nsISelectionController::SELECTION_WHOLE_SELECTION,
               "should only be SELECTION_WHOLE_SELECTION here");

  nsRect anchorRect;
  nsIFrame* anchorFrame = GetSelectionEndPointGeometry(
      nsISelectionController::SELECTION_ANCHOR_REGION, &anchorRect);
  if (!anchorFrame) return nullptr;

  nsRect focusRect;
  nsIFrame* focusFrame = GetSelectionEndPointGeometry(
      nsISelectionController::SELECTION_FOCUS_REGION, &focusRect);
  if (!focusFrame) return nullptr;

  NS_ASSERTION(anchorFrame->PresContext() == focusFrame->PresContext(),
               "points of selection in different documents?");
  // make focusRect relative to anchorFrame
  focusRect += focusFrame->GetOffsetTo(anchorFrame);

  *aRect = anchorRect.UnionEdges(focusRect);
  return anchorFrame;
}

nsIFrame* Selection::GetSelectionEndPointGeometry(SelectionRegion aRegion,
                                                  nsRect* aRect) {
  if (!mFrameSelection) return nullptr;  // nothing to do

  NS_ENSURE_TRUE(aRect, nullptr);

  aRect->SetRect(0, 0, 0, 0);

  nsINode* node = nullptr;
  uint32_t nodeOffset = 0;

  switch (aRegion) {
    case nsISelectionController::SELECTION_ANCHOR_REGION:
      node = GetAnchorNode();
      nodeOffset = AnchorOffset();
      break;
    case nsISelectionController::SELECTION_FOCUS_REGION:
      node = GetFocusNode();
      nodeOffset = FocusOffset();
      break;
    default:
      return nullptr;
  }

  if (!node) return nullptr;

  nsCOMPtr<nsIContent> content = do_QueryInterface(node);
  NS_ENSURE_TRUE(content.get(), nullptr);
  FrameAndOffset frameAndOffset = SelectionMovementUtils::GetFrameForNodeOffset(
      content, nodeOffset, mFrameSelection->GetHint());
  if (!frameAndOffset) {
    return nullptr;
  }

  SelectionMovementUtils::AdjustFrameForLineStart(
      frameAndOffset.mFrame, frameAndOffset.mOffsetInFrameContent);

  // Figure out what node type we have, then get the
  // appropriate rect for its nodeOffset.
  bool isText = node->IsText();

  nsPoint pt(0, 0);
  if (isText) {
    nsIFrame* childFrame = nullptr;
    int32_t frameOffset = 0;
    nsresult rv = frameAndOffset->GetChildFrameContainingOffset(
        // FIXME: nodeOffset is offset in content (same as node) but
        // frameAndOffset.mFrame may be a frame for its descendant.  Therefore,
        // frameAndOffset.mOffsetInFrameContent should be used here.
        nodeOffset, mFrameSelection->GetHint() == CaretAssociationHint::After,
        &frameOffset, &childFrame);
    if (NS_FAILED(rv)) return nullptr;
    if (!childFrame) return nullptr;

    frameAndOffset.mFrame = childFrame;

    // Get the coordinates of the offset into the text frame.
    rv = GetCachedFrameOffset(
        frameAndOffset.mFrame,
        static_cast<int32_t>(frameAndOffset.mOffsetInFrameContent), pt);
    if (NS_FAILED(rv)) return nullptr;
  }

  // Return the rect relative to the frame, with zero inline-size.  The
  // inline-position is either 'pt' (if we're a text node) or otherwise just
  // the physical "end" edge of the frame (which we express as the frame's own
  // width or height, since the returned position is relative to the frame).
  // The block position and size are set so as to fill the frame in that axis.
  // (i.e. block-position of 0, and block-size matching the frame's own block
  // size).
  const WritingMode wm = frameAndOffset->GetWritingMode();
  // Helper to determine the inline-axis position for the aRect outparam.
  auto GetInlinePosition = [&]() {
    if (isText) {
      return wm.IsVertical() ? pt.y : pt.x;
    }
    // Return the frame's physical end edge of its inline axis, relative to the
    // frame.  That's just its height or width.
    // TODO(dholbert): This seems to work, but perhaps we really want the
    // inline-end edge (rather than physical end of inline axis)? (i.e. if we
    // have direction:rtl, maybe this code would want to return 0 instead of
    // height/width?)
    return frameAndOffset->ISize(wm);
  };

  // Set the inline position and block-size. Leave inline size and block
  // position set to 0, as discussed above.
  if (wm.IsVertical()) {
    aRect->y = GetInlinePosition();
    aRect->SetWidth(frameAndOffset->BSize(wm));
  } else {
    aRect->x = GetInlinePosition();
    aRect->SetHeight(frameAndOffset->BSize(wm));
  }

  return frameAndOffset;
}

NS_IMETHODIMP
Selection::ScrollSelectionIntoViewEvent::Run() {
  if (!mSelection) {
    // event revoked
    return NS_OK;
  }

  const RefPtr<Selection> selection{mSelection};
  selection->mScrollEvent.Forget();
  selection->ScrollIntoView(mRegion, mVerticalScroll, mHorizontalScroll, mFlags,
                            SelectionScrollMode::SyncFlush);
  return NS_OK;
}

nsresult Selection::PostScrollSelectionIntoViewEvent(
    SelectionRegion aRegion, ScrollFlags aFlags, AxisScrollParams aVertical,
    AxisScrollParams aHorizontal) {
  // If we've already posted an event, revoke it and place a new one at the
  // end of the queue to make sure that any new pending reflow events are
  // processed before we scroll. This will insure that we scroll to the
  // correct place on screen.
  mScrollEvent.Revoke();
  nsPresContext* presContext = GetPresContext();
  NS_ENSURE_STATE(presContext);
  nsRefreshDriver* refreshDriver = presContext->RefreshDriver();
  NS_ENSURE_STATE(refreshDriver);

  mScrollEvent = new ScrollSelectionIntoViewEvent(this, aRegion, aVertical,
                                                  aHorizontal, aFlags);
  refreshDriver->AddEarlyRunner(mScrollEvent.get());
  return NS_OK;
}

nsresult Selection::ScrollIntoView(SelectionRegion aRegion,
                                   AxisScrollParams aVertical,
                                   AxisScrollParams aHorizontal,
                                   ScrollFlags aScrollFlags,
                                   SelectionScrollMode aMode) {
  if (!mFrameSelection) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<PresShell> presShell = mFrameSelection->GetPresShell();
  if (!presShell || !presShell->GetDocument()) {
    return NS_OK;
  }

  if (mFrameSelection->IsBatching()) {
    return NS_OK;
  }

  if (aMode == SelectionScrollMode::Async) {
    return PostScrollSelectionIntoViewEvent(aRegion, aScrollFlags, aVertical,
                                            aHorizontal);
  }

  MOZ_ASSERT(aMode == SelectionScrollMode::SyncFlush ||
             aMode == SelectionScrollMode::SyncNoFlush);

  // From this point on, the presShell may get destroyed by the calls below, so
  // hold on to it using a strong reference to ensure the safety of the
  // accesses to frame pointers in the callees.
  RefPtr<PresShell> kungFuDeathGrip(presShell);

  // Now that text frame character offsets are always valid (though not
  // necessarily correct), the worst that will happen if we don't flush here
  // is that some callers might scroll to the wrong place.  Those should
  // either manually flush if they're in a safe position for it or use the
  // async version of this method.
  if (aMode == SelectionScrollMode::SyncFlush) {
    presShell->GetDocument()->FlushPendingNotifications(FlushType::Layout);

    // Reget the presshell, since it might have been Destroy'ed.
    presShell = mFrameSelection ? mFrameSelection->GetPresShell() : nullptr;
    if (!presShell) {
      return NS_OK;
    }
  }

  nsRect rect;
  nsIFrame* frame = GetSelectionAnchorGeometry(aRegion, &rect);
  if (!frame) {
    return NS_ERROR_FAILURE;
  }

  presShell->ScrollFrameIntoView(frame, Some(rect), aVertical, aHorizontal,
                                 aScrollFlags);
  return NS_OK;
}

void Selection::AddSelectionListener(nsISelectionListener* aNewListener) {
  MOZ_ASSERT(aNewListener);
  mSelectionListeners.AppendElement(aNewListener);  // AddRefs
}

void Selection::RemoveSelectionListener(
    nsISelectionListener* aListenerToRemove) {
  mSelectionListeners.RemoveElement(aListenerToRemove);  // Releases
}

Element* Selection::StyledRanges::GetCommonEditingHost() const {
  Element* editingHost = nullptr;
  for (const RefPtr<AbstractRange>& range : Ranges()) {
    MOZ_ASSERT(range);
    nsINode* commonAncestorNode = range->GetClosestCommonInclusiveAncestor();
    if (!commonAncestorNode || !commonAncestorNode->IsContent()) {
      return nullptr;
    }
    nsIContent* commonAncestor = commonAncestorNode->AsContent();
    Element* foundEditingHost = commonAncestor->GetEditingHost();
    // Even when common ancestor is a non-editable element in a contenteditable
    // element, we don't need to move focus to the contenteditable element
    // because Chromium doesn't set focus to it.
    if (!foundEditingHost) {
      return nullptr;
    }
    if (!editingHost) {
      editingHost = foundEditingHost;
      continue;
    }
    if (editingHost == foundEditingHost) {
      continue;
    }
    if (foundEditingHost->IsInclusiveDescendantOf(editingHost)) {
      continue;
    }
    if (editingHost->IsInclusiveDescendantOf(foundEditingHost)) {
      editingHost = foundEditingHost;
      continue;
    }
    // editingHost and foundEditingHost are not a descendant of the other.
    // So, there is no common editing host.
    return nullptr;
  }
  return editingHost;
}

void Selection::StyledRanges::MaybeFocusCommonEditingHost(
    PresShell* aPresShell) const {
  if (!aPresShell) {
    return;
  }

  nsPresContext* presContext = aPresShell->GetPresContext();
  if (!presContext) {
    return;
  }

  Document* document = aPresShell->GetDocument();
  if (!document) {
    return;
  }

  nsPIDOMWindowOuter* window = document->GetWindow();
  // If the document is in design mode or doesn't have contenteditable
  // element, we don't need to move focus.
  if (window && !document->IsInDesignMode() &&
      nsContentUtils::GetHTMLEditor(presContext)) {
    RefPtr<Element> newEditingHost = GetCommonEditingHost();
    RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
    nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
    nsIContent* focusedContent = nsFocusManager::GetFocusedDescendant(
        window, nsFocusManager::eOnlyCurrentWindow,
        getter_AddRefs(focusedWindow));
    nsCOMPtr<Element> focusedElement = do_QueryInterface(focusedContent);
    // When all selected ranges are in an editing host, it should take focus.
    // But otherwise, we shouldn't move focus since Chromium doesn't move
    // focus but only selection range is updated.
    if (newEditingHost && newEditingHost != focusedElement) {
      MOZ_ASSERT(!newEditingHost->IsInNativeAnonymousSubtree());
      // Note that don't steal focus from focused window if the window doesn't
      // have focus.  Additionally, although when an element gets focus, we
      // usually scroll to the element, but in this case, we shouldn't do it
      // because Chrome does not do so.
      fm->SetFocus(newEditingHost, nsIFocusManager::FLAG_NOSWITCHFRAME |
                                       nsIFocusManager::FLAG_NOSCROLL);
    }
  }
}

void Selection::NotifySelectionListeners(
    bool aCalledByJS, IsFromRangeMutationObserver aIsFromRange) {
  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = aCalledByJS;
  NotifySelectionListeners();
  if (aIsFromRange == IsFromRangeMutationObserver::Yes &&
      mSelectionChangeEventDispatcher) {
    mSelectionChangeEventDispatcher->SelectionRangeObservedMutation();
  }
}

void Selection::NotifySelectionListeners() {
  if (!mFrameSelection) {
    return;  // nothing to do
  }

  MOZ_LOG_FMT(sSelectionLog, LogLevel::Debug, "{}: selection={}", __func__,
              static_cast<void*>(this));
  SelectionChangeGuard::DidChange();

  mStyledRanges.mRangesMightHaveChanged = true;

  // This flag will be set to Double or Triple if a selection by double click or
  // triple click is detected. As soon as the selection is modified, it needs to
  // be reset to NotApplicable.
  mFrameSelection->SetClickSelectionType(ClickSelectionType::NotApplicable);

  // If we're batching changes, record our batching flag and bail out, we'll be
  // called once the batch ends.
  if (mFrameSelection->IsBatching()) {
    mChangesDuringBatching = true;
    return;
  }
  // If being called at end of batching, `mFrameSelection->IsBatching()` will
  // return false. In this case, this method will only be called if
  // `mChangesDuringBatching` was true.
  // (see `nsFrameSelection::EndBatchChanges()`).
  // Since arriving here means that batching ended, the flag needs to be reset.
  mChangesDuringBatching = false;

  // Our internal code should not move focus with using this class while
  // this moves focus nor from selection listeners.
  AutoRestore<bool> calledByJSRestorer(mCalledByJS);
  mCalledByJS = false;

  // When normal selection is changed by Selection API, we need to move focus
  // if common ancestor of all ranges are in an editing host.  Note that we
  // don't need to move focus *to* the other focusable node because other
  // browsers don't do it either.
  if (mSelectionType == SelectionType::eNormal &&
      calledByJSRestorer.SavedValue()) {
    RefPtr<PresShell> presShell = GetPresShell();
    mStyledRanges.MaybeFocusCommonEditingHost(presShell);
  }

  nsCOMPtr<Document> doc;
  if (PresShell* presShell = GetPresShell()) {
    doc = presShell->GetDocument();
    presShell->ScheduleContentRelevancyUpdate(ContentRelevancyReason::Selected);
    if (mSelectionType == SelectionType::eNormal && RangeCount() && doc) {
      // Focus navigation should start from the new selection, instead
      // of the last focused element.
      doc->SetPreviouslyFocusedContent(nullptr);
      doc->SetSelectionMoreRecentThanFocus(true);
    }
  }

  RefPtr<nsFrameSelection> frameSelection = mFrameSelection;

  // We've notified all selection listeners even when some of them are removed
  // (and may be destroyed) during notifying one of them.  Therefore, we should
  // copy all listeners to the local variable first.
  const CopyableAutoTArray<nsCOMPtr<nsISelectionListener>, 5>
      selectionListeners = mSelectionListeners;

  int32_t amount = static_cast<int32_t>(frameSelection->GetCaretMoveAmount());
  int16_t reason = frameSelection->PopChangeReasons();
  if (calledByJSRestorer.SavedValue()) {
    reason |= nsISelectionListener::JS_REASON;
  }
  if (mSelectionType == SelectionType::eNormal) {
    if (mNotifyAutoCopy) {
      AutoCopyListener::OnSelectionChange(doc, *this, reason);
    }

    if (mAccessibleCaretEventHub) {
      RefPtr<AccessibleCaretEventHub> hub(mAccessibleCaretEventHub);
      hub->OnSelectionChange(doc, this, reason);
    }

    if (mSelectionChangeEventDispatcher) {
      RefPtr<SelectionChangeEventDispatcher> dispatcher(
          mSelectionChangeEventDispatcher);
      dispatcher->OnSelectionChange(doc, this, reason);
    }
  }
  for (const auto& listener : selectionListeners) {
    // MOZ_KnownLive because 'selectionListeners' is guaranteed to
    // keep it alive.
    //
    // This can go away once
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1620312 is fixed.
    MOZ_KnownLive(listener)->NotifySelectionChanged(doc, this, reason, amount);
  }
}

void Selection::StartBatchChanges(const char* aDetails) {
  if (mFrameSelection) {
    RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
    frameSelection->StartBatchChanges(aDetails);
  }
}

void Selection::EndBatchChanges(const char* aDetails, int16_t aReasons) {
  if (mFrameSelection) {
    RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
    frameSelection->EndBatchChanges(aDetails, aReasons);
  }
}

void Selection::AddSelectionChangeBlocker() { mSelectionChangeBlockerCount++; }

void Selection::RemoveSelectionChangeBlocker() {
  MOZ_ASSERT(mSelectionChangeBlockerCount > 0,
             "mSelectionChangeBlockerCount has an invalid value - "
             "maybe you have a mismatched RemoveSelectionChangeBlocker?");
  mSelectionChangeBlockerCount--;
}

bool Selection::IsBlockingSelectionChangeEvents() const {
  return mSelectionChangeBlockerCount > 0;
}

void Selection::DeleteFromDocument(ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__);
    LogStackForSelectionAPI();
  }

  if (mSelectionType != SelectionType::eNormal) {
    return;  // Nothing to do.
  }

  // If we're already collapsed, then we do nothing (bug 719503).
  if (IsCollapsed()) {
    return;
  }

  // nsRange::DeleteContents() may run script, let's store all ranges first.
  nsTArray<RefPtr<AbstractRange>> ranges{mStyledRanges.Ranges()};
  for (const auto& range : ranges) {
    // This runs only for normal selections, therefore all ranges are guaranteed
    // to be dynamic.
    MOZ_KnownLive(range)->AsDynamicRange()->DeleteContents(aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  // Collapse to the new location.
  // If we deleted one character, then we move back one element.
  // FIXME  We don't know how to do this past frame boundaries yet.
  if (AnchorOffset() > 0) {
    RefPtr<nsINode> anchor = GetAnchorNode();
    CollapseInLimiter(anchor, AnchorOffset());
  }
#ifdef DEBUG
  else {
    printf("Don't know how to set selection back past frame boundary\n");
  }
#endif
}

void Selection::Modify(const nsAString& aAlter, const nsAString& aDirection,
                       const nsAString& aGranularity) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aAlter", aAlter, "aDirection", aDirection,
                    "aGranularity", aGranularity);
    LogStackForSelectionAPI();
  }

  if (!mFrameSelection) {
    return;
  }

  if (!GetAnchorFocusRange() || !GetFocusNode()) {
    return;
  }

  if (!aAlter.LowerCaseEqualsLiteral("move") &&
      !aAlter.LowerCaseEqualsLiteral("extend")) {
    return;
  }

  if (!aDirection.LowerCaseEqualsLiteral("forward") &&
      !aDirection.LowerCaseEqualsLiteral("backward") &&
      !aDirection.LowerCaseEqualsLiteral("left") &&
      !aDirection.LowerCaseEqualsLiteral("right")) {
    return;
  }

  // Make sure the layout is up to date as we access bidi information below.
  if (RefPtr<Document> doc = GetDocument()) {
    doc->FlushPendingNotifications(FlushType::Layout);
  }

  // Line moves are always visual.
  bool visual = aDirection.LowerCaseEqualsLiteral("left") ||
                aDirection.LowerCaseEqualsLiteral("right") ||
                aGranularity.LowerCaseEqualsLiteral("line");

  bool forward = aDirection.LowerCaseEqualsLiteral("forward") ||
                 aDirection.LowerCaseEqualsLiteral("right");

  bool extend = aAlter.LowerCaseEqualsLiteral("extend");

  nsSelectionAmount amount;
  if (aGranularity.LowerCaseEqualsLiteral("character")) {
    amount = eSelectCluster;
  } else if (aGranularity.LowerCaseEqualsLiteral("word")) {
    amount = eSelectWordNoSpace;
  } else if (aGranularity.LowerCaseEqualsLiteral("line")) {
    amount = eSelectLine;
  } else if (aGranularity.LowerCaseEqualsLiteral("lineboundary")) {
    amount = forward ? eSelectEndLine : eSelectBeginLine;
  } else if (aGranularity.LowerCaseEqualsLiteral("sentence") ||
             aGranularity.LowerCaseEqualsLiteral("sentenceboundary") ||
             aGranularity.LowerCaseEqualsLiteral("paragraph") ||
             aGranularity.LowerCaseEqualsLiteral("paragraphboundary") ||
             aGranularity.LowerCaseEqualsLiteral("documentboundary")) {
    Document* document = GetParentObject();
    if (document) {
      AutoTArray<nsString, 1> params;
      params.AppendElement(aGranularity);
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                      document, PropertiesFile::DOM_PROPERTIES,
                                      "SelectionModifyGranualirtyUnsupported",
                                      params);
    }
    return;
  } else {
    return;
  }

  // If the anchor doesn't equal the focus and we try to move without first
  // collapsing the selection, MoveCaret will collapse the selection and quit.
  // To avoid this, we need to collapse the selection first.
  nsresult rv = NS_OK;
  if (!extend) {
    RefPtr<nsINode> focusNode = GetFocusNode();
    // We should have checked earlier that there was a focus node.
    if (!focusNode) {
      return;
    }
    uint32_t focusOffset = FocusOffset();
    CollapseInLimiter(focusNode, focusOffset);
  }

  // If the paragraph direction of the focused frame is right-to-left,
  // we may have to swap the direction of movement.
  const PrimaryFrameData frameForFocus =
      GetPrimaryFrameForCaretAtFocusNode(visual);
  if (frameForFocus) {
    if (visual) {
      // FYI: This was done during a call of GetPrimaryFrameForCaretAtFocusNode.
      // Therefore, this may not be intended by the original author.
      mFrameSelection->SetHint(frameForFocus.mHint);
    }
    mozilla::intl::BidiDirection paraDir =
        nsBidiPresUtils::ParagraphDirection(frameForFocus.mFrame);

    if (paraDir == mozilla::intl::BidiDirection::RTL && visual) {
      if (amount == eSelectBeginLine) {
        amount = eSelectEndLine;
        forward = !forward;
      } else if (amount == eSelectEndLine) {
        amount = eSelectBeginLine;
        forward = !forward;
      }
    }
  }

  // MoveCaret will return an error if it can't move in the specified
  // direction, but we just ignore this error unless it's a line move, in which
  // case we call nsISelectionController::CompleteMove to move the cursor to
  // the beginning/end of the line.
  RefPtr<nsFrameSelection> frameSelection = mFrameSelection;
  rv = frameSelection->MoveCaret(
      forward ? eDirNext : eDirPrevious,
      nsFrameSelection::ExtendSelection(extend), amount,
      visual ? nsFrameSelection::eVisual : nsFrameSelection::eLogical);

  if (aGranularity.LowerCaseEqualsLiteral("line") && NS_FAILED(rv)) {
    RefPtr<PresShell> presShell = frameSelection->GetPresShell();
    if (!presShell) {
      return;
    }
    presShell->CompleteMove(forward, extend);
  }
}

void Selection::SetBaseAndExtentJS(nsINode& aAnchorNode, uint32_t aAnchorOffset,
                                   nsINode& aFocusNode, uint32_t aFocusOffset,
                                   ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aAnchorNode", aAnchorNode, "aAnchorOffset",
                    aAnchorOffset, "aFocusNode", aFocusNode, "aFocusOffset",
                    aFocusOffset);
    LogStackForSelectionAPI();
  }

  if (MOZ_UNLIKELY(
          !IsValidNodeAndOffsetForBoundary(aAnchorNode, aAnchorOffset, aRv) ||
          !IsValidNodeAndOffsetForBoundary(aFocusNode, aFocusOffset, aRv))) {
    return;
  }

  AutoRestore<bool> calledFromJSRestorer(mCalledByJS);
  mCalledByJS = true;
  SetBaseAndExtentInternal(InLimiter::eNo,
                           RawRangeBoundary(&aAnchorNode, aAnchorOffset),
                           RawRangeBoundary(&aFocusNode, aFocusOffset), aRv);
  if (StaticPrefs::dom_selection_mimic_chrome_tostring_enabled() &&
      !aRv.Failed()) {
    if (auto* presShell = GetPresShell()) {
      presShell->UpdateLastSelectionForToString(mFrameSelection);
    }
  }
}

void Selection::SetBaseAndExtent(nsINode& aAnchorNode, uint32_t aAnchorOffset,
                                 nsINode& aFocusNode, uint32_t aFocusOffset,
                                 ErrorResult& aRv) {
  if (MOZ_UNLIKELY(
          !IsValidNodeAndOffsetForBoundary(aAnchorNode, aAnchorOffset, aRv) ||
          !IsValidNodeAndOffsetForBoundary(aFocusNode, aFocusOffset, aRv))) {
    return;
  }

  SetBaseAndExtentInternal(InLimiter::eNo,
                           RawRangeBoundary(&aAnchorNode, aAnchorOffset),
                           RawRangeBoundary(&aFocusNode, aFocusOffset), aRv);
}

void Selection::SetBaseAndExtent(const RawRangeBoundary& aAnchorRef,
                                 const RawRangeBoundary& aFocusRef,
                                 ErrorResult& aRv) {
  if (!mCalledByJS && NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aAnchorRef", aAnchorRef, "aFocusRef",
                    aFocusRef);
    LogStackForSelectionAPI();
  }

  if (NS_WARN_IF(!aAnchorRef.IsSetAndValid()) ||
      NS_WARN_IF(!aFocusRef.IsSetAndValid())) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  SetBaseAndExtentInternal(InLimiter::eNo, aAnchorRef, aFocusRef, aRv);
}

void Selection::SetBaseAndExtentInLimiter(const RawRangeBoundary& aAnchorRef,
                                          const RawRangeBoundary& aFocusRef,
                                          ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aAnchorRef", aAnchorRef, "aFocusRef",
                    aFocusRef);
    LogStackForSelectionAPI();
  }

  if (NS_WARN_IF(!aAnchorRef.IsSetAndValid()) ||
      NS_WARN_IF(!aFocusRef.IsSetAndValid())) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  SetBaseAndExtentInternal(InLimiter::eYes, aAnchorRef, aFocusRef, aRv);
}

void Selection::SetBaseAndExtentInternal(InLimiter aInLimiter,
                                         const RawRangeBoundary& aAnchorRef,
                                         const RawRangeBoundary& aFocusRef,
                                         ErrorResult& aRv) {
  MOZ_ASSERT(aAnchorRef.IsSetAndValid());
  MOZ_ASSERT(aFocusRef.IsSetAndValid());

  if (!mFrameSelection) {
    aRv.Throw(NS_ERROR_NOT_INITIALIZED);
    return;
  }

  if (!HasSameRootOrSameComposedDoc(*aAnchorRef.GetContainer()) ||
      !HasSameRootOrSameComposedDoc(*aFocusRef.GetContainer())) {
    // Return with no error
    return;
  }

  // Prevent "selectionchange" event temporarily because it should be fired
  // after we set the direction.
  // XXX If they are disconnected, shouldn't we return error before allocating
  //     new nsRange instance?
  SelectionBatcher batch(this, __FUNCTION__);
  const Maybe<int32_t> order =
      IsEditorSelection()
          ? nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                aAnchorRef, aFocusRef)
          : nsContentUtils::ComparePoints<TreeKind::FlatForSelection>(
                aAnchorRef, aFocusRef);
  if (order && (*order <= 0)) {
    SetStartAndEndInternal(aInLimiter, aAnchorRef, aFocusRef, eDirNext, aRv);
    return;
  }

  // If there's no `order`, the range will be collapsed, unless another error is
  // detected before.
  SetStartAndEndInternal(aInLimiter, aFocusRef, aAnchorRef, eDirPrevious, aRv);
}

void Selection::SetStartAndEndInLimiter(const RawRangeBoundary& aStartRef,
                                        const RawRangeBoundary& aEndRef,
                                        ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aStartRef", aStartRef, "aEndRef", aEndRef);
    LogStackForSelectionAPI();
  }

  if (NS_WARN_IF(!aStartRef.IsSetAndValid()) ||
      NS_WARN_IF(!aEndRef.IsSetAndValid())) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  SetStartAndEndInternal(InLimiter::eYes, aStartRef, aEndRef, eDirNext, aRv);
}

Result<Ok, nsresult> Selection::SetStartAndEndInLimiter(
    nsINode& aStartContainer, uint32_t aStartOffset, nsINode& aEndContainer,
    uint32_t aEndOffset, nsDirection aDirection, int16_t aReason) {
  MOZ_ASSERT(aDirection == eDirPrevious || aDirection == eDirNext);
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aStartContainer", aStartContainer,
                    "aStartOffset", aStartOffset, "aEndContainer",
                    aEndContainer, "aEndOffset", aEndOffset, "nsDirection",
                    aDirection, "aReason", aReason);
    LogStackForSelectionAPI();
  }

  if (mFrameSelection) {
    mFrameSelection->AddChangeReasons(aReason);
  }

  ErrorResult error;
  if (MOZ_UNLIKELY(
          !IsValidNodeAndOffsetForBoundary(aStartContainer, aStartOffset,
                                           error) ||
          !IsValidNodeAndOffsetForBoundary(aEndContainer, aEndOffset, error))) {
    return Err(error.StealNSResult());
  }

  SetStartAndEndInternal(
      InLimiter::eYes, RawRangeBoundary(&aStartContainer, aStartOffset),
      RawRangeBoundary(&aEndContainer, aEndOffset), aDirection, error);
  MOZ_TRY(error.StealNSResult());
  return Ok();
}

void Selection::SetStartAndEnd(const RawRangeBoundary& aStartRef,
                               const RawRangeBoundary& aEndRef,
                               ErrorResult& aRv) {
  if (NeedsToLogSelectionAPI(*this)) {
    LogSelectionAPI(this, __func__, "aStartRef", aStartRef, "aEndRef", aEndRef);
    LogStackForSelectionAPI();
  }

  if (NS_WARN_IF(!aStartRef.IsSetAndValid()) ||
      NS_WARN_IF(!aEndRef.IsSetAndValid())) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  SetStartAndEndInternal(InLimiter::eNo, aStartRef, aEndRef, eDirNext, aRv);
}

void Selection::SetStartAndEndInternal(InLimiter aInLimiter,
                                       const RawRangeBoundary& aStartRef,
                                       const RawRangeBoundary& aEndRef,
                                       nsDirection aDirection,
                                       ErrorResult& aRv) {
  MOZ_ASSERT(aStartRef.IsSetAndValid());
  MOZ_ASSERT(aEndRef.IsSetAndValid());

  // Don't fire "selectionchange" event until everything done.
  SelectionBatcher batch(this, __FUNCTION__);

  if (aInLimiter == InLimiter::eYes) {
    if (!mFrameSelection ||
        !mFrameSelection->NodeIsInLimiters(aStartRef.GetContainer())) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }
    if (aStartRef.GetContainer() != aEndRef.GetContainer() &&
        !mFrameSelection->NodeIsInLimiters(aEndRef.GetContainer())) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }
  }

  RefPtr<nsRange> newRange = nsRange::Create(
      aStartRef, aEndRef, aRv,
      aInLimiter == InLimiter::eNo ? AllowRangeCrossShadowBoundary::Yes
                                   : AllowRangeCrossShadowBoundary::No);
  if (aRv.Failed()) {
    return;
  }

  RemoveAllRangesInternal(aRv);
  if (aRv.Failed()) {
    return;
  }

  RefPtr<Document> document(GetDocument());
  AddRangeAndSelectFramesAndNotifyListenersInternal(*newRange, document, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Adding a range may set 2 or more ranges if there are non-selectable
  // contents only when this change is caused by a user operation.  Therefore,
  // we need to select frames with the result in such case.
  if (mUserInitiated) {
    RefPtr<nsPresContext> presContext = GetPresContext();
    if (mStyledRanges.Length() > 1 && presContext) {
      SelectFramesInAllRanges(presContext);
    }
  }

  SetDirection(aDirection);
}

/** SelectionLanguageChange modifies the cursor Bidi level after a change in
 * keyboard direction
 *  @param aLangRTL is true if the new language is right-to-left or false if the
 * new language is left-to-right
 */
nsresult Selection::SelectionLanguageChange(bool aLangRTL) {
  if (!mFrameSelection) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<nsFrameSelection> frameSelection = mFrameSelection;

  // if the direction of the language hasn't changed, nothing to do
  mozilla::intl::BidiEmbeddingLevel kbdBidiLevel =
      aLangRTL ? mozilla::intl::BidiEmbeddingLevel::RTL()
               : mozilla::intl::BidiEmbeddingLevel::LTR();
  if (kbdBidiLevel == frameSelection->mKbdBidiLevel) {
    return NS_OK;
  }

  frameSelection->mKbdBidiLevel = kbdBidiLevel;

  PrimaryFrameData focusFrameData = GetPrimaryFrameForCaretAtFocusNode(false);
  if (!focusFrameData.mFrame) {
    return NS_ERROR_FAILURE;
  }

  auto [frameStart, frameEnd] = focusFrameData.mFrame->GetOffsets();
  RefPtr<nsPresContext> context = GetPresContext();
  mozilla::intl::BidiEmbeddingLevel levelBefore, levelAfter;
  if (!context) {
    return NS_ERROR_FAILURE;
  }

  mozilla::intl::BidiEmbeddingLevel level =
      focusFrameData.mFrame->GetEmbeddingLevel();
  int32_t focusOffset = static_cast<int32_t>(FocusOffset());
  if ((focusOffset != frameStart) && (focusOffset != frameEnd))
    // the cursor is not at a frame boundary, so the level of both the
    // characters (logically) before and after the cursor is equal to the frame
    // level
    levelBefore = levelAfter = level;
  else {
    // the cursor is at a frame boundary, so use GetPrevNextBidiLevels to find
    // the level of the characters before and after the cursor
    nsCOMPtr<nsIContent> focusContent = do_QueryInterface(GetFocusNode());
    nsPrevNextBidiLevels levels =
        frameSelection->GetPrevNextBidiLevels(focusContent, focusOffset, false);

    levelBefore = levels.mLevelBefore;
    levelAfter = levels.mLevelAfter;
  }

  if (levelBefore.IsSameDirection(levelAfter)) {
    // if cursor is between two characters with the same orientation, changing
    // the keyboard language must toggle the cursor level between the level of
    // the character with the lowest level (if the new language corresponds to
    // the orientation of that character) and this level plus 1 (if the new
    // language corresponds to the opposite orientation)
    if ((level != levelBefore) && (level != levelAfter)) {
      level = std::min(levelBefore, levelAfter);
    }
    if (level.IsSameDirection(kbdBidiLevel)) {
      frameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(level);
    } else {
      frameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(
          mozilla::intl::BidiEmbeddingLevel(level + 1));
    }
  } else {
    // if cursor is between characters with opposite orientations, changing the
    // keyboard language must change the cursor level to that of the adjacent
    // character with the orientation corresponding to the new language.
    if (levelBefore.IsSameDirection(kbdBidiLevel)) {
      frameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(levelBefore);
    } else {
      frameSelection->SetCaretBidiLevelAndMaybeSchedulePaint(levelAfter);
    }
  }

  // The caret might have moved, so invalidate the desired position
  // for future usages of up-arrow or down-arrow
  frameSelection->InvalidateDesiredCaretPos();

  return NS_OK;
}

void Selection::SetColors(const nsAString& aForegroundColor,
                          const nsAString& aBackgroundColor,
                          const nsAString& aAltForegroundColor,
                          const nsAString& aAltBackgroundColor,
                          ErrorResult& aRv) {
  if (mSelectionType != SelectionType::eFind) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  mCustomColors.reset(new SelectionCustomColors);

  constexpr auto currentColorStr = u"currentColor"_ns;
  constexpr auto transparentStr = u"transparent"_ns;

  if (!aForegroundColor.Equals(currentColorStr)) {
    nscolor foregroundColor;
    nsAttrValue aForegroundColorValue;
    aForegroundColorValue.ParseColor(aForegroundColor);
    if (!aForegroundColorValue.GetColorValue(foregroundColor)) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }
    mCustomColors->mForegroundColor = Some(foregroundColor);
  } else {
    mCustomColors->mForegroundColor = Nothing();
  }

  if (!aBackgroundColor.Equals(transparentStr)) {
    nscolor backgroundColor;
    nsAttrValue aBackgroundColorValue;
    aBackgroundColorValue.ParseColor(aBackgroundColor);
    if (!aBackgroundColorValue.GetColorValue(backgroundColor)) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }
    mCustomColors->mBackgroundColor = Some(backgroundColor);
  } else {
    mCustomColors->mBackgroundColor = Nothing();
  }

  if (!aAltForegroundColor.Equals(currentColorStr)) {
    nscolor altForegroundColor;
    nsAttrValue aAltForegroundColorValue;
    aAltForegroundColorValue.ParseColor(aAltForegroundColor);
    if (!aAltForegroundColorValue.GetColorValue(altForegroundColor)) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }
    mCustomColors->mAltForegroundColor = Some(altForegroundColor);
  } else {
    mCustomColors->mAltForegroundColor = Nothing();
  }

  if (!aAltBackgroundColor.Equals(transparentStr)) {
    nscolor altBackgroundColor;
    nsAttrValue aAltBackgroundColorValue;
    aAltBackgroundColorValue.ParseColor(aAltBackgroundColor);
    if (!aAltBackgroundColorValue.GetColorValue(altBackgroundColor)) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }
    mCustomColors->mAltBackgroundColor = Some(altBackgroundColor);
  } else {
    mCustomColors->mAltBackgroundColor = Nothing();
  }
}

void Selection::ResetColors() { mCustomColors = nullptr; }

void Selection::SetHighlightSelectionData(
    dom::HighlightSelectionData aHighlightSelectionData) {
  MOZ_ASSERT(mSelectionType == SelectionType::eHighlight);
  mHighlightData = std::move(aHighlightSelectionData);
}

JSObject* Selection::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return mozilla::dom::Selection_Binding::Wrap(aCx, this, aGivenProto);
}

// AutoHideSelectionChanges
AutoHideSelectionChanges::AutoHideSelectionChanges(
    const nsFrameSelection* aFrame)
    : AutoHideSelectionChanges(aFrame ? &aFrame->NormalSelection() : nullptr) {}

bool Selection::HasSameRootOrSameComposedDoc(const nsINode& aNode) const {
  nsINode* root = aNode.SubtreeRoot();
  Document* doc = GetDocument();
  return doc == root || (root && doc == root->GetComposedDoc());
}
