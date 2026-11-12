/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* some layout debugging functions that ought to live in nsFrame.cpp */

#include <stdio.h>

#include "nsAttrValue.h"
#include "nsDisplayList.h"
#include "nsIFrame.h"
#include "nsPrintfCString.h"

using namespace mozilla;
using namespace mozilla::layers;

static std::ostream& operator<<(std::ostream& os, const nsPrintfCString& rhs) {
  os << rhs.get();
  return os;
}

static void PrintDisplayListTo(nsDisplayListBuilder* aBuilder,
                               const nsDisplayList& aList,
                               std::stringstream& aStream, uint32_t aIndent,
                               bool aDumpHtml);

static void PrintDisplayItemTo(nsDisplayListBuilder* aBuilder,
                               nsDisplayItem* aItem, std::stringstream& aStream,
                               uint32_t aIndent, bool aDumpSublist,
                               bool aDumpHtml) {
  std::stringstream ss;

  if (!aDumpHtml) {
    for (uint32_t indent = 0; indent < aIndent; indent++) {
      aStream << "  ";
    }
  }
  nsAutoString contentData;
  nsIFrame* f = aItem->Frame();
#ifdef DEBUG_FRAME_DUMP
  f->GetFrameName(contentData);
#endif
  nsIContent* content = f->GetContent();
  if (content) {
    nsString tmp;
    if (content->GetID()) {
      content->GetID()->ToString(tmp);
      contentData.AppendLiteral(" id:");
      contentData.Append(tmp);
    }
    const nsAttrValue* classes =
        content->IsElement() ? content->AsElement()->GetClasses() : nullptr;
    if (classes) {
      classes->ToString(tmp);
      contentData.AppendLiteral(" class:");
      contentData.Append(tmp);
    }
  }
  bool snap;
  nsRect rect = aBuilder ? aItem->GetBounds(aBuilder, &snap) : nsRect();
  nsRect component =
      aBuilder ? aItem->GetComponentAlphaBounds(aBuilder) : nsRect();
  nsDisplayList* list = aItem->GetChildren();
  const DisplayItemClip& clip = aItem->GetClip();
  nsRegion opaque =
      aBuilder ? aItem->GetOpaqueRegion(aBuilder, &snap) : nsRect();

#ifdef MOZ_DUMP_PAINTING
  if (aDumpHtml && aItem->Painted()) {
    nsCString string(aItem->Name());
    string.Append('-');
    string.AppendInt((uint64_t)aItem);
    aStream << nsPrintfCString("<a href=\"javascript:ViewImage('%s')\">",
                               string.get());
  }
#endif

  aStream << nsPrintfCString(
      "%s p=0x%p f=0x%p(%s) key=%d %sbounds(%d,%d,%d,%d) "
      "componentAlpha(%d,%d,%d,%d) clip(%s) asr(%s) clipChain(%s) ",
      aItem->Name(), aItem, (void*)f, NS_ConvertUTF16toUTF8(contentData).get(),
      aItem->GetPerFrameKey(),
      (aItem->ZIndex() ? nsPrintfCString("z=%d ", aItem->ZIndex()).get() : ""),
      rect.x, rect.y, rect.width, rect.height, component.x, component.y,
      component.width, component.height, clip.ToString().get(),
      ActiveScrolledRoot::ToString(aItem->GetActiveScrolledRoot()).get(),
      DisplayItemClipChain::ToString(aItem->GetClipChain()).get());

  for (auto iter = opaque.RectIter(); !iter.Done(); iter.Next()) {
    const nsRect& r = iter.Get();
    aStream << nsPrintfCString(" (opaque %d,%d,%d,%d)", r.x, r.y, r.width,
                               r.height);
  }

  const auto& willChange = aItem->Frame()->StyleDisplay()->mWillChange;
  if (!willChange.features.IsEmpty()) {
    aStream << " (will-change=";
    for (size_t i = 0; i < willChange.features.Length(); i++) {
      if (i > 0) {
        aStream << ",";
      }
      nsDependentAtomString buffer(willChange.features.AsSpan()[i].AsAtom());
      aStream << NS_LossyConvertUTF16toASCII(buffer).get();
    }
    aStream << ")";
  }

  if (aItem->HasHitTestInfo()) {
    const auto& hitTestInfo = aItem->GetHitTestInfo();
    aStream << nsPrintfCString(" hitTestInfo(0x%x)",
                               hitTestInfo.Info().serialize());

    nsRect area = hitTestInfo.Area();
    aStream << nsPrintfCString(" hitTestArea(%d,%d,%d,%d)", area.x, area.y,
                               area.width, area.height);
  }

  auto ReuseStateToString = [](nsDisplayItem::ReuseState aState) {
    switch (aState) {
      case nsDisplayItem::ReuseState::None:
        return "None";
      case nsDisplayItem::ReuseState::Reusable:
        return "Reusable";
      case nsDisplayItem::ReuseState::PreProcessed:
        return "PreProcessed";
      case nsDisplayItem::ReuseState::Reused:
        return "Reused";
    }

    MOZ_ASSERT_UNREACHABLE();
    return "";
  };

  aStream << nsPrintfCString(" reuse-state(%s)",
                             ReuseStateToString(aItem->GetReuseState()));

  // Display item specific debug info
  aItem->WriteDebugInfo(aStream);

#ifdef MOZ_DUMP_PAINTING
  if (aDumpHtml && aItem->Painted()) {
    aStream << "</a>";
  }
#endif
#ifdef MOZ_DUMP_PAINTING
  if (aItem->GetType() == DisplayItemType::TYPE_MASK) {
    nsCString str;
    (static_cast<nsDisplayMasksAndClipPaths*>(aItem))->PrintEffects(str);
    aStream << str.get();
  }

  if (aItem->GetType() == DisplayItemType::TYPE_FILTER) {
    nsCString str;
    (static_cast<nsDisplayFilters*>(aItem))->PrintEffects(str);
    aStream << str.get();
  }
#endif
  aStream << "\n";
#ifdef MOZ_DUMP_PAINTING
  if (aDumpHtml && aItem->Painted()) {
    nsCString string(aItem->Name());
    string.Append('-');
    string.AppendInt((uint64_t)aItem);
    aStream << nsPrintfCString("<br><img id=\"%s\">\n", string.get());
  }
#endif

  if (aDumpSublist && list) {
    PrintDisplayListTo(aBuilder, *list, aStream, aIndent + 1, aDumpHtml);
  }
}

