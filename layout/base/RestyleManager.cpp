/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/RestyleManager.h"

#include "mozilla/AnimationUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/EffectSet.h"
#include "mozilla/GeckoBindings.h"
#include "mozilla/LayerAnimationInfo.h"
#include "mozilla/layers/AnimationInfo.h"
#include "mozilla/layout/ScrollAnchorContainer.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/Unused.h"
#include "mozilla/ViewportFrame.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/dom/ChildIterator.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLInputElement.h"

#include "ScrollSnap.h"
#include "nsAnimationManager.h"
#include "nsBlockFrame.h"
#include "nsContentUtils.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsDocShell.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsImageFrame.h"
#include "nsPlaceholderFrame.h"
#include "nsPrintfCString.h"
#include "nsRefreshDriver.h"
#include "nsStyleChangeList.h"
#include "nsStyleUtil.h"
#include "nsTransitionManager.h"
#include "StickyScrollContainer.h"
#include "ActiveLayerTracker.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

using mozilla::layers::AnimationInfo;
using mozilla::layout::ScrollAnchorContainer;

using namespace mozilla::dom;
using namespace mozilla::layers;

namespace mozilla {

RestyleManager::RestyleManager(nsPresContext* aPresContext)
    : mPresContext(aPresContext),
      mRestyleGeneration(1),
      mUndisplayedRestyleGeneration(1),
      mInStyleRefresh(false),
      mAnimationGeneration(0) {
  MOZ_ASSERT(mPresContext);
}

void RestyleManager::ContentInserted(nsIContent* aChild) {
  MOZ_ASSERT(aChild->GetParentNode());
  if (aChild->IsElement()) {
    StyleSet()->MaybeInvalidateForElementInsertion(*aChild->AsElement());
  }
  RestyleForInsertOrChange(aChild);
}

void RestyleManager::ContentAppended(nsIContent* aFirstNewContent) {
  MOZ_ASSERT(aFirstNewContent->GetParentNode());

#ifdef DEBUG
  for (nsIContent* cur = aFirstNewContent; cur; cur = cur->GetNextSibling()) {
    NS_ASSERTION(cur->IsRootOfNativeAnonymousSubtree() ==
                     aFirstNewContent->IsRootOfNativeAnonymousSubtree(),
                 "anonymous nodes should not be in child lists");
  }
#endif

  // We get called explicitly with NAC by editor and view transitions code, but
  // in those cases we don't need to do any invalidation.
  if (MOZ_UNLIKELY(aFirstNewContent->IsRootOfNativeAnonymousSubtree())) {
    return;
  }

  StyleSet()->MaybeInvalidateForElementAppend(*aFirstNewContent);

  auto* container = aFirstNewContent->GetParentNode();
  const auto selectorFlags = container->GetSelectorFlags() &
                             NodeSelectorFlags::AllSimpleRestyleFlagsForAppend;
  if (!selectorFlags) {
    return;
  }

  // The container cannot be a document.
  MOZ_ASSERT(container->IsElement() || container->IsShadowRoot());

  if (selectorFlags & NodeSelectorFlags::HasEmptySelector) {
    // see whether we need to restyle the container
    bool wasEmpty = true;  // :empty or :-moz-only-whitespace
    for (nsIContent* cur = container->GetFirstChild(); cur != aFirstNewContent;
         cur = cur->GetNextSibling()) {
      // We don't know whether we're testing :empty or :-moz-only-whitespace,
      // so be conservative and assume :-moz-only-whitespace (i.e., make
      // IsSignificantChild less likely to be true, and thus make us more
      // likely to restyle).
      if (nsStyleUtil::IsSignificantChild(cur, false)) {
        wasEmpty = false;
        break;
      }
    }
    if (wasEmpty && container->IsElement()) {
      RestyleForEmptyChange(container->AsElement());
      return;
    }
  }

  if (selectorFlags & NodeSelectorFlags::HasSlowSelector) {
    RestyleWholeContainer(container, selectorFlags);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NodeSelectorFlags::HasEdgeChildSelector) {
    // restyle the last element child before this node
    for (nsIContent* cur = aFirstNewContent->GetPreviousSibling(); cur;
         cur = cur->GetPreviousSibling()) {
      if (cur->IsElement()) {
        auto* element = cur->AsElement();
        PostRestyleEvent(element, RestyleHint::RestyleSubtree(),
                         nsChangeHint(0));
        StyleSet()->MaybeInvalidateRelativeSelectorForNthEdgeDependency(
            *element, StyleRelativeSelectorNthEdgeInvalidateFor::Last);
        break;
      }
    }
  }
}

void RestyleManager::RestylePreviousSiblings(nsIContent* aStartingSibling) {
  for (nsIContent* sibling = aStartingSibling; sibling;
       sibling = sibling->GetPreviousSibling()) {
    if (auto* element = Element::FromNode(sibling)) {
      PostRestyleEvent(element, RestyleHint::RestyleSubtree(), nsChangeHint(0));
    }
  }
}

void RestyleManager::RestyleSiblingsStartingWith(nsIContent* aStartingSibling) {
  for (nsIContent* sibling = aStartingSibling; sibling;
       sibling = sibling->GetNextSibling()) {
    if (auto* element = Element::FromNode(sibling)) {
      PostRestyleEvent(element, RestyleHint::RestyleSubtree(), nsChangeHint(0));
    }
  }
}

void RestyleManager::RestyleWholeContainer(nsINode* aContainer,
                                           NodeSelectorFlags aSelectorFlags) {
  if (!mRestyledAsWholeContainer.EnsureInserted(aContainer)) {
    return;
  }
  if (auto* containerElement = Element::FromNode(aContainer)) {
    PostRestyleEvent(containerElement, RestyleHint::RestyleSubtree(),
                     nsChangeHint(0));
    if (aSelectorFlags & NodeSelectorFlags::HasSlowSelectorNthAll) {
      StyleSet()->MaybeInvalidateRelativeSelectorForNthDependencyFromSibling(
          containerElement->GetFirstElementChild(),
          /* aForceRestyleSiblings = */ false);
    }
  } else {
    RestyleSiblingsStartingWith(aContainer->GetFirstChild());
  }
}

void RestyleManager::RestyleForEmptyChange(Element* aContainer) {
  PostRestyleEvent(aContainer, RestyleHint::RestyleSubtree(), nsChangeHint(0));
  StyleSet()->MaybeInvalidateRelativeSelectorForEmptyDependency(*aContainer);

  // In some cases (:empty + E, :empty ~ E), a change in the content of
  // an element requires restyling its parent's siblings.
  nsIContent* grandparent = aContainer->GetParent();
  if (!grandparent || !(grandparent->GetSelectorFlags() &
                        NodeSelectorFlags::HasSlowSelectorLaterSiblings)) {
    return;
  }
  RestyleSiblingsStartingWith(aContainer->GetNextSibling());
}

void RestyleManager::MaybeRestyleForEdgeChildChange(nsINode* aContainer,
                                                    nsIContent* aChangedChild) {
  MOZ_ASSERT(aContainer->GetSelectorFlags() &
             NodeSelectorFlags::HasEdgeChildSelector);
  MOZ_ASSERT(aChangedChild->GetParent() == aContainer);
  // restyle the previously-first element child if it is after this node
  bool passedChild = false;
  for (nsIContent* content = aContainer->GetFirstChild(); content;
       content = content->GetNextSibling()) {
    if (content == aChangedChild) {
      passedChild = true;
      continue;
    }
    if (content->IsElement()) {
      if (passedChild) {
        auto* element = content->AsElement();
        PostRestyleEvent(element, RestyleHint::RestyleSubtree(),
                         nsChangeHint(0));
        StyleSet()->MaybeInvalidateRelativeSelectorForNthEdgeDependency(
            *element, StyleRelativeSelectorNthEdgeInvalidateFor::First);
      }
      break;
    }
  }
  // restyle the previously-last element child if it is before this node
  passedChild = false;
  for (nsIContent* content = aContainer->GetLastChild(); content;
       content = content->GetPreviousSibling()) {
    if (content == aChangedChild) {
      passedChild = true;
      continue;
    }
    if (content->IsElement()) {
      if (passedChild) {
        auto* element = content->AsElement();
        PostRestyleEvent(element, RestyleHint::RestyleSubtree(),
                         nsChangeHint(0));
        StyleSet()->MaybeInvalidateRelativeSelectorForNthEdgeDependency(
            *element, StyleRelativeSelectorNthEdgeInvalidateFor::Last);
      }
      break;
    }
  }
}

template <typename CharT>
bool WhitespaceOnly(const CharT* aBuffer, size_t aUpTo) {
  for (auto index : IntegerRange(aUpTo)) {
    if (!dom::IsSpaceCharacter(aBuffer[index])) {
      return false;
    }
  }
  return true;
}

template <typename CharT>
bool WhitespaceOnlyChangedOnAppend(const CharT* aBuffer, size_t aOldLength,
                                   size_t aNewLength) {
  MOZ_ASSERT(aOldLength <= aNewLength);
  if (!WhitespaceOnly(aBuffer, aOldLength)) {
    // The old text was already not whitespace-only.
    return false;
  }

  return !WhitespaceOnly(aBuffer + aOldLength, aNewLength - aOldLength);
}

static bool HasAnySignificantSibling(Element* aContainer, nsIContent* aChild) {
  MOZ_ASSERT(aChild->GetParent() == aContainer);
  for (nsIContent* child = aContainer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child == aChild) {
      continue;
    }
    // We don't know whether we're testing :empty or :-moz-only-whitespace,
    // so be conservative and assume :-moz-only-whitespace (i.e., make
    // IsSignificantChild less likely to be true, and thus make us more
    // likely to restyle).
    if (nsStyleUtil::IsSignificantChild(child, false)) {
      return true;
    }
  }

  return false;
}

void RestyleManager::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  nsINode* parent = aContent->GetParentNode();
  MOZ_ASSERT(parent, "How were we notified of a stray node?");

  const auto slowSelectorFlags =
      parent->GetSelectorFlags() & NodeSelectorFlags::AllSimpleRestyleFlags;
  if (!(slowSelectorFlags & (NodeSelectorFlags::HasEmptySelector |
                             NodeSelectorFlags::HasEdgeChildSelector))) {
    // Nothing to do, no other slow selector can change as a result of this.
    return;
  }

  if (!aContent->IsText()) {
    // Doesn't matter to styling (could be a processing instruction or a
    // comment), it can't change whether any selectors match or don't.
    return;
  }

  if (MOZ_UNLIKELY(!parent->IsElement())) {
    MOZ_ASSERT(parent->IsShadowRoot());
    return;
  }

  if (MOZ_UNLIKELY(aContent->IsRootOfNativeAnonymousSubtree())) {
    // This is an anonymous node and thus isn't in child lists, so isn't taken
    // into account for selector matching the relevant selectors here.
    return;
  }

  // Handle appends specially since they're common and we can know both the old
  // and the new text exactly.
  //
  // TODO(emilio): This could be made much more general if :-moz-only-whitespace
  // / :-moz-first-node and :-moz-last-node didn't exist. In that case we only
  // need to know whether we went from empty to non-empty, and that's trivial to
  // know, with CharacterDataChangeInfo...
  if (!aInfo.mAppend) {
    // FIXME(emilio): This restyles unnecessarily if the text node is the only
    // child of the parent element. Fortunately, it's uncommon to have such
    // nodes and this not being an append.
    //
    // See the testcase in bug 1427625 for a test-case that triggers this.
    RestyleForInsertOrChange(aContent);
    return;
  }

  const nsTextFragment* text = &aContent->AsText()->TextFragment();

  const size_t oldLength = aInfo.mChangeStart;
  const size_t newLength = text->GetLength();

  const bool emptyChanged = !oldLength && newLength;

  const bool whitespaceOnlyChanged =
      text->Is2b()
          ? WhitespaceOnlyChangedOnAppend(text->Get2b(), oldLength, newLength)
          : WhitespaceOnlyChangedOnAppend(text->Get1b(), oldLength, newLength);

  if (!emptyChanged && !whitespaceOnlyChanged) {
    return;
  }

  if (slowSelectorFlags & NodeSelectorFlags::HasEmptySelector) {
    if (!HasAnySignificantSibling(parent->AsElement(), aContent)) {
      // We used to be empty, restyle the parent.
      RestyleForEmptyChange(parent->AsElement());
      return;
    }
  }

  if (slowSelectorFlags & NodeSelectorFlags::HasEdgeChildSelector) {
    MaybeRestyleForEdgeChildChange(parent, aContent);
  }
}

// Restyling for a ContentInserted or CharacterDataChanged notification.
// This could be used for ContentRemoved as well if we got the
// notification before the removal happened (and sometimes
// CharacterDataChanged is more like a removal than an addition).
// The comments are written and variables are named in terms of it being
// a ContentInserted notification.
void RestyleManager::RestyleForInsertOrChange(nsIContent* aChild) {
  nsINode* container = aChild->GetParentNode();
  MOZ_ASSERT(container);

  const auto selectorFlags =
      container->GetSelectorFlags() & NodeSelectorFlags::AllSimpleRestyleFlags;
  if (!selectorFlags) {
    return;
  }

  NS_ASSERTION(!aChild->IsRootOfNativeAnonymousSubtree(),
               "anonymous nodes should not be in child lists");

  // The container cannot be a document.
  MOZ_ASSERT(container->IsElement() || container->IsShadowRoot());

  if (selectorFlags & NodeSelectorFlags::HasEmptySelector &&
      container->IsElement()) {
    // See whether we need to restyle the container due to :empty /
    // :-moz-only-whitespace.
    const bool wasEmpty =
        !HasAnySignificantSibling(container->AsElement(), aChild);
    if (wasEmpty) {
      // FIXME(emilio): When coming from CharacterDataChanged this can restyle
      // unnecessarily. Also can restyle unnecessarily if aChild is not
      // significant anyway, though that's more unlikely.
      RestyleForEmptyChange(container->AsElement());
      return;
    }
  }

  if (selectorFlags & NodeSelectorFlags::HasSlowSelector) {
    RestyleWholeContainer(container, selectorFlags);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NodeSelectorFlags::HasSlowSelectorLaterSiblings) {
    // Restyle all later siblings.
    if (selectorFlags & NodeSelectorFlags::HasSlowSelectorNthAll) {
      StyleSet()->MaybeInvalidateRelativeSelectorForNthDependencyFromSibling(
          aChild->GetNextElementSibling(), /* aForceRestyleSiblings = */ true);
    } else {
      RestyleSiblingsStartingWith(aChild->GetNextSibling());
    }
  }

  if (selectorFlags & NodeSelectorFlags::HasEdgeChildSelector) {
    MaybeRestyleForEdgeChildChange(container, aChild);
  }
}

void RestyleManager::ContentWillBeRemoved(nsIContent* aOldChild) {
  auto* container = aOldChild->GetParentNode();
  MOZ_ASSERT(container);

  // Computed style data isn't useful for detached nodes, and we'll need to
  // recompute it anyway if we ever insert the nodes back into a document.
  if (auto* element = Element::FromNode(aOldChild)) {
    RestyleManager::ClearServoDataFromSubtree(element);
    // If this element is undisplayed or may have undisplayed descendants, we
    // need to invalidate the cache, since there's the unlikely event of those
    // elements getting destroyed and their addresses reused in a way that we
    // look up the cache with their address for a different element before it's
    // invalidated.
    IncrementUndisplayedRestyleGeneration();
  }

  // This is called with anonymous nodes explicitly by editor and view
  // transitions code, which manage anon content manually.
  // See similar code in ContentAppended.
  if (MOZ_UNLIKELY(aOldChild->IsRootOfNativeAnonymousSubtree())) {
    MOZ_ASSERT(!aOldChild->GetNextSibling(), "NAC doesn't have siblings");
    MOZ_ASSERT(aOldChild->GetProperty(nsGkAtoms::restylableAnonymousNode),
               "anonymous nodes should not be in child lists (bug 439258)");
    return;
  }

  if (aOldChild->IsElement()) {
    StyleSet()->MaybeInvalidateForElementRemove(*aOldChild->AsElement());
  }

  const auto selectorFlags =
      container->GetSelectorFlags() & NodeSelectorFlags::AllSimpleRestyleFlags;
  if (!selectorFlags) {
    return;
  }

  // The container cannot be a document.
  const bool containerIsElement = container->IsElement();
  MOZ_ASSERT(containerIsElement || container->IsShadowRoot());

  if (selectorFlags & NodeSelectorFlags::HasEmptySelector &&
      containerIsElement) {
    // see whether we need to restyle the container
    bool isEmpty = true;  // :empty or :-moz-only-whitespace
    for (nsIContent* child = container->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      // We don't know whether we're testing :empty or :-moz-only-whitespace,
      // so be conservative and assume :-moz-only-whitespace (i.e., make
      // IsSignificantChild less likely to be true, and thus make us more
      // likely to restyle).
      if (child != aOldChild && nsStyleUtil::IsSignificantChild(child, false)) {
        isEmpty = false;
        break;
      }
    }
    if (isEmpty && containerIsElement) {
      RestyleForEmptyChange(container->AsElement());
      return;
    }
  }

  // It is somewhat common to remove all nodes in a container from the
  // beginning. If we're doing that, going through the
  // HasSlowSelectorLaterSiblings code-path would be quadratic, so that's not
  // amazing. Instead, we take the slower path (which also restyles the
  // container) in that case. It restyles one more element, but it avoids the
  // quadratic behavior.
  const bool restyleWholeContainer =
      (selectorFlags & NodeSelectorFlags::HasSlowSelector) ||
      (selectorFlags & NodeSelectorFlags::HasSlowSelectorLaterSiblings &&
       !aOldChild->GetPreviousSibling());

  if (restyleWholeContainer) {
    RestyleWholeContainer(container, selectorFlags);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NodeSelectorFlags::HasSlowSelectorLaterSiblings) {
    // Restyle all later siblings.
    if (selectorFlags & NodeSelectorFlags::HasSlowSelectorNthAll) {
      Element* nextSibling = aOldChild->GetNextElementSibling();
      StyleSet()->MaybeInvalidateRelativeSelectorForNthDependencyFromSibling(
          nextSibling, /* aForceRestyleSiblings = */ true);
    } else {
      RestyleSiblingsStartingWith(aOldChild->GetNextSibling());
    }
  }

  if (selectorFlags & NodeSelectorFlags::HasEdgeChildSelector) {
    const nsIContent* nextSibling = aOldChild->GetNextSibling();
    // restyle the now-first element child if it was after aOldChild
    bool reachedFollowingSibling = false;
    for (nsIContent* content = container->GetFirstChild(); content;
         content = content->GetNextSibling()) {
      if (content == aOldChild) {
        // aOldChild is getting removed, so we don't want to account for it for
        // the purposes of computing whether we're now the first / last child.
        continue;
      }
      if (content == nextSibling) {
        reachedFollowingSibling = true;
        // do NOT continue here; we might want to restyle this node
      }
      if (content->IsElement()) {
        if (reachedFollowingSibling) {
          auto* element = content->AsElement();
          PostRestyleEvent(element, RestyleHint::RestyleSubtree(),
                           nsChangeHint(0));
          StyleSet()->MaybeInvalidateRelativeSelectorForNthEdgeDependency(
              *element, StyleRelativeSelectorNthEdgeInvalidateFor::First);
        }
        break;
      }
    }
    // restyle the now-last element child if it was before aOldChild
    reachedFollowingSibling = !nextSibling;
    for (nsIContent* content = container->GetLastChild(); content;
         content = content->GetPreviousSibling()) {
      if (content == aOldChild) {
        // See above.
        continue;
      }
      if (content->IsElement()) {
        if (reachedFollowingSibling) {
          auto* element = content->AsElement();
          PostRestyleEvent(element, RestyleHint::RestyleSubtree(),
                           nsChangeHint(0));
          StyleSet()->MaybeInvalidateRelativeSelectorForNthEdgeDependency(
              *element, StyleRelativeSelectorNthEdgeInvalidateFor::Last);
        }
        break;
      }
      if (content == nextSibling) {
        reachedFollowingSibling = true;
      }
    }
  }
}

