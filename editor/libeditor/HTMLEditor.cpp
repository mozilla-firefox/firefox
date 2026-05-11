/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditorInlines.h"
#include "HTMLEditorNestedClasses.h"

#include "AutoClonedRangeArray.h"
#include "AutoSelectionRestorer.h"
#include "CSSEditUtils.h"
#include "EditAction.h"
#include "EditorBase.h"
#include "EditorDOMAPIWrapper.h"
#include "EditorDOMPoint.h"
#include "EditorLineBreak.h"
#include "EditorUtils.h"
#include "ErrorList.h"
#include "HTMLEditorEventListener.h"
#include "HTMLEditUtils.h"
#include "InsertNodeTransaction.h"
#include "JoinNodesTransaction.h"
#include "MoveNodeTransaction.h"
#include "PendingStyles.h"
#include "ReplaceTextTransaction.h"
#include "SplitNodeTransaction.h"
#include "WhiteSpaceVisibilityKeeper.h"
#include "WSRunScanner.h"

#include "mozilla/ComposerCommandsUpdater.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EditorForwards.h"
#include "mozilla/Encoding.h"  // for Encoding
#include "mozilla/FlushType.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/IntegerRange.h"  // for IntegerRange
#include "mozilla/mozInlineSpellChecker.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/glean/EditorLibeditorMetrics.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TextServicesDocument.h"
#include "mozilla/ToString.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Attr.h"
#include "mozilla/dom/BorrowedAttrInfo.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/HTMLButtonElement.h"
#include "mozilla/dom/HTMLSummaryElement.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/Selection.h"

#include "mozilla/dom/ContentList.h"
#include "nsContentUtils.h"
#include "nsCRT.h"
#include "nsDebug.h"
#include "nsElementTable.h"
#include "nsFocusManager.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsHTMLDocument.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"  // for nsINode::IsInDesignMode()
#include "nsIEditActionListener.h"
#include "nsIFrame.h"
#include "nsIPrincipal.h"
#include "nsISelectionController.h"
#include "nsIURI.h"
#include "nsIWidget.h"
#include "nsNetUtil.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "nsPIDOMWindow.h"
#include "nsStyledElement.h"
#include "nsUnicharUtils.h"

namespace mozilla {

using namespace dom;
using namespace widget;

LazyLogModule gHTMLEditorFocusLog("HTMLEditorFocus");

using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;
using LeafNodeOptions = HTMLEditUtils::LeafNodeOptions;

// Some utilities to handle overloading of "A" tag for link and named anchor.
static bool IsLinkTag(const nsAtom& aTagName) {
  return &aTagName == nsGkAtoms::href;
}

static bool IsNamedAnchorTag(const nsAtom& aTagName) {
  return &aTagName == nsGkAtoms::anchor;
}

// Helper struct for DoJoinNodes() and DoSplitNode().
struct MOZ_STACK_CLASS SavedRange final {
  RefPtr<Selection> mSelection;
  nsCOMPtr<nsINode> mStartContainer;
  nsCOMPtr<nsINode> mEndContainer;
  uint32_t mStartOffset = 0;
  uint32_t mEndOffset = 0;
};

/******************************************************************************
 * HTMLEditor
 *****************************************************************************/

template Result<CreateContentResult, nsresult>
HTMLEditor::InsertNodeIntoProperAncestorWithTransaction(
    nsIContent& aContentToInsert, const EditorDOMPoint& aPointToInsert,
    SplitAtEdges aSplitAtEdges);
template Result<CreateElementResult, nsresult>
HTMLEditor::InsertNodeIntoProperAncestorWithTransaction(
    Element& aContentToInsert, const EditorDOMPoint& aPointToInsert,
    SplitAtEdges aSplitAtEdges);
template Result<CreateTextResult, nsresult>
HTMLEditor::InsertNodeIntoProperAncestorWithTransaction(
    Text& aContentToInsert, const EditorDOMPoint& aPointToInsert,
    SplitAtEdges aSplitAtEdges);

MOZ_RUNINIT HTMLEditor::InitializeInsertingElement
    HTMLEditor::DoNothingForNewElement =
        [](HTMLEditor&, Element&, const EditorDOMPoint&) { return NS_OK; };

MOZ_RUNINIT HTMLEditor::InitializeInsertingElement
    HTMLEditor::InsertNewBRElement =
        [](HTMLEditor& aHTMLEditor, Element& aNewElement,
           const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
          MOZ_ASSERT(!aNewElement.IsInComposedDoc());
          Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
              aHTMLEditor.InsertLineBreak(WithTransaction::No,
                                          LineBreakType::BRElement,
                                          EditorDOMPoint(&aNewElement, 0u));
          if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::InsertLineBreak(WithTransaction::No, "
                "LineBreakType::BRElement) failed");
            return insertBRElementResultOrError.unwrapErr();
          }
          insertBRElementResultOrError.unwrap().IgnoreCaretPointSuggestion();
          return NS_OK;
        };

// static
Result<CreateElementResult, nsresult>
HTMLEditor::AppendNewElementToInsertingElement(
    HTMLEditor& aHTMLEditor, const nsStaticAtom& aTagName, Element& aNewElement,
    const InitializeInsertingElement& aInitializer) {
  MOZ_ASSERT(!aNewElement.IsInComposedDoc());
  Result<CreateElementResult, nsresult> createNewElementResult =
      aHTMLEditor.CreateAndInsertElement(
          WithTransaction::No, const_cast<nsStaticAtom&>(aTagName),
          EditorDOMPoint(&aNewElement, 0u), aInitializer);
  NS_WARNING_ASSERTION(
      createNewElementResult.isOk(),
      "HTMLEditor::CreateAndInsertElement(WithTransaction::No) failed");
  return createNewElementResult;
}

// static
Result<CreateElementResult, nsresult>
HTMLEditor::AppendNewElementWithBRToInsertingElement(
    HTMLEditor& aHTMLEditor, const nsStaticAtom& aTagName,
    Element& aNewElement) {
  MOZ_ASSERT(!aNewElement.IsInComposedDoc());
  Result<CreateElementResult, nsresult> createNewElementWithBRResult =
      HTMLEditor::AppendNewElementToInsertingElement(
          aHTMLEditor, aTagName, aNewElement, HTMLEditor::InsertNewBRElement);
  NS_WARNING_ASSERTION(
      createNewElementWithBRResult.isOk(),
      "HTMLEditor::AppendNewElementToInsertingElement() failed");
  return createNewElementWithBRResult;
}

MOZ_RUNINIT HTMLEditor::AttributeFilter HTMLEditor::CopyAllAttributes =
    [](HTMLEditor&, const Element&, const Element&, int32_t, const nsAtom&,
       nsString&) { return true; };
MOZ_RUNINIT HTMLEditor::AttributeFilter HTMLEditor::CopyAllAttributesExceptId =
    [](HTMLEditor&, const Element&, const Element&, int32_t aNamespaceID,
       const nsAtom& aAttrName, nsString&) {
      return aNamespaceID != kNameSpaceID_None || &aAttrName != nsGkAtoms::id;
    };
MOZ_RUNINIT HTMLEditor::AttributeFilter HTMLEditor::CopyAllAttributesExceptDir =
    [](HTMLEditor&, const Element&, const Element&, int32_t aNamespaceID,
       const nsAtom& aAttrName, nsString&) {
      return aNamespaceID != kNameSpaceID_None || &aAttrName != nsGkAtoms::dir;
    };
MOZ_RUNINIT HTMLEditor::AttributeFilter
    HTMLEditor::CopyAllAttributesExceptIdAndDir =
        [](HTMLEditor&, const Element&, const Element&, int32_t aNamespaceID,
           const nsAtom& aAttrName, nsString&) {
          return !(
              aNamespaceID == kNameSpaceID_None &&
              (&aAttrName == nsGkAtoms::id || &aAttrName == nsGkAtoms::dir));
        };

HTMLEditor::HTMLEditor(const Document& aDocument)
    : EditorBase(EditorBase::EditorType::HTML),
      mCRInParagraphCreatesParagraph(false),
      mIsObjectResizingEnabled(
          StaticPrefs::editor_resizing_enabled_by_default()),
      mIsResizing(false),
      mPreserveRatio(false),
      mResizedObjectIsAnImage(false),
      mIsAbsolutelyPositioningEnabled(
          StaticPrefs::editor_positioning_enabled_by_default()),
      mResizedObjectIsAbsolutelyPositioned(false),
      mGrabberClicked(false),
      mIsMoving(false),
      mSnapToGridEnabled(false),
      mIsInlineTableEditingEnabled(
          StaticPrefs::editor_inline_table_editing_enabled_by_default()),
      mIsCSSPrefChecked(StaticPrefs::editor_use_css()),
      mOriginalX(0),
      mOriginalY(0),
      mResizedObjectX(0),
      mResizedObjectY(0),
      mResizedObjectWidth(0),
      mResizedObjectHeight(0),
      mResizedObjectMarginLeft(0),
      mResizedObjectMarginTop(0),
      mResizedObjectBorderLeft(0),
      mResizedObjectBorderTop(0),
      mXIncrementFactor(0),
      mYIncrementFactor(0),
      mWidthIncrementFactor(0),
      mHeightIncrementFactor(0),
      mInfoXIncrement(20),
      mInfoYIncrement(20),
      mPositionedObjectX(0),
      mPositionedObjectY(0),
      mPositionedObjectWidth(0),
      mPositionedObjectHeight(0),
      mPositionedObjectMarginLeft(0),
      mPositionedObjectMarginTop(0),
      mPositionedObjectBorderLeft(0),
      mPositionedObjectBorderTop(0),
      mGridSize(0),
      mDefaultParagraphSeparator(ParagraphSeparator::div) {}

HTMLEditor::~HTMLEditor() {
  glean::htmleditors::with_beforeinput_listeners
      .EnumGet(static_cast<glean::htmleditors::WithBeforeinputListenersLabel>(
          MayHaveBeforeInputEventListenersForTelemetry() ? 1 : 0))
      .Add();
  glean::htmleditors::overridden_by_beforeinput_listeners
      .EnumGet(static_cast<
               glean::htmleditors::OverriddenByBeforeinputListenersLabel>(
          mHasBeforeInputBeenCanceled ? 1 : 0))
      .Add();
  glean::htmleditors::with_mutation_observers_without_beforeinput_listeners
      .EnumGet(static_cast<
               glean::htmleditors::
                   WithMutationObserversWithoutBeforeinputListenersLabel>(
          !MayHaveBeforeInputEventListenersForTelemetry() &&
                  MutationObserverHasObservedNodeForTelemetry()
              ? 1
              : 0))
      .Add();

  mPendingStylesToApplyToNewContent = nullptr;

  if (mDisabledLinkHandling) {
    if (Document* doc = GetDocument()) {
      doc->SetLinkHandlingEnabled(mOldLinkHandlingEnabled);
    }
  }

  RemoveEventListeners();

  HideAnonymousEditingUIs();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLEditor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLEditor, EditorBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingStylesToApplyToNewContent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mComposerCommandsUpdater)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChangedRangeForTopLevelEditSubAction)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPaddingBRElementForEmptyEditor)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLastCollapsibleWhiteSpaceAppendedTextNode)
  tmp->HideAnonymousEditingUIs();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLEditor, EditorBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingStylesToApplyToNewContent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mComposerCommandsUpdater)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChangedRangeForTopLevelEditSubAction)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPaddingBRElementForEmptyEditor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLastCollapsibleWhiteSpaceAppendedTextNode)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTopLeftHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTopHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTopRightHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLeftHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRightHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBottomLeftHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBottomHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBottomRightHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mActivatedHandle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResizingShadow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResizingInfo)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResizedObject)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAbsolutelyPositionedObject)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGrabber)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPositioningShadow)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInlineEditedCell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAddColumnBeforeButton)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRemoveColumnButton)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAddColumnAfterButton)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAddRowBeforeButton)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRemoveRowButton)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAddRowAfterButton)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(HTMLEditor, EditorBase)
NS_IMPL_RELEASE_INHERITED(HTMLEditor, EditorBase)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(HTMLEditor)
  NS_INTERFACE_MAP_ENTRY(nsIHTMLEditor)
  NS_INTERFACE_MAP_ENTRY(nsIHTMLObjectResizer)
  NS_INTERFACE_MAP_ENTRY(nsIHTMLAbsPosEditor)
  NS_INTERFACE_MAP_ENTRY(nsIHTMLInlineTableEditor)
  NS_INTERFACE_MAP_ENTRY(nsITableEditor)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsIEditorMailSupport)
NS_INTERFACE_MAP_END_INHERITING(EditorBase)

nsresult HTMLEditor::Init(Document& aDocument,
                          ComposerCommandsUpdater& aComposerCommandsUpdater,
                          uint32_t aFlags) {
  MOZ_ASSERT(!mInitSucceeded,
             "HTMLEditor::Init() called again without calling PreDestroy()?");

  MOZ_DIAGNOSTIC_ASSERT(!mComposerCommandsUpdater ||
                        mComposerCommandsUpdater == &aComposerCommandsUpdater);
  mComposerCommandsUpdater = &aComposerCommandsUpdater;

  RefPtr<PresShell> presShell = aDocument.GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = InitInternal(aDocument, nullptr, *presShell, aFlags);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::InitInternal() failed");
    return rv;
  }

  // Init mutation observer
  aDocument.AddMutationObserverUnlessExists(this);

  if (!mRootElement) {
    UpdateRootElement();
  }

  // disable Composer-only features
  if (IsMailEditor()) {
    DebugOnly<nsresult> rvIgnored = SetAbsolutePositioningEnabled(false);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::SetAbsolutePositioningEnabled(false) failed, but ignored");
    rvIgnored = SetSnapToGridEnabled(false);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::SetSnapToGridEnabled(false) failed, but ignored");
  }

  // disable links
  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return NS_ERROR_FAILURE;
  }
  if (!IsPlaintextMailComposer() && !IsInteractionAllowed()) {
    mDisabledLinkHandling = true;
    mOldLinkHandlingEnabled = document->LinkHandlingEnabled();
    document->SetLinkHandlingEnabled(false);
  }

  // init the type-in state
  mPendingStylesToApplyToNewContent = new PendingStyles();

  AutoEditActionDataSetter editActionData(*this, EditAction::eInitializing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  rv = InitEditorContentAndSelection();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::InitEditorContentAndSelection() failed");
    // XXX Sholdn't we expose `NS_ERROR_EDITOR_DESTROYED` even though this
    //     is a public method?
    return EditorBase::ToGenericNSResult(rv);
  }

  // Throw away the old transaction manager if this is not the first time that
  // we're initializing the editor.
  ClearUndoRedo();
  EnableUndoRedo();  // FYI: Creating mTransactionManager in this call

  if (mTransactionManager) {
    mTransactionManager->Attach(*this);
  }

  MOZ_ASSERT(!mInitSucceeded, "HTMLEditor::Init() shouldn't be nested");
  mInitSucceeded = true;
  editActionData.OnEditorInitialized();
  return NS_OK;
}

nsresult HTMLEditor::PostCreate() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = PostCreateInternal();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::PostCreatInternal() failed");
  return rv;
}

void HTMLEditor::PreDestroy() {
  if (mDidPreDestroy) {
    return;
  }

  mInitSucceeded = false;

  // FYI: Cannot create AutoEditActionDataSetter here.  However, it does not
  //      necessary for the methods called by the following code.

  RefPtr<Document> document = GetDocument();
  if (document) {
    document->RemoveMutationObserver(this);
  }

  // Clean up after our anonymous content -- we don't want these nodes to
  // stay around (which they would, since the frames have an owning reference).
  PresShell* presShell = GetPresShell();
  if (presShell && presShell->IsDestroying()) {
    // Just destroying PresShell now.
    // We have to keep UI elements of anonymous content until PresShell
    // is destroyed.
    RefPtr<HTMLEditor> self = this;
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "HTMLEditor::PreDestroy", [self]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
          self->HideAnonymousEditingUIs();
        }));
  } else {
    // PresShell is alive or already gone.
    HideAnonymousEditingUIs();
  }

  mPaddingBRElementForEmptyEditor = nullptr;

  PreDestroyInternal();
}

bool HTMLEditor::IsStyleEditable() const {
  if (IsInDesignMode()) {
    return true;
  }
  if (IsPlaintextMailComposer()) {
    return false;
  }
  const Element* const editingHost = ComputeEditingHost(LimitInBodyElement::No);
  // Let's return true if there is no focused editing host for the backward
  // compatibility.
  return !editingHost || !editingHost->IsContentEditablePlainTextOnly();
}

NS_IMETHODIMP HTMLEditor::GetDocumentCharacterSet(nsACString& aCharacterSet) {
  nsresult rv = GetDocumentCharsetInternal(aCharacterSet);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetDocumentCharsetInternal() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::SetDocumentCharacterSet(
    const nsACString& aCharacterSet) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSetCharacterSet);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return EditorBase::ToGenericNSResult(NS_ERROR_NOT_INITIALIZED);
  }
  // This method is scriptable, so add-ons could pass in something other
  // than a canonical name.
  const Encoding* encoding = Encoding::ForLabelNoReplacement(aCharacterSet);
  if (!encoding) {
    NS_WARNING("Encoding::ForLabelNoReplacement() failed");
    return EditorBase::ToGenericNSResult(NS_ERROR_INVALID_ARG);
  }
  document->SetDocumentCharacterSet(WrapNotNull(encoding));

  // Update META charset element.
  if (UpdateMetaCharsetWithTransaction(*document, aCharacterSet)) {
    return NS_OK;
  }

  // Set attributes to the created element
  if (aCharacterSet.IsEmpty()) {
    return NS_OK;
  }

  RefPtr<ContentList> headElementList =
      document->GetElementsByTagName(u"head"_ns);
  if (NS_WARN_IF(!headElementList)) {
    return NS_OK;
  }

  nsCOMPtr<nsIContent> primaryHeadElement = headElementList->Item(0);
  if (NS_WARN_IF(!primaryHeadElement)) {
    return NS_OK;
  }

  // Create a new meta charset tag
  Result<CreateElementResult, nsresult> createNewMetaElementResult =
      CreateAndInsertElement(
          WithTransaction::Yes, *nsGkAtoms::meta,
          EditorDOMPoint(primaryHeadElement, 0),
          [&aCharacterSet](HTMLEditor&, Element& aMetaElement,
                           const EditorDOMPoint&) {
            MOZ_ASSERT(!aMetaElement.IsInComposedDoc());
            DebugOnly<nsresult> rvIgnored =
                aMetaElement.SetAttr(kNameSpaceID_None, nsGkAtoms::httpEquiv,
                                     u"Content-Type"_ns, false);
            NS_WARNING_ASSERTION(
                NS_SUCCEEDED(rvIgnored),
                "Element::SetAttr(nsGkAtoms::httpEquiv, \"Content-Type\", "
                "false) failed, but ignored");
            rvIgnored =
                aMetaElement.SetAttr(kNameSpaceID_None, nsGkAtoms::content,
                                     u"text/html;charset="_ns +
                                         NS_ConvertASCIItoUTF16(aCharacterSet),
                                     false);
            NS_WARNING_ASSERTION(
                NS_SUCCEEDED(rvIgnored),
                nsPrintfCString(
                    "Element::SetAttr(nsGkAtoms::content, "
                    "\"text/html;charset=%s\", false) failed, but ignored",
                    nsPromiseFlatCString(aCharacterSet).get())
                    .get());
            return NS_OK;
          });
  NS_WARNING_ASSERTION(createNewMetaElementResult.isOk(),
                       "HTMLEditor::CreateAndInsertElement(WithTransaction::"
                       "Yes, nsGkAtoms::meta) failed, but ignored");
  // Probably, we don't need to update selection in this case since we should
  // not put selection into <head> element.
  createNewMetaElementResult.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

bool HTMLEditor::UpdateMetaCharsetWithTransaction(
    Document& aDocument, const nsACString& aCharacterSet) {
  // get a list of META tags
  RefPtr<ContentList> metaElementList =
      aDocument.GetElementsByTagName(u"meta"_ns);
  if (NS_WARN_IF(!metaElementList)) {
    return false;
  }

  for (uint32_t i = 0; i < metaElementList->Length(true); ++i) {
    RefPtr<Element> metaElement = metaElementList->Item(i);
    MOZ_ASSERT(metaElement);

    nsAutoString currentValue;
    metaElement->GetAttr(nsGkAtoms::httpEquiv, currentValue);

    if (!FindInReadable(u"content-type"_ns, currentValue,
                        nsCaseInsensitiveStringComparator)) {
      continue;
    }

    metaElement->GetAttr(nsGkAtoms::content, currentValue);

    constexpr auto charsetEquals = u"charset="_ns;
    nsAString::const_iterator originalStart, start, end;
    originalStart = currentValue.BeginReading(start);
    currentValue.EndReading(end);
    if (!FindInReadable(charsetEquals, start, end,
                        nsCaseInsensitiveStringComparator)) {
      continue;
    }

    // set attribute to <original prefix> charset=text/html
    nsresult rv = SetAttributeWithTransaction(
        *metaElement, *nsGkAtoms::content,
        Substring(originalStart, start) + charsetEquals +
            NS_ConvertASCIItoUTF16(aCharacterSet));
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::SetAttributeWithTransaction(nsGkAtoms::content) failed");
    return NS_SUCCEEDED(rv);
  }
  return false;
}

NS_IMETHODIMP HTMLEditor::NotifySelectionChanged(Document* aDocument,
                                                 Selection* aSelection,
                                                 int16_t aReason,
                                                 int32_t aAmount) {
  if (NS_WARN_IF(!aDocument) || NS_WARN_IF(!aSelection)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mPendingStylesToApplyToNewContent) {
    RefPtr<PendingStyles> pendingStyles = mPendingStylesToApplyToNewContent;
    pendingStyles->OnSelectionChange(*this, aReason);

    // We used a class which derived from nsISelectionListener to call
    // HTMLEditor::RefreshEditingUI().  The lifetime of the class was
    // exactly same as mPendingStylesToApplyToNewContent.  So, call it only when
    // mPendingStylesToApplyToNewContent is not nullptr.
    if ((aReason & (nsISelectionListener::MOUSEDOWN_REASON |
                    nsISelectionListener::KEYPRESS_REASON |
                    nsISelectionListener::SELECTALL_REASON)) &&
        aSelection) {
      // the selection changed and we need to check if we have to
      // hide and/or redisplay resizing handles
      // FYI: This is an XPCOM method.  So, the caller, Selection, guarantees
      //      the lifetime of this instance.  So, don't need to grab this with
      //      local variable.
      DebugOnly<nsresult> rv = RefreshEditingUI();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::RefreshEditingUI() failed, but ignored");
    }
  }

  if (mComposerCommandsUpdater) {
    RefPtr<ComposerCommandsUpdater> updater = mComposerCommandsUpdater;
    updater->OnSelectionChange();
  }

  nsresult rv = EditorBase::NotifySelectionChanged(aDocument, aSelection,
                                                   aReason, aAmount);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::NotifySelectionChanged() failed");
  return rv;
}

void HTMLEditor::UpdateRootElement() {
  // Use the HTML documents body element as the editor root if we didn't
  // get a root element during initialization.

  mRootElement = GetBodyElement();
  if (!mRootElement) {
    RefPtr<Document> doc = GetDocument();
    if (doc) {
      // If there is no HTML body element,
      // we should use the document root element instead.
      mRootElement = doc->GetDocumentElement();
    }
    // else leave it null, for lack of anything better.
  }
}

nsresult HTMLEditor::FocusedElementOrDocumentBecomesEditable(
    Document& aDocument, Element* aElement) {
  MOZ_LOG(gHTMLEditorFocusLog, LogLevel::Info,
          ("%s(aDocument=%p, aElement=%s): mHasFocus=%s, mIsInDesignMode=%s, "
           "aDocument.IsInDesignMode()=%s, aElement->IsInDesignMode()=%s",
           __FUNCTION__, &aDocument, ToString(RefPtr{aElement}).c_str(),
           mHasFocus ? "true" : "false", mIsInDesignMode ? "true" : "false",
           aDocument.IsInDesignMode() ? "true" : "false",
           aElement ? (aElement->IsInDesignMode() ? "true" : "false") : "N/A"));

  const bool enteringInDesignMode =
      (aDocument.IsInDesignMode() && (!aElement || aElement->IsInDesignMode()));

  // If we should've already handled focus event, selection limiter should not
  // be set.  However, IMEStateManager is not notified the pseudo focus change
  // in this case. Therefore, we need to notify IMEStateManager of this.
  if (mHasFocus) {
    if (enteringInDesignMode) {
      mIsInDesignMode = true;
      return NS_OK;
    }
    // Although editor is already initialized due to re-used, ISM may not
    // create IME content observer yet. So we have to create it.
    Result<IMEState, nsresult> newStateOrError = GetPreferredIMEState();
    if (MOZ_UNLIKELY(newStateOrError.isErr())) {
      NS_WARNING("HTMLEditor::GetPreferredIMEState() failed");
      mIsInDesignMode = false;
      return NS_OK;
    }
    const RefPtr<Element> focusedElement = GetFocusedElement();
    if (focusedElement) {
      MOZ_ASSERT(focusedElement == aElement);
      TextControlElement* const textControlElement =
          TextControlElement::FromNode(focusedElement);
      if (textControlElement &&
          textControlElement->IsSingleLineTextControlOrTextArea()) {
        // Let's emulate blur first.
        DebugOnly<nsresult> rv = FinalizeSelection();
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "HTMLEditor::FinalizeSelection() failed, but ignored");
        mHasFocus = false;
        mIsInDesignMode = false;
      }
      IMEStateManager::UpdateIMEState(newStateOrError.unwrap(), focusedElement,
                                      *this);
      // XXX Do we need to notify focused TextEditor of focus?  In theory,
      // the TextEditor should get focus event in this case.
    }
    mIsInDesignMode = false;
    return NS_OK;
  }

  // If we should be in the design mode, we want to handle focus event fired
  // on the document node.  Therefore, we should emulate it here.
  if (enteringInDesignMode) {
    MOZ_ASSERT(&aDocument == GetDocument());
    nsresult rv = OnFocus(aDocument);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::OnFocus() failed");
    return rv;
  }

  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Otherwise, we should've already handled focus event on the element,
  // therefore, we need to emulate it here.
  MOZ_ASSERT(nsFocusManager::GetFocusedElementStatic() == aElement);
  nsresult rv = OnFocus(*aElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::OnFocus() failed");

  // Note that we don't need to call
  // IMEStateManager::MaybeOnEditableStateDisabled here because
  // EditorBase::OnFocus must have already been called IMEStateManager::OnFocus
  // if succeeded. And perhaps, it's okay that IME is not enabled when
  // HTMLEditor fails to start handling since nobody can handle composition
  // events anyway...

  return rv;
}

nsresult HTMLEditor::OnFocus(const nsINode& aOriginalEventTargetNode) {
  MOZ_LOG(gHTMLEditorFocusLog, LogLevel::Info,
          ("%s(aOriginalEventTarget=%s): mIsInDesignMode=%s, "
           "aOriginalEventTargetNode.IsInDesignMode()=%s",
           __FUNCTION__, ToString(RefPtr{&aOriginalEventTargetNode}).c_str(),
           mIsInDesignMode ? "true" : "false",
           aOriginalEventTargetNode.IsInDesignMode() ? "true" : "false"));

  // Before doing anything, we should check whether the original target is still
  // valid focus event target because it may have already lost focus.
  if (!CanKeepHandlingFocusEvent(aOriginalEventTargetNode)) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = EditorBase::OnFocus(aOriginalEventTargetNode);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::OnFocus() failed");
    return rv;
  }
  mHasFocus = true;
  mIsInDesignMode = aOriginalEventTargetNode.IsInDesignMode();
  if (StaticPrefs::dom_editcontext_enabled() && EditContext::IsAnyAttached()) {
    // Active EditContext may have changed
    aOriginalEventTargetNode.OwnerDoc()->UpdateTextEditContext();
  }

  return NS_OK;
}

nsresult HTMLEditor::FocusedElementOrDocumentBecomesNotEditable(
    HTMLEditor* aHTMLEditor, Document& aDocument, Element* aElement) {
  MOZ_LOG(
      gHTMLEditorFocusLog, LogLevel::Info,
      ("%s(aHTMLEditor=%p, aDocument=%p, aElement=%s): "
       "aHTMLEditor->HasFocus()=%s, aHTMLEditor->IsInDesignMode()=%s, "
       "aDocument.IsInDesignMode()=%s, aElement->IsInDesignMode()=%s, "
       "nsFocusManager::GetFocusedElementStatic()=%s",
       __FUNCTION__, aHTMLEditor, &aDocument,
       ToString(RefPtr{aElement}).c_str(),
       aHTMLEditor ? (aHTMLEditor->HasFocus() ? "true" : "false") : "N/A",
       aHTMLEditor ? (aHTMLEditor->IsInDesignMode() ? "true" : "false") : "N/A",
       aDocument.IsInDesignMode() ? "true" : "false",
       aElement ? (aElement->IsInDesignMode() ? "true" : "false") : "N/A",
       ToString(RefPtr{nsFocusManager::GetFocusedElementStatic()}).c_str()));

  nsresult rv = [&]() MOZ_CAN_RUN_SCRIPT {
    // If HTMLEditor has not been created yet, we just need to adjust
    // IMEStateManager.  So, don't return error.
    if (!aHTMLEditor || !aHTMLEditor->HasFocus()) {
      return NS_OK;
    }

    // Let's emulate blur first.
    nsresult rv = aHTMLEditor->FinalizeSelection();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::FinalizeSelection() failed");
    aHTMLEditor->mHasFocus = false;
    aHTMLEditor->mIsInDesignMode = false;

    if (StaticPrefs::dom_editcontext_enabled() &&
        EditContext::IsAnyAttached()) {
      // Active EditContext may have changed
      aDocument.UpdateTextEditContext();
    }

    RefPtr<Element> focusedElement = nsFocusManager::GetFocusedElementStatic();
    if (focusedElement && !focusedElement->IsInComposedDoc()) {
      // nsFocusManager may keep storing the focused element even after
      // disconnected from the tree, but HTMLEditor cannot work with editable
      // nodes not in a composed document.  Therefore, we should treat no
      // focused element in the case.
      focusedElement = nullptr;
    }
    TextControlElement* const focusedTextControlElement =
        TextControlElement::FromNodeOrNull(focusedElement);
    if ((focusedElement && focusedElement->IsEditable() &&
         focusedElement->OwnerDoc() == aHTMLEditor->GetDocument() &&
         (!focusedTextControlElement ||
          !focusedTextControlElement->IsSingleLineTextControlOrTextArea())) ||
        (!focusedElement && aDocument.IsInDesignMode())) {
      // Then, the focused element is still editable, let's emulate focus to
      // make the editor be ready to handle input.
      DebugOnly<nsresult> rvIgnored = aHTMLEditor->OnFocus(
          focusedElement ? static_cast<nsINode&>(*focusedElement)
                         : static_cast<nsINode&>(aDocument));
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "HTMLEditor::OnFocus() failed, but ignored");
    } else if (focusedTextControlElement &&
               focusedTextControlElement->IsSingleLineTextControlOrTextArea()) {
      if (const RefPtr<TextEditor> textEditor =
              focusedTextControlElement->GetExtantTextEditor()) {
        textEditor->OnFocus(*focusedElement);
      }
    }
    return rv;
  }();

  // If the element becomes not editable without focus change, IMEStateManager
  // does not have a chance to disable IME.  Therefore, (even if we fail to
  // handle the emulated blur/focus above,) we should notify IMEStateManager of
  // the editing state change.  Note that if the window of the HTMLEditor has
  // already lost focus, we don't need to do that and we should not touch the
  // other windows.
  if (const RefPtr<nsPresContext> presContext = aDocument.GetPresContext()) {
    const RefPtr<Element> focusedElementInDocument =
        Element::FromNodeOrNull(aDocument.GetUnretargetedFocusedContent());
    MOZ_ASSERT_IF(focusedElementInDocument,
                  focusedElementInDocument->GetPresContext(
                      Element::PresContextFor::eForComposedDoc));
    IMEStateManager::MaybeOnEditableStateDisabled(*presContext,
                                                  focusedElementInDocument);
  }

  return rv;
}

