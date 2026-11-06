/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLTableAccessible.h"

#include <stdint.h>

#include "ARIAMap.h"
#include "AccAttributes.h"
#include "CacheConstants.h"
#include "DocAccessible-inl.h"
#include "LocalAccessible-inl.h"
#include "Relation.h"
#include "States.h"
#include "mozilla/Assertions.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/a11y/TableAccessible.h"
#include "mozilla/a11y/TableCellAccessible.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "nsAccessibilityService.h"
#include "nsCOMPtr.h"
#include "nsCaseTreatment.h"
#include "nsColor.h"
#include "nsCoreUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsLiteralString.h"
#include "nsMargin.h"
#include "nsQueryFrame.h"
#include "nsSize.h"
#include "nsStringFwd.h"
#include "nsTableCellFrame.h"
#include "nsTableWrapperFrame.h"
#include "nsTextEquivUtils.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// HTMLTableCellAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLTableCellAccessible::HTMLTableCellAccessible(nsIContent* aContent,
                                                 DocAccessible* aDoc)
    : HyperTextAccessible(aContent, aDoc) {
  mType = eHTMLTableCellType;
  mGenericTypes |= eTableCell;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableCellAccessible: LocalAccessible implementation

role HTMLTableCellAccessible::NativeRole() const {
  // We implement this rather than using the markup maps because we only want
  // this role to be returned if this is a valid cell. An invalid cell (e.g. if
  // the table has role="none") won't use this class, so it will get a generic
  // role, since the markup map doesn't specify a role.
  if (mContent->IsMathMLElement(nsGkAtoms::mtd)) {
    return roles::MATHML_CELL;
  }
  return roles::CELL;
}

uint64_t HTMLTableCellAccessible::NativeState() const {
  uint64_t state = HyperTextAccessible::NativeState();

  nsIFrame* frame = mContent->GetPrimaryFrame();
  NS_ASSERTION(frame, "No frame for valid cell accessible!");

  if (frame && frame->IsSelected()) {
    state |= states::SELECTED;
  }

  return state;
}

already_AddRefed<AccAttributes> HTMLTableCellAccessible::NativeAttributes() {
  RefPtr<AccAttributes> attributes = HyperTextAccessible::NativeAttributes();

  // We only need to expose table-cell-index to clients. If we're in the content
  // process, we don't need this, so building a CachedTableAccessible is very
  // wasteful. This will be exposed by RemoteAccessible in the parent process
  // instead.
  if (!IPCAccessibilityActive()) {
    if (const TableCellAccessible* cell = AsTableCell()) {
      TableAccessible* table = cell->Table();
      const uint32_t row = cell->RowIdx();
      const uint32_t col = cell->ColIdx();
      const int32_t cellIdx = table->CellIndexAt(row, col);
      if (cellIdx != -1) {
        attributes->SetAttribute(nsGkAtoms::tableCellIndex, cellIdx);
      }
    }
  }

  // abbr attribute

  // Pick up object attribute from abbr DOM element (a child of the cell) or
  // from abbr DOM attribute.
  nsString abbrText;
  if (ChildCount() == 1) {
    LocalAccessible* abbr = LocalFirstChild();
    if (abbr->IsAbbreviation()) {
      nsIContent* firstChildNode = abbr->GetContent()->GetFirstChild();
      if (firstChildNode) {
        nsTextEquivUtils::AppendTextEquivFromTextContent(firstChildNode,
                                                         &abbrText);
      }
    }
  }
  if (abbrText.IsEmpty()) {
    mContent->AsElement()->GetAttr(nsGkAtoms::abbr, abbrText);
  }

  if (!abbrText.IsEmpty()) {
    attributes->SetAttribute(nsGkAtoms::abbr, std::move(abbrText));
  }

  // axis attribute
  nsString axisText;
  mContent->AsElement()->GetAttr(nsGkAtoms::axis, axisText);
  if (!axisText.IsEmpty()) {
    attributes->SetAttribute(nsGkAtoms::axis, std::move(axisText));
  }

  return attributes.forget();
}

void HTMLTableCellAccessible::DOMAttributeChanged(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute,
                                                  AttrModType aModType,
                                                  const nsAttrValue* aOldValue,
                                                  uint64_t aOldState) {
  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);

  if (aAttribute == nsGkAtoms::headers || aAttribute == nsGkAtoms::abbr ||
      aAttribute == nsGkAtoms::scope) {
    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_OBJECT_ATTRIBUTE_CHANGED,
                           this);
    if (HTMLTableAccessible* table = Table()) {
      // Modifying these attributes can also modify our table's classification
      // as either a layout or data table. Queue an update on the table itself
      // to re-compute our "layout guess"
      mDoc->QueueCacheUpdate(table, CacheDomain::Table);
    }
    mDoc->QueueCacheUpdate(this, CacheDomain::Table);
  } else if (aAttribute == nsGkAtoms::rowspan ||
             aAttribute == nsGkAtoms::colspan) {
    if (HTMLTableAccessible* table = Table()) {
      // Modifying these attributes can also modify our table's classification
      // as either a layout or data table. Queue an update on the table itself
      // to re-compute our "layout guess"
      mDoc->QueueCacheUpdate(table, CacheDomain::Table);
    }
    mDoc->QueueCacheUpdate(this, CacheDomain::Table);
  }
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableCellAccessible implementation

