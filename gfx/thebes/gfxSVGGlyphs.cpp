/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxSVGGlyphs.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PresShell.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/SVGContextPaint.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/dom/SVGDocument.h"
#include "nsError.h"
#include "nsString.h"
#include "nsICategoryManager.h"
#include "nsIDocumentLoaderFactory.h"
#include "nsIDocumentViewer.h"
#include "nsIStreamListener.h"
#include "nsServiceManagerUtils.h"
#include "nsNetUtil.h"
#include "nsIInputStream.h"
#include "nsStringStream.h"
#include "nsStreamUtils.h"
#include "nsIPrincipal.h"
#include "nsContentUtils.h"
#include "gfxFont.h"
#include "gfxContext.h"
#include "harfbuzz/hb.h"
#include "zlib.h"

#define SVG_CONTENT_TYPE "image/svg+xml"_ns
#define UTF8_CHARSET "utf-8"_ns

using namespace mozilla;
using mozilla::dom::Document;
using mozilla::dom::Element;

gfxSVGGlyphs::gfxSVGGlyphs(hb_blob_t* aSVGTable, gfxFontEntry* aFontEntry)
    : mSVGData(aSVGTable), mFontEntry(aFontEntry) {
  unsigned int length;
  const char* svgData = hb_blob_get_data(mSVGData, &length);
  mHeader = reinterpret_cast<const Header*>(svgData);
  mDocIndex = nullptr;

  if (sizeof(Header) <= length && uint16_t(mHeader->mVersion) == 0 &&
      uint64_t(mHeader->mDocIndexOffset) + 2 <= length) {
    const DocIndex* docIndex =
        reinterpret_cast<const DocIndex*>(svgData + mHeader->mDocIndexOffset);
    // Limit the number of documents to avoid overflow
    if (uint64_t(mHeader->mDocIndexOffset) + 2 +
            uint16_t(docIndex->mNumEntries) * sizeof(IndexEntry) <=
        length) {
      mDocIndex = docIndex;
    }
  }
}

gfxSVGGlyphs::~gfxSVGGlyphs() { hb_blob_destroy(mSVGData); }

void gfxSVGGlyphs::DidRefresh() { mFontEntry->NotifyGlyphsChanged(); }

/*
 * Comparison operator for finding a range containing a given glyph ID. Simply
 *   checks whether |key| is less (greater) than every element of |range|, in
 *   which case return |key| < |range| (|key| > |range|). Otherwise |key| is in
 *   |range|, in which case return equality.
 * The total ordering here is guaranteed by
 *   (1) the index ranges being disjoint; and
 *   (2) the (sole) key always being a singleton, so intersection => containment
 *       (note that this is wrong if we have more than one intersection or two
 *        sets intersecting of size > 1 -- so... don't do that)
 */
/* static */
int gfxSVGGlyphs::CompareIndexEntries(const void* aKey, const void* aEntry) {
  const uint32_t key = *(uint32_t*)aKey;
  const IndexEntry* entry = (const IndexEntry*)aEntry;

  if (key < uint16_t(entry->mStartGlyph)) {
    return -1;
  }
  if (key > uint16_t(entry->mEndGlyph)) {
    return 1;
  }
  return 0;
}

gfxSVGGlyphsDocument* gfxSVGGlyphs::FindOrCreateGlyphsDocument(
    uint32_t aGlyphId) {
  if (!mDocIndex) {
    // Invalid table
    return nullptr;
  }

  IndexEntry* entry = (IndexEntry*)bsearch(
      &aGlyphId, mDocIndex->mEntries, uint16_t(mDocIndex->mNumEntries),
      sizeof(IndexEntry), CompareIndexEntries);
  if (!entry) {
    return nullptr;
  }

  return mGlyphDocs.WithEntryHandle(
      entry->mDocOffset, [&](auto&& glyphDocsEntry) -> gfxSVGGlyphsDocument* {
        if (!glyphDocsEntry) {
          unsigned int length;
          const uint8_t* data =
              (const uint8_t*)hb_blob_get_data(mSVGData, &length);
          if (entry->mDocOffset > 0 && uint64_t(mHeader->mDocIndexOffset) +
                                               entry->mDocOffset +
                                               entry->mDocLength <=
                                           length) {
            return glyphDocsEntry
                .Insert(MakeUnique<gfxSVGGlyphsDocument>(
                    data + mHeader->mDocIndexOffset + entry->mDocOffset,
                    entry->mDocLength, this))
                .get();
          }

          return nullptr;
        }

        return glyphDocsEntry->get();
      });
}

