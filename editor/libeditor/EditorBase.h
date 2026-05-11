/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EditorBase_h
#define mozilla_EditorBase_h

#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/Assertions.h"      // for MOZ_ASSERT, etc.
#include "mozilla/EditAction.h"      // for EditAction and EditSubAction
#include "mozilla/EditorDOMPoint.h"  // for EditorDOMPoint
#include "mozilla/EditorForwards.h"
#include "mozilla/EventForwards.h"       // for InputEventTargetRanges
#include "mozilla/Likely.h"              // for MOZ_UNLIKELY, MOZ_LIKELY
#include "mozilla/Maybe.h"               // for Maybe
#include "mozilla/OwningNonNull.h"       // for OwningNonNull
#include "mozilla/PendingStyles.h"       // for PendingStyle, PendingStyleCache
#include "mozilla/RangeBoundary.h"       // for RawRangeBoundary, RangeBoundary
#include "mozilla/SelectionState.h"      // for RangeUpdater, etc.
#include "mozilla/StyleSheet.h"          // for StyleSheet
#include "mozilla/TransactionManager.h"  // for TransactionManager
#include "mozilla/WeakPtr.h"             // for WeakPtr
#include "mozilla/dom/DataTransfer.h"    // for dom::DataTransfer
#include "mozilla/dom/HTMLBRElement.h"   // for dom::HTMLBRElement
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Text.h"
#include "nsAtom.h"    // for nsAtom, nsStaticAtom
#include "nsCOMPtr.h"  // for already_AddRefed, nsCOMPtr
#include "nsCycleCollectionParticipant.h"
#include "nsGkAtoms.h"
#include "nsIClipboard.h"            // for nsIClipboard::ClipboardType
#include "nsIContentInlines.h"       // for nsINode::IsEditable()
#include "nsIEditor.h"               // for nsIEditor, etc.
#include "nsISelectionController.h"  // for nsISelectionController constants
#include "nsISelectionListener.h"    // for nsISelectionListener
#include "nsISupportsImpl.h"         // for EditorBase::Release, etc.
#include "nsIWeakReferenceUtils.h"   // for nsWeakPtr
#include "nsLiteralString.h"         // for NS_LITERAL_STRING
#include "nsPIDOMWindow.h"           // for nsPIDOMWindowInner, etc.
#include "nsString.h"                // for nsCString
#include "nsTArray.h"                // for nsTArray and AutoTArray
#include "nsWeakReference.h"         // for nsSupportsWeakReference
#include "nscore.h"                  // for nsresult, nsAString, etc.

#include <tuple>  // for std::tuple

class mozInlineSpellChecker;
class nsAtom;
class nsCaret;
class nsIContent;
class nsIDocumentEncoder;
class nsIDocumentStateListener;
class nsIEditActionListener;
class nsINode;
class nsIPrincipal;
class nsISupports;
class nsITransferable;
class nsITransaction;
class nsIWidget;
class nsRange;