static bool StateChangeMayAffectFrame(const Element& aElement,
                                      const nsIFrame& aFrame,
                                      ElementState aStates) {
  const bool brokenChanged = aStates.HasState(ElementState::BROKEN);
  if (!brokenChanged) {
    return false;
  }

  if (aFrame.IsGeneratedContentFrame()) {
    // If it's other generated content, ignore state changes on it.
    return aElement.IsHTMLElement(nsGkAtoms::mozgeneratedcontentimage);
  }

  if (aElement.IsAnyOfHTMLElements(nsGkAtoms::object, nsGkAtoms::embed)) {
    // Broken affects object fallback behavior.
    return true;
  }

  const bool mightChange = [&] {
    if (aElement.IsHTMLElement(nsGkAtoms::img)) {
      return true;
    }
    const auto* input = HTMLInputElement::FromNode(aElement);
    return input && input->ControlType() == FormControlType::InputImage;
  }();

  if (!mightChange) {
    return false;
  }

  const bool needsImageFrame =
      nsImageFrame::ImageFrameTypeFor(aElement, *aFrame.Style()) !=
      nsImageFrame::ImageFrameType::None;
  return needsImageFrame != aFrame.IsImageFrameOrSubclass();
}

static bool RepaintForAppearance(nsIFrame& aFrame, const Element& aElement,
                                 ElementState aStateMask) {
  constexpr auto kThemingStates =
      ElementState::HOVER | ElementState::ACTIVE | ElementState::FOCUSRING |
      ElementState::DISABLED | ElementState::CHECKED |
      ElementState::INDETERMINATE | ElementState::READONLY |
      ElementState::FOCUS;
  if (!aStateMask.HasAtLeastOneOfStates(kThemingStates)) {
    return false;
  }

  if (aElement.IsAnyOfXULElements(nsGkAtoms::checkbox, nsGkAtoms::radio)) {
    // The checkbox inside these elements inherit hover state and so on, see
    // nsNativeTheme::GetContentState.
    // FIXME(emilio): Would be nice to not have these hard-coded.
    return true;
  }
  auto appearance = aFrame.StyleDisplay()->EffectiveAppearance();
  if (appearance == StyleAppearance::None) {
    return false;
  }
  nsPresContext* pc = aFrame.PresContext();
  return pc->Theme()->ThemeSupportsWidget(pc, &aFrame, appearance);
}

/**
 * Calculates the change hint and the restyle hint for a given content state
 * change.
 */
static nsChangeHint ChangeForContentStateChange(const Element& aElement,
                                                ElementState aStateMask) {
  auto changeHint = nsChangeHint(0);

  // Any change to a content state that affects which frames we construct
  // must lead to a frame reconstruct here if we already have a frame.
  // Note that we never decide through non-CSS means to not create frames
  // based on content states, so if we already don't have a frame we don't
  // need to force a reframe -- if it's needed, the HasStateDependentStyle
  // call will handle things.
  if (nsIFrame* primaryFrame = aElement.GetPrimaryFrame()) {
    if (StateChangeMayAffectFrame(aElement, *primaryFrame, aStateMask)) {
      return nsChangeHint_ReconstructFrame;
    }
    if (RepaintForAppearance(*primaryFrame, aElement, aStateMask)) {
      changeHint |= nsChangeHint_RepaintFrame;
    }
    primaryFrame->ElementStateChanged(aStateMask);
  }

  if (aStateMask.HasState(ElementState::VISITED)) {
    // Exposing information to the page about whether the link is
    // visited or not isn't really something we can worry about here.
    // FIXME: We could probably do this a bit better.
    changeHint |= nsChangeHint_RepaintFrame;
  }

  // This changes the applicable text-transform in the editor root.
  if (aStateMask.HasState(ElementState::REVEALED)) {
    // This is the same change hint as tweaking text-transform.
    changeHint |= NS_STYLE_HINT_REFLOW;
  }

  return changeHint;
}

#ifdef DEBUG
/* static */
nsCString RestyleManager::ChangeHintToString(nsChangeHint aHint) {
  nsCString result;
  bool any = false;
  const char* names[] = {"RepaintFrame",
                         "NeedReflow",
                         "ClearAncestorIntrinsics",
                         "ClearDescendantIntrinsics",
                         "NeedDirtyReflow",
                         "UpdateCursor",
                         "UpdateEffects",
                         "UpdateOpacityLayer",
                         "UpdateTransformLayer",
                         "ReconstructFrame",
                         "UpdateOverflow",
                         "UpdateSubtreeOverflow",
                         "UpdatePostTransformOverflow",
                         "UpdateParentOverflow",
                         "ChildrenOnlyTransform",
                         "RecomputePosition",
                         "UpdateContainingBlock",
                         "BorderStyleNoneChange",
                         "SchedulePaint",
                         "NeutralChange",
                         "InvalidateRenderingObservers",
                         "ReflowChangesSizeOrPosition",
                         "UpdateComputedBSize",
                         "UpdateUsesOpacity",
                         "UpdateBackgroundPosition",
                         "AddOrRemoveTransform",
                         "ScrollbarChange",
                         "UpdateTableCellSpans",
                         "VisibilityChange"};
  static_assert(nsChangeHint_AllHints ==
                    static_cast<uint32_t>((1ull << std::size(names)) - 1),
                "Name list doesn't match change hints.");
  uint32_t hint = aHint & static_cast<uint32_t>((1ull << std::size(names)) - 1);
  uint32_t rest =
      aHint & ~static_cast<uint32_t>((1ull << std::size(names)) - 1);
  if ((hint & NS_STYLE_HINT_REFLOW) == NS_STYLE_HINT_REFLOW) {
    result.AppendLiteral("NS_STYLE_HINT_REFLOW");
    hint = hint & ~NS_STYLE_HINT_REFLOW;
    any = true;
  } else if ((hint & nsChangeHint_AllReflowHints) ==
             nsChangeHint_AllReflowHints) {
    result.AppendLiteral("nsChangeHint_AllReflowHints");
    hint = hint & ~nsChangeHint_AllReflowHints;
    any = true;
  } else if ((hint & NS_STYLE_HINT_VISUAL) == NS_STYLE_HINT_VISUAL) {
    result.AppendLiteral("NS_STYLE_HINT_VISUAL");
    hint = hint & ~NS_STYLE_HINT_VISUAL;
    any = true;
  }
  for (uint32_t i = 0; i < std::size(names); i++) {
    if (hint & (1u << i)) {
      if (any) {
        result.AppendLiteral(" | ");
      }
      result.AppendPrintf("nsChangeHint_%s", names[i]);
      any = true;
    }
  }
  if (rest) {
    if (any) {
      result.AppendLiteral(" | ");
    }
    result.AppendPrintf("0x%0x", rest);
  } else {
    if (!any) {
      result.AppendLiteral("nsChangeHint(0)");
    }
  }
  return result;
}
#endif

/**
 * Frame construction helpers follow.
 */
#ifdef DEBUG
static bool gInApplyRenderingChangeToTree = false;
#endif

/**
 * Sync views on the frame and all of it's descendants (following placeholders).
 * The change hint should be some combination of nsChangeHint_RepaintFrame,
 * nsChangeHint_UpdateOpacityLayer and nsChangeHint_SchedulePaint, nothing else.
 */
static void SyncViewsAndInvalidateDescendants(nsIFrame*, nsChangeHint);

static void StyleChangeReflow(nsIFrame* aFrame, nsChangeHint aHint);

/**
 * This helper function is used to find the correct SVG frame to target when we
 * encounter nsChangeHint_ChildrenOnlyTransform; needed since sometimes we end
 * up handling that hint while processing hints for one of the SVG frame's
 * ancestor frames.
 *
 * The reason that we sometimes end up trying to process the hint for an
 * ancestor of the SVG frame that the hint is intended for is due to the way we
 * process restyle events. ApplyRenderingChangeToTree adjusts the frame from
 * the restyled element's principle frame to one of its ancestor frames based
 * on what nsCSSRendering::FindBackground returns, since the background style
 * may have been propagated up to an ancestor frame. Processing hints using an
 * ancestor frame is fine in general, but nsChangeHint_ChildrenOnlyTransform is
 * a special case since it is intended to update a specific frame.
 */
static nsIFrame* GetFrameForChildrenOnlyTransformHint(nsIFrame* aFrame) {
  if (aFrame->IsViewportFrame()) {
    // This happens if the root-<svg> is fixed positioned, in which case we
    // can't use aFrame->GetContent() to find the primary frame, since
    // GetContent() returns nullptr for ViewportFrame.
    aFrame = aFrame->PrincipalChildList().FirstChild();
  }
  // For a ScrollContainerFrame, this will get the SVG frame that has the
  // children-only transforms:
  aFrame = aFrame->GetContent()->GetPrimaryFrame();
  if (aFrame->IsSVGOuterSVGFrame()) {
    aFrame = aFrame->PrincipalChildList().FirstChild();
    MOZ_ASSERT(aFrame->IsSVGOuterSVGAnonChildFrame(),
               "Where is the SVGOuterSVGFrame's anon child??");
  }
  MOZ_ASSERT(aFrame->IsSVGContainerFrame(),
             "Children-only transforms only expected on SVG frames");
  return aFrame;
}

// This function tries to optimize a position style change by either
// moving aFrame or ignoring the style change when it's safe to do so.
// It returns true when that succeeds, otherwise it posts a reflow request
// and returns false.
static bool RecomputePosition(nsIFrame* aFrame) {
  // It's pointless to move around frames that have never been reflowed or
  // are dirty (i.e. they will be reflowed), or aren't affected by position
  // styles.
  if (aFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                              NS_FRAME_SVG_LAYOUT)) {
    return true;
  }

  // Don't process position changes on table frames, since we already handle
  // the dynamic position change on the table wrapper frame, and the
  // reflow-based fallback code path also ignores positions on inner table
  // frames.
  if (aFrame->IsTableFrame()) {
    return true;
  }

  const nsStyleDisplay* display = aFrame->StyleDisplay();
  // Changes to the offsets of a non-positioned element can safely be ignored.
  if (display->mPosition == StylePositionProperty::Static) {
    return true;
  }

  // Don't process position changes on frames which have views or the ones which
  // have a view somewhere in their descendants, because the corresponding view
  // needs to be repositioned properly as well.
  if (aFrame->HasView() ||
      aFrame->HasAnyStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW)) {
    return false;
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    // If the frame has an intrinsic block-size, we resolve its 'auto' margins
    // after doing layout, since we need to know the frame's block size. See
    // nsAbsoluteContainingBlock::ResolveAutoMarginsAfterLayout().
    //
    // Since the size of the frame doesn't change, we could modify the below
    // computation to compute the margin correctly without doing a full reflow,
    // however we decided to try doing a full reflow for now.
    if (aFrame->HasIntrinsicKeywordForBSize()) {
      WritingMode wm = aFrame->GetWritingMode();
      const auto* styleMargin = aFrame->StyleMargin();
      const auto positionProperty = aFrame->StyleDisplay()->mPosition;
      if (styleMargin->HasBlockAxisAuto(wm, positionProperty)) {
        return false;
      }
    }
    // Flexbox and Grid layout supports CSS Align and the optimizations below
    // don't support that yet.
    nsIFrame* ph = aFrame->GetPlaceholderFrame();
    if (ph && ph->HasAnyStateBits(PLACEHOLDER_STATICPOS_NEEDS_CSSALIGN)) {
      return false;
    }
  }

  // If we need to reposition any descendant that depends on our static
  // position, then we also can't take the optimized path.
  //
  // TODO(emilio): It may be worth trying to find them and try to call
  // RecomputePosition on them too instead of disabling the optimization...
  if (aFrame->DescendantMayDependOnItsStaticPosition()) {
    return false;
  }

  aFrame->SchedulePaint();

  auto postPendingScrollAnchorOrResnap = [](nsIFrame* frame) {
    if (frame->IsInScrollAnchorChain()) {
      ScrollAnchorContainer* container = ScrollAnchorContainer::FindFor(frame);
      frame->PresShell()->PostPendingScrollAnchorAdjustment(container);
    }

    // We need to trigger re-snapping to this content if we snapped to the
    // content on the last scroll operation.
    ScrollSnapUtils::PostPendingResnapIfNeededFor(frame);
  };

  // For relative positioning, we can simply update the frame rect
  if (display->IsRelativelyOrStickyPositionedStyle()) {
    if (aFrame->IsGridItem()) {
      // A grid item's CB is its grid area, not the parent frame content area
      // as is assumed below.
      return false;
    }
    // Move the frame
    if (display->mPosition == StylePositionProperty::Sticky) {
      // Update sticky positioning for an entire element at once, starting with
      // the first continuation or ib-split sibling.
      // It's rare that the frame we already have isn't already the first
      // continuation or ib-split sibling, but it can happen when styles differ
      // across continuations such as ::first-line or ::first-letter, and in
      // those cases we will generally (but maybe not always) do the work twice.
      nsIFrame* firstContinuation =
          nsLayoutUtils::FirstContinuationOrIBSplitSibling(aFrame);

      StickyScrollContainer::ComputeStickyOffsets(firstContinuation);
      StickyScrollContainer* ssc =
          StickyScrollContainer::GetStickyScrollContainerForFrame(
              firstContinuation);
      if (ssc) {
        ssc->PositionContinuations(firstContinuation);
      }
    } else {
      MOZ_ASSERT(display->IsRelativelyPositionedStyle(),
                 "Unexpected type of positioning");
      for (nsIFrame* cont = aFrame; cont;
           cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
        nsIFrame* cb = cont->GetContainingBlock();
        WritingMode wm = cb->GetWritingMode();
        const LogicalSize cbSize = cb->ContentSize();
        const LogicalMargin newLogicalOffsets =
            ReflowInput::ComputeRelativeOffsets(wm, cont, cbSize);
        const nsMargin newOffsets = newLogicalOffsets.GetPhysicalMargin(wm);

        // ReflowInput::ApplyRelativePositioning would work here, but
        // since we've already checked mPosition and aren't changing the frame's
        // normal position, go ahead and add the offsets directly.
        // First, we need to ensure that the normal position is stored though.
        bool hasProperty;
        nsPoint normalPosition = cont->GetNormalPosition(&hasProperty);
        if (!hasProperty) {
          cont->AddProperty(nsIFrame::NormalPositionProperty(), normalPosition);
        }
        cont->SetPosition(normalPosition +
                          nsPoint(newOffsets.left, newOffsets.top));
      }
    }

    postPendingScrollAnchorOrResnap(aFrame);
    return true;
  }

  // For the absolute positioning case, set up a fake HTML reflow input for
  // the frame, and then get the offsets and size from it. If the frame's size
  // doesn't need to change, we can simply update the frame position. Otherwise
  // we fall back to a reflow.
  UniquePtr<gfxContext> rc =
      aFrame->PresShell()->CreateReferenceRenderingContext();

  // Construct a bogus parent reflow input so that there's a usable reflow input
  // for the containing block.
  nsIFrame* parentFrame = aFrame->GetParent();
  WritingMode parentWM = parentFrame->GetWritingMode();
  WritingMode frameWM = aFrame->GetWritingMode();
  LogicalSize parentSize = parentFrame->GetLogicalSize();

  nsFrameState savedState = parentFrame->GetStateBits();
  ReflowInput parentReflowInput(aFrame->PresContext(), parentFrame, rc.get(),
                                parentSize);
  parentFrame->RemoveStateBits(~nsFrameState(0));
  parentFrame->AddStateBits(savedState);

  // The bogus parent state here was created with no parent state of its own,
  // and therefore it won't have an mCBReflowInput set up.
  // But we may need one (for InitCBReflowInput in a child state), so let's
  // try to create one here for the cases where it will be needed.
  Maybe<ReflowInput> cbReflowInput;
  nsIFrame* cbFrame = parentFrame->GetContainingBlock();
  if (cbFrame && (aFrame->GetContainingBlock() != parentFrame ||
                  parentFrame->IsTableFrame())) {
    const auto cbWM = cbFrame->GetWritingMode();
    LogicalSize cbSize = cbFrame->GetLogicalSize();
    cbReflowInput.emplace(cbFrame->PresContext(), cbFrame, rc.get(), cbSize);
    cbReflowInput->SetComputedLogicalMargin(
        cbWM, cbFrame->GetLogicalUsedMargin(cbWM));
    cbReflowInput->SetComputedLogicalPadding(
        cbWM, cbFrame->GetLogicalUsedPadding(cbWM));
    cbReflowInput->SetComputedLogicalBorderPadding(
        cbWM, cbFrame->GetLogicalUsedBorderAndPadding(cbWM));
    parentReflowInput.mCBReflowInput = cbReflowInput.ptr();
  }

  NS_WARNING_ASSERTION(parentSize.ISize(parentWM) != NS_UNCONSTRAINEDSIZE &&
                           parentSize.BSize(parentWM) != NS_UNCONSTRAINEDSIZE,
                       "parentSize should be valid");
  parentReflowInput.SetComputedISize(std::max(parentSize.ISize(parentWM), 0));
  parentReflowInput.SetComputedBSize(std::max(parentSize.BSize(parentWM), 0));
  parentReflowInput.SetComputedLogicalMargin(parentWM, LogicalMargin(parentWM));

  parentReflowInput.SetComputedLogicalPadding(
      parentWM, parentFrame->GetLogicalUsedPadding(parentWM));
  parentReflowInput.SetComputedLogicalBorderPadding(
      parentWM, parentFrame->GetLogicalUsedBorderAndPadding(parentWM));
  LogicalSize availSize = parentSize.ConvertTo(frameWM, parentWM);
  availSize.BSize(frameWM) = NS_UNCONSTRAINEDSIZE;

  ViewportFrame* viewport = do_QueryFrame(parentFrame);
  nsSize cbSize =
      viewport
          ? viewport->AdjustReflowInputAsContainingBlock(&parentReflowInput)
                .Size()
          : aFrame->GetContainingBlock()->GetSize();
  const nsMargin& parentBorder =
      parentReflowInput.mStyleBorder->GetComputedBorder();
  cbSize -= nsSize(parentBorder.LeftRight(), parentBorder.TopBottom());
  LogicalSize lcbSize(frameWM, cbSize);
  ReflowInput reflowInput(aFrame->PresContext(), parentReflowInput, aFrame,
                          availSize, Some(lcbSize));
  nscoord computedISize = reflowInput.ComputedISize();
  nscoord computedBSize = reflowInput.ComputedBSize();
  const auto frameBP = reflowInput.ComputedLogicalBorderPadding(frameWM);
  computedISize += frameBP.IStartEnd(frameWM);
  if (computedBSize != NS_UNCONSTRAINEDSIZE) {
    computedBSize += frameBP.BStartEnd(frameWM);
  }
  LogicalSize logicalSize = aFrame->GetLogicalSize(frameWM);
  nsSize size = aFrame->GetSize();
  // The RecomputePosition hint is not used if any offset changed between auto
  // and non-auto. If computedSize.height == NS_UNCONSTRAINEDSIZE then the new
  // element height will be its intrinsic height, and since 'top' and 'bottom''s
  // auto-ness hasn't changed, the old height must also be its intrinsic
  // height, which we can assume hasn't changed (or reflow would have
  // been triggered).
  if (computedISize == logicalSize.ISize(frameWM) &&
      (computedBSize == NS_UNCONSTRAINEDSIZE ||
       computedBSize == logicalSize.BSize(frameWM))) {
    // If we're solving for 'left' or 'top', then compute it here, in order to
    // match the reflow code path.
    //
    // TODO(emilio): It'd be nice if this did logical math instead, but it seems
    // to me the math should work out on vertical writing modes as well. See Bug
    // 1675861 for some hints.
    const nsMargin offset = reflowInput.ComputedPhysicalOffsets();
    const nsMargin margin = reflowInput.ComputedPhysicalMargin();

    nscoord left = offset.left;
    if (left == NS_AUTOOFFSET) {
      left =
          cbSize.width - offset.right - margin.right - size.width - margin.left;
    }

    nscoord top = offset.top;
    if (top == NS_AUTOOFFSET) {
      top = cbSize.height - offset.bottom - margin.bottom - size.height -
            margin.top;
    }

    // Move the frame
    nsPoint pos(parentBorder.left + left + margin.left,
                parentBorder.top + top + margin.top);
    aFrame->SetPosition(pos);

    postPendingScrollAnchorOrResnap(aFrame);
    return true;
  }

  // Fall back to a reflow
  return false;
}

