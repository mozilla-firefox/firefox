/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"

#include "EditAction.h"
#include "EditorDOMAPIWrapper.h"
#include "EditorUtils.h"
#include "HTMLEditorNestedClasses.h"

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Range.h"
#include "mozInlineSpellChecker.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIMutationObserver.h"
#include "nsINode.h"
#include "nsThreadUtils.h"

namespace mozilla {

using namespace dom;

/******************************************************************************
 * DOM mutation logger
 ******************************************************************************/

// - HTMLEditorMutation:3: Logging only mutations in editable containers which
// is not expected.
// - HTMLEditorMutation:4: Logging only mutations in editable containers which
// is either expected or not expected.
// - HTMLEditorMutation:5: Logging any mutations including in
// non-editable containers.
LazyLogModule gHTMLEditorMutationLog("HTMLEditorMutation");

LogLevel HTMLEditor::MutationLogLevelOf(
    nsIContent* aContent,
    const CharacterDataChangeInfo* aCharacterDataChangeInfo,
    DOMMutationType aDOMMutationType) const {
  // Should be called only when the "info" level is enabled at least since we
  // shouldn't add any new unnecessary calls in the hot paths when the logging
  // is disabled.
  MOZ_ASSERT(MOZ_LOG_TEST(gHTMLEditorMutationLog, LogLevel::Info));

  if (MOZ_UNLIKELY(!aContent->IsInComposedDoc())) {
    return LogLevel::Disabled;
  }
  Element* const containerElement = aContent->GetAsElementOrParentElement();
  if (!containerElement || !containerElement->IsEditable()) {
    return MOZ_LOG_TEST(gHTMLEditorMutationLog, LogLevel::Verbose)
               ? LogLevel::Verbose
               : LogLevel::Disabled;
  }
  if (!mRunningDOMAPIWrapper) {
    return LogLevel::Info;
  }
  switch (aDOMMutationType) {
    case DOMMutationType::ContentAppended:
      return mRunningDOMAPIWrapper->IsExpectedContentAppended(aContent)
                 ? LogLevel::Debug
                 : LogLevel::Info;
    case DOMMutationType::ContentInserted:
      return mRunningDOMAPIWrapper->IsExpectedContentInserted(aContent)
                 ? LogLevel::Debug
                 : LogLevel::Info;
    case DOMMutationType::ContentWillBeRemoved:
      return mRunningDOMAPIWrapper->IsExpectedContentWillBeRemoved(aContent)
                 ? LogLevel::Debug
                 : LogLevel::Info;
    case DOMMutationType::CharacterDataChanged:
      MOZ_ASSERT(aCharacterDataChangeInfo);
      return mRunningDOMAPIWrapper->IsExpectedCharacterDataChanged(
                 aContent, *aCharacterDataChangeInfo)
                 ? LogLevel::Debug
                 : LogLevel::Info;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid DOMMutationType value");
      return LogLevel::Disabled;
  }
}

// - HTMLEditorAttrMutation:3: Logging only mutations of editable element which
// is not expected.
// - HTMLEditorAttrMutation:4: Logging only mutations of editable element which
// is either expected or not expected.
// - HTMLEditorAttrMutation:5: Logging any mutations including non-editable
// elements' attributes.
LazyLogModule gHTMLEditorAttrMutationLog("HTMLEditorAttrMutation");

LogLevel HTMLEditor::AttrMutationLogLevelOf(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    AttrModType aModType, const nsAttrValue* aOldValue) const {
  // Should be called only when the "info" level is enabled at least since we
  // shouldn't add any new unnecessary calls in the hot paths when the logging
  // is disabled.
  MOZ_ASSERT(MOZ_LOG_TEST(gHTMLEditorAttrMutationLog, LogLevel::Info));
  if (MOZ_UNLIKELY(!aElement->IsInComposedDoc())) {
    return LogLevel::Disabled;
  }
  if (!aElement->IsEditable()) {
    return MOZ_LOG_TEST(gHTMLEditorAttrMutationLog, LogLevel::Verbose)
               ? LogLevel::Verbose
               : LogLevel::Disabled;
  }
  if (!mRunningDOMAPIWrapper) {
    return LogLevel::Info;
  }
  return mRunningDOMAPIWrapper->IsExpectedAttributeChanged(
             aElement, aNameSpaceID, aAttribute, aModType, aOldValue)
             ? LogLevel::Debug
             : LogLevel::Info;
}

void HTMLEditor::MaybeLogContentAppended(nsIContent* aFirstNewContent) const {
  const LogLevel logLevel = MutationLogLevelOf(
      aFirstNewContent, nullptr, DOMMutationType::ContentAppended);
  if (logLevel == LogLevel::Disabled) {
    return;
  }
  MOZ_LOG(
      gHTMLEditorMutationLog, logLevel,
      ("%p %s ContentAppended: %s (previousSibling=%s, nextSibling=%s)", this,
       logLevel == LogLevel::Debug ? "HTMLEditor  " : "SomebodyElse",
       NodeToString(aFirstNewContent).get(),
       NodeToString(aFirstNewContent ? aFirstNewContent->GetPreviousSibling()
                                     : nullptr)
           .get(),
       NodeToString(aFirstNewContent ? aFirstNewContent->GetNextSibling()
                                     : nullptr)
           .get()));
}

void HTMLEditor::MaybeLogContentInserted(nsIContent* aChild) const {
  const LogLevel logLevel =
      MutationLogLevelOf(aChild, nullptr, DOMMutationType::ContentInserted);
  if (logLevel == LogLevel::Disabled) {
    return;
  }
  MOZ_LOG(gHTMLEditorMutationLog, logLevel,
          ("%p %s ContentInserted: %s (previousSibling=%s, nextSibling=%s)",
           this, logLevel == LogLevel::Debug ? "HTMLEditor  " : "SomebodyElse",
           NodeToString(aChild).get(),
           NodeToString(aChild ? aChild->GetPreviousSibling() : nullptr).get(),
           NodeToString(aChild ? aChild->GetNextSibling() : nullptr).get()));
}

void HTMLEditor::MaybeLogContentWillBeRemoved(nsIContent* aChild) const {
  const LogLevel logLevel = MutationLogLevelOf(
      aChild, nullptr, DOMMutationType::ContentWillBeRemoved);
  if (logLevel == LogLevel::Disabled) {
    return;
  }
  MOZ_LOG(
      gHTMLEditorMutationLog, logLevel,
      ("%p %s ContentWillBeRemoved: %s (previousSibling=%s, nextSibling=%s)",
       this, logLevel == LogLevel::Debug ? "HTMLEditor  " : "SomebodyElse",
       NodeToString(aChild).get(),
       NodeToString(aChild ? aChild->GetPreviousSibling() : nullptr).get(),
       NodeToString(aChild ? aChild->GetNextSibling() : nullptr).get()));
}

void HTMLEditor::MaybeLogCharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) const {
  const LogLevel logLevel = MutationLogLevelOf(
      aContent, &aInfo, DOMMutationType::CharacterDataChanged);
  if (logLevel == LogLevel::Disabled) {
    return;
  }
  nsAutoString data;
  aContent->GetCharacterDataBuffer()->AppendTo(data);
  MarkSelectionAndShrinkLongString shrunkenData(
      data, aInfo.mChangeStart, aInfo.mChangeStart + aInfo.mReplaceLength);
  MakeHumanFriendly(shrunkenData);
  MOZ_LOG(
      gHTMLEditorMutationLog, logLevel,
      ("%p %s CharacterDataChanged: %s, data=\"%s\", (length=%u), aInfo=%s",
       this, logLevel == LogLevel::Debug ? "HTMLEditor  " : "SomebodyElse",
       ToString(*aContent).c_str(), NS_ConvertUTF16toUTF8(shrunkenData).get(),
       aContent->Length(), ToString(aInfo).c_str()));
}