namespace mozilla {
class AlignStateAtSelection;
class AutoTransactionsConserveSelection;
class AutoUpdateViewBatch;
class ErrorResult;
class IMEContentObserver;
class ListElementSelectionState;
class ListItemElementSelectionState;
class ParagraphStateAtSelection;
class PresShell;
class TextComposition;
class TextInputListener;
class TextServicesDocument;
namespace dom {
class AbstractRange;
class DataTransfer;
class Document;
class DragEvent;
class Element;
class EventTarget;
class HTMLBRElement;
}  // namespace dom

namespace widget {
struct IMEState;
}  // namespace widget

/**
 * Implementation of an editor object.  it will be the controller/focal point
 * for the main editor services. i.e. the GUIManager, publishing, transaction
 * manager, event interfaces. the idea for the event interfaces is to have them
 * delegate the actual commands to the editor independent of the XPFE
 * implementation.
 */
class EditorBase : public nsIEditor,
                   public nsISelectionListener,
                   public nsSupportsWeakReference {
 public:
  /****************************************************************************
   * NOTE: DO NOT MAKE YOUR NEW METHODS PUBLIC IF they are called by other
   *       classes under libeditor except EditorEventListener and
   *       HTMLEditorEventListener because each public method which may fire
   *       eEditorInput event will need to instantiate new stack class for
   *       managing input type value of eEditorInput and cache some objects
   *       for smarter handling.  In other words, when you add new root
   *       method to edit the DOM tree, you can make your new method public.
   ****************************************************************************/

  using DataTransfer = dom::DataTransfer;
  using Document = dom::Document;
  using Element = dom::Element;
  using InterlinePosition = dom::Selection::InterlinePosition;
  using Selection = dom::Selection;
  using Text = dom::Text;

  enum class EditorType { Text, HTML };

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(EditorBase, nsIEditor)

  // nsIEditor methods
  NS_DECL_NSIEDITOR

  // nsISelectionListener method
  NS_DECL_NSISELECTIONLISTENER

  /**
   * The default constructor. This should suffice. the setting of the
   * interfaces is done after the construction of the editor class.
   */
  explicit EditorBase(EditorType aEditorType);

  [[nodiscard]] bool IsInitialized() const {
    return mDocument && mDidPostCreate;
  }
  [[nodiscard]] bool IsBeingInitialized() const {
    return mDocument && !mDidPostCreate;
  }
  [[nodiscard]] bool Destroyed() const { return mDidPreDestroy; }

  Document* GetDocument() const { return mDocument; }
  nsPIDOMWindowOuter* GetWindow() const;
  nsPIDOMWindowInner* GetInnerWindow() const;

  /**
   * MaybeNodeRemovalsObservedByDevTools() returns true when the mutations in
   * the document is observed by DevTools.
   *
   * @return                    true if the editor is an HTMLEditor instance
   *                            and the mutations in the document is observed by
   *                            DevTools.
   */
  [[nodiscard]] bool MaybeNodeRemovalsObservedByDevTools() const;

  /**
   * MayHaveBeforeInputEventListenersForTelemetry() returns true when the
   * window may have or have had one or more `beforeinput` event listeners.
   * Note that this may return false even if there is a `beforeinput`.
   * See nsPIDOMWindowInner::HasBeforeInputEventListenersForTelemetry()'s
   * comment for the detail.
   */
  bool MayHaveBeforeInputEventListenersForTelemetry() const {
    if (const nsPIDOMWindowInner* window = GetInnerWindow()) {
      return window->HasBeforeInputEventListenersForTelemetry();
    }
    return false;
  }

  /**
   * MutationObserverHasObservedNodeForTelemetry() returns true when a node in
   * the window may have been observed by the web apps with a mutation observer
   * (i.e., `MutationObserver.observe()` called by chrome script and addon's
   * script does not make this returns true).
   * Note that this may return false even if there is a node observed by
   * a MutationObserver.  See
   * nsPIDOMWindowInner::MutationObserverHasObservedNodeForTelemetry()'s comment
   * for the detail.
   */
  bool MutationObserverHasObservedNodeForTelemetry() const {
    if (const nsPIDOMWindowInner* window = GetInnerWindow()) {
      return window->MutationObserverHasObservedNodeForTelemetry();
    }
    return false;
  }

  /**
   * This checks whether the call with aPrincipal should or should not be
   * treated as user input.
   */
  [[nodiscard]] static bool TreatAsUserInput(nsIPrincipal* aPrincipal);

  PresShell* GetPresShell() const;
  nsPresContext* GetPresContext() const;
  already_AddRefed<nsCaret> GetCaret() const;

  already_AddRefed<nsIWidget> GetWidget() const;

  nsISelectionController* GetSelectionController() const;

  nsresult GetSelection(SelectionType aSelectionType,
                        Selection** aSelection) const;

  Selection* GetSelection(
      SelectionType aSelectionType = SelectionType::eNormal) const {
    if (aSelectionType == SelectionType::eNormal &&
        IsEditActionDataAvailable()) {
      return &SelectionRef();
    }
    nsISelectionController* sc = GetSelectionController();
    if (!sc) {
      return nullptr;
    }
    Selection* selection = sc->GetSelection(ToRawSelectionType(aSelectionType));
    return selection;
  }

  /**
   * @return Ancestor limiter of normal selection
   */
  [[nodiscard]] nsIContent* GetSelectionAncestorLimiter() const {
    Selection* selection = GetSelection(SelectionType::eNormal);
    return selection ? selection->GetAncestorLimiter() : nullptr;
  }

  /**
   * Create a DataTransfer object that can be shared between the paste event
   * and pasting into a DOM element.
   */
  already_AddRefed<DataTransfer> CreateDataTransferForPaste(
      EventMessage aEventMessage,
      nsIClipboard::ClipboardType aClipboardType) const;

  /**
   * Fast non-refcounting editor root element accessor
   */
  Element* GetRoot() const { return mRootElement; }

  /**
   * Likewise, but gets the text control element instead of the root for
   * plaintext editors.
   */
  Element* GetExposedRoot() const;

  /**
   * Set or unset TextInputListener.  If setting non-nullptr when the editor
   * already has a TextInputListener, this will crash in debug build.
   */
  void SetTextInputListener(TextInputListener* aTextInputListener);

  /**
   * Set or unset IMEContentObserver.  If setting non-nullptr when the editor
   * already has an IMEContentObserver, this will crash in debug build.
   */
  void SetIMEContentObserver(IMEContentObserver* aIMEContentObserver);

  /**
   * Returns current composition.
   */
  TextComposition* GetComposition() const;

  /**
   * Get preferred IME status of current widget.
   */
  [[nodiscard]] virtual Result<widget::IMEState, nsresult>
  GetPreferredIMEState() const = 0;

  /**
   * Returns true if there is composition string and not fixed.
   */
  bool IsIMEComposing() const;

  /**
   * Commit composition if there is.
   * Note that when there is a composition, this requests to commit composition
   * to native IME.  Therefore, when there is composition, this can do anything.
   * For example, the editor instance, the widget or the process itself may
   * be destroyed.
   */
  nsresult CommitComposition();

  /**
   * ToggleTextDirection() toggles text-direction of the root element.
   *
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  ToggleTextDirectionAsAction(nsIPrincipal* aPrincipal = nullptr);

  /**
   * SwitchTextDirectionTo() sets the text-direction of the root element to
   * LTR or RTL.
   */
  enum class TextDirection {
    eLTR,
    eRTL,
  };
  MOZ_CAN_RUN_SCRIPT void SwitchTextDirectionTo(TextDirection aTextDirection);

  /**
   * Finalizes selection and caret for the editor.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult FinalizeSelection();

  /**
   * Returns true if selection is in an editable element and both the range
   * start and the range end are editable.  E.g., even if the selection range
   * includes non-editable elements, returns true when one of common ancestors
   * of the range start and the range end is editable.  Otherwise, false.
   */
  bool IsSelectionEditable();

  /**
   * Returns number of undo or redo items.
   */
  size_t NumberOfUndoItems() const {
    return mTransactionManager ? mTransactionManager->NumberOfUndoItems() : 0;
  }
  size_t NumberOfRedoItems() const {
    return mTransactionManager ? mTransactionManager->NumberOfRedoItems() : 0;
  }

  /**
   * Returns number of maximum undo/redo transactions.
   */
  int32_t NumberOfMaximumTransactions() const {
    return mTransactionManager
               ? mTransactionManager->NumberOfMaximumTransactions()
               : 0;
  }

  /**
   * Returns true if this editor can store transactions for undo/redo.
   */
  bool IsUndoRedoEnabled() const {
    return mTransactionManager &&
           mTransactionManager->NumberOfMaximumTransactions();
  }

  /**
   * Return true if it's possible to undo/redo right now.
   */
  bool CanUndo() const {
    return IsUndoRedoEnabled() && NumberOfUndoItems() > 0;
  }
  bool CanRedo() const {
    return IsUndoRedoEnabled() && NumberOfRedoItems() > 0;
  }

  /**
   * Enables or disables undo/redo feature.  Returns true if it succeeded,
   * otherwise, e.g., we're undoing or redoing, returns false.
   */
  bool EnableUndoRedo(int32_t aMaxTransactionCount = -1) {
    if (!mTransactionManager) {
      mTransactionManager = new TransactionManager();
    }
    return mTransactionManager->EnableUndoRedo(aMaxTransactionCount);
  }
  bool DisableUndoRedo() {
    if (!mTransactionManager) {
      return true;
    }
    return mTransactionManager->DisableUndoRedo();
  }
  bool ClearUndoRedo() {
    if (!mTransactionManager) {
      return true;
    }
    return mTransactionManager->ClearUndoRedo();
  }

  /**
   * See Document::AreClipboardCommandsUnconditionallyEnabled.
   */
  bool AreClipboardCommandsUnconditionallyEnabled() const;

  /**
   * IsCutCommandEnabled() returns whether cut command can be enabled or
   * disabled.  This always returns true if we're in non-chrome HTML/XHTML
   * document.  Otherwise, same as the result of `IsCopyToClipboardAllowed()`.
   */
  MOZ_CAN_RUN_SCRIPT bool IsCutCommandEnabled() const;

  /**
   * IsCopyCommandEnabled() returns copy command can be enabled or disabled.
   * This always returns true if we're in non-chrome HTML/XHTML document.
   * Otherwise, same as the result of `IsCopyToClipboardAllowed()`.
   */
  MOZ_CAN_RUN_SCRIPT bool IsCopyCommandEnabled() const;

  /**
   * IsCopyToClipboardAllowed() returns true if the selected content can
   * be copied into the clipboard.  This returns true when:
   * - `Selection` is not collapsed and we're not a password editor.
   * - `Selection` is not collapsed and we're a password editor but selection
   *   range is in unmasked range.
   */
  bool IsCopyToClipboardAllowed() const {
    AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
    if (NS_WARN_IF(!editActionData.CanHandle())) {
      return false;
    }
    return IsCopyToClipboardAllowedInternal();
  }

  /**
   * Called before starting to handle eMouseDown or eMouseUp in PresShell.
   *
   * @return true if IME consumed aMouseEvent.
   */
  MOZ_CAN_RUN_SCRIPT bool WillHandleMouseButtonEvent(
      WidgetMouseEvent& aMouseEvent);

  /**
   * HandleDropEvent() is called from EditorEventListener::Drop that is handler
   * of drop event.
   */
  MOZ_CAN_RUN_SCRIPT nsresult HandleDropEvent(dom::DragEvent* aDropEvent);

  MOZ_CAN_RUN_SCRIPT virtual nsresult HandleKeyPressEvent(
      WidgetKeyboardEvent* aKeyboardEvent);

  virtual dom::EventTarget* GetDOMEventTarget() const = 0;

  /**
   * OnCompositionStart() is called when editor receives eCompositionStart
   * event which should be handled in this editor.
   */
  nsresult OnCompositionStart(WidgetCompositionEvent& aCompositionStartEvent);

  /**
   * OnCompositionChange() is called when editor receives an eCompositioChange
   * event which should be handled in this editor.
   *
   * @param aCompositionChangeEvent     eCompositionChange event which should
   *                                    be handled in this editor.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  OnCompositionChange(WidgetCompositionEvent& aCompositionChangeEvent);

  /**
   * OnCompositionEnd() is called when editor receives an eCompositionChange
   * event and it's followed by eCompositionEnd event and after
   * OnCompositionChange() is called.
   */
  MOZ_CAN_RUN_SCRIPT void OnCompositionEnd(
      WidgetCompositionEvent& aCompositionEndEvent);

  /**
   * Accessor methods to flags.
   */
  uint32_t Flags() const { return mFlags; }

  MOZ_CAN_RUN_SCRIPT nsresult AddFlags(uint32_t aFlags) {
    const uint32_t kOldFlags = Flags();
    const uint32_t kNewFlags = (kOldFlags | aFlags);
    if (kNewFlags == kOldFlags) {
      return NS_OK;
    }
    return SetFlags(kNewFlags);  // virtual call and may be expensive.
  }
  MOZ_CAN_RUN_SCRIPT nsresult RemoveFlags(uint32_t aFlags) {
    const uint32_t kOldFlags = Flags();
    const uint32_t kNewFlags = (kOldFlags & ~aFlags);
    if (kNewFlags == kOldFlags) {
      return NS_OK;
    }
    return SetFlags(kNewFlags);  // virtual call and may be expensive.
  }
  MOZ_CAN_RUN_SCRIPT nsresult AddAndRemoveFlags(uint32_t aAddingFlags,
                                                uint32_t aRemovingFlags) {
    MOZ_ASSERT(!(aAddingFlags & aRemovingFlags),
               "Same flags are specified both adding and removing");
    const uint32_t kOldFlags = Flags();
    const uint32_t kNewFlags = ((kOldFlags | aAddingFlags) & ~aRemovingFlags);
    if (kNewFlags == kOldFlags) {
      return NS_OK;
    }
    return SetFlags(kNewFlags);  // virtual call and may be expensive.
  }

  bool IsSingleLineEditor() const {
    const bool isSingleLineEditor =
        (mFlags & nsIEditor::eEditorSingleLineMask) != 0;
    MOZ_ASSERT_IF(isSingleLineEditor, IsTextEditor());
    return isSingleLineEditor;
  }

  bool IsPasswordEditor() const {
    const bool isPasswordEditor =
        (mFlags & nsIEditor::eEditorPasswordMask) != 0;
    MOZ_ASSERT_IF(isPasswordEditor, IsTextEditor());
    return isPasswordEditor;
  }

  // FYI: Both IsRightToLeft() and IsLeftToRight() may return false if
  //      the editor inherits the content node's direction.
  bool IsRightToLeft() const {
    return (mFlags & nsIEditor::eEditorRightToLeft) != 0;
  }
  bool IsLeftToRight() const {
    return (mFlags & nsIEditor::eEditorLeftToRight) != 0;
  }

  bool IsReadonly() const {
    return (mFlags & nsIEditor::eEditorReadonlyMask) != 0;
  }

  bool IsMailEditor() const {
    return (mFlags & nsIEditor::eEditorMailMask) != 0;
  }

  bool IsInteractionAllowed() const {
    const bool isInteractionAllowed =
        (mFlags & nsIEditor::eEditorAllowInteraction) != 0;
    MOZ_ASSERT_IF(isInteractionAllowed, IsHTMLEditor());
    return isInteractionAllowed;
  }

  bool ShouldSkipSpellCheck() const {
    return (mFlags & nsIEditor::eEditorSkipSpellCheck) != 0;
  }

  bool HasIndependentSelection() const {
    MOZ_ASSERT_IF(mSelectionController, IsTextEditor());
    return !!mSelectionController;
  }

  bool IsModifiable() const { return !IsReadonly(); }

  /**
   * IsInEditSubAction() return true while the instance is handling an edit
   * sub-action.  Otherwise, false.
   */
  bool IsInEditSubAction() const { return mIsInEditSubAction; }

  /**
   * IsEmpty() checks whether the editor is empty.  If editor has only padding
   * <br> element for empty editor, returns true.  If editor's root element has
   * non-empty text nodes or other nodes like <br>, returns false.
   */
  virtual bool IsEmpty() const = 0;

  /**
   * SuppressDispatchingInputEvent() suppresses or unsuppresses dispatching
   * "input" event.
   */
  void SuppressDispatchingInputEvent(bool aSuppress) {
    mDispatchInputEvent = !aSuppress;
  }

  /**
   * IsSuppressingDispatchingInputEvent() returns true if the editor stops
   * dispatching input event.  Otherwise, false.
   */
  bool IsSuppressingDispatchingInputEvent() const {
    return !mDispatchInputEvent;
  }

  /**
   * Returns true if markNodeDirty() has any effect.  Returns false if
   * markNodeDirty() is a no-op.
   */
  bool OutputsMozDirty() const {
    // Return true for Composer (!IsInteractionAllowed()) or mail
    // (IsMailEditor()), but false for webpages.
    return !IsInteractionAllowed() || IsMailEditor();
  }

  /**
   * Get the focused element, if we're focused.  Returns null otherwise.
   */
  virtual Element* GetFocusedElement() const;

  /**
   * Whether the aGUIEvent should be handled by this editor or not.  When this
   * returns false, The aGUIEvent shouldn't be handled on this editor,
   * i.e., The aGUIEvent should be handled by another inner editor or ancestor
   * elements.
   */
  virtual bool IsAcceptableInputEvent(WidgetGUIEvent* aGUIEvent) const;

  /**
   * FindSelectionRoot() returns a selection root of this editor when aNode
   * gets focus.  aNode must be a content node or a document node.  When the
   * target isn't a part of this editor, returns nullptr.  If this is for
   * designMode, you should set the document node to aNode except that an
   * element in the document has focus.
   */
  [[nodiscard]] virtual Element* FindSelectionRoot(const nsINode& aNode) const;

  /**
   * OnFocus() is called when we get a focus event.
   *
   * @param aOriginalEventTargetNode    The original event target node of the
   *                                    focus event.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult OnFocus(
      const nsINode& aOriginalEventTargetNode);

  /**
   * OnBlur() is called when we're blurred.
   *
   * @param aEventTarget        The event target of the blur event.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult OnBlur(
      const dom::EventTarget* aEventTarget) = 0;

  /** Resyncs spellchecking state (enabled/disabled).  This should be called
   * when anything that affects spellchecking state changes, such as the
   * spellcheck attribute value.
   */
  void SyncRealTimeSpell();

  /**
   * Do "cut".
   *
   * @param aPrincipal          If you know current context is subject
   *                            principal or system principal, set it.
   *                            When nullptr, this checks it automatically.
   */
  MOZ_CAN_RUN_SCRIPT nsresult CutAsAction(nsIPrincipal* aPrincipal = nullptr);

  /**
   * CanPaste() returns true if user can paste something at current selection.
   */
  virtual bool CanPaste(nsIClipboard::ClipboardType aClipboardType) const = 0;

  /**
   * Do "undo" or "redo".
   *
   * @param aCount              How many count of transactions should be
   *                            handled.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult UndoAsAction(uint32_t aCount,
                                           nsIPrincipal* aPrincipal = nullptr);
  MOZ_CAN_RUN_SCRIPT nsresult RedoAsAction(uint32_t aCount,
                                           nsIPrincipal* aPrincipal = nullptr);

  /**
   * InsertTextAsAction() inserts aStringToInsert at selection.
   * Although this method is implementation of nsIEditor.insertText(),
   * this treats the input is an edit action.  If you'd like to insert text
   * as part of edit action, you probably should use InsertTextAsSubAction().
   *
   * @param aStringToInsert     The string to insert.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertTextAsAction(
      const nsAString& aStringToInsert, nsIPrincipal* aPrincipal = nullptr);

  /**
   * InsertLineBreakAsAction() is called when user inputs a line break with
   * Enter or something.  If the instance is `HTMLEditor`, this is called
   * when Shift + Enter or "insertlinebreak" command.
   *
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult InsertLineBreakAsAction(
      nsIPrincipal* aPrincipal = nullptr) = 0;

  /**
   * CanDeleteSelection() returns true if `Selection` is not collapsed and
   * it's allowed to be removed.
   */
  bool CanDeleteSelection() const {
    AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
    if (NS_WARN_IF(!editActionData.CanHandle())) {
      return false;
    }
    return IsModifiable() && !SelectionRef().IsCollapsed();
  }

  /**
   * DeleteSelectionAsAction() removes selection content or content around
   * caret with transactions.  This should be used for handling it as an
   * edit action.  If you'd like to remove selection for preparing to insert
   * something, you probably should use DeleteSelectionAsSubAction().
   *
   * @param aDirectionAndAmount How much range should be removed.
   * @param aStripWrappers      Whether the parent blocks should be removed
   *                            when they become empty.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteSelectionAsAction(nsIEditor::EDirection aDirectionAndAmount,
                          nsIEditor::EStripWrappers aStripWrappers,
                          nsIPrincipal* aPrincipal = nullptr);

  enum class AllowBeforeInputEventCancelable {
    No,
    Yes,
  };

  enum class PreventSetSelection {
    No,
    Yes,
  };

  /**
   * Replace text in aReplaceRange or all text in this editor with aString and
   * treat the change as inserting the string.
   *
   * @param aString             The string to set.
   * @param aReplaceRange       The range to be replaced.
   *                            If nullptr, all contents will be replaced.
   *                            NOTE: Currently, nullptr is not allowed if
   *                                  the editor is an HTMLEditor.
   * @param aAllowBeforeInputEventCancelable
   *                            Whether `beforeinput` event which will be
   *                            dispatched for this can be cancelable or not.
   * @param aPreventSetSelection
   *                            Whether setting selection after replacing text.
   *                            If No, selection is the tail of replaced text.
   *                            If Yes, selection isn't changed.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult ReplaceTextAsAction(
      const nsAString& aString, nsRange* aReplaceRange,
      AllowBeforeInputEventCancelable aAllowBeforeInputEventCancelable,
      PreventSetSelection aPreventSetSelection = PreventSetSelection::No,
      nsIPrincipal* aPrincipal = nullptr);

  /**
   * Can we paste |aTransferable| or, if |aTransferable| is null, will a call
   * to pasteTransferable later possibly succeed if given an instance of
   * nsITransferable then? True if the doc is modifiable, and, if
   * |aTransfeable| is non-null, we have pasteable data in |aTransfeable|.
   */
  virtual bool CanPasteTransferable(nsITransferable* aTransferable) = 0;

  /**
   * PasteAsAction() pastes clipboard content to Selection.  This method
   * may dispatch ePaste event first.  If its defaultPrevent() is called,
   * this does nothing but returns NS_OK.
   *
   * @param aClipboardType      nsIClipboard::kGlobalClipboard or
   *                            nsIClipboard::kSelectionClipboard.
   * @param aDispatchPasteEvent Yes if this should dispatch ePaste event
   *                            before pasting.  Otherwise, No.
   * @param aDataTransfer       The object containing the data to use for the
   *                            paste operation. May be nullptr, in which case
   *                            this will just get the data from the clipboard.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  enum class DispatchPasteEvent { No, Yes };
  MOZ_CAN_RUN_SCRIPT nsresult
  PasteAsAction(nsIClipboard::ClipboardType aClipboardType,
                DispatchPasteEvent aDispatchPasteEvent,
                DataTransfer* aDataTransfer = nullptr,
                nsIPrincipal* aPrincipal = nullptr);

  /**
   * Paste aTransferable at Selection.
   *
   * @param aTransferable       Must not be nullptr.
   * @param aDispatchPasteEvent Yes if this should dispatch ePaste event
   *                            before pasting.  Otherwise, No.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult PasteTransferableAsAction(
      nsITransferable* aTransferable, DispatchPasteEvent aDispatchPasteEvent,
      nsIPrincipal* aPrincipal = nullptr);

  /**
   * PasteAsQuotationAsAction() pastes content in clipboard as quotation.
   * If the editor is TextEditor or in plaintext mode, will paste the content
   * with appending ">" to start of each line.
   * if the editor is HTMLEditor and is not in plaintext mode, will patste it
   * into newly created blockquote element.
   *
   * @param aClipboardType      nsIClipboard::kGlobalClipboard or
   *                            nsIClipboard::kSelectionClipboard.
   * @param aDispatchPasteEvent Yes if this should dispatch ePaste event
   *                            before pasting.  Otherwise, No.
   * @param aDataTransfer       The object containing the data to use for the
   *                            paste operation. May be nullptr, in which case
   *                            this will just get the data from the clipboard.
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  PasteAsQuotationAsAction(nsIClipboard::ClipboardType aClipboardType,
                           DispatchPasteEvent aDispatchPasteEvent,
                           DataTransfer* aDataTransfer = nullptr,
                           nsIPrincipal* aPrincipal = nullptr);

  /**
   * Return true if `beforeinput` or `input` event is being dispatched.
   */
  [[nodiscard]] bool IsDispatchingInputEvent() const {
    return mEditActionData && mEditActionData->IsDispatchingInputEvent();
  }

 protected:  // May be used by friends.
  class AutoEditActionDataSetter;

  /**
   * TopLevelEditSubActionData stores temporary data while we're handling
   * top-level edit sub-action.
   */
  struct MOZ_STACK_CLASS TopLevelEditSubActionData final {
    friend class AutoEditActionDataSetter;

    TopLevelEditSubActionData(const TopLevelEditSubActionData& aOther) = delete;

    // Set selected range before edit.  Then, RangeUpdater keep modifying
    // the range while we're changing the DOM tree.
    RefPtr<RangeItem> mSelectedRange;

    // Computing changed range while we're handling sub actions.
    RefPtr<nsRange> mChangedRange;

    // XXX In strict speaking, mCachedPendingStyles isn't enough to cache
    //     inline styles because inline style can be specified with "style"
    //     attribute and/or CSS in <style> elements or CSS files.  So, we need
    //     to look for better implementation about this.
    // FYI: Initialization cost of AutoPendingStyleCacheArray is expensive and
    //      it is not used by TextEditor so that we should construct it only
    //      when we're an HTMLEditor.
    Maybe<AutoPendingStyleCacheArray> mCachedPendingStyles;

    // If we tried to delete selection, set to true.
    bool mDidDeleteSelection;

    // If we have explicitly set selection inter line, set to true.
    // `AfterEdit()` or something shouldn't overwrite it in such case.
    bool mDidExplicitlySetInterLine;

    // If we have deleted non-collapsed range set to true, there are only 2
    // cases for now:
    //   - non-collapsed range was selected.
    //   - selection was collapsed in a text node and a Unicode character
    //     was removed.
    bool mDidDeleteNonCollapsedRange;

    // If we have deleted parent empty blocks, set to true.
    bool mDidDeleteEmptyParentBlocks;

    // If we're a contenteditable editor, we temporarily increase edit count
    // of the document between `BeforeEdit()` and `AfterEdit()`.  I.e., if
    // we increased the count in `BeforeEdit()`, we need to decrease it in
    // `AfterEdit()`, however, the document may be changed to designMode or
    // non-editable.  Therefore, we need to store with this whether we need
    // to restore it.
    bool mRestoreContentEditableCount;

    // If we explicitly normalized whitespaces around the changed range,
    // set to true.
    bool mDidNormalizeWhitespaces;

    // Set to true by default.  If somebody inserts an HTML fragment
    // intentionally, any empty elements shouldn't be cleaned up later.  In the
    // case this is set to false.
    // TODO: We should not do this by default.  If it's necessary, each edit
    //       action handler do it by itself instead.  Then, we can avoid such
    //       unnecessary DOM tree scan.
    bool mNeedsToCleanUpEmptyElements;

    /**
     * The following methods modifies some data of this struct and
     * `EditSubActionData` struct.  Currently, these are required only
     * by `HTMLEditor`.  Therefore, for cutting the runtime cost of
     * `TextEditor`, these methods should be called only by `HTMLEditor`.
     * But it's fine to use these methods in `TextEditor` if necessary.
     * If so, you need to call `DidDeleteText()` and `DidInsertText()`
     * from `SetTextNodeWithoutTransaction()`.
     */
    void DidCreateElement(EditorBase& aEditorBase, Element& aNewElement);
    void DidInsertContent(EditorBase& aEditorBase, nsIContent& aNewContent);
    void WillDeleteContent(EditorBase& aEditorBase,
                           nsIContent& aRemovingContent);
    void DidSplitContent(EditorBase& aEditorBase, nsIContent& aSplitContent,
                         nsIContent& aNewContent);
    void DidJoinContents(EditorBase& aEditorBase,
                         const EditorRawDOMPoint& aJoinedPoint);
    void DidInsertText(EditorBase& aEditorBase,
                       const EditorRawDOMPoint& aInsertionBegin,
                       const EditorRawDOMPoint& aInsertionEnd);
    void DidDeleteText(EditorBase& aEditorBase,
                       const EditorRawDOMPoint& aStartInTextNode);
    void WillDeleteRange(EditorBase& aEditorBase,
                         const EditorRawDOMPoint& aStart,
                         const EditorRawDOMPoint& aEnd);

   private:
    void Clear() {
      mDidExplicitlySetInterLine = false;
      // We don't need to clear other members which are referred only when the
      // editor is an HTML editor anymore.  Note that if `mSelectedRange` is
      // non-nullptr, that means that we're in `HTMLEditor`.
      if (!mSelectedRange) {
        return;
      }
      mSelectedRange->Clear();
      mChangedRange->Reset();
      if (mCachedPendingStyles.isSome()) {
        mCachedPendingStyles->Clear();
      }
      mDidDeleteSelection = false;
      mDidDeleteNonCollapsedRange = false;
      mDidDeleteEmptyParentBlocks = false;
      mRestoreContentEditableCount = false;
      mDidNormalizeWhitespaces = false;
      mNeedsToCleanUpEmptyElements = true;
    }

    /**
     * Extend mChangedRange to include `aNode`.
     */
    nsresult AddNodeToChangedRange(const HTMLEditor& aHTMLEditor,
                                   nsINode& aNode);

    /**
     * Extend mChangedRange to include `aPoint`.
     */
    nsresult AddPointToChangedRange(const HTMLEditor& aHTMLEditor,
                                    const EditorRawDOMPoint& aPoint);

    /**
     * Extend mChangedRange to include `aStart` and `aEnd`.
     */
    nsresult AddRangeToChangedRange(const HTMLEditor& aHTMLEditor,
                                    const EditorRawDOMPoint& aStart,
                                    const EditorRawDOMPoint& aEnd);

    TopLevelEditSubActionData() = default;
  };

  struct MOZ_STACK_CLASS EditSubActionData final {
    // While this is set to false, TopLevelEditSubActionData::mChangedRange
    // shouldn't be modified since in some cases, modifying it in the setter
    // itself may be faster.  Note that we should affect this only for current
    // edit sub action since mutation event listener may edit different range.
    bool mAdjustChangedRangeFromListener;

   private:
    void Clear() { mAdjustChangedRangeFromListener = true; }

    friend EditorBase;
  };

 protected:  // AutoEditActionDataSetter, this shouldn't be accessed by friends.
  /**
   * SettingDataTransfer enum class is used to specify whether DataTransfer
   * should be initialized with or without format.  For example, when user
   * uses Accel + Shift + V to paste text without format, DataTransfer should
   * have only plain/text data to make web apps treat it without format.
   */
  enum class SettingDataTransfer {
    eWithFormat,
    eWithoutFormat,
  };

  /**
   * AutoEditActionDataSetter grabs some necessary objects for handling any
   * edit actions and store the edit action what we're handling.  When this is
   * created, its pointer is set to the mEditActionData, and this guarantees
   * the lifetime of grabbing objects until it's destroyed.
   */
  class MOZ_STACK_CLASS AutoEditActionDataSetter final {
   public:
    // NOTE: aPrincipal will be used when we implement "beforeinput" event.
    //       It's set only when maybe we shouldn't dispatch it because of
    //       called by JS.  I.e., if this is nullptr, we can always dispatch
    //       it.
    AutoEditActionDataSetter(const EditorBase& aEditorBase,
                             EditAction aEditAction,
                             nsIPrincipal* aPrincipal = nullptr);
    AutoEditActionDataSetter() = delete;
    AutoEditActionDataSetter(const AutoEditActionDataSetter& aOther) = delete;
    ~AutoEditActionDataSetter();

    void SetSelectionCreatedByDoubleclick(bool aSelectionCreatedByDoubleclick) {
      mSelectionCreatedByDoubleclick = aSelectionCreatedByDoubleclick;
    }

    [[nodiscard]] bool SelectionCreatedByDoubleclick() const {
      return mSelectionCreatedByDoubleclick;
    }

    void UpdateEditAction(EditAction aEditAction) {
      MOZ_ASSERT(!mHasTriedToDispatchBeforeInputEvent,
                 "It's too late to update EditAction since this may have "
                 "already dispatched a beforeinput event");
      mEditAction = aEditAction;
    }

    /**
     * CanHandle() or CanHandleAndHandleBeforeInput() must be called
     * immediately after creating the instance.  If caller does not need to
     * handle "beforeinput" event or caller needs to set additional information
     * the events later, use the former.  Otherwise, use the latter.  If caller
     * uses the former, it's required to call MaybeDispatchBeforeInputEvent() by
     * itself.
     *
     */
    [[nodiscard]] bool CanHandle() const {
#ifdef DEBUG
      mHasCanHandleChecked = true;
#endif  // #ifdef DEBUG
      // Don't allow to run new edit action when an edit action caused
      // destroying the editor while it's being handled.
      if (mEditAction != EditAction::eInitializing &&
          HasEditorDestroyedDuringHandlingEditActionAndNotYetReinitialized()) {
        NS_WARNING("Editor was destroyed during an edit action being handled");
        return false;
      }
      return IsDataAvailable();
    }
    [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
    CanHandleAndMaybeDispatchBeforeInputEvent() {
      if (MOZ_UNLIKELY(NS_WARN_IF(!CanHandle()))) {
        return NS_ERROR_NOT_INITIALIZED;
      }
      nsresult rv = MaybeFlushPendingNotifications();
      if (MOZ_UNLIKELY(NS_FAILED(rv))) {
        return rv;
      }
      return MaybeDispatchBeforeInputEvent();
    }
    [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
    CanHandleAndFlushPendingNotifications() {
      if (MOZ_UNLIKELY(NS_WARN_IF(!CanHandle()))) {
        return NS_ERROR_NOT_INITIALIZED;
      }
      MOZ_ASSERT(MayEditActionRequireLayout(mRawEditAction));
      return MaybeFlushPendingNotifications();
    }

    [[nodiscard]] bool IsDataAvailable() const {
      return mSelection && mEditorBase.mDocument;
    }

    /**
     * MaybeDispatchBeforeInputEvent() considers whether this instance needs to
     * dispatch "beforeinput" event or not.  Then,
     * mHasTriedToDispatchBeforeInputEvent is set to true.
     *
     * @param aDeleteDirectionAndAmount
     *                  If `MayEditActionDeleteAroundCollapsedSelection(
     *                  mEditAction)` returns true, this must be set.
     *                  Otherwise, don't set explicitly.
     * @return          If this method actually dispatches "beforeinput" event
     *                  and it's canceled, returns
     *                  NS_ERROR_EDITOR_ACTION_CANCELED.
     */
    [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MaybeDispatchBeforeInputEvent(
        nsIEditor::EDirection aDeleteDirectionAndAmount = nsIEditor::eNone);

    /**
     * MarkAsBeforeInputHasBeenDispatched() should be called only when updating
     * the DOM occurs asynchronously from user input (e.g., inserting blob
     * object which is loaded asynchronously) and `beforeinput` has already
     * been dispatched (always should be so).
     */
    void MarkAsBeforeInputHasBeenDispatched() {
      MOZ_ASSERT(!HasTriedToDispatchBeforeInputEvent());
      MOZ_ASSERT(mEditAction == EditAction::ePaste ||
                 mEditAction == EditAction::ePasteAsQuotation ||
                 mEditAction == EditAction::eDrop);
      mHasTriedToDispatchBeforeInputEvent = true;
    }

    /**
     * MarkAsHandled() is called before dispatching `input` event and notifying
     * editor observers.  After this is called, any nested edit action become
     * non illegal case.
     */
    void MarkAsHandled() {
      MOZ_ASSERT(!mHandled);
      mHandled = true;
    }

    /**
     * ShouldAlreadyHaveHandledBeforeInputEventDispatching() returns true if the
     * edit action requires to handle "beforeinput" event but not yet dispatched
     * it nor considered as not dispatched it and can dispatch it when this is
     * called.
     */
    bool ShouldAlreadyHaveHandledBeforeInputEventDispatching() const {
      return !HasTriedToDispatchBeforeInputEvent() &&
             NeedsBeforeInputEventHandling(mEditAction) &&
             IsBeforeInputEventEnabled() /* &&
              // If we still need to dispatch a clipboard event, we should
              // dispatch it first, then, we need to dispatch beforeinput
              // event later.
              !NeedsToDispatchClipboardEvent()*/
          ;
    }

    /**
     * HasTriedToDispatchBeforeInputEvent() returns true if the instance's
     * MaybeDispatchBeforeInputEvent() has already been called.
     */
    bool HasTriedToDispatchBeforeInputEvent() const {
      return mHasTriedToDispatchBeforeInputEvent;
    }

    bool IsCanceled() const { return mBeforeInputEventCanceled; }

    /**
     * Returns a `Selection` for normal selection.  The lifetime is guaranteed
     * during alive this instance in the stack.
     */
    MOZ_KNOWN_LIVE Selection& SelectionRef() const {
      MOZ_ASSERT(!mSelection ||
                 (mSelection->GetType() == SelectionType::eNormal));
      return *mSelection;
    }

    Text* GetCachedTextNode() const {
      MOZ_ASSERT(mEditorBase.IsTextEditor());
      return mTextNode;
    }

    nsIPrincipal* GetPrincipal() const { return mPrincipal; }
    EditAction GetEditAction() const { return mEditAction; }

    template <typename PT, typename CT>
    void SetSpellCheckRestartPoint(const EditorDOMPointBase<PT, CT>& aPoint) {
      MOZ_ASSERT(aPoint.IsSet());
      // We should store only container and offset because new content may
      // be inserted before referring child.
      // XXX Shouldn't we compare whether aPoint is before
      //     mSpellCheckRestartPoint if it's set.
      mSpellCheckRestartPoint =
          EditorDOMPoint(aPoint.GetContainer(), aPoint.Offset());
    }
    void ClearSpellCheckRestartPoint() { mSpellCheckRestartPoint.Clear(); }
    const EditorDOMPoint& GetSpellCheckRestartPoint() const {
      return mSpellCheckRestartPoint;
    }

    void SetData(const nsAString& aData) {
      MOZ_ASSERT(!mHasTriedToDispatchBeforeInputEvent,
                 "It's too late to set data since this may have already "
                 "dispatched a beforeinput event");
      mData = aData;
    }
    const nsString& GetData() const { return mData; }

    void SetColorData(const nsAString& aData);

    /**
     * InitializeDataTransfer(DataTransfer*) sets mDataTransfer to
     * aDataTransfer.  In this case, aDataTransfer should not be read/write
     * because it'll be set to InputEvent.dataTransfer and which should be
     * read-only.
     */
    void InitializeDataTransfer(DataTransfer* aDataTransfer);
    /**
     * InitializeDataTransfer(nsITransferable*) creates new DataTransfer
     * instance, initializes it with aTransferable and sets mDataTransfer to
     * it.
     */
    void InitializeDataTransfer(nsITransferable* aTransferable);
    /**
     * InitializeDataTransfer(const nsAString&) creates new DataTransfer
     * instance, initializes it with aString and sets mDataTransfer to it.
     */
    void InitializeDataTransfer(const nsAString& aString);
    /**
     * InitializeDataTransferWithClipboard() creates new DataTransfer instance,
     * initializes it with clipboard and sets mDataTransfer to it.
     */
    void InitializeDataTransferWithClipboard(
        SettingDataTransfer aSettingDataTransfer, DataTransfer* aDataTransfer,
        nsIClipboard::ClipboardType aClipboardType);
    DataTransfer* GetDataTransfer() const { return mDataTransfer; }

    /**
     * AppendTargetRange() appends aTargetRange to target ranges.  This should
     * be used only by edit action handlers which do not want to set target
     * ranges to selection ranges.
     */
    void AppendTargetRange(dom::StaticRange& aTargetRange);
    void AppendTargetRange(RefPtr<dom::StaticRange>&& aTargetRange);

    /**
     * Make dispatching `beforeinput` forcibly non-cancelable.
     */
    void MakeBeforeInputEventNonCancelable() {
      mMakeBeforeInputEventNonCancelable = true;
    }

    /**
     * NotifyOfDispatchingClipboardEvent() is called after dispatching
     * a clipboard event.
     */
    void NotifyOfDispatchingClipboardEvent() {
      MOZ_ASSERT(NeedsToDispatchClipboardEvent());
      MOZ_ASSERT(!mHasTriedToDispatchClipboardEvent);
      mHasTriedToDispatchClipboardEvent = true;
    }

    void Abort() { mAborted = true; }
    bool IsAborted() const { return mAborted; }

    void OnEditorDestroy() {
      if (!mHandled && mHasTriedToDispatchBeforeInputEvent) {
        // Remember the editor was destroyed only when this edit action is being
        // handled because they are caused by mutation event listeners or
        // something other unexpected event listeners.  In the cases, new child
        // edit action shouldn't been aborted.
        mEditorWasDestroyedDuringHandlingEditAction = true;
        mEditorWasReinitialized = false;
      }
      if (mParentData) {
        mParentData->OnEditorDestroy();
      }
    }
    void OnEditorInitialized();
    /**
     * Return true if the editor was destroyed at least once while the
     * EditAction is being handled.  Note that the editor may have already been
     * reinitialized even if this returns true.
     */
    [[nodiscard]] bool HasEditorDestroyedDuringHandlingEditAction() const {
      return mEditorWasDestroyedDuringHandlingEditAction;
    }
    /**
     * Return true if the editor was destroyed while the EditAction is being
     * handled and has not been reinitialized.  I.e., the editor is still under
     * the destroyed state.
     */
    [[nodiscard]] bool
    HasEditorDestroyedDuringHandlingEditActionAndNotYetReinitialized() const {
      return mEditorWasDestroyedDuringHandlingEditAction &&
             !mEditorWasReinitialized;
    }

    void SetTopLevelEditSubAction(EditSubAction aEditSubAction,
                                  EDirection aDirection = eNone) {
      mTopLevelEditSubAction = aEditSubAction;
      TopLevelEditSubActionDataRef().Clear();
      switch (mTopLevelEditSubAction) {
        case EditSubAction::eInsertNode:
        case EditSubAction::eMoveNode:
        case EditSubAction::eCreateNode:
        case EditSubAction::eSplitNode:
        case EditSubAction::eInsertText:
        case EditSubAction::eInsertTextComingFromIME:
        case EditSubAction::eSetTextProperty:
        case EditSubAction::eRemoveTextProperty:
        case EditSubAction::eRemoveAllTextProperties:
        case EditSubAction::eSetText:
        case EditSubAction::eInsertLineBreak:
        case EditSubAction::eInsertParagraphSeparator:
        case EditSubAction::eCreateOrChangeList:
        case EditSubAction::eIndent:
        case EditSubAction::eOutdent:
        case EditSubAction::eSetOrClearAlignment:
        case EditSubAction::eCreateOrRemoveBlock:
        case EditSubAction::eFormatBlockForHTMLCommand:
        case EditSubAction::eMergeBlockContents:
        case EditSubAction::eRemoveList:
        case EditSubAction::eCreateOrChangeDefinitionListItem:
        case EditSubAction::eInsertElement:
        case EditSubAction::eInsertQuotation:
        case EditSubAction::eInsertQuotedText:
        case EditSubAction::ePasteHTMLContent:
        case EditSubAction::eInsertHTMLSource:
        case EditSubAction::eSetPositionToAbsolute:
        case EditSubAction::eSetPositionToStatic:
        case EditSubAction::eDecreaseZIndex:
        case EditSubAction::eIncreaseZIndex:
          MOZ_ASSERT(aDirection == eNext);
          mDirectionOfTopLevelEditSubAction = eNext;
          break;
        case EditSubAction::eJoinNodes:
        case EditSubAction::eDeleteText:
          MOZ_ASSERT(aDirection == ePrevious);
          mDirectionOfTopLevelEditSubAction = ePrevious;
          break;
        case EditSubAction::eUndo:
        case EditSubAction::eRedo:
        case EditSubAction::eComputeTextToOutput:
        case EditSubAction::eCreatePaddingBRElementForEmptyEditor:
        case EditSubAction::eMaintainWhiteSpaceVisibility:
        case EditSubAction::eNone:
          MOZ_ASSERT(aDirection == eNone);
          mDirectionOfTopLevelEditSubAction = eNone;
          break;
        case EditSubAction::eDeleteNode:
        case EditSubAction::eDeleteSelectedContent:
          // Unfortunately, eDeleteNode and eDeleteSelectedContent is used with
          // any direction.  We might have specific sub-action for each
          // direction, but there are some points referencing
          // eDeleteSelectedContent so that we should keep storing direction
          // as-is for now.
          mDirectionOfTopLevelEditSubAction = aDirection;
          break;
      }
    }
    EditSubAction GetTopLevelEditSubAction() const {
      MOZ_ASSERT(IsDataAvailable());
      return mTopLevelEditSubAction;
    }
    EDirection GetDirectionOfTopLevelEditSubAction() const {
      return mDirectionOfTopLevelEditSubAction;
    }

    const TopLevelEditSubActionData& TopLevelEditSubActionDataRef() const {
      return mParentData ? mParentData->TopLevelEditSubActionDataRef()
                         : mTopLevelEditSubActionData;
    }
    TopLevelEditSubActionData& TopLevelEditSubActionDataRef() {
      return mParentData ? mParentData->TopLevelEditSubActionDataRef()
                         : mTopLevelEditSubActionData;
    }

    const EditSubActionData& EditSubActionDataRef() const {
      return mEditSubActionData;
    }
    EditSubActionData& EditSubActionDataRef() { return mEditSubActionData; }

    SelectionState& SavedSelectionRef() {
      return mParentData ? mParentData->SavedSelectionRef() : mSavedSelection;
    }
    const SelectionState& SavedSelectionRef() const {
      return mParentData ? mParentData->SavedSelectionRef() : mSavedSelection;
    }

    RangeUpdater& RangeUpdaterRef() {
      return mParentData ? mParentData->RangeUpdaterRef() : mRangeUpdater;
    }
    const RangeUpdater& RangeUpdaterRef() const {
      return mParentData ? mParentData->RangeUpdaterRef() : mRangeUpdater;
    }

    MOZ_CAN_RUN_SCRIPT void UpdateSelectionCache(Selection& aSelection);

    bool IsDispatchingInputEvent() const {
      return mDispatchingInputEvent ||
             (mParentData && mParentData->IsDispatchingInputEvent());
    }
    void WillDispatchInputEvent() {
      MOZ_ASSERT(!mDispatchingInputEvent);
      mDispatchingInputEvent = true;
    }
    void DidDispatchInputEvent() {
      MOZ_ASSERT(mDispatchingInputEvent);
      mDispatchingInputEvent = false;
    }

   private:
    bool IsBeforeInputEventEnabled() const;

    [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
    MaybeFlushPendingNotifications() const;

    static bool NeedsBeforeInputEventHandling(EditAction aEditAction) {
      MOZ_ASSERT(aEditAction != EditAction::eNone);
      switch (aEditAction) {
        case EditAction::eNone:
        // If we're not handling edit action, we don't need to handle
        // "beforeinput" event.
        case EditAction::eNotEditing:
        // If we're being initialized, we may need to create a padding <br>
        // element, but it shouldn't cause `beforeinput` event.
        case EditAction::eInitializing:
        // If we're just selecting or getting table cells, we shouldn't
        // dispatch `beforeinput` event.
        case NS_EDIT_ACTION_CASES_ACCESSING_TABLE_DATA_WITHOUT_EDITING:
        // If raw level transaction API is used, the API user needs to handle
        // both "beforeinput" event and "input" event if it's necessary.
        case EditAction::eUnknown:
        // Hiding/showing password affects only layout so that we don't need
        // to handle beforeinput event for it.
        case EditAction::eHidePassword:
        // We don't need to dispatch "beforeinput" event before
        // "compositionstart".
        case EditAction::eStartComposition:
        // We don't need to let web apps know the mode change.
        case EditAction::eEnableOrDisableCSS:
        case EditAction::eEnableOrDisableAbsolutePositionEditor:
        case EditAction::eEnableOrDisableResizer:
        case EditAction::eEnableOrDisableInlineTableEditingUI:
        // We don't need to let contents in chrome's editor to know the size
        // change.
        case EditAction::eSetWrapWidth:
        // While resizing or moving element, we update only shadow, i.e.,
        // don't touch to the DOM in content.  Therefore, we don't need to
        // dispatch "beforeinput" event.
        case EditAction::eResizingElement:
        case EditAction::eMovingElement:
        // Perhaps, we don't need to dispatch "beforeinput" event for
        // padding `<br>` element for empty editor because it's internal
        // handling and it should be occurred by another change.
        case EditAction::eCreatePaddingBRElementForEmptyEditor:
          return false;
        default:
          return true;
      }
    }

    bool NeedsToDispatchClipboardEvent() const {
      if (mHasTriedToDispatchClipboardEvent) {
        return false;
      }
      switch (mEditAction) {
        case EditAction::ePaste:
        case EditAction::ePasteAsQuotation:
        case EditAction::eCut:
        case EditAction::eCopy:
          return true;
        default:
          return false;
      }
    }

    void MarkEditActionCanceled();

    EditorBase& mEditorBase;
    RefPtr<Selection> mSelection;
    nsTArray<OwningNonNull<Selection>> mRetiredSelections;

    // mTextNode is the text node if and only if the instance is TextEditor.
    // This is set when the instance is created and updated when the TextEditor
    // is reinitialized with the new native anonymous subtree.
    RefPtr<Text> mTextNode;

    // True if the selection was created by doubleclicking a word.
    bool mSelectionCreatedByDoubleclick{false};

    nsCOMPtr<nsIPrincipal> mPrincipal;
    // EditAction may be nested, for example, a command may be executed
    // from mutation event listener which is run while editor changes
    // the DOM tree.  In such case, we need to handle edit action separately.
    AutoEditActionDataSetter* mParentData;

    // Cached selection for AutoSelectionRestorer.
    SelectionState mSavedSelection;

    // Utility class object for maintaining preserved ranges.
    RangeUpdater mRangeUpdater;

    // The data should be set to InputEvent.data.
    nsString mData;

    // The dataTransfer should be set to InputEvent.dataTransfer.
    RefPtr<DataTransfer> mDataTransfer;

    // They are used for result of InputEvent.getTargetRanges() of beforeinput.
    OwningNonNullStaticRangeArray mTargetRanges;

    // Start point where spell checker should check from.  This is used only
    // by TextEditor.
    EditorDOMPoint mSpellCheckRestartPoint;

    // Different from mTopLevelEditSubAction, its data should be stored only
    // in the most ancestor AutoEditActionDataSetter instance since we don't
    // want to pay the copying cost and sync cost.
    TopLevelEditSubActionData mTopLevelEditSubActionData;

    // Different from mTopLevelEditSubActionData, this stores temporaly data
    // for current edit sub action.
    EditSubActionData mEditSubActionData;

    // mEditAction and mRawEditActions stores edit action.  The difference of
    // them is, if and only if edit actions are nested and parent edit action
    // is one of trying to edit something, but nested one is not so, it's
    // overwritten by the parent edit action.
    EditAction mEditAction;
    EditAction mRawEditAction;

    // Different from its data, you can refer "current" AutoEditActionDataSetter
    // instance's mTopLevelEditSubAction member since it's copied from the
    // parent instance at construction and it's always cleared before this
    // won't be overwritten and cleared before destruction.
    EditSubAction mTopLevelEditSubAction = EditSubAction::eNone;

    EDirection mDirectionOfTopLevelEditSubAction = nsIEditor::eNone;

    bool mAborted = false;

    // Set to true when this handles "beforeinput" event dispatching.  Note
    // that even if "beforeinput" event shouldn't be dispatched for this,
    // instance, this is set to true when it's considered.
    bool mHasTriedToDispatchBeforeInputEvent = false;
    // Set to true if "beforeinput" event was dispatched and it's canceled.
    bool mBeforeInputEventCanceled = false;
    // Set to true if `beforeinput` event must not be cancelable even if
    // its inputType is defined as cancelable by the standards.
    bool mMakeBeforeInputEventNonCancelable = false;
    // Set to true when the edit action handler tries to dispatch a clipboard
    // event.
    bool mHasTriedToDispatchClipboardEvent = false;
    // The editor instance may be destroyed once temporarily if `document.write`
    // etc runs.  In such case, we should mark this flag of being handled
    // edit action.
    bool mEditorWasDestroyedDuringHandlingEditAction;
    // This is set to `true` if the editor was destroyed but now, it's
    // initialized again.
    bool mEditorWasReinitialized;
    // This is set before dispatching `input` event and notifying editor
    // observers.
    bool mHandled = false;
    // Whether the editor is dispatching a `beforeinput` or `input` event.
    bool mDispatchingInputEvent = false;

#ifdef DEBUG
    mutable bool mHasCanHandleChecked = false;
#endif  // #ifdef DEBUG
  };

  void UpdateEditActionData(const nsAString& aData) {
    mEditActionData->SetData(aData);
  }

  void NotifyOfDispatchingClipboardEvent() {
    MOZ_ASSERT(mEditActionData);
    mEditActionData->NotifyOfDispatchingClipboardEvent();
  }

 protected:  // May be called by friends.
  /****************************************************************************
   * Some friend classes are allowed to call the following protected methods.
   * However, those methods won't prepare caches of some objects which are
   * necessary for them.  So, if you call them from friend classes, you need
   * to make sure that AutoEditActionDataSetter is created.
   ****************************************************************************/

  bool IsEditActionCanceled() const {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData->IsCanceled();
  }

  bool ShouldAlreadyHaveHandledBeforeInputEventDispatching() const {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData
        ->ShouldAlreadyHaveHandledBeforeInputEventDispatching();
  }

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MaybeDispatchBeforeInputEvent() {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData->MaybeDispatchBeforeInputEvent();
  }

  void MarkAsBeforeInputHasBeenDispatched() {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData->MarkAsBeforeInputHasBeenDispatched();
  }

  bool HasTriedToDispatchBeforeInputEvent() const {
    return mEditActionData &&
           mEditActionData->HasTriedToDispatchBeforeInputEvent();
  }

  bool IsEditActionDataAvailable() const {
    return mEditActionData && mEditActionData->IsDataAvailable();
  }

  bool IsTopLevelEditSubActionDataAvailable() const {
    return mEditActionData && !!GetTopLevelEditSubAction();
  }

  bool IsEditActionAborted() const {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData->IsAborted();
  }

  nsresult GetDataFromDataTransferOrClipboard(
      DataTransfer* aDataTransfer, nsITransferable* aTransferable,
      nsIClipboard::ClipboardType aClipboardType) const;

  /**
   * SelectionRef() returns cached normal Selection.  This is pretty faster than
   * EditorBase::GetSelection() if available.
   * Note that this never crash unless public methods ignore the result of
   * AutoEditActionDataSetter::CanHandle() and keep handling edit action but any
   * methods should stop handling edit action if it returns false.
   */
  MOZ_KNOWN_LIVE Selection& SelectionRef() const {
    MOZ_ASSERT(mEditActionData);
    MOZ_ASSERT(mEditActionData->SelectionRef().GetType() ==
               SelectionType::eNormal);
    return mEditActionData->SelectionRef();
  }

  // Return the Text if and only if we're a TextEditor instance.  It's cached
  // while we're handling an edit action, so, this stores the latest value even
  // after we have been destroyed.
  Text* GetCachedTextNode() {
    MOZ_ASSERT(IsTextEditor());
    return mEditActionData ? mEditActionData->GetCachedTextNode() : nullptr;
  }

  // Return the Text if and only if we're a TextEditor instance.  It's cached
  // while we're handling an edit action, so, this stores the latest value even
  // after we have been destroyed.
  const Text* GetCachedTextNode() const {
    MOZ_ASSERT(IsTextEditor());
    return const_cast<EditorBase*>(this)->GetCachedTextNode();
  }

  nsIPrincipal* GetEditActionPrincipal() const {
    MOZ_ASSERT(mEditActionData);
    return mEditActionData->GetPrincipal();
  }

  /**
   * GetEditAction() returns EditAction which is being handled.  If some
   * edit actions are nested, this returns the innermost edit action.
   */
  EditAction GetEditAction() const {
    return mEditActionData ? mEditActionData->GetEditAction()
                           : EditAction::eNone;
  }

  /**
   * GetInputEventData() returns inserting or inserted text value with
   * current edit action.  The result is proper for InputEvent.data value.
   */
  const nsString& GetInputEventData() const {
    return mEditActionData ? mEditActionData->GetData() : VoidString();
  }

  /**
   * GetInputEventDataTransfer() returns inserting or inserted transferable
   * content with current edit action.  The result is proper for
   * InputEvent.dataTransfer value.
   */
  DataTransfer* GetInputEventDataTransfer() const {
    return mEditActionData ? mEditActionData->GetDataTransfer() : nullptr;
  }

  /**
   * GetTopLevelEditSubAction() returns the top level edit sub-action.
   * For example, if selected content is being replaced with inserted text,
   * while removing selected content, the top level edit sub-action may be
   * EditSubAction::eDeleteSelectedContent.  However, while inserting new
   * text, the top level edit sub-action may be EditSubAction::eInsertText.
   * So, this result means what we are doing right now unless you're looking
   * for a case which the method is called via mutation event listener or
   * selectionchange event listener which are fired while handling the edit
   * sub-action.
   */
  EditSubAction GetTopLevelEditSubAction() const {
    return mEditActionData ? mEditActionData->GetTopLevelEditSubAction()
                           : EditSubAction::eNone;
  }

  /**
   * GetDirectionOfTopLevelEditSubAction() returns direction which user
   * intended for doing the edit sub-action.
   */
  EDirection GetDirectionOfTopLevelEditSubAction() const {
    return mEditActionData
               ? mEditActionData->GetDirectionOfTopLevelEditSubAction()
               : eNone;
  }

  /**
   * SavedSelection() returns reference to saved selection which are
   * stored by AutoSelectionRestorer.
   */
  SelectionState& SavedSelectionRef() {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->SavedSelectionRef();
  }
  const SelectionState& SavedSelectionRef() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->SavedSelectionRef();
  }

  RangeUpdater& RangeUpdaterRef() {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->RangeUpdaterRef();
  }
  const RangeUpdater& RangeUpdaterRef() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->RangeUpdaterRef();
  }

  template <typename PT, typename CT>
  void SetSpellCheckRestartPoint(const EditorDOMPointBase<PT, CT>& aPoint) {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->SetSpellCheckRestartPoint(aPoint);
  }

  void ClearSpellCheckRestartPoint() {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->ClearSpellCheckRestartPoint();
  }

  const EditorDOMPoint& GetSpellCheckRestartPoint() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->GetSpellCheckRestartPoint();
  }

  const TopLevelEditSubActionData& TopLevelEditSubActionDataRef() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->TopLevelEditSubActionDataRef();
  }
  TopLevelEditSubActionData& TopLevelEditSubActionDataRef() {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->TopLevelEditSubActionDataRef();
  }

  const EditSubActionData& EditSubActionDataRef() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->EditSubActionDataRef();
  }
  EditSubActionData& EditSubActionDataRef() {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return mEditActionData->EditSubActionDataRef();
  }