nsresult HTMLEditor::OnBlur(const EventTarget* aEventTarget) {
  MOZ_LOG(gHTMLEditorFocusLog, LogLevel::Info,
          ("%s(aEventTarget=%s): mHasFocus=%s, mIsInDesignMode=%s, "
           "aEventTarget->IsInDesignMode()=%s",
           __FUNCTION__, ToString(RefPtr{aEventTarget}).c_str(),
           mHasFocus ? "true" : "false", mIsInDesignMode ? "true" : "false",
           nsINode::FromEventTargetOrNull(aEventTarget)
               ? (nsINode::FromEventTarget(aEventTarget)->IsInDesignMode()
                      ? "true"
                      : "false")
               : "N/A"));
  const Element* eventTargetAsElement =
      Element::FromEventTargetOrNull(aEventTarget);

  // If another element already has focus, we should not maintain the selection
  // because we may not have the rights doing it.
  const Element* focusedElement = nsFocusManager::GetFocusedElementStatic();
  if (focusedElement && focusedElement != eventTargetAsElement) {
    // XXX If we had focus and new focused element is a text control, we may
    // need to notify focus of its TextEditor...
    mIsInDesignMode = false;
    mHasFocus = false;
    return NS_OK;
  }

  // If we're in the designMode and blur occurs, the target must be the document
  // node.  If a blur event is fired and the target is an element, it must be
  // delayed blur event at initializing the `HTMLEditor`.
  if (mIsInDesignMode && eventTargetAsElement &&
      eventTargetAsElement->IsInComposedDoc()) {
    return NS_OK;
  }

  nsresult rv = FinalizeSelection();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::FinalizeSelection() failed");
  mIsInDesignMode = false;
  mHasFocus = false;
  if (StaticPrefs::dom_editcontext_enabled() && EditContext::IsAnyAttached() &&
      eventTargetAsElement) {
    // Active EditContext may have changed
    eventTargetAsElement->OwnerDoc()->UpdateTextEditContext();
  }

  return rv;
}

Element* HTMLEditor::FindSelectionRoot(const nsINode& aNode) const {
  MOZ_ASSERT(aNode.IsDocument() || aNode.IsContent(),
             "aNode must be content or document node");

  if (NS_WARN_IF(!aNode.IsInComposedDoc())) {
    return nullptr;
  }

  if (aNode.IsInDesignMode()) {
    return GetDocument()->GetRootElement();
  }

  nsIContent* content = const_cast<nsIContent*>(aNode.AsContent());
  if (!content->HasFlag(NODE_IS_EDITABLE)) {
    // If the content is in read-write state but is not editable itself,
    // return it as the selection root.
    if (content->IsElement() &&
        content->AsElement()->State().HasState(ElementState::READWRITE)) {
      return content->AsElement();
    }
    return nullptr;
  }

  // For non-readonly editors we want to find the root of the editable subtree
  // containing aContent.
  return content->GetEditingHost();
}

bool HTMLEditor::EntireDocumentIsEditable() const {
  Document* document = GetDocument();
  return document && document->GetDocumentElement() &&
         (document->GetDocumentElement()->IsEditable() ||
          (document->GetBody() && document->GetBody()->IsEditable()));
}

void HTMLEditor::CreateEventListeners() {
  // Don't create the handler twice
  if (!mEventListener) {
    mEventListener = new HTMLEditorEventListener();
  }
}

nsresult HTMLEditor::InstallEventListeners() {
  // FIXME InstallEventListeners() should not be called if we failed to set
  // document or create an event listener.  So, these checks should be
  // MOZ_DIAGNOSTIC_ASSERT instead.
  MOZ_ASSERT(GetDocument());
  if (MOZ_UNLIKELY(!GetDocument()) || NS_WARN_IF(!mEventListener)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // NOTE: HTMLEditor doesn't need to initialize mEventTarget here because
  // the target must be document node and it must be referenced as weak pointer.

  HTMLEditorEventListener* listener =
      reinterpret_cast<HTMLEditorEventListener*>(mEventListener.get());
  nsresult rv = listener->Connect(this);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditorEventListener::Connect() failed");
  return rv;
}

void HTMLEditor::Detach(
    const ComposerCommandsUpdater& aComposerCommandsUpdater) {
  MOZ_DIAGNOSTIC_ASSERT_IF(
      mComposerCommandsUpdater,
      &aComposerCommandsUpdater == mComposerCommandsUpdater);
  if (mComposerCommandsUpdater == &aComposerCommandsUpdater) {
    mComposerCommandsUpdater = nullptr;
    if (mTransactionManager) {
      mTransactionManager->Detach(*this);
    }
  }
}

NS_IMETHODIMP HTMLEditor::BeginningOfDocument() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = MaybeCollapseSelectionAtFirstEditableNode(false);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::MaybeCollapseSelectionAtFirstEditableNode(false) failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::EndOfDocument() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  nsresult rv = CollapseSelectionToEndOfLastLeafNodeOfDocument();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::CollapseSelectionToEndOfLastLeafNodeOfDocument() failed");
  // This is low level API for embedders and chrome script so that we can return
  // raw error code here.
  return rv;
}

nsresult HTMLEditor::CollapseSelectionToEndOfLastLeafNodeOfDocument() const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // We should do nothing with the result of GetRoot() if only a part of the
  // document is editable.
  if (!EntireDocumentIsEditable()) {
    return NS_OK;
  }

  RefPtr<Element> bodyOrDocumentElement = GetRoot();
  if (NS_WARN_IF(!bodyOrDocumentElement)) {
    return NS_ERROR_NULL_POINTER;
  }

  auto pointToPutCaret = [&]() -> EditorRawDOMPoint {
    nsCOMPtr<nsIContent> lastLeafContent =
        HTMLEditUtils::GetLastLeafContent(*bodyOrDocumentElement, {});
    if (!lastLeafContent) {
      return EditorRawDOMPoint::AtEndOf(*bodyOrDocumentElement);
    }
    // TODO: We should put caret into text node if it's visible.
    return lastLeafContent->IsText() ||
                   HTMLEditUtils::IsContainerNode(*lastLeafContent)
               ? EditorRawDOMPoint::AtEndOf(*lastLeafContent)
               : EditorRawDOMPoint(lastLeafContent);
  }();
  nsresult rv = CollapseSelectionTo(pointToPutCaret);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

void HTMLEditor::InitializeSelectionAncestorLimit(
    Element& aAncestorLimit) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Hack for initializing selection.
  // HTMLEditor::MaybeCollapseSelectionAtFirstEditableNode() will try to
  // collapse selection at first editable text node or inline element which
  // cannot have text nodes as its children.  However, selection has already
  // set into the new editing host by user, we should not change it.  For
  // solving this issue, we should do nothing if selection range is in active
  // editing host except it's not collapsed at start of the editing host since
  // aSelection.SetAncestorLimiter(aAncestorLimit) will collapse selection
  // at start of the new limiter if focus node of aSelection is outside of the
  // editing host.  However, we need to check here if selection is already
  // collapsed at start of the editing host because it's possible JS to do it.
  // In such case, we should not modify selection with calling
  // MaybeCollapseSelectionAtFirstEditableNode().

  // Basically, we should try to collapse selection at first editable node
  // in HTMLEditor.
  bool tryToCollapseSelectionAtFirstEditableNode = true;
  if (SelectionRef().RangeCount() == 1 && SelectionRef().IsCollapsed()) {
    Element* editingHost = ComputeEditingHost();
    const nsRange* range = SelectionRef().GetRangeAt(0);
    if (range->GetStartContainer() == editingHost && !range->StartOffset()) {
      // JS or user operation has already collapsed selection at start of
      // the editing host.  So, we don't need to try to change selection
      // in this case.
      tryToCollapseSelectionAtFirstEditableNode = false;
    }
  }

  EditorBase::InitializeSelectionAncestorLimit(aAncestorLimit);

  // XXX Do we need to check if we still need to change selection?  E.g.,
  //     we could have already lost focus while we're changing the ancestor
  //     limiter because it may causes "selectionchange" event.
  if (tryToCollapseSelectionAtFirstEditableNode) {
    DebugOnly<nsresult> rvIgnored =
        MaybeCollapseSelectionAtFirstEditableNode(true);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::MaybeCollapseSelectionAtFirstEditableNode(true) failed, "
        "but ignored");
  }

  // If the target is a text control element, we won't handle user input
  // for the `TextEditor` in it.  However, we need to be open for `execCommand`.
  // Therefore, we shouldn't set ancestor limit in this case.
  // Note that we should do this once setting ancestor limiter for backward
  // compatiblity of select events, etc.  (Selection should be collapsed into
  // the text control element.)
  if (aAncestorLimit.HasIndependentSelection()) {
    SelectionRef().SetAncestorLimiter(nullptr);
  }
}

nsresult HTMLEditor::MaybeCollapseSelectionAtFirstEditableNode(
    bool aIgnoreIfSelectionInEditingHost) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> editingHost = ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_OK;
  }

  // If selection range is already in the editing host and the range is not
  // start of the editing host, we shouldn't reset selection.  E.g., window
  // is activated when the editor had focus before inactivated.
  if (aIgnoreIfSelectionInEditingHost && SelectionRef().RangeCount() == 1) {
    const nsRange* range = SelectionRef().GetRangeAt(0);
    if (!range->Collapsed() ||
        range->GetStartContainer() != editingHost.get() ||
        range->StartOffset()) {
      return NS_OK;
    }
  }

  constexpr LeafNodeOptions leafNodeOptions = {
      LeafNodeOption::TreatNonEditableNodeAsLeafNode,
      LeafNodeOption::TreatChildBlockAsLeafNode,
      // FIXME: Ignore empty inline containers such as <span></span> because we
      // cannot visually put caret into it.
  };
  for (nsIContent* leafContent = HTMLEditUtils::GetFirstLeafContent(
           *editingHost, leafNodeOptions,
           BlockInlineCheck::UseComputedDisplayStyle);
       leafContent;) {
    // If we meet a non-editable node first, we should move caret to start
    // of the container block or editing host.
    if (!EditorUtils::IsEditableContent(*leafContent, EditorType::HTML)) {
      MOZ_ASSERT(leafContent->GetParent());
      MOZ_ASSERT(EditorUtils::IsEditableContent(*leafContent->GetParent(),
                                                EditorType::HTML));
      if (const Element* editableBlockElementOrInlineEditingHost =
              HTMLEditUtils::GetAncestorElement(
                  *leafContent,
                  HTMLEditUtils::ClosestEditableBlockElementOrInlineEditingHost,
                  BlockInlineCheck::UseComputedDisplayStyle)) {
        nsresult rv = CollapseSelectionTo(
            EditorDOMPoint(editableBlockElementOrInlineEditingHost, 0));
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::CollapseSelectionTo() failed");
        return rv;
      }
      NS_WARNING("Found leaf content did not have editable parent, why?");
      return NS_ERROR_FAILURE;
    }

    // When we meet an empty inline element, we should look for a next sibling.
    // For example, if current editor is:
    // <div contenteditable><span></span><b><br></b></div>
    // then, we should put caret at the <br> element.  So, let's check if found
    // node is an empty inline container element.
    if (Element* leafElement = Element::FromNode(leafContent)) {
      if (HTMLEditUtils::IsInlineContent(
              *leafElement, BlockInlineCheck::UseComputedDisplayStyle) &&
          !HTMLEditUtils::IsNeverElementContentsEditableByUser(*leafElement) &&
          HTMLEditUtils::CanNodeContain(*leafElement,
                                        *nsGkAtoms::textTagName)) {
        // Chromium collapses selection to start of the editing host when this
        // is the last leaf content.  So, we don't need special handling here.
        leafContent = HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
            *leafElement, leafNodeOptions,
            BlockInlineCheck::UseComputedDisplayStyle, editingHost);
        continue;
      }
    }

    if (Text* text = leafContent->GetAsText()) {
      // If there is editable and visible text node, move caret at first of
      // the visible character.
      const WSScanResult scanResultInTextNode =
          WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
              {WSRunScanner::Option::OnlyEditableNodes},
              EditorRawDOMPoint(text, 0));
      if ((scanResultInTextNode.InVisibleOrCollapsibleCharacters() ||
           scanResultInTextNode.ReachedPreformattedLineBreak()) &&
          scanResultInTextNode.TextPtr() == text) {
        nsresult rv = CollapseSelectionTo(
            scanResultInTextNode.PointAtReachedContent<EditorRawDOMPoint>());
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::CollapseSelectionTo() failed");
        return rv;
      }
      // If it's an invisible text node, keep scanning next leaf.
      leafContent = HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
          *leafContent, leafNodeOptions,
          BlockInlineCheck::UseComputedDisplayStyle, editingHost);
      continue;
    }

    // If there is editable <br> or something void element like <img>, <input>,
    // <hr> etc, move caret before it.
    if (!HTMLEditUtils::CanNodeContain(*leafContent, *nsGkAtoms::textTagName) ||
        HTMLEditUtils::IsNeverElementContentsEditableByUser(*leafContent)) {
      MOZ_ASSERT(leafContent->GetParent());
      if (EditorUtils::IsEditableContent(*leafContent, EditorType::HTML)) {
        nsresult rv = CollapseSelectionTo(EditorDOMPoint(leafContent));
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::CollapseSelectionTo() failed");
        return rv;
      }
      MOZ_ASSERT_UNREACHABLE(
          "How do we reach editable leaf in non-editable element?");
      // But if it's not editable, let's put caret at start of editing host
      // for now.
      nsresult rv = CollapseSelectionTo(EditorDOMPoint(editingHost, 0));
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::CollapseSelectionTo() failed");
      return rv;
    }

    // If we meet non-empty block element, we need to scan its child too.
    if (HTMLEditUtils::IsBlockElement(
            *leafContent, BlockInlineCheck::UseComputedDisplayStyle) &&
        !HTMLEditUtils::IsEmptyNode(
            *leafContent,
            {EmptyCheckOption::TreatSingleBRElementAsVisible,
             EmptyCheckOption::TreatNonEditableContentAsInvisible}) &&
        !HTMLEditUtils::IsNeverElementContentsEditableByUser(*leafContent)) {
      leafContent = HTMLEditUtils::GetFirstLeafContent(
          *leafContent, leafNodeOptions,
          BlockInlineCheck::UseComputedDisplayStyle);
      continue;
    }

    // Otherwise, we must meet an empty block element or a data node like
    // comment node.  Let's ignore it.
    leafContent = HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
        *leafContent, leafNodeOptions,
        BlockInlineCheck::UseComputedDisplayStyle, editingHost);
  }

  // If there is no visible/editable node except another block element in
  // current editing host, we should move caret to very first of the editing
  // host.
  // XXX This may not make sense, but Chromium behaves so.  Therefore, the
  //     reason why we do this is just compatibility with Chromium.
  nsresult rv = CollapseSelectionTo(EditorDOMPoint(editingHost, 0));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

void HTMLEditor::PreHandleMouseDown(const MouseEvent& aMouseDownEvent) {
  if (mPendingStylesToApplyToNewContent) {
    // mPendingStylesToApplyToNewContent will be notified of selection change
    // even if aMouseDownEvent is not an acceptable event for this editor.
    // Therefore, we need to notify it of this event too.
    mPendingStylesToApplyToNewContent->PreHandleMouseEvent(aMouseDownEvent);
  }
}

void HTMLEditor::PreHandleMouseUp(const MouseEvent& aMouseUpEvent) {
  if (mPendingStylesToApplyToNewContent) {
    // mPendingStylesToApplyToNewContent will be notified of selection change
    // even if aMouseUpEvent is not an acceptable event for this editor.
    // Therefore, we need to notify it of this event too.
    mPendingStylesToApplyToNewContent->PreHandleMouseEvent(aMouseUpEvent);
  }
}

void HTMLEditor::PreHandleSelectionChangeCommand(Command aCommand) {
  if (mPendingStylesToApplyToNewContent) {
    mPendingStylesToApplyToNewContent->PreHandleSelectionChangeCommand(
        aCommand);
  }
}

void HTMLEditor::PostHandleSelectionChangeCommand(Command aCommand) {
  if (!mPendingStylesToApplyToNewContent) {
    return;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (!editActionData.CanHandle()) {
    return;
  }
  mPendingStylesToApplyToNewContent->PostHandleSelectionChangeCommand(*this,
                                                                      aCommand);
}

nsresult HTMLEditor::HandleKeyPressEvent(WidgetKeyboardEvent* aKeyboardEvent) {
  // NOTE: When you change this method, you should also change:
  //   * editor/libeditor/tests/test_htmleditor_keyevent_handling.html
  if (NS_WARN_IF(!aKeyboardEvent)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (IsReadonly()) {
    HandleKeyPressEventInReadOnlyMode(*aKeyboardEvent);
    return NS_OK;
  }

  MOZ_ASSERT(aKeyboardEvent->mMessage == eKeyPress,
             "HandleKeyPressEvent gets non-keypress event");

  switch (aKeyboardEvent->mKeyCode) {
    case NS_VK_META:
    case NS_VK_WIN:
    case NS_VK_SHIFT:
    case NS_VK_CONTROL:
    case NS_VK_ALT:
      // FYI: This shouldn't occur since modifier key shouldn't cause eKeyPress
      //      event.
      aKeyboardEvent->PreventDefault();
      return NS_OK;

    case NS_VK_BACK:
    case NS_VK_DELETE: {
      nsresult rv = EditorBase::HandleKeyPressEvent(aKeyboardEvent);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::HandleKeyPressEvent() failed");
      return rv;
    }
    case NS_VK_TAB: {
      // Basically, "Tab" key be used only for focus navigation.
      // FYI: In web apps, this is always true.
      if (IsTabbable()) {
        return NS_OK;
      }

      // If we're in the plaintext mode, and not tabbable editor, let's
      // insert a horizontal tabulation.
      if (IsPlaintextMailComposer()) {
        if (aKeyboardEvent->IsShift() || aKeyboardEvent->IsControl() ||
            aKeyboardEvent->IsAlt() || aKeyboardEvent->IsMeta()) {
          return NS_OK;
        }

        // else we insert the tab straight through
        aKeyboardEvent->PreventDefault();
        nsresult rv = OnInputText(u"\t"_ns);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::OnInputText(\\t) failed");
        return rv;
      }

      // Otherwise, e.g., we're an embedding editor in chrome, we can handle
      // "Tab" key as an input.
      if (aKeyboardEvent->IsControl() || aKeyboardEvent->IsAlt() ||
          aKeyboardEvent->IsMeta()) {
        return NS_OK;
      }

      RefPtr<Selection> selection = GetSelection();
      if (NS_WARN_IF(!selection) || NS_WARN_IF(!selection->RangeCount())) {
        return NS_ERROR_FAILURE;
      }

      nsINode* startContainer = selection->GetRangeAt(0)->GetStartContainer();
      MOZ_ASSERT(startContainer);
      if (!startContainer->IsContent()) {
        break;
      }

      const Element* editableBlockElement =
          HTMLEditUtils::GetInclusiveAncestorElement(
              *startContainer->AsContent(),
              HTMLEditUtils::ClosestEditableBlockElement,
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (!editableBlockElement) {
        break;
      }

      // If selection is in a table element, we need special handling.
      if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(
              *editableBlockElement)) {
        Result<EditActionResult, nsresult> result =
            HandleTabKeyPressInTable(aKeyboardEvent);
        if (MOZ_UNLIKELY(result.isErr())) {
          NS_WARNING("HTMLEditor::HandleTabKeyPressInTable() failed");
          return EditorBase::ToGenericNSResult(result.unwrapErr());
        }
        if (!result.inspect().Handled()) {
          return NS_OK;
        }
        nsresult rv = ScrollSelectionFocusIntoView();
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "EditorBase::ScrollSelectionFocusIntoView() failed");
        return EditorBase::ToGenericNSResult(rv);
      }

      // If selection is in an list item element, treat it as indent or outdent.
      if (HTMLEditUtils::IsListItemElement(*editableBlockElement)) {
        aKeyboardEvent->PreventDefault();
        if (!aKeyboardEvent->IsShift()) {
          nsresult rv = IndentAsAction();
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                               "HTMLEditor::IndentAsAction() failed");
          return EditorBase::ToGenericNSResult(rv);
        }
        nsresult rv = OutdentAsAction();
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "HTMLEditor::OutdentAsAction() failed");
        return EditorBase::ToGenericNSResult(rv);
      }

      // If only "Tab" key is pressed in normal context, just treat it as
      // horizontal tab character input.
      if (aKeyboardEvent->IsShift()) {
        return NS_OK;
      }
      aKeyboardEvent->PreventDefault();
      nsresult rv = OnInputText(u"\t"_ns);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::OnInputText(\\t) failed");
      return EditorBase::ToGenericNSResult(rv);
    }
    case NS_VK_RETURN: {
      if (!aKeyboardEvent->IsInputtingLineBreak()) {
        return NS_OK;
      }
      // Anyway consume the event even if we cannot handle it actually because
      // we've already checked whether the an editing host has focus.
      aKeyboardEvent->PreventDefault();
      const RefPtr<Element> editingHost =
          ComputeEditingHost(LimitInBodyElement::No);
      if (NS_WARN_IF(!editingHost)) {
        return NS_ERROR_UNEXPECTED;
      }
      // Shift + Enter should insert a <br> or a LF instead of splitting current
      // paragraph.  Additionally, if we're in plaintext-only mode, we should
      // do so because Chrome does so, but execCommand("insertParagraph") keeps
      // working as contenteditable=true.  So, we cannot redirect in
      // InsertParagraphSeparatorAsAction().
      if (aKeyboardEvent->IsShift() ||
          editingHost->IsContentEditablePlainTextOnly()) {
        // Only inserts a <br> element.
        nsresult rv = InsertLineBreakAsAction();
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "HTMLEditor::InsertLineBreakAsAction() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
      // uses rules to figure out what to insert
      nsresult rv = InsertParagraphSeparatorAsAction();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::InsertParagraphSeparatorAsAction() failed");
      return EditorBase::ToGenericNSResult(rv);
    }
  }

  if (!aKeyboardEvent->IsInputtingText()) {
    // we don't PreventDefault() here or keybindings like control-x won't work
    return NS_OK;
  }
  aKeyboardEvent->PreventDefault();
  // If we dispatch 2 keypress events for a surrogate pair and we set only
  // first `.key` value to the surrogate pair, the preceding one has it and the
  // other has empty string.  In this case, we should handle only the first one
  // with the key value.
  if (!StaticPrefs::dom_event_keypress_dispatch_once_per_surrogate_pair() &&
      !StaticPrefs::dom_event_keypress_key_allow_lone_surrogate() &&
      aKeyboardEvent->mKeyValue.IsEmpty() &&
      IS_SURROGATE(aKeyboardEvent->mCharCode)) {
    return NS_OK;
  }
  nsAutoString str(aKeyboardEvent->mKeyValue);
  if (str.IsEmpty()) {
    str.Assign(static_cast<char16_t>(aKeyboardEvent->mCharCode));
  }
  // FYI: DIfferent from TextEditor, we can treat \r (CR) as-is in HTMLEditor.
  nsresult rv = OnInputText(str);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::OnInputText() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::NodeIsBlock(nsINode* aNode, bool* aIsBlock) {
  if (NS_WARN_IF(!aNode)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (MOZ_UNLIKELY(!aNode->IsElement())) {
    *aIsBlock = false;
    return NS_OK;
  }
  // If the node is in composed doc, we'll refer its style.  If we don't flush
  // pending style here, another API call may change the style.  Therefore,
  // let's flush the pending style changes right now.
  if (aNode->IsInComposedDoc()) {
    if (RefPtr<PresShell> presShell = GetPresShell()) {
      presShell->FlushPendingNotifications(FlushType::Style);
    }
  }
  *aIsBlock = HTMLEditUtils::IsBlockElement(
      *aNode->AsElement(), BlockInlineCheck::UseComputedDisplayOutsideStyle);
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::UpdateBaseURL() {
  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return NS_ERROR_FAILURE;
  }

  // Look for an HTML <base> tag
  RefPtr<ContentList> baseElementList =
      document->GetElementsByTagName(u"base"_ns);

  // If no base tag, then set baseURL to the document's URL.  This is very
  // important, else relative URLs for links and images are wrong
  if (!baseElementList || !baseElementList->Item(0)) {
    document->SetBaseURI(document->GetDocumentURI());
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::InsertLineBreak() {
  // XPCOM method's InsertLineBreak() should insert paragraph separator in
  // HTMLEditor.
  AutoEditActionDataSetter editActionData(
      *this, EditAction::eInsertParagraphSeparator);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  Result<EditActionResult, nsresult> result =
      InsertParagraphSeparatorAsSubAction(*editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::InsertParagraphSeparatorAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

nsresult HTMLEditor::InsertLineBreakAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertLineBreak,
                                          aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (IsSelectionRangeContainerNotContent()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  rv = InsertLineBreakAsSubAction();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertLineBreakAsSubAction() failed");
  // Don't return NS_SUCCESS_DOM_NO_OPERATION for compatibility of `execCommand`
  // result of Chrome.
  return NS_FAILED(rv) ? rv : NS_OK;
}

nsresult HTMLEditor::InsertParagraphSeparatorAsAction(
    nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this, EditAction::eInsertParagraphSeparator, aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  Result<EditActionResult, nsresult> result =
      InsertParagraphSeparatorAsSubAction(*editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::InsertParagraphSeparatorAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

Result<EditActionResult, nsresult> HTMLEditor::HandleTabKeyPressInTable(
    WidgetKeyboardEvent* aKeyboardEvent) {
  MOZ_ASSERT(aKeyboardEvent);

  AutoEditActionDataSetter dummyEditActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!dummyEditActionData.CanHandle())) {
    // Do nothing if we didn't find a table cell.
    return EditActionResult::IgnoredResult();
  }

  // Find enclosing table cell from selection (cell may be selected element)
  const RefPtr<Element> cellElement =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cellElement) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td) "
        "returned nullptr");
    // Do nothing if we didn't find a table cell.
    return EditActionResult::IgnoredResult();
  }

  // find enclosing table
  RefPtr<Element> table =
      HTMLEditUtils::GetClosestAncestorTableElement(*cellElement);
  if (!table) {
    NS_WARNING("HTMLEditor::GetClosestAncestorTableElement() failed");
    return EditActionResult::IgnoredResult();
  }

  // advance to next cell
  // first create an iterator over the table
  PostContentIterator postOrderIter;
  nsresult rv = postOrderIter.Init(table);
  if (NS_FAILED(rv)) {
    NS_WARNING("PostContentIterator::Init() failed");
    return Err(rv);
  }
  // position postOrderIter at block
  rv = postOrderIter.PositionAt(cellElement);
  if (NS_FAILED(rv)) {
    NS_WARNING("PostContentIterator::PositionAt() failed");
    return Err(rv);
  }

  do {
    if (aKeyboardEvent->IsShift()) {
      postOrderIter.Prev();
    } else {
      postOrderIter.Next();
    }

    const RefPtr<Element> element =
        Element::FromNodeOrNull(postOrderIter.GetCurrentNode());
    if (element && HTMLEditUtils::IsTableCellElement(*element) &&
        HTMLEditUtils::GetClosestAncestorTableElement(*element) == table) {
      aKeyboardEvent->PreventDefault();
      CollapseSelectionToDeepestNonTableFirstChild(element);
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      return EditActionResult::HandledResult();
    }
  } while (!postOrderIter.IsDone());

  if (aKeyboardEvent->IsShift()) {
    return EditActionResult::IgnoredResult();
  }

  // If we haven't handled it yet, then we must have run off the end of the
  // table.  Insert a new row.
  // XXX We should investigate whether this behavior is supported by other
  //     browsers later.
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertTableRowElement);
  rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return Err(rv);
  }
  rv = InsertTableRowsWithTransaction(*cellElement, 1,
                                      InsertPosition::eAfterSelectedCell);
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::InsertTableRowsWithTransaction(*cellElement, 1, "
        "InsertPosition::eAfterSelectedCell) failed");
    return Err(rv);
  }
  aKeyboardEvent->PreventDefault();
  // Put selection in right place.  Use table code to get selection and index
  // to new row...
  RefPtr<Element> tblElement, cell;
  int32_t row;
  rv = GetCellContext(getter_AddRefs(tblElement), getter_AddRefs(cell), nullptr,
                      nullptr, &row, nullptr);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return Err(rv);
  }
  if (!tblElement) {
    NS_WARNING("HTMLEditor::GetCellContext() didn't return table element");
    return Err(NS_ERROR_FAILURE);
  }
  // ...so that we can ask for first cell in that row...
  cell = GetTableCellElementAt(*tblElement, row, 0);
  // ...and then set selection there.  (Note that normally you should use
  // CollapseSelectionToDeepestNonTableFirstChild(), but we know cell is an
  // empty new cell, so this works fine)
  if (cell) {
    nsresult rv = CollapseSelectionToStartOf(*cell);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionToStartOf() failed");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
  }
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  return EditActionResult::HandledResult();
}

void HTMLEditor::CollapseSelectionToDeepestNonTableFirstChild(nsINode* aNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_ASSERT(aNode);

  nsCOMPtr<nsINode> node = aNode;

  for (nsIContent* child = node->GetFirstChild(); child;
       child = child->GetFirstChild()) {
    // Stop if we find a table, don't want to go into nested tables
    if (child->IsHTMLElement(nsGkAtoms::table) ||
        !HTMLEditUtils::IsContainerNode(*child)) {
      break;
    }
    node = child;
  }

  DebugOnly<nsresult> rvIgnored = CollapseSelectionToStartOf(*node);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::CollapseSelectionToStartOf() failed, but ignored");
}

NS_IMETHODIMP HTMLEditor::InsertElementAtSelection(Element* aElement,
                                                   bool aDeleteSelection) {
  InsertElementOptions options;
  if (aDeleteSelection) {
    options += InsertElementOption::DeleteSelection;
  }
  nsresult rv = InsertElementAtSelectionAsAction(aElement, options);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertElementAtSelectionAsAction() failed");
  return rv;
}

