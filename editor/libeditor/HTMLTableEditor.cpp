/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include "HTMLEditor.h"
#include "HTMLEditorInlines.h"

#include "AutoSelectionRestorer.h"
#include "EditAction.h"
#include "EditorDOMPoint.h"
#include "EditorUtils.h"
#include "HTMLEditUtils.h"

#include "mozilla/Assertions.h"
#include "mozilla/FlushType.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/Range.h"
#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsAtom.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsISupportsUtils.h"
#include "nsITableCellLayout.h"  // For efficient access to table cell
#include "nsLiteralString.h"
#include "nsQueryFrame.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableWrapperFrame.h"
#include "nscore.h"
#include <algorithm>

namespace mozilla {

using namespace dom;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;

/**
 * Stack based helper class for restoring selection after table edit.
 */
class MOZ_STACK_CLASS AutoSelectionSetterAfterTableEdit final {
 private:
  const RefPtr<HTMLEditor> mHTMLEditor;
  const RefPtr<Element> mTable;
  int32_t mCol, mRow, mDirection, mSelected;

 public:
  AutoSelectionSetterAfterTableEdit(HTMLEditor& aHTMLEditor, Element* aTable,
                                    int32_t aRow, int32_t aCol,
                                    int32_t aDirection, bool aSelected)
      : mHTMLEditor(&aHTMLEditor),
        mTable(aTable),
        mCol(aCol),
        mRow(aRow),
        mDirection(aDirection),
        mSelected(aSelected) {}

  MOZ_CAN_RUN_SCRIPT ~AutoSelectionSetterAfterTableEdit() {
    if (mHTMLEditor) {
      mHTMLEditor->SetSelectionAfterTableEdit(mTable, mRow, mCol, mDirection,
                                              mSelected);
    }
  }
};

/******************************************************************************
 * HTMLEditor::CellIndexes
 ******************************************************************************/

void HTMLEditor::CellIndexes::Update(HTMLEditor& aHTMLEditor,
                                     Selection& /*aSelection*/) {
  // Guarantee the life time of the cell element since Init() will access
  // layout methods.
  RefPtr<Element> cellElement =
      aHTMLEditor.GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cellElement) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::td) "
        "failed");
    return;
  }

  RefPtr<PresShell> presShell{aHTMLEditor.GetPresShell()};
  Update(*cellElement, presShell);
}

void HTMLEditor::CellIndexes::Update(Element& aCellElement,
                                     PresShell* aPresShell) {
  // If the table cell is created immediately before this call, e.g., using
  // innerHTML, frames have not been created yet. Hence, flush layout to create
  // them.
  if (NS_WARN_IF(!aPresShell)) {
    return;
  }

  aPresShell->FlushPendingNotifications(FlushType::Frames);

  nsIFrame* frameOfCell = aCellElement.GetPrimaryFrame();
  if (!frameOfCell) {
    NS_WARNING("There was no layout information of aCellElement");
    return;
  }

  nsITableCellLayout* tableCellLayout = do_QueryFrame(frameOfCell);
  if (!tableCellLayout) {
    NS_WARNING("aCellElement was not a table cell");
    return;
  }

  if (NS_FAILED(tableCellLayout->GetCellIndexes(mRow, mColumn))) {
    NS_WARNING("nsITableCellLayout::GetCellIndexes() failed");
    mRow = mColumn = -1;
    return;
  }

  MOZ_ASSERT(!isErr());
}

/******************************************************************************
 * HTMLEditor::CellData
 ******************************************************************************/

// static
HTMLEditor::CellData HTMLEditor::CellData::AtIndexInTableElement(
    const HTMLEditor& /*aHTMLEditor*/, const Element& aTableElement,
    int32_t aRowIndex, int32_t aColumnIndex) {
  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(&aTableElement);
  if (!tableFrame) {
    NS_WARNING("There was no layout information of the table");
    return CellData::Error(aRowIndex, aColumnIndex);
  }

  // If there is no cell at the indexes.  Don't set the error state to the new
  // instance.
  nsTableCellFrame* cellFrame =
      tableFrame->GetCellFrameAt(aRowIndex, aColumnIndex);
  if (!cellFrame) {
    return CellData::NotFound(aRowIndex, aColumnIndex);
  }

  Element* cellElement = Element::FromNodeOrNull(cellFrame->GetContent());
  if (!cellElement) {
    return CellData::Error(aRowIndex, aColumnIndex);
  }
  return CellData(*cellElement, aRowIndex, aColumnIndex, *cellFrame,
                  *tableFrame);
}

HTMLEditor::CellData::CellData(Element& aElement, int32_t aRowIndex,
                               int32_t aColumnIndex,
                               nsTableCellFrame& aTableCellFrame,
                               nsTableWrapperFrame& aTableWrapperFrame)
    : mElement(&aElement),
      mCurrent(aRowIndex, aColumnIndex),
      mFirst(aTableCellFrame.RowIndex(), aTableCellFrame.ColIndex()),
      mRowSpan(aTableCellFrame.GetRowSpan()),
      mColSpan(aTableCellFrame.GetColSpan()),
      mEffectiveRowSpan(
          aTableWrapperFrame.GetEffectiveRowSpanAt(aRowIndex, aColumnIndex)),
      mEffectiveColSpan(
          aTableWrapperFrame.GetEffectiveColSpanAt(aRowIndex, aColumnIndex)),
      mIsSelected(aTableCellFrame.IsSelected()) {
  MOZ_ASSERT(!mCurrent.isErr());
}

/******************************************************************************
 * HTMLEditor::TableSize
 ******************************************************************************/

// static
Result<HTMLEditor::TableSize, nsresult> HTMLEditor::TableSize::Create(
    HTMLEditor& aHTMLEditor, Element& aTableOrElementInTable) {
  // Currently, nsTableWrapperFrame::GetRowCount() and
  // nsTableWrapperFrame::GetColCount() are safe to use without grabbing
  // <table> element.  However, editor developers may not watch layout API
  // changes.  So, for keeping us safer, we should use RefPtr here.
  RefPtr<Element> tableElement =
      aHTMLEditor.GetInclusiveAncestorByTagNameInternal(*nsGkAtoms::table,
                                                        aTableOrElementInTable);
  if (!tableElement) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
        "failed");
    return Err(NS_ERROR_FAILURE);
  }
  nsTableWrapperFrame* tableFrame =
      do_QueryFrame(tableElement->GetPrimaryFrame());
  if (!tableFrame) {
    NS_WARNING("There was no layout information of the <table> element");
    return Err(NS_ERROR_FAILURE);
  }
  const int32_t rowCount = tableFrame->GetRowCount();
  const int32_t columnCount = tableFrame->GetColCount();
  if (NS_WARN_IF(rowCount < 0) || NS_WARN_IF(columnCount < 0)) {
    return Err(NS_ERROR_FAILURE);
  }
  return TableSize(rowCount, columnCount);
}

/******************************************************************************
 * HTMLEditor
 ******************************************************************************/

nsresult HTMLEditor::InsertCell(Element* aCell, int32_t aRowSpan,
                                int32_t aColSpan, bool aAfter, bool aIsHeader,
                                Element** aNewCell) {
  if (aNewCell) {
    *aNewCell = nullptr;
  }

  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  // And the parent and offsets needed to do an insert
  EditorDOMPoint pointToInsert(aCell);
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<Element> newCell =
      CreateElementWithDefaults(aIsHeader ? *nsGkAtoms::th : *nsGkAtoms::td);
  if (!newCell) {
    NS_WARNING(
        "HTMLEditor::CreateElementWithDefaults(nsGkAtoms::th or td) failed");
    return NS_ERROR_FAILURE;
  }

  // Optional: return new cell created
  if (aNewCell) {
    *aNewCell = do_AddRef(newCell).take();
  }

  if (aRowSpan > 1) {
    // Note: Do NOT use editor transaction for this
    nsAutoString newRowSpan;
    newRowSpan.AppendInt(aRowSpan, 10);
    DebugOnly<nsresult> rvIgnored =
        newCell->SetAttr(kNameSpaceID_None, nsGkAtoms::rowspan, newRowSpan,
                         true, IsKnownNewAttr::No);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "Element::SetAttr(nsGkAtoms::rawspan) failed, but ignored");
  }
  if (aColSpan > 1) {
    // Note: Do NOT use editor transaction for this
    nsAutoString newColSpan;
    newColSpan.AppendInt(aColSpan, 10);
    DebugOnly<nsresult> rvIgnored =
        newCell->SetAttr(kNameSpaceID_None, nsGkAtoms::colspan, newColSpan,
                         true, IsKnownNewAttr::No);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "Element::SetAttr(nsGkAtoms::colspan) failed, but ignored");
  }
  if (aAfter) {
    DebugOnly<bool> advanced = pointToInsert.AdvanceOffset();
    NS_WARNING_ASSERTION(advanced,
                         "Failed to advance offset to after the old cell");
  }

  // TODO: Remove AutoTransactionsConserveSelection here.  It's not necessary
  //       in normal cases.  However, it may be required for nested edit
  //       actions which may be caused by legacy mutation event listeners or
  //       chrome script.
  AutoTransactionsConserveSelection dontChangeSelection(*this);
  Result<CreateElementResult, nsresult> insertNewCellResult =
      InsertNodeWithTransaction<Element>(*newCell, pointToInsert);
  if (MOZ_UNLIKELY(insertNewCellResult.isErr())) {
    NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
    return insertNewCellResult.unwrapErr();
  }
  // Because of dontChangeSelection, we've never allowed to transactions to
  // update selection here.
  insertNewCellResult.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

nsresult HTMLEditor::SetColSpan(Element* aCell, int32_t aColSpan) {
  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoString newSpan;
  newSpan.AppendInt(aColSpan, 10);
  nsresult rv =
      SetAttributeWithTransaction(*aCell, *nsGkAtoms::colspan, newSpan);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::SetAttributeWithTransaction(nsGkAtoms::colspan) failed");
  return rv;
}

nsresult HTMLEditor::SetRowSpan(Element* aCell, int32_t aRowSpan) {
  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoString newSpan;
  newSpan.AppendInt(aRowSpan, 10);
  nsresult rv =
      SetAttributeWithTransaction(*aCell, *nsGkAtoms::rowspan, newSpan);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::SetAttributeWithTransaction(nsGkAtoms::rowspan) failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::InsertTableCell(int32_t aNumberOfCellsToInsert,
                                          bool aInsertAfterSelectedCell) {
  if (aNumberOfCellsToInsert <= 0) {
    return NS_OK;  // Just do nothing.
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertTableCellElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<RefPtr<Element>, nsresult> cellElementOrError =
      GetFirstSelectedCellElementInTable();
  if (cellElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetFirstSelectedCellElementInTable() failed");
    return EditorBase::ToGenericNSResult(cellElementOrError.unwrapErr());
  }

  if (!cellElementOrError.inspect()) {
    return NS_OK;
  }

  EditorDOMPoint pointToInsert(cellElementOrError.inspect());
  if (!pointToInsert.IsSet()) {
    NS_WARNING("Found an orphan cell element");
    return NS_ERROR_FAILURE;
  }
  if (aInsertAfterSelectedCell && !pointToInsert.IsEndOfContainer()) {
    DebugOnly<bool> advanced = pointToInsert.AdvanceOffset();
    NS_WARNING_ASSERTION(
        advanced,
        "Failed to set insertion point after current cell, but ignored");
  }
  Result<CreateElementResult, nsresult> insertCellElementResult =
      InsertTableCellsWithTransaction(pointToInsert, aNumberOfCellsToInsert);
  if (MOZ_UNLIKELY(insertCellElementResult.isErr())) {
    NS_WARNING("HTMLEditor::InsertTableCellsWithTransaction() failed");
    return EditorBase::ToGenericNSResult(insertCellElementResult.unwrapErr());
  }
  // We don't need to modify selection here.
  insertCellElementResult.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

Result<CreateElementResult, nsresult>
HTMLEditor::InsertTableCellsWithTransaction(
    const EditorDOMPoint& aPointToInsert, int32_t aNumberOfCellsToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());
  MOZ_ASSERT(aNumberOfCellsToInsert > 0);

  if (!HTMLEditUtils::IsTableRowElement(
          aPointToInsert.GetContainerAs<nsIContent>())) {
    NS_WARNING("Tried to insert cell elements to non-<tr> element");
    return Err(NS_ERROR_FAILURE);
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of BR in new cell until we're done
  // XXX Why? I think that we should insert <br> element for every cell
  //     **before** inserting new cell into the <tr> element.
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(error.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");
  error.SuppressException();

  // Put caret into the cell before the first inserting cell, or the first
  // table cell in the row.
  RefPtr<Element> cellToPutCaret =
      aPointToInsert.IsEndOfContainer()
          ? nullptr
          : HTMLEditUtils::GetPreviousTableCellElementSibling(
                *aPointToInsert.GetChild());

  RefPtr<Element> firstCellElement, lastCellElement;
  nsresult rv = [&]() MOZ_CAN_RUN_SCRIPT {
    // TODO: Remove AutoTransactionsConserveSelection here.  It's not necessary
    //       in normal cases.  However, it may be required for nested edit
    //       actions which may be caused by legacy mutation event listeners or
    //       chrome script.
    AutoTransactionsConserveSelection dontChangeSelection(*this);

    // Block legacy mutation events for making this job simpler.
    nsAutoScriptBlockerSuppressNodeRemoved blockToRunScript;

    // If there is a child to put a cell, we need to put all cell elements
    // before it.  Therefore, creating `EditorDOMPoint` with the child element
    // is safe.  Otherwise, we need to try to append cell elements in the row.
    // Therefore, using `EditorDOMPoint::AtEndOf()` is safe.  Note that it's
    // not safe to creat it once because the offset and child relation in the
    // point becomes invalid after inserting a cell element.
    nsIContent* referenceContent = aPointToInsert.GetChild();
    for ([[maybe_unused]] const auto i :
         IntegerRange<uint32_t>(aNumberOfCellsToInsert)) {
      RefPtr<Element> newCell = CreateElementWithDefaults(*nsGkAtoms::td);
      if (!newCell) {
        NS_WARNING(
            "HTMLEditor::CreateElementWithDefaults(nsGkAtoms::td) failed");
        return NS_ERROR_FAILURE;
      }
      Result<CreateElementResult, nsresult> insertNewCellResult =
          InsertNodeWithTransaction(
              *newCell, referenceContent
                            ? EditorDOMPoint(referenceContent)
                            : EditorDOMPoint::AtEndOf(
                                  *aPointToInsert.ContainerAs<Element>()));
      if (MOZ_UNLIKELY(insertNewCellResult.isErr())) {
        NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
        return insertNewCellResult.unwrapErr();
      }
      CreateElementResult unwrappedInsertNewCellResult =
          insertNewCellResult.unwrap();
      lastCellElement = unwrappedInsertNewCellResult.UnwrapNewNode();
      if (!firstCellElement) {
        firstCellElement = lastCellElement;
      }
      // Because of dontChangeSelection, we've never allowed to transactions
      // to update selection here.
      unwrappedInsertNewCellResult.IgnoreCaretPointSuggestion();
      if (!cellToPutCaret) {
        cellToPutCaret = std::move(newCell);  // This is first cell in the row.
      }
    }

    // TODO: Stop touching selection here.
    MOZ_ASSERT(cellToPutCaret);
    MOZ_ASSERT(cellToPutCaret->GetParent());
    CollapseSelectionToDeepestNonTableFirstChild(cellToPutCaret);
    return NS_OK;
  }();
  if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED ||
                   NS_WARN_IF(Destroyed()))) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    return Err(rv);
  }
  MOZ_ASSERT(firstCellElement);
  MOZ_ASSERT(lastCellElement);
  return CreateElementResult(std::move(firstCellElement),
                             EditorDOMPoint(lastCellElement, 0u));
}

NS_IMETHODIMP HTMLEditor::GetFirstRow(Element* aTableOrElementInTable,
                                      Element** aFirstRowElement) {
  if (NS_WARN_IF(!aTableOrElementInTable) || NS_WARN_IF(!aFirstRowElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetFirstRow);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetFirstRow() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<RefPtr<Element>, nsresult> firstRowElementOrError =
      GetFirstTableRowElement(*aTableOrElementInTable);
  NS_WARNING_ASSERTION(!firstRowElementOrError.isErr(),
                       "HTMLEditor::GetFirstTableRowElement() failed");
  if (firstRowElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetFirstTableRowElement() failed");
    return EditorBase::ToGenericNSResult(firstRowElementOrError.unwrapErr());
  }
  firstRowElementOrError.unwrap().forget(aFirstRowElement);
  return NS_OK;
}

Result<RefPtr<Element>, nsresult> HTMLEditor::GetFirstTableRowElement(
    const Element& aTableOrElementInTable) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Element* tableElement = GetInclusiveAncestorByTagNameInternal(
      *nsGkAtoms::table, aTableOrElementInTable);
  // If the element is not in <table>, return error.
  if (!tableElement) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
        "failed");
    return Err(NS_ERROR_FAILURE);
  }

  for (nsIContent* tableChild = tableElement->GetFirstChild(); tableChild;
       tableChild = tableChild->GetNextSibling()) {
    if (tableChild->IsHTMLElement(nsGkAtoms::tr)) {
      // Found a row directly under <table>
      return RefPtr<Element>(tableChild->AsElement());
    }
    // <table> can have table section elements like <tbody>.  <tr> elements
    // may be children of them.
    if (tableChild->IsAnyOfHTMLElements(nsGkAtoms::tbody, nsGkAtoms::thead,
                                        nsGkAtoms::tfoot)) {
      for (nsIContent* tableSectionChild = tableChild->GetFirstChild();
           tableSectionChild;
           tableSectionChild = tableSectionChild->GetNextSibling()) {
        if (tableSectionChild->IsHTMLElement(nsGkAtoms::tr)) {
          return RefPtr<Element>(tableSectionChild->AsElement());
        }
      }
    }
  }
  // Don't return error when there is no <tr> element in the <table>.
  return RefPtr<Element>();
}