  /**
   * GetFirstIMESelectionStartPoint() and GetLastIMESelectionEndPoint() returns
   * start of first IME selection range or end of last IME selection range if
   * there is.  Otherwise, returns non-set DOM point.
   */
  template <typename EditorDOMPointType>
  EditorDOMPointType GetFirstIMESelectionStartPoint() const;
  template <typename EditorDOMPointType>
  EditorDOMPointType GetLastIMESelectionEndPoint() const;

  /**
   * IsSelectionRangeContainerNotContent() returns true if one of container
   * of selection ranges is not a content node, i.e., a Document node.
   */
  bool IsSelectionRangeContainerNotContent() const;

  /**
   * OnInputText() is called when user inputs text with keyboard or something.
   *
   * @param aStringToInsert     The string to insert.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  OnInputText(const nsAString& aStringToInsert);

  enum class InsertTextFor {
    NormalText,
    CompositionStart,
    CompositionUpdate,
    CompositionEnd,
    CompositionStartAndEnd,
  };
  friend inline std::ostream& operator<<(std::ostream& aStream,
                                         const InsertTextFor& aPurpose) {
    switch (aPurpose) {
      case InsertTextFor::NormalText:
        return aStream << "InsertTextFor::NormalText";
      case InsertTextFor::CompositionStart:
        return aStream << "InsertTextFor::CompositionStart";
      case InsertTextFor::CompositionUpdate:
        return aStream << "InsertTextFor::CompositionUpdate";
      case InsertTextFor::CompositionEnd:
        return aStream << "InsertTextFor::CompositionEnd";
      case InsertTextFor::CompositionStartAndEnd:
        return aStream << "InsertTextFor::CompositionStartAndEnd";
    }
    return aStream << "<illegal value>";
  }
  [[nodiscard]] static bool InsertingTextForComposition(
      InsertTextFor aPurpose) {
    return aPurpose != InsertTextFor::NormalText;
  }
  [[nodiscard]] static bool InsertingTextForExtantComposition(
      InsertTextFor aPurpose) {
    return aPurpose == InsertTextFor::CompositionUpdate ||
           aPurpose == InsertTextFor::CompositionEnd;
  }
  [[nodiscard]] static bool InsertingTextForStartingComposition(
      InsertTextFor aPurpose) {
    return aPurpose == InsertTextFor::CompositionStart ||
           aPurpose == InsertTextFor::CompositionStartAndEnd;
  }
  [[nodiscard]] static bool InsertingTextForCommittingComposition(
      InsertTextFor aPurpose) {
    return aPurpose == InsertTextFor::CompositionEnd ||
           aPurpose == InsertTextFor::CompositionStartAndEnd;
  }
  [[nodiscard]] static bool NothingToDoIfInsertingEmptyText(
      InsertTextFor aPurpose) {
    return aPurpose == InsertTextFor::NormalText ||
           aPurpose == InsertTextFor::CompositionStartAndEnd;
  }

  /**
   * InsertTextAsSubAction() inserts aStringToInsert at selection.  This
   * should be used for handling it as an edit sub-action.
   *
   * @param aStringToInsert     The string to insert.
   * @param aPurpose            Specify the purpose to insert text.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertTextAsSubAction(
      const nsAString& aStringToInsert, InsertTextFor aPurpose);

  /**
   * Insert aStringToInsert to aPointToInsert or better insertion point around
   * it.  If aPointToInsert isn't in a text node, this method looks for the
   * nearest point in a text node with TextEditor::FindBetterInsertionPoint()
   * or EditorDOMPoint::GetPointInTextNodeIfPointingAroundTextNode().
   * If there is no text node, this creates new text node and put
   * aStringToInsert to it.
   *
   * @param aStringToInsert The string to insert.
   * @param aPointToInsert  The point to insert aStringToInsert.
   *                        Must be valid DOM point.
   * @param aInsertTextTo   Whether forcibly creates a new `Text` node in
   *                        specific condition or use existing `Text` if
   *                        available.
   */
  enum class InsertTextTo {
    SpecifiedPoint,
    ExistingTextNodeIfAvailable,
    ExistingTextNodeIfAvailableAndNotStart,
    AlwaysCreateNewTextNode
  };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual Result<InsertTextResult, nsresult>
  InsertTextWithTransaction(const nsAString& aStringToInsert,
                            const EditorDOMPoint& aPointToInsert,
                            InsertTextTo aInsertTextTo);

