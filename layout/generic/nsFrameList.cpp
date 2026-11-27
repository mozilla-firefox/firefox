/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFrameList.h"

#include "mozilla/ArenaObjectID.h"
#include "mozilla/PresShell.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsBidiPresUtils.h"
#include "nsContainerFrame.h"
#include "nsILineIterator.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;

const nsFrameList nsFrameList::sEmptyList;

void* nsFrameList::operator new(size_t sz) {
  return FrameArena::Allocate(eArenaObjectID_nsFrameList, sz);
}

void nsFrameList::Delete() {
  MOZ_ASSERT(this != &EmptyList(), "Shouldn't Delete() this list");
  NS_ASSERTION(IsEmpty(), "Shouldn't Delete() a non-empty list");

  FrameArena::Free(eArenaObjectID_nsFrameList, this);
}

void nsFrameList::DestroyFrames(FrameDestroyContext& aContext) {
  while (nsIFrame* frame = RemoveLastChild()) {
    frame->Destroy(aContext);
  }
  MOZ_ASSERT(!mFirstChild && !mLastChild, "We should've destroyed all frames!");
}

void nsFrameList::RemoveFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "null ptr");
#ifdef DEBUG_FRAME_LIST
  // ContainsFrame is O(N)
  MOZ_ASSERT(ContainsFrame(aFrame), "wrong list");
#endif

  nsIFrame* nextFrame = aFrame->GetNextSibling();
  if (aFrame == mFirstChild) {
    mFirstChild = nextFrame;
    aFrame->SetNextSibling(nullptr);
    if (!nextFrame) {
      mLastChild = nullptr;
    }
  } else {
    nsIFrame* prevSibling = aFrame->GetPrevSibling();
    NS_ASSERTION(prevSibling && prevSibling->GetNextSibling() == aFrame,
                 "Broken frame linkage");
    prevSibling->SetNextSibling(nextFrame);
    aFrame->SetNextSibling(nullptr);
    if (!nextFrame) {
      mLastChild = prevSibling;
    }
  }
}

nsFrameList nsFrameList::TakeFramesAfter(nsIFrame* aFrame) {
  if (!aFrame) {
    return std::move(*this);
  }

  MOZ_ASSERT(ContainsFrame(aFrame), "aFrame is not on this list!");

  nsIFrame* newFirstChild = aFrame->GetNextSibling();
  if (!newFirstChild) {
    return nsFrameList();
  }

  nsIFrame* newLastChild = mLastChild;
  mLastChild = aFrame;
  mLastChild->SetNextSibling(nullptr);
  return nsFrameList(newFirstChild, newLastChild);
}

nsIFrame* nsFrameList::RemoveFirstChild() {
  if (mFirstChild) {
    nsIFrame* firstChild = mFirstChild;
    RemoveFrame(firstChild);
    return firstChild;
  }
  return nullptr;
}

nsIFrame* nsFrameList::RemoveLastChild() {
  if (mLastChild) {
    nsIFrame* lastChild = mLastChild;
    RemoveFrame(lastChild);
    return lastChild;
  }
  return nullptr;
}

void nsFrameList::DestroyFrame(FrameDestroyContext& aContext,
                               nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "null ptr");
  RemoveFrame(aFrame);
  aFrame->Destroy(aContext);
}

nsFrameList::Slice nsFrameList::InsertFrames(nsContainerFrame* aParent,
                                             nsIFrame* aPrevSibling,
                                             nsFrameList&& aFrameList) {
  MOZ_ASSERT(aFrameList.NotEmpty(), "Unexpected empty list");

  if (aParent) {
    aFrameList.ApplySetParent(aParent);
  }

  NS_ASSERTION(IsEmpty() || FirstChild()->GetParent() ==
                                aFrameList.FirstChild()->GetParent(),
               "frame to add has different parent");
  NS_ASSERTION(!aPrevSibling || aPrevSibling->GetParent() ==
                                    aFrameList.FirstChild()->GetParent(),
               "prev sibling has different parent");
#ifdef DEBUG_FRAME_LIST
  // ContainsFrame is O(N)
  NS_ASSERTION(!aPrevSibling || ContainsFrame(aPrevSibling),
               "prev sibling is not on this list");
#endif

  nsIFrame* firstNewFrame = aFrameList.FirstChild();
  nsIFrame* nextSibling;
  if (aPrevSibling) {
    nextSibling = aPrevSibling->GetNextSibling();
    aPrevSibling->SetNextSibling(firstNewFrame);
  } else {
    nextSibling = mFirstChild;
    mFirstChild = firstNewFrame;
  }

  nsIFrame* lastNewFrame = aFrameList.LastChild();
  lastNewFrame->SetNextSibling(nextSibling);
  if (!nextSibling) {
    mLastChild = lastNewFrame;
  }

  VerifyList();

  aFrameList.Clear();
  return Slice(firstNewFrame, nextSibling);
}