Result<RefPtr<Element>, nsresult> HTMLEditor::GetNextTableRowElement(
    const Element& aTableRowElement) const {
  if (NS_WARN_IF(!aTableRowElement.IsHTMLElement(nsGkAtoms::tr))) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  for (nsIContent* maybeNextRow = aTableRowElement.GetNextSibling();
       maybeNextRow; maybeNextRow = maybeNextRow->GetNextSibling()) {
    if (maybeNextRow->IsHTMLElement(nsGkAtoms::tr)) {
      return RefPtr<Element>(maybeNextRow->AsElement());
    }
  }

  // In current table section (e.g., <tbody>), there is no <tr> element.
  // Then, check the following table sections.
  Element* parentElementOfRow = aTableRowElement.GetParentElement();
  if (!parentElementOfRow) {
    NS_WARNING("aTableRowElement was an orphan node");
    return Err(NS_ERROR_FAILURE);
  }

  // Basically, <tr> elements should be in table section elements even if
  // they are not written in the source explicitly.  However, for preventing
  // cross table boundary, check it now.
  if (parentElementOfRow->IsHTMLElement(nsGkAtoms::table)) {
    // Don't return error since this means just not found.
    return RefPtr<Element>();
  }

  for (nsIContent* maybeNextTableSection = parentElementOfRow->GetNextSibling();
       maybeNextTableSection;
       maybeNextTableSection = maybeNextTableSection->GetNextSibling()) {
    // If the sibling of parent of given <tr> is a table section element,
    // check its children.
    if (maybeNextTableSection->IsAnyOfHTMLElements(
            nsGkAtoms::tbody, nsGkAtoms::thead, nsGkAtoms::tfoot)) {
      for (nsIContent* maybeNextRow = maybeNextTableSection->GetFirstChild();
           maybeNextRow; maybeNextRow = maybeNextRow->GetNextSibling()) {
        if (maybeNextRow->IsHTMLElement(nsGkAtoms::tr)) {
          return RefPtr<Element>(maybeNextRow->AsElement());
        }
      }
    }
    // I'm not sure whether this is a possible case since table section
    // elements are created automatically.  However, DOM API may create
    // <tr> elements without table section elements.  So, let's check it.
    else if (maybeNextTableSection->IsHTMLElement(nsGkAtoms::tr)) {
      return RefPtr<Element>(maybeNextTableSection->AsElement());
    }
  }
  // Don't return error when the given <tr> element is the last <tr> element in
  // the <table>.
  return RefPtr<Element>();
}