  /**
   * Compute insertion point from aPoint and aInsertTextTo.
   */
  [[nodiscard]] EditorDOMPoint ComputePointToInsertText(
      const EditorDOMPoint& aPoint, InsertTextTo aInsertTextTo) const;

  /**
   * Insert aStringToInsert to aPointToInsert.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<InsertTextResult, nsresult>
  InsertTextIntoTextNodeWithTransaction(
      const nsAString& aStringToInsert,
      const EditorDOMPointInText& aPointToInsert);

  /**
   * SetTextNodeWithoutTransaction() is optimized path to set new value to
   * the text node directly and without transaction.  This is used when
   * setting `<input>.value` and `<textarea>.value`.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetTextNodeWithoutTransaction(const nsAString& aString, Text& aTextNode);

  /**
   * DeleteNodeWithTransaction() removes aContent from the DOM tree.
   *
   * @param aContent    The node which will be removed form the DOM tree.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DeleteNodeWithTransaction(nsIContent& aContent);

  /**
   * InsertNodeWithTransaction() inserts aContentToInsert before the child
   * specified by aPointToInsert.
   *
   * @param aContentToInsert    The node to be inserted.
   * @param aPointToInsert      The insertion point of aContentToInsert.
   *                            If this refers end of the container, the
   *                            transaction will append the node to the
   *                            container.  Otherwise, will insert the node
   *                            before child node referred by this.
   * @return                    If succeeded, returns the new content node and
   *                            point to put caret.
   */
  template <typename ContentNodeType>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT
      Result<CreateNodeResultBase<ContentNodeType>, nsresult>
      InsertNodeWithTransaction(ContentNodeType& aContentToInsert,
                                const EditorDOMPoint& aPointToInsert);