nsresult HTMLEditor::InsertElementAtSelectionAsAction(
    Element* aElement, const InsertElementOptions aOptions,
    nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsReadonly()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(
      *this, HTMLEditUtils::GetEditActionForInsert(*aElement), aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return EditorBase::ToGenericNSResult(result.unwrapErr());
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  UndefineCaretBidiLevel();

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertElement, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return EditorBase::ToGenericNSResult(NS_ERROR_FAILURE);
  }

  rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(*editingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  if (aOptions.contains(InsertElementOption::DeleteSelection) &&
      !SelectionRef().IsCollapsed()) {
    if (!HTMLEditUtils::IsBlockElement(
            *aElement, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      // E.g., inserting an image.  In this case we don't need to delete any
      // inline wrappers before we do the insertion.  Otherwise we let
      // DeleteSelectionAndPrepareToCreateNode do the deletion for us, which
      // calls DeleteSelection with aStripWrappers = eStrip.
      nsresult rv = DeleteSelectionAsSubAction(
          eNone,
          aOptions.contains(InsertElementOption::SplitAncestorInlineElements)
              ? eStrip
              : eNoStrip);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "EditorBase::DeleteSelectionAsSubAction(eNone, eNoStrip) failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }

    nsresult rv = DeleteSelectionAndPrepareToCreateNode();
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteSelectionAndPrepareToCreateNode() failed");
      return rv;
    }
  }
  // If deleting, selection will be collapsed.
  // so if not, we collapse it
  else {
    // Named Anchor is a special case,
    // We collapse to insert element BEFORE the selection
    // For all other tags, we insert AFTER the selection
    if (HTMLEditUtils::IsNamedAnchorElement(*aElement)) {
      IgnoredErrorResult ignoredError;
      SelectionRef().CollapseToStart(ignoredError);
      if (NS_WARN_IF(Destroyed())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "Selection::CollapseToStart() failed, but ignored");
    } else {
      IgnoredErrorResult ignoredError;
      SelectionRef().CollapseToEnd(ignoredError);
      if (NS_WARN_IF(Destroyed())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "Selection::CollapseToEnd() failed, but ignored");
    }
  }

  if (!SelectionRef().GetAnchorNode()) {
    return NS_OK;
  }
  if (NS_WARN_IF(!SelectionRef().GetAnchorNode()->IsInclusiveDescendantOf(
          editingHost))) {
    return NS_ERROR_FAILURE;
  }

  EditorRawDOMPoint atAnchor(SelectionRef().AnchorRef());
  // Adjust position based on the node we are going to insert.
  EditorDOMPoint pointToInsert =
      HTMLEditUtils::GetBetterInsertionPointFor<EditorDOMPoint>(*aElement,
                                                                atAnchor);
  if (!pointToInsert.IsSet()) {
    NS_WARNING("HTMLEditUtils::GetBetterInsertionPointFor() failed");
    return NS_ERROR_FAILURE;
  }
  Result<EditorDOMPoint, nsresult> pointToInsertOrError =
      WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt(
          *this, pointToInsert,
          {WhiteSpaceVisibilityKeeper::NormalizeOption::
               StopIfFollowingWhiteSpacesStartsWithNBSP});
  if (MOZ_UNLIKELY(pointToInsertOrError.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt() failed");
    return pointToInsertOrError.propagateErr();
  }
  pointToInsert = pointToInsertOrError.unwrap();
  if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (aOptions.contains(InsertElementOption::SplitAncestorInlineElements)) {
    if (const RefPtr<Element> topmostInlineElement = Element::FromNodeOrNull(
            HTMLEditUtils::GetMostDistantAncestorInlineElement(
                *pointToInsert.ContainerAs<nsIContent>(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle,
                editingHost))) {
      Result<SplitNodeResult, nsresult> splitInlinesResult =
          SplitNodeDeepWithTransaction(
              *topmostInlineElement, pointToInsert,
              SplitAtEdges::eDoNotCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitInlinesResult.isErr())) {
        NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
        return splitInlinesResult.unwrapErr();
      }
      splitInlinesResult.inspect().IgnoreCaretPointSuggestion();
      auto splitPoint =
          splitInlinesResult.inspect().AtSplitPoint<EditorDOMPoint>();
      if (MOZ_LIKELY(splitPoint.IsSet())) {
        pointToInsert = std::move(splitPoint);
      }
    }
  }
  {
    Result<CreateElementResult, nsresult> insertElementResult =
        InsertNodeIntoProperAncestorWithTransaction<Element>(
            *aElement, pointToInsert,
            SplitAtEdges::eAllowToCreateEmptyContainer);
    if (MOZ_UNLIKELY(insertElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertNodeIntoProperAncestorWithTransaction("
          "SplitAtEdges::eAllowToCreateEmptyContainer) failed");
      return EditorBase::ToGenericNSResult(insertElementResult.unwrapErr());
    }
    if (MOZ_LIKELY(aElement->IsInComposedDoc())) {
      const auto afterElement = EditorDOMPoint::After(*aElement);
      if (MOZ_LIKELY(afterElement.IsInContentNode())) {
        nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
            afterElement,
            // When user inserting content, the web app may expect that nothing
            // extant content will be deleted. Therefore, we should preserve
            // preformatted linefeed at least.
            PreservePreformattedLineBreak::Yes,
            PaddingForEmptyBlock::Significant, *editingHost);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
          return EditorBase::ToGenericNSResult(rv);
        }
      }
    }
    insertElementResult.inspect().IgnoreCaretPointSuggestion();
  }
  // Set caret after element, but check for special case
  //  of inserting table-related elements: set in first cell instead
  if (!SetCaretInTableCell(aElement)) {
    if (NS_WARN_IF(Destroyed())) {
      return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
    }
    nsresult rv = CollapseSelectionTo(EditorRawDOMPoint::After(*aElement));
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::CollapseSelectionTo() failed");
      return EditorBase::ToGenericNSResult(rv);
    }
  }

  // check for inserting a whole table at the end of a block. If so insert
  // a br after it.
  if (!aElement->IsHTMLElement(nsGkAtoms::table) ||
      !HTMLEditUtils::IsLastChild(
          *aElement, {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    return NS_OK;
  }

  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      EditorDOMPoint::After(*aElement),
                      // Will collapse selection to before the new line break.
                      ePrevious);
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement, ePrevious) failed");
    return EditorBase::ToGenericNSResult(
        insertBRElementResultOrError.unwrapErr());
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  MOZ_ASSERT(insertBRElementResult.Handled());
  rv = insertBRElementResult.SuggestCaretPointTo(*this, {});
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "CaretPoint::SuggestCaretPointTo() failed");
  return EditorBase::ToGenericNSResult(rv);
}

template <typename NodeType>
Result<CreateNodeResultBase<NodeType>, nsresult>
HTMLEditor::InsertNodeIntoProperAncestorWithTransaction(
    NodeType& aContentToInsert, const EditorDOMPoint& aPointToInsert,
    SplitAtEdges aSplitAtEdges) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValidInComposedDoc());
  if (NS_WARN_IF(!aPointToInsert.IsInContentNode())) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  if (aContentToInsert.NodeType() == nsINode::DOCUMENT_TYPE_NODE ||
      aContentToInsert.NodeType() == nsINode::PROCESSING_INSTRUCTION_NODE) {
    return CreateNodeResultBase<NodeType>::NotHandled();
  }

  // Search up the parent chain to find a suitable container.
  EditorDOMPoint pointToInsert(aPointToInsert);
  MOZ_ASSERT(pointToInsert.IsInContentNode());
  while (!HTMLEditUtils::CanNodeContain(*pointToInsert.GetContainer(),
                                        aContentToInsert)) {
    // If the current parent is a root (body or table element)
    // then go no further - we can't insert.
    if (MOZ_UNLIKELY(pointToInsert.IsContainerHTMLElement(nsGkAtoms::body) ||
                     HTMLEditUtils::IsAnyTableElementExceptColumnElement(
                         *pointToInsert.ContainerAs<nsIContent>()))) {
      NS_WARNING(
          "There was no proper container element to insert the content node in "
          "the document");
      return Err(NS_ERROR_FAILURE);
    }

    // Get the next point.
    pointToInsert = pointToInsert.ParentPoint();

    if (MOZ_UNLIKELY(
            !pointToInsert.IsInContentNode() ||
            !EditorUtils::IsEditableContent(
                *pointToInsert.ContainerAs<nsIContent>(), EditorType::HTML))) {
      NS_WARNING(
          "There was no proper container element to insert the content node in "
          "the editing host");
      return Err(NS_ERROR_FAILURE);
    }
  }

  if (pointToInsert != aPointToInsert) {
    // We need to split some levels above the original selection parent.
    MOZ_ASSERT(pointToInsert.GetChild());
    Result<SplitNodeResult, nsresult> splitNodeResult =
        SplitNodeDeepWithTransaction(MOZ_KnownLive(*pointToInsert.GetChild()),
                                     aPointToInsert, aSplitAtEdges);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
      return splitNodeResult.propagateErr();
    }
    pointToInsert =
        splitNodeResult.inspect().template AtSplitPoint<EditorDOMPoint>();
    MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
    // Caret should be set by the caller of this method so that we don't
    // need to handle it here.
    splitNodeResult.inspect().IgnoreCaretPointSuggestion();
  }

  // Now we can insert the new node.
  Result<CreateNodeResultBase<NodeType>, nsresult> insertContentNodeResult =
      InsertNodeWithTransaction<NodeType>(aContentToInsert, pointToInsert);
  if (MOZ_LIKELY(insertContentNodeResult.isOk()) &&
      MOZ_UNLIKELY(NS_WARN_IF(!aContentToInsert.GetParentNode()) ||
                   NS_WARN_IF(aContentToInsert.GetParentNode() !=
                              pointToInsert.GetContainer()))) {
    NS_WARNING(
        "EditorBase::InsertNodeWithTransaction() succeeded, but the inserted "
        "node was moved or removed by the web app");
    insertContentNodeResult.inspect().IgnoreCaretPointSuggestion();
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  NS_WARNING_ASSERTION(insertContentNodeResult.isOk(),
                       "EditorBase::InsertNodeWithTransaction() failed");
  return insertContentNodeResult;
}

NS_IMETHODIMP HTMLEditor::SelectElement(Element* aElement) {
  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = SelectContentInternal(MOZ_KnownLive(*aElement));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SelectContentInternal() failed");
  return rv;
}