NS_IMETHODIMP HTMLEditor::InsertTableColumn(int32_t aNumberOfColumnsToInsert,
                                            bool aInsertAfterSelectedCell) {
  if (aNumberOfColumnsToInsert <= 0) {
    return NS_OK;  // XXX Traditional behavior
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertTableColumn);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<RefPtr<Element>, nsresult> cellElementOrError =
      GetFirstSelectedCellElementInTable();
  if (cellElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetFirstSelectedCellElementInTable() failed");
    return EditorBase::ToGenericNSResult(cellElementOrError.unwrapErr());
  }

  if (!cellElementOrError.inspect()) {
    return NS_OK;
  }

  EditorDOMPoint pointToInsert(cellElementOrError.inspect());
  if (!pointToInsert.IsSet()) {
    NS_WARNING("Found an orphan cell element");
    return NS_ERROR_FAILURE;
  }
  if (aInsertAfterSelectedCell && !pointToInsert.IsEndOfContainer()) {
    DebugOnly<bool> advanced = pointToInsert.AdvanceOffset();
    NS_WARNING_ASSERTION(
        advanced,
        "Failed to set insertion point after current cell, but ignored");
  }
  rv = InsertTableColumnsWithTransaction(pointToInsert,
                                         aNumberOfColumnsToInsert);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::InsertTableColumnsWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::InsertTableColumnsWithTransaction(
    const EditorDOMPoint& aPointToInsert, int32_t aNumberOfColumnsToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());
  MOZ_ASSERT(aNumberOfColumnsToInsert > 0);

  const RefPtr<PresShell> presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!aPointToInsert.IsInContentNode()) ||
      NS_WARN_IF(!HTMLEditUtils::IsTableRowElement(
          *aPointToInsert.ContainerAs<nsIContent>()))) {
    return NS_ERROR_FAILURE;
  }

  const RefPtr<Element> tableElement =
      HTMLEditUtils::GetClosestAncestorTableElement(
          *aPointToInsert.ContainerAs<Element>());
  if (!tableElement) {
    NS_WARNING("There was no ancestor <table> element");
    return NS_ERROR_FAILURE;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *tableElement);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();
  if (NS_WARN_IF(tableSize.IsEmpty())) {
    return NS_ERROR_FAILURE;  // We cannot handle it in an empty table
  }

  // If aPointToInsert points non-cell element or end of the row, it means that
  // the caller wants to insert column immediately after the last cell of
  // the pointing cell element or in the raw.
  const bool insertAfterPreviousCell = [&]() {
    if (HTMLEditUtils::IsTableCellElement(aPointToInsert.GetChild())) {
      return false;  // Insert before the cell element.
    }
    // There is a previous cell element, we should add a column after it.
    Element* previousCellElement =
        aPointToInsert.IsEndOfContainer()
            ? HTMLEditUtils::GetLastTableCellElementChild(
                  *aPointToInsert.ContainerAs<Element>())
            : HTMLEditUtils::GetPreviousTableCellElementSibling(
                  *aPointToInsert.GetChild());
    return previousCellElement != nullptr;
  }();

  // Consider the column index in the table from given point and direction.
  auto referenceColumnIndexOrError =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<int32_t, nsresult> {
    if (!insertAfterPreviousCell) {
      if (aPointToInsert.IsEndOfContainer()) {
        return tableSize.mColumnCount;  // Empty row, append columns to the end
      }
      // Insert columns immediately before current column.
      const OwningNonNull<Element> tableCellElement =
          *aPointToInsert.GetChild()->AsElement();
      MOZ_ASSERT(HTMLEditUtils::IsTableCellElement(*tableCellElement));
      CellIndexes cellIndexes(*tableCellElement, presShell);
      if (NS_WARN_IF(cellIndexes.isErr())) {
        return Err(NS_ERROR_FAILURE);
      }
      return cellIndexes.mColumn;
    }

    // Otherwise, insert columns immediately after the previous column.
    RefPtr<Element> previousCellElement =
        aPointToInsert.IsEndOfContainer()
            ? HTMLEditUtils::GetLastTableCellElementChild(
                  *aPointToInsert.ContainerAs<Element>())
            : HTMLEditUtils::GetPreviousTableCellElementSibling(
                  *aPointToInsert.GetChild());
    MOZ_ASSERT(previousCellElement);
    CellIndexes cellIndexes(*previousCellElement, presShell);
    if (NS_WARN_IF(cellIndexes.isErr())) {
      return Err(NS_ERROR_FAILURE);
    }
    return cellIndexes.mColumn;
  }();
  if (MOZ_UNLIKELY(referenceColumnIndexOrError.isErr())) {
    return referenceColumnIndexOrError.unwrapErr();
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of <br> element in new cell until we're done.
  // XXX Why? We should put <br> element to every cell element before inserting
  //     the cells into the tree.
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");
  error.SuppressException();

  // Suppress Rules System selection munging.
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  // If we are inserting after all existing columns, make sure table is
  // "well formed" before appending new column.
  // XXX As far as I've tested, NormalizeTableInternal() always fails to
  //     normalize non-rectangular table.  So, the following CellData will
  //     fail if the table is not rectangle.
  if (referenceColumnIndexOrError.inspect() >= tableSize.mColumnCount) {
    DebugOnly<nsresult> rv = NormalizeTableInternal(*tableElement);
    if (MOZ_UNLIKELY(Destroyed())) {
      NS_WARNING(
          "HTMLEditor::NormalizeTableInternal() caused destroying the editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::NormalizeTableInternal() failed, but ignored");
  }

  // First, we should collect all reference nodes to insert new table cells.
  AutoTArray<CellData, 32> arrayOfCellData;
  {
    arrayOfCellData.SetCapacity(tableSize.mRowCount);
    for (const int32_t rowIndex : IntegerRange(tableSize.mRowCount)) {
      const auto cellData = CellData::AtIndexInTableElement(
          *this, *tableElement, rowIndex,
          referenceColumnIndexOrError.inspect());
      if (NS_WARN_IF(cellData.FailedOrNotFound())) {
        return NS_ERROR_FAILURE;
      }
      arrayOfCellData.AppendElement(cellData);
    }
  }

  // Note that checking whether the editor destroyed or not should be done
  // after inserting all cell elements.  Otherwise, the table is left as
  // not a rectangle.
  auto cellElementToPutCaretOrError =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<RefPtr<Element>, nsresult> {
    // Block legacy mutation events for making this job simpler.
    nsAutoScriptBlockerSuppressNodeRemoved blockToRunScript;
    RefPtr<Element> cellElementToPutCaret;
    for (const CellData& cellData : arrayOfCellData) {
      // Don't fail entire process if we fail to find a cell (may fail just in
      // particular rows with < adequate cells per row).
      // XXX So, here wants to know whether the CellData actually failed
      //     above.  Fix this later.
      if (!cellData.mElement) {
        continue;
      }

      if ((!insertAfterPreviousCell && cellData.IsSpannedFromOtherColumn()) ||
          (insertAfterPreviousCell &&
           cellData.IsNextColumnSpannedFromOtherColumn())) {
        // If we have a cell spanning this location, simply increase its
        // colspan to keep table rectangular.
        if (cellData.mColSpan > 0) {
          DebugOnly<nsresult> rvIgnored = SetColSpan(
              cellData.mElement, cellData.mColSpan + aNumberOfColumnsToInsert);
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                               "HTMLEditor::SetColSpan() failed, but ignored");
        }
        continue;
      }

      EditorDOMPoint pointToInsert = [&]() {
        if (!insertAfterPreviousCell) {
          // Insert before the reference cell.
          return EditorDOMPoint(cellData.mElement);
        }
        if (!cellData.mElement->GetNextSibling()) {
          // Insert after the reference cell, but nothing follows it, append
          // to the end of the row.
          return EditorDOMPoint::AtEndOf(*cellData.mElement->GetParentNode());
        }
        // Otherwise, returns immediately before the next sibling.  Note that
        // the next sibling may not be a table cell element.  E.g., it may be
        // a text node containing only white-spaces in most cases.
        return EditorDOMPoint(cellData.mElement->GetNextSibling());
      }();
      if (NS_WARN_IF(!pointToInsert.IsInContentNode())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      Result<CreateElementResult, nsresult> insertCellElementsResult =
          InsertTableCellsWithTransaction(pointToInsert,
                                          aNumberOfColumnsToInsert);
      if (MOZ_UNLIKELY(insertCellElementsResult.isErr())) {
        NS_WARNING("HTMLEditor::InsertTableCellsWithTransaction() failed");
        return insertCellElementsResult.propagateErr();
      }
      CreateElementResult unwrappedInsertCellElementsResult =
          insertCellElementsResult.unwrap();
      // We'll update selection later into the first inserted cell element in
      // the current row.
      unwrappedInsertCellElementsResult.IgnoreCaretPointSuggestion();
      if (pointToInsert.ContainerAs<Element>() ==
          aPointToInsert.ContainerAs<Element>()) {
        cellElementToPutCaret =
            unwrappedInsertCellElementsResult.UnwrapNewNode();
        MOZ_ASSERT(cellElementToPutCaret);
        MOZ_ASSERT(HTMLEditUtils::IsTableCellElement(*cellElementToPutCaret));
      }
    }
    return cellElementToPutCaret;
  }();
  if (MOZ_UNLIKELY(cellElementToPutCaretOrError.isErr())) {
    return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED
                                   : cellElementToPutCaretOrError.unwrapErr();
  }
  const RefPtr<Element> cellElementToPutCaret =
      cellElementToPutCaretOrError.unwrap();
  NS_WARNING_ASSERTION(
      cellElementToPutCaret,
      "Didn't find the first inserted cell element in the specified row");
  if (MOZ_LIKELY(cellElementToPutCaret)) {
    CollapseSelectionToDeepestNonTableFirstChild(cellElementToPutCaret);
  }
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

NS_IMETHODIMP HTMLEditor::InsertTableRow(int32_t aNumberOfRowsToInsert,
                                         bool aInsertAfterSelectedCell) {
  if (aNumberOfRowsToInsert <= 0) {
    return NS_OK;
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertTableRowElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  Result<RefPtr<Element>, nsresult> cellElementOrError =
      GetFirstSelectedCellElementInTable();
  if (cellElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetFirstSelectedCellElementInTable() failed");
    return EditorBase::ToGenericNSResult(cellElementOrError.unwrapErr());
  }

  if (!cellElementOrError.inspect()) {
    return NS_OK;
  }

  rv = InsertTableRowsWithTransaction(
      MOZ_KnownLive(*cellElementOrError.inspect()), aNumberOfRowsToInsert,
      aInsertAfterSelectedCell ? InsertPosition::eAfterSelectedCell
                               : InsertPosition::eBeforeSelectedCell);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertTableRowsWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::InsertTableRowsWithTransaction(
    Element& aCellElement, int32_t aNumberOfRowsToInsert,
    InsertPosition aInsertPosition) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(HTMLEditUtils::IsTableCellElement(aCellElement));

  const RefPtr<PresShell> presShell = GetPresShell();
  if (MOZ_UNLIKELY(NS_WARN_IF(!presShell))) {
    return NS_ERROR_FAILURE;
  }

  if (MOZ_UNLIKELY(
          !HTMLEditUtils::IsTableRowElement(aCellElement.GetParentElement()))) {
    NS_WARNING("Tried to insert columns to non-<tr> element");
    return NS_ERROR_FAILURE;
  }

  const RefPtr<Element> tableElement =
      HTMLEditUtils::GetClosestAncestorTableElement(aCellElement);
  if (MOZ_UNLIKELY(!tableElement)) {
    return NS_OK;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *tableElement);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();
  // Should not be empty since we've already found a cell.
  MOZ_ASSERT(!tableSize.IsEmpty());

  const CellIndexes cellIndexes(aCellElement, presShell);
  if (NS_WARN_IF(cellIndexes.isErr())) {
    return NS_ERROR_FAILURE;
  }

  // Get more data for current cell in row we are inserting at because we need
  // rowspan.
  const auto cellData =
      CellData::AtIndexInTableElement(*this, *tableElement, cellIndexes);
  if (NS_WARN_IF(cellData.FailedOrNotFound())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(&aCellElement == cellData.mElement);

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of BR in new cell until we're done
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  struct ElementWithNewRowSpan final {
    const OwningNonNull<Element> mCellElement;
    const int32_t mNewRowSpan;

    ElementWithNewRowSpan(Element& aCellElement, int32_t aNewRowSpan)
        : mCellElement(aCellElement), mNewRowSpan(aNewRowSpan) {}
  };
  AutoTArray<ElementWithNewRowSpan, 16> cellElementsToModifyRowSpan;
  if (aInsertPosition == InsertPosition::eAfterSelectedCell &&
      !cellData.mRowSpan) {
    // Detect when user is adding after a rowspan=0 case.
    // Assume they want to stop the "0" behavior and really add a new row.
    // Thus we set the rowspan to its true value.
    cellElementsToModifyRowSpan.AppendElement(
        ElementWithNewRowSpan(aCellElement, cellData.mEffectiveRowSpan));
  }

  struct MOZ_STACK_CLASS TableRowData {
    RefPtr<Element> mElement;
    int32_t mNumberOfCellsInStartRow;
    int32_t mOffsetInTRElementToPutCaret;
  };
  const auto referenceRowDataOrError = [&]() -> Result<TableRowData, nsresult> {
    const int32_t startRowIndex =
        aInsertPosition == InsertPosition::eBeforeSelectedCell
            ? cellData.mCurrent.mRow
            : cellData.mCurrent.mRow + cellData.mEffectiveRowSpan;
    if (startRowIndex < tableSize.mRowCount) {
      // We are inserting above an existing row.  Get each cell in the insert
      // row to adjust for rowspan effects while we count how many cells are
      // needed.
      RefPtr<Element> referenceRowElement;
      int32_t numberOfCellsInStartRow = 0;
      int32_t offsetInTRElementToPutCaret = 0;
      for (int32_t colIndex = 0;;) {
        const auto cellDataInStartRow = CellData::AtIndexInTableElement(
            *this, *tableElement, startRowIndex, colIndex);
        if (cellDataInStartRow.FailedOrNotFound()) {
          break;  // Perhaps, we reach end of the row.
        }

        // XXX So, this is impossible case. Will be removed.
        if (!cellDataInStartRow.mElement) {
          NS_WARNING("CellData::Update() succeeded, but didn't set mElement");
          break;
        }

        if (cellDataInStartRow.IsSpannedFromOtherRow()) {
          // We have a cell spanning this location.  Increase its rowspan.
          // Note that if rowspan is 0, we do nothing since that cell should
          // automatically extend into the new row.
          if (cellDataInStartRow.mRowSpan > 0) {
            cellElementsToModifyRowSpan.AppendElement(ElementWithNewRowSpan(
                *cellDataInStartRow.mElement,
                cellDataInStartRow.mRowSpan + aNumberOfRowsToInsert));
          }
          colIndex = cellDataInStartRow.NextColumnIndex();
          continue;
        }

        if (colIndex < cellDataInStartRow.mCurrent.mColumn) {
          offsetInTRElementToPutCaret++;
        }

        numberOfCellsInStartRow += cellDataInStartRow.mEffectiveColSpan;
        if (!referenceRowElement) {
          if (Element* maybeTableRowElement =
                  cellDataInStartRow.mElement->GetParentElement()) {
            if (HTMLEditUtils::IsTableRowElement(*maybeTableRowElement)) {
              referenceRowElement = maybeTableRowElement;
            }
          }
        }
        MOZ_ASSERT(colIndex < cellDataInStartRow.NextColumnIndex());
        colIndex = cellDataInStartRow.NextColumnIndex();
      }
      if (MOZ_UNLIKELY(!referenceRowElement)) {
        NS_WARNING(
            "Reference row element to insert new row elements was not found");
        return Err(NS_ERROR_FAILURE);
      }
      return TableRowData{std::move(referenceRowElement),
                          numberOfCellsInStartRow, offsetInTRElementToPutCaret};
    }

    // We are adding a new row after all others.  If it weren't for colspan=0
    // effect,  we could simply use tableSize.mColumnCount for number of new
    // cells...
    // XXX colspan=0 support has now been removed in table layout so maybe this
    //     can be cleaned up now? (bug 1243183)
    int32_t numberOfCellsInStartRow = tableSize.mColumnCount;
    int32_t offsetInTRElementToPutCaret = 0;

    // but we must compensate for all cells with rowspan = 0 in the last row.
    const int32_t lastRowIndex = tableSize.mRowCount - 1;
    for (int32_t colIndex = 0;;) {
      const auto cellDataInLastRow = CellData::AtIndexInTableElement(
          *this, *tableElement, lastRowIndex, colIndex);
      if (cellDataInLastRow.FailedOrNotFound()) {
        break;  // Perhaps, we reach end of the row.
      }

      if (!cellDataInLastRow.mRowSpan) {
        MOZ_ASSERT(numberOfCellsInStartRow >=
                   cellDataInLastRow.mEffectiveColSpan);
        numberOfCellsInStartRow -= cellDataInLastRow.mEffectiveColSpan;
      } else if (colIndex < cellDataInLastRow.mCurrent.mColumn) {
        offsetInTRElementToPutCaret++;
      }
      MOZ_ASSERT(colIndex < cellDataInLastRow.NextColumnIndex());
      colIndex = cellDataInLastRow.NextColumnIndex();
    }
    return TableRowData{nullptr, numberOfCellsInStartRow,
                        offsetInTRElementToPutCaret};
  }();
  if (MOZ_UNLIKELY(referenceRowDataOrError.isErr())) {
    return referenceRowDataOrError.inspectErr();
  }

  const TableRowData& referenceRowData = referenceRowDataOrError.inspect();
  if (MOZ_UNLIKELY(!referenceRowData.mNumberOfCellsInStartRow)) {
    NS_WARNING("There was no cell element in the row");
    return NS_OK;
  }

  MOZ_ASSERT_IF(referenceRowData.mElement,
                HTMLEditUtils::IsTableRowElement(*referenceRowData.mElement));
  if (NS_WARN_IF(
          !HTMLEditUtils::IsTableRowElement(aCellElement.GetParentElement()))) {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }

  // The row parent and offset where we will insert new row.
  EditorDOMPoint pointToInsert = [&]() {
    if (aInsertPosition == InsertPosition::eBeforeSelectedCell) {
      MOZ_ASSERT(referenceRowData.mElement);
      return EditorDOMPoint(referenceRowData.mElement);
    }
    // Look for the last row element in the same table section or immediately
    // before the reference row element.  Then, we can insert new rows
    // immediately after the given row element.
    Element* lastRowElement = nullptr;
    for (Element* rowElement = aCellElement.GetParentElement();
         rowElement && rowElement != referenceRowData.mElement;) {
      lastRowElement = rowElement;
      const Result<RefPtr<Element>, nsresult> nextRowElementOrError =
          GetNextTableRowElement(*rowElement);
      if (MOZ_UNLIKELY(nextRowElementOrError.isErr())) {
        NS_WARNING("HTMLEditor::GetNextTableRowElement() failed");
        return EditorDOMPoint();
      }
      rowElement = nextRowElementOrError.inspect();
    }
    MOZ_ASSERT(lastRowElement);
    return EditorDOMPoint::After(*lastRowElement);
  }();
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  // Note that checking whether the editor destroyed or not should be done
  // after inserting all cell elements.  Otherwise, the table is left as
  // not a rectangle.
  auto firstInsertedTRElementOrError =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<RefPtr<Element>, nsresult> {
    // Block legacy mutation events for making this job simpler.
    nsAutoScriptBlockerSuppressNodeRemoved blockToRunScript;

    // Suppress Rules System selection munging.
    AutoTransactionsConserveSelection dontChangeSelection(*this);

    for (const ElementWithNewRowSpan& cellElementAndNewRowSpan :
         cellElementsToModifyRowSpan) {
      DebugOnly<nsresult> rvIgnored =
          SetRowSpan(MOZ_KnownLive(cellElementAndNewRowSpan.mCellElement),
                     cellElementAndNewRowSpan.mNewRowSpan);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "HTMLEditor::SetRowSpan() failed, but ignored");
    }

    RefPtr<Element> firstInsertedTRElement;
    IgnoredErrorResult error;
    for ([[maybe_unused]] const int32_t rowIndex :
         Reversed(IntegerRange(aNumberOfRowsToInsert))) {
      // Create a new row
      RefPtr<Element> newRowElement = CreateElementWithDefaults(*nsGkAtoms::tr);
      if (!newRowElement) {
        NS_WARNING(
            "HTMLEditor::CreateElementWithDefaults(nsGkAtoms::tr) failed");
        return Err(NS_ERROR_FAILURE);
      }

      for ([[maybe_unused]] const int32_t i :
           IntegerRange(referenceRowData.mNumberOfCellsInStartRow)) {
        const RefPtr<Element> newCellElement =
            CreateElementWithDefaults(*nsGkAtoms::td);
        if (!newCellElement) {
          NS_WARNING(
              "HTMLEditor::CreateElementWithDefaults(nsGkAtoms::td) failed");
          return Err(NS_ERROR_FAILURE);
        }
        newRowElement->AppendChild(*newCellElement, error);
        if (error.Failed()) {
          NS_WARNING("nsINode::AppendChild() failed");
          return Err(error.StealNSResult());
        }
      }

      AutoEditorDOMPointChildInvalidator lockOffset(pointToInsert);
      Result<CreateElementResult, nsresult> insertNewRowResult =
          InsertNodeWithTransaction<Element>(*newRowElement, pointToInsert);
      if (MOZ_UNLIKELY(insertNewRowResult.isErr())) {
        if (insertNewRowResult.inspectErr() == NS_ERROR_EDITOR_DESTROYED) {
          NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
          return insertNewRowResult.propagateErr();
        }
        NS_WARNING(
            "EditorBase::InsertNodeWithTransaction() failed, but ignored");
      }
      firstInsertedTRElement = std::move(newRowElement);
      // We'll update selection later.
      insertNewRowResult.inspect().IgnoreCaretPointSuggestion();
    }
    return firstInsertedTRElement;
  }();
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (MOZ_UNLIKELY(firstInsertedTRElementOrError.isErr())) {
    return firstInsertedTRElementOrError.unwrapErr();
  }

  const OwningNonNull<Element> cellElementToPutCaret = [&]() {
    if (MOZ_LIKELY(firstInsertedTRElementOrError.inspect())) {
      EditorRawDOMPoint point(firstInsertedTRElementOrError.inspect(),
                              referenceRowData.mOffsetInTRElementToPutCaret);
      if (MOZ_LIKELY(point.IsSetAndValid()) &&
          MOZ_LIKELY(HTMLEditUtils::IsTableCellElement(point.GetChild()))) {
        return OwningNonNull<Element>(*point.GetChild()->AsElement());
      }
    }
    return OwningNonNull<Element>(aCellElement);
  }();
  CollapseSelectionToDeepestNonTableFirstChild(cellElementToPutCaret);
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

nsresult HTMLEditor::DeleteTableElementAndChildrenWithTransaction(
    Element& aTableElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Block selectionchange event.  It's enough to dispatch selectionchange
  // event immediately after removing the table element.
  {
    AutoHideSelectionChanges hideSelection(SelectionRef());

    // Select the <table> element after clear current selection.
    if (SelectionRef().RangeCount()) {
      ErrorResult error;
      SelectionRef().RemoveAllRanges(error);
      if (error.Failed()) {
        NS_WARNING("Selection::RemoveAllRanges() failed");
        return error.StealNSResult();
      }
    }

    RefPtr<dom::Range> range = dom::Range::Create(&aTableElement);
    ErrorResult error;
    range->SelectNode(aTableElement, error);
    if (error.Failed()) {
      NS_WARNING("Range::SelectNode() failed");
      return error.StealNSResult();
    }
    SelectionRef().AddRangeAndSelectFramesAndNotifyListeners(*range, error);
    if (error.Failed()) {
      NS_WARNING(
          "Selection::AddRangeAndSelectFramesAndNotifyListeners() failed");
      return error.StealNSResult();
    }

#ifdef DEBUG
    range = SelectionRef().GetRangeAt(0);
    MOZ_ASSERT(range);
    MOZ_ASSERT(range->GetStartContainer() == aTableElement.GetParent());
    MOZ_ASSERT(range->GetEndContainer() == aTableElement.GetParent());
    MOZ_ASSERT(range->GetChildAtStartOffset() == &aTableElement);
    MOZ_ASSERT(range->GetChildAtEndOffset() == aTableElement.GetNextSibling());
#endif  // #ifdef DEBUG
  }

  nsresult rv = DeleteSelectionAsSubAction(eNext, eStrip);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::DeleteSelectionAsSubAction(eNext, eStrip) failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::DeleteTable() {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eRemoveTableElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> table;
  rv = GetCellContext(getter_AddRefs(table), nullptr, nullptr, nullptr, nullptr,
                      nullptr);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (!table) {
    NS_WARNING("HTMLEditor::GetCellContext() didn't return <table> element");
    return NS_ERROR_FAILURE;
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = DeleteTableElementAndChildrenWithTransaction(*table);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::DeleteTableCell(int32_t aNumberOfCellsToDelete) {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eRemoveTableCellElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = DeleteTableCellWithTransaction(aNumberOfCellsToDelete);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::DeleteTableCellWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::DeleteTableCellWithTransaction(
    int32_t aNumberOfCellsToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;

  nsresult rv =
      GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                     nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return rv;
  }
  if (!table || !cell) {
    NS_WARNING(
        "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
    // Don't fail if we didn't find a table or cell.
    return NS_OK;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent rules testing until we're done
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  MOZ_ASSERT(SelectionRef().RangeCount());

  SelectedTableCellScanner scanner(SelectionRef());

  Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.unwrapErr();
  }
  // FYI: Cannot be a const reference because the row count will be updated
  TableSize tableSize = tableSizeOrError.unwrap();
  MOZ_ASSERT(!tableSize.IsEmpty());

  // If only one cell is selected or no cell is selected, remove cells
  // starting from the first selected cell or a cell containing first
  // selection range.
  if (!scanner.IsInTableCellSelectionMode() ||
      SelectionRef().RangeCount() == 1) {
    for (int32_t i = 0; i < aNumberOfCellsToDelete; i++) {
      nsresult rv =
          GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                         nullptr, &startRowIndex, &startColIndex);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::GetCellContext() failed");
        return rv;
      }
      if (!table || !cell) {
        NS_WARNING(
            "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
        // Don't fail if no cell found
        return NS_OK;
      }

      int32_t numberOfCellsInRow = GetNumberOfCellsInRow(*table, startRowIndex);
      NS_WARNING_ASSERTION(
          numberOfCellsInRow >= 0,
          "HTMLEditor::GetNumberOfCellsInRow() failed, but ignored");

      if (numberOfCellsInRow == 1) {
        // Remove <tr> or <table> if we're removing all cells in the row or
        // the table.
        if (tableSize.mRowCount == 1) {
          nsresult rv = DeleteTableElementAndChildrenWithTransaction(*table);
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() "
              "failed");
          return rv;
        }

        // We need to call DeleteSelectedTableRowsWithTransaction() to handle
        // cells with rowspan attribute.
        rv = DeleteSelectedTableRowsWithTransaction(1);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "HTMLEditor::DeleteSelectedTableRowsWithTransaction(1) failed");
          return rv;
        }

        // Adjust table rows simply.  In strictly speaking, we should
        // recompute table size with the latest layout information since
        // mutation event listener may have changed the DOM tree. However,
        // this is not in usual path of Firefox.  So, we can assume that
        // there are no mutation event listeners.
        MOZ_ASSERT(tableSize.mRowCount);
        tableSize.mRowCount--;
        continue;
      }

      // The setCaret object will call AutoSelectionSetterAfterTableEdit in its
      // destructor
      AutoSelectionSetterAfterTableEdit setCaret(
          *this, table, startRowIndex, startColIndex, ePreviousColumn, false);
      AutoTransactionsConserveSelection dontChangeSelection(*this);

      // XXX Removing cell element causes not adjusting colspan.
      rv = DeleteNodeWithTransaction(*cell);
      // If we fail, don't try to delete any more cells???
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
      // Note that we don't refer column number in this loop.  So, it must
      // be safe not to recompute table size since number of row is synced
      // above.
    }
    return NS_OK;
  }

  // When 2 or more cells are selected, ignore aNumberOfCellsToRemove and
  // remove all selected cells.
  const RefPtr<PresShell> presShell{GetPresShell()};
  // `MOZ_KnownLive(scanner.ElementsRef()[0])` is safe because scanner grabs
  // it until it's destroyed later.
  const CellIndexes firstCellIndexes(MOZ_KnownLive(scanner.ElementsRef()[0]),
                                     presShell);
  if (NS_WARN_IF(firstCellIndexes.isErr())) {
    return NS_ERROR_FAILURE;
  }
  startRowIndex = firstCellIndexes.mRow;
  startColIndex = firstCellIndexes.mColumn;

  // The setCaret object will call AutoSelectionSetterAfterTableEdit in its
  // destructor
  AutoSelectionSetterAfterTableEdit setCaret(
      *this, table, startRowIndex, startColIndex, ePreviousColumn, false);
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  bool checkToDeleteRow = true;
  bool checkToDeleteColumn = true;
  for (RefPtr<Element> selectedCellElement = scanner.GetFirstElement();
       selectedCellElement;) {
    if (checkToDeleteRow) {
      // Optimize to delete an entire row
      // Clear so we don't repeat AllCellsInRowSelected within the same row
      checkToDeleteRow = false;
      if (AllCellsInRowSelected(table, startRowIndex, tableSize.mColumnCount)) {
        // First, find the next cell in a different row to continue after we
        // delete this row.
        int32_t nextRow = startRowIndex;
        while (nextRow == startRowIndex) {
          selectedCellElement = scanner.GetNextElement();
          if (!selectedCellElement) {
            break;
          }
          const CellIndexes nextSelectedCellIndexes(*selectedCellElement,
                                                    presShell);
          if (NS_WARN_IF(nextSelectedCellIndexes.isErr())) {
            return NS_ERROR_FAILURE;
          }
          nextRow = nextSelectedCellIndexes.mRow;
          startColIndex = nextSelectedCellIndexes.mColumn;
        }
        if (tableSize.mRowCount == 1) {
          nsresult rv = DeleteTableElementAndChildrenWithTransaction(*table);
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() "
              "failed");
          return rv;
        }
        nsresult rv = DeleteTableRowWithTransaction(*table, startRowIndex);
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::DeleteTableRowWithTransaction() failed");
          return rv;
        }
        // Adjust table rows simply.  In strictly speaking, we should
        // recompute table size with the latest layout information since
        // mutation event listener may have changed the DOM tree. However,
        // this is not in usual path of Firefox.  So, we can assume that
        // there are no mutation event listeners.
        MOZ_ASSERT(tableSize.mRowCount);
        tableSize.mRowCount--;
        if (!selectedCellElement) {
          break;  // XXX Seems like a dead path
        }
        // For the next cell: Subtract 1 for row we deleted
        startRowIndex = nextRow - 1;
        // Set true since we know we will look at a new row next
        checkToDeleteRow = true;
        continue;
      }
    }

    if (checkToDeleteColumn) {
      // Optimize to delete an entire column
      // Clear this so we don't repeat AllCellsInColSelected within the same Col
      checkToDeleteColumn = false;
      if (AllCellsInColumnSelected(table, startColIndex,
                                   tableSize.mColumnCount)) {
        // First, find the next cell in a different column to continue after
        // we delete this column.
        int32_t nextCol = startColIndex;
        while (nextCol == startColIndex) {
          selectedCellElement = scanner.GetNextElement();
          if (!selectedCellElement) {
            break;
          }
          const CellIndexes nextSelectedCellIndexes(*selectedCellElement,
                                                    presShell);
          if (NS_WARN_IF(nextSelectedCellIndexes.isErr())) {
            return NS_ERROR_FAILURE;
          }
          startRowIndex = nextSelectedCellIndexes.mRow;
          nextCol = nextSelectedCellIndexes.mColumn;
        }
        // Delete all cells which belong to the column.
        nsresult rv = DeleteTableColumnWithTransaction(*table, startColIndex);
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::DeleteTableColumnWithTransaction() failed");
          return rv;
        }
        // Adjust table columns simply.  In strictly speaking, we should
        // recompute table size with the latest layout information since
        // mutation event listener may have changed the DOM tree. However,
        // this is not in usual path of Firefox.  So, we can assume that
        // there are no mutation event listeners.
        MOZ_ASSERT(tableSize.mColumnCount);
        tableSize.mColumnCount--;
        if (!selectedCellElement) {
          break;
        }
        // For the next cell, subtract 1 for col. deleted
        startColIndex = nextCol - 1;
        // Set true since we know we will look at a new column next
        checkToDeleteColumn = true;
        continue;
      }
    }

    nsresult rv = DeleteNodeWithTransaction(*selectedCellElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }

    selectedCellElement = scanner.GetNextElement();
    if (!selectedCellElement) {
      return NS_OK;
    }

    const CellIndexes nextCellIndexes(*selectedCellElement, presShell);
    if (NS_WARN_IF(nextCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    startRowIndex = nextCellIndexes.mRow;
    startColIndex = nextCellIndexes.mColumn;
    // When table cell is removed, table size of column may be changed.
    // For example, if there are 2 rows, one has 2 cells, the other has
    // 3 cells, tableSize.mColumnCount is 3.  When this removes a cell
    // in the latter row, mColumnCount should be come 2.  However, we
    // don't use mColumnCount in this loop, so, this must be okay for now.
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::DeleteTableCellContents() {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eDeleteTableCellContents);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = DeleteTableCellContentsWithTransaction();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::DeleteTableCellContentsWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::DeleteTableCellContentsWithTransaction() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv =
      GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                     nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return rv;
  }
  if (!cell) {
    NS_WARNING("HTMLEditor::GetCellContext() didn't return cell element");
    // Don't fail if no cell found.
    return NS_OK;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent rules testing until we're done
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Don't let Rules System change the selection
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  SelectedTableCellScanner scanner(SelectionRef());
  if (scanner.IsInTableCellSelectionMode()) {
    const RefPtr<PresShell> presShell{GetPresShell()};
    // `MOZ_KnownLive(scanner.ElementsRef()[0])` is safe because scanner
    // grabs it until it's destroyed later.
    const CellIndexes firstCellIndexes(MOZ_KnownLive(scanner.ElementsRef()[0]),
                                       presShell);
    if (NS_WARN_IF(firstCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    cell = scanner.ElementsRef()[0];
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }

  AutoSelectionSetterAfterTableEdit setCaret(
      *this, table, startRowIndex, startColIndex, ePreviousColumn, false);

  for (RefPtr<Element> selectedCellElement = std::move(cell);
       selectedCellElement; selectedCellElement = scanner.GetNextElement()) {
    DebugOnly<nsresult> rvIgnored =
        DeleteAllChildrenWithTransaction(*selectedCellElement);
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::DeleteAllChildrenWithTransaction() failed, but ignored");
    if (!scanner.IsInTableCellSelectionMode()) {
      break;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::DeleteTableColumn(int32_t aNumberOfColumnsToDelete) {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eRemoveTableColumn);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = DeleteSelectedTableColumnsWithTransaction(aNumberOfColumnsToDelete);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::DeleteSelectedTableColumnsWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::DeleteSelectedTableColumnsWithTransaction(
    int32_t aNumberOfColumnsToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv =
      GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                     nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return rv;
  }
  if (!table || !cell) {
    NS_WARNING(
        "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
    // Don't fail if no cell found.
    return NS_OK;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // Prevent rules testing until we're done
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Shortcut the case of deleting all columns in table
  if (!startColIndex && aNumberOfColumnsToDelete >= tableSize.mColumnCount) {
    nsresult rv = DeleteTableElementAndChildrenWithTransaction(*table);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() failed");
    return rv;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  SelectedTableCellScanner scanner(SelectionRef());
  if (scanner.IsInTableCellSelectionMode() && SelectionRef().RangeCount() > 1) {
    const RefPtr<PresShell> presShell{GetPresShell()};
    // `MOZ_KnownLive(scanner.ElementsRef()[0])` is safe because `scanner`
    // grabs it until it's destroyed later.
    const CellIndexes firstCellIndexes(MOZ_KnownLive(scanner.ElementsRef()[0]),
                                       presShell);
    if (NS_WARN_IF(firstCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }

  // We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(
      *this, table, startRowIndex, startColIndex, ePreviousRow, false);

  // If 2 or more cells are not selected, removing columns starting from
  // a column which contains first selection range.
  if (!scanner.IsInTableCellSelectionMode() ||
      SelectionRef().RangeCount() == 1) {
    int32_t columnCountToRemove = std::min(
        aNumberOfColumnsToDelete, tableSize.mColumnCount - startColIndex);
    for (int32_t i = 0; i < columnCountToRemove; i++) {
      nsresult rv = DeleteTableColumnWithTransaction(*table, startColIndex);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::DeleteTableColumnWithTransaction() failed");
        return rv;
      }
    }
    return NS_OK;
  }

  // If 2 or more cells are selected, remove all columns which contain selected
  // cells.  I.e., we ignore aNumberOfColumnsToDelete in this case.
  const RefPtr<PresShell> presShell{GetPresShell()};
  for (RefPtr<Element> selectedCellElement = scanner.GetFirstElement();
       selectedCellElement;) {
    if (selectedCellElement != scanner.ElementsRef()[0]) {
      const CellIndexes cellIndexes(*selectedCellElement, presShell);
      if (NS_WARN_IF(cellIndexes.isErr())) {
        return NS_ERROR_FAILURE;
      }
      startRowIndex = cellIndexes.mRow;
      startColIndex = cellIndexes.mColumn;
    }
    // Find the next cell in a different column
    // to continue after we delete this column
    int32_t nextCol = startColIndex;
    while (nextCol == startColIndex) {
      selectedCellElement = scanner.GetNextElement();
      if (!selectedCellElement) {
        break;
      }
      const CellIndexes cellIndexes(*selectedCellElement, presShell);
      if (NS_WARN_IF(cellIndexes.isErr())) {
        return NS_ERROR_FAILURE;
      }
      startRowIndex = cellIndexes.mRow;
      nextCol = cellIndexes.mColumn;
    }
    nsresult rv = DeleteTableColumnWithTransaction(*table, startColIndex);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteTableColumnWithTransaction() failed");
      return rv;
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::DeleteTableColumnWithTransaction(Element& aTableElement,
                                                      int32_t aColumnIndex) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  for (int32_t rowIndex = 0;; rowIndex++) {
    const auto cellData = CellData::AtIndexInTableElement(
        *this, aTableElement, rowIndex, aColumnIndex);
    // Failure means that there is no more row in the table.  In this case,
    // we shouldn't return error since we just reach the end of the table.
    // XXX Should distinguish whether CellData returns error or just not found
    //     later.
    if (cellData.FailedOrNotFound()) {
      return NS_OK;
    }

    // Find cells that don't start in column we are deleting.
    MOZ_ASSERT(cellData.mColSpan >= 0);
    if (cellData.IsSpannedFromOtherColumn() || cellData.mColSpan != 1) {
      // If we have a cell spanning this location, decrease its colspan to
      // keep table rectangular, but if colspan is 0, it'll be adjusted
      // automatically.
      if (cellData.mColSpan > 0) {
        NS_WARNING_ASSERTION(cellData.mColSpan > 1,
                             "colspan should be 2 or larger");
        DebugOnly<nsresult> rvIgnored =
            SetColSpan(cellData.mElement, cellData.mColSpan - 1);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "HTMLEditor::SetColSpan() failed, but ignored");
      }
      if (!cellData.IsSpannedFromOtherColumn()) {
        // Cell is in column to be deleted, but must have colspan > 1,
        // so delete contents of cell instead of cell itself (We must have
        // reset colspan above).
        DebugOnly<nsresult> rvIgnored =
            DeleteAllChildrenWithTransaction(*cellData.mElement);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "HTMLEditor::DeleteAllChildrenWithTransaction() "
                             "failed, but ignored");
      }
      // Skip rows which the removed cell spanned.
      rowIndex += cellData.NumberOfFollowingRows();
      continue;
    }

    // Delete the cell
    int32_t numberOfCellsInRow =
        GetNumberOfCellsInRow(aTableElement, cellData.mCurrent.mRow);
    NS_WARNING_ASSERTION(
        numberOfCellsInRow > 0,
        "HTMLEditor::GetNumberOfCellsInRow() failed, but ignored");
    if (numberOfCellsInRow != 1) {
      // If removing cell is not the last cell of the row, we can just remove
      // it.
      nsresult rv = DeleteNodeWithTransaction(*cellData.mElement);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
      // Skip rows which the removed cell spanned.
      rowIndex += cellData.NumberOfFollowingRows();
      continue;
    }

    // When the cell is the last cell in the row, remove the row instead.
    Element* parentRow = GetInclusiveAncestorByTagNameInternal(
        *nsGkAtoms::tr, *cellData.mElement);
    if (!parentRow) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::tr) "
          "failed");
      return NS_ERROR_FAILURE;
    }

    // Check if its the only row left in the table.  If so, we can delete
    // the table instead.
    const Result<TableSize, nsresult> tableSizeOrError =
        TableSize::Create(*this, aTableElement);
    if (NS_WARN_IF(tableSizeOrError.isErr())) {
      return tableSizeOrError.inspectErr();
    }
    const TableSize& tableSize = tableSizeOrError.inspect();

    if (tableSize.mRowCount == 1) {
      // We're deleting the last row.  So, let's remove the <table> now.
      nsresult rv = DeleteTableElementAndChildrenWithTransaction(aTableElement);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() failed");
      return rv;
    }

    // Delete the row by placing caret in cell we were to delete.  We need
    // to call DeleteTableRowWithTransaction() to handle cells with rowspan.
    nsresult rv =
        DeleteTableRowWithTransaction(aTableElement, cellData.mFirst.mRow);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteTableRowWithTransaction() failed");
      return rv;
    }

    // Note that we decrement rowIndex since a row was deleted.
    rowIndex--;
  }

  // Not reached because for (;;) loop never breaks.
}

NS_IMETHODIMP HTMLEditor::DeleteTableRow(int32_t aNumberOfRowsToDelete) {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eRemoveTableRowElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = DeleteSelectedTableRowsWithTransaction(aNumberOfRowsToDelete);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::DeleteSelectedTableRowsWithTransaction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::DeleteSelectedTableRowsWithTransaction(
    int32_t aNumberOfRowsToDelete) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv =
      GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                     nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return rv;
  }
  if (!table || !cell) {
    NS_WARNING(
        "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
    // Don't fail if no cell found.
    return NS_OK;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // Prevent rules testing until we're done
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Shortcut the case of deleting all rows in table
  if (!startRowIndex && aNumberOfRowsToDelete >= tableSize.mRowCount) {
    nsresult rv = DeleteTableElementAndChildrenWithTransaction(*table);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::DeleteTableElementAndChildrenWithTransaction() failed");
    return rv;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  SelectedTableCellScanner scanner(SelectionRef());
  if (scanner.IsInTableCellSelectionMode() && SelectionRef().RangeCount() > 1) {
    // Fetch indexes again - may be different for selected cells
    const RefPtr<PresShell> presShell{GetPresShell()};
    // `MOZ_KnownLive(scanner.ElementsRef()[0])` is safe because `scanner`
    // grabs it until it's destroyed later.
    const CellIndexes firstCellIndexes(MOZ_KnownLive(scanner.ElementsRef()[0]),
                                       presShell);
    if (NS_WARN_IF(firstCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }

  // We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(
      *this, table, startRowIndex, startColIndex, ePreviousRow, false);
  // Don't change selection during deletions
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  // XXX Perhaps, the following loops should collect <tr> elements to remove
  //     first, then, remove them from the DOM tree since mutation event
  //     listener may change the DOM tree during the loops.

  // If 2 or more cells are not selected, removing rows starting from
  // a row which contains first selection range.
  if (!scanner.IsInTableCellSelectionMode() ||
      SelectionRef().RangeCount() == 1) {
    int32_t rowCountToRemove =
        std::min(aNumberOfRowsToDelete, tableSize.mRowCount - startRowIndex);
    for (int32_t i = 0; i < rowCountToRemove; i++) {
      nsresult rv = DeleteTableRowWithTransaction(*table, startRowIndex);
      // If failed in current row, try the next
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::DeleteTableRowWithTransaction() failed, but trying "
            "next...");
        startRowIndex++;
      }
      // Check if there's a cell in the "next" row.
      cell = GetTableCellElementAt(*table, startRowIndex, startColIndex);
      if (!cell) {
        return NS_OK;
      }
    }
    return NS_OK;
  }

  // If 2 or more cells are selected, remove all rows which contain selected
  // cells.  I.e., we ignore aNumberOfRowsToDelete in this case.
  const RefPtr<PresShell> presShell{GetPresShell()};
  for (RefPtr<Element> selectedCellElement = scanner.GetFirstElement();
       selectedCellElement;) {
    if (selectedCellElement != scanner.ElementsRef()[0]) {
      const CellIndexes cellIndexes(*selectedCellElement, presShell);
      if (NS_WARN_IF(cellIndexes.isErr())) {
        return NS_ERROR_FAILURE;
      }
      startRowIndex = cellIndexes.mRow;
      startColIndex = cellIndexes.mColumn;
    }
    // Find the next cell in a different row
    // to continue after we delete this row
    int32_t nextRow = startRowIndex;
    while (nextRow == startRowIndex) {
      selectedCellElement = scanner.GetNextElement();
      if (!selectedCellElement) {
        break;
      }
      const CellIndexes cellIndexes(*selectedCellElement, presShell);
      if (NS_WARN_IF(cellIndexes.isErr())) {
        return NS_ERROR_FAILURE;
      }
      nextRow = cellIndexes.mRow;
      startColIndex = cellIndexes.mColumn;
    }
    // Delete the row containing selected cell(s).
    nsresult rv = DeleteTableRowWithTransaction(*table, startRowIndex);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteTableRowWithTransaction() failed");
      return rv;
    }
  }
  return NS_OK;
}

// Helper that doesn't batch or change the selection
nsresult HTMLEditor::DeleteTableRowWithTransaction(Element& aTableElement,
                                                   int32_t aRowIndex) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, aTableElement);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Prevent rules testing until we're done
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");
  error.SuppressException();

  // Scan through cells in row to do rowspan adjustments
  // Note that after we delete row, startRowIndex will point to the cells in
  // the next row to be deleted.

  // The list of cells we will change rowspan in and the new rowspan values
  // for each.
  struct MOZ_STACK_CLASS SpanCell final {
    RefPtr<Element> mElement;
    int32_t mNewRowSpanValue;

    SpanCell(Element* aSpanCellElement, int32_t aNewRowSpanValue)
        : mElement(aSpanCellElement), mNewRowSpanValue(aNewRowSpanValue) {}
  };
  AutoTArray<SpanCell, 10> spanCellArray;
  RefPtr<Element> cellInDeleteRow;
  int32_t columnIndex = 0;
  while (aRowIndex < tableSize.mRowCount &&
         columnIndex < tableSize.mColumnCount) {
    const auto cellData = CellData::AtIndexInTableElement(
        *this, aTableElement, aRowIndex, columnIndex);
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // XXX So, we should distinguish if CellDate returns error or just not
    //     found later.
    if (!cellData.mElement) {
      break;
    }

    // Compensate for cells that don't start or extend below the row we are
    // deleting.
    if (cellData.IsSpannedFromOtherRow()) {
      // If a cell starts in row above us, decrease its rowspan to keep table
      // rectangular but we don't need to do this if rowspan=0, since it will
      // be automatically adjusted.
      if (cellData.mRowSpan > 0) {
        // Build list of cells to change rowspan.  We can't do it now since
        // it upsets cell map, so we will do it after deleting the row.
        int32_t newRowSpanValue = std::max(cellData.NumberOfPrecedingRows(),
                                           cellData.NumberOfFollowingRows());
        spanCellArray.AppendElement(
            SpanCell(cellData.mElement, newRowSpanValue));
      }
    } else {
      if (cellData.mRowSpan > 1) {
        // Cell spans below row to delete, so we must insert new cells to
        // keep rows below.  Note that we test "rowSpan" so we don't do this
        // if rowSpan = 0 (automatic readjustment).
        int32_t aboveRowToInsertNewCellInto =
            cellData.NumberOfPrecedingRows() + 1;
        nsresult rv = SplitCellIntoRows(
            &aTableElement, cellData.mFirst.mRow, cellData.mFirst.mColumn,
            aboveRowToInsertNewCellInto, cellData.NumberOfFollowingRows(),
            nullptr);
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::SplitCellIntoRows() failed");
          return rv;
        }
      }
      if (!cellInDeleteRow) {
        // Reference cell to find row to delete.
        cellInDeleteRow = std::move(cellData.mElement);
      }
    }
    // Skip over other columns spanned by this cell
    columnIndex += cellData.mEffectiveColSpan;
  }

  // Things are messed up if we didn't find a cell in the row!
  if (!cellInDeleteRow) {
    NS_WARNING("There was no cell in deleting row");
    return NS_ERROR_FAILURE;
  }

  // Delete the entire row.
  RefPtr<Element> parentRow =
      GetInclusiveAncestorByTagNameInternal(*nsGkAtoms::tr, *cellInDeleteRow);
  if (parentRow) {
    nsresult rv = DeleteNodeWithTransaction(*parentRow);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::tr) "
          "failed");
      return rv;
    }
  }

  // Now we can set new rowspans for cells stored above.
  for (SpanCell& spanCell : spanCellArray) {
    if (NS_WARN_IF(!spanCell.mElement)) {
      continue;
    }
    nsresult rv =
        SetRowSpan(MOZ_KnownLive(spanCell.mElement), spanCell.mNewRowSpanValue);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::SetRawSpan() failed");
      return rv;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::SelectTable() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSelectTable);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SelectTable() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> table =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
  if (!table) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::table)"
        " failed");
    return NS_OK;  // Don't fail if we didn't find a table.
  }

  rv = ClearSelection();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::ClearSelection() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  rv = AppendContentToSelectionAsRange(*table);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::AppendContentToSelectionAsRange() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SelectTableCell() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSelectTableCell);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SelectTableCell() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> cell =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cell) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::td) "
        "failed");
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  rv = ClearSelection();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::ClearSelection() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  rv = AppendContentToSelectionAsRange(*cell);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::AppendContentToSelectionAsRange() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SelectAllTableCells() {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eSelectAllTableCells);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SelectAllTableCells() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> cell =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cell) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::td) "
        "failed");
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get parent table
  RefPtr<Element> table =
      GetInclusiveAncestorByTagNameInternal(*nsGkAtoms::table, *cell);
  if (!table) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
        "failed");
    return NS_ERROR_FAILURE;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Suppress nsISelectionListener notification
  // until all selection changes are finished
  SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  rv = ClearSelection();
  if (rv == NS_ERROR_EDITOR_DESTROYED) {
    NS_WARNING("HTMLEditor::ClearSelection() caused destroying the editor");
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::ClearSelection() failed, but might be ignored");

  // Select all cells in the same column as current cell
  bool cellSelected = false;
  // Safety code to select starting cell if nothing else was selected
  auto AppendContentToStartCell = [&]() MOZ_CAN_RUN_SCRIPT {
    MOZ_ASSERT(!cellSelected);
    // XXX In this case, we ignore `NS_ERROR_FAILURE` set by above inner
    //     `for` loop.
    nsresult rv = AppendContentToSelectionAsRange(*startCell);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::AppendContentToSelectionAsRange() failed");
    return EditorBase::ToGenericNSResult(rv);
  };
  for (int32_t row = 0; row < tableSize.mRowCount; row++) {
    for (int32_t col = 0; col < tableSize.mColumnCount;) {
      const auto cellData =
          CellData::AtIndexInTableElement(*this, *table, row, col);
      if (NS_WARN_IF(cellData.FailedOrNotFound())) {
        return !cellSelected ? AppendContentToStartCell() : NS_ERROR_FAILURE;
      }

      // Skip cells that are spanned from previous rows or columns
      // XXX So, we should distinguish whether CellData returns error or just
      //     not found later.
      if (cellData.mElement && !cellData.IsSpannedFromOtherRowOrColumn()) {
        nsresult rv = AppendContentToSelectionAsRange(*cellData.mElement);
        if (rv == NS_ERROR_EDITOR_DESTROYED) {
          NS_WARNING(
              "HTMLEditor::AppendContentToSelectionAsRange() caused "
              "destroying the editor");
          return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
        }
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "HTMLEditor::AppendContentToSelectionAsRange() failed, but "
              "might be ignored");
          return !cellSelected ? AppendContentToStartCell()
                               : EditorBase::ToGenericNSResult(rv);
        }
        cellSelected = true;
      }
      MOZ_ASSERT(col < cellData.NextColumnIndex());
      col = cellData.NextColumnIndex();
    }
  }
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SelectTableRow() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eSelectTableRow);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SelectTableRow() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> cell =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cell) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::td) "
        "failed");
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get table and location of cell:
  RefPtr<Element> table;
  int32_t startRowIndex, startColIndex;

  rv = GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                      nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (!table) {
    NS_WARNING("HTMLEditor::GetCellContext() didn't return <table> element");
    return NS_ERROR_FAILURE;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Note: At this point, we could get first and last cells in row,
  // then call SelectBlockOfCells, but that would take just
  // a little less code, so the following is more efficient

  // Suppress nsISelectionListener notification
  // until all selection changes are finished
  SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  rv = ClearSelection();
  if (rv == NS_ERROR_EDITOR_DESTROYED) {
    NS_WARNING("HTMLEditor::ClearSelection() caused destroying the editor");
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::ClearSelection() failed, but might be ignored");

  // Select all cells in the same row as current cell
  bool cellSelected = false;
  for (int32_t col = 0; col < tableSize.mColumnCount;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *table, startRowIndex, col);
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      if (cellSelected) {
        return NS_ERROR_FAILURE;
      }
      // Safety code to select starting cell if nothing else was selected
      nsresult rv = AppendContentToSelectionAsRange(*startCell);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::AppendContentToSelectionAsRange() failed");
      NS_WARNING_ASSERTION(
          cellData.isOk() || NS_SUCCEEDED(rv) ||
              NS_FAILED(EditorBase::ToGenericNSResult(rv)),
          "CellData::AtIndexInTableElement() failed, but ignored");
      return EditorBase::ToGenericNSResult(rv);
    }

    // Skip cells that are spanned from previous rows or columns
    // XXX So, we should distinguish whether CellData returns error or just
    //     not found later.
    if (cellData.mElement && !cellData.IsSpannedFromOtherRowOrColumn()) {
      nsresult rv = AppendContentToSelectionAsRange(*cellData.mElement);
      if (rv == NS_ERROR_EDITOR_DESTROYED) {
        NS_WARNING(
            "HTMLEditor::AppendContentToSelectionAsRange() caused destroying "
            "the editor");
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
      }
      if (NS_FAILED(rv)) {
        if (cellSelected) {
          NS_WARNING("HTMLEditor::AppendContentToSelectionAsRange() failed");
          return EditorBase::ToGenericNSResult(rv);
        }
        // Safety code to select starting cell if nothing else was selected
        nsresult rvTryAgain = AppendContentToSelectionAsRange(*startCell);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "HTMLEditor::AppendContentToSelectionAsRange() failed");
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(EditorBase::ToGenericNSResult(rv)) ||
                NS_SUCCEEDED(rvTryAgain) ||
                NS_FAILED(EditorBase::ToGenericNSResult(rvTryAgain)),
            "HTMLEditor::AppendContentToSelectionAsRange(*cellData.mElement) "
            "failed, but ignored");
        return EditorBase::ToGenericNSResult(rvTryAgain);
      }
      cellSelected = true;
    }
    MOZ_ASSERT(col < cellData.NextColumnIndex());
    col = cellData.NextColumnIndex();
  }
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SelectTableColumn() {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eSelectTableColumn);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SelectTableColumn() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> cell =
      GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  if (!cell) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::td) "
        "failed");
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get location of cell:
  RefPtr<Element> table;
  int32_t startRowIndex, startColIndex;

  rv = GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                      nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (!table) {
    NS_WARNING("HTMLEditor::GetCellContext() didn't return <table> element");
    return NS_ERROR_FAILURE;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Suppress nsISelectionListener notification
  // until all selection changes are finished
  SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  rv = ClearSelection();
  if (rv == NS_ERROR_EDITOR_DESTROYED) {
    NS_WARNING("HTMLEditor::ClearSelection() caused destroying the editor");
    return EditorBase::ToGenericNSResult(rv);
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::ClearSelection() failed, but might be ignored");

  // Select all cells in the same column as current cell
  bool cellSelected = false;
  for (int32_t row = 0; row < tableSize.mRowCount;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *table, row, startColIndex);
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      if (cellSelected) {
        return NS_ERROR_FAILURE;
      }
      // Safety code to select starting cell if nothing else was selected
      nsresult rv = AppendContentToSelectionAsRange(*startCell);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::AppendContentToSelectionAsRange() failed");
      NS_WARNING_ASSERTION(
          cellData.isOk() || NS_SUCCEEDED(rv) ||
              NS_FAILED(EditorBase::ToGenericNSResult(rv)),
          "CellData::AtIndexInTableElement() failed, but ignored");
      return EditorBase::ToGenericNSResult(rv);
    }

    // Skip cells that are spanned from previous rows or columns
    // XXX So, we should distinguish whether CellData returns error or just
    //     not found later.
    if (cellData.mElement && !cellData.IsSpannedFromOtherRowOrColumn()) {
      nsresult rv = AppendContentToSelectionAsRange(*cellData.mElement);
      if (rv == NS_ERROR_EDITOR_DESTROYED) {
        NS_WARNING(
            "HTMLEditor::AppendContentToSelectionAsRange() caused destroying "
            "the editor");
        return EditorBase::ToGenericNSResult(rv);
      }
      if (NS_FAILED(rv)) {
        if (cellSelected) {
          NS_WARNING("HTMLEditor::AppendContentToSelectionAsRange() failed");
          return EditorBase::ToGenericNSResult(rv);
        }
        // Safety code to select starting cell if nothing else was selected
        nsresult rvTryAgain = AppendContentToSelectionAsRange(*startCell);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "HTMLEditor::AppendContentToSelectionAsRange() failed");
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(EditorBase::ToGenericNSResult(rv)) ||
                NS_SUCCEEDED(rvTryAgain) ||
                NS_FAILED(EditorBase::ToGenericNSResult(rvTryAgain)),
            "HTMLEditor::AppendContentToSelectionAsRange(*cellData.mElement) "
            "failed, but ignored");
        return EditorBase::ToGenericNSResult(rvTryAgain);
      }
      cellSelected = true;
    }
    MOZ_ASSERT(row < cellData.NextRowIndex());
    row = cellData.NextRowIndex();
  }
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SplitTableCell() {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eSplitTableCellElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, actualRowSpan, actualColSpan;
  // Get cell, table, etc. at selection anchor node
  rv = GetCellContext(getter_AddRefs(table), getter_AddRefs(cell), nullptr,
                      nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (!table || !cell) {
    NS_WARNING(
        "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  // We need rowspan and colspan data
  rv = GetCellSpansAt(table, startRowIndex, startColIndex, actualRowSpan,
                      actualColSpan);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellSpansAt() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // Must have some span to split
  if (actualRowSpan <= 1 && actualColSpan <= 1) {
    return NS_OK;
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of BR in new cell until we're done
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return EditorBase::ToGenericNSResult(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // We reset selection
  AutoSelectionSetterAfterTableEdit setCaret(
      *this, table, startRowIndex, startColIndex, ePreviousColumn, false);
  //...so suppress Rules System selection munging
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  RefPtr<Element> newCell;
  int32_t rowIndex = startRowIndex;
  int32_t rowSpanBelow, colSpanAfter;

  // Split up cell row-wise first into rowspan=1 above, and the rest below,
  // whittling away at the cell below until no more extra span
  for (rowSpanBelow = actualRowSpan - 1; rowSpanBelow >= 0; rowSpanBelow--) {
    // We really split row-wise only if we had rowspan > 1
    if (rowSpanBelow > 0) {
      nsresult rv = SplitCellIntoRows(table, rowIndex, startColIndex, 1,
                                      rowSpanBelow, getter_AddRefs(newCell));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::SplitCellIntoRows() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
      DebugOnly<nsresult> rvIgnored = CopyCellBackgroundColor(newCell, cell);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::CopyCellBackgroundColor() failed, but ignored");
    }
    int32_t colIndex = startColIndex;
    // Now split the cell with rowspan = 1 into cells if it has colSpan > 1
    for (colSpanAfter = actualColSpan - 1; colSpanAfter > 0; colSpanAfter--) {
      nsresult rv = SplitCellIntoColumns(table, rowIndex, colIndex, 1,
                                         colSpanAfter, getter_AddRefs(newCell));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::SplitCellIntoColumns() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
      DebugOnly<nsresult> rvIgnored = CopyCellBackgroundColor(newCell, cell);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::CopyCellBackgroundColor() failed, but ignored");
      colIndex++;
    }
    // Point to the new cell and repeat
    rowIndex++;
  }
  return NS_OK;
}

nsresult HTMLEditor::CopyCellBackgroundColor(Element* aDestCell,
                                             Element* aSourceCell) {
  if (NS_WARN_IF(!aDestCell) || NS_WARN_IF(!aSourceCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aSourceCell->HasAttr(nsGkAtoms::bgcolor)) {
    return NS_OK;
  }

  // Copy backgournd color to new cell.
  nsString backgroundColor;
  aSourceCell->GetAttr(nsGkAtoms::bgcolor, backgroundColor);
  nsresult rv = SetAttributeWithTransaction(*aDestCell, *nsGkAtoms::bgcolor,
                                            backgroundColor);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::SetAttributeWithTransaction(nsGkAtoms::bgcolor) failed");
  return rv;
}

nsresult HTMLEditor::SplitCellIntoColumns(Element* aTable, int32_t aRowIndex,
                                          int32_t aColIndex,
                                          int32_t aColSpanLeft,
                                          int32_t aColSpanRight,
                                          Element** aNewCell) {
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (aNewCell) {
    *aNewCell = nullptr;
  }

  const auto cellData =
      CellData::AtIndexInTableElement(*this, *aTable, aRowIndex, aColIndex);
  if (NS_WARN_IF(cellData.FailedOrNotFound())) {
    return NS_ERROR_FAILURE;
  }

  // We can't split!
  if (cellData.mEffectiveColSpan <= 1 ||
      aColSpanLeft + aColSpanRight > cellData.mEffectiveColSpan) {
    return NS_OK;
  }

  // Reduce colspan of cell to split
  nsresult rv = SetColSpan(cellData.mElement, aColSpanLeft);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetColSpan() failed");
    return rv;
  }

  // Insert new cell after using the remaining span
  // and always get the new cell so we can copy the background color;
  RefPtr<Element> newCellElement;
  rv = InsertCell(cellData.mElement, cellData.mEffectiveRowSpan, aColSpanRight,
                  true, false, getter_AddRefs(newCellElement));
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::InsertCell() failed");
    return rv;
  }
  if (!newCellElement) {
    return NS_OK;
  }
  if (aNewCell) {
    *aNewCell = do_AddRef(newCellElement).take();
  }
  rv = CopyCellBackgroundColor(newCellElement, cellData.mElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::CopyCellBackgroundColor() failed");
  return rv;
}

nsresult HTMLEditor::SplitCellIntoRows(Element* aTable, int32_t aRowIndex,
                                       int32_t aColIndex, int32_t aRowSpanAbove,
                                       int32_t aRowSpanBelow,
                                       Element** aNewCell) {
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNewCell) {
    *aNewCell = nullptr;
  }

  const auto cellData =
      CellData::AtIndexInTableElement(*this, *aTable, aRowIndex, aColIndex);
  if (NS_WARN_IF(cellData.FailedOrNotFound())) {
    return NS_ERROR_FAILURE;
  }

  // We can't split!
  if (cellData.mEffectiveRowSpan <= 1 ||
      aRowSpanAbove + aRowSpanBelow > cellData.mEffectiveRowSpan) {
    return NS_OK;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *aTable);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Find a cell to insert before or after
  RefPtr<Element> cellElementAtInsertionPoint;
  RefPtr<Element> lastCellFound;
  bool insertAfter = (cellData.mFirst.mColumn > 0);
  for (int32_t colIndex = 0,
               rowBelowIndex = cellData.mFirst.mRow + aRowSpanAbove;
       colIndex <= tableSize.mColumnCount;) {
    const auto cellDataAtInsertionPoint = CellData::AtIndexInTableElement(
        *this, *aTable, rowBelowIndex, colIndex);
    // If we fail here, it could be because row has bad rowspan values,
    // such as all cells having rowspan > 1 (Call FixRowSpan first!).
    // XXX According to the comment, this does not assume that
    //     FixRowSpan() doesn't work well and user can create non-rectangular
    //     table.  So, we should not return error when CellData cannot find
    //     a cell.
    if (NS_WARN_IF(cellDataAtInsertionPoint.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // FYI: Don't use std::move() here since the following checks will use
    //      utility methods of cellDataAtInsertionPoint, but some of them
    //      check whether its mElement is not nullptr.
    cellElementAtInsertionPoint = cellDataAtInsertionPoint.mElement;

    // Skip over cells spanned from above (like the one we are splitting!)
    if (cellDataAtInsertionPoint.mElement &&
        !cellDataAtInsertionPoint.IsSpannedFromOtherRow()) {
      if (!insertAfter) {
        // Inserting before, so stop at first cell in row we want to insert
        // into.
        break;
      }
      // New cell isn't first in row,
      // so stop after we find the cell just before new cell's column
      if (cellDataAtInsertionPoint.NextColumnIndex() ==
          cellData.mFirst.mColumn) {
        break;
      }
      // If cell found is AFTER desired new cell colum,
      // we have multiple cells with rowspan > 1 that
      // prevented us from finding a cell to insert after...
      if (cellDataAtInsertionPoint.mFirst.mColumn > cellData.mFirst.mColumn) {
        // ... so instead insert before the cell we found
        insertAfter = false;
        break;
      }
      // FYI: Don't use std::move() here since
      //      cellDataAtInsertionPoint.NextColumnIndex() needs it.
      lastCellFound = cellDataAtInsertionPoint.mElement;
    }
    MOZ_ASSERT(colIndex < cellDataAtInsertionPoint.NextColumnIndex());
    colIndex = cellDataAtInsertionPoint.NextColumnIndex();
  }

  if (!cellElementAtInsertionPoint && lastCellFound) {
    // Edge case where we didn't find a cell to insert after
    // or before because column(s) before desired column
    // and all columns after it are spanned from above.
    // We can insert after the last cell we found
    cellElementAtInsertionPoint = std::move(lastCellFound);
    insertAfter = true;  // Should always be true, but let's be sure
  }

  // Reduce rowspan of cell to split
  nsresult rv = SetRowSpan(cellData.mElement, aRowSpanAbove);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetRowSpan() failed");
    return rv;
  }

  // Insert new cell after using the remaining span
  // and always get the new cell so we can copy the background color;
  RefPtr<Element> newCell;
  rv = InsertCell(cellElementAtInsertionPoint, aRowSpanBelow,
                  cellData.mEffectiveColSpan, insertAfter, false,
                  getter_AddRefs(newCell));
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::InsertCell() failed");
    return rv;
  }
  if (!newCell) {
    return NS_OK;
  }
  if (aNewCell) {
    *aNewCell = do_AddRef(newCell).take();
  }
  rv = CopyCellBackgroundColor(newCell, cellElementAtInsertionPoint);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::CopyCellBackgroundColor() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::SwitchTableCellHeaderType(Element* aSourceCell,
                                                    Element** aNewCell) {
  if (NS_WARN_IF(!aSourceCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eSetTableCellElementType);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of BR in new cell created by
  // ReplaceContainerAndCloneAttributesWithTransaction().
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return EditorBase::ToGenericNSResult(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Save current selection to restore when done.
  // This is needed so ReplaceContainerAndCloneAttributesWithTransaction()
  // can monitor selection when replacing nodes.
  AutoSelectionRestorer restoreSelectionLater(this);

  // Set to the opposite of current type
  nsAtom* newCellName =
      aSourceCell->IsHTMLElement(nsGkAtoms::td) ? nsGkAtoms::th : nsGkAtoms::td;

  // This creates new node, moves children, copies attributes (true)
  //   and manages the selection!
  Result<CreateElementResult, nsresult> newCellElementOrError =
      ReplaceContainerAndCloneAttributesWithTransaction(
          *aSourceCell, MOZ_KnownLive(*newCellName));
  if (MOZ_UNLIKELY(newCellElementOrError.isErr())) {
    NS_WARNING(
        "EditorBase::ReplaceContainerAndCloneAttributesWithTransaction() "
        "failed");
    return newCellElementOrError.unwrapErr();
  }
  // restoreSelectionLater will change selection
  newCellElementOrError.inspect().IgnoreCaretPointSuggestion();

  // Return the new cell
  if (aNewCell) {
    newCellElementOrError.unwrap().UnwrapNewNode().forget(aNewCell);
  }

  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::JoinTableCells(bool aMergeNonContiguousContents) {
  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eJoinTableCellElements);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("CanHandleAndFlushPendingNotifications() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<Element> table;
  RefPtr<Element> targetCell;
  int32_t startRowIndex, startColIndex;

  // Get cell, table, etc. at selection anchor node
  rv = GetCellContext(getter_AddRefs(table), getter_AddRefs(targetCell),
                      nullptr, nullptr, &startRowIndex, &startColIndex);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellContext() failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (!table || !targetCell) {
    NS_WARNING(
        "HTMLEditor::GetCellContext() didn't return <table> and/or cell");
    return NS_OK;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Don't let Rules System change the selection
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  // Note: We dont' use AutoSelectionSetterAfterTableEdit here so the selection
  // is retained after joining. This leaves the target cell selected
  // as well as the "non-contiguous" cells, so user can see what happened.

  SelectedTableCellScanner scanner(SelectionRef());

  // If only one cell is selected, join with cell to the right
  if (scanner.ElementsRef().Length() > 1) {
    // We have selected cells: Join just contiguous cells
    // and just merge contents if not contiguous
    Result<TableSize, nsresult> tableSizeOrError =
        TableSize::Create(*this, *table);
    if (NS_WARN_IF(tableSizeOrError.isErr())) {
      return EditorBase::ToGenericNSResult(tableSizeOrError.unwrapErr());
    }
    // FYI: Cannot be const because the row count will be updated
    TableSize tableSize = tableSizeOrError.unwrap();

    RefPtr<PresShell> presShell = GetPresShell();
    // `MOZ_KnownLive(scanner.ElementsRef()[0])` is safe because `scanner`
    // grabs it until it's destroyed later.
    const CellIndexes firstSelectedCellIndexes(
        MOZ_KnownLive(scanner.ElementsRef()[0]), presShell);
    if (NS_WARN_IF(firstSelectedCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }

    // Get spans for cell we will merge into
    int32_t firstRowSpan, firstColSpan;
    nsresult rv = GetCellSpansAt(table, firstSelectedCellIndexes.mRow,
                                 firstSelectedCellIndexes.mColumn, firstRowSpan,
                                 firstColSpan);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::GetCellSpansAt() failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    // This defines the last indexes along the "edges"
    // of the contiguous block of cells, telling us
    // that we can join adjacent cells to the block
    // Start with same as the first values,
    // then expand as we find adjacent selected cells
    int32_t lastRowIndex = firstSelectedCellIndexes.mRow;
    int32_t lastColIndex = firstSelectedCellIndexes.mColumn;

    // First pass: Determine boundaries of contiguous rectangular block that
    // we will join into one cell, favoring adjacent cells in the same row.
    for (int32_t rowIndex = firstSelectedCellIndexes.mRow;
         rowIndex <= lastRowIndex; rowIndex++) {
      int32_t currentRowCount = tableSize.mRowCount;
      // Be sure each row doesn't have rowspan errors
      rv = FixBadRowSpan(table, rowIndex, tableSize.mRowCount);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::FixBadRowSpan() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
      // Adjust rowcount by number of rows we removed
      lastRowIndex -= currentRowCount - tableSize.mRowCount;

      bool cellFoundInRow = false;
      bool lastRowIsSet = false;
      int32_t lastColInRow = 0;
      int32_t firstColInRow = firstSelectedCellIndexes.mColumn;
      int32_t colIndex = firstSelectedCellIndexes.mColumn;
      for (; colIndex < tableSize.mColumnCount;) {
        const auto cellData =
            CellData::AtIndexInTableElement(*this, *table, rowIndex, colIndex);
        if (NS_WARN_IF(cellData.FailedOrNotFound())) {
          return NS_ERROR_FAILURE;
        }

        if (cellData.mIsSelected) {
          if (!cellFoundInRow) {
            // We've just found the first selected cell in this row
            firstColInRow = cellData.mCurrent.mColumn;
          }
          if (cellData.mCurrent.mRow > firstSelectedCellIndexes.mRow &&
              firstColInRow != firstSelectedCellIndexes.mColumn) {
            // We're in at least the second row,
            // but left boundary is "ragged" (not the same as 1st row's start)
            // Let's just end block on previous row
            // and keep previous lastColIndex
            // TODO: We could try to find the Maximum firstColInRow
            //      so our block can still extend down more rows?
            lastRowIndex = std::max(0, cellData.mCurrent.mRow - 1);
            lastRowIsSet = true;
            break;
          }
          // Save max selected column in this row, including extra colspan
          lastColInRow = cellData.LastColumnIndex();
          cellFoundInRow = true;
        } else if (cellFoundInRow) {
          // No cell or not selected, but at least one cell in row was found
          if (cellData.mCurrent.mRow > firstSelectedCellIndexes.mRow + 1 &&
              cellData.mCurrent.mColumn <= lastColIndex) {
            // Cell is in a column less than current right border in
            // the third or higher selected row, so stop block at the previous
            // row
            lastRowIndex = std::max(0, cellData.mCurrent.mRow - 1);
            lastRowIsSet = true;
          }
          // We're done with this row
          break;
        }
        MOZ_ASSERT(colIndex < cellData.NextColumnIndex());
        colIndex = cellData.NextColumnIndex();
      }  // End of column loop

      // Done with this row
      if (cellFoundInRow) {
        if (rowIndex == firstSelectedCellIndexes.mRow) {
          // First row always initializes the right boundary
          lastColIndex = lastColInRow;
        }

        // If we didn't determine last row above...
        if (!lastRowIsSet) {
          if (colIndex < lastColIndex) {
            // (don't think we ever get here?)
            // Cell is in a column less than current right boundary,
            // so stop block at the previous row
            lastRowIndex = std::max(0, rowIndex - 1);
          } else {
            // Go on to examine next row
            lastRowIndex = rowIndex + 1;
          }
        }
        // Use the minimum col we found so far for right boundary
        lastColIndex = std::min(lastColIndex, lastColInRow);
      } else {
        // No selected cells in this row -- stop at row above
        // and leave last column at its previous value
        lastRowIndex = std::max(0, rowIndex - 1);
      }
    }

    // The list of cells we will delete after joining
    nsTArray<RefPtr<Element>> deleteList;

    // 2nd pass: Do the joining and merging
    for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
      for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount;) {
        const auto cellData =
            CellData::AtIndexInTableElement(*this, *table, rowIndex, colIndex);
        if (NS_WARN_IF(cellData.FailedOrNotFound())) {
          return NS_ERROR_FAILURE;
        }

        // If this is 0, we are past last cell in row, so exit the loop
        if (!cellData.mEffectiveColSpan) {
          break;
        }

        // Merge only selected cells (skip cell we're merging into, of course)
        if (cellData.mIsSelected &&
            cellData.mElement != scanner.ElementsRef()[0]) {
          if (cellData.mCurrent.mRow >= firstSelectedCellIndexes.mRow &&
              cellData.mCurrent.mRow <= lastRowIndex &&
              cellData.mCurrent.mColumn >= firstSelectedCellIndexes.mColumn &&
              cellData.mCurrent.mColumn <= lastColIndex) {
            // We are within the join region
            // Problem: It is very tricky to delete cells as we merge,
            // since that will upset the cellmap
            // Instead, build a list of cells to delete and do it later
            NS_ASSERTION(!cellData.IsSpannedFromOtherRow(),
                         "JoinTableCells: StartRowIndex is in row above");

            if (cellData.mEffectiveColSpan > 1) {
              // Check if cell "hangs" off the boundary because of colspan > 1
              // Use split methods to chop off excess
              int32_t extraColSpan = cellData.mFirst.mColumn +
                                     cellData.mEffectiveColSpan -
                                     (lastColIndex + 1);
              if (extraColSpan > 0) {
                nsresult rv = SplitCellIntoColumns(
                    table, cellData.mFirst.mRow, cellData.mFirst.mColumn,
                    cellData.mEffectiveColSpan - extraColSpan, extraColSpan,
                    nullptr);
                if (NS_FAILED(rv)) {
                  NS_WARNING("HTMLEditor::SplitCellIntoColumns() failed");
                  return EditorBase::ToGenericNSResult(rv);
                }
              }
            }

            nsresult rv =
                MergeCells(scanner.ElementsRef()[0], cellData.mElement, false);
            if (NS_FAILED(rv)) {
              NS_WARNING("HTMLEditor::MergeCells() failed");
              return EditorBase::ToGenericNSResult(rv);
            }

            // Add cell to list to delete
            deleteList.AppendElement(cellData.mElement.get());
          } else if (aMergeNonContiguousContents) {
            // Cell is outside join region -- just merge the contents
            nsresult rv =
                MergeCells(scanner.ElementsRef()[0], cellData.mElement, false);
            if (NS_FAILED(rv)) {
              NS_WARNING("HTMLEditor::MergeCells() failed");
              return rv;
            }
          }
        }
        MOZ_ASSERT(colIndex < cellData.NextColumnIndex());
        colIndex = cellData.NextColumnIndex();
      }
    }

    // All cell contents are merged. Delete the empty cells we accumulated
    // Prevent rules testing until we're done
    IgnoredErrorResult error;
    AutoEditSubActionNotifier startToHandleEditSubAction(
        *this, EditSubAction::eDeleteNode, nsIEditor::eNext, error);
    if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
      return EditorBase::ToGenericNSResult(error.StealNSResult());
    }
    NS_WARNING_ASSERTION(!error.Failed(),
                         "HTMLEditor::OnStartToHandleTopLevelEditSubAction() "
                         "failed, but ignored");

    for (uint32_t i = 0, n = deleteList.Length(); i < n; i++) {
      RefPtr<Element> nodeToBeRemoved = deleteList[i];
      if (nodeToBeRemoved) {
        nsresult rv = DeleteNodeWithTransaction(*nodeToBeRemoved);
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return EditorBase::ToGenericNSResult(rv);
        }
      }
    }
    // Cleanup selection: remove ranges where cells were deleted
    uint32_t rangeCount = SelectionRef().RangeCount();

    // TODO: Rewriting this with reversed ranged-loop may make it simpler.
    RefPtr<dom::Range> range;
    for (uint32_t i = 0; i < rangeCount; i++) {
      range = SelectionRef().GetRangeAt(i);
      if (NS_WARN_IF(!range)) {
        return NS_ERROR_FAILURE;
      }

      Element* deletedCell =
          HTMLEditUtils::GetTableCellElementIfOnlyOneSelected(*range);
      if (!deletedCell) {
        SelectionRef().RemoveRangeAndUnselectFramesAndNotifyListeners(*range,
                                                                      error);
        NS_WARNING_ASSERTION(
            !error.Failed(),
            "Selection::RemoveRangeAndUnselectFramesAndNotifyListeners() "
            "failed, but ignored");
        rangeCount--;
        i--;
      }
    }

    // Set spans for the cell everything merged into
    rv = SetRowSpan(MOZ_KnownLive(scanner.ElementsRef()[0]),
                    lastRowIndex - firstSelectedCellIndexes.mRow + 1);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::SetRowSpan() failed");
      return EditorBase::ToGenericNSResult(rv);
    }
    rv = SetColSpan(MOZ_KnownLive(scanner.ElementsRef()[0]),
                    lastColIndex - firstSelectedCellIndexes.mColumn + 1);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::SetColSpan() failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    // Fixup disturbances in table layout
    DebugOnly<nsresult> rvIgnored = NormalizeTableInternal(*table);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::NormalizeTableInternal() failed, but ignored");
  } else {
    // Joining with cell to the right -- get rowspan and colspan data of target
    // cell.
    const auto leftCellData = CellData::AtIndexInTableElement(
        *this, *table, startRowIndex, startColIndex);
    if (NS_WARN_IF(leftCellData.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // Get data for cell to the right.
    const auto rightCellData = CellData::AtIndexInTableElement(
        *this, *table, leftCellData.mFirst.mRow,
        leftCellData.mFirst.mColumn + leftCellData.mEffectiveColSpan);
    if (NS_WARN_IF(rightCellData.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // XXX So, this does not assume that CellData returns error when just not
    //     found.  We need to fix this later.
    if (!rightCellData.mElement) {
      return NS_OK;  // Don't fail if there's no cell
    }

    // sanity check
    NS_ASSERTION(
        rightCellData.mCurrent.mRow >= rightCellData.mFirst.mRow,
        "JoinCells: rightCellData.mCurrent.mRow < rightCellData.mFirst.mRow");

    // Figure out span of merged cell starting from target's starting row
    // to handle case of merged cell starting in a row above
    int32_t spanAboveMergedCell = rightCellData.NumberOfPrecedingRows();
    int32_t effectiveRowSpan2 =
        rightCellData.mEffectiveRowSpan - spanAboveMergedCell;
    if (effectiveRowSpan2 > leftCellData.mEffectiveRowSpan) {
      // Cell to the right spans into row below target
      // Split off portion below target cell's bottom-most row
      nsresult rv = SplitCellIntoRows(
          table, rightCellData.mFirst.mRow, rightCellData.mFirst.mColumn,
          spanAboveMergedCell + leftCellData.mEffectiveRowSpan,
          effectiveRowSpan2 - leftCellData.mEffectiveRowSpan, nullptr);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::SplitCellIntoRows() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }

    // Move contents from cell to the right
    // Delete the cell now only if it starts in the same row
    //   and has enough row "height"
    nsresult rv =
        MergeCells(leftCellData.mElement, rightCellData.mElement,
                   !rightCellData.IsSpannedFromOtherRow() &&
                       effectiveRowSpan2 >= leftCellData.mEffectiveRowSpan);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::MergeCells() failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    if (effectiveRowSpan2 < leftCellData.mEffectiveRowSpan) {
      // Merged cell is "shorter"
      // (there are cells(s) below it that are row-spanned by target cell)
      // We could try splitting those cells, but that's REAL messy,
      // so the safest thing to do is NOT really join the cells
      return NS_OK;
    }

    if (spanAboveMergedCell > 0) {
      // Cell we merged started in a row above the target cell
      // Reduce rowspan to give room where target cell will extend its colspan
      nsresult rv = SetRowSpan(rightCellData.mElement, spanAboveMergedCell);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::SetRowSpan() failed");
        return EditorBase::ToGenericNSResult(rv);
      }
    }

    // Reset target cell's colspan to encompass cell to the right
    rv = SetColSpan(leftCellData.mElement, leftCellData.mEffectiveColSpan +
                                               rightCellData.mEffectiveColSpan);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::SetColSpan() failed");
      return EditorBase::ToGenericNSResult(rv);
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::MergeCells(RefPtr<Element> aTargetCell,
                                RefPtr<Element> aCellToMerge,
                                bool aDeleteCellToMerge) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aTargetCell) || NS_WARN_IF(!aCellToMerge)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Prevent rules testing until we're done
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eDeleteNode, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // Don't need to merge if cell is empty
  if (!IsEmptyCell(aCellToMerge)) {
    // Get index of last child in target cell
    // If we fail or don't have children,
    // we insert at index 0
    int32_t insertIndex = 0;

    // Start inserting just after last child
    uint32_t len = aTargetCell->GetChildCount();
    if (len == 1 && IsEmptyCell(aTargetCell)) {
      // Delete the empty node
      nsCOMPtr<nsIContent> cellChild = aTargetCell->GetFirstChild();
      if (NS_WARN_IF(!cellChild)) {
        return NS_ERROR_FAILURE;
      }
      nsresult rv = DeleteNodeWithTransaction(*cellChild);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
      insertIndex = 0;
    } else {
      insertIndex = (int32_t)len;
    }

    // Move the contents
    EditorDOMPoint pointToPutCaret;
    while (aCellToMerge->HasChildren()) {
      nsCOMPtr<nsIContent> cellChild = aCellToMerge->GetLastChild();
      if (NS_WARN_IF(!cellChild)) {
        return NS_ERROR_FAILURE;
      }
      nsresult rv = DeleteNodeWithTransaction(*cellChild);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
      Result<CreateContentResult, nsresult> insertChildContentResult =
          InsertNodeWithTransaction(*cellChild,
                                    EditorDOMPoint(aTargetCell, insertIndex));
      if (MOZ_UNLIKELY(insertChildContentResult.isErr())) {
        NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
        return insertChildContentResult.unwrapErr();
      }
      CreateContentResult unwrappedInsertChildContentResult =
          insertChildContentResult.unwrap();
      unwrappedInsertChildContentResult.MoveCaretPointTo(
          pointToPutCaret, *this,
          {SuggestCaret::OnlyIfHasSuggestion,
           SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    }
    if (pointToPutCaret.IsSet()) {
      nsresult rv = CollapseSelectionTo(pointToPutCaret);
      if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
        NS_WARNING(
            "EditorBase::CollapseSelectionTo() caused destroying the editor");
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }
  }

  if (!aDeleteCellToMerge) {
    return NS_OK;
  }

  // Delete cells whose contents were moved.
  nsresult rv = DeleteNodeWithTransaction(*aCellToMerge);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteNodeWithTransaction() failed");
  return rv;
}

nsresult HTMLEditor::FixBadRowSpan(Element* aTable, int32_t aRowIndex,
                                   int32_t& aNewRowCount) {
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *aTable);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  int32_t minRowSpan = -1;
  for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *aTable, aRowIndex, colIndex);
    // NOTE: This is a *real* failure.
    // CellData passes if cell is missing from cellmap
    // XXX If <table> has large rowspan value or colspan value than actual
    //     cells, we may hit error.  So, this method is always failed to
    //     "fix" the rowspan...
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // XXX So, this does not assume that CellData returns error when just not
    //     found.  We need to fix this later.
    if (!cellData.mElement) {
      break;
    }

    if (cellData.mRowSpan > 0 && !cellData.IsSpannedFromOtherRow() &&
        (cellData.mRowSpan < minRowSpan || minRowSpan == -1)) {
      minRowSpan = cellData.mRowSpan;
    }
    MOZ_ASSERT(colIndex < cellData.NextColumnIndex());
    colIndex = cellData.NextColumnIndex();
  }

  if (minRowSpan > 1) {
    // The amount to reduce everyone's rowspan
    // so at least one cell has rowspan = 1
    int32_t rowsReduced = minRowSpan - 1;
    for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount;) {
      const auto cellData =
          CellData::AtIndexInTableElement(*this, *aTable, aRowIndex, colIndex);
      if (NS_WARN_IF(cellData.FailedOrNotFound())) {
        return NS_ERROR_FAILURE;
      }

      // Fixup rowspans only for cells starting in current row
      // XXX So, this does not assume that CellData returns error when just
      //     not found a cell.  Fix this later.
      if (cellData.mElement && cellData.mRowSpan > 0 &&
          !cellData.IsSpannedFromOtherRowOrColumn()) {
        nsresult rv =
            SetRowSpan(cellData.mElement, cellData.mRowSpan - rowsReduced);
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::SetRawSpan() failed");
          return rv;
        }
      }
      MOZ_ASSERT(colIndex < cellData.NextColumnIndex());
      colIndex = cellData.NextColumnIndex();
    }
  }
  const Result<TableSize, nsresult> newTableSizeOrError =
      TableSize::Create(*this, *aTable);
  if (NS_WARN_IF(newTableSizeOrError.isErr())) {
    return newTableSizeOrError.inspectErr();
  }
  aNewRowCount = newTableSizeOrError.inspect().mRowCount;
  return NS_OK;
}

nsresult HTMLEditor::FixBadColSpan(Element* aTable, int32_t aColIndex,
                                   int32_t& aNewColCount) {
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *aTable);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.inspectErr();
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  int32_t minColSpan = -1;
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *aTable, rowIndex, aColIndex);
    // NOTE: This is a *real* failure.
    // CellData passes if cell is missing from cellmap
    // XXX If <table> has large rowspan value or colspan value than actual
    //     cells, we may hit error.  So, this method is always failed to
    //     "fix" the colspan...
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      return NS_ERROR_FAILURE;
    }

    // XXX So, this does not assume that CellData returns error when just
    //     not found a cell.  Fix this later.
    if (!cellData.mElement) {
      break;
    }
    if (cellData.mColSpan > 0 && !cellData.IsSpannedFromOtherColumn() &&
        (cellData.mColSpan < minColSpan || minColSpan == -1)) {
      minColSpan = cellData.mColSpan;
    }
    MOZ_ASSERT(rowIndex < cellData.NextRowIndex());
    rowIndex = cellData.NextRowIndex();
  }

  if (minColSpan > 1) {
    // The amount to reduce everyone's colspan
    // so at least one cell has colspan = 1
    int32_t colsReduced = minColSpan - 1;
    for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount;) {
      const auto cellData =
          CellData::AtIndexInTableElement(*this, *aTable, rowIndex, aColIndex);
      if (NS_WARN_IF(cellData.FailedOrNotFound())) {
        return NS_ERROR_FAILURE;
      }

      // Fixup colspans only for cells starting in current column
      // XXX So, this does not assume that CellData returns error when just
      //     not found a cell.  Fix this later.
      if (cellData.mElement && cellData.mColSpan > 0 &&
          !cellData.IsSpannedFromOtherRowOrColumn()) {
        nsresult rv =
            SetColSpan(cellData.mElement, cellData.mColSpan - colsReduced);
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::SetColSpan() failed");
          return rv;
        }
      }
      MOZ_ASSERT(rowIndex < cellData.NextRowIndex());
      rowIndex = cellData.NextRowIndex();
    }
  }
  const Result<TableSize, nsresult> newTableSizeOrError =
      TableSize::Create(*this, *aTable);
  if (NS_WARN_IF(newTableSizeOrError.isErr())) {
    return newTableSizeOrError.inspectErr();
  }
  aNewColCount = newTableSizeOrError.inspect().mColumnCount;
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::NormalizeTable(Element* aTableOrElementInTable) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNormalizeTable);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(editingHost &&
                 editingHost->IsContentEditablePlainTextOnly())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (!aTableOrElementInTable) {
    aTableOrElementInTable =
        GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
    if (!aTableOrElementInTable) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::"
          "table) failed");
      return NS_OK;  // Don't throw error even if the element is not in <table>.
    }
  }
  rv = NormalizeTableInternal(*aTableOrElementInTable);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::NormalizeTableInternal() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::NormalizeTableInternal(Element& aTableOrElementInTable) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> tableElement;
  if (aTableOrElementInTable.NodeInfo()->NameAtom() == nsGkAtoms::table) {
    tableElement = &aTableOrElementInTable;
  } else {
    tableElement = GetInclusiveAncestorByTagNameInternal(
        *nsGkAtoms::table, aTableOrElementInTable);
    if (!tableElement) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
          "failed");
      return NS_OK;  // Don't throw error even if the element is not in <table>.
    }
  }

  Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *tableElement);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return tableSizeOrError.unwrapErr();
  }
  // FYI: Cannot be const because the row/column count will be updated
  TableSize tableSize = tableSizeOrError.unwrap();

  // Save current selection
  AutoSelectionRestorer restoreSelectionLater(this);

  AutoPlaceholderBatch treateAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  // Prevent auto insertion of BR in new cell until we're done
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertNode, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // XXX If there is a cell which has bigger or smaller "rowspan" or "colspan"
  //     values, FixBadRowSpan() will return error.  So, we can do nothing
  //     if the table needs normalization...
  // Scan all cells in each row to detect bad rowspan values
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
    nsresult rv = FixBadRowSpan(tableElement, rowIndex, tableSize.mRowCount);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::FixBadRowSpan() failed");
      return rv;
    }
  }
  // and same for colspans
  for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount; colIndex++) {
    nsresult rv = FixBadColSpan(tableElement, colIndex, tableSize.mColumnCount);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::FixBadColSpan() failed");
      return rv;
    }
  }

  // Fill in missing cellmap locations with empty cells
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
    RefPtr<Element> previousCellElementInRow;
    for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount; colIndex++) {
      const auto cellData = CellData::AtIndexInTableElement(
          *this, *tableElement, rowIndex, colIndex);
      // NOTE: This is a *real* failure.
      // CellData passes if cell is missing from cellmap
      // XXX So, this method assumes that CellData won't return error when
      //     just not found.  Fix this later.
      if (NS_WARN_IF(cellData.FailedOrNotFound())) {
        return NS_ERROR_FAILURE;
      }

      if (cellData.mElement) {
        // Save the last cell found in the same row we are scanning
        if (!cellData.IsSpannedFromOtherRow()) {
          previousCellElementInRow = std::move(cellData.mElement);
        }
        continue;
      }

      // We are missing a cell at a cellmap location.
      // Add a cell after the previous cell element in the current row.
      if (NS_WARN_IF(!previousCellElementInRow)) {
        // We don't have any cells in this row -- We are really messed up!
        return NS_ERROR_FAILURE;
      }

      // Insert a new cell after (true), and return the new cell to us
      RefPtr<Element> newCellElement;
      nsresult rv = InsertCell(previousCellElementInRow, 1, 1, true, false,
                               getter_AddRefs(newCellElement));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::InsertCell() failed");
        return rv;
      }

      if (newCellElement) {
        previousCellElementInRow = std::move(newCellElement);
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetCellIndexes(Element* aCellElement,
                                         int32_t* aRowIndex,
                                         int32_t* aColumnIndex) {
  if (NS_WARN_IF(!aRowIndex) || NS_WARN_IF(!aColumnIndex)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetCellIndexes);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellIndexes() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  *aRowIndex = 0;
  *aColumnIndex = 0;

  if (!aCellElement) {
    // Use cell element which contains anchor of Selection when aCellElement is
    // nullptr.
    const CellIndexes cellIndexes(*this, SelectionRef());
    if (NS_WARN_IF(cellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    *aRowIndex = cellIndexes.mRow;
    *aColumnIndex = cellIndexes.mColumn;
    return NS_OK;
  }

  const RefPtr<PresShell> presShell{GetPresShell()};
  const CellIndexes cellIndexes(*aCellElement, presShell);
  if (NS_WARN_IF(cellIndexes.isErr())) {
    return NS_ERROR_FAILURE;
  }
  *aRowIndex = cellIndexes.mRow;
  *aColumnIndex = cellIndexes.mColumn;
  return NS_OK;
}

// static
nsTableWrapperFrame* HTMLEditor::GetTableFrame(const Element* aTableElement) {
  if (NS_WARN_IF(!aTableElement)) {
    return nullptr;
  }
  return do_QueryFrame(aTableElement->GetPrimaryFrame());
}

// Return actual number of cells (a cell with colspan > 1 counts as just 1)
int32_t HTMLEditor::GetNumberOfCellsInRow(Element& aTableElement,
                                          int32_t aRowIndex) {
  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, aTableElement);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return -1;
  }

  int32_t numberOfCells = 0;
  for (int32_t columnIndex = 0;
       columnIndex < tableSizeOrError.inspect().mColumnCount;) {
    const auto cellData = CellData::AtIndexInTableElement(
        *this, aTableElement, aRowIndex, columnIndex);
    // Failure means that there is no more cell in the row.  In this case,
    // we shouldn't return error since we just reach the end of the row.
    // XXX So, this method assumes that CellData won't return error when
    //     just not found.  Fix this later.
    if (cellData.FailedOrNotFound()) {
      break;
    }

    // Only count cells that start in row we are working with
    if (cellData.mElement && !cellData.IsSpannedFromOtherRow()) {
      numberOfCells++;
    }
    MOZ_ASSERT(columnIndex < cellData.NextColumnIndex());
    columnIndex = cellData.NextColumnIndex();
  }
  return numberOfCells;
}

NS_IMETHODIMP HTMLEditor::GetTableSize(Element* aTableOrElementInTable,
                                       int32_t* aRowCount,
                                       int32_t* aColumnCount) {
  if (NS_WARN_IF(!aRowCount) || NS_WARN_IF(!aColumnCount)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetTableSize);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetTableSize() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  *aRowCount = 0;
  *aColumnCount = 0;

  Element* tableOrElementInTable = aTableOrElementInTable;
  if (!tableOrElementInTable) {
    tableOrElementInTable =
        GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
    if (!tableOrElementInTable) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::"
          "table) failed");
      return NS_ERROR_FAILURE;
    }
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *tableOrElementInTable);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  *aRowCount = tableSizeOrError.inspect().mRowCount;
  *aColumnCount = tableSizeOrError.inspect().mColumnCount;
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetCellDataAt(
    Element* aTableElement, int32_t aRowIndex, int32_t aColumnIndex,
    Element** aCellElement, int32_t* aStartRowIndex, int32_t* aStartColumnIndex,
    int32_t* aRowSpan, int32_t* aColSpan, int32_t* aEffectiveRowSpan,
    int32_t* aEffectiveColSpan, bool* aIsSelected) {
  if (NS_WARN_IF(!aCellElement) || NS_WARN_IF(!aStartRowIndex) ||
      NS_WARN_IF(!aStartColumnIndex) || NS_WARN_IF(!aRowSpan) ||
      NS_WARN_IF(!aColSpan) || NS_WARN_IF(!aEffectiveRowSpan) ||
      NS_WARN_IF(!aEffectiveColSpan) || NS_WARN_IF(!aIsSelected)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetCellDataAt);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellDataAt() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  *aStartRowIndex = 0;
  *aStartColumnIndex = 0;
  *aRowSpan = 0;
  *aColSpan = 0;
  *aEffectiveRowSpan = 0;
  *aEffectiveColSpan = 0;
  *aIsSelected = false;
  *aCellElement = nullptr;

  // Let's keep the table element with strong pointer since editor developers
  // may not handle layout code of <table>, however, this method depends on
  // them.
  RefPtr<Element> table = aTableElement;
  if (!table) {
    // Get the selected table or the table enclosing the selection anchor.
    table = GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
    if (!table) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::"
          "table) failed");
      return NS_ERROR_FAILURE;
    }
  }

  const CellData cellData =
      CellData::AtIndexInTableElement(*this, *table, aRowIndex, aColumnIndex);
  if (NS_WARN_IF(cellData.FailedOrNotFound())) {
    return NS_ERROR_FAILURE;
  }
  NS_ADDREF(*aCellElement = cellData.mElement.get());
  *aIsSelected = cellData.mIsSelected;
  *aStartRowIndex = cellData.mFirst.mRow;
  *aStartColumnIndex = cellData.mFirst.mColumn;
  *aRowSpan = cellData.mRowSpan;
  *aColSpan = cellData.mColSpan;
  *aEffectiveRowSpan = cellData.mEffectiveRowSpan;
  *aEffectiveColSpan = cellData.mEffectiveColSpan;
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetCellAt(Element* aTableElement, int32_t aRowIndex,
                                    int32_t aColumnIndex,
                                    Element** aCellElement) {
  if (NS_WARN_IF(!aCellElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetCellAt);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetCellAt() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  *aCellElement = nullptr;

  Element* tableElement = aTableElement;
  if (!tableElement) {
    // Get the selected table or the table enclosing the selection anchor.
    tableElement = GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
    if (!tableElement) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::"
          "table) failed");
      return NS_ERROR_FAILURE;
    }
  }

  RefPtr<Element> cellElement =
      GetTableCellElementAt(*tableElement, aRowIndex, aColumnIndex);
  cellElement.forget(aCellElement);
  return NS_OK;
}