void HTMLEditor::MaybeLogAttributeChanged(Element* aElement,
                                          int32_t aNameSpaceID,
                                          nsAtom* aAttribute,
                                          AttrModType aModType,
                                          const nsAttrValue* aOldValue) const {
  const LogLevel logLevel = AttrMutationLogLevelOf(
      aElement, aNameSpaceID, aAttribute, aModType, aOldValue);
  if (logLevel == LogLevel::Disabled) {
    return;
  }
  nsAutoString value;
  aElement->GetAttr(aAttribute, value);
  MOZ_LOG(
      gHTMLEditorAttrMutationLog, logLevel,
      ("%p %s AttributeChanged: %s of %s %s", this,
       logLevel == LogLevel::Debug ? "HTMLEditor  " : "SomebodyElse",
       nsAutoAtomCString(aAttribute).get(), NodeToString(aElement).get(),
       aModType == AttrModType::Removal
           ? "Removed"
           : nsPrintfCString("to \"%s\"", NS_ConvertUTF16toUTF8(value).get())
                 .get()));
}

/******************************************************************************
 * mozilla::HTMLEditor - Start/end of a DOM API call to modify the DOM
 ******************************************************************************/

const AutoDOMAPIWrapperBase* HTMLEditor::OnDOMAPICallStart(
    const AutoDOMAPIWrapperBase& aWrapperBase) {
  const AutoDOMAPIWrapperBase* const prevRunner = mRunningDOMAPIWrapper;
  mRunningDOMAPIWrapper = &aWrapperBase;
  MOZ_LOG(gHTMLEditorMutationLog, LogLevel::Warning,
          (">>>> %p Calling DOM API: %s", this,
           ToString(*mRunningDOMAPIWrapper).c_str()));
  return prevRunner;
}