nsresult gfxSVGGlyphsDocument::SetupPresentation() {
  nsCOMPtr<nsIDocumentLoaderFactory> docLoaderFactory =
      nsContentUtils::FindInternalDocumentViewer(SVG_CONTENT_TYPE);
  NS_ASSERTION(docLoaderFactory, "Couldn't get DocumentLoaderFactory");

  nsCOMPtr<nsIDocumentViewer> viewer;
  nsresult rv = docLoaderFactory->CreateInstanceForDocument(
      nullptr, mDocument, nullptr, getter_AddRefs(viewer));
  NS_ENSURE_SUCCESS(rv, rv);

  auto upem = mOwner->FontEntry()->UnitsPerEm();
  rv = viewer->Init(nullptr, LayoutDeviceIntRect(0, 0, upem, upem), nullptr);
  if (NS_SUCCEEDED(rv)) {
    rv = viewer->Open(nullptr, nullptr);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  RefPtr<PresShell> presShell = viewer->GetPresShell();
  if (!presShell->DidInitialize()) {
    rv = presShell->Initialize();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mDocument->FlushPendingNotifications(FlushType::Layout);

  if (mDocument->HasAnimationController()) {
    mDocument->GetAnimationController()->Resume(SMILTimeContainer::PAUSE_IMAGE);
  }
  mDocument->ImageTracker()->SetAnimatingState(true);

  mViewer = viewer;
  mPresShell = presShell;
  mPresShell->AddPostRefreshObserver(this);

  return NS_OK;
}

void gfxSVGGlyphsDocument::DidRefresh() { mOwner->DidRefresh(); }

/**
 * Walk the DOM tree to find all glyph elements and insert them into the lookup
 * table
 * @param aElem The element to search from
 */
void gfxSVGGlyphsDocument::FindGlyphElements(Element* aElem) {
  for (nsIContent* child = aElem->GetLastChild(); child;
       child = child->GetPreviousSibling()) {
    if (!child->IsElement()) {
      continue;
    }
    FindGlyphElements(child->AsElement());
  }

  InsertGlyphId(aElem);
}

/**
 * If there exists an SVG glyph with the specified glyph id, render it and
 * return true If no such glyph exists, or in the case of an error return false
 * @param aContext The thebes aContext to draw to
 * @param aGlyphId The glyph id
 * @return true iff rendering succeeded
 */
void gfxSVGGlyphs::RenderGlyph(gfxContext* aContext, uint32_t aGlyphId,
                               SVGContextPaint* aContextPaint) {
  gfxContextAutoSaveRestore aContextRestorer(aContext);

  Element* glyph = mGlyphIdMap.Get(aGlyphId);
  MOZ_ASSERT(glyph, "No glyph element. Should check with HasSVGGlyph() first!");

  AutoSetRestoreSVGContextPaint autoSetRestore(aContextPaint,
                                               glyph->OwnerDoc());

  SVGUtils::PaintSVGGlyph(glyph, aContext);

#if DEBUG
  // This will not have any effect, because we're about to restore the state
  // via the aContextRestorer destructor, but it prevents debug builds from
  // asserting if it turns out that PaintSVGGlyph didn't actually do anything.
  // This happens if the SVG document consists of just an image, and the image
  // hasn't finished loading yet so we can't draw it.
  aContext->SetOp(gfx::CompositionOp::OP_OVER);
#endif
}

bool gfxSVGGlyphs::GetGlyphExtents(uint32_t aGlyphId,
                                   const gfxMatrix& aSVGToAppSpace,
                                   gfxRect* aResult) {
  Element* glyph = mGlyphIdMap.Get(aGlyphId);
  NS_ASSERTION(glyph,
               "No glyph element. Should check with HasSVGGlyph() first!");

  return SVGUtils::GetSVGGlyphExtents(glyph, aSVGToAppSpace, aResult);
}

Element* gfxSVGGlyphs::GetGlyphElement(uint32_t aGlyphId) {
  return mGlyphIdMap.LookupOrInsertWith(aGlyphId, [&] {
    Element* elem = nullptr;
    if (gfxSVGGlyphsDocument* set = FindOrCreateGlyphsDocument(aGlyphId)) {
      elem = set->GetGlyphElement(aGlyphId);
    }
    return elem;
  });
}

bool gfxSVGGlyphs::HasSVGGlyph(uint32_t aGlyphId) {
  return !!GetGlyphElement(aGlyphId);
}

size_t gfxSVGGlyphs::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  // We don't include the size of mSVGData here, because (depending on the
  // font backend implementation) it will either wrap a block of data owned
  // by the system (and potentially shared), or a table that's in our font
  // table cache and therefore already counted.
  size_t result = aMallocSizeOf(this) +
                  mGlyphDocs.ShallowSizeOfExcludingThis(aMallocSizeOf) +
                  mGlyphIdMap.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mGlyphDocs.Values()) {
    result += entry->SizeOfIncludingThis(aMallocSizeOf);
  }
  return result;
}