nsFrameList nsFrameList::TakeFramesBefore(nsIFrame* aFrame) {
  if (!aFrame) {
    // We handed over the whole list.
    return std::move(*this);
  }

  MOZ_ASSERT(ContainsFrame(aFrame), "aFrame is not on this list!");

  if (aFrame == mFirstChild) {
    // aFrame is our first child. Nothing to extract.
    return nsFrameList();
  }

  // Extract all previous siblings of aFrame as a new list.
  nsIFrame* prev = aFrame->GetPrevSibling();
  nsIFrame* newFirstChild = mFirstChild;
  nsIFrame* newLastChild = prev;

  prev->SetNextSibling(nullptr);
  mFirstChild = aFrame;

  return nsFrameList(newFirstChild, newLastChild);
}

nsIFrame* nsFrameList::FrameAt(int32_t aIndex) const {
  MOZ_ASSERT(aIndex >= 0, "invalid arg");
  if (aIndex < 0) {
    return nullptr;
  }
  nsIFrame* frame = mFirstChild;
  while ((aIndex-- > 0) && frame) {
    frame = frame->GetNextSibling();
  }
  return frame;
}

int32_t nsFrameList::IndexOf(const nsIFrame* aFrame) const {
  int32_t count = 0;
  for (nsIFrame* f = mFirstChild; f; f = f->GetNextSibling()) {
    if (f == aFrame) {
      return count;
    }
    ++count;
  }
  return -1;
}

bool nsFrameList::ContainsFrame(const nsIFrame* aFrame) const {
  MOZ_ASSERT(aFrame, "null ptr");

  nsIFrame* frame = mFirstChild;
  while (frame) {
    if (frame == aFrame) {
      return true;
    }
    frame = frame->GetNextSibling();
  }
  return false;
}

int32_t nsFrameList::GetLength() const {
  int32_t count = 0;
  nsIFrame* frame = mFirstChild;
  while (frame) {
    count++;
    frame = frame->GetNextSibling();
  }
  return count;
}

void nsFrameList::ApplySetParent(nsContainerFrame* aParent) const {
  NS_ASSERTION(aParent, "null ptr");

  for (nsIFrame* f = FirstChild(); f; f = f->GetNextSibling()) {
    f->SetParent(aParent);
  }
}

/* static */
void nsFrameList::UnhookFrameFromSiblings(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->GetPrevSibling() && aFrame->GetNextSibling());
  nsIFrame* const nextSibling = aFrame->GetNextSibling();
  nsIFrame* const prevSibling = aFrame->GetPrevSibling();
  aFrame->SetNextSibling(nullptr);
  prevSibling->SetNextSibling(nextSibling);
  MOZ_ASSERT(!aFrame->GetPrevSibling() && !aFrame->GetNextSibling());
}

#ifdef DEBUG_FRAME_DUMP
void nsFrameList::List(FILE* out) const {
  fprintf_stderr(out, "<\n");
  for (nsIFrame* frame = mFirstChild; frame; frame = frame->GetNextSibling()) {
    frame->List(out, "  ");
  }
  fprintf_stderr(out, ">\n");
}
#endif