  /**
   * InsertPaddingBRElementForEmptyLastLineWithTransaction() creates a padding
   * <br> element with setting flags to NS_PADDING_FOR_EMPTY_LAST_LINE and
   * inserts it around aPointToInsert.
   *
   * @param aPointToInsert      The DOM point where should be <br> node inserted
   *                            before.
   * @return                    If succeeded, returns the new <br> element and
   *                            point to put caret around it.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertPaddingBRElementForEmptyLastLineWithTransaction(
      const EditorDOMPoint& aPointToInsert);

  enum class BRElementType {
    Normal,
    PaddingForEmptyEditor,
    PaddingForEmptyLastLine
  };
  friend inline auto format_as(const BRElementType& aType) {
    constexpr const char* sNames[] = {"Normal", "PaddingForEmptyEditor",
                                      "PaddingForEmptyLastLine"};
    return std::string(sNames[static_cast<size_t>(aType)]);
  }
  friend inline std::ostream& operator<<(std::ostream& aStream,
                                         const BRElementType& aType) {
    return aStream << format_as(aType);
  }

  /**
   * Updates the type of aBRElement.  If it will be hidden or shown from
   * IMEContentObserver and ContentEventHandler points of view, this temporarily
   * removes the node and reconnect to the same position.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  UpdateBRElementType(dom::HTMLBRElement& aBRElement, BRElementType aNewType);

  /**
   * Create and insert a line break to aPointToInsert.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertBRElement(WithTransaction aWithTransaction,
                  BRElementType aBRElementType,
                  const EditorDOMPoint& aPointToInsert);

  /**
   * CloneAttributesWithTransaction() clones all attributes from
   * aSourceElement to aDestElement after removing all attributes in
   * aDestElement.
   */
  MOZ_CAN_RUN_SCRIPT void CloneAttributesWithTransaction(
      Element& aDestElement, Element& aSourceElement);

  /**
   * CloneAttributeWithTransaction() copies aAttribute of aSourceElement to
   * aDestElement.  If aSourceElement doesn't have aAttribute, this removes
   * aAttribute from aDestElement.
   *
   * @param aAttribute          Attribute name to be cloned.
   * @param aDestElement        Element node which will be set aAttribute or
   *                            whose aAttribute will be removed.
   * @param aSourceElement      Element node which provides the value of
   *                            aAttribute in aDestElement.
   */
  MOZ_CAN_RUN_SCRIPT nsresult CloneAttributeWithTransaction(
      nsAtom& aAttribute, Element& aDestElement, Element& aSourceElement);

  /**
   * RemoveAttributeWithTransaction() removes aAttribute from aElement.
   *
   * @param aElement        Element node which will lose aAttribute.
   * @param aAttribute      Attribute name to be removed from aElement.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  RemoveAttributeWithTransaction(Element& aElement, nsAtom& aAttribute);

  MOZ_CAN_RUN_SCRIPT virtual nsresult RemoveAttributeOrEquivalent(
      Element* aElement, nsAtom* aAttribute, bool aSuppressTransaction) = 0;

  /**
   * SetAttributeWithTransaction() sets aAttribute of aElement to aValue.
   *
   * @param aElement        Element node which will have aAttribute.
   * @param aAttribute      Attribute name to be set.
   * @param aValue          Attribute value be set to aAttribute.
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetAttributeWithTransaction(
      Element& aElement, nsAtom& aAttribute, const nsAString& aValue);

  MOZ_CAN_RUN_SCRIPT virtual nsresult SetAttributeOrEquivalent(
      Element* aElement, nsAtom* aAttribute, const nsAString& aValue,
      bool aSuppressTransaction) = 0;

  /**
   * Method to replace certain CreateElementNS() calls.
   *
   * @param aTag        Tag you want.
   */
  already_AddRefed<Element> CreateHTMLContent(const nsAtom* aTag) const;

  /**
   * Creates text node which is marked as "maybe modified frequently" and
   * "maybe masked" if this is a password editor.
   */
  already_AddRefed<nsTextNode> CreateTextNode(const nsAString& aData) const;

