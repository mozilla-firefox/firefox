/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedFontList-impl.h"
#include "gfxFont.h"
#include "gfxFontUtils.h"
#include "gfxPlatformFontList.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "nsReadableUtils.h"
#include "prerror.h"

#define LOG_FONTLIST(args) \
  MOZ_LOG(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug, args)
#define LOG_FONTLIST_ENABLED() \
  MOZ_LOG_TEST(gfxPlatform::GetLog(eGfxLog_fontlist), LogLevel::Debug)

namespace mozilla {
namespace fontlist {

static double WSSDistance(const Face* aFace, const gfxFontStyle& aStyle) {
  double widthDist = WidthDistance(aFace->mWidth, aStyle.width);
  double styleDist = StyleDistance(
      aFace->mStyle, aStyle.style,
      aStyle.synthesisStyle != StyleFontSynthesisStyle::ObliqueOnly);
  double weightDist = WeightDistance(aFace->mWeight, aStyle.weight);

  // Sanity-check that the distances are within the expected range
  // (update if implementation of the distance functions is changed).
  MOZ_ASSERT(widthDist >= 0.0 && widthDist <= 2000.0);
  MOZ_ASSERT(styleDist >= 0.0 && styleDist <= 900.0);
  MOZ_ASSERT(weightDist >= 0.0 && weightDist <= 1600.0);

  // weight/style/width priority: width >> style >> weight
  // so we multiply the width and style values to make them dominate
  // the result
  return widthDist * kWidthFactor + styleDist * kStyleFactor +
         weightDist * kWeightFactor;
}

void* Pointer::ToPtr(FontList* aFontList,
                     size_t aSize) const MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // On failure, we'll return null; callers need to handle this appropriately
  // (e.g. via fallback).
  void* result = nullptr;

  // Load the atomic once to derive the block and offset, so a concurrent
  // rewrite can't tear the two apart.
  uint32_t blockAndOffset = mBlockAndOffset;

  if (blockAndOffset == kNullValue) {
    return result;
  }

  // Ensure the list doesn't get replaced out from under us. Font-list rebuild
  // happens on the main thread, so only non-main-thread callers need to lock
  // it here.
  bool isMainThread = NS_IsMainThread();
  if (!isMainThread) {
    gfxPlatformFontList::PlatformFontList()->Lock();
  }

  uint32_t blockIndex = blockAndOffset >> kBlockShift;
  uint32_t offset = blockAndOffset & kOffsetMask;

  // If the Pointer refers to a block we have not yet mapped in this process,
  // we first need to retrieve new block handle(s) from the parent and update
  // our mBlocks list.
  auto& blocks = aFontList->mBlocks;
  if (blockIndex >= blocks.Length()) {
    if (MOZ_UNLIKELY(XRE_IsParentProcess())) {
      // Shouldn't happen! A content process tried to pass a bad Pointer?
      goto cleanup;
    }
    // If we're not on the main thread, we can't do the IPC involved in
    // UpdateShmBlocks; just let the lookup fail for now.
    if (!isMainThread) {
      goto cleanup;
    }
    // UpdateShmBlocks can fail, if the parent has replaced the font list with
    // a new generation. In that case we just return null, and whatever font
    // the content process was trying to use will appear unusable for now. It's
    // about to receive a notification of the new font list anyhow, at which
    // point it will flush its caches and reflow everything, so the temporary
    // failure of this font will be forgotten.
    // UpdateShmBlocks will take the platform font-list lock during the update.
    if (MOZ_UNLIKELY(!aFontList->UpdateShmBlocks(true))) {
      goto cleanup;
    }
    MOZ_ASSERT(blockIndex < blocks.Length(), "failure in UpdateShmBlocks?");
    // This is wallpapering bug 1667977; it's unclear if we will always survive
    // this, as the content process may be unable to shape/render text if all
    // font lookups are failing.
    // In at least some cases, however, this can occur transiently while the
    // font list is being rebuilt by the parent; content will then be notified
    // that the list has changed, and should refresh everything successfully.
    if (MOZ_UNLIKELY(blockIndex >= blocks.Length())) {
      goto cleanup;
    }
  }

  {
    // Don't create a pointer that's outside what the block has allocated!
    const auto& block = blocks[blockIndex];
    if (MOZ_LIKELY(offset + aSize <= block->Allocated())) {
      result = static_cast<char*>(block->Memory()) + offset;
    }
  }

cleanup:
  if (!isMainThread) {
    gfxPlatformFontList::PlatformFontList()->Unlock();
  }

  return result;
}

void String::Assign(const nsACString& aString, FontList* aList) {
  // We only assign to previously-empty strings; they are never modified
  // after initial assignment.
  MOZ_ASSERT(mPointer.IsNull());
  mLength = aString.Length();
  mPointer = aList->Alloc(mLength + 1);
  auto* p = mPointer.ToArray<char>(aList, mLength);
  std::memcpy(p, aString.BeginReading(), mLength);
  p[mLength] = '\0';
}

Family::Family(FontList* aList, const InitData& aData)
    : mFaceCount(0),
      mKey(aList, aData.mKey),
      mName(aList, aData.mName),
      mCharacterMap(Pointer::Null()),
      mFaces(Pointer::Null()),
      mIndex(aData.mIndex),
      mVisibility(aData.mVisibility),
      mIsSimple(false),
      mIsBundled(aData.mBundled),
      mIsBadUnderlineFamily(aData.mBadUnderline),
      mIsForceClassic(aData.mForceClassic),
      mIsAltLocale(aData.mAltLocale) {}

class SetCharMapRunnable : public mozilla::Runnable {
 public:
  SetCharMapRunnable(uint32_t aListGeneration,
                     std::pair<uint32_t, bool> aFamilyIndex,
                     uint32_t aFaceIndex, gfxCharacterMap* aCharMap)
      : Runnable("SetCharMapRunnable"),
        mListGeneration(aListGeneration),
        mFamilyIndex(aFamilyIndex),
        mFaceIndex(aFaceIndex),
        mCharMap(aCharMap) {}

  NS_IMETHOD Run() override {
    auto* list = gfxPlatformFontList::PlatformFontList()->SharedFontList();
    if (!list || list->GetGeneration() != mListGeneration) {
      return NS_OK;
    }
    dom::ContentChild::GetSingleton()->SendSetCharacterMap(
        mListGeneration, mFamilyIndex.first, mFamilyIndex.second, mFaceIndex,
        *mCharMap);
    return NS_OK;
  }

