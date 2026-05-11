/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_HTMLEditor_h
#define mozilla_HTMLEditor_h

#include "mozilla/Attributes.h"
#include "mozilla/ComposerCommandsUpdater.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EditorForwards.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/ManualNAC.h"
#include "mozilla/Result.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/File.h"

#include "nsAttrName.h"
#include "nsCOMPtr.h"
#include "nsIDocumentObserver.h"
#include "nsIDOMEventListener.h"
#include "nsIEditorMailSupport.h"
#include "nsIHTMLAbsPosEditor.h"
#include "nsIHTMLEditor.h"
#include "nsIHTMLInlineTableEditor.h"
#include "nsIHTMLObjectResizer.h"
#include "nsIPrincipal.h"
#include "nsITableEditor.h"
#include "nsPoint.h"
#include "nsStubMutationObserver.h"

#include <functional>

class nsDocumentFragment;
class nsFrameSelection;
class nsHTMLDocument;
class nsITransferable;
class nsRange;
class nsStaticAtom;
class nsStyledElement;
class nsTableCellFrame;
class nsTableWrapperFrame;
template <class E>
class nsTArray;

namespace mozilla {
class AlignStateAtSelection;
class AutoSelectionSetterAfterTableEdit;
class EmptyEditableFunctor;
class ListElementSelectionState;
class ListItemElementSelectionState;
class ParagraphStateAtSelection;
class ResizerSelectionListener;
class Runnable;
template <class T>
class OwningNonNull;
enum class LogLevel;
namespace dom {
class AbstractRange;
class Blob;
class DocumentFragment;
class Event;
class HTMLBRElement;
class MouseEvent;
class StaticRange;
}  // namespace dom
namespace widget {
struct IMEState;
}  // namespace widget

enum class ParagraphSeparator { div, p, br };

/**
 * The HTML editor implementation.<br>
 * Use to edit HTML document represented as a DOM tree.
 */
class HTMLEditor final : public EditorBase,
                         public nsIHTMLEditor,
                         public nsIHTMLObjectResizer,
                         public nsIHTMLAbsPosEditor,
                         public nsITableEditor,
                         public nsIHTMLInlineTableEditor,
                         public nsStubMutationObserver,
                         public nsIEditorMailSupport {
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

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLEditor, EditorBase)

  // nsStubMutationObserver overrides
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED

  // nsIHTMLEditor methods
  NS_DECL_NSIHTMLEDITOR

  // nsIHTMLObjectResizer methods (implemented in HTMLObjectResizer.cpp)
  NS_DECL_NSIHTMLOBJECTRESIZER

  // nsIHTMLAbsPosEditor methods (implemented in HTMLAbsPositionEditor.cpp)
  NS_DECL_NSIHTMLABSPOSEDITOR

  // nsIHTMLInlineTableEditor methods (implemented in HTMLInlineTableEditor.cpp)
  NS_DECL_NSIHTMLINLINETABLEEDITOR

  // nsIEditorMailSupport methods
  NS_DECL_NSIEDITORMAILSUPPORT

  // nsITableEditor methods
  NS_DECL_NSITABLEEDITOR

  // nsISelectionListener overrides
  NS_DECL_NSISELECTIONLISTENER

  /**
   * @param aDocument   The document whose content will be editable.
   */
  explicit HTMLEditor(const Document& aDocument);

  /**
   * @param aDocument   The document whose content will be editable.
   * @param aComposerCommandsUpdater     The composer command updater.
   * @param aFlags      Some of nsIEditor::eEditor*Mask flags.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  Init(Document& aDocument, ComposerCommandsUpdater& aComposerCommandsUpdater,
       uint32_t aFlags);

  /**
   * PostCreate() should be called after Init, and is the time that the editor
   * tells its documentStateObservers that the document has been created.
   */
  MOZ_CAN_RUN_SCRIPT nsresult PostCreate();

  /**
   * PreDestroy() is called before the editor goes away, and gives the editor a
   * chance to tell its documentStateObservers that the document is going away.
   */
  MOZ_CAN_RUN_SCRIPT void PreDestroy();

  static HTMLEditor* GetFrom(nsIEditor* aEditor) {
    return aEditor ? aEditor->GetAsHTMLEditor() : nullptr;
  }
  static const HTMLEditor* GetFrom(const nsIEditor* aEditor) {
    return aEditor ? aEditor->GetAsHTMLEditor() : nullptr;
  }

  [[nodiscard]] bool GetReturnInParagraphCreatesNewParagraph() const;

  // EditorBase overrides
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD BeginningOfDocument() final;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD EndOfDocument() final;

  NS_IMETHOD GetDocumentCharacterSet(nsACString& aCharacterSet) final;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD
  SetDocumentCharacterSet(const nsACString& aCharacterSet) final;

  bool IsEmpty() const final;

  bool CanPaste(nsIClipboard::ClipboardType aClipboardType) const final;
  using EditorBase::CanPaste;

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD DeleteNode(nsINode* aNode,
                                           bool aPreseveSelection,
                                           uint8_t aOptionalArgCount) final;

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD InsertLineBreak() final;

  /**
   * PreHandleMouseDown() and PreHandleMouseUp() are called before
   * HTMLEditorEventListener handles them.  The coming event may be
   * non-acceptable event.
   */
  void PreHandleMouseDown(const dom::MouseEvent& aMouseDownEvent);
  void PreHandleMouseUp(const dom::MouseEvent& aMouseUpEvent);

  /**
   * PreHandleSelectionChangeCommand() and PostHandleSelectionChangeCommand()
   * are called before or after handling a command which may change selection
   * and/or scroll position.
   */
  void PreHandleSelectionChangeCommand(Command aCommand);
  void PostHandleSelectionChangeCommand(Command aCommand);

  MOZ_CAN_RUN_SCRIPT nsresult
  HandleKeyPressEvent(WidgetKeyboardEvent* aKeyboardEvent) final;
  Element* GetFocusedElement() const final;
  bool IsActiveInDOMWindow() const final;
  dom::EventTarget* GetDOMEventTarget() const final;
  [[nodiscard]] Element* FindSelectionRoot(const nsINode& aNode) const final;
  bool IsAcceptableInputEvent(WidgetGUIEvent* aGUIEvent) const final;
  [[nodiscard]] Result<widget::IMEState, nsresult> GetPreferredIMEState()
      const final;
  MOZ_CAN_RUN_SCRIPT nsresult
  OnFocus(const nsINode& aOriginalEventTargetNode) final;
  MOZ_CAN_RUN_SCRIPT nsresult
  OnBlur(const dom::EventTarget* aEventTarget) final;

  /**
   * Called when aDocument or aElement becomes editable without focus change.
   * E.g., when the design mode is enabled or the contenteditable attribute
   * is set to the focused element.
   */
  MOZ_CAN_RUN_SCRIPT nsresult FocusedElementOrDocumentBecomesEditable(
      Document& aDocument, Element* aElement);

  /**
   * Called when aDocument or aElement becomes not editable without focus
   * change. E.g., when the design mode ends or the contenteditable attribute is
   * removed or set to "false".
   */
  MOZ_CAN_RUN_SCRIPT static nsresult FocusedElementOrDocumentBecomesNotEditable(
      HTMLEditor* aHTMLEditor, Document& aDocument, Element* aElement);

  /**
   * GetBackgroundColorState() returns what the background color of the
   * selection.
   *
   * @param aMixed      true if there is more than one font color
   * @param aOutColor   Color string. "" is returned for none.
   */
  MOZ_CAN_RUN_SCRIPT nsresult GetBackgroundColorState(bool* aMixed,
                                                      nsAString& aOutColor);

  /**
   * PasteNoFormattingAsAction() pastes content in clipboard without any style
   * information.
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
  PasteNoFormattingAsAction(nsIClipboard::ClipboardType aClipboardType,
                            DispatchPasteEvent aDispatchPasteEvent,
                            DataTransfer* aDataTransfer = nullptr,
                            nsIPrincipal* aPrincipal = nullptr);

  bool CanPasteTransferable(nsITransferable* aTransferable) final;

  MOZ_CAN_RUN_SCRIPT nsresult
  InsertLineBreakAsAction(nsIPrincipal* aPrincipal = nullptr) final;

  /**
   * InsertParagraphSeparatorAsAction() is called when user tries to separate
   * current paragraph with Enter key press in HTMLEditor or something.
   *
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  InsertParagraphSeparatorAsAction(nsIPrincipal* aPrincipal = nullptr);

  enum class InsertElementOption {
    // Delete selection if set, otherwise, insert aElement at start or end of
    // selection.
    DeleteSelection,
    // Whether split all inline ancestors or not.
    SplitAncestorInlineElements,
  };
  using InsertElementOptions = EnumSet<InsertElementOption>;
  MOZ_CAN_RUN_SCRIPT nsresult InsertElementAtSelectionAsAction(
      Element* aElement, const InsertElementOptions aOptions,
      nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult InsertLinkAroundSelectionAsAction(
      Element* aAnchorElement, nsIPrincipal* aPrincipal = nullptr);

  /**
   * CreateElementWithDefaults() creates new element whose name is
   * aTagName with some default attributes are set.  Note that this is a
   * public utility method.  I.e., just creates element, not insert it
   * into the DOM tree.
   * NOTE: This is available for internal use too since this does not change
   *       the DOM tree nor undo transactions, and does not refer Selection,
   *       etc.
   *
   * @param aTagName            The new element's tag name.  If the name is
   *                            one of "href", "anchor" or "namedanchor",
   *                            this creates an <a> element.
   * @return                    Newly created element.
   */
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Element> CreateElementWithDefaults(
      const nsAtom& aTagName);

  /**
   * Indent or outdent content around Selection.
   *
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  IndentAsAction(nsIPrincipal* aPrincipal = nullptr);
  MOZ_CAN_RUN_SCRIPT nsresult
  OutdentAsAction(nsIPrincipal* aPrincipal = nullptr);

  /**
   * The Document.execCommand("formatBlock") handler.
   *
   * @param aParagraphFormat    Must not be an empty string, and the value must
   *                            be one of address, article, aside, blockquote,
   *                            div, footer, h1, h2, h3, h4, h5, h6, header,
   *                            hgroup, main, nav, p, pre, selection, dt or dd.
   */
  MOZ_CAN_RUN_SCRIPT nsresult FormatBlockAsAction(
      const nsAString& aParagraphFormat, nsIPrincipal* aPrincipal = nullptr);

  /**
   * The cmd_paragraphState command handler.
   *
   * @param aParagraphFormat    Can be empty string.  If this is empty string,
   *                            this removes ancestor format elements.
   *                            Otherwise, the value must be one of p, pre,
   *                            h1, h2, h3, h4, h5, h6, address, dt or dl.
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetParagraphStateAsAction(
      const nsAString& aParagraphFormat, nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult AlignAsAction(const nsAString& aAlignType,
                                            nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult RemoveListAsAction(
      const nsAString& aListType, nsIPrincipal* aPrincipal = nullptr);

  /**
   * MakeOrChangeListAsAction() makes selected hard lines list element(s).
   *
   * @param aListElementTagName         The new list element tag name.  Must be
   *                                    nsGkAtoms::ul, nsGkAtoms::ol or
   *                                    nsGkAtoms::dl.
   * @param aBulletType                 If this is not empty string, it's set
   *                                    to `type` attribute of new list item
   *                                    elements.  Otherwise, existing `type`
   *                                    attributes will be removed.
   * @param aSelectAllOfCurrentList     Yes if this should treat all of
   *                                    ancestor list element at selection.
   */
  enum class SelectAllOfCurrentList { Yes, No };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MakeOrChangeListAsAction(
      const nsStaticAtom& aListElementTagName, const nsAString& aBulletType,
      SelectAllOfCurrentList aSelectAllOfCurrentList,
      nsIPrincipal* aPrincipal = nullptr);

  /**
   * If aTargetElement is a resizer, start to drag the resizer.  Otherwise, if
   * aTargetElement is the grabber, start to handle drag gester on it.
   *
   * @param aMouseDownEvent     A `mousedown` event fired on aTargetElement.
   * @param aEventTargetElement The target element being pressed.  This must
   *                            be same as explicit original event target of
   *                            aMouseDownEvent.
   */
  MOZ_CAN_RUN_SCRIPT nsresult StartToDragResizerOrHandleDragGestureOnGrabber(
      dom::MouseEvent& aMouseDownEvent, Element& aEventTargetElement);

  /**
   * If the editor is handling dragging a resizer, handling drag gesture on
   * the grabber or dragging the grabber, this finalize it.  Otherwise,
   * does nothing.
   *
   * @param aClientPoint    The final point of the drag.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  StopDraggingResizerOrGrabberAt(const CSSIntPoint& aClientPoint);

  /**
   * If the editor is handling dragging a resizer, handling drag gesture to
   * start dragging the grabber or dragging the grabber, this method updates
   * it's position.
   *
   * @param aClientPoint    The new point of the drag.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  UpdateResizerOrGrabberPositionTo(const CSSIntPoint& aClientPoint);

  /**
   * IsCSSEnabled() returns true if this editor treats styles with style
   * attribute of HTML elements.  Otherwise, if this editor treats all styles
   * with "font style elements" like <b>, <i>, etc, and <blockquote> to indent,
   * align attribute to align contents, returns false.
   */
  bool IsCSSEnabled() const { return mIsCSSPrefChecked; }

  /**
   * Return true when editing host is not plaintext-only.
   */
  [[nodiscard]] bool IsStyleEditable() const;

  /**
   * Enable/disable object resizers for <img> elements, <table> elements,
   * absolute positioned elements (required absolute position editor enabled).
   */
  MOZ_CAN_RUN_SCRIPT void EnableObjectResizer(bool aEnable) {
    if (mIsObjectResizingEnabled == aEnable) {
      return;
    }

    AutoEditActionDataSetter editActionData(
        *this, EditAction::eEnableOrDisableResizer);
    if (NS_WARN_IF(!editActionData.CanHandle())) {
      return;
    }

    mIsObjectResizingEnabled = aEnable;
    RefreshEditingUI();
  }
  bool IsObjectResizerEnabled() const {
    return mIsObjectResizingEnabled && IsStyleEditable();
  }

  Element* GetResizerTarget() const { return mResizedObject; }

  /**
   * Enable/disable inline table editor, e.g., adding new row or column,
   * removing existing row or column.
   */
  MOZ_CAN_RUN_SCRIPT void EnableInlineTableEditor(bool aEnable) {
    if (mIsInlineTableEditingEnabled == aEnable) {
      return;
    }

    AutoEditActionDataSetter editActionData(
        *this, EditAction::eEnableOrDisableInlineTableEditingUI);
    if (NS_WARN_IF(!editActionData.CanHandle())) {
      return;
    }

    mIsInlineTableEditingEnabled = aEnable;
    RefreshEditingUI();
  }
  bool IsInlineTableEditorEnabled() const {
    return mIsInlineTableEditingEnabled && IsStyleEditable();
  }

  /**
   * Enable/disable absolute position editor, resizing absolute positioned
   * elements (required object resizers enabled) or positioning them with
   * dragging grabber.
   */
  MOZ_CAN_RUN_SCRIPT void EnableAbsolutePositionEditor(bool aEnable) {
    if (mIsAbsolutelyPositioningEnabled == aEnable) {
      return;
    }

    AutoEditActionDataSetter editActionData(
        *this, EditAction::eEnableOrDisableAbsolutePositionEditor);
    if (NS_WARN_IF(!editActionData.CanHandle())) {
      return;
    }

    mIsAbsolutelyPositioningEnabled = aEnable;
    RefreshEditingUI();
  }
  bool IsAbsolutePositionEditorEnabled() const {
    return mIsAbsolutelyPositioningEnabled && IsStyleEditable();
  }

  /**
   * returns the deepest absolutely positioned container of the selection
   * if it exists or null.
   */
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Element>
  GetAbsolutelyPositionedSelectionContainer() const;

  Element* GetPositionedElement() const { return mAbsolutelyPositionedObject; }

  /**
   * extracts the selection from the normal flow of the document and
   * positions it.
   *
   * @param aEnabled [IN] true to absolutely position the selection,
   *                      false to put it back in the normal flow
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetSelectionToAbsoluteOrStaticAsAction(
      bool aEnabled, nsIPrincipal* aPrincipal = nullptr);

  /**
   * returns the absolute z-index of a positioned element. Never returns 'auto'
   * @return         the z-index of the element
   * @param aElement [IN] the element.
   */
  MOZ_CAN_RUN_SCRIPT int32_t GetZIndex(Element& aElement);