nsresult HTMLEditor::SelectContentInternal(nsIContent& aContentToSelect) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Must be sure that element is contained in the editing host
  const RefPtr<Element> editingHost = ComputeEditingHost();
  if (NS_WARN_IF(!editingHost) ||
      NS_WARN_IF(!aContentToSelect.IsInclusiveDescendantOf(editingHost))) {
    return NS_ERROR_FAILURE;
  }

  EditorRawDOMPoint newSelectionStart(&aContentToSelect);
  if (NS_WARN_IF(!newSelectionStart.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  EditorRawDOMPoint newSelectionEnd(EditorRawDOMPoint::After(aContentToSelect));
  MOZ_ASSERT(newSelectionEnd.IsSet());
  ErrorResult error;
  SelectionRef().SetStartAndEndInLimiter(newSelectionStart, newSelectionEnd,
                                         error);
  NS_WARNING_ASSERTION(!error.Failed(),
                       "Selection::SetStartAndEndInLimiter() failed");
  return error.StealNSResult();
}

nsresult HTMLEditor::AppendContentToSelectionAsRange(nsIContent& aContent) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  EditorRawDOMPoint atContent(&aContent);
  if (NS_WARN_IF(!atContent.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsRange> range = nsRange::Create(
      atContent.ToRawRangeBoundary(),
      atContent.NextPoint().ToRawRangeBoundary(), IgnoreErrors());
  if (NS_WARN_IF(!range)) {
    NS_WARNING("nsRange::Create() failed");
    return NS_ERROR_FAILURE;
  }

  ErrorResult error;
  SelectionRef().AddRangeAndSelectFramesAndNotifyListeners(*range, error);
  if (NS_WARN_IF(Destroyed())) {
    if (error.Failed()) {
      error.SuppressException();
    }
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(!error.Failed(), "Failed to add range to Selection");
  return error.StealNSResult();
}

nsresult HTMLEditor::ClearSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  ErrorResult error;
  SelectionRef().RemoveAllRanges(error);
  if (NS_WARN_IF(Destroyed())) {
    if (error.Failed()) {
      error.SuppressException();
    }
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(!error.Failed(), "Selection::RemoveAllRanges() failed");
  return error.StealNSResult();
}

nsresult HTMLEditor::FormatBlockAsAction(const nsAString& aParagraphFormat,
                                         nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(aParagraphFormat.IsEmpty())) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(
      *this, EditAction::eInsertBlockElement, aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<nsAtom> tagName = NS_Atomize(aParagraphFormat);
  MOZ_ASSERT(tagName);
  if (NS_WARN_IF(!tagName->IsStatic()) ||
      NS_WARN_IF(!HTMLEditUtils::IsFormatTagForFormatBlockCommand(
          *tagName->AsStatic()))) {
    return NS_ERROR_INVALID_ARG;
  }

  if (tagName == nsGkAtoms::dd || tagName == nsGkAtoms::dt) {
    // MOZ_KnownLive(tagName->AsStatic()) because nsStaticAtom instances live
    // while the process is running.
    Result<EditActionResult, nsresult> result =
        MakeOrChangeListAndListItemAsSubAction(
            MOZ_KnownLive(*tagName->AsStatic()), EmptyString(),
            SelectAllOfCurrentList::No, *editingHost);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "HTMLEditor::MakeOrChangeListAndListItemAsSubAction("
          "SelectAllOfCurrentList::No) failed");
      return EditorBase::ToGenericNSResult(result.unwrapErr());
    }
    return NS_OK;
  }

  rv = FormatBlockContainerAsSubAction(MOZ_KnownLive(*tagName->AsStatic()),
                                       FormatBlockMode::HTMLFormatBlockCommand,
                                       *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::FormatBlockContainerAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::SetParagraphStateAsAction(
    const nsAString& aParagraphFormat, nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this, EditAction::eInsertBlockElement, aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // TODO: Computing the editing host here makes the `execCommand` in
  //       docshell/base/crashtests/file_432114-2.xhtml cannot run
  //       `DOMNodeRemoved` event listener with deleting the bogus <br> element.
  //       So that it should be rewritten with different mutation event listener
  //       since we'd like to stop using it.

  nsAutoString lowerCaseTagName(aParagraphFormat);
  ToLowerCase(lowerCaseTagName);
  RefPtr<nsAtom> tagName = NS_Atomize(lowerCaseTagName);
  MOZ_ASSERT(tagName);
  if (NS_WARN_IF(!tagName->IsStatic())) {
    return NS_ERROR_INVALID_ARG;
  }
  if (tagName == nsGkAtoms::dd || tagName == nsGkAtoms::dt) {
    // MOZ_KnownLive(tagName->AsStatic()) because nsStaticAtom instances live
    // while the process is running.
    Result<EditActionResult, nsresult> result =
        MakeOrChangeListAndListItemAsSubAction(
            MOZ_KnownLive(*tagName->AsStatic()), EmptyString(),
            SelectAllOfCurrentList::No, *editingHost);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "HTMLEditor::MakeOrChangeListAndListItemAsSubAction("
          "SelectAllOfCurrentList::No) failed");
      return EditorBase::ToGenericNSResult(result.unwrapErr());
    }
    return NS_OK;
  }

  rv = FormatBlockContainerAsSubAction(
      MOZ_KnownLive(*tagName->AsStatic()),
      FormatBlockMode::XULParagraphStateCommand, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::FormatBlockContainerAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

// static
bool HTMLEditor::IsFormatElement(FormatBlockMode aFormatBlockMode,
                                 const nsIContent& aContent) {
  // FYI: Optimize for HTML command because it may run too many times.
  return MOZ_LIKELY(aFormatBlockMode == FormatBlockMode::HTMLFormatBlockCommand)
             ? HTMLEditUtils::IsFormatElementForFormatBlockCommand(aContent)
             : (HTMLEditUtils::IsFormatElementForParagraphStateCommand(
                    aContent) &&
                // XXX The XUL paragraph state command treats <dl>, <dd> and
                // <dt> elements but all handlers do not treat them as a format
                // node.  Therefore, we keep the traditional behavior here.
                !aContent.IsAnyOfHTMLElements(nsGkAtoms::dd, nsGkAtoms::dl,
                                              nsGkAtoms::dt));
}

NS_IMETHODIMP HTMLEditor::GetParagraphState(bool* aMixed,
                                            nsAString& aFirstParagraphState) {
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mInitSucceeded) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ErrorResult error;
  ParagraphStateAtSelection paragraphState(
      *this, FormatBlockMode::XULParagraphStateCommand, error);
  if (error.Failed()) {
    NS_WARNING("ParagraphStateAtSelection failed");
    return error.StealNSResult();
  }

  *aMixed = paragraphState.IsMixed();
  if (NS_WARN_IF(!paragraphState.GetFirstParagraphStateAtSelection())) {
    // XXX Odd result, but keep this behavior for now...
    aFirstParagraphState.AssignASCII("x");
  } else {
    paragraphState.GetFirstParagraphStateAtSelection()->ToString(
        aFirstParagraphState);
  }
  return NS_OK;
}

nsresult HTMLEditor::GetBackgroundColorState(bool* aMixed,
                                             nsAString& aOutColor) {
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (IsCSSEnabled()) {
    // if we are in CSS mode, we have to check if the containing block defines
    // a background color
    nsresult rv = GetCSSBackgroundColorState(
        aMixed, aOutColor,
        {RetrievingBackgroundColorOption::OnlyBlockBackgroundColor,
         RetrievingBackgroundColorOption::
             DefaultColorIfNoSpecificBackgroundColor});
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::GetCSSBackgroundColorState() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  // in HTML mode, we look only at page's background
  nsresult rv = GetHTMLBackgroundColorState(aMixed, aOutColor);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetCSSBackgroundColorState() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::GetHighlightColorState(bool* aMixed,
                                                 nsAString& aOutColor) {
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aMixed = false;
  aOutColor.AssignLiteral("transparent");
  if (!IsCSSEnabled() && IsMailEditor()) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // in CSS mode, text background can be added by the Text Highlight button
  // we need to query the background of the selection without looking for
  // the block container of the ranges in the selection
  RetrievingBackgroundColorOptions options;
  if (IsMailEditor()) {
    options += RetrievingBackgroundColorOption::StopAtInclusiveAncestorBlock;
  }
  nsresult rv = GetCSSBackgroundColorState(aMixed, aOutColor, options);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetCSSBackgroundColorState() failed");
  return rv;
}

nsresult HTMLEditor::GetCSSBackgroundColorState(
    bool* aMixed, nsAString& aOutColor,
    RetrievingBackgroundColorOptions aOptions) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aMixed = false;
  // the default background color is transparent
  aOutColor.AssignLiteral("transparent");

  RefPtr<const nsRange> firstRange = SelectionRef().GetRangeAt(0);
  if (NS_WARN_IF(!firstRange)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsINode> startContainer = firstRange->GetStartContainer();
  if (NS_WARN_IF(!startContainer) || NS_WARN_IF(!startContainer->IsContent())) {
    return NS_ERROR_FAILURE;
  }

  // is the selection collapsed?
  nsIContent* contentToExamine;
  if (SelectionRef().IsCollapsed() || startContainer->IsText()) {
    if (NS_WARN_IF(!startContainer->IsContent())) {
      return NS_ERROR_FAILURE;
    }
    // we want to look at the startContainer and ancestors
    contentToExamine = startContainer->AsContent();
  } else {
    // otherwise we want to look at the first editable node after
    // {startContainer,offset} and its ancestors for divs with alignment on them
    contentToExamine = firstRange->GetChildAtStartOffset();
    // GetNextNode(startContainer, offset, true, address_of(contentToExamine));
  }

  if (NS_WARN_IF(!contentToExamine)) {
    return NS_ERROR_FAILURE;
  }

  if (aOptions.contains(
          RetrievingBackgroundColorOption::OnlyBlockBackgroundColor)) {
    // we are querying the block background (and not the text background), let's
    // climb to the block container.  Note that background color of ancestor
    // of editing host may be what the caller wants to know.  Therefore, we
    // should ignore the editing host boundaries.
    Element* const closestBlockElement =
        HTMLEditUtils::GetInclusiveAncestorElement(
            *contentToExamine, HTMLEditUtils::ClosestBlockElement,
            BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (NS_WARN_IF(!closestBlockElement)) {
      return NS_OK;
    }

    for (RefPtr<Element> blockElement = closestBlockElement; blockElement;) {
      RefPtr<Element> nextBlockElement = HTMLEditUtils::GetAncestorElement(
          *blockElement, HTMLEditUtils::ClosestBlockElement,
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
      DebugOnly<nsresult> rvIgnored = CSSEditUtils::GetComputedProperty(
          *blockElement, *nsGkAtoms::background_color, aOutColor);
      if (NS_WARN_IF(Destroyed())) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      if (MaybeNodeRemovalsObservedByDevTools() &&
          NS_WARN_IF(nextBlockElement !=
                     HTMLEditUtils::GetAncestorElement(
                         *blockElement, HTMLEditUtils::ClosestBlockElement,
                         BlockInlineCheck::UseComputedDisplayOutsideStyle))) {
        return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
      }
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "CSSEditUtils::GetComputedProperty(nsGkAtoms::"
                           "background_color) failed, but ignored");
      // look at parent if the queried color is transparent and if the node to
      // examine is not the root of the document
      if (!HTMLEditUtils::IsTransparentCSSColor(aOutColor)) {
        return NS_OK;
      }
      if (aOptions.contains(
              RetrievingBackgroundColorOption::StopAtInclusiveAncestorBlock)) {
        aOutColor.AssignLiteral("transparent");
        return NS_OK;
      }
      blockElement = std::move(nextBlockElement);
    }

    if (aOptions.contains(RetrievingBackgroundColorOption::
                              DefaultColorIfNoSpecificBackgroundColor) &&
        HTMLEditUtils::IsTransparentCSSColor(aOutColor)) {
      CSSEditUtils::GetDefaultBackgroundColor(aOutColor);
    }
    return NS_OK;
  }

  // no, we are querying the text background for the Text Highlight button
  if (contentToExamine->IsText()) {
    // if the node of interest is a text node, let's climb a level
    contentToExamine = contentToExamine->GetParent();
  }
  // Return default value due to no parent node
  if (!contentToExamine) {
    return NS_OK;
  }

  for (RefPtr<Element> element :
       contentToExamine->InclusiveAncestorsOfType<Element>()) {
    // is the node to examine a block ?
    if (aOptions.contains(
            RetrievingBackgroundColorOption::StopAtInclusiveAncestorBlock) &&
        HTMLEditUtils::IsBlockElement(
            *element, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      // yes it is a block; in that case, the text background color is
      // transparent
      aOutColor.AssignLiteral("transparent");
      break;
    }

    // no, it's not; let's retrieve the computed style of background-color
    // for the node to examine
    nsCOMPtr<nsINode> parentNode = element->GetParentNode();
    DebugOnly<nsresult> rvIgnored = CSSEditUtils::GetComputedProperty(
        *element, *nsGkAtoms::background_color, aOutColor);
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_WARN_IF(parentNode != element->GetParentNode())) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "CSSEditUtils::GetComputedProperty(nsGkAtoms::"
                         "background_color) failed, but ignored");
    if (!HTMLEditUtils::IsTransparentCSSColor(aOutColor)) {
      HTMLEditUtils::GetNormalizedCSSColorValue(
          aOutColor, HTMLEditUtils::ZeroAlphaColor::RGBAValue, aOutColor);
      return NS_OK;
    }
  }
  if (aOptions.contains(RetrievingBackgroundColorOption::
                            DefaultColorIfNoSpecificBackgroundColor) &&
      HTMLEditUtils::IsTransparentCSSColor(aOutColor)) {
    CSSEditUtils::GetDefaultBackgroundColor(aOutColor);
  }
  return NS_OK;
}

nsresult HTMLEditor::GetHTMLBackgroundColorState(bool* aMixed,
                                                 nsAString& aOutColor) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // TODO: We don't handle "mixed" correctly!
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aMixed = false;
  aOutColor.Truncate();

  Result<RefPtr<Element>, nsresult> cellOrRowOrTableElementOrError =
      GetSelectedOrParentTableElement();
  if (cellOrRowOrTableElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetSelectedOrParentTableElement() returned error");
    return cellOrRowOrTableElementOrError.unwrapErr();
  }

  for (RefPtr<Element> element = cellOrRowOrTableElementOrError.unwrap();
       element; element = element->GetParentElement()) {
    // We are in a cell or selected table
    element->GetAttr(nsGkAtoms::bgcolor, aOutColor);

    // Done if we have a color explicitly set
    if (!aOutColor.IsEmpty()) {
      return NS_OK;
    }

    // Once we hit the body, we're done
    if (element->IsHTMLElement(nsGkAtoms::body)) {
      return NS_OK;
    }

    // No color is set, but we need to report visible color inherited
    // from nested cells/tables, so search up parent chain so that
    // let's keep checking the ancestors.
  }

  // If no table or cell found, get page body
  Element* rootElement = GetRoot();
  if (NS_WARN_IF(!rootElement)) {
    return NS_ERROR_FAILURE;
  }

  rootElement->GetAttr(nsGkAtoms::bgcolor, aOutColor);
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetListState(bool* aMixed, bool* aOL, bool* aUL,
                                       bool* aDL) {
  if (NS_WARN_IF(!aMixed) || NS_WARN_IF(!aOL) || NS_WARN_IF(!aUL) ||
      NS_WARN_IF(!aDL)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mInitSucceeded) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ErrorResult error;
  ListElementSelectionState state(*this, error);
  if (error.Failed()) {
    NS_WARNING("ListElementSelectionState failed");
    return error.StealNSResult();
  }

  *aMixed = state.IsNotOneTypeListElementSelected();
  *aOL = state.IsOLElementSelected();
  *aUL = state.IsULElementSelected();
  *aDL = state.IsDLElementSelected();
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetListItemState(bool* aMixed, bool* aLI, bool* aDT,
                                           bool* aDD) {
  if (NS_WARN_IF(!aMixed) || NS_WARN_IF(!aLI) || NS_WARN_IF(!aDT) ||
      NS_WARN_IF(!aDD)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mInitSucceeded) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ErrorResult error;
  ListItemElementSelectionState state(*this, error);
  if (error.Failed()) {
    NS_WARNING("ListItemElementSelectionState failed");
    return error.StealNSResult();
  }

  // XXX Why do we ignore `<li>` element selected state?
  *aMixed = state.IsNotOneTypeDefinitionListItemElementSelected();
  *aLI = state.IsLIElementSelected();
  *aDT = state.IsDTElementSelected();
  *aDD = state.IsDDElementSelected();
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetAlignment(bool* aMixed,
                                       nsIHTMLEditor::EAlignment* aAlign) {
  if (NS_WARN_IF(!aMixed) || NS_WARN_IF(!aAlign)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mInitSucceeded) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ErrorResult error;
  AlignStateAtSelection state(*this, error);
  if (error.Failed()) {
    NS_WARNING("AlignStateAtSelection failed");
    return error.StealNSResult();
  }

  *aMixed = false;
  *aAlign = state.AlignmentAtSelectionStart();
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::MakeOrChangeList(const nsAString& aListType,
                                           bool aEntireList,
                                           const nsAString& aBulletType) {
  RefPtr<nsAtom> listTagName = NS_Atomize(aListType);
  if (NS_WARN_IF(!listTagName) || NS_WARN_IF(!listTagName->IsStatic())) {
    return NS_ERROR_INVALID_ARG;
  }
  // MOZ_KnownLive(listTagName->AsStatic()) because nsStaticAtom instances live
  // while the process is running.
  nsresult rv = MakeOrChangeListAsAction(
      MOZ_KnownLive(*listTagName->AsStatic()), aBulletType,
      aEntireList ? SelectAllOfCurrentList::Yes : SelectAllOfCurrentList::No);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::MakeOrChangeListAsAction() failed");
  return rv;
}

nsresult HTMLEditor::MakeOrChangeListAsAction(
    const nsStaticAtom& aListElementTagName, const nsAString& aBulletType,
    SelectAllOfCurrentList aSelectAllOfCurrentList, nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  AutoEditActionDataSetter editActionData(
      *this, HTMLEditUtils::GetEditActionForInsert(aListElementTagName),
      aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<EditActionResult, nsresult> result =
      MakeOrChangeListAndListItemAsSubAction(aListElementTagName, aBulletType,
                                             aSelectAllOfCurrentList,
                                             *editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::MakeOrChangeListAndListItemAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::RemoveList(const nsAString& aListType) {
  nsresult rv = RemoveListAsAction(aListType);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::RemoveListAsAction() failed");
  return rv;
}

nsresult HTMLEditor::RemoveListAsAction(const nsAString& aListType,
                                        nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // Note that we ignore aListType when we actually remove parent list elements.
  // However, we need to set InputEvent.inputType to "insertOrderedList" or
  // "insertedUnorderedList" when this is called for
  // execCommand("insertorderedlist") or execCommand("insertunorderedlist").
  // Otherwise, comm-central UI may call this methods with "dl" or "".
  // So, it's okay to use mismatched EditAction here if this is called in
  // comm-central.

  RefPtr<nsAtom> listAtom = NS_Atomize(aListType);
  if (NS_WARN_IF(!listAtom)) {
    return NS_ERROR_INVALID_ARG;
  }
  AutoEditActionDataSetter editActionData(
      *this, HTMLEditUtils::GetEditActionForRemoveList(*listAtom), aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  const RefPtr<Element> editingHost = ComputeEditingHost();
  if (!editingHost) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  rv = RemoveListAtSelectionAsSubAction(*editingHost);
  NS_WARNING_ASSERTION(NS_FAILED(rv),
                       "HTMLEditor::RemoveListAtSelectionAsSubAction() failed");
  return rv;
}

nsresult HTMLEditor::FormatBlockContainerAsSubAction(
    const nsStaticAtom& aTagName, FormatBlockMode aFormatBlockMode,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  MOZ_ASSERT(&aTagName != nsGkAtoms::dd && &aTagName != nsGkAtoms::dt);

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eCreateOrRemoveBlock, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  if (IsSelectionRangeContainerNotContent()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  // FormatBlockContainerWithTransaction() creates AutoSelectionRestorer.
  // Therefore, even if it returns NS_OK, editor might have been destroyed
  // at restoring Selection.
  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  Result<RefPtr<Element>, nsresult> suggestBlockElementToPutCaretOrError =
      FormatBlockContainerWithTransaction(selectionRanges, aTagName,
                                          aFormatBlockMode, aEditingHost);
  if (suggestBlockElementToPutCaretOrError.isErr()) {
    NS_WARNING("HTMLEditor::FormatBlockContainerWithTransaction() failed");
    return suggestBlockElementToPutCaretOrError.unwrapErr();
  }

  if (selectionRanges.HasSavedRanges()) {
    selectionRanges.RestoreFromSavedRanges();
  }

  if (selectionRanges.IsCollapsed()) {
    // FIXME: If we get rid of the legacy mutation events, we should be able to
    // just insert a line break without empty check.
    Result<CreateLineBreakResult, nsresult>
        insertPaddingBRElementResultOrError =
            InsertPaddingBRElementIfInEmptyBlock(
                selectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>(),
                eNoStrip);
    if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(eNoStrip) failed");
      return insertPaddingBRElementResultOrError.unwrapErr();
    }
    EditorDOMPoint pointToPutCaret;
    insertPaddingBRElementResultOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (pointToPutCaret.IsSet()) {
      nsresult rv = selectionRanges.Collapse(pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoClonedRangeArray::Collapse() failed");
        return rv;
      }
    }
  }

  if (!suggestBlockElementToPutCaretOrError.inspect() ||
      !selectionRanges.IsCollapsed()) {
    nsresult rv = selectionRanges.ApplyTo(SelectionRef());
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoClonedSelectionRangeArray::ApplyTo() failed, but ignored");
    return rv;
  }

  const auto firstSelectionStartPoint =
      selectionRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
  if (NS_WARN_IF(!firstSelectionStartPoint.IsSetAndValidInComposedDoc())) {
    return NS_ERROR_FAILURE;
  }
  Result<EditorRawDOMPoint, nsresult> pointInBlockElementOrError =
      HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
          EditorRawDOMPoint>(*suggestBlockElementToPutCaretOrError.inspect(),
                             firstSelectionStartPoint);
  NS_WARNING_ASSERTION(
      pointInBlockElementOrError.isOk(),
      "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() failed, but "
      "ignored");
  // Note that if the point is unset, it means that firstSelectionStartPoint is
  // in the block element.
  if (MOZ_LIKELY(pointInBlockElementOrError.isOk()) &&
      pointInBlockElementOrError.inspect().IsSet()) {
    nsresult rv =
        selectionRanges.Collapse(pointInBlockElementOrError.inspect());
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return rv;
    }
  }

  rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "AutoClonedSelectionRangeArray::ApplyTo() failed, but ignored");
  return rv;
}

nsresult HTMLEditor::IndentAsAction(nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eIndent,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<EditActionResult, nsresult> result = IndentAsSubAction(*editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::IndentAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

nsresult HTMLEditor::OutdentAsAction(nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eOutdent,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<EditActionResult, nsresult> result = OutdentAsSubAction(*editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::OutdentAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

// TODO: IMPLEMENT ALIGNMENT!

nsresult HTMLEditor::AlignAsAction(const nsAString& aAlignType,
                                   nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this, HTMLEditUtils::GetEditActionForAlignment(aAlignType), aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<EditActionResult, nsresult> result =
      AlignAsSubAction(aAlignType, *editingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::AlignAsSubAction() failed");
    return EditorBase::ToGenericNSResult(result.unwrapErr());
  }
  return NS_OK;
}

Element* HTMLEditor::GetInclusiveAncestorByTagName(const nsStaticAtom& aTagName,
                                                   nsIContent& aContent) const {
  MOZ_ASSERT(&aTagName != nsGkAtoms::_empty);

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return nullptr;
  }

  return GetInclusiveAncestorByTagNameInternal(aTagName, aContent);
}

Element* HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(
    const nsStaticAtom& aTagName) const {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(&aTagName != nsGkAtoms::_empty);

  // If no node supplied, get it from anchor node of current selection
  const EditorRawDOMPoint atAnchor(SelectionRef().AnchorRef());
  if (NS_WARN_IF(!atAnchor.IsInContentNode())) {
    return nullptr;
  }

  // Try to get the actual selected node
  nsIContent* content = nullptr;
  if (atAnchor.GetContainer()->HasChildNodes() &&
      atAnchor.ContainerAs<nsIContent>()) {
    content = atAnchor.GetChild();
  }
  // Anchor node is probably a text node - just use that
  if (!content) {
    content = atAnchor.ContainerAs<nsIContent>();
    if (NS_WARN_IF(!content)) {
      return nullptr;
    }
  }
  return GetInclusiveAncestorByTagNameInternal(aTagName, *content);
}

Element* HTMLEditor::GetInclusiveAncestorByTagNameInternal(
    const nsStaticAtom& aTagName, const nsIContent& aContent) const {
  MOZ_ASSERT(&aTagName != nsGkAtoms::_empty);

  Element* currentElement = aContent.GetAsElementOrParentElement();
  if (NS_WARN_IF(!currentElement)) {
    MOZ_ASSERT(!aContent.GetParentNode());
    return nullptr;
  }

  bool lookForLink = IsLinkTag(aTagName);
  bool lookForNamedAnchor = IsNamedAnchorTag(aTagName);
  for (Element* const element :
       currentElement->InclusiveAncestorsOfType<Element>()) {
    // Stop searching if parent is a body element.  Note: Originally used
    // IsRoot() to/ stop at table cells, but that's too messy when you are
    // trying to find the parent table.
    if (element->IsHTMLElement(nsGkAtoms::body)) {
      return nullptr;
    }
    if (lookForLink) {
      // Test if we have a link (an anchor with href set)
      if (HTMLEditUtils::IsHyperlinkElement(*element)) {
        return element;
      }
    } else if (lookForNamedAnchor) {
      // Test if we have a named anchor (an anchor with name set)
      if (HTMLEditUtils::IsNamedAnchorElement(*element)) {
        return element;
      }
    } else if (&aTagName == nsGkAtoms::list) {
      // Match "ol", "ul", or "dl" for lists
      if (HTMLEditUtils::IsListElement(*element)) {
        return element;
      }
    } else if (&aTagName == nsGkAtoms::td) {
      // Table cells are another special case: match either "td" or "th"
      if (HTMLEditUtils::IsTableCellElement(*element)) {
        return element;
      }
    } else if (&aTagName == element->NodeInfo()->NameAtom()) {
      return element;
    }
  }
  return nullptr;
}

NS_IMETHODIMP HTMLEditor::GetElementOrParentByTagName(const nsAString& aTagName,
                                                      nsINode* aNode,
                                                      Element** aReturn) {
  if (NS_WARN_IF(aTagName.IsEmpty()) || NS_WARN_IF(!aReturn)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsStaticAtom* tagName = EditorUtils::GetTagNameAtom(aTagName);
  if (NS_WARN_IF(!tagName)) {
    // We don't need to support custom elements since this is an internal API.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  if (NS_WARN_IF(tagName == nsGkAtoms::_empty)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aNode) {
    AutoEditActionDataSetter dummyEditAction(*this, EditAction::eNotEditing);
    if (NS_WARN_IF(!dummyEditAction.CanHandle())) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    RefPtr<Element> parentElement =
        GetInclusiveAncestorByTagNameAtSelection(*tagName);
    if (!parentElement) {
      return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
    }
    parentElement.forget(aReturn);
    return NS_OK;
  }

  if (!aNode->IsContent() || !aNode->GetAsElementOrParentElement()) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> parentElement =
      GetInclusiveAncestorByTagName(*tagName, *aNode->AsContent());
  if (!parentElement) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  parentElement.forget(aReturn);
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetSelectedElement(const nsAString& aTagName,
                                             nsISupports** aReturn) {
  if (NS_WARN_IF(!aReturn)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aReturn = nullptr;

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ErrorResult error;
  nsStaticAtom* tagName = EditorUtils::GetTagNameAtom(aTagName);
  if (!aTagName.IsEmpty() && !tagName) {
    // We don't need to support custom elements becaus of internal API.
    return NS_OK;
  }
  RefPtr<nsINode> selectedNode = GetSelectedElement(tagName, error);
  NS_WARNING_ASSERTION(!error.Failed(),
                       "HTMLEditor::GetSelectedElement() failed");
  selectedNode.forget(aReturn);
  return error.StealNSResult();
}

already_AddRefed<Element> HTMLEditor::GetSelectedElement(const nsAtom* aTagName,
                                                         ErrorResult& aRv) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_ASSERT(!aRv.Failed());

  // If there is no Selection or two or more selection ranges, that means that
  // not only one element is selected so that return nullptr.
  if (SelectionRef().RangeCount() != 1) {
    return nullptr;
  }

  bool isLinkTag = aTagName && IsLinkTag(*aTagName);
  bool isNamedAnchorTag = aTagName && IsNamedAnchorTag(*aTagName);

  RefPtr<nsRange> firstRange = SelectionRef().GetRangeAt(0);
  MOZ_ASSERT(firstRange);

  const RangeBoundary& startRef = firstRange->StartRef();
  if (NS_WARN_IF(!startRef.IsSet())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  const RangeBoundary& endRef = firstRange->EndRef();
  if (NS_WARN_IF(!endRef.IsSet())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Optimization for a single selected element
  if (startRef.GetContainer() == endRef.GetContainer()) {
    nsIContent* const startContent = startRef.GetChildAtOffset();
    nsIContent* const endContent = endRef.GetChildAtOffset();
    if (startContent && endContent &&
        startContent->GetNextSibling() == endContent) {
      if (!aTagName) {
        if (!startContent->IsElement()) {
          // This means only a text node or something is selected.  We should
          // return nullptr in this case since no other elements are selected.
          return nullptr;
        }
        return do_AddRef(startContent->AsElement());
      }
      // Test for appropriate node type requested
      if (aTagName == startContent->NodeInfo()->NameAtom() ||
          (isLinkTag && HTMLEditUtils::IsHyperlinkElement(*startContent)) ||
          (isNamedAnchorTag &&
           HTMLEditUtils::IsNamedAnchorElement(*startContent))) {
        MOZ_ASSERT(startContent->IsElement());
        return do_AddRef(startContent->AsElement());
      }
    }
  }

  if (isLinkTag && startRef.GetContainer()->IsContent() &&
      endRef.GetContainer()->IsContent()) {
    // Link node must be the same for both ends of selection.
    Element* parentLinkOfStart = GetInclusiveAncestorByTagNameInternal(
        *nsGkAtoms::href, *startRef.GetContainer()->AsContent());
    if (parentLinkOfStart) {
      if (SelectionRef().IsCollapsed()) {
        // We have just a caret in the link.
        return do_AddRef(parentLinkOfStart);
      }
      // Link node must be the same for both ends of selection.
      Element* parentLinkOfEnd = GetInclusiveAncestorByTagNameInternal(
          *nsGkAtoms::href, *endRef.GetContainer()->AsContent());
      if (parentLinkOfStart == parentLinkOfEnd) {
        return do_AddRef(parentLinkOfStart);
      }
    }
  }

  if (SelectionRef().IsCollapsed()) {
    return nullptr;
  }

  PostContentIterator postOrderIter;
  nsresult rv = postOrderIter.Init(firstRange);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return nullptr;
  }

  RefPtr<Element> lastElementInRange;
  for (nsINode* lastNodeInRange = nullptr; !postOrderIter.IsDone();
       postOrderIter.Next()) {
    if (lastElementInRange) {
      // When any node follows an element node, not only one element is
      // selected so that return nullptr.
      return nullptr;
    }

    // This loop ignors any non-element nodes before first element node.
    // Its purpose must be that this method treats this case as selecting
    // the <b> element:
    // - <p>abc <b>d[ef</b>}</p>
    // because children of an element node is listed up before the element.
    // However, this case must not be expected by the initial developer:
    // - <p>a[bc <b>def</b>}</p>
    // When we meet non-parent and non-next-sibling node of previous node,
    // it means that the range across element boundary (open tag in HTML
    // source).  So, in this case, we should not say only the following
    // element is selected.
    nsINode* currentNode = postOrderIter.GetCurrentNode();
    MOZ_ASSERT(currentNode);
    if (lastNodeInRange && lastNodeInRange->GetParentNode() != currentNode &&
        lastNodeInRange->GetNextSibling() != currentNode) {
      return nullptr;
    }

    lastNodeInRange = currentNode;

    lastElementInRange = Element::FromNodeOrNull(lastNodeInRange);
    if (!lastElementInRange) {
      continue;
    }

    // And also, if it's followed by a <br> element, we shouldn't treat the
    // the element is selected like this case:
    // - <p><b>[def</b>}<br></p>
    // Note that we don't need special handling for <a href> because double
    // clicking it selects the element and we use the first path to handle it.
    // Additionally, we have this case too:
    // - <p><b>[def</b><b>}<br></b></p>
    // In these cases, the <br> element is not listed up by PostContentIterator.
    // So, we should return nullptr if next sibling is a `<br>` element or
    // next sibling starts with `<br>` element.
    if (nsIContent* nextSibling = lastElementInRange->GetNextSibling()) {
      if (nextSibling->IsHTMLElement(nsGkAtoms::br)) {
        return nullptr;
      }
      nsIContent* firstEditableLeaf = HTMLEditUtils::GetFirstLeafContent(
          *nextSibling,
          // XXX Should we ignore invisible inline elements and text nodes?
          {});
      if (firstEditableLeaf &&
          firstEditableLeaf->IsHTMLElement(nsGkAtoms::br)) {
        return nullptr;
      }
    }

    if (!aTagName) {
      continue;
    }

    if (isLinkTag && HTMLEditUtils::IsHyperlinkElement(*lastElementInRange)) {
      continue;
    }

    if (isNamedAnchorTag &&
        HTMLEditUtils::IsNamedAnchorElement(*lastElementInRange)) {
      continue;
    }

    if (aTagName == lastElementInRange->NodeInfo()->NameAtom()) {
      continue;
    }

    // First element in the range does not match what the caller is looking
    // for.
    return nullptr;
  }
  return lastElementInRange.forget();
}

Result<CreateElementResult, nsresult> HTMLEditor::CreateAndInsertElement(
    WithTransaction aWithTransaction, const nsAtom& aTagName,
    const EditorDOMPoint& aPointToInsert,
    const InitializeInsertingElement& aInitializer) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  // XXX We need offset at new node for RangeUpdaterRef().  Therefore, we need
  //     to compute the offset now but this is expensive.  So, if it's possible,
  //     we need to redesign RangeUpdaterRef() as avoiding using indices.
  (void)aPointToInsert.Offset();

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eCreateNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // TODO: This method should have a callback function which is called
  //       immediately after creating an element but before it's inserted into
  //       the DOM tree.  Then, caller can init the new element's attributes
  //       and children **without** transactions (it'll reduce the number of
  //       legacy mutation events).  Finally, we can get rid of
  //       CreatElementTransaction since we can use InsertNodeTransaction
  //       instead.

  auto createNewElementResult =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<CreateElementResult, nsresult> {
    RefPtr<Element> newElement = CreateHTMLContent(&aTagName);
    if (MOZ_UNLIKELY(!newElement)) {
      NS_WARNING("EditorBase::CreateHTMLContent() failed");
      return Err(NS_ERROR_FAILURE);
    }
    nsresult rv = MarkElementDirty(*newElement);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING("EditorBase::MarkElementDirty() caused destroying the editor");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::MarkElementDirty() failed, but ignored");
    rv = aInitializer(*this, *newElement, aPointToInsert);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "aInitializer failed");
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    RefPtr<InsertNodeTransaction> transaction =
        InsertNodeTransaction::Create(*this, *newElement, aPointToInsert);
    rv = aWithTransaction == WithTransaction::Yes
             ? DoTransactionInternal(transaction)
             : transaction->DoTransaction();
    // FYI: Transaction::DoTransaction never returns NS_ERROR_EDITOR_*.
    if (MOZ_UNLIKELY(Destroyed())) {
      NS_WARNING(
          "InsertNodeTransaction::DoTransaction() caused destroying the "
          "editor");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("InsertNodeTransaction::DoTransaction() failed");
      return Err(rv);
    }
    // Override the success code if new element was moved by the web apps.
    if (newElement &&
        newElement->GetParentNode() != aPointToInsert.GetContainer()) {
      NS_WARNING("The new element was not inserted into the expected node");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return CreateElementResult(
        std::move(newElement),
        transaction->SuggestPointToPutCaret<EditorDOMPoint>());
  }();

  if (MOZ_UNLIKELY(createNewElementResult.isErr())) {
    NS_WARNING("EditorBase::DoTransactionInternal() failed");
    // XXX Why do we do this even when DoTransaction() returned error?
    DebugOnly<nsresult> rvIgnored =
        RangeUpdaterRef().SelAdjCreateNode(aPointToInsert);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "RangeUpdater::SelAdjCreateNode() failed");
    return createNewElementResult;
  }

  // If we succeeded to create and insert new element, we need to adjust
  // ranges in RangeUpdaterRef().  It currently requires offset of the new
  // node.  So, let's call it with original offset.  Note that if
  // aPointToInsert stores child node, it may not be at the offset since new
  // element must be inserted before the old child.  Although, mutation
  // observer can do anything, but currently, we don't check it.
  DebugOnly<nsresult> rvIgnored =
      RangeUpdaterRef().SelAdjCreateNode(EditorRawDOMPoint(
          aPointToInsert.GetContainer(), aPointToInsert.Offset()));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "RangeUpdater::SelAdjCreateNode() failed, but ignored");
  if (MOZ_LIKELY(createNewElementResult.inspect().GetNewNode())) {
    TopLevelEditSubActionDataRef().DidCreateElement(
        *this, *createNewElementResult.inspect().GetNewNode());
  }

  return createNewElementResult;
}

nsresult HTMLEditor::CopyAttributes(WithTransaction aWithTransaction,
                                    Element& aDestElement, Element& aSrcElement,
                                    const AttributeFilter& aFilterFunc) {
  if (!aSrcElement.GetAttrCount()) {
    return NS_OK;
  }
  struct MOZ_STACK_CLASS AttrCache {
    int32_t mNamespaceID;
    const OwningNonNull<nsAtom> mName;
    nsString mValue;
  };
  AutoTArray<AttrCache, 16> srcAttrs;
  srcAttrs.SetCapacity(aSrcElement.GetAttrCount());
  for (const uint32_t i : IntegerRange(aSrcElement.GetAttrCount())) {
    const BorrowedAttrInfo attrInfo = aSrcElement.GetAttrInfoAt(i);
    if (const nsAttrName* attrName = attrInfo.mName) {
      MOZ_ASSERT(attrName->LocalName());
      MOZ_ASSERT(attrInfo.mValue);
      nsString attrValue;
      attrInfo.mValue->ToString(attrValue);
      srcAttrs.AppendElement(AttrCache{attrInfo.mName->NamespaceID(),
                                       *attrName->LocalName(),
                                       std::move(attrValue)});
    }
  }
  if (aWithTransaction == WithTransaction::No) {
    for (auto& attr : srcAttrs) {
      if (!aFilterFunc(*this, aSrcElement, aDestElement, attr.mNamespaceID,
                       attr.mName, attr.mValue)) {
        continue;
      }
      DebugOnly<nsresult> rvIgnored =
          AutoElementAttrAPIWrapper(*this, aDestElement)
              .SetAttr(MOZ_KnownLive(attr.mName), attr.mValue, false);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored) && rvIgnored != NS_ERROR_EDITOR_DESTROYED,
          "AutoElementAttrAPIWrapper::SetAttr() failed, but ignored");
    }
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    return NS_OK;
  }
  MOZ_ASSERT_UNREACHABLE("Not implemented yet, but you try to use this");
  return NS_ERROR_NOT_IMPLEMENTED;
}

already_AddRefed<Element> HTMLEditor::CreateElementWithDefaults(
    const nsAtom& aTagName) {
  // NOTE: Despite of public method, this can be called for internal use.

  // Although this creates an element, but won't change the DOM tree nor
  // transaction.  So, EditAction::eNotEditing is proper value here.  If
  // this is called for internal when there is already AutoEditActionDataSetter
  // instance, this would be initialized with its EditAction value.
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return nullptr;
  }

  const nsAtom* realTagName = IsLinkTag(aTagName) || IsNamedAnchorTag(aTagName)
                                  ? nsGkAtoms::a
                                  : &aTagName;

  // We don't use editor's CreateElement because we don't want to go through
  // the transaction system

  // New call to use instead to get proper HTML element, bug 39919
  RefPtr<Element> newElement = CreateHTMLContent(realTagName);
  if (!newElement) {
    return nullptr;
  }

  // Mark the new element dirty, so it will be formatted
  // XXX Don't we need to check the error result of setting _moz_dirty attr?
  IgnoredErrorResult ignoredError;
  newElement->SetAttribute(u"_moz_dirty"_ns, u""_ns, ignoredError);
  NS_WARNING_ASSERTION(!ignoredError.Failed(),
                       "Element::SetAttribute(_moz_dirty) failed, but ignored");
  ignoredError.SuppressException();

  // Set default values for new elements
  if (realTagName == nsGkAtoms::table) {
    newElement->SetAttr(nsGkAtoms::cellpadding, u"2"_ns, ignoredError);
    if (ignoredError.Failed()) {
      NS_WARNING("Element::SetAttr(nsGkAtoms::cellpadding, 2) failed");
      return nullptr;
    }
    ignoredError.SuppressException();

    newElement->SetAttr(nsGkAtoms::cellspacing, u"2"_ns, ignoredError);
    if (ignoredError.Failed()) {
      NS_WARNING("Element::SetAttr(nsGkAtoms::cellspacing, 2) failed");
      return nullptr;
    }
    ignoredError.SuppressException();

    newElement->SetAttr(nsGkAtoms::border, u"1"_ns, ignoredError);
    if (ignoredError.Failed()) {
      NS_WARNING("Element::SetAttr(nsGkAtoms::border, 1) failed");
      return nullptr;
    }
  } else if (realTagName == nsGkAtoms::td) {
    nsresult rv = SetAttributeOrEquivalent(newElement, nsGkAtoms::valign,
                                           u"top"_ns, true);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::SetAttributeOrEquivalent(nsGkAtoms::valign, top) "
          "failed");
      return nullptr;
    }
  }
  // ADD OTHER TAGS HERE

  return newElement.forget();
}

NS_IMETHODIMP HTMLEditor::CreateElementWithDefaults(const nsAString& aTagName,
                                                    Element** aReturn) {
  if (NS_WARN_IF(aTagName.IsEmpty()) || NS_WARN_IF(!aReturn)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aReturn = nullptr;

  nsStaticAtom* tagName = EditorUtils::GetTagNameAtom(aTagName);
  if (NS_WARN_IF(!tagName)) {
    return NS_ERROR_INVALID_ARG;
  }
  RefPtr<Element> newElement =
      CreateElementWithDefaults(MOZ_KnownLive(*tagName));
  if (!newElement) {
    NS_WARNING("HTMLEditor::CreateElementWithDefaults() failed");
    return NS_ERROR_FAILURE;
  }
  newElement.forget(aReturn);
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::InsertLinkAroundSelection(Element* aAnchorElement) {
  nsresult rv = InsertLinkAroundSelectionAsAction(aAnchorElement);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::InsertLinkAroundSelectionAsAction() failed");
  return rv;
}

nsresult HTMLEditor::InsertLinkAroundSelectionAsAction(
    Element* aAnchorElement, nsIPrincipal* aPrincipal) {
  if (NS_WARN_IF(!aAnchorElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertLinkElement,
                                          aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<Element> const editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  if (IsPlaintextMailComposer() ||
      editingHost->IsContentEditablePlainTextOnly()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  if (SelectionRef().IsCollapsed()) {
    NS_WARNING("Selection was collapsed");
    return NS_OK;
  }

  // Be sure we were given an anchor element
  RefPtr<HTMLAnchorElement> anchor =
      HTMLAnchorElement::FromNodeOrNull(aAnchorElement);
  if (!anchor) {
    return NS_OK;
  }

  nsAutoString rawHref;
  anchor->GetAttr(nsGkAtoms::href, rawHref);
  editActionData.SetData(rawHref);

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // XXX Is this ok? Does this just want to check that we're a link? If so
  // there are faster ways to do this.
  {
    nsAutoCString href;
    anchor->GetHref(href);
    if (href.IsEmpty()) {
      return NS_OK;
    }
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // Set all attributes found on the supplied anchor element
  // TODO: We should stop using this loop for adding attributes to newly created
  //       `<a href="...">` elements.  Then, we can avoid to increate the ref-
  //       counter of attribute names since we can use nsStaticAtom if we don't
  //       need to support unknown attributes.
  AutoTArray<EditorInlineStyleAndValue, 32> stylesToSet;
  if (const uint32_t attrCount = anchor->GetAttrCount()) {
    stylesToSet.SetCapacity(attrCount);
    for (const uint32_t i : IntegerRange(attrCount)) {
      const BorrowedAttrInfo attrInfo = anchor->GetAttrInfoAt(i);
      if (const nsAttrName* attrName = attrInfo.mName) {
        // We don't need to handle _moz_dirty attribute.  If it's required, the
        // handler should add it to the new element.
        if (attrName->IsAtom() && attrName->Equals(nsGkAtoms::mozdirty)) {
          continue;
        }
        RefPtr<nsAtom> attributeName = attrName->LocalName();
        MOZ_ASSERT(attrInfo.mValue);
        nsString attrValue;
        attrInfo.mValue->ToString(attrValue);
        stylesToSet.AppendElement(EditorInlineStyleAndValue(
            *nsGkAtoms::a, std::move(attributeName), std::move(attrValue)));
      }
    }
  }
  rv = SetInlinePropertiesAsSubAction(stylesToSet, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertiesAsSubAction() failed");
  return rv;
}

nsresult HTMLEditor::SetHTMLBackgroundColorWithTransaction(
    const nsAString& aColor) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Find a selected or enclosing table element to set background on
  bool isCellSelected = false;
  Result<RefPtr<Element>, nsresult> cellOrRowOrTableElementOrError =
      GetSelectedOrParentTableElement(&isCellSelected);
  if (cellOrRowOrTableElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetSelectedOrParentTableElement() failed");
    return cellOrRowOrTableElementOrError.unwrapErr();
  }

  bool setColor = !aColor.IsEmpty();
  RefPtr<Element> rootElementOfBackgroundColor =
      cellOrRowOrTableElementOrError.unwrap();
  if (rootElementOfBackgroundColor) {
    // Needs to set or remove background color of each selected cell elements.
    // Therefore, just the cell contains selection range, we don't need to
    // do this.  Note that users can select each cell, but with Selection API,
    // web apps can select <tr> and <td> at same time. With <table>, looks
    // odd, though.
    if (isCellSelected || rootElementOfBackgroundColor->IsAnyOfHTMLElements(
                              nsGkAtoms::table, nsGkAtoms::tr)) {
      SelectedTableCellScanner scanner(SelectionRef());
      if (scanner.IsInTableCellSelectionMode()) {
        if (setColor) {
          for (const OwningNonNull<Element>& cellElement :
               scanner.ElementsRef()) {
            // `MOZ_KnownLive(cellElement)` is safe because of `scanner`
            // is stack only class and keeps grabbing it until it's destroyed.
            nsresult rv = SetAttributeWithTransaction(
                MOZ_KnownLive(cellElement), *nsGkAtoms::bgcolor, aColor);
            if (NS_WARN_IF(Destroyed())) {
              return NS_ERROR_EDITOR_DESTROYED;
            }
            if (NS_FAILED(rv)) {
              NS_WARNING(
                  "EditorBase::::SetAttributeWithTransaction(nsGkAtoms::"
                  "bgcolor) failed");
              return rv;
            }
          }
          return NS_OK;
        }
        for (const OwningNonNull<Element>& cellElement :
             scanner.ElementsRef()) {
          // `MOZ_KnownLive(cellElement)` is safe because of `scanner`
          // is stack only class and keeps grabbing it until it's destroyed.
          nsresult rv = RemoveAttributeWithTransaction(
              MOZ_KnownLive(cellElement), *nsGkAtoms::bgcolor);
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::bgcolor)"
                " failed");
            return rv;
          }
        }
        return NS_OK;
      }
    }
    // If we failed to find a cell, fall through to use originally-found element
  } else {
    // No table element -- set the background color on the body tag
    rootElementOfBackgroundColor = GetRoot();
    if (NS_WARN_IF(!rootElementOfBackgroundColor)) {
      return NS_ERROR_FAILURE;
    }
  }
  // Use the editor method that goes through the transaction system
  if (setColor) {
    nsresult rv = SetAttributeWithTransaction(*rootElementOfBackgroundColor,
                                              *nsGkAtoms::bgcolor, aColor);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::SetAttributeWithTransaction(nsGkAtoms::bgcolor) failed");
    return rv;
  }
  nsresult rv = RemoveAttributeWithTransaction(*rootElementOfBackgroundColor,
                                               *nsGkAtoms::bgcolor);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::bgcolor) failed");
  return rv;
}

Result<CaretPoint, nsresult>
HTMLEditor::DeleteEmptyInclusiveAncestorInlineElements(
    nsIContent& aContent, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(HTMLEditUtils::IsRemovableFromParentNode(aContent));

  constexpr static HTMLEditUtils::EmptyCheckOptions kOptionsToCheckInline =
      HTMLEditUtils::EmptyCheckOptions{
          EmptyCheckOption::TreatBlockAsVisible,
          EmptyCheckOption::TreatListItemAsVisible,
          EmptyCheckOption::TreatSingleBRElementAsVisible,
          EmptyCheckOption::TreatTableCellAsVisible};

  if (&aContent == &aEditingHost ||
      HTMLEditUtils::IsBlockElement(
          aContent, BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
      !HTMLEditUtils::IsRemovableFromParentNode(aContent) ||
      !aContent.GetParent() ||
      !HTMLEditUtils::IsEmptyNode(aContent, kOptionsToCheckInline)) {
    return CaretPoint(EditorDOMPoint());
  }

  OwningNonNull<nsIContent> content = aContent;
  for (nsIContent* parentContent : aContent.AncestorsOfType<nsIContent>()) {
    if (HTMLEditUtils::IsBlockElement(
            *parentContent, BlockInlineCheck::UseComputedDisplayStyle) ||
        !HTMLEditUtils::IsRemovableFromParentNode(*parentContent) ||
        parentContent == &aEditingHost) {
      break;
    }
    bool parentIsEmpty = true;
    if (parentContent->GetChildCount() > 1) {
      for (nsIContent* sibling = parentContent->GetFirstChild(); sibling;
           sibling = sibling->GetNextSibling()) {
        if (sibling == content) {
          continue;
        }
        if (!HTMLEditUtils::IsEmptyNode(*sibling, kOptionsToCheckInline)) {
          parentIsEmpty = false;
          break;
        }
      }
    }
    if (!parentIsEmpty) {
      break;
    }
    content = *parentContent;
  }

  const nsCOMPtr<nsIContent> nextSibling = content->GetNextSibling();
  const nsCOMPtr<nsINode> parentNode = content->GetParentNode();
  MOZ_ASSERT(parentNode);
  nsresult rv = DeleteNodeWithTransaction(content);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return Err(rv);
  }
  if (NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parentNode) ||
      NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(*parentNode))) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  // Note that even if nextSibling is not editable, we can put caret before it
  // unless parentNode is not editable.
  return CaretPoint(nextSibling ? EditorDOMPoint(nextSibling)
                                : EditorDOMPoint::AtEndOf(*parentNode));
}

nsresult HTMLEditor::DeleteAllChildrenWithTransaction(Element& aElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Prevent rules testing until we're done
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  while (nsCOMPtr<nsIContent> child = aElement.GetLastChild()) {
    nsresult rv = DeleteNodeWithTransaction(*child);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::DeleteNode(nsINode* aNode, bool aPreserveSelection,
                                     uint8_t aOptionalArgCount) {
  if (NS_WARN_IF(!aNode) || NS_WARN_IF(!aNode->IsContent())) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eRemoveNode);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // Make dispatch `input` event after stopping preserving selection.
  AutoPlaceholderBatch treatAsOneTransaction(
      *this,
      ScrollSelectionIntoView::No,  // not a user interaction
      __FUNCTION__);

  Maybe<AutoTransactionsConserveSelection> preserveSelection;
  if (aOptionalArgCount && aPreserveSelection) {
    preserveSelection.emplace(*this);
  }

  rv = DeleteNodeWithTransaction(MOZ_KnownLive(*aNode->AsContent()));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteNodeWithTransaction() failed");
  return rv;
}

Result<CaretPoint, nsresult> HTMLEditor::DeleteTextWithTransaction(
    Text& aTextNode, uint32_t aOffset, uint32_t aLength) {
  if (NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(aTextNode))) {
    return Err(NS_ERROR_FAILURE);
  }

  Result<CaretPoint, nsresult> caretPointOrError =
      EditorBase::DeleteTextWithTransaction(aTextNode, aOffset, aLength);
  NS_WARNING_ASSERTION(caretPointOrError.isOk(),
                       "EditorBase::DeleteTextWithTransaction() failed");
  return caretPointOrError;
}

Result<InsertTextResult, nsresult> HTMLEditor::ReplaceTextWithTransaction(
    dom::Text& aTextNode, const ReplaceWhiteSpacesData& aData) {
  Result<InsertTextResult, nsresult> insertTextResultOrError =
      ReplaceTextWithTransaction(aTextNode, aData.mReplaceStartOffset,
                                 aData.ReplaceLength(),
                                 aData.mNormalizedString);
  if (MOZ_UNLIKELY(insertTextResultOrError.isErr()) ||
      aData.mNewOffsetAfterReplace > aTextNode.TextDataLength()) {
    return insertTextResultOrError;
  }
  InsertTextResult insertTextResult = insertTextResultOrError.unwrap();
  insertTextResult.IgnoreCaretPointSuggestion();
  EditorDOMPoint pointToPutCaret(&aTextNode, aData.mNewOffsetAfterReplace);
  return InsertTextResult(std::move(insertTextResult),
                          std::move(pointToPutCaret));
}

Result<InsertTextResult, nsresult> HTMLEditor::ReplaceTextWithTransaction(
    Text& aTextNode, uint32_t aOffset, uint32_t aLength,
    const nsAString& aStringToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aLength > 0 || !aStringToInsert.IsEmpty());

  if (aStringToInsert.IsEmpty()) {
    Result<CaretPoint, nsresult> caretPointOrError =
        DeleteTextWithTransaction(aTextNode, aOffset, aLength);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
      return caretPointOrError.propagateErr();
    }
    return InsertTextResult(EditorDOMPoint(&aTextNode, aOffset),
                            caretPointOrError.unwrap());
  }

  if (!aLength) {
    Result<InsertTextResult, nsresult> insertTextResult =
        InsertTextWithTransaction(aStringToInsert,
                                  EditorDOMPoint(&aTextNode, aOffset),
                                  InsertTextTo::ExistingTextNodeIfAvailable);
    NS_WARNING_ASSERTION(insertTextResult.isOk(),
                         "HTMLEditor::InsertTextWithTransaction() failed");
    return insertTextResult;
  }

  if (NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(aTextNode))) {
    return Err(NS_ERROR_FAILURE);
  }

  // This should emulates inserting text for better undo/redo behavior.
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // FYI: Create the insertion point before changing the DOM tree because
  //      the point may become invalid offset after that.
  EditorDOMPointInText pointToInsert(&aTextNode, aOffset);

  RefPtr<ReplaceTextTransaction> transaction = ReplaceTextTransaction::Create(
      *this, aStringToInsert, aTextNode, aOffset, aLength);
  MOZ_ASSERT(transaction);

  if (aLength && !mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->WillDeleteText(&aTextNode, aOffset, aLength);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::WillDeleteText() failed, but ignored");
    }
  }

  nsresult rv = DoTransactionInternal(transaction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DoTransactionInternal() failed");

  // Don't check whether we've been destroyed here because we need to notify
  // listeners and observers below even if we've already destroyed.

  EditorDOMPoint endOfInsertedText(&aTextNode,
                                   aOffset + aStringToInsert.Length());

  if (pointToInsert.IsSet()) {
    auto [begin, end] = ComputeInsertedRange(pointToInsert, aStringToInsert);
    if (begin.IsSet() && end.IsSet()) {
      TopLevelEditSubActionDataRef().DidDeleteText(
          *this, begin.RefOrTo<EditorRawDOMPoint>());
      TopLevelEditSubActionDataRef().DidInsertText(
          *this, begin.RefOrTo<EditorRawDOMPoint>(),
          end.RefOrTo<EditorRawDOMPoint>());
    }

    // XXX Should we update endOfInsertedText here?
  }

  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->DidInsertText(&aTextNode, aOffset, aStringToInsert, rv);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidInsertText() failed, but ignored");
    }
  }

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  return InsertTextResult(
      std::move(endOfInsertedText),
      transaction->SuggestPointToPutCaret<EditorDOMPoint>());
}

Result<InsertTextResult, nsresult>
HTMLEditor::InsertOrReplaceTextWithTransaction(
    const EditorDOMPoint& aPointToInsert,
    const NormalizedStringToInsertText& aData) {
  MOZ_ASSERT(aPointToInsert.IsInContentNodeAndValid());
  MOZ_ASSERT_IF(aData.ReplaceLength(), aPointToInsert.IsInTextNode());

  Result<InsertTextResult, nsresult> insertTextResultOrError =
      !aData.ReplaceLength()
          ? InsertTextWithTransaction(aData.mNormalizedString, aPointToInsert,
                                      InsertTextTo::SpecifiedPoint)
          : ReplaceTextWithTransaction(
                MOZ_KnownLive(*aPointToInsert.ContainerAs<Text>()),
                aData.mReplaceStartOffset, aData.ReplaceLength(),
                aData.mNormalizedString);
  if (MOZ_UNLIKELY(insertTextResultOrError.isErr())) {
    NS_WARNING(!aData.ReplaceLength()
                   ? "HTMLEditor::InsertTextWithTransaction() failed"
                   : "HTMLEditor::ReplaceTextWithTransaction() failed");
    return insertTextResultOrError;
  }
  InsertTextResult insertTextResult = insertTextResultOrError.unwrap();
  if (!aData.ReplaceLength()) {
    auto pointToPutCaret = [&]() -> EditorDOMPoint {
      return insertTextResult.HasCaretPointSuggestion()
                 ? insertTextResult.UnwrapCaretPoint()
                 : insertTextResult.EndOfInsertedTextRef();
    }();
    return InsertTextResult(std::move(insertTextResult),
                            std::move(pointToPutCaret));
  }
  insertTextResult.IgnoreCaretPointSuggestion();
  Text* const insertedTextNode =
      insertTextResult.EndOfInsertedTextRef().GetContainerAs<Text>();
  if (NS_WARN_IF(!insertedTextNode) ||
      NS_WARN_IF(!insertedTextNode->IsInComposedDoc()) ||
      NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(*insertedTextNode))) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  const uint32_t expectedEndOffset = aData.EndOffsetOfInsertedText();
  if (NS_WARN_IF(expectedEndOffset > insertedTextNode->TextDataLength())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  // We need to return end point of the insertion string instead of end of
  // replaced following white-spaces.
  EditorDOMPoint endOfNewString(insertedTextNode, expectedEndOffset);
  EditorDOMPoint pointToPutCaret = endOfNewString;
  return InsertTextResult(std::move(endOfNewString),
                          CaretPoint(std::move(pointToPutCaret)));
}

Result<InsertTextResult, nsresult> HTMLEditor::InsertTextWithTransaction(
    const nsAString& aStringToInsert, const EditorDOMPoint& aPointToInsert,
    InsertTextTo aInsertTextTo) {
  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  // Do nothing if the node is read-only
  if (NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(
          *aPointToInsert.GetContainer()))) {
    return Err(NS_ERROR_FAILURE);
  }

  return EditorBase::InsertTextWithTransaction(aStringToInsert, aPointToInsert,
                                               aInsertTextTo);
}

Result<EditorDOMPoint, nsresult> HTMLEditor::PrepareToInsertLineBreak(
    LineBreakType aLineBreakType, const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aPointToInsert.IsInContentNode())) {
    return Err(NS_ERROR_FAILURE);
  }

  const auto CanInsertLineBreak = [aLineBreakType](const nsIContent& aContent) {
    if (aLineBreakType == LineBreakType::BRElement) {
      return HTMLEditUtils::CanNodeContain(aContent, *nsGkAtoms::br);
    }
    MOZ_ASSERT(aLineBreakType == LineBreakType::Linefeed);
    return HTMLEditUtils::CanNodeContain(aContent, *nsGkAtoms::textTagName) &&
           EditorUtils::IsNewLinePreformatted(aContent);
  };

  // If we're being initialized, we cannot normalize white-spaces because the
  // normalizer may remove invisible `Text`, but it's not allowed during the
  // initialization.
  // FIXME: Anyway, we should not do this at initialization. This is required
  // only for making users can put caret into empty table cells and list items.
  const bool canNormalizeWhiteSpaces = mInitSucceeded;

  if (!aPointToInsert.IsInTextNode()) {
    if (NS_WARN_IF(
            !CanInsertLineBreak(*aPointToInsert.ContainerAs<nsIContent>()))) {
      return Err(NS_ERROR_FAILURE);
    }
    if (!canNormalizeWhiteSpaces) {
      return aPointToInsert;
    }
    Result<EditorDOMPoint, nsresult> pointToInsertOrError =
        WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt(
            *this, aPointToInsert,
            {WhiteSpaceVisibilityKeeper::NormalizeOption::
                 StopIfPrecedingWhiteSpacesEndsWithNBP});
    if (NS_WARN_IF(pointToInsertOrError.isErr())) {
      return pointToInsertOrError.propagateErr();
    }
    return pointToInsertOrError.unwrap();
  }

  // If the text node is not in an element node, we cannot insert a line break
  // around the text node.
  const Element* const containerOrNewLineBreak =
      aPointToInsert.GetContainerParentAs<Element>();
  if (NS_WARN_IF(!containerOrNewLineBreak) ||
      NS_WARN_IF(!CanInsertLineBreak(*containerOrNewLineBreak))) {
    return Err(NS_ERROR_FAILURE);
  }

  Result<EditorDOMPoint, nsresult> pointToInsertOrError =
      canNormalizeWhiteSpaces
          ? WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt(
                *this, aPointToInsert,
                {WhiteSpaceVisibilityKeeper::NormalizeOption::
                     StopIfPrecedingWhiteSpacesEndsWithNBP})
          : aPointToInsert;
  if (NS_WARN_IF(pointToInsertOrError.isErr())) {
    return pointToInsertOrError.propagateErr();
  }
  const EditorDOMPoint pointToInsert = pointToInsertOrError.unwrap();
  if (!pointToInsert.IsInTextNode()) {
    return pointToInsert.ParentPoint();
  }

  if (pointToInsert.IsStartOfContainer()) {
    // Insert before the text node.
    return pointToInsert.ParentPoint();
  }

  if (pointToInsert.IsEndOfContainer()) {
    // Insert after the text node.
    return EditorDOMPoint::After(*pointToInsert.ContainerAs<Text>());
  }

  MOZ_DIAGNOSTIC_ASSERT(pointToInsert.IsSetAndValid());

  // Unfortunately, we need to split the text node at the offset.
  Result<SplitNodeResult, nsresult> splitTextNodeResult =
      SplitNodeWithTransaction(pointToInsert);
  if (MOZ_UNLIKELY(splitTextNodeResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
    return splitTextNodeResult.propagateErr();
  }

  // TODO: Stop updating `Selection`.
  nsresult rv = splitTextNodeResult.inspect().SuggestCaretPointTo(
      *this, {SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
  if (NS_FAILED(rv)) {
    NS_WARNING("SplitNodeResult::SuggestCaretPointTo() failed");
    return Err(rv);
  }

  // Insert new line break before the right node.
  auto atNextContent =
      splitTextNodeResult.inspect().AtNextContent<EditorDOMPoint>();
  if (MOZ_UNLIKELY(!atNextContent.IsInContentNode())) {
    NS_WARNING("The next node seems not in the DOM tree");
    return Err(NS_ERROR_FAILURE);
  }
  return atNextContent;
}

Maybe<HTMLEditor::LineBreakType> HTMLEditor::GetPreferredLineBreakType(
    const nsINode& aNode, const Element& aEditingHost) const {
  const Element* const container = aNode.GetAsElementOrParentElement();
  if (MOZ_UNLIKELY(!container)) {
    return Nothing();
  }
  // For backward compatibility, we should not insert a linefeed if
  // paragraph separator is set to "br" which is Gecko-specific mode.
  if (GetDefaultParagraphSeparator() == ParagraphSeparator::br) {
    return Some(LineBreakType::BRElement);
  }
  // And also if we're the mail composer, the content needs to be serialized.
  // Therefore, we should always use <br> for the serializer.
  if (IsMailEditor() || IsPlaintextMailComposer()) {
    return Some(LineBreakType::BRElement);
  }
  if (HTMLEditUtils::ShouldInsertLinefeedCharacter(EditorDOMPoint(container, 0),
                                                   aEditingHost) &&
      HTMLEditUtils::CanNodeContain(*container, *nsGkAtoms::textTagName)) {
    return Some(LineBreakType::Linefeed);
  }
  if (MOZ_UNLIKELY(
          !HTMLEditUtils::CanNodeContain(*container, *nsGkAtoms::br))) {
    return Nothing();
  }
  return Some(LineBreakType::BRElement);
}

Result<CreateLineBreakResult, nsresult> HTMLEditor::InsertLineBreak(
    WithTransaction aWithTransaction, LineBreakType aLineBreakType,
    const EditorDOMPoint& aPointToInsert, EDirection aSelect /* = eNone */) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Result<EditorDOMPoint, nsresult> pointToInsertOrError =
      PrepareToInsertLineBreak(aLineBreakType, aPointToInsert);
  if (MOZ_UNLIKELY(pointToInsertOrError.isErr())) {
    NS_WARNING(
        nsPrintfCString("HTMLEditor::PrepareToInsertLineBreak(%s) failed",
                        ToString(aWithTransaction).c_str())
            .get());
    return pointToInsertOrError.propagateErr();
  }
  EditorDOMPoint pointToInsert = pointToInsertOrError.unwrap();
  MOZ_ASSERT(pointToInsert.IsInContentNode());
  MOZ_ASSERT(pointToInsert.IsSetAndValid());

  auto lineBreakOrError = [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<EditorLineBreak, nsresult> {
    if (aLineBreakType == LineBreakType::BRElement) {
      Result<CreateElementResult, nsresult> insertBRElementResultOrError =
          InsertBRElement(aWithTransaction, BRElementType::Normal,
                          pointToInsert);
      if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
        NS_WARNING(
            nsPrintfCString(
                "EditorBase::InsertBRElement(%s, BRElementType::Normal) failed",
                ToString(aWithTransaction).c_str())
                .get());
        return insertBRElementResultOrError.propagateErr();
      }
      CreateElementResult insertBRElementResult =
          insertBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertBRElementResult.Handled());
      insertBRElementResult.IgnoreCaretPointSuggestion();
      return EditorLineBreak(insertBRElementResult.UnwrapNewNode());
    }
    MOZ_ASSERT(aLineBreakType == LineBreakType::Linefeed);
    RefPtr<Text> newTextNode = CreateTextNode(u"\n"_ns);
    if (NS_WARN_IF(!newTextNode)) {
      return Err(NS_ERROR_FAILURE);
    }
    if (aWithTransaction == WithTransaction::Yes) {
      Result<CreateTextResult, nsresult> insertTextNodeResult =
          InsertNodeWithTransaction<Text>(*newTextNode, pointToInsert);
      if (MOZ_UNLIKELY(insertTextNodeResult.isErr())) {
        NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
        return insertTextNodeResult.propagateErr();
      }
      insertTextNodeResult.unwrap().IgnoreCaretPointSuggestion();
    } else {
      (void)pointToInsert.Offset();
      RefPtr<InsertNodeTransaction> transaction =
          InsertNodeTransaction::Create(*this, *newTextNode, pointToInsert);
      nsresult rv = transaction->DoTransaction();
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (NS_FAILED(rv)) {
        NS_WARNING("InsertNodeTransaction::DoTransaction() failed");
        return Err(rv);
      }
      if (NS_WARN_IF(newTextNode->GetParentNode() !=
                     pointToInsert.GetContainer())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      RangeUpdaterRef().SelAdjInsertNode(EditorRawDOMPoint(
          pointToInsert.GetContainer(), pointToInsert.Offset()));
    }
    if (NS_WARN_IF(!newTextNode->TextDataLength() ||
                   newTextNode->DataBuffer().CharAt(0) != '\n')) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return EditorLineBreak(std::move(newTextNode), 0u);
  }();
  if (MOZ_UNLIKELY(lineBreakOrError.isErr())) {
    return lineBreakOrError.propagateErr();
  }
  EditorLineBreak lineBreak = lineBreakOrError.unwrap();
  auto pointToPutCaret = [&]() -> EditorDOMPoint {
    switch (aSelect) {
      case eNext: {
        return lineBreak.After<EditorDOMPoint>();
      }
      case ePrevious: {
        return lineBreak.Before<EditorDOMPoint>();
      }
      default:
        NS_WARNING(
            "aSelect has invalid value, the caller need to set selection "
            "by itself");
        [[fallthrough]];
      case eNone:
        return lineBreak.To<EditorDOMPoint>();
    }
  }();
  return CreateLineBreakResult(std::move(lineBreak),
                               std::move(pointToPutCaret));
}

nsresult HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak(
    const EditorDOMPoint& aNextOrAfterModifiedPoint,
    PreservePreformattedLineBreak aPreservePreformattedLineBreak,
    PaddingForEmptyBlock aPaddingForEmptyBlock, const Element& aEditingHost) {
  MOZ_ASSERT(aNextOrAfterModifiedPoint.IsInContentNode());
  MOZ_ASSERT(aNextOrAfterModifiedPoint.IsSetAndValid());

  // If the point is in a mailcite in plaintext mail composer (it is a <span>
  // styled as block), we should not treat its padding <br> as unnecessary
  // because it's required by the serializer to give next content of the
  // mailcite has its own line.
  if (IsPlaintextMailComposer()) {
    const Element* const blockElement =
        HTMLEditUtils::GetInclusiveAncestorElement(
            *aNextOrAfterModifiedPoint.ContainerAs<nsIContent>(),
            HTMLEditUtils::ClosestEditableBlockElement,
            BlockInlineCheck::UseComputedDisplayStyle);
    if (blockElement && HTMLEditUtils::IsMailCiteElement(*blockElement) &&
        HTMLEditUtils::IsInlineContent(*blockElement,
                                       BlockInlineCheck::UseHTMLDefaultStyle)) {
      return NS_OK;
    }
  }

  const bool isWhiteSpacePreformatted = EditorUtils::IsWhiteSpacePreformatted(
      *aNextOrAfterModifiedPoint.ContainerAs<nsIContent>());
  const DebugOnly<bool> isNewLinePreformatted =
      EditorUtils::IsNewLinePreformatted(
          *aNextOrAfterModifiedPoint.ContainerAs<nsIContent>());

  const WSScanResult nextThing =
      HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
          aNextOrAfterModifiedPoint, aPaddingForEmptyBlock, aEditingHost);
  const Maybe<EditorLineBreak>& unnecessaryLineBreak =
      nextThing.MaybeIgnoredLineBreak();
  if (unnecessaryLineBreak.isNothing() ||
      !unnecessaryLineBreak->IsInclusiveDescendantOf(aEditingHost) ||
      !unnecessaryLineBreak->IsDeletableFromComposedDoc()) [[likely]] {
    return NS_OK;
  }
  if (unnecessaryLineBreak->IsHTMLBRElement()) {
    // If the found unnecessary <br> is a preceding one of a mailcite which is a
    // <span> styled as block, we need to preserve the <br> element for the
    // serializer to cause a line break before the mailcite.
    if (IsPlaintextMailComposer()) {
      if (nextThing.ReachedOtherBlockElement() &&
          HTMLEditUtils::IsMailCiteElement(*nextThing.ElementPtr()) &&
          HTMLEditUtils::IsInlineContent(
              *nextThing.ElementPtr(), BlockInlineCheck::UseHTMLDefaultStyle)) {
        return NS_OK;
      }
    }
    // If the invisible break is a placeholder of ancestor inline elements, we
    // should not delete it to allow users to insert text with the format
    // specified by them.
    if (HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
            unnecessaryLineBreak->BRElementRef(),
            BlockInlineCheck::UseComputedDisplayStyle)) {
      return NS_OK;
    }
    nsresult rv = DeleteNodeWithTransaction(
        MOZ_KnownLive(unnecessaryLineBreak->BRElementRef()));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DeleteNodeWithTransaction() failed "
                         "to delete unnecessary <br>");
    return rv;
  }
  MOZ_ASSERT(isNewLinePreformatted);
  if (aPreservePreformattedLineBreak == PreservePreformattedLineBreak::Yes) {
    // Do not preserve preformatted linefeed if it's a padding for empty block
    // even if the caller wants to preserve unnecessary preformatted linefeed
    // because it's set to "Yes" when the caller inserts new content, but the
    // other browsers do not preserve the padding linefeed for empty block.
    if (aPaddingForEmptyBlock == PaddingForEmptyBlock::Significant ||
        !unnecessaryLineBreak->IsPaddingForEmptyBlock()) {
      return NS_OK;
    }
  }
  const auto IsVisibleChar = [&](char16_t aChar) {
    switch (aChar) {
      case HTMLEditUtils::kNewLine:
        return true;
      case HTMLEditUtils::kSpace:
      case HTMLEditUtils::kTab:
      case HTMLEditUtils::kCarriageReturn:
        return isWhiteSpacePreformatted;
      default:
        return true;
    }
  };
  const CharacterDataBuffer& characterDataBuffer =
      unnecessaryLineBreak->TextRef().DataBuffer();
  const uint32_t length = characterDataBuffer.GetLength();
  const DebugOnly<const char16_t> lastChar =
      characterDataBuffer.CharAt(length - 1);
  MOZ_ASSERT(lastChar == HTMLEditUtils::kNewLine);
  const bool textNodeHasVisibleChar = [&]() {
    if (length == 1u) {
      return false;
    }
    for (const uint32_t offset : Reversed(IntegerRange(length - 1))) {
      if (IsVisibleChar(characterDataBuffer.CharAt(offset))) {
        return true;
      }
    }
    return false;
  }();
  if (!textNodeHasVisibleChar) {
    // If the invisible break is a placeholder of ancestor inline elements, we
    // should not delete it to allow users to insert text with the format
    // specified by them.
    if (HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
            unnecessaryLineBreak->TextRef(),
            BlockInlineCheck::UseComputedDisplayStyle)) {
      return NS_OK;
    }
    nsresult rv = DeleteNodeWithTransaction(
        MOZ_KnownLive(unnecessaryLineBreak->TextRef()));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DeleteNodeWithTransaction() failed "
                         "to delete unnecessary Text node");
    return rv;
  }
  Result<CaretPoint, nsresult> result =
      DeleteTextWithTransaction(MOZ_KnownLive(unnecessaryLineBreak->TextRef()),
                                unnecessaryLineBreak->Offset(), 1);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
    return result.unwrapErr();
  }
  result.unwrap().IgnoreCaretPointSuggestion();
  return NS_OK;
}