void HTMLEditor::OnDOMAPICallEnd(const AutoDOMAPIWrapperBase* aPrevWrapper) {
  MOZ_LOG(gHTMLEditorMutationLog, LogLevel::Warning,
          ("<<<< %p Called DOM API: %s", this,
           ToString(mRunningDOMAPIWrapper->Type()).c_str()));
  mRunningDOMAPIWrapper = aPrevWrapper;
}

/******************************************************************************
 * mozilla::HTMLEditor - mutation observers/handlers
 ******************************************************************************/

void HTMLEditor::NotifyRootChanged() {
  MOZ_ASSERT(mPendingRootElementUpdatedRunner,
             "HTMLEditor::NotifyRootChanged() should be called via a runner");
  mPendingRootElementUpdatedRunner = nullptr;

  nsCOMPtr<nsIMutationObserver> kungFuDeathGrip(this);

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return;
  }

  RemoveEventListeners();
  nsresult rv = InstallEventListeners();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::InstallEventListeners() failed, but ignored");
    return;
  }

  UpdateRootElement();

  if (MOZ_LIKELY(mRootElement)) {
    rv = MaybeCollapseSelectionAtFirstEditableNode(false);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::MaybeCollapseSelectionAtFirstEditableNode(false) "
          "failed, "
          "but ignored");
      return;
    }

    // When this editor has focus, we need to reset the selection limiter to
    // new root.  Otherwise, that is going to be done when this gets focus.
    nsCOMPtr<nsINode> node = GetFocusedNode();
    if (node) {
      DebugOnly<nsresult> rvIgnored = InitializeSelection(*node);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "EditorBase::InitializeSelection() failed, but ignored");
    }

    SyncRealTimeSpell();
  }

  RefPtr<Element> newRootElement(mRootElement);
  IMEStateManager::OnUpdateHTMLEditorRootElement(*this, newRootElement);
}

// nsStubMutationObserver::ContentAppended override
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLEditor::ContentAppended(
    nsIContent* aFirstNewContent, const ContentAppendInfo&) {
  DoContentInserted(aFirstNewContent, ContentNodeIs::Appended);
}

// nsStubMutationObserver::ContentInserted override
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLEditor::ContentInserted(
    nsIContent* aChild, const ContentInsertInfo&) {
  DoContentInserted(aChild, ContentNodeIs::Inserted);
}

bool HTMLEditor::IsInObservedSubtree(nsIContent* aChild) {
  if (!aChild) {
    return false;
  }

  // FIXME(emilio, bug 1596856): This should probably work if the root is in the
  // same shadow tree as the child, probably? I don't know what the
  // contenteditable-in-shadow-dom situation is.
  if (Element* root = GetRoot()) {
    // To be super safe here, check both ChromeOnlyAccess and NAC / Shadow DOM.
    // That catches (also unbound) native anonymous content and ShadowDOM.
    if (root->ChromeOnlyAccess() != aChild->ChromeOnlyAccess() ||
        root->IsInNativeAnonymousSubtree() !=
            aChild->IsInNativeAnonymousSubtree() ||
        root->IsInShadowTree() != aChild->IsInShadowTree()) {
      return false;
    }
  }

  return !aChild->ChromeOnlyAccess() && !aChild->IsInShadowTree() &&
         !aChild->IsInNativeAnonymousSubtree();
}

bool HTMLEditor::ShouldReplaceRootElement() const {
  if (!mRootElement) {
    // If we don't know what is our root element, we should find our root.
    return true;
  }

  // If we temporary set document root element to mRootElement, but there is
  // body element now, we should replace the root element by the body element.
  return mRootElement != GetBodyElement();
}