  /**
   * adds aChange to the z-index of the currently positioned element.
   *
   * @param aChange [IN] relative change to apply to current z-index
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  AddZIndexAsAction(int32_t aChange, nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult SetBackgroundColorAsAction(
      const nsAString& aColor, nsIPrincipal* aPrincipal = nullptr);

  /**
   * SetInlinePropertyAsAction() sets a property which changes inline style of
   * text.  E.g., bold, italic, super and sub.
   * This automatically removes exclusive style, however, treats all changes
   * as a transaction.
   *
   * @param aPrincipal          Set subject principal if it may be called by
   *                            JS.  If set to nullptr, will be treated as
   *                            called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetInlinePropertyAsAction(
      nsStaticAtom& aProperty, nsStaticAtom* aAttribute,
      const nsAString& aValue, nsIPrincipal* aPrincipal = nullptr);

  /**
   * GetInlineProperty() gets aggregate properties of the current selection.
   * All object in the current selection are scanned and their attributes are
   * represented in a list of Property object.
   * TODO: Make this return Result<Something> instead of bool out arguments.
   *
   * @param aHTMLProperty   the property to get on the selection
   * @param aAttribute      the attribute of the property, if applicable.
   *                        May be null.
   *                        Example: aHTMLProperty=nsGkAtoms::font,
   *                            aAttribute=nsGkAtoms::color
   * @param aValue          if aAttribute is not null, the value of the
   *                        attribute. May be null.
   *                        Example: aHTMLProperty=nsGkAtoms::font,
   *                            aAttribute=nsGkAtoms::color,
   *                            aValue="0x00FFFF"
   * @param aFirst          [OUT] true if the first text node in the
   *                              selection has the property
   * @param aAny            [OUT] true if any of the text nodes in the
   *                              selection have the property
   * @param aAll            [OUT] true if all of the text nodes in the
   *                              selection have the property
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult GetInlineProperty(
      nsStaticAtom& aHTMLProperty, nsAtom* aAttribute, const nsAString& aValue,
      bool* aFirst, bool* aAny, bool* aAll) const;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult GetInlinePropertyWithAttrValue(
      nsStaticAtom& aHTMLProperty, nsAtom* aAttribute, const nsAString& aValue,
      bool* aFirst, bool* aAny, bool* aAll, nsAString& outValue);

  /**
   * RemoveInlinePropertyAsAction() removes a property which changes inline
   * style of text.  E.g., bold, italic, super and sub.
   *
   * @param aHTMLProperty   Tag name whcih represents the inline style you want
   *                        to remove.  E.g., nsGkAtoms::strong, nsGkAtoms::b,
   *                        etc.  If nsGkAtoms::href, <a> element which has
   *                        href attribute will be removed.
   *                        If nsGkAtoms::name, <a> element which has non-empty
   *                        name attribute will be removed.
   * @param aAttribute  If aHTMLProperty is nsGkAtoms::font, aAttribute should
   *                    be nsGkAtoms::fase, nsGkAtoms::size, nsGkAtoms::color
   *                    or nsGkAtoms::bgcolor.  Otherwise, set nullptr.
   *                    Must not use nsGkAtoms::_empty here.
   * @param aPrincipal  Set subject principal if it may be called by JS.  If
   *                    set to nullptr, will be treated as called by system.
   */
  MOZ_CAN_RUN_SCRIPT nsresult RemoveInlinePropertyAsAction(
      nsStaticAtom& aHTMLProperty, nsStaticAtom* aAttribute,
      nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult
  RemoveAllInlinePropertiesAsAction(nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult
  IncreaseFontSizeAsAction(nsIPrincipal* aPrincipal = nullptr);

  MOZ_CAN_RUN_SCRIPT nsresult
  DecreaseFontSizeAsAction(nsIPrincipal* aPrincipal = nullptr);

  /**
   * GetFontColorState() returns foreground color information in first
   * range of Selection.
   * If first range of Selection is collapsed and there is a cache of style for
   * new text, aIsMixed is set to false and aColor is set to the cached color.
   * If first range of Selection is collapsed and there is no cached color,
   * this returns the color of the node, aIsMixed is set to false and aColor is
   * set to the color.
   * If first range of Selection is not collapsed, this collects colors of
   * each node in the range.  If there are two or more colors, aIsMixed is set
   * to true and aColor is truncated.  If only one color is set to all of the
   * range, aIsMixed is set to false and aColor is set to the color.
   * If there is no Selection ranges, aIsMixed is set to false and aColor is
   * truncated.
   *
   * @param aIsMixed            Must not be nullptr.  This is set to true
   *                            if there is two or more colors in first
   *                            range of Selection.
   * @param aColor              Returns the color if only one color is set to
   *                            all of first range in Selection.  Otherwise,
   *                            returns empty string.
   * @return                    Returns error only when illegal cases, e.g.,
   *                            Selection instance has gone, first range
   *                            Selection is broken.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  GetFontColorState(bool* aIsMixed, nsAString& aColor);

  /**
   * Detach aComposerCommandsUpdater from this.
   */
  void Detach(const ComposerCommandsUpdater& aComposerCommandsUpdater);

  nsStaticAtom& DefaultParagraphSeparatorTagName() const {
    return HTMLEditor::ToParagraphSeparatorTagName(mDefaultParagraphSeparator);
  }
  ParagraphSeparator GetDefaultParagraphSeparator() const {
    return mDefaultParagraphSeparator;
  }
  void SetDefaultParagraphSeparator(ParagraphSeparator aSep) {
    mDefaultParagraphSeparator = aSep;
  }
  static nsStaticAtom& ToParagraphSeparatorTagName(
      ParagraphSeparator aSeparator) {
    switch (aSeparator) {
      case ParagraphSeparator::div:
        return *nsGkAtoms::div;
      case ParagraphSeparator::p:
        return *nsGkAtoms::p;
      case ParagraphSeparator::br:
        return *nsGkAtoms::br;
      default:
        MOZ_ASSERT_UNREACHABLE("New paragraph separator isn't handled here");
        return *nsGkAtoms::div;
    }
  }

  /**
   * Modifies the table containing the selection according to the
   * activation of an inline table editing UI element
   * @param aUIAnonymousElement [IN] the inline table editing UI element
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DoInlineTableEditingAction(const Element& aUIAnonymousElement);

  /**
   * GetInclusiveAncestorByTagName() looks for an element node whose name
   * matches aTagName from aNode or anchor node of Selection to <body> element.
   *
   * @param aTagName        The tag name which you want to look for.
   *                        Must not be nsGkAtoms::_empty.
   *                        If nsGkAtoms::list, the result may be <ul>, <ol> or
   *                        <dl> element.
   *                        If nsGkAtoms::td, the result may be <td> or <th>.
   *                        If nsGkAtoms::href, the result may be <a> element
   *                        which has "href" attribute with non-empty value.
   *                        If nsGkAtoms::anchor, the result may be <a> which
   *                        has "name" attribute with non-empty value.
   * @param aContent        Start node to look for the result.
   * @return                If an element which matches aTagName, returns
   *                        an Element.  Otherwise, nullptr.
   */
  Element* GetInclusiveAncestorByTagName(const nsStaticAtom& aTagName,
                                         nsIContent& aContent) const;

  /**
   * Compute editing host for aContent.  If this editor isn't active in the DOM
   * window, this returns nullptr.
   */
  enum class LimitInBodyElement { No, Yes };
  [[nodiscard]] Element* ComputeEditingHost(
      const nsIContent& aContent,
      LimitInBodyElement aLimitInBodyElement = LimitInBodyElement::Yes) const {
    return ComputeEditingHostInternal(&aContent, aLimitInBodyElement);
  }

  /**
   * Compute editing host for the focus node of the Selection.  If this editor
   * isn't active in the DOM window, this returns nullptr.
   */
  [[nodiscard]] Element* ComputeEditingHost(
      LimitInBodyElement aLimitInBodyElement = LimitInBodyElement::Yes) const {
    return ComputeEditingHostInternal(nullptr, aLimitInBodyElement);
  }

  /**
   * Return true if this editor was notified of focus, but has not been notified
   * of the blur.
   */
  [[nodiscard]] bool HasFocus() const { return mHasFocus; }

  /**
   * Return true if this editor is in the designMode.
   */
  [[nodiscard]] bool IsInDesignMode() const { return mIsInDesignMode; }

  /**
   * Return true if entire the document is editable (although the document
   * may have non-editable nodes, e.g.,
   * <body contenteditable><div contenteditable="false"></div></body>
   */
  bool EntireDocumentIsEditable() const;

  /**
   * Basically, this always returns true if we're for `contenteditable` or
   * `designMode` editor in web apps.  However, e.g., Composer of SeaMonkey
   * can make the editor not tabbable.
   */
  bool IsTabbable() const { return IsInteractionAllowed(); }

  /**
   * NotifyEditingHostMaybeChanged() is called when new element becomes
   * contenteditable when the document already had contenteditable elements.
   */
  MOZ_CAN_RUN_SCRIPT void NotifyEditingHostMaybeChanged();

  /** Insert a string as quoted text
   * (whose representation is dependant on the editor type),
   * replacing the selected text (if any).
   *
   * @param aQuotedText    The actual text to be quoted
   * @parem aNodeInserted  Return the node which was inserted.
   */
  MOZ_CAN_RUN_SCRIPT  // USED_BY_COMM_CENTRAL
      nsresult
      InsertAsQuotation(const nsAString& aQuotedText, nsINode** aNodeInserted);

  MOZ_CAN_RUN_SCRIPT nsresult InsertHTMLAsAction(
      const nsAString& aInString, nsIPrincipal* aPrincipal = nullptr);

  /**
   * Refresh positions of resizers.  If you change size of target of resizers,
   * you need to refresh position of resizers with calling this.
   */
  MOZ_CAN_RUN_SCRIPT nsresult RefreshResizers();

  bool IsWrapHackEnabled() const {
    return (mFlags & nsIEditor::eEditorEnableWrapHackMask) != 0;
  }

  /**
   * Return true if this is in the plaintext mail composer mode of
   * Thunderbird or something.
   * NOTE: This is different from contenteditable="plaintext-only"
   */
  bool IsPlaintextMailComposer() const {
    const bool isPlaintextMode =
        (mFlags & nsIEditor::eEditorPlaintextMask) != 0;
    MOZ_ASSERT_IF(IsTextEditor(), isPlaintextMode);
    return isPlaintextMode;
  }

 protected:  // May be called by friends.
  /****************************************************************************
   * Some friend classes are allowed to call the following protected methods.
   * However, those methods won't prepare caches of some objects which are
   * necessary for them.  So, if you call them from friend classes, you need
   * to make sure that AutoEditActionDataSetter is created.
   ****************************************************************************/

  enum class LineBreakType : bool {
    BRElement,  // <br>
    Linefeed,   // Preformatted linefeed
  };
  friend std::ostream& operator<<(std::ostream& aStream,
                                  const LineBreakType aLineBreakType) {
    switch (aLineBreakType) {
      case LineBreakType::BRElement:
        return aStream << "LineBreakType::BRElement";
      case LineBreakType::Linefeed:
        return aStream << "LineBreakType::BRElement";
    }
    MOZ_ASSERT_UNREACHABLE("Invalid LineBreakType");
    return aStream;
  }

  /**
   * Return preferred line break when you insert a line break in aNode (if
   * aNode is a Text node, this assumes that line break will be inserted to
   * its parent element).
   *
   * @param aNode           The node where you want to insert a line break.
   *                        This should be a inclusive descendant of
   *                        aEditingHost because if it's not connected, we can
   *                        not refer the proper style information.
   * @param aEditingHost    The editing host.
   */
  Maybe<LineBreakType> GetPreferredLineBreakType(
      const nsINode& aNode, const Element& aEditingHost) const;

  /**
   * InsertLineBreak() creates a <br> element or a Text node which has only
   * preformatted linefeed and inserts it at aPointToInsert.
   *
   * @param aWithTransaction    Whether the inserting is new element is undoable
   *                            or not.  WithTransaction::No is useful only when
   *                            the new element is inserted into a new element
   *                            which has not been connected yet.
   * @param aLineBreakType      Whether a <br> element or a linefeed should be
   *                            used.
   * @param aPointToInsert      The DOM point where a <br> element or a Text
   *                            node should be inserted.
   * @param aSelect             If eNone, returns a point to put caret which is
   *                            suggested by InsertNodeTransaction.
   *                            If eNext, returns a point after the new <br>
   *                            element.
   *                            If ePrevious, returns a point at the new <br>
   *                            element.
   * @return                    The new <br> or Text node and suggesting point
   *                            to put caret with respecting aSelect.
   */
  MOZ_CAN_RUN_SCRIPT Result<CreateLineBreakResult, nsresult> InsertLineBreak(
      WithTransaction aWithTransaction, LineBreakType aLineBreakType,
      const EditorDOMPoint& aPointToInsert, EDirection aSelect = eNone);

  /**
   * Delete text in the range in aTextNode.  If aTextNode is not editable, this
   * does nothing.
   *
   * @param aTextNode           The text node which should be modified.
   * @param aOffset             Start offset of removing text in aTextNode.
   * @param aLength             Length of removing text.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteTextWithTransaction(dom::Text& aTextNode, uint32_t aOffset,
                            uint32_t aLength);

  /**
   * Replace text in the range with aStringToInsert.  If there is a DOM range
   * exactly same as the replacing range, it'll be collapsed to
   * {aTextNode, aOffset} because of the order of deletion and insertion.
   * Therefore, the callers may need to handle `Selection` even when callers
   * do not want to update `Selection`.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<InsertTextResult, nsresult>
  ReplaceTextWithTransaction(dom::Text& aTextNode, uint32_t aOffset,
                             uint32_t aLength,
                             const nsAString& aStringToInsert);

  struct NormalizedStringToInsertText;

  /**
   * Insert text to aPointToInsert or replace text in the range stored by aData
   * in the text node specified by aPointToInsert with the normalized string
   * stored by aData.  So, aPointToInsert must be in a `Text` node if
   * aData.ReplaceLength() is not 0.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<InsertTextResult, nsresult>
  InsertOrReplaceTextWithTransaction(const EditorDOMPoint& aPointToInsert,
                                     const NormalizedStringToInsertText& aData);

  struct ReplaceWhiteSpacesData;

  /**
   * Replace or insert white-spaces of aData to aTextNode.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<InsertTextResult, nsresult>
  ReplaceTextWithTransaction(dom::Text& aTextNode,
                             const ReplaceWhiteSpacesData& aData);

  /**
   * Insert aStringToInsert to aPointToInsert.  If the point is not editable,
   * this returns error.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<InsertTextResult, nsresult>
  InsertTextWithTransaction(const nsAString& aStringToInsert,
                            const EditorDOMPoint& aPointToInsert,
                            InsertTextTo aInsertTextTo) final;

  /**
   * CopyLastEditableChildStyles() clones inline container elements into
   * aPreviousBlock to aNewBlock to keep using same style in it.
   *
   * @param aPreviousBlock      The previous block element.  All inline
   *                            elements which are last sibling of each level
   *                            are cloned to aNewBlock.
   * @param aNewBlock           New block container element.  All children of
   *                            this is deleted first.
   * @param aEditingHost        The editing host.
   * @return                    If succeeded, returns a suggesting point to put
   *                            caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  CopyLastEditableChildStylesWithTransaction(Element& aPreviousBlock,
                                             Element& aNewBlock,
                                             const Element& aEditingHost);

  /**
   * RemoveBlockContainerWithTransaction() removes aElement from the DOM tree
   * but moves its all children to its parent node and if its parent needs <br>
   * element to have at least one line-height, this inserts <br> element
   * automatically.
   *
   * @param aElement            Block element to be removed.
   * @return                    If succeeded, returns a suggesting point to put
   *                            caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  RemoveBlockContainerWithTransaction(Element& aElement);

  MOZ_CAN_RUN_SCRIPT nsresult RemoveAttributeOrEquivalent(
      Element* aElement, nsAtom* aAttribute, bool aSuppressTransaction) final;
  MOZ_CAN_RUN_SCRIPT nsresult SetAttributeOrEquivalent(
      Element* aElement, nsAtom* aAttribute, const nsAString& aValue,
      bool aSuppressTransaction) final;
  using EditorBase::RemoveAttributeOrEquivalent;
  using EditorBase::SetAttributeOrEquivalent;

  /**
   * Returns container element of ranges in Selection.  If Selection is
   * collapsed, returns focus container node (or its parent element).
   * If Selection selects only one element node, returns the element node.
   * If Selection is only one range, returns common ancestor of the range.
   * XXX If there are two or more Selection ranges, this returns parent node
   *     of start container of a range which starts with different node from
   *     start container of the first range.
   */
  Element* GetSelectionContainerElement() const;

  /**
   * DeleteTableCellContentsWithTransaction() removes any contents in cell
   * elements.  If two or more cell elements are selected, this removes
   * all selected cells' contents.  Otherwise, this removes contents of
   * a cell which contains first selection range.  This does not return
   * error even if selection is not in cell element, just does nothing.
   */
  MOZ_CAN_RUN_SCRIPT nsresult DeleteTableCellContentsWithTransaction();

  /**
   * extracts an element from the normal flow of the document and
   * positions it, and puts it back in the normal flow.
   * @param aElement [IN] the element
   * @param aEnabled [IN] true to absolutely position the element,
   *                      false to put it back in the normal flow
   */
  MOZ_CAN_RUN_SCRIPT nsresult SetPositionToAbsoluteOrStatic(Element& aElement,
                                                            bool aEnabled);

  /**
   * adds aChange to the z-index of an arbitrary element.
   * @param aElement    [IN] the element
   * @param aChange     [IN] relative change to apply to current z-index of
   *                    the element
   * @return            The new z-index of the element
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<int32_t, nsresult>
  AddZIndexWithTransaction(nsStyledElement& aStyledElement, int32_t aChange);

  /**
   * Join together adjacent editable text nodes in the range except preformatted
   * linefeed only nodes.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CollapseAdjacentTextNodes(nsRange& aRange);

  static dom::Element* GetLinkElement(nsINode* aNode);

  /**
   * Helper routines for font size changing.
   */
  enum class FontSize { incr, decr };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  SetFontSizeOnTextNode(Text& aTextNode, uint32_t aStartOffset,
                        uint32_t aEndOffset, FontSize aIncrementOrDecrement);

  enum class SplitAtEdges {
    // SplitNodeDeepWithTransaction() won't split container element
    // nodes at their edges.  I.e., when split point is start or end of
    // container, it won't be split.
    eDoNotCreateEmptyContainer,
    // SplitNodeDeepWithTransaction() always splits containers even
    // if the split point is at edge of a container.  E.g., if split point is
    // start of an inline element, empty inline element is created as a new left
    // node.
    eAllowToCreateEmptyContainer,
  };

  /**
   * SplitAncestorStyledInlineElementsAtRangeEdges() splits all ancestor inline
   * elements in the block at aRange if given style matches with some of them.
   *
   * @param aRange              Ancestor inline elements of the start and end
   *                            boundaries will be split.
   * @param aStyle              The style which you want to split.
   *                            RemoveAllStyles instance is allowed to split any
   *                            inline elements.
   * @param aSplitAtEdges       Whether this should split elements at start or
   *                            end of inline elements or not.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitRangeOffResult, nsresult>
  SplitAncestorStyledInlineElementsAtRangeEdges(const EditorDOMRange& aRange,
                                                const EditorInlineStyle& aStyle,
                                                SplitAtEdges aSplitAtEdges);

  /**
   * SplitAncestorStyledInlineElementsAt() splits ancestor inline elements at
   * aPointToSplit if specified style matches with them.
   *
   * @param aPointToSplit       The point to split style at.
   * @param aStyle              The style which you want to split.
   *                            RemoveAllStyles instance is allowed to split any
   *                            inline elements.
   * @param aSplitAtEdges       Whether this should split elements at start or
   *                            end of inline elements or not.
   * @return                    The result of SplitNodeDeepWithTransaction()
   *                            with topmost split element.  If this didn't
   *                            find inline elements to be split, Handled()
   *                            returns false.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitNodeResult, nsresult>
  SplitAncestorStyledInlineElementsAt(const EditorDOMPoint& aPointToSplit,
                                      const EditorInlineStyle& aStyle,
                                      SplitAtEdges aSplitAtEdges);

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult GetInlinePropertyBase(
      const EditorInlineStyle& aStyle, const nsAString* aValue, bool* aFirst,
      bool* aAny, bool* aAll, nsAString* outValue) const;

  /**
   * ClearStyleAt() splits parent elements to remove the specified style.
   * If this splits some parent elements at near their start or end, such
   * empty elements will be removed.  Then, remove the specified style
   * from the point and returns DOM point to put caret.
   *
   * @param aPoint      The point to clear style at.
   * @param aStyleToRemove   The style which you want to clear.
   * @param aSpecifiedStyle  Whether the class and style attributes should
   *                         be preserved or discarded.
   * @param aEditingHost     The editing host.
   * @return            A candidate position to put caret.  If there is
   *                    AutoTransactionsConserveSelection instances, this stops
   *                    suggesting caret point only in some cases.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  ClearStyleAt(const EditorDOMPoint& aPoint,
               const EditorInlineStyle& aStyleToRemove,
               SpecifiedStyle aSpecifiedStyle, const Element& aEditingHost);

  MOZ_CAN_RUN_SCRIPT nsresult SetPositionToAbsolute(Element& aElement);
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetPositionToStatic(Element& aElement);

  /**
   * Called when aRunner starts calling a DOM API to modify the DOM.
   *
   * @return The previous runner if the DOM API calls are unfortunately nested.
   */
  [[nodiscard]] const AutoDOMAPIWrapperBase* OnDOMAPICallStart(
      const AutoDOMAPIWrapperBase& aRunner);

  /**
   * Called when aRunner ends calling a DOM API to modify the DOM.
   *
   * @param aPrevRunner Must be set to the result of the preceding
   *                    OnDOMMutationStart() call.
   */
  void OnDOMAPICallEnd(const AutoDOMAPIWrapperBase* aPrevRunner);

  class DocumentModifiedEvent;

  /**
   * OnModifyDocument() is called when the editor is changed.  This should
   * be called only by DocumentModifiedEvent when AutoEditActionDataSetter
   * instance is in the stack.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  OnModifyDocument(const DocumentModifiedEvent& aRunner);

  /**
   * DoSplitNode() inserts aNewNode and moves all content before or after
   * aStartOfRightNode to aNewNode.
   *
   * @param aStartOfRightNode   The point to split.  The container will keep
   *                            having following or previous content of this.
   * @param aNewNode            The new node called.  The previous or following
   *                            content of aStartOfRightNode will be moved into
   *                            this node.
   */
  MOZ_CAN_RUN_SCRIPT Result<SplitNodeResult, nsresult> DoSplitNode(
      const EditorDOMPoint& aStartOfRightNode, nsIContent& aNewNode);

  /**
   * DoJoinNodes() merges contents in aContentToRemove to aContentToKeep and
   * remove aContentToRemove from the DOM tree.  aContentToRemove and
   * aContentToKeep must have same parent.  Additionally, if one of
   * aContentToRemove or aContentToKeep is a text node, the other must be a
   * text node.
   *
   * @param aContentToKeep    The node that will remain after the join.
   * @param aContentToRemove  The node that will be joined with aContentToKeep.
   *                          There is no requirement that the two nodes be of
   *                          the same type.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DoJoinNodes(nsIContent& aContentToKeep, nsIContent& aContentToRemove);

  /**
   * Called when JoinNodesTransaction::DoTransaction() did its transaction.
   * Note that this is not called when undoing nor redoing.
   *
   * @param aTransaction        The transaction which did join nodes.
   * @param aDoJoinNodesResult  Result of the doing join nodes.
   */
  MOZ_CAN_RUN_SCRIPT void DidJoinNodesTransaction(
      const JoinNodesTransaction& aTransaction, nsresult aDoJoinNodesResult);

 protected:  // edit sub-action handler
  /**
   * CanHandleHTMLEditSubAction() checks whether there is at least one
   * selection range or not, and whether the first range is editable.
   * If it's not editable, `Canceled()` of the result returns true.
   * If `Selection` is in odd situation, returns an error.
   *
   * XXX I think that `IsSelectionEditable()` is better name, but it's already
   *     in `EditorBase`...
   */
  enum class CheckSelectionInReplacedElement { No, Yes, OnlyWhenNotInSameNode };
  Result<EditActionResult, nsresult> CanHandleHTMLEditSubAction(
      CheckSelectionInReplacedElement aCheckSelectionInReplacedElement =
          CheckSelectionInReplacedElement::Yes) const;

  /**
   * EnsureCaretNotAfterInvisibleBRElement() makes sure that caret is NOT after
   * padding `<br>` element for preventing insertion after padding `<br>`
   * element at empty last line.
   * NOTE: This method should be called only when `Selection` is collapsed
   *       because `Selection` is a pain to work with when not collapsed.
   *       (no good way to extend start or end of selection), so we need to
   *       ignore those types of selections.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  EnsureCaretNotAfterInvisibleBRElement(const Element& aEditingHost);

  /**
   * MaybeCreatePaddingBRElementForEmptyEditor() creates padding <br> element
   * for empty editor if there is no children.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MaybeCreatePaddingBRElementForEmptyEditor();

  /**
   * EnsureNoPaddingBRElementForEmptyEditor() removes padding <br> element
   * for empty editor if there is.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  EnsureNoPaddingBRElementForEmptyEditor();

  /**
   * ReflectPaddingBRElementForEmptyEditor() scans the tree from the root
   * element and sets mPaddingBRElementForEmptyEditor if exists, or otherwise
   * nullptr.  Can be used to manage undo/redo.
   */
  [[nodiscard]] nsresult ReflectPaddingBRElementForEmptyEditor();

  /**
   * PrepareInlineStylesForCaret() consider inline styles from top level edit
   * sub-action and setting it to `mPendingStylesToApplyToNewContent` and clear
   * inline style cache if necessary.
   * NOTE: This method should be called only when `Selection` is collapsed.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult PrepareInlineStylesForCaret();

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  HandleInsertText(const nsAString& aInsertionString,
                   InsertTextFor aPurpose) final;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertDroppedDataTransferAsAction(
      AutoEditActionDataSetter& aEditActionData, DataTransfer& aDataTransfer,
      const EditorDOMPoint& aDroppedAt, nsIPrincipal* aSourcePrincipal) final;

  /**
   * GetInlineStyles() retrieves the style of aElement and modifies each item of
   * aPendingStyleCacheArray.  This might cause flushing layout at retrieving
   * computed values of CSS properties.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult GetInlineStyles(
      Element& aElement, AutoPendingStyleCacheArray& aPendingStyleCacheArray);

  /**
   * CacheInlineStyles() caches style of aElement into mCachedPendingStyles of
   * TopLevelEditSubAction.  This may cause flushing layout at retrieving
   * computed value of CSS properties.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CacheInlineStyles(Element& aElement);

  /**
   * ReapplyCachedStyles() restores some styles which are disappeared during
   * handling edit action and it should be restored.  This may cause flushing
   * layout at retrieving computed value of CSS properties.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult ReapplyCachedStyles();

  /**
   * CreateStyleForInsertText() sets CSS properties which are stored in
   * PendingStyles to proper element node.
   *
   * @param aPointToInsertText  The point to insert text.
   * @param aEditingHost        The editing host.
   * @return                    A suggest point to put caret or unset point.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  CreateStyleForInsertText(const EditorDOMPoint& aPointToInsertText,
                           const Element& aEditingHost);

  /**
   * GetMostDistantAncestorMailCiteElement() returns most-ancestor mail cite
   * element. "mail cite element" is <pre> element when it's in plaintext editor
   * mode or an element with which calling HTMLEditUtils::IsMailCite() returns
   * true.
   *
   * @param aNode       The start node to look for parent mail cite elements.
   */
  Element* GetMostDistantAncestorMailCiteElement(const nsINode& aNode) const;

  /**
   * Splits inclusive inline ancestors at both start and end of aRangeItem.  If
   * this splits at every point, this modifies aRangeItem to point each split
   * point (typically, at right node).
   *
   * @param aRangeItem          [in/out] One or two DOM points where should be
   *                            split.  Will be modified to split point if
   *                            they're split.
   * @param aBlockInlineCheck   [in] Whether this method considers block vs.
   *                            inline with computed style or the default style.
   * @param aEditingHost        [in] The editing host.
   * @param aAncestorLimiter    [in/optional] If specified, this stops splitting
   *                            ancestors when meets this node.
   * @return                    A suggest point to put caret if succeeded, but
   *                            it may be unset.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SplitInlineAncestorsAtRangeBoundaries(
      RangeItem& aRangeItem, BlockInlineCheck aBlockInlineCheck,
      const Element& aEditingHost,
      const nsIContent* aAncestorLimiter = nullptr);

  /**
   * SplitElementsAtEveryBRElement() splits before all <br> elements in
   * aMostAncestorToBeSplit.  All <br> nodes will be moved before right node
   * at splitting its parent.  Finally, this returns left node, first <br>
   * element, next left node, second <br> element... and right-most node.
   *
   * @param aMostAncestorToBeSplit      Most-ancestor element which should
   *                                    be split.
   * @param aOutArrayOfNodes            First left node, first <br> element,
   *                                    Second left node, second <br> element,
   *                                    ...right-most node.  So, all nodes
   *                                    in this list should be siblings (may be
   *                                    broken the relation by mutation event
   *                                    listener though). If first <br> element
   *                                    is first leaf node of
   *                                    aMostAncestorToBeSplit, starting from
   *                                    the first <br> element.
   * @return                            A suggest point to put caret if
   *                                    succeeded, but it may unset.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SplitElementsAtEveryBRElement(
      nsIContent& aMostAncestorToBeSplit,
      nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents);

  /**
   * MaybeSplitElementsAtEveryBRElement() calls SplitElementsAtEveryBRElement()
   * for each given node when this needs to do that for aEditSubAction.
   * If split a node, it in aArrayOfContents is replaced with split nodes and
   * <br> elements.
   *
   * @return                            A suggest point to put caret if
   *                                    succeeded, but it may unset.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  MaybeSplitElementsAtEveryBRElement(
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      EditSubAction aEditSubAction);

  /**
   * CreateRangeIncludingAdjuscentWhiteSpaces() creates an nsRange instance
   * which may be expanded from the given range to include adjuscent
   * white-spaces.  If this fails handling something, returns nullptr.
   */
  template <typename EditorDOMRangeType>
  already_AddRefed<nsRange> CreateRangeIncludingAdjuscentWhiteSpaces(
      const EditorDOMRangeType& aRange);
  template <typename EditorDOMPointType1, typename EditorDOMPointType2>
  already_AddRefed<nsRange> CreateRangeIncludingAdjuscentWhiteSpaces(
      const EditorDOMPointType1& aStartPoint,
      const EditorDOMPointType2& aEndPoint);

  /**
   * GetRangeExtendedToHardLineEdgesForBlockEditAction() returns an extended
   * range if aRange should be extended before handling a block level editing.
   * If aRange start and/or end point <br> or something non-editable point, they
   * should be moved to nearest text node or something where the other methods
   * easier to handle edit action.
   */
  [[nodiscard]] Result<EditorRawDOMRange, nsresult>
  GetRangeExtendedToHardLineEdgesForBlockEditAction(
      const nsRange* aRange, const Element& aEditingHost) const;

  /**
   * InitializeInsertingElement is a callback type of methods which inserts
   * an element into the DOM tree.  This is called immediately before inserting
   * aNewElement into the DOM tree.
   *
   * @param aHTMLEditor     The HTML editor which modifies the DOM tree.
   * @param aNewElement     The new element which will be or was inserted into
   *                        the DOM tree.
   * @param aPointToInsert  The position aNewElement will be or was inserted.
   */
  using InitializeInsertingElement =
      std::function<nsresult(HTMLEditor& aHTMLEditor, Element& aNewElement,
                             const EditorDOMPoint& aPointToInsert)>;
  static InitializeInsertingElement DoNothingForNewElement;
  static InitializeInsertingElement InsertNewBRElement;

  /**
   * Helper methods to implement InitializeInsertingElement.
   */
  MOZ_CAN_RUN_SCRIPT static Result<CreateElementResult, nsresult>
  AppendNewElementToInsertingElement(
      HTMLEditor& aHTMLEditor, const nsStaticAtom& aTagName,
      Element& aNewElement,
      const InitializeInsertingElement& aInitializer = DoNothingForNewElement);
  MOZ_CAN_RUN_SCRIPT static Result<CreateElementResult, nsresult>
  AppendNewElementWithBRToInsertingElement(HTMLEditor& aHTMLEditor,
                                           const nsStaticAtom& aTagName,
                                           Element& aNewElement);

  /**
   * Create an element node whose name is aTag at before aPointToInsert.  When
   * this succeed to create an element node, this inserts the element to
   * aPointToInsert.
   *
   * @param aWithTransaction    Whether the inserting is new element is undoable
   *                            or not.  WithTransaction::No is useful only when
   *                            the new element is inserted into a new element
   *                            which has not been connected yet.
   * @param aTagName            The element name to create.
   * @param aPointToInsert      The insertion point of new element.
   *                            If this refers end of the container or after,
   *                            the transaction will append the element to the
   *                            container.
   *                            Otherwise, will insert the element before the
   *                            child node referred by this.
   *                            Note that this point will be invalid once this
   *                            method inserts the new element.
   * @param aInitializer        A function to initialize the new element before
   *                            connecting the element into the DOM tree. Note
   *                            that this should not touch outside given element
   *                            because doing it would break range updater's
   *                            result.
   * @return                    The created new element node and candidate caret
   *                            position.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  CreateAndInsertElement(
      WithTransaction aWithTransaction, const nsAtom& aTagName,
      const EditorDOMPoint& aPointToInsert,
      const InitializeInsertingElement& aInitializer = DoNothingForNewElement);

  /**
   * Callback of CopyAttributes().
   *
   * @param aHTMLEditor   The HTML editor.
   * @param aSrcElement   The element which have the attribute.
   * @param aDestElement  The element which will have the attribute.
   * @param aNamespaceID  [in] The namespace ID of aAttrName.
   * @param aAttrName     [in] The attribute name which will be copied.
   * @param aValue        [in/out] The attribute value which will be copied.
   *                      Once updated, the new value is used.
   * @return              true if the attribute should be copied, otherwise,
   *                      false.
   */
  using AttributeFilter = std::function<bool(
      HTMLEditor& aHTMLEditor, Element& aSrcElement, Element& aDestElement,
      int32_t aNamespaceID, const nsAtom& aAttrName, nsString& aValue)>;
  static AttributeFilter CopyAllAttributes;
  static AttributeFilter CopyAllAttributesExceptId;
  static AttributeFilter CopyAllAttributesExceptDir;
  static AttributeFilter CopyAllAttributesExceptIdAndDir;

  /**
   * Copy all attributes of aSrcElement to aDestElement as-is.  Different from
   * EditorBase::CloneAttributesWithTransaction(), this does not use
   * SetAttributeOrEquivalent() nor does not clear existing attributes of
   * aDestElement.
   *
   * @param aWithTransaction    Whether recoding with transactions or not.
   * @param aDestElement        The element will have attributes.
   * @param aSrcElement         The element whose attributes will be copied.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult CopyAttributes(
      WithTransaction aWithTransaction, Element& aDestElement,
      Element& aSrcElement, const AttributeFilter& = CopyAllAttributes);

  /**
   * MaybeSplitAncestorsForInsertWithTransaction() does nothing if container of
   * aStartOfDeepestRightNode can have an element whose tag name is aTag.
   * Otherwise, looks for an ancestor node which is or is in active editing
   * host and can have an element whose name is aTag.  If there is such
   * ancestor, its descendants are split.
   *
   * Note that this may create empty elements while splitting ancestors.
   *
   * @param aTag                        The name of element to be inserted
   *                                    after calling this method.
   * @param aStartOfDeepestRightNode    The start point of deepest right node.
   *                                    This point must be in aEditingHost.
   * @param aEditingHost                The editing host.
   * @return                            When succeeded, SplitPoint() returns
   *                                    the point to insert the element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitNodeResult, nsresult>
  MaybeSplitAncestorsForInsertWithTransaction(
      const nsAtom& aTag, const EditorDOMPoint& aStartOfDeepestRightNode,
      const Element& aEditingHost);

  /**
   * InsertElementWithSplittingAncestorsWithTransaction() is a wrapper of
   * MaybeSplitAncestorsForInsertWithTransaction() and CreateAndInsertElement().
   * I.e., will create an element whose tag name is aTagName and split ancestors
   * if it's necessary, then, insert it.
   *
   * @param aTagName            The tag name which you want to insert new
   *                            element at aPointToInsert.
   * @param aPointToInsert      The insertion point.  New element will be
   *                            inserted before here.
   * @param aBRElementNextToSplitPoint
   *                            Whether <br> element should be deleted or
   *                            kept if and only if a <br> element follows
   *                            split point.
   * @param aEditingHost        The editing host with which we're handling it.
   * @param aInitializer        A function to initialize the new element before
   *                            connecting the element into the DOM tree. Note
   *                            that this should not touch outside given element
   *                            because doing it would break range updater's
   *                            result.
   * @return                    If succeeded, returns the new element node and
   *                            suggesting point to put caret.
   */
  enum class BRElementNextToSplitPoint { Keep, Delete };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertElementWithSplittingAncestorsWithTransaction(
      const nsAtom& aTagName, const EditorDOMPoint& aPointToInsert,
      BRElementNextToSplitPoint aBRElementNextToSplitPoint,
      const Element& aEditingHost,
      const InitializeInsertingElement& aInitializer = DoNothingForNewElement);

  /**
   * Split aElementToSplit at two points, before aStartOfMiddleElement and after
   * aEndOfMiddleElement.  If they are very start or very end of aBlockElement,
   * this won't create empty block.
   *
   * @param aElementToSplit         An element which will be split.
   * @param aStartOfMiddleElement   Start node of middle block element.
   * @param aEndOfMiddleElement     End node of middle block element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitRangeOffFromNodeResult, nsresult>
  SplitRangeOffFromElement(Element& aElementToSplit,
                           nsIContent& aStartOfMiddleElement,
                           nsIContent& aEndOfMiddleElement);

  /**
   * RemoveBlockContainerElementWithTransactionBetween() splits the nodes
   * at aStartOfRange and aEndOfRange, then, removes the middle element which
   * was split off from aBlockContainerElement and moves the ex-children to
   * where the middle element was.  I.e., all nodes between aStartOfRange and
   * aEndOfRange (including themselves) will be unwrapped from
   * aBlockContainerElement.
   *
   * @param aBlockContainerElement  The node which will be split.
   * @param aStartOfRange           The first node which will be unwrapped
   *                                from aBlockContainerElement.
   * @param aEndOfRange             The last node which will be unwrapped from
   *                                aBlockContainerElement.
   * @param aBlockInlineCheck       Whether this method considers block vs.
   *                                inline with computed style or the default
   *                                style.
   * @return                        The left content is new created left
   *                                element of aBlockContainerElement.
   *                                The right content is split element,
   *                                i.e., must be aBlockContainerElement.
   *                                The middle content is nullptr since
   *                                removing it is the job of this method.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitRangeOffFromNodeResult, nsresult>
  RemoveBlockContainerElementWithTransactionBetween(
      Element& aBlockContainerElement, nsIContent& aStartOfRange,
      nsIContent& aEndOfRange, BlockInlineCheck aBlockInlineCheck);

  /**
   * WrapContentsInBlockquoteElementsWithTransaction() inserts at least one
   * <blockquote> element and moves nodes in aArrayOfContents into new
   * <blockquote> elements. If aArrayOfContents includes a table related element
   * except <table>, this calls itself recursively to insert <blockquote> into
   * the cell.
   *
   * @param aArrayOfContents    Nodes which will be moved into created
   *                            <blockquote> elements.
   * @param aEditingHost        The editing host.
   * @return                    A blockquote element which is created at last
   *                            and a suggest of caret position if succeeded.
   *                            The caret suggestion may be unset if there is
   *                            no suggestion.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  WrapContentsInBlockquoteElementsWithTransaction(
      const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      const Element& aEditingHost);

  /**
   * Our traditional formatBlock was same as XUL cmd_paragraphState command.
   * However, the behavior is pretty different from the others and aligning
   * the XUL command behavior may break Thunderbird a lot because it handles
   * <blockquote> in a special path and <div> (generic block element) is not
   * treated as a format node and these things may be used for designing
   * current roles of the elements in the email composer of Thunderbird.
   * Therefore, we create a new mode for HTMLFormatBlockCommand to align
   * the behavior to the others but does not harm Thunderbird.
   */
  enum class FormatBlockMode {
    // Document.execCommand("formatBlock"). Cannot set new format to "normal"
    // nor "".  So, the paths to handle these ones are not used in this mode.
    HTMLFormatBlockCommand,
    // cmd_paragraphState.  Can set new format to "normal" or "" to remove
    // ancestor format blocks.
    XULParagraphStateCommand,
  };

  /**
   * RemoveBlockContainerElementsWithTransaction() removes all format blocks,
   * table related element, etc in aArrayOfContents from the DOM tree. If
   * aArrayOfContents has a format node, it will be removed and its contents
   * will be moved to where it was.
   * If aArrayOfContents has a table related element, <li>, <blockquote> or
   * <div>, it will be removed and its contents will be moved to where it was.
   *
   * @param aFormatBlockMode    Whether HTML formatBlock command or XUL
   *                            paragraphState command.
   *
   * @return            A suggest point to put caret if succeeded, but it may be
   *                    unset if there is no suggestion.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  RemoveBlockContainerElementsWithTransaction(
      const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      FormatBlockMode aFormatBlockMode, BlockInlineCheck aBlockInlineCheck);

  /**
   * CreateOrChangeFormatContainerElement() formats all nodes in
   * aArrayOfContents with block elements whose name is aNewFormatTagName.
   *
   * If aArrayOfContents has an inline element, a block element is created and
   * the inline element and following inline elements are moved into the new
   * block element.
   * If aArrayOfContents has <br> elements, they'll be removed from the DOM tree
   * and new block element will be created when there are some remaining inline
   * elements.
   * If aArrayOfContents has a block element, this calls itself with children of
   * the block element.  Then, new block element will be created when there are
   * some remaining inline elements.
   *
   * @param aArrayOfContents    Must be descendants of a node.
   * @param aNewFormatTagName   The element name of new block elements.
   * @param aFormatBlockMode    The replacing block element target type is for
   *                            whether HTML formatBLock command or XUL
   *                            paragraphState command.
   * @param aEditingHost        The editing host.
   * @return                    The latest created new block element and a
   *                            suggest point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  CreateOrChangeFormatContainerElement(
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      const nsStaticAtom& aNewFormatTagName, FormatBlockMode aFormatBlockMode,
      const Element& aEditingHost);

  /**
   * FormatBlockContainerWithTransaction() is implementation of "formatBlock"
   * command of `Document.execCommand()`.  This applies block style or removes
   * it.
   *
   * @param aSelectionRanges    The ranges which are cloned by selection or
   *                            updated from it with doing something before
   *                            calling this.
   * @param aNewFormatTagName   New block tag name.
   *                            If nsGkAtoms::normal or nsGkAtoms::_empty,
   *                            RemoveBlockContainerElementsWithTransaction()
   *                            will be called.
   *                            If nsGkAtoms::blockquote,
   *                            WrapContentsInBlockquoteElementsWithTransaction()
   *                            will be called.
   *                            Otherwise, CreateOrChangeBlockContainerElement()
   *                            will be called.
   * @param aFormatBlockMode    Whether HTML formatBlock command or XUL
   *                            paragraphState command.
   * @param aEditingHost        The editing host.
   * @return                    If selection should be finally collapsed in a
   *                            created block element, this returns the element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<RefPtr<Element>, nsresult>
  FormatBlockContainerWithTransaction(
      AutoClonedSelectionRangeArray& aSelectionRanges,
      const nsStaticAtom& aNewFormatTagName, FormatBlockMode aFormatBlockMode,
      const Element& aEditingHost);

  /**
   * Retrun true if the specified line break can be inserted around aContent.
   * If aContent is an Element, this checks whether the element can have the
   * line break.
   * If aContent is a Text, this check whether its container element can have
   * the line break.
   */
  [[nodiscard]] static bool CanInsertLineBreak(LineBreakType aLineBreakType,
                                               const nsIContent& aContent);

  /**
   * If aPointToInsert is between line breaks or block boundaries, this
   * puts a <br> element to make an empty line between them.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateLineBreakResult, nsresult>
  InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded(
      const EditorDOMPoint& aPointToInsert, const Element& aEditingHost);

  /**
   * Insert a padding <br> if aPoint is in an empty block.
   *
   * @param aPoint              The place where you want to put a padding line
   *                            break.
   * @param aDeleteEmptyInlines If nsIEditor::eStrip, this deletes empty inlines
   *                            before inserting a line break from the inserting
   *                            point.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateLineBreakResult, nsresult>
  InsertPaddingBRElementIfInEmptyBlock(
      const EditorDOMPoint& aPoint,
      nsIEditor::EStripWrappers aDeleteEmptyInlines);

  /**
   * Insert a padding <br> element for making preceding collapsible white-spaces
   * visible or the point is empty between block boundaries.
   *
   * @param aPoint              Where you want to check.  A padding <br> may be
   *                            inserted different from this point.
   * @param aDeleteEmptyInlines If nsIEditor::eStrip, this deletes empty inlines
   *                            before inserting <br> from the inserting point.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateLineBreakResult, nsresult>
  InsertPaddingBRElementIfNeeded(const EditorDOMPoint& aPoint,
                                 nsIEditor::EStripWrappers aDeleteEmptyInlines,
                                 const Element& aEditingHost);

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteRangesWithTransaction(nsIEditor::EDirection aDirectionAndAmount,
                              nsIEditor::EStripWrappers aStripWrappers,
                              AutoClonedRangeArray& aRangesToDelete) override;

  class AutoInsertParagraphHandler;
  class AutoInsertLineBreakHandler;

  /**
   * InsertParagraphSeparatorAsSubAction() handles insertParagraph command
   * (i.e., handling Enter key press) with the above helper methods.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  InsertParagraphSeparatorAsSubAction(const Element& aEditingHost);

  /**
   * InsertLineBreakAsSubAction() inserts a new <br> element or a linefeed
   * character at selection.  If there is non-collapsed selection ranges, the
   * selected ranges is deleted first.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertLineBreakAsSubAction();

  /**
   * ChangeListElementType() replaces child list items of aListElement with
   * new list item element whose tag name is aNewListItemTag.
   * Note that if there are other list elements as children of aListElement,
   * this calls itself recursively even though it's invalid structure.
   *
   * @param aListElement        The list element whose list items will be
   *                            replaced.
   * @param aNewListTag         New list tag name.
   * @param aNewListItemTag     New list item tag name.
   * @return                    New list element or an error code if it fails.
   *                            New list element may be aListElement if its
   *                            tag name is same as aNewListTag.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  ChangeListElementType(Element& aListElement, nsAtom& aListType,
                        nsAtom& aItemType);

  class AutoListElementCreator;

  [[nodiscard]] static bool IsFormatElement(FormatBlockMode aFormatBlockMode,
                                            const nsIContent& aContent);

  /**
   * MakeOrChangeListAndListItemAsSubAction() handles create list commands with
   * current selection.  If
   *
   * @param aListElementOrListItemElementTagName
   *                                    The new list element tag name or
   *                                    new list item tag name.
   *                                    If the former, list item tag name will
   *                                    be computed automatically.  Otherwise,
   *                                    list tag name will be computed.
   * @param aBulletType                 If this is not empty string, it's set
   *                                    to `type` attribute of new list item
   *                                    elements.  Otherwise, existing `type`
   *                                    attributes will be removed.
   * @param aSelectAllOfCurrentList     Yes if this should treat all of
   *                                    ancestor list element at selection.
   * @param aEditingHost                The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  MakeOrChangeListAndListItemAsSubAction(
      const nsStaticAtom& aListElementOrListItemElementTagName,
      const nsAString& aBulletType,
      SelectAllOfCurrentList aSelectAllOfCurrentList,
      const Element& aEditingHost);

  /**
   * DeleteTextAndTextNodesWithTransaction() removes text or text nodes in
   * the given range.
   */
  enum class TreatEmptyTextNodes {
    // KeepIfContainerOfRangeBoundaries:
    //   Will remove empty text nodes middle of the range, but keep empty
    //   text nodes which are containers of range boundaries.
    KeepIfContainerOfRangeBoundaries,
    // Remove:
    //   Will remove all empty text nodes.
    Remove,
    // RemoveAllEmptyInlineAncestors:
    //   Will remove all empty text nodes and its inline ancestors which
    //   become empty due to removing empty text nodes.
    RemoveAllEmptyInlineAncestors,
  };
  template <typename EditorDOMPointType>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteTextAndTextNodesWithTransaction(
      const EditorDOMPointType& aStartPoint,
      const EditorDOMPointType& aEndPoint,
      TreatEmptyTextNodes aTreatEmptyTextNodes);

  /**
   * Delete the line break with DeleteNodeTransaction or DeleteTextTransaction.
   *
   * @param aLineBreak          The line break to be deleted.
   * @param aDeleteEmptyInlines If nsIEditor::eStrip, this deletes new empty
   *                            inline element if and only if this deletes the
   *                            line break node.
   * @param aEditingHost        The editing host.
   * @return                    The point where the line break was.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  DeleteLineBreakWithTransaction(const EditorLineBreak& aLineBreak,
                                 nsIEditor::EStripWrappers aDeleteEmptyInlines,
                                 const Element& aEditingHost);

  /**
   * JoinNodesWithTransaction() joins aLeftContent and aRightContent.  Content
   * of aLeftContent will be merged into aRightContent.  Actual implemenation of
   * this method is JoinNodesImpl().  So, see its explanation for the detail.
   *
   * @param aLeftContent   Will be removed from the DOM tree.
   * @param aRightContent  The node which will be new container of the content
   *                       of aLeftContent.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<JoinNodesResult, nsresult>
  JoinNodesWithTransaction(nsIContent& aLeftContent, nsIContent& aRightContent);

  /**
   * JoinNearestEditableNodesWithTransaction() joins two editable nodes which
   * are themselves or the nearest editable node of aLeftNode and aRightNode.
   * XXX This method's behavior is odd.  For example, if user types Backspace
   *     key at the second editable paragraph in this case:
   *     <div contenteditable>
   *       <p>first editable paragraph</p>
   *       <p contenteditable="false">non-editable paragraph</p>
   *       <p>second editable paragraph</p>
   *     </div>
   *     The first editable paragraph's content will be moved into the second
   *     editable paragraph and the non-editable paragraph becomes the first
   *     paragraph of the editor.  I don't think that it's expected behavior of
   *     any users...
   *
   * @param aLeftNode   The node which will be removed.
   * @param aRightNode  The node which will be inserted the content of
   *                    aLeftNode.
   * @param aNewFirstChildOfRightNode
   *                    [out] The point at the first child of aRightNode.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  JoinNearestEditableNodesWithTransaction(
      nsIContent& aLeftNode, nsIContent& aRightNode,
      EditorDOMPoint* aNewFirstChildOfRightNode);

  /**
   * ReplaceContainerAndCloneAttributesWithTransaction() creates new element
   * whose name is aTagName, copies all attributes from aOldContainer to the
   * new element, moves all children in aOldContainer to the new element, then,
   * removes aOldContainer from the DOM tree.
   *
   * @param aOldContainer       The element node which should be replaced
   *                            with new element.
   * @param aTagName            The name of new element node.
   */
  [[nodiscard]] inline MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  ReplaceContainerAndCloneAttributesWithTransaction(Element& aOldContainer,
                                                    const nsAtom& aTagName);

  /**
   * ReplaceContainerWithTransaction() creates new element whose name is
   * aTagName, sets aAttributes of the new element to aAttributeValue, moves
   * all children in aOldContainer to the new element, then, removes
   * aOldContainer from the DOM tree.
   *
   * @param aOldContainer       The element node which should be replaced
   *                            with new element.
   * @param aTagName            The name of new element node.
   * @param aAttribute          Attribute name to be set to the new element.
   * @param aAttributeValue     Attribute value to be set to aAttribute.
   */
  [[nodiscard]] inline MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  ReplaceContainerWithTransaction(Element& aOldContainer,
                                  const nsAtom& aTagName,
                                  const nsAtom& aAttribute,
                                  const nsAString& aAttributeValue);

  /**
   * ReplaceContainerWithTransaction() creates new element whose name is
   * aTagName, moves all children in aOldContainer to the new element, then,
   * removes aOldContainer from the DOM tree.
   *
   * @param aOldContainer       The element node which should be replaced
   *                            with new element.
   * @param aTagName            The name of new element node.
   */
  [[nodiscard]] inline MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  ReplaceContainerWithTransaction(Element& aOldContainer,
                                  const nsAtom& aTagName);

  /**
   * RemoveContainerWithTransaction() removes aElement from the DOM tree and
   * moves all its children to the parent of aElement.
   *
   * @param aElement            The element to be removed.
   * @return                    A suggestion point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  RemoveContainerWithTransaction(Element& aElement);

  /**
   * InsertContainerWithTransaction() creates new element whose name is
   * aWrapperTagName, moves aContentToBeWrapped into the new element, then,
   * inserts the new element into where aContentToBeWrapped was.
   * NOTE: This method does not check if aContentToBeWrapped is valid child
   * of the new element.  So, callers need to guarantee it.
   *
   * @param aContentToBeWrapped The content which will be wrapped with new
   *                            element.
   * @param aWrapperTagName     Element name of new element which will wrap
   *                            aContent and be inserted into where aContent
   *                            was.
   * @param aInitializer        A callback to initialize new element before
   *                            inserting to the DOM tree.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertContainerWithTransaction(
      nsIContent& aContentToBeWrapped, const nsAtom& aWrapperTagName,
      const InitializeInsertingElement& aInitializer = DoNothingForNewElement);

  /**
   * MoveNodeWithTransaction() moves aContentToMove to aPointToInsert.
   *
   * @param aContentToMove  The node to be moved.
   * @param aPointToInsert  The point where aContentToMove will be inserted.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveNodeWithTransaction(nsIContent& aContentToMove,
                          const EditorDOMPoint& aPointToInsert);

  /**
   * Moves all siblings from aFirstContentToMove to aLastContentToMove to
   * aPointToInsert with a transaction.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveSiblingsWithTransaction(nsIContent& aFirstContentToMove,
                              nsIContent& aLastContentToMove,
                              const EditorDOMPoint& aPointToInsert);

  /**
   * MoveNodeToEndWithTransaction() moves aContentToMove to end of
   * aNewContainer.
   *
   * @param aContentToMove  The node to be moved.
   * @param aNewContainer   The new container which will contain aContentToMove
   *                        as its last child.
   */
  [[nodiscard]] inline MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveNodeToEndWithTransaction(nsIContent& aContentToMove,
                               nsINode& aNewContainer);

  /**
   * Moves all siblings from aFirstContentToMove to aLastContentToMove to the
   * end of aNewContainer with a transaction.
   */
  [[nodiscard]] inline MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveSiblingsToEndWithTransaction(nsIContent& aFirstContentToMove,
                                   nsIContent& aLastContentToMove,
                                   nsINode& aNewContainer);

  /**
   * MoveNodeOrChildrenWithTransaction() moves aContent to aPointToInsert.  If
   * cannot insert aContent due to invalid relation, moves only its children
   * recursively and removes aContent from the DOM tree.
   *
   * @param aContent            Content which should be moved.
   * @param aPointToInsert      The point to be inserted aContent or its
   *                            descendants.
   * @param aPreserveWhiteSpaceStyle
   *                            If yes and if it's possible to keep white-space
   *                            style, this method will set `style` attribute to
   *                            moving node or creating new <span> element.
   * @param aRemoveIfCommentNode
   *                            If yes, this removes invisible nodes such as
   *                            comment nodes, empty inline containers (even if
   *                            it has border, etc) instead of moving it to the
   *                            destination.  Note that this does not remove
   *                            invisible descendants in containers which have
   *                            visible nodes because it requires additional
   *                            scan.
   */
  enum class PreserveWhiteSpaceStyle { No, Yes };
  friend std::ostream& operator<<(
      std::ostream& aStream,
      const PreserveWhiteSpaceStyle aPreserveWhiteSpaceStyle);
  enum class RemoveIfInvisibleNode { No, Yes };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveNodeOrChildrenWithTransaction(
      nsIContent& aContentToMove, const EditorDOMPoint& aPointToInsert,
      PreserveWhiteSpaceStyle aPreserveWhiteSpaceStyle,
      RemoveIfInvisibleNode aRemoveIfInvisibleNode);

  /**
   * CanMoveNodeOrChildren() returns true if
   * `MoveNodeOrChildrenWithTransaction()` can move or delete at least a
   * descendant of aElement into aNewContainer.  I.e., when this returns true,
   * `MoveNodeOrChildrenWithTransaction()` must return "handled".
   */
  Result<bool, nsresult> CanMoveNodeOrChildren(
      const nsIContent& aContent, const nsINode& aNewContainer) const;

  /**
   * MoveChildrenWithTransaction() moves the children of aElement to
   * aPointToInsert.  If cannot insert some children due to invalid relation,
   * calls MoveNodeOrChildrenWithTransaction() to remove the children but keep
   * moving its children.
   *
   * @param aElement            Container element whose children should be
   *                            moved.
   * @param aPointToInsert      The point to be inserted children of aElement
   *                            or its descendants.
   * @param aPreserveWhiteSpaceStyle
   *                            If yes and if it's possible to keep white-space
   *                            style, this method will set `style` attribute to
   *                            moving node or creating new <span> element.
   * @param aRemoveIfCommentNode
   *                            If yes, this removes invisible nodes such as
   *                            comment nodes, empty inline containers (even if
   *                            it has border, etc) instead of moving it to the
   *                            destination.  Note that this does not remove
   *                            invisible descendants in containers which have
   *                            visible nodes because it requires additional
   *                            scan.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<MoveNodeResult, nsresult>
  MoveChildrenWithTransaction(Element& aElement,
                              const EditorDOMPoint& aPointToInsert,
                              PreserveWhiteSpaceStyle aPreserveWhiteSpaceStyle,
                              RemoveIfInvisibleNode aRemoveIfInvisibleNode);

  /**
   * CanMoveChildren() returns true if `MoveChildrenWithTransaction()` can move
   * at least a descendant of aElement into aNewContainer.  I.e., when this
   * returns true, `MoveChildrenWithTransaction()` return "handled".
   */
  Result<bool, nsresult> CanMoveChildren(const Element& aElement,
                                         const nsINode& aNewContainer) const;

  /**
   * MoveAllChildren() moves all children of aContainer to before
   * aPointToInsert.GetChild().
   * See explanation of MoveChildrenBetween() for the detail of the behavior.
   *
   * @param aContainer          The container node whose all children should
   *                            be moved.
   * @param aPointToInsert      The insertion point.  The container must not
   *                            be a data node like a text node.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MoveAllChildren(nsINode& aContainer, const EditorRawDOMPoint& aPointToInsert);

  /**
   * MoveChildrenBetween() moves all children between aFirstChild and aLastChild
   * to before aPointToInsert.GetChild(). If some children are moved to
   * different container while this method moves other children, they are just
   * ignored. If the child node referred by aPointToInsert is moved to different
   * container while this method moves children, returns error.
   *
   * @param aFirstChild         The first child which should be moved to
   *                            aPointToInsert.
   * @param aLastChild          The last child which should be moved.  This
   *                            must be a sibling of aFirstChild and it should
   *                            be positioned after aFirstChild in the DOM tree
   *                            order.
   * @param aPointToInsert      The insertion point.  The container must not
   *                            be a data node like a text node.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MoveChildrenBetween(nsIContent& aFirstChild, nsIContent& aLastChild,
                      const EditorRawDOMPoint& aPointToInsert);

  /**
   * MovePreviousSiblings() moves all siblings before aChild (i.e., aChild
   * won't be moved) to before aPointToInsert.GetChild().
   * See explanation of MoveChildrenBetween() for the detail of the behavior.
   *
   * @param aChild              The node which is next sibling of the last
   *                            node to be moved.
   * @param aPointToInsert      The insertion point.  The container must not
   *                            be a data node like a text node.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MovePreviousSiblings(
      nsIContent& aChild, const EditorRawDOMPoint& aPointToInsert);

  /**
   * MoveInclusiveNextSiblings() moves aChild and all siblings after it to
   * before aPointToInsert.GetChild().
   * See explanation of MoveChildrenBetween() for the detail of the behavior.
   *
   * @param aChild              The node which is first node to be moved.
   * @param aPointToInsert      The insertion point.  The container must not
   *                            be a data node like a text node.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MoveInclusiveNextSiblings(
      nsIContent& aChild, const EditorRawDOMPoint& aPointToInsert);

  /**
   * SplitNodeWithTransaction() creates a transaction to create a new node
   * (left node) identical to an existing node (right node), and split the
   * contents between the same point in both nodes, then, execute the
   * transaction.
   *
   * @param aStartOfRightNode   The point to split.  Its container will be
   *                            the right node, i.e., become the new node's
   *                            next sibling.  And the point will be start
   *                            of the right node.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitNodeResult, nsresult>
  SplitNodeWithTransaction(const EditorDOMPoint& aStartOfRightNode);

  /**
   * SplitNodeDeepWithTransaction() splits aMostAncestorToSplit deeply.
   *
   * @param aMostAncestorToSplit        The most ancestor node which should be
   *                                    split.
   * @param aStartOfDeepestRightNode    The start point of deepest right node.
   *                                    This point must be descendant of
   *                                    aMostAncestorToSplit.
   * @param aSplitAtEdges               Whether the caller allows this to
   *                                    create empty container element when
   *                                    split point is start or end of an
   *                                    element.
   * @return                            SplitPoint() returns split point in
   *                                    aMostAncestorToSplit.  The point must
   *                                    be good to insert something if the
   *                                    caller want to do it.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitNodeResult, nsresult>
  SplitNodeDeepWithTransaction(nsIContent& aMostAncestorToSplit,
                               const EditorDOMPoint& aDeepestStartOfRightNode,
                               SplitAtEdges aSplitAtEdges);

  /**
   * DeleteEmptyInclusiveAncestorInlineElements() removes empty inclusive
   * ancestor inline elements in inclusive ancestor block element of aContent.
   *
   * @param aContent       Must be an empty content.
   * @param aEditingHost   The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteEmptyInclusiveAncestorInlineElements(nsIContent& aContent,
                                             const Element& aEditingHost);

  /**
   * DeleteTextAndNormalizeSurroundingWhiteSpaces() deletes text between
   * aStartToDelete and immediately before aEndToDelete and return new caret
   * position.  If surrounding characters are white-spaces, this normalize them
   * too.  Finally, inserts `<br>` element if it's required.
   * Note that if you wants only normalizing white-spaces, you can set same
   * point to both aStartToDelete and aEndToDelete.  Then, this tries to
   * normalize white-space sequence containing previous character of
   * aStartToDelete.
   */
  enum class DeleteDirection {
    Forward,
    Backward,
  };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CaretPoint, nsresult>
  DeleteTextAndNormalizeSurroundingWhiteSpaces(
      const EditorDOMPointInText& aStartToDelete,
      const EditorDOMPointInText& aEndToDelete,
      TreatEmptyTextNodes aTreatEmptyTextNodes,
      DeleteDirection aDeleteDirection, const Element& aEditingHost);

  enum class NormalizeSurroundingWhiteSpaces : bool { No, Yes };
  friend constexpr bool operator!(NormalizeSurroundingWhiteSpaces aValue) {
    return !static_cast<bool>(aValue);
  }

  /**
   * Return a normalized string.  If aPointToInsert is in a `Text` node,
   * this returns a range in the `Text` to replace surrounding white-spaces at
   * aPointToInsert with the normalized string if white-spaces are collapsible
   * and aNormalizeSurroundingWhiteSpaces is "Yes".
   */
  NormalizedStringToInsertText NormalizeWhiteSpacesToInsertText(
      const EditorDOMPoint& aPointToInsert, const nsAString& aStringToInsert,
      NormalizeSurroundingWhiteSpaces aNormalizeSurroundingWhiteSpaces) const;

  /**
   * Return normalized white-spaces of a white-space sequence which contains
   * aPoint.  This returns new offset of aPoint.Offset() after replacing the
   * white-space sequence with normalized white-spaces.
   */
  ReplaceWhiteSpacesData GetNormalizedStringAt(
      const EditorDOMPointInText& aPoint) const;

  /**
   * Return normalized white-spaces after aPointToSplit if there are some
   * collapsible white-spaces after the point.
   */
  ReplaceWhiteSpacesData GetFollowingNormalizedStringToSplitAt(
      const EditorDOMPointInText& aPointToSplit) const;

  /**
   * Return normalized white-spaces before aPointToSplit if there are some
   * collapsible white-spaces before the point.
   */
  ReplaceWhiteSpacesData GetPrecedingNormalizedStringToSplitAt(
      const EditorDOMPointInText& aPointToSplit) const;

  /**
   * Return normalized surrounding white-spaces of the given range in aTextNode
   * if there are some collapsible white-spaces.
   */
  ReplaceWhiteSpacesData GetSurroundingNormalizedStringToDelete(
      const Text& aTextNode, uint32_t aOffset, uint32_t aLength) const;

  /**
   * ExtendRangeToDeleteWithNormalizingWhiteSpaces() is a helper method of
   * DeleteTextAndNormalizeSurroundingWhiteSpaces().  This expands
   * aStartToDelete and/or aEndToDelete if there are white-spaces which need
   * normalizing.
   *
   * @param aStartToDelete      [In/Out] Start to delete.  If this point
   *                            follows white-spaces, this may be modified.
   * @param aEndToDelete        [In/Out] Next point of last content to be
   *                            deleted.  If this point is a white-space,
   *                            this may be modified.
   * @param aNormalizedWhiteSpacesInStartNode
   *                            [Out] If container text node of aStartToDelete
   *                            should be modified, this offers new string
   *                            in the range in the text node.
   * @param aNormalizedWhiteSpacesInEndNode
   *                            [Out] If container text node of aEndToDelete
   *                            is different from the container of
   *                            aStartToDelete and it should be modified, this
   *                            offers new string in the range in the text node.
   */
  void ExtendRangeToDeleteWithNormalizingWhiteSpaces(
      EditorDOMPointInText& aStartToDelete, EditorDOMPointInText& aEndToDelete,
      nsString& aNormalizedWhiteSpacesInStartNode,
      nsString& aNormalizedWhiteSpacesInEndNode) const;

  /**
   * CharPointType let the following helper methods of
   * ExtendRangeToDeleteWithNormalizingWhiteSpaces() know what type of
   * character will be previous or next char point after deletion.
   */
  enum class CharPointType {
    TextEnd,  // Start or end of the text (hardline break or replaced inline
              // element)
    ASCIIWhiteSpace,   // One of ASCII white-spaces (collapsible white-space)
    NoBreakingSpace,   // NBSP
    VisibleChar,       // Non-white-space characters
    PreformattedChar,  // Any character except a linefeed in a preformatted
                       // node.
    PreformattedLineBreak,  // Preformatted linebreak
  };

  /**
   * GetPreviousCharPointType() and GetCharPointType() get type of
   * previous/current char point from current DOM tree.  In other words, if the
   * point will be deleted, you cannot use these methods.
   */
  template <typename EditorDOMPointType>
  static CharPointType GetPreviousCharPointType(
      const EditorDOMPointType& aPoint);
  template <typename EditorDOMPointType>
  static CharPointType GetCharPointType(const EditorDOMPointType& aPoint);

  /**
   * CharPointData let the following helper methods of
   * ExtendRangeToDeleteWithNormalizingWhiteSpaces() know what type of
   * character will be previous or next char point and the point is
   * in same or different text node after deletion.
   */
  class MOZ_STACK_CLASS CharPointData final {
   public:
    CharPointData() = delete;

    static CharPointData InDifferentTextNode(CharPointType aCharPointType) {
      return {aCharPointType, true};
    }
    static CharPointData InSameTextNode(CharPointType aCharPointType) {
      // Let's mark this as in different text node if given one indicates
      // that there is end of text because it means that adjacent content
      // from point of text node view is another element.
      return {aCharPointType, aCharPointType == CharPointType::TextEnd};
    }

    bool AcrossTextNodeBoundary() const { return mIsInDifferentTextNode; }
    bool IsCollapsibleWhiteSpace() const {
      return mType == CharPointType::ASCIIWhiteSpace ||
             mType == CharPointType::NoBreakingSpace;
    }
    CharPointType Type() const { return mType; }

   private:
    CharPointData(CharPointType aType, bool aIsInDifferentTextNode)
        : mType(aType), mIsInDifferentTextNode(aIsInDifferentTextNode) {}

    CharPointType mType;
    bool mIsInDifferentTextNode;
  };

  /**
   * GetPreviousCharPointDataForNormalizingWhiteSpaces() and
   * GetInclusiveNextCharPointDataForNormalizingWhiteSpaces() is helper methods
   * of ExtendRangeToDeleteWithNormalizingWhiteSpaces().  This retrieves
   * previous or inclusive next editable char point and returns its data.
   */
  CharPointData GetPreviousCharPointDataForNormalizingWhiteSpaces(
      const EditorDOMPointInText& aPoint) const;
  CharPointData GetInclusiveNextCharPointDataForNormalizingWhiteSpaces(
      const EditorDOMPointInText& aPoint) const;

  enum class Linefeed : bool { Collapsible, Preformatted };

  /**
   * Normalize all white-spaces in aResult. aPreviousCharPointData is used only
   * when the first character of aResult is an ASCII space or an NBSP.
   * aNextCharPointData is used only when the last character of aResult is an
   * ASCII space or an NBSP.
   */
  static void NormalizeAllWhiteSpaceSequences(
      nsString& aResult, const CharPointData& aPreviousCharPointData,
      const CharPointData& aNextCharPointData, Linefeed aLinefeed);

  /**
   * GenerateWhiteSpaceSequence() generates white-space sequence which won't
   * be collapsed.
   *
   * @param aResult             [out] White space sequence which won't be
   *                            collapsed, but wrappable.
   * @param aLength             Length of generating white-space sequence.
   *                            Must be 1 or larger.
   * @param aPreviousCharPointData
   *                            Specify the previous char point where it'll be
   *                            inserted.  Currently, for keepin this method
   *                            simple, does not support to generate a part
   *                            of white-space sequence in a text node.  So,
   *                            if the type is white-space, it must indicate
   *                            different text nodes white-space.
   * @param aNextCharPointData  Specify the next char point where it'll be
   *                            inserted.  Same as aPreviousCharPointData,
   *                            this must node indicate white-space in same
   *                            text node.
   */
  static void GenerateWhiteSpaceSequence(
      nsString& aResult, uint32_t aLength,
      const CharPointData& aPreviousCharPointData,
      const CharPointData& aNextCharPointData);

  /**
   * Replace characters starting from aOffset in aResult with normalized
   * white-space sequence.
   */
  static void ReplaceStringWithNormalizedWhiteSpaceSequence(
      nsString& aResult, uint32_t aOffset, uint32_t aLength,
      const CharPointData& aPreviousCharPointData,
      const CharPointData& aNextCharPointData);

  /**
   * ComputeTargetRanges() computes actual delete ranges which will be deleted
   * unless the following `beforeinput` event is canceled.
   *
   * @param aDirectionAndAmount         The direction and amount of deletion.
   * @param aRangesToDelete             [In/Out] The ranges to be deleted,
   *                                    typically, initialized with the
   *                                    selection ranges.  This may be modified
   *                                    if selection ranges should be extened.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  ComputeTargetRanges(nsIEditor::EDirection aDirectionAndAmount,
                      AutoClonedSelectionRangeArray& aRangesToDelete) const;

  /**
   * This method handles "delete selection" commands.
   *
   * @param aDirectionAndAmount Direction of the deletion.
   * @param aStripWrappers      Must be eStrip or eNoStrip.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  HandleDeleteSelection(nsIEditor::EDirection aDirectionAndAmount,
                        nsIEditor::EStripWrappers aStripWrappers) final;

  class AutoDeleteRangesHandler;
  class AutoMoveOneLineHandler;

  /**
   * DeleteMostAncestorMailCiteElementIfEmpty() deletes most ancestor
   * mail cite element (`<blockquote type="cite">` or
   * `<span _moz_quote="true">`, the former can be created with middle click
   * paste with `Control` or `Command` even in the web) of aContent if it
   * becomes empty.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DeleteMostAncestorMailCiteElementIfEmpty(nsIContent& aContent);

  /**
   * LiftUpListItemElement() moves aListItemElement outside its parent.
   * If it's in a middle of a list element, the parent list element is split
   * before aListItemElement.  Then, moves aListItemElement to before its
   * parent list element.  I.e., moves aListItemElement between the 2 list
   * elements if original parent was split.  Then, if new parent becomes not a
   * list element, the list item element is removed and its contents are moved
   * to where the list item element was.  If aListItemElement becomse not a
   * child of list element, its contents are unwrapped from aListItemElement.
   *
   * @param aListItemElement    Must be a <li>, <dt> or <dd> element.
   * @param aLiftUpFromAllParentListElements
   *                            If Yes, this method calls itself recursively
   *                            to unwrap the contents in aListItemElement
   *                            from any ancestor list elements.
   *                            XXX This checks only direct parent of list
   *                                elements.  Therefore, if a parent list
   *                                element in a list item element, the
   *                                list item element and its list element
   *                                won't be unwrapped.
   */
  enum class LiftUpFromAllParentListElements { Yes, No };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult LiftUpListItemElement(
      dom::Element& aListItemElement,
      LiftUpFromAllParentListElements aLiftUpFromAllParentListElements);

  /**
   * DestroyListStructureRecursively() destroys the list structure of
   * aListElement recursively.
   * If aListElement has <li>, <dl> or <dt> as a child, the element is removed
   * but its descendants are moved to where the list item element was.
   * If aListElement has another <ul>, <ol> or <dl> as a child, this method is
   * called recursively.
   * If aListElement has other nodes as its child, they are just removed.
   * Finally, aListElement is removed. and its all children are moved to
   * where the aListElement was.
   *
   * @param aListElement        A <ul>, <ol> or <dl> element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  DestroyListStructureRecursively(Element& aListElement);

  /**
   * RemoveListAtSelectionAsSubAction() removes list elements and list item
   * elements at Selection.  And move contents in them where the removed list
   * was.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  RemoveListAtSelectionAsSubAction(const Element& aEditingHost);

  /**
   * ChangeMarginStart() changes margin of aElement to indent or outdent.
   * If it's rtl text, margin-right will be changed.  Otherwise, margin-left.
   * XXX This is not aware of vertical writing-mode.
   *
   * @param aElement            The element whose start margin should be
   *                            changed.
   * @param aChangeMargin       Whether increase or decrease the margin.
   * @param aEditingHost        The editing host.
   * @return                    May suggest a suggest point to put caret.
   */
  enum class ChangeMargin { Increase, Decrease };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  ChangeMarginStart(Element& aElement, ChangeMargin aChangeMargin,
                    const Element& aEditingHost);

  /**
   * HandleCSSIndentAroundRanges() indents around aRanges with CSS.
   *
   * @param aRanges             The ranges where the content should be indented.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult HandleCSSIndentAroundRanges(
      AutoClonedSelectionRangeArray& aRanges, const Element& aEditingHost);

  /**
   * HandleCSSIndentAroundRanges() indents around aRanges with HTML.
   *
   * @param aRanges             The ranges where the content should be indented.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult HandleHTMLIndentAroundRanges(
      AutoClonedSelectionRangeArray& aRanges, const Element& aEditingHost);

  /**
   * HandleIndentAtSelection() indents around Selection with HTML or CSS.
   *
   * @param aEditingHost        The editing host.
   */
  // TODO: Make this take AutoClonedSelectionRangeArray instead of retrieving
  // `Selection`
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  HandleIndentAtSelection(const Element& aEditingHost);

  /**
   * OutdentPartOfBlock() outdents the nodes between aStartOfOutdent and
   * aEndOfOutdent.  This splits the range off from aBlockElement first.
   * Then, removes the middle element if aIsBlockIndentedWithCSS is false.
   * Otherwise, decreases the margin of the middle element.
   *
   * @param aBlockElement       A block element which includes both
   *                            aStartOfOutdent and aEndOfOutdent.
   * @param aStartOfOutdent     First node which is descendant of
   *                            aBlockElement will be outdented.
   * @param aEndOfOutdent       Last node which is descandant of
   *                            aBlockElement will be outdented.
   * @param aBlockIndentedWith  `CSS` if aBlockElement is indented with
   *                            CSS margin property.
   *                            `HTML` if aBlockElement is `<blockquote>`
   *                            or something.
   * @param aEditingHost        The editing host.
   * @return                    The left content is new created element
   *                            splitting before aStartOfOutdent.
   *                            The right content is existing element.
   *                            The middle content is outdented element
   *                            if aBlockIndentedWith is `CSS`.
   *                            Otherwise, nullptr.
   */
  enum class BlockIndentedWith { CSS, HTML };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitRangeOffFromNodeResult, nsresult>
  OutdentPartOfBlock(Element& aBlockElement, nsIContent& aStartOfOutdent,
                     nsIContent& aEndOfOutdent,
                     BlockIndentedWith aBlockIndentedWith,
                     const Element& aEditingHost);

  /**
   * HandleOutdentAtSelectionInternal() outdents contents around Selection.
   * This method creates AutoSelectionRestorer.  Therefore, each caller
   * needs to check if the editor is still available even if this returns
   * NS_OK.
   * NOTE: Call `HandleOutdentAtSelection()` instead.
   *
   * @param aEditingHost        The editing host.
   * @return                    The left content is left content of last
   *                            outdented element.
   *                            The right content is right content of last
   *                            outdented element.
   *                            The middle content is middle content of last
   *                            outdented element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<SplitRangeOffFromNodeResult, nsresult>
  HandleOutdentAtSelectionInternal(const Element& aEditingHost);

  /**
   * HandleOutdentAtSelection() outdents contents around Selection.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  HandleOutdentAtSelection(const Element& aEditingHost);

  /**
   * AlignBlockContentsWithDivElement() sets align attribute of <div> element
   * which is only child of aBlockElement to aAlignType.  If aBlockElement
   * has 2 or more children or does not have a `<div>` element, inserts a
   * new `<div>` element into aBlockElement and move all children of
   * aBlockElement into the new `<div>` element.
   *
   * @param aBlockElement       The element node whose contents should be
   *                            aligned to aAlignType.  This should be
   *                            an element which can have `<div>` element
   *                            as its child.
   * @param aAlignType          New value of align attribute of `<div>`
   *                            element.
   * @return                    A candidate position to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  AlignBlockContentsWithDivElement(Element& aBlockElement,
                                   const nsAString& aAlignType);

  /**
   * AlignContentsInAllTableCellsAndListItems() calls
   * AlignBlockContentsWithDivElement() for aligning contents in every list
   * item element and table cell element in aElement.
   *
   * @param aElement            The node which is or whose descendants should
   *                            be aligned to aAlignType.
   * @param aAlignType          New value of `align` attribute of `<div>`.
   * @return                    A candidate position to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  AlignContentsInAllTableCellsAndListItems(dom::Element& aElement,
                                           const nsAString& aAlignType);

  /**
   * MakeTransitionList() detects all the transitions in the array, where a
   * transition means that adjacent nodes in the array don't have the same
   * parent.
   */
  static void MakeTransitionList(
      const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      nsTArray<bool>& aTransitionArray);

  /**
   * EnsureHardLineBeginsWithFirstChildOf() inserts `<br>` element before
   * first child of aRemovingContainerElement if it will not be start of a
   * hard line after removing aRemovingContainerElement.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  EnsureHardLineBeginsWithFirstChildOf(Element& aRemovingContainerElement);

  /**
   * EnsureHardLineEndsWithLastChildOf() inserts `<br>` element after last
   * child of aRemovingContainerElement if it will not be end of a hard line
   * after removing aRemovingContainerElement.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  EnsureHardLineEndsWithLastChildOf(Element& aRemovingContainerElement);

  /**
   * RemoveAlignFromDescendants() removes align attributes, text-align
   * properties and <center> elements in aElement.
   *
   * @param aElement    Alignment information of the node and/or its
   *                    descendants will be removed.
   *                    NOTE: aElement must not be a `<table>` element.
   * @param aAlignType  New align value to be set only when it's in
   *                    CSS mode and this method meets <table> or <hr>.
   *                    XXX This is odd and not clear when you see caller of
   *                    this method.  Do you have better idea?
   * @param aEditTarget If `OnlyDescendantsExceptTable`, modifies only
   *                    descendants of aElement.
   *                    If `NodeAndDescendantsExceptTable`, modifies `aElement`
   *                    and its descendants.
   * @return            A candidate point to put caret.
   */
  enum class EditTarget {
    OnlyDescendantsExceptTable,
    NodeAndDescendantsExceptTable
  };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  RemoveAlignFromDescendants(Element& aElement, const nsAString& aAlignType,
                             EditTarget aEditTarget);

  /**
   * SetBlockElementAlign() resets `align` attribute, `text-align` property
   * of descendants of aBlockOrHRElement except `<table>` element descendants.
   * Then, set `align` attribute or `text-align` property of aBlockOrHRElement.
   *
   * @param aBlockOrHRElement   The element whose contents will be aligned.
   *                            This must be a block element or `<hr>` element.
   *                            If we're not in CSS mode, this element has
   *                            to support `align` attribute (i.e.,
   *                            `HTMLEditUtils::SupportsAlignAttr()` must
   *                            return true).
   * @param aAlignType          Boundary or "center" which contents should be
   *                            aligned on.
   * @param aEditTarget         If `OnlyDescendantsExceptTable`, modifies only
   *                            descendants of aBlockOrHRElement.
   *                            If `NodeAndDescendantsExceptTable`, modifies
   *                            aBlockOrHRElement and its descendants.
   * @return                    A candidate point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SetBlockElementAlign(Element& aBlockOrHRElement, const nsAString& aAlignType,
                       EditTarget aEditTarget);

  /**
   * InsertDivElementToAlignContents() inserts a new <div> element (which has
   * only a padding <br> element) to aPointToInsert for a placeholder whose
   * contents will be aligned.
   *
   * @param aPointToInsert      A point to insert new empty <div>.
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   * @param aEditingHost        The editing host.
   * @return                    New <div> element which has only a padding <br>
   *                            element and is styled to align contents.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertDivElementToAlignContents(const EditorDOMPoint& aPointToInsert,
                                  const nsAString& aAlignType,
                                  const Element& aEditingHost);

  /**
   * AlignNodesAndDescendants() make contents of nodes in aArrayOfContents and
   * their descendants aligned to aAlignType.
   *
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   * @param aEditingHost        The editing host.
   * @return                    Last created <div> element which should contain
   *                            caret and candidate position which may be
   *                            outside the <div> element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  AlignNodesAndDescendants(
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      const nsAString& aAlignType, const Element& aEditingHost);

  /**
   * AlignContentsAtRanges() aligns contents around aRanges to aAlignType.
   *
   * @param aRanges             The ranges where should be aligned.
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult AlignContentsAtRanges(
      AutoClonedSelectionRangeArray& aRanges, const nsAString& aAlignType,
      const Element& aEditingHost);

  /**
   * AlignAsSubAction() handles "align" command with `Selection`.
   *
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  AlignAsSubAction(const nsAString& aAlignType, const Element& aEditingHost);

  /**
   * AdjustCaretPositionAndEnsurePaddingBRElement() may adjust caret
   * position to nearest editable content and if padding `<br>` element is
   * necessary at caret position, this creates it.
   *
   * @param aDirectionAndAmount Direction of the edit action.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  AdjustCaretPositionAndEnsurePaddingBRElement(
      nsIEditor::EDirection aDirectionAndAmount);

  /**
   * EnsureSelectionInBodyOrDocumentElement() collapse `Selection` to the
   * primary `<body>` element or document element when `Selection` crosses
   * `<body>` element's boundary.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  EnsureSelectionInBodyOrDocumentElement();

  /**
   * InsertBRElementToEmptyListItemsAndTableCellsInRange() inserts
   * `<br>` element into empty list item or table cell elements between
   * aStartRef and aEndRef.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  InsertBRElementToEmptyListItemsAndTableCellsInRange(
      const RawRangeBoundary& aStartRef, const RawRangeBoundary& aEndRef);

  /**
   * RemoveEmptyNodesIn() removes all empty nodes in aRange.  However, if
   * mail-cite node has only a `<br>` element, the node will be removed
   * but <br> element is moved to where the mail-cite node was.
   * XXX This method is expensive if aRange is too wide and may remove
   *     unexpected empty element, e.g., it was created by JS, but we haven't
   *     touched it.  Cannot we remove this method and make guarantee that
   *     empty nodes won't be created?
   *
   * @param aRange      Must be positioned.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  RemoveEmptyNodesIn(const EditorDOMRange& aRange);

  /**
   * SetSelectionInterlinePosition() may set interline position if caret is
   * positioned around `<br>` or block boundary.  Don't call this when
   * `Selection` is not collapsed.
   */
  void SetSelectionInterlinePosition();

  /**
   * Called by `HTMLEditor::OnEndHandlingTopLevelEditSubAction()`.  This may
   * adjust Selection, remove unnecessary empty nodes, create `<br>` elements
   * if needed, etc.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  OnEndHandlingTopLevelEditSubActionInternal();

  /**
   * MoveSelectedContentsToDivElementToMakeItAbsolutePosition() looks for
   * a `<div>` element in selection first.  If not, creates new `<div>`
   * element.  Then, move all selected contents into the target `<div>`
   * element.
   * Note that this creates AutoSelectionRestorer.  Therefore, callers need
   * to check whether we have been destroyed even when this returns NS_OK.
   *
   * @param aTargetElement      Returns target `<div>` element which should be
   *                            changed to absolute positioned.
   * @param aEditingHost        The editing host.
   */
  // TODO: Rewrite this with `Result<RefPtr<Element>, nsresult>`.
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MoveSelectedContentsToDivElementToMakeItAbsolutePosition(
      RefPtr<Element>* aTargetElement, const Element& aEditingHost);

  /**
   * SetSelectionToAbsoluteAsSubAction() move selected contents to first
   * selected `<div>` element or newly created `<div>` element and make
   * the `<div>` element positioned absolutely.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  SetSelectionToAbsoluteAsSubAction(const Element& aEditingHost);

  /**
   * SetSelectionToStaticAsSubAction() sets the `position` property of a
   * selection parent's block whose `position` is `absolute` to `static`.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  SetSelectionToStaticAsSubAction();

  /**
   * AddZIndexAsSubAction() adds aChange to `z-index` of nearest parent
   * absolute-positioned element from current selection.
   *
   * @param aChange     Amount to change `z-index`.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  AddZIndexAsSubAction(int32_t aChange);

  /**
   * RunOrScheduleOnModifyDocument() is called when editor content is changed.
   */
  MOZ_CAN_RUN_SCRIPT nsresult RunOrScheduleOnModifyDocument(
      const nsIContent* aContentWillBeRemoved = nullptr);

 protected:  // Called by helper classes.
  MOZ_CAN_RUN_SCRIPT void OnStartToHandleTopLevelEditSubAction(
      EditSubAction aTopLevelEditSubAction,
      nsIEditor::EDirection aDirectionOfTopLevelEditSubAction,
      ErrorResult& aRv) final;
  MOZ_CAN_RUN_SCRIPT nsresult OnEndHandlingTopLevelEditSubAction() final;

 protected:  // Shouldn't be used by friend classes
  virtual ~HTMLEditor();

  enum class DOMMutationType {
    ContentAppended,
    ContentInserted,
    ContentWillBeRemoved,
    CharacterDataChanged,
  };
  [[nodiscard]] LogLevel MutationLogLevelOf(
      nsIContent* aContent,
      const CharacterDataChangeInfo* aCharacterDataChangeInfo,
      DOMMutationType aDOMMutationType) const;
  [[nodiscard]] LogLevel AttrMutationLogLevelOf(
      Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
      AttrModType aModType, const nsAttrValue* aOldValue) const;

  void MaybeLogContentAppended(nsIContent*) const;
  void MaybeLogContentInserted(nsIContent*) const;
  void MaybeLogContentWillBeRemoved(nsIContent*) const;
  void MaybeLogCharacterDataChanged(nsIContent*,
                                    const CharacterDataChangeInfo&) const;
  void MaybeLogAttributeChanged(Element*, int32_t, nsAtom*, AttrModType,
                                const nsAttrValue*) const;

  /**
   * InitEditorContentAndSelection() may insert `<br>` elements and padding
   * `<br>` elements if they are required for `<body>` or document element.
   * And collapse selection at the end if there is no selection ranges.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InitEditorContentAndSelection();

  /**
   * Collapse `Selection` to the last leaf content of the <body> or the document
   * element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  CollapseSelectionToEndOfLastLeafNodeOfDocument() const;

  MOZ_CAN_RUN_SCRIPT nsresult SelectAllInternal() final;

  [[nodiscard]] Element* ComputeEditingHostInternal(
      const nsIContent* aContent, LimitInBodyElement aLimitInBodyElement) const;

  /**
   * Creates a range with just the supplied node and appends that to the
   * selection.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  AppendContentToSelectionAsRange(nsIContent& aContent);

  /**
   * When you are using AppendContentToSelectionAsRange(), call this first to
   * start a new selection.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult ClearSelection();

  /**
   * SelectContentInternal() sets Selection to aContentToSelect to
   * aContentToSelect + 1 in parent of aContentToSelect.
   *
   * @param aContentToSelect    The content which should be selected.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  SelectContentInternal(nsIContent& aContentToSelect);

  /**
   * GetInclusiveAncestorByTagNameAtSelection() looks for an element node whose
   * name matches aTagName from anchor node of Selection to <body> element.
   *
   * @param aTagName        The tag name which you want to look for.
   *                        Must not be nsGkAtoms::_empty.
   *                        If nsGkAtoms::list, the result may be <ul>, <ol> or
   *                        <dl> element.
   *                        If nsGkAtoms::td, the result may be <td> or <th>.
   *                        If nsGkAtoms::href, the result may be <a> element
   *                        which has "href" attribute with non-empty value.
   *                        If nsGkAtoms::anchor, the result may be <a> which
   *                        has "name" attribute with non-empty value.
   * @return                If an element which matches aTagName, returns
   *                        an Element.  Otherwise, nullptr.
   */
  Element* GetInclusiveAncestorByTagNameAtSelection(
      const nsStaticAtom& aTagName) const;

  /**
   * GetInclusiveAncestorByTagNameInternal() looks for an element node whose
   * name matches aTagName from aNode to <body> element.
   *
   * @param aTagName        The tag name which you want to look for.
   *                        Must not be nsGkAtoms::_empty.
   *                        If nsGkAtoms::list, the result may be <ul>, <ol> or
   *                        <dl> element.
   *                        If nsGkAtoms::td, the result may be <td> or <th>.
   *                        If nsGkAtoms::href, the result may be <a> element
   *                        which has "href" attribute with non-empty value.
   *                        If nsGkAtoms::anchor, the result may be <a> which
   *                        has "name" attribute with non-empty value.
   * @param aContent        Start node to look for the element.  This should
   *                        not be an orphan node.
   * @return                If an element which matches aTagName, returns
   *                        an Element.  Otherwise, nullptr.
   */
  Element* GetInclusiveAncestorByTagNameInternal(
      const nsStaticAtom& aTagName, const nsIContent& aContent) const;

  /**
   * GetSelectedElement() returns a "selected" element node.  "selected" means:
   * - there is only one selection range
   * - the range starts from an element node or in an element
   * - the range ends at immediately after same element
   * - and the range does not include any other element nodes.
   * Additionally, only when aTagName is nsGkAtoms::href, this thinks that an
   * <a> element which has non-empty "href" attribute includes the range, the
   * <a> element is selected.
   *
   * NOTE: This method is implementation of nsIHTMLEditor.getSelectedElement()
   * and comm-central depends on this behavior.  Therefore, if you need to use
   * this method internally but you need to change, perhaps, you should create
   * another method for avoiding breakage of comm-central apps.
   *
   * @param aTagName    The atom of tag name in lower case.  Set this to
   *                    result  of EditorUtils::GetTagNameAtom() if you have a
   *                    tag name with nsString.
   *                    If nullptr, this returns any element node or nullptr.
   *                    If nsGkAtoms::href, this returns an <a> element which
   *                    has non-empty "href" attribute or nullptr.
   *                    If nsGkAtoms::anchor, this returns an <a> element which
   *                    has non-empty "name" attribute or nullptr.
   *                    Otherwise, returns an element node whose name is
   *                    same as aTagName or nullptr.
   * @param aRv         Returns error code.
   * @return            A "selected" element.
   */
  already_AddRefed<Element> GetSelectedElement(const nsAtom* aTagName,
                                               ErrorResult& aRv);

  /**
   * GetFirstTableRowElement() returns the first <tr> element in the most
   * nearest ancestor of aTableOrElementInTable or itself.
   * When aTableOrElementInTable is neither <table> nor in a <table> element,
   * returns NS_ERROR_FAILURE. However, if <table> does not have <tr> element,
   * returns nullptr.
   *
   * @param aTableOrElementInTable      <table> element or another element.
   *                                    If this is a <table> element, returns
   *                                    first <tr> element in it.  Otherwise,
   *                                    returns first <tr> element in nearest
   *                                    ancestor <table> element.
   */
  Result<RefPtr<Element>, nsresult> GetFirstTableRowElement(
      const Element& aTableOrElementInTable) const;

  /**
   * GetNextTableRowElement() returns next <tr> element of aTableRowElement.
   * This won't cross <table> element boundary but may cross table section
   * elements like <tbody>.
   * Note that if given element is <tr> but there is no next <tr> element, this
   * returns nullptr but does not return error.
   *
   * @param aTableRowElement    A <tr> element.
   */
  Result<RefPtr<Element>, nsresult> GetNextTableRowElement(
      const Element& aTableRowElement) const;

  struct CellData;

  /**
   * CellIndexes store both row index and column index of a table cell.
   */
  struct MOZ_STACK_CLASS CellIndexes final {
    int32_t mRow;
    int32_t mColumn;

    /**
     * This constructor initializes mRowIndex and mColumnIndex with indexes of
     * aCellElement.
     *
     * @param aCellElement      An <td> or <th> element.
     */
    MOZ_CAN_RUN_SCRIPT CellIndexes(Element& aCellElement, PresShell* aPresShell)
        : mRow(-1), mColumn(-1) {
      Update(aCellElement, aPresShell);
    }

    /**
     * Update mRowIndex and mColumnIndex with indexes of aCellElement.
     *
     * @param                   See above.
     */
    MOZ_CAN_RUN_SCRIPT void Update(Element& aCellElement,
                                   PresShell* aPresShell);

    /**
     * This constructor initializes mRowIndex and mColumnIndex with indexes of
     * cell element which contains anchor of Selection.
     *
     * @param aHTMLEditor       The editor which creates the instance.
     * @param aSelection        The Selection for the editor.
     */
    MOZ_CAN_RUN_SCRIPT CellIndexes(HTMLEditor& aHTMLEditor,
                                   Selection& aSelection)
        : mRow(-1), mColumn(-1) {
      Update(aHTMLEditor, aSelection);
    }

    /**
     * Update mRowIndex and mColumnIndex with indexes of cell element which
     * contains anchor of Selection.
     *
     * @param                   See above.
     */
    MOZ_CAN_RUN_SCRIPT void Update(HTMLEditor& aHTMLEditor,
                                   Selection& aSelection);

    bool operator==(const CellIndexes& aOther) const {
      return mRow == aOther.mRow && mColumn == aOther.mColumn;
    }
    bool operator!=(const CellIndexes& aOther) const {
      return mRow != aOther.mRow || mColumn != aOther.mColumn;
    }

    [[nodiscard]] bool isErr() const { return mRow < 0 || mColumn < 0; }

   private:
    CellIndexes() : mRow(-1), mColumn(-1) {}
    CellIndexes(int32_t aRowIndex, int32_t aColumnIndex)
        : mRow(aRowIndex), mColumn(aColumnIndex) {}

    friend struct CellData;
  };

  struct MOZ_STACK_CLASS CellData final {
    MOZ_KNOWN_LIVE RefPtr<Element> mElement;
    // Current indexes which this is initialized with.
    CellIndexes mCurrent;
    // First column/row indexes of the cell.  When current position is spanned
    // from other column/row, this value becomes different from mCurrent.
    CellIndexes mFirst;
    // Computed rowspan/colspan values which are specified to the cell.
    // Note that if the cell has larger rowspan/colspan value than actual
    // table size, these values are the larger values.
    int32_t mRowSpan = -1;
    int32_t mColSpan = -1;
    // Effective rowspan/colspan value at the index.  For example, if first
    // cell element in first row has rowspan="3", then, if this is initialized
    // with 0-0 indexes, effective rowspan is 3.  However, if this is
    // initialized with 1-0 indexes, effective rowspan is 2.
    int32_t mEffectiveRowSpan = -1;
    int32_t mEffectiveColSpan = -1;
    // mIsSelected is set to true if mElement itself or its parent <tr> or
    // <table> is selected.  Otherwise, e.g., the cell just contains selection
    // range, this is set to false.
    bool mIsSelected = false;

    CellData() = delete;

    /**
     * This returns an instance which is initialized with a <table> element and
     * both row and column index to specify a cell element.
     */
    [[nodiscard]] static CellData AtIndexInTableElement(
        const HTMLEditor& aHTMLEditor, const Element& aTableElement,
        int32_t aRowIndex, int32_t aColumnIndex);
    [[nodiscard]] static CellData AtIndexInTableElement(
        const HTMLEditor& aHTMLEditor, const Element& aTableElement,
        const CellIndexes& aIndexes) {
      MOZ_ASSERT(!aIndexes.isErr());
      return AtIndexInTableElement(aHTMLEditor, aTableElement, aIndexes.mRow,
                                   aIndexes.mColumn);
    }

    /**
     * Treated as error if fails to compute current index or first index of the
     * cell.  Note that even if the cell is not found due to no corresponding
     * frame at current index, it's not an error situation.
     */
    [[nodiscard]] bool isOk() const { return !isErr(); }
    [[nodiscard]] bool isErr() const { return mFirst.isErr(); }

    /**
     * FailedOrNotFound() returns true if this failed to initialize/update
     * or succeeded but found no cell element.
     */
    [[nodiscard]] bool FailedOrNotFound() const { return isErr() || !mElement; }

    /**
     * IsSpannedFromOtherRowOrColumn(), IsSpannedFromOtherColumn and
     * IsSpannedFromOtherRow() return true if there is no cell element
     * at the index because of spanning from other row and/or column.
     */
    [[nodiscard]] bool IsSpannedFromOtherRowOrColumn() const {
      return mElement && mCurrent != mFirst;
    }
    [[nodiscard]] bool IsSpannedFromOtherColumn() const {
      return mElement && mCurrent.mColumn != mFirst.mColumn;
    }
    [[nodiscard]] bool IsSpannedFromOtherRow() const {
      return mElement && mCurrent.mRow != mFirst.mRow;
    }
    [[nodiscard]] bool IsNextColumnSpannedFromOtherColumn() const {
      return mElement && mCurrent.mColumn + 1 < NextColumnIndex();
    }

    /**
     * NextColumnIndex() and NextRowIndex() return column/row index of
     * next cell.  Note that this does not check whether there is next
     * cell or not actually.
     */
    [[nodiscard]] int32_t NextColumnIndex() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mCurrent.mColumn + mEffectiveColSpan;
    }
    [[nodiscard]] int32_t NextRowIndex() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mCurrent.mRow + mEffectiveRowSpan;
    }

    /**
     * LastColumnIndex() and LastRowIndex() return column/row index of
     * column/row which is spanned by the cell.
     */
    [[nodiscard]] int32_t LastColumnIndex() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return NextColumnIndex() - 1;
    }
    [[nodiscard]] int32_t LastRowIndex() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return NextRowIndex() - 1;
    }

    /**
     * NumberOfPrecedingColmuns() and NumberOfPrecedingRows() return number of
     * preceding columns/rows if current index is spanned from other column/row.
     * Otherwise, i.e., current point is not spanned form other column/row,
     * returns 0.
     */
    [[nodiscard]] int32_t NumberOfPrecedingColmuns() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mCurrent.mColumn - mFirst.mColumn;
    }
    [[nodiscard]] int32_t NumberOfPrecedingRows() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mCurrent.mRow - mFirst.mRow;
    }

    /**
     * NumberOfFollowingColumns() and NumberOfFollowingRows() return
     * number of remaining columns/rows if the cell spans to other
     * column/row.
     */
    [[nodiscard]] int32_t NumberOfFollowingColumns() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mEffectiveColSpan - 1;
    }
    [[nodiscard]] int32_t NumberOfFollowingRows() const {
      if (NS_WARN_IF(FailedOrNotFound())) {
        return -1;
      }
      return mEffectiveRowSpan - 1;
    }

   private:
    explicit CellData(int32_t aCurrentRowIndex, int32_t aCurrentColumnIndex,
                      int32_t aFirstRowIndex, int32_t aFirstColumnIndex)
        : mCurrent(aCurrentRowIndex, aCurrentColumnIndex),
          mFirst(aFirstRowIndex, aFirstColumnIndex) {}
    explicit CellData(Element& aElement, int32_t aRowIndex,
                      int32_t aColumnIndex, nsTableCellFrame& aTableCellFrame,
                      nsTableWrapperFrame& aTableWrapperFrame);

    [[nodiscard]] static CellData Error(int32_t aRowIndex,
                                        int32_t aColumnIndex) {
      return CellData(aRowIndex, aColumnIndex, -1, -1);
    }
    [[nodiscard]] static CellData NotFound(int32_t aRowIndex,
                                           int32_t aColumnIndex) {
      return CellData(aRowIndex, aColumnIndex, aRowIndex, aColumnIndex);
    }
  };

  /**
   * TableSize stores and computes number of rows and columns of a <table>
   * element.
   */
  struct MOZ_STACK_CLASS TableSize final {
    int32_t mRowCount;
    int32_t mColumnCount;

    TableSize() = delete;

    /**
     * @param aHTMLEditor               The editor which creates the instance.
     * @param aTableOrElementInTable    If a <table> element, computes number
     *                                  of rows and columns of it.
     *                                  If another element in a <table> element,
     *                                  computes number of rows and columns
     *                                  of nearest ancestor <table> element.
     *                                  Otherwise, i.e., non-<table> element
     *                                  not in <table>, returns error.
     */
    [[nodiscard]] static Result<TableSize, nsresult> Create(
        HTMLEditor& aHTMLEditor, Element& aTableOrElementInTable);

    [[nodiscard]] bool IsEmpty() const { return !mRowCount || !mColumnCount; }

   private:
    TableSize(int32_t aRowCount, int32_t aColumCount)
        : mRowCount(aRowCount), mColumnCount(aColumCount) {}
  };

  /**
   * GetTableCellElementAt() returns a <td> or <th> element of aTableElement
   * if there is a cell at the indexes.
   *
   * @param aTableElement       Must be a <table> element.
   * @param aCellIndexes        Indexes of cell which you want.
   *                            If rowspan and/or colspan is specified 2 or
   *                            larger, any indexes are allowed to retrieve
   *                            the cell in the area.
   * @return                    The cell element if there is in the <table>.
   *                            Returns nullptr without error if the indexes
   *                            are out of bounds.
   */
  [[nodiscard]] inline Element* GetTableCellElementAt(
      Element& aTableElement, const CellIndexes& aCellIndexes) const;
  [[nodiscard]] Element* GetTableCellElementAt(Element& aTableElement,
                                               int32_t aRowIndex,
                                               int32_t aColumnIndex) const;

  /**
   * GetSelectedOrParentTableElement() returns <td>, <th>, <tr> or <table>
   * element:
   *   #1 if the first selection range selects a cell, returns it.
   *   #2 if the first selection range does not select a cell and
   *      the selection anchor refers a <table>, returns it.
   *   #3 if the first selection range does not select a cell and
   *      the selection anchor refers a <tr>, returns it.
   *   #4 if the first selection range does not select a cell and
   *      the selection anchor refers a <td>, returns it.
   *   #5 otherwise, nearest ancestor <td> or <th> element of the
   *      selection anchor if there is.
   * In #1 and #4, *aIsCellSelected will be set to true (i.e,, when
   * a selection range selects a cell element).
   */
  Result<RefPtr<Element>, nsresult> GetSelectedOrParentTableElement(
      bool* aIsCellSelected = nullptr) const;

  /**
   * GetFirstSelectedCellElementInTable() returns <td> or <th> element at
   * first selection (using GetSelectedOrParentTableElement).  If found cell
   * element is not in <table> or <tr> element, this returns nullptr.
   */
  Result<RefPtr<Element>, nsresult> GetFirstSelectedCellElementInTable() const;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  HandlePaste(AutoEditActionDataSetter& aEditActionData,
              nsIClipboard::ClipboardType aClipboardType,
              DataTransfer* aDataTransfer) final;
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  HandlePasteAsQuotation(AutoEditActionDataSetter& aEditActionData,
                         nsIClipboard::ClipboardType aClipboardType,
                         DataTransfer* aDataTransfer) final;
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  HandlePasteTransferable(AutoEditActionDataSetter& aEditActionData,
                          nsITransferable& aTransferable) final;

  /**
   * PasteInternal() pasts text with replacing selected content.
   * This tries to dispatch ePaste event first.  If its defaultPrevent() is
   * called, this does nothing but returns NS_OK.
   *
   * @param aClipboardType      nsIClipboard::kGlobalClipboard or
   *                            nsIClipboard::kSelectionClipboard.
   * @param aEditingHost        The editing host.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  PasteInternal(nsIClipboard::ClipboardType aClipboardType,
                DataTransfer* aDataTransfer, const Element& aEditingHost);

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  InsertWithQuotationsAsSubAction(const nsAString& aQuotedText) final;

  /**
   * InsertAsCitedQuotationInternal() inserts a <blockquote> element whose
   * cite attribute is aCitation and whose content is aQuotedText.
   * Note that this shouldn't be called when IsPlaintextMailComposer() is true.
   *
   * @param aQuotedText     HTML source if aInsertHTML is true.  Otherwise,
   *                        plain text.  This is inserted into new <blockquote>
   *                        element.
   * @param aCitation       cite attribute value of new <blockquote> element.
   * @param aInsertHTML     true if aQuotedText should be treated as HTML
   *                        source.
   *                        false if aQuotedText should be treated as plain
   *                        text.
   * @param aEditingHost    The editing host.
   * @param aNodeInserted   [OUT] The new <blockquote> element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertAsCitedQuotationInternal(
      const nsAString& aQuotedText, const nsAString& aCitation,
      bool aInsertHTML, const Element& aEditingHost, nsINode** aNodeInserted);

  /**
   * InsertNodeIntoProperAncestorWithTransaction() attempts to insert aNode
   * into the document, at aPointToInsert.  Checks with strict dtd to see if
   * containment is allowed.  If not allowed, will attempt to find a parent
   * in the parent hierarchy of aPointToInsert.GetContainer() that will accept
   * aNode as a child.  If such a parent is found, will split the document
   * tree from aPointToInsert up to parent, and then insert aNode.
   * aPointToInsert is then adjusted to point to the actual location that
   * aNode was inserted at.  aSplitAtEdges specifies if the splitting process
   * is allowed to result in empty nodes.
   *
   * @param aContent          The content node to insert.
   * @param aPointToInsert    Insertion point.
   * @param aSplitAtEdges     Splitting can result in empty nodes?
   */
  template <typename NodeType>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT
      Result<CreateNodeResultBase<NodeType>, nsresult>
      InsertNodeIntoProperAncestorWithTransaction(
          NodeType& aContent, const EditorDOMPoint& aPointToInsert,
          SplitAtEdges aSplitAtEdges);

  /**
   * InsertTextWithQuotationsInternal() replaces selection with new content.
   * First, this method splits aStringToInsert to multiple chunks which start
   * with non-linebreaker except first chunk and end with a linebreaker except
   * last chunk.  Then, each chunk starting with ">" is inserted after wrapping
   * with <span _moz_quote="true">, and each chunk not starting with ">" is
   * inserted as normal text.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertTextWithQuotationsInternal(
      const nsAString& aStringToInsert, const Element& aEditingHost);

  /**
   * ReplaceContainerWithTransactionInternal() is implementation of
   * ReplaceContainerWithTransaction() and
   * ReplaceContainerAndCloneAttributesWithTransaction().
   *
   * @param aOldContainer       The element which will be replaced with new
   *                            element.
   * @param aTagName            The name of new element node.
   * @param aAttribute          Attribute name which will be set to the new
   *                            element.  This will be ignored if
   *                            aCloneAllAttributes is set to true.
   * @param aAttributeValue     Attribute value which will be set to
   *                            aAttribute.
   * @param aCloneAllAttributes If true, all attributes of aOldContainer will
   *                            be copied to the new element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  ReplaceContainerWithTransactionInternal(Element& aOldContainer,
                                          const nsAtom& aTagName,
                                          const nsAtom& aAttribute,
                                          const nsAString& aAttributeValue,
                                          bool aCloneAllAttributes);

  /**
   * DeleteSelectionAndCreateElement() creates a element whose name is aTag.
   * And insert it into the DOM tree after removing the selected content.
   *
   * @param aTag                The element name to be created.
   * @param aInitializer        A function to initialize the new element before
   *                            or after (depends on the pref) connecting the
   *                            element into the DOM tree. Note that this should
   *                            not touch outside given element because doing it
   *                            would break range updater's result.
   */
  MOZ_CAN_RUN_SCRIPT Result<RefPtr<Element>, nsresult>
  DeleteSelectionAndCreateElement(
      nsAtom& aTag,
      const InitializeInsertingElement& aInitializer = DoNothingForNewElement);

  /**
   * This method first deletes the selection, if it's not collapsed.  Then if
   * the selection lies in a CharacterData node, it splits it.  If the
   * selection is at this point collapsed in a CharacterData node, it's
   * adjusted to be collapsed right before or after the node instead (which is
   * always possible, since the node was split).
   */
  MOZ_CAN_RUN_SCRIPT nsresult DeleteSelectionAndPrepareToCreateNode();

  /**
   * PrepareToInsertLineBreak() returns a point where a new line break node
   * should be inserted.  If aPointToInsert points middle of a text node, this
   * method splits the text node and returns the point before right node.
   *
   * @param aLineBreakType      Whether you will insert <br> or a preformatted
   *                            linefeed.
   * @param aPointToInsert      Candidate point to insert new line break node.
   * @return                    Computed point to insert new line break node.
   *                            If something failed, this return error.
   */
  MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult> PrepareToInsertLineBreak(
      LineBreakType aLineBreakType, const EditorDOMPoint& aPointToInsert);

  enum class PreservePreformattedLineBreak : bool { No, Yes };

  /**
   * If unnecessary line break is there immediately after aPoint, this deletes
   * the line break.  Note that unnecessary line break means that the line break
   * is a padding line break for empty line immediately before a block boundary
   * and it's not a placeholder of ancestor inline elements.
   *
   * @param aNextOrAfterModifiedPoint
   *                            If you inserted something, this should be next
   *                            point or after the inserted content. If you
   *                            deleted something, this should be end of the
   *                            deleted range.
   * @param aPreservePreformattedLineBreak
   *                            Whether this method should preserve preformatted
   *                            linefeed or not. If "Yes", this will delete only
   *                            when the unnecessary line break is a <br>.
   * @param aPaddingForEmptyBlock
   *                            Treat the line break for empty block is
   *                            unnecessary or not. If this is set to
   *                            "Unnecessary", preformatted line break will be
   *                            deleted if it's a padding for empty block and
   *                            aPreservePreformattedLineBreak is "Yes".
   *                            This should be "Significant" if you use this
   *                            after handling the edit action. So, you can use
   *                            "Unnecessary" only when you are calling this
   *                            before handling the edit action.
   * @param aEditingHost        The editing host containing
   *                            aNextOrAfterModifiedPoint.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  EnsureNoFollowingUnnecessaryLineBreak(
      const EditorDOMPoint& aNextOrAfterModifiedPoint,
      PreservePreformattedLineBreak aPreservePreformattedLineBreak,
      PaddingForEmptyBlock aPaddingForEmptyBlock, const Element& aEditingHost);

  /**
   * IndentAsSubAction() indents the content around Selection.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  IndentAsSubAction(const Element& aEditingHost);

  /**
   * OutdentAsSubAction() outdents the content around Selection.
   *
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  OutdentAsSubAction(const Element& aEditingHost);

  MOZ_CAN_RUN_SCRIPT nsresult LoadHTML(const nsAString& aInputString);

  /**
   * UpdateMetaCharsetWithTransaction() scans all <meta> elements in the
   * document and if and only if there is a <meta> element having `httpEquiv`
   * attribute and whose value includes `content-type`, updates its `content`
   * attribute value to aCharacterSet.
   */
  MOZ_CAN_RUN_SCRIPT bool UpdateMetaCharsetWithTransaction(
      Document& aDocument, const nsACString& aCharacterSet);

  /**
   * SetInlinePropertiesAsSubAction() stores new styles with
   * mPendingStylesToApplyToNewContent if `Selection` is collapsed.  Otherwise,
   * applying the styles to all selected contents.
   *
   * @param aStylesToSet        The styles which should be applied to the
   *                            selected content.
   * @param aEditingHost        The editing host.
   */
  template <size_t N>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SetInlinePropertiesAsSubAction(
      const AutoTArray<EditorInlineStyleAndValue, N>& aStylesToSet,
      const Element& aEditingHost);

  /**
   * SetInlinePropertiesAroundRanges() applying the styles to the ranges even if
   * the ranges are collapsed.
   */
  template <size_t N>
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SetInlinePropertiesAroundRanges(
      AutoClonedRangeArray& aRanges,
      const AutoTArray<EditorInlineStyleAndValue, N>& aStylesToSet,
      const Element& aEditingHost);

  /**
   * RemoveInlinePropertiesAsSubAction() removes specified styles from
   * mPendingStylesToApplyToNewContent if `Selection` is collapsed.  Otherwise,
   * removing the style.
   *
   * @param aStylesToRemove     Styles to remove from the selected contents.
   * @param aEditingHost        The editing host.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult RemoveInlinePropertiesAsSubAction(
      const nsTArray<EditorInlineStyle>& aStylesToRemove,
      const Element& aEditingHost);

  /**
   * Helper method to call RemoveInlinePropertiesAsSubAction().  If you want to
   * remove other elements to remove the style completely, this will append
   * related elements of aStyleToRemove and aStyleToRemove itself to the array.
   * E.g., nsGkAtoms::strong and nsGkAtoms::b will be appended if aStyleToRemove
   * is nsGkAtoms::b.
   */
  void AppendInlineStyleAndRelatedStyle(
      const EditorInlineStyle& aStyleToRemove,
      nsTArray<EditorInlineStyle>& aStylesToRemove) const;

  enum class RetrievingBackgroundColorOption {
    // Ignore inline styles, i.e., return only background color of ancestor
    // blocks.
    OnlyBlockBackgroundColor,
    // Stop at nearest inclusive ancestor block.
    StopAtInclusiveAncestorBlock,
    // Return default background color for the document if there is no ancestor
    // elements which have background color.
    DefaultColorIfNoSpecificBackgroundColor,
  };
  using RetrievingBackgroundColorOptions =
      EnumSet<RetrievingBackgroundColorOption>;
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  GetCSSBackgroundColorState(bool* aMixed, nsAString& aOutColor,
                             RetrievingBackgroundColorOptions aOptions);

  nsresult GetHTMLBackgroundColorState(bool* aMixed, nsAString& outColor);

  /**
   * This sets background on the appropriate container element (table, cell,)
   * or calls to set the page background.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetBlockBackgroundColorWithCSSAsSubAction(const nsAString& aColor);
  MOZ_CAN_RUN_SCRIPT nsresult
  SetHTMLBackgroundColorWithTransaction(const nsAString& aColor);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void InitializeSelectionAncestorLimit(
      Element& aAncestorLimit) const final;

  /**
   * Make the given selection span the entire document.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SelectEntireDocument() final;

  /**
   * Use this to assure that selection is set after attribute nodes when
   * trying to collapse selection at begining of a block node
   * e.g., when setting at beginning of a table cell
   * This will stop at a table, however, since we don't want to
   * "drill down" into nested tables.
   */
  MOZ_CAN_RUN_SCRIPT void CollapseSelectionToDeepestNonTableFirstChild(
      nsINode* aNode);
  /**
   * MaybeCollapseSelectionAtFirstEditableNode() may collapse selection at
   * proper position to staring to edit.  If there is a non-editable node
   * before any editable text nodes or inline elements which can have text
   * nodes as their children, collapse selection at start of the editing
   * host.  If there is an editable text node which is not collapsed, collapses
   * selection at the start of the text node.  If there is an editable inline
   * element which cannot have text nodes as its child, collapses selection at
   * before the element node.  Otherwise, collapses selection at start of the
   * editing host.
   *
   * @param aIgnoreIfSelectionInEditingHost
   *                        This method does nothing if selection is in the
   *                        editing host except if it's collapsed at start of
   *                        the editing host.
   *                        Note that if selection ranges were outside of
   *                        current selection limiter, selection was collapsed
   *                        at the start of the editing host therefore, if
   *                        you call this with setting this to true, you can
   *                        keep selection ranges if user has already been
   *                        changed.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MaybeCollapseSelectionAtFirstEditableNode(
      bool aIgnoreIfSelectionInEditingHost) const;

  /**
   * Join aLeftText and aRightText with normalizing white-spaces at the joining
   * point if it's required.  aRightText must be the next sibling of aLeftText.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<JoinNodesResult, nsresult>
  JoinTextNodesWithNormalizeWhiteSpaces(Text& aLeftText, Text& aRightText);

  class BlobReader final {
    using AutoEditActionDataSetter = EditorBase::AutoEditActionDataSetter;

   public:
    MOZ_CAN_RUN_SCRIPT BlobReader(dom::BlobImpl* aBlob, HTMLEditor* aHTMLEditor,
                                  SafeToInsertData aSafeToInsertData,
                                  const EditorDOMPoint& aPointToInsert,
                                  DeleteSelectedContent aDeleteSelectedContent,
                                  const Element& aEditingHost);

    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(BlobReader)
    NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(BlobReader)

    MOZ_CAN_RUN_SCRIPT nsresult OnResult(const nsACString& aResult);
    nsresult OnError(const nsAString& aErrorName);

   private:
    ~BlobReader() = default;

    RefPtr<dom::BlobImpl> mBlob;
    RefPtr<HTMLEditor> mHTMLEditor;
    RefPtr<const Element> mEditingHost;
    RefPtr<DataTransfer> mDataTransfer;
    EditorDOMPoint mPointToInsert;
    EditAction mEditAction;
    SafeToInsertData mSafeToInsertData;
    DeleteSelectedContent mDeleteSelectedContent;
    bool mNeedsToDispatchBeforeInputEvent;
  };

  void CreateEventListeners() final;
  nsresult InstallEventListeners() final;

  bool ShouldReplaceRootElement() const;
  MOZ_CAN_RUN_SCRIPT void NotifyRootChanged();
  Element* GetBodyElement() const;

  /**
   * Get the focused node of this editor.
   * @return    If the editor has focus, this returns the focused node.
   *            Otherwise, returns null.
   */
  nsINode* GetFocusedNode() const;

  already_AddRefed<Element> GetInputEventTargetElement() const final;

  /**
   * Return TRUE if aElement is a table-related elemet and caret was set.
   */
  MOZ_CAN_RUN_SCRIPT bool SetCaretInTableCell(dom::Element* aElement);

  /**
   * HandleTabKeyPressInTable() handles "Tab" key press in table if selection
   * is in a `<table>` element.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult>
  HandleTabKeyPressInTable(WidgetKeyboardEvent* aKeyboardEvent);

  /**
   * InsertPosition is an enum to indicate where the method should insert to.
   */
  enum class InsertPosition {
    // Before selected cell or a cell containing first selection range.
    eBeforeSelectedCell,
    // After selected cell or a cell containing first selection range.
    eAfterSelectedCell,
  };

  /**
   * InsertTableCellsWithTransaction() inserts <td> elements at aPointToInsert.
   * Note that this simply inserts <td> elements, i.e., colspan and rowspan
   * around the cell containing selection are not modified.  So, for example,
   * adding a cell to rectangular table changes non-rectangular table.
   * And if the cell containing selection is at left of row-spanning cell,
   * it may be moved to right side of the row-spanning cell after inserting
   * some cell elements before it.  Similarly, colspan won't be adjusted
   * for keeping table rectangle.
   * Finally, puts caret into previous cell of the insertion point or the
   * first inserted cell if aPointToInsert is start of the row.
   *
   * @param aPointToInsert              The place to insert one or more cell
   *                                    elements.  The container must be a
   *                                    <tr> element.
   * @param aNumberOfCellsToInsert      Number of cells to insert.
   * @return                            The first inserted cell element and
   *                                    start of the last inserted cell element
   *                                    as a point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateElementResult, nsresult>
  InsertTableCellsWithTransaction(const EditorDOMPoint& aPointToInsert,
                                  int32_t aNumberOfCellsToInsert);

  /**
   * InsertTableColumnsWithTransaction() inserts cell elements to every rows
   * at same column index as the cell specified by aPointToInsert.
   *
   * @param aNumberOfColumnsToInsert    Number of columns to insert.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertTableColumnsWithTransaction(
      const EditorDOMPoint& aPointToInsert, int32_t aNumberOfColumnsToInsert);

  /**
   * InsertTableRowsWithTransaction() inserts <tr> elements before or after
   * aCellElement.  When aCellElement spans rows and aInsertPosition is
   * eAfterSelectedCell, new rows will be inserted after the most-bottom row
   * which contains the cell.
   *
   * @param aCellElement                The cell element pinting where this will
   *                                    insert a row before or after.
   * @param aNumberOfRowsToInsert       Number of rows to insert.
   * @param aInsertPosition             Before or after the target cell which
   *                                    contains first selection range.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertTableRowsWithTransaction(
      Element& aCellElement, int32_t aNumberOfRowsToInsert,
      InsertPosition aInsertPosition);

  /**
   * Insert a new cell after or before supplied aCell.
   * Optional: If aNewCell supplied, returns the newly-created cell (addref'd,
   * of course)
   * This doesn't change or use the current selection.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertCell(Element* aCell, int32_t aRowSpan,
                                         int32_t aColSpan, bool aAfter,
                                         bool aIsHeader, Element** aNewCell);

  /**
   * DeleteSelectedTableColumnsWithTransaction() removes cell elements which
   * belong to same columns of selected cell elements.
   * If only one cell element is selected or first selection range is
   * in a cell, removes cell elements which belong to same column.
   * If 2 or more cell elements are selected, removes cell elements which
   * belong to any of all selected columns.  In this case,
   * aNumberOfColumnsToDelete is ignored.
   * If there is no selection ranges, returns error.
   * If selection is not in a cell element, this does not return error,
   * just does nothing.
   * WARNING: This does not remove <col> nor <colgroup> elements.
   *
   * @param aNumberOfColumnsToDelete    Number of columns to remove.  This is
   *                                    ignored if 2 ore more cells are
   *                                    selected.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteSelectedTableColumnsWithTransaction(int32_t aNumberOfColumnsToDelete);

  /**
   * DeleteTableColumnWithTransaction() removes cell elements which belong
   * to the specified column.
   * This method adjusts colspan attribute value if cells spanning the
   * column to delete.
   * WARNING: This does not remove <col> nor <colgroup> elements.
   *
   * @param aTableElement       The <table> element which contains the
   *                            column which you want to remove.
   * @param aRowIndex           Index of the column which you want to remove.
   *                            0 is the first column.
   */
  MOZ_CAN_RUN_SCRIPT nsresult DeleteTableColumnWithTransaction(
      Element& aTableElement, int32_t aColumnIndex);

  /**
   * DeleteSelectedTableRowsWithTransaction() removes <tr> elements.
   * If only one cell element is selected or first selection range is
   * in a cell, removes <tr> elements starting from a <tr> element
   * containing the selected cell or first selection range.
   * If 2 or more cell elements are selected, all <tr> elements
   * which contains selected cell(s).  In this case, aNumberOfRowsToDelete
   * is ignored.
   * If there is no selection ranges, returns error.
   * If selection is not in a cell element, this does not return error,
   * just does nothing.
   *
   * @param aNumberOfRowsToDelete   Number of rows to remove.  This is ignored
   *                                if 2 or more cells are selected.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteSelectedTableRowsWithTransaction(int32_t aNumberOfRowsToDelete);

  /**
   * DeleteTableRowWithTransaction() removes a <tr> element whose index in
   * the <table> is aRowIndex.
   * This method adjusts rowspan attribute value if the <tr> element contains
   * cells which spans rows.
   *
   * @param aTableElement       The <table> element which contains the
   *                            <tr> element which you want to remove.
   * @param aRowIndex           Index of the <tr> element which you want to
   *                            remove.  0 is the first row.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteTableRowWithTransaction(Element& aTableElement, int32_t aRowIndex);

  /**
   * DeleteTableCellWithTransaction() removes table cell elements.  If two or
   * more cell elements are selected, this removes all selected cell elements.
   * Otherwise, this removes some cell elements starting from selected cell
   * element or a cell containing first selection range.  When this removes
   * last cell element in <tr> or <table>, this removes the <tr> or the
   * <table> too.  Note that when removing a cell causes number of its row
   * becomes less than the others, this method does NOT fill the place with
   * rowspan nor colspan.  This does not return error even if selection is not
   * in cell element, just does nothing.
   *
   * @param aNumberOfCellsToDelete  Number of cells to remove.  This is ignored
   *                                if 2 or more cells are selected.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteTableCellWithTransaction(int32_t aNumberOfCellsToDelete);

  /**
   * DeleteAllChildrenWithTransaction() removes all children of aElement from
   * the tree.
   *
   * @param aElement        The element whose children you want to remove.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteAllChildrenWithTransaction(Element& aElement);

  /**
   * Move all contents from aCellToMerge into aTargetCell (append at end).
   */
  MOZ_CAN_RUN_SCRIPT nsresult MergeCells(RefPtr<Element> aTargetCell,
                                         RefPtr<Element> aCellToMerge,
                                         bool aDeleteCellToMerge);

  /**
   * DeleteTableElementAndChildren() removes aTableElement (and its children)
   * from the DOM tree with transaction.
   *
   * @param aTableElement   The <table> element which you want to remove.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DeleteTableElementAndChildrenWithTransaction(Element& aTableElement);

  MOZ_CAN_RUN_SCRIPT nsresult SetColSpan(Element* aCell, int32_t aColSpan);
  MOZ_CAN_RUN_SCRIPT nsresult SetRowSpan(Element* aCell, int32_t aRowSpan);

  /**
   * Helper used to get nsTableWrapperFrame for a table.
   */
  static nsTableWrapperFrame* GetTableFrame(const Element* aTable);

  /**
   * GetNumberOfCellsInRow() returns number of actual cell elements in the row.
   * If some cells appear by "rowspan" in other rows, they are ignored.
   *
   * @param aTableElement   The <table> element.
   * @param aRowIndex       Valid row index in aTableElement.  This method
   *                        counts cell elements in the row.
   * @return                -1 if this meets unexpected error.
   *                        Otherwise, number of cells which this method found.
   */
  int32_t GetNumberOfCellsInRow(Element& aTableElement, int32_t aRowIndex);

  /**
   * Test if all cells in row or column at given index are selected.
   */
  bool AllCellsInRowSelected(Element* aTable, int32_t aRowIndex,
                             int32_t aNumberOfColumns);
  bool AllCellsInColumnSelected(Element* aTable, int32_t aColIndex,
                                int32_t aNumberOfRows);

  bool IsEmptyCell(Element* aCell);

  /**
   * Most insert methods need to get the same basic context data.
   * Any of the pointers may be null if you don't need that datum (for more
   * efficiency).
   * Input: *aCell is a known cell,
   *        if null, cell is obtained from the anchor node of the selection.
   * Returns NS_EDITOR_ELEMENT_NOT_FOUND if cell is not found even if aCell is
   * null.
   */
  MOZ_CAN_RUN_SCRIPT nsresult GetCellContext(Element** aTable, Element** aCell,
                                             nsINode** aCellParent,
                                             int32_t* aCellOffset,
                                             int32_t* aRowIndex,
                                             int32_t* aColIndex);

  nsresult GetCellSpansAt(Element* aTable, int32_t aRowIndex, int32_t aColIndex,
                          int32_t& aActualRowSpan, int32_t& aActualColSpan);

  MOZ_CAN_RUN_SCRIPT nsresult SplitCellIntoColumns(
      Element* aTable, int32_t aRowIndex, int32_t aColIndex,
      int32_t aColSpanLeft, int32_t aColSpanRight, Element** aNewCell);

  MOZ_CAN_RUN_SCRIPT nsresult SplitCellIntoRows(
      Element* aTable, int32_t aRowIndex, int32_t aColIndex,
      int32_t aRowSpanAbove, int32_t aRowSpanBelow, Element** aNewCell);

  MOZ_CAN_RUN_SCRIPT nsresult CopyCellBackgroundColor(Element* aDestCell,
                                                      Element* aSourceCell);

  /**
   * Reduce rowspan/colspan when cells span into nonexistent rows/columns.
   */
  MOZ_CAN_RUN_SCRIPT nsresult FixBadRowSpan(Element* aTable, int32_t aRowIndex,
                                            int32_t& aNewRowCount);
  MOZ_CAN_RUN_SCRIPT nsresult FixBadColSpan(Element* aTable, int32_t aColIndex,
                                            int32_t& aNewColCount);

  /**
   * XXX NormalizeTableInternal() is broken.  If it meets a cell which has
   *     bigger or smaller rowspan or colspan than actual number of cells,
   *     this always failed to scan the table.  Therefore, this does nothing
   *     when the table should be normalized.
   *
   * @param aTableOrElementInTable  An element which is in a <table> element
   *                                or <table> element itself.  Otherwise,
   *                                this returns NS_OK but does nothing.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  NormalizeTableInternal(Element& aTableOrElementInTable);

  /**
   * Fallback method: Call this after using ClearSelection() and you
   * failed to set selection to some other content in the document.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SetSelectionAtDocumentStart();

  // Methods for handling plaintext quotations
  MOZ_CAN_RUN_SCRIPT nsresult PasteAsPlaintextQuotation(
      nsIClipboard::ClipboardType aSelectionType, DataTransfer* aDataTransfer,
      const Element& aEditingHost);

  enum class AddCites { No, Yes };
  /**
   * Insert a string as quoted text, replacing the selected text (if any).
   * @param aQuotedText     The string to insert.
   * @param aAddCites       Whether to prepend extra ">" to each line
   *                        (usually true, unless those characters
   *                        have already been added.)
   * @param aEditingHost    The editing host.
   * @return aNodeInserted  The node spanning the insertion, if applicable.
   *                        If aAddCites is false, this will be null.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertAsPlaintextQuotation(
      const nsAString& aQuotedText, AddCites aAddCites,
      const Element& aEditingHost, nsINode** aNodeInserted = nullptr);

  /**
   * InsertObject() inserts given object at aPointToInsert.
   *
   * @param aType one of kFileMime, kJPEGImageMime, kJPGImageMime,
   *              kPNGImageMime, kGIFImageMime.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertObject(
      const nsACString& aType, nsISupports* aObject,
      SafeToInsertData aSafeToInsertData, const EditorDOMPoint& aPointToInsert,
      DeleteSelectedContent aDeleteSelectedContent,
      const Element& aEditingHost);

  class HTMLTransferablePreparer;
  nsresult PrepareHTMLTransferable(nsITransferable** aTransferable,
                                   const Element* aEditingHost) const;

  enum class HavePrivateHTMLFlavor { No, Yes };
  MOZ_CAN_RUN_SCRIPT nsresult InsertFromTransferableAtSelection(
      nsITransferable* aTransferable, const nsAString& aContextStr,
      const nsAString& aInfoStr, HavePrivateHTMLFlavor aHavePrivateHTMLFlavor,
      const Element& aEditingHost);

  /**
   * InsertFromDataTransfer() is called only when user drops data into
   * this editor.  Don't use this method for other purposes.
   *
   * @param aIndex index of aDataTransfer's item to insert.
   */
  MOZ_CAN_RUN_SCRIPT nsresult InsertFromDataTransfer(
      const DataTransfer* aDataTransfer, uint32_t aIndex,
      nsIPrincipal* aSourcePrincipal, const EditorDOMPoint& aDroppedAt,
      DeleteSelectedContent aDeleteSelectedContent,
      const Element& aEditingHost);

  MOZ_CAN_RUN_SCRIPT nsresult InsertURLAsLinkInternal(
      const nsAString& aURL, const EditorDOMPoint& aPointToInsert,
      DeleteSelectedContent aDeleteSelectedContent);

  static HavePrivateHTMLFlavor DataTransferOrClipboardHasPrivateHTMLFlavor(
      DataTransfer* aDataTransfer, nsIClipboard* clipboard);

  /**
   * CF_HTML:
   * <https://docs.microsoft.com/en-us/windows/win32/dataxchg/html-clipboard-format>.
   *
   * @param[in]  aCfhtml a CF_HTML string as defined above.
   * @param[out] aStuffToPaste the fragment, excluding context.
   * @param[out] aCfcontext the context, excluding the fragment, including a
   *                        marker (`kInsertionCookie`) indicating where the
   *                        fragment begins.
   */
  nsresult ParseCFHTML(const nsCString& aCfhtml, char16_t** aStuffToPaste,
                       char16_t** aCfcontext);

  /**
   * AutoHTMLFragmentBoundariesFixer fixes both edges of topmost child contents
   * which are created with SubtreeContentIterator.
   */
  class MOZ_STACK_CLASS AutoHTMLFragmentBoundariesFixer final {
   public:
    /**
     * @param aArrayOfTopMostChildContents
     *                         [in/out] The topmost child contents which will be
     *                         inserted into the DOM tree.  Both edges, i.e.,
     *                         first node and last node in this array will be
     *                         checked whether they can be inserted into
     *                         another DOM tree.  If not, it'll replaces some
     *                         orphan nodes around nodes with proper parent.
     */
    explicit AutoHTMLFragmentBoundariesFixer(
        nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents);

   private:
    /**
     * EnsureBeginsOrEndsWithValidContent() replaces some nodes starting from
     * start or end with proper element node if it's necessary.
     * If first or last node of aArrayOfTopMostChildContents is in list and/or
     * `<table>` element, looks for topmost list element or `<table>` element
     * with `CollectTableAndAnyListElementsOfInclusiveAncestorsAt()` and
     * `GetMostDistantAncestorListOrTableElement()`.  Then, checks
     * whether some nodes are in aArrayOfTopMostChildContents are the topmost
     * list/table element or its descendant and if so, removes the nodes from
     * aArrayOfTopMostChildContents and inserts the list/table element instead.
     * Then, aArrayOfTopMostChildContents won't start/end with list-item nor
     * table cells.
     */
    enum class StartOrEnd { start, end };
    void EnsureBeginsOrEndsWithValidContent(
        StartOrEnd aStartOrEnd,
        nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents)
        const;

    /**
     * CollectTableAndAnyListElementsOfInclusiveAncestorsAt() collects list
     * elements and table related elements from the inclusive ancestors
     * (https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor) of aNode.
     */
    static void CollectTableAndAnyListElementsOfInclusiveAncestorsAt(
        nsIContent& aContent,
        nsTArray<OwningNonNull<Element>>& aOutArrayOfListAndTableElements);

    /**
     * GetMostDistantAncestorListOrTableElement() returns a list or a
     * `<table>` element which is in
     * aInclusiveAncestorsTableOrListElements and they are actually
     * valid ancestor of at least one of aArrayOfTopMostChildContents.
     */
    static Element* GetMostDistantAncestorListOrTableElement(
        const nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents,
        const nsTArray<OwningNonNull<Element>>&
            aInclusiveAncestorsTableOrListElements);

    /**
     * FindReplaceableTableElement() is a helper method of
     * EnsureBeginsOrEndsWithValidContent().  If aNodeMaybeInTableElement is
     * a descendant of aTableElement, returns aNodeMaybeInTableElement or its
     * nearest ancestor whose tag name is `<td>`, `<th>`, `<tr>`, `<thead>`,
     * `<tfoot>`, `<tbody>` or `<caption>`.
     *
     * @param aTableElement               Must be a `<table>` element.
     * @param aContentMaybeInTableElement A node which may be in aTableElement.
     */
    Element* FindReplaceableTableElement(
        Element& aTableElement, nsIContent& aContentMaybeInTableElement) const;

    /**
     * IsReplaceableListElement() is a helper method of
     * EnsureBeginsOrEndsWithValidContent().  If aNodeMaybeInListElement is a
     * descendant of aListElement, returns true.  Otherwise, false.
     *
     * @param aListElement                Must be a list element.
     * @param aContentMaybeInListElement  A node which may be in aListElement.
     */
    bool IsReplaceableListElement(Element& aListElement,
                                  nsIContent& aContentMaybeInListElement) const;
  };

  /**
   * MakeDefinitionListItemWithTransaction() replaces parent list of current
   * selection with <dl> or create new <dl> element and creates a definition
   * list item whose name is aTagName.
   *
   * @param aTagName            Must be nsGkAtoms::dt or nsGkAtoms::dd.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  MakeDefinitionListItemWithTransaction(nsAtom& aTagName);

  /**
   * FormatBlockContainerAsSubAction() inserts a block element whose name
   * is aTagName at selection.  If selection is not collapsed and aTagName is
   * nsGkAtoms::normal or nsGkAtoms::_empty, this removes block containers.
   *
   * @param aTagName            A block level element name.  Must NOT be
   *                            nsGkAtoms::dt nor nsGkAtoms::dd.
   * @param aFormatBlockMode    Whether HTML formatBlock command or XUL
   *                            paragraphState command.
   * @param aEditingHost        The editing host.
   */
  MOZ_CAN_RUN_SCRIPT nsresult FormatBlockContainerAsSubAction(
      const nsStaticAtom& aTagName, FormatBlockMode aFormatBlockMode,
      const Element& aEditingHost);

  /**
   * Increase/decrease the font size of selection.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  IncrementOrDecrementFontSizeAsSubAction(FontSize aIncrementOrDecrement);

  /**
   * Wrap aContent in <big> or <small> element and make children of
   * <font size=n> wrap with <big> or <small> too.  Note that if there is
   * opposite element for aIncrementOrDecrement, their children will be just
   * unwrapped.
   *
   * @param aDir        Whether increase or decrease the font size of aContent.
   * @param aContent    The content node whose font size will be changed.
   * @return            A suggest point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SetFontSizeWithBigOrSmallElement(nsIContent& aContent,
                                   FontSize aIncrementOrDecrement);

  /**
   * Adjust font size of font element children recursively with handling
   * <big> and <small> elements.
   *
   * @param aDir        Whether increase or decrease the font size of aContent.
   * @param aContent    The content node whose font size will be changed.
   *                    All descendants will be handled recursively.
   * @return            A suggest point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SetFontSizeOfFontElementChildren(nsIContent& aContent,
                                   FontSize aIncrementOrDecrement);

  /**
   * Get extended range to select element whose all children are selected by
   * aRange.
   */
  EditorRawDOMRange GetExtendedRangeWrappingEntirelySelectedElements(
      const EditorRawDOMRange& aRange) const;

  /**
   * Get extended range to select ancestor <a name> elements.
   */
  EditorRawDOMRange GetExtendedRangeWrappingNamedAnchor(
      const EditorRawDOMRange& aRange) const;

  // Declared in HTMLEditorNestedClasses.h and defined in HTMLStyleEditor.cpp
  class AutoInlineStyleSetter;

  /**
   * RemoveStyleInside() removes elements which represent aStyleToRemove
   * and removes CSS style.  This handles aElement and all its descendants
   * (including leaf text nodes) recursively.
   * TODO: Rename this to explain that this maybe remove aElement from the DOM
   *       tree.
   *
   * @param aSpecifiedStyle  Whether the class and style attributes should
   *                         be preserved or discarded.
   * @return                 A suggest point to put caret.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  RemoveStyleInside(Element& aElement, const EditorInlineStyle& aStyleToRemove,
                    SpecifiedStyle aSpecifiedStyle);

  /**
   * CollectEditableLeafTextNodes() collects text nodes in aElement.
   */
  void CollectEditableLeafTextNodes(
      Element& aElement, nsTArray<OwningNonNull<Text>>& aLeafTextNodes) const;

  /**
   * IsRemovableParentStyleWithNewSpanElement() checks whether aStyle of parent
   * block can be removed from aContent with creating `<span>` element.  Note
   * that this does NOT check whether the specified style comes from parent
   * block or not.
   * XXX This may destroy the editor, but using `Result<bool, nsresult>`
   *     is not reasonable because code for accessing the result becomes
   *     messy.  However, anybody must forget to check `Destroyed()` after
   *     calling this.  Which is the way to smart to make every caller
   *     must check the editor state?
   */
  MOZ_CAN_RUN_SCRIPT Result<bool, nsresult>
  IsRemovableParentStyleWithNewSpanElement(
      nsIContent& aContent, const EditorInlineStyle& aStyle) const;

  /**
   * HasStyleOrIdOrClassAttribute() returns true when at least one of
   * `style`, `id` or `class` attribute value of aElement is not empty.
   */
  static bool HasStyleOrIdOrClassAttribute(Element& aElement);

  /**
   * Whether the outer window of the DOM event target has focus or not.
   */
  bool OurWindowHasFocus() const;

  class HTMLWithContextInserter;

  /**
   * This function is used to insert a string of HTML input optionally with some
   * context information into the editable field.  The HTML input either comes
   * from a transferable object created as part of a drop/paste operation, or
   * from the InsertHTML method.  We may want the HTML input to be sanitized
   * (for example, if it's coming from a transferable object), in which case
   * aTrustedInput should be set to false, otherwise, the caller should set it
   * to true, which means that the HTML will be inserted in the DOM verbatim.
   */
  enum class InlineStylesAtInsertionPoint {
    Preserve,  // If you want the paste to be affected by local style, e.g.,
               // for the insertHTML command, use "Preserve"
    Clear,     // If you want the paste to be keep its own style, e.g., pasting
               // from clipboard, use "Clear"
  };
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult InsertHTMLWithContextAsSubAction(
      const nsAString& aInputString, const nsAString& aContextStr,
      const nsAString& aInfoStr, const nsAString& aFlavor,
      SafeToInsertData aSafeToInsertData, const EditorDOMPoint& aPointToInsert,
      DeleteSelectedContent aDeleteSelectedContent,
      InlineStylesAtInsertionPoint aInlineStylesAtInsertionPoint,
      const Element& aEditingHost);

  /**
   * sets the position of an element; warning it does NOT check if the
   * element is already positioned or not and that's on purpose.
   * @param aStyledElement      [IN] the element
   * @param aX                  [IN] the x position in pixels.
   * @param aY                  [IN] the y position in pixels.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SetTopAndLeftWithTransaction(
      nsStyledElement& aStyledElement, int32_t aX, int32_t aY);

  /**
   * Reset a selected cell or collapsed selection (the caret) after table
   * editing.
   *
   * @param aTable      A table in the document.
   * @param aRow        The row ...
   * @param aCol        ... and column defining the cell where we will try to
   *                    place the caret.
   * @param aSelected   If true, we select the whole cell instead of setting
   *                    caret.
   * @param aDirection  If cell at (aCol, aRow) is not found, search for
   *                    previous cell in the same column (aPreviousColumn) or
   *                    row (ePreviousRow) or don't search for another cell
   *                    (aNoSearch).  If no cell is found, caret is place just
   *                    before table; and if that fails, at beginning of
   *                    document.  Thus we generally don't worry about the
   *                    return value and can use the
   *                    AutoSelectionSetterAfterTableEdit stack-based object to
   *                    insure we reset the caret in a table-editing method.
   */
  MOZ_CAN_RUN_SCRIPT void SetSelectionAfterTableEdit(Element* aTable,
                                                     int32_t aRow, int32_t aCol,
                                                     int32_t aDirection,
                                                     bool aSelected);

  void RemoveListenerAndDeleteRef(const nsAString& aEvent,
                                  nsIDOMEventListener* aListener,
                                  bool aUseCapture, ManualNACPtr aElement,
                                  PresShell* aPresShell);
  void DeleteRefToAnonymousNode(ManualNACPtr aContent, PresShell* aPresShell);

  /**
   * RefreshEditingUI() may refresh editing UIs for current Selection, focus,
   * etc.  If this shows or hides some UIs, it causes reflow.  So, this is
   * not safe method.
   */
  MOZ_CAN_RUN_SCRIPT nsresult RefreshEditingUI();

  /**
   * Returns the offset of an element's frame to its absolute containing block.
   */
  nsresult GetElementOrigin(Element& aElement, int32_t& aX, int32_t& aY);
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult GetPositionAndDimensions(
      Element& aElement, int32_t& aX, int32_t& aY, int32_t& aW, int32_t& aH,
      int32_t& aBorderLeft, int32_t& aBorderTop, int32_t& aMarginLeft,
      int32_t& aMarginTop);

  bool IsInObservedSubtree(nsIContent* aChild);

  void UpdateRootElement();

  /**
   * SetAllResizersPosition() moves all resizers to proper position.
   * If the resizers are hidden or replaced with another set of resizers
   * while this is running, this returns error.  So, callers shouldn't
   * keep handling the resizers if this returns error.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult SetAllResizersPosition();

  /**
   * Shows active resizers around an element's frame
   * @param aResizedElement [IN] a DOM Element
   */
  MOZ_CAN_RUN_SCRIPT nsresult ShowResizersInternal(Element& aResizedElement);

  /**
   * Hide resizers if they are visible.  If this is called while there is no
   * visible resizers, this does not return error, but does nothing.
   */
  nsresult HideResizersInternal();

  /**
   * RefreshResizersInternal() moves resizers to proper position.  This does
   * nothing if there is no resizing target.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult RefreshResizersInternal();

  ManualNACPtr CreateResizer(int16_t aLocation, nsIContent& aParentContent);
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetAnonymousElementPositionWithoutTransaction(nsStyledElement& aStyledElement,
                                                int32_t aX, int32_t aY);

  ManualNACPtr CreateShadow(nsIContent& aParentContent,
                            Element& aOriginalObject);

  /**
   * SetShadowPosition() moves the shadow element to proper position.
   *
   * @param aShadowElement      Must be mResizingShadow or mPositioningShadow.
   * @param aElement            The element which has the shadow.
   * @param aElementX           Left of aElement.
   * @param aElementY           Top of aElement.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetShadowPosition(Element& aShadowElement, Element& aElement,
                    int32_t aElementLeft, int32_t aElementTop);

  ManualNACPtr CreateResizingInfo(nsIContent& aParentContent);
  MOZ_CAN_RUN_SCRIPT nsresult SetResizingInfoPosition(int32_t aX, int32_t aY,
                                                      int32_t aW, int32_t aH);

  enum class ResizeAt {
    eX,
    eY,
    eWidth,
    eHeight,
  };
  [[nodiscard]] int32_t GetNewResizingIncrement(int32_t aX, int32_t aY,
                                                ResizeAt aResizeAt) const;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult StartResizing(Element& aHandle);
  int32_t GetNewResizingX(int32_t aX, int32_t aY);
  int32_t GetNewResizingY(int32_t aX, int32_t aY);
  int32_t GetNewResizingWidth(int32_t aX, int32_t aY);
  int32_t GetNewResizingHeight(int32_t aX, int32_t aY);
  void HideShadowAndInfo();
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetFinalSizeWithTransaction(int32_t aX, int32_t aY);
  void SetResizeIncrements(int32_t aX, int32_t aY, int32_t aW, int32_t aH,
                           bool aPreserveRatio);

  /**
   * HideAnonymousEditingUIs() forcibly hides all editing UIs (resizers,
   * inline-table-editing UI, absolute positioning UI).
   *
   * XXX This method is called by the CC, therefore, needs to be a boundary
   * method.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void HideAnonymousEditingUIs();

  /**
   * HideAnonymousEditingUIsIfUnnecessary() hides all editing UIs if some of
   * visible UIs are now unnecessary.
   */
  MOZ_CAN_RUN_SCRIPT void HideAnonymousEditingUIsIfUnnecessary();

  /**
   * sets the z-index of an element.
   * @param aElement [IN] the element
   * @param aZorder  [IN] the z-index
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  SetZIndexWithTransaction(nsStyledElement& aElement, int32_t aZIndex);

  /**
   * shows a grabber attached to an arbitrary element. The grabber is an image
   * positioned on the left hand side of the top border of the element. Draggin
   * and dropping it allows to change the element's absolute position in the
   * document. See chrome://editor/content/images/grabber.gif
   * @param aElement [IN] the element
   */
  MOZ_CAN_RUN_SCRIPT nsresult ShowGrabberInternal(Element& aElement);

  /**
   * Setting grabber to proper position for current mAbsolutelyPositionedObject.
   * For example, while an element has grabber, the element may be resized
   * or repositioned by script or something.  Then, you need to reset grabber
   * position with this.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult RefreshGrabberInternal();

  /**
   * hide the grabber if it shown.
   */
  MOZ_CAN_RUN_SCRIPT void HideGrabberInternal();

  /**
   * CreateGrabberInternal() creates a grabber for moving aParentContent.
   * This sets mGrabber to the new grabber.  If this returns true, it's
   * always non-nullptr.  Otherwise, i.e., the grabber is hidden during
   * creation, this returns false.
   */
  bool CreateGrabberInternal(nsIContent& aParentContent);

  MOZ_CAN_RUN_SCRIPT nsresult StartMoving();
  MOZ_CAN_RUN_SCRIPT nsresult SetFinalPosition(int32_t aX, int32_t aY);
  void SnapToGrid(int32_t& newX, int32_t& newY) const;
  nsresult GrabberClicked();
  MOZ_CAN_RUN_SCRIPT nsresult EndMoving();
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  GetTemporaryStyleForFocusedPositionedElement(Element& aElement,
                                               nsAString& aReturn);

  /**
   * Shows inline table editing UI around a <table> element which contains
   * aCellElement.  This returns error if creating UI is hidden during this,
   * or detects another set of UI during this.  In such case, callers
   * shouldn't keep handling anything for the UI.
   *
   * @param aCellElement    Must be an <td> or <th> element.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  ShowInlineTableEditingUIInternal(Element& aCellElement);

  /**
   * Hide all inline table editing UI.
   */
  void HideInlineTableEditingUIInternal();

  /**
   * RefreshInlineTableEditingUIInternal() moves inline table editing UI to
   * proper position.  This returns error if the UI is hidden or replaced
   * during moving.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult
  RefreshInlineTableEditingUIInternal();

  enum class ContentNodeIs { Inserted, Appended };
  MOZ_CAN_RUN_SCRIPT void DoContentInserted(nsIContent* aChild,
                                            ContentNodeIs aContentNodeIs);

  /**
   * Returns an anonymous Element of type aTag,
   * child of aParentContent. If aIsCreatedHidden is true, the class
   * "hidden" is added to the created element. If aClass is not the empty
   * string, it becomes the value of the class attribute
   * @return a Element
   * @param aTag             [IN] desired type of the element to create
   * @param aParentContent   [IN] the parent node of the created anonymous
   *                              element
   * @param aClass       [IN] contents of the _moz_anonclass attribute
   * @param aIsCreatedHidden [IN] a boolean specifying if the class "hidden"
   *                              is to be added to the created anonymous
   *                              element
   */
  ManualNACPtr CreateAnonymousElement(nsAtom* aTag, nsIContent& aParentContent,
                                      const nsAString& aClass,
                                      bool aIsCreatedHidden);

  /**
   * Reads a blob into memory and notifies the BlobReader object when the read
   * operation is finished.
   *
   * @param aBlob       The input blob
   * @param aGlobal     The global object under which the read should happen.
   * @param aBlobReader The blob reader object to be notified when finished.
   */
  static nsresult SlurpBlob(dom::Blob* aBlob, nsIGlobalObject* aGlobal,
                            BlobReader* aBlobReader);

  /**
   * For saving allocation cost in the constructor of
   * EditorBase::TopLevelEditSubActionData, we should reuse same RangeItem
   * instance with all top level edit sub actions.
   * The instance is always cleared when TopLevelEditSubActionData is
   * destructed and the class is stack only class so that we don't need
   * to (and also should not) add the RangeItem into the cycle collection.
   */
  [[nodiscard]] inline already_AddRefed<RangeItem>
  GetSelectedRangeItemForTopLevelEditSubAction() const;

  /**
   * For saving allocation cost in the constructor of
   * EditorBase::TopLevelEditSubActionData, we should reuse same nsRange
   * instance with all top level edit sub actions.
   * The instance is always cleared when TopLevelEditSubActionData is
   * destructed, but AbstractRange::mOwner keeps grabbing the owner document
   * so that we need to make it in the cycle collection.
   */
  [[nodiscard]] inline already_AddRefed<nsRange>
  GetChangedRangeForTopLevelEditSubAction() const;

  MOZ_CAN_RUN_SCRIPT void DidDoTransaction(
      TransactionManager& aTransactionManager, nsITransaction& aTransaction,
      nsresult aDoTransactionResult) {
    if (mComposerCommandsUpdater) {
      RefPtr<ComposerCommandsUpdater> updater(mComposerCommandsUpdater);
      updater->DidDoTransaction(aTransactionManager);
    }
  }

  MOZ_CAN_RUN_SCRIPT void DidUndoTransaction(
      TransactionManager& aTransactionManager, nsITransaction& aTransaction,
      nsresult aUndoTransactionResult) {
    if (mComposerCommandsUpdater) {
      RefPtr<ComposerCommandsUpdater> updater(mComposerCommandsUpdater);
      updater->DidUndoTransaction(aTransactionManager);
    }
  }

  MOZ_CAN_RUN_SCRIPT void DidRedoTransaction(
      TransactionManager& aTransactionManager, nsITransaction& aTransaction,
      nsresult aRedoTransactionResult) {
    if (mComposerCommandsUpdater) {
      RefPtr<ComposerCommandsUpdater> updater(mComposerCommandsUpdater);
      updater->DidRedoTransaction(aTransactionManager);
    }
  }

 protected:
  /**
   * IndentListChildWithTransaction() is a helper method of
   * Handle(CSS|HTML)IndentAtSelectionInternal().
   *
   * @param aSubListElement     [in/out] Specify a sub-list element of the
   *                            container of aPointInListElement or nullptr.
   *                            When there is no proper sub-list element to
   *                            move aContentMovingToSubList, this method
   *                            inserts a new sub-list element and update this
   *                            to it.
   * @param aPointInListElement A point in a list element whose child should
   *                            be indented.  If this method creates new list
   *                            element into the list element, this inserts
   *                            the new list element to this point.
   * @param aContentMovingToSubList
   *                            A content node which is a child of a list
   *                            element and should be moved into a sub-list
   *                            element.
   * @param aEditingHost        The editing host.
   * @return                    A candidate caret position.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  IndentListChildWithTransaction(RefPtr<Element>* aSubListElement,
                                 const EditorDOMPoint& aPointInListElement,
                                 nsIContent& aContentMovingToSubList,
                                 const Element& aEditingHost);

  /**
   * Stack based helper class for calling EditorBase::EndTransactionInternal().
   * NOTE:  This does not suppress multiple input events.  In most cases,
   *        only one "input" event should be fired for an edit action rather
   *        than per edit sub-action.  In such case, you should use
   *        EditorBase::AutoPlaceholderBatch instead.
   */
  class MOZ_RAII AutoTransactionBatch final {
   public:
    /**
     * @param aRequesterFuncName function name which wants to end the batch.
     * This won't be stored nor exposed to selection listeners etc, used only
     * for logging. This MUST be alive when the destructor runs.
     */
    MOZ_CAN_RUN_SCRIPT explicit AutoTransactionBatch(
        HTMLEditor& aHTMLEditor, const char* aRequesterFuncName)
        : mHTMLEditor(aHTMLEditor), mRequesterFuncName(aRequesterFuncName) {
      MOZ_KnownLive(mHTMLEditor).BeginTransactionInternal(mRequesterFuncName);
    }

    MOZ_CAN_RUN_SCRIPT ~AutoTransactionBatch() {
      MOZ_KnownLive(mHTMLEditor).EndTransactionInternal(mRequesterFuncName);
    }

   protected:
    // The lifetime must be guaranteed by the creator of this instance.
    MOZ_KNOWN_LIVE HTMLEditor& mHTMLEditor;
    const char* const mRequesterFuncName;
  };

  RefPtr<PendingStyles> mPendingStylesToApplyToNewContent;
  RefPtr<ComposerCommandsUpdater> mComposerCommandsUpdater;

  // Used by TopLevelEditSubActionData::mSelectedRange.
  mutable RefPtr<RangeItem> mSelectedRangeForTopLevelEditSubAction;
  // Used by TopLevelEditSubActionData::mChangedRange.
  mutable RefPtr<nsRange> mChangedRangeForTopLevelEditSubAction;

  RefPtr<Runnable> mPendingRootElementUpdatedRunner;
  RefPtr<DocumentModifiedEvent> mPendingDocumentModifiedRunner;

  // mPaddingBRElementForEmptyEditor should be used for placing caret
  // at proper position when editor is empty.
  RefPtr<dom::HTMLBRElement> mPaddingBRElementForEmptyEditor;

  // This is set only when HandleInsertText appended a collapsible white-space.
  RefPtr<dom::Text> mLastCollapsibleWhiteSpaceAppendedTextNode;

  // While this instance or its helper class updates the DOM with a DOM API,
  // this is set to the wrapper class to call the DOM API.
  const AutoDOMAPIWrapperBase* mRunningDOMAPIWrapper = nullptr;

  bool mCRInParagraphCreatesParagraph;

  // resizing
  bool mIsObjectResizingEnabled;
  bool mIsResizing;
  bool mPreserveRatio;
  bool mResizedObjectIsAnImage;

  // absolute positioning
  bool mIsAbsolutelyPositioningEnabled;
  bool mResizedObjectIsAbsolutelyPositioned;
  bool mGrabberClicked;
  bool mIsMoving;

  bool mSnapToGridEnabled;

  // inline table editing
  bool mIsInlineTableEditingEnabled;

  bool mIsCSSPrefChecked;

  // resizing
  ManualNACPtr mTopLeftHandle;
  ManualNACPtr mTopHandle;
  ManualNACPtr mTopRightHandle;
  ManualNACPtr mLeftHandle;
  ManualNACPtr mRightHandle;
  ManualNACPtr mBottomLeftHandle;
  ManualNACPtr mBottomHandle;
  ManualNACPtr mBottomRightHandle;

  RefPtr<Element> mActivatedHandle;

  ManualNACPtr mResizingShadow;
  ManualNACPtr mResizingInfo;

  RefPtr<Element> mResizedObject;

  int32_t mOriginalX;
  int32_t mOriginalY;

  int32_t mResizedObjectX;
  int32_t mResizedObjectY;
  int32_t mResizedObjectWidth;
  int32_t mResizedObjectHeight;

  int32_t mResizedObjectMarginLeft;
  int32_t mResizedObjectMarginTop;
  int32_t mResizedObjectBorderLeft;
  int32_t mResizedObjectBorderTop;

  int32_t mXIncrementFactor;
  int32_t mYIncrementFactor;
  int32_t mWidthIncrementFactor;
  int32_t mHeightIncrementFactor;

  int8_t mInfoXIncrement;
  int8_t mInfoYIncrement;

  // absolute positioning
  int32_t mPositionedObjectX;
  int32_t mPositionedObjectY;
  int32_t mPositionedObjectWidth;
  int32_t mPositionedObjectHeight;

  int32_t mPositionedObjectMarginLeft;
  int32_t mPositionedObjectMarginTop;
  int32_t mPositionedObjectBorderLeft;
  int32_t mPositionedObjectBorderTop;

  RefPtr<Element> mAbsolutelyPositionedObject;
  ManualNACPtr mGrabber;
  ManualNACPtr mPositioningShadow;

  int32_t mGridSize;

  // inline table editing
  RefPtr<Element> mInlineEditedCell;

  ManualNACPtr mAddColumnBeforeButton;
  ManualNACPtr mRemoveColumnButton;
  ManualNACPtr mAddColumnAfterButton;

  ManualNACPtr mAddRowBeforeButton;
  ManualNACPtr mRemoveRowButton;
  ManualNACPtr mAddRowAfterButton;

  void AddPointerClickListener(Element* aElement);
  void RemovePointerClickListener(Element* aElement);

  bool mDisabledLinkHandling = false;
  bool mOldLinkHandlingEnabled = false;

  bool mHasBeforeInputBeenCanceled = false;

  bool mHasFocus = false;
  bool mIsInDesignMode = false;

  ParagraphSeparator mDefaultParagraphSeparator;

  friend class AlignStateAtSelection;  // CollectEditableTargetNodes,
                                       // CollectNonEditableNodes
  friend class AutoClonedRangeArray;   // RangeUpdaterRef,
                                       // SplitNodeWithTransaction,
                                       // SplitInlineAncestorsAtRangeBoundaries
  friend class AutoDOMAPIWrapperBase;  // OnDOMAPICallEnd,
                                       // OnDOMAPICallStart,
  friend class AutoClonedSelectionRangeArray;  // RangeUpdaterRef,
  friend class AutoSelectionRestore;
  friend class AutoSelectionSetterAfterTableEdit;  // SetSelectionAfterEdit
  friend class CSSEditUtils;  // DoTransactionInternal, HasAttributes,
                              // RemoveContainerWithTransaction
  friend class EditorBase;    // ComputeTargetRanges,
                              // GetChangedRangeForTopLevelEditSubAction,
                              // GetSelectedRangeItemForTopLevelEditSubAction,
                              // MaybeCreatePaddingBRElementForEmptyEditor,
                              // PrepareToInsertBRElement,
                              // ReflectPaddingBRElementForEmptyEditor,
                              // RefreshEditingUI,
                              // mComposerUpdater, mHasBeforeInputBeenCanceled
  friend class JoinNodesTransaction;  // DidJoinNodesTransaction, DoJoinNodes,
                                      // DoSplitNode, // RangeUpdaterRef
  friend class ListElementSelectionState;      // CollectEditTargetNodes,
                                               // CollectNonEditableNodes
  friend class ListItemElementSelectionState;  // CollectEditTargetNodes,
                                               // CollectNonEditableNodes
  friend class MoveNodeTransaction;      // AllowsTransactionsToChangeSelection,
                                         // CollapseSelectionTo, RangeUpdaterRef
  friend class MoveSiblingsTransaction;  // AllowsTransactionsToChangeSelection,
                                         // CollapseSelectionTo, RangeUpdaterRef
  friend class ParagraphStateAtSelection;  // CollectChildren,
                                           // CollectEditTargetNodes,
                                           // CollectListChildren,
                                           // CollectNonEditableNodes,
                                           // CollectTableChildren
  friend class SlurpBlobEventListener;     // BlobReader
  friend class SplitNodeTransaction;       // DoJoinNodes, DoSplitNode
  friend class TransactionManager;  // DidDoTransaction, DidRedoTransaction,
                                    // DidUndoTransaction
  friend class
      WhiteSpaceVisibilityKeeper;  // AutoMoveOneLineHandler
                                   // CanMoveChildren,
                                   // ChangeListElementType,
                                   // DeleteNodeWithTransaction,
                                   // DeleteTextAndTextNodesWithTransaction,
                                   // InsertLineBreak,
                                   // JoinNearestEditableNodesWithTransaction,
                                   // LineBreakType,
                                   // MoveChildrenWithTransaction,
                                   // SplitAncestorStyledInlineElementsAt,
                                   // TreatEmptyTextNodes
};