Result<CreateElementResult, nsresult>
HTMLEditor::InsertContainerWithTransaction(
    nsIContent& aContentToBeWrapped, const nsAtom& aWrapperTagName,
    const InitializeInsertingElement& aInitializer) {
  EditorDOMPoint pointToInsertNewContainer(&aContentToBeWrapped);
  if (NS_WARN_IF(!pointToInsertNewContainer.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }
  // aContentToBeWrapped will be moved to the new container before inserting the
  // new container.  So, when we insert the container, the insertion point is
  // before the next sibling of aContentToBeWrapped.
  // XXX If pointerToInsertNewContainer stores offset here, the offset and
  //     referring child node become mismatched.  Although, currently this
  //     is not a problem since InsertNodeTransaction refers only child node.
  MOZ_ALWAYS_TRUE(pointToInsertNewContainer.AdvanceOffset());

  // Create new container.
  RefPtr<Element> newContainer = CreateHTMLContent(&aWrapperTagName);
  if (NS_WARN_IF(!newContainer)) {
    return Err(NS_ERROR_FAILURE);
  }

  if (&aInitializer != &HTMLEditor::DoNothingForNewElement) {
    nsresult rv = aInitializer(*this, *newContainer,
                               EditorDOMPoint(&aContentToBeWrapped));
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("aInitializer() failed");
      return Err(rv);
    }
  }

  // Notify our internal selection state listener
  AutoInsertContainerSelNotify selNotify(RangeUpdaterRef());

  // Put aNode in the new container, first.
  // XXX Perhaps, we should not remove the container if it's not editable.
  nsresult rv = DeleteNodeWithTransaction(aContentToBeWrapped);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return Err(rv);
  }

  {
    // TODO: Remove AutoTransactionsConserveSelection here.  It's not necessary
    //       in normal cases.  However, it may be required for nested edit
    //       actions which may be caused by legacy mutation event listeners or
    //       chrome script.
    AutoTransactionsConserveSelection conserveSelection(*this);
    Result<CreateContentResult, nsresult> insertContentNodeResult =
        InsertNodeWithTransaction(aContentToBeWrapped,
                                  EditorDOMPoint(newContainer, 0u));
    if (MOZ_UNLIKELY(insertContentNodeResult.isErr())) {
      NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
      return insertContentNodeResult.propagateErr();
    }
    insertContentNodeResult.inspect().IgnoreCaretPointSuggestion();
  }

  // Put the new container where aNode was.
  Result<CreateElementResult, nsresult> insertNewContainerElementResult =
      InsertNodeWithTransaction<Element>(*newContainer,
                                         pointToInsertNewContainer);
  NS_WARNING_ASSERTION(insertNewContainerElementResult.isOk(),
                       "EditorBase::InsertNodeWithTransaction() failed");
  return insertNewContainerElementResult;
}

Result<CreateElementResult, nsresult>
HTMLEditor::ReplaceContainerWithTransactionInternal(
    Element& aOldContainer, const nsAtom& aTagName, const nsAtom& aAttribute,
    const nsAString& aAttributeValue, bool aCloneAllAttributes) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(aOldContainer)) ||
      NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(aOldContainer))) {
    return Err(NS_ERROR_FAILURE);
  }

  // If we're replacing <dd> or <dt> with different type of element, we need to
  // split the parent <dl>.
  OwningNonNull<Element> containerElementToDelete = aOldContainer;
  if (aOldContainer.IsAnyOfHTMLElements(nsGkAtoms::dd, nsGkAtoms::dt) &&
      &aTagName != nsGkAtoms::dt && &aTagName != nsGkAtoms::dd &&
      // aOldContainer always has a parent node because of removable.
      aOldContainer.GetParentNode()->IsHTMLElement(nsGkAtoms::dl)) {
    OwningNonNull<Element> const dlElement = *aOldContainer.GetParentElement();
    if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(dlElement)) ||
        NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(dlElement))) {
      return Err(NS_ERROR_FAILURE);
    }
    Result<SplitRangeOffFromNodeResult, nsresult> splitDLElementResult =
        SplitRangeOffFromElement(dlElement, aOldContainer, aOldContainer);
    if (MOZ_UNLIKELY(splitDLElementResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitRangeOffFromElement() failed");
      return splitDLElementResult.propagateErr();
    }
    splitDLElementResult.inspect().IgnoreCaretPointSuggestion();
    RefPtr<Element> middleDLElement = aOldContainer.GetParentElement();
    if (NS_WARN_IF(!middleDLElement) ||
        NS_WARN_IF(!middleDLElement->IsHTMLElement(nsGkAtoms::dl)) ||
        NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*middleDLElement))) {
      NS_WARNING("The parent <dl> was lost at splitting it");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    containerElementToDelete = std::move(middleDLElement);
  }

  const RefPtr<Element> newContainer = CreateHTMLContent(&aTagName);
  if (NS_WARN_IF(!newContainer)) {
    return Err(NS_ERROR_FAILURE);
  }

  // Set or clone attribute if needed.
  // FIXME: What should we do attributes of <dl> elements if we removed it
  // above?
  if (aCloneAllAttributes) {
    MOZ_ASSERT(&aAttribute == nsGkAtoms::_empty);
    CloneAttributesWithTransaction(*newContainer, aOldContainer);
  } else if (&aAttribute != nsGkAtoms::_empty) {
    nsresult rv = newContainer->SetAttr(kNameSpaceID_None,
                                        const_cast<nsAtom*>(&aAttribute),
                                        aAttributeValue, true);
    if (NS_FAILED(rv)) {
      NS_WARNING("Element::SetAttr() failed");
      return Err(NS_ERROR_FAILURE);
    }
  }

  const OwningNonNull<nsINode> parentNode =
      *containerElementToDelete->GetParentNode();
  const nsCOMPtr<nsINode> referenceNode =
      containerElementToDelete->GetNextSibling();
  AutoReplaceContainerSelNotify selStateNotify(RangeUpdaterRef(), aOldContainer,
                                               *newContainer);
  if (aOldContainer.HasChildren()) {
    // TODO: Remove AutoTransactionsConserveSelection here.  It's not necessary
    //       in normal cases.  However, it may be required for nested edit
    //       actions which may be caused by legacy mutation event listeners or
    //       chrome script.
    // Move non-editable children too because its container, aElement, is
    // editable so that all children must be removable node.
    const OwningNonNull<nsIContent> firstChild = *aOldContainer.GetFirstChild();
    const OwningNonNull<nsIContent> lastChild = *aOldContainer.GetLastChild();
    Result<MoveNodeResult, nsresult> moveChildrenResultOrError =
        MoveSiblingsWithTransaction(firstChild, lastChild,
                                    EditorDOMPoint(newContainer, 0));
    if (MOZ_UNLIKELY(moveChildrenResultOrError.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsWithTransaction() failed");
      return moveChildrenResultOrError.propagateErr();
    }
    // We'll suggest new caret point which is suggested by new container
    // element insertion result.  Therefore, we need to do nothing here.
    moveChildrenResultOrError.inspect().IgnoreCaretPointSuggestion();
  }

  // Delete containerElementToDelete from the DOM tree to make it not referred
  // by InsertNodeTransaction.
  nsresult rv = DeleteNodeWithTransaction(containerElementToDelete);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return Err(rv);
  }

  if (referenceNode && (!referenceNode->GetParentNode() ||
                        parentNode != referenceNode->GetParentNode())) {
    NS_WARNING(
        "The reference node for insertion has been moved to different parent, "
        "so we got lost the insertion point");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // Finally, insert the new node to where probably aOldContainer was.
  Result<CreateElementResult, nsresult> insertNewContainerElementResult =
      InsertNodeWithTransaction<Element>(
          *newContainer, referenceNode ? EditorDOMPoint(referenceNode)
                                       : EditorDOMPoint::AtEndOf(*parentNode));
  NS_WARNING_ASSERTION(insertNewContainerElementResult.isOk(),
                       "EditorBase::InsertNodeWithTransaction() failed");
  MOZ_ASSERT_IF(
      insertNewContainerElementResult.isOk(),
      insertNewContainerElementResult.inspect().GetNewNode() == newContainer);
  return insertNewContainerElementResult;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::RemoveContainerWithTransaction(
    Element& aElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(aElement)) ||
      NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(aElement))) {
    return Err(NS_ERROR_FAILURE);
  }

  // Notify our internal selection state listener.
  AutoRemoveContainerSelNotify selNotify(RangeUpdaterRef(),
                                         EditorRawDOMPoint(&aElement));
  const nsCOMPtr<nsINode> parentNode = aElement.GetParentNode();
  const nsCOMPtr<nsIContent> nextSibling = aElement.GetNextSibling();
  EditorDOMPoint pointToPutCaret;
  // FIXME: If we'd improve the range updater to be able to track moving nodes
  // after removing the container first, we should do that because
  // IMEContentObserver can run faster.
  if (aElement.HasChildren()) {
    const OwningNonNull<nsIContent> firstChild = *aElement.GetFirstChild();
    const OwningNonNull<nsIContent> lastChild = *aElement.GetLastChild();
    Result<MoveNodeResult, nsresult> moveChildrenResultOrError =
        MoveSiblingsWithTransaction(firstChild, lastChild,
                                    nextSibling
                                        ? EditorDOMPoint(nextSibling)
                                        : EditorDOMPoint::AtEndOf(*parentNode));
    if (MOZ_UNLIKELY(moveChildrenResultOrError.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsWithTransaction() failed");
      return moveChildrenResultOrError.propagateErr();
    }
    pointToPutCaret = moveChildrenResultOrError.unwrap().UnwrapCaretPoint();
  }
  {
    AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
    nsresult rv = DeleteNodeWithTransaction(aElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    if (NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parentNode)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
  }
  if (pointToPutCaret.IsSetAndValidInComposedDoc()) {
    return pointToPutCaret;
  }
  return nextSibling && nextSibling->GetParentNode() == parentNode
             ? EditorDOMPoint(nextSibling)
             : EditorDOMPoint::AtEndOf(*parentNode);
}

nsresult HTMLEditor::SelectEntireDocument() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!mInitSucceeded) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // XXX It's odd to select all of the document body if an contenteditable
  //     element has focus.
  RefPtr<Element> bodyOrDocumentElement = GetRoot();
  if (NS_WARN_IF(!bodyOrDocumentElement)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // If we're empty, don't select all children because that would select the
  // padding <br> element for empty editor.
  if (IsEmpty()) {
    nsresult rv = CollapseSelectionToStartOf(*bodyOrDocumentElement);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionToStartOf() failed");
    return rv;
  }

  // Otherwise, select all children.
  ErrorResult error;
  SelectionRef().SelectAllChildren(*bodyOrDocumentElement, error);
  if (NS_WARN_IF(Destroyed())) {
    error.SuppressException();
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(!error.Failed(),
                       "Selection::SelectAllChildren() failed");
  return error.StealNSResult();
}

nsresult HTMLEditor::SelectAllInternal() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  CommitComposition();
  const RefPtr<Document> doc = GetDocument();
  if (NS_WARN_IF(!doc)) {
    // The root caller should not use HTMLEditor in this case (mDocument won't
    // be cleared except by the cycle collector).
    return NS_ERROR_NOT_AVAILABLE;
  }

  auto GetBodyElementIfElementIsParentOfHTMLBody =
      [](const Element& aElement) -> Element* {
    if (!aElement.OwnerDoc()->IsHTMLDocument()) {
      return const_cast<Element*>(&aElement);
    }
    HTMLBodyElement* bodyElement = aElement.OwnerDoc()->GetBodyElement();
    return bodyElement && nsContentUtils::ContentIsFlattenedTreeDescendantOf(
                              bodyElement, &aElement)
               ? bodyElement
               : const_cast<Element*>(&aElement);
  };

  const nsCOMPtr<nsIContent> selectionRootContent =
      [&]() MOZ_CAN_RUN_SCRIPT -> nsIContent* {
    const RefPtr<Element> elementForComputingSelectionRoot = [&]() -> Element* {
      // If there is at least one selection range, we should compute the
      // selection root from the anchor node.
      if (SelectionRef().RangeCount()) {
        if (nsIContent* content =
                nsIContent::FromNodeOrNull(SelectionRef().GetAnchorNode())) {
          if (content->IsElement()) {
            return content->AsElement();
          }
          if (Element* parentElement =
                  content->GetParentElementCrossingShadowRoot()) {
            return parentElement;
          }
        }
      }
      // If no element contains a selection range, we should select all children
      // of the focused element at least.
      if (Element* focusedElement = GetFocusedElement()) {
        return focusedElement;
      }
      // Okay, there is no selection range and no focused element, let's select
      // all of the body or the document element.
      if (Element* const bodyElement = GetBodyElement()) {
        return bodyElement;
      }
      return doc->GetDocumentElement();
    }();
    if (MOZ_UNLIKELY(!elementForComputingSelectionRoot)) {
      return nullptr;
    }

    // Then, compute the selection root content to select all including
    // elementToBeSelected.
    RefPtr<PresShell> presShell = GetPresShell();
    nsIContent* computedSelectionRootContent =
        elementForComputingSelectionRoot->GetSelectionRootContent(
            presShell, nsINode::IgnoreOwnIndependentSelection::Yes,
            nsINode::AllowCrossShadowBoundary::No);
    if (NS_WARN_IF(!computedSelectionRootContent)) {
      return nullptr;
    }
    if (MOZ_UNLIKELY(!computedSelectionRootContent->IsElement())) {
      return computedSelectionRootContent;
    }
    return GetBodyElementIfElementIsParentOfHTMLBody(
        *computedSelectionRootContent->AsElement());
  }();
  if (MOZ_UNLIKELY(!selectionRootContent)) {
    // If there is no element in the document, the document node can have
    // selection range whose container is the document node. However, Chrome
    // does not handle "Select All" command in this case (if there is no
    // selection range, no new range is created and if there is a collapsed
    // range, the range won't be extended to select all children of the
    // Document).  Let's follow it.
    return NS_OK;
  }

  Maybe<Selection::AutoUserInitiated> userSelection;
  // XXX Do we need to mark it as "user initiated" for
  //     `Document.execCommand("selectAll")`?
  if (!selectionRootContent->IsEditable()) {
    userSelection.emplace(SelectionRef());
  }
  ErrorResult error;
  SelectionRef().SelectAllChildren(*selectionRootContent, error);
  NS_WARNING_ASSERTION(!error.Failed(),
                       "Selection::SelectAllChildren() failed");
  return error.StealNSResult();
}