/**
 * Return true if aFrame's subtree has placeholders for out-of-flow content
 * that would be affected due to the change to
 * `aPossiblyChangingContainingBlock` (and thus would need to get reframed).
 *
 * In particular, this function returns true if there are placeholders whose OOF
 * frames may need to be reparented (via reframing) as a result of whatever
 * change actually happened.
 *
 * The `aIs{Abs,Fixed}PosContainingBlock` params represent whether
 * `aPossiblyChangingContainingBlock` is a containing block for abs pos / fixed
 * pos stuff, respectively, for the _new_ style that the frame already has, not
 * the old one.
 */
static bool ContainingBlockChangeAffectsDescendants(
    nsIFrame* aPossiblyChangingContainingBlock, nsIFrame* aFrame,
    bool aIsAbsPosContainingBlock, bool aIsFixedPosContainingBlock) {
  // All fixed-pos containing blocks should also be abs-pos containing blocks.
  MOZ_ASSERT_IF(aIsFixedPosContainingBlock, aIsAbsPosContainingBlock);

  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* f : childList.mList) {
      if (f->IsPlaceholderFrame()) {
        nsIFrame* outOfFlow = nsPlaceholderFrame::GetRealFrameForPlaceholder(f);
        // If SVG text frames could appear here, they could confuse us since
        // they ignore their position style ... but they can't.
        NS_ASSERTION(!outOfFlow->IsInSVGTextSubtree(),
                     "SVG text frames can't be out of flow");
        // Top-layer frames don't change containing block based on direct
        // ancestors.
        auto* display = outOfFlow->StyleDisplay();
        if (display->IsAbsolutelyPositionedStyle() &&
            display->mTopLayer == StyleTopLayer::None) {
          const bool isContainingBlock =
              aIsFixedPosContainingBlock ||
              (aIsAbsPosContainingBlock &&
               display->mPosition == StylePositionProperty::Absolute);
          // NOTE(emilio): aPossiblyChangingContainingBlock is guaranteed to be
          // a first continuation, see the assertion in the caller.
          nsIFrame* parent = outOfFlow->GetParent()->FirstContinuation();
          if (isContainingBlock) {
            // If we are becoming a containing block, we only need to reframe if
            // this oof's current containing block is an ancestor of the new
            // frame.
            if (parent != aPossiblyChangingContainingBlock &&
                nsLayoutUtils::IsProperAncestorFrame(
                    parent, aPossiblyChangingContainingBlock)) {
              return true;
            }
          } else {
            // If we are not a containing block anymore, we only need to reframe
            // if we are the current containing block of the oof frame.
            if (parent == aPossiblyChangingContainingBlock) {
              return true;
            }
          }
        }
      }
      // NOTE:  It's tempting to check f->IsAbsPosContainingBlock() or
      // f->IsFixedPosContainingBlock() here.  However, that would only
      // be testing the *new* style of the frame, which might exclude
      // descendants that currently have this frame as an abs-pos
      // containing block.  Taking the codepath where we don't reframe
      // could lead to an unsafe call to
      // cont->MarkAsNotAbsoluteContainingBlock() before we've reframed
      // the descendant and taken it off the absolute list.
      if (ContainingBlockChangeAffectsDescendants(
              aPossiblyChangingContainingBlock, f, aIsAbsPosContainingBlock,
              aIsFixedPosContainingBlock)) {
        return true;
      }
    }
  }
  return false;
}

// Returns the frame that would serve as the containing block for aFrame's
// positioned descendants, if aFrame had styles to make it a CB for such
// descendants. (Typically this is just aFrame itself, or its insertion frame).
//
// Returns nullptr if this frame can't be easily determined.
static nsIFrame* ContainingBlockForFrame(nsIFrame* aFrame) {
  if (aFrame->IsFieldSetFrame()) {
    // FIXME: This should be easily implementable.
    return nullptr;
  }
  nsIFrame* insertionFrame = aFrame->GetContentInsertionFrame();
  if (insertionFrame == aFrame) {
    return insertionFrame;
  }
  // Generally frames with a different insertion frame are hard to deal with,
  // but scrollframes are easy because the containing block is just the
  // insertion frame.
  if (aFrame->IsScrollContainerFrame()) {
    return insertionFrame;
  }
  // Combobox frames are easy as well because they can't have positioned
  // children anyways.
  // Button and table cell frames are also easy because the containing block is
  // the frame itself.
  if (aFrame->IsComboboxControlFrame() || aFrame->IsHTMLButtonControlFrame() ||
      aFrame->IsTableCellFrame()) {
    return aFrame;
  }
  return nullptr;
}

static bool NeedToReframeToUpdateContainingBlock(nsIFrame* aFrame,
                                                 nsIFrame* aMaybeChangingCB) {
  // NOTE: This looks at the new style.
  const bool isFixedContainingBlock = aFrame->IsFixedPosContainingBlock();
  MOZ_ASSERT_IF(isFixedContainingBlock, aFrame->IsAbsPosContainingBlock());

  const bool isAbsPosContainingBlock =
      isFixedContainingBlock || aFrame->IsAbsPosContainingBlock();

  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
    if (ContainingBlockChangeAffectsDescendants(aMaybeChangingCB, f,
                                                isAbsPosContainingBlock,
                                                isFixedContainingBlock)) {
      return true;
    }
  }
  return false;
}

static void DoApplyRenderingChangeToTree(nsIFrame* aFrame,
                                         nsChangeHint aChange) {
  MOZ_ASSERT(gInApplyRenderingChangeToTree,
             "should only be called within ApplyRenderingChangeToTree");

  for (; aFrame;
       aFrame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame)) {
    // Invalidate and sync views on all descendant frames, following
    // placeholders. We don't need to update transforms in
    // SyncViewsAndInvalidateDescendants, because there can't be any
    // out-of-flows or popups that need to be transformed; all out-of-flow
    // descendants of the transformed element must also be descendants of the
    // transformed frame.
    SyncViewsAndInvalidateDescendants(
        aFrame, nsChangeHint(aChange & (nsChangeHint_RepaintFrame |
                                        nsChangeHint_UpdateOpacityLayer |
                                        nsChangeHint_SchedulePaint)));
    // This must be set to true if the rendering change needs to
    // invalidate content.  If it's false, a composite-only paint
    // (empty transaction) will be scheduled.
    bool needInvalidatingPaint = false;

    // if frame has view, will already be invalidated
    if (aChange & nsChangeHint_RepaintFrame) {
      // Note that this whole block will be skipped when painting is suppressed
      // (due to our caller ApplyRendingChangeToTree() discarding the
      // nsChangeHint_RepaintFrame hint).  If you add handling for any other
      // hints within this block, be sure that they too should be ignored when
      // painting is suppressed.
      needInvalidatingPaint = true;
      aFrame->InvalidateFrameSubtree();
      if ((aChange & nsChangeHint_UpdateEffects) &&
          aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
        // Need to update our overflow rects:
        SVGUtils::ScheduleReflowSVG(aFrame);
      }

      ActiveLayerTracker::NotifyNeedsRepaint(aFrame);
    }
    if (aChange & nsChangeHint_UpdateOpacityLayer) {
      // FIXME/bug 796697: we can get away with empty transactions for
      // opacity updates in many cases.
      needInvalidatingPaint = true;

      ActiveLayerTracker::NotifyRestyle(aFrame, eCSSProperty_opacity);
      if (SVGIntegrationUtils::UsingEffectsForFrame(aFrame)) {
        // SVG effects paints the opacity without using
        // nsDisplayOpacity. We need to invalidate manually.
        aFrame->InvalidateFrameSubtree();
      }
    }
    if ((aChange & nsChangeHint_UpdateTransformLayer) &&
        aFrame->IsTransformed()) {
      // Note: All the transform-like properties should map to the same
      // layer activity index, so does the restyle count. Therefore, using
      // eCSSProperty_transform should be fine.
      ActiveLayerTracker::NotifyRestyle(aFrame, eCSSProperty_transform);
      needInvalidatingPaint = true;
    }
    if (aChange & nsChangeHint_ChildrenOnlyTransform) {
      needInvalidatingPaint = true;
      nsIFrame* childFrame = GetFrameForChildrenOnlyTransformHint(aFrame)
                                 ->PrincipalChildList()
                                 .FirstChild();
      for (; childFrame; childFrame = childFrame->GetNextSibling()) {
        // Note: All the transform-like properties should map to the same
        // layer activity index, so does the restyle count. Therefore, using
        // eCSSProperty_transform should be fine.
        ActiveLayerTracker::NotifyRestyle(childFrame, eCSSProperty_transform);
      }
    }
    if (aChange & nsChangeHint_SchedulePaint) {
      needInvalidatingPaint = true;
    }
    aFrame->SchedulePaint(needInvalidatingPaint
                              ? nsIFrame::PAINT_DEFAULT
                              : nsIFrame::PAINT_COMPOSITE_ONLY);
  }
}

static void SyncViewsAndInvalidateDescendants(nsIFrame* aFrame,
                                              nsChangeHint aChange) {
  MOZ_ASSERT(gInApplyRenderingChangeToTree,
             "should only be called within ApplyRenderingChangeToTree");

  NS_ASSERTION(nsChangeHint_size_t(aChange) ==
                   (aChange & (nsChangeHint_RepaintFrame |
                               nsChangeHint_UpdateOpacityLayer |
                               nsChangeHint_SchedulePaint)),
               "Invalid change flag");

  aFrame->SyncFrameViewProperties();

  for (const auto& [list, listID] : aFrame->ChildLists()) {
    for (nsIFrame* child : list) {
      if (!child->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
        // only do frames that don't have placeholders
        if (child->IsPlaceholderFrame()) {
          // do the out-of-flow frame and its continuations
          nsIFrame* outOfFlowFrame =
              nsPlaceholderFrame::GetRealFrameForPlaceholder(child);
          DoApplyRenderingChangeToTree(outOfFlowFrame, aChange);
        } else {  // regular frame
          SyncViewsAndInvalidateDescendants(child, aChange);
        }
      }
    }
  }
}

static void ApplyRenderingChangeToTree(PresShell* aPresShell, nsIFrame* aFrame,
                                       nsChangeHint aChange) {
  // We check StyleDisplay()->HasTransformStyle() in addition to checking
  // IsTransformed() since we can get here for some frames that don't support
  // CSS transforms, and table frames, which are their own odd-ball, since the
  // transform is handled by their wrapper, which _also_ gets a separate hint.
  NS_ASSERTION(!(aChange & nsChangeHint_UpdateTransformLayer) ||
                   aFrame->IsTransformed() ||
                   aFrame->StyleDisplay()->HasTransformStyle(),
               "Unexpected UpdateTransformLayer hint");

  if (aPresShell->IsPaintingSuppressed()) {
    // Don't allow synchronous rendering changes when painting is turned off.
    aChange &= ~nsChangeHint_RepaintFrame;
    if (!aChange) {
      return;
    }
  }

// Trigger rendering updates by damaging this frame and any
// continuations of this frame.
#ifdef DEBUG
  gInApplyRenderingChangeToTree = true;
#endif
  if (aChange & nsChangeHint_RepaintFrame) {
    // If the frame is the primary frame of either the body element or
    // the html element, we propagate the repaint change hint to the
    // viewport. This is necessary for background and scrollbar colors
    // propagation.
    if (aFrame->ShouldPropagateRepaintsToRoot()) {
      nsIFrame* rootFrame = aPresShell->GetRootFrame();
      MOZ_ASSERT(rootFrame, "No root frame?");
      DoApplyRenderingChangeToTree(rootFrame, nsChangeHint_RepaintFrame);
      aChange &= ~nsChangeHint_RepaintFrame;
      if (!aChange) {
        return;
      }
    }
  }
  DoApplyRenderingChangeToTree(aFrame, aChange);
#ifdef DEBUG
  gInApplyRenderingChangeToTree = false;
#endif
}

static void AddSubtreeToOverflowTracker(
    nsIFrame* aFrame, OverflowChangedTracker& aOverflowChangedTracker) {
  if (aFrame->FrameMaintainsOverflow()) {
    aOverflowChangedTracker.AddFrame(aFrame,
                                     OverflowChangedTracker::CHILDREN_CHANGED);
  }
  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      AddSubtreeToOverflowTracker(child, aOverflowChangedTracker);
    }
  }
}

static void StyleChangeReflow(nsIFrame* aFrame, nsChangeHint aHint) {
  IntrinsicDirty dirtyType;
  if (aHint & nsChangeHint_ClearDescendantIntrinsics) {
    NS_ASSERTION(aHint & nsChangeHint_ClearAncestorIntrinsics,
                 "Please read the comments in nsChangeHint.h");
    NS_ASSERTION(aHint & nsChangeHint_NeedDirtyReflow,
                 "ClearDescendantIntrinsics requires NeedDirtyReflow");
    dirtyType = IntrinsicDirty::FrameAncestorsAndDescendants;
  } else if ((aHint & nsChangeHint_UpdateComputedBSize) &&
             aFrame->HasAnyStateBits(
                 NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE)) {
    dirtyType = IntrinsicDirty::FrameAncestorsAndDescendants;
  } else if (aHint & nsChangeHint_ClearAncestorIntrinsics) {
    dirtyType = IntrinsicDirty::FrameAndAncestors;
  } else {
    dirtyType = IntrinsicDirty::None;
  }

  if (aHint & nsChangeHint_UpdateComputedBSize) {
    aFrame->SetHasBSizeChange(true);
  }

  nsFrameState dirtyBits;
  if (aFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    dirtyBits = nsFrameState(0);
  } else if ((aHint & nsChangeHint_NeedDirtyReflow) ||
             dirtyType == IntrinsicDirty::FrameAncestorsAndDescendants) {
    dirtyBits = NS_FRAME_IS_DIRTY;
  } else {
    dirtyBits = NS_FRAME_HAS_DIRTY_CHILDREN;
  }

  // If we're not going to clear any intrinsic sizes on the frames, and
  // there are no dirty bits to set, then there's nothing to do.
  if (dirtyType == IntrinsicDirty::None && !dirtyBits) {
    return;
  }

  ReflowRootHandling rootHandling;
  if (aHint & nsChangeHint_ReflowChangesSizeOrPosition) {
    rootHandling = ReflowRootHandling::PositionOrSizeChange;
  } else {
    rootHandling = ReflowRootHandling::NoPositionOrSizeChange;
  }

  do {
    aFrame->PresShell()->FrameNeedsReflow(aFrame, dirtyType, dirtyBits,
                                          rootHandling);
    aFrame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame);
  } while (aFrame);
}

// Get the next sibling which might have a frame.  This only considers siblings
// that stylo post-traversal looks at, so only elements and text.  In
// particular, it ignores comments.
static nsIContent* NextSiblingWhichMayHaveFrame(nsIContent* aContent) {
  for (nsIContent* next = aContent->GetNextSibling(); next;
       next = next->GetNextSibling()) {
    if (next->IsElement() || next->IsText()) {
      return next;
    }
  }

  return nullptr;
}

// If |aFrame| is dirty or has dirty children, then we can skip updating
// overflows since that will happen when it's reflowed.
static inline bool CanSkipOverflowUpdates(const nsIFrame* aFrame) {
  return aFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY |
                                 NS_FRAME_HAS_DIRTY_CHILDREN);
}

static inline void TryToDealWithScrollbarChange(nsChangeHint& aHint,
                                                nsIContent* aContent,
                                                nsIFrame* aFrame,
                                                nsPresContext* aPc) {
  if (!(aHint & nsChangeHint_ScrollbarChange)) {
    return;
  }
  aHint &= ~nsChangeHint_ScrollbarChange;
  if (aHint & nsChangeHint_ReconstructFrame) {
    return;
  }

  MOZ_ASSERT(aFrame, "If we're not reframing, we ought to have a frame");

  const bool isRoot = aContent->IsInUncomposedDoc() && !aContent->GetParent();

  // Only bother with this if we're the root or the body element, since:
  //  (a) It'd be *expensive* to reframe these particular nodes.  They're
  //      at the root, so reframing would mean rebuilding the world.
  //  (b) It's often *unnecessary* to reframe for "overflow" changes on
  //      these particular nodes.  In general, the only reason we reframe
  //      for "overflow" changes is so we can construct (or destroy) a
  //      scrollframe & scrollbars -- and the html/body nodes often don't
  //      need their own scrollframe/scrollbars because they coopt the ones
  //      on the viewport (which always exist). So depending on whether
  //      that's happening, we can skip the reframe for these nodes.
  if (isRoot || aContent->IsHTMLElement(nsGkAtoms::body)) {
    // If the restyled element provided/provides the scrollbar styles for
    // the viewport before and/or after this restyle, AND it's not coopting
    // that responsibility from some other element (which would need
    // reconstruction to make its own scrollframe now), THEN: we don't need
    // to reconstruct - we can just reflow, because no scrollframe is being
    // added/removed.
    Element* prevOverride = aPc->GetViewportScrollStylesOverrideElement();
    Element* newOverride = aPc->UpdateViewportScrollStylesOverride();

    const auto ProvidesScrollbarStyles = [&](nsIContent* aOverride) {
      if (aOverride) {
        return aOverride == aContent;
      }
      return isRoot;
    };

    if (ProvidesScrollbarStyles(prevOverride) ||
        ProvidesScrollbarStyles(newOverride)) {
      // If we get here, the restyled element provided the scrollbar styles
      // for viewport before this restyle, OR it will provide them after.
      if (!prevOverride || !newOverride || prevOverride == newOverride) {
        // If we get here, the restyled element is NOT replacing (or being
        // replaced by) some other element as the viewport's
        // scrollbar-styles provider. (If it were, we'd potentially need to
        // reframe to create a dedicated scrollframe for whichever element
        // is being booted from providing viewport scrollbar styles.)
        //
        // Under these conditions, we're OK to assume that this "overflow"
        // change only impacts the root viewport's scrollframe, which
        // already exists, so we can simply reflow instead of reframing.
        if (ScrollContainerFrame* sf = do_QueryFrame(aFrame)) {
          sf->MarkScrollbarsDirtyForReflow();
        } else if (ScrollContainerFrame* sf =
                       aPc->PresShell()->GetRootScrollContainerFrame()) {
          sf->MarkScrollbarsDirtyForReflow();
        }
        aHint |= nsChangeHint_ReflowHintsForScrollbarChange;
      } else {
        // If we changed the override element, we need to reconstruct as the old
        // override element might start / stop being scrollable.
        aHint |= nsChangeHint_ReconstructFrame;
      }
      return;
    }
  }

  const bool scrollable = aFrame->StyleDisplay()->IsScrollableOverflow();
  if (ScrollContainerFrame* sf = do_QueryFrame(aFrame)) {
    if (scrollable && sf->HasAllNeededScrollbars()) {
      sf->MarkScrollbarsDirtyForReflow();
      // Once we've created scrollbars for a frame, don't bother reconstructing
      // it just to remove them if we still need a scroll frame.
      aHint |= nsChangeHint_ReflowHintsForScrollbarChange;
      return;
    }
  } else if (aFrame->IsTextInputFrame()) {
    // input / textarea for the most part don't honor overflow themselves, the
    // editor root will deal with the change if needed.
    // However the textarea intrinsic size relies on GetDesiredScrollbarSizes(),
    // so we need to reflow the textarea itself, not just the inner control.
    aHint |= nsChangeHint_ReflowHintsForScrollbarChange;
    return;
  } else if (!scrollable) {
    // Something changed, but we don't have nor will have a scroll frame,
    // there's nothing to do here.
    return;
  }

  // Oh well, we couldn't optimize it out, just reconstruct frames for the
  // subtree.
  aHint |= nsChangeHint_ReconstructFrame;
}