/**
 * ListElementSelectionState class gets which list element is selected right
 * now.
 */
class MOZ_STACK_CLASS ListElementSelectionState final {
 public:
  ListElementSelectionState() = delete;
  ListElementSelectionState(HTMLEditor& aHTMLEditor, ErrorResult& aRv);

  bool IsOLElementSelected() const { return mIsOLElementSelected; }
  bool IsULElementSelected() const { return mIsULElementSelected; }
  bool IsDLElementSelected() const { return mIsDLElementSelected; }
  bool IsNotOneTypeListElementSelected() const {
    return (mIsOLElementSelected + mIsULElementSelected + mIsDLElementSelected +
            mIsOtherContentSelected) > 1;
  }

 private:
  bool mIsOLElementSelected = false;
  bool mIsULElementSelected = false;
  bool mIsDLElementSelected = false;
  bool mIsOtherContentSelected = false;
};

/**
 * ListItemElementSelectionState class gets which list item element is selected
 * right now.
 */
class MOZ_STACK_CLASS ListItemElementSelectionState final {
 public:
  ListItemElementSelectionState() = delete;
  ListItemElementSelectionState(HTMLEditor& aHTMLEditor, ErrorResult& aRv);

  bool IsLIElementSelected() const { return mIsLIElementSelected; }
  bool IsDTElementSelected() const { return mIsDTElementSelected; }
  bool IsDDElementSelected() const { return mIsDDElementSelected; }
  bool IsNotOneTypeDefinitionListItemElementSelected() const {
    return (mIsDTElementSelected + mIsDDElementSelected +
            mIsOtherElementSelected) > 1;
  }