Element* gfxSVGGlyphsDocument::GetGlyphElement(uint32_t aGlyphId) {
  return mGlyphIdMap.Get(aGlyphId);
}

gfxSVGGlyphsDocument::gfxSVGGlyphsDocument(const uint8_t* aBuffer,
                                           uint32_t aBufLen,
                                           gfxSVGGlyphs* aSVGGlyphs)
    : mOwner(aSVGGlyphs) {
  if (aBufLen >= 14 && aBuffer[0] == 31 && aBuffer[1] == 139) {
    // It's a gzip-compressed document; decompress it before parsing.
    // The original length (modulo 2^32) is found in the last 4 bytes
    // of the data, stored in little-endian format. We read it as
    // individual bytes to avoid possible alignment issues.
    // (Note that if the original length was >2^32, then origLen here
    // will be incorrect; but then the inflate() call will not return
    // Z_STREAM_END and we'll bail out safely.)
    size_t origLen = (size_t(aBuffer[aBufLen - 1]) << 24) +
                     (size_t(aBuffer[aBufLen - 2]) << 16) +
                     (size_t(aBuffer[aBufLen - 3]) << 8) +
                     size_t(aBuffer[aBufLen - 4]);
    AutoTArray<uint8_t, 4096> outBuf;
    if (outBuf.SetLength(origLen, mozilla::fallible)) {
      z_stream s = {0};
      s.next_in = const_cast<Byte*>(aBuffer);
      s.avail_in = aBufLen;
      s.next_out = outBuf.Elements();
      s.avail_out = outBuf.Length();
      // The magic number 16 here is the zlib flag to expect gzip format,
      // see http://www.zlib.net/manual.html#Advanced
      if (Z_OK == inflateInit2(&s, 16 + MAX_WBITS)) {
        int result = inflate(&s, Z_FINISH);
        if (Z_STREAM_END == result) {
          MOZ_ASSERT(size_t(s.next_out - outBuf.Elements()) == origLen);
          ParseDocument(outBuf.Elements(), outBuf.Length());
        } else {
          NS_WARNING("Failed to decompress SVG glyphs document");
        }
        inflateEnd(&s);
      }
    } else {
      NS_WARNING("Failed to allocate memory for SVG glyphs document");
    }
  } else {
    ParseDocument(aBuffer, aBufLen);
  }

  if (!mDocument) {
    NS_WARNING("Could not parse SVG glyphs document");
    return;
  }

  Element* root = mDocument->GetRootElement();
  if (!root) {
    NS_WARNING("Could not parse SVG glyphs document");
    return;
  }

  nsresult rv = SetupPresentation();
  if (NS_FAILED(rv)) {
    NS_WARNING("Couldn't setup presentation for SVG glyphs document");
    return;
  }

  FindGlyphElements(root);
}

gfxSVGGlyphsDocument::~gfxSVGGlyphsDocument() {
  if (mDocument) {
    mDocument->OnPageHide(false, nullptr);
  }
  if (mPresShell) {
    mPresShell->RemovePostRefreshObserver(this);
  }
  if (mViewer) {
    mViewer->Close(nullptr);
    mViewer->Destroy();
  }
}