Element* HTMLEditor::GetTableCellElementAt(Element& aTableElement,
                                           int32_t aRowIndex,
                                           int32_t aColumnIndex) const {
  // Let's grab the <table> element while we're retrieving layout API since
  // editor developers do not watch all layout API changes.  So, it may
  // become unsafe.
  OwningNonNull<Element> tableElement(aTableElement);
  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(tableElement);
  if (!tableFrame) {
    NS_WARNING("There was no table layout information");
    return nullptr;
  }
  nsIContent* cell = tableFrame->GetCellAt(aRowIndex, aColumnIndex);
  return Element::FromNodeOrNull(cell);
}

// When all you want are the rowspan and colspan (not exposed in nsITableEditor)
nsresult HTMLEditor::GetCellSpansAt(Element* aTable, int32_t aRowIndex,
                                    int32_t aColIndex, int32_t& aActualRowSpan,
                                    int32_t& aActualColSpan) {
  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(aTable);
  if (!tableFrame) {
    NS_WARNING("There was no table layout information");
    return NS_ERROR_FAILURE;
  }
  aActualRowSpan = tableFrame->GetEffectiveRowSpanAt(aRowIndex, aColIndex);
  aActualColSpan = tableFrame->GetEffectiveColSpanAt(aRowIndex, aColIndex);

  return NS_OK;
}