HTMLTableAccessible* HTMLTableCellAccessible::Table() const {
  LocalAccessible* parent = const_cast<HTMLTableCellAccessible*>(this);
  while ((parent = parent->LocalParent())) {
    if (parent->IsHTMLTable()) {
      return HTMLTableAccessible::GetFrom(parent);
    }
  }

  return nullptr;
}

uint32_t HTMLTableCellAccessible::ColExtent() const {
  nsTableCellFrame* cell = do_QueryFrame(GetFrame());
  if (!cell) {
    // This probably isn't a table according to the layout engine; e.g. it has
    // display: block.
    return 1;
  }
  nsTableFrame* table = cell->GetTableFrame();
  MOZ_ASSERT(table);
  return table->GetEffectiveColSpan(*cell);
}

uint32_t HTMLTableCellAccessible::RowExtent() const {
  nsTableCellFrame* cell = do_QueryFrame(GetFrame());
  if (!cell) {
    // This probably isn't a table according to the layout engine; e.g. it has
    // display: block.
    return 1;
  }
  nsTableFrame* table = cell->GetTableFrame();
  MOZ_ASSERT(table);
  return table->GetEffectiveRowSpan(*cell);
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableHeaderCellAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLTableHeaderCellAccessible::HTMLTableHeaderCellAccessible(
    nsIContent* aContent, DocAccessible* aDoc)
    : HTMLTableCellAccessible(aContent, aDoc) {}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableHeaderCellAccessible: LocalAccessible implementation

role HTMLTableHeaderCellAccessible::NativeRole() const {
  dom::Element* el = Elm();
  if (!el) {
    return roles::NOTHING;
  }

  // Check value of @scope attribute.
  static mozilla::dom::Element::AttrValuesArray scopeValues[] = {
      nsGkAtoms::col, nsGkAtoms::colgroup, nsGkAtoms::row, nsGkAtoms::rowgroup,
      nullptr};
  int32_t valueIdx = el->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::scope,
                                         scopeValues, eCaseMatters);

  switch (valueIdx) {
    case 0:
    case 1:
      return roles::COLUMNHEADER;
    case 2:
    case 3:
      return roles::ROWHEADER;
  }

  dom::Element* nextEl = el->GetNextElementSibling();
  dom::Element* prevEl = el->GetPreviousElementSibling();
  // If this is the only cell in its row, it's a column header.
  if (!nextEl && !prevEl) {
    return roles::COLUMNHEADER;
  }
  const bool nextIsHeader = nextEl && nsCoreUtils::IsHTMLTableHeader(nextEl);
  const bool prevIsHeader = prevEl && nsCoreUtils::IsHTMLTableHeader(prevEl);
  // If this has a header on both sides, it is a column header.
  if (prevIsHeader && nextIsHeader) {
    return roles::COLUMNHEADER;
  }
  // If this has a header on one side and only a single normal cell on the
  // other, it's a column header.
  if (nextIsHeader && prevEl && !prevEl->GetPreviousElementSibling()) {
    return roles::COLUMNHEADER;
  }
  if (prevIsHeader && nextEl && !nextEl->GetNextElementSibling()) {
    return roles::COLUMNHEADER;
  }
  // If this has a normal cell next to it, it 's a row header.
  if ((nextEl && !nextIsHeader) || (prevEl && !prevIsHeader)) {
    return roles::ROWHEADER;
  }
  // If this has a row span, it could be a row header.
  if (RowExtent() > 1) {
    // It isn't a row header if it has 1 or more consecutive headers next to it.
    if (prevIsHeader &&
        (!prevEl->GetPreviousElementSibling() ||
         nsCoreUtils::IsHTMLTableHeader(prevEl->GetPreviousElementSibling()))) {
      return roles::COLUMNHEADER;
    }
    if (nextIsHeader &&
        (!nextEl->GetNextElementSibling() ||
         nsCoreUtils::IsHTMLTableHeader(nextEl->GetNextElementSibling()))) {
      return roles::COLUMNHEADER;
    }
    return roles::ROWHEADER;
  }
  // Otherwise, assume it's a column header.
  return roles::COLUMNHEADER;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableAccessible
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// HTMLTableAccessible: LocalAccessible

bool HTMLTableAccessible::InsertChildAt(uint32_t aIndex,
                                        LocalAccessible* aChild) {
  // Move caption accessible so that it's the first child. Check for the first
  // caption only, because nsAccessibilityService ensures we don't create
  // accessibles for the other captions, since only the first is actually
  // visible.
  bool isOurCaption =
      aChild->IsHTMLCaption() && mContent == aChild->GetContent()->GetParent();
  return HyperTextAccessible::InsertChildAt(isOurCaption ? 0 : aIndex, aChild);
}

uint64_t HTMLTableAccessible::NativeState() const {
  return LocalAccessible::NativeState() | states::READONLY;
}

ENameValueFlag HTMLTableAccessible::NativeName(nsString& aName) const {
  ENameValueFlag nameFlag = LocalAccessible::NativeName(aName);
  if (!aName.IsEmpty()) {
    return nameFlag;
  }

  // Use table caption as a name.
  LocalAccessible* caption = Caption();
  if (caption) {
    nsIContent* captionContent = caption->GetContent();
    if (captionContent) {
      bool usedHiddenContent = nsTextEquivUtils::AppendTextEquivFromContent(
          this, captionContent, &aName);
      aName.CompressWhitespace();
      if (!aName.IsEmpty()) {
        return usedHiddenContent ? eNameOK : eNameFromRelations;
      }
    }
  }

  // If no caption then use summary as a name.
  mContent->AsElement()->GetAttr(nsGkAtoms::summary, aName);
  return eNameOK;
}

void HTMLTableAccessible::DOMAttributeChanged(int32_t aNameSpaceID,
                                              nsAtom* aAttribute,
                                              AttrModType aModType,
                                              const nsAttrValue* aOldValue,
                                              uint64_t aOldState) {
  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);

  if (aAttribute == nsGkAtoms::summary) {
    nsAutoString name;
    ARIAName(name);
    if (name.IsEmpty()) {
      if (!Caption()) {
        // XXX: Should really be checking if caption provides a name.
        mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
      }
    }

    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_OBJECT_ATTRIBUTE_CHANGED,
                           this);
    mDoc->QueueCacheUpdate(this, CacheDomain::Table);
  }
}