 private:
  uint32_t mListGeneration;
  std::pair<uint32_t, bool> mFamilyIndex;
  uint32_t mFaceIndex;
  RefPtr<gfxCharacterMap> mCharMap;
};

void Face::SetCharacterMap(FontList* aList, gfxCharacterMap* aCharMap,
                           const Family* aFamily) {
  if (!XRE_IsParentProcess()) {
    Maybe<std::pair<uint32_t, bool>> familyIndex = aFamily->FindIndex(aList);
    if (!familyIndex) {
      NS_WARNING("Family index not found! Ignoring SetCharacterMap");
      return;
    }
    const auto* faces = aFamily->Faces(aList);
    uint32_t faceIndex = 0;
    while (faceIndex < aFamily->NumFaces()) {
      if (faces[faceIndex].ToPtr<Face>(aList) == this) {
        break;
      }
      ++faceIndex;
    }
    if (faceIndex >= aFamily->NumFaces()) {
      NS_WARNING("Face not found in family! Ignoring SetCharacterMap");
      return;
    }
    if (NS_IsMainThread()) {
      dom::ContentChild::GetSingleton()->SendSetCharacterMap(
          aList->GetGeneration(), familyIndex->first, familyIndex->second,
          faceIndex, *aCharMap);
    } else {
      NS_DispatchToMainThread(new SetCharMapRunnable(
          aList->GetGeneration(), familyIndex.value(), faceIndex, aCharMap));
    }
    return;
  }
  auto pfl = gfxPlatformFontList::PlatformFontList();
  mCharacterMap = pfl->GetShmemCharMap(aCharMap);
}

void Family::AddFaces(FontList* aList, const nsTArray<Face::InitData>& aFaces) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (mFaceCount > 0) {
    // Already initialized!
    return;
  }

  uint32_t count = aFaces.Length();
  bool isSimple = false;
  // A family is "simple" (i.e. simplified style selection may be used instead
  // of the full CSS font-matching algorithm) if there is at maximum one normal,
  // bold, italic, and bold-italic face; in this case, they are stored at known
  // positions in the mFaces array.
  const Face::InitData* slots[4] = {nullptr, nullptr, nullptr, nullptr};
  if (count >= 2 && count <= 4) {
    // Check if this can be treated as a "simple" family
    isSimple = true;
    for (const auto& f : aFaces) {
      if (!f.mWeight.IsSingle() || !f.mWidth.IsSingle() ||
          !f.mStyle.IsSingle()) {
        isSimple = false;
        break;
      }
      if (!f.mWidth.Min().IsNormal()) {
        isSimple = false;
        break;
      }
      // Figure out which slot (0-3) this face belongs in
      size_t slot = 0;
      static_assert((kBoldMask | kItalicMask) == 0b11, "bad bold/italic bits");
      if (f.mWeight.Min().IsBold()) {
        slot |= kBoldMask;
      }
      if (!f.mStyle.Min().IsNormal()) {
        slot |= kItalicMask;
      }
      if (slots[slot]) {
        // More than one face mapped to the same slot - not a simple family!
        isSimple = false;
        break;
      }
      slots[slot] = &f;
    }
    if (isSimple) {
      // Ensure all 4 slots will exist, even if some are empty.
      count = 4;
    }
  }

  // Allocate space for the face records, and initialize them.
  // coverity[suspicious_sizeof]
  Pointer p = aList->Alloc(count * sizeof(Pointer));
  auto* facePtrs = p.ToArray<Pointer>(aList, count);
  for (size_t i = 0; i < count; i++) {
    if (isSimple && !slots[i]) {
      facePtrs[i] = Pointer::Null();
    } else {
      const auto* initData = isSimple ? slots[i] : &aFaces[i];
      Pointer fp = aList->Alloc(sizeof(Face));
      auto* face = fp.ToPtr<Face>(aList);
      (void)new (face) Face(aList, *initData);
      facePtrs[i] = fp;
      if (initData->mCharMap) {
        face->SetCharacterMap(aList, initData->mCharMap, this);
      }
    }
  }

  mIsSimple = isSimple;
  mFaces = p;
  mFaceCount.store(count);

  if (LOG_FONTLIST_ENABLED()) {
    const nsCString& fam = DisplayName().AsString(aList);
    for (unsigned j = 0; j < aFaces.Length(); j++) {
      nsAutoCString weight, style, width;
      aFaces[j].mWeight.ToString(weight);
      aFaces[j].mStyle.ToString(style);
      aFaces[j].mWidth.ToString(width);
      LOG_FONTLIST(
          ("(shared-fontlist) family (%s) added face (%s) index %u, weight "
           "%s, style %s, width %s",
           fam.get(), aFaces[j].mDescriptor.get(), aFaces[j].mIndex,
           weight.get(), style.get(), width.get()));
    }
  }
}