nsresult HTMLEditor::GetCellContext(Element** aTable, Element** aCell,
                                    nsINode** aCellParent, int32_t* aCellOffset,
                                    int32_t* aRowIndex, int32_t* aColumnIndex) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Initialize return pointers
  if (aTable) {
    *aTable = nullptr;
  }
  if (aCell) {
    *aCell = nullptr;
  }
  if (aCellParent) {
    *aCellParent = nullptr;
  }
  if (aCellOffset) {
    *aCellOffset = 0;
  }
  if (aRowIndex) {
    *aRowIndex = 0;
  }
  if (aColumnIndex) {
    *aColumnIndex = 0;
  }

  RefPtr<Element> table;
  RefPtr<Element> cell;

  // Caller may supply the cell...
  if (aCell && *aCell) {
    cell = *aCell;
  }

  // ...but if not supplied,
  //    get cell if it's the child of selection anchor node,
  //    or get the enclosing by a cell
  if (!cell) {
    // Find a selected or enclosing table element
    Result<RefPtr<Element>, nsresult> cellOrRowOrTableElementOrError =
        GetSelectedOrParentTableElement();
    if (cellOrRowOrTableElementOrError.isErr()) {
      NS_WARNING("HTMLEditor::GetSelectedOrParentTableElement() failed");
      return cellOrRowOrTableElementOrError.unwrapErr();
    }
    if (!cellOrRowOrTableElementOrError.inspect()) {
      return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
    }
    if (cellOrRowOrTableElementOrError.inspect()->IsHTMLElement(
            nsGkAtoms::table)) {
      // We have a selected table, not a cell
      if (aTable) {
        cellOrRowOrTableElementOrError.unwrap().forget(aTable);
      }
      return NS_OK;
    }
    if (!HTMLEditUtils::IsTableCellElement(
            cellOrRowOrTableElementOrError.inspect())) {
      return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
    }

    // We found a cell
    cell = cellOrRowOrTableElementOrError.unwrap();
  }
  if (aCell) {
    // we don't want to cell.forget() here, because we use it below.
    *aCell = do_AddRef(cell).take();
  }

  // Get containing table
  table = GetInclusiveAncestorByTagNameInternal(*nsGkAtoms::table, *cell);
  if (!table) {
    NS_WARNING(
        "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
        "failed");
    // Cell must be in a table, so fail if not found
    return NS_ERROR_FAILURE;
  }
  if (aTable) {
    table.forget(aTable);
  }

  // Get the rest of the related data only if requested
  if (aRowIndex || aColumnIndex) {
    const RefPtr<PresShell> presShell{GetPresShell()};
    const CellIndexes cellIndexes(*cell, presShell);
    if (NS_WARN_IF(cellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    if (aRowIndex) {
      *aRowIndex = cellIndexes.mRow;
    }
    if (aColumnIndex) {
      *aColumnIndex = cellIndexes.mColumn;
    }
  }
  if (aCellParent) {
    // Get the immediate parent of the cell
    EditorRawDOMPoint atCellElement(cell);
    if (NS_WARN_IF(!atCellElement.IsSet())) {
      return NS_ERROR_FAILURE;
    }

    if (aCellOffset) {
      *aCellOffset = atCellElement.Offset();
    }

    // Now it's safe to hand over the reference to cellParent, since
    // we don't need it anymore.
    *aCellParent = do_AddRef(atCellElement.GetContainer()).take();
  }

  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetSelectedCells(
    nsTArray<RefPtr<Element>>& aOutSelectedCellElements) {
  MOZ_ASSERT(aOutSelectedCellElements.IsEmpty());

  AutoEditActionDataSetter editActionData(*this, EditAction::eGetSelectedCells);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetSelectedCells() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  SelectedTableCellScanner scanner(SelectionRef());
  if (!scanner.IsInTableCellSelectionMode()) {
    return NS_OK;
  }

  aOutSelectedCellElements.SetCapacity(scanner.ElementsRef().Length());
  for (const OwningNonNull<Element>& cellElement : scanner.ElementsRef()) {
    aOutSelectedCellElements.AppendElement(cellElement);
  }
  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::GetFirstSelectedCellInTable(int32_t* aRowIndex,
                                                      int32_t* aColumnIndex,
                                                      Element** aCellElement) {
  if (NS_WARN_IF(!aRowIndex) || NS_WARN_IF(!aColumnIndex) ||
      NS_WARN_IF(!aCellElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(
      *this, EditAction::eGetFirstSelectedCellInTable);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::GetFirstSelectedCellInTable() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should return NS_OK?
  }

  *aRowIndex = 0;
  *aColumnIndex = 0;
  *aCellElement = nullptr;
  RefPtr<Element> firstSelectedCellElement =
      HTMLEditUtils::GetFirstSelectedTableCellElement(SelectionRef());
  if (!firstSelectedCellElement) {
    return NS_OK;
  }

  RefPtr<PresShell> presShell = GetPresShell();
  const CellIndexes indexes(*firstSelectedCellElement, presShell);
  if (NS_WARN_IF(indexes.isErr())) {
    return NS_ERROR_FAILURE;
  }

  firstSelectedCellElement.forget(aCellElement);
  *aRowIndex = indexes.mRow;
  *aColumnIndex = indexes.mColumn;
  return NS_OK;
}

void HTMLEditor::SetSelectionAfterTableEdit(Element* aTable, int32_t aRow,
                                            int32_t aCol, int32_t aDirection,
                                            bool aSelected) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aTable) || NS_WARN_IF(Destroyed())) {
    return;
  }

  RefPtr<Element> cell;
  bool done = false;
  do {
    cell = GetTableCellElementAt(*aTable, aRow, aCol);
    if (cell) {
      if (aSelected) {
        // Reselect the cell
        DebugOnly<nsresult> rv = SelectContentInternal(*cell);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "HTMLEditor::SelectContentInternal() failed, but ignored");
        return;
      }

      // Set the caret to deepest first child
      //   but don't go into nested tables
      // TODO: Should we really be placing the caret at the END
      // of the cell content?
      CollapseSelectionToDeepestNonTableFirstChild(cell);
      return;
    }

    // Setup index to find another cell in the
    //   direction requested, but move in other direction if already at
    //   beginning of row or column
    switch (aDirection) {
      case ePreviousColumn:
        if (!aCol) {
          if (aRow > 0) {
            aRow--;
          } else {
            done = true;
          }
        } else {
          aCol--;
        }
        break;
      case ePreviousRow:
        if (!aRow) {
          if (aCol > 0) {
            aCol--;
          } else {
            done = true;
          }
        } else {
          aRow--;
        }
        break;
      default:
        done = true;
    }
  } while (!done);

  // We didn't find a cell
  // Set selection to just before the table
  if (aTable->GetParentNode()) {
    EditorRawDOMPoint atTable(aTable);
    if (NS_WARN_IF(!atTable.IsSetAndValid())) {
      return;
    }
    DebugOnly<nsresult> rvIgnored = CollapseSelectionTo(atTable);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
    return;
  }
  // Last resort: Set selection to start of doc
  // (it's very bad to not have a valid selection!)
  DebugOnly<nsresult> rvIgnored = SetSelectionAtDocumentStart();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "HTMLEditor::SetSelectionAtDocumentStart() failed, but ignored");
}