already_AddRefed<AccAttributes> HTMLTableAccessible::NativeAttributes() {
  RefPtr<AccAttributes> attributes = AccessibleWrap::NativeAttributes();

  if (mContent->IsMathMLElement(nsGkAtoms::mtable)) {
    GetAccService()->MarkupAttributes(this, attributes);
  }

  if (IsProbablyLayoutTable()) {
    attributes->SetAttribute(nsGkAtoms::layout_guess, true);
  }

  return attributes.forget();
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableAccessible: LocalAccessible

Relation HTMLTableAccessible::RelationByType(RelationType aType) const {
  Relation rel = AccessibleWrap::RelationByType(aType);
  if (aType == RelationType::LABELLED_BY) {
    rel.AppendTarget(Caption());
  }

  return rel;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableAccessible: Table

LocalAccessible* HTMLTableAccessible::Caption() const {
  LocalAccessible* child = mChildren.SafeElementAt(0, nullptr);
  return child && child->IsHTMLCaption() ? child : nullptr;
}

uint32_t HTMLTableAccessible::ColCount() const {
  nsTableWrapperFrame* tableFrame = GetTableWrapperFrame();
  return tableFrame ? tableFrame->GetColCount() : 0;
}

uint32_t HTMLTableAccessible::RowCount() {
  nsTableWrapperFrame* tableFrame = GetTableWrapperFrame();
  return tableFrame ? tableFrame->GetRowCount() : 0;
}

bool HTMLTableAccessible::IsProbablyLayoutTable() {
  // Implement a heuristic to determine if table is most likely used for layout.

  // XXX do we want to look for rowspan or colspan, especialy that span all but
  // a couple cells  at the beginning or end of a row/col, and especially when
  // they occur at the edge of a table?

  // XXX For now debugging descriptions are always on via SHOW_LAYOUT_HEURISTIC
  // This will allow release trunk builds to be used by testers to refine
  // the algorithm. Integrate it into Logging.
  // Change to |#define SHOW_LAYOUT_HEURISTIC DEBUG| before final release
#ifdef SHOW_LAYOUT_HEURISTIC
#  define RETURN_LAYOUT_ANSWER(isLayout, heuristic)                         \
    {                                                                       \
      mLayoutHeuristic = isLayout                                           \
                             ? nsLiteralString(u"layout table: " heuristic) \
                             : nsLiteralString(u"data table: " heuristic);  \
      return isLayout;                                                      \
    }
#else
#  define RETURN_LAYOUT_ANSWER(isLayout, heuristic) \
    {                                               \
      return isLayout;                              \
    }
#endif

  MOZ_ASSERT(!IsDefunct(), "Table accessible should not be defunct");

  // Need to see all elements while document is being edited.
  if (Document()->State() & states::EDITABLE) {
    RETURN_LAYOUT_ANSWER(false, "In editable document");
  }

  // Check to see if an ARIA role overrides the role from native markup,
  // but for which we still expose table semantics (treegrid, for example).
  if (HasARIARole()) {
    RETURN_LAYOUT_ANSWER(false, "Has role attribute");
  }

  dom::Element* el = Elm();
  if (el->IsMathMLElement(nsGkAtoms::mtable)) {
    RETURN_LAYOUT_ANSWER(false, "MathML matrix");
  }

  MOZ_ASSERT(el->IsHTMLElement(nsGkAtoms::table),
             "Table should not be built by CSS display:table style");

  // Check if datatable attribute has "0" value.
  if (el->AttrValueIs(kNameSpaceID_None, nsGkAtoms::datatable, u"0"_ns,
                      eCaseMatters)) {
    RETURN_LAYOUT_ANSWER(true, "Has datatable = 0 attribute, it's for layout");
  }

  // Check for legitimate data table attributes.
  if (el->Element::HasNonEmptyAttr(nsGkAtoms::summary)) {
    RETURN_LAYOUT_ANSWER(false, "Has summary -- legitimate table structures");
  }

  // Check for legitimate data table elements.
  LocalAccessible* caption = LocalFirstChild();
  if (caption && caption->IsHTMLCaption() && caption->HasChildren()) {
    RETURN_LAYOUT_ANSWER(false,
                         "Not empty caption -- legitimate table structures");
  }

  for (nsIContent* childElm = el->GetFirstChild(); childElm;
       childElm = childElm->GetNextSibling()) {
    if (!childElm->IsHTMLElement()) continue;

    if (childElm->IsAnyOfHTMLElements(nsGkAtoms::col, nsGkAtoms::colgroup,
                                      nsGkAtoms::tfoot, nsGkAtoms::thead)) {
      RETURN_LAYOUT_ANSWER(
          false,
          "Has col, colgroup, tfoot or thead -- legitimate table structures");
    }

    if (childElm->IsHTMLElement(nsGkAtoms::tbody)) {
      for (nsIContent* rowElm = childElm->GetFirstChild(); rowElm;
           rowElm = rowElm->GetNextSibling()) {
        if (rowElm->IsHTMLElement(nsGkAtoms::tr)) {
          if (LocalAccessible* row = Document()->GetAccessible(rowElm)) {
            if (const nsRoleMapEntry* roleMapEntry = row->ARIARoleMap()) {
              if (roleMapEntry->role != roles::ROW) {
                RETURN_LAYOUT_ANSWER(true, "Repurposed tr with different role");
              }
            }
          }

          for (nsIContent* cellElm = rowElm->GetFirstChild(); cellElm;
               cellElm = cellElm->GetNextSibling()) {
            if (cellElm->IsHTMLElement()) {
              if (cellElm->NodeInfo()->Equals(nsGkAtoms::th)) {
                RETURN_LAYOUT_ANSWER(false,
                                     "Has th -- legitimate table structures");
              }

              if (cellElm->AsElement()->HasAttr(nsGkAtoms::headers) ||
                  cellElm->AsElement()->HasAttr(nsGkAtoms::scope) ||
                  cellElm->AsElement()->HasAttr(nsGkAtoms::abbr)) {
                RETURN_LAYOUT_ANSWER(false,
                                     "Has headers, scope, or abbr attribute -- "
                                     "legitimate table structures");
              }

              if (LocalAccessible* cell = Document()->GetAccessible(cellElm)) {
                if (const nsRoleMapEntry* roleMapEntry = cell->ARIARoleMap()) {
                  if (roleMapEntry->role != roles::CELL &&
                      roleMapEntry->role != roles::COLUMNHEADER &&
                      roleMapEntry->role != roles::ROWHEADER &&
                      roleMapEntry->role != roles::GRID_CELL) {
                    RETURN_LAYOUT_ANSWER(true,
                                         "Repurposed cell with different role");
                  }
                }
                if (cell->ChildCount() == 1 &&
                    cell->LocalFirstChild()->IsAbbreviation()) {
                  RETURN_LAYOUT_ANSWER(
                      false, "has abbr -- legitimate table structures");
                }
              }
            }
          }
        }
      }
    }
  }

  // If only 1 column or only 1 row, it's for layout.
  auto colCount = ColCount();
  if (colCount <= 1) {
    RETURN_LAYOUT_ANSWER(true, "Has only 1 column");
  }
  auto rowCount = RowCount();
  if (rowCount <= 1) {
    RETURN_LAYOUT_ANSWER(true, "Has only 1 row");
  }

  // Check for many columns.
  if (colCount >= 5) {
    RETURN_LAYOUT_ANSWER(false, ">=5 columns");
  }

  // Now we know there are 2-4 columns and 2 or more rows. Check to see if
  // there are visible borders on the cells.
  // XXX currently, we just check the first cell -- do we really need to do
  // more?
  nsTableWrapperFrame* tableFrame = do_QueryFrame(el->GetPrimaryFrame());
  if (!tableFrame) {
    RETURN_LAYOUT_ANSWER(false, "table with no frame!");
  }

  nsIFrame* cellFrame = tableFrame->GetCellFrameAt(0, 0);
  if (!cellFrame) {
    RETURN_LAYOUT_ANSWER(false, "table's first cell has no frame!");
  }

  nsMargin border = cellFrame->StyleBorder()->GetComputedBorder();
  if (border.top || border.bottom || border.left || border.right) {
    RETURN_LAYOUT_ANSWER(false, "Has nonzero border-width on table cell");
  }

  // Check for nested tables.
  RefPtr<HTMLCollection> nestedTables = el->GetElementsByTagName(u"table"_ns);
  if (nestedTables->Length() > 0) {
    RETURN_LAYOUT_ANSWER(true, "Has a nested table within it");
  }

  // Rules for non-bordered tables with 2-4 columns and 2+ rows from here on
  // forward.

  // Check for styled background color across rows (alternating background
  // color is a common feature for data tables).
  auto childCount = ChildCount();
  nscolor rowColor = 0;
  nscolor prevRowColor;
  for (auto childIdx = 0U; childIdx < childCount; childIdx++) {
    LocalAccessible* child = LocalChildAt(childIdx);
    if (child->IsHTMLTableRow()) {
      prevRowColor = rowColor;
      nsIFrame* rowFrame = child->GetFrame();
      MOZ_ASSERT(rowFrame, "Table hierarchy got screwed up");
      if (!rowFrame) {
        RETURN_LAYOUT_ANSWER(false, "Unexpected table hierarchy");
      }

      rowColor = rowFrame->StyleBackground()->BackgroundColor(rowFrame);

      if (childIdx > 0 && prevRowColor != rowColor) {
        RETURN_LAYOUT_ANSWER(false,
                             "2 styles of row background color, non-bordered");
      }
    }
  }

  // Check for many rows.
  const uint32_t kMaxLayoutRows = 20;
  if (rowCount > kMaxLayoutRows) {  // A ton of rows, this is probably for data
    RETURN_LAYOUT_ANSWER(false, ">= kMaxLayoutRows (20) and non-bordered");
  }

  // Check for very wide table.
  nsIFrame* documentFrame = Document()->GetFrame();
  nsSize documentSize = documentFrame->GetSize();
  if (documentSize.width > 0) {
    nsSize tableSize = GetFrame()->GetSize();
    int32_t percentageOfDocWidth = (100 * tableSize.width) / documentSize.width;
    if (percentageOfDocWidth > 95) {
      // 3-4 columns, no borders, not a lot of rows, and 95% of the doc's width
      // Probably for layout
      RETURN_LAYOUT_ANSWER(
          true, "<= 4 columns, table width is 95% of document width");
    }
  }

  // Two column rules.
  if (rowCount * colCount <= 10) {
    RETURN_LAYOUT_ANSWER(true, "2-4 columns, 10 cells or less, non-bordered");
  }

  static const nsLiteralString tags[] = {u"embed"_ns, u"object"_ns,
                                         u"iframe"_ns};
  for (const auto& tag : tags) {
    RefPtr<HTMLCollection> descendants = el->GetElementsByTagName(tag);
    if (descendants->Length() > 0) {
      RETURN_LAYOUT_ANSWER(true,
                           "Has no borders, and has iframe, object or embed, "
                           "typical of advertisements");
    }
  }

  RETURN_LAYOUT_ANSWER(false,
                       "No layout factor strong enough, so will guess data");
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTableAccessible: protected implementation

EDescriptionValueFlag HTMLTableAccessible::Description(
    nsString& aDescription) const {
  // Helpful for debugging layout vs. data tables
  aDescription.Truncate();
  EDescriptionValueFlag descFlag = LocalAccessible::Description(aDescription);
  if (!aDescription.IsEmpty()) {
    return descFlag;
  }

  // Use summary as description if it weren't used as a name.
  // XXX: get rid code duplication with NameInternal().
  LocalAccessible* caption = Caption();
  if (caption) {
    nsIContent* captionContent = caption->GetContent();
    if (captionContent) {
      nsAutoString captionText;
      nsTextEquivUtils::AppendTextEquivFromContent(this, captionContent,
                                                   &captionText);

      if (!captionText.IsEmpty()) {  // summary isn't used as a name.
        mContent->AsElement()->GetAttr(nsGkAtoms::summary, aDescription);
      }
    }
  }

#ifdef SHOW_LAYOUT_HEURISTIC
  if (aDescription.IsEmpty()) {
    bool isProbablyForLayout = IsProbablyLayoutTable();
    aDescription = mLayoutHeuristic;
  }
  printf("\nTABLE: %s\n", NS_ConvertUTF16toUTF8(mLayoutHeuristic).get());
#endif

  return eDescriptionOK;
}

nsTableWrapperFrame* HTMLTableAccessible::GetTableWrapperFrame() const {
  nsTableWrapperFrame* tableFrame = do_QueryFrame(mContent->GetPrimaryFrame());
  if (tableFrame && tableFrame->InnerTableFrame()) {
    return tableFrame;
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLCaptionAccessible
////////////////////////////////////////////////////////////////////////////////

Relation HTMLCaptionAccessible::RelationByType(RelationType aType) const {
  Relation rel = HyperTextAccessible::RelationByType(aType);
  if (aType == RelationType::LABEL_FOR) {
    rel.AppendTarget(LocalParent());
  }

  return rel;
}

role HTMLCaptionAccessible::NativeRole() const { return roles::CAPTION; }