static void TryToHandleContainingBlockChange(nsChangeHint& aHint,
                                             nsIFrame* aFrame) {
  if (!(aHint & nsChangeHint_UpdateContainingBlock)) {
    return;
  }
  if (aHint & nsChangeHint_ReconstructFrame) {
    return;
  }
  MOZ_ASSERT(aFrame, "If we're not reframing, we ought to have a frame");
  nsIFrame* containingBlock = ContainingBlockForFrame(aFrame);
  if (!containingBlock ||
      NeedToReframeToUpdateContainingBlock(aFrame, containingBlock)) {
    // The frame has positioned children that need to be reparented, or it can't
    // easily be converted to/from being an abs-pos container correctly.
    aHint |= nsChangeHint_ReconstructFrame;
    return;
  }
  const bool isCb = aFrame->IsAbsPosContainingBlock();

  // The absolute container should be containingBlock.
  for (nsIFrame* cont = containingBlock; cont;
       cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
    // Normally frame construction would set state bits as needed,
    // but we're not going to reconstruct the frame so we need to set
    // them. It's because we need to set this state on each affected frame
    // that we can't coalesce nsChangeHint_UpdateContainingBlock hints up
    // to ancestors (i.e. it can't be an change hint that is handled for
    // descendants).
    if (isCb) {
      if (!cont->IsAbsoluteContainer() &&
          cont->HasAnyStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN)) {
        cont->MarkAsAbsoluteContainingBlock();
      }
    } else if (cont->IsAbsoluteContainer()) {
      if (cont->HasAbsolutelyPositionedChildren()) {
        // If |cont| still has absolutely positioned children,
        // we can't call MarkAsNotAbsoluteContainingBlock.  This
        // will remove a frame list that still has children in
        // it that we need to keep track of.
        // The optimization of removing it isn't particularly
        // important, although it does mean we skip some tests.
        NS_WARNING("skipping removal of absolute containing block");
      } else {
        cont->MarkAsNotAbsoluteContainingBlock();
      }
    }
  }
}

void RestyleManager::ProcessRestyledFrames(nsStyleChangeList& aChangeList) {
  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot a script blocker");

  // See bug 1378219 comment 9:
  // Recursive calls here are a bit worrying, but apparently do happen in the
  // wild (although not currently in any of our automated tests). Try to get a
  // stack from Nightly/Dev channel to figure out what's going on and whether
  // it's OK.
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyedFrames, "ProcessRestyledFrames recursion");

  if (aChangeList.IsEmpty()) {
    return;
  }

  // If mDestroyedFrames is null, we want to create a new hashtable here
  // and destroy it on exit; but if it is already non-null (because we're in
  // a recursive call), we will continue to use the existing table to
  // accumulate destroyed frames, and NOT clear mDestroyedFrames on exit.
  // We use a MaybeClearDestroyedFrames helper to conditionally reset the
  // mDestroyedFrames pointer when this method returns.
  typedef decltype(mDestroyedFrames) DestroyedFramesT;
  class MOZ_RAII MaybeClearDestroyedFrames {
   private:
    DestroyedFramesT& mDestroyedFramesRef;  // ref to caller's mDestroyedFrames
    const bool mResetOnDestruction;

   public:
    explicit MaybeClearDestroyedFrames(DestroyedFramesT& aTarget)
        : mDestroyedFramesRef(aTarget),
          mResetOnDestruction(!aTarget)  // reset only if target starts out null
    {}
    ~MaybeClearDestroyedFrames() {
      if (mResetOnDestruction) {
        mDestroyedFramesRef.reset(nullptr);
      }
    }
  };

  MaybeClearDestroyedFrames maybeClear(mDestroyedFrames);
  if (!mDestroyedFrames) {
    mDestroyedFrames = MakeUnique<nsTHashSet<const nsIFrame*>>();
  }

  AUTO_PROFILER_LABEL("RestyleManager::ProcessRestyledFrames", LAYOUT);

  nsPresContext* presContext = PresContext();
  nsCSSFrameConstructor* frameConstructor = presContext->FrameConstructor();

  bool didUpdateCursor = false;

  for (size_t i = 0; i < aChangeList.Length(); ++i) {
    // Collect and coalesce adjacent siblings for lazy frame construction.
    // Eventually it would be even better to make RecreateFramesForContent
    // accept a range and coalesce all adjacent reconstructs (bug 1344139).
    size_t lazyRangeStart = i;
    while (i < aChangeList.Length() && aChangeList[i].mContent &&
           aChangeList[i].mContent->HasFlag(NODE_NEEDS_FRAME) &&
           (i == lazyRangeStart ||
            NextSiblingWhichMayHaveFrame(aChangeList[i - 1].mContent) ==
                aChangeList[i].mContent)) {
      MOZ_ASSERT(aChangeList[i].mHint & nsChangeHint_ReconstructFrame);
      MOZ_ASSERT(!aChangeList[i].mFrame);
      ++i;
    }
    if (i != lazyRangeStart) {
      nsIContent* start = aChangeList[lazyRangeStart].mContent;
      nsIContent* end =
          NextSiblingWhichMayHaveFrame(aChangeList[i - 1].mContent);
      if (!end) {
        frameConstructor->ContentAppended(
            start, nsCSSFrameConstructor::InsertionKind::Sync);
      } else {
        frameConstructor->ContentRangeInserted(
            start, end, nsCSSFrameConstructor::InsertionKind::Sync);
      }
    }
    for (size_t j = lazyRangeStart; j < i; ++j) {
      MOZ_ASSERT(!aChangeList[j].mContent->GetPrimaryFrame() ||
                 !aChangeList[j].mContent->HasFlag(NODE_NEEDS_FRAME));
    }
    if (i == aChangeList.Length()) {
      break;
    }

    const nsStyleChangeData& data = aChangeList[i];
    nsIFrame* frame = data.mFrame;
    nsIContent* content = data.mContent;
    nsChangeHint hint = data.mHint;
    bool didReflowThisFrame = false;

    NS_ASSERTION(!(hint & nsChangeHint_AllReflowHints) ||
                     (hint & nsChangeHint_NeedReflow),
                 "Reflow hint bits set without actually asking for a reflow");

    // skip any frame that has been destroyed due to a ripple effect
    if (frame && mDestroyedFrames->Contains(frame)) {
      continue;
    }

    if (frame && frame->GetContent() != content) {
      // XXXbz this is due to image maps messing with the primary frame of
      // <area>s.  See bug 135040.  Remove this block once that's fixed.
      frame = nullptr;
      if (!(hint & nsChangeHint_ReconstructFrame)) {
        continue;
      }
    }

    TryToDealWithScrollbarChange(hint, content, frame, presContext);
    TryToHandleContainingBlockChange(hint, frame);

    if (hint & nsChangeHint_ReconstructFrame) {
      // If we ever start passing true here, be careful of restyles
      // that involve a reframe and animations.  In particular, if the
      // restyle we're processing here is an animation restyle, but
      // the style resolution we will do for the frame construction
      // happens async when we're not in an animation restyle already,
      // problems could arise.
      // We could also have problems with triggering of CSS transitions
      // on elements whose frames are reconstructed, since we depend on
      // the reconstruction happening synchronously.
      frameConstructor->RecreateFramesForContent(
          content, nsCSSFrameConstructor::InsertionKind::Sync);
      continue;
    }

    MOZ_ASSERT(frame, "This shouldn't happen");
    if (hint & nsChangeHint_AddOrRemoveTransform) {
      for (nsIFrame* cont = frame; cont;
           cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
        if (cont->StyleDisplay()->HasTransform(cont)) {
          cont->AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
        }
        // Don't remove NS_FRAME_MAY_BE_TRANSFORMED since it may still be
        // transformed by other means. It's OK to have the bit even if it's
        // not needed.
      }
      // When dropping a running transform animation we will first add an
      // nsChangeHint_UpdateTransformLayer hint as part of the animation-only
      // restyle. During the subsequent regular restyle, if the animation was
      // the only reason the element had any transform applied, we will add
      // nsChangeHint_AddOrRemoveTransform as part of the regular restyle.
      //
      // With the Gecko backend, these two change hints are processed
      // after each restyle but when using the Servo backend they accumulate
      // and are processed together after we have already removed the
      // transform as part of the regular restyle. Since we don't actually
      // need the nsChangeHint_UpdateTransformLayer hint if we already have
      // a nsChangeHint_AddOrRemoveTransform hint, and since we
      // will fail an assertion in ApplyRenderingChangeToTree if we try
      // specify nsChangeHint_UpdateTransformLayer but don't have any
      // transform style, we just drop the unneeded hint here.
      hint &= ~nsChangeHint_UpdateTransformLayer;
    }

    if (!frame->FrameMaintainsOverflow()) {
      // frame does not maintain overflow rects, so avoid calling
      // FinishAndStoreOverflow on it:
      hint &=
          ~(nsChangeHint_UpdateOverflow | nsChangeHint_ChildrenOnlyTransform |
            nsChangeHint_UpdatePostTransformOverflow |
            nsChangeHint_UpdateParentOverflow |
            nsChangeHint_UpdateSubtreeOverflow);
    }

    if (!frame->HasAnyStateBits(NS_FRAME_MAY_BE_TRANSFORMED)) {
      // Frame can not be transformed, and thus a change in transform will
      // have no effect and we should not use either
      // nsChangeHint_UpdatePostTransformOverflow or
      // nsChangeHint_UpdateTransformLayerhint.
      hint &= ~(nsChangeHint_UpdatePostTransformOverflow |
                nsChangeHint_UpdateTransformLayer);
    }

    if ((hint & nsChangeHint_UpdateEffects) &&
        frame == nsLayoutUtils::FirstContinuationOrIBSplitSibling(frame)) {
      SVGObserverUtils::UpdateEffects(frame);
    }
    if ((hint & nsChangeHint_InvalidateRenderingObservers) ||
        ((hint & nsChangeHint_UpdateOpacityLayer) &&
         frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT))) {
      SVGObserverUtils::InvalidateRenderingObservers(frame);
      frame->SchedulePaint();
    }
    if (hint & nsChangeHint_NeedReflow) {
      StyleChangeReflow(frame, hint);
      didReflowThisFrame = true;
    }

    // Here we need to propagate repaint frame change hint instead of update
    // opacity layer change hint when we do opacity optimization for SVG.
    // We can't do it in nsStyleEffects::CalcDifference() just like we do
    // for the optimization for 0.99 over opacity values since we have no way
    // to call SVGUtils::CanOptimizeOpacity() there.
    if ((hint & nsChangeHint_UpdateOpacityLayer) &&
        SVGUtils::CanOptimizeOpacity(frame)) {
      hint &= ~nsChangeHint_UpdateOpacityLayer;
      hint |= nsChangeHint_RepaintFrame;
    }

    if ((hint & nsChangeHint_UpdateUsesOpacity) && frame->IsTablePart()) {
      NS_ASSERTION(hint & nsChangeHint_UpdateOpacityLayer,
                   "should only return UpdateUsesOpacity hint "
                   "when also returning UpdateOpacityLayer hint");
      // When an internal table part (including cells) changes between
      // having opacity 1 and non-1, it changes whether its
      // backgrounds (and those of table parts inside of it) are
      // painted as part of the table's nsDisplayTableBorderBackground
      // display item, or part of its own display item.  That requires
      // invalidation, so change UpdateOpacityLayer to RepaintFrame.
      hint &= ~nsChangeHint_UpdateOpacityLayer;
      hint |= nsChangeHint_RepaintFrame;
    }

    // Opacity disables preserve-3d, so if we toggle it, then we also need
    // to update the overflow areas of all potentially affected frames.
    if ((hint & nsChangeHint_UpdateUsesOpacity) &&
        frame->StyleDisplay()->mTransformStyle ==
            StyleTransformStyle::Preserve3d) {
      hint |= nsChangeHint_UpdateSubtreeOverflow;
    }

    if (hint & nsChangeHint_UpdateBackgroundPosition) {
      // For most frame types, DLBI can detect background position changes,
      // so we only need to schedule a paint.
      hint |= nsChangeHint_SchedulePaint;
      if (frame->IsTablePart() || frame->IsMathMLFrame()) {
        // Table parts and MathML frames don't build display items for their
        // backgrounds, so DLBI can't detect background-position changes for
        // these frames. Repaint the whole frame.
        hint |= nsChangeHint_RepaintFrame;
      }
    }

    if (hint &
        (nsChangeHint_RepaintFrame | nsChangeHint_UpdateOpacityLayer |
         nsChangeHint_UpdateTransformLayer |
         nsChangeHint_ChildrenOnlyTransform | nsChangeHint_SchedulePaint)) {
      ApplyRenderingChangeToTree(presContext->PresShell(), frame, hint);
    }

    if (hint & (nsChangeHint_UpdateTransformLayer |
                nsChangeHint_AddOrRemoveTransform)) {
      // We need to trigger re-snapping to this content if we snapped to the
      // content on the last scroll operation.
      ScrollSnapUtils::PostPendingResnapIfNeededFor(frame);
    }

    if ((hint & nsChangeHint_RecomputePosition) && !didReflowThisFrame) {
      // It is possible for this to fall back to a reflow
      if (!RecomputePosition(frame)) {
        StyleChangeReflow(frame, nsChangeHint_NeedReflow |
                                     nsChangeHint_ReflowChangesSizeOrPosition);
        didReflowThisFrame = true;
      }
    }
    NS_ASSERTION(!(hint & nsChangeHint_ChildrenOnlyTransform) ||
                     (hint & nsChangeHint_UpdateOverflow),
                 "nsChangeHint_UpdateOverflow should be passed too");
    if (!didReflowThisFrame &&
        (hint & (nsChangeHint_UpdateOverflow |
                 nsChangeHint_UpdatePostTransformOverflow |
                 nsChangeHint_UpdateParentOverflow |
                 nsChangeHint_UpdateSubtreeOverflow))) {
      if (hint & nsChangeHint_UpdateSubtreeOverflow) {
        for (nsIFrame* cont = frame; cont;
             cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
          AddSubtreeToOverflowTracker(cont, mOverflowChangedTracker);
        }
        // The work we just did in AddSubtreeToOverflowTracker
        // subsumes some of the other hints:
        hint &= ~(nsChangeHint_UpdateOverflow |
                  nsChangeHint_UpdatePostTransformOverflow);
      }
      if (hint & nsChangeHint_ChildrenOnlyTransform) {
        // We need to update overflows. The correct frame(s) to update depends
        // on whether the ChangeHint came from an outer or an inner svg.
        nsIFrame* hintFrame = GetFrameForChildrenOnlyTransformHint(frame);
        NS_ASSERTION(!nsLayoutUtils::GetNextContinuationOrIBSplitSibling(frame),
                     "SVG frames should not have continuations "
                     "or ib-split siblings");
        NS_ASSERTION(
            !nsLayoutUtils::GetNextContinuationOrIBSplitSibling(hintFrame),
            "SVG frames should not have continuations "
            "or ib-split siblings");
        if (hintFrame->IsSVGOuterSVGAnonChildFrame()) {
          // The children only transform of an outer svg frame is applied to
          // the outer svg's anonymous child frame (instead of to the
          // anonymous child's children).

          if (!CanSkipOverflowUpdates(hintFrame)) {
            mOverflowChangedTracker.AddFrame(
                hintFrame, OverflowChangedTracker::CHILDREN_CHANGED);
          }
        } else {
          // The children only transform is applied to the child frames of an
          // inner svg frame, so update the child overflows.
          nsIFrame* childFrame = hintFrame->PrincipalChildList().FirstChild();
          for (; childFrame; childFrame = childFrame->GetNextSibling()) {
            MOZ_ASSERT(childFrame->IsSVGFrame(),
                       "Not expecting non-SVG children");
            if (!CanSkipOverflowUpdates(childFrame)) {
              mOverflowChangedTracker.AddFrame(
                  childFrame, OverflowChangedTracker::CHILDREN_CHANGED);
            }
            NS_ASSERTION(
                !nsLayoutUtils::GetNextContinuationOrIBSplitSibling(childFrame),
                "SVG frames should not have continuations "
                "or ib-split siblings");
            NS_ASSERTION(
                childFrame->GetParent() == hintFrame,
                "SVG child frame not expected to have different parent");
          }
        }
      }
      if (!CanSkipOverflowUpdates(frame)) {
        if (hint & (nsChangeHint_UpdateOverflow |
                    nsChangeHint_UpdatePostTransformOverflow)) {
          OverflowChangedTracker::ChangeKind changeKind;
          // If we have both nsChangeHint_UpdateOverflow and
          // nsChangeHint_UpdatePostTransformOverflow,
          // CHILDREN_CHANGED is selected as it is
          // strictly stronger.
          if (hint & nsChangeHint_UpdateOverflow) {
            changeKind = OverflowChangedTracker::CHILDREN_CHANGED;
          } else {
            changeKind = OverflowChangedTracker::TRANSFORM_CHANGED;
          }
          for (nsIFrame* cont = frame; cont;
               cont =
                   nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
            mOverflowChangedTracker.AddFrame(cont, changeKind);
          }
        }
        // UpdateParentOverflow hints need to be processed in addition
        // to the above, since if the processing of the above hints
        // yields no change, the update will not propagate to the
        // parent.
        if (hint & nsChangeHint_UpdateParentOverflow) {
          MOZ_ASSERT(frame->GetParent(),
                     "shouldn't get style hints for the root frame");
          for (nsIFrame* cont = frame; cont;
               cont =
                   nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
            mOverflowChangedTracker.AddFrame(
                cont->GetParent(), OverflowChangedTracker::CHILDREN_CHANGED);
          }
        }
      }
    }
    if ((hint & nsChangeHint_UpdateCursor) && !didUpdateCursor) {
      presContext->PresShell()->SynthesizeMouseMove(false);
      didUpdateCursor = true;
    }
    if (hint & nsChangeHint_UpdateTableCellSpans) {
      frameConstructor->UpdateTableCellSpans(content);
    }
    if (hint & nsChangeHint_VisibilityChange) {
      frame->UpdateVisibleDescendantsState();
    }
  }

  aChangeList.Clear();
  FlushOverflowChangedTracker();
}

/* static */
uint64_t RestyleManager::GetAnimationGenerationForFrame(nsIFrame* aStyleFrame) {
  EffectSet* effectSet = EffectSet::GetForStyleFrame(aStyleFrame);
  return effectSet ? effectSet->GetAnimationGeneration() : 0;
}

/* static */
void RestyleManager::AddLayerChangesForAnimation(
    nsIFrame* aStyleFrame, nsIFrame* aPrimaryFrame, Element* aElement,
    nsChangeHint aHintForThisFrame, nsStyleChangeList& aChangeListToProcess) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(!!aStyleFrame == !!aPrimaryFrame);
  if (!aStyleFrame) {
    return;
  }

  uint64_t frameGeneration =
      RestyleManager::GetAnimationGenerationForFrame(aStyleFrame);

  Maybe<nsCSSPropertyIDSet> effectiveAnimationProperties;

  nsChangeHint hint = nsChangeHint(0);
  auto maybeApplyChangeHint = [&](const Maybe<uint64_t>& aGeneration,
                                  DisplayItemType aDisplayItemType) -> bool {
    if (aGeneration && frameGeneration != *aGeneration) {
      // If we have a transform layer but don't have any transform style, we
      // probably just removed the transform but haven't destroyed the layer
      // yet. In this case we will typically add the appropriate change hint
      // (nsChangeHint_UpdateContainingBlock) when we compare styles so in
      // theory we could skip adding any change hint here.
      //
      // However, sometimes when we compare styles we'll get no change. For
      // example, if the transform style was 'none' when we sent the transform
      // animation to the compositor and the current transform style is now
      // 'none' we'll think nothing changed but actually we still need to
      // trigger an update to clear whatever style the transform animation set
      // on the compositor. To handle this case we simply set all the change
      // hints relevant to removing transform style (since we don't know exactly
      // what changes happened while the animation was running on the
      // compositor).
      //
      // Note that we *don't* add nsChangeHint_UpdateTransformLayer since if we
      // did, ApplyRenderingChangeToTree would complain that we're updating a
      // transform layer without a transform.
      if (aDisplayItemType == DisplayItemType::TYPE_TRANSFORM &&
          !aStyleFrame->StyleDisplay()->HasTransformStyle()) {
        // Add all the hints for a removing a transform if they are not already
        // set for this frame.
        if (!(NS_IsHintSubset(nsChangeHint_ComprehensiveAddOrRemoveTransform,
                              aHintForThisFrame))) {
          hint |= nsChangeHint_ComprehensiveAddOrRemoveTransform;
        }
        return true;
      }
      hint |= LayerAnimationInfo::GetChangeHintFor(aDisplayItemType);
    }

    // We consider it's the first paint for the frame if we have an animation
    // for the property but have no layer, for the case of WebRender,  no
    // corresponding animation info.
    // Note that in case of animations which has properties preventing running
    // on the compositor, e.g., width or height, corresponding layer is not
    // created at all, but even in such cases, we normally set valid change
    // hint for such animations in each tick, i.e. restyles in each tick. As
    // a result, we usually do restyles for such animations in every tick on
    // the main-thread.  The only animations which will be affected by this
    // explicit change hint are animations that have opacity/transform but did
    // not have those properies just before. e.g, setting transform by
    // setKeyframes or changing target element from other target which prevents
    // running on the compositor, etc.
    if (!aGeneration) {
      nsChangeHint hintForDisplayItem =
          LayerAnimationInfo::GetChangeHintFor(aDisplayItemType);
      // We don't need to apply the corresponding change hint if we already have
      // it.
      if (NS_IsHintSubset(hintForDisplayItem, aHintForThisFrame)) {
        return true;
      }

      if (!effectiveAnimationProperties) {
        effectiveAnimationProperties.emplace(
            nsLayoutUtils::GetAnimationPropertiesForCompositor(aStyleFrame));
      }
      const nsCSSPropertyIDSet& propertiesForDisplayItem =
          LayerAnimationInfo::GetCSSPropertiesFor(aDisplayItemType);
      if (effectiveAnimationProperties->Intersects(propertiesForDisplayItem)) {
        hint |= hintForDisplayItem;
      }
    }
    return true;
  };

  AnimationInfo::EnumerateGenerationOnFrame(
      aStyleFrame, aElement, LayerAnimationInfo::sDisplayItemTypes,
      maybeApplyChangeHint);

  if (hint) {
    // We apply the hint to the primary frame, not the style frame. Transform
    // and opacity hints apply to the table wrapper box, not the table box.
    aChangeListToProcess.AppendChange(aPrimaryFrame, aElement, hint);
  }
}