static nsresult CreateBufferedStream(const uint8_t* aBuffer, uint32_t aBufLen,
                                     nsCOMPtr<nsIInputStream>& aResult) {
  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = NS_NewByteInputStream(
      getter_AddRefs(stream),
      Span(reinterpret_cast<const char*>(aBuffer), aBufLen),
      NS_ASSIGNMENT_DEPEND);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> aBufferedStream;
  if (!NS_InputStreamIsBuffered(stream)) {
    rv = NS_NewBufferedInputStream(getter_AddRefs(aBufferedStream),
                                   stream.forget(), 4096);
    NS_ENSURE_SUCCESS(rv, rv);
    stream = aBufferedStream;
  }

  aResult = stream;

  return NS_OK;
}

nsresult gfxSVGGlyphsDocument::ParseDocument(const uint8_t* aBuffer,
                                             uint32_t aBufLen) {
  // Mostly pulled from nsDOMParser::ParseFromStream

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = CreateBufferedStream(aBuffer, aBufLen, stream);
  NS_ENSURE_SUCCESS(rv, rv);

  // We just need a dummy URI.
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), "moz-svg-glyphs://"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal =
      NullPrincipal::CreateWithoutOriginAttributes();

  RefPtr<Document> document;
  rv = NS_NewDOMDocument(getter_AddRefs(document),
                         u""_ns,   // aNamespaceURI
                         u""_ns,   // aQualifiedName
                         nullptr,  // aDoctype
                         uri, uri, principal,
                         false,    // aLoadedAsData
                         nullptr,  // aEventObject
                         DocumentFlavor::SVG);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewInputStreamChannel(
      getter_AddRefs(channel), uri,
      nullptr,  // aStream
      principal, nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL,
      nsIContentPolicy::TYPE_OTHER, SVG_CONTENT_TYPE, UTF8_CHARSET);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set this early because various decisions during page-load depend on it.
  document->SetIsBeingUsedAsImage();
  document->SetIsSVGGlyphsDocument();
  document->SetReadyStateInternal(Document::READYSTATE_UNINITIALIZED);

  nsCOMPtr<nsIStreamListener> listener;
  rv = document->StartDocumentLoad("external-resource", channel,
                                   nullptr,  // aLoadGroup
                                   nullptr,  // aContainer
                                   getter_AddRefs(listener), true /* aReset */);
  if (NS_FAILED(rv) || !listener) {
    return NS_ERROR_FAILURE;
  }

  rv = listener->OnStartRequest(channel);
  if (NS_FAILED(rv)) {
    channel->Cancel(rv);
  }

  nsresult status;
  channel->GetStatus(&status);
  if (NS_SUCCEEDED(rv) && NS_SUCCEEDED(status)) {
    rv = listener->OnDataAvailable(channel, stream, 0, aBufLen);
    if (NS_FAILED(rv)) {
      channel->Cancel(rv);
    }
    channel->GetStatus(&status);
  }

  rv = listener->OnStopRequest(channel, status);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  document.swap(mDocument);

  return NS_OK;
}

void gfxSVGGlyphsDocument::InsertGlyphId(Element* aGlyphElement) {
  nsAutoString glyphIdStr;
  static const uint32_t glyphPrefixLength = 5;
  // The maximum glyph ID is 65535 so the maximum length of the numeric part
  // is 5.
  if (!aGlyphElement->GetAttr(nsGkAtoms::id, glyphIdStr) ||
      !StringBeginsWith(glyphIdStr, u"glyph"_ns) ||
      glyphIdStr.Length() > glyphPrefixLength + 5) {
    return;
  }

  uint32_t id = 0;
  for (uint32_t i = glyphPrefixLength; i < glyphIdStr.Length(); ++i) {
    char16_t ch = glyphIdStr.CharAt(i);
    if (ch < '0' || ch > '9') {
      return;
    }
    if (ch == '0' && i == glyphPrefixLength) {
      return;
    }
    id = id * 10 + (ch - '0');
  }

  mGlyphIdMap.InsertOrUpdate(id, aGlyphElement);
}

size_t gfxSVGGlyphsDocument::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) +
         mGlyphIdMap.ShallowSizeOfExcludingThis(aMallocSizeOf);
}