NS_IMETHODIMP HTMLEditor::GetSelectedOrParentTableElement(
    nsAString& aTagName, int32_t* aSelectedCount,
    Element** aCellOrRowOrTableElement) {
  if (NS_WARN_IF(!aSelectedCount) || NS_WARN_IF(!aCellOrRowOrTableElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  aTagName.Truncate();
  *aCellOrRowOrTableElement = nullptr;
  *aSelectedCount = 0;

  AutoEditActionDataSetter editActionData(
      *this, EditAction::eGetSelectedOrParentTableElement);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::GetSelectedOrParentTableElement() couldn't handle the "
        "job");
    return EditorBase::ToGenericNSResult(rv);
  }

  bool isCellSelected = false;
  Result<RefPtr<Element>, nsresult> cellOrRowOrTableElementOrError =
      GetSelectedOrParentTableElement(&isCellSelected);
  if (cellOrRowOrTableElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetSelectedOrParentTableElement() failed");
    return EditorBase::ToGenericNSResult(
        cellOrRowOrTableElementOrError.unwrapErr());
  }
  if (!cellOrRowOrTableElementOrError.inspect()) {
    return NS_OK;
  }
  RefPtr<Element> cellOrRowOrTableElement =
      cellOrRowOrTableElementOrError.unwrap();

  if (isCellSelected) {
    aTagName.AssignLiteral("td");
    *aSelectedCount = SelectionRef().RangeCount();
    cellOrRowOrTableElement.forget(aCellOrRowOrTableElement);
    return NS_OK;
  }

  if (HTMLEditUtils::IsTableCellElement(*cellOrRowOrTableElement)) {
    aTagName.AssignLiteral("td");
    // Keep *aSelectedCount as 0.
    cellOrRowOrTableElement.forget(aCellOrRowOrTableElement);
    return NS_OK;
  }

  if (cellOrRowOrTableElement->IsHTMLElement(nsGkAtoms::table)) {
    aTagName.AssignLiteral("table");
    *aSelectedCount = 1;
    cellOrRowOrTableElement.forget(aCellOrRowOrTableElement);
    return NS_OK;
  }

  if (HTMLEditUtils::IsTableRowElement(*cellOrRowOrTableElement)) {
    aTagName.AssignLiteral("tr");
    *aSelectedCount = 1;
    cellOrRowOrTableElement.forget(aCellOrRowOrTableElement);
    return NS_OK;
  }

  MOZ_ASSERT_UNREACHABLE("Which element was returned?");
  return NS_ERROR_UNEXPECTED;
}