bool Family::FindAllFacesForStyleInternal(FontList* aList,
                                          const gfxFontStyle& aStyle,
                                          nsTArray<Face*>& aFaceList) const {
  MOZ_ASSERT(aFaceList.IsEmpty());
  if (!IsInitialized()) {
    return false;
  }

  Pointer* facePtrs = Faces(aList);
  if (!facePtrs) {
    return false;
  }

  // Depending on the kind of family, we have to do varying amounts of work
  // to figure out what face(s) to use for the requested style properties.

  // If the family has only one face, we simply use it; no further style
  // checking needed. (However, for bitmap fonts we may still need to check
  // whether the size is acceptable.)
  if (NumFaces() == 1) {
    MOZ_ASSERT(!facePtrs[0].IsNull());
    auto* face = facePtrs[0].ToPtr<Face>(aList);
    if (face && face->HasValidDescriptor()) {
      aFaceList.AppendElement(face);
#ifdef MOZ_WIDGET_GTK
      if (face->mSize) {
        return true;
      }
#endif
    }
    return false;
  }

  // Most families are "simple", having just Regular/Bold/Italic/BoldItalic,
  // or some subset of these. In this case, we have exactly 4 entries in
  // mAvailableFonts, stored in the above order; note that some of the entries
  // may be nullptr. We can then pick the required entry based on whether the
  // request is for bold or non-bold, italic or non-italic, without running
  // the more complex matching algorithm used for larger families with many
  // weights and/or widths.

  if (mIsSimple) {
    // Family has no more than the "standard" 4 faces, at fixed indexes;
    // calculate which one we want.
    // Note that we cannot simply return it as not all 4 faces are necessarily
    // present.
    bool wantBold = aStyle.weight.PreferBold();
    bool wantItalic = !aStyle.style.IsNormal();
    uint8_t faceIndex =
        (wantItalic ? kItalicMask : 0) | (wantBold ? kBoldMask : 0);

    // If the desired style is available, use it directly.
    auto* face = facePtrs[faceIndex].ToPtr<Face>(aList);
    if (face && face->HasValidDescriptor()) {
      aFaceList.AppendElement(face);
#ifdef MOZ_WIDGET_GTK
      if (face->mSize) {
        return true;
      }
#endif
      return false;
    }

    // Order to check fallback faces in a simple family, depending on the
    // requested style.
    static const uint8_t simpleFallbacks[4][3] = {
        {kBoldFaceIndex, kItalicFaceIndex,
         kBoldItalicFaceIndex},  // fallback sequence for Regular
        {kRegularFaceIndex, kBoldItalicFaceIndex, kItalicFaceIndex},  // Bold
        {kBoldItalicFaceIndex, kRegularFaceIndex, kBoldFaceIndex},    // Italic
        {kItalicFaceIndex, kBoldFaceIndex, kRegularFaceIndex}  // BoldItalic
    };
    const uint8_t* order = simpleFallbacks[faceIndex];

    for (uint8_t trial = 0; trial < 3; ++trial) {
      // check remaining faces in order of preference to find the first that
      // actually exists
      face = facePtrs[order[trial]].ToPtr<Face>(aList);
      if (face && face->HasValidDescriptor()) {
        aFaceList.AppendElement(face);
#ifdef MOZ_WIDGET_GTK
        if (face->mSize) {
          return true;
        }
#endif
        return false;
      }
    }

    // We can only reach here if we failed to resolve the face pointer, which
    // can happen if we're on a stylo thread and caught the font list being
    // updated; in that case we just fail quietly and let font fallback do
    // something for the time being.
    return false;
  }

  // Pick the font(s) that are closest to the desired weight, style, and
  // width. Iterate over all fonts, measuring the weight/style distance.
  // Because of unicode-range values, there may be more than one font for a
  // given but the 99% use case is only a single font entry per
  // weight/style/width distance value. To optimize this, only add entries
  // to the matched font array when another entry already has the same
  // weight/style/width distance and add the last matched font entry. For
  // normal platform fonts with a single font entry for each
  // weight/style/width combination, only the last matched font entry will
  // be added.
  double minDistance = INFINITY;
  Face* matched = nullptr;
  // Keep track of whether we've included any non-scalable font resources in
  // the selected set.
  bool anyNonScalable = false;
  for (uint32_t i = 0; i < NumFaces(); i++) {
    auto* face = facePtrs[i].ToPtr<Face>(aList);
    if (face) {
      // weight/style/width priority: width >> style >> weight
      double distance = WSSDistance(face, aStyle);
      if (distance < minDistance) {
        matched = face;
        if (!aFaceList.IsEmpty()) {
          aFaceList.Clear();
        }
        minDistance = distance;
      } else if (distance == minDistance) {
        if (matched) {
          aFaceList.AppendElement(matched);
#ifdef MOZ_WIDGET_GTK
          if (matched->mSize) {
            anyNonScalable = true;
          }
#endif
        }
        matched = face;
      }
    }
  }

  MOZ_ASSERT(matched, "didn't match a font within a family");
  if (matched) {
    aFaceList.AppendElement(matched);
#ifdef MOZ_WIDGET_GTK
    if (matched->mSize) {
      anyNonScalable = true;
    }
#endif
  }

  return anyNonScalable;
}

void Family::FindAllFacesForStyle(FontList* aList, const gfxFontStyle& aStyle,
                                  nsTArray<Face*>& aFaceList,
                                  bool aIgnoreSizeTolerance) const {
#ifdef MOZ_WIDGET_GTK
  bool anyNonScalable =
#else
  (void)
#endif
      FindAllFacesForStyleInternal(aList, aStyle, aFaceList);

#ifdef MOZ_WIDGET_GTK
  // aFaceList now contains whatever faces are the best style match for
  // the requested style. If specifically-sized bitmap faces are supported,
  // we need to additionally filter the list to choose the appropriate size.
  //
  // It would be slightly more efficient to integrate this directly into the
  // face-selection algorithm above, but it's a rare case that doesn't apply
  // at all to most font families.
  //
  // Currently we only support pixel-sized bitmap font faces on Linux/Gtk (i.e.
  // when using the gfxFcPlatformFontList implementation), so this filtering is
  // not needed on other platforms.
  //
  // (Note that color-bitmap emoji fonts like Apple Color Emoji or Noto Color
  // Emoji don't count here; they package multiple bitmap sizes into a single
  // OpenType wrapper, so they appear as a single "scalable" face in our list.)
  if (anyNonScalable) {
    uint16_t best = 0;
    gfxFloat dist = 0.0;
    for (const auto& f : aFaceList) {
      if (f->mSize == 0) {
        // Scalable face; no size distance to compute.
        continue;
      }
      gfxFloat d = fabs(gfxFloat(f->mSize) - aStyle.size);
      if (!aIgnoreSizeTolerance && (d * 5.0 > f->mSize)) {
        continue;  // Too far from the requested size, ignore.
      }
      // If we haven't found a "best" bitmap size yet, or if this is a better
      // match, remember it.
      if (!best || d < dist) {
        best = f->mSize;
        dist = d;
      }
    }
    // Discard all faces except the chosen "best" size; or if no pixel size was
    // chosen, all except scalable faces.
    // This may eliminate *all* faces in the family, if all were bitmaps and
    // none was a good enough size match, in which case we'll fall back to the
    // next font-family name.
    aFaceList.RemoveElementsBy([=](const auto& e) { return e->mSize != best; });
  }
#endif
}

Face* Family::FindFaceForStyle(FontList* aList, const gfxFontStyle& aStyle,
                               bool aIgnoreSizeTolerance) const {
  AutoTArray<Face*, 4> faces;
  FindAllFacesForStyle(aList, aStyle, faces, aIgnoreSizeTolerance);
  return faces.IsEmpty() ? nullptr : faces[0];
}