void HTMLEditor::DoContentInserted(nsIContent* aChild,
                                   ContentNodeIs aContentNodeIs) {
  MOZ_ASSERT(aChild);
  nsINode* container = aChild->GetParentNode();
  MOZ_ASSERT(container);

  if (!IsInObservedSubtree(aChild)) {
    return;
  }

  if (MOZ_LOG_TEST(gHTMLEditorMutationLog, LogLevel::Info)) {
    if (aContentNodeIs == ContentNodeIs::Appended) {
      MaybeLogContentAppended(aChild);
    } else {
      MOZ_ASSERT(aContentNodeIs == ContentNodeIs::Inserted);
      MaybeLogContentInserted(aChild);
    }
  }

  // XXX Why do we need this? This method is a helper of mutation observer.
  //     So, the callers of mutation observer should guarantee that this won't
  //     be deleted at least during the call.
  RefPtr<HTMLEditor> kungFuDeathGrip(this);

  // Do not create AutoEditActionDataSetter here because it grabs `Selection`,
  // but that appear in the profile. If you need to create to it in some cases,
  // you should do it in the minimum scope.

  if (ShouldReplaceRootElement()) {
    // Forget maybe disconnected root element right now because nobody should
    // work with it.
    mRootElement = nullptr;
    if (mPendingRootElementUpdatedRunner) {
      return;
    }
    mPendingRootElementUpdatedRunner = NewRunnableMethod(
        "HTMLEditor::NotifyRootChanged", this, &HTMLEditor::NotifyRootChanged);
    nsContentUtils::AddScriptRunner(
        do_AddRef(mPendingRootElementUpdatedRunner));
    return;
  }

  // We don't need to handle our own modifications
  // (If firing textupdate, then we do want to run spell check,
  //  since the textupdate handler may update the DOM arbitrarily.)
  if ((!GetTopLevelEditSubAction() || IsFiringTextUpdate()) &&
      container->IsEditable()) {
    if (EditorUtils::IsPaddingBRElementForEmptyEditor(*aChild)) {
      // Ignore insertion of the padding <br> element.
      return;
    }
    nsresult rv = RunOrScheduleOnModifyDocument();
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::RunOrScheduleOnModifyDocument() failed, but ignored");

    // Update spellcheck for only the newly-inserted node (bug 743819)
    if (mInlineSpellChecker) {
      nsIContent* endContent = aChild;
      if (aContentNodeIs == ContentNodeIs::Appended) {
        nsIContent* child = nullptr;
        for (child = aChild; child; child = child->GetNextSibling()) {
          if (child->InclusiveDescendantMayNeedSpellchecking(this)) {
            break;
          }
        }
        if (!child) {
          // No child needed spellchecking, return.
          return;
        }

        // Maybe more than 1 child was appended.
        endContent = container->GetLastChild();
      } else if (!aChild->InclusiveDescendantMayNeedSpellchecking(this)) {
        return;
      }

      RefPtr<dom::Range> range = dom::Range::Create(aChild);
      range->SelectNodesInContainer(container, aChild, endContent);
      DebugOnly<nsresult> rvIgnored =
          mInlineSpellChecker->SpellCheckRangeIgnoringWordAtCaret(range);
      NS_WARNING_ASSERTION(
          rvIgnored == NS_ERROR_NOT_INITIALIZED || NS_SUCCEEDED(rvIgnored),
          "mozInlineSpellChecker::SpellCheckRange() failed, but ignored");
    }
  }
}