nsIFrame* nsFrameList::GetPrevVisualFor(nsIFrame* aFrame) const {
  if (!mFirstChild) {
    return nullptr;
  }

  nsIFrame* parent = mFirstChild->GetParent();
  if (!parent) {
    return aFrame ? aFrame->GetPrevSibling() : LastChild();
  }

  mozilla::intl::BidiDirection paraDir =
      nsBidiPresUtils::ParagraphDirection(mFirstChild);

  AutoAssertNoDomMutations guard;
  nsILineIterator* iter = parent->GetLineIterator();
  if (!iter) {
    // Parent is not a block Frame
    if (parent->IsLineFrame()) {
      // Line frames are not bidi-splittable, so need to consider bidi
      // reordering
      if (paraDir == mozilla::intl::BidiDirection::LTR) {
        return nsBidiPresUtils::GetFrameToLeftOf(aFrame, mFirstChild, -1);
      } else {  // RTL
        return nsBidiPresUtils::GetFrameToRightOf(aFrame, mFirstChild, -1);
      }
    } else {
      // Just get the next or prev sibling, depending on block and frame
      // direction.
      if (nsBidiPresUtils::IsFrameInParagraphDirection(mFirstChild)) {
        return aFrame ? aFrame->GetPrevSibling() : LastChild();
      } else {
        return aFrame ? aFrame->GetNextSibling() : mFirstChild;
      }
    }
  }

  // Parent is a block frame, so use the LineIterator to find the previous
  // visual sibling on this line, or the last one on the previous line.

  int32_t thisLine;
  if (aFrame) {
    thisLine = iter->FindLineContaining(aFrame);
    if (thisLine < 0) {
      return nullptr;
    }
  } else {
    thisLine = iter->GetNumLines();
  }

  nsIFrame* frame = nullptr;

  if (aFrame) {
    auto line = iter->GetLine(thisLine).unwrap();

    if (paraDir == mozilla::intl::BidiDirection::LTR) {
      frame = nsBidiPresUtils::GetFrameToLeftOf(aFrame, line.mFirstFrameOnLine,
                                                line.mNumFramesOnLine);
    } else {  // RTL
      frame = nsBidiPresUtils::GetFrameToRightOf(aFrame, line.mFirstFrameOnLine,
                                                 line.mNumFramesOnLine);
    }
  }

  if (!frame && thisLine > 0) {
    // Get the last frame of the previous line
    auto line = iter->GetLine(thisLine - 1).unwrap();

    if (paraDir == mozilla::intl::BidiDirection::LTR) {
      frame = nsBidiPresUtils::GetFrameToLeftOf(nullptr, line.mFirstFrameOnLine,
                                                line.mNumFramesOnLine);
    } else {  // RTL
      frame = nsBidiPresUtils::GetFrameToRightOf(
          nullptr, line.mFirstFrameOnLine, line.mNumFramesOnLine);
    }
  }
  return frame;
}

nsIFrame* nsFrameList::GetNextVisualFor(nsIFrame* aFrame) const {
  if (!mFirstChild) {
    return nullptr;
  }

  nsIFrame* parent = mFirstChild->GetParent();
  if (!parent) {
    return aFrame ? aFrame->GetPrevSibling() : mFirstChild;
  }

  mozilla::intl::BidiDirection paraDir =
      nsBidiPresUtils::ParagraphDirection(mFirstChild);

  AutoAssertNoDomMutations guard;
  nsILineIterator* iter = parent->GetLineIterator();
  if (!iter) {
    // Parent is not a block Frame
    if (parent->IsLineFrame()) {
      // Line frames are not bidi-splittable, so need to consider bidi
      // reordering
      if (paraDir == mozilla::intl::BidiDirection::LTR) {
        return nsBidiPresUtils::GetFrameToRightOf(aFrame, mFirstChild, -1);
      } else {  // RTL
        return nsBidiPresUtils::GetFrameToLeftOf(aFrame, mFirstChild, -1);
      }
    } else {
      // Just get the next or prev sibling, depending on block and frame
      // direction.
      if (nsBidiPresUtils::IsFrameInParagraphDirection(mFirstChild)) {
        return aFrame ? aFrame->GetNextSibling() : mFirstChild;
      } else {
        return aFrame ? aFrame->GetPrevSibling() : LastChild();
      }
    }
  }

  // Parent is a block frame, so use the LineIterator to find the next visual
  // sibling on this line, or the first one on the next line.

  int32_t thisLine;
  if (aFrame) {
    thisLine = iter->FindLineContaining(aFrame);
    if (thisLine < 0) {
      return nullptr;
    }
  } else {
    thisLine = -1;
  }

  nsIFrame* frame = nullptr;

  if (aFrame) {
    auto line = iter->GetLine(thisLine).unwrap();

    if (paraDir == mozilla::intl::BidiDirection::LTR) {
      frame = nsBidiPresUtils::GetFrameToRightOf(aFrame, line.mFirstFrameOnLine,
                                                 line.mNumFramesOnLine);
    } else {  // RTL
      frame = nsBidiPresUtils::GetFrameToLeftOf(aFrame, line.mFirstFrameOnLine,
                                                line.mNumFramesOnLine);
    }
  }

  int32_t numLines = iter->GetNumLines();
  if (!frame && thisLine < numLines - 1) {
    // Get the first frame of the next line
    auto line = iter->GetLine(thisLine + 1).unwrap();

    if (paraDir == mozilla::intl::BidiDirection::LTR) {
      frame = nsBidiPresUtils::GetFrameToRightOf(
          nullptr, line.mFirstFrameOnLine, line.mNumFramesOnLine);
    } else {  // RTL
      frame = nsBidiPresUtils::GetFrameToLeftOf(nullptr, line.mFirstFrameOnLine,
                                                line.mNumFramesOnLine);
    }
  }
  return frame;
}