void Family::SearchAllFontsForChar(FontList* aList,
                                   GlobalFontMatch* aMatchData) {
  auto* charmap = mCharacterMap.ToPtr<const SharedBitSet>(aList);
  if (!charmap) {
    // If the face list is not yet initialized, or if character maps have
    // not been loaded, go ahead and do this now (by sending a message to the
    // parent process, if we're running in a child).
    // After this, all faces should have their mCharacterMap set up, and the
    // family's mCharacterMap should also be set; but in the code below we
    // don't assume this all succeeded, so it still checks.
    if (!gfxPlatformFontList::PlatformFontList()->InitializeFamily(this,
                                                                   true)) {
      return;
    }
    charmap = mCharacterMap.ToPtr<const SharedBitSet>(aList);
  }
  if (charmap && !charmap->test(aMatchData->mCh)) {
    return;
  }

  uint32_t numFaces = NumFaces();
  uint32_t charMapsLoaded = 0;  // number of faces whose charmap is loaded
  Pointer* facePtrs = Faces(aList);
  if (!facePtrs) {
    return;
  }
  for (uint32_t i = 0; i < numFaces; i++) {
    auto* face = facePtrs[i].ToPtr<Face>(aList);
    if (!face) {
      continue;
    }
    MOZ_ASSERT(face->HasValidDescriptor());
    // Get the face's character map, if available (may be null!)
    charmap = face->mCharacterMap.ToPtr<const SharedBitSet>(aList);
    if (charmap) {
      ++charMapsLoaded;
    }
    // Check style distance if the char is supported, or if charmap not known
    // (so that we don't trigger cmap-loading for faces that would be a worse
    // match than what we've already found).
    if (!charmap || charmap->test(aMatchData->mCh)) {
      double distance = WSSDistance(face, aMatchData->mStyle);
      if (distance < aMatchData->mMatchDistance) {
        // It's a better style match: get a fontEntry, and if we haven't
        // already checked character coverage, do it now (note that
        // HasCharacter() will trigger loading the fontEntry's cmap, if
        // needed).
        RefPtr<gfxFontEntry> fe =
            gfxPlatformFontList::PlatformFontList()->GetOrCreateFontEntry(face,
                                                                          this);
        // The shared character map doesn't know when the font entry has been
        // found to be unusable.
        if (!fe || fe->IsUnusable()) {
          continue;
        }
        if (!charmap && !fe->HasCharacter(aMatchData->mCh)) {
          continue;
        }
        if (aMatchData->mPresentation != FontPresentation::Any) {
          RefPtr<gfxFont> font = fe->FindOrMakeFont(&aMatchData->mStyle);
          if (!font) {
            continue;
          }
          bool hasColorGlyph =
              font->HasColorGlyphFor(aMatchData->mCh, aMatchData->mNextCh);
          if (hasColorGlyph != PrefersColor(aMatchData->mPresentation)) {
            distance += kPresentationMismatch;
            if (distance >= aMatchData->mMatchDistance) {
              continue;
            }
          }
        }
        aMatchData->mBestMatch = fe;
        aMatchData->mMatchDistance = distance;
        aMatchData->mMatchedSharedFamily = this;
      }
    }
  }
  if (mCharacterMap.IsNull() && charMapsLoaded == numFaces) {
    SetupFamilyCharMap(aList);
  }
}

void Family::SetFacePtrs(FontList* aList, nsTArray<Pointer>& aFaces) {
  if (aFaces.Length() >= 2 && aFaces.Length() <= 4) {
    // Check whether the faces meet the criteria for a "simple" family: no more
    // than one each of Regular, Bold, Italic, BoldItalic styles. If so, store
    // them at the appropriate slots in mFaces and set the mIsSimple flag to
    // accelerate font-matching.
    bool isSimple = true;
    Pointer slots[4] = {Pointer::Null(), Pointer::Null(), Pointer::Null(),
                        Pointer::Null()};
    for (const Pointer& fp : aFaces) {
      auto* f = fp.ToPtr<const Face>(aList);
      if (!f->mWeight.IsSingle() || !f->mStyle.IsSingle() ||
          !f->mWidth.IsSingle()) {
        isSimple = false;
        break;
      }
      if (!f->mWidth.Min().IsNormal()) {
        isSimple = false;
        break;
      }
      size_t slot = 0;
      if (f->mWeight.Min().IsBold()) {
        slot |= kBoldMask;
      }
      if (!f->mStyle.Min().IsNormal()) {
        slot |= kItalicMask;
      }
      if (!slots[slot].IsNull()) {
        isSimple = false;
        break;
      }
      slots[slot] = fp;
    }
    if (isSimple) {
      size_t size = 4 * sizeof(Pointer);
      mFaces = aList->Alloc(size);
      memcpy(mFaces.ToPtr(aList, size), slots, size);
      mFaceCount.store(4);
      mIsSimple = true;
      return;
    }
  }
  size_t size = aFaces.Length() * sizeof(Pointer);
  mFaces = aList->Alloc(size);
  memcpy(mFaces.ToPtr(aList, size), aFaces.Elements(), size);
  mFaceCount.store(aFaces.Length());
}

void Family::SetupFamilyCharMap(FontList* aList) {
  // Set the character map of the family to the union of all the face cmaps,
  // to allow font fallback searches to more rapidly reject the family.
  if (!mCharacterMap.IsNull()) {
    return;
  }
  if (!XRE_IsParentProcess()) {
    // |this| could be a Family record in either the Families() or Aliases()
    // arrays; FindIndex will map it back to its index and which array.
    Maybe<std::pair<uint32_t, bool>> index = FindIndex(aList);
    if (!index) {
      NS_WARNING("Family index not found! Ignoring SetupFamilyCharMap");
      return;
    }
    if (NS_IsMainThread()) {
      dom::ContentChild::GetSingleton()->SendSetupFamilyCharMap(
          aList->GetGeneration(), index->first, index->second);
      return;
    }
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "SetupFamilyCharMap callback",
        [gen = aList->GetGeneration(), idx = index->first,
         alias = index->second] {
          dom::ContentChild::GetSingleton()->SendSetupFamilyCharMap(gen, idx,
                                                                    alias);
        }));
    return;
  }
  gfxSparseBitSet familyMap;
  Pointer firstMapShmPointer;
  const SharedBitSet* firstMap = nullptr;
  bool merged = false;
  Pointer* faces = Faces(aList);
  if (!faces) {
    return;
  }
  for (size_t i = 0; i < NumFaces(); i++) {
    auto* f = faces[i].ToPtr<const Face>(aList);
    if (!f) {
      continue;  // Skip missing face (in an incomplete "simple" family)
    }
    auto* faceMap = f->mCharacterMap.ToPtr<const SharedBitSet>(aList);
    if (!faceMap) {
      continue;  // If there's a face where setting up the cmap failed, we skip
                 // it as unusable.
    }
    if (!firstMap) {
      firstMap = faceMap;
      firstMapShmPointer = f->mCharacterMap;
    } else if (faceMap != firstMap) {
      if (!merged) {
        familyMap.Union(*firstMap);
        merged = true;
      }
      familyMap.Union(*faceMap);
    }
  }
  // If we created a merged cmap, we need to save that on the family; or if we
  // found no usable cmaps at all, we need to store the empty familyMap so that
  // we won't repeatedly attempt this for an unusable family.
  if (merged || firstMapShmPointer.IsNull()) {
    mCharacterMap =
        gfxPlatformFontList::PlatformFontList()->GetShmemCharMap(&familyMap);
  } else {
    // If all [usable] faces had the same cmap, we can just share it.
    mCharacterMap = firstMapShmPointer;
  }
}