  /**
   * DoInsertText(), DoDeleteText(), DoReplaceText() and DoSetText() are
   * wrapper of `CharacterData::InsertData()`, `CharacterData::DeleteData()`,
   * `CharacterData::ReplaceData()` and `CharacterData::SetData()`.
   */
  MOZ_CAN_RUN_SCRIPT void DoInsertText(dom::Text& aText, uint32_t aOffset,
                                       const nsAString& aStringToInsert,
                                       ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void DoDeleteText(dom::Text& aText, uint32_t aOffset,
                                       uint32_t aCount, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void DoReplaceText(dom::Text& aText, uint32_t aOffset,
                                        uint32_t aCount,
                                        const nsAString& aStringToInsert,
                                        ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void DoSetText(dom::Text& aText,
                                    const nsAString& aStringToSet,
                                    ErrorResult& aRv);

  /**
   * Delete text in the range in aTextNode.  Use
   * `HTMLEditor::ReplaceTextWithTransaction` if you'll insert text there (and
   * if you want to use it in `TextEditor`, move it into `EditorBase`).
   *
   * @param aTextNode           The text node which should be modified.
   * @param aOffset             Start offset of removing text in aTextNode.
   * @param aLength             Length of removing text.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteTextWithTransaction(dom::Text& aTextNode, uint32_t aOffset,
                            uint32_t aLength);

  /**
   * MarkElementDirty() sets a special dirty attribute on the element.
   * Usually this will be called immediately after creating a new node.
   *
   * @param aElement    The element for which to insert formatting.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MarkElementDirty(Element& aElement);

  MOZ_CAN_RUN_SCRIPT nsresult
  DoTransactionInternal(nsITransaction* aTransaction);

  /**
   * Returns true if aNode is our root node.  The root is:
   * If TextEditor, the anonymous <div> element.
   * If HTMLEditor, a <body> element or the document element which may not be
   * editable if it's not in the design mode.
   */
  bool IsRoot(const nsINode* inNode) const;

  /**
   * Returns true if aNode is a descendant of our root node.
   * See the comment for IsRoot() for what the root node means.
   */
  bool IsDescendantOfRoot(const nsINode* inNode) const;

  /**
   * Returns true when inserting text should be a part of current composition.
   */
  bool ShouldHandleIMEComposition() const;

  template <typename EditorDOMPointType>
  EditorDOMPointType GetFirstSelectionStartPoint() const;
  template <typename EditorDOMPointType>
  EditorDOMPointType GetFirstSelectionEndPoint() const;

  static nsresult GetEndChildNode(const Selection& aSelection,
                                  nsIContent** aEndNode);

  template <typename PT, typename CT>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CollapseSelectionTo(const EditorDOMPointBase<PT, CT>& aPoint) const {
    // We don't need to throw exception directly for a failure of updating
    // selection.  Therefore, let's use IgnoredErrorResult for the performance.
    IgnoredErrorResult error;
    CollapseSelectionTo(aPoint, error);
    return error.StealNSResult();
  }

  template <typename PT, typename CT>
  MOZ_CAN_RUN_SCRIPT void CollapseSelectionTo(
      const EditorDOMPointBase<PT, CT>& aPoint, ErrorResult& aRv) const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    MOZ_ASSERT(!aRv.Failed());

    if (aPoint.GetInterlinePosition() != InterlinePosition::Undefined) {
      if (MOZ_UNLIKELY(NS_FAILED(SelectionRef().SetInterlinePosition(
              aPoint.GetInterlinePosition())))) {
        NS_WARNING("Selection::SetInterlinePosition() failed");
        aRv.Throw(NS_ERROR_FAILURE);
        return;
      }
    }

    SelectionRef().CollapseInLimiter(aPoint, aRv);
    if (MOZ_UNLIKELY(Destroyed())) {
      NS_WARNING("Selection::CollapseInLimiter() caused destroying the editor");
      aRv.Throw(NS_ERROR_EDITOR_DESTROYED);
      return;
    }
    NS_WARNING_ASSERTION(!aRv.Failed(),
                         "Selection::CollapseInLimiter() failed");
  }

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CollapseSelectionToStartOf(nsINode& aNode) const {
    return CollapseSelectionTo(EditorRawDOMPoint(&aNode, 0u));
  }

  MOZ_CAN_RUN_SCRIPT void CollapseSelectionToStartOf(nsINode& aNode,
                                                     ErrorResult& aRv) const {
    CollapseSelectionTo(EditorRawDOMPoint(&aNode, 0u), aRv);
  }

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CollapseSelectionToEndOf(nsINode& aNode) const {
    return CollapseSelectionTo(EditorRawDOMPoint::AtEndOf(aNode));
  }

  MOZ_CAN_RUN_SCRIPT void CollapseSelectionToEndOf(nsINode& aNode,
                                                   ErrorResult& aRv) const {
    CollapseSelectionTo(EditorRawDOMPoint::AtEndOf(aNode), aRv);
  }

  /**
   * AllowsTransactionsToChangeSelection() returns true if editor allows any
   * transactions to change Selection.  Otherwise, transactions shouldn't
   * change Selection.
   */
  inline bool AllowsTransactionsToChangeSelection() const {
    return mAllowsTransactionsToChangeSelection;
  }

  /**
   * MakeThisAllowTransactionsToChangeSelection() with true makes this editor
   * allow transactions to change Selection.  Otherwise, i.e., with false,
   * makes this editor not allow transactions to change Selection.
   */
  inline void MakeThisAllowTransactionsToChangeSelection(bool aAllow) {
    mAllowsTransactionsToChangeSelection = aAllow;
  }

  nsresult HandleInlineSpellCheck(
      const EditorDOMPoint& aPreviouslySelectedStart,
      const dom::AbstractRange* aRange = nullptr);

  /**
   * Whether the editor is active on the DOM window.  Note that when this
   * returns true but GetFocusedElement() returns null, it means that this
   * editor was focused when the DOM window was active.
   */
  virtual bool IsActiveInDOMWindow() const;

  /**
   * HideCaret() hides caret with nsCaret::AddForceHide() or may show carent
   * with nsCaret::RemoveForceHide().  This does NOT set visibility of
   * nsCaret.  Therefore, this is stateless.
   */
  void HideCaret(bool aHide);

 protected:  // Edit sub-action handler
  /**
   * AutoCaretBidiLevelManager() computes bidi level of caret, deleting
   * character(s) from aPointAtCaret at construction.  Then, if you'll
   * need to extend the selection, you should calls `UpdateCaretBidiLevel()`,
   * then, this class may update caret bidi level for you if it's required.
   */
  class MOZ_RAII AutoCaretBidiLevelManager final {
   public:
    /**
     * @param aEditorBase         The editor.
     * @param aPointAtCaret       Collapsed `Selection` point.
     * @param aDirectionAndAmount The direction and amount to delete.
     */
    template <typename PT, typename CT>
    AutoCaretBidiLevelManager(const EditorBase& aEditorBase,
                              nsIEditor::EDirection aDirectionAndAmount,
                              const EditorDOMPointBase<PT, CT>& aPointAtCaret);

    /**
     * Failed() returns true if the constructor failed to handle the bidi
     * information.
     */
    bool Failed() const { return mFailed; }

    /**
     * Canceled() returns true if when the caller should stop deleting
     * characters since caret position is not visually adjacent the deleting
     * characters and user does not wand to delete them in that case.
     */
    bool Canceled() const { return mCanceled; }

    /**
     * MaybeUpdateCaretBidiLevel() may update caret bidi level and schedule to
     * paint it if they are necessary.
     */
    void MaybeUpdateCaretBidiLevel(const EditorBase& aEditorBase) const;

   private:
    Maybe<mozilla::intl::BidiEmbeddingLevel> mNewCaretBidiLevel;
    bool mFailed = false;
    bool mCanceled = false;
  };

  /**
   * UndefineCaretBidiLevel() resets bidi level of the caret.
   */
  void UndefineCaretBidiLevel() const;

  /**
   * Flushing pending notifications if nsFrameSelection requires the latest
   * layout information to compute deletion range.  This may destroy the
   * editor instance itself.  When this returns false, don't keep doing
   * anything.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT bool
  FlushPendingNotificationsIfToHandleDeletionWithFrameSelection(
      nsIEditor::EDirection aDirectionAndAmount) const;

  /**
   * DeleteSelectionAsSubAction() removes selection content or content around
   * caret with transactions.  This should be used for handling it as an
   * edit sub-action.
   *
   * @param aDirectionAndAmount How much range should be removed.
   * @param aStripWrappers      Whether the parent blocks should be removed
   *                            when they become empty.  If this instance is
   *                            a TextEditor, Must be nsIEditor::eNoStrip.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DeleteSelectionAsSubAction(nsIEditor::EDirection aDirectionAndAmount,
                             nsIEditor::EStripWrappers aStripWrappers);

  /**
   * This method handles "delete selection" commands.
   *
   * @param aDirectionAndAmount Direction of the deletion.
   * @param aStripWrappers      Must be nsIEditor::eNoStrip if this is a
   *                            TextEditor instance.  Otherwise,
   *                            nsIEditor::eStrip is also valid.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual Result<EditActionResult, nsresult>
  HandleDeleteSelection(nsIEditor::EDirection aDirectionAndAmount,
                        nsIEditor::EStripWrappers aStripWrappers) = 0;

  /**
   * ReplaceSelectionAsSubAction() replaces selection with aString.
   *
   * @param aString    The string to replace.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  ReplaceSelectionAsSubAction(const nsAString& aString);

  /**
   * HandleInsertText() handles inserting text at selection.
   *
   * @param aInsertionString    String to be inserted at selection.
   * @param aPurpose            Specify the purpose of inserting text.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual Result<EditActionResult, nsresult>
  HandleInsertText(const nsAString& aInsertionString,
                   InsertTextFor aPurpose) = 0;

  /**
   * InsertWithQuotationsAsSubAction() inserts aQuotedText with appending ">"
   * to start of every line.
   *
   * @param aQuotedText         String to insert.  This will be quoted by ">"
   *                            automatically.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual nsresult
  InsertWithQuotationsAsSubAction(const nsAString& aQuotedText) = 0;

  /**
   * PrepareInsertContent() is a helper method of InsertTextAt(),
   * HTMLEditor::HTMLWithContextInserter::Run().  They insert content coming
   * from clipboard or drag and drop.  Before that, they may need to remove
   * selected contents and adjust selection.  This does them instead.
   *
   * @param aPointToInsert      Point to insert.  Must be set.  Callers
   *                            shouldn't use this instance after calling this
   *                            method because this method may cause changing
   *                            the DOM tree and Selection.
   */
  enum class DeleteSelectedContent : bool {
    No,   // Don't delete selection
    Yes,  // Delete selected content
  };
  MOZ_CAN_RUN_SCRIPT nsresult
  PrepareToInsertContent(const EditorDOMPoint& aPointToInsert,
                         DeleteSelectedContent aDeleteSelectedContent);

  /**
   * InsertTextAt() inserts aStringToInsert at aPointToInsert.
   *
   * @param aStringToInsert     The string which you want to insert.
   * @param aPointToInsert      The insertion point.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertTextAt(
      const nsAString& aStringToInsert, const EditorDOMPoint& aPointToInsert,
      DeleteSelectedContent aDeleteSelectedContent);

  /**
   * Return whether the data is safe to insert as the source and destination
   * principals match, or we are in a editor context where this doesn't matter.
   * Otherwise, the data must be sanitized first.
   */
  enum class SafeToInsertData : bool { No, Yes };
  SafeToInsertData IsSafeToInsertData(nsIPrincipal* aSourcePrincipal) const;

  /**
   * Routines for managing the preservation of selection across
   * various editor actions.
   */
  bool ArePreservingSelection() const;
  void PreserveSelectionAcrossActions();
  MOZ_CAN_RUN_SCRIPT nsresult RestorePreservedSelection();
  void StopPreservingSelection();

 protected:  // Called by helper classes.
  /**
   * OnStartToHandleTopLevelEditSubAction() is called when
   * GetTopLevelEditSubAction() is EditSubAction::eNone and somebody starts to
   * handle aEditSubAction.
   *
   * @param aTopLevelEditSubAction              Top level edit sub action which
   *                                            will be handled soon.
   * @param aDirectionOfTopLevelEditSubAction   Direction of aEditSubAction.
   */
  MOZ_CAN_RUN_SCRIPT virtual void OnStartToHandleTopLevelEditSubAction(
      EditSubAction aTopLevelEditSubAction,
      nsIEditor::EDirection aDirectionOfTopLevelEditSubAction,
      ErrorResult& aRv);

  /**
   * OnEndHandlingTopLevelEditSubAction() is called after
   * SetTopLevelEditSubAction() is handled.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult OnEndHandlingTopLevelEditSubAction();

  /**
   * OnStartToHandleEditSubAction() and OnEndHandlingEditSubAction() are called
   * when starting to handle an edit sub action and ending handling an edit
   * sub action.
   */
  void OnStartToHandleEditSubAction() { EditSubActionDataRef().Clear(); }
  void OnEndHandlingEditSubAction() { EditSubActionDataRef().Clear(); }

  /**
   * (Begin|End)PlaceholderTransaction() are called by AutoPlaceholderBatch.
   * This set of methods are similar to the (Begin|End)Transaction(), but do
   * not use the transaction managers batching feature.  Instead we use a
   * placeholder transaction to wrap up any further transaction while the
   * batch is open.  The advantage of this is that placeholder transactions
   * can later merge, if needed.  Merging is unavailable between transaction
   * manager batches.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void BeginPlaceholderTransaction(
      nsStaticAtom& aTransactionName, const char* aRequesterFuncName);
  enum class ScrollSelectionIntoView { No, Yes };
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void EndPlaceholderTransaction(
      ScrollSelectionIntoView aScrollSelectionIntoView,
      const char* aRequesterFuncName);

  void BeginUpdateViewBatch(const char* aRequesterFuncName);
  MOZ_CAN_RUN_SCRIPT void EndUpdateViewBatch(const char* aRequesterFuncName);

  /**
   * Used by HTMLEditor::AutoTransactionBatch, nsIEditor::BeginTransaction
   * and nsIEditor::EndTransation.  After calling BeginTransactionInternal(),
   * all transactions will be treated as an atomic transaction.  I.e., two or
   * more transactions are undid once.
   * XXX What's the difference with PlaceholderTransaction? Should we always
   *     use it instead?
   */
  MOZ_CAN_RUN_SCRIPT void BeginTransactionInternal(
      const char* aRequesterFuncName);
  MOZ_CAN_RUN_SCRIPT void EndTransactionInternal(
      const char* aRequesterFuncName);

 protected:  // Shouldn't be used by friend classes
  /**
   * The default destructor. This should suffice. Should this be pure virtual
   * for someone to derive from the EditorBase later? I don't believe so.
   */
  virtual ~EditorBase();

  /**
   * @param aDocument   The dom document interface being observed
   * @param aRootElement
   *                    This is the root of the editable section of this
   *                    document. If it is null then we get root from document
   *                    body.
   * @param aSelectionController
   *                    The selection controller of selections which will be
   *                    used in this editor.
   * @param aFlags      Some of nsIEditor::eEditor*Mask flags.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  InitInternal(Document& aDocument, Element* aRootElement,
               nsISelectionController& aSelectionController, uint32_t aFlags);

  /**
   * PostCreateInternal() should be called after InitInternal(), and is the time
   * that the editor tells its documentStateObservers that the document has been
   * created.
   */
  MOZ_CAN_RUN_SCRIPT nsresult PostCreateInternal();

  /**
   * PreDestroyInternal() is called before the editor goes away, and gives the
   * editor a chance to tell its documentStateObservers that the document is
   * going away.
   */
  MOZ_CAN_RUN_SCRIPT virtual void PreDestroyInternal();

  MOZ_ALWAYS_INLINE EditorType GetEditorType() const {
    return mIsHTMLEditorClass ? EditorType::HTML : EditorType::Text;
  }

  /**
   * Check whether the caller can keep handling focus event.
   *
   * @param aOriginalEventTargetNode    The original event target of the focus
   *                                    event.
   */
  [[nodiscard]] bool CanKeepHandlingFocusEvent(
      const nsINode& aOriginalEventTargetNode) const;

  /**
   * If this editor has skipped spell checking and not yet flushed, this runs
   * the spell checker.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult FlushPendingSpellCheck();

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult EnsureEmptyTextFirstChild();

  int32_t WrapWidth() const { return mWrapColumn; }

  /**
   * ToGenericNSResult() computes proper nsresult value for the editor users.
   * This should be used only when public methods return result of internal
   * methods.
   */
  static inline nsresult ToGenericNSResult(nsresult aRv) {
    switch (aRv) {
      // If the editor is destroyed while handling an edit action, editor needs
      // to stop handling it.  However, editor throw exception in this case
      // because Chrome does not throw exception even in this case.
      case NS_ERROR_EDITOR_DESTROYED:
        return NS_OK;
      // If editor meets unexpected DOM tree due to modified by mutation event
      // listener, editor needs to stop handling it.  However, editor shouldn't
      // return error for the users because Chrome does not throw exception in
      // this case.
      case NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE:
        return NS_OK;
      // If the editing action is canceled by event listeners, editor needs
      // to stop handling it.  However, editor shouldn't return error for
      // the callers but they should be able to distinguish whether it's
      // canceled or not.  Although it's DOM specific code, let's return
      // DOM_SUCCESS_DOM_NO_OPERATION here.
      case NS_ERROR_EDITOR_ACTION_CANCELED:
        return NS_SUCCESS_DOM_NO_OPERATION;
      // If there is no selection range or editable selection ranges, editor
      // needs to stop handling it.  However, editor shouldn't return error for
      // the callers to avoid throwing exception.  However, they may want to
      // check whether it works or not.  Therefore, we should return
      // NS_SUCCESS_DOM_NO_OPERATION instead.
      case NS_ERROR_EDITOR_NO_EDITABLE_RANGE:
        return NS_SUCCESS_DOM_NO_OPERATION;
      // If CreateNodeResultBase::SuggestCaretPointTo etc is called with
      // SuggestCaret::AndIgnoreTrivialErrors and CollapseSelectionTo returns
      // non-critical error e.g., not NS_ERROR_EDITOR_DESTROYED, it returns
      // this success code instead of actual error code for making the caller
      // handle the case easier.  Therefore, this should be mapped to NS_OK
      // for the users of editor.
      case NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR:
        return NS_OK;
      default:
        return aRv;
    }
  }

  /**
   * GetDocumentCharsetInternal() returns charset of the document.
   */
  nsresult GetDocumentCharsetInternal(nsACString& aCharset) const;

  /**
   * ComputeValueInternal() computes string value of this editor for given
   * format.  This may be too expensive if it's in hot path.
   *
   * @param aFormatType             MIME type like "text/plain".
   * @param aDocumentEncoderFlags   Flags of nsIDocumentEncoder.
   * @param aCharset                Encoding of the document.
   */
  nsresult ComputeValueInternal(const nsAString& aFormatType,
                                uint32_t aDocumentEncoderFlags,
                                nsAString& aOutputString) const;

  /**
   * GetAndInitDocEncoder() returns a document encoder instance for aFormatType
   * after initializing it.  The result may be cached for saving recreation
   * cost.
   *
   * @param aFormatType             MIME type like "text/plain".
   * @param aDocumentEncoderFlags   Flags of nsIDocumentEncoder.
   * @param aCharset                Encoding of the document.
   */
  already_AddRefed<nsIDocumentEncoder> GetAndInitDocEncoder(
      const nsAString& aFormatType, uint32_t aDocumentEncoderFlags,
      const nsACString& aCharset) const;

  /**
   * EnsurePaddingBRElementInMultilineEditor() creates a padding `<br>` element
   * at end of multiline text editor.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  EnsurePaddingBRElementInMultilineEditor();

  /**
   * SelectAllInternal() should be used instead of SelectAll() in editor
   * because SelectAll() creates AutoEditActionSetter but we should avoid
   * to create it as far as possible.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult SelectAllInternal();

  nsresult DetermineCurrentDirection();

  /**
   * DispatchInputEvent() dispatches an "input" event synchronously or
   * asynchronously if it's not safe to dispatch.
   */
  MOZ_CAN_RUN_SCRIPT void DispatchInputEvent();

  /**
   * Return true if it's NOT blocked by the pref to dispatch `input` event
   * immediately before `compositionend`.
   */
  [[nodiscard]] bool CanDispatchInputEventBeforeCompositionEnd() const;

  /**
   * Return true if it's NOT blocked by the pref to dispatch `input` event
   * immediately after `compositionend`.
   */
  [[nodiscard]] bool CanDispatchInputEventAfterCompositionEnd() const;

  /**
   * Called after a transaction is done successfully.
   */
  MOZ_CAN_RUN_SCRIPT void DoAfterDoTransaction(nsITransaction* aTransaction);

  /**
   * Called after a transaction is undone successfully.
   */

  MOZ_CAN_RUN_SCRIPT void DoAfterUndoTransaction();

  /**
   * Called after a transaction is redone successfully.
   */
  MOZ_CAN_RUN_SCRIPT void DoAfterRedoTransaction();

  /**
   * Tell the doc state listeners that the doc state has changed.
   */
  enum TDocumentListenerNotification {
    eDocumentCreated,
    eDocumentToBeDestroyed,
    eDocumentStateChanged
  };
  MOZ_CAN_RUN_SCRIPT nsresult
  NotifyDocumentListeners(TDocumentListenerNotification aNotificationType);

  /**
   * Make the given selection span the entire document.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult SelectEntireDocument() = 0;

  /**
   * Helper method for scrolling the selection into view after
   * an edit operation.
   *
   * Editor methods *should* call this method instead of the versions
   * in the various selection interfaces, since this makes sure that
   * the editor's sync/async settings for reflowing, painting, and scrolling
   * match.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  ScrollSelectionFocusIntoView() const;

  virtual nsresult InstallEventListeners();
  virtual void CreateEventListeners();
  void RemoveEventListeners();
  [[nodiscard]] bool IsListeningToEvents() const;

  /**
   * Called if and only if this editor is in readonly mode.
   */
  void HandleKeyPressEventInReadOnlyMode(
      WidgetKeyboardEvent& aKeyboardEvent) const;

  /**
   * Get the input event target. This might return null.
   */
  virtual already_AddRefed<Element> GetInputEventTargetElement() const = 0;

  /**
   * Return true if spellchecking should be enabled for this editor.
   */
  [[nodiscard]] bool GetDesiredSpellCheckState();

  [[nodiscard]] bool CanEnableSpellCheck() const {
    // Check for password/readonly/disabled, which are not spellchecked
    // regardless of DOM. Also, check to see if spell check should be skipped
    // or not.
    return !IsPasswordEditor() && !IsReadonly() && !ShouldSkipSpellCheck();
  }

  /**
   * InitializeSelectionAncestorLimit() is called by InitializeSelection().
   * When this is called, each implementation has to call
   * Selection::SetAncestorLimiter() with aAnotherLimit.
   *
   * @param aAncestorLimit      New ancestor limit of Selection.  This always
   *                            has parent node.  So, it's always safe to
   *                            call SetAncestorLimit() with this node.
   */
  MOZ_CAN_RUN_SCRIPT virtual void InitializeSelectionAncestorLimit(
      Element& aAncestorLimit) const;

  /**
   * Initializes selection and caret for the editor at getting focus.  If
   * aOriginalEventTargetNode isn't a host of the editor, i.e., the editor
   * doesn't get focus, this does nothing.
   *
   * @param aOriginalEventTargetNode    The original event target node of the
   *                                    focus event.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  InitializeSelection(const nsINode& aOriginalEventTargetNode);

  enum NotificationForEditorObservers {
    eNotifyEditorObserversOfEnd,
    eNotifyEditorObserversOfBefore,
    eNotifyEditorObserversOfCancel
  };
  MOZ_CAN_RUN_SCRIPT void NotifyEditorObservers(
      NotificationForEditorObservers aNotification);

  /**
   * HowToHandleCollapsedRange indicates how collapsed range should be treated.
   */
  enum class HowToHandleCollapsedRange {
    // Ignore collapsed range.
    Ignore,
    // Extend collapsed range for removing previous content.
    ExtendBackward,
    // Extend collapsed range for removing next content.
    ExtendForward,
  };

  static HowToHandleCollapsedRange HowToHandleCollapsedRangeFor(
      nsIEditor::EDirection aDirectionAndAmount) {
    switch (aDirectionAndAmount) {
      case nsIEditor::eNone:
        return HowToHandleCollapsedRange::Ignore;
      case nsIEditor::ePrevious:
        return HowToHandleCollapsedRange::ExtendBackward;
      case nsIEditor::eNext:
        return HowToHandleCollapsedRange::ExtendForward;
      case nsIEditor::ePreviousWord:
      case nsIEditor::eNextWord:
      case nsIEditor::eToBeginningOfLine:
      case nsIEditor::eToEndOfLine:
        // If the amount is word or
        // line,`AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor()`
        // must have already been extended collapsed ranges before.
        return HowToHandleCollapsedRange::Ignore;
    }
    MOZ_ASSERT_UNREACHABLE("Invalid nsIEditor::EDirection value");
    return HowToHandleCollapsedRange::Ignore;
  }

  /**
   * InsertDroppedDataTransferAsAction() inserts all data items in aDataTransfer
   * at aDroppedAt unless the editor is destroyed.
   *
   * @param aEditActionData     The edit action data whose edit action must be
   *                            EditAction::eDrop.
   * @param aDataTransfer       The data transfer object which is dropped.
   * @param aDroppedAt          The DOM tree position whether aDataTransfer
   *                            is dropped.
   * @param aSourcePrincipal    Principal of the source of the drag.
   *                            May be nullptr if it comes from another app
   *                            or process.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual nsresult
  InsertDroppedDataTransferAsAction(AutoEditActionDataSetter& aEditActionData,
                                    DataTransfer& aDataTransfer,
                                    const EditorDOMPoint& aDroppedAt,
                                    nsIPrincipal* aSourcePrincipal) = 0;

  /**
   * DeleteSelectionByDragAsAction() removes selection and dispatch "input"
   * event whose inputType is "deleteByDrag".
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DeleteSelectionByDragAsAction(bool aDispatchInputEvent);

  /**
   * DeleteRangeWithTransaction() removes content in aRangeToDelete or content
   * around collapsed aRangeToDelete with transactions and remove empty
   * inclusive ancestor inline elements of the collapsed range after removing
   * the contents.
   *
   * @param aDirectionAndAmount How much range should be removed.
   * @param aStripWrappers      Whether the parent blocks should be removed
   *                            when they become empty.
   *                            Note that this must be `nsIEditor::eNoStrip`
   *                            if this is a TextEditor because anyway it'll
   *                            be ignored.
   * @param aRangeToDelete     The range to delete content.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteRangeWithTransaction(nsIEditor::EDirection aDirectionAndAmount,
                             nsIEditor::EStripWrappers aStripWrappers,
                             nsRange& aRangeToDelete);

  /**
   * DeleteRangesWithTransaction() removes content in aRangesToDelete or content
   * around collapsed ranges in aRangesToDelete with transactions and remove
   * empty inclusive ancestor inline elements of collapsed ranges after
   * removing the contents.
   *
   * @param aDirectionAndAmount How much range should be removed.
   * @param aStripWrappers      Whether the parent blocks should be removed
   *                            when they become empty.
   *                            Note that this must be `nsIEditor::eNoStrip`
   *                            if this is a TextEditor because anyway it'll
   *                            be ignored.
   * @param aRangesToDelete     The ranges to delete content.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual Result<CaretPoint, nsresult>
  DeleteRangesWithTransaction(nsIEditor::EDirection aDirectionAndAmount,
                              nsIEditor::EStripWrappers aStripWrappers,
                              AutoClonedRangeArray& aRangesToDelete);

  /**
   * Create a transaction for delete the content in aRangesToDelete.
   * The result may include DeleteRangeTransaction (for deleting non-collapsed
   * range), DeleteNodeTransactions and DeleteTextTransactions (for deleting
   * collapsed range) as its children.
   *
   * @param aHowToHandleCollapsedRange
   *                            How to handle collapsed ranges.
   * @param aRangesToDelete     The ranges to delete content.
   */
  already_AddRefed<DeleteMultipleRangesTransaction>
  CreateTransactionForDeleteSelection(
      HowToHandleCollapsedRange aHowToHandleCollapsedRange,
      const AutoClonedRangeArray& aRangesToDelete);

  /**
   * Create a DeleteNodeTransaction or DeleteTextTransaction for removing a
   * nodes or some text around aRangeToDelete.
   *
   * @param aCollapsedRange     The range to be removed.  This must be
   *                            collapsed.
   * @param aHowToHandleCollapsedRange
   *                            How to handle aCollapsedRange.  Must
   *                            be HowToHandleCollapsedRange::ExtendBackward or
   *                            HowToHandleCollapsedRange::ExtendForward.
   */
  already_AddRefed<DeleteContentTransactionBase>
  CreateTransactionForCollapsedRange(
      const nsRange& aCollapsedRange,
      HowToHandleCollapsedRange aHowToHandleCollapsedRange);

  /**
   * ComputeInsertedRange() returns actual range modified by inserting string
   * in a text node.  If mutation event listener changed the text data, this
   * returns a range which covers all over the text data.
   */
  std::tuple<EditorDOMPointInText, EditorDOMPointInText> ComputeInsertedRange(
      const EditorDOMPointInText& aInsertedPoint,
      const nsAString& aInsertedString) const;

  /**
   * EnsureComposition() should be called by composition event handlers.  This
   * tries to get the composition for the event and set it to mComposition.
   * However, this may fail because the composition may be committed before
   * the event comes to the editor.
   *
   * @return            true if there is a composition.  Otherwise, for example,
   *                    a composition event handler in web contents moved focus
   *                    for committing the composition, returns false.
   */
  bool EnsureComposition(WidgetCompositionEvent& aCompositionEvent);

  /**
   * See comment of IsCopyToClipboardAllowed() for the detail.
   */
  virtual bool IsCopyToClipboardAllowedInternal() const {
    MOZ_ASSERT(IsEditActionDataAvailable());
    return !SelectionRef().IsCollapsed();
  }

  /**
   * Helper for Is{Cut|Copy}CommandEnabled.
   * Look for a listener for the given command, including up the target chain.
   */
  MOZ_CAN_RUN_SCRIPT bool CheckForClipboardCommandListener(
      nsAtom* aCommand, EventMessage aEventMessage) const;

  /**
   * DispatchClipboardEventAndUpdateClipboard() may dispatch a clipboard event
   * and update clipboard if aEventMessage is eCopy or eCut.
   *
   * @param aEventMessage       The event message which may be set to the
   *                            dispatching event.
   * @param aClipboardType      Working with global clipboard or selection.
   */
  enum class ClipboardEventResult {
    // We have met an error in nsCopySupport::FireClipboardEvent,
    // or, default of dispatched event is NOT prevented, the event is "cut"
    // and the event target is not editable.
    IgnoredOrError,
    // A "paste" event is dispatched and prevented its default.
    DefaultPreventedOfPaste,
    // Default of a "copy" or "cut" event is prevented but the clipboard is
    // updated unless the dataTransfer of the event is cleared by the listener.
    // Or, default of the event is NOT prevented but selection is collapsed
    // when the event target is editable or the event is "copy".
    CopyOrCutHandled,
    // A clipboard event is maybe dispatched and not canceled by the web app.
    // In this case, the clipboard has been updated if aEventMessage is eCopy
    // or eCut.
    DoDefault,
  };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<ClipboardEventResult, nsresult>
  DispatchClipboardEventAndUpdateClipboard(
      EventMessage aEventMessage,
      mozilla::Maybe<nsIClipboard::ClipboardType> aClipboardType,
      DataTransfer* aDataTransfer = nullptr);

  /**
   * Called after PasteAsAction() dispatches "paste" event and it's not
   * canceled.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual nsresult HandlePaste(
      AutoEditActionDataSetter& aEditActionData,
      nsIClipboard::ClipboardType aClipboardType,
      DataTransfer* aDataTransfer) = 0;

  /**
   * Called after PasteAsQuotationAsAction() dispatches "paste" event and it's
   * not canceled.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual nsresult HandlePasteAsQuotation(
      AutoEditActionDataSetter& aEditActionData,
      nsIClipboard::ClipboardType aClipboardType,
      DataTransfer* aDataTransfer) = 0;

  /**
   * Called after PasteTransferableAsAction() dispatches "paste" event and
   * it's not canceled.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual nsresult HandlePasteTransferable(
      AutoEditActionDataSetter& aEditActionData,
      nsITransferable& aTransferable) = 0;

 private:
  nsCOMPtr<nsISelectionController> mSelectionController;
  RefPtr<Document> mDocument;

  AutoEditActionDataSetter* mEditActionData;

  /**
   * SetTextDirectionTo() sets text-direction of the root element.
   * Should use SwitchTextDirectionTo() or ToggleTextDirection() instead.
   * This is a helper class of them.
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetTextDirectionTo(TextDirection aTextDirection);

 protected:  // helper classes which may be used by friends
  /**
   * Stack based helper class for batching a collection of transactions
   * inside a placeholder transaction.  Different from AutoTransactionBatch,
   * this notifies editor observers of before/end edit action handling, and
   * dispatches "input" event if it's necessary.
   */
  class MOZ_RAII AutoPlaceholderBatch final {
   public:
    /**
     * @param aRequesterFuncName function name which wants to end the batch.
     * This won't be stored nor exposed to selection listeners etc, used
     * only for logging. This MUST be alive when the destructor runs.
     */
    AutoPlaceholderBatch(EditorBase& aEditorBase,
                         ScrollSelectionIntoView aScrollSelectionIntoView,
                         const char* aRequesterFuncName)
        : mEditorBase(aEditorBase),
          mScrollSelectionIntoView(aScrollSelectionIntoView),
          mRequesterFuncName(aRequesterFuncName) {
      mEditorBase->BeginPlaceholderTransaction(*nsGkAtoms::_empty,
                                               mRequesterFuncName);
    }

    AutoPlaceholderBatch(EditorBase& aEditorBase,
                         nsStaticAtom& aTransactionName,
                         ScrollSelectionIntoView aScrollSelectionIntoView,
                         const char* aRequesterFuncName)
        : mEditorBase(aEditorBase),
          mScrollSelectionIntoView(aScrollSelectionIntoView),
          mRequesterFuncName(aRequesterFuncName) {
      mEditorBase->BeginPlaceholderTransaction(aTransactionName,
                                               mRequesterFuncName);
    }

    ~AutoPlaceholderBatch() {
      mEditorBase->EndPlaceholderTransaction(mScrollSelectionIntoView,
                                             mRequesterFuncName);
    }

   protected:
    const OwningNonNull<EditorBase> mEditorBase;
    const ScrollSelectionIntoView mScrollSelectionIntoView;
    const char* const mRequesterFuncName;
  };

  /**
   * AutoEditSubActionNotifier notifies editor of start to handle
   * top level edit sub-action and end handling top level edit sub-action.
   */
  class MOZ_RAII AutoEditSubActionNotifier final {
   public:
    MOZ_CAN_RUN_SCRIPT AutoEditSubActionNotifier(
        EditorBase& aEditorBase, EditSubAction aEditSubAction,
        nsIEditor::EDirection aDirection, ErrorResult& aRv)
        : mEditorBase(aEditorBase), mIsTopLevel(true) {
      // The top level edit sub action has already be set if this is nested
      // call
      // XXX Looks like that this is not aware of unexpected nested edit
      // action
      //     handling via selectionchange event listener or mutation event
      //     listener.
      if (!mEditorBase.GetTopLevelEditSubAction()) {
        MOZ_KnownLive(mEditorBase)
            .OnStartToHandleTopLevelEditSubAction(aEditSubAction, aDirection,
                                                  aRv);
      } else {
        mIsTopLevel = false;
      }
      mEditorBase.OnStartToHandleEditSubAction();
    }

    MOZ_CAN_RUN_SCRIPT ~AutoEditSubActionNotifier() {
      mEditorBase.OnEndHandlingEditSubAction();
      if (mIsTopLevel) {
        MOZ_KnownLive(mEditorBase).OnEndHandlingTopLevelEditSubAction();
      }
    }

   protected:
    EditorBase& mEditorBase;
    bool mIsTopLevel;
  };

  /**
   * Stack based helper class for turning off active selection adjustment
   * by low level transactions
   */
  class MOZ_RAII AutoTransactionsConserveSelection final {
   public:
    explicit AutoTransactionsConserveSelection(EditorBase& aEditorBase)
        : mEditorBase(aEditorBase),
          mAllowedTransactionsToChangeSelection(
              aEditorBase.AllowsTransactionsToChangeSelection()) {
      mEditorBase.MakeThisAllowTransactionsToChangeSelection(false);
    }

    ~AutoTransactionsConserveSelection() {
      mEditorBase.MakeThisAllowTransactionsToChangeSelection(
          mAllowedTransactionsToChangeSelection);
    }

   protected:
    EditorBase& mEditorBase;
    bool mAllowedTransactionsToChangeSelection;
  };

  /***************************************************************************
   * stack based helper class for batching reflow and paint requests.
   */
  class MOZ_RAII AutoUpdateViewBatch final {
   public:
    /**
     * @param aRequesterFuncName function name which wants to end the batch.
     * This won't be stored nor exposed to selection listeners etc, used
     * only for logging. This MUST be alive when the destructor runs.
     */
    MOZ_CAN_RUN_SCRIPT explicit AutoUpdateViewBatch(
        EditorBase& aEditorBase, const char* aRequesterFuncName)
        : mEditorBase(aEditorBase), mRequesterFuncName(aRequesterFuncName) {
      mEditorBase.BeginUpdateViewBatch(mRequesterFuncName);
    }

    MOZ_CAN_RUN_SCRIPT ~AutoUpdateViewBatch() {
      MOZ_KnownLive(mEditorBase).EndUpdateViewBatch(mRequesterFuncName);
    }

   protected:
    EditorBase& mEditorBase;
    const char* const mRequesterFuncName;
  };

 protected:
  enum Tristate { eTriUnset, eTriFalse, eTriTrue };

  // MIME type of the doc we are editing.
  nsString mContentMIMEType;

  RefPtr<mozInlineSpellChecker> mInlineSpellChecker;
  // Reference to text services document for mInlineSpellChecker.
  RefPtr<TextServicesDocument> mTextServicesDocument;

  RefPtr<TransactionManager> mTransactionManager;
  // Cached root node.
  RefPtr<Element> mRootElement;

  // The form field as an event receiver.
  nsCOMPtr<dom::EventTarget> mEventTarget;
  RefPtr<EditorEventListener> mEventListener;
  // Strong reference to placeholder for begin/end batch purposes.
  RefPtr<PlaceholderTransaction> mPlaceholderTransaction;
  // Name of placeholder transaction.
  nsStaticAtom* mPlaceholderName;
  // Saved selection state for placeholder transaction batching.
  mozilla::Maybe<SelectionState> mSelState;
  // IME composition this is not null between compositionstart and
  // compositionend.
  RefPtr<TextComposition> mComposition;

  RefPtr<TextInputListener> mTextInputListener;

  RefPtr<IMEContentObserver> mIMEContentObserver;

  // These members cache last encoder and its type for the performance in
  // TextEditor::ComputeTextValue() which is the implementation of
  // `<input>.value` and `<textarea>.value`.  See `GetAndInitDocEncoder()`.
  mutable nsCOMPtr<nsIDocumentEncoder> mCachedDocumentEncoder;
  mutable nsString mCachedDocumentEncoderType;

  // Listens to all low level actions on the doc.
  // Edit action listener is currently used by highlighter of the findbar
  // and the spellchecker.  So, we should reserve only 2 items.
  using AutoActionListenerArray =
      AutoTArray<OwningNonNull<nsIEditActionListener>, 2>;
  AutoActionListenerArray mActionListeners;
  // Listen to overall doc state (dirty or not, just created, etc.).
  // Document state listener is currently used by FinderHighlighter and
  // BlueGriffon so that reserving only one is enough.
  using AutoDocumentStateListenerArray =
      AutoTArray<OwningNonNull<nsIDocumentStateListener>, 1>;
  AutoDocumentStateListenerArray mDocStateListeners;

  // Number of modifications (for undo/redo stack).
  uint32_t mModCount;
  // Behavior flags. See nsIEditor.idl for the flags we use.
  uint32_t mFlags;

  int32_t mUpdateCount;

  // Nesting count for batching.
  int32_t mPlaceholderBatch;

  int32_t mWrapColumn = 0;
  int32_t mNewlineHandling;
  int32_t mCaretStyle;

  // -1 = not initialized
  int8_t mDocDirtyState;
  // A Tristate value.
  uint8_t mSpellcheckCheckboxState;

  // If true, initialization was succeeded.
  bool mInitSucceeded;
  // If false, transactions should not change Selection even after modifying
  // the DOM tree.
  bool mAllowsTransactionsToChangeSelection;
  // Whether PreDestroy has been called.
  bool mDidPreDestroy;
  // Whether PostCreate has been called.
  bool mDidPostCreate;
  bool mDispatchInputEvent;
  // True while the instance is handling an edit sub-action.
  bool mIsInEditSubAction;
  // Whether caret is hidden forcibly.
  bool mHidingCaret;
  // Whether spellchecker dictionary is initialized after focused.
  bool mSpellCheckerDictionaryUpdated;
  // Whether we are an HTML editor class.
  bool mIsHTMLEditorClass;

  friend class AlignStateAtSelection;          // AutoEditActionDataSetter,
                                               // ToGenericNSResult
  friend class AutoClonedRangeArray;           // IsSEditActionDataAvailable,
                                               // RangeUpdaterRef
  friend class AutoClonedSelectionRangeArray;  // RangeUpdaterRef, SelectionRef
  friend class AutoSelectionRestorer;   // RangeUpdaterRef, SavedSelectionRef
  friend class CaretPoint;              // AllowsTransactionsToChangeSelection,
                                        // CollapseSelectionTo
  friend class CompositionTransaction;  // CollapseSelectionTo,
                                        // DoDeleteText, DoInsertText,
                                        // DoReplaceText, HideCaret,
                                        // RangeUpdaterRef
  friend class DeleteNodeTransaction;   // RangeUpdaterRef
  friend class DeleteRangeTransaction;  // AllowsTransactionsToChangeSelection,
                                        // CollapseSelectionTo
  friend class DeleteTextTransaction;   // AllowsTransactionsToChangeSelection,
                                        // DoDeleteText, DoInsertText,
                                        // RangeUpdaterRef
  friend class InsertNodeTransaction;   // AllowsTransactionsToChangeSelection,
                                        // CollapseSelectionTo,
                                        //  MarkElementDirty, ToGenericNSResult
  friend class InsertTextTransaction;   // AllowsTransactionsToChangeSelection,
                                        // CollapseSelectionTo, DoDeleteText,
                                        // DoInsertText, RangeUpdaterRef
  friend class ListElementSelectionState;      // AutoEditActionDataSetter,
                                               // ToGenericNSResult
  friend class ListItemElementSelectionState;  // AutoEditActionDataSetter,
                                               // ToGenericNSResult
  friend class MoveNodeTransaction;      // MarkElementDirty, ToGenericNSResult
  friend class MoveSiblingsTransaction;  // MarkElementDirty, ToGenericNSResult
  friend class ParagraphStateAtSelection;  // AutoEditActionDataSetter,
                                           // ToGenericNSResult
  friend class PendingStyles;              // GetEditAction,
                                           // GetFirstSelectionStartPoint,
                                           // SelectionRef
  friend class ReplaceTextTransaction;  // AllowsTransactionsToChangeSelection,
                                        // CollapseSelectionTo, DoReplaceText,
                                        // RangeUpdaterRef
  friend class SplitNodeTransaction;    // ToGenericNSResult
  friend class
      WhiteSpaceVisibilityKeeper;  // AutoTransactionsConserveSelection,
                                   // ComputePointToInsertText
  friend class nsIEditor;          // mIsHTMLEditorClass
};

}  // namespace mozilla

bool nsIEditor::IsTextEditor() const {
  return !AsEditorBase()->mIsHTMLEditorClass;
}

bool nsIEditor::IsHTMLEditor() const {
  return AsEditorBase()->mIsHTMLEditorClass;
}

mozilla::EditorBase* nsIEditor::AsEditorBase() {
  return static_cast<mozilla::EditorBase*>(this);
}

const mozilla::EditorBase* nsIEditor::AsEditorBase() const {
  return static_cast<const mozilla::EditorBase*>(this);
}

#endif  // #ifndef mozilla_EditorBase_h