RestyleManager::AnimationsWithDestroyedFrame::AnimationsWithDestroyedFrame(
    RestyleManager* aRestyleManager)
    : mRestyleManager(aRestyleManager),
      mRestorePointer(mRestyleManager->mAnimationsWithDestroyedFrame) {
  MOZ_ASSERT(!mRestyleManager->mAnimationsWithDestroyedFrame,
             "shouldn't construct recursively");
  mRestyleManager->mAnimationsWithDestroyedFrame = this;
}

void RestyleManager::AnimationsWithDestroyedFrame ::
    StopAnimationsForElementsWithoutFrames() {
  StopAnimationsWithoutFrame(mContents, PseudoStyleRequest::NotPseudo());
  StopAnimationsWithoutFrame(mBeforeContents, PseudoStyleRequest::Before());
  StopAnimationsWithoutFrame(mAfterContents, PseudoStyleRequest::After());
  StopAnimationsWithoutFrame(mMarkerContents, PseudoStyleRequest::Marker());
}

void RestyleManager::AnimationsWithDestroyedFrame ::StopAnimationsWithoutFrame(
    nsTArray<RefPtr<Element>>& aArray,
    const PseudoStyleRequest& aPseudoRequest) {
  nsPresContext* context = mRestyleManager->PresContext();
  nsAnimationManager* animationManager = context->AnimationManager();
  nsTransitionManager* transitionManager = context->TransitionManager();
  const Document* doc = context->Document();
  for (Element* element : aArray) {
    PseudoStyleRequest request = aPseudoRequest;

    switch (aPseudoRequest.mType) {
      case PseudoStyleType::NotPseudo: {
        if (element->GetPrimaryFrame()) {
          continue;
        }

        // The contents of view transition pseudos are put together with
        // NotPseudo.
        const auto type = element->GetPseudoElementType();
        if (PseudoStyle::IsViewTransitionPseudoElement(type)) {
          request = {
              type,
              element->HasName()
                  ? element->GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
                  : nullptr};
          // View transition pseudo-elements use the document element to look up
          // their animations.
          element = doc->GetRootElement();
          MOZ_ASSERT(element);
        }
        break;
      }
      case PseudoStyleType::before:
        if (nsLayoutUtils::GetBeforeFrame(element)) {
          continue;
        }
        break;
      case PseudoStyleType::after:
        if (nsLayoutUtils::GetAfterFrame(element)) {
          continue;
        }
        break;
      case PseudoStyleType::marker:
        if (nsLayoutUtils::GetMarkerFrame(element)) {
          continue;
        }
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected PseudoStyleType");
        break;
    }

    animationManager->StopAnimationsForElement(element, request);
    transitionManager->StopAnimationsForElement(element, request);

    // All other animations should keep running but not running on the
    // *compositor* at this point.
    if (EffectSet* effectSet = EffectSet::Get(element, request)) {
      for (KeyframeEffect* effect : *effectSet) {
        effect->ResetIsRunningOnCompositor();
      }
    }
  }
}

// When using handled hints by an ancestor, we need to make sure that our
// ancestor in the DOM tree is actually our ancestor in the flat tree.
// Otherwise, we can't guarantee that e.g. a repaint from an ancestor in the DOM
// will really end up repainting us.
static bool CanUseHandledHintsFromAncestors(const nsIFrame* aFrame) {
  if (aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    // An out of flow can be parented in other part of the tree.
    return false;
  }
  if (aFrame->IsColumnSpanInMulticolSubtree()) {
    // Any column-spanner's parent frame is not its DOM parent's primary frame.
    return false;
  }
  if (aFrame->IsTableCaption()) {
    // This one is more subtle. captions are in-flow children of the table
    // frame. But they are parented to the table wrapper. So hints handled for
    // the inner table might not be applicable to us.
    return false;
  }
  return true;
}

#ifdef DEBUG
static bool IsAnonBox(const nsIFrame* aFrame) {
  return aFrame->Style()->IsAnonBox();
}

static const nsIFrame* FirstContinuationOrPartOfIBSplit(
    const nsIFrame* aFrame) {
  if (!aFrame) {
    return nullptr;
  }

  return nsLayoutUtils::FirstContinuationOrIBSplitSibling(aFrame);
}

static const nsIFrame* ExpectedOwnerForChild(const nsIFrame* aFrame) {
  const nsIFrame* parent = aFrame->GetParent();
  if (aFrame->IsTableFrame()) {
    MOZ_ASSERT(parent->IsTableWrapperFrame());
    parent = parent->GetParent();
  }

  if (IsAnonBox(aFrame) && !aFrame->IsTextFrame()) {
    if (parent->IsLineFrame()) {
      parent = parent->GetParent();
    }
    return parent->IsViewportFrame() ? nullptr
                                     : FirstContinuationOrPartOfIBSplit(parent);
  }

  if (aFrame->IsLineFrame()) {
    // A ::first-line always ends up here via its block, which is therefore the
    // right expected owner.  That block can be an
    // anonymous box.  For example, we could have a ::first-line on a columnated
    // block; the blockframe is the column-content anonymous box in that case.
    // So we don't want to end up in the code below, which steps out of anon
    // boxes.  Just return the parent of the line frame, which is the block.
    return parent;
  }

  if (aFrame->IsLetterFrame()) {
    // Ditto for ::first-letter. A first-letter always arrives here via its
    // direct parent, except when it's parented to a ::first-line.
    if (parent->IsLineFrame()) {
      parent = parent->GetParent();
    }
    return FirstContinuationOrPartOfIBSplit(parent);
  }

  if (parent->IsLetterFrame()) {
    // Things never have ::first-letter as their expected parent.  Go
    // on up to the ::first-letter's parent.
    parent = parent->GetParent();
  }

  parent = FirstContinuationOrPartOfIBSplit(parent);

  // We've handled already anon boxes, so now we're looking at
  // a frame of a DOM element or pseudo. Hop through anon and line-boxes
  // generated by our DOM parent, and go find the owner frame for it.
  while (parent && (IsAnonBox(parent) || parent->IsLineFrame())) {
    auto pseudo = parent->Style()->GetPseudoType();
    if (pseudo == PseudoStyleType::tableWrapper) {
      const nsIFrame* tableFrame = parent->PrincipalChildList().FirstChild();
      MOZ_ASSERT(tableFrame->IsTableFrame());
      // Handle :-moz-table and :-moz-inline-table.
      parent = IsAnonBox(tableFrame) ? parent->GetParent() : tableFrame;
    } else {
      // We get the in-flow parent here so that we can handle the OOF anonymous
      // boxed to get the correct parent.
      parent = parent->GetInFlowParent();
    }
    parent = FirstContinuationOrPartOfIBSplit(parent);
  }

  return parent;
}

// FIXME(emilio, bug 1633685): We should ideally figure out how to properly
// restyle replicated fixed pos frames... We seem to assume everywhere that they
// can't get restyled at the moment...
static bool IsInReplicatedFixedPosTree(const nsIFrame* aFrame) {
  if (!aFrame->PresContext()->IsPaginated()) {
    return false;
  }

  for (; aFrame; aFrame = aFrame->GetParent()) {
    if (aFrame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        !aFrame->FirstContinuation()->IsPrimaryFrame() &&
        nsLayoutUtils::IsReallyFixedPos(aFrame)) {
      return true;
    }
  }

  return true;
}

void ServoRestyleState::AssertOwner(const ServoRestyleState& aParent) const {
  MOZ_ASSERT(mOwner);
  MOZ_ASSERT(CanUseHandledHintsFromAncestors(mOwner));
  // We allow aParent.mOwner to be null, for cases when we're not starting at
  // the root of the tree.  We also allow aParent.mOwner to be somewhere up our
  // expected owner chain not our immediate owner, which allows us creating long
  // chains of ServoRestyleStates in some cases where it's just not worth it.
  if (aParent.mOwner) {
    const nsIFrame* owner = ExpectedOwnerForChild(mOwner);
    if (owner != aParent.mOwner && !IsInReplicatedFixedPosTree(mOwner)) {
      MOZ_ASSERT(IsAnonBox(owner),
                 "Should only have expected owner weirdness when anon boxes "
                 "are involved");
      bool found = false;
      for (; owner; owner = ExpectedOwnerForChild(owner)) {
        if (owner == aParent.mOwner) {
          found = true;
          break;
        }
      }
      MOZ_ASSERT(found, "Must have aParent.mOwner on our expected owner chain");
    }
  }
}

nsChangeHint ServoRestyleState::ChangesHandledFor(
    const nsIFrame* aFrame) const {
  if (!mOwner) {
    MOZ_ASSERT(!mChangesHandled);
    return mChangesHandled;
  }

  MOZ_ASSERT(mOwner == ExpectedOwnerForChild(aFrame) ||
                 IsInReplicatedFixedPosTree(aFrame),
             "Missed some frame in the hierarchy?");
  return mChangesHandled;
}
#endif

void ServoRestyleState::AddPendingWrapperRestyle(nsIFrame* aWrapperFrame) {
  MOZ_ASSERT(aWrapperFrame->Style()->IsWrapperAnonBox(),
             "All our wrappers are anon boxes, and why would we restyle "
             "non-inheriting ones?");
  MOZ_ASSERT(aWrapperFrame->Style()->IsInheritingAnonBox(),
             "All our wrappers are anon boxes, and why would we restyle "
             "non-inheriting ones?");
  MOZ_ASSERT(
      aWrapperFrame->Style()->GetPseudoType() != PseudoStyleType::cellContent,
      "Someone should be using TableAwareParentFor");
  MOZ_ASSERT(
      aWrapperFrame->Style()->GetPseudoType() != PseudoStyleType::tableWrapper,
      "Someone should be using TableAwareParentFor");
  // Make sure we only add first continuations.
  aWrapperFrame = aWrapperFrame->FirstContinuation();
  nsIFrame* last = mPendingWrapperRestyles.SafeLastElement(nullptr);
  if (last == aWrapperFrame) {
    // Already queued up, nothing to do.
    return;
  }

  // Make sure to queue up parents before children.  But don't queue up
  // ancestors of non-anonymous boxes here; those are handled when we traverse
  // their non-anonymous kids.
  if (aWrapperFrame->ParentIsWrapperAnonBox()) {
    AddPendingWrapperRestyle(TableAwareParentFor(aWrapperFrame));
  }

  // If the append fails, we'll fail to restyle properly, but that's probably
  // better than crashing.
  if (mPendingWrapperRestyles.AppendElement(aWrapperFrame, fallible)) {
    aWrapperFrame->SetIsWrapperAnonBoxNeedingRestyle(true);
  }
}

void ServoRestyleState::ProcessWrapperRestyles(nsIFrame* aParentFrame) {
  size_t i = mPendingWrapperRestyleOffset;
  while (i < mPendingWrapperRestyles.Length()) {
    i += ProcessMaybeNestedWrapperRestyle(aParentFrame, i);
  }

  mPendingWrapperRestyles.TruncateLength(mPendingWrapperRestyleOffset);
}

size_t ServoRestyleState::ProcessMaybeNestedWrapperRestyle(nsIFrame* aParent,
                                                           size_t aIndex) {
  // The frame at index aIndex is something we should restyle ourselves, but
  // following frames may need separate ServoRestyleStates to restyle.
  MOZ_ASSERT(aIndex < mPendingWrapperRestyles.Length());

  nsIFrame* cur = mPendingWrapperRestyles[aIndex];
  MOZ_ASSERT(cur->Style()->IsWrapperAnonBox());

  // Where is cur supposed to inherit from?  From its parent frame, except in
  // the case when cur is a table, in which case it should be its grandparent.
  // Also, not in the case when the resulting frame would be a first-line; in
  // that case we should be inheriting from the block, and the first-line will
  // do its fixup later if needed.
  //
  // Note that after we do all that fixup the parent we get might still not be
  // aParent; for example aParent could be a scrollframe, in which case we
  // should inherit from the scrollcontent frame.  Or the parent might be some
  // continuation of aParent.
  //
  // Try to assert as much as we can about the parent we actually end up using
  // without triggering bogus asserts in all those various edge cases.
  nsIFrame* parent = cur->GetParent();
  if (cur->IsTableFrame()) {
    MOZ_ASSERT(parent->IsTableWrapperFrame());
    parent = parent->GetParent();
  }
  if (parent->IsLineFrame()) {
    parent = parent->GetParent();
  }
  MOZ_ASSERT(FirstContinuationOrPartOfIBSplit(parent) == aParent ||
             (parent->Style()->IsInheritingAnonBox() &&
              parent->GetContent() == aParent->GetContent()));

  // Now "this" is a ServoRestyleState for aParent, so if parent is not a next
  // continuation (possibly across ib splits) of aParent we need a new
  // ServoRestyleState for the kid.
  Maybe<ServoRestyleState> parentRestyleState;
  nsIFrame* parentForRestyle =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(parent);
  if (parentForRestyle != aParent) {
    parentRestyleState.emplace(*parentForRestyle, *this, nsChangeHint_Empty,
                               CanUseHandledHints::Yes);
  }
  ServoRestyleState& curRestyleState =
      parentRestyleState ? *parentRestyleState : *this;

  // This frame may already have been restyled.  Even if it has, we can't just
  // return, because the next frame may be a kid of it that does need restyling.
  if (cur->IsWrapperAnonBoxNeedingRestyle()) {
    parentForRestyle->UpdateStyleOfChildAnonBox(cur, curRestyleState);
    cur->SetIsWrapperAnonBoxNeedingRestyle(false);
  }

  size_t numProcessed = 1;

  // Note: no overflow possible here, since aIndex < length.
  if (aIndex + 1 < mPendingWrapperRestyles.Length()) {
    nsIFrame* next = mPendingWrapperRestyles[aIndex + 1];
    if (TableAwareParentFor(next) == cur &&
        next->IsWrapperAnonBoxNeedingRestyle()) {
      // It might be nice if we could do better than nsChangeHint_Empty.  On
      // the other hand, presumably our mChangesHandled already has the bits
      // we really want here so in practice it doesn't matter.
      ServoRestyleState childState(*cur, curRestyleState, nsChangeHint_Empty,
                                   CanUseHandledHints::Yes,
                                   /* aAssertWrapperRestyleLength = */ false);
      numProcessed +=
          childState.ProcessMaybeNestedWrapperRestyle(cur, aIndex + 1);
    }
  }

  return numProcessed;
}

nsIFrame* ServoRestyleState::TableAwareParentFor(const nsIFrame* aChild) {
  // We want to get the anon box parent for aChild. where aChild has
  // ParentIsWrapperAnonBox().
  //
  // For the most part this is pretty straightforward, but there are two
  // wrinkles.  First, if aChild is a table, then we really want the parent of
  // its table wrapper.
  if (aChild->IsTableFrame()) {
    aChild = aChild->GetParent();
    MOZ_ASSERT(aChild->IsTableWrapperFrame());
  }

  nsIFrame* parent = aChild->GetParent();
  // Now if parent is a cell-content frame, we actually want the cellframe.
  if (parent->Style()->GetPseudoType() == PseudoStyleType::cellContent) {
    parent = parent->GetParent();
  } else if (parent->IsTableWrapperFrame()) {
    // Must be a caption.  In that case we want the table here.
    MOZ_ASSERT(aChild->StyleDisplay()->mDisplay == StyleDisplay::TableCaption);
    parent = parent->PrincipalChildList().FirstChild();
  }
  return parent;
}

void RestyleManager::PostRestyleEvent(Element* aElement,
                                      RestyleHint aRestyleHint,
                                      nsChangeHint aMinChangeHint) {
  MOZ_ASSERT(!(aMinChangeHint & nsChangeHint_NeutralChange),
             "Didn't expect explicit change hints to be neutral!");
  if (MOZ_UNLIKELY(IsDisconnected()) ||
      MOZ_UNLIKELY(PresContext()->PresShell()->IsDestroying())) {
    return;
  }

  // We allow posting restyles from within change hint handling, but not from
  // within the restyle algorithm itself.
  MOZ_ASSERT(!ServoStyleSet::IsInServoTraversal());

  if (!aRestyleHint && !aMinChangeHint) {
    // FIXME(emilio): we should assert against this instead.
    return;  // Nothing to do.
  }

  // Assuming the restyle hints will invalidate cached style for
  // getComputedStyle, since we don't know if any of the restyling that we do
  // would affect undisplayed elements.
  if (aRestyleHint) {
    if (!(aRestyleHint & RestyleHint::ForAnimations())) {
      mHaveNonAnimationRestyles = true;
    }

    IncrementUndisplayedRestyleGeneration();
  }

  // Processing change hints sometimes causes new change hints to be generated,
  // and very occasionally, additional restyle hints. We collect the change
  // hints manually to avoid re-traversing the DOM to find them.
  if (mReentrantChanges && !aRestyleHint) {
    mReentrantChanges->AppendElement(ReentrantChange{aElement, aMinChangeHint});
    return;
  }

  if (aRestyleHint || aMinChangeHint) {
    Servo_NoteExplicitHints(aElement, aRestyleHint, aMinChangeHint);
  }
}

void RestyleManager::PostRestyleEventForAnimations(
    Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    RestyleHint aRestyleHint) {
  Element* elementToRestyle = aElement->GetPseudoElement(aPseudoRequest);
  if (!elementToRestyle) {
    // FIXME: Bug 1371107: When reframing happens,
    // EffectCompositor::mElementsToRestyle still has unbound old pseudo
    // element. We should drop it.
    return;
  }

  mPresContext->TriggeredAnimationRestyle();

  Servo_NoteExplicitHints(elementToRestyle, aRestyleHint, nsChangeHint(0));
}