static void PrintDisplayListTo(nsDisplayListBuilder* aBuilder,
                               const nsDisplayList& aList,
                               std::stringstream& aStream, uint32_t aIndent,
                               bool aDumpHtml) {
  if (aDumpHtml) {
    aStream << "<ul>";
  }

  for (nsDisplayItem* i : aList) {
    if (aDumpHtml) {
      aStream << "<li>";
    }
    PrintDisplayItemTo(aBuilder, i, aStream, aIndent, true, aDumpHtml);
    if (aDumpHtml) {
      aStream << "</li>";
    }
  }

  if (aDumpHtml) {
    aStream << "</ul>";
  }
}

void nsIFrame::PrintDisplayList(nsDisplayListBuilder* aBuilder,
                                const nsDisplayList& aList, uint32_t aIndent,
                                bool aDumpHtml) {
  std::stringstream ss;
  PrintDisplayListTo(aBuilder, aList, ss, aIndent, aDumpHtml);
  fprintf_stderr(stderr, "%s", ss.str().c_str());
}

void nsIFrame::PrintDisplayList(nsDisplayListBuilder* aBuilder,
                                const nsDisplayList& aList,
                                std::stringstream& aStream, bool aDumpHtml) {
  PrintDisplayListTo(aBuilder, aList, aStream, 0, aDumpHtml);
}

void nsIFrame::PrintDisplayItem(nsDisplayListBuilder* aBuilder,
                                nsDisplayItem* aItem,
                                std::stringstream& aStream, uint32_t aIndent,
                                bool aDumpSublist, bool aDumpHtml) {
  PrintDisplayItemTo(aBuilder, aItem, aStream, aIndent, aDumpSublist,
                     aDumpHtml);
}

/**
 * The two functions below are intended to be called from a debugger.
 */
void PrintDisplayItemToStdout(nsDisplayListBuilder* aBuilder,
                              nsDisplayItem* aItem) {
  std::stringstream stream;
  PrintDisplayItemTo(aBuilder, aItem, stream, 0, true, false);
  puts(stream.str().c_str());
}

void PrintDisplayListToStdout(nsDisplayListBuilder* aBuilder,
                              const nsDisplayList& aList) {
  std::stringstream stream;
  PrintDisplayListTo(aBuilder, aList, stream, 0, false);
  puts(stream.str().c_str());
}

#ifdef MOZ_DUMP_PAINTING
static void PrintDisplayListSetItem(nsDisplayListBuilder* aBuilder,
                                    const char* aItemName,
                                    const nsDisplayList& aList,
                                    std::stringstream& aStream,
                                    bool aDumpHtml) {
  if (aDumpHtml) {
    aStream << "<li>";
  }
  aStream << aItemName << "\n";
  PrintDisplayListTo(aBuilder, aList, aStream, 0, aDumpHtml);
  if (aDumpHtml) {
    aStream << "</li>";
  }
}

void nsIFrame::PrintDisplayListSet(nsDisplayListBuilder* aBuilder,
                                   const nsDisplayListSet& aSet,
                                   std::stringstream& aStream, bool aDumpHtml) {
  if (aDumpHtml) {
    aStream << "<ul>";
  }
  PrintDisplayListSetItem(aBuilder, "[BorderBackground]",
                          *(aSet.BorderBackground()), aStream, aDumpHtml);
  PrintDisplayListSetItem(aBuilder, "[BlockBorderBackgrounds]",
                          *(aSet.BlockBorderBackgrounds()), aStream, aDumpHtml);
  PrintDisplayListSetItem(aBuilder, "[Floats]", *(aSet.Floats()), aStream,
                          aDumpHtml);
  PrintDisplayListSetItem(aBuilder, "[PositionedDescendants]",
                          *(aSet.PositionedDescendants()), aStream, aDumpHtml);
  PrintDisplayListSetItem(aBuilder, "[Outlines]", *(aSet.Outlines()), aStream,
                          aDumpHtml);
  PrintDisplayListSetItem(aBuilder, "[Content]", *(aSet.Content()), aStream,
                          aDumpHtml);
  if (aDumpHtml) {
    aStream << "</ul>";
  }
}

#endif