Result<RefPtr<Element>, nsresult> HTMLEditor::GetSelectedOrParentTableElement(
    bool* aIsCellSelected /* = nullptr */) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (aIsCellSelected) {
    *aIsCellSelected = false;
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return Err(NS_ERROR_FAILURE);  // XXX Shouldn't throw an exception?
  }

  // Try to get the first selected cell, first.
  RefPtr<Element> cellElement =
      HTMLEditUtils::GetFirstSelectedTableCellElement(SelectionRef());
  if (cellElement) {
    if (aIsCellSelected) {
      *aIsCellSelected = true;
    }
    return cellElement;
  }

  const RangeBoundary& anchorRef = SelectionRef().AnchorRef();
  if (NS_WARN_IF(!anchorRef.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  // If anchor selects a <td>, <table> or <tr>, return it.
  if (anchorRef.GetContainer()->HasChildNodes()) {
    nsIContent* selectedContent = anchorRef.GetChildAtOffset();
    if (selectedContent) {
      // XXX Why do we ignore <th> element in this case?
      if (selectedContent->IsHTMLElement(nsGkAtoms::td)) {
        // FYI: If first range selects a <tr> element, but the other selects
        //      a <td> element, you can reach here.
        // Each cell is in its own selection range in this case.
        // XXX Although, other ranges may not select cells, though.
        if (aIsCellSelected) {
          *aIsCellSelected = true;
        }
        return RefPtr<Element>(selectedContent->AsElement());
      }
      if (selectedContent->IsAnyOfHTMLElements(nsGkAtoms::table,
                                               nsGkAtoms::tr)) {
        return RefPtr<Element>(selectedContent->AsElement());
      }
    }
  }

  if (NS_WARN_IF(!anchorRef.GetContainer()->IsContent())) {
    return RefPtr<Element>();
  }

  // Then, look for a cell element (either <td> or <th>) which contains
  // the anchor container.
  cellElement = GetInclusiveAncestorByTagNameInternal(
      *nsGkAtoms::td, *anchorRef.GetContainer()->AsContent());
  if (!cellElement) {
    return RefPtr<Element>();  // Not in table.
  }
  // Don't set *aIsCellSelected to true in this case because it does NOT
  // select a cell, just in a cell.
  return cellElement;
}

Result<RefPtr<Element>, nsresult>
HTMLEditor::GetFirstSelectedCellElementInTable() const {
  Result<RefPtr<Element>, nsresult> cellOrRowOrTableElementOrError =
      GetSelectedOrParentTableElement();
  if (cellOrRowOrTableElementOrError.isErr()) {
    NS_WARNING("HTMLEditor::GetSelectedOrParentTableElement() failed");
    return cellOrRowOrTableElementOrError;
  }

  if (!cellOrRowOrTableElementOrError.inspect()) {
    return cellOrRowOrTableElementOrError;
  }

  const RefPtr<Element>& element = cellOrRowOrTableElementOrError.inspect();
  if (!HTMLEditUtils::IsTableCellElement(*element)) {
    return RefPtr<Element>();
  }

  if (!HTMLEditUtils::IsTableRowElement(element->GetParent())) {
    NS_WARNING("There was no parent <tr> element for the found cell");
    return RefPtr<Element>();
  }

  if (!HTMLEditUtils::GetClosestAncestorTableElement(*element)) {
    NS_WARNING("There was no ancestor <table> element for the found cell");
    return Err(NS_ERROR_FAILURE);
  }
  return cellOrRowOrTableElementOrError;
}

NS_IMETHODIMP HTMLEditor::GetSelectedCellsType(Element* aElement,
                                               uint32_t* aSelectionType) {
  if (NS_WARN_IF(!aSelectionType)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aSelectionType = 0;

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eGetSelectedCellsType);
  nsresult rv = editActionData.CanHandleAndFlushPendingNotifications();
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetSelectedCellsType() couldn't handle the job");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (NS_WARN_IF(!SelectionRef().RangeCount())) {
    return NS_ERROR_FAILURE;  // XXX Should we just return NS_OK?
  }

  // Be sure we have a table element
  //  (if aElement is null, this uses selection's anchor node)
  RefPtr<Element> table;
  if (aElement) {
    table = GetInclusiveAncestorByTagNameInternal(*nsGkAtoms::table, *aElement);
    if (!table) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameInternal(nsGkAtoms::table) "
          "failed");
      return NS_ERROR_FAILURE;
    }
  } else {
    table = GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::table);
    if (!table) {
      NS_WARNING(
          "HTMLEditor::GetInclusiveAncestorByTagNameAtSelection(nsGkAtoms::"
          "table) failed");
      return NS_ERROR_FAILURE;
    }
  }

  const Result<TableSize, nsresult> tableSizeOrError =
      TableSize::Create(*this, *table);
  if (NS_WARN_IF(tableSizeOrError.isErr())) {
    return EditorBase::ToGenericNSResult(tableSizeOrError.inspectErr());
  }
  const TableSize& tableSize = tableSizeOrError.inspect();

  // Traverse all selected cells
  SelectedTableCellScanner scanner(SelectionRef());
  if (!scanner.IsInTableCellSelectionMode()) {
    return NS_OK;
  }

  // We have at least one selected cell, so set return value
  *aSelectionType = static_cast<uint32_t>(TableSelectionMode::Cell);

  // Store indexes of each row/col to avoid duplication of searches
  nsTArray<int32_t> indexArray;

  const RefPtr<PresShell> presShell{GetPresShell()};
  bool allCellsInRowAreSelected = false;
  for (const OwningNonNull<Element>& selectedCellElement :
       scanner.ElementsRef()) {
    // `MOZ_KnownLive(selectedCellElement)` is safe because `scanner` grabs
    // it until it's destroyed later.
    const CellIndexes selectedCellIndexes(MOZ_KnownLive(selectedCellElement),
                                          presShell);
    if (NS_WARN_IF(selectedCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }
    if (!indexArray.Contains(selectedCellIndexes.mColumn)) {
      indexArray.AppendElement(selectedCellIndexes.mColumn);
      allCellsInRowAreSelected = AllCellsInRowSelected(
          table, selectedCellIndexes.mRow, tableSize.mColumnCount);
      // We're done as soon as we fail for any row
      if (!allCellsInRowAreSelected) {
        break;
      }
    }
  }

  if (allCellsInRowAreSelected) {
    *aSelectionType = static_cast<uint32_t>(TableSelectionMode::Row);
    return NS_OK;
  }
  // Test for columns

  // Empty the indexArray
  indexArray.Clear();

  // Start at first cell again
  bool allCellsInColAreSelected = false;
  for (const OwningNonNull<Element>& selectedCellElement :
       scanner.ElementsRef()) {
    // `MOZ_KnownLive(selectedCellElement)` is safe because `scanner` grabs
    // it until it's destroyed later.
    const CellIndexes selectedCellIndexes(MOZ_KnownLive(selectedCellElement),
                                          presShell);
    if (NS_WARN_IF(selectedCellIndexes.isErr())) {
      return NS_ERROR_FAILURE;
    }

    if (!indexArray.Contains(selectedCellIndexes.mRow)) {
      indexArray.AppendElement(selectedCellIndexes.mColumn);
      allCellsInColAreSelected = AllCellsInColumnSelected(
          table, selectedCellIndexes.mColumn, tableSize.mRowCount);
      // We're done as soon as we fail for any column
      if (!allCellsInRowAreSelected) {
        break;
      }
    }
  }
  if (allCellsInColAreSelected) {
    *aSelectionType = static_cast<uint32_t>(TableSelectionMode::Column);
  }

  return NS_OK;
}