void RestyleManager::RebuildAllStyleData(nsChangeHint aExtraHint,
                                         RestyleHint aRestyleHint) {
  // NOTE(emilio): The semantics of these methods are quite funny, in the sense
  // that we're not supposed to need to rebuild the actual stylist data.
  //
  // That's handled as part of the MediumFeaturesChanged stuff, if needed.
  //
  // Clear the cached style data only if we are guaranteed to process the whole
  // DOM tree again.
  //
  // FIXME(emilio): Decouple this, probably. This probably just wants to reset
  // the "uses viewport units / uses rem" bits, and _maybe_ clear cached anon
  // box styles and such... But it doesn't really always need to clear the
  // initial style of the document and similar...
  if (aRestyleHint.DefinitelyRecascadesAllSubtree()) {
    StyleSet()->ClearCachedStyleData();
  }

  DocumentStyleRootIterator iter(mPresContext->Document());
  while (Element* root = iter.GetNextStyleRoot()) {
    PostRestyleEvent(root, aRestyleHint, aExtraHint);
  }

  // TODO(emilio, bz): Extensions can add/remove stylesheets that can affect
  // non-inheriting anon boxes. It's not clear if we want to support that, but
  // if we do, we need to re-selector-match them here.
}

/* static */
void RestyleManager::ClearServoDataFromSubtree(Element* aElement,
                                               IncludeRoot aIncludeRoot) {
  if (aElement->HasServoData()) {
    StyleChildrenIterator it(aElement);
    for (nsIContent* n = it.GetNextChild(); n; n = it.GetNextChild()) {
      if (n->IsElement()) {
        ClearServoDataFromSubtree(n->AsElement(), IncludeRoot::Yes);
      }
    }
  }

  if (MOZ_LIKELY(aIncludeRoot == IncludeRoot::Yes)) {
    aElement->ClearServoData();
    MOZ_ASSERT(!aElement->HasAnyOfFlags(Element::kAllServoDescendantBits |
                                        NODE_NEEDS_FRAME));
    MOZ_ASSERT(aElement != aElement->OwnerDoc()->GetServoRestyleRoot());
  }
}

/* static */
void RestyleManager::ClearRestyleStateFromSubtree(Element* aElement) {
  if (aElement->HasAnyOfFlags(Element::kAllServoDescendantBits)) {
    StyleChildrenIterator it(aElement);
    for (nsIContent* n = it.GetNextChild(); n; n = it.GetNextChild()) {
      if (n->IsElement()) {
        ClearRestyleStateFromSubtree(n->AsElement());
      }
    }
  }

  bool wasRestyled = false;
  Unused << Servo_TakeChangeHint(aElement, &wasRestyled);
  aElement->UnsetFlags(Element::kAllServoDescendantBits);
}

/**
 * This struct takes care of encapsulating some common state that text nodes may
 * need to track during the post-traversal.
 *
 * This is currently used to properly compute change hints when the parent
 * element of this node is a display: contents node, and also to avoid computing
 * the style for text children more than once per element.
 */
struct RestyleManager::TextPostTraversalState {
 public:
  TextPostTraversalState(Element& aParentElement, ComputedStyle* aParentContext,
                         bool aDisplayContentsParentStyleChanged,
                         ServoRestyleState& aParentRestyleState)
      : mParentElement(aParentElement),
        mParentContext(aParentContext),
        mParentRestyleState(aParentRestyleState),
        mStyle(nullptr),
        mShouldPostHints(aDisplayContentsParentStyleChanged),
        mShouldComputeHints(aDisplayContentsParentStyleChanged),
        mComputedHint(nsChangeHint_Empty) {}

  nsStyleChangeList& ChangeList() { return mParentRestyleState.ChangeList(); }

  ComputedStyle& ComputeStyle(nsIContent* aTextNode) {
    if (!mStyle) {
      mStyle = mParentRestyleState.StyleSet().ResolveStyleForText(
          aTextNode, &ParentStyle());
    }
    MOZ_ASSERT(mStyle);
    return *mStyle;
  }

  void ComputeHintIfNeeded(nsIContent* aContent, nsIFrame* aTextFrame,
                           ComputedStyle& aNewStyle) {
    MOZ_ASSERT(aTextFrame);
    MOZ_ASSERT(aNewStyle.GetPseudoType() == PseudoStyleType::mozText);

    if (MOZ_LIKELY(!mShouldPostHints)) {
      return;
    }

    ComputedStyle* oldStyle = aTextFrame->Style();
    MOZ_ASSERT(oldStyle->GetPseudoType() == PseudoStyleType::mozText);

    // We rely on the fact that all the text children for the same element share
    // style to avoid recomputing style differences for all of them.
    //
    // TODO(emilio): The above may not be true for ::first-{line,letter}, but
    // we'll cross that bridge when we support those in stylo.
    if (mShouldComputeHints) {
      mShouldComputeHints = false;
      uint32_t equalStructs;
      mComputedHint = oldStyle->CalcStyleDifference(aNewStyle, &equalStructs);
      mComputedHint = NS_RemoveSubsumedHints(
          mComputedHint, mParentRestyleState.ChangesHandledFor(aTextFrame));
    }

    if (mComputedHint) {
      mParentRestyleState.ChangeList().AppendChange(aTextFrame, aContent,
                                                    mComputedHint);
    }
  }

 private:
  ComputedStyle& ParentStyle() {
    if (!mParentContext) {
      mLazilyResolvedParentContext =
          ServoStyleSet::ResolveServoStyle(mParentElement);
      mParentContext = mLazilyResolvedParentContext;
    }
    return *mParentContext;
  }

  Element& mParentElement;
  ComputedStyle* mParentContext;
  RefPtr<ComputedStyle> mLazilyResolvedParentContext;
  ServoRestyleState& mParentRestyleState;
  RefPtr<ComputedStyle> mStyle;
  bool mShouldPostHints;
  bool mShouldComputeHints;
  nsChangeHint mComputedHint;
};

static void UpdateBackdropIfNeeded(nsIFrame* aFrame, ServoStyleSet& aStyleSet,
                                   nsStyleChangeList& aChangeList) {
  const nsStyleDisplay* display = aFrame->Style()->StyleDisplay();
  if (display->mTopLayer != StyleTopLayer::Auto) {
    return;
  }

  // Elements in the top layer are guaranteed to have absolute or fixed
  // position per https://fullscreen.spec.whatwg.org/#new-stacking-layer.
  MOZ_ASSERT(display->IsAbsolutelyPositionedStyle());

  nsIFrame* backdropPlaceholder =
      aFrame->GetChildList(FrameChildListID::Backdrop).FirstChild();
  if (!backdropPlaceholder) {
    return;
  }

  MOZ_ASSERT(backdropPlaceholder->IsPlaceholderFrame());
  nsIFrame* backdropFrame =
      nsPlaceholderFrame::GetRealFrameForPlaceholder(backdropPlaceholder);
  MOZ_ASSERT(backdropFrame->IsBackdropFrame());
  MOZ_ASSERT(backdropFrame->Style()->GetPseudoType() ==
             PseudoStyleType::backdrop);

  RefPtr<ComputedStyle> newStyle = aStyleSet.ResolvePseudoElementStyle(
      *aFrame->GetContent()->AsElement(), PseudoStyleType::backdrop, nullptr,
      aFrame->Style());

  // NOTE(emilio): We can't use the changes handled for the owner of the
  // backdrop frame, since it's out of flow, and parented to the viewport or
  // canvas frame (depending on the `position` value).
  MOZ_ASSERT(backdropFrame->GetParent()->IsViewportFrame() ||
             backdropFrame->GetParent()->IsCanvasFrame());
  nsTArray<nsIFrame*> wrappersToRestyle;
  nsTArray<RefPtr<Element>> anchorsToSuppress;
  ServoRestyleState state(aStyleSet, aChangeList, wrappersToRestyle,
                          anchorsToSuppress);
  nsIFrame::UpdateStyleOfOwnedChildFrame(backdropFrame, newStyle, state);
  MOZ_ASSERT(anchorsToSuppress.IsEmpty());
}

static void UpdateFirstLetterIfNeeded(nsIFrame* aFrame,
                                      ServoRestyleState& aRestyleState) {
  MOZ_ASSERT(
      !aFrame->IsBlockFrameOrSubclass(),
      "You're probably duplicating work with UpdatePseudoElementStyles!");
  if (!aFrame->HasFirstLetterChild()) {
    return;
  }

  // We need to find the block the first-letter is associated with so we can
  // find the right element for the first-letter's style resolution.  Might as
  // well just delegate the whole thing to that block.
  nsIFrame* block = aFrame->GetParent();
  while (!block->IsBlockFrameOrSubclass()) {
    block = block->GetParent();
  }

  static_cast<nsBlockFrame*>(block->FirstContinuation())
      ->UpdateFirstLetterStyle(aRestyleState);
}

static void UpdateOneAdditionalComputedStyle(nsIFrame* aFrame, uint32_t aIndex,
                                             ComputedStyle& aOldContext,
                                             ServoRestyleState& aRestyleState) {
  auto pseudoType = aOldContext.GetPseudoType();
  MOZ_ASSERT(pseudoType != PseudoStyleType::NotPseudo);
  MOZ_ASSERT(
      !nsCSSPseudoElements::PseudoElementSupportsUserActionState(pseudoType));

  RefPtr<ComputedStyle> newStyle =
      aRestyleState.StyleSet().ResolvePseudoElementStyle(
          *aFrame->GetContent()->AsElement(), pseudoType, nullptr,
          aFrame->Style());

  uint32_t equalStructs;  // Not used, actually.
  nsChangeHint childHint =
      aOldContext.CalcStyleDifference(*newStyle, &equalStructs);
  if (CanUseHandledHintsFromAncestors(aFrame)) {
    childHint = NS_RemoveSubsumedHints(childHint,
                                       aRestyleState.ChangesHandledFor(aFrame));
  }

  if (childHint) {
    if (childHint & nsChangeHint_ReconstructFrame) {
      // If we generate a reconstruct here, remove any non-reconstruct hints we
      // may have already generated for this content.
      aRestyleState.ChangeList().PopChangesForContent(aFrame->GetContent());
    }
    aRestyleState.ChangeList().AppendChange(aFrame, aFrame->GetContent(),
                                            childHint);
  }

  aFrame->SetAdditionalComputedStyle(aIndex, newStyle);
}

static void UpdateAdditionalComputedStyles(nsIFrame* aFrame,
                                           ServoRestyleState& aRestyleState) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->GetContent() && aFrame->GetContent()->IsElement());

  // FIXME(emilio): Consider adding a bit or something to avoid the initial
  // virtual call?
  uint32_t index = 0;
  while (auto* oldStyle = aFrame->GetAdditionalComputedStyle(index)) {
    UpdateOneAdditionalComputedStyle(aFrame, index++, *oldStyle, aRestyleState);
  }
}

static void UpdateFramePseudoElementStyles(nsIFrame* aFrame,
                                           ServoRestyleState& aRestyleState) {
  if (nsBlockFrame* blockFrame = do_QueryFrame(aFrame)) {
    blockFrame->UpdatePseudoElementStyles(aRestyleState);
  } else {
    UpdateFirstLetterIfNeeded(aFrame, aRestyleState);
  }

  UpdateBackdropIfNeeded(aFrame, aRestyleState.StyleSet(),
                         aRestyleState.ChangeList());
}

enum class ServoPostTraversalFlags : uint32_t {
  Empty = 0,
  // Whether parent was restyled.
  ParentWasRestyled = 1 << 0,
  // Skip sending accessibility notifications for all descendants.
  SkipA11yNotifications = 1 << 1,
  // Always send accessibility notifications if the element is shown.
  // The SkipA11yNotifications flag above overrides this flag.
  SendA11yNotificationsIfShown = 1 << 2,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(ServoPostTraversalFlags)

#ifdef ACCESSIBILITY
static bool IsVisibleForA11y(const ComputedStyle& aStyle) {
  return aStyle.StyleVisibility()->IsVisible() && !aStyle.StyleUI()->IsInert();
}

static bool IsSubtreeVisibleForA11y(const ComputedStyle& aStyle) {
  return aStyle.StyleDisplay()->mContentVisibility !=
         StyleContentVisibility::Hidden;
}
#endif

// Send proper accessibility notifications and return post traversal
// flags for kids.
static ServoPostTraversalFlags SendA11yNotifications(
    nsPresContext* aPresContext, Element* aElement,
    const ComputedStyle& aOldStyle, const ComputedStyle& aNewStyle,
    ServoPostTraversalFlags aFlags) {
  using Flags = ServoPostTraversalFlags;
  MOZ_ASSERT(!(aFlags & Flags::SkipA11yNotifications) ||
                 !(aFlags & Flags::SendA11yNotificationsIfShown),
             "The two a11y flags should never be set together");

#ifdef ACCESSIBILITY
  nsAccessibilityService* accService = GetAccService();
  if (!accService) {
    // If we don't have accessibility service, accessibility is not
    // enabled. Just skip everything.
    return Flags::Empty;
  }

  if (aNewStyle.StyleUIReset()->mMozSubtreeHiddenOnlyVisually !=
      aOldStyle.StyleUIReset()->mMozSubtreeHiddenOnlyVisually) {
    if (aElement->GetParent() &&
        aElement->GetParent()->IsXULElement(nsGkAtoms::tabpanels)) {
      accService->NotifyOfTabPanelVisibilityChange(
          aPresContext->PresShell(), aElement,
          aNewStyle.StyleUIReset()->mMozSubtreeHiddenOnlyVisually);
    }
  }

  if (aFlags & Flags::SkipA11yNotifications) {
    // Propagate the skipping flag to descendants.
    return Flags::SkipA11yNotifications;
  }

  bool needsNotify = false;
  const bool isVisible = IsVisibleForA11y(aNewStyle);
  const bool wasVisible = IsVisibleForA11y(aOldStyle);

  if (aFlags & Flags::SendA11yNotificationsIfShown) {
    if (!isVisible) {
      // Propagate the sending-if-shown flag to descendants.
      return Flags::SendA11yNotificationsIfShown;
    }
    // We have asked accessibility service to remove the whole subtree
    // of element which becomes invisible from the accessible tree, but
    // this element is visible, so we need to add it back.
    needsNotify = true;
  } else {
    // If we shouldn't skip in any case, we need to check whether our own
    // visibility has changed.
    // Also notify if the subtree visibility change due to content-visibility.
    const bool isSubtreeVisible = IsSubtreeVisibleForA11y(aNewStyle);
    const bool wasSubtreeVisible = IsSubtreeVisibleForA11y(aOldStyle);
    needsNotify =
        wasVisible != isVisible || wasSubtreeVisible != isSubtreeVisible;
  }

  if (needsNotify) {
    PresShell* presShell = aPresContext->PresShell();
    if (isVisible) {
      accService->ContentRangeInserted(presShell, aElement,
                                       aElement->GetNextSibling());
      // We are adding the subtree. Accessibility service would handle
      // descendants, so we should just skip them from notifying.
      return Flags::SkipA11yNotifications;
    }
    if (wasVisible) {
      // Remove the subtree of this invisible element, and ask any shown
      // descendant to add themselves back.
      accService->ContentRemoved(presShell, aElement);
      return Flags::SendA11yNotificationsIfShown;
    }
  }
#endif

  return Flags::Empty;
}

bool RestyleManager::ProcessPostTraversal(Element* aElement,
                                          ServoRestyleState& aRestyleState,
                                          ServoPostTraversalFlags aFlags) {
  nsIFrame* styleFrame = nsLayoutUtils::GetStyleFrame(aElement);
  nsIFrame* primaryFrame = aElement->GetPrimaryFrame();

  MOZ_DIAGNOSTIC_ASSERT(aElement->HasServoData(),
                        "Element without Servo data on a post-traversal? How?");

  // NOTE(emilio): This is needed because for table frames the bit is set on the
  // table wrapper (which is the primary frame), not on the table itself.
  const bool isOutOfFlow =
      primaryFrame && primaryFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW);

  const bool canUseHandledHints =
      primaryFrame && CanUseHandledHintsFromAncestors(primaryFrame);

  // Grab the change hint from Servo.
  bool wasRestyled = false;
  nsChangeHint changeHint =
      static_cast<nsChangeHint>(Servo_TakeChangeHint(aElement, &wasRestyled));

  RefPtr<ComputedStyle> upToDateStyleIfRestyled =
      wasRestyled ? ServoStyleSet::ResolveServoStyle(*aElement) : nullptr;

  // We should really fix the weird primary frame mapping for image maps
  // (bug 135040)...
  if (styleFrame && styleFrame->GetContent() != aElement) {
    MOZ_ASSERT(styleFrame->IsImageFrameOrSubclass());
    styleFrame = nullptr;
  }

  // Handle lazy frame construction by posting a reconstruct for any lazily-
  // constructed roots.
  if (aElement->HasFlag(NODE_NEEDS_FRAME)) {
    changeHint |= nsChangeHint_ReconstructFrame;
    MOZ_ASSERT(!styleFrame);
  }

  if (styleFrame) {
    MOZ_ASSERT(primaryFrame);

    nsIFrame* maybeAnonBoxChild;
    if (isOutOfFlow) {
      maybeAnonBoxChild = primaryFrame->GetPlaceholderFrame();
    } else {
      maybeAnonBoxChild = primaryFrame;
    }

    // Do not subsume change hints for the column-spanner.
    if (canUseHandledHints) {
      changeHint = NS_RemoveSubsumedHints(
          changeHint, aRestyleState.ChangesHandledFor(styleFrame));
    }

    // If the parent wasn't restyled, the styles of our anon box parents won't
    // change either.
    if ((aFlags & ServoPostTraversalFlags::ParentWasRestyled) &&
        maybeAnonBoxChild->ParentIsWrapperAnonBox()) {
      aRestyleState.AddPendingWrapperRestyle(
          ServoRestyleState::TableAwareParentFor(maybeAnonBoxChild));
    }

    // If we don't have a ::marker pseudo-element, but need it, then
    // reconstruct the frame.  (The opposite situation implies 'display'
    // changes so doesn't need to be handled explicitly here.)
    if (wasRestyled && styleFrame->StyleDisplay()->IsListItem() &&
        styleFrame->IsBlockFrameOrSubclass() &&
        !nsLayoutUtils::GetMarkerPseudo(aElement)) {
      RefPtr<ComputedStyle> pseudoStyle =
          aRestyleState.StyleSet().ProbePseudoElementStyle(
              *aElement, PseudoStyleType::marker, nullptr,
              upToDateStyleIfRestyled);
      if (pseudoStyle) {
        changeHint |= nsChangeHint_ReconstructFrame;
      }
    }
  }

  // Although we shouldn't generate non-ReconstructFrame hints for elements with
  // no frames, we can still get them here if they were explicitly posted by
  // PostRestyleEvent, such as a RepaintFrame hint when a :link changes to be
  // :visited.  Skip processing these hints if there is no frame.
  if ((styleFrame || (changeHint & nsChangeHint_ReconstructFrame)) &&
      changeHint) {
    aRestyleState.ChangeList().AppendChange(styleFrame, aElement, changeHint);
  }

  // If our change hint is reconstruct, we delegate to the frame constructor,
  // which consumes the new style and expects the old style to be on the frame.
  //
  // XXXbholley: We should teach the frame constructor how to clear the dirty
  // descendants bit to avoid the traversal here.
  if (changeHint & nsChangeHint_ReconstructFrame) {
    if (wasRestyled &&
        StaticPrefs::layout_css_scroll_anchoring_suppressions_enabled()) {
      const bool wasAbsPos =
          styleFrame &&
          styleFrame->StyleDisplay()->IsAbsolutelyPositionedStyle();
      auto* newDisp = upToDateStyleIfRestyled->StyleDisplay();
      // https://drafts.csswg.org/css-scroll-anchoring/#suppression-triggers
      //
      // We need to do the position check here rather than in
      // DidSetComputedStyle because changing position reframes.
      //
      // We suppress adjustments whenever we change from being display: none to
      // be an abspos.
      //
      // Similarly, for other changes from abspos to non-abspos styles.
      //
      // TODO(emilio): I _think_ chrome won't suppress adjustments whenever
      // `display` changes. But that causes some infinite loops in cases like
      // bug 1568778.
      if (wasAbsPos != newDisp->IsAbsolutelyPositionedStyle()) {
        aRestyleState.AddPendingScrollAnchorSuppression(aElement);
      }
    }
    ClearRestyleStateFromSubtree(aElement);
    return true;
  }