bool HTMLEditor::SetCaretInTableCell(Element* aElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!aElement || !aElement->IsHTMLElement() ||
      !HTMLEditUtils::IsAnyTableElementExceptColumnElement(*aElement)) {
    return false;
  }
  const RefPtr<Element> editingHost = ComputeEditingHost();
  if (!editingHost || !aElement->IsInclusiveDescendantOf(editingHost)) {
    return false;
  }

  nsCOMPtr<nsIContent> deepestFirstChild = aElement;
  while (deepestFirstChild->HasChildren()) {
    deepestFirstChild = deepestFirstChild->GetFirstChild();
  }

  // Set selection at beginning of the found node
  nsresult rv = CollapseSelectionToStartOf(*deepestFirstChild);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionToStartOf() failed");
  return NS_SUCCEEDED(rv);
}

/**
 * This method scans the selection for adjacent text nodes
 * and collapses them into a single text node.
 * "adjacent" means literally adjacent siblings of the same parent.
 * Uses HTMLEditor::JoinNodesWithTransaction() so action is undoable.
 * Should be called within the context of a batch transaction.
 */
nsresult HTMLEditor::CollapseAdjacentTextNodes(nsRange& aRange) {
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  // we can't actually do anything during iteration, so store the text nodes in
  // an array first.
  DOMSubtreeIterator subtreeIter;
  if (NS_FAILED(subtreeIter.Init(aRange))) {
    NS_WARNING("DOMSubtreeIterator::Init() failed");
    return NS_ERROR_FAILURE;
  }
  AutoTArray<OwningNonNull<Text>, 8> textNodes;
  subtreeIter.AppendNodesToArray(
      +[](nsINode& aNode, void*) -> bool {
        return EditorUtils::IsEditableContent(*aNode.AsText(),
                                              EditorType::HTML);
      },
      textNodes);

  if (textNodes.Length() < 2) {
    return NS_OK;
  }

  OwningNonNull<Text> leftTextNode = textNodes[0];
  for (size_t rightTextNodeIndex = 1; rightTextNodeIndex < textNodes.Length();
       rightTextNodeIndex++) {
    OwningNonNull<Text>& rightTextNode = textNodes[rightTextNodeIndex];
    // If the leftTextNode has only preformatted line break, keep it as-is.
    if (HTMLEditUtils::TextHasOnlyOnePreformattedLinefeed(leftTextNode)) {
      leftTextNode = rightTextNode;
      continue;
    }
    // If the rightTextNode has only preformatted line break, keep it as-is, and
    // advance the loop next to the rightTextNode.
    if (HTMLEditUtils::TextHasOnlyOnePreformattedLinefeed(rightTextNode)) {
      if (++rightTextNodeIndex == textNodes.Length()) {
        break;
      }
      leftTextNode = textNodes[rightTextNodeIndex];
      continue;
    }
    // If the text nodes are not direct siblings, we shouldn't join them, and
    // we don't need to handle the left one anymore.
    if (leftTextNode->GetNextSibling() != rightTextNode) {
      leftTextNode = rightTextNode;
      continue;
    }
    Result<JoinNodesResult, nsresult> joinNodesResultOrError =
        JoinTextNodesWithNormalizeWhiteSpaces(MOZ_KnownLive(leftTextNode),
                                              MOZ_KnownLive(rightTextNode));
    if (MOZ_UNLIKELY(joinNodesResultOrError.isErr())) {
      NS_WARNING("HTMLEditor::JoinTextNodesWithNormalizeWhiteSpaces() failed");
      return joinNodesResultOrError.unwrapErr();
    }
  }

  return NS_OK;
}

nsresult HTMLEditor::SetSelectionAtDocumentStart() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> rootElement = GetRoot();
  if (NS_WARN_IF(!rootElement)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = CollapseSelectionToStartOf(*rootElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionToStartOf() failed");
  return rv;
}

/**
 * Remove aNode, re-parenting any children into the parent of aNode.  In
 * addition, insert any br's needed to preserve identity of removed block.
 */
Result<EditorDOMPoint, nsresult>
HTMLEditor::RemoveBlockContainerWithTransaction(Element& aElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Two possibilities: the container could be empty of editable content.  If
  // that is the case, we need to compare what is before and after aNode to
  // determine if we need a br.
  //
  // Or it could be not empty, in which case we have to compare previous
  // sibling and first child to determine if we need a leading br, and compare
  // following sibling and last child to determine if we need a trailing br.

  const RefPtr<Element> parentElement = aElement.GetParentElement();
  if (NS_WARN_IF((!parentElement))) {
    return Err(NS_ERROR_FAILURE);
  }
  EditorDOMPoint pointToPutCaret;
  if (HTMLEditUtils::CanNodeContain(*parentElement, *nsGkAtoms::br)) {
    if (const nsCOMPtr<nsIContent> child = HTMLEditUtils::GetFirstChild(
            aElement, {LeafNodeOption::IgnoreNonEditableNode},
            BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      // The case of aNode not being empty.  We need a br at start unless:
      // 1) previous sibling of aNode is a block, OR
      // 2) previous sibling of aNode is a br, OR
      // 3) first child of aNode is a block OR
      // 4) either is null

      if (nsIContent* const previousSibling = HTMLEditUtils::GetPreviousSibling(
              aElement, {LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
        if (!HTMLEditUtils::IsBlockElement(
                *previousSibling,
                BlockInlineCheck::UseComputedDisplayOutsideStyle) &&
            !previousSibling->IsHTMLElement(nsGkAtoms::br) &&
            !HTMLEditUtils::IsBlockElement(
                *child, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
          Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              EditorDOMPoint(&aElement));
          if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                "LineBreakType::BRElement) failed");
            return insertBRElementResultOrError.propagateErr();
          }
          CreateLineBreakResult insertBRElementResult =
              insertBRElementResultOrError.unwrap();
          MOZ_ASSERT(insertBRElementResult.Handled());
          insertBRElementResult.IgnoreCaretPointSuggestion();
          pointToPutCaret = EditorDOMPoint(&aElement, 0);
        }
      }

      // We need a br at end unless:
      // 1) following sibling of aNode is a block, OR
      // 2) last child of aNode is a block, OR
      // 3) last child of aNode is a br OR
      // 4) either is null

      if (nsIContent* const nextSibling = HTMLEditUtils::GetNextSibling(
              aElement, {LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
        if (nextSibling &&
            !HTMLEditUtils::IsBlockElement(
                *nextSibling, BlockInlineCheck::UseComputedDisplayStyle)) {
          if (nsIContent* const lastChild = HTMLEditUtils::GetLastChild(
                  aElement, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
            if (!HTMLEditUtils::IsBlockElement(
                    *lastChild, BlockInlineCheck::UseComputedDisplayStyle) &&
                !lastChild->IsHTMLElement(nsGkAtoms::br)) {
              Result<CreateLineBreakResult, nsresult>
                  insertBRElementResultOrError = InsertLineBreak(
                      WithTransaction::Yes, LineBreakType::BRElement,
                      EditorDOMPoint::After(aElement));
              if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
                NS_WARNING(
                    "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                    "LineBreakType::BRElement) failed");
                return insertBRElementResultOrError.propagateErr();
              }
              CreateLineBreakResult insertBRElementResult =
                  insertBRElementResultOrError.unwrap();
              MOZ_ASSERT(insertBRElementResult.Handled());
              insertBRElementResult.IgnoreCaretPointSuggestion();
              pointToPutCaret = EditorDOMPoint::AtEndOf(aElement);
            }
          }
        }
      }
    } else if (nsIContent* const previousSibling =
                   HTMLEditUtils::GetPreviousSibling(
                       aElement, {LeafNodeOption::IgnoreNonEditableNode},
                       BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      // The case of aNode being empty.  We need a br at start unless:
      // 1) previous sibling of aNode is a block, OR
      // 2) previous sibling of aNode is a br, OR
      // 3) following sibling of aNode is a block, OR
      // 4) following sibling of aNode is a br OR
      // 5) either is null
      if (!HTMLEditUtils::IsBlockElement(
              *previousSibling, BlockInlineCheck::UseComputedDisplayStyle) &&
          !previousSibling->IsHTMLElement(nsGkAtoms::br)) {
        if (nsIContent* nextSibling = HTMLEditUtils::GetNextSibling(
                aElement, {LeafNodeOption::IgnoreNonEditableNode},
                BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
          if (!HTMLEditUtils::IsBlockElement(
                  *nextSibling, BlockInlineCheck::UseComputedDisplayStyle) &&
              !nextSibling->IsHTMLElement(nsGkAtoms::br)) {
            Result<CreateLineBreakResult, nsresult>
                insertBRElementResultOrError = InsertLineBreak(
                    WithTransaction::Yes, LineBreakType::BRElement,
                    EditorDOMPoint(&aElement));
            if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
              NS_WARNING(
                  "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                  "LineBreakType::BRElement) failed");
              return insertBRElementResultOrError.propagateErr();
            }
            CreateLineBreakResult insertBRElementResult =
                insertBRElementResultOrError.unwrap();
            MOZ_ASSERT(insertBRElementResult.Handled());
            insertBRElementResult.IgnoreCaretPointSuggestion();
            pointToPutCaret = EditorDOMPoint(&aElement, 0);
          }
        }
      }
    }
  }

  // Now remove container
  AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
  Result<EditorDOMPoint, nsresult> unwrapBlockElementResult =
      RemoveContainerWithTransaction(aElement);
  if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
    NS_WARNING("HTMLEditor::RemoveContainerWithTransaction() failed");
    return unwrapBlockElementResult;
  }
  trackPointToPutCaret.Flush(StopTracking::Yes);
  if (AllowsTransactionsToChangeSelection() &&
      unwrapBlockElementResult.inspect().IsSet()) {
    pointToPutCaret = unwrapBlockElementResult.unwrap();
  }
  return pointToPutCaret;  // May be unset
}

Result<SplitNodeResult, nsresult> HTMLEditor::SplitNodeWithTransaction(
    const EditorDOMPoint& aStartOfRightNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aStartOfRightNode.IsInContentNode())) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  MOZ_ASSERT(aStartOfRightNode.IsSetAndValid());

  if (NS_WARN_IF(!HTMLEditUtils::IsSplittableNode(
          *aStartOfRightNode.ContainerAs<nsIContent>()))) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSplitNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  RefPtr<SplitNodeTransaction> transaction =
      SplitNodeTransaction::Create(*this, aStartOfRightNode);
  nsresult rv = DoTransactionInternal(transaction);
  if (NS_WARN_IF(Destroyed())) {
    NS_WARNING(
        "EditorBase::DoTransactionInternal() caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DoTransactionInternal() failed");
    return Err(rv);
  }

  nsIContent* newContent = transaction->GetNewContent();
  nsIContent* splitContent = transaction->GetSplitContent();
  if (NS_WARN_IF(!newContent) || NS_WARN_IF(!splitContent)) {
    return Err(NS_ERROR_FAILURE);
  }
  TopLevelEditSubActionDataRef().DidSplitContent(*this, *splitContent,
                                                 *newContent);
  if (NS_WARN_IF(!newContent->IsInComposedDoc()) ||
      NS_WARN_IF(!splitContent->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  return SplitNodeResult(*newContent, *splitContent);
}

Result<SplitNodeResult, nsresult> HTMLEditor::SplitNodeDeepWithTransaction(
    nsIContent& aMostAncestorToSplit,
    const EditorDOMPoint& aDeepestStartOfRightNode,
    SplitAtEdges aSplitAtEdges) {
  MOZ_ASSERT(aDeepestStartOfRightNode.IsSetAndValidInComposedDoc());
  MOZ_ASSERT(
      aDeepestStartOfRightNode.GetContainer() == &aMostAncestorToSplit ||
      EditorUtils::IsDescendantOf(*aDeepestStartOfRightNode.GetContainer(),
                                  aMostAncestorToSplit));

  if (NS_WARN_IF(!aDeepestStartOfRightNode.IsInComposedDoc())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  nsCOMPtr<nsIContent> newLeftNodeOfMostAncestor;
  EditorDOMPoint atStartOfRightNode(aDeepestStartOfRightNode);
  // lastResult is as explained by its name, the last result which may not be
  // split a node actually.
  SplitNodeResult lastResult = SplitNodeResult::NotHandled(atStartOfRightNode);
  MOZ_ASSERT(lastResult.AtSplitPoint<EditorRawDOMPoint>()
                 .IsSetAndValidInComposedDoc());

  while (true) {
    // Need to insert rules code call here to do things like not split a list
    // if you are after the last <li> or before the first, etc.  For now we
    // just have some smarts about unnecessarily splitting text nodes, which
    // should be universal enough to put straight in this EditorBase routine.
    auto* splittingContent = atStartOfRightNode.GetContainerAs<nsIContent>();
    if (NS_WARN_IF(!splittingContent)) {
      lastResult.IgnoreCaretPointSuggestion();
      return Err(NS_ERROR_FAILURE);
    }
    // If we meet an orphan node before meeting aMostAncestorToSplit, we need
    // to stop splitting.  This is a bug of the caller.
    if (NS_WARN_IF(splittingContent != &aMostAncestorToSplit &&
                   !atStartOfRightNode.GetContainerParentAs<nsIContent>())) {
      lastResult.IgnoreCaretPointSuggestion();
      return Err(NS_ERROR_FAILURE);
    }
    // If the container is not splitable node such as comment node, atomic
    // element, etc, we should keep it as-is, and try to split its parents.
    if (!HTMLEditUtils::IsSplittableNode(*splittingContent)) {
      if (splittingContent == &aMostAncestorToSplit) {
        return lastResult;
      }
      atStartOfRightNode.Set(splittingContent);
      continue;
    }

    // If the split point is middle of the node or the node is not a text node
    // and we're allowed to create empty element node, split it.
    if ((aSplitAtEdges == SplitAtEdges::eAllowToCreateEmptyContainer &&
         !atStartOfRightNode.IsInTextNode()) ||
        (!atStartOfRightNode.IsStartOfContainer() &&
         !atStartOfRightNode.IsEndOfContainer())) {
      Result<SplitNodeResult, nsresult> splitNodeResult =
          SplitNodeWithTransaction(atStartOfRightNode);
      if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
        lastResult.IgnoreCaretPointSuggestion();
        return splitNodeResult;
      }
      lastResult = SplitNodeResult::MergeWithDeeperSplitNodeResult(
          splitNodeResult.unwrap(), lastResult);
      if (NS_WARN_IF(!lastResult.AtSplitPoint<EditorRawDOMPoint>()
                          .IsInComposedDoc())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      MOZ_ASSERT(lastResult.HasCaretPointSuggestion());
      MOZ_ASSERT(lastResult.GetOriginalContent() == splittingContent);
      if (splittingContent == &aMostAncestorToSplit) {
        // Actually, we split aMostAncestorToSplit.
        return lastResult;
      }

      // Then, try to split its parent before current node.
      atStartOfRightNode = lastResult.AtNextContent<EditorDOMPoint>();
    }
    // If the split point is end of the node and it is a text node or we're not
    // allowed to create empty container node, try to split its parent after it.
    else if (!atStartOfRightNode.IsStartOfContainer()) {
      lastResult = SplitNodeResult::HandledButDidNotSplitDueToEndOfContainer(
          *splittingContent, &lastResult);
      MOZ_ASSERT(lastResult.AtSplitPoint<EditorRawDOMPoint>()
                     .IsSetAndValidInComposedDoc());
      if (splittingContent == &aMostAncestorToSplit) {
        return lastResult;
      }

      // Try to split its parent after current node.
      atStartOfRightNode.SetAfter(splittingContent);
    }
    // If the split point is start of the node and it is a text node or we're
    // not allowed to create empty container node, try to split its parent.
    else {
      if (splittingContent == &aMostAncestorToSplit) {
        return SplitNodeResult::HandledButDidNotSplitDueToStartOfContainer(
            *splittingContent, &lastResult);
      }

      // Try to split its parent before current node.
      // XXX This is logically wrong.  If we've already split something but
      //     this is the last splitable content node in the limiter, this
      //     method will return "not handled".
      lastResult = SplitNodeResult::NotHandled(atStartOfRightNode, &lastResult);
      MOZ_ASSERT(lastResult.AtSplitPoint<EditorRawDOMPoint>()
                     .IsSetAndValidInComposedDoc());
      atStartOfRightNode.Set(splittingContent);
      MOZ_ASSERT(atStartOfRightNode.IsSetAndValidInComposedDoc());
    }
  }

  // Not reached because while (true) loop never breaks.
}

Result<SplitNodeResult, nsresult> HTMLEditor::DoSplitNode(
    const EditorDOMPoint& aStartOfRightNode, nsIContent& aNewNode) {
  // Ensure computing the offset if it's initialized with a child content node.
  (void)aStartOfRightNode.Offset();

  // XXX Perhaps, aStartOfRightNode may be invalid if this is a redo
  //     operation after modifying DOM node with JS.
  if (NS_WARN_IF(!aStartOfRightNode.IsInContentNode())) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  MOZ_DIAGNOSTIC_ASSERT(aStartOfRightNode.IsSetAndValid());

  // Remember all selection points.
  AutoTArray<SavedRange, 10> savedRanges;
  for (SelectionType selectionType : kPresentSelectionTypes) {
    SavedRange savingRange;
    savingRange.mSelection = GetSelection(selectionType);
    if (NS_WARN_IF(!savingRange.mSelection &&
                   selectionType == SelectionType::eNormal)) {
      return Err(NS_ERROR_FAILURE);
    }
    if (!savingRange.mSelection) {
      // For non-normal selections, skip over the non-existing ones.
      continue;
    }

    for (uint32_t j : IntegerRange(savingRange.mSelection->RangeCount())) {
      const nsRange* r = savingRange.mSelection->GetRangeAt(j);
      MOZ_ASSERT(r);
      MOZ_ASSERT(r->IsPositioned());
      // XXX Looks like that SavedRange should have mStart and mEnd which
      //     are RangeBoundary.  Then, we can avoid to compute offset here.
      savingRange.mStartContainer = r->GetStartContainer();
      savingRange.mStartOffset = r->StartOffset();
      savingRange.mEndContainer = r->GetEndContainer();
      savingRange.mEndOffset = r->EndOffset();

      savedRanges.AppendElement(savingRange);
    }
  }

  const nsCOMPtr<nsINode> containerParentNode =
      aStartOfRightNode.GetContainerParent();
  if (NS_WARN_IF(!containerParentNode)) {
    return Err(NS_ERROR_FAILURE);
  }

  // For the performance of IMEContentObserver, we should move all data into
  // aNewNode first because IMEContentObserver needs to compute moved content
  // length only once when aNewNode is connected.

  // If we are splitting a text node, we need to move its some data to the
  // new text node.
  MOZ_DIAGNOSTIC_ASSERT_IF(aStartOfRightNode.IsInTextNode(), aNewNode.IsText());
  MOZ_DIAGNOSTIC_ASSERT_IF(!aStartOfRightNode.IsInTextNode(),
                           !aNewNode.IsText());
  const nsCOMPtr<nsIContent> firstChildOfRightNode =
      aStartOfRightNode.GetChild();
  nsresult rv = [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT {
    if (aStartOfRightNode.IsEndOfContainer()) {
      return NS_OK;  // No content which should be moved into aNewNode.
    }
    if (aStartOfRightNode.IsInTextNode()) {
      Text* originalTextNode = aStartOfRightNode.ContainerAs<Text>();
      Text* newTextNode = aNewNode.AsText();
      nsAutoString movingText;
      const uint32_t cutStartOffset = aStartOfRightNode.Offset();
      const uint32_t cutLength =
          originalTextNode->Length() - aStartOfRightNode.Offset();
      IgnoredErrorResult error;
      originalTextNode->SubstringData(cutStartOffset, cutLength, movingText,
                                      error);
      NS_WARNING_ASSERTION(!error.Failed(),
                           "Text::SubstringData() failed, but ignored");
      error.SuppressException();

      // XXX This call may destroy us.
      DoDeleteText(MOZ_KnownLive(*originalTextNode), cutStartOffset, cutLength,
                   error);
      NS_WARNING_ASSERTION(!error.Failed(),
                           "EditorBase::DoDeleteText() failed, but ignored");
      error.SuppressException();

      // XXX This call may destroy us.
      DoSetText(MOZ_KnownLive(*newTextNode), movingText, error);
      NS_WARNING_ASSERTION(!error.Failed(),
                           "EditorBase::DoSetText() failed, but ignored");
      return NS_OK;
    }

    // If the right node is new one and splitting at start of the container,
    // we need to move all children to the new right node.
    if (!firstChildOfRightNode->GetPreviousSibling()) {
      // XXX Why do we ignore an error while moving nodes from the right
      //     node to the left node?
      nsresult rv = MoveAllChildren(
          // MOZ_KnownLive because aStartOfRightNode grabs the container.
          MOZ_KnownLive(*aStartOfRightNode.GetContainer()),
          EditorRawDOMPoint(&aNewNode, 0u));
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "HTMLEditor::MoveAllChildren() failed");
      return rv;
    }

    // If the right node is new one and splitting at middle of the node, we need
    // to move inclusive next siblings of the split point to the new right node.
    // XXX Why do we ignore an error while moving nodes from the right node
    //     to the left node?
    nsresult rv = MoveInclusiveNextSiblings(*firstChildOfRightNode,
                                            EditorRawDOMPoint(&aNewNode, 0u));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::MoveInclusiveNextSiblings() failed");
    return rv;
  }();

  // To avoid a dataloss bug, we should try to insert aNewNode even if we've
  // already been destroyed.
  if (NS_WARN_IF(!aStartOfRightNode.GetContainerParent())) {
    return NS_WARN_IF(Destroyed()) ? Err(NS_ERROR_EDITOR_DESTROYED)
                                   : Err(NS_ERROR_FAILURE);
  }

  // Finally, we should insert aNewNode which already has proper data or
  // children.
  {
    const nsCOMPtr<nsIContent> nextSibling =
        aStartOfRightNode.GetContainer()->GetNextSibling();
    AutoNodeAPIWrapper nodeWrapper(*this, *containerParentNode);
    nsresult rv = nodeWrapper.InsertBefore(aNewNode, nextSibling);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoNodeAPIWrapper::InsertBefore() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(
        nodeWrapper.IsExpectedResult(),
        "Inserting new node caused other mutations, but ignored");
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("Moving children from left node to right node failed");
    return Err(rv);
  }

  // Handle selection
  // TODO: Stop doing this, this shouldn't be necessary to update selection.
  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->FlushPendingNotifications(FlushType::Frames);
  }
  NS_WARNING_ASSERTION(!Destroyed(),
                       "The editor is destroyed during splitting a node");

  const bool allowedTransactionsToChangeSelection =
      AllowsTransactionsToChangeSelection();

  IgnoredErrorResult error;
  RefPtr<Selection> previousSelection;
  for (SavedRange& savedRange : savedRanges) {
    // If we have not seen the selection yet, clear all of its ranges.
    if (savedRange.mSelection != previousSelection) {
      MOZ_KnownLive(savedRange.mSelection)->RemoveAllRanges(error);
      if (MOZ_UNLIKELY(error.Failed())) {
        NS_WARNING("Selection::RemoveAllRanges() failed");
        return Err(error.StealNSResult());
      }
      previousSelection = savedRange.mSelection;
    }

    // XXX Looks like that we don't need to modify normal selection here
    //     because selection will be modified by the caller if
    //     AllowsTransactionsToChangeSelection() will return true.
    if (allowedTransactionsToChangeSelection &&
        savedRange.mSelection->Type() == SelectionType::eNormal) {
      // If the editor should adjust the selection, don't bother restoring
      // the ranges for the normal selection here.
      continue;
    }

    auto AdjustDOMPoint = [&](nsCOMPtr<nsINode>& aContainer,
                              uint32_t& aOffset) {
      if (aContainer != aStartOfRightNode.GetContainer()) {
        return;
      }

      // If the container is the left node and offset is after the split
      // point, the content was moved from the right node to aNewNode.
      // So, we need to change the container to aNewNode and decrease the
      // offset.
      if (aOffset >= aStartOfRightNode.Offset()) {
        aContainer = &aNewNode;
        aOffset -= aStartOfRightNode.Offset();
      }
    };
    AdjustDOMPoint(savedRange.mStartContainer, savedRange.mStartOffset);
    AdjustDOMPoint(savedRange.mEndContainer, savedRange.mEndOffset);

    RefPtr<nsRange> newRange =
        nsRange::Create(savedRange.mStartContainer, savedRange.mStartOffset,
                        savedRange.mEndContainer, savedRange.mEndOffset, error);
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING("nsRange::Create() failed");
      return Err(error.StealNSResult());
    }
    // The `MOZ_KnownLive` annotation is only necessary because of a bug
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1622253) in the
    // static analyzer.
    MOZ_KnownLive(savedRange.mSelection)
        ->AddRangeAndSelectFramesAndNotifyListeners(*newRange, error);
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING(
          "Selection::AddRangeAndSelectFramesAndNotifyListeners() failed");
      return Err(error.StealNSResult());
    }
  }

  // We don't need to set selection here because the caller should do that
  // in any case.

  // If splitting the node causes running mutation event listener and we've
  // got unexpected result, we should return error because callers will
  // continue to do their work without complicated DOM tree result.
  // NOTE: Perhaps, we shouldn't do this immediately after each DOM tree change
  //       because stopping handling it causes some data loss.  E.g., user
  //       may loose the text which is moved to the new text node.
  // XXX We cannot check all descendants in the right node and the new left
  //     node for performance reason.  I think that if caller needs to access
  //     some of the descendants, they should check by themselves.
  if (NS_WARN_IF(containerParentNode !=
                 aStartOfRightNode.GetContainer()->GetParentNode()) ||
      NS_WARN_IF(containerParentNode != aNewNode.GetParentNode()) ||
      NS_WARN_IF(aNewNode.GetPreviousSibling() !=
                 aStartOfRightNode.GetContainer())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  DebugOnly<nsresult> rvIgnored = RangeUpdaterRef().SelAdjSplitNode(
      *aStartOfRightNode.ContainerAs<nsIContent>(), aStartOfRightNode.Offset(),
      aNewNode);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "RangeUpdater::SelAdjSplitNode() failed, but ignored");

  return SplitNodeResult(aNewNode,
                         *aStartOfRightNode.ContainerAs<nsIContent>());
}

Result<JoinNodesResult, nsresult> HTMLEditor::JoinNodesWithTransaction(
    nsIContent& aLeftContent, nsIContent& aRightContent) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(&aLeftContent != &aRightContent);
  MOZ_ASSERT(aLeftContent.GetParentNode());
  MOZ_ASSERT(aRightContent.GetParentNode());
  MOZ_ASSERT(aLeftContent.GetParentNode() == aRightContent.GetParentNode());

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eJoinNodes, nsIEditor::ePrevious, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  if (NS_WARN_IF(!aRightContent.GetParentNode())) {
    return Err(NS_ERROR_FAILURE);
  }

  RefPtr<JoinNodesTransaction> transaction =
      JoinNodesTransaction::MaybeCreate(*this, aLeftContent, aRightContent);
  if (MOZ_UNLIKELY(!transaction)) {
    NS_WARNING("JoinNodesTransaction::MaybeCreate() failed");
    return Err(NS_ERROR_FAILURE);
  }

  const nsresult rv = DoTransactionInternal(transaction);
  // FYI: Now, DidJoinNodesTransaction() must have been run if succeeded.
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  // This shouldn't occur unless the cycle collector runs by chrome script
  // forcibly.
  if (NS_WARN_IF(!transaction->GetRemovedContent()) ||
      NS_WARN_IF(!transaction->GetExistingContent())) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  // If joined node is moved to different place, offset may not have any
  // meaning.  In this case, the web app modified the DOM tree takes on the
  // responsibility for the remaning things.
  if (NS_WARN_IF(transaction->GetExistingContent()->GetParent() !=
                 transaction->GetParentNode())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DoTransactionInternal() failed");
    return Err(rv);
  }

  return JoinNodesResult(transaction->CreateJoinedPoint<EditorDOMPoint>(),
                         *transaction->GetRemovedContent());
}

void HTMLEditor::DidJoinNodesTransaction(
    const JoinNodesTransaction& aTransaction, nsresult aDoJoinNodesResult) {
  // This shouldn't occur unless the cycle collector runs by chrome script
  // forcibly.
  if (MOZ_UNLIKELY(NS_WARN_IF(!aTransaction.GetRemovedContent()) ||
                   NS_WARN_IF(!aTransaction.GetExistingContent()))) {
    return;
  }

  // If joined node is moved to different place, offset may not have any
  // meaning.  In this case, the web app modified the DOM tree takes on the
  // responsibility for the remaning things.
  if (MOZ_UNLIKELY(aTransaction.GetExistingContent()->GetParentNode() !=
                   aTransaction.GetParentNode())) {
    return;
  }

  // Be aware, the joined point should be created for each call because
  // they may refer the child node, but some of them may change the DOM tree
  // after that, thus we need to avoid invalid point (Although it shouldn't
  // occur).
  TopLevelEditSubActionDataRef().DidJoinContents(
      *this, aTransaction.CreateJoinedPoint<EditorRawDOMPoint>());

  if (NS_SUCCEEDED(aDoJoinNodesResult)) {
    if (RefPtr<TextServicesDocument> textServicesDocument =
            mTextServicesDocument) {
      textServicesDocument->DidJoinContents(
          aTransaction.CreateJoinedPoint<EditorRawDOMPoint>(),
          *aTransaction.GetRemovedContent());
    }
  }

  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored = listener->DidJoinContents(
          aTransaction.CreateJoinedPoint<EditorRawDOMPoint>(),
          aTransaction.GetRemovedContent());
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidJoinContents() failed, but ignored");
    }
  }
}