bool HTMLEditor::AllCellsInRowSelected(Element* aTable, int32_t aRowIndex,
                                       int32_t aNumberOfColumns) {
  if (NS_WARN_IF(!aTable)) {
    return false;
  }

  for (int32_t col = 0; col < aNumberOfColumns;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *aTable, aRowIndex, col);
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      return false;
    }

    // If no cell, we may have a "ragged" right edge, so return TRUE only if
    // we already found a cell in the row.
    // XXX So, this does not assume that CellData returns error when just
    //     not found a cell.  Fix this later.
    if (!cellData.mElement) {
      NS_WARNING("CellData didn't set mElement");
      return cellData.mCurrent.mColumn > 0;
    }

    // Return as soon as a non-selected cell is found.
    // XXX Odd, this is testing if each cell element is selected.  Why do
    //     we need to warn if it's false??
    if (!cellData.mIsSelected) {
      NS_WARNING("CellData didn't set mIsSelected");
      return false;
    }

    MOZ_ASSERT(col < cellData.NextColumnIndex());
    col = cellData.NextColumnIndex();
  }
  return true;
}

bool HTMLEditor::AllCellsInColumnSelected(Element* aTable, int32_t aColIndex,
                                          int32_t aNumberOfRows) {
  if (NS_WARN_IF(!aTable)) {
    return false;
  }

  for (int32_t row = 0; row < aNumberOfRows;) {
    const auto cellData =
        CellData::AtIndexInTableElement(*this, *aTable, row, aColIndex);
    if (NS_WARN_IF(cellData.FailedOrNotFound())) {
      return false;
    }

    // If no cell, we must have a "ragged" right edge on the last column so
    // return TRUE only if we already found a cell in the row.
    // XXX So, this does not assume that CellData returns error when just
    //     not found a cell.  Fix this later.
    if (!cellData.mElement) {
      NS_WARNING("CellData didn't set mElement");
      return cellData.mCurrent.mRow > 0;
    }

    // Return as soon as a non-selected cell is found.
    // XXX Odd, this is testing if each cell element is selected.  Why do
    //     we need to warn if it's false??
    if (!cellData.mIsSelected) {
      NS_WARNING("CellData didn't set mIsSelected");
      return false;
    }

    MOZ_ASSERT(row < cellData.NextRowIndex());
    row = cellData.NextRowIndex();
  }
  return true;
}

bool HTMLEditor::IsEmptyCell(dom::Element* aCell) {
  MOZ_ASSERT(aCell);

  // Check if target only contains empty text node or <br>
  nsCOMPtr<nsINode> cellChild = aCell->GetFirstChild();
  if (!cellChild) {
    return false;
  }

  nsCOMPtr<nsINode> nextChild = cellChild->GetNextSibling();
  if (nextChild) {
    return false;
  }

  // We insert a single break into a cell by default
  //   to have some place to locate a cursor -- it is dispensable
  if (cellChild->IsHTMLElement(nsGkAtoms::br)) {
    return true;
  }

  // Or check if no real content
  return HTMLEditUtils::IsEmptyNode(
      *cellChild, {EmptyCheckOption::TreatSingleBRElementAsVisible,
                   EmptyCheckOption::TreatNonEditableContentAsInvisible});
}

}  // namespace mozilla