  // TODO(emilio): We could avoid some refcount traffic here, specially in the
  // ComputedStyle case, which uses atomic refcounting.
  //
  // Hold the ComputedStyle alive, because it could become a dangling pointer
  // during the replacement. In practice it's not a huge deal, but better not
  // playing with dangling pointers if not needed.
  //
  // NOTE(emilio): We could keep around the old computed style for display:
  // contents elements too, but we don't really need it right now.
  RefPtr<ComputedStyle> oldOrDisplayContentsStyle =
      styleFrame ? styleFrame->Style() : nullptr;

  MOZ_ASSERT(!(styleFrame && Servo_Element_IsDisplayContents(aElement)),
             "display: contents node has a frame, yet we didn't reframe it"
             " above?");
  const bool isDisplayContents = !styleFrame && aElement->HasServoData() &&
                                 Servo_Element_IsDisplayContents(aElement);
  if (isDisplayContents) {
    oldOrDisplayContentsStyle = ServoStyleSet::ResolveServoStyle(*aElement);
  }

  Maybe<ServoRestyleState> thisFrameRestyleState;
  if (styleFrame) {
    thisFrameRestyleState.emplace(
        *styleFrame, aRestyleState, changeHint,
        ServoRestyleState::CanUseHandledHints(canUseHandledHints));
  }

  // We can't really assume as used changes from display: contents elements (or
  // other elements without frames).
  ServoRestyleState& childrenRestyleState =
      thisFrameRestyleState ? *thisFrameRestyleState : aRestyleState;

  ComputedStyle* upToDateStyle =
      wasRestyled ? upToDateStyleIfRestyled : oldOrDisplayContentsStyle;

  ServoPostTraversalFlags childrenFlags =
      wasRestyled ? ServoPostTraversalFlags::ParentWasRestyled
                  : ServoPostTraversalFlags::Empty;

  if (wasRestyled && oldOrDisplayContentsStyle) {
    MOZ_ASSERT(styleFrame || isDisplayContents);

    // We want to walk all the continuations here, even the ones with different
    // styles.  In practice, the only reason we get continuations with different
    // styles here is ::first-line (::first-letter never affects element
    // styles).  But in that case, newStyle is the right context for the
    // _later_ continuations anyway (the ones not affected by ::first-line), not
    // the earlier ones, so there is no point stopping right at the point when
    // we'd actually be setting the right ComputedStyle.
    //
    // This does mean that we may be setting the wrong ComputedStyle on our
    // initial continuations; ::first-line fixes that up after the fact.
    for (nsIFrame* f = styleFrame; f; f = f->GetNextContinuation()) {
      MOZ_ASSERT_IF(f != styleFrame, !f->GetAdditionalComputedStyle(0));
      f->SetComputedStyle(upToDateStyle);
    }

    if (styleFrame) {
      UpdateAdditionalComputedStyles(styleFrame, aRestyleState);
    }

    if (!aElement->GetParent()) {
      // This is the root.  Update styles on the viewport as needed.
      ViewportFrame* viewport =
          do_QueryFrame(mPresContext->PresShell()->GetRootFrame());
      if (viewport) {
        // NB: The root restyle state, not the one for our children!
        viewport->UpdateStyle(aRestyleState);
      }
    }

    // Some changes to animations don't affect the computed style and yet still
    // require the layer to be updated. For example, pausing an animation via
    // the Web Animations API won't affect an element's style but still
    // requires to update the animation on the layer.
    //
    // We can sometimes reach this when the animated style is being removed.
    // Since AddLayerChangesForAnimation checks if |styleFrame| has a transform
    // style or not, we need to call it *after* setting |newStyle| to
    // |styleFrame| to ensure the animated transform has been removed first.
    AddLayerChangesForAnimation(styleFrame, primaryFrame, aElement, changeHint,
                                aRestyleState.ChangeList());

    childrenFlags |= SendA11yNotifications(mPresContext, aElement,
                                           *oldOrDisplayContentsStyle,
                                           *upToDateStyle, aFlags);
  }

  const bool traverseElementChildren =
      aElement->HasAnyOfFlags(Element::kAllServoDescendantBits);
  const bool traverseTextChildren =
      wasRestyled || aElement->HasFlag(NODE_DESCENDANTS_NEED_FRAMES);
  bool recreatedAnyContext = wasRestyled;
  if (traverseElementChildren || traverseTextChildren) {
    StyleChildrenIterator it(aElement);
    TextPostTraversalState textState(*aElement, upToDateStyle,
                                     isDisplayContents && wasRestyled,
                                     childrenRestyleState);
    for (nsIContent* n = it.GetNextChild(); n; n = it.GetNextChild()) {
      if (traverseElementChildren && n->IsElement()) {
        recreatedAnyContext |= ProcessPostTraversal(
            n->AsElement(), childrenRestyleState, childrenFlags);
      } else if (traverseTextChildren && n->IsText()) {
        recreatedAnyContext |= ProcessPostTraversalForText(
            n, textState, childrenRestyleState, childrenFlags);
      }
    }
  }

  // We want to update frame pseudo-element styles after we've traversed our
  // kids, because some of those updates (::first-line/::first-letter) need to
  // modify the styles of the kids, and the child traversal above would just
  // clobber those modifications.
  if (styleFrame) {
    if (wasRestyled) {
      // Make sure to update anon boxes and pseudo bits after updating text,
      // otherwise ProcessPostTraversalForText could clobber first-letter
      // styles, for example.
      styleFrame->UpdateStyleOfOwnedAnonBoxes(childrenRestyleState);
    }
    // Process anon box wrapper frames before ::first-line bits, but _after_
    // owned anon boxes, since the children wrapper anon boxes could be
    // inheriting from our own owned anon boxes.
    childrenRestyleState.ProcessWrapperRestyles(styleFrame);
    if (wasRestyled) {
      UpdateFramePseudoElementStyles(styleFrame, childrenRestyleState);
    } else if (traverseElementChildren &&
               styleFrame->IsBlockFrameOrSubclass()) {
      // Even if we were not restyled, if we're a block with a first-line and
      // one of our descendant elements which is on the first line was restyled,
      // we need to update the styles of things on the first line, because
      // they're wrong now.
      //
      // FIXME(bz) Could we do better here?  For example, could we keep track of
      // frames that are "block with a ::first-line so we could avoid
      // IsFrameOfType() and digging about for the first-line frame if not?
      // Could we keep track of whether the element children we actually restyle
      // are affected by first-line?  Something else?  Bug 1385443 tracks making
      // this better.
      nsIFrame* firstLineFrame =
          static_cast<nsBlockFrame*>(styleFrame)->GetFirstLineFrame();
      if (firstLineFrame) {
        for (nsIFrame* kid : firstLineFrame->PrincipalChildList()) {
          ReparentComputedStyleForFirstLine(kid);
        }
      }
    }
  }

  aElement->UnsetFlags(Element::kAllServoDescendantBits);
  return recreatedAnyContext;
}

bool RestyleManager::ProcessPostTraversalForText(
    nsIContent* aTextNode, TextPostTraversalState& aPostTraversalState,
    ServoRestyleState& aRestyleState, ServoPostTraversalFlags aFlags) {
  // Handle lazy frame construction.
  if (aTextNode->HasFlag(NODE_NEEDS_FRAME)) {
    aPostTraversalState.ChangeList().AppendChange(
        nullptr, aTextNode, nsChangeHint_ReconstructFrame);
    return true;
  }

  // Handle restyle.
  nsIFrame* primaryFrame = aTextNode->GetPrimaryFrame();
  if (!primaryFrame) {
    return false;
  }

  // If the parent wasn't restyled, the styles of our anon box parents won't
  // change either.
  if ((aFlags & ServoPostTraversalFlags::ParentWasRestyled) &&
      primaryFrame->ParentIsWrapperAnonBox()) {
    aRestyleState.AddPendingWrapperRestyle(
        ServoRestyleState::TableAwareParentFor(primaryFrame));
  }

  ComputedStyle& newStyle = aPostTraversalState.ComputeStyle(aTextNode);
  aPostTraversalState.ComputeHintIfNeeded(aTextNode, primaryFrame, newStyle);

  // We want to walk all the continuations here, even the ones with different
  // styles.  In practice, the only reasons we get continuations with different
  // styles are ::first-line and ::first-letter.  But in those cases,
  // newStyle is the right context for the _later_ continuations anyway (the
  // ones not affected by ::first-line/::first-letter), not the earlier ones,
  // so there is no point stopping right at the point when we'd actually be
  // setting the right ComputedStyle.
  //
  // This does mean that we may be setting the wrong ComputedStyle on our
  // initial continuations; ::first-line/::first-letter fix that up after the
  // fact.
  for (nsIFrame* f = primaryFrame; f; f = f->GetNextContinuation()) {
    f->SetComputedStyle(&newStyle);
  }

  return true;
}

void RestyleManager::ClearSnapshots() {
  for (auto iter = mSnapshots.Iter(); !iter.Done(); iter.Next()) {
    iter.Key()->UnsetFlags(ELEMENT_HAS_SNAPSHOT | ELEMENT_HANDLED_SNAPSHOT);
    iter.Remove();
  }
}

ServoElementSnapshot& RestyleManager::SnapshotFor(Element& aElement) {
  MOZ_DIAGNOSTIC_ASSERT(!mInStyleRefresh);

  // NOTE(emilio): We can handle snapshots from a one-off restyle of those that
  // we do to restyle stuff for reconstruction, for example.
  //
  // It seems to be the case that we always flush in between that happens and
  // the next attribute change, so we can assert that we haven't handled the
  // snapshot here yet. If this assertion didn't hold, we'd need to unset that
  // flag from here too.
  //
  // Can't wait to make ProcessPendingRestyles the only entry-point for styling,
  // so this becomes much easier to reason about. Today is not that day though.
  MOZ_ASSERT(!aElement.HasFlag(ELEMENT_HANDLED_SNAPSHOT));

  ServoElementSnapshot* snapshot =
      mSnapshots.GetOrInsertNew(&aElement, aElement);
  aElement.SetFlags(ELEMENT_HAS_SNAPSHOT);

  // Now that we have a snapshot, make sure a restyle is triggered.
  aElement.NoteDirtyForServo();
  return *snapshot;
}

void RestyleManager::DoProcessPendingRestyles(ServoTraversalFlags aFlags) {
  nsPresContext* presContext = PresContext();
  PresShell* presShell = presContext->PresShell();

  MOZ_ASSERT(presContext->Document(), "No document?  Pshaw!");
  // FIXME(emilio): In the "flush animations" case, ideally, we should only
  // recascade animation styles running on the compositor, so we shouldn't care
  // about other styles, or new rules that apply to the page...
  //
  // However, that's not true as of right now, see bug 1388031 and bug 1388692.
  MOZ_ASSERT((aFlags & ServoTraversalFlags::FlushThrottledAnimations) ||
                 !presContext->HasPendingMediaQueryUpdates(),
             "Someone forgot to update media queries?");
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript(), "Missing a script blocker!");
  MOZ_RELEASE_ASSERT(!mInStyleRefresh, "Reentrant call?");

  if (MOZ_UNLIKELY(!presShell->DidInitialize())) {
    // PresShell::FlushPendingNotifications doesn't early-return in the case
    // where the PresShell hasn't yet been initialized (and therefore we haven't
    // yet done the initial style traversal of the DOM tree). We should arguably
    // fix up the callers and assert against this case, but we just detect and
    // handle it for now.
    return;
  }

  // It'd be bad!
  PresShell::AutoAssertNoFlush noReentrantFlush(*presShell);

  // Create a AnimationsWithDestroyedFrame during restyling process to
  // stop animations and transitions on elements that have no frame at the end
  // of the restyling process.
  AnimationsWithDestroyedFrame animationsWithDestroyedFrame(this);

  ServoStyleSet* styleSet = StyleSet();
  Document* doc = presContext->Document();

  if (!doc->GetServoRestyleRoot()) {
    // This might post new restyles, so need to do it here. Don't do it if we're
    // already going to restyle tho, so that we don't potentially reflow with
    // dirty styling.
    presContext->UpdateContainerQueryStyles();
    presContext->FinishedContainerQueryUpdate();
  }

  // Perform the Servo traversal, and the post-traversal if required. We do this
  // in a loop because certain rare paths in the frame constructor can trigger
  // additional style invalidations.
  //
  // FIXME(emilio): Confirm whether that's still true now that XBL is gone.
  mInStyleRefresh = true;
  if (mHaveNonAnimationRestyles) {
    ++mAnimationGeneration;
  }

  if (mRestyleForCSSRuleChanges) {
    aFlags |= ServoTraversalFlags::ForCSSRuleChanges;
  }

  while (styleSet->StyleDocument(aFlags)) {
    ClearSnapshots();
    mRestyledAsWholeContainer.Clear();

    // Select scroll anchors for frames that have been scrolled. Do this
    // before processing restyled frames so that anchor nodes are correctly
    // marked when directly moving frames with RecomputePosition.
    presContext->PresShell()->FlushPendingScrollAnchorSelections();

    nsStyleChangeList currentChanges;
    bool anyStyleChanged = false;

    // Recreate styles , and queue up change hints (which also handle lazy frame
    // construction).
    nsTArray<RefPtr<Element>> anchorsToSuppress;

    // Unfortunately, the frame constructor and ProcessPostTraversal can
    // generate new change hints while processing existing ones. We redirect
    // those into a secondary queue and iterate until there's nothing left.
    ReentrantChangeList newChanges;
    mReentrantChanges = &newChanges;

    {
      DocumentStyleRootIterator iter(doc->GetServoRestyleRoot());
      while (Element* root = iter.GetNextStyleRoot()) {
        nsTArray<nsIFrame*> wrappersToRestyle;
        ServoRestyleState state(*styleSet, currentChanges, wrappersToRestyle,
                                anchorsToSuppress);
        ServoPostTraversalFlags flags = ServoPostTraversalFlags::Empty;
        anyStyleChanged |= ProcessPostTraversal(root, state, flags);
      }

      // We want to suppress adjustments the current (before-change) scroll
      // anchor container now, and save a reference to the content node so that
      // we can suppress them in the after-change scroll anchor .
      for (Element* element : anchorsToSuppress) {
        if (nsIFrame* frame = element->GetPrimaryFrame()) {
          if (auto* container = ScrollAnchorContainer::FindFor(frame)) {
            container->SuppressAdjustments();
          }
        }
      }
    }

    doc->ClearServoRestyleRoot();
    ClearSnapshots();

    while (!currentChanges.IsEmpty()) {
      ProcessRestyledFrames(currentChanges);
      MOZ_ASSERT(currentChanges.IsEmpty());
      for (ReentrantChange& change : newChanges) {
        if (!(change.mHint & nsChangeHint_ReconstructFrame) &&
            !change.mContent->GetPrimaryFrame()) {
          // SVG Elements post change hints without ensuring that the primary
          // frame will be there after that (see bug 1366142).
          //
          // Just ignore those, since we can't really process them.
          continue;
        }
        currentChanges.AppendChange(change.mContent->GetPrimaryFrame(),
                                    change.mContent, change.mHint);
      }
      newChanges.Clear();
    }

    mReentrantChanges = nullptr;

    // Suppress adjustments in the after-change scroll anchors if needed, now
    // that we're done reframing everything.
    for (Element* element : anchorsToSuppress) {
      if (nsIFrame* frame = element->GetPrimaryFrame()) {
        if (auto* container = ScrollAnchorContainer::FindFor(frame)) {
          container->SuppressAdjustments();
        }
      }
    }

    if (anyStyleChanged) {
      // Maybe no styles changed when:
      //
      //  * Only explicit change hints were posted in the first place.
      //  * When an attribute or state change in the content happens not to need
      //    a restyle after all.
      //
      // In any case, we don't need to increment the restyle generation in that
      // case.
      IncrementRestyleGeneration();
    }

    mInStyleRefresh = false;
    presContext->UpdateContainerQueryStyles();
    mInStyleRefresh = true;
  }

  doc->ClearServoRestyleRoot();
  presContext->FinishedContainerQueryUpdate();
  presContext->UpdateHiddenByContentVisibilityForAnimationsIfNeeded();
  ClearSnapshots();
  mRestyledAsWholeContainer.Clear();
  styleSet->AssertTreeIsClean();

  mHaveNonAnimationRestyles = false;
  mRestyleForCSSRuleChanges = false;
  mInStyleRefresh = false;

  // Now that everything has settled, see if we have enough free rule nodes in
  // the tree to warrant sweeping them.
  styleSet->MaybeGCRuleTree();

  // Note: We are in the scope of |animationsWithDestroyedFrame|, so
  //       |mAnimationsWithDestroyedFrame| is still valid.
  MOZ_ASSERT(mAnimationsWithDestroyedFrame);
  mAnimationsWithDestroyedFrame->StopAnimationsForElementsWithoutFrames();
}

#ifdef DEBUG
static void VerifyFlatTree(const nsIContent& aContent) {
  StyleChildrenIterator iter(&aContent);

  for (auto* content = iter.GetNextChild(); content;
       content = iter.GetNextChild()) {
    MOZ_ASSERT(content->GetFlattenedTreeParentNodeForStyle() == &aContent);
    VerifyFlatTree(*content);
  }
}
#endif

void RestyleManager::ProcessPendingRestyles() {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Styles", LAYOUT);
#ifdef DEBUG
  if (auto* root = mPresContext->Document()->GetRootElement()) {
    VerifyFlatTree(*root);
  }
#endif

  DoProcessPendingRestyles(ServoTraversalFlags::Empty);
}

void RestyleManager::ProcessAllPendingAttributeAndStateInvalidations() {
  if (mSnapshots.IsEmpty()) {
    return;
  }
  for (const auto& key : mSnapshots.Keys()) {
    // Servo data for the element might have been dropped. (e.g. by removing
    // from its document)
    if (key->HasFlag(ELEMENT_HAS_SNAPSHOT)) {
      Servo_ProcessInvalidations(StyleSet()->RawData(), key, &mSnapshots);
    }
  }
  ClearSnapshots();
}

void RestyleManager::UpdateOnlyAnimationStyles() {
  bool doCSS = PresContext()->EffectCompositor()->HasPendingStyleUpdates();
  if (!doCSS) {
    return;
  }

  DoProcessPendingRestyles(ServoTraversalFlags::FlushThrottledAnimations);
}

void RestyleManager::ElementStateChanged(Element* aElement,
                                         ElementState aChangedBits) {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("ElementStateChanged",
                                      LAYOUT_StyleComputation);
#ifdef EARLY_BETA_OR_EARLIER
  if (MOZ_UNLIKELY(mInStyleRefresh)) {
    MOZ_CRASH_UNSAFE_PRINTF(
        "Element state change during style refresh (%" PRIu64 ")",
        aChangedBits.GetInternalValue());
  }
#endif

  const ElementState kVisitedAndUnvisited =
      ElementState::VISITED | ElementState::UNVISITED;

  // We'll restyle when the relevant visited query finishes, regardless of the
  // style (see Link::VisitedQueryFinished). So there's no need to do anything
  // as a result of this state change just yet.
  //
  // Note that this check checks for _both_ bits: This is only true when visited
  // changes to unvisited or vice-versa, but not when we start or stop being a
  // link itself.
  if (aChangedBits.HasAllStates(kVisitedAndUnvisited)) {
    aChangedBits &= ~kVisitedAndUnvisited;
    if (aChangedBits.IsEmpty()) {
      return;
    }
  }

  if (auto changeHint = ChangeForContentStateChange(*aElement, aChangedBits)) {
    Servo_NoteExplicitHints(aElement, RestyleHint{0}, changeHint);
  }

  // Don't bother taking a snapshot if no rules depend on these state bits.
  //
  // We always take a snapshot for the LTR/RTL event states, since Servo doesn't
  // track those bits in the same way, and we know that :dir() rules are always
  // present in UA style sheets.
  if (!aChangedBits.HasAtLeastOneOfStates(ElementState::DIR_STATES) &&
      !StyleSet()->HasStateDependency(*aElement, aChangedBits)) {
    return;
  }

  // Assuming we need to invalidate cached style in getComputedStyle for
  // undisplayed elements, since we don't know if it is needed.
  IncrementUndisplayedRestyleGeneration();

  const bool hasData = aElement->HasServoData();
  if (!hasData &&
      !(aElement->GetSelectorFlags() &
        NodeSelectorFlags::RelativeSelectorSearchDirectionAncestorSibling)) {
    return;
  }

  ServoElementSnapshot& snapshot = SnapshotFor(*aElement);
  ElementState previousState = aElement->StyleState() ^ aChangedBits;
  snapshot.AddState(previousState);

  ServoStyleSet& styleSet = *StyleSet();
  if (hasData) {
    // If the element has no style data, the siblings don't need to be
    // invalidated, we're inside a display: none subtree anyways. We still need
    // to invalidate for :has() tho, because that might go upwards.
    MaybeRestyleForNthOfState(styleSet, aElement, aChangedBits);
  }
  MaybeRestyleForRelativeSelectorState(styleSet, aElement, aChangedBits);
}