// nsStubMutationObserver::ContentWillBeRemoved override
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLEditor::ContentWillBeRemoved(
    nsIContent* aChild, const ContentRemoveInfo&) {
  if (mLastCollapsibleWhiteSpaceAppendedTextNode == aChild) {
    mLastCollapsibleWhiteSpaceAppendedTextNode = nullptr;
  }

  if (!IsInObservedSubtree(aChild)) {
    return;
  }

  if (MOZ_LOG_TEST(gHTMLEditorMutationLog, LogLevel::Info)) {
    MaybeLogContentWillBeRemoved(aChild);
  }

  // XXX Why do we need to do this?  This method is a mutation observer's
  //     method.  Therefore, the caller should guarantee that this won't be
  //     deleted during the call.
  RefPtr<HTMLEditor> kungFuDeathGrip(this);

  // Do not create AutoEditActionDataSetter here because it grabs `Selection`,
  // but that appear in the profile. If you need to create to it in some cases,
  // you should do it in the minimum scope.

  // FYI: mRootElement may be the <body> of the document or the root element.
  // Therefore, we don't need to check it across shadow DOM boundaries.
  if (mRootElement && mRootElement->IsInclusiveDescendantOf(aChild)) {
    // Forget the disconnected root element right now because nobody should work
    // with it.
    mRootElement = nullptr;
    if (mPendingRootElementUpdatedRunner) {
      return;
    }
    mPendingRootElementUpdatedRunner = NewRunnableMethod(
        "HTMLEditor::NotifyRootChanged", this, &HTMLEditor::NotifyRootChanged);
    nsContentUtils::AddScriptRunner(
        do_AddRef(mPendingRootElementUpdatedRunner));
    return;
  }

  // We don't need to handle our own modifications
  if (!GetTopLevelEditSubAction() && aChild->GetParentNode()->IsEditable()) {
    if (aChild && EditorUtils::IsPaddingBRElementForEmptyEditor(*aChild)) {
      // Ignore removal of the padding <br> element for empty editor.
      return;
    }

    nsresult rv = RunOrScheduleOnModifyDocument(aChild);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::RunOrScheduleOnModifyDocument() failed, but ignored");
  }
}

// nsStubMutationObserver::CharacterDataChanged override
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLEditor::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  if (!IsInObservedSubtree(aContent)) {
    return;
  }
  if (MOZ_LOG_TEST(gHTMLEditorMutationLog, LogLevel::Info)) {
    MaybeLogCharacterDataChanged(aContent, aInfo);
  }
  // We don't need to handle our own modifications
  // (If firing textupdate, then we do want to run spell check,
  //  since the textupdate handler may update the DOM arbitrarily.)
  if (!mInlineSpellChecker || !aContent->IsEditable() ||
      (GetTopLevelEditSubAction() != EditSubAction::eNone &&
       !IsFiringTextUpdate())) {
    return;
  }

  nsIContent* parent = aContent->GetParent();
  if (!parent || !parent->InclusiveDescendantMayNeedSpellchecking(this)) {
    return;
  }

  RefPtr<dom::Range> range = dom::Range::Create(aContent);
  range->SelectNodesInContainer(parent, aContent, aContent);
  DebugOnly<nsresult> rvIgnored =
      mInlineSpellChecker->SpellCheckRangeIgnoringWordAtCaret(range);
}

nsresult HTMLEditor::RunOrScheduleOnModifyDocument(
    const nsIContent* /*aContentWillBeRemoved = nullptr */) {
  if (mPendingDocumentModifiedRunner) {
    return NS_OK;  // We've already posted same runnable into the queue.
  }
  mPendingDocumentModifiedRunner = new DocumentModifiedEvent(*this);
  nsContentUtils::AddScriptRunner(do_AddRef(mPendingDocumentModifiedRunner));
  // Be aware, if OnModifyDocument() may be called synchronously, the
  // editor might have been destroyed here.
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

// nsStubMutationObserver::AttributeChanged override
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLEditor::AttributeChanged(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    AttrModType aModType, const nsAttrValue* aOldValue) {
  if (MOZ_LOG_TEST(gHTMLEditorAttrMutationLog, LogLevel::Info) &&
      IsInObservedSubtree(aElement)) {
    MaybeLogAttributeChanged(aElement, aNameSpaceID, aAttribute, aModType,
                             aOldValue);
  }
}

nsresult HTMLEditor::OnModifyDocument(const DocumentModifiedEvent& aRunner) {
  MOZ_ASSERT(mPendingDocumentModifiedRunner,
             "HTMLEditor::OnModifyDocument() should be called via a runner");
  MOZ_ASSERT(&aRunner == mPendingDocumentModifiedRunner);
  mPendingDocumentModifiedRunner = nullptr;

  Maybe<AutoEditActionDataSetter> editActionData;
  if (!IsEditActionDataAvailable()) {
    editActionData.emplace(*this,
                           EditAction::eCreatePaddingBRElementForEmptyEditor);
    if (NS_WARN_IF(!editActionData->CanHandle())) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  // EnsureNoPaddingBRElementForEmptyEditor() below may cause a flush, which
  // could destroy the editor
  nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

  // Delete our padding <br> element for empty editor, if we have one, since
  // the document might not be empty any more.
  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return rv;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  // Try to recreate the padding <br> element for empty editor if needed.
  rv = MaybeCreatePaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::MaybeCreatePaddingBRElementForEmptyEditor() failed");

  return rv;
}

}  // namespace mozilla