nsresult HTMLEditor::DoJoinNodes(nsIContent& aContentToKeep,
                                 nsIContent& aContentToRemove) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  const uint32_t keepingContentLength = aContentToKeep.Length();
  const EditorDOMPoint oldPointAtRightContent(&aContentToRemove);
  if (MOZ_LIKELY(oldPointAtRightContent.IsSet())) {
    (void)oldPointAtRightContent.Offset();  // Fix the offset
  }

  // Remember all selection points.
  // XXX Do we need to restore all types of selections by ourselves?  Normal
  //     selection should be modified later as result of handling edit action.
  //     IME selections shouldn't be there when nodes are joined.  Spellcheck
  //     selections should be recreated with newer text.  URL selections
  //     shouldn't be there because of used only by the URL bar.
  AutoTArray<SavedRange, 10> savedRanges;
  {
    EditorRawDOMPoint atRemovingNode(&aContentToRemove);
    EditorRawDOMPoint atNodeToKeep(&aContentToKeep);
    for (SelectionType selectionType : kPresentSelectionTypes) {
      SavedRange savingRange;
      savingRange.mSelection = GetSelection(selectionType);
      if (selectionType == SelectionType::eNormal) {
        if (NS_WARN_IF(!savingRange.mSelection)) {
          return NS_ERROR_FAILURE;
        }
      } else if (!savingRange.mSelection) {
        // For non-normal selections, skip over the non-existing ones.
        continue;
      }

      const uint32_t rangeCount = savingRange.mSelection->RangeCount();
      for (const uint32_t j : IntegerRange(rangeCount)) {
        MOZ_ASSERT(savingRange.mSelection->RangeCount() == rangeCount);
        const RefPtr<nsRange> r = savingRange.mSelection->GetRangeAt(j);
        MOZ_ASSERT(r);
        MOZ_ASSERT(r->IsPositioned());
        savingRange.mStartContainer = r->GetStartContainer();
        savingRange.mStartOffset = r->StartOffset();
        savingRange.mEndContainer = r->GetEndContainer();
        savingRange.mEndOffset = r->EndOffset();

        // If selection endpoint is between the nodes, remember it as being
        // in the one that is going away instead.  This simplifies later
        // selection adjustment logic at end of this method.
        if (savingRange.mStartContainer) {
          MOZ_ASSERT(savingRange.mEndContainer);
          auto AdjustDOMPoint = [&](nsCOMPtr<nsINode>& aContainer,
                                    uint32_t& aOffset) {
            // If range boundary points aContentToRemove and aContentToKeep is
            // its left node, remember it as being at end of aContentToKeep.
            // Then, it will point start of the first content of moved content
            // from aContentToRemove.
            if (aContainer == atRemovingNode.GetContainer() &&
                atNodeToKeep.Offset() < aOffset &&
                aOffset <= atRemovingNode.Offset()) {
              aContainer = &aContentToKeep;
              aOffset = keepingContentLength;
            }
          };
          AdjustDOMPoint(savingRange.mStartContainer, savingRange.mStartOffset);
          AdjustDOMPoint(savingRange.mEndContainer, savingRange.mEndOffset);
        }

        savedRanges.AppendElement(savingRange);
      }
    }
  }

  // OK, ready to do join now.
  nsresult rv = [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT {
    // If it's a text node, just shuffle around some text.
    if (aContentToKeep.IsText() && aContentToRemove.IsText()) {
      nsAutoString rightText;
      aContentToRemove.AsText()->GetData(rightText);
      // Delete the node first to minimize the text change range from
      // IMEContentObserver of view.
      {
        AutoNodeAPIWrapper nodeWrapper(*this, aContentToRemove);
        if (NS_FAILED(nodeWrapper.Remove())) {
          NS_WARNING("AutoNodeAPIWrapper::Remove() failed, but ignored");
        } else {
          NS_WARNING_ASSERTION(
              nodeWrapper.IsExpectedResult(),
              "Deleting node caused other mutations, but ignored");
        }
      }
      // Even if we've already destroyed, let's update aContentToKeep for
      // avoiding a dataloss bug.
      IgnoredErrorResult ignoredError;
      DoInsertText(MOZ_KnownLive(*aContentToKeep.AsText()),
                   aContentToKeep.AsText()->TextDataLength(), rightText,
                   ignoredError);
      if (NS_WARN_IF(Destroyed())) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "EditorBase::DoSetText() failed, but ignored");
      return NS_OK;
    }
    // Otherwise it's an interior node, so shuffle around the children.
    AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfChildContents;
    HTMLEditUtils::CollectAllChildren(aContentToRemove, arrayOfChildContents);
    // Delete the node first to minimize the text change range from
    // IMEContentObserver of view.
    {
      AutoNodeAPIWrapper nodeWrapper(*this, aContentToRemove);
      if (NS_FAILED(nodeWrapper.Remove())) {
        NS_WARNING("AutoNodeAPIWrapper::Remove() failed, but ignored");
      } else {
        NS_WARNING_ASSERTION(
            nodeWrapper.IsExpectedResult(),
            "Deleting node caused other mutations, but ignored");
      }
    }
    // Even if we've already destroyed, let's update aContentToKeep for avoiding
    // a dataloss bug.
    nsresult rv = NS_OK;
    for (const OwningNonNull<nsIContent>& child : arrayOfChildContents) {
      AutoNodeAPIWrapper nodeWrapper(*this, aContentToKeep);
      nsresult rvInner = nodeWrapper.AppendChild(MOZ_KnownLive(child));
      if (NS_FAILED(rvInner)) {
        NS_WARNING("AutoNodeAPIWrapper::AppendChild() failed");
        rv = rvInner;
      } else {
        NS_WARNING_ASSERTION(
            nodeWrapper.IsExpectedResult(),
            "Appending child caused other mutations, but ignored");
      }
    }
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    return rv;
  }();

  if (MOZ_LIKELY(oldPointAtRightContent.IsSet())) {
    DebugOnly<nsresult> rvIgnored = RangeUpdaterRef().SelAdjJoinNodes(
        EditorRawDOMPoint(&aContentToKeep, std::min(keepingContentLength,
                                                    aContentToKeep.Length())),
        aContentToRemove, oldPointAtRightContent);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "RangeUpdater::SelAdjJoinNodes() failed, but ignored");
  }
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return rv;
  }

  const bool allowedTransactionsToChangeSelection =
      AllowsTransactionsToChangeSelection();

  // And adjust the selection if needed.
  RefPtr<Selection> previousSelection;
  for (SavedRange& savedRange : savedRanges) {
    // If we have not seen the selection yet, clear all of its ranges.
    if (savedRange.mSelection != previousSelection) {
      IgnoredErrorResult error;
      MOZ_KnownLive(savedRange.mSelection)->RemoveAllRanges(error);
      if (NS_WARN_IF(Destroyed())) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      if (error.Failed()) {
        NS_WARNING("Selection::RemoveAllRanges() failed");
        return error.StealNSResult();
      }
      previousSelection = savedRange.mSelection;
    }

    if (allowedTransactionsToChangeSelection &&
        savedRange.mSelection->Type() == SelectionType::eNormal) {
      // If the editor should adjust the selection, don't bother restoring
      // the ranges for the normal selection here.
      continue;
    }

    auto AdjustDOMPoint = [&](nsCOMPtr<nsINode>& aContainer,
                              uint32_t& aOffset) {
      // Now, all content of aContentToRemove are moved to end of
      // aContentToKeep.  Therefore, if a range boundary was in
      // aContentToRemove, we need to change the container to aContentToKeep and
      // adjust the offset to after the original content of aContentToKeep.
      if (aContainer == &aContentToRemove) {
        aContainer = &aContentToKeep;
        aOffset += keepingContentLength;
      }
    };
    AdjustDOMPoint(savedRange.mStartContainer, savedRange.mStartOffset);
    AdjustDOMPoint(savedRange.mEndContainer, savedRange.mEndOffset);

    const RefPtr<nsRange> newRange = nsRange::Create(
        savedRange.mStartContainer, savedRange.mStartOffset,
        savedRange.mEndContainer, savedRange.mEndOffset, IgnoreErrors());
    if (!newRange) {
      NS_WARNING("nsRange::Create() failed");
      return NS_ERROR_FAILURE;
    }

    IgnoredErrorResult error;
    // The `MOZ_KnownLive` annotation is only necessary because of a bug
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1622253) in the
    // static analyzer.
    MOZ_KnownLive(savedRange.mSelection)
        ->AddRangeAndSelectFramesAndNotifyListeners(*newRange, error);
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
  }

  if (allowedTransactionsToChangeSelection) {
    // Editor wants us to set selection at join point.
    DebugOnly<nsresult> rvIgnored = CollapseSelectionToStartOf(aContentToKeep);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionTo() caused destroying the editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBases::CollapseSelectionTos() failed, but ignored");
  }

  return NS_OK;
}

Result<MoveNodeResult, nsresult> HTMLEditor::MoveNodeWithTransaction(
    nsIContent& aContentToMove, const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  EditorDOMPoint oldPoint(&aContentToMove);
  if (NS_WARN_IF(!oldPoint.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  // Don't do anything if it's already in right place.
  if (aPointToInsert == oldPoint) {
    return MoveNodeResult::IgnoredResult(aPointToInsert.NextPoint());
  }

  RefPtr<MoveNodeTransaction> moveNodeTransaction =
      MoveNodeTransaction::MaybeCreate(*this, aContentToMove, aPointToInsert);
  if (MOZ_UNLIKELY(!moveNodeTransaction)) {
    NS_WARNING("MoveNodeTransaction::MaybeCreate() failed");
    return Err(NS_ERROR_FAILURE);
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eMoveNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  TopLevelEditSubActionDataRef().WillDeleteContent(*this, aContentToMove);

  nsresult rv = DoTransactionInternal(moveNodeTransaction);
  if (NS_SUCCEEDED(rv)) {
    if (mTextServicesDocument) {
      const OwningNonNull<TextServicesDocument> textServicesDocument =
          *mTextServicesDocument;
      textServicesDocument->DidDeleteContent(aContentToMove);
    }
  }

  if (!mActionListeners.IsEmpty()) {
    for (auto& listener : mActionListeners.Clone()) {
      DebugOnly<nsresult> rvIgnored =
          listener->DidDeleteNode(&aContentToMove, rv);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "nsIEditActionListener::DidDeleteNode() failed, but ignored");
    }
  }

  if (MOZ_UNLIKELY(Destroyed())) {
    NS_WARNING(
        "MoveNodeTransaction::DoTransaction() caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("MoveNodeTransaction::DoTransaction() failed");
    return Err(rv);
  }

  TopLevelEditSubActionDataRef().DidInsertContent(*this, aContentToMove);

  return MoveNodeResult::HandledResult(
      moveNodeTransaction->SuggestNextInsertionPoint().To<EditorDOMPoint>(),
      moveNodeTransaction->SuggestPointToPutCaret().To<EditorDOMPoint>());
}

Result<MoveNodeResult, nsresult> HTMLEditor::MoveSiblingsWithTransaction(
    nsIContent& aFirstContentToMove, nsIContent& aLastContentToMove,
    const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());
  MOZ_ASSERT(aFirstContentToMove.GetParentNode() ==
             aLastContentToMove.GetParentNode());
  if (&aFirstContentToMove == &aLastContentToMove) {
    Result<MoveNodeResult, nsresult> moveNodeResultOrError =
        MoveNodeWithTransaction(aFirstContentToMove, aPointToInsert);
    NS_WARNING_ASSERTION(moveNodeResultOrError.isOk(),
                         "HTMLEditor::MoveNodeWithTransaction() failed");
    return moveNodeResultOrError;
  }

  MOZ_ASSERT(*aFirstContentToMove.ComputeIndexInParentNode() <
             *aLastContentToMove.ComputeIndexInParentNode());

  // Don't do anything if it's already in right place.
  {
    const EditorDOMPoint atFirstContent(&aFirstContentToMove);
    if (NS_WARN_IF(!atFirstContent.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    const EditorDOMPoint atLastContent(&aLastContentToMove);
    if (NS_WARN_IF(!atLastContent.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (aPointToInsert.GetContainer() == atFirstContent.GetContainer() &&
        atFirstContent.EqualsOrIsBefore(aPointToInsert) &&
        aPointToInsert.EqualsOrIsBefore(
            atLastContent.NextPoint<EditorRawDOMPoint>())) {
      return MoveNodeResult::IgnoredResult(atLastContent.NextPoint());
    }
  }

  const RefPtr<MoveSiblingsTransaction> moveSiblingsTransaction =
      MoveSiblingsTransaction::MaybeCreate(*this, aFirstContentToMove,
                                           aLastContentToMove, aPointToInsert);
  if (MOZ_UNLIKELY(!moveSiblingsTransaction)) {
    NS_WARNING("MoveNodeTransaction::MaybeCreate() failed");
    return Err(NS_ERROR_FAILURE);
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eMoveNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // WillDeleteContent will extend the changed range to contain all points where
  // the moved nodes were.  Therefore, we need to call it only for the first and
  // and the last one.
  TopLevelEditSubActionDataRef().WillDeleteContent(*this, aFirstContentToMove);
  TopLevelEditSubActionDataRef().WillDeleteContent(*this, aLastContentToMove);

  nsresult rv = DoTransactionInternal(moveSiblingsTransaction);
  Maybe<CopyableAutoTArray<OwningNonNull<nsIContent>, 64>> movedSiblings;
  if (NS_SUCCEEDED(rv)) {
    if (mTextServicesDocument) {
      movedSiblings.emplace(moveSiblingsTransaction->TargetSiblings());
      const OwningNonNull<TextServicesDocument> textServicesDocument =
          *mTextServicesDocument;
      for (const OwningNonNull<nsIContent>& movedContent :
           movedSiblings.ref()) {
        textServicesDocument->DidDeleteContent(MOZ_KnownLive(*movedContent));
      }
    }
  }

  if (!mActionListeners.IsEmpty()) {
    if (!movedSiblings) {
      movedSiblings.emplace(moveSiblingsTransaction->TargetSiblings());
    }
    for (auto& listener : mActionListeners.Clone()) {
      for (const OwningNonNull<nsIContent>& movedContent :
           movedSiblings.ref()) {
        DebugOnly<nsresult> rvIgnored =
            listener->DidDeleteNode(MOZ_KnownLive(movedContent), rv);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsIEditActionListener::DidDeleteNode() failed, but ignored");
      }
    }
  }

  if (MOZ_UNLIKELY(Destroyed())) {
    NS_WARNING(
        "MoveNodeTransaction::DoTransaction() caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("MoveNodeTransaction::DoTransaction() failed");
    return Err(rv);
  }

  nsIContent* const firstMovedContentInExpectedContainer =
      moveSiblingsTransaction->GetFirstMovedContent();
  nsIContent* const lastMovedContentInExpectedContainer =
      moveSiblingsTransaction->GetLastMovedContent();
  if (!firstMovedContentInExpectedContainer) {
    return MoveNodeResult::IgnoredResult(aPointToInsert);
  }
  MOZ_ASSERT(lastMovedContentInExpectedContainer);

  // DidInsertContent will extend the changed range to contain all moved
  // contents.  Therefore, we need to call it only for the first and and the
  // last one.
  TopLevelEditSubActionDataRef().DidInsertContent(
      *this, *firstMovedContentInExpectedContainer);
  if (firstMovedContentInExpectedContainer ==
      lastMovedContentInExpectedContainer) {
    // Only one node was moved.
    return MoveNodeResult::HandledResult(
        moveSiblingsTransaction->SuggestNextInsertionPoint()
            .To<EditorDOMPoint>(),
        moveSiblingsTransaction->SuggestPointToPutCaret().To<EditorDOMPoint>());
  }
  TopLevelEditSubActionDataRef().DidInsertContent(
      *this, *lastMovedContentInExpectedContainer);
  return MoveNodeResult::HandledResult(
      *firstMovedContentInExpectedContainer,
      moveSiblingsTransaction->SuggestNextInsertionPoint().To<EditorDOMPoint>(),
      moveSiblingsTransaction->SuggestPointToPutCaret().To<EditorDOMPoint>());
}

Result<RefPtr<Element>, nsresult> HTMLEditor::DeleteSelectionAndCreateElement(
    nsAtom& aTag, const InitializeInsertingElement& aInitializer) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsresult rv = DeleteSelectionAndPrepareToCreateNode();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::DeleteSelectionAndPrepareToCreateNode() failed");
    return Err(rv);
  }

  EditorDOMPoint pointToInsert(SelectionRef().AnchorRef());
  if (!pointToInsert.IsSet()) {
    return Err(NS_ERROR_FAILURE);
  }
  Result<CreateElementResult, nsresult> createNewElementResult =
      CreateAndInsertElement(WithTransaction::Yes, aTag, pointToInsert,
                             aInitializer);
  if (MOZ_UNLIKELY(createNewElementResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
    return createNewElementResult.propagateErr();
  }
  MOZ_ASSERT(createNewElementResult.inspect().GetNewNode());

  // We want the selection to be just after the new node
  createNewElementResult.inspect().IgnoreCaretPointSuggestion();
  rv = CollapseSelectionTo(
      EditorRawDOMPoint::After(*createNewElementResult.inspect().GetNewNode()));
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionTo() failed");
    return Err(rv);
  }
  return createNewElementResult.unwrap().UnwrapNewNode();
}

nsresult HTMLEditor::DeleteSelectionAndPrepareToCreateNode() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!SelectionRef().GetAnchorFocusRange())) {
    return NS_OK;
  }

  if (!SelectionRef().GetAnchorFocusRange()->Collapsed()) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteSelectionAsSubAction() failed");
      return rv;
    }
    MOZ_ASSERT(SelectionRef().GetAnchorFocusRange() &&
                   SelectionRef().GetAnchorFocusRange()->Collapsed(),
               "Selection not collapsed after delete");
  }

  // If the selection is a chardata node, split it if necessary and compute
  // where to put the new node
  EditorDOMPoint atAnchor(SelectionRef().AnchorRef());
  if (NS_WARN_IF(!atAnchor.IsSet()) || !atAnchor.IsInDataNode()) {
    return NS_OK;
  }

  if (NS_WARN_IF(!atAnchor.GetContainerParent())) {
    return NS_ERROR_FAILURE;
  }

  if (atAnchor.IsStartOfContainer()) {
    const EditorRawDOMPoint atAnchorContainer(atAnchor.GetContainer());
    if (NS_WARN_IF(!atAnchorContainer.IsSetAndValid())) {
      return NS_ERROR_FAILURE;
    }
    nsresult rv = CollapseSelectionTo(atAnchorContainer);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionTo() failed");
    return rv;
  }

  if (atAnchor.IsEndOfContainer()) {
    EditorRawDOMPoint afterAnchorContainer(atAnchor.GetContainer());
    if (NS_WARN_IF(!afterAnchorContainer.AdvanceOffset())) {
      return NS_ERROR_FAILURE;
    }
    nsresult rv = CollapseSelectionTo(afterAnchorContainer);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionTo() failed");
    return rv;
  }

  Result<SplitNodeResult, nsresult> splitAtAnchorResult =
      SplitNodeWithTransaction(atAnchor);
  if (MOZ_UNLIKELY(splitAtAnchorResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
    return splitAtAnchorResult.unwrapErr();
  }

  splitAtAnchorResult.inspect().IgnoreCaretPointSuggestion();
  const auto atRightContent =
      splitAtAnchorResult.inspect().AtNextContent<EditorRawDOMPoint>();
  if (NS_WARN_IF(!atRightContent.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(atRightContent.IsSetAndValid());
  nsresult rv = CollapseSelectionTo(atRightContent);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

bool HTMLEditor::IsEmpty() const {
  if (mPaddingBRElementForEmptyEditor) {
    return true;
  }

  const Element* activeElement =
      GetDocument() ? GetDocument()->GetActiveElement() : nullptr;
  const Element* editingHostOrBodyOrRootElement =
      activeElement && activeElement->IsEditable()
          ? ComputeEditingHost(*activeElement, LimitInBodyElement::No)
          : ComputeEditingHost(LimitInBodyElement::No);
  if (MOZ_UNLIKELY(!editingHostOrBodyOrRootElement)) {
    // If there is no active element nor no selection range in the document,
    // let's check entire the document as what we do traditionally.
    editingHostOrBodyOrRootElement = GetRoot();
    if (!editingHostOrBodyOrRootElement) {
      return true;
    }
  }

  for (nsIContent* childContent =
           editingHostOrBodyOrRootElement->GetFirstChild();
       childContent; childContent = childContent->GetNextSibling()) {
    if (!childContent->IsText() || childContent->Length()) {
      return false;
    }
  }
  return true;
}

// add to aElement the CSS inline styles corresponding to the HTML attribute
// aAttribute with its value aValue
nsresult HTMLEditor::SetAttributeOrEquivalent(Element* aElement,
                                              nsAtom* aAttribute,
                                              const nsAString& aValue,
                                              bool aSuppressTransaction) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(aAttribute);

  nsAutoScriptBlocker scriptBlocker;
  nsStyledElement* styledElement = nsStyledElement::FromNodeOrNull(aElement);
  if (!IsCSSEnabled()) {
    // we are not in an HTML+CSS editor; let's set the attribute the HTML way
    if (EditorElementStyle::IsHTMLStyle(aAttribute)) {
      const EditorElementStyle elementStyle =
          EditorElementStyle::Create(*aAttribute);
      if (styledElement && elementStyle.IsCSSRemovable(*styledElement)) {
        // MOZ_KnownLive(*styledElement): It's aElement and its lifetime must
        // be guaranteed by the caller because of MOZ_CAN_RUN_SCRIPT method.
        nsresult rv = CSSEditUtils::RemoveCSSEquivalentToStyle(
            aSuppressTransaction ? WithTransaction::No : WithTransaction::Yes,
            *this, MOZ_KnownLive(*styledElement), elementStyle, nullptr);
        if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
          return NS_ERROR_EDITOR_DESTROYED;
        }
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "CSSEditUtils::RemoveCSSEquivalentToStyle() failed, but ignored");
      }
    }
    if (aSuppressTransaction) {
      AutoElementAttrAPIWrapper elementWrapper(*this, *aElement);
      nsresult rv = elementWrapper.SetAttr(aAttribute, aValue, true);
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoElementAttrAPIWrapper::SetAttr() failed");
        return rv;
      }
      NS_WARNING_ASSERTION(
          elementWrapper.IsExpectedResult(aValue),
          "Setting attribute caused other mutations, but ignored");
      return NS_OK;
    }
    nsresult rv = SetAttributeWithTransaction(*aElement, *aAttribute, aValue);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::SetAttributeWithTransaction() failed");
    return rv;
  }

  if (EditorElementStyle::IsHTMLStyle(aAttribute)) {
    const EditorElementStyle elementStyle =
        EditorElementStyle::Create(*aAttribute);
    if (styledElement && elementStyle.IsCSSSettable(*styledElement)) {
      // MOZ_KnownLive(*styledElement): It's aElement and its lifetime must
      // be guaranteed by the caller because of MOZ_CAN_RUN_SCRIPT method.
      Result<size_t, nsresult> count = CSSEditUtils::SetCSSEquivalentToStyle(
          aSuppressTransaction ? WithTransaction::No : WithTransaction::Yes,
          *this, MOZ_KnownLive(*styledElement), elementStyle, &aValue);
      if (MOZ_UNLIKELY(count.isErr())) {
        if (NS_WARN_IF(count.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
          return NS_ERROR_EDITOR_DESTROYED;
        }
        NS_WARNING(
            "CSSEditUtils::SetCSSEquivalentToStyle() failed, but ignored");
      }
      if (count.inspect()) {
        // we found an equivalence ; let's remove the HTML attribute itself if
        // it is set
        nsAutoString existingValue;
        if (!aElement->GetAttr(aAttribute, existingValue)) {
          return NS_OK;
        }

        if (aSuppressTransaction) {
          AutoElementAttrAPIWrapper elementWrapper(*this, *aElement);
          nsresult rv = elementWrapper.UnsetAttr(aAttribute, true);
          if (NS_FAILED(rv)) {
            NS_WARNING("AutoElementAttrAPIWrapper::UnsetAttr() failed");
            return rv;
          }
          NS_WARNING_ASSERTION(
              elementWrapper.IsExpectedResult(EmptyString()),
              "Removing attribute caused other mutations, but ignored");
          return NS_OK;
        }
        nsresult rv = RemoveAttributeWithTransaction(*aElement, *aAttribute);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "EditorBase::RemoveAttributeWithTransaction() failed");
        return rv;
      }
    }
  }

  // count is an integer that represents the number of CSS declarations
  // applied to the element. If it is zero, we found no equivalence in this
  // implementation for the attribute
  if (aAttribute == nsGkAtoms::style) {
    // if it is the style attribute, just add the new value to the existing
    // style attribute's value
    nsString existingValue;  // Use nsString to avoid copying the string
                             // buffer at setting the attribute below.
    aElement->GetAttr(nsGkAtoms::style, existingValue);
    if (!existingValue.IsEmpty()) {
      existingValue.Append(HTMLEditUtils::kSpace);
    }
    existingValue.Append(aValue);
    if (aSuppressTransaction) {
      AutoElementAttrAPIWrapper elementWrapper(*this, *aElement);
      nsresult rv =
          elementWrapper.SetAttr(nsGkAtoms::style, existingValue, true);
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoElementAttrAPIWrapper::SetAttr() failed");
        return rv;
      }
      NS_WARNING_ASSERTION(
          elementWrapper.IsExpectedResult(existingValue),
          "Setting style attribute caused other mutations, but ignored");
      return NS_OK;
    }
    nsresult rv = SetAttributeWithTransaction(*aElement, *nsGkAtoms::style,
                                              existingValue);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::SetAttributeWithTransaction(nsGkAtoms::style) failed");
    return rv;
  }

  // we have no CSS equivalence for this attribute and it is not the style
  // attribute; let's set it the good'n'old HTML way
  if (aSuppressTransaction) {
    AutoElementAttrAPIWrapper elementWrapper(*this, *aElement);
    nsresult rv = elementWrapper.SetAttr(aAttribute, aValue, true);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoElementAttrAPIWrapper::SetAttr() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(
        elementWrapper.IsExpectedResult(aValue),
        "Setting attribute caused other mutations, but ignored");
    return NS_OK;
  }
  nsresult rv = SetAttributeWithTransaction(*aElement, *aAttribute, aValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::SetAttributeWithTransaction() failed");
  return rv;
}

nsresult HTMLEditor::RemoveAttributeOrEquivalent(Element* aElement,
                                                 nsAtom* aAttribute,
                                                 bool aSuppressTransaction) {
  MOZ_ASSERT(aElement);
  MOZ_ASSERT(aAttribute);

  if (IsCSSEnabled() && EditorElementStyle::IsHTMLStyle(aAttribute)) {
    const EditorElementStyle elementStyle =
        EditorElementStyle::Create(*aAttribute);
    if (elementStyle.IsCSSRemovable(*aElement)) {
      // XXX It might be keep handling attribute even if aElement is not
      //     an nsStyledElement instance.
      nsStyledElement* styledElement =
          nsStyledElement::FromNodeOrNull(aElement);
      if (NS_WARN_IF(!styledElement)) {
        return NS_ERROR_INVALID_ARG;
      }
      // MOZ_KnownLive(*styledElement): It's aElement and its lifetime must
      // be guaranteed by the caller because of MOZ_CAN_RUN_SCRIPT method.
      nsresult rv = CSSEditUtils::RemoveCSSEquivalentToStyle(
          aSuppressTransaction ? WithTransaction::No : WithTransaction::Yes,
          *this, MOZ_KnownLive(*styledElement), elementStyle, nullptr);
      if (NS_FAILED(rv)) {
        NS_WARNING("CSSEditUtils::RemoveCSSEquivalentToStyle() failed");
        return rv;
      }
    }
  }

  if (!aElement->HasAttr(aAttribute)) {
    return NS_OK;
  }

  if (aSuppressTransaction) {
    AutoElementAttrAPIWrapper elementWrapper(*this, *aElement);
    nsresult rv = elementWrapper.UnsetAttr(aAttribute, true);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoElementAttrAPIWrapper::UnsetAttr() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(
        elementWrapper.IsExpectedResult(EmptyString()),
        "Removing attribute caused other mutations, but ignored");
    return NS_OK;
  }
  nsresult rv = RemoveAttributeWithTransaction(*aElement, *aAttribute);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::RemoveAttributeWithTransaction() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::SetIsCSSEnabled(bool aIsCSSPrefChecked) {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eEnableOrDisableCSS);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mIsCSSPrefChecked = aIsCSSPrefChecked;
  return NS_OK;
}

// Set the block background color
nsresult HTMLEditor::SetBlockBackgroundColorWithCSSAsSubAction(
    const nsAString& aColor) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // background-color change and committing composition should be undone
  // together
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  CommitComposition();

  // XXX Shouldn't we do this before calling `CommitComposition()`?
  if (IsPlaintextMailComposer()) {
    return NS_OK;
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertElement, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(!ignoredError.Failed(),
                       "HTMLEditor::OnStartToHandleTopLevelEditSubAction() "
                       "failed, but ignored");

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  MOZ_ALWAYS_TRUE(selectionRanges.SaveAndTrackRanges(*this));
  for (const OwningNonNull<nsRange>& domRange : selectionRanges.Ranges()) {
    EditorDOMRange range(domRange);
    if (NS_WARN_IF(!range.IsPositioned())) {
      continue;
    }

    if (range.InSameContainer()) {
      // If the range is in a text node, set background color of its parent
      // block.
      if (range.StartRef().IsInTextNode()) {
        const RefPtr<nsStyledElement> editableBlockStyledElement =
            nsStyledElement::FromNodeOrNull(HTMLEditUtils::GetAncestorElement(
                *range.StartRef().ContainerAs<Text>(),
                HTMLEditUtils::ClosestEditableBlockElement,
                BlockInlineCheck::UseComputedDisplayOutsideStyle));
        if (!editableBlockStyledElement ||
            !EditorElementStyle::BGColor().IsCSSSettable(
                *editableBlockStyledElement)) {
          continue;
        }
        Result<size_t, nsresult> result = CSSEditUtils::SetCSSEquivalentToStyle(
            WithTransaction::Yes, *this, *editableBlockStyledElement,
            EditorElementStyle::BGColor(), &aColor);
        if (MOZ_UNLIKELY(result.isErr())) {
          if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
            return NS_ERROR_EDITOR_DESTROYED;
          }
          NS_WARNING(
              "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
              "BGColor()) failed, but ignored");
        }
        continue;
      }

      // If `Selection` is collapsed in a `<body>` element, set background
      // color of the `<body>` element.
      if (range.Collapsed() &&
          range.StartRef().IsContainerHTMLElement(nsGkAtoms::body)) {
        const RefPtr<nsStyledElement> styledElement =
            range.StartRef().GetContainerAs<nsStyledElement>();
        if (!styledElement ||
            !EditorElementStyle::BGColor().IsCSSSettable(*styledElement)) {
          continue;
        }
        Result<size_t, nsresult> result = CSSEditUtils::SetCSSEquivalentToStyle(
            WithTransaction::Yes, *this, *styledElement,
            EditorElementStyle::BGColor(), &aColor);
        if (MOZ_UNLIKELY(result.isErr())) {
          if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
            return NS_ERROR_EDITOR_DESTROYED;
          }
          NS_WARNING(
              "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
              "BGColor()) failed, but ignored");
        }
        continue;
      }

      // If one node is selected, set background color of it if it's a
      // block, or of its parent block otherwise.
      if ((range.StartRef().IsStartOfContainer() &&
           range.EndRef().IsStartOfContainer()) ||
          range.StartRef().Offset() + 1 == range.EndRef().Offset()) {
        if (NS_WARN_IF(range.StartRef().IsInDataNode())) {
          continue;
        }
        const RefPtr<nsStyledElement> editableBlockStyledElement =
            nsStyledElement::FromNodeOrNull(
                HTMLEditUtils::GetInclusiveAncestorElement(
                    *range.StartRef().GetChild(),
                    HTMLEditUtils::ClosestEditableBlockElement,
                    BlockInlineCheck::UseComputedDisplayOutsideStyle));
        if (!editableBlockStyledElement ||
            !EditorElementStyle::BGColor().IsCSSSettable(
                *editableBlockStyledElement)) {
          continue;
        }
        Result<size_t, nsresult> result = CSSEditUtils::SetCSSEquivalentToStyle(
            WithTransaction::Yes, *this, *editableBlockStyledElement,
            EditorElementStyle::BGColor(), &aColor);
        if (MOZ_UNLIKELY(result.isErr())) {
          if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
            return NS_ERROR_EDITOR_DESTROYED;
          }
          NS_WARNING(
              "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
              "BGColor()) failed, but ignored");
        }
        continue;
      }
    }  // if (range.InSameContainer())

    // Collect editable nodes which are entirely contained in the range.
    AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
    {
      ContentSubtreeIterator subtreeIter;
      // If there is no node which is entirely in the range,
      // `ContentSubtreeIterator::Init()` fails, but this is possible case,
      // don't warn it.
      nsresult rv = subtreeIter.Init(range.StartRef().ToRawRangeBoundary(),
                                     range.EndRef().ToRawRangeBoundary());
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "ContentSubtreeIterator::Init() failed, but ignored");
      if (NS_SUCCEEDED(rv)) {
        for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
          nsINode* node = subtreeIter.GetCurrentNode();
          if (NS_WARN_IF(!node)) {
            return NS_ERROR_FAILURE;
          }
          if (node->IsContent() && EditorUtils::IsEditableContent(
                                       *node->AsContent(), EditorType::HTML)) {
            arrayOfContents.AppendElement(*node->AsContent());
          }
        }
      }
    }

    // This caches block parent if we set its background color.
    RefPtr<Element> handledBlockParent;

    // If start node is a text node, set background color of its parent
    // block.
    if (range.StartRef().IsInTextNode() &&
        EditorUtils::IsEditableContent(*range.StartRef().ContainerAs<Text>(),
                                       EditorType::HTML)) {
      Element* const editableBlockElement = HTMLEditUtils::GetAncestorElement(
          *range.StartRef().ContainerAs<Text>(),
          HTMLEditUtils::ClosestEditableBlockElement,
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (editableBlockElement && handledBlockParent != editableBlockElement) {
        handledBlockParent = editableBlockElement;
        nsStyledElement* const blockStyledElement =
            nsStyledElement::FromNode(handledBlockParent);
        if (blockStyledElement &&
            EditorElementStyle::BGColor().IsCSSSettable(*blockStyledElement)) {
          // MOZ_KnownLive(*blockStyledElement): It's handledBlockParent
          // whose type is RefPtr.
          Result<size_t, nsresult> result =
              CSSEditUtils::SetCSSEquivalentToStyle(
                  WithTransaction::Yes, *this,
                  MOZ_KnownLive(*blockStyledElement),
                  EditorElementStyle::BGColor(), &aColor);
          if (MOZ_UNLIKELY(result.isErr())) {
            if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
              return NS_ERROR_EDITOR_DESTROYED;
            }
            NS_WARNING(
                "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
                "BGColor()) failed, but ignored");
          }
        }
      }
    }

    // Then, set background color of each block or block parent of all nodes
    // in the range entirely.
    for (OwningNonNull<nsIContent>& content : arrayOfContents) {
      Element* const editableBlockElement =
          HTMLEditUtils::GetInclusiveAncestorElement(
              content, HTMLEditUtils::ClosestEditableBlockElement,
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (editableBlockElement && handledBlockParent != editableBlockElement) {
        handledBlockParent = editableBlockElement;
        nsStyledElement* const blockStyledElement =
            nsStyledElement::FromNode(handledBlockParent);
        if (blockStyledElement &&
            EditorElementStyle::BGColor().IsCSSSettable(*blockStyledElement)) {
          // MOZ_KnownLive(*blockStyledElement): It's handledBlockParent whose
          // type is RefPtr.
          Result<size_t, nsresult> result =
              CSSEditUtils::SetCSSEquivalentToStyle(
                  WithTransaction::Yes, *this,
                  MOZ_KnownLive(*blockStyledElement),
                  EditorElementStyle::BGColor(), &aColor);
          if (MOZ_UNLIKELY(result.isErr())) {
            if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
              return NS_ERROR_EDITOR_DESTROYED;
            }
            NS_WARNING(
                "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
                "BGColor()) failed, but ignored");
          }
        }
      }
    }

    // Finally, if end node is a text node, set background color of its
    // parent block.
    if (range.EndRef().IsInTextNode() &&
        EditorUtils::IsEditableContent(*range.EndRef().ContainerAs<Text>(),
                                       EditorType::HTML)) {
      Element* const editableBlockElement = HTMLEditUtils::GetAncestorElement(
          *range.EndRef().ContainerAs<Text>(),
          HTMLEditUtils::ClosestEditableBlockElement,
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (editableBlockElement && handledBlockParent != editableBlockElement) {
        const RefPtr<nsStyledElement> blockStyledElement =
            nsStyledElement::FromNode(editableBlockElement);
        if (blockStyledElement &&
            EditorElementStyle::BGColor().IsCSSSettable(*blockStyledElement)) {
          Result<size_t, nsresult> result =
              CSSEditUtils::SetCSSEquivalentToStyle(
                  WithTransaction::Yes, *this, *blockStyledElement,
                  EditorElementStyle::BGColor(), &aColor);
          if (MOZ_UNLIKELY(result.isErr())) {
            if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
              return NS_ERROR_EDITOR_DESTROYED;
            }
            NS_WARNING(
                "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
                "BGColor()) failed, but ignored");
          }
        }
      }
    }
  }  // for-loop of selectionRanges

  MOZ_ASSERT(selectionRanges.HasSavedRanges());
  selectionRanges.RestoreFromSavedRanges();
  nsresult rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedSelectionRangeArray::ApplyTo() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::SetBackgroundColor(const nsAString& aColor) {
  nsresult rv = SetBackgroundColorAsAction(aColor);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetBackgroundColorAsAction() failed");
  return rv;
}