 private:
  bool mIsLIElementSelected = false;
  bool mIsDTElementSelected = false;
  bool mIsDDElementSelected = false;
  bool mIsOtherElementSelected = false;
};

/**
 * AlignStateAtSelection class gets alignment at selection.
 * XXX This currently returns only first alignment.
 */
class MOZ_STACK_CLASS AlignStateAtSelection final {
 public:
  AlignStateAtSelection() = delete;
  MOZ_CAN_RUN_SCRIPT AlignStateAtSelection(HTMLEditor& aHTMLEditor,
                                           ErrorResult& aRv);

  nsIHTMLEditor::EAlignment AlignmentAtSelectionStart() const {
    return mFirstAlign;
  }
  bool IsSelectionRangesFound() const { return mFoundSelectionRanges; }

 private:
  nsIHTMLEditor::EAlignment mFirstAlign = nsIHTMLEditor::eLeft;
  bool mFoundSelectionRanges = false;
};

/**
 * ParagraphStateAtSelection class gets format block types around selection.
 */
class MOZ_STACK_CLASS ParagraphStateAtSelection final {
 public:
  using FormatBlockMode = HTMLEditor::FormatBlockMode;

  ParagraphStateAtSelection() = delete;
  /**
   * @param aFormatBlockMode    Whether HTML formatBlock command or XUL
   *                            paragraphState command.
   */
  ParagraphStateAtSelection(HTMLEditor& aHTMLEditor,
                            FormatBlockMode aFormatBlockMode, ErrorResult& aRv);