Maybe<std::pair<uint32_t, bool>> Family::FindIndex(FontList* aList) const {
  const auto* start = aList->Families();
  const auto* end = start + aList->NumFamilies();
  if (this >= start && this < end) {
    uint32_t index = this - start;
    MOZ_RELEASE_ASSERT(start + index == this, "misaligned Family ptr!");
    return Some(std::pair(index, false));
  }

  start = aList->AliasFamilies();
  end = start + aList->NumAliases();
  if (this >= start && this < end) {
    uint32_t index = this - start;
    MOZ_RELEASE_ASSERT(start + index == this, "misaligned AliasFamily ptr!");
    return Some(std::pair(index, true));
  }

  return Nothing();
}

FontList::FontList(uint32_t aGeneration) {
  if (XRE_IsParentProcess()) {
    // Create the initial shared block, and initialize Header
    if (AppendShmBlock(SHM_BLOCK_SIZE)) {
      Header& header = GetHeader();
      header.mBlockHeader.mAllocated.store(sizeof(Header));
      header.mGeneration = aGeneration;
      header.mFamilyCount = 0;
      header.mBlockCount.store(1);
      header.mAliasCount.store(0);
      header.mLocalFaceCount.store(0);
      header.mFamilies = Pointer::Null();
      header.mAliases = Pointer::Null();
      header.mLocalFaces = Pointer::Null();
    } else {
      MOZ_CRASH("parent: failed to initialize FontList");
    }
  } else {
    // Initialize using the list of shmem blocks passed by the parent via
    // SetXPCOMProcessAttributes.
    auto& blocks = dom::ContentChild::GetSingleton()->SharedFontListBlocks();
    for (auto& handle : blocks) {
      if (!handle) {
        // Bail out and let UpdateShmBlocks try to do its thing below.
        break;
      }
      if (handle.Size() < SHM_BLOCK_SIZE) {
        MOZ_CRASH("failed to map shared memory");
      }
      auto newShm = handle.Map();
      if (!newShm || !newShm.Address()) {
        MOZ_CRASH("failed to map shared memory");
      }
      uint32_t size = newShm.DataAs<BlockHeader>()->mBlockSize;
      MOZ_ASSERT(size >= SHM_BLOCK_SIZE);
      if (newShm.Size() < size) {
        MOZ_CRASH("failed to map shared memory");
      }
      mBlocks.AppendElement(new ShmBlock(std::move(newShm)));
    }
    blocks.Clear();
    // Update in case of any changes since the initial message was sent.
    for (unsigned retryCount = 0; retryCount < 3; ++retryCount) {
      if (UpdateShmBlocks(false)) {
        return;
      }
      // The only reason for UpdateShmBlocks to fail is if the parent recreated
      // the list after we read its first block, but before we finished getting
      // them all, and so the generation check failed on a subsequent request.
      // Discarding whatever we've got and retrying should get us a new,
      // consistent set of memory blocks in this case. If this doesn't work
      // after a couple of retries, bail out.
      DetachShmBlocks();
    }
    NS_WARNING("child: failed to initialize shared FontList");
  }
}

FontList::~FontList() { DetachShmBlocks(); }

FontList::Header& FontList::GetHeader() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // We only need to lock if we're not on the main thread.
  bool isMainThread = NS_IsMainThread();
  if (!isMainThread) {
    gfxPlatformFontList::PlatformFontList()->Lock();
  }

  // It's invalid to try and access this before the first block exists.
  MOZ_ASSERT(mBlocks.Length() > 0);
  auto& result = *static_cast<Header*>(mBlocks[0]->Memory());

  if (!isMainThread) {
    gfxPlatformFontList::PlatformFontList()->Unlock();
  }

  return result;
}

bool FontList::AppendShmBlock(uint32_t aSizeNeeded) {
  MOZ_ASSERT(XRE_IsParentProcess());

  // TODO: currently most callers of AppendShmBlock() (via Alloc()) assume
  // the allocation is infallible; hence the release-assert here is the safe
  // way to handle overflow. Consider whether to make the allocation fallible,
  // and instead handle null safely in the callers.
  MOZ_RELEASE_ASSERT(mBlocks.Length() < (1u << Pointer::kIndexBits),
                     "FontList shm block limit exceeded");

  uint32_t size = std::max(aSizeNeeded, SHM_BLOCK_SIZE);
  auto handle = ipc::shared_memory::CreateFreezable(size);
  if (!handle) {
    MOZ_CRASH("failed to create shared memory");
    return false;
  }
  auto [readOnly, newShm] = std::move(handle).Map().FreezeWithMutableMapping();
  if (!newShm || !newShm.Address()) {
    MOZ_CRASH("failed to map shared memory");
    return false;
  }
  if (!readOnly) {
    MOZ_CRASH("failed to create read-only copy");
    return false;
  }

  ShmBlock* block = new ShmBlock(std::move(newShm));
  block->StoreAllocated(sizeof(BlockHeader));
  block->BlockSize() = size;

  mBlocks.AppendElement(block);
  GetHeader().mBlockCount.store(mBlocks.Length());

  mReadOnlyShmems.AppendElement(std::move(readOnly));

  // We don't need to broadcast the addition of the initial block,
  // because child processes can't have initialized their list at all
  // prior to the first block being set up.
  if (mBlocks.Length() > 1) {
    if (NS_IsMainThread()) {
      dom::ContentParent::BroadcastShmBlockAdded(GetGeneration(),
                                                 mBlocks.Length() - 1);
    } else {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ShmBlockAdded callback",
          [generation = GetGeneration(), index = mBlocks.Length() - 1] {
            dom::ContentParent::BroadcastShmBlockAdded(generation, index);
          }));
    }
  }

  return true;
}

void FontList::ShmBlockAdded(uint32_t aGeneration, uint32_t aIndex,
                             ipc::ReadOnlySharedMemoryHandle aHandle) {
  MOZ_ASSERT(!XRE_IsParentProcess());
  MOZ_ASSERT(mBlocks.Length() > 0);

  if (!aHandle) {
    return;
  }
  if (aIndex != mBlocks.Length()) {
    return;
  }
  if (aGeneration != GetGeneration()) {
    return;
  }

  auto newShm = aHandle.Map();
  if (!newShm || !newShm.Address() || newShm.Size() < SHM_BLOCK_SIZE) {
    MOZ_CRASH("failed to map shared memory");
  }

  uint32_t size = newShm.DataAs<BlockHeader>()->mBlockSize;
  MOZ_ASSERT(size >= SHM_BLOCK_SIZE);
  if (newShm.Size() < size) {
    MOZ_CRASH("failed to map shared memory");
  }

  mBlocks.AppendElement(new ShmBlock(std::move(newShm)));
}

void FontList::DetachShmBlocks() {
  for (auto& i : mBlocks) {
    i->Clear();
  }
  mBlocks.Clear();
  mReadOnlyShmems.Clear();
}

