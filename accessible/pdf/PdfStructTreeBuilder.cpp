/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/a11y/PdfStructTreeBuilder.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/a11y/DocAccessible.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/a11y/TableCellAccessible.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "skia/include/docs/SkPDFDocument.h"

namespace mozilla::a11y {

static void AccNameToPdfAlt(Accessible* aAcc,
                            SkPDF::StructureElementNode& aPdf) {
  nsAutoString name;
  aAcc->Name(name);
  if (!name.IsEmpty()) {
    aPdf.fAlt = SkString(NS_ConvertUTF16toUTF8(name).get());
  }
}

// We use an array rather than a map for two reasons:
// 1. There aren't likely to be many of these alive at once.
// 2. Some functions need to find a builder associated with a descendant
// WindowContext, not just the root.
// If this turns out to be a problem, we could include ids for the root
// WindowContext and any descendant WindowContexts in the map.
static StaticAutoPtr<nsTArray<PdfStructTreeBuilder>> sBuilders;

/* static */
void PdfStructTreeBuilder::Init(dom::WindowContext* aWindowContext) {
  if (!StaticPrefs::accessibility_tagged_pdf_output_enabled()) {
    return;
  }
  if (!sBuilders) {
    sBuilders = new nsTArray<PdfStructTreeBuilder>();
    if (NS_IsMainThread()) {
      ClearOnShutdown(&sBuilders);
    }
  }
  for (PdfStructTreeBuilder& builder : *sBuilders) {
    for (dom::WindowContext* ancestor =
             aWindowContext->GetParentWindowContext();
         ancestor; ancestor = ancestor->GetParentWindowContext()) {
      if (ancestor->InnerWindowId() == builder.mRootInnerWindowId) {
        // This is an OOP iframe associated with an existing builder.
        builder.InitInternal(aWindowContext);
        return;
      }
    }
  }
  // This is a new document being printed.
  auto builder = sBuilders->EmplaceBack(aWindowContext->InnerWindowId());
  builder->InitInternal(aWindowContext);
}

/* static */
PdfStructTreeBuilder* PdfStructTreeBuilder::Get(uint64_t aInnerWindowId) {
  if (!sBuilders) {
    return nullptr;
  }
  for (PdfStructTreeBuilder& builder : *sBuilders) {
    if (builder.mRootInnerWindowId == aInnerWindowId) {
      return &builder;
    }
  }
  return nullptr;
}

/* static */
void PdfStructTreeBuilder::Done(uint64_t aInnerWindowId) {
  if (!sBuilders) {
    return;
  }
  for (size_t i = 0; i < sBuilders->Length(); ++i) {
    if ((*sBuilders)[i].mRootInnerWindowId == aInnerWindowId) {
      sBuilders->RemoveElementAt(i);
      break;
    }
  }
}

/* static */
int PdfStructTreeBuilder::GetPdfId(uint64_t aInnerWindowId, uint64_t aAccId) {
  if (NS_WARN_IF(!NS_IsMainThread())) {
    return SkPDF::NodeID::Nothing;
  }
  if (!sBuilders || sBuilders->IsEmpty()) {
    return SkPDF::NodeID::Nothing;
  }
  if (aInnerWindowId == 0) {
    // This indicates that the following drawing instructions are not associated
    // with anything in the struct tree; e.g. page headers and footers.
    switch (aAccId) {
      case SpecialId::Nothing:
        return SkPDF::NodeID::Nothing;
      case SpecialId::PageHeader:
        return SkPDF::NodeID::PaginationHeaderArtifact;
      case SpecialId::PageFooter:
        return SkPDF::NodeID::PaginationFooterArtifact;
      case SpecialId::OtherArtifact:
        return SkPDF::NodeID::OtherArtifact;
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid special acc id");
    }
    return SkPDF::NodeID::Nothing;
  }
  // aInnerWindowId might be a descendant WindowContext. Rather than walking the
  // WindowContext ancestry for each builder, we just ask each builder whether
  // it contains this id, since there won't be many builders.
  for (const PdfStructTreeBuilder& builder : *sBuilders) {
    if (int pdfId = builder.GetPdfIdInternal(aInnerWindowId, aAccId)) {
      return pdfId;
    }
  }
  return 0;
}

/* static */
PdfStructTreeBuilder::GlobalAccessibleId PdfStructTreeBuilder::GetAccId(
    nsIFrame* aFrame) {
  if (!StaticPrefs::accessibility_tagged_pdf_output_enabled()) {
    return {};
  }
  nsIContent* content = aFrame->GetContent();
  if (!content) {
    return {};
  }
  dom::Document* doc = content->OwnerDoc();
  // This should only ever be called for a document being printed and those are
  // always static documents.
  MOZ_ASSERT(doc->IsStaticDocument());
  DocAccessible* docAcc = GetExistingDocAccessible(doc);
  if (!docAcc) {
    return {};
  }
  LocalAccessible* acc = docAcc->GetAccessible(content);
  if (!acc) {
    return {};
  }
  if (content->IsText()) {
    LocalAccessible* parent = acc->LocalParent();
    if (parent && parent->GetContent() &&
        parent->GetContent()->IsMathMLElement()) {
      // Inside MathML, we can't represent a TextLeafAccessible as a separate
      // PDF structure element, as NonStruct isn't valid in a PDF MathML
      // subtree. Therefore, map it to the parent Accessible. We check the
      // parent Accessible rather than the parent DOM element because invalid
      // markup (e.g. <mtext><mtext>) results in an intervening DOM node which
      // doesn't get an Accessible.
      acc = parent;
    }
  }
  uint64_t innerWindowId = doc->InnerWindowID();
  if (!innerWindowId) {
    return {};
  }
  return {innerWindowId, acc->ID()};
}

PdfStructTreeBuilder::PdfStructTreeBuilder(uint64_t aInnerWindowId)
    : mRootInnerWindowId(aInnerWindowId) {
  mReadyPromise = new ReadyPromise::Private(__func__);
}

void PdfStructTreeBuilder::InitInternal(dom::WindowContext* aWindowContext) {
  if (aWindowContext->InnerWindowId() != mRootInnerWindowId) {
    // We've just received the document for an out-of-process iframe.
    MOZ_ASSERT(mPendingOopIframes > 0);
    --mPendingOopIframes;
  }
  if (dom::BrowserParent* bp =
          aWindowContext->Canonical()->GetBrowserParent()) {
    // Request the accessibility tree for each descendant out-of-process
    // iframe. While all of the direct children should be reachable, some of the
    // deeper descendants might not be yet, so we also traverse the descendants
    // for each OOP iframe when InitInternal is called for it , skipping any
    // that have already been requested. We could instead walk only the direct
    // children, but walking descendants means we benefit from greater
    // parallelism in the case that deeper descendants *are* already reachable.
    bp->VisitAllDescendants([this](dom::BrowserParent* descBp) {
      if (mRequestedBrowserParentIds.EnsureInserted(descBp->GetTabId())) {
        MOZ_ASSERT(!descBp->GetTopLevelDocAccessible());
        (void)descBp->SendRequestDocAccessibleForPrint();
        ++mPendingOopIframes;
      }
    });
  }
  // XXX support out-of-process iframes inside a parent process document.
  if (mPendingOopIframes == 0) {
    // Once we've received all pending out-of-process iframes, we are ready to
    // build the PDF struct tree.
    mReadyPromise->Resolve(mozilla::Ok(), __func__);
  }
}

bool PdfStructTreeBuilder::BuildStructTree(SkPDF::StructureElementNode& aRoot) {
  MOZ_ASSERT(!mBuilt, "Shouldn't try to build the struct tree more than once");
  RefPtr wgp = dom::WindowGlobalParent::GetByInnerWindowId(mRootInnerWindowId);
  if (!wgp) {
    return false;
  }
  Accessible* rootAcc = nullptr;
  if (wgp->IsInProcess()) {
    if (dom::Document* doc = wgp->GetDocument()) {
      rootAcc = GetExistingDocAccessible(doc);
    }
  } else {
    rootAcc = DocAccessibleParent::GetFrom(wgp);
  }
  if (!rootAcc) {
    return false;
  }
  BuildStructSubtree(rootAcc, aRoot);
  mBuilt = true;
  return true;
}

// Returns the inner window id of the document containing aAcc. This must match
// the id computed by GetAccId in the process rendering that document.
static uint64_t InnerWindowIdFor(Accessible* aAcc) {
  if (RemoteAccessible* remoteAcc = aAcc->AsRemote()) {
    return remoteAcc->Document()->Manager()->InnerWindowId();
  }
  return aAcc->AsLocal()->Document()->DocumentNode()->InnerWindowID();
}

int PdfStructTreeBuilder::GeneratePdfId(Accessible* aAcc) {
  // This can be called more than once for the same Accessible; e.g. when
  // referencing table cell headers. It should always return the same id for the
  // same Accessible.
  GlobalAccessibleId key = {InnerWindowIdFor(aAcc), aAcc->ID()};
  auto entry = mAccToPdf.lookupForAdd(key);
  if (!entry) {
    // We haven't seen this Accessible before. Generate a new PDF id.
    MOZ_ALWAYS_TRUE(mAccToPdf.add(entry, key, ++mLastPdfId));
  }
  return entry->value();
}

void PdfStructTreeBuilder::BuildStructSubtree(Accessible* aAcc,
                                              SkPDF::StructureElementNode& aPdf,
                                              bool aInMath) {
  role accRole = aAcc->Role();
  if (!aInMath && accRole == roles::MATHML_MATH) {
    // MathML must be contained inside a Formula node. That doesn't exist in the
    // Gecko accessibility tree, so manufacture this wrapper here.
    aPdf.fNodeId = ++mLastPdfId;
    aPdf.fTypeString = "Formula";
    aPdf.fChildVector.resize(1);
    aPdf.fChildVector[0] = std::make_unique<SkPDF::StructureElementNode>();
    BuildStructSubtree(aAcc, *aPdf.fChildVector[0], true);
    return;
  }
  aPdf.fNodeId = GeneratePdfId(aAcc);
  // Per ISO 32000-2 14.8.6.1, every MathML element must have the MathML
  // namespace.
  const char kNamespaceMathMl[] = "http://www.w3.org/1998/Math/MathML";
  switch (accRole) {
    case roles::ARTICLE:
      aPdf.fTypeString = "Art";
      break;
    case roles::BLOCKQUOTE:
      aPdf.fTypeString = "BlockQuote";
      break;
    case roles::CAPTION:
      aPdf.fTypeString = "Caption";
      break;
    case roles::CELL:
    case roles::GRID_CELL: {
      aPdf.fTypeString = "TD";
      TableCellAccessible* cell = aAcc->AsTableCell();
      if (!cell) {
        break;
      }
      // Query each axis separately so one doesn't suppress the other's implicit
      // headers.
      nsTArray<Accessible*> accHeaders;
      cell->ColHeaderCells(&accHeaders);
      nsTArray<Accessible*> accRowHeaders;
      cell->RowHeaderCells(&accRowHeaders);
      accHeaders.AppendElements(std::move(accRowHeaders));
      std::vector<int> pdfHeaders;
      pdfHeaders.reserve(accHeaders.Length());
      for (Accessible* accHeader : accHeaders) {
        pdfHeaders.push_back(GeneratePdfId(accHeader));
      }
      aPdf.fAttributes.appendNodeIdArray("Table", "Headers", pdfHeaders);
      break;
    }
    case roles::CODE:
      aPdf.fTypeString = "Code";
      break;
    case roles::COLUMNHEADER:
      aPdf.fTypeString = "TH";
      aPdf.fAttributes.appendName("Table", "Scope", "Column");
      break;
    case roles::DOCUMENT:
      aPdf.fTypeString = "Document";
      // PDF/UA-2 requires the root Document element to be explicitly in the
      // PDF 2.0 namespace, rather than the default PDF 1.7 namespace.
      aPdf.fNamespace = "http://iso.org/pdf2/ssn";
      break;
    case roles::EMPHASIS:
      aPdf.fTypeString = "Em";
      break;
    case roles::GRID:
    case roles::TABLE:
    case roles::TREE_TABLE:
      aPdf.fTypeString = "Table";
      break;
    case roles::GROUPING:
      aPdf.fTypeString = "Div";
      break;
    case roles::GRAPHIC:
      aPdf.fTypeString = "Figure";
      AccNameToPdfAlt(aAcc, aPdf);
      // XXX We should ideally expose a BBox attribute, but how do we calculate
      // this?
      break;
    case roles::HEADING: {
      // For the PDF outline, SkPDF accumulates the title text itself from
      // the glyph runs drawn within the heading. However, if the accessible
      // name doesn't come from the heading's own content (e.g. aria-label),
      // that text never appears in the rendered glyphs, so provide it
      // explicitly as alt text in that case.
      nsAutoString name;
      if (aAcc->Name(name) != eNameFromSubtree && !name.IsEmpty()) {
        aPdf.fAlt = SkString(NS_ConvertUTF16toUTF8(name).get());
      }
      int32_t level = aAcc->GroupPosition().level;
      // PDF has H1 through H6.
      if (1 <= level && level <= 6) {
        nsAutoCString type;
        type.AppendPrintf("H%d", level);
        aPdf.fTypeString = SkString(type.get());
        break;
      }
      // Otherwise, use the generic H.
      aPdf.fTypeString = "H";
      break;
    }
    case roles::LANDMARK:
      if (aAcc->LandmarkRole() == nsGkAtoms::complementary) {
        aPdf.fTypeString = "Aside";
      }
      break;
    case roles::LINK:
      aPdf.fTypeString = "Link";
      break;
    case roles::LIST:
      aPdf.fTypeString = "L";
      break;
    case roles::LISTITEM:
      aPdf.fTypeString = "LI";
      break;
    case roles::LISTITEM_MARKER:
      aPdf.fTypeString = "Lbl";
      break;
    case roles::MATHML_CELL:
      aPdf.fTypeString = "mtd";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_EMPTY_GROUP:
    case roles::MATHML_ROW:
    case roles::MATHML_TEXT: {
      // These MathML roles are shared by several different elements which don't
      // have a common PDF tag, so we use the actual tag name instead.
      nsAutoString tagName;
      aAcc->TagName()->ToString(tagName);
      aPdf.fTypeString = SkString(NS_ConvertUTF16toUTF8(tagName).get());
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    }
    case roles::MATHML_ENCLOSED:
      aPdf.fTypeString = "menclose";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: notation
      break;
    case roles::MATHML_ERROR:
      aPdf.fTypeString = "merror";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_FRACTION:
      aPdf.fTypeString = "mfrac";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: bevelled, linethickness
      break;
    case roles::MATHML_GLYPH:
      aPdf.fTypeString = "mglyph";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_IDENTIFIER:
      aPdf.fTypeString = "mi";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_LABELED_ROW:
      aPdf.fTypeString = "mlabeledtr";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_LINK:
      aPdf.fTypeString = "a";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_LONG_DIVISION:
      aPdf.fTypeString = "mlongdiv";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: longdivstyle
      break;
    case roles::MATHML_MATH:
      aPdf.fTypeString = "math";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_MULTISCRIPTS:
      aPdf.fTypeString = "mmultiscripts";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_NUMBER:
      aPdf.fTypeString = "mn";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_OPERATOR:
      aPdf.fTypeString = "mo";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: accent, fence, separator, largeop
      break;
    case roles::MATHML_OVER:
      aPdf.fTypeString = "mover";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: accent, align
      break;
    case roles::MATHML_ROOT:
      aPdf.fTypeString = "mroot";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_SQUARE_ROOT:
      aPdf.fTypeString = "msqrt";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_STACK:
      aPdf.fTypeString = "mstack";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: align, position
      break;
    case roles::MATHML_STACK_CARRIES:
      aPdf.fTypeString = "mscarries";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: location, position
      break;
    case roles::MATHML_STACK_CARRY:
      aPdf.fTypeString = "mscarry";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: crossout
      break;
    case roles::MATHML_STACK_GROUP:
      aPdf.fTypeString = "msgroup";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: position, shift
      break;
    case roles::MATHML_STACK_LINE:
      aPdf.fTypeString = "msline";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: position
      break;
    case roles::MATHML_STACK_ROW:
      aPdf.fTypeString = "msrow";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: position
      break;
    case roles::MATHML_STYLE:
      aPdf.fTypeString = "mstyle";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_SUB:
      aPdf.fTypeString = "msub";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_SUB_SUP:
      aPdf.fTypeString = "msubsup";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_SUP:
      aPdf.fTypeString = "msup";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_TABLE:
      aPdf.fTypeString = "mtable";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: align, columnlines, rowlines
      break;
    case roles::MATHML_TABLE_ROW:
      aPdf.fTypeString = "mtr";
      aPdf.fNamespace = kNamespaceMathMl;
      break;
    case roles::MATHML_UNDER:
      aPdf.fTypeString = "munder";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: accentunder, align
      break;
    case roles::MATHML_UNDER_OVER:
      aPdf.fTypeString = "munderover";
      aPdf.fNamespace = kNamespaceMathMl;
      // XXX Expose attributes: accent, accentunder, align
      break;
    case roles::PARAGRAPH:
      aPdf.fTypeString = "P";
      break;
    case roles::ROW:
      aPdf.fTypeString = "TR";
      break;
    case roles::ROWHEADER:
      aPdf.fTypeString = "TH";
      aPdf.fAttributes.appendName("Table", "Scope", "Row");
      break;
    case roles::STRONG:
      aPdf.fTypeString = "Strong";
      break;
    default:
      aPdf.fTypeString = "NonStruct";
  }
  if (TableCellAccessible* cell = aAcc->AsTableCell()) {
    uint32_t rowSpan = cell->RowExtent();
    if (rowSpan > 1) {
      aPdf.fAttributes.appendInt("Table", "RowSpan", static_cast<int>(rowSpan));
    }
    uint32_t colSpan = cell->ColExtent();
    if (colSpan > 1) {
      aPdf.fAttributes.appendInt("Table", "ColSpan", static_cast<int>(colSpan));
    }
  }

  // Determine whether aAcc itself is a MathML element. In contrast, aInMath
  // tracks whether any ancestor is a MathML element. There's no way to ask this
  // directly for a RemoteAccessible, so we check the namespace we set above.
  const bool accIsMathMLElement = aPdf.fNamespace.equals(kNamespaceMathMl);
  uint32_t count = aAcc->ChildCount();
  aPdf.fChildVector.resize(count);
  for (uint32_t c = 0; c < count; ++c) {
    aPdf.fChildVector[c] = std::make_unique<SkPDF::StructureElementNode>();
    Accessible* child = aAcc->ChildAt(c);
    if (accIsMathMLElement && child->IsTextLeaf()) {
      // For MathML, we map a TextLeafAccessible to its parent Accessible. See
      // GetAccId().
      continue;
    }
    BuildStructSubtree(child, *aPdf.fChildVector[c], aInMath);
  }
}

int PdfStructTreeBuilder::GetPdfIdInternal(uint64_t aInnerWindowId,
                                           uint64_t aAccId) const {
  if (auto entry = mAccToPdf.lookup({aInnerWindowId, aAccId})) {
    return entry->value();
  }
  MOZ_ASSERT(!mBuilt,
             "Display list contains Accessible id which isn't in the map!");
  return 0;
}

}  // namespace mozilla::a11y