void RestyleManager::CustomStatesWillChange(Element& aElement) {
  MOZ_DIAGNOSTIC_ASSERT(!mInStyleRefresh);

  IncrementUndisplayedRestyleGeneration();

  // Relative selector invalidation travels ancestor and earlier sibling
  // direction, so it's very possible that it invalidates a styled element.
  if (!aElement.HasServoData() &&
      !(aElement.GetSelectorFlags() &
        NodeSelectorFlags::RelativeSelectorSearchDirectionAncestorSibling)) {
    return;
  }

  ServoElementSnapshot& snapshot = SnapshotFor(aElement);
  snapshot.AddCustomStates(aElement);
}

void RestyleManager::CustomStateChanged(Element& aElement, nsAtom* aState) {
  ServoStyleSet& styleSet = *StyleSet();
  MaybeRestyleForNthOfCustomState(styleSet, aElement, aState);
  styleSet.MaybeInvalidateRelativeSelectorCustomStateDependency(
      aElement, aState, Snapshots());
}

void RestyleManager::MaybeRestyleForNthOfCustomState(ServoStyleSet& aStyleSet,
                                                     Element& aChild,
                                                     nsAtom* aState) {
  const auto* parentNode = aChild.GetParentNode();
  MOZ_ASSERT(parentNode);
  const auto parentFlags = parentNode->GetSelectorFlags();
  if (!(parentFlags & NodeSelectorFlags::HasSlowSelectorNthOf)) {
    return;
  }

  if (aStyleSet.HasNthOfCustomStateDependency(aChild, aState)) {
    RestyleSiblingsForNthOf(&aChild, parentFlags);
  }
}

void RestyleManager::MaybeRestyleForNthOfState(ServoStyleSet& aStyleSet,
                                               Element* aChild,
                                               ElementState aChangedBits) {
  const auto* parentNode = aChild->GetParentNode();
  MOZ_ASSERT(parentNode);
  const auto parentFlags = parentNode->GetSelectorFlags();
  if (!(parentFlags & NodeSelectorFlags::HasSlowSelectorNthOf)) {
    return;
  }

  if (aStyleSet.HasNthOfStateDependency(*aChild, aChangedBits)) {
    RestyleSiblingsForNthOf(aChild, parentFlags);
  }
}

static inline bool AttributeInfluencesOtherPseudoClassState(
    const Element& aElement, const nsAtom* aAttribute) {
  // We must record some state for :-moz-table-border-nonzero and
  // :-moz-select-list-box.

  if (aAttribute == nsGkAtoms::border) {
    return aElement.IsHTMLElement(nsGkAtoms::table);
  }

  if (aAttribute == nsGkAtoms::multiple || aAttribute == nsGkAtoms::size) {
    return aElement.IsHTMLElement(nsGkAtoms::select);
  }

  return false;
}

static inline bool NeedToRecordAttrChange(
    const ServoStyleSet& aStyleSet, const Element& aElement,
    int32_t aNameSpaceID, nsAtom* aAttribute,
    bool* aInfluencesOtherPseudoClassState) {
  *aInfluencesOtherPseudoClassState =
      AttributeInfluencesOtherPseudoClassState(aElement, aAttribute);

  // If the attribute influences one of the pseudo-classes that are backed by
  // attributes, we just record it.
  if (*aInfluencesOtherPseudoClassState) {
    return true;
  }

  // We assume that id and class attributes are used in class/id selectors, and
  // thus record them.
  //
  // TODO(emilio): We keep a filter of the ids in use somewhere in the StyleSet,
  // presumably we could try to filter the old and new id, but it's not clear
  // it's worth it.
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::id || aAttribute == nsGkAtoms::_class)) {
    return true;
  }

  // We always record lang="", even though we force a subtree restyle when it
  // changes, since it can change how its siblings match :lang(..) due to
  // selectors like :lang(..) + div.
  if (aAttribute == nsGkAtoms::lang) {
    return true;
  }

  // Otherwise, just record the attribute change if a selector in the page may
  // reference it from an attribute selector.
  return aStyleSet.MightHaveAttributeDependency(aElement, aAttribute);
}

void RestyleManager::AttributeWillChange(Element* aElement,
                                         int32_t aNameSpaceID,
                                         nsAtom* aAttribute, int32_t aModType) {
  TakeSnapshotForAttributeChange(*aElement, aNameSpaceID, aAttribute);
}

void RestyleManager::ClassAttributeWillBeChangedBySMIL(Element* aElement) {
  TakeSnapshotForAttributeChange(*aElement, kNameSpaceID_None,
                                 nsGkAtoms::_class);
}

void RestyleManager::TakeSnapshotForAttributeChange(Element& aElement,
                                                    int32_t aNameSpaceID,
                                                    nsAtom* aAttribute) {
  MOZ_DIAGNOSTIC_ASSERT(!mInStyleRefresh);

  bool influencesOtherPseudoClassState;
  if (!NeedToRecordAttrChange(*StyleSet(), aElement, aNameSpaceID, aAttribute,
                              &influencesOtherPseudoClassState)) {
    return;
  }

  // We cannot tell if the attribute change will affect the styles of
  // undisplayed elements, because we don't actually restyle those elements
  // during the restyle traversal. So just assume that the attribute change can
  // cause the style to change.
  IncrementUndisplayedRestyleGeneration();

  // Relative selector invalidation travels ancestor and earlier sibling
  // direction, so it's very possible that it invalidates a styled element.
  if (!aElement.HasServoData() &&
      !(aElement.GetSelectorFlags() &
        NodeSelectorFlags::RelativeSelectorSearchDirectionAncestorSibling)) {
    return;
  }

  // Some other random attribute changes may also affect the transitions,
  // so we also set this true here.
  mHaveNonAnimationRestyles = true;

  ServoElementSnapshot& snapshot = SnapshotFor(aElement);
  snapshot.AddAttrs(aElement, aNameSpaceID, aAttribute);

  if (influencesOtherPseudoClassState) {
    snapshot.AddOtherPseudoClassState(aElement);
  }
}

// For some attribute changes we must restyle the whole subtree:
//
// * lang="" and xml:lang="" can affect all descendants due to :lang()
// * exportparts can affect all descendant parts. We could certainly integrate
//   it better in the invalidation machinery if it was necessary.
static inline bool AttributeChangeRequiresSubtreeRestyle(
    const Element& aElement, nsAtom* aAttr) {
  if (aAttr == nsGkAtoms::exportparts) {
    // TODO(emilio, bug 1598094): Maybe finer-grained invalidation for
    // exportparts attribute changes?
    return !!aElement.GetShadowRoot();
  }
  return aAttr == nsGkAtoms::lang;
}

void RestyleManager::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                      nsAtom* aAttribute, int32_t aModType,
                                      const nsAttrValue* aOldValue) {
  MOZ_ASSERT(!mInStyleRefresh);

  auto changeHint = nsChangeHint(0);
  auto restyleHint = RestyleHint{0};

  changeHint |= aElement->GetAttributeChangeHint(aAttribute, aModType);

  MaybeRestyleForNthOfAttribute(aElement, aNameSpaceID, aAttribute, aOldValue);
  MaybeRestyleForRelativeSelectorAttribute(aElement, aNameSpaceID, aAttribute,
                                           aOldValue);

  if (aAttribute == nsGkAtoms::style) {
    restyleHint |= RestyleHint::RESTYLE_STYLE_ATTRIBUTE;
  } else if (AttributeChangeRequiresSubtreeRestyle(*aElement, aAttribute)) {
    restyleHint |= RestyleHint::RestyleSubtree();
  } else if (aElement->IsInShadowTree() && aAttribute == nsGkAtoms::part) {
    // TODO(emilio, bug 1598094): Maybe finer-grained invalidation for part
    // attribute changes?
    restyleHint |= RestyleHint::RESTYLE_SELF | RestyleHint::RESTYLE_PSEUDOS;
  }

  if (nsIFrame* primaryFrame = aElement->GetPrimaryFrame()) {
    // See if we have appearance information for a theme.
    StyleAppearance appearance =
        primaryFrame->StyleDisplay()->EffectiveAppearance();
    if (appearance != StyleAppearance::None) {
      nsITheme* theme = PresContext()->Theme();
      if (theme->ThemeSupportsWidget(PresContext(), primaryFrame, appearance) &&
          theme->WidgetAttributeChangeRequiresRepaint(appearance, aAttribute)) {
        changeHint |= nsChangeHint_RepaintFrame;
      }
    }

    primaryFrame->AttributeChanged(aNameSpaceID, aAttribute, aModType);
  }

  if (restyleHint || changeHint) {
    Servo_NoteExplicitHints(aElement, restyleHint, changeHint);
  }

  if (restyleHint) {
    // Assuming we need to invalidate cached style in getComputedStyle for
    // undisplayed elements, since we don't know if it is needed.
    IncrementUndisplayedRestyleGeneration();

    // If we change attributes, we have to mark this to be true, so we will
    // increase the animation generation for the new created transition if any.
    mHaveNonAnimationRestyles = true;
  }
}

void RestyleManager::RestyleSiblingsForNthOf(Element* aChild,
                                             NodeSelectorFlags aParentFlags) {
  StyleSet()->RestyleSiblingsForNthOf(*aChild,
                                      static_cast<uint32_t>(aParentFlags));
}

void RestyleManager::MaybeRestyleForNthOfAttribute(
    Element* aChild, int32_t aNameSpaceID, nsAtom* aAttribute,
    const nsAttrValue* aOldValue) {
  const auto* parentNode = aChild->GetParentNode();
  MOZ_ASSERT(parentNode);
  const auto parentFlags = parentNode->GetSelectorFlags();
  if (!(parentFlags & NodeSelectorFlags::HasSlowSelectorNthOf)) {
    return;
  }
  if (!aChild->HasServoData()) {
    return;
  }

  bool mightHaveNthOfDependency;
  auto& styleSet = *StyleSet();
  if (aAttribute == nsGkAtoms::id &&
      MOZ_LIKELY(aNameSpaceID == kNameSpaceID_None)) {
    auto* const oldAtom = aOldValue->Type() == nsAttrValue::eAtom
                              ? aOldValue->GetAtomValue()
                              : nullptr;
    mightHaveNthOfDependency =
        styleSet.MightHaveNthOfIDDependency(*aChild, oldAtom, aChild->GetID());
  } else if (aAttribute == nsGkAtoms::_class &&
             MOZ_LIKELY(aNameSpaceID == kNameSpaceID_None)) {
    mightHaveNthOfDependency = styleSet.MightHaveNthOfClassDependency(*aChild);
  } else {
    mightHaveNthOfDependency =
        styleSet.MightHaveNthOfAttributeDependency(*aChild, aAttribute);
  }

  if (mightHaveNthOfDependency) {
    RestyleSiblingsForNthOf(aChild, parentFlags);
  }
}

void RestyleManager::MaybeRestyleForRelativeSelectorAttribute(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    const nsAttrValue* aOldValue) {
  if (!aElement->HasFlag(ELEMENT_HAS_SNAPSHOT)) {
    return;
  }
  auto& styleSet = *StyleSet();
  if (aAttribute == nsGkAtoms::id &&
      MOZ_LIKELY(aNameSpaceID == kNameSpaceID_None)) {
    auto* const oldAtom = aOldValue->Type() == nsAttrValue::eAtom
                              ? aOldValue->GetAtomValue()
                              : nullptr;
    styleSet.MaybeInvalidateRelativeSelectorIDDependency(
        *aElement, oldAtom, aElement->GetID(), Snapshots());
  } else if (aAttribute == nsGkAtoms::_class &&
             MOZ_LIKELY(aNameSpaceID == kNameSpaceID_None)) {
    styleSet.MaybeInvalidateRelativeSelectorClassDependency(*aElement,
                                                            Snapshots());
  } else {
    styleSet.MaybeInvalidateRelativeSelectorAttributeDependency(
        *aElement, aAttribute, Snapshots());
  }
}

void RestyleManager::MaybeRestyleForRelativeSelectorState(
    ServoStyleSet& aStyleSet, Element* aElement, ElementState aChangedBits) {
  if (!aElement->HasFlag(ELEMENT_HAS_SNAPSHOT)) {
    return;
  }
  aStyleSet.MaybeInvalidateRelativeSelectorStateDependency(
      *aElement, aChangedBits, Snapshots());
}

void RestyleManager::ReparentComputedStyleForFirstLine(nsIFrame* aFrame) {
  // This is only called when moving frames in or out of the first-line
  // pseudo-element (or one of its descendants).  We can't say much about
  // aFrame's ancestors, unfortunately (e.g. during a dynamic insert into
  // something inside an inline-block on the first line the ancestors could be
  // totally arbitrary), but we will definitely find a line frame on the
  // ancestor chain.  Note that the lineframe may not actually be the one that
  // corresponds to ::first-line; when we're moving _out_ of the ::first-line it
  // will be one of the continuations instead.
#ifdef DEBUG
  {
    nsIFrame* f = aFrame->GetParent();
    while (f && !f->IsLineFrame()) {
      f = f->GetParent();
    }
    MOZ_ASSERT(f, "Must have found a first-line frame");
  }
#endif

  DoReparentComputedStyleForFirstLine(aFrame, *StyleSet());
}

static bool IsFrameAboutToGoAway(nsIFrame* aFrame) {
  auto* element = Element::FromNode(aFrame->GetContent());
  if (!element) {
    return false;
  }
  return !element->HasServoData();
}

void RestyleManager::DoReparentComputedStyleForFirstLine(
    nsIFrame* aFrame, ServoStyleSet& aStyleSet) {
  if (aFrame->IsBackdropFrame()) {
    // Style context of backdrop frame has no parent style, and thus we do not
    // need to reparent it.
    return;
  }

  if (IsFrameAboutToGoAway(aFrame)) {
    // We're entering a display: none subtree, which we know it's going to get
    // rebuilt. Don't bother reparenting.
    return;
  }

  if (aFrame->IsPlaceholderFrame()) {
    // Also reparent the out-of-flow and all its continuations.  We're doing
    // this to match Gecko for now, but it's not clear that this behavior is
    // correct per spec.  It's certainly pretty odd for out-of-flows whose
    // containing block is not within the first line.
    //
    // Right now we're somewhat inconsistent in this testcase:
    //
    //  <style>
    //    div { color: orange; clear: left; }
    //    div::first-line { color: blue; }
    //  </style>
    //  <div>
    //    <span style="float: left">What color is this text?</span>
    //  </div>
    //  <div>
    //    <span><span style="float: left">What color is this text?</span></span>
    //  </div>
    //
    // We make the first float orange and the second float blue.  On the other
    // hand, if the float were within an inline-block that was on the first
    // line, arguably it _should_ inherit from the ::first-line...
    nsIFrame* outOfFlow =
        nsPlaceholderFrame::GetRealFrameForPlaceholder(aFrame);
    MOZ_ASSERT(outOfFlow, "no out-of-flow frame");
    for (; outOfFlow; outOfFlow = outOfFlow->GetNextContinuation()) {
      DoReparentComputedStyleForFirstLine(outOfFlow, aStyleSet);
    }
  }

  nsIFrame* providerFrame;
  ComputedStyle* newParentStyle =
      aFrame->GetParentComputedStyle(&providerFrame);
  // If our provider is our child, we want to reparent it first, because we
  // inherit style from it.
  bool isChild = providerFrame && providerFrame->GetParent() == aFrame;
  nsIFrame* providerChild = nullptr;
  if (isChild) {
    DoReparentComputedStyleForFirstLine(providerFrame, aStyleSet);
    // Get the style again after ReparentComputedStyle() which might have
    // changed it.
    newParentStyle = providerFrame->Style();
    providerChild = providerFrame;
    MOZ_ASSERT(!providerFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
               "Out of flow provider?");
  }

  if (!newParentStyle) {
    // No need to do anything here for this frame, but we should still reparent
    // its descendants, because those may have styles that inherit from the
    // parent of this frame (e.g. non-anonymous columns in an anonymous
    // colgroup).
    MOZ_ASSERT(aFrame->Style()->IsNonInheritingAnonBox(),
               "Why did this frame not end up with a parent context?");
    ReparentFrameDescendants(aFrame, providerChild, aStyleSet);
    return;
  }

  bool isElement = aFrame->GetContent()->IsElement();

  // We probably don't want to initiate transitions from ReparentComputedStyle,
  // since we call it during frame construction rather than in response to
  // dynamic changes.
  // Also see the comment at the start of
  // nsTransitionManager::ConsiderInitiatingTransition.
  //
  // We don't try to do the fancy copying from previous continuations that
  // GeckoRestyleManager does here, because that relies on knowing the parents
  // of ComputedStyles, and we don't know those.
  ComputedStyle* oldStyle = aFrame->Style();
  Element* ourElement = isElement ? aFrame->GetContent()->AsElement() : nullptr;
  ComputedStyle* newParent = newParentStyle;

  if (!providerFrame) {
    // No providerFrame means we inherited from a display:contents thing.  Our
    // layout parent style is the style of our nearest ancestor frame.  But we
    // have to be careful to do that with our placeholder, not with us, if we're
    // out of flow.
    if (aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
      aFrame->FirstContinuation()
          ->GetPlaceholderFrame()
          ->GetLayoutParentStyleForOutOfFlow(&providerFrame);
    } else {
      providerFrame = nsIFrame::CorrectStyleParentFrame(
          aFrame->GetParent(), oldStyle->GetPseudoType());
    }
  }
  ComputedStyle* layoutParent = providerFrame->Style();

  RefPtr<ComputedStyle> newStyle = aStyleSet.ReparentComputedStyle(
      oldStyle, newParent, layoutParent, ourElement);
  aFrame->SetComputedStyle(newStyle);

  // This logic somewhat mirrors the logic in
  // RestyleManager::ProcessPostTraversal.
  if (isElement) {
    // We can't use UpdateAdditionalComputedStyles as-is because it needs a
    // ServoRestyleState and maintaining one of those during a _frametree_
    // traversal is basically impossible.
    int32_t index = 0;
    while (auto* oldAdditionalStyle =
               aFrame->GetAdditionalComputedStyle(index)) {
      RefPtr<ComputedStyle> newAdditionalContext =
          aStyleSet.ReparentComputedStyle(oldAdditionalStyle, newStyle,
                                          newStyle, nullptr);
      aFrame->SetAdditionalComputedStyle(index, newAdditionalContext);
      ++index;
    }
  }

  // Generally, owned anon boxes are our descendants.  The only exceptions are
  // tables (for the table wrapper) and inline frames (for the block part of the
  // block-in-inline split).  We're going to update our descendants when looping
  // over kids, and we don't want to update the block part of a block-in-inline
  // split if the inline is on the first line but the block is not (and if the
  // block is, it's the child of something else on the first line and will get
  // updated as a child).  And given how this method ends up getting called, if
  // we reach here for a table frame, we are already in the middle of
  // reparenting the table wrapper frame.  So no need to
  // UpdateStyleOfOwnedAnonBoxes() here.

  ReparentFrameDescendants(aFrame, providerChild, aStyleSet);

  // We do not need to do the equivalent of UpdateFramePseudoElementStyles,
  // because those are handled by our descendant walk.
}

void RestyleManager::ReparentFrameDescendants(nsIFrame* aFrame,
                                              nsIFrame* aProviderChild,
                                              ServoStyleSet& aStyleSet) {
  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      // only do frames that are in flow
      if (!child->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
          child != aProviderChild) {
        DoReparentComputedStyleForFirstLine(child, aStyleSet);
      }
    }
  }
}

}  // namespace mozilla