FontList::ShmBlock* FontList::GetBlockFromParent(uint32_t aIndex) {
  MOZ_ASSERT(!XRE_IsParentProcess());
  // If we have no existing blocks, we don't want a generation check yet;
  // the header in the first block will define the generation of this list
  uint32_t generation = aIndex == 0 ? 0 : GetGeneration();
  ipc::ReadOnlySharedMemoryHandle handle;
  if (!dom::ContentChild::GetSingleton()->SendGetFontListShmBlock(
          generation, aIndex, &handle)) {
    return nullptr;
  }
  if (!handle) {
    return nullptr;
  }
  auto newShm = handle.Map();
  if (!newShm || !newShm.Address() || newShm.Size() < SHM_BLOCK_SIZE) {
    MOZ_CRASH("failed to map shared memory");
  }
  uint32_t size = newShm.DataAs<BlockHeader>()->mBlockSize;
  MOZ_ASSERT(size >= SHM_BLOCK_SIZE);
  if (newShm.Size() < size) {
    MOZ_CRASH("failed to map shared memory");
  }
  return new ShmBlock(std::move(newShm));
}

// We don't take the lock when called from the constructor, so disable thread-
// safety analysis here.
bool FontList::UpdateShmBlocks(bool aMustLock) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  MOZ_ASSERT(!XRE_IsParentProcess());
  if (aMustLock) {
    gfxPlatformFontList::PlatformFontList()->Lock();
  }
  bool result = true;
  while (!mBlocks.Length() || mBlocks.Length() < GetHeader().mBlockCount) {
    ShmBlock* newBlock = GetBlockFromParent(mBlocks.Length());
    if (!newBlock) {
      result = false;
      break;
    }
    mBlocks.AppendElement(newBlock);
  }
  if (aMustLock) {
    gfxPlatformFontList::PlatformFontList()->Unlock();
  }
  return result;
}

void FontList::ShareBlocksToProcess(
    nsTArray<ipc::ReadOnlySharedMemoryHandle>* aBlocks) {
  MOZ_RELEASE_ASSERT(mReadOnlyShmems.Length() == mBlocks.Length());
  for (auto& shmem : mReadOnlyShmems) {
    auto handle = shmem.Clone();
    if (!handle) {
      // If something went wrong here, we just bail out; the child will need to
      // request the blocks as needed, at some performance cost. (Although in
      // practice this may mean resources are so constrained the child process
      // isn't really going to work at all. But that's not our problem here.)
      aBlocks->Clear();
      return;
    }
    aBlocks->AppendElement(std::move(handle));
  }
}

ipc::ReadOnlySharedMemoryHandle FontList::ShareBlockToProcess(uint32_t aIndex) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  MOZ_RELEASE_ASSERT(mReadOnlyShmems.Length() == mBlocks.Length());
  MOZ_RELEASE_ASSERT(aIndex < mReadOnlyShmems.Length());

  return mReadOnlyShmems[aIndex].Clone();
}

Pointer FontList::Alloc(uint32_t aSize) {
  // Only the parent process does allocation.
  MOZ_ASSERT(XRE_IsParentProcess());

  // 4-byte alignment is good enough for anything we allocate in the font list,
  // as our "Pointer" (block index/offset) is a 32-bit value even on x64.
  auto align = [](uint32_t aSize) -> size_t { return (aSize + 3u) & ~3u; };

  aSize = align(aSize);

  int32_t blockIndex = -1;
  uint32_t curAlloc, size;

  if (aSize < SHM_BLOCK_SIZE - sizeof(BlockHeader)) {
    // Try to allocate in the most recently added block first, as this is
    // highly likely to succeed; if not, try earlier blocks (to fill gaps).
    const int32_t blockCount = mBlocks.Length();
    for (blockIndex = blockCount - 1; blockIndex >= 0; --blockIndex) {
      size = mBlocks[blockIndex]->BlockSize();
      curAlloc = mBlocks[blockIndex]->Allocated();
      if (size - curAlloc >= aSize) {
        break;
      }
    }
  }

  if (blockIndex < 0) {
    // Couldn't find enough space (or the requested size is too large to use
    // a part of a block): create a new block.
    if (!AppendShmBlock(aSize + sizeof(BlockHeader))) {
      return Pointer::Null();
    }
    blockIndex = mBlocks.Length() - 1;
    curAlloc = mBlocks[blockIndex]->Allocated();
  }

  // We've found a block; allocate space from it, and return
  mBlocks[blockIndex]->StoreAllocated(curAlloc + aSize);

  return Pointer(blockIndex, curAlloc);
}

void FontList::SetFamilyNames(nsTArray<Family::InitData>& aFamilies) {
  // Only the parent process should ever assign the list of families.
  MOZ_ASSERT(XRE_IsParentProcess());

  Header& header = GetHeader();
  MOZ_ASSERT(!header.mFamilyCount);

  gfxPlatformFontList::PlatformFontList()->ApplyWhitelist(aFamilies);
  aFamilies.Sort();

  size_t count = aFamilies.Length();

  // Any font resources with an empty family-name will have been collected in
  // a family with empty name, and sorted to the start of the list. Such fonts
  // are not generally usable, or recognized as "installed", so we drop them.
  if (count > 1 && aFamilies[0].mKey.IsEmpty()) {
    aFamilies.RemoveElementAt(0);
    --count;
  }

  // Check for duplicate family entries (can occur if there is a bundled font
  // that has the same name as a system-installed one); in this case we keep
  // the bundled one as it will always be exposed.
  if (count > 1) {
    for (size_t i = 1; i < count; ++i) {
      if (aFamilies[i].mKey.Equals(aFamilies[i - 1].mKey)) {
        // Decide whether to discard the current entry or the preceding one
        size_t discard =
            aFamilies[i].mBundled && !aFamilies[i - 1].mBundled ? i - 1 : i;
        aFamilies.RemoveElementAt(discard);
        --count;
        --i;
      }
    }
  }

  header.mFamilies = Alloc(count * sizeof(Family));
  if (header.mFamilies.IsNull()) {
    return;
  }

  // We can't call Families() here because the mFamilyCount field has not yet
  // been set!
  auto* families = header.mFamilies.ToArray<Family>(this, count);
  for (size_t i = 0; i < count; i++) {
    (void)new (&families[i]) Family(this, aFamilies[i]);
    LOG_FONTLIST(("(shared-fontlist) family %u (%s)", (unsigned)i,
                  aFamilies[i].mName.get()));
  }

  header.mFamilyCount = count;
}