#ifdef DEBUG_FRAME_LIST
void nsFrameList::VerifyList() const {
  NS_ASSERTION((mFirstChild == nullptr) == (mLastChild == nullptr),
               "bad list state");

  if (IsEmpty()) {
    return;
  }

  // Simple algorithm to find a loop in a linked list -- advance pointers
  // through it at speeds of 1 and 2, and if they ever get to be equal bail
  NS_ASSERTION(!mFirstChild->GetPrevSibling(), "bad prev sibling pointer");
  nsIFrame *first = mFirstChild, *second = mFirstChild;
  for (;;) {
    first = first->GetNextSibling();
    second = second->GetNextSibling();
    if (!second) {
      break;
    }
    NS_ASSERTION(second->GetPrevSibling()->GetNextSibling() == second,
                 "bad prev sibling pointer");
    second = second->GetNextSibling();
    if (first == second) {
      // Loop detected!  Since second advances faster, they can't both be null;
      // we would have broken out of the loop long ago.
      NS_ERROR("loop in frame list.  This will probably hang soon.");
      return;
    }
    if (!second) {
      break;
    }
    NS_ASSERTION(second->GetPrevSibling()->GetNextSibling() == second,
                 "bad prev sibling pointer");
  }

  NS_ASSERTION(mLastChild == nsLayoutUtils::GetLastSibling(mFirstChild),
               "bogus mLastChild");
  // XXX we should also assert that all GetParent() are either null or
  // the same non-null value, but nsCSSFrameConstructor::nsFrameItems
  // prevents that, e.g. table captions.
}
#endif

namespace mozilla {

#ifdef DEBUG_FRAME_DUMP
const char* ChildListName(FrameChildListID aListID) {
  switch (aListID) {
    case FrameChildListID::Principal:
      return "";
    case FrameChildListID::Absolute:
      return "AbsoluteList";
    case FrameChildListID::PushedAbsolute:
      return "PushedAbsoluteList";
    case FrameChildListID::Overflow:
      return "OverflowList";
    case FrameChildListID::OverflowContainers:
      return "OverflowContainersList";
    case FrameChildListID::ExcessOverflowContainers:
      return "ExcessOverflowContainersList";
    case FrameChildListID::OverflowOutOfFlow:
      return "OverflowOutOfFlowList";
    case FrameChildListID::Float:
      return "FloatList";
    case FrameChildListID::Marker:
      return "MarkerList";
    case FrameChildListID::PushedFloats:
      return "PushedFloatsList";
    case FrameChildListID::NoReflowPrincipal:
      return "NoReflowPrincipalList";
  }

  MOZ_ASSERT_UNREACHABLE("unknown list");
  return "UNKNOWN_FRAME_CHILD_LIST";
}
#endif

AutoFrameListPtr::~AutoFrameListPtr() {
  if (mFrameList) {
    mFrameList->Delete();
  }
}

}  // namespace mozilla