  /**
   * GetFirstParagraphStateAtSelection() returns:
   * - nullptr if there is no format blocks nor inline nodes.
   * - nsGkAtoms::_empty if first node is not in any format block.
   * - a tag name of format block at first node.
   * XXX See the private method explanations.  If selection ranges contains
   *     non-format block first, it'll be check after its siblings.  Therefore,
   *     this may return non-first paragraph state.
   */
  nsAtom* GetFirstParagraphStateAtSelection() const {
    return mIsMixed && mIsInDLElement ? nsGkAtoms::dl
                                      : mFirstParagraphState.get();
  }

  /**
   * If selected nodes are not in same format node nor only in no-format blocks,
   * this returns true.
   */
  bool IsMixed() const { return mIsMixed && !mIsInDLElement; }

 private:
  using EditorType = EditorBase::EditorType;

  [[nodiscard]] static bool IsFormatElement(FormatBlockMode aFormatBlockMode,
                                            const nsIContent& aContent);

  /**
   * AppendDescendantFormatNodesAndFirstInlineNode() appends descendant
   * format blocks and first inline child node in aNonFormatBlockElement to
   * the last of the array (not inserting where aNonFormatBlockElement is,
   * so that the node order becomes randomly).
   *
   * @param aArrayOfContents            [in/out] Found descendant format blocks
   *                                    and first inline node in each non-format
   *                                    block will be appended to this.
   * @param aFormatBlockMode            Whether HTML formatBlock command or XUL
   *                                    paragraphState command.
   * @param aNonFormatBlockElement      Must be a non-format block element.
   */
  static void AppendDescendantFormatNodesAndFirstInlineNode(
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
      FormatBlockMode aFormatBlockMode, dom::Element& aNonFormatBlockElement);