void FontList::SetAliases(
    nsClassHashtable<nsCStringHashKey, AliasData>& aAliasTable) {
  MOZ_ASSERT(XRE_IsParentProcess());

  Header& header = GetHeader();

  // Build an array of Family::InitData records based on the entries in
  // aAliasTable, then sort them and store into the fontlist.
  nsTArray<Family::InitData> aliasArray;
  aliasArray.SetCapacity(aAliasTable.Count());
  for (const auto& entry : aAliasTable) {
    aliasArray.AppendElement(Family::InitData(
        entry.GetKey(), entry.GetData()->mBaseFamily, entry.GetData()->mIndex,
        entry.GetData()->mVisibility, entry.GetData()->mBundled,
        entry.GetData()->mBadUnderline, entry.GetData()->mForceClassic, true));
  }
  aliasArray.Sort();

  size_t count = aliasArray.Length();

  // Drop any entry with empty family-name as being unusable.
  if (count && aliasArray[0].mKey.IsEmpty()) {
    aliasArray.RemoveElementAt(0);
    --count;
  }

  if (count < header.mAliasCount) {
    // This shouldn't happen, but handle it safely by just bailing out.
    NS_WARNING("cannot reduce number of aliases");
    return;
  }
  fontlist::Pointer ptr = Alloc(count * sizeof(Family));
  auto* aliases = ptr.ToArray<Family>(this, count);
  for (size_t i = 0; i < count; i++) {
    (void)new (&aliases[i]) Family(this, aliasArray[i]);
    LOG_FONTLIST(("(shared-fontlist) alias family %u (%s: %s)", (unsigned)i,
                  aliasArray[i].mKey.get(), aliasArray[i].mName.get()));
    aliases[i].SetFacePtrs(this, aAliasTable.Get(aliasArray[i].mKey)->mFaces);
    if (LOG_FONTLIST_ENABLED()) {
      const auto& faces = aAliasTable.Get(aliasArray[i].mKey)->mFaces;
      for (unsigned j = 0; j < faces.Length(); j++) {
        auto* face = faces[j].ToPtr<const Face>(this);
        const nsCString& desc = face->mDescriptor.AsString(this);
        nsAutoCString weight, style, width;
        face->mWeight.ToString(weight);
        face->mStyle.ToString(style);
        face->mWidth.ToString(width);
        LOG_FONTLIST(
            ("(shared-fontlist) face (%s) index %u, weight %s, style %s, "
             "width %s",
             desc.get(), face->mIndex, weight.get(), style.get(), width.get()));
      }
    }
  }

  // Set the pointer before the count, so that any other process trying to read
  // will not risk out-of-bounds access to the array.
  header.mAliases = ptr;
  header.mAliasCount.store(count);
}

void FontList::SetLocalNames(
    nsTHashMap<nsCStringHashKey, LocalFaceRec::InitData>& aLocalNameTable) {
  MOZ_ASSERT(XRE_IsParentProcess());
  Header& header = GetHeader();
  if (header.mLocalFaceCount > 0) {
    return;  // already been done!
  }
  auto faceArray = ToTArray<nsTArray<nsCString>>(aLocalNameTable.Keys());
  faceArray.Sort();
  size_t count = faceArray.Length();
  Family* families = Families();
  fontlist::Pointer ptr = Alloc(count * sizeof(LocalFaceRec));
  auto* faces = ptr.ToArray<LocalFaceRec>(this, count);
  for (size_t i = 0; i < count; i++) {
    (void)new (&faces[i]) LocalFaceRec();
    const auto& rec = aLocalNameTable.Get(faceArray[i]);
    faces[i].mKey.Assign(faceArray[i], this);
    // Local face name records will refer to the canonical family name; we don't
    // need to search aliases here.
    const auto* family = FindFamily(rec.mFamilyName, /*aPrimaryNameOnly*/ true);
    if (!family) {
      // Skip this record if the family was excluded by the font whitelist pref.
      continue;
    }
    faces[i].mFamilyIndex = family - families;
    if (rec.mFaceIndex == uint32_t(-1)) {
      // The InitData record contains an mFaceDescriptor rather than an index,
      // so now we need to look for the appropriate index in the family.
      faces[i].mFaceIndex = 0;
      const Pointer* faceList =
          static_cast<const Pointer*>(family->Faces(this));
      for (uint32_t j = 0; j < family->NumFaces(); j++) {
        if (!faceList[j].IsNull()) {
          auto* f = faceList[j].ToPtr<const Face>(this);
          if (f && rec.mFaceDescriptor == f->mDescriptor.AsString(this)) {
            faces[i].mFaceIndex = j;
            break;
          }
        }
      }
    } else {
      faces[i].mFaceIndex = rec.mFaceIndex;
    }
  }
  header.mLocalFaces = ptr;
  header.mLocalFaceCount.store(count);
}

nsCString FontList::LocalizedFamilyName(const Family* aFamily) {
  // If the given family was created for an alternate locale or legacy name,
  // search for a standard family that corresponds to it. This is a linear
  // search of the font list, but (a) this is only used to show names in
  // Preferences, so is not performance-critical for layout etc.; and (b) few
  // such family names are normally present anyway, the vast majority of fonts
  // just have a single family name and we return it directly.
  if (aFamily->IsAltLocaleFamily()) {
    // Currently only the Windows backend actually does this; on other systems,
    // the family index is unused and will be kNoIndex for all fonts.
    if (aFamily->Index() != Family::kNoIndex) {
      const Family* families = Families();
      for (uint32_t i = 0; i < NumFamilies(); ++i) {
        if (families[i].Index() == aFamily->Index() &&
            families[i].IsBundled() == aFamily->IsBundled() &&
            !families[i].IsAltLocaleFamily()) {
          return families[i].DisplayName().AsString(this);
        }
      }
    }
  }

  // For standard families (or if we failed to find the expected standard
  // family for some reason), just return the DisplayName.
  return aFamily->DisplayName().AsString(this);
}