nsresult HTMLEditor::SetBackgroundColorAsAction(const nsAString& aColor,
                                                nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this, EditAction::eSetBackgroundColor, aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (IsCSSEnabled()) {
    // if we are in CSS mode, we have to apply the background color to the
    // containing block (or the body if we have no block-level element in
    // the document)
    nsresult rv = SetBlockBackgroundColorWithCSSAsSubAction(aColor);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::SetBlockBackgroundColorWithCSSAsSubAction() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // but in HTML mode, we can only set the document's background color
  rv = SetHTMLBackgroundColorWithTransaction(aColor);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::SetHTMLBackgroundColorWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

Result<EditorDOMPoint, nsresult>
HTMLEditor::CopyLastEditableChildStylesWithTransaction(
    Element& aPreviousBlock, Element& aNewBlock, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // First, clear out aNewBlock.  Contract is that we want only the styles
  // from aPreviousBlock.
  AutoTArray<OwningNonNull<nsIContent>, 32> newBlockChildren;
  HTMLEditUtils::CollectAllChildren(aNewBlock, newBlockChildren);
  for (const OwningNonNull<nsIContent>& child : newBlockChildren) {
    // MOZ_KNownLive(child) because of bug 1622253
    nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(child));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }
  if (MOZ_UNLIKELY(aNewBlock.GetFirstChild())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // XXX aNewBlock may be moved or removed.  Even in such case, we should
  //     keep cloning the styles?

  // Look for the deepest last editable leaf node in aPreviousBlock.
  // Then, if found one is a <br> element, look for non-<br> element.
  nsIContent* deepestEditableContent = HTMLEditUtils::GetPreviousLeafContent(
      EditorRawDOMPoint::AtEndOf(aPreviousBlock),
      {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  while (deepestEditableContent &&
         deepestEditableContent->IsHTMLElement(nsGkAtoms::br)) {
    deepestEditableContent = HTMLEditUtils::GetPreviousLeafContent(
        *deepestEditableContent, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle, &aEditingHost);
  }
  if (!deepestEditableContent) {
    return EditorDOMPoint(&aNewBlock, 0u);
  }

  Element* deepestVisibleEditableElement =
      deepestEditableContent->GetAsElementOrParentElement();
  if (!deepestVisibleEditableElement) {
    return EditorDOMPoint(&aNewBlock, 0u);
  }

  // Clone inline elements to keep current style in the new block.
  // XXX Looks like that this is really slow if lastEditableDescendant is
  //     far from aPreviousBlock.  Probably, we should clone inline containers
  //     from ancestor to descendants without transactions, then, insert it
  //     after that with transaction.
  RefPtr<Element> lastClonedElement, firstClonedElement;
  for (RefPtr<Element> elementInPreviousBlock = deepestVisibleEditableElement;
       elementInPreviousBlock && elementInPreviousBlock != &aPreviousBlock;
       elementInPreviousBlock = elementInPreviousBlock->GetParentElement()) {
    if (!HTMLEditUtils::IsInlineStyleElement(*elementInPreviousBlock) &&
        !elementInPreviousBlock->IsHTMLElement(nsGkAtoms::span)) {
      continue;
    }
    OwningNonNull<nsAtom> tagName =
        *elementInPreviousBlock->NodeInfo()->NameAtom();
    // At first time, just create the most descendant inline container
    // element.
    if (!firstClonedElement) {
      Result<CreateElementResult, nsresult> createNewElementResult =
          CreateAndInsertElement(
              WithTransaction::Yes, tagName, EditorDOMPoint(&aNewBlock, 0u),
              // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
              [&elementInPreviousBlock](
                  HTMLEditor& aHTMLEditor, Element& aNewElement,
                  const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
                // Clone all attributes.  Note that despite the method name,
                // CloneAttributesWithTransaction does not create
                // transactions in this case because aNewElement has not
                // been connected yet.
                // XXX Looks like that this clones id attribute too.
                aHTMLEditor.CloneAttributesWithTransaction(
                    aNewElement, *elementInPreviousBlock);
                return NS_OK;
              });
      if (MOZ_UNLIKELY(createNewElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
        return createNewElementResult.propagateErr();
      }
      CreateElementResult unwrappedCreateNewElementResult =
          createNewElementResult.unwrap();
      // We'll return with a point suggesting new caret position and the
      // following path does not require an update of selection here.
      // Therefore, we don't need to update selection here.
      unwrappedCreateNewElementResult.IgnoreCaretPointSuggestion();
      firstClonedElement = lastClonedElement =
          unwrappedCreateNewElementResult.UnwrapNewNode();
      continue;
    }
    // Otherwise, inserts new parent inline container to the previous inserted
    // inline container.
    Result<CreateElementResult, nsresult> wrapClonedElementResult =
        InsertContainerWithTransaction(*lastClonedElement, tagName);
    if (MOZ_UNLIKELY(wrapClonedElementResult.isErr())) {
      NS_WARNING("HTMLEditor::InsertContainerWithTransaction() failed");
      return wrapClonedElementResult.propagateErr();
    }
    CreateElementResult unwrappedWrapClonedElementResult =
        wrapClonedElementResult.unwrap();
    // We'll return with a point suggesting new caret so that we don't need to
    // update selection here.
    unwrappedWrapClonedElementResult.IgnoreCaretPointSuggestion();
    MOZ_ASSERT(unwrappedWrapClonedElementResult.GetNewNode());
    lastClonedElement = unwrappedWrapClonedElementResult.UnwrapNewNode();
    CloneAttributesWithTransaction(*lastClonedElement, *elementInPreviousBlock);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
  }

  if (!firstClonedElement) {
    // XXX Even if no inline elements are cloned, shouldn't we create new
    //     <br> element for aNewBlock?
    return EditorDOMPoint(&aNewBlock, 0u);
  }

  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      EditorDOMPoint(firstClonedElement, 0u));
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertBRElementResultOrError.propagateErr();
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  MOZ_ASSERT(insertBRElementResult.Handled());
  insertBRElementResult.IgnoreCaretPointSuggestion();
  return insertBRElementResult.AtLineBreak<EditorDOMPoint>();
}

nsresult HTMLEditor::GetElementOrigin(Element& aElement, int32_t& aX,
                                      int32_t& aY) {
  aX = 0;
  aY = 0;

  if (NS_WARN_IF(!IsInitialized())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  PresShell* presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsIFrame* frame = aElement.GetPrimaryFrame();
  if (NS_WARN_IF(!frame)) {
    return NS_OK;
  }

  nsIFrame* absoluteContainerBlockFrame =
      presShell->GetAbsoluteContainingBlock(frame);
  if (NS_WARN_IF(!absoluteContainerBlockFrame)) {
    return NS_OK;
  }
  nsPoint off = frame->GetOffsetTo(absoluteContainerBlockFrame);
  aX = nsPresContext::AppUnitsToIntCSSPixels(off.x);
  aY = nsPresContext::AppUnitsToIntCSSPixels(off.y);

  return NS_OK;
}

Element* HTMLEditor::GetSelectionContainerElement() const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsINode* focusNode = nullptr;
  if (SelectionRef().IsCollapsed()) {
    focusNode = SelectionRef().GetFocusNode();
    if (NS_WARN_IF(!focusNode)) {
      return nullptr;
    }
  } else {
    const uint32_t rangeCount = SelectionRef().RangeCount();
    MOZ_ASSERT(rangeCount, "If 0, Selection::IsCollapsed() should return true");

    if (rangeCount == 1) {
      const nsRange* range = SelectionRef().GetRangeAt(0);

      const RangeBoundary& startRef = range->StartRef();
      const RangeBoundary& endRef = range->EndRef();

      // This method called GetSelectedElement() to retrieve proper container
      // when only one node is selected.  However, it simply returns start
      // node of Selection with additional cost.  So, we do not need to call
      // it anymore.
      if (startRef.GetContainer()->IsElement() &&
          startRef.GetContainer() == endRef.GetContainer() &&
          startRef.GetChildAtOffset() &&
          startRef.GetChildAtOffset()->GetNextSibling() ==
              endRef.GetChildAtOffset()) {
        focusNode = startRef.GetChildAtOffset();
        MOZ_ASSERT(focusNode, "Start container must not be nullptr");
      } else {
        focusNode = range->GetClosestCommonInclusiveAncestor();
        if (!focusNode) {
          NS_WARNING(
              "AbstractRange::GetClosestCommonInclusiveAncestor() returned "
              "nullptr");
          return nullptr;
        }
      }
    } else {
      for (const uint32_t i : IntegerRange(rangeCount)) {
        MOZ_ASSERT(SelectionRef().RangeCount() == rangeCount);
        const nsRange* range = SelectionRef().GetRangeAt(i);
        MOZ_ASSERT(range);
        nsINode* startContainer = range->GetStartContainer();
        if (!focusNode) {
          focusNode = startContainer;
        } else if (focusNode != startContainer) {
          // XXX Looks odd to use parent of startContainer because previous
          //     range may not be in the parent node of current
          //     startContainer.
          focusNode = startContainer->GetParentNode();
          // XXX Looks odd to break the for-loop here because we refer only
          //     first range and another range which starts from different
          //     container, and the latter range is preferred. Why?
          break;
        }
      }
      if (!focusNode) {
        NS_WARNING("Focused node of selection was not found");
        return nullptr;
      }
    }
  }

  if (focusNode->IsText()) {
    focusNode = focusNode->GetParentNode();
    if (NS_WARN_IF(!focusNode)) {
      return nullptr;
    }
  }

  if (NS_WARN_IF(!focusNode->IsElement())) {
    return nullptr;
  }
  return focusNode->AsElement();
}

NS_IMETHODIMP HTMLEditor::IsAnonymousElement(Element* aElement, bool* aReturn) {
  if (NS_WARN_IF(!aElement)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aReturn = aElement->IsRootOfNativeAnonymousSubtree();
  return NS_OK;
}

nsresult HTMLEditor::SetReturnInParagraphCreatesNewParagraph(
    bool aCreatesNewParagraph) {
  mCRInParagraphCreatesParagraph = aCreatesNewParagraph;
  return NS_OK;
}

bool HTMLEditor::GetReturnInParagraphCreatesNewParagraph() const {
  return mCRInParagraphCreatesParagraph;
}

nsresult HTMLEditor::GetReturnInParagraphCreatesNewParagraph(
    bool* aCreatesNewParagraph) {
  *aCreatesNewParagraph = mCRInParagraphCreatesParagraph;
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetWrapWidth(int32_t* aWrapColumn) {
  if (NS_WARN_IF(!aWrapColumn)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aWrapColumn = WrapWidth();
  return NS_OK;
}

//
// See if the style value includes this attribute, and if it does,
// cut out everything from the attribute to the next semicolon.
//
static void CutStyle(const char* stylename, nsString& styleValue) {
  // Find the current wrapping type:
  int32_t styleStart = styleValue.LowerCaseFindASCII(stylename);
  if (styleStart >= 0) {
    int32_t styleEnd = styleValue.Find(u";", styleStart);
    if (styleEnd > styleStart) {
      styleValue.Cut(styleStart, styleEnd - styleStart + 1);
    } else {
      styleValue.Cut(styleStart, styleValue.Length() - styleStart);
    }
  }
}

NS_IMETHODIMP HTMLEditor::SetWrapWidth(int32_t aWrapColumn) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSetWrapWidth);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mWrapColumn = aWrapColumn;

  // Make sure we're a plaintext editor, otherwise we shouldn't
  // do the rest of this.
  if (!IsPlaintextMailComposer()) {
    return NS_OK;
  }

  // Ought to set a style sheet here...
  const RefPtr<Element> rootElement = GetRoot();
  if (NS_WARN_IF(!rootElement)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // Get the current style for this root element:
  nsAutoString styleValue;
  rootElement->GetAttr(nsGkAtoms::style, styleValue);

  // We'll replace styles for these values:
  CutStyle("white-space", styleValue);
  CutStyle("width", styleValue);
  CutStyle("font-family", styleValue);

  // If we have other style left, trim off any existing semicolons
  // or white-space, then add a known semicolon-space:
  if (!styleValue.IsEmpty()) {
    styleValue.Trim("; \t", false, true);
    styleValue.AppendLiteral("; ");
  }

  // Make sure we have fixed-width font.  This should be done for us,
  // but it isn't, see bug 22502, so we have to add "font: -moz-fixed;".
  // Only do this if we're wrapping.
  if (IsWrapHackEnabled() && aWrapColumn >= 0) {
    styleValue.AppendLiteral("font-family: -moz-fixed; ");
  }

  // and now we're ready to set the new white-space/wrapping style.
  if (aWrapColumn > 0) {
    // Wrap to a fixed column.
    styleValue.AppendLiteral("white-space: pre-wrap; width: ");
    styleValue.AppendInt(aWrapColumn);
    styleValue.AppendLiteral("ch;");
  } else if (!aWrapColumn) {
    styleValue.AppendLiteral("white-space: pre-wrap;");
  } else {
    styleValue.AppendLiteral("white-space: pre;");
  }

  AutoElementAttrAPIWrapper elementWrapper(*this, *rootElement);
  nsresult rv = elementWrapper.SetAttr(nsGkAtoms::style, styleValue, true);
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoElementAttrAPIWrapper::SetAttr() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(
      elementWrapper.IsExpectedResult(styleValue),
      "Setting style attribute caused other mutations, but ignored");
  return NS_OK;
}

Element* HTMLEditor::GetFocusedElement() const {
  Element* const focusedElement = nsFocusManager::GetFocusedElementStatic();

  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }
  const bool inDesignMode = focusedElement ? focusedElement->IsInDesignMode()
                                           : document->IsInDesignMode();
  if (!focusedElement) {
    // in designMode, nobody gets focus in most cases.
    if (inDesignMode && OurWindowHasFocus()) {
      return document->GetRootElement();
    }
    return nullptr;
  }

  if (inDesignMode) {
    return OurWindowHasFocus() &&
                   focusedElement->IsInclusiveDescendantOf(document)
               ? focusedElement
               : nullptr;
  }

  // We're HTML editor for contenteditable

  // If the focused content isn't editable, or it has independent selection,
  // we don't have focus.
  if (!focusedElement->HasFlag(NODE_IS_EDITABLE) ||
      focusedElement->HasIndependentSelection()) {
    return nullptr;
  }
  // If our window is focused, we're focused.
  return OurWindowHasFocus() ? focusedElement : nullptr;
}

bool HTMLEditor::IsActiveInDOMWindow() const {
  nsFocusManager* focusManager = nsFocusManager::GetFocusManager();
  if (NS_WARN_IF(!focusManager)) {
    return false;
  }

  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return false;
  }

  // If we're in designMode, we're always active in the DOM window.
  if (IsInDesignMode()) {
    return true;
  }

  nsPIDOMWindowOuter* ourWindow = document->GetWindow();
  nsCOMPtr<nsPIDOMWindowOuter> win;
  nsIContent* content = nsFocusManager::GetFocusedDescendant(
      ourWindow, nsFocusManager::eOnlyCurrentWindow, getter_AddRefs(win));
  if (!content) {
    return false;
  }

  if (content->IsInDesignMode()) {
    return true;
  }

  // We're HTML editor for contenteditable

  // If the active content isn't editable, or it has independent selection,
  // we're not active).
  if (!content->HasFlag(NODE_IS_EDITABLE) ||
      content->HasIndependentSelection()) {
    return false;
  }
  return true;
}

Element* HTMLEditor::ComputeEditingHostInternal(
    const nsIContent* aContent, LimitInBodyElement aLimitInBodyElement) const {
  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }

  auto MaybeLimitInBodyElement =
      [&](const Element* aCandidateEditingHost) -> Element* {
    if (!aCandidateEditingHost) {
      return nullptr;
    }
    if (aLimitInBodyElement != LimitInBodyElement::Yes) {
      return const_cast<Element*>(aCandidateEditingHost);
    }
    auto* body = document->GetBodyElement();
    // By default, we should limit editing host to the <body> element for
    // avoiding deleting or creating unexpected elements outside the <body>.
    // However, this is incompatible with Chrome so that we should stop
    // doing this with adding safety checks more.
    if (body && nsContentUtils::ContentIsFlattenedTreeDescendantOf(
                    aCandidateEditingHost, body)) {
      return const_cast<Element*>(aCandidateEditingHost);
    }
    // XXX If aContent is an editing host and has no parent node, we reach here,
    //     but returning the <body> which is not connected to aContent is odd.
    return body && body->IsEditable() ? body : nullptr;
  };

  // We're HTML editor for contenteditable
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return nullptr;
  }

  const nsIContent* const content = [&]() -> const nsIContent* {
    if (aContent) {
      return aContent;
    }
    // If there are selection ranges, let's look for their common ancestor's
    // editing host because selection ranges may be visible for users.
    nsIContent* selectionCommonAncestor = nullptr;
    for (uint32_t i : IntegerRange(SelectionRef().RangeCount())) {
      nsRange* range = SelectionRef().GetRangeAt(i);
      MOZ_ASSERT(range);
      nsIContent* commonAncestor =
          nsIContent::FromNodeOrNull(range->GetCommonAncestorContainer(
              IgnoreErrors(), AllowRangeCrossShadowBoundary::Yes));
      if (MOZ_UNLIKELY(!commonAncestor)) {
        continue;
      }
      if (!selectionCommonAncestor) {
        selectionCommonAncestor = commonAncestor;
      } else {
        selectionCommonAncestor = nsIContent::FromNodeOrNull(
            nsContentUtils::GetCommonFlattenedTreeAncestorForSelection(
                commonAncestor, selectionCommonAncestor));
      }
    }
    if (selectionCommonAncestor) {
      return selectionCommonAncestor;
    }
    // Otherwise, let's use the focused element in the window.
    nsPIDOMWindowInner* const innerWindow = document->GetInnerWindow();
    if (MOZ_UNLIKELY(!innerWindow)) {
      return nullptr;
    }
    if (Element* focusedElementInWindow = innerWindow->GetFocusedElement()) {
      if (focusedElementInWindow->ChromeOnlyAccess()) {
        focusedElementInWindow = Element::FromNodeOrNull(
            // XXX Should we use
            // nsIContent::FindFirstNonChromeOnlyAccessContent() instead of
            // nsINode::GetClosestNativeAnonymousSubtreeRootParentOrHost()?
            focusedElementInWindow
                ->GetClosestNativeAnonymousSubtreeRootParentOrHost());
      }
      if (focusedElementInWindow) {
        return focusedElementInWindow->IsEditable() ? focusedElementInWindow
                                                    : nullptr;
      }
    }
    // If there is no focused element and the document is in the design mode,
    // let's return the <body>.
    if (document->IsInDesignMode()) {
      auto* body = document->GetBodyElement();
      // return null if body has contenteditable=false
      return body && body->IsEditable() ? body : nullptr;
    }
    // Otherwise, we cannot find the editing host...
    return nullptr;
  }();
  if (!content && document->IsInDesignMode()) {
    // FIXME: There may be no <body>.  In such case and aLimitInBodyElement is
    // "No", we should use root element instead.
    auto* body = document->GetBodyElement();
    // return null if body has contenteditable=false
    return body && body->IsEditable() ? body : nullptr;
  }

  if (NS_WARN_IF(!content)) {
    return nullptr;
  }

  // If the active content isn't editable, we're not active.
  if (!content->HasFlag(NODE_IS_EDITABLE)) {
    return nullptr;
  }

  // Although the content shouldn't be in a native anonymous subtree, but
  // perhaps due to a bug of Selection or Range API, it may occur.  HTMLEditor
  // shouldn't touch native anonymous subtree so that return nullptr in such
  // case.
  if (MOZ_UNLIKELY(content->IsInNativeAnonymousSubtree())) {
    return nullptr;
  }

  // Note that `Selection` can be in <input> or <textarea>.  In the case, we
  // need to look for an ancestor which does not have editable parent.
  return MaybeLimitInBodyElement(
      const_cast<nsIContent*>(content)->GetEditingHost());
}

void HTMLEditor::NotifyEditingHostMaybeChanged() {
  // Note that even if the document is in design mode, a contenteditable element
  // in a shadow tree is focusable.   Therefore, we may need to update editing
  // host even when the document is in design mode.
  if (MOZ_UNLIKELY(NS_WARN_IF(!GetDocument()))) {
    return;
  }

  // We're HTML editor for contenteditable
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return;
  }

  // Get selection ancestor limit which may be old editing host.
  nsIContent* ancestorLimiter = SelectionRef().GetAncestorLimiter();
  if (!ancestorLimiter) {
    // If we've not initialized selection ancestor limit, we should wait focus
    // event to set proper limiter.
    return;
  }

  // Compute current editing host.
  Element* const editingHost = ComputeEditingHost();
  if (NS_WARN_IF(!editingHost)) {
    return;
  }

  // Update selection ancestor limit if current editing host includes the
  // previous editing host.
  // Additionally, the editing host may be an element in shadow DOM and the
  // shadow host is in designMode.  In this case, we need to set the editing
  // host as the new selection limiter.
  if (ancestorLimiter->IsInclusiveDescendantOf(editingHost) ||
      (ancestorLimiter->IsInDesignMode() != editingHost->IsInDesignMode())) {
    // Note that don't call HTMLEditor::InitializeSelectionAncestorLimit()
    // here because it may collapse selection to the first editable node.
    EditorBase::InitializeSelectionAncestorLimit(*editingHost);
  }
}

EventTarget* HTMLEditor::GetDOMEventTarget() const {
  // Don't use getDocument here, because we have no way of knowing
  // whether Init() was ever called.  So we need to get the document
  // ourselves, if it exists.
  Document* doc = GetDocument();
  MOZ_ASSERT(doc, "The HTMLEditor has not been initialized yet");
  if (!doc) {
    return nullptr;
  }

  // Register the EditorEventListener to the parent of window.
  //
  // The advantage of this approach is HTMLEditor can still
  // receive events when shadow dom is involved.
  if (nsPIDOMWindowOuter* win = doc->GetWindow()) {
    return win->GetParentTarget();
  }
  return nullptr;
}

Element* HTMLEditor::GetBodyElement() const {
  Document* document = GetDocument();
  MOZ_ASSERT(document, "The HTMLEditor hasn't been initialized yet");
  if (NS_WARN_IF(!document)) {
    return nullptr;
  }
  return document->GetBody();
}

nsINode* HTMLEditor::GetFocusedNode() const {
  Element* focusedElement = GetFocusedElement();
  if (!focusedElement) {
    return nullptr;
  }

  // focusedElement might be non-null even
  // nsFocusManager::GetFocusedElementStatic() returns null.  That's the
  // designMode case, and in that case our GetFocusedElement() returns the root
  // element, but we want to return the document.

  if ((focusedElement = nsFocusManager::GetFocusedElementStatic())) {
    return focusedElement;
  }

  return GetDocument();
}

bool HTMLEditor::OurWindowHasFocus() const {
  nsFocusManager* focusManager = nsFocusManager::GetFocusManager();
  if (NS_WARN_IF(!focusManager)) {
    return false;
  }
  nsPIDOMWindowOuter* focusedWindow = focusManager->GetFocusedWindow();
  if (!focusedWindow) {
    return false;
  }
  Document* document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return false;
  }
  nsPIDOMWindowOuter* ourWindow = document->GetWindow();
  return ourWindow == focusedWindow;
}

bool HTMLEditor::IsAcceptableInputEvent(WidgetGUIEvent* aGUIEvent) const {
  if (!EditorBase::IsAcceptableInputEvent(aGUIEvent)) {
    return false;
  }

  // While there is composition, all composition events in its top level
  // window are always fired on the composing editor.  Therefore, if this
  // editor has composition, the composition events should be handled in this
  // editor.
  if (mComposition && aGUIEvent->AsCompositionEvent()) {
    return true;
  }

  nsCOMPtr<nsINode> eventTargetNode =
      nsINode::FromEventTargetOrNull(aGUIEvent->GetOriginalDOMEventTarget());
  if (NS_WARN_IF(!eventTargetNode)) {
    return false;
  }

  if (eventTargetNode->IsContent()) {
    eventTargetNode =
        eventTargetNode->AsContent()->FindFirstNonChromeOnlyAccessContent();
    if (NS_WARN_IF(!eventTargetNode)) {
      return false;
    }
  }

  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    return false;
  }

  if (eventTargetNode->IsInDesignMode()) {
    // If this editor is in designMode and the event target is the document,
    // the event is for this editor.
    if (eventTargetNode->IsDocument()) {
      return eventTargetNode == document;
    }
    // Otherwise, check whether the event target is in this document or not.
    if (NS_WARN_IF(!eventTargetNode->IsContent())) {
      return false;
    }
    if (document == eventTargetNode->GetUncomposedDoc()) {
      return true;
    }
  }

  // Space event for <button> and <summary> with contenteditable
  // should be handle by the themselves.
  if (aGUIEvent->mMessage == eKeyPress &&
      aGUIEvent->AsKeyboardEvent()->ShouldWorkAsSpaceKey()) {
    nsGenericHTMLElement* element =
        HTMLButtonElement::FromNode(eventTargetNode);
    if (!element) {
      element = HTMLSummaryElement::FromNode(eventTargetNode);
    }

    if (element && element->IsContentEditable()) {
      return false;
    }
  }
  // This HTML editor is for contenteditable.  We need to check the validity
  // of the target.
  if (NS_WARN_IF(!eventTargetNode->IsContent())) {
    return false;
  }

  // If the event is a mouse event, we need to check if the target content is
  // the focused editing host or its descendant.
  if (aGUIEvent->AsMouseEventBase()) {
    nsIContent* editingHost = ComputeEditingHost();
    // If there is no active editing host, we cannot handle the mouse event
    // correctly.
    if (!editingHost) {
      return false;
    }
    // If clicked on non-editable root element but the body element is the
    // active editing host, we should assume that the click event is
    // targetted.
    if (eventTargetNode == document->GetRootElement() &&
        !eventTargetNode->HasFlag(NODE_IS_EDITABLE) &&
        editingHost == document->GetBodyElement()) {
      eventTargetNode = editingHost;
    }
    // If the target element is neither the active editing host nor a
    // descendant of it, we may not be able to handle the event.
    if (!eventTargetNode->IsInclusiveDescendantOf(editingHost)) {
      return false;
    }
    // If the clicked element has an independent selection, we shouldn't
    // handle this click event.
    if (eventTargetNode->AsContent()->HasIndependentSelection()) {
      return false;
    }
    // If the target content is editable, we should handle this event.
    return eventTargetNode->HasFlag(NODE_IS_EDITABLE);
  }

  // If the target of the other events which target focused element isn't
  // editable or has an independent selection, this editor shouldn't handle
  // the event.
  if (!eventTargetNode->HasFlag(NODE_IS_EDITABLE) ||
      eventTargetNode->AsContent()->HasIndependentSelection()) {
    return false;
  }

  // Finally, check whether we're actually focused or not.  When we're not
  // focused, we should ignore the dispatched event by script (or something)
  // because content editable element needs selection in itself for editing.
  // However, when we're not focused, it's not guaranteed.
  return IsActiveInDOMWindow();
}

Result<widget::IMEState, nsresult> HTMLEditor::GetPreferredIMEState() const {
  // HTML editor don't prefer the CSS ime-mode because IE didn't do so too.
  return IMEState{IsReadonly() ? IMEEnabled::Disabled : IMEEnabled::Enabled,
                  IMEState::DONT_CHANGE_OPEN_STATE};
}

already_AddRefed<Element> HTMLEditor::GetInputEventTargetElement() const {
  // If there is no selection ranges, we'll do nothing.  Therefore,
  // `beforeinput` event should not be fired.
  // FIXME: If there is no selection but we've already modified the DOM,
  // we should fire `input` event on the editing host.  However, we cannot
  // know which one was the editing host when we touched the DOM.
  if (MOZ_UNLIKELY(!SelectionRef().RangeCount())) {
    return nullptr;
  }

  RefPtr<Element> target = ComputeEditingHost(LimitInBodyElement::No);
  if (target) {
    return target.forget();
  }

  // When there is no active editing host due to focus node is a
  // non-editable node, we should look for its editable parent to
  // dispatch `beforeinput` event.
  nsIContent* focusContent =
      nsIContent::FromNodeOrNull(SelectionRef().GetFocusNode());
  if (!focusContent || focusContent->IsEditable()) {
    return nullptr;
  }
  for (Element* element : focusContent->AncestorsOfType<Element>()) {
    if (element->IsEditable()) {
      target = element->GetEditingHost();
      return target.forget();
    }
  }
  return nullptr;
}

}  // namespace mozilla