  /**
   * CollectEditableFormatNodesInSelection() collects only editable nodes
   * around selection ranges (with
   * AutoClonedRangeArray::ExtendRangesToWrapLines() and
   * HTMLEditor::CollectEditTargetNodes(), see its document for the detail).
   * If it includes list, list item or table related elements, they will be
   * replaced their children.
   *
   * @param aFormatBlockMode            Whether HTML formatBlock command or XUL
   *                                    paragraphState command.
   */
  static nsresult CollectEditableFormatNodesInSelection(
      HTMLEditor& aHTMLEditor, FormatBlockMode aFormatBlockMode,
      const dom::Element& aEditingHost,
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents);

  RefPtr<nsAtom> mFirstParagraphState;
  bool mIsInDLElement = false;
  bool mIsMixed = false;
};

}  // namespace mozilla

mozilla::HTMLEditor* nsIEditor::AsHTMLEditor() {
  MOZ_DIAGNOSTIC_ASSERT(IsHTMLEditor());
  return static_cast<mozilla::HTMLEditor*>(this);
}

const mozilla::HTMLEditor* nsIEditor::AsHTMLEditor() const {
  MOZ_DIAGNOSTIC_ASSERT(IsHTMLEditor());
  return static_cast<const mozilla::HTMLEditor*>(this);
}

mozilla::HTMLEditor* nsIEditor::GetAsHTMLEditor() {
  return AsEditorBase()->IsHTMLEditor() ? AsHTMLEditor() : nullptr;
}

const mozilla::HTMLEditor* nsIEditor::GetAsHTMLEditor() const {
  return AsEditorBase()->IsHTMLEditor() ? AsHTMLEditor() : nullptr;
}

#endif  // #ifndef mozilla_HTMLEditor_h