Family* FontList::FindFamily(const nsCString& aName, bool aPrimaryNameOnly) {
  struct FamilyNameComparator {
    FamilyNameComparator(FontList* aList, const nsCString& aTarget)
        : mList(aList), mTarget(aTarget) {}

    int operator()(const Family& aVal) const {
      return Compare(mTarget,
                     nsDependentCString(aVal.Key().BeginReading(mList)));
    }

   private:
    FontList* mList;
    const nsCString& mTarget;
  };

  const Header& header = GetHeader();

  Family* families = Families();
  if (!families) {
    return nullptr;
  }

  size_t match;
  if (BinarySearchIf(families, 0, header.mFamilyCount,
                     FamilyNameComparator(this, aName), &match)) {
    return &families[match];
  }

  if (aPrimaryNameOnly) {
    return nullptr;
  }

  if (header.mAliasCount) {
    Family* aliases = AliasFamilies();
    size_t match;
    if (aliases && BinarySearchIf(aliases, 0, header.mAliasCount,
                                  FamilyNameComparator(this, aName), &match)) {
      return &aliases[match];
    }
  }

#ifdef XP_WIN
  // For Windows only, because of how DWrite munges font family names in some
  // cases (see
  // https://msdnshared.blob.core.windows.net/media/MSDNBlogsFS/prod.evol.blogs.msdn.com/CommunityServer.Components.PostAttachments/00/02/24/90/36/WPF%20Font%20Selection%20Model.pdf
  // and discussion on the OpenType list), try stripping a possible style-name
  // suffix from the end of the requested family name.
  // After the deferred font loader has finished, this is no longer needed as
  // the "real" family names will have been found in AliasFamilies() above.
  if (aName.Contains(' ')) {
    auto pfl = gfxPlatformFontList::PlatformFontList();
    pfl->mLock.AssertCurrentThreadIn();
    if (header.mAliasCount) {
      // Aliases have been fully loaded by the parent process, so just discard
      // any stray mAliasTable and mLocalNameTable entries from earlier calls
      // to this code, and return.
      pfl->mAliasTable.Clear();
      pfl->mLocalNameTable.Clear();
      mFaceNamesRead.Clear();
      return nullptr;
    }

    // Do we already have an aliasData record for this name? If so, we just
    // return its base family.
    if (auto lookup = pfl->mAliasTable.Lookup(aName)) {
      return FindFamily(lookup.Data()->mBaseFamily, true);
    }

    // Strip the style suffix (after last space in the name) to get a "base"
    // family name.
    const char* data = aName.BeginReading();
    int32_t index = aName.Length();
    while (--index > 0) {
      if (data[index] == ' ') {
        break;
      }
    }
    if (index <= 0) {
      return nullptr;
    }
    nsAutoCString base(Substring(aName, 0, index));
    auto familyCount = header.mFamilyCount;
    if (BinarySearchIf(families, 0, familyCount,
                       FamilyNameComparator(this, base), &match)) {
      // Check to see if we have already read the face names for this base
      // family.

      // First, extend mFaceNamesRead as-needed up to `familyCount` (setting
      // new entries to 'false'):
      const auto oldLength = mFaceNamesRead.Length();
      mFaceNamesRead.EnsureLengthAtLeast(familyCount);
      for (auto i = oldLength; i < mFaceNamesRead.Length(); i++) {
        mFaceNamesRead[i] = false;
      }

      // Now we can check its entry at index `match` to see if we've already
      // read this one:
      if (mFaceNamesRead[match]) {
        return nullptr;
      }
      // This may be a possible base family to satisfy the search; call
      // ReadFaceNamesForFamily and see if the desired name ends up in
      // mAliasTable.
      // Note that ReadFaceNamesForFamily may store entries in mAliasTable
      // (and mLocalNameTable), but if this is happening in a content
      // process (which is the common case) those entries will not be saved
      // into the shared font list; they're just used here until the "real"
      // alias list is ready, then discarded.
      Family* baseFamily = &families[match];
      pfl->ReadFaceNamesForFamily(baseFamily, false);
      mFaceNamesRead[match] = true;
      if (auto lookup = pfl->mAliasTable.Lookup(aName)) {
        if (lookup.Data()->mFaces.Length() != baseFamily->NumFaces()) {
          // If the alias family doesn't have all the faces of the base family,
          // then style matching may end up resolving to a face that isn't
          // supposed to be available in the legacy styled family. To ensure
          // such mis-styling will get fixed, we start the async font info
          // loader (if it hasn't yet been triggered), which will pull in the
          // full metadata we need and then force a reflow.
          pfl->InitOtherFamilyNames(/* aDeferOtherFamilyNamesLoading */ true);
        }
        return baseFamily;
      }
    }
  }
#endif

  return nullptr;
}

LocalFaceRec* FontList::FindLocalFace(const nsCString& aName) {
  struct FaceNameComparator {
    FaceNameComparator(FontList* aList, const nsCString& aTarget)
        : mList(aList), mTarget(aTarget) {}

    int operator()(const LocalFaceRec& aVal) const {
      return Compare(mTarget,
                     nsDependentCString(aVal.mKey.BeginReading(mList)));
    }

   private:
    FontList* mList;
    const nsCString& mTarget;
  };

  Header& header = GetHeader();

  LocalFaceRec* faces = LocalFaces();
  size_t match;
  if (faces && BinarySearchIf(faces, 0, header.mLocalFaceCount,
                              FaceNameComparator(this, aName), &match)) {
    return &faces[match];
  }

  return nullptr;
}

void FontList::SearchForLocalFace(const nsACString& aName, Family** aFamily,
                                  Face** aFace) {
  Header& header = GetHeader();
  MOZ_ASSERT(header.mLocalFaceCount == 0,
             "do not use when local face names are already set up!");
  LOG_FONTLIST(("(shared-fontlist) local face search for (%s)",
                PromiseFlatCString(aName).get()));
  char initial = aName[0];
  Family* families = Families();
  if (!families) {
    return;
  }
  for (uint32_t i = 0; i < header.mFamilyCount; i++) {
    Family* family = &families[i];
    if (family->Key().BeginReading(this)[0] != initial) {
      continue;
    }
    LOG_FONTLIST(("(shared-fontlist) checking family (%s)",
                  family->Key().AsString(this).get()));
    if (!family->IsInitialized()) {
      if (!gfxPlatformFontList::PlatformFontList()->InitializeFamily(family)) {
        continue;
      }
    }
    Pointer* faces = family->Faces(this);
    if (!faces) {
      continue;
    }
    for (uint32_t j = 0; j < family->NumFaces(); j++) {
      auto* face = faces[j].ToPtr<Face>(this);
      if (!face) {
        continue;
      }
      nsAutoCString psname, fullname;
      if (gfxPlatformFontList::PlatformFontList()->ReadFaceNames(
              family, face, psname, fullname)) {
        LOG_FONTLIST(("(shared-fontlist) read psname (%s) fullname (%s)",
                      psname.get(), fullname.get()));
        ToLowerCase(psname);
        ToLowerCase(fullname);
        if (aName == psname || aName == fullname) {
          *aFamily = family;
          *aFace = face;
          return;
        }
      }
    }
  }
}

size_t FontList::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

size_t FontList::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t result = mBlocks.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& b : mBlocks) {
    result += aMallocSizeOf(b.get());
  }
  return result;
}

size_t FontList::AllocatedShmemSize() const {
  size_t result = 0;
  for (const auto& b : mBlocks) {
    result += b->BlockSize();
  }
  return result;
}

}  // namespace fontlist
}  // namespace mozilla
